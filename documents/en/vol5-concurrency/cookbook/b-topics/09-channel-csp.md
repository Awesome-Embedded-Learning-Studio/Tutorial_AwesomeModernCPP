---
title: "Channel and CSP: Select, Pipelines"
description: "The theoretical foundation of CSP and its three differences from the Actor model, implementing unbuffered, buffered, and unified-interface Go-like channels in C++17, simulating Select semantics, and closing with a producer-consumer case and a pipeline case discussing when to choose channels over locks."
chapter: 5
order: 9
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
  source: documents/vol5-concurrency/cookbook/b-topics/09-channel-csp.md
  source_hash: 26291010583ed99c251510d07e67d2f80d7b2f6ddf723008b853e00294690530
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 23921
---
# Channel and CSP: Select, Pipelines

This piece and [The Actor Model and Supervisor Strategies](08-actor-model) form a pair: both schools advocate "no shared memory", but they walk different paths. Neither enters a required unit; we migrated the pair, whole, into the readings section.

Before reading it, you need to have read [The Actor Model and Supervisor Strategies](08-actor-model) (a comparison stands only with both sides present), be able to organize producer-consumer with mutex and condition variables ([Recovering the Lost Numbers — std::mutex and the Three Scoped Locks](../../chapter02-sharing-data/02-mutex-and-locks), [Waiting for a Condition: From Polling to Condition Variables](../../chapter03-wait-and-coordinate/01-wait-for-a-condition)), and have seen the queue's closing semantics ([What If the Queue Cannot Close: Closing and Backpressure](../../chapter03-wait-and-coordinate/04-queue-close-and-backpressure)).

The migration trail: mechanism explanations, code, and exercises are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The material's claimed build environment (GCC 12+ / Clang 15+ / MSVC 2022+, `-std=c++17 -pthread -O2`) remains as-is; I did not recompile and re-verify this round.

The previous piece discussed the Actor model — organizing concurrency with identity-bearing Actors and asynchronous message passing. This piece looks at another school that likewise advocates "no shared memory": CSP (Communicating Sequential Processes).

CSP was proposed by Tony Hoare in 1978; the paper *"Communicating Sequential Processes"* appeared in Communications of the ACM, the source readily available. Like the Actor model, CSP's core idea is replacing shared memory with message passing, but it takes a different road: Actors have identity and mailboxes, and messages go to a specific Actor address. Lay the two side by side. CSP communicates through anonymous channels, and processes themselves need not know who is on the other end. The difference looks subtle, yet it produces large differences in programming style and expressive power. Go's goroutine + channel is CSP's most successful industrial practice, and Rob Pike's famous line compresses the CSP philosophy into one phrase: "share memory by communicating" — do not communicate by sharing memory; share memory by communicating.

In this piece we start from CSP's theoretical foundation, then implement a Go-like channel in C++ — buffered and unbuffered channels, close semantics, the select pattern — and finally discuss when to use channels and when to use locks directly.

## Environment Note

As in the previous piece, all code is based on C++17, compiled under GCC 12+ / Clang 15+ / MSVC 2022+ with `-std=c++17 -pthread -O2`. Linux, macOS, and Windows all work, as long as your standard library supports `<thread>`, `<mutex>`, and `<condition_variable>`. The whole piece depends on no third-party library.

## The Theoretical Foundation of CSP

CSP's original paper came five years after the Actor model (1978 versus 1973), but its influence, look closely, is equally far-reaching. Hoare's original design was a concurrent programming language (not the formal calculus it later became), with syntax like this:

```text
COPY = *[c:character; west?c -> east!c]
```

This code means: repeatedly receive a character `c` from the process named `west`, then send it to the process named `east`. Original CSP's communication was synchronous message passing based on process names; sender and receiver must both be ready for communication to happen — picture the two ends as holding a phone call.

Later (1984-1985), Hoare, Stephen Brookes, and A. W. Roscoe took it over and developed it into a full process algebra; follow the thread onward. In this version, communication is no longer based on process names but on anonymous channels — and that is what the Go language adopted.

CSP's influence on programming languages is deep; count them off: it directly shaped occam (designed for the INMOS Transputer processor), Limbo (the programming language of Plan 9), and — most importantly — Go's concurrency model. Go is not a complete implementation of CSP, but it borrowed the core ideas: the goroutine corresponds to CSP's process, the channel to CSP's communication channel.

