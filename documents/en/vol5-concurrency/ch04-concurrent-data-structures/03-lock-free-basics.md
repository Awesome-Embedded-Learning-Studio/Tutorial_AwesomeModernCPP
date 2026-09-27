---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 'CAS loops, lock-free vs. wait-free, the ABA problem, and memory reclamation
  challenges: building solid judgment for lock-free programming.'
difficulty: advanced
order: 3
platform: host
prerequisites:
- Atomic Operation Patterns
reading_time_minutes: 28
related:
- SPSC and MPMC Queues
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
  translated_at: '2026-09-26T08:12:42+00:00'
  engine: anthropic
  token_count: 5865
---
# Lock-Free Programming Fundamentals

In the previous two articles we built thread-safe queues and containers with mutex + condition_variable, in ch03 we fully took apart the operation set of `std::atomic` and all six memory orders, and in the "Atomic Operation Patterns" article we wrote a SeqLock, a spinlock, and reference counting. All of that answered the question of "how to do atomic operations," but it never touched a deeper one: **if we use no locks at all, can we still write correct concurrent data structures?**

Honestly, the first time I heard the term "lock-free programming," my gut reaction was "isn't this just showing off?" Only after reading a few lock-free stack implementations did I realize it isn't showing off at all—it is an entirely different way of thinking from lock-based concurrency. You no longer wrap a critical section in a lock and make threads queue up; you let all threads operate on the data structure simultaneously and coordinate conflicts with atomic operations—whoever hits a conflict retries, but the system as a whole keeps moving forward. The cost of this mindset is a dramatic jump in the complexity of reasoning about correctness; the payoff is more controllable latency under high contention.

The term "lock-free" is honestly a bit misleading—it doesn't mean that no locks are used at all; it means the system's overall progress can never be blocked by the delay or crash of any single thread. This distinction matters, and it is subtle. I got twisted around by it more than a few times when I first entered this area, so in this article we will start from the precise definitions of progress guarantees, thoroughly sort out the difference between lock-free and wait-free, then move into the CAS loop—the core building block of lock-free programming—implement a classic lock-free stack, and finally take on the two nastiest problems in lock-free programming: the ABA problem and memory reclamation. At the end we will talk about when to use lock-free and when not to—this judgment matters more than knowing how to write lock-free code in the first place.

## Lock-free vs. Wait-free: What Exactly Is Guaranteed

Many people read "lock-free" as "doesn't use a mutex." That reading isn't exactly wrong, but it isn't precise either—it is actually pretty far off. In academia, Herlihy's 1991 paper laid the definitional foundation for wait-free and lock-free, and in 2003 Herlihy, Luchangco, and Moir introduced obstruction-free, a still weaker notion. The C++ standard and industry practice have essentially adopted this three-level framework, so we first need to get the three levels of progress guarantee straight.

Start with the weakest: **obstruction-free** guarantees that if a thread runs alone at some point in time—that is, with every other thread paused—it can complete its operation in a finite number of steps. Put bluntly: "if there is no contention, you can make progress." That guarantee is too weak to be of much practical use, and we will not expand on it later.

**Lock-free** steps it up: it guarantees that at any moment, **at least one thread in the system** can complete its operation in a finite number of steps. Note the wording—"at least one," not "every one." That is, in a lock-free system the system as a whole keeps making progress, but individual threads may keep retrying because their CAS attempts keep failing—starvation remains theoretically possible. The spinlock we wrote in the previous article is not lock-free: if one thread grabs the lock and never lets go (say it gets suspended by the operating system), every other thread is stuck waiting and the system as a whole stalls.

**Wait-free** is the strongest guarantee: **every thread** is guaranteed to complete its own operation in a finite number of steps, regardless of what the other threads are doing or how fast they run. Wait-free means no starvation and no retry loops—every operation has a deterministic upper bound on its step count.

From weakest to strongest: blocking -> obstruction-free -> lock-free -> wait-free. Each level up makes implementation dramatically harder. What we chase in real engineering is usually lock-free, because wait-free is too expensive to implement and lock-free is already good enough in most scenarios—at least the system won't collapse as a whole just because one thread got stuck.

