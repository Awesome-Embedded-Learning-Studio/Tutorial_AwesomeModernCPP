---
title: "new[] 的三个新面孔：块头 cookie、循环构造与倒序析构"
chapter: 10
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
description: "new[] 在块头多要 8 字节 cookie 存元素个数，构造顺着地址循环、析构从尾后倒着走，平凡析构的类型连 cookie 都不要，最后把整段旅程连起来"
reading_time_minutes: 12
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# new[] 的三个新面孔：块头 cookie、循环构造与倒序析构

环境、工具和贴文的读法都跟[第一篇](01-allocation-from-operator-new.md)相同，您要复跑就照它来。上一篇的 placement new 留下了调用、抽掉了分配，变体咱们还剩最后一个没看。

## 变体的另一站：new[]，块头的 cookie 与倒序析构

第二个变体把一个对象变成了一排。new[] 的故事里有三个新面孔：块头的 cookie、逐元素的循环构造，还有倒序的析构。cookie 在这里的含义得说清楚：它是编译器在数组块头部多要的 8 个字节，存的是元素个数，将来的 delete[] 靠它知道要析构几个。咱们用会打印的 Widget 做素材：

```cpp
// N6: new[] 与 delete[] —— 逐元素构造/析构与头部 cookie
#include <cstdio>

struct Widget {
    int id;
    Widget() : id(0) { std::printf("Widget() ctor\n"); }
    explicit Widget(int i) : id(i) { std::printf("Widget(%d) ctor\n", i); }
    ~Widget() { std::printf("Widget(%d) dtor\n", id); }
};

int main() {
    Widget* w = new Widget[3];
    volatile int sink = w[2].id;
    (void)sink;
    delete[] w;
    return 0;
}
```

源码里有一个笔者自己犯过的错得交代。头一版咱们只给了 `explicit Widget(int)`，编译直接就被拒了，报错的原文是：

```text
n6.cpp: In function 'int main()':
n6.cpp:11:29: error: no matching function for call to 'Widget::Widget()'
   11 |     Widget* w = new Widget[3];
      |                             ^
  • there are 2 candidates
    • candidate 1: 'Widget::Widget(int)'
      n6.cpp:6:14:
          6 |     explicit Widget(int i) : id(i) { std::printf("Widget(%d) ctor\n", i); }
            |              ^~~~~~~
    • candidate expects 1 argument, 0 provided
    • candidate 2: 'constexpr Widget::Widget(const Widget&)'
```

new[] 要做的就是把每个元素默认构造出来，缺了默认的构造函数就编不过。您自己写例子的时候，默认构造的版本别漏了。

```text
$ g++ -O0 -o n6 n6.cpp && ./n6
Widget() ctor
Widget() ctor
Widget() ctor
Widget(0) dtor
Widget(0) dtor
Widget(0) dtor
```

三声构造的打印在前，三声析构的打印在后，而且您细看析构那边的顺序，id 相同的时候看不出来，咱们到汇编里看游标的方向。`main` 的汇编如下，构造和析构的函数体删节：

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r15
	pushq	%r14
	pushq	%r13
	pushq	%r12
	pushq	%rbx
	subq	$24, %rsp
	movl	$20, %edi           # 注意:不是 12!3*4 + 8 字节 cookie = 20
.LEHB0:
	call	_Znam@PLT           # operator new[](20)
.LEHE0:
	movq	%rax, %rbx
	movq	$3, (%rbx)          # cookie:元素个数 3 写进块头 8 字节
	movl	$1, %r15d
	leaq	8(%rbx), %r12       # 数组首元素地址 = 块首 + 8(new[] 的求值结果)
	movl	$2, %r13d           # 循环计数器(3-1 起)
	movq	%r12, %r14          # 游标指针
	jmp	.L4
.L5:
	movq	%r14, %rdi
.LEHB1:
	call	_ZN6WidgetC1Ev      # 逐元素构造(循环体)
.LEHE1:
	subq	$1, %r13            # 计数器递减
	addq	$4, %r14            # 游标前进 sizeof(Widget)
.L4:
	testq	%r13, %r13
	jns	.L5                     # 计数器 >= 0 则继续 → 共 3 次
	movl	$0, %edx
	leaq	8(%rbx), %rax
	movq	%rax, -56(%rbp)
	testb	%dl, %dl
	je	.L6                     # 恒跳,死代码开始
	movl	$20, %esi
	movq	%rbx, %rdi
	call	_ZdaPvm@PLT
.L6:
	movq	-56(%rbp), %rax
	addq	$8, %rax
	movl	(%rax), %eax
	movl	%eax, -60(%rbp)     # sink = w[2].id
	movl	-60(%rbp), %eax
	cmpq	$0, -56(%rbp)
	je	.L7
	movq	-56(%rbp), %rax
	subq	$8, %rax            # w - 8:回到 cookie
	movq	(%rax), %rax        # 读出元素个数 n = 3
	leaq	0(,%rax,4), %rdx    # n * sizeof(Widget)
	movq	-56(%rbp), %rax
	leaq	(%rdx,%rax), %rbx   # 尾后指针 = w + n*4
