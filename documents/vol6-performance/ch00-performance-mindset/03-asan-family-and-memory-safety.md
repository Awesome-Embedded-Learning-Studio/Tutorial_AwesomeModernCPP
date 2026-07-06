---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 从 Heartbleed 说起,拆解 AddressSanitizer 的 shadow memory 三件套,实测 OOB/UAF/全局越界与
  UBSan,理清 ASan/LSan/MSan/TSan/UBSan 五兄弟的职责与互斥关系
difficulty: advanced
order: 3
platform: host
prerequisites:
- 动态内存管理(new/delete 与智能指针)
- 并发程序调试技巧(ThreadSanitizer)
reading_time_minutes: 24
related:
- 动态内存管理(new/delete 与智能指针)
- 并发程序调试技巧(ThreadSanitizer)
- C 语言动态内存管理(malloc/free 与 valgrind)
tags:
- host
- cpp-modern
- advanced
- 内存安全
- 调试
- 内存管理
title: ASan 工具家族与内存安全:shadow memory、Heartbleed 与 sanitizer 选型
---
# ASan 工具家族与内存安全:shadow memory、Heartbleed 与 sanitizer 选型

> PS: 这部分内容是笔者读大学期间迁移的笔记，仅经过有限的搜索验证，如果发现存在技术断言不严谨的问题甚至时错误，欢迎报告 Issue! 或者是直接的修复PR！

写过一段时间 C/C++,你大概率被这几类问题反复折磨过:数组多读了一位、释放完的指针被别人又用了一次、同一个 `delete` 调了两遍。这些错误有个共同的恶心之处:它们是**未定义行为**(大名鼎鼎的Undefined Behavior)。不一定崩,在 Debug 构建里跑得好好的,到了 Release 或者换了台机器就随机爆炸。更糟的是,崩的地方往往离真正出错的代码十万八千里,堆栈指向的可能是某个无辜的库函数。

为什么会这样?因为这类 bug 破坏的是内存管理器自己的元数据,等到下一次 `malloc`/`free` 走到那个被踩坏的位置才发作。本卷前面讲性能,这一篇我们换到另一个维度:怎么在 bug 还没酿成线上事故之前,就用工具把它揪出来。主角是 AddressSanitizer(ASan)和它背后那一整套 sanitizer 工具家族。

先别急着把 ASan 当成一个"加个 flag 就完事"的小工具。它背后的设计(shadow memory、编译期插桩)其实是过去十几年 C/C++ 内存安全领域最重要的工程进展之一,而且它最初被发明出来,就是为了堵住一个让整个互联网心惊肉跳的洞。我们从那个洞讲起。

## 一切的起点:Heartbleed 与 buffer over-read

2014 年 4 月,CVE-2014-0160 被披露,代号 Heartbleed。这是 OpenSSL 实现里一个听起来人畜无害的特性(TLS 心跳扩展,heartbeat)里藏的洞。协议很简单:客户端发来一段任意数据,告诉服务器"这段数据有 N 字节,请原样读回来",用来验证连接还活着。

漏洞在于,服务器**信任了客户端报上来的长度 N,但没有校验 N 是否真的不超过自己手里那段数据的实际长度**。于是攻击者只要把 N 报得很大(比如 64KB),服务器就会从自己进程内存里"读回" 64KB 给攻击者。读到的可能是别的会话的 TLS 私钥、用户密码、session token,进程内存里挨着那块缓冲区的一切,统统泄漏。

这个 bug 的本质是越界**读**(buffer over-read / over-read),不是越界**写**。写越界好歹会破坏数据、容易暴露;读越界安静得多,进程自己不会崩,数据就这么悄无声息地流出去了。ASan 当年被反复拎出来讲,正是因为它属于少数能**稳定检测 over-read**的工具:只要那段越界内存碰到了 ASan 埋下的 redzone,读一下就立刻报错。

我们用几十行 Modern C++ 复刻一个 Heartbleed 形状的 bug,然后让 ASan 抓现行。这是本篇的王牌演示,后面要反复用到。

