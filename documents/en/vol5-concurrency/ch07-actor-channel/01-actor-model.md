---
title: "Actor Model and Message Passing"
description: "Understand the core idea of the Actor model—replacing shared memory with message passing—and build a minimal C++ Actor framework"
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
  - "Thread-Safe Queue"
  - "Thread Pool Design"
related:
  - "Channels and the CSP Model"
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/01-actor-model.md
  source_hash: 87462fddcd7f11ecb83365fbc1d45241ddcd2e0bce39abaee1fcec8855c28c73
  translated_at: '2026-09-26T07:30:40+00:00'
  engine: anthropic
  token_count: 5010
---

# Actor Model and Message Passing

Up to this point, every concurrency model we have used in this volume has been essentially the same thing: shared memory plus locks. Whether it is a mutex, a condition_variable, or an atomic, the underlying logic is always "multiple threads see the same memory, and synchronization primitives keep their accesses from colliding". This model works, but honestly, as the system scales up it gets harder and harder to manage: how to choose lock granularity, how to fix the lock acquisition order, how to prevent deadlocks—each one demands engineer judgment, and human judgment is the least reliable thing there is.

But there is one school that rejects this path at the root. The Actor model says: stop sharing memory. Every message is a copy, every computational entity is independent, and they communicate exclusively through asynchronous messages. No shared state means no race conditions, which means no locks. The idea sounds a bit utopian, but it has been validated at industrial scale in Erlang/Akka—the telecom systems Ericsson built with Erlang/OTP are credited with nine nines (99.9999999%) of availability, and while how that number was measured is open to debate, Erlang's standing in the high-availability world is rock solid.

In this article we take a deep dive into the Actor model: its theoretical foundations, its core concepts, and a C++ implementation. We are not going to build a production-grade Actor framework (that is CAF's or SObjectizer's job), but we will build a minimal framework complete enough to tie together all the core ideas of the Actor model.

## Environment Setup

All code in this article is based on C++17. My build environment is GCC 12+ / Clang 15+ / MSVC 2022+ with the flags `-std=c++17 -pthread -O2`; it runs on Linux / macOS / Windows alike (as long as your standard library implements `<thread>`, `<mutex>`, and `<condition_variable>`). The code depends on no third-party libraries and uses only standard library components—copy it verbatim and it compiles.

## Origins and Core Ideas of the Actor Model

The Actor model was first proposed by Carl Hewitt, Peter Bishop, and Richard Steiger in their 1973 paper *"A Universal Modular ACTOR Formalism for Artificial Intelligence"*. It was born out of AI research at MIT, motivated by the vision of "looking forward to parallel computing architectures composed of tens, hundreds, or even thousands of independent microprocessors". That prediction looks remarkably accurate today—multicore processors and distributed systems have become the mainstream.

> If you are curious about the original paper, you can find it in the IJCAI 1973 conference proceedings. Although the original paper's context is AI research, the Actor model itself is a general-purpose model of concurrent computation.

The core claim of the Actor model is **"everything is an Actor"**, which rhymes with "everything is an object" in object-oriented programming. Each Actor is an independent computational entity with its own private state, inaccessible from the outside. So how do Actors interact? There is exactly one way: send messages.

By Hewitt's definition, upon receiving a message an Actor can do three things at once: send a finite number of messages to other Actors (only to Actors whose addresses it knows), create a finite number of new Actors (created dynamically, with no limit on the total), and decide the behavior it will use when processing the next message—in other words, an Actor's behavior can change over time. There is no ordering requirement among these three actions; they can happen concurrently. This is the fundamental difference in mindset between the Actor model and the traditional "sequential execution + shared memory" model: an Actor is a concurrent entity by nature, and sequential execution is merely a special degenerate case of it.

### The Fundamental Difference from the Shared Memory Model

In the shared memory model, multiple threads exchange information by reading and writing the same memory, then use locks, atomic operations, and similar mechanisms to keep things consistent. The problems of this model are what we have been discussing all volume long: data races, deadlocks, spurious wakeups on condition variables, object lifetimes—each one a pit of its own.

