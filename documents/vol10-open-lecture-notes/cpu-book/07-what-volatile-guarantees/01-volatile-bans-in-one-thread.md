---
title: "volatile 在单线程里禁掉的三件事：合并、消除与重排"
chapter: 7
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 20]
description: "从十次读被合并成一次装载的循环看起，再用四组小函数把没人消费的读、马上被盖的写、互相换位的读写挨个对到汇编层，数清 volatile 在单线程里禁掉的合并、消除与重排"
reading_time_minutes: 13
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# volatile 在单线程里禁掉的三件事：合并、消除与重排

> 咱们在这里拿 `volatile` 当防优化的锚点。它要求对 sink 的每次读写都必须真实发生，读写的顺序也得保持原样。

这是 [01 第一组实验](../01-prefix-vs-postfix-increment/01-int-instructions.md)里的一句话。当时咱们急着对比 i++ 与 ++i 的指令差别，volatile 只被当了一回防优化的锚点，一个关键字凭什么就能命令编译器不许合并、不许删、不许挪？笔者当时一句没讲，这一篇咱们专门讲它。

咱们把判断提前交到您手里：volatile 是写给编译器的禁令，说话的对象是编译器，对 CPU 那头它是一个字都没提的。您在前六篇的汇编里找不到 volatile 的任何痕迹（因为编译器根本不会把它编成指令），它改变的恰恰是那些本该出现却没有出现的指令。而它最广为流传的用法，也就是两个线程共用一个 volatile 变量当通信通道的写法，恰好是它完全没有承诺的领域。管什么、不管什么，咱们这个主题里全拿实验对出来。

这一篇的例子与实验全部是咱们从零写的，文中您看到的每一块汇编与每一段程序输出，都逐字来自本机的真实运行。

系列的主链在上一篇走完，指针值从虚拟地址一路接到了内存条上的真实位置。这一篇是主链之外的专场：主角不再是某类代码落到了什么指令上，而是一个关键字拦下了哪些优化。看的办法还是咱们在用的那一套：摆源码、贴汇编、跑程序，差别在于这个主题的证据有一半是消失的指令，另一半是程序跑出来的竞争现场。

## 咱们的实验环境

咱们照惯例把环境摆在前面：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

这一篇的实验都在 WSL2 里跑，本机的逻辑核有 16 个，后面两个线程抢一个变量的实验用的就是它。取汇编的命令咱们统一用 `g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables`，输出的语法统一是 AT&T，贴的时候照 03 立下的惯例省去 `.ident`、`.section` 一类文件尾部的排版行，指令的部分一条未动，后文也就不再重复说明了。函数头尾的 `.cfi_startproc` 与 `.cfi_endproc` 咱们照 05 的做法原样保留，它是喂给调试器的栈展开元数据、不生成机器码。带线程的程序按发布口径编译 `g++ -O2 -std=c++20 -pthread`，反汇编咱们用 `objdump -d --no-show-raw-insn`，省掉了机器码字节，读起来干净多了。源文件名里的 v1、v4 一类是笔者做实验时的编号，您复现时换什么名字都行。

## 十次读合并成一次装载

咱们从最朴素的一件事看起：函数的循环里反复读同一个全局变量，-O2 会把重复的装载合并掉。装载这个词说的就是 load，把内存上的值读进寄存器。咱们要看的源码只有几行：

```cpp
// v1_loop_plain.cpp: 普通全局变量,循环读 10 次累加 —— 观察 -O2 是否合并重复装载
int g;  // 普通全局,本翻译单元内无写入

int sum10() {
    int s = 0;
    for (int i = 0; i != 10; ++i)
        s += g;
    return s;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v1_loop_plain.cpp -o v1_loop_plain.s
```

```asm
_Z5sum10v:
.LFB0:
	.cfi_startproc
	movl	g(%rip), %eax
	leal	(%rax,%rax,4), %eax
	addl	%eax, %eax
	ret
	.cfi_endproc
```

源码里 10 次 `s += g` 对应的是 10 次装载，汇编里只剩开头的一次 `movl g(%rip), %eax`。后面两条咱们分开认：`leal (%rax,%rax,4)` 算出 g 的 5 倍，也就是 rax 的 4 倍加它本身的 1 倍，`addl %eax, %eax` 又把值翻了一倍，合起来直接算出了 g * 10。循环整个消失了。编译器的推理咱们替它念一遍：循环体内没有任何可能写 g 的操作（没有调用、没有写入），10 次读读到的值全一样，合并成一次就成了，再用一次乘法顶替了整个循环。用便宜运算顶替贵运算的手法叫强度削减，它是 01 见过的常量折叠之外的又一门编译期手艺。

