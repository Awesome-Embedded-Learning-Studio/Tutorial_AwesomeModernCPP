---
title: "Where Is the Boundary of One Operation"
description: "Put the lock on the boundary of one complete operation: see the window between check and act, invariants spanning two objects, and how references, iterators, and callbacks leak internal state to callers."
chapter: 5
order: 4
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
  source: documents/vol5-concurrency/chapter02-sharing-data/04-check-then-act.md
  source_hash: f7f3f63970c73d6bb34b81dd832bfc4a786f2486c91e3b14f64a826ec0650fef
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 15179
---
# Where Is the Boundary of One Operation

How Do We Get the Lost Counts Back? put `std::mutex` and the lock-on-construction, unlock-on-destruction `lock_guard` into our hands, and with the lock fitted, the counter stopped losing counts. But real operations rarely touch just one variable: placing an order queries the stock and then deducts it; a transfer moves two wallets at once. How large the lock must be in such places is this article's business.

One item left on the shelf, two threads arriving to buy at once, and the stock ends up recorded as -1. This is the shelf this author set up in the experiment: every step was locked, and the one item still sold to two buyers.

If locking every step still sells wrong, the defect is not in the lock itself but in which stretch the lock covers. We cannot measure a lock's boundary in lines of code: whatever result an operation promises its caller, the lock must cover every step and every piece of data the result passes through; anything beyond the promise need not be stuffed into the critical section. Mis-draw the boundary, and the cases fan out by object count: first inside one object, then between two objects, and finally the interface's exits. The first two get their crime scenes in this article; the interface's exits have the most going on, so they get an article of their own — the next one, The Boundary Handed to the Caller.

## The Check Passed — by Act Time, the Condition Has Changed

First spot: the check and the act split into two calls. The opening shelf is the ready-made scene; run the broken version first:

<OnlineCompilerDemo
  title="Broken shelf: check and act apart — one item sold twice"
  source-path="code/examples/vol5/chapter02/18_demo_shelf_check_then_act.cpp"
  description="has_one and take_one each lock, and the window between stays open: both buyers sees stock = 1, both takes one; all three rounds end stock left -1, sold 2."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_shelf_check_then_act.cpp —— 检查与动手分开：一件货卖给了两位买家
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Shelf {
  std::mutex m;
  int stock = 1;

  bool has_one() {
    std::lock_guard<std::mutex> lock(m);
    return stock > 0;
  }
  void take_one() {
    std::lock_guard<std::mutex> lock(m);
    --stock;
  }
};

Shelf shelf;

void buy(const char* who) {
  if (!shelf.has_one()) {
    std::printf("%s: sold out\n", who);
    return;
  }
  std::printf("%s: sees stock = 1\n", who);
  std::this_thread::sleep_for(50ms);   // 把检查与动手之间的窗口放大
  shelf.take_one();
  std::printf("%s: takes one\n", who);
}

int main() {
  const int kRounds = 3;
  int total_sold = 0;
  for (int r = 1; r <= kRounds; ++r) {
    shelf.stock = 1;
    std::thread a(buy, "buyer A");
    std::thread b(buy, "buyer B");
    a.join();
    b.join();
    int sold = 1 - shelf.stock;
    total_sold += sold;
    std::printf("round %d: stock left %d, sold %d\n", r, shelf.stock, sold);
  }
  std::printf("%d rounds, %d sold\n", kRounds, total_sold);
  return 0;
}
```

</OnlineCompilerDemo>

<OnlineCompilerDemo
  title="buy(): the stretch between two critical sections stands open"
  source-path="code/examples/vol5/chapter02/18_demo_shelf_check_then_act.cpp"
  description="Same program as above: the check goes through has_one(), the act through take_one(); the 50 milliseconds in between merely widen the window. In a real program the scheduler assembles the window — nobody has to actually sleep."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_shelf_check_then_act.cpp —— 检查与动手分开：一件货卖给了两位买家
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Shelf {
  std::mutex m;
  int stock = 1;

  bool has_one() {
    std::lock_guard<std::mutex> lock(m);
    return stock > 0;
  }
  void take_one() {
    std::lock_guard<std::mutex> lock(m);
    --stock;
  }
};

Shelf shelf;

void buy(const char* who) {
  if (!shelf.has_one()) {
    std::printf("%s: sold out\n", who);
    return;
  }
  std::printf("%s: sees stock = 1\n", who);
  std::this_thread::sleep_for(50ms);   // 把检查与动手之间的窗口放大
  shelf.take_one();
  std::printf("%s: takes one\n", who);
}

int main() {
  const int kRounds = 3;
  int total_sold = 0;
  for (int r = 1; r <= kRounds; ++r) {
    shelf.stock = 1;
    std::thread a(buy, "buyer A");
    std::thread b(buy, "buyer B");
    a.join();
    b.join();
    int sold = 1 - shelf.stock;
    total_sold += sold;
    std::printf("round %d: stock left %d, sold %d\n", r, shelf.stock, sold);
  }
  std::printf("%d rounds, %d sold\n", kRounds, total_sold);
  return 0;
}
```

