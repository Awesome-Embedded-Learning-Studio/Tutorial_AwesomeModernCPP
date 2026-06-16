---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'From SPSC ring buffers to Michael-Scott MPMC queues: cache-friendly
  producer-consumer queue designs'
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
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/04-lock-free-queues.md
  source_hash: 66d571c6fa2d1aa18d5c8f20f1515e4dbd253ab6d4944511e78961fbb07fc774
  translated_at: '2026-06-16T04:04:52.019269+00:00'
  engine: anthropic
  token_count: 5454
---
# SPSC and MPMC Queues

To be honest, I debated with myself for a long time while writing this article—should I walk everyone through implementing a Michael-Scott queue step-by-step? The CAS logic looks simple enough, but once you actually start writing it, you'll find pitfalls everywhere. Specifically, the timing issues between data reading and CAS in ``dequeue`` tripped me up the first time I implemented it. However, despite the hesitation, this is a path we must walk, because only by implementing it yourself can you truly understand "why SPSC is so much faster than MPMC."

In the previous post, we established a basic understanding of lock-free programming—CAS loops, lock-free vs. wait-free, the ABA problem, and memory reclamation. This knowledge is sufficient for us to understand the principles of any lock-free data structure, but there is still a way to go before we can write truly high-performance concurrent queues. Lock-free is just a prerequisite for correctness; **cache friendliness** is the key to performance.

In this article, we will start with the simplest and most efficient SPSC queue, gradually increase complexity, and finally arrive at the MPMC queue. The SPSC (Single Producer Single Consumer) queue is the implementation with the highest performance ceiling among concurrent queues—in some benchmarks, it can achieve over 90% of the throughput of a single-threaded queue. The reason is simple: with only one producer and one consumer, we don't need CAS, we don't need locks, we only need a pair of atomic indices and carefully arranged memory ordering. We will explain key optimizations like cache line padding, power-of-two sizing, and memory ordering selection one by one, as their impact on performance is order-of-magnitude level.

Then, we will extend to MPSC (Multiple Producers Single Consumer) and MPMC (Multiple Producers Multiple Consumers) scenarios, discuss the classic Michael-Scott unbounded queue algorithm, and finally run a benchmark comparison covering SPSC, mutex queues, and MPMC, introducing the industrial-grade ``moodycamel::ConcurrentQueue`` as a practical reference.

## SPSC Ring Buffer: The Performance King of Concurrent Queues

Let's start with the SPSC queue. It is the foundation of this entire article and is also the most widely used in actual engineering. The core data structure of an SPSC queue is a ring buffer: a contiguous block of memory identified by two indices (read index and write index) to mark data positions, wrapping around to the beginning when the end is reached. Because there is only one producer and one consumer, the two indices are each modified by only one thread—``write_idx`` is only written by the producer and read by the consumer, ``read_idx`` is only written by the consumer and read by the producer. This "single writer, single reader" pattern allows us to avoid CAS; we only need ``load`` and ``store`` with appropriate memory ordering.

### Basic Structure

````cpp
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
````

The structure has three members: ``write_idx_``, ``read_idx_``, and ``buffer_``. Note that ``write_idx_`` and ``read_idx_`` each bring ``alignas(64)``—this is **cache line padding**, one of the most important optimizations in this entire article. Modern CPU caches transfer data between cores in units of cache lines (usually 64 bytes). If ``write_idx_`` and ``read_idx_`` happen to fall on the same cache line (they are adjacent member variables, so this is likely), every time the producer writes ``write_idx_``, it will invalidate the cache line on the consumer core, and every time the consumer reads ``read_idx_``, it will invalidate the cache line on the producer core—this is **false sharing**. Under high-frequency operations, false sharing can knock performance down by one or two orders of magnitude. ``alignas(64)`` ensures that each index exclusively occupies a cache line, eliminating false sharing.

> Don't rush ahead—if you want to intuitively feel the power of false sharing in later exercises, try removing ``alignas(64)`` and running the benchmark again. You will most likely see throughput drop by half or more, especially on ARM platforms where the difference is even more exaggerated. This optimization is almost standard in all high-performance concurrent data structures; don't be lazy and skip it.

C++17 provides a more standard way: ``alignas(std::hardware_destructive_interference_size)``, a compile-time constant representing "the minimum alignment required to avoid false sharing." On x86-64 it is usually 64, while on ARM it might be different. If your compiler supports it, it is recommended to use this constant instead of hardcoding 64.

