#include <iostream>

#include "global.h"
#include "rocketmq_delay_scheduler.h"

int main(int argc, char* argv[]) {

    register_global_extensions();
    
    RocketMQDelayScheduler scheduler;
    if (!scheduler.init("../conf/schedulers/rocketmq_delay_scheduler.yml")) {
        SPDLOG_ERROR("Failed to initialize scheduler");
        return 1;
    }

    scheduler.start();

    std::cin.get();

    SPDLOG_WARN("Stopping scheduler...");

    scheduler.stop();

    return 0;
}