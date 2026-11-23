---
title: "The Actor Model and Supervisor Strategies"
description: "The core ideas and message-delivery semantics of the Actor model, implementing a minimal C++ Actor framework with std::variant messages, a thread-safe mailbox, an ActorSystem, and supervisor strategies, closing with a distributed counter that strings the parts together."
chapter: 5
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/08-actor-model.md
  source_hash: 151fc9608a300747f485fdac5953d9b0812013f9a1b266310f1697254754b3a2
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 26128
---
# The Actor Model and Supervisor Strategies

This piece is the readings section's first "paradigm switch": the Volume 5 required line walks the shared-memory-plus-locks road from start to finish (Unit 2's mutual exclusion, Unit 3's waiting, Unit 4's task organization), while the Actor model represents another road — no shared memory, concurrency organized through message passing. It enters no required unit; content beyond the requirement lives in the readings section. It and the next piece, [Channel and CSP: Select, Pipelines](09-channel-csp), form a pair — two implementation paths toward the same goal — and we read them as a pair.

Before reading it, you need to be comfortable with threads, mutexes, and condition variables ([The First std::thread Program](../../chapter01-start-concurrency/03-first-std-thread), [Recovering the Lost Numbers — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks)), and ideally the queue's closing semantics ([What If the Queue Cannot Close: Closing and Backpressure](../../chapter03-wait-and-coordinate/04-queue-close-and-backpressure)) — the Actor's mailbox is a direct reuse of that pattern. `std::variant` and `std::visit` are explained on the spot; no prior mastery required.

The migration trail: mechanism explanations, code, and exercises are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The material's claimed build environment (GCC 12+ / Clang 15+ / MSVC 2022+, `-std=c++17 -pthread -O2`) remains as-is; I did not recompile and re-verify this round.

Up to this point, every concurrency model in this volume has been essentially the same thing — shared memory plus locks. Mutex, condition_variable, atomic — the underlying logic is always "several threads see the same memory; synchronization primitives keep their accesses from clashing". The model works, but as the system scales it becomes harder and harder to manage: how to choose lock granularity, how to order lock acquisition, how to prevent deadlock — every item demands engineer judgment, and human judgment is the least reliable thing there is.

But one school rejects this road at the root. The Actor model says: stop sharing memory — every message is a copy, every computational entity is independent, and they communicate solely through asynchronous messages; let us walk this road in full. No shared state means no race conditions and no need for locks. The idea sounds a bit utopian, but it has been validated industrially at scale in Erlang/Akka: Ericsson's telecom systems built on Erlang/OTP claim nine nines (99.9999999%) of availability. How that number is measured is disputed, but Erlang's standing in high availability is solid fact.

In this piece we go deep into the Actor model: its theoretical foundation, core concepts, and a C++ implementation. We will not build a production-grade Actor framework (that is CAF's or SObjectizer's business) but will implement a minimal framework complete enough to string together all the Actor model's core ideas.

## Environment Note

All code in this piece is based on C++17; my build environment is GCC 12+ / Clang 15+ / MSVC 2022+ with `-std=c++17 -pthread -O2`. Linux / macOS / Windows all work — as long as your standard library implements `<thread>`, `<mutex>`, and `<condition_variable>`, you can compile directly. The code depends on no third-party library, uses only standard-library components, and compiles as-is once copied over.

## The Actor Model's Origins and Core Ideas

The Actor model was proposed by Carl Hewitt, Peter Bishop, and Richard Steiger; the paper appeared at IJCAI 1973, and its title is collected in the reference resources for those who want to follow the trail. It was born against the backdrop of AI research at MIT, motivated by "looking forward to parallel computer architectures composed of tens, hundreds, or even thousands of independent microprocessors". The prediction looks fairly precise today — multicore processors and distributed systems have become the mainstream.

> If you are curious about the original paper, you can find it in the IJCAI 1973 proceedings. Though the original context was AI research, the Actor model itself is a general model of concurrent computation.

The Actor model's central claim is **"everything is an Actor"**, kindred in spirit to object orientation's "everything is an object". Each Actor is an independent computational entity holding its own private state, inaccessible from outside. So how do Actors interact? Let us say it plainly: exactly one way — sending messages.

