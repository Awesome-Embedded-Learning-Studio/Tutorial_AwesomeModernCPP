---
title: "金丝雀防线与 -O2 的另一个故事"
chapter: 4
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "逐指令看金丝雀的装载、落位与校验，三档栈保护对照并用 diff 验出默认档，-O2 再把标量搬进指令流、数组挪去 rsp+0"
reading_time_minutes: 11
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 金丝雀防线与 -O2 的另一个故事

上一篇结尾处三个远处的目标都打过了，这一篇咱们把挡在返回地址前头的防线本身摆上桌面。咱们用的环境与[第一篇](01-stack-frame-neighbors.md)相同，您要复跑就照它来。

## 防线一：金丝雀怎么装、怎么查、抓得住谁

第一个主题里咱们扫过 `%fs:40` 相关的几条指令，说它们的用途是抓栈溢出类的破坏，现在咱们把它正面看一遍。金丝雀的完整三件套，d1 的 f 里其实都露过面。头一件是装载：`mov %fs:0x28,%rax` 从 fs 段的 0x28 偏移读出随机值（第一个主题里写的 %fs:40，十进制的 40 与十六进制的 0x28 是同一个位置）。第二件是落位：把它存进 rbp-0x8 的哨兵槽，卡在数组与控制数据的中间。第三件是校验：返回前的 `sub %fs:0x28,%rax` 做一次比对，对不上的话就 `call __stack_chk_fail`。那个随机值是进程启动时生成的，它的存放处是 TLS（线程局部存储，每线程一份的私有数据格），原件是不放在栈上的，栈上的越界写够不着它，所以比对才有意义。

三件套并不是每个函数都有的，装不装由保护档位说了算。咱们拿一对函数做对照：scalar_only 里只有标量，with_arr 里放的是一个数组：

```cpp
// d5.cpp —— 金丝雀的装配与校验，三种栈保护档位的汇编对照
#include <cstdio>

__attribute__((noinline))
int scalar_only(int x) { // 没有数组： strong 档位不插金丝雀， all 档位插
    int t = x * 3;
    return t + 1;
}

__attribute__((noinline))
int with_arr(int x) { // 有数组： strong 与 all 都插金丝雀
    int arr[4] = {x, x + 1, x + 2, x + 3};
    int s = 0;
    for (int i = 0; i < 4; ++i)
        s += arr[i];
    return s;
}

int main(int argc, char**) {
    std::printf("scalar_only=%d with_arr=%d\n",
                scalar_only(argc), with_arr(argc));
    return 0;
}
```

```text
g++ -O0 -g -o d5_strong d5.cpp                 # 默认
g++ -O0 -g -fstack-protector-all -o d5_all d5.cpp
g++ -O0 -g -fno-stack-protector -o d5_none d5.cpp
./d5_strong && ./d5_all && ./d5_none     # 三个都打 scalar_only=4 with_arr=10
```

咱们的对照一律用 -O0，这里得交代一次失败的尝试：档位的对照咱们最早想拿 -O2 来做，结果 with_arr 在 -O2 下被整个折叠成了一条 `lea 0x6(,%rdi,4),%eax`，数组在指令流里找不到了，金丝雀也跟着没了，档位之间也就没有了可对照的东西。所以本篇通篇拿 -O0 说布局与档位，-O2 的世界留给下一节专门讲。

默认档下的 scalar_only，咱们在汇编里找不到金丝雀：

```asm
0000000000001149 <scalar_only(int)>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	mov    %edi,-0x14(%rbp)
    ...（纯算术，删节）...
    1162:	pop    %rbp
    1163:	ret
```

咱们再换成 -fstack-protector-all，两头都装上了：

```asm
0000000000001149 <scalar_only(int)>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	sub    $0x20,%rsp
    1151:	mov    %edi,-0x14(%rbp)
    1154:	mov    %fs:0x28,%rax        <- 装载金丝雀
    115d:	mov    %rax,-0x8(%rbp)      <- 存进 rbp-0x8
    ...（纯算术，删节）...
    1175:	mov    -0x8(%rbp),%rdx      <- 取出
    1179:	sub    %fs:0x28,%rdx        <- 与原件比对
    1182:	je     1189 <scalar_only(int)+0x40>
    1184:	call   1040 <__stack_chk_fail@plt>
    1189:	leave
    118a:	ret
```

-fno-stack-protector 的输出与默认档相同，里头也没有金丝雀的影子，咱们不重复贴。带数组的 with_arr 在默认档下则两头都齐（arr 又住在 rbp-0x20，您顺手可以再对一遍座位表）：

