---
title: "同一个计数器，两条线程为什么算不对？"
description: "用两条线程把同一个计数器算错的小程序开场，分清 data race 与 race condition，说清偶然成功为什么不能当作正确性依据，再把死锁、饥饿与优先级反转摆成一张问题全景。"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
prev:
  text: "让工作有序结束"
  link: /vol5-concurrency/chapter01-start-concurrency/06-jthread-and-stopping
---

# 同一个计数器，两条线程为什么算不对？

从这里开始，我们并不会搞天花乱坠的东西，兄弟们，只干一件事：请您亲手跑出一个会算错的程序，然后把我们发现的“错误”分成几类。

## 第一手实验：丢失的计数与偶发的失败

咱们从一个最朴素的错误入手。两条线程，各自把一个全局变量加 160000 次，加完主线程读总数，期望是 320000。

<OnlineCompilerDemo
  title="两条线程各加 160000 次，30 轮里丢多少"
  source-path="code/examples/vol5/chapter02/01_demo_lost_updates.cpp"
  description="期望 320000，30 轮里总有几轮对不上，丢掉的数从两百多到十几万都见过。演示 data race 的未定义行为：换优化级别、换机器，读数都会变。"
  run-options="-O0 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_lost_updates.cpp —— 两条线程各加 160000 次，30 轮里丢多少
#include <cstdio>
#include <thread>

long long counter = 0;

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) ++counter;
}