### The Fundamental Differences Between CSP and Actors

The Wikipedia CSP article carries a very clear comparison; let us see where they truly differ.

The first difference is identity. CSP processes are anonymous — you need not know who the other party is, only which channel to send data into. Actors differ: each Actor has an address (a pid in Erlang, an ActorRef in Akka), and messages must be sent to a specific address. This means CSP's channel is a decoupling layer: sender and receiver relate indirectly through the channel, and either end can be replaced at any time, while the Actor model couples more tightly — the sender must know the receiver's address.

The second difference lies in communication synchrony; set both sides against each other: CSP's communication is synchronous in its base semantics (rendezvous) — sender and receiver must both be ready. The Actor model's communication is asynchronous — the sender returns immediately after sending, without waiting for the receiver. Interestingly, the two are duals: synchronous communication plus a buffering queue becomes asynchronous, and asynchronous communication plus an acknowledgment/reply protocol becomes synchronous.

The third difference is composability; look closely. CSP provides rich algebraic operators for composing processes (sequential composition, choice, parallelism, hiding, and so on); these operators carry formal semantics, and you can run tools for automated deadlock and liveness checking, such as the FDR refinement checker. The Actor model composes mainly through message protocols — two Actors agree on message formats and interaction sequences. The former is more formal, the latter more flexible.

> Neither model is absolutely superior; put it on the table: which one to choose in real engineering depends more on team familiarity and the system's specific character. Go chose CSP, Erlang chose the Actor model, and both achieved enormous success.

## A Basic Channel Implementation

Let us implement a Go-like channel. Go's channels come in two basic forms; take them separately: the unbuffered channel and the buffered channel. An unbuffered channel's sender blocks until a receiver is ready, and the receiver blocks until a sender is ready — synchronous communication, send and receive happening at the same instant. A buffered channel holds an internal queue: the sender does not block while the buffer is not full, blocks waiting when it is; the receiver blocks waiting when the buffer is empty. Both support `close` — after closing, no more sending, but remaining data can still be received.

### The Unbuffered Channel

The unbuffered channel is the purest form; remember it with one scene: send and receive must happen simultaneously, like a handshake between two people — both must extend a hand for the grip to happen.

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

The crux of the unbuffered channel's implementation is the "rendezvous": sender and receiver complete the data exchange at the same instant; follow the code through. `send()` puts the data into `transfer_buffer_`, wakes the receiver, and waits for the receiver's confirmation of pickup. `receive()` marks itself waiting, then waits for the data to arrive. The two sides coordinate through two condition variables (`sender_cv_` and `receiver_cv_`).

One subtlety in this implementation: the `receiver_waiting_` flag. It tells the sender "someone is waiting to receive right now", so the sender knows it is safe to begin the transfer. Without that flag, the sender might wake with no receiver at all — like shouting into an empty room, "anyone want this package?", and waiting forever for an answer.

> **Note**: an unbuffered channel's send is synchronous — it blocks until the receiver takes the data. If your code has a sender but no receiver, send blocks forever. This is CSP philosophy made direct: communication is synchronous, both parties must participate simultaneously. Said plainly: if that does not suit you, use a buffered channel.

### The Buffered Channel

A buffered channel is internally a thread-safe queue, which we know intimately from [A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue). While the buffer is not full, the sender enqueues and returns. When full, the sender blocks waiting for a slot. After closing, the receiver keeps consuming the queue's remaining data, returning `std::nullopt` only when the queue drains.

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

The buffered channel's implementation is the classic producer-consumer model, recognizable at a glance. Two condition variables manage the two conditions "buffer not full" and "buffer not empty". On close, every waiting thread wakes: senders see closed and return false; receivers finish consuming the remaining data and then return `std::nullopt`.

This close semantics basically matches Go's channel-closing behavior: after closing, no more sends (our send returns false; Go panics), and the receiver may keep reading the buffered data until exhausted (Go then returns the zero value; we return `std::nullopt`).

### A Unified Channel Interface

In practice we usually do not want to care whether a channel is buffered or unbuffered — the API should be uniform. So we unify both implementations into one template class, distinguished by the `capacity` parameter: 0 means unbuffered, greater than 0 means buffered.

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

