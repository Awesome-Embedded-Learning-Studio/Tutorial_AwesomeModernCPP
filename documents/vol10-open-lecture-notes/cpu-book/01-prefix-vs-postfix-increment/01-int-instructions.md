---
title: "i++ 与 ++i：int 在 -O0 下的汇编对比"
chapter: 1
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "搭好汇编实验的环境与读法底子，对比 int 自增在 -O0 下的输出：取值场景后缀多一条暂存旧值的指令，丢弃场景两种写法一条不差"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# i++ 与 ++i：int 在 -O0 下的汇编对比

```cpp
int a = i++;  // a 拿到自增前的旧值
int b = ++i;  // b 拿到自增后的新值
```

这两行您肯定写过无数遍。前缀后缀的语义差别、`i = i++` 那类未定义行为，vol1 的 [运算符基础：让数据动起来](../../../vol1-fundamentals/c_tutorials/03A-operators-basics.md) 讲过，咱们这里只复习一句：后缀把自增前的值交给表达式，前缀把自增后的值交出来。

与语义差别并肩流传的，还有一句性能口诀：循环里要写 `++i` 而不是 `i++`，因为后缀的开销大。笔者以前也是这么背的，直到有一天被人问住：慢在哪条指令上？答不上来。那就别背了，咱们把编译器的输出摆到桌面上，一条一条地对过去。

这一篇的例子与实验都是本站原创的，您在文中看到的每一块汇编和每一行输出，都来自本机的真实运行。

## 咱们的实验环境

照本站的惯例，咱们把环境摆在最前面，后面的每一块汇编，您都可以在同样配置的机器上复现：

```text
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

```text
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

还有几个会影响解读的环境事实，您自己复跑要是和文中对不上，多半就出在这里了：

- 咱们没有显式给 `-std`，GCC 16 的默认语言标准是 gnu++20（`__cplusplus` 为 202002L），下文所有实验都按这个默认走。
- 发行版默认开了 `-fstack-protector-strong`，部分 -O0 函数会带栈金丝雀指令（和 `%fs:40` 相关），与主题无关，您看到别慌。
- 本机输出里没有 `endbr64`。它是开了 CET（Intel 的控制流强制技术，约束间接跳转只能落到合法入口）以后每个函数开头都会有的指令，您若开了 CET 会多出它，属正常差异。
- 笔者本想用 `-fno-asynchronous-unwind-tables` 给汇编减噪，实测它对 C++ 编译不生效：`.cfi_*` 伪指令行照样在，因为 C++ 抛异常时要靠栈展开（unwind）表一路找回 catch，g++ 会把它重新打开。所以下文贴汇编时，统一手工删掉 `.cfi_*` 行和 `.globl`、`.size` 一类伪指令，指令部分一条未动，后文不再重复说明。

您在汇编里偶尔见到的引号（比如 `"_Z8use_posti"`）也是原文如此：Intel 语法的输出会给含特殊字符的符号名加引号，那是排版的产物，您可别当成错误。

## 看汇编之前的准备

咱们要用到的工具只有 g++ 自己。怎么拿到汇编？严格意义的反汇编，指从编译好的机器码倒推回汇编文本，`objdump` 干的就是这个活。咱们这里更省事，直接让编译器把汇编写出来：您给 g++ 加上 `-S`，得到的就是一个 `.s` 文本文件。两种来源的读法一样，而 `-S` 的输出还带着函数名和标签，咱们拿来对比正合适。

-O0 与 -O2 是什么？编译选项里的 O 来自 optimization（优化）。`-O0` 是不做优化的档位，编译器把每一条语句都老老实实地直译成指令，变量各住各的栈槽，每一步的读写都回到内存，调试器的单步走起来最舒服。`-O2` 打开常用的优化，是发布版本的常态。咱们要看的东西，说到底就是同一件事在这两档下的长相差别。

