---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: From compiler reordering to CPU reordering, we take the six memory_order
  values and the happens-before relationship apart one by one
difficulty: advanced
order: 2
platform: host
prerequisites:
- Atomic Operations
reading_time_minutes: 16
related:
- Fences and Compiler Barriers
- Atomic Operation Patterns
tags:
- host
- cpp-modern
- advanced
- atomic
- memory_order
title: A Deep Dive into Memory Ordering
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md
  source_hash: e9518f80952e6e053dd85a07c494de2063d8d534c1ab40c13fe89e94c7167527
  translated_at: '2026-09-26T07:15:43+00:00'
  engine: anthropic
  token_count: 3300
---
# A Deep Dive into Memory Ordering

In the previous article we took the complete operation set of `std::atomic<T>` apart—load, store, fetch_add, compare_exchange—and everything runs fine if you just fire them off with the default arguments. But have you noticed that almost every atomic operation takes an optional `std::memory_order` parameter? Plenty of people (including the author, back in the day) simply ignore it—the default value works, after all.

That is indeed the case in simple scenarios. But the moment you start using atomic variables for synchronization between threads—one thread writes data, another thread reads it—all kinds of spooky phenomena pop up: data you clearly wrote first simply never shows up in the other thread; the two threads observe completely incompatible orders of the same operations. The problem is not the atomic operations themselves, but the fact that **both the compiler and the CPU are reordering instructions behind your back**—and memory order is the tool you use to control that reordering.

In this article we take the six `memory_order` values apart one by one, work out what each order guarantees, what it does not guarantee, and when to use which.

## Why Reordering Happens: Compiler Optimization and CPU Optimization

Before diving into the six memory orders, we have to accept one basic fact: the order you write code in and the order the CPU actually executes it may not be the same thing. This is not a bug—it is the inevitable consequence of performance optimization.

Compilers reorder instructions during the optimization phase. When the compiler sees two pieces of code that do not depend on each other, it may swap them—writes to two different variables, for instance: the compiler judges that their relative order does not affect single-threaded semantics, so it may flip them. Consider this classic example:

```cpp
int data = 0;
bool ready = false;

// Thread 1
data = 42;         // Step A
ready = true;      // Step B

// Thread 2
if (ready) {       // Step C
    use(data);     // Step D
}
```

From a single-threaded point of view, the order of A and B does not matter (there is no data dependency between `data` and `ready`). The compiler is entirely free to schedule B ahead of A. From a multi-threaded point of view, though, this means thread 2 may see `ready == true` while `data` is still 0—it thinks the data is ready, but it is not.

Out-of-order execution exists at the CPU level as well. Modern CPUs are superscalar, deeply pipelined designs, and to keep the pipeline full and minimize stalls, the hardware dynamically adjusts the order in which instructions execute. x86 has a very strong memory model (TSO, Total Store Ordering) that permits only store-load reordering; ARM and PowerPC have much weaker memory models that allow store-store, load-load, store-load, and load-store reordering—all of it. The same code that runs correctly on x86 may break on ARM—which is exactly why the C++ standard defines a platform-independent memory model.

To sum up: the compiler reorders for the efficiency of instruction scheduling and register allocation; the CPU reorders for pipeline throughput. Both are "transparent" to single-threaded semantics—in a single-threaded program, no matter how things get reordered, the final result never changes (the as-if rule). But a multi-threaded program depends not only on the final result but also on the **order of visibility** between operations—and reordering is precisely what destroys that order.

## The Six Memory Orders at a Glance

C++ defines six memory orders in the `std::memory_order` enumeration, listed below from weakest to strongest. `memory_order_consume` was flagged as "discouraged" in C++17 and formally deprecated in C++26; in practice, the mainstream compilers all treat it as `memory_order_acquire`. We will mention it briefly later but will not discuss it in depth.

- `memory_order_relaxed`: guarantees atomicity only, with no ordering constraints whatsoever.
- `memory_order_consume`: data-dependency ordering (deprecated; use acquire instead).
- `memory_order_acquire`: for load operations; guarantees that subsequent reads and writes cannot be reordered before this load.
- `memory_order_release`: for store operations; guarantees that preceding reads and writes cannot be reordered after this store.
- `memory_order_acq_rel`: for read-modify-write operations; carries both acquire and release semantics.
- `memory_order_seq_cst`: the default value and the strongest guarantee—all seq_cst operations participate in one globally consistent total order.

