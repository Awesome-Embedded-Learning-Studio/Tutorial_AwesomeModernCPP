---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: A systematic tour of the mutex family and RAII lock guards, covering
  the evolution from lock_guard to scoped_lock and the best practices along the way
difficulty: intermediate
order: 1
platform: host
prerequisites:
- Thread Ownership and RAII
reading_time_minutes: 17
related:
- Deadlock and Lock Ordering
- condition_variable and Wait Semantics
tags:
- host
- cpp-modern
- intermediate
- mutex
- RAII守卫
title: mutex and RAII Locks
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/01-mutex-and-raii-guards.md
  source_hash: 121661f2254941ac278f7660d0c0819cd8810aca0c06c09be07224f8ccabac3d
  translated_at: '2026-09-26T07:00:12+00:00'
  engine: anthropic
  token_count: 5500
---
# mutex and RAII Locks

In the previous article we talked about thread ownership and RAII, and picked up the ideas of `std::thread` lifetime management and scope-based resource control. Now the question: we have threads, so how do they share data safely? We already saw the destructive power of the data race back in the article on fundamental concurrency problems—two threads writing the same `int` at the same time, and the result goes from 2000000 to 1345687. The most general tool against data races is the mutex, and the C++ standard library ships a whole family of mutexes plus the matching RAII lock guards.

The plan for this article is clear: first we walk through the four members of the mutex family—`std::mutex`, `std::recursive_mutex`, `std::timed_mutex`, and `std::recursive_timed_mutex`—one by one, to understand what problem each one solves; then we systematically go over the three RAII lock guards—`lock_guard`, `unique_lock`, and `scoped_lock`—the tools that should actually appear in our daily code. Throughout the process we will keep stressing one principle: never call `lock()` and `unlock()` by hand.

## std::mutex: The Most Basic Mutex

`std::mutex` is the standard mutex introduced in C++11, defined in the `<mutex>` header. It offers exactly three operations: `lock()`, `unlock()`, and `try_lock()`.

`lock()` is a blocking call—if the mutex is already held by another thread, the current thread blocks and waits until it gets the lock. `unlock()` releases the lock. `try_lock()` is the non-blocking version—it tries to acquire the lock, returns `true` on success and `false` on failure, and never waits. These three operations are the entire mutex interface, absurdly simple.

Don't rush to conclude that simple means trap-free. Take a look at this "hand-crafted workshop" style code:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx;
int shared_counter = 0;

void bad_increment()
{
    mtx.lock();              // manual lock
    shared_counter++;
    // if an exception is thrown here... unlock never executes
    mtx.unlock();            // manual unlock
}
```

This code works on the happy path, but it carries a few fatal hazards. If anything between `shared_counter++` and `mtx.unlock()` throws (sure, incrementing an `int` can't throw—but swap the `int` for a complex type, or interleave some other operation that might throw, and what then?), `unlock()` never runs. The lock never gets released, and every thread waiting on it blocks—not a deadlock, strictly speaking, but the effect is about the same, and even harder to debug, because the program isn't stuck in some obvious circular wait; it just stopped "inexplicably".

Multiple return paths make it worse. If the middle of your critical section has three or four `if-return` branches, each one needs a `mtx.unlock()` right before it—miss a single one and you have a bug. In a large codebase, this "manually paired lock/unlock" pattern is nearly impossible to keep correct.

There is one more classic trap: the same thread locking the same mutex twice. `std::mutex` does not allow the same thread to relock it—if you call `lock()` while already holding the lock, the result is undefined behavior (most implementations simply deadlock on the spot). With deep, tangled call chains it is remarkably easy to step on this without noticing:

```cpp
std::mutex mtx;

void function_a()
{
    mtx.lock();
    function_b();    // function_b also locks the same mutex internally
    mtx.unlock();
}

