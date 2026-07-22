---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Understanding `shared_ptr` control block mechanisms, thread safety, and
  performance characteristics
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 1: RAII 深入理解'
- 'Chapter 1: unique_ptr 详解'
reading_time_minutes: 25
related:
- weak_ptr 与循环引用
- 自定义删除器
tags:
- host
- cpp-modern
- intermediate
- shared_ptr
- 智能指针
- 引用计数
title: 'Detailed Explanation of `shared_ptr`: Shared Ownership and Reference Counting'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/03-shared-ptr.md
  source_hash: 93f7f08990ef8d3c911a5c29f45f86b850ece61f3c68f948f4402ff2086313c1
  translated_at: '2026-06-16T03:56:12.821060+00:00'
  engine: anthropic
  token_count: 4044
---
# shared_ptr Deep Dive: Shared Ownership and Reference Counting

In the previous post, we discussed `unique_ptr`—the zero-overhead smart pointer for exclusive ownership. However, in the real world, resources aren't always "single-owner." Sometimes, an object genuinely needs to be held and managed jointly by multiple modules—like a configuration object read by multiple subsystems, a network connection shared among tasks, or a cache entry accessed by multiple consumers. In these cases, the "exclusive" semantics of `unique_ptr` just aren't enough.

`shared_ptr` is designed for exactly this scenario. Its core concept is **reference counting**: every time a new `shared_ptr` points to the object, the count increments; every time one is destroyed, the count decrements; when the count reaches zero, the object is automatically destroyed. It sounds simple and elegant, but the implementation details—control blocks, atomic operations, memory allocation strategies—are far more complex than one might imagine.

## Shared Ownership: Semantics and Cost

`shared_ptr` expresses "shared ownership" semantics: multiple `shared_ptr` instances can point to the same object, jointly determining its lifecycle. The object is only deleted when the very last `shared_ptr` is destroyed.

```cpp
#include <iostream>
#include <memory>

struct Widget {
    Widget() { std::cout << "Widget constructed\n"; }
    ~Widget() { std::cout << "Widget destroyed\n"; }
    void work() { std::cout << "Widget working\n"; }
};

int main() {
    // Create a Widget and manage it with shared_ptr
    auto ptr1 = std::make_shared<Widget>();

    {
        // Copy ptr1, reference count becomes 2
        auto ptr2 = ptr1;
        ptr2->work();
    } // ptr2 goes out of scope, reference count drops to 1

    ptr1->work();
} // ptr1 goes out of scope, count drops to 0, Widget destroyed
```

Output:

```text
Widget constructed
Widget working
Widget working
Widget destroyed
```

This looks great. But shared ownership isn't free—every copy and destruction of a `shared_ptr` requires updating the reference count, and this count must be thread-safe (atomic operations). Furthermore, `shared_ptr` internally maintains a control block to store the reference count and other metadata. These overheads become very noticeable in scenarios involving frequent creation and destruction of `shared_ptr` instances.

My advice is: use `unique_ptr` whenever you can, and only use `shared_ptr` when you genuinely need shared ownership. `shared_ptr` should not become an excuse for "being too lazy to think about ownership."

## The Control Block: The Internal Structure of `shared_ptr`

To understand the performance characteristics of `shared_ptr`, we must first understand its internal structure. A `shared_ptr` actually contains two pointers: one to the managed object, and another to the control block.

The control block is a data structure allocated on the heap, containing the strong reference count (number of `shared_ptr` instances), the weak reference count (number of `weak_ptr` instances), a custom deleter (if any), and a custom allocator (if any). When you create a `shared_ptr` using `make_shared`, the object and the control block are placed in the same memory block (single allocation); whereas using `new` results in two separate allocations.

Let's use a simplified diagram to understand this:

![shared_ptr internal structure diagram](./03-shared-ptr-structure.drawio)

So, a `shared_ptr` object itself is the size of two pointers (`2 * sizeof(void*)`). On a 64-bit system, that's 16 bytes—double the size of `unique_ptr` (8 bytes). The size of the control block itself depends on the implementation (GNU libstdc++ on x86_64 is approximately 32 bytes).