</OnlineCompilerDemo>

The `sleep_for` was added by this author only to widen the window. In a real program, the window is assembled by the scheduler and the two threads' respective execution times; nobody actually needs to sleep 50 milliseconds. We ran three rounds; the results all matched: stock -1, sold 2. The print order is decided by the scheduler and could swap.

```text
buyer B: sees stock = 1
buyer A: sees stock = 1
buyer B: takes one
buyer A: takes one
round 1: stock left -1, sold 2
buyer A: sees stock = 1
buyer B: sees stock = 1
buyer A: takes one
buyer B: takes one
round 2: stock left -1, sold 2
buyer A: sees stock = 1
buyer B: sees stock = 1
buyer A: takes one
buyer B: takes one
round 3: stock left -1, sold 2
3 rounds, 6 sold
```

See: at check time both sides saw 1; both released the lock after checking, and in between nobody blocked anybody. `take_one()` each took the lock once, each decremented once, down to -1: one item sold to two buyers.

The lock itself isn't broken — the boundary was mis-drawn. `has_one()` and `take_one()` each take a lock, the accesses inside the critical sections are indeed mutually exclusive, and the problem sits between the two critical sections. We cut one operation into two critical sections, and the stretch in between stands open. A mutex guarantees only one thread is inside a critical section at a time; it does not guarantee that after you walk out, the state still looks the way you saw it.

Only one change is needed: let the lock cover the whole operation.

<OnlineCompilerDemo
  title="Fixed shelf: the check and the deduction enter one critical section"
  source-path="code/examples/vol5/chapter02/19_demo_shelf_try_take.cpp"
  description="One change only — try_take() completes the check and the deduction in one call: all three rounds have one buyer takes the last one, the other finds none left, stock left 0, sold 1."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_shelf_try_take.cpp —— 判断与扣减进同一个临界区：只卖出一件
#include <cstdio>
#include <mutex>
#include <thread>

struct Shelf {
  std::mutex m;
  int stock = 1;

  bool try_take() {
    std::lock_guard<std::mutex> lock(m);
    if (stock == 0) return false;
    --stock;
    return true;
  }
};

Shelf shelf;

void buy(const char* who) {
  if (shelf.try_take()) {
    std::printf("%s: takes the last one\n", who);
  } else {
    std::printf("%s: finds none left\n", who);
  }
}

