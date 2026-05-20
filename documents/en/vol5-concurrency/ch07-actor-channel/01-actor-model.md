---
title: Actor Model and Message Passing
description: Understand the core idea of the Actor model—replacing shared memory with
  message passing, and implement a simple C++ Actor framework.
chapter: 7
order: 1
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 进阶
difficulty: intermediate
platform: host
reading_time_minutes: 25
cpp_standard:
- 17
- 20
prerequisites:
- 线程安全队列
- 线程池设计
related:
- Channel 与 CSP 模型
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/01-actor-model.md
  source_hash: 87462fddcd7f11ecb83365fbc1d45241ddcd2e0bce39abaee1fcec8855c28c73
  translated_at: '2026-05-20T04:48:37.851668+00:00'
  engine: anthropic
  token_count: 6305
---
# The Actor Model and Message Passing

Up to this point, the concurrency model we have used throughout this entire volume is fundamentally the same thing: shared memory plus locks. Whether it is a mutex, a `condition_variable`, or an `atomic`, the underlying logic is always "multiple threads see the same memory, and we use synchronization primitives to prevent conflicting access." This model works, but frankly, as system scale grows, it becomes increasingly difficult to manage. How do we choose lock granularity? How do we determine lock acquisition order? How do we prevent deadlocks? Each of these requires human judgment, and human judgment is the least reliable thing in the world.

But there is a school of thought that fundamentally rejects this path. The Actor model says: stop sharing memory. Every message is a copy, every computational entity is independent, and they communicate exclusively through asynchronous messages. No shared state means no race conditions, which means no locks. This idea might sound utopian, but it has been validated at industrial scale in Erlang/Akka. The telecom systems Ericsson built with Erlang/OTP reportedly achieve nine nines (99.9999999%) availability. While the measurement methodology behind that number is debatable, Erlang's status in the high-availability domain is indisputable.

In this chapter, we dive deep into the Actor model: its theoretical foundations, core concepts, and C++ implementation. We will not build a production-grade Actor framework (that is the job of CAF or SObjectizer), but we will implement a sufficiently complete minimal framework that ties together all the core ideas of the Actor model.

## Environment Setup

All code in this chapter is based on C++17. The author's compilation environment is GCC 12+ / Clang 15+ / MSVC 2022+, with compiler flags `-std=c++17 -pthread -O2`. It runs on Linux / macOS / Windows (as long as your standard library implements `<thread>`, `<mutex>`, and `<condition_variable>`). The code does not depend on any third-party libraries and uses only standard library components, so you can copy it directly and it will compile.

## Origins and Core Ideas of the Actor Model

The Actor model was first proposed by Carl Hewitt, Peter Bishop, and Richard Steiger in their 1973 paper, *"A Universal Modular ACTOR Formalism for Artificial Intelligence"*. It was originally born out of MIT's artificial intelligence research, motivated by the vision of "parallel computing architectures composed of tens, hundreds, or even thousands of independent microprocessors." This prediction looks remarkably accurate today—multicore processors and distributed systems have become the mainstream.

> If you are curious about the original paper, you can find it in the IJCAI 1973 conference proceedings. Although the original paper's context is AI research, the Actor model itself is a general-purpose concurrent computation model.

The core thesis of the Actor model is **"everything is an Actor,"** which is analogous to "everything is an object" in object-oriented programming. Each Actor is an independent computational entity with its own private state that cannot be accessed directly from the outside. So how do Actors interact? There is only one way: send messages.

According to Hewitt's definition, when an Actor receives a message, it can simultaneously do three things: send a finite number of messages to other Actors (only to Actors whose addresses it knows), create a finite number of new Actors (dynamically created, with no upper limit on quantity), and decide what behavior to use for processing the next message—meaning an Actor's behavior can change over time. There is no ordering requirement among these three actions; they can happen concurrently. This is the fundamental conceptual difference between the Actor model and the traditional "sequential execution + shared memory" model: Actors are inherently concurrent entities, and sequential execution is merely a special degenerate case.

