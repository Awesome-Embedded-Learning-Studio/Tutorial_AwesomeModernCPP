---
title: "i++ 与 ++i：类类型的拷贝开销"
chapter: 1
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "-O2 下 int 取值场景的指令数与成本拉平之后，手写迭代器把类类型后缀自增的拷贝数出来：语义要求的一次躲不掉，数据搬运在 -O2 下被清干净"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# i++ 与 ++i：类类型的拷贝开销

咱们接着用跟[上一篇](01-int-instructions.md)一模一样的环境往下做。上一篇数出来的差别就两条：取值的时候后缀多一条暂存旧值的指令，丢弃的时候两种写法一条不差。

## 打开 -O2：差异去哪了

现在咱们把优化打开。源码里放的是四个函数，取值与丢弃的场景各放一对，咱们用两次 volatile 存储，既观察了 x 和 i，也把两次存储的前后顺序固定住了：

```cpp
volatile int sink;

// 场景一:自增的值被取走(x 被使用),i 的新值也被使用
void post_val(int i) {
    int x = i++;
    sink = x;
    sink = i;
}

void pre_val(int i) {
    int x = ++i;
    sink = x;
    sink = i;
}

// 场景二:自增的值被丢弃,只关心 i 变了
void post_disc(int i) {
    i++;
    sink = i;
}

void pre_disc(int i) {
    ++i;
    sink = i;
}

int main() {
    post_val(41);
    pre_val(41);
    post_disc(41);
    pre_disc(41);
    return 0;
}
```

```bash
g++ -Wall -Wextra -O2 -fno-asynchronous-unwind-tables -S a3.cpp -o a3.s
```

咱们看场景一（取值、AT&T 语法）：

```asm
_Z8post_vali:
	leal	1(%rdi), %eax
	movl	%edi, sink(%rip)
	movl	%eax, sink(%rip)
	ret

_Z7pre_vali:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	movl	%edi, sink(%rip)
	ret
```

咱们接着看场景二（丢弃、AT&T 语法）：

```asm
_Z9post_disci:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	ret

_Z8pre_disci:
	addl	$1, %edi
	movl	%edi, sink(%rip)
	ret
```

咱们看丢弃的场景，一目了然：`post_disc` 与 `pre_disc` 是逐字节相同的。旧值在数据流里没有了使用者，-O2 就直接按 `i = i + 1` 的思路处理，两种写法合流成了同一个函数。

取值场景咱们要看得细一点。两者的指令各有三条，但序列是不同的。后缀版的 `leal` 把新值另算进 eax，旧值原封不动地留在 edi 里，于是能按语义要求的顺序存旧再存新。前缀版的 `addl` 把 edi 原地改成新值，随后两次存的是同一个值。咱们把成本对齐着看：指令的条数相同，访存的次数也相同，寄存器的用量也一样，两边的成本是一致的。但序列又是没办法变成一样的，因为语义本来就是不同的：x 的值一边是旧的一边是新的，两次 volatile 存储的前后顺序又是语义强制的，编译器也只能在这样的框架里安排自己的寄存器：后缀用 lea 把新值另放进一个寄存器，前缀用 add 在原地改写。差异最后固定下来的就是这样的形态。这跟 -O0 下多出来的指令是同一个根源，只不过成本已经被压到零。

所以咱们要把完全一样的说法限定在丢弃值场景。取值场景的准确说法是：指令数与成本是一致的，而序列是不同的。

优化器为什么能做到这一步？咱们请出的规则叫 as-if：标准只要求程序的可观察行为（volatile 访问的次数与顺序、终端输出这些）不变，中间的过程随编译器安排。丢弃场景里的旧值根本没有可观察的使用者，暂存它就成了纯内部动作，所以能被整个抹掉，两种写法在数据流上就变成了同一件事。取值场景的旧值要写进 sink，它就有可观察的用途，暂存是省不掉的，但形态可以挑最便宜的：不动原寄存器、不回内存、换个寄存器算新值就行了。

还有一个附带的现象：main 在 -O2 下把四次调用全部常量折叠了，四个已知的结果直接变成了六条 volatile 存储，被调用的函数一个都没跑。常量折叠就是编译期把能算完的算完，后面讲计数器的实验时咱们还会再见到它的脾气。

