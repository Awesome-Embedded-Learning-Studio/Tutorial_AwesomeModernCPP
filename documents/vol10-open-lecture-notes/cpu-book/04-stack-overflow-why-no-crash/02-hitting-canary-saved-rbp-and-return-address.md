---
title: "往远处打：金丝雀、saved rbp 与返回地址"
chapter: 4
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "把越界下标一路加大：打到金丝雀在返回前被拦下，打到返回地址控制流当场被劫走，打到 saved rbp 则崩在调用者身上"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 往远处打：金丝雀、saved rbp 与返回地址

上一篇的那一次越界打得很近，伤到的只有一个数据邻居。咱们用的环境与[上一篇](01-stack-frame-neighbors.md)相同，您要复跑就照它来。咱们接着问：打远一点会怎样？咱们把下标改成从命令行传入，同一份源码咱们编两回，一回用的是默认档（带金丝雀），一回加的是 -fno-stack-protector（拆掉保护），两套布局的下标换算咱们都从反汇编里量好了：

```cpp
// d3.cpp —— 越界写直达金丝雀 / saved rbp / 返回地址
// 下标由命令行传入。同一份源码分别用默认(带栈保护)与 -fno-stack-protector 编译
#include <cstdio>
#include <cstdlib>

volatile int g_index   = 0;
volatile int g_payload = 0x1234567; // 写进目标槽的值，坏地址的低 32 位一眼可认

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    arr[g_index] = g_payload; // 越界写
    std::fprintf(stderr, "store done (idx=%d), now return\n", (int)g_index);
}

int main(int argc, char** argv) {
    g_index = std::atoi(argv[1]);
    f();
    std::fprintf(stderr, "main: returned from f normally\n");
    return 0;
}
```

d3 的进度打印全部走 stderr，它是不带缓冲的，崩在半路也不丢已打印的行。两套构建的 f 咱们各贴一份（fprintf 参数搬运段已删节，函数头行尾的档位标注是咱们加的）：

```text
$ g++ -O0 -g -o d3_default d3.cpp
$ g++ -O0 -g -fno-stack-protector -o d3_noprot d3.cpp
```

```asm
0000000000001169 <f()>:                （默认构建，带金丝雀）
    1169:	push   %rbp
    116a:	mov    %rsp,%rbp
    116d:	sub    $0x20,%rsp
    1171:	mov    %fs:0x28,%rax
    117a:	mov    %rax,-0x8(%rbp)
    117e:	xor    %eax,%eax
    1180:	movl   $0x1,-0x20(%rbp)
    ...（arr[1..3] 初始化，删节）...
    11aa:	mov    %edx,-0x20(%rbp,%rax,4)
    ...（fprintf 参数搬运，删节）...
    11d3:	mov    -0x8(%rbp),%rax
    11d7:	sub    %fs:0x28,%rax
    11e0:	je     11e7 <f()+0x7e>
    11e2:	call   1040 <__stack_chk_fail@plt>
    11e7:	leave
    11e8:	ret
```

```asm
0000000000001159 <f()>:                （去保护构建）
    1159:	push   %rbp
    115a:	mov    %rsp,%rbp
    115d:	sub    $0x10,%rsp
    1161:	movl   $0x1,-0x10(%rbp)
    1168:	movl   $0x2,-0xc(%rbp)
    116f:	movl   $0x3,-0x8(%rbp)
    1176:	movl   $0x4,-0x4(%rbp)
    117b:	mov    0x2ea5(%rip),%edx        # 4028 <g_payload>
    1183:	mov    0x2ec3(%rip),%eax        # 404c <g_index>
    1189:	cltq
    118b:	mov    %edx,-0x10(%rbp,%rax,4)
    118f:	mov    0x2eb7(%rip),%edx        # 404c <g_index>
    ...（fprintf 参数搬运，删节）...
    11b3:	nop
    11b4:	leave
    11b5:	ret
```

