---
title: 'Lab 5: Channel or Actor Runtime'
description: Practice message-passing concurrency using the Channel or Actor pattern,
  and master CSP, mailbox, select, and cancellation semantics.
chapter: 10
order: 6
difficulty: advanced
tags:
- host
- cpp-modern
- coroutine
- advanced
cpp_standard:
- 20
reading_time_minutes: 13
prerequisites:
- '卷五 ch07: Actor 与 Channel'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
- 'Lab 4: Coroutine Scheduler and Event Loop'
translation:
  source: documents/vol5-concurrency/exercises/05-channel-actor.md
  source_hash: 2f161479dabe8697da6f7cd6cec5cf86bfd93f87d2b234b1b045dd56e0978139
  translated_at: '2026-05-26T11:49:08.582591+00:00'
  engine: anthropic
  token_count: 2608
---
# Lab 5: Channel or Actor Runtime

## Objectives

Previous labs focused on shared-memory concurrency—multiple threads coordinating access to shared data via mutex, atomic, and condition variables. In this lab, we take a different approach: instead of having multiple threads modify the same data simultaneously, we pass messages and ownership through channels or mailboxes. Data travels with the message, and only one thread/actor has access to the data at any given time—eliminating data races at their root.

This lab offers two tracks. The main track recommends the **Channel route** (clearer tests, more reuse from Lab 1's queue), while the Actor route is suitable as an extension for those who want to challenge their design skills.

## Prerequisites

Before starting, make sure you have read the following chapters:

- **ch07-01**: Actor Model and Message Passing — Basic concepts and implementation of the Actor model
- **ch07-02**: Channel and the CSP Model — CSP (Communicating Sequential Processes), Go-style channel

## Environment Setup

Same as Lab 4 (C++20, Catch2 v3).

## Track Selection

### Channel Track (Recommended)

Implement `Channel<T>`, supporting buffered channels, send/receive, close semantics, and a simplified select. Use channels to implement a pipeline (parse → transform → write).

### Actor Track (Extension)

Implement `ActorSystem` and `ActorRef`, where each actor owns its own mailbox, supporting spawn, send, and stop. Implement a ping-pong or chat room demo.

Below, we use the Channel track as the main thread.

## Final Interface (Channel Track)

### `Channel<T>` — Message Channel

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::queue<T>` | `buffer_` | Buffer |
| `mutable std::mutex` | `mutex_` | Protects internal state |
| `std::condition_variable` | `not_full_` | Sender wait condition |
| `std::condition_variable` | `not_empty_` | Receiver wait condition |
| `std::size_t` | `capacity_` | Buffer capacity (0 = unbuffered/synchronous channel) |
| `bool` | `closed_` | Closed flag |

Interface:

| Method | Signature | Description | Milestone |
|--------|-----------|-------------|-----------|
| Constructor | `Channel(size_t capacity = 1)` | A capacity of 0 means an unbuffered synchronous channel | MS1 |
| send | `bool send(T item)` | Blocking send; returns false after close | MS1 |
| receive | `std::optional<T> receive()` | Blocking receive; returns nullopt when closed and empty | MS1 |
| try_send | `bool try_send(T item)` | Non-blocking send; returns false if full or already closed | MS2 |
| try_receive | `std::optional<T> try_receive()` | Non-blocking receive; returns nullopt if empty | MS2 |
| close | `void close()` | Closes the channel, wakes up all waiting threads | MS1 |
| is_closed | `bool is_closed() const` | Queries the closed state | MS1 |
| len | `size_t len() const` | Number of elements in the buffer | MS1 |

### `channel_select` — Simplified select (Milestone 3)

| Signature | Description | Milestone |
|-----------|-------------|-----------|
| `optional<pair<size_t, T>> channel_select(vector<Channel<T>*>&)` | Selects one ready channel from multiple channels, returns `(channel_index, value)` | MS3 |

## Milestone 1: Buffered Channel

### Objective

Implement `Channel<T>`'s `send` and `receive`, supporting buffered message passing. The close semantics are similar to `BoundedBlockingQueue`.

### Why

A channel is the core abstraction of the CSP (Communicating Sequential Processes) model. It looks a lot like a `BoundedBlockingQueue`—a thread-safe blocking queue—but there is an important conceptual distinction: a channel represents a "communication endpoint," not just a data structure. This distinction becomes apparent in the later select and pipeline implementations.

### Implementation Guide

The good news is that the underlying implementation of `Channel<T>` is almost identical to Lab 1's `BoundedBlockingQueue<T>`—a mutex + two condition_variables + a closed flag. If your Lab 1 implementation is correct, this milestone is mostly a matter of "changing the name and interface."

One subtle difference is the concept of an "unbuffered channel" (capacity = 0). For an unbuffered channel, both send and receive must be ready simultaneously to complete—the sender blocks until a receiver arrives, and the receiver blocks until a sender arrives. This implements "synchronous handshake" semantics. In practice, you can treat an unbuffered channel as a queue with a capacity of 0—when send finds capacity_ to be 0, it immediately enters a wait state until a receive wakes it up.

### Verification

```cpp
TEST_CASE("Milestone 1: channel send and receive",
          "[lab5][milestone1]")
{
    Channel<int> ch(10);

    JoiningThread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            ch.send(i);
        }
        ch.close();
    });

    std::vector<int> received;
    while (auto val = ch.receive()) {
        received.push_back(*val);
    }

    REQUIRE(received.size() == 100);
    REQUIRE(received[0] == 0);
    REQUIRE(received[99] == 99);
}

