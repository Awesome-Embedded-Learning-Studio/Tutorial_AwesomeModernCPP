---
title: "NRVO：返回路径上的拷贝从未存在"
chapter: 8
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "具名局部 p 直接构造进 sret 槽，帧里没有 p 的座位、return p 只是一条 movq、计数报 0，四件证据立住 NRVO，再用七个编译版本数出 C++14 的 2 次与 C++20 的 1 次，C++17 把接收侧的省略写进了语法"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# NRVO：返回路径上的拷贝从未存在

前两篇把三条物理通道与分路的判据验完了，接下来是 C++ 语言自己的规则。咱们用的环境与[第一篇](01-rax-family-and-register-pairs.md)相同，您要复跑就照它来。01 对比 i++ 与 ++i 的时候，后缀自增的返回路径上有一桩怪事：教科书说返回要发生两次拷贝，汇编里咱们只见到一次构造，构造的目标还是返回槽。当时咱们给它挂了 NRVO 的名字就继续赶路了，这一篇咱们把它正式展开。咱们把源码贴在下面，Point 是带拷贝计数器的类型，make_point 返回的是局部对象 p，这是命名返回值优化最典型的用武之地：

```cpp
// NRVO 的汇编证据 + 拷贝构造计数(默认版与禁省略版共用这份源码)
#include <cstdio>

struct Point {
    long x, y;
    static long copies;
    Point(long x_, long y_) : x(x_), y(y_) {}
    Point(const Point& o) : x(o.x), y(o.y) { ++copies; }
};
long Point::copies = 0;

Point make_point(long seed) __attribute__((noinline));
Point make_point(long seed) {
    Point p{seed, seed * 2};   // 局部对象
    return p;                  // 具名返回 -> NRVO 的用武之地
}

int main() {
    Point q = make_point(7);
    volatile long sink = q.x + q.y;   // 只读成员,不触发拷贝
    std::printf("copies = %ld, q = (%ld, %ld), sink = %ld\n",
                Point::copies, q.x, q.y, sink);
    return 0;
}
```

## 默认形态：构造直接落在接收方的栈槽里

编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r4_nrvo.cpp -o r4_nrvo.s`。咱们把 make_point 的函数体逐字贴出来，省略了 `.cfi_*` 行：

```asm
_Z10make_pointl:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movq	%rdi, -8(%rbp)        ; 溢出 sret 指针
	movq	%rsi, -16(%rbp)       ; 溢出 seed
	movq	-16(%rbp), %rax
	leaq	(%rax,%rax), %rdx     ; seed*2
	movq	-16(%rbp), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi            ; this = sret 槽!
	call	_ZN5PointC1Ell        ; Point::Point(long,long) 直接构造进槽
	nop
	movq	-8(%rbp), %rax        ; return p = 把槽地址还回去
	leave
	ret
```

咱们把 main 侧的调用与接收段也贴出来：

```asm
	leaq	-32(%rbp), %rax
	movl	$7, %esi
	movq	%rax, %rdi
	call	_Z10make_pointl
	movq	-32(%rbp), %rdx       ; 直接读 q
	movq	-24(%rbp), %rax
	addq	%rdx, %rax
