---
title: "大件走隐藏指针：sret 的全链路与分路判据"
chapter: 8
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "二十四字节的 Big 经 rdi 里的隐藏指针把数据直接写进调用方的栈槽，ABI 归类按八字节切段、两段为限，非平凡的拷贝控制不看体量直接换走 sret 通道"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 大件走隐藏指针：sret 的全链路与分路判据

上一篇咱们验完了前两条通道：单段的小件数据本身走 rax 家族，十六字节的聚合体按八字节切段，各领各的寄存器。咱们用的环境与[上一篇](01-rax-family-and-register-pairs.md)相同，您要复跑就照它来。

## 大件走隐藏指针：sret 的全链路

二十四字节的 `struct Big { long a, b, c; }` 装不进两个寄存器，第三条通道为它而设。这套做法您在 03 讲 make_big 的时候见过全程：调用方在栈上备好缓冲，缓冲的地址经 rdi 交给被调函数，待遇跟第一个实参的一模一样，函数把字段直接写进那块地盘，返回时 rax 装的还是那个地址。工具链的资料里常用 sret 称呼它，它是 struct return 的缩写，咱们本篇就借这个词。03 给过两段上限的白话版判据，规则原文与返回侧的寄存器名册咱们下一节请出来，外加一条 03 没讲过的例外。

```cpp
// sret 全链路 —— 24 字节 Big 的返回
struct Big { long a, b, c; };
static_assert(sizeof(Big) == 24, "Big must be 24 bytes");

Big make_big(long x) __attribute__((noinline));
Big make_big(long x) { return {x, x + 1, x + 2}; }

int main() {
    Big b = make_big(100);
    volatile long sink = b.a + b.b + b.c;   // 消费,防删
    return sink == 303 ? 0 : 1;
}
```

