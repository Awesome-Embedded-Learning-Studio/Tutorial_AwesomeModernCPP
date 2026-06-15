---
chapter: 10
cpp_standard:
- 17
- 20
description: 实现固定大小线程池，掌握 future、packaged_task、异常传播、优雅关闭和背压策略
difficulty: advanced
order: 4
prerequisites:
- '卷五 ch05: future、任务与线程池'
- 'Lab 0: Thread Lifecycle Lab'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
reading_time_minutes: 12
tags:
- host
- cpp-modern
- advanced
title: 'Lab 3: Production-style Thread Pool'
---
# Lab 3: Production-style Thread Pool

## 目标

线程池是卷五最适合做成 CS144 风格大作业的项目。它把前面所有 Lab 的知识串了起来——`JoiningThread` 管理线程生命周期、`BoundedBlockingQueue` 作为任务队列、atomic 做统计、关闭语义做优雅退出。但线程池不只是这些组件的简单拼装——它引入了几个新的工程难题：`std::future` 和 `packaged_task` 的类型擦除、异常跨线程传播、move-only 任务支持、以及关闭时任务队列的排空策略。

完成这个 Lab 之后，你应该有一个接口清楚、可测试、可关闭、能传播异常的线程池组件——可以直接在 Capstone 项目中使用。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch05-01**：std::async 与 future — `std::future`、`std::promise`、`std::async`
- **ch05-02**：promise 与 packaged_task — `std::packaged_task`、类型擦除
- **ch05-03**：jthread 与 stop_token — C++20 协作式取消
- **ch05-04**：线程池设计 — 线程池的基本架构和设计考量
- **Lab 0**：`JoiningThread` 的实现
- **Lab 1**：`BoundedBlockingQueue` 的实现（本 Lab 直接复用）

## 环境准备

与 Lab 1 相同（C++20，Catch2 v3，TSan）。

## 最终接口

### `ThreadPool` — 固定大小线程池（不可复制，析构自动 shutdown）

类型别名：`using Task = std::function<void()>;`（类型擦除的任务包装）

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `BoundedBlockingQueue<Task>` | `task_queue_` | 任务队列（复用 Lab 1） |
| `std::vector<JoiningThread>` | `workers_` | worker 线程集合（复用 Lab 0） |
| `std::atomic<bool>` | `stopped_` | 关闭标志 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `ThreadPool(size_t thread_count)` | 创建指定数量的 worker 线程 | MS1 |
| 析构 | `~ThreadPool() noexcept` | 调用 shutdown()，等待所有任务完成 | MS4 |
| submit | `auto submit(F&&, Args&&...) -> future<invoke_result_t<F, Args...>>` | 提交任务，返回 future；已关闭时抛异常 | MS2 |
| shutdown | `void shutdown()` | 排空队列，拒绝新提交，join 所有 worker | MS4 |
| pending_tasks | `size_t pending_tasks() const` | 当前队列中的任务数量 | MS1 |

## Milestone 1: 基础线程池

### 目标

实现最基础的线程池：固定数量 worker，共享任务队列，析构时停止并 join。`submit` 接受 `std::function<void()>` 类型的任务，不返回 future。

### 为什么

先跑通"多 worker 从共享队列取任务"的基本架构，不涉及模板、future 和异常传播。这个骨架搭好后，后续 milestone 只是往上面叠加功能。

### 实现指引

核心结构是 `BoundedBlockingQueue<Task>` + `std::vector<JoiningThread>`。每个 worker 线程的循环逻辑很简单：从队列里 `pop` 一个任务，执行它，继续取下一个。队列关闭且为空时，worker 退出循环。

```cpp

void worker_loop() {
    while (auto task = task_queue_.pop()) {
        (*task)();  // 执行任务
    }
}

```

构造函数里创建 N 个 worker：

```cpp

ThreadPool(size_t count)
    : task_queue_(256)  // 队列容量
{
    for (size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

```

踩坑预警：`worker_loop` 作为成员函数传给 `JoiningThread` 时，第一个参数是 `this` 指针。确保线程池对象的生命周期比所有 worker 长——析构函数必须先关闭队列、等待所有 worker 退出。另外，`BoundedBlockingQueue` 的容量选多大合适？256 是个不错的默认值——太大浪费内存，太小容易阻塞提交线程。如果你不想设上限，可以用一个很大的值或者自己实现无界队列，但本 Lab 建议用有界队列。

