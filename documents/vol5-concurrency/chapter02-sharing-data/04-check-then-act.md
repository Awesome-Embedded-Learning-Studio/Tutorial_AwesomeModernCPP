---
title: "一次操作的边界在哪里"
description: "把锁放到一次完整操作的边界上：看清检查与动手之间的窗口、跨两个对象的不变量，以及引用、迭代器和回调怎样把内部状态漏给调用者。"
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
---

# 一次操作的边界在哪里

《丢掉的数怎么收回来》把 `std::mutex` 和构造时拿锁、析构时放锁的 `lock_guard` 交到了咱们手里，计数器配上锁就不丢数了。可真实的操作很少只碰一个变量：一次下单要查库存再扣库存，一次转账要同时动两个钱包。锁在这些地方盖多大才算够，就是本篇的事。

货架上只剩一件货，两条线程同时来买，库存最后记成了 -1。这是笔者在实验里摆的货架，每一步都加了锁，一件货还是卖给了两位买家。

每一步都加锁还会卖错，毛病就不在锁本身，在锁盖住了哪一段。咱们没法用代码行数来量锁的边界：一次操作向调用者承诺了什么结果，锁就得盖住产生这个结果要经过的全部步骤和数据，承诺之外的事，不必塞进临界区。把边界划错，情形按对象个数往外扩：先一个对象内部，再两个对象之间，最后接口的出口。头两种在这篇摆现场，接口的出口事情最多，单独成篇，归下一篇《边界交给了调用者》。

## 检查通过了，动手的时候条件已经变了

第一处：检查与动手分成两次调用。开场那个货架就是现成的现场，先跑坏版：

<OnlineCompilerDemo
  title="货架坏版：检查与动手分开，一件货卖两位"
  source-path="code/examples/vol5/chapter02/18_demo_shelf_check_then_act.cpp"
  description="has_one 与 take_one 各自加锁，中间的窗口敞着：两位买家都 sees stock = 1、都 takes one，三轮全都是 stock left -1, sold 2。"
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
  title="buy()：两次临界区之间的一段敞开"
  source-path="code/examples/vol5/chapter02/18_demo_shelf_check_then_act.cpp"
  description="同上一个程序：检查走 has_one()，动手走 take_one()，中间 50 毫秒只是把窗口放大。真实的程序里窗口由调度凑出来，不需要谁真睡。"
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

`sleep_for` 是笔者加的，只为把窗口放大。真实的程序里，窗口由调度和两条线程各自的执行时间凑出来，不需要谁真的睡 50 毫秒。咱们跑了三轮，结果都一样：库存 -1、卖出 2。打印顺序由调度定，可能换过来。

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

您瞧，检查的时候两边都看到 1，检查完都放锁，中间谁也没挡住谁。`take_one()` 各拿一次锁，各减一次，减到 -1：一件货卖给了两位买家。

锁本身没有坏，可边界划错了。`has_one()` 和 `take_one()` 各自都加了锁，临界区里的访问也确实互斥，问题出在两次临界区之间。咱们把一次操作切成了两个临界区，中间一段是敞开的。互斥量保证同一时刻只有一个线程在临界区里，它不保证您走出临界区之后，状态还停在您看见的样子。

咱们只需要改一处：让锁盖住整次操作。

<OnlineCompilerDemo
  title="货架修版：判断与扣减进同一个临界区"
  source-path="code/examples/vol5/chapter02/19_demo_shelf_try_take.cpp"
  description="只改一处，try_take() 一次调用完成判断和扣减：三轮都是一人 takes the last one、另一人 finds none left，stock left 0, sold 1。"
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

咱们把 `buy()` 里的 `has_one()` 和 `take_one()` 换成了一次 `try_take()`，判断和扣减就在同一个临界区里完成了。笔者跑了三轮，结果都是：

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

咱们只认结果。谁抢到货，每回可能不一样，不变的是只有一个人拿到货。

咱们在货架上看的毛病有名字，叫 **check-then-act**，检查过了再动手，讲竞争的那一篇提过。它的形状不止货架，那一篇的库存例子也是同一类问题：查的时候还有，扣的时候已经被别人买走了。`size()` 看一眼再 `push_back`，`find()` 找到再 `erase()`，中间都隔着一次解锁和一次加锁，别的线程就能进来。

还有一层容易漏。如果接口把检查单独做成 `has_one()` 交出去，调用者拿它决定下一步，那您把临界区切在哪儿都没用，决策发生在锁外面。答案是在锁里读到的，可您拿它做决定的时候，锁已经放了，状态可能已经变了。`has_one()` 可以留着，给日志、给界面显示、给“大概还有没有”之类的参考。但凡调用者要拿它去改状态，接口就该提供一次调用完成判断和修改的操作，比如 `try_take()`。

`std::atomic` 也不解决这个问题，那一篇的原子计数器就是证据：每次读写都有定义，连起来还是丢。原子只管单次动作，复合操作要的是把动作连起来。咱们要的“连”，得靠锁，或者靠一次原子读改写，比如 `fetch_add`。后者要等讲原子操作与内存模型的几篇文章。

## 一条不变量横跨两个对象

第二处：一条关系跨在两个对象上。笔者拿两个钱包做例子（`Wallet`，分锁的版本），A 里有 700，B 里有 300，两边加起来 1000。操作是从 A 挪 10 到 B。要求很朴素：任何时刻两边加起来都得是 1000。咱们管这样的关系叫**不变量**，操作做到一半它也得成立。

<OnlineCompilerDemo
  title="分锁钱包：每个钱包一把锁，和不变量没人管"
  source-path="code/examples/vol5/chapter02/20_demo_wallet_split_locks.cpp"
  description="两步各自加锁、中间放锁：观察线程近一半采样读到 990（A 减了 10、B 还没加上）。每回挪 40 次跑两回，final 行两边加起来仍是 1000。"
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

每一步都锁了，每个钱包的余额也从来没被两条线程同时碰过。可挪动本身有两步，两步之间放锁，别的线程就能看见“A 少了 10，B 还没多”。笔者加了一条观察线程，它按同样的办法读两边：

<OnlineCompilerDemo
  title="观察线程的 total()：同样的办法分开读两边"
  source-path="code/examples/vol5/chapter02/20_demo_wallet_split_locks.cpp"
  description="同一个程序：total() 锁 a 再锁 b 分开读，move() 停在两步中间时它也正好读到 990。坏采样计数就是它攒出来的。"
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

观察线程读两边的时候，`move()` 也可能正停在两步中间。笔者让 `move()` 两步之间睡 1 毫秒，挪完再睡 1 毫秒，让两种状态各占一段时间。每一回挪 40 次，跑两回。观察线程从挪动开始跟着采，挪动大概一百毫秒量级结束（40 次乘上每次两段 1 毫秒睡眠，没单独计时），挪动结束就停。下面是第一回的前四个坏采样和两回的汇总：

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

990 是 A 已经减了 10、B 还没加上的那一刻。它每次只停 1 毫秒，观察线程采得密，两回都有近一半的采样落在里面。您可能觉得观察线程太闲，现实里要读两边总和的人不少：报表、额度检查、另一个方向的挪动，都可能在中间读。

咱们把修法收在一处。不变量说的是 A 和 B 的和，那就让一把锁把 A 和 B 一起盖住。换成 `Ledger`（合锁的版本）：

<OnlineCompilerDemo
  title="合锁账本：一把锁盖住两边，零次读到 990"
  source-path="code/examples/vol5/chapter02/21_demo_ledger_one_lock.cpp"
  description="Ledger 的 transfer 与 total 共用一把锁，transfer 里两次修改在同一个临界区。同一套采样窗口再跑两回：零个采样偏离 1000。"
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

`transfer()` 里两次修改在同一个临界区里，`total()` 也用同一把锁。咱们用同一套采样窗口再跑两回。实验里的调用方在每次 `transfer()` 之后睡 1 毫秒，和 Wallet 版的节奏对齐，零次读到 990：

```text
round 1: one lock for both sides: 0 of 1524458 samples saw a total other than 1000
round 1 final: A = 300, B = 700, total = 1000
round 2: one lock for both sides: 0 of 1511547 samples saw a total other than 1000
round 2 final: A = 300, B = 700, total = 1000
```

关系提到两份数据，只锁其中一份，另一份就在保护之外。观察者也得用同一把锁，不然它仍然会把两边分开读。咱们写接口的时候，常按对象分锁，每个钱包一把。不变量跨过两把锁，就没有任何一把锁在管。

一把锁盖住所有钱包，代价是所有挪动都排队，哪怕两边根本不相干。咱们要按钱包分锁，就得在一次挪动里同时拿住两把，并且固定拿锁顺序，防止两边互相等。《丢掉的数怎么收回来》里讲过的 C++17 `std::scoped_lock` 可以一次接收几把锁，拿法交给标准库的死锁避免算法。拿锁顺序和死锁预防归《谁也不松手，怎么办》，咱们本篇只把边界说清：不变量需要两把锁一起在场，缺一把都不成立。

> 咱们实验里的 `Shelf`、`Wallet` 和 `Ledger` 都把字段露在外面。真正写接口的时候，字段和锁要一起收进 private，《丢掉的数怎么收回来》里的 `Counter` 就是这么做的。封装之后还会从哪里漏，往下讲接口的部分接着说。


## 参考资源

本篇写作时对照借鉴了 Anthony Williams 的《C++ Concurrency in Action》（Manning 第二版），check-then-act 的讲法参考了它讲共享数据的几章。例子与文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [std::mutex（cppreference）][cppref-mutex]，互斥量的接口与不可重入的说明，C++11 起。
- [std::lock_guard（cppreference）][cppref-lockguard]，构造时加锁、析构时解锁，C++11 起。
- [Mutex requirements（现行草案 thread.mutex.requirements.mutex）][draft-mutex-req]，`lock()` 与 `unlock()` 的前置条件和同步关系。
- [Class mutex（现行草案 thread.mutex.class）][draft-mutex-class]，不可重入，同一线程重复加锁可能死锁（草案注）。
- [Data races（现行草案 intro.races）][draft-races]，data race 的定义与未定义行为。
- [Multi-threaded executions and data races（cppreference）][cppref-multithread]，happens-before 与同步操作的说明。
- [ThreadSanitizer（Google Sanitizers wiki）][tsan-wiki]，报告格式与检测范围。

[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[cppref-lockguard]: https://en.cppreference.com/w/cpp/thread/lock_guard
[draft-mutex-req]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-mutex-class]: https://eel.is/c++draft/thread.mutex.class
[draft-races]: https://eel.is/c++draft/intro.races
[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
