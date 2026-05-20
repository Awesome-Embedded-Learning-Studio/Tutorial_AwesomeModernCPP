---
title: Lock-Free Programming Fundamentals
description: CAS loops, lock-free vs. wait-free, the ABA problem, and memory reclamation
  challenges—building foundational judgment for lock-free programming.
chapter: 4
order: 3
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
difficulty: advanced
platform: host
reading_time_minutes: 25
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 原子操作模式
related:
- SPSC 与 MPMC 队列
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/03-lock-free-basics.md
  source_hash: 8bc0fe05876e6efe7565af0e9169a74089ffe28ab7016505920ed17fc98e20e5
  translated_at: '2026-05-20T04:41:18.899171+00:00'
  engine: anthropic
  token_count: 4442
---
# Lock-Free Programming Fundamentals

In the previous two articles, we built thread-safe queues and containers using `mutex` + `condition_variable`. In ch03, we exhaustively covered the `std::atomic` operation set and all six memory orders, and in the "Atomic Operation Patterns" article, we implemented a SeqLock, a spinlock, and a reference counter. Those articles answered the question of "how to perform atomic operations," but we haven't touched upon a deeper question yet: **if we completely avoid locks, can we write correct concurrent data structures?**

To be honest, the first time the author heard the term "lock-free programming," the immediate reaction was, "Isn't this just showing off?" It wasn't until looking at a few lock-free stack implementations that it became clear this wasn't posturing—it's an entirely different mindset from lock-based concurrency. Instead of wrapping a critical section with a lock to make threads line up, all threads operate on the data structure simultaneously, using atomic operations to coordinate conflicts—those who conflict simply retry, but the system as a whole always moves forward. The cost of this approach is a massive increase in the complexity of correctness reasoning, and the benefit is more controllable latency in high-contention scenarios.

The term "lock-free" is actually quite misleading—it doesn't mean using no locks at all, but rather that the overall progress of the system cannot be blocked by the delay or crash of any single thread. This distinction is important and subtle. The author got tripped up by it several times when first entering this field, so in this article we will start with the precise definition of progress guarantees, thoroughly clarify the difference between lock-free and wait-free, and then move into the CAS loop, the core building block of lock-free programming. We will implement a classic lock-free stack, and then discuss the ABA problem and memory reclamation—two of the most notoriously difficult problems in lock-free programming. Finally, we will discuss when to use lock-free and when not to—this judgment is more important than knowing how to write lock-free code itself.

## Lock-free vs Wait-free: What Exactly Is Guaranteed

Many people understand "lock-free" as "not using `mutex`." This understanding isn't wrong, but it's imprecise—quite far from the full picture. In academia, Herlihy laid the foundation for the definitions of wait-free and lock-free in his 1991 paper, and later Herlihy, Luchangco, and Moir introduced the weaker concept of obstruction-free in 2003. The C++ standard and industry largely follow this three-tier framework, so we need to clarify the three levels of progress guarantees first.

Let's start with the weakest: **obstruction-free** guarantees that if a thread is executed in isolation at some point in time—meaning all other threads are paused—it can complete its operation in a finite number of steps. Put simply, "if there's no contention, it can make progress." This guarantee is too weak to have any practical value, so we won't discuss it further.

**Lock-free** takes it a step further: it guarantees that at any given moment, **at least one thread** in the system can complete its operation in a finite number of steps. Note that this is "at least one," not "every single one." This means that in a lock-free system, the system as a whole is making progress, but individual threads might keep retrying due to continuous CAS failures—theoretically, starvation is possible. The spinlock we wrote in the previous article is not lock-free: if one thread holds the lock and doesn't let go (for example, if it gets suspended by the OS), all other threads have to wait, and the system as a whole stalls.

**Wait-free** is the strongest guarantee: **every single thread** is guaranteed to complete its own operation in a finite number of steps, regardless of what other threads are doing or how fast they are running. Wait-free means no starvation, no retry loops, and every operation has a deterministic upper bound on the number of steps.