### Fundamental Differences from the Shared Memory Model

In the shared memory model, multiple threads exchange information by reading and writing the same memory, then use mechanisms like locks and atomic operations to ensure consistency. We have been discussing the problems with this model throughout the entire volume: data races, deadlocks, spurious wakeups on condition variables, object lifecycles—each one is a pitfall.

The Actor model takes a completely different path: **share no state at all**. Each Actor has its own independent memory space, and the only way the outside world can affect it is by sending a message. This means there are no data races (because there is no shared mutable state), no need for locks (because there are no resources requiring mutually exclusive access), and it is naturally suited for distributed deployment—message passing is location-transparent, and the sender does not need to know whether the receiver is on the same machine or on the other side of the planet.

But there is no such thing as a free lunch. The Actor model introduces new complexities: message ordering, handling message loss, and Actor error propagation and recovery. We will discuss each of these next.

### Message Delivery Semantics

There are several different message delivery guarantee semantics, which is a very important conceptual dimension in distributed systems and the Actor model. The lightest is **at-most-once**: messages may be lost but will not be delivered more than once. The original definition of the Actor model uses this—once a message is sent, delivery is not guaranteed, much like sending a postcard; once you drop it in the mailbox, there is no way to confirm the recipient actually received it. One step stronger is **at-least-once**: messages will not be lost but may be delivered repeatedly. The sender keeps retrying until it receives an acknowledgment, but network partitions or timeouts might cause the same message to be processed twice, requiring the receiver to implement idempotency to handle this. The strongest is **exactly-once**: messages are neither lost nor duplicated, but the implementation cost is also the highest—usually requiring distributed transactions or a combination of idempotency and deduplication mechanisms.

The original Actor model uses at-most-once semantics. In Erlang's implementation, message passing is also "best effort"—delivery is not guaranteed, and the sender has no way to confirm whether a message arrived. This choice is intentional: stronger delivery guarantees mean more synchronization and higher latency, while the Actor model pursues high concurrency and high throughput.

> ⚠️ **Warning**: Although the original Actor model does not guarantee message ordering, many practical implementations (including Erlang) do guarantee ordering between the same pair of Actors—meaning if Actor A sends two messages M1 and M2 to Actor B, B will always receive M1 before M2. However, there is no ordering guarantee for messages sent from different senders to the same Actor.

## Implementing a Simple Actor in C++

Enough theory—let's get to work. Below, we implement a minimal Actor framework with exactly four core components: type-safe messages implemented with `std::variant` (`Message`), a thread-safe queue-based message mailbox (`Mailbox`), an Actor base class with a message loop and message dispatch, and an `ActorSystem` that manages the lifecycle and addressing of all Actors.

### Starting with Message Types

We use `std::variant` to implement message types. Why not create an inheritance-based `Message` base class? Because `std::variant` natively supports the visitation pattern, and combined with `std::overload`, it enables elegant pattern matching—which is extremely useful in Actor message handling.

First, let's define some message types we will use later:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <optional>
#include <functional>

// 前向声明
class Actor;

// 唯一标识一个 Actor
using ActorId = uint64_t;

// ===== 消息定义 =====

/// 普通字符串消息
struct StringMessage {
    std::string content;
};

/// 请求增加计数
struct IncrementMessage {
    int64_t delta{1};
};

/// 查询当前计数值
struct QueryMessage {};

/// 查询的应答
struct QueryResponse {
    int64_t value{0};
    ActorId requester{0};  // 回传给谁
};

/// 创建子 Actor 的请求
struct SpawnRequest {
    std::string actor_type;
};

/// 停止某个 Actor
struct StopMessage {
    ActorId target{0};
};

/// 错误报告——Actor 内部异常时发送给 supervisor
struct ErrorMessage {
    ActorId failed_actor{0};
    std::string error_description;
};

/// 所有消息的联合类型
using Message = std::variant<
    StringMessage,
    IncrementMessage,
    QueryMessage,
    QueryResponse,
    SpawnRequest,
    StopMessage,
    ErrorMessage
