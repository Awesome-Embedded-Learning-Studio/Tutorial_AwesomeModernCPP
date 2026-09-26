---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A complete guide to std::atomic<T>: load/store, fetch_add, compare_exchange, and lock-free detection'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- latch, barrier, and semaphore
reading_time_minutes: 20
related:
- A Deep Dive into Memory Ordering
- Atomic Operation Patterns
tags:
- host
- cpp-modern
- intermediate
- atomic
title: Atomic Operations
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md
  source_hash: 026f0fed36a121949b421472daaa505042017d8db2e1a2e3d4cdf9822a9b6e5a
  translated_at: '2026-09-26T07:13:42+00:00'
  engine: anthropic
  token_count: 4300
---
# Atomic Operations

So far, every synchronization primitive we have discussed—mutex, condition variable, latch, barrier, semaphore—has essentially followed the same idea: "lock first, operate, unlock last". They are safe and intuitive, but they share a common cost: even if all you want to protect is a simple integer increment, you still have to go through the full lock → modify → unlock cycle. For an operation with granularity as fine as "modify one variable", that ceremony weighs more than the work itself.

`std::atomic` is designed for exactly these "minimal granularity" scenarios. It does not rely on locks (at least in the ideal case); instead it uses the atomic instructions the CPU provides directly to make operations indivisible. In the previous article we already fixed a data race with `std::atomic<int>` while working through the fundamental problems of concurrency, but we only scratched the surface. This time we take `std::atomic<T>` apart completely—every operation from the most basic `load`/`store`, through the CAS (compare-and-swap) mechanism, to lock-free detection and the specialized type `atomic_flag`. Memory ordering gets its own article next; here we keep the focus on what atomic operations can do.

## Types That std::atomic<T> Supports

`std::atomic` is a class template defined in the `<atomic>` header. Not every type can be put into a `std::atomic`—the standard draws explicit lines here.

For integral types—`bool`, `char`, `short`, `int`, `long`, `long long`, and their unsigned variants—the standard library provides explicit specializations of `std::atomic` with the full set of arithmetic and bitwise atomic operations (`fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor`). Pointer types are specialized too, supporting `fetch_add` and `fetch_sub` for moving a pointer atomically.

For a custom type `T`, `std::atomic<T>` exists as well, provided `T` satisfies one core condition: `std::is_trivially_copyable<T>::value` is true—meaning `T` must not have user-provided copy constructors/assignment operators (`= default` is fine), virtual functions, virtual base classes, and so on. A custom type that meets this condition can use the generic operations—`load()`, `store()`, `exchange()`, `compare_exchange_weak/strong()`—but not arithmetic ones like `fetch_add`: the standard is under no obligation to define what "addition" means for your custom type.

Note that these generic operations bring their own additional requirements on `T`: `load()` requires `T` to be CopyConstructible, `store()` requires `T` to be CopyAssignable, and `exchange()` and `compare_exchange_*` need both. Since `T` is trivially copyable, though, these requirements are almost always satisfied automatically. Also, the default constructor `std::atomic<T> a;` value-initializes the `T` before C++20 (so `T` must be default constructible), but from C++20 onward it leaves the value uninitialized—if you use the constructor that takes an argument, `std::atomic<T> a{T{...}};`, `T` does not need to be default constructible.

```cpp
#include <atomic>
#include <iostream>

// Integral types: fully supported
std::atomic<int> atomic_int{0};
std::atomic<unsigned long> atomic_ulong{0};

// Pointers: fetch_add/fetch_sub supported (offsets in units of element size)
struct Node {
    int value;
    Node* next;
};
std::atomic<Node*> atomic_head{nullptr};

// A custom trivially-copyable type:
// load/store/exchange/CAS supported, but not fetch_add
struct alignas(8) PacketHeader {
    uint32_t id;
    uint32_t flags;
};
static_assert(std::is_trivially_copyable_v<PacketHeader>);
std::atomic<PacketHeader> atomic_header{PacketHeader{0, 0}};
```