```cpp
// oob_read.cpp —— 复刻 Heartbleed 形状的越界读
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g oob_read.cpp -o oob_read
#include <array>
#include <cstdio>
#include <string>

// 心跳回显:客户端说"还给我 n 字节"。服务器照办,但不校验 n 上界。
std::string read_back(const std::array<char, 8>& buf, int n)
{
    return std::string(buf.data(), n);   // n 可能远大于 8
}

int main()
{
    std::array<char, 8> buf{'H', 'i', '!', 0, 0, 0, 0, 0};
    // 只授权了 8 字节,却要求"读回" 64 字节 —— 经典 over-read
    auto leaked = read_back(buf, 64);
    std::printf("读到 %zu 字节: %.8s...\n", leaked.size(), leaked.c_str());
}
```

不带 ASan 编译运行,这段代码大概率"看起来正常":`std::string` 的构造函数老老实实按你给的长度从 `buf.data()` 起拷贝 64 字节,把栈上后面那些不相干的字节全读走了,程序不会崩。这正是 over-read 可怕的地方。

加 `-fsanitize=address` 再跑,画风突变。用本机 GCC 16.1.1 跑出来:

```text
=================================================================
==37023==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x72175e1f0028 at pc 0x761760d29ac2 ...
READ of size 64 at 0x72175e1f0028 thread T0
    #0 0x... in memcpy (/usr/lib/libasan.so.8+0x129ac1)
    ...
    #6 0x... in read_back[abi:cxx11](std::array<char, 8ul> const&, int) oob_read.cpp:11
    #7 0x... in main oob_read.cpp:18
    ...

  This frame has 2 object(s):
    [32, 40) 'buf' (line 16)
    [64, 96) 'leaked' (line 18) <== Memory access at offset 40 partially underflows this variable
SUMMARY: AddressSanitizer: stack-buffer-overflow oob_read.cpp:11 in read_back
```

注意两个细节。第一,报错类型是 `stack-buffer-overflow`,发生在 `read_back` 第 11 行,也就是 `return std::string(buf.data(), n);` 那一行,精确到源码位置,这正是为什么编译时必须带 `-g`。第二,ASan 甚至告诉我们栈帧里有两个对象,`buf` 占 `[32, 40)`、`leaked` 占 `[64, 96)`,越界读的位置(offset 40)正好落在两者之间。这种级别的现场信息,是 ASan 区别于"加个断言慢慢找"的根本所在。

## 所以，ASan 到底做了什么才能这样的呢？

### 第一件:编译期插桩(CTI)

ASan 在**编译期就改写你的代码**,不是事后才分析的 profiler。当你加 `-fsanitize=address`,编译器(GCC 或 Clang 都行)会在每一次内存访问(每一个 `*p`、每一次数组下标、每一次 `memcpy`)前后插入额外的检查指令。这种技术叫**编译期插桩**(compile-time instrumentation,CTI),也叫静态插桩。

这里先验证一下它真的"动了你的代码"。把上面的 `oob_read.cpp` 不带 ASan 编译一份,带 ASan 编译一份,对比两者的**代码段(.text)大小**,也就是真正塞进二进制里的机器指令:

```text
普通构建 .text:   2792 字节
ASan 构建 .text:  5736 字节   (+105%)
```

(本机 GCC 16.1.1,`g++ -std=c++20 -O1 -g`,用 `size` 看 `.text` 段。) 多出来的那一倍,就是编译器在每次内存访问前后塞进去的检查指令。注意这里有个坑:**别拿整个二进制文件的大小来比**:ASan 的运行时库 `libasan.so.8` 是**动态链接**的(`ldd` 能看到它),并没有被打进可执行文件,所以整个文件大小其实只涨了 5% 左右;真正反映插桩量的是 `.text` 代码段,那才是翻倍增长的地方。代价是体积变大、运行变慢,但比起它能抓到的 bug,这点开销在开发阶段几乎可以忽略。CTI 是**编译期**决定的事,所以你必须**编译时**就带上 `-fsanitize=address`,而且**链接时也要带**。如果你只编译主程序时加了、链接某个第三方 `.a` 库时没加,那库内部的内存访问就没被插桩,ASan 对那部分代码就是瞎的。完整流程是:

