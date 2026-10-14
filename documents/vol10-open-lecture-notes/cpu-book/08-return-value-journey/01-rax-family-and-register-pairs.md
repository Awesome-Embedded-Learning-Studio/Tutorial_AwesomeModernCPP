---
title: "函数返回值的回家路：rax 家族与成对寄存器"
chapter: 8
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "实测装得进寄存器的两条返回通道：int、bool、指针的数据本身走 rax 家族，十六字节聚合体按八字节切段分给成对的寄存器，整数与浮点各回各的池，填充位上的怪值经 gdb 查明只是过程不是结局"
reading_time_minutes: 13
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 函数返回值的回家路：rax 家族与成对寄存器

```cpp
Big b = make_big(100);
```

这样的一行您天天在写。03 讲传参的时候，咱们把 call 指令送参数出门的方向讲完了，反方向的事只开了一个头：被调函数算完了，结果怎么回到调用方手里？这个方向上攒着两件没讲完的事。[01 对比 i++ 与 ++i 的时候](../01-prefix-vs-postfix-increment/02-class-type-cost.md)，教科书说返回路径上的拷贝有两次，汇编里咱们只见到一次构造，当时给它挂了个 NRVO 的名字就继续赶路了，把机制留给了后面。[03 讲返回值的那部分](../03-arguments-in-registers/02-stack-args-and-return-values.md)，大返回值的 rax 只回传地址这件事咱们验过，两段上限的判据 03 也给过白话版，规则原文、返回侧的寄存器名册，还有一条不看体量的例外，留给咱们这个主题来讲。这两件事咱们在后几篇里一起讲完。

返回值的回家路按体量与类别分成了三条物理通道。装得进一个寄存器的，数据本身走的是 rax。十六字节上下的聚合体，按八字节的规格切段，分给两个寄存器、整数与浮点各回各的池。再大的，就交给一个隐藏的指针去搬运，让被调函数把数据直接写进调用方的地盘。C++ 在三条通道之上还叠了一层省略的机制，它的名字叫拷贝省略，能让一次本该发生的搬运整个消失，咱们在这个主题里把它一路追到汇编里看。

您在文中看到的每一块汇编、每一段 gdb 会话与每一行输出，都逐字来自本机的真实运行，例子与实验全部是本站的原创。工具用的还是老几样，汇编是 g++ 出的，反汇编是 objdump 做的，断点读寄存器的活归 gdb，您跟着前几篇走过一遍的话，这一篇的读法都不陌生。

## 咱们的实验环境

照惯例咱们把环境摆在最前面，后面的每一块汇编，您都可以在同样配置的机器上复现：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

还有几条会影响解读的环境事实。您复跑要是跟文中对不上，咱们建议您对照下面的几条：

- 咱们没有显式给 `-std`，GCC 16 的默认方言是 gnu++20，用 `echo | g++ -dM -E -x c++ - | grep __cplusplus` 实测 `__cplusplus` 为 202002L。涉及拷贝省略语义的实验另配 `-std=c++14` 对照，因为 C++17 把那块语义改了，老方言的数字不能跟新方言混着报。
- 取汇编的口径统一是 `g++ -S -O0 -fno-asynchronous-unwind-tables`，被测函数一律挂 `__attribute__((noinline))`，让函数体在 -O0 下保持独立原貌。讲打包的那一节另附 -O2 的视角。
- 这个开关对 C++ 编译实际不生效：g++ 要靠栈展开表在抛异常时找回 catch，会把它重新打开，01 实测过这一点。所以贴出来的汇编是手工删去了 `.cfi_*` 行与 `.ident` 一类文件级元数据的，指令序列逐字未动，省略处都在块前注明。
- 返回值一律用 volatile sink 消费，防止整段被优化删掉。07 专门讲过 volatile 禁的是什么，咱们这里只把它当观察工具。
- 计数器实验里，sink 只读成员，不触发拷贝，观察手段不干扰观察对象。
- 汇编统一 AT&T 语法，源操作数在左、目的在右，寄存器带 `%`、立即数带 `$`，您在 01 里已经用过这套读法。

## 一个寄存器装得下的，走 rax 家族

头一条路从最小的返回值走起。int、bool、裸指针三种的体量都不超过八字节，所以一个寄存器就装得下。咱们实测一遍，顺便看它们各自落在 rax 的哪一段。源码咱们贴在下面：

```cpp
// 小件返回走 rax —— int / bool / 裸指针 三种返回
int get_int(int x) __attribute__((noinline));
bool get_bool(int x) __attribute__((noinline));
int* get_ptr(int* p) __attribute__((noinline));

int get_int(int x)   { return x + 1; }
bool get_bool(int x) { return x > 0; }
int* get_ptr(int* p) { return p + 1; }

int main() {
    volatile int  vi = get_int(41);
    volatile bool vb = get_bool(5);
    int arr[4] = {0, 0, 0, 0};
    volatile int* vp = get_ptr(arr);
    return vi + vb + (vp != nullptr);  // 消费 sink,防止整体被删
}
```

