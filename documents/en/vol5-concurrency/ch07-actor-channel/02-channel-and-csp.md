---
chapter: 7
cpp_standard:
- 17
- 20
description: 'Understand the CSP (Communicating Sequential Processes) concurrency model and implement Go-like channel communication pipelines in C++'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Actor Model and Message Passing'
- 'Thread-Safe Queue'
reading_time_minutes: 24
related:
- 'Hands-on: Coroutine Echo Server'
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 进阶
title: 'Channels and the CSP Model'
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/02-channel-and-csp.md
  source_hash: c362874de4f213c18c18aa225c44615d9709dd414f3db5c1f0a5dbf1323fef42
  translated_at: '2026-09-26T07:59:44+00:00'
  engine: anthropic
  token_count: 6800
---
# Channels and the CSP Model

In the previous article we talked about the Actor model—organizing concurrency with identifiable Actors plus asynchronous message passing. In this article we meet another school that makes the same "don't share memory" bet: CSP (Communicating Sequential Processes).

CSP was first proposed by Tony Hoare in his 1978 paper *"Communicating Sequential Processes"* (published in Communications of the ACM). Like the Actor model, CSP's core idea is to replace shared memory with message passing, but it takes a different route: Actors have identity and mailboxes, and messages are sent to a specific Actor's address; CSP communicates through anonymous channels, and processes never need to know who is on the other end. The difference looks subtle, but it produces big differences in programming style and expressive power. Go's goroutine + channel is CSP's most successful industrial practice, and Rob Pike's famous line—"Don't communicate by sharing memory; share memory by communicating"—is Go's summary of the CSP philosophy.

In this article we start from CSP's theoretical foundations, then implement a Go-like channel communication pipeline in C++—covering buffered and unbuffered channels, close semantics, and the select pattern—and finish by discussing when to use a channel and when to reach for a lock directly.

## Environment Notes

As in the previous article, all of our code is based on C++17, compiles under GCC 12+ / Clang 15+ / MSVC 2022+, and is built with `-std=c++17 -pthread -O2`. Linux, macOS, and Windows all work, as long as your standard library supports `<thread>`, `<mutex>`, and `<condition_variable>`. Nothing in this article depends on any third-party library.

## The Theoretical Foundations of CSP

CSP's original paper came five years after the Actor model (1978 vs 1973), but its influence is just as deep. Hoare's original design was a concurrent programming language (not the formal calculus it later became), and its syntax looked like this:

```text
COPY = *[c:character; west?c -> east!c]
```

This code means: repeatedly receive a character `c` from the process named `west`, then send it to the process named `east`. Communication in original CSP was synchronous message passing based on process names—the sender and the receiver must both be ready before communication can happen.

Later (1984-1985), Hoare, Stephen Brookes, and A. W. Roscoe developed CSP into a full process algebra. In that version, communication is no longer based on process names but on anonymous channels—and this is the version the Go language adopted.

CSP's influence on programming languages has been profound. It directly shaped the occam language (designed for the INMOS Transputer processor), the Limbo language (the programming language of Plan 9), and most importantly of all, Go's concurrency model. Go is not a complete implementation of CSP, but it borrowed the core ideas: a goroutine corresponds to a CSP process, and a channel corresponds to CSP's communication channel.

### The Fundamental Differences Between CSP and the Actor Model

Wikipedia's CSP article contains a very crisp comparison; let's look at where the two models actually differ.

The first difference is identity. CSP processes are anonymous—you don't need to know who the other party is, only which channel to send data to. Actors are different: every Actor has an address (a pid in Erlang, an ActorRef in Akka), and messages must be sent to a specific address. This makes the CSP channel a decoupling layer: sender and receiver are connected only indirectly through the channel, and either end can be swapped at any time, whereas the Actor model is more tightly coupled—the sender must know the receiver's address.

