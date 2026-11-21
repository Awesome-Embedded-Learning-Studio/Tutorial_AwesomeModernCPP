---
title: "谁也不松手，怎么办：减少共享与避免死锁"
description: "从两个方向相反的挪钱把进程卡死讲起，逐条看 Coffman 四条件在程序里长什么样，再用锁顺序、分层和拿不到就全放的回退把它解开，最后用独立持有、传递结果、共同修改三种方案给单元二收尾。"
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
---

# 谁也不松手，怎么办：减少共享与避免死锁

《丢掉的数怎么收回来》里的计数器一把锁就护住了，因为一次加减只碰一个对象。挪钱不是这种活：钱从钱包 A 挪到钱包 B，两个钱包都得在手里。两条线程各挪各的，握锁的次序要是不巧正好相反，会发生什么？笔者把它写成了程序。

挪钱的程序笔者跑了六遍，六遍都只打出两行，然后停住不动。

<OnlineCompilerDemo
  title="AB-BA 反转：方向相反的两次挪钱把进程卡死"
  source-path="code/examples/vol5/chapter02/28_demo_abba_deadlock.cpp"
  description="两条线程各打出 holds the first wallet 之后一起停住（在线运行有超时会替您收场，本地得 Ctrl-C）。fflush 是给重定向留的，否则被杀进程的缓冲会让两行都丢掉。"
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

两个钱包，各一把锁和一份余额。一条线程从 A 挪十块给 B，另一条从 B 挪十块给 A。您要是读过《一次操作的边界在哪里》，会认出它来：那里的钱包只有锁和余额，本篇给它加上了编号，余额换成 `long long`。

为了拿到一个确定的读数，笔者用 `timeout 3` 跑它。三秒后进程被杀掉，退出码 124：

```text
t1: holds the first wallet, asking for the second
t2: holds the first wallet, asking for the second
exit=124
```

`fflush(stdout)` 是给重定向用的。输出接进文件或者管道，`stdout` 的缓冲会跟着被杀掉的进程一起丢，两行输出您就读不到了。

两条线程都拿到了各自的锁，然后各自去要对方手里的锁。谁也不松手，谁也不往下走，收尾的 `a = ..., b = ...` 您始终没等到，因为主线程卡在 `t1.join()` 上。

笔者又趁它卡着看了一眼进程。三条线程都停在内核等待里，`/proc/<pid>/task/*/wchan` 报的是 `futex_do_wait`（WSL2，内核 6.18.33.2，`uname -r` 报的），`ps` 报的 CPU 占用是 `0.0`。它在睡，不在算。

它有个名字，叫 **AB-BA 反转**：两条线程按相反的次序拿同一对锁。

两条线在这里合上：能不共享就不共享，非共享不可就按次序。

死锁成立之后是稳定的，不看概率。这段程序有可能一次就过去，万一一条线程在另一条动手之前，已经把自己的一次挪动做完了呢。您多跑几遍，卡住的每一回都是同一个样子。

可只要两条线程停在“各持一把”的状态上，后面无论谁去要第二把都要不到，它不会自己解开。代码里 100 毫秒的 sleep 就是干这件事的，它把两条线程都推进各持一把的状态，让您不用每次都靠运气。

## Coffman 四条件：缺一条，死锁就立不住

1971 年，Coffman、Elphick 和 Shoshani 在《System Deadlocks》里列过四条，后来咱们在教科书里看到的 **Coffman 四条件** 就是它。

四条都是必要条件：缺一条，死锁不可能成立。四条都在，也不保证每次都卡住，还得看时序，那个 sleep 就是替咱们把时序摆好的。

### 互斥：同一时刻只让一条线程进来

`std::mutex` 的独占是四条里最硬的一条。一条线程把锁拿在手里，另一条调 `lock()` 就得停在原地，直到前一条放开（[thread.mutex.class]/1）。您在调试器里能看到它就停在调用 `lock()` 的地方，动不了。

证据也拿得到手上：数一数锁里待着的线程，同一时刻最多一条。笔者在锁里放了个计数，进去加一、出来减一，记下出现过的最大值：

<OnlineCompilerDemo
  title="互斥的证据：锁里至多一条线程"
  source-path="code/examples/vol5/chapter02/29_demo_peak_inside.cpp"
  description="锁里放个计数，进去加一、出来减一，记下出现过的最大值：四条线程各进一次，peak inside = 1。把 lock_guard 删掉再跑，峰值就会往上走。"
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

