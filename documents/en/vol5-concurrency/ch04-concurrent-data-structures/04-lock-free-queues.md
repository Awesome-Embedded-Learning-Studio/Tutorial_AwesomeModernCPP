---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: From ring buffer SPSC to Michael-Scott MPMC queues, cache-friendly producer-consumer
  queue design
difficulty: advanced
order: 4
platform: host
prerequisites:
- 无锁编程基础
reading_time_minutes: 26
related:
- 线程安全队列
- 线程池设计
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
- 循环缓冲区
title: SPSC and MPMC Queues
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/04-lock-free-queues.md
  source_hash: 93b8e3696584cb9feffefabb4e6f6400c2d6939ae465023f98c8c1b900458246
  token_count: 5461
  translated_at: '2026-05-20T04:41:56.714493+00:00'
---
# SPSC and MPMC Queues

To be honest, I debated for a long time while writing this article—should I walk everyone through a hands-on implementation of the Michael-Scott queue? The CAS logic doesn't look complicated at first glance, but once you start coding, you'll find pitfalls everywhere. The timing issues between data reads and CAS in the `dequeue` are especially tricky, and I crashed and burned on my first attempt. But despite the hesitation, we still need to walk this path, because only by writing it yourself can you truly understand "why SPSC is so much faster than MPMC."

In the previous article, we built up a basic intuition for lock-free programming—CAS loops, lock-free vs. wait-free, the ABA problem, and memory reclamation. This knowledge is enough to understand the principles behind any lock-free data structure, but we still have a way to go before writing truly high-performance concurrent queues. Lock-freedom is merely a correctness prerequisite; **cache friendliness** is the real key to performance.

In this article, we start with the simplest and most efficient SPSC queue, gradually increase the complexity, and finally arrive at the MPMC queue. The SPSC (Single Producer Single Consumer) queue has the highest performance ceiling among concurrent queue implementations—in some benchmarks, it achieves over 90% of the throughput of a single-threaded queue. The reason is simple: with only one producer and one consumer, we need no CAS, no locks, just a pair of atomic indices and carefully arranged memory orders. We will explain key optimizations like cache line padding, power-of-two sizing, and memory order selection one by one, because their impact on performance is measured in orders of magnitude.

Then we expand to MPSC (Multiple Producers Single Consumer) and MPMC (Multiple Producers Multiple Consumers) scenarios, discuss the classic Michael-Scott unbounded queue algorithm, and finally run a benchmark comparing SPSC, mutex queues, and MPMC. We also introduce the industrial-grade `moodycamel::ConcurrentQueue` as a practical reference.

## SPSC Ring Buffer: The Performance King of Concurrent Queues

We start with the SPSC queue. It is the foundation of this entire article and the most widely used in real-world engineering. The core data structure of an SPSC queue is a ring buffer: a contiguous block of memory with two indices (a read index and a write index) marking data positions, wrapping around to the beginning when the end is reached. Because there is only one producer and one consumer, each index is modified by only one thread—`write_idx` is written only by the producer and read by the consumer, while `read_idx` is written only by the consumer and read by the producer. This "single-writer, single-reader" pattern means we don't need CAS; we only need `load` and `store` with appropriate memory orders.

### Basic Structure

```cpp
#include <atomic>
#include <array>

template <typename T, std::size_t Capacity>
class SPSCQueue {
public:
    SPSCQueue() : write_idx_(0), read_idx_(0) {}

    bool push(const T& item);
    bool pop(T& item);
    bool empty() const;

private:
    alignas(64) std::atomic<std::size_t> write_idx_;
    alignas(64) std::atomic<std::size_t> read_idx_;
    std::array<T, Capacity> buffer_;
};
```

The structure has three members: `write_idx_`, `read_idx_`, and `buffer_`. Notice that `write_idx_` and `read_idx_` each come with a `alignas(64)`—this is **cache line padding**, one of the most important optimizations in this entire article. Modern CPUs transfer cache between cores in units of cache lines (typically 64 bytes). If `write_idx_` and `read_idx_` happen to fall on the same cache line (which is very likely since they are adjacent member variables), every time the producer writes `write_idx_`, it invalidates the cache line on the consumer's core, and every time the consumer reads `read_idx_`, it invalidates the cache line on the producer's core—this is **false sharing**. Under high-frequency operations, false sharing can degrade performance by one to two orders of magnitude. `alignas(64)` ensures each index exclusively occupies a cache line, eliminating false sharing.