The second difference lies in the synchrony of communication. CSP communication is synchronous in its base semantics (a rendezvous)—sender and receiver must both be ready before communication happens. Communication in the Actor model is asynchronous—the sender returns immediately after sending and does not wait for the receiver to be ready. Interestingly, these two semantics are duals of each other: synchronous communication plus a buffer queue becomes asynchronous communication, and asynchronous communication plus an acknowledgment/reply protocol becomes synchronous communication.

The third difference is compositionality. CSP provides a rich set of algebraic operators for composing processes—sequential composition, choice (internal/external), parallelism, hiding, and so on. These operators have formal semantics, so tools (such as the FDR refinement checker) can perform automated deadlock and liveness checks. Composition in the Actor model mostly comes down to message protocols—two Actors agree on a message format and an interaction sequence. The former is more formal; the latter is more flexible.

> Honestly, neither model is absolutely better than the other. In real-world engineering, which one fits depends more on the team's familiarity and the specific characteristics of the system. Go chose CSP, Erlang chose Actors, and both have been enormously successful.

## A Basic Channel Implementation

Let's implement a Go-like communication channel. Go's channels come in two basic forms: the unbuffered channel and the buffered channel. With an unbuffered channel, the sender blocks until a receiver is ready, and the receiver blocks until a sender is ready—this is synchronous communication: send and receive happen at the same instant. A buffered channel holds an internal queue: while the buffer is not full, the sender does not block; when the buffer is full, the sender blocks waiting for a free slot; when the buffer is empty, the receiver blocks waiting. Both kinds of channel support a `close` operation—after closing, no more sends are allowed, but the remaining data can still be received.

### Unbuffered Channel

The unbuffered channel is the purest form. Send and receive must happen at the same moment—like a handshake between two people: both must extend a hand before the handshake can happen.

```cpp
#pragma once

#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class UnbufferedChannel {
public:
    UnbufferedChannel() = default;
    ~UnbufferedChannel()
    {
        close();
    }

    /// Send a value (blocks until a receiver takes it)
    /// Returns true if the send succeeded, false if the channel is closed
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait for a receiver to be ready, or for the channel to close
        sender_cv_.wait(lock, [this] {
            return receiver_waiting_ || closed_;
        });

        if (closed_) {
            return false;
        }

        // Hand the value over to the receiver
        transfer_buffer_ = value;
        data_ready_ = true;

        // Wake the receiver to come take the data
        receiver_cv_.notify_one();

        // Wait for the receiver to confirm it has taken the data
        sender_cv_.wait(lock, [this] {
            return !data_ready_ || closed_;
        });

        return !closed_;
    }

    /// Receive a value (blocks until a sender delivers data)
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // Mark that a receiver is waiting
        receiver_waiting_ = true;
        sender_cv_.notify_one();

        // Wait for data to arrive, or for the channel to close with no data left
        receiver_cv_.wait(lock, [this] {
            return data_ready_ || closed_;
        });

        receiver_waiting_ = false;

        if (data_ready_) {
            T value = std::move(transfer_buffer_);
            data_ready_ = false;

            // Notify the sender: the data has been taken
            sender_cv_.notify_one();
            return value;
        }

        // Channel is closed and there is no data
        return std::nullopt;
    }

    /// Close the channel
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_) return;
            closed_ = true;
        }
        sender_cv_.notify_all();
        receiver_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable sender_cv_;
    std::condition_variable receiver_cv_;

    T transfer_buffer_;          // Data handoff buffer
    bool data_ready_{false};     // Whether data is waiting to be taken
    bool receiver_waiting_{false}; // Whether a receiver is waiting
    bool closed_{false};
};
```

The core of the unbuffered channel implementation is the "rendezvous"—sender and receiver complete the data exchange at the same instant. `send()` puts the data into `transfer_buffer_`, wakes the receiver, then waits for the receiver to confirm it has taken the data. `receive()` marks itself as waiting, then waits for data to arrive. The two sides coordinate through a pair of condition variables (`sender_cv_` and `receiver_cv_`).

