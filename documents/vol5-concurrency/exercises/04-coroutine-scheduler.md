---
title: "Lab 4: Coroutine Scheduler and Event Loop"
description: "实现极简协程调度器，掌握 C++20 协程从语法到运行时的完整链路：Task、Scheduler、timer、epoll 事件循环"
chapter: 10
order: 5
difficulty: advanced
tags:
  - host
  - cpp-modern
  - coroutine
  - advanced
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "卷五 ch06: 异步 I/O 与协程"
  - "Lab 3: Production-style Thread Pool"
---

# Lab 4: Coroutine Scheduler and Event Loop

## 目标

Lab 3 的线程池是"任务级"的并发——每个任务是一个完整的函数调用，从开始到结束独占一个线程。这个 Lab 我们进入更细粒度的并发：协程。一个协程可以在执行到某个点时挂起（suspend），把执行权交还给调度器，等条件满足后再恢复（resume）。这意味着一个线程可以轮流执行多个协程——不再是一个任务占一个线程，而是一个线程跑多个"半完成"的任务。

我们要实现一个极简协程调度器：先支持手动调度和 `yield`，再加 timer，最后在 Linux/WSL2 上接入 epoll，实现一个 coroutine echo server。这个 Lab 是卷五的高阶核心项目——它把 C++20 协程从"语法理解"推进到"运行时理解"。

## 前置知识

在开始之前，确保你已经读完以下章节：

- **ch06-01**：异步编程演进 — 从回调到协程的动机
- **ch06-02**：C++20 协程基础 — `co_await`、`co_return`、`promise_type`
- **ch06-03**：promise_type 与 awaitable — 自定义 awaitable 的完整机制
- **ch06-04**：异步 I/O 与事件循环 — epoll/kqueue 事件驱动模型
- **ch06-05**：协程实战：echo server — 完整的协程网络应用
- **Lab 3**：线程池的关闭语义设计思路（本 Lab 的关闭设计参考）

## 环境准备

这个 Lab 需要 C++20 和 Linux/WSL2 环境。

- **编译器**：GCC 12+ 或 Clang 15+（完整协程支持）
- **平台**：Linux 或 WSL2（epoll milestone 需要）
- **CMake**：3.14+

```cmake
cmake_minimum_required(VERSION 3.14)
project(lab4_coroutine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(lab4_tests tests/main.cpp)
target_link_libraries(lab4_tests PRIVATE Catch2::Catch2WithMain)
```

## 最终接口

### `Task<T>` — 协程任务包装器（Milestone 1，move-only）

内部定义 `promise_type`，需实现以下回调：

| promise_type 方法 | 返回类型 | 说明 | Milestone |
|-------------------|----------|------|-----------|
| get_return_object | `Task<T>` | 创建 Task 对象 | MS1 |
| initial_suspend | `std::suspend_always` | lazy 模式，创建后不自动执行 | MS1 |
| final_suspend | `std::suspend_always` | 结束后不自动销毁 frame | MS1 |
| return_value | `void` | 存储 `co_return` 的值 | MS1 |
| unhandled_exception | `void` | 存储异常（`std::exception_ptr`） | MS1 |

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `coroutine_handle<promise_type>` | `handle_` | 协程句柄 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| 构造 | `Task(handle_type)` | 接受协程句柄 | MS1 |
| 析构 | `~Task()` | 销毁 coroutine frame | MS1 |
| get | `T get()` | 获取结果或重新抛出异常 | MS1 |

### `Scheduler` — 协程调度器（Milestone 2）

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `std::queue<coroutine_handle<>>` | `ready_queue_` | 就绪协程队列 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| schedule | `void schedule(coroutine_handle<>)` | 将协程加入就绪队列 | MS2 |
| yield | `auto yield()` | 返回 awaitable，挂起并放回队列 | MS2 |
| run | `void run()` | 循环执行就绪协程直到队列空 | MS2 |
| has_work | `bool has_work() const` | 是否有待执行协程 | MS2 |

