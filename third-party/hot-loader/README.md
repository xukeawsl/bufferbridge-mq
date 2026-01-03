# hot-loader

`hot-loader` 是一个基于 Linux inotify/epoll 的高性能热加载文件监控库。它可以监听文件的变更（如保存、重写等），并在文件发生变化时自动触发回调，适用于配置热加载、自动重载等场景。

## 核心特性

- ✅ **高效性能**：基于 Linux inotify + epoll 实现，支持非阻塞 I/O 和事件聚合
- ✅ **线程安全**：支持多线程动态注册/注销任务
- ✅ **多任务支持**：允许多个不同的 task 监听同一个文件（每个 task 独立触发）
- ✅ **自动恢复**：文件删除后重新创建可自动感知并恢复监控
- ✅ **灵活管理**：支持 OWN_TASK 和 DOESNT_OWN_TASK 两种内存管理模式
- ✅ **单头文件**：Header-only 设计，易于集成
- ✅ **零依赖**：仅依赖 C++17 标准库和 Linux 系统调用

## 系统要求

- **操作系统**：Linux（使用 inotify 和 epoll）
- **编译器**：支持 C++17 的编译器（GCC 7+, Clang 5+）
- **依赖库**：pthread（线程支持）

## 快速开始

### 1. 基本使用

```cpp
#include "hot_loader.h"

// 继承 HotLoadTask，实现自定义的重载逻辑
class MyConfigTask : public HotLoadTask {
public:
    MyConfigTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        std::cout << "配置文件已更改，正在重新加载: " << watch_file() << std::endl;
        // 添加你的配置加载逻辑
    }
};

int main() {
    // 1. 初始化 HotLoader
    HotLoader::instance().init();

    // 2. 创建并注册任务
    auto* task = new MyConfigTask("config.json");
    HotLoader::instance().register_task(task, HotLoader::OWN_TASK);

    // 3. 启动监控
    HotLoader::instance().run();

    // 程序运行中...

    // 4. 停止监控（程序退出前）
    HotLoader::instance().stop();

    return 0;
}
```

### 2. 编译

```bash
g++ your_code.cpp -o your_program -lpthread -std=c++17
```

或使用提供的构建脚本：

```bash
./run.sh
```

## 核心类说明

### HotLoadTask

抽象基类，用户需要继承此类并实现 `on_reload()` 方法。

**方法：**

- `HotLoadTask(const std::string& file)` - 构造函数，指定要监控的文件
- `virtual void on_reload()` - 文件变化时的回调函数（需重写）
- `const std::string& watch_file()` - 获取监控的文件路径

**示例：**

```cpp
class MyTask : public HotLoadTask {
public:
    MyTask(const std::string& file) : HotLoadTask(file) {}

    void on_reload() override {
        // 文件发生变化时调用
        std::cout << "文件 " << watch_file() << " 已修改" << std::endl;
    }
};
```

### HotLoader

单例类，管理所有文件监控任务。

**主要方法：**

```cpp
// 获取单例实例
static HotLoader& instance();

// 初始化 inotify 和 epoll
int init();

// 注册任务（线程安全）
// ownership: OWN_TASK（自动管理内存）或 DOESNT_OWN_TASK（用户管理）
int register_task(HotLoadTask* task, OwnerShip ownership);

// 注销任务（线程安全）
// 注意：unregister_task(task*) 只注销指定的 task
//       unregister_task(file) 会注销该文件的所有 task
int unregister_task(HotLoadTask* task);
int unregister_task(const std::string& file);

// 注销所有任务
int unregister_all_tasks();

// 启动监控线程
int run();

// 停止监控线程
void stop();
```

## 高级用法

### 1. 多个任务监听同一文件

**新特性！** 现在可以注册多个不同的 task 监听同一个文件：

```cpp
// 创建三个不同类型的 task，都监控同一个文件
class ConfigReloader : public HotLoadTask {
public:
    ConfigReloader(const std::string& file) : HotLoadTask(file) {}
    void on_reload() override {
        std::cout << "[ConfigReloader] 重新加载配置" << std::endl;
    }
};

class CacheInvalidator : public HotLoadTask {
public:
    CacheInvalidator(const std::string& file) : HotLoadTask(file) {}
    void on_reload() override {
        std::cout << "[CacheInvalidator] 清除缓存" << std::endl;
    }
};

class Logger : public HotLoadTask {
public:
    Logger(const std::string& file) : HotLoadTask(file) {}
    void on_reload() override {
        std::cout << "[Logger] 记录配置变更" << std::endl;
    }
};

// 注册到同一个文件
ConfigReloader* task1 = new ConfigReloader("app.conf");
CacheInvalidator* task2 = new CacheInvalidator("app.conf");
Logger* task3 = new Logger("app.conf");

HotLoader::instance().register_task(task1, HotLoader::OWN_TASK);
HotLoader::instance().register_task(task2, HotLoader::OWN_TASK);
HotLoader::instance().register_task(task3, HotLoader::OWN_TASK);

// 当 app.conf 修改时，所有三个 task 都会被触发
```

### 2. 多线程动态注册

支持在多个线程中动态注册和注销任务：

