---
title: "Lock-Free Introduction: CAS Loops, ABA, Memory Reclamation"
description: "The progress-guarantee difference between lock-free and wait-free, CAS loops and the implementation of a lock-free stack, the causes of the ABA problem with tagged pointer and Hazard Pointer as the two countermeasures, plus when to use — and when not to use — lock-free."
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - advanced
  - atomic
  - 无锁
difficulty: advanced
platform: host
cpp_standard: [11, 20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/cookbook/b-topics/02-lock-free-intro.md
  source_hash: 53d35b6a4b6ded095c7070586a9bbec419562a96cc3d305b49b896763c48e33a
  translated_at: '2026-11-22T13:29:27+00:00'
  engine: glm-5.3
  token_count: 18901
---
# Lock-Free Introduction: CAS Loops, ABA, Memory Reclamation

This piece is the readings section's introduction to lock-free. Unit 5 of Volume 5 covers `std::atomic` to the required depth: [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve) gives the operation surface and CAS retry semantics, while [How One Thread Observes Another Thread's Modifications](../../chapter05-atomic-memory-model/03-happens-before) together with [The Four Levels of Memory Ordering: From Sequential Consistency to relaxed](../../chapter05-atomic-memory-model/04-choosing-memory-orders) spreads out the six memory orders. But writing a lock-free container is not part of the entrance requirements, and complex lock-free data structures do not enter the required line. We moved the part beyond the required scope into the readings section, whole.

Before reading it, you need to hold all of Unit 5: the difference between weak and strong CAS, the synchronization relationships acquire/release establish, and the applicable range of relaxed. The old-volume material on patterns such as SeqLock, spinlocks, and double-checked locking has been split into separate pieces, landing in [SeqLock and Spinlocks](05-seqlock-spinlock) and the Unit 5 main text; this piece focuses purely on lock-free.

The migration trail, likewise, made clear here. The mechanism explanations, code, and exercises are preserved as they were; what I changed is limited to numbering, frontmatter, and the cross-references pointing at the main text, plus a banned-words-list cleanup. The analysis in the text claims no measured readings — the judgments all come from mechanism reasoning and the literature; wherever a step leaves you unsure, you are welcome to measure it yourself with Unit 5's experimental methods.

In the earlier units we used mutex plus condition_variable to build thread-safe queues and containers. That content answered "how to do atomic operations", but it has not yet touched a deeper question: **if we use no locks at all, can we still write correct concurrent data structures?**

The first time I heard the term "lock-free programming", my gut said: isn't this just showing off? Only after reading a few lock-free stack implementations did I realize it is not showing off — it is an entire way of thinking completely different from lock-based concurrency. You no longer wrap the critical section in a lock and make threads queue; you let all threads operate on the data structure at once and coordinate conflicts with atomic operations — whoever conflicts redoes the work, but the system as a whole always moves forward. The price of this approach is a steep climb in the complexity of correctness reasoning; the gain is more controllable latency under high contention.

The word "lock-free" is actually rather misleading; it does not mean using no locks at all. What it says is that the system's overall progress cannot be blocked by any single thread's delay or crash. This distinction matters and is subtle. When I first entered this area I got tangled in it several times myself, so this piece starts from the precise definition of progress guarantees, explains the difference between lock-free and wait-free clearly, then enters the CAS loop — the core building block of lock-free programming — and implements a classic lock-free stack, before discussing the ABA problem and memory reclamation, the two nastiest things in lock-free programming. At the end we talk about when to use lock-free and when not to — that judgment matters more than knowing how to write lock-free code.

## Lock-free vs Wait-free: What Is Actually Guaranteed

Many people read "lock-free" as "no mutex". That reading is not exactly wrong, but it is imprecise — quite far off, in fact. In academia, Herlihy's 1991 paper laid the definitional foundation for wait-free and lock-free; later, Herlihy, Luchangco, and Moir in 2003 introduced obstruction-free, an even weaker notion. The C++ standard and industry largely adopted this three-tier framework, so we need to get the three levels of progress guarantee straight.

The weakest is **obstruction-free**: if a thread runs alone at some point in time (that is, all other threads are paused), it can finish its operation in a bounded number of steps. Really just "if there is no contention, it moves forward". The guarantee is too weak to hold much practical value, and we will not dwell on it later.

