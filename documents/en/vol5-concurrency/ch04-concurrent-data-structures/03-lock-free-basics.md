---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'CAS loops, lock-free vs. wait-free, the ABA problem, and memory reclamation
  challenges: building a solid foundation for lock-free programming.'
difficulty: advanced
order: 3
platform: host
prerequisites:
- 原子操作模式
reading_time_minutes: 28
related:
- SPSC 与 MPMC 队列
tags:
- host
- cpp-modern
- advanced
- atomic
- 无锁
title: Lock-Free Programming Fundamentals
translation:
  source: documents/vol5-concurrency/ch04-concurrent-data-structures/03-lock-free-basics.md
  source_hash: b1a4c983b2f86adc46e35c09edaf55282c9a7391f4904105e92a1f35b60cf663
  translated_at: '2026-06-16T04:05:10.246618+00:00'
  engine: anthropic
  token_count: 4437
---
# Lock-Free Programming Basics

In the previous two articles, we built thread-safe queues and containers using mutexes and condition variables. In ch03, we exhaustively broke down the operation set and six memory orders of `std::atomic`, and in the article on "Atomic Operation Patterns," we implemented SeqLock, spinlocks, and reference counting. Those content answered the question of "how to perform atomic operations," but we haven't touched upon a deeper question yet: **If we completely abandon locks, can we write correct concurrent data structures?**

Honestly, when I first heard the term "lock-free programming," my immediate intuition was, "Isn't this just showing off?" Later, after seeing a few lock-free stack implementations, I realized it wasn't showing off—it represents a completely different mindset from lock-based concurrency. You no longer wrap a critical section with a lock to make threads queue up; instead, you let all threads operate on the data structure simultaneously, using atomic operations to coordinate conflicts—whoever conflicts retries, but the system as a whole always moves forward. The cost of this approach is a skyrocketing complexity in reasoning about correctness, while the benefit is more controllable latency in high-contention scenarios.

The term "lock-free" is actually quite misleading—it doesn't mean using no locks whatsoever, but rather that the system's overall progress cannot be blocked by the delay or crash of any single thread. This distinction is important and subtle. I personally got tangled up in this several times when first entering this field, so in this article, we will start with the precise definition of progress guarantees, thoroughly clarify the difference between lock-free and wait-free, and then dive into the CAS loop, the core building block of lock-free programming. We will implement a classic lock-free stack, and then discuss the two thorniest problems in lock-free programming: the ABA problem and memory reclamation. Finally, we will discuss when to use lock-free techniques and when not to—this judgment is more important than the ability to write lock-free code itself.

## Lock-free vs Wait-free: What Exactly Is Guaranteed

Many people understand "lock-free" as "not using mutex." This understanding isn't exactly wrong, but it's not precise enough—it's actually quite far off. In academia, Herlihy's 1991 paper established the definitional foundation for wait-free and lock-free. Later, in 2003, Herlihy, Luchangco, and Moir introduced the weaker concept of obstruction-free. The C++ standard and industry basically follow this three-tier framework, so we need to clarify the three levels of progress guarantees first.

Let's start with the weakest: **obstruction-free** guarantees that if a thread is executed in isolation at some point in time—meaning all other threads are paused—it can complete its operation in a finite number of steps. Simply put, "if there is no contention, progress is made." This guarantee is too weak and has almost no practical value, so we won't discuss it further here.

**Lock-free** takes a step further: it guarantees that at any moment, **at least one thread** in the system can complete its operation in a finite number of steps. Note the emphasis on "at least one," not "every single one." This means that in a lock-free system, the system as a whole is moving forward, but individual threads might keep retrying due to continuous CAS failures—theoretically, starvation is possible. The spinlock we wrote in the last article is not lock-free: if a thread holds the lock and won't let go (e.g., it gets suspended by the OS), all other threads have to wait idly, and the entire system stalls.

**Wait-free** is the strongest guarantee: **every single thread** is guaranteed to complete its operation in a finite number of steps, regardless of what other threads are doing or how fast they are running. Wait-free implies no starvation and no retry loops; every operation has a deterministic upper bound on steps.

