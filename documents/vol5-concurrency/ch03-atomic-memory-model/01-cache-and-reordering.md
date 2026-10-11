---
title: "缓存与重排：机器真的会乱序"
chapter: 3
order: 1
description: "从锁的世界换到机器的世界：编译器的 as-if 自由、CPU 的 store buffer、MESI 缓存一致性，再看 C++ 内存模型这份跨平台合同，把次序在哪里丢的看清楚"
tags:
  - host
  - cpp-modern
  - intermediate
  - atomic
  - memory_order
  - 内存管理
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
reading_time_minutes: 18
prerequisites:
  - "数据竞争与 ThreadSanitizer 第一课"
  - "同步原语工具箱"
related:
  - "原子操作与 happens-before"
  - "内存序"
---

# 缓存与重排：机器真的会乱序

[同步原语工具箱](../ch02-mutex-condition-sync/06-sync-primitives-toolkit.md)收尾的时候咱们说定了，锁与等待的工具都配齐了，卷内的下一站是换地基，第 3 章接管 happens-before 的正式定义。您现在就站在第 3 章的门口。公理这件事咱们往后放一放，本篇只带您去看机器。理由倒是不复杂，happens-before 的每一条，约束的都是机器的某一种自由。没亲眼见过这些自由的读者，读到的公理只是几个干巴巴、掂不出分量的名词。

乱序的来源不止一处，咱们要盯的机器有两台：编译器与 CPU。您写的语句顺序，到了机器码里可以被改得面目全非，这就是编译器的手笔。把编译器按住了也不行，指令进了核，对外可见的顺序照样可能不是您写的顺序，这是 CPU 的手笔。缓存系统还得单独给咱们留一节，因为解释里流传最广的那个，把乱序的根子安在了多核缓存不一致上，它偏偏是不对的，对的说法咱们会在本篇里给全。

路线咱们也交代清楚：咱们拿一个求和循环量出编译器的自由，再钻进汇编里看它合并访存的样子，然后用一个两线程的小程序把 CPU 的乱序逼出来，把这个名叫 store buffer 的实物来源讲清楚。跟着咱们补上缓存分层与 MESI，把一致性（所有核最终看到同一份数据）与有序性（您的写什么时候对别人可见）分成两件事，末了把 C++ 内存模型请出来，它是一份跨平台的合同，x86 和 ARM 只是两种兑现的法子。

本篇的实测数字全部带着复现命令。实测都在笔者自己的 WSL2 Arch Linux（内核 6.18、g++ 16.2.1、AMD Ryzen 7 9700X）上跑出来，完整的代码放在 `code/volumn_codes/vol5/ch03-atomic-memory-model/`，正文里咱们只贴关键的片段，读法的约定在[卷首](../)立着，咱们照它来。

## as-if 规则：编译器的自由

咱们写代码的时候，心里默认的是源代码的顺序，也就是程序执行的顺序。标准其实从来没承诺过这句话，它承诺的是另一句，[intro.abstract] 条款的原文是这样的：

> `Rather, conforming implementations are required to emulate (only) the observable behavior of the abstract machine as explained below.`（咱们把它译过来：实现需要模拟的，只是抽象机的**可观察行为**）

脚注里给它起的名字就是 as-if 规则：只要结果“如同”遵守了规则，实现想怎么改咱们都管不着，这个口气咱们后面会反复见识到。

可观察行为指的是什么？咱们把同一处条款列出的三条摆出来：对 `volatile` 对象的访问严格按抽象机的规则求值，写进文件的数据要如实写出，交互设备（典型如终端）的提示输出得排在等待输入之前完成。除了这几样，机器码层面怎么动都行：指令可以换位置，相邻的两次访问可以合成一次，整段循环甚至能换成一个现成的算式。判断的标准只有一条，就是**单线程的可观察行为不变**。

> 咱们把术语卡立在这儿：**as-if 规则**。它是标准与实现之间的一条约定：实现只需要保证程序的可观察行为和抽象机一致，内部的变换不受任何限制。编译器的重排、合并、删除，合法性都是从它来的。

这句话在单线程的世界里听着无害，换个角度您就能看见牙了：您的程序顺序，编译器是不认的。能约束它的只有可观察行为，而可观察行为的清单里，偏偏没有普通变量的访问顺序这一项，所以并发场景里，这正是次序丢掉的第一处。

`volatile` 的位置咱们也给您说清：它进了可观察行为的清单，指的是对它的访问不许被合并、也不许被省略。不过它不管原子性，也不管一个线程的写什么时候对另一个线程可见，所以拿 `volatile` 当线程同步用，是流传很久的老错误。正解记在[原子操作与 happens-before](./02-atomics-and-happens-before.md) 的那一篇里，本篇就不展开了。

### 同一个循环的两种命运

空口说自由是没意思的，咱们量给您看。程序跑两个等价的求和循环，关键的部分咱们整段贴出来：

```cpp
std::uint64_t g_sink = 0;
constexpr long kN = 50'000'000;

__attribute__((noinline)) void free_transform() {
    // 没有屏障：编译器可以跨迭代重排、合并，甚至把整个循环换成闭式算式
    for (long i = 0; i < kN; ++i) g_sink += static_cast<std::uint64_t>(i);
}

__attribute__((noinline)) void fenced_transform() {
    // 每圈一道编译器屏障：跨迭代的访存合并与搬运全被掐住
    for (long i = 0; i < kN; ++i) {
        asm volatile("" ::: "memory");
        g_sink += static_cast<std::uint64_t>(i);
    }
}
```

