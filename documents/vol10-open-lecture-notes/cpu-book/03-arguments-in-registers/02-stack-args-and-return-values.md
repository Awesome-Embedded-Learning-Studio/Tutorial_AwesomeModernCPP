---
title: "第 7 个参数上栈与返回值的两条通道"
chapter: 3
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "实测第 7 个整数参数怎么上栈、多个栈参数为什么从右往左排，用 gdb 两场会话抓出调用方写入与被调方读取的同一个地址，再把返回值的两条通道验完，小值进 rax，超过 16 字节的大值走隐藏指针"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 第 7 个参数上栈与返回值的两条通道

上一篇咱们数到第 6 个参数，整数寄存器的名额正好用光，第 7 个参数来的时候会发生什么、返回值又从哪条路回来，咱们这一篇接着看。咱们用的环境与[上一篇](01-integer-args-register-allocation.md)相同，您要复跑就照它来。

## 第 7 个参数上了栈

答案咱们已经能猜到：转去栈上。咱们把参数加到七个：

```cpp
// c3_seven_int.cpp
__attribute__((noinline)) int sum7(int a, int b, int c, int d, int e, int f, int g) {
    return a + b + c + d + e + f + g;
}

volatile int sink = 0;

int main() {
    int r = sum7(1, 2, 3, 4, 5, 6, 7);
    sink = r;
    return 0;
}
```

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -S c3_seven_int.cpp -o c3_seven_int.s
```

```asm
_Z4sum7iiiiiii:
	pushq	%rbp
	movq	%rsp, %rbp
	movl	%edi, -4(%rbp)
	movl	%esi, -8(%rbp)
	movl	%edx, -12(%rbp)
	movl	%ecx, -16(%rbp)
	movl	%r8d, -20(%rbp)
	movl	%r9d, -24(%rbp)
	...（前六项的读回累加，略）
	movl	16(%rbp), %eax
	addl	%edx, %eax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	pushq	$7
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum7iiiiiii
	addq	$8, %rsp
	movl	%eax, -4(%rbp)
	...（写回 sink，略）
	leave
	ret
```

咱们看 main 的动作序列，多出了三件新东西。`pushq $7` 把第 7 个实参压进了栈，然后才轮到六个寄存器的装货与 `call`，返回以后还跟着一条清栈的 `addq $8, %rsp`。被调方那边前六个参数走的还是寄存器，照旧 spill 进自己的栈槽，第 7 个参数 g 却是直接从 `movl 16(%rbp), %eax` 取的，寻址用的是 rbp 的正向偏移，跟局部变量的负向偏移正好分居两侧。

咱们再看 `pushq $7` 这个容易看漏的细节：q 后缀标明的是 64 位压栈。g 的类型是 int、宽度只占 4 字节，但它在栈上占的格子是完整的 8 字节，被调方用 `movl` 取的时候只消费低 32 位。栈上的参数也是按每八字节一段来安排的，参数的类型再小也得把一整格住满，这个安排到下一篇讲结构体的部分还会再遇到。

偏移 16 是怎么来的？咱们拿上一个主题的栈帧知识推一遍。call 落地的那个瞬间，rsp 指着的是返回地址。被调方进门的头一件事是 `pushq %rbp`，栈顶又往低了挪了 8 字节。随后的 `movq %rsp, %rbp` 把帧基定下来。于是 rbp+0 住着的是保存的旧 rbp，rbp+8 住着的是返回地址，rbp+16 起的位置是栈参数区的第一格，第 7 个参数住的正是那里。

咱们在这里给参数区的栈布局留一个图位，图待补：内容为 sum7 调用现场从高地址往低地址的排布，也就是栈参数 g、返回地址、保存的 rbp、被调方局部变量区各自的相对位置，后续由本站的绘图管线补上。

`addq $8, %rsp` 也值得咱们记下：栈参数的清理归调用方，call 一返回 main 就把那 8 字节还了回去，被调方就不用管这件事了。这跟某些老约定里被调方清栈的做法不一样，System V 这套选了让调用方动手。

栈上眼下只有孤零零的第 7 个参数，还看不出多个栈参数的排布规律，咱们补一个 8 参数的版本。源码就是在 sum7 的基础上再添一个 int h、实参加个 8，其余的部分与 sum7 同构。它 main 里的关键段是这样的：

```asm
main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$16, %rsp
	pushq	$8
	pushq	$7
	movl	$6, %r9d
	movl	$5, %r8d
	movl	$4, %ecx
	movl	$3, %edx
	movl	$2, %esi
	movl	$1, %edi
	call	_Z4sum8iiiiiiii
	addq	$16, %rsp
