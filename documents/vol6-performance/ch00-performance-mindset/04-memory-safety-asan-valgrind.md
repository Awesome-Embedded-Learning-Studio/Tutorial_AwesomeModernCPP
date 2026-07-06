---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 把 Valgrind 五件套（memcheck/callgrind/cachegrind/helgrind+drd/massif）的职责拆开，用
  ASan 真实编译运行六类经典内存错误，讲透「动态二进制翻译」与「编译期影子内存插桩」两条路线的本质差别
difficulty: advanced
order: 4
platform: host
prerequisites:
- 动态内存管理（new/delete 与智能指针）
- C 语言动态内存（malloc/free 与 valgrind 速览）
reading_time_minutes: 27
related:
- ASan 工具家族与内存安全：shadow memory、Heartbleed 与 sanitizer 选型
- 并发程序调试技巧（TSan / Helgrind 深入）
- 动态内存管理
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
title: Valgrind 与 ASan 对照：JIT 解释 vs 编译期插桩
---
# Valgrind 与 ASan 对照：JIT 解释 vs 编译期插桩

> PS: 这部分内容由笔者大学期间的笔记迁移而来，关键结论均已用本机 GCC 16.1.1 + valgrind 3.25.1 实编实跑核对过；若仍有疏漏，欢迎 Issue 或 PR。

先说一个我们大概都干过的事：一段 C++ 代码本地跑得好好的，一上线就偶发崩溃，或者内存 RSS 一路涨到被 OOM Killer 收掉。你回头去翻代码，`new` 配 `delete` 看着都对，越界也就差那么一两个字节,光读代码根本读不出问题。这种 bug，肉眼 debug 是没希望的，必须靠工具去「看见」内存的每一次访问。

这篇我们要做的，是把抓内存错误的工具按「实现路线」分成两大派，把它们拆开跑一遍。一派是 **Valgrind**：老牌的、在程序外面套一层「虚拟 CPU」去解释执行的 JIT 方案；另一派是 **AddressSanitizer（ASan）**：编译期就把检查代码插进你程序里、靠「影子内存」记账的方案。源头的旧笔记只讲了 Valgrind，对 ASan 一字未提，但这恰恰是现在工程里更常用的那条路。这篇就把这个缺口补上，并把两条路线摆在一起对照。

## 一、两类内存错误，和「为什么读代码读不出来」

在动手用工具之前，先把要抓的「敌人」分清楚。内存错误大致两类，抓它们的难度天差地别：

**第一类：确定性的越界 / use-after-free / double-free。** 这类错误的特征是「访问了一块不该访问的地址」。它危险，但相对好抓：只要工具能标记「哪块内存是合法的、哪块不是」，越界那一刻就能当场报出来。`char buf[8]; buf[8] = 'x';` 这种 off-by-one、`free(p); return *p;` 这种悬垂指针，都属于这一类。

**第二类：未初始化读取 / 内存泄漏。** 这类更阴险。未初始化读取是「地址合法、但值是垃圾」，程序不会崩，只是悄悄算错；内存泄漏是「地址一直合法、只是永远不归还」，程序也不崩，只是 RSS 慢慢涨。这俩你不能靠「合法地址表」抓，得靠另一套机制：Valgrind 给每个字节维护「这个值是不是已经初始化过」的标记，ASan 的泄漏检测（LSan）则在程序退出时扫一遍堆，看还有没有「分配了但没人指着」的块。

读代码读不出来的根本原因，是这两类错误都**取决于运行时的内存状态**，而不取决于代码的字面写法。你光看 `*p`，根本不知道这一刻 `p` 指的内存是活的还是死的、是初始化过的还是垃圾。这正是为什么我们需要工具去「记录」每一次分配、每一次释放、每一次读写,把运行时的内存状态变成一份事后可查的账本。

那「记录」这件事，Valgrind 和 ASan 是两条完全不同的实现路线。先把结论摆前面，后面再逐个拆。

| 维度 | Valgrind（memcheck） | AddressSanitizer |
|------|---------------------|------------------|
| 怎么记 | 动态二进制翻译：运行时把每条机器指令翻译成带检查的版本 | 编译期插桩：编译时就在每次访存前后插检查代码 |
| 要不要重新编译 | **不用**，拿现成二进制就能跑 | **要**，必须用 `-fsanitize=address` 重新编 |
| 运行时开销 | 慢 20~50 倍，内存 2 倍以上（官方原话） | 慢约 2 倍，内存约 3 倍 |
| 平台 | Linux/macOS（FreeBSD/Solaris），x86/ARM 等 | GCC/Clang/MSVC 全平台，含 Windows |
| 谁来抓未初始化读取 | memcheck 原生能抓（V-bit） | ASan **抓不了**，得另上 `-fsanitize=memory`（MSan，Clang 专属） |
| 抓栈上越界 | 能（但要 `-tool=memcheck` 全套） | 默认抓栈/全局红区，`detect_stack_use_after_return` 抓栈返回后访问 |

