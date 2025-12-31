#pragma once

#include <gflags/gflags.h>

#include "brpc/channel.h"
#include "iratelimiter.h"

class RedisRateLimiter : public IRateLimiter {
public:
    RedisRateLimiter()
        : _initialized(false), _tokens_per_second(0.0), _capacity(0.0) {}

    bool init(const std::string& config) override;

    bool is_allowed() override;

    std::shared_ptr<IRateLimiter> clone() const override {
        return std::dynamic_pointer_cast<IRateLimiter>(
            std::make_shared<RedisRateLimiter>());
    }

private:
    bool _initialized;
    std::string _lua_script;
    std::string _lua_script_sha1;
    std::string _bucket_key;
    double _tokens_per_second;
    double _capacity;
    brpc::Channel _redis_channel;
};