两种汇编语法有什么差别？x86 的汇编有两套书写习惯。AT&T 语法是 GNU 工具链的默认，操作数的方向与 C 赋值相反：源在左、目的在右，寄存器的名字带 `%`、立即数带 `$`。Intel 语法的方向与 C 赋值一致，`mov eax, 1` 就是把 1 放进 eax 的意思。本篇两组都会用到：头一组实验咱们用 Intel 语法细看（编译时加 `-masm=intel`），它对写 C++ 的人更友好。从第二组起咱们切回 AT&T，因为您往后自己用 objdump 一类工具看到的都是它。往下每个汇编块旁边都标了语法，您对着读就行。

寄存器是什么？寄存器是 CPU 内部自带的几个极快的存储格子，eax、edx 是 32 位的通用寄存器，rax、rdx 是它们的 64 位加宽版。rbp 通常被编译器用作栈帧的锚：函数的局部变量按它的负向偏移安排，`-20[rbp]` 读作 rbp 减 20 那个地址上的内存，前面的 `DWORD PTR` 标明按 32 位宽访问。至于 `_Z8use_posti` 这样的怪名字，那是 GCC 给 C++ 函数修饰出的符号名，把名字长度和参数类型编了进去，您用 `c++filt` 一还原就是 `use_post(int)`。

## int 的取值场景：-O0 下后缀多一条指令

咱们从最朴素的场景开始：操作数是 int 的普通变量，而且自增表达式的值被人拿走了。往后您会看到，这样的限定是有讲究的，自己动手时千万写成取值的样子。

```cpp
volatile int sink;

void use_post(int i) {
    int x = i++;
    sink = x;
}

void use_pre(int i) {
    int x = ++i;
    sink = x;
}

int main() {
    use_post(41);
    use_pre(41);
    return 0;
}
```

咱们在这里拿 `volatile` 当防优化的锚点。它要求对 sink 的每次读写都必须真实发生，读写的顺序也得保持原样。于是无论咱们把优化开到几档，两个函数里对 x 的搬运都有落点，对比的意义也就保住了。咱们的编译命令：

```bash
g++ -O0 -fno-asynchronous-unwind-tables -masm=intel -S a1.cpp -o a1_intel.s
```

（unwind 选项对 C++ 是无效的，前面咱们已经说过、留着也无害。文件名里的 a1 是笔者做实验时的编号，后面两篇里您还会遇到 a3、a4、a6、a7，跳过的编号属于文章没展示的对比场景，复现时您换成什么名字都行。）咱们编译下来一个警告都没见到。后缀版的输出（Intel 语法）：

```asm
"_Z8use_posti":
	push	rbp
	mov	rbp, rsp
	mov	DWORD PTR -20[rbp], edi
	mov	eax, DWORD PTR -20[rbp]
	lea	edx, 1[rax]
	mov	DWORD PTR -20[rbp], edx
	mov	DWORD PTR -4[rbp], eax
	mov	eax, DWORD PTR -4[rbp]
	mov	DWORD PTR "sink"[rip], eax
	nop
	pop	rbp
	ret
```

咱们再看前缀版的输出（Intel 语法）：

```asm
"_Z7use_prei":
	push	rbp
	mov	rbp, rsp
	mov	DWORD PTR -20[rbp], edi
	add	DWORD PTR -20[rbp], 1
	mov	eax, DWORD PTR -20[rbp]
	mov	DWORD PTR -4[rbp], eax
	mov	eax, DWORD PTR -4[rbp]
	mov	DWORD PTR "sink"[rip], eax
	nop
	pop	rbp
	ret
```

开头两行 `push rbp` 和 `mov rbp, rsp` 是 -O0 下函数开场的标配，负责把每个函数的栈帧立起来。它们是干什么用的，系列后面讲函数调用与栈帧时咱们专门看，这里咱们就跳过，您要看的正题从第三行开始。

第三行的 `mov DWORD PTR -20[rbp], edi` 也有讲究。Linux 下 x86-64 的调用约定规定，函数的第一个整数参数从 edi 里传进来，这行指令把参数存进了栈槽，往后的 i 就住在那个位置上。咱们把两个函数的核心段各列一张表：