By Hewitt's definition, upon receiving a message an Actor may do three things at once: send a finite number of messages to other Actors (only to Actors whose addresses it knows), create a finite number of new Actors (created dynamically, with no cap on the total), and decide its behavior when handling the next message — meaning an Actor's behavior can change over time. Note there is no ordering among the three; they may happen in parallel. Here lies the Actor model's fundamental difference in mindset from the traditional "sequential execution + shared memory" model: Actors are concurrent entities by birth, and sequential execution is merely one of its degenerate special cases.

### The Fundamental Difference from the Shared-Memory Model

In the shared-memory model, several threads exchange information by reading and writing the same memory, then use locks, atomics, and the like to keep it consistent. What ails this model has been this whole volume's topic: data races, deadlocks, condition-variable spurious wakeups, object lifetimes — each one a concrete headache.

The Actor model takes a completely different road: **share no state at all**. Each Actor owns an independent memory space, and the only way the outside can influence it is by message. That means no data races (no shared mutable state), no locks (no resource requiring mutually exclusive access), and a natural fit for distributed deployment — message passing is location-agnostic; you need not know whether the receiver sits on the neighboring machine or on the far side of the planet.

But no bargain comes free. The Actor model imports new complexities: message ordering, handling lost messages, Actor error propagation and recovery — we face them one by one. All of these are discussed in turn below.

### The Semantics of Message Passing

Message passing comes with several delivery-guarantee semantics, an important conceptual dimension in distributed systems and the Actor model. The lightest is **at-most-once**: messages may be lost but never duplicated — the Actor model's original definition is exactly this; once a message is sent, arrival is not guaranteed, like mailing a postcard: once it is in the mailbox you can no longer confirm the recipient got it. One rung stronger is **at-least-once**: messages are not lost but may be duplicated — the sender keeps retrying until an acknowledgment arrives, yet network partitions or timeouts may get the same message processed twice, and the receiver must implement idempotency to cope. The strongest is **exactly-once**: messages neither vanish nor repeat, but at the highest implementation cost — typically demanding distributed transactions or idempotency plus deduplication.

Back to the original definition: the Actor model uses at-most-once semantics. In Erlang's implementation, message passing is likewise "best effort" — no delivery guarantee, and the sender has no way to confirm arrival. The choice is deliberate: stronger delivery guarantees mean more synchronization and higher latency, while the Actor model chases high concurrency and high throughput.

> **Note**: the original Actor model does not guarantee message ordering — but one addendum: many real implementations (Erlang included) do guarantee ordering between the same pair of Actors, that is, of two messages M1 and M2 sent from Actor A to Actor B, B receives M1 before M2. Messages from different senders to the same Actor carry no ordering guarantee.

## Implementing a Simple Actor in C++

Theory said enough; now to work. We are about to implement a minimal Actor framework with exactly four core components; let us meet them one by one: the type-safe `Message` built on `std::variant`, the `Mailbox` built on a thread-safe queue, the Actor base class with its message loop and dispatch, and the `ActorSystem` managing every Actor's lifecycle and addressing.

### Starting with the Message Type

We implement the message type with `std::variant`. Why not an inheritance-based `Message` base class? Because `std::variant` natively supports the visitation pattern, and paired with `std::overload` it yields elegant pattern matching — exactly what we need in the Actor's message handling.

Defining some message types we will use later:

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

You may feel this way of defining messages is a bit "hardcoded" — it is. In a real Actor framework (CAF, for example), message types are realized through templates and type erasure and can carry messages of any type. But our goal is understanding the core machinery, not building a wheel, so `std::variant` suffices. Its benefits: type safety, zero heap allocation (as long as messages stay small), and compile-time checking that you have handled every message type.

> **Note**: a `std::variant`'s size equals its largest member plus a discriminant (recording the index of the currently active alternative); mainstream implementations usually store it in a 1-4 byte small integer (1 byte when there are no more than 255 alternatives), and thanks to alignment the discriminant often tucks into the members' alignment padding, taking no extra room. But put one very large type inside and every message pays that size. Hence the message-design principle: **small and light**. When moving large data, pass a pointer or a reference-counted object (say `std::shared_ptr<std::vector<T>>`) instead of copying the data.

### The Mailbox: A Thread-Safe Queue

