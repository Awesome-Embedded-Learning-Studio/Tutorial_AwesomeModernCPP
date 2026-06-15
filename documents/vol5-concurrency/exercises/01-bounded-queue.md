---
chapter: 10
cpp_standard:
- 17
- 20
description: 通过阻塞队列、分片缓存和 C++20 同步原语实践，掌握 mutex、condition_variable、关闭语义和背压策略
difficulty: intermediate
order: 1
prerequisites:
- '卷五 ch00: 并发思维与基础'
- '卷五 ch01: 线程生命周期与 RAII'
- '卷五 ch02: 互斥量、条件变量与同步原语'
- 'Lab 0: Thread Lifecycle Lab'
reading_time_minutes: 21
tags:
- host
- cpp-modern
- mutex
- intermediate
title: 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
---
# Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives

## 目标

Lab 0 让我们跑通了多线程的基本骨架——创建线程、RAII 包装、参数安全传递。但那些代码有一个共同特点：所有线程都是"各干各的"，主线程只是等它们结束。真实的并发系统远不是这样——线程之间需要协作，生产者往队列里塞数据，消费者从队列里取数据，队列满了要背压，队列关了要优雅退出。

这个 Lab 的核心产物是三个组件：一个带关闭语义的 `BoundedBlockingQueue<T>`，一个分片锁的 `ConcurrentCache<K, V>`，以及用 C++20 的 `latch`、`barrier`、`counting_semaphore` 实现的经典并发模式。这三个组件不是孤立的练习——Lab 3 的线程池会直接复用 `BoundedBlockingQueue` 作为任务队列，Capstone 项目会组合所有这些组件。

完成这个 Lab 之后，你应该对 mutex + condition_variable 的组合拳有肌肉记忆，能正确处理谓词等待、虚假唤醒、丢失唤醒和关闭唤醒这四种等待场景，并且理解粗粒度锁 vs 细粒度锁的性能权衡。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch02-01**：mutex 与 RAII 锁 — `std::mutex`、`lock_guard`、`unique_lock`、`scoped_lock`
- **ch02-02**：死锁与锁顺序 — 死锁预防、`std::scoped_lock` 多锁同时获取
- **ch02-03**：condition_variable 与等待语义 — 谓词等待、虚假唤醒、notify_one vs notify_all
- **ch02-04**：shared_mutex 与读写锁 — 共享锁、读写分离场景
- **ch02-05**：latch、barrier 与 semaphore — C++20 同步原语
- **Lab 0**：`JoiningThread` 的实现和使用

这个 Lab 直接依赖 Lab 0 的 `JoiningThread` 组件。

## 环境准备

与 Lab 0 相同的编译器和 Catch2 配置。新增要求：

- **C++20**：Milestone 6 需要用到 `std::latch`、`std::barrier`、`std::counting_semaphore`，需要 GCC 12+ 或 Clang 15+ 并开启 `-std=c++20`
- **pthread**：Linux 上链接 `-pthread`

CMakeLists.txt 在 Lab 0 的基础上把 `CMAKE_CXX_STANDARD` 改为 20，并确保链接 pthread：

```cmake
cmake_minimum_required(VERSION 3.14)
project(lab1_bounded_queue LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(lab1_tests tests/main.cpp)
target_link_libraries(lab1_tests PRIVATE Catch2::Catch2WithMain)

target_compile_options(lab1_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread -g>
)
target_link_options(lab1_tests PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=thread>
)
```

## 最终接口

