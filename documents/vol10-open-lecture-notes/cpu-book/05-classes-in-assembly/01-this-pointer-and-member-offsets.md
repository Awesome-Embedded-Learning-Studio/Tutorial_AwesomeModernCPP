---
title: "你的类在汇编里长什么样：this 指针与成员偏移"
chapter: 5
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "成员函数与收对象指针的自由函数编出的汇编逐指令相同，this 坐在 rdi 上，对象里只有成员偏移，内存里没有类型的标记"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 你的类在汇编里长什么样：this 指针与成员偏移

[上一篇](../04-stack-overflow-why-no-crash/01-stack-frame-neighbors.md)咱们在栈帧上干了一回危险的事，负下标写出了数组的界，程序居然安静地跑完了。这一篇咱们回到安全地带，换一批更体面的素材：您天天在写的类。`p.translate(3, 4)` 这样的一句点号调用，`Point p{1, 2}` 这样的一句定义，`p->who()` 这样的一次多态，在 CPU 的眼里各自是什么样子？03 的结尾咱们留过一句话头：调用成员函数的时候，对象自己的地址走的正是 rdi 那个位置，也就是咱们说的 this 指针，当时还约好了它一露头咱们就能认出来。04 的结尾又催了一遍。现在咱们来兑现，而且兑现的不止这一件：this 的下落、构造与析构的插入点、虚函数背后的函数指针表，咱们本篇把它们全部放进汇编里验一遍。等咱们把这几样都看完，C++ 的对象在机器层面就没有魔法了，剩下的只有内存里的一串字节和几条老老实实的 call。

这一篇的例子与实验都是本站原创的，您在文中看到的每一块汇编与每一段调试器会话，都逐字来自本机的真实运行。

咱们照惯例把环境摆在前面。这一篇除了 g++ 和 gdb，咱们还请 objdump 出场，它干的活是从二进制里翻符号表：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ gdb --version | sed -n 1p
GNU gdb (GDB) 17.2

$ objdump --version | sed -n 1p
GNU objdump (GNU Binutils) 2.47
```

咱们用的机器是 x86-64，调用约定沿用 03 里咱们认过的 System V AMD64，前三个整型参数依次走的是 rdi、rsi、rdx。实验条件用的也是老一套。被观察的函数一律用 -O0 编译，需要的地方挂 `__attribute__((noinline))` 堵住内联，返回值和对象状态的消费交给 volatile 变量，免得在没有消费方的时候，被优化器把整段指令删了。汇编的语法是 AT&T，从 02 起就是系列的默认。贴文的做法在这里一次说清。`.cfi_*` 伪指令您在 02 认过了，它是喂给调试器的栈展开元数据、不生成机器码，本篇把它们原样保留了下来。汇编行里 `#` 起头的注释都是咱们的解读，指令咱们逐字照贴。个别长函数做了节选的地方会注明，偶尔略去的是 `.globl`、`.size` 一类的排版行，改动的指令一条都没有，后文就不再重复说明了。

## this 指针就坐在 rdi 的位置上

咱们从一个最小的类开始。`Point` 的身体里有两个 int 和一个成员函数 `translate`，旁边咱们再写一个自由函数 `point_translate`，它长得很刻意：参数表把对象指针显式写成了第一个参数，函数体逐句地对应 `translate`。两个函数干的是同一件事，咱们接下来要问的就是，编译器是不是把它们编译成了同一个东西：

```cpp
// e1.cc —— 成员函数 vs 自由函数，this 就是第一个参数
#include <cstdio>

struct Point {
    int x;
    int y;

    void translate(int dx, int dy) {
        x += dx;
        y += dy;
    }
};

// 与 translate 逐参数对应的自由函数：对象地址显式做第一个参数
void point_translate(Point* p, int dx, int dy) {
    p->x += dx;
    p->y += dy;
}

int main() {
    Point p{1, 2};

    p.translate(3, 4);
    point_translate(&p, 5, 6);

    volatile int sink = p.x + p.y;  // 消费对象状态，防优化
    (void)sink;

    std::printf("p.x=%d p.y=%d\n", p.x, p.y);
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e1.cc -o e1.s
$ g++ -O0 -fno-asynchronous-unwind-tables e1.cc -o e1 && ./e1
p.x=9 p.y=12
exit=0
```

输出咱们来对一遍：1 加 3 加 5 等于 9，2 加 4 加 6 得到的是 12，两个函数都起了作用。然后就该看汇编了。`Point::translate` 编出来的符号是 `_ZN5Point9translateEii`，名字修饰的套路 02 讲过，这里多出来的 `N5Point...E` 是把类名也编了进去。咱们把它的函数体贴在下面：