这张表你先有个印象。接下来我们从「源头的痛点」开始，先看 Valgrind 这条路是怎么走通的。

## 二、Valgrind：套一层「虚拟 CPU」去 JIT 解释你的程序

### 2.1 它到底在干什么

Valgrind 本质上是一个**动态二进制翻译（dynamic binary translation, DBT）框架**。它不是个普通的检测库，而是把你的程序整个塞进一个「虚拟 CPU」里跑。你敲 `valgrind ./myprog`，真实发生的事是：Valgrind 拦截你的每一条机器指令，把它**即时翻译**成「干原来的活 + 顺带记录内存状态」的一串新指令，然后才执行。所以你的程序不是直接在 CPU 上跑的，而是在 Valgrind 的核心（core）里被「解释」着跑。

这就是它那句著名的副作用的来源：**慢 20 到 50 倍**，内存占用翻倍以上。Valgrind 官方手册原话就写着：

> Programs running under Valgrind run significantly more slowly, and use much more memory -- e.g. more than twice as much as normal under the Memcheck tool.

换算一下：一个跑 1 秒的程序，塞进 memcheck 可能要跑半分钟。所以 Valgrind 不是给你做日常开发时挂着的，是给你「这程序真的有内存 bug，我专门拿一段时间来揪它」用的。

这个 JIT 解释的架构有个巨大的好处，也是 Valgrind 至今没被淘汰的根本原因：**不用重新编译**。你手头有一个十年前的、连源码都找不全的二进制，怀疑它泄漏,`valgrind ./老古董` 一敲就能跑。ASan 做不到这点，ASan 必须从源码重新编一遍。这是两条路线最硬的差别。

### 2.2 五件套：一个框架，五个工具

Valgrind 的精髓是「框架 + 工具」。core 负责翻译和调度，具体「记什么、报什么」交给可插拔的 tool。`--tool=<name>` 选哪个，就是选哪副「检查眼镜」。让我们瞧瞧，可以看到手册里列的核心工具有这些：

**Memcheck**：内存错误检测器，Valgrind 的默认工具，也是绝大多数人说的「用 Valgrind 查内存」时实际用的那个。它抓的全集是（引自手册 4.1）：访问不该访问的内存（堆块越界、栈顶越界、释放后访问）、使用未初始化的值、错误的释放（double-free、`malloc` 配 `delete` 这类不匹配）、`memcpy` 源目的重叠、传给分配函数「可疑」的负数 size、`realloc` 传 0、对齐值不是 2 的幂、以及内存泄漏。一句话：memcheck 把 C/C++ 程序里最常见的内存错误几乎一网打尽。

**Callgrind**：调用图 + 缓存/分支预测 profiler。它不需要你在编译时加特殊选项（但推荐 `-g`），运行结束时把分析数据写进一个文件，再用 `callgrind_annotate` 转成人能读的格式。定位「哪个函数被调了多少次、调用关系长啥样」用。

**Cachegrind**：缓存 profiler。它模拟 CPU 的 I1/D1/L2 缓存，精确指出程序里 cache miss 和命中的位置，能给你每行代码、每个函数、每个模块产生了多少次 miss、多少条指令。想压缓存性能用它。

**Helgrind 和 DRD**：这俩都是**线程错误检测器**，抓数据竞争、锁顺序不一致、POSIX 线程 API 误用。源笔记把 Helgrind 写成「仍然处于实验阶段」，这个说法**早就过时了**，2026 年的官方手册里 Helgrind 和 DRD 都是正式列出的稳定工具，各有独立的章节（手册第 7、8 章），不是实验功能。顺带提一句：源笔记只提了 Helgrind，**漏了 DRD**，它俩目的相同（抓线程 bug）但算法不同，DRD 通常更快、对某些场景（比如大量小对象、Boost.Thread、OpenMP）支持更好。线程错误这块我在卷五的[并发程序调试技巧](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md)里专门讲过 TSan/Helgrind 的实战，这篇不重复，记住「线程类 bug 找 helgrind/drd、或更现代的 TSan」就行。