>;
```

You might feel that defining messages this way has a somewhat "hardcoded" feel—and it does. In real Actor frameworks (like CAF), message types are implemented through templates and type erasure, supporting arbitrary message types. But our goal is to understand the core mechanisms, not to reinvent the wheel, so using `std::variant` is sufficient. Its advantages are type safety, zero heap allocation (as long as the messages themselves are not large), and compile-time checking to ensure you have handled all message types.

> ⚠️ **Warning**: The size of a `std::variant` equals the size of its largest member plus a discriminant (used to record the index of the currently active alternative). Mainstream implementations typically use a small integer of 1–4 bytes to store it (only 1 byte is needed when there are no more than 255 alternatives), and due to alignment, the discriminant can often fit into the alignment padding of the member storage without taking up extra space. However, if you put a very large type inside it, all messages will occupy that size. So the principle of message design is: **small and lightweight**. When you need to pass large amounts of data, pass pointers or reference-counted objects (like `std::shared_ptr<std::vector<T>>`) instead of copying the data directly.

### The Mailbox: A Thread-Safe Queue

A mailbox is simply a thread-safe queue. If you followed the concurrent data structures in ch04, you have already seen this pattern. Here we present a concise implementation:

```cpp
#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>
#include <atomic>

template <typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue() = default;
    ~ThreadSafeQueue() = default;

    // 禁止拷贝
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /// 入队（阻塞）
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    /// 出队（阻塞，直到有数据或队列被关闭）
    std::optional<T> wait_and_pop()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] {
            return !queue_.empty() || closed_;
        });

        if (closed_ && queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// 尝试出队（非阻塞）
    std::optional<T> try_pop()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// 关闭队列，唤醒所有等待中的线程
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
    bool closed_{false};
};
```

This implementation should already be very familiar from ch04. The only new thing is the `close()` method—when an Actor stops, we need to close its mailbox so the message loop can exit. `wait_and_pop()` returns `std::nullopt` when the queue is closed and empty, and the message loop uses this to determine that it is time to exit.

### The Actor Core: The Message Loop

Now let's implement the core of the Actor—the message loop. Each Actor owns a mailbox (`ThreadSafeQueue<Message>`), a message loop thread, and a message handling function overridden by subclasses.

```cpp
#pragma once

#include "thread_safe_queue.hpp"
#include "message_types.hpp"

#include <thread>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

class Actor {
public:
    explicit Actor(ActorId id)
        : id_(id)
    {
    }

    virtual ~Actor()
    {
        stop();
    }

    // 禁止拷贝和移动
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    /// 启动 Actor 的消息循环
    void start()
    {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread(&Actor::message_loop, this);
    }

