---
title: "Capstone: Mini Concurrent Runtime"
description: "组合卷五所有 Lab 的组件，构建一个 mini 并发运行时，训练系统设计、组件组合和可观测性"
chapter: 10
order: 7
difficulty: advanced
tags:
  - host
  - cpp-modern
  - coroutine
  - advanced
cpp_standard: [20]
reading_time_minutes: 9
prerequisites:
  - "Lab 0: Thread Lifecycle Lab"
  - "Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives"
  - "Lab 2: Atomic Metrics and SPSC Ring Buffer"
  - "Lab 2.5: Concurrency Debugging Lab"
  - "Lab 3: Production-style Thread Pool"
  - "Lab 4: Coroutine Scheduler and Event Loop"
  - "Lab 5: Channel or Actor Runtime"
---

# Capstone: Mini Concurrent Runtime

## 目标

卷五从"学过很多并发工具"到这里要收束成"能组合并发系统"。这个 Capstone 不追求生产级完整性，而是要求你把前面 7 个 Lab 的成品组件组合在一起，构建一个可以运行的小系统——一个 mini concurrent runtime 或网络服务框架。

重点不在于从零实现新组件，而在于回答三个工程问题：组件之间怎么连接？系统怎么停止？出错了怎么传播和处理？

## 前置知识

完成全部 Lab 0–5。这个 Capstone 直接复用前面 Lab 的组件。

## 环境准备

与 Lab 4 相同（C++20，Linux/WSL2 for epoll，Catch2 v3，TSan）。

## 推荐组成

下面是 mini runtime 的推荐组件列表。每个组件来自一个之前的 Lab：

| 组件 | 来源 Lab | 职责 |
|------|----------|------|
| `JoiningThread` | Lab 0 | 线程生命周期管理 |
| `BoundedBlockingQueue` | Lab 1 | 任务队列 / channel 底层 |
| `ConcurrentCache` | Lab 1 | 配置缓存 / 连接池 |
| `AtomicCounter` / `AtomicMaxTracker` | Lab 2 | 运行时指标 |
| `StopFlag` | Lab 2 | 优雅停止信号 |
| `ThreadPool` | Lab 3 | CPU-bound 任务调度 |
| `Scheduler` + `EventLoop` | Lab 4 | 协程调度 + I/O 事件循环 |
| `Channel` | Lab 5 | 组件间通信 / pipeline |

## Milestone 1: 架构设计与接口定义

### 目标

画出 mini runtime 的组件图，定义各组件之间的交互接口。不要写任何实现代码——这个 milestone 纯粹是设计。

### 为什么

系统设计的第一步不是写代码，而是搞清楚组件之间的关系和职责边界。特别是"谁来创建谁""谁拥有谁""谁可以关闭谁"这三个问题。在并发系统中，这些问题比单线程系统重要得多——一个错误的所有权关系可能导致死锁、资源泄漏或者在关闭时崩溃。

### 实现指引

用一段文字或图描述你的 runtime 的架构。建议从"请求从进入到离开的完整路径"开始：

```cpp
客户端请求 → epoll accept → 协程 handle_connection
    → Channel 传递给 worker pipeline
    → ThreadPool 处理 CPU-bound 任务
    → 结果通过 future 返回
    → 协程 write response → 客户端
```

在这个路径上标注每个组件的职责和生命周期关系。比如：`EventLoop` 拥有 epoll fd 和协程调度器；`ThreadPool` 拥有 worker 线程和任务队列；`Channel` 连接协程层和线程池层。

你需要回答以下设计问题：

1. `EventLoop` 和 `ThreadPool` 谁先创建、谁先关闭？
2. `Channel` 的 close 由谁负责——生产者还是消费者？
3. 一个组件的异常如何传播到其他组件？

### 验证

跟同伴或 AI 讨论你的设计方案，确认没有遗漏的边界情况。不需要写代码，但需要能回答上面三个设计问题。

## Milestone 2: 组件组装与启动

### 目标

把所有 Lab 的组件组合在一起，实现 runtime 的启动流程。不需要处理网络请求——只需要确认所有组件正确初始化和运行。

### 为什么

组件的启动顺序很重要。`ThreadPool` 需要在 `Channel` 之前创建（因为 worker 线程需要从 channel 取任务），`EventLoop` 需要在 `ThreadPool` 之前创建（因为协程调度在 I/O 事件之前）。这个 milestone 的目标是确认启动顺序正确，组件之间的依赖关系没有循环。

### 实现指引

定义一个 `MiniRuntime` 类，按正确顺序创建和持有所有组件：

```cpp
class MiniRuntime {
public:
    MiniRuntime()
        : metrics_()
        , task_queue_(256)
        , thread_pool_(4)
        , channels_()
        , event_loop_()
        , stop_flag_()
    {
        // 注册 metrics 回调
        // 启动 event loop 线程（如果需要独立线程）
    }

    void start();
    void stop();

private:
    AtomicCounter active_tasks_;
    AtomicMaxTracker max_connections_;
    StopFlag stop_flag_;
    ThreadPool thread_pool_;
    Channel<Request> request_channel_;
    EventLoop event_loop_;
};
```

踩坑预警：成员的声明顺序就是初始化顺序，析构顺序是逆序。确保 `ThreadPool` 在 `BoundedBlockingQueue` 之前析构（因为 worker 线程需要从队列取数据直到队列关闭），`EventLoop` 在所有 channel 之前析构。

