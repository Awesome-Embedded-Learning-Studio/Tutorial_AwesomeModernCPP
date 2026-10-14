---
title: "帧指针省了，符号剥了：unwind 表与 addr2line"
chapter: 9
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "-O2 省掉帧指针后 rbp 里留的是 libc 的旧值，bt 改靠 .eh_frame 的 unwind 表活着，strip 剥掉符号后，addr2line 减基址把函数名换回来"
reading_time_minutes: 15
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 帧指针省了，符号剥了：unwind 表与 addr2line

上一篇咱们在带帧指针的 -O0 构建里手工爬了 rbp 链，每层的两个槽位一读，整条链就跟 bt 的输出逐层咬合了。咱们用的环境、会话的注解习惯与[上一篇](01-bt-baseline-and-rbp-chain.md)相同，您要复跑就照它来。这一篇咱们把帧指针抽掉，看看 rbp 链爬上的是什么、bt 又靠什么活着，然后咱们把符号也剥掉，看看咱们怎么用 addr2line 从裸地址里把名字换回来。

## -O2 之后：帧指针省了，bt 还活着

咱们在 02 里读过，`-fomit-frame-pointer` 这个开关从 -O1 起就是默认打开的，-O2 的函数里不再有 `push rbp` 那一套。帧指针链断了，bt 却照样把结果交了出来，本节咱们把 bt 依赖的东西找出来。不过在演示之前，有一个前置事实咱们必须交代，不然您自己一试就会觉得咱们在瞎说。

咱们把同一份 chain.cpp 直接换 -O2 编译，整条调用链会整个没了，连一层的影子都见不着了：

```text
$ g++ -O2 -g -o chain_O2_plain chain.cpp
$ objdump -d --no-show-raw-insn chain_O2_plain | sed -n '/<main>:/,/^$/p'
0000000000001020 <main>:
    1020:	mov    $0x26,%eax
    1025:	ret
    1026:	cs nopw 0x0(%rax,%rax,1)
```

main 的全部机器码只剩一条 `mov $0x26,%eax`。0x26 的十进制就是 38，f1(10) 沿着 f2、f3 算下去的结果正好是 38，编译期就把值折完了，连一次 call 的机会都没留。调用都没有了，回溯也就没有了对象。所以想看省帧指针但调用链还在的样子，咱们得换一个受控的变体，让四个函数在 -O2 下仍是真实的 call：

```cpp
__attribute__((noinline)) int f3(int x) { return x * 3; }
__attribute__((noinline)) int f2(int x) { return f3(x + 1) + 1; }
__attribute__((noinline)) int f1(int x) { return f2(x + 1) + 1; }
int main(int argc, char **) { return f1(argc) + 1; }
```

三个 `noinline` 挡住的是内联，参数换成运行期才知道的 argc 挡住常量折叠。main 里那个 `+ 1` 也是必要的，笔者头一版没加它，翻过车：缺了 +1 的话，main 对 f1 的调用就成了尾调用，-O2 下的 call 会变成 jmp，f1 的返回地址直接就是 main 的调用者，main 自己的帧就根本不出现了，bt 里也就数不出 main 了。而加了 +1 之后，main 在 call 之后还有自己的活要干，帧就保住了。

咱们把两份都编出来，一份是 -O2 的、一份是 -O0 的，然后用反汇编确认 call 还在、帧指针不见了：

```text
$ g++ -O2 -g -o chain_ni_O2 chain_ni.cpp
$ g++ -O0 -g -o chain_ni_O0 chain_ni.cpp
$ objdump -d --no-show-raw-insn chain_ni_O2 | grep -A6 '<_Z2f3i>:\|<_Z2f2i>:\|<main>:'
0000000000001020 <main>:
    1020:	call   1150 <_Z2f1i>
    1025:	add    $0x1,%eax
    1028:	ret
    1029:	nopl   0x0(%rax)

0000000000001130 <_Z2f3i>:
    1130:	lea    (%rdi,%rdi,2),%eax
    1133:	ret
    1134:	nop

0000000000001140 <_Z2f2i>:
    1140:	add    $0x1,%edi
    1143:	call   1130 <_Z2f3i>
    1148:	add    $0x1,%eax
    114b:	ret

$ objdump -d --no-show-raw-insn chain_ni_O2 | sed -n '/^0000000000001150 <_Z2f1i>:/,/^$/p'
0000000000001150 <_Z2f1i>:
    1150:	add    $0x1,%edi
    1153:	call   1140 <_Z2f2i>
    1158:	add    $0x1,%eax
    115b:	ret
```