int main() {
  const int kRounds = 160000;
  const int kTrials = 30;
  int correct = 0;
  for (int t = 0; t < kTrials; ++t) {
    counter = 0;
    std::thread a(work, kRounds);
    std::thread b(work, kRounds);
    a.join();
    b.join();
    if (counter == 2LL * kRounds) {
      ++correct;
    } else {
      std::printf("trial %2d: printed %lld, lost %lld\n",
                  t, counter, 2LL * kRounds - counter);
    }
  }
  std::printf("%d of %d trials printed the expected %d\n",
              correct, kTrials, 2 * kRounds);
}
```

</OnlineCompilerDemo>

OK，这里把原则立起来：跑一次，结果什么都说明不了，咱们要跑好多次，观察输出的分布。

比如说，请你让这个程序跑 30 轮，每轮把 `counter` 清零，重新起两条线程。编译成 `-O0`，咱们跑出来是这样：

```text
trial  0: printed 161267, lost 158733
trial  4: printed 315296, lost 4704
trial  5: printed 313422, lost 6578
trial 13: printed 319718, lost 282
trial 14: printed 315173, lost 4827
trial 29: printed 299905, lost 20095
24 of 30 trials printed the expected 320000
```

30 轮里 24 轮对，6 轮错。错的 6 轮，咱们见过的丢法从两百多到十几万都有。多么的随机！您换台机器再跑，结果又是完全不一样，好一个黑盒啊！

为什么丢？`++counter` 在机器上不是一步。嘴上说“加一”，机器干了三件事。把 `counter` 从内存读进寄存器，给寄存器加一，再把结果写回内存。`-O0` 下 `work(int)` 的循环体，读、加、写各占一条指令（拿 `objdump -d` 把编译好的二进制翻回汇编看到的，地址列和注释笔者缩短了，栈帧和循环控制的几条指令也省掉了）：

```text
mov    0x2e98(%rip),%rax        # counter
add    $0x1,%rax
mov    %rax,0x2e8d(%rip)        # counter
```

`%rax` 是一个通用寄存器，您可以把它当成一块临时的小格子。两条线程各自执行读、加、写，步与步之间可以随便穿插。咱们盯着一个常见的坏次序看：a 线程执行了读，拿到 5，还没等它把结果写回去，b 线程也执行了同一条读，同样拿到 5。接下来两条各自 `add` 成 6，各自写回 6。两次加一，数只涨了一，一次加一就这么没了。

“两边都读到旧值”有多常见，咱们得看窗口。从第一条 `mov` 读出旧值，到最后一条 `mov` 写回新值，中间隔的这几步就是窗口。窗口越大，叠在一起的来回越多，丢掉的加一就越多。丢 158733 的那一轮（trial 0），等于两条线程的大半段循环整个叠在一起跑。`-O0` 下这个窗口每轮循环都开一次，160000 轮乘两条线程，总有叠上的时候。

笔者拿同一份源码编了 `-O2` 一版，30 轮，结果让您看：

```text
30 of 30 trials printed the expected 320000
```

一行错误都没有。咱们看看这次的机器码，`work(int)` 在 `-O2` 下换了一副样子（`objdump -d` 的输出，笔者省掉了地址列、符号表，还有入口处判断 `rounds` 是否为正的两条指令）：

```text
mov    0x2c8d(%rip),%rdx        # counter
lea    -0x1(%rdi),%eax
lea    0x1(%rdx,%rax,1),%rax
mov    %rax,0x2c7e(%rip)        # counter
ret
```

您自己数一遍。除去末尾的返回指令 `ret`，前四行就是四条指令：`mov` 读一次 `counter`，两条 `lea` 算出并加上 `rounds`，`mov` 写回去。`lea` 本意是算地址，编译器常用它顺路做一次加法，`%rdi` 里装着传进来的第一个参数，也就是 `rounds`。循环没了，原来 160000 次“读一次、加一、写回”，折成了这一次读写。窗口的形状也跟着变了。不再是每轮循环开一个小窗口、开 160000 次，而是整个函数一个大窗口、只开一次。

编译器凭什么这么改？咱们把两种情况分开看：在没有竞争的代码里，这是等价变换，循环 160000 次加一和一次加 160000 结果一样。在有竞争的代码里，它把“读 160000 次”变成了“只读一次”，另一条线程中间写进去的数会被整个覆盖掉。

标准允许编译器这么做，因为在标准眼里，咱们写的程序已经是**未定义行为**了。就是，额，你这个呢，不是错哈，是我也不保证你这个灰色地带是不是正确的。标准也没说对不对。

## data race：标准规定的未定义行为

data race，中文一般叫**数据竞争**。它有正式定义，C++11 引入内存模型的时候写进标准，条款编号随草案版本改过，现在叫 [intro.races]。标准把四件事写进定义，咱们逐条对一遍：同一块内存上有两个冲突动作，两条动作可能并发，其中至少一个不是原子操作，并且两者之间谁也没有 happens-before 谁。

咱们把四个词逐个换成白话。冲突动作，指一条写、另一条读或者也写，也包括在同一块存储上开始或结束对象的生命期。可能并发说的是两条动作由不同线程执行，信号处理里还有一种边角情况，本篇不碰。原子操作则是一步做完、中间不让别的线程看见半成品的动作，细节归讲原子操作与内存模型的几篇。**happens-before** 是标准里用来推“谁在谁之前”的关系，join、互斥量、原子操作都能建立它。

咱们往标准里再读一句，写的是后果。任何这样的 data race，都导致未定义行为（现行草案 [intro.races] 第 17 段，C++11 起就有）。

未定义行为是个容易被读轻的词。它不是说“结果随机”。它的意思是标准对这个程序不再提任何要求，编译器可以假设别的线程没有在动这块内存。上一节让您逐条数过的 `-O2` 机器码，就是编译器按“没有别人在动这块内存”的假设做出来的。

程序算错数，是您观察到的现象，它有概率、有分布，每次可能不同。未定义行为不一样，它是标准给出的判定，固定、无歧义。

还有一条容易混的条款，[res.on.data.races]，它管的是标准库自己。原文大意是：标准库函数不许直接或间接访问别的线程能访问的对象，除非对象是通过参数（包括 `this`）传进来的。条款给的是您的保障，让 `std::vector`、`std::string`、`std::cout` 不会背地里碰您没交给它的数据。它不给您的代码任何豁免。

落到您自己的代码上，判断法很短，您要问四件事：两条动作是不是由不同线程执行，碰的是不是同一块内存（其中一条是写，或者开始、结束对象的生命期），有没有一条不是原子操作，两条之间有没有 happens-before 关系。四条都对上，程序就已经不在标准的管辖之内了。

## race condition：逻辑层面的顺序问题

笔者搭了一个程序，里面一条 data race 都没有。共享变量是 `std::atomic<int>`（C++11 起），每一次读、每一次写各自完整，标准不会判它未定义行为。

<OnlineCompilerDemo
  title="没有 data race，照样丢计数：原子上的读改写"
  source-path="code/examples/vol5/chapter02/02_demo_race_condition.cpp"
  description="每次读写各自完整原子，但读、加、写回三步会被打断：期望 200000，每次都差一截。标准不判它未定义行为，毛病在设计层的次序上。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_race_condition.cpp —— 没有 data race，照样丢计数：原子上的读改写
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> counter{0};

void work() {
  for (int i = 0; i < 100000; ++i) {
    counter = counter.load() + 1;
  }
}

int main() {
  std::thread a(work);
  std::thread b(work);
  a.join();
  b.join();
  std::printf("%d\n", counter.load());
}
```