```

咱们实测到的压入次序是 `pushq $8` 排在前头、`pushq $7` 跟在后面，也就是说压栈是从最靠右的参数开始的。栈是往低地址长的，压得早的反而住高地址，于是被调方读到的偏移是 g 在 `16(%rbp)`、h 在 `24(%rbp)`，sum8 被调方读栈参数的两行正是 `movl 16(%rbp), %eax` 与 `movl 24(%rbp), %eax`。靠右的参数地址更高，这就是从右往左的布局。ABI 文档在这一点的原话是：

```text
Once registers are assigned, the arguments passed in memory are
pushed on the stack in reversed (right-to-left) order.
```

咱们把它翻译过来：寄存器分配完之后，走内存的参数按反序（从右到左）压上栈。文档还配了条脚注解释动机：参数按从右往左排好之后，最靠左的栈参数地址在编译期就能算准，处理可变参数（printf 一类参数个数不定的函数）的时候取参才方便。这里有一处措辞要抠准：ABI 真正约束的是参数在栈上的最终布局，也就是靠右的参数住高地址，push 指令按什么次序执行是实现自己的选择，GCC 的选择恰好与文档的措辞吻合，咱们才能拿 push 的次序当佐证。您在别处读到有人把它引申成 ABI 规定了指令执行顺序，那就已经越过文档的本意了。

咱们不搞空口无凭这一套，让 gdb 把调用方写入与被调方读出的同一个地址抓出来。头一场咱们用源码级断点进 sum7，此时序言已经过去了，rbp 已经是 sum7 的帧基，咱们直接拿它量栈：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break sum7' -ex 'run' -ex 'printf "=== prologue done (rbp is sum7 frame). stack layout: ===\n"' -ex 'printf "[rbp+0x00] saved rbp  = "' -ex 'x/1gx $rbp' -ex 'printf "[rbp+0x08] return addr = "' -ex 'x/1gx $rbp+8' -ex 'printf "[rbp+0x10] 7th arg g  = "' -ex 'x/1dw $rbp+16' -ex 'p g' ./c3_seven_int
Breakpoint 1 at 0x1131: file c3_seven_int.cpp, line 3.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, sum7 (a=1, b=2, c=3, d=4, e=5, f=6, g=7) at c3_seven_int.cpp:3
3	    return a + b + c + d + e + f + g;
=== prologue done (rbp is sum7 frame). stack layout: ===
[rbp+0x00] saved rbp  = 0x7fffffffd098:	0x00007fffffffd0c0
[rbp+0x08] return addr = 0x7fffffffd0a0:	0x0000555555555183
[rbp+0x10] 7th arg g  = 0x7fffffffd0a8:	7
$1 = 7
```

rbp+16 的地址是 0x7fffffffd0a8，那里躺着的值是 7。第二场咱们去调用那一侧，把断点下在 call 指令本身的地址上。本机的 GCC 默认开了 PIE，程序加载的基址每次不同，直接拿文件里的偏移下断点，gdb 会报出 Cannot access memory 的错误，正路是拿运行时的基址加偏移来算。好在 gdb 默认把地址随机化（ASLR）关掉，加载的基址固定在 0x555555554000，运行时地址也就可以复现了。call 的文件偏移来自 `disassemble main` 的输出，关键的一段如下：