```asm
_ZN5Point9translateEii:
.LFB2:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)       # rdi 里的 this 落栈
	movl	%esi, -12(%rbp)      # rsi 里的 dx 落栈
	movl	%edx, -16(%rbp)      # rdx 里的 dy 落栈
	movq	-8(%rbp), %rax
	movl	(%rax), %edx         # x 在 this+0
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, (%rax)         # x += dx 写回 this+0
	movq	-8(%rbp), %rax
	movl	4(%rax), %edx        # y 在 this+4
	movl	-16(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, 4(%rax)        # y += dy 写回 this+4
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

开头三条落栈指令把答案直接摆上了台面。`movq %rdi, -8(%rbp)` 把 rdi 的值存进了栈槽，`movl %esi, -12(%rbp)` 和 `movl %edx, -16(%rbp)` 跟着把 rsi 与 rdx 存了进来。-O0 的习惯是把每个入参都从寄存器搬到栈上，03 里咱们就是这么认参数的。现在三个参数的来源清清楚楚：this 住的是 rdi，dx 住的是 rsi，dy 住的是 rdx。成员函数没有任何隐秘的通道去摸对象，它收到的就是 rdi 里那个地址，跟一个收 `Point*` 的普通函数拿到的东西没有区别。

咱们看调用点那边，也是同样的秩序。`main` 里两次调用的指令如下（printf 的参数准备和栈保护省略了，完整原文在 `e1.s` 的 76 到 120 行）：

```asm
	leaq	-16(%rbp), %rax      # &p 进 %rax，p 在 rbp-16
	movl	$4, %edx             # dy = 4   → rdx
	movl	$3, %esi             # dx = 3   → rsi
	movq	%rax, %rdi           # this=&p  → rdi
	call	_ZN5Point9translateEii
	leaq	-16(%rbp), %rax
	movl	$6, %edx             # dy = 6   → rdx
	movl	$5, %esi             # dx = 5   → rsi
	movq	%rax, %rdi           # this=&p  → rdi
	call	_Z15point_translateP5Pointii
```

您看这两段装载的形制：`leaq` 把 p 的地址算出来送进 rdi，实参 3 和 4 进的是 esi 与 edx，然后就 call 了。源码上一个写的是 `p.translate(3, 4)`，另一个写的是 `point_translate(&p, 5, 6)`，落进汇编里的形制却是一模一样的。03 埋的话头到这里算是兑现了一半：this 并没有什么关键字魔法，它就是排在参数表第一位的那个指针，占的正是您已经认识的 rdi。

下面是更有分量的另一半。咱们把 `point_translate` 的函数体也贴出来，您可以拿眼睛粗粗地过一遍：

```asm
_Z15point_translateP5Pointii:
.LFB3:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	movq	%rdi, -8(%rbp)
	movl	%esi, -12(%rbp)
	movl	%edx, -16(%rbp)
	movq	-8(%rbp), %rax
	movl	(%rax), %edx
	movl	-12(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, (%rax)
	movq	-8(%rbp), %rax
	movl	4(%rax), %edx
	movl	-16(%rbp), %eax
	addl	%eax, %edx
	movq	-8(%rbp), %rax
	movl	%edx, 4(%rax)
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

咱们肉眼看难免走眼，所以让 diff 来判。咱们提取两段函数体做机械比对，原始输出是这样的：

```text
--- e1.member.asm	2026-10-12 12:04:18.886467048 +0800
+++ e1.free.asm	2026-10-12 12:04:18.888154193 +0800
@@ -1,5 +1,5 @@
-_ZN5Point9translateEii:
-.LFB2:
+_Z15point_translateP5Pointii:
+.LFB3:
 	.cfi_startproc
 	pushq	%rbp
 	.cfi_def_cfa_offset 16
```

真正的差异只有 2 行：一个函数符号名，加一个 GCC 内部标号 `.LFB` 的编号，diff 把它们连同三行没变的上下文圈在了 5 行的窗口里。再往下还有 23 行是逐字相同的。咱们把两处名字也抹平（符号统一替换成 FUNC，`.LFB` 编号抹掉）之后咱们再做一次 diff：

```text
$ diff -u e1.member.norm e1.free.norm
(无输出)
```

diff 的输出一行都没有，退出码是干净的 0。这就是本篇最省话的一个证据：“成员函数就是拿了 this 的普通函数”在咱们这版编译器上已经退出了修辞的范畴，一条 diff 命令就能机械地验证。您想自己验的话，把上面的两段汇编各自存进文件、替换掉符号再 diff，得到的也会是一片安静。

## 成员变量：对象里只有偏移，没有类型

咱们回到函数体中间那几条访存指令。`movl (%rax), %edx` 读的是 `(%rax)`，此刻 rax 里装的是 this，所以这句话读的是 this+0 处的 4 字节，也就是 x 所在的位置。`movl 4(%rax), %edx` 读的是 this+4，那就是 y 了。成员变量在机器层面就是从对象地址起算的固定偏移，`x` 住的是 +0，`y` 住的是 +4，两个 int 是紧挨着的，布局跟 C 的结构体一个样。

不过这件事翻过来还有一层意思，值得您停一下。`p` 这块内存里拢共只有 8 个字节的容量，存的是两个整数，您在这里找不到“我是一个 Point”。您在源码里写的类型名、成员名、访问控制，编译完之后就统统不在对象里了，它们只活在编译期的世界里，编译器拿来算偏移、查权限的都是这些信息。运行时的 CPU 拿到的只是一个地址和一堆取偏移的指令，这块内存是谁？没有谁来问它。这也解释了为什么把指针强转成错误类型再访问，机器层面倒是畅通无阻，错的只是偏移对不上了。04 里越界写砸中邻居的逻辑，跟这里是同一套：内存里不带类型的任何标记，类型的世界到编译期为止。

还有一句语法版本的事要交代。系列首篇用的是 Intel 语法，本篇默认的是 AT&T，您要是想拿 Intel 版对照，您加 `-masm=intel` 编一次就行。同一个调用点的 Intel 版长这样：

```asm
	lea	rax, -16[rbp]
	mov	edx, 4
	mov	esi, 3
	mov	rdi, rax
	call	"_ZN5Point9translateEii"
```

用的指令还是同一批，只是操作数两边掉了位置。有一个版本差异得提醒您：GCC 16 在 Intel 模式下会给符号名加上引号，`call "..."` 的写法就是这么来的，旧版的资料里可没有这对引号，不是您编错了。

this 和成员偏移到这里都认下了。编译器替您插的构造与析构调用长什么样，咱们下一篇接着看。