四个函数的 prologue 都没有了，f3 干脆就剩下了 lea 加 ret。咱们再拿 -O0 的版本对照，-O0 下同一个 f3 的开头，就是咱们在 02 里见惯的标准样子：

```text
$ objdump -d --no-show-raw-insn chain_ni_O0 | grep -A8 '<_Z2f3i>:'
0000000000001119 <_Z2f3i>:
    1119:	push   %rbp
    111a:	mov    %rsp,%rbp
    111d:	mov    %edi,-0x4(%rbp)
    1120:	mov    -0x4(%rbp),%edx
    1123:	mov    %edx,%eax
    1125:	add    %eax,%eax
    1127:	add    %edx,%eax
    1129:	pop    %rbp
```

接下来咱们把断点停在 -O2 版的 f3 上，看 bt 的输出，再用老办法爬一遍 rbp 的链子：

```text
Reading symbols from ./chain_ni_O2...
--- break f3 / run ---
(gdb) Breakpoint 1 at 0x1130: file chain_ni.cpp, line 1.
(gdb) Starting program: /tmp/cpu-book-09/chain_ni_O2
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, f3 (x=x@entry=3) at chain_ni.cpp:1
1	__attribute__((noinline)) int f3(int x) { return x * 3; }
--- bt ---
(gdb) #0  f3 (x=x@entry=3) at chain_ni.cpp:1
#1  0x0000555555555148 in f2 (x=2) at chain_ni.cpp:2
#2  0x0000555555555158 in f1 (x=<optimized out>) at chain_ni.cpp:3
#3  0x0000555555555025 in main (argc=<optimized out>) at chain_ni.cpp:4
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd190
--- x/gx $rbp ---
(gdb) 0x7fffffffd190:	0x00007fffffffd1f0
--- x/gx $rbp+8 ---
(gdb) 0x7fffffffd198:	0x00007ffff78278b9
```

bt 里的参数显示成 `x=x@entry=3`、`<optimized out>`，咱们在这里说一句：这是 gdb 在 -O2 下的正常形态，参数只在进门的那一瞬间可查，过后寄存器就挪作他用了，咱们不把它当异常。bt 本身倒是四层完好，而且报的每个地址都能跟反汇编对上。f2 的 call 位于 0x1143，call 的下一条指令在 0x1148，咱们再加上基址 0x555555554000，正好就是 bt #1 的 0x555555555148。f1 的 call 返回落在 0x1158，对应的就是 bt #2。main 的 call 返回落在 0x1025，对应的就是 bt #3。咱们一层一层数了下来，每一层都对上了。

再看咱们手工爬的三个数，味道就不对了。$rbp 的值是 0x7fffffffd190，它的两个槽位里躺着 0x7fffffffd1f0 和 0x00007ffff78278b9，三个数跟 bt 四层里的任何一层都对不上。可是爬的过程畅通无阻，咱们没有看到报错，乱码的影子也不见，读出来的都是老老实实的栈地址。这是本篇最要紧的一个观察，咱们把话说重一点：帧指针链失效的方式很特别，它不给您垃圾值，咱们顺着它爬上去，走的就是一条真实存在但完全错误的链。

爬上的链是谁的？咱们请 `info symbol` 来查，它管的事就是回答一个地址落在哪个符号里。除了刚读到的 0x7ffff78278b9，咱们还把 -O0 会话表格里出现过的第四跳 0x7ffff7827781 一并查了，看看它在符号表里的身份：

```text
Reading symbols from ./chain_ni_O2...
--- break f3 / run（输出与上一会话相同，略）---
--- info symbol 0x7ffff78278b9 ---
(gdb) __libc_start_main + 137 in section .text of /usr/lib/libc.so.6
--- info symbol 0x7ffff7827781 ---
(gdb) No symbol matches 0x7ffff7827781.
--- p/x $rbp ---
(gdb) $1 = 0x7fffffffd190
--- set $r = *(unsigned long *)$rbp（无输出），随后 p/x $r ---
(gdb) (gdb) $2 = 0x7fffffffd1f0
--- x/gx $r+8 ---
(gdb) 0x7fffffffd1f8:	0x0000555555555055
--- info symbol *(unsigned long *)($r+8) ---
(gdb) _start + 37 in section .text of /tmp/cpu-book-09/chain_ni_O2
```