**Massif**：堆 profiler。测程序在堆上到底吃了多少内存，给你堆块、堆管理结构、栈的增长曲线。想给程序「瘦身」、找 RSS 大户用它。

> **一个容易被忽略的分工点**：memcheck 抓「对错」（这块内存能不能访问、有没有初始化），callgrind/cachegrind/massif 抓「快慢/多少」（性能和用量）。新人常把它们混为一谈，以为 Valgrind 就是查内存泄漏的，其实那只是 memcheck 一个工具的活。性能分析那几个工具（callgrind/cachegrind/massif）和 ASan 完全不在一个赛道，ASan 不碰性能 profiling。

### 2.3 memcheck 的双表原理：A-bit 与 V-bit

memcheck 凭什么能抓住那么多种内存错误？关键就在它维护了两张覆盖整个进程地址空间的「影子表」。手册 4.5 节讲得很清楚：

**Valid-Address 表（A-bit）。** 进程地址空间的每一个字节，都对应 1 个 bit，记录「这个地址当前能不能被读写」。malloc 出来一块、A-bit 就把那几个字节标记成「有效」；free 掉、标记翻回「无效」。当指令要去读写某个字节时，先查它的 A-bit，如果显示无效，就是非法访问，memcheck 当场报错。这层抓住了：越界、use-after-free、访问未分配区域。

**Valid-Value 表（V-bit）。** 进程地址空间的每一个字节，对应 8 个 bit；CPU 的每个寄存器也对应一个 bit 向量。它们记录「这个值是不是已经被初始化过了」。malloc 出来的内存，V-bit 全是「未初始化」；一旦有指令往里写了确定的值，对应字节的 V-bit 翻成「已初始化」。关键设计是：**V-bit 会跟着值「传播」**，你把一个未初始化的值从内存读进寄存器，V-bit 也跟着搬到寄存器里；你拿它做运算，结果的 V-bit 也是「未初始化」。但 memcheck 不会一读到未初始化值就报，它只在「这个值被拿去影响程序输出、或被用来生成地址」的那一刻才报。这个延迟是有意为之的，避免满屏误报。

把两张表合起来看就明白了：A-bit 管「地址合不合法」，V-bit 管「值干不干净」。前者抓越界/UAF，后者抓未初始化读取。double-free 和 alloc-dealloc 不匹配则靠 memcheck 自己维护的「这块内存是用什么分配器申请的」账本去比对。

这套「每字节都记账」的机制，代价就是前面说的内存翻倍，A-bit 和 V-bit 本身就要占地方。

## 三、ASan：编译期插桩 + 影子内存

### 3.1 思路完全反过来

ASan 的实现路线和 Valgrind 正好反过来。它**不**在程序外面套虚拟 CPU，而是**在编译的时候**就把检查代码插进你的程序里。你加 `-fsanitize=address`，编译器就会在你每一次读/写内存的前后，插一小段代码：这段代码会查一张「影子内存（shadow memory）」表，判断这次访问合不合法，不合法就报错并 abort。

所以 ASan 的检查是「程序自己查自己」，而不是「外面的虚拟 CPU 替它查」。这就解释了两条路线开销的巨大差距：ASan 只在被插桩的那几次访存上多花几条指令，没有「翻译整条指令流」的成本，所以**只慢约 2 倍**（Valgrind 是 20~50 倍）；代价是必须重新编译，且检查只覆盖被插桩的代码，动态加载的、没带 ASan 编译的第三方 .so，它管不到（Valgrind 能，因为它在指令层全盘拦截）。

### 3.2 影子内存：8 字节 → 1 字节的编码