One subtle spot in this implementation is the `receiver_waiting_` flag. It tells the sender "someone is waiting to receive right now," so the sender knows it can safely start the transfer. Without this flag, the sender could wake up with no receiver around—like shouting "anyone want this package?" into an empty room and waiting forever for an answer.

> ⚠️ **Note**: send on an unbuffered channel is synchronous—it blocks until a receiver takes the data. If your code has a sender but no receiver, send blocks forever. This is the CSP philosophy made concrete: communication is synchronous, and both sides must participate at the same time. If that doesn't suit you, use a buffered channel.

### Buffered Channel

A buffered channel is internally just a thread-safe queue—something we already know very well from ch04. While the buffer is not full, the sender enqueues and returns immediately; when the buffer is full, the sender blocks waiting for a free slot. After close, receivers can keep consuming the data left in the queue; only when the queue is empty do they get `std::nullopt`.

```cpp
template <typename T>
class BufferedChannel {
public:
    explicit BufferedChannel(size_t capacity)
        : capacity_(capacity)
    {
    }

    ~BufferedChannel()
    {
        close();
    }

    /// Send a value (blocks when the buffer is full)
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // Wait for a free slot in the buffer, or for the channel to close
        not_full_cv_.wait(lock, [this] {
            return buffer_.size() < capacity_ || closed_;
        });

        if (closed_) {
            return false;
        }

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// Try to send (non-blocking)
    /// Returns true if the send succeeded
    bool try_send(const T& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_ || buffer_.size() >= capacity_) {
            return false;
        }

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// Receive a value (blocks when the buffer is empty)
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_cv_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });

        if (buffer_.empty()) {
            // closed_ must be true here, and the buffer is already empty
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    /// Try to receive (non-blocking)
    std::optional<T> try_receive()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (buffer_.empty()) {
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    /// Close the channel
    /// After closing, send is no longer allowed, but the remaining data in the buffer can still be received
    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        not_full_cv_.notify_all();
        not_empty_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    /// Number of elements currently in the buffer
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::queue<T> buffer_;
    size_t capacity_;
    bool closed_{false};
};
```

The buffered channel implementation is the classic producer-consumer model. Two condition variables manage the two conditions "buffer not full" and "buffer not empty". On close, every waiting thread is woken—senders see closed and return false, while receivers drain the remaining data and then return `std::nullopt`.

These close semantics essentially match Go's channel-closing behavior: after closing you cannot send anymore (in our implementation send returns false; in Go it panics), and receivers can keep reading the buffered data until it runs out (Go then returns the zero value; we return `std::nullopt`).

### A Unified Channel Interface

In practice, we usually don't want to care whether a channel is buffered or unbuffered—the API should be identical. So we merge the two implementations into one template class, distinguished by a `capacity` parameter: 0 means unbuffered, anything greater than 0 means buffered.