两个函数的差别只有一行：`fenced_transform` 每圈加了一道 `asm volatile("" ::: "memory")`，这是 GCC 与 Clang 的写法：一条不生成任何指令的空内联汇编，只是告诉编译器这道墙两边的访存不许互相搬运、也不许合并，它起的作用是纯编译器屏障。`noinline` 防止两个函数被内联进计时函数、搅浑了对比。两边加的都是 0 到 49999999，加完了打印总和与耗时，您点“动手试一试”，您自己跑一遍：

<OnlineCompilerDemo
  title="动手验证：同一个求和循环，放任与按住编译器的两种耗时"
  source-path="code/examples/vol5/52_compiler_reorder_probe.cpp"
  description="两个等价的求和循环：一版放任编译器优化，一版每圈加一道编译器屏障。两版 sum 相同（as-if 规则的现场），耗时差五个数量级上下——被闭式化的一版比纳秒级大不了多少，被按住重排的一版要几十毫秒。多跑几遍耗时会波动，量级不会变。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者在同一台 WSL2 上的实测（`-O2 -std=c++20`）：

```text
free  version: sum=1249999975000000  127 ns
fence version: sum=1249999975000000  64407535 ns
same result: yes
```

三个数咱们挨个看。两边的 sum 一模一样，这行输出本身就是 as-if 规则的现场：变换不许改变可观察行为，结果必须一致。耗时的对比更吓人：127 纳秒对 64407535 纳秒，差到五十万倍上下去了。五千万次的累加只用了 127 纳秒，比纳秒级大不了多少——它其实不是跑得快，是循环真的没了。GCC 认出了归纳变量的形状，把 0+1+…+49999999 折成一个编译期就算好的常数，到运行时就只剩一次加法了。练习 1 会请您去机器码里亲眼数一数，咱们在这里把解释立住。

被按住的那版为什么是 64 毫秒？您看，每圈拦着一道编译器的屏障，跨迭代的合并与搬运全断了，编译器就只好一圈一圈老老实实地累加。咱们拿 64407535 纳秒除以五千万圈，平均一圈只有 1.3 纳秒的样子，这正是循环完全没被优化时的正常身价。两版的可观察行为一模一样，机器码却天差地别，中间的那道缝，就是编译器的自由。

分寸上咱们再交代一句：这个实验量的是“自由的价格”，而不是重排本身。重排的实物证据在汇编里，咱们这就去看。

### 汇编里亲眼看到合并

咱们把两个函数的关键行贴出来，完整的源文件在代码仓 `code/volumn_codes/vol5/ch03-atomic-memory-model/compiler_reordering_asm.cpp`：

```cpp
int g_flag = 0;
int g_data = 0;

void merge_two_stores(int v) {
    g_flag = v;      // 两次写中间没有任何读
    g_flag = v + 1;
}

int double_load() {
    int a = g_data;
    g_flag = 1;       // 与 g_data 无关的写夹在中间
    return a + g_data;
}
```

`-S` 的意思是让编译器停在汇编这一步，`-O2` 跟前面用的是同一个优化档。命令咱们照旧在仓库根目录跑：

```bash
g++ -std=c++20 -O2 -S \
  -o /tmp/compiler_reordering_asm.s \
  code/volumn_codes/vol5/ch03-atomic-memory-model/compiler_reordering_asm.cpp
grep -A6 "merge_two_stores\|double_load" /tmp/compiler_reordering_asm.s
```

笔者机上跑出来的结果，指令是逐条照贴的，两个标签行里的 `#` 注释是咱们加的：

```text
_Z16merge_two_storesi:          # g_flag = v; g_flag = v + 1; 两条写合成一条
    addl    $1, %edi
    movl    %edi, g_flag(%rip)
    ret

_Z11double_loadv:               # int a = g_data; g_flag = 1; return a + g_data; 两次读合成一条
    movl    $1, g_flag(%rip)
    movl    g_data(%rip), %eax
    addl    %eax, %eax
    ret
```

两个函数咱们挨个读。`merge_two_stores`：源代码写了两次 `g_flag`，机器码里给它留下的只有一条 `movl`，头一次的写被整个删了。单线程里没有人读得到它的中间值，第二次的写又必然覆盖它，所以删掉它毫无损失。`double_load` 的结果，咱们得把话说准：GCC 做的并不是“把读搬到了写前面”的搬运，而是**把两次读合成了一次**，对 `g_data` 只留下了一条 `movl`，取回的值加上它自己当了返回值。合成以后的 load 落在了 store 后面，那是合并顺手带的。您要管它叫重排也行，但您心里得清楚，合才是这里的主角。

这些变换为什么合法？咱们拿 as-if 的规则来验。单线程里的 `g_data` 不可能在两次读之间变，读一次读两次的结果相同，`g_flag` 的中间值没有观察者，删了也就删了。可一旦有了第二条线程，“中间值没有观察者”的前提就塌了，另一个线程可能恰好在两次读之间写了 `g_data`。多线程场景下的合并意味着什么、合法不合法怎么判，得等 happens-before 立了法才谈得清，这套判据的正式版本在[原子操作与 happens-before](./02-atomics-and-happens-before.md) 那一篇，本篇就不展开了。

这个没有 `main` 的源文件，咱们不拿来跑，只喂给编译器看它的输出。您要是想看合并发生以前的样子，把 `-O2` 换成 `-O0` 再生成一遍：`merge_two_stores` 里会是老老实实的两条 `movl`，`double_load` 里是两次读夹着一次写的形状。合并是优化档带来的变换，不是编译器天生的形状，您对照着看一遍，对自由两个字的把握会更实。