## The Advantage of `make_shared`: Single Allocation

As mentioned earlier, `make_shared` places the object and the control block in a contiguous memory block. This brings three significant benefits.

First is **fewer heap allocations**—reduced from two to one. In performance-sensitive code, heap allocation is expensive (often involving locks, traversing free lists, etc.), so reducing allocation counts is always beneficial. You can verify that `make_shared` indeed performs only one allocation using tools like `valgrind --tool=massif`.

Second is **better cache locality**. Since the object and control block are in the same memory block, a CPU cache line is likely to hit both. Conversely, two separately allocated blocks might be physically far apart, leading to more cache misses.

Third is **less memory fragmentation**. One allocation means one deallocation, rather than freeing separately at two different locations.

```cpp
// Recommended: Single allocation
auto sp1 = std::make_shared<Widget>();

// Not recommended: Two allocations
auto sp2 = std::shared_ptr<Widget>(new Widget);
```

⚠️ `make_shared` also has a lesser-known downside: because the object and the control block share the same memory block, when all `shared_ptr` instances are destroyed (strong reference count reaches zero), the object is destructed, but the memory block is not released immediately—it must wait until all `weak_ptr` instances are also destroyed (weak reference count reaches zero) before the entire block is reclaimed. If the object is large and a `weak_ptr` is still alive, it may result in higher memory usage than expected. If you expect `weak_ptr` instances to exist for a long time, consider using `new` to separate the object's memory from the control block, allowing the object's memory to be released immediately when the strong count hits zero.

## Atomic Operations and Thread Safety of Reference Counts

`shared_ptr` uses atomic operations for its reference count to ensure thread safety. This means that in a multi-threaded environment, you can safely copy and destroy the `shared_ptr` instance itself (incrementing/decrementing the count is atomic), but **access to the managed object is not protected**—if multiple threads read and write to the object itself simultaneously, you still need to implement your own locking.

This is a common misconception: many think `shared_ptr` provides "thread safety for the object," but it actually only guarantees "thread safety for the reference count." We can use cppreference's description to understand this precisely: the control block of `shared_ptr` is thread-safe—multiple threads can operate on different `shared_ptr` instances (even if they point to the same object) without external synchronization. However, the same `shared_ptr` instance cannot be read/written by multiple threads simultaneously (requires locking). Concurrent access to the managed object must be made safe by the user.

```cpp
// Thread-safe: copying shared_ptr
std::shared_ptr<Widget> global_ptr;

void thread1() {
    // Atomic increment, safe
    auto local = global_ptr;
    if (local) local->work();
}

// NOT thread-safe: accessing the object
void thread2() {
    // local->data++ is NOT protected by shared_ptr!
    if (global_ptr) global_ptr->data++;
}
```

From a performance perspective, every copy or destruction of a `shared_ptr` generates an atomic operation (typically `fetch_add` or `fetch_sub`). Atomic operations have low overhead on single-core systems (possibly just a specific CPU instruction), but on multi-core systems, they incur cache coherence protocol overhead (cache line bouncing). If your code frequently creates and destroys `shared_ptr` instances (e.g., in a hot loop), this overhead can become very significant. You can verify the overhead difference between single-threaded and multi-threaded scenarios using Google Benchmark.

The logic when decrementing the reference count deserves particular attention. When `fetch_sub` returns 1 (meaning this is the last `shared_ptr`), the object needs to be destroyed. Mainstream implementations (like GNU libstdc++) use `release` semantics to ensure all previous writes are visible to the destruction code, and insert a `acquire` fence before destruction. These memory barriers have little cost on x86 (which has strong memory ordering anyway), but on weakly-ordered architectures like ARM, they can cause pipeline flushes.

## Performance Overhead Analysis of `shared_ptr`

Let's make an intuitive comparison, putting the overhead of `shared_ptr`, `unique_ptr`, and raw pointers into a single table:

| Dimension | Raw Pointer | unique_ptr | shared_ptr |
|-----------|-------------|------------|------------|
| Object Size | 8B (64-bit) | 8B | 16B |
| Extra Heap Alloc | None | None | Control Block (24-32B+) |
| Copy Overhead | 8B copy | Not copyable | Atomic fetch_add |
| Destruction Overhead | None | delete | Atomic fetch_sub + potential delete |
| Thread Safety | None | None | Ref count safe, object unsafe |

From this table, it is clear that `shared_ptr` is heavier than `unique_ptr` in every dimension. This isn't to say `shared_ptr` is bad—it is the correct design choice for shared ownership scenarios—but you should use it only when shared ownership is strictly necessary, not "just for convenience."

In real projects, I've seen many codebases manage almost all objects with `shared_ptr`, resulting in reference counts flying everywhere, unoptimizable performance, and frequent circular reference issues. A better approach is to clarify ownership relationships during the design phase: manage most resources with `unique_ptr`, use `shared_ptr` only in the few places where sharing is truly needed, and pass non-owning access via references (`T&`) or raw pointers (`T*`, which does not hold ownership).

## Aliasing Constructor: A Powerful, Little-Known Feature

`shared_ptr` has a very powerful but relatively unknown constructor called the **aliasing constructor**. Its signature is:

```cpp
template<class Y>
shared_ptr(const shared_ptr& x, Y* ptr) noexcept;
```

This constructor creates a new `shared_ptr` that shares ownership of `x` (i.e., the reference count is shared with `x`), but `get()` returns `ptr` instead of `x.get()`. Simply put: **it allows you to hold a "part" of an object without managing that part's lifecycle separately.**

The most common use is accessing members of an object:

```cpp
struct Member {
    int data;
};

struct Container {
    Member m;
};

auto container_ptr = std::make_shared<Container>();

// Create a shared_ptr to 'm' that shares ownership with 'container_ptr'
std::shared_ptr<Member> member_ptr(container_ptr, &container_ptr->m);

// 'container_ptr' is still alive, 'member_ptr' keeps it alive
```

This feature is particularly useful when implementing "smart pointers to container elements"—for example, if you want to return a `shared_ptr` to an element inside a `vector`, but don't want the caller to hold the `shared_ptr` to the whole `vector`. With the aliasing constructor, you can return a `shared_ptr` that only exposes the element type, while the lifecycle is still managed by the container's `shared_ptr` underneath.

## `enable_shared_from_this`: Obtaining `shared_ptr` in Member Functions

Sometimes, a member function of an object needs to return a `shared_ptr` to itself. The most intuitive approach, `std::shared_ptr<Widget>(this)`, is fatally flawed—it creates a new control block, causing the object to be deleted twice. The correct way is to inherit from `std::enable_shared_from_this` and call `shared_from_this()`:

```cpp
class Widget : public std::enable_shared_from_this<Widget> {
public:
    std::shared_ptr<Widget> get_shared() {
        return shared_from_this(); // Correct
    }
};

auto w = std::make_shared<Widget>();
auto w2 = w->get_shared(); // OK
```

⚠️ Using `enable_shared_from_this` has a prerequisite: the object must already be managed by a `shared_ptr`. If you create an object on the stack or manage it with a raw pointer, calling `shared_from_this()` results in undefined behavior. Also, do not call `shared_from_this()` in the constructor—because the `shared_ptr` constructor hasn't finished yet.

## Common Misuses and Pitfalls

Before diving into embedded trade-offs, let's inventory several common misuse patterns of `shared_ptr`. I've stepped in these "potholes" more than once myself, and I hope readers can avoid them early.

**Misuse 1: Using `new` to create a second control block.** This is the most fatal error. If you write `std::shared_ptr<Widget>(this)` inside a member function of an object already managed by a `shared_ptr`, the compiler creates a brand new control block with a reference count starting at 1. The result is two independent control blocks managing the same object—when both `shared_ptr`s are destroyed, the object is deleted twice. The correct approach is to inherit from `enable_shared_from_this` and call `shared_from_this()`.

