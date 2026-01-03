#include <iostream>

#include "global.h"
#include "hot_loader.h"
#include "rocketmq_delay_scheduler.h"

int main(int argc, char* argv[]) {
    if (!global_init()) {
        SPDLOG_ERROR("Global initialization failed");
        return 1;
    }

    RocketMQDelayScheduler scheduler;
    if (!scheduler.init("../conf/schedulers/rocketmq_delay_scheduler.yml")) {
        SPDLOG_ERROR("Failed to initialize scheduler");
        global_destroy();
        return 1;
    }

    scheduler.start();

    SPDLOG_INFO("Scheduler started. Press Enter to stop...");

    std::cin.get();

    SPDLOG_WARN("Stopping scheduler...");

    scheduler.stop();

    global_destroy();

    return 0;
}