    /// 停止 Actor
    void stop()
    {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        running_.store(false, std::memory_order_release);
        mailbox_.close();

        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /// 向这个 Actor 发送消息
    void tell(Message msg)
    {
        if (running_.load(std::memory_order_acquire)) {
            mailbox_.push(std::move(msg));
        }
    }

    ActorId id() const { return id_; }
    bool running() const
    {
        return running_.load(std::memory_order_acquire);
    }

protected:
    /// 子类必须实现：处理一条消息
    /// 返回 true 表示继续运行，返回 false 表示停止
    virtual bool on_message(const Message& msg) = 0;

    /// 子类可选实现：Actor 启动前的初始化
    virtual void on_start() {}

    /// 子类可选实现：Actor 停止时的清理
    virtual void on_stop() {}

private:
    /// 消息循环——Actor 的核心
    void message_loop()
    {
        on_start();

        while (running_.load(std::memory_order_acquire)) {
            auto msg = mailbox_.wait_and_pop();
            if (!msg.has_value()) {
                // 邮箱被关闭，退出循环
                break;
            }

            try {
                bool should_continue = on_message(msg.value());
                if (!should_continue) {
                    break;
                }
            }
            catch (const std::exception& e) {
                // 异常不中断消息循环，只打印警告
                // 真实框架里会把异常上报给 supervisor
                std::cerr << "[Actor " << id_
                          << "] 异常: " << e.what() << "\n";
            }
        }

        running_.store(false, std::memory_order_release);
        on_stop();
    }

    ActorId id_;
    ThreadSafeQueue<Message> mailbox_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
```

The message loop is the heart of the entire Actor. Its job is to repeatedly pull messages from the mailbox and hand them to `on_message()` for processing. If the mailbox is closed or `on_message()` returns `false`, the loop ends.

There is a design decision worth discussing here: when `on_message()` throws an exception, we do not let it kill the entire Actor; instead, we catch it and continue running. This seemingly violates Erlang's "let it crash" philosophy, but in our minimal framework, each Actor is an independent thread, and letting it crash means the thread exits directly—at which point a supervisor would be needed to restart it. We will implement a supervisor later, but for now we play it safe.

### ActorSystem: Managing Everything

The ActorSystem is responsible for Actor creation, addressing, and lifecycle management. It is not an Actor itself, but a management container.

```cpp
#pragma once

#include "message_types.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <iostream>

// 前向声明
class Actor;

/// Actor 的工厂函数签名
using ActorFactory = std::function<std::unique_ptr<Actor>(ActorId)>;

class ActorSystem {
public:
    ActorSystem() = default;

    ~ActorSystem()
    {
        shutdown();
    }

    // 禁止拷贝
    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;

    /// 注册一个 Actor 工厂（按类型名）
    void register_factory(const std::string& type_name, ActorFactory factory)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[type_name] = std::move(factory);
    }

    /// 创建并启动一个 Actor
    template <typename ActorType, typename... Args>
    ActorId spawn(Args&&... args)
    {
        auto actor = std::make_unique<ActorType>(
            next_id(), std::forward<Args>(args)...
        );
        ActorId id = actor->id();
        Actor* raw_ptr = actor.get();  // 在 move 前缓存裸指针

        {
            std::lock_guard<std::mutex> lock(mutex_);
            actors_[id] = raw_ptr;
            owned_actors_.push_back(std::move(actor));
        }

        raw_ptr->start();  // 用缓存指针而非重新查找 map
        return id;
    }

    /// 通过工厂创建 Actor
    ActorId spawn_from_factory(const std::string& type_name)
    {
        auto it = factories_.find(type_name);
        if (it == factories_.end()) {
            std::cerr << "[ActorSystem] 未知 Actor 类型: "
                      << type_name << "\n";
            return 0;  // 无效 ID
        }

        auto actor = it->second(next_id());
        ActorId id = actor->id();
        Actor* raw_ptr = actor.get();  // 在 move 前缓存裸指针

        {
            std::lock_guard<std::mutex> lock(mutex_);
            actors_[id] = raw_ptr;
            owned_actors_.push_back(std::move(actor));
        }

        raw_ptr->start();  // 用缓存指针而非重新查找 map
        return id;
    }

    /// 向指定 Actor 发送消息
    void tell(ActorId target, Message msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(target);
        if (it != actors_.end()) {
            it->second->tell(std::move(msg));
        }
    }

    /// 查找 Actor
    Actor* find(ActorId id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(id);
        return it != actors_.end() ? it->second : nullptr;
    }

    /// 停止指定 Actor
    void stop(ActorId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(id);
        if (it != actors_.end()) {
            it->second->stop();
            actors_.erase(it);
        }
    }

    /// 关闭整个系统
    void shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, actor] : actors_) {
            actor->stop();
        }
        actors_.clear();
        owned_actors_.clear();
    }