```asm
0000000000001164 <with_arr(int)>:
    1164:	push   %rbp
    1165:	mov    %rsp,%rbp
    1168:	sub    $0x40,%rsp
    116c:	mov    %edi,-0x34(%rbp)
    116f:	mov    %fs:0x28,%rax        <- 装载
    1178:	mov    %rax,-0x8(%rbp)      <- 存 rbp-0x8
    117c:	xor    %eax,%eax
    117e:	mov    -0x34(%rbp),%eax
    1181:	mov    %eax,-0x20(%rbp)     <- arr[0]，注意 arr 又在 rbp-0x20
    ...（arr[1..3] 与求和循环，删节）...
    11c5:	mov    -0x28(%rbp),%eax
    11c8:	mov    -0x8(%rbp),%rdx      <- 取出金丝雀
    11cc:	sub    %fs:0x28,%rdx        <- 比对
    11d5:	je     11dc <with_arr(int)+0x78>
    11d7:	call   1040 <__stack_chk_fail@plt>
    11dc:	leave
    11dd:	ret
```

三档的行为咱们从汇编里读出来了：strong 只给带数组一类局部变量的函数装金丝雀，纯标量的函数不装。-fstack-protector-all 连 scalar_only 这样的函数都装，哪怕它一条 lea 就能算完了。而 -fno-stack-protector 是一律不装的。本机的默认档等不等于 strong，咱们拿逐指令 diff 验，不背书上的说法：

```text
$ g++ -O0 -g -fstack-protector-strong -o d5_explicit d5.cpp
$ objdump -d --no-show-raw-insn -C d5_strong > a.txt
$ objdump -d --no-show-raw-insn -C d5_explicit > b.txt
$ diff a.txt b.txt
2c2
< d5_strong:     file format elf64-x86-64
---
> d5_explicit:   file format elf64-x86-64
```

咱们看到两份 objdump 的唯一差异是文件名那一行，指令流是完全相同的，所以本机的默认档就是 -fstack-protector-strong。

### 主动踩坏它，看看拦截长什么样

档位说完了，咱们把金丝雀本身踩坏一次。d5smash 与 d3 的默认构建同形：arr 基址 rbp-0x20，金丝雀的位置在 rbp-0x8，命中的下标是 6：

```cpp
// d5smash.cpp —— 主动踩坏金丝雀，触发 __stack_chk_fail
#include <cstdio>

volatile int g_index   = 6;          // 实测布局： arr 基址 rbp-0x20，金丝雀在 rbp-0x8，即 arr[6]
volatile int g_payload = 0x41414141;

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    arr[g_index] = g_payload; // 越界写，落在金丝雀上
}

int main() {
    f();
    std::fprintf(stderr, "main: returned from f normally\n");
    return 0;
}
```

布局勘察咱们照例用 objdump，f 的汇编与 d3 默认构建的同形，金丝雀装载、`mov %edx,-0x20(%rbp,%rax,4)` 的越界寻址、结尾比对三段都在，咱们略过不贴。咱们运行它：

```text
$ ./d5smash
*** stack smashing detected ***: terminated
exit=134
```

main 的进度行没打出来，abort 发生在 f 的尾声，控制权是没有回到 main 的。咱们看 gdb 把整条链完整地记了下来：

```text
$ gdb -batch -q -ex 'set debuginfod enabled off' -ex run -ex bt \
      --args ./d5smash 2>&1 | grep -v 'libthread_db\|Thread debugging'
*** stack smashing detected ***: terminated

Program received signal SIGABRT, Aborted.
0x00007ffff789a17c in ?? () from /usr/lib/libc.so.6
#0  0x00007ffff789a17c in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff783e5d0 in raise () from /usr/lib/libc.so.6
#2  0x00007ffff7825685 in abort () from /usr/lib/libc.so.6
#3  0x00007ffff7826704 in ?? () from /usr/lib/libc.so.6
#4  0x00007ffff792d030 in __fortify_fail () from /usr/lib/libc.so.6
#5  0x00007ffff792e414 in __stack_chk_fail () from /usr/lib/libc.so.6
#6  0x00005555555551a3 in f () at d5smash.cpp:11
#7  0x00005555555551ae in main () at d5smash.cpp:14
```

咱们现在能把打到金丝雀那一幕的时序补全了。arr[6] 写的是 int、4 字节，落在金丝雀槽的低 4 字节上，8 字节的哨兵被改掉一半。f 返回前的比对发现对不上，就跳进了 `__stack_chk_fail`，再经 `__fortify_fail` 走到了 abort，进程收到了 SIGABRT，返回地址至死都是完好的。整条防线干的就是这一件事：返回前把哨兵比对一次，对不上的话就把进程拦停在这里，这次调用也就不返回了。

金丝雀能抓的是从数组往高地址打、奔着返回地址去的经典路径。抓不住的咱们也当面摆出来：第一篇 d1 的 arr[-1] 往低地址打同帧的标量，金丝雀全程都是完好的，校验一路给的是绿灯，程序正常地退出了。哨兵只守它站的那道岗，负方向与同帧的数据邻居，它是一概不管的。

