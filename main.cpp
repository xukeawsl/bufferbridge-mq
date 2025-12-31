#include <iostream>

#include "rocketmq_delay_scheduler.h"

int main(int argc, char* argv[]) {
    RocketMQDelayScheduler scheduler;
    if (!scheduler.init("../conf/schedulers/rocketmq_delay_scheduler.yml")) {
        SPDLOG_ERROR("Failed to initialize scheduler");
        return 1;
    }

    scheduler.start();

    std::cin.get();

    SPDLOG_WARN("Stopping scheduler...");

    scheduler.stop();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}