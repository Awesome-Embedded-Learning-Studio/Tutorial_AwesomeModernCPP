---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Dive into the four necessary conditions for dead lock, and master lock
  ordering constraints, `try_lock` fallbacks, and `scoped_lock` multi-lock acquisition
  strategies.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- mutex 与 RAII 锁
reading_time_minutes: 18
related:
- condition_variable 与等待语义
tags:
- host
- cpp-modern
- intermediate
- mutex
title: Deadlock and Lock Ordering
translation:
  engine: anthropic
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/02-deadlock-and-lock-ordering.md
  source_hash: 3610a18246675fc93df655733191a20ffacfe350c0b8125cab6c1c970e7d6a96
  token_count: 3600
  translated_at: '2026-05-20T04:36:27.588635+00:00'
---
# Deadlock and Lock Ordering

In the previous article, we systematically covered the mutex family and three RAII lock guards, mastering the selection strategy from `lock_guard` to `scoped_lock`. We repeatedly mentioned the term "deadlock" in that article, but didn't dive deep into it—because we wanted to get our tools ready first before facing this true enemy. In this article, we confront deadlock head-on.

Deadlock is arguably one of the most frustrating bugs in multithreaded programming. Unlike a data race, which gives you a wrong result, it simply freezes your program. Worse, the conditions for freezing often depend heavily on thread scheduling timing. You might run it locally a hundred thousand times without issue, but it hangs at 3 AM in a customer's production environment. You grab the dump file and see two threads each holding a lock, both waiting for the other to release—classic deadlock.

The goal of this article is clear: first, understand why deadlocks occur (the four necessary conditions), then master the deadlock prevention tools provided by the C++ standard library (`std::lock()`, `std::scoped_lock`), and finally learn several practical deadlock prevention strategies in engineering (lock ordering, hierarchical locks, avoiding callbacks).

## Coffman Conditions: The Four Necessary Conditions for Deadlock

In 1971, E. G. Coffman Jr., M. J. Elphick, and A. Shoshani proposed the four necessary conditions for deadlock in a classic paper. All four conditions must be **simultaneously satisfied** for a deadlock to occur—breaking any one of them makes deadlock impossible. Understanding these four conditions is the theoretical foundation for deadlock prevention.

**Mutual Exclusion**: At least one resource can only be held by one thread at a time. A `std::mutex` is inherently mutually exclusive—only the thread that acquires the lock can enter the critical section, and all other threads must wait. This condition is unbreakable in most scenarios—if a resource could be freely shared, we wouldn't need a lock in the first place.

**Hold and Wait**: A thread holds at least one resource while simultaneously waiting for other resources. A thread locks mutex A, then tries to lock mutex B. B is held by someone else, so the thread blocks on B—but it still holds onto A. This is "hold and wait." If we require a thread to release all currently held locks before acquiring a new one, this condition is broken.

**No Preemption**: Resources cannot be forcibly taken away from their holder. When a thread locks a mutex, other threads can't say "step aside, let me use it"—they can only wait for that thread to unlock it. This condition is also unbreakable with standard mutexes—we cannot force another thread to release a lock.

**Circular Wait**: A circular waiting chain exists—thread 1 waits for a resource held by thread 2, thread 2 waits for a resource held by thread 3, ..., and thread N waits for a resource held by thread 1. If resource acquisition always follows a fixed global order, a circular wait cannot form—because an ordering relation is transitive and cannot form a cycle.

Among the four conditions, mutual exclusion and no preemption are usually inherent to the nature of locks and are not easily broken. Practical deadlock prevention strategies in engineering focus primarily on breaking "hold and wait" and "circular wait." `std::lock()` and `std::scoped_lock` break hold and wait—they either acquire all locks at once or acquire none at all. The lock ordering strategy breaks circular wait—if all threads acquire locks in the same order, the waiting relationship cannot form a cycle.

## The Classic Two-Lock Reversal: The AB-BA Deadlock

The most classic deadlock scenario is inconsistent lock acquisition order with two locks. Let's construct a minimal reproduction:

