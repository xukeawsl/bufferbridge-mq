# hot-loader

`hot-loader` 是一个基于 Linux inotify/epoll 的热加载文件监控工具。它可以监听文件的变更（如保存、重写等），并在文件发生变化时自动触发回调，适用于配置热加载、自动重载等场景

## 主要类说明

HotLoadTask

* 继承该类，实现 `on_reload()` 方法，即可自定义文件变更时的处理逻辑
* 构造时传入需要监控的文件路径

### 示例

```cpp
class MyHotLoadTask : public HotLoadTask {
public:
    MyHotLoadTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        // 文件变更时的处理逻辑
        std::cout << "Reloading task for file: " << watch_file() << std::endl;
    }
};
```

HotLoader

* 单例模式，使用 `HotLoader::instance()` 获取实例
* 需先调用 `init()` 初始化，再注册任务
* 支持注册/注销任务、启动/停止监控线程


## 使用流程
1. **实现自定义任务类**（继承 HotLoadTask 并重写 `on_reload()`）
2. **初始化 HotLoader**
```cpp
HotLoader::instance().init();
```
3. **注册任务**
```cpp
auto* task = new MyHotLoadTask("config.json");
HotLoader::instance().register_task(task, HotLoader::OWN_TASK);
```

4. **启动监控线程**
```cpp
HotLoader::instance().run();
```

5. **程序退出前停止监控**
```cpp
HotLoader::instance().stop();
```

## 注意事项
* 仅支持 Linux 平台，且需要支持 C++17
* 被监控的文件注册时需要存在，后面如果删除后再创建可以自动感知并触发重载
* HotLoader 采用单例模式，所有任务均注册到同一个实例
* 若使用 `OWN_TASK`，HotLoader 会自动释放任务对象（使用 delete）；使用 `DOESNT_OWN_TASK` 则需用户自行管理任务生命周期
* 除了注册/注销方法，其它方法都不是线程安全的，用户需要确保不会并发调用


## 完整示例

```cpp
#include <iostream>

#include "hot_loader.h" // step1: 包含头文件

// step2: 继承 HotLoadTask，实现 on_reload 重载逻辑
class MyHotLoadTask : public HotLoadTask {
public:
    MyHotLoadTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        // Custom reload logic
        std::cout << "Reloading task for file: " << watch_file() << std::endl;
    }
};

int main() {
    MyHotLoadTask task("config1.json");

    // step3: 初始化 HotLoader
    if (HotLoader::instance().init() != 0) {
        std::cerr << "Failed to initialize HotLoader" << std::endl;
        return 1;
    }

    // step4: 可以在 run 之前注册 Task，DOESNT_ONW_TASK 表示 HotLoader 不会在注销任务时 delete
    int ret = HotLoader::instance().register_task(&task, HotLoader::DOESNT_OWN_TASK);
    if (ret != 0) {
        std::cerr << "Failed to register task: " << ret << std::endl;
        return 1;
    }

    // step4: 运行 HotLoader 进行监控
    if (HotLoader::instance().run() != 0) {
        std::cerr << "Failed to start HotLoader" << std::endl;
        return 1;
    }
    std::cout << "HotLoader is running. Press Enter to stop..." << std::endl;

    std::thread([]() {
        // 运行后注册 Task，线程安全
        int ret;
        ret = HotLoader::instance().register_task(new MyHotLoadTask("config2.json"), HotLoader::OWN_TASK);
        if (ret != 0) {
            std::cerr << "Failed to register task: " << ret << std::endl;
            return;
        } else {
            std::cout << "Task registered successfully." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(10));
        // step5: 可以主动注销 Task
        ret = HotLoader::instance().unregister_task("config2.json");
        if (ret != 0) {
            std::cerr << "Failed to unregister task: " << ret << std::endl;
            return;
        } else {
            std::cout << "Task unregistered successfully." << std::endl;
        }
    }).detach();

    std::cin.get(); // Wait for user input to stop
    // step6: 停止 HotLoader，会注销所有 Task
    HotLoader::instance().stop();
    std::cout << "HotLoader stopped." << std::endl;

    return 0;
}
```