Now let's expand on them one by one.

## memory_order_relaxed: Atomicity Only

`memory_order_relaxed` is the lightest memory order. It guarantees that the operation itself is atomic—no torn reads or torn writes, and no thread ever sees an intermediate state. But it **guarantees no ordering whatsoever between operations**, which means the compiler and the CPU may freely reorder a relaxed operation with respect to the operations around it.

A typical scenario is a plain counter. You only care about the counter's final value, not about the relative order between the counting operations and anything else:

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

std::atomic<int> request_count{0};
std::atomic<int> error_count{0};

void handle_request()
{
    request_count.fetch_add(1, std::memory_order_relaxed);
    // ... handle the request ...
}

void log_error()
{
    error_count.fetch_add(1, std::memory_order_relaxed);
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([]() {
            for (int j = 0; j < 100000; ++j) {
                handle_request();
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Total requests: " << request_count.load(
                     std::memory_order_relaxed) << "\n";
    // Output: Total requests: 400000
    return 0;
}
```

The danger of relaxed is that you cannot use it for inter-thread synchronization. A mistake many beginners make is to use a relaxed store/load pair as a "the data is ready" flag:

```cpp
// Dangerous example: using relaxed for synchronization
std::atomic<bool> data_ready{false};
int data = 0;

// Thread 1: producer
data = 42;
data_ready.store(true, std::memory_order_relaxed);

// Thread 2: consumer
if (data_ready.load(std::memory_order_relaxed)) {
    // data may still be 0!
    use(data);
}
```

Why is that wrong? Because `memory_order_relaxed` does not prevent reordering. The compiler or the CPU may move `data_ready.store(true)` ahead of `data = 42`. From thread 2's point of view, `data_ready` has become true while `data` still holds the old value. To use a flag for synchronization you must use acquire-release—which is exactly what the next section covers.

## memory_order_acquire and memory_order_release: The Golden Pair for Synchronization

acquire and release are the most frequently used pair of memory orders; together they form the fundamental mechanism of inter-thread synchronization. Understanding this pair is the key to understanding the whole memory model.

### release: The "Publish" Semantics of a Store

`memory_order_release` applies to store operations. It guarantees: **all reads and writes before this store (atomic or not) will not be reordered after the store**. You can think of it as a "publish" action—all the preparation work before the store is finished, and now it is officially published.

```cpp
int data = 0;
std::atomic<bool> ready{false};

// Thread 1: producer
data = 42;                                  // Prepare the data
ready.store(true, std::memory_order_release); // Publish: guarantees data is written first
```

A release store is like a sealed envelope—everything inside it (all the prior writes) was written before the seal went on; nothing gets slipped in after it is sealed.

### acquire: The "Subscribe" Semantics of a Load

`memory_order_acquire` applies to load operations. It guarantees: **all reads and writes after this load will not be reordered before the load**. More importantly, if one thread loads—with acquire—a value that another thread stored with release, then every write the writing thread performed before the release becomes visible to the reading thread.

```cpp
// Thread 2: consumer
if (ready.load(std::memory_order_acquire)) {  // Subscribe
    // Guaranteed to see data == 42
    use(data);
}
```

An acquire load is like opening the envelope—you can only read the letter after breaking the seal. What you see after opening must be what the sender wrote before sealing it.

### synchronizes-with and happens-before

Now we can introduce the most central relationship in the C++ memory model. When thread A performs a release store and thread B performs an acquire load that reads the value thread A wrote, we say that thread A's store **synchronizes-with** thread B's load.

A synchronizes-with relationship establishes a **happens-before** relationship: every operation thread A performed before the release store happens-before every operation thread B performs after the acquire load. What happens-before means is that the earlier operation is **visible** to the later one.

This chain extends further. If operation A happens-before operation B, and operation B happens-before operation C, then A also happens-before C—that is transitivity. In a multi-threaded environment, this transitivity is established through the **inter-thread-happens-before** relationship, which chains together the sequenced-before relationship (program order) within a single thread and the synchronizes-with relationship across threads, forming a complete "visibility chain".

Back to our example: `data = 42` is sequenced-before `ready.store(true, release)` (within the same thread), `ready.store(true, release)` synchronizes-with `ready.load(acquire)` == true (across threads), and `ready.load(acquire)` is sequenced-before `use(data)` (within the same thread). By transitivity, `data = 42` happens-before `use(data)`—so `use(data)` is guaranteed to see `data == 42`.

### The message passing pattern

The most classic application of acquire-release is the message passing pattern: one thread prepares data and then notifies another thread that "the data is ready" through an atomic flag.

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <string>

struct Message {
    int id;
    std::string content;
};

Message msg;
std::atomic<bool> ready{false};

void producer()
{
    msg.id = 1;
    msg.content = "Hello from producer";
    // release: guarantees the assignments above complete before the store
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    // Spin until we see ready == true
    while (!ready.load(std::memory_order_acquire)) {
        // In real code, add a yield or sleep here to avoid a pure spin
    }
    // At this point the complete msg is guaranteed to be visible
    std::cout << "Received message #" << msg.id
              << ": " << msg.content << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

Note that `msg` itself is not an atomic variable—it is a plain `Message` object. But the happens-before relationship established by acquire-release guarantees that once `consumer` reads `ready == true`, it is guaranteed to see the complete `msg` written by `producer`. That is the power of memory ordering: by synchronizing one atomic variable, you indirectly synchronize all the non-atomic data around it.

## memory_order_acq_rel: Bidirectional Guarantees for Read-Modify-Write Operations

`memory_order_acq_rel` applies to read-modify-write (RMW) operations—`fetch_add`, `exchange`, `compare_exchange`, and friends. Such operations involve both a read and a write, so this order carries both acquire and release semantics: acquire guarantees that operations after the RMW are not reordered before it, and release guarantees that operations before the RMW are not reordered after it.

```cpp
std::atomic<int> counter{0};

// acq_rel: carries both acquire and release semantics
int old = counter.fetch_add(1, std::memory_order_acq_rel);
```

When do you need `acq_rel`? The most typical scenario is reference counting. When `fetch_sub` brings the count down to 0, the object must be destroyed—acquire guarantees you can see the object's fully constructed state, and release guarantees that all prior uses happened before the decrement:

```cpp
class RefCounted {
public:
    void add_ref()
    {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    void release()
    {
        // acq_rel: decrement the reference while guaranteeing visibility
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            // The last reference has been released; safe to destroy
            delete this;
        }
    }

protected:
    virtual ~RefCounted() = default;

private:
    std::atomic<int> ref_count_{1};
};
```

## memory_order_seq_cst: The Default Global Total Order

`memory_order_seq_cst` (sequentially consistent) is the default memory order for all atomic operations and the strongest guarantee. On top of acquire-release it adds one extra constraint: **there exists a single, globally consistent total order over all `seq_cst` operations**—every thread observes the same execution order of the `seq_cst` operations.

What does that mean? Consider a scenario involving multiple atomic variables:

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// Thread 1
x.store(1, std::memory_order_seq_cst);

// Thread 2
y.store(1, std::memory_order_seq_cst);

// Thread 3
int r1 = x.load(std::memory_order_seq_cst);
int r2 = y.load(std::memory_order_seq_cst);

// Thread 4
int r3 = y.load(std::memory_order_seq_cst);
int r4 = x.load(std::memory_order_seq_cst);
```

With `seq_cst`, it is impossible for thread 3 to see `r1 == 1, r2 == 0` (x changed first) while thread 4 simultaneously sees `r3 == 1, r4 == 0` (y changed first). Because `seq_cst` guarantees that all threads agree on the modification order of x and y—either the whole system takes x as changing first, or the whole system takes y as changing first.

Switch to `acquire-release` and that agreement is no longer guaranteed. acquire-release only establishes a synchronizes-with relationship between paired loads and stores; it imposes no global constraint on the ordering between different atomic variables. When several atomic variables must cooperate, `seq_cst` is the safest choice.

What is the cost? On x86 it is tiny—x86's TSO model is already very strong, and a `seq_cst` store only requires a single `MFENCE` or `LOCK XCHG` instruction. But on weak-memory architectures such as ARM and PowerPC, `seq_cst` requires a full memory barrier (ARMv8's `DMB ISH`, PowerPC's `sync`), and the overhead can be 3 to 6 times that of `relaxed`.

A practical principle: **start with `seq_cst`, and if the program runs and performance is satisfactory, leave it alone**. Only consider downgrading to acquire-release or even relaxed when you have a clear performance bottleneck and profiling has confirmed that the atomic operations are where it lives. Prematurely optimizing memory ordering is a hidden source of errors in concurrent programming.

## memory_order_consume: The Dependency Order Deprecated in C++26

The original design intent of `memory_order_consume` was to be lighter than `acquire`: it only guarantees that operations depending on the loaded value are not reordered before the load, while operations that do not depend on the value remain unconstrained. In pointer-publishing scenarios this is in theory more efficient than `acquire`—you only need the data reached through the pointer to be correct, with no need to synchronize every other memory operation.

In reality, though, no mainstream compiler truly implements consume's exact semantics. Dependency-chain tracking is extraordinarily difficult for a compiler to do, so both GCC and Clang promote `consume` to `acquire`. C++17 marked `consume` as "discouraged"; in practice, just use `acquire`.

## When to Use Which: A Practical Guide

At this point we have taken every memory order apart individually. The practical decision flow below can help you make the choice while actually coding.

**Plain counters, statistics, metrics**: use `memory_order_relaxed`. You only care about the final value being correct, not about its ordering relative to other operations.

**One thread writes data, another thread reads it** (the message passing pattern): the writer uses `memory_order_release`, the reader uses `memory_order_acquire`. This is the most common pattern and the one most worth mastering.

**Reference counting, semaphores, and other RMW operations**: use `memory_order_acq_rel`. When a decrement reaches 0 the object must be destroyed, which requires both seeing the object's complete state (acquire) and ensuring all prior accesses have finished (release).

**Multiple atomic variables that must cooperate**: use `memory_order_seq_cst`. If you are not sure what to use, start with `seq_cst` as well.

**Never use `memory_order_consume`**: use `acquire` instead.

An even simpler rule of thumb: when you can point at two spots in the code and say "this synchronizes-with that", use acquire-release; when you need "all threads agreeing on one order over all atomic operations", use seq_cst; when you need no synchronization at all and care only about atomicity itself, use relaxed.

## Exercises

### Exercise 1: A Message Passing Experiment

Write a program that verifies the correctness of acquire-release synchronization. Create two threads: the producer writes a non-atomic `int payload`, then stores a `std::atomic<bool> ready` with release semantics; the consumer loads `ready` with acquire semantics and, once it reads true, reads `payload`. Confirm that the consumer always sees the correct payload value.

Then change the memory order on both ends to `memory_order_relaxed` and run repeatedly under high concurrency. Can you observe the payload being read with a stale value? (Hint: this is hard to reproduce on x86, because x86's hardware model is stronger than relaxed. Try an ARM device, or use ThreadSanitizer to raise the odds of reproduction.)

```cpp
#include <atomic>
#include <thread>
#include <iostream>

int payload = 0;
std::atomic<bool> ready{false};

void producer()
{
    payload = 42;
    ready.store(true, std::memory_order_release);
}

void consumer()
{
    while (!ready.load(std::memory_order_acquire)) {}
    std::cout << "payload = " << payload << "\n";
}

int main()
{
    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    return 0;
}
```

### Exercise 2: Comparing the Behavior of relaxed and acquire-release

Write a program using two atomic variables `x` and `y` (both initialized to 0). Thread 1 stores 1 into x and y respectively; thread 2 reads y and x (y first, then x). Run it under two configurations:

1. All operations use `memory_order_relaxed`.
2. All operations use `memory_order_seq_cst`.

Repeat the experiment in a loop (say, one million times) and count how often thread 2 sees `y == 1 && x == 0`. In theory this outcome can occur under relaxed (there is no ordering constraint between the two stores), while under seq_cst it should never occur. Note: the difference is very hard to observe on x86; this experiment is far better suited to weak-memory architectures.

> 💡 The complete sample code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) — see `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::memory_order -- cppreference](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [C++ Standard Draft [intro.multithread] -- eel.is](https://eel.is/c++draft/intro.multithread)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams, Chapter 5](https://www.oreilly.com/library/view/c-concurrency-in/9781617294693/)
- [Herb Sutter: atomic Weapons -- CppCon 2012](https://www.youtube.com/watch?v=A8e5OjAVHEA)
- [Memory Ordering in Modern Microprocessors -- Paul E. McKenney](https://www.linuxjournal.com/content/memory-ordering-modern-microprocessors-part-i)