默认版的 f 与上一篇 d1 里的 f 是同形的，金丝雀的位置在 rbp-0x8，数组的基址在 rbp-0x20。去保护版的 f 里没了金丝雀，数组的基址挪到了 rbp-0x10，帧也缩到了 0x10。两套布局的下标换算（arr[k] 的地址等于数组基址加 4k）咱们列在表里：

| 目标 | 默认构建（基址 rbp-0x20） | 去保护构建（基址 rbp-0x10） |
|---|---|---|
| 金丝雀（rbp-0x8） | k = 6 | 无（这一版没有金丝雀） |
| saved rbp（rbp+0x0） | k = 8 | k = 4 |
| 返回地址（rbp+0x8） | k = 10 | k = 6 |

## 打到金丝雀：写入成功，返回被拦

```text
$ ./d3_default 6
store done (idx=6), now return
*** stack smashing detected ***: terminated
exit=134
```

写入成功了，进度行也打出来了，随后 glibc 的 `__stack_chk_fail` 路线往 stderr 打了那行星号文本，进程以 SIGABRT 收了场，退出码报了 134。您注意时序：金丝雀的校验发生在 f 返回之前，返回地址从头到尾是完好的，防线把它护在了身后。那行星号文本的出处，[第一篇](01-stack-frame-neighbors.md)环境一节的取证纪律里交代过，完整的调用链证据咱们放到[下一篇](03-canary-defense-and-the-o2-story.md)踩坏金丝雀的一节看。

## 打到返回地址：控制流当场被劫走

```text
$ ./d3_noprot 6
store done (idx=6), now return
exit=139
```

一行进度打完了，退出码报了 139，shell 里没有任何别的文本，这是非交互 shell 的预期。这一版里是没有金丝雀的，同一个 k=6 的写入直接落在返回地址槽的低 4 字节上。咱们请 gdb 全程取证：

```text
$ gdb -batch -q \
      -ex 'set debuginfod enabled off' \
      -ex 'break d3.cpp:13' -ex run \
      -ex 'printf "== 写入之后、返回之前 ==\n"' \
      -ex 'info frame' -ex 'x/12gx $rbp-0x28' \
      -ex 'printf "== continue 到崩溃现场 ==\n"' \
      -ex continue \
      -ex 'info registers rip rsp rbp' \
      -ex 'printf "== 崩溃时 rsp 周围 ==\n"' \
      -ex 'x/6gx $rsp-0x10' -ex bt \
      --args ./d3_noprot 6 2>&1 | grep -v 'libthread_db\|Thread debugging'
```

（Breakpoint 1 at 一类的家务行已删，== 分隔行同样来自咱们让 gdb 打的 printf 命令，其余都是逐字的）。