One common misconception to clear up in advance: **lock-free does not mean "faster."** Lock-free solves the progress-guarantee problem, not the performance problem. A lock-free data structure can be slower than its mutex version under low contention, because the cost of CAS retries can exceed the cost of simply taking the lock. Lock-free earns its keep in high-contention, latency-sensitive scenarios—it won't let an entire critical section jam up just because the scheduler paused one thread. We will return to this distinction with concrete numbers in the "when to use lock-free" section.

## The CAS Loop: The Cornerstone of Lock-Free Programming

Alright—the concept of progress guarantees is settled, so let's get our hands dirty. Nearly every lock-free algorithm stands on one atomic primitive: Compare-And-Swap (CAS). In C++, those are the `compare_exchange_weak` and `compare_exchange_strong` member functions of `std::atomic`. We already introduced the signatures and semantics of these two functions in the "Atomic Operations" article of ch03, so instead of repeating the basics here, we will focus on the usage patterns they follow in lock-free programming.

If you remember ch03, the core semantics of CAS boil down to one sentence: **"I believe the current value should be X; if it is, swap it for Y; otherwise, tell me what it actually is."** In code terms, `compare_exchange_weak/strong` takes two key parameters—`expected` (the anticipated value) and `desired` (the new value). If the current value equals `expected`, it is changed to `desired` and the function returns `true`; if not, the current value is written back into `expected` and the function returns `false`. The whole operation is atomic—no other thread's modification can slip in between the "compare" and the "swap."

We also discussed the difference between weak and strong in ch03; here is a quick recap. `compare_exchange_weak` may fail spuriously: even when the current value really does equal `expected`, it can still return `false`. On some hardware architectures (ARM's LL/SC instruction pair, for example) this is unavoidable. `compare_exchange_strong` guarantees no spurious failures. On x86, weak and strong generate exactly the same machine code (both are `lock cmpxchg`), but on ARM the strong version needs an internal retry loop to eliminate spurious failures.

One key rule of thumb—the same one ch03 gave: **use weak inside loops, and strong for one-shot checks outside loops**. The reason is direct: if you are already in a loop, you will retry after a CAS failure anyway, so one extra spurious failure just costs one more trip around the loop. But if you use weak outside a loop, a single spurious failure convinces you the value changed when it didn't, and you may take the wrong branch. On ARM, using strong inside a loop produces nested retry loops (your loop on the outside, strong's loop on the inside) and wastes instructions for nothing.

Let's start with the simplest CAS loop there is—a hand-rolled atomic add. In real projects this one is unnecessary (`fetch_add` does the job), but it displays the basic structure of a CAS loop with total clarity, and it is the foundation for the lock-free stack we write next:

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
        // On CAS failure, old is automatically updated to the current value
        // Recompute old + delta, then retry
    }
}
```

Here is what the loop does: read the current value, compute the new value, then attempt to swap the current value from `old` to `old + delta`. If another thread modified `value` in the meantime, the CAS fails and tells us the latest value (by writing it back into the `old` parameter); we simply recompute from the latest value and try again. This is so-called "optimistic concurrency": assume no conflict, and redo when there is one. Notice that this loop cannot spin forever—after each failure `old` holds a fresher value, so the system as a whole moves forward. That is lock-free semantics playing out at the microscopic level.

Of course, for addition you would just call `fetch_add`—no need to hand-write a CAS loop. The power of the CAS loop shows up in more complex operations, such as updating a linked-list pointer or swapping a data structure's head node. Those operations cannot be expressed with a plain `fetch_add` or `exchange`; they require CAS. So next, let's write a real lock-free data structure.

## The Classic Lock-Free Stack: From CAS Loop to Real Data Structure

With the basic CAS-loop pattern in hand, we can take on a real lock-free data structure. The lock-free stack is the simplest of the family and the starting point of nearly every lock-free programming textbook—Treiber published its design back in 1986. Let's put the overall structure up first, then break down push and pop step by step.

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

The structure is dead simple: a singly linked list, with `head_` as an atomic pointer to the top node. All operations happen at the head, so this one pointer is the only thing that needs synchronizing.

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

push works in three steps: create the new node, point the new node's `next` at the current top, then attempt with CAS to swap `head_` from `old_head` to `new_node`. If the CAS succeeds, the new node has become the new top. If it fails, some other thread beat us to modifying `head_`—but `compare_exchange_weak` has already refreshed `old_head` to the latest value, so we only need to reset `new_node->next` and try again.

Pay attention to the choice of memory orders: on CAS success we use `memory_order_release`, which guarantees that the writes to the new node's `data` and `next` complete before the CAS succeeds, so any thread that reads the new value of `head_` with `acquire` is guaranteed to see those writes. On CAS failure, `relaxed` is enough—nothing was changed on failure, so no synchronization is required. `head_.load()` is also `relaxed`, because the real synchronization is guaranteed by the memory order of the CAS operation itself.

### pop: Taking a Node Off the Top

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
            // CAS succeeded: old_head has been unlinked from the stack
            T value = std::move(old_head->data);
            // ⚠️ A serious problem lurks here: when do we delete old_head?
            return value;
        }
        // CAS failed; old_head was updated to the latest value, retry
    }

    return std::nullopt;  // stack is empty
}
```

