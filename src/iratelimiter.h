#pragma once

#include <memory>
#include <string>

#include "brpc/extension.h"
#include "spdlog/spdlog.h"

namespace bmq {

class IRateLimiter {
public:
    virtual ~IRateLimiter() noexcept = default;

    virtual bool init(const std::string& config) = 0;

    virtual bool is_allowed() = 0;

    virtual std::shared_ptr<IRateLimiter> clone() const = 0;
};

}    // namespace bmq

inline brpc::Extension<const bmq::IRateLimiter>* RateLimiterExtension() {
    return brpc::Extension<const bmq::IRateLimiter>::instance();
}