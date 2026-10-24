---
title: "多值返回：pair 打包与三条通道的全景"
chapter: 8
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "std::pair<int, int> 的返回只领一个 rax，计数实验数出的 2 次拷贝全部落在 pair 构造函数内部，结构化绑定接住返回值不多拷一次，最后把三条物理通道与一层拷贝省略收拢起来"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 多值返回：pair 打包与三条通道的全景

上一篇把拷贝省略追完了，咱们回到物理通道，看一个天天在用的形态：多值返回。咱们用的环境与[第一篇](01-rax-family-and-register-pairs.md)相同，您要复跑就照它来。

## 多值返回：pair 的打包与结构化绑定

C++ 没有多返回值的语法，咱们习惯拿 std::pair 打包两个结果。pair 在 ABI 的眼里就是普通结构体，落回的是同一条归类，所以本篇这一段算判据的复习。咱们起手的预判是，pair 的成员有两个，返回走的应该是 rax 加 rdx。实测把咱们押输了。

```cpp
// std::pair<int,int>(8 字节,平凡可拷贝)的返回寄存器打包 + 结构化绑定接收
#include <utility>

std::pair<int, int> make_ipair(int a, int b) __attribute__((noinline));
std::pair<int, int> make_ipair(int a, int b) { return {a, b}; }

int main() {
    auto [x, y] = make_ipair(3, 4);        // 结构化绑定接收
    volatile int sink = x + y;             // 消费,防删
    return sink == 7 ? 0 : 1;
}
```

### 与预判相反：pair 只领一个寄存器

`std::pair<int, int>` 的体量是八字节，也就是一个 eightbyte 的体量，归的是 INTEGER 类，按判据只领一个寄存器的名额。编译用的命令是 `g++ -O0 -S -fno-asynchronous-unwind-tables r6a_ipair.cpp -o r6a_ipair.s`。咱们把 -O0 的函数体贴出来，省略了 `.cfi_*` 行与栈保护金丝雀(canary)的设置段，金丝雀校验的尾声原样保留：

```asm
_Z10make_ipairii:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	leaq	-24(%rbp), %rdx
	leaq	-20(%rbp), %rcx
	leaq	-16(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi
	call	_ZNSt4pairIiiEC1IRiS2_EEOT_OT0_
	movq	-16(%rbp), %rax        ; 整个 pair 一次装进 rax
	movq	-8(%rbp), %rdx         ; (栈保护 canary,与返回无关)
	subq	%fs:40, %rdx
	je	.L3
	call	__stack_chk_fail@PLT
.L3:
	leave
	ret
```

那个 call 是 -O0 不内联库函数的形态，转发构造把两个 int 落进栈上的 pair，随后的一条 `movq -16(%rbp), %rax` 把它一次装走。-O2 的版本更干净，咱们把同一份 r6a_ipair.cpp 换成 -O2 再取汇编，输出的函数体只用了三条指令：

```asm
_Z10make_ipairii:
	salq	$32, %rsi
	movl	%edi, %eax
	orq	%rsi, %rax
	ret
```

`salq $32, %rsi` 把后一个 int 移到了高半，`movl %edi, %eax` 把前一个 int 放进了低半，三十二位的写入顺手把 rax 高半清了零，末尾的 `orq` 完成拼合，两个 int 就住进了同一个 rax。分不分段的判据，从头到尾只认八字节的段数，不认成员的个数，也不认类型的名字是不是 pair。真要找 rax 加 rdx 的正例，第一篇里的 Pair8 就是现成的，两段 INTEGER 换来了两个寄存器。想当然把 pair 记成分段返回的预判，这一篇已经实测翻过车了，您记笔记的时候留意一下。咱们运行编出来的 `./r6a`，退出码报的是 0。

main 侧用的是结构化绑定接收，C++17 的写法，`auto [x, y]` 会引入一个隐藏的变量承接整个返回值，x 与 y 绑定到它的两个成员上。咱们把 -O0 的接收段贴出来，`.cfi_*`、金丝雀设置段与比较分支的尾声省略：

```asm
	movl	$4, %esi
	movl	$3, %edi
	call	_Z10make_ipairii
	movq	%rax, -16(%rbp)       ; 隐藏变量一次落地,无第二笔搬运
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_ZSt3getILm0EiiEONSt13tuple_elementIXT_ESt4pairIT0_T1_EE4typeEOS4_
	movq	%rax, -32(%rbp)       ; std::get<0> 返回成员地址(引用)
	leaq	-16(%rbp), %rax
	movq	%rax, %rdi
	call	_ZSt3getILm1EiiEONSt13tuple_elementIXT_ESt4pairIT0_T1_EE4typeEOS4_
	movq	%rax, -24(%rbp)
	movq	-32(%rbp), %rax
	movl	(%rax), %edx          ; x 经地址读
	movq	-24(%rbp), %rax
	movl	(%rax), %eax          ; y 经地址读
	addl	%edx, %eax
	movl	%eax, -36(%rbp)
```

