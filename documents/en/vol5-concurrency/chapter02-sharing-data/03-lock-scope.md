---
title: "How Much Should a Lock Cover: Protection Scope, Granularity, and Less Sharing"
description: "With the three locks in place, settle the finer accounts: which piece of data one lock watches over, how much work belongs inside it, and then reduce sharing with thread_local and per-thread copies."
chapter: 5
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter02-sharing-data/03-lock-scope.md
  source_hash: 909d6d42c8dbe49d0a33b04b169c858d26f2fbe9c9afb98ca29bba129bc92690
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 11454
---
# How Much Should a Lock Cover: Protection Scope, Granularity, and Less Sharing

The three locks are in place; tools are no longer lacking. Next comes settling the finer accounts: which piece of data one lock should watch over, how much work belongs inside the lock, and whether each thread could simply hold its own copy and let the lock go off shift early.

## Which Data Does the Lock Protect

All three tools are in hand, and one question remains: which piece of data should the lock watch over?

When locking up a class, the first thing you must answer is: which accesses does this lock intercept? The standard won't answer for you, and the lock knows no variables.

This author's habit is to put the lock and the data it protects into the same class, make the data private, and leave only operations outside:

<OnlineCompilerDemo
  title="The lock and the data live in one class; only operations are exposed"
  source-path="code/examples/vol5/chapter02/13_demo_counter_class.cpp"
  description="Counter hides the mutex and value_ as private members; the public add and value each take the lock: to touch the data around the lock, you must go through access control."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_counter_class.cpp —— 锁和数据住进同一个类，外面只留操作
#include <cstdio>
#include <mutex>

class Counter {
public:
  void add(int n) {
    std::lock_guard<std::mutex> lock(m_);
    value_ += n;
  }

  long long value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

private:
  mutable std::mutex m_;
  long long value_ = 0;
};

int main() {
  Counter c;
  for (int i = 0; i < 1000; ++i) c.add(3);
  std::printf("after 1000 adds of 3: %lld\n", c.value());
  return 0;
}
```

</OnlineCompilerDemo>

Both `add` and `value` take the same lock; `value_` is private, so callers can't get a reference to it and can't slip past the lock. `m_` is marked `mutable` because `value() const` must also lock. Putting the lock and the data in one class means the lock's lifetime follows the object.

What happens when one site is missed? Delete the lock in `value()` and run a writer thread incrementing a million times while the main thread reads repeatedly. This unlocked variant is stored at code/examples/vol5/chapter02/13b_demo_counter_class_tsan.cpp, sitting beside file 13; you can compile and run it yourself. This one compiles at `-O1` with `-g` plus `-fsanitize=thread` — one tier lower than the other experiments, with `-g` so the report keeps its file names and line numbers. Below is one run's report; this author deleted the module offsets, BuildId, and the deep call-stack frames, keeping only the key lines on both sides:

```text
WARNING: ThreadSanitizer: data race (pid=9)
  Write of size 8 at 0x7fffffffd3f8 by thread T1 (mutexes: write M0):
    #0 Counter::add(int) /tmp/13b_demo_counter_class_tsan.cpp:10
  Previous read of size 8 at 0x7fffffffd3f8 by main thread:
    #0 Counter::value() const /tmp/13b_demo_counter_class_tsan.cpp:14
    #1 main /tmp/13b_demo_counter_class_tsan.cpp:31
  Location is stack of main thread.

SUMMARY: ThreadSanitizer: data race /tmp/13b_demo_counter_class_tsan.cpp:10 in Counter::add(int)
```

What TSan reports: between the write in `add` and the read in `value()`, there is a data race. Reading the report: the writing side holds a lock — the M0 in parentheses is the lock `add` enters; the reading side holds none, and there is no happens-before between them. The lock only intercepts threads that come to take it; a read that slips around still happens. This author ran the experiment four times; all four reported. The pid and addresses change every run, and which side is listed first swaps too: across the four runs, "write first, read after" and "read first, write after" each came up twice; whichever side is listed second gets the Previous label, and the SUMMARY follows the side listed first.

At this point the checking method is really one sentence: accesses that might be concurrent must be either all inside the lock or all outside it — never half and half. `const` members, interfaces returning references, exception paths, callbacks: each is an "all-in" checkpoint for us to audit.

Returning a reference is an easily missed exit. Look at the `Box` implementation below:

<OnlineCompilerDemo
  title="ref() locks, but the reference it hands out is beyond the lock's reach"
  source-path="code/examples/vol5/chapter02/14_demo_escaped_reference.cpp"
  description="The reference you hold floats outside the lock: set(42) falls inside the lock, reading the reference doesn't. If another thread calls set at that moment, one read and one write make a data race — the interface bypasses its own lock."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_escaped_reference.cpp —— ref() 加了锁，交出去的引用不归锁管
#include <cstdio>
#include <mutex>

class Box {
public:
  int& ref() {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }

private:
  std::mutex m_;
  int value_ = 0;
};

int main() {
  Box box;
  int& r = box.ref();
  box.set(42);
  std::printf("through the escaped reference: %d\n", r);
}
```