## 传闻的来路：类类型的后缀真的要多干活

int 上的实验没有找到性能差距，那句口诀总得有个来路吧？来路在类类型的身上。C++ 允许咱们重载 `operator++`，语言给后缀定下的语义是返回自增前的旧值，而类类型的旧值必须是一个真实构造出来的对象。cppreference 对内建类型的描述是这样：

> ```text
> The result of postfix increment or decrement is the value obtained by
> applying the lvalue-to-rvalue conversion to expression (before modification).
> The type of the result is the cv-unqualified version of the type of expression.
> ```

咱们把它翻译过来：后缀自增的结果，是修改之前读出来的那个值。同页的 Notes 里还有一句与本篇主题直接相关的：

> ```text
> Because a temporary copy of the object is constructed during post-increment
> and post-decrement, pre-increment or pre-decrement operators are usually
> more efficient in contexts where the returned value is not used.
> ```

因为后缀要构造一个对象的临时副本，在返回值没人用的场合，前缀的效率通常更高。您留意一下人家用的词是 usually，说的是典型的成本、不是标准强制的保证。这句话如今是否属实、实到什么程度，咱们自己测。

## 手写一个迭代器，把拷贝数出来

咱们写一个迭代器样子的类，内部放的是一个裸指针，拷贝构造里挂上了一个静态计数器，统计真实的调用次数。后缀的写法就是教科书那个经典形状，您在教材里见过它：

```cpp
#include <cstdio>

// 迭代器样子的类:内部一个裸指针。
// 拷贝构造带静态计数器,统计真实调用次数。
struct Iter {
    int* p;
    static int copies;

    Iter() : p(nullptr) {}
    explicit Iter(int* q) : p(q) {}
    Iter(const Iter& other) : p(other.p) { ++copies; }

    Iter& operator++() {   // 前缀:返回自增后自身的引用
        ++p;
        return *this;
    }

    Iter operator++(int) { // 后缀:返回自增前的旧值副本
        Iter old(*this);
        ++p;
        return old;
    }
};

int Iter::copies = 0;

// noinline/noclone:保证 -O2 下这两个函数不被内联、不被特化克隆,
// 汇编证据落在独立的函数体里,main 里的计数读取不会被常量折叠掉。
__attribute__((noinline, noclone)) void run_prefix(Iter& it) {
    ++it;
}

__attribute__((noinline, noclone)) void run_postfix(Iter& it) {
    it++;
}

int main() {
    int arr[4] = {10, 20, 30, 40};
    Iter it(arr);

    run_prefix(it);
    std::printf("prefix  : copies=%d\n", Iter::copies);

    int before = Iter::copies;
    run_postfix(it);
    std::printf("postfix : delta_copies=%d\n", Iter::copies - before);
    std::printf("p offset: %ld\n", static_cast<long>(it.p - arr));
    return 0;
}
```

咱们把代码里的几处设计说明一下。`operator++(int)` 里那个 int 扮演的是哑参数，纯粹用来让后缀与前缀的重载在签名上区分开，调用的时候传零，别的用途是没有的。`noinline` 的作用是禁止内联（内联指编译器把被调函数的函数体直接塞进调用处，省掉 call 与返回的开销），`noclone` 则是禁止编译器为特化调用克隆出函数体的，两个属性合在一起的组合保证了证据落在独立的函数体里，也就不会被优化搅进调用方了。咱们跑起来看计数：

```bash
g++ -Wall -Wextra -O0 a4.cpp -o a4_O0 && ./a4_O0
```

```text
prefix  : copies=0
postfix : delta_copies=1
p offset: 2
```

前缀的拷贝是零次，后缀的拷贝是一次，指针经两次调用一共前移了两个元素（offset 为 2），行为是正常的。现在咱们进汇编对质。拷贝构造的样子如下（`_ZN4IterC2ERKS_` 还原后是 `Iter::Iter(const Iter&)`、AT&T 语法）：

