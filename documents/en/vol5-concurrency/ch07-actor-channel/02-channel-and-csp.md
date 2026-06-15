---
chapter: 7
cpp_standard:
- 17
- 20
description: Understanding the CSP (Communicating Sequential Processes) concurrency
  model, implementing Go-like channels in C++
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Actor 模型与消息传递
- 线程安全队列
reading_time_minutes: 24
related:
- 协程 Echo Server 实战
tags:
- host
- cpp-modern
- intermediate
- 异步编程
- 进阶
title: Channels and the CSP Model
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch07-actor-channel/02-channel-and-csp.md
  source_hash: 2fe033f08c0df15f8bfef37e75e815af4947f53456d931c5f4a80ade88241259
  token_count: 5701
  translated_at: '2026-05-20T04:48:08.473555+00:00'
---
# Channels and the CSP Model

In the previous article, we explored the Actor model—organizing concurrency through identified Actors and asynchronous message passing. In this article, we look at another school of thought that also advocates "not sharing memory": CSP (Communicating Sequential Processes).

CSP was first proposed by Tony Hoare in his 1978 paper, *"Communicating Sequential Processes"* (published in Communications of the ACM). Like the Actor model, the core idea of CSP is to replace shared memory with message passing, but it takes a different path: Actors have identity and mailboxes, and messages are sent to specific Actor addresses; CSP, on the other hand, communicates through anonymous channels, and the processes themselves do not need to know who the other party is. This difference may seem subtle, but it creates significant variations in programming style and expressive power. Go's goroutine + channel is the most successful industrial implementation of CSP, and Rob Pike's famous quote—"Don't communicate by sharing memory; share memory by communicating"—is Go's summary of the CSP philosophy.

In this article, we start with the theoretical foundations of CSP, then implement a Go-like communication pipeline in C++, including buffered/unbuffered channels, close semantics, and the select pattern. Finally, we discuss when to use channels and when to use locks directly.

## Environment Setup

As with the previous article, all our code is based on C++17 and compiles successfully under GCC 12+ / Clang 15+ / MSVC 2022+ with the compiler flag `-std=c++17 -pthread -O2`. It runs on Linux, macOS, and Windows, as long as your standard library supports `<thread>`, `<mutex>`, and `<condition_variable>`. The code throughout this article does not depend on any third-party libraries.

## Theoretical Foundations of CSP

The original CSP paper was published five years after the Actor model (1978 vs. 1973), but its influence is equally profound. Hoare's initial design was a concurrent programming language (rather than the formal calculus it later became), with syntax that looked like this:

```text
COPY = *[c:character; west?c -> east!c]
```

This code means: repeatedly receive a character `c` from a process named `west`, then send it to a process named `east`. Communication in the original CSP was synchronous message passing based on process names—both the sender and receiver must be ready at the same time for communication to occur.

Later (1984-1985), Hoare, Stephen Brookes, and A. W. Roscoe developed CSP into a complete process algebra. In this version, communication was no longer based on process names, but on anonymous channels—this is the version that the Go language adopted.

CSP's influence on programming languages is profound. It directly influenced the occam language (designed for the INMOS Transputer processor), the Limbo language (a programming language for Plan 9), and most importantly—the Go language's concurrency model. Go is not a complete implementation of CSP, but it borrows the core ideas: goroutines correspond to CSP processes, and channels correspond to CSP communication channels.

### Fundamental Differences Between CSP and Actor

The Wikipedia article on CSP has a very clear comparison; let's look at exactly where the two differ.

The first difference is identity. CSP processes are anonymous—you don't need to know who the other party is, only which channel to send data to. Actors are different; each Actor has an address (a pid in Erlang, an ActorRef in Akka), and messages must be sent to a specific address. This means a CSP channel acts as a decoupling layer: the sender and receiver are indirectly associated through the channel, and either end can be replaced at any time. The Actor model has tighter coupling—the sender must know the receiver's address.

The second difference lies in the synchronicity of communication. CSP communication is synchronous (rendezvous) in its base semantics—both the sender and receiver must be ready at the same time for communication to occur. Actor model communication is asynchronous—the sender returns immediately after sending a message, without waiting for the receiver to be ready. Interestingly, these two semantics are duals of each other: synchronous communication plus a buffer queue becomes asynchronous communication, and asynchronous communication plus an acknowledgment/reply protocol becomes synchronous communication.

The third difference is composability. CSP provides rich algebraic operators for combining processes—sequential composition, choice (internal/external), parallel composition, hiding, and so on. These operators have formal semantics and can be used with tools (like the FDR refinement checker) for automated dead lock and liveness checking. Composition in the Actor model relies primarily on message protocols—two Actors agree on message formats and interaction sequences. The former is more formal, while the latter is more flexible.

