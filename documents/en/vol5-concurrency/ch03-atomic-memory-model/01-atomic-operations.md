---
chapter: 3
cpp_standard:
- 11
- 14
- 17
- 20
description: 'Complete guide to std::atomic<T>: load/store, fetch_add, compare_exchange,
  and lock-free detection'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- latch、barrier 与 semaphore
reading_time_minutes: 20
related:
- 内存序详解
- 原子操作模式
tags:
- host
- cpp-modern
- intermediate
- atomic
title: atomic operation
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md
  source_hash: 026f0fed36a121949b421472daaa505042017d8db2e1a2e3d4cdf9822a9b6e5a
  translated_at: '2026-06-16T04:03:59.408630+00:00'
  engine: anthropic
  token_count: 4105
---
# Atomic Operations

So far, the synchronization primitives we have discussed—mutex, condition variable, latch, barrier, and semaphore—essentially follow the "lock, operate, unlock" philosophy. They are safe and intuitive, but they share a common cost: even if you only want to protect a simple integer increment, you must go through the full lock → modify → unlock cycle. For operations with such fine granularity, like "modifying a variable," this process feels disproportionately heavy.

`std::atomic` is designed for these "minimal granularity" scenarios. It does not rely on locks (at least ideally), but instead utilizes atomic instructions provided directly by the CPU to guarantee that operations are indivisible. In the previous article, we used `std::atomic` to fix data races in our discussion of basic concurrency issues, but we only scratched the surface. In this article, we will completely dissect all `std::atomic` operations—from the most basic `load`/`store`, to the CAS (Compare-And-Swap) mechanism, and finally to lock-free determination and the specialized type `std::atomic_flag`. We will discuss memory ordering in the next article; for now, let's focus on "what atomic operations can do."

## Which types does std::atomic<T> support?

`std::atomic` is a class template defined in the `<atomic>` header file. Not all types can be used with `std::atomic`—the standard places explicit restrictions on this.

For integral types—`char`, `short`, `int`, `long`, `long long`, and their unsigned variants—the standard library provides explicit specializations of `std::atomic` that support full arithmetic and bitwise atomic operations (`fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor`). Pointer types are similarly specialized, supporting `fetch_add` and `fetch_sub` to atomically move pointers.

For custom types `T`, `std::atomic` can also be used, provided `T` meets a core condition: `std::is_trivially_copyable_v` must be true—meaning `T` cannot have user-provided copy constructors/assignment (a compiler-generated default is fine), virtual functions, virtual base classes, etc. Custom types meeting this condition can use generic operations like `load`, `store`, `exchange`, and `compare_exchange`, but cannot use arithmetic operations like `fetch_add`—the standard has no obligation to define "addition" semantics for your custom type.

Note that these generic operations impose additional requirements on `T`: `store` requires `T` to be CopyConstructible, `load` requires `T` to be CopyAssignable, and `exchange` and `compare_exchange` require both. However, since `T` is trivially copyable, these requirements are almost always automatically satisfied. Additionally, the default constructor `atomic()` performs value initialization on `T` prior to C++20 (requiring `T` to be default constructible), but from C++20 onwards it leaves it uninitialized—if you use the constructor with parameters like `atomic(T desired)`, `T` does not need to be default constructible.

```cpp
std::atomic<MyStruct> a; // C++20: uninitialized, C++17: zero-initialized
std::atomic<MyStruct> b(MyStruct{}); // Initialized with a value
```

It is worth noting that C++20 explicitly supports `std::atomic<float>` and `std::atomic<double>`, providing `fetch_add` and `fetch_sub` for floating-point specializations. Before C++20, floating-point atomic variables could only `load`, `store`, `exchange`, or `compare_exchange`—direct atomic addition or subtraction was not possible. We will discuss the caveats of floating-point atomic operations later.

## load() and store(): The foundation of atomic read/write

`load` and `store` are the most basic pair of atomic operations. All atomic reads and writes ultimately boil down to these two operations (plus an optional memory order parameter). If no memory order is specified, all atomic operations default to `std::memory_order_seq_cst`—the strongest ordering guarantee. We will expand on the specific meaning of memory orders in the next article; for now, just remember: the default parameters are safe, though not necessarily the fastest.

```cpp
std::atomic<int> a{0};
int local = a.load();            // Read
a.store(10);                     // Write
```

Don't rush to use the convenient syntax just yet. `int local = a;` looks like a normal variable copy, but behind the scenes, it is an atomic load. Mixing implicit conversions in complex expressions can sometimes obscure the intent of the code—is this a normal assignment or an atomic read? In team collaboration, the author prefers explicitly calling `load` and `store`. While it requires typing a few more characters, it makes it immediately obvious that we are operating on an atomic variable.

## fetch_add, fetch_sub, and bitwise operations: Atomic arithmetic

For integral and pointer types, `std::atomic` provides a set of `fetch` operations. They execute the entire Read-Modify-Write (RMW) sequence of "read current value → perform calculation → write back new value," and guarantee that this sequence is atomic—no intermediate state can be observed by other threads.

The return value of the `fetch` series is the **old value before modification**, not the new value. This is a very pragmatic design choice: returning the old value allows you to complete both "read current state" and "modify state" in one shot, which is extremely convenient when implementing lock-free algorithms.

```cpp
std::atomic<int> counter{0};
int old_val = counter.fetch_add(1); // Returns 0, counter becomes 1
```

These operations also have corresponding compound assignment and increment/decrement operator overloads, but note that the operator overloads return the **new value** (specifically, the value after the operation is applied), not the old value—this is the opposite of the `fetch` series:

```cpp
std::atomic<int> counter{0};
int a = ++counter;    // prefix: returns 1 (new value)
int b = counter++;    // postfix: returns 0 (old value)
```

I want to emphasize a confusing detail here: `counter++` (postfix increment) and `counter.fetch_add(1)` do not have exactly the same effect. `counter++` returns the value **before** the increment, which is indeed consistent with `fetch_add(1)`. However, `++counter` (prefix increment) returns the value **after** the increment, which is equivalent to `counter.fetch_add(1) + 1`. In scenarios where the return value is not needed (e.g., pure increment counting), it doesn't matter which one you use; but if you use the return value in an expression, this distinction is crucial.

## Caveats for floating-point atomic operations

This is a problem many encounter the first time they use `std::atomic` with floating-point numbers. While C++20 provides `fetch_add` and `fetch_sub` for floating-point specializations, there are two levels of specificity to be aware of.

At the hardware level, the vast majority of CPU architectures do not provide atomic floating-point addition instructions. x86 has the `LOCK ADD` instruction for integer atomic addition, but floating-point addition goes through the FPU/SSE/AVX execution units, which are not designed for atomic operations in the first place. Therefore, `fetch_add` on most platforms internally degrades into a CAS loop—there is no hardware-level atomic floating-point addition.

At the semantic level, floating-point addition is not associative—`(a + b) + c` does not always equal `a + (b + c)`, because each operation involves precision rounding. This means that even if multiple threads perform `fetch_add` on a floating-point atomic variable simultaneously, the final result depends on the execution order of the operations, and this order is non-deterministic. Furthermore, the results of floating-point operations may vary depending on the floating-point environment (rounding mode, precision control), bringing additional irreproducibility to the semantics of `fetch_add`.

If you need to modify floating-point variables atomically in a pre-C++20 environment, or if you need to avoid the reproducibility issues of `fetch_add` precision, the standard approach is to use a CAS loop:

```cpp
std::atomic<double> value{0.0};
double desired = 1.5;
double expected = value.load();
while (!value.compare_exchange_strong(expected, desired)) {
    expected = value.load(); // expected has been updated to the actual value
}
```

We will see this pattern again in the CAS section—it is the cornerstone of lock-free programming.

## compare_exchange_weak and compare_exchange_strong: The CAS mechanism

Compare-And-Swap (CAS) is the single most important primitive in atomic operations. Almost all lock-free data structure implementations are built on top of CAS. C++ provides two variants: `compare_exchange_weak` and `compare_exchange_strong`, and their difference is subtle but critical.

Let's look at the interface. Their signatures are identical:

```cpp
bool compare_exchange_weak(T& expected, T desired,
    std::memory_order success = std::memory_order_seq_cst,
    std::memory_order failure = std::memory_order_seq_cst);
```

The execution logic is: atomically compare the current value with `expected`. If they are equal, replace the current value with `desired` and return `true`; if not equal, load the current value into `expected` and return `false`. Note that on failure, `expected` is overwritten—this is an easily overlooked detail. If you need to use the original `expected` value later, remember to back it up.

The difference lies in "spurious failure": `compare_exchange_weak` may return `false` even if the current value is equal to `expected`. This is not a bug, but a hardware limitation. On architectures like ARM and PowerPC that use LL/SC (Load-Linked/Store-Conditional) primitives to implement CAS, the SC instruction may fail for various reasons—another processor touched the same cache line, an interrupt occurred, or even purely due to scheduling events. x86 uses the hardware `LOCK CMPXCHG` instruction and does not have this problem, so on x86, `weak` and `strong` generate identical code.

```cpp
std::atomic<int> a{0};
int expected = 0;
// weak version: may fail spuriously
while (!a.compare_exchange_weak(expected, 1)) {
    // expected is updated to the current value of a
}
```

When should you use `weak` vs. `strong`? The rule is simple: if your CAS is already wrapped in a loop, use `weak`—a spurious failure just means one extra iteration, but `weak` avoids the internal retry loop on LL/SC architectures, making it faster overall. If you are doing a one-shot CAS (not in a loop), use `strong`—otherwise, a single spurious failure could lead your logic down the wrong branch.

### Implementing lock-free stack push with CAS

Let's look at a classic CAS application scenario—the push operation for a lock-free stack. This example demonstrates the usage of `compare_exchange_weak` in a loop:

```cpp
template <typename T>
class LockFreeStack {
    struct Node {
        T data;
        Node* next;
    };
    std::atomic<Node*> head;
public:
    void push(const T& value) {
        Node* node = new Node{value, nullptr};
        node->next = head.load(std::memory_order_relaxed);

        while (!head.compare_exchange_weak(
            node->next,       // expected (updated on failure)
            node,             // desired
            std::memory_order_release, // success memory order
            std::memory_order_relaxed  // failure memory order
        )) {
            // If CAS fails, node->next is automatically updated
            // to the latest head, just retry.
        }
    }
};
```

The logic here is: read the current `head`, point the new node's `next` to it, and then try to swap `head` with the new node using CAS. If another thread pushes a node (changing `head`) while we are preparing the new node, the CAS fails, `node->next` is updated to the latest `head`, we reset `node->next` and try again. This process repeats until CAS succeeds.

You might notice that `compare_exchange_weak` accepts two memory order parameters here: `success` and `failure`. On success, we use `release` (because we just wrote a new node and need to ensure other threads see the complete data). On failure, we use `relaxed` (failure requires no synchronization guarantees, it's just a retry).

## exchange(): Atomic swap

`exchange` is a relatively simple but very practical operation: atomically write a new value in while taking the old value out. It is a combination of `store` and `load`, but it guarantees that these two steps are indivisible.

```cpp
std::atomic<int> state{0};
int old_state = state.exchange(1); // Returns 0, state becomes 1
```

A typical use case for `exchange` is "state handover"—atomically switching a state from A to B while deciding subsequent behavior based on the old state:

```cpp
enum State { Idle, Busy, Error };
std::atomic<State> server_state{Idle};

void handle_request() {
    State old = server_state.exchange(Busy);
    if (old == Error) {
        // Handle error recovery logic
    }
    // ... process request ...
    server_state.store(Idle);
}
```

Note that this example could actually be written more precisely with CAS (`compare_exchange` would unconditionally write the new value even if the old state wasn't `Idle`), but the advantage of `exchange` lies in its simplicity—if you just want to swap a value in and know what the old value was, `exchange` is much more concise than a CAS loop.

## is_lock_free and is_always_lock_free

We have been saying "atomic operations don't rely on locks," but that is not always the case. Whether `std::atomic` is truly lock-free depends on two factors: the size of type `T` and the hardware capabilities of the target platform. If the hardware lacks atomic instructions for the corresponding width (e.g., atomic operations on 64-bit integers on 32-bit ARM), the compiler will settle for the next best thing: implementing it with internal locks. In this case, `std::atomic` operations are not truly lock-free.

The standard library provides two interfaces to query this. `is_lock_free()` is a runtime query returning `true` if operations on the current object are lock-free. `is_always_lock_free` is a compile-time constant (`constexpr`) returning `true` if atomic operations for this type are lock-free for **all** instances on this platform. If you need to make static assertions at compile time, use `is_always_lock_free`; if you need to make branching decisions at runtime, use `is_lock_free()`.

```cpp
std::atomic<int> a;
if (a.is_lock_free()) {
    // Use lock-free algorithm
} else {
    // Fallback to mutex-based implementation
}
```

In actual projects, `is_always_lock_free` is more valuable than `is_lock_free()`. The reason is: if your code path has a branch dependent on the return value of `is_lock_free()`, it means the same code might take different paths on different running instances—this is a nightmare for testing and debugging. In contrast, `is_always_lock_free` + `static_assert` can expose the problem at compile time: either the platform fully supports lock-free, or the code fails to compile; there is no gray area.

In embedded scenarios, this is particularly important. On 32-bit ARM Cortex-M, `std::atomic<int>` is almost always lock-free (hardware has `LDREX`/`STREX` instruction pairs), but `std::atomic<double>` may not be on Cortex-M0/M3. If you use atomic operations in an ISR, be sure to confirm they are lock-free—ISRs cannot block, and lock-based atomic operations will block.

## atomic_flag: The standard guaranteed lock-free primitive

Whether `std::atomic` is lock-free depends on the platform, but `std::atomic_flag` is an exception—the standard guarantees that `std::atomic_flag` **is always lock-free**. On all platforms, with all compilers, without exception. This makes `std::atomic_flag` the most reliable cornerstone for building low-level synchronization primitives (like spinlocks).

`std::atomic_flag` has only two states: set (true) and clear (false). It provides three core operations: `test_and_set` atomically sets the flag to true and returns the previous value; `clear` atomically sets the flag to false; and C++20 adds `test` for atomically reading the current value without modifying it.

```cpp
std::atomic_flag flag = ATOMIC_FLAG_INIT; // Initialize to clear (false)
if (flag.test_and_set()) {
    // Was already set, now still set
}
flag.clear(); // Set to false
```

### Implementing a spinlock with atomic_flag

The most classic application of `std::atomic_flag` is the spinlock. The principle is simple: when acquiring the lock, keep trying `test_and_set`. If it returns `false` (was previously clear), we successfully acquired the lock; if it returns `true` (was already set), the lock is held by someone else, so we spin. When releasing the lock, call `clear`.

```cpp
class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() {
        while (flag.test_and_set(std::memory_order_acquire)) {
            // Spin: wait until the flag is successfully set
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};
```

The downside of a spinlock is obvious: other threads are spinning (busy-waiting) while the lock is held, wasting CPU time. Therefore, spinlocks are only suitable for scenarios with extremely short critical sections—ideally, the lock hold time should be so short that "the other thread hasn't had time to be scheduled away before it's released." If the critical section is relatively long, using `std::mutex` (an OS-level blocking lock) is more appropriate.

C++20 also adds `wait` and `notify_one`/`notify_all` operations to `std::atomic_flag`, allowing the spinlock to evolve into a more efficient "wait lock"—instead of spinning when acquisition fails, the thread is suspended and woken up when the lock is released. Under the hood, it uses `futex` on Linux and `WaitOnAddress` on Windows, saving much more CPU than pure spinning.

## Common misconceptions

Before we finish, let's quickly go over a few easy pitfalls.

The first misconception: thinking atomic variables solve all race conditions. Atomic operations guarantee the atomicity of a **single access**, but they do not guarantee atomicity **between multiple atomic operations**. For example:

```cpp
std::atomic<int> x{0}, y{0};

// Thread 1
x.store(1);
y.store(1);

// Thread 2
int r1 = y.load();
int r2 = x.load();
// Possible result: r1 == 1, r2 == 0
```

Even though `x` and `y`'s individual `store`/`load` are atomic, Thread 2 might still see `y` as 1 but `x` as 0—because there is no synchronization relationship between the two `store`s or between the two `load`s. This is not something atomic operations can solve; it requires memory ordering to constrain. We will expand on this topic in the next article.

The second misconception: thinking `volatile` is equivalent to `std::atomic`. The semantics of `volatile` are "do not optimize away accesses to this variable"—every read or write actually accesses memory, without caching. However, `volatile` **guarantees neither atomicity nor memory ordering**. `++` on a `volatile int` is still a three-step read-modify-write operation and can still have data races. `volatile` was designed for memory-mapped hardware registers and signal handlers, not for multithreading.

The third misconception: using `std::atomic` on non-trivially-copyable types like `std::string`. The standard does not allow this—the compiler will error out directly. `std::string` has a user-defined copy constructor (involving heap memory allocation internally) and does not meet the trivially copyable requirement. If you need to share strings atomically, use `std::atomic<std::shared_ptr>` (supported from C++20) or protect them with a mutex.

## Run Online

Experience atomic `load`/`store`, `fetch_add`, `compare_exchange`, and `atomic_flag` spinlock primitives online:

<OnlineCompilerDemo
  title="atomic Operations"
  source-path="code/examples/vol5/11_atomic.cpp"
  description="Experience atomic load/store, fetch_add, compare_exchange_strong, and atomic_flag"
  allow-run
  allow-x86-asm
/>

## Exercises

### Exercise 1: Lock-free counter

Implement a multi-thread-safe counter using `std::atomic`. Requirements: Launch 8 threads, each incrementing the counter 100,000 times. The final result should be 800,000. Test both implementations using `fetch_add` and a `compare_exchange` loop, and compare their correctness and performance differences.

**Hint:** The idea of using `compare_exchange` to implement `fetch_add` is—read the current value, calculate the new value, try to replace with CAS, and retry on failure.

### Exercise 2: Lock-free maximum tracker

Implement a thread-safe maximum tracker: multiple threads continuously write random values, and the tracker always records the maximum value among all written values. Requirements: Use `compare_exchange_strong` (not `compare_exchange_weak`).

**Hint:** The `expected` parameter of `compare_exchange_strong` is updated to the current value on failure—you need to compare this current value with your candidate new value in this "failure" branch to decide whether a retry is necessary.

```cpp
class MaxTracker {
    std::atomic<int> max_val;
public:
    MaxTracker() : max_val(0) {}

    void update(int value);
    int get_max() const;
};
```

After completing the `update` function above, test with multiple threads: create 8 threads, each generating 100,000 random values and calling `update`, and finally verify that `get_max` returns the maximum value among all generated values.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/examples/vol5/11_atomic.cpp`.

## References

- [std::atomic -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic)
- [std::atomic_flag -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [compare_exchange_weak vs compare_exchange_strong -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- [atomic is_lock_free -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)
