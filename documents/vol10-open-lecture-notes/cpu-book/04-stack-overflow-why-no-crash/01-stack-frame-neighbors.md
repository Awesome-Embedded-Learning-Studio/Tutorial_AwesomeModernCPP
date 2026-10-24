---
title: "栈帧里的邻居：越界写落在了谁头上"
chapter: 4
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "拿 objdump 与 gdb 量出 f 的栈帧座位表，看清 arr[-1] 打中的是 scalar，再验明排座位的是栈保护档位而不是声明顺序"
reading_time_minutes: 13
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 栈帧里的邻居：越界写落在了谁头上

[上一篇](../03-arguments-in-registers/01-integer-args-register-allocation.md)咱们跟着参数走完了寄存器的交接，[再前一篇](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md)咱们把栈帧一砖一瓦搭了起来：rbp 往上是 saved rbp 与返回地址，局部变量住的是 rbp 的负偏移区。这一篇咱们就在 02 搭好的栈帧上干点危险的事，往一个 4 个元素的栈上数组外面写数据。咱们按直觉想，写出界了应该当场出事，可咱们下面这个程序跑完啥事没有，printf 照旧把活干完了，退出码也是干干净净的 0。

咱们用的程序小得能整个贴出来。咱们在 f 里放一个 `int arr[4]` 和一个标量 scalar，然后咱们拿一个负下标往数组界外写一个 999：

```cpp
// d1.cpp —— 越界写实验的主用源码
// -O0 与 -O2 编的是同一份代码，后面还要交给 ASan 再跑一遍
// 下标与载荷都走 volatile 全局，编译期折叠不了
// 栈上布局怎么量，排座位一节的末尾给了方法
#include <cstdio>

volatile int g_index   = -1;  // 越界下标，为什么是 -1，看了实测布局才知道
volatile int g_payload = 999; // 越界写入的值

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    int scalar = 111;

    std::printf("before: scalar=%d arr={%d,%d,%d,%d}\n",
                scalar, arr[0], arr[1], arr[2], arr[3]);

    arr[g_index] = g_payload; // 越界写

    std::printf("after : scalar=%d arr[g_index]=%d\n",
                scalar, arr[g_index]);
}

int main() {
    f();
    std::printf("main: returned from f, program ends normally\n");
    return 0;
}
```

咱们的编译与运行只要两条命令，输出咱们原样贴在下面（各运行块末尾的 exit=数字，是咱们用 echo $? 接在命令后面打的退出码回显，后文就不再重复说明了）：

```text
$ g++ -O0 -g -o d1_o0 d1.cpp
$ ./d1_o0
before: scalar=111 arr={1,2,3,4}
after : scalar=999 arr[g_index]=999
main: returned from f, program ends normally
exit=0
```

您看 after 那一行：scalar 本来是 111，现在成了 999。咱们声明的数组只有 4 个元素，arr[-1] 压根不在数组的界内，这次写入却安安稳稳落了地，还改坏了同帧的邻居，程序照样跑完了、正常退出。这一篇要回答的问题就在眼前：它为什么没崩？落点到底是谁？打到多远才会出事？

咱们把名分也定下来：往数组界外写这件事，标准给它起的名字是未定义行为（undefined behavior），白话的意思就是标准撒手不管、实现随便来，编译器把变量怎么排、优化成什么样、跑出什么样的结果，在标准看来都是算它对的。所以本篇要看的其实是本机这一版编译器的实际安排。往后咱们不凭记忆画布局，一切判断都来自反汇编与调试器的实测。

这一篇的例子与实验都是本站原创的，您在后文看到的每一段输出，都来自本机的真实运行。

## 咱们的实验环境

照本站的惯例，咱们把环境摆在最前面，后面的每一块汇编、每一段会话都出自同一套配置：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is
NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ ulimit -s
8192