谜底揭开了。咱们查出来的 0x7ffff78278b9 是 `__libc_start_main + 137`，0x555555555055 报的是本程序自己的 `_start + 37`，中间那个查不到符号的 0x7ffff7827781，落在 libc 内部一个没有导出名字的函数里。咱们顺着链子往上爬，爬到的是 libc 启动过程自己的帧：f3、f2、f1、main，用户代码的帧一层都不在链上。还有个印证让您看得更透：$rbp 的值 0x7fffffffd190，正是前面 -O0 会话的表格里 main 那一层的 [$rbp]。libc 的启动函数把 rbp 设成了 0x7fffffffd190，而 f3、f2、f1、main 就没有一个碰过 rbp，咱们断点停进 f3 时读到的，还是人家留下的旧值。它是合法的，它指向的也是真实的帧，它就不是您的调用链。

您体会一下这个失效形态的险恶。如果 rbp 里装的是乱码，读两步就会读到离谱的地址，您自然会起疑。可是爬的全程畅通，每一跳落的都是合法地址，`info symbol` 还能查出体面的名字，不知情的您多半就把它当成了答案。乱码会替您报警，而错链不会。

那么 bt 靠什么活？靠的是 unwind 信息。咱们在 02 讲省帧指针的时候读过一句：想从当前的指令位置恢复调用链，得靠记录每个函数栈布局的 unwind 数据一层层去算，`.cfi` 伪指令就是它的源头。现在咱们把它的成品挖出来看，它住在 ELF 文件的 `.eh_frame` 节里：

```text
$ readelf -S chain_ni_O2 | grep -i 'eh_frame'
  [13] .eh_frame_hdr     PROGBITS         0000000000002004  00002004
  [14] .eh_frame         PROGBITS         0000000000002038  00002038
```

`.eh_frame` 是编译器产出的运行时回溯表，`.eh_frame_hdr` 是它前面带搜索表的索引。表的内容按 CIE 与 FDE 两条一组来组织，咱们看清单：

```text
$ readelf -wf chain_ni_O2 | grep -n 'FDE\|CIE'
18:00000000 0000000000000014 00000000 CIE
30:00000018 0000000000000014 0000001c FDE cie=00000000 pc=0000000000001030..0000000000001056
38:00000030 0000000000000010 00000034 FDE cie=00000000 pc=0000000000001130..0000000000001134
43:00000044 0000000000000010 00000048 FDE cie=00000000 pc=0000000000001140..000000000000114c
48:00000058 0000000000000010 0000005c FDE cie=00000000 pc=0000000000001150..000000000000115c
53:0000006c 0000000000000010 00000070 FDE cie=00000000 pc=0000000000001020..0000000000001029
```

每条 FDE 用 pc 区间盖住一段代码：起自 0x1130 的盖住 f3，起自 0x1140 的盖住 f2，起自 0x1150 的盖住 f1，起自 0x1020 的盖住 main。每个函数都有自己的一条，漏掉的函数一个都没有。CIE 是它们共享的默认规则，全文倒是很短，咱们整份读下来：

```text
$ readelf -wf chain_ni_O2 | sed -n '/^00000000 .* CIE/,/^$/p'
00000000 0000000000000014 00000000 CIE
  Version:               1
  Augmentation:          "zR"
  Code alignment factor: 1
  Data alignment factor: -8
  Return address column: 16
  Augmentation data:     1b
  DW_CFA_def_cfa: r7 (rsp) ofs 8
  DW_CFA_offset: r16 (rip) at cfa-8
  DW_CFA_nop
  DW_CFA_nop
```

末尾两条指令合起来就是配方的正文，咱们把它翻译成人话。`DW_CFA_def_cfa: r7 (rsp) ofs 8` 说的是：咱们回溯某一帧时，基准地址 CFA 的取值等于 $rsp 加 8。`DW_CFA_offset: r16 (rip) at cfa-8` 说的是：返回地址存在 CFA 减 8 的位置。咱们把两式合一，返回地址住的就是 [$rsp]。再对照 f3 的 FDE（命令取的是 30 到 48 行，输出里还有 0x1030、0x1140 等别的 FDE，咱们只留下 0x1130 对应 f3 的那一段，其余的删节）：

```text
$ readelf -wf chain_ni_O2 | sed -n '30,48p'
00000030 0000000000000010 00000034 FDE cie=00000000 pc=0000000000001130..0000000000001134
  DW_CFA_nop
  DW_CFA_nop
  DW_CFA_nop
```