int main() {
  const int kRounds = 3;
  int total_sold = 0;
  for (int r = 1; r <= kRounds; ++r) {
    shelf.stock = 1;
    std::thread a(buy, "buyer A");
    std::thread b(buy, "buyer B");
    a.join();
    b.join();
    int sold = 1 - shelf.stock;
    total_sold += sold;
    std::printf("round %d: stock left %d, sold %d\n", r, shelf.stock, sold);
  }
  std::printf("%d rounds, %d sold\n", kRounds, total_sold);
  return 0;
}
```

</OnlineCompilerDemo>

We replaced the `has_one()` and `take_one()` in `buy()` with a single `try_take()`, and the check and the deduction complete inside one critical section. This author ran three rounds; the results were always:

```text
buyer A: takes the last one
buyer B: finds none left
round 1: stock left 0, sold 1
buyer A: takes the last one
buyer B: finds none left
round 2: stock left 0, sold 1
buyer A: takes the last one
buyer B: finds none left
round 3: stock left 0, sold 1
3 rounds, 3 sold
```

We only count the result. Who wins the item may differ each round; what never changes is that only one person gets it.

The defect we watched on the shelf has a name: **check-then-act** — check first, act after — mentioned back in the article on races. Its shape goes beyond shelves; that article's stock example was the same class of problem: present at query time, bought away by someone else by deduction time. A glance at `size()` then `push_back`, a `find()` then an `erase()` — each pair is separated by one unlock and one lock, and other threads can walk in.

One more layer is easy to miss. If the interface exposes the check alone as `has_one()` and the caller uses it to decide the next step, then no matter where you cut the critical section, it is useless — the decision happens outside the lock. The answer was read inside the lock, but by the time you decide with it, the lock is already released and the state may have changed. `has_one()` may stay — for logs, for interface display, for "roughly is there any" reference. But wherever the caller would use it to change state, the interface should provide an operation that completes the check and the modification in one call — `try_take()`, for instance.

`std::atomic` doesn't solve this problem either — that article's atomic counter is the evidence: every read and write defined, and the sequence still loses. Atomics govern single actions; a compound operation needs its actions joined. The "joining" we need comes from a lock, or from a single atomic read-modify-write such as `fetch_add`. The latter waits for the articles on atomics and the memory model.

## One Invariant Spans Two Objects

Second spot: one relation straddling two objects. This author uses two wallets as the example (the `Wallet`, split-lock version): A holds 700, B holds 300, together 1000. The operation moves 10 from A to B. The requirement is plain: at any moment the two must sum to 1000. We call such a relation an **invariant**, and it must hold even halfway through an operation.

<OnlineCompilerDemo
  title="Split-lock wallets: one lock per wallet, the sum invariant unattended"
  source-path="code/examples/vol5/chapter02/20_demo_wallet_split_locks.cpp"
  description="Two steps each lock with a release in between: the observer thread reads 990 (A minus 10, B not yet plus) on nearly half its samples. Each run moves 40 times, two runs, and the final line still sums to 1000."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_wallet_split_locks.cpp —— 每个钱包一把锁：和不变量跨了两把锁
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  long balance;
};

void move(Wallet& from, Wallet& to, long amount) {
  {
    std::lock_guard<std::mutex> lock(from.m);
    from.balance -= amount;
  }
  std::this_thread::sleep_for(1ms);   // 把两步之间的窗口放大
  {
    std::lock_guard<std::mutex> lock(to.m);
    to.balance += amount;
  }
  std::this_thread::sleep_for(1ms);   // 让完整状态也占一段窗口
}

long total(Wallet& a, Wallet& b) {
  long sum = 0;
  {
    std::lock_guard<std::mutex> lock(a.m);
    sum += a.balance;
  }
  {
    std::lock_guard<std::mutex> lock(b.m);
    sum += b.balance;
  }
  return sum;
}

void run_round(int round) {
  Wallet a{};
  Wallet b{};
  a.balance = 700;
  b.balance = 300;
  std::atomic<bool> moving{true};
  long long bad = 0, samples = 0;
  int printed = 0;

  std::thread mover([&] {
    for (int i = 0; i < 40; ++i) move(a, b, 10);
    moving.store(false);
  });
  std::thread observer([&] {
    while (moving.load()) {
      long t = total(a, b);
      ++samples;
      if (t != 1000) {
        ++bad;
        if (printed < 4) {
          std::printf("round %d sample %lld: total %ld\n", round, samples, t);
          ++printed;
        }
      }
    }
  });
  mover.join();
  observer.join();
  std::printf("round %d: per-object locks: %lld of %lld samples saw a total other than 1000\n",
              round, bad, samples);
  std::printf("round %d final: A = %ld, B = %ld, total = %ld\n",
              round, a.balance, b.balance, a.balance + b.balance);
}

int main() {
  for (int r = 1; r <= 2; ++r) run_round(r);
  return 0;
}
```

</OnlineCompilerDemo>

Every step locked; no wallet's balance was ever touched by two threads at once. But the move itself has two steps, the lock is released between them, and other threads can see "A down 10, B not yet up." This author added an observer thread that reads both sides by the same method:

<OnlineCompilerDemo
  title="The observer thread's total(): reading both sides apart, the same way"
  source-path="code/examples/vol5/chapter02/20_demo_wallet_split_locks.cpp"
  description="Same program: total() locks a then b and reads them apart; when move() pauses between its two steps it happens to read 990. The bad-sample count is its tally."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_wallet_split_locks.cpp —— 每个钱包一把锁：和不变量跨了两把锁
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  long balance;
};

void move(Wallet& from, Wallet& to, long amount) {
  {
    std::lock_guard<std::mutex> lock(from.m);
    from.balance -= amount;
  }
  std::this_thread::sleep_for(1ms);   // 把两步之间的窗口放大
  {
    std::lock_guard<std::mutex> lock(to.m);
    to.balance += amount;
  }
  std::this_thread::sleep_for(1ms);   // 让完整状态也占一段窗口
}

long total(Wallet& a, Wallet& b) {
  long sum = 0;
  {
    std::lock_guard<std::mutex> lock(a.m);
    sum += a.balance;
  }
  {
    std::lock_guard<std::mutex> lock(b.m);
    sum += b.balance;
  }
  return sum;
}

void run_round(int round) {
  Wallet a{};
  Wallet b{};
  a.balance = 700;
  b.balance = 300;
  std::atomic<bool> moving{true};
  long long bad = 0, samples = 0;
  int printed = 0;

  std::thread mover([&] {
    for (int i = 0; i < 40; ++i) move(a, b, 10);
    moving.store(false);
  });
  std::thread observer([&] {
    while (moving.load()) {
      long t = total(a, b);
      ++samples;
      if (t != 1000) {
        ++bad;
        if (printed < 4) {
          std::printf("round %d sample %lld: total %ld\n", round, samples, t);
          ++printed;
        }
      }
    }
  });
  mover.join();
  observer.join();
  std::printf("round %d: per-object locks: %lld of %lld samples saw a total other than 1000\n",
              round, bad, samples);
  std::printf("round %d final: A = %ld, B = %ld, total = %ld\n",
              round, a.balance, b.balance, a.balance + b.balance);
}

int main() {
  for (int r = 1; r <= 2; ++r) run_round(r);
  return 0;
}
```

</OnlineCompilerDemo>

While the observer reads both sides, `move()` may be paused exactly between its two steps. This author made `move()` sleep 1 millisecond between steps and another 1 after each move, so both states occupy a stretch of time. Each run moves 40 times; two runs were taken. The observer samples from the start of moving until it ends — moving runs roughly on the order of a hundred milliseconds (40 moves times two 1-millisecond sleeps each; not separately timed), and the observer stops when the moving ends. Below are the first four bad samples of the first run and both runs' summaries:

```text
round 1 sample 623: total 990
round 1 sample 624: total 990
round 1 sample 625: total 990
round 1 sample 626: total 990
round 1: per-object locks: 1277732 of 2580701 samples saw a total other than 1000
round 1 final: A = 300, B = 700, total = 1000
round 2: per-object locks: 1221448 of 2470174 samples saw a total other than 1000
round 2 final: A = 300, B = 700, total = 1000
```

990 is the instant when A has already lost 10 and B hasn't gained it yet. It pauses only 1 millisecond each time, the observer samples densely, and in both runs nearly half the samples landed inside. You might think the observer thread too idle, but in reality plenty of parties read the two-sided total: reports, quota checks, moves in the other direction — all may read in between.

The fix gathers into one place. The invariant speaks of the sum of A and B, so let one lock cover A and B together. Switch to the `Ledger` (the one-lock version):

<OnlineCompilerDemo
  title="One-lock ledger: one lock covers both sides, zero reads of 990"
  source-path="code/examples/vol5/chapter02/21_demo_ledger_one_lock.cpp"
  description="Ledger's transfer and total share one lock; the two modifications in transfer sit in the same critical section. The same sampling window, two more runs: zero samples deviate from 1000."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_ledger_one_lock.cpp —— 一把锁盖住两边：不变量不再露缝
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Ledger {
  std::mutex m;
  long a = 700;
  long b = 300;

  void transfer(long amount) {
    std::lock_guard<std::mutex> lock(m);
    a -= amount;
    b += amount;
  }
  long total() {
    std::lock_guard<std::mutex> lock(m);
    return a + b;
  }
};

void run_round(int round) {
  Ledger ledger;
  std::atomic<bool> moving{true};
  long long bad = 0, samples = 0;

  std::thread mover([&] {
    for (int i = 0; i < 40; ++i) {
      ledger.transfer(10);
      std::this_thread::sleep_for(1ms);   // 和 Wallet 版的节奏对齐
    }
    moving.store(false);
  });
  std::thread observer([&] {
    while (moving.load()) {
      long t = ledger.total();
      ++samples;
      if (t != 1000) ++bad;
    }
  });
  mover.join();
  observer.join();
  std::printf("round %d: one lock for both sides: %lld of %lld samples saw a total other than 1000\n",
              round, bad, samples);
  std::printf("round %d final: A = %ld, B = %ld, total = %ld\n",
              round, ledger.a, ledger.b, ledger.a + ledger.b);
}

int main() {
  for (int r = 1; r <= 2; ++r) run_round(r);
  return 0;
}
```