```cpp
template <typename T>
class Channel {
public:
    /// capacity = 0 means an unbuffered channel
    explicit Channel(size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    ~Channel() { close(); }

    // Non-copyable
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /// Send (blocking)
    bool send(const T& value)
    {
        if (capacity_ == 0) {
            return unbuffered_send(value);
        }
        return buffered_send(value);
    }

    /// Receive (blocking)
    std::optional<T> receive()
    {
        if (capacity_ == 0) {
            return unbuffered_receive();
        }
        return buffered_receive();
    }

    /// Try to send (non-blocking)
    bool try_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return false;

        if (capacity_ == 0) {
            // Unbuffered channel: fail if no receiver is waiting, or if the previous transfer has not been consumed yet
            if (!receiver_waiting_ || data_ready_) return false;
            transfer_buffer_ = value;
            data_ready_ = true;
            receiver_cv_.notify_one();
            return true;
        }

        if (buffer_.size() >= capacity_) return false;
        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    /// Try to receive (non-blocking)
    std::optional<T> try_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (capacity_ == 0) {
            if (!data_ready_) return std::nullopt;
            T value = std::move(transfer_buffer_);
            data_ready_ = false;
            sender_cv_.notify_one();
            return value;
        }

        if (buffer_.empty()) return std::nullopt;
        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        sender_cv_.notify_all();
        receiver_cv_.notify_all();
        not_full_cv_.notify_all();
        not_empty_cv_.notify_all();
    }

    bool is_closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    // --- Unbuffered channel implementation ---
    bool unbuffered_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        sender_cv_.wait(lock, [this] {
            return receiver_waiting_ || closed_;
        });
        if (closed_) return false;

        transfer_buffer_ = value;
        data_ready_ = true;
        receiver_cv_.notify_one();

        sender_cv_.wait(lock, [this] {
            return !data_ready_ || closed_;
        });
        return !closed_;
    }

    std::optional<T> unbuffered_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        receiver_waiting_ = true;
        sender_cv_.notify_one();

        receiver_cv_.wait(lock, [this] {
            return data_ready_ || closed_;
        });
        receiver_waiting_ = false;

        if (data_ready_) {
            T value = std::move(transfer_buffer_);
            data_ready_ = false;
            sender_cv_.notify_one();
            return value;
        }
        return std::nullopt;
    }

    // --- Buffered channel implementation ---
    bool buffered_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_full_cv_.wait(lock, [this] {
            return buffer_.size() < capacity_ || closed_;
        });
        if (closed_) return false;

        buffer_.push(value);
        not_empty_cv_.notify_one();
        return true;
    }

    std::optional<T> buffered_receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        not_empty_cv_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });
        if (buffer_.empty()) return std::nullopt;

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    mutable std::mutex mutex_;

    // Members used by the unbuffered channel
    std::condition_variable sender_cv_;
    std::condition_variable receiver_cv_;
    T transfer_buffer_;
    bool data_ready_{false};
    bool receiver_waiting_{false};

    // Members used by the buffered channel
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::queue<T> buffer_;
    size_t capacity_;

    bool closed_{false};
};
```

This unified interface packs both channel implementations together: the `capacity` chosen at construction decides the behavior—0 is unbuffered, greater than 0 is buffered. The exposed `send` and `receive` are completely identical, so users never need to care whether data changes hands directly or travels through a queue underneath. Go works the same way—`make(chan int)` creates an unbuffered channel, `make(chan int, 5)` creates a channel with a buffer of 5, and the two are used identically.

## The Select Pattern

Go's `select` statement is one of the most powerful composition primitives in the CSP model. It lets you wait on several channel operations at once and execute whichever becomes ready first:

```go
// Go code example
select {
case msg := <-ch1:
    fmt.Println("收到 from ch1:", msg)
case msg := <-ch2:
    fmt.Println("收到 from ch2:", msg)
case ch3 <- 42:
    fmt.Println("发送 42 到 ch3 成功")
case <-time.After(time.Second):
    fmt.Println("超时")
}
```

C++ has no language-level select, but we can imitate the core idea with polling plus condition variables. A complete select implementation is genuinely complicated (fair scheduling, random choice, starvation avoidance, and more), so here we implement a simplified version that demonstrates the core mechanism.

### A Simplified Select