笔者让四条线程各进一次，单次读数是：

```text
peak inside = 1, left inside = 0
```

1 是它们排队的证据。您把 `lock_guard` 删掉再跑，峰值就会往上走，笔者这边三遍都是 4。这读数只当观察，删了锁之后计数本身就在竞争，不算安全代码，4 只是这三次没撕出别的样子。

### 持有并等待：手里攥着一把，还在等另一把

证据不用另找：t1、t2 两行输出就是它的样子，两条线程都打出“握着第一把，要第二把”，然后一起停了。一条线程手里有一把，还在等另一把，这就是您看到的持有并等待。

不打印也看得出来：您把调试器挂上去，看线程栈，两条都停在 `lock()` 里面。

### 非抢占：等下去不会自己解开

`std::mutex` 的标准接口里，锁的操作只有 `lock()`、`try_lock()`、`unlock()` 三个，而 `unlock()` 只有持有者能调。标准里没有“帮别人放掉手里的锁”的步骤，咱们翻遍标准库也找不到。

判据是等下去不会自己解开。咱们拿 `timeout 3` 跑出来的读数就是现成的观察：三秒里没有任何一行新输出，锁也没自己松，最后是被外面的 `timeout` 杀掉的。

标准还补了一句，持有锁的线程退出（终止）是未定义行为（[thread.mutex.class]/5），所以您想让持有者死掉、把锁带走，也不通。

### 循环等待：两条边也能围成一个圈

眼前这个圈最短，只有两条边：t1 等 t2 手里的 B，t2 等 t1 手里的 A。三条线程各持一把、首尾相接，就是三条边，一样是死锁，只是您一眼不容易看出来。

您要把圈找出来，把“谁等谁”画成有向图就行，图里有环就是它。

四条摆完，咱们能动的只有两条：持有并等待、循环等待。互斥和非抢占是锁的本性：要共享数据就躲不开互斥，标准又不给抢占的接口。

## 锁顺序：让环画不出来

咱们给钱包编号。编号在创建时定下来，之后不再变。任何一次挪动，不管钱往哪个方向走，都按编号从小到大拿锁：

<OnlineCompilerDemo
  title="按编号拿锁：200000 次对向挪动，一次没卡"
  source-path="code/examples/vol5/chapter02/30_demo_lock_order_by_id.cpp"
  description="不管钱往哪个方向走，都按编号从小到大拿。前半程只跑挪动：sum 稳在 1000、退出码 0。后半程另开观察线程一把一把地读，跨挪动拼接出的和会偏离 1000——要读到真实时刻得同时拿住两把。"
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

咱们让两条线程各跑 200000 次，一个从 A 挪给 B，一个反过来：

```text
by id: a = 700, b = 300, sum = 1000
exit=0
```

没有卡住，退出码 0，您看跑完之后两边加起来是 1000。

中途呢？笔者另开一条线程当观察者，让它一把一把地读两个钱包：拿 A 的锁读 A，放下，再拿 B 的锁读 B。两次读之间，别的线程可能已经做完几笔挪动，它读到的和就跟任何时刻都对不上。下面是几个不是 1000 的采样：

```text
sample 1: total 577
sample 2: total 1750
sample 3: total 1035
sampled 50000 totals, 90 were not 1000
after join: a = 700, b = 300, sum = 1000
```

五万次采样里，咱们看到九十次不是 1000。只有同时把两把锁都拿住，读到的和才对应某个真实时刻。这跟《一次操作的边界在哪里》里钱包的例子说的是同一件事：不变量跨两把锁，读它的人也得同时拿两把。

道理不复杂。假设所有线程都只按编号由小到大的方向拿锁。真要存在一个等待环，环上每条边都是“持有编号小的，等编号大的”：我手里是 3，等的是 7，他手里是 7，等的是 9。绕着环走一圈，编号必须一路严格变大，回到起点就要求“起点的编号比起点的编号大”。

环画不出来，剩下三条就凑不齐，死锁也就不成立。咱们得说清一个前提：全进程共用同一套编号，谁都别绕过它。

编号从哪来？对象创建时给一个，数据库主键也行，文件的绝对路径按字典序排也行，模块的层级也行。您选哪种都不要紧，唯一不能松的是次序得在进程里处处一致，不能某个模块按编号，另一个模块按名字。

