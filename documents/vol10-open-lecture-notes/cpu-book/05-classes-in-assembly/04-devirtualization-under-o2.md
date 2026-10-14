---
title: "你的类在汇编里长什么样：-O2 的去虚拟化"
chapter: 5
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "看得穿局部对象的类型，-O2 就把虚调用改成目标写死的直接 call，看不穿就跟候选比对赌一把，兜底的间接 call 是编译器承认自己吃不透的证据"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 你的类在汇编里长什么样：-O2 的去虚拟化

咱们用的环境跟[第一篇](01-this-pointer-and-member-offsets.md)还是同一套。三步的虚调用看着就贵：内存多读了两次，call 还拐了一道弯。编译器也这么觉得，所以 -O2 有一类优化专门冲它来，它的名字叫去虚拟化，把间接的虚调用改回直接调用，运气好时连 vptr 都不用装了。咱们能不能做去虚拟化，全看编译器看不看得穿对象的动态类型。咱们设计一对函数，把两种处境都造了出来：

```cpp
// e7.cc —— -O2 去虚拟化：编译器看得穿类型 vs 看不穿
#include <cstdio>

struct B {
    virtual int f();
};
struct D : B {
    int f() override;
};

volatile int magic_b = 111;  // volatile：返回值无法被跨过程折叠成常量
volatile int magic_d = 222;
volatile int sink = 0;

// noinline：把「去虚拟化」与「内联」两件事分开，保证汇编里还看得见 call 指令
__attribute__((noinline)) int B::f() { return magic_b; }
__attribute__((noinline)) int D::f() { return magic_d; }

__attribute__((noinline)) int seen_through() {
    D d;        // 局部对象，类型摆在编译器眼前
    B* p = &d;
    int r = p->f();  // -O2 预期：直接 call D::f
    sink = r;
    return r;
}

B* volatile whom = nullptr;  // 运行时才知道指向谁

__attribute__((noinline)) int unknown_target() {
    B* p = whom;     // 从 volatile 内存里读出来的指针
    int r = p->f();  // -O2 预期：仍是间接 call
    sink = r;
    return r;
}

int main(int argc, char**) {
    B b;
    D d;
    whom = (argc > 1) ? (B*)&b : (B*)&d;  // 运行时才装定

    std::printf("seen_through()   -> %d\n", seen_through());
    std::printf("unknown_target() -> %d\n", unknown_target());
    return 0;
}
```

两个防干扰的设计得交代一下。`f()` 标了 noinline，咱们用它把去虚拟化和内联分开，不然的话函数体一展开，call 就没了，咱们想看的正是 call 长什么样。另两个 magic 变量挂了 volatile，所以返回值没法被跨过程折叠成常量，不然整个调用都可能被编译器算没了。咱们看得穿的 `seen_through` 在 -O0 下还是标准三步（节选，栈保护的尾部省略）：

```asm
_Z12seen_throughv:
.LFB4:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movq	%fs:40, %rax
	movq	%rax, -8(%rbp)
	xorl	%eax, %eax
	leaq	16+_ZTV1D(%rip), %rax    # 构造 d：写 vptr（内联的隐式构造）
	movq	%rax, -24(%rbp)
	leaq	-24(%rbp), %rax
	movq	%rax, -16(%rbp)          # p = &d
	movq	-16(%rbp), %rax
	movq	(%rax), %rax             # 装 vptr
	movq	(%rax), %rdx             # 取槽位
	movq	-16(%rbp), %rax
	movq	%rax, %rdi               # this → rdi
	call	*%rdx                    # -O0：间接 call，三步齐全
	movl	%eax, -28(%rbp)
	movl	-28(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	-28(%rbp), %eax
	...
```

咱们给同一个函数换 -O2 编，画风就突变了：

```asm
_Z12seen_throughv:
.LFB17:
	.cfi_startproc
	subq	$24, %rsp
	.cfi_def_cfa_offset 32
	movq	%fs:40, %rdi
	movq	%rdi, 8(%rsp)
	movq	%rsp, %rdi
	call	_ZN1D1fEv               # -O2：直接 call D::f，连 vptr 都不用装
	movl	%eax, sink(%rip)
	movq	8(%rsp), %rdx
	subq	%fs:40, %rdx
	jne	.L7
	addq	$24, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L7:
	.cfi_restore_state
	call	__stack_chk_fail@PLT
	.cfi_endproc
```