The hierarchy from weak to strong is: blocking -> obstruction-free -> lock-free -> wait-free. With each step up, implementation difficulty increases significantly. In actual engineering, we usually aim for lock-free, because the cost of implementing wait-free is too high, and lock-free is sufficient in most scenarios—at least the system won't completely crash because one thread gets stuck.

A common misconception needs to be clarified upfront: **lock-free does not mean "faster"**. Lock-free solves the problem of progress guarantees, not performance. A lock-free data structure might be slower than a mutex version in low-contention scenarios because the overhead of CAS retries might be higher than simply taking a lock. The advantage of lock-free shows up in high-contention, latency-sensitive scenarios—it won't cause the entire critical section to block because a thread gets suspended by the scheduler. We will expand on this distinction with concrete data later in the "When to Use Lock-Free" section.

## The CAS Loop: The Cornerstone of Lock-Free Programming

Alright, with the concept of progress guarantees clear, let's get our hands dirty. Almost all lock-free algorithms are built on one atomic primitive: Compare-And-Swap (CAS). In C++, this corresponds to the `compare_exchange_weak` and `compare_exchange_strong` member functions of `std::atomic`. We already introduced the signatures and semantics of these two functions in the "Atomic Operations" article in ch03, so we won't repeat the basics here. Instead, we will focus on their usage patterns in lock-free programming.

If you remember the content from ch03, the core semantics of CAS can be summarized in one sentence: **"I think the current value should be X; if it is, swap it to Y; otherwise, tell me what it actually is now."** In code, `compare_exchange` accepts two key parameters—`expected` (the expected value) and `desired` (the new value). If the current value equals `expected`, it changes to `desired` and returns `true`; if not, it writes the current value back into `expected` and returns `false`. The entire operation is atomic, with no modifications from other threads interleaving between the "compare" and the "swap."

We also discussed the difference between weak and strong in ch03, so let's do a quick review. `compare_exchange_weak` allows spurious failure: even if the current value actually equals `expected`, it might return `false`. This is inevitable on certain hardware architectures (like ARM's LL/SC instruction pair). `compare_exchange_strong` guarantees no spurious failure. On x86, weak and strong generate exactly the same machine code (both are `cmpxchg`), but on ARM, the strong version requires an internal retry loop to eliminate spurious failures.

A key rule of thumb—same as in ch03: **use weak in loops, and use strong for one-off checks outside loops**. The reason is straightforward—if you are already in a loop, you will retry after a CAS failure anyway, so an extra spurious failure just means one more loop iteration. If you use weak outside a loop, a single spurious failure will lead you to wrongly believe the value has changed, potentially taking the wrong branch. On ARM, using strong inside a loop results in nested retry loops (your outer loop plus the inner loop of strong), wasting instructions.

Let's look at the simplest CAS loop—a manual implementation of atomic addition. While this example is unnecessary in actual engineering (`fetch_add` suffices), it clearly demonstrates the basic structure of a CAS loop and serves as the foundation for our lock-free stack later:

```cpp
// Atomic addition implemented via CAS loop
int atomic_add_cas(std::atomic<int>& val, int delta) {
    int old_val = val.load(std::memory_order_relaxed);
    int new_val;
    do {
        new_val = old_val + delta;
        // weak is preferred here because we are in a loop
    } while (!val.compare_exchange_weak(old_val, new_val,
                                        std::memory_order_relaxed));
    return new_val;
}
```

What this loop does is: read the current value, calculate the new value, and then try to swap the current value from `old_val` to `new_val`. If another thread modified `val` during this process, CAS fails and tells us the latest value (by writing back to the `old_val` parameter), and we just recalculate using the latest value and try again. This is so-called "optimistic concurrency": assume no conflict, and retry if there is one. You will find that this loop cannot be an infinite loop—after every failure, `old_val` is updated to a newer value, so the system as a whole is moving forward—this is the embodiment of lock-free semantics at the microscopic level.

