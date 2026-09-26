---
chapter: 10
cpp_standard:
- 20
description: Practice message-passing concurrency with the Channel or Actor model, and master CSP, mailboxes, select, and cancellation semantics
difficulty: advanced
order: 6
prerequisites:
- 'Volume 5 ch07: Actor Model and CSP'
- 'Lab 1: Bounded Queue, Concurrent Cache and Sync Primitives'
- 'Lab 4: Coroutine Scheduler and Event Loop'
reading_time_minutes: 10
tags:
- host
- cpp-modern
- coroutine
- advanced
title: 'Lab 5: Channel or Actor Runtime'
translation:
  source: documents/vol5-concurrency/exercises/05-channel-actor.md
  source_hash: fedc8b88d082333492e650ecc9d6821d2a8093354f355d6e320245dc9f73a36d
  translated_at: '2026-09-26T09:31:55+00:00'
  engine: anthropic
  token_count: 6000
---
# Lab 5: Channel or Actor Runtime

## Objectives

The earlier Labs mostly trained shared-memory concurrency — multiple threads coordinating access to shared data through mutexes, atomics, and condition variables. This Lab takes a different angle: instead of letting several threads mutate the same data at once, we pass messages and ownership through channels or mailboxes. Data travels with the message, and at any given moment only one thread/actor holds the right to access it — eliminating data races at the root.

This Lab offers two routes. The recommended main line is the **Channel route** (clearer tests, and more reuse of Lab 1's queue); the Actor route is an extension for anyone who wants to stretch their design skills.

## Prerequisites

Before you start, make sure you have read the following sections:

- **ch07-01** Actor Model and Message Passing — the basic concepts and implementation of the Actor model
- **ch07-02** Channels and the CSP Model — CSP (Communicating Sequential Processes) and Go-style channels

## Environment Setup

Same as Lab 4 (C++20, Catch2 v3).

## Choosing a Route

### The Channel Route (Recommended)

Implement `Channel<T>`, supporting buffered channels, send/receive, close semantics, and a simplified select. Then use channels to build a pipeline (parse → transform → write).

### The Actor Route (Extension)

Implement `ActorSystem` and `ActorRef`, where each actor owns its own mailbox, with support for spawn, send, and stop. Build a ping-pong or chat room demo.

The rest of this Lab unfolds along the Channel route.

## Final Interface (Channel Route)

### `Channel<T>` — Message Channel

Member variables:

| Type | Member | Semantics |
|------|------|------|
| `std::queue<T>` | `buffer_` | Buffer |
| `mutable std::mutex` | `mutex_` | Protects internal state |
| `std::condition_variable` | `not_full_` | Wait condition for senders |
| `std::condition_variable` | `not_empty_` | Wait condition for receivers |
| `std::size_t` | `capacity_` | Buffer capacity (0 = unbuffered/synchronous channel) |
| `bool` | `closed_` | Close flag |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `Channel(size_t capacity = 1)` | Capacity of 0 means an unbuffered, synchronous channel | MS1 |
| send | `bool send(T item)` | Blocking send; returns false after close | MS1 |
| receive | `std::optional<T> receive()` | Blocking receive; returns nullopt when closed and empty | MS1 |
| try_send | `bool try_send(T item)` | Non-blocking send; returns false when full or closed | MS2 |
| try_receive | `std::optional<T> try_receive()` | Non-blocking receive; returns nullopt when empty | MS2 |
| close | `void close()` | Closes the channel and wakes all waiting threads | MS1 |
| is_closed | `bool is_closed() const` | Queries the close state | MS1 |
| len | `size_t len() const` | Number of elements in the buffer | MS1 |

### `channel_select` — Simplified select (Milestone 3)

| Signature | Description | Milestone |
|------|------|-----------|
| `optional<pair<size_t, T>> channel_select(vector<Channel<T>*>&)` | Picks one ready channel out of several and returns `(channel_index, value)` | MS3 |

## Milestone 1: Buffered Channel

### Objective

Implement `Channel<T>`'s `send` and `receive`, supporting buffered message passing. The close semantics work like `BoundedBlockingQueue`'s.

### Why

The channel is the core abstraction of the CSP (Communicating Sequential Processes) model. It looks a lot like `BoundedBlockingQueue` — a thread-safe blocking queue — but there is an important conceptual difference: a channel represents a communication endpoint, not merely a data structure. That distinction will come into play later, in select and in the pipeline.

### Implementation Guide

The good news: the machinery under `Channel<T>` is almost identical to Lab 1's `BoundedBlockingQueue<T>` — a mutex + two condition_variables + a close flag. If your Lab 1 implementation was correct, this milestone is mostly a rename-and-reinterface job.

One subtle difference is the notion of an unbuffered channel (capacity = 0). On an unbuffered channel, send and receive must both be ready before either can complete — the sender blocks until a receiver shows up, and the receiver blocks until a sender does. This is what gives the synchronous-handshake semantics. Implementation-wise, you can treat an unbuffered channel as a queue with capacity 0 — when send sees that capacity_ is 0, it goes straight into waiting, until a receive wakes it up.

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
    Channel<int> ch(0);  // unbuffered
    std::atomic<int> value{0};
    std::atomic<bool> sent{false};

    JoiningThread sender([&]() {
        ch.send(42);
        sent.store(true);
    });

    // wait a short while and confirm that send is blocked
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE_FALSE(sent.load());

    // send completes only once a receive pairs with it
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

    REQUIRE_FALSE(ch.send(3));     // cannot send after close
    REQUIRE(ch.receive() == 1);    // data already sent can still be received
    REQUIRE(ch.receive() == 2);
    REQUIRE(ch.receive() == std::nullopt);  // nullopt once drained
}
```

## Milestone 2: try_send, try_receive, and Non-blocking Operations

### Objective

Implement `try_send` and `try_receive` — non-blocking versions that return success or failure immediately.

### Why

Blocking send/receive is too heavy in many scenarios — sometimes you just want to "take the data if it's there, otherwise go do something else." Non-blocking operations give the caller a chance to adopt a different strategy when there is no data, instead of waiting passively. The implementation of select later on will also lean on try_receive.

### Implementation Guide

`try_send` is simply: lock, then check whether the buffer is full — return false if full, otherwise push the item in and notify. `try_receive` checks whether the buffer is empty — return nullopt if empty, otherwise pop the item out and notify.

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
    REQUIRE_FALSE(ch.try_send(3));  // full

    REQUIRE(ch.try_receive() == 1);
    REQUIRE(ch.try_receive() == 2);
    REQUIRE(ch.try_receive() == std::nullopt);  // empty
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

Implement `channel_select`: pick one channel that has data ready to read out of several, and return `(channel_index, value)`. If every channel is empty, block and wait.

### Why

select is the most powerful composition primitive in the CSP model — it lets a coroutine/thread wait on several event sources at once and handle whichever becomes ready first. Go's `select` statement is the best-known implementation. In C++ we have no language-level select, but we can emulate one with polling + condition_variable.

### Implementation Guide

The simplest implementation is polling: walk through all the channels and call `try_receive` on each. If one succeeds, return. If they are all empty, `sleep` for a short while and retry.

A more efficient implementation registers a callback for each channel — when a channel gets new data, it wakes the select. But that requires adding a notification mechanism to Channel, which raises the complexity. For this Lab, get the polling version working first, confirm it is correct, and only then consider optimizing.

```cpp