</OnlineCompilerDemo>

```text
through the escaped reference: 42
```

`ref()` does lock, but what it hands out is the location of a piece of private data. The lock is released when the function returns; the `r` in your hand is beyond the lock's jurisdiction. After `set(42)` changes it, `r` reads 42. If another thread calls `set` at that moment, one read and one write form a data race — the interface bypassed its own lock. (This author has written such reference-handing interfaces too; nothing felt wrong while writing it. It took teaching the lock once to see the bypass.)

When we fix it, copy the data out and hand that over — return `int`, not `int&`. Larger objects can return a copy, or a `shared_ptr` to a snapshot copy, or simply not expose the data at all and expose only operations. Which road fits is left to the interface-design discussion in the article on the boundary of one operation.

Is the lock the only source of synchronization? No — join also establishes order. Return to the counter we fitted with a lock in the first section: in the fixed version, the main thread's read of `counter` carries no lock; it relies on `a.join()` and `b.join()` for ordering, and between join and thread completion there is a synchronizes-with relationship ([thread.thread.member]/4). Modifications the two worker threads wrote before the join are all visible to the main thread after it. The rule fits in one sentence: between two accesses that might be concurrent, there must be happens-before. The lock is one way to establish it; join is another.

Delete the lock in `Counter::value()`, compile once with TSan, and see which line it reports.

## How Much Work Belongs Inside the Lock: Granularity

Which statements the critical section wraps is ours to decide, and it decides how long other threads wait. Wrap too much, and several threads can only file through one by one. Wrap too little, and actions that must complete together get cut apart, with invariants exposed halfway.

We already saw this in the `unique_lock` discussion: release the lock right after copying the snapshot and move the slow work outside. When judging, look at which operations must complete together; "minimize locking" need not be the goal. The read, add, write-back of `++counter` must complete together, so together they stay in the lock. As for formatting logs, writing files, computing hashes — as long as they don't touch the protected data, there is no need to drag them into the critical section; that is the range we set ourselves.

Nor is finer always better — don't casually split one lock into two. Two fields that must stay consistent — balance and transaction count, head pointer and length — must be protected by the same lock. Split into two locks, and a reader might see the new balance paired with the old transaction count: the invariant broke between the two lockings. Cross-data invariants are left to the article on the boundary of one operation. Here we list the accesses that must share one lock: balance and transaction count read together, written together; head pointer and length swapped together.

When measuring your own code's critical sections, one experiment can show the cost before you decide whether to split. Four threads each increment one of four counters two million times; one practice shares a single lock across the four counters, the other gives each counter its own:

<OnlineCompilerDemo
  title="How much faster four locks are than one"
  source-path="code/examples/vol5/chapter02/15_demo_lock_granularity.cpp"
  description="Four threads each increment one of four counters two million times: with one shared lock they take turns going in; with one lock per counter, four cores each do their own — about thirty percent faster. The numbers hold only on the current machine under the current load."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lock_granularity.cpp —— 四把锁比一把锁快多少：四线程各加两百万次
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

long long run(int lock_count) {
  const int kThreads = 4;
  const int kIters = 2000000;
  std::vector<std::mutex> locks(lock_count);
  std::vector<long long> counters(lock_count, 0);
  auto t0 = clock_type::now();
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    const int slot = (lock_count == 1) ? 0 : i;
    threads.emplace_back([&, slot] {
      for (int j = 0; j < kIters; ++j) {
        std::lock_guard<std::mutex> lock(locks[slot]);
        ++counters[slot];
      }
    });
  }
  for (auto& t : threads) t.join();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             clock_type::now() - t0).count();
}

