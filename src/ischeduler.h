#pragma once

#include <memory>
#include <string>

#include "brpc/extension.h"

namespace bmq {

class IScheduler {
public:
    virtual ~IScheduler() noexcept = default;

    virtual bool init(const std::string& name, const std::string& config) = 0;

    virtual void start() = 0;

    virtual void stop() = 0;

    virtual std::shared_ptr<IScheduler> clone() const = 0;
};

}    // namespace bmq

inline brpc::Extension<const bmq::IScheduler>* SchedulerExtension() {
    return brpc::Extension<const bmq::IScheduler>::instance();
}