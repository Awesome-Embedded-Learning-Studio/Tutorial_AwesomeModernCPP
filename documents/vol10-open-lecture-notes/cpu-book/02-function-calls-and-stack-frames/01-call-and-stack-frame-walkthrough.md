---
title: "一次函数调用，CPU 眼里发生了什么"
chapter: 2
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard:
  - 11
  - 17
description: "反汇编走读一次最小的函数调用，从 call 压返回地址、gdb 验出栈顶真值，到栈帧的搭与收"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 一次函数调用，CPU 眼里发生了什么

[上一篇](../01-prefix-vs-postfix-increment/01-int-instructions.md)咱们拿 i++ 与 ++i 练过手，那个主题里咱们把同一份源码分别用 -O0 和 -O2 编译，出来的指令序列长得完全不同。不过那时的咱们一直在函数的外围打转，`call` 一出现就当它是黑盒跳过去了。这一篇咱们把这个盒子打开，跟着一次完整的函数调用从跳进去到跳回来，看 CPU 每一步都做了什么：它怎么跳进被调函数，又靠什么跳回到原来的地方，而局部变量住在哪里，函数返回的时候这一切又怎么被收拾干净。

这一篇的例子与实验都是本站原创的，您在文中看到的汇编与调试器的输出，都逐字来自本机的真实运行。

咱们后面的每一份汇编、每一段 gdb 会话，都是在这样的环境里跑出来的。环境里的 gdb 是一个调试器，咱们用它把程序停在指定的指令上，看寄存器和内存里的值，后面那些带寄存器读数的会话全是它的输出。咱们把版本信息贴在下面：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

有一处降噪要交代：`.cfi_*` 是 unwind 的调试伪指令（unwind 指把调用链一层层展开还原的机制）、不生成机器码，只是喂给调试器的信息。本篇贴汇编时把这些行删掉了，咱们看到的指令流一个字都没改。

## 从一个最小的调用看起

咱们从最小的程序开始：`main` 调一次 `add`、把 3 和 4 交给它相加。整个程序不过七行，但它已经把函数调用的基本要素都带上了：参数、跳转、局部变量、返回值。

```cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int s = add(3, 4);
    return s;
}
```

咱们用 -O0 把它编译成汇编。-O0 的意思是编译器几乎不做优化，它把 C++ 的结构老老实实翻译成指令，最适合咱们看清调用本身长什么样。命令里那个 `-fno-asynchronous-unwind-tables` 是为了让汇编干净一点，不过 GCC 16 为了 C++ 异常展开仍然会发出 `.cfi_*` 伪指令，所以下面贴出来的汇编还经过了一道手工删除：

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b1_add.cpp -o b1_add.s
```

```asm
	.file	"b1_add.cpp"
	.text
	.globl	_Z3addii
	.type	_Z3addii, @function
_Z3addii:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	-4(%rbp), %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret
.LFE0:
	.size	_Z3addii, .-_Z3addii
	.globl	main
	.type	main, @function
main:
.LFB1:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$4, %esi
	movl	$3, %edi
	call	_Z3addii
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	leave
	ret
.LFE1:
	.size	main, .-main
	.ident	"GCC: (GNU) 16.2.1 20260810"
	.section	.note.GNU-stack,"",@progbits
