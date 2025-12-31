# BufferBridge MQ

一个基于 C++ 的高性能消息队列调度系统，用于在 RocketMQ 主题之间进行智能延时调度和流量控制。

## 项目简介

BufferBridge MQ 是一个消息调度中间件，主要用于以下场景：

- **延时消息处理**：从缓冲主题消费消息，并在指定的时间窗口内将消息投递到目标主题
- **时间窗口控制**：支持配置多个不连续的时间窗口，仅在特定时间段处理消息
- **流量限制**：提供基于令牌桶算法的速率限制功能，防止下游系统过载
- **分布式限流**：支持本地内存限流和基于 Redis 的分布式限流

### 典型应用场景

- **批处理窗口调度**：在凌晨或系统空闲时段批量处理业务数据
- **削峰填谷**：在高峰期缓存消息，在低峰期匀速处理
- **系统保护**：防止突发流量冲击下游服务
- **定时任务调度**：按时间窗口执行定时消息分发

## 核心特性

### 1. 时间窗口调度
- 支持配置多个时间窗口（如 00:30-07:30, 13:00-14:00）
- 每个时间窗口可独立启用/禁用
- 自动验证时间窗口，不允许跨天和重叠
- 支持非连续时间段的灵活配置

### 2. 灵活的限流策略
- **本地限流器**：基于内存的令牌桶算法，单机部署适用
- **Redis 限流器**：基于 Redis + Lua 脚本的分布式限流，多实例部署适用
- 每个时间窗口可配置不同的限流参数

### 3. 高可靠性
- 消息确认机制，确保消息不丢失
- 可见的消息超时机制（invisible duration）
- 异常处理和重试机制
- 优雅停机支持

### 4. 高性能设计
- 多线程工作池并行处理消息
- 批量消费和生产消息
- 连接复用减少开销
- 基于 brpc 的高性能扩展机制

### 5. 插件化架构
- 基于 brpc 的扩展系统
- 接口化设计，易于扩展新的调度器和限流器
- 运行时可配置不同的实现

## 系统架构

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Buffer Topic   │───▶│ BufferBridge MQ │───▶│  Target Topic   │
│   (RocketMQ)    │    │   Scheduler     │    │   (RocketMQ)    │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                                │
                                ▼
                       ┌─────────────────┐
                       │  Rate Limiter   │
                       │  (Local/Redis)  │
                       └─────────────────┘
```

### 工作流程

1. **消息缓冲**：从缓冲主题消费消息，设置不可见时长
2. **时间窗口检查**：判断当前时间是否在启用的处理窗口内
3. **流量限制**：对消息进行限流控制
4. **消息转发**：将允许处理的消息投递到目标主题
5. **消息确认**：成功处理的消息在缓冲主题中确认

## 环境要求

### 编译环境
- **CMake**: >= 3.16
- **编译器**: 支持 C++17 的 GCC/Clang
- **操作系统**: Linux (推荐 CentOS 7+ 或 Ubuntu 18.04+)

### 依赖库

#### 核心依赖
- [RocketMQ C++ Client](https://github.com/apache/rocketmq-client-cpp) v5.0.3
- [brpc](https://github.com/apache/brpc) v1.15.0
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) v0.8.0
- [LevelDB](https://github.com/google/leveldb)
- [OpenSSL](https://www.openssl.org/) 1.1.1+
- [Protocol Buffers](https://github.com/protocolbuffers/protobuf) 3.19.4+
- [nlohmann/json](https://github.com/nlohmann/json) v3.12.0

#### 系统依赖
- pthread (多线程支持)
- gflags (命令行参数解析)
- libdl (动态链接库)

#### 运行时依赖
- RocketMQ Broker 实例
- Redis 服务（如使用 RedisRateLimiter）

## 编译安装

### 1. 安装依赖库

```bash
# 安装基础工具
sudo yum install -y git cmake gcc-c++