The hierarchy from weak to strong is: blocking -> obstruction-free -> lock-free -> wait-free. With each step up, the implementation difficulty increases dramatically. In practical engineering, we usually aim for lock-free, because the implementation cost of wait-free is too high, and lock-free is already good enough in most scenarios—at least the system won't completely freeze because one thread gets stuck.

There is a common misconception that needs to be cleared up right away: **lock-free does not mean "faster."** Lock-free solves the progress guarantee problem, not the performance problem. A lock-free data structure might actually be slower than a `mutex` version under low contention, because the overhead of CAS retries might be greater than simply acquiring a lock. The advantage of lock-free shows up in high-contention, latency-sensitive scenarios—it won't cause the entire critical section to stall just because some thread gets paused by the scheduler. We will expand on this distinction with concrete data later in the "When to Use Lock-Free" section.

## The CAS Loop: The Cornerstone of Lock-Free Programming

Alright, with the concept of progress guarantees cleared up, let's get our hands dirty. Almost all lock-free algorithms are built on top of one atomic primitive: Compare-And-Swap (CAS). In C++, this corresponds to the `compare_exchange_weak` and `compare_exchange_strong` member functions of `std::atomic`. We already introduced the signatures and semantics of these two functions in the "Atomic Operations" article in ch03, so we won't repeat the basics here. Instead, we will focus on their usage patterns in lock-free programming.

If you remember the ch03 content, the core semantics of CAS can be summarized in one sentence: **"I think the current value should be X; if it is, change it to Y; otherwise, tell me what it actually is right now."** In code, `compare_exchange` takes two key parameters—`expected` (the expected value) and `desired` (the new value). If the current value equals `expected`, it is changed to `desired` and returns `true`; if not, the current value is written back into `expected` and it returns `false`. The entire operation is atomic—no modifications from other threads can slip in between the "compare" and the "swap."

We also discussed the difference between weak and strong in ch03, so let's do a quick recap. `compare_exchange_weak` allows spurious failure: even if the current value actually equals `expected`, it might still return `false`. This is unavoidable on certain hardware architectures (like ARM's LL/SC instruction pair). `compare_exchange_strong` guarantees no spurious failure. On x86, weak and strong generate exactly the same machine code (both are `CMPXCHG`), but on ARM, the strong version needs an internal retry loop to eliminate spurious failures.

A key rule of thumb—same as what we said in ch03: **use weak inside loops, and use strong for one-shot checks outside loops.** The reason is straightforward—if you're already in a loop, you're going to retry after a CAS failure anyway, so an extra spurious failure just means one more loop iteration. But if you use weak outside a loop, a single spurious failure will cause you to incorrectly believe the value has changed, potentially taking the wrong branch. On ARM, using strong inside a loop results in nested retry loops (your outer loop plus the inner loop of strong), wasting instructions for nothing.

Let's first look at the simplest CAS loop—a manual implementation of atomic addition. While unnecessary in real engineering (`fetch_add` is sufficient), this example clearly demonstrates the basic structure of a CAS loop and serves as the foundation for the lock-free stack we will write later:

```cpp
std::atomic<int> value{0};

void atomic_add(int delta)
{
    int old = value.load(std::memory_order_relaxed);
    while (!value.compare_exchange_weak(
        old,
        old + delta,
        std::memory_order_relaxed,
        std::memory_order_relaxed))
    {
        // CAS 失败时 old 被自动更新为当前值
        // 重新计算 old + delta，然后重试
    }
}
```

What this loop does is: read the current value, compute the new value, and then try to swap the current value from `old_val` to `new_val`. If another thread modified `counter` during this process, CAS will fail and tell us what the latest value is (by writing it back into the `old_val` parameter), and we just need to recompute with the latest value and try again. This is so-called "optimistic concurrency": assume no conflicts, and retry if conflicts occur. You'll notice that this loop cannot be an infinite loop—after each failure, `old_val` is updated to a newer value, and the system as a whole moves forward—this is the manifestation of lock-free semantics at the micro level.

