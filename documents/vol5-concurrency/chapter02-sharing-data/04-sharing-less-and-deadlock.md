---
title: "减少共享与避免死锁"
description: "从两个方向相反的挪钱把进程卡死讲起，逐条看 Coffman 四条件在程序里长什么样，再用锁顺序、分层和拿不到就全放的回退把它解开，最后用独立持有、传递结果、共同修改三种方案给单元二收尾。"
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

# 减少共享与避免死锁

这段挪钱的程序我跑了几遍，每一遍都只打出两行，然后停住不动。

```cpp
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

两个钱包，各一把锁和一份余额。一条线程从 A 挪十块给 B，另一条从 B 挪十块给 A。您要是读过上一篇，会认出它来：上一篇的钱包只有锁和余额，这次我给它加上编号，余额换成 `long long`。

为了拿到一个确定的读数，我用 `timeout 3` 跑它。三秒后进程被杀掉，退出码 124：

```text
t1: holds the first wallet, asking for the second
t2: holds the first wallet, asking for the second
exit=124
```

那行 `fflush(stdout)` 是给重定向用的。输出接进文件或者管道，`stdout` 的缓冲会跟着被杀掉的进程一起丢，这两行您就读不到了。

两条线程都拿到了自己那一把，然后各自去要对方手里那一把。谁也不松手，谁也不往下走，最后那行 `a = ..., b = ...` 您始终没等到，因为主线程卡在 `t1.join()` 上。

我又趁它卡着看了一眼进程。三条线程都停在内核等待里，`/proc/<pid>/task/*/wchan` 报的是 `futex_do_wait`（WSL2，内核 6.18.33.2，`uname -r` 报的），`ps` 报的 CPU 占用是 `0.0`。它在睡，不在算。

它有个名字，叫 AB-BA 反转：两条线程按相反的次序拿同一对锁。《从错误程序认识竞争》篇中讲死锁的小节，标题是“死锁：各自持一把，等对方松手”，您翻回去看，说的就是它。

本单元到这里是最后一篇，咱们把丢数和 check-then-act 都修过了，剩下的两条线在这里合上：能不共享就不共享，非共享不可就按次序。本篇接着问：四条条件在程序里各自长什么样，能从哪一处让它不成立，剩下的代价在哪。

死锁成立之后是稳定的，不看概率。这段程序有可能一次就过去，万一一条线程在另一条动手之前把自己那次挪动做完了呢。您多跑几遍，卡住的那些次都是同一个样子。

可只要两条线程停在“各持一把”的状态上，后面无论谁去要第二把都要不到，它不会自己解开。代码里那 100 毫秒的 sleep 就是干这件事的，它把两条线程都推进各持一把的状态，让您不用每次都靠运气。

## Coffman 四条件：缺一条，这次就卡不住

1971 年，Coffman、Elphick 和 Shoshani 在《System Deadlocks》里列过四条，后来咱们在教科书里看到的“Coffman 四条件”就是它。

四条都是必要条件：缺一条，死锁不可能成立。四条都在，也不保证每次都卡住，还得看时序，那 100 毫秒就是替咱们把时序摆好的。

### 互斥：同一时刻只让一条线程进来

`std::mutex` 的独占是四条里最硬的一条。一条线程把锁拿在手里，另一条调 `lock()` 就得停在原地，直到前一条放开（[thread.mutex.class]/1）。您在调试器里能看到它停在那一行，动不了。

能拿到手上的证据，是在里面待着的线程数，同一时刻最多一条。我在锁里放了个计数，进去加一、出来减一，记下出现过的最大值：

```cpp
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
```

我让四条线程各进一次，跑出来是：

```text
peak inside = 1, left inside = 0
```

1 是它们排队的证据。您把 `lock_guard` 删掉再跑，峰值就会往上走，我这边三遍都是 4。

### 持有并等待：手里攥着一把，还在等另一把

它不用另写程序，刚才那两行输出就是它的样子：两条线程都打出“握着第一把，要第二把”，然后一起停了。一条线程手里有一把，还在等另一把，这就是您看到的持有并等待。

不打印也看得出来：您把调试器挂上去，看线程栈，两条都停在 `lock()` 里面。

### 非抢占：等下去不会自己解开

`std::mutex` 的标准接口里，锁的操作只有 `lock()`、`try_lock()`、`unlock()` 三个，而 `unlock()` 只有持有者能调。标准里没有“帮别人放掉他那把锁”这一步，咱们翻遍标准库也找不到。

判据是等下去不会自己解开。咱们那次 `timeout 3` 就是现成的观察：三秒里没有任何一行新输出，锁也没自己松，最后是被外面的 `timeout` 杀掉的。

标准还补了一句，持有锁的线程退出（终止）是未定义行为（[thread.mutex.class]/5），所以您想让持有者死掉、把锁带走，也不通。

### 循环等待：两条边也能围成一个圈

咱们画出来的圈最短，只有两条边：t1 等 t2 手里的 B，t2 等 t1 手里的 A。三条线程各持一把、首尾相接，就是三条边，一样是死锁，只是您一眼不容易看出来。

您要把圈找出来，把“谁等谁”画成有向图就行，图里有环就是它。

四条摆完，咱们能动的只有两条：持有并等待、循环等待。互斥和非抢占是锁的本性，想共享就得互斥，标准又不给抢占的接口。

## 锁顺序：让环画不出来

咱们给钱包编号。编号在创建时定下来，之后不再变。任何一次挪动，不管钱往哪个方向走，都按编号从小到大拿锁：

```cpp
void move(Wallet& from, Wallet& to, long long amount) {
  Wallet& low  = from.id < to.id ? from : to;
  Wallet& high = from.id < to.id ? to : from;
  std::lock_guard<std::mutex> a(low.m);
  std::lock_guard<std::mutex> b(high.m);
  from.balance -= amount;
  to.balance += amount;
}
```

我让两条线程各跑 200000 次，一个从 A 挪给 B，一个反过来：

```cpp
std::thread t1([&] { for (int i = 0; i < 200000; ++i) move(a, b, 1); });
std::thread t2([&] { for (int i = 0; i < 200000; ++i) move(b, a, 1); });
```

```text
by id: a = 700, b = 300, sum = 1000
exit=0
```

没有卡住，退出码 0，您看跑完之后两边加起来是 1000。

中途呢？我另开一条线程当观察者，让它一把一把地读两个钱包：拿 A 的锁读 A，放下，再拿 B 的锁读 B。两次读之间，别的线程可能已经做完几笔挪动，它读到的和就跟任何时刻都对不上。下面是几个不是 1000 的采样：

```text
sample 3093: total 1017
sample 3094: total 999
sample 3920: total 1008
sampled 170453 totals, 3230 were not 1000
after join: a = 700, b = 300, sum = 1000
```

十七万多次采样里，咱们看到三千多次不是 1000。只有同时把两把锁都拿住，读到的和才对应某个真实时刻。这跟《一次操作的边界在哪里》里钱包的例子说的是同一件事：不变量跨两把锁，读它的人也得同时拿两把。

道理不复杂。假设所有线程都只按编号由小到大的方向拿锁。假如真的存在一个等待环，环上每条边都是“持有编号小的，等编号大的”：我手里是 3，等的是 7，他手里是 7，等的是 9。绕着环走一圈，编号必须一路严格变大，回到起点就要求“起点的编号比起点的编号大”。

环画不出来，剩下三条就凑不齐，死锁也就不成立。咱们得说清一个前提：全进程共用同一套编号，谁都别绕过它。

编号从哪来？对象创建时给一个，数据库主键也行，文件的绝对路径按字典序排也行，模块的层级也行。您选哪种都不要紧，唯一不能松的是次序得在进程里处处一致，不能某个模块按编号，另一个模块按名字。

### 没有编号的时候，按地址排

动态创建的对象，不方便提前编号。咱们可以按对象的地址排，前提是用 `std::less`，别用内建的 `<`：

```cpp
void move(Wallet& from, Wallet& to, long long amount) {
  Wallet& low  = std::less<Wallet*>{}(&from, &to) ? from : to;
  Wallet& high = std::less<Wallet*>{}(&from, &to) ? to : from;
  std::lock_guard<std::mutex> a(low.m);
  std::lock_guard<std::mutex> b(high.m);
  from.balance -= amount;
  to.balance += amount;
}
```

我把同一份程序换成按地址的写法，也是 200000 次来回，结果一样：

```text
by address: a = 700, b = 300, sum = 1000
```

为什么不能用内建的 `<`？两个指针指向不同的对象，又不是同一个数组的元素，也不是同一个对象的成员时，标准对 `p < q` 的结果说的是“未指定”（[expr.rel]/5）。它可能每次都给您同一个答案，也可能不是，您没法拿它当全序用。`std::less` 给的是一个实现定义的严格全序，对所有指针都成立（[comparisons]/2），咱们排序该用的就是它。

还有一层您自己能看见：换一次进程、换一个分配器，您再跑一遍，次序可能就换过来了。这不要紧，正确性只要求“同一次运行里，所有线程用同一套次序”。今天 A 排在 B 前面，明天反过来，都没关系，只要今天两条线程都认今天这套。

## 分层：让代码替您查顺序

编号这东西，写进注释也会有人看不见。您新加一处代码，没看见那段注释，反着写了一遍，死锁就又回来了，而且只在两条线程同时走到那里的时候回来。

分层不靠注释。咱们把层级交给锁自己记：每把锁带一个编号，每条线程记着自己现在停在哪一层，`lock()` 进来的时候比一次，反着拿就抛 `std::logic_error`，写反的人当场知道。

```cpp
#include <climits>
#include <mutex>
#include <stdexcept>

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
```

您看 `current_level_`，它是 `thread_local`（C++11 起）的，每条线程各有一份，互不干扰。谁也没拿锁的时候它是 `INT_MAX`，任何一层都能进。

检查写的是 `level_ >= current_level_`，相等也算违规。两把同一层的锁之间没有定好的次序，谁排在谁前面都说得过去，环就又能画出来。咱们把“同一层只能有一把”当成约定，`>=` 就顺手管住了。

咱们拿三层试一下：配置层编号 300，缓存层 200，设备层 100。一次正常的调用从配置层往下走：

```cpp
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
```

我另开一条线程跑 `read_all()`，再开一条，让它从设备层反着往上拿缓存层：

```cpp
void read_upward() {
  std::lock_guard<LevelMutex> g(device);
  try {
    std::lock_guard<LevelMutex> up(cache);   // 层级不对，进来之前就抛
    std::printf("upward: got cache as well\n");
  } catch (const std::logic_error& e) {
    std::printf("upward: %s\n", e.what());
  }
}
```

```text
config: level 300, going down to cache
cache: level 200, going down to device
device: level 100
upward: lock level goes the wrong way
both threads finished
```

两条线程都结束了，退出码 0。反向那次没有停在 `lock()` 里干等，它在进入临界区之前就被 `check()` 拦下来了。我这三遍跑下来，都是 config、cache、device 三行排在前头，upward 收在后面，换了调度也可能换过来。

您得接受三处代价。每把锁都得是 `LevelMutex`，裸的 `std::mutex` 用不进来。每次加锁多一次比较，虽然很便宜，但它是每一次。层级编号得全项目统一，跟编号一样，谁的模块自己定一套，检查就白做了。

还得说一句，这是教学实现，不是库。您拿进项目之前，错误处理和接口要求都得按自己那边的规范再验一遍。

它顺手还帮咱们抓另一件事。同一条线程重复拿同一把锁，也能死锁（[thread.mutex.class]/4），那不是循环等待，锁顺序帮不上忙。分层反而会当场抛出来，因为同一层不许进第二次。

## 拿不到就全放：回退这一路

锁顺序管得住拿的次序，管不住“拿不到还想拿”。咱们在《用互斥保护共享状态》里见过一个 `std::lock`，它走的是另一条路。

您翻开标准，跟死锁有关的要求只有一条：这一串调用的次序不会造成死锁，其余怎么排不管（[thread.lock.algorithm]/5）。同一节的注解举了个典型做法，叫 try-and-back-off：拿一把，试着拿下一把，试不上就把已经拿到的全放回去，换个次序重来。

所以准确地说，破掉持有并等待的是主流实现的那套回退。标准只管不死锁，具体怎么排由实现自己定，咱们能拿到手的保证，就只有不死锁。

C++17 的 `std::scoped_lock` 接过两把以上的锁时，构造函数调的就是 `std::lock`（[thread.lock.scoped]/2）。前面讲 `scoped_lock` 的那一篇是《用互斥保护共享状态》，它用这套写法收掉了 AB-BA。到了《一次操作的边界在哪里》，一次操作如果非得同时拿两把锁，可以走的就是这套写法，您在那儿又见过它一次。

咱们说回退的要点：绝不在握着锁的时候阻塞着等下一把。试不上就全放，手一空再去试，“持有并等待”就立不住脚了。

您可以把 `std::try_lock` 看成这套思路的非阻塞版，一件事试一轮就回来。它按参数顺序逐个 `try_lock`，中间有一把失败，就把前面拿到的都 `unlock`，然后返回失败项的零基下标，全部成功则返回 -1（[thread.lock.algorithm]/2、/3）。

```cpp
std::mutex m1, m2;

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
```

```text
holder: m1 taken, keeping it for 300 ms
main: try_lock(m2, m1) returned 1
main: m2 is free
exit=0
```

返回值 1 说的是第二把（`m1`）没试上，这一点没什么意外。咱们看后面那一下：刚才它明明已经握住了 `m2`，失败以后又还回去了，所以 `m2.try_lock()` 拿得到。

什么时候值得用这一套？您想想拿不到锁就该去干别的事的场合。比如一条线程负责刷新统计，锁被占着，它不该干等，可以攒着下一轮再刷。或者干脆降级：这次跳过，把这件事写进日志。

代价是重试，重试就可能活锁：两边同时松手、同时又伸手，每次都撞在同一个节拍上。《从错误程序认识竞争》里的活锁样本就是它，修法在退避，重试之间错开一个随机的时间。您要是只写 `while (true)` 加 `try_lock`，就等于把死锁换成了活锁。

还有一条边界，《用互斥保护共享状态》里提过：已经握在手里的锁，别递给 `std::lock` 或者 `std::try_lock`。算法要自己安排次序，您手里那一把它不知道，回退和重试都得绕着它转。

## 三种方案：独立持有、传递结果、共同修改

本篇开头说“能不共享就不共享”，到这儿才轮到它兑现。咱们在《用互斥保护共享状态》末尾留了个尾巴：独立持有、传递结果、共同修改，三种方案怎么取舍，归本篇。

三种方案说的是三种“谁在改数据”的安排，跟用哪套同步设施是两回事。同一件活我写了三遍，您对着看。

活很简单：3000 万个整数，求平方和，咱们让四条线程分着算。数据是 `data[i] = i % 1000` 从前往后填满的，数就落在 0 到 999 之间。

```cpp
constexpr int kThreads = 4;

long long sum_range(const std::vector<int>& data, int lo, int hi) {
  long long s = 0;
  for (int i = lo; i < hi; ++i) s += static_cast<long long>(data[i]) * data[i];
  return s;
}
```

独立持有的写法，您看主线程的动作：给每条线程准备一格，线程只写自己分到的槽位，跑完再合并。

```cpp
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
```

换成传递结果，每条线程算完把最终值交出去，自己不留下任何能改的东西。这里我借了 `std::promise` 和 `std::future`：第四单元 4.3《自行传递结果：`std::promise` 与 `std::future`》讲，本篇只用它们做一次交接。

```cpp
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
```

共同修改是另一条路：总额只有一份，四条线程都改它，靠锁保护。您看锁里只放了一次加法，算数的活全在外面。

```cpp
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
        total += part;
      }
    });
  }
  for (auto& th : threads) th.join();
  return total;
}
```

三份都跑三轮，C 用 `block = 1024` 调。再补一个对照组，把 `block` 改成 1，也就是每加一个数就锁一次。读数里 A 是 `design_a`（独立持有），B 是传递结果，C 是共同修改，D 就是这个对照组，锁的次数我在函数里另加了一个计数。《用互斥保护共享状态》里的计数器也是每加一次锁一次，D 就是照它写的。

```text
round 1: A 3 ms, B 3 ms, C 3 ms, D 778 ms
round 2: A 2 ms, B 3 ms, C 4 ms, D 833 ms
round 3: A 3 ms, B 3 ms, C 4 ms, D 838 ms
C locked 29300 times, D locked 30000000 times
total = 9985005000000
```

第一轮 A 和 B 并列，这一点我想停一下：在 3000 万的量级上，交接一次的开销还不够看。C 只锁了三万次不到，也跟着打平。对照组差了两个数量级，因为它锁了 3000 万次。

咱们把三种方案摆在一起：

| 方案 | 谁在改数据 | 同步发生在哪 | 它解决什么 | 代价 |
|------|------------|--------------|------------|------|
| 独立持有 | 每条线程改自己的一份 | 收尾合并那一次 | 运行期间没有共享写，锁可以完全不要 | 按线程复制整份数据时，内存随线程数增加。只存结果槽位时，是线程数乘结果大小 |
| 传递结果 | 数据只有一个主人，改只发生在主人手里 | 每一次交接 | 共享的东西缩成交接点，其余全是独占 | 每次交接都要建共享状态、都要等，链上最慢的一段决定整体节奏 |
| 共同修改 | 几条线程都直接改同一份 | 每一次进入临界区 | 结果立刻可见，不复制、不合并 | 代价跟改的次数成正比，次序和边界都得自己管 |

表格只能把问题摆清楚，挑还得您来：要一个总数，三种方案都行。要一张反复查、反复改的表，独立持有就撑不住，只剩后两种，传递结果这一路还得靠一个长期活着的汇总方。

### 减少共享的另外三条路

前面三种方案说的是“谁在改数据”，下面三条路说的是怎么把共享本身改小，您别把两组混在一起。《用互斥保护共享状态》里讲的两条路，`thread_local` 和把结果放进各自的位置，都是各持一份，归在前面那一组。

最省事的是只读共享。您把数据在线程起来之前填好，之后谁都不改，锁就省下了。C++ 里用 `const` 引用传进去只能挡住通过它去改，别处还留着非 `const` 的入口就没用。认真的做法是让数据在构造函数里填完，之后不再暴露可写的入口。

再进一步，让对象只有一个主人。咱们把一个 `std::unique_ptr<Chunk>` 从一条线程 move 给另一条，同一时刻只有一个人手里有它，谁拿到谁负责，谁也不用防谁。交接那一下本身要同步，可以靠 `join`、靠一个 future、靠一个队列。

最后一种，把共享收到一个点。您让所有线程都往一个地方交结果，守在那儿的线程负责改。`design_b` 就是这一路，队列版的写法也一样，区别只是汇总点在主线程，还是在一条专门负责合并的线程上。

我给自己留的判断法很短：这块数据非得共享吗？答得上“不必”，后面那些锁、次序、回退，一下都不用问了。答不上，才轮到本篇前半部分那些功夫。

## 轮到您动手的时候

我把这几件事写成自问的几句，落笔之前过一遍：

1. 这块数据非得共享吗？各持一份（也就是独立持有）、只读、单所有者、收到一处，哪一条能把共享改小？
2. 非共享不可，那一次操作要同时拿几把锁？次序定下来了吗，它写进代码了吗，还是只活在注释里？
3. 次序定不下来，您有拿不到就全放的回退吗？重试之间退避了吗？
4. 三种方案您走了哪条？内存按线程数增加、每次交接要等、锁的次数往上走，这三笔代价您认哪一笔？

## 下一步

咱们把本单元收到这里。丢数、check-then-act、死锁，三个问题各自有了修法，也各自有了要付的代价。下一单元换一个问题：让一条线程停下来，等到某件事成立再往下走。

传递结果要等交接点有东西可收，回退要等“过一会儿再试”，共同修改里每算完一块，部分和什么时候并进总额，也得等。三处都在等一个条件成立，而咱们现在手里只有 `sleep_for` 和 `try_lock` 转圈两种办法。轮询费 CPU，讲竞争那一篇的活锁样本就是转出来的。

下一单元的开篇[《等待一个条件成立》](../chapter03-wait-and-coordinate/01-wait-for-a-condition)已经写好，从轮询进场，把等待交给条件变量：咱们让线程睡下去，有人通知再醒，醒来重新看一遍条件。

翻页之前，请您挑一段自己写过的、同时拿两把锁的代码，把次序写下来。写不出来，就给两个对象加上编号。要是那段代码里还有“拿不到就改干别的”的地方，把回退也写下来。

## 参考资源

我写本篇的时候，推进顺序参照了 Anthony Williams 那本书。书名叫《C++ Concurrency in Action》，讲共享数据的几章跟咱们这里的安排最接近。Coffman 四条件的名称和定义照论文列出，白话解释和四条核对办法是我为了在程序里核对而补的。例子和文字都是我自己写的，实验在 GCC 16.2.1 上编译实测（`g++ -std=c++20 -O2 -pthread`，WSL2，16 逻辑核，2026-11-14）。输出都是单次运行的读数，换台机器或者换个负载，毫秒数会动。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的现行草案与 cppreference。

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
