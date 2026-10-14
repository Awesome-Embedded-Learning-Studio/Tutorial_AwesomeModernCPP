---
title: "栈的余量、ASan 与运气"
chapter: 4
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "8 MiB 的栈窗怎么撑大又怎么顶穿，ASan 怎么在写入发生的当场拦下越界，最后把不崩的来路收拢成一张表"
reading_time_minutes: 12
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 栈的余量、ASan 与运气

## 栈的余量：8 MiB 的窗

不崩的来路还剩最后一条，它是由余量决定的，跟落点是没有关系的。咱们用的环境与[第一篇](01-stack-frame-neighbors.md)相同，您要复跑就照它来。栈给每个进程划的窗有多大，咱们用三层证据说清：ulimit -s 报的 8192（KiB）是一层，/proc/self/limits 里的写法是一层，/proc/self/maps 给出的 [stack] 区间是第三层，后两层咱们直接从程序里打印。d4a 声明一个 7 MiB 的 char 数组，把数组的两头各摸一下，再把自己看到的 [stack] 区间与限额打出来：

```cpp
// d4a.cpp —— 7 MiB 栈数组，贴着 8 MiB 上限但不越限
#include <cstdio>
#include <cstring>

static void print_stack_facts(const char* tag) {
    char line[512];
    FILE* fp = std::fopen("/proc/self/maps", "r");
    if (fp) {
        while (std::fgets(line, sizeof line, fp))
            if (std::strstr(line, "[stack]"))
                std::printf("%s [maps]   %s", tag, line);
        std::fclose(fp);
    }
    fp = std::fopen("/proc/self/limits", "r");
    if (fp) {
        while (std::fgets(line, sizeof line, fp))
            if (std::strstr(line, "Max stack size"))
                std::printf("%s [limits] %s", tag, line);
        std::fclose(fp);
    }
}

int main() {
    print_stack_facts("begin:");

    static const unsigned N = 7u << 20; // 7 MiB，小于 8 MiB 限额
    char big[N];

    big[0]     = 1; // 数组最低地址，栈向下生长的前沿
    big[N - 1] = 2; // 数组最高地址，靠近 rbp

    std::printf("touched both ends of the %u MiB array, still alive\n", N >> 20);
    print_stack_facts("end:  ");
    return 0;
}
```

```text
$ g++ -O0 -g -o d4a d4a.cpp && ./d4a
begin: [maps]   7ffeca672000-7ffecad76000 rw-p 00000000 00:00 0                          [stack]
begin: [limits] Max stack size            8388608              unlimited            bytes
touched both ends of the 7 MiB array, still alive
end:   [maps]   7ffeca672000-7ffecad76000 rw-p 00000000 00:00 0                          [stack]
end:   [limits] Max stack size            8388608              unlimited            bytes
exit=0
```

软限额的 8388608 字节正好是 8 MiB，硬限额给的是 unlimited，7 MiB 的数组两头都摸到了，程序活得好好的。您再看 maps 的两行：begin 与 end 打出的 [stack] 区间一模一样，都是 0x704000 字节的区间、约合 7 MiB。这里有一个咱们没料到的现象：新进程的初始栈不该有 7 MiB。咱们写了个只打印自己 [stack] 行的最小探针，咱们连跑五回：

```text
$ for i in 1 2 3 4 5; do ./probe_maps; done
7ffc7707a000-7ffc7709c000 rw-p 00000000 00:00 0                          [stack]
7ffc8b4de000-7ffc8b500000 rw-p 00000000 00:00 0                          [stack]
7ffce3779000-7ffce379b000 rw-p 00000000 00:00 0                          [stack]
7ffd930ad000-7ffd930cf000 rw-p 00000000 00:00 0                          [stack]
7ffdaabc0000-7ffdaabe2000 rw-p 00000000 00:00 0                          [stack]
```

每个新进程的初始 [stack] 都是 0x22000。咱们把它折合成 136 KiB。所以 d4a 的 7 MiB 是被撑大的，而且撑大的动作发生在 begin 打印之前。答案在 d4a 的序言里（箭头注释是咱们加的，汇编的原输出里没有它们）：

```asm
0000000000001307 <main>:
    1307:	push   %rbp
    1308:	mov    %rsp,%rbp
    130b:	sub    $0x700010,%rsp        <- 无声下移 rsp，不碰任何页
    ...
    132b:	call   1189 <print_stack_facts(char const*)>   <- 这次 call 把返回地址压到帧底，首次触碰
    1330:	movb   $0x1,-0x700010(%rbp)   <- big[0]
```