> Don't rush ahead just yet—if you want to intuitively feel the power of false sharing in the exercises later, try removing the `alignas(64)` and running the benchmark again. You will most likely see throughput drop by half or more, especially on ARM platforms where the difference is even more dramatic. This optimization is practically standard in all high-performance concurrent data structures, so don't get lazy and skip it.

C++17 provides a more standard approach: `alignas(std::hardware_destructive_interference_size)`, a compile-time constant representing "the minimum alignment needed to avoid false sharing." On x86-64 it is typically 64, and on ARM it may differ. If your compiler supports it, we recommend using this constant instead of hardcoding 64.

### push and pop Implementation

```cpp
bool push(const T& item)
{
    const std::size_t write = write_idx_.load(std::memory_order_relaxed);
    const std::size_t next_write = write + 1;

    if (next_write == read_idx_.load(std::memory_order_acquire)) {
        return false;  // 队列满
    }

    buffer_[write % Capacity] = item;
    write_idx_.store(next_write, std::memory_order_release);
    return true;
}
```

The push flow is: the producer performs local operations with its own `write_idx_` (`relaxed` load), checks if the queue is full (reads `read_idx_` with `acquire`), writes the data, and then publishes the new `write_idx_` (`release` store).

There is a clever detail here: `write_idx_` and `read_idx_` are continuously incrementing integers, not modulo-reduced indices. The actual buffer position is calculated via `write % Capacity`. This approach avoids wraparound issues when writing back the moduloed index, making the "full check" logic very simple—`next_write == read_idx` means the queue is full. The trade-off is that the indices grow indefinitely, but on a 64-bit platform, running at a rate of one billion operations per second, it would take centuries to overflow.

The choice of memory orders is worth discussing in detail. The producer reads `write_idx_` with `relaxed` because this variable is only written by the producer itself; the producer doesn't need to synchronize any information through it—it is simply a local counter. The producer reads `read_idx_` with `acquire`, which pairs with the consumer's `release` store of `read_idx_`, ensuring the producer can see the data the consumer has already consumed. The producer's write to `buffer_` is a plain write (not atomic, because the consumer won't be reading this position at this time), followed by a `release` store of `write_idx_`, which guarantees the buffer write completes before the `write_idx_` update.

```cpp
bool pop(T& item)
{
    const std::size_t read = read_idx_.load(std::memory_order_relaxed);

    if (read == write_idx_.load(std::memory_order_acquire)) {
        return false;  // 队列空
    }

    item = buffer_[read % Capacity];
    read_idx_.store(read + 1, std::memory_order_release);
    return true;
}

bool empty() const
{
    return read_idx_.load(std::memory_order_acquire)
        == write_idx_.load(std::memory_order_acquire);
}
```

pop is the mirror image of push: the consumer reads its own `read_idx_` with `relaxed`, reads the producer's `write_idx_` with `acquire`, extracts the data, and then does a `release` store of `read_idx_`. The symmetric acquire/release pairing ensures a correct happens-before relationship between data production and consumption.

### Power-of-Two Sizing Optimization

Great, now we have a working SPSC queue. But there is one more small detail where we can squeeze out some performance. Above, we used `write % Capacity` to calculate the buffer position. The modulo operation compiles to a division instruction on most architectures, and the latency of a division instruction (dozens of cycles) can become a bottleneck on the hot path. If `Capacity` is a power of two, the modulo can be optimized to a bitwise AND: `write & (Capacity - 1)`, taking only one cycle.

```cpp
template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    // ...
    static constexpr std::size_t kMask = Capacity - 1;

    bool push(const T& item)
    {
        const std::size_t write = write_idx_.load(std::memory_order_relaxed);

        if (write + 1 == read_idx_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[write & kMask] = item;  // 位与代替取模
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }
};
```

This is a classic space-for-time trade-off—you might need to adjust the queue size from 1000 to 1024, wasting 24 slots, but in exchange, you save dozens of CPU cycles per operation. On the hot path, this optimization is completely worth it. In production code, SPSC queues almost always use power-of-two sizing.

### A Complete, Compilable Example

