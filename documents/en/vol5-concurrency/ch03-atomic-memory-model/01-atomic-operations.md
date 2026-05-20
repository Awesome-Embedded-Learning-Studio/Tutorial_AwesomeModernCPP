---
title: atomic operations
description: 'The complete operation manual for `std::atomic<T>`: load/store, fetch_add,
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
  source_hash: e41920c53dc8b4294d149014ca036add1567af8fa53e929b907b0b1b0405b251
  translated_at: '2026-05-20T04:38:05.224013+00:00'
  engine: anthropic
  token_count: 4038
---
# Atomic Operations

So far, the synchronization primitives we have discussed—mutex, condition variable, latch, barrier, and semaphore—all follow the same fundamental pattern: "lock, operate, unlock." They are safe and intuitive, but they share a common cost: even if you only want to protect a simple integer increment, you still pay for the full lock → modify → unlock cycle. For an operation as fine-grained as "modifying a single variable," this overhead feels disproportionate.

`std::atomic` is designed specifically for these fine-grained scenarios. Rather than relying on locks (at least ideally), it leverages atomic instructions provided directly by the CPU to guarantee indivisible operations. In the previous chapter, we used `std::atomic<int>` to fix a data race when covering concurrency fundamentals, but we only scratched the surface. In this chapter, we will fully break down all `std::atomic<T>` operations—from the most basic `load`/`store`, to the CAS (compare-and-swap) mechanism, and finally to lock-free checks and the specialized `atomic_flag` type. We will discuss memory order in the next chapter; for now, let us focus purely on "what atomic operations can do."

## Which Types Does std::atomic<T> Support

`std::atomic` is a class template defined in the `<atomic>` header. Not all types can be placed into `std::atomic`—the standard places explicit restrictions on this.

For integer types—`bool`, `char`, `short`, `int`, `long`, `long long`, and their unsigned variants—the standard library provides explicit specializations of `std::atomic` that support a full set of arithmetic and bitwise atomic operations (`fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor`). Pointer types are similarly specialized, supporting `fetch_add` and `fetch_sub` for atomically advancing pointers.

For custom types `T`, `std::atomic<T>` also exists, provided that `T` satisfies one core requirement: `std::is_trivially_copyable<T>::value` must be true—meaning `T` cannot have user-provided copy constructors/assignment operators (defaulted ones are fine), virtual functions, virtual base classes, and so on. Custom types meeting this condition can use the generic operations `load()`, `store()`, `exchange()`, and `compare_exchange_weak/strong()`, but not arithmetic operations like `fetch_add`—the standard has no obligation to define "addition" semantics for your custom types.

Note that these generic operations impose additional requirements on `T`—`load()` requires `T` to be CopyConstructible, `store()` requires `T` to be CopyAssignable, and `exchange()` and `compare_exchange_*` require both. However, since `T` is trivially copyable, these requirements are almost always automatically satisfied. Additionally, the default constructor `std::atomic<T> a;` performs value initialization on `T` prior to C++20 (so `T` must be default-constructible), but starting from C++20 it leaves the value uninitialized—if you use a parameterized constructor like `std::atomic<T> a{T{...}};`, `T` does not need to be default-constructible.

```cpp
#include <atomic>
#include <iostream>

// 整型：完全支持
std::atomic<int> atomic_int{0};
std::atomic<unsigned long> atomic_ulong{0};

// 指针：支持 fetch_add/fetch_sub（按元素大小偏移）
struct Node {
    int value;
    Node* next;
};
std::atomic<Node*> atomic_head{nullptr};

// 自定义 trivially-copyable 类型：
// 支持 load/store/exchange/CAS，但不支持 fetch_add
struct alignas(8) PacketHeader {
    uint32_t id;
    uint32_t flags;
};
static_assert(std::is_trivially_copyable_v<PacketHeader>);
std::atomic<PacketHeader> atomic_header{PacketHeader{0, 0}};
```

It is worth noting that starting from C++20, the standard explicitly supports `std::atomic<float>` and `std::atomic<double>`, and provides `fetch_add` and `fetch_sub` for floating-point specializations. Prior to C++20, however, floating-point atomic variables could only be `load`, `store`, `exchange`, and `compare_exchange`—direct atomic addition and subtraction were not available. We will discuss the caveats of floating-point atomic operations in detail later.

## load() and store(): The Foundation of Atomic Read-Write