### `SleepAwaiter` — sleep_for 的 awaitable（Milestone 3）

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| await_ready | `bool await_ready() noexcept` | 返回 false（总是挂起） | MS3 |
| await_suspend | `void await_suspend(coroutine_handle<>)` | 注册到 timer heap | MS3 |
| await_resume | `void await_resume() noexcept` | 恢复时无操作 | MS3 |

### `EventLoop` — epoll 事件循环（Milestone 4，Linux/WSL2）

成员变量：

| 类型 | 成员 | 语义 |
|------|------|------|
| `int` | `epoll_fd_` | epoll 实例文件描述符 |
| `bool` | `running_` | 运行标志 |

接口：

| 方法 | 签名 | 说明 | Milestone |
|------|------|------|-----------|
| read | `auto read(int fd, void* buf, size_t size)` | 注册读事件，返回 awaitable | MS4 |
| write | `auto write(int fd, const void* buf, size_t size)` | 注册写事件，返回 awaitable | MS4 |
| accept | `auto accept(int listen_fd)` | 注册 accept 事件，返回 awaitable | MS4 |
| run | `void run()` | 事件循环主循环 | MS4 |
| stop | `void stop()` | 停止事件循环 | MS4 |

## Milestone 1: Task<void> 与基础协程

### 目标

实现 `Task<T>` 的 `promise_type`，包括 `initial_suspend`、`final_suspend`、`return_value` 和 `unhandled_exception`。先实现 `Task<void>` 的特化，再扩展到 `Task<T>`。

### 为什么

`Task` 是协程调度器的基础货币——所有协程函数返回 `Task`，调度器通过 `Task` 内部的 `coroutine_handle` 来管理协程的挂起和恢复。`promise_type` 定义了协程生命周期中各个关键点的行为：创建时做什么（`initial_suspend`）、返回时做什么（`return_value`）、结束时不做什么（`final_suspend`）、异常时做什么（`unhandled_exception`）。理解了这四个回调，你就理解了 C++20 协程的运行时模型。

### 实现指引

`promise_type` 的核心职责是在协程的各个生命周期节点上插入自定义逻辑。

`initial_suspend` 返回 `std::suspend_always`——这意味着协程在函数体开始执行之前就挂起，不会自动运行。这是"lazy"（惰性）任务的标志——协程被创建后什么都不做，直到有人显式 `resume` 它。相对的是 `std::suspend_never`（"eager"任务，创建后立即执行）。我们选择 lazy 是因为调度器需要控制"什么时候开始执行"。

`final_suspend` 返回 `std::suspend_always`——协程执行到 `co_return` 后挂起，不自动销毁 coroutine frame。这是为了防止在 `get()` 读取结果之前 frame 被销毁。`Task` 的析构函数负责销毁 frame。

`unhandled_exception` 存储异常（用 `std::exception_ptr`），`get()` 时重新抛出。

踩坑预警：`final_suspend` 如果返回 `suspend_never`，协程结束时会自动销毁 frame。这看起来很方便，但如果你在 `get()` 之前 frame 就被销毁了，访问 `promise_type` 的成员就是 UB。大多数教学实现选择 `suspend_always` + 析构函数中 `destroy()`，虽然多一次手动管理，但更安全。

### 验证

```cpp
Task<int> simple_task()
{
    co_return 42;
}

Task<void> void_task()
{
    co_return;
}

TEST_CASE("Milestone 1: Task returns value",
          "[lab4][milestone1]")
{
    auto task = simple_task();
    // Task 是 lazy 的，不会自动执行
    // 需要手动 resume
    task.handle_.resume();
    REQUIRE(task.get() == 42);
}

TEST_CASE("Milestone 1: Task<void> compiles",
          "[lab4][milestone1]")
{
    auto task = void_task();
    task.handle_.resume();
    REQUIRE_NOTHROW(task.get());
}

Task<int> throwing_task()
{
    throw std::runtime_error("coroutine error");
    co_return 0;
}

TEST_CASE("Milestone 1: exception propagates through get",
          "[lab4][milestone1]")
{
    auto task = throwing_task();
    task.handle_.resume();
    REQUIRE_THROWS_AS(task.get(), std::runtime_error);
}
```cpp

## Milestone 2: Scheduler 与 yield

### 目标

