---
title: "SPSC and MPMC Lock-Free Queues"
description: "From a cache-line-padded, power-of-two-capacity, acquire/release-paired SPSC ring buffer, to a sequence-slot MPSC design and the Michael-Scott unbounded queue, closing with a benchmark set comparing against the mutex version and moodycamel's industrial implementation."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - 无锁
  - 循环缓冲区
difficulty: advanced
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/03-lock-free-queues.md
  source_hash: ed721ce0aeb8c78af27cbb409473ab8279d1ec789830130e6b1d2ba2decdf973
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 22628
---
# SPSC and MPMC Lock-Free Queues

This piece follows [Lock-Free Introduction](02-lock-free-intro) toward the practical direction, likewise a migrated piece in the readings section: lock-free containers sit outside the Volume 5 required line, writing one is not an entrance requirement, and the topics worth going deep on all live here. Before putting pen to paper we line up the prerequisites: the CAS loops, ABA, and memory reclamation of Lock-Free Introduction, plus the memory-order criteria of the Unit 5 main text ([The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders)).

The migration trail, made clear: mechanism explanations, code, benchmark readings, and exercises are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The benchmark readings at the end carry over from the material's original records (the environment is noted ahead of the data); I did not re-measure them this round. Your reruns will see the numbers drift, but the magnitudes and relative relationships should hold.

While writing this piece I wrestled for a long time with one question: should I walk everyone through hand-implementing the Michael-Scott queue? Its CAS logic does not look complicated, but once you start writing you find traps everywhere — especially the ordering between the data read and the CAS in dequeue; my first implementation crashed right there. But wrestle as I did, the road still had to be walked once, because only after writing it yourself do you truly understand "why SPSC is so much faster than MPMC".

[Lock-Free Introduction](02-lock-free-intro) built up the basic judgment for lock-free programming. CAS loops, the difference between lock-free and wait-free, the ABA problem, memory reclamation — we have met them all. That knowledge is enough to understand the principles of any lock-free data structure, but a stretch of road remains before writing a genuinely high-performance concurrent queue. Because lock-free is merely the correctness precondition; **cache friendliness** is what holds the key to performance.

In this piece we start from the simplest and most efficient SPSC queue, dial up the complexity step by step, and finally arrive at the MPMC queue. The SPSC (Single Producer Single Consumer) queue has the highest performance ceiling among concurrent queues; in single-produce single-consume scenarios its throughput can approach a single-threaded queue. The reason is simple: with only one producer and one consumer, no CAS and no locks are needed — just a pair of atomic indices and carefully arranged memory orders. We will explain the key optimizations one by one — cache line padding, power-of-two sizing, memory-order choice — because their impact on performance is measured in orders of magnitude.

Then we extend to MPSC (multiple producers, single consumer) and MPMC (multiple producers, multiple consumers) scenarios, discuss the classic Michael-Scott unbounded queue algorithm, and finally run a benchmark set covering SPSC, mutex queues, and MPMC, plus a look at the industrial-grade `moodycamel::ConcurrentQueue` as a practical reference.

## The SPSC Ring Buffer: King of Concurrent Queue Performance

We start with the SPSC queue: it is the foundation of the whole piece and the most used in real engineering. The core data structure of an SPSC queue is the ring buffer: one contiguous block of memory with two indices (read and write) marking data positions, wrapping back to the start at the end. With exactly one producer and one consumer, each index is modified by exactly one thread — `write_idx` is written only by the producer and read only by the consumer, `read_idx` written only by the consumer and read only by the producer. Thanks to this "single writer, single reader" pattern we need no CAS — only `load` and `store` with the right memory orders.

### The Basic Structure

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

The structure has three members: `write_idx_`, `read_idx_`, and `buffer_`. Notice that `write_idx_` and `read_idx_` each carry `alignas(64)` — that is **cache line padding**, one of the most important optimizations in this whole piece. Modern CPU caches move data between cores in units of cache lines (usually 64 bytes). If `write_idx_` and `read_idx_` happen to land in the same cache line (they are adjacent members, so quite likely), every producer write to `write_idx_` invalidates the cache line on the consumer's core, and every consumer read of `read_idx_` invalidates it on the producer's core — this is **false sharing**. Under high-frequency operation false sharing can knock performance down by one to two orders of magnitude. `alignas(64)` ensures each index owns a full cache line, eliminating false sharing. For a fuller account of false sharing and cache-line mechanics, see Unit 6's [How to Adjust the Design](../../chapter06-validating-designs/05-adjusting-design).

