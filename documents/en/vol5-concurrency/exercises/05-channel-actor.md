---
chapter: 10
cpp_standard:
- 20
description: Practice message-passing concurrency using Channel or Actor patterns,
  and master CSP, mailbox, select, and cancellation semantics.
difficulty: advanced
order: 6
prerequisites:
- '卷五 ch07: Actor 与 Channel'
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
  translated_at: '2026-06-16T04:07:46.735064+00:00'
  engine: anthropic
  token_count: 2604
---
# Lab 5: Channel or Actor Runtime

## Objectives

Previous labs focused on shared-memory concurrency—where multiple threads coordinate access to shared data using mutexes, atomics, and condition variables. In this lab, we take a different approach: instead of having multiple threads modify the same data simultaneously, we pass messages and ownership via channels or mailboxes. Data travels with the message, and only one thread/actor has access to the data at any given moment—fundamentally eliminating data races.

This lab offers two tracks. The main track is the **Channel track** (recommended for clearer tests and better reusability with the queue from Lab 1). The Actor track is suitable for those who want to challenge their design skills as an extension.

## Prerequisites

Before starting, ensure you have read the following chapters:

- **ch07-01**: Actor Model and Message Passing — Basic concepts and implementation of the Actor model.
- **ch07-02**: Channel and CSP Model — CSP (Communicating Sequential Processes), Go-style channels.

## Environment Setup

Same as Lab 4 (C++20, Catch2 v3).

## Track Selection

### Channel Track (Recommended)

Implement `Channel<T>`, supporting buffered channels, send/receive, close semantics, and a simplified select. Use channels to implement a pipeline (parse → transform → write).

### Actor Track (Extension)

Implement `Actor` and `Mailbox`, where each actor owns its mailbox, supporting spawn, send, and stop. Implement a ping-pong or chat room demo.

The following sections focus on the Channel track.

## Final Interface (Channel Track)

### `Channel<T>` — Message Channel

Member variables:

| Type | Member | Semantics |
|------|--------|-----------|
| `std::deque<T>` | `buffer_` | Buffer |
| `std::mutex` | `mtx_` | Protects internal state |
| `std::condition_variable` | `cv_send_` | Sender wait condition |
| `std::condition_variable` | `cv_recv_` | Receiver wait condition |
| `size_t` | `capacity_` | Buffer capacity (0 = unbuffered/synchronous channel) |
| `std::atomic<bool>` | `closed_` | Close flag |

Interface:

| Method | Signature | Description | Milestone |
|------|------|------|-----------|
| Constructor | `explicit Channel(size_t capacity = 0)` | Capacity of 0 means an unbuffered synchronous channel | MS1 |
| send | `bool send(T item)` | Blocking send; returns false after close | MS1 |
| receive | `std::optional<T> receive()` | Blocking receive; returns nullopt when closed and empty | MS1 |
| try_send | `bool try_send(T item)` | Non-blocking send; returns false if full or closed | MS2 |
| try_receive | `std::optional<T> try_receive()` | Non-blocking receive; returns nullopt if empty | MS2 |
| close | `void close()` | Closes the channel, wakes all waiting threads | MS1 |
| is_closed | `bool is_closed() const` | Query close status | MS1 |
| len | `size_t len() const` | Number of elements in buffer | MS1 |

### `select` — Simplified select (Milestone 3)

| Signature | Description | Milestone |
|------|------|-----------|
| `std::optional<SelectedIndex> select(std::vector<std::function<bool()>> ops)` | Selects one ready channel from multiple, returns `SelectedIndex` | MS3 |

## Milestone 1: Buffered Channel

### Objectives

Implement `Channel<T>`'s `send` and `receive` to support buffered message passing. The close semantics are similar to `BoundedBlockingQueue`.

### Why

The channel is the core abstraction of the CSP (Communicating Sequential Processes) model. It looks very similar to a thread-safe blocking queue, but there is a conceptual difference: a channel represents a "communication endpoint," not just a data structure. This distinction becomes apparent in later sections on select and pipelines.

### Implementation Guide

The good news is that the underlying implementation of `Channel<T>` is almost identical to the `BoundedBlockingQueue` from Lab 1—mutex + two condition_variables + a close flag. If your Lab 1 implementation was correct, this milestone is mostly "renaming and changing interfaces."

