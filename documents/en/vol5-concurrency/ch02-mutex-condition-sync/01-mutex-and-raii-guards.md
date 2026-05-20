---
title: Mutex and RAII Locks
description: A systematic overview of the mutex family and RAII lock guards, covering
  the evolution and best practices from `lock_guard` to `scoped_lock`
chapter: 2
order: 1
tags:
- host
- cpp-modern
- intermediate
- mutex
- RAII守卫
difficulty: intermediate
platform: host
reading_time_minutes: 22
cpp_standard:
- 11
- 14
- 17
- 20
prerequisites:
- 线程所有权与 RAII
related:
- 死锁与锁顺序
- condition_variable 与等待语义
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md
  source_hash: 10329a5367083b3e9d79bf0af34aec7bbdbfd1bbf5477a8da4a18bdf2c85d2a4
  translated_at: '2026-05-20T04:35:36.314317+00:00'
  engine: anthropic
  token_count: 3112
---
# mutex and RAII Locks

In the previous article, we discussed thread ownership and RAII, mastering the lifetime management of `std::thread` and the scope-based resource control approach. Now a question arises: with threads in place, how do we safely share data between them? We have already seen the destructive power of a data race in the article on fundamental concurrency issues—two threads writing to the same `int` simultaneously can turn a result of 2,000,000 into 1,345,687. The most common solution to a data race is the mutex, and the C++ standard library provides an entire mutex family along with matching RAII lock guards.

Our goal in this article is clear: first, we will walk through the four members of the mutex family—`std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, and `std::recursive_timed_mutex`—to understand what problem each one solves. Then, we will systematically review the three RAII lock guards—`std::lock_guard`, `std::unique_lock`, and `std::scoped_lock`—which are the tools that should actually appear in our daily code. Throughout this process, we will repeatedly emphasize one principle: absolutely never manually call `lock()` and `unlock()`.

## std::mutex: The Most Basic Mutex

`std::mutex` is the standard mutex introduced in C++11, defined in the `<mutex>` header. It provides only three operations: `lock()`, `unlock()`, and `try_lock()`.

`lock()` is a blocking call—if the mutex is already held by another thread, the current thread blocks and waits until it acquires the lock. `unlock()` releases the lock. `try_lock()` is the non-blocking version—it attempts to acquire the lock, returning `true` on success and `false` on failure, without waiting. These three operations constitute the entire interface of the mutex, simple to an astonishing degree.

Don't be too quick to assume that simplicity means there are no pitfalls. Take a look at this "handcrafted" style of code:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx;
int shared_counter = 0;

void bad_increment()
{
    mtx.lock();              // 手动加锁
    shared_counter++;
    // 如果这里抛出异常... unlock 永远不会执行
    mtx.unlock();            // 手动解锁
}
```