# 安装第三方依赖到指定目录
# 假设安装到 /usr/local/3rd/
```

### 2. 克隆代码

```bash
git clone <repository-url>
cd bufferbridge-mq
```

### 3. 编译项目

```bash
mkdir build && cd build
cmake ..
make -j4
```

编译成功后会生成可执行文件 `bufferbridge-mq`。

### 4. 代码格式化

项目已配置 clang-format，可以使用以下命令自动格式化代码：

```bash
make clang-format
```

## 配置说明

### 主配置文件

配置文件位置：`conf/schedulers/rocketmq_delay_scheduler.yml`

```yaml
# 调度器工作线程数
worker_threads: 4

# 调度器调度间隔时间，单位秒
scheduler_interval_seconds: 10

# RocketMQ 配置
rocketmq:
  # 缓冲主题消费者配置
  buffer_consumer_topic: "BUFFER_TOPIC"              # 缓冲主题名称
  buffer_consumer_access_point: "127.0.0.1:8081"     # RocketMQ 地址
  buffer_consumer_group: "BUFFER_GROUP"              # 消费者组
  buffer_consumer_await_duration: 5                  # 等待时长（秒）
  buffer_consumer_batch_size: 32                     # 批量消费大小
  buffer_consumer_invisible_duration: 20             # 不可见时长（秒）

  # 目标主题生产者配置
  target_producer_topic: "TARGET_TOPIC"              # 目标主题名称
  target_producer_access_point: "127.0.0.1:8081"     # RocketMQ 地址

# 时间窗口配置
time_windows:
  # 使用本地限流器
  - start: "00:30"                                  # 开始时间
    end: "07:30"                                    # 结束时间
    rate_limiter_config: '{"rate": 100}'            # 本地限流器配置
    enable: true                                    # 是否启用

  # 使用本地限流器，带突发容量
  - start: "13:00"
    end: "14:00"
    rate_limiter_config: '{"rate": 50, "burst": 100}'
    enable: true

  # 使用 Redis 限流器
  - start: "18:00"
    end: "20:00"
    rate_limiter_config: '{"bucket_key": "evening_window", "rate": 200, "burst": 300}'
    enable: true

  # 禁用的窗口
  - start: "23:00"
    end: "23:59"
    rate_limiter_config: '{"rate": 200}'
    enable: false