编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r1_small.cpp -o r1_small.s`，方言取默认的 gnu++20。咱们把三个函数体逐字贴出来，省略了 `.cfi_*` 行：

```asm
_Z7get_inti:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	-4(%rbp), %eax
	addl	$1, %eax
	popq	%rbp
	ret

_Z8get_booli:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	cmpl	$0, -4(%rbp)
	setg	%al
	popq	%rbp
	ret

_Z7get_ptrPi:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	addq	$4, %rax
	popq	%rbp
	ret
```

咱们挨个看。`get_int` 干的事是把实参溢出到栈再读回来，`addl $1, %eax` 算出的结果落在 eax，而三十二位的写入会顺手把 rax 的高三十二位清零，这是 x86-64 的硬性行为，所以整个 rax 都是定义好的值。`get_bool` 用 `setg %al` 把比较的结果写进 al 一个字节。`get_ptr` 走的是整宽 rax，`addq $4, %rax` 做的是指针前进一个 int 的步长。三种小件的共同点是，数据回程走的都是 rax 家族，eax、al、rax 是同一个寄存器的不同宽度。ABI 文档的寄存器用法表里，rax 挂的身份是 1st return register，也就是第一返回寄存器的意思，rdx 那一栏写的则是第二返回寄存器，咱们马上会用到它。

main 侧的接收也一样直白，咱们把三处接收逐字节选出来，栈保护的尾声省略了：

```asm
	movl	$41, %edi
	call	_Z7get_inti
	movl	%eax, -44(%rbp)
	movl	$5, %edi
	call	_Z8get_booli
	movb	%al, -45(%rbp)
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_Z7get_ptrPi
	movq	%rax, -40(%rbp)
```

接收的宽度跟发送的宽度严格对齐，`movl %eax` 收的是 int，`movb %al` 收 bool 的一个字节，指针用的是 `movq %rax` 收整宽。程序跑起来的退出码是多少，咱们验一下语义：

```bash
g++ -O0 r1_small.cpp -o r1_small && ./r1_small; echo exit=$?
```

```text
exit=44
```

咱们核对一下数字：44 的构成是 42 加 1 加 1，vi 的值是 42，vb 与指针非空判定各贡献了一个 1，所以语义没有跑偏。

## 十六字节上下：成对的寄存器与两套寄存器池

体量长到了十六字节，一个寄存器就装不下了，但也还没到要动用内存的程度，约定把它分给了两个寄存器。咱们准备两个十六字节的结构体，一个装的是纯整数，另一个掺了浮点，看看寄存器怎么分。

```cpp
// 16 字节内结构体返回拆寄存器
// Pair8: 两个 long,16 字节 —— 预期 INTEGER+INTEGER -> rax+rdx
// Mixed: int + double,16 字节(4+4pad+8) —— 按 ABI 归类实测
struct Pair8 { long a; long b; };
struct Mixed { int a; double b; };

static_assert(sizeof(Pair8) == 16, "Pair8 must be 16 bytes");
static_assert(sizeof(Mixed) == 16, "Mixed must be 16 bytes");

Pair8 make_pair8(long a, long b) __attribute__((noinline));
Mixed make_mixed(int a, double b) __attribute__((noinline));

Pair8 make_pair8(long a, long b) { return {a, b}; }
Mixed make_mixed(int a, double b) { return {a, b}; }

int main() {
    Pair8 p = make_pair8(11, 22);
    Mixed  m = make_mixed(33, 4.5);
    volatile long sink1 = p.a + p.b;          // 消费,防删
    volatile double sink2 = m.a + m.b;
    return sink1 == 33 && sink2 == 37.5 ? 0 : 1;
}
```

### Pair8：两段整数，两个寄存器

编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r2_struct.cpp -o r2_struct.s`。咱们把两个函数体逐字贴出来，省略了 `.cfi_*` 行：

```asm
_Z10make_pair8ll:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	popq	%rbp
	ret

_Z10make_mixedid:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movsd	%xmm0, -16(%rbp)
	movl	-4(%rbp), %ecx
	movq	%rax, %rdi
	movabsq	$-4294967296, %rsi
	andq	%rdi, %rsi
	orq	%rsi, %rcx
	movq	%rcx, %rax
	movq	-16(%rbp), %rdx
	movl	%eax, %ecx
	movq	%rdx, %xmm0
	movl	%ecx, %eax
	popq	%rbp
	ret
```

`make_pair8` 的返回动作就两条 mov，第一个成员进的是 rax，第二个进的是 rdx。main 那边也是两笔 mov 分别落进 p 的两个成员，咱们把接收段逐字节选出来，栈保护与浮点比较的尾部省略：

