---
title: "atomic_flag 与原子的边界：一次动作不等于一次业务"
description: "标准保证无锁的例外 atomic_flag：手工自旋锁与 ATOMIC_FLAG_INIT 的历史，一次动作与一次业务的边界怎么立，常见误认逐个判，浮点与自定义类型各卡在哪一关。"
chapter: 5
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
  - atomic
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# atomic_flag 与原子的边界：一次动作不等于一次业务

能担保的与不能担保的，前一篇画到了 CAS 这一站。这一篇看标准保证无锁的例外 atomic_flag，再把“一次动作”和“一次业务”的边界立起来，数一数常见的误认。

## atomic_flag：标准保证无锁的例外

`std::atomic<T>` 的操作是否无锁看平台，`std::atomic_flag` 是例外，cppreference 的原话是 it is guaranteed to be lock-free，标准保证它无锁。代价您也看得见，操作面极窄，`test_and_set` 把标志置真、交回旧值，`clear` 置假。C++20 之前连读一眼的操作都没有，`test()` 与 `wait()`、`notify_*` 都是 C++20 添的。

它的经典用场是一把手工自旋锁，[《同一个计数器，两条线程为什么算不对？》](../chapter02-sharing-data/01-from-wrong-programs)的饥饿演示里咱们已经见过它的形状（那边用的 `ATOMIC_FLAG_INIT` 写法），现在咱们把它单独跑一遍，两条线程各自加 100000 次：

<OnlineCompilerDemo
  title="atomic_flag 自旋锁：test_and_set 拿锁，clear 放锁"
  source-path="code/examples/vol5/chapter05/05_demo_flag.cpp"
  description="默认构造后 test() 读到 0，两条线程各加 100000 次，guarded 分毫不差地落在 200000。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_flag.cpp —— atomic_flag 自旋锁：标准唯一保证无锁的原子类型
#include <atomic>
#include <cstdio>
#include <thread>

std::atomic_flag busy{};      // C++20 起默认构造就是清除态；C++11 得写 ATOMIC_FLAG_INIT

void lock_flag() { while (busy.test_and_set(std::memory_order_acquire)) {} }
void unlock_flag() { busy.clear(std::memory_order_release); }

long long guarded = 0;        // 普通变量，保护全靠上面那把自旋锁

void work(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    lock_flag();
    ++guarded;
    unlock_flag();
  }
}

int main() {
  std::printf("flag 默认构造后 test(): %d\n", busy.test() ? 1 : 0);
  std::thread a(work, 100000);
  std::thread b(work, 100000);
  a.join();
  b.join();
  std::printf("两条线程各加 100000 次，结果 %lld\n", guarded);
  return 0;
}
```

</OnlineCompilerDemo>

```text
flag 默认构造后 test(): 0
两条线程各加 100000 次，结果 200000
```

200000 分毫不差。锁的用法、饥饿的毛病，2.1《同一个计数器，两条线程为什么算不对？》都量过了，咱们在这里补的是初始化的历史。C++11 时代，`atomic_flag` 想有确定的初值只有一条路，`std::atomic_flag f = ATOMIC_FLAG_INIT;`，别的写法落下的值未指定。C++20 起默认构造就是清除态，demo 里 `test()` 打出来的 0 就是这么来的，宏从此不必写，2.1 的旧写法照样能用。

`test_and_set` 与 `clear` 后面括号里的 acquire 与 release 是内存序的词眼，2.1 的样本也带着它们，咱们照着写就行，词眼归 5.4《内存序的四档：从顺序一致性到 relaxed》专讲。

> 插曲也记一笔：C++20 一度把这个宏标成废弃，缺陷报告 LWG 3659 又把它撤回，理由是某些平台上的 C 还需要它。您在 C 与 C++ 之间搬代码时，见到它别当成错误。

自旋锁这边，拿不到锁的线程原地空转，临界区短到纳秒级它才划算，长了烧 CPU，咱们回 `std::mutex`。C++20 的 `wait` 与 `notify_*` 能把空转换成挂起，cookbook 的 atomic_wait 配方还没成文，到时候咱们接着说。

## 一次动作与一次业务，边界画在哪

前一篇的操作都是一次动作，fetch_add 是一次读改写，CAS 成功是一次读改写，失败是一次读。[《同一个计数器，两条线程为什么算不对？》](../chapter02-sharing-data/01-from-wrong-programs)留的现场还没收拾，`atomic<int>` 计数器跑 8 次没有一次到 200000，因为 `counter = counter.load() + 1` 是三步。原子担保每一步完整，不担保三步连成一步。2.4《一次操作的边界在哪里》讲 check-then-act 收尾的那句就是本节的题面，原子只管单次动作，复合操作要的是把动作连起来。连的办法有两种，咱们各跑一遍。

咱们把场面摆回 2.4 的货架，只剩一件货，两位买家。库存换成 `std::atomic<int>`，每一步都原子，检查是一次 load，扣减是一次 load 接一次 store。编排好的窗口里，两位买家都看到 1，都动手：

<OnlineCompilerDemo
  title="检查与动手分成两次原子操作，货照样卖超"
  source-path="code/examples/vol5/chapter05/06_demo_boundary.cpp"
  description="库存换成 atomic<int>，两位买家都看到 1 都动手，结余 -1。CAS 循环把判断和扣压抑进一次读改写，一件货只一个人拿走。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
[naive  ] buyer B 动手，结余 0
[naive  ] buyer A 动手，结余 -1
[cas    ] buyer B 动手，结余 0
[cas    ] buyer A 重试看见 0，放弃
```