Let's integrate all the optimizations above and write a complete version that can be compiled and run directly. This version uses power-of-two sizing (bitwise AND instead of modulo) and an improved full-check logic, representing the standard form of an SPSC queue in production code.

```cpp
#include <atomic>
#include <array>
#include <thread>
#include <iostream>
#include <chrono>

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

public:
    bool push(const T& item)
    {
        const std::size_t write = write_idx_.load(std::memory_order_relaxed);
        if (write - read_idx_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        buffer_[write & kMask] = item;
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item)
    {
        const std::size_t read = read_idx_.load(std::memory_order_relaxed);
        if (read == write_idx_.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer_[read & kMask];
        read_idx_.store(read + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::atomic<std::size_t> read_idx_{0};
    alignas(64) std::array<T, Capacity> buffer_{};
};

int main()
{
    constexpr int kItemCount = 10'000'000;
    SPSCQueue<int, 1024> queue;

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&] {
        for (int i = 0; i < kItemCount; ++i) {
            while (!queue.push(i)) {
                // 自旋等待
            }
        }
    });

    std::thread consumer([&] {
        int value;
        for (int i = 0; i < kItemCount; ++i) {
            while (!queue.pop(value)) {
                // 自旋等待
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    std::cout << "SPSC: " << kItemCount << " items in "
              << us << " us ("
              << (kItemCount * 1000000.0 / us) << " ops/s)\n";
    return 0;
}
```

Note that the full-check logic changed from `write + 1 == read` to `write - read >= Capacity`. Because both `write` and `read` are incrementing, `write - read` is the number of elements in the queue. The wraparound behavior of unsigned integer subtraction happens to be correct here: even if `write` is much larger than `read`, the difference correctly reflects the number of elements in the queue.

## MPSC Queues: The Challenge of Multiple Producers

Alright, we've got SPSC sorted out, and its performance is indeed beautiful. But reality is rarely this ideal—you will most likely encounter scenarios where "multiple threads are pushing data into the same queue." This is MPSC (Multiple Producers Single Consumer). Going from SPSC to MPSC, the complexity jumps a level because we no longer have the privileged condition of "only one writer." Multiple producers must compete for `write_idx_`, and we must introduce CAS to coordinate.

A common MPSC design retains the ring buffer structure but changes the update of `write_idx_` from a simple `store` to a CAS operation: each producer atomically competes to increment `write_idx_` via CAS to reserve a slot, writes data to that slot, and finally marks the slot as "data ready." The consumer checks slots in order for readiness, reads the data if ready, and advances `read_idx_`.

```cpp
template <typename T, std::size_t Capacity>
class MPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");

    struct Slot {
        std::atomic<std::size_t> sequence;
        T data;
    };

public:
    MPSCQueue()
    {
        for (std::size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    bool push(const T& item)
    {
        std::size_t pos = write_idx_.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = slots_[pos & kMask];
            std::size_t seq = slot.sequence.load(std::memory_order_acquire);
            std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) - pos;

            if (diff == 0) {
                // 槽位属于当前 pos，尝试预约
                if (write_idx_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed)) {
                    // 预约成功，写入数据
                    slot.data = item;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS 失败，pos 已被更新为最新值，重试
            } else if (diff < 0) {
                // 槽位还没被消费者释放，队列满
                return false;
            } else {
                // 其他生产者已经预约了这个位置，重新加载
                pos = write_idx_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pop(T& item)
    {
        Slot& slot = slots_[read_idx_ & kMask];
        std::size_t seq = slot.sequence.load(std::memory_order_acquire);
        std::ptrdiff_t diff = static_cast<std::ptrdiff_t>(seq) -
                              static_cast<std::ptrdiff_t>(read_idx_);

        if (diff < 1) {
            // diff == 0：槽位等待写入（队列空）
            // diff < 0：消费者超前（不应发生，但防御性处理）
            return false;
        }

        item = std::move(slot.data);
        slot.sequence.store(read_idx_ + Capacity, std::memory_order_release);
        ++read_idx_;
        return true;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(64) std::atomic<std::size_t> write_idx_{0};
    alignas(64) std::size_t read_idx_{0};
    alignas(64) std::array<Slot, Capacity> slots_{};
};
```