```asm
_ZN4IterC2ERKS_:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-16(%rbp), %rax
	movq	(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)
	movl	_ZN4Iter6copiesE(%rip), %eax
	addl	$1, %eax
	movl	%eax, _ZN4Iter6copiesE(%rip)
	nop
	popq	%rbp
	ret
```

前面的一半把源对象的指针搬给新对象，后面的三行是计数器加一。咱们看前缀版的 `operator++()`（AT&T 语法）：

```asm
_ZN4IterppEv:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	(%rax), %rax
	leaq	4(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, (%rax)
	movq	-8(%rbp), %rax
	popq	%rbp
	ret
```

咱们一条 `call` 都看不到，指针加 4（一个 int 的步长），就完事了。接下来咱们看后缀版的 `operator++(int)`（AT&T 语法）：

```asm
_ZN4IterppEi:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movl	%edx, -20(%rbp)
	movq	-16(%rbp), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, %rsi
	movq	%rax, %rdi
	call	_ZN4IterC1ERKS_
	movq	-16(%rbp), %rax
	movq	(%rax), %rax
	leaq	4(%rax), %rdx
	movq	-16(%rbp), %rax
	movq	%rdx, (%rax)
	nop
	movq	-8(%rbp), %rax
	leave
	ret
```

咱们在这里遇到了一个新角色。遇到要返回一个巴掌大的对象的时候，C++ 的调用约定是调用方在栈上开好返回值槽，把槽的地址藏在 rdi 里传进来，真正的 this 反而排在了 rsi，哑参数 int 则占住了 edx。所以 rdi 在这个函数里装的不是参数，它装的就是返回槽的地址。咱们把这个函数体的活计列成表：

| 指令 | 它干的事 |
|---|---|
| `movq %rdi, -8(%rbp)` | 返回槽的地址落栈 |
| `movq %rsi, -16(%rbp)` | this 落栈 |
| `movl %edx, -20(%rbp)` | 哑参数落栈 |
| `movq -16(%rbp), %rdx` 到 `movq %rax, %rdi` 四条 | 排实参：rsi 装 this，rdi 装返回槽 |
| `call _ZN4IterC1ERKS_` | 拷贝构造，构造目标就是返回槽 |
| `movq -16(%rbp), %rax` 起五条 | 读出 this，指针加 4，写回 |
| `movq -8(%rbp), %rax` 加 `leave` 加 `ret` | 返回槽地址放进 rax，收尾返回 |

咱们数下来：后缀的函数体里有且仅有这一次 call，前缀的次数是零，这与运行时的 0 对 1 是互相印证的。

咱们把调用方也看一眼，两个包装函数在 -O0 下的样子如下（AT&T 语法）：

```asm
_Z10run_prefixR4Iter:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	movq	%rdi, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN4IterppEv
	nop
	leave
	ret

_Z11run_postfixR4Iter:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -24(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	leaq	-16(%rbp), %rax
	movq	-24(%rbp), %rcx
	movl	$0, %edx
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZN4IterppEi
	nop
	movq	-8(%rbp), %rax
	subq	%fs:40, %rax
	je	.L9
	call	__stack_chk_fail@PLT
.L9:
	leave
	ret
```

`run_prefix` 这边没什么可说的，它把引用参数转存了一下就转发给前缀版。`run_postfix` 里最扎眼的是 `%fs:40` 相关的几行，它们就是[上一篇](01-int-instructions.md)环境清单里提过的栈金丝雀：函数进来的时候从 `%fs:40` 读一个哨兵值放在栈顶旁，退出前的比对要是发现对不上，就调用 `__stack_chk_fail` 把进程带走了。它的用途是抓栈溢出类的破坏，跟自增的主题是无关的，您在这里见到了真身就够了。您还能在发起调用前看到 `leaq -16(%rbp), %rax`：调用方在栈上开出返回值槽，再把地址放进了 rdi 才发起调用，跟咱们刚才在函数体内看到的 rdi 用法对上了。

