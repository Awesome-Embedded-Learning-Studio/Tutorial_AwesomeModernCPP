---
title: “一个线程怎样观察另一个线程的修改”
description: “从编译器与 CPU 的重排走到缓存一致性，把 happens-before 讲成可以逐节检查的桥，再拿它把前面的锁、join 与条件变量重新证明一遍。”
chapter: 5
order: 2
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

# 一个线程怎样观察另一个线程的修改

[《原子操作解决到哪一步》](01-what-atomics-solve)收尾的地方，咱们已经把单次操作交给 `std::atomic`：一次读、一次写、一次读改写，每步完整，别的线程看不见半成品。丢数的计数器换成 `fetch_add` 那样的读改写，加一的完整性就有了着落。

可是只靠“每步完整”不够。两条线程一个写一个读，写在前、读在后，读的一侧凭什么一定看见新值？第二单元判定 data race 的时候（[《从错误程序认识竞争》](../chapter02-sharing-data/01-from-wrong-programs)），四件事里有一件，我欠到了现在：两条动作之间，谁也没有 happens-before 谁。happens-before 是标准里管“谁在谁之前”的关系，本篇把它讲成可以逐节检查的桥：它由什么搭起来，怎么查，查完之后，前面用过的锁、join 和条件变量都能重新证明一遍。

内存序的挑选归下一篇 5.3《[选择内存序需要什么依据](03-choosing-memory-orders)》。咱们测的是 `x`、`y` 两端的读写次序，两端分别配 relaxed 与 seq_cst，一个不搭桥，一个全加满。中间的档位留给 5.3，本篇摆出来的是两端。

实验都在临时目录里编译跑过（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`，WSL2，16 逻辑核，2026-11-16）。多线程的读数都是单次运行的读数。litmus 实验我跑了两遍，锁住的计数器与死循环程序的 `-O0` 档也各跑了两遍，读数都写进正文。标准条款按 eel.is 上的现行草案核对，段号随草案版本会挪，引用时条款名都带着。

## 为什么会重排：编译器与 CPU

咱们从编译器看起。下面的程序只干一件事：工作线程反复查看一个 `bool`，主线程过 200 毫秒把它改成 `true`。

```cpp
#include <chrono>
#include <cstdio>
#include <thread>

bool stop = false;   // 故意不用 std::atomic<bool>，本节只做演示

int main() {
  long long spins = 0;
  std::thread t([&] {
    while (!stop) ++spins;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop = true;
  t.join();
  std::printf("main set stop, thread saw it, spins=%lld\n", spins);
}
```

`-O0` 编出来，程序正常收场。我跑了两遍，工作线程分别转了三亿五千多万与五亿二千万圈，都看见 `stop` 翻转，打印一行退出。同一份源码换 `-O2`，工作线程出不来了。我用 `timeout 2` 跑，两秒后进程被杀掉，退出码 124，`spins` 一行始终没出现。

机器码把话说得很直。`-O2` 下工作线程的整个函数是（`objdump` 的输出，我截掉了地址列与符号修饰）：

```text
cmpb $0x0,0x2d0a(%rip)   # 4071 <stop>    ← stop 只在这里读一次
jne  1378                                 ← 已经是 true，函数返回
jmp  1370                                 ← 否则跳回自己，循环体整个没了
```

编译器把读挪出了循环，顺带把 `++spins` 也删了个干净。在它眼里，循环不停，`spins` 的值就没人能读到。循环若停，说明入口处 `stop` 已是 `true`，加多少次都白加。它没骗人。标准要求实现模拟的只是抽象机的可观察行为（[intro.abstract] 第 1 段，脚注给它起了名字，叫 as-if 规则），单线程看不见的变换，它有权做。多线程一上来，您马上看得见：工作线程再也不看 `stop` 了。

公道话也得补一句。它本身就是一条 data race，标准对它已经不提任何要求（[intro.races] 第 17 段），编译器按“没有别的线程在写 stop”往下推，推得完全合法。您拿它当反面教材就好，别拿它当写法。

咱们再看 CPU 一侧的换序，它更隐蔽。现代处理器同时执行着几十条指令，谁的操作数早到，谁就走。写还有更扎眼的去处：写指令到了核上，值不会马上让别的核看见，它落在写缓冲区（store buffer，每个核自带的、存放尚未对外生效的写的小队列）里排队。压在缓冲区里的写没有生效，同一核后发的读却可以越过它走，读的人自己不知道前面还有一笔没生效的写。

x86-64 的内存模型常被业界称作 TSO（Total Store Order，全存储排序）：写与写的次序保住，读与读的次序保住，放行一种穿越，即自己后发的读越过自己排队的写。ARM 一类弱内存模型的机器上，四种次序都有得翻。同一份代码在两边跑出不同读数，这就是标准要自立一套内存模型的原因：语言得给出一套与平台无关的承诺，咱们不能把正确性押在某家硬件的脾气上。

光讲道理不够，咱们真测一次。两个原子变量 `x` 与 `y`，都从 0 出发。线程 a 写 `x`，接着读 `y`，记下读到的数。线程 b 对 `y` 和 `x` 做镜像的一对。两边各自“写在前、读在后”都定死了，测的是：有没有可能两边同时读到对方的旧值 0。这类拿两条线程互测读写次序的实验，内存模型圈叫它 litmus test，咱们文件就叫 litmus.cpp。

完整程序在两处费了笔墨，我交代一下。起跑对齐是一处：两条线程得在同一瞬出发，我用一个逐轮递增的放行哨兵，让它们自旋等同一轮再动手。结算与重置的次序是另一处：主线程收齐双方的应答才开下一轮，每一轮的重置压在放行之前。不是随便写的，后面有一段排查会回到这里。架子自身的握手也借原子操作搭桥，用 release 与 acquire 配对，后文讲 happens-before 的表里，最后一行就是它们。握手不是要测的对象，测的始终是 `x`、`y` 两端。内存序本身怎么挑，归 5.3。

```cpp
#include <atomic>
#include <cstdio>
#include <pthread.h>
#include <sched.h>
#include <thread>

#ifndef USE_RELAXED
#define USE_RELAXED 0
#endif
constexpr bool kRelaxed = USE_RELAXED;

struct alignas(64) Slot { std::atomic<int> v{0}; };   // x、y 各住一条缓存行
static Slot sx, sy;
static std::atomic<int>& x = sx.v;
static std::atomic<int>& y = sy.v;

struct alignas(64) Hand {
  std::atomic<long> round{1};   // 主线程每轮推一号，是唯一放行口
  std::atomic<long> ack{0};     // 回 2r（读到 1）或 2r+1（读到 0），只增不减
};
static Hand ha, hb;

static void pin(std::thread& t, int cpu) {   // glibc 的绑核接口（_np，不可移植），拿来固定实验条件
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  pthread_setaffinity_np(t.native_handle(), sizeof(set), &set);
}

int main() {
  const auto ord = kRelaxed ? std::memory_order_relaxed
                            : std::memory_order_seq_cst;
  const long kTrials = 10000000;
  long hits = 0;

  std::thread ta([&] {
    for (long r = 1; r <= kTrials; ) {
      if (ha.round.load(std::memory_order_relaxed) < r) continue;
      x.store(1, ord);
      int got = y.load(ord);
      ha.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });
  std::thread tb([&] {
    for (long r = 1; r <= kTrials; ) {
      if (hb.round.load(std::memory_order_relaxed) < r) continue;
      y.store(1, ord);
      int got = x.load(ord);
      hb.ack.store(2 * r + (got == 0 ? 1 : 0), std::memory_order_release);
      ++r;
    }
  });

  pin(ta, 0);   // 0 号与 8 号逻辑核，本机拓扑分属物理核 0 与 4
  pin(tb, 8);

  for (long r = 1; r <= kTrials; ++r) {
    if (r > 1) {   // 上一轮的应答收讫，才轮到重置与放行
      while (ha.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
      while (hb.ack.load(std::memory_order_acquire) < 2 * (r - 1)) {}
    }
    x.store(0, std::memory_order_relaxed);
    y.store(0, std::memory_order_relaxed);
    ha.round.store(r, std::memory_order_seq_cst);   // 放行，顺带把重置压出去
    hb.round.store(r, std::memory_order_seq_cst);
    long aa = 0, ba = 0;
    while ((aa = ha.ack.load(std::memory_order_acquire)) < 2 * r) {}
    while ((ba = hb.ack.load(std::memory_order_acquire)) < 2 * r) {}
    if ((aa & 1) && (ba & 1)) ++hits;
  }

  ta.join();
  tb.join();
  std::printf("%s: %ld of %ld rounds saw r1=0 and r2=0\n",
              kRelaxed ? "relaxed" : "seq_cst", hits, kTrials);
}
```

两种编译命令给您，内存序是编译期选的：

```bash
g++ -std=c++20 -O2 -pthread -DUSE_RELAXED=1 litmus.cpp -o litmus_relaxed
g++ -std=c++20 -O2 -pthread -DUSE_RELAXED=0 litmus.cpp -o litmus_seqcst
```

输出里的 r1，是线程 a 读 `y` 的读数，r2 是线程 b 读 `x` 的读数，两边同时为 0 才记一次命中。读数在下面，我各跑了两遍，都是单次运行的读数：

```text
relaxed: 88260 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0

relaxed: 89054 of 10000000 rounds saw r1=0 and r2=0
seq_cst: 0 of 10000000 rounds saw r1=0 and r2=0
```

relaxed 组每一万轮里有几十上百次两边同时读到旧值。正式的两遍落在 88 与 89，后来复跑几次，落在 88 到 137 之间，量级不变。咱们把读数放到机器上看，形状正合适：两条线程的写各自压在自己的写缓冲区里没有生效，两个读各自越过了自己排队的写，把对方的 0 捞了回来。TSO 放行的一种穿越，测出来的正是它。seq_cst 组整整一千万轮零次。这不是运气，编译器把 seq_cst 的写编成了另一条指令，两种编译摆在一起看（`objdump` 输出节选，线程 b 的循环）：

```text
// relaxed：写是一条普通 mov，随后直接读另一个变量
movl   $0x1,0x2c2d(%rip)        # sy     ← 写
mov    0x2c65(%rip),%ecx        # sx     ← 读

// seq_cst：写换成 xchg，自带全屏障
xchg   %esi,0x2c2c(%rip)        # sy     ← 写，当场生效
mov    0x2c66(%rip),%edx        # sx     ← 读
```

`xchg` 在 x86 上自带全屏障，写不等缓冲区，当场对全体核生效，随后的读自然越不过去。咱们把两端读数放在一起看，顺序不是时间白送的，要有人花指令把它立起来。

读数敢这样摆出来，中间有一段排查想留给您。头几版架子里，seq_cst 组也报过命中，少则十几万，多则几百万，一轮一个样。`xchg` 的全屏障把写当场生效，前面的指令形态刚看过，两边同时读到 0，在本机的指令形态下摆不出来，读数与指令形态顶了牛，毛病只能出在我的架子上。语言层还有一条更普适的道理，全部 seq_cst 操作存在单一总序（[atomics.order] 第 4 段），本篇立一句定性就够，细讲归 5.3。翻出来一看：上一轮末尾放行的口子，赶在下一轮重置之前就生效了，线程读到的 0 可能是主线程后提交的重置，根本不是“对方没写”。把放行收成唯一出口，把重置压在放行之前，假阳性就消失了。读数和模型对不上的时候，值得怀疑的是架子，不是模型。

绑核也交代一句。两条线程我固定在 0 号与 8 号逻辑核上（glibc 的 `pthread_setaffinity_np`，名字带 _np，不是可移植接口，拿来固定实验条件用）。本机是 AMD 的 8 核 16 线程，逻辑核成对枚举，`/sys/devices/system/cpu/cpu0/topology/thread_siblings_list` 读出来是 0-1，cpu8 读出来是 8-9，0 号与 8 号分属物理核 0 与 4。您绑核之前，值得把自己机器上 /sys 里的 sibling 信息也读一眼。同一物理核的两个逻辑核共用一套写缓冲区，绑开是为了把核间的窗口测准。

## 缓存一致性背景：MESI

前面的实验里，两条线程读写的是两个分开存放的原子变量。要让“写会最终被看见”成立，底下还得有一层东西在管，就是各核的缓存与它们之间的一致性协议。本节是硬件背景的白话版，协议状态机的细节各家实现有出入，咱们只取主干。

每核都有自己私有的缓存，数据按缓存行搬动，x86-64 上常见的一条是 64 字节。核改一个数，改的是自己缓存里的副本，别的核手里攥着的还是旧副本。听上去各自为政，其实乱不了：缓存行之间有一套一致性协议，最常见的一类叫 MESI。名字来自四个状态的首字母，Modified、Exclusive、Shared、Invalid。四个状态的白话版，咱们过一遍：

| 状态 | 白话 | 顺带的规则 |
|------|------|-----------|
| Modified（已改） | 我刚写过，全机最新的值在我这里，内存里是旧值 | 只有我拿着，别人要读最新的得来找我 |
| Exclusive（独占） | 只有我缓存了它，与内存一致 | 想写随时写，写完升级成 Modified |
| Shared（共享） | 可能几个核都有，都与内存一致 | 谁想写，谁把别人的份作废 |
| Invalid（无效） | 手里的份作废，等于没有 | 想读写，重新要 |

写一件事的流程，咱们白话走一遍：想写的核发出作废消息，等收齐回执，自己升级成 Modified，然后落笔。此后谁要读它，只要手里没有最新的，就得从 Modified 的核那里拿。所以“写会最终可见”有协议明文管着：同一个地址的写，全体核看到同一条次序，谁也读不到跳过某次写的值。

标准自己也点了名：内存模型的条款底下有一条注，说一致性规则把多数硬件提供的缓存一致性保证，交到了 C++ 原子操作手里（[intro.races] 第 15 段的注）。咱们能看到，语言层与硬件层在可见性上是对齐的。

可见归可见，及时是另一回事。写压在自己核的写缓冲区里时，别的核什么都看不见。作废消息在路上飞，回执也要等。litmus 的 relaxed 组每万轮几十上百次，量的就是窗口的宽度。协议答应的是“迟早一致”，没答应“下一瞬就到”，您往后测并发程序的性能，两者的区别会一而再地冒出来。

还差一步要分清。一致性管的是同一地址（同一缓存行）之内读值的次序，它不替您约束两个不同地址之间谁在前、谁在后。litmus 恰好是证明：`x` 与 `y` 的写都“最终可见”了，两边观察到的顺序照样对不上。跨地址的次序，硬件不送，得往语言层要，要的地方就是下一节。

## happens-before：由同步建立的顺序

从同一线程内说起。标准给单线程内部立了次序，叫 sequenced-before（[intro.execution] 第 8 段）：同一条线程里求值 A sequenced-before 求值 B，A 就得在 B 之前完成。它非对称、可传递，是“程序顺序”里被语言真正保证的部分。您写 `data = 42; ready = true;`，两句之间就是它。

跨线程的桥叫 synchronizes-with（同步关系）。特定的库调用成对出现时，一边与另一边建立同步关系（[intro.races] 第 6 段）。搭桥的场合标准数得过来，咱们本篇用得上的有四类：

| 搭桥的动作 | 与谁建立同步关系 | 条款 |
|------------|------------------|------|
| 放锁与拿锁配对：`m.unlock()` 与同一把锁随后成功的 `m.lock()` | 前一次放锁与该次拿锁之间 | [thread.mutex.requirements.mutex]/8、/24 |
| `std::thread` 构造完成 | 新线程函数的开头 | [thread.thread.constr]/7 |
| 线程结束 | 成功的 `join()` 返回 | [thread.thread.member]/4 |
| release 写 | 读到它值的 acquire 读 | [atomics.order]/2 |

咱们把两者接起来，得到总关系 happens-before（[intro.races] 第 7 段）：A happens-before B，当且仅当 A sequenced-before B，或者 A synchronizes-with B，或者存在中间的 X，A happens-before X，X happens-before B。传递性管的就是这个，桥可以一座接一座地搭，只要能一路连过去，头与尾就有关系。

它的用处落在两条里，咱们分开看。可见性：写 A happens-before 读 B，中间没有别的写插队，B 读到的就是 A 写的值（[intro.races] 第 9 段，管它叫可见的副作用）。判定：两条可能并发的冲突动作，谁也没有 happens-before 谁，而且其中至少一条是非原子操作，这时执行就包含 data race，后果是未定义行为（第 17 段）。第二单元引过判定的后半句。

白话判据，咱们检查代码时就按它问。一对可能并发的读写之间，您要能在代码里指出完整的桥：写 sequenced-before 放锁（或者 release 写），放锁 synchronizes-with 对方拿锁（或者 acquire 读），拿锁 sequenced-before 读。三节连成一条，缺哪节都不行。反过来，数不出桥就别赌：要么补同步，要么该程序已经不在标准的管辖之内，跑对只算样本。

relaxed 呢，咱们在 litmus 里已经见过它的样子：每步完整，桥一节都不搭，两边读 0 读得整整齐齐。条款给它的定义是“不安排任何内存次序”（[atomics.order] 第 1 段），不过它仍是原子操作，不产生 data race（[intro.races] 第 3 段的注）。原子加什么样的序才搭桥、搭多大，归 5.3，咱们在那里接着对。[《让工作有序结束》](../chapter01-start-concurrency/05-jthread-and-stopping)讲停止标志时那句 relaxed，连可见性都不保证，咱们按判据数一遍就清楚：标志 relaxed，桥一节不搭。

还有一道许诺要兑现。[《按协作需求选择工具》](../chapter03-wait-and-coordinate/04-choosing-tools)借屏障的完成函数垫过一句“强发生”（strongly happens before），当时说它是内存模型的正式关系，第五单元专门讲。咱们现在就在第五单元，把它摆正。强发生是比 happens-before 更紧的一条关系（[intro.races] 第 8 段）：同一线程内部的 sequenced-before，都算强发生。跨线程搭桥的，桥的两端都得是 seq_cst 原子操作才谈得上强发生，或者“同线程的顺序、桥、再同线程的顺序”整段连起来也行。barrier 的完成步，标准立的正是更强的档位：`arrive` 强发生在本轮完成步开始之前（[thread.barrier.class]/14），完成步的结束强发生在被放行线程返回之前（同条款 /3）。3.4 那句白话照旧成立：完成步里写的任何东西，放行之后读都读得到，完成函数读各线程刚写好的数据不用加锁，靠的正是更紧的承诺。强发生与 happens-before 平常差在哪儿？差在桥上 seq_cst 的门槛，门槛后面的事归 5.3。

## 用 happens-before 重新理解前面的锁与条件变量

零件齐了，咱们回头把前面的程序重新证一遍。证法只有一种：数桥。

### 锁住的计数器为什么 30 轮不丢

[《用互斥保护共享状态》](../chapter02-sharing-data/02-mutex-and-locks)把丢数的计数器收进锁里，30 轮全对。同一条读数我 2026-11-16 重跑了两遍，都是 30/30，按原文退到 `-O0` 编的。链条咱们一节节数：线程 A 在临界区里执行 `++counter`，写 sequenced-before 同一临界区收尾的 `unlock()`，这是程序顺序。接着是桥的一节，`unlock()` 与线程 B 随后成功的 `lock()` 之间，标准写明 synchronizes-with（[thread.mutex.requirements.mutex]/24）。B 的拿锁 sequenced-before 它的读。三节连上，A 的写 happens-before B 的读。

为什么每一次接链都接得上？咱们再往锁里看一步：全部加锁放锁动作排成一条单一总序（同条款 /4）。两条线程各 160000 次，共 320000 次临界区，沿着它接成一条长链，B 每次进临界区，A 在前面写下的值它都看得见。主线程收尾读 `counter` 之前，`a.join()` 与 `b.join()` 把两条工作线程的全部写垫在自己前面（[thread.thread.member]/4）。

您回头看，30 轮不丢的原因能落到链条上：桥一路都在。

### `-O2` 下看起来对的程序，差在哪一节

《从错误程序认识竞争》里没上锁的计数器在 `-O2` 下 30 轮全对。您现在可以按链条语言重判：两条工作线程对 `counter` 的写与读之间，没有锁，`counter` 也不是原子，桥一节都数不出来。join 帮不上，它只把每条工作线程连到主线程，连不到彼此。判定 data race，后果未定义行为，30 轮全对只是一个样本。本篇开头的死循环是同一件事的另一个下场：标准不管之后，编译器按“没有别人写 stop”推出了原地打转。

### 条件变量：桥从锁里借

[《等待一个条件成立》](../chapter03-wait-and-coordinate/01-wait-for-a-condition)的最小程序里，worker 在锁内改 `done`，放锁，然后 `notify_one()`。waiter 拿着锁进 `wait`，带谓词。当时说过的“醒来之后 `done` 一定是 true”，咱们用链条补全。worker 的 `done = true` 在前，作用域收尾的放锁在后，两者是 sequenced-before。waiter 从 `wait` 里醒来时，`wait` 内部替它重新拿了锁（[thread.condition.condvar] 第 8 段）。重新拿到的锁与 worker 的放锁之间，标准写明 synchronizes-with（[thread.mutex.requirements.mutex]/24）。谓词通过之后的读，sequenced-after 重新拿锁。链成，`done` 和它旁边的数据对 waiter 全部可见。您注意，可见性的桥没走通知，走的是锁。

咱们把通知放回链条里看。条款里 notify 的效果只有一句，解除一条正在等待的线程的阻塞（[thread.condition.condvar]/5）。同步关系，一句也没有。您看通知的价值在时序上，它把睡着的线程拉回来，重新走“拿锁、查谓词”的路，可见性来自其中重新拿锁的一步。3.1《等待一个条件成立》说过的“谓词和锁得成对用，您少一样都会出事”，在链条语言里看得更清楚：桥在锁里，通知只负责把人叫回桥口。丢失唤醒也是同一句话，没有等待中的 `wait` 被解除，notify 就没有跟谁搭桥，它自己不留记录。《等待一个条件成立》的实验里，通知发完 150 毫秒没人接，`worker` 一直睡在 `wait` 里。咱们数完就认下：搭桥的从来是谓词与同一把锁。

### cppreference 的线程安全段落，您现在能自己读了

讲到这里，咱们回到读文档上。cppreference 各页底部的 Thread safety 段落，用的正是这套语言。`std::atomic<std::shared_ptr>` 的页上有一段（2026-11-16 核对的原文）：

> If multiple threads of execution access the same std::shared_ptr object without synchronization and any of those accesses uses a non-const member function of shared_ptr then a data race will occur unless all such access is performed through an instance of `std::atomic<std::shared_ptr>` …

逐句换成链条语言。without synchronization，两条线程的访问之间没有 happens-before。data race will occur，[intro.races] 第 17 段的判定成立。performed through an instance of `std::atomic<std::shared_ptr>`，把访问挪到原子操作上走，桥由原子操作搭。别的页面的 Thread safety 段落，措辞都是三件套的变体，您顺着查就行。

### 检查的时候问些什么

拿到并发代码，我习惯按下面的顺序过一遍共享的读写：

1. 要检查的读写之间，有桥吗？数得出成对的同步动作（放锁与拿锁、构造与线程函数、线程结束与 join、release 与 acquire）才算数。
2. 桥的两端连上要检查的读写了吗？写要在放锁（或 release 写）之前，读要在拿锁（或 acquire 读）之后，链子中途不许断。
3. 数不出桥的，谁来补？补锁、补 join、把访问挪到原子操作上，都是补桥的路子。什么都不补，该程序就退出了标准的管辖，读数再好看也只是样本。

## 下一步

顺序，咱们从两层摸过一遍：硬件层的一致性保证“写迟早可见”，语言层的 happens-before 决定什么时候可见、两步谁在谁前。下一篇 5.3《选择内存序需要什么依据》已经成篇，咱们接着往原子操作的参数里走。同一对写与读，隔着的几档内存序各保什么、各花多少，咱们在 litmus 里用过的 acquire/release，就住在 relaxed 与 seq_cst 的中间。

翻页之前，请您把本篇的 litmus 编出来跑一遍，两条命令都在上面。有精力的话，把 `pin` 的两行删掉再跑，两条线程在调度器手里换核，读数怎么动，您自己量一遍。

## 参考资源

推进思路借鉴了 Anthony Williams 的《C++ Concurrency in Action》，取的是内存模型一章。缓存与一致性的白话讲法，参考了 Paul McKenney 在网上公开全文的书。happens-before 的写法，参考了 Preshing on Programming 上的实验文章。借的是结构与讲法，例子和文字都是我自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的现行草案与 cppreference。

- [Multi-threaded executions and data races（cppreference）][cppref-multithread]，data race 与 happens-before 的总览页。
- [现行标准草案：Data races（intro.races）][draft-intro-races]，冲突、同步关系、happens-before 与强发生的定义、可见的副作用。
- [现行标准草案：Sequential execution（intro.execution）][draft-intro-exec]，sequenced-before 的定义。
- [现行标准草案：Abstract machine（intro.abstract）][draft-intro-abstract]，as-if 规则的出处。
- [现行标准草案：Mutex requirements（thread.mutex.requirements.mutex）][draft-mutex]，锁的总序与 unlock/lock 的同步关系。
- [现行标准草案：thread constructors（thread.thread.constr）][draft-thread-constr]，构造完成与新线程函数开头的同步关系。
- [现行标准草案：thread members（thread.thread.member）][draft-thread-member]，join 的同步关系。
- [现行标准草案：Class condition_variable（thread.condition.condvar）][draft-condvar]，wait 的分段动作与 notify 的效果。
- [现行标准草案：Order and consistency（atomics.order）][draft-atomics-order]，内存序的含义、release/acquire 的同步关系与 seq_cst 总序。
- [现行标准草案：Class template barrier（thread.barrier.class）][draft-barrier]，完成步与强发生关系，回访 3.4 时引用。
- [std::atomic 的 shared_ptr 特化（cppreference）][cppref-atomic-shared-ptr]，本篇引用的 Thread safety 段落所在页。
- [Memory Reordering Caught in the Act（Preshing on Programming）][preshing-reordering]，与本篇 litmus 同形的实验，写缓冲区穿越的测法参考。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，内存模型一章。
- [Is Parallel Programming Hard, And, If So, What Can You Do About It?（Paul E. McKenney）][mckenney]，缓存的章节。

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[draft-intro-races]: https://eel.is/c++draft/intro.races
[draft-intro-exec]: https://eel.is/c++draft/intro.execution
[draft-intro-abstract]: https://eel.is/c++draft/intro.abstract
[draft-mutex]: https://eel.is/c++draft/thread.mutex.requirements.mutex
[draft-thread-constr]: https://eel.is/c++draft/thread.thread.constr
[draft-thread-member]: https://eel.is/c++draft/thread.thread.member
[draft-condvar]: https://eel.is/c++draft/thread.condition.condvar
[draft-atomics-order]: https://eel.is/c++draft/atomics.order
[draft-barrier]: https://eel.is/c++draft/thread.barrier.class
[cppref-atomic-shared-ptr]: https://en.cppreference.com/w/cpp/memory/shared_ptr/atomic2
[preshing-reordering]: https://preshing.com/20120515/memory-reordering-caught-in-the-act/
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
[mckenney]: https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook-e1.html