```cpp
#include <mutex>
#include <thread>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void thread1()
{
    std::lock_guard<std::mutex> lock_a(mtx_a);   // 先锁 A
    std::cout << "thread1: locked A, waiting for B\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // 增加死锁触发概率
    std::lock_guard<std::mutex> lock_b(mtx_b);   // 再锁 B
    std::cout << "thread1: locked both\n";
}

void thread2()
{
    std::lock_guard<std::mutex> lock_b(mtx_b);   // 先锁 B
    std::cout << "thread2: locked B, waiting for A\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard<std::mutex> lock_a(mtx_a);   // 再锁 A
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

When you run this code, the program will most likely hang. If thread1 acquires `mtx_a` first and thread2 acquires `mtx_b` first, both sides fall into a circular wait—thread1 holds A and waits for B, thread2 holds B and waits for A, and neither will let go. We added a `sleep_for` to increase the probability of triggering the deadlock—in real projects, deadlocks might only appear under specific loads and scheduling timings, which is one reason they are so hard to debug.

This example perfectly maps to the four Coffman conditions: mutual exclusion (mutexes are inherently mutually exclusive), hold and wait (thread1 holds A and waits for B), no preemption (neither A nor B can be forcibly taken), and circular wait (thread1 waits for thread2, thread2 waits for thread1).

## Lock Ordering: The Most Practical Deadlock Prevention Strategy

Lock ordering is the most direct and practical strategy for preventing deadlocks. Its core idea is to break circular wait—all code that needs to acquire multiple locks simultaneously must acquire them in the same global order.

If both thread1 and thread2 lock A first and then B, deadlock is impossible. Because only one thread can acquire A first, the other will block on A, and it won't be holding B—so there is no circular wait.

### Total Order

The simplest lock ordering strategy is to establish a global total order—assign a number to every mutex, and any code acquiring multiple mutexes must acquire them in ascending order by number. It's like everyone lining up at a cafeteria—everyone joins the same line, so two people can never block each other.

```cpp
#include <mutex>
#include <thread>
#include <iostream>

// 全局约定：先锁 account_a（ID 较小），再锁 account_b（ID 较大）
std::mutex account_a_mtx;  // "编号" 1
std::mutex account_b_mtx;  // "编号" 2

void transfer_a_to_b(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // 先锁 "编号" 小的
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // 再锁 "编号" 大的
    // 执行转账...
    std::cout << "Transferred " << amount << " from A to B\n";
}

void transfer_b_to_a(int amount)
{
    std::lock_guard<std::mutex> lock_a(account_a_mtx);  // 依然是先锁 "编号" 小的！
    std::lock_guard<std::mutex> lock_b(account_b_mtx);  // 再锁 "编号" 大的
    // 执行反向转账...
    std::cout << "Transferred " << amount << " from B to A\n";
}
```

Note that although the logic of `transfer_b_to_a` is "B transfers to A," the locking order is still A first, then B—the direction doesn't matter, the order does.

### Comparing Addresses: When Numbering Isn't Feasible

In scenarios where mutexes are created dynamically (for example, each object has its own lock), you can't assign a global number to all mutexes. A common trick in this case is to compare the mutex addresses—lock the one with the lower address first, and the one with the higher address second:

```cpp
#include <mutex>
#include <iostream>

class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // 按地址排序加锁，保证全局一致的顺序
        if (&from < &to) {
            from.mtx_.lock();
            to.mtx_.lock();
        } else {
            to.mtx_.lock();
            from.mtx_.lock();
        }

        // 用 adopt_lock 把已获取的锁交给 RAII 守卫管理
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

    // 两个方向的转账都不会死锁
    Account::transfer(a, b, 100);
    Account::transfer(b, a, 50);

    std::cout << "A: " << a.balance() << ", B: " << b.balance() << "\n";
    return 0;
}
```

There is a detail worth noting here: we manually `lock()` both mutexes first, then use `std::adopt_lock` to hand them over to `lock_guard` for management. This pattern was the standard way to acquire multiple locks in the C++11/14 era—first manually acquire the locks using some deadlock avoidance strategy (here, comparing addresses), then use `adopt_lock` to guarantee exception safety. If you have C++17, just use `std::scoped_lock` directly—it handles this internally.

## std::lock() and std::try_lock(): Standard Library Multi-Lock Tools

C++11 provides two functions for acquiring multiple locks simultaneously: `std::lock()` and `std::try_lock()`.

### std::lock(): Blocking Multi-Lock Acquisition

`std::lock()` accepts any number of `Lockable` objects and uses a deadlock avoidance algorithm to acquire all locks at once. Its guarantee is: either all locks are acquired successfully, or it throws an exception and releases any locks already acquired. The standard doesn't specify the exact algorithm, but mainstream implementations use a `try_lock` backoff strategy—repeatedly trying to `try_lock` in different orders, and if one fails, releasing acquired locks and retrying.

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void safe_swap(std::vector<int>& data_a, std::vector<int>& data_b)
{
    // 先构造 defer_lock 的 unique_lock，不实际加锁
    std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
    std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

    // std::lock 一次性安全获取所有锁
    std::lock(lock_a, lock_b);

    // 现在两把锁都已获取，可以安全操作
    data_a.swap(data_b);
}
```

This combination of `defer_lock` + `std::lock()` + `unique_lock` was the standard pattern for acquiring multiple locks in the C++11/14 era. Its benefit is that the destructor of `unique_lock` will correctly release the acquired locks, guaranteeing exception safety even if an exception is thrown in the middle.