$ cat /proc/sys/kernel/randomize_va_space
2
```

本机的几项配置后面都要用到。栈的软限额是 8192 KiB、正好 8 MiB（ulimit -s 那一行给的），这个数咱们到[第四篇](04-stack-headroom-asan-and-luck.md)量栈余量时拿它当地板。randomize_va_space 报的是 2，地址空间随机化开到了最大，每次运行栈的基线都会换，[02](../02-function-calls-and-stack-frames/02-leaf-functions-stack-direction-and-o2.md) 在嵌套调用一节见过它的效果。

发行版默认开了栈保护，[01](../01-prefix-vs-postfix-increment/01-int-instructions.md) 的环境清单里交代过 -fstack-protector-strong，带数组一类局部变量的函数会多出金丝雀（canary，栈保护的哨兵值）的装载与校验指令。咱们这个主题要正面研究的就是这套东西，至于本机的默认档等不等于 strong，这个等式咱们不背，留到[第三篇](03-canary-defense-and-the-o2-story.md)用逐指令的 diff 当面验。

还有一条取证纪律沿用 02 的口径：WSL2 的非交互 shell 不打印 Segmentation fault 字样，shell 层的证据只有退出码，139 减 128 得到的 11 是 SIGSEGV 的编号，134 减 128 得到的 6 是 SIGABRT 的编号。凡文中出现 SIGSEGV 或 SIGABRT 文本的地方，出处要么是 gdb 的 Program received signal 行，要么是程序自己的 stderr。`*** stack smashing detected ***` 那一行则是 glibc 打到 stderr 的，咱们在 shell 里看得到，也就照实贴上了。

## f 的栈帧里住了谁

要弄清 arr[-1] 打中了谁，咱们把 f 的栈帧搬出来对号。02 讲过 -O0 下 GCC 拿 rbp 当帧的基准：rbp+8 是返回地址，rbp 指着的是保存旧值的 saved rbp，局部变量住的是负偏移区。d1 的 f 比 02 的例子多了一样东西，就是环境一节说的金丝雀，它的住处在 rbp-0x8。座位的事咱们不猜，咱们拿 objdump 现场勘察：

```text
$ objdump -d --no-show-raw-insn -C d1_o0 | sed -n '/<f()>:/,/^$/p'
```

```asm
0000000000001159 <f()>:
    1159:	push   %rbp
    115a:	mov    %rsp,%rbp
    115d:	sub    $0x30,%rsp
    1161:	mov    %fs:0x28,%rax
    116a:	mov    %rax,-0x8(%rbp)
    116e:	xor    %eax,%eax
    1170:	movl   $0x1,-0x20(%rbp)
    1177:	movl   $0x2,-0x1c(%rbp)
    117e:	movl   $0x3,-0x18(%rbp)
    1185:	movl   $0x4,-0x14(%rbp)
    118c:	movl   $0x6f,-0x24(%rbp)
    1193:	mov    -0x14(%rbp),%r8d
    ...（printf 参数搬运，删节）...
    11b7:	call   1030 <printf@plt>
    11bc:	mov    0x2e6a(%rip),%edx        # 402c <g_payload>
    11c2:	mov    0x2e60(%rip),%eax        # 4028 <g_index>
    11c8:	cltq
    11ca:	mov    %edx,-0x20(%rbp,%rax,4)
    ...（第二次 printf 参数搬运，删节）...
    11f3:	nop
    11f4:	mov    -0x8(%rbp),%rax
    11f8:	sub    %fs:0x28,%rax
    1201:	je     1208 <f()+0xaf>
    1203:	call   1040 <__stack_chk_fail@plt>
    1208:	leave
    1209:	ret
```

咱们顺着初始化指令把座位表读出来，每一行的常量都是依据：

| 槽位 | 偏移 | 依据 |
|---|---|---|
| 金丝雀 | rbp-0x8 | `mov %fs:0x28,%rax` 后存进 `-0x8(%rbp)` |
| arr[0..3] | rbp-0x20 .. rbp-0x14 | `movl $0x1,-0x20(%rbp)` 起的四个初始化 |
| scalar | rbp-0x24 | `movl $0x6f,-0x24(%rbp)`，0x6f 就是 111 |
| 越界寻址 | 基址 rbp-0x20、步长 4 | `mov %edx,-0x20(%rbp,%rax,4)` |

您把 arr 与 scalar 的偏移摆在一起，要害就露出来了：rbp-0x24 比 rbp-0x20 更低，标量住在数组的下方。这与不少旧教材画的图正好相反，旧图里的数组住在低处，标量住的位置更高，拿正下标一路向上穿的时候，能打中的恰好是标量。本机 GCC 16 在默认栈保护档下的 -O0 布局是另一番安排：数组被安置在紧挨金丝雀的高位，标量挪到了数组底下。咱们算下标就按实测来：越界寻址的基址是 rbp-0x20、每格 4 字节，scalar 的槽相对基址是 (rbp-0x24 - (rbp-0x20)) / 4 = -1，所以源码里 g_index 放的是 -1，arr[-1] 不偏不倚落在 scalar 的头上。咱们再往正方向打：arr[4] 落在 rbp-0x10、arr[5] 落在 rbp-0xc，那 8 字节是没人住的填充。想碰金丝雀的话，得打到 arr[6] 的位置，arr[8] 够到的是 saved rbp，arr[10] 轮到的是返回地址，这段距离咱们留到[下一篇](02-hitting-canary-saved-rbp-and-return-address.md)正面量。

咱们在这里给 f 的栈帧布局留一个图位，图待补：内容为从返回地址往下依次是 saved rbp、金丝雀（rbp-0x8）、8 字节填充、arr 的四格（rbp-0x20 到 rbp-0x14）、scalar（rbp-0x24），与 02 的栈帧图共用同一套视觉体系，后续由本站的绘图管线补上。

程序没崩的机制，从座位表上已经能说出一半：这次写的落点还在本帧的普通变量区里，离金丝雀、saved rbp、返回地址这些控制数据足够远，写坏的只是 scalar 一个数据邻居。空口说打中了不算数，咱们拿 gdb 到内存里对号，断点下在第二个 printf 的那行源码上，也就是写入之后、返回之前的时刻：

```text
$ gdb -batch -q \
      -ex 'break d1.cpp:20' -ex run \
      -ex 'printf "== 写入后 f 帧内内存(从 rbp-0x28 往上) ==\n"' \
      -ex 'x/10wx $rbp-0x28' \
      -ex 'printf "== 关键槽位 ==\n"' \
      -ex 'printf "scalar 槽 rbp-0x24 = %d\ncanary 槽 rbp-0x8  = 0x%lx\n", *(int*)($rbp-0x24), *(unsigned long*)($rbp-0x8)' \
      -ex 'info frame' \
      --args ./d1_o0
