---
title: atomic operation
description: 'Complete operation manual for std::atomic<T>: load/store, fetch_add,
  compare_exchange, and lock-free determination'
chapter: 3
order: 1
tags:
- host
- cpp-modern
- intermediate
- atomic
difficulty: intermediate
platform: host
reading_time_minutes: 22
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- latch、barrier 与 semaphore
related:
- 内存序详解
- 原子操作模式
translation:
  source: documents/vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md
  source_hash: dc06bd976c7e03b3be88b94c28c35be66ac78a049e3ff700d91050ffdb32f196
  translated_at: '2026-06-15T09:26:55.817508+00:00'
  engine: anthropic
  token_count: 4110
---
# Atomic Operations

So far, the synchronization primitives we have discussed—mutex, condition variable, latch, barrier, and semaphore—essentially follow the "lock, operate, unlock" philosophy. They are safe and intuitive, but they share a common cost: even if you only want to protect a simple integer increment, you must go through the full lock → modify → unlock cycle. For operations with such fine granularity, like "modifying a variable," the weight of this process can feel mismatched.

`std::atomic` is designed specifically for these "fine-grained" scenarios. It does not rely on locks (at least ideally), but instead uses CPU-provided atomic instructions to guarantee that operations are indivisible. In the previous article, we used `std::atomic` to fix a data race in our discussion of basic concurrency issues, but we only scratched the surface. In this article, we will fully decompose all operations of `std::atomic`—from the most basic `load`/`store`, to the CAS (Compare-And-Swap) mechanism, and finally to lock-free determination and the specialized type `std::atomic_flag`. We will discuss memory ordering in the next article; for now, let's focus on "what atomic operations can do."

## Which types does `std::atomic<T>` support?

`std::atomic` is a class template defined in the `<atomic>` header file. Not all types can be used with `std::atomic`—the standard places explicit limits on this.

For integral types—`char`, `short`, `int`, `long`, `long long`, and their unsigned variants—the standard library provides explicit specializations of `std::atomic` that support full arithmetic and bitwise atomic operations (`fetch_add`, `fetch_sub`, `fetch_or`, `fetch_and`, `fetch_xor`). Pointer types are similarly specialized, supporting `fetch_add` and `fetch_sub` for atomically moving pointers.

For custom types `T`, `std::atomic<T>` also exists, provided `T` meets a core condition: `std::is_trivially_copyable<T>` is true. This means `T` cannot have user-provided copy constructors/assignment (default ones are fine), virtual functions, virtual base classes, etc. Custom types meeting this condition can use generic operations like `load`, `store`, `exchange`, and `compare_exchange`, but cannot use arithmetic operations like `fetch_add`—the standard is not obligated to define "addition" semantics for your custom type.

Note that these generic operations have additional requirements on `T`. `load` requires `T` to be CopyConstructible, `store` requires `T` to be CopyAssignable, and `exchange` and `compare_exchange` require both. However, since `T` is trivially copyable, these requirements are almost always automatically met. Additionally, the default constructor `atomic()` performs value initialization on `T` prior to C++20 (requiring `T` to be default constructible), but since C++20 it leaves it uninitialized. If you use the parameterized constructor `atomic(T)`, `T` does not need to be default constructible.

```cpp
struct MyData {
    int x, y;
};
static_assert(std::is_trivially_copyable_v<MyData>); // Required for std::atomic<MyData>

std::atomic<MyData> data;
MyData local = data.load(); // OK
data.store({1, 2});         // OK
// data.fetch_add(...);      // Error: MyData does not support arithmetic
```

It is worth noting that since C++20, the standard explicitly supports `std::atomic<float>` and `std::atomic<double>`, providing `fetch_add` and `fetch_sub` specializations for floating-point types. Before C++20, floating-point atomic variables could only be `load`, `store`, `exchange`, and `compare_exchange`—direct atomic addition/subtraction was not possible. We will discuss the caveats of floating-point atomic operations later.

## `load()` and `store()`: The foundation of atomic read/write

