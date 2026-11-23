---
title: "How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks"
description: "Bring the previous article's lost counts under a lock, and get clear on the use and choice of std::mutex and the three scoped locks."
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter02-sharing-data/02-mutex-and-locks.md
  source_hash: 79eea284156cbbbbec7ded4a70207e7c3e9ec27c0e58eb3b9f0b46fdbcda5c91
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 15419
---
# How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks

The last article ran a counter that undercounts — remember it? Two threads each incremented 160000 times, and under `-O0`, 6 of 30 trials came out wrong. We agreed then that this article would fit it with a lock and reclaim every lost increment.

## Fitting the Counter with a Lock

Fitting the counter with a lock adds only two things: a `std::mutex`, and a `std::lock_guard` constructed when entering the critical section. The critical section — the code that lets only one thread in at a time.

<OnlineCompilerDemo
  title="Fitting the previous article's losing counter with a lock"
  source-path="code/examples/vol5/chapter02/06_demo_mutex_counter.cpp"
  description="One mutex plus lock_guard: 30 trials, not one lost, all 320000. The lock makes read, add, write-back an unbroken chain inside the critical section."
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_mutex_counter.cpp —— 给上一篇丢数的计数器配一把锁
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex> lock(counter_mutex);
    ++counter;
  }
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) ++correct;
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

We ran it and got:

```text
30 of 30 trials printed the expected 320000
```

The same logic in the last article got only 24 of 30 trials right, losing anywhere from two hundred-odd to a hundred fifty thousand. This author reran it at `-O0`: 17 correct, landing inside the 14-to-27 floating range the last article mentioned. Same `-O0` tier, now with the lock: 30 trials, not one lost.

The same lock-free code at `-O2` was 30/30, and this author's rerun was too. Change the optimization level and the compiler folds the 160000 read-modify-writes into a few instructions; the rerun's 30 trials showed no losses. Lock-free is still a data race — `-O2` cannot fix it. Pitting an `-O2` fixed build against an `-O0` lock-free build compares different optimization levels, and the reading gap sits in the compile options. So the count comparison is done uniformly at `-O0`.

`std::mutex` lives in `<mutex>`, usable by us since C++11. It provides the three operations `lock()`, `try_lock()`, `unlock()`, and the standard positions it as a non-recursive mutex with exclusive ownership ([thread.mutex.class], since C++11). While one thread holds it, other threads calling `lock()` block, and calling `try_lock()` immediately returns false, until the holder calls `unlock()`.

Two preconditions for you to check: `lock()` requires that the calling thread does not currently own the lock, and `unlock()` requires that the calling thread does own it ([thread.mutex.requirements.mutex]/6, /21, since C++11). If the same thread locks a `std::mutex` twice in a row, the precondition fails and the behavior is undefined — hanging is merely the common manifestation.