pop is just as intuitive: read the current top, note its `next`, then attempt with CAS to swap `head_` from `old_head` to `next_node`. On success, `old_head` has been unlinked from the stack, and we take out its data and return it.

But—that is not the end of the story; there is a giant pitfall in this code, the one I flagged with a comment. We hold `old_head`, and we know it has been unlinked from the stack, yet we **cannot `delete` it immediately**. The reason: before our CAS executed, other threads may have read the very same `old_head` and may be operating on its `next` pointer right now. If we released `old_head`'s memory at this point, those threads would be touching freed memory—use-after-free, textbook undefined behavior. Unlike a data race, this problem cannot be solved by adding `std::atomic`; it is a **lifetime problem at the logic level**.

This is the stickiest problem in lock-free programming: **memory reclamation**. We will set it aside for now and discuss it together after the ABA problem—ABA and memory reclamation are entangled with each other, and looking at them separately makes the full picture hard to see.

## The ABA Problem: CAS's Number-One Trap

The next character we meet is the most notorious bug pattern in lock-free programming—the ABA problem. If you have ever been asked about lock-free programming in an interview, you have probably been asked about this one. It is famous not because it is hard to understand, but because it genuinely happens in practice, and once it happens it is brutally hard to debug—the program does not crash; it just quietly produces wrong results.

### How ABA Happens

Let's demonstrate with a concrete scenario. Suppose two threads are operating on our lock-free stack, whose initial state is A -> B -> C, with A on top.

Thread 1 starts executing `pop`: it reads `head_ = A`, reads `A->next = B`, and prepares to CAS `head_` from A to B. But right before the CAS, thread 1 is suspended by the scheduler—that is where the trouble begins.

Thread 2 now gets to work: it executes two complete `pop`s, first popping A (the stack becomes B -> C), then popping B (the stack becomes C). Then thread 2 `push`es a new value, and the allocator happens to reuse A's memory address, so the new node's address is identical to the old A's. The stack is now A' -> C, and this A' has exactly the same address as the previous A.

Thread 1 wakes up and executes its CAS: `head_.compare_exchange(A, B)`. It finds that `head_` is indeed A (same address), the CAS succeeds, and `head_` is set to B.

Here is the problem: B was already popped and freed by thread 2. Thread 1 has just pointed `head_` at a dead node. Every subsequent operation on the stack touches freed memory—the program may crash at any moment, or, worse, silently produce wrong results, and you will have no idea where to even start looking.

### Why ABA Is So Dangerous

ABA is insidious because CAS only cares whether "the value equals what I expected," not whether "the value changed in the meantime." In the ABA scenario, the pointer's value really did go from A back to A (having passed through B along the way), and CAS cannot tell "it was A the whole time" apart from "A -> B -> A"—to CAS, the two are identical. This is not a design flaw of CAS; it is an inherent limitation of CAS as a value-comparison primitive.

You may ask: does this really happen in practice? The answer is yes. Under high contention, nodes are allocated and freed constantly, and the memory allocator is quite likely to reuse a just-freed address—especially `malloc`/`new`-style allocators optimized for small objects, which maintain size-bucketed free lists and can hand freshly freed memory right back out. Add multithreaded scheduling timings on top, and a scenario like "thread 1 gets suspended right after its read while thread 2 runs a full round of operations" is entirely possible.