The mailbox is simply a thread-safe queue. If you have read [A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue), you have already seen this pattern. Here is a concise implementation:

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

The queue's skeleton was already covered in Unit 3. The only new thing is the `close()` method — when an Actor stops, we need to close its mailbox so the message loop exits. `wait_and_pop()` returns `std::nullopt` when the queue is both closed and empty; the message loop takes that as its cue to leave.

### The Actor Core: The Message Loop

Now we implement the Actor's core, the message loop. Each Actor owns a mailbox (a `ThreadSafeQueue<Message>`), a message-loop thread, and a message-handling function overridden by subclasses.

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

The message loop is the Actor's heart; watch it work: repeatedly take a message from the mailbox, then hand it to `on_message()`. If the mailbox closes or `on_message()` returns `false`, the loop ends.

One design decision here deserves words: when `on_message()` throws, we do not let it kill the whole Actor — we catch and keep running. It may look like a violation of Erlang's "let it crash" philosophy, but in this simple framework every Actor is its own thread; letting it crash means the thread exits outright, and then a supervisor would be needed to restart it. We implement the supervisor later; for now we stay conservative.

### ActorSystem: Managing Everything

ActorSystem is responsible for Actor creation, addressing, and lifecycle management. It is not itself an Actor; treat it as a management container.

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

`ActorSystem`'s design is fairly direct; a quick pass: it keeps an ID-to-Actor-pointer map in an `unordered_map` while holding ownership in a `vector<unique_ptr>`. `spawn` is a template method that can create any Actor type. `tell` is an indirect way to send messages — in real frameworks this usually goes through an ActorRef (a lightweight Actor reference object) rather than holding pointers directly.

> **Note**: this implementation stores raw pointers (`Actor*`) in the map. That simplifies the code but risks dangling pointers in a multithreaded setting — one Actor gets stopped while another thread still holds its pointer. Production-grade frameworks use `weak_ptr` or an ActorRef (an unforgeable address token); we rely on those to solve the problem.

### Pattern Matching on Messages

`std::variant` with `std::visit` enables pattern-matching-style message handling. C++17 has no language-level pattern matching, but one helper makes the code clearer:

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

With this helper, message handling can be written like this:

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

The elegance of this style, pointed out for you: `std::visit` has one hard requirement — the visitor must handle every alternative of the variant, no exceptions. Remove the trailing `[](const auto&)` catch-all and leave one type unhandled, and the compiler errors out immediately. That is far safer than switch-case, where a forgotten case is only a warning, while an incomplete `std::visit` visitor is a hard error. Of course, once you add the `auto` catch-all it swallows every unmatched type and the compiler stays silent again — the catch-all is a double-edged sword: convenient, yes, but it also means you might be quietly ignoring certain messages.

## In Practice: A Distributed Counter

Now we assemble the parts from before into a simple distributed counter. The scenario: several Counter Actors, each maintaining its own local count, plus one Aggregator Actor that periodically queries every Counter's count and prints the aggregate.

### The Counter Actor

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

### The Aggregator Actor

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

Now assemble all the parts and see the result:

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

This example shows the Actor model's typical interaction pattern — message-driven, no shared state, asynchronous communication; let us absorb it fully. The Counter Actors care only about their own counts, the Aggregator only about summing — no shared variables between them, no locks, coordination entirely through messages. Simple as the example is, you can imagine extending it to a distributed setting: Counters on different machines, the Aggregator sending and receiving over the network — the code structure barely changes.

## Error Propagation and Supervisor Strategies

Among the Actor model's most eye-catching innovations is Erlang's **supervisor** mechanism and its **"let it crash"** philosophy; let us get it explained clearly. The idea is counterintuitive but extremely practical: rather than writing heaps of defensive error handling inside every Actor, let Actors contain only the happy path — on failure, crash — and let the supervisor decide how to recover.

### Erlang's Let It Crash

In Erlang, every process (Actor) has a supervisor; three strategies, let us look. When a process crashes, the supervisor is notified and acts per its configured strategy. The most used is **one_for_one** — restart only the crashed process, leaving its siblings untouched. With tight inter-process dependencies, **one_for_all** applies: one process crashes, all siblings are terminated and restarted together. And a middle option, **rest_for_one**, restarts the crashed process together with every sibling started after it — fitting chains with startup-order dependencies.