**Lock-free** steps it up: it guarantees that at any moment, **at least one thread in the system** can complete its operation in a bounded number of steps. Note "at least one", not "every one". That is, in a lock-free system the system as a whole advances, but individual threads may keep retrying through persistent CAS failures — starvation remains theoretically possible. The spinlock in [SeqLock and Spinlocks](05-seqlock-spinlock) is not lock-free: if one thread holds the lock and refuses to let go (say the OS suspends it), every other thread waits empty-handed, and the system as a whole stalls.

**Wait-free** is the strongest guarantee: **every thread** is guaranteed to complete its own operation in a bounded number of steps, no matter what the other threads are doing or how fast they run. Keep it in mind as the ideal: wait-free means no starvation, no retry loops, and a deterministic upper bound on the steps of every operation.

From weak to strong the ladder runs: blocking, then obstruction-free, then lock-free, with wait-free at the top. Each rung up multiplies the implementation difficulty. What we usually chase in real engineering is lock-free, because wait-free costs too much to implement, and lock-free is already good enough in most scenarios — at least the system will not collapse as a whole because one thread got stuck.

A common misconception needs clearing up front: **lock-free does not mean "faster"**. Lock-free solves the progress-guarantee problem, not the performance problem. A lock-free data structure can be slower than the mutex version under low contention, because CAS retry overhead can exceed the cost of simply taking the lock. Lock-free shows its advantage in high-contention, latency-sensitive scenarios, where a thread being paused by the scheduler does not clog the whole critical section. We will unfold this difference again with concrete data in the "When to Use Lock-Free" part.

## The CAS Loop: The Cornerstone of Lock-Free Programming

Right — the concepts of progress guarantees are clear, now let us get our hands dirty. Nearly all lock-free algorithms stand on one atomic primitive: Compare-And-Swap (CAS). In C++ the counterparts are the `compare_exchange_weak` and `compare_exchange_strong` member functions of `std::atomic`. [How Far Atomic Operations Take You](../../chapter05-atomic-memory-model/01-what-atomics-solve) already introduced the two functions' signatures and semantics; we skip the basics here and focus on their usage patterns in lock-free programming.

CAS's core semantics, in plain language: **"I believe the current value should be X; if so, swap in Y; otherwise, tell me what it actually is"**. In code terms, `compare_exchange_weak/strong` takes two key parameters: `expected` (the anticipated value) and `desired` (the new value). If the current value equals `expected`, it becomes `desired` and the function returns `true`. If not, the current value is written back into `expected` and the function returns `false`. The whole operation is atomic — no other thread's modification can slip between the "compare" and the "swap".

