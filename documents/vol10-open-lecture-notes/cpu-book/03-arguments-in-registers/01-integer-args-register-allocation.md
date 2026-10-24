---
title: "参数是怎么进寄存器的：整数参数的分配表"
chapter: 3
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "从两个参数到六个参数，实测 Linux x86-64 整数参数进 rdi 到 r9 的分配表，分清 ABI 锁定的分配关系与编译器自己的书写习惯，再用 gdb 把六个寄存器逐一验货"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 参数是怎么进寄存器的：整数参数的分配表

[上一篇](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md)开篇讲 call 与返回地址的时候，gdb 把咱们带到了 add 的门口：断点落在 `return a + b;` 那一行源码上，rdi 里装的是 3，rsi 里装的是 4，参数稳稳地待在寄存器里，栈上还没有它们的影子。当时咱们说过，参数本来走的是寄存器，落栈是被调函数自己的事。这一篇咱们就把前半句讲完：3 凭什么进 rdi？4 凭什么进 rsi？参数来了七个、来了 double、来了结构体的时候，又各往哪放？

这些问题的答案不在 CPU 的指令手册里。x86-64 的手册只登记了一堆寄存器的名字，手册里从没规定过第 1 个参数的住处。咱们拿同一颗 CPU 来看，Linux 上跑的程序用这一套寄存器传参，Windows 上跑的程序用另一套，而芯片对两套都照单全收。真正的出处是一份叫 ABI 的文档。ABI 的全名是 Application Binary Interface，应用二进制接口是它的中文名，说成白话就是调用方与被调方都遵守的参数交接规约：参数该放进哪个寄存器、又从哪里取走、返回值走的是哪条路，两边提前商量好了，编译器生成的机器码就按它来。您写下的每一次函数调用，背后都有它的规约在运转。

这一篇的例子与实验都是本站原创的，您在文中看到的每一块汇编与每一段调试器会话，都逐字来自本机的真实运行。

## 咱们的实验环境

照本站的惯例，咱们把环境摆在最前面。后面的每一块汇编、每一段 gdb 会话，都出自同样的配置：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

这套 GCC 的配置里有两项默认值会影响咱们的观察，您自己复跑要是和文中对不上，多半就是它们俩闹的。一项是默认开了 PIE（位置无关可执行文件，加载的基址由系统当场决定），它会让 gdb 里拿固定文件地址下断点的尝试报出 Cannot access memory，咱们到栈参数的部分会专门处理它。另一项是默认开了栈保护，带数组一类局部变量的函数会多出金丝雀指令，就是第 1 篇交代过的 `%fs:40` 相关那几条，它们跟传参是不相干的，您自己复跑看到别慌，咱们在后文个别小节的编译命令里加了 `-fno-stack-protector` 把它避开。

还有几条实验条件是全程统一的。被测函数名前都挂了 `__attribute__((noinline))`，目的是堵住内联的口子，保证咱们能看到真实的 call。每个程序还设了一个 volatile 的 sink 变量（sink 在测试程序里专指用来接住结果的变量），咱们把返回值一律写进去，免得整个调用被当成死代码删掉了。汇编命令统一用的是 `g++ -O0 -S` 再加 `-fno-asynchronous-unwind-tables` 降噪，不过 C++ 的输出里 `.cfi_*` 伪指令仍然会在。咱们贴文时手工删掉它们和 `.globl`、`.size`、`.file`、`.ident` 一类排版行，个别长例子还删去了 sink 的 `.bss` 定义段，指令部分咱们一条都没动，后文就不再重复说明了。

## 两个参数，两个寄存器

咱们从最小的例子起步，还是上一个主题开篇的 add，咱们只把实参换成 11 和 22：

```cpp
// c1_two_int.cpp
__attribute__((noinline)) int add(int a, int b) { return a + b; }

volatile int sink = 0;

int main() {
    int r = add(11, 22);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c1_two_int.cpp -o c1_two_int.s
```

编译出来的汇编如下。本篇沿用上一篇的 AT&T 语法（gcc 的默认），它的源操作数在左、目的在右，系列第 1 篇看过的 Intel 语法方向正好相反，您来回对照着读的时候，可得留神别把方向给读反了：