.L9:
	cmpq	-56(%rbp), %rbx
	je	.L8
	subq	$4, %rbx            # 从最后一个元素倒着走
	movq	%rbx, %rdi
	call	_ZN6WidgetD1Ev      # 逐元素析构(倒序)
	jmp	.L9
.L8:
	movq	-56(%rbp), %rax
	subq	$8, %rax
	movq	(%rax), %rax        # 再读一次 cookie:n
	addq	$2, %rax
	leaq	0(,%rax,4), %rdx    # (n + 2) * 4 = n*4 + 8 = 20,整块尺寸
	movq	-56(%rbp), %rax
	subq	$8, %rax            # 块首(w - 8)而非 w
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZdaPvm@PLT         # operator delete[](块首, 20)
.L7:
	movl	$0, %eax
	jmp	.L16
.L15:                             # 异常着陆:构造抛出时只析构已构造的元素
	movq	%rax, %r14
	testq	%r12, %r12
	je	.L12
	movl	$2, %eax
	subq	%r13, %rax
	salq	$2, %rax
	leaq	(%r12,%rax), %r13
.L13:
	cmpq	%r12, %r13
	je	.L12
	subq	$4, %r13
	movq	%r13, %rdi
	call	_ZN6WidgetD1Ev
	jmp	.L13
.L12:
	movq	%r14, %r12
	testb	%r15b, %r15b
	je	.L14
	movl	$20, %esi
	movq	%rbx, %rdi
	call	_ZdaPvm@PLT
.L14:
	movq	%r12, %rax
	movq	%rax, %rdi
.LEHB2:
	call	_Unwind_Resume@PLT
.LEHE2:
.L16:
	addq	$24, %rsp
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	popq	%rbp
	ret
```

开场第一条指令就把 cookie 摆上了台面。`movl $20, %edi` 要的是 20 字节，三个 int 占的明明只有 12，多出来的 8 就是块头的 cookie。分配回来的头一件事是 `movq $3, (%rbx)`，它把元素个数 3 写进了块头的那个 64 位字。随后的 `leaq 8(%rbx), %r12` 算出数组首元素的地址，从块首加上了 8，跳过了 cookie。这里有个容易错记的点：new[] 表达式求值的结果不是块首，落点是块首加 8 的位置。咱们在 gdb 里把这件事和 cookie 一起眼见为实，断点下在 `volatile int sink = w[2].id;` 的那一行：

```text
Breakpoint 1, main () at n6.cpp:13
13	    volatile int sink = w[2].id;
== w and and-w[0] ==
$1 = (Widget *) 0x55555556b028
$2 = (Widget *) 0x55555556b028
== raw memory at w-8 (two giant words) ==
0x55555556b020:	0x0000000000000003	0x0000000000000000
== same as 32-bit words ==
0x55555556b020:	0x00000003	0x00000000	0x00000000	0x00000000
0x55555556b030:	0x00000000	0x00000000
```

w 的值是 0x55555556b028，咱们用内存窗口看它前面 8 字节，地址 0x55555556b020 处的第一个 64 位字是 `0x0000000000000003`。cookie 从文档里的名词变成了看得见的字节，就住在您数组指针的前面 8 字节。

构造这一侧的形态也值得看。它是货真价实的循环，构造的调用没有展开成三份：计数器 r13d 从 2 起步，那个 2 是元素个数减一的结果，循环的出口靠 `testq` 配 `jns` 把守，计数器只要还顶着非负的值，就带咱们回 `.L5` 再来一轮，游标 r14 每轮的增量是 4，走向排在后面的元素。三个元素是同一份循环体跑出来的，您要是 new 一百个，跑的还是同一个循环体。构造的顺序顺着地址递增，从前往后地一路排下去。

delete[] 那一侧的舞步咱们逐步看。个数的记录它从头到尾都没有留，而是现场回到 cookie 去读：`subq $8` 退到块头，`movq (%rax), %rax` 把 n 读了出来，乘上 4 就算出了尾后指针。随后的 `.L9` 循环从尾后指针开始，每轮把游标退回一个元素的宽度，调的析构函数是 `_ZN6WidgetD1Ev`，一路退到数组的开头为止。构造的方向是从前往后，析构的方向是从后往前，和栈上对象走的是同一个道理，05 讲嵌套作用域时咱们在栈帧上看过一模样的次序。全部析构完了之后，最后由 `subq $8` 再算一次块首的位置，从 cookie 里把 n 又读了一遍，再用 n 加 2 乘 4 的算式凑出整块的尺寸 20，把块首和 20 一起交给带尺寸版的 `_ZdaPvm`。注意这个尺寸是整块的 20，不是元素的 12，分配的时候是怎么要的，释放的时候就怎么还回去。

异常着陆区 `.L15` 的作为咱们也看一眼：构造中途抛了异常，它只倒序析构已经构造成功的那些元素，然后就释放了整块，把继续上抛的活交给 `_Unwind_Resume`。个数从 r13 的残值里现算，多析的一个也没有，少析的同样一个都没有。

cookie 是不是总在？咱们做一个对照实验就有答案。把 Widget 换成没有用户析构的同型结构，`main` 的关键行是这样的：

```asm
	movl	$12, %edi           # 3*4,无 cookie