Of course, for an addition operation, just using `fetch_add` is fine; there's no need to write a CAS loop manually. The real power of the CAS loop emerges in more complex operations—like updating linked list pointers or swapping the head node of a data structure. These operations cannot be expressed with simple `fetch_add` or `exchange` and must use CAS. Next, let's write a real lock-free data structure.

## The Classic Lock-Free Stack: From CAS Loops to Real Data Structures

Having understood the basic pattern of the CAS loop, we can now tackle a real lock-free data structure. The lock-free stack is the simplest of all lock-free data structures, and it's the starting point for almost all lock-free programming textbooks—Treiber published its design back in 1986. Let's first set up the overall structure, and then break down the implementations of push and pop step by step.

```cpp
#include <atomic>
#include <optional>

template <typename T>
class LockFreeStack {
public:
    LockFreeStack() : head_(nullptr) {}
    ~LockFreeStack();

    void push(const T& value);
    std::optional<T> pop();

private:
    struct Node {
        T data;
        Node* next;
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    std::atomic<Node*> head_;
};
```

The structure is very simple: a singly linked list where `head_` is an atomic pointer pointing to the top node of the stack. All operations happen at the head, requiring synchronization of only this one pointer.

### push: Inserting a Node at the Top

```cpp
void push(const T& value)
{
    Node* new_node = new Node(value);
    Node* old_head = head_.load(std::memory_order_relaxed);

    do {
        new_node->next = old_head;
    } while (!head_.compare_exchange_weak(
        old_head,
        new_node,
        std::memory_order_release,
        std::memory_order_relaxed));
}
```

The logic of push has three steps: create a new node, point the new node's `next` to the current top of the stack, and then try to use CAS to swap `head_` from `old_head` to `new_node`. If CAS succeeds, the new node becomes the new top of the stack. If CAS fails, it means another thread beat us to modifying `head_`, but `compare_exchange_weak` will update `old_head` to the latest value, and we just need to reset `new_node->next` and try again.

Note the choice of memory orders: when CAS succeeds, we use `memory_order_release`, which guarantees that the writes to `next` and `data` in the new node complete before the CAS succeeds, so other threads that read the new value of `head_` via `memory_order_acquire` are guaranteed to see those writes. When CAS fails, `memory_order_relaxed` is sufficient—nothing was changed, so no synchronization is needed. The initial `load` of `head_` also uses `memory_order_relaxed`, because the real synchronization is guaranteed by the memory order of the CAS operation itself.

### pop: Removing a Node from the Top

```cpp
std::optional<T> pop()
{
    Node* old_head = head_.load(std::memory_order_acquire);

    while (old_head) {
        Node* next_node = old_head->next;
        if (head_.compare_exchange_weak(
                old_head,
                next_node,
                std::memory_order_acquire,
                std::memory_order_relaxed)) {
            // CAS 成功，old_head 已经从栈上摘下来了
            T value = std::move(old_head->data);
            // ⚠️ 这里有一个严重的问题：什么时候 delete old_head？
            return value;
        }
        // CAS 失败，old_head 已被更新为最新值，重试
    }

    return std::nullopt;  // 栈空
}
```

The logic of pop is also quite intuitive: read the current top of the stack, note its `next`, and then try to use CAS to swap `head_` from `old_head` to `old_head->next`. If successful, `old_head` has been detached from the stack, and we extract its data and return.

But—things aren't over yet. There is a huge pitfall in this code, which the author has marked with a comment. We have `old_head`, and we know it has been detached from the stack, but **we cannot immediately `delete` it**. The reason is: before we executed CAS, other threads might have also read the same `old_head` and are currently accessing its `next` pointer. If we free `old_head`'s memory right now, those threads would be accessing freed memory—use-after-free, a classic case of undefined behavior (UB). This problem cannot be solved by simply adding a `std::atomic` like a data race can—it is a **logical-level lifetime issue**.

