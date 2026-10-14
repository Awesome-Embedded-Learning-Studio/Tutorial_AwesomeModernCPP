---
title: "内联与崩溃取证：三层依赖各管一段"
chapter: 9
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "内联在无调试信息时让 bt 少一层，再把 unwind 表、栈上排队的返回地址与 symbol-file 合起来，对一个 -O2 编的、strip 过的崩溃程序做完整取证"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 内联与崩溃取证：三层依赖各管一段

咱们在前一篇里把省掉的帧指针交给了 unwind 表，把剥掉的符号交给了 addr2line。咱们用的环境与会话的注解习惯与[第一篇](01-bt-baseline-and-rbp-chain.md)相同，您要复跑就照它来。动调用链形状的优化，咱们还剩内联一门没测，这一篇咱们看它让 bt 少不少一层，然后咱们把三样东西合起来，对付一个崩溃的、优化过的、剥过符号的程序，做一次完整的取证。

## 内联之后，bt 会少一层吗

优化还有一招会动调用链的形状：内联。咱们在 02 里读过，编译器把小函数的函数体直接展开进调用处，call 就没有了。照咱们的直觉，函数都没了调用，bt 里的那一层似乎也该跟着消失。咱们拿最小的例子实测，您带没带调试信息，答案是不一样的。

```cpp
int add_one(int x) { return x + 1; }
int main(int argc, char **) { return add_one(argc); }
```

```text
$ g++ -O0 -g -o inline_O0 inline.cpp
$ g++ -O2 -g -o inline_O2g inline.cpp
$ g++ -O2    -o inline_O2  inline.cpp
```

咱们从 -O0 -g 的 bt 看起（头一个会话删去了开头的 Reading symbols、break 与 run 输出，从停稳的地方贴起，中间那个会话只删了 Reading symbols 一行，最后一个会话的开头是原样保留的）：

```text
Breakpoint 1, add_one (x=1) at inline.cpp:1
1	int add_one(int x) { return x + 1; }
--- bt ---
(gdb) #0  add_one (x=1) at inline.cpp:1
#1  0x0000555555555141 in main (argc=1) at inline.cpp:2
```

bt 给了两帧，add_one 和 main 各占了一层，这就是咱们的正常基准。那 -O2 下发生了什么？咱们请机器码发言：

```text
$ objdump -d --no-show-raw-insn inline_O2g | sed -n '/<main>:/,/ret/p'
0000000000001020 <main>:
    1020:	lea    0x1(%rdi),%eax
    1023:	ret

$ objdump -d --no-show-raw-insn inline_O2 | sed -n '/<main>:/,/ret/p'
0000000000001020 <main>:
    1020:	lea    0x1(%rdi),%eax
    1023:	ret

$ nm inline_O2 | grep 'add_one\|T main'
0000000000001020 T main
0000000000001130 T _Z7add_onei
```

main 的函数体就剩一条 `lea 0x1(%rdi),%eax`，它就是 add_one 的函数体，call 已经没有了。带 -g 与不带 -g 的机器码一字不差。同时 add_one 的独立实体还留在 0x1130，而它没人调用也照样删不得，缘由咱们在 02 里读过：它是外部链接的，别的编译单元可能取它的地址。

现在咱们看两个版本的 bt，带调试信息的在前面：

```text
--- break add_one ---
(gdb) Breakpoint 1 at 0x1020: add_one. (2 locations)
--- run ---
(gdb) Breakpoint 1.1, add_one (x=<optimized out>) at inline.cpp:1
1	int add_one(int x) { return x + 1; }
--- bt ---
(gdb) #0  add_one (x=<optimized out>) at inline.cpp:1
#1  main (argc=1) at inline.cpp:2
--- info frame ---
(gdb) Stack level 0, frame at 0x7fffffffd0f0:
 rip = 0x555555555020 in add_one (inline.cpp:1); saved rip = 0x7ffff7827781
 inlined into frame 1
 source language c++.
 Arglist at unknown address.
 Locals at unknown address, Previous frame's sp in rsp
```

bt 还是给了两帧，add_one 的那层还在。但形态变了：#0 和 #1 都没有 `0x... in` 的地址前缀，因为两层共用的是同一个真实的 PC。`info frame` 更是把判定直接写了出来：`inlined into frame 1`，这一帧是内联进 1 号帧的合成帧。断点的输出也在说同一件事：`Breakpoint 1 at 0x1020: add_one. (2 locations)`，函数名后面跟着的句点和两处位置，一处是 main 里的内联实例，一处是 0x1130 的独立副本，咱们停的是 1.1，也就是 main 里的内联实例。合成帧的依据是 DWARF 调试信息里记录的内联关系，gdb 读它把消失的那层补了回来。

不带调试信息的版本就直白了。咱们在同一个 PC 上再停一次，bt 就只剩一帧了：

```text
Reading symbols from ./inline_O2...
(No debugging symbols found in ./inline_O2)
--- 按地址下断点 ---
(gdb) Breakpoint 1 at 0x555555555020
--- run ---
(gdb) Breakpoint 1, 0x0000555555555020 in main ()
--- bt ---
(gdb) #0  0x0000555555555020 in main ()
```