.LEHB0:
	call	_Znam@PLT           # operator new[](12)
...
	call	_ZN4Pod3C1Ev        # 构造循环仍在(默认构造非平凡)
...
	movq	%rbx, %rdi
	call	_ZdaPv@PLT          # 无 cookie → 无读个数、无析构循环,无尺寸 _ZdaPv
```

咱们把元素类型换成平凡析构的再看，分配要的就是 12 字节整，cookie 不在了，析构的循环也不在了，delete[] 也退化成无尺寸的 `_ZdaPv`。道理想通了一点都不奇怪：析构无事可做，delete[] 也就用不着知道个数了，cookie 的存在理由只剩下了一条，那就是帮 delete[] 把要析构的个数数清楚。cppreference 引的 Itanium C++ ABI 原句把有无 cookie 的边界规定得明明白白，GCC 在 Linux 上遵循的就是这套 ABI，MSVC 也是同样的做法：

```text
Itanium C++ ABI requires that the array allocation overhead is zero if the
element type of the created array is trivially destructible. So does MSVC.
```

[new 表达式页面](https://en.cppreference.com/w/cpp/language/new)对数组开销的说明也值得读一遍，咱们检索的日期是 2026 年 10 月 13 日：

```text
Array allocation may supply unspecified overhead, which may vary from one call
to new to the next, unless the allocation function selected is the standard
non-allocating form. The pointer returned by the new expression will be offset
by that value from the pointer returned by the allocation function. Many
implementations use the array overhead to store the number of objects in the
array which is used by the delete[] expression to call the correct number of
destructors.
```

数组分配可以带未指明的开销，new 表达式返回的指针相对分配函数的返回值偏移这个量，许多实现都用它存放元素的个数、供 delete[] 取用。咱们今天量出来的 8 字节偏移和 gdb 里的那个 3，就是这段话在本机的具体值。

cookie 的存在还顺带回答了一个老问题，就是 new[] 的指针为什么不能交给 delete。您拿 w 去走 delete，delete 的站点既不会去读个数，析构的循环整个都不会发生，而且它交给 operator delete 的指针是 w 本身，比真正的块首多出了 8 字节，对分配器来说这是一个从来没有交给过您的地址。两边的指令在每一个环节都对不上，配对使用因此是指令层的硬约束。

## 把整段旅程连起来

咱们从源码里的一句 `new Point(3, 4)` 出发，现在可以把它的一生连起来讲了。它在机器上是一串接力：operator new 经 PLT 跨进了 libstdc++，libstdc++ 里的转发把它交给 malloc，地址从 rax 寄存到了 rbx，再进 rdi 充当了 this，构造函数就在这块还裸着的内存上动土。使用的期限到了，delete 让析构走在了前面，operator delete 随后带着尺寸把块还了回去。失败的下场有两种：要不到内存就抛 bad_alloc，头一步就了结，构造的环节免谈，内存到手而构造抛异常的，就由着陆区的善后代码还了内存再继续展开。两个变体动的部位各不相同：placement new 留下了调用却抽掉了分配的实质，new[] 在两步之外多出了一块 cookie 和一排循环的构造。

想往语言层深走的读者，去处咱们指几个。new 与 delete 的语义、失败处理和 placement new 的完整讲法在[卷一的动态内存管理一篇](../../../vol1-fundamentals/ch12/02-new-delete.md)，析构函数与资源管理的关系在[卷一的析构函数一篇](../../../vol1-fundamentals/ch06/03-destructors.md)。在本系列的篇目里，05 给您看过 this 的出身和构造调用的插入点，09 带您靠 unwind 表爬过调用链，今天咱们把两者之间的交接也补上了。

收尾把最省事的自证留给您。咱们开一个 gdb 的会话，加载第一篇 N1 的程序，断点的写法要照全签名 `break operator new(unsigned long)`，等断点命中了就 `finish`，用 `p/x $rax` 记下当时的地址，再下 `break Point::Point` 的断点继续运行，看断点报告里的 this。两处的值会一模一样，就像咱们那场会话里的 0x55555556b020。接着您再跑一遍 N6，在 sink 的那一行停下，用内存窗口看 w 前面的 8 字节，第一个 64 位字写的就是 3。两个数字到了手，两步走的交接和块头的 cookie 就都不再是别人说的了，是您自己拍下来的。

不想开 gdb 的话，执行器里也有一份可以单步的现场。下面的场景把 new[] 的全过程搬了进来：您先看传给分配函数的立即数是 20 不是 12，cookie 那 8 字节就藏在差值里。构造循环走完后，delete[] 读回块头的 3，从最后一个元素往前收，析构的调用逐个落下来。分配函数依旧是 .data 桩顶替，栈寄存器的用法照文章：数组首元素地址装在被调用者保存的 r12 里，循环下标住在栈槽上。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/array_new_cookie.json" />