This is the most notoriously difficult **memory reclamation problem** in lock-free programming. Let's set it aside for now and discuss it together after covering the ABA problem—the ABA and memory reclamation problems are intertwined, and it's hard to see the full picture if we look at them separately.

## The ABA Problem: CAS's Number One Trap

Next up is the most infamous bug pattern in lock-free programming—the ABA problem. If you've ever been asked about lock-free programming in an interview, chances are you've been asked about this too. It's famous not because it's hard to understand, but because it actually happens in practice, and once it does, it's extremely difficult to debug—the program won't crash; it will just silently produce incorrect results.

### How ABA Happens

Let's demonstrate with a concrete scenario. Suppose two threads are operating on our lock-free stack, and the initial state is A -> B -> C, with A at the top.

Thread 1 starts executing `pop`: it reads `head_`, gets A, and prepares to execute CAS to swap `head_` from A to B. But right before the CAS, Thread 1 gets suspended by the scheduler—this is where the trouble begins.

Thread 2 starts working at this point: it fully executes two `pop`s, first popping A off (the stack becomes B -> C), then popping B off (the stack becomes C). Then Thread 2 `push`es a new value, and by coincidence the allocator reuses A's memory address, so the new node's address is exactly the same as the previous A's. Now the stack is A' -> C, but this A' has the exact same address as the previous A.

Thread 1 wakes up and executes CAS: `head_.compare_exchange_weak(A, B)`. It finds that `head_` is indeed A (same address), so CAS succeeds, and `head_` is set to B.

Here's the problem: B has already been popped and freed by Thread 2. Thread 1 has pointed `head_` to an already-invalidated node. Any subsequent operation on the stack will access freed memory—the program could crash at any moment, or worse, silently produce incorrect results, and you would have no idea where to start looking.

### Why ABA Is So Dangerous

The reason ABA is insidious is that CAS only cares about "whether the value equals the expected value," not "whether the value has changed in the meantime." In the ABA scenario, the pointer's value does indeed go from A to A (passing through B in between), and CAS cannot distinguish between "it's always been A" and "A -> B -> A"—to CAS, these two situations are exactly the same. This is not a design flaw in CAS, but an inherent limitation of it as a "value comparison" primitive.

You might ask: does this really happen in practice? The answer is yes. In high-contention environments, nodes are frequently allocated and freed, and memory allocators are very likely to reuse recently freed addresses—especially allocators like `jemalloc`/`tcmalloc` that are optimized for small objects. They maintain free lists bucketed by size, and freshly freed memory can be immediately allocated out again. Combined with multi-threaded scheduling timing, the scenario of "Thread 1 reads and then gets suspended, Thread 2 does a full round of operations" can absolutely occur.

### Tagged Pointer: Adding Version Numbers to Pointers

Alright, the problem is clear, so let's look at the solution. The most common approach is the **tagged pointer**. The idea is straightforward: pack the pointer together with an incrementing version number, and increment the version number each time the pointer is modified. This way, even if the pointer's value goes from A -> B -> A, the version number goes from 0 -> 1 -> 2, and CAS will correctly fail due to the version number mismatch—the version number only increases and never decreases, so a wraparound is impossible.

On 64-bit systems, we can use the upper 16 bits of the pointer to store the version number (because on most architectures, user-space pointers only use the lower 48 bits). Here is a simplified implementation:

