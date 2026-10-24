---
title: "叶子函数、栈的方向与 -O2 的调用点"
chapter: 2
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard:
  - 11
  - 17
description: "叶子函数靠红区省掉 sub rsp，三层调用的地址证明栈往低处长，-O2 算掉调用点却保住外部实体，取地址则连调用点都得留下"
reading_time_minutes: 12
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 叶子函数、栈的方向与 -O2 的调用点

上一篇咱们走读 three 的时候，在结尾留了个话头，consume 和 add 这类不再调用别人的函数，收尾用的是裸的 pop rbp。这一篇咱们就从它们看起，看一个不再调用别的函数的栈帧能省成什么样。咱们这一篇的汇编，跑在跟[上一篇](01-call-and-stack-frame-walkthrough.md)相同的环境里。

## 没有第三步的叶子函数

叶子函数（leaf function）指的是内部一次 call 都没有的函数，它不再调用别的函数，是调用链的末端。咱们把 three 里的调用拿掉、再加一个 volatile 变量接住求和结果防它被优化掉，就得到一个纯叶子的版本：

```cpp
int three() {
    int x = 1;
    int y = 2;
    int z = 3;
    volatile int sink = x + y + z;
    return sink;
}

int main() {
    return three();
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b3_locals.cpp -o b3_locals.s
```

```asm
_Z5threev:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	$1, -12(%rbp)
	movl	$2, -8(%rbp)
	movl	$3, -4(%rbp)
	movl	-12(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%eax, %edx
	movl	-4(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -16(%rbp)
	movl	-16(%rbp), %eax
	popq	%rbp
	ret
.LFE0:
	.size	_Z5threev, .-_Z5threev
```

汇编里找不到 subq 的影子：prologue 只剩 push rbp 和 mov rbp,rsp 的两步，四个变量（x、y、z、sink）全都直接住在 rbp 的下方，各自的偏移是 -12、-8、-4、-16(%rbp)。凭什么可以这么住？咱们请出 System V ABI 的红区（red zone）来解释。System V ABI 是 Linux 上 C 与 C++ 编译器共同遵守的约定手册：参数走哪个寄存器、栈怎么用，都记在里面了。红区的条目住在手册的第 3.2.2 节（The Stack Frame），原文在 [x86-64 ABI 仓库](https://gitlab.com/x86-psABIs/x86-64-ABI) 的源文件里。红区的约定是 rsp 以下的 128 字节归本函数自由使用，只要本函数不再发 call、就不会有别人的帧压过来覆盖，变量住在 rsp 下面是安全的，sub rsp 的一步自然就省掉了。epilogue 也跟着简化：此时 rsp 本来就停在 rbp 的位置，leave 的拉回动作成了多余，裸的 popq %rbp 加 ret 就够。

很多教程把上一篇那三步（`push rbp`、`mov rbp,rsp`、`sub rsp`）讲成每个函数都有的样子，您自己反汇编一个叶子函数就会发现对不上号。准确的表述是：会调用别人的函数才有 sub rsp 这一步，叶子函数在 -O0 下的汇编里没有这一步，局部变量住的是红区。您手头任何一个小函数，只要它不调用别的函数，-O0 编出来的多半就是上面的样子。

## 栈往低处长：嵌套调用的地址证据

到目前为止咱们都在说往低地址挪，是时候拿运行时的数字验一验了。咱们的做法很直接：三层函数 main 调 outer、outer 调 inner，各层都声明一个自己的局部变量，把自己的地址打印出来。如果栈真的如咱们所说往低处长，越深的那层地址应该越小。

```cpp
#include <cstdio>

void inner() {
    int i = 0;
    std::printf("inner: &i = %p\n", (void*)&i);
}

void outer() {
    int o = 0;
    std::printf("outer: &o = %p\n", (void*)&o);
    inner();
}

int main() {
    int m = 0;
    std::printf("main:  &m = %p\n", (void*)&m);
    outer();
    return 0;
}
```

```text
$ g++ -O0 b4_nest.cpp -o b4_nest
$ ./b4_nest
$ ./b4_nest
```

```text
main:  &m = 0x7fff5d59f9d4
outer: &o = 0x7fff5d59f9b4
inner: &i = 0x7fff5d59f994
main:  &m = 0x7ffd56d4ae94
outer: &o = 0x7ffd56d4ae74
inner: &i = 0x7ffd56d4ae54
```

两次运行的数字都摆在这里：第一回 main 的 m 在 0x7fff5d59f9d4，outer 的 o 在 0x7fff5d59f9b4，inner 的 i 在 0x7fff5d59f994，深一层的地址就小了 0x20、合 32 字节。这 32 字节的构成咱们算得出来：返回地址 8 字节、保存的 rbp 8 字节、sub $16 的局部区 16 字节，加起来的总数正好 32。

咱们再看第二回：三个地址整体变成了 0x7ffd56d4ae94 一带，跟第一回完全不同了。干这件事的是地址空间随机化（ASLR），每一次运行都给栈换了个基线。但无论基线怎么挪，内层更小的次序一次都没变过，咱们要的正是这个不变的次序。

## -O2：调用点消失了，函数实体还在

看过 -O0 的老实样子，咱们把优化开到 -O2，看看一次函数调用会变成什么样子。最常见的变化是：小函数的调用点直接消失。咱们给第一个程序的 s 加上 volatile、逼得结果必须落进内存：

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    volatile int s = add(3, 4);
    return s;
}
```

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5a_inline.cpp -o b5a_inline.s
```

