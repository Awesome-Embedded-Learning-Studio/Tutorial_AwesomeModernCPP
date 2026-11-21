---
title: "判据的练习场：停止标志、引用计数与 DCLP"
description: "把内存序判据放回真实场面：停止标志 release 置 acquire 读，引用计数增 relaxed 减 acq_rel，双重检查锁定与 Meyers 单例，外加发布订阅、fence 与 volatile 的边界，挑档的判据摆在一起。"
chapter: 5
order: 5
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 判据的练习场：停止标志、引用计数与 DCLP

四档的脾气上一篇数完了，这一篇把它们放回真实场面里：停止标志、引用计数、双重检查锁定，每一处都拿判据现场挑一遍档。

## 常见模式：判据的练习场

判据三问挨个上：桥的两端是谁，桥上过什么货，操作站在链条的哪一段。

### 停止标志：release 置、acquire 读

1.6 讲协作式停止之前，轮询标志是人人写过的版本。工作线程反复看标志，主线程置标志，多数时候标志旁边还有一起更新的配置。5.3 数出 relaxed 版桥一节不搭，咱们把修法写成 demo，relaxed 的对照留给篇末动手题：

<OnlineCompilerDemo
  title="停止标志：release 置、acquire 读，配置跟着过桥"
  source-path="code/examples/vol5/chapter05/15_stop_flag.cpp"
  description="main 改完 batch_size 再用 release 置标志，worker 用 acquire 看到停止后读配置，读到的是 64 不是旧值 8。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// stop_flag.cpp —— 停止标志：release 置、acquire 读，标志旁边的数据跟着过桥
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

std::atomic<bool> stop_flag{false};
int batch_size = 8;              // 标志旁边共享的配置，普通变量

int config_after_stop = 0;