```cpp
#include <atomic>
#include <cstdint>

template <typename T>
class TaggedPointer {
public:
    TaggedPointer() : atomic_(0) {}
    TaggedPointer(T* ptr, uint16_t tag)
    {
        uint64_t raw = (static_cast<uint64_t>(tag) << kTagShift)
                     | reinterpret_cast<uint64_t>(ptr);
        atomic_.store(raw, std::memory_order_relaxed);
    }

    T* get_ptr() const
    {
        return reinterpret_cast<T*>(atomic_.load(std::memory_order_relaxed) & kPtrMask);
    }

    uint16_t get_tag() const
    {
        return static_cast<uint16_t>(atomic_.load(std::memory_order_relaxed) >> kTagShift);
    }

    bool compare_exchange_weak(TaggedPointer& expected, TaggedPointer desired)
    {
        uint64_t exp_value = expected.atomic_.load(std::memory_order_relaxed);
        if (atomic_.compare_exchange_weak(exp_value,
                desired.atomic_.load(std::memory_order_relaxed))) {
            return true;
        }
        expected = TaggedPointer(exp_value);
        return false;
    }

    TaggedPointer load() const
    {
        return TaggedPointer(atomic_.load(std::memory_order_acquire));
    }

    void store(TaggedPointer tp)
    {
        atomic_.store(tp.atomic_.load(std::memory_order_relaxed),
                     std::memory_order_release);
    }

private:
    std::atomic<uint64_t> atomic_;
    static constexpr uint64_t kTagShift = 48;
    static constexpr uint64_t kPtrMask = (1ULL << kTagShift) - 1;

    explicit TaggedPointer(uint64_t raw) : atomic_(raw) {}
};
```

Rewriting the lock-free stack's `push` with a tagged pointer:

```cpp
void push(const T& value)
{
    Node* new_node = new Node(value);
    TaggedPointer<Node> old_head = head_.load();

    do {
        new_node->next = old_head.get_ptr();
    } while (!head_.compare_exchange_weak(
        old_head,
        TaggedPointer<Node>(new_node, old_head.get_tag() + 1)));

    // 每次成功 CAS 都伴随着 tag + 1
    // 即使指针地址被复用，tag 不会重复，ABA 不会发生
}
```

The tagged pointer approach has a prerequisite: the architecture you're using must support CAS operations on 64 bits (or 128 bits, if you want to use more version number bits). On x86-64, this is not a problem; `CMPXCHG` natively supports 64-bit operations. On certain 32-bit embedded platforms, double-word CAS might be unavailable or very expensive, requiring other approaches.

### Hazard Pointer: A More Universal Memory Protection

The tagged pointer solves the ABA problem, but you'll notice it doesn't solve the memory reclamation problem we mentioned earlier—we still don't know when it's safe to `delete` a node. Hazard Pointer is a more universal approach proposed by Maged Michael in 2004. It solves both the ABA and memory reclamation problems simultaneously, and it's not limited to stacks—it works for queues, linked lists, and various other lock-free data structures. C++26 has already incorporated Hazard Pointers into the standard (`std::hazard_pointer`).

The core idea of Hazard Pointers is very elegant: each thread holds one or a set of "hazard pointers" used to declare "I am currently accessing this node." When a thread wants to free a node, it cannot directly `delete` it; instead, it must first check all threads' hazard pointers—if someone is using this node, it defers the deallocation. Only when it confirms that no thread's hazard pointer points to this node can it be safely freed.

Simplified pseudocode is as follows:

```cpp
// 全局的 hazard pointer 表，每个线程一个槽位
constexpr int kMaxThreads = 64;
std::atomic<Node*> g_hazard_pointers[kMaxThreads];

// 线程在访问节点前，先"发布"自己的 hazard pointer
void publish_hazard(int slot, Node* node)
{
    g_hazard_pointers[slot].store(node, std::memory_order_release);
}

// 释放节点前，检查是否有线程在用
bool is_hazardous(Node* node)
{
    for (int i = 0; i < kMaxThreads; ++i) {
        if (g_hazard_pointers[i].load(std::memory_order_acquire) == node) {
            return true;
        }
    }
    return false;
}
```