A quick recap of the difference between weak and strong. `compare_exchange_weak` permits spurious failure: even when the current value really equals `expected`, it may still return `false`. On some hardware architectures (such as ARM's LL/SC instruction pairs) this is unavoidable. `compare_exchange_strong` guarantees no spurious failure. On x86, weak and strong generate identical machine code (both `lock cmpxchg`); on ARM, the strong version needs an internal retry loop to eliminate spurious failures.

A key rule of thumb — same as Unit 5 says: **use weak inside loops, strong in one-shot decisions outside loops**. The reason is direct: if you are already in a loop, a failed CAS means a retry anyway, and one more spurious failure is just one more lap. But use weak outside a loop, and a single spurious failure fools you into thinking the value changed, possibly taking the wrong branch. On ARM, using strong inside a loop nests retry loops (yours outside, strong's inside), wasting instructions for nothing.

Look at the simplest CAS loop — a hand-rolled atomic add. In real engineering this example is unnecessary (`fetch_add` suffices), but it cleanly displays the basic structure of a CAS loop and is the foundation for the lock-free stack we write later:

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

Watch what the loop does: read the current value, compute the new value, then try to swap the current value from `old` to `old + delta`. If some other thread modified `value` in the meantime, CAS fails and tells us the latest value (by writing it back into the `old` parameter); we simply recompute from the latest value and try again. This is so-called "optimistic concurrency": assume no conflict; on conflict, redo. You will notice this loop cannot spin forever — after each failure `old` is refreshed to a newer value, and the system advances overall — lock-free semantics made visible at the micro scale.

Of course, for addition, `fetch_add` alone does the job; no hand-written CAS loop needed. The power of the CAS loop shows in more complex operations — updating a linked-list pointer, swapping a data structure's head node. These operations cannot be expressed with a plain `fetch_add` or `exchange`; CAS is required. Next we write a real lock-free data structure.

## The Classic Lock-Free Stack: From CAS Loop to Real Data Structure

With the basic CAS-loop pattern in hand, we can take on a real lock-free data structure. The lock-free stack is the simplest of them and the starting point of nearly every lock-free textbook; Treiber published its design back in 1986. Let us put up the overall structure, then walk through push and pop step by step.

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

The structure is dead simple: a singly linked list where `head_` is an atomic pointer to the top node. As you can see, everything happens at the head, so the only thing to synchronize is `head_`.

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

The logic of push runs in three steps: create the new node, point the new node's `next` at the current top, then try with CAS to swap `head_` from `old_head` to `new_node`. If the CAS succeeds, the new node becomes the new top. If it fails, some other thread beat us to modifying `head_` — but `compare_exchange_weak` refreshes `old_head` to the latest value, and we simply reset `new_node->next` and try again.

Note the memory-order choices: on CAS success we use `memory_order_release`, which guarantees the writes to the new node's `data` and `next` complete before the CAS succeeds; any thread that later reads the new value of `head_` with `acquire` is guaranteed to see those writes. On CAS failure, `relaxed` suffices — nothing was changed, so no synchronization is needed. `head_.load()` also uses `relaxed`, because the real synchronization is carried by the memory order of the CAS operation itself.

### pop: Taking a Node off the Top

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
            // 这里有一个严重的问题：什么时候 delete old_head？
            return value;
        }
        // CAS 失败，old_head 已被更新为最新值，重试
    }

    return std::nullopt;  // 栈空
}
```

The logic of pop is just as direct: read the current top, note its `next`, then try with CAS to swap `head_` from `old_head` to `next_node`. On success, `old_head` comes off the stack, and we extract its data and return it.

But — the story does not end here; a big trouble is buried in the code, flagged by my comment. We hold `old_head`, we know it has been detached from the stack, yet we **cannot `delete` it immediately**. The reason: before our CAS executes, other threads may also have read the same `old_head` and be operating on its `next` pointer. If we release `old_head`'s memory now, those threads are touching freed memory — use-after-free, a textbook case of undefined behavior. This problem cannot be solved by throwing a `std::atomic` at it the way a data race can; it is a **lifecycle problem at the logic level**.

This is the nastiest of lock-free programming: the **memory reclamation problem**. Let us set it aside for now and discuss it together after the ABA problem — ABA and memory reclamation are entangled, and viewing them apart makes the full picture hard to see.

## The ABA Problem: CAS's Number-One Trap

Next we meet the most notorious bug pattern in lock-free programming — the ABA problem. If you have been interviewed on lock-free programming, you were probably asked about this one. It is famous not because the entry bar is high but because it genuinely happens in practice, and once it does it is extremely hard to debug — the program does not crash; it silently produces wrong results.

### How ABA Happens

Let us demonstrate with a concrete scenario. Suppose two threads are operating on our lock-free stack, with initial state A -> B -> C and A on top.

Let thread 1 start a `pop`: it reads `head_ = A`, reads `A->next = B`, and prepares the CAS to swap `head_` from A to B. But right before the CAS, the scheduler suspends thread 1 — that is where the trouble starts.

Now watch thread 2 at work: it completes two full `pop`s — A comes off (the stack becomes B -> C), then B comes off (the stack becomes C). Then thread 2 `push`es a new value, and the allocator happens to reuse A's memory address, so the new node's address equals the old A's. The stack is now A' -> C, and A' has exactly the same address as the previous A.

Thread 1 wakes and executes its CAS: `head_.compare_exchange(A, B)`. As you can see, it finds `head_` is indeed A (same address), the CAS succeeds, and `head_` is set to B.

Here is the problem: B was already popped and freed by thread 2. Thread 1 has pointed `head_` at a dead node. Every subsequent stack operation touches freed memory — the program may crash at any moment, or worse, silently produce wrong results, and you have no idea where to even start looking.

### Why ABA Is So Dangerous

ABA is insidious because CAS only cares "does the value equal the expectation", not "has the value changed in the meantime". Let us put its blind spot in plain sight: in the ABA scenario the pointer's value really did go from A to A (passing through B); CAS cannot distinguish "was A all along" from "A -> B -> A" — to CAS the two situations are identical. This is hardly a design flaw of CAS; it is the inherent limitation of a "value comparison" primitive.

You might ask: does this really happen in practice? The answer is yes. Under high contention nodes are allocated and freed constantly, and memory allocators readily reuse just-freed addresses — especially `malloc`/`new` style allocators optimized for small objects, which maintain size-bucketed free lists where freshly freed memory is handed right back out. Add multithreaded scheduling timing, and the scene "thread 1 reads, gets suspended; thread 2 runs a full round of operations" can absolutely occur.

### Tagged Pointer: Adding a Version Number to the Pointer

Right — the problem is clear, now the solutions. The most common is the **tagged pointer**. The idea is direct: pack the pointer together with a monotonically increasing version number, bumping the version on every pointer modification. Then even if the pointer's value goes A -> B -> A, the version goes 0 -> 1 -> 2, and CAS fails correctly on the version mismatch — the version only ever increases, so no wraparound is possible.

On 64-bit systems we can stash the version number in the pointer's top 16 bits, because on most architectures user-space pointers use only the low 48. Here is a simplified implementation:

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

Rewriting the lock-free stack's `push` with tagged pointer:

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

The tagged-pointer scheme carries one premise: on your architecture CAS can operate on 64 bits (or 128, if you want more version bits). On x86-64 this is a non-issue — `lock cmpxchg` natively supports 64-bit operations. On some 32-bit embedded platforms, double-word CAS may be unavailable or expensive, and other schemes need consideration.

### Hazard Pointer: More General Memory Protection

Tagged pointer solves the ABA problem, but you will notice it does not solve the memory reclamation problem from before — we still do not know when it is safe to `delete` a node. Hazard Pointer is a more general scheme Maged Michael proposed in 2004; it addresses ABA and memory reclamation at once, and applies not just to stacks but to queues, linked lists, and all manner of lock-free data structures. C++26 has brought Hazard Pointer into the standard (`std::hazard_pointer`).

The core idea on the table: each thread holds one or a set of "hazard pointers" declaring "I am accessing this node right now". When a thread wants to release a node, it cannot `delete` outright — it checks every thread's hazard pointers; if someone is using the node, the release is deferred. Only after confirming that no thread's hazard pointer targets the node is releasing safe.

Here is a simplified pseudocode:

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

Wired into the lock-free stack's `pop`, we use it roughly like this: the thread publishes a hazard pointer aimed at `old_head`, then executes the CAS. If the CAS succeeds, the thread clears its hazard pointer and puts `old_head` into a "retirement list". Periodically (say, when the retirement list reaches a certain length), the thread scans all hazard pointers and truly frees the nodes nobody uses.

Hazard Pointer's strength is generality — it fits every kind of lock-free data structure. Its weakness is overhead: every `pop` must publish and clear a hazard pointer, and scanning the retirement list means walking every thread's slot. Under high contention this overhead can be significant.

## Memory Reclamation: The Hardest Problem in Lock-Free Programming

We have bumped into this problem repeatedly and set it aside each time. Now is the moment to face it head-on. If you found the ABA problem tiresome enough, memory reclamation will give you a worse headache — it is widely acknowledged as the hardest problem in lock-free programming, and one of the biggest obstacles keeping lock-free data structures out of wide real-world use.

In lock-based data structures, memory reclamation is simple: take the lock, operate, free the memory, unlock. The lock guarantees only one thread operates on the structure at a time, so we never face "one thread still using a node while another frees it".

But in lock-free data structures, several threads can read the same node simultaneously. Picture this: thread A has just finished reading `old_head->next` and is about to CAS, while thread B may already have popped `old_head` and `delete`d it. Thread A's CAS has not even executed, and the `old_head` in its hand is already a dangling pointer. This problem cannot be eliminated with `std::atomic` the way a data race can; it is a **lifecycle problem at the logic level**.

The mainstream approaches come in a few lines; let us take them one by one. Besides the Hazard Pointer above, there are **Epoch-based Reclamation** and **reference counting**.

Epoch-based Reclamation, understood this way: divide time into "epochs", with a global current-epoch number. Each thread records its epoch on entering the critical section. At reclamation time, nodes of an epoch can be safely freed only after every thread has left that epoch. The scheme costs less scanning than Hazard Pointer but is more complex to implement, and under certain extremes reclamation can be delayed a long time — if one thread gets stuck in an old epoch and never leaves, all the old-epoch nodes pile up unreleaseable. Facebook's Folly library has a production-grade implementation; the `WeakRef` mechanism in `folly/concurrency/UnboundedQueue.h` uses a similar idea.

Reference counting sounds the most intuitive: give each node an atomic reference count, decrement on `pop`, free at zero. But look closer and the trouble shows: the count's up and down itself needs atomic operations, and there is a window between "load the pointer" and "increment the count" in which another thread may free the node. To fix that load-increment atomicity, reference-counting schemes often degenerate into some form of Hazard Pointer or need double-word CAS — the implementation complexity never really drops. `std::atomic<std::shared_ptr>` is usable as of C++20, but its overhead is not small (usually implemented with an internal spinlock), and it does not fit genuinely lock-free scenarios.

## When to Use Lock-Free — and When Not To

After all these problems and solutions, you might ask: if lock-free programming is this complicated, why use it at all? The answer: in specific scenarios lock-free really does deliver performance advantages mutex cannot. But those "specific scenarios" are far narrower than you imagine. I have seen plenty of cases where great effort went into converting a mutex-protected data structure to lock-free, only for the benchmark to come out slower — followed by blank stares at the numbers.

### Scenarios That Suit Lock-Free

The most typical scenario, first in line: **high contention, low latency**. When hordes of threads fight over one data structure, mutexes cause frequent context switches (each switch a kernel-mode round trip costing microseconds). Lock-free algorithms turn the contention from "queue for the lock" into "CAS retry"; retries cost too, but they happen in user space with no kernel scheduling involved — latency is more controllable and tail latency smaller. High-frequency trading, real-time signal processing, the main loop of online game servers: in these settings a few microseconds of latency difference can be the line between acceptable and unacceptable.

**Single-producer single-consumer (SPSC) queues** are another lock-free scenario to watch. With exactly one producer and one consumer, no CAS loop is needed — atomic variables with `acquire/release` semantics alone give correct synchronization. Simple to implement, extremely fast, nearly zero contention: here lock-free is practically the default choice. [SPSC and MPMC Lock-Free Queues](03-lock-free-queues) devotes itself to the SPSC queue design.

**Communication between an interrupt context and the main loop** is also common in embedded systems. To get work done inside an interrupt handler, you cannot call anything that might block (including `mutex::lock`), so a lock-free queue is practically the only choice.

### Scenarios That Do Not Suit Lock-Free

Please do not rush to replace every mutex in your project; in the scenarios below, lock-free usually trades at a loss.

**Low-contention scenarios** often leave lock-free slower than mutex. The reason is simple: uncontended mutex lock/unlock costs very little (one atomic instruction plus a branch prediction), while a CAS loop needs at least one atomic operation and one conditional check even on the success path. For a data structure that sees contention once per 1000 accesses on average, the total cost of mutex likely sits below lock-free.

**Complex critical sections** do not suit lock-free. Try to express "erase an element from the map and update the size counter at once" with CAS and you will find it extremely hard — the code is difficult to get right, let alone maintain. Mutexes natively support critical sections of arbitrary complexity, an advantage that is irreplaceable when the logic gets complex.

**Team maintenance cost** is a consideration that cannot be ignored. Lock-free code is far harder to read, review, and debug than its mutex counterpart. A CAS loop's bug may trigger once in a million runs, and ThreadSanitizer's false-positive rate on lock-free code is not low either. If your team has not yet accumulated enough lock-free experience, our advice is to write correct code with mutex — it is worth more than fast-but-unreliable code written with CAS; correct code always beats fast wrong code.

### Benchmark: Do Not Guess — Measure

Any claim of "lock-free is faster" or "mutex is faster" is empty talk without concrete benchmark data. I have seen too many cases that were "theoretically faster lock-free" yet ran slower in reality because of cache-coherence overhead, CAS retry storms, false sharing, and the like. The bottleneck of concurrent performance often hides where you least expect. [Before Measuring, Set the Conditions: Serial Baselines and Measurement Traps](../../chapter06-validating-designs/03-measuring-benefits) spends the whole piece on the complete measurement methodology.

A basic benchmark framework you organize should include: throughput tests at different thread counts (1, 2, 4, 8, 16), latency distributions (p50, p99, p999) under different operation mixes (pure push, pure pop, mixed), and result comparisons across hardware. [SPSC and MPMC Lock-Free Queues](03-lock-free-queues) carries a complete benchmark set covering SPSC, mutex queues, and MPMC.

We can start with this simple but effective benchmark template:

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

When running benchmarks, it is advisable to disable CPU frequency scaling (`cpupower frequency-set -g performance`), pin CPU cores (`taskset` or `pthread_setaffinity_np`), and take the median over several runs. These variable-control measures strongly influence concurrent benchmark results — without them you may get one number today, a completely different one tomorrow, and then stare blankly at two sets of data.

## Where to Go from Here

In this piece we built the basic cognitive frame for lock-free programming: lock-free and wait-free are not the same thing (the former guarantees the system as a whole advances, the latter that every thread advances); the CAS loop is the core building block of lock-free algorithms ("optimistic concurrency" — on conflict, redo); the lock-free stack is the classic introductory case, yet it already exposed the two core difficulties of ABA and memory reclamation. Tagged pointer solves ABA with a version number, and Hazard Pointer provides more general memory protection, but each carries its own performance cost and implementation complexity. Finally we discussed when to use lock-free and when not to — that engineering judgment matters more than knowing how to write lock-free code.

The lock-free stack, though, is only a starting point. [SPSC and MPMC Lock-Free Queues](03-lock-free-queues) takes us onward to more practical data structures: the SPSC queue, with exactly one producer and one consumer, needs no CAS loop — clean to implement and extremely fast, a common choice in embedded and network programming. The MPMC queue must handle multi-producer multi-consumer contention, another step up in complexity, and over there a complete benchmark set awaits — let the data speak, not the guesses.

> The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); take it from `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Exercises