## store buffer：CPU 的现场

编译器看完了。您可能会想：那把优化关掉，或者用屏障把编译器也按住了，程序顺序总该保住了吧？按住了编译器，CPU 这一头也有自己的自由，就算机器码一字不差地按您写的顺序排着，进了核之后，对外可见的顺序照样可能不是您写的顺序。

### 协议：写完自己的，就去读对方的

咱们用内存模型研究里最经典的一类小程序来逼它现形。这类程序在文献里的名字是 litmus test，直译是石蕊试验：咱们写好一个具体的输入与一个具体的结局，拿去问某种机器：这个结局你给不给。协议是这样的：两个线程，各自把自家的标志写下去，写完就去读对方的标志（写和读各带一个可换的内存序参数，咱们下文把它记作 `MO`）：

```cpp
// 线程 T1                      // 线程 T2
x.store(1, MO);                 y.store(1, MO);
out1 = y.load(MO);              out2 = x.load(MO);
```

`x` 和 `y` 的起始值都是 0，一轮跑完咱们看 `out1`、`out2`，能数出的结局有四种：两边都读到 1 的、只有一边读到的，还有双方都读到 0 的 `(0,0)`。前三种都是好理解的，`(0,0)` 最扎眼：T1 读到的 `y` 是 0，说明它读的时候 T2 的写还没到，T2 读到的 `x` 也是 0，说明 T1 的写也没到，于是两个写就被对方的读双双错过了。

真跑起来的程序比这两行多一层外壳，关键的部分咱们贴出来：

```cpp
template <std::memory_order MO>
long run_litmus() {
    std::atomic<int> x{0}, y{0};
    std::atomic<long> gate{0}, done1{0}, done2{0};
    std::atomic<int> out1{0}, out2{0};
    long both_zero = 0;

    std::thread t1([&] {
        long seen = 0;
        for (;;) {
            long r;
            while ((r = gate.load(std::memory_order_acquire)) == seen) {}
            if (r < 0) return;
            seen = r;
            x.store(1, MO);
            out1.store(y.load(MO), std::memory_order_relaxed);
            done1.store(r, std::memory_order_release);
        }
    });
    // t2 与 t1 对称：写 y、读 x

    for (long r = 1; r <= kRounds; ++r) {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
        gate.store(r, std::memory_order_release);
        while (done1.load(std::memory_order_acquire) < r ||
               done2.load(std::memory_order_acquire) < r) {}
        if (out1.load(std::memory_order_relaxed) == 0 &&
            out2.load(std::memory_order_relaxed) == 0)
            ++both_zero;
    }
    gate.store(-1, std::memory_order_release);
    t1.join();
    t2.join();
    return both_zero;
}
```

外壳的工作方式是回合制。您盯着 main 的每一轮看：`x`、`y` 被清零，轮号从 `gate` 放出去、两个工人线程跑各自的两句、把结果交回 `done`，main 收齐了再检查两个 out，两边都是 0 就给 `(0,0)` 的计数加一。您顺着 `gate` 和 `done` 两个变量看，放行以前清零一定完成了，收卷之后结果也一定写好了，回合的边界就管到这儿。被试的东西只有一处：模板参数 `MO`，咱们拿 `relaxed` 与 `seq_cst` 各跑一个阶段。`gate` 与 `done` 自己用的是另外两种内存序（release/acquire，六种里的两种，正源在[内存序](./03-memory-ordering.md)的那一篇里），它们只负责划定回合的边界，不约束回合内 `x` 与 `y` 的顺序，所以咱们测的就是 `MO` 的两处用法。

有位心细的朋友可能要问：四个变量为什么非得是 `atomic`？换成普通 `int` 不是更贴近底层吗？偏偏不行。一换咱们就有两条线程在同时读写同一个普通变量，那就落回了[第 0 章判过的 data race](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)，标准直接把它判了未定义行为，编译器和 CPU 想干什么都合法，咱们就没法把观察到的结局归因给任何机制了。`relaxed` 的原子操作是合法的无竞争程序，它保证了读写不可分割，偏偏不承诺任何次序，咱们要测的正是这一次序的自由度，它是合法但不保证次序的最小探针。

咱们动笔算一算 `(0,0)` 该不该存在。笔算得有个立足的世界，咱们就选**顺序一致性**（sequential consistency，SC）：所有核的所有操作排进一条全体共见的总顺序，而且每条线程自己的操作在这个总顺序里保序。咱们写源代码的时候，直觉里默认的就是这个世界。

在顺序一致的世界里 `(0,0)` 是出不来的，咱们用反证走一遍。假设双方都读到了 0，再加上两条程序序的边——T1 写在前读在后，T2 也是一样的——四条边就凑齐了：

```text
T1 读 y 见 0  ⇒  T1 读 y 排在 T2 写 y 之前
T2 读 x 见 0  ⇒  T2 读 x 排在 T1 写 x 之前
T1 程序序    ⇒  T1 写 x 排在 T1 读 y 之前
T2 程序序    ⇒  T2 写 y 排在 T2 读 x 之前
```

咱们把四条边接成一个圈：T1 写 x 接 T1 读 y 接 T2 写 y 接 T2 读 x，末了这个圈又接回了 T1 的写，它排到了自己后面，这就矛盾了。所以顺序一致的世界里 `(0,0)` 一个都不该出现，这就是咱们的笔算判据。这套四条边的推法值得您练熟，练习 2 咱们还要再用它一次。