## 同一份源码，-O2 编出来是另一个故事

布局与防线都说完了，咱们把第一篇用的 d1.cpp 原样用 -O2 再编一回。同一份源码里的同一次 `arr[g_index] = g_payload`，行为却换了一个剧本：

```text
$ g++ -O2 -g -o d1_o2 d1.cpp
$ ./d1_o2
before: scalar=111 arr={1,2,3,4}
after : scalar=111 arr[g_index]=999
main: returned from f, program ends normally
exit=0
```

咱们把两份输出并排看：-O0 的 after 里 scalar=999，-O2 的 after 里 scalar=111，它倒是纹丝没动，arr[g_index] 倒都读回了 999。变化是从哪来的，咱们对着 -O2 的 f 反汇编逐条找（咱们一行都没删，下面贴的是全文）：

```asm
0000000000001180 <f()>:
    1180:	sub    $0x28,%rsp
    1184:	mov    $0x1,%edx
    1189:	mov    $0x2,%ecx
    118e:	mov    $0x4,%r9d
    1194:	mov    %fs:0x28,%rax
    119d:	mov    %rax,0x18(%rsp)
    11a2:	movabs $0x200000001,%rax
    11ac:	mov    $0x3,%r8d
    11b2:	mov    $0x6f,%esi
    11b7:	lea    0xe4a(%rip),%rdi        # 2008 <_IO_stdin_used+0x8>
    11be:	mov    %rax,(%rsp)
    11c2:	movabs $0x400000003,%rax
    11cc:	mov    %rax,0x8(%rsp)
    11d1:	xor    %eax,%eax
    11d3:	call   1030 <printf@plt>
    11d8:	mov    0x2e4a(%rip),%edx        # 4028 <g_payload>
    11de:	movslq 0x2e47(%rip),%rax        # 402c <g_index>
    11e5:	mov    %edx,(%rsp,%rax,4)
    11e8:	movslq 0x2e3d(%rip),%rax        # 402c <g_index>
    11ef:	mov    0x18(%rsp),%rdx
    11f4:	sub    %fs:0x28,%rdx
    11fd:	jne    1219 <f()+0x99>
    11ff:	mov    (%rsp,%rax,4),%edx
    1202:	mov    $0x6f,%esi
    1207:	xor    %eax,%eax
    1209:	add    $0x28,%rsp
    120d:	lea    0xe24(%rip),%rdi        # 2038 <_IO_stdin_used+0x30>
    1214:	jmp    1030 <printf@plt>
    1219:	call   1040 <__stack_chk_fail@plt>
```

咱们来看 scalar：它的栈槽没了。两次 printf 里的 scalar 都是 `mov $0x6f,%esi` 拿立即数现装的 111。标量住进了指令流，栈上已经不存在可被写坏的槽了。-O0 里被改成 999 的那个位置，在 -O2 的世界里没有主人。

咱们接着看数组：它挪到了 rsp+0x00。`movabs $0x200000001,%rax` 接 `mov %rax,(%rsp)` 的两条指令把 1 和 2 打包成 8 字节一次写进去，3 和 4 也同样打包进了 rsp+8。数组倒是必须留下一个基址对象，因为运行期的下标访问 `mov (%rsp,%rax,4)` 需要它，但位置从 rbp-0x20 挪到了 rsp+0x00，帧也从 0x30 缩到了 0x28，寻址基准从 rbp 换成了 rsp（f 在这一版里连 push rbp 都没有了，也就没有 saved rbp 可言了）。

同一个 arr[-1] 现在写到了 rsp-4，落进了 02 讲过的 red zone，也就是 rsp 之下 128 字节的保留区。那里是没有别的变量的，写入的 999 只被紧随其后的回读 `mov (%rsp,%rax,4),%edx` 碰过一次，打出来给咱们看，之后再没有人去读它了。金丝雀从 rbp-0x8 挪到了 rsp+0x18，保护的几何没变：从数组上边界往上，idx 4、5 是填充的 8 字节，idx 6 是金丝雀的槽，再往上是 8 字节的填充，rsp+0x28 之外就是返回地址的所在。结尾则成了尾调用 `jmp printf`，f 把收尾的活儿整个转交给被调方，连自己的 ret 都省了。

在 -O2 的汇编里，常见的“安静越界”机制几乎都到场了，咱们逐个对号。头一个是写进填充与对齐的空隙，证据就是 idx 4、5 的那 8 字节。另一个是编译器的变量重排，数组挪了窝，标量进了指令流。再一个写的是没人再读的值，red zone 里的 999 只被回读过一次。同一份源码的两个剧本都是真实存在的：-O0 演的是改坏邻居，-O2 演的是打了个空，决定演哪一个的是编译器的排座与优化。落点的来路咱们看到了这儿，还有一条不崩的来路跟落点没有关系，咱们接着往下看。