头一笔 `movq %rax, -16(%rbp)` 让隐藏变量一次落地，绑定本身没有第二笔的搬运。后面的两个 call 来自 `std::get<0>` 与 `std::get<1>`，它们经地址访问成员。您可别拿这些 call 说绑定有开销，-O0 下的编译器连库函数都不内联，它们是工具链的形态，不是绑定的语义，开销的判定咱们用计数来做。

### 绑定接收的计数：加号来自 pair 内部

计数实验咱们把成员换成带计数器的 Counter，从函数的内部一路数到绑定的接收：

```cpp
// 结构化绑定接收处的拷贝计数(语义证据)
#include <utility>
#include <cstdio>

struct Counter {
    int v;
    static long copies;
    Counter(int v_) : v(v_) {}
    Counter(const Counter& o) : v(o.v) { ++copies; }
};
long Counter::copies = 0;

using CPair = std::pair<Counter, Counter>;

CPair make_cpair(int a, int b) __attribute__((noinline));
CPair make_cpair(int a, int b) { return {Counter{a}, Counter{b}}; }

int main() {
    auto [x, y] = make_cpair(1, 2);   // 隐藏变量 + x/y 按引用绑定到成员
    volatile int sink = x.v + y.v;    // 只读成员,不拷贝
    std::printf("member-copies = %ld, x = %d, y = %d\n", Counter::copies, x.v, y.v);
    return sink == 3 ? 0 : 1;
}
```

咱们把四组编译与运行逐字贴出来，C++14 那组的结构化绑定扩展警告也原样保留：

```text
$ g++ -O0 r6b_binding.cpp -o r6b_O0 && ./r6b_O0
member-copies = 2, x = 1, y = 2
$ g++ -O0 -fno-elide-constructors r6b_binding.cpp -o r6b_O0_noelide && ./r6b_O0_noelide
member-copies = 2, x = 1, y = 2
$ g++ -O2 -fno-elide-constructors r6b_binding.cpp -o r6b_O2_noelide && ./r6b_O2_noelide
member-copies = 2, x = 1, y = 2
$ g++ -std=c++14 -O0 -fno-elide-constructors r6b_binding.cpp -o r6b_cpp14_O0_noelide && ./r6b_cpp14_O0_noelide
r6b_binding.cpp: In function ‘int main()’:
r6b_binding.cpp:20:10: warning: structured bindings only available with ‘-std=c++17’ or ‘-std=gnu++17’ [-Wc++17-extensions]
   20 |     auto [x, y] = make_cpair(1, 2);   // 隐藏变量 + x/y 按引用绑定到成员
      |          ^
member-copies = 4, x = 1, y = 2
```

默认口径下的 2 是不是返回路径的锅？咱们用 objdump 把拷贝点定位出来，再看看两个函数自己的身上有没有拷贝调用：

```text
$ objdump -d r6b_O0 | grep -B4 'call.*_ZN7CounterC1ERKS_'
    133a:	48 89 55 f8          	mov    %rdx,-0x8(%rbp)
    133e:	48 8b 55 f8          	mov    -0x8(%rbp),%rdx
    1342:	48 89 d6             	mov    %rdx,%rsi
    1345:	48 89 c7             	mov    %rax,%rdi
    1348:	e8 2f ff ff ff       	call   127c <_ZN7CounterC1ERKS_>
--
    1359:	48 89 45 f0          	mov    %rax,-0x10(%rbp)
    135d:	48 8b 45 f0          	mov    -0x10(%rbp),%rax
    1361:	48 89 c6             	mov    %rdx,%rsi
    1364:	48 89 d7             	mov    %rdx,%rdi
    1367:	e8 10 ff ff ff       	call   127c <_ZN7CounterC1ERKS_>

$ nm r6b_O0 | grep -E 'CounterC1ERKS_|St4pair.*C[12]'
000000000000127c W _ZN7CounterC1ERKS_
000000000000131e W _ZNSt4pairI7CounterS0_EC1IS0_S0_EEOT_OT0_   <- pair 转发构造函数起址 0x131e

$ objdump -d r6b_O0 | awk '/<_Z11make_cpairii>:/,/^$/' | grep -c 'CounterC1ERKS_'
0
$ objdump -d r6b_O0 | awk '/<main>:/,/^$/' | grep -c 'CounterC1ERKS_'
0
```

咱们看到两处拷贝调用的地址 0x1348 与 0x1367，都落在 0x131e 起始的 `std::pair<Counter,Counter>` 转发构造函数体内：临时 Counter 交给形参是强制省略的，形参到成员的这一拷贝不是，每个成员各来了一次，凑成了 2。make_cpair 与 main 自己的函数体里，拷贝构造的 call 数都是 0。所以裸数 2 的归属是 pair 的构造函数，返回、接收、绑定三个环节身上都是清白的。C++14 禁省略版多出的那 2 次也定了位：main 在调用之后用 pair 的移动构造初始化隐藏变量，而 Counter 没有移动构造，用户拷贝构造把隐式移动抑制了。pair 移动构造的成员初始化于是回落到拷贝。两次都在那个移动构造的体内。老方言接收侧的这笔开销，C++17 强制省略之后就直接归了零。

咱们再做一组同基线的对照，把 r6b 的 main 换成下面两段、存成 r6c_ab.cpp，绑定接收与具名变量接收在同一份程序里前后脚各测了一遍：

```cpp
    long before = Counter::copies;
    auto [x, y] = make_cpair(1, 2);              // 结构化绑定接收
    volatile int s1 = x.v + y.v;
    std::printf("binding   reception: +%ld\n", Counter::copies - before);

    before = Counter::copies;
    CPair p = make_cpair(3, 4);                  // 具名变量接收
    volatile int s2 = p.first.v + p.second.v;
    std::printf("namedvar reception: +%ld\n", Counter::copies - before);
```

三组配置对应输出里的 ./r6c_O0、./r6c_O0_noelide 与 ./r6c_O2，咱们看到的输出是这样的：

```text
== -O0 默认 ==        ./r6c_O0
binding   reception: +2
namedvar reception: +2
== -O0 -fno-elide ==  ./r6c_O0_noelide
binding   reception: +2
namedvar reception: +2
== -O2 默认 ==        ./r6c_O2
binding   reception: +2
namedvar reception: +2
(exit=0)
```

三种配置下读到的数字里，两种接收方式的增量逐字相同，都报的是 +2，而这个 +2 的来源咱们刚才已经定位到 pair 构造的内部。所以结构化绑定接住 pair 的返回，额外的拷贝是零，绑定干的只是给成员起名字。tuple 的道理相同，它也是普通的类类型，走的也是同一条八字节归类，字段一多、体量就容易超过两段的限制，通道自然就换成了 sret。这一句是判据的推论，咱们没有为 tuple 单独做实验，您想验证，拿今天的这套办法量一遍就行。

## 线头接起来：三条通道加一层省略

咱们把回家路收拢。体量与类别定了三条物理通道：一个八字节段的，数据走的是 rax，bool 窄到 al 的一个字节，int 用的是 eax，指针占的是整宽。两段以内的聚合体，按段领的是 rax 与 rdx，整数与浮点各回各的池。超过两段的，或者类型对调用非平凡的，交出去的就换成 rdi 里的隐藏指针，数据直接写进调用方的栈槽，rax 还的是地址。C++ 在通道之上叠的省略分两层：NRVO 把具名局部的构造挪进返回槽，C++17 起纯右值的接收侧省略干脆写进语法，连省略这个名字都显得多余了。

这一篇收的线，往前接的是 01 与 03。[01 里教科书说两次、汇编见一次的那桩怪事](../01-prefix-vs-postfix-increment/02-class-type-cost.md)，咱们如今有了四件套的答案，帧里没有 p 的座位，构造的 this 就是 sret 地址，return p 编译成了一条 movq，计数报的是 0。[03 演过机制的隐藏首参数](../03-arguments-in-registers/02-stack-args-and-return-values.md)，这一篇请出了规则原文与返回侧的名册，还补了 03 没讲过的非平凡例外，顺带把 sret 这个名字正式介绍给您。更远处的线头也能接上：02 的 call 与 ret 是这些回家路的载体，05 的构造函数在这里把 this 落在了调用方的栈上，07 的 volatile sink 是咱们全系列赖以观察的手段。您要是想顺着性能的方向继续走，vol6 有一篇 [RVO、NRVO 与 move 的真实成本](../../../vol6-performance/ch06-cpp-abstraction-cost/06-05-rvo-move.md)，拿会计数的 Tracked 类型量了按值返回的成本，还把 `return std::move(局部)` 这个反模式的真实代价数了出来，与本篇的计数法一脉相承。

最后摆一件最小的证据，算这篇的落点。-O2 之下的版本里，make_ipair 整个函数体就只剩 `salq`、`movl`、`orq` 的三条指令，两个 int 当场拼进了同一个 rax。返回值的回家路，编译器替您安排到了什么程度，它们就是编译器的态度。

这两个版本也搬进了执行器。下面的场景把 -O0 与 -O2 的 make_ipair 放进同一个 main：O0 版先在栈上构造 pair，再用一条 movq 把整个 8 字节装进 rax；O2 版三条指令现场打包，连栈都不碰。您单步走完 O2 版再抬头看 rax，低 32 位是 first，高 32 位是 second，shr 一下就能拆开。场景去掉了文章里的金丝雀段寻址，其余指令一一对应。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/pair_in_rax.json" />
