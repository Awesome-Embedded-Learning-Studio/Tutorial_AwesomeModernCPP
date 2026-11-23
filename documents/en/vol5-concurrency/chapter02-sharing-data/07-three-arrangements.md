---
title: "Don't Share If You Don't Have To: Choosing Among Three Arrangements"
description: "The same sum-of-squares job written three times: own independently, pass results, and modify together, set side by side, with the criteria for choosing."
chapter: 5
order: 7
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
  source: documents/vol5-concurrency/chapter02-sharing-data/07-three-arrangements.md
  source_hash: 1329b70c30ea747ddebe2fb595bddad568337a7e24612990f7721029fa19c443
  translated_at: '2026-11-22T13:28:59+00:00'
  engine: glm-5.3
  token_count: 19310
---
# Don't Share If You Don't Have To: Choosing Among Three Arrangements

The deadlock defenses all live in the previous article: lock ordering, layering, back-off — all guarding scenes where sharing is unavoidable. There is one cheaper road: touch the same data less, starting at the root.

## Choosing Among Three Arrangements: Own Independently, Pass Results, Modify Together

The three designs are three arrangements of "who modifies the data" — a separate matter from which synchronization facilities you use. This author wrote the same job three times; compare them side by side.

The job is simple: 30 million integers, compute the sum of squares, with four threads splitting the work. The data is filled front to back as `data[i] = i % 1000`, so the values fall between 0 and 999.

<OnlineCompilerDemo
  title="Four ways on one stage: own independently, pass results, modify together, lock-per-number"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="Sum of squares over 30 million integers split across four threads: A writes per-thread slots, B hands results over via promise/future, C takes the lock once per 1024-number block, D once per number. Three rounds of timings plus lock counts — A/B/C even, D two orders of magnitude behind, because it locked 30 million times."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_three_designs.cpp —— 同一件活三种写法：独立持有、传递结果、共同修改
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}

long long design_a(const std::vector<int>& data) {
  std::vector<long long> parts(kThreads, 0);
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    long long* slot = &parts[t];           // 每条线程只碰自己的槽位
    threads.emplace_back([&data, slot, t, step] {
      *slot = sum_range(data, t * step, (t + 1) * step);
    });
  }
  for (auto& th : threads) th.join();
  long long total = 0;
  for (long long v : parts) total += v;
  return total;
}

long long design_b(const std::vector<int>& data) {
  std::vector<std::promise<long long>> boxes(kThreads);
  std::vector<std::future<long long>> futs;
  for (auto& box : boxes) futs.push_back(box.get_future());
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&data, &boxes, t, step] {
      boxes[t].set_value(sum_range(data, t * step, (t + 1) * step));
    });
  }
  long long total = 0;
  for (auto& f : futs) total += f.get();   // 算完一条，收一条
  for (auto& th : threads) th.join();
  return total;
}

long long locked_times = 0;

long long design_c(const std::vector<int>& data, int block) {
  std::mutex m;
  long long total = 0;
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      int lo = t * step;
      int hi = (t + 1) * step;
      for (int begin = lo; begin < hi; begin += block) {
        int end = begin + block < hi ? begin + block : hi;
        long long part = sum_range(data, begin, end);
        std::lock_guard<std::mutex> lock(m);   // 锁里只有一次加法
        ++locked_times;
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}

int main() {
  const int kN = 30000000;
  std::vector<int> data(kN);
  for (int i = 0; i < kN; ++i) data[i] = i % 1000;

  long long total = 0;
  for (int r = 1; r <= 3; ++r) {
    auto ta = clock_type::now();
    long long va = design_a(data);
    long long da = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - ta).count();

    auto tb = clock_type::now();
    long long vb = design_b(data);
    long long db = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tb).count();

    locked_times = 0;
    auto tc = clock_type::now();
    long long vc = design_c(data, 1024);
    long long dc = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tc).count();
    long long c_locks = locked_times;

    locked_times = 0;
    auto td = clock_type::now();
    long long vd = design_c(data, 1);
    long long dd = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - td).count();
    long long d_locks = locked_times;

    total = va;
    if (vb != va || vc != va || vd != va) {   // 四种写法算的得是同一个数
      std::printf("mismatch: %lld %lld %lld %lld\n", va, vb, vc, vd);
      return 1;
    }
    std::printf("round %d: A %lld ms, B %lld ms, C %lld ms, D %lld ms\n",
                r, da, db, dc, dd);
    if (r == 3) {
      std::printf("C locked %lld times, D locked %lld times\n", c_locks, d_locks);
      std::printf("total = %lld\n", total);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

The own-independently form — watch the main thread's moves: prepare one slot per thread, each thread writes only its allotted slot, and merge after the run.

<OnlineCompilerDemo
  title="design_a: each thread writes only its own slot"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="design_a from the same program: the main thread prepares one parts[t] slot per thread, each thread touches only its own, and the main thread merges after join. No shared writes during the run, not a single lock."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_three_designs.cpp —— 同一件活三种写法：独立持有、传递结果、共同修改
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}