### Tagged Pointers: Adding a Version Number to the Pointer

Alright, the problem is clear—now for solutions. The most common one is the **tagged pointer**. The idea is straightforward: pack the pointer together with an ever-increasing version number, and bump the version every time the pointer is modified. That way, even if the pointer's value goes A -> B -> A, the version goes 0 -> 1 -> 2, and the CAS fails correctly on a version mismatch—the version only ever increases, so a wrap-around is impossible.

On 64-bit systems, we can use the top 16 bits of the pointer to store the version number (because on most architectures user-space pointers only occupy the low 48 bits). Here is a simplified implementation:

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

    // Every successful CAS comes with tag + 1
    // Even if a pointer address gets reused, the tag never repeats, so ABA cannot happen
}
```

The tagged-pointer approach comes with one premise: on your architecture, CAS must be able to operate on 64 bits (or 128 bits, if you want more version-number bits). On x86-64 this is a non-issue—`lock cmpxchg` natively supports 64-bit operations. On some 32-bit embedded platforms, double-word CAS may be unavailable or very expensive, and other schemes need to be considered.

### Hazard Pointers: More General Memory Protection

Tagged pointers solve the ABA problem, but notice that they do not solve the memory reclamation problem we raised earlier—we still do not know when it is safe to `delete` a node. Hazard Pointers are a more general scheme proposed by Maged Michael in 2004; they solve both ABA and memory reclamation at once, and they apply not just to stacks but also to queues, linked lists, and all sorts of other lock-free data structures. C++26 has already brought Hazard Pointers into the standard (`std::hazard_pointer`).

The core idea of Hazard Pointers is elegant: each thread holds one or a set of "hazard pointers" that declare "I am currently accessing this node." When a thread wants to free a node, it cannot `delete` directly—it first checks every thread's hazard pointers; if someone is using the node, the reclamation is deferred. Only after confirming that no thread's hazard pointer points at the node can it be freed safely.

Simplified pseudocode:

```cpp
// Global hazard pointer table, one slot per thread
constexpr int kMaxThreads = 64;
std::atomic<Node*> g_hazard_pointers[kMaxThreads];

// Before accessing a node, a thread first "publishes" its hazard pointer
void publish_hazard(int slot, Node* node)
{
    g_hazard_pointers[slot].store(node, std::memory_order_release);
}

// Before freeing a node, check whether any thread is using it
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

In the lock-free stack's `pop`, the usage looks roughly like this: the thread first publishes a hazard pointer pointing at `old_head`, then executes the CAS. If the CAS succeeds, the thread clears its hazard pointer and puts `old_head` onto a "retirement list". Periodically (say, when the retirement list has grown past a certain length), the thread scans all hazard pointers and actually frees the nodes nobody is using.

The strength of Hazard Pointers is their generality—they fit all kinds of lock-free data structures. The weakness is their performance overhead: every `pop` must publish and clear a hazard pointer, and scanning the retirement list means walking every thread's slot. Under high contention, this overhead can be significant.

## Memory Reclamation: The Hardest Problem in Lock-Free Programming

We have run into this problem again and again, and each time we said "let's set it aside for now." Now it is time to face it head-on. If you found the ABA problem troublesome enough, memory reclamation will give you an even bigger headache—it is widely recognized as the hardest problem in lock-free programming, and one of the biggest obstacles preventing lock-free data structures from seeing wide use in real projects.

In lock-based data structures, memory reclamation is simple: take the lock, operate, free the memory, unlock. Because the lock guarantees that only one thread operates on the data structure at a time, "one thread still using a node while another thread frees it" simply cannot arise.

In lock-free data structures, however, multiple threads can read the same node simultaneously. Thread A has just finished reading `old_head->next` and is about to execute its CAS, at which moment thread B may already have popped `old_head` and `delete`d it. Thread A's CAS has not even executed, yet the `old_head` in its hand is already a dangling pointer. This problem cannot be eliminated with `std::atomic` the way a data race can—it is a **lifetime problem at the logic level**.

Industry practice currently has a few mainstream schemes. Besides the Hazard Pointers mentioned earlier, there are **Epoch-based Reclamation** and **reference counting**.