For situations needing recursive locking, the mutex family offers `std::recursive_mutex`; its usage is planned for the recipes in the cookbook (Volume 5's recipe reader) and is not laid out here.

What lets the lock cure the lost counts is the synchronizes-with relationship between one `unlock()` and a subsequent `lock()` that successfully acquires the same lock: in plain words, whatever was written before the previous release is visible to the thread that next acquires. It is one source of happens-before ([thread.mutex.requirements.mutex]/8, /24). We use the lock to chain read, add, write-back together: thread A finishes writing `counter` in the critical section, leaves the scope and unlocks; when thread B acquires the lock, A's write already happens-before its read.

The wrong turn you can easily take here is believing that adding a lock settles everything. The lock doesn't know the data; it only guarantees that one thread at a time gets in. For the program to be correct, every thread touching `counter` must agree to enter through the same gate — whoever slips around it is beyond the lock's reach. When we later discuss which piece of data a lock protects, we will see what slipping around looks like.

## Why Manual lock and unlock Don't Work

Taking over someone else's code, the two lines you are most likely to read are the plain direct calls:

<OnlineCompilerDemo
  title="Manual lock/unlock: an exception flies, the lock stays put"
  source-path="code/examples/vol5/chapter02/07_demo_manual_lock_ub.cpp"
  description="The worker throws inside the critical section and catches its way out, the lock still in its name; the main thread then waits forever (online running has a timeout; locally you will need Ctrl-C). A thread exiting while holding a mutex is undefined behavior."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_manual_lock_ub.cpp —— 手动 lock/unlock：异常一抛，锁留在原地
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  gate.lock();
  throw std::runtime_error("boom");
  gate.unlock();
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s, the lock is still held\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 工作线程退出时仍持有锁：未定义行为，这里是一直等
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
```

</OnlineCompilerDemo>

Here this author deliberately lets the exception fly before the unlock. The worker thread catches it; the main thread then goes for the same lock. It runs like this (wrapped up by `timeout 2`):

```text
[worker] caught boom, the lock is still held
[main] asking for the same lock
exit=124
```

> Interestingly, this author's LLM pointed out: when this program's output is piped, the `stdout` buffer dies together with the killed process. Run it with `stdbuf -oL`, or directly in a terminal, to see the two lines from worker and main. This author does not plan to take responsibility for that promise for now.

The main thread stopped after `[main] asking`; two seconds up, `timeout` killed it, and `[main] got it` never appeared. The worker thread itself had exited, the lock still hanging in its name. The standard writes "a thread exiting while holding a mutex" as undefined behavior ([thread.mutex.class]/5), and what we saw is one of its faces: the main thread waiting forever. A different implementation, a different schedule, might show another face.

Multiple return paths make it worse. Look: every early `return` added inside the critical section adds one more place that must remember to `unlock()`. Exception paths, early returns, someone later adding a `throw` line — all can leave the lock standing where it was. Such errors don't surface at compile time, nor do they necessarily appear on every run.

RAII's way is to bind the lock's lifetime to the scope. `std::lock_guard` is the lightest wrapper, there since C++11: the constructor calls `m.lock()`, the destructor calls `m.unlock()` ([thread.lock.guard]/2, /6). The copy constructor is deleted and there is no move interface — the standard writes both into the class declaration (same clause). It has no `unlock()` member; once the guard is constructed in your hand, nothing remains but to wait for the scope to end.

The same code, switched to `lock_guard`:

<OnlineCompilerDemo
  title="Switch to lock_guard: the lock is released on the exception's way out"
  source-path="code/examples/vol5/chapter02/08_demo_lock_guard_raii.cpp"
  description="Same scene, the lock's lifetime bound to the scope: unwinding carries the destructor along, the lock is released on the exception's way out of the scope, and the main thread then takes it and prints got it."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lock_guard_raii.cpp —— 换成 lock_guard：异常路上就把锁放了
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

std::mutex gate;

void work() {
  std::lock_guard<std::mutex> lock(gate);
  throw std::runtime_error("boom");
}

int main() {
  std::thread t([] {
    try {
      work();
    } catch (const std::exception& e) {
      std::printf("[worker] caught %s\n", e.what());
    }
  });
  t.join();

  std::printf("[main] asking for the same lock\n");
  gate.lock();   // 构造时拿锁、析构时放锁：锁在异常离开作用域的路上就放掉了
  std::printf("[main] got it\n");
  gate.unlock();
  return 0;
}
```

</OnlineCompilerDemo>

```text
[worker] caught boom
[main] asking for the same lock
[main] got it
```

The exception passes out of `work()`, the stack unwinding carries `lock`'s destructor with it, and the lock is released on the way out of the scope. The main thread then takes the lock and prints `got it`. We needn't patch an unlock onto every return path — the scope's end is the unlock point.

One formulation deserves its own word, because it looks like RAII in use:

<OnlineCompilerDemo
  title="A nameless lock_guard: looks like RAII, isn't"
  source-path="code/examples/vol5/chapter02/09_demo_unnamed_guard.cpp"
  description="The temporary object destructs at the statement's end and the lock is released before ++counter: 0 of 30 trials correct. Locally with -Wall you will see the nodiscard warning — exactly the warning discussed in the text."
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_unnamed_guard.cpp —— 没有名字的 lock_guard 临时对象：锁在加一之前就放了
#include <cstdio>
#include <mutex>
#include <thread>

long long counter = 0;
std::mutex counter_mutex;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    std::lock_guard<std::mutex>{counter_mutex};   // 临时对象：语句结束即析构
    ++counter;
  }
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) ++correct;
    else std::printf("trial %d: printed %lld, lost %lld\n",
                     t, counter, 2LL * kRounds - counter);
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