```cpp
/// Channel operation type
enum class ChannelOpType {
    kSend,
    kReceive
};

/// A description of one channel operation
template <typename T>
struct ChannelOp {
    Channel<T>* channel;
    ChannelOpType type;
    T send_value;            // Used only for kSend
    std::optional<T> result; // Filled only for kReceive
    bool completed{false};
};

/// A simplified select: wait on multiple channel operations at once
/// Returns the index of the first operation to complete; blocks if no operation can complete
///
/// Usage example:
///   Channel<int> ch1, ch2;
///   auto ops = make_receive_ops(ch1, ch2);
///   size_t idx = channel_select(ops);
///   if (idx == 0) { /* ch1 has data */ auto val = ops[0].result; }
///   if (idx == 1) { /* ch2 has data */ auto val = ops[1].result; }
template <typename T>
size_t channel_select(std::vector<ChannelOp<T>>& ops)
{
    // Poll repeatedly, trying to complete some operation
    while (true) {
        for (size_t i = 0; i < ops.size(); ++i) {
            auto& op = ops[i];
            if (op.completed) {
                return i;
            }

            if (op.type == ChannelOpType::kReceive) {
                auto result = op.channel->try_receive();
                if (result.has_value()) {
                    op.result = std::move(result);
                    op.completed = true;
                    return i;
                }
            }
            else {
                if (op.channel->try_send(op.send_value)) {
                    op.completed = true;
                    return i;
                }
            }
        }

        // No operation can complete immediately; briefly yield the timeslice and retry
        // A real implementation should wait on a condition variable instead of busy-waiting
        std::this_thread::yield();
    }
}

/// Helper: create a set of receive operations
template <typename T, typename... Channels>
std::vector<ChannelOp<T>> make_receive_ops(Channels&... channels)
{
    std::vector<ChannelOp<T>> ops;
    (ops.push_back(ChannelOp<T>{
        &channels, ChannelOpType::kReceive, T{}, std::nullopt, false
    }), ...);
    return ops;
}
```

> ⚠️ **Note**: this select implementation is heavily simplified. It busy-waits (via `yield`) polling all the channels, which wastes CPU in high-frequency scenarios. Go's select uses a sophisticated runtime mechanism (`selectgo`) that puts goroutines to sleep while waiting and wakes them precisely when a channel is ready, and it guarantees a random choice among simultaneously ready cases to avoid starvation. An efficient select in C++ would require maintaining a global poller, or using system-level I/O multiplexing such as epoll/kqueue. But for understanding select's semantics, this simplified version is enough.

## Hands-On 1: The Producer-Consumer Pattern

Producer-consumer is the most classic use case for channels. Let's use a buffered channel to build a pipeline with multiple producers and multiple consumers.

```cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

void producer(Channel<int>& ch, int id, int count)
{
    for (int i = 0; i < count; ++i) {
        int value = id * 1000 + i;
        ch.send(value);
        std::cout << "[Producer " << id << "] 发送: "
                  << value << "\n";

        // Simulate production time
        std::this_thread::sleep_for(
            std::chrono::milliseconds(10 + id * 5)
        );
    }
}

void consumer(Channel<int>& ch, int id)
{
    while (true) {
        auto value = ch.receive();
        if (!value.has_value()) {
            // Channel is closed and the buffer is empty
            std::cout << "[Consumer " << id << "] 退出\n";
            break;
        }
        std::cout << "[Consumer " << id << "] 接收: "
                  << *value << "\n";
    }
}

int main()
{
    // Create a channel with a buffer of capacity 5
    Channel<int> ch(5);

    // Start 2 producers and 3 consumers
    std::vector<std::thread> threads;

    threads.emplace_back(producer, std::ref(ch), 0, 10);
    threads.emplace_back(producer, std::ref(ch), 1, 10);

    threads.emplace_back(consumer, std::ref(ch), 0);
    threads.emplace_back(consumer, std::ref(ch), 1);
    threads.emplace_back(consumer, std::ref(ch), 2);

    // Wait for the producers to finish
    // Note: simplified here; a real scenario needs a better coordination mechanism
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Close the channel, telling the consumers to exit
    ch.close();

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
```

This example is completely straightforward: producers push data into the channel, consumers pull data out, and the channel's buffer acts as an elastic regulator—when producers run temporarily ahead, data piles up in the buffer; when consumers run temporarily ahead, the buffer drains. When the buffer is full, producers block automatically; when it is empty, consumers block automatically. No explicit locks or condition variables anywhere—the channel takes care of all of it for you.

## Hands-On 2: The Pipeline Pattern

The pipeline is the other classic use of channels. The core idea is to split a complex data-processing flow into multiple stages, each stage an independent goroutine (a thread, in C++), with the stages connected by channels.

```cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

/// Stage one: generate data
void generator(Channel<int>& output, int count)
{
    for (int i = 1; i <= count; ++i) {
        output.send(i);
        std::cout << "[Generator] 产生: " << i << "\n";
    }
    output.close();
    std::cout << "[Generator] 完成\n";
}

/// Stage two: square the values
void squarer(Channel<int>& input, Channel<int>& output)
{
    while (true) {
        auto value = input.receive();
        if (!value.has_value()) {
            break;
        }
        int squared = (*value) * (*value);
        output.send(squared);
        std::cout << "[Squarer] " << *value
                  << " -> " << squared << "\n";
    }
    output.close();
    std::cout << "[Squarer] 完成\n";
}

/// Stage three: print the results
void printer(Channel<int>& input)
{
    while (true) {
        auto value = input.receive();
        if (!value.has_value()) {
            break;
        }
        std::cout << "[Printer] 结果: " << *value << "\n";
    }
    std::cout << "[Printer] 完成\n";
}

int main()
{
    // Create the channels connecting the stages
    Channel<int> gen_to_square(3);   // generator -> squarer
    Channel<int> square_to_print(3); // squarer -> printer

    // Start the stages of the pipeline
    std::thread t1(generator, std::ref(gen_to_square), 8);
    std::thread t2(squarer,
                   std::ref(gen_to_square),
                   std::ref(square_to_print));
    std::thread t3(printer, std::ref(square_to_print));

    t1.join();
    t2.join();
    t3.join();

    // Expected output:
    // [Generator] 产生: 1
    // [Squarer] 1 -> 1
    // [Printer] 结果: 1
    // [Generator] 产生: 2
    // [Squarer] 2 -> 4
    // [Printer] 结果: 4
    // ...
    // [Generator] 产生: 8
    // [Squarer] 8 -> 64
    // [Printer] 结果: 64

    return 0;
}
```

The beauty of the pipeline pattern is that every stage is independent—it only cares about reading from its input channel and writing to its output channel, never about where the data came from or where it is going. That means you can freely insert, remove, or reorder stages without affecting the code of the other stages.

A classic example from the Go blog implements concurrent MD5 hashing with a pipeline—each file flows through three stages (read, hash, summarize), all running in parallel. If you have ever written a shell pipeline (say `cat file | grep pattern | sort | uniq -c`), you already understand the core idea of pipelines—we are just applying it to concurrent programming in C++.

## How Channels Relate to mutex/condition_variable

Now that we have implemented and used channels, let's answer a question you may have been itching to ask: what is a channel, underneath?

The answer is simple: **under the hood, a channel is just mutex + condition_variable + a queue**. There is no magic.

In our `BufferedChannel`, `mutex_` protects the `buffer_` queue, while `not_full_cv_` and `not_empty_cv_` announce "there is a free slot" and "there is data" respectively. This is exactly the producer-consumer model we covered in ch02. `UnbufferedChannel` is a bit more complicated, but its core is still mutex + condition_variable—only the transfer model changes from "put it in a queue" to "hand it over directly".

So the question arises: since a channel is a lock underneath, why use channels at all?

The answer is **the level of abstraction**. mutex and condition_variable are low-level primitives; a channel is a high-level abstraction. Low-level primitives are flexible but error-prone—you have to manage lock acquisition and release yourself, condition-variable waits and notifications yourself, and state checks and protection yourself. A high-level abstraction limits your freedom in exchange for correctness guarantees—a channel's interface design ensures you cannot forget to unlock, forget to notify, or write the wrong wait condition.

### A Selection Guide

When should you use a channel, and when should you go straight to mutex/condition_variable? Honestly, there is no standard answer, but there is a rough criterion you can consult.

If your concurrency model is fundamentally about "data flowing between producers and consumers"—pipelines, work queues, event dispatch, log collection—then channel semantics (send, receive, close) match those scenarios exactly. Also, when your system needs many concurrent entities (goroutines/threads) whose interactions are mostly point-to-point message passing, a channel fits better than a lock.