ASan 的核心机制是影子内存（shadow memory 的完整拆解见本卷[ASan 工具家族](./03-asan-family-and-memory-safety.md)，那里还讲了它当年怎么堵 Heartbleed 这种越界读漏洞）。它把进程的整个地址空间按 8 字节一组映射到一张影子表里，每 8 个应用字节对应 1 个影子字节。那个影子字节的值有明确含义，我直接把本机跑出来的图例贴给你（后面那段输出是真实的）：

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
```

翻译一下这套编码的精妙之处：

- 影子字节是 `00`：这 8 字节全部可访问；
- 是 `01`~`07`：只有前 N 字节可访问，剩下的是越界红区。这正是 ASan 抓 off-by-one 的原理，它在每个堆块、栈帧、全局变量周围都铺了一圈「红区」，红区的影子字节标记成 `fa`/`f9` 等，你一踩进红区，插桩代码查影子字节发现不是「可访问」，立刻报错；
- 是 `fd`：这块内存已经 free 了，再访问就是 use-after-free，当场抓住。

也就是说，ASan 走的是另一条路：memcheck 逐字节记账地址合法性，ASan 则在合法区域外围铺红区，靠红区来界定边界。这套机制对越界和 UAF 极其有效，但**它没有 V-bit**，所以 ASan 抓不了未初始化读取。这块缺口得用 MSan（MemorySanitizer，`-fsanitize=memory`）补，而 MSan 只有 Clang 实现，**GCC 到 16.1.1 都不支持 `-fsanitize=memory`**（本机实测 `unrecognized argument`）。这是 ASan 路线相对 memcheck 的一个真实短板。

> **踩坑预警**：ASan 和别的 sanitizer 是「一次只能开一类」的关系。`-fsanitize=address` 和 `-fsanitize=thread`（TSan）**不能同时开**：它们对影子内存的布局假设不同，混用会直接报错或行为异常。所以抓内存错误时开 ASan，抓并发数据竞争时单独开 TSan，别想着「一把梭」。线程错误该怎么查，见[卷五的并发调试篇](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md)。

## 四、上手跑一跑：六类经典错误，ASan 真实输出

光讲原理不过瘾。我们把源笔记里那六类「全是截图、没源码」的经典错误，全部用真代码写出来，用 `g++ -std=c++20 -O0 -g -fsanitize=address,undefined` 在本机（GCC 16.1.1）编译运行。下面每一段输出都是我**真跑出来的**，不是手编的。

先把六类错误装进同一个程序：

```cpp
// cases.cpp — 六类经典内存错误，逐个用 ASan 复现
// 编译: g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases
// 运行: ./cases <1..6>   不传参则只跑内存泄漏
#include <cstdio>
#include <cstdlib>

// 1. 使用未初始化内存（ASan 抓不到，要 MSan）
int case_uninit() {
    int* p = (int*)malloc(sizeof(int));   // 内容是垃圾
    int v = *p;                            // 读到垃圾值，但地址合法
    free(p);
    return v;
}

// 2. use-after-free
int case_uaf() {
    int* p = (int*)malloc(sizeof(int));
    *p = 42;
    free(p);
    return *p;                             // 读已释放内存
}

// 3. 堆缓冲区越界（尾部读写）
int case_oob() {
    int* a = (int*)malloc(4 * sizeof(int)); // 只有 a[0..3]
    a[4] = 99;                              // 第 5 个元素越界
    int r = a[4];
    free(a);
    return r;
}

// 4. 内存泄漏（忘记 free）
void case_leak() {
    int* p = (int*)malloc(sizeof(int));
    *p = 7;                                 // 故意不 free
}

// 5. malloc 配 delete（分配/释放不匹配）
void case_mismatch() {
    int* p = (int*)malloc(sizeof(int));
    *p = 5;
    delete p;                               // malloc 该配 free
}

// 6. 双重释放
void case_double_free() {
    int* p = (int*)malloc(sizeof(int));
    free(p);
    free(p);                                // 第二次 free
}

int main(int argc, char** argv) {
    if (argc < 2) { case_leak(); puts("done: leak only"); return 0; }
    switch (atoi(argv[1])) {
        case 1: printf("uninit=%d\n", case_uninit()); break;
        case 2: printf("uaf=%d\n", case_uaf()); break;
        case 3: printf("oob=%d\n", case_oob()); break;
        case 4: case_leak(); puts("done leak"); break;
        case 5: case_mismatch(); puts("done mismatch"); break;
        case 6: case_double_free(); puts("done double-free"); break;
        default: puts("usage: ./cases [1..6]"); break;
    }
    return 0;
}
```

编译这行你记一下，后面每个 case 都用它：`g++ -std=c++20 -O0 -g -fsanitize=address,undefined cases.cpp -o cases`。`-g` 是为了让 ASan 报告里带行号，`-O0` 是别让优化把我们的越界访问优化掉（高优化级别下，`a[4]` 这种「写了立刻读」可能被折叠，ASan 仍能抓，但调试时 `-O0` 最干净）。

### 4.1 用未初始化内存 —— ASan 的盲区

先跑 case 1，看 ASan 的反应：

```text
$ ./cases 1
uninit=-1094795586
```

**ASan 一声不吭**，程序正常返回了一个垃圾值（`-1094795586`）。这就是前面说的短板：这块内存地址是合法的（malloc 来的），ASan 的影子内存里它标成「可访问」，没有 V-bit 去判断「这个值有没有被初始化过」。这块错误 memcheck 能抓（靠 V-bit），ASan 抓不到，要抓得换 MSan（`-fsanitize=memory`，Clang 专属）。这是两条路线一个**实质性的能力差异**，不是谁更强，是各管一摊。

### 4.2 use-after-free —— 红区当场咬住

跑 case 2：

```text
$ ./cases 2
=================================================================
==44083==ERROR: AddressSanitizer: heap-use-after-free on address 0x799329de0010 ...
READ of size 4 at 0x799329de0010 thread T0
    #0 ... in case_uaf() /tmp/asand/cases.cpp:20
    #1 ... in main /tmp/asand/cases.cpp:56
    ...

