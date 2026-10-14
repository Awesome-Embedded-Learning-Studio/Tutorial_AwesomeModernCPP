---
title: "i++ 与 ++i：vector 迭代器与副作用的边界"
chapter: 1
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "vector 的迭代器在 -O2 下前后缀逐指令相同，拷贝构造带上可观察副作用后，省不掉的只剩副作用本身，最后给出该写哪个的判断法"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# i++ 与 ++i：vector 迭代器与副作用的边界

咱们把[第一篇](01-int-instructions.md)的实验环境原样带到这篇来用。

## 标准库的真家伙：vector 的迭代器

自己手写的迭代器说服力终归有限，咱们换标准库的 vector 上场。两份源码的差别只有一处：循环里的 `it++` 换成了 `++it`。函数名则是刻意取得相同的，修饰出来的名字才会一致，这样 diff 出来才是干净的：

```cpp
#include <vector>

volatile int sink;

int sum(const std::vector<int>& v) {
    int s = 0;
    for (auto it = v.begin(); it != v.end(); it++)
        s += *it;
    return s;
}

int main() {
    std::vector<int> v{1, 2, 3, 4, 5};
    sink = sum(v);
    return 0;
}
```

另一份把循环里的自增写成 `++it`，其余的一个字都不差。在看 -O0 的输出之前，咱们得认识一下 libstdc++ 迭代器的真身，下面是 `/usr/include/c++/16/bits/stl_iterator.h` 第 1098 到 1112 行的原文：

```cpp
      __attribute__((__always_inline__))
      _GLIBCXX14_CONSTEXPR
      __normal_iterator&
      operator++() _GLIBCXX_NOEXCEPT
      {
	++_M_current;
	return *this;
      }

      __attribute__((__always_inline__))
      _GLIBCXX14_CONSTEXPR
      __normal_iterator
      operator++(int) _GLIBCXX_NOEXCEPT
      { return __normal_iterator(_M_current++); }
```

咱们原以为，-O0 的汇编里能看到对 operator++ 的函数调用，库的实际做法把这个预判推翻了。后缀这边没有采用教科书那个 tmp 两步的写法，写的是 `return __normal_iterator(_M_current++);`，也就是让内部的裸指针去做后缀（产出旧值）、再用它直接把返回值构造出来。而从 C++17 起，返回纯右值的构造不再有拷贝，直接落在调用方的槽里。纯右值（prvalue）粗略说就是刚算出来、还没住进任何变量的值，咱们这句里的 `_M_current++` 产出的就是它。再看调用这件事本身：这些成员全部标了 `__attribute__((__always_inline__))`，属性的效力一直管到 -O0，于是 -O0 的汇编里您看不到对 operator++ 的 call（begin 和 end 还是老实的 call，没有得到这样的优待），差异会以内联展开的形态出现。

咱们看 -O0 的循环自增段（AT&T 语法）。后缀版的循环体（`.L8` 是循环体标签，前面三行是在做 `s += *it` 的求和，后面被省略的是循环判断）：

```asm
.L8:
	movq	-48(%rbp), %rax
	movl	(%rax), %eax
	addl	%eax, -56(%rbp)
	movl	$0, -52(%rbp)
	movq	-48(%rbp), %rax
	leaq	4(%rax), %rdx
	movq	%rdx, -48(%rbp)
	movq	%rax, -40(%rbp)
	movq	-40(%rbp), %rax
	movq	%rax, -32(%rbp)
	nop
	nop
```

咱们再看前缀版（AT&T 语法）：

```asm
.L8:
	movq	-40(%rbp), %rax
	movl	(%rax), %eax
	addl	%eax, -44(%rbp)
	movq	-40(%rbp), %rax
	addq	$4, %rax
	movq	%rax, -40(%rbp)
	nop
```

咱们从 `movl $0, -52(%rbp)` 数起，后缀的自增段七条，前缀的指令是三条。咱们把后缀版自增段的七条列成表：

| 指令 | 它干的事 |
|---|---|
| `movl $0, -52(%rbp)` | 把后缀哑参数物化出来 |
| `movq -48(%rbp), %rax` | 把指针的旧值读进 rax |
| `leaq 4(%rax), %rdx` | 在 rdx 里算出指针加 4 的新值 |
| `movq %rdx, -48(%rbp)` | 新指针写回迭代器 |
| `movq %rax, -40(%rbp)` | 旧值存进裸指针自增的结果临时 |
| `movq -40(%rbp), %rax` | 把结果临时读回来 |
| `movq %rax, -32(%rbp)` | 写进返回的迭代器对象（内联后的返回槽） |

您对着表数一遍就能看出，多出来的四条全在伺候那个没人要的旧值：哑参数被物化了一次，旧值落进了 `-40`（裸指针 `_M_current++` 的结果临时），又从 `-40` 搬进了 `-32`（内联展开后的返回槽），前前后后入了两回栈。整个函数的栈帧，后缀版用了 80 字节，前缀版只用了 64 字节，两份 .s 文件的 diff 出了 67 行不同，自增段之外的部分，全是栈槽偏移挪位带出来的连带差异。

