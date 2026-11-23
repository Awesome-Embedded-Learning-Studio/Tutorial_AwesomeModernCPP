---
title: "The Boundary Handed to the Caller: Four Ways Interfaces Leak"
description: "The lock is not bypassed, yet the caller holds internal state: references, const references, iterators, and callback parameters — one crime scene for each hand-off, and at the end the boundary lands on the operation."
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter02-sharing-data/05-interface-leaks.md
  source_hash: 7f739bdb3a7125f6893e4b089e4cea9ef9fe423c78122137d0f583cb8aef142d
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 15645
---
# The Boundary Handed to the Caller: Four Ways Interfaces Leak

The three situations arrive at the third spot, the most hidden one: the interface hands internal state out. The lock is not bypassed, yet the caller holds internal state. The `Box` in How Much Should a Lock Cover let us see one leaked reference, the example being `int&`. This article looks at four hand-offs: references, `const&`, iterators, and callback parameters. What they hand out are all locations or references, and once the caller holds them, everything happens outside the lock.

This author wrote another one, a `Gauge`: both `set()` and `value()` lock, with only `value()`'s return type written as `const int&`:

<OnlineCompilerDemo
  title="Broken Gauge: value() returns const int&"
  source-path="code/examples/vol5/chapter02/22_demo_gauge_const_ref.cpp"
  description="set and value both lock, but the lock is released when the function returns: the main thread reads the reference outside the lock while the writer writes inside it. The online component runs the detector-less version; the TSan report must be reproduced locally with the parameters given in the text (-O1 -fsanitize=thread)."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_const_ref.cpp —— const int& 交出去：锁放了，读还在锁外
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  const int& value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (with -fsanitize=thread this reports a data race)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

`value()` does lock, but the lock is released when the function returns. The reference passes into the caller's hands, and the read then happens outside the lock. We let the main thread read through the reference while another thread `set()`s nonstop:

<OnlineCompilerDemo
  title="The read-the-reference loop: another thread set()s nonstop"
  source-path="code/examples/vol5/chapter02/22_demo_gauge_const_ref.cpp"
  description="Same program: the writer does a hundred thousand sets while the main thread's while loop accumulates through the reference handed out by value(), outside the lock. In the TSan report the writing side holds a lock (M0); the reading side holds none."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_const_ref.cpp —— const int& 交出去：锁放了，读还在锁外
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  const int& value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (with -fsanitize=thread this reports a data race)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

Three places were compiled instead with `-O1 -fsanitize=thread` (ThreadSanitizer, TSan for short), and one more compiles its first half with the address detector and its second half with TSan. They can report races and memory errors invisible to ordinary runs; read on and you will meet the reports.

We compiled a version with TSan; one run's report:

```text
WARNING: ThreadSanitizer: data race (pid=37)
  Write of size 4 at 0x7fffffffd3d8 by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ... (gauge_lock+0x1372)
  Previous read of size 4 at 0x7fffffffd3d8 by main thread:
    #0 main ... (gauge_lock+0x1601)
  Location is stack of main thread.
```

Which line leads the report swaps between runs. We look at only two points: the writing side holds a lock; the reading side holds none. This author deleted a few lines of addresses and call stack. `const` constrains only the caller's side; it cannot govern another thread writing at the same moment. With no happens-before between the read and the write, by the definition in the article on races, this is a data race.

Change the return to by-value:

<OnlineCompilerDemo
  title="Fixed Gauge: return by value, the read completes inside the lock"
  source-path="code/examples/vol5/chapter02/23_demo_gauge_by_value.cpp"
  description="The return type changes from const int& to int: every read enters the critical section once, and TSan no longer reports. The price is copying large objects on every read."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_gauge_by_value.cpp —— 返回按值：每次读都在锁里完成
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

class Gauge {
 public:
  void set(int v) {
    std::lock_guard<std::mutex> lock(m_);
    value_ = v;
  }
  int value() const {
    std::lock_guard<std::mutex> lock(m_);
    return value_;
  }

 private:
  mutable std::mutex m_;
  int value_ = 0;
};