> Do not rush on — if you want to feel the power of false sharing firsthand in the exercises later, try removing `alignas(64)` and rerunning the benchmark. You will most likely see throughput drop by half or more, and the gap is even more dramatic on ARM. This optimization is practically standard equipment in every high-performance concurrent data structure; do not lazily skip it.

C++17 offers a more standard spelling: `alignas(std::hardware_destructive_interference_size)`, a compile-time constant meaning "the minimum alignment needed to avoid destructive interference". On x86-64 it is usually 64; on ARM it may differ. If your compiler supports it, use the constant in place of the hardcoded 64.

### Implementing push and pop

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

Let us walk the push flow: the producer works locally with its own `write_idx_` (`relaxed` load), checks whether the queue is full (reading `read_idx_` with `acquire`), writes the data, then publishes the new `write_idx_` (`release` store).

There is a clever detail worth pausing on: `write_idx_` and `read_idx_` are ever-increasing integers, not wrapped indices. The actual buffer position is computed via `write % Capacity`. Writing it this way avoids the wraparound problem of storing the modded index back and makes the "is it full" logic dead simple: `next_write == read_idx` means full. The cost is unbounded index growth, but on a 64-bit platform, running at a billion operations per second for centuries, you still will not overflow it.

The memory-order choices deserve careful words. The producer reads `write_idx_` with `relaxed`, because only the producer writes that variable and it needs to synchronize nothing through it — a purely local counter. The producer reads `read_idx_` with `acquire`, pairing with the consumer's `release` store of `read_idx_`, guaranteeing the producer sees data the consumer has finished consuming. The producer's write to `buffer_` is a plain write (atomicity unneeded, because the consumer will not read that position at this point), followed by the `release` store of `write_idx_`, which guarantees the buffer writes complete before `write_idx_` updates.

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

pop is the mirror of push; read them side by side: the consumer reads its own `read_idx_` with `relaxed`, reads the producer's `write_idx_` with `acquire`, and after taking the data does a `release` store of `read_idx_`. The symmetric acquire/release pairing ensures a correct happens-before relationship between producing and consuming the data.

### The Power-of-Two Sizing Optimization

Good — we now have a working SPSC queue. One more detail can squeeze further performance. Above we computed the buffer position with `write % Capacity`. The modulo is a division instruction on most architectures, and a division's latency (tens of cycles) can become the bottleneck on a hot path. If `Capacity` is a power of two, the modulo optimizes into a bitwise AND: `write & (Capacity - 1)` — a single cycle.

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

This is a classic space-for-time trade: you might need to size the queue 1024 instead of 1000, wasting 24 slots, in exchange for tens of CPU cycles saved per operation. On a hot path, such trades are entirely worth it. In production code, SPSC queues almost always use power-of-two sizing.

### A Complete Compilable Example

Let us fold all the optimizations above together into a complete version you can compile and run directly. This version uses power-of-two sizing (bitwise AND in place of modulo) and the improved fullness check — the standard shape of an SPSC queue in production code.

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

Note the change in the fullness check: from `write + 1 == read` to `write - read >= Capacity`. Since `write` and `read` both only increase, `write - read` is exactly the number of elements in the queue. The wraparound behavior of unsigned subtraction happens to be correct here: even when `write` is far larger than `read`, the difference still faithfully reflects the element count.

## The MPSC Queue: The Multi-Producer Challenge

Right — SPSC is done, and its performance is genuinely beautiful. Reality, though, is rarely that ideal — you will most likely meet the scenario "several threads stuffing data into the same queue": that is MPSC (multiple producers, single consumer). From SPSC to MPSC the complexity jumps a level, because we lose the uniquely favorable condition of "only one writer". Multiple producers must contend for `write_idx_`, and CAS must enter to coordinate.

One common MPSC design, laid out here: it keeps the ring-buffer structure but changes the update of `write_idx_` from a simple `store` into a CAS. Each producer uses CAS to atomically contend for incrementing `write_idx_` — thereby reserving a slot — then writes data into that slot and finally marks the slot "data ready". The consumer checks slots for readiness in order, reading and advancing `read_idx_` as they become ready.

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