Conversely, if what you need is to protect a small piece of shared data rather than "passing data between entities"—say a shared counter, a cache table, or a configuration object—then a channel is simply clumsy. To update a counter you would have to create a channel, a handler thread, and a message protocol—nowhere near worth it. Furthermore, when you need very fine-grained performance control (on a hot path, say), using an atomic or a spinlock directly can be far cheaper than a channel.

A practical rule of thumb: if you catch yourself using a channel to imitate a lock (for example, serializing access to some resource through a channel), you should just use a lock. Channels solve the "data flows between entities" problem, not the "protect shared state" problem. The right tool makes for clean code.

## The CSP Ecosystem in C++

Although the C++ standard library has no channel, the community offers several mature libraries with comparable functionality:

- **Boost.Asio**'s `experimental::channel`: Boost is experimentally introducing channels; the API style is close to Go's channel but integrated with Asio's executor model.
- **cppcoro** (Lewis Baker): primarily a coroutine library, but its `single_consumer_async_queue` and `static_thread_pool` can be used to build channel semantics.
- **Folly** (Facebook/Meta): `folly/ProducerConsumerQueue.h` provides a high-performance single-producer single-consumer lock-free queue that can serve as the foundation of a channel.
- **moodycamel::ConcurrentQueue**: a high-performance multi-producer multi-consumer lock-free queue that underlies many high-performance channel implementations.

If you need channels in a serious project, prefer Boost.Asio's experimental channels or a wrapper based on moodycamel rather than building your own from scratch like we did—our implementation is geared toward teaching, and there is plenty left to optimize for performance and fairness under high concurrency.

## Where We Are

In this article we started from CSP's theory and examined its core differences from the Actor model—anonymous channels versus identifiable Actors, synchronous versus asynchronous communication, algebraic composition versus message protocols. Then we implemented a complete channel class in C++, covering unbuffered and buffered modes, close semantics, try_send/try_receive, and a simplified select. Finally, two hands-on examples—producer-consumer and pipeline—showed channels in action, and we worked out criteria for choosing between channels and mutex/condition_variable.

With this, the two articles of ch07 are complete. We spent two articles exploring concurrency paradigms that refuse to share memory—the Actor model and the CSP model. Both pursue the same goal: eliminating the complexity that shared state brings, but by different roads. Actors decouple through identity and mailboxes; CSP decouples through anonymous channels. In real-world engineering, the two models are often mixed—for example, inside an Actor system, communication between Actors may well be implemented over channels.

From the next article on, we enter the final big topic of Volume 5: debugging, testing, and performance optimization—when your concurrent program goes wrong, how do you locate and fix the problem? From theory to practice, from implementation to troubleshooting—this closes the loop of our entire concurrency journey.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch07-actor-channel/`.

## References

- [Communicating Sequential Processes — Hoare, 1978 (CACM)](https://dl.acm.org/doi/10.1145/359576.359585) — the original CSP paper
- [Communicating Sequential Processes — Hoare, 1985 (Book)](https://usingcsp.com/cspbook.pdf) — the complete CSP monograph, free online
- [CSP — Wikipedia](https://en.wikipedia.org/wiki/Communicating_sequential_processes) — a detailed history and theoretical introduction to CSP
- [Go Channel Types Specification](https://go.dev/ref/spec#Channel_types) — the official semantics of Go's channels
- [Go Concurrency Patterns: Pipelines and cancellation (Go Blog)](https://go.dev/blog/pipelines) — the Go blog's tutorial on the pipeline pattern
- [Share Memory By Communicating (Go Blog)](https://go.dev/blog/codelab-share) — Go's exposition of the CSP philosophy
- [Boost.Asio Experimental Channel](https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/composition/channel.html) — a channel implementation on its way to standardization in the C++ ecosystem