int main() {
  Gauge gauge;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) gauge.set(i);
    done.store(true);
  });

  long long sum = 0;
  while (!done.load()) sum += gauge.value();
  writer.join();
  std::printf("sum = %lld (TSan compiles clean, every read inside the lock)\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

This author switched back to by-value return and compiled again; TSan no longer reported. Every number the read loop obtains is the value at some complete instant; the price is entering the critical section on every read, and copying large objects besides.

Iterators hand out locations too. Look at a container with a `begin()`:

<OnlineCompilerDemo
  title="History: what begin() hands out is a location"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="add and begin both lock, but by the time the caller holds the iterator the lock was long released: single-threaded, adding one element can invalidate the iterator from begin(); multithreaded, reading it while another thread writes the same element is a data race. The online component runs the detector-less version; ASan and TSan reports must be reproduced locally with the parameters given in the text."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

Once the caller holds the iterator, the lock was released long before. Even a single-threaded run can step on it:

<OnlineCompilerDemo
  title="Single-threaded steps on a dangling iterator; multithreaded steps on a data race"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="First half, single-threaded: after begin(), add(3) triggers reallocation and it reads freed memory. Second half, multithreaded: another thread set(0, i)s nonstop while the main thread reads *rit outside the lock. Each detector reports its own."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

When `push_back` triggers reallocation, every reference, pointer, and iterator to the elements is invalidated ([vector.modifiers]/2, current draft). This author compiled once with AddressSanitizer (ASan), options `-O1 -fsanitize=address`:

```text
ERROR: AddressSanitizer: heap-use-after-free
READ of size 4 ... thread T0
    #0 ... main ...
```

This read is undefined behavior; ASan's report is merely an observation record, not a certificate of safety. Run bare without detectors and it mostly prints no error, just a number nobody can trace — exactly why it is hard to hunt.

The multithreaded side adds one more layer; read on. With no reallocation and nobody writing the same element, reading `*it` itself does not conflict. The moment another thread writes that same element, the out-of-lock read and the in-lock write share no happens-before, and TSan reports a data race:

<OnlineCompilerDemo
  title="Multithreaded: an out-of-lock read meets an in-lock write"
  source-path="code/examples/vol5/chapter02/24_demo_history_iterators.cpp"
  description="The second half of the same program: the writer modifies the first element holding set()'s lock, while the main thread reads through the iterator begin() handed out, outside the lock — TSan's report shows the writer with M0 and the reader empty-handed."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_iterators.cpp —— 迭代器交出去的是位置：单线程也踩得中
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  void set(std::size_t i, int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_[i] = v;
  }
  std::vector<int>::iterator begin() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.begin();
  }
  std::vector<int>::iterator end() {
    std::lock_guard<std::mutex> lock(m_);
    return items_.end();
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  history.add(1);
  history.add(2);
  auto it = history.begin();   // 锁在 begin() 返回时放了
  history.add(3);              // 容量不够，重新分配，迭代器失效
  int x = *it;                 // 读的是已经释放的内存（ASan 下报 heap-use-after-free）
  std::printf("dangling iterator read: %d\n", x);

  History raced;
  raced.add(10);
  raced.add(20);
  auto rit = raced.begin();    // 锁在 begin() 返回时放了
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (int i = 1; i <= 100000; ++i) raced.set(0, i);
    done.store(true);
  });
  long long sum = 0;
  while (!done.load()) sum += *rit;   // 锁外的读（TSan 下报 data race）
  writer.join();
  std::printf("out-of-lock reads summed to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

```text
WARNING: ThreadSanitizer: data race (pid=21)
  Write of size 4 ... by thread T1 (mutexes: write M0):
    #0 ... main::{lambda()#1} ...
  Previous read of size 4 ... by main thread:
    #0 main ...
```

Look at the report's two sides. The writer holds `set()`'s lock — the M0 in the report is it. The reader holds none. All the caller has is a location; he cannot know when another thread modifies it or pushes back once more. Comparing `it` against `end()` is equally unreliable. When `it` has dangled, you are comparing with a location that is no longer yours. When `it` is still valid and another thread is mid-`push_back`, `end()` — though fetched inside the lock — may have its fetched end position changed by the next `push_back` after the lock releases; the comparison again has no reliable footing.

If we must hand something over, hand over a copy. Copy the container out inside the lock; use it freely outside:

<OnlineCompilerDemo
  title="If handing over, hand over a copy"
  source-path="code/examples/vol5/chapter02/25_demo_history_snapshot.cpp"
  description="Copy the container out inside the lock, use it freely outside: adds after the snapshot don't affect the copy in hand. With many elements the copy isn't cheap — a price to accept."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_history_snapshot.cpp —— 锁里复制一份，锁外随便用
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  std::vector<int> snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    return items_;
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);
  std::vector<int> copy = history.snapshot();
  history.add(4);              // 快照之后容器再动，副本不受影响
  long long sum = 0;
  for (int v : copy) sum += v;
  std::printf("snapshot holds %zu items summing to %lld\n", copy.size(), sum);
  return 0;
}
```

</OnlineCompilerDemo>

On `History`'s interface, copies have a price, and with many elements the copy isn't cheap. Either let the caller take a copy and peruse it outside the lock, or we compute the needed result inside the lock and hand that over. For large objects, return a `shared_ptr` to a snapshot copy, or hand over no data at all — only operations.

Now callbacks — another form of the same exit. Call the caller-supplied function inside the lock, and it runs inside the lock, able to receive references to internal elements:

<OnlineCompilerDemo
  title="The callback runs inside the lock: store the reference and it leaks out"
  source-path="code/examples/vol5/chapter02/26_demo_callback_in_lock.cpp"
  description="for_each calls the caller's function inside the lock; the callback receives int& and can store the location into its own container. Once the lock releases, the locations remain: the following 100 adds trigger reallocation and every stored location dangles."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_callback_in_lock.cpp —— 回调在锁里跑：引用存下来就漏出去了
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  template <class F>
  void for_each(F f) {              // 回调在锁里跑
    std::lock_guard<std::mutex> lock(m_);
    for (auto& item : items_) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  std::vector<int*> kept;           // 回调把内部引用的位置存了下来
  history.for_each([&](int& item) { kept.push_back(&item); });

  for (int i = 0; i < 100; ++i) history.add(0);   // 重新分配后位置全失效
  std::printf("callback kept %zu positions, first one now reads %d\n",
              kept.size(), *kept.front());
  return 0;
}
```

