---
title: "虚拟地址与 4 KiB 切页：ASLR 与平铺大表的算术"
chapter: 6
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "从同一个变量两次运行地址不一样说起，确认指针里装的是虚拟地址，看内存怎么按 4 KiB 切页、低 12 位为什么不参与翻译，再算一算一张平铺大表为什么要 512 GiB"
reading_time_minutes: 10
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 虚拟地址与 4 KiB 切页：ASLR 与平铺大表的算术

[上一篇](../05-classes-in-assembly/01-this-pointer-and-member-offsets.md)咱们把类放回了汇编里，this、构造函数、虚表挨个过了一遍，上一个主题的末篇把去处也指好了：去内存那一侧看页表。这一篇咱们就来兑现它。前面五篇里咱们见过的每一个地址，无论是 02 里栈帧的 rbp，还是 05 里对象头部的 vptr，都有一个共同的身份：它们全是虚拟地址。指针里装的那个数，从来就不等于内存条上的真实位置。真实的位置得靠一次翻译才能拿到，咱们把指针值交给 CPU 去查页表，四张表接力走完了，指令才落到物理内存的一个页上。这一篇咱们就把这趟翻译从头走一遍，地址怎么按页切块，一张大表为什么撑不住，四张表怎么接力，走查失败会发生什么，走得太慢又拿什么来救。

这一篇的例子与实验都是本站原创的，您在文中看到的每一份 /proc 输出、每一行的终端记录，都逐字来自本机的真实运行。

咱们照惯例把环境摆在前面。这一篇的画风和前五篇的不同，汇编退了场，系统接口上了场，除了 g++ 与 gdb，主角换成了一批 /proc 文件。咱们把版本和几项与本篇直接相关的内核配置贴在下面：

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ id -u
1000

$ whoami
charliechen
```

咱们再看四条配置。头一条管的是地址随机化，第二条管的是透明大页，后两条管的是地址位宽：

```text
$ cat /proc/sys/kernel/randomize_va_space
2
```

```text
$ cat /sys/kernel/mm/transparent_hugepage/enabled
always [madvise] never
```

```text
$ grep -i 'address sizes' /proc/cpuinfo | sort | uniq -c
     16 address sizes	: 48 bits physical, 48 bits virtual
```

```text
$ grep -o 'la57' /proc/cpuinfo | sort -u      # 输出为空
$ grep -m1 'model name' /proc/cpuinfo
model name	: AMD Ryzen 7 9700X 8-Core Processor
```

咱们把它们翻译成人话。`randomize_va_space` 的值是 2，地址随机化全开。透明大页的模式是 madvise，也就是说普通代码里 mmap 出来的匿名内存都按 4 KiB 的基页走，不会悄悄换成 2 MiB 的大页，这一点保证了后面 RSS 实验的数字干净。cpuinfo 里 16 个逻辑 CPU 报的地址位宽全部一致，物理的位宽是 48 位，虚拟的位宽也是 48 位。`la57` 那一行 grep 出来是空的，说明这块 9700X 没有通告 57 位线性地址的五级页表能力，所以四级页表在本机是唯一形态，不存在五级的变体，咱们后面拆地址也就只拆一种。机器的内存一共 52 GiB，咱们写作的时候可用 43 GiB，gdb 的版本是 17.2。

## 同一个变量，两次运行的地址不一样

咱们从最熟悉的一句代码开始：打印一个局部变量的地址。程序顺手把 malloc 的返回值和 main 函数自己的地址也打出来，三个地址分别代表栈、堆和代码段：

```cpp
// f1_aslr.cpp —— 打印栈变量、malloc 与代码段的地址，观察 ASLR
#include <cstdio>
#include <cstdlib>

