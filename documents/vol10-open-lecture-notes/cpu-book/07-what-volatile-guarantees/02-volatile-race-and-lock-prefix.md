---
title: "volatile 拦不住数据竞争：丢更新现场与 lock 前缀的分界"
chapter: 7
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "两个线程并发自增一个 volatile int，十万次六回全对、一百万次八回全丢，再看三条语句熔回一条 addl、编译器屏障把窗口真拉开，最后拿 std::atomic 的 lock 前缀对出禁编译器与禁竞争的机器码分界"
reading_time_minutes: 13
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# volatile 拦不住数据竞争：丢更新现场与 lock 前缀的分界

上一篇的收尾停在禁令的清单上：合并、消除、重排，volatile 在单线程里把三件事全替编译器禁了，几组实验的范围也从头到尾都在一个函数、一个线程里。这一篇咱们跨出单线程：两个线程共用一个 volatile 变量、甚至并发自增，它还保得住吗，咱们拿竞争现场与反汇编接着对。咱们用的环境与[上一篇](01-volatile-bans-in-one-thread.md)相同，您要复跑就照它来。

## 转折：那多线程加 volatile 行吗

咱们讲到这里，一个流传极广的用法正等着咱们：两个线程共用一个变量、一边写一边读，只要给它加上 volatile 的限定，是不是就能及时看到对方的新值？咱们再往前问一步，两个线程都对它做自增的场景，volatile 能保住计数不丢吗？咱们直接做实验，实验的口径得交代在前面：程序里没有任何同步的手段，两个线程在无同步的状态下并发读写同一个 volatile int，在 C++ 标准那里就成了数据竞争。数据竞争说的是两个线程访问同一对象、至少一方是写、又没有任何同步隔开的局面，所以标准把它归为未定义行为，而未定义行为的意思，是标准不再承诺程序的任何行为边界。下面的输出咱们只记录本机实际发生的现象，您换机器、换负载量，看到的东西完全可能不同。

```cpp
// v4_race.cpp: volatile 共享变量 + 两线程并发自增 —— 数据竞争(未定义行为)展示
// 本程序是 UB 的展示,只记录本机实际发生的现象
#include <cstdio>
#include <thread>

volatile int counter = 0;

int main() {
    constexpr int kIters = 100000;  // 每线程自增次数

    std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race.cpp -o v4_race
```

编译阶段就有两条警告冒了出来，咱们一条 `-Wall` 都没开，GCC 16 默认就报了出来：

```text
v4_race.cpp: In lambda function:
v4_race.cpp:11:61: warning: '++' expression of 'volatile'-qualified type is deprecated [-Wvolatile]
   11 |     std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
      |                                                             ^~~~~~~
v4_race.cpp: In lambda function:
v4_race.cpp:12:61: warning: '++' expression of 'volatile'-qualified type is deprecated [-Wvolatile]
   12 |     std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
      |                                                             ^~~~~~~
```

警告的来路后面专门讲，您只要记下它来自 C++20 的弃用。咱们连跑了 6 次，输出咱们逐字贴在下面：

```text
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
```

六次跑出来的全是 200000，丢掉的更新数是零。您要是拿这个结果去相信 volatile 能保住计数，那这一篇就白写了。咱们看反汇编找原因，反汇编里的线程体长这样（`objdump -d --no-show-raw-insn v4_race`）：

```asm
0000000000001370 <...6_M_runEv>:
    1370:	mov    $0x186a0,%eax
    1375:	data16 cs nopw 0x0(%rax,%rax,1)
    1380:	addl   $0x1,0x2cdd(%rip)        # 4064 <counter>
    1387:	sub    $0x1,%eax
    138a:	jne    1380 <...6_M_runEv+0x10>
    138c:	ret
```