写完自家标志就去读对方标志的形状，不是咱们编的教具。它是 Dekker 互斥算法（1960 年代最早的一批互斥算法之一）的核心一步，那一代机器是顺序一致的，算法的正确性证明就立在 SC 上。放到今天的 x86 上裸跑，`(0,0)` 这样的结局一旦冒头，互斥就被破了——顺序一致的世界里被证明不可能的事，真实机器亲手把它造了出来，这正是本篇标题说的事。

那机器怎么说？开跑以前咱们押个注：您按顺序一致性的直觉，猜猜 `(0,0)` 会有多少轮？猜完了您就点“动手试一试”。

<OnlineCompilerDemo
  title="动手验证：先写自己的、再读对方的，双方都读到 0"
  source-path="code/examples/vol5/51_store_buffer_litmus.cpp"
  description="两个线程各自写自己的标志、再读对方的标志，二十万轮统计双方都读到 0 的次数。relaxed 阶段 (0,0) 几乎必现，且每次运行的计数差异很大；seq_cst 阶段恒为 0。多跑几遍：非零是常态，比例当不了常数。"
  run-options="-O2 -std=c++20 -pthread"
  allow-run
/>

笔者连着跑了两次（每次运行含 relaxed、seq_cst 各一个二十万轮的阶段）：

```text
rounds per phase: 200000
relaxed  (0,0) outcomes: 156281
seq_cst  (0,0) outcomes: 0

rounds per phase: 200000
relaxed  (0,0) outcomes: 24127
seq_cst  (0,0) outcomes: 0
```

您看 relaxed 的输出：同一段程序跑了两遍，两次的计数差得很远：156281 对 24127，差出了六倍多。`(0,0)` 的出现是常态，比例是波动的，它取决于调度的安排，也取决于两个线程当时落在了哪些核上。**非零的出现是常态，比例当不了常数**，这个口径往后引用任何重排实验的时候都要带着。再看 seq_cst 的输出，两行的输出都是 0：程序一行没改，只把两处访存的内存序参数从 `relaxed` 换成了默认的 `seq_cst`，`(0,0)` 就绝迹了。这个对比您记下来，机关咱们在讲完机制之后揭晓。

### 次序是在哪儿丢的

咱们这就把实物来源请出来：每个核私有的 store buffer。

> 术语卡咱们再立一张：**store buffer**（存储缓冲）。它是挂在每个核前端的私有队列，写的头一站就是它：写落进了队列，核转头就能去干别的活，把写送进缓存系统的任务留给队列慢慢挑时候干。它的深度有限，装几十条写就满了。

写进 store buffer 的那一刻起，核自己已经认为写完成了，不过这个写**还没进缓存系统，别的核看不见**。要害在后面的那步，您注意排在写后面的 load：它读的若是别的地址，不等 buffer 排空就去缓存里取了。于是在同一个核的视角里，写 x=1 和读 y 是按程序序发出去的。可对别的核来说，x=1 变成可见的时间，被 buffer 拖到了 y 被读走之后。两个核同时这么干：T1 的 x=1 在 buffer 里排队，T1 的 y 读到 0，T2 的 y=1 也在 buffer 里排队，T2 的 x 读到 0，`(0,0)` 就是这么来的。

咱们还有半句得补上，不然您会冤枉 CPU 改了您的程序序：**本地的视野从来不乱**。核读自己刚写的地址，硬件会直接从 store buffer 里把刚写的值递出来，走的就是 store forwarding（写前递），所以单看一个核，写在前读在后、读到什么，都与程序序是一致的。乱的不是核自己的执行，是写**对外可见的时间**——每个核自己的顺序都对，两个核互相看的顺序对不上。这类排在后面的 load 越过了排在前面、还没对外的 store 的换序，它的名字叫 StoreLoad 重排，是 x86 允许的唯一一种。

窗口的尺寸小得很：写从 buffer 落进缓存通常只要几十个周期，一眨眼就关上了。可这么小的概率，架不住一秒几十亿个周期的基数——光二十万轮里它就出了两万多次。这也解释了这类问题为什么难缠：窗口稍纵即逝，您测试跑一百遍可能一遍都碰不上，到了生产环境的高频路径上它才冒头。

::: details 把一轮 (0,0) 的时间线摆开

咱们让 T1 和 T2 分别跑在核 A 与核 B，按步走一遍最典型的坏结局：

1. 两个核同时收到回合放行。
2. 核 A 执行 `x.store(1)`：写落进核 A 的 store buffer，还没进缓存，核 A 立刻去干下一条。
3. 核 B 执行 `y.store(1)`：同样落进核 B 的 store buffer。
4. 核 A 执行 `y.load`：读的是别的地址，不等自己 buffer 排空，直接查缓存，`y` 还是 0，取走。
5. 核 B 执行 `x.load`：同理直接查缓存，`x` 还是 0，取走。
6. 两个 buffer 一前一后落地，`x=1`、`y=1` 都进了缓存，只是这轮没人再读它们。

一轮就这样跑完了：out1=0、out2=0，咱们给计数加一。请您注意，时间线上没有任何一步在乱来：核 A 与核 B 都守着各自的程序序，错位的只有写的落地时间。

:::

### 绑上核，把比例稳下来

