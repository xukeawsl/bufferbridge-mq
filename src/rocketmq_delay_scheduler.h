#pragma once

#include <memory>

#include "butil/containers/doubly_buffered_data.h"
#include "iratelimiter.h"
#include "ischeduler.h"
#include "rocketmq/ErrorCode.h"
#include "rocketmq/Logger.h"
#include "rocketmq/Message.h"
#include "rocketmq/Producer.h"
#include "rocketmq/SimpleConsumer.h"

struct RocketMQDelaySchedulerConfig {
    std::size_t worker_threads{std::thread::hardware_concurrency()};
    std::size_t scheduler_interval_seconds;

    std::string buffer_consumer_group;
    std::string buffer_consumer_access_point;
    std::string buffer_consumer_topic;
    std::size_t buffer_consumer_await_duration;
    std::size_t buffer_consumer_batch_size;
    std::size_t buffer_consumer_invisible_duration;

    std::string target_producer_access_point;
    std::string target_producer_topic;

    std::shared_ptr<rocketmq::SimpleConsumer> buffer_mq_consumer;
    std::shared_ptr<rocketmq::Producer> target_mq_producer;

    struct TimeWindow {
        short start;    // "05:30" -> 530
        short end;      // "09:30" -> 930
        std::shared_ptr<IRateLimiter> rate_limiter;
        bool enable;
    };

    std::vector<TimeWindow> time_windows;
};

class RocketMQDelayScheduler : public IScheduler {
public:
    RocketMQDelayScheduler();

    ~RocketMQDelayScheduler();

    bool init(const std::string& config) override;

    void start() override;

    void stop() override;

    std::shared_ptr<IScheduler> clone() const override {
        return std::dynamic_pointer_cast<IScheduler>(
            std::make_shared<RocketMQDelayScheduler>());
    }

private:
    void worker_thread_func();

    static bool modify(RocketMQDelaySchedulerConfig& bg_cfg,
                       const RocketMQDelaySchedulerConfig& new_cfg) {
        bg_cfg = new_cfg;
        return true;
    }

private:
    std::atomic<bool> _running;
    std::vector<std::thread> _worker_threads;
    butil::DoublyBufferedData<RocketMQDelaySchedulerConfig> _cfg;
};