sub 是只动寄存器不碰内存的，rsp 无声地跨过了 7 MiB。真正碰到新页的是 `call print_stack_facts` 的返回地址压栈，它落在 7 MiB 帧的最底下一页。内核管理栈的单位是 VMA（虚拟内存区间，内核给进程登记一段地址范围的条目），栈的 VMA 允许往下扩，这一次的扩展直接批到了帧底那一页，[stack] 的区间瞬间从 136 KiB 变成了约 7 MiB，begin 打印时已经扩完了，所以 begin 与 end 看到的是同一个区间。咱们在这里还能读出另一件事：本机 GCC 16 默认没有开 -fstack-clash-protection，序言里是没有逐页探测的，一条大 sub 就直接跨过了 7 MiB。栈冲突防护（stack clash protection）要治的就是这个场景，开了它的构建会把序言改成逐页触碰的写法，您在发行版的加固构建里常常见到它。

咱们把数组加到 8 MiB、正好顶到限额再试一次会怎样？d4b 只改了数组大小：

```cpp
// d4b.cpp —— 8 MiB 栈数组，正好顶到 8 MiB 限额
// 进度打印走 stderr(无缓冲)，崩在半路也不丢已打印的行
#include <cstdio>

int main() {
    static const unsigned N = 8u << 20; // 8 MiB，与 ulimit -s 的限额相等
    char big[N];

    std::fprintf(stderr, "frame set up, array declared, nothing touched yet\n");

    big[N - 1] = 2; // 数组最高地址，离 rbp 最近
    std::fprintf(stderr, "high end (big[N-1]) touched, still alive\n");

    big[0] = 1; // 数组最低地址，越过 8 MiB 边界的访问
    std::fprintf(stderr, "low end (big[0]) touched, still alive\n");
    return 0;
}
```

```text
$ g++ -O0 -g -o d4b d4b.cpp && ./d4b
exit=139
```

咱们连一行输出都没有等到，连第一句 frame set up 的影子都没见到，退出码就报了 139。崩的位置咱们看汇编与 gdb（汇编为节选）：

```asm
0000000000001149 <main>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	sub    $0x800010,%rsp
    ...
    117e:	call   1040 <fwrite@plt>     <- fprintf 被改写成 fwrite，这是第一条 call
    11a7:	movb   $0x1,-0x800010(%rbp)  <- big[0]，永远没执行到
```

```text
$ gdb -batch -q -ex 'set debuginfod enabled off' -ex run \
      -ex 'info registers rip rsp rbp' -ex bt \
      --args ./d4b 2>&1 | grep -v 'libthread_db\|Thread debugging'
Program received signal SIGSEGV, Segmentation fault.
0x000055555555517e in main () at d4b.cpp:9
9	    std::fprintf(stderr, "frame set up, array declared, nothing touched yet\n");
rip            0x555555555517e      0x555555555517e <main()+53>
rsp            0x7fffff7fd0c0      0x7fffff7fd0c0
rbp            0x7fffffffd0d0      0x7fffffffd0d0
#0  0x000055555555517e in main () at d4b.cpp:9
```

咱们把数字对一遍。咱们量到 rbp = 0x7fffffffd0d0，`sub $0x800010` 过后的 rsp 停在了 0x7fffff7fd0c0。8 MiB 限额的地板大约在 0x7fffff7fe000，咱们拿栈顶 0x7fffffffe000 减去 0x800000，得到的差就是它。main 的第一条 call 是 `call fwrite`（fprintf 被改写成了 fwrite，汇编里标着 main+53 的就是它），它要把返回地址压到 rsp-8 的位置，那个位置已经越过了地板，内核拒绝了继续往下的扩栈，SIGSEGV 落在了它的身上。数组元素一次都没被摸到：帧本身加上环境与 argv 的开销（rbp 距栈顶 0xf30），把 8 MiB 的限额用完了，崩掉的却是恰好排在后面的头一次函数调用。

这里有个与直觉相反的细节，值得您多看一眼：崩点跟源码里看着最可疑的 big[0] 没有关系，数组元素是一个都没被碰的。咱们看到的崩点落在哪条指令上，取决于哪次内存访问头一个越过栈区的边界页，跟数组下标写在第几行是没有对应关系的。咱们的叙述要是写成“数组把栈写穿了”，那就是想当然了，d4b 的现场就摆在那里反驳它。

到这里咱们就把“没崩”的来路集齐了五条：

| 不崩的来路 | 咱们在哪一篇看到的 |
|---|---|
| 打中同帧的数据邻居 | 第一篇的 arr[-1] 把 scalar 从 111 改成 999，程序照常退出 |
| 写进填充与对齐空隙 | 第三篇的 -O2 版 f 里 idx 4、5 的 8 字节填充 |
| 编译器重排变量 | 第三篇的数组挪到 rsp+0、标量进指令流，第一篇的保护档位改排座 |
| 写的是没人再读的值 | 第三篇的 red zone 里只被回读一次的 999 |
| 越界没出 8 MiB 的栈窗 | 本篇的 7 MiB 数组两头摸到仍活着 |