结余 -1，一件货卖给两个人，跟 2.3 里那版加了锁却把临界区切开的货架死法一样。您把每一步都换成原子操作，救不了边界划错的病，检查与动手之间那道窗口，原子管不着。

修法甲藏在 demo 的后半场，咱们把业务动作压成一次读改写。判断加扣减写不成一条 fetch_add，加减没有条件，CAS 循环兜得住，读旧值，值为 0 就走人，不为零就拿 CAS 换成旧值减一，失败重读重来。buyer B 买走了一件，buyer A 的 CAS 失败，重读看见 0，放弃，结余 0，一件货一个人。

修法乙是压不成的时候，锁回场。哪些压不成？咱们数一数。不变量横跨两个对象的，2.4 的钱包，A 减 B 加要一起成立。条件能复合的，[《有容量的队列：先让它站得住》](../chapter03-wait-and-coordinate/03-bounded-queue)的队列，空、满、关闭三个条件咬在一起。还有动手前后要调别的代码、失败要回滚别处的场面。原子与锁，分工不同，动作能压成一次的归原子，压不成的归锁或者改设计。

3.7《按协作需求选择工具》量峰值的仪表，咱们现在能读懂了：

<OnlineCompilerDemo
  title="峰值仪表：只许涨不许跌的 CAS 循环"
  source-path="code/examples/vol5/chapter05/07_peak_gauge.cpp"
  description="正文那三行原样长在 task 里：inside 记当前同时在线的任务数，量到更大 now 的人把 peak 顶上去。跑完 inside 归 0，峰值 4。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// peak_gauge.cpp —— 峰值仪表：只许涨不许跌的 CAS 循环（3.7 量峰值片段的补全版）
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

std::atomic<int> inside{0};    // 当前同时在线的任务数
std::atomic<int> peak{0};      // 峰值仪表：只许涨，不许跌

void task() {
  int now = ++inside;
  int old = peak.load();
  while (now > old && !peak.compare_exchange_weak(old, now)) {}
  std::this_thread::sleep_for(10ms);   // 在里面待一会儿，让并发叠起来
  --inside;
}

int main() {
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; ++i) ts.emplace_back(task);
  for (auto& t : ts) t.join();
  std::printf("4 个任务做完，inside=%d，峰值 %d\n", inside.load(), peak.load());
  return 0;
}
```

</OnlineCompilerDemo>

peak 只许涨不许跌，谁量到更大的 now，谁用 CAS 把它顶上去，输的人拿到最新的 peak，下一轮接着比。咱们把它的动作数一遍，一次读改写加循环保完整，锁都不用请。

## 常见的几种误认

旧目录 ch03 的素材文《atomic 操作》收过一节误认，咱们把它重写成现在的判定版本。

### volatile 不是 atomic

`volatile` 拦的是编译器优化，保证每次读写真的落到内存，不保证动作完整，也不保证次序。`++volatile_counter` 仍是读、加、写三步，两条线程一起来照样丢数，而且按 2.1 的四件事判下来，volatile 变量上的冲突访问没有原子豁免，就是 data race，标准判未定义行为。volatile 的本职在硬件寄存器映射与信号处理，跟多线程无关，想让它兼职原子，兼职那半是您自己脑补的。

### std::string 造不进 atomic

编译探针只有一行，咱们拿 `std::atomic<std::string>` 造一个对象，GCC 16 的回话：

```text
In file included from probe_str.cpp:1:
/usr/include/c++/16/atomic: In instantiation of ‘struct std::atomic<std::__cxx11::basic_string<char> >’:
probe_str.cpp:3:26:   required from here
    3 | std::atomic<std::string> s;
      |                          ^