Worth noting: since C++20 the standard explicitly supports `std::atomic<float>` and `std::atomic<double>`, and the floating-point specializations provide `fetch_add` and `fetch_sub`. Before C++20, a floating-point atomic could only `load`, `store`, `exchange`, and `compare_exchange`—no direct atomic addition or subtraction. We will come back to the caveats of floating-point atomics later in a dedicated section.

## load() and store(): The Foundation of Atomic Reads and Writes

`load()` and `store()` are the most fundamental pair of atomic operations. Every atomic read or write ultimately boils down to these two (plus an optional memory order parameter). When no memory order is specified, every atomic operation defaults to `memory_order_seq_cst`—the strongest ordering guarantee. We will unpack what that actually means in the next article; for now just remember: the default is the safe choice, just not necessarily the fastest.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{0};

    // store: atomically write a value
    value.store(42);
    value.store(100, std::memory_order_relaxed);

    // load: atomically read the current value
    int x = value.load();
    int y = value.load(std::memory_order_relaxed);

    // Convenience syntax: implicit conversion
    int z = value;       // equivalent to value.load()
    value = 200;         // equivalent to value.store(200)

    std::cout << "value = " << value.load() << "\n";
    return 0;
}
```

Don't reach for the convenience syntax too quickly, though. `int z = value;` looks like an ordinary variable copy, but underneath it sits an atomic load. Mixing implicit conversions into a complex expression can blur the code's intent—is that a plain assignment or an atomic read? When working in a team, we prefer calling `load()` and `store()` explicitly; a few extra characters, but anyone reading it can tell at a glance that an atomic variable is being touched.

## fetch_add, fetch_sub, and Bitwise Operations: Atomic Arithmetic

For integral and pointer types, `std::atomic` offers a family of fetch operations. Each one performs the entire read-modify-write (Read-Modify-Write, RMW) sequence—read the current value → apply the operation → write the new value back—and guarantees that the sequence is atomic: no other thread can observe the intermediate state.

The fetch operations return the **old value from before the modification**, not the new one. This is a very pragmatic design choice: returning the old value means a single call accomplishes both "read the current state" and "change the state", which is extremely convenient when implementing lock-free algorithms.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> counter{0};

    // fetch_add: atomic addition, returns the old value
    int old1 = counter.fetch_add(5);    // counter becomes 5, old1 = 0
    int old2 = counter.fetch_add(3);    // counter becomes 8, old2 = 5

    // fetch_sub: atomic subtraction, returns the old value
    int old3 = counter.fetch_sub(2);    // counter becomes 6, old3 = 8

    // Bitwise operations
    counter.fetch_or(0xFF);    // bitwise OR
    counter.fetch_and(0xF0);   // bitwise AND
    counter.fetch_xor(0x0F);   // bitwise XOR

    std::cout << "counter = " << counter.load() << "\n";
    return 0;
}
```

These operations also have corresponding compound-assignment and increment/decrement operator overloads, but be careful: the operator overloads return the **new value** (strictly speaking, the value after the operation has been applied), not the old one—exactly the opposite of the fetch family:

```cpp
std::atomic<int> x{10};

// Operator overloads return the new value
int new_val = ++x;       // x becomes 11, new_val = 11
int old_val = x++;       // x becomes 12, old_val = 11 (postfix returns the old value)
x += 5;                  // x becomes 17
```

Let us stress an easily confused detail here: `x++` (postfix increment) and `x.fetch_add(1)` are not exactly equivalent. `x++` returns the value from **before** the increment, so in terms of behavior it does match `fetch_add(1)`. But `++x` (prefix increment) returns the value from **after** the increment—it is equivalent to `x.fetch_add(1) + 1`. When the return value is not needed (say, a pure counting increment), either works; the moment you use the result in an expression, the difference matters.

## Caveats of Floating-Point Atomic Operations