The supervisor is itself an Actor, so a supervisor can have its own supervisor, forming a supervisor tree. If one level's supervisor itself crashes (say, from too-high a restart frequency), its parent supervisor takes over. Get this hierarchical fault-tolerance machinery clear in view — it is the key to Erlang's extreme availability.

### Implementing a Supervisor in C++

Below is a simplified supervisor implementation. When a child Actor throws, the supervisor catches the exception and decides per strategy whether to restart or stop.

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

This supervisor implementation is heavily simplified, but it conveys the core idea. The spot demanding our full attention is `handle_error`: upon receiving an error message, decide per strategy to restart or stop. The restart idea is "recreate from the factory" — which is why the supervisor holds the child Actor's factory rather than the Actor itself. A crashed Actor may sit in an indeterminate state; "repairing" it in place is unsafe. Destroying the old one and creating a fresh one is the clean approach.

> **Note**: `restart_child` here carries a simplification flaw — after a restart, every place referencing the old Actor ID needs updating. In a real framework the ActorRef is a layer of indirection: after restart the ActorRef points at the new Actor, and senders never notice. We omit that indirection here to keep the code readable.

## The Actor Model's Strengths and Limits

By now we hold a fairly complete picture of the Actor model — theory, implementation, error handling. Before entering the next piece on CSP, let us soberly weigh the model's merits and faults.

On the strengths side, the essentials. Conceptually it is clean: no shared state means no fretting over lock granularity and ordering; each Actor minds only its own state and the messages it receives. It is a natural fit for distribution — message passing is location-agnostic, and scaling a system from one machine to a cluster means swapping the message transport layer. Fault tolerance comes too: the supervisor tree + let it crash combination has proven highly effective in engineering practice, especially in systems demanding high availability.

On the limits side, performance is the eternal topic: message passing means data copying (logically at least), slower than directly reading and writing shared memory; shared pointers can dodge the deep copy, but a shared pointer is itself a kind of "shared state" — the road loops back. Message ordering is another nuisance: ordering between the same pair of Actors is usually guaranteed, but interleavings among multiple Actors have no determinism guarantee, which makes debugging hard. Finally, the Actor model is inherently unfit for fine-grained parallel computation — you cannot use Actors to process each element of an array in parallel, because creating an Actor costs far more than the computation itself.

## Where to Go from Here

In this piece we started from the Actor model's history and theory and understood its core idea — replacing shared memory with message passing, shared-state threads with independent computational entities. Then we implemented a minimal Actor framework in C++, including type-safe messages (`std::variant`), a thread-safe mailbox, the Actor base class, ActorSystem, and supervisor error handling. Finally a distributed counter strung all the parts together.

But the Actor model is only one branch of the "no shared memory" road. The next piece looks at another school with equally deep theoretical roots, [Channel and CSP: Select, Pipelines](09-channel-csp), proposed by Tony Hoare in 1978; Go's goroutine + channel is its classic implementation. Actors have identity and mailboxes; CSP's channels are anonymous — the distinction looks subtle, but it produces large differences in actual programming style.

> The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); take it from `code/volumn_codes/vol5/ch07-actor-channel/`.

## Reference Resources

- [Actor model — Wikipedia](https://en.wikipedia.org/wiki/Actor_model) — a complete history and theoretical introduction to the Actor model
- [Hewitt, Bishop, Steiger (1973). "A Universal Modular ACTOR Formalism for Artificial Intelligence"](https://worrydream.com/refs/Hewitt_1973_-_A_Universal_Modular_Actor_Formalism_for_Artificial_Intelligence.pdf) — the original Actor model paper (IJCAI 1973)
- [Erlang OTP Design Principles — Supervisor Behaviour](https://www.erlang.org/doc/design_principles/sup_princ) — the official Erlang supervisor documentation
- [C++ Actor Framework (CAF)](https://actor-framework.org/) — the most mature Actor framework implementation in C++
- [SObjectizer](https://github.com/Stiffstream/sobjectizer) — another active C++ Actor framework
- [Akka Documentation](https://doc.akka.io/) — the best-known Actor framework on the JVM; its documentation explains the Actor model's concepts very clearly