/usr/include/c++/16/atomic:215:21: error: static assertion failed: std::atomic requires a trivially copyable type
```

卡住它的是什么，咱们交代清楚：**可平凡复制**（trivially copyable），复制这个类型等于把字节原样搬一遍，`std::string` 管着堆内存，搬家不是搬字节。C++11 的文本把它写成一条要求，违反没有诊断。C++20 起升格成编译期检查，不满足直接编不过。可平凡复制的自定义类型能编过，操作面只有 load、store、exchange、CAS，扩展节细说。

> 咱们把比较方式交代清楚，CAS 按字节比，[atomics.types.operations] 注 4 的原话，是 memcmp 加 memcpy 的效果。它不调用您的 operator==，也不调用拷贝构造。自定义类型当原子用，相等就是字节相等。

### 默认构造拿到什么值

咱们实测一眼：

<OnlineCompilerDemo
  title="C++20 起默认构造拿到确定的零值"
  source-path="code/examples/vol5/chapter05/08_demo_init.cpp"
  description="atomic<int> 默认构造后 load 出 0，atomic_flag 默认构造后 test() 出 0，都是 C++20 才有的确定起点。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

```text
atomic<int> 默认构造 load: 0
atomic_flag 默认构造 test: 0
```

两个都是确定的起点，这是 C++20 的行为。历史的走向容易读反，C++11 的文本里，默认构造连 T 对象都不建，读到什么没有定义。LWG 2334 把条款修正成“用 T() 值初始化”，libstdc++ 与 libc++ 到 C++20 模式才照改，cppreference 在构造函数页记了这段。所以老代码惯用带初值的构造 `std::atomic<int> c{0}`，这个习惯您接着用没有任何损失，两种写法在新标准下都得到确定的起点。

### 每个操作都原子，两个变量合起来仍没有次序

素材文里的例子值得再看一次，线程一做 `x.store(1)` 接 `y.store(2)`，线程二读 y 再读 x。两个变量各自读写完整，线程二却可能读到 y 是 2、x 是 0。默认的 seq_cst 下读不到这个场面，它要求所有 seq_cst 操作排成一条全序，x 的写必须落在 y 的读前面，把读放松到 relaxed 才可能。为什么放松、怎么判，归 5.3《一个线程怎样观察另一个线程的修改》与 5.4《内存序的四档：从顺序一致性到 relaxed》，这里笔者只把边界立住，原子的完整是按单变量说的，两个变量之间谁在前谁在后，本篇的工具给不了答案。

## 浮点与自定义类型的原子边界（扩展）

本节带（扩展）标记，您跳过它不影响单元主线。给浮点计数器、给自定义结构上原子的朋友，边界就在下面两小节里。

### 浮点：C++20 起能原子加减，每步一次舍入

C++20 给 `atomic<float>`、`atomic<double>`、`atomic<long double>` 配了 fetch_add 与 fetch_sub，乘除没有。咱们把语义上的关键放在舍入上，每次 fetch_add 都按浮点加法算一遍，再舍入一次。demo 从 2 的 24 次方减 1 起步：

<OnlineCompilerDemo
  title="浮点原子加法：每一步都完整，合起来数字没动"
  source-path="code/examples/vol5/chapter05/09_demo_float.cpp"
  description="从 16777215 加 1 动了一步，再原子加 100 次原地不动。四条线程各加 100 万次的对照段里 float 卡住，long long 数满。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
>

```cpp
// demo_float.cpp —— 浮点原子加法：每一步都完整，合起来数字没动
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