所以那句话咱们得分场景说。真实栈帧确实少了一层，机器码里连 call 的影子都没有，这件事不因调试信息而变。而 bt 报出几层，取决于 gdb 手里有什么：没有调试信息的时候，它只能报告物理上的帧，两层就变一层了。而带着 -g 的时候，它就读 DWARF 把内联层合成了回来，条数倒是没减，形态也认得出来了。您以后读到“内联让 bt 少一层”的说法，对方多半是在无调试信息的场景里说的。

## 实战：一个 strip 过的 -O2 程序崩了

三样条件凑齐的场景，就是真实的崩溃现场：程序用 -O2 编的，帧指针没了，符号还剥了。咱们故意造一个。自造野指针的手法是用左移，造一个运行期才确定的非法地址：

```cpp
__attribute__((noinline)) int f3(long *p) { *p = 42; return 0; }
__attribute__((noinline)) int f2(long *p) { return f3(p) + 1; }
__attribute__((noinline)) int f1(long *p) { return f2(p) + 1; }
int main(int argc, char **) {
  long *p = reinterpret_cast<long *>(static_cast<long>(argc) << 44);
  return f1(p) + 1;
}
```

咱们在 gdb 里跑，argc 的值是 1，左移 44 位就得到了 0x100000000000，这个值的揭晓得等运行期，编译器也就没法把这个写入删掉了。咱们编译、复制、strip，再用反汇编确认了 call 确实都在，而帧指针则不见了：

```text
$ g++ -O2 -g -o crash crash.cpp
$ cp crash crash.stripped
$ strip crash.stripped
$ objdump -d --no-show-raw-insn crash | sed -n '/^0000000000001130 <_Z2f3Pl>:/,/^$/p;/^0000000000001140 <_Z2f2Pl>:/,/^$/p;/^0000000000001150 <_Z2f1Pl>:/,/^$/p'
0000000000001130 <_Z2f3Pl>:
    1130:	movq   $0x2a,(%rdi)
    1137:	xor    %eax,%eax
    1139:	ret
    113a:	nopw 0x0(%rax,%rax,1)

0000000000001140 <_Z2f2Pl>:
    1140:	call   1130 <_Z2f3Pl>
    1145:	mov    $0x1,%eax
    114a:	ret
    114b:	nopl  0x0(%rax,%rax,1)

0000000000001150 <_Z2f1Pl>:
    1150:	call   1140 <_Z2f2Pl>
    1155:	mov    $0x2,%eax
    115a:	ret
```

main 的反汇编咱们只提开头：`shl $0x2c,%rdi` 之后跟一条 call，全文就不粘贴了。

取证咱们现在开始，咱们的头一步是把 strip 版在 gdb 里跑起来：

```text
Reading symbols from ./crash.stripped...
(No debugging symbols found in ./crash.stripped)
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/crash.stripped
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555130 in ?? ()
--- bt ---
(gdb) #0  0x0000555555555130 in ?? ()
#1  0x0000555555555145 in ?? ()
#2  0x0000555555555155 in ?? ()
#3  0x0000555555555029 in ?? ()
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555055 in ?? ()
--- x/i $pc ---
(gdb) => 0x555555555130:	movq   $0x2a,(%rdi)
--- p/x $rdi ---
(gdb) $1 = 0x100000000000
--- x/12gx $rsp ---
(gdb) 0x7fffffffd0c0:	0x0000555555555145	0x0000555555555155
0x7fffffffd0d0:	0x0000555555555029	0x00007ffff7827781
0x7fffffffd0e0:	0x00007ffff7ecf248	0x00007fffffffd208
0x7fffffffd0f0:	0x0000000100000008	0x0000555555555020
0x7fffffffd100:	0x0000000000000000	0x0000000000000009
0x7fffffffd110:	0x0000000000000000	0x921a7316ea03ae4c
```

咱们用五个动作换来了五份证据。`Program received signal SIGSEGV` 把崩溃指令定在了 0x555555555130，`x/i $pc` 把出事的指令摆了出来：`movq $0x2a,(%rdi)`，干的是往 [rdi] 写 42 的活，正对上源码里的 `*p = 42`。`p/x $rdi` 则报出了 0x100000000000，正是 argc 左移 44 位的结果。bt 拿到了七个裸地址，形状与符号一节的规律一致：#5 的名字是 libc 的 `.dynsym` 在撑，多出的尾部是认不出 main 的连带效果。