```bash
g++ -std=c++20 -O1 -fsanitize=address -g -c a.cpp -o a.o     # 编译带
g++ -std=c++20 -O1 -fsanitize=address -g main.cpp a.o -o app  # 链接也带
```

`-fsanitize=address` 在编译和链接两个阶段都要出现,缺一个就白搭。

### 第二件:shadow memory(影子内存)

光有插桩还不够。插桩插入的检查代码,需要一个"账本"来回答"这个地址现在到底能不能访问"。这个账本就是 **shadow memory**(影子内存)。

核心思想是一句很优雅的设计:**用 1 个字节的影子内存,记录 8 个字节的实际内存的可访问状态**。也就是说,ASan 把整个进程地址空间按 8 字节一组映射到一片连续的影子区,比例是 1:8。这样检查一个地址是否合法,只需要算出它对应的影子字节、读出来看一眼就行,不用维护什么复杂的哈希表。

影子字节的取值,ASan 的报告末尾会直接打印出来。我们看真实输出里的图例:

```text
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
```

这就是 1:8 映射的全部语义。`00` 表示这 8 字节都可以访问;`01`–`07` 表示只有前几个字节合法(比如 `03` 表示前 3 字节能访问、后 5 字节不能,用于对齐尾部的部分可访问区域);`fa` 是堆分配周围的 redzone:ASan 在你 `new` 出来的每块内存四周都偷偷塞了一圈"禁入区",你一旦读到 `fa`,就是堆越界;`fd` 是已经 `free` 掉的内存,你一碰就是 use-after-free;`f1`/`f2` 是栈对象的 redzone。

回头看上面那段报错的 shadow dump:

```text
=>0x72175e1f0000: f1 f1 f1 f1 00[f2]f2 f2 00 00 00 00 f3 f3 f3 f3
```

`00` 是 `buf` 本体(8 字节,1 个影子字节),紧跟着的 `[f2]` 就是栈对象之间的 mid redzone。我们越界读的地址恰好落在这个 `f2` 上,ASan 一眼就看出来了。这是 shadow memory 机制能精确到字节级的原因。

### 第三件:运行时库 + quarantine

光有插桩和影子区还不够,还得有人**填这个账本**。`new`/`delete`、`malloc`/`free` 这些函数,ASan 运行时库(`libasan`)会把它们整个换掉,换成自己的版本。每分配一块内存,运行时就给它在影子区里画上 redzone;每释放一块,就把对应影子区标记成 `fd`。

这里还有个关键设计叫 **quarantine(隔离区)**。`free` 掉的内存,ASan 不会立刻归还给系统重新分配,而是先扔进一个隔离队列里晾着。为什么?因为 use-after-free 这种 bug,如果你 `free` 完马上又分配出去给别人用了,那块内存的影子状态就变回 `00` 了,后面误读就抓不到了。隔离一段时间,保证"已释放"的状态能被后续的误访问撞上。

不过 quarantine 不是无限的,队列有上限,满了之后旧的已释放内存会按 FIFO 被真正回收。所以 ASan 对 use-after-free 的检测也不是 100%:如果隔离窗口已经滑过去了、内存已经被重新分配,那次误读就抓不到。但配合足够的测试覆盖,绝大多数 UAF 都能被逮住。

### 代价:2-4 倍开销,为什么还是值得

三件套加起来,ASan 的典型开销是 **2-4 倍的运行时间减速、3-5 倍的内存开销**(影子区占 1/8,加上 redzone 和 quarantine)。听起来不少,但这是跟谁比的问题。

传统的内存检测工具 Valgrind(Memcheck)用的是**动态二进制插桩**(DBI):它不重新编译你的程序,而是在运行时把每一条机器指令翻译成自己的一套中间表示、逐条分析再执行。精度高、不用重新编译,但代价是 20-50 倍的减速。跑一个原本 1 秒的测试,Valgrind 要等半分钟,很多时候根本没法纳入日常 CI。

ASan 把分析成本**前移到了编译期**(CTI),运行时只做查表,所以能把开销压到 2-4 倍。这个量级意味着你可以在开发和 CI 里**常驻**开着 ASan 跑完整测试套件,而不是偶尔想起来才手动跑一次 Valgrind。这是 ASan 相对 Valgrind 最根本的优势:**用得起**。