两次运行的差距有六倍，搅局的正是调度。咱们想稳住比例，唯一的路子是把两个线程固定在两个核上。完整的工程在 `code/volumn_codes/vol5/ch03-atomic-memory-model/store_buffer_litmus.cpp`：一百万轮，两个线程分别固定在 CPU 0 与 CPU 2（`pthread_setaffinity_np` 做的绑定），源码开头的 `kThreadACpu`、`kThreadBCpu` 两个常量管这件事，您的机器没有这样的编号就改它们。命令咱们在仓库根目录跑：

```bash
g++ -std=c++20 -O2 -pthread \
  -o /tmp/store_buffer_litmus \
  code/volumn_codes/vol5/ch03-atomic-memory-model/store_buffer_litmus.cpp
/tmp/store_buffer_litmus
```

笔者机上跑的（CPU 0 与 CPU 2 是 9700X 上两个不同的物理核，`lscpu -e` 的 CORE 列可以核对）：

```text
rounds per phase: 1000000
relaxed  (0,0) outcomes: 29657 (2.97%)
seq_cst  (0,0) outcomes: 0
```

咱们把核一绑，比例就有了着落。为什么不绑就忽高忽低？调度器随时可能把咱们绑好的两条线程挪到同一个核上，现场就时有时无了。咱们把它们固定在两个核上之后，两个 store buffer 就常驻了下来，交错的窗口一轮接一轮。这个程序笔者复跑了多次：seq_cst 恒为 0，笔者一次都没见它变过，relaxed 的比例在百分之一上下到百分之三之间波动，首跑的成绩是 2.97%，两次复跑的数分别是 1.09% 与 1.32%。

您要是换个机器、换个编译器再跑，绝对数和比例都是会变的，但两个阶段有与无的分界不变，咱们要看的正是这个分界。

至于 seq_cst 为什么能把 `(0,0)` 清零：它让编译器在关键的位置生成带排空效果的指令，把 store buffer 落了地再放行后面的 load，StoreLoad 的窗口就关上了。那样的指令在 x86 上具体是什么、它贵不贵、什么时候可以不用它，正源在[内存序](./03-memory-ordering.md)与[等待与模式篇](./04-fence-wait-and-patterns.md)的那两篇里，咱们在本篇只认现象、不碰机关。

## MESI：缓存的一致性，与它不管的事

讲线程开销的资料，解释上下文切换为什么贵的时候，几乎都会提一条：新线程的数据大概率不在本核的缓存里，是冷的。[第 4 章开头的 OS 线程篇](../ch04-concurrent-data-structures/00-os-threads-and-cost.md)就是这么讲的，它把两道题留给了第 3 章：缓存怎么分层，多核之间的一致性怎么维持。这道题的落点就是本节，咱们把这块接住。

咱们来分层。现代 CPU 的缓存不止一层：L1、L2 每个核私有，L3 是全核共享的。这么设计是被一对矛盾逼的：越快的存储越贵也越小，慢的才做得大。寄存器快而小，主内存大而慢，中间就用 L1、L2、L3 一层一层地垫。越靠近核的越小也越快，越远的越大也越慢。咱们把量级放进参考框，您别当数字用：

> 外部参考咱们只取量级（Drepper 的长文与 Intel 优化手册里的典型值，不是本机实测）：L1 的命中延迟大约几个周期，L2 与 L3 的量级是十几个和几十个，主内存的两三百个周期折合上百纳秒。具体的数字每代 CPU 都在变，您带着“每级差几倍，L1 到主内存差着两个数量级上下”的梯度看就行。

一致性协议要解决的问题从哪儿来？就从私有来：同一个地址的数据，各个核的 L1 里都可能有副本，那谁来保证这些副本说的是同一件事？咱们把这件事交给协议办：它按**缓存行**（cache line，缓存的搬运单位，x86 的一行是 64 字节）管理，给每一行标上各自的状态，最经典的一套状态就是 MESI。核与核之间怎么通气，常见的做法有两种：小机器上广播，每个核都听着总线上的请求，咱们叫它侦听（snooping）。核多了广播扛不住，咱们就换成目录（directory），由一份表记录着每一行的主人，请求按表上的记录投过去。通气的方式在变，状态机要维护的规则没变，咱们往下看状态。

> 最后咱们记一张术语卡：**MESI**。它是一套缓存一致性协议的状态名，四个字母对应四个状态：Modified 是改过的意思，有效的副本全机只剩一份，内存里的副本已经过期。Exclusive 是干净而且独占，行只在咱们的核里。Shared 是干净而且共享，别的核可能也存着一份。Invalid 的意思最干脆：这行在这儿不算数。现代实现多用它的变体（Intel 的 MESIF、AMD 的 MOESI），四态的基底没变，咱们按 MESI 讲。

咱们把四态排成一张速查表，衡量的标准是本核能不能直接写，您后面读协议细节的时候拿它用：

| 状态 | 这行的处境 | 本核能直接写吗 |
| --- | --- | --- |
| Modified | 只有本核有，且改过，内存里的副本已过期 | 能 |
| Exclusive | 只有本核有，干净 | 能，不用通知任何人 |
| Shared | 可能多核都有，干净 | 不能，先让别人的副本失效 |
| Invalid | 本核这份不算数 | 不能，先把数据拉回来 |

::: details 跟一次写走完全程

咱们假设 CPU 0 要写 `x`，`x` 所在的行此刻的状态是：CPU 1 持有 S（干净共享），CPU 0 手里是空的（I）。跟着咱们走一遍：