</OnlineCompilerDemo>

咱们跑 8 次，结果如下：

```text
147112
164959
183999
178921
152737
140398
144015
125283
```

咱们期望 200000，一次都没到。每次读写都是原子的，为什么还少？因为 `counter = counter.load() + 1` 是三个动作：读一次，加一，写回去。原子只保证每一个动作自身完整，不保证三个动作连在一起不被打断。两条线程各自读完旧值、各自加一、各自写回，一次加一就丢了。这跟前面非原子版本的丢法一模一样，区别只在判定上：这里标准不提任何意见，程序每一步都有定义。

race condition 不是标准里的术语。您在标准草案里查不到它，它也没有形式定义。它说的是设计层面的事：程序的正确性依赖两条执行流交错的次序，换个次序就得到不同的结果。

丢法的名字是现成的，叫**读改写**：读一次，算一次，写回去，三步被当成一步用。它还有一门近亲叫 **check-then-act**，检查之后动手。咱们看毛病出在哪：检查看到的是旧状态，动手的时候状态已经变了。前面 200000 掉到 147112 的代码，走的正是读改写。

您往现实里看，race condition 常常藏得更深。电商扣库存，“查了还有”和“扣减”分成两次操作，中间别人买走一件。文件系统上“判断不存在就创建”，两条线程一起判断，一起创建。这样的程序里可能一条 data race 都没有，共享数据全是原子的，甚至根本没共享 C++ 对象，结果照样错。

data race 管的是语言层面的判定，有确切定义，有固定后果，命中就是未定义行为。race condition 管的是设计层面的缺陷，结果依赖执行次序，而次序不由您安排。

一个程序可以没有 data race 却有 race condition，咱们前面跑过的原子计数程序就是。反过来，有 data race 的程序一定不可靠，因为标准已经不管它了。

## 偶然成功不是正确性依据

前面用 `-O0` 编的版本，30 轮里总有一批对不上。只跑一轮看见 320000 就认为程序没问题，是最容易犯的误判，您换个规模重跑，数字立刻变。

错在什么时候显形，由几样东西决定：次数规模、您的机器、优化级别。笔者拿同一段程序只改次数，四档都用 `-O0` 编，每档跑 30 轮：每边 50000 次时 30 轮全对，100000 次时对 28 轮，160000 次时对 24 轮（160000 一档笔者多跑了几次，落在 14 到 27 之间），200000 次时只剩 2 轮。变量只有一个，就是次数。您换一台电脑，缓存、核数、调度都不一样，几组数字会跟着变。

优化级别也一样。同一份源码用 `-O2` 编出来，30 轮全对。要是您一开始就用 `-O2` 编译它，可能压根见不到错，还会觉得代码写得挺稳。

那您怎么判断一段并发代码对不对？测试给不出肯定答案，它只能证伪。

笔者把同一份源码加上 `-fsanitize=thread`（GCC 的 ThreadSanitizer，下面简称 TSan）重新编译（`-O1 -g`，不带 `-g` 时报告里只剩地址和函数名，不出文件名和行号），跑了一次，它报出来的东西是这样：

```text
WARNING: ThreadSanitizer: data race (pid=8)
  Read of size 8 at 0x…… by thread T2:
    #0 work(int) 01_demo_lost_updates.cpp:8
  Previous write of size 8 at 0x…… by thread T1:
    #0 work(int) 01_demo_lost_updates.cpp:8
  Location is global 'counter' of size 8 at 0x……
```

