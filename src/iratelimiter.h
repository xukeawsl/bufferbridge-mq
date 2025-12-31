#pragma once

#include <memory>
#include <string>

#include "brpc/extension.h"
#include "spdlog/spdlog.h"

class IRateLimiter {
public:
    virtual ~IRateLimiter() noexcept = default;

    virtual bool init(const std::string& config) = 0;

    virtual bool is_allowed() = 0;

    virtual std::shared_ptr<IRateLimiter> clone() const = 0;
};

inline brpc::Extension<const IRateLimiter>* RateLimiterExtension() {
    return brpc::Extension<const IRateLimiter>::instance();
}