`load` and `store` are the most basic pair of atomic operations. All atomic reads and writes ultimately boil down to these two operations (plus an optional memory order parameter). If no memory order is specified, all atomic operations default to `std::memory_order_seq_cst`—the strongest ordering guarantee. We will expand on the specific meaning of memory orders in the next article; for now, just remember: the default parameters are safe, though not necessarily the fastest.

```cpp
std::atomic<int> counter{0};

// Explicit load/store
int old_val = counter.load(std::memory_order_relaxed);
counter.store(old_val + 1, std::memory_order_relaxed);

// Implicit conversion (uses load)
int current = counter;
```

Don't rush to use the convenient shorthand just yet. While `int current = counter` looks like a normal variable copy, behind the scenes it is an atomic load. Mixing implicit conversions in complex expressions can sometimes obscure the code's intent—is this a normal assignment or an atomic read? In collaborative development, the author prefers explicitly calling `load` and `store`. While it requires typing a few more characters, it makes it immediately obvious that we are operating on an atomic variable.

## `fetch_add`, `fetch_sub`, and bitwise operations: Atomic arithmetic

For integral and pointer types, `std::atomic` provides a set of `fetch` operations. They execute the "read current value → perform operation → write back new value" Read-Modify-Write (RMW) sequence, guaranteeing that this sequence is atomic—no intermediate state can be observed by other threads.

The return value of `fetch` operations is the **old value** (before modification), not the new value. This is a very pragmatic design choice: returning the old value allows you to accomplish both "reading current state" and "modifying state" in one shot, which is extremely convenient when implementing lock-free algorithms.

```cpp
std::atomic<int> value{10};

// Returns 10, value becomes 15
int old = value.fetch_add(5);

// Returns 15, value becomes 10
int old2 = value.fetch_sub(5);
```

These operations also have corresponding compound assignment and increment/decrement operator overloads, but note that the operator overloads return the **new value** (specifically, the value after the operation is applied), not the old value—this is the opposite of the `fetch` series:

```cpp
std::atomic<int> counter{0};

// Returns 1 (new value), counter becomes 1
int result = ++counter;

// Returns 1 (old value), counter becomes 2
int result2 = counter++;
```

I want to emphasize a confusing detail here: `counter++` (post-increment) and `counter.fetch_add(1)` do not have exactly the same effect. `counter++` returns the value **before** the increment, which is indeed consistent with `fetch_add(1)`. However, `++counter` (pre-increment) returns the value **after** the increment, which is equivalent to `counter.fetch_add(1) + 1`. In scenarios where the return value is not needed (e.g., a pure counter increment), it doesn't matter which one you use; but if you use the return value in an expression, this distinction is crucial.

## Caveats for floating-point atomic operations

This is a problem many encounter the first time they use `std::atomic<float>`. While C++20 provides `fetch_add` and `fetch_sub` for floating-point specializations, there are two levels of specificity to be aware of.

At the hardware level, most CPU architectures do not provide atomic floating-point addition instructions. x86 has the `lock add` instruction for integer atomic addition, but floating-point addition goes through the FPU/SSE/AVX execution units, which are not designed for atomic operations in the first place. Therefore, `atomic<float>::fetch_add` internally degrades into a CAS loop on most platforms—there is no hardware-level atomic floating-point addition.

At the semantic level, floating-point addition is not associative—`(a + b) + c` does not always equal `a + (b + c)` because each operation involves precision rounding. This means that even if multiple threads perform `fetch_add` on a floating-point atomic variable simultaneously, the final result depends on the execution order of the operations, and this order is non-deterministic. Furthermore, the results of floating-point operations may vary depending on the floating-point environment (rounding mode, precision control), bringing additional non-reproducibility to the semantics of `fetch_add`.

If you need to atomically modify a floating-point variable in a pre-C++20 environment, or if you need to avoid the reproducibility issues of `fetch_add` precision, the standard approach is to use a CAS loop:

```cpp
std::atomic<double> shared_value{0.0};

void add_to_value(double delta) {
    double expected = shared_value.load();
    while (!shared_value.compare_exchange_weak(expected, expected + delta)) {
        // expected is updated by compare_exchange_weak on failure
    }
}
```

We will see this pattern again in the CAS section—it is the cornerstone of lock-free programming.

## `compare_exchange_weak` vs `compare_exchange_strong`: The CAS mechanism

Compare-And-Swap (CAS) is the most important primitive in atomic operations, hands down. Almost all lock-free data structure implementations are built on CAS. C++ provides two variants: `compare_exchange_weak` and `compare_exchange_strong`, and their difference is subtle but critical.

Let's look at the interface. Both signatures are identical:

```cpp
bool compare_exchange_weak(T& expected, T desired,
    std::memory_order success = std::memory_order_seq_cst,
    std::memory_order failure = std::memory_order_seq_cst);
```

The execution logic is this: atomically compares the current value with `expected`. If they are equal, it replaces the current value with `desired` and returns `true`; if not equal, it loads the current value into `expected` and returns `false`. Note that on failure, `expected` is overwritten—this is an easily overlooked detail. If you need to use the original `expected` value later, remember to back it up.

The difference lies in "spurious failure": `compare_exchange_weak` may return `false` even if the current value equals `expected`. This is not a bug, but a hardware limitation. On architectures like ARM and PowerPC that implement CAS using LL/SC (Load-Linked/Store-Conditional) primitives, the SC instruction may fail for various reasons—another processor touched the same cache line, an interrupt occurred, or even purely due to scheduling events. x86 uses the hardware `lock cmpxchg` instruction and does not have this problem, so on x86, `weak` and `strong` generate identical code.

```cpp
std::atomic<int> value{0};
int expected = 0;

// Weak version: May fail spuriously
while (!value.compare_exchange_weak(expected, 1)) {
    // expected is updated to the current value on failure
}

// Strong version: Only fails if values differ
while (!value.compare_exchange_strong(expected, 1)) {
    // expected is updated to the current value on failure
}
```

When should you use `weak` vs `strong`? The rule is simple: if your CAS is already wrapped in a loop, use `weak`—a spurious failure just means one extra iteration, but `weak` avoids the internal retry loop on LL/SC architectures, making it faster overall. If you are doing a one-shot CAS (not in a loop), use `strong`—otherwise, a single spurious failure could send your logic down the wrong branch.

### Implementing a lock-free stack push with CAS

Let's look at a classic CAS application scenario—the push operation for a lock-free stack. This example demonstrates the usage of `compare_exchange_weak` in a loop:

```cpp
struct Node {
    int data;
    Node* next;
};

std::atomic<Node*> head{nullptr};

void push(int new_data) {
    Node* new_node = new Node{new_data, nullptr};

    // new_node->next points to the current head
    new_node->next = head.load(std::memory_order_relaxed);

    // If head is still what we think it is, swap it to new_node
    while (!head.compare_exchange_weak(new_node->next, new_node,
        std::memory_order_release,
        std::memory_order_relaxed)) {
        // If CAS fails, new_node->next is automatically updated
        // to the current head. We just retry.
    }
}
```

The logic here is: read the current `head`, point the new node's `next` to it, and then try to swap `head` to the new node with one CAS. If another thread pushes a node (changing `head`) while we are preparing the new node, the CAS fails, `new_node->next` is updated to the latest `head`, and we reset `new_node->next` and try again. This process repeats until CAS succeeds.

You might notice that `compare_exchange_weak` here accepts two memory order parameters: `success` and `failure`. On success, we use `memory_order_release` (because we just wrote a new node and need to ensure other threads see the complete data). On failure, we use `memory_order_relaxed` (if it fails, no synchronization guarantees are needed, we are just retrying).

## `exchange()`: Atomic swap

`exchange` is a relatively simple but very practical operation: atomically writes a new value in while taking the old value out. It is a combination of `store` and `load`, but it guarantees that these two steps are indivisible.

```cpp
std::atomic<int> status{0};

// Writes 1, returns the old value 0
int old_status = status.exchange(1);
```

A typical use case for `exchange` is "state handover"—atomically switching a state from A to B while deciding subsequent behavior based on the old state:

```cpp
enum State { Idle, Running, Stopped };
std::atomic<State> current_state{State::Idle};

void stop() {
    // Switch to Stopped, check what state we were in
    State prev = current_state.exchange(State::Stopped);
    if (prev == State::Idle) {
        // Was idle, cleanup not needed
    } else if (prev == State::Running) {
        // Was running, need cleanup
    }
}
```

Note that this example could be written more precisely with CAS (`compare_exchange` checks the old state before swapping, whereas `exchange` swaps unconditionally even if the old state isn't what you expected). However, the advantage of `exchange` lies in its simplicity—if you just want to swap a value in and know what the old value was, `exchange` is much more concise than a CAS loop.

## `is_lock_free` and `is_always_lock_free`

We have been saying "atomic operations don't use locks," but that is not always the case. Whether `std::atomic` is truly lock-free depends on two factors: the size of type `T` and the hardware capabilities of the target platform. If the hardware lacks atomic instructions of the corresponding width (e.g., atomic operations on 64-bit integers on 32-bit ARM), the compiler will settle for the next best thing: implementing it with internal locks. In this case, `std::atomic` operations are not truly lock-free.

The standard library provides two interfaces to query this. `is_lock_free()` is a runtime query returning `true` if operations on the current object are lock-free. `is_always_lock_free` is a compile-time constant (`constexpr`) returning `true` if atomic operations of this type are lock-free for **all** instances on this platform. If you need to make a static assertion at compile time, use `is_always_lock_free`; if you need to make a branch judgment at runtime, use `is_lock_free()`.

```cpp
std::atomic<int> int_atom;
std::atomic<long long> ll_atom;

if (int_atom.is_lock_free()) {
    // int operations are lock-free at runtime
}

if constexpr (std::atomic<long long>::is_always_lock_free) {
    // long long operations are guaranteed lock-free at compile time
}
```

In actual projects, `is_always_lock_free` is more valuable than `is_lock_free()`. The reason is: if your code path branches based on the return value of `is_lock_free()`, it means the same code might take different paths on different runtime instances—a nightmare for testing and debugging. In contrast, `static_assert` + `is_always_lock_free` exposes the problem at compile time: either the platform fully supports lock-free, or the code fails to compile, leaving no gray area.

In embedded scenarios, this is particularly important. On 32-bit ARM Cortex-M, `std::atomic<int>` is almost always lock-free (hardware has `LDREX`/`STREX` instruction pairs), but `std::atomic<long long>` may not be on Cortex-M0/M3. If you use atomic operations in an ISR, make sure they are lock-free—ISRs cannot block, and lock-based atomic operations will block.

## `atomic_flag`: The standard-guaranteed lock-free primitive

Whether `std::atomic` is lock-free depends on the platform, but `std::atomic_flag` is an exception—the standard guarantees that `std::atomic_flag` **is always lock-free**. On all platforms, with all compilers, without exception. This makes `std::atomic_flag` the most reliable cornerstone for building low-level synchronization primitives (like spinlocks).

`std::atomic_flag` has only two states: set (true) and clear (false). It provides three core operations: `test_and_set` atomically sets the flag to true and returns the previous value; `clear` atomically sets the flag to false; and C++20 added `test` for atomically reading the current value without modifying it.

```cpp
std::atomic_flag flag = ATOMIC_FLAG_INIT; // Initialize to clear

// Set to true, return previous value (false)
bool was_set = flag.test_and_set();

// Set to false
flag.clear();

// C++20: Read current value
bool is_set = flag.test();
```

### Implementing a spinlock with `atomic_flag`

The most classic application of `std::atomic_flag` is a spinlock. The principle of a spinlock is simple: when acquiring the lock, keep trying `test_and_set`. If it returns false (was in clear state), you successfully acquired the lock; if it returns true (was already in set state), the lock is held by someone else, so keep spinning. When releasing the lock, call `clear`.

```cpp
class SpinLock {
    std::atomic_flag flag = ATOMIC_FLAG_INIT;
public:
    void lock() {
        // Spin until we successfully set the flag from false to true
        while (flag.test_and_set(std::memory_order_acquire)) {
            // Optional: CPU pause hint (e.g., _mm_pause() on x86)
        }
    }
    void unlock() {
        flag.clear(std::memory_order_release);
    }
};
```

The downside of a spinlock is obvious: other threads are spinning while the lock is held, wasting CPU time in vain. Therefore, spinlocks are only suitable for scenarios with extremely short critical sections—ideally, the lock hold time should be so short that "the other thread hasn't had time to be scheduled away before it's released." If the critical section is relatively long, `std::mutex` (an OS-level blocking lock) is more appropriate.

C++20 also added `wait` and `notify`/`notify_one` operations to `atomic_flag`, allowing the spinlock to evolve into a more efficient "wait lock"—instead of spinning when acquisition fails, the thread is suspended and woken up when the lock is released. Under the hood, it uses `futex` on Linux and `WaitOnAddress` on Windows, saving much more CPU than pure spinning.

## Common misconceptions

Before we wrap up, let's quickly go over a few common pitfalls.

The first misconception: thinking atomic variables solve all race conditions. Atomic operations guarantee the atomicity of a **single access**, but they do not guarantee atomicity **between multiple atomic operations**. For example:

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// Thread 1
x.store(1, std::memory_order_relaxed);
y.store(1, std::memory_order_relaxed);

// Thread 2
int r1 = y.load(std::memory_order_relaxed); // Might see 1
int r2 = x.load(std::memory_order_relaxed); // Might see 0
```

Even though `x` and `y`'s individual `store`/`load` are atomic, Thread 2 might still see `y` as 1 but `x` as 0—because there is no synchronization relationship between the two `store`s or between the two `load`s. This is not something atomic operations can solve; it requires memory ordering to constrain. We will expand on this topic in the next article.

The second misconception: thinking `volatile` is equivalent to `std::atomic`. The semantics of `volatile` are "do not optimize accesses to this variable"—every read/write actually touches memory, no caching. However, `volatile` **guarantees neither atomicity nor memory ordering**. `++` on a `volatile int` is still a three-step read-modify-write operation and can still have a data race. `volatile` was designed for memory-mapped hardware registers and signal handlers, not for multithreading.

The third misconception: using `std::atomic` on non-trivially-copyable types like `std::string`. The standard does not allow this—the compiler will error out directly. `std::string` has user-defined copy constructors (involving heap memory allocation internally) and does not meet the trivially copyable requirement. If you need to share strings atomically, use `std::atomic<std::shared_ptr<std::string>>` (supported since C++20) or protect it with a mutex.

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

Implement a multithread-safe counter using `std::atomic<int>`. Requirements: Start 8 threads, each incrementing the counter 100,000 times. The final result should be 800,000. Test both `fetch_add` and a `compare_exchange` loop implementation, and compare their correctness and performance differences.

**Hint:** The idea of using `compare_exchange` to implement `fetch_add` is—read the current value, calculate the new value, try to replace with CAS, and retry on failure.

### Exercise 2: Lock-free maximum tracker

Implement a thread-safe maximum tracker: multiple threads continuously write random values, and the tracker always records the maximum value among all written values. Requirements: Use `compare_exchange_strong` (not `compare_exchange_weak`).

**Hint:** The `expected` parameter of `compare_exchange_strong` is updated to the current value on failure—you need to compare this current value with your candidate new value in this "failure" branch to decide whether a retry is needed.

```cpp
class MaxTracker {
    std::atomic<int> max_val;
public:
    MaxTracker() : max_val(0) {}

    void update(int candidate) {
        // TODO: Implement this
    }

    int get_max() const {
        return max_val.load();
    }
};
```

After completing the `update` function above, test it with multiple threads: create 8 threads, each generating 100,000 random values and calling `update`, and finally verify that `get_max` returns the maximum value among all generated values.

> 💡 Complete example code is available at [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/examples/vol5/11_atomic.cpp`.

## References

- [std::atomic -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic)
- [std::atomic_flag -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [compare_exchange_weak vs compare_exchange_strong -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams](https://wwwcpluspluscom/reference/atomic/atomic/)
- [atomic is_lock_free -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)