0x799329de0010 is located 0 bytes inside of 4-byte region [0x799329de0010,0x799329de0014)
freed by thread T0 here:
    #0 ... in free ...
    #1 ... in case_uaf() /tmp/asand/cases.cpp:19
    ...

previously allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_uaf() /tmp/asand/cases.cpp:17
    ...

SUMMARY: AddressSanitizer: heap-use-after-free /tmp/asand/cases.cpp:20 in case_uaf()
```

（上面把 build-id 等无关行省略了，关键信息全在。）你看 ASan 给了三段信息：**这次非法读发生在哪**（`case_uaf()` 第 20 行的 `return *p`）、**这块内存是哪里 free 的**（第 19 行）、**它最初是哪里 malloc 的**（第 17 行）。三段凑一起，整条「申请→释放→又访问」的因果链一目了然。这就是红区机制加上「free 后影子字节翻成 `fd`」的功劳：`free` 之后那块内存对 ASan 来说不再是「可访问」，再碰就报。

### 4.3 堆缓冲区越界 —— 尾部红区

跑 case 3（`a[4]` 越界，`a` 只开了 4 个 int）：

```text
$ ./cases 3
=================================================================
==44191==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x7288a7be0020 ...
WRITE of size 4 at 0x7288a7be0020 thread T0
    #0 ... in case_oob() /tmp/asand/cases.cpp:26
    ...

0x7288a7be0020 is located 0 bytes after 16-byte region [0x7288a7be0010,0x7288a7be0020)
allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_oob() /tmp/asand/cases.cpp:25
    ...
```

`located 0 bytes after 16-byte region`，这块内存是 16 字节（4 个 int），访问点正好踩在它**结尾之后的第一个字节**，也就是尾部红区的起点。这就是 ASan 抓 off-by-one 的原理：malloc 返回的块后面紧跟一圈红区，红区的影子字节是 `fa`（heap left redzone，其实是堆块周围的 poison），`a[4]` 落进红区，插桩代码一查影子字节不是 `00`，当场报错。

> **一个源笔记提到、但容易误解的点**：源笔记说「Valgrind 不检查静态分配数组」。这点对老 memcheck 是真的（栈/全局数组越界历史上是 memcheck 的弱项），但 **ASan 不是这样**：ASan 对栈数组、全局变量都铺红区（影子字节 `f1`~`f3` 是栈红区、`f9` 是全局红区），栈上数组越界它抓得很利索。所以「静态数组越界查不到」这条结论，只对 Valgrind 成立，对 ASan 不成立。别把两个工具的局限混为一谈。

### 4.4 内存泄漏 —— LSan 在程序退出时扫堆

跑 case 4（`./cases 4`，故意不 free）：

```text
$ ./cases 4

=================================================================
==44296==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 ... in malloc ...
    #1 ... in case_leak() /tmp/asand/cases.cpp:34
    #2 ... in main /tmp/asand/cases.cpp:58
    ...

SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
```

注意这个报错是 **`LeakSanitizer`**，不是 ASan 本体：LSan 是 ASan 默认捆绑的泄漏检测器，在**程序正常退出时**扫一遍整个堆，把「分配了但没有任何指针指向」的块揪出来。它报告的是「still reachable / definitely lost」这套分类里的「definitely lost」。这和 memcheck 的泄漏检测思路一致（都是退出时扫堆），只是 LSan 是 ASan 工具链的一部分。

> **守护进程怎么办？** LSan 默认在程序 `exit` 时才扫，长跑的 daemon/服务进程不会自己退出。这时可以发信号让它中途 dump：`ASAN_OPTIONS=abort_on_error=0:detect_leaks=1` 配合 `kill`，或用 LSan 的 `__lsan_do_leak_check()` API 在代码里主动触发一次扫描。Valgrind 那边对应的办法是另一个终端 `kill` 掉 memcheck 进程让它输出（源笔记提过这招）。

### 4.5 malloc 配 delete —— 分配/释放不匹配

跑 case 5：

```text
$ ./cases 5
=================================================================
==44300==ERROR: AddressSanitizer: alloc-dealloc-mismatch (malloc vs operator delete) ...
    #0 ... in operator delete(void*, unsigned long) ...
    #1 ... in case_mismatch() /tmp/asand/cases.cpp:42
    ...

