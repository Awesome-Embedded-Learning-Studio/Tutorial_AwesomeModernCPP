---
title: 'A Detailed Look at shared_ptr: Shared Ownership and Reference Counting'
description: Understanding the control block mechanism, thread safety, and performance
  characteristics of shared pointers
chapter: 1
order: 3
tags:
- host
- cpp-modern
- intermediate
- shared_ptr
- 智能指针
- 引用计数
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 20
prerequisites:
- 'Chapter 1: RAII 深入理解'
- 'Chapter 1: unique_ptr 详解'
related:
- weak_ptr 与循环引用
- 自定义删除器
translation:
  source: documents/vol2-modern-features/ch01-smart-pointers/03-shared-ptr.md
  source_hash: a799d925944b2310bf0dc27f58be8797d7ae93684134663a67d3d65f21ff1ffc
  translated_at: '2026-05-26T11:21:17.720531+00:00'
  engine: anthropic
  token_count: 4052
---
# A Deep Dive into shared_ptr: Shared Ownership and Reference Counting

In the previous article, we discussed `unique_ptr`—a zero-overhead smart pointer with exclusive ownership. But in the real world, resources aren't always "exclusively owned." Sometimes, an object genuinely needs to be held and managed jointly by multiple modules—such as a configuration object read by multiple subsystems, a network connection shared across tasks, or a cache entry accessed by multiple consumers. In these cases, the "exclusive" semantics of `unique_ptr` fall short.

`std::shared_ptr` is designed for exactly this scenario. Its core idea is **reference counting**: each additional `shared_ptr` pointing to the object increments the count; each destruction decrements it; when the count reaches zero, the object is automatically destroyed. It sounds simple and elegant, but the underlying implementation details—control blocks, atomic operations, and memory allocation strategies—are far more complex than one might imagine.

## Shared Ownership: Semantics and Costs

`shared_ptr` expresses "shared ownership" semantics: multiple `shared_ptr` instances can point to the same object, and they jointly determine its lifetime. The object is only deleted when the very last `shared_ptr` is destroyed.

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
        auto conn2 = conn;  // 引用计数: 1 → 2
        conn2->send("hello from conn2");
        std::cout << "use_count: " << conn.use_count() << "\n";  // 2
    }   // conn2 离开作用域，引用计数: 2 → 1

    conn->send("hello from conn");
    std::cout << "use_count: " << conn.use_count() << "\n";  // 1
}   // conn 离开作用域，引用计数: 1 → 0，Connection 被销毁
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

This looks great. But shared ownership isn't free—every copy and destruction of a `shared_ptr` requires updating the reference count, and this count must be thread-safe (using atomic operations). Furthermore, `shared_ptr` internally maintains a control block to store the reference count and other metadata. These overheads become very noticeable in scenarios where `shared_ptr` instances are frequently created and destroyed.

Our recommendation is to use `unique_ptr` whenever possible, and only resort to `shared_ptr` when shared ownership is genuinely needed. `shared_ptr` should not become an excuse for being "too lazy to think about ownership."

## The Control Block: Internal Structure of shared_ptr

To understand the performance characteristics of `shared_ptr`, we must first understand its internal structure. A `shared_ptr` actually contains two pointers: one to the managed object, and another to the control block.

The control block is a heap-allocated data structure containing the strong reference count (the number of `shared_ptr` instances), the weak reference count (the number of `weak_ptr` instances), a custom deleter (if any), and a custom allocator (if any). When you create a `shared_ptr` using `std::make_shared`, the object and the control block are placed in the same memory block (a single allocation); when created using `std::shared_ptr<T>(new T)`, the object and the control block are two separate allocations.

Let's use a simplified diagram to understand this:

![shared_ptr internal structure diagram](./03-shared-ptr-structure.drawio)

So a `shared_ptr` object itself is `2 * sizeof(void*)` in size—two pointers. On a 64-bit system, that's 16 bytes, exactly twice the size of a `unique_ptr` (8 bytes). The size of the control block itself depends on the implementation (GNU libstdc++ on x86_64 is approximately 32 bytes).

## The Advantage of make_shared: Single Allocation

As mentioned earlier, `make_shared` places the object and the control block in a single contiguous memory block. This brings three significant benefits.

First, **fewer heap allocations**—reduced from two to one. In performance-sensitive code, heap allocation is an expensive operation (typically involving locks, traversing free lists, etc.), so reducing the number of allocations is always a win. You can verify through `code/volumn_codes/vol2/ch01-smart-pointers/verify_shared_ptr_layout.cpp` that `make_shared` indeed performs only one allocation.

