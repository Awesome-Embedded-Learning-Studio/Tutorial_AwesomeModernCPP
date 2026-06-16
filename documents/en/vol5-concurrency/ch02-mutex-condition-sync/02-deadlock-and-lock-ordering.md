---
chapter: 2
cpp_standard:
- 11
- 14
- 17
- 20
description: Delve into the four necessary conditions for deadlocks, and master lock
  ordering constraints, `try_lock` backoff, and `scoped_lock` multi-lock acquisition
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
  source: documents/vol5-concurrency/ch02-mutex-condition-sync/02-deadlock-and-lock-ordering.md
  source_hash: 6065fab5133f82a0c38e410ec54e5a6aa8a250ebc4c8b9d4d8fdfda3e8d686da
  translated_at: '2026-06-16T04:03:42.892393+00:00'
  engine: anthropic
  token_count: 3595
---
# Deadlock and Lock Ordering

In the previous post, we systematically reviewed the mutex family and three RAII lock guards, mastering the selection strategy from `std::mutex` to `std::shared_mutex`. We mentioned the term "deadlock" repeatedly there, but didn't dive deep—because we wanted to prepare our tools first before confronting this real enemy. Now, let's tackle deadlock head-on.

Deadlock is arguably one of the most frustrating bugs in multithreaded programming. Unlike a data race, which gives you a wrong result, deadlock simply freezes your program. Worse, the conditions for freezing often depend heavily on thread scheduling timing. You might run it locally a hundred thousand times without issue, then it crashes in a customer environment at 3 AM. You grab the dump file, and sure enough—two threads each hold a lock, both waiting for the other to release—classic deadlock.

The goal of this post is clear: first, understand why deadlocks happen (the four necessary conditions); then, master the deadlock prevention tools provided by the C++ Standard Library (`std::lock`, `std::scoped_lock`); and finally, learn several practical deadlock prevention strategies (lock ordering, hierarchical locks, avoiding callbacks).

## Coffman Conditions: The Four Necessary Conditions for Deadlock

In 1971, E. G. Coffman Jr., M. J. Elphick, and A. Shoshani identified four necessary conditions for deadlock in a classic paper. All four conditions must be met **simultaneously** for a deadlock to occur—break any one of them, and deadlock becomes impossible. Understanding these conditions is the theoretical foundation for prevention.

**Mutual Exclusion**: At least one resource can only be held by one thread at any given moment. `std::mutex` is inherently mutually exclusive—only the thread that acquires the lock can enter the critical section; others must wait. This condition is often unbreakable in most scenarios—if resources could be freely shared, we wouldn't need locks.

**Hold and Wait**: A thread holds at least one resource while waiting for other resources. A thread locks mutex A, then tries to lock mutex B. If B is occupied, the thread blocks on B—but it still holds A. This is "hold and wait." If we require a thread to release all currently held locks before acquiring new ones, this condition is broken.

**No Preemption**: Resources cannot be forcibly taken from the holder. If a thread locks a mutex, other threads can't say "get out of the way, let me use it"—they can only wait for the thread to unlock. This condition is also generally unbreakable for standard mutexes—we cannot force another thread to release a lock.

**Circular Wait**: There exists a cycle of waiting dependencies—Thread 1 waits for a resource held by Thread 2, Thread 2 waits for a resource held by Thread 3, ..., Thread N waits for a resource held by Thread 1. If resource acquisition always follows a fixed global order, a cycle cannot form—because ordering is transitive and cannot loop back.

Of the four conditions, Mutual Exclusion and No Preemption are usually intrinsic to the nature of locks and hard to break. Practical engineering strategies for deadlock prevention focus on breaking "Hold and Wait" and "Circular Wait." `std::lock` and `std::scoped_lock` break Hold and Wait—they either acquire all locks at once or acquire none. Lock ordering strategies break Circular Wait—if all threads acquire locks in the same global order, a cycle cannot form.

## The Classic Two-Lock Reversal: AB-BA Deadlock

The most classic deadlock scenario involves inconsistent acquisition orders of two locks. Let's construct a minimal reproduction:

```cpp
// ... (Code preserved)
```

Run this code, and the program will likely freeze. If thread1 acquires `mutex_a` first and thread2 acquires `mutex_b` first, they fall into a circular wait—thread1 holds A and waits for B, thread2 holds B and waits for A, and neither yields. We added a `std::this_thread::sleep_for` to increase the probability of deadlock—in real projects, deadlocks might only appear under specific loads and scheduling timings, which is one reason they are hard to debug.

This example perfectly maps to the Coffman conditions: Mutual Exclusion (mutexes are exclusive), Hold and Wait (thread1 holds A and waits for B), No Preemption (A and B cannot be forcibly taken), and Circular Wait (thread1 waits for thread2, thread2 waits for thread1).

