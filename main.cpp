#include <iostream>

#include "global.h"
#include "hot_loader.h"
#include "rocketmq_delay_scheduler.h"

int main(int argc, char* argv[]) {

    register_global_extensions();

    // 初始化 HotLoader
    if (HotLoader::instance().init() != 0) {
        SPDLOG_ERROR("Failed to initialize HotLoader");
        return 1;
    }

    // 启动 HotLoader
    if (HotLoader::instance().run() != 0) {
        SPDLOG_ERROR("Failed to start HotLoader");
        return 1;
    }

    RocketMQDelayScheduler scheduler;
    if (!scheduler.init("../conf/schedulers/rocketmq_delay_scheduler.yml")) {
        SPDLOG_ERROR("Failed to initialize scheduler");
        HotLoader::instance().stop();
        return 1;
    }

    scheduler.start();

    SPDLOG_INFO("Scheduler started. Press Enter to stop...");

    std::cin.get();

    SPDLOG_WARN("Stopping scheduler...");

    scheduler.stop();

    // 停止 HotLoader
    HotLoader::instance().stop();

    return 0;
}