</OnlineCompilerDemo>

What the callback receives is an `int&`, and it can store the reference into its own container. Once we release the lock, the reference stays in the callback's hands — when it uses it and for what, we can no longer govern. The fix is as before: take out the data to be handed over inside the lock, and pass the copy to the callback after leaving the critical section:

<OnlineCompilerDemo
  title="Take the copy inside the lock, feed the callback outside"
  source-path="code/examples/vol5/chapter02/27_demo_callback_outside.cpp"
  description="The copy is gathered inside the lock; the callback runs outside: the reference the callback receives points only into the copy, unaffected by whatever the container does afterwards. A callback wanting the reference to outlive the call must copy it itself."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_callback_outside.cpp —— 锁里取副本，锁外喂给回调
#include <cstdio>
#include <mutex>
#include <vector>

class History {
 public:
  void add(int v) {
    std::lock_guard<std::mutex> lock(m_);
    items_.push_back(v);
  }
  template <class F>
  void for_each(F f) const {
    std::vector<int> copy;
    {
      std::lock_guard<std::mutex> lock(m_);
      copy = items_;
    }
    for (const auto& item : copy) f(item);
  }

 private:
  mutable std::mutex m_;
  std::vector<int> items_;
};

int main() {
  History history;
  for (int v : {1, 2, 3}) history.add(v);

  long long sum = 0;
  history.for_each([&](const int& item) { sum += item; });   // 副本里的引用，随便用
  for (int i = 0; i < 100; ++i) history.add(0);              // 之后再动也不影响
  std::printf("callback summed the snapshot to %lld\n", sum);
  return 0;
}
```

</OnlineCompilerDemo>

We hand the copy to the callback, and the callback's signature changes along with it to a read-only value or read-only reference. The copy is valid for the duration of `for_each`'s execution. A callback wanting the reference to outlive the call must copy it itself — this layer of agreement belongs in the interface's documentation.

A callback that calls this class's member functions again inside the lock can deadlock ([thread.mutex.class]/4, since C++11) — mentioned back when How Much Should a Lock Cover discussed granularity. This author tried once on GCC 16.2.1: with the same thread locking the same `std::mutex` a second time, the process simply stuck, exiting with 124 after `timeout`'s two seconds. The callback has two restrictions: don't call it inside the lock, and don't let the references it receives outlive the call. Do both, and neither the deadlock nor the leaked reference can happen.

To judge whether an interface leaves a back door, the question is direct: after the caller receives the return value, does using it still require the lock? If yes, the class hasn't finished its protection. References, `const&`, iterators, and callback parameters — all must pass this question. `const` members likewise: read-only accesses also enter the critical section.

We follow the two rules from How Much Should a Lock Cover: no slow work inside the lock, and no uncontrolled external code called there.

## Draw the Boundary on the Operation

Having looked through the shelf, the wallets, and the gauge, when putting pen to paper this author runs through a few questions:

1. One operation: where do you count the start, and where the end? Does the caller see one step, or several? If the lock is released in between, what can other threads see? The shelf's answer: "check and deduct" counts as one step. The `Wallet`'s answer: "move from A to B" counts as one step. Over at the `Gauge`, the operation was one step all along — the problem was the location the interface handed out.
2. Which relation does one operation depend on? Count how many pieces of data the relation mentions, and whether they sit under the same lock. In the split-lock `Wallet`, the two balances have a lock each, and the invariant crossing them goes unattended. In the one-lock `Ledger`, one lock covers both.
3. What does the interface hand out? Run references, `const&`, iterators, and callback parameters through, and ask whether they let the caller keep acting outside the lock. The `Gauge` handed out a `const int&`, and the caller then never needed to enter the critical section.

## Next Steps

Before you leave this article, this author asks one thing: find a piece of code you wrote yourself that goes "query a bit, then modify a bit," and check whether the query interface handed the answer to the caller. If it can be merged into one call, merge it. If it can't, write down why not — that reason is usually the multi-lock problem the next article deals with.

## Reference Resources

While writing this article, this author drew on Anthony Williams' C++ Concurrency in Action (Manning, second edition); the treatment of concurrent interfaces references its chapters on sharing data. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against the draft text on eel.is and cppreference per the clauses noted in the text.

- [Class mutex (current draft thread.mutex.class)][draft-mutex-class] — non-reentrant; repeated locking by the same thread may deadlock (draft note).
- [vector modifiers (current draft vector.modifiers)][draft-vector-modifiers] — invalidation rules for iterators and references when `push_back` reallocates.
- [C++ Core Guidelines CP.50][cp50] — define a mutex together with the data it guards.

[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-vector-modifiers]: https://eel.is/c++draft/vector.modifiers
[cp50]: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#cp50-define-a-mutex-together-with-the-data-it-guards-use-synchronized_valuet-where-possible