### std::try_lock(): Non-Blocking Multi-Lock Acquisition

`std::try_lock()` is the non-blocking version—it attempts to acquire all locks, returns `-1` if all succeed, and if any acquisition fails, it immediately releases all acquired locks and returns the index of the failure (starting from 0). `std::try_lock()` does not retry—it only makes one attempt:

```cpp
#include <mutex>
#include <iostream>

std::mutex mtx_a;
std::mutex mtx_b;

void try_swap(bool& success)
{
    int result = std::try_lock(mtx_a, mtx_b);
    if (result == -1) {
        // 所有锁都获取成功
        std::lock_guard<std::mutex> lock_a(mtx_a, std::adopt_lock);
        std::lock_guard<std::mutex> lock_b(mtx_b, std::adopt_lock);

        // 执行操作...
        success = true;
    } else {
        // 第 result 个锁获取失败
        std::cout << "Failed to acquire lock at index " << result << "\n";
        success = false;
        // 可以做降级处理或者稍后重试
    }
}
```

`std::try_lock()` is suitable for "if we can't get it, forget it" scenarios—for example, when you have a fallback plan and don't absolutely need to wait for the lock. It's also useful for implementing custom backoff strategies—such as a retry mechanism with exponential backoff.

## std::scoped_lock (C++17): The Best Practice for Multi-Lock Acquisition

If you have C++17 available, `std::scoped_lock` is the best choice for acquiring multiple locks. It compresses the three-step operation of `defer_lock` + `std::lock()` + `unique_lock` into a single line:

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
    std::scoped_lock lock(mtx_a, mtx_b);  // 一行搞定：安全获取 + RAII 管理
    data_a.swap(data_b);
}
```

The constructor of `scoped_lock` internally calls `std::lock()`'s deadlock avoidance algorithm to acquire all mutexes, and releases them in reverse order upon destruction. It can also accept a single mutex—in which case its behavior is identical to `lock_guard`, but for code clarity, we still recommend using `lock_guard` for a single lock.

If you look back at the earlier "comparing addresses" example, rewriting it with `scoped_lock` makes the code much more concise:

```cpp
class Account {
public:
    explicit Account(int balance) : balance_(balance) {}

    static void transfer(Account& from, Account& to, int amount)
    {
        // scoped_lock 内部自动处理死锁避免，不需要手动比较地址
        std::scoped_lock lock(from.mtx_, to.mtx_);

        from.balance_ -= amount;
        to.balance_ += amount;
    }

private:
    mutable std::mutex mtx_;
    int balance_;
};
```

Note that we don't even need to manually compare addresses—the internal deadlock avoidance algorithm of `scoped_lock` handles it. Of course, if you know the global lock order, passing them in order to `scoped_lock` yields better performance (because the internal `try_lock` backoffs happen fewer times). But even if the order is inconsistent, `scoped_lock` will not deadlock.

## The try_lock Backoff Pattern: When Order Cannot Be Established

In some scenarios, a global lock order truly cannot be established—for example, if you have a callback system where callback functions might acquire arbitrary locks, and you can't control the locking order within them. In such cases, the `try_lock` backoff pattern is a practical choice.

The core idea is: try to acquire all needed locks, and if that fails, release all acquired locks, wait a short while, and retry. Because a thread never blocks while waiting for another lock while holding one, the "hold and wait" condition is broken:

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
        // 尝试获取第一把锁
        std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
        if (!lock_a.try_lock()) {
            std::this_thread::yield();
            continue;
        }

        // 持有第一把锁，尝试获取第二把
        std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);
        if (!lock_b.try_lock()) {
            // 获取第二把失败，释放第一把，回退
            lock_a.unlock();
            std::this_thread::yield();
            continue;
        }

        // 两把锁都拿到了
        break;
    }

    // 临界区...
}
```

The key to this pattern is: once `try_lock` fails, immediately release all held locks. This means a thread will never block waiting for another lock while holding one—the "hold and wait" condition is broken. The role of `yield()` is to yield the CPU time slice, avoiding the waste of busy-waiting. In real engineering, you can also use exponential backoff to reduce contention.

Of course, if your project can use C++17, just use `scoped_lock` directly—it does exactly this internally.

## Hierarchical Locks: Locking by Role/Level

Hierarchical locking is a more structured lock ordering strategy. The core idea is to assign a hierarchy number to each mutex, stipulating that threads can only acquire locks from lower levels to higher levels—if a thread currently holds a lock at level N, it cannot acquire a lock at a level lower than N. Violating this rule is a programming error and can be detected at runtime.

The advantage of this strategy is that it makes the lock ordering constraint explicit—instead of relying on developer memory and documentation, it's enforced by the code itself. Let's look at a simplified implementation:

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