```

输出里的 `_Z3addii` 是 `add(int, int)` 经过 C++ 名字修饰之后的符号名：编译器把函数名和参数类型编码进符号里，链接的时候才不会把不同签名的同名函数混在一起。`main` 咱们也看到了，它的符号就叫 `main`，是不参与修饰的。

咱们在汇编里认两条主角指令就好：main 里的 `call _Z3addii`，add 末尾的 `ret`。call 干的事情有两件：把返回地址压进栈，然后跳到 add 的开头。ret 是反过来的动作：从栈顶弹出返回地址，然后跳回到那个地址。听着像两句干巴巴的定义，咱们接下来就用 gdb 看栈顶，把返回地址真的在那里这件事验出来。动手之前还得交代汇编的读法，不然您第一次看容易读反：这是 AT&T 语法，也是本篇通篇的默认，上一篇的汇编用的是 Intel 语法，操作数两边的位置正好相反，您对照着读的时候注意区分。`movq %rsp, %rbp` 里源操作数在左、目的在右，百分号开头的是寄存器，`$` 开头的是立即数。括号写法 `-4(%rbp)` 表示内存地址，意思是 rbp 的值减 4 那个位置上的内存。

纸上读汇编终究隔一层，咱们把开头这段调用的机器码原样搬进了页面。下面的执行器加载的就是 add(3, 4) 的完整现场：从 main 的第一条指令开始，可以单步，也可以自动执行，每一步之后的寄存器和栈都摆在眼前。它跟上面的 GCC 输出只有一处差别，执行器里的函数符号就叫 add，不带 _Z3addii 的名字修饰，方便您对上号。您可以先随便把玩，重点看 call 前后栈顶的动作。栈是怎么回事，下一节咱们正式请它登场。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/b1_add.json" />

## call 压下去的到底是什么

栈是一块很普通的内存，它的用法很特别：只允许从一头进出，越晚进来的数据越早出去。把数据放进栈里的动作叫压栈，x86-64 的压栈会一次腾出 8 字节、让栈顶往低地址挪。把数据取走的动作叫弹栈，栈顶跟着回到高的那边。记栈顶位置的是 rsp 寄存器，您把 rsp 理解成一根指着栈顶的指针就行。

call 说自己会把返回地址压进栈，落到机器动作上是三小步：算出 call 自己下一条指令的地址、让 rsp 往低地址挪 8 字节、把这个地址写进 rsp 指着的内存，然后跳去目标函数的入口。返回地址是 CPU 留下的回程路标：函数干完了活，CPU 能靠它找到回去的路。咱们拿开头汇编里的 main 来看，`call _Z3addii` 后面紧跟的 `movl %eax, -4(%rbp)`，它的地址正是这一次被压进栈的返回地址。

空口说返回地址躺在栈顶，咱们不如直接看。做法是这样：用 gdb 把断点下在 add 的第一条机器码指令上（`break *add`，星号写法表示下在函数入口的指令地址，而不是某个源码行），此刻 add 的一条指令都还没执行，如果咱们的说法成立，$rsp 指着的内存里，就应该躺着 main 里 call 的下一条指令地址。咱们编译时带上了 -g，跑起来的会话如下（debuginfod 的提示横幅已删，--- [1] --- 一类的分隔行是咱们后加的注解，其余都是逐字的）：

```text
$ g++ -O0 -g b1_add.cpp -o b1_add
$ gdb --batch -q b1_add -ex 'set debuginfod enabled off' -ex 'break *add' -ex 'run' ...
```

```text
Breakpoint 1 at 0x1119: file b1_add.cpp, line 1.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, add (a=32767, b=-141919601) at b1_add.cpp:1
1	int add(int a, int b) {
--- [1] stopped BEFORE any instruction of add ---
rsp            0x7fffffffd0b8      0x7fffffffd0b8
rip            0x555555555119      0x555555555119 <add(int, int)>
0x7fffffffd0b8:	0x0000555555555144
--- [2] two single steps: push rbp ; mov rbp,rsp ---
0x00005555555511a	1	int add(int, int) {
0x00005555555511d	1	int add(int, int) {
rsp            0x7fffffffd0b0      0x7fffffffd0b0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
--- [3] info frame: where gdb thinks saved return address lives ---
Stack level 0, frame at 0x7fffffffd0c0:
 rip = 0x55555555511d in add (b1_add.cpp:1); saved rip = 0x555555555144
 called by frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0b0, args: a=32767, b=-141919601
 Locals at 0x7fffffffd0b0, Previous frame's sp is 0x7fffffffd0c0
 Saved registers:
  rbp at 0x7fffffffd0b0, rip at 0x7fffffffd0b8
--- [4] read the return address slot at rbp+8 ---
0x7fffffffd0b8:	0x0000555555555144
```

咱们看会话里 [1] 处，程序停在 add 的入口，`rsp` 的值是 0x7fffffffd0b8，`x/gx $rsp` 从这个地址读出了 0x0000555555555144。这个数是不是返回地址，咱们拿同一份二进制里 main 的反汇编来对：

```text
Dump of assembler code for function main():
   0x000055555555512d <+0>:	push   %rbp
   0x000055555555512e <+1>:	mov    %rsp,%rbp
   0x0000555555555131 <+4>:	sub    $0x10,%rsp
   0x0000555555555135 <+8>:	mov    $0x4,%esi
   0x000055555555513a <+13>:	mov    $0x3,%edi
   0x000055555555513f <+18>:	call   0x555555555119 <_Z3addii>
   0x0000555555555144 <+23>:	mov    %eax,-0x4(%rbp)
   0x0000555555555147 <+26>:	mov    -0x4(%rbp),%eax
   0x000055555555514a <+29>:	leave
   0x000055555555514b <+30>:	ret
End of assembler dump.
```

main 里的 `call` 位于 0x55555555513f，它的下一条指令 `mov %eax,-0x4(%rbp)` 位于 0x5555555555144，咱们从栈顶读出的数与后者一字不差。咱们接着看 [3]，gdb 对栈帧的解析也说 `saved rip = 0x555555555144`，还标明了返回地址住在 0x7fffffffd0b8。[4] 的读取发生在 prologue 之后（prologue 指函数开头搭栈的那几条指令，咱们到栈帧一节细讲），`rbp+8` 处读出的值还是 0x0000555555555144。三方对上了：栈顶的值、call 下一条指令的地址、gdb 眼里的 saved rip，说的都是同一个地址。

[2] 处咱们单步走了两条指令（`push rbp` 与 `mov rbp,rsp`），rsp 从 0x7fffffffd0b8 变成了 0x7fffffffd0b0，正好往低地址挪了 8 字节，rbp 则被设成了新的栈顶。它们是干什么的，正是下一节的内容。

这个会话里还藏着个怪东西：入口处 gdb 显示的参数是 `add (a=32767, b=-141919601)`、一对垃圾值。实验其实是成功的，咱们看到的反而是真相的样子：此刻 3 和 4 还安稳地待在 edi、esi 两个寄存器里，而栈上压根还没有它们的影子，gdb 按照 rbp 偏移的调试信息去读内存，读到的自然不是 3 和 4。对照组咱们也有：断点改下在源码行上（落在 prologue 之后，这里的分隔行同样是注解后加的，横幅也删了），会话是这样的：

```text
Breakpoint 1, add (a=3, b=4) at b1_add.cpp:2
2	    return a + b;
--- at entry of add ---
rdi            0x3                 3
rsi            0x4                 4
rsp            0x7fffffffd0b0      0x7fffffffd0b0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
```

咱们把断点挪到 `return a + b;` 那行源码上，rdi、rsi 就分别报出了 3 和 4。两个会话合起来印证了一件事：参数本来走的是寄存器，落栈是函数自己的事。开头那个程序里 add 的头两行 `movl %edi, -4(%rbp)`、`movl %esi, -8(%rbp)` 干的就是这个搬运，参数怎么装进寄存器的完整约定，咱们放到系列的下一个专题细讲。

## 栈帧：函数占住的那段栈

返回地址认清楚了，咱们回头看函数自己。被调函数一进来的时候，栈顶上只躺着 call 压下来的返回地址，再往高处就是调用者的东西了。而函数要干活，就得在栈上给自己占一段空间：要住的局部变量、要保的寄存器、后面 call 别人时的返回地址，都在这一段里各有各的位置。这一段空间加上管理它的约定，合起来的名字叫栈帧（stack frame），您把它想成函数在栈上的一段私有工作区就行。

GCC 在 -O0 下用 rbp 给每个函数当基准点：rbp 指向的位置存着上一个函数的 rbp 旧值，rbp+8 放的是返回地址，rbp 减去若干偏移就是本帧的局部变量。这套布局咱们在上一节已经见过一半了：gdb 报的 saved rip 住在 0x7fffffffd0b8，那里正是 prologue 执行后的 rbp+8。每个函数都会这样用 rbp、把它当成自己的基准，所以被调函数进门的头一件事，就是把调用者的 rbp 存好、回去前再恢复，不然调用者的基准点就丢了。

咱们把函数开头搭栈帧的几条指令叫 prologue（序幕），把收尾拆栈帧的叫 epilogue（尾声）。讲它们最好的素材是一个带局部变量的函数，咱们写一个 three、里面放 3 个 int 和 1 次求和，求和的结果再交给 consume 加一后返回。consume 与 main 的汇编和开头程序里 add、main 的样子同构，咱们只看 three 本身：

```cpp
int consume(int v) {
    return v + 1;
}

int three() {
    int x = 1;
    int y = 2;
    int z = 3;
    int s = x + y + z;
    return consume(s);
}

int main() {
    return three();
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S b3b_locals_call.cpp -o b3b_locals_call.s
```

咱们用 -O0 编译后，`_Z5threev` 的汇编如下（已删 .cfi 伪指令，main 与 consume 两段和开头程序里的同构，这里一并删掉了）：

```asm
_Z5threev:
.LFB1:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movl	$1, -16(%rbp)
	movl	$2, -12(%rbp)
	movl	$3, -8(%rbp)
	movl	-16(%rbp), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %eax
	movl	%eax, %edi
	call	_Z7consumei
	leave
	ret
.LFE1:
	.size	_Z5threev, .-_Z5threev
```

## 逐行走读：three 的每一步栈变化

走读从 call 刚落地、three 的第一条指令还没执行时开始。此刻的栈和 call 一节 gdb 里看到的是同一个样子：rsp 指着的是返回地址，再往高处是 main 的帧。咱们用相对位置叙述每一步，不去编具体的地址：把返回地址所在的槽叫第 0 格，往下的格子依次是第 1 格、第 2 格，一格的大小是 8 字节。

`pushq %rbp` 把 rbp 的旧值压进栈：rsp 下移一格，main 的 rbp 旧值写进新的栈顶格（咱们叫它第 1 格）。压栈的动作本身就把 rsp 挪低了 8 字节，同时调用者的基准点也被存进了本帧。从此 main 想找回自己的 rbp，来第 1 格取就行了。

`movq %rsp, %rbp` 把栈顶的位置记成新基准：rbp 从此指向第 1 格（存着旧 rbp 的格子），本帧内的栈上寻址都拿它当参照。咱们前面看到的 -4(%rbp)、-16(%rbp) 这类写法，参照的基准就是它定下来的。

`subq $16, %rsp` 给局部变量腾地方：rsp 从第 1 格再往下走 16 字节，让出四个 4 字节的槽。为什么 three 必须有这一步？理由是这样的：three 的内部还要 call consume，call 会把返回地址写到 rsp 再往下面的 8 字节里，如果 rsp 没有挪低、写下的位置正好落在 three 的变量区里，变量 s 就要被冲掉了。会调用别人的函数才有这第三步，咱们数一数，3 个变量加 1 个求和结果正好填满了那 16 字节。

咱们接着看三条 `movl` 把初值写进变量的槽，四个槽的分配是这样的：

| 变量 | 位置 | 初值 |
|---|---|---|
| x | -16(%rbp) | 1 |
| y | -12(%rbp) | 2 |
| z | -8(%rbp) | 3 |
| s（求和结果） | -4(%rbp) | 6 |

咱们看 GCC 在 -O0 下的排布就是这么老实：每个 int 占一个 4 字节的槽，槽与槽之间是不留空隙的，16 字节就这样用完了。求和结果 s 住的 -4(%rbp) 紧挨着 rbp 的脚下。

咱们在这里给栈帧布局留一个图位，图待补：内容为 call 之后从栈顶到局部变量区的布局，也就是返回地址、保存的 rbp、x、y、z、s 各自的相对位置，后续由本站的绘图管线补上。

求和的六条指令咱们看着眼熟，干的都是搬运和相加：

```asm
	movl	-16(%rbp), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movl	-8(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -4(%rbp)
```

咱们看 x 和 y 进了 edx 与 eax，`addl %eax, %edx` 把 x+y 的结果留在 edx，z 随后也进了 eax，第二回相加把结果 6 留在了 eax，最后一条 movl 把它写进 s 的槽。-O0 是不做寄存器优化的，每个变量的读写都要经过内存，指令数多但是每一步都直白。

接下来是本篇的第二次 call。开头的 `movl %eax, %edi` 把参数 s 装进了 edi，`call _Z7consumei` 压返回地址、跳进 consume 的入口。咱们等 consume 算完它的 v+1、把结果放进 eax 交回来，three 的 `leave` 和 `ret` 接力收尾。leave 是一条顶两条的指令：`mov %rbp, %rsp` 加 `pop %rbp`。头一半把 rsp 一步拉回 rbp 的位置，局部变量区不论当初 sub 了多少都一并丢掉。后一半的动作是弹出旧 rbp 交还 main。这一下过后栈顶上摆着的正好又是返回地址，`ret` 随即弹出返回地址跳了回去，three 的栈帧就没了。

consume 和开头的 add 一样是叶子函数，这件事值得记下：它收尾用的是裸的 pop rbp，下一篇咱们专门说它。