## Lock Ordering: The Most Practical Deadlock Prevention Strategy

Lock Ordering is the most direct and practical strategy for preventing deadlocks. Its core idea is to break Circular Wait—any code that needs to acquire multiple locks must do so in a consistent global order.

If both thread1 and thread2 lock A first and then B, deadlock is impossible. Because only one thread can grab A first, the other will block on A without holding B—thus, no cycle exists.

### Total Order

The simplest lock ordering strategy is to establish a global Total Order—assign a number to every mutex, and any code acquiring multiple mutexes must acquire them in ascending numerical order. It's like a cafeteria line—everyone queues in the same single line, so two people can't block each other.

```cpp
// ... (Code preserved)
```

Note that although the logic is "transfer from B to A," the locking order is still A then B—direction doesn't matter, order does.

### Comparing Addresses: When Numbering Isn't Feasible

In scenarios where mutexes are created dynamically (e.g., each object has its own lock), you can't assign a global number to all mutexes. A common trick here is to compare mutex addresses—lock the one with the lower address first, then the higher one:

```cpp
// ... (Code preserved)
```

There is a detail worth noting here: we manually `lock()` two mutexes first, then pass them to `std::lock_guard` for management via `std::adopt_lock`. This pattern was the standard way to acquire multiple locks in the C++11/14 era—first manually acquire locks using some deadlock avoidance strategy (here, address comparison), then use `std::lock_guard` to ensure exception safety. If you have C++17, just use `std::scoped_lock` directly—it handles this internally.

## std::lock() and std::try_lock(): Standard Library Multi-Lock Tools

C++11 provides two functions for acquiring multiple locks simultaneously: `std::lock` and `std::try_lock`.

### std::lock(): Blocking Multi-Lock Acquisition

`std::lock` accepts any number of `Lockable` objects and uses a deadlock avoidance algorithm to acquire all locks at once. It guarantees that either all locks are acquired successfully, or an exception is thrown and any acquired locks are released. The standard doesn't mandate a specific algorithm, but mainstream implementations use a try-and-back-off strategy—repeatedly trying to `lock()` in different orders, and if one fails, releasing acquired locks and retrying.

```cpp
// ... (Code preserved)
```

This `std::lock` + `std::lock_guard` + `std::adopt_lock` combination was the standard pattern for multi-lock acquisition in the C++11/14 era. Its benefit is that `std::lock_guard`'s destructor correctly releases acquired locks, ensuring exception safety even if an exception is thrown midway.

### std::try_lock(): Non-Blocking Multi-Lock Acquisition

`std::try_lock` is the non-blocking version—it attempts to acquire all locks. If all succeed, it returns `-1`; if any fails, it immediately releases acquired locks and returns the index of the failure (0-based). `std::try_lock` does not retry—it makes only one attempt:

```cpp
// ... (Code preserved)
```

`std::try_lock` is suitable for "try and give up" scenarios—for example, if you have a fallback plan and don't strictly need to wait for the lock. It's also useful for implementing custom back-off strategies, such as a retry mechanism with exponential backoff.

## std::scoped_lock (C++17): Best Practice for Multi-Lock Acquisition

If you have C++17 available, `std::scoped_lock` is the best choice for acquiring multiple locks. It compresses the three-step `std::lock` + `std::lock_guard` + `std::adopt_lock` operation into a single line:

```cpp
// ... (Code preserved)
```

`std::scoped_lock`'s constructor internally calls `std::lock`'s deadlock avoidance algorithm to acquire all mutexes and releases them in reverse order upon destruction. It can also accept a single mutex—in which case it behaves like `std::lock_guard`—but for code clarity, we still recommend `std::lock_guard` for single locks.

If you look back at the "compare addresses" example, rewriting it with `std::scoped_lock` makes the code much cleaner:

```cpp
// ... (Code preserved)
```

Note that we don't even need to compare addresses manually—`std::scoped_lock`'s internal deadlock avoidance algorithm handles it. Of course, if you know the global lock order, passing them in order to `std::scoped_lock` yields better performance (because the internal `lock` back-off happens fewer times). But even if the order is inconsistent, `std::scoped_lock` won't deadlock.

## The try_lock Back-off Pattern: When Order Cannot Be Established

In some scenarios, a global lock order truly cannot be established—for example, in a callback system where callback functions might acquire arbitrary locks, and you can't control the locking order inside them. In such cases, the `try_lock` back-off pattern is a practical choice.

The core idea is: try to acquire all needed locks; if that fails, release any acquired locks, wait a short while, and retry. Since a thread never blocks waiting for another lock while holding one, the "Hold and Wait" condition is broken:

```cpp
// ... (Code preserved)
```

The key to this pattern is: once `try_lock` fails, immediately release all held locks. This means the thread won't block-wait for another lock while holding one—breaking the "Hold and Wait" condition. `std::this_thread::sleep_for` yields the CPU timeslice, avoiding busy-wait waste. In real engineering, you can also use exponential backoff to reduce contention.

Of course, if your project uses C++17, just use `std::scoped_lock` directly—it does exactly this internally.

## Hierarchical Locks: Locking by Role/Level

Hierarchical Locking is a more structured lock ordering strategy. The core idea is to assign a hierarchy level number to each mutex and mandate that threads can only acquire locks from lower levels to higher levels. If a thread already holds a lock at level N, it cannot acquire a lock at a level lower than N. Violating this rule is a programming error that can be detected at runtime.

The advantage of this strategy is that it makes lock ordering constraints explicit—no longer relying on developer memory or documentation, but enforced by the code itself. Let's look at a simplified implementation:

```cpp
// ... (Code preserved)
```

When using it, assign different hierarchy levels to mutexes in different modules:

```cpp
// ... (Code preserved)
```

The beauty of hierarchical locks lies in using a `thread_local` variable to track each thread's current lock level, checking for hierarchy violations on `lock()` and `unlock()`. If violated, it throws an exception immediately—meaning you can catch lock ordering violations during development and testing, rather than discovering a deadlock in production. The cost of this strategy is the extra checking overhead on every `lock` and `unlock`, but for most applications, this overhead is acceptable.

## Avoiding Callbacks While Holding Locks

This is another easily overlooked source of deadlock risk. If your code calls a callback function, virtual function, or any function whose implementation you cannot control while holding a lock, you are entrusting lock safety to someone else's code. The callback might do anything—including acquiring other locks.

```cpp
// ... (Code preserved)
```

If `callback` internally acquires a lock whose holder is in turn waiting for `mutex`, deadlock forms. More subtly, the callback implementation might not acquire any locks today, but six months later someone changes it—boom, deadlock strikes from out of the blue.

The safe approach is to move the callback outside the lock: first, copy the necessary data under the lock's protection, then release the lock, and finally call the callback outside the lock:

```cpp
// ... (Code preserved)
```

This principle can be generalized into a universal rule: **While holding a lock, only manipulate code and data you have complete control over.** Any external interface—callbacks, virtual functions, I/O operations, or even `log` functions—should not be called while holding a lock. This isn't just about preventing deadlock; it's also about minimizing critical section length to improve concurrency.

> 💡 Complete example code is available in [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP), visit `deadlock`.

## Exercises

### Exercise 1: Reproduce and Fix Deadlock

Compile and run the AB-BA deadlock example provided at the beginning of this post. Confirm that the program hangs (if it doesn't hang the first time, try a few times). Then replace the two `std::lock_guard`s with `std::scoped_lock` and confirm the program exits normally. Next, try fixing it using the lock ordering strategy (always lock A then B) and confirm there is no deadlock.

### Exercise 2: Implement Hierarchical Locks and Test

Based on the `HierarchicalMutex` implementation provided in this post, write a test program: create three mutexes at different hierarchy levels. Acquire them in the correct hierarchical order (low to high) and confirm no exception is thrown. Then deliberately violate the hierarchy order (high to low) and confirm it throws `std::logic_error`. Hint: You need `std::this_thread::get_id` to get the thread ID.

### Exercise 3: The Dining Philosophers Problem

The classic Dining Philosophers problem: 5 philosophers sit at a table, each with a chopstick to their left. A philosopher needs both left and right chopsticks to eat. In a naive implementation, each philosopher picks up the left chopstick first, then the right. If all 5 philosophers pick up their left chopstick simultaneously, they all wait for the right chopstick (held by their neighbor), resulting in deadlock. Use the strategies learned in this post (lock ordering or `std::scoped_lock`) to fix this deadlock.

## References

- [std::lock -- cppreference](https://en.cppreference.com/w/cpp/thread/lock)
- [std::try_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/try_lock)
- [std::scoped_lock -- cppreference](https://en.cppreference.com/w/cpp/thread/scoped_lock)
- [C++ Core Guidelines: CP.21 -- Use lock() and unlock() with care](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp21-use-lock-and-unlock-with-care)
- [C++ Core Guidelines: CP.22 -- Never call unknown code while holding a lock](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp22-never-call-unknown-code-while-holding-a-lock-cp22)
- [C++ Concurrency in Action (2nd Edition) -- Anthony Williams, Chapter 3](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
- [System Deadlocks -- Coffman, Elphick, Shoshani (1971)](https://doi.org/10.1145/356586.356588)