void worker() {
  while (!stop_flag.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  config_after_stop = batch_size;   // 看到停止之后读配置
}

int main() {
  std::thread w(worker);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  batch_size = 64;                  // 配置更新压在置标志之前
  stop_flag.store(true, std::memory_order_release);
  w.join();
  std::printf("worker 看到停止后读到的 batch_size=%d\n", config_after_stop);
}
```

</OnlineCompilerDemo>

```text
worker 看到停止后读到的 batch_size=64
```

桥咱们一节节数。batch_size 的写 sequenced-before stop_flag 的 release 写，这是程序顺序。worker 的 acquire 读拿到 true，synchronizes-with 接通。配置的读排在 acquire 之后。三节连成一条，64 进来，8 没有再出现。

什么时候 relaxed 真的够？标志旁边没有任何共享数据的时候。停止信号本身就是全部内容，看到即执行，没有货要跟着过桥，relaxed 的动作完整就够了。判断您自己来：标志的读之后，还要读谁？一个名字都数不出来，relaxed 放心用。数得出来，就给写端 release、读端 acquire。在本机上，acquire 的读与 relaxed 的读编成同一条 mov，加桥不添指令。换到弱内存模型的机器上，acquire 要付屏障的钱，那是买正确性的钱。

### 引用计数：增 relaxed、减 acq_rel

引用计数是 relaxed 与 acq_rel 同框的样板。完整的侵入式指针，旧目录 ch03 的素材文《原子操作模式》有全本，这里咱们只留两个成员函数，内存序才是主角：

<OnlineCompilerDemo
  title="引用计数：增用 relaxed，减用 acq_rel"
  source-path="code/examples/vol5/chapter05/16_refcount_core.cpp"
  description="正文那两个成员函数原样在 Counted 里，main 补成可运行：refs 从 1 加到 3，再逐个放手，减到零时的 release_ref 返回 true。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// refcount_core.cpp —— 引用计数两个成员的内存序搭配
// （正文只留两个成员函数，这里补一个 main 展示行为；完整的侵入式指针见素材文）
#include <atomic>
#include <cstdio>

struct Counted {
  std::atomic<int> refs{1};

  void add_ref() {
    refs.fetch_add(1, std::memory_order_relaxed);   // 只要动作完整
  }

  bool release_ref() {
    // 减到零的线程要销毁对象。读的一头接住别的线程的使用（acquire），
    // 写的一头把自己的使用压在减法之前（release）。
    return refs.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }
};

int main() {
  Counted c;                        // refs 从 1 起步
  c.add_ref();                      // 拿一个新引用：1 -> 2
  c.add_ref();                      // 再拿一个：2 -> 3
  std::printf("两次 add_ref 后 refs=%d\n", c.refs.load());

  while (!c.release_ref()) {}       // 逐个放手，减到零的那一次返回 true，销毁权归它
  std::printf("减到零的那一次拿到销毁权，refs=%d\n", c.refs.load());
  return 0;
}
```

</OnlineCompilerDemo>

判据咱们两头数。减法的一头，减到零的人要销毁对象，销毁要看见别的线程对对象的全部使用，所以减法的读得是 acquire，接住上游。同时每个使用过对象的人，自己的访问得压在减法之前，所以减法的写得是 release，向下游发布。两头都接链，acq_rel。加法的一头，增引用的人只关心自己拿到引用这件事成立，动作完整就够，relaxed。增与减的待遇不一样，这句概括是咱们自己下的，上面两个成员函数就是它的落点。

### 双重检查锁定：C++11 起才谈得上正确

假设您手上有一个初始化很贵的对象，配置要读文件，连接要握手，多数调用其实只想拿现成的。每次访问都加锁当然对，可锁的开销全摊在快路径上，九成九的调用都在替百分之一的初始化买单。检查、加锁、再检查，把锁往后挪，这就是**双重检查锁定**（double-checked locking pattern，简称 DCLP）的形状。它在 C++11 之前没法正确实现，Scott Meyers 与 Andrei Alexandrescu 2004 年的分析文章把旧标准的难处写全了，C++11 的内存模型给它补上了地基。咱们看能用的写法：

<OnlineCompilerDemo
  title="双重检查锁定：外层 acquire，锁内 relaxed，发布 release"
  source-path="code/examples/vol5/chapter05/17_dclp.cpp"
  description="八条线程各取 10 万次，指针一致、字段齐全。少写一行带回指针的翻车现场，见下方 AddressSanitizer 的回话。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// dclp.cpp —— 双重检查锁定：外层 acquire，锁内 relaxed，发布 release
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

struct Config { int id; int value; };
Config* raw_instance = nullptr;              // 真正的对象
std::atomic<Config*> published{nullptr};     // 发布口
std::mutex init_mutex;

__attribute__((noinline)) Config* instance() {
  Config* p = published.load(std::memory_order_acquire);        // 外层检查，acquire
  if (p == nullptr) {
    std::lock_guard<std::mutex> lock(init_mutex);
    p = published.load(std::memory_order_relaxed);              // 锁内复查，已持锁，relaxed 就够
    if (p == nullptr) {
      raw_instance = new Config{1, 42};                         // 构造全部完成才发布
      published.store(raw_instance, std::memory_order_release); // 桥从这里搭
      p = raw_instance;      // 发布后把指针带回来；第一版漏了这行，抢到锁的线程拿着 nullptr 回去
    }
  }
  return p;
}

const Config* seen[8] = {};

int main() {
  std::thread ts[8];
  for (int i = 0; i < 8; ++i) {
    ts[i] = std::thread([i] {                  // seen 是全局数组，lambda 里直接可见，不用捕获
      for (int k = 0; k < 100000; ++k) {
        const Config* p = instance();
        if (p->id != 1 || p->value != 42) {
          std::printf("线程 %d 读到不一致的对象\n", i);
        }
        seen[i] = p;
      }
    });
  }
  for (auto& t : ts) t.join();
  bool all_same = true;
  for (int i = 1; i < 8; ++i) if (seen[i] != seen[0]) all_same = false;
  std::printf("8 条线程各取 10 万次：指针一致=%d，id=%d value=%d\n",
              all_same ? 1 : 0, seen[0]->id, seen[0]->value);
}
```

</OnlineCompilerDemo>

```text
8 条线程各取 10 万次：指针一致=1，id=1 value=42
```

读数笔者跑了两遍，都一样。内存序的分工咱们摆一遍：外层检查用 acquire，读到非空就要保证构造完成对它可见，不然后面读字段就是读构造一半的对象。锁内复查用 relaxed，拿锁的动作已经替它搭好桥（5.3 数过的锁的同步关系），relaxed 只需要一个完整的读。发布用 release，构造的全部写入压在它之前。三问各就各位。

读数来得不容易，笔者把翻车的现场留给您。翻车当场，笔者盯着段错误愣了半天。第一版 demo 少写一行，锁内构造完、发布完，忘了把指针带回局部变量，抢到锁的线程拿着 nullptr 返回，程序当场就崩。AddressSanitizer 的报告两行就把位置点了出来：

```text
==43==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 ... in operator() /tmp/concurrency-u5/dclp.cpp:33
```

空指针解引用，33 行正是线程函数里取字段的地方。机器帮笔者抓住了，补上 `p = raw_instance` 一行，读数就齐。

笔者把它讲出来，是因为它正好量出了两种错误的边界。指针忘带回，本机抓得住，读数当场爆炸。内存序的错本机抓不住：您把实现里的 acquire 与 release 全换成 relaxed，x86 上大概率照旧全对，TSO 替您扛着写与读的次序。正确性的依据不是读数，是标准语义：release 发布让构造完成对 acquire 读可见，换掉桥，构造与使用之间没有 happens-before，按 [intro.races] 第 17 段就是 data race，未定义行为。弱内存模型的机器上 relaxed 版才真会读到构造一半的对象。那边的机器笔者没测过，本篇读数不覆盖，这句话笔者自己记着。

Meyers 单例在旁边等着收尾。块级 static 的初始化，标准替咱们管了并发（[stmt.dcl] 第 3 段：并发进入声明时，并发执行要等待初始化完成）：

<OnlineCompilerDemo
  title="块级 static：并发初始化由标准担保"
  source-path="code/examples/vol5/chapter05/18_meyers_singleton.cpp"
  description="config() 两次调用取到同一个对象，字段齐全。能写成块级 static 就不必手搓 DCLP，Config 沿用 dclp.cpp 的定义。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// meyers_singleton.cpp —— 块级 static 的并发初始化由标准担保
// （正文片段的补全版：Config 沿用 dclp.cpp 的定义，main 演示取到的是同一个对象）
#include <cstdio>

struct Config { int id; int value; };

Config& config() {
  static Config inst{1, 42};   // 并发初始化由标准担保
  return inst;
}

int main() {
  Config& a = config();
  Config& b = config();
  std::printf("两次取到同一个对象=%d，id=%d value=%d\n",
              &a == &b ? 1 : 0, a.id, a.value);
  return 0;
}
```

</OnlineCompilerDemo>

您能写成块级 static 的，就别手搓 DCLP。DCLP 的价值在形状本身，检查、加锁、再检查，延迟初始化任何资源都用得上，单例只是它最有名的雇主。

## 发布—订阅标志：relaxed 计数配 acquire-release（扩展）

本节带（扩展）标记，跳过它不影响主线。大伙想看进度，可以。可见性的层面，得由完成标志来扛，您 acquire 到 true，货才算真的到手。分工摆出来，就是 relaxed 与 acquire-release 同框的另一处：

<OnlineCompilerDemo
  title="发布订阅：relaxed 计数只报进度，可见性归标志"
  source-path="code/examples/vol5/chapter05/19_pubsub.cpp"
  description="done 的 release 与 acquire 把三份普通货物送过桥，消费者看到计数 3、货物 100、101、102。桥接通后读进度连 relaxed 都够。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// pubsub.cpp —— 发布订阅：relaxed 计数只报进度，acquire/release 标志承担可见性
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic<int> finished{0};    // 进度计数，relaxed
std::atomic<bool> done{false};   // 完成标志，release/acquire
int goods[3] = {0, 0, 0};        // 生产的三份货，普通变量

void producer() {
  for (int i = 0; i < 3; ++i) {
    goods[i] = 100 + i;
    finished.fetch_add(1, std::memory_order_relaxed);   // 只报进度
  }
  done.store(true, std::memory_order_release);          // 桥在这里搭
}

int seen_count = -1;
int seen_goods[3] = {0, 0, 0};

void consumer() {
  while (!done.load(std::memory_order_acquire)) {}      // 读到 release 写的值，桥接通
  seen_count = finished.load(std::memory_order_relaxed); // 桥接通后，读进度连 relaxed 都够
  for (int i = 0; i < 3; ++i) seen_goods[i] = goods[i];
}

int main() {
  std::thread p(producer);
  std::thread c(consumer);
  p.join();
  c.join();
  std::printf("消费者看到的计数=%d，货物=%d,%d,%d\n",
              seen_count, seen_goods[0], seen_goods[1], seen_goods[2]);
}
```

</OnlineCompilerDemo>

```text
消费者看到的计数=3，货物=100,101,102
```

分工的判据咱们过一遍。finished 的三次加法用 relaxed，读它的人少看见一两次更新没有损害，数字只是进度。done 的两端用 release 与 acquire，三份货物是普通变量，没有桥它们到不了对面。桥接通之后还有一个白送的推论，咱们单独拎出来。consumer 再读 finished，连 acquire 都不用，relaxed 就够。依据是桥的形状。写端 release 之前的全部写，对读端 acquire 之后的读全部可见。cppreference 讲 release 与 acquire 的小节里，原话就是这么写的。

写岔的形状笔者也摆在旁边，您对照着认：只用一个 relaxed 标志报完成。本机跑，大概率照样全对，TSO 帮忙。标准语义里没有桥，货物就可能在对面开工时还没齐。这类错偏要到弱内存模型的机器上才肯现形，又是那句，本篇读数不覆盖。

## fence 与 volatile 的边界

到这里，排序都绑在具体的原子操作上，release 长在 store 身上，acquire 长在 load 身上。fence（屏障，让编译器与 CPU 在一个位置上停住、前后的内存操作不许跨越）把排序从操作上挪下来，单独放在代码的一个位置。`std::atomic_thread_fence` 接一个内存序参数，C++11 起。本节是选讲内容，咱们只取判据要用的部分，跳过它不影响主线。出处分清楚，fence-atomic 同步与 volatile 辨析两块承自旧目录 ch03 的素材文《fence 与编译器屏障》，剩下的深度归选读。

咱们只讲两端都挂 fence 的接法。cppreference 在 `atomic_thread_fence` 的页上列了三种接法，fence 对原子、原子对 fence、fence 对 fence，demo 用的是 fence 对 fence。条件咱们数全：写端把 release fence 排在一个原子写之前，原子写用什么内存序都行。读端用 relaxed 读拿到值，再把 acquire fence 排在读的后面。两端凑齐，桥就接通，写端 fence 之前的写对读端 fence 之后的读可见。

<OnlineCompilerDemo
  title="fence 对 fence：排序约束从原子操作挪到 fence 上"
  source-path="code/examples/vol5/chapter05/20_fence_demo.cpp"
  description="发布端挂 release fence、订阅端挂 acquire fence，flag 的读写都降成 relaxed，消费者照样读到 payload=42。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// fence_demo.cpp —— fence-fence 同步：两端各挂一条 fence，排序约束从原子操作挪到 fence 上
#include <atomic>
#include <cstdio>
#include <thread>

int payload = 0;                 // 普通变量，可见性靠 fence 与 flag 共同垫起来
std::atomic<int> flag{0};

__attribute__((noinline)) void publish() {
  payload = 42;
  std::atomic_thread_fence(std::memory_order_release);   // 独立 fence 承担排序
  flag.store(1, std::memory_order_relaxed);              // relaxed store 就够
}

int received = 0;

__attribute__((noinline)) void subscribe() {
  while (flag.load(std::memory_order_relaxed) != 1) {}
  std::atomic_thread_fence(std::memory_order_acquire);
  received = payload;                                     // 依据标准语义，此处读到 42
}

int main() {
  std::thread p(publish);
  std::thread c(subscribe);
  p.join();
  c.join();
  std::printf("消费者读到的 payload=%d\n", received);
}
```

</OnlineCompilerDemo>

```text
消费者读到的 payload=42
```

指令的形状与 release 配 acquire 同构，排序的钱从 store 与 load 两头挪到了两条 fence 上，flag 的读写都降成了 relaxed。fence 本身的指令，咱们也抓了一份，本机 GCC 16 编出来：

```text
fence_release:  ret                      ← 函数体只剩返回，零指令
fence_acquire:  ret                      ← 同上
fence_seqcst:   lock orq $0x0,(%rsp)     ← 一条带 LOCK 前缀的指令
```

cppreference 在 Notes 里写明，x86 上 `atomic_thread_fence` 除 seq_cst 外一条 CPU 指令都不发，只影响编译期的代码移动，与咱们抓到的指令对得上。教科书画 seq_cst 档，常画成一条 mfence，本机 GCC 16 实际编出来的却是一条带 LOCK 前缀的 or。

本机上看不出差别，笔者如实交代。fence 值钱的地方不在省指令，在把付钱的时间点推到运行时确认之后。cppreference 的页上有一个信箱扫描的例子，用的是原子对 fence 的另一种接法，32 个信箱逐个 relaxed 读，只有读到属于自己的格子，才插 acquire fence，没命中的格子不花同步的钱。带内存序的原子操作做不到，acquire 长在读上，读了一次钱就付了。

判据笔者这么摆：默认写带内存序的原子操作，显式，桥好数。fence 留给同步位置确实要与操作分开的场面，用它的代价是数桥变绕，fence 与操作谁在前谁在后、读到底拿到哪次写，三个条件少一条就断，走查的时候容易漏。

volatile 的边界 5.2 判过一半，咱们在这里收拢。volatile 承诺的只有一件事：每次访问真的发生，编译器不合并不删。它的本职在硬件寄存器映射与信号处理。它不保证动作完整，不安排次序，不搭桥，跨线程的标志与计数用它，按 [intro.races] 第 17 段就是 data race。MSVC 历史上给 volatile 添了 acquire 与 release 语义，非标准扩展，代码要跨编译器就别依赖。跨线程的场面您现在有整套工具：动作完整找 atomic，次序找内存序，volatile 请回它的寄存器与信号处理。

## 挑档的判据，摆在一起

标题的问题该正面回答了。判据三问，每问一个完整句，您对着自己的代码逐个过：

1. 您要检查的读写之间，桥的两端连得上吗？写端落在 release（或者放锁），读端落在 acquire（或者拿锁），读端拿到写端写下的值，三节连成一条，acquire 与 release 就够，不用更贵的档。
2. 桥要护的是一次交接，还是几个原子变量互相咬合的次序？一次交接归 acquire 与 release，互相咬合归 seq_cst。5.3 的 litmus 与 5.4 中间档的读数，分界线就画在这里。
3. 这个操作站在链条的哪一段？不接链只递增，relaxed。读改写两头都接链，acq_rel。一时数不清，回到 seq_cst。

机器成本咱们也量了，本机 x86-64、GCC 16.2.1、-O2 的 objdump 读数摆成一张表：

| 动作 | relaxed | release / acquire | seq_cst |
|---|---|---|---|
| store | mov | mov | xchg |
| load | mov | mov | mov |
| 读改写 | lock xadd | lock xadd | lock xadd |
| fence | 无指令 | 无指令 | lock orq |

读改写一列含 acq_rel。弱内存模型的机器上，release、acquire 与 seq_cst 要付屏障的钱，本篇没有那边的机器，笔者给不出读数。

成本表底下还有一层不对称，咱们摆在明处。数不出桥的时候留在 seq_cst，多花几条指令。数错了桥，代价就是正确性，读数再干净也救不回来。判据偏向保守，是代价不对称逼的。

## 轮到您动手的时候

动笔改代码之前，笔者把这两篇攒下的问题列成一列，您对着自己的并发代码过一遍：

1. 您要检查的读写之间，数一遍桥，把两端写在注释里。写端 release、读端 acquire、读到写下的值，三节缺一节就要补。
2. 桥要护的是一次交接，还是几个原子变量的咬合？前者 acquire 与 release，后者留在 seq_cst。
3. 您的读改写两头各接在哪条链上？两头都要才配 acq_rel。
4. 您的计数器旁边还有没有别人要读的数据？没有，relaxed 才放得下心。
5. 代码库里还有 `memory_order_consume` 吗？换成 acquire。
6. 单例能写成块级 static 吗？能，就不必手搓 DCLP。

动手题两个。题目一是给 litmus 加一档，写端保持 release，读端降到 relaxed。笔者预跑过两遍，命中 109083 与 108575，量级与中间档几乎一样。您跑完想想为什么读数几乎不动：acquire 端缺席，桥本来就没搭起来，机器也没有多付钱。读数不变不等于桥还在。题目二把 stop_flag 的内存序全部换成 relaxed，跑一遍，读数照样是 64，然后数桥，数完您自己判断该不该合进去。

## 下一步

您现在回头看 [《同一个计数器，两条线程为什么算不对？》](../chapter02-sharing-data/01-from-wrong-programs)的 atomic_flag、5.2 的 `atomic_flag` 自旋锁演示、5.3 的 litmus 与同步关系表，代码里的 acquire 与 release，都能按判据数一遍了。

6.1《竞争找出来：重复测试与 TSan》讲重复测试与 ThreadSanitizer，本篇反复出现的那句“本机看不出”，要在那里拿到工具。翻页之前，把本篇动手题里的两档跑一遍，读数您自己核。

## 参考资源

实验的读数与架子记录交代一遍。篇末动手题加的一档笔者预跑过两遍，读数记在动手题里。fence_demo 的 42、stop_flag 的 64、pubsub 的 3 与 100、101、102，每次运行读数一致。DCLP 的两遍读数一致，翻车现场由 AddressSanitizer 定位。指令对照来自 objdump，地址列截掉。litmus 的架子与 5.3 相同，放行哨兵、应答只增不减、重置压在放行之前、绑核 0 与 8 号逻辑核。

断言核对用的 eel.is 草案是 2026-11-16 抓的，正文条款名都带着，段号随草案版本会挪。[stmt.dcl] 核了块级 static 并发初始化的等待条款。[intro.races] 的 data race 判定 5.3 已核，本篇复用。fence 的三种同步模式、x86 不发指令的 Notes 与信箱扫描的例子，按 cppreference 的 `atomic_thread_fence` 页核对。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，取的是单元排布与例子设计的思路。DCLP 的写法参考了两份材料，Jeff Preshing 的文章是一份，Meyers 与 Alexandrescu 2004 年的分析是另一份。借的是结构与讲法，例子和文字都是笔者自己写的。

- [std::memory_order（cppreference）][cppref-mo]，release-acquire 小节。
- [现行标准草案：Declaration statement（stmt.dcl）][draft-stmt-dcl]，块级 static 并发初始化的等待条款。
- [现行标准草案：Data races（intro.races）][draft-intro-races]，data race 判定，5.3 已核，本篇复用。
- [std::atomic_thread_fence（cppreference）][cppref-fence]，三种同步模式、x86 不发指令的 Notes、信箱扫描的例子。
- [Double-Checked Locking is Fixed In C++11（Jeff Preshing）][preshing-dclp]，DCLP 的写法参考。
- [C++ and the Perils of Double-Checked Locking（Scott Meyers, Andrei Alexandrescu）][meyers-dclp]，旧标准下 DCLP 为什么失败的分析。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-mo]: https://en.cppreference.com/w/cpp/atomic/memory_order
[draft-stmt-dcl]: https://eel.is/c++draft/stmt.dcl
[draft-intro-races]: https://eel.is/c++draft/intro.races
[cppref-fence]: https://en.cppreference.com/w/cpp/atomic/atomic_thread_fence
[preshing-dclp]: https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11
[meyers-dclp]: https://www.aristeia.com/Papers/DDJ_Jul_Aug_2004_revised.pdf
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