What the braces construct is a nameless temporary object. The statement ends, the temporary destructs, and the lock is released before `++counter`. Switch the last article's experiment to the nameless form, and under `-O0` the 30 trials come out:

```text
trial 0: printed 313868, lost 6132
trial 1: printed 313778, lost 6222
trial 2: printed 314428, lost 5572
0 of 30 trials printed the expected 320000
```

Not one correct round. GCC 16.2.1's libstdc++ marks the lock guard's constructor `[[nodiscard]]`, and by default it emits a `-Wunused-result` warning saying the constructor's return value was ignored.

The warning is the compiler helping out; other implementations may not provide it. The name you must give yourself: write `std::lock_guard<std::mutex> lock(counter_mutex)` — the variable name cannot be omitted. (This author has written the nameless form by reflex too; that warning is what saved me.)

C++ Core Guidelines CP.20 decides exactly this: use RAII, don't call `lock()` and `unlock()` directly. One exception to know: `adopt_lock`, which takes over a lock already acquired by `std::lock()` or a similar algorithm and is only responsible for releasing it at destruction. The acquiring step still goes to the library function, not into application code's hands.

## When the Lock Must Move, Release Early, or Wait: unique_lock

Only when you need to move it, release it early, or accompany a condition-variable wait do you need `std::unique_lock` (since C++11, in `<mutex>`). Wherever `lock_guard` suffices, the lock may as well stay in its scope.

It carries one extra thing over `lock_guard`: an ownership state. We can construct it locking as usual, or with `std::defer_lock` to not lock yet and `lock()` when needed, with `std::try_to_lock` to try once, or with `std::adopt_lock` to take over an already-acquired lock. It provides `owns_lock()` to query whether it currently holds the lock, provides `unlock()` for early release, and it can move: lock ownership can pass from one function into another's hands — `lock_guard` cannot do that.

How much waiting an early unlock saves — let's measure once. In the code, a holder thread takes the lock, copies a snapshot of one hundred thousand integers, then does something time-consuming. Two practices: hold the lock all the way through the work, or release right after the copy and work outside the lock. The main thread goes for the same lock 5 milliseconds later, and we measure how long it waits:

<OnlineCompilerDemo
  title="Release the lock right after the snapshot: the main thread waits 45 ms less"
  source-path="code/examples/vol5/chapter02/10_demo_unlock_early.cpp"
  description="The holder thread copies a snapshot of one hundred thousand integers then does 50 ms of slow work: holding the lock all through makes the main thread wait 45 ms; a unique_lock that unlocks after the copy lets the main thread straight in."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_unlock_early.cpp —— 拷完快照就放锁：主线程少等 45 毫秒
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;

long long run(bool unlock_after_copy) {
  std::mutex m;
  std::vector<int> shared(100000, 1);
  long long waited = -1;

  std::thread holder([&] {
    std::unique_lock<std::mutex> lock(m);
    auto snapshot = shared;
    if (unlock_after_copy) lock.unlock();
    std::this_thread::sleep_for(50ms);   // 顶替耗时的活
    long long sink = 0;
    for (int v : snapshot) sink += v;
    if (sink < 0) std::puts("unreachable");
  });

  std::this_thread::sleep_for(5ms);
  auto t0 = clock_type::now();
  {
    std::lock_guard<std::mutex> lock(m);
    waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                 clock_type::now() - t0).count();
  }
  holder.join();
  return waited;
}