同一份源码，`-O2` 的二进制 30 轮全对，TSan 照样报出 data race。它报的是标准意义上的竞争。工具怎么插桩、能力边界在哪，归讲调试、测试与性能的几篇，这里笔者只借它说明一件事：跑对和正确是两件事。

那您靠什么？靠标准条款看标准怎么判眼前的共享访问，靠同步关系看读写有没有被 happens-before 连起来。工具和测试是另一路，看它们报了什么、跑了哪几次。

笔者自己的做法是，写代码前问自己：这块数据归谁，谁在读，谁在写。这是笔者写并发 demo 前的门槛，不是验收并发库的办法。答不上就别指望测试帮您兜住。测试跑一次只能算一个样本，样本少的时候它连错都看不见。

## 竞争问题全景：死锁、饥饿

前面几节的实验都落在结果算错上。还有一类问题，程序压根走不下去，或者只有一部分线程走不下去。咱们把两种常见的摆出来，各配一个最小样本：死锁里两条线程各持一把锁，各自在等对方手里的锁。饥饿是被观察的线程一直等，其他线程一直拿到锁。饥饿本篇只认情形，修法不在这里铺。

### 死锁：各自持一把，等对方松手

白话讲，咱们让两条线程各自拿到一把锁，又都去要对方手里的锁，谁也不松手，于是谁也走不了。

样本里用了 `std::lock_guard`，您只要知道它构造时拿锁、析构时放锁。细节等下一篇讲互斥量时再展开，这里只看它们把线程拦在哪儿。

<OnlineCompilerDemo
  title="死锁：各持一把锁，等对方松手"
  source-path="code/examples/vol5/chapter02/03_demo_deadlock.cpp"
  description="打印两行 [one]/[two] holds 之后进程就停住不走了（在线运行有超时会替您收场，本地得 Ctrl-C）。死锁一旦成立就是稳定的，回回如此，不带概率。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_deadlock.cpp —— 各持一把锁，等对方松手：跑起来会停在两行
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

std::mutex m1, m2;

void route_one() {
  std::lock_guard<std::mutex> a(m1);
  std::puts("[one] holds m1");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> b(m2);
  std::puts("[one] got m2 as well");
}

void route_two() {
  std::lock_guard<std::mutex> b(m2);
  std::puts("[two] holds m2");
  std::this_thread::sleep_for(50ms);
  std::lock_guard<std::mutex> a(m1);
  std::puts("[two] got m1 as well");
}