### 验证

```cpp
TEST_CASE("Milestone 1: basic thread pool executes tasks",
          "[lab3][milestone1]")
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 100; ++i) {
        pool.submit([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    // 等待所有任务完成
    // 注意：基础版本的 submit 不返回 future
    // 需要通过其他方式等待——这里用一个简单的 sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    REQUIRE(counter.load() == 100);
}

TEST_CASE("Milestone 1: destructor joins all workers",
          "[lab3][milestone1]")
{
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 50; ++i) {
            pool.submit([&counter]() {
                counter.fetch_add(1);
            });
        }
    }  // pool 析构 → shutdown → join

    REQUIRE(counter.load() == 50);
}
```

## Milestone 2: submit 返回 future

### 目标

实现模板版本的 `submit`，接受任意可调用对象和参数，返回 `std::future<R>`。调用者通过 `future::get()` 获取任务返回值。

### 为什么

基础版本的 `submit` 只接受 `std::function<void()>`，调用者无法获取任务的返回值。在实际工程中，线程池的调用者几乎总是需要知道任务的结果——不管是成功返回的数据，还是抛出的异常。`std::future` + `std::packaged_task` 是 C++ 标准提供的"跨线程传递结果"机制。

### 实现指引

核心思路是把用户提交的可调用对象包装成一个 `std::packaged_task<R()>`，然后把 `packaged_task` 的 `future` 返回给调用者，把 `packaged_task` 本身（包装成 `std::function<void()>`）塞进任务队列。

伪代码：

```cpp
template <class F, class... Args>
auto submit(F&& f, Args&&... args)
    -> future<invoke_result_t<F, Args...>>
{
    using R = invoke_result_t<F, Args...>;

    // 把 f(args...) 绑定成一个无参可调用对象
    auto task = make_shared<packaged_task<R()>>(
        bind(forward<F>(f), forward<Args>(args)...)
    );

    future<R> result = task->get_future();

    // 包装成 function<void()> 放进队列
    task_queue_.push([task]() { (*task)(); });

    return result;
}
```

这里用 `std::shared_ptr<packaged_task>` 是因为 `packaged_task` 是 move-only 的（不可复制），而 `std::function` 要求可复制构造。把 `packaged_task` 放在 `shared_ptr` 里，lambda 捕获 `shared_ptr`（可复制），就解决了这个问题。

踩坑预警：`std::bind` 在处理引用参数时有坑。如果你的可调用对象接受引用参数，`bind` 可能 decay 掉引用语义。更安全的方式是用 lambda 来绑定：

```cpp
auto wrapper = [f = forward<F>(f),
                ... args = forward<Args>(args)]() mutable {
    return f(args...);
};
```

C++20 的 lambda 初始化捕获支持参数包展开（`... args = forward<Args>(args)`），如果你的编译器不支持，可以用 `std::tuple` 来存储参数。

### 验证

```cpp
TEST_CASE("Milestone 2: submit returns future with value",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);

    auto f1 = pool.submit([]() { return 42; });
    auto f2 = pool.submit([](int a, int b) { return a + b; },
                          10, 20);

    REQUIRE(f1.get() == 42);
    REQUIRE(f2.get() == 30);
}

TEST_CASE("Milestone 2: submit handles void return",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);
    std::atomic<bool> done{false};

    auto f = pool.submit([&done]() {
        done.store(true);
    });

    f.get();  // 不应抛异常
    REQUIRE(done.load());
}

TEST_CASE("Milestone 2: multiple futures collected",
          "[lab3][milestone2]")
{
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(
            pool.submit([i]() { return i * i; }));
    }

    int sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    // sum = 0^2 + 1^2 + ... + 19^2 = 2470 - 19 = 2275? No.
    // 0+1+4+9+...+361 = 2470
    REQUIRE(sum == 2470);
}
```

## Milestone 3: 异常传播与 move-only 参数

### 目标

确保 `future::get()` 能重新抛出任务中的异常。支持 move-only 类型的参数（如 `std::unique_ptr`）。

### 为什么