### Implementation of push and pop

````cpp
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
````

The flow of `push` is: the producer uses its own ``write_idx_`` for local operation (``relaxed`` load), checks if the queue is full (reads ``read_idx_`` with ``acquire``), writes the data, and then publishes the new ``write_idx_`` (``release`` store).

There is a clever detail here: ``write_idx_`` and ``read_idx_`` are continuously incrementing integers, not moduloed indices. The actual buffer position is calculated via ``write % Capacity``. This approach avoids the wrap-around problem when moduloing back to write the index, making the "full check" logic very simple—``next_write == read_idx`` means full. The cost is that the indices grow indefinitely, but on 64-bit platforms, running at a rate of one billion operations per second, it won't overflow for hundreds of years.

The choice of memory ordering is worth explaining carefully. The producer reads ``write_idx_`` using ``relaxed`` because this variable is only written by the producer itself; the producer doesn't need to synchronize any information through it—it's just a local counter. The producer reads ``read_idx_`` using ``acquire``, which pairs with the consumer's ``release`` store of ``read_idx_``, ensuring the producer sees data the consumer has already consumed. The producer writes ``buffer_`` is a normal write (doesn't need to be atomic, because the consumer won't read this location at this point in time), then ``release`` stores ``write_idx_``, which guarantees the buffer write completes before the ``write_idx_`` update.

````cpp
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
````

`pop` is the mirror of `push`: the consumer uses ``relaxed`` to read its own ``read_idx_``, uses ``acquire`` to read the producer's ``write_idx_``, retrieves the data, and then ``release`` stores ``read_idx_``. Symmetric acquire/release pairing ensures the correct happens-before relationship between data production and consumption.

### Power-of-Two Sizing Optimization

Great, now we have a working SPSC queue. But there is a small detail where we can squeeze out a bit more performance. Above we used ``write % Capacity`` to calculate the buffer position. The modulo operation is a division instruction on most architectures, and the latency of the division instruction (dozens of cycles) can become a bottleneck on the hot path. If ``Capacity`` is a power of two, the modulo can be optimized into a bitwise AND operation: ``write & (Capacity - 1)``, taking only one cycle.

````cpp
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
````

This is a classic space-for-time optimization—you might need to adjust the queue size from 1000 to 1024, wasting 24 slots, but in exchange, you save dozens of CPU cycles per operation. On the hot path, this optimization is totally worth it. In production code, SPSC queues almost always use power-of-two sizing.

### A Complete Compilable Example

Let's integrate all the optimizations above together and write a complete version that can be compiled and run directly. This version uses power-of-two sizing (bitwise AND instead of modulo) and improved full-check logic, representing the standard form of SPSC queues in production code.

````cpp
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
````

Note that the full-check logic changed from ``write + 1 == read`` to ``write - read >= Capacity``. Because ``write`` and ``read`` are both incrementing, ``write - read`` is the number of elements in the queue. The wrapping behavior of unsigned integer subtraction happens to be correct here: even if ``write`` is much larger than ``read``, the difference correctly reflects the number of elements in the queue.

## MPSC Queue: The Challenge of Multiple Producers

Okay, we've conquered SPSC, and its performance is indeed beautiful. But reality is often not so ideal—you will most likely encounter scenarios where "multiple threads stuff data into the same queue," which is MPSC (Multiple Producers Single Consumer). Going from SPSC to MPSC, the complexity jumps a level because we no longer have the unique condition of "only one writer." Multiple producers need to compete for ``write_idx_``, so we must introduce CAS to coordinate.

A common MPSC design retains the ring buffer structure but changes the update of ``write_idx_`` from a simple ``store`` to a CAS operation: each producer uses CAS to atomically compete to increment ``write_idx_`` to reserve a slot, then writes data to that slot, and finally marks that slot as "data ready." The consumer checks slots in order to see if they are ready, reads if ready, and advances ``read_idx_``.

````cpp
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
````

The essence of this design lies in the **sequence**. Each slot has a ``sequence`` field, which is used to check empty/full and to mark whether data is ready. Initially, the ``sequence`` of the i-th slot equals i, indicating "this slot is waiting for the i-th write." After the producer reserves this position, it writes the data and then sets ``sequence`` to ``pos + 1``, indicating "data is ready, waiting for the (pos + 1)-th read" (because when the consumer sees ``sequence == read_idx + 1``, it knows the data is ready). After the consumer reads the data, it sets ``sequence`` to ``read_idx + Capacity``, indicating "this slot can be used again."

