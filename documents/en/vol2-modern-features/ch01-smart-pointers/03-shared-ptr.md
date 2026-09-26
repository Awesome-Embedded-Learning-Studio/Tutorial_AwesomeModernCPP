---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Understand shared_ptr's control block mechanism, thread safety, and
  performance characteristics
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
- 'Chapter 1: Deep Dive into unique_ptr: A Zero-Overhead Smart Pointer with Exclusive Ownership'
reading_time_minutes: 25
related:
- 'weak_ptr and Circular References: Breaking the Ownership Deadlock'
- 'Custom Deleters and Intrusive Reference Counting'
tags:
- host
- cpp-modern
- intermediate
- shared_ptr
- 智能指针
- 引用计数
title: 'Deep Dive into shared_ptr: Shared Ownership and Reference Counting'
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/03-shared-ptr.md
  source_hash: 03eda88dc65fa0e0eb537c6e3cbc7f87613d3eb17d1759e52fe4dd2f794f0247
  translated_at: '2026-09-25T14:40:51+00:00'
  engine: anthropic
  token_count: 6300
---
# Deep Dive into shared_ptr: Shared Ownership and Reference Counting

Last time we talked about `unique_ptr`—the zero-overhead smart pointer for exclusive ownership. But real-world resources are not always "owned by one master." Sometimes an object genuinely needs to be held and managed jointly by multiple modules—a configuration object read by several subsystems, a network connection shared by several tasks, a cache entry accessed by many consumers. In cases like these, `unique_ptr`'s "exclusive" semantics simply aren't enough.

`std::shared_ptr` is designed for exactly this scenario. Its core idea is **reference counting**: each additional `shared_ptr` pointing at the object increments the count; each one that goes away decrements it; when the count reaches zero, the object is destroyed automatically. It sounds simple and elegant, but the implementation details behind it—control blocks, atomic operations, memory allocation strategies—are far more intricate than you might imagine.

## Shared Ownership: Semantics and Cost

`shared_ptr` expresses "shared ownership" semantics: multiple `shared_ptr`s can point to the same object, and together they decide the object's lifetime. Only when the last `shared_ptr` is destroyed does the object get deleted.

```cpp
#include <memory>
#include <iostream>

struct Connection {
    explicit Connection(const std::string& addr) : addr_(addr) {
        std::cout << "Connected to " << addr_ << "\n";
    }
    ~Connection() {
        std::cout << "Disconnected from " << addr_ << "\n";
    }
    void send(const std::string& msg) {
        std::cout << "Send to " << addr_ << ": " << msg << "\n";
    }
private:
    std::string addr_;
};

void demo_shared() {
    auto conn = std::make_shared<Connection>("192.168.1.1:8080");
    {
        auto conn2 = conn;  // Reference count: 1 → 2
        conn2->send("hello from conn2");
        std::cout << "use_count: " << conn.use_count() << "\n";  // 2
    }   // conn2 leaves scope, reference count: 2 → 1

    conn->send("hello from conn");
    std::cout << "use_count: " << conn.use_count() << "\n";  // 1
}   // conn leaves scope, reference count: 1 → 0, Connection destroyed
```

Output:

```text
Connected to 192.168.1.1:8080
Send to 192.168.1.1:8080: hello from conn2
use_count: 2
Send to 192.168.1.1:8080: hello from conn
use_count: 1
Disconnected from 192.168.1.1:8080
```

We turned this rising and falling of the count into an animation—you can play it, pause it, or step through it one increment at a time with the step button, and see every add and subtract of use_count clearly:

<Anim id="shared-refcount" />

It all looks lovely. But shared ownership is not free—every copy and every destruction of a `shared_ptr` must update the reference count, and that count has to be thread-safe (atomic operations). On top of that, `shared_ptr` internally maintains a control block to store the reference count and other metadata. In scenarios where `shared_ptr`s are created and destroyed frequently, these costs become very noticeable.

