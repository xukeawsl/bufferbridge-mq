/**
 * HotLoader 完整示例程序
 *
 * 本示例展示了 HotLoader 的所有核心功能：
 * 1. 基本的文件热加载
 * 2. 多个不同的 task 监听同一个文件（新特性）
 * 3. 多线程动态注册/注销 task
 * 4. 运行时动态添加和移除文件监控
 * 5. 不同类型的 task 处理同一个文件
 * 6. 细粒度注销单个 task（不影响其他监听同一文件的 task）
 *
 * 编译命令：
 * g++ example.cpp -o example -lpthread -std=c++17
 *
 * 运行方式：
 * ./example
 *
 * 运行后，在另一个终端修改配置文件来测试热加载：
 * echo "new config" > config1.json
 * echo "new config" > shared_config.txt
 */

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#include "hot_loader.h"

// ============================================================
// Task 类型 1: 配置文件热加载任务
// ============================================================
class ConfigTask : public HotLoadTask {
public:
    ConfigTask(const std::string& file, const std::string& task_name)
        : HotLoadTask(file), _task_name(task_name) {
        std::cout << "[ConfigTask-" << _task_name << "] 创建任务，监控文件: " << file << std::endl;
    }

    ~ConfigTask() {
        std::cout << "[ConfigTask-" << _task_name << "] 销毁任务" << std::endl;
    }

    void on_reload() override {
        std::cout << "[ConfigTask-" << _task_name << "] 检测到文件变化，重新加载配置: "
                  << watch_file() << std::endl;

        // 这里可以添加实际的配置加载逻辑
        // 例如：读取配置文件、解析 JSON/YAML、更新应用配置等
        load_config();
    }

private:
    void load_config() {
        // 模拟配置加载
        std::cout << "  -> 配置已重新加载完成" << std::endl;
    }

    std::string _task_name;
};

// ============================================================
// Task 类型 2: 日志文件分析任务
// ============================================================
class LogAnalyzerTask : public HotLoadTask {
public:
    LogAnalyzerTask(const std::string& file, int analyzer_id)
        : HotLoadTask(file), _analyzer_id(analyzer_id) {
        std::cout << "[LogAnalyzer-" << _analyzer_id << "] 创建日志分析器，监控文件: " << file << std::endl;
    }

    ~LogAnalyzerTask() {
        std::cout << "[LogAnalyzer-" << _analyzer_id << "] 销毁日志分析器" << std::endl;
    }

    void on_reload() override {
        std::cout << "[LogAnalyzer-" << _analyzer_id << "] 检测到新日志，开始分析: "
                  << watch_file() << std::endl;

        // 这里可以添加实际的日志分析逻辑
        analyze_logs();
    }

private:
    void analyze_logs() {
        // 模拟日志分析
        std::cout << "  -> 日志分析完成，发现 0 个错误" << std::endl;
    }

    int _analyzer_id;
};

// ============================================================
// Task 类型 3: 缓存失效任务
// ============================================================
class CacheInvalidatorTask : public HotLoadTask {
public:
    CacheInvalidatorTask(const std::string& file, const std::string& cache_name)
        : HotLoadTask(file), _cache_name(cache_name) {
        std::cout << "[CacheInvalidator-" << _cache_name << "] 创建缓存失效器，监控文件: " << file << std::endl;
    }

    ~CacheInvalidatorTask() {
        std::cout << "[CacheInvalidator-" << _cache_name << "] 销毁缓存失效器" << std::endl;
    }

    void on_reload() override {
        std::cout << "[CacheInvalidator-" << _cache_name << "] 检测到依赖变化，清除缓存: "
                  << watch_file() << std::endl;

        // 这里可以添加实际的缓存清除逻辑
        invalidate_cache();
    }

private:
    void invalidate_cache() {
        // 模拟缓存失效
        std::cout << "  -> 缓存已清除" << std::endl;
    }

    std::string _cache_name;
};

// ============================================================
// 辅助函数：创建测试文件
// ============================================================
void create_test_files() {
    system("echo 'initial config 1' > config1.json");
    system("echo 'initial config 2' > config2.json");
    system("echo 'shared configuration file' > shared_config.txt");
    std::cout << "\n=== 测试文件已创建 ===" << std::endl;
}

// ============================================================
// 辅助函数：清理测试文件
// ============================================================
void cleanup_test_files() {
    system("rm -f config1.json config2.json config3.json shared_config.txt");
    std::cout << "=== 测试文件已清理 ===" << std::endl;
}