0x71a3249e0010 is located 0 bytes inside of 4-byte region [0x71a3249e0010,0x71a3249e0014)
allocated by thread T0 here:
    #0 ... in malloc ...
    #1 ... in case_mismatch() /tmp/asand/cases.cpp:40
    ...
```

`alloc-dealloc-mismatch (malloc vs operator delete)`：ASan 替每个分配记下了「是用谁申请的」，释放时一比对，`malloc` 配 `delete` 不匹配，当场报。memcheck 抓的是同一类（手册 4.2.5「freed with an inappropriate deallocation function」），两边能力对齐。

> **平台差异提醒**：这个 `alloc-dealloc-mismatch` 检查在 Windows 上**默认是关的**（MSVC 的 ASan，因为 Windows 上 `delete` 和 `free` 经常实际等价）。Linux/macOS 默认开。如果你在 Windows 上发现这类错误没被抓，查一下 `ASAN_OPTIONS=alloc_dealloc_mismatch=1`。

### 4.6 双重释放

跑 case 6：

```text
$ ./cases 6
=================================================================
==44193==ERROR: AddressSanitizer: attempting double-free on 0x6d0d527e0010 in thread T0:
    #0 ... in free ...
    #1 ... in case_double_free() /tmp/asand/cases.cpp:49
    ...

0x6d0d527e0010 is located 0 bytes inside of 4-byte region [0x6d0d527e0010,0x6d0d527e0014)
freed by thread T0 here:
    #0 ... in free ...
    #1 ... in case_double_free() /tmp/asand/cases.cpp:48
    ...
```

`attempting double-free`：第一次 `free` 之后影子字节翻成 `fd`，第二次再 `free` 同一地址，ASan 发现它已经是 `fd` 状态（已释放），直接判定为 double-free。还贴心地告诉你「上次是在第 48 行 free 的」。

### 4.7 额外彩蛋：栈上 use-after-return

ASan 还能抓一个 memcheck 历史上很难抓的东西，**栈帧返回后被访问**（函数返回了，调用方却还持有指向函数内局部变量的指针）。这个要显式开：

```cpp
// suar2.cpp
#include <cstdio>
static int* g = nullptr;
void stash() { int local = 0xc0ffee; g = &local; }  // 把局部变量地址存出去
int main() { stash(); return *g; }                   // local 已随 stash 返回而消失
```

```text
$ g++ -std=c++20 -O0 -g -fsanitize=address suar2.cpp -o suar2
$ ASAN_OPTIONS=detect_stack_use_after_return=1 ./suar2
=================================================================
==44702==ERROR: AddressSanitizer: stack-use-after-return on address 0x6da50b8f0020 ...
READ of size 4 at 0x6da50b8f0020 thread T0
    #0 ... in main /tmp/asand/suar2.cpp:4
    ...

Address 0x6da50b8f0020 is located in stack of thread T0 at offset 32 in frame
    #0 ... in stash() /tmp/asand/suar2.cpp:3

  This frame has 1 object(s):
    [32, 36) 'local' (line 3) <== Memory access at offset 32 is inside this variable
HINT: this may be a false positive if your program uses some custom stack unwind mechanism ...
SUMMARY: AddressSanitizer: stack-use-after-return /tmp/asand/suar2.cpp:4 in main
```

注意那个地址 `0x6da50b8f0020`，它在进程地址空间里**很靠前**（不是普通栈区），因为开了 `detect_stack_use_after_return` 后，ASan 会把「可能被逃逸指针指向的局部变量」挪到一块专门的「假栈（fake stack）」上，函数返回时把那块假栈标成毒，再访问就报 `stack-use-after-return`（影子字节 `f5`）。默认它是关的，因为有一定开销和少量误报（看那个 HINT）。但这种「函数返回后还在用栈内存」的 bug 极其难查，值得知道有这招。

## 五、Valgrind 怎么用：把上面那些错误喂给 memcheck

讲完原理，我们把第四节那同一个 `cases.cpp`（这次不带 `-fsanitize=`，普通编译）塞进 valgrind 跑一遍，看 memcheck 对同一批错误是怎么报的，两套话术正面相对，对照才看得清。本机用的是 valgrind 3.25.1。

先用 `-g` 编一个干净版（valgrind 不需要 ASan 那套插桩，但要 `-g` 才能在报告里给出行号）：

```bash
g++ -std=c++20 -g -O0 cases.cpp -o cases_plain