My advice: use `unique_ptr` whenever you can, and reach for `shared_ptr` only when you truly need shared ownership. `shared_ptr` should not become an excuse for being too lazy to think about ownership.

## The Control Block: shared_ptr's Internal Structure

To understand `shared_ptr`'s performance characteristics, you must first understand its internal structure. A `shared_ptr` actually contains two pointers: one to the managed object, and one to the control block.

The control block is a heap-allocated data structure containing the strong reference count (the number of `shared_ptr`s), the weak reference count (the number of `weak_ptr`s), the custom deleter (if any), and the custom allocator (if any). When you create a `shared_ptr` with `std::make_shared`, the object and the control block are placed in the same chunk of memory (one allocation); when you create it with `std::shared_ptr<T>(new T)`, the object and the control block are two separate allocations.

Let's use a simplified diagram to build intuition:

![Simplified diagram of shared_ptr's internal structure](./03-shared-ptr-structure.drawio)

So a `shared_ptr` object itself is `2 * sizeof(void*)` in size—two pointers. On a 64-bit system that is 16 bytes, twice as big as `unique_ptr` (8 bytes). The control block's own size depends on the implementation (GNU libstdc++'s is about 32 bytes on x86_64).

## The Advantages of make_shared: A Single Allocation

As mentioned above, `make_shared` puts the object and the control block into one contiguous chunk of memory. This brings three significant benefits.

First, **fewer heap allocations**—from two down to one. In performance-sensitive code, heap allocation is expensive (it typically involves locks, traversing free lists, and so on), so fewer allocations is always a good thing. Write a small program that prints allocation addresses, and you can see that `make_shared` `new`s only once.

Second, **better cache locality**. The object and the control block are in the same chunk of memory, so a CPU cache line may hit both at once. Two separately allocated chunks, on the other hand, may be physically far apart, causing more cache misses.

Third, **less memory fragmentation**. One allocation means one release, rather than two releases at two different locations.

```cpp
// Recommended: a single allocation
auto p1 = std::make_shared<Connection>("10.0.0.1:9090");

// Not recommended: two allocations, and less exception-safe than make_shared
auto p2 = std::shared_ptr<Connection>(new Connection("10.0.0.1:9090"));

// Size comparison
std::cout << "sizeof(shared_ptr): " << sizeof(p1) << "\n";  // 16 (64-bit)
std::cout << "sizeof(unique_ptr): " << sizeof(std::unique_ptr<Connection>) << "\n";  // 8
```

`make_shared` also has a lesser-known drawback: because the object and the control block share the same chunk of memory, when all `shared_ptr`s are destroyed (the strong count reaches zero), the object is destructed, but the control block's memory is not released immediately—the whole chunk can only be reclaimed after all `weak_ptr`s are destroyed too (the weak count reaches zero). If the object is large and `weak_ptr`s are still in use, memory usage may end up higher than expected. If you expect `weak_ptr`s to live for a long time, consider using `std::shared_ptr<T>(new T)` to make the object's memory independent of the control block, so that the object's memory is released immediately once the strong count hits zero.

## Atomic Operations and Thread Safety of the Reference Count

`shared_ptr`'s reference count uses atomic operations to guarantee thread safety. This means that in a multithreaded environment, you can safely copy and destroy `shared_ptr`s themselves (the increments and decrements of the count are atomic), but **access to the managed object is not protected**—if multiple threads simultaneously read and write the object itself, you still need to do your own locking.

This is a common misconception: many people think `shared_ptr` provides "thread safety for the object," but in reality it only guarantees "thread safety for the reference count." We can use cppreference's description to be precise: a `shared_ptr`'s control block is thread-safe—multiple threads can simultaneously operate on different `shared_ptr` instances (even when they point to the same object) without external synchronization. But a single `shared_ptr` instance must not be read and written by multiple threads at the same time (that requires a lock). Concurrent access to the managed object must be made safe by yourself.