咱们在这里认识读改写这个词：读一次旧值、改一下、再写回去，三步合起来的就是一次读改写，自增就是最典型的读改写。咱们把开场两条也认了：`mov $0x186a0,%eax` 装的是循环计数，0x186a0 正是 100000 的十六进制写法，`data16 cs nopw` 是对齐填充的空操作，正主是中间的 addl。汇编里 `++counter` 被编成了单条 `addl $0x1, counter(%rip)`，一条指令同时干完了读、加、写三件事，读改写的窗口只剩几拍（拍就是时钟周期，CPU 的节拍）。两个线程的 addl 要落在同几拍里才会丢更新，这样的概率本来就低，再叠加缓存行（CPU 缓存管理的最小单位，常见 64 字节）在两个核之间来回搬运时自带的串行化效果，10 万次的量级下，本机的六次运行一回都没碰上。

但是咱们把边界划清：单条的 addl 是没有 lock 前缀的，原子指令的资格它依然没有，窗口小不等于窗口关上了。没观察到丢更新的这六回，不等于程序是对的。未定义行为从来没有灾难一定上演的承诺，它连上演的时机都不说，这正是它最麻烦的地方：您从通过的测试里拿不到任何保证。光说不过瘾，咱们把量级放大十倍，每线程的自增放大到 100 万次，代码里只改了一个常量，`kIters = 1000000`，编译的警告与刚才相同。八次运行的输出咱们全贴：

```text
volatile counter = 1681797 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1046051 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1492306 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1609693 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1895598 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1159715 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1029614 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1086008 (2*1000000 = 2000000 if no update were lost)
```

八次运行全部丢了更新。跌得最狠的一回是 1029614，离 2000000 差了将近一半，差不多把一整个线程的全部工作量丢光了。最好的那回是 1895598，也丢了约 5%。丢多少在每一回里都是随机的，因为丢不丢取决于两边的执行在时间上怎么交错，而时机每回都重新洗牌。同一份代码就摆在咱们的眼前，每线程 10 万次的时候六回全对，拉到 100 万次的时候就八回全丢，咱们把两个结果放在一起读才算读全了。竞争的后果取决于窗口与时机：负载一放大，概率就攒了起来，现场也就藏不住了。您要是在项目里见过一段多线程 volatile 计数跑了好几年没出过事，它和咱们这六回是同一种运气。

复现的口径咱们也交代在前面：文中贴的数字都是本机实测的现场记录，您在自己机器上跑 v4_race 与 v4_race_1m（两者只差 kIters 一个常量），负载与调度时机和咱们这里不同，得到的结果可能也不同。而没炸不等于对的判断，本来就不依赖哪一次的具体数字：十万次的量级本机跑了六回全对，一百万次的量级就八回全丢了，同一份代码换个负载就换了个局面，这正是咱们刚走过的路。

## 拆成三条语句，照样熔成一条指令

您读到这里，可能已经想出一个自救的办法：++ 的读改写挤在一条指令里、窗口才那么窄，那咱们把读、算、写拆成三条语句，窗口不就开了？编译器还能把它们粘回去不成？GCC 16 的实测答案是：能。咱们顺便认识熔合这个词，后文咱们都靠它说话：编译器把两次独立的内存访问拼装进同一条指令的现象，咱们叫熔合。咱们要看的源码长这样：

```cpp
// v4_race_rmw.cpp: 读、算、写拆成三条语句 —— 观察 GCC 是否仍熔合
#include <cstdio>
#include <thread>

volatile int counter = 0;

void bump() {
    int v = counter;   // volatile 读
    v = v + 1;         // 普通计算
    counter = v;       // volatile 写
}

int main() {
    constexpr int kIters = 100000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) bump(); });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) bump(); });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race_rmw.cpp -o v4_race_rmw
```

咱们看 `bump` 的反汇编：统共两条。

```asm
0000000000001410 <_Z4bumpv>:
    1410:	addl   $0x1,0x2c4d(%rip)        # 4064 <counter>
    1417:	ret
```