This is a problem many people run into the first time they use `std::atomic<float>`. Since C++20 the floating-point specializations do provide `fetch_add` and `fetch_sub`, but two layers of specialness deserve attention.

At the hardware level, the overwhelming majority of CPU architectures offer no atomic floating-point addition instruction. x86 has `LOCK XADD` for atomic integer addition, but floating-point addition runs on the FPU/SSE/AVX execution units, which were never designed for atomicity in the first place. So on most platforms `atomic<float>::fetch_add` degenerates internally into a CAS loop—there is no hardware-level atomic floating-point add.

At the semantic level, floating-point addition is not associative—`(a + b) + c` does not equal `a + (b + c)`, because every operation involves precision rounding. That means even with multiple threads doing `fetch_add` on the same floating-point atomic, the final result depends on the order in which the operations execute, and that order is nondeterministic. On top of that, the result of a floating-point operation can change with the floating-point environment (rounding mode, precision control), which adds yet another layer of non-reproducibility to the semantics of `fetch_add`.

If you need to modify a floating-point variable atomically in a pre-C++20 environment, or you want to sidestep the precision-reproducibility problems of `fetch_add`, the standard approach is a CAS loop:

```cpp
#include <atomic>

std::atomic<float> atomic_value{0.0f};

float atomic_fetch_add(float delta)
{
    float old_val = atomic_value.load(std::memory_order_relaxed);
    float new_val;
    do {
        new_val = old_val + delta;
        // If atomic_value still equals old_val, swap it for new_val
        // otherwise old_val gets updated to the current value; retry
    } while (!atomic_value.compare_exchange_weak(
                 old_val, new_val, std::memory_order_relaxed));
    return old_val;
}
```

We will meet this pattern again in a moment in the CAS section—it is the cornerstone of lock-free programming.

## compare_exchange_weak and compare_exchange_strong: The CAS Mechanism

Compare-And-Swap (CAS) is the most important primitive in the atomic toolbox—no contest. Nearly every lock-free data structure is built on top of CAS. C++ offers two variants, `compare_exchange_weak` and `compare_exchange_strong`, and the difference between them is subtle but critical.

First, the interface. The two signatures are identical:

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           std::memory_order success = memory_order_seq_cst,
                           std::memory_order failure = memory_order_seq_cst);

bool compare_exchange_strong(T& expected, T desired,
                             std::memory_order success = memory_order_seq_cst,
                             MemoryOrder failure = memory_order_seq_cst);
```

The execution logic goes like this: atomically compare the current value with `expected`. If they are equal, replace the current value with `desired` and return `true`; if not, load the current value into `expected` and return `false`. Note that on failure `expected` gets overwritten—an easy detail to miss, so if you still need the original `expected` value later on, save a copy beforehand.

The difference between the two lies in spurious failure: `compare_exchange_weak` may return `false` even when the current value does equal `expected`. That is not a bug—it is a hardware-level limitation. On architectures such as ARM and PowerPC that implement CAS with LL/SC (Load-Linked/Store-Conditional) primitives, the SC instruction can fail for all sorts of reasons: another processor touched the same cache line, an interrupt arrived, even a pure scheduling event. x86 uses the hardware `CMPXCHG` instruction and has no such problem, so on x86 `weak` and `strong` generate identical code.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{10};

    // A successful CAS
    int expected = 10;
    bool ok = value.compare_exchange_strong(expected, 20);
    // ok = true, value = 20, expected unchanged

    // A failed CAS
    expected = 10;  // set it back to 10
    ok = value.compare_exchange_strong(expected, 30);
    // ok = false, value is still 20, expected updated to 20
    std::cout << "value = " << value.load()
              << ", expected = " << expected << "\n";
    return 0;
}
```

