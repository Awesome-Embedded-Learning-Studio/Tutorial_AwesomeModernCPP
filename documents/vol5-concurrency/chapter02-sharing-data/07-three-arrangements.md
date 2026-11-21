---
title: "能不共享就不共享：三种安排怎么挑"
description: "同一份求平方和的活写三遍：独立持有、传递结果、共同修改三种安排对着看，给出挑选的判据。"
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
---

# 能不共享就不共享：三种安排怎么挑

死锁的防线都在前一篇：锁顺序、分层、回退，防的都是非共享不可的场面。还有一条更便宜的路，从根上就少碰同一份数据。

## 三种安排怎么挑：独立持有、传递结果、共同修改

三种方案说的是三种“谁在改数据”的安排，跟用哪套同步设施是两回事。同一件活笔者写了三遍，您对着看。

活很简单：3000 万个整数，求平方和，咱们让四条线程分着算。数据是 `data[i] = i % 1000` 从前往后填满的，数就落在 0 到 999 之间。

<OnlineCompilerDemo
  title="四种写法同台：独立持有、传递结果、共同修改、每数一锁"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="3000 万个整数求平方和，四条线程分着算：A 各写各的槽位、B 用 promise/future 交结果、C 每块 1024 个数进一次锁、D 每个数进一次锁。三轮读数加锁次统计——A/B/C 打平，D 差两个数量级，因为锁了 3000 万次。"
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

独立持有的写法，您看主线程的动作：给每条线程准备一格，线程只写自己分到的槽位，跑完再合并。

<OnlineCompilerDemo
  title="design_a：线程只写自己的槽位"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="同一个程序里的 design_a：主线程给每条线程准备一格 parts[t]，线程只碰自己那格，join 之后主线程合并。运行期间没有共享写，锁一把都没有。"
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

换成传递结果，每条线程算完把最终值交出去，自己不留下任何能改的东西。这里笔者借了 `std::promise` 和 `std::future`：第四单元 4.3《自行传递结果：`std::promise` 与 `std::future`》讲，本篇只用它们做一次交接。

<OnlineCompilerDemo
  title="design_b：算完把最终值交出去"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="同一个程序里的 design_b：每条线程把最终和 set_value 给自己的 promise，主线程逐个 f.get() 收——算完一条，收一条。线程自己不留下任何能改的东西。"
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

共同修改是另一条路：总额只有一份，四条线程都改它，靠锁保护。您看锁里只放了一次加法，算数的活全在外面。

<OnlineCompilerDemo
  title="design_c：锁里只放一次加法"
  source-path="code/examples/vol5/chapter02/34_demo_three_designs.cpp"
  description="同一个程序里的 design_c：总额只有一份，四条线程都改它。每块 1024 个数算完才进一次锁，锁里只有 total += part 一行。把 block 换成 1 就是对照组 D。"
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

三种写法都跑三轮，C 用 `block = 1024` 调。再补一个对照组，把 `block` 改成 1，也就是每加一个数就锁一次。读数里 A 是 `design_a`（独立持有），B 是传递结果，C 是共同修改，D 就是这个对照组，锁的次数笔者在函数里另加了一个计数。《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》里的计数器也是每加一次锁一次，D 就是照它写的。

```text
round 1: A 3 ms, B 3 ms, C 3 ms, D 778 ms
round 2: A 2 ms, B 3 ms, C 4 ms, D 833 ms
round 3: A 3 ms, B 3 ms, C 4 ms, D 838 ms
C locked 29300 times, D locked 30000000 times
total = 9985005000000
```

第一轮 A 和 B 并列，这儿笔者想停一下：在 3000 万的量级上，交接一次的开销还不够看。C 只锁了三万次不到，也跟着打平。对照组差了两个数量级，因为它锁了 3000 万次。

咱们把三种方案摆在一起：

| 方案     | 谁在改数据                           | 同步发生在哪     | 它解决什么                         | 代价                                                                       |
| -------- | ------------------------------------ | ---------------- | ---------------------------------- | -------------------------------------------------------------------------- |
| 独立持有 | 每条线程改自己的一份                 | 收尾合并的时候   | 运行期间没有共享写，锁可以完全不要 | 按线程复制整份数据时，内存随线程数增加。只存结果槽位时，是线程数乘结果大小 |
| 传递结果 | 数据只有一个主人，改只发生在主人手里 | 每一次交接       | 共享的东西缩成交接点，其余全是独占 | 每次交接都要建共享状态、都要等，链上最慢的一段决定整体节奏                 |
| 共同修改 | 几条线程都直接改同一份               | 每一次进入临界区 | 结果立刻可见，不复制、不合并       | 代价跟改的次数成正比，次序和边界都得自己管                                 |

表格只能把问题摆清楚，挑还得您来：要一个总数，三种方案都行。要一张反复查、反复改的表，独立持有就撑不住，只剩后两种，传递结果还得靠一个长期活着的汇总方。

### 减少共享的另外三条路

前面三种方案说的是“谁在改数据”，下面三条路说的是怎么把共享本身改小，您别把两组混在一起。

最省事的是只读共享。您把数据在线程起来之前填好，之后谁都不改，锁就省下了。C++ 里用 `const` 引用传进去只能挡住通过它去改，别处还留着非 `const` 的入口就没用。认真的做法是让数据在构造函数里填完，之后不再暴露可写的入口。

再进一步，让对象只有一个主人。咱们把一个 `std::unique_ptr<Chunk>` 从一条线程 move 给另一条，同一时刻只有一个人手里有它，谁拿到谁负责，谁也不用防谁。交接本身要同步，可以靠 `join`、靠一个 future、靠一个队列。

最后一种，把共享收到一个点。您让所有线程都往一个地方交结果，守在那儿的线程负责改。`design_b` 就是这么干的，队列版的写法也一样，区别只是汇总点在主线程，还是在一条专门负责合并的线程上。

笔者给自己留的判断法很短：数据非得共享吗？答得上“不必”，后面锁、次序、回退的事，一下都不用问了。答不上，才轮到本篇前半部分的功夫。

## 轮到您动手的时候

笔者把落笔前该自问的话写成几句，过一遍：

1. 您手头的数据非得共享吗？各持一份（也就是独立持有）、只读、单所有者、收到一处，哪一条能把共享改小？
2. 非共享不可，一次操作要同时拿几把锁？次序定下来了吗，它写进代码了吗，还是只活在注释里？
3. 次序定不下来，您有拿不到就全放的回退吗？重试之间退避了吗？
4. 三种方案您走了哪条？内存按线程数增加、每次交接要等、锁的次数往上走，三笔代价您认哪一笔？

丢数、check-then-act、死锁，三个问题各自有了修法，也各自有了要付的代价。

翻页之前，请您挑一段自己写过的、同时拿两把锁的代码，把次序写下来。写不出来，就给两个对象加上编号。要是里面还有“拿不到就改干别的”的地方，把回退也写下来。


## 参考资源

三种安排的取舍对照参考了 Anthony Williams 的《C++ Concurrency in Action》讲设计与共享数据的章节，例子与文字都是笔者自己写的。

- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，共享数据各章的推进顺序参考。

[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