The soul of this design is the **sequence**; let us take it apart step by step: each slot carries a `sequence` field, serving both to detect empty/full and to mark data readiness. Initially slot i's `sequence` equals i, meaning "this slot awaits the i-th write". The producer that reserves the position writes data, then sets `sequence` to `pos + 1`, meaning "data is ready, awaiting the (pos + 1)-th read" (the consumer knows data is ready upon seeing `sequence == read_idx + 1`). After reading, the consumer sets `sequence` to `read_idx + Capacity`, meaning "this slot is usable again".

One crash-prone detail here — I lost that bet myself, so a special warning: in the consumer's `pop`, the empty-check is `diff < 1`, not `diff < 0`. Why? When the queue is empty, the slot's `sequence` equals `read_idx_` (meaning "awaiting write"), so `seq - read_idx_ == 0`. Write `< 0` instead, and the consumer mistakes it for "data present" and reads out uninitialized garbage — a very well-hidden bug: most test cases keep the queue non-empty, and it triggers only when the consumer runs faster than the producer.

The consumer's `pop` needs no CAS, because there is exactly one consumer and `read_idx_` is a plain `size_t`, not an atomic. As you can imagine, this keeps the consumer side of an MPSC queue at the same high performance as SPSC.

## The Michael-Scott MPMC Queue: An Unbounded Linked-List Design

MPSC builds a bounded queue on a ring buffer, but what if we need an **unbounded MPMC queue**? Things get more complicated here: multiple producers and multiple consumers, plus unbounded growth. This question has a classic answer, given by Michael and Scott in 1996 — a lock-free queue based on a linked list. That paper's influence is enormous; Java's `ConcurrentLinkedQueue` and Boost.Lockfree's queue are both built on this algorithm. Next we take it apart.

### The Data Structure

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

The queue keeps two atomic pointers; commit them to memory: `head_` points at the queue head (used by dequeue), `tail_` at the tail (used by enqueue). At initialization there is one sentinel node, with both `head_` and `tail_` pointing at it. The sentinel stores no real data; its existence simplifies handling the empty queue.

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

The logic of enqueue runs in a few steps. Read `tail` and `tail->next`. Then verify that `tail` is still the queue's tail, guarding against another thread having advanced it during our reads. If `tail->next` is `nullptr`, tail really is the last node, and we try to hang the new node on with CAS. After the CAS succeeds, we try to advance `tail_` to the new node — note that this CAS failing is fine too, because other threads will help advance it in their own enqueues. This is so-called "cooperative advancement", a common pattern in lock-free algorithms.

If `tail->next` turns out non-null, some other thread has already hung a new node on but has not yet advanced `tail_`. We help advance `tail_`, then retry.

### dequeue: Removing at the Head

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
                // CAS 抢占 head 在前，数据移动在后
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

dequeue reads `head`, `tail`, and `head->next` (since `head` is the sentinel, the actual data sits in `head->next`). If `head == tail` and `head->next == nullptr`, the queue is empty. If `head == tail` but `head->next != nullptr`, a node has been hung on while `tail_` has not advanced; we help advance it and retry. In the normal case, the CAS that moves `head_` from `head` to `next` goes first; only after it succeeds do we move `next->data`.

One crash-prone point in the C++ implementation deserves special emphasis. **Never execute `std::move(next->data)` before the CAS.** The CAS can fail — failure means another thread already claimed this node. If we `std::move` the data before the CAS, the data is gone (`std::move` is not itself a move, it merely makes one possible, but the move assignment called here really does transfer resources), and the other thread receives a gutted node. That is why our code does the CAS first and moves the data only after confirming the node is ours. This is also the "crash point" I mentioned at the start — in the original paper, `*pvalue = next->value` is a plain value copy with no move semantics involved, but in C++ it must be handled with care.

After a successful dequeue, the old sentinel node becomes a dangling pointer — as discussed in Lock-Free Introduction, the memory reclamation problem lives here. The Michael-Scott paper does not address it directly; real implementations need Hazard Pointer, epoch-based reclamation, or some other scheme alongside. I must stress once more: memory reclamation in lock-free programming is not optional — it is a precondition of correctness. If you `delete` the old head node outright, the threads that just read the old head pointer from their CASes will touch freed memory. Use-after-free behaves more eerily in concurrent settings than in single-threaded ones: it may fire only once after a million test runs, and by then you have probably already deployed the queue to production.