f3 的 FDE 只有三条 nop，意思是全程沿用 CIE 的默认规则。f3 的身份是叶子函数，压栈的事一条都没有，躺在栈顶的就是返回地址，CFA 配方说的 [$rsp] 分毫不差。gdb 停在 f3 时读一下 $rsp 的值，就把回 f2 的返回地址拿到了，而全程没有碰 rbp。f2、f1、main 的 FDE 同样极短，后续每条的 nop 内容略去了，readelf 的原样输出咱们在此截断。函数体里 call 前后的栈布局一变，FDE 就会在对应的位置补一条修正，回溯器沿着指令地址查表，逐层套用对应的公式，就把没有帧指针的调用链算了出来。咱们在 02 里删过的那些 `.cfi_*` 伪指令，汇编之后进了 ELF，就是眼前的成品表。

还有一层关系值得咱们记下：`.eh_frame` 是 ELF 的运行时回溯表，而不算调试信息。C++ 在抛异常找 catch 的路上走的是它，栈展开逐层回收对象走的也是它。这层身份咱们在下一节马上就要用到。

## 符号被剥之后：bt 变成裸地址

bt 的输出里有两样东西：调用链的形状，和挂在每一层上的名字。前一样靠栈上的记录与 unwind 表，后一样靠的则是符号表。咱们把符号表剥掉，看看两样东西各自变成什么样。咱们拿上一篇基准节的 chain_O0 复制一份再 strip：

```text
$ cp chain_O0 chain_O0s
$ strip chain_O0s
$ file chain_O0 chain_O0s
chain_O0:  ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=c35dfbc2d99926338b8d56a9fc1fdb63c82f4333, for GNU/Linux 4.4.0, with debug_info, not stripped
chain_O0s: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV), dynamically linked, interpreter /lib64/ld-linux-x86-64.so.2, BuildID[sha1]=c35dfbc2d99926338b8d56a9fc1fdb63c82f4333, for GNU/Linux 4.4.0, stripped

$ nm chain_O0 | grep ' T '
0000000000001178 T _fini
0000000000001000 T _init
0000000000001165 T main
0000000000001020 T _start
0000000000001148 T _Z2f1i
000000000000112b T _Z2f2i
0000000000001119 T _Z2f3i

$ nm chain_O0s 2>&1 | head -3
nm: chain_O0s: no symbols
```

两个文件的 BuildID 相同，strip 只删了内容而没改布局。f3 的链接期地址是 0x1119，咱们拿基址 0x555555554000 加上 0x1119，算出来的运行期地址就是 0x555555555119。断点咱们只能按这个地址下了：符号没了之后，按函数名下断点的路会被 gdb 挡回来，它报的是 No symbol table is loaded，这本身就是 strip 的代价。bt 出来就成了下面的样子：

```text
Reading symbols from ./chain_O0s...
(No debugging symbols found in ./chain_O0s)
--- break *0x555555555119 ---
(gdb) Breakpoint 1 at 0x555555555119
--- run ---
(gdb) Starting program: /tmp/cpu-book-09/chain_O0s
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, 0x0000555555555119 in ?? ()
--- bt ---
(gdb) #0  0x0000555555555119 in ?? ()
#1  0x0000555555555143 in ?? ()
#2  0x0000555555555160 in ?? ()
#3  0x0000555555555173 in ?? ()
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555045 in ?? ()
```

咱们核对下来，名字全成了 `??`，但链的形状完好：#1 到 #3 的三个返回地址，与上一篇基准节里未 strip 版 bt 对应层的地址逐个相同，#0 的停点两边下得不同，咱们不把它算进比较。回溯本身倒是没受什么影响，这又印证了上一节的判断，bt 找调用链靠的是 `.eh_frame`，strip 删的是 `.symtab` 与调试信息，而没有删它。丢掉的东西只有一样：从地址查回名字的对照表。

有两个细节值得咱们停下来看。头一个细节是 #5 的名字居然还在。libc 的身份是共享库，它导出的符号放在 `.dynsym` 里，那是运行时动态链接要用的，strip 删的只是主程序自己的 `.symtab`，而管不到 libc 的头上。咱们查过本程序的 `.dynsym`，里面的用户函数一个都没有：

```text
$ nm -D chain_O0s
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
                 U __libc_start_main@GLIBC_2.34
```

