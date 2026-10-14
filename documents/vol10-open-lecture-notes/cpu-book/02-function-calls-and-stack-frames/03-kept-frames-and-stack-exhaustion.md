---
title: "保住栈帧的函数，与递归把栈用满"
chapter: 2
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard:
  - 11
  - 17
description: "树形递归才保得住帧，帧指针从 -O1 起默认省掉，不带终止条件的递归把 8 MiB 栈用满后崩在 call 的压栈上"
reading_time_minutes: 14
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 保住栈帧的函数，与递归把栈用满

上一篇咱们看到 -O2 会把小函数的调用点直接算没，取走函数地址又逼得实体必须生成。这一篇咱们把问题推到栈帧身上，看什么样的函数在 -O2 下还得老老实实地开帧，再把不带终止条件的递归一路压到栈的限额上，看它怎么收场。咱们这一篇的汇编与调试器会话，跑在跟[第一篇](01-call-and-stack-frame-walkthrough.md)相同的环境里。

## 什么样的函数能在 -O2 保住栈帧

那什么样的函数在 -O2 下还留着货真价实的栈帧？直觉的答案是函数体大或者带递归。咱们拿这个直觉做实验，结果它被编译器一一拆掉了，三个反例全都来自真实的尝试。

咱们头一个尝试是循环求和的函数、上界写死成常量 100：整个循环在编译期就被求完了值，`sum100` 直接变成了 `movl $5050, %eax` 加 `ret`，main 里留下的也是常量 5050。函数都被编译器算空了，栈帧自然就谈不上了。

第二个尝试把上界换成了 volatile 的参数 n，循环保住了，但 `sum_up_to` 成了叶子函数、循环体里没有 call，GCC 让局部变量住进了红区（`movl %edi, -4(%rsp)`），传统的栈帧依然没有出现，main 里的调用还被做了部分内联和向量化改写。向量化指的是把循环改写成一次算多个元素的 SIMD 指令。所以函数体大保不住帧，咱们在这里就拿到了活的证据。

咱们第三个尝试换成了递归，函数就是 `return n * fact(n-1)` 的样子：GCC 在 -O2 下做了累加器变换，把整个递归都改写成了循环，call 也跟着消失了。累加器变换的意思是：把乘到 n 的过程改写成带着乘积往下走的循环，非尾递归的写法也不保险，递归的形式本身挡不住编译器。

真正保住帧的是树形递归，咱们拿它跑成功了。fib 的源码是这样的：

```cpp
int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    volatile int n = 10;
    volatile int s = fib(n);
    return s;
}
```

`fib` 的两个递归调用 `fib(n-1)` 和 `fib(n-2)` 没法收成简单循环，每一次调用都需要独立的返回地址和帧，call 也就消不掉了，栈帧也就得老老实实地开了。实测的汇编是节选（中段的循环体已删，您拿上面的源码可以自行编译核对），咱们把编译命令和节选贴在下面：

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b5b2_recur.s
```

```asm
_Z3fibi:
.LFB0:
	subq	$152, %rsp
	movq	%r14, 136(%rsp)
	movl	%edi, %r14d
	cmpl	$1, %edi
	jle	.L37
	...
.L30:
	leal	-1(%r12), %edi
	subl	$2, %r12d
	call	_Z3fibi
	addl	%eax, %r15d
	cmpl	$1, %r12d
	jg	.L30
	...
.L37:
	movl	%r14d, %eax
	movq	136(%rsp), %r14
	addq	$152, %rsp
	ret
```

咱们看 main 段的节选（`call _Z3fibi` 同样在、同样被部分改写，省略的后续指令不再贴）：

```asm
main:
.LFB1:
	pushq	%rbx
	xorl	%r9d, %r9d
	subq	$16, %rsp
	movl	$10, 8(%rsp)
	movl	8(%rsp), %ebx
	cmpl	$1, %ebx
	jle	.L57
	movl	%ebx, %r8d
.L56:
	leal	-1(%r8), %edi
	subl	$2, %r8d
	call	_Z3fibi
	...