Each enqueue and dequeue on the Michael-Scott queue needs at most two CASes (one for the data operation, one to advance tail/head), with extra helping CASes in the worst case. Compare that with SPSC's zero CASes and the overhead becomes significant under high contention. But as a general-purpose MPMC scheme it remains one of the best-performing choices in multi-producer multi-consumer scenarios.

## Producer-Consumer Batching

By now we hold implementations of SPSC, MPSC, and MPMC queues. The next question: is there any performance left to squeeze? There is, and the optimization is often overlooked — **batching**. In high-frequency scenarios, the atomic overhead of pushing and popping one at a time accumulates: each round pays acquire/release memory barriers and possible cache-line invalidations. Process several elements at once, merging many atomic operations into one, and throughput can rise substantially.

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

The essence of batching: many data writes need only one `release` store to publish. The consumer side mirrors it — many reads need only one `release` store to confirm. This pays off especially in block-transfer scenarios (network packets, DMA buffers, file I/O): you were moving bulk data anyway, so might as well move more per trip.

## Benchmark: SPSC vs Mutex Queue vs MPMC

However sweet the theory sounds, the actual data decides; next we run a benchmark set to feel the performance gaps between implementations directly. My test environment: Intel i7-12700K, Ubuntu 22.04, GCC 13.2, compile flags `-O2 -march=native`. Queue capacity 1024; each test performs 10,000,000 push + pop operations. (The general methodology for controlling measurement conditions is the whole subject of [Before Measuring, Set the Conditions: Serial Baselines and Measurement Traps](../../chapter06-validating-designs/03-measuring-benefits).)

### Single Producer, Single Consumer (SPSC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|------|-----------|----------------|
| SPSC ring buffer | 28 | 357 |
| mutex + std::queue | 135 | 74 |
| Michael-Scott MPMC (1p1c) | 95 | 105 |

The SPSC ring buffer opens a wide gap — look at the numbers: the mutex version is nearly 5x slower, with the bulk of the cost in lock acquire and release; even in an uncontended SPSC scenario, `lock()` and `unlock()` each need an atomic instruction plus a memory barrier. The Michael-Scott queue in 1p1c mode beats mutex but trails the SPSC ring buffer by more than 3x — those two CASes cost real money.

### Four Producers, Four Consumers (MPMC)

| Implementation | Time (ms) | Throughput (M ops/s) |
|------|-----------|----------------|
| MPSC ring buffer (4p1c) | 180 | 56 |
| Michael-Scott MPMC (4p4c) | 320 | 31 |
| mutex + std::queue (4p4c) | 850 | 12 |
| moodycamel (4p4c) | 95 | 105 |

As we can see, the mutex version degrades sharply in the multithreaded scenario — hordes of context switches and lock contention drag throughput down to 12M ops/s. The Michael-Scott queue fares better than mutex but falls far short of `moodycamel::ConcurrentQueue`. moodycamel's secret is that it is no plain linked list: it uses tiered contiguous block storage, thread-local caches, and lock-free bulk operations, beating linked-list designs on cache locality by a wide margin.

The data hands us an important fact: **a general-purpose lock-free algorithm is not necessarily faster than a mature library implementation**. The Michael-Scott queue's algorithm is correct and lock-free, but its linked-list structure and double-CAS overhead cap its performance. In performance-sensitive production code, choosing a heavily optimized industrial library over a hand-written MPMC queue is the wiser move.

## Industrial Case: moodycamel::ConcurrentQueue

Having talked through the hand-written queue implementations, let us look at the industrial-grade option. `moodycamel::ConcurrentQueue` is one of the most widely used high-performance MPMC queues in the C++ community; its author Cameron Desrochers has written in the design notes about why a "correct lock-free algorithm" is not the same as a "high-performance lock-free implementation". We will not dive into the source, but understanding its core design ideas helps a lot in writing high-performance concurrent code.

Watch its three moves. First, contiguous block storage replaces the linked list: a Michael-Scott queue `new`s a node on every enqueue, and allocation cost plus the linked list's cache hostility are performance killers. moodycamel stores elements in contiguous memory blocks that can grow dynamically, so successive elements sit adjacent in memory and the CPU's prefetcher works efficiently. Second, it adopts an implicit producer-consumer mapping: instead of forcing a "thread A is the producer, thread B is the consumer" model, each thread auto-registers on first use of the queue, with thread-local subqueues maintained internally — reducing global contention while keeping MPMC generality. Finally, it supports bulk operations and stealing: when a thread's local subqueue empties, it can "steal" a batch of elements from another thread's subqueue rather than one at a time, slashing the number of CASes.

