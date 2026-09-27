---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'From the SPSC ring buffer to the Michael-Scott MPMC queue: cache-friendly
  producer-consumer queue design'
difficulty: advanced
order: 4
platform: host
prerequisites:
- Lock-Free Programming Fundamentals
reading_time_minutes: 26
related:
- Thread-Safe Queue
- Thread Pool Design
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
- 循环缓冲区
title: SPSC and MPMC Queues
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/04-lock-free-queues.md
  source_hash: 66d571c6fa2d1aa18d5c8f20f1515e4dbd253ab6d4944511e78961fbb07fc774
  translated_at: '2026-09-26T08:16:18+00:00'
  engine: anthropic
  token_count: 13300
---
# SPSC and MPMC Queues

To be honest, I went back and forth for a long time while writing this article—should we really walk through implementing the Michael-Scott queue by hand? The CAS logic doesn't look complicated, but the moment you start writing it you find pitfalls everywhere, especially the ordering between the data read and the CAS in `dequeue`—my own first implementation crashed right there. But hesitate as I did, this is a road we have to walk, because only after writing one yourself do you truly understand "why SPSC is so much faster than MPMC".

In the previous article we built up the basic judgment for lock-free programming—CAS loops, lock-free vs. wait-free, the ABA problem, memory reclamation. That knowledge is enough to understand how any lock-free data structure works, but there is still a stretch of road between it and writing a genuinely high-performance concurrent queue. Lock-free is only the precondition for correctness; **cache friendliness** is what performance is really about.

In this article we start from the simplest and fastest queue there is—the SPSC queue—and work our way up in complexity until we reach MPMC. The SPSC (Single Producer Single Consumer) queue has the highest performance ceiling of any concurrent queue—in some benchmarks it reaches more than 90% of a single-threaded queue's throughput. The reason is simple: with only one producer and one consumer, there is no CAS and no lock—just a pair of atomic indices and carefully arranged memory ordering. We will walk through the key optimizations—cache line padding, power-of-two sizing, memory order selection—one by one, because their impact on performance is measured in orders of magnitude.

From there we extend to MPSC (multiple producers, single consumer) and MPMC (multiple producers, multiple consumers) scenarios, discuss the classic Michael-Scott unbounded queue algorithm, and finish with a benchmark comparison covering SPSC, a mutex queue, and MPMC, plus a look at the industrial-grade `moodycamel::ConcurrentQueue` as a practical reference.

## The SPSC Ring Buffer: The Performance King of Concurrent Queues

Let's start with the SPSC queue: it is the foundation of this whole article and also the one most used in real engineering. The core data structure of an SPSC queue is a ring buffer: one contiguous block of memory, with two indices (a read index and a write index) marking where the data is, wrapping back to the beginning when the end is reached. Since there is exactly one producer and one consumer, each index is modified by only one thread—`write_idx` is written only by the producer and read by the consumer; `read_idx` is written only by the consumer and read by the producer. This "single writer, single reader" pattern means we need no CAS—only `load` and `store` with the right memory ordering.

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

The structure has three members: `write_idx_`, `read_idx_`, and `buffer_`. Notice that `write_idx_` and `read_idx_` each carry `alignas(64)`—this is **cache line padding**, one of the most important optimizations in the whole article. Modern CPU caches move data between cores in units of cache lines (usually 64 bytes). If `write_idx_` and `read_idx_` happen to land on the same cache line (they are adjacent members, so the odds are good), every write the producer makes to `write_idx_` invalidates that cache line on the consumer's core, and every read of `read_idx_` by the consumer invalidates it on the producer's core—this is **false sharing**. Under high-frequency operation, false sharing can knock one to two orders of magnitude off your performance. `alignas(64)` guarantees that each index exclusively occupies a cache line, eliminating false sharing.

> Don't rush ahead just yet—if you want a first-hand feel for how much false sharing hurts in the exercises later, try removing `alignas(64)` and running the benchmark again. Odds are you will see throughput drop by half or more, and the gap is even more dramatic on ARM. This optimization is practically standard equipment in every high-performance concurrent data structure; don't get lazy and skip it.

