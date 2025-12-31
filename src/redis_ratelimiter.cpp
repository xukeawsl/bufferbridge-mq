#include "redis_ratelimiter.h"

#include <fstream>

#include "brpc/redis.h"
#include "nlohmann/json.hpp"

DEFINE_string(limiter_script_path, "../conf/redis_rate_limiter.lua",
              "Path to the Lua script for Redis rate limiting");
DEFINE_string(limiter_redis_address, "127.0.0.1:6379",
              "Address of the Redis server for rate limiting");
DEFINE_string(limiter_redis_password, "",
              "Password for the Redis server for rate limiting");
DEFINE_int32(limiter_script_load_timeout_ms, 1000,
             "Timeout in milliseconds for loading the Lua script into Redis");
DEFINE_int32(limiter_check_timeout_ms, 100,
             "Timeout in milliseconds for each is_allowed check");

bool RedisRateLimiter::init(const std::string& config) {
    try {
        auto json_config = nlohmann::json::parse(config);

        std::string lua_script_path = FLAGS_limiter_script_path;
        if (json_config.contains("script_path")) {
            lua_script_path = json_config["script_path"].get<std::string>();
        }

        if (lua_script_path.empty()) {
            SPDLOG_ERROR("RedisRateLimiter init failed: script_path is empty");
            return false;
        }

        std::ifstream ifs(lua_script_path, std::ios::in | std::ios::binary);

        if (!ifs) {
            SPDLOG_ERROR(
                "RedisRateLimiter init failed: cannot open Lua script file at "
                "{}",
                lua_script_path);
            return false;
        }

        std::ostringstream oss;
        oss << ifs.rdbuf();
        _lua_script = oss.str();

        std::string redis_address = FLAGS_limiter_redis_address;
        if (json_config.contains("redis_address")) {
            redis_address = json_config["redis_address"].get<std::string>();
        }

        std::string redis_password = FLAGS_limiter_redis_password;
        if (json_config.contains("redis_password")) {
            redis_password = json_config["redis_password"].get<std::string>();
        }

        std::string bucket_key =
            json_config.at("bucket_key").get<std::string>();
        if (bucket_key.empty()) {
            SPDLOG_ERROR("RedisRateLimiter init failed: bucket_key is empty");
            return false;
        }
        double tokens_per_second = json_config["rate"].get<double>();
        if (std::abs(tokens_per_second) < 1e-6) {
            SPDLOG_ERROR(
                "RedisRateLimiter init failed: rate must be greater than 0");
            return false;
        }
        double capacity = 0.0;
        if (json_config.contains("burst")) {
            capacity = json_config["burst"].get<double>();
        }
        capacity = std::max(capacity, tokens_per_second);

        _bucket_key = bucket_key;
        _tokens_per_second = tokens_per_second;
        _capacity = capacity;

        // init redis channel
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_REDIS;
        options.max_retry = 3;
        options.connect_timeout_ms = 500;

        if (_redis_channel.Init(redis_address.c_str(), &options) != 0) {
            SPDLOG_ERROR(
                "RedisRateLimiter init failed: cannot connect to Redis at {}",
                redis_address);
            return false;
        }

        if (!redis_password.empty()) {
            brpc::Controller cntl;
            brpc::RedisRequest request;
            brpc::RedisResponse response;

            request.AddCommand("AUTH " + redis_password);
            _redis_channel.CallMethod(nullptr, &cntl, &request, &response,
                                      nullptr);

            if (cntl.Failed() || response.reply_size() == 0 ||
                response.reply(0).type() != brpc::REDIS_REPLY_STATUS ||
                response.reply(0).data() != "OK") {
                SPDLOG_ERROR(
                    "RedisRateLimiter init failed: Redis AUTH failed: {}",
                    cntl.ErrorText());
                return false;
            }
        }

        {
            brpc::Controller cntl;
            brpc::RedisRequest request;
            brpc::RedisResponse response;

            cntl.set_timeout_ms(
                FLAGS_limiter_script_load_timeout_ms);    // 1 second timeout
                                                          // for script loading

            request.AddCommand("SCRIPT LOAD %b", _lua_script.data(),
                               _lua_script.size());
            _redis_channel.CallMethod(nullptr, &cntl, &request, &response,
                                      nullptr);

            if (cntl.Failed() || response.reply_size() == 0 ||
                response.reply(0).type() != brpc::REDIS_REPLY_STRING) {
                SPDLOG_ERROR(
                    "RedisRateLimiter init failed: cannot load Lua script into "
                    "Redis: {}",
                    cntl.ErrorText());
                return false;
            }

            _lua_script_sha1 = response.reply(0).data().as_string();
        }

        _initialized = true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("RedisRateLimiter init failed: {}", e.what());
        return false;
    }
    return true;
}

bool RedisRateLimiter::is_allowed() {
    if (!_initialized) {
        return true;
    }

    brpc::Controller cntl;
    brpc::RedisRequest request;
    brpc::RedisResponse response;

    cntl.set_timeout_ms(
        FLAGS_limiter_check_timeout_ms);    // 100 ms timeout for each
                                            // is_allowed check

    request.AddCommand("EVALSHA %s 1 %s %lf %lf %lld", _lua_script_sha1.c_str(),
                       _bucket_key.c_str(), _tokens_per_second, _capacity,
                       butil::gettimeofday_ms());

    _redis_channel.CallMethod(nullptr, &cntl, &request, &response, nullptr);

    if (cntl.Failed() || response.reply_size() == 0) {
        SPDLOG_ERROR("RedisRateLimiter is_allowed failed: {}",
                     cntl.ErrorText());
        return true;
    }

    if (response.reply(0).is_error()) {
        SPDLOG_WARN("RedisRateLimiter is_allowed got error reply: {}",
                    response.reply(0).error_message());

        cntl.Reset();
        request.Clear();
        response.Clear();

        std::vector<butil::StringPiece> components;
        components.emplace_back("EVAL");
        components.emplace_back(_lua_script);
        components.emplace_back("1");
        components.emplace_back(_bucket_key);
        components.emplace_back(std::to_string(_tokens_per_second));
        components.emplace_back(std::to_string(_capacity));
        components.emplace_back(std::to_string(butil::gettimeofday_ms()));

        request.AddCommandByComponents(components.data(), components.size());

        _redis_channel.CallMethod(nullptr, &cntl, &request, &response, nullptr);

        if (cntl.Failed() || response.reply_size() == 0 ||
            response.reply(0).is_error()) {
            SPDLOG_ERROR("RedisRateLimiter is_allowed fallback failed: {}",
                         cntl.ErrorText());
            return true;
        }
    }

    if (response.reply(0).type() != brpc::REDIS_REPLY_INTEGER) {
        SPDLOG_ERROR("RedisRateLimiter is_allowed unexpected reply type: {}",
                     response.reply(0).type());
        return true;
    }

    if (response.reply(0).integer() == 0) {
        return false;
    }

    return true;
}