异常传播是线程池设计中最容易被忽略的部分。如果任务抛了异常，而 `future::get()` 不会重新抛出它，那异常就被静默吞掉了——调用者完全不知道任务失败了。好消息是 `std::packaged_task` 已经处理了异常传播——任务抛异常时，`packaged_task` 会捕获它并存储在 `future` 中，`get()` 时重新抛出。所以这个 milestone 的主要工作不是"实现"异常传播，而是"验证"它正确工作，并确保你的 `submit` 实现没有意外吞掉异常。

move-only 参数的支持更直接——`std::packaged_task` 本身就是 move-only 的，lambda 也可以捕获 move-only 类型。你需要确保从 `submit` 到 worker 执行的整个传递链中，没有任何地方强制复制。

### 实现指引

如果你的 Milestone 2 实现使用了 `shared_ptr<packaged_task>`，异常传播已经自动工作了。你只需要验证它。

对于 move-only 参数，用 lambda 初始化捕获来传递：

```cpp

auto ptr = make_unique<Data>(42);
auto f = pool.submit(`[p = move(ptr)]()` {
    return p->compute();
});

```

踩坑预警：不要在 `submit` 的参数中使用 `std::ref` 来传递 move-only 类型——`std::ref` 不转移所有权，它只是创建一个引用包装器，引用的对象可能在 worker 执行时已经被销毁了。

### 验证

```cpp
TEST_CASE("Milestone 3: exception propagates through future",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);

    auto f = pool.submit([]() {
        throw std::runtime_error("task failed");
        return 42;
    });

    REQUIRE_THROWS_AS(f.get(), std::runtime_error);
}

TEST_CASE("Milestone 3: move-only parameter support",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);

    auto ptr = std::make_unique<int>(42);
    auto f = pool.submit([p = std::move(ptr)]() {
        return *p;
    });

    REQUIRE(f.get() == 42);
}

TEST_CASE("Milestone 3: exception in one task doesn't affect others",
          "[lab3][milestone3]")
{
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    futures.push_back(pool.submit([]() { return 1; }));
    futures.push_back(pool.submit([]() {
        throw std::runtime_error("fail");
    }));
    futures.push_back(pool.submit([]() { return 3; }));

    REQUIRE(futures[0].get() == 1);
    REQUIRE_THROWS_AS(futures[1].get(), std::runtime_error);
    REQUIRE(futures[2].get() == 3);
}
```

## Milestone 4: 关闭语义

### 目标

实现 `shutdown()` 方法：排空队列中已有的任务，但拒绝新提交。析构函数调用 `shutdown()` 并等待所有 worker 退出。

### 为什么

线程池的关闭是最考验设计的环节。一个生产级线程池的关闭必须同时满足三个条件：已有任务被执行完（不丢失）、新提交被拒绝（有明确的错误信号）、所有 worker 线程被 join（不泄漏）。任何一个条件不满足都是工程缺陷——丢失任务导致数据不完整，不拒绝新提交导致无限等待，不 join 导致 `std::terminate()`。

### 实现指引

`shutdown()` 的实现思路是：设置 `stopped_` 标志为 true，然后 `close()` 任务队列。worker 的循环不变——`pop` 返回 `nullopt` 时退出。`submit` 在 `stopped_` 为 true 时抛出异常（或返回一个 broken promise 的 future）。

```cpp
void shutdown() {
    bool expected = false;
    if (!stopped_.compare_exchange_strong(expected, true)) {
        return;  // 已经关闭了
    }
    task_queue_.close();
    // workers_ 的析构会自动 join
}
```

析构函数调用 `shutdown()`：

```cpp
~ThreadPool() noexcept {
    shutdown();
    // workers_ 的 JoiningThread 析构时自动 join
}
```

踩坑预警：`shutdown()` 必须是幂等的——调用多次不应该出问题。用 `compare_exchange_strong` 来保证只有一个线程执行关闭逻辑。另外，如果队列中有积压的任务，`close()` 之后 worker 仍然会执行它们（因为 `BoundedBlockingQueue::close` 允许 drain 剩余数据）。如果你想要"立即停止"的行为（丢弃未执行的任务），需要修改关闭逻辑。

### 验证