C++17 offers a more standard spelling: `alignas(std::hardware_destructive_interference_size)`, a compile-time constant meaning "the minimum alignment required to avoid false sharing". On x86-64 it is usually 64; on ARM it may differ. If your compiler supports it, prefer this constant over a hardcoded 64.

### Implementing push and pop

```cpp
bool push(const T& item)
{
    const std::size_t write = write_idx_.load(std::memory_order_relaxed);
    const std::size_t next_write = write + 1;

    if (next_write == read_idx_.load(std::memory_order_acquire)) {
        return false;  // queue is full
    }

    buffer_[write % Capacity] = item;
    write_idx_.store(next_write, std::memory_order_release);
    return true;
}
```

The flow of `push` is: the producer works with its own `write_idx_` locally (a `relaxed` load), checks whether the queue is full (reading `read_idx_` with `acquire`), writes the data, and then publishes the new `write_idx_` (a `release` store).

There is a clever detail here: `write_idx_` and `read_idx_` are ever-increasing integers, not moduloed indices. The actual buffer position is computed as `write % Capacity`. This avoids the wrap-around headaches of writing a moduloed index back, and makes the full check trivially simple—`next_write == read_idx` means full. The cost is that the indices grow without bound, but on a 64-bit platform, even at a rate of one billion operations per second, they won't overflow for hundreds of years.

The memory ordering choices deserve a careful explanation. The producer reads `write_idx_` with `relaxed`, because only the producer itself ever writes that variable, so the producer doesn't need to synchronize anything through it—it is just a local counter. The producer reads `read_idx_` with `acquire`, which pairs with the consumer's `release` store of `read_idx_` and guarantees the producer sees data the consumer has already finished consuming. The producer's write to `buffer_` is a plain write (no atomicity needed, because the consumer won't read that position at this point in time), followed by a `release` store of `write_idx_`, which guarantees the buffer write completes before the `write_idx_` update becomes visible.

```cpp
bool pop(T& item)
{
    const std::size_t read = read_idx_.load(std::memory_order_relaxed);

    if (read == write_idx_.load(std::memory_order_acquire)) {
        return false;  // queue is empty
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

`pop` is the mirror image of `push`: the consumer reads its own `read_idx_` with `relaxed`, reads the producer's `write_idx_` with `acquire`, takes the data out, and then `release` stores `read_idx_`. This symmetric acquire/release pairing ensures a correct happens-before relationship between producing and consuming the data.

### Power-of-Two Sizing Optimization

Good—now we have a working SPSC queue. But there is one small detail where we can squeeze out a bit more performance. Above we computed the buffer position with `write % Capacity`. On most architectures the modulo is a division instruction, and the latency of division (tens of cycles) can become a bottleneck on the hot path. If `Capacity` is a power of two, the modulo can be reduced to a bitwise AND: `write & (Capacity - 1)`, which costs a single cycle.

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

        buffer_[write & kMask] = item;  // bitwise AND instead of modulo
        write_idx_.store(write + 1, std::memory_order_release);
        return true;
    }
};
```

This is a classic space-for-time trade—you might have to bump the queue size from 1000 to 1024, wasting 24 slots, in exchange for saving tens of CPU cycles on every operation. On a hot path, that trade is absolutely worth it. In production code, SPSC queues almost always use power-of-two sizing.

### A Complete, Compilable Example