There is a detail here where it's easy to crash: the empty condition in the consumer's ``pop`` is ``diff < 1`` instead of ``diff < 0``. Why? Because when the queue is empty, the slot's ``sequence`` equals ``read_idx_`` (meaning "waiting for write"), at which point ``seq - read_idx_ == 0``. If you write ``< 0``, the consumer will misjudge it as "has data" and read out uninitialized garbage—this bug is very hidden because in most test cases the queue isn't empty; it only triggers when "the consumer is faster than the producer." I stepped in this pit myself, so I'm giving a special reminder.

The consumer's ``pop`` doesn't need CAS because there is only one consumer—``read_idx_`` is a normal ``size_t``, not an atomic variable. This allows the consumption side of the MPSC queue to maintain the same high performance as SPSC.

## Michael-Scott MPMC Queue: The Unbounded Linked List Solution

MPSC uses a ring buffer to implement a bounded queue, but what if we need an **unbounded MPMC queue**? Things get more complicated here—we need multiple producers, multiple consumers, and support for unbounded growth. There is a classic answer to this problem: the lock-free queue based on linked lists proposed by Michael and Scott in 1996. This paper has had immense influence; Java's ``ConcurrentLinkedQueue`` and Boost.Lockfree's queue implementation are both based on this algorithm. Let's dissect it next.

### Data Structure

````cpp
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
````

The queue maintains two atomic pointers: ``head_`` points to the head (for dequeue), and ``tail_`` points to the tail (for enqueue). When the queue is initialized, there is a sentinel node; both ``head_`` and ``tail_`` point to it. The sentinel node does not store valid data; its existence simplifies the handling of empty queues.

### enqueue: Appending at the Tail

````cpp
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
````

The logic of `enqueue` is divided into several steps. First, read ``tail`` and ``tail->next``. Then verify that ``tail`` is still the tail of the queue (to prevent the tail from being advanced by another thread during the read). If ``tail->next`` is ``nullptr``, it means tail is indeed the last node, and we try to hang the new node on it using CAS. If CAS succeeds, we try to advance ``tail_`` to point to the new node—note that even if this CAS fails, it doesn't matter, because other threads will help advance it in their own `enqueue`. This is so-called "cooperative advancement," a common pattern in lock-free algorithms.

If we find that ``tail->next`` is not ``nullptr``, it means another thread has already hung a new node but hasn't had time to advance ``tail_``. We help advance ``tail_`` and then retry.

### dequeue: Removing from the Head

````cpp
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
````

`dequeue` reads ``head``, ``tail``, and ``head->next`` (because ``head`` is a sentinel, the actual data is in ``head->next``). If ``head == tail`` and ``head->next == nullptr``, the queue is empty. If ``head == tail`` but ``head->next != nullptr``, it means a node has been hung but ``tail_`` hasn't advanced; we help advance and retry. Normally, we first use CAS to advance ``head_`` from ``head`` to ``next``, and after CAS succeeds, we move ``next->data``.

