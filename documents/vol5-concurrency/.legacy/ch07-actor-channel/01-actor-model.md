---
title: "Actor 模型与消息传递"
description: "理解 Actor 模型的核心思想——用消息传递替代共享内存，实现一个简易的 C++ Actor 框架"
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
cpp_standard: [17, 20]
prerequisites:
  - "线程安全队列"
  - "线程池设计"
related:
  - "Channel 与 CSP 模型"
---

# Actor 模型与消息传递

到这一步为止，我们在这整卷里用的并发模型本质上都是同一种东西——共享内存加锁。无论是 mutex、condition_variable 还是 atomic，底层逻辑都是"多个线程看到同一块内存，用同步原语保证访问不冲突"。这套模型能工作，但说实话，随着系统规模变大，它会变得越来越难管理：锁的粒度怎么选、锁的获取顺序怎么定、死锁怎么预防——每一项都需要工程师的判断力，而人的判断力是最不可靠的东西。

但有一个流派从根本上否定了这条路。Actor 模型说：别共享内存了，每条消息都是一份拷贝，每个计算实体都是独立的，之间只通过异步消息通信。没有共享状态，就没有竞争条件，就不需要锁。这个想法听起来有点乌托邦，但它在 Erlang/Akka 里已经被大规模工业验证了——Ericsson 用 Erlang/OTP 构建的电信系统号称达到了九个九（99.9999999%）的可用性，虽然这个数字的测量方法见仁见智，但 Erlang 在高可用领域的地位是实打实的。

这一篇我们就来深入 Actor 模型：它的理论基础、核心概念、和 C++ 实现。我们不会去造一个生产级的 Actor 框架（那是 CAF 或者 SObjectizer 的事），但会实现一个足够完整的最小框架，把 Actor 模型的核心思想都串起来。

## 环境说明

这篇的所有代码基于 C++17，笔者的编译环境是 GCC 12+ / Clang 15+ / MSVC 2022+，编译选项 `-std=c++17 -pthread -O2`，Linux / macOS / Windows 都可以跑（只要你的标准库实现了 `<thread>`、`<mutex>`、`<condition_variable>`）。代码不依赖任何第三方库，只使用标准库组件，直接抄就能编译。

## Actor 模型的起源与核心思想

Actor 模型由 Carl Hewitt、Peter Bishop 和 Richard Steiger 在 1973 年的论文 *"A Universal Modular ACTOR Formalism for Artificial Intelligence"* 中首次提出。它最初是在 MIT 的人工智能研究背景下诞生的，动机是"展望由几十、几百甚至上千个独立微处理器组成的并行计算架构"。这个预测在今天看来相当精准——多核处理器和分布式系统已经成为了主流。

> 如果你好奇这篇原始论文，可以在 IJCAI 1973 的会议记录中找到它。虽然原始论文的语境是 AI 研究，但 Actor 模型本身是一个通用的并发计算模型。

Actor 模型的核心主张是 **"一切皆 Actor"**，这和面向对象里的"一切皆对象"异曲同工。每个 Actor 是一个独立的计算实体，拥有自己的私有状态，外部无法直接访问。那 Actor 之间怎么交互？只有一种方式：发消息。

根据 Hewitt 的定义，当一个 Actor 收到一条消息后，它可以同时做三件事：向其他 Actor 发送有限数量的消息（只能发给它知道地址的 Actor）、创建有限数量的新 Actor（动态创建，数量不限）、以及决定处理下一条消息时的行为——也就是说 Actor 的行为是可以随时间变化的。这三件事之间没有顺序要求，可以并行发生。这就是 Actor 模型和传统的"顺序执行 + 共享内存"模型在思维层面的根本差异：Actor 天生就是并发实体，顺序执行反而是它的一个特殊退化。

### 和共享内存模型的根本区别

在共享内存模型里，多个线程通过读写同一块内存来交换信息，然后用锁、原子操作等机制来保证一致。这种模型的问题我们整卷都在讨论：data race、死锁、条件变量的虚假唤醒、对象生命周期——每一项都是一个坑。

Actor 模型走了一条完全不同的路：**不共享任何状态**。每个 Actor 有自己独立的内存空间，外部唯一能影响它的方式就是发消息。这意味着不存在 data race（因为没有共享的可变状态），不需要锁（因为没有需要互斥访问的资源），而且天然适合分布式部署——消息传递是位置无关的，发送方不需要知道接收方是在同一台机器还是地球另一端。