int main() {
  std::thread t1(route_one);
  std::thread t2(route_two);
  t1.join();
  t2.join();
  std::puts("both finished");
}
```

</OnlineCompilerDemo>

咱们跑出来两行，然后就没了：

```text
[one] holds m1
[two] holds m2
```

进程不动了。笔者用 `timeout 2` 跑，两秒后进程被杀掉，退出码 124，`both finished` 一行始终没打印。把输出重定向到文件或接进管道时，`stdout` 的缓冲会跟着被杀掉的进程一起丢，您得在终端里直接跑，或者加 `stdbuf -oL`，两行输出才留得住。

死锁一旦成立就是稳定的，回回如此，不带概率。两边都拿到第一把之后，不管谁去要第二把，结果都一样：拿不到。两条线程阻塞在内核的等待队列上，CPU 是闲的。您在进程管理器里看到的是一条几乎不占 CPU、却怎么也不结束的进程。

固定获取顺序归讲死锁与减少共享的一篇，一次性把几把锁拿全（比如 `std::scoped_lock`）归下一篇。这里您只需要认清形状：一条线程持有一把锁，同时等着另一把。

### 饥饿：系统在前进，有人永远排不上

饥饿是另一回事。您看整个系统在往前走，只是某个线程一直拿不到资源。样本里用 `std::atomic_flag` 当一把手工锁，`test_and_set` 与 `clear` 后面括号里是内存序参数。原子标志与内存序的细节归讲原子操作与内存模型的几篇，不耽误您看数字。

<OnlineCompilerDemo
  title="饥饿：锁不排队，谁手快谁拿"
  source-path="code/examples/vol5/chapter02/05_demo_starvation.cpp"
  description="八条线程 200 毫秒里抢到一百多万次，被观察的线程只拿到一万四到五万次，最慢一次等了若干毫秒。标准对互斥量只要求互斥，没答应过排队。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_starvation.cpp —— 锁不排队：八条线程抢 200 毫秒，观察者排不上
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic_flag busy = ATOMIC_FLAG_INIT;

void lock() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock() { busy.clear(std::memory_order_release); }

int main() {
  std::atomic<long> hog_turns{0};
  std::atomic<long> victim_turns{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> hogs;
  for (int i = 0; i < 8; ++i) {
    hogs.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        lock();
        hog_turns.fetch_add(1, std::memory_order_relaxed);
        unlock();
      }
    });
  }
  std::this_thread::sleep_for(50ms);

  auto deadline = std::chrono::steady_clock::now() + 200ms;
  const long hog_before = hog_turns.load();   // 窗口开头的快照
  long worst_us = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    auto t0 = std::chrono::steady_clock::now();
    lock();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (us > worst_us) worst_us = us;
    victim_turns.fetch_add(1, std::memory_order_relaxed);
    unlock();
  }
  const long hog_during = hog_turns.load() - hog_before;

  stop.store(true);
  for (auto& t : hogs) t.join();
  std::printf("in 200 ms: 8 threads took the lock %ld times, the other %ld times\n",
              hog_during, victim_turns.load());
  std::printf("its longest single wait: %ld us\n", worst_us);
}
```

</OnlineCompilerDemo>

走起三次：

```text
in 200 ms: 8 threads took the lock 1397111 times, the other 34867 times
its longest single wait: 11133 us
in 200 ms: 8 threads took the lock 1338603 times, the other 14041 times
its longest single wait: 4880 us
in 200 ms: 8 threads took the lock 1422153 times, the other 49968 times
its longest single wait: 17082 us
```

八条线程在 200 毫秒里拿到锁 134 万到 142 万次，咱们要观察的线程只拿到 1.4 万到 5.0 万次，最慢一次等了 5 到 17 毫秒。临界区，也就是拿到锁到放掉锁中间的几行，只有几条指令，正常等待是纳秒级，机器谈不上忙。差距来自锁不排队，谁手快谁拿。

笔者这次没看到彻底饿死，得说清楚：被观察的线程 200 毫秒里还是进去了 1 万多次，样本给的是读数，它不证明某台机器上一定会永远排不上。

您翻 C++ 标准会发现，它只规定了互斥语义，公平性（排队次序）不在要求里，也没有禁止实现自己排队（条款 [thread.mutex.requirements]）。所以别怪哪把锁做得不好，标准压根没答应过排队。

两种问题的差别，咱们摆在一起看：

| 问题 | 线程在做什么               | 整个系统             |
| ---- | -------------------------- | -------------------- |
| 死锁 | 各自持有一把，阻塞等另一把 | 停住                 |
| 饥饿 | 一直等，其他线程一直抢到   | 在前进，它一直排不上 |

## 优先级反转（扩展）

本节带（扩展）标记，跳过它不影响本篇主线。不过您以后碰到实时系统、碰到“高优先级的任务卡住了”一类的报告，本节能帮您认出等待关系长什么样：高优先级的任务卡在一把锁上，锁在低优先级任务手里，而低优先级任务又被中优先级的活挤在一边。

优先级反转要三个角色，笔者按优先级叫它们高、中、低。低优先级线程持有一把锁正在干活。高优先级线程来了，要同一把锁，只能等低优先级的放锁。到这里还算正常，因为低优先级很快就会放。麻烦在中优先级线程这时候醒了，它不需要同一把锁，可它比低优先级高，于是把低优先级挤掉，占住 CPU。结果就是：高优先级线程等的时间，从低优先级干完手头的一点活，变成了中优先级所有活干完，而中优先级的活有多长，没有上限。

这就是反转。您顺着等待关系捋一遍：高优先级的线程实际在等一个中优先级的线程，而它俩之间根本没有共享数据。