```text
   0x000000000000115c <+8>:	push   $0x7
   0x000000000000115e <+10>:	mov    $0x6,%r9d
   0x0000000000001164 <+16>:	mov    $0x5,%r8d
   0x000000000000116a <+22>:	mov    $0x4,%ecx
   0x000000000000116f <+27>:	mov    $0x3,%edx
   0x0000000000001174 <+32>:	mov    $0x2,%esi
   0x0000000000001179 <+37>:	mov    $0x1,%edi
   0x000000000000117e <+42>:	call   0x1119 <_Z4sum7iiiiiii>
   0x0000000000001183 <+47>:	add    $0x8,%rsp
```

咱们看，call 落在 +42 的位置，也就是 0x117e 的文件偏移，运行时的地址就是 0x555555554000 加上它：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break *(0x555555554000+0x117e)' -ex 'run' -ex 'info registers rsp' -ex 'x/1dw $rsp' ./c3_seven_int
Breakpoint 1 at 0x55555555517e
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x000055555555517e in main () at c3_seven_int.cpp:9
9	    int r = sum7(1, 2, 3, 4, 5, 6, 7);
rsp            0x7fffffffd0a8      0x7fffffffd0a8
0x7fffffffd0a8:	7
```

call 执行之前的那一瞬，rsp 指的还是 0x7fffffffd0a8，那个地址上的值是 7。咱们把两场会话并排一看，链条就接上了：调用方在 0x7fffffffd0a8 写入的是 7，被调方从 rbp+16 读出的，也是同一个 0x7fffffffd0a8 上的 7。写入与读取用的是同一个地址，双方各按各的偏移认路，所以交接没有半点含糊。上一个主题里咱们看返回地址是这么交接的，这一篇的参数也是同样的路数。

## 返回值：小值进 rax，大值走地址

参数的走向看完了，咱们来追返回值。最小的不用多讲，一个返回 int 的函数：

```cpp
// c4a_ret_int.cpp
__attribute__((noinline)) int answer() { return 42; }

volatile int sink = 0;

int main() {
    int r = answer();
    sink = r;
    return 0;
}
```

被调方与 main 的核心各占一行，咱们直接看指令：

```asm
_Z6answerv:
	...（序言，略）
	movl	$42, %eax
	popq	%rbp
	ret

main:
	...（序言与 subq，略）
	call	_Z6answerv
	movl	%eax, -4(%rbp)
	...（写回 sink，略）
```

`movl $42, %eax` 把返回值放进了 eax，ret 之后 main 的 `movl %eax, -4(%rbp)` 立刻取走，通道就是 rax 的低 32 位，这跟参数走 edi 的思路是一路的。真正有意思的地方在返回值变大以后，咱们接着看。

咱们让函数返回一个 24 字节的 `struct Big { long a, b, c; }`：

```cpp
// c4b_ret_big.cpp
struct Big { long a, b, c; };  // 24 bytes > 16 -> MEMORY class

__attribute__((noinline)) Big make_big(long x) {
    Big b;
    b.a = x;
    b.b = x + 1;
    b.c = x + 2;
    return b;
}

volatile long sink = 0;