The Actor model takes a completely different road: **share no state at all**. Each Actor has its own independent memory space, and the only way the outside world can affect it is by sending messages. That means no data races (there is no shared mutable state), no locks (there is no resource that needs mutually exclusive access), and a natural fit for distributed deployment—message passing is location-independent, and the sender does not need to know whether the receiver is on the same machine or on the other side of the planet.

But there is no free lunch. The Actor model introduces its own new complexities: message ordering, handling lost messages, and error propagation and recovery across Actors. We will take each of these up in turn.

### Semantics of Message Delivery

Message passing comes with several delivery-guarantee semantics, an important conceptual axis in distributed systems and in the Actor model. The lightest is **at-most-once**: a message may be lost, but it will never be delivered twice—the original definition of the Actor model is exactly this. Once a message is sent, its arrival is not guaranteed; it is like mailing a postcard—once it is in the mailbox, you have no way to confirm the recipient actually got it. One level stronger is **at-least-once**: messages are not lost, but they may be delivered more than once; the sender keeps retrying until it receives an acknowledgment, though a network partition or a timeout can cause the same message to be processed twice, so the receiver must implement idempotency to cope. The strongest is **exactly-once**: a message is neither lost nor duplicated, but it is also the most expensive to implement—typically requiring distributed transactions, or idempotency plus deduplication.

The original Actor model uses at-most-once semantics. In Erlang's implementation, message passing is likewise "best effort"—delivery is not guaranteed, and the sender has no way to confirm whether a message arrived. The choice is deliberate: stronger delivery guarantees mean more synchronization and higher latency, while the Actor model chases high concurrency and high throughput.

> ⚠️ **Note**: although the original Actor model does not guarantee message ordering, many real implementations (Erlang included) do guarantee ordering between a given pair of Actors—if Actor A sends messages M1 and M2 to Actor B, B always receives M1 before M2. But there is no ordering guarantee among messages that different senders deliver to the same Actor.

## Implementing a Minimal Actor in C++

Enough theory—time to build. We are going to implement a minimal Actor framework with four core components: a type-safe message built on `std::variant` (`Message`), a message mailbox based on a thread-safe queue (`Mailbox`), an Actor base class with a message loop and message dispatch, and an `ActorSystem` that manages the lifecycle and addressing of all Actors.

### Starting with the Message Types

We implement the message type with `std::variant`. Why not an inheritance-based `Message` base class? Because `std::variant` natively supports the visitation pattern, and paired with `std::overload` it gives us elegant pattern matching—which is extremely useful in an Actor's message handling.

First, some message types we will use later:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <optional>
#include <functional>

// Forward declaration
class Actor;

// Uniquely identifies an Actor
using ActorId = uint64_t;

// ===== Message definitions =====

/// A plain string message
struct StringMessage {
    std::string content;
};

/// Request to increment the counter
struct IncrementMessage {
    int64_t delta{1};
};

/// Query the current counter value
struct QueryMessage {};

/// Reply to a query
struct QueryResponse {
    int64_t value{0};
    ActorId requester{0};  // who to send the reply back to
};

/// Request to spawn a child Actor
struct SpawnRequest {
    std::string actor_type;
};

/// Stop a specific Actor
struct StopMessage {
    ActorId target{0};
};

/// Error report—sent to the supervisor when an Actor hits an internal exception
struct ErrorMessage {
    ActorId failed_actor{0};
    std::string error_description;
};

/// The union of all message types
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

You may feel this way of defining messages is a bit "hard-coded"—it is. In a real Actor framework (CAF, for instance), message types are implemented with templates and type erasure and can carry arbitrary message types. But our goal is to understand the core machinery, not to reinvent the wheel, so `std::variant` is enough. Its virtues are type safety, zero heap allocation (as long as each message itself is small), and a compile-time check that you have handled every message type.

> ⚠️ **Note**: the size of a `std::variant` equals the size of its largest member plus a discriminant (which records the index of the currently active alternative). Mainstream implementations usually store it as a small 1-4 byte integer (a single byte suffices when there are no more than 255 alternatives), and because of alignment the discriminant often fits into the padding of the members' storage, costing no extra space. But if you put one very large type inside, every message pays that size. Hence the design rule for messages: **small and light**. When you need to move a lot of data, pass a pointer or a reference-counted object (for example `std::shared_ptr<std::vector<T>>`) instead of copying the data itself.