The essence of this design lies in the **sequence**. Each slot has a `sequence` field, which serves both to check for empty/full status and to mark whether data is ready. Initially, the `sequence` of the i-th slot equals i, meaning "this slot is waiting for the i-th write." After the producer reserves this position and writes the data, it sets `sequence` to `pos + 1`, meaning "data is ready, waiting for the (pos + 1)-th read" (because when the consumer sees `sequence == read_idx + 1`, it knows the data is ready). After the consumer reads the data, it sets `sequence` to `read_idx + Capacity`, meaning "this slot can be used again."

There is an easy-to-miss detail here: the empty-check condition in the consumer's `pop` is `diff < 1` rather than `diff < 0`. Why? Because when the queue is empty, the slot's `sequence` equals `read_idx_` (meaning "waiting for write"), so `seq - read_idx_ == 0`. If you write it as `< 0`, the consumer will incorrectly judge it as "has data" and read out uninitialized garbage values—this bug is extremely subtle because in most test cases the queue is not empty, and it only triggers when "the consumer is faster than the producer." I've fallen into this trap myself, so I'm giving a special warning.

The consumer's `pop` doesn't need CAS because there is only one consumer—`read_idx_` is a plain `size_t`, not an atomic variable. This keeps the consumption side of the MPSC queue performing just as well as SPSC.

## Michael-Scott MPMC Queue: The Unbounded Linked-List Approach

MPSC uses a ring buffer to implement a bounded queue, but what if we need an **unbounded MPMC queue**? Things get even more complicated here—we need multiple producers, multiple consumers, and support for unbounded growth. There is a classic answer to this problem: the lock-free queue based on a linked list, proposed by Michael and Scott in 1996. This paper has been hugely influential; Java's `ConcurrentLinkedQueue` and Boost.Lockfree's queue implementation are both based on this algorithm. Let's break it down.

### Data Structure

```cpp
template <typename T>
class MichaelScottQueue {
public:
    MichaelScottQueue()
    {
        Node* sentinel = new Node();
        head_.store(sentinel, std::memory_order_relaxed);
        tail_.store(sentinel, std::memory_order_relaxed);
    }

    void enqueue(const T& value);
    bool dequeue(T& result);

private:
    struct Node {
        T data;
        std::atomic<Node*> next;
        Node() : next(nullptr) {}
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    alignas(64) std::atomic<Node*> head_;
    alignas(64) std::atomic<Node*> tail_;
};
```

The queue maintains two atomic pointers: `head_` points to the head (for dequeue), and `tail_` points to the tail (for enqueue). When the queue is initialized, there is a sentinel node, and both `head_` and `tail_` point to it. The sentinel node does not store valid data; its existence simplifies the handling of empty queues.

### enqueue: Appending at the Tail

```cpp
void enqueue(const T& value)
{
    Node* new_node = new Node(value);

    for (;;) {
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = tail->next.load(std::memory_order_acquire);

        // 检查 tail 是否还是最后一个节点
        if (tail == tail_.load(std::memory_order_acquire)) {
            if (next == nullptr) {
                // tail 确实是最后一个，尝试挂上新节点
                Node* null_ptr = nullptr;
                if (tail->next.compare_exchange_weak(
                        null_ptr, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                    // 成功挂上，尝试推进 tail（失败也无妨，其他线程会帮忙推进）
                    tail_.compare_exchange_weak(
                        tail, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                    return;
                }
            } else {
                // tail 后面还有节点，说明 tail 落后了
                // 帮忙推进 tail
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
        }
    }
}
```

The enqueue logic has several steps. First, read `tail` and `tail->next`. Then verify that `tail` is still the tail of the queue (to prevent tail from being advanced by another thread during the read). If `tail->next` is `nullptr`, it means tail is indeed the last node, and we try to attach the new node using CAS. If the CAS succeeds, we attempt to advance `tail_` to point to the new node—note that even if this CAS fails, it doesn't matter, because other threads will help advance it in their own enqueue. This is so-called "cooperative advancement," a common pattern in lock-free algorithms.

If we find that `tail->next` is not `nullptr`, it means another thread has already attached a new node but hasn't had time to advance `tail_`. We help advance `tail_` and then retry.

### dequeue: Removing from the Head