TEST_CASE("Milestone 1: unbuffered channel blocks until paired",
          "[lab5][milestone1]")
{
    Channel<int> ch(0);  // 无缓冲
    std::atomic<int> value{0};
    std::atomic<bool> sent{false};

    JoiningThread sender([&]() {
        ch.send(42);
        sent.store(true);
    });

    // 等一小段时间，确认 send 阻塞了
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(sent.load());

    // receive 配对后 send 才完成
    auto val = ch.receive();
    REQUIRE(val.has_value());
    REQUIRE(*val == 42);
}

TEST_CASE("Milestone 1: close semantics",
          "[lab5][milestone1]")
{
    Channel<int> ch(5);
    ch.send(1);
    ch.send(2);
    ch.close();

    REQUIRE_FALSE(ch.send(3));     // 关闭后不能 send
    REQUIRE(ch.receive() == 1);    // 已有数据仍可 receive
    REQUIRE(ch.receive() == 2);
    REQUIRE(ch.receive() == std::nullopt);  // 耗尽后 nullopt
}
```

## Milestone 2: try_send, try_receive, and Non-blocking Operations

### Objective

Implement `try_send` and `try_receive`—non-blocking versions that immediately return success or failure.

### Why

Blocking send/receive is too heavy in many scenarios—you might just want to "take data if it's there, otherwise do something else." Non-blocking operations give the caller a chance to adopt alternative strategies when no data is available, rather than passively waiting. The later select implementation will also use try_receive.

### Implementation Guide

`try_send` simply locks, checks if the buffer is full—returns false if full, otherwise pushes the data in and notifies. `try_receive` checks if the buffer is empty—returns nullopt if empty, otherwise pops the data out and notifies.

```cpp
bool try_send(T item) {
    lock_guard lock(mutex_);
    if (closed_ || buffer_.size() >= capacity_) return false;
    buffer_.push(move(item));
    not_empty_.notify_one();
    return true;
}
```

### Verification

```cpp
TEST_CASE("Milestone 2: try_send and try_receive",
          "[lab5][milestone2]")
{
    Channel<int> ch(2);

    REQUIRE(ch.try_send(1));
    REQUIRE(ch.try_send(2));
    REQUIRE_FALSE(ch.try_send(3));  // 满了

    REQUIRE(ch.try_receive() == 1);
    REQUIRE(ch.try_receive() == 2);
    REQUIRE(ch.try_receive() == std::nullopt);  // 空了
}

TEST_CASE("Milestone 2: try operations on empty channel",
          "[lab5][milestone2]")
{
    Channel<int> ch(5);
    REQUIRE(ch.try_receive() == std::nullopt);
    REQUIRE(ch.try_send(42));
    REQUIRE(ch.try_receive() == 42);
}
```

## Milestone 3: Simplified select

### Objective

Implement `channel_select` to select one channel with data available to read from multiple channels, returning `(channel_index, value)`. If all channels are empty, block and wait.

### Why

Select is the most powerful combinator primitive in the CSP model—it allows a coroutine/thread to wait on multiple event sources simultaneously, processing whichever becomes ready first. Go's `select` statement is the most famous implementation. In C++, we don't have a language-level select, but we can simulate it using polling + condition_variable.

### Implementation Guide

The simplest implementation is polling: iterate through all channels, calling `try_receive` on each. If one succeeds, return. If all are empty, `sleep` for a short period and retry.

A more efficient implementation would register a callback for each channel—waking up select when a channel has new data. However, this requires adding a notification mechanism to the Channel class, resulting in higher complexity. For this lab, we recommend implementing it with polling first, confirming functional correctness before considering optimizations.

```cpp

optional<pair<size_t, T>> channel_select(
    vector<Channel<T>*>& channels)
{
    while (true) {
        for (size_t i = 0; i < channels.size(); ++i) {
            auto val = channels[i]->try_receive();
            if (val) return make_pair(i, move(*val));
        }
        // 检查是否所有 channel 都关闭了
        bool all_closed = true;
        for (auto* ch : channels) {
            if (!ch->is_closed()) all_closed = false;
        }
        if (all_closed) return nullopt;

        // 短暂等待后重试
        this_thread::sleep_for(milliseconds(1));
    }
}