::: warning 本机没装 Valgrind
这篇的所有 ASan/UBSan 输出都是本机真跑出来的(GCC 16.1.1 / Clang 22,WSL2)。Valgrind 在本机环境里没装(`which valgrind` → not found),所以本篇不贴 Valgrind 实测输出。需要 Valgrind 的同学在 Debian/Ubuntu 上 `apt install valgrind` 即可,用法见 vol1 的 [C 语言动态内存管理](../../vol1-fundamentals/c_tutorials/14-dynamic-memory.md) 一文的 valgrind 段。两条命令的本质区别记牢:**ASan 是编译期 `-fsanitize=address`(CTI),Valgrind 是运行时 `valgrind ./prog`(DBI)**。
:::

## 工具家族:五个 sanitizer 各管一类

ASan 其实只是一个家族的成员。这套工具最早是 Google 的工程师作为 GCC 和 Clang 的补丁实现的,后来成了主流编译器的标配。家族里一共五个成员,每个盯着一类特定的错误:

| 工具 | 编译开关 | 抓什么 | 典型开销 |
|------|---------|--------|---------|
| **ASan**(AddressSanitizer) | `-fsanitize=address` | 越界读写、use-after-free、double-free、栈/全局越界 | 2-4x 减速 |
| **LSan**(LeakSanitizer) | `-fsanitize=leak` | 内存泄漏(程序退出时未释放的堆内存) | 几乎零开销 |
| **MSan**(MemorySanitizer) | `-fsanitize=memory` | 读未初始化的内存(use of uninitialized value) | ~3x 减速 |
| **TSan**(ThreadSanitizer) | `-fsanitize=thread` | 数据竞争(data race)、死锁 | 5-15x 减速 |
| **UBSan**(UndefinedBehaviorSanitizer) | `-fsanitize=undefined` | 未定义行为(有符号溢出、空指针解引用、位移越界等) | 可配置,多数子项开销很小 |

五兄弟里,ASan 是主力,日常开发几乎必备;LSan 默认会随 ASan 一起启用(GCC/Clang 在支持的环境下);MSan 只在 Clang 上完整可用、且必须**全程序**都编译成 MSan 版本(连 libc 都要 MSan 版,否则误报满天飞);TSan 专门盯并发,我们在 vol5 的 [并发程序调试技巧](../../vol5-concurrency/ch08-debug-testing-perf/01-debugging-concurrency.md) 里专门讲过;UBSan 是个"补刀手",开销小、可以和别的组合。

### ASan 和 TSan 互斥:一条铁律

这五个工具不是随便组合的。最重要的约束是:**ASan 和 TSan 不能同时启用**。ASan 要用自己的影子内存布局,TSan 也要用自己的,两套机制会打架。编译器会在编译期直接拒绝你:

```text
$ g++ -std=c++20 -fsanitize=address,thread -g conflict.cpp -o conflict
cc1plus: error: '-fsanitize=thread' is incompatible with '-fsanitize=address'
```

报错直白。这条约束的工程后果是:一个项目的 CI 里,内存错误检测和数据竞争检测得分**两套独立的构建**:一套开 ASan、一套开 TSan,各自跑一遍测试。vol5 的 TSan 篇专门讲过这个"双构建"实践,这里我们记住结论就好。

至于 MSan,它跟 ASan/TSan 都不兼容(它要求所有代码都"干净"地走自己的未初始化追踪),而且只支持 Clang,所以实际项目里用得最少。LSan 和 UBSan 是两个"百搭":LSan 几乎零开销可以常驻,UBSan 的多数子项也能跟 ASan 一起开。

## 实测:ASan 抓三类典型错误

光讲原理不过瘾。我们把 C++ 里最容易翻车的三类内存错误各写一个,让 ASan 一个个抓出来。下面三段都是本机真跑出来的。

### 堆 use-after-free

智能指针能挡住大部分 UAF,但只要项目里还有裸指针、还有 C 风格 API,这个洞就堵不完。看一个最小例子:把一个 `unique_ptr` 释放后,还拿着它先前吐出来的裸指针去读:

```cpp
// uaf.cpp —— use-after-free
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g uaf.cpp -o uaf
#include <cstdio>
#include <memory>

int main()
{
    auto p = std::make_unique<int>(42);
    int* raw = p.get();     // 拿到裸指针
    p.reset();              // 这里释放 —— raw 立刻变成悬空指针
    std::printf("悬空指针读到的值: %d\n", *raw);   // use-after-free
}
```

带 ASan 跑:

```text
=================================================================
==37082==ERROR: AddressSanitizer: heap-use-after-free on address 0x7a948abe0010 ...
READ of size 4 at 0x7a948abe0010 thread T0
    #0 0x... in main uaf.cpp:12

0x7a948abe0010 is located 0 bytes inside of 4-byte region [0x7a948abe0010,0x7a948abe0014)
freed by thread T0 here:
    #0 0x... in operator delete(void*, unsigned long) (/usr/lib/libasan.so.8+0x12e4c1)
    ...
    #4 0x... in main uaf.cpp:11

previously allocated by thread T0 here:
    #0 0x... in operator new(unsigned long) (/usr/lib/libasan.so.8+0x12d341)
    ...
    #2 0x... in main uaf.cpp:9

SUMMARY: AddressSanitizer: heap-use-after-free uaf.cpp:12 in main
```

这份报告是 ASan 最有价值的地方。它不仅告诉你"第 12 行那次读是 use-after-free",还同时给出**这条内存的两段历史**:在 `uaf.cpp:9` 由 `make_unique` 分配、在 `uaf.cpp:11` 由 `reset` 释放。盯着这两行,bug 的因果链就完整了,这正是 quarantine + redzone 机制的价值:已释放的内存被标记成 `fd` 而不是立刻回收,所以后续误读能撞上。

shadow dump 里那个 `[fd]` 就是铁证:

```text
=>0x7a948abe0000: fa fa[fd]fa fa fa fa fa ...
```

`fd` = freed heap region。这就是 ASan "账本"的功劳。

### 全局缓冲区越界

全局/静态变量同样有 redzone 保护。一个全局数组越界访问,ASan 照抓不误:

```cpp
// global_oob.cpp —— 全局数组越界
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g global_oob.cpp -o global_oob
#include <cstdio>
int g[4] = {1, 2, 3, 4};
int main() { std::printf("g[5] = %d\n", g[5]); }
```

```text
==38356==ERROR: AddressSanitizer: global-buffer-overflow on address 0x63ca65acd074 ...
SUMMARY: AddressSanitizer: global-buffer-overflow global_oob.cpp:5 in main
```

报错类型明确标成 `global-buffer-overflow`。ASan 把栈、堆、全局三类区域用不同的 redzone 编码区分开(`f1`/`f2` 栈、`fa` 堆、`f9` 全局),所以你能一眼看出越界发生在哪类存储上。

::: warning 关于「Clang 11 才支持全局 OOB」这个说法
有些老资料会说"ASan 检测全局变量越界需要 Clang 11 以上"。这个说法的历史背景是:早期 ASan 对全局变量的 redzone 支持不完整,Clang 11 引入了 ODR 指示器(`-fsanitize-address-use-odr-indicator`)等改进才把全局检测做扎实。但**今天**(GCC 8.3+ / Clang 主流版本)对全局越界的检测都是默认开、开箱即用的,本机 GCC 16.1.1 上面这个例子就是默认配置一把抓到的。所以这条"版本门槛"对现在的工具链已经过时,别被老资料带歪。
:::

### 泄漏:LSan 在退出时收尾

最后看内存泄漏。LSan 的工作方式和前几个不一样:它等到 `main` 返回、程序即将退出时,才扫描所有还"活着"的堆分配,把没人引用的、没有对应释放的全标出来。我们写一个故意泄漏的最小例子:

```cpp
// leak.cpp —— 故意泄漏
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=address -g leak.cpp -o leak
#include <cstdlib>
#include <cstdio>
int main()
{
    int* p = (int*)std::malloc(sizeof(int) * 4);  // 拿了堆内存
    p[0] = 42;
    std::printf("ptr = %p\n", (void*)p);  // 让指针逃逸,防止整段被优化器删掉
    // 没有 free,程序退出时 p 指向的内存泄漏
}
```