实现 `Scheduler`，维护一个就绪队列，支持 `schedule`（加入队列）和 `yield`（挂起当前协程，放回队列）。`run()` 循环从队列中取出协程并 resume，直到队列为空。

### 为什么

有了 `Task`，我们有了可以挂起和恢复的执行单元。但没有调度器，协程的执行顺序完全由手动控制——谁 `resume` 谁，什么时候 `resume`。`Scheduler` 把这个编排过程自动化了：所有协程进入就绪队列，调度器按 FIFO 顺序执行。`yield` 让出执行权给其他协程——这就是"协作式多任务"的核心。

### 实现指引

`Scheduler` 的数据结构很简单——一个 `std::queue<std::coroutine_handle<>>`。`schedule` 把 handle 放进队列。`run` 循环取 handle 并 `resume`。

`yield` 是一个 awaitable，它的 `await_suspend` 把当前协程的 handle 放回就绪队列，返回 `true`（表示挂起）。这样调度器在下一轮循环中会再次取出这个协程并 resume。

```

auto yield() {
    struct YieldAwaiter {
        Scheduler& sched;

        bool await_ready() { return false; }
        // 总是挂起

        void await_suspend(coroutine_handle<> handle) {
            sched.schedule(handle);
            // 放回队列
        }

        void await_resume() {}
    };
    return YieldAwaiter{*this};
}

```cpp

踩坑预警：`run()` 不能是简单的 `while (!queue.empty())`，因为协程可能在 `await_suspend` 中添加新的协程到队列。你需要确保 `run()` 一直循环直到队列为空且没有正在执行的协程。一个简单的做法是：`while (!queue_.empty()) { auto h = queue_.front(); queue_.pop(); h.resume(); }`。

### 验证

```cpp
Scheduler sched;

Task<void> ping(int id, int rounds)
{
    for (int i = 0; i < rounds; ++i) {
        // yield 让出执行权
        co_await sched.yield();
    }
    co_return;
}

TEST_CASE("Milestone 2: scheduler runs multiple coroutines",
          "[lab4][milestone2]")
{
    Scheduler sched;
    std::vector<std::string> log;

    auto make_task = [&](int id) -> Task<void> {
        for (int i = 0; i < 3; ++i) {
            log.push_back(
                std::to_string(id) + "-" + std::to_string(i));
            co_await sched.yield();
        }
    };

    sched.schedule(make_task(1));
    sched.schedule(make_task(2));
    sched.run();

    // 验证交替执行
    REQUIRE(log.size() == 6);
    // 日志应该是交错的: 1-0, 2-0, 1-1, 2-1, 1-2, 2-2
}

TEST_CASE("Milestone 2: scheduler drains all work",
          "[lab4][milestone2]")
{
    Scheduler sched;
    std::atomic<int> counter{0};

    auto make_task = [&]() -> Task<void> {
        counter.fetch_add(1);
        co_await sched.yield();
        counter.fetch_add(1);
    };

    sched.schedule(make_task());
    sched.schedule(make_task());
    sched.run();

    REQUIRE(counter.load() == 4);
    REQUIRE_FALSE(sched.has_work());
}
```

## Milestone 3: sleep_for 与 timer heap

### 目标

实现 `sleep_for(duration)` awaitable。调度器维护一个 timer heap（最小堆），到期后把协程放回就绪队列。

### 为什么

`yield` 让协程立即让出执行权，但很多时候我们需要"让出并在一段时间后恢复"——比如轮询间隔、超时等待、动画帧率控制。`sleep_for` 是最基础的定时 awaitable，它的实现引入了调度器的第一个"非即时"事件源——协程不是马上回到就绪队列，而是先在 timer heap 里等一段时间。

### 实现指引

`SleepAwaiter` 的 `await_suspend` 做两件事：计算唤醒时间点（`steady_clock::now() + duration`），把 `(时间点, handle)` 放入 timer heap。`await_ready` 返回 false（总是挂起）。

调度器的 `run()` 循环需要修改——每次取任务时，先检查 timer heap 的最小元素是否到期。如果到期了，把它从 heap 取出并放入就绪队列。如果没到期且就绪队列为空，`sleep` 到最近一个 timer 到期。