```

Pitfall warning: The polling implementation has poor CPU utilization—it still consumes CPU when no data is available. A production-grade implementation should use condition_variable or epoll to achieve true wait-wake behavior. However, for educational purposes, polling is sufficient to demonstrate the semantics of select.

### Verification

```cpp
TEST_CASE("Milestone 3: select picks ready channel",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);

    ch2.send(42);  // 只有 ch2 有数据

    std::vector<Channel<int>*> channels = {&ch1, &ch2};
    auto result = channel_select(channels);

    REQUIRE(result.has_value());
    REQUIRE(result->first == 1);   // ch2 的索引
    REQUIRE(result->second == 42);
}

TEST_CASE("Milestone 3: select blocks until data available",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);

    std::vector<Channel<int>*> channels = {&ch1, &ch2};

    JoiningThread producer([&]() {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50));
        ch1.send(99);
    });

    auto result = channel_select(channels);
    REQUIRE(result.has_value());
    REQUIRE(result->first == 0);
    REQUIRE(result->second == 99);
}

TEST_CASE("Milestone 3: select returns nullopt when all closed",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);
    ch1.close();
    ch2.close();

    std::vector<Channel<int>*> channels = {&ch1, &ch2};
    auto result = channel_select(channels);
    REQUIRE_FALSE(result.has_value());
}
```

## Milestone 4: Pipeline Pattern

### Objective

Use channels to implement a pipeline: parse → transform → write. Each stage is an independent thread/coroutine, passing data through channels.

### Why

The pipeline is the most classic use case for channels. It breaks down a complex processing flow into multiple independent stages, where each stage is responsible for only one thing, and stages are connected via channels. The advantages of this design are: each stage can independently adjust its concurrency level (parse can be single-threaded, transform can be multi-threaded), and rate differences between stages are naturally absorbed by the channel's buffer (backpressure).

### Implementation Guide

A simple pipeline has three stages and two channels:

```cpp
Channel<string> raw_data(16);     // parse 输出
Channel<string> transformed(16);  // transform 输出

// Stage 1: parse — 从数据源读取原始数据，解析后发给 raw_data
// Stage 2: transform — 从 raw_data 读取，转换后发给 transformed
// Stage 3: write — 从 transformed 读取，写入目标

// 每个 stage 是一个独立的线程函数
void parse_stage(Channel<string>& output) {
    for (...) {
        output.send(parsed_item);
    }
    output.close();
}

void transform_stage(Channel<string>& input,
                     Channel<string>& output) {
    while (auto val = input.receive()) {
        output.send(transform(*val));
    }
    output.close();
}

void write_stage(Channel<string>& input) {
    while (auto val = input.receive()) {
        write(*val);
    }
}
```

Pitfall warning: The shutdown order of the pipeline is critical. The upstream stage must `close()` its output channel after processing all data, so that the downstream stage can naturally exit when `receive` returns `nullopt`. If you forget to `close()`, the downstream stage will block forever.

### Verification

```cpp
TEST_CASE("Milestone 4: three-stage pipeline processes data",
          "[lab5][milestone4]")
{
    Channel<int> stage1_out(8);
    Channel<std::string> stage2_out(8);

    // Stage 1: 生成数字并翻倍
    JoiningThread s1([&]() {
        for (int i = 1; i <= 20; ++i) {
            stage1_out.send(i * 2);
        }
        stage1_out.close();
    });

    // Stage 2: 转成字符串
    JoiningThread s2([&]() {
        while (auto val = stage1_out.receive()) {
            stage2_out.send("item_" + std::to_string(*val));
        }
        stage2_out.close();
    });

    // Stage 3: 收集结果
    std::vector<std::string> results;
    while (auto val = stage2_out.receive()) {
        results.push_back(*val);
    }

    REQUIRE(results.size() == 20);
    REQUIRE(results[0] == "item_2");
    REQUIRE(results[19] == "item_40");
}
```

## Self-Check List

- [ ] Channel's send/receive use predicate waits
- [ ] Close semantics are correct: cannot send after close, existing data can still be received
- [ ] Unbuffered channel correctly implements synchronous handshake
- [ ] try_send/try_receive exhibit correct non-blocking behavior
- [ ] select can pick a ready channel from multiple channels
- [ ] select returns nullopt after all channels are closed
- [ ] Pipeline shutdown order is correct, no deadlocks
- [ ] All tests pass under TSan with no data race reports
- [ ] Can explain the advantages of Channel compared to mutex-based approaches (message passing eliminates shared state) and the costs (overhead of data copy or move)
- [ ] Can describe the similarities and differences between Channel's close semantics and Lab 1's BoundedBlockingQueue close semantics
- [ ] If the Actor track was completed as an extension, can compare the design trade-offs between Channel and Actor