编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r3_sret.cpp -o r3_sret.s`，咱们把两个函数体逐字贴出来，省略了 `.cfi_*` 行：

```asm
_Z8make_bigl:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)        ; 溢出隐藏返回槽指针
	movq	%rsi, -16(%rbp)       ; 溢出真实实参 x(已让位到 rsi!)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	movq	%rdx, (%rax)          ; a -> 槽+0
	movq	-16(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 8(%rax)         ; b -> 槽+8
	movq	-16(%rbp), %rax
	leaq	2(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 16(%rax)        ; c -> 槽+16
	movq	-8(%rbp), %rax        ; rax = 还回槽地址
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$48, %rsp
	leaq	-32(%rbp), %rax       ; 缓冲就是 main 自己的局部 b
	movl	$100, %esi            ; 真实实参进 esi(第二槽)
	movq	%rax, %rdi            ; 隐藏指针进 rdi(第一槽)
	call	_Z8make_bigl
	movq	-32(%rbp), %rdx       ; 返回后直接读 b,无搬运
	movq	-24(%rbp), %rax
	addq	%rax, %rdx
	movq	-16(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, -40(%rbp)
	...
```

main 里 `.cfi_*` 行、栈保护校验与返回比较的尾部同样省略。咱们按四个环节对汇编。调用方备槽：`leaq -32(%rbp), %rax` 取的是局部 b 的地址，缓冲就是 b 自己的栈槽，中间没有第二块场地。实参让位：真实实参 x=100 装进了 esi，因为 rdi 的位置被返回缓冲的地址占了，这就是 03 见过的隐藏首参数。直接写槽：make_big 里的三个 movq 把 100、101、102 直接写进了槽内偏移 0、8、16，全程没有二十四字节的整块搬运。还址：出口的 `movq -8(%rbp), %rax` 把进来的地址原样放回 rax。

链路是不是真的这么走，咱们用 gdb 断点实测，编译用的命令是 `g++ -O0 -g r3_sret.cpp -o r3_sret_dbg`：

```text
$ gdb -q -batch \
    -ex 'break make_big' \
    -ex 'run' \
    -ex 'printf "ENTRY  : rdi (hidden sret ptr) = %p\n", $rdi' \
    -ex 'printf "ENTRY  : rsi (real arg x)      = %ld\n", $rsi' \
    -ex 'up' \
    -ex 'printf "CALLER : &b  (main 栈上局部)   = %p\n", &b' \
    -ex 'down' \
    -ex 'finish' \
    -ex 'printf "RETURN : rax (还回的地址)      = %p\n", $rax' \
    -ex 'printf "RETURN : 缓冲里三个字段        = %ld %ld %ld\n", b.a, b.b, b.c' \
    ./r3_sret_dbg
Breakpoint 1 at 0x1145: file r3_sret.cpp, line 8.

This GDB supports auto-downloading debuginfo from the following URLs:
	<https://debuginfod.archlinux.org>
Enable debuginfod for this session? (y or [n]) [answered N; input not from terminal]
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_big (x=100) at r3_sret.cpp:8
8	Big make_big(long x) { return {x, x + 1, x + 2}; }
ENTRY  : rdi (hidden sret ptr) = 0x7fffffffd0a0
ENTRY  : rsi (real arg x)      = 100
#1  0x000055555555519e in main () at r3_sret.cpp:11
11	    Big b = make_big(100);
CALLER : &b  (main 栈上局部)   = 0x7fffffffd0a0
#0  make_big (x=100) at r3_sret.cpp:8
8	Big make_big(long x) { return {x, x + 1, x + 2}; }
main () at r3_sret.cpp:12
12	    volatile long sink = b.a + b.b + b.c;   // 消费,防删
Value returned is $1 = {a = 100, b = 101, c = 102}
RETURN : rax (还回的地址)      = 0x7fffffffd0a0
RETURN : 缓冲里三个字段        = 100 101 102
```

会话里读到的三个 0x7fffffffd0a0 是同一个地址，它们分别是入口的 rdi、main 里 `&b` 的值、出口的 rax。数据由被调函数的写指令直接落进接收变量自己的栈槽，这是一条不搬数据的回家路，寄存器里来回跑的只有地址。这套流程的依据写在 ABI 文档 Returning of Values 一节，咱们把它第 2 步的原文请出来：

```text
2. If the type has class MEMORY, then the caller provides space for the return
value and passes the address of this storage in %rdi as if it were the first
argument to the function. In effect, this address becomes a "hidden" first
argument. ... On return %rax will contain the address that has been passed in
by the caller in %rdi.
```

翻译过来：类型归入 MEMORY 类的时候，调用方为返回值备好了空间，把这块空间的地址放进 rdi，就像它是函数的第一个实参，这个地址成了一个隐藏的首参数。返回时 rax 里装的，正是调用方当初经 rdi 交进来的地址。文档写明的事，咱们刚才在汇编与 gdb 里逐条兑现了。咱们用的还是 03 里介绍过的那本文档，它的版本是 Version 1.0，落款的日期是 2025 年 3 月 12 日，Returning of Values 一节在 PDF 的第 29 页，仓库还在咱们用过的老地方 [gitlab.com/x86-psABIs/x86-64-ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)，咱们检索它的日期是 2026 年 10 月 12 日。

## 分路的判据：八字节归类，两段为限

三条路咱们都见过了，判据也该收进来了。ABI 的做法是拿返回类型过一遍归类算法：类型按八字节切段，每段都判了类，int、指针一类的内容归 INTEGER，double 归的是 SSE，然后再按段去领各自的寄存器。算法的另外三步原文是：

```text
1. Classify the return type with the classification algorithm.

3. If the class is INTEGER, the next available register of the sequence %rax,
%rdx is used.

4. If the class is SSE, the next available vector register of the sequence
%xmm0, %xmm1 is used.
```

第 1 步说返回值用的就是传参那套归类算法，第 3、4 步翻译过来的意思是 INTEGER 类依次领 rax、rdx，SSE 类依次领的是 xmm0、xmm1。Pair8 两段 INTEGER 领走了 rax 加 rdx，Mixed 的 INTEGER 段领 rax、SSE 段领 xmm0，咱们前面的实测全是这两句的执行。那 MEMORY 的门槛在哪？归类收尾规则的第 (c) 条写的是：

```text
(c) If the size of the aggregate exceeds two eightbytes and the first
eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole argument is
passed in memory.
```

翻译过来：聚合体的体量超过两个八字节，只要开头的一段不是 SSE，或者后面的某段不是 SSEUP，整个参数就改成了内存传递。Big 的二十四字节折成三段全是整数，超了限，于是整体转成了 MEMORY，走上一节的隐藏指针。您把它跟传参那边的规则对一下就会发现，参数与返回值共用的是一套归类，只是寄存器名册换了。

判据到这儿还只讲了体量与类别，另外还有一条不看体量的例外，咱们在数拷贝的实验里正好踩到它。

## 加了拷贝计数，通道就换了

接下来咱们要数拷贝构造的次数，数它就得给类型加一个用户提供的拷贝构造。咱们这么一加，类型在 ABI 眼里的身份就变了。咱们用对照实验把这件事看清楚，源码里的 P16 与 Pair8 同为十六字节、同为两个 long，唯一的差别是 P16 多了一个自己写的拷贝构造：

```cpp
// 16 字节、但拷贝构造非平凡 —— 归类是否仍走寄存器?
struct P16 {
    long a, b;
    static long copies;
    P16(long a_, long b_) : a(a_), b(b_) {}
    P16(const P16& o) : a(o.a), b(o.b) { ++copies; }
};
long P16::copies = 0;

P16 make_p16(long a, long b) __attribute__((noinline));
P16 make_p16(long a, long b) { return {a, b}; }

int main() {
    P16 p = make_p16(1, 2);
    volatile long sink = p.a + p.b;
    return sink == 3 ? 0 : 1;
}
```

编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r4a_nontrivial16.cpp -o r4a_nontrivial16.s`。咱们把函数体逐字贴出来，省略了 `.cfi_*` 行：

```asm
_Z8make_p16ll:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)        ; 隐藏 sret 指针
	movq	%rsi, -16(%rbp)       ; 实参 a(让位到 rsi)
	movq	%rdx, -24(%rbp)       ; 实参 b(再让到 rdx)
	movq	-24(%rbp), %rdx
	movq	-16(%rbp), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi            ; 构造函数 this = sret 槽
	call	_ZN3P16C1Ell          ; P16::P16(long,long) 直接写进槽
	movq	-8(%rbp), %rax
	leave
	ret
```

咱们把 main 侧的调用段也贴出来：

```asm
	leaq	-32(%rbp), %rax
	movl	$2, %edx
	movl	$1, %esi
	movq	%rax, %rdi
	call	_Z8make_p16ll
```

程序运行后的退出码是 0。咱们看到的通道跟 Big 的一模一样：rdi 收隐藏指针，两个实参让位到了 rsi 与 rdx，构造函数的 this 取的是 sret 槽，直接把数据写进调用方的地盘，rax 在出口把地址还了回去。体量在这里说了不算，判据里还压着一条类别方面的规则。ABI 归类规则第 2 条写的是：

```text
2. If a C++ object is non-trivial for the purpose of calls, as specified in the
C++ ABI, it is passed by invisible reference (the object is replaced in the
parameter list by a pointer that has class INTEGER)
```

脚注 7 还给了咱们理由：

```text
An object whose type is non-trivial for the purpose of calls cannot be passed
by value because such objects must have the same address in the caller and the
callee. Similar issues apply when returning an object from a function.
```

翻译过来：对调用而言非平凡的 C++ 对象，按不可见引用的方式来传递，对象在参数表里的位置被一个指针顶替。非平凡的意思是，类型带上了用户提供的拷贝控制这一类代码。脚注说的理由更直白：这样的对象在调用方与被调方必须拥有同一个地址，您写的拷贝构造、析构要在对象身上执行，两边地址不同，这些代码看到的对象就不一致了，所以约定干脆让它全程走指针。

平凡这个判据的白话版就是：类型没有自定义的拷贝控制，搬运这样的类型就等于搬字节，编译器想怎么搬都行。一旦您写了拷贝构造，哪怕函数体只是把两个成员复制了一遍，类型就成了非平凡，寄存器分段的待遇随之取消。这也提前解释了一件容易纳闷的事：下一篇的计数实验里，Point 同样是十六字节的体量，它走的是 sret，您别以为是编译器退化了，是计数器把通道换了。三条通道与判据到这儿齐了，下一篇轮到 C++ 语言自己的规则，看返回路径上的拷贝怎么整个不见。