Epoch-based Reclamation divides time into a series of "epochs" and maintains a global current epoch number. Each thread records its epoch upon entering a critical section. At reclamation time, nodes belonging to an epoch can be freed safely only after every thread has left that epoch. This scheme costs less scanning than Hazard Pointers, but it is more complex to implement, and in some extreme cases reclamation can be delayed for a long time—if one thread gets stuck in an old epoch and never leaves, all nodes from older epochs pile up, unable to be freed. Facebook's Folly library has a production-grade implementation (the `WeakRef` mechanism in `folly/concurrency/UnboundedQueue.h` uses a similar idea).

Reference counting sounds the most intuitive: attach an atomic reference count to each node, decrement it on `pop`, and free the node when it reaches zero. The problem is that the increments and decrements themselves are atomic operations too, and there is a window between "loading the pointer" and "incrementing the reference count"—a window in which the node may be freed by another thread. To solve the atomicity of this "load-then-increment" step, reference-counting schemes often degenerate into some form of Hazard Pointers or require double-word CAS, so the implementation complexity never really drops. `std::atomic<std::shared_ptr>` is usable in C++20, but its performance overhead (usually implemented with an internal spinlock) makes it a poor fit for genuinely lock-free scenarios.

## When to Use Lock-Free—and When Not To

After all these problems and solutions, you may ask: if lock-free programming is this complicated, why use it at all? The answer: in specific scenarios, lock-free truly delivers performance advantages a mutex cannot. But those "specific scenarios" are much narrower than you imagine. I have seen quite a few cases where a great deal of effort went into converting a mutex-protected data structure into a lock-free one, only for the benchmark to come out slower—and then everyone stares blankly at the numbers.

### Scenarios That Suit Lock-Free

**High contention with low latency requirements** is the most typical scenario. When large numbers of threads compete frequently for the same data structure, a mutex causes frequent context switches (each switch is a round trip into the kernel, costing on the order of microseconds). A lock-free algorithm turns the contention from "queueing for the lock" into "CAS retries"; retries have overhead too, but they happen in user space and involve no kernel scheduling, so latency is more controllable and tail latency is smaller. High-frequency trading systems, real-time signal processing, the main loop of an online game server—in these scenarios a difference of a few microseconds can be the line between acceptable and unacceptable.

**Single-producer, single-consumer (SPSC) queues** are another scenario especially suited to lock-free. Because there is exactly one producer and one consumer, no CAS loop is needed—atomic variables with `acquire/release` semantics are enough for correct synchronization. Simple to implement, extremely high performance, almost no contention: in this scenario lock-free is practically the default choice. We will expand on SPSC queue design in the next article.

**Communication between an interrupt context and the main loop** is also common in embedded systems. Interrupt handlers must not call functions that might block (including `mutex::lock`), so a lock-free queue is very nearly the only choice.

### Scenarios That Do Not Suit Lock-Free

Don't rush to replace every mutex in your project—lock-free is usually a losing deal in these scenarios.

**Under low contention**, lock-free is often slower than a mutex. The reason is simple: a mutex's lock/unlock cost in the absence of contention is actually very low (one atomic instruction plus a branch prediction), whereas a CAS loop needs at least one atomic operation and one conditional check even on its success path. If your data structure runs into contention only once every 1000 accesses on average, the mutex's total cost is very likely lower than lock-free's.

**Complex critical sections** do not suit lock-free. If your operation involves coordinated modification of multiple variables (say, "remove an element from the map and update the size counter at the same time"), expressing such a compound operation with CAS is extremely difficult; the code is hard to get right, and harder still to maintain. A mutex natively supports critical sections of arbitrary complexity—an advantage that is irreplaceable when the logic gets complicated.

**Team maintenance cost** is also a consideration that cannot be ignored. Lock-free code is far harder to read, review, and debug than the mutex version. A bug in a CAS loop may trigger only once in a million runs, and ThreadSanitizer's false-positive rate on lock-free code is not low either. If your team does not have enough lock-free experience, writing correct code with a mutex is more valuable than writing fast-but-unreliable code with CAS—correct code always beats fast wrong code.

### Benchmark: Don't Guess—Measure

Any claim that "lock-free is faster" or "mutex is faster" is empty talk without concrete benchmark data. I have seen far too many cases that were "theoretically faster with lock-free" but actually slower because of cache-coherence overhead, CAS retry storms, false sharing, and the like—the bottleneck of concurrent performance often sits where you least expect it.