Of course, for addition, just using `fetch_add` is enough; there's no need to write a CAS loop manually. The power of the CAS loop manifests in more complex operations—like updating linked list pointers or swapping the head node of a data structure. These operations cannot be expressed by simple `fetch_add` or `fetch_sub` and must use CAS. Next, let's write a real lock-free data structure.

## Classic Lock-Free Stack: From CAS Loop to Real Data Structures

Understanding the basic pattern of the CAS loop, we can now challenge a real lock-free data structure. The lock-free stack is the simplest among lock-free data structures and is the starting point for almost all lock-free programming textbooks—Treiber published its design back in 1986. We will first build the overall structure, then gradually break down the implementation of push and pop.

```cpp
template <typename T>
class LockFreeStack {
    struct Node {
        T data;
        Node* next;
        explicit Node(const T& val) : data(val), next(nullptr) {}
    };

    std::atomic<Node*> head;

public:
    LockFreeStack() : head(nullptr) {}
    void push(const T& val);
    bool pop(T& res);
};
```

The structure is very simple: a singly linked list where `head` is an atomic pointer pointing to the top node. All operations happen at the head, requiring synchronization on only this one pointer.

### push: Inserting a Node at the Top

```cpp
template <typename T>
void LockFreeStack<T>::push(const T& val) {
    Node* new_node = new Node(val);
    Node* old_head = head.load(std::memory_order_acquire);

    do {
        new_node->next = old_head;
        // weak is preferred here because we are in a loop
    } while (!head.compare_exchange_weak(old_head, new_node,
                                         std::memory_order_release,
                                         std::memory_order_relaxed));
}
```

The logic of `push` is three steps: create a new node, point the new node's `next` to the current top, and then try to use CAS to swap `head` from `old_head` to `new_node`. If CAS succeeds, the new node becomes the new top. If CAS fails, it means another thread preemptively modified `head`, but `compare_exchange_weak` updates `old_head` to the latest value, so we just reset `new_node->next` and try again.

Note the choice of memory order: when CAS succeeds, `memory_order_release` is used. This ensures that the writes to `new_node->data` and `new_node->next` complete before the CAS succeeds. When other threads read the new value of `head` via `acquire`, they are guaranteed to see these writes. When CAS fails, `memory_order_relaxed` is sufficient—nothing was modified, so no synchronization is needed. The initial `load` also uses `relaxed` because the real synchronization is guaranteed by the memory order of the CAS operation itself.

### pop: Removing a Node from the Top

```cpp
template <typename T>
bool LockFreeStack<T>::pop(T& res) {
    Node* old_head = head.load(std::memory_order_acquire);

    while (old_head) {
        Node* next = old_head->next;
        // Try to point head to the next node
        if (head.compare_exchange_weak(old_head, next,
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
            res = old_head->data;
            // ⚠️ CRITICAL: Cannot delete old_head here!
            // We will discuss this later
            break;
        }
        // CAS failed, old_head was updated to the latest value by CAS, retry
    }

    return old_head != nullptr;
}
```

The logic of `pop` is also intuitive: read the current top, note its `next`, and then try to use CAS to swap `head` from `old_head` to `next`. If successful, `old_head` is removed from the stack, and we extract its data and return.

However—things aren't finished here. There is a huge pitfall in the code, which I marked with a comment. We have obtained `old_head` and know it has been removed from the stack, but **we cannot `delete` it immediately**. The reason is: before we executed CAS, other threads might have also read the same `old_head` and are operating on its `next` pointer. If we release the memory of `old_head` now, those threads are accessing freed memory—use-after-free, a typical undefined behavior. This problem isn't like a data race that can be solved by adding a `mutex`; it is a **logical-level lifetime issue**.

This problem is the most tricky **memory reclamation problem** in lock-free programming. Let's put it aside for now and discuss it together after explaining the ABA problem—ABA and memory reclamation are intertwined, and it's hard to see the full picture by looking at them separately.

## The ABA Problem: The Number One Trap of CAS

Next, we encounter the most notorious bug pattern in lock-free programming—the ABA problem. If you've been asked about lock-free programming in an interview, you've likely been asked about this. It's famous not because it's hard to understand, but because it really happens in practice, and once it does, it's extremely hard to debug—the program won't crash; it will just silently produce wrong results.