long long design_a(const std::vector<int>& data) {
  std::vector<long long> parts(kThreads, 0);
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    long long* slot = &parts[t];           // 每条线程只碰自己的槽位
    threads.emplace_back([&data, slot, t, step] {
      *slot = sum_range(data, t * step, (t + 1) * step);
    });
  }
  for (auto& th : threads) th.join();
  long long total = 0;
  for (long long v : parts) total += v;
  return total;
}

long long design_b(const std::vector<int>& data) {
  std::vector<std::promise<long long>> boxes(kThreads);
  std::vector<std::future<long long>> futs;
  for (auto& box : boxes) futs.push_back(box.get_future());
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&data, &boxes, t, step] {
      boxes[t].set_value(sum_range(data, t * step, (t + 1) * step));
    });
  }
  long long total = 0;
  for (auto& f : futs) total += f.get();   // 算完一条，收一条
  for (auto& th : threads) th.join();
  return total;
}

long long locked_times = 0;

long long design_c(const std::vector<int>& data, int block) {
  std::mutex m;
  long long total = 0;
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      int lo = t * step;
      int hi = (t + 1) * step;
      for (int begin = lo; begin < hi; begin += block) {
        int end = begin + block < hi ? begin + block : hi;
        long long part = sum_range(data, begin, end);
        std::lock_guard<std::mutex> lock(m);   // 锁里只有一次加法
        ++locked_times;
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}

int main() {
  const int kN = 30000000;
  std::vector<int> data(kN);
  for (int i = 0; i < kN; ++i) data[i] = i % 1000;

  long long total = 0;
  for (int r = 1; r <= 3; ++r) {
    auto ta = clock_type::now();
    long long va = design_a(data);
    long long da = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - ta).count();

    auto tb = clock_type::now();
    long long vb = design_b(data);
    long long db = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tb).count();

    locked_times = 0;
    auto tc = clock_type::now();
    long long vc = design_c(data, 1024);
    long long dc = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tc).count();
    long long c_locks = locked_times;

    locked_times = 0;
    auto td = clock_type::now();
    long long vd = design_c(data, 1);
    long long dd = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - td).count();
    long long d_locks = locked_times;

    total = va;
    if (vb != va || vc != va || vd != va) {   // 四种写法算的得是同一个数
      std::printf("mismatch: %lld %lld %lld %lld\n", va, vb, vc, vd);
      return 1;
    }
    std::printf("round %d: A %lld ms, B %lld ms, C %lld ms, D %lld ms\n",
                r, da, db, dc, dd);
    if (r == 3) {
      std::printf("C locked %lld times, D locked %lld times\n", c_locks, d_locks);
      std::printf("total = %lld\n", total);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

Switch to passing results: each thread, having computed, hands its final value out and keeps nothing modifiable for itself. Here this author borrows `std::promise` and `std::future` — Unit 4.3, Passing Results Yourself: `std::promise` and `std::future`, covers them; this article uses them only for one hand-off.

<OnlineCompilerDemo
  title="design_b: compute, then hand the final value out"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="design_b from the same program: each thread set_values its final sum into its own promise, and the main thread collects one by one with f.get() — computed one, collected one. The thread itself keeps nothing modifiable."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_three_designs.cpp —— 同一件活三种写法：独立持有、传递结果、共同修改
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}

long long design_a(const std::vector<int>& data) {
  std::vector<long long> parts(kThreads, 0);
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    long long* slot = &parts[t];           // 每条线程只碰自己的槽位
    threads.emplace_back([&data, slot, t, step] {
      *slot = sum_range(data, t * step, (t + 1) * step);
    });
  }
  for (auto& th : threads) th.join();
  long long total = 0;
  for (long long v : parts) total += v;
  return total;
}

long long design_b(const std::vector<int>& data) {
  std::vector<std::promise<long long>> boxes(kThreads);
  std::vector<std::future<long long>> futs;
  for (auto& box : boxes) futs.push_back(box.get_future());
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&data, &boxes, t, step] {
      boxes[t].set_value(sum_range(data, t * step, (t + 1) * step));
    });
  }
  long long total = 0;
  for (auto& f : futs) total += f.get();   // 算完一条，收一条
  for (auto& th : threads) th.join();
  return total;
}

long long locked_times = 0;