1. CPU 0 不能直接写，手里得拿到独占权才行。它发出请求，要求拿到这行并让别的副本失效（文献里叫 RFO，read for ownership）。
2. CPU 1 收到请求，把自己那份标成 I，回话。
3. CPU 0 拿到独占（E 态），动手写，行进 M 态。此刻全机只有它这一份是有效的。
4. 之后 CPU 1 再读 `x`，一查自己是 I，就得发起一次到共享的读，把有效的那份数据拉回来，两边都落到 S。

一轮写就这么走完。您把要点收住两条：拿不到独占就不能写，而写下去之后，全机只剩一份有效的副本。

:::

教科书给缓存一致性（coherence）的承诺拢共两条。头一条是**同一个位置的写，所有核看到的次序是同一个顺序**（写串行化）。另一条说的是**写最终会传播**，写好的值不会永远赖在哪个核里，失效机制逼着旧的副本作废，下次读必须拉新的。咱们把两条承诺合起来，就是您常听到的那句话：所有核最终看到同一份数据。

可它管的对象，咱们要看清：**一行一行的单独位置**。两个不同位置之间的次序，一致性协议是一个字都不管的。一致性不等于有序性：前者说的是单变量大家看法一致，后者说的是多个变量的访问次序有保证，两个词的模样像亲戚，干的活却是两回事。咱们上一节追到的那个 `(0,0)`，恰好卡在咱们刚说的缝里。

现在回头审那个流传最广的错解：多核缓存不一致导致了 `(0,0)`。咱们把现场重放一遍就知道冤枉谁了：x=1 从头到尾没有丢，它好端端地在 T1 的 store buffer 里排队，缓存那边的一致性维护得一丝不乱。**乱序的位置不在缓存里，在缓存的前面**——store buffer 挂在每个核与它自己的 L1 之间，写要出了 buffer 才进得了缓存系统，一致性协议根本看不见还在 buffer 里的写。所以缓存再一致也管不住 `(0,0)`，咱们反过来说：`(0,0)` 也从来不是缓存不一致的证据。

那 CPU 为什么非要这个 buffer？为了速度。您看 details 里那趟流程：动手写以前要拿独占，光失效请求的一来一回，搭进去的就是几十上百个周期，核要是原地干等，流水线就停了。把写塞进 buffer、立刻放行后面的指令，等独占到手 buffer 再落地，写的延迟被藏进了后续指令的执行里。这是一笔交换：**核每秒能干完的事，换了写的可见性延迟**。全卷开头咱们说过把正确性放在性能前面，CPU 在这里做的是同类交换，只是它没问过您。

按行管理的一致性还给咱们送来一个后续话题：两个不相干的变量挤在同一行上，各写各的也会互相把对方的数据行打失效，这就是常说的伪共享。第 2 章练习里那道分片结构的题问过：分片翻倍，每秒能干完的事为什么不跟着翻倍？答案的一半就落在这儿——分片切得再细，挤在同一行上的写照样互相打失效。它的实测与量法，咱们留给[第 4 章](../ch04-concurrent-data-structures/)。

## 内存模型：一份跨平台的合同

到这里咱们把嫌疑人凑齐了：丢次序的来源一共三位。编译器在 as-if 之下有合并、删除、换序的自由，store buffer 则给每个核的写加了一段别人看不见的滞留，再往上还有指令集自己的脾气。次序的换法一共四种组合，咱们列一张表，您看两类指令集的差别：

| 换序类型 | x86（TSO） | ARM 一类弱序 |
| --- | --- | --- |
| LoadLoad，两个读换先后 | 不换 | 可能换 |
| LoadStore，写越过排在前的读 | 不换 | 可能换 |
| StoreStore，两个写换先后 | 不换 | 可能换 |
| StoreLoad，读越过还没对外的写 | **会换** | 可能换 |

x86 是送得多的，除了 StoreLoad 之外的次序硬件都白送，这个模型的正式名字是 TSO（total store order 的中译是全局存储定序），它还带一条总保证：所有核看到的写顺序是同一个总序。ARM 那一类是送得少的，没有依赖也没有屏障的时候，四种换法都是可能发生的（ARM 保一条：有依赖关系的访问不换）。StoreLoad 那一行的实物来源，您已经在上一节亲眼见过了。

这就带来一个工程问题：三个来源各有各的规则，指令集之间的规则还各不一样，您总不能给代码标上“仅限 x86 上运行”。测试也帮不上咱们的忙，[第 0 章](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)那个 -O2 下次次全对的计数器咱们见过，测试通过的并发代码照样可以是错的。

C++11 给了咱们一个答案：把合法与非法的判定从具体硬件上独立出来，定成一套平台无关的规则，这个东西咱们叫它**内存模型**。C++ 标准定义的是一台抽象机，写明白什么样的执行是合法的：普通变量之间没有同步动作就没有次序承诺（这正是 data race 判成未定义行为的源头），原子操作按您指定的内存序提供约束。这样的保证是一份**跨平台的合同**：标准说清保证什么，兑现的活儿交给编译器与硬件。这些次序在 x86 上不需要额外的指令，在 ARM 上缺的次序由编译器生成屏障指令补上，同一份 C++ 程序搬到任何合规的平台，合法与非法的判定不变。

您别以为合同只管硬件，它管的第二个对象是编译器。硬件白送的次序，编译器未必送——汇编那一段就是现成的例子，x86 硬件没动 `double_load` 的两次读，是 GCC 把它们合成了。所以“在 x86 上硬件保证次序”的说法两头都站不住：硬件那边确实只开 StoreLoad 一种换序的口子，可您别忘了编译器在 as-if 之下还能合并、还能搬运，除非您用原子操作把约束点上。合同把两位一起管住了，这正是它要跨平台的原因。