This code works on the normal path, but it has several fatal hidden dangers. If any exception is thrown between `mtx.lock()` and `mtx.unlock()` (of course, incrementing an `int` won't throw, but what if you replace the `int` with a complex type, or insert other operations that might throw in between?), `mtx.unlock()` will never be executed. The lock is not released, and all other threads waiting for this lock will block—this isn't a dead lock, but the effect is similar, and it's even harder to track down because the program doesn't freeze in an obvious loop-wait; it just "inexplicably" stops.

An even worse scenario involves multiple return paths. If there are three or four `if` branches in the middle of your critical section, you have to write `mtx.unlock()` before each branch. Missing even one is a bug. In a large codebase, it is virtually impossible to guarantee correctness with this "manual lock/unlock pairing" pattern.

There is also a classic pitfall: the same lock being acquired twice by the same thread. `std::mutex` does not allow a thread to repeatedly lock it—if you call `lock()` while already holding the lock, the result is undefined behavior (most implementations will dead lock immediately). This is easy to stumble into unknowingly when the function call chain is complex:

```cpp
std::mutex mtx;

void function_a()
{
    mtx.lock();
    function_b();    // function_b 内部也锁了同一把 mutex
    mtx.unlock();
}

void function_b()
{
    mtx.lock();      // 死锁！同一线程对 std::mutex 重复加锁
    // ...
    mtx.unlock();
}
```

So the conclusion is clear: the direct interface of `std::mutex` should not appear in application code. Its design intent is to serve as the underlying cornerstone for RAII wrappers, not for you to manually call `lock()`/`unlock()` every day.

## std::recursive_mutex: Allowing Repeated Locking by the Same Thread

`std::recursive_mutex` solves the "repeated locking by the same thread" problem mentioned above. It internally maintains a lock counter—the first `lock()` by the same thread sets the counter to 1, the second to 2, and so on; each `unlock()` decrements the counter by 1, and the lock is only truly released when the counter reaches 0.

```cpp
#include <mutex>
#include <iostream>

std::recursive_mutex rmtx;

void recursive_function(int depth)
{
    std::lock_guard<std::recursive_mutex> lock(rmtx);
    std::cout << "depth = " << depth << "\n";
    if (depth > 0) {
        recursive_function(depth - 1);  // 递归调用，再次加锁
    }
}

int main()
{
    recursive_function(5);
    return 0;
}
```

This code is perfectly legal—`std::recursive_mutex` allows the same thread to lock multiple times. Each recursive call increments the counter, each return triggers the destructor of `std::lock_guard` to decrement the counter, and the lock is only truly released when the outermost function returns.

However, `std::recursive_mutex` is usually a signal of a design smell. If you need a recursive lock, it's highly likely because your interface design mixes "functions that need to be called under lock protection" with "internal implementations that don't need a lock." A better approach is to extract the "operations under lock protection" into an unlocked internal function, and let the outer interface handle the locking. A recursive lock is a crutch; it can help you walk, but you shouldn't rely on it.

## std::timed_mutex: Mutex with Timeout

`std::timed_mutex` adds two timeout-based locking operations on top of `std::mutex`: `try_lock_for()` and `try_lock_until()`.

`try_lock_for()` accepts a time duration (`std::chrono::duration`), repeatedly attempting to acquire the lock within the specified time, and returns `false` on timeout. `try_lock_until()` accepts an absolute time point (`std::chrono::time_point`), attempting to acquire the lock before the specified moment, and returns `false` on timeout. The difference between the two is similar to "wait for at most 100 milliseconds" versus "wait until 3 PM."

```cpp
#include <mutex>
#include <chrono>
#include <iostream>

std::timed_mutex tmtx;

void try_with_timeout()
{
    if (tmtx.try_lock_for(std::chrono::milliseconds(100))) {
        // 成功获取锁
        std::cout << "Lock acquired within 100ms\n";
        // ... 临界区操作 ...
        tmtx.unlock();
    } else {
        // 超时，锁获取失败
        std::cout << "Failed to acquire lock within 100ms\n";
        // 可以做降级处理、记录日志、或者稍后重试
    }
}
```

`std::recursive_timed_mutex` is a combination of a recursive lock and a timed lock—the same thread can lock multiple times, while also supporting `try_lock_for()` and `try_lock_until()`. It is rarely used in practical engineering; just knowing it exists is enough.

A quick reminder here: locks with timeouts have higher overhead on some platforms because they need to interact with the system clock. If your scenario doesn't require timeout capabilities, a plain `std::mutex` is sufficient. Don't default to `std::timed_mutex` just because "it might come in handy."

## std::lock_guard: The Simplest RAII Wrapper

We have finally arrived at the tools we should actually use. `std::lock_guard` is the lightest RAII lock guard introduced in C++11—it calls `lock()` on construction and `unlock()` on destruction, that's it. It doesn't accept `std::defer_lock`, has no `unlock()` method, and doesn't support moving—it has no extra capabilities whatsoever, but it is precisely this minimalist design that guarantees you can't use it incorrectly.

```cpp
#include <mutex>
#include <iostream>
#include <vector>

std::mutex mtx;
std::vector<int> shared_data;

void safe_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // 构造时自动 lock
    shared_data.push_back(value);
    // 无论正常返回、异常抛出、还是 early return，析构时都会 unlock
}
```

Note a common mistake made by beginners—forgetting to name the `std::lock_guard` variable:

```cpp
void bad_push(int value)
{
    std::lock_guard<std::mutex>(mtx);  // 临时对象！立刻析构！
    shared_data.push_back(value);      // 没有锁保护
}

void good_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // lock 有名字，生命周期是整个作用域
    shared_data.push_back(value);
}
```

An unnamed temporary object is destructed immediately at the end of the statement—the lock is released the moment it is acquired, which is equivalent to not locking at all. Compilers usually don't issue warnings for this situation, so you must remember to give the lock object a name.

`std::lock_guard` has a less commonly used but worth-knowing constructor option: `std::adopt_lock`. It tells `std::lock_guard`: "The lock is already held by the current thread; just handle releasing it on destruction, don't lock again." This option is primarily used in conjunction with the `std::lock()` function—first acquire multiple locks simultaneously via `std::lock()`, then hand them over to `std::lock_guard` management using `std::adopt_lock`. We will see the specific usage when we discuss dead lock prevention in the next article.

## std::unique_lock: A Flexible but Not Heavy Swiss Army Knife

If `std::lock_guard` is a reliable screwdriver, `std::unique_lock` is a Swiss army knife. On top of `std::lock_guard`, it adds several key capabilities: deferred locking, manual unlocking, lock ownership transfer, and cooperation with condition variables. Of course, the extra capabilities also mean extra state—`std::unique_lock` internally needs to store an "is holding lock" flag, making its overhead slightly larger than `std::lock_guard`, but in the vast majority of scenarios, this difference is negligible.

### Basic Usage: As Simple as lock_guard

```cpp
#include <mutex>

std::mutex mtx;

void basic_unique_lock()
{
    std::unique_lock<std::mutex> lock(mtx);  // 构造时加锁，析构时解锁
    // 临界区...
}
```

The most basic usage is exactly the same as `std::lock_guard`: lock on construction, unlock on destruction.

### Deferred Locking: defer_lock

`std::defer_lock` tells `std::unique_lock` not to lock upon construction; we decide when to lock later. This is useful in "conditional locking" scenarios—not all code paths need the lock, but you want to enjoy RAII protection on the paths that do:

```cpp
#include <mutex>

std::mutex mtx;
bool needs_sync = true;  // 假设由外部条件决定

void conditional_lock()
{
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);  // 构造时不加锁

    if (needs_sync) {
        lock.lock();  // 按需加锁
    }

    // ... 无论加没加锁，析构时都能正确处理
}
```

A more common use of `std::defer_lock` is cooperating with `std::lock()` to achieve safe acquisition of multiple locks—first construct two `std::unique_lock`s with `std::defer_lock`, then use `std::lock()` to lock them simultaneously. This pattern will be detailed in the next article.

### Early Unlocking: Shrinking the Critical Section

`std::unique_lock` allows you to manually call `unlock()` before the end of the scope—which is valuable when you need to shrink the critical section. The shorter the lock is held, the shorter the wait time for other threads, and the higher the concurrency:

```cpp
#include <mutex>
#include <vector>
#include <fstream>

std::mutex mtx;
std::vector<int> shared_data;

void process_and_save()
{
    std::unique_lock<std::mutex> lock(mtx);

    // 在锁的保护下拷贝数据
    auto snapshot = shared_data;

    lock.unlock();  // 临界区结束，提前解锁

    // 在锁外做耗时操作——不会阻塞其他线程
    for (auto& v : snapshot) {
        v *= 2;
    }

    // 保存到文件也是锁外的操作
    std::ofstream ofs("output.txt");
    for (int v : snapshot) {
        ofs << v << "\n";
    }
}
```

This example demonstrates an important pattern: quickly complete the necessary data copy under the protection of the lock, then immediately release the lock, and perform subsequent processing outside the lock. `std::lock_guard` cannot unlock early—its design philosophy is "the lifetime of the lock equals the lifetime of the scope," with no exceptions.

### Cooperating with Condition Variables

This is the most irreplaceable scenario for `std::unique_lock`. The `wait()` family of functions in `std::condition_variable` requires a `std::unique_lock`, and you cannot use `std::lock_guard`. The reason lies in the working mechanism of condition variables: a thread must release the lock while waiting (so other threads can enter the critical section to modify the condition), and it must re-acquire the lock when woken up. The "unlock-then-relock" capability provided by `std::unique_lock` is exactly what condition variables need.

```cpp
#include <mutex>
#include <condition_variable>
#include <queue>
#include <iostream>

template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& value)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();
    }

    T pop()
    {
        std::unique_lock<std::mutex> lock(mtx_);  // 必须用 unique_lock
        cv_.wait(lock, [this] { return !queue_.empty(); });
        // wait 内部：条件不满足 -> unlock -> 等待 -> 被唤醒 -> re-lock -> 检查条件

        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};
```

If you try to replace the `std::unique_lock` in the code with `std::lock_guard`, it won't even compile—the signature of `wait()` requires a `std::unique_lock`.

### Lock Ownership Transfer

`std::unique_lock` supports move semantics, allowing lock ownership to be transferred between functions. This is useful in certain architectural designs—for example, a function is responsible for acquiring the lock and doing some initialization work, then transferring the lock ownership to the caller, who is responsible for subsequent critical section operations and the final unlocking:

```cpp
#include <mutex>

std::mutex mtx;

std::unique_lock<std::mutex> acquire_and_initialize()
{
    std::unique_lock<std::mutex> lock(mtx);
    // 做一些需要锁保护的初始化工作
    prepare_shared_state();
    return lock;  // NRVO 或移动返回，锁的所有权转移给调用者
}

void use_lock()
{
    std::unique_lock<std::mutex> lock = acquire_and_initialize();
    // lock 持有锁，可以在临界区操作
    modify_shared_state();
    // lock 离开作用域时自动解锁
}
```

Note that `std::lock_guard` does not support moving—both its copy constructor and move constructor are deleted. If you need to transfer lock ownership, `std::unique_lock` is the only choice.

## std::scoped_lock: C++17's Multi-Lock Dead Lock Prevention

`std::scoped_lock` is the RAII lock guard introduced in C++17, specifically designed for multi-lock scenarios. Its constructor can accept any number of mutexes (it also accepts a single mutex, of course), and it internally uses the dead lock avoidance algorithm provided by `std::lock()` to acquire all locks at once, releasing them in reverse order upon destruction.

This feature solves a very practical problem. Suppose two threads need to simultaneously operate on two data structures protected by different mutexes. The most naive approach is to nest `std::lock_guard`s:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 先锁 A
    std::cout << "thread1: locked A\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 再锁 B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // 先锁 B
    std::cout << "thread2: locked B\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // 再锁 A
    std::cout << "thread2: locked both\n";
}
```

If thread1 acquires `mutex_a` while thread2 acquires `mutex_b`, both sides get stuck—the classic AB-BA dead lock. `std::scoped_lock` solves this with a single line of code:

```cpp
void safe_thread()
{
    std::scoped_lock lock(mtx_a, mtx_b);  // 一次性安全获取两把锁
    // 临界区...
}
```

The dead lock avoidance algorithm inside `std::scoped_lock` is based on a `std::lock()` backoff strategy: it tries to acquire all locks in a certain order, and if a certain `try_lock()` fails, it releases the already acquired locks and retries in a different order. This algorithm breaks the "hold and wait" condition among the four necessary conditions for a dead lock—if acquisition fails, already held locks are released, eliminating the situation of "holding one lock while waiting for another."

`std::scoped_lock` can also be used for a single mutex scenario, where it is equivalent to `std::lock_guard`. However, for the clarity of code intent, `std::lock_guard` is still recommended for single-lock scenarios—seeing `std::lock_guard` tells you there is only one lock, and seeing `std::scoped_lock` tells you multiple locks might be involved. This is valuable information for anyone reading the code.

## lock_guard vs unique_lock vs scoped_lock: Selection Guide

Let's put the core differences of the three RAII lock guards together to help you make quick choices in actual development.

The design philosophy of `std::lock_guard` is "simplicity is beauty." It is neither copyable nor movable, cannot unlock early, and cannot defer locking—these "limitations" are precisely its advantages, because the more restrictions there are, the less room there is for error. For 90% of daily scenarios, `std::lock_guard` is sufficient: enter the function, construct a `std::lock_guard`, operate on shared data, return from the function, and the `std::lock_guard` destructor releases the lock. The entire process is a straight line with no branches.

`std::unique_lock` is suited for the 10% of scenarios that require extra flexibility. The most typical is cooperating with condition variables—this is the irreplaceable core scenario for `std::unique_lock`. Next is the "copy data first, then unlock early" pattern—moving time-consuming operations outside the lock to reduce lock hold time. There are also deferred locking and lock ownership transfer, which are used in more complex architectural designs.

The core value of `std::scoped_lock` is dead lock prevention for multi-lock acquisition. As long as your code needs to hold two or more locks simultaneously, you should use `std::scoped_lock`. If the project has already adopted C++17, using `std::scoped_lock` for single-lock scenarios is also perfectly fine—but in terms of team conventions, distinguishing `std::lock_guard` (single lock) and `std::scoped_lock` (multiple locks) helps with code readability and maintainability.

## Engineering Principle: Absolutely Never Manually Call lock()/unlock()

We spent an entire article discussing the mutex family and RAII lock guards, and the core principle we want to emphasize in the end is just one: absolutely never directly call `lock()` and `unlock()` in application code. We have repeatedly seen the reasons earlier—managing lock/unlock manually is virtually impossible to guarantee correctness in scenarios involving exception paths, multiple return paths, and nested calls, whereas RAII lock guards fundamentally eliminate this entire class of bugs by binding the lock's lifetime to the scope.

This principle is explicitly recorded in the C++ Core Guidelines as CP.20: "Use RAII, never plain `lock()`/`unlock()`." The only exception is `std::adopt_lock`—it accepts a mutex that is already locked and is only responsible for unlocking on destruction. But even in this case, the locking action should be done through `std::lock()` or other safe mechanisms, not by manually calling `lock()`.

> 💡 The complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `mutex_and_raii_lock`.

## Exercises

### Exercise 1: Implement a Thread-Safe Wrapper for stack

Given a `std::stack`, use `std::mutex` and `std::lock_guard` to implement a thread-safe wrapper for it. You are required to provide four interfaces: `push()`, `pop()` (returns `std::optional`, returning `std::nullopt` when the stack is empty), `top()` (also returns `std::optional`), and `empty()`. Hint: note that `top()` and `pop()` cannot return references—because after unlocking, the reference accessed by the caller would be invalid.

### Exercise 2: Compare the Performance of lock_guard and unique_lock

Write a simple benchmark: use four threads to each increment a shared counter 1,000,000 times, protected by `std::lock_guard` and `std::unique_lock` respectively. Compare the running times of the two—you will find that the difference is usually within the noise range, but in extreme scenarios, the extra state maintenance of `std::unique_lock` might manifest as a measurable overhead. Question: under what conditions would this difference become significant?

### Exercise 3: Safely Swap Two Protected Data Structures Using scoped_lock

Suppose there are two `std::vector<int>`s, each protected by a `std::mutex`. Write a `swap_data()` function that uses `std::scoped_lock` to acquire both locks simultaneously, and then swap the contents of the two vectors. Verify that repeatedly calling this function in a multi-threaded environment does not cause a dead lock.

## Reference Resources

- [std::mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/mutex)
- [std::recursive_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/recursive_mutex)
- [std::timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/timed_mutex)
- [std::lock_guard -- cppreference](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [std::unique_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/unique_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.20 -- Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