int main() {
  std::printf("lock held through the work: waited %lld ms\n", run(false));
  std::printf("unlock after the copy:     waited %lld ms\n", run(true));
  return 0;
}
```

</OnlineCompilerDemo>

```text
lock held through the work: waited 45 ms
unlock after the copy:     waited 0 ms
```

In the output, the holder thread kept the lock the whole way and the main thread waited 45 milliseconds, having started 5 milliseconds after the holder. Switch to the copy-then-release practice and the main thread gets in almost immediately. The 50-millisecond `sleep_for` is only a stand-in; in reality swap in sorting, formatting, writing files, sending network requests — all the same: these things need no lock, yet they can keep others waiting.

The copy itself costs too. The snapshot occupies memory, copying takes time, and with large data that expense cannot go uncounted. Which side you pick depends on the relative length of two things: if copying is cheaper than waiting, copy it out. If the data is too big to copy, the design must change — you cannot just grip the lock harder and hold on.

`unique_lock`'s extra state carries a little overhead too — typically a pointer plus a bool — and we simply use it where needed. When all you hold is one stretch of scope, `lock_guard` is more peace of mind: it doesn't even give you the chance to "forget whether you're holding the lock."

Over on the condition-variable side, `std::condition_variable::wait` asks precisely for a `std::unique_lock<std::mutex>&`: during the wait it must unlock for us and relock on waking — `lock_guard` has no such ability. `std::unique_lock`'s most common uses are early release and accompanying condition-variable waits. Ownership transfer of locks is left to more elaborate interfaces. Unit 3, How Tasks Wait and Coordinate, is already written, covering the waiting and coordination tools; the entry point is [Waiting for a Condition: From Polling to the Condition Variable](../chapter03-wait-and-coordinate/01-wait-for-a-condition).

Swap the example's `sleep_for` for a slow job you would really have in hand, measure again, and see what the waiting becomes.

## Can Several Locks Be Taken in One Go: scoped_lock

When two locks are in hand at once, nesting `lock_guard` re-enacts the last article's deadlock of reversed locking order. Two objects, each with a lock and a value; both sides must touch both objects, locking in opposite orders:

<OnlineCompilerDemo
  title="Two locks nested: deadlock returns"
  source-path="code/examples/vol5/chapter02/11_demo_two_locks_deadlock.cpp"
  description="Each thread takes one side's lock then demands the other's; after two holding one side lines the process stops (online running has a timeout). The last article's deadlock returns in a new coat."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_two_locks_deadlock.cpp —— 各拿一把再嵌套要对方那把：死锁重现
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::lock_guard<std::mutex> a(from.m);
  std::printf("holding one side\n");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  std::thread t1([&] { move(x, y, 1); });
  std::thread t2([&] { move(y, x, 1); });
  t1.join();
  t2.join();
  std::printf("sum = %lld\n", x.value + y.value);
  return 0;
}
```

</OnlineCompilerDemo>

```text
holding one side
holding one side
exit=124
```

This program likewise loses the buffered output when piped; use `stdbuf -oL`, or run it directly in a terminal.

We watched the two threads each take one lock, each wait for the one in the other's hand, and get killed at two seconds.

Switch it to `std::scoped_lock` (since C++17) — look, the only change is the locking line:

<OnlineCompilerDemo
  title="scoped_lock takes both in one go: deadlock no more"
  source-path="code/examples/vol5/chapter02/12_demo_scoped_lock_transfer.cpp"
  description="Only the locking line changes: two threads each run 200000 opposing transfers, one direction subtracting 1 and the other adding 2; the sum stays 2000000 and it never once sticks."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_scoped_lock_transfer.cpp —— scoped_lock 一趟拿两把：死锁不再来
#include <cstdio>
#include <mutex>
#include <thread>

struct Item {
  std::mutex m;
  long long value = 1000000;
};

void move(Item& from, Item& to, long long amount) {
  std::scoped_lock lock(from.m, to.m);
  from.value -= amount;
  to.value += amount;
}