```

### 限流器配置

#### 本地限流器（LocalRateLimiter）

基于本地内存的令牌桶算法实现，适用于单机部署场景。

```json
{
  "rate": 100              // 速率（每秒生成的令牌数，必需）
}
```

可选参数：
```json
{
  "rate": 100,             // 速率（每秒生成的令牌数）
  "burst": 200             // 突发容量（可选，默认与 rate 相同）
}
```

**参数说明**：
- `rate`（必需）：每秒生成的令牌数量，必须大于 0
- `burst`（可选）：令牌桶的最大容量，用于处理突发流量。如果未设置，默认值为 `rate`。允许短时间内的流量超过 `rate`，但不能超过 `burst`

#### Redis 限流器（RedisRateLimiter）

基于 Redis + Lua 脚本的分布式令牌桶实现，适用于多实例部署场景。

```json
{
  "bucket_key": "my_bucket",        // 令牌桶在 Redis 中的键名（必需）
  "rate": 100                       // 速率（每秒生成的令牌数，必需）
}
```

完整配置示例：
```json
{
  "bucket_key": "my_bucket",        // 令牌桶在 Redis 中的键名（必需）
  "rate": 100,                      // 速率（每秒生成的令牌数，必需）
  "burst": 200,                     // 突发容量（可选，默认与 rate 相同）
  "script_path": "../conf/redis_rate_limiter.lua",  // Lua 脚本路径（可选）
  "redis_address": "127.0.0.1:6379",// Redis 服务器地址（可选，可通过命令行参数指定）
  "redis_password": "password"      // Redis 密码（可选）
}
```

**参数说明**：
- `bucket_key`（必需）：令牌桶在 Redis 中的键名，每个不同的时间窗口应使用不同的键名
- `rate`（必需）：每秒生成的令牌数量，必须大于 0
- `burst`（可选）：令牌桶的最大容量，如果未设置，默认值为 `rate`
- `script_path`（可选）：Lua 脚本文件路径，默认为 `../conf/redis_rate_limiter.lua`
- `redis_address`（可选）：Redis 服务器地址和端口，默认通过 `--limiter_redis_address` 命令行参数指定
- `redis_password`（可选）：Redis 认证密码，默认通过 `--limiter_redis_password` 命令行参数指定

**命令行参数**（Redis 限流器相关）：
```bash
--limiter_script_path: Lua 脚本路径（默认：../conf/redis_rate_limiter.lua）
--limiter_redis_address: Redis 地址（默认：127.0.0.1:6379）
--limiter_redis_password: Redis 密码（默认：空）
--limiter_script_load_timeout_ms: 脚本加载超时时间（默认：1000ms）
--limiter_check_timeout_ms: 限流检查超时时间（默认：100ms）
```

### 时间窗口规则

- 时间格式：`HH:MM`（24 小时制）
- 不允许跨天配置（如 23:00-01:00）
- 不允许时间窗口重叠
- 每个时间窗口可以独立启用或禁用

## 使用方法

### 启动服务

```bash
cd build
./bufferbridge-mq
```

### 停止服务

在运行界面按 `Enter` 键，程序会优雅停机：

1. 停止消费新消息
2. 等待已消费消息处理完成
3. 关闭连接和线程池
4. 退出程序

### 日志说明

程序使用 spdlog 进行日志输出：
- `INFO`: 正常运行日志
- `WARN`: 警告信息（如启动/停止、消息处理失败）
- `ERROR`: 错误信息（如初始化失败、连接错误）

## 项目结构

```
bufferbridge-mq/
├── CMakeLists.txt              # CMake 构建配置
├── main.cpp                    # 程序入口
├── conf/                       # 配置文件目录
│   ├── conf.yml                # 主配置
│   ├── redis_rate_limiter.lua  # Redis 限流脚本
│   └── schedulers/             # 调度器配置
│       └── rocketmq_delay_scheduler.yml
├── src/                        # 源代码目录
│   ├── global.h/cpp            # 全局注册和初始化
│   ├── iratelimiter.h          # 限流器接口
│   ├── local_ratelimiter.h/cpp # 本地限流器实现
│   ├── redis_ratelimiter.h/cpp # Redis 限流器实现
│   ├── ischeduler.h            # 调度器接口
│   └── rocketmq_delay_scheduler.h/cpp  # RocketMQ 延时调度器
└── third-party/                # 第三方库
    ├── hot-loader/             # 热加载库
    ├── json-3.12.0/            # JSON 库
    ├── rocketmq-client-cpp-5.0.3/  # RocketMQ 客户端
    └── yaml-cpp-0.8.0/         # YAML 解析库
```

## 核心组件说明

### RocketMQDelayScheduler
主要的调度器实现，负责：
- 从缓冲主题消费消息
- 检查时间窗口
- 应用限流策略
- 向目标主题生产消息
- 消息确认和异常处理

### IRateLimiter 接口
限流器抽象接口，定义了限流器的统一规范。所有限流器实现必须继承此接口。

**接口定义**：
```cpp
class IRateLimiter {
public:
    virtual ~IRateLimiter() noexcept = default;

    // 初始化限流器
    // config: JSON 格式的配置字符串
    virtual bool init(const std::string& config) = 0;

    // 检查是否允许一次请求（消耗一个令牌）
    virtual bool is_allowed() = 0;

    // 克隆当前限流器实例
    virtual std::shared_ptr<IRateLimiter> clone() const = 0;
};
```

**已有实现**：
- `LocalRateLimiter`: 基于本地内存的令牌桶算法实现
  - 优点：性能高，无网络开销
  - 缺点：不支持分布式限流
  - 适用场景：单机部署

- `RedisRateLimiter`: 基于 Redis 的分布式令牌桶算法实现
  - 优点：支持分布式限流，多实例共享限流状态
  - 缺点：有网络开销，依赖 Redis 服务
  - 适用场景：多实例部署

### IScheduler 接口
调度器抽象接口，定义了调度器的统一规范。所有调度器实现必须继承此接口。

**接口定义**：
```cpp
class IScheduler {
public:
    virtual ~IScheduler() noexcept = default;

    // 初始化调度器
    // config: 配置文件的路径
    virtual bool init(const std::string& config) = 0;

    // 启动调度器
    virtual void start() = 0;

    // 停止调度器（优雅停机）
    virtual void stop() = 0;