```asm
	movl	$22, %esi
	movl	$11, %edi
	call	_Z10make_pair8ll
	movq	%rax, -32(%rbp)
	movq	%rdx, -24(%rbp)
	movq	.LC0(%rip), %rax
	movq	%rax, %xmm0
	movl	$33, %edi
	call	_Z10make_mixedid
	movl	%eax, %edx
	movq	%xmm0, %rax
	movl	%edx, -16(%rbp)
	movq	%rax, -8(%rbp)
```

没有整块的十六字节搬运，Pair8 被切成了两段，各走各的寄存器。切分的单位您在 03 讲传参的时候见过：ABI 归类的单位是 eightbyte，也就是八个字节的意思。Pair8 占的正好是两段，两段含的都是整数内容，所以都归进了 INTEGER 类，而 INTEGER 在返回这一侧的寄存器序列是 rax、rdx，与传参侧的 rdi、rsi 序列平行，是两套独立的名册。咱们运行 `./r2_struct`，得到的退出码是 0，数据完整地到了家。

### Mixed：整数半走 rax，浮点半走 xmm0

`make_mixed` 的汇编比 Pair8 多出一串，咱们直接看主干。Mixed 的前八字节装的是 int a 加四字节的填充，归进了 INTEGER 类，走的是 rax。后八字节装的是 double b，归的是 SSE 类，走的是 xmm0。main 的接收也印证了这个分工，`movl %eax` 只收四字节的 a，`movq %xmm0` 收的是 b。整数走的是通用寄存器，浮点走的是向量寄存器，返回侧的两套池跟 03 在传参侧看到的一样，名额也是互不占用的。

### 填充位上的一次虚惊

`make_mixed` 中段的一串拼接指令，笔者头一回读到的时候差点当成大发现，干活的正是 `movabsq`、`andq`、`orq` 这几位。它们把进函数时 rax 里的旧值拼进了第一个八字节的填充位。进来的旧值是什么？gdb 告诉咱们，读到的值是 0x4012000000000000，恰好是 main 装 4.5 进 xmm0 时留在 rax 里的位模式。乍看之下像是垃圾数据混进了返回值、马上要流出到调用方。咱们拿 gdb 把入口和出口的寄存器都读一遍，编译用的命令是 `g++ -O0 -g r2_struct.cpp -o r2_dbg`：

```text
$ gdb -q -batch \
    -ex 'set debuginfod enabled off' \
    -ex 'break make_mixed' \
    -ex 'run' \
    -ex 'printf "ENTRY : incoming rax = 0x%016lx\n", $rax' \
    -ex 'finish' \
    -ex 'printf "RETURN: rax          = 0x%016lx\n", $rax' \
    -ex 'printf "RETURN: xmm0 bits    = 0x%016lx\n", (unsigned long)$xmm0.v2_int64[0]' \
    ./r2_dbg
Breakpoint 1 at 0x113b: file r2_struct.cpp, line 14.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_mixed (a=33, b=4.5) at r2_struct.cpp:14
14	Mixed make_mixed(int a, double b) { return {a, b}; }
ENTRY : incoming rax = 0x4012000000000000
0x0000555555555198 in main () at r2_struct.cpp:18
18	    Mixed  m = make_mixed(33, 4.5);
Value returned is $1 = {a = 33, b = 4.5}
RETURN: rax          = 0x0000000000000021
RETURN: xmm0 bits    = 0x4012000000000000
```

会话把结局亮出来了。返回时刻的 rax 是 0x0000000000000021，出口前的 `movl %ecx, %eax` 是一笔三十二位写入，它把 rax 的高半整个清了零，中段拼进去的垃圾根本没能活着离开函数。xmm0 里的 0x4012000000000000 正是 4.5 的位模式，0x21 的十进制就是 33。笔者差点把垃圾泄漏写进笔记，后来划掉了：垃圾拼接是 -O0 代码生成的真实形态，出口的截断同样是真实形态，咱们不能拿过程当结局。ABI 在这件事上也有明文的规定，脚注 20 写的是：

```text
When a value of a type of class INTEGER is returned or passed in a register or
on the stack, the excess bits that would not be present in the memory
representation of the type (see figure 3.1) are unspecified.
```

翻译过来：INTEGER 类的值经寄存器或栈传递时，超出类型内存表示的多余位是未指定的。填充位本来就不在约定的范围内，今天 GCC 的 -O0 恰好把它清零，换了编译器或者优化级别，留什么值都合规。您往后在调试器里读寄存器全宽，看到高位有怪值的时候，要多想一层填充位的事。装得进寄存器的两条通道咱们验到这儿，再大的返回值怎么回家，咱们下一篇接着看。
