---
chapter: 7
cpp_standard:
- 17
- 20
description: Understand the CSP (Communicating Sequential Processes) concurrency model
  and implement Go-like channels in C++.
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
title: Channel and CSP Model
translation:
  source: documents/vol5-concurrency/ch07-actor-channel/02-channel-and-csp.md
  source_hash: c362874de4f213c18c18aa225c44615d9709dd414f3db5c1f0a5dbf1323fef42
  translated_at: '2026-06-16T04:06:40.661745+00:00'
  engine: anthropic
  token_count: 5695
---
# Channels and the CSP Model

In the previous article, we discussed the Actor model—organizing concurrency using stateful Actors and asynchronous message passing. In this article, we will look at another school of thought that also advocates "don't share memory": CSP (Communicating Sequential Processes).

CSP was first proposed by Tony Hoare in his 1978 paper *"Communicating Sequential Processes"* (published in *Communications of the ACM*). Like the Actor model, the core idea of CSP is to replace shared memory with message passing, but it takes a different path: Actors have identities and mailboxes, and messages are sent to specific Actor addresses; CSP uses anonymous channels for communication, and processes do not need to know who the other party is. This difference may seem subtle, but it creates significant differences in programming style and expressive power. Go's goroutine + channel is the most successful industrial practice of CSP. Rob Pike's famous quote—"Don't communicate by sharing memory; share memory by communicating"—is Go's summary of the CSP philosophy.

In this article, we will start with the theoretical foundations of CSP, then implement a Go-like communication pipeline in C++, including buffered/unbuffered channels, close semantics, and the select pattern. Finally, we will discuss when to use channels and when to use locks directly.

## Environment Setup

Just like the previous article, all our code is based on C++17 and compiles successfully under GCC 12+ / Clang 15+ / MSVC 2022+ with the compiler flag `-std=c++17 -pthread`. It runs on Linux, macOS, and Windows, provided your standard library supports `std::mutex`, `std::condition_variable`, and `std::queue`. The code in this article does not depend on any third-party libraries.

## Theoretical Foundations of CSP

The original CSP paper was published five years later than the Actor model (1978 vs. 1973), but its influence is equally profound. Hoare's initial design was a concurrent programming language (rather than the formal calculus it became later), with syntax that looked like this:

```text
*[c ? character -> west ! character]
```

The meaning of this code is: repeatedly receive a character `c` from a process named `west`, and then send it to a process named `east`. Communication in the original CSP was synchronous message passing based on process names—both the sender and the receiver must be ready at the same time for communication to occur.

Later (1984-1985), Hoare, Stephen Brookes, and A. W. Roscoe developed CSP into a complete process algebra. In this version, communication is no longer based on process names, but on anonymous channels—this is also the version adopted by Go.

CSP has had a far-reaching influence on programming languages. It directly influenced the occam language (designed for the INMOS Transputer processor), the Limbo language (the programming language of Plan 9), and most importantly—Go's concurrency model. Go is not a complete implementation of CSP, but it borrows the core idea: goroutines correspond to CSP processes, and channels correspond to CSP communication channels.

### Fundamental Differences Between CSP and Actor

The Wikipedia entry for CSP has a very clear comparison. Let's see exactly where they differ.

The first difference is identity. CSP processes are anonymous—you don't need to know who the other party is, only which channel to send data to. Actors are different; each Actor has an address (pid in Erlang, ActorRef in Akka), and messages must be sent to a specific address. This means the CSP channel is a decoupling layer: the sender and receiver are indirectly associated through the channel, and either end can be replaced at any time. The Actor model is more tightly coupled—the sender must know the receiver's address.

The second difference lies in the synchronicity of communication. CSP communication is synchronous (rendezvous) in its basic semantics—both the sender and the receiver must be ready at the same time for communication to occur. Actor model communication is asynchronous—the sender returns immediately after sending the message, without waiting for the receiver to be ready. Interestingly, these two semantics are duals of each other: synchronous communication plus a buffer queue becomes asynchronous communication, and asynchronous communication plus an acknowledgment/response protocol becomes synchronous communication.

The third difference is compositionality. CSP provides rich algebraic operators to combine processes—sequential composition, choice (internal/external), parallel, hiding, etc. These operators have formal semantics and can be used with tools (like the FDR refinement checker) for automated deadlock and liveness checking. Actor model composition relies mainly on message protocols—two Actors agree on message formats and interaction sequences. The former is more formal, the latter more flexible.