### The Mailbox: a Thread-Safe Queue

The mailbox is simply a thread-safe queue. If you have kept up with the concurrent data structures in ch04, you have already seen this pattern. Here is a lean implementation:

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

    // No copying
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    /// Enqueue (blocking)
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    /// Dequeue (blocks until data is available or the queue is closed)
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

    /// Try to dequeue (non-blocking)
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

    /// Close the queue and wake up all waiting threads
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

This implementation should be old hat from ch04. The only new thing is the `close()` method—when an Actor stops, we need to close its mailbox so the message loop can exit. `wait_and_pop()` returns `std::nullopt` when the queue is both closed and empty, and the message loop takes that as its cue to leave.

### The Actor Core: the Message Loop

Now for the heart of the Actor: the message loop. Each Actor owns a mailbox (a `ThreadSafeQueue<Message>`), a thread running the message loop, and a message-handling function that subclasses override.

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

    // No copying or moving
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    /// Start the Actor's message loop
    void start()
    {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread(&Actor::message_loop, this);
    }

    /// Stop the Actor
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

    /// Send a message to this Actor
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
    /// Subclasses must implement this: handle one message
    /// Return true to keep running, false to stop
    virtual bool on_message(const Message& msg) = 0;

    /// Optional for subclasses: initialization before the Actor starts
    virtual void on_start() {}

    /// Optional for subclasses: cleanup when the Actor stops
    virtual void on_stop() {}

