---
title: "new 在汇编里的两步走：内存从 operator new 手里来"
chapter: 10
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "跟着一句 new 下到 operator new 的库调用链，看两步交接的地址证据、malloc 的转发，以及分配失败时 bad_alloc 的来路"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# new 在汇编里的两步走：内存从 operator new 手里来

[05 那一篇](../05-classes-in-assembly/02-constructor-and-destructor-calls.md)讲构造函数的时候，咱们给 `new Point(3, 4)` 拍过一张两步走的快照：打头的是 `call _Znwm@PLT`，收尾的是 `call _ZN5PointC1Eii`。当时咱们只说了句落到机器上是两步，就继续讲析构和虚表去了。咱们这几篇把那次调用从头跟到尾：内存从谁的手里来，构造函数在上面干了什么，分配失败的时候程序从哪里拐出去，最后这块内存又是怎么被还回去的。一张快照展开成一段完整的旅程。

旅程有固定的走法。咱们从源码里的一句 new 出发，第一站看它怎么向库要内存，再下到库的内部看那层转发，接着走一趟失败的岔路，然后沿着 delete 的对称两步把内存送回去。归途之后还有两个变体站要停：placement new 省掉的是哪一步，new[] 多出来的又是什么。路上有两处证据最值得看：一处是同一场 gdb 会话里拍下的交接，operator new 交回来的地址，转过头就成了构造函数的 this，连一个数字的改动都没有。另一处在 new[] 那一段的汇编里，咱们直接从内存窗口里读出编译器写在块头的元素个数。您在文中看到的每一块汇编、每一段会话和每一行程序输出，都逐字来自本机的真实运行，例子也是本站原创的。

## 咱们的实验环境

照惯例咱们把环境摆在最前面，后面的每一块输出，您都可以在同样配置的机器上对照复现：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:44:43 UTC 2026 x86_64 GNU/Linux
```

调试器用的是 GNU gdb（GDB）17.2，实验平台是 WSL2 的 x86_64 环境。咱们没有显式给 `-std`，GCC 16 的默认语言标准是 gnu++20，下一篇讲 delete 符号的时候，咱们还会再碰到这个默认。

实验条件用的是系列的老一套。被观察的代码一律用 `-O0` 编译，这是 [01](../01-prefix-vs-postfix-increment/01-int-instructions.md) 里咱们认过的直译档：每条语句都老老实实地翻成指令，不做任何的清理和折叠。汇编统一加了 `-fno-asynchronous-unwind-tables` 降噪，01 里咱们实测过它对 C++ 的效果有限，同步的 `.cfi_*` 仍然会在，所以本篇贴汇编时手工删掉了 `.cfi_*` 行和 `.LFB`、`.LFE` 标签行，指令咱们一条都没有动。`.LEHB`、`.LEHE` 和异常表咱们原样保留，因为异常路径的证据就藏在它们身上，归程的那段汇编要靠它们指路。语法用的是 AT&T，方向是源在左、目的在右，寄存器的名字带 `%`，立即数的写法带 `$`，从 02 起就是系列的默认。汇编行里 `#` 起头的注释都是咱们的解读，指令咱们逐字照贴。会话里的地址也原样保留，gdb 默认关掉的正是地址随机化，所以同批会话之间地址能对得上，您换到别的机器上跑出来的会是别的值，属于正常的现象。

还有一个从 01 延续下来的道具：volatile 的 sink 变量。没有消费者的代码在优化档位下会被整段删掉，咱们把每次分配的结果都交给 volatile 消费掉，观察的对象才活得下来。本篇被观察的汇编都取自 -O0 的编译，例外只有下一篇里 placement 一节的一次 -O2 对照，到那里咱们会单独说明，sink 看上去是多余的，留着它是为了让您把代码原样搬到别的档位也不会失真。

C++ 的符号名要过一道修饰，本篇咱们会反复碰到它们，下面把出场的名单列出来，全部经过 `c++filt` 的实测解码：

