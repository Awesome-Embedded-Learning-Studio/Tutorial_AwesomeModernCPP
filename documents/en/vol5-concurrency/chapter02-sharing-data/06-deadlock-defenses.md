---
title: "What If Nobody Lets Go: Reducing Sharing and Avoiding Deadlock"
description: "Start from two opposite-direction transfers wedging the process, see each of the four Coffman conditions as it looks inside a program, then unwind it with lock ordering, layering, and back-off that releases everything, and close Unit 2 with the three arrangements: own independently, pass results, modify together."
chapter: 5
order: 6
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
  source: documents/vol5-concurrency/chapter02-sharing-data/06-deadlock-defenses.md
  source_hash: 2cd263f5e7c92a68ebfae5a5fe71506d2005421e2106a346b44b66bcb7cd5a1a
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 20005
---
# What If Nobody Lets Go: Reducing Sharing and Avoiding Deadlock

The counter in How Do We Get the Lost Counts Back? was protected by one lock, because one add touches only one object. Moving money is not that kind of work: money goes from wallet A to wallet B, and both wallets must be in hand. Two threads each doing their own move, with lock-holding orders that happen to be exactly opposite — what happens? This author wrote it into a program.

This author ran the money-moving program six times; all six printed only two lines, then stopped moving.

<OnlineCompilerDemo
  title="AB-BA inversion: two opposite transfers wedge the process"
  source-path="code/examples/vol5/chapter02/28_demo_abba_deadlock.cpp"
  description="Two threads each print holds the first wallet and then stop together (online running has a timeout that wraps up for you; locally you will need Ctrl-C). fflush is there for redirection — otherwise the killed process's buffer takes both lines with it."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_abba_deadlock.cpp —— 方向相反的两次挪钱：各持一把，等对方松手
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  Wallet(int number, long long b) : id(number), balance(b) {}
  std::mutex m;
  int id;
  long long balance;
};

// 按 from 然后 to 的次序拿锁。方向反过来，锁的次序也跟着反过来。
void pay(Wallet& from, Wallet& to, long long amount, const char* name) {
  std::lock_guard<std::mutex> first(from.m);
  std::printf("%s: holds the first wallet, asking for the second\n", name);
  std::fflush(stdout);
  std::this_thread::sleep_for(100ms);  // 把两条线程都停在"手里一把"的位置
  std::lock_guard<std::mutex> second(to.m);
  from.balance -= amount;
  to.balance += amount;
}