您翻遍标准库也找不到给线程设优先级的接口，`std::thread` 身上没有对应的成员，想动优先级得用 `native_handle()` 走到平台接口上，它把标准库的线程对象换成本机平台的原始句柄，《第一个 `std::thread` 程序》的参考资源里挂了它的对照页。所以优先级反转不是 C++ 标准层面的事，它属于调度策略。

修法有名字，咱们叫它**优先级继承**。高优先级线程等锁的时候，把持有者的优先级临时抬到和等待者一样高，中优先级就挤不掉它了，锁一放，优先级恢复。POSIX 把它做成了互斥量的属性，`pthread_mutexattr_setprotocol` 配 `PTHREAD_PRIO_INHERIT`，本机的 `<pthread.h>` 里就能找到它们。

笔者给您找一个真事：1997 年火星探路者号的着陆器反复重启，NASA 喷气推进实验室（JPL）的复盘把原因归到优先级反转上，后来成了教材里的标准案例。

本节没有实验，原因是优先级继承要真正起作用，线程得跑在实时调度策略下（比如 `SCHED_FIFO`），那需要权限，笔者没在这儿跑过。上面讲的是结构。

## 下一步

第二单元往下走，咱们接着看具体的修法。读改写造成的丢计数、检查之后动手的复合操作，修法都在后面几篇。饥饿只认情形，不往修法上走。

下一篇讲互斥量，处理本篇少算的计数器：咱们把共享访问收进锁里，明确每把锁保护什么，再讨论锁的粒度。检查与动手分开的做法，也就是“查了还有、扣的时候没了”，再往后一篇专门讲。用锁顺序防死锁归《谁也不松手，怎么办》，独立持有、传递结果、共同修改三种方案的取舍归《能不共享就不共享》。

趁您还没翻页，笔者想请您做一件事：把本篇 30 轮的实验在您自己的机器上跑一遍，`-O0` 和 `-O2` 都跑一遍，看看两边差多少，再把 `-O0` 一版多跑几次。

## 参考资源

本篇的章节推进思路借鉴了 Anthony Williams 的《C++ Concurrency in Action》，取的是讲共享数据的几章。例子和文字都是笔者自己写的，实验在 GCC 16.2.1 上编译实测。涉及标准行为的断言，按文中标注的条款核对过 eel.is 上的草案文本与 cppreference。

- [Multi-threaded executions and data races（cppreference）][cppref-multithread]，data race 的定义、happens-before 与同步操作。
- [现行标准草案：Data races（intro.races，eel.is）][draft-intro-races]，定义原文，以及“任何这样的 data race 都导致未定义行为”这句话，段号按现行草案。
- [C++ 标准草案：Data race avoidance（res.on.data.races）][draft-lib-races]，标准库实现的义务。
- [std::mutex（cppreference）][cppref-mutex]，互斥量要求的对照页（[thread.mutex.requirements]），只规定互斥语义，不涉及排队次序。
- [Why Undefined Semantics for C++ Data Races? — Hans Boehm][boehm]，为什么标准选择未定义行为，而不是给出一套确定的语义。
- [GCC 的插桩选项（含 `-fsanitize=thread`）][tsan]，选项说明。
- [ThreadSanitizer（Google Sanitizers wiki）][tsan-wiki]，报告格式与检测原理。
- [Priority inversion — Wikipedia][wiki-priority]，优先级反转的表述与常见修法。
- [What Really Happened on Mars — Glenn Reeves（JPL）][reeves]，探路者号那次故障的复盘。

[cppref-multithread]: https://en.cppreference.com/w/cpp/language/multithread
[draft-intro-races]: https://eel.is/c++draft/intro.races
[draft-lib-races]: https://eel.is/c++draft/res.on.data.races
[cppref-mutex]: https://en.cppreference.com/w/cpp/thread/mutex
[boehm]: https://www.hboehm.info/c++mm/why_undef.html
[tsan]: https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
[tsan-wiki]: https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual
[wiki-priority]: https://en.wikipedia.org/wiki/Priority_inversion
[reeves]: https://www.cs.unc.edu/~anderson/teach/comp790/papers/mars_pathfinder_long_version.html