### `BoundedBlockingQueue<T>` — 带关闭语义的有界阻塞队列

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::queue<T>` | `queue_` | 内部数据存储 |
| `mutable std::mutex` | `mutex_` | 保护队列状态的互斥量 |
| `std::condition_variable` | `not_full_` | 生产者等待条件（队列不满） |
| `std::condition_variable` | `not_empty_` | 消费者等待条件（队列不空） |
| `std::size_t` | `capacity_` | 队列容量上限 |
| `bool` | `closed_` | 关闭标志 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `BoundedBlockingQueue(size_t capacity)` | 设置队列容量 | MS1 |
| push | `bool push(T item)` | 阻塞写入；关闭后返回 false | MS1 |
| pop | `std::optional<T> pop()` | 阻塞读取；关闭且空时返回 nullopt | MS1 |
| close | `void close()` | 关闭队列，唤醒所有等待线程 | MS2 |
| is_closed | `bool is_closed() const` | 查询关闭状态 | MS2 |
| try_push_for | `bool try_push_for(T, milliseconds)` | 带超时写入 | MS3 |
| try_pop_for | `std::optional<T> try_pop_for(milliseconds)` | 带超时读取 | MS3 |
| size | `size_t size() const` | 当前队列长度 | MS1 |

### `ConcurrentCache<K, V>` — 分片锁缓存（Milestone 5）

内部定义 `Shard` 结构体，包含 `std::shared_mutex` + `std::unordered_map<K, V>`。

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::vector<Shard>` | `shards_` | 分片数组，默认 16 个 |
| `std::hash<K>` | `hasher_` | 用于哈希 key 到分片 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `ConcurrentCache(size_t num_shards = 16)` | 设置分片数量 | MS5 |
| put | `void put(const K&, const V&)` | 写入键值对（独占锁） | MS5 |
| get | `std::optional<V> get(const K&)` | 查询值（共享锁） | MS5 |
| erase | `void erase(const K&)` | 删除键 | MS5 |
| size | `size_t size() const` | 总条目数 | MS5 |

## Milestone 1: 固定容量阻塞队列

### 目标

实现 `BoundedBlockingQueue<T>` 的 `push` 和 `pop` 方法——固定容量、阻塞写入、阻塞读取。这个 milestone 先不管关闭语义和超时，只关注最基本的 mutex + condition_variable 协作。

### 为什么

阻塞队列是并发编程中最经典的同步组件，也是 mutex 和 condition_variable 最直观的应用场景。它把"生产者-消费者"这个抽象模型变成了一个具体的、可测试的数据结构。后续所有的 milestone 都在这个基础上叠加功能——关闭、超时、背压——所以先把它做对。

### 实现指引

核心数据结构很简单：一个 `std::queue<T>`、一个 `std::mutex`、两个 `std::condition_variable`（一个 `not_full_` 给生产者用，一个 `not_empty_` 给消费者用），以及一个容量上限。

`push` 的逻辑是：加锁 → 检查队列是否满了 → 如果满了就 `wait` 在 `not_full_` 上 → 把元素塞进队列 → `notify_one` 唤醒一个消费者。`pop` 是镜像操作：加锁 → 检查队列是否空了 → 如果空了就 `wait` 在 `not_empty_` 上 → 取出元素 → `notify_one` 唤醒一个生产者。

这里有几个必须用谓词等待（predicated wait）的地方。`push` 里的等待不能写成 `not_full_.wait(lock)`，必须写成 `not_full_.wait(lock, [&]{ return queue_.size() < capacity_; })`。为什么？因为 condition_variable 有两个恼人的特性——虚假唤醒（spurious wakeup，没有 notify 也能醒）和丢失唤醒（lost wakeup，notify 发生在 wait 之前）。谓词等待同时解决了这两个问题：每次被唤醒（无论是真的还是虚假的）都重新检查条件，条件不满足就继续等。

踩坑预警：如果你用了 `notify_one` 而不是 `notify_all`，要确认被唤醒的那个线程确实能继续工作。在我们的场景中，一个 push 操作最多释放一个消费者（队列从不空变成不空），所以 `notify_one` 是正确的。但如果你在某个地方改成了批量操作（比如 `push_n`），就可能需要 `notify_all`。

### 验证