> Honestly, there is no absolute superiority between the two models. In practical engineering, the choice depends more on the team's familiarity and the specific system characteristics. Go chose CSP, Erlang chose Actor, and both have achieved tremendous success.

## Basic Channel Implementation

Let's implement a Go-like communication pipeline. Go's channels have two basic forms: unbuffered channels and buffered channels. An unbuffered channel blocks the sender until a receiver is ready, and blocks the receiver until a sender is ready—this is synchronous communication, where sending and receiving happen at the same instant. A buffered channel has an internal queue; the sender does not block when the buffer is not full, but blocks when the buffer is full. The receiver blocks when the buffer is empty. Both types of channels support the `close` operation—after closing, no more data can be sent, but remaining data can still be received.

### Unbuffered Channel

The unbuffered channel is the purest form. Sending and receiving must happen simultaneously—like two people shaking hands; both must reach out for the handshake to occur.

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

    /// 发送一个值（阻塞，直到有接收方取走）
    /// 返回 true 表示发送成功，false 表示 channel 已关闭
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待接收方就绪，或者 channel 被关闭
        sender_cv_.wait(lock, [this] {
            return receiver_waiting_ || closed_;
        });

        if (closed_) {
            return false;
        }

        // 把值交给接收方
        transfer_buffer_ = value;
        data_ready_ = true;

        // 唤醒接收方来取数据
        receiver_cv_.notify_one();

        // 等待接收方确认取走了数据
        sender_cv_.wait(lock, [this] {
            return !data_ready_ || closed_;
        });

        return !closed_;
    }

    /// 接收一个值（阻塞，直到有发送方送来数据）
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 标记有接收方在等待
        receiver_waiting_ = true;
        sender_cv_.notify_one();

        // 等待数据到达，或者 channel 关闭且数据已耗尽
        receiver_cv_.wait(lock, [this] {
            return data_ready_ || closed_;
        });

        receiver_waiting_ = false;

        if (data_ready_) {
            T value = std::move(transfer_buffer_);
            data_ready_ = false;

            // 通知发送方：数据已被取走
            sender_cv_.notify_one();
            return value;
        }

        // channel 已关闭且没有数据
        return std::nullopt;
    }

    /// 关闭 channel
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

    T transfer_buffer_;          // 数据传递缓冲区
    bool data_ready_{false};     // 是否有待取的数据
    bool receiver_waiting_{false}; // 是否有接收方在等待
    bool closed_{false};
};
```

The core of the unbuffered channel implementation is "rendezvous"—the sender and receiver complete the data exchange at the exact same moment. `send()` places the data into `transfer_buffer_`, wakes up the receiver, and then waits for the receiver to confirm it has taken the data. `receive()` marks itself as waiting, and then waits for the data to arrive. Both parties coordinate through two condition variables (`sender_cv_` and `receiver_cv_`).

There is a subtle aspect to this implementation: the `receiver_waiting_` flag. It tells the sender "someone is currently waiting to receive," so the sender knows it is safe to start the transfer. Without this flag, the sender might wake up without any receiver present—like shouting "Does anyone want this package?" into an empty room, and waiting forever for a response.

> ⚠️ **Warning**: The send operation on an unbuffered channel is synchronous—it blocks until the receiver takes the data. If you have a sender but no receiver in your code, send will block forever. This is a direct reflection of the CSP philosophy: communication is synchronous, and both parties must participate simultaneously. If this doesn't suit your needs, use a buffered channel.

### Buffered Channel

A buffered channel is essentially a thread-safe queue internally—something we became very familiar with in ch04. When the buffer is not full, the sender enqueues the data and returns immediately; when the buffer is full, the sender blocks and waits for a free slot. After closing, the receiver can continue consuming the remaining data in the queue, and only returns `std::nullopt` when the queue is empty.

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

    /// 发送一个值（缓冲区满时阻塞）
    bool send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        // 等待缓冲区有空位，或者 channel 被关闭
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

    /// 尝试发送（非阻塞）
    /// 返回 true 表示发送成功
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

    /// 接收一个值（缓冲区空时阻塞）
    std::optional<T> receive()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_cv_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });

        if (buffer_.empty()) {
            // closed_ 一定为 true，且缓冲区已空
            return std::nullopt;
        }

        T value = std::move(buffer_.front());
        buffer_.pop();
        not_full_cv_.notify_one();
        return value;
    }

    /// 尝试接收（非阻塞）
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

    /// 关闭 channel
    /// 关闭后不能再 send，但可以继续 receive 缓冲区里的剩余数据
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

    /// 当前缓冲区中的元素数量
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

The buffered channel implementation is the classic producer-consumer model. Two condition variables manage the "buffer not full" and "buffer not empty" conditions, respectively. Upon close, all waiting threads are woken up—senders find that the channel is closed and return false, while receivers continue consuming the remaining data before returning `std::nullopt`.

This close semantics is basically consistent with Go's channel closing behavior: after closing, you can no longer send (our implementation returns false from send, whereas Go panics), and the receiver can continue reading buffered data until it is exhausted (Go then returns a zero value, while we return `std::nullopt`).

### Unified Channel Interface

In practice, we often don't want to care about whether a channel is buffered or unbuffered—the API should be consistent. So we unify both implementations into a single template class, using the `capacity` parameter to distinguish: 0 means unbuffered, and greater than 0 means buffered.

```cpp
template <typename T>
class Channel {
public:
    /// capacity = 0 表示无缓冲 channel
    explicit Channel(size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    ~Channel() { close(); }

    // 禁止拷贝
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    /// 发送（阻塞）
    bool send(const T& value)
    {
        if (capacity_ == 0) {
            return unbuffered_send(value);
        }
        return buffered_send(value);
    }

    /// 接收（阻塞）
    std::optional<T> receive()
    {
        if (capacity_ == 0) {
            return unbuffered_receive();
        }
        return buffered_receive();
    }

    /// 尝试发送（非阻塞）
    bool try_send(const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (closed_) return false;

        if (capacity_ == 0) {
            // 无缓冲 channel：没有接收方在等待，或者前一次传输尚未被消费，就失败
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

    /// 尝试接收（非阻塞）
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
    // --- 无缓冲 channel 的实现 ---
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

    // --- 有缓冲 channel 的实现 ---
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

    // 无缓冲通道使用的成员
    std::condition_variable sender_cv_;
    std::condition_variable receiver_cv_;
    T transfer_buffer_;
    bool data_ready_{false};
    bool receiver_waiting_{false};

    // 有缓冲通道使用的成员
    std::condition_variable not_full_cv_;
    std::condition_variable not_empty_cv_;
    std::queue<T> buffer_;
    size_t capacity_;

    bool closed_{false};
};
```

This unified interface packages both channel implementations together. Specifying `capacity` at construction time determines the behavior—0 for unbuffered, greater than 0 for buffered. The externally exposed `send` and `receive` are completely identical, so users don't need to care whether the internal mechanism is a direct handoff or a queue. This design is the same in Go—`make(chan int)` creates an unbuffered channel, `make(chan int, 5)` creates a channel with a buffer size of five, and they are used in exactly the same way.

## The Select Pattern

Go's `select` statement is one of the most powerful composition primitives in the CSP model. It allows you to wait on multiple channel operations simultaneously, executing whichever one becomes ready first:

```go
// Go 代码示例
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

C++ does not have a language-level select, but we can simulate the core idea using polling + condition variables. A full select implementation is quite complex (requiring fair scheduling, random selection, starvation avoidance, etc.), so here we implement a simplified version to demonstrate the core mechanism.

### Simplified Select

```cpp
/// channel 操作类型
enum class ChannelOpType {
    kSend,
    kReceive
};

/// 一个 channel 操作描述
template <typename T>
struct ChannelOp {
    Channel<T>* channel;
    ChannelOpType type;
    T send_value;            // 仅 kSend 时使用
    std::optional<T> result; // 仅 kReceive 时填充
    bool completed{false};
};

/// 简化版 select：同时等待多个 channel 操作
/// 返回第一个完成的操作的索引，如果没有操作能完成则阻塞
///
/// 用法示例：
///   Channel<int> ch1, ch2;
///   auto ops = make_receive_ops(ch1, ch2);
///   size_t idx = channel_select(ops);
///   if (idx == 0) { /* ch1 有数据 */ auto val = ops[0].result; }
///   if (idx == 1) { /* ch2 有数据 */ auto val = ops[1].result; }
template <typename T>
size_t channel_select(std::vector<ChannelOp<T>>& ops)
{
    // 反复轮询，尝试完成某个操作
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

        // 没有操作能立刻完成，短暂让出时间片后再试
        // 真实实现应该用条件变量等待而不是忙等
        std::this_thread::yield();
    }
}

/// 辅助函数：创建一组接收操作
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

> ⚠️ **Warning**: This select implementation is highly simplified. It uses busy-waiting (`yield`) to poll all channels, which wastes CPU in high-frequency scenarios. Go's select internally uses complex runtime mechanisms (`selectgo`); it puts goroutines to sleep while waiting and precisely wakes them when a channel is ready, and it guarantees random selection when multiple cases become ready simultaneously to avoid starvation. To implement an efficient select in C++, you would need to maintain a global poller or use system-level I/O multiplexing mechanisms like epoll/kqueue. However, for understanding the semantics of select, this simplified version is sufficient.

## Practical Example 1: Producer-Consumer Pattern

The producer-consumer pattern is the most classic use case for channels. We use a buffered channel to implement a multi-producer, multi-consumer pipeline.

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

        // 模拟生产耗时
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
            // channel 已关闭且缓冲区为空
            std::cout << "[Consumer " << id << "] 退出\n";
            break;
        }
        std::cout << "[Consumer " << id << "] 接收: "
                  << *value << "\n";
    }
}

int main()
{
    // 创建一个缓冲区大小为 5 的 channel
    Channel<int> ch(5);

    // 启动 2 个生产者和 3 个消费者
    std::vector<std::thread> threads;

    threads.emplace_back(producer, std::ref(ch), 0, 10);
    threads.emplace_back(producer, std::ref(ch), 1, 10);

    threads.emplace_back(consumer, std::ref(ch), 0);
    threads.emplace_back(consumer, std::ref(ch), 1);
    threads.emplace_back(consumer, std::ref(ch), 2);

    // 等待生产者完成
    // 注意：这里简化了，真实场景需要更好的协调机制
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 关闭 channel，通知消费者退出
    ch.close();

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    return 0;
}
```

This example is very straightforward: producers push data into the channel, and consumers pull data from the channel. The channel's buffer acts as an elastic adjuster—when producers are temporarily faster, data accumulates in the buffer; when consumers are temporarily faster, the buffer is drained. When the buffer is full, producers automatically block; when the buffer is empty, consumers automatically block. No explicit locks or condition variables are needed—the channel handles everything for you.

## Practical Example 2: Pipeline Pattern

The pipeline is another classic use case for channels. The core idea of a pipeline is to break down a complex data processing flow into multiple stages, where each stage is an independent goroutine (a thread in C++), and stages are connected by channels.

```cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

/// 阶段一：生成数据
void generator(Channel<int>& output, int count)
{
    for (int i = 1; i <= count; ++i) {
        output.send(i);
        std::cout << "[Generator] 产生: " << i << "\n";
    }
    output.close();
    std::cout << "[Generator] 完成\n";
}

/// 阶段二：平方运算
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

/// 阶段三：打印结果
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
    // 创建连接各阶段的 channel
    Channel<int> gen_to_square(3);   // generator -> squarer
    Channel<int> square_to_print(3); // squarer -> printer

    // 启动管道各阶段
    std::thread t1(generator, std::ref(gen_to_square), 8);
    std::thread t2(squarer,
                   std::ref(gen_to_square),
                   std::ref(square_to_print));
    std::thread t3(printer, std::ref(square_to_print));

    t1.join();
    t2.join();
    t3.join();

    // 预期输出：
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

The beauty of the pipeline pattern lies in the independence of each stage—it only needs to care about reading data from the input channel and writing data to the output channel, without knowing where the data comes from or where it goes. This means you can freely insert, remove, or reorder stages without affecting the code of other stages.

The Go blog has a classic example of using pipelines for concurrent MD5 hash computation—each file goes through three stages: reading, computing, and summarizing, with all stages running in parallel. If you have ever written shell pipelines (like `cat file | grep pattern | sort | uniq -c`), you already understand the core idea of a pipeline—except here we are applying it to concurrent programming in C++.

## The Relationship Between Channels and mutex/condition_variable

Now that we have implemented and used channels, let's answer a question you may have been wanting to ask for a while: what exactly is under the hood of a channel?

The answer is simple: **the underlying implementation of a channel is just mutex + condition_variable + queue**. There is no magic.

In our `BufferedChannel`, `mutex_` protects the `buffer_` queue, while `not_full_cv_` and `not_empty_cv_` notify "there is a free slot" and "there is data available," respectively. This is completely consistent with the producer-consumer pattern discussed in ch02. `UnbufferedChannel` is slightly more complex, but the core is still mutex + condition_variable; the only difference is that the transfer model changes from "put into a queue" to "direct handoff."

So the question arises: since the underlying mechanism of a channel is just locks, why bother using channels?

The answer is **abstraction level**. mutex and condition_variable are low-level primitives, while channels are high-level abstractions. Low-level primitives are flexible but error-prone—you need to manually manage lock acquisition and release, condition variable waiting and notification, and state checking and protection. High-level abstractions restrict your freedom but guarantee correctness—the interface design of a channel ensures you won't forget to unlock, won't forget to notify, and won't get the wait condition wrong.

### Selection Guide

When should you use channels, and when should you use mutex/condition_variable directly? Honestly, there is no standard answer to this question, but there is a rough rule of thumb you can follow.

If your concurrency model is essentially "data flowing between producers and consumers"—such as pipelines, work queues, event dispatching, or log collection—then the semantics of a channel (send, receive, close) perfectly match these scenarios. Additionally, when your system requires a large number of concurrent entities (goroutines/threads) and their interactions are primarily point-to-point message passing, channels are more suitable than locks.

Conversely, if what you are protecting is a small piece of shared data, rather than "passing data between entities"—such as a shared counter, a cache table, or a configuration object—then using a channel becomes clumsy. Creating a channel, a handler thread, and an entire message protocol just to update a counter is completely not worth the effort. Furthermore, when you need very fine-grained performance control (such as on a hot path), using atomic operations or a spinlock directly may have much lower overhead than a channel.

A practical rule of thumb: if you find yourself using a channel to simulate a lock (for example, using a channel to serialize access to a resource), you should just use a lock directly. Channels solve the problem of "data flowing between entities," not the problem of "protecting shared state." Only by using the right tool will your code stay clean.

## The CSP Ecosystem in C++

Although the C++ standard library does not include channels, there are some mature libraries in the community that provide similar functionality:

- **Boost.Asio's** `experimental::channel`: Boost is experimentally introducing channels, with an API style close to Go's channels, but integrated with Asio's executor model.
- **cppcoro** (Lewis Baker): Although primarily a coroutine library, it provides `single_consumer_async_queue` and `static_thread_pool` which can be used to build channel semantics.
- **Folly** (Facebook/Meta): `folly/ProducerConsumerQueue.h` provides a high-performance single-producer, single-consumer lock-free queue that can be used as the underlying mechanism for a channel.
- **moodycamel::ConcurrentQueue**: A high-performance multi-producer, multi-consumer lock-free queue that serves as the underlying mechanism for many high-performance channel implementations.

If you need channels in a serious project, we recommend prioritizing Boost.Asio's experimental channels or a wrapper around moodycamel, rather than implementing them from scratch as we did here—our implementation focuses on educational purposes, and there is still much room for optimization regarding performance and fairness under high concurrency.

## Where We Are

In this article, we started from the theory of CSP and understood its core differences from the Actor model—anonymous channels versus identified Actors, synchronous communication versus asynchronous communication, and algebraic composition versus message protocols. We then used C++ to implement a complete channel class, including both unbuffered and buffered modes, close semantics, try_send/try_receive, and a simplified version of select. Finally, we demonstrated how to use channels through two practical examples—producer-consumer and pipelines—and discussed the selection criteria between channels and mutex/condition_variable.

With this, the two articles of ch07 come to a close. We spent two articles exploring the "don't share memory" concurrency paradigm—the Actor model and the CSP model. Both pursue the same goal: eliminating the complexity brought by shared state, but they take different paths. Actors use identity and mailboxes to decouple, while CSP uses anonymous channels to decouple. In practical engineering, these two models are often used in a mixed fashion—for example, within an Actor system, communication between Actors might be implemented through channels.

In the next article, we will enter the final major topic of Volume 5: debugging, testing, and performance optimization—when your concurrent program has problems, how do you locate and fix them? From theory to practice, from implementation to troubleshooting, this completes the loop of our entire concurrency journey.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch07-actor-channel/`.

## References

- [Communicating Sequential Processes — Hoare, 1978 (CACM)](https://dl.acm.org/doi/10.1145/359576.359585) — The original CSP paper
- [Communicating Sequential Processes — Hoare, 1985 (Book)](https://usingcsp.com/cspbook.pdf) — The complete CSP monograph, free online version
- [CSP — Wikipedia](https://en.wikipedia.org/wiki/Communicating_sequential_processes) — Detailed history and theoretical introduction to CSP
- [Go Channel Types Specification](https://go.dev/ref/spec#Channel_types) — Official semantic definition of Go language channels
- [Go Concurrency Patterns: Pipelines and cancellation (Go Blog)](https://go.dev/blog/pipelines) — Pipeline pattern tutorial from the official Go blog
- [Share Memory By Communicating (Go Blog)](https://go.dev/blog/codelab-share) — Go's exposition of the CSP philosophy
- [Boost.Asio Experimental Channel](https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/composition/channel.html) — A channel implementation in the C++ ecosystem currently being standardized