When using it, assign different hierarchy levels to mutexes in different modules:

```cpp
HierarchicalMutex high_level_mutex(10000);    // 高层：应用层
HierarchicalMutex mid_level_mutex(5000);      // 中层：业务逻辑
HierarchicalMutex low_level_mutex(100);       // 低层：底层 IO

void high_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(high_level_mutex);
    // 允许：10000 > 5000，可以向更低层级获取
    mid_level_operation();
}

void mid_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(mid_level_mutex);
    // 允许：5000 > 100
    low_level_operation();
}

void low_level_operation()
{
    std::lock_guard<HierarchicalMutex> lock(low_level_mutex);
    // 如果在这里尝试获取 mid_level_mutex，会抛异常！
    // 因为 100 < 5000，违反了层级约束
}
```

The elegance of hierarchical locks lies in using a `thread_local` variable to track each thread's current lock level, checking for hierarchy violations during `lock()`. If a violation occurs, it throws an exception immediately—meaning you can catch lock ordering violations during development and testing, rather than discovering the problem only when a deadlock appears in production. The cost of this strategy is the extra checking overhead on every `lock()` and `unlock()`, but for most applications this overhead is perfectly acceptable.

## Avoiding Callbacks While Holding a Lock

This is another easily overlooked source of deadlock risk. If your code calls a callback function, a virtual function, or any function whose implementation you cannot control while holding a lock, you are entrusting the lock's safety to someone else's code. The callback function could do anything—including acquiring other locks.

```cpp
#include <mutex>
#include <functional>
#include <iostream>

std::mutex data_mtx;

class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        std::lock_guard<std::mutex> lock(data_mtx);  // 持锁
        int value = get_latest_value();
        callback(value);  // 危险！回调可能获取其他锁
    }

private:
    int get_latest_value() { return 42; }
};
```

If `callback` internally acquires some lock, and the holder of that lock is in turn waiting for `data_mtx`, a deadlock forms. Even more insidiously, the callback's implementation might not acquire any locks today, but six months from now someone changes its implementation—boom, a deadlock appears out of nowhere.

The safe approach is to move the callback invocation outside the lock: first copy the needed data under the lock's protection, then release the lock, and finally call the callback outside the lock:

```cpp
class EventSystem {
public:
    void on_data_update(std::function<void(int)> callback)
    {
        int value;
        {
            std::lock_guard<std::mutex> lock(data_mtx);
            value = get_latest_value();
        }  // 锁在这里释放
        callback(value);  // 安全：不在持锁状态下调用回调
    }

private:
    int get_latest_value() { return 42; }
};
```

This principle can be generalized into a universal rule: **while holding a lock, only manipulate code and data you have complete control over**. Any external interface—callbacks, virtual functions, I/O operations, or even `std::cout`—should not be called while holding a lock. This isn't just about preventing deadlocks; it's also about reducing critical section length to improve concurrency.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `code/volumn_codes/vol5/ch02-mutex-condition-sync/`.

## Exercises

### Exercise 1: Reproduce and Fix a Deadlock

Compile and run the AB-BA deadlock example provided at the beginning of this article, and confirm that the program hangs (if it doesn't hang on the first try, try a few more times). Then replace the two `lock_guard` with `std::scoped_lock`, and confirm that the program exits normally. Next, try fixing it using the lock ordering strategy (uniformly locking A first, then B), and similarly confirm there is no deadlock.

### Exercise 2: Implement and Test Hierarchical Locks

Based on the `HierarchicalMutex` implementation provided in this article, write a test program: create three mutexes at different hierarchy levels, acquire them in the correct hierarchy order (from high to low), and confirm no exception is thrown; then deliberately violate the hierarchy order (acquiring from low to high), and confirm that a `std::logic_error` is thrown. Hint: you need `#include <limits>` to obtain the `std::numeric_limits`.

### Exercise 3: The Dining Philosophers Problem

The classic dining philosophers problem: five philosophers sit around a table, each with a chopstick on their left. A philosopher needs to pick up both the left and right chopsticks simultaneously to eat. In a naive implementation, each philosopher picks up the left chopstick first, then the right one—all five philosophers pick up their left chopstick at the same time, then all wait for their right chopstick (which is being held by the person on their right), resulting in a deadlock. Use the strategies learned in this article (lock ordering or `scoped_lock`) to fix this deadlock.

## References

- [std::lock -- cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::try_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/try_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.21 -- Use lock() and unlock() with care](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-lock-and-unlock-with-care)
- [C++ Core Guidelines: CP.22 -- Never call unknown code while holding a lock](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp22-never-call-unknown-code-while-holding-a-lock-cp22)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- [System Deadlocks -- Coffman, Elphick, Shoshani (1971)](https://doi.org/10.1145/356586.356588)