optional<pair<size_t, T>> channel_select(
    vector<Channel<T>*>& channels)
{
    while (true) {
        for (size_t i = 0; i < channels.size(); ++i) {
            auto val = channels[i]->try_receive();
            if (val) return make_pair(i, move(*val));
        }
        // check whether all channels are closed
        bool all_closed = true;
        for (auto* ch : channels) {
            if (!ch->is_closed()) all_closed = false;
        }
        if (all_closed) return nullopt;

        // wait briefly, then retry
        this_thread::sleep_for(milliseconds(1));
    }
}

```

Pitfall warning: the polling implementation has poor CPU utilization — it keeps burning CPU even when there is no data. A production-grade implementation should use a condition_variable or epoll for genuine wait-and-wake. For teaching purposes, though, polling is enough to demonstrate the semantics of select.

### Verification

```cpp
TEST_CASE("Milestone 3: select picks ready channel",
          "[lab5][milestone3]")
{
    Channel<int> ch1(5);
    Channel<int> ch2(5);

    ch2.send(42);  // only ch2 has data

    std::vector<Channel<int>*> channels = {&ch1, &ch2};
    auto result = channel_select(channels);

    REQUIRE(result.has_value());
    REQUIRE(result->first == 1);   // the index of ch2
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

## Milestone 4: The Pipeline Pattern

### Objective

Build a pipeline out of channels: parse → transform → write. Each stage is an independent thread/coroutine, passing data along through channels.

### Why

The pipeline is the most classic use case for channels. It splits a complex processing flow into multiple independent stages, each responsible for exactly one thing, with the stages connected by channels. The advantages of this design: each stage can tune its concurrency independently (parse can be single-threaded while transform runs multi-threaded), and rate differences between stages are absorbed naturally by the channel's buffering (backpressure).

### Implementation Guide

A simple pipeline has three stages and two channels:

```cpp
Channel<string> raw_data(16);     // parse output
Channel<string> transformed(16);  // transform output

// Stage 1: parse — reads raw data from the source, parses it, and sends it to raw_data
// Stage 2: transform — reads from raw_data, transforms, and sends to transformed
// Stage 3: write — reads from transformed and writes to the destination

// each stage is an independent thread function
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

Pitfall warning: the shutdown order of a pipeline matters. The upstream stage must `close()` its output channel after processing all the data, so the downstream stage can exit naturally once `receive` returns `nullopt`. If you forget the `close()`, the downstream stage blocks forever.

### Verification

```cpp
TEST_CASE("Milestone 4: three-stage pipeline processes data",
          "[lab5][milestone4]")
{
    Channel<int> stage1_out(8);
    Channel<std::string> stage2_out(8);

    // Stage 1: generate numbers and double them
    JoiningThread s1([&]() {
        for (int i = 1; i <= 20; ++i) {
            stage1_out.send(i * 2);
        }
        stage1_out.close();
    });

    // Stage 2: convert to strings
    JoiningThread s2([&]() {
        while (auto val = stage1_out.receive()) {
            stage2_out.send("item_" + std::to_string(*val));
        }
        stage2_out.close();
    });

    // Stage 3: collect the results
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
- [ ] The close semantics are correct: no send after close, data already in the channel can still be received
- [ ] The unbuffered channel implements the synchronous handshake correctly
- [ ] try_send/try_receive non-blocking behavior is correct
- [ ] select can pick the one ready channel out of several
- [ ] select returns nullopt after all channels are closed
- [ ] The pipeline's shutdown order is correct and nothing deadlocks
- [ ] All tests report no data races under TSan
- [ ] You can explain the Channel approach's advantages over a mutex-based design (message passing eliminates shared state) and its costs (the overhead of copying or moving data)
- [ ] You can describe the similarities and differences between Channel's close semantics and Lab 1's BoundedBlockingQueue close semantics
- [ ] If you took the Actor route, you can compare the design trade-offs between Channel and Actor
