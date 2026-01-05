#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ischeduler.h"

namespace bmq {

class SchedulerManager {
public:
    SchedulerManager() = default;
    ~SchedulerManager();

    // 从主配置文件加载所有调度器
    bool load_from_config(const std::string& config_file);

    // 启动所有调度器
    void start_all();

    // 停止所有调度器
    void stop_all();

    // 获取调度器数量
    size_t get_scheduler_count() const { return _schedulers.size(); }

private:
    struct SchedulerInstance {
        std::string name;
        std::shared_ptr<bmq::IScheduler> scheduler;
        std::string config_file;
    };

    std::vector<SchedulerInstance> _schedulers;
};

}    // namespace bmq