int main() {
  std::atomic<float> f{16777215.0f};          // 2 的 24 次方减 1
  float r1 = f.fetch_add(1.0f);
  std::printf("16777215 + 1 -> %.0f（交回旧值 %.0f），这一步动了\n", f.load(), r1);
  for (int i = 0; i < 100; ++i) f.fetch_add(1.0f);
  std::printf("再原子加 100 次 1.0 -> %.0f，卡住了\n", f.load());

  std::atomic<float> acc{16777216.0f};        // 四条线程各加 100 万次 1.0
  std::atomic<long long> ref{16777216LL};
  std::vector<std::thread> ts;
  for (int t = 0; t < 4; ++t) {
    ts.emplace_back([&] {
      for (int i = 0; i < 1000000; ++i) {
        acc.fetch_add(1.0f);
        ref.fetch_add(1);
      }
    });
  }
  for (auto& t : ts) t.join();
  std::printf("4 条线程各加 100 万次后：float=%.0f   long long=%lld\n", acc.load(), ref.load());
  return 0;
}
```

</OnlineCompilerDemo>

```text
16777215 + 1 -> 16777216（交回旧值 16777215），这一步动了
再原子加 100 次 1.0 -> 16777216，卡住了
4 条线程各加 100 万次后：float=16777216   long long=20777216
```

第一加真的动了，16777216 可以表示，结果落在它身上。后面的加全部原地，float 的尾数只有 24 位，16777217 装不下，就近舍回 16777216，每一步都完整，每一步都白加。四条线程各加 100 万次的对照段更直白，float 原地不动，long long 数到 20777216。cppreference 在 fetch_add 页补过 C++20 的两条注，咱们照录，结果超出可表示范围时，值未指定但没有未定义行为。浮点环境可能与调用线程的不同。要给浮点计数，咱们把最小单位换成整数（以厘记、以纳秒记），原子加整数，读的时候再换算，这是最省心的死板办法。

还有一个字节比较的边角，咱们拿它单独对一遍。CAS 按值表示比，在遵从 ISO/IEC 60559 的实现上，-0.0 与 +0.0 用 `==` 相等，按字节不等。同 payload 的 NaN 按字节相等，`==` 不等（[atomics.types.operations] 注 6）。拿 CAS 判浮点相等的朋友，判法得跟着字节走。

### 自定义类型：过了可平凡复制，只有四个操作

咱们拿 `Tick` 开刀，它长这样：

<OnlineCompilerDemo
  title="自定义类型过了可平凡复制，只剩四个操作"
  source-path="code/examples/vol5/chapter05/10_atomic_tick.cpp"
  description="Tick 过了可平凡复制的检查，atomic<Tick> 能构造，load、store、exchange、CAS 逐一在跑。fetch_add 编不过，探针回话见下方。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

两个字段，可平凡复制，static_assert 通过，`std::atomic<Tick>` 能构造，操作面就是前面数过的四个，load、store、exchange、CAS。想给它来一手 fetch_add，比如推进 seq，咱们把探针喂给编译器，回话在下面：

```text
probe_fetch.cpp: In function ‘int main()’:
probe_fetch.cpp:12:16: error: ‘struct std::atomic<Tick>’ has no member named ‘fetch_add’
   12 | int main() { t.fetch_add(1); return 0; }
      |                ^~~~~~~~~