`load()` and `store()` are the most fundamental pair of atomic operations. All atomic reads and writes ultimately boil down to these two operations (plus an optional memory order parameter). When no memory order is specified, all atomic operations default to `memory_order_seq_cst`—the strongest ordering guarantee. We will dive into the specific meanings of memory order in the next chapter; for now, just remember: the default parameter is safe, though not necessarily the fastest.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{0};

    // store：原子地写入一个值
    value.store(42);
    value.store(100, std::memory_order_relaxed);

    // load：原子地读取当前值
    int x = value.load();
    int y = value.load(std::memory_order_relaxed);

    // 便捷写法：隐式转换
    int z = value;       // 等价于 value.load()
    value = 200;         // 等价于 value.store(200)

    std::cout << "value = " << value.load() << "\n";
    return 0;
}
```

Do not rush to use the convenient syntax. `int z = value;` looks like an ordinary variable copy, but behind the scenes it performs an atomic load. Mixing implicit conversions in a complex expression can sometimes obscure the code's intent—is this a normal assignment or an atomic read? In team collaboration, we prefer explicitly calling `load()` and `store()`. Even though it requires a few more keystrokes, it makes it immediately obvious that we are operating on an atomic variable.

## fetch_add, fetch_sub, and Bitwise Operations: Atomic Arithmetic

For integer and pointer types, `std::atomic` provides a family of fetch operations. They execute the entire "read current value → compute → write back new value" Read-Modify-Write (RMW) sequence, and guarantee that this sequence is atomic—no intermediate state can be observed by other threads.

The return value of the fetch family of operations is the **old value prior to modification**, not the new value. This is a highly pragmatic design choice: returning the old value allows you to accomplish both "reading the current state" and "modifying the state" in one step, which is extremely convenient when implementing lock-free algorithms.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> counter{0};

    // fetch_add：原子加法，返回旧值
    int old1 = counter.fetch_add(5);    // counter 变成 5，old1 = 0
    int old2 = counter.fetch_add(3);    // counter 变成 8，old2 = 5

    // fetch_sub：原子减法，返回旧值
    int old3 = counter.fetch_sub(2);    // counter 变成 6，old3 = 8

    // 位运算
    counter.fetch_or(0xFF);    // 按位或
    counter.fetch_and(0xF0);   // 按位与
    counter.fetch_xor(0x0F);   // 按位异或

    std::cout << "counter = " << counter.load() << "\n";
    return 0;
}
```

These operations also have corresponding compound assignment and increment/decrement operator overloads, but note that the operator overloads return the **new value** (specifically, the value after the operation is applied), not the old value—exactly the opposite of the fetch family:

```cpp
std::atomic<int> x{10};

// 运算符重载返回新值
int new_val = ++x;       // x 变成 11，new_val = 11
int old_val = x++;       // x 变成 12，old_val = 11（后置返回旧值）
x += 5;                  // x 变成 17
```

We want to emphasize a detail that is easily confused: `x++` (post-increment) and `x.fetch_add(1)` do not behave exactly the same. `x++` returns the value **before** the increment, which is indeed consistent with `fetch_add(1)`. However, `++x` (pre-increment) returns the value **after** the increment, which is equivalent to `x.fetch_add(1) + 1`. In scenarios where the return value is not needed (such as a pure increment counter), it does not matter which one you use; but if you use the return value in an expression, this distinction is critical.

## Caveats of Floating-Point Atomic Operations

This is a problem many people encounter the first time they use `std::atomic<float>`. Starting from C++20, floating-point specializations do provide `fetch_add` and `fetch_sub`, but there are two layers of特殊性 to be aware of when using them.

At the hardware level, the vast majority of CPU architectures do not provide atomic floating-point addition instructions. x86 has `LOCK XADD` for integer atomic addition, but floating-point addition goes through the FPU/SSE/AVX execution units, which are not designed for atomic operations in the first place. Therefore, on most platforms, `atomic<float>::fetch_add` internally degrades into a CAS loop—there is no hardware-level atomic floating-point addition.

At the semantic level, floating-point addition is not associative—`(a + b) + c` does not equal `a + (b + c)`, because each operation involves precision rounding. This means that even if multiple threads simultaneously perform `fetch_add` on a floating-point atomic variable, the final result depends on the execution order of the operations, and this order is nondeterministic. Furthermore, the results of floating-point operations may vary depending on the floating-point environment (rounding mode, precision control), which introduces additional non-reproducibility to the semantics of `fetch_add`.

If you need to atomically modify a floating-point variable in a pre-C++20 environment, or if you need to avoid the precision non-reproducibility issues of `fetch_add`, the standard approach is to use a CAS loop:

```cpp
#include <atomic>

std::atomic<float> atomic_value{0.0f};

float atomic_fetch_add(float delta)
{
    float old_val = atomic_value.load(std::memory_order_relaxed);
    float new_val;
    do {
        new_val = old_val + delta;
        // 如果 atomic_value 还是 old_val，就把它换成 new_val
        // 否则 old_val 被更新为当前值，重试
    } while (!atomic_value.compare_exchange_weak(
                 old_val, new_val, std::memory_order_relaxed));
    return old_val;
}
```

We will see this pattern again shortly in the CAS section—it is the cornerstone of lock-free programming.

## compare_exchange_weak and compare_exchange_strong: The CAS Mechanism

Compare-And-Swap (CAS) is the most important primitive among atomic operations, bar none. Almost all lock-free data structure implementations are built on top of CAS. C++ provides two variants: `compare_exchange_weak` and `compare_exchange_strong`, and the difference between them is subtle but critical.

Let us look at the interface first. Both have identical signatures:

```cpp
bool compare_exchange_weak(T& expected, T desired,
                           std::memory_order success = memory_order_seq_cst,
                           std::memory_order failure = memory_order_seq_cst);

bool compare_exchange_strong(T& expected, T desired,
                             std::memory_order success = memory_order_seq_cst,
                             MemoryOrder failure = memory_order_seq_cst);
```

The execution logic is as follows: atomically compare the current value with `expected`. If they are equal, replace the current value with `desired` and return `true`; if they are not equal, load the current value into `expected` and return `false`. Note that on failure, `expected` is overwritten—this is an easily overlooked detail. If you need to use the original `expected` value afterward, remember to back it up in advance.

The difference lies in "spurious failure": `compare_exchange_weak` may return `false` even when the current value equals `expected`. This is not a bug, but rather a hardware-level limitation. On architectures like ARM and PowerPC that implement CAS using LL/SC (Load-Linked/Store-Conditional) primitives, the SC instruction can fail for various reasons—another processor touched the same cache line, an interrupt occurred, or even a pure scheduling event. x86 uses the hardware `CMPXCHG` instruction and does not have this issue, so on x86, `weak` and `strong` generate identical code.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> value{10};

    // CAS 成功的场景
    int expected = 10;
    bool ok = value.compare_exchange_strong(expected, 20);
    // ok = true, value = 20, expected 不变

    // CAS 失败的场景
    expected = 10;  // 重新设为 10
    ok = value.compare_exchange_strong(expected, 30);
    // ok = false, value 仍为 20, expected 被更新为 20
    std::cout << "value = " << value.load()
              << ", expected = " << expected << "\n";
    return 0;
}
```

When should you use `weak`, and when should you use `strong`? The rule is simple: if your CAS is already wrapped in a loop, use `weak`—a spurious failure just means one extra iteration, but `weak` saves the internal retry loop on LL/SC architectures, making it faster overall. If you are doing a one-shot CAS (not in a loop), use `strong`—otherwise, a single spurious failure could send your logic down the wrong branch.

### Implementing a Lock-Free Stack Push with CAS

Let us look at a classic CAS application scenario—the push operation of a lock-free stack. This example nicely demonstrates the usage of `compare_exchange_weak` in a loop:

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
        // 尝试把 head 从 old_head 换成 new_node
        // 如果成功，push 完成
        // 如果失败（别人已经改了 head），old_head 被更新为最新值，重试
    } while (!head.compare_exchange_weak(
                 old_head, new_node,
                 std::memory_order_release,
                 std::memory_order_relaxed));
}
```

The logic of this code is: first read the current `head`, point the new node's `next` to it, and then attempt to swap `head` to the new node via a single CAS. If another thread has already pushed a node (changing `head`) while we were preparing the new node, the CAS will fail, `old_head` will be updated to the latest `head`, and we reset `new_node->next` and try again. This process repeats until the CAS succeeds.

You may have noticed that `compare_exchange_weak` here accepts two memory order parameters: `success` and `failure`. On success, we use `memory_order_release` (because we just wrote a new node and need to ensure other threads can see the complete data); on failure, we use `memory_order_relaxed` (since we failed, no synchronization guarantees are needed—we are simply retrying).

## exchange(): Atomic Swap

`exchange()` is a relatively simple but highly practical operation: it atomically writes in a new value while taking out the old value. It is a combination of `load` and `store`, but guarantees that these two steps are indivisible.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    std::atomic<int> flag{0};

    int old = flag.exchange(1);
    // 现在 flag = 1，old = 0
    std::cout << "flag = " << flag.load()
              << ", old = " << old << "\n";
    return 0;
}
```

A typical use case for `exchange()` is "state handoff"—atomically switching some state from A to B while deciding subsequent behavior based on the old state:

```cpp
#include <atomic>
#include <iostream>