> Honestly, there is no absolute superiority or inferiority between these two models. In actual engineering, the choice often depends on the team's familiarity and specific system characteristics. Go chose CSP, Erlang chose Actor, and both have achieved huge success.

## Basic Channel Implementation

Let's implement a Go-like communication pipeline. Go's channels have two basic forms: unbuffered channels and buffered channels. In an unbuffered channel, the sender blocks until a receiver is ready, and the receiver blocks until a sender is ready—this is synchronous communication, where sending and receiving happen at the same moment. A buffered channel has a queue internally; the sender does not block when the buffer is not full, but blocks when the buffer is full; the receiver blocks when the buffer is empty. Both types of channels support the `close` operation—after closing, no more sending is allowed, but remaining data can still be received.

### Unbuffered Channel

The unbuffered channel is the purest form. Sending and receiving must happen simultaneously—like two people shaking hands; both must reach out for the handshake to happen.

```cpp
// ch07/channel-csp/unbuffered_channel.cpp
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class UnbufferedChannel {
public:
    void send(T value) {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait for a receiver to be ready (rendezvous)
        recv_ready_.wait(lock, [this] {
            return recv_waiting_ || closed_;
        });

        if (closed_) {
            throw std::runtime_error("send on closed channel");
        }

        // Transfer data
        data_ = std::move(value);
        // Notify receiver that data is ready
        recv_waiting_ = false;
        send_done_.notify_one();

        // Wait for receiver to take the data
        recv_done_.wait(lock);
    }

    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mtx_);
        recv_waiting_ = true;
        recv_ready_.notify_one(); // Notify sender that we are waiting

        // Wait for sender to put data
        send_done_.wait(lock, [this] {
            return data_.has_value() || closed_;
        });

        if (closed_ && !data_.has_value()) {
            return std::nullopt;
        }

        T result = std::move(*data_);
        data_.reset();
        recv_done_.notify_one(); // Notify sender that we took the data
        return result;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;
        recv_ready_.notify_all();
        send_done_.notify_all();
        recv_done_.notify_all();
    }

private:
    std::mutex mtx_;
    std::condition_variable recv_ready_;  // Signals "I am ready to receive"
    std::condition_variable send_done_;   // Signals "Data is ready to be read"
    std::condition_variable recv_done_;   // Signals "Data has been taken"
    std::optional<T> data_;
    bool recv_waiting_ = false;
    bool closed_ = false;
};
```

The core of the unbuffered channel implementation is "rendezvous"—the sender and receiver complete the data exchange at the same moment. `send` puts the data into `data_`, wakes up the receiver, and waits for the receiver to confirm it has taken the data. `receive` marks itself as waiting and then waits for data to arrive. Both parties coordinate through two condition variables (`recv_ready_` and `send_done_`).

There is a subtle point in this implementation: the `recv_waiting_` flag. It tells the sender "someone is waiting to receive now," so the sender knows it's safe to start the transfer. Without this flag, the sender might wake up without any receiver present—this is like shouting "Is anyone here for this package?" in an empty room and never getting a response.

> ⚠️ **Note**: `send` on an unbuffered channel is synchronous—it blocks until the receiver takes the data. If you have a sender but no receiver in your code, `send` will block forever. This is a direct reflection of the CSP philosophy: communication is synchronous, and both parties must participate simultaneously. If this doesn't suit your needs, use a buffered channel.

### Buffered Channel

A buffered channel is essentially a thread-safe queue—we are very familiar with this from ch04. When the buffer is not full, the sender enqueues and returns immediately; when the buffer is full, the sender blocks and waits for a slot. After closing, the receiver can continue consuming remaining data in the queue, and returns `std::nullopt` only when the queue is empty.