```cpp
TEST_CASE("Milestone 4: shutdown drains pending tasks",
          "[lab3][milestone4]")
{
    auto pool = std::make_unique<ThreadPool>(2);
    std::atomic<int> counter{0};

    std::vector<std::future<void>> futures;
    for (int i = 0; i < 50; ++i) {
        futures.push_back(
            pool->submit([&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10));
            }));
    }

    pool->shutdown();

    // 所有 future 应该都能 get（任务都被执行了）
    for (auto& f : futures) {
        REQUIRE_NOTHROW(f.get());
    }
    REQUIRE(counter.load() == 50);
}

TEST_CASE("Milestone 4: submit after shutdown throws",
          "[lab3][milestone4]")
{
    ThreadPool pool(2);
    pool.shutdown();

    REQUIRE_THROWS_AS(
        pool.submit([]() { return 42; }),
        std::runtime_error);
}

TEST_CASE("Milestone 4: destructor calls shutdown",
          "[lab3][milestone4]")
{
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4);
        for (int i = 0; i < 20; ++i) {
            pool.submit([&counter]() {
                counter.fetch_add(1);
            });
        }
    }  // 析构 → shutdown → drain → join

    REQUIRE(counter.load() == 20);
}
```

## Milestone 5: 可选容量与背压策略

### 目标

给线程池的任务队列加入容量限制，实现三种背压策略：block（阻塞等待空间）、reject（立即拒绝）、caller-runs（调用者线程执行）。

### 为什么

无界队列在生产环境中是危险的——如果消费者处理速度跟不上生产者，队列会无限增长，最终耗尽内存。有界队列加上背压策略是生产级线程池的标准设计。三种策略各有适用场景：block 适合不允许丢失任务的场景，reject 适合可以容忍任务丢失的高吞吐场景，caller-runs 适合想要自动降速的场景。

### 实现指引

在 `submit` 中加入容量检查逻辑。`BoundedBlockingQueue` 已经有容量限制和 `try_push_for`，所以实现相对直接。

- **block**：直接用 `push()`（阻塞等待空间）
- **reject**：用 `try_push_for(timeout=0)`，失败时抛异常
- **caller-runs**：`try_push_for` 失败时直接在当前线程执行任务

背压策略可以作为构造函数参数传入，或者通过模板策略参数实现。为了简单起见，本 Lab 建议用枚举：

```cpp

enum class BackpressurePolicy {
    kBlock,
    kReject,
    kCallerRuns
};

```

### 验证

```cpp
TEST_CASE("Milestone 5: block policy waits for space",
          "[lab3][milestone5]")
{
    ThreadPool pool(2, BackpressurePolicy::kBlock,
                    4);  // 队列容量 4
    std::atomic<int> counter{0};

    // 提交大量任务，应该都能成功（会阻塞等待）
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.submit([&counter]() {
            counter.fetch_add(1);
            std::this_thread::sleep_for(
                std::chrono::milliseconds(50));
        }));
    }

    for (auto& f : futures) f.get();
    REQUIRE(counter.load() == 20);
}

TEST_CASE("Milestone 5: reject policy throws on full queue",
          "[lab3][milestone5]")
{
    ThreadPool pool(2, BackpressurePolicy::kReject, 2);
    std::atomic<int> counter{0};

    // 填满队列
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 10; ++i) {
        try {
            futures.push_back(pool.submit([&counter]() {
                counter.fetch_add(1);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }));
        }
        catch (const std::runtime_error&) {
            // 队列满了，预期会被拒绝一部分
        }
    }

    for (auto& f : futures) f.get();
    REQUIRE(counter.load() <= 10);
}
```

## 自查清单

- [ ] 基础线程池能并发执行任务，不丢失
- [ ] `submit` 返回的 `future` 能拿到正确的返回值
- [ ] 任务抛异常时，`future::get()` 能重新抛出
- [ ] move-only 参数（`unique_ptr`）能正确传递
- [ ] `shutdown()` 排空队列，拒绝新提交
- [ ] 析构函数调用 `shutdown()` 并 join 所有 worker
- [ ] `shutdown()` 是幂等的，多次调用不出问题
- [ ] 背压策略的行为符合预期
- [ ] 全部测试在 TSan 下无 data race 报告
- [ ] 能解释 `shared_ptr<packaged_task>` 解决了什么问题（为什么不能直接用 `packaged_task`）
- [ ] 能解释关闭时"排空队列"vs"丢弃任务"的权衡
- [ ] 能口头说明这个线程池将在 Capstone 项目中被直接使用