So when do you use `weak`, and when `strong`? The rule is simple: if your CAS is already wrapped in a loop, use `weak`—a spurious failure just costs you one more turn around the loop, while `weak` skips the internal retry loop that `strong` needs on LL/SC architectures, so it is faster overall. If it is a one-shot CAS outside any loop, use `strong`—otherwise a single spurious failure can send your logic down the wrong branch.

### Implementing Lock-Free Stack push with CAS

Let's look at a classic CAS application: the push operation of a lock-free stack. It shows the in-a-loop use of `compare_exchange_weak` nicely:

```cpp
#include <atomic>

struct Node {
    int data;
    Node* next;
};

std::atomic<Node*> head{nullptr};

void push(int value)
{
    Node* new_node = new Node{value, nullptr};

    Node* old_head = head.load(std::memory_order_relaxed);
    do {
        new_node->next = old_head;
        // Try to swap head from old_head to new_node
        // If it succeeds, the push is done
        // If it fails (someone else changed head), old_head is updated to the latest value; retry
    } while (!head.compare_exchange_weak(
                 old_head, new_node,
                 std::memory_order_release,
                 std::memory_order_relaxed));
}
```

The logic of this code: read the current `head`, point the new node's `next` at it, then try to swap `head` for the new node with a single CAS. If another thread pushed a node while we were preparing ours (`head` changed), the CAS fails, `old_head` is refreshed to the latest `head`, we reset `new_node->next`, and try again. This repeats until the CAS succeeds.

You may have noticed that `compare_exchange_weak` here takes two memory order parameters: `success` and `failure`. On success it uses `memory_order_release` (we just wrote a new node, and we need to be sure other threads can see the complete data), and on failure it uses `memory_order_relaxed` (a failure needs no synchronization guarantees at all—it's just a retry).

## exchange(): Atomic Exchange

`exchange()` is a relatively simple but very practical operation: it atomically writes the new value in and pulls the old value out at the same time. It is a combination of `load` and `store`, with a guarantee that the two steps are indivisible.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> flag{0};

    int old = flag.exchange(1);
    // now flag = 1, old = 0
    std::cout << "flag = " << flag.load()
              << ", old = " << old << "\n";
    return 0;
}
```

A typical use of `exchange()` is a "state handover"—atomically switch some state from A to B, and let the old state decide what happens next:

```cpp
#include <atomic>
#include <iostream>

enum class DeviceState { kIdle, kBusy, kError };

std::atomic<DeviceState> state{DeviceState::kIdle};

void try_start_work()
{
    // Atomically try to switch from Idle to Busy
    DeviceState old = state.exchange(DeviceState::kBusy);
    if (old != DeviceState::kIdle) {
        // It was not Idle before, meaning another thread is already using it
        // Restore the previous state (or enter error handling)
        state.store(old);
        std::cout << "Cannot start: device was " <<
                     static_cast<int>(old) << "\n";
        return;
    }
    // Successfully switched to Busy; start working
    std::cout << "Work started\n";
}
```

Admittedly, this example could be written more precisely with CAS (`exchange` writes the new value unconditionally, even when the old state was not `kIdle`), but `exchange` wins on simplicity—if all you want is to swap a value in and learn what the old one was, `exchange` is far tidier than a CAS loop.

## is_lock_free and is_always_lock_free

Up to this point we kept saying "atomic operations don't rely on locks", but that is not always true. Whether a `std::atomic<T>` really is lock-free depends on two things: the size of `T` and the hardware capabilities of the target platform. If the hardware has no atomic instruction of the matching width (say, 64-bit integer atomics on a 32-bit ARM), the compiler falls back to an internal lock—and then the operations of that `std::atomic` are not truly lock-free.

The standard library provides two interfaces for querying this. `is_lock_free()` is a runtime query: `true` means operations on this particular object are lock-free. `is_always_lock_free` is a compile-time constant (`static constexpr`): `true` means atomic operations on this type are lock-free for **every** instance on this platform. If you need a compile-time static assertion, use `is_always_lock_free`; if you need a runtime branch, use `is_lock_free()`.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> ai;
    std::atomic<long long> all;

    std::cout << "atomic<int>: "
              << (ai.is_lock_free() ? "lock-free" : "uses lock")
              << "\n";
    std::cout << "atomic<long long>: "
              << (all.is_lock_free() ? "lock-free" : "uses lock")
              << "\n";

    // Compile-time check: if int is not lock-free, fail the build right here
    static_assert(std::atomic<int>::is_always_lock_free,
                  "int must be lock-free on this platform!");

    return 0;
}
```