```cpp
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>
#include <atomic>

TEST_CASE("Milestone 1: single producer single consumer",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(5);
    const int kItems = 100;
    std::atomic<int> sum{0};

    // 生产者
    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            queue.push(i);
        }
    });

    // 消费者
    JoiningThread consumer([&]() {
        for (int i = 0; i < kItems; ++i) {
            auto val = queue.pop();
            if (val) {
                sum += *val;
            }
        }
    });

    // 等待完成后验证
    // sum 应该等于 1+2+...+100 = 5050
    // 注意：因为队列没有关闭，消费者目前会死锁
    // 这个测试需要 Milestone 2 的 close() 才能正确运行
    // 现在先验证 push/pop 基本功能
}

TEST_CASE("Milestone 1: queue respects capacity",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(3);

    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));
    REQUIRE(queue.push(3));
    // 队列满了，下一个 push 会阻塞
    // 需要先 pop 一个才能继续 push
    auto val = queue.pop();
    REQUIRE(val.has_value());
    REQUIRE(*val == 1);
    REQUIRE(queue.push(4));  // 现在有空间了
}

TEST_CASE("Milestone 1: multiple producers multiple consumers",
          "[lab1][milestone1]")
{
    BoundedBlockingQueue<int> queue(10);
    const int kProducers = 4;
    const int kItemsPerProducer = 50;
    const int kTotalItems = kProducers * kItemsPerProducer;

    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};
    std::atomic<int> consumed_count{0};

    std::vector<JoiningThread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                int val = p * kItemsPerProducer + i + 1;
                queue.push(val);
                produced_sum += val;
            }
        });
    }

    // 单个消费者收集所有数据
    JoiningThread consumer([&]() {
        while (consumed_count.load() < kTotalItems) {
            auto val = queue.pop();
            if (val) {
                consumed_sum += *val;
                consumed_count.fetch_add(1);
            }
        }
    });

    // 注意：这个测试在生产者全部 push 完后，消费者恰好消费完时结束
    // 实际上需要 close() 来正确终止，见 Milestone 2
}
```

## Milestone 2: 关闭语义

### 目标

给 `BoundedBlockingQueue` 加入 `close()` 方法。关闭后不能再 push（返回 `false`），但队列中已有的元素仍可 pop（返回剩余数据），队列空且关闭后 pop 返回 `std::nullopt`。正在阻塞等待的 push 和 pop 都必须被唤醒。

### 为什么

没有关闭语义的阻塞队列是一个定时炸弹。考虑一个典型的生产者-消费者场景：生产者线程已经结束（文件读完了、数据生成完了），但消费者还在 `pop()` 上阻塞——等待一个永远不会到来的数据。程序就这么挂住了。`close()` 就是告诉消费者"没有新数据了，你可以走了"的工具。它不仅仅是一个 API 方法，而是整个并发组件生命周期管理的关键一环——后面 Lab 3 的线程池 shutdown、Lab 5 的 Channel close，都是同一个模式。

### 实现指引

`close()` 的实现思路是：加锁 → 设 `closed_` 为 true → `notify_all` 唤醒所有在等的生产者和消费者。关键在于 `push` 和 `pop` 的等待循环里需要加上 `closed_` 的检查。

`push` 的等待变成：`not_full_.wait(lock, [&]{ return queue_.size() < capacity_ || closed_; })`。被唤醒后先检查 `closed_`，如果关了就直接返回 `false`，不再塞数据。`pop` 的等待变成：`not_empty_.wait(lock, [&]{ return !queue_.empty() || closed_; })`。被唤醒后如果队列为空且 `closed_`，返回 `std::nullopt`；如果队列不为空（可能关闭了但还有数据），正常取出元素。

这里有一个容易忽略的微妙之处：`push` 在检查到 `closed_` 之后返回 `false`，但这并不意味着这个元素没有被塞进去——它确实没有。但如果你在 `close()` 之前恰好有一个 `push` 正在等待 `not_full_`，`close()` 会把它唤醒，它检查到 `closed_` 后返回 `false`。这个行为是合理的——调用者知道队列关了，就不会再尝试。

踩坑预警：`close()` 必须用 `notify_all()` 而不是 `notify_one()`。因为 `close()` 是一个"全局事件"——所有在等的线程都需要知道状态变了。用 `notify_one()` 可能只唤醒一个线程，其他线程继续阻塞。

### 验证