然后咱们看 -O2。四个编译命令加上两次 diff 就是全部的家当（`grep -c` 数出的 67 就是上面说的连带差异行数）：

```bash
g++ -O0 -S a6_post.cpp -o a6_post_O0.s
g++ -O0 -S a6_pre.cpp  -o a6_pre_O0.s
g++ -O2 -S a6_post.cpp -o a6_post_O2.s
g++ -O2 -S a6_pre.cpp  -o a6_pre_O2.s
diff a6_post_O0.s a6_pre_O0.s | grep -c "^[<>]"
diff a6_post_O2.s a6_pre_O2.s
```

咱们拿到的 -O2 diff 完整输出只有这么一点：

```diff
1c1
< 	.file	"a6_post.cpp"
---
> 	.file	"a6_pre.cpp"
```

两个文件除了写着各自文件名的 `.file` 行之外完全相同。咱们看 sum 的函数体（AT&T 语法），指针从头部走到了尾部，一路地加过去：

```asm
_Z3sumRKSt6vectorIiSaIiEE:
	movq	(%rdi), %rax
	movq	8(%rdi), %rcx
	xorl	%edx, %edx
	cmpq	%rcx, %rax
	je	.L1
	.p2align 4
	.p2align 4
	.p2align 3
.L3:
	addl	(%rax), %edx
	addq	$4, %rax
	cmpq	%rcx, %rax
	jne	.L3
.L1:
	movl	%edx, %eax
	ret
```

咱们再看 main，它被编译器折叠成了三条指令：`movl $15` 把 15 写进 sink（15 就是 1 加到 5 的和），`xorl` 把返回值寄存器清了零，最后的一条 `ret` 收尾。volatile 的存储保留了下来，被调用的函数一个都没有跑。旧值没有了使用者，连同哑参数、临时构造一起消失了，两份输出之间 diff 只认了那一行 `.file`。自增形式对生成代码的影响，到这里就只剩一个文件名了。

还有个值得记下的细节：内联展开的路径上，旧值经了一次栈中转（`-40`）才进返回槽（`-32`），并没有享受[上一篇](02-class-type-cost.md)里独立函数体那样的 -O0 NRVO 待遇。always_inline 的前端集成与普通函数的编译是两条路子，咱们不在这里追下去，您只要别把两者混为一谈就行。

## 副作用的边界：什么时候真的省不掉

咱们一路看下来，后缀倒是一直在退让，您可能要替它问一句：它有守住不动的时候吗？有的。咱们把拷贝构造变成带可观察副作用的，每次的拷贝都往 volatile 数组里写一条记录：

```cpp
#include <cstdio>

// 拷贝构造带可观察副作用:往 volatile 全局写两个数。
// volatile 写按定义不可被优化掉。
volatile unsigned long side_log[64];
volatile int side_count = 0;

struct NoisyIter {
    int* p;

    NoisyIter() : p(nullptr) {}
    explicit NoisyIter(int* q) : p(q) {}

    NoisyIter(const NoisyIter& other) : p(other.p) {
        int idx = side_count;      // volatile 读
        side_count = idx + 1;      // volatile 写:可观察副作用
        side_log[idx] = 0xC0DEul;  // volatile 写:可观察副作用
    }

    NoisyIter& operator++() {
        ++p;
        return *this;
    }

    NoisyIter operator++(int) {
        NoisyIter old(*this);
        ++p;
        return old;
    }
};

__attribute__((noinline, noclone)) void run_prefix(NoisyIter& it) {
    ++it;
}

__attribute__((noinline, noclone)) void run_postfix(NoisyIter& it) {
    it++;
}

int main() {
    int arr[4] = {10, 20, 30, 40};
    NoisyIter it(arr);

    run_prefix(it);
    run_postfix(it);
    run_postfix(it);
    std::printf("side_count=%d p_offset=%ld\n",
                side_count, static_cast<long>(it.p - arr));
    return 0;
}
```

计数用读、写、写三步而不用 `side_count++`，是为了避开 C++20 起 volatile 复合操作被弃用的警告。咱们让 main 调了一次前缀、两次后缀，编译与运行咱们在两个优化级别下各来一遍：

```bash
g++ -Wall -Wextra -O0 a7.cpp -o a7_O0 && ./a7_O0
g++ -Wall -Wextra -O2 a7.cpp -o a7_O2 && ./a7_O2
g++ -O2 -S a7.cpp -o a7_O2.s
```

```text
--- O0 run ---
side_count=2 p_offset=3
--- O2 run ---
side_count=2 p_offset=3
```

两次后缀的调用，两级优化的结果，副作用计数的读数都是 2。咱们看 -O2 的汇编（AT&T 语法）：