In real projects, `is_always_lock_free` is more valuable than `is_lock_free()`. Here is why: if a branch on your code path depends on the return value of `is_lock_free()`, the same code can take different paths across different runs and instances—a testing and debugging nightmare. By contrast, `static_assert` + `is_always_lock_free` surfaces the problem at compile time: either the platform fully supports lock-free, or the build fails. No gray area.

In embedded settings this matters all the more. On 32-bit ARM Cortex-M, `std::atomic<int>` is almost always lock-free (the hardware has the `LDREX`/`STREX` instruction pair), but `std::atomic<int64_t>` may not be on a Cortex-M0/M3. If you use atomics inside an ISR, make absolutely sure they are lock-free—an ISR must not block, and lock-backed atomics do block.

## atomic_flag: The Guaranteed Lock-Free Primitive

Whether `std::atomic<T>` is lock-free depends on the platform, but `std::atomic_flag` is the exception—the standard guarantees that `std::atomic_flag` is **always lock-free**. On every platform, with every compiler, no exceptions. That makes `atomic_flag` the most reliable building block for low-level synchronization primitives such as spinlocks.

An `atomic_flag` has only two states: set (true) and clear (false). It provides three core operations: `test_and_set()` atomically sets the flag to true and returns the previous value; `clear()` atomically sets it to false; and C++20 adds `test()`, which atomically reads the current value without modifying it.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    // Since C++20 you can initialize directly with {}
    std::atomic_flag flag{};

    // test_and_set: set to true, return the old value
    bool was_set = flag.test_and_set();
    std::cout << "was_set = " << std::boolalpha << was_set << "\n";

    // test (C++20): read the current value
    bool current = flag.test();
    std::cout << "current = " << current << "\n";

    // clear: set to false
    flag.clear();
    std::cout << "after clear: " << flag.test() << "\n";

    return 0;
}
```

### Implementing a Spinlock with atomic_flag

The most classic application of `atomic_flag` is the spinlock. The idea is simple: when acquiring the lock, keep trying `test_and_set`; if it returns false (the flag was clear), you got the lock; if it returns true (the flag was already set), someone else holds it, so keep spinning. Releasing the lock is a call to `clear`.

```cpp
#include <atomic>
#include <thread>
#include <iostream>
#include <vector>

class SpinLock {
public:
    void lock()
    {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            // Spin-wait: the CPU is burning cycles
            // On x86 you can insert _mm_pause() to reduce power draw
            // On ARM you can insert __yield()
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_{};
};

// Usage example
SpinLock spinlock;
int shared_counter = 0;

void increment(int times)
{
    for (int i = 0; i < times; ++i) {
        spinlock.lock();
        ++shared_counter;
        spinlock.unlock();
    }
}

int main()
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment, 250000);
    }
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "shared_counter = " << shared_counter << "\n";
    // Output: shared_counter = 1000000
    return 0;
}
```

The drawback of a spinlock is plain to see: while the lock is held, other threads burn CPU cycles for nothing. So spinlocks only fit scenarios with very short critical sections—ideally, the lock is held so briefly that it is released before another thread even gets scheduled away. For longer critical sections, `std::mutex` (an OS-level blocking lock) is the better fit.

C++20 also added `wait()` and `notify_one()`/`notify_all()` to `atomic_flag`, letting a spinlock evolve into a more efficient "waiting lock"—on a failed acquisition the thread no longer spins but is suspended instead, to be woken when the lock is released. Under the hood it uses `futex` on Linux and `WaitOnAddress` on Windows, saving far more CPU than pure spinning.

## Common Misconceptions

Before wrapping up, let's run quickly through a few traps that are easy to step into.

First misconception: believing that atomic variables can solve every race condition. Atomic operations guarantee the atomicity of a **single access**, but not the atomicity **between multiple atomic operations**. For example:

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// Thread 1
x.store(1);
y.store(2);

// Thread 2
int a = y.load();
int b = x.load();
```