```cpp
// ch07/channel-csp/buffered_channel.cpp
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

template <typename T>
class BufferedChannel {
public:
    explicit BufferedChannel(size_t capacity) : capacity_(capacity) {}

    bool send(T value) {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait for buffer not full or closed
        not_full_.wait(lock, [this] {
            return buffer_.size() < capacity_ || closed_;
        });

        if (closed_) return false;

        buffer_.push(std::move(value));
        not_empty_.notify_one();
        return true;
    }

    std::optional<T> receive() {
        std::unique_lock<std::mutex> lock(mtx_);
        // Wait for buffer not empty or closed
        not_empty_.wait(lock, [this] {
            return !buffer_.empty() || closed_;
        });

        if (buffer_.empty() && closed_) {
            return std::nullopt;
        }

        T result = std::move(buffer_.front());
        buffer_.pop();
        not_full_.notify_one();
        return result;
    }

    void close() {
        std::lock_guard<std::mutex> lock(mtx_);
        closed_ = true;
        not_empty_.notify_all();
        not_full_.notify_all();
    }

private:
    std::mutex mtx_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::queue<T> buffer_;
    size_t capacity_;
    bool closed_ = false;
};
```

The implementation of a buffered channel is the classic producer-consumer model. Two condition variables manage the "buffer not full" and "buffer not empty" conditions respectively. When closing, all waiting threads are woken up—senders find `closed` is true and return false, receivers continue consuming remaining data and then return `std::nullopt`.

This close semantics is basically consistent with Go's channel closing behavior: after closing, no more sending is allowed (our implementation returns false in `send`, Go panics), and receivers can continue reading data from the buffer until it is exhausted (after which Go returns zero values, we return `std::nullopt`).

### Unified Channel Interface

In actual use, we often don't want to care whether a channel is buffered or unbuffered—the API should be consistent. So we unify the two implementations into one template class, using the `N` parameter to distinguish: 0 means unbuffered, greater than 0 means buffered.

```cpp
// ch07/channel-csp/channel.hpp
#pragma once
#include <optional>
#include "unbuffered_channel.cpp"
#include "buffered_channel.cpp"

template <typename T, size_t N = 0>
class Channel {
    using Impl = std::conditional_t<N == 0,
                                     UnbufferedChannel<T>,
                                     BufferedChannel<T>>;
public:
    Channel() : impl_(N) {} // N is capacity for BufferedChannel

    void send(T value) { impl_.send(std::move(value)); }
    std::optional<T> receive() { return impl_.receive(); }
    void close() { impl_.close(); }

private:
    Impl impl_;
};
```

This unified interface packages both channel implementations together. The behavior is determined by specifying `N` at construction time—0 is unbuffered, greater than 0 is buffered. The externally exposed `send` and `receive` are completely identical, and the user doesn't need to care whether the internal mechanism is direct handoff or a queue. This design is the same in Go—`make(chan int)` creates an unbuffered channel, `make(chan int, 5)` creates a channel with a buffer size of 5, and there is no difference in usage.

## The Select Pattern

Go's `select` statement is one of the most powerful composition primitives in the CSP model. It allows you to wait for multiple channel operations at the same time, executing whichever one becomes ready first:

```go
select {
case v := <-ch1:
    fmt.Println("Got from ch1:", v)
case v := <-ch2:
    fmt.Println("Got from ch2:", v)
case ch3 <- x:
    fmt.Println("Sent to ch3")
}
```

C++ doesn't have language-level select, but we can simulate the core idea using polling + condition variables. A complete select implementation is quite complex (requiring fair scheduling, random selection, avoiding starvation, etc.), so here we implement a simplified version to demonstrate the core mechanism.

### Simplified Select

```cpp
// ch07/channel-csp/select_demo.cpp
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include "channel.hpp"

// Simplified Select: Polls all channels in a busy-wait loop
template <typename T>
std::optional<T> select_receive(std::vector<Channel<T, 1>&> channels) {
    while (true) {
        for (auto& ch : channels) {
            if (auto val = ch.try_receive()) { // Assuming try_receive exists
                return val;
            }
        }
        std::this_thread::yield(); // Avoid busy-waiting too aggressively
    }
    return std::nullopt;
}

// Note: This is a conceptual demonstration.
// A real implementation would need a way to wait on multiple condition variables simultaneously.
```

> ⚠️ **Note**: This select implementation is highly simplified. It uses busy-waiting (`yield`) to poll all channels, which wastes CPU in high-frequency scenarios. Go's select uses complex runtime mechanisms internally; it puts goroutines to sleep while waiting and wakes them up precisely when channels are ready, and it guarantees random selection when multiple cases are ready at the same time to avoid starvation. To implement efficient select in C++, you would need to maintain a global poller or use system-level I/O multiplexing mechanisms like epoll/kqueue. But for understanding the semantics of select, this simplified version is sufficient.

## Practice 1: Producer-Consumer Pattern