伪代码：

```cpp
void run() {
    while (!ready_queue_.empty() || !timers_.empty()) {
        // 1. 处理到期的 timer
        while (!timers_.empty() &&
               timers_.top().deadline <= now()) {
            auto& t = timers_.top();
            ready_queue_.push(t.handle);
            timers_.pop();
        }

        // 2. 执行就绪协程
        if (!ready_queue_.empty()) {
            auto h = ready_queue_.front();
            ready_queue_.pop();
            h.resume();
        }
        else if (!timers_.empty()) {
            // 等到最近一个 timer 到期
            sleep_until(timers_.top().deadline);
        }
    }
}
```

踩坑预警：不要为每个 `sleep_for` 创建一个独立线程来计时——那样就退回到了"一个任务一个线程"的模式。timer heap 的设计目标是所有定时器共享一个线程，用最小堆来高效找到最近的到期时间。另外，`std::priority_queue` 默认是最大堆，你需要自定义比较器让最小的元素在堆顶。

### 验证

```cpp
TEST_CASE("Milestone 3: sleep_for delays execution",
          "[lab4][milestone3]")
{
    Scheduler sched;
    std::vector<std::string> log;

    auto timed_task = [&](int id) -> Task<void> {
        log.push_back(std::to_string(id) + "-start");
        co_await sleep_for(std::chrono::milliseconds(50));
        log.push_back(std::to_string(id) + "-end");
    };

    auto start = std::chrono::steady_clock::now();
    sched.schedule(timed_task(1));
    sched.schedule(timed_task(2));
    sched.run();
    auto elapsed = std::chrono::steady_clock::now() - start;

    // 两个 task 各 sleep 50ms，并行执行
    // 总耗时应该接近 50ms 而不是 100ms
    REQUIRE(elapsed < std::chrono::milliseconds(100));

    REQUIRE(log.size() == 4);
}

TEST_CASE("Milestone 3: timer respects order",
          "[lab4][milestone3]")
{
    Scheduler sched;
    std::vector<int> order;

    auto timed = [&](int id, int ms) -> Task<void> {
        co_await sleep_for(std::chrono::milliseconds(ms));
        order.push_back(id);
    };

    sched.schedule(timed(1, 50));
    sched.schedule(timed(2, 20));
    sched.schedule(timed(3, 30));
    sched.run();

    REQUIRE(order == std::vector<int>{2, 3, 1});
}
```cpp

## Milestone 4: epoll 事件循环

### 目标

在 Linux/WSL2 上实现基于 epoll 的事件循环，支持 non-blocking fd 的 read/write/accept awaitable。

### 为什么

timer 让协程能在指定时间后恢复，但真正的异步编程需要等待的是"I/O 事件就绪"——socket 可读、socket 可写、新连接到来。epoll 是 Linux 的高效 I/O 多路复用机制，它让一个线程同时监控多个 fd 的状态变化，fd 就绪时唤醒等待的协程。把 epoll 集成到调度器中，我们就得到了一个完整的"协程 + I/O"运行时。

### 实现指引

核心思路：每个 I/O awaitable 在 `await_suspend` 中把 `(fd, 事件类型, handle)` 注册到 epoll，当 epoll 报告 fd 就绪时，把对应的 handle 放回就绪队列。

read awaitable 的伪代码：

```

struct ReadAwaiter {
    int fd;
    void* buffer;
    size_t size;
    EventLoop& loop;

    bool await_ready() {
        // 尝试非阻塞读取
        // 如果 EAGAIN → 返回 false，需要等待
    }

    void await_suspend(coroutine_handle<> handle) {
        // 注册 fd 到 epoll，关注 EPOLLIN
        // 存储 handle 以便后续恢复
        epoll_event ev;
        ev.events = EPOLLIN | EPOLLET;  // 边缘触发
        ev.data.ptr = handle.address();
        epoll_ctl(loop.epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    }

    size_t await_resume() {
        // 返回实际读取的字节数
        return bytes_read;
    }
};

```cpp

调度器的 `run()` 循环需要再次扩展——在处理 timer 和就绪队列的同时，也要调用 `epoll_wait` 来检查 I/O 事件：