### How ABA Happens

Let's use a concrete scenario to demonstrate. Suppose two threads are operating on our lock-free stack, with an initial state of A -> B -> C, where A is the top.

Thread 1 starts executing `pop`: it reads `head`, gets A, and prepares to execute CAS to swap `head` from A to B. But just before CAS, Thread 1 gets suspended by the scheduler—this is where the trouble starts.

Thread 2 starts working at this point: it fully executes two `pop`s, first popping A (stack becomes B -> C), then popping B (stack becomes C). Then Thread 2 `push`es a new value, and the allocator happens to reuse A's memory address, so the new node's address is exactly the same as the previous A. Now the stack becomes A' -> C, but this A' has the exact same address as the previous A.

Thread 1 wakes up and executes CAS: `head.compare_exchange_weak(old_head, next)`. It finds `head` is indeed A (address matches), CAS succeeds, and `head` is set to B.

Here is the problem: B has already been popped and released by Thread 2. Thread 1 has pointed `head` to a node that is already invalid. Any subsequent operation on the stack will access freed memory—the program might crash at any time, or worse, silently produce wrong results, and you won't know where to start looking.

### Why ABA Is So Dangerous

ABA is insidious because CAS only cares about "whether the value equals the expected," not "whether the value has changed in between." In the ABA scenario, the pointer value indeed goes from A to A (via B in between), but CAS cannot distinguish between "always was A" and "A -> B -> A"—to CAS, these two situations are identical. This isn't a design flaw of CAS, but an inherent limitation of it as a "value comparison" primitive.

You might ask: Does this really happen in practice? The answer is yes. In high-contention environments, nodes are frequently allocated and freed, and memory allocators are likely to reuse just-freed addresses—especially allocators like `jemalloc`/`tcmalloc` that are optimized for small objects, which maintain freelists bucketed by size, so memory just released can be allocated again immediately. Combined with multi-threaded scheduling timing, the scenario where "Thread 1 reads and gets suspended, Thread 2 does a full round of operations" is entirely possible.

### Tagged Pointer: Adding a Version Number to Pointers

Okay, the problem is clear, now let's look at the solution. The most common solution is the **tagged pointer**. The idea is straightforward: pack a pointer with an incrementing version number, and increment the version number every time the pointer is modified. This way, even if the pointer value goes from A -> B -> A, the version number goes from 0 -> 1 -> 2, and CAS will correctly fail because the version numbers don't match—the version number only increases, so a loop is impossible.

On 64-bit systems, we can use the upper 16 bits of the pointer to store the version number (since on most architectures, user-space pointers only use the lower 48 bits). Here is a simplified implementation:

```cpp
template <typename T>
class TaggedPtr {
    using IntPtr = uintptr_t;
    static constexpr IntPtr PTR_MASK = 0x0000FFFFFFFFFFFF; // Lower 48 bits for pointer
    static constexpr IntPtr TAG_MASK = 0xFFFF000000000000; // Upper 16 bits for tag
    static constexpr int TAG_SHIFT = 48;

    IntPtr ptr_and_tag;

public:
    TaggedPtr(T* p = nullptr, uint16_t tag = 0)
        : ptr_and_tag(reinterpret_cast<IntPtr>(p) | (static_cast<IntPtr>(tag) << TAG_SHIFT)) {}

    T* get_ptr() const {
        return reinterpret_cast<T*>(ptr_and_tag & PTR_MASK);
    }

    uint16_t get_tag() const {
        return static_cast<uint16_t>((ptr_and_tag & TAG_MASK) >> TAG_SHIFT);
    }

    TaggedPtr next_tag() const {
        return TaggedPtr(get_ptr(), get_tag() + 1);
    }
};
```

Rewriting the lock-free stack's `head` using tagged pointer:

```cpp
std::atomic<TaggedPtr<Node>> head; // Change type
```