Even though the `load`/`store` on `x` and on `y` are each atomic, thread 2 can still observe `a == 2` while `b == 0`—there is no synchronization between the two stores, nor between the two loads. Atomicity cannot fix this; it takes memory ordering to constrain. We will expand on this in detail in the next article.

Second misconception: assuming that `volatile` is equivalent to `std::atomic`. The semantics of `volatile` are "do not optimize away accesses to this variable"—every read and write genuinely touches memory, nothing gets cached. But `volatile` **guarantees neither atomicity nor memory ordering**. `++counter` on a `volatile int counter;` is still a three-step read-modify-write, and it still races. `volatile` was designed for memory-mapped hardware registers and signal handlers, not for multithreading.

Third misconception: using `std::atomic` on a non-trivially-copyable type such as `std::atomic<std::string>`. The standard does not allow it—the compiler rejects the code outright. `std::string` has a user-defined copy constructor (with heap allocation involved inside), so it fails the trivially copyable requirement. If you need to share a string atomically, use `std::atomic<std::shared_ptr<std::string>>` (supported since C++20) or protect it with a mutex.

## Run It Online

Try atomic load/store, fetch_add, compare_exchange, and the atomic_flag spinlock primitive online:

<OnlineCompilerDemo
  title="Atomic Operations"
  source-path="code/examples/vol5/11_atomic.cpp"
  description="Explore atomic load/store, fetch_add, compare_exchange_strong, and atomic_flag"
  allow-run
  allow-x86-asm
/>

## Exercises

### Exercise 1: A Lock-Free Counter

Implement a multithread-safe counter with `std::atomic<int>`. Launch 8 threads, each incrementing the counter 100000 times; the final result should be 800000. Test two implementations—one with `fetch_add`, one with a `compare_exchange_weak` loop—and compare the two for correctness and performance differences.

Hint: the idea behind implementing `fetch_add` with `compare_exchange_weak` is—read the current value, compute the new value, CAS to try to replace it, and retry on failure.

### Exercise 2: A Lock-Free Max Tracker

Implement a thread-safe max tracker: multiple threads keep writing random values, and the tracker always records the maximum of every value written. You must implement it with `compare_exchange_strong` (not `fetch_add`).

Hint: on failure, the `expected` parameter of `compare_exchange_strong` is updated to the current value—in that "failure" branch you need to compare the current value against your candidate new value and decide whether to retry.

```cpp
class MaxTracker {
public:
    void update(int new_value)
    {
        int current = max_.load(std::memory_order_relaxed);
        while (new_value > current) {
            if (max_.compare_exchange_strong(
                    current, new_value, std::memory_order_relaxed)) {
                break;  // updated successfully
            }
            // Failed: current was updated to the latest value; keep comparing
        }
    }

    int get() const
    {
        return max_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> max_{std::numeric_limits<int>::min()};
};
```

Once the `update` function above is complete, test it with multiple threads: create 8 threads, each generating 100000 random values and calling `update`, then verify that `get()` really returns the maximum of all values generated by all threads.

> 💡 The complete example code lives in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) under `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic)
- [std::atomic_flag — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [compare_exchange_weak vs compare_exchange_strong — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Concurrency in Action, 2nd Edition — Anthony Williams](https://www.cplusplus.com/reference/atomic/atomic/)
- [atomic is_lock_free — cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)
