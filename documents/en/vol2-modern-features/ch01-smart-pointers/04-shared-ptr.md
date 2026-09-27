---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Understand shared_ptr's control block mechanics, thread safety, and performance characteristics
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 1: Deep Dive into RAII: The Cornerstone of Resource Management'
- 'Chapter 1: Deep Dive into unique_ptr: The Zero-Overhead Smart Pointer with Exclusive Ownership'
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
  source: documents/vol2-modern-features/ch01-smart-pointers/04-shared-ptr.md
  source_hash: 22976b99ba622871dab185d440dfc152290c26976cae2f9316b23ab51e478eed
  translated_at: '2026-09-27T04:54:20+00:00'
  engine: anthropic
  token_count: 4700
---
# Deep Dive into shared_ptr: Shared Ownership and Reference Counting

In the previous article we talked about `unique_ptr`, the zero-overhead smart pointer with exclusive ownership. But real-world resources aren't always owned by exactly one master. Sometimes an object genuinely needs to be held and managed jointly by multiple modules — a configuration object read by several subsystems, a network connection shared among tasks, a cache entry accessed by many consumers. In cases like these, the "exclusive" semantics of `unique_ptr` simply aren't enough.

`std::shared_ptr` is designed for exactly these scenarios. Its core idea is **reference counting**: every additional `shared_ptr` pointing at the object bumps the count up by one, every one that goes away brings it down by one, and when the count hits zero the object is destroyed automatically. It sounds simple and elegant, but the implementation details underneath — control blocks, atomic operations, memory allocation strategy — are far more intricate than you'd expect.

## Shared Ownership: Semantics and Costs

What `shared_ptr` expresses is "shared ownership" semantics: multiple `shared_ptr`s can point to the same object, and together they decide the object's lifetime. Only when the last `shared_ptr` is destroyed does the object get deleted.

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

That demo program is right below — click "Try It Yourself" and run it directly:

<OnlineCompilerDemo
  title="Hands-On: Watching the Reference Count Rise and Fall"
  source-path="code/examples/vol2/26_shared_ptr_refcount.cpp"
  description="Watch use_count change live: copying it inside the inner scope brings it to 2, leaving the scope drops it back to 1, and Connection doesn't disconnect until the last copy is destroyed."
  run-options="-std=c++17"
  allow-run
/>

We turned this rise and fall of the counter into an animation — you can play it, pause it, or single-step through it to see every increment and decrement of use_count in full detail:

<Anim id="shared-refcount" />

It all looks lovely. But shared ownership isn't free — every copy and every destruction of a `shared_ptr` has to update the reference count, and that count must be thread-safe (atomic operations). On top of that, a `shared_ptr` internally maintains a control block that stores the reference count and other metadata. In scenarios where `shared_ptr`s are created and destroyed frequently, these costs become very visible.

My recommendation: use `unique_ptr` whenever you can, and reach for `shared_ptr` only when you genuinely need shared ownership<RefLink :id="1" preview="C++ Core Guidelines R.21 — Prefer unique_ptr over shared_ptr unless you need to share ownership" />. `shared_ptr` should never become an excuse for being too lazy to think about ownership.

## The Control Block: Inside shared_ptr

To understand `shared_ptr`'s performance profile, you first have to understand its internal structure. A `shared_ptr` actually holds two pointers: one to the managed object, and another that **points to the control block**.

Some of you might be puzzled. Huh? Why does this little `shared_ptr` get to charge me rent on memory? The answer is that it has to house the strong reference count (the number of `shared_ptr`s), the weak reference count (the number of `weak_ptr`s), the custom deleter (if there is one), and the custom allocator (if there is one). All of that typically has to persist for the lifetime of a `shared_ptr` — which naturally makes it a heap-allocated data structure. (Which is why I nearly got torn apart once for using one on an extremely hot path = =.)

So when you create a `shared_ptr` with `std::make_shared`, the object and the control block land in the same chunk of memory (one allocation); when you create it with `std::shared_ptr<T>(new T)`, the object and the control block are two separate allocations<RefLink :id="2" preview="Raymond Chen, Inside STL: separate vs combined control block, The Old New Thing, 2023" />.

Let's build our intuition with a simplified diagram:

![Simplified diagram of shared_ptr's internal structure](./04-shared-ptr-structure.drawio)

Seen this way, a `shared_ptr` object itself is `2 * sizeof(void*)` — two pointers. On a 64-bit system that's 16 bytes, twice the size of a `unique_ptr` (8 bytes). The control block's own size depends on the implementation (on x86_64 with GNU libstdc++, the control block header is 16 bytes — one vptr plus two 4-byte counters — and then it stores a pointer to the object; measured with GCC 16.2, the plainest control block for `shared_ptr(new T)` is 24 bytes as a whole).

## The Advantages of make_shared: One Allocation

As mentioned above, `make_shared` puts the object and the control block into one contiguous chunk of memory. That brings three notable benefits.

First, **fewer heap allocations** — down from two to one. In performance-sensitive code, heap allocation is expensive (it usually involves locks, free-list traversal, and the like), so cutting the number of allocations is always good. Write a tiny program that prints allocation addresses and you can watch `make_shared` `new` only once<RefLink :id="3" preview="cppreference make_shared — typically performs only one allocation for object and control block" />.

Second, **better cache locality**. With the object and the control block in the same chunk, a single CPU cache line may cover both. Two independently allocated chunks can end up physically far apart, causing more cache misses.

Third, **less memory fragmentation**. One allocation means one deallocation, rather than releasing memory at two separate locations.

```cpp
// Recommended: single allocation
auto p1 = std::make_shared<Connection>("10.0.0.1:9090");

// Not recommended: two allocations, and less exception-safe than make_shared
auto p2 = std::shared_ptr<Connection>(new Connection("10.0.0.1:9090"));

// Size comparison
std::cout << "sizeof(shared_ptr): " << sizeof(p1) << "\n";  // 16 (64-bit)
std::cout << "sizeof(unique_ptr): " << sizeof(std::unique_ptr<Connection>) << "\n";  // 8
```

`make_shared` also has a lesser-known downside: because the object and the control block share one chunk of memory, when all `shared_ptr`s are destroyed (the strong count hits zero) the object is destructed, but the control block's memory is not released immediately — the whole chunk is only reclaimed after all `weak_ptr`s are destroyed too (the weak count hits zero). If the object is large and `weak_ptr`s are still in use, memory usage can stay higher than expected. If you expect `weak_ptr`s to live for a long time, consider using `std::shared_ptr<T>(new T)` to give the object its own memory, independent of the control block, so the object's memory is released as soon as the strong count reaches zero.

## Atomic Operations and Thread Safety of the Reference Count

`shared_ptr`'s reference count uses atomic operations to stay thread-safe. This means that in a multithreaded environment you can safely copy and destroy `shared_ptr` objects themselves (the count increments and decrements are atomic), but **access to the managed object is not protected** — if multiple threads read and write the object itself concurrently, you still have to do your own locking.

> Many people assume `shared_ptr` gives you "thread safety for the object", but what it actually guarantees is only "**thread safety for the reference count**". In plain words: I can guarantee that my bookkeeping of how many people are using the resource — adding and removing references concurrently — is itself **thread-safe**, but the thing you are managing is not. For instance, concurrent access to the managed object itself will still blow up! So locking for object access remains your job.
>
> For a precise take, we can lean on cppreference's description<RefLink :id="4" preview="cppreference std::shared_ptr — member functions safe on distinct instances; data race on the same instance via non-const access" />: `shared_ptr`'s control block is thread-safe — multiple threads can operate on distinct `shared_ptr` instances at the same time (even when they point to the same object) with no external synchronization. But a single `shared_ptr` instance must not be read and written by multiple threads concurrently (that needs a lock). Concurrent access to the managed object has to be made safe by you<RefLink :id="5" preview="Stack Overflow — To what degree does std::shared_ptr ensure thread-safety?" />.

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
        threads.emplace_back([data]() {  // Copies the shared_ptr, atomically incrementing the reference count
            // Reading *data is safe (read-only)
            std::cout << "value: " << *data << "\n";

            // But multiple threads writing *data at the same time is a data race — locking required!
        });
    }

    for (auto& t : threads) t.join();
    std::cout << "final use_count: " << data.use_count() << "\n";  // should be 1
}
```

From a performance standpoint, every copy or destruction of a `shared_ptr` incurs one atomic operation (typically `fetch_add` or `fetch_sub`). On a single-core system the cost is tiny (possibly just one special CPU instruction), but on multicore systems it triggers cache-coherence-protocol overhead (cache line bouncing). If your code creates and destroys `shared_ptr`s frequently (in a hot loop, say), this cost can become very significant.

The decrement path deserves particular attention. When `fetch_sub` returns 1 (meaning this is the last `shared_ptr`), the object must be destroyed. Mainstream implementations (GNU libstdc++ for one) use `memory_order_acq_rel` to make all earlier writes visible to the destruction code, and insert an `acquire` fence before destroying. These memory barriers are cheap on x86 (which is already strongly ordered), but on weakly ordered architectures like ARM they can force pipeline flushes.

## A Performance Cost Analysis of shared_ptr

Let's do a side-by-side comparison and put the costs of `shared_ptr`, `unique_ptr`, and raw pointers into one table:

| Dimension         | Raw pointer  | unique_ptr | shared_ptr                       |
| ----------------- | ------------ | ---------- | -------------------------------- |
| Object size       | 8B (64-bit)  | 8B         | 16B                              |
| Extra heap allocs | None         | None       | Control block (24-32B+)          |
| Copy cost         | 8B copy      | Not copyable | Atomic fetch_add               |
| Destruction cost  | None         | delete     | Atomic fetch_sub + maybe delete  |
| Thread safety     | None         | None       | Count is safe, object is not     |

The table makes it plain: `shared_ptr` is heavier than `unique_ptr` on every single dimension. That doesn't make `shared_ptr` bad — in shared-ownership scenarios it is the right design choice — but you should use it when you truly need shared ownership, not "sprinkle `shared_ptr` everywhere for convenience".

In real projects I've seen plenty of codebases manage nearly every object with `shared_ptr`, and the result is always the same: reference counts flying everywhere, performance that can't be optimized, and circular-reference problems popping up constantly. The better approach is to settle ownership relationships at design time: manage most resources with `unique_ptr`, use `shared_ptr` only in the few places that genuinely need sharing, and pass non-owning access through references (`T&`) or raw pointers (`T*`, holding no ownership).

## The Aliasing Constructor: A Powerful, Little-Known Feature

`shared_ptr` has one constructor that is extremely powerful yet not widely known, called the **aliasing constructor**. Its signature is:

```cpp
template <typename U>
shared_ptr(const shared_ptr<U>& r, T* ptr) noexcept;
```

This constructor creates a new `shared_ptr` that shares ownership with `r` (its reference count is `r`'s), but `get()` returns `ptr` instead of `r.get()`. Put simply: **it lets you hold "a piece" of an object without having to manage that piece's lifetime separately**.

The most common use is accessing a member of an object:

```cpp
struct Config {
    std::string host;
    int port;
    std::string db_name;
};

auto config = std::make_shared<Config>();

// Get a shared_ptr pointing to config->host
// It shares config's reference count — as long as someone holds host_ptr, config won't be destroyed
std::shared_ptr<std::string> host_ptr(config, &config->host);

// Use host_ptr in another component that doesn't need to know Config exists
void connect(const std::shared_ptr<std::string>& host) {
    std::cout << "Connecting to " << *host << "\n";
}
```

This feature is especially handy for implementing "smart pointers to container elements" — say you want to return a `shared_ptr` to one element of a `vector` without making the caller hold a `shared_ptr` to the entire `vector`. With the aliasing constructor you can return a `shared_ptr` that only exposes the element's type, while underneath, the lifetime is still managed by the container's `shared_ptr`.

## enable_shared_from_this: Obtaining a shared_ptr Inside a Member Function

Sometimes a member function needs to return a `shared_ptr` to the object itself. The most intuitive spelling, `shared_ptr(this)`, is a fatal mistake — it creates a brand-new control block and gets the object deleted twice. The right approach is to inherit from `std::enable_shared_from_this` and call `shared_from_this()`:

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
        // Async reads usually need to hold a shared_ptr to self so the object isn't destroyed before the read completes
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

Using `shared_from_this()` comes with one precondition: the object must already be managed by a `shared_ptr`. If you create the object on the stack or manage it with a raw pointer, calling `shared_from_this()` is undefined behavior. And you can't call `shared_from_this()` from the constructor either — at that point the `shared_ptr` hasn't finished constructing.

## Common Misuses and Pitfalls

Before diving into the embedded-specific trade-offs, let's run through a few classic misuse patterns for `shared_ptr`. I've stepped into every one of these pits more than once myself, and I'd rather you sidestepped them in advance.

**Misuse #1: using `shared_ptr(this)` to create a second control block**. This is the most lethal mistake of all. If you write `return std::shared_ptr<Widget>(this)` inside a member function of an object already managed by a `shared_ptr`, the compiler happily creates a fresh control block with the count starting from 1. Now two independent control blocks manage the same object — and when both `shared_ptr`s are destroyed, the object gets deleted twice. The correct fix is to inherit `enable_shared_from_this` and call `shared_from_this()`.

**Misuse #2: exposing `shared_ptr`'s ownership intent in your interfaces**. If you write a function `void process(std::shared_ptr<Widget> w)`, the signature itself says "I want to share ownership with you". Most of the time, though, the function merely wants to use the object, not own it. In that case passing `const Widget&` or `Widget*` fits better — it implies no ownership and skips the reference-counting cost.

**Misuse #3: managing objects with `shared_ptr` when nothing needs sharing**. Some teams, to save themselves the thinking, manage every heap object with `shared_ptr` — "shared_ptr can manage anything anyway". The results: muddy ownership semantics (everyone holding it means nobody is responsible), degraded performance (atomic operations everywhere), and a heightened risk of circular references. My rule of thumb: **90% of objects should be managed by `unique_ptr`; only the 10% that genuinely need sharing get `shared_ptr`**.

**Misuse #4: ignoring the difference between `make_shared` and `new`**. `make_shared` merges the object and the control block into one allocation, but that also means the object's destruction and the control block's release don't happen at the same moment — when all `shared_ptr`s are gone the object is destructed, yet if any `weak_ptr` is still alive, the whole chunk of memory (including the space the object occupied) is not released until every `weak_ptr` is destroyed too. For large objects this can produce the "nobody is using it but the memory still isn't returned" effect. If you expect long-lived `weak_ptr`s, allocating the object and the control block separately with `shared_ptr<T>(new T)` may be the better fit.

## The Systemic Consequences of Abusing shared_ptr

I've given this its own section for one simple reason: I used to be one of the abusers...

Above we went through `shared_ptr`'s common misuse patterns one by one, but the problem runs deeper than "someone wrote it wrong in one place". When `shared_ptr` is abused systematically across a codebase, it becomes **chronic poison at the architectural level** — not the acute kind of error that breaks the build, but a progressive rot that gradually makes the codebase unmaintainable, impossible to reason about, and impossible to optimize. I've watched more than one project sink into this swamp over "manage every object with `shared_ptr`", and digging out usually takes a massive refactor.

### The Collapse of the Ownership Model

In a healthy design, every object has a clear owner — "who created it, who destroys it, who decides its lifetime" — questions that should be answered during the design stage. Once you use `shared_ptr` everywhere, those answers degrade into "who knows, it gets destroyed whenever the count hits zero". Sounds convenient, but the price is losing control over object lifetimes: you can't guarantee the object is alive at any particular moment (other holders may let go at any time), and you can't guarantee it's destroyed at any particular moment either (holders you don't know about may still be referencing it). This "nobody is responsible" state is cut from exactly the same cloth as an epidemic of global variables.

In his *C++ Seasoning* talk at GoingNative 2013, Sean Parent nailed it by comparing `shared_ptr` abuse to **implicit global variables** — his exact words were "A shared pointer is as good as a global variable"<RefLink :id="6" preview="Sean Parent, C++ Seasoning, GoingNative 2013, slides PDF" /> — any code holding a `shared_ptr` is participating in managing that object's lifetime, which is strikingly similar to a global variable's "accessible from anywhere, extendable from anywhere" character. The more practical problem: once your public interface returns `shared_ptr<T>`, every caller is forced into `shared_ptr` even if they only wanted to borrow the object briefly. You've stripped callers of the right to choose an ownership model — the better move is to return `unique_ptr` (callers can freely `std::move` it into a `shared_ptr`) or a raw pointer/reference (non-owning access).

### Cache Line Contention Under Multithreading

This one never shows up in single-threaded code, but under multithreading it gets glaring. A `shared_ptr`'s control block stores both the strong count and the weak count, and those two atomic counters usually live in the same control block, very likely on the same cache line (typically 64 bytes). When multiple threads frequently copy and destroy `shared_ptr`s that point to **the same object**, every atomic modification of the count sends that cache line bouncing between cores — even though each thread is manipulating its own independent `shared_ptr` instance, as long as they point to the same object they contend for the same control block's cache line.

Talk is cheap, so let's run a test. The benchmark below builds a producer-consumer thread-safe queue and passes messages through it with raw pointers and with `shared_ptr` in turn. The test environment: my Windows WSL2 Arch Linux, AMD Ryzen 7 5800H (14 threads), GCC 15.2, compiled `-O2` Release. Results:

| Approach     | Messages | Average time | Relative cost |
| ------------ | -------- | ------------ | ------------- |
| Raw pointer  | 10,000   | ~30 ms       | Baseline      |
| `shared_ptr` | 10,000   | ~35 ms       | **+15-20%**   |

That 15-20% cost can be even more pronounced in real applications, because our test used a mutex-protected queue, and the mutex's overhead masks part of `shared_ptr`'s. With a lock-free queue or higher concurrency (8 threads in the original test, for instance), `shared_ptr`'s overhead becomes more visible. Where the cost comes from is clear: every `shared_ptr` copy atomically increments the count and every destruction atomically decrements it — when multiple threads hammer the same control block at the same time, those atomic operations trigger cache line contention. Low-concurrency, low-throughput scenarios can ignore it; on high-concurrency hot paths, be very careful.

### Circular References: The Silent Memory Leak

When objects leak because of a circular reference, you get no error of any kind — the `shared_ptr` count never reaches zero, and the object just sits quietly on the heap holding memory. No crash, no failed assertion, no log line telling you "hey, this object leaked". At best you notice when memory usage keeps creeping up, and only then do you reach for Valgrind or AddressSanitizer to pin down the leak. Worse, circular references are often not a simple two-object loop but a complex dependency graph over many objects — A holds B, B holds C, C holds A again — and tracing the reference chain in that situation is genuinely painful.

By contrast, `unique_ptr`'s exclusive-ownership model makes circular references impossible at compile time (you cannot construct a legal cycle of exclusive ownership) — a huge advantage at the design level. And if you find yourself needing `weak_ptr` all over the place just to break cycles, that in itself is a loud signal: your ownership model has problems, and you should re-examine the dependencies between objects rather than slapping `weak_ptr` band-aids everywhere.

### Ownership Inversion: A Time Bomb in Callbacks

This one is especially common in asynchronous programming, and the resulting bugs are brutally hard to chase down. Suppose object A holds a Timer, and the Timer's callback captures A's `shared_ptr` via `shared_from_this()`. After A is reset on the main thread, the Timer thread becomes A's sole owner instead — A's lifetime has been "inverted" onto the Timer thread. If the Timer's destructor then needs to join the very thread it is running on (`std::jthread` does exactly that), you get a `std::system_error`: a thread attempting to join itself, which is undefined behavior. The root cause of this class of bug is that `shared_ptr` lets you "get away without thinking about ownership" — you think A was released, but the callback is still quietly tugging on it. The right fix is to nail down lifetime constraints at design time: if A's destruction depends on the Timer thread finishing, then A must be destroyed before the Timer, and that constraint is exactly what `unique_ptr`'s exclusive semantics expresses.

### Unpredictable Destruction Timing and Real-Time Hazards

When you finally decide to drop the object held by a `shared_ptr`, you can't tell whether yours is the last one — the object may be destroyed by this drop, or it may survive because other holders remain. That means the moment the destructor runs is **unpredictable**, and the destruction order is **undefined** as well.

That's genuinely dangerous in real-time systems — the classic case being the audio callback, and for embedded, the ISR (interrupt service routine). On any code path with real-time requirements, if it just so happens to be the last holder, the destructor it triggers can introduce unacceptable latency — heap deallocation, file I/O, log writes are all nondeterministic, time-consuming operations.

> In his talks on C++ audio development, Timur Doumler has described a clever `ReleasePool` scheme: a low-priority thread periodically cleans up the `shared_ptr`s that may need destructing, guaranteeing that no destructor ever fires on the real-time thread<RefLink :id="7" preview="Timur Doumler — Talks on C++ Audio: Sharing Data Across Threads, JUCE Forum, 2018" />. But at the end of the day, if you had used `unique_ptr` plus explicit lifetime management from the design stage, you wouldn't need this workaround at all.

## A Practical Selection Guide: When to Use shared_ptr

Before we get to the embedded trade-offs, let's do a practice-oriented selection analysis. Many people dither between `unique_ptr` and `shared_ptr`, when the criterion is actually simple — ask yourself one question: **does this object need to be co-owned by multiple independent modules?**

If the answer is "no" — the object's lifetime is decided by one clear "owner" and other modules merely borrow it temporarily — use `unique_ptr` plus raw pointer/reference passing. That covers the overwhelming majority of cases.

If the answer is "yes" — multiple modules genuinely need to decide independently "I'm still using this object", and no module can claim "I am the sole owner" — use `shared_ptr`.

Typical scenarios where `shared_ptr` fits: shared modules in a plugin system (multiple components may depend on the same plugin instance at once, so none of them may unload it early), shared state in an asynchronous callback chain (multiple futures/callbacks need to keep the state alive until they themselves finish), and shared nodes in a tree or graph (multiple parents referencing the same child).

Typical scenarios where you should not use `shared_ptr`: passing function arguments (a reference does the job), the unique owner of an object (that's `unique_ptr`), and simple caches (observe with `weak_ptr`, hold with `shared_ptr`).

Let's look at a concrete design decision — implementing a simple task scheduler:

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

// If tasks need to be shared by multiple schedulers — only now do we need shared_ptr
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

The first version uses `unique_ptr` — once a task is submitted, ownership belongs to the scheduler; simple and unambiguous. The second uses `shared_ptr` — multiple schedulers or external code may hold references to the same task, and the task is destroyed only when the last holder walks away. Which one you pick should follow your design requirements, not "whichever is more convenient".

In the next article we'll sit down for a proper chat about `weak_ptr` — `shared_ptr`'s partner, built specifically to solve the thorny problem of **circular references**.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup / Herb Sutter (eds.)"
    title="C++ Core Guidelines — R.20-R.24: Smart Pointer Rules"
    publisher="isocpp.org"
    chapter="R.21: Prefer unique_ptr over shared_ptr"
    url="https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rr-owner"
  />
  <ReferenceItem
    :id="2"
    author="Raymond Chen"
    title="Inside STL: The Different Types of Shared Pointer Control Blocks"
    publisher="The Old New Thing, Microsoft DevBlogs"
    :year="2023"
    url="https://devblogs.microsoft.com/oldnewthing/20230821-00/?p=108626"
  />
  <ReferenceItem
    :id="3"
    author="cppreference.com"
    title="std::shared_ptr — make_shared"
    chapter="Notes: single allocation; memory retention with weak_ptr"
    url="https://en.cppreference.com/w/cpp/memory/shared_ptr/make_shared"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="std::shared_ptr"
    chapter="Thread safety; Implementation notes"
    url="https://en.cppreference.com/w/cpp/memory/shared_ptr"
  />
  <ReferenceItem
    :id="5"
    title="To What Degree Does std::shared_ptr Ensure Thread-Safety?"
    publisher="Stack Overflow"
    url="https://stackoverflow.com/questions/9127816/to-what-degree-does-stdshared-ptr-ensure-thread-safety"
  />
  <ReferenceItem
    :id="6"
    author="Sean Parent"
    title="C++ Seasoning (Slides)"
    publisher="GoingNative 2013"
    :year="2013"
    url="https://sean-parent.stlab.cc/presentations/2013-09-11-cpp-seasoning/cpp-seasoning.pdf"
  />
  <ReferenceItem
    :id="7"
    author="Timur Doumler"
    title="Timur Doumler Talks on C++ Audio: Sharing Data Across Threads (Discussion)"
    publisher="JUCE Forum"
    :year="2018"
    url="https://forum.juce.com/t/timur-doumler-talks-on-c-audio-sharing-data-across-threads/26311"
  />
</ReferenceCard>