Second, **better cache locality**. Because the object and the control block are in the same memory block, a CPU cache line might hit both simultaneously. With two separate allocations, the memory blocks could be physically far apart, leading to more cache misses.

Third, **less memory fragmentation**. One allocation means one deallocation, rather than two separate deallocations at different locations.

```cpp
// 推荐：单次分配
auto p1 = std::make_shared<Connection>("10.0.0.1:9090");

// 不推荐：两次分配，且不如 make_shared 异常安全
auto p2 = std::shared_ptr<Connection>(new Connection("10.0.0.1:9090"));

// 大小对比
std::cout << "sizeof(shared_ptr): " << sizeof(p1) << "\n";  // 16 (64-bit)
std::cout << "sizeof(unique_ptr): " << sizeof(std::unique_ptr<Connection>) << "\n";  // 8
```

⚠️ `make_shared` also has a lesser-known drawback: because the object and the control block share the same memory block, when all `shared_ptr` instances are destroyed (strong reference count reaches zero), the object is destructed, but the control block's memory is not immediately freed—the entire memory block is only reclaimed when all `weak_ptr` instances are also destroyed (weak reference count reaches zero). If the object is large and `weak_ptr` instances are still alive, this can result in higher memory usage than expected. If you anticipate `weak_ptr` instances living for a long time, consider using `std::shared_ptr<T>(new T)` to allocate the object's memory independently from the control block, so that the object's memory can be freed immediately when the strong reference count reaches zero.

## Atomic Operations on Reference Counts and Thread Safety

`shared_ptr` uses atomic operations for its reference count to ensure thread safety. This means that in a multithreaded environment, you can safely copy and destroy the `shared_ptr` instances themselves (the incrementing and decrementing of the reference count is atomic), but **access to the managed object is not protected**—if multiple threads simultaneously read and write to the object itself, you still need to provide your own locking.

This is a common misconception: many people think that `shared_ptr` provides "thread safety for the object," but it actually only guarantees "thread safety for the reference count." We can use cppreference's description to understand this precisely: the control block of a `shared_ptr` is thread-safe—multiple threads can simultaneously operate on different `shared_ptr` instances (even if they point to the same object) without external synchronization. However, the same `shared_ptr` instance cannot be read and written simultaneously by multiple threads (locking is required). Concurrent access to the managed object must be made safe by you.

```cpp
#include <memory>
#include <thread>
#include <vector>
#include <iostream>

void demo_thread_safety() {
    auto data = std::make_shared<int>(0);

    // 多个线程各自持有 shared_ptr 的拷贝——安全
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([data]() {  // 拷贝 shared_ptr，引用计数原子递增
            // 读取 *data 是安全的（只读）
            std::cout << "value: " << *data << "\n";

            // 但如果多个线程同时写 *data，就是数据竞争——需要加锁！
        });
    }

    for (auto& t : threads) t.join();
    std::cout << "final use_count: " << data.use_count() << "\n";  // 应该是 1
}
```

From a performance perspective, every copy or destruction of a `shared_ptr` incurs an atomic operation (typically `fetch_add` or `fetch_sub`). On a single-core system, the overhead of atomic operations is small (it might just be a special CPU instruction), but on multi-core systems, it triggers cache coherence protocol overhead (cache line bouncing). If your code frequently creates and destroys `shared_ptr` instances (for example, in a hot loop), this overhead can become very significant. You can verify the overhead difference between single-threaded and multi-threaded scenarios through `code/volumn_codes/vol2/ch01-smart-pointers/verify_shared_ptr_performance.cpp`.

The logic when decrementing the reference count is particularly worth noting. When `fetch_sub` returns 1 (meaning this is the last `shared_ptr`), the object needs to be destroyed. Mainstream implementations (like GNU libstdc++) use `memory_order_acq_rel` to ensure that all previous write operations are visible to the destruction code, and insert an `acquire` fence before destruction. These memory barriers have little overhead on x86 (x86 inherently has strong memory ordering), but on weakly-ordered architectures like ARM, they can cause pipeline flushes.

## Performance Overhead Analysis of shared_ptr

Let's do an intuitive comparison, putting the overheads of `shared_ptr`, `unique_ptr`, and raw pointers into one table:

| Dimension | Raw Pointer | unique_ptr | shared_ptr |
|-----------|-------------|------------|------------|
| Object size | 8B (64-bit) | 8B | 16B |
| Extra heap allocation | None | None | Control block (24-32B+) |
| Copy overhead | 8B copy | Not copyable | Atomic fetch_add |
| Destruction overhead | None | delete | Atomic fetch_sub + possible delete |
| Thread safety | None | None | Reference count safe, object unsafe |

From this table, we can clearly see that `shared_ptr` is heavier than `unique_ptr` in every dimension. This isn't to say that `shared_ptr` is bad—it's the correct design choice in scenarios requiring shared ownership—but you should use it only when shared ownership is genuinely needed, rather than "using `shared_ptr` everywhere for convenience."

In real-world projects, we've seen codebases that manage almost all objects with `shared_ptr`, resulting in reference counts flying everywhere, unoptimizable performance, and frequent circular reference issues. A better approach is to clarify ownership relationships during the design phase: manage most resources with `unique_ptr`, use `shared_ptr` only in the few places where sharing is truly needed, and pass non-owning access via references (`T&`) or raw pointers (`T*`, which don't hold ownership).

## Aliasing Constructor: A Powerful, Lesser-Known Feature

`shared_ptr` has a very powerful but lesser-known constructor called the **aliasing constructor**. Its signature is:

```cpp
template <typename U>
shared_ptr(const shared_ptr<U>& r, T* ptr) noexcept;
```

This constructor creates a new `shared_ptr` that shares the ownership of `r` (i.e., its reference count is shared with `r`), but `get()` returns `ptr` instead of `r.get()`. Simply put: **it lets you hold a "part" of the same object without needing to manage that part's lifetime separately.**

The most common use case is accessing a member of an object:

```cpp
struct Config {
    std::string host;
    int port;
    std::string db_name;
};

auto config = std::make_shared<Config>();

// 获取一个指向 config->host 的 shared_ptr
// 它共享 config 的引用计数——只要有人持有 host_ptr，config 就不会被销毁
std::shared_ptr<std::string> host_ptr(config, &config->host);

// 在另一个组件中使用 host_ptr，不需要知道 Config 的存在
void connect(const std::shared_ptr<std::string>& host) {
    std::cout << "Connecting to " << *host << "\n";
}
```

This feature is particularly useful when implementing "smart pointers to container elements"—for example, if you want to return a `shared_ptr` pointing to a specific element in a `vector`, but you don't want the caller to hold a `shared_ptr` to the entire `vector`. Through the aliasing constructor, you can return a `shared_ptr` that only exposes the element type, while the underlying lifetime is still managed by the container's `shared_ptr`.

## enable_shared_from_this: Obtaining a shared_ptr in Member Functions

Sometimes, an object's member function needs to return a `shared_ptr` pointing to itself. The most intuitive approach, `shared_ptr(this)`, is a fatal error—it creates a new control block, causing the object to be deleted twice. The correct approach is to inherit from `std::enable_shared_from_this` and call `shared_from_this()`:

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
        // 异步读取通常需要持有自身的 shared_ptr，防止在读完成前被销毁
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

