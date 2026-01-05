#pragma once

#include <chrono>
#include <mutex>

#include "iratelimiter.h"

namespace bmq {

class LocalRateLimiter : public bmq::IRateLimiter {
public:
    LocalRateLimiter()
        : _initialized(false),
          _tokens_per_second(0.0),
          _capacity(0.0),
          _tokens(0.0),
          _last_refill_time(std::chrono::steady_clock::now()) {}

    bool init(const std::string& config) override;

    bool is_allowed() override;

    std::shared_ptr<bmq::IRateLimiter> clone() const override {
        return std::dynamic_pointer_cast<bmq::IRateLimiter>(
            std::make_shared<LocalRateLimiter>());
    }

private:
    std::mutex _mtx;
    bool _initialized;
    double _tokens_per_second;
    double _capacity;
    double _tokens;
    std::chrono::steady_clock::time_point _last_refill_time;
};

}    // namespace bmq