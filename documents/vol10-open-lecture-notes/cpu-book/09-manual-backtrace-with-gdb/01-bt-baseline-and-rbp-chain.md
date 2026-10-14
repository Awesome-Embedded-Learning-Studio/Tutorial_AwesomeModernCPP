---
title: "bt 的正常形态与 rbp 链手工回溯"
chapter: 9
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "在 -O0 基准里读 bt 与 info frame 的输出，认下 rbp 与返回地址两个槽位，再用读内存命令顺着 rbp 链手工回溯，与 bt 逐层咬合"
reading_time_minutes: 9
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# bt 的正常形态与 rbp 链手工回溯

咱们在 [02 的开篇](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md)里跟着一对 call 与 ret，把一次函数调用的始末走读了：返回地址是怎么进栈的，prologue 又是怎么把帧搭起来的。调试器那边咱们也用过 bt，02 的递归溢出一节里，咱们用 bt 5 看过栈顶的 5 帧，整口栈 26 万层的深度是推算出来的。不过 bt 在咱们手里一直是个黑盒，您一回车，它就把函数名带着行号排队报了出来，好像天生就知道是谁调的谁。这一篇咱们把这个盒子打开，它干的活儿咱们亲手干一遍。它没辙的场子，帧指针被优化省掉了怎么办，符号被 strip 剥掉了怎么办，这个主题的后面几篇接着看。

路线是这样的。在带帧指针的构建里，调用链在栈上就是一条现成的链表，咱们用两条读内存的命令手工爬一遍，再跟 bt 的输出逐层比对。-O2 把帧指针省掉了，bt 却依然活得好好的，咱们查清楚它靠什么活着。名字的事咱们单独讲，strip 之后的裸地址，咱们用 addr2line 把函数名换回来。最后咱们把三样东西合起来，对付一个崩溃的、优化过的、剥过符号的程序，做一次完整的取证。前面八篇里的主角多数是汇编，这一篇的主角换成了 gdb，您跟着敲，每一步都能在自己的机器上复现。

您在文中看到的每一段会话、每一行输出，都逐字来自本机的真实运行，例子与实验也全部是本站的原创。

## 咱们的实验环境