int main() {
    Big r = make_big(10);
    sink = r.a + r.b + r.c;  // consume so nothing is optimized away
    return 0;
}
```

本节的编译命令多了个 `-fno-stack-protector`，为的是别让金丝雀三件套来搅局。不开它的话，默认配置会在 main 里塞进 `movq %fs:40, %rax` 相关的装填与结尾校验，被测的核心序列是不变的，您感兴趣可以去掉开关自己编一版对照。下面看的是干净版：

```text
$ g++ -O0 -fno-asynchronous-unwind-tables -fno-stack-protector -S c4b_ret_big.cpp -o c4b_ret_big_nosp.s
```

```asm
_Z8make_bigl:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	movq	%rdx, (%rax)
	movq	-16(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 8(%rax)
	movq	-16(%rbp), %rax
	leaq	2(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 16(%rax)
	nop
	movq	-8(%rbp), %rax
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	leaq	-32(%rbp), %rax
	movl	$10, %esi
	movq	%rax, %rdi
	call	_Z8make_bigl
	movq	-32(%rbp), %rdx
	movq	-24(%rbp), %rax
	addq	%rax, %rdx
	movq	-16(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, sink(%rip)
	movl	$0, %eax
	leave
	ret
```

咱们猛一看有点眼晕，那就把它拆成三段地址接力来看。

第 1 棒的交接在 main 手里。`leaq -32(%rbp), %rax` 取的是 main 栈上接收者 r 的地址，r 的体量是 24 字节，住处就安排在 -32(%rbp) 往下的位置。接着 `movl $10, %esi` 把实参 x=10 装进了 esi。您注意到了吗，实参走的已经不是 edi，进的已经是第 2 个整数寄存器，因为 rdi 的位置被返回值的缓冲地址占了：`movq %rax, %rdi` 交出去的正是 r 的地址。返回大值的函数会多出一个看不见的第 1 参数，真正的第 1 个实参被它挤到了 rsi。

第 2 棒咱们去被调方 make_big 里看。`movq %rdi, -8(%rbp)` 把那个隐藏指针 spill 进了栈槽，随后的一串指令全是围着它写的：`movq %rdx, (%rax)` 写的是 a，`movq %rdx, 8(%rax)` 写的是 b，`movq %rdx, 16(%rax)` 写的是 c。三个成员直接写进调用方栈上 r 的地盘，从头到尾没有 24 字节的整体搬运。返回值不走寄存器里的数据通道，寄存器里传的是地址。

第 3 棒干的是收尾。`movq -8(%rbp), %rax` 把隐藏指针原样放进了 rax，ret 带回去的还是那个地址。咱们用 gdb 把三棒连起来验一遍：断点进函数的时候读 rdi，咱们再用临时断点卡在调用返回处读 rax：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' -ex 'break make_big' -ex 'run' -ex 'info registers rdi rsi' -ex 'set $buf = $rdi' -ex 'tbreak *_Z8make_bigl+92' -ex 'continue' -ex 'info registers rax' -ex 'printf "rax = 0x%x, equals hidden ptr? %d\n", $rax, ($rax == $buf)' -ex 'x/3dg $rax' ./c4b_ret_big
Breakpoint 1 at 0x1125: file c4b_ret_big.cpp, line 6.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_big (x=10) at c4b_ret_big.cpp:6
6	    b.a = x;
rdi            0x7fffffffd0a0      140737488343200
rsi            0xa                 10
Temporary breakpoint 2 at 0x555555555175: file c4b_ret_big.cpp, line 16.

Temporary breakpoint 2, 0x0000555555555175 in main () at c4b_ret_big.cpp:16
16	    sink = r.a + r.b + r.c;  // consume so nothing is optimized away
rax            0x7fffffffd0a0      140737488343200
rax = 0xffffd0a0, equals hidden ptr? 1
0x7fffffffd0a0:	10	11
0x7fffffffd0b0:	12
```

咱们看会话：入口处的 rdi 是 0x7fffffffd0a0，rsi 里装的是 10，call 返回处的 rax 还是 0x7fffffffd0a0，`equals hidden ptr?` 的判定是 1。入口交进去的是这个地址，ret 带回来的还是它，那块内存里躺着的正是 10、11、12。这套机制的依据写在 ABI 文档的 Returning of Values 一节里，原文是这样的：

```text
2. If the type has class MEMORY, then the caller provides space for
   the return value and passes the address of this storage in %rdi
   as if it were the first argument to the function. In effect, this
   address becomes a "hidden" first argument. ... On return %rax will
   contain the address that has been passed in by the caller in %rdi.
```

咱们把它翻译过来：类型属于 MEMORY 类的时候，调用方为返回值备好了空间，把这个空间的地址放进 rdi，待遇跟第 1 个实参的一模一样，事实上它就成了一个隐藏的首参数。返回时 rax 装的，就是调用方当初经 rdi 交进来的那个地址。文档明文写了的事，咱们刚才在汇编与 gdb 里逐条兑现了。什么样的返回值落 MEMORY 类、什么条件走寄存器，规则到下一篇讲结构体的部分咱们再展开，眼前您手上已经有对照的两极样本：小返回值（int）的数据本身放 rax，大返回值（超过 16 字节）的 rax 只回传地址，数据走的是内存。整数参数与返回值咱们验到这里，下一篇就该轮到 double 和结构体了。
