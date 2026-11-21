---
title: “内存序的四档：从顺序一致性到 relaxed”
description: “从 seq_cst 的全体次序到 relaxed 的只保原子，中间的 release 与 acquire、acq_rel 与被弃用的 consume 逐档交代保什么、不保什么、各花多少。”
chapter: 5
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20, 26]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 内存序的四档：从顺序一致性到 relaxed

咱们把立场摆在前面：给判据，不给背答案。六档参数背下来，换一个场景照样发懵。判据只有一座，就是 5.3 立起来的那座桥。您要检查的读写之间需要桥，内存序的任务就是把桥的两端与桥上的货安排好，挑哪一档，看桥要连什么。

实验的读数交代一句：litmus 的三档笔者各跑了两遍，读数都写在正文里，多线程读数都是单次运行的读数。

## 从顺序一致性出发

六档里最贵也最好推理的是默认档，咱们从它出发。seq_cst 让读带上 acquire、写带上 release、读改写两头都占，在这三样之上还叠了一条：全部 seq_cst 操作排成一条全体的次序，所有线程看到的是同一条（[atomics.order] 第 4 段）。白话就是，全局的 seq_cst 动作排在一条线上，谁在谁前面，人人看法一致。

全序的用处，咱们拿 5.2 留下的一个最短例子看：线程一做 `x.store(1)` 接着 `y.store(2)`，线程二读 y 再读 x。把读放松到 relaxed，线程二可能读到 y 是 2、x 是 0，两个变量各自完整，合起来却对不上。换成 seq_cst，挡住它的是总序。5.2 只立了定性，5.3 的 litmus 把它量了出来，一千万轮零命中。

草案在注里补了一句提醒，笔者原样转述（[atomics.order] 第 7 段的注）：seq_cst 只对没有 data race、并且全部原子操作都用 seq_cst 的程序保证顺序一致，掺进任何更弱的用法，保证就作废，除非格外小心。笔者把它读成这样：默认档推理省力，是因为它替您把所有原子变量的次序一起管了。您一降档，管住的那部分就得自己接管。5.3 讲强发生时留给本篇的那句，门槛就设在桥两端的 seq_cst 上，看过总序再看下一节的配对桥，门槛为什么立在那儿就清楚了。

什么时候留在默认档？笔者的判据有两条，占一条就留着。您手里的几个原子变量互相咬合、谁也离不开谁的次序，这时候 seq_cst 是标准里唯一给全序的档。或者您拿着代码数不出桥、说不清哪一对读写要接起来，留在默认档是给自己留退路，保住对，再谈省。

## release 与 acquire：配对的地方搭桥

中间的档位，咱们从 release 与 acquire 讲起。release 用在写上：它前面的读写，不许越过它挪到后面。acquire 用在读上：它后面的读写，不许提到它前面。读的一侧拿到 release 写下的值时，两边 synchronizes-with，桥从配对处接通（[atomics.order] 第 2 段），写端在 release 之前的全部操作，对读端在 acquire 之后的操作可见。

握手只需要两个线程之间的动作对接上，全局总序在架子上是白花的钱。配对处搭桥的档正好够用，这就是 acquire 与 release 的位置，只在需要桥的地方付桥的钱。

光说不够，咱们量一把。笔者把 5.3 的 litmus 原架搬来，动作对单独装进一个函数，内存序做成编译期三选一，relaxed 一档，release 配 acquire 一档，seq_cst 一档。

<OnlineCompilerDemo
  title="litmus 三档：relaxed、release 配 acquire、seq_cst"
  source-path="code/examples/vol5/chapter05/13_litmus_acqrel.cpp"
  description="内存序编译期三选一，默认 -DORDER_MODE=0 是 relaxed 档，命中数万次。换成 1 或 2 重编（命令与读数见下方）。"
  run-options="-O2 -std=c++20 -pthread -DORDER_MODE=0"
  allow-run
>

```cpp
// litmus_acqrel.cpp —— 两条线程互测 x、y 两端的读写次序，内存序编译期三选一
// 架子与 5.3 的 litmus 相同：放行哨兵、应答只增不减、重置压在放行之前、绑核 0 与 8
// 用法: g++ -std=c++20 -O2 -pthread -DORDER_MODE=0|1|2 litmus_acqrel.cpp
// 0 = relaxed   1 = release/acquire   2 = seq_cst
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <thread>

#if ORDER_MODE == 0
#define SORD std::memory_order_relaxed
#define LORD std::memory_order_relaxed
#define NAME "relaxed"
#elif ORDER_MODE == 1
#define SORD std::memory_order_release
#define LORD std::memory_order_acquire
#define NAME "release/acquire"
#elif ORDER_MODE == 2
#define SORD std::memory_order_seq_cst
#define LORD std::memory_order_seq_cst
#define NAME "seq_cst"
#endif

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

// 动作对单独装一个函数，objdump 对照三版指令时一眼可见
__attribute__((noinline)) int probe(std::atomic<int>& w, std::atomic<int>& r) {
  w.store(1, SORD);
  return r.load(LORD);
}

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 放行哨兵，主线程每轮推一号，唯一的起跑口
  std::atomic<long> ack{0};     // 应答，2r 表示读到 1，2r+1 表示读到 0，只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 绑核接口（_np，不可移植），固定实验条件用
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(x, y);              // 写 x，读 y
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      int got = probe(y, x);              // 写 y，读 x
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮应答收齐才开下一轮，重置压在放行之前
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;   // 两边同时读到旧值 0 才记命中
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n", NAME, hits, kTrials);
}
```