In the lock-free stack's `pop`, the usage looks roughly like this: the thread first publishes a hazard pointer pointing to `old_head`, then executes CAS. If CAS succeeds, the thread clears its own hazard pointer and puts `old_head` into a "to-be-reclaimed list." Periodically (for example, when the to-be-reclaimed list accumulates to a certain length), the thread scans all hazard pointers and truly frees the nodes that no one is using.

The advantage of Hazard Pointers is good generality, applicable to various lock-free data structures. The disadvantage is performance overhead: every `pop` requires publishing and clearing a hazard pointer, and scanning the to-be-reclaimed list also requires traversing all threads' slots. In high-contention scenarios, this overhead can be significant.

## Memory Reclamation: The Hardest Problem in Lock-Free Programming

We've bumped into this problem repeatedly earlier, and each time we "set it aside for now." Now it's time to face it head-on. If you thought the ABA problem was already a headache, memory reclamation will give you an even bigger one—it is widely recognized as the most difficult problem in lock-free programming, and one of the biggest obstacles preventing lock-free data structures from being widely used in real projects.

In lock-based data structures, memory reclamation is simple: acquire the lock, operate, free the memory, release the lock. Because the lock guarantees that only one thread is operating on the data structure at any given moment, there's no problem of "one thread is still using a node while another thread frees it."

But in lock-free data structures, multiple threads can read the same node simultaneously. Thread A has just finished reading `old_head` and is about to execute CAS, while Thread B might have already popped `old_head` off and `delete`d it. Thread A's CAS hasn't executed yet, and the `old_head` in its hands is already a dangling pointer. This problem cannot be eliminated through `std::atomic` like a data race can—it is a **logical-level lifetime issue**.

There are currently several mainstream solutions in the industry. Besides the Hazard Pointer mentioned earlier, there are **Epoch-based Reclamation** and **reference counting**.

The idea behind Epoch-based Reclamation is to divide time into several "epochs" and maintain a global current epoch number. Each thread records the epoch it is in when entering the critical section. During reclamation, nodes from a given epoch can only be safely freed after all threads have left that epoch. This approach has lower scanning overhead than Hazard Pointers, but the implementation is more complex, and in certain extreme cases, reclamation might be delayed for a long time—if a thread gets stuck in an old epoch and doesn't come out, all nodes from old epochs will pile up and cannot be freed. Facebook's Folly library has a production-grade implementation (the `RCU` mechanism in `folly/synchronization/` uses a similar approach).

Reference counting sounds the most intuitive: add an atomic reference count to each node, decrement it on `pop`, and free it when it reaches zero. But the problem is that incrementing and decrementing the reference count themselves also require atomic operations, and there is a window between "loading the pointer" and "incrementing the reference count"—during this window, the node might be freed by another thread. To solve this "load-increment" atomicity problem, reference counting schemes often degenerate into some form of Hazard Pointer or require double-word CAS, so the implementation complexity doesn't truly decrease. `std::shared_ptr` can be used in C++20, but its performance overhead (usually implemented with an internal spinlock) makes it unsuitable for true lock-free scenarios.

## When to Use Lock-Free—And When Not To

After discussing all these problems and solutions, you might ask: if lock-free programming is this complex, why bother with it? The answer is: in specific scenarios, lock-free can indeed deliver performance advantages that `mutex` cannot provide. But this "specific scenario" is much narrower than you might think. The author has seen quite a few cases where people spent a lot of effort converting a `mutex`-protected data structure to a lock-free one, only to find that the benchmark ran slower—then they stared at the data in a daze.

### Scenarios Suited for Lock-Free

**High contention, low latency** is the most typical scenario. When a large number of threads frequently compete for the same data structure, `mutex` causes frequent context switches (each switch is a round-trip to kernel space, costing on the order of microseconds). Lock-free algorithms turn contention from "queuing for a lock" into "CAS retries." Although retries have overhead too, they happen in user space without involving kernel scheduling, making latency more controllable and tail latency smaller. High-frequency trading systems, real-time signal processing, and the main loops of online game servers—in these scenarios, a few microseconds of latency difference might be the dividing line between acceptable and unacceptable.