```text
Breakpoint 1, f () at d3.cpp:13
13	    std::fprintf(stderr, "store done (idx=%d), now return\n", (int)g_index);
== 写入之后、返回之前 ==
Stack level 0, frame at 0x7fffffffd0a0:
 rip = 0x55555555518f in f (d3.cpp:13); saved rip = 0x555501234567
 called by frame at 0x7fffffffd0a8
 source language c++.
 Arglist at 0x7fffffffd090, args:
 Locals at 0x7fffffffd090, Previous frame's sp is 0x7fffffffd0a0
 Saved registers:
  rbp at 0x7fffffffd090, rip at 0x7fffffffd098
0x7fffffffd068:	0x0000000000000002	0x00007ffff7ffd000
0x7fffffffd078:	0x0000555555557da8	0x0000000200000001
0x7fffffffd088:	0x0000000400000003	0x00007fffffffd0b0
0x7fffffffd098:	0x0000555501234567	0x00007fffffffd1e8
0x7fffffffd0a8:	0x00000002f7ebd538	0x00007fffffffd160
0x7fffffffd0b8:	0x00007ffff7827781	0x00007ffff7ecf248
== continue 到崩溃现场 ==
store done (idx=6), now return

Program received signal SIGSEGV, Segmentation fault.
0x0000555501234567 in ?? ()
rip            0x555501234567      0x555501234567
rsp            0x7fffffffd0a0      0x7fffffffd0a0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
== 崩溃时 rsp 周围 ==
0x7fffffffd090:	0x00007fffffffd0b0	0x0000555501234567
0x7fffffffd0a0:	0x00007fffffffd1e8	0x00000002f7ebd538
0x7fffffffd0b0:	0x00007fffffffd160	0x00007ffff7827781
#0  0x0000555501234567 in ?? ()
#1  0x00007fffffffd1e8 in ?? ()
#2  0x00000002f7ebd538 in ?? ()
#3  0x00007fffffffd160 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

咱们把关键几处对上。返回前的 info frame 报 `saved rip = 0x555501234567`，它本来是 main 里的地址（0x5555555552xx 一带），低 32 位被载荷 0x01234567 顶掉了，高 32 位还是原来的 0x00005555，这正是只写了 4 字节 int、坏掉半条返回地址的现场。内存里也看得一清二楚：0x...d078 与 0x...d088 两行拼出 arr 的 1、2、3、4，0x...d090 是完好的 saved rbp（0x00007fffffffd0b0），紧挨着的 0x...d098 是被写坏的返回地址槽 0x0000555501234567。continue 之后程序收到了 SIGSEGV，崩溃现场的 rip 逐字等于写进去的坏地址，bt 已经回溯不动了（`corrupt stack?`）。控制流被劫走的样子，没有比这更直白的证据了。

## 打到 saved rbp：崩在调用者身上

```text
$ ./d3_noprot 4
store done (idx=4), now return
main: returned from f normally
exit=139
```

这一幕更值得咱们细看：f 自己正常返回了（返回地址没坏），main 的进度行也打出来了，退出码却报了 139。咱们把断点下在 main 刚接回控制权的地方（命令的形状与上一幕相同，断点换到了 d3.cpp:19，同样过滤了线程库提示行，家务行咱们删了，其余都是逐字的）：

```text
store done (idx=4), now return

Breakpoint 1, main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:19
19	    std::fprintf(stderr, "main: returned from f normally\n");
== f 已返回、main 刚接回控制权 ==
rbp            0x7fff01234567      0x7fff01234567
rsp            0x7fffffffd0a0      0x7fffffffd0a0
== main 帧里残存的 f 帧:rbp-0x10 起 ==
0x7fff0123455f:	Cannot access memory at address 0x7fff0123455f
== continue 看接下来发生什么 ==
main: returned from f normally

Program received signal SIGSEGV, Segmentation fault.
main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:21
21	}
rip            0x555555555208      0x555555555208 <main(int, char**)+82>
rsp            0x7fffffffd0a0      0x7fffffffd0a0
rbp            0x7fff01234567      0x7fff01234567
#0  main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:21
Backtrace stopped: Cannot access memory at address 0x7fff0123456f
```

咱们把经过还原出来。f 收尾时执行的 `pop %rbp`，把坏值 0x7fff01234567 装进了 rbp 寄存器。返回地址是完好的，所以 main 还能接着跑，但 main 从此失去了用 rbp 寻址的能力，gdb 连 argc、argv 都替它读不出来了（`Cannot access memory`），最后死在 main 的收尾上：leave 拿坏的 rbp 当 rsp 去 pop，SIGSEGV 就落在了这里。写坏 saved rbp 的是 arr[4] 的越界写，f 自己正常返回了，死的是调用它的 main。晚崩的这一档在排障时最唬人，因为崩溃的位置与越界发生的位置隔了一次函数返回，栈上的数据也早被后续的帧覆盖过了。三个远处的目标都试过了，挡在返回地址前头的金丝雀是怎么装、怎么查的，咱们下一篇正面看它。