### 没有编号的时候，按地址排

动态创建的对象，不方便提前编号。咱们可以按对象的地址排，前提是用 `std::less`，别用内建的 `<`：

<OnlineCompilerDemo
  title="没有编号的时候，按地址排"
  source-path="code/examples/vol5/chapter02/31_demo_lock_order_by_address.cpp"
  description="动态创建的对象用 std::less<Wallet*> 排出全序，同样 200000 次来回 sum 稳在 1000。别用内建的 <：指向不同对象的指针比大小，标准说结果未指定，当不得全序。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者把同一份程序换成按地址的写法，也是 200000 次来回，结果一样：

```text
by address: a = 700, b = 300, sum = 1000
```

为什么不能用内建的 `<`？两个指针指向不同的对象，又不是同一个数组的元素，也不是同一个对象的成员时，标准对 `p < q` 的结果说的是“未指定”（[expr.rel]/5）。它可能每次都给您同一个答案，也可能不是，您没法拿它当全序用。`std::less` 给的是一个实现定义的严格全序，对所有指针都成立（[comparisons.general]/2），咱们排序该用的就是它。

还有一层您自己能看见：换一次进程、换一个分配器，您再跑一遍，次序可能就换过来了。这不要紧，正确性只要求“同一次运行里，所有线程用同一套次序”。今天 A 排在 B 前面，明天反过来，都没关系，只要今天两条线程都认今天的次序。

## 分层：让代码替您查顺序

编号这东西，写进注释也会有人看不见。您新加一处代码，没看见注释，反着写了一遍，死锁就又回来了，而且只在两条线程同时走到那里的时候回来。

分层不靠注释。咱们把层级交给锁自己记：每把锁带一个编号，每条线程记着自己现在停在哪一层，`lock()` 进来的时候比一次，反着拿就抛 `std::logic_error`，写反的人当场知道。

<OnlineCompilerDemo
  title="LevelMutex：每把锁记着自己的层级"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="lock() 进来就 check()"
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

您看 `current_level_`，它是 `thread_local`（C++11 起）的，每条线程各有一份，互不干扰。谁也没拿锁的时候它是 `INT_MAX`，任何一层都能进。

检查写的是 `level_ >= current_level_`，相等也算违规。两把同一层的锁之间没有定好的次序，谁排在谁前面都说得过去，环就又能画出来。咱们把“同一层只能有一把”当成约定，`>=` 就顺手管住了。

咱们拿三层试一下：配置层编号 300，缓存层 200，设备层 100。一次正常的调用从配置层往下走：

<OnlineCompilerDemo
  title="分层互斥：拿反了当场抛，不进锁里等"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="配置层 300、缓存层 200、设备层 100：从上往下正常走，反向在进临界区之前就被 check() 拦下 logic_error。current_level_ 是 thread_local，同一层进第二次也拦。教学实现，别当库用。"
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
  title="三层往下走与反向往上拿"
  source-path="code/examples/vol5/chapter02/32_demo_level_mutex.cpp"
  description="同一个程序：read_all 从 300 走到 100 三行顺序输出。read_upward 拿着设备层去拿缓存层，lock level goes the wrong way，两条线程都正常结束。"
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

笔者另开一条线程跑 `read_all()`，再开一条，让它从设备层反着往上拿缓存层：

```text
config: level 300, going down to cache
cache: level 200, going down to device
device: level 100
upward: lock level goes the wrong way
both threads finished
```

两条线程都结束了，退出码 0。反向的调用没有停在 `lock()` 里干等，它在进入临界区之前就被 `check()` 拦下来了。笔者跑了三遍，都是 config、cache、device 三行排在前头，upward 收在后面，换了调度也可能换过来。

您得接受三处代价。每把锁都得是 `LevelMutex`，裸的 `std::mutex` 用不进来。每次加锁多一次比较，单看很便宜，架不住回回都要付。层级编号得全项目统一，跟编号一样，谁的模块自己定一套，检查就白做了。

这是教学实现，不是库。您拿进项目之前，错误处理和接口要求都得按自己那边的规范再验一遍。

它顺手还帮咱们抓另一件事。同一条线程重复拿同一把锁，也能死锁（[thread.mutex.class]/4），那不是循环等待，锁顺序帮不上忙。分层反而会当场抛出来，因为同一层不许进第二次。

