/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "RocketMQ.h"
#include "spdlog/spdlog.h"

#ifndef RMQLOG_ACTIVE_LEVEL
#define RMQLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif

#define RMQLOGGER "rocketmq_logger"

#define RMQ_LOGGER (spdlog::get(RMQLOGGER) ? spdlog::get(RMQLOGGER) : spdlog::default_logger())

#if RMQLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
#    define RMQLOG_LOGGER_TRACE(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::trace, __VA_ARGS__)
#    define RMQLOG_TRACE(...) RMQLOG_LOGGER_TRACE(RMQ_LOGGER, __VA_ARGS__)
#else
#    define RMQLOG_LOGGER_TRACE(logger, ...) (void)0
#    define RMQLOG_TRACE(...) (void)0
#endif

#if RMQLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
#    define RMQLOG_LOGGER_DEBUG(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::debug, __VA_ARGS__)
#    define RMQLOG_DEBUG(...) RMQLOG_LOGGER_DEBUG(RMQ_LOGGER, __VA_ARGS__)
#else
#    define RMQLOG_LOGGER_DEBUG(logger, ...) (void)0
#    define RMQLOG_DEBUG(...) (void)0
#endif

#if RMQLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_INFO
#    define RMQLOG_LOGGER_INFO(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::info, __VA_ARGS__)
#    define RMQLOG_INFO(...) RMQLOG_LOGGER_INFO(RMQ_LOGGER, __VA_ARGS__)
#else
#    define RMQLOG_LOGGER_INFO(logger, ...) (void)0
#    define RMQLOG_INFO(...) (void)0
#endif

#if RMQLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_WARN
#    define RMQLOG_LOGGER_WARN(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::warn, __VA_ARGS__)
#    define RMQLOG_WARN(...) RMQLOG_LOGGER_WARN(RMQ_LOGGER, __VA_ARGS__)
#else
#    define RMQLOG_LOGGER_WARN(logger, ...) (void)0
#    define RMQLOG_WARN(...) (void)0
#endif

#if RMQLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_ERROR
#    define RMQLOG_LOGGER_ERROR(logger, ...) SPDLOG_LOGGER_CALL(logger, spdlog::level::err, __VA_ARGS__)
#    define RMQLOG_ERROR(...) RMQLOG_LOGGER_ERROR(RMQ_LOGGER, __VA_ARGS__)
#else
#    define RMQLOG_LOGGER_ERROR(logger, ...) (void)0
#    define RMQLOG_ERROR(...) (void)0
#endif

ROCKETMQ_NAMESPACE_BEGIN

enum class Level : uint8_t
{
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
  Off = 5
};

class Logger {
public:
  virtual ~Logger() = default;

  /**
   * @brief Set log level for file sink. Its default log level is: Info.
   *
   * @param level
   */
  virtual void setLevel(Level level) = 0;

  /**
   * @brief Set log level for stdout, aka, console. Its default log level is: Warn.
   *
   * @param level
   */
  virtual void setConsoleLevel(Level level) = 0;

  virtual void setLogHome(const std::string& log_home) = 0;

  virtual void setFileSize(std::size_t file_size) = 0;

  virtual void setFileCount(std::size_t file_count) = 0;

  virtual void setPattern(std::string pattern) = 0;

  virtual void init() = 0;
};

Logger& getLogger();

ROCKETMQ_NAMESPACE_END