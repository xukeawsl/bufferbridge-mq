#include "rocketmq_delay_scheduler.h"

#include <set>

#include "global.h"
#include "nlohmann/json.hpp"
#include "yaml-cpp/yaml.h"

static short get_current_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm = *std::localtime(&now_c);
    return static_cast<short>(local_tm.tm_hour * 100 + local_tm.tm_min);
}

static short time_str_to_short(const std::string& time_str) {
    if (time_str.length() != 5 || time_str[2] != ':') {
        throw std::runtime_error("Invalid time format: " + time_str);
    }

    int hour = std::stoi(time_str.substr(0, 2));
    int minute = std::stoi(time_str.substr(3, 2));

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        throw std::runtime_error("Invalid time value: " + time_str);
    }

    return static_cast<short>(hour * 100 + minute);
}

static void validate_time_windows(
    std::vector<RocketMQDelaySchedulerConfig::TimeWindow>& time_windows) {
    std::sort(time_windows.begin(), time_windows.end(),
              [](const RocketMQDelaySchedulerConfig::TimeWindow& a,
                 const RocketMQDelaySchedulerConfig::TimeWindow& b) {
                  return a.start < b.start;
              });

    for (std::size_t i = 0; i < time_windows.size(); ++i) {
        const auto& window = time_windows[i];
        if (i > 0) {
            const auto& prev_window = time_windows[i - 1];
            if (window.start <= prev_window.end) {
                throw std::runtime_error(
                    "Overlapping time windows: [" +
                    std::to_string(prev_window.start) + ", " +
                    std::to_string(prev_window.end) + "] and [" +
                    std::to_string(window.start) + ", " +
                    std::to_string(window.end) + "]");
            }
        }

        if (window.start >= window.end) {
            throw std::runtime_error(
                "Invalid time window: start " + std::to_string(window.start) +
                " should be less than end " + std::to_string(window.end));
        }
    }
}

RocketMQDelayScheduler::RocketMQDelayScheduler() : _running(false) {}

RocketMQDelayScheduler::~RocketMQDelayScheduler() { stop(); }

bool RocketMQDelayScheduler::init(const std::string& name,
                                  const std::string& config) {
    // 保存调度器名称和配置文件路径
    _name = name;
    _config_file = config;

    try {
        YAML::Node config_node = YAML::LoadFile(config);

        RocketMQDelaySchedulerConfig cfg;

        if (config_node["worker_threads"].IsDefined()) {
            cfg.worker_threads =
                config_node["worker_threads"].as<std::size_t>();
        }

        if (cfg.worker_threads == 0) {
            cfg.worker_threads = std::thread::hardware_concurrency();
        }

        if (config_node["scheduler_interval_seconds"].IsDefined()) {
            cfg.scheduler_interval_seconds =
                config_node["scheduler_interval_seconds"].as<std::size_t>();
        } else {
            SPDLOG_ERROR("scheduler_interval_seconds is not defined in config");
            return false;
        }

        if (cfg.scheduler_interval_seconds == 0) {
            SPDLOG_ERROR("scheduler_interval_seconds must be greater than 0");
            return false;
        }

        YAML::Node rocketmq_node = config_node["rocketmq"];

        if (rocketmq_node["buffer_consumer_topic"].IsDefined()) {
            cfg.buffer_consumer_topic =
                rocketmq_node["buffer_consumer_topic"].as<std::string>();
        } else {
            SPDLOG_ERROR("buffer_consumer_topic is not defined in config");
            return false;
        }

        if (rocketmq_node["buffer_consumer_access_point"].IsDefined()) {
            cfg.buffer_consumer_access_point =
                rocketmq_node["buffer_consumer_access_point"].as<std::string>();
        } else {
            SPDLOG_ERROR(
                "buffer_consumer_access_point is not defined in config");
            return false;
        }

        if (rocketmq_node["buffer_consumer_group"].IsDefined()) {
            cfg.buffer_consumer_group =
                rocketmq_node["buffer_consumer_group"].as<std::string>();
        } else {
            SPDLOG_ERROR("buffer_consumer_group is not defined in config");
            return false;
        }

        if (rocketmq_node["buffer_consumer_await_duration"].IsDefined()) {
            cfg.buffer_consumer_await_duration =
                rocketmq_node["buffer_consumer_await_duration"]
                    .as<std::size_t>();
        } else {
            SPDLOG_ERROR(
                "buffer_consumer_await_duration is not defined in config");
            return false;
        }

        if (cfg.buffer_consumer_await_duration == 0) {
            SPDLOG_ERROR(
                "buffer_consumer_await_duration must be greater than 0");
            return false;
        }

        if (rocketmq_node["buffer_consumer_batch_size"].IsDefined()) {
            cfg.buffer_consumer_batch_size =
                rocketmq_node["buffer_consumer_batch_size"].as<std::size_t>();
        } else {
            SPDLOG_ERROR("buffer_consumer_batch_size is not defined in config");
            return false;
        }

        if (cfg.buffer_consumer_batch_size == 0) {
            SPDLOG_ERROR("buffer_consumer_batch_size must be greater than 0");
            return false;
        }

        if (rocketmq_node["buffer_consumer_invisible_duration"].IsDefined()) {
            cfg.buffer_consumer_invisible_duration =
                rocketmq_node["buffer_consumer_invisible_duration"]
                    .as<std::size_t>();
        } else {
            SPDLOG_ERROR(
                "buffer_consumer_invisible_duration is not defined in config");
            return false;
        }

        if (cfg.buffer_consumer_invisible_duration <= 10) {
            SPDLOG_ERROR(
                "buffer_consumer_invisible_duration must be greater than 10");
            return false;
        }

        if (rocketmq_node["target_producer_access_point"].IsDefined()) {
            cfg.target_producer_access_point =
                rocketmq_node["target_producer_access_point"].as<std::string>();
        } else {
            SPDLOG_ERROR(
                "target_producer_access_point is not defined in config");
            return false;
        }

        if (rocketmq_node["target_producer_topic"].IsDefined()) {
            cfg.target_producer_topic =
                rocketmq_node["target_producer_topic"].as<std::string>();
        } else {
            SPDLOG_ERROR("target_producer_topic is not defined in config");
            return false;
        }

        auto consumer =
            rocketmq::SimpleConsumer::newBuilder()
                .withGroup(cfg.buffer_consumer_group)
                .withConfiguration(
                    rocketmq::Configuration::newBuilder()
                        .withEndpoints(cfg.buffer_consumer_access_point)
                        .withSsl(false)
                        .build())
                .subscribe(cfg.buffer_consumer_topic, std::string("*"))
                .withAwaitDuration(
                    std::chrono::seconds(cfg.buffer_consumer_await_duration))
                .build();

        cfg.buffer_mq_consumer =
            std::make_shared<rocketmq::SimpleConsumer>(std::move(consumer));

        auto producer =
            rocketmq::Producer::newBuilder()
                .withConfiguration(
                    rocketmq::Configuration::newBuilder()
                        .withEndpoints(cfg.target_producer_access_point)
                        .withSsl(false)
                        .build())
                .withTopics({cfg.target_producer_topic})
                .build();

        cfg.target_mq_producer =
            std::make_shared<rocketmq::Producer>(std::move(producer));

        YAML::Node time_windows_node = config_node["time_windows"];

        // 用于检查时间窗口 id 重复
        std::set<std::string> window_ids;

        for (const auto& time_window_node : time_windows_node) {
            RocketMQDelaySchedulerConfig::TimeWindow window;

            // 读取时间窗口 id（必需）
            if (!time_window_node["id"].IsDefined()) {
                SPDLOG_ERROR("Time window 'id' is required");
                return false;
            }

            // 支持 int 和 string 两种类型
            if (time_window_node["id"].Type() == YAML::NodeType::Scalar) {
                try {
                    window.id =
                        std::to_string(time_window_node["id"].as<int>());
                } catch (...) {
                    window.id = time_window_node["id"].as<std::string>();
                }
            } else {
                window.id = time_window_node["id"].as<std::string>();
            }

            // 检查时间窗口 id 是否重复
            if (window_ids.find(window.id) != window_ids.end()) {
                SPDLOG_ERROR("Duplicate time window id '{}'", window.id);
                return false;
            }
            window_ids.insert(window.id);

            std::string start_str = time_window_node["start"].as<std::string>();
            std::string end_str = time_window_node["end"].as<std::string>();
            window.start = time_str_to_short(start_str);
            window.end = time_str_to_short(end_str);

            if (time_window_node["rate_limiter_config"].IsDefined()) {
                std::string rate_limiter_config =
                    time_window_node["rate_limiter_config"].as<std::string>();

                // 获取限流器类型，默认为 "local"
                std::string rate_limiter_type = "local";
                if (time_window_node["rate_limiter_type"].IsDefined()) {
                    rate_limiter_type =
                        time_window_node["rate_limiter_type"].as<std::string>();
                }

                // 如果是 Redis 限流器，自动设置 bucket_key 为
                // scheduler_name:window_id
                if (rate_limiter_type == "redis") {
                    try {
                        auto json_config =
                            nlohmann::json::parse(rate_limiter_config);
                        std::string bucket_key = _name + ":" + window.id;
                        json_config["bucket_key"] = bucket_key;
                        rate_limiter_config = json_config.dump();
                    } catch (const std::exception& e) {
                        SPDLOG_ERROR(
                            "Failed to set bucket_key for Redis rate limiter: "
                            "{}",
                            e.what());
                        return false;
                    }
                }

                const IRateLimiter* rate_limiter_ext =
                    RateLimiterExtension()->Find(rate_limiter_type.c_str());
                if (rate_limiter_ext) {
                    auto rate_limiter = rate_limiter_ext->clone();
                    if (rate_limiter->init(rate_limiter_config)) {
                        window.rate_limiter = rate_limiter;
                    } else {
                        SPDLOG_ERROR(
                            "Failed to initialize rate limiter '{}' for time "
                            "window "
                            "[{} - {}]",
                            rate_limiter_type, start_str, end_str);
                        return false;
                    }
                } else {
                    SPDLOG_ERROR(
                        "Rate limiter extension '{}' not found for time "
                        "window [{} - {}]",
                        rate_limiter_type, start_str, end_str);
                    return false;
                }
            }

            window.enable = time_window_node["enable"].as<bool>();
            cfg.time_windows.push_back(window);
        }

        validate_time_windows(cfg.time_windows);

        _cfg.Modify(modify, cfg);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to initialize RocketMQDelayScheduler: {}",
                     e.what());
        return false;
    }

    return true;
}

void RocketMQDelayScheduler::start() {
    if (_running) {
        SPDLOG_WARN("RocketMQDelayScheduler is already running");
        return;
    }

    butil::DoublyBufferedData<RocketMQDelaySchedulerConfig>::ScopedPtr cfg_ptr;
    if (_cfg.Read(&cfg_ptr)) {
        SPDLOG_ERROR("Failed to read configuration for RocketMQDelayScheduler");
        return;
    }

    _running = true;

    for (std::size_t i = 0; i < cfg_ptr->worker_threads; ++i) {
        _worker_threads.emplace_back(
            &RocketMQDelayScheduler::worker_thread_func, this);
    }

    // 启动工作线程后再启用配置热加载
    enable_hot_reload();
}

void RocketMQDelayScheduler::stop() {
    if (!_running) {
        return;
    }

    _running = false;

    // 注销热加载任务
    HotLoader::instance().unregister_task(_hot_load_task.get());
    _hot_load_task.reset();

    for (auto& thread : _worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void RocketMQDelayScheduler::worker_thread_func() {
    while (_running) {
        // 快速读取配置并复制到本地变量
        RocketMQDelaySchedulerConfig local_cfg;

        {
            butil::DoublyBufferedData<RocketMQDelaySchedulerConfig>::ScopedPtr
                cfg_ptr;
            if (_cfg.Read(&cfg_ptr)) {
                SPDLOG_ERROR(
                    "Failed to read configuration for RocketMQDelayScheduler");
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            // 立即复制配置到本地，最小化持有读锁的时间
            local_cfg = *cfg_ptr;
        }    // ScopedPtr 在这里析构，立即释放读锁

        short current_time = get_current_time();

        bool hit_flag = false;
        std::shared_ptr<IRateLimiter> current_rate_limiter;
        for (const auto& window : local_cfg.time_windows) {
            if (window.enable && current_time >= window.start &&
                current_time <= window.end) {
                hit_flag = true;
                current_rate_limiter = window.rate_limiter;
                break;
            }
        }

        if (!hit_flag) {
            std::this_thread::sleep_for(
                std::chrono::seconds(local_cfg.scheduler_interval_seconds));
            continue;
        }

        if (current_rate_limiter) {
            if (!current_rate_limiter->is_allowed()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
        }

        std::vector<rocketmq::MessageConstSharedPtr> messages;
        std::error_code ec;
        local_cfg.buffer_mq_consumer->receive(
            local_cfg.buffer_consumer_batch_size,
            std::chrono::seconds(local_cfg.buffer_consumer_invisible_duration),
            ec, messages);

        if (ec) {
            SPDLOG_ERROR("Failed to receive messages from buffer MQ: {}",
                         ec.message());
            std::this_thread::sleep_for(
                std::chrono::seconds(local_cfg.scheduler_interval_seconds));
            continue;
        }

        if (messages.empty()) {
            std::this_thread::sleep_for(
                std::chrono::seconds(local_cfg.scheduler_interval_seconds));
            continue;
        }

        for (const auto& message : messages) {
            auto new_message = rocketmq::Message::newBuilder()
                                   .withTopic(local_cfg.target_producer_topic)
                                   .withTag(message->tag())
                                   .withKeys(message->keys())
                                   .withBody(message->body())
                                   .build();

            std::error_code send_ec;
            rocketmq::SendReceipt send_receipt =
                local_cfg.target_mq_producer->send(std::move(new_message),
                                                   send_ec);

            if (send_ec) {
                SPDLOG_ERROR("Failed to send message to target MQ: {}",
                             send_ec.message());
                continue;
            } else {
                SPDLOG_INFO(
                    "Successfully sent message to topic {}. Message ID: {}",
                    local_cfg.target_producer_topic, send_receipt.message_id);
            }

            std::string receipt_handle = message->extension().receipt_handle;
            std::error_code ack_ec;
            local_cfg.buffer_mq_consumer->ack(*message, ack_ec);
            if (ack_ec) {
                SPDLOG_ERROR("Failed to ack message in buffer MQ: {}",
                             ack_ec.message());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void RocketMQDelayScheduler::reload_config() {
    SPDLOG_INFO("Reloading configuration from: {}", _config_file);

    if (_config_file.empty()) {
        SPDLOG_ERROR("Config file path is empty, cannot reload");
        return;
    }

    // 复用 init 方法重新加载配置
    // 由于 init 使用 _cfg.Modify() 更新配置，是线程安全的
    if (init(_name, _config_file)) {
        SPDLOG_INFO("Configuration reloaded successfully");
    } else {
        SPDLOG_INFO("Failed to reload configuration");
    }
}

void RocketMQDelayScheduler::enable_hot_reload() {
    if (_config_file.empty()) {
        SPDLOG_ERROR("Config file path is empty, cannot enable hot reload");
        return;
    }

    if (_hot_load_task) {
        SPDLOG_WARN("Hot reload is already enabled");
        return;
    }

    // 创建热加载任务
    _hot_load_task =
        std::make_unique<RocketMQDelaySchedulerHotLoadTask>(this, _config_file);

    // 注册到 HotLoader
    if (HotLoader::instance().register_task(_hot_load_task.get(),
                                            HotLoader::DOESNT_OWN_TASK) != 0) {
        SPDLOG_ERROR("Failed to register hot load task");
        _hot_load_task.reset();
        return;
    }

    SPDLOG_INFO("Hot reload enabled for config file: {}", _config_file);
}