```

咱们看 fib 的帧是实打实的：`subq $152, %rsp` 一步就开了 152 字节，还保存了一串按约定用前必须保住原值的寄存器（行话叫 callee-saved，被调用方用前要保的寄存器，咱们这次在帧里见到的有 r14、rbx、r12、r13、rbp、r15），分别放在 rsp 的相对偏移处。`call _Z3fibi` 留在 `.L30` 的循环里。有意思的是 GCC 还是做了部分迭代化，也就是把递归的一侧改成循环：`fib(n-2)` 那一侧被改写成循环（`.L30` 里一路 `subl $2, %r12d` 的就是它），只保留 `fib(n-1)` 一侧的 call。到这里判定条件可以直说了：保帧靠的是调用消不掉，每一次调用都需要独立的返回地址，编译器变不出第二个可以共享的帧。fib 至少留了一个 call，帧就留下了。

## 帧指针自己也能省

prologue 的头两步都在伺候 rbp，看到这里您可能会想：这个基准点是非有不可的吗？GCC 的回答是未必。-fomit-frame-pointer 这个开关管的就是它，[GCC 手册优化选项页](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)里的说法是 “Omit the frame pointer in functions that don't need one”，在不需要帧指针的函数里省掉它，而且从 -O1 起默认就开了。

省掉之后的汇编变成什么样？咱们拿 fib 做对照，它在 -O2 下帧最大、栈上暂存的寄存器也最多，对比的效果最明显。同一份 fib 源码咱们编两回，一回用 -O2 的默认设置，一回加 -fno-omit-frame-pointer（要求保留帧指针的开关）：

```text
$ g++ -O2 -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b7_fib_default.s
$ g++ -O2 -fno-omit-frame-pointer -fno-asynchronous-unwind-tables -S b5b2_recur.cpp -o b7_fip_fip.s
```

咱们从两个文件里都只贴 `_Z3fibi` 的头尾，中段的循环体已删、用 `...` 标出。头一份是 -O2 的默认（省帧指针）：

```asm
_Z3fibi:
.LFB0:
	subq	$152, %rsp
	movq	%r14, 136(%rsp)
	movl	%edi, %r14d
	...
.L37:
	movl	%r14d, %eax
	movq	136(%rsp), %r14
	addq	$152, %rsp
	ret
```

咱们看另一份，它加了 -fno-omit-frame-pointer：

```asm
_Z3fibi:
.LFB0:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$144, %rsp
	movq	%rbx, -40(%rbp)
	movl	%edi, %ebx
	...
.L37:
	movl	%ebx, %eax
	movq	-40(%rbp), %rbx
	leave
	ret
```

咱们看默认版的 prologue 只剩 `subq $152, %rsp`，`push rbp` 与 `mov rbp,rsp` 都没有了，栈上的寻址一律改成 rsp 相对（`136(%rsp)`、`112(%rsp)` 这类）。epilogue 也跟着变了脸：不再用 leave，改回手工的 `addq $152, %rsp` 加 `ret`。帧的大小从 152 变到 144 也有它的道理：带帧指针的版本多 push 了 8 字节的 rbp，寄存器的分配也不同，默认版是拿 r14 存住 n 的，带帧指针的版本改拿 rbx。

更有说服力的是 rbp 的去向。默认版汇编里有这么几行（行内的注释是咱们加的、汇编原文件里没有）：

```asm
	movq	%rbp, 112(%rsp)     # rbp 旧值当普通 callee-saved 数据存进帧里
	movl	%eax, %ebp          # rbp 当循环变量用
	subl	%edx, %ebp
	movl	%ebp, 56(%rsp)
	xorl	%ebp, %ebp
	addl	%eax, %ebp          # 参与加法