private:
    ActorId next_id()
    {
        return ++next_id_;
    }

    mutable std::mutex mutex_;
    std::atomic<ActorId> next_id_{0};
    std::unordered_map<ActorId, Actor*> actors_;
    std::vector<std::unique_ptr<Actor>> owned_actors_;
    std::unordered_map<std::string, ActorFactory> factories_;
};
```

The design of `ActorSystem` is fairly straightforward: it uses a `unordered_map` to maintain an ID-to-Actor-pointer mapping, while holding ownership with a `vector<unique_ptr>`. `spawn` is a template method that can create any type of Actor. `tell` is an indirect message-sending method—in real frameworks, this is typically done through an ActorRef (a lightweight Actor reference object) rather than holding a pointer directly.

> ⚠️ **Warning**: This implementation stores raw pointers (`Actor*`) in the map. This is to simplify the code, but in a multithreaded environment it carries the risk of dangling pointers—if an Actor is stopped but another thread still holds its pointer. Production-grade frameworks would use `weak_ptr` or an ActorRef (an unforgeable address token) to solve this problem.

### Pattern Matching on Messages

`std::variant` combined with `std::visit` can implement pattern-matching-style message handling. C++17 does not have language-level pattern matching, but we can use a helper utility to make the code clearer:

```cpp
// 辅助工具：一组可调用对象的 overload 集合
template <typename... Handlers>
struct overload : Handlers... {
    using Handlers::operator()...;
};

// 推导指引
template <typename... Handlers>
overload(Handlers...) -> overload<Handlers...>;
```

With this utility, message handling can be written like this:

```cpp
bool on_message(const Message& msg) override
{
    return std::visit(overload{
        [this](const IncrementMessage& m) {
            counter_ += m.delta;
            return true;
        },
        [this](const QueryMessage& m) {
            // 向请求者发送应答
            if (system_ && requester_) {
                system_->tell(requester_,
                    QueryResponse{counter_, id()});
            }
            return true;
        },
        [](const StringMessage& m) {
            std::cout << "收到: " << m.content << "\n";
            return true;
        },
        // 其他消息类型可以加在这里
        [](const auto&) {
            // 未知消息，忽略
            return true;
        }
    }, msg);
}
```

The elegance of this approach lies in the fact that `std::visit` requires the visitor to handle all alternative types of the variant—if you remove the trailing `[](const auto&)` catch-all handler and forget to handle one type, the compiler will error out directly. This is much safer than switch-case, because forgetting a case in a switch is only a warning, while an incomplete set of types for `std::visit` is a hard error. Of course, once you add the `auto` catch-all handler, it catches all unmatched types and the compiler will no longer remind you—so the catch-all handler is a double-edged sword: convenient, but it also means you might be silently ignoring certain messages.

## In Practice: A Distributed Counter

Now let's assemble the parts we built above and implement a simple distributed counter. The scenario is this: we have multiple Counter Actors, each maintaining its own local counter; simultaneously, an Aggregator Actor periodically queries all Counter Actors for their counts and aggregates the output.

### Counter Actor

```cpp
#include "actor.hpp"
#include "actor_system.hpp"

/// 计数器 Actor：维护一个局部计数器
class CounterActor : public Actor {
public:
    CounterActor(ActorId id, ActorSystem* system)
        : Actor(id), system_(system)
    {
    }

protected:
    bool on_message(const Message& msg) override
    {
        return std::visit(overload{
            [this](const IncrementMessage& m) {
                counter_ += m.delta;
                return true;
            },
            [this](const QueryMessage&) {
                // 向聚合者报告当前值
                if (aggregator_id_ != 0) {
                    system_->tell(aggregator_id_,
                        QueryResponse{counter_, id()});
                }
                return true;
            },
            [this](const StringMessage& m) {
                std::cout << "[Counter " << id()
                          << "] " << m.content
                          << " (当前值: " << counter_ << ")\n";
                return true;
            },
            [](const auto&) { return true; }
        }, msg);
    }

private:
    friend class AggregatorActor;  // 允许聚合者设置 ID

    int64_t counter_{0};
    ActorSystem* system_;
    ActorId aggregator_id_{0};
};
```

### Aggregator Actor

```cpp
#include "actor.hpp"
#include "actor_system.hpp"
#include <unordered_map>
#include <vector>

/// 聚合 Actor：收集所有 Counter 的值并汇总
class AggregatorActor : public Actor {
public:
    AggregatorActor(ActorId id, ActorSystem* system)
        : Actor(id), system_(system)
    {
    }