**Single-Producer Single-Consumer (SPSC) queues** are another scenario particularly well-suited for lock-free. Because there is only one producer and one consumer, no CAS loop is needed; correct synchronization can be achieved with atomic variables using only `store`/`load` semantics. The implementation is simple, the performance is extremely high, and there is almost no contention—in this scenario, lock-free is almost the default choice. We will dedicate the next article to a detailed breakdown of SPSC queue design.

**Communication between interrupt contexts and the main loop** is also common in embedded systems. Interrupt service routines (ISRs) cannot call functions that might block (including `mutex::lock`), making lock-free queues almost the only choice.

### Scenarios Not Suited for Lock-Free

Don't rush to replace all the `mutex` instances in your project—in these scenarios, lock-free is often a losing proposition.

Under **low-contention scenarios**, lock-free is often slower than `mutex`. The reason is simple: the lock/unlock overhead of `mutex` without contention is actually very low (one atomic instruction plus a branch prediction), while a CAS loop requires at least one atomic operation and one conditional check even on the success path. If your data structure encounters contention only once every 1,000 accesses on average, the total overhead of `mutex` is likely lower than lock-free.

**Complex critical sections** are not suited for lock-free. If your operation involves coordinated modifications of multiple variables (like "deleting an element from a map while simultaneously updating a size counter"), expressing such compound operations with CAS is extremely difficult. The code is hard to implement correctly and even harder to maintain. `mutex` naturally supports arbitrarily complex critical sections, and this advantage is irreplaceable in the face of complex logic.

**Team maintenance cost** is also a consideration that cannot be ignored. The difficulty of reading, reviewing, and debugging lock-free code is far higher than the `mutex` version. A bug in a CAS loop might only trigger once in a million runs, and ThreadSanitizer's false positive rate for lock-free code is not low. If your team doesn't have sufficient lock-free programming experience, writing correct code with `mutex` is more valuable than writing fast but unreliable code with CAS—correct code is always better than fast incorrect code.

### Benchmark: Don't Guess, Measure

Any assertion about "lock-free is faster" or "`mutex` is faster" is empty talk without concrete benchmark data. The author has seen too many cases where "lock-free is theoretically faster" but is actually slower due to cache coherence overhead, CAS retry storms, false sharing, and other reasons—the bottlenecks in concurrent performance often show up where you least expect them.

A basic benchmark framework should include: throughput tests under different thread counts (1, 2, 4, 8, 16), latency distribution (p50, p99, p999) under different operation ratios (pure push, pure pop, mixed), and result comparisons on different hardware. When we implement SPSC and MPMC queues in the next article, we will do a complete benchmark comparison.

Here is a simple but effective benchmark template:

```cpp
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

/// 测量 N 次 push + N 次 pop 的总耗时
template <typename Queue, typename T>
void benchmark_queue(Queue& q, int num_items, int num_producers, int num_consumers)
{
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    std::atomic<int> consumed_count{0};

    for (int i = 0; i < num_producers; ++i) {
        producers.emplace_back([&q, num_items, num_producers] {
            int per_producer = num_items / num_producers;
            for (int j = 0; j < per_producer; ++j) {
                while (!q.push(T(j))) {
                    // 队列满，重试
                }
            }
        });
    }

    for (int i = 0; i < num_consumers; ++i) {
        consumers.emplace_back([&q, &consumed_count, num_items] {
            T value;
            while (consumed_count.load(std::memory_order_relaxed) < num_items) {
                if (q.pop(value)) {
                    consumed_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "Items: " << num_items
              << " | Producers: " << num_producers
              << " | Consumers: " << num_consumers
              << " | Time: " << ms << " ms"
              << " | Throughput: " << (num_items * 1000.0 / ms) << " ops/s"
              << "\n";
}
```