The tagged pointer solution has a prerequisite: your architecture's CAS must be able to operate on 64 bits (or 128 bits if you want more version bits). On x86-64, this is no problem; `std::atomic` natively supports 64-bit operations. On some 32-bit embedded platforms, double-word CAS might be unavailable or expensive, requiring other solutions.

### Hazard Pointer: More General Memory Protection

Tagged pointer solves the ABA problem, but you'll notice it doesn't solve the memory reclamation problem we mentioned earlier—we still don't know when it's safe to `delete` a node. Hazard Pointer is a more general solution proposed by Maged Michael in 2004. It solves both ABA and memory reclamation problems simultaneously, and it's not just for stacks, but also for queues, linked lists, and various other lock-free data structures. C++26 has already included Hazard Pointer in the standard (`std::hazard_pointer`).

The core idea of Hazard Pointer is very elegant: each thread holds one or a set of "hazard pointers," used to declare "I am currently accessing this node." When a thread wants to reclaim a node, it cannot `delete` it directly. Instead, it first checks all threads' hazard pointers—if someone is using this node, reclamation is deferred. Only when it is confirmed that no thread's hazard pointer points to this node can it be safely reclaimed.

Simplified pseudocode is as follows:

```cpp
// Each thread has an array of hazard pointers
thread_local std::array<HazardPointer, MAX_HPS> my_hazard_pointers;

void publish_hazard(HazardPointer& hp, void* ptr) {
    hp.store(ptr, std::memory_order_release);
}

void reclaim_later(Node* node) {
    // Add to the thread's local reclaim list
    // Periodically scan other threads' hazard pointers
    // If no one is holding the node, delete it
}
```

In the lock-free stack's `pop`, the usage is roughly this: the thread first publishes a hazard pointer pointing to `old_head`, then executes CAS. If CAS succeeds, the thread clears its hazard pointer and puts `old_head` into a "to-be-reclaimed list." Periodically (e.g., when the to-be-reclaimed list accumulates to a certain length), the thread scans all hazard pointers and reclaims nodes that no one is using.

The advantage of Hazard Pointer is its good generality, suitable for various lock-free data structures. The disadvantage is performance overhead: every `pop` needs to publish and clear hazard pointers, and scanning the to-be-reclaimed list also requires traversing all threads' slots. In high-contention scenarios, this overhead can be significant.

## Memory Reclamation: The Hardest Problem in Lock-Free Programming

We have bumped into this problem repeatedly before, always "putting it aside." Now is the time to face it head-on. If you thought the ABA problem was already tricky enough, memory reclamation will give you an even bigger headache—it is widely recognized as the hardest problem in lock-free programming and is one of the biggest obstacles preventing the widespread use of lock-free data structures in actual projects.

In lock-based data structures, memory reclamation is simple: take the lock, operate, free memory, unlock. Because the lock guarantees that only one thread operates on the data structure at a time, there is no problem of "one thread is still using a node while another thread frees it."

But in lock-free data structures, multiple threads can read the same node simultaneously. Thread A just finished reading `old_head` and is preparing to execute CAS. At this moment, Thread B might have already popped `old_head` and `delete`d it. Thread A's CAS hasn't executed yet, but the `old_head` in its hand is already a dangling pointer. This problem isn't like a data race that can be eliminated by `std::atomic`—it is a **logical-level lifetime issue**.

There are currently several mainstream solutions in the industry. Besides the Hazard Pointer mentioned earlier, there is **Epoch-based Reclamation** and **reference counting**.

The idea of Epoch-based Reclamation is to divide time into several "epochs," with a global current epoch number maintained. Each thread records the epoch it is in when entering the critical section. When reclaiming, nodes from an epoch can only be safely freed after all threads have left that epoch. This solution has less scanning overhead than Hazard Pointer, but is more complex to implement, and in some extreme cases, reclamation might be delayed for a long time—if a thread is stuck in an old epoch and doesn't come out, all nodes from that epoch pile up and cannot be freed. Facebook's Folly library has a production-grade implementation (the `AtomicUnorderedMap` mechanism in Folly uses similar ideas).

