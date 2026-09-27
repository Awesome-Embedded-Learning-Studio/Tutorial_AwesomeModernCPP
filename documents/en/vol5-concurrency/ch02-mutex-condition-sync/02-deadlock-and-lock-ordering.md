---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Dig into the four necessary conditions for deadlock, and master lock
  ordering constraints, `try_lock` backoff, and `scoped_lock` multi-lock acquisition
  strategies
difficulty: intermediate
order: 2
platform: host
prerequisites:
- mutex and RAII Locks
reading_time_minutes: 18
related:
- condition_variable and Wait Semantics
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Deadlock and Lock Ordering
translation:
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/02-deadlock-and-lock-ordering.md
  source_hash: 6065fab5133f82a0c38e410ec54e5a6aa8a250ebc4c8b9d4d8fdfda3e8d686da
  translated_at: '2026-09-26T07:02:03+00:00'
  engine: anthropic
  token_count: 8800
---
# Deadlock and Lock Ordering

In the previous article we systematically walked through the mutex family and the three RAII lock guards, and nailed down how to choose among them from `lock_guard` to `scoped_lock`. That article kept mentioning the word "deadlock" without ever digging into it—because we wanted to get our tools ready first, then confront the real enemy. This article is where we take on deadlock head-on.

Deadlock is arguably one of the most headache-inducing bugs in multithreaded programming. Unlike a data race, which hands you a wrong result, deadlock simply makes your program freeze—and whether it freezes often depends heavily on thread scheduling timing. You run it locally a hundred thousand times without a hitch, it goes live, and at 3 a.m. it hangs in a customer environment. You pull the dump file, look inside, and there they are: two threads, each holding one lock, both waiting for the other to release—classic deadlock.

The goal of this article is clear: first understand why deadlock happens (the four necessary conditions); then master the deadlock-prevention tools the C++ standard library provides (`std::lock()` and `std::scoped_lock`); and finally learn a few deadlock-prevention strategies from engineering practice (lock ordering, hierarchical locks, avoiding callbacks).

## The Coffman Conditions: Four Necessary Conditions for Deadlock

In 1971, E. G. Coffman Jr., M. J. Elphick, and A. Shoshani laid out four necessary conditions for deadlock in a classic paper. All four must hold **at the same time** for deadlock to occur—break any one of them and deadlock becomes impossible. Understanding these four conditions is the theoretical foundation of deadlock prevention.

**Mutual exclusion**: at least one resource can be held by only one thread at a time. `std::mutex` is mutual exclusion by nature—only the thread that has acquired the lock can enter the critical section; every other thread has to wait. In most scenarios this condition cannot be broken—if the resource could be freely shared, you wouldn't need a lock in the first place.

**Hold and wait**: a thread holds at least one resource while waiting for another. A thread locks mutex A, then tries to lock mutex B; B is held by someone else, so the thread blocks on B—while still hanging on to A. That is "hold and wait". If we require a thread to release every lock it holds before acquiring a new one, this condition is broken.

**No preemption**: a resource cannot be forcibly taken away from its holder. Once a thread locks a mutex, no other thread can say "excuse me, step aside, it's my turn"—they can only wait for that thread to unlock it itself. With standard mutexes this condition is also unbreakable—we cannot force another thread to release a lock.

**Circular wait**: there exists a cycle of waiting threads—thread 1 waits for a resource held by thread 2, thread 2 waits for a resource held by thread 3, ..., thread N waits for a resource held by thread 1. If resources are always acquired in some fixed global order, a circular wait can never form—an ordering relation is transitive, so it cannot loop back on itself.

Of the four conditions, mutual exclusion and no preemption are dictated by the very nature of locks and are hard to break. Deadlock-prevention strategies in real engineering therefore concentrate on breaking hold-and-wait and circular wait. `std::lock()` and `std::scoped_lock` break hold and wait—they acquire all the locks at once, or none of them. The lock-ordering strategy breaks circular wait—if every thread acquires locks in the same order, the wait relationships cannot form a cycle.