You might ask: if moodycamel is this strong, why still learn hand-written SPSC and Michael-Scott queues? The reason is simple: only by understanding the performance bottlenecks of these foundational implementations (the linked list's cache hostility, CAS contention overhead, the power of false sharing) can you truly understand what moodycamel's design decisions are optimizing. Moreover, in strict SPSC scenarios the hand-written ring buffer is still the fastest — moodycamel's thread-local subqueue machinery introduces an unnecessary layer of indirection in the single-producer single-consumer case.

Usage is dead simple — just two headers, `concurrentqueue.h` and `blockingconcurrentqueue.h`. Let us run a minimal example:

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

If you need blocking semantics (the consumer blocks waiting when the queue is empty), use `BlockingConcurrentQueue`:

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

Selection advice: for a strict SPSC scenario, a hand-written ring buffer is fastest, and moodycamel is somewhat overkill. For MPSC or MPMC with high performance demands, go straight to moodycamel — do not build your own wheel. If you need a closable blocking queue with timeout support, use the `BoundedQueue` from [A Queue with Capacity: First Make It Stand](../../chapter03-wait-and-coordinate/03-bounded-queue) or `moodycamel::BlockingConcurrentQueue`.

## Exercises

Watching without practicing counts for nothing. The three exercises below run from easy to hard and cover this piece's core points. We suggest you complete at least Exercises 1 and 2 — they take little time, but they build two visceral senses: how much cache line padding really matters, and how much locks really cost.

### Exercise 1: Implement and Benchmark the SPSC Ring Buffer

This exercise aims to let you personally verify the actual effect of every optimization mentioned in this piece. Take the complete `SPSCQueue` code from this piece, compile and run it, and confirm basic correctness (finishing 10,000,000 push + pop rounds without crashing counts as correct). Then try the following variations one by one and record throughput: raise the queue capacity to 4096 and watch the change, then drop it to 16 and watch again — think about how capacity influences performance. Next, remove `alignas(64)` and re-benchmark; you will most likely see performance fall — that is the power of false sharing. Finally, change every `memory_order_acquire/release` to `memory_order_seq_cst` and observe the difference; on x86 it may be small (x86's acquire/release is nearly as heavy as seq_cst), but on ARM it can be more visible.

### Exercise 2: SPSC vs Mutex Queue Comparison

This exercise builds your performance intuition for "lock vs lock-free". Implement a simple thread-safe queue with `std::mutex` + `std::queue<int>`, then use this piece's benchmark framework to compare the SPSC ring buffer against the mutex queue under 1p1c, 2p2c, and 4p4c. If you have the energy, record CAS retry counts and mutex wait times and analyze where the bottleneck sits — you will find the mutex decay curve from 1p1c to 4p4c is extremely steep.

### Exercise 3: Observing the CAS Cost of the MPMC Queue

This exercise is for readers who want to go deeper into CAS contention cost. Implement (or take an existing open-source implementation of) the Michael-Scott queue and benchmark it under 4p4c. Then add counters to the CAS loops in enqueue and dequeue, tally total retries, and compare against SPSC at the same data volume to quantify just how big the "CAS cost" is. If circumstances allow, repeat the test on an ARM platform (a Raspberry Pi 4, say): ARM's LL/SC instruction pairs behave distinctly differently from x86's `lock cmpxchg` under high contention, and the comparison is highly illuminating.

> The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); take it from `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Reference Resources

- [Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms — Michael & Scott, 1996](https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf)
- [A Fast General-Purpose Lock-Free Queue for C++ — moodycamel](https://moodycamel.com/blog/2014/a-fast-general-purpose-lock-free-queue-for-c%2B%2B)
- [Detailed Design of a Lock-Free Queue — moodycamel](https://moodycamel.com/blog/2014/detailed-design-of-a-lock-free-queue)
- [std::hardware_destructive_interference_size — cppreference](https://en.cppreference.com/cpp/thread/hardware_destructive_interference_size)
- [rigtorp/SPSCQueue — a minimal and efficient SPSC queue implementation](https://github.com/rigtorp/SPSCQueue)
- [atomic_queue benchmarks — max0x7ba](https://max0x7ba.github.io/atomic_queue/html/benchmarks.html)
