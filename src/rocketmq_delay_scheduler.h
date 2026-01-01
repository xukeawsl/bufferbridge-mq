#pragma once

#include <memory>
#include <string>

#include "butil/containers/doubly_buffered_data.h"
#include "hot_loader.h"
#include "iratelimiter.h"
#include "ischeduler.h"
#include "rocketmq/ErrorCode.h"
#include "rocketmq/Logger.h"
#include "rocketmq/Message.h"
#include "rocketmq/Producer.h"
#include "rocketmq/SimpleConsumer.h"

// 前置声明
class RocketMQDelaySchedulerHotLoadTask;

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

    // 重新加载配置（线程安全）
    void reload_config();

private:
    void worker_thread_func();

    void enable_hot_reload();

    static bool modify(RocketMQDelaySchedulerConfig& bg_cfg,
                       const RocketMQDelaySchedulerConfig& new_cfg) {
        bg_cfg = new_cfg;
        return true;
    }

private:
    std::atomic<bool> _running;
    std::vector<std::thread> _worker_threads;
    butil::DoublyBufferedData<RocketMQDelaySchedulerConfig> _cfg;
    std::string _config_file;  // 配置文件路径，用于热加载
    std::unique_ptr<RocketMQDelaySchedulerHotLoadTask> _hot_load_task;  // 热加载任务
};

// 热加载任务类
class RocketMQDelaySchedulerHotLoadTask : public HotLoadTask {
public:
    RocketMQDelaySchedulerHotLoadTask(RocketMQDelayScheduler* scheduler,
                                     const std::string& file)
        : HotLoadTask(file), _scheduler(scheduler) {}

    void on_reload() override {
        SPDLOG_INFO("Hot reload triggered for config file: {}", watch_file());
        _scheduler->reload_config();
    }

private:
    RocketMQDelayScheduler* _scheduler;
};