```cpp
bool dequeue(T& result)
{
    for (;;) {
        Node* head = head_.load(std::memory_order_acquire);
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);

        // 验证 head 没变
        if (head == head_.load(std::memory_order_acquire)) {
            if (head == tail) {
                if (next == nullptr) {
                    // 队列空
                    return false;
                }
                // tail 落后了，帮忙推进
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            } else {
                // 先 CAS 抢占 head，成功后再移动数据
                // 不能在 CAS 之前 std::move(next->data)——如果 CAS 失败，
                // 说明另一个线程已经消费了这个节点，move 会破坏数据
                if (head_.compare_exchange_weak(
                        head, next,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    result = std::move(next->data);
                    return true;
                }
            }
        }
    }
}
```

dequeue reads `head`, `tail`, and `head->next` (because `head` is the sentinel, the actual data is in `head->next`). If `head == tail` and `head->next == nullptr`, the queue is empty. If `head == tail` but `head->next != nullptr`, it means a node has been attached but `tail_` hasn't been advanced yet; we help advance it and retry. Under normal circumstances, we first use CAS to advance `head_` from `head` to `next`, and after the CAS succeeds, we move `next->data`.

Here we must strongly emphasize a pitfall easy to stumble into in a C++ implementation: **absolutely do not execute `std::move(next->data)` before the CAS**. Because the CAS might fail—failure means another thread has already grabbed this node. If we have already `std::move` the data before the CAS, that data is gone (`std::move` is not a move itself, it merely makes moving possible, but the move assignment called here does transfer resources), and the other thread gets a hollowed-out node. This is why in our code we do the CAS first, and only safely move the data after confirming we have grabbed the node. This is also the "pitfall" I mentioned at the beginning—in the original paper, `*pvalue = next->value` is a simple value copy, which doesn't involve move semantics issues, but in C++ we must handle it carefully.

After a successful dequeue, the old sentinel node becomes a dangling pointer—as we discussed in the previous article, there is a memory reclamation problem here. The Michael-Scott paper doesn't directly solve this problem, and actual implementations need to pair it with Hazard Pointers, epoch-based reclamation, or other schemes. I must emphasize once again: memory reclamation in lock-free programming is not an optional add-on; it is a necessity for correctness. If you directly `delete` the old head node, those threads that just read the old head pointer from a CAS will access freed memory—use-after-free in concurrent scenarios manifests even more bizarrely than in single-threaded code, because it might only occur sporadically after you've run a million tests, by which time you've probably already deployed this queue to production.

Each enqueue and dequeue of the Michael-Scott queue requires at most two CAS operations (one to manipulate data, one to advance tail/head), and in the worst case, there are additional CAS operations for helping to advance. Compared to SPSC's zero CAS, this overhead becomes significant under high contention. But it is a general-purpose MPMC solution and remains one of the best-performing choices in multi-producer, multi-consumer scenarios.

## Producer-Consumer Batch Processing

At this point, we have implementations for SPSC, MPSC, and MPMC queues. The next question is: is there still room to squeeze out more performance? The answer is yes, and this optimization is often overlooked—**batching**. In high-frequency scenarios, the overhead of per-element push/pop atomic operations adds up—each time there is an acquire/release memory barrier and potential cache line invalidation. If we process multiple elements at once, merging multiple atomic operations into one, throughput can be significantly improved.

```cpp
/// 批量 push：一次性写入多个元素，只发布一次 write_idx
template <typename T, std::size_t Capacity>
std::size_t batch_push(SPSCQueue<T, Capacity>& queue,
                       const T* items, std::size_t count)
{
    const std::size_t write = queue.write_idx_.load(std::memory_order_relaxed);
    const std::size_t read = queue.read_idx_.load(std::memory_order_acquire);
    const std::size_t available = Capacity - (write - read);
    const std::size_t to_write = std::min(count, available);

    for (std::size_t i = 0; i < to_write; ++i) {
        queue.buffer_[(write + i) & (Capacity - 1)] = items[i];
    }

    // 一次性发布所有写入
    queue.write_idx_.store(write + to_write, std::memory_order_release);
    return to_write;
}
```

The key to batch operations is that multiple data writes only need one `release` store to publish. The same applies to the consumer side: multiple reads only need one `release` store to confirm. This is especially effective in data block transfer scenarios (network packets, DMA buffers, file I/O)—since you have a large amount of data to move anyway, you might as well move more at once.