咱们运行了 6 次，跑出来的全是 200000：

```text
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
volatile counter = 200000 (2*100000 = 200000 if no update were lost)
```

咱们写下的三条源码语句，到机器码里只剩了一条指令。volatile 读与 volatile 写中间隔着的只是普通加法，GCC 16 照样把它们熔成了单条 addl。从抽象机（01 介绍过的、标准虚构出来的机器模型）的角度看，指令把一次读与一次写都干完了，访问的次数没有减少，顺序也保持了原样、语义上完全合规。但是从机器层面看，读与写之间的窗口已经不存在了，六次运行全对的结果与十万次自增的那一版一模一样。咱们把判断说准：语句级的拆分不等于指令级的拆分。您在源码里数出来的窗口，得看编译器把它拼进了几条指令才算数，volatile 管住了访问的次数与顺序，偏偏管不住的是访问被拼进同一条指令。

## 编译器屏障把窗口真拉开

咱们想真把窗口拉开，这时候请的就是编译器屏障。屏障这个词咱们给它一句白话：一道只对编译器有效的墙、机器码里一个字节都不占，但编译器不许把墙两边的内存访问合并或者穿越它。咱们用 GCC 的内联汇编写法 `asm volatile ("" : "+r"(v))`：一条空的内联汇编，约束 `"+r"(v)` 把变量 v 绑在它的输入输出上，编译器必须假设 v 在墙这边读到的值和墙那边要写的值互不知情，两头的访问就都不敢并了。有意思的是，连这道墙都是靠 volatile 撑起来的，asm volatile 声明的是内联汇编自带副作用、不许删，您看，对 CPU 那头它是管不到的，管编译器的时候倒是什么地方都使得上。屏障在这组实验里只是观察的手段，它拦的是编译器把被观察对象的两条访问合并，被观察的对象自始至终是那个 volatile int counter：

```cpp
// v4_race_split.cpp: 读与写之间加编译器屏障,观察窗口拉开后的竞争形态
#include <cstdio>
#include <thread>

volatile int counter = 0;

void bump() {
    int v = counter;                  // volatile 读
    asm volatile ("" : "+r"(v));      // 编译器屏障:防止两条访问被熔合(观察手段,不是观察对象)
    counter = v + 1;                  // volatile 写
}

int main() {
    constexpr int kIters = 1000000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) bump(); });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) bump(); });
    t1.join();
    t2.join();

    std::printf("volatile counter = %d (2*%d = %d if no update were lost)\n",
                counter, kIters, 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v4_race_split.cpp -o v4_race_split
```

```asm
0000000000001420 <_Z4bumpv>:
    1420:	mov    0x2c3e(%rip),%eax        # 4064 <counter>
    1426:	add    $0x1,%eax
    1429:	mov    %eax,0x2c35(%rip)        # 4064 <counter>
    142f:	ret
```

这回 `bump` 是货真价实的三条指令：装载、加一、存回，屏障自己的字节一个都不占，这正是咱们刚说过的性质。每线程 100 万次的负载，六次运行的输出咱们也全贴：

```text
volatile counter = 1181492 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1186724 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1499203 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1364355 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1067062 (2*1000000 = 2000000 if no update were lost)
volatile counter = 1229806 (2*1000000 = 2000000 if no update were lost)
```

咱们把窗口一开，丢更新就大规模出现了，六回丢掉的幅度在 25% 到 47%。读与写中间隔着的是一条 add，这时候两个线程读到的可能是同一个旧值，随后各自加了一、又各自写回，两个 1 写进了同一个位置，净效果就只加了一。装载、加、存回的三段式汇编，就是读改写竞争的最小现场。咱们在这里多交代一句，这道屏障拦的只有编译器，对 CPU 那一层它是半个字节都不管的，真正在指令流里立墙的 fence 是另一路货色，下一节咱们把去处指给您。

## 只差一个 lock 前缀