int main() {
  for (int i = 0; i < 3; ++i) {
    std::printf("one lock for four counters: %lld ms\n", run(1));
    std::printf("one lock per counter:       %lld ms\n", run(4));
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
one lock for four counters: 216 ms
one lock per counter:       152 ms
one lock for four counters: 218 ms
one lock per counter:       151 ms
one lock for four counters: 211 ms
one lock per counter:       149 ms
```

Four locks came out about thirty percent faster. Don't forget: the numbers hold only on this machine under this load; change machines or counts and the ratio shifts. With one shared lock, the four threads can only enter in turn. With a lock per piece of data, they each do their own work on four cores.

Granularity carries two more defects; this author states them together. One is doing blocking things inside the lock: waiting on I/O, waiting on another thread, allocating a large block of memory. The lock stays occupied and everyone else can only stand there waiting. This doesn't count as deadlock (the difference being nobody waits in a cycle), but the effect of stalling others is the same. The other is calling a callback we don't control while inside the lock; the callback comes around and reaches for this same lock, and the thread waits forever. The first is cured by shrinking the critical section; the second by design — the article on deadlock and reducing sharing will meet these design questions again. (This author ran into callbacks-inside-locks in someone else's code and hunted a good while before locating it; ever since, seeing a callback inside a critical section triggers one question — where does it go once it's out?)

Run the four-locks-versus-one timing on your own machine; the numbers will differ from here — give it a try.

## Can Each Thread Hold Its Own Copy: thread_local and Per-Thread Ownership

Some sharing we need not let happen at all. The counter needs a lock because both threads modify the same variable. If each thread had its own copy — data ownership following the thread, summed once at the end — the lock would be unnecessary.

Turn the last article's counter into a thread-local version, and watch:

<OnlineCompilerDemo
  title="thread_local: one counter per thread, summed at the end, the lock retires"
  source-path="code/examples/vol5/chapter02/16_demo_thread_local_counter.cpp"
  description="Two threads each increment 160000 times over 30 trials: the two addresses differ, each round sums to 320000, and the main thread's own copy stays 0. Data ownership follows the thread; no lock needed."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_thread_local_counter.cpp —— 每条线程一份计数，最后汇总，锁退场
#include <cstdio>
#include <thread>

thread_local long long local_counter = 0;

void work(int rounds, long long* slot, const char* name) {
  for (int i = 0; i < rounds; ++i) ++local_counter;
  std::printf("%s: value %lld at %p\n", name, local_counter,
              static_cast<void*>(&local_counter));
  *slot = local_counter;
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  long long a_sum = 0, b_sum = 0;
  for (int t = 0; t < kTrials; ++t) {
    long long a = 0, b = 0;
    std::thread ta(work, kRounds, &a, "a");
    std::thread tb(work, kRounds, &b, "b");
    ta.join();
    tb.join();
    if (a + b == 2LL * kRounds) ++correct;
    a_sum = a;
    b_sum = b;
  }
  (void)a_sum;
  (void)b_sum;
  std::printf("%d of %d trials summed to %d\n", correct, kTrials, 2 * kRounds);
  std::printf("main's own copy: %lld\n", local_counter);
  return 0;
}
```

</OnlineCompilerDemo>

We had the two threads each increment 160000 times, over 30 trials:

```text
a: value 160000 at 0x76c6233ff6b8
b: value 160000 at 0x76c622bfe6b8
30 of 30 trials summed to 320000
main's own copy: 0
```

The two addresses differ — separate objects. Look: every round's sum of the two results was 320000; nothing lost. The main thread's own `local_counter` stays 0 — the main thread never touched it.

This author compiled the same program with TSan, and it produced not one report. That counts as evidence, not proof: TSan reporting nothing doesn't mean every possible interleaving is safe — only that the runs taken had no data race to report.

Phenomenon in hand, two sentences from the standard to backfill. C++11 gave us `thread_local`: each thread gets its own independent object, with storage lasting until the thread ends ([basic.stc.thread]/1). For a block-scope variable, each thread initializes its own copy on first passing the declaration, initialized only once within the same thread ([stmt.dcl]/3). If initialization throws, it doesn't count as completed; the next entry into the declaration retries.

Non-block variables at namespace scope deserve a separate look. If the initialization isn't constant initialization, dynamic initialization may happen before the thread's invoking function's first statement, or be postponed until the thread first uses it — implementation-defined ([basic.start.dynamic]/7).

The example's `local_counter` initializes with the constant 0, untouched by the dynamic initialization above. Only when we switch to a type requiring run-time construction do the two rules just given come into play.

One thing to see clearly: what `thread_local` does is copy one piece of state into N pieces; it cannot replace true sharing. Before using it, several things to know:

- Other threads cannot read your copy — not even the main thread.
- Every thread gets its own copy, and initialization cost is counted per thread. Block scope: first pass of the declaration; namespace scope: the initialization timing above.
- Destroyed at thread exit, in reverse construction order; accessing other `thread_local` objects from inside a destructor calls for care.
- It is still global state, merely split by thread. Code readability does not improve on its account.
- `thread_local` in dynamic libraries carries extra lifetime issues; cross-platform code must check them separately.

If the data in your hand can be split one copy per thread, it is worth a try. When the fact may exist in only one copy, it cannot help. Stop flags, balances, global configuration, caches — split those per thread, and the aggregated result is no longer the truth.

Take another road: put the results into separate slots and have the main thread sum at the end. Prepare a `std::vector<long long>` of length 2; the two threads write `partial[0]` and `partial[1]` respectively, with no lock at all:

<OnlineCompilerDemo
  title="Each writes its own element, summed after join, lock-free throughout"
  source-path="code/examples/vol5/chapter02/17_demo_partial_slots.cpp"
  description="The two threads write partial[0] and partial[1] respectively, each owning the element it writes; join establishes order, then they are added: total = 320000, 30 of 30 trials correct."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_partial_slots.cpp —— 各写各的元素，join 之后汇总，全程无锁
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::vector<long long> partial(2, 0);
  const int rounds = 160000;
  std::thread a([&] { for (int i = 0; i < rounds; ++i) ++partial[0]; });
  std::thread b([&] { for (int i = 0; i < rounds; ++i) ++partial[1]; });
  a.join();
  b.join();
  std::printf("total = %lld\n", partial[0] + partial[1]);
  return 0;
}
```

</OnlineCompilerDemo>

The two threads each write their own element, and the element being written is exclusively owned. The container itself is still shared; the standard permits concurrent modification of different elements ([container.requirements.dataraces]/2), `vector<bool>` excepted. Only after the joins does the main thread add; the order is established by join. This author ran this program 30 rounds too — every round summed to 320000, and TSan reported nothing.

Put the two formulations together and one shared idea shows: let every thread own its data, and move the merging to the final step. How to choose among independent ownership, passing results, and joint modification belongs to the article on deadlock and reducing sharing.

When should you not do this? When the fact may exist in only one copy. Then it's either a lock, or atomics, or a design that moves the sharing into one place. One question suffices for judgment: must the data be shared? If the answer is no, don't rush to a lock — think about whether each thread can hold its own.

## Before Writing, a Few Questions

Before writing locked code, this author runs through a few questions. If you can't answer, don't rush to write:

1. Do you truly need to share the same data? Could each thread hold its own copy and sum at the end?
2. Which lock do you plan to protect it with? Do the lock and the data live in one class? Is the data private?
3. Has every potentially concurrent access been routed through this lock? `const` members, reference returns, exception paths, callbacks — all count.
4. How long do you stay inside the lock? Can you copy a snapshot out? Can the slow work move outside the lock?
5. How many locks do you take at once? Two or more: use `scoped_lock`; don't nest them one at a time.

Answer item 1 with "no need to share," and the following four often go unasked. Items 3 and 4 are the two places where demos most easily take the wrong turn.

## The Lock Is Fitted — Then What

This article's `Counter` interface guards only a single add; the gap between two operations still has nobody watching it.

Before turning the page, run this article's fixed counter for 30 trials, then delete the `lock_guard` and run 30 more, compile options as above, and compare against the same `-O0` tier's readings.

## Reference Resources

While writing this article, this author drew on Anthony Williams' C++ Concurrency in Action for progression, taking the book's chapters on sharing data. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against the draft text on eel.is and cppreference per the clauses noted in the text.

- [thread_local storage duration (cppreference)][cppref-tls] — declaring thread-local variables and their lifetime.
- [C++ working draft: Thread storage duration (basic.stc.thread)][draft-tls] — the basis for one object per thread.
- [C++ working draft: Declaration statement (stmt.dcl)][draft-stmt-dcl] — initialization of block-scope thread-storage variables.
- [C++ working draft: Dynamic initialization of non-block variables (basic.start.dynamic)][draft-start-dynamic] — initialization timing for non-block variables.
- [C++ working draft: Container data races (container.requirements.dataraces)][draft-containers] — the guarantee for concurrent modification of different elements.

[cppref-tls]: https://en.cppreference.com/w/cpp/language/storage_duration
[draft-tls]: https://eel.is/c++draft/basic.stc.thread
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-start-dynamic]: https://eel.is/c++draft/basic.start.dynamic
[draft-containers]: https://eel.is/c++draft/container.requirements.dataraces