    /// 注册一个 Counter Actor
    void register_counter(ActorId counter_id)
    {
        counter_ids_.push_back(counter_id);

        // 告诉 Counter 自己是谁，方便它回复查询
        auto* actor = system_->find(counter_id);
        if (auto* counter = dynamic_cast<CounterActor*>(actor)) {
            counter->aggregator_id_ = id();
        }
    }

protected:
    void on_start() override
    {
        std::cout << "[Aggregator] 启动，监控 "
                  << counter_ids_.size() << " 个 Counter\n";
    }

    bool on_message(const Message& msg) override
    {
        return std::visit(overload{
            [this](const QueryResponse& resp) {
                // 收集来自某个 Counter 的报告
                collected_[resp.requester] = resp.value;
                received_++;

                // 如果所有 Counter 都报告了，汇总输出
                if (received_ >= counter_ids_.size()) {
                    int64_t total = 0;
                    for (auto& [id, val] : collected_) {
                        total += val;
                    }
                    std::cout << "[Aggregator] 汇总: 总计 = "
                              << total << " (来自 "
                              << received_ << " 个 Counter)\n";

                    // 重置，等待下一轮
                    received_ = 0;
                    collected_.clear();
                }
                return true;
            },
            [](const StringMessage& m) {
                std::cout << "[Aggregator] 收到: "
                          << m.content << "\n";
                return true;
            },
            [](const auto&) { return true; }
        }, msg);
    }

private:
    ActorSystem* system_;
    std::vector<ActorId> counter_ids_;
    std::unordered_map<ActorId, int64_t> collected_;
    size_t received_{0};
};
```

### Assembling and Running

Now let's assemble all the parts and see the result:

```cpp
#include "counter_actor.hpp"
#include "aggregator_actor.hpp"
#include "actor_system.hpp"

#include <thread>
#include <chrono>

int main()
{
    ActorSystem system;

    // 创建 Aggregator
    auto agg_id = system.spawn<AggregatorActor>(&system);

    // 创建 3 个 Counter
    auto c1 = system.spawn<CounterActor>(&system);
    auto c2 = system.spawn<CounterActor>(&system);
    auto c3 = system.spawn<CounterActor>(&system);

    // 注册到 Aggregator
    auto* agg = dynamic_cast<AggregatorActor*>(system.find(agg_id));
    agg->register_counter(c1);
    agg->register_counter(c2);
    agg->register_counter(c3);

    // 给 Counter 发一些增量消息
    for (int i = 0; i < 5; ++i) {
        system.tell(c1, IncrementMessage{2});
        system.tell(c2, IncrementMessage{3});
        system.tell(c3, IncrementMessage{1});
    }

    // 等一下让消息处理完
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 发起查询：每个 Counter 收到 QueryMessage 后回复 Aggregator
    system.tell(c1, QueryMessage{});
    system.tell(c2, QueryMessage{});
    system.tell(c3, QueryMessage{});

    // 等待聚合完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 预期输出：
    // [Aggregator] 启动，监控 3 个 Counter
    // [Aggregator] 汇总: 总计 = 30 (来自 3 个 Counter)
    //   (c1 = 10, c2 = 15, c3 = 5)

    system.shutdown();
    return 0;
}
```

This example demonstrates the typical interaction pattern of the Actor model: message-driven, no shared state, asynchronous communication. The Counter Actor only cares about its own count, and the Aggregator Actor only cares about aggregation—there are no shared variables between them, no locks, and coordination is done entirely through messages. Although this example is simple, you can imagine extending it to a distributed scenario: Counters on different machines, the Aggregator sending and receiving messages over the network—the code structure would barely need to change.

## Error Propagation and Supervisor Strategies

One of the most striking innovations in the Actor model is Erlang's **supervisor** mechanism and **"let it crash"** philosophy. This idea is counterintuitive but extremely practical: rather than writing extensive defensive error-handling code in every Actor, let Actors only write the "happy path," crash when something goes wrong, and let their supervisor decide how to recover.

### Erlang's Let It Crash

In Erlang, every process (Actor) has a supervisor. When a process crashes, the supervisor receives a notification and handles it according to a preset strategy. The most common is **one_for_one**—only the crashed process is restarted, and sibling processes are unaffected. If processes have tight dependencies, you can use **one_for_all**: when one process crashes, all sibling processes are terminated and then restarted together. There is also a compromise called **rest_for_one**—the crashed process and all sibling processes started after it are restarted together, suitable for process chains with sequential dependencies.

Supervisors themselves are also Actors, so a supervisor can have its own supervisor—forming a supervisor tree. If a supervisor at a certain level itself crashes (for example, if the restart frequency is too high), its parent supervisor takes over. This hierarchical fault-tolerance mechanism is the key to Erlang achieving ultra-high availability.

### Implementing a Supervisor in C++

Below is a simplified supervisor implementation. When a child Actor throws an exception, the supervisor catches it and decides whether to restart or stop based on the strategy.

```cpp
/// supervisor 策略
enum class SupervisorStrategy {
    kRestart,   // 重启子 Actor
    kStop,      // 停止子 Actor
    kEscalate   // 上报给更上层的 supervisor
};