long long design_c(const std::vector<int>& data, int block) {
  std::mutex m;
  long long total = 0;
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      int lo = t * step;
      int hi = (t + 1) * step;
      for (int begin = lo; begin < hi; begin += block) {
        int end = begin + block < hi ? begin + block : hi;
        long long part = sum_range(data, begin, end);
        std::lock_guard<std::mutex> lock(m);   // 锁里只有一次加法
        ++locked_times;
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}

int main() {
  const int kN = 30000000;
  std::vector<int> data(kN);
  for (int i = 0; i < kN; ++i) data[i] = i % 1000;

  long long total = 0;
  for (int r = 1; r <= 3; ++r) {
    auto ta = clock_type::now();
    long long va = design_a(data);
    long long da = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - ta).count();

    auto tb = clock_type::now();
    long long vb = design_b(data);
    long long db = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tb).count();

    locked_times = 0;
    auto tc = clock_type::now();
    long long vc = design_c(data, 1024);
    long long dc = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tc).count();
    long long c_locks = locked_times;

    locked_times = 0;
    auto td = clock_type::now();
    long long vd = design_c(data, 1);
    long long dd = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - td).count();
    long long d_locks = locked_times;

    total = va;
    if (vb != va || vc != va || vd != va) {   // 四种写法算的得是同一个数
      std::printf("mismatch: %lld %lld %lld %lld\n", va, vb, vc, vd);
      return 1;
    }
    std::printf("round %d: A %lld ms, B %lld ms, C %lld ms, D %lld ms\n",
                r, da, db, dc, dd);
    if (r == 3) {
      std::printf("C locked %lld times, D locked %lld times\n", c_locks, d_locks);
      std::printf("total = %lld\n", total);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

Modify-together is another road: one total only, all four threads modify it, protected by a lock. Watch: the lock holds only one addition; all the arithmetic stays outside.

<OnlineCompilerDemo
  title="design_c: only one addition inside the lock"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="design_c from the same program: one total, modified by all four threads. Each 1024-number block enters the lock only after being computed, and the lock holds only the total += part line. Setting block to 1 gives the control group D."
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_three_designs.cpp —— 同一件活三种写法：独立持有、传递结果、共同修改
#include <chrono>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

using clock_type = std::chrono::steady_clock;

constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}

long long design_a(const std::vector<int>& data) {
  std::vector<long long> parts(kThreads, 0);
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    long long* slot = &parts[t];           // 每条线程只碰自己的槽位
    threads.emplace_back([&data, slot, t, step] {
      *slot = sum_range(data, t * step, (t + 1) * step);
    });
  }
  for (auto& th : threads) th.join();
  long long total = 0;
  for (long long v : parts) total += v;
  return total;
}

long long design_b(const std::vector<int>& data) {
  std::vector<std::promise<long long>> boxes(kThreads);
  std::vector<std::future<long long>> futs;
  for (auto& box : boxes) futs.push_back(box.get_future());
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&data, &boxes, t, step] {
      boxes[t].set_value(sum_range(data, t * step, (t + 1) * step));
    });
  }
  long long total = 0;
  for (auto& f : futs) total += f.get();   // 算完一条，收一条
  for (auto& th : threads) th.join();
  return total;
}

long long locked_times = 0;