```cpp
#include <memory>
#include <thread>
#include <vector>
#include <iostream>

void demo_thread_safety() {
    auto data = std::make_shared<int>(0);

    // Multiple threads each hold their own copy of the shared_ptr — safe
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([data]() {  // Copying the shared_ptr atomically increments the count
            // Reading *data is safe (read-only)
            std::cout << "value: " << *data << "\n";

            // But multiple threads writing *data at the same time is a data race — lock it!
        });
    }

    for (auto& t : threads) t.join();
    std::cout << "final use_count: " << data.use_count() << "\n";  // Should be 1
}
```

From a performance perspective, every copy or destruction of a `shared_ptr` incurs an atomic operation (typically `fetch_add` or `fetch_sub`). Atomic operations are cheap on single-core systems (possibly just one special CPU instruction), but on multicore systems they incur cache-coherence protocol overhead (cache line bouncing). If your code frequently creates and destroys `shared_ptr`s (in a hot loop, say), this cost can become very significant.

The decrement path of the reference count deserves special attention. When `fetch_sub` returns 1 (meaning this is the last `shared_ptr`), the object must be destroyed. Mainstream implementations (such as GNU libstdc++) use `memory_order_acq_rel` to ensure that all previous writes are visible to the destruction code, and insert an `acquire` fence before destroying the object. These memory barriers are not expensive on x86 (x86 itself has a strong memory model), but on weakly ordered architectures such as ARM they may cause pipeline flushes.

## Analyzing shared_ptr's Performance Overhead

Let's do an intuitive comparison, putting the overheads of `shared_ptr`, `unique_ptr`, and raw pointers into one table:

| Dimension | Raw pointer | unique_ptr | shared_ptr |
|------|--------|------------|------------|
| Object size | 8B (64-bit) | 8B | 16B |
| Extra heap allocation | None | None | Control block (24-32B+) |
| Copy cost | 8B copy | Not copyable | Atomic fetch_add |
| Destruction cost | None | delete | Atomic fetch_sub + possible delete |
| Thread safety | None | None | Count is safe, object is not |

From this table you can clearly see that `shared_ptr` is heavier than `unique_ptr` on every single dimension. This does not mean `shared_ptr` is bad—in scenarios that call for shared ownership, it is the right design choice—but you should use it when shared ownership is genuinely needed, not "everywhere, for convenience."

In real projects, I have seen quite a few codebases manage almost every object with `shared_ptr`, and the result is reference counts flying everywhere, performance that cannot be optimized, and circular-reference problems cropping up constantly. The better approach is to settle ownership relationships at design time: manage most resources with `unique_ptr`, use `shared_ptr` only in the few places that genuinely need sharing, and pass non-owning access around through references (`T&`) or raw pointers (`T*`, holding no ownership).

## The Aliasing Constructor: A Powerful, Little-Known Feature

`shared_ptr` has a very powerful but little-known constructor called the **aliasing constructor**. Its signature is:

```cpp
template <typename U>
shared_ptr(const shared_ptr<U>& r, T* ptr) noexcept;
```

This constructor creates a new `shared_ptr` that shares `r`'s ownership (that is, its reference count is shared with `r`), but `get()` returns `ptr` instead of `r.get()`. Put simply: **it lets you hold "a part" of the same object, without having to manage that part's lifetime separately**.

The most common use is accessing an object's members:

```cpp
struct Config {
    std::string host;
    int port;
    std::string db_name;
};

auto config = std::make_shared<Config>();

// Get a shared_ptr pointing at config->host
// It shares config's reference count — as long as anyone holds host_ptr, config won't be destroyed
std::shared_ptr<std::string> host_ptr(config, &config->host);

// Use host_ptr in another component, with no need to know that Config exists
void connect(const std::shared_ptr<std::string>& host) {
    std::cout << "Connecting to " << *host << "\n";
}
```

This feature is especially useful when implementing "smart pointers to container elements"—say you want to return a `shared_ptr` pointing at a certain element of a `vector`, but you don't want the caller to hold a `shared_ptr` to the entire `vector`. Through the aliasing constructor, you can return a `shared_ptr` that exposes only the element's type, while underneath, the lifetime is still managed by the container's `shared_ptr`.