int main() {
  Wallet a(1, 700);
  Wallet b(2, 300);
  std::thread t1(pay, std::ref(a), std::ref(b), 10LL, "t1");
  std::thread t2(pay, std::ref(b), std::ref(a), 10LL, "t2");
  t1.join();
  t2.join();
  std::printf("a = %lld, b = %lld\n", a.balance, b.balance);
}
```

</OnlineCompilerDemo>

Two wallets, each with a lock and a balance. One thread moves ten from A to B; the other moves ten from B to A. If you have read Where Is the Boundary of One Operation, you will recognize it: the wallets there had only lock and balance; this article adds an id and changes the balance to `long long`.

To get a definite reading, this author ran it under `timeout 3`. Three seconds later the process was killed, exit code 124:

```text
t1: holds the first wallet, asking for the second
t2: holds the first wallet, asking for the second
exit=124
```

The `fflush(stdout)` is for redirection. With output going to a file or a pipe, the `stdout` buffer dies with the killed process, and you would never read the two lines.

Both threads took their own lock, then each went for the lock in the other's hand. Nobody lets go, nobody moves on; the closing `a = ..., b = ...` never arrived, because the main thread was stuck on `t1.join()`.

This author stole a look at the process while it was stuck. All three threads sat in kernel waits; `/proc/<pid>/task/*/wchan` reported `futex_do_wait` (WSL2, kernel 6.18.33.2, per `uname -r`), and `ps` reported CPU usage of `0.0`. It was sleeping, not computing.

It has a name: the **AB-BA inversion** — two threads taking the same pair of locks in opposite orders.

Two threads of thought meet here: don't share if you can avoid it; when sharing is unavoidable, go by order.

Once deadlock forms it is stable, indifferent to probability. This program might pass on the first try — what if one thread finished its whole move before the other started? Run it more times: every stuck run looks exactly the same.

But as long as the two threads stand in the "each holding one" state, whichever goes for the second lock cannot have it, and it will not untie itself. The 100-millisecond sleep in the code exists for exactly this: it pushes both threads into the each-holding-one state, so that you needn't rely on luck every run.

## The Four Coffman Conditions: Miss One, and Deadlock Cannot Stand

In 1971, Coffman, Elphick, and Shoshani listed four conditions in System Deadlocks; what we later see in textbooks as the **Coffman conditions** is exactly that.

All four are necessary conditions: miss one, and deadlock cannot form. All four present still doesn't guarantee a hang every time — timing matters too, and that sleep is what arranges the timing for us.

### Mutual Exclusion: Only One Thread In at a Time

`std::mutex`'s exclusivity is the hardest of the four. One thread holds the lock; another calling `lock()` must stand still until the first releases ([thread.mutex.class]/1). In a debugger you can watch it stopped right at the `lock()` call, unable to move.

The evidence can be held in hand too: count the threads inside the lock — at most one at any moment. This author put a counter inside the lock, incrementing on entry and decrementing on exit, and recorded the maximum ever seen:

<OnlineCompilerDemo
  title="Evidence of mutual exclusion: at most one thread inside"
  source-path="code/examples/vol5/chapter02/29_demo_peak_inside.cpp"
  description="A counter inside the lock, incremented on entry and decremented on exit, records the maximum: four threads each enter once, peak inside = 1. Delete the lock_guard and rerun, and the peak climbs."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_peak_inside.cpp —— 互斥的证据：锁里至多一条线程
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m;
int inside = 0;
int peak = 0;

void visit() {
  std::lock_guard<std::mutex> lock(m);
  ++inside;
  if (inside > peak) peak = inside;
  std::this_thread::sleep_for(1ms);
  --inside;
}

int main() {
  std::thread ts[4];
  for (auto& t : ts) t = std::thread(visit);
  for (auto& t : ts) t.join();
  std::printf("peak inside = %d, left inside = %d\n", peak, inside);
  return 0;
}
```

</OnlineCompilerDemo>

This author had four threads each enter once; a single reading:

```text
peak inside = 1, left inside = 0
```

The 1 is evidence they queued. Delete the `lock_guard` and rerun, and the peak climbs — on this author's side, all three runs gave 4. Treat this reading as observation only: with the lock gone, the counter itself races, so this is not safe code; 4 is just what these three runs happened to tear into.

### Hold and Wait: Clutching One Lock While Waiting for Another

No need to look elsewhere for evidence: the t1 and t2 output lines are its face — both threads printed "holding the first, asking for the second," then stopped together. A thread with one lock in hand still waiting for another: that is the hold-and-wait you saw.

Even without the printing you can tell: attach a debugger and look at the thread stacks — both stopped inside `lock()`.

### No Preemption: Waiting Will Not Untie Itself

In `std::mutex`'s standard interface, the lock operations are exactly three — `lock()`, `try_lock()`, `unlock()` — and only the holder may call `unlock()`. The standard has no step for "releasing someone else's lock for them," and search the whole standard library as we might, we won't find one.

The criterion: waiting will not untie itself. The `timeout 3` reading is the ready-made observation — in three seconds not one new line of output, no lock released on its own, and finally the outside `timeout` killed it.

The standard adds one more sentence: a thread exiting (terminating) while holding a lock is undefined behavior ([thread.mutex.class]/5) — so letting the holder die and take the lock with it is no way out either.

### Circular Wait: Two Edges Can Close a Ring Too

The ring in front of us is the shortest — only two edges: t1 waits for the B in t2's hand, t2 waits for the A in t1's hand. Three threads each holding one, linked head to tail, make three edges — still a deadlock, just harder to see at a glance.

To find the ring, draw "who waits for whom" as a directed graph; a cycle in the graph is it.

With the four laid out, only two are ours to move: hold-and-wait and circular wait. Mutual exclusion and no-preemption are the lock's nature: sharing data cannot dodge mutual exclusion, and the standard offers no preemption interface.

## Lock Ordering: Make the Ring Impossible to Draw

Give the wallets numbers. The number is fixed at creation and never changes. Any move, whichever way the money flows, takes the locks in ascending id order:

<OnlineCompilerDemo
  title="Lock by id: 200000 opposing moves, not one stuck"
  source-path="code/examples/vol5/chapter02/30_demo_lock_order_by_id.cpp"
  description="Whichever way the money flows, locks are taken in ascending id order. The first half runs only the moves: sum steady at 1000, exit code 0. The second half adds an observer thread reading one lock at a time; the total it stitches together across a move can deviate from 1000 — reading a true instant requires holding both locks at once."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lock_order_by_id.cpp —— 按编号从小到大拿锁：环画不出来
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

struct Wallet {
  std::mutex m;
  int id;
  long long balance;
};

void move(Wallet& from, Wallet& to, long long amount) {
  Wallet& low  = from.id < to.id ? from : to;
  Wallet& high = from.id < to.id ? to : from;
  std::lock_guard<std::mutex> a(low.m);
  std::lock_guard<std::mutex> b(high.m);
  from.balance -= amount;
  to.balance += amount;
}

long long read_total(Wallet& a, Wallet& b) {
  long long va, vb;
  {
    std::lock_guard<std::mutex> ga(a.m);
    va = a.balance;
  }
  std::this_thread::sleep_for(50us);   // 放下 A 之后、拿起 B 之前：挪动照常发生
  {
    std::lock_guard<std::mutex> gb(b.m);
    vb = b.balance;
  }
  return va + vb;
}

int main() {
  // 第一回：只有两条对向挪动的线程，200000 次来回
  {
    Wallet a;
    Wallet b;
    a.id = 1; a.balance = 700;
    b.id = 2; b.balance = 300;
    std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
    std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
    t1.join();
    t2.join();
    std::printf("by id: a = %lld, b = %lld, sum = %lld\n",
                a.balance, b.balance, a.balance + b.balance);
  }
  // 第二回：另开一条观察线程，一把一把地读两个钱包
  {
    Wallet a;
    Wallet b;
    a.id = 1; a.balance = 700;
    b.id = 2; b.balance = 300;
    std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
    std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
    long long sampled = 0, bad = 0;
    int printed = 0;
    std::thread observer([&] {
      while (sampled < 50000) {
        long long t = read_total(a, b);
        ++sampled;
        if (t != 1000) {
          ++bad;
          if (printed < 3) {
            std::printf("sample %lld: total %lld\n", sampled, t);
            ++printed;
          }
        }
      }
    });
    t1.join();
    t2.join();
    observer.join();
    std::printf("sampled %lld totals, %lld were not 1000\n", sampled, bad);
    std::printf("after join: a = %lld, b = %lld, sum = %lld\n",
                a.balance, b.balance, a.balance + b.balance);
  }
  return 0;
}
```

</OnlineCompilerDemo>

We had the two threads each run 200000 iterations, one moving A to B, the other the reverse:

```text
by id: a = 700, b = 300, sum = 1000
exit=0
```

Not stuck; exit code 0. Look: after the run, the two sides sum to 1000.

And mid-run? This author opened another thread as an observer, having it read the two wallets one lock at a time: take A's lock, read A, release, then take B's lock and read B. Between the two reads, other threads may have finished several moves, so the total it reads matches no actual moment. Below are a few samples that were not 1000:

```text
sample 1: total 577
sample 2: total 1750
sample 3: total 1035
sampled 50000 totals, 90 were not 1000
after join: a = 700, b = 300, sum = 1000
```

Of fifty thousand samples, we saw ninety that were not 1000. Only by holding both locks at once does the read total correspond to a real instant. This says the same thing as the wallet example in Where Is the Boundary of One Operation: when an invariant crosses two locks, whoever reads it must hold both at once too.

The reasoning isn't complicated. Suppose every thread only takes locks in ascending id order. If a waiting ring truly existed, every edge on the ring would be "holding the smaller id, waiting for the larger": I hold 3 and wait for 7; he holds 7 and waits for 9. Walk around the ring and the ids must strictly increase all the way — returning to the start would demand "the starting id is greater than the starting id."

With the ring impossible to draw, the remaining three conditions cannot all gather, and deadlock falls apart. One premise must be made clear: the whole process shares one numbering, and nobody bypasses it.

Where do numbers come from? Assign one at object creation; a database primary key works; absolute file paths ordered lexicographically work; module hierarchy works. Which you choose doesn't matter — the one thing that cannot loosen is that the order must be consistent everywhere in the process: no module ordering by id while another orders by name.

### When There Is No Id, Order by Address

Dynamically created objects are awkward to number in advance. We can order by object address, provided we use `std::less`, not the built-in `<`:

<OnlineCompilerDemo
  title="When there is no id, order by address"
  source-path="code/examples/vol5/chapter02/31_demo_lock_order_by_address.cpp"
  description="Dynamically created objects ordered into a total order by std::less<Wallet*>; the same 200000 back-and-forth runs keep sum steady at 1000. Don't use the built-in <: for pointers to different objects, the standard says the result of ordering is unspecified — not usable as a total order."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

This author switched the same program to the address-based form; also 200000 round trips, same result:

```text
by address: a = 700, b = 300, sum = 1000
```

Why not the built-in `<`? When two pointers point to different objects, are not elements of the same array, and are not members of the same object, the standard calls the result of `p < q` "unspecified" ([expr.rel]/5). It may give you the same answer every time, or it may not — you cannot use it as a total order. `std::less` provides an implementation-defined strict total order that holds for all pointers ([comparisons.general]/2); that is what our ordering should use.

One more layer you can see yourself: restart the process or switch allocators, run it again, and the order may flip. That's fine — correctness only requires "within one run, all threads use the same order." Today A sorts before B, tomorrow the reverse — no matter, as long as both threads honor today's order today.

## Layering: Let the Code Check the Order for You

Numbering, however well documented in comments, will be missed by someone. You add a new piece of code, don't see the comment, write the order backwards — and the deadlock returns, exactly when two threads walk there at once.

Layering doesn't rely on comments. We let the locks remember their own levels: each lock carries a number, each thread remembers which level it currently sits at, `lock()` compares once on entry, and a backwards take throws `std::logic_error` — whoever wrote it backwards learns on the spot.

<OnlineCompilerDemo
  title="LevelMutex: each lock remembers its level"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="lock() calls check() on entry"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_level_mutex.cpp —— 分层：拿反了当场抛，不进锁里等
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

class LevelMutex {
 public:
  explicit LevelMutex(int level) : level_(level) {}

  void lock() {
    check();
    m_.lock();
    previous_ = current_level_;
    current_level_ = level_;
  }
  void unlock() {
    current_level_ = previous_;
    m_.unlock();
  }
  bool try_lock() {
    check();
    if (!m_.try_lock()) return false;
    previous_ = current_level_;
    current_level_ = level_;
    return true;
  }

 private:
  void check() const {
    if (level_ >= current_level_) {
      throw std::logic_error("lock level goes the wrong way");
    }
  }

  static thread_local int current_level_;
  std::mutex m_;
  int level_;
  int previous_ = 0;
};

thread_local int LevelMutex::current_level_ = INT_MAX;

LevelMutex config(300);   // 越靠上层，编号越大
LevelMutex cache(200);
LevelMutex device(100);

void read_device() {
  std::lock_guard<LevelMutex> g(device);
  std::printf("device: level 100\n");
}
void read_cache() {
  std::lock_guard<LevelMutex> g(cache);
  std::printf("cache: level 200, going down to device\n");
  read_device();
}
void read_all() {
  std::lock_guard<LevelMutex> g(config);
  std::printf("config: level 300, going down to cache\n");
  read_cache();
}
void read_upward() {
  std::lock_guard<LevelMutex> g(device);
  try {
    std::lock_guard<LevelMutex> up(cache);   // 层级不对，进来之前就抛
    std::printf("upward: got cache as well\n");
  } catch (const std::logic_error& e) {
    std::printf("upward: %s\n", e.what());
  }
}

int main() {
  std::thread down(read_all);
  std::thread up(read_upward);
  down.join();
  up.join();
  std::printf("both threads finished\n");
  return 0;
}
```

</OnlineCompilerDemo>

Look at `current_level_`: it is `thread_local` (since C++11), one copy per thread, no interference. When nobody holds a lock it is `INT_MAX`, so any level may be entered.

The check reads `level_ >= current_level_` — equality also counts as a violation. Two locks on the same level have no agreed order between them; either could come first, and the ring becomes drawable again. We adopt the convention "one lock per level," and `>=` enforces it in passing.

Let's try three levels: config at 300, cache at 200, device at 100. A normal call descends from the config level:

<OnlineCompilerDemo
  title="Layered mutexes: a backwards take throws on the spot, no waiting inside the lock"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="Config level 300, cache 200, device 100: descending from the top runs normally; the reverse direction is stopped by check() with a logic_error before entering the critical section. current_level_ is thread_local, and entering the same level a second time is stopped too. A teaching implementation — not a library."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_level_mutex.cpp —— 分层：拿反了当场抛，不进锁里等
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

class LevelMutex {
 public:
  explicit LevelMutex(int level) : level_(level) {}

  void lock() {
    check();
    m_.lock();
    previous_ = current_level_;
    current_level_ = level_;
  }
  void unlock() {
    current_level_ = previous_;
    m_.unlock();
  }
  bool try_lock() {
    check();
    if (!m_.try_lock()) return false;
    previous_ = current_level_;
    current_level_ = level_;
    return true;
  }

 private:
  void check() const {
    if (level_ >= current_level_) {
      throw std::logic_error("lock level goes the wrong way");
    }
  }

  static thread_local int current_level_;
  std::mutex m_;
  int level_;
  int previous_ = 0;
};

thread_local int LevelMutex::current_level_ = INT_MAX;

LevelMutex config(300);   // 越靠上层，编号越大
LevelMutex cache(200);
LevelMutex device(100);

void read_device() {
  std::lock_guard<LevelMutex> g(device);
  std::printf("device: level 100\n");
}
void read_cache() {
  std::lock_guard<LevelMutex> g(cache);
  std::printf("cache: level 200, going down to device\n");
  read_device();
}
void read_all() {
  std::lock_guard<LevelMutex> g(config);
  std::printf("config: level 300, going down to cache\n");
  read_cache();
}
void read_upward() {
  std::lock_guard<LevelMutex> g(device);
  try {
    std::lock_guard<LevelMutex> up(cache);   // 层级不对，进来之前就抛
    std::printf("upward: got cache as well\n");
  } catch (const std::logic_error& e) {
    std::printf("upward: %s\n", e.what());
  }
}

int main() {
  std::thread down(read_all);
  std::thread up(read_upward);
  down.join();
  up.join();
  std::printf("both threads finished\n");
  return 0;
}
```

</OnlineCompilerDemo>

<OnlineCompilerDemo
  title="Descending three levels vs. taking upward in reverse"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="Same program: read_all walks from 300 down to 100 with three lines in order. read_upward, holding the device level, reaches for the cache level — lock level goes the wrong way — and both threads end normally."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_level_mutex.cpp —— 分层：拿反了当场抛，不进锁里等
#include <climits>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>

class LevelMutex {
 public:
  explicit LevelMutex(int level) : level_(level) {}

  void lock() {
    check();
    m_.lock();
    previous_ = current_level_;
    current_level_ = level_;
  }
  void unlock() {
    current_level_ = previous_;
    m_.unlock();
  }
  bool try_lock() {
    check();
    if (!m_.try_lock()) return false;
    previous_ = current_level_;
    current_level_ = level_;
    return true;
  }

 private:
  void check() const {
    if (level_ >= current_level_) {
      throw std::logic_error("lock level goes the wrong way");
    }
  }

  static thread_local int current_level_;
  std::mutex m_;
  int level_;
  int previous_ = 0;
};

thread_local int LevelMutex::current_level_ = INT_MAX;

LevelMutex config(300);   // 越靠上层，编号越大
LevelMutex cache(200);
LevelMutex device(100);

void read_device() {
  std::lock_guard<LevelMutex> g(device);
  std::printf("device: level 100\n");
}
void read_cache() {
  std::lock_guard<LevelMutex> g(cache);
  std::printf("cache: level 200, going down to device\n");
  read_device();
}
void read_all() {
  std::lock_guard<LevelMutex> g(config);
  std::printf("config: level 300, going down to cache\n");
  read_cache();
}
void read_upward() {
  std::lock_guard<LevelMutex> g(device);
  try {
    std::lock_guard<LevelMutex> up(cache);   // 层级不对，进来之前就抛
    std::printf("upward: got cache as well\n");
  } catch (const std::logic_error& e) {
    std::printf("upward: %s\n", e.what());
  }
}

int main() {
  std::thread down(read_all);
  std::thread up(read_upward);
  down.join();
  up.join();
  std::printf("both threads finished\n");
  return 0;
}
```

</OnlineCompilerDemo>

This author opened one thread to run `read_all()`, and another to reach upward from the device level for the cache level:

```text
config: level 300, going down to cache
cache: level 200, going down to device
device: level 100
upward: lock level goes the wrong way
both threads finished
```

Both threads ended, exit code 0. The backwards call did not stand waiting inside `lock()`; `check()` stopped it before it entered the critical section. This author ran it three times; all had the config, cache, device lines first and upward last — a different schedule could swap them.

Three costs to accept. Every lock must be a `LevelMutex` — bare `std::mutex` can't join. Every locking pays one extra comparison, cheap in isolation but paid every single time. Level numbers must be unified project-wide; as with numbering, if some module invents its own set, the check is wasted.

This is a teaching implementation, not a library. Before bringing it into your project, the error handling and interface requirements must be re-verified against your own standards.

It also, in passing, catches one more thing for us. The same thread taking the same lock twice can also deadlock ([thread.mutex.class]/4) — that is not circular wait, and lock ordering can't help. Layering throws on the spot instead, because the same level may not be entered twice.

## Can't Take It? Release Everything: The Back-Off Road

Lock ordering governs the order of taking; it cannot govern "still wanting what can't be taken." We met a `std::lock` in How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks; it walks the other road.

Open the standard and the deadlock-related requirement is exactly one: the sequence of calls must not cause deadlock; how it orders the rest is unspecified ([thread.lock.algorithm]/5). The notes in the same section give a typical approach, try-and-back-off: take one, try the next, and on failure release everything already taken and retry in a different order.

So, to be precise, what breaks hold-and-wait is the back-off approach mainstream implementations chose for themselves. The standard only mandates no deadlock; the ordering is the implementation's choice, and the guarantee we hold in hand is only "no deadlock."

When C++17's `std::scoped_lock` receives two or more locks, its constructor calls exactly `std::lock` ([thread.lock.scoped]/2).

The essence of back-off: never block waiting for the next lock while holding one. On failure release everything; with empty hands, try again — and "hold and wait" loses its footing.

You can see `std::try_lock` as its non-blocking version: one round of trying, then back. It `try_lock`s its arguments in order; on any failure it `unlock`s everything taken so far and returns the zero-based index of the failed one, or -1 if all succeeded ([thread.lock.algorithm]/2, /3).

<OnlineCompilerDemo
  title="std::try_lock: on failure, everything taken is given back"
  source-path="code/examples/vol5/chapter02/33_demo_try_lock_backoff.cpp"
  description="holder takes m1 and sleeps 300 ms. main's try_lock(m2, m1) takes m2, fails m1, returns 1, and hands m2 back on the way — the immediately following m2.try_lock() proves it is free."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_try_lock_backoff.cpp —— std::try_lock：试不上就把拿到的全放回去
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m1, m2;

int main() {
  std::thread holder([&] {
    std::lock_guard<std::mutex> lock(m1);
    std::printf("holder: m1 taken, keeping it for 300 ms\n");
    std::fflush(stdout);
    std::this_thread::sleep_for(300ms);
  });

  std::this_thread::sleep_for(50ms);

  // 参数次序是 m2、m1。m2 本来是空的，所以它会拿到 m2，再去试 m1。
  int index = std::try_lock(m2, m1);
  std::printf("main: try_lock(m2, m1) returned %d\n", index);

  // 失败以后 m2 应该已经被放回去了，咱们试着拿一下看看。
  bool got_m2 = m2.try_lock();
  std::printf("main: m2 is %s\n", got_m2 ? "free" : "still held");
  if (got_m2) m2.unlock();

  holder.join();
  return 0;
}
```

</OnlineCompilerDemo>

```text
holder: m1 taken, keeping it for 300 ms
main: try_lock(m2, m1) returned 1
main: m2 is free
exit=0
```

The return value 1 says the second lock (`m1`) failed — no surprise there. What deserves our look is what follows: it had clearly been holding `m2`, and after the failure it handed it back — which is why `m2.try_lock()` succeeds.

When is back-off worth it? Think of occasions where failing to get a lock should send the thread off to do something else. Say a thread refreshes statistics: if the lock is occupied, it shouldn't wait dry — it can batch it for the next round. Or simply degrade: skip this round and write it into the log.

One more boundary, mentioned in How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks: don't hand locks you already hold to `std::lock` or `std::try_lock`. The algorithm arranges its own order and doesn't know your hand is already full; its back-offs and retries would have to orbit around your lock.

## Reference Resources

While writing this article, this author followed the progression of Anthony Williams' book, C++ Concurrency in Action; its chapters on sharing data come closest to our arrangement here. The names and definitions of the four Coffman conditions are listed as in the paper; the plain-language explanations and the four checking methods were added by this author for verifying them inside programs. The examples and prose are this author's own.

- [std::mutex (cppreference)][cppref-mutex] — since C++11; non-recursive, exclusive ownership.
- [std::lock (cppreference)][cppref-lock] — takes several locks at once, guaranteeing a deadlock-free call sequence.
- [std::try_lock (cppreference)][cppref-trylock] — the non-blocking multi-lock attempt that releases what it took on failure.
- [std::scoped_lock (cppreference)][cppref-scoped] — since C++17; forwards to `std::lock` for multiple locks.
- [std::less (cppreference)][cppref-less] — the pointer specializations give a consistent total order.
- [Class mutex (current draft thread.mutex.class)][draft-mutex-class] — non-recursive, blocking semantics, and the note that repeated locking by the same thread may deadlock.
- [Generic locking algorithms (current draft thread.lock.algorithm)][draft-lock-algorithm] — the requirements for `std::lock` and `std::try_lock`, and the try-and-back-off note.
- [Class template scoped_lock (current draft thread.lock.scoped)][draft-lock-scoped] — the constructor calls `lock(m...)` for multiple arguments.
- [Relational operators (current draft expr.rel)][draft-expr-rel] — ordering pointers to different objects gives an unspecified result.
- [Comparisons (current draft comparisons)][draft-comparisons] — `std::less` gives pointers an implementation-defined strict total order.
- [System Deadlocks (Coffman, Elphick, Shoshani, 1971)][coffman] — the origin of the four conditions.
- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — progression reference for the chapters on sharing data and deadlock.

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-lock]: https://en.cppreference.com/w/cpp/thread/lock
[cppref-trylock]: https://en.cppreference.com/w/cpp/thread/try_lock
[cppref-scoped]: https://en.cppreference.com/w/cpp/thread/scoped_lock
[cppref-less]: https://en.cppreference.com/w/cpp/utility/functional/less
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-lock-algorithm]: https://eel.is/c++draft/thread.lock.algorithm
[draft-lock-scoped]: https://eel.is/c++draft/thread.lock.scoped
[draft-expr-rel]: https://eel.is/c++draft/expr.rel
[draft-comparisons]: https://eel.is/c++draft/comparisons
[coffman]: https://dl.acm.org/doi/10.1145/356586.356588
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