| 修饰名 | 本名 |
|---|---|
| `_Znwm` | `operator new(unsigned long)` |
| `_ZnwmPv` | `operator new(unsigned long, void*)`，placement 版 |
| `_Znam` | `operator new[](unsigned long)` |
| `_ZdlPvm` | `operator delete(void*, unsigned long)`，带尺寸版 |
| `_ZdlPvS_` | `operator delete(void*, void*)`，placement 配对版 |
| `_ZdaPv` | `operator delete[](void*)` |
| `_ZdaPvm` | `operator delete[](void*, unsigned long)`，带尺寸版 |
| `_ZN5PointC1Eii` | `Point::Point(int, int)` |
| `_ZN8ResourceC1Ei` | `Resource::Resource(int)` |
| `_ZN8ResourceD1Ev` | `Resource::~Resource()` |
| `_ZN6WidgetC1Ev` | `Widget::Widget()` |
| `_ZN6WidgetD1Ev` | `Widget::~Widget()` |
| `_ZN4Pod3C1Ev` | `Pod3::Pod3()` |

## 分配这一站：内存从 operator new 手里来

咱们把主角请出来。operator new 是标准库提供的分配函数，名字就长成现在的样子，它干的只有一件事：按字节数向分配器要一块内存，要到了就把地址放进 rax 交回来。构造的事不归它管，初始化是 new 表达式紧接着安排的另一件事。咱们用一个最小的类把两步都拍下来：

```cpp
// N1: new 的两步 —— operator new 分配 + 构造函数
struct Point {
    int x;
    int y;
    Point(int x_, int y_) : x(x_), y(y_) {}
};

int main() {
    Point* p = new Point(3, 4);
    volatile int sink = p->x + p->y;  // 防优化消费
    (void)sink;
    delete p;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S -o n1.s n1.cpp
```

`main` 的汇编如下，`#` 注释是咱们加上去的解读：

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$24, %rsp
	movl	$8, %edi            # 第一步的参数:sizeof(Point) = 8 字节
	call	_Znwm@PLT           # 第一步:operator new(8),向分配器要内存
	movq	%rax, %rbx          # 返回值(块地址)从 rax 存进被调用者保存寄存器 rbx
	movl	$4, %edx            # 构造实参 y = 4
	movl	$3, %esi            # 构造实参 x = 3
	movq	%rbx, %rdi          # this = 刚分配的地址(rbx → rdi)
	call	_ZN5PointC1Eii      # 第二步:Point::Point(this, 3, 4)
	movl	$0, %eax
	movq	%rbx, -24(%rbp)
	testb	%al, %al            # al 刚被上一条置 0,恒为零
	je	.L3                     # 恒跳转,下面 3 行是不可达死代码
	movl	$8, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT         # (不可达)构造抛异常时的清理删除
.L3:
	movq	-24(%rbp), %rax
	movl	(%rax), %edx
	movq	-24(%rbp), %rax
	movl	4(%rax), %eax
	addl	%edx, %eax
	movl	%eax, -28(%rbp)     # sink = p->x + p->y
	movl	-28(%rbp), %eax
	movq	-24(%rbp), %rax
	testq	%rax, %rax          # delete 的空指针检查
	je	.L4
	movl	$8, %esi            # sized delete 的尺寸参数
	movq	%rax, %rdi
	call	_ZdlPvm@PLT         # delete p → operator delete(p, 8)
.L4:
	movl	$0, %eax
	movq	-8(%rbp), %rbx
	leave
	ret
