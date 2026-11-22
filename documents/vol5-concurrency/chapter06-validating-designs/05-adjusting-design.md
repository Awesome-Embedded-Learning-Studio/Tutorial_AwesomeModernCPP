---
title: "怎样调整设计"
description: "缓存行与假共享的实测与修法，锁竞争、任务过细、共享过多的读数形状，以及量在前、改在后的调整顺序，单元六在这里收束。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 怎样调整设计

多线程读数都是单次运行的读数，几轮原始读数笔者原样列出，不挑最好看的一轮。

## 缓存行：假共享从哪里来

CPU 跟内存打交道，不按字节来，按行来。内存被划成一段段等长的小块，一段就是一条缓存行，在常见的桌面与服务器处理器上，一条 64 字节。您读行里的一个字节，整条行一起进缓存。您写行里的一个字节，别人要是也写同一条行，麻烦就跟着来了。行宽随实现变。笔者机器上的答数摆在下面的命令里，64。C++17 起标准还给了常量 `std::hardware_destructive_interference_size`，住在 `<new>` 里，语义是避免假共享需要的最小间隔（现行草案给它 `[hardware.interference]` 的条款名），值由实现自定：Apple 的 arm64 核心行宽是 128 字节，Clang 在 2026 年 7 月为 Apple 目标把常量调成 128（提交说明在参考资源里，原话是 sysctl 报 128），而 GCC 在 AArch64 上给的是 ARM 文档的保守值 256。同一个常量，换个目标就换个值，所以它不适合编进跨编译器共享的二进制接口。

```bash
cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
```

```text
64
```

5.3《[一个线程怎样观察另一个线程的修改](../chapter05-atomic-memory-model/03-happens-before)》讲缓存一致性的时候，咱们见过它的脾气：核心要写一条行，得拿独占权，别的核手里的副本当场作废。两条核各写各的变量，只要变量住在同一条行上，独占权就在两条核之间来回倒手，谁也别想安稳写下去。变量的逻辑关系一点没变，读数塌了。

咱们拿实验把机制变成读数。两个计数器，各归各的线程写，源代码里找不出一处共享。Packed 版把两个 `long long` 装进同一个结构体，地址只差 8 字节，落在同一条行上。Slot 版给每个计数器套上 `alignas(64)`，两个地址相差整 64 字节，一人一条行。再摆一条串行对照，一条线程把两个计数器挨个加完，好让咱们知道单核的速度在哪里。计数器声明成了 `volatile long long`，您别误会，它不是同步手段，5.5《[判据的练习场：停止标志、引用计数与 DCLP](../chapter05-atomic-memory-model/05-memory-order-patterns)》划过 volatile 的边界，这里是拿它挡编译器：没有它，`-O2` 会把两千万次加一折成一次加法，行争用根本没机会发生。顺带交代一个新变化，对 volatile 的对象写 `++`，C++20 起算弃用的写法，GCC 16.2.1 当场提醒过笔者：

```text
warning: ‘++’ expression of ‘volatile’-qualified type is deprecated [-Wvolatile]
```

所以实验里写的是 `v = v + 1`，咱们把读写分开，各算各的。闸门咱们用的是 ready 与 go 两扇门，`ready` 数工作线程报到，`go` 由主线程按一下放行，秒表卡在放行那一刻（为什么是两扇门，调整顺序一节有个病例交代）。

<OnlineCompilerDemo
  title="假共享实测：挤一条行对一人一条行"
  source-path="code/examples/vol5/chapter06/16_exp_g_falsesharing.cpp"
  description="两个计数器各归各的线程写：Packed 版两地址差 8 字节、Slot 版差 64 字节，串行、packed、slot 三组各三轮读数原样列出（本机若把两条线程排在同一处核上，差距会缩小，机制见正文）。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_g_falsesharing.cpp —— 各写各的两个计数器，挤一条行对一人一条行
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <latch>
#include <new>
#include <thread>
#include <vector>