合同里还有一处容易看漏的性格，咱们把它单独拎出来：**它比任何一台真实的机器都保守**。它允许的变换，取的是各家编译器、各种硬件可能做的事的并集。同一个程序要在所有这些机器上合法地跑，并集只能这么取。所以会有这么一种处境：一段代码在您的 x86 机器上明明是对的，relaxed 写出来的发布逻辑常常碰巧能跑，靠的是硬件把 StoreLoad 之外的次序都保证了下来。可咱们拿到合同上看，它多半是立不住的，只是当时的机器、那个编译器版本没有追究。换一台 ARM 的机器，或者只把编译器的版本换掉，它就塌了。在每个平台上单独看都是对的，并发事故最常见的来路就是它。

这套设计也不是 C++ 的独创，Java 的内存模型走得更早，C++11 的版本，地基是 Boehm 与 Adve 在 2008 年的论文。还有一条总保证您见过面：无竞争的程序享受顺序一致性（DRF-SC），[第 0 章的折叠框](../ch00-concurrency-fundamentals/02-data-race-and-tsan.md)里咱们打过照面，正式的讲法在下一篇。

::: details 一段在每个平台上分别看起来对的历史病历

咱们看老代码里常见的一对函数，发布的那一方写数据、竖标志，等的那一方等标志、用数据：

```cpp
int data = 0;
std::atomic<bool> ready{false};

void producer()
{
    data = 42;                 // 普通写
    ready.store(true, std::memory_order_relaxed);
}

void consumer()
{
    if (ready.load(std::memory_order_relaxed))
        use(data);             // 在 x86 上"总是"读到 42，在 ARM 上可能读到 0
}
```

咱们现在能把它判得很准。第一层咱们判得干脆：`data` 是普通变量，咱们一边写一边读，中间没有任何同步的隔断，这就构成了 data race，标准那边的判罚是未定义行为，它在合同上根本是立不住的。第二层要解释它为什么在 x86 上看着是对的。表里 StoreStore 一栏写的是不换，写的落地顺序硬件本来就保证，读者的 load 排在后面自然取到新值（编译器在这一对上未必要动手，这得看它的心情）。第三层咱们看 ARM：StoreStore 可能换，`ready` 早一步落地、`data` 晚一步落地的交错合法存在，读者就可能拿着 0 进 `use`。咱们的修法不是给 `data` 加 atomic，而是让标志的 store/load 带上发布与获取的内存序——那是下一篇的主角，这里咱们只把病历归档：错是固定的，平台决定的只是它露不露头。

:::

第 0 章咱们把 data race 的判据折成了三问，您还记得吧：同一个内存位置吗，至少一个写吗，有 happens-before 隔开吗。当时第三问只给了直觉。看完本篇的机器，您对那一问的分量应该有了实感。它问的正是那件事：编译器与硬件的三种自由，被同步动作收走了没有。收走它们的公理长什么样，咱们下一篇立。

顺序一致的世界里 `(0,0)` 不该出现，咱们用四条边证过了。真实机器把它造了出来，咱们也看见是在哪里造的。判次序靠平台直觉是不行的，咱们得靠公理：给“什么时候谁能看见谁”立一套每个人都要遵守的规则。公理的名字就是 happens-before。

## 练习

咱们给三道题排了梯度：笔算、改现场、下预言，一道比一道地往上走。建议您全做。

### 练习 1：闭式和的笔算与实测

example 52 里的 `kN` 是 50'000'000。您动笔算一算：0+1+…+(kN−1) 等于多少？拿公式 kN×(kN−1)/2 算出来的数，与输出的 sum 对上。然后您把 `kN` 改成 5'000'000 再跑一遍，预期的 sum 是 12499997500000。耗时就分出两样了：fenced 版大约缩到十分之一，free 版还是一百来纳秒的样子，因为闭式算式根本不在乎 `kN` 的大小。再想一层：free 版那 127 纳秒里到底剩了什么？您拿 objdump 数一数机器码，看看循环的影子还在不在：

```bash
g++ -std=c++20 -O2 -c -o /tmp/probe.o code/examples/vol5/52_compiler_reorder_probe.cpp
objdump -d --no-show-raw-insn /tmp/probe.o | grep -A 6 'free_transform'
```

答案的预期是这样的：`free_transform` 里没有循环，只剩下一两条加法与搬运的指令。您要是没对上，多半是优化档没给到 `-O2` 的缘故。

### 练习 2：同核再跑一遍 litmus

把绑核工程开头的 `kThreadACpu`、`kThreadBCpu` 都改成同一个编号，您把它跑满一百万轮，记下 relaxed 的 `(0,0)` 计数。预期的结果是它掉到 0 附近，请您把理由讲出来。咱们给两条提示。提示的头一条：同核只剩一条时间线，两个 load 都抢到了对方 store 的前面，这跟两边的程序序矛盾，各自都是写在前、读在后（正文那套四条边的反证直接搬过来用）。第二条是切换的影响：切换发生的时候，store buffer 也跟着被清了个干净，同核的两条线程共用同一个 buffer，读到的值直接从 buffer 前递。有兴致再试一步：挑两个同一物理核的硬件线程编号（`lscpu -e` 的 CORE 列能查对应关系），看比例的变化，想想为什么。