## The Classic Two-Lock Inversion: The AB-BA Deadlock

The most classic deadlock scenario is two locks acquired in inconsistent orders. Let's build a minimal reproduction:

```cpp
#include <mutex>
#include <thread>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);   // lock A first
    std::cout << "thread1: locked A, waiting for B\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // raise the odds of the deadlock firing
    std::lock_guard<std::mutex> lock_b(mtx_b);   // then lock B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);   // lock B first
    std::cout << "thread2: locked B, waiting for A\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard<std::mutex> lock_a(mtx_a);   // then lock A
    std::cout << "thread2: locked both\n";
}

int main()
{
    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();
    t2.join();
    return 0;
}
```

Run this code and the program will most likely get stuck. If thread1 grabs `mtx_a` first and thread2 grabs `mtx_b` first, both sides fall into a circular wait—thread1 holds A and waits for B, thread2 holds B and waits for A, and neither will ever let go. The `sleep_for` is there to raise the odds of the deadlock firing—in a real project, a deadlock may only appear under a particular load and scheduling interleaving, which is one of the reasons it is so hard to debug.

This example maps perfectly onto the four Coffman conditions: mutual exclusion (a mutex is exclusive by nature), hold and wait (thread1 holds A while waiting for B), no preemption (neither A nor B can be forcibly taken away), and circular wait (thread1 waits for thread2, thread2 waits for thread1).

## Lock Ordering: The Most Practical Deadlock-Prevention Strategy

Lock ordering is the most direct and most practical deadlock-prevention strategy. Its core idea is to break circular wait—all code that needs to hold several locks at once must acquire them in the same global order.

If both thread1 and thread2 lock A before B, deadlock cannot happen. Only one thread can get A first; the other blocks on A without holding B—so no circular wait can exist.

### Total Order

The simplest lock-ordering strategy is to establish a global total order—number every mutex, and require any code that acquires several of them to take them in ascending order. It's like the queue at a cafeteria: everyone lines up in the same line, so no two people can end up blocking each other.

```cpp
#include <mutex>
#include <thread>
#include <iostream>

// Global convention: lock account_a first (smaller ID), then account_b (larger ID)
std::mutex account_a_mtx;  // "number" 1
std::mutex account_b_mtx;  // "number" 2

void transfer_a_to_b(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // lock the smaller "number" first
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // then the larger "number"
    // perform the transfer...
    std::cout << "Transferred " << amount << " from A to B\n";
}

void transfer_b_to_a(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // still the smaller "number" first!
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // then the larger "number"
    // perform the reverse transfer...
    std::cout << "Transferred " << amount << " from B to A\n";
}
```

Note that although `transfer_b_to_a` logically moves money "from B to A", its locking order is still A before B—the direction doesn't matter; the order does.

### Comparing Addresses: When Numbering Isn't Feasible

When mutexes are created dynamically (say, each object carries its own lock), you can't assign them all global numbers. A common trick in that situation is to compare the mutexes' addresses—lock the lower address first, the higher one second:

```cpp
#include <mutex>
#include <iostream>

class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // Lock in address order to guarantee a globally consistent order
        if (&from < &to) {
            from.mtx_.lock();
            to.mtx_.lock();
        } else {
            to.mtx_.lock();
            from.mtx_.lock();
        }

        // Hand the already-acquired locks over to RAII guards with adopt_lock
        std::lock_guard<std::mutex> lock_from(from.mtx_, std::adopt_lock);
        std::lock_guard<std::mutex> lock_to(to.mtx_, std::adopt_lock);

        from.balance_ -= amount;
        to.balance_ += amount;
    }

    int balance() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return balance_;
    }

private:
    mutable std::mutex mtx_;
    int balance_;
};

int main()
{
    Account a(1000);
    Account b(2000);

    // Neither transfer direction can deadlock
    Account::transfer(a, b, 100);
    Account::transfer(b, a, 50);

    std::cout << "A: " << a.balance() << ", B: " << b.balance() << "\n";
    return 0;
}
```