Reference counting sounds the most intuitive: add an atomic reference count to each node, decrement when popping, and free when zero. But the problem is that incrementing and decrementing the reference count itself requires atomic operations, and there is a window between "loading the pointer" and "incrementing the reference count"—within this window, the node might be freed by another thread. To solve this "load-increment" atomicity problem, reference counting solutions often degenerate into some form of Hazard Pointer or require double-word CAS, and the implementation complexity doesn't really decrease. `std::atomic_shared_ptr` in C++20 can be used, but its performance overhead (usually implemented with an internal spinlock) makes it unsuitable for true lock-free scenarios.

## When to Use Lock-Free—And When Not To

Having discussed so many problems and solutions, you might ask: Since lock-free programming is so complex, why use it? The answer is: In specific scenarios, lock-free can indeed bring performance benefits that mutexes cannot. But this "specific scenario" is much narrower than you think. I have seen many cases where a lot of effort was spent converting a mutex-protected data structure to a lock-free one, only to find out from benchmarks that it became slower—then staring at the data in a daze.

### Scenarios Suitable for Lock-Free

**High contention, low latency** is the most typical scenario. When a large number of threads frequently compete for the same data structure, mutexes cause frequent context switches (each switch is a round trip to kernel mode, costing microseconds). Lock-free algorithms turn contention from "queuing for a lock" to "CAS retries." Although retries have overhead, they happen in user space and don't involve kernel scheduling, making latency more controllable and tail latency smaller. High-frequency trading systems, real-time signal processing, main loops of network game servers—in these scenarios, a difference of a few microseconds in latency might be the dividing line between acceptable and unacceptable.

**Single Producer-Single Consumer (SPSC) queues** are another scenario particularly well-suited for lock-free. Because there is only one producer and one consumer, no CAS loop is needed; synchronization can be achieved correctly with just atomic variables with `relaxed` semantics. Simple implementation, extremely high performance, almost no contention—in this scenario, lock-free is almost the default choice. We will dedicate the next article to the design of SPSC queues.

**Communication between interrupt context and the main loop** is also common in embedded systems. Interrupt handlers cannot call potentially blocking functions (including `mutex::lock`), making lock-free queues almost the only choice.

### Scenarios Not Suitable for Lock-Free

Don't rush to replace all mutexes in your project—in these scenarios, lock-free is often a losing proposition.

**Low contention scenarios** often see lock-free being slower than mutex. The reason is simple: the overhead of locking/unlocking a mutex without contention is actually very low (one atomic instruction plus a branch prediction), while a CAS loop requires at least one atomic operation and one conditional check even on the success path. If your data structure encounters contention only once every 1,000 accesses, the total overhead of mutex is likely lower than lock-free.

**Complex critical sections** are not suitable for lock-free. If your operation involves coordinated modification of multiple variables (e.g., "delete an element from a map while updating the size counter"), expressing such composite operations with CAS is extremely difficult, code is hard to implement correctly, and even harder to maintain. Mutexes natively support arbitrarily complex critical sections, and this advantage is irreplaceable in the face of complex logic.

**Team maintenance cost** is also a consideration that cannot be ignored. Lock-free code is far harder to read, review, and debug than mutex versions. A bug in a CAS loop might only trigger once in a million runs, and ThreadSanitizer's false positive rate for lock-free code isn't low either. If your team doesn't have enough lock-free programming experience, writing correct code with mutexes is more valuable than writing fast but unreliable code with CAS—correct code is always better than fast, broken code.

### Benchmark: Don't Guess, Measure

Any assertion about "lock-free is faster" or "mutex is faster" without concrete benchmark data is empty talk. I have seen too many cases where "theoretically lock-free is faster" but in reality is slower due to cache coherence overhead, CAS retry storms, false sharing, etc.—the bottlenecks of concurrent performance are often where you least expect them.

A basic benchmark framework should include: throughput tests under different thread counts (1, 2, 4, 8, 16), latency distribution (p50, p99, p999) under different operation ratios (pure push, pure pop, mixed), and result comparisons on different hardware. When we implement SPSC and MPMC queues in the next article, we will do a complete benchmark comparison.