但天下没有免费的午餐。Actor 模型引入了新的复杂性：消息的顺序性、消息丢失的处理、Actor 的错误传播和恢复。这些我们接下来都会逐一讨论。

### 消息传递的语义

消息传递有几种不同的送达保证语义，这是分布式系统和 Actor 模型里非常重要的一个概念维度。最轻量的是 **at-most-once（最多一次）**：消息可能丢失，但不会被重复投递，Actor 模型的原始定义就是这种——消息被发出后不保证一定到达，就像寄明信片，你扔进邮筒后就没法确认对方一定收到了。比它强一档的是 **at-least-once（至少一次）**：消息不会丢失，但可能被重复投递，发送方会不断重试直到收到确认，但网络分区或超时可能导致同一条消息被处理两次，接收方需要自己实现幂等性来应对。最强的是 **exactly-once（恰好一次）**：消息既不会丢失也不会重复，但实现代价也最高——通常需要分布式事务或幂等性加去重机制。

原始的 Actor 模型使用 at-most-once 语义。在 Erlang 的实现中，消息传递也是"best effort"——不保证送达，发送方没有办法确认消息是否到达。这种选择是有意的：更强的送达保证意味着更多的同步和更高的延迟，而 Actor 模型追求的是高并发和高吞吐。

> ⚠️ **注意**：虽然原始 Actor 模型不保证消息顺序，但很多实际实现（包括 Erlang）在同一对 Actor 之间是保证消息顺序的——即 Actor A 发给 Actor B 的两条消息 M1 和 M2，B 收到时一定是 M1 先于 M2。但不同发送方发给同一个 Actor 的消息之间没有顺序保证。

## 用 C++ 实现简易 Actor

理论讲够了，接下来我们来干活。下面我们要实现一个最小的 Actor 框架，核心组件一共四个：用 `std::variant` 实现的类型安全消息（`Message`）、基于线程安全队列的消息邮箱（`Mailbox`）、消息循环加消息分发的 Actor 基类、以及管理所有 Actor 生命周期和寻址的 `ActorSystem`。

### 从消息类型开始

我们用 `std::variant` 来实现消息类型。为什么不搞一个基于继承的 `Message` 基类？因为 `std::variant` 天然支持 visitation 模式，配合 `std::overload` 可以实现优雅的模式匹配——这在 Actor 的消息处理中非常有用。

先定义一些我们后面要用到的消息类型：

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

你可能觉得这样定义消息有点"硬编码"的感觉——确实如此。在真实的 Actor 框架里（比如 CAF），消息类型是通过模板和类型擦除来实现的，可以支持任意类型的消息。但我们的目标是理解核心机制，不是造轮子，所以用 `std::variant` 就够了。它的好处是类型安全、零堆分配（只要消息本身不大），而且编译期能检查你是否处理了所有消息类型。

> ⚠️ **注意**：`std::variant` 的大小等于最大成员的大小加上一个 discriminant（用于记录当前活跃的备选类型索引），主流实现通常用 1-4 字节的小整数来存储它（备选类型不超过 255 个时只需 1 字节），而且由于对齐的原因，discriminant 经常能塞进成员存储的对齐填充里，不额外占空间。但如果你往里面放了一个很大的类型，所有消息都会占用那个大小。所以消息设计的原则是：**小且轻**。需要传大量数据时，传指针或引用计数对象（比如 `std::shared_ptr<std::vector<T>>`）而不是直接拷贝数据。

### 邮箱：线程安全队列

邮箱就是一个线程安全队列。如果你跟到了 ch04 的并发数据结构，那你已经见过这个模式了。这里我们给出一个简洁的实现：

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

这个实现在 ch04 应该已经很熟悉了。唯一的新东西是 `close()` 方法——当 Actor 停止时，我们需要关闭它的邮箱，让消息循环退出。`wait_and_pop()` 在队列关闭且为空时返回 `std::nullopt`，消息循环据此判断是时候退出了。

### Actor 核心：消息循环

现在来实现 Actor 的核心——消息循环。每个 Actor 拥有一个邮箱（`ThreadSafeQueue<Message>`），一个消息循环线程，以及一个由子类覆写的消息处理函数。

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