/// supervisor 的配置
struct SupervisorConfig {
    SupervisorStrategy strategy{SupervisorStrategy::kRestart};
    int max_restarts{3};          // 在时间窗口内的最大重启次数
    int restart_window_seconds{60}; // 时间窗口（秒）
};

class SupervisorActor : public Actor {
public:
    SupervisorActor(ActorId id, ActorSystem* system,
                    SupervisorConfig config = {})
        : Actor(id)
        , system_(system)
        , config_(config)
    {
    }

    /// 注册一个子 Actor 的工厂（用于重启）
    void register_child(ActorId child_id, ActorFactory factory)
    {
        std::lock_guard<std::mutex> lock(children_mutex_);
        children_[child_id] = std::move(factory);
    }

protected:
    bool on_message(const Message& msg) override
    {
        return std::visit(overload{
            [this](const ErrorMessage& err) {
                handle_error(err);
                return true;
            },
            [](const StringMessage& m) {
                std::cout << "[Supervisor] " << m.content << "\n";
                return true;
            },
            [](const auto&) { return true; }
        }, msg);
    }

private:
    void handle_error(const ErrorMessage& err)
    {
        std::cout << "[Supervisor] 收到错误报告: Actor "
                  << err.failed_actor << " - "
                  << err.error_description << "\n";

        switch (config_.strategy) {
            case SupervisorStrategy::kRestart:
                restart_child(err.failed_actor);
                break;
            case SupervisorStrategy::kStop:
                system_->stop(err.failed_actor);
                std::cout << "[Supervisor] 已停止 Actor "
                          << err.failed_actor << "\n";
                break;
            case SupervisorStrategy::kEscalate:
                // 在真实系统里，这里应该向自己的 supervisor 发消息
                std::cout << "[Supervisor] 上报错误给上级\n";
                break;
        }
    }

    void restart_child(ActorId child_id)
    {
        std::lock_guard<std::mutex> lock(children_mutex_);

        auto it = children_.find(child_id);
        if (it == children_.end()) {
            std::cerr << "[Supervisor] 找不到子 Actor 的工厂: "
                      << child_id << "\n";
            return;
        }

        // 停止旧的
        system_->stop(child_id);

        // 用工厂创建新的
        auto new_actor = it->second(++next_child_id_);
        ActorId new_id = new_actor->id();

        std::cout << "[Supervisor] 重启 Actor: "
                  << child_id << " -> " << new_id << "\n";

        // 更新注册表（简化实现）
        children_.erase(child_id);
        // 新 Actor 的工厂沿用旧的
        // 实际上需要重新注册...
        // 这里简化了，省略了重新注册的细节
    }