Here is a simple but effective benchmark template:

```cpp
template <typename Func>
void run_benchmark(const std::string& name, Func func) {
    constexpr int ITER = 1000000;
    auto start = std::chrono::high_resolution_clock::now();
    func();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << name << ": " << duration.count() << " us\n";
}
```

When running benchmarks, it is recommended to disable CPU frequency scaling (`cpupower frequency-set --governor performance`), bind CPU cores (`taskset` or `pthread_setaffinity_np`), and take the median of multiple runs. These means of controlling variables have a large impact on concurrent benchmark results—without them, you might run one set of data today and a completely different set tomorrow, then stare at the two groups of data in a daze.

## Where We Are

In this article, we established the basic cognitive framework for lock-free programming: lock-free and wait-free are not the same thing (the former guarantees the system as a whole moves forward, the latter guarantees every thread moves forward). The CAS loop is the core building block of lock-free algorithms ("optimistic concurrency"—retry on conflict). The lock-free stack is the most classic introductory case but has already exposed the two core problems of ABA and memory reclamation. Tagged pointer solves the ABA problem with version numbers, and Hazard Pointer provides more general memory protection, but both have their own performance costs and implementation complexity. Finally, we discussed when to use lock-free and when not to—this engineering judgment is more important than the ability to write lock-free code itself.

But the lock-free stack implemented in this article is just a starting point. In the next article, we will face more practical data structures: SPSC and MPMC queues. Because the SPSC queue has only one producer and one consumer, it doesn't need a CAS loop, has a concise implementation, and extremely high performance, making it a common choice in embedded and network programming. MPMC queues need to handle competition from multiple producers and consumers, adding another layer of complexity. We will use a complete benchmark to compare the performance differences between lock-free and mutex versions—let the data do the talking, not guesses.

## Exercises

### Exercise 1: Implement a Lock-Free Stack and Observe CAS Retries

Using the `LockFreeStack` code provided in this article, complete the following tasks:

1. Implement complete `push` and `pop` (don't handle memory reclamation for now; just let the run for a short time during testing).
2. Start 4 threads to concurrently push 1,000,000 integers, then use 4 threads to concurrently pop.
3. Add a counter in the CAS loop to count the total number of CAS retries. This number will be large under high contention.
4. Compare the performance of `std::mutex` + `std::stack`. Don't rush to conclusions—try different thread counts and operation counts.

### Exercise 2: Reproduce the ABA Problem

The ABA problem is hard to reproduce under normal circumstances because it requires precise scheduling timing. But we can use `std::this_thread::sleep_for` to artificially create a delay to enlarge the window:

1. Add a `std::this_thread::sleep_for(std::chrono::milliseconds(100))` before the CAS in `pop`.
2. Let Thread 1 start `pop` (it will sleep before CAS), and Thread 2 pops all elements on the stack and then pushes a new node within this 100ms.
3. Observe whether Thread 1's CAS succeeds after waking up and whether the data is correct. If the allocator happens to reuse the address, you have seen ABA.

### Exercise 3: Tagged Pointer Refactoring

1. Use the `TaggedPtr` template provided in this article to refactor `LockFreeStack`, making `head` a `std::atomic<TaggedPtr<Node>>` type.
2. Re-run the test from Exercise 2 to confirm ABA no longer happens.
3. Think: What problems will the tagged pointer solution encounter on 32-bit platforms? If the pointer occupies 32 bits, how do you encode the version number in the remaining space?

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `ch04/lock_free_stack`.

## References

- [Wait-Free Synchronization — Maurice Herlihy (1991)](https://cs.brown.edu/people/mph/Herlihy91/p124-herlihy.pdf)
- [Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects — Maged Michael](https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
- [compare_exchange_weak / compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Atomic Operations: The Performance Cost — Fedor Pikus, CppCon 2024](https://www.youtube.com/watch?v=ZQFzMfHIxng)
- [Non-blocking algorithm — Wikipedia](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
- [Lock-Free Programming — cppreference](https://en.cppreference.com/w/cpp/atomic#Lock-free_property)