This unified interface packs both channel implementations together; look at the entry: the `capacity` given at construction decides the behavior — 0 unbuffered, above 0 buffered. The exposed `send` and `receive` are identical, and users need not care whether the inside is a direct handoff or a queue. Go works the same way — `make(chan int)` creates an unbuffered channel, `make(chan int, 5)` creates one with a buffer of 5, and usage shows no difference.

## The Select Pattern

Go's `select` statement is one of the CSP model's most powerful composition primitives. It lets you wait on several channel operations at once and execute whichever becomes ready:

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

C++ has no language-level select, but we can simulate the core idea with polling plus condition variables. A complete select implementation is rather complex (fair scheduling, random choice, starvation avoidance, and so on); here we implement a simplified version that displays the core mechanism.

### A Simplified Select

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
/// 返回扫描到的首个就绪操作的索引，如果没有操作能完成则阻塞
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

> **Note**: this select implementation is heavily simplified. It uses busy-waiting (`yield`) to poll every channel, wasting CPU in high-frequency scenarios. Go's select uses elaborate runtime machinery internally (`selectgo`): goroutines sleep while waiting, wake precisely when a channel is ready, and simultaneous readiness among cases is resolved randomly to avoid starvation. An efficient C++ select would need a global poller or system-level I/O multiplexing such as epoll/kqueue — the machinery [Asynchronous I/O and Event Loops](06-async-io-event-loop) covered; you may flip back. For understanding select's semantics, though, this simplified version suffices.

## Case 1: The Producer-Consumer Pattern

Producer-consumer is the channel's most classic application scenario. We use a buffered channel to implement a multi-producer, multi-consumer pipeline.

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

This example is dead plain: producers stuff data into the channel, consumers take it out, and the channel's buffer acts as the elastic regulator — when producers run momentarily fast, data piles up in the buffer; when consumers run momentarily fast, the buffer drains. When full, producers block automatically; when empty, consumers block automatically. No explicit locks or condition variables anywhere — the channel manages it all for you.

## Case 2: The Pipeline Pattern

The pipeline is another classic channel use; take it in sections: the core idea is splitting a complex data-processing flow into multiple stages, each stage an independent execution unit (a thread, in C++), with channels connecting the stages.

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

The pipeline pattern's beauty is that every stage is independent: it cares only about reading from its input channel and writing to its output channel, never about where the data came from or where it goes. That means you can freely insert, delete, and reorder stages without touching the others' code.

The Go blog carries a classic example computing MD5 hashes concurrently through a pipeline, each file passing through read, compute, and aggregate stages, all running in parallel. If you have written a shell pipeline (say `cat file | grep pattern | sort | uniq -c`), you already understand the pipeline's core idea — we are simply applying it to concurrent programming in C++.

## Channels and mutex/condition_variable

Having implemented and used channels, next comes a question you may have wanted to ask all along: what is actually underneath a channel?

The answer, straight: **underneath a channel is just mutex + condition_variable plus a queue**. No magic.

Look at our own `BufferedChannel`: `mutex_` guards the `buffer_` queue, `not_full_cv_` and `not_empty_cv_` announce "a slot opened" and "data arrived" respectively. That is exactly the producer-consumer pattern of Unit 2. The `UnbufferedChannel` is slightly more complex, but its core is still mutex + condition_variable — only the transfer model shifts from "put into a queue" to "direct handoff".

Then the question — let us ask it for you: if channels are locks underneath, why use channels at all?

The answer is **the level of abstraction**. mutex and condition_variable are low-level primitives; the channel is a high-level abstraction. Low-level primitives are flexible but error-prone — you manage lock acquisition and release, condition-variable waits and notifications, state checks and guards yourself, with no room for carelessness anywhere. A high-level abstraction limits your freedom in exchange for correctness guarantees: the channel's interface design makes it impossible to forget to unlock, forget to notify, or write the wrong wait condition.

### A Selection Guide

When to use channels, and when to use mutex/condition_variable directly? There is no standard answer; here is a rough set of criteria for reference.

The first situation on the table: your concurrency model is essentially "data flowing between producers and consumers" — pipelines, work queues, event dispatch, log collection — and the channel's semantics (send, receive, close) match those scenarios exactly. Moreover, when the system needs many concurrent entities (goroutines/threads) whose interaction is mostly point-to-point message passing, channels fit better than locks.