long long design_c(const std::vector<int>& data, int block) {
  std::mutex m;
  long long total = 0;
  std::vector<std::thread> threads;
  int step = static_cast<int>(data.size()) / kThreads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      int lo = t * step;
      int hi = (t + 1) * step;
      for (int begin = lo; begin < hi; begin += block) {
        int end = begin + block < hi ? begin + block : hi;
        long long part = sum_range(data, begin, end);
        std::lock_guard<std::mutex> lock(m);   // 锁里只有一次加法
        ++locked_times;
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}

int main() {
  const int kN = 30000000;
  std::vector<int> data(kN);
  for (int i = 0; i < kN; ++i) data[i] = i % 1000;

  long long total = 0;
  for (int r = 1; r <= 3; ++r) {
    auto ta = clock_type::now();
    long long va = design_a(data);
    long long da = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - ta).count();

    auto tb = clock_type::now();
    long long vb = design_b(data);
    long long db = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tb).count();

    locked_times = 0;
    auto tc = clock_type::now();
    long long vc = design_c(data, 1024);
    long long dc = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - tc).count();
    long long c_locks = locked_times;

    locked_times = 0;
    auto td = clock_type::now();
    long long vd = design_c(data, 1);
    long long dd = std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - td).count();
    long long d_locks = locked_times;

    total = va;
    if (vb != va || vc != va || vd != va) {   // 四种写法算的得是同一个数
      std::printf("mismatch: %lld %lld %lld %lld\n", va, vb, vc, vd);
      return 1;
    }
    std::printf("round %d: A %lld ms, B %lld ms, C %lld ms, D %lld ms\n",
                r, da, db, dc, dd);
    if (r == 3) {
      std::printf("C locked %lld times, D locked %lld times\n", c_locks, d_locks);
      std::printf("total = %lld\n", total);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

All three forms ran three rounds, with C invoked at `block = 1024`. Add a control group with `block` set to 1 — locking once per number added. In the readings, A is `design_a` (own independently), B is pass results, C is modify together, and D is this control group; this author added a separate counter in the function for the lock counts. The counter in How Do We Get the Lost Counts Back? — std::mutex and the Three Scoped Locks also locked once per increment, and D was written after it.

```text
round 1: A 3 ms, B 3 ms, C 3 ms, D 778 ms
round 2: A 2 ms, B 3 ms, C 4 ms, D 833 ms
round 3: A 3 ms, B 3 ms, C 4 ms, D 838 ms
C locked 29300 times, D locked 30000000 times
total = 9985005000000
```

In round one A and B tied, and this author would like to pause here: at the 30-million scale, the cost of one hand-off barely registers. C locked fewer than thirty thousand times and kept pace. The control group fell two orders of magnitude behind, because it locked 30 million times.

Set the three designs side by side:

| Design | Who modifies the data | Where synchronization happens | What it solves | The cost |
| -------- | ------------------------------------ | ---------------- | ---------------------------------- | -------------------------------------------------------------------------- |
| Own independently | Each thread modifies its own copy | At the final merge | No shared writes during the run; locks can be dropped entirely | Copying the whole data set per thread grows memory with thread count; storing only result slots costs thread count times result size |
| Pass results | The data has one owner; modification happens only in the owner's hands | At every hand-off | The shared thing shrinks to the hand-off point; everything else is exclusive | Every hand-off builds shared state and waits; the slowest link sets the pace |
| Modify together | Several threads directly modify the same copy | At every critical-section entry | Results are immediately visible; no copying, no merging | Cost scales with the number of modifications; ordering and boundaries are yours to manage |

The table can only lay the problem out clearly; the choosing stays yours: for a single total, all three work. For a table queried and modified repeatedly, own-independently cannot hold up — only the latter two remain, and pass results further needs a long-lived aggregator.

### Three More Roads to Less Sharing

The three designs above speak of "who modifies the data"; the three roads below speak of shrinking the sharing itself — don't mix the two groups.

The cheapest is read-only sharing. Fill the data before the threads start, let nobody modify it afterwards, and the lock is saved. In C++, passing it by `const` reference only blocks modification through that reference — leaving a non-`const` entrance elsewhere makes it useless. The serious way: fill the data in the constructor and never expose a writable entrance again.

One step further: give the object exactly one owner. We move a `std::unique_ptr<Chunk>` from one thread to another; at any moment exactly one hand holds it — whoever holds it is responsible, and nobody needs to guard against anybody. The hand-off itself needs synchronization: by `join`, by a future, or by a queue.

The last one: gather the sharing into one point. All threads hand their results to one place, and the thread standing guard there does the modifying. `design_b` does exactly this, and so does the queue-based form; the only difference is whether the aggregation point sits on the main thread or on a dedicated merging thread.

The judgment this author keeps for himself is short: must the data be shared? Answer "no," and everything after — locks, ordering, back-off — goes unasked. Only when you can't answer does the first half of this article's work begin.

## When It's Your Turn to Write

This author compressed the self-questions before writing into a few lines; run through them:

1. Must the data in your hand be shared? Which of these shrinks the sharing — one copy each (own independently), read-only, single owner, or gathered into one place?
2. If sharing is unavoidable, how many locks must one operation hold at once? Is the order settled — written into the code, or living only in comments?
3. If the order can't be settled, do you have the release-everything back-off? Do you back off between retries?
4. Which of the three designs did you take? Memory growing with thread count, waiting at every hand-off, or a climbing lock count — which of the three costs do you accept?

Lost counts, check-then-act, deadlock — each of the three problems now has its fix, and each its price to pay.

Before turning the page, pick a piece of code you wrote that holds two locks at once, and write the order down. If you can't, give the two objects numbers. And if there is still a spot of "if you can't take it, go do something else," write the back-off down too.

## Reference Resources

The trade-off comparison of the three arrangements references Anthony Williams' C++ Concurrency in Action chapters on design and sharing data; the examples and prose are this author's own.

- [C++ Concurrency in Action, 2nd Edition (Anthony Williams)][williams] — progression reference for the chapters on sharing data.

[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