照惯例咱们把环境摆在最前面，后面的每一块输出，您都可以在同样配置的机器上对照复现：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux
```

另有几条影响解读的事实，咱们一条条交代。

- 会话都是非交互跑的，命令从 heredoc 喂给 gdb，就像下面这样。`(gdb)` 提示符是 gdb 原样打印的，命令本身不会被回显，所以咱们在会话块里加了 `--- 命令 ---` 形式的注解行，标出每段输出来自哪条命令。注解行之外的每一个字符都是逐字的，删节的地方会注明。

```text
DEBUGINFOD_URLS= gdb -q ./chain_O0 <<'EOF'
break f3
run
bt
EOF
```

- 命令行最前面那个 `DEBUGINFOD_URLS=` 不是 gdb 的参数，它把环境变量清空，为的是跳过 gdb 17.2 开头那个 debuginfod 联网询问，省四行噪音，对回溯行为没有别的影响。
- gdb 默认关闭地址空间随机化，所以 PIE 程序每次都加载在 0x555555554000，libc 每次都在 0x7ffff7xxxxxx 一带。不同会话里的地址可以直接互相对着看，后面咱们要反复用到这一点。
- 每个会话结尾 gdb 都会追问一句要不要退出，非交互模式下它自动答 Y。这类结尾提示在后面的会话里统一删去了，此处注明一次。
- 崩溃取证的纪律沿用 [02 的判例](../02-function-calls-and-stack-frames/03-kept-frames-and-stack-exhaustion.md)：证据以 gdb 的 `Program received signal` 行为准，非交互 shell 层面只有退出码可看。

## bt 的正常输出长什么样

咱们从最干净的基准开始，把四个函数串成了一条链，从 main 到 f3 的调用一层层嵌下去，源码就是这样的：

```cpp
int f3(int x) { return x * 3; }
int f2(int x) { return f3(x + 1) + 1; }
int f1(int x) { return f2(x + 1) + 1; }
int main() { return f1(10); }
```

咱们用 -O0 加 -g 编译，把断点下到了 f3，跑起来看 bt 的输出：

```text
$ g++ -O0 -g -o chain_O0 chain.cpp
```

```text
Reading symbols from ./chain_O0...
--- break f3 ---
(gdb) Breakpoint 1 at 0x1120: file chain.cpp, line 1.
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/chain_O0
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, f3 (x=12) at chain.cpp:1
1	int f3(int x) { return x * 3; }
--- bt ---
(gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
```

正常的样子咱们见到了：bt 给了四层，每层带的都是函数名、参数值、文件行号。规则咱们说细一点，[gdb 手册的 Frames 词条](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Frames.html)（检索日期 2026-10-12）对帧的定义是 “each frame is the data associated with one call to one function”，咱们把它翻过来：每个帧就是与一次函数调用绑在一起的数据。

程序刚启动的时候，栈上就只有 main 的那一帧，咱们每调一次函数，栈上的帧就多一层，而每返回一次，栈上的帧也就少一层。bt 的输出规则是零号帧在最内层，也就是当前正停着的这层，一号帧是调用它的那层，咱们越往外数，编号的数字也就越大。手册的 [Backtrace 词条](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html)（检索日期 2026-10-12）开头的一句是 “A backtrace is a summary of how your program got where it is”，它回答的就是程序怎么走到了眼下这一步。

细心的您会注意到 bt 停在 main 就收住了。main 上面明明还有 libc 的启动函数在调用它，这一次的输出里却没有往上翻。咱们把这个疑问搁到这里，讲完下一篇符号的一节再来解释。

接下来咱们看 gdb 对单个帧知道多少。`frame n` 可以把当前帧切到编号 n 的那一层，`info frame` 则把这一帧的底细整份报出来。手册对它的说明是“打印所选栈帧的详细描述”，[Frame Info 词条](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Frame-Info.html)（检索日期 2026-10-12）的原文是 “This command prints a verbose description of the selected stack frame”，咱们接着往下用。咱们把 0 号、1 号、3 号帧挨个查了一遍：

```text
--- frame 0 ---
(gdb) #0  f3 (x=12) at chain.cpp:1
1	int f3(int x) { return x * 3; }
--- info frame ---
(gdb) Stack level 0, frame at 0x7fffffffd0b0:
 rip = 0x555555555120 in f3 (chain.cpp:1); saved rip = 0x555555555143
 called by frame at 0x7fffffffd0c8
 source language c++.
 Arglist at 0x7fffffffd0a0, args: x=12
 Locals at 0x7fffffffd0a0, Previous frame's sp is 0x7fffffffd0b0
 Saved registers:
  rbp at 0x7fffffffd0a0, rip at 0x7fffffffd0a8
--- frame 1 ---
(gdb) #1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
2	int f2(int x) { return f3(x + 1) + 1; }
--- info frame ---
(gdb) Stack level 1, frame at 0x7fffffffd0c8:
 rip = 0x555555555143 in f2 (chain.cpp:2); saved rip = 0x555555555160
 called by frame at 0x7fffffffd0e0, caller of frame at 0x7fffffffd0b0
 source language c++.
 Arglist at 0x7fffffffd0b8, args: x=11
 Locals at 0x7fffffffd0b8, Previous frame's sp is 0x7fffffffd0c8
 Saved registers:
  rbp at 0x7fffffffd0b8, rip at 0x7fffffffd0c0
--- frame 3 ---
(gdb) #3  0x0000555555555173 in main () at chain.cpp:4
4	int main() { return f1(10); }
--- info frame ---
(gdb) Stack level 3, frame at 0x7fffffffd0f0:
 rip = 0x555555555173 in main (chain.cpp:4); saved rip = 0x7ffff7827781
 caller of frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0e0, args:
 Locals at 0x7fffffffd0e0, Previous frame's sp is 0x7fffffffd0f0
 Saved registers:
  rbp at 0x7fffffffd0e0, rip at 0x7fffffffd0e8
```

info frame 的输出里，最有分量的是 `Saved registers:` 底下的两行。`rbp at 0x7fffffffd0a0` 说的是这一帧执行期间 $rbp 的值，`rip at 0x7fffffffd0a8` 给的是返回地址的住址，它正好处在前一个数加 8 的位置，也就是 $rbp+8 的那个槽位。02 讲 prologue 的时候咱们见过这套布局：`push rbp` 把调用者的 rbp 存进了栈，`mov rbp,rsp` 把新的栈顶定成基准，于是 [rbp] 里住的就是上一层的 rbp 旧值，[rbp+8] 住的则是返回地址。

更妙的是三处输出能互锁。咱们看 frame 0 报的 `saved rip = 0x555555555143`，它正对着 bt 里 #1 那一行的地址。frame 1 报的 `saved rip = 0x555555555160`，正对 bt #2 的地址。咱们看下来，bt 每一层显示的那个地址，就是它的下一层替它存好的返回地址。连 bt 没显示的第四跳也对得上：frame 3 的 `saved rip = 0x7ffff7827781`，是 main 返回 libc 的返回地址，它的住处就在栈上，而 bt 默认不往上翻。三个工具读的是同一份栈上的记录，这个互锁就是下一节咱们动手的依据。

## 手工回溯：不请 bt，咱们自己逐层取值

上一节立好了两个槽位的概念，这一回咱们把 bt 撇开，只用两个读内存的动作把整条链读出来。做法其实很简单：`p/x $rbp` 看 rbp 自己的值，`x/gx $rbp` 读出上一层的 rbp，`x/gx $rbp+8` 读出的就是返回地址。咱们爬完一层之后，用 `set` 命令把上一层的 rbp 接进一个新的 gdb 变量，再对那个变量重复读它的两个槽位。`set` 命令是没有输出的，所以会话里的提示符会连着出现两个，咱们在注解里标明了。

咱们还在同一个 chain_O0 上，断点照旧停在了 f3，会话咱们原样贴在下面（开头的 break 与 run 的输出和上一节相同，结尾的退出提示已经删去）：

```text
--- bt（对照用）---
(gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd0a0
--- x/gx $rbp ---
(gdb) 0x7fffffffd0a0:	0x00007fffffffd0b8
--- x/gx $rbp+8 ---
(gdb) 0x7fffffffd0a8:	0x0000555555555143
--- set $rbp_f2 = *(unsigned long *)$rbp（无输出），随后 p/x 新变量 ---
(gdb) (gdb) $2 = 0x7fffffffd0b8
--- x/gx 新变量，再读 +8 槽 ---
(gdb) 0x7fffffffd0b8:	0x00007fffffffd0d0
(gdb) 0x7fffffffd0c0:	0x0000555555555160
--- set 接住 f1 那层的 rbp（无输出），随后 p/x 新变量 ---
(gdb) (gdb) $3 = 0x7fffffffd0d0
--- 读它的两个槽位 ---
(gdb) 0x7fffffffd0d0:	0x00007fffffffd0e0
(gdb) 0x7fffffffd0d8:	0x0000555555555173
--- set 接住 main 那层的 rbp（无输出），随后 p/x 新变量 ---
(gdb) (gdb) $4 = 0x7fffffffd0e0
--- 读它的两个槽位 ---
(gdb) 0x7fffffffd0e0:	0x00007fffffffd190
(gdb) 0x7fffffffd0e8:	0x00007ffff7827781
```

咱们把爬出来的东西整理成表，跟 bt 的结果摆在一起：

| 层 | 该层 $rbp | [$rbp] 上一层 rbp | [$rbp+8] 返回地址 | bt 对应 |
|---|---|---|---|---|
| f3 | 0x7fffffffd0a0 | 0x7fffffffd0b8 | 0x0000555555555143 | #1 f2 |
| f2 | 0x7fffffffd0b8 | 0x7fffffffd0d0 | 0x0000555555555160 | #2 f1 |
| f1 | 0x7fffffffd0d0 | 0x7fffffffd0e0 | 0x0000555555555173 | #3 main |
| main | 0x7fffffffd0e0 | 0x7fffffffd190 | 0x00007ffff7827781 | bt 默认不显示 |

三个用户态的返回地址 0x143、0x160、0x173（咱们这里只写尾数，全值在上面的表格里），与 bt 的 #1 到 #3 逐个咬合。第四跳 0x00007ffff7827781 是 main 返给 libc 的返回地址，而 bt 没显示它，但上一节 frame 3 的 `saved rip` 报的就是这个数，两个来源对上了。表格还有一条规律值得您多看一眼：咱们看每一层的 [$rbp]，正好就是下一行里那层的 $rbp。0x7fffffffd0b8 在 f3 那行是咱们读出来的上层 rbp，到了 f2 那行就成了它自己的 $rbp。这正是 `push rbp; mov rbp,rsp` 两条指令执行后形成的东西，02 的汇编里咱们读到过它们，落进内存就成了这样一张单向链表，每个节点存着下一个节点的地址和一条回家的返回地址。

在带帧指针的构建里，链子在每一层上都是完好的，gdb 复原调用链的时候，顺着它走就是一条现成的路。咱们手工爬的结果与 bt 一字不差，这就是走得通的证明。您在别的材料里也许见过拿脚本顺 rbp 链解码调用栈的做法，干的就是刚才这一串动作。这套爬法成立的前提只有一个，链上的每个函数都得老老实实执行过 push rbp。