咱们看 `call *%rdx` 变成了 `call _ZN1D1fEv`，目标直接写死成 D::f 的符号，装 vptr 和取槽位两条指令整个消失了。道理不玄：d 是本函数的局部对象，p 就是指向它的指针，往后没有任何地方能把它换成别的类型，编译器证明出 p 的动态类型必是 D，虚表查出来的地址必然是 D::f，那还查什么表呢。运行输出咱们到下一节贴，那里 `seen_through()` 两次报的都是 222。这就是去虚拟化的全部意思：能被去虚拟化的虚调用跟普通调用没差，间接跳转的代价只在编译器无法确定类型的时候才发生。

## 看不穿的时候，GCC 会赌一把

看不穿的那一半，故事就没有这么直了，这也是本篇跟不少资料分岔的地方。咱们从 -O0 的 `unknown_target` 看起：

```asm
_Z14unknown_targetv:
.LFB11:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$16, %rsp
	movq	whom(%rip), %rax        # p = whom（运行时才知）
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	movq	(%rax), %rax            # 装 vptr
	movq	(%rax), %rdx            # 取槽位
	movq	-8(%rbp), %rax
	movq	%rax, %rdi              # this → rdi
	call	*%rdx                   # 间接 call
	movl	%eax, -12(%rbp)
	movl	-12(%rbp), %eax
	movl	%eax, sink(%rip)
	movl	-12(%rbp), %eax
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

p 来自的是 volatile 变量 whom，编译期里谁也不知道它指向的是 B 还是 D，三步也就老老实实地排着。您要是以为 -O2 下它还是这三步，拿本机的 GCC 编一次就会愣住，实际输出是这样的：

```asm
_Z14unknown_targetv:
.LFB24:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	whom(%rip), %rdi        # p = whom
	leaq	_ZN1D1fEv(%rip), %rdx   # 候选一：D::f
	movq	(%rdi), %rax            # 装 vptr
	movq	(%rax), %rax            # 取槽位（真正会调的地址）
	cmpq	%rdx, %rax
	je	.L13                    # 是 D::f → 走 .L13 的直接 call
	leaq	_ZN1B1fEv(%rip), %rdx   # 候选二：B::f
	cmpq	%rdx, %rax
	jne	.L11                    # 都不是 → 走 .L11 的间接 call 兜底
	call	_ZN1B1fEv
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.p2align 4,,10
	.p2align 3
.L11:
	.cfi_restore_state
	call	*%rax                   # 间接 call 兜底：编译器不认识的任何目标从这走
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.p2align 4,,10
	.p2align 3
.L13:
	.cfi_restore_state
	call	_ZN1D1fEv
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
```

咱们顺着指令读：vptr 照装、真实槽位照取，但是拿到地址后并没有直接 call 它，而是拿它跟两个候选挨个比：头一个比的是 D::f，命中的话就跳 `.L13` 走直接 call，第二个比的是 B::f，命中就落进紧挨着的直接 call，都不中的时候才跳 `.L11`，那里守着一条 `call *%rax` 的间接调用兜底。这套做法的名字叫投机去虚拟化，白话就是赌一把：编译器从本编译单元的类型继承图里看到，B 的 f 在眼前只有 B::f 和 D::f 两个候选，运行时大概率是这俩里的一个，所以它就比一比，赌中了就赚一条目标写死的直接 call。赌本倒是不大，比对失败的话就落回兜底，行为跟原来的分毫不差。运行输出的佐证就在下面，咱们谁都没受伤：

```text
$ g++ -O2 e7.cc -o e7 && ./e7 && ./e7 extra
seen_through()   -> 222
unknown_target() -> 222
seen_through()   -> 222
unknown_target() -> 111
```

咱们无参跑的时候 whom 指向 d，unknown_target 报的是 222。带参跑的时候 whom 指向 b，报的则是 111。上面的输出跑在 -O2 默认版上。咱们前面贴过 -O0 版的汇编，它经 vptr 取的是真实槽位，call 的目标终究由对象里的表决定，所以 -O0 与 -O2 的行为一致，咱们赌赢赌输，拿到手的结果都一样。

咱们最想请您看一眼的，是 `.L11` 里兜底的 `call *%rax` 还在。编译器明明是肯赌的，为什么不留一条直接 call 硬赌到底？因为它知道自己吃不透：这个编译单元之外，可能还有别的 .cc 文件定义了 B 的更多派生类，whom 运行时完全可能指向那些它没见过的类型，槽位里取出的会是两个候选之外的地址。兜底间接 call 的存在，就是编译器承认自己吃不透的最硬证据，这个分支平时冷冷清清的，一有陌生类型到来就是唯一的活路。顺带把范围也说清：这套投机是在单个编译单元内部做的，跨单元的更多候选它一概不知情。

想看不含投机的干净版，咱们加上 `-fno-devirtualize-speculatively`：

```text
$ g++ -O2 -fno-devirtualize-speculatively -S e7.cc -o e7-O2-nospec.s
```

```asm
_Z14unknown_targetv:
.LFB24:
	.cfi_startproc
	subq	$8, %rsp
	.cfi_def_cfa_offset 16
	movq	whom(%rip), %rdi        # p = whom
	movq	(%rdi), %rax            # 装 vptr
	call	*(%rax)                 # 取槽位与间接 call 合并：call 虚表 [0] 槽
	movl	%eax, sink(%rip)
	addq	$8, %rsp
	.cfi_def_cfa_offset 8
	ret
	.cfi_endproc