</OnlineCompilerDemo>

读数摆在下面，咱们三档各跑了两遍，都是单次运行的读数：

```text
relaxed: 89258 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 60782 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0

relaxed: 63698 of 10000000 rounds saw r1=0 and r2=0
release/acquire: 70389 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0
```

咱们把读数并排一摆，中间档与全松档同量级，每一千万轮命中六到九万次，全序档照旧零次。机器码把原因摆得很直，三档的 probe 函数并排看（objdump 输出，地址列截掉）：

```text
relaxed:         movl $0x1,(%rdi)     ← 写
                 mov  (%rsi),%eax     ← 读

release/acquire: movl $0x1,(%rdi)     ← 写，与 relaxed 一条不差
                 mov  (%rsi),%eax     ← 读

seq_cst:         mov  $0x1,%eax
                 xchg %eax,(%rdi)     ← 写换成 xchg，当场生效
                 mov  (%rsi),%eax     ← 读
```

咱们看指令。release 与 acquire 在 x86 上编出来的与 relaxed 完全相同，编译器只多拦了一类编译期的重排。litmus 测的场面是 x 与 y 两个变量互相咬合，两边同时读到旧值需要的正是全局次序，acquire 与 release 不设全局次序，读数自然落在全松档的量级。您要零命中，就得付 `xchg` 的钱。

5.2 的欠单咱们也在这里清。`atomic_flag` 的 `test_and_set` 带 acquire、`clear` 带 release，2.1 的手工自旋锁用的正是它：拿锁的人，锁住之后的读写不许提到 `test_and_set` 前面。放锁的人，临界区里的写压在 `clear` 前面。桥的形状与 5.3 数过的 `std::mutex` 一模一样，只是零件从锁换成原子标志。2.1 与 5.2 代码里的几处 acquire 与 release，加上 5.3 同步关系表里的最后一行，判法咱们已经熟了：桥的两端是不是它们，读的一侧拿到写的一侧的值没有。

还有一个细节值得立住。读的一侧拿到的不一定是 release 写本身，排它后面、任何人做的读改写也算数，标准管它叫**释放序列**（release sequence，[atomics.order] 第 2 段原文写的就是“从 A 头部的释放序列里取值”）。释放序列的概念，咱们就在本节立住，往后 cookbook 选读区的《无锁入门》会反复用到它，那里还没成文。

## acq_rel：读改写的两头

咱们把**读改写**交代清（read-modify-write，一次动作里读旧值、算新值、写回去），它同时有一读一写，两头都能接序。acq_rel 让它两头都占：作为读，它是 acquire，接住上游的桥。作为写，它是 release，开下游的桥。cppreference 的常量表把两句话并排写着，标准原文在 [atomics.order] 第 1 段，acquire 与 release 的清单里都点着它。

判据还是数桥，只是要数两头。您写下一个 `fetch_sub` 的时候问两件事：它的读在等谁的发布，它的写要发布给谁。两头都接链，用 acq_rel。只接一头，挑 acquire 或者 release。两头都不接，relaxed。引用计数的减法是两头都接的样板，5.5 的引用计数一节展开。

成本给个底：x86 上读改写不管什么内存序都编成同一条 `lock xadd`，指令对照在后面的表里。差距要到弱内存模型的机器上才显出来，那边的读数本篇没法给，笔者不装作测过。

## relaxed：只保原子性的场面

relaxed 的承诺清单最短，值钱的也正在这里，咱们一条一条看。头一件，动作完整，别的线程看不见半成品。再一件，同一个变量的修改，全体线程按同一条次序看。标准管它叫**修改次序**，四条一致性要求对全部原子操作生效，relaxed 也在受益的名单里。它不给的是桥，一节都不搭。

咱们拿计数器量一遍它的底：

<OnlineCompilerDemo
  title="relaxed 计数器：只保原子性也分毫不差"
  source-path="code/examples/vol5/chapter05/14_counter_relaxed.cpp"
  description="四条线程各加 100 万次，relaxed 读数 4000000。数值本身就是目的，旁边没有货要过桥，这就是它的用场。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// counter_relaxed.cpp —— relaxed 只保原子性：四条线程各加 100 万次
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<long long> served{0};

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    served.fetch_add(1, std::memory_order_relaxed);   // 只保原子性，不搭桥
  }
}

