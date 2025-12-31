#include "global.h"

#include "local_ratelimiter.h"
#include "redis_ratelimiter.h"
#include "rocketmq_delay_scheduler.h"

struct GlobalExtensions {
    LocalRateLimiter local_rate_limiter;

    RocketMQDelayScheduler rocketmq_delay_scheduler;
};

static GlobalExtensions g_global_extensions;

void register_global_extensions() {
    RateLimiterExtension()->RegisterOrDie(
        "local", &g_global_extensions.local_rate_limiter);

    SchedulerExtension()->RegisterOrDie(
        "rocketmq_delay_scheduler",
        &g_global_extensions.rocketmq_delay_scheduler);
}