### 练习 3：给合并下个预言

您自己写两个小函数，动笔算完了再编译对答案：

```cpp
void triple_store(int v) {
    g_flag = v;
    g_flag = v + 1;
    g_flag = v + 2;
}

int load_load() {
    int a = g_data;
    int b = g_flag;
    return a + g_data + b;
}
```

您预言一下 `-O2` 之下 `triple_store` 对 `g_flag` 有几条写指令、`load_load` 对 `g_data` 有几条读指令，然后照本篇的 `g++ -S` 命令验证。答案是这样的：三条写合成一条（只剩 `v+2` 那次），两次读也合成一条（中间只隔一次对别的变量的读，拦不住这样的合并）。您要是没对上，多半是优化档没给到 `-O2` 的缘故。咱们再进一问：把 `load_load` 里那次 `g_flag` 的读改回写（变成 `double_load` 的形状），读的条数会不会变？您动笔算一算再编译。

## 本篇收进包里的东西

- as-if 规则：实现只需要保住可观察行为，程序顺序从来不是承诺，合并、删除、换序、闭式化全在编译器的许可范围里。
- store buffer：每个核私有的写队列，StoreLoad 重排的实物来源。`(0,0)` 现场里它是主角，非零是常态，比例当不了常数。
- 顺序一致性：判 `(0,0)` 不可能的公理世界，四条边的反证是本篇的判据样板，Dekker 那代算法就住在里面。
- 一致性不等于有序性：MESI 保证同一位置全局同序、写最终可见。不同位置之间的次序它不管，乱序发生在缓存前面的 buffer 里，不在缓存里。
- 内存模型是一份跨平台的合同：x86 与 ARM 只是两种兑现方式，合同管着编译器与硬件两位，比任何一台真实的机器都保守。在每个平台上分别看起来对的程序，在合同上往往站不住。
- 判次序的下一步是 happens-before 公理，下一篇立。

## 下一步

机器的三种自由，咱们都摆完了。编译器的变换是一样，store buffer 的 StoreLoad 是一样，指令集的换序又是一样。接下来的公理由[原子操作与 happens-before](./02-atomics-and-happens-before.md) 来立，第 2 章押着的 DCLP 悬案在那边有了正式判据。六种内存序怎么挑、各自的形状长什么样，配方的正源在[内存序](./03-memory-ordering.md)。fence 这套手动挡与 atomic wait 的等待模式，咱们留给[等待与模式篇](./04-fence-wait-and-patterns.md)。动手量更大的活儿在[练习体系](../exercises/)候着，全卷的走向见[卷地图](../)。

> 💡 咱们把完整示例代码放在 [Tutorial_AwesomeModernCPP](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)，您可以访问 `code/volumn_codes/vol5/ch03-atomic-memory-model/`。

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="C++ 标准草案"
    title="[intro.abstract] 条款"
    url="https://eel.is/c++draft/intro.abstract"
    chapter="as-if 规则与可观察行为三条的原文"
  />
  <ReferenceItem
    :id="2"
    author="cppreference"
    title="as-if rule"
    url="https://en.cppreference.com/w/cpp/language/as_if"
    chapter="判定的展开讲法"
  />
  <ReferenceItem
    :id="3"
    author="Intel"
    title="Intel 64 and IA-32 Architectures Software Developer's Manual（Vol. 3A §8.2 Memory Ordering）"
    url="https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html"
    chapter="x86 内存序的官方文本"
  />
  <ReferenceItem
    :id="4"
    author="Sewell, Sarkar, Owens, Zappa Nardelli, Myreen"
    title="x86-TSO: A Rigorous and Usable Programmer's Model for x86 Multiprocessors"
    :year="2010"
    url="https://www.cl.cam.ac.uk/~pes20/weakmemory/cacm.pdf"
    chapter="CACM 53(7)，把 x86 严格化成带 store buffer 的机器模型"
  />
  <ReferenceItem
    :id="5"
    author="Hans-J. Boehm, Sarita V. Adve"
    title="Foundations of the C++ Concurrency Memory Model"
    :year="2008"
    url="https://dl.acm.org/doi/10.1145/1375581.1375591"
    chapter="PLDI 2008，C++11 内存模型的奠基论文"
  />
  <ReferenceItem
    :id="6"
    author="Sarita V. Adve, Kourosh Gharachorloo"
    title="Shared Memory Consistency Models: A Tutorial"
    :year="1996"
    url="https://doi.org/10.1109/2.546611"
    chapter="IEEE Computer 29(12)，一致性与一致性模型的分工，本篇两分法的出处"
  />
  <ReferenceItem
    :id="7"
    author="Ulrich Drepper"
    title="What Every Programmer Should Know About Memory"
    :year="2007"
    url="https://akkadia.org/drepper/cpumemory.pdf"
    chapter="缓存分层、MESI、store buffer 的长文，本篇参考框里量级的来源"
  />
  <ReferenceItem
    :id="8"
    author="cppreference"
    title="std::memory_order"
    url="https://en.cppreference.com/w/cpp/atomic/memory_order"
    chapter="本篇点过其中四种的名字（relaxed、seq_cst、release、acquire），正源在内存序篇"
  />
  <ReferenceItem
    :id="9"
    author="Anthony Williams"
    title="C++ Concurrency in Action, 2nd ed"
    publisher="Manning"
    :year="2019"
    url="https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition"
    chapter="第 5 章内存模型的展开"
  />
</ReferenceCard>