```asm
_Z10run_prefixR9NoisyIter:
	addq	$4, (%rdi)
	ret

_Z11run_postfixR9NoisyIter:
	movslq	side_count(%rip), %rax
	movq	(%rdi), %rdx
	leal	1(%rax), %ecx
	movl	%ecx, side_count(%rip)
	leaq	side_log(%rip), %rcx
	movq	$49374, (%rcx,%rax,8)
	leaq	4(%rdx), %rax
	movq	%rax, (%rdi)
	ret
```

（49374 是 0xC0DE 的十进制。）咱们把省与不省分三层看清楚：

- 副作用指令序列一条没少：读 side_count，写回加一的值，往 side_log 写入标记，完整保留。volatile 写按定义不可消除，这部分是拷贝构造必须发生的骨架。
- 纯数据搬运照省：old 对象的指针成员没有进任何 old 槽，`movq (%rdi), %rdx` 只是把旧指针读进寄存器备用，没有写出去。
- return 路径那次拷贝构造，明明也带着副作用，还是被省了。证据就是 side_count 等于 2 而不是 4：倘若每次后缀的两次拷贝都执行，两次调用应该数出 4。

第三层是最反直觉的，咱们把标准的说法请出来对齐。这里叠着两条不同的规则。无副作用的拷贝，按 as-if 的规则就可以省，您反正观察不到差别。有副作用的拷贝还能省，依据换成了拷贝省略（copy elision），那是标准明文放行的例外，as-if 在这里是说不上话的。cppreference 的 copy_elision 页开头写道：

> ```text
> When certain criteria are met, the creation of a class object from a source
> object of the same type (ignoring cv-qualification) can be omitted, even if
> the selected constructor and/or the destructor for the object have side effects.
> ```

咱们把它翻译过来：特定条件被满足的时候，从同类型对象创建类对象的过程可以被省略，即便被选中的构造函数或析构函数有副作用。同页的 Notes 给了它在优化体系里的定位：

> ```text
> Copy elision is the only allowed form of optimization(until C++14) one of the
> two allowed forms of optimization, alongside allocation elision and
> extension,(since C++14) that can change observable side-effects. Because some
> compilers do not perform copy elision in every situation where it is allowed
> (e.g., in debug mode), programs that rely on the side-effects of copy/move
> constructors and destructors are not portable.
> ```

咱们把 Notes 的后半句当作实践提醒：有的编译器在调试模式下不做拷贝省略，依赖拷贝构造副作用的程序是不可移植的。拷贝省略的身份则是这样：C++14 以前唯一被允许改变可观察副作用的优化，C++14 之后则与分配省略的待遇并列，一共两处标准明文的例外。这正好解释了[上一篇](02-class-type-cost.md)里 Iter 实验的复现提示，老编译器在 -O0 下数出来的是 2，您也不用觉得奇怪。

笔者特意核过 copy_elision 的页面原文，as-if 这个词从头到尾是没有出现过的。所以严谨的表述要分成两句：无副作用的拷贝按 as-if 规则可省，有副作用的拷贝能省，靠的是拷贝省略给出的专门许可。刚才的实测正好把两层都演给您看了：副作用的序列留在原地（as-if 是管不着它的，因为删了它可观察行为就变了），数据搬运的消失走的是 as-if 的放行，return 路径那次副作用拷贝的消失，靠的则是拷贝省略的放行。

## 那到底该写哪个

i++ 与 ++i 生成的指令到底一样吗？答案是要分场景的。丢弃值的场景，从 -O0 起就是逐指令一致的。int 的取值场景，-O0 下后缀比前缀多了一条指令（六条对五条），到了 -O2，指令数与成本是一致的，序列因语义不同而不同。类类型在不带副作用的时候，-O2 把旧值的搬运清得干干净净，和逐指令相同之间只隔着一条计数器的距离，而计数器是咱们为观察硬加进去的。类类型在带副作用的时候，副作用的构造必须真实发生，能省掉的只有搬运和 return 路径那次。

所以咱们的判断法就两个问题：表达式的值有人用吗？操作数是类类型吗？int 的场景里无论哪个答案，性能都不构成改写的理由，您写哪个都行，您为它改代码的工夫不如省下来干别的。在类类型的场景里，咱们拿 ++i 当默认习惯，理由是省心：您永远不用回头想旧值副本这件事。至于性能上的理由，int 的实验已经量过了，那几个纳秒是不存在的。cppreference 的 Notes 用的词是 usually more efficient，讲的是典型成本，也从来没有打包票的意思。

最后的活证据送给您：两份 vector 的源码，差别只有循环里 `it++` 与 `++it` 的写法，咱们用 -O2 各编译一份，diff 出来的东西只有一行，还是写着各自文件名的那行 `.file`。下回再有人拿 i++ 慢来劝您，您就请他跑一遍这个实验。