```cpp
TEST_CASE("Milestone 2: close prevents further pushes",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(5);

    REQUIRE(queue.push(1));
    REQUIRE(queue.push(2));

    queue.close();
    REQUIRE(queue.is_closed());

    // 关闭后 push 应该失败
    REQUIRE_FALSE(queue.push(3));
}

TEST_CASE("Milestone 2: close allows draining remaining items",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(5);

    queue.push(10);
    queue.push(20);
    queue.push(30);
    queue.close();

    // 关闭后仍可 pop 已有数据
    REQUIRE(queue.pop() == 10);
    REQUIRE(queue.pop() == 20);
    REQUIRE(queue.pop() == 30);

    // 耗尽后返回 nullopt
    REQUIRE(queue.pop() == std::nullopt);
}

TEST_CASE("Milestone 2: close wakes blocked threads",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(2);

    // 塞满队列
    queue.push(1);
    queue.push(2);

    // push 会阻塞（队列满了）
    std::atomic<bool> push_returned{false};
    JoiningThread t([&]() {
        bool ok = queue.push(3);
        push_returned.store(true);
        // 应该返回 false（被 close 唤醒）
    });

    // 等一小段时间确保线程进入了 wait
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.close();

    // push 线程应该被唤醒并返回
    // (JoiningThread 析构时会 join，确保线程结束)
}

TEST_CASE("Milestone 2: producer-consumer with close",
          "[lab1][milestone2]")
{
    BoundedBlockingQueue<int> queue(10);
    const int kItems = 100;
    std::vector<int> consumed;
    std::mutex consumed_mutex;

    // 生产者：生产完就关闭队列
    JoiningThread producer([&]() {
        for (int i = 1; i <= kItems; ++i) {
            queue.push(i);
        }
        queue.close();
    });

    // 消费者：pop 到 nullopt 就停止
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            std::lock_guard lock(consumed_mutex);
            consumed.push_back(*val);
        }
    });

    // consumed 应该包含 1..100
    REQUIRE(consumed.size() == kItems);
    // 验证总和
    int sum = 0;
    for (int v : consumed) sum += v;
    REQUIRE(sum == kItems * (kItems + 1) / 2);
}
```

## Milestone 3: 超时等待

### 目标

实现 `try_push_for` 和 `try_pop_for`，支持带超时的等待。如果指定时间内队列状态没有变化，返回失败而不是无限等待。

### 为什么

在实际系统中，无限等待是危险的——如果消费者的处理速度突然变慢（比如下游服务超时），生产者可能整组线程都卡在 `push` 上。超时等待让调用者有机会在等待过长时采取其他策略：重试、丢弃、记录告警、或者降级。后面 Milestone 4 的背压策略会直接用到超时等待。

### 实现指引

`try_push_for` 和 `try_push` 的区别只是把 `wait` 换成 `wait_for`。`condition_variable::wait_for(lock, timeout, predicate)` 在超时或被唤醒时都会检查谓词，如果谓词不满足且超时了，就返回 `false`。

伪代码如下：

```cpp
bool try_push_for(T item, milliseconds timeout) {
    unique_lock lock(mutex_);
    bool ok = not_full_.wait_for(lock, timeout,
        [&] { return queue_.size() < capacity_ || closed_; });

    if (closed_) return false;
    if (!ok) return false;  // 超时

    queue_.push(std::move(item));
    not_empty_.notify_one();
    return true;
}
```

踩坑预警：`wait_for` 返回 `false` 并不一定是超时了——也可能是被唤醒了但谓词仍然不满足。你需要区分"超时了"和"被虚假唤醒了但条件还不满足"这两种情况。实际上用谓词版本的 `wait_for` 时，返回值就是"谓词是否满足"——`true` 满足，`false` 不满足（可能是超时也可能是其他原因）。在你的逻辑里，如果返回 `false`，就意味着在超时时间内没能成功操作。

### 验证

```cpp
TEST_CASE("Milestone 3: try_push_for times out on full queue",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(2);
    queue.push(1);
    queue.push(2);

    auto start = std::chrono::steady_clock::now();
    bool ok = queue.try_push_for(3, std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(ok);
    // 应该在 100ms 左右超时，而不是立即返回
    REQUIRE(elapsed >= std::chrono::milliseconds(80));
}

TEST_CASE("Milestone 3: try_pop_for times out on empty queue",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(5);

    auto start = std::chrono::steady_clock::now();
    auto val = queue.try_pop_for(std::chrono::milliseconds(100));
    auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE_FALSE(val.has_value());
    REQUIRE(elapsed >= std::chrono::milliseconds(80));
}

TEST_CASE("Milestone 3: try_push_for succeeds when space available",
          "[lab1][milestone3]")
{
    BoundedBlockingQueue<int> queue(5);

    bool ok = queue.try_push_for(42, std::chrono::milliseconds(100));
    REQUIRE(ok);

    auto val = queue.try_pop_for(std::chrono::milliseconds(100));
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);
}
```