咱们把声明处的一个词换掉，普通 int 换成了 volatile int，其余的函数体一个字都没动：

```cpp
// v2_loop_volatile.cpp: 与 v1 相同的代码,变量改 volatile —— 对照:每次迭代都重新装载
volatile int g;

int sum10() {
    int s = 0;
    for (int i = 0; i != 10; ++i)
        s += g;
    return s;
}
```

```asm
_Z5sum10v:
.LFB0:
	.cfi_startproc
	movl	$10, %eax
	xorl	%edx, %edx
	.p2align 4
	.p2align 4
	.p2align 3
.L2:
	addl	g(%rip), %edx
	subl	$1, %eax
	jne	.L2
	movl	%edx, %eax
	ret
	.cfi_endproc
```

循环回来了。开场两条是准备工作：`movl $10, %eax` 把 10 装进了 eax 当倒计数，`xorl %edx, %edx` 把累加器 edx 清成了零。`.L2` 到 `jne` 的循环一共 10 轮，每轮的循环体是一条 `addl g(%rip), %edx`，每条 addl 干的都是同一件事，从内存里把 g 的值重新装一遍，10 次迭代就攒出了 10 次装载，一次也少不了。源码的差别只有声明处的一个词，汇编的差别却是循环的去留。这就是 volatile 禁下的第一件事：重复访问不许合并。谁需要这样的保证？有些对象的值会被咱们代码之外的东西改写（设备与信号处理器都属于这一类），而编译器看不见那些改写者，默认把它们当成不存在的东西，volatile 就是咱们替编译器补的一句话：这个对象，您别替它假设值不变。这个场景本主题的最后一篇会走到它真正的用场上，咱们接着把禁令的清单数完。

## 没人消费的读、马上被盖的写

编译器的手里除了合并这把刀，还有两把更干脆的：一把删的是没人消费的读，另一把删的是马上要被覆盖的写。咱们用四个小函数把两把刀都验一遍：

```cpp
// v3_dead.cpp: 死读消除与死写消除 —— 普通变量 vs volatile
int g;             // 普通全局
volatile int vg;   // volatile 全局

void dead_read_plain() {
    int x = g;   // 读到的值没人消费
    (void)x;
}

void dead_read_volatile() {
    int x = vg;
    (void)x;
}

void dead_store_plain() {
    g = 1;   // 后面立即覆盖,这次写是死写
    g = 2;
}

void dead_store_volatile() {
    vg = 1;
    vg = 2;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v3_dead.cpp -o v3_dead.s
```

```asm
_Z15dead_read_plainv:
.LFB0:
	.cfi_startproc
	ret
	.cfi_endproc

_Z18dead_read_volatilev:
.LFB1:
	.cfi_startproc
	movl	vg(%rip), %eax
	ret
	.cfi_endproc

_Z16dead_store_plainv:
.LFB2:
	.cfi_startproc
	movl	$2, g(%rip)
	ret
	.cfi_endproc

_Z19dead_store_volatilev:
.LFB3:
	.cfi_startproc
	movl	$1, vg(%rip)
	movl	$2, vg(%rip)
	ret
	.cfi_endproc
```

咱们一个函数一个函数读。`dead_read_plain` 的整个函数只剩 `ret`，装载被删得干干净净的，读普通变量而不用、就当没读过。`dead_read_volatile` 的 `movl vg(%rip), %eax` 保留了下来，哪怕值是没有人消费的，这次的读也必须发生。`dead_store_plain` 的两次写只剩 `movl $2`，写 1 的那一次被删了（反正马上就要被 2 覆盖）。`dead_store_volatile` 这边的写 1、写 2 都在，省掉哪一次都是不许的。volatile 禁下的第二件事在这里拿到了证据：访问本身不许消除。它的原理与合并的相同，编译器把 volatile 访问当成可见的副作用，它挂在程序的可观察行为清单上，删了它行为就变了。01 里咱们反复用 volatile sink 保住对比的意义，靠的正是这一点：两次写 sink 的次数与顺序都在可观察清单上，优化器是绕不过它的。