## enable_shared_from_this: Getting a shared_ptr Inside a Member Function

Sometimes, an object's member function needs to return a `shared_ptr` to itself. The most intuitive spelling, `shared_ptr(this)`, is a fatal mistake—it creates a new control block and gets the object deleted twice. The correct approach is to inherit from `std::enable_shared_from_this` and call `shared_from_this()`:

```cpp
#include <memory>
#include <iostream>
#include <functional>

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    explicit TcpSession(int fd) : fd_(fd) {
        std::cout << "Session created (fd=" << fd_ << ")\n";
    }
    ~TcpSession() {
        std::cout << "Session destroyed (fd=" << fd_ << ")\n";
    }

    void start_read() {
        // Async reads usually need to hold a shared_ptr to self, preventing destruction before the read completes
        auto self = shared_from_this();
        // async_read(socket_, buffer_, [self](error_code ec, size_t n) {
        //     self->on_read_complete(ec, n);
        // });
        std::cout << "Start reading (use_count="
                  << self.use_count() << ")\n";
    }

private:
    int fd_;
};

// Correct usage: the object must be held via a shared_ptr
void session_demo() {
    auto session = std::make_shared<TcpSession>(3);
    session->start_read();
}
```

Using `shared_from_this()` comes with one precondition: the object must already be managed by a `shared_ptr`. If you create the object on the stack or manage it with a raw pointer, calling `shared_from_this()` is undefined behavior. In addition, you must not call `shared_from_this()` inside the constructor—because at that point the `shared_ptr` has not finished constructing.

## Common Misuses and Pitfalls

Before diving into the embedded trade-offs, let's first go over a few common `shared_ptr` misuse patterns. I have stepped into these pits myself more than once, and I hope you, dear reader, can steer around them in advance.

**Misuse one: creating a second control block with `shared_ptr(this)`**. This is the deadliest mistake. If you write `return std::shared_ptr<Widget>(this)` inside a member function of an object already managed by a `shared_ptr`, the compiler creates a brand-new control block with the count starting at 1. The result is two independent control blocks managing the same object—when both `shared_ptr`s are destroyed, the object gets deleted twice. The correct approach is to inherit from `enable_shared_from_this` and call `shared_from_this()`.

**Misuse two: exposing `shared_ptr`'s ownership intent in an interface**. If you write a function `void process(std::shared_ptr<Widget> w)`, the signature itself implies "I want to share ownership with you." But very often the function just wants to use the object and doesn't need to hold it. In such scenarios, passing `const Widget&` or `Widget*` is more appropriate—no ownership implied, and no reference-counting overhead either.

**Misuse three: managing objects that "don't need to be shared" with `shared_ptr`**. Some teams, to save themselves the trouble, manage every heap object with `shared_ptr`—"shared_ptr can manage anything, after all." This leads to blurry ownership semantics (everyone holding it equals nobody being responsible), degraded performance (atomic operations everywhere), and increased risk of circular references. My rule of thumb from experience: **90% of objects should be managed with `unique_ptr`; only the 10% that genuinely need sharing get `shared_ptr`**.

**Misuse four: ignoring the difference between `make_shared` and `new`**. `make_shared` merges the object and the control block into a single allocation, but this also means the object's destruction and the control block's release do not happen at the same moment—when all `shared_ptr`s are destroyed, the object is destructed, but if any `weak_ptr` is still alive, the whole chunk of memory (including the space the object occupied) will not be released until all `weak_ptr`s are destroyed too. For large objects, this can cause the phenomenon of "clearly nobody is using it anymore, yet the memory is not given back." If you expect long-lived `weak_ptr`s, using `shared_ptr<T>(new T)` to allocate the object and the control block separately may be more appropriate.

## The Systemic Consequences of shared_ptr Abuse

I gave this topic its own dedicated section for one simple reason: I myself used to be one of the abusers...