When running benchmarks, it's recommended to disable CPU frequency scaling (`cpupower frequency-set -g performance`), pin to CPU cores (`taskset` or `pthread_setaffinity_np`), and run multiple times taking the median. These methods of controlling variables have a significant impact on concurrent benchmark results—if you don't control them, you might get one set of data today and a completely different set tomorrow, and then stare at both sets in a daze.

## Where We Are

In this article, we established a basic cognitive framework for lock-free programming: lock-free and wait-free are not the same thing (the former guarantees the system as a whole moves forward, while the latter guarantees every thread moves forward). The CAS loop is the core building block of lock-free algorithms ("optimistic concurrency"—retry on conflict). The lock-free stack is the most classic introductory case, but it already exposes the two core challenges of the ABA problem and memory reclamation. Tagged pointers solve the ABA problem using version numbers, and Hazard Pointers provide more universal memory protection, but both have their own performance costs and implementation complexity. Finally, we discussed when to use lock-free and when not to—this engineering judgment is more important than knowing how to write lock-free code itself.

But the lock-free stack we implemented in this article is just a starting point. In the next article, we will face more practical data structures: SPSC and MPMC queues. Because SPSC queues have only one producer and one consumer, they don't need CAS loops. Their implementation is concise and their performance is extremely high, making them a common choice in embedded and network programming. MPMC queues need to handle competition among multiple producers and multiple consumers, adding another level of complexity. We will use a complete benchmark to compare the performance differences between lock-free and `mutex` versions—let the data speak, not guesses.

## Exercises

### Exercise 1: Implement a Lock-Free Stack and Observe CAS Retries

Using the `LockFreeStack` code provided in this article, complete the following tasks:

1. Implement the complete `push` and `pop` (don't handle memory reclamation for now; just let the program run for a short time during testing).
2. Launch 4 threads to concurrently push a total of 1,000,000 integers, then use 4 threads to concurrently pop.
3. Add a counter in the CAS loop to track the total number of CAS retries. Under high contention, this number will be very large.
4. Compare the performance with `std::mutex` + `std::stack`. Don't rush to conclusions—try different thread counts and operation counts.

### Exercise 2: Reproduce the ABA Problem

The ABA problem is hard to reproduce under normal circumstances because it requires precise scheduling timing. But we can use `std::this_thread::sleep_for` to artificially introduce delays and widen the window:

1. Add a `std::this_thread::sleep_for(100ms)` before the CAS in `pop`.
2. Let Thread 1 start a `pop` (it will sleep before the CAS), and have Thread 2 pop all elements off the stack and push a new node back within those 100ms.
3. Observe whether Thread 1's CAS succeeds after it wakes up and whether the data is correct. If the allocator happens to reuse the address, you've witnessed ABA.

### Exercise 3: Tagged Pointer Refactoring

1. Use the `TaggedPtr` template provided in this article to refactor `LockFreeStack`, making `head_` a `TaggedPtr<Node>` type.
2. Re-run the test from Exercise 2 and confirm that ABA no longer occurs.
3. Think about this: what problems would the tagged pointer approach encounter on a 32-bit platform? If the pointer takes up 32 bits, how do you encode the version number in the remaining space?

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## References

- [Wait-Free Synchronization — Maurice Herlihy (1991)](https://cs.brown.edu/people/mph/Herlihy91/p124-herlihy.pdf)
- [Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects — Maged Michael](https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
- [compare_exchange_weak / compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Atomic Operations: The Performance Cost — Fedor Pikus, CppCon 2024](https://www.youtube.com/watch?v=ZQFzMfHIxng)
- [Non-blocking algorithm — Wikipedia](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
- [Lock-Free Programming — cppreference](https://en.cppreference.com/w/cpp/atomic#Lock-free_property)