## Milestone 4: 背压策略

### 目标

在 `BoundedBlockingQueue` 的基础上实现两种背压策略：**阻塞等待**（已有）和 **调用者执行（caller-runs）**。写一个 producer-consumer pipeline，对比两种策略在不同生产/消费速度比下的行为。

### 为什么

背压（backpressure）是并发系统中的核心工程问题。当生产者比消费者快时，如果没有背压机制，队列会无限增长（如果是无界队列）或者生产者阻塞（如果是阻塞队列）。阻塞是最简单的背压，但它会占用一个线程——如果所有生产者都阻塞了，系统就卡死了。caller-runs 策略是一种替代方案：当队列满时，不让生产者阻塞，而是让生产者自己执行消费者的工作——既减轻了队列压力，又不会浪费线程。

### 实现指引

阻塞策略已经在 Milestone 1 里实现了。caller-runs 策略的核心思路是：如果队列满了，不调用 `push`，而是直接在当前线程（生产者线程）上执行消费者逻辑。

伪代码：

```cpp

// caller-runs 策略的提交逻辑
void submit_with_caller_runs(BoundedBlockingQueue<Task>& queue,
                              Task task,
                              std::function<void(Task&)> processor)
{
    if (!queue.try_push_for(std::move(task),
                            std::chrono::milliseconds(0))) {
        // 队列满了，生产者自己执行
        processor(task);
    }
}

```

你需要写一个简单的 benchmark 来对比两种策略：固定生产速率（比如每秒 1000 个任务），让消费者的处理速度可调（通过 `sleep_for` 模拟），观察队列长度和吞吐量在不同速率比下的变化。不需要追求精确的数字，重点是用数据说明两种策略各自的适用场景。

### 验证

```cpp
TEST_CASE("Milestone 4: blocking strategy backpressures producers",
          "[lab1][milestone4]")
{
    BoundedBlockingQueue<int> queue(5);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    // 慢速消费者
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
            consumed.fetch_add(1);
        }
    });

    // 快速生产者：队列满了就阻塞
    JoiningThread producer([&]() {
        for (int i = 0; i < 50; ++i) {
            queue.push(i);
            produced.fetch_add(1);
        }
        queue.close();
    });

    // producer 会被阻塞在 push 上，因为消费者太慢
    // 验证 produced 和 consumed 最终一致
}

TEST_CASE("Milestone 4: caller-runs avoids blocking",
          "[lab1][milestone4]")
{
    BoundedBlockingQueue<int> queue(5);
    std::atomic<int> processed_by_caller{0};
    std::atomic<int> processed_by_consumer{0};

    auto processor = [&](int val) {
        // 模拟处理
    };

    // caller-runs 提交
    for (int i = 0; i < 20; ++i) {
        if (!queue.try_push_for(i,
                std::chrono::milliseconds(0))) {
            processor(i);
            processed_by_caller.fetch_add(1);
        }
    }
    queue.close();

    // 消费者处理队列中的任务
    JoiningThread consumer([&]() {
        while (auto val = queue.pop()) {
            processor(*val);
            processed_by_consumer.fetch_add(1);
        }
    });

    // 验证：caller 处理了一部分，消费者处理了一部分
    int total = processed_by_caller.load() +
                processed_by_consumer.load();
    REQUIRE(total == 20);
}
```

## Milestone 5: 分片锁缓存

### 目标

实现 `ConcurrentCache<K, V>`，使用分片锁（sharded locking）来减少锁竞争。对比单锁缓存的吞吐量，观察分片数量对性能的影响。

### 为什么

`BoundedBlockingQueue` 用的是一把 mutex 保护整个队列——在多线程高并发场景下，这把锁可能成为瓶颈。分片锁是一种常见的优化思路：把数据分成 N 个分片（shard），每个分片有自己的锁，不同分片可以并行访问。哈希函数决定一个 key 属于哪个分片，操作时只锁对应的分片。这样不同 key 的操作就不再竞争同一把锁了。ch02-04 讲过 `shared_mutex` 的读写分离，这里我们可以进一步用 `shared_mutex` 来实现读写分片——读操作用共享锁，写操作用独占锁。

