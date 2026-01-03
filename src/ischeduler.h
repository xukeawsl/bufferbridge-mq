#pragma once

#include <memory>
#include <string>

#include "brpc/extension.h"

class IScheduler {
public:
    virtual ~IScheduler() noexcept = default;

    virtual bool init(const std::string& name, const std::string& config) = 0;

    virtual void start() = 0;

    virtual void stop() = 0;

    virtual std::shared_ptr<IScheduler> clone() const = 0;
};

inline brpc::Extension<const IScheduler>* SchedulerExtension() {
    return brpc::Extension<const IScheduler>::instance();
}