消息循环是整个 Actor 的心脏。它的工作就是反复从邮箱里取消息，然后交给 `on_message()` 处理。如果邮箱关闭了或者 `on_message()` 返回 `false`，循环就结束了。

这里有一个设计决策值得说一说：当 `on_message()` 抛出异常时，我们没有让它杀死整个 Actor，而是 catch 住了继续运行。这看似违反了 Erlang 的 "let it crash" 哲学，但在我们这个简易框架里，每个 Actor 都是一个独立的线程，让它 crash 意味着线程直接退出——这时候就需要 supervisor 来重启它。我们后面会实现 supervisor，这里先保守一点。

### ActorSystem：管理一切

ActorSystem 负责 Actor 的创建、寻址和生命周期管理。它本身不是 Actor，而是一个管理容器。

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

`ActorSystem` 的设计比较直接：它用 `unordered_map` 维护 ID 到 Actor 指针的映射，同时用 `vector<unique_ptr>` 持有所有权。`spawn` 是一个模板方法，可以创建任意类型的 Actor。`tell` 是一个间接发送消息的方法——在真实框架里，这通常是通过 ActorRef（一个轻量的 Actor 引用对象）来做的，而不是直接持有指针。

> ⚠️ **注意**：这个实现用裸指针（`Actor*`）存在映射里。这是为了简化代码，但在多线程环境下有悬挂指针的风险——如果一个 Actor 被 stop 了但另一个线程还持有它的指针。生产级框架会用 `weak_ptr` 或 ActorRef（一种不可伪造的地址令牌）来解决这个问题。

### 消息的模式匹配

`std::variant` 配合 `std::visit` 可以实现模式匹配风格的消息处理。C++17 没有语言级的 pattern matching，但我们可以用一个辅助工具来让代码更清晰：

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

有了这个工具，消息处理就可以写成这样：

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

这种写法的优雅之处在于：`std::visit` 要求 visitor 能处理 variant 的所有备选类型——如果你移除末尾的 `[](const auto&)` 通配处理器，少写一个类型的处理，编译器就会直接报错。这比 switch-case 安全得多，因为 switch-case 忘了一个 case 只是一个 warning，而 `std::visit` 的类型不完整是 hard error。当然，一旦你加了 `auto` 通配处理器，它会把所有未匹配的类型都兜住，编译器就不会再提醒了——所以通配处理器是一把双刃剑，方便是方便，但也意味着你可能悄悄忽略了某些消息。

## 实战：分布式计数器

现在我们把前面的零件组装起来，实现一个简单的分布式计数器。场景是这样的：我们有多个 Counter Actor，每个 Actor 维护自己的局部计数器；同时有一个 Aggregator Actor，定期向所有 Counter Actor 查询它们的计数，然后汇总输出。

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

### 组装运行

现在把所有零件组装起来，看看效果：

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

这个例子展示了 Actor 模型的典型交互模式：消息驱动、无共享状态、异步通信。Counter Actor 只关心自己的计数，Aggregator Actor 只关心汇总——它们之间没有共享变量，没有锁，完全通过消息来协调。虽然这个例子很简单，但你可以想象把它扩展到分布式的场景：Counter 在不同的机器上，Aggregator 通过网络收发消息——代码结构几乎不需要改变。

## 错误传递与 Supervisor 策略

Actor 模型里最让人眼前一亮的创新之一，就是 Erlang 的 **supervisor** 机制和 **"let it crash"** 哲学。这个想法反直觉但极其实用：与其在每个 Actor 里写大量的防御性错误处理代码，不如让 Actor 只写"happy path"，出错就 crash，然后由它的 supervisor 来决定怎么恢复。

### Erlang 的 Let It Crash

在 Erlang 里，每个进程（Actor）都有一个 supervisor。当一个进程崩溃时，supervisor 会收到通知，然后按照预设的策略来处理。最常用的是 **one_for_one**——只重启崩溃的那个进程，其他兄弟进程不受影响。如果进程之间有紧密依赖，可以用 **one_for_all**：一个进程崩溃，所有兄弟进程都被终止然后一起重启。还有一种折中的 **rest_for_one**——崩溃的进程和它之后启动的所有兄弟进程一起重启，适合有先后依赖关系的进程链。

supervisor 本身也是 Actor，所以 supervisor 也可以有自己的 supervisor——形成一棵 supervisor 树。如果某一级的 supervisor 自己也崩了（比如重启频率太高），它的父 supervisor 会接管。这种层级化的容错机制是 Erlang 实现超高可用性的关键。