```asm
_Z3addii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	-4(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$22, %esi
	movl	$11, %edi
	call	_Z3addii
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

main 在 `call` 之前干的就是装货：`movl $11, %edi` 把第 1 个实参 a=11 写进 edi，也就是 rdi 的低 32 位。`movl $22, %esi` 写进的是第 2 个实参 b=22，落点就是 rsi 的低 32 位。int 的宽度是 32 位，所以装货用的是 64 位寄存器的 32 位别名。等跳进了 add 之后，头两条 mov 把两个寄存器倒进它自己的栈帧，`movl %edi, -4(%rbp)` 存的是 a，`movl %esi, -8(%rbp)` 存的是 b。把寄存器里的值写进栈槽的动作，工程师的口语里叫 spill（洒出来存到栈上），您在别的资料里见到这个词，指的就是这场搬运。随后的求和从两个栈槽把数读回来，`addl %edx, %eax` 算出的结果落在 eax，所以 ret 一回去，main 的 `movl %eax, -4(%rbp)` 就把返回值接走了。

咱们在这里停一下，您细看 main 里装货的两条 mov 的排布，能看出一件怪事：写在前面的 `movl $22, %esi` 装的是第 2 个参数，写在后面的 `movl $11, %edi` 反而装的是第 1 个。它不是排版的错误。GCC 在 -O0 下装载实参的习惯是从最后一个参数往回倒着填，两个参数的例子它就把 esi 排在了 edi 前面，下一节六个参数的例子里这个习惯会更显眼。咱们看指令把谁写在前面、把谁写在后面，这跟参数的次序无关，跟参数的次序挂钩的是寄存器的分配关系，咱们读这类汇编，认的只能是分配关系。ABI 锁定的只是谁进哪个寄存器，至于编译器按什么次序写这些 mov 的事，它是不管的。您要是把书写顺序当成参数顺序，换了一个编译器或者优化档位之后，读出来的对应关系就会全错位。

spill 这一步咱们再补一句：它属于 -O0 的调试友好行为而非规范的强制要求，编译器的习惯与 ABI 的约定各管各的，两者的边界咱们到下一篇讲栈参数的地方还会再遇到。两个参数的交接到这里就走完了：货从 main 的 edi 和 esi 出发，回程走的是同一个 eax。两个名额显然是不够用的，整数寄存器的名额到底有几个，下一节咱们数给您看。

## 六个参数：整数寄存器的分配表

咱们把参数加到六个，看看整数寄存器的家底有多厚：

```cpp
// c2_six_int.cpp
__attribute__((noinline)) int sum6(int a, int b, int c, int d, int e, int f) {
    return a + b + c + d + e + f;
}

volatile int sink = 0;

int main() {
    int r = sum6(1, 2, 3, 4, 5, 6);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c2_six_int.cpp -o c2_six_int.s
```

被调方与 main 的指令如下，sum6 中间那串反复的读回累加咱们省了，读法与上一个主题 three 的求和一段完全一样：

```asm
_Z4sum6iiiiii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	%edx, -12(%rbp)
	movl	%ecx, -16(%rbp)
	movl	%r8d, -20(%rbp)
	movl	%r9d, -24(%rbp)
	...（六次从栈槽读回并累加，略）
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum6iiiiii
	movl	%eax, -4(%rbp)
	...（写回 sink，略）
	leave
	ret
```

分配关系现在一目了然，咱们把它整理成一张表：

| 参数 | 实参值 | 64 位寄存器 | 32 位写法 |
|---|---|---|---|
| 第 1 个 a | 1 | rdi | edi |
| 第 2 个 b | 2 | rsi | esi |
| 第 3 个 c | 3 | rdx | edx |
| 第 4 个 d | 4 | rcx | ecx |
| 第 5 个 e | 5 | r8 | r8d |
| 第 6 个 f | 6 | r9 | r9d |

命名上有个地方值得您多看：r8 和 r9 是 x86-64 才新增的寄存器，没有 edi 那样的历史包袱，所以 32 位写法直接叫 r8d、r9d，末尾的 d 就是 double-word（双字，32 位）的意思。再看 main 的装货顺序，这次排的是 r9d、r8d、ecx、edx、esi、edi，从第 6 个参数一路倒着填回了第 1 个，上一节说的代码生成习惯在这里全露了脸。但您对照表格看，分配关系与两个参数那一场的规则严丝合缝：越靠前的参数，占的寄存器越靠前。

只看汇编咱们还不放心，所以请 gdb 来验货。命令里的 `break _Z4sum6iiiiii` 是源码级断点，gdb 会自动越过函数开头的序言（prologue，上一个主题讲过的搭栈帧那几条），从而把程序停在第一条用户指令上，这时候寄存器里的参数还原封未动：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break _Z4sum6iiiiii' -ex 'run' -ex 'info registers rdi rsi rdx rcx r8 r9' -ex 'p (int)$rdi' -ex 'p (int)$rsi' -ex 'p (int)$rdx' -ex 'p (int)$rcx' -ex 'p (int)$r8' -ex 'p (int)$r9' ./c2_six_int
Breakpoint 1 at 0x1131: file c2_six_int.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, sum6 (a=1, b=2, c=3, d=4, e=5, f=6) at c2_six_int.cpp:3
3	    return a + b + c + d + e + f;
rdi            0x1                 1
rsi            0x2                 2
rdx            0x3                 3
rcx            0x4                 4
r8             0x5                 5
r9             0x6                 6
$1 = 1
$2 = 2
$3 = 3
$4 = 4
$5 = 5
$6 = 6
```

六个寄存器报出的是 1 到 6，与实参一一对上了，gdb 顺带用自己的调试信息念出了 `sum6 (a=1, b=2, ...)`，咱们等于拿到了两路对得上的证据。

gdb 的证据之外还有第三路，您亲手走出来的。下面的执行器加载的就是 add(11, 22) 与 sum6(1..6) 的现场，单步到 `call sum6` 前停一下，抬头看寄存器面板：edi、esi、edx、ecx、r8d、r9d 正好装着 1 到 6，装载的顺序是反着来的，第六个参数先进 r9d。符号名去掉了修饰，sum6 不叫 _Z4sum6iiiiii，指令本身与上面一一对应。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/reg_args_7.json" />

所以数到第 6 个参数，整数寄存器的名额正好用光，那么第 7 个参数来的时候会发生什么？答案就在下一篇。