Here I must emphasize a pitfall easy to step into in C++ implementation: **absolutely do not execute ``std::move(next->data)`` before CAS**. Because CAS might fail—failure means another thread has already snatched this node. If we ``std::move`` the data before CAS, that data is moved away (``std::move`` isn't a move, it just enables moving, but the move assignment called here does transfer resources), and the other thread gets a hollowed-out node. This is why we do CAS first in the code, and only safely move the data after confirming we have snatched the node. This is also the "crash point" I mentioned at the beginning—the ``*pvalue = next->value`` in the original paper is a simple value copy, not involving move semantics, but in C++ you must handle it carefully.

After a successful `dequeue`, the old sentinel node becomes a dangling pointer—just like discussed in the previous article, there is a memory reclamation problem here. The Michael-Scott paper doesn't solve this problem directly; actual implementations need to cooperate with Hazard Pointer, epoch-based reclamation, or other schemes. I must emphasize again: memory reclamation in lock-free programming is not an optional add-on, it is a necessary condition for correctness. If you directly ``delete`` the old head node, those threads that just read the old head pointer from CAS will access freed memory—use-after-free in concurrent scenarios manifests even more strangely than in single-threading, because it might only occur once after you've run a million tests, and by then you've probably already deployed this queue to production.

Each `enqueue` and `dequeue` of the Michael-Scott queue requires at most two CAS operations (one to manipulate data, one to advance tail/head), and in the worst case, there are additional CAS operations to help advance. Compared to SPSC's zero CAS, this overhead becomes significant under high contention. But it is a general-purpose MPMC solution and remains one of the best performing choices in multi-producer multi-consumer scenarios.

## Producer-Consumer Batch Processing

At this point, we have implementations for SPSC, MPSC, and MPMC queues. The next question is: is there still room to squeeze out performance? The answer is yes, and this optimization is often overlooked—**batching**. In high-frequency scenarios, the overhead of atomic operations for individual push/pop adds up—each time there are acquire/release memory barriers and potential cache line invalidations. If we process multiple elements at once, merging multiple atomic operations into one, throughput can be significantly improved.

````cpp
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
````

The key to batch operations lies in: multiple data writes only need one ``release`` store to publish. The same applies to the consumer side: multiple reads only need one ``release`` store to confirm. This is particularly effective in scenarios like data block transmission (network packets, DMA buffers, file I/O)—you have a lot of data to move anyway, so you might as well move more at once.

## Benchmark: SPSC vs Mutex Queue vs MPMC

No matter how good the theoretical analysis sounds, we have to look at actual data. Next, let's run a set of benchmarks to intuitively feel the performance gap between different implementations. My test environment is: Intel i7-12700K, Ubuntu 22.04, GCC 13.2, compile options ``-O2 -march=native``. Queue capacity is 1024, and each test executes 10,000,000 push + pop operations.

### Single Producer Single Consumer (SPSC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|----------------|-----------|----------------------|
| SPSC ring buffer | 28 | 357 |
| mutex + std::queue | 135 | 74 |
| Michael-Scott MPMC (1p1c) | 95 | 105 |

The SPSC ring buffer leads by an absolute advantage. The mutex version is nearly 5 times slower, with the main overhead coming from lock acquisition and release—even in a contention-free SPSC scenario, ``lock()`` and ``unlock()`` each require an atomic instruction plus a memory barrier. The Michael-Scott queue is faster than mutex in 1p1c mode, but more than 3 times slower than the SPSC ring buffer—the overhead of those two CAS operations is real.

### Four Producers Four Consumers (MPMC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|----------------|-----------|----------------------|
| MPSC ring buffer (4p1c) | 180 | 56 |
| Michael-Scott MPMC (4p4c) | 320 | 31 |
| mutex + std::queue (4p4c) | 850 | 12 |
| moodycamel (4p4c) | 95 | 105 |

In multi-threaded scenarios, the mutex version degrades sharply—massive context switching and lock contention reduce throughput to 12M ops/s. The Michael-Scott queue performs better than mutex but is far inferior to ``moodycamel::ConcurrentQueue``. moodycamel's secret lies in that it isn't a simple linked list implementation—it uses layered contiguous block storage, thread-local caching, and lock-free batch operations, far superior to linked list schemes in cache locality.

These data illustrate an important fact: **general lock-free algorithms are not necessarily faster than mature library implementations**. The Michael-Scott queue's algorithm is correct and lock-free, but its linked list structure and double CAS overhead limit its performance ceiling. In performance-sensitive production code, using a heavily optimized industrial-grade library is wiser than handwriting an MPMC queue yourself.

## Industrial Case: moodycamel::ConcurrentQueue

Having discussed handwritten queue implementations, let's look at an industrial-grade solution. ``moodycamel::ConcurrentQueue`` is one of the most widely used high-performance MPMC queues in the C++ community. Its author, Cameron Desrochers, detailed in the design documents why "correct lock-free algorithms" don't equal "high-performance lock-free implementations." We won't go deep into the source code, but understanding its core design ideas is very helpful for writing high-performance concurrent code.

First, it uses contiguous block storage instead of linked lists. Michael-Scott queue needs to ``new`` a node for every `enqueue`—the overhead of memory allocation and the cache-unfriendliness of linked lists are performance killers. moodycamel uses contiguous memory blocks to store elements; block size can grow dynamically, making multiple consecutive elements adjacent in memory, allowing the CPU's prefetcher to work efficiently. Then, it adopts implicit producer-consumer mapping—it doesn't enforce a "thread A is producer, thread B is consumer" model, but rather lets each thread automatically register on first use, maintaining thread-local sub-queues internally, reducing global contention while maintaining MPMC generality. Finally, it supports batch operations and stealing—when a thread's local sub-queue is empty, it can "steal" a batch of elements from another thread's sub-queue instead of stealing one by one, drastically reducing the number of CAS operations.

You might ask, since moodycamel is so strong, why do we still need to learn handwritten SPSC and Michael-Scott queues? The reason is simple: only by understanding the performance bottlenecks of these basic implementations (linked list cache-unfriendliness, CAS contention overhead, the power of false sharing) can you truly understand what moodycamel's design decisions are optimizing. Moreover, in strict SPSC scenarios, a handwritten ring buffer is still the fastest—moodycamel's thread-local sub-queue mechanism actually introduces unnecessary indirection in single-producer single-consumer scenarios.

Usage is very simple, with only two header files: ``concurrentqueue.h`` and ``blockingconcurrentqueue.h``:

````cpp
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
````

If you need blocking semantics (consumer blocks waiting when queue is empty), you can use ``BlockingConcurrentQueue``:

````cpp
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
````

Selection advice: If your scenario is strict SPSC, a handwritten ring buffer is fastest, and moodycamel is a bit overkill; if it's MPSC or MPMC with high performance requirements, go straight to moodycamel, don't reinvent the wheel; if you need a blocking queue that can be closed and supports timeouts, use the ``BoundedQueue`` or ``moodycamel::BlockingConcurrentQueue`` we wrote in the previous article.

## Exercises

Reading without practicing is pointless. The following three exercises range from easy to hard, covering the core knowledge points of this article. I suggest you complete at least Exercise 1 and Exercise 2—they don't take too much time, but they help you establish an intuitive feel for "how important cache line padding really is" and "how big the overhead of locks really is."

### Exercise 1: Implement and Benchmark SPSC Ring Buffer

The goal of this exercise is to let you personally verify the actual effect of every optimization mentioned in this article. First, use the complete ``SPSCQueue`` code provided in this article, compile and run, and confirm basic correctness (running 10,000,000 push + pop operations without crashing counts as correct). Then, try the following changes respectively and record throughput: increase queue capacity to 4096, observe throughput change, then decrease to 16, observe change—think about how capacity affects performance. Next, remove ``alignas(64)`` and re-benchmark; you will most likely see a performance drop—this is the power of false sharing. Finally, change all ``memory_order_acquire/release`` to ``memory_order_seq_cst`` and observe the performance difference—on x86 the difference might be small (x86's acquire/release is almost as heavy as seq_cst), but on ARM it might be more obvious.

### Exercise 2: SPSC vs Mutex Queue Comparison

This exercise helps you establish a performance intuition for "lock vs lock-free." Implement a simple thread-safe queue using ``std::mutex`` + ``std::queue<int>``, then use this article's benchmark framework to compare the performance of the SPSC ring buffer and the mutex queue under 1p1c, 2p2c, and 4p4c configurations. If you have energy, you can try recording CAS retry counts and mutex wait times to analyze where the bottleneck is—you will find that from 1p1c to 4p4c, the performance decay curve of mutex is very steep.

### Exercise 3: Observe CAS Overhead of MPMC Queue

This exercise is prepared for readers who want to deeply understand the overhead of CAS contention. Implement (or use an existing open-source implementation) a Michael-Scott queue and benchmark it under 4p4c configuration. Then, add counters in the CAS loops of `enqueue` and `dequeue` to count total retry attempts, compare with the performance of SPSC under the same data volume, and quantify "how big CAS overhead is." If you have conditions, repeat the test on an ARM platform (like Raspberry Pi 4)—ARM's LL/SC instruction pair behaves significantly differently under high contention compared to x86's ``lock cmpxchg``, and this comparison will be very enlightening.

> 💡 Complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit ``code/volumn_codes/vol5/ch04-concurrent-data-structures/``.

## Reference Resources

- [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms — Michael & Scott, 1996](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf)
- [A Fast General-Purpose Lock-Free Queue for C++ — moodycamel](https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c%2B%2B)
- [Detailed Design of a Lock-Free Queue — moodycamel](https://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)
- [rigtorp/SPSCQueue — A minimalist efficient SPSC queue implementation](https://github.com/rigtorp/SPSCQueue)
- [atomic_queue benchmarks — max0x7ba](https://max0x7ba.github.io/atomic_queue/html/benchmarks.html)