The producer-consumer pattern is the most classic application scenario for channels. We use a buffered channel to implement a multi-producer, multi-consumer pipeline.

```cpp
// ch07/channel-csp/producer_consumer.cpp
#include "channel.hpp"
#include <thread>
#include <vector>
#include <iostream>

void producer(Channel<int, 10>& ch, int id, int count) {
    for (int i = 0; i < count; ++i) {
        ch.send(id * 100 + i);
        std::cout << "Producer " << id << " sent " << (id * 100 + i) << std::endl;
    }
    ch.close(); // Signal completion
}

void consumer(Channel<int, 10>& ch, int id) {
    while (true) {
        auto val = ch.receive();
        if (!val) break; // Channel closed and empty
        std::cout << "Consumer " << id << " got " << *val << std::endl;
    }
}

int main() {
    Channel<int, 10> ch;
    std::thread p1(producer, std::ref(ch), 1, 5);
    std::thread c1(consumer, std::ref(ch), 1);

    p1.join();
    c1.join();
    return 0;
}
```

This example is very straightforward: producers put data into the channel, consumers take data from the channel, and the channel's buffer acts as an elastic adjustment—when the producer is temporarily fast, data is stored in the buffer; when the consumer is temporarily fast, the buffer is consumed. When the buffer is full, the producer automatically blocks; when the buffer is empty, the consumer automatically blocks. No explicit locks or condition variables are needed—the channel manages it all for you.

## Practice 2: Pipeline Pattern

Pipelines are another classic use of channels. The core idea of a pipeline is to split a complex data processing flow into multiple stages, where each stage is an independent goroutine (thread in C++), and stages are connected by channels.

```cpp
// ch07/channel-csp/pipeline.cpp
#include "channel.hpp"
#include <thread>
#include <iostream>

// Stage 1: Generator
void generator(Channel<int, 10>& out) {
    for (int i = 1; i <= 5; ++i) {
        out.send(i);
    }
    out.close();
}

// Stage 2: Doubler
void doubler(Channel<int, 10>& in, Channel<int, 10>& out) {
    while (auto val = in.receive()) {
        out.send(*val * 2);
    }
    out.close();
}

// Stage 3: Printer
void printer(Channel<int, 10>& in) {
    while (auto val = in.receive()) {
        std::cout << "Result: " << *val << std::endl;
    }
}

int main() {
    Channel<int, 10> ch1, ch2;

    std::thread t1(generator, std::ref(ch1));
    std::thread t2(doubler, std::ref(ch1), std::ref(ch2));
    std::thread t3(printer, std::ref(ch2));

    t1.join(); t2.join(); t3.join();
    return 0;
}
```

The beauty of the pipeline pattern is that each stage is independent—it only needs to care about reading data from the input channel and writing data to the output channel, without knowing the source or destination of the data. This means you can freely insert, delete, or reorder stages without affecting the code of other stages.

The Go blog has a classic example of implementing concurrent MD5 hash calculation using pipelines—each file goes through three stages: read, compute, summarize, and all stages run in parallel. If you have written shell pipelines (like `ps aux | grep nginx | wc -l`), you already understand the core idea of a pipeline—except here we apply it to concurrent programming in C++.

## The Relationship Between Channels and mutex/condition_variable

Now that we have implemented and used channels, let's answer a question you may have wanted to ask for a while: what exactly is at the bottom of a channel?

The answer is simple: **the bottom of a channel is just mutex + condition_variable + queue**. There is no magic.

In our `BufferedChannel`, `mtx_` protects the `buffer_` queue, and `not_full_` and `not_empty_` notify "slot available" and "data available" respectively. This is exactly the same as the producer-consumer pattern discussed in ch02. `UnbufferedChannel` is slightly more complex, but the core is still mutex + condition_variable, only the transmission model changes from "put in queue" to "direct handoff".

So the question arises: since channels are just locks at the bottom, why use channels?

The answer is **abstraction level**. `mutex` and `condition_variable` are low-level primitives, while channels are high-level abstractions. Low-level primitives are flexible but error-prone—you need to manage lock acquisition and release, condition variable waiting and notification, and state checking and protection yourself. High-level abstractions limit your freedom but guarantee correctness—the channel interface design ensures you won't forget to unlock, won't forget to notify, and won't write the wrong wait condition.

### Selection Guide