```cpp
#include <thread>

void worker_thread(int id) {
    std::string file = "config_" + std::to_string(id) + ".json";

    // 在线程中注册任务
    auto* task = new MyTask(file);
    HotLoader::instance().register_task(task, HotLoader::OWN_TASK);

    // 工作一段时间后注销
    std::this_thread::sleep_for(std::chrono::seconds(10));
    HotLoader::instance().unregister_task(file);
}

int main() {
    HotLoader::instance().init();
    HotLoader::instance().run();

    // 启动多个工作线程
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back(worker_thread, i);
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }

    HotLoader::instance().stop();
    return 0;
}
```

### 3. 内存管理

提供两种内存管理模式：

```cpp
// 模式 1: HotLoader 自动管理内存
auto* task = new MyTask("config.json");
HotLoader::instance().register_task(task, HotLoader::OWN_TASK);
// 注销时会自动 delete task

// 模式 2: 用户管理内存
MyTask task("config.json");
HotLoader::instance().register_task(&task, HotLoader::DOESNT_OWN_TASK);
// 用户需要负责 task 的生命周期
```

### 4. 文件自动恢复

支持文件删除后重新创建的自动恢复：

```cpp
// 文件存在时注册
auto* task = new MyTask("config.json");
HotLoader::instance().register_task(task, HotLoader::OWN_TASK);

// 即使文件被删除后又重新创建，监控会自动恢复
// 重新创建时会自动触发 on_reload()
```

### 5. 细粒度任务管理

当多个 task 监听同一文件时，可以选择性地注销单个 task 或所有 task：

```cpp
// 注册多个 task 到同一文件
ConfigReloader* task1 = new ConfigReloader("app.conf");
CacheInvalidator* task2 = new CacheInvalidator("app.conf");
Logger* task3 = new Logger("app.conf");

HotLoader::instance().register_task(task1, HotLoader::OWN_TASK);
HotLoader::instance().register_task(task2, HotLoader::OWN_TASK);
HotLoader::instance().register_task(task3, HotLoader::OWN_TASK);

// 方式 1: 只注销指定的 task（不影响其他 task）
HotLoader::instance().unregister_task(task1);
// 现在 task2 和 task3 仍在运行

// 方式 2: 注销该文件的所有 task
HotLoader::instance().unregister_task("app.conf");
// task2 和 task3 都会被注销
```

## 使用流程

1. **实现自定义任务类**
   - 继承 `HotLoadTask`
   - 重写 `on_reload()` 方法

2. **初始化 HotLoader**
   ```cpp
   HotLoader::instance().init();
   ```

3. **注册任务**
   ```cpp
   auto* task = new MyTask("config.json");
   HotLoader::instance().register_task(task, HotLoader::OWN_TASK);
   ```

4. **启动监控**
   ```cpp
   HotLoader::instance().run();
   ```

5. **程序退出前停止监控**
   ```cpp
   HotLoader::instance().stop();
   ```

## 完整示例

查看 `example.cpp` 获取完整的示例代码，包括：

- ✅ 多个不同类型的 task 监听同一个文件
- ✅ 多线程动态注册和注销
- ✅ 相同类的多个实例监听同一文件
- ✅ 运行时动态添加和移除文件监控

**运行示例：**

```bash
# 编译
g++ example.cpp -o example -lpthread -std=c++17

# 运行
./example
```

## 注意事项

1. **平台限制**：仅支持 Linux 平台（依赖 inotify 和 epoll）
2. **C++17**：需要支持 C++17 的编译器（`std::filesystem`）
3. **文件存在性**：被监控的文件在注册时需要存在
4. **线程安全**：
   - `register_task()` 和 `unregister_task()` 是线程安全的
   - 其他方法建议在主线程调用
5. **单例模式**：HotLoader 采用单例模式，全局唯一实例
6. **性能考虑**：大量文件监控时，inotify watch 数量可能受系统限制（可通过 `/proc/sys/fs/inotify/max_user_watches` 调整）

## API 返回码说明

所有 `int` 返回值的 API 使用以下错误码：

- `0`：成功
- `-1`：无效的 task 指针或 HotLoader 未初始化
- `-2`：HotLoader 未初始化
- `-3`：任务已注册或文件路径无效
- `-4`：添加 inotify watch 失败（文件不存在或权限不足）

## 性能特点

- **事件聚合**：多个文件事件会被聚合处理，减少系统调用
- **边缘触发**：使用 epoll 的 EPOLLET 模式，提高 I/O 效率
- **非阻塞**：所有 I/O 操作均为非阻塞模式
- **线程安全**：使用 mutex 保护共享数据结构

## 常见问题

**Q: 为什么我的文件变化没有被检测到？**

A: 确保文件在注册时存在，并且程序有读取权限。

**Q: 可以监控目录吗？**

A: 当前版本仅支持监控文件，不支持目录监控。

**Q: 最多可以监控多少个文件？**

A: 理论上无限制，实际受系统 `inotify.max_user_watches` 限制（默认通常为 8192）。

**Q: 多个 task 监听同一文件会影响性能吗？**

A: 不会。所有 task 共享同一个 inotify watch，性能开销很小。

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request！