int main() {
  Item x, y;
  const int kIters = 200000;
  std::thread t1([&] {
    for (int i = 0; i < kIters; ++i) move(x, y, 1);
  });
  std::thread t2([&] {
    for (int i = 0; i < kIters; ++i) move(y, x, 2);
  });
  t1.join();
  t2.join();
  std::printf("sum = %lld, x = %lld, y = %lld\n",
              x.value + y.value, x.value, y.value);
  return 0;
}
```

</OnlineCompilerDemo>

We had the two threads each run 200000 iterations, one direction subtracting 1, the other adding 2:

```text
sum = 2000000, x = 1200000, y = 800000
```

The two numbers always added up to 2000000. We ran this form 40 rounds: never stuck once, and not one cent missing.

Look at `scoped_lock`'s constructor: when handed more than one lock, it calls `std::lock(m...)` ([thread.lock.scoped]/2, since C++17), and at destruction it releases the acquired locks one by one (same section, /6).

The standard's demand of `std::lock` is this: using a combination of `lock()`, `try_lock()`, and `unlock()` it acquires all its arguments with a call sequence that cannot cause deadlock; exactly how it orders them is unspecified ([thread.lock.algorithm]/5). When an acquisition fails it releases everything and retries in a different order — the draft notes call this style try-and-back-off. What it guarantees is only the algorithm itself; we must not hand it locks we already hold.

When only one lock is in hand, `scoped_lock`'s constructor calls `m.lock()` directly, equivalent to `lock_guard` ([thread.lock.scoped]/2). Reading code, we can tell them apart at a glance: seeing `lock_guard` tells you there is exactly one lock here; seeing `scoped_lock` tells you it may take several at once.

Line the three tools up and the differences match at a glance:

| Tool | How many locks | Early release | Movable | Common occasions |
| ------------------ | ---------- | -------- | ------ | ---------------------------------- |
| `std::lock_guard` | One | No | No | A single lock over one scope |
| `std::unique_lock` | One | Yes | Yes | Condition variables, early unlock, lock ownership transfer |
| `std::scoped_lock` | One or more | No | No | Taking two or more together, avoiding ordering deadlocks |

This author left `std::recursive_mutex` and `std::timed_mutex` out of the table. They are the recursive and timed variants of the mutex family, likewise left to the cookbook's recipes.

One more boundary this author must state: `scoped_lock` cannot be handed to a condition variable. `condition_variable::wait` wants a `unique_lock`; `scoped_lock` has no ability to unlock during the wait and relock after. With several locks in hand and a condition to wait on, the formulation returns to `unique_lock` plus `std::lock`.

## Reference Resources

While writing this article, this author drew on Anthony Williams' C++ Concurrency in Action for progression, taking the book's chapters on sharing data. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against the draft text on eel.is and cppreference per the clauses noted in the text.

- [std::mutex (cppreference)][cppref-mutex] — the mutex, since C++11.
- [std::lock_guard (cppreference)][cppref-guard] — the lightweight guard that locks on construction and unlocks on destruction.
- [std::unique_lock (cppreference)][cppref-unique] — the movable, early-releasable lock wrapper.
- [std::scoped_lock (cppreference)][cppref-scoped] — the multi-lock guard, since C++17.
- [std::lock (cppreference)][cppref-lock] — the algorithm that takes several locks at once.
- [C++ working draft: Mutex requirements (thread.mutex.requirements.mutex)][draft-mutex] — lock semantics and synchronization relationships.
- [C++ working draft: Class mutex (thread.mutex.class)][draft-mutex-class] — the non-recursive mutex's definition and ownership rules.
- [C++ working draft: Class template lock_guard (thread.lock.guard)][draft-guard] — construction, destruction, and the class declaration.
- [C++ working draft: Generic locking algorithms (thread.lock.algorithm)][draft-lock] — the guarantees of `std::lock` and `try_lock`.
- [C++ working draft: Class template scoped_lock (thread.lock.scoped)][draft-scoped] — construction and destruction behavior.
- [C++ working draft: thread members (thread.thread.member)][draft-thread-member] — the synchronization between join and thread completion.
- [C++ Core Guidelines: CP.20][cp20] — use RAII, never plain lock()/unlock().

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-guard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[cppref-unique]: https://en.cppreference.com/w/cpp/thread/unique_lock
[cppref-scoped]: https://en.cppreference.com/w/cpp/thread/scoped_lock
[cppref-lock]: https://en.cppreference.com/w/cpp/thread/lock
[draft-mutex]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-guard]: https://eel.is/c++draft/thread.lock.guard
[draft-lock]: https://eel.is/c++draft/thread.lock.algorithm
[draft-scoped]: https://eel.is/c++draft/thread.lock.scoped
[draft-thread-member]: https://eel.is/c++draft/thread.thread.member
[cp20]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp20-use-raii-never-plain-lockunlock