```

（会话里 == 开头的分隔行来自咱们让 gdb 打的 printf 命令，Breakpoint 1 at 一类的家务行、debuginfod 询问与线程库提示已删，其余都是逐字的）。

```text
Breakpoint 1, f () at d1.cpp:20
20	    std::printf("after : scalar=%d arr[g_index]=%d\n",
== 写入后 f 帧内内存(从 rbp-0x28 往上) ==
0x7fffffffd098:	0xf7ebd538	0x000003e7	0x00000001	0x00000002
0x7fffffffd0a8:	0x00000003	0x00000004	0x00000001	0x00000000
0x7fffffffd0b8:	0xef969100	0x2364d555
== 关键槽位 ==
scalar 槽 rbp-0x24 = 999
canary 槽 rbp-0x8  = 0x2364d555ef969100
Stack level 0, frame at 0x7fffffffd0d0:
 rip = 0x5555555551d4 in f (d1.cpp:20); saved rip = 0x555555555213
 called by frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0c0, args:
 Locals at 0x7fffffffd0c0, Previous frame's sp is 0x7fffffffd0d0
 Saved registers:
  rbp at 0x7fffffffd0c0, rip at 0x7fffffffd0c8
```

咱们一行行对，info frame 的 Saved registers 告诉了咱们 rbp 的落点，它给的是 0x7fffffffd0c0。咱们让 x/10wx 从 rbp-0x28 起读 10 个 4 字节的字，读到 rbp 跟前就停了。0x...d09c 上的 0x3e7 就是 999，它的位置正是 rbp-0x24、scalar 的槽。0x...d0a0 到 0x...d0ac 四格是 arr 的 1、2、3、4。0x...d0b8 起是完好的金丝雀 0x2364d555ef969100。rbp-0x10 与 rbp-0xc 的两格填充里还留着旧值，但咱们翻遍座位表，也没找到认领它们的变量。写入的落点、受损者、幸存者，三方都摆在咱们眼前的内存里。

开场那一幕的每个数字到这里有了着落：arr[-1] 打中的是 scalar，把它从 111 改成了 999，金丝雀与返回地址都是完好的，f 正常地返回了，退出码停在了 0。谁排在数组的上方、谁的座位在下方，这件事由 GCC 说了算，咱们的下标是用实测偏移算出来的。下一节咱们就验证，排座位这件事到底听谁的。

## 排座位的是保护档位，不是声明顺序

咱们的头一个怀疑对象是声明顺序：是不是把标量写在数组前面，标量就能住到高处去？咱们做了四份对照来审它。d7a 与 d7b 两份源码只差两个声明的顺序对调，咱们各自再编两回，一回用的是默认档（带金丝雀），一回加的是 -fno-stack-protector（拆掉保护）：

```cpp
// d7a.cpp —— 数组先声明的那份
__attribute__((noinline))
void f() {
    int arr[4];
    int scalar;

    arr[0] = 0x11111111; // 汇编里找 movl $0x11111111,-0xXX(%rbp) 定位数组基址
    scalar = 0x22222222; // 汇编里找 movl $0x22222222,-0xYY(%rbp) 定位标量

    arr[1] = scalar; // 用一下两个变量
}

int main() {
    f();
    return 0;
}
```

```text
g++ -O0 -g -o d7a_p d7a.cpp            # 默认(strong)
g++ -O0 -g -o d7b_p d7b.cpp
g++ -O0 -g -fno-stack-protector -o d7a_n d7a.cpp
g++ -O0 -g -fno-stack-protector -o d7b_n d7b.cpp
```

咱们选 0x11111111 和 0x22222222 两个一眼可认的常量，就是为了在汇编里把两个变量的槽位直接认出来。带保护档的输出如下（d7b_p 的输出与它逐指令相同，咱们略过不贴）：

```asm
0000000000001139 <f()>:
    1139:	push   %rbp
    113a:	mov    %rsp,%rbp
    113d:	sub    $0x30,%rsp
    1141:	mov    %fs:0x28,%rax
    114a:	mov    %rax,-0x8(%rbp)
    114e:	xor    %eax,%eax
    1150:	movl   $0x11111111,-0x20(%rbp)   <- arr 基址 rbp-0x20
    1157:	movl   $0x22222222,-0x24(%rbp)   <- scalar 在 rbp-0x24，数组下方
    115e:	mov    -0x24(%rbp),%eax
    1161:	mov    %eax,-0x1c(%rbp)
    1164:	nop
    1165:	mov    -0x8(%rbp),%rax
    1169:	sub    %fs:0x28,%rax
    1172:	je     1179 <f()+0x40>
    1174:	call   1030 <__stack_chk_fail@plt>
    1179:	leave
    117a:	ret
```

拆掉保护的两份也是同一个样子（d7b_n 与 d7a_n 逐指令相同，咱们略过不贴）：

```asm
0000000000001119 <f()>:
    1119:	push   %rbp
    111a:	mov    %rsp,%rbp
    111d:	movl   $0x11111111,-0x20(%rbp)   <- arr 基址 rbp-0x20
    1124:	movl   $0x22222222,-0x4(%rbp)    <- scalar 在 rbp-0x4，数组上方
    112b:	mov    -0x4(%rbp),%eax
    112e:	mov    %eax,-0x1c(%rbp)
    1131:	nop
    1132:	pop    %rbp
    1133:	ret
```

四份布局咱们汇总成一张表（汇编里的箭头注释是咱们加的，汇编的原输出里是没有它们的）：

| 构建 | arr 基址 | scalar | 相对关系 | 挨着金丝雀的 |
|---|---|---|---|---|
| d7a_p（数组在前声明，strong） | rbp-0x20 | rbp-0x24 | 标量在数组下方 | 数组（隔 8 字节填充） |
| d7b_p（标量在前声明，strong） | rbp-0x20 | rbp-0x24 | 同上 | 同上 |
| d7a_n（数组在前声明，无保护） | rbp-0x20 | rbp-0x4 | 标量在数组上方 | 无（没有金丝雀，标量紧挨 saved rbp） |
| d7b_n（标量在前声明，无保护） | rbp-0x20 | rbp-0x4 | 同上 | 无 |

四份对照给出了干脆的答复。头一个答复咱们已经剧透过：声明顺序对 -O0 布局是零影响，两组的输出逐指令相同，地址也是分毫未动的，想靠把标量写在数组前面的办法来安排座位，在 GCC 16 这里是无效的操作。起作用的是金丝雀的有无。保护开着的时候，数组被安置在紧挨金丝雀的高位，标量挪到了数组下方，正向的越界永远够不着标量，想碰它的话只能用负下标，开场的实验正是这个布局的产物。金丝雀拆掉之后的世界咱们也量到了：标量被重新打包到 rbp-0x4，夹在数组与 saved rbp 的中间，这时正向越界的 arr[7] 会直接改写标量，arr[8] 够到的就是 saved rbp。挨着金丝雀住的是数组，栈保护的设计意图也在这里：最可能越界的东西被放在离控制数据最近的位置，中间隔着的却是一个哨兵。

量偏移的方法咱们也留给您，它比背下来的任何数字都可靠。咱们拿到 objdump 的反汇编之后，在输出里找 `movl $常量,-偏移(%rbp)` 一类的初始化行，常量会把每个变量的槽位直接供出来，拿到数组基址与目标的偏移，下标就是（目标偏移减去数组基址的偏移）除以 4。GCC 的版本、保护的档位、函数的形状（有没有 printf、参数多不多）都会挪动偏移，本篇全部具体数字属于 GCC 16.2.1 在 -O0 下的实测，您自己的二进制排出了什么布局，请您量一遍再下判断，别把咱们的偏移表当成普适的承诺。

帧里的邻居排布也搬进了执行器。下面的场景里 f 建起 0x30 的帧，您单步过 arr 的四条初始化，看栈窗口从低到高依次是局部 sum、arr 的 1 到 4、留白、金丝雀槽、saved rbp、返回地址，越界写的射程按这个顺序排。有两处与真程序不同：金丝雀读 fs:0x28 的段寻址执行器不仿真，场景用 .data 里的固定值顶替，写入与返回前的对比照做；printf 走 PLT，不在场景里。

<AsmExecutor src="/Tutorial_AwesomeModernCPP/payloads/frame_layout.json" />

近处的落点咱们量清了，金丝雀与返回地址离数组的距离，咱们接着往下量。