带 ASan 跑(GCC 16.1.1 / WSL2,LSan 默认随 ASan 启用):

```text
ptr = 0x730c4cbe0010
=================================================================
==364484==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 16 byte(s) in 1 object(s) allocated from:
    #0 0x... in malloc (/usr/lib/libasan.so.8+0x12c161)
    #1 0x... in main leak.cpp:8
    ...

SUMMARY: AddressSanitizer: 16 byte(s) leaked in 1 allocation(s).
```

注意:报告是在 `main` 返回**之后**才打出来的,这正是 LSan "退出时收尾"的工作方式。vol1 的 [动态内存管理](../../vol1-fundamentals/ch12/02-new-delete.md) 也给过一个等价示例,可对照。

::: warning LSan 的"静默退出"陷阱
LSan 在主流 Linux 环境(GCC 16.1.1 / Clang 22)下默认随 ASan 启用,上面这个例子本机能稳定抓到。但要小心一个真实陷阱:**泄漏只有在进程正常退出时才会被扫描**。如果你的程序是被 `SIGKILL` 干掉的、或调了 `_exit` 绕过 `atexit` 钩子、或在某些容器/沙箱里 LSan 的退出钩子没跑起来,泄漏报告就会**静默消失**:程序看起来"没报错",但其实是 LSan 压根没机会扫描。

排查办法:确认进程是正常 return 退出的;需要时显式 `ASAN_OPTIONS=detect_leaks=1` 强制开;长期运行的服务(根本不退出)用不了 LSan 这种"退出时收尾"的模型,得改用 Valgrind massif 或堆采样。别假设 LSan "没报就是没漏"。
:::

## UBSan:把"未定义行为"从沉默变成报错

讲完 ASan 家族的主力,我们看补刀手 UBSan。C/C++ 有个让人血压拉满的特性:**未定义行为(UB)**。编译器对 UB 的态度是"既然标准没规定会发生什么,我就当它不会发生,放心优化"。后果是,有符号整数溢出、位移越界、空指针解引用这些错误,程序**经常看起来跑得好好的**,直到某天开了 `-O2`、换了编译器版本,优化器基于"这段不会溢出"的假设做了激进变换,程序突然算出离谱的结果。

UBSan 的思路是:在每个可能产生 UB 的操作旁边插一个运行时检查,一旦真的发生 UB,立刻打印 `runtime error: ...` 报告(默认不中止程序,可以配置成中止)。开销很小,很多子项可以和 ASan 一起常驻。

看一个最小例子,一次塞进去三种经典 UB:

```cpp
// ubsan.cpp —— UBSan 捕获未定义行为
// Platform: host    Standard: C++20
// 编译: g++ -std=c++20 -O1 -fsanitize=undefined -g ubsan.cpp -o ubsan
#include <cstdio>
#include <limits>

int main()
{
    int arr[4]{1, 2, 3, 4};
    int idx = 10;
    std::printf("越界下标 arr[10] = %d\n", arr[idx]);   // 下标越界

    int max = std::numeric_limits<int>::max();
    std::printf("有符号溢出: %d\n", max + 1);           // 有符号整数溢出

    int shift = 32;
    std::printf("左移 32 位: %d\n", 1 << shift);        // 位移量 >= 位宽
}
```

带 UBSan 跑:

```text
ubsan.cpp:11:55: runtime error: index 10 out of bounds for type 'int [4]'
ubsan.cpp:11:16: runtime error: load of address 0x7ffe8a0525c8 with insufficient space for an object of type 'int'
ubsan.cpp:14:16: runtime error: signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'
ubsan.cpp:17:42: runtime error: shift exponent 32 is too large for 32-bit type 'int'
```

三个 UB 全被抓到,精确到 `文件:行号:列号`。UBSan 覆盖的 UB 清单很长,常见的包括:

- **算术类**:有符号整数溢出/下溢、除以零;
- **位移类**:位移量为负或大于等于位宽、左移把符号位改掉;
- **内存/指针类**:空指针解引用、对齐错误的内存访问、对象大小不匹配(通过错误类型的指针访问);
- **数组类**:下标越界(`-fsanitize=bounds`,这个和 ASan 的越界检测有重叠但侧重不同:ASan 看 redzone,UBSan 看编译期已知的数组尺寸)。