### 实现指引

`ConcurrentCache` 的核心数据结构是 `std::vector<Shard>`，每个 `Shard` 包含一个 `std::shared_mutex` 和一个 `std::unordered_map<K, V>`。`put` 和 `get` 操作先通过 `std::hash<K>` 算出 key 的哈希值，然后对分片数量取模，得到目标分片，再锁定该分片进行操作。

`put` 的伪代码：

```cpp
void put(const K& key, const V& value) {
    auto& shard = get_shard(key);           // 哈希到具体分片
    unique_lock lock(shard.mutex);          // 独占锁
    shard.map[key] = value;
}
```

`get` 的伪代码：

```cpp
optional<V> get(const K& key) {
    auto& shard = get_shard(key);
    shared_lock lock(shard.mutex);          // 共享锁，允许多读
    auto it = shard.map.find(key);
    if (it != shard.map.end()) {
        return it->second;
    }
    return nullopt;
}
```

分片数量通常选择 2 的幂（16、32、64），方便用位运算取模。数量太少（比如 1）退化为单锁，太多（比如 1024）浪费内存。16 是一个不错的起点。

### 验证

```cpp
TEST_CASE("Milestone 5: concurrent put and get",
          "[lab1][milestone5]")
{
    ConcurrentCache<int, std::string> cache(16);

    // 并发写入
    std::vector<JoiningThread> writers;
    for (int i = 0; i < 8; ++i) {
        writers.emplace_back([&cache, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                cache.put(key,
                    "value_" + std::to_string(key));
            }
        });
    }

    // 并发读取
    std::atomic<int> hits{0};
    std::vector<JoiningThread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&cache, &hits, i]() {
            for (int j = 0; j < 100; ++j) {
                int key = i * 100 + j;
                if (cache.get(key)) {
                    hits.fetch_add(1);
                }
            }
        });
    }

    // 验证所有写入的数据都能读到
    for (int i = 0; i < 800; ++i) {
        auto val = cache.get(i);
        REQUIRE(val.has_value());
        REQUIRE(*val == "value_" + std::to_string(i));
    }
}

TEST_CASE("Milestone 5: erase removes entries",
          "[lab1][milestone5]")
{
    ConcurrentCache<std::string, int> cache(4);

    cache.put("a", 1);
    cache.put("b", 2);

    REQUIRE(cache.get("a") == 1);
    cache.erase("a");
    REQUIRE_FALSE(cache.get("a").has_value());
    REQUIRE(cache.get("b") == 2);
}
```

## Milestone 6: C++20 同步原语实践

### 目标

用 `std::latch`、`std::barrier`、`std::counting_semaphore` 分别实现三个经典并发模式：fork-join、分阶段并行处理和资源池。

### 为什么

ch02-05 介绍了这三个 C++20 同步原语的 API，但光看 API 不如在真实场景中用过之后来得深刻。这三个原语各自解决一类特定的同步问题——latch 解决"等待一组任务完成"的问题，barrier 解决"多轮同步"的问题，semaphore 解决"限制并发访问数量"的问题。在实际工程中，它们比手写的 mutex + condition_variable 组合更简洁、更不容易出错。

### 实现指引

**fork-join 模式**（`std::latch`）：主线程派发 N 个任务到线程池，用 latch 等待全部完成后汇总结果。

```cpp

void fork_join_example() {
    const int kTasks = 8;
    latch done(kTasks);
    vector<int> results(kTasks);

    for (int i = 0; i < kTasks; ++i) {
        JoiningThread([&done, &results, i]() {
            results[i] = compute(i);
            done.count_down();    // 完成一个任务
        });
    }

    done.wait();  // 等待所有任务完成
    // 汇总 results
}

```

**分阶段并行处理**（`std::barrier`）：多轮 map-reduce，每轮结束后 barrier 同步，确保前一阶段的输出是后一阶段的输入。

```cpp

void phased_parallel_example() {
    const int kWorkers = 4;
    barrier sync_point(kWorkers, `[]()` noexcept {
        // 每轮结束后的回调（可选）
    });

    vector<JoiningThread> workers;
    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&sync_point, i]() {
            // Phase 1: map
            do_map_phase(i);
            sync_point.arrive_and_wait();

            // Phase 2: reduce
            do_reduce_phase(i);
            sync_point.arrive_and_wait();

            // Phase 3: sort
            do_sort_phase(i);
        });
    }
}

```