场景还是原来的样子，咱们把计数器从 `volatile int` 换成了 `std::atomic<int>`，两个线程各十万次自增的负载与循环写法原样保留：

```cpp
// v5_atomic.cpp: 同场景换 std::atomic —— 计数应当恒正确
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> counter{0};

int main() {
    constexpr int kIters = 100000;

    std::thread t1([] { for (int i = 0; i != kIters; ++i) ++counter; });
    std::thread t2([] { for (int i = 0; i != kIters; ++i) ++counter; });
    t1.join();
    t2.join();

    std::printf("atomic counter = %d (expected %d)\n",
                counter.load(), 2 * kIters);
    return 0;
}
```

```bash
g++ -O2 -std=c++20 -pthread v5_atomic.cpp -o v5_atomic
```

八次运行拿到的全部是 `atomic counter = 200000 (expected 200000)`，一行的差别都没出现过，咱们就不把八行贴满了。真正的主角是线程体的反汇编：

```asm
00000000000013f0 <...6_M_runEv>:
    13f0:	mov    $0x186a0,%eax
    13f5:	data16 cs nopw 0x0(%rax,%rax,1)
    1400:	lock addl $0x1,0x2c5c(%rip)        # 4064 <counter>
    1408:	lock addl $0x1,0x2c54(%rip)        # 4064 <counter>
    1410:	sub    $0x2,%eax
    1413:	jne    1400 <...6_M_runEv+0x10>
    1415:	ret
```

lock 咱们给它一句白话：它是 x86 的指令前缀，写在指令的最前面，它的作用是要求 CPU 独占执行。它碰的缓存行在执行期间被这个核攥住，别的核就插不进来了，整条读改写在硬件层面就成了不可分割的。GCC 还把循环按 2 展开了，每轮的循环体落两条 `lock addl`（两条的偏移差 8，正是各长 8 字节的指令自己带来的），结尾的 `sub $0x2,%eax` 与 `jne` 一轮放行两次自增，正好配合按 2 展开的节奏，每线程整整十万次的自增一分不差，展开的调度不破坏任何一次自增的原子性。

咱们把 volatile 版与 atomic 版线程体里做自增的指令摆在一起，全篇最硬的一组对照就是它：

| 版本 | 线程体的自增指令 |
|---|---|
| volatile int，++counter | `addl $0x1,0x2cdd(%rip)` |
| std::atomic&lt;int&gt;，++counter | `lock addl $0x1,0x2c5c(%rip)` |

指令的本体一模一样，连操作数的形状都相同，差别只有指令前面的 lock 这个词。编译器在源码的层面看不出谁会丢更新，它给两边交出的都是合法的代码，差别则全落在了机器层，兜底的活儿交给了硬件。volatile 的 addl 与 atomic 的 lock addl，这个词就是禁编译器与禁竞争的分界线在机器码上的投影。再串上前面那道屏障您就看全了：volatile 与编译器屏障说话的对象都是编译器、机器码里没有为它们多花一个字节，而 lock 前缀说话的对象是 CPU，它的话就写在指令里。

atomic 的话题到 lock 这里只是开了个头。原子操作还带着内存序（memory_order）的参数，管的是这次原子访问前后的普通读写能不能被 CPU 重新排布，而 volatile 在这个维度上没有任何的对应物。那一侧是 vol5 并发卷的领地，咱们不抢：[原子操作](../../../vol5-concurrency/ch03-atomic-memory-model/01-atomic-operations.md)与[内存序详解](../../../vol5-concurrency/ch03-atomic-memory-model/02-memory-ordering.md)两篇有完整的实测讲法，上一节那道编译器屏障与真正 CPU 屏障的分别，[同章的屏障一篇](../../../vol5-concurrency/ch03-atomic-memory-model/03-fence-and-barrier.md)里铺开了。volatile 名下到底写了哪些承诺，下一篇咱们去对标准的原文。