**Misuse 2: Exposing `shared_ptr` ownership intent in interfaces.** If you write a function `void func(std::shared_ptr<Widget>)`, the signature itself implies "I want to share ownership with you." But often, the function just wants to use the object, not hold it. In these scenarios, passing `Widget&` or `Widget*` is more appropriate—no ownership implication, no reference count overhead.

**Misuse 3: Using `shared_ptr` to manage objects that "don't need sharing."** Some teams use `shared_ptr` for all heap objects for convenience—"shared_ptr can handle anything." This leads to fuzzy ownership semantics (everyone holds it, so no one is responsible), degraded performance (atomic operations everywhere), and increased risk of circular references. My experience is: **90% of objects should be managed by `unique_ptr`, only 10% that truly need sharing should use `shared_ptr`.**

**Misuse 4: Ignoring the difference between `make_shared` and `new`.** `make_shared` merges the object and control block in a single allocation, but this also means the object's destruction and the control block's release don't happen at the same time—when all `shared_ptr`s are destroyed, the object is destructed, but if `weak_ptr`s are still alive, the entire memory block (including the object's space) isn't released until all `weak_ptr`s are destroyed. For large objects, this can lead to a situation where "no one is using it, but memory isn't returned." If you expect long-lived `weak_ptr`s, using `new` to allocate the object and control block separately might be better.

## Systemic Consequences of `shared_ptr` Abuse

I've dedicated a separate section to this because, simply put, I used to be an abuser myself...

We've inventoried common misuse patterns of `shared_ptr`, but the severity goes beyond just "writing something wrong somewhere." When `shared_ptr` is systematically abused in a codebase, it brings **chronic poison at the architectural level**—not the acute kind of error that prevents compilation, but a progressive rot that makes the codebase unmaintainable, unreasonable, and unoptimizable. I've seen more than one project fall into this quagmire because "all objects are managed with `shared_ptr," and fixing it often requires massive refactoring.

### Collapse of the Ownership Model

In a healthy design, every object should have a clear owner—"who created it, who destroys it, who decides its lifecycle"—these questions should be answered in the design phase. But when you use `shared_ptr` everywhere, the answer becomes "who knows? It gets destroyed when the count hits zero." It sounds convenient, but the cost is losing control over the object's lifecycle: you can't guarantee the object is alive at any specific moment (because other holders might release it), nor can you guarantee it is destroyed at any specific moment (because unknown holders might still be referencing it). This "nobody's responsible" state is similar to the problems caused by global variable proliferation.

Sean Parent, in his C++Now talk, aptly compared abusing `shared_ptr` to **implicit global variables**—any code holding a `shared_ptr` participates in the object's lifecycle management, which is strikingly similar to global variables' "accessible anywhere, lifetime can be extended anywhere" characteristic. A more practical problem is that once your public interface returns a `shared_ptr`, all callers are forced to use `shared_ptr`, even if they just want to borrow the object temporarily. You deprive the caller of the right to choose the ownership model—a better approach is to return `unique_ptr` (the caller can freely `move` it to `shared_ptr`) or a raw pointer/reference (non-owning access).

### Cache Line Contention Under Multithreading

This issue doesn't appear in single-threaded code at all, but becomes glaring in multi-threaded scenarios. The control block of `shared_ptr` stores both strong and weak reference counts. These two atomic counters are typically in the same control block and likely share the same cache line (usually 64 bytes). When multiple threads frequently copy and destroy `shared_ptr`s pointing to the **same object**, every atomic modification of the reference count by any thread causes that cache line to bounce between cores—even if these threads are operating on their own independent `shared_ptr` instances, as long as they point to the same object, they compete for the same control block's cache line.

Talking isn't enough; let's run a test. The benchmark program below (`shared_ptr_benchmark.cpp`) builds a thread-safe producer-consumer queue, passing messages using raw pointers and `shared_ptr` respectively. The test environment is my Windows WSL2 Arch Linux, AMD Ryzen 7 5800H (14 threads), GCC 15.2, C++23 Release build. Results are as follows:

| Approach | Messages | Avg Time | Relative Overhead |
|----------|----------|----------|-------------------|
| Raw Pointer | 10,000 | ~30 ms | Baseline |
| `shared_ptr` | 10,000 | ~35 ms | **+15-20%** |