void function_b()
{
    mtx.lock();      // deadlock! the same thread relocks a std::mutex
    // ...
    mtx.unlock();
}
```

So the conclusion is clear: `std::mutex`'s direct interface should not appear in application code. It was designed as the low-level building block for RAII wrappers, not as something for you to `lock()`/`unlock()` every day.

## std::recursive_mutex: Allowing the Same Thread to Lock Repeatedly

`std::recursive_mutex` solves the "same thread locks twice" problem above. It maintains an internal lock counter—the first `lock()` from the same thread sets the counter to 1, the second to 2, and so on; each `unlock()` decrements the counter, and the lock is truly released only when it reaches 0.

```cpp
#include <mutex>
#include <iostream>

std::recursive_mutex rmtx;

void recursive_function(int depth)
{
    std::lock_guard<std::recursive_mutex> lock(rmtx);
    std::cout << "depth = " << depth << "\n";
    if (depth > 0) {
        recursive_function(depth - 1);  // recursive call, locks again
    }
}

int main()
{
    recursive_function(5);
    return 0;
}
```

This code is perfectly legal—`recursive_mutex` allows the same thread to lock multiple times. Every recursive call increments the counter, and every return triggers the `lock_guard` destructor to decrement it, until the outermost function returns and the lock is truly released.

That said, `recursive_mutex` is usually a signal of a design smell. If you need a recursive lock, odds are your API design has mixed "functions that must be called under lock protection" together with "internal implementation that needs no lock". The better approach is to extract "the operations that run under lock protection" into an internal function that does no locking, and let the outer API be responsible for locking. A recursive lock is a crutch—it helps you walk, but you really shouldn't depend on it.

## std::timed_mutex: A Mutex with Timeouts

`std::timed_mutex` adds two timeout-aware locking operations on top of `std::mutex`: `try_lock_for()` and `try_lock_until()`.

`try_lock_for()` takes a duration (`std::chrono::duration`) and repeatedly tries to acquire the lock within the given time, returning `false` on timeout. `try_lock_until()` takes an absolute time point (`std::chrono::time_point`) and tries to acquire the lock before the given moment, likewise returning `false` on timeout. The difference is like "wait for at most 100 milliseconds" versus "wait until 3 p.m.".

```cpp
#include <mutex>
#include <chrono>
#include <iostream>

std::timed_mutex tmtx;

void try_with_timeout()
{
    if (tmtx.try_lock_for(std::chrono::milliseconds(100))) {
        // lock successfully acquired
        std::cout << "Lock acquired within 100ms\n";
        // ... critical section work ...
        tmtx.unlock();
    } else {
        // timed out, lock acquisition failed
        std::cout << "Failed to acquire lock within 100ms\n";
        // you can degrade gracefully, log, or retry later
    }
}
```

`std::recursive_timed_mutex` is the combination of a recursive lock and a timeout lock—the same thread can lock it multiple times, and it supports both `try_lock_for()` and `try_lock_until()`. It sees little use in real-world engineering; just knowing it exists is enough.

One word of caution here: timeout-capable locks cost more on some platforms, because they need to interact with the system clock. If your scenario doesn't need timeout capability, a plain `std::mutex` is enough. Don't default to `timed_mutex` on the "just in case it's useful" theory.

## std::lock_guard: The Simplest RAII Wrapper

Finally we arrive at the tools we should actually be using. `std::lock_guard` is the lightest RAII lock guard, introduced in C++11—the constructor calls `lock()`, the destructor calls `unlock()`, and that's it. It accepts no `defer_lock`, has no `unlock()` method, supports no moving—no extra capabilities whatsoever, and it is precisely this minimalist design that guarantees you can't misuse it.

```cpp
#include <mutex>
#include <iostream>
#include <vector>

std::mutex mtx;
std::vector<int> shared_data;

void safe_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // locks automatically on construction
    shared_data.push_back(value);
    // normal return, thrown exception, or early return alike: the destructor unlocks
}
```

Watch out for a mistake newcomers make all the time—forgetting to give the `lock_guard` variable a name:

```cpp
void bad_push(int value)
{
    std::lock_guard<std::mutex>(mtx);  // temporary object! destroyed immediately!
    shared_data.push_back(value);      // no lock protection
}

void good_push(int value)
{
    std::lock_guard<std::mutex> lock(mtx);  // lock has a name, its lifetime spans the whole scope
    shared_data.push_back(value);
}
```

The unnamed temporary is destroyed the instant the statement ends—the lock is released as soon as it is taken, which amounts to no lock at all. Compilers usually don't warn about this situation, so make a point of remembering: always give your lock object a name.

`lock_guard` has one rarely used but worth-knowing constructor option: `std::adopt_lock`. It tells the `lock_guard`: "the lock is already held by the current thread; your only job is to release it on destruction, don't lock again". This option exists mainly to pair with the `std::lock()` function—first acquire several locks simultaneously via `std::lock()`, then hand them to `lock_guard` for management with `adopt_lock`. We will see the concrete usage in the next article on deadlock prevention.

## std::unique_lock: The Flexible yet Lightweight Swiss Army Knife

If `lock_guard` is a dependable screwdriver, `std::unique_lock` is a Swiss Army knife. On top of `lock_guard` it adds several key capabilities: deferred locking, manual unlocking, lock ownership transfer, and cooperation with condition variables. Of course, the extra capability also means extra state—`unique_lock` internally needs to store an additional "is the lock held" flag, so its overhead is slightly higher than `lock_guard`, but in the overwhelming majority of scenarios this difference is negligible.

### Basic Usage: As Simple as lock_guard

```cpp
#include <mutex>

std::mutex mtx;

void basic_unique_lock()
{
    std::unique_lock<std::mutex> lock(mtx);  // locks on construction, unlocks on destruction
    // critical section...
}
```

The most basic usage is exactly the same as `lock_guard`: lock on construction, unlock on destruction.

### Deferred Locking: defer_lock

`std::defer_lock` tells `unique_lock` not to lock at construction—we decide later when to lock. This is useful in "conditional locking" scenarios—not every code path needs the lock, but you want RAII protection on the paths that do:

```cpp
#include <mutex>

std::mutex mtx;
bool needs_sync = true;  // say this is decided by external conditions

void conditional_lock()
{
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);  // no locking at construction

    if (needs_sync) {
        lock.lock();  // lock on demand
    }

    // ... whether or not we locked, the destructor handles it correctly
}
```

The more common use of `defer_lock` is pairing with `std::lock()` for the safe acquisition of multiple locks—first construct two `unique_lock`s with `defer_lock`, then lock them both simultaneously with `std::lock()`. We will expand on this pattern in detail in the next article.

### Early Unlocking: Shrinking the Critical Section

`unique_lock` allows you to call `unlock()` manually before the scope ends—which is valuable when you need to shrink the critical section. The shorter the lock is held, the shorter other threads wait, and the higher the concurrency:

```cpp
#include <mutex>
#include <vector>
#include <fstream>

std::mutex mtx;
std::vector<int> shared_data;