### 用 C++ 实现 Supervisor

下面是一个简化的 supervisor 实现。当子 Actor 抛出异常时，supervisor 捕获异常，根据策略决定是重启还是停止。

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

这个 supervisor 实现是高度简化的，但它传达了核心思想。关键在于 `handle_error` 方法：收到错误消息后，根据策略决定是重启还是停止。重启的核心是"用工厂重新创建"——这就是为什么 supervisor 需要持有子 Actor 的工厂而不是 Actor 本身。Actor 崩溃后可能处于不确定的状态，直接"修复"它是不安全的；相反，直接销毁旧的、创建全新的，才是干净的做法。

> ⚠️ **注意**：这里的 `restart_child` 有一个简化缺陷——重启后需要更新所有引用旧 Actor ID 的地方。在真实框架里，ActorRef 是一个间接层，重启后 ActorRef 指向新 Actor，发送方无需感知。我们这里省略了这个间接层以保持代码可读性。

## Actor 模型的优势与局限

到这一步我们对 Actor 模型有了相当完整的认知，从理论到实现到错误处理。在进入下一篇 CSP 之前，让我们冷静地评估一下这个模型的优缺点。

先说优势。首先是概念上的简洁——没有共享状态意味着你不需要操心锁的粒度和顺序，每个 Actor 只需要关心自己的状态和收到的消息。其次是天然适合分布式——消息传递是位置无关的，一个系统从单机扩展到集群只需要换一个消息传输层。再者是容错能力——supervisor 树 + let it crash 的组合在工程实践中被证明是非常有效的，尤其是在需要高可用性的系统里。

再说局限。性能是一个永恒的话题——消息传递意味着数据拷贝（至少在逻辑上），比直接读写共享内存慢，虽然可以用共享指针来避免深拷贝，但共享指针本身就是一种"共享状态"，又绕回来了。消息顺序性也是一个坑——虽然同一对 Actor 之间通常保证顺序，但多个 Actor 之间的消息交错是没有确定性保证的，这让调试变得困难。最后，Actor 模型天然不适合细粒度的并行计算——你不能用 Actor 来并行处理一个数组的每个元素，因为创建 Actor 的开销远大于计算本身。

## 我们的位置

这一篇我们从 Actor 模型的历史和理论开始，理解了它的核心思想——用消息传递替代共享内存，用独立计算实体替代共享状态线程。然后我们用 C++ 实现了一个最小的 Actor 框架，包括类型安全的消息（`std::variant`）、线程安全邮箱、Actor 基类、ActorSystem、以及 supervisor 错误处理。最后用一个分布式计数器把所有零件串了起来。

但 Actor 模型只是"不共享内存"这条路的一个分支。下一篇我们要看另一个同样有着深厚理论基础的流派——CSP（Communicating Sequential Processes），由 Tony Hoare 在 1978 年提出，Go 语言的 goroutine + channel 就是它的经典实现。Actor 有身份和邮箱，CSP 的 channel 是匿名的——这个区别看起来微妙，但在实际编程风格上会产生很大的差异。

> 💡 完整示例代码在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，访问 `code/volumn_codes/vol5/ch07-actor-channel/`。

## 参考资源

- [Actor model — Wikipedia](https://en.wikipedia.org/wiki/Actor_model) — Actor 模型的完整历史和理论介绍
- [Hewitt, Bishop, Steiger (1973). "A Universal Modular ACTOR Formalism for Artificial Intelligence"](https://worrydream.com/refs/Hewitt_1973_-_A_Universal_Modular_Actor_Formalism_for_Artificial_Intelligence.pdf) — Actor 模型的原始论文（IJCAI 1973）
- [Erlang OTP Design Principles — Supervisor Behaviour](https://www.erlang.org/doc/design_principles/sup_princ) — Erlang supervisor 的官方文档
- [C++ Actor Framework (CAF)](https://actor-framework.org/) — C++ 最成熟的 Actor 框架实现
- [SObjectizer](https://github.com/Stiffstream/sobjectizer) — 另一个活跃的 C++ Actor 框架
- [Akka Documentation](https://doc.akka.io/) — JVM 上最知名的 Actor 框架，文档对 Actor 模型的概念解释非常清晰