int main() {
  std::thread ts[4];
  for (int i = 0; i < 4; ++i) ts[i] = std::thread(work, 1000000);
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次，relaxed 读数 %lld\n", served.load(std::memory_order_relaxed));
}
```

</OnlineCompilerDemo>

```text
4 条线程各加 100 万次，relaxed 读数 4000000
```

四条线程各加一百万次，分毫不差，relaxed 的底咱们量完了。它适合的场面也清楚了：数值本身是目的，它旁边没有别人等着看的货。统计计数器、只报进度的任务数、引用计数的增，都落在这里。cppreference 在 relaxed 的小节里举过例子，说典型用法是递增计数器，比如 `std::shared_ptr` 的引用计数，同页紧接着提醒，减引用要 acquire-release 来配合析构，待遇确实不一样，判据在 5.5 的引用计数一节数。

1.6 讲停止标志时提过的 relaxed，5.3 数出桥一节不搭，咱们把边界在这里立清楚：标志本身用 relaxed 不会碎，动作照样完整。碎的是它旁边该一起可见的数据，看到停止的人往往还要读配置，配置不在桥上，看到的就可能是旧值。修法是加桥，咱们在 5.5 的停止标志一节给，本篇的 demo 就按修法写，relaxed 的对照组留给 5.5 篇末的动手题。

## consume 的现状：被弃用的依赖序（扩展）

本节带（扩展）标记，跳过它不影响主线。您只要带走一件事：别写 `memory_order_consume`。

consume 当初的算盘比 acquire 精细，咱们把它摆开看。acquire 是全量订阅，读出来之后的全部读写不许提前。consume 只想约束依赖读出值的操作。指针发布是它设想的主场：读者拿到指针，顺着指针访问的数据受排序保护，与指针无关的访问不受约束，理论上一分冤枉钱都不花。

现实没有配合，咱们直说。要兑现依赖序，编译器得沿整个函数追踪值的流向，变量一换名、值一进寄存器，依赖链就断在半路，链断了排序就得放大，不然就出错。没有主流编译器实现它的精确语义，实际都按 acquire 的强度来跑 consume 的名字。标准委员会的处置提案编号 P3475R2，标题里的动词很直白，defang and deprecate，把语义拔掉，再标成废弃。

现状笔者按日子记录，一个来源一句。cppreference 的条目表笔者 2026-11-16 抓过，它给 `memory_order_consume` 标了 deprecated in C++26。配套的 `kill_dependency` 同批废弃。两个正式定义，携带依赖与依赖排序在前，同一个表里都标了 until C++26。eel.is 的现行工作草案笔者也抓了，[atomics.order] 的枚举只剩五个值，relaxed、acquire、release、acq_rel、seq_cst，consume 已经不在里面。libstdc++ 在 2026 年 5 月合入了对应的弃用补丁。往后哪个版本彻底删掉名字，笔者还没查到定论，您读到的时候值得再核一眼。

判据一句话：您想表达“读到指针再用它”，acquire 就是现在的写法，语义只强不弱，在所有编译器上行为一致。代码库里要是还留着 consume，换成 acquire。

## 参考资源

实验的读数交代一遍。litmus 三档笔者各跑了两遍，都是单次运行的读数，更早一轮预跑的同为三档（relaxed、release 配 acquire、seq_cst），读数量级相同，seq_cst 全部零命中。counter_relaxed 的 4000000 每次运行读数一致。指令对照来自 objdump，地址列截掉。断言核对用的 eel.is 草案是 2026-11-16 抓的，正文条款名都带着，段号随草案版本会挪。[atomics.order] 核了枚举、release 与 acquire 的同步关系、释放序列、seq_cst 总序与掺弱作废的注。consume 的弃用状态核对过两处，一处是 cppreference 条目表的 C++26 标记，另一处是 libstdc++ 的合入记录。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，取的是单元排布与例子设计的思路，例子和文字都是笔者自己写的。

- [std::memory_order（cppreference）][cppref-mo]，六档的常量表、consume 的 C++26 弃用标记、relaxed 的典型用法。
- [现行标准草案：Order and consistency（atomics.order）][draft-atomics-order]，五档枚举、同步关系、释放序列、seq_cst 总序与掺弱作废的注。
- [libstdc++ 弃用 memory_order_consume 的补丁（GCC 邮件列表）][gcc-consume]，2026 年 5 月合入。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-mo]: https://en.cppreference.com/w/cpp/atomic/memory_order
[draft-atomics-order]: https://eel.is/c++draft/atomics.order
[gcc-consume]: https://gcc.gnu.org/pipermail/gcc-patches/2026-May/718127.html
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