Let's roll all the optimizations above into one complete version you can compile and run directly. It uses power-of-two sizing (bitwise AND instead of modulo) and an improved full check—this is the standard shape of an SPSC queue in production code.

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
                // spin-wait
            }
        }
    });

    std::thread consumer([&] {
        int value;
        for (int i = 0; i < kItemCount; ++i) {
            while (!queue.pop(value)) {
                // spin-wait
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

Note that the full check changed from `write + 1 == read` to `write - read >= Capacity`. Since `write` and `read` both only increase, `write - read` is exactly the number of elements in the queue. The wrap-around behavior of unsigned integer subtraction happens to be correct here: even when `write` is far larger than `read`, the difference still correctly reflects the number of elements in the queue.

## The MPSC Queue: The Challenge of Multiple Producers

Alright, SPSC is done, and its performance really is beautiful. But reality is rarely that convenient—you will most likely run into the scenario of "multiple threads stuffing data into the same queue", which is MPSC (Multiple Producers Single Consumer). Going from SPSC to MPSC, the complexity jumps a level, because we no longer have the enviable condition of "only one writer". Multiple producers have to contend on `write_idx_`, so CAS must come in to coordinate.

One common MPSC design keeps the ring buffer structure but changes the update of `write_idx_` from a simple `store` to a CAS operation: each producer uses CAS to atomically race to increment `write_idx_` and reserve a slot, then writes data into that slot, and finally marks the slot "data ready". The consumer checks slots in order for readiness, reads the ready ones, and advances `read_idx_`.

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
                // the slot belongs to the current pos; try to reserve it
                if (write_idx_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed)) {
                    // reservation succeeded; write the data
                    slot.data = item;
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed; pos has been updated to the latest value, retry
            } else if (diff < 0) {
                // the slot hasn't been released by the consumer yet; queue is full
                return false;
            } else {
                // another producer has already reserved this position; reload
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
            // diff == 0: the slot is waiting to be written (queue empty)
            // diff < 0: the consumer ran ahead (shouldn't happen, but handle defensively)
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

The soul of this design is the **sequence**. Each slot has a `sequence` field that serves both the empty/full check and the data-ready flag. Initially, the `sequence` of the i-th slot equals i, meaning "this slot is waiting for the i-th write". After a producer reserves the position and writes the data, it sets `sequence` to `pos + 1`, meaning "data is ready, waiting for the (pos + 1)-th read" (because when the consumer sees `sequence == read_idx + 1`, it knows the data is ready). After the consumer reads the data, it sets `sequence` to `read_idx + Capacity`, meaning "this slot can be used again".

Here is a detail where it is easy to crash: the empty check in the consumer's `pop` is `diff < 1`, not `diff < 0`. Why? Because when the queue is empty, the slot's `sequence` equals `read_idx_` (meaning "waiting for a write"), so `seq - read_idx_ == 0`. If you write `< 0`, the consumer will misjudge it as "data available" and read out uninitialized garbage—this bug hides extremely well, because in most test cases the queue isn't empty; it only fires when "the consumer is faster than the producer". I landed in this pit myself, so consider this a special warning.

The consumer's `pop` needs no CAS, because there is only one consumer—`read_idx_` is a plain `size_t`, not an atomic variable. This lets the consuming side of the MPSC queue keep the same high performance as SPSC.

## The Michael-Scott MPMC Queue: The Unbounded Linked-List Design

MPSC implements a bounded queue on a ring buffer, but what if we need an **unbounded MPMC queue**? Things get more complicated here—multiple producers, multiple consumers, and unbounded growth, all at once. This problem has a classic answer: the linked-list-based lock-free queue proposed by Michael and Scott in 1996. That paper has been enormously influential—Java's `ConcurrentLinkedQueue` and Boost.Lockfree's queue implementation are both based on this algorithm. Let's take it apart.

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

The queue maintains two atomic pointers: `head_` points to the head of the queue (used by dequeue), and `tail_` points to the tail (used by enqueue). When the queue is initialized there is a sentinel node, and both `head_` and `tail_` point to it. The sentinel node stores no real data; its existence simplifies handling the empty queue.

### enqueue: Appending at the Tail

```cpp
void enqueue(const T& value)
{
    Node* new_node = new Node(value);

    for (;;) {
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = tail->next.load(std::memory_order_acquire);

        // check whether tail is still the last node
        if (tail == tail_.load(std::memory_order_acquire)) {
            if (next == nullptr) {
                // tail really is the last one; try to link the new node on
                Node* null_ptr = nullptr;
                if (tail->next.compare_exchange_weak(
                        null_ptr, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                    // linked successfully; try to advance tail (failure is fine,
                    // other threads will help advance it)
                    tail_.compare_exchange_weak(
                        tail, new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                    return;
                }
            } else {
                // there are nodes after tail, meaning tail has fallen behind
                // help advance tail
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
        }
    }
}
```

The logic of `enqueue` unfolds in steps. First read `tail` and `tail->next`. Then verify that `tail` is still the tail of the queue (guarding against the tail having been advanced by another thread while we were reading). If `tail->next` is `nullptr`, tail really is the last node, and we try to link the new node on with a CAS. Once the CAS succeeds, we try to advance `tail_` to point to the new node—note that this CAS failing is harmless, because other threads will help advance it in their own enqueue. This is the so-called "cooperative advancement", a common pattern in lock-free algorithms.

If we find that `tail->next` is not `nullptr`, some other thread has already linked a new node on but hasn't gotten around to advancing `tail_`. We help advance `tail_`, then retry.

### dequeue: Removing from the Head

```cpp
bool dequeue(T& result)
{
    for (;;) {
        Node* head = head_.load(std::memory_order_acquire);
        Node* tail = tail_.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);

        // verify head hasn't changed
        if (head == head_.load(std::memory_order_acquire)) {
            if (head == tail) {
                if (next == nullptr) {
                    // queue is empty
                    return false;
                }
                // tail has fallen behind; help advance it
                tail_.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            } else {
                // CAS to grab head first, and only move the data after succeeding;
                // never std::move(next->data) before the CAS—if the CAS fails,
                // another thread already consumed this node, and the move would corrupt the data
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

`dequeue` reads `head`, `tail`, and `head->next` (since `head` is the sentinel, the real data lives in `head->next`). If `head == tail` and `head->next == nullptr`, the queue is empty. If `head == tail` but `head->next != nullptr`, a node has been linked on while `tail_` hasn't advanced yet; we help advance it and retry. In the normal case, we first CAS `head_` forward from `head` to `next`, and only after the CAS succeeds do we move `next->data`.

Here I must emphasize a trap that C++ implementations easily fall into: **absolutely never execute `std::move(next->data)` before the CAS**. The CAS can fail—and failure means another thread has already claimed this node. If we `std::move` the data before the CAS, that data is moved away (`std::move` is not itself a move, it only makes moving possible, but the move assignment called here really does transfer the resources), and the other thread is left holding a hollowed-out node. That is why the code does the CAS first and only moves the data once the node is securely ours. This is also the "crash site" I mentioned at the beginning—in the original paper, `*pvalue = next->value` is a plain value copy with no move semantics involved, but in C++ you must handle it carefully.

After a successful dequeue, the old sentinel node becomes a dangling pointer—exactly as discussed in the previous article, this is a memory reclamation problem. The Michael-Scott paper doesn't solve this problem directly; actual implementations need to be paired with Hazard Pointers, epoch-based reclamation, or another scheme. I must stress this once more: memory reclamation in lock-free programming is not an optional add-on, it is a necessary condition for correctness. If you just `delete` the old head node, those threads that read the old head pointer right out of their CAS will access freed memory—use-after-free behaves even more eerily in concurrent scenarios than in single-threaded ones, because it may fire only once after a million test runs, and by then you have probably already deployed this queue to production.

Each enqueue and dequeue on the Michael-Scott queue takes at most two CASes (one to operate on the data, one to advance tail/head), plus additional CASes to help advance in the worst case. Compared with SPSC's zero CASes, this overhead becomes significant under high contention. But it is a general-purpose MPMC solution and remains one of the best-performing choices in multi-producer, multi-consumer scenarios.

## Producer-Consumer Batching

At this point we have implementations of three kinds of queue: SPSC, MPSC, and MPMC. So the next question: is there still room to squeeze out more performance? The answer is yes, and it is an optimization that often gets overlooked—**batching**. In high-frequency scenarios, the cost of one push/pop at a time accumulates—every single operation pays acquire/release memory barriers and possible cache line invalidations. If we process multiple elements at once, merging many atomic operations into one, throughput can improve substantially.

```cpp
/// Batch push: write multiple elements at once, publish write_idx only once
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

    // publish all the writes at once
    queue.write_idx_.store(write + to_write, std::memory_order_release);
    return to_write;
}
```

The key to batch operations: many data writes need only one `release` store to be published. The consumer side is symmetric—many reads need only one `release` store to be confirmed. This is especially effective in data-block transfer scenarios (network packets, DMA buffers, file I/O)—you have a large amount of data to move anyway, so you might as well move more of it per trip.

## Benchmark: SPSC vs Mutex Queue vs MPMC

However nice the theoretical analysis sounds, we still have to look at actual data, so next we run a set of benchmarks to get a first-hand feel for the performance gaps between the implementations. My test environment: Intel i7-12700K, Ubuntu 22.04, GCC 13.2, compile options `-O2 -march=native`. Queue capacity 1024, and each test executes 10,000,000 push + pop operations.

### Single Producer, Single Consumer (SPSC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|------|-----------|----------------|
| SPSC ring buffer | 28 | 357 |
| mutex + std::queue | 135 | 74 |
| Michael-Scott MPMC (1p1c) | 95 | 105 |

The SPSC ring buffer leads by an absolute margin. The mutex version is nearly 5x slower, with the main overhead coming from lock acquisition and release—even in a contention-free SPSC scenario, `lock()` and `unlock()` each cost an atomic instruction plus a memory barrier. The Michael-Scott queue in 1p1c mode is faster than the mutex but more than 3x slower than the SPSC ring buffer—the cost of those two CASes is real.

### Four Producers, Four Consumers (MPMC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|------|-----------|----------------|
| MPSC ring buffer (4p1c) | 180 | 56 |
| Michael-Scott MPMC (4p4c) | 320 | 31 |
| mutex + std::queue (4p4c) | 850 | 12 |
| moodycamel (4p4c) | 95 | 105 |

In multi-threaded scenarios, the mutex version degrades sharply—massive context switching and lock contention drag throughput down to 12M ops/s. The Michael-Scott queue performs better than the mutex but is far behind `moodycamel::ConcurrentQueue`. moodycamel's secret is that it is not a naive linked-list implementation—it uses a hierarchy of contiguous blocks, thread-local caching, and lock-free batch operations, which is far superior to linked-list designs in cache locality.

These numbers tell us one important fact: **a general lock-free algorithm is not necessarily faster than a mature library implementation**. The Michael-Scott queue's algorithm is correct and lock-free, but its linked-list structure and double-CAS overhead limit its performance ceiling. In performance-sensitive production code, using a heavily optimized industrial-grade library is wiser than hand-writing your own MPMC queue.

## Industrial Case Study: moodycamel::ConcurrentQueue

Now that we have talked through the hand-written queue implementations, let's look at an industrial-grade option. `moodycamel::ConcurrentQueue` is one of the most widely used high-performance MPMC queues in the C++ community, and its author, Cameron Desrochers, spelled out in the design documents why a "correct lock-free algorithm" does not equal a "high-performance lock-free implementation". We won't go deep into the source code, but understanding its core design ideas helps a lot with writing high-performance concurrent code.

First, it replaces the linked list with contiguous block storage. The Michael-Scott queue has to `new` a node on every enqueue—the overhead of memory allocation and the cache-unfriendliness of linked lists are performance killers. moodycamel stores elements in contiguous memory blocks whose size can grow dynamically, so consecutive elements sit next to each other in memory and the CPU's prefetcher can work efficiently. Second, it adopts an implicit producer-consumer mapping—instead of forcing a "thread A is the producer, thread B is the consumer" model, it lets every thread register automatically the first time it uses the queue, maintaining thread-local sub-queues internally, which reduces global contention while keeping MPMC generality. Finally, it supports batch operations and stealing—when a thread's local sub-queue is empty, it can "steal" a batch of elements from another thread's sub-queue instead of stealing one by one, drastically reducing the number of CASes.

You might ask: since moodycamel is this strong, why should we still learn hand-written SPSC and Michael-Scott queues? The reason is simple: only by understanding the performance bottlenecks of these basic implementations (the cache-unfriendliness of linked lists, the contention overhead of CAS, the power of false sharing) can you truly understand what moodycamel's design decisions are optimizing. Moreover, in a strict SPSC scenario, the hand-written ring buffer is still the fastest—moodycamel's thread-local sub-queue mechanism actually introduces an unnecessary layer of indirection in the single-producer, single-consumer case.

Usage is very simple; there are only two headers, `concurrentqueue.h` and `blockingconcurrentqueue.h`:

```cpp
#include "concurrentqueue.h"
#include <thread>
#include <iostream>

int main()
{
    moodycamel::ConcurrentQueue<int> q;

    // producer
    std::thread producer([&] {
        for (int i = 0; i < 100000; ++i) {
            q.enqueue(i);
        }
    });

    // consumer
    std::thread consumer([&] {
        int item;
        for (int i = 0; i < 100000; ++i) {
            while (!q.try_dequeue(item)) {
                // spin
            }
        }
    });

    producer.join();
    consumer.join();
    return 0;
}
```

If you need blocking semantics (the consumer blocks waiting when the queue is empty), you can use `BlockingConcurrentQueue`:

```cpp
#include "blockingconcurrentqueue.h"

moodycamel::BlockingConcurrentQueue<int> q;

// consumer: blocks while the queue is empty
int item;
q.wait_dequeue(item);  // block until data arrives

// with a timeout
if (q.wait_dequeue_timed(item, std::chrono::milliseconds(100))) {
    // got an item within 100 ms
} else {
    // timed out
}
```

Selection advice: if your scenario is strict SPSC, the hand-written ring buffer is the fastest, and moodycamel is a bit of a sledgehammer for that gnat; if it is MPSC or MPMC with high performance requirements, go straight to moodycamel and don't build your own wheel; if you need a closable blocking queue with timeout support, use the `BoundedQueue` we wrote in the previous article, or `moodycamel::BlockingConcurrentQueue`.

## Exercises

Reading without practicing gets you nowhere. The three exercises below go from easy to hard and cover the core knowledge points of this article. I suggest you complete at least Exercise 1 and Exercise 2—they don't take much time, but they help you build the intuitive feel for "how much cache line padding really matters" and "how big the overhead of locks really is".

### Exercise 1: Implement and Benchmark an SPSC Ring Buffer

The goal of this exercise is to let you verify, with your own hands, the actual effect of every optimization point mentioned in this article. First, take the complete `SPSCQueue` code provided in this article, compile and run it, and confirm basic correctness (surviving 10,000,000 push + pop operations without crashing counts as correct). Then, try the following variations one by one and record throughput: increase the queue capacity to 4096 and observe how throughput changes, then decrease it to 16 and observe again—think about how capacity affects performance. Next, remove `alignas(64)` and re-run the benchmark; you will most likely see performance drop—this is the power of false sharing. Finally, change all `memory_order_acquire/release` to `memory_order_seq_cst` and observe the performance difference—on x86 the difference may be small (x86's acquire/release is almost as heavy as seq_cst), but on ARM it may be more visible.

### Exercise 2: SPSC vs Mutex Queue Comparison

This exercise helps you build the performance intuition for "lock vs lock-free". Implement a simple thread-safe queue with `std::mutex` + `std::queue<int>`, then use this article's benchmark framework to compare the performance of the SPSC ring buffer and the mutex queue under three configurations: 1p1c, 2p2c, and 4p4c. If you have the energy, try recording CAS retry counts and mutex wait times and analyze where the bottleneck is—you will find that from 1p1c to 4p4c, the mutex's performance decay curve is very steep.

### Exercise 3: Observe the CAS Overhead of an MPMC Queue

This exercise is prepared for readers who want to understand CAS contention overhead in depth. Implement (or use an existing open-source implementation of) a Michael-Scott queue and benchmark it in a 4p4c configuration. Then, add counters to the CAS loops of enqueue and dequeue, tally the total retries, compare against SPSC's performance on the same amount of data, and quantify just how big the "CAS overhead" is. If you have the conditions, repeat the test on an ARM platform (a Raspberry Pi 4, for example)—ARM's LL/SC instruction pair behaves significantly differently from x86's `lock cmpxchg` under high contention, and this comparison is very enlightening.

> 💡 Complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); browse `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## References

- [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms — Michael & Scott, 1996](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf)
- [A Fast General-Purpose Lock-Free Queue for C++ — moodycamel](https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c%2B%2B)
- [Detailed Design of a Lock-Free Queue — moodycamel](https://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)
- [rigtorp/SPSCQueue — a minimal, efficient SPSC queue implementation](https://github.com/rigtorp/SPSCQueue)
- [atomic_queue benchmarks — max0x7ba](https://max0x7ba.github.io/atomic_queue/html/benchmarks.html)