</OnlineCompilerDemo>

The two modifications in `transfer()` sit in the same critical section, and `total()` uses the same lock. We reran two rounds under the same sampling window. The caller in the experiment sleeps 1 millisecond after each `transfer()`, aligning the rhythm with the Wallet version — zero reads of 990:

```text
round 1: one lock for both sides: 0 of 1524458 samples saw a total other than 1000
round 1 final: A = 300, B = 700, total = 1000
round 2: one lock for both sides: 0 of 1511547 samples saw a total other than 1000
round 2 final: A = 300, B = 700, total = 1000
```

When a relation touches two pieces of data and you lock only one, the other stands outside the protection. The observer must use the same lock too, or it will still read the two sides apart. When we write interfaces we often lock per object — one lock per wallet. The invariant crosses two locks, and then no lock at all is minding it.

One lock covering all wallets has its price: every move queues up, even when the two sides are entirely unrelated. To lock per wallet, one move must hold both locks at once, with a fixed acquisition order so the two sides never wait on each other. The C++17 `std::scoped_lock` covered in How Do We Get the Lost Counts Back? accepts several locks at once, handing the how to the standard library's deadlock-avoidance algorithm. Lock ordering and deadlock prevention belong to What If Nobody Lets Go; this article only makes the boundary clear: an invariant needs both locks present together — missing either one, it does not hold.

> The `Shelf`, `Wallet`, and `Ledger` in our experiments all leave their fields exposed. When writing real interfaces, the fields and the lock go into private together — exactly what the `Counter` in How Do We Get the Lost Counts Back? does. Where else things leak even after encapsulation, the interface-focused part below continues.

## Reference Resources

While writing this article, this author drew on Anthony Williams' C++ Concurrency in Action (Manning, second edition); the treatment of check-then-act references its chapters on sharing data. The examples and prose are this author's own; the experiments were compiled and run on GCC 16.2.1. Claims about standard behavior were checked against the draft text on eel.is and cppreference per the clauses noted in the text.

- [std::mutex (cppreference)][cppref-mutex] — the mutex interface and its non-reentrancy, since C++11.
- [std::lock_guard (cppreference)][cppref-lockguard] — locks on construction, unlocks on destruction, since C++11.
- [Mutex requirements (current draft thread.mutex.requirements.mutex)][draft-mutex-req] — preconditions of `lock()` and `unlock()` and their synchronization relationships.
- [Class mutex (current draft thread.mutex.class)][draft-mutex-class] — non-reentrant; repeated locking by the same thread may deadlock (draft note).
- [Data races (current draft intro.races)][draft-races] — the definition of a data race and its undefined behavior.
- [Multi-threaded executions and data races (cppreference)][cppref-multithread] — happens-before and synchronizing operations.
- [ThreadSanitizer (Google Sanitizers wiki)][tsan-wiki] — report format and detection scope.

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-lockguard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[draft-mutex-req]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-races]: https://eel.is/c++draft/intro.races
[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
