#include <iostream>

#include "global.h"
#include "hot_loader.h"
#include "scheduler_manager.h"

int main(int argc, char* argv[]) {
    if (!global_init()) {
        SPDLOG_ERROR("Global initialization failed");
        return 1;
    }

    SchedulerManager manager;

    // 从主配置文件加载所有调度器
    std::string config_file = "../conf/conf.yml";
    if (!manager.load_from_config(config_file)) {
        SPDLOG_ERROR("Failed to load schedulers from config: {}", config_file);
        global_destroy();
        return 1;
    }

    SPDLOG_INFO("Loaded {} scheduler(s)", manager.get_scheduler_count());

    // 启动所有调度器
    manager.start_all();

    SPDLOG_INFO("All schedulers started. Press Enter to stop...");

    std::cin.get();

    SPDLOG_WARN("Stopping all schedulers...");

    // 停止所有调度器
    manager.stop_all();

    global_destroy();

    return 0;
}