### Exercise 1: Implement the Lock-Free Stack and Watch CAS Retries

Using the `LockFreeStack` code from this piece, complete the following tasks:

1. Implement complete `push` and `pop` (skip memory reclamation for now; just let the test program run briefly).
2. Start 4 threads pushing 1000000 integers in total, then 4 threads popping concurrently.
3. Add a counter to the CAS loop recording the total number of CAS retries. Under high contention this number gets large.
4. Compare against `std::mutex` + `std::stack`. Do not rush to judge faster or slower — try different thread counts and operation counts.

### Exercise 2: Reproducing the ABA Problem

The ABA problem is hard to reproduce under normal conditions because it needs precise scheduling timing. But we can use `std::this_thread::sleep_for` to manufacture a delay and widen the window:

1. Add a `sleep_for(std::chrono::milliseconds(100))` before the CAS in `pop`.
2. Let thread 1 start a `pop` (it will sleep before the CAS), while thread 2, inside those 100 ms, pops every element off the stack and pushes one new node back.
3. Observe whether thread 1's CAS succeeds after waking and whether the data is correct. If the allocator happens to reuse the address, you have witnessed ABA.

### Exercise 3: The Tagged Pointer Retrofit

1. Rewrite `LockFreeStack` with the `TaggedPointer` template from this piece, making `head_` a `TaggedPointer<Node>`.
2. Rerun the test from Exercise 2 and confirm ABA no longer happens.
3. Think: what problem does the tagged-pointer scheme hit on 32-bit platforms? If a pointer occupies 32 bits, how do you encode a version number in the remaining space?

> The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); take it from `code/volumn_codes/vol5/ch04-concurrent-data-structures/`.

## Reference Resources

- [Wait-Free Synchronization — Maurice Herlihy (1991)](https://cs.brown.edu/people/mph/Herlihy91/p124-herlihy.pdf)
- [Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects — Maged Michael](https://www.cs.otago.ac.nz/cosc440/readings/hazard-pointers.pdf)
- [compare_exchange_weak / compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Atomic Operations: The Performance Cost — Fedor Pikus, CppCon 2024](https://www.youtube.com/watch?v=ZQFzMfHIxng)
- [Non-blocking algorithm — Wikipedia](https://en.wikipedia.org/wiki/Non-blocking_algorithm)
- [Lock-Free Programming — cppreference](https://en.cppreference.com/w/cpp/atomic#Lock-free_property)