```

没有 fetch_add，算术操作面只给整型特化。咱们用 CAS 循环照样能把任意读改写压成一次动作，上一篇 CAS 一节的全套办法都适用。字节层面还有两个注意，标准用注给了。从不参与值表示的填充位会被忽略（C++20 起），带 padding 的结构体不容易虚假失败。union 里有的位只参与部分成员的值表示，CAS 可能永远失败（[atomics.types.operations] 注 7、注 8）。

最后两个现成的出口，咱们一并交代。`atomic<std::shared_ptr<T>>` 是 C++20 的偏特化，主模板进不去的类型库亲自认识，想原子地换一份共享指针又不想自己管引用计数，走它。`std::atomic_ref`（C++20）给普通对象临时套原子操作，cookbook 的 atomic_ref 配方还没成文，到时候咱们接着说。

## 嵌入式视角：ISR、LDREX 与 64 位原子（扩展）

本节带（扩展）标记，您跳过它不影响本篇主线。内容面向在 MCU 上写并发的读者，三块的底稿收在旧目录的《atomic 操作》里，咱们重新装了一遍，次序从指令到锁再到中断。

### LDREX 与 STREX：原子操作在 M 核上怎么落地

M3、M4、M7 这类 ARMv7-M 内核配了一对独占访问指令，独占读 `LDREX` 与独占写 `STREX`。`LDREX` 把值读进来，顺手给这个地址立一道独占标记，`STREX` 把新值写回去，标记还在，写入成功，指令交回 0。有人插手，最典型的是中断打了进来，写入当场作废，交回 1，库里包着的那层循环拿这个返回值决定重来还是收工。这对指令的学名是 load-link/store-conditional，5.1《原子操作解决到哪一步：类型、操作与 CAS》讲 CAS 虚假失败时提过，SC 那半可能平白失败，weak 把失败照实报出来，strong 在内部补上重试，说的就是它们在 M 核上的真身。

标准库的原子操作在这些核上就是这么落地的。您调一次 `fetch_add`，编译器与运行库把它展开成读旧值、寄存器里加出新值、`STREX` 写回的一轮，写不进去退回重来，CAS 的比较加交换、`atomic_flag` 的 `test_and_set` 同样压在这对指令上。M0、M0+ 这类 ARMv6-M 内核连这对指令都没有，读改写得靠工具链另找路子，常见的是短促关中断护住那几条指令，细节各家不同，手册说了算。

### 64 位原子：M0/M3 上多半退到内部锁

5.1 的仪表上，16 字节的 `atomic<Wide>` 落在内部锁，x86-64 的 int 与 long long 都无锁。把同一套仪表搬到 32 位的 M 核上，落在锁上的成员就多了一类。M 核的独占访问只有字节、半字、单字三档宽度，8 字节的类型没有对应的指令，编译器退而求其次，把操作兜进运行库的内部锁，接口一点没变，拿锁、改值、放锁的次序藏在调用背后，`std::atomic<int64_t>` 的操作就不再是无锁的了。M0 与 M3 都在这条路上，M0 更紧一点，连 32 位的独占指令也没有。判断仍归那两个查询口管，`is_lock_free()` 跑一回，`is_always_lock_free` 在编译期问一遍。

问法与 5.1 是同一套，这儿把闸压在 long long 上：

```cpp
// lock_free_probe.cpp —— 换一套工具链，同一套问法照用
#include <atomic>
#include <cstdio>