## 拿不到就全放：回退的路子

锁顺序管得住拿的次序，管不住“拿不到还想拿”。咱们在《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》里见过一个 `std::lock`，它走的是另一条路。

您翻开标准，跟死锁有关的要求只有一条：一串调用的次序不会造成死锁，其余怎么排不管（[thread.lock.algorithm]/5）。同一节的注解举了个典型做法，叫 try-and-back-off：拿一把，试着拿下一把，试不上就把已经拿到的全放回去，换个次序重来。

所以准确地说，破掉持有并等待的是主流实现自己选的回退做法。标准只管不死锁，具体怎么排由实现自己定，咱们能拿到手的保证，就只有不死锁。

C++17 的 `std::scoped_lock` 接过两把以上的锁时，构造函数调的就是 `std::lock`（[thread.lock.scoped]/2）。

咱们说回退的要点：绝不在握着锁的时候阻塞着等下一把。试不上就全放，手一空再去试，“持有并等待”就立不住脚了。

您可以把 `std::try_lock` 看成它的非阻塞版，一件事试一轮就回来。它按参数顺序逐个 `try_lock`，中间有一把失败，就把前面拿到的都 `unlock`，然后返回失败项的零基下标，全部成功则返回 -1（[thread.lock.algorithm]/2、/3）。

<OnlineCompilerDemo
  title="std::try_lock：试不上就把拿到的全放回去"
  source-path="code/examples/vol5/chapter02/33_demo_try_lock_backoff.cpp"
  description="holder 拿着 m1 睡 300 毫秒。main 的 try_lock(m2, m1) 拿到 m2、试不上 m1，返回 1，顺手把 m2 放回去，紧接着 m2.try_lock() 证明它是空的。"
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

返回值 1 说的是第二把（`m1`）没试上，没什么意外。真正值得咱们看的是后面：刚才它明明已经握住了 `m2`，失败以后又还回去了，所以 `m2.try_lock()` 拿得到。

什么时候值得走回退？您想想拿不到锁就该去干别的事的场合。比如一条线程负责刷新统计，锁被占着，它不该干等，可以攒着下一轮再刷。或者干脆降级，跳过当次，把这件事写进日志。

还有一条边界，《丢掉的数怎么收回来？——std::mutex 与三种作用域锁》里提过：已经握在手里的锁，别递给 `std::lock` 或者 `std::try_lock`。算法要自己安排次序，它不知道您手里已经握着一把，回退和重试都得绕着它转。

## 参考资源

笔者写本篇的时候，推进顺序参照了 Anthony Williams 那本书。书名叫《C++ Concurrency in Action》，讲共享数据的几章跟咱们这里的安排最接近。Coffman 四条件的名称和定义照论文列出，白话解释和四条核对办法是笔者为了在程序里核对而补的。例子和文字都是笔者自己写的。

- [std::mutex（cppreference）][cppref-mutex]，C++11 起，非递归、独占所有权。
- [std::lock（cppreference）][cppref-lock]，一次拿多把锁，保证调用序列不死锁。
- [std::try_lock（cppreference）][cppref-trylock]，非阻塞的多锁尝试，失败时释放已经拿到的。
- [std::scoped_lock（cppreference）][cppref-scoped]，C++17 起，多把锁时转调 `std::lock`。
- [std::less（cppreference）][cppref-less]，指针特化给出一致的全序。
- [Class mutex（现行草案 thread.mutex.class）][draft-mutex-class]，非递归、阻塞语义，以及同线程重复加锁可能死锁的注解。
- [Generic locking algorithms（现行草案 thread.lock.algorithm）][draft-lock-algorithm]，`std::lock` 与 `std::try_lock` 的要求和 try-and-back-off 注解。
- [Class template scoped_lock（现行草案 thread.lock.scoped）][draft-lock-scoped]，构造函数在多参数时调用 `lock(m...)`。
- [Relational operators（现行草案 expr.rel）][draft-expr-rel]，指向不同对象的指针做大小比较，结果未指定。
- [Comparisons（现行草案 comparisons）][draft-comparisons]，`std::less` 对指针给出实现定义的严格全序。
- [System Deadlocks（Coffman、Elphick、Shoshani，1971）][coffman]，四条件的出处。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，共享数据与死锁各章的推进顺序参考。

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