第五份证据是本次实战独有的手法。咱们看 `x/12gx $rsp` 的头四个 8 字节：0x0000555555555145、0x0000555555555155、0x0000555555555029、0x00007ffff7827781，与 bt 的 #1 到 #4 一个不差。在无帧指针的世界里，返回地址倒是没有消失，只是换了住址：链上 f3 一进门就直接写了内存，f2 与 f1 的函数体只有 call 加一条 mov，而 main 的贴出片段里只有 shl 加 call，谁都没压过别的栈，四次 call 压下的返回地址在栈上连续相邻，从 $rsp 起就一路排开了。识别的办法也简单，值落在 0x555555555000 一带的程序文本段区间，或者 0x7ffff7xxxxxx 的已加载库区间，就是返回地址的候选，咱们逐个验证就行。就算 bt 完全失效了，头四个字也够咱们把调用链手工拼出来了。

咱们的第二步是 addr2line 还原名字，咱们照旧把算式摆出来：

```text
0x555555555130 - 0x555555554000 = 0x1130
0x555555555145 - 0x555555554000 = 0x1145
0x555555555155 - 0x555555554000 = 0x1155
0x555555555029 - 0x555555554000 = 0x1029
```

咱们对着含调试信息的 crash 查四个链接期地址：

```text
$ addr2line -e crash -f -C 0x1130 0x1145 0x1155 0x1029
f3(long*)
/tmp/cpu-book-09/crash.cpp:1
f2(long*)
/tmp/cpu-book-09/crash.cpp:2
f1(long*)
/tmp/cpu-book-09/crash.cpp:3
main
/tmp/cpu-book-09/crash.cpp:7
```

帧 #0 到 #3 翻回来了：f3、f2、f1、main。第三步，gdb 会话内还有一条更快的路。咱们用 `symbol-file` 把未 strip 版的符号，原地换进还停在崩溃点上的进程：

```text
--- symbol-file ./crash ---
(gdb) Reading symbols from ./crash...
--- bt ---
(gdb) #0  f3 (p=p@entry=0x100000000000) at crash.cpp:1
#1  0x0000555555555145 in f2 (p=p@entry=0x100000000000) at crash.cpp:2
#2  0x0000555555555155 in f1 (p=p@entry=0x100000000000) at crash.cpp:3
#3  0x0000555555555029 in main (argc=<optimized out>) at crash.cpp:6
```

咱们不重跑进程，bt 立刻恢复了名字、行号、参数，连 p=0x100000000000 都层层带了回来。能这样做的前提是两个文件的 BuildID 相同，也就是同一份构建出来的产物，咱们的 crash.stripped 本来就是从 crash 复制出去再 strip 的，条件正好是合格的。

有个小出入咱们得交代一下，也定下一条引用的纪律。咱们拿 0x555555555029 这个地址来说，gdb 恢复符号之后归到了 crash.cpp:6，也就是 `return f1(p) + 1;` 所在的那一行，addr2line 却报了第 7 行，也就是右花括号的那一行。两个工具查的是同一份行表，取的行不同，倒是谁也不算错。您在报告里引用行号时，咱们把两个来源分开写，别把它们混排成同一行的双重证据。

第四步是 shell 层的佐证，咱们退出 gdb 到普通 shell 里跑一遍：

```text
$ ./crash.stripped; echo "exit=$?"
exit=139
```

139 减掉 128 得到的是 11，正是 SIGSEGV 的编号。咱们按 02 立下的纪律，非交互 shell 里不打印崩溃的文本，这一行退出码就是 shell 层的全部证据，事故的细节只在 gdb 的 `Program received signal` 行里。

## 三层依赖，各管一段

把这三篇走过的路收拢一下，bt 依赖三层各自独立的东西。栈上的帧指针链是 `push rbp; mov rbp,rsp` 两条指令执行后形成的单向链表，-O0 的世界里咱们逐层取值走的就是它，每层的两个槽位一读一个准。省掉帧指针之后接棒的是 `.eh_frame` 里的 unwind 表，CIE 里那两行配方给每个函数标好了返回地址住在哪，而 strip 对它无可奈何，C++ 的异常展开靠的也是它。名字这一层靠的则是符号表，strip 删的正是它，还原的路子有两条：您走离线的 addr2line 就得记着减基址，您走会话内的 symbol-file，停着的进程就能原地恢复。三层各有失效的场合，也各有各的替补，帧指针断了链的时候有 unwind 表接手，符号没了的时候有 addr2line 补名字，全都指望不上的时候，您还可以像实战里那样，去认栈上排队的返回地址，认下的每一个都验上一遍。

往前的线头也接得上。栈帧与 prologue 的地基在 [02](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md)，崩溃取证的完整传统在 [crash-lab 的空指针现场](../../../crash-lab/a-memory-safety/01-null-deref.md)，那里把这套手艺用在了真教训上。您要是想在程序运行中自己拿到调用栈，不借 gdb 的手，C++23 的 std::stacktrace 把这件事包成了库，链接参数的细节，在 [cpp-reference 的 stacktrace 参考卡](../../../cpp-reference/core-language/19-stacktrace.md)里。

最后咱们把实战会话里 symbol-file 之后的那行 bt 再摆一次，算作这一篇的落点：`#0 f3 (p=p@entry=0x100000000000) at crash.cpp:1`。从一条写崩了的 movq，到带着肇事指针值的完整调用链，中间隔着的只是一条换符号的命令。