第二个细节更反直觉：strip 之后的 bt 反而变长了，多出了 #4 到 #6 三层，一路翻到了 `_start`。删了东西，输出倒多了，这不是回溯变强了。咱们回头把上一篇基准节留下的那个疑问收回来，说说 bt 为什么停在 main。gdb 有个叫 `backtrace past-main` 的设置，默认是关闭的，[Backtrace 词条设置节](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Backtrace.html)（检索日期 2026-10-12）的原文是 “Backtraces will stop when they encounter the user entry point”，后面紧跟着的一句是 This is the default，说的就是默认值。遇到用户入口的时候它就停，这就是默认的行为。但这句话的背后藏着一个前提：认出 main 的位置，靠的是符号。符号没了，gdb 就认不出用户入口了，于是才一路翻到了 `_start`。咱们在未 strip 的版本上把这个设置打开验证（咱们把会话停在同一个 f3 断点，break、run 的输出与上一篇基准节的相同，就不重复贴了）：

```text
--- set backtrace past-main on（无输出）---
(gdb) (gdb) #0  f3 (x=12) at chain.cpp:1
#1  0x0000555555555143 in f2 (x=11) at chain.cpp:2
#2  0x0000555555555160 in f1 (x=10) at chain.cpp:3
#3  0x0000555555555173 in main () at chain.cpp:4
#4  0x00007ffff7827781 in ?? () from /usr/lib/libc.so.6
#5  0x00007ffff78278b9 in __libc_start_main () from /usr/lib/libc.so.6
#6  0x0000555555555045 in _start ()
```

bt 给了七帧，每一层的地址跟 strip 版的一字不差。所以 strip 版多出的三层不是新信息，只是同一份调用链露出的部分变多了。这件事顺带告诉咱们一个不那么显眼的事实：bt 停在哪一层的这件事，原来也是符号说了算的。

名字没了，咱们怎么把它找回来？还原的路子有两条，咱们从 addr2line 走起。它从含调试信息的文件里把地址翻回函数名与行号，有两个前置条件咱们得交代。

头一个要对付的是 PIE。file 输出里那个 pie 的意思是位置无关可执行文件：它没有链接期定死的代码地址，加载的基址由操作系统在运行期挑。gdb 关了随机化，基址也就固定成了 0x555555554000，可 addr2line 查的是文件里的链接期地址，基址的事它并不知道。所以喂给它的地址得咱们手工减过基址。咱们把四个减法一条一条摆出来：

```text
$ for a in 0x555555555119 0x555555555143 0x555555555160 0x555555555173; do printf '%s - 0x555555554000 = 0x%x\n' $a $((a - 0x555555554000)); done
0x555555555119 - 0x555555554000 = 0x1119
0x555555555143 - 0x555555554000 = 0x1143
0x555555555160 - 0x555555554000 = 0x1160
0x555555555173 - 0x555555554000 = 0x1173
```

把减法写成显式的一步，是笔者用一次翻车换来的习惯。笔者把 0x555555555143 减成了 0x143，少减了一位，addr2line 对整批地址返回了 `??`，一个字的抱怨都没有。`??` 在这里有两种来源：一种是地址减错的，另一种是文件里没有调试信息的。两种情况它给的都是一样的 `??`，不替您区分。所以咱们留了对照实验，咱们对着 strip 版查了同一批地址：

```text
$ addr2line -e chain_O0 -f -C 0x1119 0x1143 0x1160 0x1173
f3(int)
/tmp/cpu-book-09/chain.cpp:1
f2(int)
/tmp/cpu-book-09/chain.cpp:2 (discriminator 1)
f1(int)
/tmp/cpu-book-09/chain.cpp:3 (discriminator 1)
main
/tmp/cpu-book-09/chain.cpp:4 (discriminator 1)

$ addr2line -e chain_O0s -f -C 0x1119 0x1143 0x1160 0x1173
??
??:0
??
??:0
??
??:0
??
??:0
```

咱们对着未 strip 的 chain_O0 查，四个地址就翻回了四个函数名加行号，与 bt 报的层层对应。咱们再对着 strip 版查，得到的整批都是 `??`。行号后面的 discriminator 是行表里的附加标记，不影响咱们认行号。第二个前置条件就是它了：`-e` 指向的文件必须含调试信息。两个前置各有自己的翻车形态，减基址错了得到的是 `??`，文件不对得到的还是 `??`，咱们把算式摆出来，把对照也做了，哪一种都藏不住了。帧指针与符号这两头的麻烦，咱们就都对付过去了。