int main() {
    int stack_var = 42;
    void* heap_ptr = malloc(64);
    printf("stack local : %p\n", (void*)&stack_var);
    printf("malloc      : %p\n", heap_ptr);
    printf("code (main) : %p\n", (void*)&main);
    free(heap_ptr);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f1_aslr f1_aslr.cpp
$ ./f1_aslr     (x6)
stack local : 0x7ffd89c0f6ac
malloc      : 0x5725120bf020
code (main) : 0x5724fca5a169
---
stack local : 0x7ffc83e62ffc
malloc      : 0x5eecdf603020
code (main) : 0x5eecbe2b2169
---
stack local : 0x7ffddd90286c
malloc      : 0x592d46b1b020
code (main) : 0x592d23b7b169
---
stack local : 0x7ffdf5620b8c
malloc      : 0x5aeb577f3020
code (main) : 0x5aeb4e6b6169
---
stack local : 0x7fff885ed72c
malloc      : 0x56f9e37e3020
code (main) : 0x56f9e287d169
---
stack local : 0x7ffc826d8b3c
malloc      : 0x60e1c5ad6020
code (main) : 0x60e1931e2169
---
```

咱们跑了六回，三个地址全都变了。变化的范围各有各的地盘：栈地址始终以 `0x7ff...` 开头，malloc 的返回值和代码段都挤在 `0x55..` 到 `0x61..` 的区间里。咱们再看细一点，同一回运行里 code 与 malloc 的前缀是同源的，比如第一回的 `0x5724f...` 与 `0x57251...`，第二回的 `0x5eec...` 与 `0x5eec...`，代码段跟着程序映像的基址走，malloc 的起点也贴着它。这件事的背景是 GCC 在 Linux 上默认编出 PIE（位置无关可执行文件，程序映像可以落在任意的基址上运行），内核加载的时候会把基址随机挪一个数。

这套随机挪地址的机制叫 ASLR（地址空间布局随机化）。它存在的理由是安全：攻击者要是能提前知道栈、堆、代码的确切地址，很多攻击手法就能对着固定的地址下手。每次运行的地址都会变，固定的算盘就打不响了。咱们拿对照组验证一下，这回用 `setarch -R` 关掉了随机化，连着跑了六回：

```text
$ setarch $(uname -m) -R ./f1_aslr   (x6，每次输出一字不差)
stack local : 0x7fffffffd0dc
malloc      : 0x55555556b020
code (main) : 0x555555555169
---
(下 5 次同上，逐字相同)
---
```

六回的输出一字不差，`0x555555555169` 正是 PIE 程序不做随机化时大家熟悉的那个经典加载基址（跨发行版一般一致，咱们不把它当承诺）。变量还是原来的变量，程序还是原来的程序，变的只是落点而已。

那么 `%p` 打出来的到底是什么地址？答案是虚拟地址。每个进程都有一整套自己的地址空间，栈、堆、代码段、共享库各占了一块，进程 A 眼里的 `0x7ffd89c0f6ac` 和进程 B 眼里的同一串数字，对应的是两个互不相干的物理位置。02 里咱们在 gdb 会话里见过的那些 `0x7fff` 开头的栈地址，住的就是这块区域。分给每个进程一套独立的编号，是隔离的底座，而把编号翻译成内存条上的真实位置，就是页表要干的活。

## 内存按 4 KiB 切页，低 12 位不参与翻译

指针的值看着就是按字节编址的，但翻译的单位不是单个字节，咱们翻译的是整个页。物理内存和虚拟地址空间都被切成同样大小的页，翻译按页进行：虚拟地址的高位告诉咱们这是哪个页，低位告诉咱们页内偏了多少。咱们回过头确认一下页的大小：

```text
$ getconf PAGESIZE
4096
```

页的大小是 4096 字节，也就是咱们熟悉的 4 KiB，即 2 的 12 次方。低 12 位刚好编完一页以内的任何偏移，所以虚拟地址的低 12 位就是页内偏移。有一个观察可以佐证：内核发地址是按整页发的，任何新映射的起点必然页对齐，咱们看到的低 12 位就必然全是 0。咱们写个程序连着 mmap 八回，每回映射了一页就还回去，看它返回的地址长什么样：

```cpp
// f2_pagesize.cpp —— 页大小与 mmap 返回地址的低 12 位对齐性
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    long ps = sysconf(_SC_PAGESIZE);
    printf("sysconf(_SC_PAGESIZE) = %ld\n", ps);

    for (int i = 0; i < 8; i++) {
        void* p = mmap(nullptr, ps, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 1; }
        unsigned long addr = (unsigned long)p;
        printf("mmap #%d -> 0x%012lx   low 12 bits = 0x%03lx   page-aligned: %s\n",
               i, addr, addr & 0xfff, (addr & 0xfff) == 0 ? "yes" : "NO");
        munmap(p, ps);
    }
    return 0;
}
```

```text
$ ./f2_pagesize     (run 1)
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #1 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #2 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #3 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #4 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #5 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #6 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #7 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
=== run 2 ===
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x77251c360000   low 12 bits = 0x000   page-aligned: yes
(run 2 的 #1..#7 与 #0 相同，均为 0x77251c360000，low 12 bits = 0x000)
=== run 3 ===
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x7586296f9000   low 12 bits = 0x000   page-aligned: yes
(run 3 的 #1..#7 与 #0 相同，均为 0x7586296f9000，low 12 bits = 0x000)
```

咱们三次运行共做了 24 次映射，24 次的低 12 位全都是 0。输出里还藏着两个值得咱们看的小观察。同一进程里还回去的地址马上又被发回来，八次拿到同一个地址，内核这是在复用刚腾出来的空洞。而三个 run 的基址各不相同，起作用的还是 ASLR。拿本篇开头那个栈地址 `0x7fffffffd0dc` 来看，它的低 12 位是 `0x0dc`，翻译的时候页号会被换成物理页号，而 `0x0dc` 这个尾巴原样接在物理页号的后面，咱们一个比特都不用动。页内偏移是不需要翻译的，因为页内的排布在虚拟和物理两侧是完全一样的。

## 一张表管到底的算术，算出来就没法看

现在咱们来想翻译机制的构造。最直白的方案是一张平铺的大表：每个虚拟页在表里占一项，项里写的是物理页号，从虚拟地址算出页号，咱们拿页号当索引进表一查就完事。这个方案本身是没有错的，咱们拿本机的数字算一算它的代价。

咱们的虚拟地址有 48 位，地址空间就是 2 的 48 次方字节，也就是 256 TiB 的体量。咱们按 4 KiB 一页来切，页数是 2 的 48 次方除以 2 的 12 次方，等于 2 的 36 次方，得到的是 68719476736 个页。每个页咱们都得给它留一项，一项的大小是 8 字节（后面咱们会在 pagemap 里亲眼看到一项正好 8 字节），总共要 2 的 39 次方字节，也就是 549755813888 字节的体量，顶得上 512 GiB 的一张表。

咱们把这笔数摆回机器上看。本机的内存一共 52 GiB，一个进程的平铺页表就要 512 GiB，表比机器所有的内存还大十倍。而且这 512 GiB 里绝大多数项是浪费的：f1 那六回输出已经给过咱们底样，栈住在 `0x7ff...` 的一带，程序映像住在 `0x55..` 到 `0x61..` 的区间，中间从 `0x0000` 到 `0x5555` 的巨大区间根本没人用，一个正常进程的地址空间稀疏得很，剩下的几块亮区隔着大片空洞。咱们为空洞也留一行表项，平铺方案就死了。

咱们的出路是把大表分级。思路和图书馆的索引一样：顶层一本薄薄的目录，只登记下一级目录的位置，下一级登记的又是更下一级，最底层的表才真正登记物理页号。用到的区域才建表，没用的区域在顶层就是一项空的，整棵子树也就不用存在了。下面咱们就看 x86-64 的具体做法，四张表的接力就交给下一篇。