```asm
	.file	"b5a_inline.cpp"
	.text
	.p2align 4
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	leal	(%rdi,%rsi), %eax
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB1:
	movl	$7, -4(%rsp)
	movl	-4(%rsp), %eax
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

main 里已经没有 call 了。`add(3, 4)` 在编译期就算出了 7，volatile 的存储又必须落内存，所以这次调用在 main 里没留下任何指令，只剩 volatile 存储需要的 `movl $7, -4(%rsp)`，紧跟的 `movl -4(%rsp), %eax` 做的是把 s 装进 eax 当返回值。咱们连 inline 展开都跳过了：常量折叠（编译期直接把表达式算成结果）比 inline 更早，就把这次调用吃掉了。`-4(%rsp)` 的写法跟叶子函数一节的 `-4(%rbp)` 只差了基准寄存器：main 在这里也成了叶子函数，变量住的还是红区，只不过寻址的基准从 rbp 换成了 rsp。

inline（内联）指的是一种优化：编译器把函数体直接展开进调用处，所以省掉了 call。有意思的是，这次的汇编里 inline 根本没派上用场。另一边您再看：add 的函数实体还在。`_Z3addii` 依然带着 `.globl` 的导出标记，实体的指令精简成了一条 `leal (%rdi,%rsi), %eax` 加 `ret`，push rbp 与帧都不见了。原因藏在链接属性里：add 是外部链接的，别的编译单元可能引用它，所以 GCC 必须把实体留着。准确的说法是这样的：-O2 消掉的是 main 里的调用点，函数的实体没有消失，只是整份输出里 call 它的指令都不见了。所以“inline 之后函数就没了”说的是过头话，您以后再见到这类说法时可要多留个心眼。

这两种形态放在一起单步看最清楚。下面的执行器里，-O0 的 three 老老实实 push rbp、建帧、把 1、2、3 落栈再搬回来相加，另一边的 add_fast 就是那条精简后的实体：leal 一条加法，ret 收工，连栈帧都不开。您各走一遍，对比栈窗口的动作次数，帧的成本就摆在眼前。符号名去掉了修饰，其余指令与上面的输出一一对应。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/leaf_vs_plain.json" />

## 取走函数地址，实体必须生成

调用点消失的前提是编译器看得清调用目标。咱们把函数地址取走、再经由函数指针去调用，编译期就没法确定指针指向的是谁，函数的实体就必须生成了。代码咱们这么写：

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int (* volatile fp)(int, int) = add;
    volatile int s = fp(3, 4);
    return s;
}
```

有个容易写错的地方值得咱们停下来看：volatile 修饰的是指针本身，正确的写法是 `int (* volatile fp)(int, int)`。要是把它写成 `volatile int (*fp)(int, int)` 的样子，volatile 就跑到返回类型上去了，GCC 16 直接就报了编译错误。您要是打算自己跑一遍，声明就照上面的样子写。

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5c_fptr.cpp -o b5c_fptr.s
```

```asm
	.file	"b5c_fptr.cpp"
	.text
	.p2align 4
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	leal	(%rdi,%rsi), %eax
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB1:
	subq	$24, %rsp
	movl	$4, %esi
	movl	$3, %edi
	leaq	_Z3addii(%rip), %rax
	movq	%rax, 8(%rsp)
	call	*8(%rsp)
	movl	%eax, 4(%rsp)
	movl	4(%rsp), %eax
	addq	$24, %rsp
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

您看 main 这次破天荒开了 24 字节的帧：`leaq _Z3addii(%rip), %rax` 把函数的入口地址算出来，`movq %rax, 8(%rsp)` 负责把它存进 volatile 的函数指针（必须落内存），随后的 `call *8(%rsp)` 做的是间接调用：目标地址从内存里读。volatile 的语义逼出了这一次内存往返，add 的实体也必须生成，因为它的地址已经被取走了。咱们这一取地址，调用点和实体就都留下了。