```

返回路径上没发生拷贝的判断，咱们手里得有四件证据才立得住：

- make_point 自己的栈帧只有 `subq $16`，两个槽装的是溢出的 sret 指针与 seed，局部 p 在帧里没有座位。
- 构造函数的 this 取的是 `-8(%rbp)` 里存的 sret 地址，也就是说 p 从出生起就住在调用方的栈槽里。05 讲构造与 this 的时候您见过这个位置的用法，这次的 this 落在了别人的地盘上。
- `return p;` 编译成一条 `movq -8(%rbp), %rax`，把槽地址还回去，没有第二次构造，也没有任何拷贝调用。
- 程序跑出来的计数是 0。

运行输出咱们贴在下面：

```text
copies = 0, q = (7, 14), sink = 21
```

咱们手里还有一层佐证，拷贝构造的调用次数是零，连接之后连符号都没了。`nm` 的输出里只有 `Point::Point(long,long)`，没有拷贝构造的名字，objdump 里数拷贝构造的 call 也数出 0 个：

```text
$ nm r4_nrvo | grep Point
0000000000004028 B _ZN5Point6copiesE
00000000000011f8 W _ZN5PointC1Ell
00000000000011f8 W _ZN5PointC2Ell
$ objdump -d r4_nrvo | grep -c 'call.*PointC1ERKS_'
0
```

没被用到的内联成员函数根本不会被实例化，所以没调用与没实例化同时成立。计数 0 与汇编的证据互为印证，而这个计数器本身没有坏，咱们马上用禁省略的版本来反证。

这套做法的标准出处，咱们在 cppreference 的 copy elision 页里找到了，它给 NRVO 的定义原文是：

```text
In a return statement in a function with a class return type, when the operand
is the name of a non-volatile object obj with automatic storage duration
(other than a function parameter or a handler parameter), the
copy-initialization of the result object can be omitted by constructing obj
directly into the function call's result object. This variant of copy elision
is known as named return value optimization (NRVO).
```

翻译过来：函数的返回类型是类类型，return 的操作数是一个自动存储期的非 volatile 对象的名字，而不是函数参数，这时结果对象的拷贝初始化可以被省略，办法是把 obj 直接构造进函数调用的结果对象。这个变体的名字叫命名返回值优化。咱们汇编里看到的，正是把 p 直接构造进 sret 槽的动作。01 里笔者就意外过一次，GCC 在 -O0、一档优化都不开的默认形态下就做了它，在这个编译器里它算代码生成的默认形态，不是哪档优化挑出来的结果。

## 把省略关掉：同一函数的镜像

您想亲眼看拷贝回来，咱们加上 `-fno-elide-constructors` 就能把拷贝省略关掉。咱们拿同一份 r4_nrvo.cpp 配出七个版本：头一份编出来的可执行还叫 r4_nrvo，是默认的 -O0 版。后面六份的文件名换成了 r5 前缀，那是做实验时的批次叫法，对应的差异您可以在命令里逐个对上，默认与禁省略做了交叉，又叠了两种方言与两档优化：

```bash
g++ -O0 r4_nrvo.cpp -o r4_nrvo
g++ -O0 -fno-elide-constructors r4_nrvo.cpp -o r5_O0_noelide
g++ -O2 r4_nrvo.cpp -o r5_O2_default
g++ -O2 -fno-elide-constructors r4_nrvo.cpp -o r5_O2_noelide
g++ -std=c++14 -O0 r4_nrvo.cpp -o r5_cpp14_O0_default
g++ -std=c++14 -O0 -fno-elide-constructors r4_nrvo.cpp -o r5_cpp14_O0_noelide
g++ -std=c++14 -O2 -fno-elide-constructors r4_nrvo.cpp -o r5_cpp14_O2_noelide
```

咱们把七组运行的输出逐字贴出来：

```text
== c++20 -O0 默认     ==          (./r4_nrvo)
copies = 0, q = (7, 14), sink = 21
== c++20 -O0 禁省略   ==          (./r5_O0_noelide)
copies = 1, q = (7, 14), sink = 21
== c++20 -O2 默认     ==          (./r5_O2_default)
copies = 0, q = (7, 14), sink = 21
== c++20 -O2 禁省略   ==          (./r5_O2_noelide)
copies = 1, q = (7, 14), sink = 21
== c++14 -O0 默认     ==          (./r5_cpp14_O0_default)
copies = 0, q = (7, 14), sink = 21
== c++14 -O0 禁省略   ==          (./r5_cpp14_O0_noelide)
copies = 2, q = (7, 14), sink = 21
== c++14 -O2 禁省略   ==          (./r5_cpp14_O2_noelide)
copies = 2, q = (7, 14), sink = 21
```

咱们把数字汇总成表：

| 标准/开关 | -O0 默认 | -O0 禁省略 | -O2 默认 | -O2 禁省略 |
|---|---|---|---|---|
| c++20(GCC 16 默认) | 0 | 1 | 0 | 1 |
| c++14 | 0 | 2 | — | 2 |

默认版报出的全是 0，这样的事不新鲜。新鲜的在禁省略那一列：C++14 语义下数出 2，咱们默认的 gnu++20 语义下只有 1。老教材的那句关掉省略会看到两次拷贝，说的是 C++14 的世界，而 C++17 起它就不再成立了，因为语言把其中一次省略升级成了强制，开关也救不回它了。咱们把禁省略版 make_point 的汇编也贴出来对照，编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables -fno-elide-constructors r4_nrvo.cpp`，省略了 `.cfi_*` 与栈保护：

```asm
_Z10make_pointl:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$48, %rsp
	movq	%rdi, -40(%rbp)       ; sret 指针
	movq	%rsi, -48(%rbp)       ; seed
	movq	-48(%rbp), %rax
	leaq	(%rax,%rax), %rdx
	movq	-48(%rbp), %rcx
	leaq	-32(%rbp), %rax       ; p 现在有自己的槽(本帧 -32)
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZN5PointC1Ell        ; 先构造进本帧局部 p
	leaq	-32(%rbp), %rdx
	movq	-40(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZN5PointC1ERKS_      ; 再拷贝 p -> sret 槽(这就是那 1 次)
	movq	-40(%rbp), %rax
	leave
	ret
```