# 最常用：memcheck 全量查泄漏
valgrind --tool=memcheck --leak-check=full ./cases_plain 4

# 更狠：连 still reachable 也列出来 + 跟进子进程
valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --trace-children=yes ./cases_plain
```

几个关键参数：`--leak-check=full` 是完全检查泄漏（给出行号），`--show-leak-kinds=all` 连「still reachable」（还有指针指着、理论上还能 free）的块也列出来（老版本的 `--show-reachable=yes` 是它的别名，仍能用但已不推荐），`--trace-children=yes` 跟进 `fork`/`exec` 出来的子进程。换工具就改 `--tool=`：`callgrind`、`cachegrind`、`helgrind`、`drd`、`massif`。

### 5.1 同一个 UAF，memcheck 这么报

跑 case 2（就是第四节那个 use-after-free）：

```text
$ valgrind --tool=memcheck --leak-check=full ./cases_plain 2
==453796== Memcheck, a memory error detector
...
==453796== Invalid read of size 4
==453796==    at 0x40011E9: case_uaf() (cases.cpp:20)
==453796==    by 0x4001377: main (cases.cpp:56)
==453796==  Address 0x4ee9080 is 0 bytes inside a block of size 4 free'd
==453796==    at 0x48529EF: free (vg_replace_malloc.c:989)
==453796==    by 0x40011E4: case_uaf() (cases.cpp:19)
==453796==  Block was alloc'd at
==453796==    at 0x484F8A8: malloc (vg_replace_malloc.c:446)
==453796==    by 0x40011CA: case_uaf() (cases.cpp:17)
uaf=42
...
==453796== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
```

注意行号，`cases.cpp:20` 读、`:19` free、`:17` malloc，和第四节 ASan 报的**一模一样**（ASan 那边也是 :20/:19/:17）。同一个 bug，两个工具各自定位到同一行，只是话术不同：

- ASan 说 `heap-use-after-free` + `located 0 bytes inside of 4-byte region`；
- memcheck 说 `Invalid read of size 4` + `Address ... is 0 bytes inside a block of size 4 free'd`。

memcheck 还多了句 `Block was alloc'd at ... :17`：它靠 A-bit 账本记下了这块内存的「一生」（在哪申请、在哪释放、现在又被读），整条因果链一次给全，和 ASan 的「allocated by / freed by」三段式是同一个思路、两套措辞。

### 5.2 泄漏：LEAK SUMMARY 对位 LSan

跑 case 4（故意不 free）：

```text
$ valgrind --tool=memcheck --leak-check=full ./cases_plain 4
==453446== HEAP SUMMARY:
==453446==     in use at exit: 4 bytes in 1 blocks
==453446==   total heap usage: 3 allocs, 2 frees, 77,828 bytes allocated
==453446== 4 bytes in 1 blocks are definitely lost in loss record 1 of 1
==453446==    at 0x484F8A8: malloc (vg_replace_malloc.c:446)
==453446==    by 0x400123D: case_leak() (cases.cpp:34)
==453446==    by 0x40013B5: main (cases.cpp:58)
==453446== LEAK SUMMARY:
==453446==    definitely lost: 4 bytes in 1 blocks
==453446==    indirectly lost: 0 bytes in 0 blocks
==453446==      possibly lost: 0 bytes in 0 blocks
==453446==    still reachable: 0 bytes in 0 blocks
==453446== ERROR SUMMARY: 1 errors from 1 contexts (suppressed: 0 from 0)
```

`definitely lost: 4 bytes`，对位第四节 ASan 那边 LSan 的 `Direct leak of 4 byte(s)`。两边都是「程序退出时扫一遍堆」，只是 memcheck 把泄漏分成 `definitely lost / indirectly lost / possibly lost / still reachable` 四档（更细），LSan 默认只报 `Direct` 和 `Indirect` 两档。行号同样是 `:34`，和 ASan 一致。

