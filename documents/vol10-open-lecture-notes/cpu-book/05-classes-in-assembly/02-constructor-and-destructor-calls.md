---
title: "你的类在汇编里长什么样：构造与析构的插入点"
chapter: 5
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "构造 call 由编译器插在对象定义的地方，new 是拿内存加跑构造的两步，提前 return 与正常路径在汇合点上共用一条析构 call，RAII 的底气就压在这套机制上"
reading_time_minutes: 14
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 你的类在汇编里长什么样：构造与析构的插入点

环境、工具和贴文的读法都跟[上一篇](01-this-pointer-and-member-offsets.md)相同，您要复跑就照它来。this 认清楚了，咱们接着看构造函数。不少教材的说法是“构造函数在对象创建时自动执行”，这话本身倒是没错，“自动”两个字的说法没讲机制是什么。但是落到汇编里，它就是一次普通的 call，由编译器插在对象定义的地方。咱们写两个对象验这件事，一个住在栈上的，另一个是用 new 放到堆上的：

```cpp
// e2.cc —— 构造函数的插入点：局部对象与 new 对象
struct Point {
    int x;
    int y;

    Point(int a, int b) : x(a), y(b) {}
};

int main() {
    Point p(1, 2);  // (a) 局部对象：栈上分配，直接 call 构造
    volatile int s1 = p.x + p.y;
    (void)s1;

    Point* q = new Point(3, 4);  // (b) 堆对象：operator new 拿内存，再 call 构造
    volatile int s2 = q->x + q->y;
    (void)s2;

    delete q;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e2.cc -o e2.s
```

咱们看 `e2.s` 的 `main` 里，两处插入点是这样的（节选 50 到 66 行的关键两段，栈保护和 volatile 消费的重复装载省略）：

```asm
	leaq	-32(%rbp), %rax      # (a) &p，p 在栈上 rbp-32
	movl	$2, %edx             # b = 2 → rdx
	movl	$1, %esi             # a = 1 → rsi
	movq	%rax, %rdi           # this=&p → rdi
	call	_ZN5PointC1Eii       # ← 编译器插入的构造调用

	movl	$8, %edi             # (b) sizeof(Point)=8 → rdi
	call	_Znwm@PLT            # operator new(8)：拿 8 字节内存，返回值在 rax
	movq	%rax, %rbx           # 新内存地址存进 rbx
	movl	$4, %edx             # b = 4
	movl	$3, %esi             # a = 3
	movq	%rbx, %rdi           # this=新内存 → rdi
	call	_ZN5PointC1Eii       # ← 第二步：对着新内存调构造
```

栈上的那一路，源码里写 `Point p(1, 2);` 的位置，汇编里排的是四条装载加一条 `call _ZN5PointC1Eii`。p 的内存不需要谁去申请，它就是栈上 rbp-32 起的 8 个字节，构造函数领到的 this 指向了那里。您把这一段跟前面 `p.translate` 的调用点并排看就会发现，定义一个对象和调用一个函数在机器层面是同一件事的两种写法。

堆上的那一路就更有看头了。`new Point(3, 4)` 源码里写的是一句话，汇编里翻出来的却是两次 call。第一次的 call 落在 `_Znwm@PLT` 上，`_Znwm` 是 `operator new(unsigned long)` 的修饰名，实参里的 8 就是 sizeof(Point)，它负责从堆上拿回 8 字节的裸内存，地址放在 rax 里，随后被转存进了 rbx。第二次的 `call _ZN5PointC1Eii` 您也看到了，this 换成了刚拿到的这块新内存。所以 new 并不是一条指令，甚至算不上一个完整的动作，它干的第一件事是从堆上拿内存，干的第二件事是对着这块内存跑构造，两步之间那块内存还只是几个没初始化的字节。new 与 malloc 的关系在这个层面看得最清楚：malloc 停在第一步，new 多走了第二步。您想往深处走的话，[卷一的动态内存管理一篇](../../../vol1-fundamentals/ch12/02-new-delete.md)有完整的讲法。

构造函数本体的汇编咱们也贴出来，它的长度很短：

```asm
_ZN5PointC2Eii:
.LFB1:
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
	movl	-12(%rbp), %edx
	movl	%edx, (%rax)         # x = a → this+0
	movq	-8(%rbp), %rax
	movl	-16(%rbp), %edx
	movl	%edx, 4(%rax)        # y = b → this+4
	nop
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

咱们看入参照样是从 rdi、rsi、rdx 落栈的，函数体就干了两件事：往 this+0 写了 a，往 this+4 写了 b。初始化列表在这里没有任何特殊的待遇，它就是两次带偏移的写内存。

符号上有个值得交代的小机关。您可能已经注意到，调用点 call 的是 `_ZN5PointC1Eii`，上面这个函数体的符号却是 `_ZN5PointC2Eii`，末尾的一个字母对不上。这是 ABI 里的两套构造函数符号：C1 的名字是“完整对象构造函数”，负责从头构造一个完整的对象，C2 的名字是“基类子对象构造函数”，负责构造派生类对象肚子里的那一部分。两者的差别要到继承里才真正显出来，咱们这个 Point 没有虚基类，两个符号指向的代码完全一样，GCC 就直接把它们声明成了别名：

```asm
	.weak	_ZN5PointC1Eii
	.set	_ZN5PointC1Eii,_ZN5PointC2Eii
```

所以您在汇编里两个名字都会碰到，指到的是同一份指令。析构函数的 D1、D2 也是同样的安排。

main 里还有一小段容易让读者迷路的代码，咱们提前打个招呼。new 之后 GCC 生成了这么几行：

```asm
	movl	$0, %eax
	movq	%rbx, -40(%rbp)
	testb	%al, %al
	je	.L3
	movl	$8, %esi
	movq	%rbx, %rdi
	call	_ZdlPvm@PLT          # 这条 delete 永远执行不到
.L3:
```

`testb %al, %al` 在测的是 al 是不是零，可它的上一条指令刚刚 `movl $0, %eax` 把 eax 清了零，测试的结果永远是相等，`je .L3` 也就永远跳走了，中间那段 delete 的 call 一辈子执行不到。这是 -O0 下清理路径的生成伪影，编译器没做任何死代码的清理，把一条逻辑上到不了的路原样留在了输出里。您自己反汇编时遇到它，不用怀疑哪里做错了，您也不用在这上面花时间。真正的 delete 是 `_ZdlPvm`，带尺寸的 operator delete，esi 里装的 8 还是那个 sizeof。

## 析构函数：每条退出路径都要路过

析构函数是构造的镜像，但它多一个难题：return 不止一个出口的时候怎么办？语言层面的约定是对象活到作用域结束，那么走在提前 return 的那一路上，析构也躲不掉了。咱们写一个带提前返回的函数来验它：

```cpp
// e3.cc —— 析构函数的插入点：提前 return 路径与正常落到底的路径
struct Tracer {
    int id;

    explicit Tracer(int i) : id(i) {}
    ~Tracer() {
        volatile int sink = id;  // 给析构函数一个可观察的函数体
        (void)sink;
    }
};

int work(int mode) {
    Tracer t(5);

    if (mode == 1) {
        return 10;  // 提前返回路径：也要先析构 t
    }

    volatile int s = t.id;
    (void)s;
    return 20;  // 正常落到底的返回路径：析构 t
}

int main(int argc, char**) {
    volatile int mode = argc - 1;  // 运行时才知道走哪条路
    return work(mode);
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e3.cc -o e3.s
$ g++ -O0 -fno-asynchronous-unwind-tables e3.cc -o e3
$ ./e3; echo "exit=$?"
exit=20
$ ./e3 1; echo "exit=$?"
exit=10
```

咱们无参跑一次，走的都是正常路径，拿到的是返回值 20。带参运行走了提前返回，拿到的是返回值 10。`work` 的汇编译出来了（注释为解读所加）：

```asm
_Z4worki:
.LFB6:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%rbx
	subq	$40, %rsp
	.cfi_offset 3, -24
	movl	%edi, -36(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax
	movl	$5, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # 构造 t
	cmpl	$1, -36(%rbp)
	jne	.L4
	movl	$10, %ebx              # 提前返回：返回值先放 ebx
	jmp	.L5                    # ┐ 跳去公共收尾
.L4:                                   # │
	movl	-32(%rbp), %eax       # │ 正常路径：volatile 消费 t.id
	movl	%eax, -28(%rbp)       # │
	movl	-28(%rbp), %eax       # │
	movl	$20, %ebx              # │ 返回值放 ebx
.L5:                                   # ┘ 两条路都到这
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← 析构 t：两条退出路径唯一的汇合点
	movl	%ebx, %eax
	movq	-24(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L7
	call	__stack_chk_fail@PLT
.L7:
	movq	-8(%rbp), %rbx
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

这里藏着一个跟不少资料对不上的细节，咱们把它说准。有的说法是“每条 return 路径上各有一条析构的 call”，从语义上这么理解没错，但您拿上面的汇编去数，物理的 `call _ZN6TracerD1Ev` 只有一条、落在 `.L5`。GCC 的做法是把两条退出路径合并：提前返回的分支在算好返回值后 `jmp .L5` 汇入，而正常分支一路到底也到 `.L5`，析构调用放在了汇合点上，落成一处 call、两条入边的样子。返回值提前暂存进了 ebx，因为 eax 后面还有别的活，两条路各带各的返回值到汇合点，等析构完了以后再挪进 eax。所以严谨的表述是：每条退出路径的控制流都必经析构调用。语义倒是一点没打折，形态上则共用了一个调用点。

那么多个析构调用点长什么样，咱们去哪里看？得有多个对象才看得出。咱们上嵌套作用域的版本，再给析构函数装上一个运行时的计数器，好让每条路径跑过的析构次数，变成可以打印出来的数字：

```cpp
// e3b.cc —— 析构插入点：嵌套作用域 + 提前 return，运行时可数
#include <cstdio>

volatile int dtor_calls = 0;

struct Tracer {
    int id;

    explicit Tracer(int i) : id(i) {}
    ~Tracer() {
        volatile int keep = id;  // 给析构函数一个可观察的函数体
        (void)keep;
        dtor_calls = dtor_calls + 1;
    }
};

int work(int mode) {
    Tracer outer(1);
    {
        Tracer inner(2);
        if (mode == 1) {
            return 10;  // 提前返回：这条路上要析构 inner，再析构 outer
        }
    }  // 内层作用域结束：析构 inner
    volatile int s = outer.id;
    (void)s;
    return 20;  // 正常路径：只析构 outer
}

int main(int argc, char**) {
    volatile int mode = argc - 1;
    int rc = work(mode);
    std::printf("work()=%d dtor_calls=%d\n", rc, (int)dtor_calls);
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S e3b.cc -o e3b.s
$ g++ -O0 -fno-asynchronous-unwind-tables e3b.cc -o e3b
$ ./e3b; ./e3b 1
work()=20 dtor_calls=2
work()=10 dtor_calls=2
```

咱们看两条路径的计数都是 2。这里有个源码细节要交代一句：计数写的是 `dtor_calls = dtor_calls + 1` 而不是 `++dtor_calls`，因为 C++20 起把 ++ 作用在 volatile 变量上划成了弃用用法，GCC 会报的警告是 `-Wvolatile`，显式赋值才是干净的写法。`work` 的汇编如下（注释为解读所加）：

```asm
_Z4worki:
.LFB8:
	.cfi_startproc
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	pushq	%r12
	pushq	%rbx
	subq	$32, %rsp
	.cfi_offset 12, -24
	.cfi_offset 3, -32
	movl	%edi, -36(%rbp)
	movq	%fs:40, %rax
	movq	%rax, -24(%rbp)
	xorl	%eax, %eax
	leaq	-32(%rbp), %rax
	movl	$1, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # 构造 outer (rbp-32)
	leaq	-28(%rbp), %rax
	movl	$2, %esi
	movq	%rax, %rdi
	call	_ZN6TracerC1Ei         # 构造 inner (rbp-28)
	cmpl	$1, -36(%rbp)
	jne	.L4
	movl	$10, %r12d
	movl	$0, %ebx               # ebx=0：记住走的是提前返回
	jmp	.L5
.L4:
	movl	$1, %ebx               # ebx=1：正常路径
.L5:
	leaq	-28(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← inner 的析构：提前返回与作用域结束在此汇合
	cmpl	$1, %ebx
	jne	.L7                    # 提前返回：跳过 outer 专属语句
	nop
	movl	-32(%rbp), %eax
	movl	%eax, -28(%rbp)
	movl	-28(%rbp), %eax
	movl	$20, %r12d
.L7:
	leaq	-32(%rbp), %rax
	movq	%rax, %rdi
	call	_ZN6TracerD1Ev         # ← outer 的析构：两条路径最终都经过
	movl	%r12d, %eax
	movq	-24(%rbp), %rdx
	subq	%fs:40, %rdx
	je	.L9
	call	__stack_chk_fail@PLT
.L9:
	addq	$32, %rsp
	popq	%rbx
	popq	%r12
	popq	%rbp
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
```

两个对象在这里换出了两条 `call _ZN6TracerD1Ev`，每个对象的一条，正是析构调用点拆成多处的样子。inner 的析构在 `.L5`，提前返回的 `jmp .L5` 和内层作用域直落的路都汇到了它。构造析构的次序您看地址就行：两条构造 call 的操作数是 rbp-32 和 rbp-28，两条析构 call 的操作数是 rbp-28 和 rbp-32，顺序正好是反着的。构造的次序 outer 在前、inner 挨后，析构的次序则反过来，排的是 inner 在前、outer 在后。咱们在 C++ 里学过的“析构顺序与构造相反”，在汇编里就是两个地址的次序问题。这一版里 ebx 真正当上了路径标志，`cmpl $1, %ebx` 加 `jne .L7` 决定要不要跳过 outer 专属的那几句 volatile 消费，提前返回的路从这里岔开，但是两条析构 call 它一条都躲不掉。

RAII 之所以敢把资源安全压在析构上，底气就是这套插入机制：不管您从哪个出口离开作用域，直落的出口也好、提前 return 的出口也好，控制流必须路过编译器安放的析构调用。这是[卷一析构函数与资源管理一篇](../../../vol1-fundamentals/ch06/03-destructors.md)在语言层面讲的约定，今天您在指令层面看到它长什么样了。

构造与析构到这里就验完了，咱们下一篇把虚函数也放进汇编里，那是对象身上的最后一样真家伙。