## Benchmark: SPSC vs Mutex Queue vs MPMC

No matter how good the theoretical analysis sounds, we still need to look at actual data. Next, we run a set of benchmarks to intuitively feel the performance gap between different implementations. My test environment is: Intel i7-12700K, Ubuntu 22.04, GCC 13.2, with compiler flags `-O2 -march=native`. Queue capacity is 1024, and each test executes 10,000,000 push + pop operations.

### Single Producer Single Consumer (SPSC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|----------------|-----------|----------------------|
| SPSC ring buffer | 28 | 357 |
| mutex + std::queue | 135 | 74 |
| Michael-Scott MPMC (1p1c) | 95 | 105 |

The SPSC ring buffer leads with an absolute advantage. The mutex version is nearly 5 times slower, with the main overhead coming from lock acquisition and release—even in uncontended SPSC scenarios, `lock()` and `unlock()` each require an atomic instruction plus a memory barrier. The Michael-Scott queue is faster than mutex in 1p1c mode, but more than 3 times slower than the SPSC ring buffer—the overhead of those two CAS operations is very real.

### Four Producers Four Consumers (MPMC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|----------------|-----------|----------------------|
| MPSC ring buffer (4p1c) | 180 | 56 |
| Michael-Scott MPMC (4p4c) | 320 | 31 |
| mutex + std::queue (4p4c) | 850 | 12 |
| moodycamel (4p4c) | 95 | 105 |

Under multi-threaded scenarios, the mutex version degrades sharply—massive context switching and lock contention drop throughput to 12M ops/s. The Michael-Scott queue performs better than mutex but falls far short of `moodycamel::ConcurrentQueue`. moodycamel's secret is that it is not a simple linked list implementation—it uses tiered contiguous block storage, thread-local caches, and lock-free batch operations, offering far better cache locality than linked list approaches.

These data illustrate an important fact: **a general-purpose lock-free algorithm is not necessarily faster than a mature library implementation**. The Michael-Scott queue algorithm is correct and lock-free, but its linked list structure and dual-CAS overhead limit its performance ceiling. In performance-sensitive production code, using a heavily optimized, industrial-grade library is wiser than hand-writing an MPMC queue yourself.

## Industrial Case Study: moodycamel::ConcurrentQueue

Having discussed hand-written queue implementations, let's look at an industrial-grade solution. `moodycamel::ConcurrentQueue` is one of the most widely used high-performance MPMC queues in the C++ community. Its author, Cameron Desrochers, details in the design documentation why a "correct lock-free algorithm" does not equal a "high-performance lock-free implementation." We won't dive into the source code, but understanding its core design philosophy is very helpful for writing high-performance concurrent code.

First, it replaces linked lists with contiguous block storage. The Michael-Scott queue requires `new`ing a node on every enqueue—the overhead of memory allocation and the cache-unfriendly nature of linked lists are performance killers. moodycamel stores elements in contiguous memory blocks whose sizes can grow dynamically, ensuring that consecutive elements are adjacent in memory, allowing the CPU's prefetcher to work efficiently. Then, it adopts implicit producer-consumer mapping—it doesn't enforce a model where "Thread A is a producer, Thread B is a consumer," but instead lets each thread automatically register on first use of the queue, internally maintaining thread-local sub-queues to reduce global contention while preserving MPMC generality. Finally, it supports batch operations and stealing—when a thread's local sub-queue is empty, it can "steal" a batch of elements from another thread's sub-queue rather than stealing them one by one, dramatically reducing the number of CAS operations.

You might ask: since moodycamel is so powerful, why do we still need to learn how to hand-write SPSC and Michael-Scott queues? The reason is simple: only by understanding the performance bottlenecks of these foundational implementations (the cache-unfriendliness of linked lists, the contention overhead of CAS, the power of false sharing) can you truly understand what moodycamel's design decisions are optimizing for. Moreover, in strict SPSC scenarios, a hand-written ring buffer is still the fastest—moodycamel's thread-local sub-queue mechanism actually introduces unnecessary layers of indirection in a single-producer, single-consumer scenario.

Usage is very simple; there are only two header files: `concurrentqueue.h` and `blockingconcurrentqueue.h`:

```cpp
#include "concurrentqueue.h"
#include <thread>
#include <iostream>

int main()
{
    moodycamel::ConcurrentQueue<int> q;

    // 生产者
    std::thread producer([&] {
        for (int i = 0; i < 100000; ++i) {
            q.enqueue(i);
        }
    });

    // 消费者
    std::thread consumer([&] {
        int item;
        for (int i = 0; i < 100000; ++i) {
            while (!q.try_dequeue(item)) {
                // 自旋
            }
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

If you need blocking semantics (the consumer blocks and waits when the queue is empty), you can use `BlockingConcurrentQueue`:

```cpp
#include "blockingconcurrentqueue.h"

moodycamel::BlockingConcurrentQueue<int> q;

// 消费者：队列为空时阻塞
int item;
q.wait_dequeue(item);  // 阻塞直到有数据

// 带超时
if (q.wait_dequeue_timed(item, std::chrono::milliseconds(100))) {
    // 100ms 内取到了
} else {
    // 超时
}
```

Selection advice: if your scenario is strictly SPSC, a hand-written ring buffer is the fastest, and moodycamel is somewhat overkill; if it's MPSC or MPMC with high performance requirements, go straight to moodycamel and don't reinvent the wheel; if you need a blocking queue that supports shutdown and timeouts, use the `BoundedQueue` or `moodycamel::BlockingConcurrentQueue` we wrote in the previous article.

## Exercises

Reading without practicing is pointless. The following three exercises range from easy to hard, covering the core knowledge points of this article. We recommend completing at least Exercise 1 and Exercise 2—they don't take much time, but they will help you build an intuitive feel for "just how important cache line padding is" and "just how large the overhead of locks is."

### Exercise 1: Implement and Benchmark an SPSC Ring Buffer

The goal of this exercise is to let you personally verify the actual effect of every optimization mentioned in this article. First, use the complete `SPSCQueue` code provided in this article, compile and run it, and confirm basic correctness (being able to finish 10,000,000 push + pop operations without crashing counts as correct). Then, try the following variations separately and record the throughput: increase the queue capacity to 4096 and observe the throughput change, then decrease it to 16 and observe the change—think about how capacity affects performance. Next, remove the `alignas(64)` and re-benchmark; you will most likely see a performance drop—this is the power of false sharing. Finally, change all `memory_order_acquire/release` to `memory_order_seq_cst` and observe the performance difference—on x86 the difference might be small (x86's acquire/release is almost as heavy as seq_cst), but on ARM it might be more pronounced.

### Exercise 2: SPSC vs Mutex Queue Comparison

This exercise helps you build a performance intuition for "locks vs. lock-free." Use `std::mutex` + `std::queue<int>` to implement a simple thread-safe queue, then use this article's benchmark framework to compare the performance of the SPSC ring buffer and the mutex queue under three configurations: 1p1c, 2p2c, and 4p4c. If you have the energy, try recording the number of CAS retries and mutex wait times, and analyze where the bottleneck lies—you will find that from 1p1c to 4p4c, the mutex performance degradation curve is extremely steep.

### Exercise 3: Observing the CAS Overhead of MPMC Queues

This exercise is for readers who want to deeply understand CAS contention overhead. Implement (or use an existing open-source implementation of) a Michael-Scott queue and benchmark it under a 4p4c configuration. Then, add counters in the CAS loops of enqueue and dequeue to tally the total number of retries, and compare it with SPSC's performance under the same data volume to quantify just how large the "CAS overhead" really is. If you have the means, repeat the test on an ARM platform (like a Raspberry Pi 4)—ARM's LL/SC instruction pair behaves significantly differently from x86's `lock cmpxchg` under high contention, and this comparison will be very enlightening.

> 💡 The complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## References

- [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms — Michael & Scott, 1996](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf)
- [A Fast General-Purpose Lock-Free Queue for C++ — moodycamel](https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c%2B%2B)
- [Detailed Design of a Lock-Free Queue — moodycamel](https://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)
- [rigtorp/SPSCQueue — A minimal and efficient SPSC queue implementation](https://github.com/rigtorp/SPSCQueue)
- [atomic_queue benchmarks — max0x7ba](https://max0x7ba.github.io/atomic_queue/html/benchmarks.html)