```

咱们从第一步的参数看起。`movl $8, %edi` 装进 edi 的是 sizeof(Point) 算出来的 8，3 和 4 都不在它的参数表里，它们要等第二步才作为构造的实参进 esi 与 edx。这个安排把两步的分工摆得很清楚：operator new 认的只有字节数，它不知道也不需要知道 Point 里有什么。咱们拿到地址之后，3 和 4 这才跟着 this 一起进了构造函数。调用目标后面的 `@PLT` 也值得一句白话：PLT 是过程链接表，动态链接下跨共享库的调用都经它中转，`call _Znwm@PLT` 的意思是这一趟要跨进 libstdc++.so.6 里去。跨过去之后是什么，下一站咱们进去看。

两步之间的那段搬运，请您多看一眼，因为不少资料在这里讲含糊了。指针走的路径是从 rax 到 rbx 再到 rdi，中间还在 rbx 那里落了一脚。`movq %rax, %rbx` 把分配的结果从 rax 挪进 rbx 保管，构造的实参装载完之后，`movq %rbx, %rdi` 这才把它送进了第一个实参寄存器 rdi 当 this。为什么非要中转一下？因为 rax 活不过下一次调用。它是返回值的寄存器，也属于调用者保存的那一组，跨过一次 call 就不能再指望里面的值还在，被调的函数完全有权拿它装自己的东西。rbx 则属于被调用者保存的那一组寄存器，约定是谁用的谁负责复原，所以放在 rbx 里的值可以平安穿过一次 call。证据也在咱们贴的清单里：构造调用回来之后，main 用的是 `movq %rbx, -24(%rbp)`，指针从 rbx 落进栈槽当了 p，后头用的都是保管在 rbx 里的值。您以后自己读 -O0 的汇编，看到分配的结果进了 rbx，就知道它在等下一次的调用。

清单里还有一段多余的东西，咱们得提前打个招呼，不然您数 delete 的时候，数出来的会比源码里的语句多一处。构造调用之后紧跟的是 `movl $0, %eax`，跟在后面的 `testb %al, %al` 测的是 al 是否为零，可 al 刚被上一条清了零，测试的结果永远是相等，所以 `je .L3` 永远跳走，中间的那次 `call _ZdlPvm@PLT` 一辈子都执行不到。这是 -O0 不做清理的直译脾气，05 讲构造函数时已经把同款的伪影指给您看过。真正的 `delete p` 语句是 `.L3` 之后带 `testq` 空指针检查的那段，咱们到下一篇的归程再细看它。读 -O0 的 new 和 delete，判别的方法就一条：带 `testq 指针, 指针` 空指针检查的才是语句本体，别的都是编译器照章生成的善后路径，有的恒不可达，有的只在异常时到达。

## 库的内部：operator new 的下面是 malloc

`call _Znwm@PLT` 跨进 libstdc++.so.6 之后发生什么？咱们带着 gdb 跟进去，把调用链的每一层都拍下来。本站的实验咱们记作 N2，它没有新的源码，用的就是 N1 的源程序：把它再编一个带调试信息的可执行文件，用的命令是 `g++ -O0 -g -o n2 n1.cpp`，调试脚本存成了 `n2b.gdb`。脚本是这样的：

```gdb
set pagination off
set confirm off
break operator new(unsigned long)
run
bt
break malloc
continue
bt
finish
finish
p/x $rax
info registers rdi
quit
```

断点的写法请您照这个样子写全签名 `break operator new(unsigned long)`，只写 operator new 的话 gdb 会为难您的。会话的原文如下，`== … ==` 样式的分隔标签是咱们整理转录时插进去的路标，帮您对上下文用的，其余各行都是 gdb 的原样输出：

```text
$ gdb -q -x n2b.gdb ./n2
Reading symbols from ./n2...
Breakpoint 1 at 0x1030

This GDB supports auto-downloading debuginfo from the following URLs:
  <https://debuginfod.archlinux.org>
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to your .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
== bt at operator new entry ==
#0  0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#1  0x000055555555515c in main () at n1.cpp:9
Breakpoint 2 at 0x7ffff78a89a0