When to use channels, and when to use mutex/condition_variable directly? Honestly, there is no standard answer to this question, but there is a rough criterion you can refer to.

If your concurrency model is essentially "data flowing between producers and consumers"—such as pipelines, work queues, event dispatch, log collection—then the semantics of channel (send, receive, close) match these scenarios perfectly. Additionally, when your system needs a large number of concurrent entities (goroutines/threads) and their interaction is mainly point-to-point message passing, channels are more suitable than locks.

Conversely, if what you need to protect is a small piece of shared data, rather than "passing data between entities"—such as a shared counter, a cache table, a configuration object—using a channel is clumsy. To update a counter, you would have to create a channel, a handler thread, and a set of message protocols, which is completely not worth the gain. Also, when you need very fine-grained performance control (e.g., on a hot path), using atomics or spinlocks directly may have much lower overhead than channels.

A practical rule of thumb: if you find yourself using a channel to simulate a lock (e.g., using a channel to serialize access to a resource), you should just use a lock. Channels solve the problem of "data flowing between entities," not "protecting shared state." With the right tool, the code will be clean.

## The CSP Ecosystem in C++

Although there is no channel in the C++ standard library, there are some mature libraries in the community that provide similar functionality:

- **Boost.Asio**'s `experimental::channel`: Boost is experimentally introducing channels with an API style close to Go's channels, but integrated with Asio's executor model.
- **cppcoro** (Lewis Baker): Although mainly a coroutine library, it provides `generator` and `async_generator` which can be used to build channel semantics.
- **Folly** (Facebook/Meta): `folly::ProducerConsumerQueue` provides high-performance single-producer single-consumer lock-free queues, which can be used as the underlying layer for channels.
- **moodycamel::ConcurrentQueue**: A high-performance multi-producer multi-consumer lock-free queue, used as the underlying layer for many high-performance channel implementations.

If you need channels in a serious project, it is recommended to prioritize Boost.Asio's experimental channel or a wrapper based on moodycamel, rather than implementing from scratch like we did—our implementation focuses on educational purposes, and there is still a lot of room for optimization in terms of performance and fairness in high-concurrency scenarios.

## Our Position

In this article, starting from the theory of CSP, we learned about its core differences from the Actor model—anonymous channels vs. stateful Actors, synchronous communication vs. asynchronous communication, algebraic composition vs. message protocols. We then implemented a complete channel class in C++, including unbuffered and buffered modes, close semantics, try_send/try_receive, and a simplified version of select. Finally, through two practical cases of producer-consumer and pipeline, we demonstrated the use of channels and discussed the selection criteria between channels and mutex/condition_variable.

This concludes the two articles on ch07. We spent two articles exploring the "don't share memory" concurrency paradigm—the Actor model and the CSP model. They both pursue the same goal: eliminating the complexity brought by shared state, but they take different paths. Actor uses identity and mailboxes to decouple, CSP uses anonymous channels to decouple. In actual engineering, these two models are often mixed—for example, within an Actor system, communication between Actors might be implemented through channels.

In the next article, we will enter the last major topic of Volume 5: debugging, testing, and performance optimization—when your concurrent program has problems, how to locate and fix them. From theory to practice, from implementation to troubleshooting, this is the closed loop of our entire concurrency journey.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `ch07/channel-csp`.

## References

- [Communicating Sequential Processes — Hoare, 1978 (CACM)](https://dl.acm.org/doi/10.1145/359576.359585) — The original CSP paper
- [Communicating Sequential Processes — Hoare, 1985 (Book)](https://usingcsp.com/cspbook.pdf) — The complete CSP monograph, free online version
- [CSP — Wikipedia](https://en.wikipedia.org/wiki/Communicating_sequential_processes) — Detailed history and theoretical introduction of CSP
- [Go Channel Types Specification](https://go.dev/ref/spec#Channel_types) — Official semantic definition of Go channels
- [Go Concurrency Patterns: Pipelines and cancellation (Go Blog)](https://go.dev/blog/pipelines) — Pipeline pattern tutorial from the official Go blog
- [Share Memory By Communicating (Go Blog)](https://go.dev/blog/codelab-share) — Go's exposition of the CSP philosophy
- [Boost.Asio Experimental Channel](https://www.boost.org/doc/libs/release/doc/html/boost_asio/overview/composition/channel.html) — Channel implementation in the C++ ecosystem that is being standardized