One detail here deserves attention: we manually `lock()` both mutexes, then hand them to `lock_guard` via `std::adopt_lock`. In the C++11/14 era this was the standard way to acquire multiple locks—first acquire them manually through some deadlock-avoidance strategy (here, comparing addresses), then use `adopt_lock` to keep things exception-safe. If you have C++17, just use `std::scoped_lock`—it handles all of this automatically.

## std::lock() and std::try_lock(): The Standard Library's Multi-Lock Tools

C++11 provides two functions for acquiring multiple locks at once: `std::lock()` and `std::try_lock()`.

### std::lock(): Blocking Multi-Lock Acquisition

`std::lock()` takes any number of `Lockable` objects and acquires all of them at once using a deadlock-avoidance algorithm. Its guarantee: either every lock is acquired, or an exception is thrown and the already-acquired locks are released. The standard doesn't mandate a specific algorithm, but mainstream implementations all use a `try_lock` back-off strategy—repeatedly attempting `try_lock` in varying orders, and if one attempt fails, releasing the acquired locks and retrying.

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void safe_swap(std::vector<int>& data_a, std::vector<int>& data_b)
{
    // First construct defer_lock unique_locks without actually locking
    std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
    std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

    // std::lock safely acquires all locks at once
    std::lock(lock_a, lock_b);

    // Both locks are now held, so it is safe to operate
    data_a.swap(data_b);
}
```

This `defer_lock` + `std::lock()` + `unique_lock` combination was the standard multi-lock pattern of the C++11/14 era. Its virtue is that the `unique_lock` destructor correctly releases the acquired locks, so you stay exception-safe even if an exception is thrown in between.

### std::try_lock(): Non-Blocking Multi-Lock Acquisition

`std::try_lock()` is the non-blocking version—it tries to acquire all the locks; if every acquisition succeeds it returns `-1`, and if one fails it immediately releases the locks it did get and returns the zero-based index of the failure. `std::try_lock()` does not retry—it makes exactly one round of attempts:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void try_swap(bool& success)
{
    int result = std::try_lock(mtx_a, mtx_b);
    if (result == -1) {
        // All locks acquired successfully
        std::lock_guard<std::mutex> lock_a(mtx_a, std::adopt_lock);
        std::lock_guard<std::mutex> lock_b(mtx_b, std::adopt_lock);

        // do the work...
        success = true;
    } else {
        // Lock number result failed to be acquired
        std::cout << "Failed to acquire lock at index " << result << "\n";
        success = false;
        // fall back to an alternative, or retry later
    }
}
```

`std::try_lock()` suits the "if I can't get it, never mind" scenarios—say you have a fallback plan and don't absolutely need the lock. It also works well for implementing custom back-off strategies, such as a retry mechanism combined with exponential backoff.

## std::scoped_lock (C++17): The Best Practice for Multi-Lock Acquisition

If C++17 is available to you, `std::scoped_lock` is the best choice for acquiring multiple locks. It compresses the three-step `defer_lock` + `std::lock()` + `unique_lock` sequence into a single line:

```cpp
#include <mutex>
#include <iostream>
#include <vector>

std::mutex mtx_a;
std::mutex mtx_b;
std::vector<int> data_a;
std::vector<int> data_b;

void modern_safe_swap()
{
    std::scoped_lock lock(mtx_a, mtx_b);  // one line does it all: safe acquisition + RAII management
    data_a.swap(data_b);
}
```

Internally, `scoped_lock`'s constructor acquires all the mutexes using the deadlock-avoidance algorithm of `std::lock()`, and its destructor releases them in reverse order. It can also take a single mutex—in that case it behaves the same as `lock_guard`, but for code clarity, single locks are still better written with `lock_guard`.

If you look back at the earlier "comparing addresses" example, rewriting it with `scoped_lock` makes the code much cleaner:

```cpp
class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // scoped_lock handles deadlock avoidance internally; no manual address comparison needed
        std::scoped_lock lock(from.mtx_, to.mtx_);

        from.balance_ -= amount;
        to.balance_ += amount;
    }

private:
    mutable std::mutex mtx_;
    int balance_;
};
```