### 验证

```cpp
TEST_CASE("Milestone 2: runtime starts and stops cleanly",
          "[capstone][milestone2]")
{
    MiniRuntime runtime;
    runtime.start();

    // 提交一些测试任务
    auto f1 = runtime.thread_pool().submit([]() {
        return 42;
    });
    REQUIRE(f1.get() == 42);

    runtime.stop();

    // stop 后不应该崩溃
    // 所有 worker 线程应该已经退出
}
```cpp

## Milestone 3: 失败路径测试

### 目标

测试 runtime 在各种失败场景下的行为：任务抛异常、客户端断开、队列关闭、组件异常。

### 为什么

并发系统的正确性不只体现在"正常路径"上。一个生产级系统必须能优雅地处理各种失败——任务执行失败不应该导致整个 runtime 崩溃，客户端断开不应该导致资源泄漏，组件异常应该被捕获并报告而不是静默丢失。

### 实现指引

测试以下场景：

1. **任务异常**：提交一个会抛异常的任务，确认 `future::get()` 能重新抛出，且 runtime 继续正常运行
2. **客户端断开**：模拟客户端在协程处理过程中断开连接，确认协程正确退出且不泄漏资源
3. **队列关闭**：在 pipeline 运行过程中关闭中间的 channel，确认上游和下游都正确处理
4. **重复停止**：多次调用 `stop()`，确认幂等性

### 验证

```cpp
TEST_CASE("Milestone 3: task exception doesn't crash runtime",
          "[capstone][milestone3]")
{
    MiniRuntime runtime;
    runtime.start();

    auto f1 = runtime.thread_pool().submit([]() {
        throw std::runtime_error("boom");
    });
    auto f2 = runtime.thread_pool().submit([]() {
        return 42;
    });

    REQUIRE_THROWS_AS(f1.get(), std::runtime_error);
    REQUIRE(f2.get() == 42);  // 其他任务不受影响

    runtime.stop();
}

TEST_CASE("Milestone 3: double stop is safe",
          "[capstone][milestone3]")
{
    MiniRuntime runtime;
    runtime.start();
    runtime.stop();
    REQUIRE_NOTHROW(runtime.stop());  // 幂等
}

TEST_CASE("Milestone 3: channel close propagates through pipeline",
          "[capstone][milestone3]")
{
    Channel<int> input(8);
    Channel<int> output(8);

    JoiningThread stage([&]() {
        while (auto val = input.receive()) {
            output.send(*val * 2);
        }
        output.close();
    });

    input.send(1);
    input.send(2);
    input.close();  // 关闭触发 pipeline 关闭

    REQUIRE(output.receive() == 2);
    REQUIRE(output.receive() == 4);
    REQUIRE(output.receive() == std::nullopt);
}
```

## Milestone 4: 可观测性与性能验证

### 目标

给 runtime 加入指标采集（`AtomicCounter`、`AtomicMaxTracker`），实现至少一个端到端的 benchmark，用 TSan 验证正确性。

### 为什么

一个没有可观测性的并发系统就像一个黑盒——你不知道它在干什么、性能如何、有没有问题。Lab 2 的 atomic metrics 组件在这里发挥作用：统计已完成的任务数、当前队列长度、最大并发连接数。这些指标不需要精确到毫秒级别——它们的价值在于让你看到"系统在运行"和"系统在恶化"。

### 实现指引

在 runtime 的关键路径上插入指标采集点：

- 任务提交时 `active_tasks_.increment()`
- 任务完成时 `active_tasks_.decrement()`
- 新连接建立时 `max_connections_.update(current_connections)`
- 队列长度定期采样（可选）

写一个端到端 benchmark：启动 runtime，提交 N 个任务，等待所有 future 完成，报告总耗时和吞吐量。参考 `.claude/chapter-projects-outline.md` 中的 benchmark 方法论。

最后，用 TSan 运行完整的测试套件，确认没有 data race。

### 验证

```cpp
TEST_CASE("Milestone 4: metrics track runtime behavior",
          "[capstone][milestone4]")
{
    MiniRuntime runtime;
    runtime.start();

    std::vector<std::future<int>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(
            runtime.thread_pool().submit([i]() {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1));
                return i;
            }));
    }

    for (auto& f : futures) f.get();

    REQUIRE(runtime.total_tasks_completed() == 100);

    runtime.stop();
}
```

## 自查清单

- [ ] 所有 Lab 0–5 的组件被正确组合
- [ ] 组件的创建顺序和析构顺序正确（无循环依赖、无悬空引用）
- [ ] `stop()` 是幂等的，不会死锁或泄漏
- [ ] 有清晰的 shutdown 顺序：停止接受新请求 → 排空队列 → join 所有线程
- [ ] 任务异常不会导致 runtime 崩溃
- [ ] Channel 关闭正确传播到 pipeline 的所有阶段
- [ ] 指标采集不影响正确性（用 `relaxed` atomic）
- [ ] 至少有一个端到端 benchmark，报告了吞吐量
- [ ] 完整测试套件在 TSan 下无 data race 报告
- [ ] 能回答：哪些地方用锁，哪些地方用 atomic，哪些地方通过消息传递避免共享状态
- [ ] 能解释 benchmark 结果不能证明什么（比如"单机测试不代表网络环境下的表现"）
- [ ] 能说明如果有更多时间，你会优先改进哪个组件