The 15-20% overhead might be more significant in real applications because our test used a mutex-protected queue, and mutex overhead masks part of the `shared_ptr` cost. In lock-free queues or higher concurrency scenarios (like 8 threads in the original test), the overhead of `shared_ptr` becomes even more obvious. The source of this overhead is clear: every `shared_ptr` copy requires an atomic increment of the reference count, and every destruction requires an atomic decrement—in multi-threaded scenarios where multiple threads operate on the same control block, these atomic operations cause cache line contention. It can be ignored in low-concurrency, low-throughput scenarios, but be cautious on high-concurrency hot paths.

### Circular References: The Silent Memory Leak

When an object leaks due to circular references, you won't get any error message—the `shared_ptr` reference count never reaches zero, so the object sits quietly on the heap, occupying memory. No crash, no assertion failure, no logs telling you "hey, this object leaked." You might only notice the problem when memory usage keeps growing, and then need tools like Valgrind or AddressSanitizer to locate the leak. Worse still, circular references are often not simple loops between two objects, but complex dependency graphs involving multiple objects—A holds B, B holds C, C holds A—tracking the reference chain itself is very painful.

In contrast, the exclusive ownership model of `unique_ptr` makes circular references impossible at compile time (you cannot construct a valid exclusive ownership ring), which is its huge advantage at the design level. If you find yourself needing extensive use of `weak_ptr` to break circular references, that itself is a strong signal: your ownership model design has issues, and you should re-examine the dependencies between objects rather than patching everywhere with `weak_ptr`.

### Ownership Inversion: The Time Bomb in Callbacks

This problem is particularly common in asynchronous programming and extremely difficult to debug. Suppose Object A holds a Timer, and the Timer's callback captures A's `shared_ptr`. When A is reset in the main thread, the Timer thread becomes the sole owner of A—A's lifecycle is "inverted" onto the Timer thread. If the Timer's destructor needs to join the thread it resides on (as `std::jthread` does), it triggers a deadlock: a thread tries to join itself. This is undefined behavior. The root of this bug lies in `shared_ptr` letting you be "too lazy to think about ownership"—you thought you released A, but the callback is still holding onto it in the shadows. The correct approach is to define lifecycle constraints at the design stage: if A's destruction depends on the Timer thread ending, then A must be destroyed before the Timer, using `unique_ptr`'s exclusive semantics to express this constraint.

### Uncertainty of Destruction Timing and Real-Time Risks

When you drop a `shared_ptr`, you can't be sure if it's the last one—the object might be destroyed in this drop, or it might survive because other holders exist. This means the timing of the destructor call is **unpredictable**, and the destruction order is **undefined**. In real-time systems, this is especially dangerous: if you drop a `shared_ptr` in an audio callback, interrupt service routine (ISR), or any code path with real-time requirements, and it happens to be the last holder, the triggered destructor could bring unacceptable latency—heap deallocation, file I/O, log writing—these are all non-deterministic, time-consuming operations. Timur Doumler proposed a clever `defer_destruction` scheme when discussing C++ audio development: periodically clean up `shared_ptr`s that might need destruction on a low-priority thread, ensuring real-time threads never trigger destruction. But ultimately, if you used `unique_ptr` with explicit lifecycle management at the design stage, you wouldn't need such workarounds at all.

## Practical Selection Guide: When to Use `shared_ptr`

Before discussing embedded trade-offs, let's do a practical, decision-oriented analysis. Many people hesitate between `unique_ptr` and `shared_ptr`, but the judgment criteria are simple—ask yourself one question: **Does this object need to be jointly owned by multiple independent modules?**

If the answer is "No"—the object's lifecycle is determined by a clear "owner," and other modules just borrow it temporarily—use `unique_ptr` + raw pointers/references for passing. This covers the vast majority of scenarios.

If the answer is "Yes"—multiple modules genuinely need to independently decide "I'm still using this object," and no module can claim "I am the only owner"—then use `shared_ptr`.