## 顺序不许挪：一次肉眼可见的交换

最后的一件禁令最微妙，管的是顺序。普通变量的读写之间，只要两头没有数据上的依赖，编译器就可以交换它们的位置，轮到 volatile 的访问就互相不许换了。咱们空口说顺序重排，您未必有实感，所以咱们做一组在汇编里肉眼可见的实验：

```cpp
// v3_reorder.cpp: 读写搬动 —— 普通变量 vs volatile
// 例1: 无依赖的"读 a、写 b",源顺序读在前 —— 观察编译器是否交换
// 例2: 循环内每次写同一变量 —— 观察写是否被合并/搬出循环
int a, b;
volatile int va, vb;

int ld_st_plain() {
    int v = a;   // 读 a
    b = 42;      // 写 b,与读 a 无依赖
    return v;
}

int ld_st_volatile() {
    int v = va;
    vb = 42;
    return v;
}

void store_loop_plain(int n) {
    for (int i = 0; i != n; ++i)
        a = i;   // n 次写,只有最后一次可见
}

void store_loop_volatile(int n) {
    for (int i = 0; i != n; ++i)
        va = i;
}
```

```bash
g++ -S -O2 -std=c++20 -fno-asynchronous-unwind-tables v3_reorder.cpp -o v3_reorder.s
```

```asm
_Z11ld_st_plainv:
.LFB0:
	.cfi_startproc
	movl	$42, b(%rip)
	movl	a(%rip), %eax
	ret
	.cfi_endproc

_Z14ld_st_volatilev:
.LFB1:
	.cfi_startproc
	movl	va(%rip), %eax
	movl	$42, vb(%rip)
	ret
	.cfi_endproc

_Z16store_loop_plaini:
.LFB2:
	.cfi_startproc
	testl	%edi, %edi
	je	.L4
	subl	$1, %edi
	movl	%edi, a(%rip)
.L4:
	ret
	.cfi_endproc

_Z19store_loop_volatilei:
.LFB3:
	.cfi_startproc
	testl	%edi, %edi
	je	.L9
	xorl	%eax, %eax
	testb	$1, %dil
	je	.L11
	movl	$0, va(%rip)
	movl	$1, %eax
	cmpl	$1, %edi
	je	.L21
	.p2align 5
	.p2align 4
	.p2align 3
.L11:
	movl	%eax, va(%rip)
	leal	1(%rax), %edx
	addl	$2, %eax
	movl	%edx, va(%rip)
	cmpl	%eax, %edi
	jne	.L11
.L9:
	ret
.L21:
	ret
	.cfi_endproc
```

头一对咱们看得心惊：`ld_st_plain` 的源顺序是读 a 在前、写 b 在后，汇编里 `movl $42, b(%rip)` 却排在了读 a 的前面，两条指令真的换了位。依据是同一翻译单元（咱们这个 .cpp 连同它包含的头文件）内 GCC 看得见全部代码，a 与 b 互不干扰这件事它证明得了，交换也就不改变任何可观察的东西。`ld_st_volatile` 里读 va 在前、写 vb 在后的源顺序，被原样保住了。第三件事也有了：volatile 的访问之间不许重排。

后一对的看头更足。`store_loop_plain(n)` 的循环写 a 写了 n 次，编译器认出来的只有最后一次可见，整个循环换成了 `if (n > 0) a = n - 1;` 这一句，汇编里 `testl`、`je` 的形状就是那个 if：n 次的写并成了循环外的一次写，写跨过循环边界被搬走了。`store_loop_volatile(n)` 这边的写一次不少，形状却值得咱们细看：GCC 把循环按 2 展开了，每轮的循环体落两条 `movl`，偶数的 n 走主循环，奇数的 n 进函数后补一次 `movl $0`。展开确实改了循环的结构，可写的总次数仍是 n，次序也还是 `i=0,1,2,…` 的原样，咱们数下来一次都不多、一次都不少。这里咱们把边界说准：volatile 冻结的是访问的次数与顺序，不是编译器的全部调度，只要可观察的访问序列不变，展开、复用寄存器这些调度都是合法的。

禁令的清单到此凑齐：合并、消除、重排，这三件事 volatile 全都替编译器禁了。适用范围您也见到了，前面几组实验的范围从头到尾都在一个函数、一个线程里。出了这个范围它还管不管用？咱们下一篇拿竞争现场接着验。