| 后缀版的指令 | 它干的事 |
|---|---|
| `mov eax, DWORD PTR -20[rbp]` | 把 i 的旧值读进 eax |
| `lea edx, 1[rax]` | 在 edx 里算出 i+1，eax 里的旧值原封不动 |
| `mov DWORD PTR -20[rbp], edx` | 新值写回 i |
| `mov DWORD PTR -4[rbp], eax` | x 被赋成旧值 |
| `mov eax, DWORD PTR -4[rbp]` | 把 x 读回 eax |
| `mov DWORD PTR "sink"[rip], eax` | 写进 sink |

| 前缀版的指令 | 它干的事 |
|---|---|
| `add DWORD PTR -20[rbp], 1` | 内存原地加一，i 直接变新值 |
| `mov eax, DWORD PTR -20[rbp]` | 把新值读进 eax |
| `mov DWORD PTR -4[rbp], eax` | x 被赋成新值 |
| `mov eax, DWORD PTR -4[rbp]` | 把 x 读回 eax |
| `mov DWORD PTR "sink"[rip], eax` | 写进 sink |

咱们对照上面的两张表，您从 x 看起：它明明刚存进了栈槽 `-4[rbp]`，下一条指令又原样读了回来。您可别当成编译器犯了傻，这正是 -O0 的直译作风：每条语句各管各的，语句之间不留寄存器里的捷径。咱们再看 `lea`：它的本职是算地址（load effective address），后缀版在这里把它借来做了加法，因为 `add` 会改写自己的操作数，而旧值必须留在原寄存器里不能动，新值也就只能另找地方算了。

咱们按上面两张表的口径数：从形参落栈那一步往后数到写完 sink，后缀的指令是六条，前缀的指令是五条，形参落栈与开场白、收尾是两边共有的，是没算在内的。多出来的一条指令就是暂存旧值的代价。后缀的语义要求旧值在自增发生的同时还活着，编译器只能让它占住一个独立的寄存器（或栈槽），而前缀压根没有这样的负担，一条 `add` 原地就解决了。

咱们再附一份 AT&T 的对照：同一份输出的另一种书写。

```asm
_Z8use_posti:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	movl	-20(%rbp), %eax
	leal	1(%rax), %edx
	movl	%edx, -20(%rbp)
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret

_Z7use_prei:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -20(%rbp)
	addl	$1, -20(%rbp)
	movl	-20(%rbp), %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret
```

`leal 1(%rax), %edx` 与上面的 `lea edx, 1[rax]` 是同一件事，指令上是一一对应的，只是书写的方向相反。从下一组实验起咱们就固定用 AT&T 了，它是工具链的默认，也是您往后用 objdump 看汇编时见到的样式，Intel 到这里就退场了。

## 旧值没人要的时候：-O0 也看不出差别

咱们刚才的实验里，x 把自增的值拿走了。那么值没人要的时候呢？咱们来猜一猜：换成单独一句 `i++;`，旧值是马上就要被扔掉的，可造它的时候也费一道工，后缀总该多做点功吧。咱们把源码换成两个分开的函数：

```cpp
volatile int sink;

void post_only(int i) {
    i++;
    sink = i;
}

void pre_only(int i) {
    ++i;
    sink = i;
}

int main() {
    post_only(41);
    pre_only(41);
    return 0;
}
```

咱们不能省掉那句 `sink = i;`，不然的话 -O2 会把整个函数体都删光，对比就失去意义了。编译出来的两个函数体是这样的，用的是 AT&T 语法（下同）：

```asm
_Z9post_onlyi:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	addl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret

_Z8pre_onlyi:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	addl	$1, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	nop
	popq	%rbp
	ret
```

您来找茬也找不到，两个函数的指令一条不差（函数名除外）。丢弃值的 `i++;` 在 -O0 下就是一条内存原地的 `addl`，跟 `++i;` 编出来的完全一致。

这比笔者预想的更早合流了。一点优化都没开的 -O0，照理最有机会暴露两种写法的差别，可是旧值没有使用者的时候，暂存旧值的动作根本就不会发生。GCC 的前端（管语法语义的那一半）在展开语句时就直接选了原地加。所以您要自己上手验证，请务必写成取值的样子（`int x = i++;`），裸的 `i++;` 在 -O0 也一模一样，守着它等差异是等不出来的。