Breakpoint 2, 0x00007ffff78a89a0 in malloc () from /usr/lib/libc.so.6
== bt at malloc (below it: operator new) ==
#0  0x00007ffff78a89a0 in malloc () from /usr/lib/libc.so.6
#1  0x00007ffff7cb6005 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#2  0x000055555555515c in main () at n1.cpp:9
== finish malloc, then finish operator new ==
0x00007ffff7cb6005 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
0x000055555555515c in main () at n1.cpp:9
9	Point* p = new Point(3, 4);
== rax = operator new return value ==
$1 = 0x55555556b020
rdi            0x4                 4
```

末行那个 `rdi 0x4` 是脚本里 `info registers rdi` 的输出，咱们原样贴上。operator new 返回之后 rdi 里留着的是 libc 内部干完活以后的残值 4，与咱们的断言无关，但它提醒了一件事：返回之后的寄存器内容不做数，要看的值得在正确的时机取。

会话里的两个层次值得分开说。头一层的 bt 拍在 operator new 的入口，拍到的只有它自己和 main 两帧，malloc 的调用这时还没发生。想在栈上看到 malloc 的帧，咱们就得在 malloc 上另下断点，这是画调用链时最容易画错的地方。第二个断点命中了之后，三层栈就齐了：main 调的是 operator new，operator new 调的是 malloc。帧地址也分属两个库，`0x00007ffff7cb...` 是 libstdc++.so.6 里的地址，`0x00007ffff78a...` 是 libc.so.6 里的。在这套 libstdc++ 16.2 的实现里，operator new 就是薄薄的一层转发，收下的只有字节数，转手就交给了 malloc。

措辞上咱们要收着一点，因为标准在这件事上留了余地。cppreference 的 [operator new 页面](https://en.cppreference.com/w/cpp/memory/new/operator_new)写着，咱们检索的日期是 2026 年 10 月 13 日：

```text
It is unspecified whether library versions of operator new make any calls to
std::malloc or std::aligned_alloc (since C++17).
```

翻译过来：库版本的 operator new 有没有调用 malloc，这件事在标准里是未指定的。咱们今天看到的转发关系是 libstdc++ 16.2 的实现事实，换了别的标准库，很可能就是另一幅的样子，所以本篇的说法一律落在 libstdc++ 的实现里，咱们不把它写成标准的规定。

调用链看清了，咱们回到本篇最想拍的那一幕：两步的交接。上面那场会话里 `$1 = 0x55555556b020` 是 operator new 的返回值，咱们再把两个断点放进同一场会话里抓全，免得跨会话的地址对比落进巧合，这场用的脚本存成了 `n2c.gdb`，加载的还是 `./n2`。会话的原文如下：

```text
Breakpoint 1, 0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
== frame at operator new ==
#0  0x00007ffff7cb5fe0 in operator new(unsigned long) ()
   from /usr/lib/libstdc++.so.6
#1  0x000055555555515c in main () at n1.cpp:9
0x000055555555515c in main () at n1.cpp:9
9	Point* p = new Point(3, 4);
== operator new returned (rax) ==
$1 = 0x55555556b020

Breakpoint 2, Point::Point (this=0x55555556b020, x_=3, y_=4) at n1.cpp:5
5	Point(int x_, int y_) : x(x_), y(y_) {}
== constructor this ==
$2 = (Point * const) 0x55555556b020
$3 = 3
$4 = 4
#0  Point::Point (this=0x55555556b020, x_=3, y_=4) at n1.cpp:5
#1  0x0000555555555171 in main () at n1.cpp:9
```

两场断点之间隔了一次 finish 和一次调用，可地址一次都没变。`$1` 里 operator new 交回来的是 0x55555556b020，`$2` 里构造函数收到的 this 还是 0x55555556b020，它们是同一次运行里的同一个地址。上一节汇编里 rax 经 rbx 进 rdi 的路径，在运行时兑现成了这个数字。构造函数 bt 的直接调用方就是 main 里 new 表达式那一行，中间没有别的帧，构造是编译器原地插入的普通函数调用，05 讲插入点时咱们已经见过它的函数体长什么样。

## 失败的岔路：分配要不到的时候

旅程当然不会总是一帆风顺，咱们专门造一次失败，看看岔路通向了哪里。想触发分配的失败，就要 new 一个大到必然要不到的块：

```cpp
// N3: new 失败路径 —— std::bad_alloc
#include <cstdio>
#include <new>