Conversely, if what you need is to protect a small piece of shared data rather than "pass data between entities" — a shared counter, a cache table, a configuration object — channels turn clumsy. Updating one counter would demand a channel, a handler thread, and a message protocol — a losing bargain all around. Also, when you need very fine-grained performance control (on a hot path, say), raw atomics or spinlocks may cost far less than a channel.

A practical rule of thumb: if you catch yourself using a channel to imitate a lock (say, serializing access to some resource through a channel), you should just use a lock. Channels solve "data flowing between entities", not "protecting shared state". The right tool keeps the code clean.

## The CSP Ecosystem in C++

Though the C++ standard library has no channel, we can count off several mature community libraries offering similar functionality:

- **Boost.Asio**'s `experimental::channel`: Boost is experimentally introducing channels; the API style approaches Go's channels but integrates with Asio's executor model.
- **cppcoro** (Lewis Baker): mainly a coroutine library, but its `single_consumer_async_queue` and `static_thread_pool` can build channel semantics.
- **Folly** (Facebook/Meta): `folly/ProducerConsumerQueue.h` provides a high-performance single-producer single-consumer lock-free queue usable as a channel's underpinning.
- **moodycamel::ConcurrentQueue**: a high-performance multi-producer multi-consumer lock-free queue, the underpinning of many high-performance channel implementations ([SPSC and MPMC Lock-Free Queues](03-lock-free-queues) analyzed its design).

If you need channels in a serious project, prefer Boost.Asio's experimental channel or a moodycamel-based wrapper over building from scratch as we did — our implementation serves teaching purposes, and its performance and fairness under high concurrency leave much room for optimization.

## Where to Go from Here

In this piece we set out from CSP's theory and sorted its core differences from the Actor model — anonymous channels versus identity-bearing Actors, synchronous versus asynchronous communication, algebraic composition versus message protocols. Then we implemented a complete channel class in C++, covering unbuffered and buffered modes, close semantics, try_send/try_receive, and a simplified select. Finally, through the producer-consumer and pipeline cases, we showed how channels are used and worked through the criteria for choosing between channels and mutex/condition_variable.

With this, the readings section's "no shared memory" pair is complete. We spent two pieces exploring concurrency paradigms without shared memory — the Actor model and the CSP model. Both chase the same goal, eliminating the complexity shared state brings, but by different roads: the Actor decouples with identity and mailboxes, CSP with anonymous channels. In real engineering the two models often mix — inside an Actor system, the communication between Actors may well be implemented with channels.

To return to the required line, the next big topic is validating concurrent designs: how to locate and fix problems when your concurrent program misbehaves, how to measure the gains, how to adjust the design. Unit 6's [Finding the Races: Repetition Testing and TSan](../../chapter06-validating-designs/01-finding-concurrency-bugs), [Before Measuring, Set the Conditions: Serial Baselines and Measurement Traps](../../chapter06-validating-designs/03-measuring-benefits), and [How to Adjust the Design](../../chapter06-validating-designs/05-adjusting-design) — three pieces chained together teach the complete method; read on.

> The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); take it from `code/volumn_codes/vol5/ch07-actor-channel/`.

## Reference Resources

- [Communicating Sequential Processes — Hoare, 1978 (CACM)](https://dl.acm.org/doi/10.1145/359576.359585) — the original CSP paper
- [Communicating Sequential Processes — Hoare, 1985 (Book)](https://usingcsp.com/cspbook.pdf) — the complete CSP book, free online edition
- [CSP — Wikipedia](https://en.wikipedia.org/wiki/Communicating_sequential_processes) — a detailed history and theoretical introduction to CSP
- [Go Channel Types Specification](https://go.dev/ref/spec#Channel_types) — the official semantic definition of Go channels
- [Go Concurrency Patterns: Pipelines and cancellation (Go Blog)](https://go.dev/blog/pipelines) — the Go blog's official pipeline pattern tutorial
- [Share Memory By Communicating (Go Blog)](https://go.dev/blog/codelab-share) — Go's exposition of the CSP philosophy
- [Boost.Asio Experimental Channel](https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/composition/channel.html) — a channel implementation heading toward standardization in the C++ ecosystem