enum class DeviceState { kIdle, kBusy, kError };

std::atomic<DeviceState> state{DeviceState::kIdle};

void try_start_work()
{
    // 原子地尝试从 Idle 切换到 Busy
    DeviceState old = state.exchange(DeviceState::kBusy);
    if (old != DeviceState::kIdle) {
        // 之前不是 Idle，说明有其他线程已经在用了
        // 恢复原状态（或者进入错误处理）
        state.store(old);
        std::cout << "Cannot start: device was " <<
                     static_cast<int>(old) << "\n";
        return;
    }
    // 成功切换到 Busy，开始工作
    std::cout << "Work started\n";
}
```

Note that this example could actually be written more precisely with CAS (`exchange` unconditionally writes the new value even if the old state is not `kIdle`), but the advantage of `exchange` lies in its simplicity—if you just want to swap in a value and know what the old value was, `exchange` is much more concise than a CAS loop.

## is_lock_free and is_always_lock_free

Up to this point we have been saying "atomic operations do not rely on locks," but the truth is not always so. Whether `std::atomic<T>` is truly lock-free depends on two factors: the size of type `T` and the hardware capabilities of the target platform. If the hardware lacks atomic instructions of the corresponding width (for example, atomic operations on 64-bit integers on a 32-bit ARM), the compiler will fall back to using internal locks—making operations on `std::atomic` not truly lock-free.

The standard library provides two interfaces to query this. `is_lock_free()` is a runtime query that returns `true` to indicate that operations on the current object are lock-free. `is_always_lock_free` is a compile-time constant (`static constexpr`) that returns `true` to indicate that atomic operations of this type are lock-free for **all** instances on the platform. If you need to make a static assertion at compile time, use `is_always_lock_free`; if you need to make a branch decision at runtime, use `is_lock_free()`.

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

    // 编译期检查：如果 int 不是 lock-free 的，直接编译报错
    static_assert(std::atomic<int>::is_always_lock_free,
                  "int must be lock-free on this platform!");

    return 0;
}
```

In real-world projects, `is_always_lock_free` is more valuable than `is_lock_free()`. The reason is: if your code path has branches that depend on the return value of `is_lock_free()`, it means the same code might take different paths on different running instances—this is a nightmare for testing and debugging. In contrast, `static_assert` + `is_always_lock_free` can expose the problem at compile time: either the platform fully supports lock-free operations, or the code fails to compile—there is no gray area.

In embedded scenarios, this is especially important. On 32-bit ARM Cortex-M, `std::atomic<int>` is almost always lock-free (the hardware has the `LDREX`/`STREX` instruction pair), but `std::atomic<int64_t>` may not be on Cortex-M0/M3. If you use atomic operations in an ISR, you must confirm that they are lock-free—an ISR cannot block, and lock-based atomic operations will block.

## atomic_flag: The Standard-Guaranteed Lock-Free Primitive

Whether `std::atomic<T>` is lock-free depends on the platform, but `std::atomic_flag` is an exception—the standard guarantees that `std::atomic_flag` is **always lock-free**. On all platforms, with all compilers, without exception. This makes `atomic_flag` the most reliable cornerstone for building low-level synchronization primitives (such as spinlocks).

`atomic_flag` has only two states: set (true) and clear (false). It provides three core operations: `test_and_set()` atomically sets the flag to true and returns the previous value; `clear()` atomically sets the flag to false; and C++20 adds `test()` to atomically read the current value without modifying it.

```cpp
#include <atomic>
#include <iostream>

int main()
{
    // C++20 起可以直接 {} 初始化
    std::atomic_flag flag{};

    // test_and_set：设置为 true，返回旧值
    bool was_set = flag.test_and_set();
    std::cout << "was_set = " << std::boolalpha << was_set << "\n";

    // test（C++20）：读取当前值
    bool current = flag.test();
    std::cout << "current = " << current << "\n";

    // clear：设置为 false
    flag.clear();
    std::cout << "after clear: " << flag.test() << "\n";

    return 0;
}
```

### Implementing a Spinlock with atomic_flag

The most classic application of `atomic_flag` is the spinlock. The principle of a spinlock is simple: when acquiring the lock, continuously attempt `test_and_set`; if it returns false (previously in the clear state), it means you successfully acquired the lock; if it returns true (previously in the set state), it means the lock is held by someone else, so you spin again. When releasing the lock, call `clear`.

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
            // 自旋等待：CPU 在空转
            // 在 x86 上可以插入 _mm_pause() 降低功耗
            // 在 ARM 上可以插入 __yield()
        }
    }

    void unlock()
    {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_{};
};