## 防线二：ASan 在写入发生的地方拦下

金丝雀只做返回前的一次哨兵比对，方向、距离、受害者它都是不报的。想要精确的现场，咱们请 AddressSanitizer（ASan，编译期加 -fsanitize=address 接入的内存错误检测器，02 拿它看过递归的栈溢出）出场。它的原理用一句话讲是影子内存：每 8 字节的真实内存配 1 字节的登记位，登记的内容是这 8 字节能不能碰，插桩的指令在每次访存前都去查这道登记，查到不能碰的，当场就把进程拦下了并打出报告。栈帧也被它重新安排过：可能越界的对象（数组一类）两侧垫上带毒的填充，写入一越界就落在了毒上，当场就被抓了。

咱们把越界改成刚出头一步的最小版本，g_index 放的是 4，正好越过 `int arr[4]` 的上边界一格：

```cpp
// d6.cpp —— 最小越界，数组刚出头一步，ASan 当场拦截
#include <cstdio>

volatile int g_index   = 4; // arr[4] 刚好越过 int arr[4] 的上边界一步
volatile int g_payload = 999;

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

```text
$ g++ -O0 -g -fsanitize=address -o d6_asan d6.cpp && ./d6_asan
=================================================================
==181327==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7921c1ef0030 at pc 0x579d0b86a3c7 bp 0x7ffc4f6d8050 sp 0x7ffc4f6d8040
WRITE of size 4 at 0x7921c1ef0030 thread T0
    #0 0x579d0b86a3c6 in f() /tmp/cpu-book-04/d6.cpp:15
    #1 0x579d0b86a496 in main /tmp/cpu-book-04/d6.cpp:22
    #2 0x7d21c4227780  (/usr/lib/libc.so.6+0x27780) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #3 0x7d21c42278b8  __libc_start_main (/usr/lib/libc.so.6+0x278b8) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #4 0x579d0b86a0f4  _start (/tmp/cpu-book-04/d6_asan+0x10f4) (BuildId: 0e05d556e4f2343277a9428ab3b6f4b9bb0349f3)

Address 0x7921c1ef0030 is located in stack of thread T0 at offset 48 in frame
    #0 0x579d0b86a1d8 in f() /tmp/cpu-book-04/d6.cpp:8

  This frame has 1 object(s):
    [32, 48) 'arr' (line 9) <== Memory access at offset 48 overflows this variable
HINT: this may be a false positive if your program uses some custom stack unwind mechanism, swapcontext or vfork
      (longjmp and C++ exceptions *are* supported)
SUMMARY: AddressSanitizer: stack-buffer-overflow /tmp/cpu-book-04/d6.cpp:15 in f()
Shadow bytes around the buggy address:
  0x7921c1eefd80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eefe00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eefe80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eeff00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eeff80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x7921c1ef0000: f1 f1 f1 f1 00 00[f3]f3 00 00 00 00 00 00 00 00
  0x7921c1ef0080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  ...（后续行删节）
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==181327==ABORTING
exit=1
```

咱们把报告的字段一个个读出来。帧对象表的那一行是 `This frame has 1 object(s): [32, 48) 'arr'`，意思是 ASan 重排了栈帧，arr 被单独安置在偏移 32 到 48 的区间，两侧垫的是毒。您注意名单里只有 arr，是没有 scalar 的。ASan 只给数组这类可能越界的对象围红区，纯标量是进不了保护名单的，它的本体还留在帧里，只是在名单上看不见的。所以咱们不能把话说成“ASan 把所有局部变量都围起来”，它围的是有越界能力的那类对象。命中行写的是 `Memory access at offset 48 overflows this variable`，它写在了 [32,48) 右边界外的第一步，写入的尺寸（WRITE of size 4）也在报告里，发生的位置精确到了 #0 帧的 d6.cpp:15。

咱们看影子图里 => 标出的那一行，它就是红区布局的直接证据。一个影子字节代表 8 字节真实内存：f1 f1 f1 f1 是左红区的 32 字节，00 00 是 arr 本体的 16 字节可寻址区，方括号标出的 [f3] 是这次写命中的影子字节，f3 f3 合起来是右红区的 16 字节，后面的 00 是帧的其余部分。图例里也给了它们的名字，f1 的名字是 Stack left redzone，f3 的名字是 Stack right redzone。这里有个容易混的名字得提一句：ASan 的红区（redzone）与 02 讲过的 x86-64 红区（red zone）重名不同物，前者是 ASan 垫的带毒填充，后者是 ABI 留给叶子函数的 rsp 之下 128 字节。

还有一个工程细节藏在运行里：stdout 的 before 行丢了。进程 abort 的那一刻没有冲掉 stdout 的块缓冲，报告本身走的是 stderr，完整地保留了下来。您自己做这类演示，进度行要么老老实实地 fflush(stdout)，要么干脆走 stderr 的路子，咱们 d3 与 d4b 用的就是后一种。

同一份源码咱们还能再测一轮。第一篇用的 d1.cpp（g_index 放 -1、裸 -O0 下静默改坏 scalar 的原版），咱们把它原样交给 ASan：

```text
$ g++ -O0 -g -fsanitize=address -o d1_asan d1.cpp && ./d1_asan
==181448==ERROR: AddressSanitizer: stack-buffer-underflow on address 0x77ee0d0f001c at pc 0x59e780e7f3c7 bp 0x7ffe6655a9a0 sp 0x7ffe6655a990
WRITE of size 4 at 0x77ee0d0f001c thread T0
    #0 0x59e780e7f3c6 in f() /tmp/cpu-book-04/d1.cpp:18
    #1 0x59e780e7f496 in main /tmp/cpu-book-04/d1.cpp:25
    ...（libc 帧与 SUMMARY 行删节，与上份报告同形）