咱们眼前的两个版本是完美的镜像，说的是同一个函数。默认版的帧是 16 字节，禁省略版膨胀到了 48 字节，p 在本帧 -32 有了自己的座位，构造完了 p，再由 `call _ZN5PointC1ERKS_` 这笔拷贝构造把整份的对象拷进 sret 槽，这就是数出来的那 1 次。构造局部再拷出去的这个动作，不是被优化掉才消失的，它在 GCC 的汇编输出里从未出现。

C++20 语义下剩下的只有这一次，C++14 的语义下就还有第 2 次，多出来的那一次拷贝发生在接收侧，也就是 main 里 q 的初始化。C++17 的规定让 `make_point(7)` 这样的纯右值直接在 q 的存储里构造，拷贝从语法上就不存在了，所以咱们手里的 `-fno-elide` 对它无计可施。这里的纯右值是 prvalue 的中文名，指的是还没在内存里落地的那类值表达式，物化说的就是它在内存里真正占下一个位置的动作。

## C++17 把一次省略写进了语法

原文咱们请两段。头一段讲强制省略的机制，出自 cppreference 的 copy_elision 页：

```text
Since C++17, a prvalue is not materialized until needed, and then it is
constructed directly into the storage of its final destination. This sometimes
means that even when the language syntax visually suggests a copy/move (e.g.
copy initialization), no copy/move is performed — which means the type
need not have an accessible copy/move constructor at all.
```

咱们把它翻译过来：自 C++17 起，prvalue 在被需要之前是不会物化的，需要的时候会直接构造进最终目标的存储。有时语言语法在字面上暗示了一次拷贝或移动，比如拷贝初始化的写法，实际发生的次数是零，类型甚至可以完全没有可访问的拷贝或移动构造函数。

笔者觉得第二段更有意思，标准连优化的名分都没给它：

```text
Note: This rule does not specify an optimization, and the Standard does not
formally describe it as 'copy elision' (because nothing is being elided).
Instead, the C++17 core language specification of prvalues and temporaries is
fundamentally different from that of earlier C++ revisions: there is no longer
a temporary to copy/move from. Another way to describe C++17 mechanics is
'unmaterialized value passing' or 'deferred temporary materialization':
prvalues are returned and used without ever materializing a temporary.
```

咱们翻译过来的意思是：它没有规定任何优化，标准也没有把 C++17 的这部分正式叫作拷贝省略，因为被省略的东西根本不存在。C++17 对 prvalue 与临时对象的核心语言规定，与更早的 C++ 版本已经是两套，临时对象已经不存在了，自然也没有从它出发的拷贝或移动。另一种描述是未物化的值传递，或者叫推迟的临时对象物化。

C++17 前后的待遇是不一样的。在 C++17 之前的语义里，接收侧的省略与具名返回的 NRVO 都是标准允许的优化，做不做全由编译器说了算，C++14 的禁省略版数出 2，就是两边都恢复原样的样子。到了 C++17，接收侧的省略写进语言成了语法本身，NRVO 依旧属于可选的优化，所以 GCC 16 的 C++20 方言在禁省略下只剩被调侧那 1 次。特性测试宏 `__cpp_guaranteed_copy_elision` 的值是 201606L，您可以用它判断手上的工具链兑现到哪一步。

cppreference 的 Notes 还有一句实践提醒，咱们顺手引了：

```text
Because some compilers do not perform copy elision in every situation where it
is allowed (e.g., in debug mode), programs that rely on the side-effects of
copy/move constructors and destructors are not portable.
```

翻译过来：有的编译器并不是在每个允许省略的场景都做省略，比如调试模式的场景，依赖拷贝、移动构造函数或析构函数副作用的程序是不可移植的。咱们反过来读它，别把逻辑挂在拷贝构造的执行次数上，今天的 GCC 连 -O0 都做省略，01 的复现提示里也说过，更老的编译器在 -O0 不做 NRVO，同一个程序数出来的就是 2，那并不代表程序错了，只能算版本上的差异。省略的机制，咱们到这儿就看全了。

这套机制搬进了执行器，您可以看到"没有搬运"长什么样。下面的场景是 make_point(7)：main 先在 rbp-32 备好 16 字节的槽，槽地址经 rdi 传进去，被调方对着槽直接调构造，rax 里还回来的只是槽地址。您单步进 make_point，重点看 rdi——它从头到尾指着 main 的栈，没有任何一步在搬运对象本体。最后 main 直接从槽里读出 x=14、y=7 相加得 21。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/sret_inplace.json" />