private:
    /// The message loop—the heart of the Actor
    void message_loop()
    {
        on_start();

        while (running_.load(std::memory_order_acquire)) {
            auto msg = mailbox_.wait_and_pop();
            if (!msg.has_value()) {
                // Mailbox closed: leave the loop
                break;
            }

            try {
                bool should_continue = on_message(msg.value());
                if (!should_continue) {
                    break;
                }
            }
            catch (const std::exception& e) {
                // An exception does not kill the message loop; just print a warning
                // A real framework would report the exception to the supervisor
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

The message loop is the heart of the whole Actor. Its job is to fetch messages from the mailbox over and over and hand each one to `on_message()`. If the mailbox closes, or `on_message()` returns `false`, the loop ends.

One design decision here deserves a word: when `on_message()` throws, we do not let it kill the whole Actor—we catch the exception and keep running. That looks like a betrayal of Erlang's "let it crash" philosophy, but in this minimal framework every Actor is its own thread, and letting it crash means the thread simply exits—at which point you need a supervisor to restart it. We will implement a supervisor later on; for now we play it conservative.

### ActorSystem: Managing Everything

The ActorSystem is responsible for creating Actors, addressing them, and managing their lifecycles. It is not an Actor itself—it is a management container.

```cpp
#pragma once

#include "message_types.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <iostream>

// Forward declaration
class Actor;

/// Signature of an Actor factory function
using ActorFactory = std::function<std::unique_ptr<Actor>(ActorId)>;

class ActorSystem {
public:
    ActorSystem() = default;

    ~ActorSystem()
    {
        shutdown();
    }

    // No copying
    ActorSystem(const ActorSystem&) = delete;
    ActorSystem& operator=(const ActorSystem&) = delete;

    /// Register an Actor factory (by type name)
    void register_factory(const std::string& type_name, ActorFactory factory)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        factories_[type_name] = std::move(factory);
    }

    /// Create and start an Actor
    template <typename ActorType, typename... Args>
    ActorId spawn(Args&&... args)
    {
        auto actor = std::make_unique<ActorType>(
            next_id(), std::forward<Args>(args)...
        );
        ActorId id = actor->id();
        Actor* raw_ptr = actor.get();  // cache the raw pointer before the move

        {
            std::lock_guard<std::mutex> lock(mutex_);
            actors_[id] = raw_ptr;
            owned_actors_.push_back(std::move(actor));
        }

        raw_ptr->start();  // use the cached pointer instead of looking up the map again
        return id;
    }

    /// Create an Actor through a factory
    ActorId spawn_from_factory(const std::string& type_name)
    {
        auto it = factories_.find(type_name);
        if (it == factories_.end()) {
            std::cerr << "[ActorSystem] 未知 Actor 类型: "
                      << type_name << "\n";
            return 0;  // invalid ID
        }

        auto actor = it->second(next_id());
        ActorId id = actor->id();
        Actor* raw_ptr = actor.get();  // cache the raw pointer before the move

        {
            std::lock_guard<std::mutex> lock(mutex_);
            actors_[id] = raw_ptr;
            owned_actors_.push_back(std::move(actor));
        }

        raw_ptr->start();  // use the cached pointer instead of looking up the map again
        return id;
    }

    /// Send a message to a specific Actor
    void tell(ActorId target, Message msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(target);
        if (it != actors_.end()) {
            it->second->tell(std::move(msg));
        }
    }

    /// Look up an Actor
    Actor* find(ActorId id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(id);
        return it != actors_.end() ? it->second : nullptr;
    }

    /// Stop a specific Actor
    void stop(ActorId id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = actors_.find(id);
        if (it != actors_.end()) {
            it->second->stop();
            actors_.erase(it);
        }
    }

    /// Shut down the whole system
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

The design of `ActorSystem` is straightforward: it keeps an `unordered_map` from IDs to Actor pointers, while a `vector<unique_ptr>` holds ownership. `spawn` is a template method that can create an Actor of any type. `tell` is an indirect way to send a message—in a real framework this usually goes through an ActorRef (a lightweight Actor reference object) rather than a directly held pointer.

> ⚠️ **Note**: this implementation stores raw pointers (`Actor*`) in the map. That keeps the code simple, but in a multithreaded environment it risks dangling pointers—an Actor gets stopped while another thread still holds its pointer. Production-grade frameworks solve this with `weak_ptr` or an ActorRef (an unforgeable address token).

### Pattern Matching on Messages

`std::variant` together with `std::visit` gives us pattern-matching-style message handling. C++17 has no language-level pattern matching, but a small helper makes the code much cleaner:

```cpp
// Helper: an overload set over a group of callable objects
template <typename... Handlers>
struct overload : Handlers... {
    using Handlers::operator()...;
};

// Deduction guide
template <typename... Handlers>
overload(Handlers...) -> overload<Handlers...>;
```

With this helper in hand, message handling can look like this:

```cpp
bool on_message(const Message& msg) override
{
    return std::visit(overload{
        [this](const IncrementMessage& m) {
            counter_ += m.delta;
            return true;
        },
        [this](const QueryMessage& m) {
            // Send the reply back to the requester
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
        // More message types can be added here
        [](const auto&) {
            // Unknown message: ignore
            return true;
        }
    }, msg);
}
```

The elegance of this style: `std::visit` requires the visitor to handle every alternative of the variant—remove the trailing `[](const auto&)` catch-all and forget one type's handler, and the compiler errors out on the spot. That is far safer than switch-case, where a missing case is only a warning, while an incomplete set of types for `std::visit` is a hard error. Of course, once you add the `auto` catch-all, it swallows every unmatched type and the compiler stays silent again—so the catch-all is a double-edged sword: convenient, yes, but it also means you may be quietly ignoring certain messages.

## Hands-On: a Distributed Counter

Now let's bolt the parts together and build a simple distributed counter. The scenario: several Counter Actors, each maintaining its own local count; plus one Aggregator Actor that periodically queries every Counter for its value and prints the total.

### Counter Actor

```cpp
#include "actor.hpp"
#include "actor_system.hpp"

/// Counter Actor: maintains a local counter
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
                // Report the current value to the aggregator
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
    friend class AggregatorActor;  // let the aggregator set its ID

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

/// Aggregator Actor: collects every Counter's value and totals them
class AggregatorActor : public Actor {
public:
    AggregatorActor(ActorId id, ActorSystem* system)
        : Actor(id), system_(system)
    {
    }

    /// Register a Counter Actor
    void register_counter(ActorId counter_id)
    {
        counter_ids_.push_back(counter_id);

        // Tell the Counter who we are so it can reply to queries
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
                // Collect a report from one of the Counters
                collected_[resp.requester] = resp.value;
                received_++;

                // Once every Counter has reported, total and print
                if (received_ >= counter_ids_.size()) {
                    int64_t total = 0;
                    for (auto& [id, val] : collected_) {
                        total += val;
                    }
                    std::cout << "[Aggregator] 汇总: 总计 = "
                              << total << " (来自 "
                              << received_ << " 个 Counter)\n";

                    // Reset and wait for the next round
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

### Assembling and Running It

Now assemble all the parts and see what happens:

```cpp
#include "counter_actor.hpp"
#include "aggregator_actor.hpp"
#include "actor_system.hpp"

#include <thread>
#include <chrono>

int main()
{
    ActorSystem system;

    // Create the Aggregator
    auto agg_id = system.spawn<AggregatorActor>(&system);

    // Create 3 Counters
    auto c1 = system.spawn<CounterActor>(&system);
    auto c2 = system.spawn<CounterActor>(&system);
    auto c3 = system.spawn<CounterActor>(&system);

    // Register them with the Aggregator
    auto* agg = dynamic_cast<AggregatorActor*>(system.find(agg_id));
    agg->register_counter(c1);
    agg->register_counter(c2);
    agg->register_counter(c3);

    // Send the Counters some increment messages
    for (int i = 0; i < 5; ++i) {
        system.tell(c1, IncrementMessage{2});
        system.tell(c2, IncrementMessage{3});
        system.tell(c3, IncrementMessage{1});
    }

    // Wait a moment for the messages to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Fire the queries: each Counter replies to the Aggregator upon receiving QueryMessage
    system.tell(c1, QueryMessage{});
    system.tell(c2, QueryMessage{});
    system.tell(c3, QueryMessage{});

    // Wait for the aggregation to finish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Expected output:
    // [Aggregator] 启动，监控 3 个 Counter
    // [Aggregator] 汇总: 总计 = 30 (来自 3 个 Counter)
    //   (c1 = 10, c2 = 15, c3 = 5)

    system.shutdown();
    return 0;
}
```

This example shows the typical interaction pattern of the Actor model: message-driven, no shared state, asynchronous communication. The Counter Actors care only about their own counts; the Aggregator Actor cares only about totaling them—there are no shared variables between them, no locks, and all coordination flows through messages. The example is simple, but you can easily imagine extending it to a distributed setting: Counters on different machines, the Aggregator sending and receiving messages over the network—the code structure would barely need to change.

## Error Propagation and Supervisor Strategies

One of the most eye-opening innovations in the Actor model is Erlang's **supervisor** mechanism and its **"let it crash"** philosophy. The idea is counterintuitive but extremely practical: instead of writing piles of defensive error-handling code inside every Actor, let each Actor contain only its "happy path"—if something goes wrong, crash, and let the supervisor decide how to recover.

### Erlang's Let It Crash

In Erlang, every process (Actor) has a supervisor. When a process crashes, the supervisor is notified and reacts according to a preset strategy. The most common is **one_for_one**—only the crashed process is restarted, and the sibling processes are untouched. If the processes depend tightly on one another, **one_for_all** fits: one process crashes, all siblings are terminated and then restarted together. And a middle ground, **rest_for_one**—the crashed process and every sibling started after it are restarted together, suited to chains of processes with sequential dependencies.

A supervisor is itself an Actor, so a supervisor can have its own supervisor—forming a supervisor tree. If a supervisor at some level crashes itself (because the restart frequency is too high, say), its parent supervisor takes over. This hierarchical fault-tolerance mechanism is the key to Erlang's ultra-high availability.

### Implementing a Supervisor in C++

Below is a simplified supervisor implementation. When a child Actor throws an exception, the supervisor catches it and, per its strategy, decides whether to restart or stop.

```cpp
/// Supervisor strategy
enum class SupervisorStrategy {
    kRestart,   // restart the child Actor
    kStop,      // stop the child Actor
    kEscalate   // escalate to a higher-level supervisor
};

/// Supervisor configuration
struct SupervisorConfig {
    SupervisorStrategy strategy{SupervisorStrategy::kRestart};
    int max_restarts{3};          // maximum restarts within the time window
    int restart_window_seconds{60}; // the time window (seconds)
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

    /// Register a child Actor's factory (used for restarts)
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
                // In a real system, this should send a message to our own supervisor
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

        // Stop the old one
        system_->stop(child_id);

        // Create a new one with the factory
        auto new_actor = it->second(++next_child_id_);
        ActorId new_id = new_actor->id();

        std::cout << "[Supervisor] 重启 Actor: "
                  << child_id << " -> " << new_id << "\n";

        // Update the registry (simplified)
        children_.erase(child_id);
        // The new Actor reuses the old factory
        // In practice it needs to be re-registered...
        // Simplified here; the re-registration details are omitted
    }

    ActorSystem* system_;
    SupervisorConfig config_;
    std::mutex children_mutex_;
    std::unordered_map<ActorId, ActorFactory> children_;
    ActorId next_child_id_{1000};  // child Actor ID counter
};
```

This supervisor implementation is heavily simplified, but it carries the core idea. The key is the `handle_error` method: upon receiving an error message, it decides between restart and stop per the strategy. The essence of restarting is "re-create from the factory"—which is exactly why the supervisor must hold the child Actor's factory rather than the Actor itself. After a crash an Actor may be in an indeterminate state, and "repairing" it in place is unsafe; on the contrary, destroying the old one outright and creating a brand-new one is the clean move.

> ⚠️ **Note**: `restart_child` here has a simplification flaw—after a restart, everything that references the old Actor ID needs updating. In a real framework, the ActorRef is a layer of indirection: after the restart the ActorRef points at the new Actor, and senders never notice. We omitted that indirection layer to keep the code readable.

## Strengths and Limits of the Actor Model

At this point we have a fairly complete picture of the Actor model—theory, implementation, error handling. Before moving on to CSP in the next article, let's soberly weigh this model's pros and cons.

Strengths first. There is conceptual simplicity—no shared state means you never worry about lock granularity and ordering; each Actor minds only its own state and the messages it receives. There is the natural fit for distribution—message passing is location-independent, so scaling a system from a single machine to a cluster is just a swap of the message transport layer. And there is fault tolerance—the combination of supervisor trees and let-it-crash has proven very effective in engineering practice, especially in systems that demand high availability.

Now the limits. Performance is the eternal topic—message passing means copying data (logically, at least), which is slower than directly reading and writing shared memory; shared pointers can avoid the deep copy, but a shared pointer is itself a form of "shared state", so you loop right back to where you started. Message ordering is another pitfall—although ordering between a given pair of Actors is usually guaranteed, the interleaving of messages among multiple Actors has no determinism guarantee, which makes debugging difficult. Finally, the Actor model is inherently unsuited to fine-grained parallel computation—you would not use Actors to process every element of an array in parallel, because creating an Actor costs far more than the computation itself.

## Where We Are

In this article we started from the Actor model's history and theory and grasped its core idea—replace shared memory with message passing, and replace shared-state threads with independent computational entities. Then we built a minimal Actor framework in C++, covering type-safe messages (`std::variant`), a thread-safe mailbox, an Actor base class, an ActorSystem, and supervisor-based error handling. Finally we wired all the parts together with a distributed counter.

But the Actor model is only one branch of the "no shared memory" road. In the next article we look at another school with equally deep theoretical roots—CSP (Communicating Sequential Processes), proposed by Tony Hoare in 1978; Go's goroutines and channels are its classic implementation. Actors have identity and mailboxes; CSP's channels are anonymous—a difference that looks subtle, but it produces big divergences in actual programming style.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), under `code/volumn_codes/vol5/ch07-actor-channel/`.

## Reference Resources

- [Actor model — Wikipedia](https://en.wikipedia.org/wiki/Actor_model) — A complete history and theoretical introduction to the Actor model
- [Hewitt, Bishop, Steiger (1973). "A Universal Modular ACTOR Formalism for Artificial Intelligence"](https://worrydream.com/refs/Hewitt_1973_-_A_Universal_Modular_Actor_Formalism_for_Artificial_Intelligence.pdf) — The original Actor model paper (IJCAI 1973)
- [Erlang OTP Design Principles — Supervisor Behaviour](https://www.erlang.org/doc/design_principles/sup_princ) — Official documentation for the Erlang supervisor
- [C++ Actor Framework (CAF)](https://actor-framework.org/) — The most mature Actor framework implementation in C++
- [SObjectizer](https://github.com/Stiffstream/sobjectizer) — Another active C++ Actor framework
- [Akka Documentation](https://doc.akka.io/) — The best-known Actor framework on the JVM; its documentation explains Actor model concepts with exceptional clarity