int main() {
    // (size_t)-1 / 2 ≈ 2^63 - 1 字节,必然远超任何分配上限
    size_t huge = static_cast<size_t>(-1) / 2;
    try {
        char* p = new char[huge];
        std::printf("allocated?! p=%p\n", static_cast<void*>(p));
        delete[] p;
    } catch (const std::bad_alloc& e) {
        std::printf("caught std::bad_alloc, what(): %s\n", e.what());
        return 1;
    }
    return 0;
}
```

```text
$ g++ -O0 -o n3 n3.cpp && ./n3
caught std::bad_alloc, what(): std::bad_alloc
exit code: 1
```

取值的讲究咱们得交代在前面，因为直接写 `new char[(size_t)-1]` 是编译不过的，GCC 在编译期就会拦下这样的写法：

```text
n3.cpp: In function 'int main()':
n3.cpp:5:28: error: size '18446744073709551615' of array exceeds maximum object size '9223372036854775807'
    5 |         char* p = new char[(size_t)-1];
      |                            ^~~~~~~~~~~
```

诊断说得很直白：数组尺寸超过了最大对象尺寸，也就是 2 的 63 次方减 1。`(size_t)-1` 是 64 位里的最大无符号数，一上来就超了线，所以编译期就被拒了。`(size_t)-1 / 2` 走的是整数除法，向下取整以后正好是 2 的 63 次方减 1，和最大对象尺寸的数值相同，恰好压着线通过了编译，到了运行期才分配失败。您自己做实验请照着取这个值，写整个 -1 是过不了编译的。

咱们再看它出场时报的内容。`what()` 打出来的文本就是 `std::bad_alloc` 这个词本身，没有更长的描述。libstdc++ 没打算在这句话里解释失败的原因，咱们也不替它加戏。cppreference 的 [operator new 页面](https://en.cppreference.com/w/cpp/memory/new/operator_new)对失败行为的写法是：

```text
1-4) Throws an exception of a type that would match a handler of type
std::bad_alloc on failure to allocate memory.

Otherwise, if currently no new-handler is installed, throws std::bad_alloc.
```

分配失败就抛一个能被 `std::bad_alloc` 的 handler 接住的异常，咱们的 catch 正好接住了它。放到两步走的框架里看，失败发生在两步的头一步，咱们连内存都还没拿到，第二步的构造根本就不会开始，自然也没有善后的活要干。new 表达式还有一个不抛异常的版本，走的是另一条岔路，[new 表达式页面](https://en.cppreference.com/w/cpp/language/new)的原句是：

```text
If a non-throwing allocation function (e.g. the one selected by
new(std::nothrow) T) returns a null pointer because of an allocation failure,
then the new expression returns immediately, it does not attempt to initialize
an object or to call a deallocation function.
```

nothrow 版失败的时候，分配函数返回的是空指针，new 表达式立即就收了场，初始化对象与调释放函数的步骤都会被跳过。咱们再看它与常规版的差别，差别只在失败的表示方式上：一个抛异常，一个给的是空指针，头一步的失败都会让第二步免谈，这一点两边是一致的。

这两步拆解您可以亲手走。下面的执行器把 main 的完整流程搬了进来，operator new 与 operator delete 用最小桩顶替：分配函数固定返回 .data 里的一块静态缓冲，释放函数只记调用次数。桩之外的一切照文章现场：先 call 分配、rbx 接住块地址，再 call 构造，连那段恒为零的异常清理死代码都原样保留。您单步到 delete 的空指针检查之后，看 rsi 里装的 8，那是 sized delete 带的尺寸参数。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/new_two_steps.json" />

还有一种失败咱们还没遇到：内存到手了，构造函数半路抛了异常。那时内存已经在咱们名下，善后的活再也不能省。岔路口的应急代码长什么样，咱们在[下一篇](02-delete-two-steps-and-placement-new.md)正好会路过它。