A basic benchmark framework should include: throughput tests at different thread counts (1, 2, 4, 8, 16), latency distributions (p50, p99, p999) under different operation mixes (pure push, pure pop, mixed), and a comparison of results across different hardware. When we implement the SPSC and MPMC queues in the next article, we will do a complete benchmark comparison.

Here is a simple but effective benchmark template:

```cpp
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>

/// Measure the total time of N pushes + N pops
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
                    // Queue is full, retry
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

When running benchmarks, it is advisable to disable CPU frequency scaling (`cpupower frequency-set -g performance`), pin CPU cores (`taskset` or `pthread_setaffinity_np`), and take the median over multiple runs. These variable-control measures heavily influence the results of a concurrency benchmark—without them you may get one number today and a completely different one tomorrow, and then end up staring blankly at the two sets of numbers.

## Where We Are

In this article we built the basic cognitive framework of lock-free programming: lock-free and wait-free are not the same thing (the former guarantees the system as a whole moves forward; the latter guarantees every thread moves forward); the CAS loop is the core building block of lock-free algorithms ("optimistic concurrency"—on conflict, redo); the lock-free stack is the classic introductory case, yet it already exposed the two core hard problems, ABA and memory reclamation. Tagged pointers solve ABA with version numbers, and Hazard Pointers provide more general memory protection, but both carry their own performance costs and implementation complexity. Finally, we discussed when to use lock-free and when not to—this engineering judgment matters more than the ability to write lock-free code itself.

But the lock-free stack we implemented here is only a starting point. In the next article we face more practical data structures: SPSC and MPMC queues. An SPSC queue, with exactly one producer and one consumer, needs no CAS loop; it is concise to implement and extremely fast, a common choice in embedded and network programming. An MPMC queue must handle multi-producer, multi-consumer contention, and the complexity climbs yet another level. We will use a complete benchmark to compare the lock-free and mutex versions—let the data talk, not the guessing.

## Exercises

### Exercise 1: Implement the Lock-Free Stack and Observe CAS Retries

Using the `LockFreeStack` code from this article, complete the following tasks:

1. Implement complete `push` and `pop` (leave memory reclamation unhandled for now—just let the test run for a short time).
2. Start 4 threads concurrently pushing a total of 1000000 integers, then use 4 threads to pop concurrently.
3. Add a counter in the CAS loop to tally the total number of CAS retries. Under high contention this number will be large.
4. Compare the performance against `std::mutex` + `std::stack`. Don't jump to conclusions—try different thread counts and operation counts first.

### Exercise 2: Reproduce the ABA Problem

The ABA problem is hard to reproduce under normal conditions, because it requires precise scheduling timing. But we can artificially widen the window with `std::this_thread::sleep_for`:

1. Insert a `sleep_for(std::chrono::milliseconds(100))` before the CAS in `pop`.
2. Let thread 1 start a `pop` (it will sleep before the CAS), while thread 2 spends those 100 ms popping every element off the stack and then pushing one new node back.
3. Observe whether thread 1's CAS succeeds after it wakes up, and whether the data is correct. If the allocator happens to have reused the address, you have just witnessed ABA.

### Exercise 3: Retrofitting with Tagged Pointers

1. Rewrite `LockFreeStack` with the `TaggedPointer` template from this article, making `head_` a `TaggedPointer<Node>`.
2. Rerun the test from Exercise 2 and confirm that ABA no longer happens.
3. Think it over: what problem does the tagged-pointer scheme run into on 32-bit platforms? If a pointer occupies 32 bits, how do you encode the version number in the remaining space?

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); see `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Reference Resources

- [Wait-Free Synchronization — Maurice Herlihy (1991)](https://cs.brown.edu/people/mph/Herlihy91/p124-herlihy.pdf)
- [Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects — Maged Michael](https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
- [compare_exchange_weak / compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Atomic Operations: The Performance Cost — Fedor Pikus, CppCon 2024](https://www.youtube.com/watch?v=ZQFzMfHIxng)
- [Non-blocking algorithm — Wikipedia](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
- [Lock-Free Programming — cppreference](https://en.cppreference.com/w/cpp/atomic#Lock-free_property)