Typical `shared_ptr` use cases include: shared modules in plugin systems (multiple components may depend on the same plugin instance simultaneously, no one can unload it prematurely), shared state in asynchronous callback chains (multiple futures/callbacks need to keep the state alive until they complete), shared nodes in trees or graphs (multiple parents reference the same child).

Typical scenarios where you should *not* use `shared_ptr` include: function parameter passing (passing a reference is enough), objects with a unique owner (use `unique_ptr`), simple caches (use `weak_ptr` to observe, `unique_ptr` to hold).

Let's look at a specific design decision example—implementing a simple task scheduler:

```cpp
// Version 1: unique_ptr - Scheduler owns the task
class Scheduler {
public:
    void add(std::unique_ptr<Task> task) {
        tasks_.push_back(std::move(task));
    }
private:
    std::vector<std::unique_ptr<Task>> tasks_;
};

// Version 2: shared_ptr - Shared ownership
class Scheduler {
public:
    void add(std::shared_ptr<Task> task) {
        tasks_.push_back(task);
    }
private:
    std::vector<std::shared_ptr<Task>> tasks_;
};
```

The first version uses `unique_ptr`—ownership transfers to the scheduler upon submission, simple and clear. The second version uses `shared_ptr`—allowing multiple schedulers or external code to hold a reference to the same task, and the task is destroyed only when the last holder leaves. The choice depends on your design requirements, not "which is more convenient."

## Embedded Trade-offs: Memory Overhead and ISR Considerations

Using `shared_ptr` in embedded scenarios requires extreme caution. Let's analyze the reasons one by one.

First is **memory overhead**. On a 32-bit MCU, a `shared_ptr` object occupies 8 bytes (two pointers), and the control block is at least 16-24 bytes (depending on implementation). If you use `make_shared`, the object and control block together might occupy `sizeof(T) + 24` bytes. For an MCU with only a few dozen KB of RAM, this overhead becomes very noticeable when the number of objects is large. Let's do the math: suppose your MCU has 64KB of RAM, and you need to manage 50 peripheral handles, each 16 bytes. Managed with `unique_ptr`, the total overhead is `50 * (16 + 8) = 1200` bytes; with `shared_ptr` + `make_shared`, the total overhead is `50 * (16 + 8 + 24) = 2400` bytes—an extra 1600 bytes, or 2.4% of total RAM. On MCUs with tighter memory (like the STM32F103 with only 20KB RAM), this figure becomes even more glaring.

Second is **heap allocation**. The control block needs to be allocated on the heap, yet many embedded systems either disable the heap or have very limited heap space. Frequent heap allocation leads to memory fragmentation, eventually causing allocation failures. If your system runs for a long time (embedded devices usually run for years), the fragmentation problem gets progressively worse. A possible mitigation is using `shared_ptr` with a custom allocator (like a memory pool allocator), moving control block allocation from the system heap to a pre-allocated memory pool.

Third is **atomic operations**. Atomic increment/decrement of the reference count on a single-core MCU might degrade into disabling interrupts (depending on the toolchain's implementation of `std::atomic`), which affects interrupt response times. Using `shared_ptr` in an ISR is a terrible idea—not just because of heap operations, but also because atomic operations might disable interrupts. If your system has strict real-time requirements (e.g., a control loop must complete within 100us), any indeterminate delay in the ISR is unacceptable.

My advice is: in embedded systems, prioritize `unique_ptr` or use RAII wrapper classes directly. If shared semantics are truly needed, consider intrusive reference counting—placing the reference count inside the object to avoid extra heap allocation. In single-threaded environments, the reference count in an intrusive solution can be a plain `size_t`, requiring no atomic operations and having extremely low overhead. We will discuss this topic in detail in the "Custom Deleters and Intrusive Reference Counting" article.

The next chapter discusses `weak_ptr`—`shared_ptr`'s partner, built specifically to solve circular references.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [Inside STL: The different types of shared pointer control blocks](https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626)
- [std::shared_ptr thread safety](https://stackoverflow.com/questions/9127816/stdshared-ptr-thread-safety)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