// 正确用法：必须通过 shared_ptr 持有
void session_demo() {
    auto session = std::make_shared<TcpSession>(3);
    session->start_read();
}
```

⚠️ Using `enable_shared_from_this` has a prerequisite: the object must already be managed by a `shared_ptr`. If you create the object on the stack or manage it with a raw pointer, calling `shared_from_this()` leads to undefined behavior. Additionally, you cannot call `shared_from_this()` in the constructor—because at that point, the `shared_ptr` has not finished being constructed.

## Common Misuses and Pitfalls

Before diving into embedded trade-offs, let's inventory a few common misuse patterns of `shared_ptr`. We've fallen into these "pitfalls" more than once ourselves, and we hope readers can avoid them in advance.

**Misuse 1: Creating a second control block with `shared_ptr(this)`**. This is the most fatal error. If you write `return std::shared_ptr<Widget>(this)` in a member function of an object already managed by a `shared_ptr`, the compiler creates a brand-new control block with a reference count starting at 1. The result is two independent control blocks managing the same object—when both `shared_ptr` instances are destroyed, the object gets deleted twice. The correct approach is to inherit from `enable_shared_from_this` and call `shared_from_this()`.

**Misuse 2: Exposing ownership intent with `shared_ptr` in interfaces**. If you write a function `void process(std::shared_ptr<Widget> w)`, the signature itself implies "I want to share ownership with you." But often, the function just wants to use the object without needing to hold it. In this scenario, passing `const Widget&` or `Widget*` is more appropriate—it doesn't imply ownership, and it avoids the overhead of reference counting.

**Misuse 3: Using `shared_ptr` to manage objects that "don't need sharing"**. Some teams use `shared_ptr` to manage all heap objects just to save effort—"shared_ptr can handle anything anyway." This leads to blurred ownership semantics (everyone holds it means no one is responsible), degraded performance (atomic operations everywhere), and increased risk of circular references. Our experience is: **90% of objects should be managed by `unique_ptr`, and only the 10% that truly need sharing should use `shared_ptr`**.

**Misuse 4: Ignoring the difference between `make_shared` and `new`**. `make_shared` merges the object and the control block into a single allocation, but this also means the object's destruction and the control block's release don't happen at the same time—when all `shared_ptr` instances are destroyed, the object is destructed, but if `weak_ptr` instances are still alive, the entire memory block (including the space occupied by the object) won't be freed until all `weak_ptr` instances are also destroyed. For large objects, this can lead to a situation where "no one is using it, but the memory isn't returned." If you expect `weak_ptr` instances to live for a long time, using `shared_ptr<T>(new T)` to separate the object and control block allocations might be more appropriate.

## Systemic Consequences of shared_ptr Abuse

We're dedicating a separate section to this simply because we ourselves used to be abusers...

Earlier, we went through common misuse patterns of `shared_ptr` one by one, but the severity of the problem goes far beyond "a mistake somewhere." When `shared_ptr` is systematically abused in a codebase, it brings a **chronic poison at the architectural level**—not the kind of acute error that fails to compile, but a progressive decay that makes the codebase gradually unmaintainable, unreasonably complex, and unoptimizable. We've seen more than one project fall into this quagmire because "all objects are managed by `shared_ptr`," and fixing it often requires large-scale refactoring.

### Collapse of the Ownership Model

In a healthy design, every object should have a clear owner—"who created it, who destroys it, whose responsibility is the lifetime"—these questions should be answered clearly during the design phase. But when you use `shared_ptr` everywhere, the answer to these questions becomes "who knows, it'll naturally be destroyed when the reference count reaches zero." It sounds convenient, but the price is that you lose control over the object's lifetime: you can't guarantee the object is alive at any specific moment (because other holders might release it at any time), and you can't guarantee the object is destroyed at any specific moment (because unknown holders might still be referencing it). This "nobody is responsible" state is exactly the same problem caused by an overabundance of global variables.

In his C++Now talk, Sean Parent aptly compared abusing `shared_ptr` to **implicit global variables**—any code holding a `shared_ptr` participates in the object's lifetime management, which is strikingly similar to the characteristic of global variables where "anywhere can access it, anywhere can extend its lifetime." A more practical problem is that once your public interface returns a `shared_ptr<T>`, all callers are forced to use `shared_ptr`, even if they just want to temporarily borrow the object. You deprive callers of the right to choose their ownership model—a better approach is to return `unique_ptr` (callers can freely `std::move` it into a `shared_ptr`) or a raw pointer/reference (for non-owning access).

### Cache Line Contention Under Multithreading

This problem doesn't appear at all in single-threaded code, but it becomes very glaring in multithreaded scenarios. The control block of a `shared_ptr` stores the strong reference count and the weak reference count. These two atomic counters are in the same control block and likely share the same cache line (typically 64 bytes). When multiple threads frequently copy and destroy `shared_ptr` instances pointing to the **same object**, every atomic modification to the reference count by each thread causes that cache line to bounce back and forth between different cores—even if these threads are operating on their own independent `shared_ptr` instances, as long as they point to the same object, they compete for the same control block's cache line.

Talking isn't enough; let's run a test. The following benchmark program (`code/volumn_codes/vol2/ch01-smart-pointers/verify_cache_contention.cpp`) builds a thread-safe producer-consumer queue, passing messages via raw pointers and `shared_ptr` respectively. The test environment is our Windows WSL2 Arch Linux, AMD Ryzen 7 5800H (14 threads), GCC 15.2, compiled with `-O2` in Release mode. The results are as follows:

| Approach | Messages | Average Time | Relative Overhead |
|----------|----------|--------------|-------------------|
| Raw pointer | 10,000 | ~30 ms | Baseline |
| `shared_ptr` | 10,000 | ~35 ms | **+15-20%** |

The 15-20% overhead might be even more significant in real-world applications, because our test used a mutex-protected queue, and the mutex overhead masks some of the `shared_ptr` overhead. In lock-free queues or higher-concurrency scenarios (like the 8-thread setup in the original test), the overhead of `shared_ptr` becomes even more pronounced. The source of this overhead is clear: every `shared_ptr` copy atomically increments the reference count, and every destruction atomically decrements it—in scenarios where multiple threads simultaneously operate on the same control block, these atomic operations trigger cache line contention. This can be ignored in low-concurrency, low-throughput scenarios, but must be carefully considered on high-concurrency hot paths.

### Circular References: Silent Memory Leaks

When an object leaks due to a circular reference, you won't get any error messages—the reference count of the `shared_ptr` will never reach zero, and the object just quietly sits on the heap occupying memory. No crashes, no assertion failures, no logs telling you "hey, this object leaked." You might only notice the problem when memory usage keeps growing, and then you need tools like Valgrind or AddressSanitizer to pinpoint the leak. What's worse is that circular references are often not simple loops between two objects, but complex dependency graphs involving multiple objects—A holds B, B holds C, and C holds A—tracking the reference chain in such cases is a very painful endeavor.

In contrast, the exclusive ownership model of `unique_ptr` makes circular references impossible at compile time (you cannot construct a valid exclusive ownership cycle), which is a huge advantage at the design level. If you find yourself needing to extensively use `weak_ptr` to break circular references, that in itself is a strong signal: there's a problem with your ownership model design, and you should re-examine the dependencies between objects rather than patching things up everywhere with `weak_ptr`.

### Ownership Inversion: A Ticking Time Bomb in Callbacks

This problem is particularly common in asynchronous programming, and the bugs it causes are extremely difficult to track down. Suppose object A holds a Timer, and the Timer's callback captures A's `shared_ptr` via `shared_from_this()`. When A is reset on the main thread, the Timer thread ironically becomes A's sole holder—A's lifetime gets "inverted" onto the Timer thread. If the Timer's destructor needs to join its own thread (`std::jthread` will do exactly this), it triggers a `std::system_error`: a thread attempting to join itself, which is undefined behavior. The root cause of this type of bug is that `shared_ptr` lets you "be too lazy to think about ownership"—you thought you released A, but the callback is still secretly holding onto it in the shadows. The correct approach is to clarify lifetime constraints during the design phase: if A's destruction depends on the Timer thread finishing, then A must be destroyed before the Timer, using `unique_ptr`'s exclusive semantics to express this constraint.

### Uncertain Destruction Timing and Real-Time Hazards

When you drop a `shared_ptr`, you can't be sure whether it's the last one—the object might be destroyed in this drop, or it might continue living because other holders still exist. This means the timing of the destructor call is **unpredictable**, and the destruction order is **undefined**. In real-time systems, this is especially dangerous: if you drop a `shared_ptr` in an audio callback, an interrupt service routine, or any code path with real-time requirements, and it happens to be the last holder, the triggered destructor could bring unacceptable latency—heap deallocation, file I/O, log writing, these are all non-deterministic, time-consuming operations. Timur Doumler proposed a clever `ReleasePool` approach when discussing C++ audio development: periodically clean up `shared_ptr` instances that might need destruction on a low-priority thread, ensuring that destructors are never triggered on real-time threads. But ultimately, if you had used `unique_ptr` with explicit lifetime management during the design phase, you wouldn't need this workaround at all.

## Practical Selection Guide: When to Use shared_ptr

Before discussing embedded trade-offs, let's do a practical, decision-oriented analysis. Many people hesitate between `unique_ptr` and `shared_ptr`, but the judgment criterion is simple—ask yourself one question: **Does this object need to be jointly owned by multiple independent modules?**

If the answer is "no"—the object's lifetime is determined by one clear "owner," and other modules just temporarily borrow it—then use `unique_ptr` + raw pointer/reference passing. This covers the vast majority of scenarios.

If the answer is "yes"—multiple modules genuinely need to independently decide "I'm still using this object," and no single module can claim "I'm the sole owner"—then use `shared_ptr`.

Typical use cases for `shared_ptr` include: shared modules in a plugin system (multiple components might depend on the same plugin instance simultaneously, and none can unload it prematurely), shared state in asynchronous callback chains (multiple futures/callbacks need to keep the state alive until they complete), and shared nodes in trees or graphs (multiple parent nodes referencing the same child node).

Typical scenarios where you should not use `shared_ptr` include: passing function parameters (passing by reference is enough), sole owners of objects (use `unique_ptr`), and simple caches (use `weak_ptr` to observe, `shared_ptr` to hold).

Let's look at a specific design decision example—implementing a simple task scheduler:

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
    // 调度器持有任务的所有权——用 unique_ptr 足够
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

// 如果任务需要被多个调度器共享——这时才需要 shared_ptr
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

The first version uses `unique_ptr`—after a task is submitted, ownership belongs to the scheduler, simple and clear. The second version uses `shared_ptr`—allowing multiple schedulers or external code to hold a reference to the same task, and the task is only destroyed when the last holder goes away. Which one to choose depends on your design needs, not "which one is more convenient."

## Embedded Trade-offs: Memory Overhead and ISR Considerations

Using `shared_ptr` in embedded scenarios requires extra caution. Let's analyze the reasons one by one.

First is the **memory overhead**. On a 32-bit MCU, a `shared_ptr` object takes up 8 bytes (two pointers), and the control block takes at least 16-24 bytes (depending on the implementation). If you use `make_shared`, the object and the control block together might occupy `sizeof(T) + 24+` bytes. For an MCU with only a few tens of KB of RAM, this overhead becomes very noticeable when the number of objects is large. Let's do the specific math: suppose your MCU has 64KB of RAM, and you need to manage 50 peripheral handles, each handle object being 16 bytes itself. Managing them with `unique_ptr` costs a total of `50 * (8 + 16) = 1200` bytes; managing them with `shared_ptr` + `make_shared` costs a total of `50 * (16 + 16 + 24) = 2800` bytes—that's 1,600 extra bytes, accounting for 2.4% of the total RAM. On MCUs with even tighter memory (like the STM32F103 with only 20KB of RAM), this number becomes even more glaring.

Second is **heap allocation**. The control block needs to be allocated on the heap, and many embedded systems either have the heap disabled or have very limited heap space. Frequent heap allocation leads to memory fragmentation, ultimately causing allocation failures. If your system runs for a long time (embedded devices typically run year-round), the fragmentation problem will only get worse. One possible mitigation is to use `std::allocate_shared` with a custom allocator (such as a memory pool allocator), moving the control block's allocation from the system heap to a pre-allocated memory pool.

Third is **atomic operations**. The atomic increment/decrement of the reference count on a single-core MCU might degrade into disabling interrupts (depending on the toolchain's implementation of `std::atomic`), which affects interrupt response times. Using `shared_ptr` in an ISR is a terrible idea—not only because of heap operations, but also because atomic operations might disable interrupts. If your system has strict real-time requirements (for example, a control loop must complete within 100us), any indeterminate delay in an ISR is unacceptable.

Our recommendation is to prioritize `unique_ptr` or directly use RAII wrapper classes in embedded systems. If shared semantics are truly needed, consider intrusive reference counting—placing the reference count inside the object itself to avoid extra heap allocations. In a single-threaded environment, the reference count in an intrusive solution can be a plain `uint32_t`, requiring no atomic operations and having extremely low overhead. We will discuss this topic in detail in the article on "Custom Deleters and Intrusive Reference Counting."

## Summary

`shared_ptr` implements shared ownership semantics through reference counting, complementing the exclusive semantics of `unique_ptr`. The key to understanding it lies in the control block mechanism—each `shared_ptr` instance holds two pointers (to the object and to the control block), and the atomic reference count in the control block guarantees safety under multithreading, but it also brings non-negligible performance overhead.

`make_shared` optimizes performance and memory locality through a single allocation, and should be the preferred way to create `shared_ptr`. The aliasing constructor and `enable_shared_from_this` are two lesser-known but highly useful advanced features. In embedded scenarios, the memory overhead, heap allocation, and atomic operation costs of `shared_ptr` need to be carefully weighed—in most cases, `unique_ptr` or intrusive solutions are better choices.

In the next article, we will discuss `weak_ptr`—the partner of `shared_ptr`, specifically designed to solve the thorny problem of circular references.

## References

- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::make_shared](https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared)
- [Inside STL: The different types of shared pointer control blocks](https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626)
- [std::shared_ptr thread safety](https://stackoverflow.com/questions/9127816/stdshared-ptr-thread-safety)
- [C++ Core Guidelines: R.20-24](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rr-smart)