**资源池**（`std::counting_semaphore`）：模拟数据库连接池，最多 5 个连接，多个线程竞争获取。

```cpp

void resource_pool_example() {
    counting_semaphore<5> pool(5);  // 5 个连接
    const int kClients = 20;

    vector<JoiningThread> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&pool, i]() {
            pool.acquire();           // 获取连接（最多 5 个并发）
            use_database(i);          // 使用连接
            pool.release();           // 释放连接
        });
    }
}
```

踩坑预警：`barrier` 的回调函数必须是 `noexcept` 的。如果你的回调会抛异常，编译会报错。`counting_semaphore` 的 acquire/release 不要求在同一线程——生产者可以 release，消费者可以 acquire，这跟 mutex 的 lock/unlock 必须同一线程不同。

### 验证

```cpp
TEST_CASE("Milestone 6: latch fork-join collects all results",
          "[lab1][milestone6]")
{
    const int kTasks = 8;
    std::latch done(kTasks);
    std::vector<int> results(kTasks, 0);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kTasks; ++i) {
        threads.emplace_back([&done, &results, i]() {
            results[i] = i * i;
            done.count_down();
        });
    }

    done.wait();

    // 所有任务都完成了
    for (int i = 0; i < kTasks; ++i) {
        REQUIRE(results[i] == i * i);
    }
}

TEST_CASE("Milestone 6: barrier synchronizes phases",
          "[lab1][milestone6]")
{
    const int kWorkers = 4;
    std::atomic<int> phase1_done_count{0};
    std::atomic<int> phase2_started_count{0};

    std::barrier sync(kWorkers);

    std::vector<JoiningThread> threads;
    for (int i = 0; i < kWorkers; ++i) {
        threads.emplace_back([&, i]() {
            // Phase 1
            phase1_done_count.fetch_add(1);
            sync.arrive_and_wait();

            // Phase 2: 确保 Phase 1 全部完成
            REQUIRE(phase1_done_count.load() == kWorkers);
            phase2_started_count.fetch_add(1);
        });
    }
}

TEST_CASE("Milestone 6: semaphore limits concurrency",
          "[lab1][milestone6]")
{
    std::counting_semaphore<5> sem(5);
    std::atomic<int> max_concurrent{0};
    std::atomic<int> current{0};

    const int kClients = 20;
    std::vector<JoiningThread> threads;
    for (int i = 0; i < kClients; ++i) {
        threads.emplace_back([&]() {
            sem.acquire();
            int c = current.fetch_add(1) + 1;
            // 更新最大并发数
            int old_max = max_concurrent.load();
            while (c > old_max &&
                   !max_concurrent.compare_exchange_weak(
                       old_max, c)) {}

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));

            current.fetch_sub(1);
            sem.release();
        });
    }

    // 最大并发数不应超过 5
    REQUIRE(max_concurrent.load() <= 5);
    REQUIRE(max_concurrent.load() >= 1);
}
```

## 自查清单

- [ ] Milestone 1：`push` 和 `pop` 使用谓词等待，不存在虚假唤醒和丢失唤醒
- [ ] Milestone 2：`close()` 后不能再 push，已有数据可 pop，阻塞线程被唤醒
- [ ] Milestone 3：`try_push_for` 和 `try_pop_for` 在超时后正确返回
- [ ] Milestone 4：两种背压策略的行为符合预期，有简单的性能对比数据
- [ ] Milestone 5：分片缓存在多线程压力测试下数据正确，TSan 无 data race
- [ ] Milestone 6：latch、barrier、semaphore 的使用场景正确，测试通过
- [ ] 全部测试在 TSan 下无 data race 报告
- [ ] 能解释 `notify_one` vs `notify_all` 的使用时机
- [ ] 能解释 `close()` 为什么必须用 `notify_all`
- [ ] 能解释分片锁相比单锁的性能优势和代价（额外的内存、哈希计算开销）
- [ ] 能口头说明 `BoundedBlockingQueue` 将在 Lab 3 线程池中被复用
