---
title: "浮点参数、结构体装箱与 ABI 文档的出处"
chapter: 3
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "double 参数从 xmm0 起另占一个寄存器池，结构体按八字节装箱拆段归类，Pair 一个 rdi 装走，Mixed 拆进整数与向量两个池，最后交代 System V AMD64 ABI 文档的出处与整数名单的原文"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 浮点参数、结构体装箱与 ABI 文档的出处

这一篇咱们接着验 double 和结构体两种参数，顺带把这套约定的文档出处交代清楚。咱们用的环境与[第一篇](01-integer-args-register-allocation.md)相同，您要复跑就照它来。

## 浮点参数另有一路：从 xmm0 起

整数有整数的寄存器，浮点数有浮点的。咱们换三个 double 进来：

```cpp
// c5_double.cpp
__attribute__((noinline)) double avg3(double x, double y, double z) {
    return (x + y + z) / 3.0;
}

volatile double sink = 0;

int main() {
    double r = avg3(1.5, 2.5, 3.5);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c5_double.cpp -o c5_double.s
```

```asm
_Z4avg3ddd:
	pushq	%rbp
	movq	%rsp, %rbp
	movsd	%xmm0, -8(%rbp)
	movsd	%xmm1, -16(%rbp)
	movsd	%xmm2, -24(%rbp)
	movsd	-8(%rbp), %xmm0
	addsd	-16(%rbp), %xmm0
	addsd	-24(%rbp), %xmm0
	movsd	.LC0(%rip), %xmm1
	divsd	%xmm1, %xmm0
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movsd	.LC1(%rip), %xmm1
	movsd	.LC2(%rip), %xmm0
	movq	.LC3(%rip), %rax
	movapd	%xmm1, %xmm2
	movapd	%xmm0, %xmm1
	movq	%rax, %xmm0
	call	_Z4avg3ddd
	movq	%xmm0, %rax
	movq	%rax, -8(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	%xmm0, sink(%rip)
	movl	$0, %eax
	leave
	ret

	.section	.rodata
	.align 8
.LC0:
	.long	0
	.long	1074266112
	.align 8
.LC1:
	.long	0
	.long	1074528256
	.align 8
.LC2:
	.long	0
	.long	1074003968
	.align 8
.LC3:
	.long	0
	.long	1073217536
```

被调方的 spill 序列咱们可以直接读：x、y、z 依次占的是 xmm0、xmm1、xmm2，`movsd` 是 SSE 的双精度搬运指令。浮点返回值走的同样是 xmm0，`divsd` 的结果就留在那里，main 随后用 `movq %xmm0, %rax` 把它转进了通用寄存器再写进 sink。

main 那边的装货在咱们看来值得玩味。GCC 把三个常量从 `.rodata` 读进了 xmm1、xmm0 与 rax，再用两条 `movapd` 加一条 `movq` 洗牌到最终的位置，洗完的结果是 xmm2 装 3.5、xmm1 装 2.5、xmm0 装 1.5。常量本身是可以验算的，咱们拿 .LC3 来看，它高 32 位的 1073217536 换算成十六进制是 0x3FF80000，与低 32 位的 0 拼出 64 位位型 0x3FF8000000000000，按 IEEE 754 的双精度解码就是 1.5。.LC2 对应的是 2.5，.LC1 对应的是 3.5。咱们再看 gdb 的实测：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break avg3' -ex 'run' -ex 'p $xmm0.v2_double[0]' -ex 'p $xmm1.v2_double[0]' -ex 'p $xmm2.v2_double[0]' -ex 'printf "=== general regs rdi/rsi untouched by FP args: ===\n"' -ex 'info registers rdi rsi' ./c5_double
Breakpoint 1 at 0x112c: file c5_double.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, avg3 (x=1.5, y=2.5, z=3.5) at c5_double.cpp:3
3	    return (x + y + z) / 3.0;
$1 = 1.5
$2 = 2.5
$3 = 3.5
=== general regs rdi/rsi untouched by FP args: ===
rdi            0x1                 1
rsi            0x7fffffffd1f8      140737488343544
```

xmm0 到 xmm2 报出的是 1.5、2.5、3.5，与实参对上了。下面那两行在咱们看来更有意思：rdi 和 rsi 里躺着的 1 和一个栈地址都是残留值，浮点参数连通用寄存器的边都没碰。两套寄存器池是独立的，整数池排整数池的名次，向量池排向量池的名次，互相不占用对方的名额。ABI 文档对这一路的原话是：

```text
If the class is SSE, the next available vector register is used,
the registers are taken in the order from %xmm0 to %xmm7.
```

类别是 SSE（浮点向量一类）的时候，占用的寄存器从 xmm0 排到 xmm7。整数的名额是六个，而向量的名额有八个。两池独立这个设定咱们记下来，到结构体的部分它会变成主角。

## 小结构体：按八字节装箱

前面的参数都是标量，结构体按值传参的时候会发生什么？vol1 的[函数基础与参数传递](../../../vol1-fundamentals/c_tutorials/05-function-basics.md)讲过值传递的语义：实参拷贝了一份交给函数，函数拿到的是副本。咱们现在要看的，就是副本在机器层面的搬运过程。第 1 个例子是个 8 字节的纯整数结构体：

```cpp
// c6_struct_pair.cpp
struct Pair { int a, b; };