> **别再去下源码包手动编译。** 源笔记给的安装流程是 `tar -jxvf valgrind-3.12.0.tar.bz2 && ./configure && make && sudo make install`，`3.12.0` 是 2016 年的版本，**十年前了**，而且对现代内核/新 CPU 指令（比如较新的 AVX）支持差，跑新编的程序容易各种报错。现在直接用发行版包：Debian/Ubuntu 是 `apt install valgrind`、Fedora/RHEL 是 `dnf install valgrind`、Arch 是 `pacman -S valgrind`，装上就是 3.2x 版本（本机 3.25.1）。

## 六、两条路线怎么选

说了这么多，到底什么时候用哪个？给你一套实战决策：

**默认用 ASan。** 日常开发、CI 里挂着的内存错误检测，首选 ASan：它快（慢 2 倍 vs 20~50 倍，CI 能忍），跨平台（Windows/macOS/Linux 通吃，MSVC 也支持），报告干净。现代 C++ 项目里，`-fsanitize=address,undefined` 几乎是调试构建的标配。我们卷一的[动态内存管理](../../vol1-fundamentals/ch12/02-new-delete.md)里讲 ASan 抓泄漏、卷五的并发调试里讲 TSan，都是这条路上的工具。

**这几类场景，必须上 Valgrind：**

1. **只有二进制、没源码**，或者重新编译成本太高（比如巨大的遗留项目）。ASan 必须重新编，Valgrind 拿现成二进制就能跑。
2. **要抓未初始化读取，但只有 GCC**。ASan 没 V-bit，MSan 又只有 Clang，用 GCC 编的项目要抓未初始化，memcheck 是现成的。
3. **要性能 profiling**（callgrind/cachegrind/massif）。这几个工具 ASan 完全没有对应物，想看 cache miss、堆增长曲线、调用图，只有 Valgrind 这一套。
4. **要全盘覆盖，包括没带 ASan 编译的第三方库**。Valgrind 在指令层拦截，连没源码的 .so 里的内存错误也能抓；ASan 只覆盖被插桩的代码。

反过来，**这几类 Valgrind 干不了、或干不好，得用 ASan**：抓栈数组/全局数组越界（ASan 的栈/全局红区是强项）、跑得快（CI 友好）、Windows 平台（Valgrind 基本不支持 Windows）、抓 stack-use-after-return（ASan 有专门的 fake stack 机制）。

一句话收口：**ASan 是「开发期」的标配，Valgrind 是「疑难杂症 / 性能 / 遗留二进制」的专科」。** 它们不是替代关系，是互补关系，很多团队是 CI 里挂 ASan 做日常守门，遇到 ASan 抓不到的怪问题再上 Valgrind 复查。

## 七、回到 C++：工具是兜底，RAII 才是治本

讲了一整篇工具，最后必须把话拉回来：**这些工具再强，也是「事后抓 bug」，不是「消灭 bug」。** 真正让内存错误从根上消失的，是 C++ 的 RAII 和智能指针。

回头看那六类错误，你会发现它们**清一色都建立在「裸 malloc/free、裸指针」**上：

- 泄漏？用 `std::unique_ptr` / `std::vector`，对象出作用域自动释放，根本没机会忘 `free`；
- use-after-free？智能指针的所有权语义让「这块内存还能不能用」变成编译期就能约束的事；
- double-free？`unique_ptr` 不能拷贝、移动后原指针置空，物理上就 double 不了；
- 越界？`std::vector` 配 `.at()` 会抛异常、`std::span` 带边界，别用裸 `[]` 配手工长度。

C 风格的 `malloc`/`free`/裸指针把「内存什么时候释放、谁能访问」全部丢给程序员记，人脑记这些必然出错，所以才需要 Valgrind 和 ASan 这种「记账工具」来兜底。Modern C++ 的思路是把这套记账**搬到类型系统里**：资源的生命周期绑死在对象上，编译器替你保证释放。这是从「工具抓 bug」到「语言消灭 bug」的根本跃迁，我们卷一的[动态内存管理](../../vol1-fundamentals/ch12/02-new-delete.md)整篇就在讲这个。

但这**不**意味着 Modern C++ 项目就不需要 ASan/Valgrind 了。只要你的代码还会调 C 库、还会用 `new`/`delete`、还会碰第三方没有 RAII 包装的接口，内存错误就还有缝可钻。所以正确的姿势是：**先用 RAII 把 99% 的内存错误在写代码时就消灭，再用 ASan 把漏网的那 1% 在测试期抓出来，最后拿 Valgrind 兜底那些最古怪的疑难杂症。** 三层防线，缺一不可。