int main() {
  std::atomic<int> ai{0};
  std::atomic<long long> all{0};

  std::printf("atomic<int>: %s\n",
              ai.is_lock_free() ? "lock-free" : "uses lock");
  std::printf("atomic<long long>: %s\n",
              all.is_lock_free() ? "lock-free" : "uses lock");

  // 编译期的闸：long long 在这个平台上不是无锁的，直接编不过
  static_assert(std::atomic<long long>::is_always_lock_free,
                "int64 atomics must be lock-free on this platform!");
  return 0;
}
```

本机 x86-64 上这段稳稳编过，两条打印都报 lock-free。同一份文件交给 ARM 的工具链，读数与那条闸的判定都会跟着平台走，5.1 交代过，拿不准就问它，别背答案。板上读数笔者没有量过，这节交代的是机制，数字归您手上的工具链交代。

### ISR 里的告诫：带锁的原子操作会死等

中断服务程序（ISR）里别碰带锁的原子操作，这一条单独拎出来。ISR 不能阻塞，锁实现的原子操作却要拿到锁才动手，两件事对上了就是死等。单核板上这个场面不需要编排，主程序的 `fetch_add` 做到一半，内部锁刚拿到手，中断打进来，ISR 里也动同一个变量，运行库照样先拿锁，锁在谁手里，在被这条中断压下去的主程序手里，它得等中断返回才有机会放锁，ISR 就停在拿锁那一步，锁等不到，中断退不出去，整个系统卡死在一条 `fetch_add` 上。这条死等是按机制推演出来的，笔者没在板上演过它，推演的每一步都落在机制上。

本篇的主角在这儿有个天然的优势，`atomic_flag` 的 `test_and_set` 与 `clear` 无锁是标准担保的，操作那一层不经过任何锁。豁免有边界，拿它拼的自旋锁照旧有持锁者，主程序拿着锁的那一会儿中断打进来，ISR 里的 `test_and_set` 本身照样完成，围着它的 while 循环还是转着等。标准豁免的是操作那一层，持锁的纪律得您自己守。

所以中断路径上的原子变量，动笔之前确认它真的无锁，确认的法子本篇末尾问题清单的第 4 条备好了，`is_always_lock_free` 加 `static_assert`，编译期就把带锁的特化拦下来，这道闸立在源码里，换平台、换编译选项都得先过这一关。

## 轮到您动手的时候

动笔改代码之前，笔者把这两篇攒下的问题列成一列，您对着自己的共享变量过一遍：

1. 您要保护的业务动作，写成内存动作是几步？每一步各自完整吗，步与步之间谁护着？
2. 它能压成一次读改写吗？您能压，就用 fetch_add 或者 CAS 循环。压不成，锁回场，或者改设计，别硬压。
3. 您循环里的 CAS 用的是 weak 吗？循环外的一次性判断换 strong 了吗？
4. 您的原子变量真的无锁吗？`is_always_lock_free` 一问便知，关键路径上用 static_assert 压住。
5. 浮点计数您换整数最小单位了吗？自定义类型的字段里有没有 padding，有没有 union？

翻页之前，笔者给您留一个动手题。把 2.1 里 `atomic<int>` 计数器那版程序翻出来，修两版，一版用 fetch_add，一版用 CAS 循环，循环里加个计数器数重试。跑 30 轮，看两版的结果是不是都齐，CAS 版的重试次数落在什么范围。

## 参考资源

实验的现场记录交代一遍。两个编译探针的回话照录在上文。卖超 demo 的场面是编排出来的，线程次序写死，读数可复现。demo_flag 的 200000、demo_float 的整型对照与卡住的读数、demo_init 的零值，每次运行都一致。正文方括号里的是草案小节名，小节名跟正式标准一致，段号可能挪。cppreference 的 atomic_flag、fetch_add、构造函数、ATOMIC_FLAG_INIT 四页都抓到了全文，与草案对读无出入。推进顺序参照了 Anthony Williams 的《C++ Concurrency in Action》，例子与文字都是笔者自己写的。

- [std::atomic_flag（cppreference）][cppref-flag]，保证无锁的说明，C++20 的 test 与 wait/notify。
- [std::atomic 的 fetch_add（cppreference）][cppref-fetchadd]，浮点特化的 C++20 注与有符号整数的回绕。
- [std::atomic 的构造函数（cppreference）][cppref-ctor]，默认构造的值初始化与 LWG 2334。
- [ATOMIC_FLAG_INIT（cppreference）][cppref-afi]，C++20 起默认构造取代它，LWG 3659 撤回废弃。
- [Class template atomic（现行草案 atomics.types.generic）][draft-generic]，可平凡复制等编译期要求，整型、浮点、指针与智能指针特化的分工，bool 特化走主模板的注。
- [Operations on atomic types（现行草案 atomics.types.operations）][draft-ops]，CAS 的效果与虚假失败注、is_lock_free 的说明、wait 与 notify。
- [Partial specializations for smart pointers（现行草案 util.smartptr.atomic）][draft-smartptr]，shared_ptr 与 weak_ptr 的原子偏特化。
- [Multi-threaded executions and data races（cppreference）][cppref-races]，data race 判定的回顾，2.1 核过的条款。
- [C++ Concurrency in Action, 2nd Edition（Anthony Williams）][williams]，推进顺序参考。

[cppref-flag]: https://en.cppreference.com/w/cpp/atomic/atomic_flag
[cppref-fetchadd]: https://en.cppreference.com/w/cpp/atomic/atomic/fetch_add
[cppref-ctor]: https://en.cppreference.com/w/cpp/atomic/atomic/atomic
[cppref-afi]: https://en.cppreference.com/w/cpp/atomic/ATOMIC_FLAG_INIT
[draft-generic]: https://eel.is/c++draft/atomics.types.generic
[draft-ops]: https://eel.is/c++draft/atomics.types.operations
[draft-smartptr]: https://eel.is/c++draft/util.smartptr.atomic
[cppref-races]: https://en.cppreference.com/w/cpp/language/multithread
[williams]: https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition
