#include "global.h"

#include "local_ratelimiter.h"
#include "redis_ratelimiter.h"
#include "rocketmq_delay_scheduler.h"

DEFINE_string(rocketmq_log_path, "./logs/rocketmq.log", "RocketMQ log file path");
DEFINE_uint64(rocketmq_log_file_size, 50 * 1024 * 1024, "RocketMQ log file size in bytes");
DEFINE_uint64(rocketmq_log_file_count, 5, "RocketMQ log file count");
DEFINE_string(log_file, "./logs/bufferbridge.log", "BufferBridge log file path");
DEFINE_uint64(log_rotate_size, 10 * 1024 * 1024, "Log rotate size in bytes");
DEFINE_uint32(log_rotate_count, 5, "Log rotate file count");
DEFINE_uint32(log_thread_pool_q_size, 8192, "Log thread pool queue size");
DEFINE_uint32(log_thread_pool_t_num, 1, "Log thread pool thread number");

struct GlobalExtensions {
    LocalRateLimiter local_rate_limiter;
    RedisRateLimiter redis_rate_limiter;

    RocketMQDelayScheduler rocketmq_delay_scheduler;
};

static GlobalExtensions g_global_extensions;

void register_global_extensions() {
    RateLimiterExtension()->RegisterOrDie(
        "local", &g_global_extensions.local_rate_limiter);

    RateLimiterExtension()->RegisterOrDie(
        "redis", &g_global_extensions.redis_rate_limiter);

    SchedulerExtension()->RegisterOrDie(
        "rocketmq_delay_scheduler",
        &g_global_extensions.rocketmq_delay_scheduler);
}

bool init_rocketmq_logger() {
    try {
        auto& logger = rocketmq::getLogger();
        logger.setLogHome(FLAGS_rocketmq_log_path);
        logger.setConsoleLevel(rocketmq::Level::Warn);
        logger.setLevel(rocketmq::Level::Info);
        logger.setFileSize(FLAGS_rocketmq_log_file_size);
        logger.setFileCount(FLAGS_rocketmq_log_file_count);
        logger.init();
    } catch (const std::exception& e) {
        return false;
    }

    return true;
}

bool init_bufferbridge_logger() {
    try {
        spdlog::init_thread_pool(FLAGS_log_thread_pool_q_size,
                                 FLAGS_log_thread_pool_t_num);
        auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            FLAGS_log_file, FLAGS_log_rotate_size, FLAGS_log_rotate_count);
#ifndef NDEBUG
        spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(
            "debug_logger", spdlog::sinks_init_list({console_sink, file_sink}),
            spdlog::thread_pool(),
            spdlog::async_overflow_policy::overrun_oldest));
#else    // Release
        spdlog::set_default_logger(std::make_shared<spdlog::async_logger>(
            "release_logger", file_sink, spdlog::thread_pool(),
            spdlog::async_overflow_policy::block));
#endif

        switch (SPDLOG_ACTIVE_LEVEL) {
            case SPDLOG_LEVEL_TRACE:
                spdlog::set_level(spdlog::level::trace);
                break;
            case SPDLOG_LEVEL_DEBUG:
                spdlog::set_level(spdlog::level::debug);
                break;
            case SPDLOG_LEVEL_INFO:
                spdlog::set_level(spdlog::level::info);
                break;
            case SPDLOG_LEVEL_WARN:
                spdlog::set_level(spdlog::level::warn);
                break;
            case SPDLOG_LEVEL_ERROR:
                spdlog::set_level(spdlog::level::err);
                break;
            case SPDLOG_LEVEL_CRITICAL:
                spdlog::set_level(spdlog::level::critical);
                break;
            case SPDLOG_LEVEL_OFF:
                spdlog::set_level(spdlog::level::off);
                break;
            default:
                break;
        }

        spdlog::set_pattern("[%Y-%m-%d %T.%f] [%^%l%$] [%s:%#] [thread %t] %v");
    } catch (const std::exception &e) {
        spdlog::error("Failed to initialize logger: {}", e.what());
        return false;
    }

    SPDLOG_INFO("Logger initialized successfully");
    return true;
}

bool init_hot_loader() {
    // 初始化 HotLoader
    if (HotLoader::instance().init() != 0) {
        SPDLOG_ERROR("Failed to initialize HotLoader");
        return false;
    }

    // 启动 HotLoader
    if (HotLoader::instance().run() != 0) {
        SPDLOG_ERROR("Failed to start HotLoader");
        return false;
    }

    return true;
}

bool global_init() {
    register_global_extensions();

    if (!init_bufferbridge_logger() || !init_rocketmq_logger()) {
        return false;
    }

    if (!init_hot_loader()) {
        return false;
    }

    return true;
}

void global_destroy() {
    HotLoader::instance().stop();
}