```

`movq %rbp, 112(%rsp)` 把 rbp 的旧值当普通数据存进帧里，跟着 `movl %eax, %ebp`、`addl %eax, %ebp` 又拿 rbp 当了循环变量、参与加法。省下帧指针的收益就在这：多出来的就是一个人人可用的寄存器。手册的原话是它 “makes an extra register available”，咱们在指令里亲眼看到了。

调试器为什么在意这件事？咱们把机制讲开：带帧指针的时候，每个帧里 [rbp] 存着上一帧的 rbp，顺着 rbp 往上一格一格地走，就能回溯出完整的调用链。省掉之后的 rbp 不再可靠，想从当前的指令位置恢复调用链，就得靠 unwind 的信息一层层去算，那是记录每个函数栈布局的调试数据，`.cfi` 伪指令就是它的源头。所以想顺着 rbp 链回溯的场合就得显式加上 -fno-omit-frame-pointer，代价是每个函数多出两条指令、少掉一个可用的寄存器。手册里的原话还提醒了另一件事：负形式 -fno-omit-frame-pointer 不保证所有函数都带帧指针，有的目标在叶子函数里永远省掉它。这跟咱们在[上一篇](02-leaf-functions-stack-direction-and-o2.md)叶子函数一节看到的省法是两回事：那里省掉的是 sub rsp，`push rbp` 与 `mov rbp,rsp` 都还好好地待在开头，而手册说的省法是连帧指针本身都不要。

## 递归把栈用满之后

咱们最后看一件会真实翻车的事：不带终止条件的递归。countdown 的每层调用都要压一个返回地址、开一小块帧，栈往低处一层层地探，限额总有探到头的时候。

```cpp
int countdown(int n) {
    return countdown(n - 1);
}

int main() {
    return countdown(0);
}
```

栈限额与 core 的配置咱们原样贴在这里：

```text
$ ulimit -s
8192
$ ulimit -c
0
$ cat /proc/sys/kernel/core_pattern
|/usr/lib/systemd/systemd-coredump %P %u %g %s %t %c %h %d %F %I
```

```text
$ g++ -O0 -g b6_countdown.cpp -o b6_countdown
$ ulimit -c 0; ./b6_countdown; echo "=== exit status: $? ==="
```

```text
=== exit status: 139 ===
```

程序死掉了，shell 层面的全部证据只有这一行退出码：139 里减去 128 得到的是 11，而 11 正是 SIGSEGV 的编号。本机是 WSL2 的环境，非交互 shell 下 bash 和 zsh 都不会打印 Segmentation fault 的那行字，所以您自己复现时看不到崩溃文本，请拿退出码当 shell 层的证据。core 文件也是没有的：本机的 ulimit -c 是 0，core_pattern 还指向 systemd-coredump 的管道，崩溃转储被 systemd 接管了。咱们想看崩溃现场，就请 gdb 出场（会话里 === 开头的分隔行是命令里 printf 打出来的，不属于 gdb 自己的输出）：

```text
$ gdb --batch -q b6_countdown -ex 'set debuginfod enabled off' -ex 'run' \
      -ex 'printf "=== program stopped by crash ===\n"' -ex 'info program' \
      -ex 'printf "=== registers around crash ===\n"' -ex 'info registers rsp rip' \
      -ex 'printf "=== backtrace head (repeats elided by tool) ===\n"' -ex 'bt 5' \
      -ex 'printf "=== can we read the stack page at rsp? ===\n"' -ex 'x/gx $rsp'

