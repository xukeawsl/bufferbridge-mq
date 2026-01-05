#include "scheduler_manager.h"

#include <set>

#include "global.h"
#include "spdlog/spdlog.h"
#include "yaml-cpp/yaml.h"

namespace bmq {

SchedulerManager::~SchedulerManager() { stop_all(); }

bool SchedulerManager::load_from_config(const std::string& config_file) {
    try {
        YAML::Node config_node = YAML::LoadFile(config_file);

        if (!config_node["schedulers"].IsDefined()) {
            SPDLOG_ERROR("'schedulers' field not found in config file: {}",
                         config_file);
            return false;
        }

        YAML::Node schedulers_node = config_node["schedulers"];

        // 用于检查调度器名称重复
        std::set<std::string> scheduler_names;

        for (const auto& scheduler_node : schedulers_node) {
            SchedulerInstance instance;

            // 读取调度器名称（必需）
            if (!scheduler_node["name"].IsDefined()) {
                SPDLOG_ERROR("Scheduler 'name' is required");
                return false;
            }
            instance.name = scheduler_node["name"].as<std::string>();

            // 检查调度器名称是否重复
            if (scheduler_names.find(instance.name) != scheduler_names.end()) {
                SPDLOG_ERROR("Duplicate scheduler name '{}'", instance.name);
                return false;
            }
            scheduler_names.insert(instance.name);

            // 读取是否启用（可选，默认为 true）
            bool enabled = true;
            if (scheduler_node["enabled"].IsDefined()) {
                enabled = scheduler_node["enabled"].as<bool>();
            }

            if (!enabled) {
                SPDLOG_INFO("Scheduler '{}' is disabled, skipping",
                            instance.name);
                continue;
            }

            // 读取配置文件路径（必需）
            if (!scheduler_node["config_file"].IsDefined()) {
                SPDLOG_ERROR("Scheduler 'config_file' is required for '{}'",
                             instance.name);
                return false;
            }
            instance.config_file =
                scheduler_node["config_file"].as<std::string>();

            // 读取调度器类型（可选，默认为 rocketmq_delay_scheduler）
            std::string scheduler_type = "rocketmq_delay_scheduler";
            if (scheduler_node["type"].IsDefined()) {
                scheduler_type = scheduler_node["type"].as<std::string>();
            }

            // 从扩展系统获取调度器实现
            const IScheduler* scheduler_ext =
                SchedulerExtension()->Find(scheduler_type.c_str());
            if (!scheduler_ext) {
                SPDLOG_ERROR("Scheduler extension '{}' not found for '{}'",
                             scheduler_type, instance.name);
                return false;
            }

            // 克隆调度器实例
            instance.scheduler = scheduler_ext->clone();
            if (!instance.scheduler) {
                SPDLOG_ERROR("Failed to clone scheduler '{}'", instance.name);
                return false;
            }

            // 初始化调度器（传入名称和配置文件）
            if (!instance.scheduler->init(instance.name,
                                          instance.config_file)) {
                SPDLOG_ERROR(
                    "Failed to initialize scheduler '{}' from config: {}",
                    instance.name, instance.config_file);
                return false;
            }

            _schedulers.push_back(std::move(instance));
            SPDLOG_INFO("Scheduler '{}' loaded successfully", instance.name);
        }

        if (_schedulers.empty()) {
            SPDLOG_WARN("No schedulers loaded from config file: {}",
                        config_file);
        }

        return true;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to load schedulers from config file '{}': {}",
                     config_file, e.what());
        return false;
    }
}

void SchedulerManager::start_all() {
    SPDLOG_INFO("Starting {} scheduler(s)...", _schedulers.size());

    for (const auto& instance : _schedulers) {
        try {
            instance.scheduler->start();
            SPDLOG_INFO("Scheduler '{}' started", instance.name);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to start scheduler '{}': {}", instance.name,
                         e.what());
        }
    }

    SPDLOG_INFO("All schedulers started");
}

void SchedulerManager::stop_all() {
    SPDLOG_INFO("Stopping {} scheduler(s)...", _schedulers.size());

    for (const auto& instance : _schedulers) {
        try {
            instance.scheduler->stop();
            SPDLOG_INFO("Scheduler '{}' stopped", instance.name);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("Failed to stop scheduler '{}': {}", instance.name,
                         e.what());
        }
    }

    SPDLOG_INFO("All schedulers stopped");
}

}    // namespace bmq
