#include "local_ratelimiter.h"

#include "nlohmann/json.hpp"

bool LocalRateLimiter::init(const std::string& config) {
    try {
        auto json_config = nlohmann::json::parse(config);
        double tokens_per_second = json_config["rate"].get<double>();

        if (std::abs(tokens_per_second) < 1e-6) {
            SPDLOG_ERROR(
                "LocalRateLimiter init failed: rate must be greater than 0");
            return false;
        }

        double capacity = 0.0;
        if (json_config.contains("burst")) {
            capacity = json_config["burst"].get<double>();
        }

        capacity = std::max(capacity, tokens_per_second);

        _tokens_per_second = tokens_per_second;
        _capacity = capacity;
        _tokens = _capacity;
        _last_refill_time = std::chrono::steady_clock::now();
        _initialized = true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("LocalRateLimiter init failed: {}", e.what());
        return false;
    }

    return true;
}

bool LocalRateLimiter::is_allowed() {
    if (!_initialized) {
        return true;
    }

    std::lock_guard<std::mutex> lock(_mtx);
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> elapsed_seconds = now - _last_refill_time;
    double tokens_to_add = elapsed_seconds.count() * _tokens_per_second;
    _tokens = std::min(_capacity, _tokens + tokens_to_add);
    _last_refill_time = now;

    if (_tokens >= 1.0) {
        _tokens -= 1.0;
        return true;
    }

    return false;
}