// ============================================================
// 演示功能 1: 多个不同类型的 task 监听同一个文件
// ============================================================
void demo_multiple_tasks_same_file() {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "演示 1: 多个不同类型的 task 监听同一个文件" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 创建三个不同类型的 task，都监控同一个文件
    ConfigTask* task1 = new ConfigTask("shared_config.txt", "main");
    LogAnalyzerTask* task2 = new LogAnalyzerTask("shared_config.txt", 1);
    CacheInvalidatorTask* task3 = new CacheInvalidatorTask("shared_config.txt", "l1_cache");

    std::cout << "\n注册 3 个不同的 task 到同一个文件 shared_config.txt..." << std::endl;

    // 同时注册这 3 个 task
    HotLoader::instance().register_task(task1, HotLoader::OWN_TASK);
    HotLoader::instance().register_task(task2, HotLoader::OWN_TASK);
    HotLoader::instance().register_task(task3, HotLoader::OWN_TASK);

    std::cout << "✓ 所有 task 已注册完成" << std::endl;
    std::cout << "\n等待 3 秒后修改文件..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 修改文件，触发所有 task
    std::cout << "\n>>> 修改 shared_config.txt" << std::endl;
    system("echo 'updated shared config' > shared_config.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n>>> 再次修改 shared_config.txt" << std::endl;
    system("echo 'another update' > shared_config.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n卸载所有 task..." << std::endl;
    HotLoader::instance().unregister_task("shared_config.txt");
    std::cout << "✓ 演示 1 完成" << std::endl;
}

// ============================================================
// 演示功能 2: 多线程动态注册
// ============================================================
void demo_multithreaded_registration() {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "演示 2: 多线程动态注册 task" << std::endl;
    std::cout << "==================================================" << std::endl;

    std::atomic<int> success_count{0};

    // 启动 3 个线程，每个线程注册不同的 task
    std::vector<std::thread> threads;

    for (int i = 1; i <= 3; ++i) {
        threads.emplace_back([i, &success_count]() {
            std::string file = "config" + std::to_string(i) + ".json";

            // 模拟一些处理延迟
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * i));

            // 在线程中动态注册 task
            ConfigTask* task = new ConfigTask(file, "thread_" + std::to_string(i));
            int ret = HotLoader::instance().register_task(task, HotLoader::OWN_TASK);

            if (ret == 0) {
                success_count++;
                std::cout << "[线程 " << i << "] 成功注册 task: " << file << std::endl;
            } else {
                std::cerr << "[线程 " << i << "] 注册失败: " << ret << std::endl;
            }

            // 5 秒后自动注销
            std::this_thread::sleep_for(std::chrono::seconds(5));
            HotLoader::instance().unregister_task(file);
            std::cout << "[线程 " << i << "] 已自动注销 task: " << file << std::endl;
        });
    }

    // 等待所有线程完成注册
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "\n所有线程注册完成，成功: " << success_count << "/3" << std::endl;
    std::cout << "\n修改配置文件来触发热加载..." << std::endl;

    // 修改所有配置文件
    for (int i = 1; i <= 3; ++i) {
        std::string file = "config" + std::to_string(i) + ".json";
        std::cout << "\n>>> 修改 " << file << std::endl;
        system(("echo 'thread update " + std::to_string(i) + "' > " + file).c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "\n等待线程自动注销..." << std::endl;

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "✓ 演示 2 完成" << std::endl;
}

// ============================================================
// 演示功能 3: 相同类的多个实例监听同一文件
// ============================================================
void demo_same_class_multiple_instances() {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "演示 3: 相同类的多个实例监听同一文件" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 创建同一个类的 5 个不同实例，都监控同一个文件
    std::cout << "创建 5 个 ConfigTask 实例，都监控 config1.json..." << std::endl;

    std::vector<ConfigTask*> tasks;
    for (int i = 1; i <= 5; ++i) {
        ConfigTask* task = new ConfigTask("config1.json", "instance_" + std::to_string(i));
        tasks.push_back(task);

        int ret = HotLoader::instance().register_task(task, HotLoader::OWN_TASK);
        if (ret == 0) {
            std::cout << "  ✓ 实例 " << i << " 已注册" << std::endl;
        }
    }

    std::cout << "\n等待 2 秒后修改文件..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 修改文件，应该触发所有 5 个实例
    std::cout << "\n>>> 修改 config1.json，应该触发所有 5 个实例" << std::endl;
    system("echo 'trigger all instances' > config1.json");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "\n卸载所有实例..." << std::endl;
    HotLoader::instance().unregister_task("config1.json");
    std::cout << "✓ 演示 3 完成" << std::endl;
}

// ============================================================
// 演示功能 4: 细粒度注销单个 task
// ============================================================
void demo_granular_unregistration() {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "演示 4: 细粒度注销单个 task" << std::endl;
    std::cout << "==================================================" << std::endl;

    // 创建测试文件
    system("echo 'granular test' > granular_test.txt");

    // 注册 3 个不同类型的 task 到同一个文件
    ConfigTask* task1 = new ConfigTask("granular_test.txt", "main");
    LogAnalyzerTask* task2 = new LogAnalyzerTask("granular_test.txt", 1);
    CacheInvalidatorTask* task3 = new CacheInvalidatorTask("granular_test.txt", "l1");

    std::cout << "\n注册 3 个不同的 task 到 granular_test.txt..." << std::endl;
    HotLoader::instance().register_task(task1, HotLoader::OWN_TASK);
    std::cout << "  ✓ task1 (ConfigTask) 已注册" << std::endl;

    HotLoader::instance().register_task(task2, HotLoader::OWN_TASK);
    std::cout << "  ✓ task2 (LogAnalyzerTask) 已注册" << std::endl;

    HotLoader::instance().register_task(task3, HotLoader::OWN_TASK);
    std::cout << "  ✓ task3 (CacheInvalidatorTask) 已注册" << std::endl;

    std::cout << "\n等待 2 秒后修改文件（应该触发所有 3 个 task）..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n>>> 第一次修改文件" << std::endl;
    system("echo 'first update' > granular_test.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 注销 task2，其他 task 应该继续工作
    std::cout << "\n>>> 注销 task2 (LogAnalyzerTask)，task1 和 task3 应该继续工作" << std::endl;
    int ret = HotLoader::instance().unregister_task(task2);
    if (ret == 0) {
        std::cout << "✓ task2 已成功注销" << std::endl;
    } else {
        std::cerr << "✗ task2 注销失败: " << ret << std::endl;
    }

    std::cout << "\n等待 2 秒后再次修改文件（应该只触发 task1 和 task3）..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n>>> 第二次修改文件" << std::endl;
    system("echo 'second update' > granular_test.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 再注销 task1
    std::cout << "\n>>> 注销 task1，只剩下 task3" << std::endl;
    HotLoader::instance().unregister_task(task1);
    std::cout << "✓ task1 已成功注销" << std::endl;

    std::cout << "\n等待 2 秒后最后一次修改文件（应该只触发 task3）..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "\n>>> 第三次修改文件" << std::endl;
    system("echo 'third update' > granular_test.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 清理
    std::cout << "\n>>> 注销最后一个 task3" << std::endl;
    HotLoader::instance().unregister_task(task3);
    std::cout << "✓ task3 已成功注销" << std::endl;

    std::cout << "\n>>> 最后一次修改文件（不应该触发任何 task）" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    system("echo 'final update' > granular_test.txt");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 清理测试文件
    system("rm -f granular_test.txt");

    std::cout << "✓ 演示 4 完成" << std::endl;
}

// ============================================================
// 主函数
// ============================================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "    HotLoader 完整功能演示程序" << std::endl;
    std::cout << "========================================" << std::endl;

    // 创建测试文件
    create_test_files();

    // 初始化 HotLoader
    std::cout << "\n初始化 HotLoader..." << std::endl;
    if (HotLoader::instance().init() != 0) {
        std::cerr << "✗ HotLoader 初始化失败" << std::endl;
        return 1;
    }
    std::cout << "✓ HotLoader 初始化成功" << std::endl;

    // 启动 HotLoader 工作线程
    if (HotLoader::instance().run() != 0) {
        std::cerr << "✗ HotLoader 启动失败" << std::endl;
        return 1;
    }
    std::cout << "✓ HotLoader 已启动，开始监控文件变化" << std::endl;

    // 运行各个演示
    demo_multiple_tasks_same_file();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    demo_multithreaded_registration();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    demo_same_class_multiple_instances();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    demo_granular_unregistration();

    // 所有演示完成
    std::cout << "\n==================================================" << std::endl;
    std::cout << "所有演示完成！" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "\nHotLoader 核心特性总结：" << std::endl;
    std::cout << "  1. ✓ 支持多个不同的 task 监听同一个文件" << std::endl;
    std::cout << "  2. ✓ 支持多线程动态注册和注销 task" << std::endl;
    std::cout << "  3. ✓ 支持运行时添加和移除文件监控" << std::endl;
    std::cout << "  4. ✓ 支持相同类的多个实例监听同一文件" << std::endl;
    std::cout << "  5. ✓ 支持细粒度注销单个 task（不影响其他 task）" << std::endl;
    std::cout << "  6. ✓ 线程安全的任务管理" << std::endl;
    std::cout << "  7. ✓ 自动内存管理（支持 OWN_TASK 模式）" << std::endl;

    // 停止 HotLoader
    std::cout << "\n停止 HotLoader..." << std::endl;
    HotLoader::instance().stop();
    std::cout << "✓ HotLoader 已停止" << std::endl;

    // 清理测试文件
    cleanup_test_files();

    std::cout << "\n程序结束" << std::endl;
    return 0;
}