Above we went through `shared_ptr`'s common misuse patterns one by one, but the severity of the problem goes far beyond "someone wrote it wrong in one place." When `shared_ptr` is abused systematically across a codebase, what it brings is **slow-acting poison at the architectural level**—not the acute kind of error that fails to compile, but a progressive rot that gradually makes the codebase unmaintainable, impossible to reason about, and impossible to optimize. I have seen more than one project sink into this swamp because "every object is managed with `shared_ptr`," and fixing it usually requires large-scale refactoring.

### The Collapse of the Ownership Model

In a healthy design, every object should have a clear owner—"who created it, who destroys it, who decides its lifetime"—questions that should be answered clearly at design time. But when you use `shared_ptr` everywhere, the answers to these questions become "who knows, it gets destroyed naturally when the reference count hits zero." That sounds convenient, but the price is losing control over the object's lifetime: you cannot guarantee that the object is alive at any particular moment (because other holders may release it at any time), nor can you guarantee that it is destroyed at any particular moment (because holders you don't know about may still be referencing it). This state of "nobody is responsible" is exactly the same problem brought on by a flood of global variables.

In his C++Now talk, Sean Parent incisively compared abusing `shared_ptr` to **implicit global variables**—any code holding a `shared_ptr` is participating in managing the object's lifetime, which is strikingly similar to global variables' property of "accessible from anywhere, keepable-alive from anywhere." The more practical problem is that once your public interface returns `shared_ptr<T>`, all callers are forced to use `shared_ptr`, even if they only want to borrow the object temporarily. You have stripped callers of the right to choose an ownership model—the better approach is to return `unique_ptr` (which callers can freely `std::move` into a `shared_ptr`) or a raw pointer/reference (non-owning access).

### Cache Line Contention Under Multithreading

This problem never appears at all in single-threaded code, but it becomes glaring in multithreaded scenarios. A `shared_ptr`'s control block stores the strong reference count and the weak reference count; these two atomic counters are usually in the same control block and very likely share the same cache line (typically 64 bytes). When multiple threads frequently copy and destroy `shared_ptr`s pointing at **the same object**, every atomic modification of the reference count by any thread makes that cache line bounce back and forth between cores—even if these threads are operating on their own independent `shared_ptr` instances, as long as they point to the same object, they contend for the same control block's cache line.

Words alone aren't enough, so let's run a test. The benchmark below builds a producer-consumer thread-safe queue and passes messages through it using raw pointers and `shared_ptr` respectively. The test environment was my Arch Linux under Windows WSL2, AMD Ryzen 7 5800H (14 threads), GCC 15.2, compiled with `-O2` Release. The results:

| Approach | Messages | Average time | Relative overhead |
|------|--------|---------|---------|
| Raw pointer | 10,000 | ~30 ms | Baseline |
| `shared_ptr` | 10,000 | ~35 ms | **+15-20%** |

The 15-20% overhead may be even more pronounced in real applications, because our test used a mutex-protected queue, and the mutex's overhead masks part of `shared_ptr`'s overhead. With a lock-free queue or under higher concurrency (8 threads in the original test, for example), `shared_ptr`'s overhead becomes much more visible. The source of this overhead is clear: every `shared_ptr` copy must atomically increment the reference count, and every destruction must atomically decrement it—when multiple threads simultaneously operate on the same control block, these atomic operations trigger cache line contention. It can be ignored in low-concurrency, low-throughput scenarios, but be very careful on high-concurrency hot paths.

### Circular References: The Silent Memory Leak

When objects leak because of circular references, you get no error message at all—the `shared_ptr` reference count never reaches zero, and the objects just lie quietly on the heap, occupying memory. No crash, no failed assertion, no log entry telling you "hey, this object leaked." You may only notice the problem when memory usage keeps growing, and only then can you use Valgrind or AddressSanitizer to locate the leak. Worse still, circular references are often not a simple loop between two objects, but a complex dependency graph involving many objects—A holds B, B holds C, and C holds A again—in which case tracing the reference chain is itself a very painful exercise.