// 使用示例
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
    // 输出：shared_counter = 1000000
    return 0;
}
```

The drawback of a spinlock is obvious: while the lock is held, other threads are spinning idly, wasting CPU time. Therefore, spinlocks are only suitable for scenarios with very short critical sections—ideally, the lock should be held for such a short time that "another thread has not even had time to be scheduled away before it is released." If the critical section is relatively long, using `std::mutex` (an OS-level blocking lock) is more appropriate.

C++20 also adds `wait()` and `notify_one()`/`notify_all()` operations for `atomic_flag`, allowing spinlocks to evolve into more efficient "wait locks"—instead of spinning idly on acquisition failure, the thread is suspended and woken up when the lock is released. Under the hood, this uses `futex` on Linux and `WaitOnAddress` on Windows, saving far more CPU than pure spinning.

## Common Misconceptions

Before we wrap up, let us quickly go over a few easy-to-fall-into traps.

The first misconception: assuming atomic variables can solve all race conditions. Atomic operations guarantee the atomicity of a **single access**, but they do not guarantee atomicity **across multiple atomic operations**. For example:

```cpp
std::atomic<int> x{0};
std::atomic<int> y{0};

// 线程 1
x.store(1);
y.store(2);

// 线程 2
int a = y.load();
int b = x.load();
```

Even though the individual `load`/`store` of `x` and `y` are each atomic, thread 2 might still see `a == 2` but `b == 0`—because there is no synchronization relationship between the two `store` operations or between the two `load` operations. This is not something atomic operations can solve; it requires memory order constraints. We will explore this topic in detail in the next chapter.

The second misconception: believing that `volatile` is equivalent to `std::atomic`. The semantics of `volatile` are "do not optimize away accesses to this variable"—every read and write will truly access memory without caching. However, `volatile` **does not guarantee atomicity, nor does it guarantee memory order**. A `++counter` on a `volatile int counter;` is still a three-step read-modify-write operation and can still result in a data race. The original design intent of `volatile` was for hardware register mapping and signal handlers, not for multithreading.

The third misconception: using `std::atomic` on a non-trivially-copyable type like `std::atomic<std::string>`. The standard does not allow this—the compiler will report an error directly. `std::string` has a user-defined copy constructor (involving heap memory allocation internally) and does not satisfy the trivially copyable requirement. If you need to share a string atomically, you can use `std::atomic<std::shared_ptr<std::string>>` (supported starting from C++20) or protect it with a mutex.

## Exercises

### Exercise 1: Lock-Free Counter

Implement a multithread-safe counter using `std::atomic<int>`. Launch eight threads, each incrementing the counter 100,000 times, and the final result should be 800,000. Test both implementations—using `fetch_add` and a `compare_exchange_weak` loop—and compare their correctness and performance differences.

Hint: The approach to implementing `fetch_add` with `compare_exchange_weak` is—read the current value, compute the new value, attempt to replace it with CAS, and retry on failure.

### Exercise 2: Lock-Free Maximum Value Tracker

Implement a thread-safe maximum value tracker: multiple threads continuously write random values, and the tracker always records the maximum value among all written values. You must use `compare_exchange_strong` (not `fetch_add`) to implement this.

Hint: The `expected` parameter of `compare_exchange_strong` is updated to the current value on failure—you need to compare the current value with your candidate new value in this "failure" branch to decide whether to retry.

```cpp
class MaxTracker {
public:
    void update(int new_value)
    {
        int current = max_.load(std::memory_order_relaxed);
        while (new_value > current) {
            if (max_.compare_exchange_strong(
                    current, new_value, std::memory_order_relaxed)) {
                break;  // 成功更新
            }
            // 失败：current 被更新为最新值，继续比较
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

After completing the `update` function above, test it with multiple threads: create eight threads, each generating 100,000 random values and calling `update`, and finally verify that the value returned by `get()` is indeed the maximum among all values generated by the threads.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch03-atomic-memory-model/`.

## References

- [std::atomic -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic)
- [std::atomic_flag -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic_flag)
- [compare_exchange_weak vs compare_exchange_strong -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/compare_exchange)
- [C++ Concurrency in Action, 2nd Edition -- Anthony Williams](https://www.cplusplus.com/reference/atomic/atomic/)
- [atomic is_lock_free -- cppreference](https://en.cppreference.com/w/cpp/atomic/atomic/is_lock_free)