```

咱们把投机一关，比对阶梯整个就没了，输出收敛成了教科书形态。这里有个容易看漏的细节得讲明白：末尾的 `call *(%rax)`，它的操作数是内存寻址，它把取槽位和 call 两步合并成了一条指令，跟 -O0 那套取到 rdx 再 `call *%rdx` 的做法相比少了一步。所以严格数下来，-O0 的虚调用是三条可指认的指令，也就是咱们说的装 vptr、取槽位、间接 call，关掉投机的 -O2 压缩成了两条，即装 vptr 加合并版的间接 call，指令数确实少了，但是“目标在运行时从内存取”的本质一步都没让。您要是想拿指令数衡量虚调用的开销，两个口径的数法就要分开，拿 -O0 的三步去跟 -O2 默认输出的比对阶梯比，比出来的只会是困惑。

虚函数的性能话题在[性能卷的虚函数与去虚拟化一篇](../../../vol6-performance/ch06-cpp-abstraction-cost/06-01-virtual-devirtualization.md)有完整的实测视角，那边的说法跟咱们今天在指令层面看到的一致：能被去虚拟化的虚调用，开销已经跟普通调用没有了差别，真正的成本只在类型真看不穿的地方。

## 对象还原成内存与代码

咱们走完这一路，把开头立的几个问题各收一句。this 是成员函数参数表第一位上的指针，x86-64 的 Linux 上走 rdi，成员函数与显式收对象指针的自由函数，汇编归一化之后的 diff 是空的。构造与析构是编译器插在定义点与退出路径上的普通 call，new 是从堆上拿内存、再对着内存跑构造的两步，每条退出路径的控制流必经析构，RAII 的底气就在这里。虚函数是对象头部的 vptr 加一张类级共享的函数指针表加一次三步的间接调用，-O2 在看得穿类型的时候就整个拆掉，看不穿也会赌一把常见的候选，留着兜底的间接 call 才是它诚实的部分。

想再往深走的读者，去处咱们指几个。类与对象的语言层讲法在[卷一类的定义](../../../vol1-fundamentals/ch06/01-class-basics.md)，this 指针的常规用法在[同卷的 this 与链式调用](../../../vol1-fundamentals/ch06/06-this-and-cascading.md)，虚函数与多态的系统讲法在[虚函数与多态一篇](../../../vol1-fundamentals/ch08/03-virtual-functions.md)。咱们这个系列里，02 与 03 把普通函数调用的骨架和传参讲完了，咱们这几篇把对象语法接了上去，下一步咱们去内存那一侧看页表。

收尾把最省事的自证留给您：拿 e7.cc 编三个版本，-O0 的、-O2 的、-O2 加 `-fno-devirtualize-speculatively` 的，咱们只看 `unknown_target` 一个函数。第一回您会看到三步齐全的间接 call，第二回是比对阶梯加兜底的形态，第三回是两条指令的干净版。等您把三个版本摆开，编译器对同一个虚调用的三种态度就都在眼前了。