By contrast, `unique_ptr`'s exclusive ownership model makes circular references impossible at compile time (you cannot construct a legal ring of exclusive ownership), which is its huge advantage at the design level. If you find yourself needing `weak_ptr` all over the place to break circular references, that in itself is a strong signal: your ownership model design has problems, and you should re-examine the dependencies between objects instead of patching things up with `weak_ptr` everywhere.

### Ownership Inversion: A Time Bomb in Callbacks

This problem is especially common in asynchronous programming, and the resulting bugs are extremely hard to track down. Suppose object A holds a Timer, and the Timer's callback captures A's `shared_ptr` via `shared_from_this()`. After A is reset on the main thread, the Timer thread instead becomes A's sole holder—A's lifetime has been "inverted" onto the Timer thread. If the Timer's destructor needs to join the very thread it is running on (`std::jthread` does exactly that), it triggers a `std::system_error`: a thread attempting to join itself, which is undefined behavior. The root cause of this kind of bug is that `shared_ptr` lets you "get lazy about ownership"—you think you've released A, but a callback is still secretly holding on to it. The correct approach is to make lifetime constraints explicit at design time: if A's destruction depends on the Timer thread ending, then A must be destroyed before the Timer, expressing that constraint with `unique_ptr`'s exclusive semantics.

### Uncertain Destruction Timing and Real-Time Hazards

When you drop a `shared_ptr`, you cannot know whether it is the last one—the object may be destroyed in this very drop, or it may keep living because other holders remain. This means the moment the destructor is called is **unpredictable**, and the order of destruction is **undefined** as well. In real-time systems this is especially dangerous: if you drop a `shared_ptr` on an audio callback, an interrupt service routine, or any code path with real-time requirements, and it happens to be the last holder, the destructor that fires may bring unacceptable latency—heap deallocation, file I/O, log writes: all of these are nondeterministic, time-consuming operations. Discussing C++ audio development, Timur Doumler proposed a clever `ReleasePool` scheme: periodically clean up the `shared_ptr`s that may need destruction on a low-priority thread, ensuring that destructors never fire on the real-time thread. But at the end of the day, if you had used `unique_ptr` plus explicit lifetime management at design time, you would not need this kind of workaround at all.

## A Practical Selection Guide: When to Use shared_ptr

Before talking about the embedded trade-offs, let's do a practice-oriented selection analysis. Many people hesitate between `unique_ptr` and `shared_ptr`, but the criterion is actually simple—ask yourself one question: **does this object need to be jointly owned by multiple independent modules?**

If the answer is "no"—the object's lifetime is decided by one clear "owner," and other modules merely borrow it temporarily—then use `unique_ptr` plus raw-pointer/reference passing. That covers the vast majority of scenarios.

If the answer is "yes"—multiple modules genuinely need to independently decide "I'm still using this object," and no single module can claim "I am the sole owner"—then use `shared_ptr`.

Typical scenarios where `shared_ptr` fits: shared modules in a plugin system (multiple components may depend on the same plugin instance at the same time, and none of them may unload it early), shared state in an asynchronous callback chain (multiple futures/callbacks need to keep the state alive until they themselves finish), and shared nodes in trees or graphs (multiple parent nodes referencing the same child node).

Typical scenarios where `shared_ptr` should not be used: passing function parameters (passing by reference is enough), an object's sole owner (use `unique_ptr`), and simple caches (observe with `weak_ptr`, hold with `shared_ptr`).

Let's look at a concrete design decision example—implementing a simple task scheduler:

```cpp
#include <memory>
#include <vector>
#include <functional>
#include <iostream>

class Task {
public:
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual std::string name() const = 0;
};

class PrintTask : public Task {
public:
    explicit PrintTask(std::string msg) : msg_(std::move(msg)) {}
    void execute() override { std::cout << msg_ << "\n"; }
    std::string name() const override { return "PrintTask"; }
private:
    std::string msg_;
};

class TaskScheduler {
public:
    // The scheduler owns the tasks — unique_ptr is enough
    void submit(std::unique_ptr<Task> task) {
        std::cout << "提交任务: " << task->name() << "\n";
        tasks_.push_back(std::move(task));
    }

    void run_all() {
        for (auto& task : tasks_) {
            task->execute();
        }
        tasks_.clear();
    }

private:
    std::vector<std::unique_ptr<Task>> tasks_;
};

// If tasks need to be shared by multiple schedulers — only then is shared_ptr needed
class SharedTaskScheduler {
public:
    void submit(std::shared_ptr<Task> task) {
        tasks_.push_back(std::move(task));
    }

    std::shared_ptr<Task> get_task(size_t index) {
        if (index < tasks_.size()) return tasks_[index];
        return nullptr;
    }

private:
    std::vector<std::shared_ptr<Task>> tasks_;
};
```

The first version uses `unique_ptr`—once a task is submitted, ownership belongs to the scheduler; simple and clear. The second version uses `shared_ptr`—allowing multiple schedulers or external code to hold references to the same task, with the task destroyed only when the last holder goes away. Which one to choose depends on your design requirements, not on "which one is more convenient."

## Embedded Trade-offs: Memory Overhead and ISR Caveats

Using `shared_ptr` in embedded scenarios calls for extra caution, and we'll analyze the reasons one by one.

First is **memory overhead**. On a 32-bit MCU, a `shared_ptr` object takes 8 bytes (two pointers), and the control block at least 16-24 bytes (depending on the implementation). If you use `make_shared`, the object and the control block together may take up `sizeof(T) + 24+` bytes. For an MCU with only a few dozen KB of RAM, this overhead becomes very noticeable when there are many objects. Let's do the concrete math: suppose your MCU has 64KB of RAM and you need to manage 50 peripheral handles, each handle object itself 16 bytes. Managed with `unique_ptr`, the total overhead is `50 * (8 + 16) = 1200` bytes; managed with `shared_ptr` + `make_shared`, the total is `50 * (16 + 16 + 24) = 2800` bytes—an extra 1600 bytes, 2.4% of total RAM. On a more memory-constrained MCU (the STM32F103 has only 20KB of RAM), this number becomes even more glaring.

Second is **heap allocation**. The control block must be allocated on the heap, but many embedded systems either disable the heap or have very limited heap space. Frequent heap allocations lead to memory fragmentation and, eventually, allocation failure. If your system runs for a long time (embedded devices typically run for years on end), the fragmentation problem gets worse and worse. One possible mitigation is to use `std::allocate_shared` together with a custom allocator (a memory-pool allocator, for example), moving the control block's allocation off the system heap and into a pre-allocated memory pool.

Third is **atomic operations**. The atomic increment/decrement of the reference count may degrade into interrupt-disabling operations on single-core MCUs (depending on the toolchain's implementation of `std::atomic`), which affects interrupt response time. Using `shared_ptr` in an ISR is a bad idea—not only because of heap operations, but also because the atomic operations may disable interrupts. If your system has strict real-time requirements (say, a control loop that must complete within 100us), any nondeterministic delay in an ISR is unacceptable.

My advice: in embedded systems, prefer `unique_ptr`, or use plain RAII wrapper classes directly. If you truly need shared semantics, consider intrusive reference counting—putting the reference count inside the object itself and avoiding the extra heap allocation. In a single-threaded environment, the intrusive scheme's reference count can be a plain `uint32_t`, no atomic operations needed, with extremely low overhead. We'll discuss this topic in detail in the "Custom Deleters and Intrusive Reference Counting" article.

Next up is `weak_ptr`—`shared_ptr`'s partner, dedicated to solving the thorny problem of circular references.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [Inside STL: The different types of shared pointer control blocks](https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626)
- [std::shared_ptr thread safety](https://stackoverflow.com/questions/9127816/stdshared-ptr-thread-safety)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