template <class T>
inline void do_not_optimize(T& value) {
  asm volatile("" : "+m,r"(value) : : "memory");
}

constexpr long long kPerThread = 20000000LL;   // 每线程两千万次加一

struct alignas(64) Packed { volatile long long a = 0; volatile long long b = 0; };
struct alignas(64) Slot   { volatile long long v = 0; };

template <class F, class G>
double run_two(F&& body1, G&& body2) {
  using clock = std::chrono::steady_clock;
  std::latch ready{2};
  std::latch go{1};
  std::thread t1([&] { ready.count_down(); go.wait(); body1(); });
  std::thread t2([&] { ready.count_down(); go.wait(); body2(); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  t1.join();
  t2.join();
  auto t1c = clock::now();
  return std::chrono::duration<double, std::milli>(t1c - t0).count();
}

int main(int argc, char** argv) {
  std::printf("sizeof(Packed)=%zu sizeof(Slot)=%zu\n", sizeof(Packed), sizeof(Slot));
  Packed paddr{};
  std::array<Slot, 2> saddr{};
  std::printf("packed a->b delta=%lld  slot0->slot1 delta=%lld\n",
      (long long)((char*)&paddr.b - (char*)&paddr.a),
      (long long)((char*)&saddr[1].v - (char*)&saddr[0].v));
  bool do_serial = true, do_packed = true, do_slot = true;
  int rounds = 3;
  if (argc > 1) {   // 单发模式留给 perf c2c 一类的工具
    do_serial = do_packed = do_slot = false;
    if (argv[1][0] == 's' && argv[1][1] == 'e') do_serial = true;
    else if (argv[1][0] == 'p') do_packed = true;
    else if (argv[1][0] == 's') do_slot = true;
    if (argc > 2) rounds = std::atoi(argv[2]);
  }
  if (do_serial) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      auto t0 = std::chrono::steady_clock::now();
      for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1;
      for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1;
      auto t1 = std::chrono::steady_clock::now();
      std::printf("serial round=%d %.2fms\n", r,
                  std::chrono::duration<double, std::milli>(t1 - t0).count());
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_packed) {
    for (int r = 0; r < rounds; ++r) {
      Packed packed{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) packed.a = packed.a + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) packed.b = packed.b + 1; });
      std::printf("packed round=%d %.2fms\n", r, ms);
      do_not_optimize(packed.a); do_not_optimize(packed.b);
    }
  }
  if (do_slot) {
    for (int r = 0; r < rounds; ++r) {
      std::array<Slot, 2> slots{};
      double ms = run_two([&] { for (long long i = 0; i < kPerThread; ++i) slots[0].v = slots[0].v + 1; },
                          [&] { for (long long i = 0; i < kPerThread; ++i) slots[1].v = slots[1].v + 1; });
      std::printf("slot   round=%d %.2fms\n", r, ms);
      do_not_optimize(slots[0].v); do_not_optimize(slots[1].v);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```bash
g++ -std=c++20 -O2 -pthread exp_g_falsesharing.cpp -o exp_g_falsesharing
./exp_g_falsesharing
```

```text
sizeof(Packed)=64 sizeof(Slot)=64
packed a->b delta=8  slot0->slot1 delta=64
serial round=0 7.62ms
serial round=1 7.63ms
serial round=2 7.69ms
packed round=0 53.84ms
packed round=1 58.62ms
packed round=2 49.16ms
slot   round=0 4.49ms
slot   round=1 3.72ms
slot   round=2 3.77ms
```

咱们看读数。串行一轮 7.6 毫秒上下，单核把四千万次加一做完了。packed 三轮在 49 到 59 毫秒，比串行慢六七倍，两条核干得比一条核还慢，这就是互相打脸。slot 三轮在 3.7 到 4.5 毫秒，正好是串行的一半左右，两条核真在并行。packed 对 slot，差出 12 到 16 倍。两个版本的内存占用都是 64 字节，差别只在两个计数器隔着多远。

咱们把名字放在一起看就好认了。6.4 的争用版是八条线程抢同一个 `std::atomic`，八条线程真的写同一个变量，那是真共享。本篇的 packed 各写各的变量，共享是假的，代价是真的，假共享（false sharing）的名字说的是共享是假的。两个版本的读数形状一样：6.4 量到周期多 295 倍而指令只多 18%，本篇量到慢十几倍而加法次数一点没多，时间全花在行的来回倒手上。

修法直白：把热路径上并发写的变量隔开，一人一条行。padding 也能干活，垫多少字节得您自己算。还有一层得您知道：Padded 自己的对齐要求仍是 8，行边界落在哪里不由咱们定，一行里挨着谁也不由咱们定，尺寸还得手工算，字段一加就错位，所以咱们干脆用 `alignas` 把对齐要求提到 64，边界落得稳：

<OnlineCompilerDemo
  title="alignas(64) 与手工垫字节：尺寸都对，对齐不同"
  source-path="code/examples/vol5/chapter06/17_slot_vs_padded.cpp"
  description="Slot 与 Padded 的 sizeof 都是 64，alignof 一个是 64、一个是 8：手工垫满能到尺寸，行边界落在哪里仍不由咱们定。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// slot_vs_padded.cpp —— alignas(64) 与手工垫字节：尺寸都对，对齐不同
#include <cstdio>

struct alignas(64) Slot   { long long v = 0; };      // 一人一条行
struct Padded             { long long v = 0; char pad[56]; };   // 手工垫满，也能用

int main() {
  std::printf("sizeof(Slot)=%zu   alignof(Slot)=%zu\n", sizeof(Slot), alignof(Slot));
  std::printf("sizeof(Padded)=%zu alignof(Padded)=%zu\n", sizeof(Padded), alignof(Padded));
  return 0;
}
```

</OnlineCompilerDemo>

对齐的值别手写 64，用标准常量。本机两个编译器都给出 64（GCC 16.2.1 与 clang 23.1.3，2026-11-17 实测，feature 宏 `__cpp_lib_hardware_interference_size` 的值是 201703L）。Clang 起初没实现它（LLVM 的 60174 号追踪单记着这段），如今能用了，笔者来复核，把变化如实记在这里。

<OnlineCompilerDemo
  title="标准常量 hardware_destructive_interference_size 的本机答数"
  source-path="code/examples/vol5/chapter06/18_interference_size.cpp"
  description="C++17 起的最小反假共享间隔，本机 g++ 与 clang++ 都报 64。实现没提供这个常量时打印 constant unavailable。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
#include <cstddef>
#include <cstdio>
#include <new>

int main() {
#ifdef __cpp_lib_hardware_interference_size
  std::printf("%zu\n", std::hardware_destructive_interference_size);
#else
  std::printf("constant unavailable\n");
#endif
}
```

</OnlineCompilerDemo>

空间的代价笔者也交代一句：`alignas(64)` 让一个 8 字节的计数器占一整条行，行里剩下 56 个字节空着。空着也要占，因为邻居会打脸。56 个字节换十几倍的读数，热路径上划算，冷数据就别浪费了。

6.3 交代的四倍加速比，本篇顺手处理了两条候选解释。头一条是领任务的下标，它确实是一条被八条线程抢的缓存行，可块数定在 800 的时候，整场实验只发生八百次 `fetch_add`，两亿次的活里排不进主角，算术摆在这里。第二条是调度把八条线程摆上同一条物理核的超线程兄弟，这里检验的只是调度的一种具体摆法，咱们查一下机器就知道兄弟怎么配对：

```bash
cat /sys/devices/system/cpu/cpu0/topology/thread_siblings_list
```

```text
0-1
```

本机的兄弟成对出现，0-1、2-3，偶数号是物理核，奇数号是它的兄弟。咱们绑一把，每条物理核只放一条线程。程序就是 6.3 的动态领活求和（块数同样定在 800），装进本篇同款的 ready 与 go 闸门，编成 `exp_i_pinsum`：

```bash
g++ -std=c++20 -O2 -pthread exp_i_pinsum.cpp -o exp_i_pinsum
./exp_i_pinsum                                 # 没绑的跑一遍
taskset -c 0,2,4,6,8,10,12,14 ./exp_i_pinsum   # 再绑到物理核上跑一遍
```

```text
-- unpinned --
round=0 16.26ms total=599999994
round=1 15.94ms total=599999994
round=2 15.61ms total=599999994
-- pinned --
round=0 15.79ms total=599999994
round=1 16.92ms total=599999994
round=2 17.92ms total=599999994
```

两段读数里的 unpinned 与 pinned 标签是笔者加的，程序自己只报 round。没绑的三轮在 15.6 到 16.3 毫秒，绑过的三轮在 15.8 到 17.9 毫秒，没抬起来，抖动里还略差一点。读数不动，候选解释在笔者机器上就划掉一条。剩下的清单还挂着：频率分摊，以及绑核对照没覆盖的调度部分，笔者没有验证（6.3 交代过笔者没关频率调节）。跨时段的读数只比量级：6.4 粒度实验的 800 块档里，同一份活在另一个时段跑出过 17 到 19 毫秒，跟本篇的数字放在一起，您看量级就好。

## 三种病，读数长什么样

锁竞争、任务过细、共享过多，是本教程卷五给三类读数形状起的叫法。叫法归叫法，读数得是您自己的。三样病在墙钟、单次同步的价钱与缓存计数上各有形状，认出形状，才知道该动哪里。

### 锁竞争：等待长在锁上

咱们把总量固定下来：四十万次加一，跟线程数无关，全落在同一把 `std::mutex` 后面，每次加一都进锁。线程数 1、2、4、8，咱们各跑三轮：

<OnlineCompilerDemo
  title="锁竞争：总量固定，线程越多越慢"
  source-path="code/examples/vol5/chapter06/19_exp_h_fixedtotal.cpp"
  description="四十万次加一全进同一把锁，线程数 1 到 8 各三轮：总耗时从 4ms 一路涨到 17ms 上下，单次进锁从 11 纳秒涨到 44 纳秒。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_h_fixedtotal.cpp —— 总量固定四十万次，每次加一都进同一把锁
#include <chrono>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

long long counter = 0;
std::mutex mu;

constexpr long long kTotal = 400000LL;

void op_lock(int threads) {
  const long long n = kTotal / threads;
  for (long long i = 0; i < n; ++i) {
    std::lock_guard<std::mutex> lk(mu);
    ++counter;
  }
}

template <class F>
double run_gated(int threads, F&& op) {
  using clock = std::chrono::steady_clock;
  std::latch ready{threads};
  std::latch go{1};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { ready.count_down(); go.wait(); op(threads); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  for (int nt : {1, 2, 4, 8}) {
    for (int r = 0; r < 3; ++r) {
      counter = 0;
      double ms = run_gated(nt, op_lock);
      std::printf("threads=%d round=%d %.2fms per_op=%.0fns counter=%lld\n",
                  nt, r, ms, ms * 1e6 / kTotal, counter);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
threads=1 round=0 3.31ms per_op=8ns counter=400000
threads=1 round=1 3.28ms per_op=8ns counter=400000
threads=1 round=2 3.25ms per_op=8ns counter=400000
threads=2 round=0 6.28ms per_op=16ns counter=400000
threads=2 round=1 6.62ms per_op=17ns counter=400000
threads=2 round=2 6.64ms per_op=17ns counter=400000
threads=4 round=0 8.48ms per_op=21ns counter=400000
threads=4 round=1 8.94ms per_op=22ns counter=400000
threads=4 round=2 9.93ms per_op=25ns counter=400000
threads=8 round=0 10.15ms per_op=25ns counter=400000
threads=8 round=1 10.20ms per_op=26ns counter=400000
threads=8 round=2 10.27ms per_op=26ns counter=400000
```

咱们看读数。总量没动，时间从 3.3 毫秒涨到 10.2 毫秒，加人反而慢了三倍。单次进锁的价钱从 8 纳秒涨到 26 纳秒，八条线程把一把锁用出了三倍的价钱。锁把所有人串成一条队，人越多，队尾等得越久。

咱们给同一份总量换一种摆法做对照：每条线程把加法做在自己的局部变量上，收工进一次锁合并。改动就几行：

<OnlineCompilerDemo
  title="锁外合并：同一份总量，几十倍的差距"
  source-path="code/examples/vol5/chapter06/20_exp_h_merge.cpp"
  description="每线程局部累加、收工进一次锁合并，同样 1 到 8 线程：读数落回零点几毫秒，与逐次进锁的十几毫秒差出两个数量级。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// exp_h_merge.cpp —— 同一份总量，每线程局部累加、收工进一次锁合并
#include <chrono>
#include <cstdio>
#include <latch>
#include <mutex>
#include <thread>
#include <vector>

long long counter = 0;
std::mutex mu;

constexpr long long kTotal = 400000LL;

void op_merge(int threads) {
  const long long n = kTotal / threads;
  long long local = 0;
  for (long long i = 0; i < n; ++i) ++local;
  std::lock_guard<std::mutex> lk(mu);
  counter += local;
}

template <class F>
double run_gated(int threads, F&& op) {
  using clock = std::chrono::steady_clock;
  std::latch ready{threads};
  std::latch go{1};
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; ++t)
    pool.emplace_back([&] { ready.count_down(); go.wait(); op(threads); });
  ready.wait();
  auto t0 = clock::now();
  go.count_down();
  for (auto& t : pool) t.join();
  auto t1 = clock::now();
  return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
  for (int nt : {1, 2, 4, 8}) {
    for (int r = 0; r < 3; ++r) {
      counter = 0;
      double ms = run_gated(nt, op_merge);
      std::printf("threads=%d round=%d %.2fms counter=%lld\n", nt, r, ms, counter);
    }
  }
  return 0;
}
```

</OnlineCompilerDemo>

```text
threads=1 round=0 0.09ms counter=400000
threads=2 round=0 0.09ms counter=400000
threads=4 round=0 0.14ms counter=400000
threads=8 round=0 0.15ms counter=400000
threads=8 round=1 0.20ms counter=400000
threads=8 round=2 0.17ms counter=400000
```

咱们把两组读数摆在一起看。同一把锁，同一份总量，逐次进锁的八条线程要 10 毫秒上下，锁外合并只要 0.15 到 0.20 毫秒，差出五十到六十几倍。差出来的就是等待。活在锁外，锁只管合并，等待就没有地方长。

识别的办法，笔者把做过的几件摆成清单：

1. 加线程看墙钟：总量固定，线程加上去读数不降反涨，第一嫌疑人是锁。
2. 算单次同步的价钱：总耗时除以同步总次数。6.3 说过，无竞争的加锁解锁在几十纳秒的量级，读数明显高于它而且随线程数涨，争用就在场。
3. 把活在锁外搬：动一处，复测，读数差多少，等待就值多少。

同一套读法，咱们前面的单元里就用过：2.3 量过一把锁对四把锁，216 毫秒对 152 毫秒（[《锁盖多大才算够：保护范围、粒度与少共享》](../chapter02-sharing-data/03-lock-scope)）。6.4 的争用版差出 108 倍，是它的极端形状。本篇的十毫秒对零点二毫秒，是家常版本。

### 任务过细：协调反客为主

读数咱们在 6.4 已经见过，那里量的是块数：两亿次的求和活，8 条线程，块数从 800 一路加到两千万。读数与条件都在 [《基准工具与三个变量》](04-benchmark-and-variables)的粒度一节里。800 块三轮 17.3 到 19.3 毫秒，8 万块还是 18 毫秒上下，两千万块跳到 257.6 到 262.0 毫秒，慢了 14 倍。每块只剩 10 次加法，每块却要领一次任务，领一次的 `fetch_add` 在争用下比活本身还贵，协调反客为主。

识别的读数形状分两段，平的一段，陡的一段。块数（或者任务大小）从粗往细扫一遍，平的地方说明协调还是零头，陡起来的位置就是交叉点，判据归 6.4，您把陡点附近的数字记下来，往后定粒度就绕着它走。交叉点随活移动，没有统一的块数，量了才知道。

### 共享过多：缓存行上排队

症状您在 6.4 的 perf stat 表里见过：指令只多 18%，周期多 295 倍，IPC 从 4.5 掉到 0.018，缓存未命中从 23.3% 跳到 70.9%。真共享是它。假共享长着同样的读数形状，只是源代码里找不到共享，本篇的 packed 就是样子，逻辑上各写各的，读数照样排队。

识别咱们从数据流下手：谁写，谁读，多久一次。写的人多于一条，就问问能不能各写各的最后合并。merge 是锁上的答案，slot 是缓存行上的答案，两条路一个思路。

perf stat 指认有争用。要把具体的缓存行指认出来，按手册是 `perf c2c` 的活，它把 HITM 最高的缓存行排在报告头一张表里，HITM 记的命中，落在别的核改过、本核还在读的行上（手册在参考资源）。笔者得跟您如实交代：命令在笔者这里没能跑通，perf 报了 no PMU supports the memory events，PMU 是处理器里记周期与缓存事件的计数单元，WSL2 不给它内存延迟事件。

```bash
perf c2c record -o c2c.data -- ./exp_g_falsesharing packed 2
```

```text
failed: no PMU supports the memory events
```

环境给不了，笔者照 6.2 对 GDB 的处理如实注明，不编读数。方法您在自己的 Linux 上跑，报告里 HITM 高的行就是嫌疑人。

## 调整的顺序：量在前，改在后

顺序是三步。头一步量出瓶颈，咱们把可疑的段落配上对照，条件写全，原始读数留档。第二步对症下药，读数指认病症，一次只动一处。第三步复测确认，同一条件再量一遍，对照的是动之前的原始读数，别拿记忆里的应该多快当基准。

咱们拿本篇的 packed 走一遍全程。量出来的读数，packed 对一人一条行的 slot 版慢十几倍，比串行还慢六七倍。指认的病症，是源代码里没有一处共享，读数却像真共享，嫌疑落在数据布局，一查地址，两个计数器只差 8 字节，同一条行。动的一处，是给计数器套上 `alignas(64)`，改动就一个类型声明。复测落在 3.7 到 4.5 毫秒，十几倍找了回来。全程没动锁，没动线程数，因为读数没有指向它们。

顺序里还有一条容易漏的：读数反常，头一个嫌疑人是测量代码。本篇的架子让笔者吃过一回苦头。握手实验里闸门被笔者写成了 `std::latch go{2}`，却配了三只扣门的手，两条工作线程各扣一下，主线程又扣一下。多数时候，主线程扣掉第一下，某条工作线程扣开第二下，晚到的工作线程再来扣的时候，计数已经过了零，`wait` 里看见的是负数，不是零，它就永远等下去。四千万次加一的活，收工本是毫秒级的事，join 却十来分钟没有回来。改法就是您在上面源码里看到的两扇门，`ready` 数报到，`go` 管放行，谁扣谁的门。挂死与复测是两回实验。挂死的一次跑的是四千万次全量活，修好闸门之后，另起二十万次加一的握手，单独量的是闸门与握手的开销，读数 0.29 毫秒。架子错了，后面每一步调整都是在给假读数动手术，所以自查架子排在所有步骤前面。

直觉常常喊错方向。慢十几倍的程序，直觉喊您去加锁、去换无锁结构，读数指向的却是缓存行。方向拧了，力气都白费。6.4 留过一句动哪一处、查缓存行还是动锁，读数说了算，到本篇落成了上面那三步。

## 轮到您动手的时候

1. 把您程序里最贵的一段挑出来，配上串行对照，同组编译选项量三轮起步，原始读数连同机器、核数、日期写进记录，6.3 的做法照搬。
2. 线程数与块数各扫一遍，读数拐弯的位置记下来，拐弯的地方就是协调成本的交叉点。
3. 读数反常的时候，把 perf stat 的周期、IPC 与缓存未命中拉出来（事件清单见 6.4），分清等待与访存。
4. 按读数指认病症，一次只动一处，复测对表，读数不动就把改动退回去。
5. 架子自证：闸门的配数、计时的边界、防优化函数，都过一遍眼，别让测量代码替您拿读数。

本篇把读数接过去，变成缓存行的排布、锁的范围与任务的粒度。等待的另一半问题归第七单元的协程：线程等待的时候整条核闲着，协程换一种组织方式，把等待的线程还回去。

## 参考资源

推进思路参考了 Williams《C++ Concurrency in Action》讲性能与调试的章节和 McKenney 的 perfbook，借的是组织与讲法，例子和文字都是笔者自己写的，读数在文中交代的机器上实测。缓存行与 interference 常量的断言，笔者按 cppreference 与 LLVM 的提交核对过，访问日期是 2026-11-17。

- [alignas 说明符（cppreference）][cppref-alignas]，C++11 起，本篇修法用的出处。
- [hardware_destructive_interference_size（cppreference）][cppref-interference]，C++17、`<new>`、值由实现自定与 feature 宏 201703L 的出处。
- [Clang 为 Apple AArch64 采用 128 字节破坏性干扰尺寸][llvm-apple-128]，LLVM 提交 9c8485a2，2026-07-10 合入，Apple 行宽 128 与 GCC AArch64 取 256 的出处。
- [LLVM issue 60174][llvm-60174]，旧目录《CPU cache 与 OS 线程》引过的 Clang 支持追踪单，本机 clang 23.1.3 实测已可用。
- [perf-c2c 手册页（man7）][perf-c2c]，HITM 排表与各架构事件的出处，2026-11-17 查证。
- [perf 教程（perf wiki）][perf-wiki]，perf stat 的用法，6.4 引过，本篇沿用。
- [std::latch（cppreference）][cppref-latch]，C++20 闸门，架子的 ready 与 go 用它。
- [std::atomic（cppreference）][cppref-atomic]，`fetch_add` 与内存序参数的出处。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，性能与调试章节。
- [Is Parallel Programming Hard, And, If So, What Can You Do About It?（perfbook）][perfbook]，Paul E. McKenney 的免费在线书，kernel.org 镜像。

[cppref-alignas]: https://en.cppreference.com/w/cpp/language/alignas
[cppref-interference]: https://en.cppreference.com/w/cpp/thread/hardware_destructive_interference_size
[llvm-apple-128]: https://llvm.googlesource.com/llvm-project/+/9c8485a2c216160bfc5d64a71cbbd298776618bd
[llvm-60174]: https://github.com/llvm/llvm-project/issues/60174
[perf-c2c]: https://man7.org/linux/man-pages/man1/perf-c2c.1.html
[perf-wiki]: https://perf.wiki.kernel.org/index.php/Tutorial#Counting_with_perf_stat
[cppref-latch]: https://en.cppreference.com/w/cpp/thread/latch
[cppref-atomic]: https://en.cppreference.com/w/cpp/atomic/atomic
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[perfbook]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