Notice that we don't even need to compare addresses manually anymore—the deadlock-avoidance algorithm inside `scoped_lock` takes care of it. Of course, if you know the global order of the locks, passing them to `scoped_lock` in that order performs better (fewer internal `try_lock` back-offs). But even when the order is inconsistent, `scoped_lock` won't deadlock.

## The try_lock Back-Off Pattern: When No Order Can Be Established

In some scenarios a global lock order genuinely cannot be established—say you have a callback system where the callback functions may acquire arbitrary locks, and you have no control over their locking order. That's where the `try_lock` back-off pattern becomes a practical choice.

The core idea: try to acquire all the locks you need; if that fails, release the locks you already hold, wait a short while, and try again. Since a thread never blocks waiting for one lock while already holding another, the hold-and-wait condition is broken:

```cpp
#include <mutex>
#include <thread>
#include <chrono>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void try_lock_with_backoff()
{
    while (true) {
        // Try to acquire the first lock
        std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
        if (!lock_a.try_lock()) {
            std::this_thread::yield();
            continue;
        }

        // Holding the first lock, try to acquire the second
        std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);
        if (!lock_b.try_lock()) {
            // Failed to get the second lock; release the first and back off
            lock_a.unlock();
            std::this_thread::yield();
            continue;
        }

        // Both locks acquired
        break;
    }

    // critical section...
}
```

The key to this pattern: the moment a `try_lock` fails, immediately release every lock you already hold. That means a thread never blocks waiting for one lock while holding another—the hold-and-wait condition is broken. The point of `yield()` is to give up the CPU time slice and avoid wasting it in a busy wait. In real projects you can also use exponential backoff to reduce contention.

Of course, if your project can use C++17, just use `scoped_lock` directly—this is exactly what it does internally.

## Hierarchical Locks: Locking by Role or Level

A hierarchical lock is a more structured lock-ordering strategy. The core idea is to assign every mutex a level number and decree that threads may only acquire locks from lower levels toward higher levels—if the current thread already holds a lock at level N, it may not acquire any lock below level N. Violating this rule is a programming error, and one that can be detected at run time.

The strength of this strategy is that it makes the lock-ordering constraint explicit—it no longer relies on developers' memory and documentation; the code itself enforces it. Let's look at a simplified implementation:

```cpp
#include <mutex>
#include <stdexcept>
#include <thread>
#include <limits>

class HierarchicalMutex {
public:
    explicit HierarchicalMutex(unsigned long level)
        : hierarchy_level_(level)
    {}

    void lock()
    {
        check_for_hierarchy_violation();
        internal_mutex_.lock();
        update_previous_level();
    }

    void unlock()
    {
        this_thread_hierarchy_level_ = previous_level_;
        internal_mutex_.unlock();
    }

    bool try_lock()
    {
        check_for_hierarchy_violation();
        if (!internal_mutex_.try_lock()) {
            return false;
        }
        update_previous_level();
        return true;
    }

private:
    void check_for_hierarchy_violation()
    {
        if (hierarchy_level_ >= this_thread_hierarchy_level_) {
            throw std::logic_error("Mutex hierarchy violated");
        }
    }

    void update_previous_level()
    {
        previous_level_ = this_thread_hierarchy_level_;
        this_thread_hierarchy_level_ = hierarchy_level_;
    }

    std::mutex internal_mutex_;
    unsigned long const hierarchy_level_;
    unsigned long previous_level_;
    static thread_local unsigned long this_thread_hierarchy_level_;
};

thread_local unsigned long HierarchicalMutex::this_thread_hierarchy_level_
    = std::numeric_limits<unsigned long>::max();
```

In use, you assign the mutexes of different modules to different levels:

```cpp
HierarchicalMutex high_level_mutex(10000);    // high level: application layer
HierarchicalMutex mid_level_mutex(5000);      // middle level: business logic
HierarchicalMutex low_level_mutex(100);       // low level: low-level IO

void high_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(high_level_mutex);
    // Allowed: 10000 > 5000, we may still acquire toward lower levels
    mid_level_operation();
}

void mid_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(mid_level_mutex);
    // Allowed: 5000 > 100
    low_level_operation();
}

void low_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(low_level_mutex);
    // Trying to acquire mid_level_mutex here would throw!
    // 100 < 5000 violates the hierarchy constraint
}
```

The elegance of hierarchical locks is that they use a `thread_local` variable to track each thread's current lock level, checking for a hierarchy violation inside `lock()`. On violation, they throw immediately—which means you can catch lock-ordering violations during development and testing instead of discovering the problem as a deadlock in production. The cost of this strategy is an extra check on every `lock()` and `unlock()`, but for most applications that overhead is perfectly acceptable.

## Avoiding Callbacks While Holding a Lock

This is another easily overlooked source of deadlock risk. If your code calls a callback function, a virtual function, or any function whose implementation you cannot control while holding a lock, you have handed the safety of your lock to someone else's code. A callback can do anything—including acquiring other locks.

```cpp
#include <mutex>
#include <functional>
#include <iostream>

std::mutex data_mtx;

class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        std::lock_guard<std::mutex> lock(data_mtx);  // holding the lock
        int value = get_latest_value();
        callback(value);  // Danger! The callback may acquire other locks
    }

private:
    int get_latest_value() { return 42; }
};
```

If `callback` internally acquires some lock whose holder is in turn waiting for `data_mtx`, a deadlock has formed. The sneakier part: the callback's implementation might acquire no locks today, but six months from now someone changes it—and boom, deadlock falls out of the sky.

The safe approach moves the callback invocation outside the lock: first copy the data you need under the lock's protection, then release the lock, and finally call the callback outside the lock:

```cpp
class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        int value;
        {
            std::lock_guard<std::mutex> lock(data_mtx);
            value = get_latest_value();
        }  // the lock is released here
        callback(value);  // safe: the callback runs without holding the lock
    }

private:
    int get_latest_value() { return 42; }
};
```

This principle generalizes into a universal rule: **while holding a lock, touch only the code and data you fully control**. No external interface—callbacks, virtual functions, I/O operations, even `std::cout`—should be invoked while holding a lock. This is not only about preventing deadlock; it also shrinks the critical section and improves concurrency.

> 💡 The complete example code is in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP); visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Reproduce and Fix a Deadlock

Build and run the AB-BA deadlock example from the beginning of this article, and confirm that the program hangs (if it doesn't hang on the first try, try a few more times). Then replace the two `lock_guard`s with `std::scoped_lock` and confirm that the program exits normally. Finally, fix it with the lock-ordering strategy (uniformly lock A before B) and likewise confirm there is no deadlock.

### Exercise 2: Implement a Hierarchical Mutex and Test It

Based on the `HierarchicalMutex` implementation provided in this article, write a test program: create three mutexes at different levels, acquire them in the correct level order (from high to low), and confirm that no exception is thrown; then deliberately violate the hierarchy order (acquiring from low to high) and confirm that `std::logic_error` is thrown. Hint: you need `#include <limits>` for `std::numeric_limits`.

### Exercise 3: The Dining Philosophers Problem

The classic dining philosophers problem: 5 philosophers sit around a table, each with one chopstick to their left, and a philosopher needs to pick up both the left and right chopsticks to eat. In the naive implementation, every philosopher picks up the left chopstick first and then the right—all 5 philosophers simultaneously pick up their left-hand chopsticks, and then each waits for the right-hand one (held by the person on their right): deadlock. Fix this deadlock with the strategies learned in this article (lock ordering or `scoped_lock`).

## References

- [std::lock -- cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::try_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/try_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.21 -- Use lock() and unlock() with care](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-lock-and-unlock-with-care)
- [C++ Core Guidelines: CP.22 -- Never call unknown code while holding a lock](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp22-never-call-unknown-code-while-holding-a-lock-cp22)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- [System Deadlocks -- Coffman, Elphick, Shoshani (1971)](https://doi.org/10.1145/356586.356588)
