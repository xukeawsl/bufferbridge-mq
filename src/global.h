#pragma once

#include "brpc/extension.h"
#include "gflags/gflags.h"
#include "hot_loader.h"
#include "rocketmq/Logger.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"

namespace bmq {

bool global_init();

void global_destroy();

}    // namespace bmq