A subtle difference is the concept of an "unbuffered channel" (capacity = 0). For an unbuffered channel, send and receive must be ready simultaneously to complete—the sender blocks until a receiver arrives, and the receiver blocks until a sender arrives. This implements a "synchronous handshake" semantics. In implementation, you can treat an unbuffered channel as a queue with capacity 0—send sees `capacity_` is 0 and immediately waits, until a receive wakes it.

### Verification

```cpp
// Tests for basic send/receive/close semantics
```

## Milestone 2: try_send, try_receive, and Non-blocking Operations

### Objectives

Implement `try_send` and `try_receive`—non-blocking versions that return success or failure immediately.

### Why

Blocking send/receive is too heavy in many scenarios—you might just want to "take data if it's there, otherwise do something else." Non-blocking operations allow the caller to adopt other strategies when no data is available, instead of passively waiting. The later implementation of select will also use `try_receive`.

### Implementation Guide

`try_send` simply locks, checks if the buffer is full—returns false if full, otherwise pushes and notifies. `try_receive` checks if the buffer is empty—returns nullopt if empty, otherwise pops and notifies.

```cpp
// Implementation hints for try_send/try_receive
```

### Verification

```cpp
// Tests for non-blocking operations
```

## Milestone 3: Simplified select

### Objectives

Implement `select`, which chooses one channel with data available from multiple channels and returns `SelectedIndex`. If all channels are empty, block and wait.

### Why

Select is the most powerful combinator in the CSP model—it allows a coroutine/thread to wait for multiple event sources simultaneously, processing whichever becomes ready first. Go's `select` statement is the most famous implementation. In C++, we don't have language-level select, but we can simulate it with polling + condition_variable.

### Implementation Guide

The simplest implementation is polling: iterate through all channels, calling `try_receive` on each. If one succeeds, return. If all are empty, `sleep_for` a short while and retry.

A more efficient implementation involves registering a callback for each channel—waking select when the channel has new data. However, this requires adding notification mechanisms to `Channel`, increasing complexity. This lab suggests implementing polling first to verify functionality, then considering optimizations.

```cpp
// Implementation hints for select
```

Pitfall warning: The polling implementation has poor CPU utilization—it still consumes CPU when there is no data. A production-grade implementation should use `condition_variable` or `epoll` for true wait-wake semantics. However, for educational purposes, polling is sufficient to demonstrate the semantics of select.

### Verification

```cpp
// Tests for select functionality
```

## Milestone 4: Pipeline Pattern

### Objectives

Use channels to implement a pipeline: parse → transform → write. Each stage is an independent thread/coroutine, passing data through channels.

### Why

Pipeline is the classic application scenario for channels. It breaks down a complex processing flow into multiple independent stages, where each stage is responsible for only one thing, and stages are connected by channels. The advantage of this design is: each stage can independently adjust concurrency (parse can be single-threaded, transform can be multi-threaded), and rate differences between stages are naturally absorbed by the channel buffer (backpressure).

### Implementation Guide

A simple pipeline has three stages and two channels:

```cpp
// Diagram or code structure for the pipeline
```

Pitfall warning: The shutdown order of the pipeline is critical. The upstream stage must `close` its output channel after processing all data, so the downstream stage can naturally exit after `receive` returns `nullopt`. If you forget to `close`, the downstream stage will block forever.

### Verification

```cpp
// Tests for pipeline execution and shutdown
```

## Checklist

- [ ] Channel's send/receive use predicate waits.
- [ ] Close semantics are correct: cannot send after close, existing data can be received.
- [ ] Unbuffered channel correctly implements synchronous handshake.
- [ ] try_send/try_receive non-blocking behavior is correct.
- [ ] select can choose a ready channel from multiple channels.
- [ ] select returns nullopt after all channels are closed.
- [ ] Pipeline shutdown order is correct, no deadlocks.
- [ ] All tests pass under TSan with no data race reports.
- [ ] Can explain the advantages of Channel compared to mutex solutions (message passing eliminates shared state) and the costs (overhead of data copy or move).
- [ ] Can describe the differences and similarities between Channel's close semantics and Lab 1's `BoundedBlockingQueue` close semantics.
- [ ] If the Actor track was chosen, can compare the design trade-offs between Channel and Actor.