    // 克隆当前调度器实例
    virtual std::shared_ptr<IScheduler> clone() const = 0;
};
```

**已有实现**：
- `RocketMQDelayScheduler`: 基于 RocketMQ 的延时调度器实现

### 扩展系统
使用 brpc 的扩展机制实现插件化架构：

- 通过 `Global` 类在程序启动时自动注册插件
- 支持运行时根据配置文件选择不同的实现
- 便于扩展新的调度器和限流器实现
- 符合开闭原则（对扩展开放，对修改关闭）

## 性能调优

### 线程数配置
根据 CPU 核心数和消息处理复杂度调整 `worker_threads`：

```yaml
worker_threads: 8  # 推荐 CPU 核心数的 1-2 倍
```

### 批量大小
调整批量消费大小以平衡延迟和吞吐量：

```yaml
buffer_consumer_batch_size: 64  # 增加批量大小提高吞吐量
```

### 调度间隔
根据实时性要求调整调度间隔：

```yaml
scheduler_interval_seconds: 5  # 减小间隔提高实时性
```

## 注意事项

1. **时间窗口配置**：确保时间窗口不重叠且不跨天
2. **Redis 连接**：使用 Redis 限流器时，确保 Redis 服务稳定
3. **消息确认**：注意设置合理的 `invisible_duration`，避免消息重复消费
4. **资源监控**：监控 CPU、内存和网络使用情况
5. **日志管理**：生产环境建议配置日志轮转

## 故障排查

### 无法连接 RocketMQ
- 检查 `access_point` 配置是否正确
- 确认 RocketMQ 服务是否正常运行
- 检查网络连接和防火墙规则

### 消息处理延迟
- 增加 `worker_threads` 数量
- 调整 `buffer_consumer_batch_size`
- 检查限流器配置是否过于严格

### 内存占用过高
- 减少 `worker_threads` 数量
- 降低 `buffer_consumer_batch_size`
- 检查内存泄漏

## 开发指南

### 添加新的限流器

要实现自定义的限流器，需要继承 `IRateLimiter` 接口并实现以下方法：

```cpp
class MyRateLimiter : public IRateLimiter {
public:
    // 初始化限流器
    // config: JSON 格式的配置字符串
    // 返回: true 表示初始化成功，false 表示失败
    bool init(const std::string& config) override;

    // 检查是否允许一次请求（消耗一个令牌）
    // 返回: true 表示允许，false 表示被限流
    bool is_allowed() override;

    // 克隆当前限流器实例
    // 返回: 新的限流器实例
    std::shared_ptr<IRateLimiter> clone() const override;
};
```

**实现步骤**：
1. 继承 `IRateLimiter` 接口
2. 实现上述三个核心方法
3. 在 `global.cpp` 中通过 `RateLimiterExtension()->Register()` 注册实现

**示例**：
```cpp
// 在 global.cpp 中注册
RateLimiterExtension()->Register<MyRateLimiter>("my_limiter");
```

### 添加新的调度器

要实现自定义的调度器，需要继承 `IScheduler` 接口并实现以下方法：

```cpp
class MyScheduler : public IScheduler {
public:
    // 初始化调度器
    // config: 配置文件的路径或配置字符串
    // 返回: true 表示初始化成功，false 表示失败
    bool init(const std::string& config) override;

    // 启动调度器
    void start() override;

    // 停止调度器（优雅停机）
    void stop() override;

    // 克隆当前调度器实例
    // 返回: 新的调度器实例
    std::shared_ptr<IScheduler> clone() const override;
};
```

**实现步骤**：
1. 继承 `IScheduler` 接口
2. 实现上述四个核心方法
3. 在 `global.cpp` 中通过 `SchedulerExtension()->Register()` 注册实现

**示例**：
```cpp
// 在 global.cpp 中注册
SchedulerExtension()->Register<MyScheduler>("my_scheduler");
```

## 许可证

本项目采用 [许可证名称] 许可证，详见 LICENSE 文件。

## 贡献指南

欢迎提交 Issue 和 Pull Request！

## 联系方式

如有问题或建议，请提交 Issue。

## 更新日志

### v1.0.0 (当前版本)
- 支持时间窗口调度
- 实现本地和 Redis 限流器
- 支持多线程并行处理
- 完善的配置系统