void process_and_save()
{
    std::unique_lock<std::mutex> lock(mtx);

    // copy the data under the lock's protection
    auto snapshot = shared_data;

    lock.unlock();  // critical section over, unlock early

    // do the time-consuming work outside the lock—no blocking of other threads
    for (auto& v : snapshot) {
        v *= 2;
    }

    // saving to the file is also an operation outside the lock
    std::ofstream ofs("output.txt");
    for (int v : snapshot) {
        ofs << v << "\n";
    }
}
```

This example demonstrates an important pattern: under the lock's protection, quickly finish the necessary data copy, then release the lock immediately and do the subsequent processing outside it. `lock_guard` cannot unlock early—its design philosophy is "the lock's lifetime equals the scope's lifetime", without a single exception.

### Working with Condition Variables

This is the scenario where `unique_lock` is most irreplaceable. The `wait()` family of functions of `std::condition_variable` requires a `std::unique_lock<std::mutex>` to be passed in; a `lock_guard` won't do. The reason lies in how condition variables work: while waiting, the thread must first release the lock (so other threads can enter the critical section and modify the condition), and when woken up it must reacquire the lock. The "unlock then relock" capability that `unique_lock` provides is exactly what condition variables need.

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
        std::unique_lock<std::mutex> lock(mtx_);  // must be unique_lock
        cv_.wait(lock, [this] { return !queue_.empty(); });
        // inside wait: condition unmet -> unlock -> wait -> woken -> re-lock -> check condition

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

If you try to replace the `unique_lock` in `pop()` with a `lock_guard`, it won't even compile—the signature of `condition_variable::wait()` demands a `unique_lock`.

### Transferring Lock Ownership

`unique_lock` supports move semantics, so lock ownership can be passed between functions. This is useful in certain architectural designs—for example, one function acquires the lock and does some initialization work, then transfers ownership of the lock to the caller, and the caller takes care of the subsequent critical section operations and the final unlock:

```cpp
#include <mutex>

std::mutex mtx;

std::unique_lock<std::mutex> acquire_and_initialize()
{
    std::unique_lock<std::mutex> lock(mtx);
    // do initialization work that needs the lock's protection
    prepare_shared_state();
    return lock;  // returned via NRVO or move; lock ownership transfers to the caller
}

void use_lock()
{
    std::unique_lock<std::mutex> lock = acquire_and_initialize();
    // lock holds the lock; critical section operations are fair game
    modify_shared_state();
    // when lock leaves the scope, it unlocks automatically
}
```

Note that `lock_guard` does not support moving—both its copy constructor and its move constructor are deleted. If you need to transfer lock ownership, `unique_lock` is the only choice.

## std::scoped_lock: C++17's Multi-Lock Deadlock Prevention

`std::scoped_lock` is the RAII lock guard introduced in C++17, purpose-built for multi-lock scenarios. Its constructor can accept any number of mutexes (a single mutex works too, of course), internally uses the deadlock-avoidance algorithm provided by `std::lock()` to acquire all the locks at once, and releases them in reverse order on destruction.

This feature solves a very real problem. Suppose two threads need to operate on two data structures protected by different mutexes at the same time; the most naive approach is to nest `lock_guard`s:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);  // lock A first
    std::cout << "thread1: locked A\n";
    std::lock_guard<std::mutex> lock_b(mtx_b);  // then B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);  // lock B first
    std::cout << "thread2: locked B\n";
    std::lock_guard<std::mutex> lock_a(mtx_a);  // then A
    std::cout << "thread2: locked both\n";
}
```

If thread1 grabs `mtx_a` while thread2 grabs `mtx_b`, both sides are stuck—the classic AB-BA deadlock. `scoped_lock` solves it in one line:

```cpp
void safe_thread()
{
    std::scoped_lock lock(mtx_a, mtx_b);  // acquire both locks safely in one shot
    // critical section...
}
```

The deadlock-avoidance algorithm inside `scoped_lock` is based on a `try_lock` fallback strategy: try to acquire all the locks in some order; if any `try_lock` fails, release the already-acquired locks and retry in a different order. This algorithm breaks "hold and wait"—one of the four necessary conditions for deadlock—because on a failed acquisition the held locks get released, so the "holding one while waiting for another" situation never exists.

`scoped_lock` also works for the single-mutex case, where it is equivalent to `lock_guard`. But for the clarity of code intent, `lock_guard` is still the recommendation for single-lock scenarios—seeing `lock_guard` tells you there is exactly one lock, and seeing `scoped_lock` tells you multiple locks may be involved, which is valuable information for whoever reads the code.

## lock_guard vs unique_lock vs scoped_lock: A Selection Guide

Let's put the core differences of the three RAII lock guards side by side to help you make a quick choice in real development.