[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
0x000055555555512c in countdown (n=-261893) at b6_countdown.cpp:2
2	    return countdown(n - 1);
=== program stopped by crash ===
Last stopped for thread 1 (Thread 0x7ffff7f66780 (LWP 97996)).
	Using the running image of child process 97996.
Program stopped at 0x55555555512c.
It stopped with signal SIGSEGV, Segmentation fault.
=== registers around crash ===
rsp            0x7fffff7ff000      0x7fffff7ff000
rip            0x55555555512c      0x55555555512c <countdown(int)+19>
=== backtrace head (repeats elided by tool) ===
#0  0x000055555555512c in countdown (n=-261893) at b6_countdown.cpp:2
#1  0x0000555555555131 in countdown (n=-261892) at b6_countdown.cpp:2
#2  0x0000555555555131 in countdown (n=-261891) at b6_countdown.cpp:2
#3  0x0000555555555131 in countdown (n=-261890) at b6_countdown.cpp:2
#4  0x0000555555555131 in countdown (n=-261889) at b6_countdown.cpp:2
=== can we read the stack page at rsp? ===
0x7fffff7ff000:	0x0000000000000000
```

countdown 的反汇编咱们也在同一份二进制上取到了：

```text
Dump of assembler code for function _Z9countdowni:
   0x0000000000001119 <+0>:	push   %rbp
   0x000000000000111a <+1>:	mov    %rsp,%rbp
   0x000000000000111d <+4>:	sub    $0x10,%rsp
   0x0000000000001121 <+8>:	mov    %edi,-0x4(%rbp)
   0x0000000000001124 <+11>:	mov    -0x4(%rbp),%eax
   0x0000000000001127 <+14>:	sub    $0x1,%eax
   0x000000000000112a <+17>:	mov    %eax,%edi
   0x000000000000112c <+19>:	call   0x1119 <_Z9countdowni>
   0x0000000000001131 <+24>:	leave
   0x0000000000001132 <+25>:	ret
End of assembler dump.
```

咱们看到程序崩在了 countdown+19 的指令上，就是 call 那一行：它要把自己算出的返回地址写进 [rsp-8] = 0x7fffff7feff8，这个地址已经越过了栈区的下界，写入的动作当场触发了 SIGSEGV。把栈顶用满的直接凶手就是 call 的压栈。rsp 停在了 0x7fffff7ff000 这个整页的边界上，拿栈区的上界 0x7ffffffff000 去减它，得到的差是 0x800000、折合 8 MiB，与 ulimit -s 给的 8192 KiB 正好对上（咱们在[第一篇](01-call-and-stack-frame-walkthrough.md) call 一节的 gdb 会话里见过的 rsp 0x7fffffffd0b8，就贴在这个上界的下面）。每层帧 24 字节（push rbp 的 8 加 sub $0x10 的 16），n 一路从 0 递减到了 -261893，约合 26.2 万层的深度，占了大约 6 MiB，与 8 MiB 的差额被进程启动时占用的初始栈内容（环境块、argv 这些）吃掉了，这部分咱们没有逐项测量。

咱们在会话末尾还能看到一组值得细看的证据：`x/gx $rsp` 读 0x7fffff7ff000 是成功的，读出的值是 0，说明那一页还是可读的，而 call 要写的是 [rsp-8]，比它再低了 8 字节，落在了栈区之外。一读一写正好把栈的下界夹了出来。`bt 5` 给出的 5 帧是命令里显式限定的帧数，这口 26 万层深的栈完整回溯会一路刷回 main，每一帧的源码行都停在同一条无限递归上。

咱们换 AddressSanitizer 再跑一遍，诊断的判读会更容易。ASan 是编译期加 -fsanitize=address 接入的内存错误检测器，它给每层的帧都加了毒区，帧变大了，大约 247 层就触了顶，所以报告里只有两百多帧。报告的全长是 253 行，几乎全是重复的 countdown 帧，咱们保留头尾、删去中间 #3 到 #245 的重复帧（完整日志可自行复跑）：

```text
$ g++ -O0 -g -fsanitize=address b6_countdown.cpp -o b6_countdown_asan
$ ./b6_countdown_asan
```

```text
AddressSanitizer:DEADLYSIGNAL
=================================================================
==97889==ERROR: AddressSanitizer: stack-overflow on address 0x7ffd66f89ff8 (pc 0x58e9a061915c bp 0x7ffd66f8a010 sp 0x7ffd66f8a000 T0)
    #0 0x58e9a061915c in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    #1 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    #2 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2
    ...(中间 #3 ~ #245 均为同一 countdown(int) 帧,已删)...
    #246 0x58e9a0619160 in countdown(int) /tmp/cpu-book-02/b6_countdown.cpp:2

SUMMARY: AddressSanitizer: stack-overflow /tmp/cpu-book-02/b6_countdown.cpp:2 in countdown(int)
==97889==ABORTING
```

ASan 直接把 stack-overflow 的判定写在报告第一行，比裸段错误的一行 SIGSEGV 更容易读出病因，进程的收尾是 ABORTING，退出码停在了 1。这一场栈溢出咱们用两条路都拿到了证据：裸版本每层 24 字节、能叠到约 26 万层，ASan 版本约 247 层就触了顶。想把崩溃取证练成手艺的您，可以移步 [crash-lab 的崩溃现场实战](../../../crash-lab/a-memory-safety/01-null-deref.md)。系列下一篇咱们讲参数装货：`movl $3, %edi` 与 `movl $4, %esi` 背后是同一套传参约定，更多的参数往哪放，咱们到那里接着拿实验说话。