    ActorSystem* system_;
    SupervisorConfig config_;
    std::mutex children_mutex_;
    std::unordered_map<ActorId, ActorFactory> children_;
    ActorId next_child_id_{1000};  // 子 Actor ID 计数器
};
```

This supervisor implementation is highly simplified, but it conveys the core idea. The key is the `handle_error` method: upon receiving an error message, it decides whether to restart or stop based on the strategy. The essence of restarting is "recreate using the factory"—this is why the supervisor needs to hold factories for child Actors rather than the Actors themselves. After an Actor crashes, it may be in an indeterminate state, and directly "fixing" it is unsafe; instead, destroying the old one and creating a brand-new one is the clean approach.

> ⚠️ **Warning**: The `restart_child` here has a simplification flaw—after restarting, all places referencing the old Actor ID need to be updated. In real frameworks, an ActorRef is an indirection layer; after a restart, the ActorRef points to the new Actor, and senders do not need to be aware of the change. We omit this indirection layer here to keep the code readable.

## Advantages and Limitations of the Actor Model

At this point, we have a fairly complete understanding of the Actor model, from theory to implementation to error handling. Before moving on to CSP in the next chapter, let's calmly evaluate the pros and cons of this model.

First, the advantages. The foremost is conceptual simplicity—no shared state means you do not need to worry about lock granularity and ordering, and each Actor only needs to care about its own state and the messages it receives. Second is natural suitability for distribution—message passing is location-transparent, and scaling a system from a single machine to a cluster only requires swapping out the message transport layer. Third is fault tolerance—the combination of supervisor trees and let it crash has been proven very effective in engineering practice, especially in systems that require high availability.

Now, the limitations. Performance is an eternal topic—message passing implies data copying (at least logically), which is slower than directly reading and writing shared memory. Although you can use shared pointers to avoid deep copies, a shared pointer is itself a form of "shared state," bringing us full circle. Message ordering is another pitfall—although ordering is usually guaranteed between the same pair of Actors, there is no deterministic guarantee for the interleaving of messages from multiple Actors, which makes debugging difficult. Finally, the Actor model is inherently unsuitable for fine-grained parallel computation—you cannot use Actors to parallelize processing of every element in an array, because the overhead of creating an Actor far exceeds the computation itself.

## Where We Are

In this chapter, we started with the history and theory of the Actor model, understanding its core idea: replacing shared memory with message passing, and replacing shared-state threads with independent computational entities. Then we implemented a minimal Actor framework in C++, including type-safe messages (`std::variant`), a thread-safe mailbox, an Actor base class, an ActorSystem, and supervisor error handling. Finally, we tied all the parts together with a distributed counter.

But the Actor model is only one branch of the "don't share memory" path. In the next chapter, we will look at another school of thought with equally deep theoretical foundations—CSP (Communicating Sequential Processes), proposed by Tony Hoare in 1978, with Go's goroutine + channel as its classic implementation. Actors have identity and mailboxes, while CSP's channels are anonymous—this difference may seem subtle, but it produces significant differences in actual programming style.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch07-actor-channel/`.

## References

- [Actor model — Wikipedia](https://en.wikipedia.org/wiki/Actor_model) — Complete history and theoretical introduction to the Actor model
- [Hewitt, Bishop, Steiger (1973). "A Universal Modular ACTOR Formalism for Artificial Intelligence"](https://worrydream.com/refs/Hewitt_1973_-_A_Universal_Modular_Actor_Formalism_for_Artificial_Intelligence.pdf) — The original Actor model paper (IJCAI 1973)
- [Erlang OTP Design Principles — Supervisor Behaviour](https://www.erlang.org/doc/design_principles/sup_princ) — Official documentation for Erlang supervisors
- [C++ Actor Framework (CAF)](https://actor-framework.org/) — The most mature Actor framework implementation in C++
- [SObjectizer](https://github.com/Stiffstream/sobjectizer) — Another active C++ Actor framework
- [Akka Documentation](https://doc.akka.io/) — The most well-known Actor framework on the JVM, with very clear documentation explaining Actor model concepts