__attribute__((noinline)) long pair_sum(Pair p) {
    return p.a + p.b;
}

volatile long sink = 0;

int main() {
    Pair p{3, 4};
    long r = pair_sum(p);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -S -fno-stack-protector -fno-asynchronous-unwind-tables -o c6_pair_nosp.s c6_struct_pair.cpp
```

```asm
_Z8pair_sum4Pair:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movl	-8(%rbp), %edx
	movl	-4(%rbp), %eax
	addl	%edx, %eax
	cltq
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$3, -16(%rbp)
	movl	$4, -12(%rbp)
	movq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_Z8pair_sum4Pair
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

本节的命令同样带了 `-fno-stack-protector`，main 里就没有金丝雀了。真正的戏码咱们看调用侧的 `movq -16(%rbp), %rax` 加 `movq %rax, %rdi`：main 在栈上把 p 的两个 int 摆好（a 在 -16、b 在 -12），然后用一次 `movq` 就把 8 字节整体读了出来，再用一次 `movq` 把它整体交给了 rdi。被调方那边是对称的，`movq %rdi, -8(%rbp)` 把 8 字节一口气 spill 进了栈，之后再用 `movl` 按 int 的粒度拆着取。整个传递是按 8 字节块整体装箱的，成员没有各自的座位。gdb 的读数把这件事摆到了明面上：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break pair_sum' -ex 'run' -ex 'info registers rdi' -ex 'printf "rdi low32 (a)  = %d\n", (int)($rdi & 0xffffffff)' -ex 'printf "rdi high32 (b) = %d\n", (int)($rdi >> 32)' -ex 'p p' ./c6_pair
Breakpoint 1 at 0x1141: file c6_struct_pair.cpp, line 5.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, pair_sum (p=...) at c6_struct_pair.cpp:5
5	    return p.a + p.b;
rdi            0x400000003         17179869187
rdi low32 (a)  = 3
rdi high32 (b) = 4
$1 = {a = 3, b = 4}
```

rdi 的完整值是 0x400000003，低 32 位装的是 a=3，高 32 位装的是 b=4，咱们在一个 64 位寄存器里肉眼就能看见两个 int。这就是装箱的样子。

为什么按 8 字节装箱？因为 ABI 归类参数的单位就是它，规则里给它的名字是 eightbyte（八字节）。任何参数都按每八字节一段的规格来切分和归类：结构体的尺寸向上取整到 8 的倍数，切出来的每一段各自判定类别，含整数、指针一类内容的段归 INTEGER 类，占的是通用寄存器。纯浮点的段归 SSE 类，占的是 xmm。所以超过两段（在普通情形下）的聚合体会整个转成 MEMORY 类上栈走。返回值那边归的也是同一条规则：make_big 的 24 字节折成三段，超出了两段的上限，所以整个落进 MEMORY 类，走的是隐藏指针回传，[上一篇](02-stack-args-and-return-values.md)中段那场地址接力就是它的样子。Pair 的体量正好是一段，整段的类别是 INTEGER，于是一个 rdi 就装走了。文档 p.29-30 里还有一段官方的示例，示例函数收的是两个整数 e、f 加一个结构体 s，s 的类型是 `struct { int a, b; double d; }`，官方给出的分配是 e 进 rdi，f 进的是 rsi，s 的前八字节（a 与 b 打包）进 rdx，s 的 double 进 xmm0。您看，官方自己演示的就是咱们刚实测的装箱：`{int, int}` 打包进的是一个通用寄存器，double 分去的是 xmm，跟本篇 Pair 和 Mixed 那两场实验是同构的。

第 2 个例子把两池独立这件事用上了。咱们给结构体换成员：一个 int 加一个 double。

```cpp
// c6_struct_mixed.cpp
struct Mixed { int a; double b; };

__attribute__((noinline)) double mixed_sum(Mixed m) {
    return m.a + m.b;
}

volatile double sink = 0;

int main() {
    Mixed m{5, 6.5};
    double r = mixed_sum(m);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -S -fno-stack-protector -fno-asynchronous-unwind-tables -o c6_mixed_nosp.s c6_struct_mixed.cpp
```

```asm
_Z9mixed_sum5Mixed:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, %eax
	movq	%xmm0, %rdx
	movq	%rax, -16(%rbp)
	movq	%rdx, -8(%rbp)
	movl	-16(%rbp), %eax
	pxor	%xmm1, %xmm1
	cvtsi2sdl	%eax, %xmm1
	movsd	-8(%rbp), %xmm0
	addsd	%xmm1, %xmm0
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	$5, -32(%rbp)
	movsd	.LC0(%rip), %xmm0
	movsd	%xmm0, -24(%rbp)
	movl	-32(%rbp), %edx
	movq	-24(%rbp), %rax
	movl	%edx, %edi
	movq	%rax, %xmm0
	call	_Z9mixed_sum5Mixed
	movq	%xmm0, %rax
	movq	%rax, -8(%rbp)
	movsd	-8(%rbp), %xmm0
	movsd	%xmm0, sink(%rip)
	movl	$0, %eax
	leave
	ret

.LC0:
	.long	0
	.long	1075445760
```

Mixed 在内存里的布局是 a 占 0 到 3 字节、填充占 4 到 7、b 占 8 到 15（填充的来路见 vol1 的[结构体与内存对齐](../../../vol1-fundamentals/c_tutorials/12-struct-and-memory-alignment.md)），咱们把它切成两段 eightbyte，各自的去向是这样的：

| 八字节段 | 覆盖字节 | 内容 | 类别 | 去向 |
|---|---|---|---|---|
| 第 1 段 | 0–7 | a 加填充 | INTEGER | rdi（写 edi） |
| 第 2 段 | 8–15 | b | SSE | xmm0 |

一个实参拆成了两半，分头进了两个池。咱们看调用侧的汇编，它正是这么干的：`movl -32(%rbp), %edx` 取的是 a，`movq -24(%rbp), %rax` 取的是 b，随后 `movl %edx, %edi` 与 `movq %rax, %xmm0` 各自把货装走了。被调方进门后用 `movl %edi, %eax` 与 `movq %xmm0, %rdx` 各取了一半，拼回自己的 16 字节局部。gdb 实测对上了：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break mixed_sum' -ex 'run' -ex 'info registers edi' -ex 'printf "rdi low32 (a) = %d\n", (int)($rdi & 0xffffffff)' -ex 'p $xmm0.v2_double[0]' -ex 'p m' ./c6_mixed
Breakpoint 1 at 0x114c: file c6_struct_mixed.cpp, line 5.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, mixed_sum (m=...) at c6_struct_mixed.cpp:5
5	    return m.a + m.b;
edi            0x5                 5
rdi low32 (a) = 5
$1 = 6.5
$2 = {a = 5, b = 6.5}
```

edi 报的是 5，xmm0 报的是 6.5，跟实参是吻合的。这里有个容易误会的细节咱们单独说说：调用方只写了 edi 的低 32 位，rdi 高 32 位对应的填充字节根本没传，gdb 里要是去读 rdi 的完整值，高位看到的是上一场留下的残留。它算不上 GCC 的偷懒，ABI 文档在这一点的原话是：

```text
When a value of a type of class INTEGER is returned or passed in a
register or on the stack, the excess bits that would not be present
in the memory representation of the type (see figure 3.1) are
unspecified.
```

咱们把它翻译过来：INTEGER 类的值经寄存器或栈传递时，超出该类型内存表示的多余位是未指定的。高位本来就不在约定的范围内，接收方被允许消费的只有有效位。这也是咱们挑 Pair 来展示打包证据的原因：纯 int 的 8 字节装箱里高位也是有效数据，gdb 里一个 rdi 就把两个成员都亮出来了。Mixed 的高位是残留，拿它的完整寄存器值当打包证据反而会误导。

## 这套约定写在哪，谁认它

咱们一路实测下来，rdi 到 r9 的名单出自哪份文档，也该交代了：它是 System V AMD64 ABI，一份由各家共同维护的文档，它的正式题名是 `System V Application Binary Interface, AMD64 Architecture Processor Supplement (With LP64 and ILP32 Programming Models)`。咱们查到的版本是 Version 1.0，落款的日期是 2025 年 3 月 12 日，全文的篇幅是 154 页，编者的署名是 H.J. Lu 等人。参数传递的规则在第 3.2.3 节 Parameter Passing，就在 PDF 的第 27 到 29 页。文档的维护仓库在 [gitlab.com/x86-psABIs/x86-64-ABI](https://gitlab.com/x86-psABIs/x86-64-ABI)，仓库如今已经转用 LaTeX 源维护了。笔者头一回去找它的时候，直接下了 x86-64-ABI.pdf 的 raw 链接，拿到的只有 404，笔者后来才发现，能用的 PDF 挂在 README 指向的 CI 构建产物里：[abi.pdf](https://gitlab.com/x86-psABIs/x86-64-ABI/-/jobs/artifacts/master/raw/x86-64-ABI/abi.pdf?job=build)，咱们检索的日期是 2026 年 10 月 12 日。PDF 的构建对应仓库 master 的最新提交，提交的日期是 2025-03-12，与落款是一致的。

整数名单的原文，咱们在[第一篇](01-integer-args-register-allocation.md)列分配表的地方就该请出来的，现在咱们把正主补上：

```text
If the class is INTEGER, the next available register of the sequence
%rdi, %rsi, %rdx, %rcx, %r8 and %r9 is used
```

类别是 INTEGER 的时候，占用的就是 rdi、rsi、rdx、rcx、r8、r9 这个序列，里头下一个还没用掉的寄存器。咱们这三篇的实测，六参数的分配、栈参数的触发、Mixed 的拆分，全是这一句加上配套规则的具体执行。Linux 这边的 GCC 是咱们一路实测用的编译器，它生成调用代码时就是照着这一句办的，前面几篇咱们看到的每一次装货，都是它按这一句生成的代码。

而这套约定本来就没绑定在硬件上。Windows 的 x64 走的是微软自家的另一套调用约定，寄存器的名单对不上号，咱们本篇的实验没有覆盖它，您要跟它打交道，就得去查微软自家的文档。从调试的场景说起：在 Linux 上用 gdb 看到的第 1 个整数参数住在 rdi，换了 Windows 平台的调试器，它就不在 rdi 里了，咱们这一篇的方法搬得过去，寄存器的名字搬不过去。源代码这边您也别假设参数的寄存器去向：普通函数您既假设不了、也用不着假设，真正需要跟寄存器分配打交道的地方是内联汇编、裸函数这一类边界写法，跨平台的代码在那里就更得靠条件编译分头处理。

## 把参数区补回栈帧图

最后咱们回到上一个主题的栈帧图。那幅图里有保存的旧 rbp（rbp+0）、返回地址（rbp+8）和负向偏移的局部变量区，rbp 再往上的部分是空着的，篇末的预告咱们也记着：更多的参数往哪放，答案就摆在前面的实验里。现在能补上了：正数偏移的那一侧，rbp+16 起的位置是栈参数区，第 7 个参数住的是第 1 格，第 8 个参数住的是第 2 格，越靠右的参数地址越高，这一区的做法是调用方压进去，被调方按约定的偏移取走，call 返回后的清栈也归调用方。负数偏移的那一侧装的是被调方 spill 进来的参数副本和局部变量，值是从 rdi、rsi 六个名额里倒过来的。一个 rbp 的两侧就这样分了工，上一个主题的跳转骨架加上这几篇的装货与卸货，一次完整调用的全貌就拼齐了。

还有一件事咱们留给系列后面。您今天已经认识 rdi 这个第 1 参数位了，等到咱们讲成员函数的那一篇，咱们再把它请出来：调用一个成员函数的时候，对象自己的地址就走这个位置，它的名字叫 this 指针。到时候咱们再拿今天这套读法去 gdb 里，它一露头咱们就能认出来。