教科书说的是两次拷贝，咱们只看到一次，另一次去哪了？按教科书的剧本，`Iter old(*this)` 构造的是一次，`return old;` 拷出去的又是一次。汇编里那次 call 的构造目标直接就是返回槽：`movq -8(%rbp), %rax` 把槽的地址取出来放进 rdi，old 从头到尾住的都是返回槽，return 路径就无事可做了。咱们在这里遇到了 NRVO，它的中文名是命名返回值优化。编译器拿到了标准的许可：返回具名局部对象的时候，对象的构造可以直接落在返回槽里，返回的那次拷贝整个消失。让笔者意外的是，GCC 16 在 -O0 就把它做了，一档优化都没开也照做了，Clang 22 在这里也是一样的。

您想亲眼看两次拷贝？加 `-fno-elide-constructors` 把拷贝省略关掉：

```bash
g++ -O0 -fno-elide-constructors a4.cpp -o a4_O0_noe && ./a4_O0_noe
g++ -O2 -fno-elide-constructors a4.cpp -o a4_O2_noe && ./a4_O2_noe
```

```text
prefix  : copies=0
postfix : delta_copies=2
p offset: 2
```

两级优化下的结果都是 2。默认配置省掉的正是 return 路径那次，暂存旧值的构造则是语义要求的，您想关都关不掉。咱们还有跨编译器的旁证：clang++ 22.1.8 在 -O0 与 -O2 下的结果同样是 1（它对 GCC 专属的 noclone 属性会发一条 unknown attribute 警告，对行为是没有影响的）。

复现这个实验时有两个地方容易翻车，笔者提前交代：

- 别把 `it++;` 直接写进 main。-O2 会连计数器一起常量折叠，printf 直接打出常量，汇编里什么都看不到，您还以为自己做错了。noinline 加 noclone 的包装是证据成立的前提。
- 编译器版本有影响。本机 GCC 16.2.1 与 Clang 22.1.8 在 -O0 都做 NRVO，所以是 1。更老的 GCC 在 -O0 不做 NRVO 时，您会看到 2，那不是做错了，是版本差异，`-fno-elide-constructors` 能把两种拷贝分离开，让您看个明白。

## 同一个类在 -O2 下：搬运消失了

咱们拿同一份代码换 -O2 再跑：

```bash
g++ -Wall -Wextra -O2 a4.cpp -o a4_O2 && ./a4_O2
```

```text
prefix  : copies=0
postfix : delta_copies=1
p offset: 2
```

咱们拿到的计数还是 1，但汇编的样子已经面目全非（AT&T 语法）：

```asm
_Z10run_prefixR4Iter:
	addq	$4, (%rdi)
	ret

_Z11run_postfixR4Iter:
	addl	$1, _ZN4Iter6copiesE(%rip)
	addq	$4, (%rdi)
	ret
```

咱们看 `run_prefix`：整个函数就两条指令，一条是把指针加上 4 的，另一条是负责返回的。`run_postfix` 的指令是三条，多出的 `addl $1, _ZN4Iter6copiesE(%rip)` 是计数器自增。它留下来的原因值得琢磨：main 随后打印的就是 copies，而计数器属于可观察的状态，所以自增是必须发生的。而真正的旧值搬运，也就是把 this 指针成员的旧值写进 old 对象的那几步，如今一条都不剩了。

所以咱们可以说，对一个不带副作用的正常迭代器类，后缀在 -O2 下与前缀的编译结果逐指令相同（咱们这个例子里多出的一条，是计数器自己挣来的，不是后缀语义挣来的）。运行时计数 delta 等于 1 是抽象机语义层面的要求，全由那多出的一条指令来兑现，并没有真的发生任何数据搬运。抽象机是标准虚构出来的机器模型，程序在它上面的行为就是应当发生的行为，编译器的全部优化都得保证可观察的结果与它一致。

优化后的两条指令咱们看过了，优化的起点也值得一走。下面的执行器是 -O0 形态的对照场景：iter_postinc 先把旧值对象拷贝到栈上，这一步要真的 call 进拷贝构造，copies 计数加一，然后才自增返回；iter_preinc 没有任何 call，两条加法完事。您各走一遍数一数 call 的次数，拷贝构造那份价目表就摆在眼前，优化省掉的正是它。符号名去掉了修饰，计数器沿用文章里的 copies。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/class_postfix_cost.json" />