Address 0x77ee0d0f001c is located in stack of thread T0 at offset 28 in frame
    #0 0x59e780e7f1d8 in f() /tmp/cpu-book-04/d1.cpp:11

  This frame has 1 object(s):
    [32, 48) 'arr' (line 12) <== Memory access at offset 28 underflows this variable
...（HINT 与 SUMMARY 行、=> 前后的全 00 影子行删节）
=>0x77ee0d0f0000: f1 f1 f1[f1]00 00 f3 f3 00 00 00 00 00 00 00 00
...（影子图例同上份报告，删节）
==181448==ABORTING
exit=1
```

负下标命中了左红区（offset 28，也就是影子图里的第四个 f1），错误类别也单独起了 `stack-buffer-underflow` 的名字。同一份源码的两种命运，咱们到这一步全部摆齐了：裸 -O0 下它安静跑完全程，邻居烂掉了也没人吭声，ASan 下它在 d1.cpp:18 的写入指令现场被拦下。拦截发生在写入的地方，是不用等函数返回的，这也是它比金丝雀强的地方：金丝雀只守一个哨兵、只管高地址方向、只在返回时查，ASan 是连方向带字节数带对象区间一起报的。

## 不崩是运气而非许可

程序没崩、printf 照常干了活、退出码还是 0，咱们把这一幕从头看到尾，您很容易把它读成这写法没什么事。咱们把标准的态度摆出来对照：越界写的名分就是未定义行为，标准的立场就是不做任何承诺，编译器想怎么排变量、想怎么优化都是它的自由，跑出什么样的结果都算它对。所以咱们今天看到的“没崩”，只是本机这一版 GCC、这一回的编译选项下的一次具体安排。咱们换个编译器版本、换个优化档、换个函数形状，座位表就重排了，同一个越界写的落点跟着换人，上一回安静的 arr[-1]，下一回压着的可能就是谁的返回地址。这一次咱们没伤到要害，靠的是运气。把没崩读成可以这么写的许可，标准是从来没有答应过的。

相关的去处咱们替您指几个。堆上的越界是另一套现场，布局由分配器与 malloc 的元数据决定，本站 crash-lab 卷有完整的取证实战，[崩溃现场怎么看](../../../crash-lab/a-memory-safety/01-null-deref.md) 练的是 gdb 读现场的手艺，[堆缓冲区越界](../../../crash-lab/a-memory-safety/03-heap-buffer-overflow.md) 讲的是堆那一侧的布局与拦截。C 字符串与缓冲安全的系统讲法，咱们不在这一篇展开，strcpy 一类为什么危险、安全的替代写法是什么，您到[卷一的缓冲安全一篇](../../../vol1-fundamentals/c_tutorials/11-c-strings-and-buffer-safety.md)去看。栈帧怎么搭起来的，回看 [02 的栈帧一节](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md) 就行。

系列下一篇咱们进成员函数的世界，[03](../03-arguments-in-registers/03-floats-structs-and-abi-docs.md) 结尾预告过的 this 指针就要登场，它走的正是 rdi 那个位置。最后咱们把最省事的自证留给您：您拿 d1.cpp 编两回，一回是裸的 -O0，一回加的是 -fsanitize=address，前一回安静地跑完了（退出码 0），后一回当场就把进程拦在了写入那一行。