```

void run() {
    while (running_) {
        // 1. 处理到期的 timer
        process_timers();

        // 2. 处理就绪协程
        process_ready_queue();

        // 3. epoll_wait 等待 I/O 事件
        int timeout = calculate_next_timeout();
        int n = epoll_wait(epoll_fd_, events, kMaxEvents,
                           timeout);
        for (int i = 0; i < n; ++i) {
            auto handle = coroutine_handle<>::from_address(
                events[i].data.ptr);
            ready_queue_.push(handle);
        }
    }
}

```cpp

踩坑预警：边缘触发（EPOLLET）模式下，`epoll_wait` 只在 fd 状态变化时报告一次。如果你没读完所有数据，下次 `epoll_wait` 不会再次报告。所以 `await_resume` 中应该循环读取直到 `EAGAIN`。另外，`EINTR`（被信号中断）不是错误，应该重试 `epoll_wait`。

### 验证

```cpp
TEST_CASE("Milestone 4: epoll echo server",
          "[lab4][milestone4]")
{
    // 启动 echo server
    int listen_fd = create_listen_socket(8080);
    EventLoop loop;

    // 每个连接一个协程
    auto handle_connection = [&](int fd) -> Task<void> {
        char buffer[1024];
        while (true) {
            auto n = co_await loop.read(fd, buffer, sizeof(buffer));
            if (n <= 0) break;  // 连接关闭
            co_await loop.write(fd, buffer, n);
        }
        close(fd);
    };

    auto accept_loop = [&]() -> Task<void> {
        while (true) {
            int client_fd = co_await loop.accept(listen_fd);
            if (client_fd < 0) break;
            loop.schedule(handle_connection(client_fd));
        }
    };

    loop.schedule(accept_loop());

    // 在另一个线程中运行客户端测试
    JoiningThread client([&]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100));
        int sock = connect_to("127.0.0.1", 8080);
        send(sock, "hello", 5, 0);
        char buf[16];
        recv(sock, buf, 5, 0);
        buf[5] = '\0';
        REQUIRE(std::string(buf) == "hello");
        close(sock);
        loop.stop();
    });

    loop.run();
    close(listen_fd);
}
```

## Milestone 5: coroutine echo server

### 目标

把 Milestone 1–4 的组件组合起来，实现一个完整的 coroutine echo server。支持多并发连接、客户端断开检测和优雅停止。

### 为什么

echo server 是网络编程的"Hello World"。用协程实现它的代码看起来跟同步版本几乎一样——顺序的 read、write 循环——但底层是异步非阻塞的，一个线程处理多个连接。这就是协程的威力：写同步风格的代码，获得异步的性能。

### 实现指引

echo server 的完整逻辑已经在 Milestone 4 的测试中体现了。这个 milestone 的重点是加入错误处理和优雅停止：

- 处理 `EAGAIN`、`EINTR`、连接关闭（read 返回 0）和部分写
- `stop()` 关闭 listen fd，等待所有已建立的连接处理完毕
- 协程异常不应影响其他连接——每个连接的协程应该有自己的 try-catch

### 验证

这个 milestone 的验证是端到端测试——启动 server，用多个客户端并发连接，发送数据，验证 echo 回来的数据正确，然后优雅停止。

## 自查清单

- [ ] `Task<T>` 的 `promise_type` 四个关键回调实现正确
- [ ] 多个协程能在 `Scheduler` 中交替执行
- [ ] `yield` 让出执行权后，其他协程能继续运行
- [ ] `sleep_for` 的定时精度在可接受范围内（±10ms）
- [ ] timer heap 正确处理不同到期时间的协程
- [ ] epoll 事件循环正确处理 read/write/accept
- [ ] echo server 能处理多个并发连接
- [ ] 协程结束后 coroutine frame 被销毁，不泄漏
- [ ] 异常处理策略明确，不会静默丢失异常
- [ ] 能解释 `initial_suspend` 返回 `suspend_always` 的设计考量
- [ ] 能解释边缘触发 vs 水平触发的区别及对代码的影响
- [ ] 能说明 I/O awaiter 正确处理了 `EAGAIN` 和 `EINTR`