UBSan 的开销取决于你开了哪些子项。`-fsanitize=undefined` 是一组默认子项的集合,多数都很轻;真正贵的是 `-fsanitize=integer`(无符号溢出也算,开销大、误报多,生产慎用)。日常建议:`-fsanitize=undefined` 跟 ASan 一起开,成本低、收益高。

## 选型:面对一个内存 bug,该上哪个工具

到这里五兄弟都亮相过了。问题来了:当你真坐到一个诡异的 bug 面前,该按什么顺序选工具?我们按"症状"来定位:

- **一跑就崩 / 段错误 / 偶发崩溃**:先开 ASan 跑复现测试。越界、UAF、double-free 这三类是段错误最常见的原因,ASan 一把抓。
- **结果偶发性错误 / 跨函数的诡异值**:怀疑 UAF 或数据竞争。先 ASan 排除 UAF,ASan 没报再单独构建一份 TSan 版本查数据竞争(别忘了两者互斥,不能同时开)。
- **算出来的数离谱 / `-O2` 下行为变了**:几乎可以锁定 UB,直接 UBSan。
- **读了"看起来正常"的垃圾值、行为依赖未初始化**:MSan(注意只 Clang、要全程序编译)。
- **进程吃内存越来越多 / 怀疑泄漏**:LSan(退出时报告),或长期运行的服务用 Valgrind massif / 堆采样的方式。

一个工程化的实践是:**CI 里常驻两套构建**:`ASan+UBSan` 一套、`TSan` 一套,每次提交都跑。开销可以接受(ASan+UBSan 在 2-4 倍量级),换来的是把"上线后偶发崩溃"这类最贵的 bug,在还没出门的时候摁住。

::: warning ASan 不是银弹
ASan 很强,但它有几个绕不开的限制,必须心里有数。

第一,**它只能抓"实际执行到"的路径**。CTI 是运行时检测,代码没跑到就不会触发检查。如果你的测试覆盖率不够、某条越界路径从来没被触发过,ASan 抓不到,这正是为什么 ASan 要配合好的测试用例、甚至 fuzzing(模糊测试)一起用,fuzzing 负责把罕见路径跑出来,ASan 负责在这些路径上一旦出错就报。

第二,**只抓内存类错误**。逻辑错误(算错了)、并发错误(数据竞争)、整数溢出这种 UB,ASan 不管:后者归 UBSan,前者归 TSan。别指望一个 flag 解决所有问题。

第三,**生产环境别开**。2-4 倍减速和额外内存,在生产负载下是灾难。ASan/UBSan/TSan 都是**开发/测试/CI 阶段**的工具,发布构建里一定要去掉这些 flag。

第四,**它有假阳性边界**。某些自定义的栈展开机制(`swapcontext`、`vfork`)会让 ASan 的影子区判断出错,报假阳性。报告里那句 `HINT: this may be a false positive if your program uses some custom stack unwind mechanism` 就是在提醒这个。
:::

真正的内存安全要靠 RAII、智能指针、`std::span`、范围 `for` 这些**从语法层面就让你写不出越界和悬空**的手段守住,那些是 vol1 和 vol3 的主题;ASan 这套工具的价值在于过渡期:在你还没把所有裸指针都替换掉、第三方 C 库还没被现代封装包住的时候,它是那道"最后一道防线",让潜伏的内存 bug 在开发阶段就显形,别拖到线上凌晨三点炸给你看。

## 参考资源

- [AddressSanitizer · google/sanitizers Wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer) —— ASan 官方说明,shadow memory 机制与 1:8 映射、2x 开销的权威出处
- [Clang: AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html) —— Clang 侧 ASan 文档,含 `-fsanitize-address-use-odr-indicator` 等全局检测演进
- [Clang: ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html) —— TSan 文档,ASan↔TSan 互斥的出处(详见 vol5 并发调试篇)
- [Clang: UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html) —— UBSan 各子项清单与开销
- [Valgrind User Manual](https://valgrind.org/docs/manual/manual.html) —— DBI 方法与 Memcheck/Helgrind,20-50x 开销的对照