`lock_guard`'s design philosophy is "simplicity is beauty". It is non-copyable, non-movable, cannot unlock early, cannot lock late—these "restrictions" are precisely its strengths, because the more restrictions there are, the less room there is to get it wrong. For 90% of everyday scenarios `lock_guard` is enough: enter the function, construct the `lock_guard`, operate on the shared data, the function returns, and the `lock_guard` destructor releases the lock. The whole flow is one straight line, with no branches.

`unique_lock` fits the 10% of scenarios that need extra flexibility. The most typical is pairing with condition variables—`unique_lock`'s irreplaceable core scenario. Next comes the "copy the data first, then unlock early" pattern—moving time-consuming operations outside the lock to reduce how long it is held. There are also deferred locking and lock ownership transfer, which come into play in more elaborate architectural designs.

`scoped_lock`'s core value is deadlock prevention for multi-lock acquisition. Whenever your code needs to hold two or more locks at the same time, you should use `scoped_lock`. If the project has already adopted C++17, using `scoped_lock` for single-lock scenarios is perfectly fine too—but as a team convention, distinguishing `lock_guard` (single lock) from `scoped_lock` (multiple locks) helps the readability and maintainability of the code.

## Engineering Principle: Never Call lock()/unlock() by Hand

We spent an entire article on the mutex family and RAII lock guards, and the core principle to emphasize at the end comes down to one line: never call `mutex.lock()` and `mutex.unlock()` directly in application code. We have seen the reasons repeatedly above—manually managing lock/unlock is nearly impossible to keep correct across exception paths, multiple return paths, nested calls, and so on, while RAII lock guards bind the lock's lifetime to the scope and eliminate this entire class of bugs at the root.

This principle is recorded explicitly in the C++ Core Guidelines as CP.20: "Use RAII, never plain `lock()`/`unlock()`". The only exception is `adopt_lock`—it takes a mutex that is already locked and is only responsible for unlocking at destruction. But even in that case, the act of locking should have been done through `std::lock()` or some other safe mechanism, not by manually calling `mutex.lock()`.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Run It Online

Try the three RAII lock guards online—lock_guard, unique_lock + condition_variable, and scoped_lock:

<OnlineCompilerDemo
  title="mutex and RAII Locks"
  source-path="code/examples/vol5/10_mutex_raii.cpp"
  description="Experience lock_guard counting, a unique_lock+CV producer-consumer queue, and a safe multi-lock swap with scoped_lock"
  allow-run
/>

## Exercises

### Exercise 1: A Thread-Safe Wrapper for stack

Given a `std::stack<int>`, implement a thread-safe wrapper for it using `std::mutex` and `std::lock_guard`. It must provide four operations: `push()`, `pop()` (returning `std::optional<int>`, and `std::nullopt` when the stack is empty), `top()` (also returning an `optional`), and `empty()`. Hint: `pop()` and `top()` must not return references—because after the unlock, a reference the caller goes on to access is no longer valid.

### Exercise 2: Comparing the Performance of lock_guard and unique_lock

Write a simple benchmark: 4 threads each increment a shared counter 1000000 times, protected by `lock_guard` and `unique_lock` respectively. Compare the runtimes of the two—the difference usually falls within noise, but in extreme scenarios the extra state maintenance of `unique_lock` can show up as measurable overhead. Think about it: under what conditions does this difference become significant?

### Exercise 3: Safely Swapping Two Guarded Pieces of Data with scoped_lock

Suppose there are two `std::vector<int>`s, each protected by its own `std::mutex`. Write a `swap_contents()` function that uses `std::scoped_lock` to acquire both locks simultaneously, then swaps the contents of the two vectors. Verify that calling this function repeatedly in a multi-threaded environment does not deadlock.

## References

- [std::mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/mutex)
- [std::recursive_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/recursive_mutex)
- [std::timed_mutex -- cppreference](https://en.cppreference.com/w/cpp/thread/timed_mutex)
- [std::lock_guard -- cppreference](https://en.cppreference.com/w/cpp/thread/lock_guard)
- [std::unique_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/unique_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.20 -- Use RAII, never plain lock()/unlock()](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
