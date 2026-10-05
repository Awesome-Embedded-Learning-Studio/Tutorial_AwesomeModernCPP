---
title: "进程内存布局:/proc/pid/maps 全图"
description: "一个 C++ 程序跑起来,它的地址空间里每一段住的是谁:本篇拿 /proc/self/maps 把一个真实程序解剖成 41 段、归成六个大类(程序映像、堆、共享库、匿名段、栈、vvar/vdso),认出每个文件模块的第二个 r-- 段是 GNU_RELRO 圈出、动态链接器重定位完 mprotect 只读的那一页;18 类变量逐个对表 18/18 一致,主发现是 .bss 跨段(开头借文件 rw 段尾页落脚、中段进匿名接续页),&printf 落在 libc 的 .text;malloc 分水岭实测 131049/131050,补测 strace 揭出真机制是堆顶有富余当场切、chunk 尺寸 roundup(请求+8,16) 大于等于 131072 才走 mmap,教科书与 malloc(3) 的措辞各差一档,free 掉 4MB 还会把 glibc 动态阈值抬到 4MB;brk 会 trim 收缩、栈顶不动低地址端下探 3180KB;Rss 与 Pss 把 libc .text 的 1004kB 摊派成 7kB,刚编译二进制的 .text 竟是 Private_Dirty,sync 之后转 Clean;ASLR 五跑全变(exe 跨约 13.9TiB、栈 7.09GiB),setarch -R 五跑逐字节全同,正是 gdb 默认看到的那个世界"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 15
prerequisites:
  - "mmap 内存映射:把文件贴进地址空间"
  - "页缓存与持久性:write() 返回之后发生了什么"
related:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 内存管理
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 进程内存布局:/proc/pid/maps 全图

文件 I/O 的六篇走完了,咱们手里的观察仪器也换过好几茬,而 `/proc/self/maps` 一直在场:咱们在 [L02](../file-io/02-mmap-memory-mapping.md) 拿它看过映射区本身,[L03](../file-io/03-page-cache.md) 看脏页计数的时候,接棒的是同住 `/proc` 的 meminfo。这一篇咱们把它请到正中间,把镜头拉到最远的地方,咱们看看一个 C++ 程序跑起来之后,它的**整个地址空间**里都住着谁。缺页是怎么把虚拟地址换成物理页的,L02 已经讲清楚了,咱们这里不重讲,今天要做的是另一件事:把全部的段一次看全。

问题从 `main` 里一个变量的地址出发。您写下一个全局数组、一个 `static` 局部变量、一次 `new`,还有您天天调用的 `printf`,它们跑起来之后各自落在哪个段?`.text`、`.data`、`.bss` 一类的名字,咱们在编译原理的课上背过,可它们在一张真实的 maps 里长什么样、边界在哪里、什么时候会动,笔者动笔之前也只是背过名字而已。咱们不空口下判断,这一篇就把一个真实的 C++ 程序解剖开,把 41 段一行行地认过去,再拿 18 类变量的地址逐个对表,顺路还要量四样东西:malloc 的分水岭,栈的生长方向,smaps 的计数,还有 ASLR 的随机量。

本篇的实验按 E1 到 E5 编号,与仓库 `code/volumn_codes/vol8/systems-programming/linux/memory/01-memory-layout/` 下的 01 到 05 五个目录一一对应,原始输出也都收在里面了,您随时能对表。[L02](../file-io/02-mmap-memory-mapping.md) 用的小写 e1 到 e5 与 [L03](../file-io/03-page-cache.md) 用的大写 E1 到 E6,是它们各自那篇的编号,与咱们这里互不相干,您别把三套对混了。实验环境咱们一次说清楚:咱们跑在 WSL2 的环境里,内核的版本是 6.18.33.2-microsoft-standard-WSL2,glibc 的版本是 2.44,编译器用的是 g++ 16.2.1,编译的口径全都一样,用的就是 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,拿到的警告数为零。透明大页的模式是 `madvise`,`ulimit -s` 报的是 8192KB,前者归 E4 的 THP 观察引用,后者是 E3 的栈实验要用的。还有一条方法上的纪律:每个程序都在运行中读**自己的** `/proc/self/maps` 与 `/proc/self/smaps`,咱们自己看自己,就不存在读别人进程时的时间差与权限问题。思维基石两篇([RAII 篇](../../thinking/01-raii-paradigm.md)与[错误处理篇](../../thinking/02-error-paradigm.md))定义的 `unique_fd` 与 `sys_call`,这一篇倒是用不上,咱们的实验全是只读的观察,连需要长持的资源都没有。

## E1:一张 maps,41 段,六个大类

maps 的六列格式,L02 已经逐列地讲过,咱们不重复。今天咱们看的是行的全貌。E1 的程序把自己的 41 行 maps 原样打印了一遍,再给每一段都归了类。下面贴的是同一次运行的原文。路径列的字太长,咱们把仓库前缀截短成了 `…/01-maps`,地址与其余的各列一字未动:

```text
63b203fb2000-63b203fb4000 r--p 00000000 08:30 1689135  …/01-maps
63b203fb4000-63b203fbb000 r-xp 00002000 08:30 1689135  …/01-maps
63b203fbb000-63b203fbc000 r--p 00009000 08:30 1689135  …/01-maps
63b203fbc000-63b203fbd000 r--p 0000a000 08:30 1689135  …/01-maps
63b203fbd000-63b203fbe000 rw-p 0000b000 08:30 1689135  …/01-maps
63b232da1000-63b232dd4000 rw-p 00000000 00:00 0        [heap]
74f996000000-74f996024000 r--p 00000000 08:30 1199257  /usr/lib/libc.so.6
74f996024000-74f99619f000 r-xp 00024000 08:30 1199257  /usr/lib/libc.so.6
74f99619f000-74f996215000 r--p 0019f000 08:30 1199257  /usr/lib/libc.so.6
74f996215000-74f996219000 r--p 00214000 08:30 1199257  /usr/lib/libc.so.6
74f996219000-74f99621b000 rw-p 00218000 08:30 1199257  /usr/lib/libc.so.6
74f99621b000-74f996223000 rw-p 00000000 00:00 0
……(libm/libstdc++/libgcc_s 的段落与 libc 同构,删节)……
74f996738000-74f996754000 r--p 00000000 08:30 20857    /etc/ld.so.cache
74f996754000-74f996758000 r--p 00000000 00:00 0        [vvar]
74f996758000-74f99675a000 r--p 00000000 00:00 0        [vvar_vclock]
74f99675a000-74f99675c000 r-xp 00000000 00:00 0        [vdso]
……(动态链接器 ld-linux 自己的 5 段加一段匿名,删节)……
7ffecb2e8000-7ffecb30a000 rw-p 00000000 00:00 0        [stack]
```

咱们从最低的地址开始认。头 5 行的 inode 同为 1689135,装的都是程序自己。把二进制的 ELF 头摆出来对照,5 段的来历就清楚了(readelf 是笔者今天拿同一份源码重编后跑的,布局跟存档是一致的):

```text
LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x001be0 0x001be0 R   0x1000
LOAD           0x002000 0x0000000000002000 0x0000000000002000 0x0063fd 0x0063fd R E 0x1000
LOAD           0x009000 0x0000000000009000 0x0000000000009000 0x000f50 0x000f50 R   0x1000
LOAD           0x00ab10 0x000000000000ab10 0x000000000000ab10 0x000688 0x0006f0 RW  0x1000
GNU_RELRO      0x00ab10 0x000000000000ab10 0x000000000000ab10 0x0004f0 0x0004f0 R   0x1
```

内核是照着四条 LOAD 落映射的。只读的头一条装着 ELF 头与链接器要看的元数据,跟着的可执行段就是代码,maps 里的 28KB 指的正是它。下一条 LOAD 是只读的,装的是 `.rodata`。最后一条 LOAD 是可写的,它带着两个不同的尺寸,文件里实占的 filesz 是 0x688,运行时应有的 memsz 是 0x6f0,`.data` 与 `.bss` 都挂在它的名下。4 条 LOAD 在 maps 上对出了 5 行,多出来的正是第四行那个 `r--p 0000a000`,它值得咱们停下来多看一会儿。

它是 **RELRO** 的产物,它的全称是 RELocation Read-Only,咱们把它直译成重定位只读化,它是动态链接的一场收尾。ELF 里还有一条叫 `GNU_RELRO` 的程序头,它在可写 LOAD 的开头圈出了一小段,圈出的范围是 0x4f0 字节,盖着的是 `.init_array`、`.dynamic` 与 `.got`。动态链接器在启动的时候填重定位,填完了,它就把圈住的这段 `mprotect` 成了只读。rw 的头一页被改了权限,maps 里自然就裂成了 r-- 加 rw 的两行。费这道手续为的是 `.got`(全局偏移表):表里躺着一排函数与变量的最终地址,老牌的攻击手法之一就是想办法往表里写,把调用改到别的地方,重定位一完成就转成了只读,往表里写字的路也就断了。咱们拿到的构建是部分 RELRO,惰性绑定用的 `.got.plt` 主体还留在可写页里,给运行期的首次调用留着改写的余地,所以 maps 上那一页只读,下一页却照旧是可写的。

再往上一格就是 `[heap]` 了,204KB 的样子,咱们到 E3 再回来量它。再往上的 26 行全是共享库的地盘,咱们从 libc、libm、libstdc++ 一路数到 libgcc_s,再算上动态链接器 ld-linux 自己的段落。每个库都带着与主程序同构的 5 段,多数库还外贴着一段匿名的 rw,紧跟着自家的 rw 段(libm 是个例外,它的 `.bss` 小,尾页里就装得下自己的),这就是 `.bss` 的接续页:memsz 超出文件的部分,由内核拿匿名的零页补上,E2 咱们拿变量地址去验它。`/etc/ld.so.cache` 这个小文件也占了一行,它是动态链接器的库查找缓存,顺路映射了进来。再往上的三行是小小的只读段 `[vvar]`、`[vvar_vclock]` 与 `[vdso]`。

**vdso** 的全名是 virtual dynamic shared object,中文的名字叫虚拟动态共享对象,它是内核映射进每个进程的一小段代码。咱们平时调的 `gettimeofday`、`clock_gettime` 只是要读内核的时间,走 vdso 就能以普通函数调用的方式拿到答案,省掉了一次系统调用的进出内核。它要读的内核数据放在 `[vvar]` 页里,在咱们的机器上,其中一部分还被单独标成了 `[vvar_vclock]`,老一些的内核,maps 里见不到这样的段名。x86_64 的老资料里还有一个 `[vsyscall]`,WSL2 的内核配置没留它,咱们在 WSL2 里等不到它出场。地址最高的 `[stack]` 是主线程的栈,反而是咱们最眼熟的一段。

41 段可以归成六个大类:程序自己占了 5 段,`[heap]` 占了 1 段,共享库的段最多,占了 26 段,没有文件背书的匿名段占了 5 段,栈占了 1 段,vvar 与 vdso 相关的占了 3 段。权限的统计更值得看:`r--p` 一类占了 21 段,带执行权限的 `r-xp` 只有 7 段,可写的 `rw-p` 占了 13 段。咱们平时念叨的程序有多大,对着权限的统计看,大头的字节全是元数据与只读数据,真正能写的段只占三成,而这三成里还有一多半是各库的数据段。

## E2:18 类变量,逐个对表

maps 认完了,咱们换个方向,从代码的这一头出发。E2 的程序声明了 18 类典型的变量,取它们的地址,再读**同一次运行**的 maps,把每个地址送回它落着的段。**ASLR**(地址空间布局随机化)只会把不同运行之间的地址搅乱,同一次运行内部是自洽的,所以对表成立。被探测的变量长这样:

```cpp
int g_init = 0x42;                                  // .data
char g_data_buf[64 * 1024] = {1};                   // .data(放大让段可见)
char g_bss_buf[1 << 20];                            // .bss(1MB,必然顶进匿名接续页)
int g_bss_small;                                    // .bss(小变量,见下方说明)
const int g_const = 7;                              // .rodata
const char* g_lit = "lm01-rodata-literal";          // 指向 .rodata 的字符串字面量

__attribute__((noinline)) int probe_target(int x) { return x + 1; }  // .text
```

取址的部分还包含了栈变量、`malloc`、`new` 与 `&printf`,咱们在存档里都留着,正文只挑几行有戏的:

```text
g_init 全局已初始化        0x64f4ed5941c0  本程序 rw 段(.data+.bss 尾页)      预期:.data      一致
&g_bss_buf[0] 未初始化起点  0x64f4ed594220  本程序 rw 段(.data+.bss 尾页)      预期:.bss(起点可能仍在文件 rw 段尾页)  一致
&g_bss_buf[512K] 未初始化中段 0x64f4ed614220 匿名 rw 段,紧贴本程序 rw 段 → .bss 接续页  预期:.bss(匿名接续页)  一致
g_const const 常量          0x64f4ed58163c  本程序 .rodata                     预期:.rodata     一致
malloc(64)                 0x64f52aa1e020  [heap] 堆(brk)                      预期:[heap]      一致
new char[4MB]              0x712c35dff010  匿名映射(mmap 区)                  预期:匿名 mmap(超 128KiB 阈值)  一致
&probe_target 本程序函数    0x64f4ed57cc60  本程序 .text                        预期:.text       一致
&printf libc 函数           0x712c3625b8b0  libc .text                          预期:libc .text  一致
对表结果:18 项中 18 项一致
```

18 项全都对上了,咱们挑三行细看。头一行其实是 `.bss` 的两副面孔,也是 E2 最主要的发现。`g_bss_buf` 是 1MB 的未初始化数组,它的起点落在**文件的 rw 段尾页**里,而 512KB 处的中段落进了紧贴其后的**匿名接续页**。咱们写的同一个数组,就这样被页边界切成了两截。教科书的那句话是 `.bss` 不占文件、运行时清零,这句话是对的,可惜它只覆盖了前一半。E1 里说过的两个尺寸在这儿派上了用场,它们的差额就是 `.bss`。内核只把文件的 rw 段映射到页边界为止,尾页里 filesz 之后的字节清零,正好放下 `.bss` 的开头,装不下的余量拿匿名零页接着补。小 bss 变量的去处也就清楚了:`g_bss_small` 与 static 局部的 `s_bss`(存档里没写初始化式的那个静态局部)排在 `.data` 的后面,还没超出尾页的边界,于是跟 `.data` 混在了同一页里,maps 上也就分不出彼此了。

第二行咱们看 `&printf`。0x712c3625b8b0 落在 libc 的可执行段里,对应的就是 `712c36224000-712c3639f000` 那一段。您调用的 `printf`,代码一个字节都不在您的二进制里,它在 libc 的 `.text` 里,您的调用指令要经过动态链接的跳板才能落到那儿,而 E1 里 RELRO 圈住保护的 `.got`,正是动态链接要查的地址表,不过部分 RELRO 下,跳板首调要改写的 `.got.plt` 还留在可写页里。咱们再看 `new char[4MB]` 那一行,它落在了 `[heap]` 的外面,与咱们直接 `mmap` 的 2MB 区间待在同一片匿名区里,位置在所有共享库的下方。malloc 的请求什么时候留在 `[heap]`、什么时候出来,这就是 E3 的正题。

## E3:边界会动:malloc 的分水岭、栈的下探、free 的归还

动手之前咱们得交代一个词:**brk** 是挪动堆顶边界的那个系统调用,maps 里 `[heap]` 标出的就是它管辖的这段,glibc 的 malloc 拿它当主料。E3 开场的思路是递增着 malloc,尺寸从 1KB 一路加到了 64MB,咱们每分配一笔就回读一次 maps,判定指针的落点在 `[heap]` 还是在匿名映射里,判定的逻辑就是下面这几行。所有分配的块全程都攥在手里不 free,免得搅乱了 glibc 的动态阈值:

```cpp
// malloc 判定:落在 [heap] → brk;落在匿名映射 → mmap
const char* classify_ptr(void* p, Range* hit) {
    Range m = mapping_of((unsigned long)p, nullptr);
    *hit = m;
    if (!m.found) return "!!未命中!!";
    Range h = heap_range();
    if (h.found && (unsigned long)p >= h.start && (unsigned long)p < h.end) return "brk([heap])";
    return "mmap(匿名)";
}
```

咱们贴出 128KiB 附近的输出(指针列咱们保留,更早的 1KB 到 64KB 各行删节):

```text
    65536  0x60fa2ff08ea0 brk([heap]) 60fa2ff23000(+0) 60fa2fef0000-60fa2ff23000(204KB)
   130048  0x60fa2ff89ef0 brk([heap]) 60fa2ffca000(+258048) 60fa2fef0000-60fa2ffca000(872KB)
   131008  0x60fa2ffa9b00 brk([heap]) 60fa2ffca000(+0) 60fa2fef0000-60fa2ffca000(872KB)
   131048  0x60fa2ffc9ad0 brk([heap]) 60fa3000a000(+262144) 60fa2fef0000-60fa3000a000(1128KB)
   131049  0x60fa2ffe9ac0 brk([heap]) 60fa3000a000(+0) 60fa2fef0000-60fa3000a000(1128KB)
   131050  0x7a0cc2ad8010 mmap(匿名) 60fa3000a000(+0) 7a0cc2ad8000-7a0cc2afe000(152KB)
   131056  0x7a0cc27df010 mmap(匿名) 60fa3000a000(+0) 7a0cc27df000-7a0cc2800000(132KB)
   131072  0x7a0cc27be010 mmap(匿名) 60fa3000a000(+0) 7a0cc27be000-7a0cc2800000(264KB)
   262144  0x7a0cc26e0010 mmap(匿名) 60fa3000a000(+0) 7a0cc26e0000-7a0cc2800000(1152KB)
分水岭:第一笔走 mmap 的请求 = 131050 字节(glibc 默认 mmap 阈值 128KiB=131072)
```

咱们看得很明白,131049 还在 `[heap]` 的范围里,131050 切去了匿名区,分水岭看起来正好卡在了两者之间。咱们别急着把这个数抄进笔记,它可经不起咱们换个问法。笔者动笔当天又补测了两组,存档里没有它们的份,编译的口径与 E3 相同。头一组咱们让每个尺寸单独起一个进程,只做一笔 malloc 的分配,它的结果并不稳定:笔者当时跑出过 131049 直落 mmap,复跑二十来次却又全回了 `[heap]`。新进程的堆顶剩多少富余,跟着二进制的 `.bss` 大小与启动的分配在漂,单独一笔的落点,本就是漂移的。真正给得出确定答案的是第二组,咱们把 E3 开场那套持有序列原样复刻,strace 的镜头里只留 brk 与 mmap 两类调用,它拍到的 malloc 引起的调用只有这几条(启动期的库映射行已删节):

```text
brk(0x6494a20c6000)                     = 0x6494a20c6000
brk(0x6494a20e8000)                     = 0x6494a20e8000
mmap(NULL, 135168, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7e9c64891000
mmap(NULL, 135168, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7e9c64870000
```

两条 brk 都是 131050 之前的请求把堆顶顶上去的,后面的两条 mmap 从 131050 起。可 131049 那一行去了哪里?它的前后连一条 syscall 都没有,指针却恰好落在了 `[heap]` 里。咱们把两组证据放到一起,事情的真相就分成了两层。

上面的一层很朴素:**malloc 的本质是库,而不是系统调用**。请求到了 glibc 的手里,它就在自己管的堆里找地方,堆顶还剩着富余就当场切一块给您,连内核都省得惊动了。开场那轮里 131048 那次把堆顶扩出了 256KB,切完之后堆顶还剩了一大截,轮到 131049 的时候富余正好够用,于是它悄无声息地住进了 `[heap]`,strace 里看到的空档就是它。

下面一层要等堆顶不够了才登场,咱们跟着 glibc 走一遍它面前的两条路。glibc 会把请求折算成内部的 **chunk 尺寸**,折算的公式是 `nb = roundup(请求 + 8, 16)`,8 字节是 chunk 头里 size 字段的占地,16 说的则是 chunk 的对齐。判据是 nb 大于等于 mmap 阈值的时候才走 mmap,默认的阈值是 131072 字节,正好就是教科书里的那个 128KiB。不满足的话 brk 就把堆扩一截,扩出来的量带着富余,mallopt(3) 里写明的名字是 `M_TOP_PAD`,默认的值也是 128KiB。mallopt(3) 的原文把两个条件都写了:原文说的是分配量大于等于 `M_MMAP_THRESHOLD`,而且空闲的列表还满足不了,两个条件都凑齐了,malloc 才会改走 `mmap(2)` 的路子。

拿着新的口径回头核对,流行的说法各有各的偏差。教科书讲的是“请求大于等于 128KiB 就走 mmap”,比较的对象就错了:比的是 chunk 的尺寸,不是请求的字节数。131049 比 128KiB 还小了 23 个字节,折算出的 chunk 却已经是 131072,够到阈值了。笔者当天单独跑它的时候真拿到过 mmap,可前面咱们刚见过,同一笔复跑二十来次全回了 `[heap]`,单独跑的落点在不在 mmap,取决于那一刻的堆顶还剩多少富余。malloc(3) 的 man 页写的是 “larger than MMAP_THRESHOLD”,边界又差了半档,mallopt(3) 写的 greater than or equal,才跟二进制的行为一致。而实验里那个 131050 也不是常数,上一笔把堆顶扩出了多少富余,会决定下一个请求的落点在 `[heap]` 还是匿名区。您从 maps 上读出的“分水岭”,漂一档才是它的常态,把它当成了物理常数,您才会栽跟头。

> 表里还藏着一条容易误读的细节:131050 那行的“所在映射”显示的是 152KB,可它的 chunk 只有 131072 字节,内核实给的 mmap 是按页取整的 132KB。多出来的 20KB 是启动期两笔小的匿名 mmap(12KiB 与 8KiB),它们紧贴在这笔映射的旁边,被内核并进了同一条 **VMA**。权限相同、地址相邻的映射,内核都会这样合并(VMA 的全名是 virtual memory area,中文的名字叫虚拟内存区域,L02 里咱们见过它),所以 maps 的行数不等于 mmap 的次数,咱们拿行数去数分配次数的时候,就得把这样的合并算进去。

E3 的后半场看 free 之后地址空间还不还。咱们把三个观察摆开:free 掉一笔 64MB 的 mmap 块,它所在的映射当场消失,munmap 归还地址空间是立即的。256 笔 48KB 的小块把 `[heap]` 从 1128KB 喂到了 13504KB,全部 free 之后它缩回了 1260KB,**brk 也是会收缩的**,只是它留了 `M_TOP_PAD` 的余量。堆顶以上的地址随收缩归还了内核,而留在 `[heap]` 里的已写页,free 是不会替咱们还给内核的,存档里程序打印的那句“已 write 过的页不再还给内核”,说的就是这后半件事。最有戏的是第三个观察:free 掉一笔 4MB 的 mmap 块,紧跟着的 malloc(3MB) 指针回到了 `[heap]`(堆顶恰好上抬了 0x300000),malloc(5MB) 则照旧走了 mmap。

这就是 glibc 的**动态 mmap 阈值**:free 一块大于当前阈值的 mmap 块,阈值就会被抬到那块的大小,此后比它小的请求都回 brk,64 位平台的上限是 4MiB×sizeof(long),也就是 32MiB 的量级。咱们不难猜到它的动机:mmap 直配的块 free 时整段还给内核,再来一个同尺寸的分配又得从头缺页一轮,brk 侧能复用的内存就不必这么折腾。还有一条边界请您留意:谁要是显式地调过了 `mallopt(M_MMAP_THRESHOLD, …)`,动态的调整就永久关闭了,所以 E3 全程不碰 mallopt,保的就是这个开关的原状。

最后咱们看栈。咱们用 alloca 每轮要 4KB,200 轮下来缓冲的地址一路走低。12000 帧的深递归把 `[stack]` 从 136KB 顶到了 3316KB,下探的 3180KB 全记在低地址端的头上,可它的高地址端一个字节都没挪。“栈向低地址生长”的口诀,落到 maps 上就是 [stack] 的起点在减、end 不变。生长是有边界的:`ulimit -s` 报的 RLIMIT_STACK 是 8192KB,咱们机器上的主线程栈最多长到 8MB,撞到了线上就是 SIGSEGV。E4 里您还会看到 smaps 给 `[stack]` 的 VmFlags 多标了一个 `gd`(growsdown),内核说的就是这桩事。

## E4:smaps:每段再摊开二十几个计数

maps 只告诉咱们“有哪些段”,`/proc/self/smaps` 在每段的块头后面再摊开二十来个计数的字段,单位记的是 kB。E4 的程序把状态做出来供观察:堆上写脏了 64×32KB,栈上也摸了 256KB,然后给四个代表段各贴一份 smaps 的原文。libc 可执行段的原文长这样(整块有 24 行,字段做了删节):

```text
72717c024000-72717c19f000 r-xp 00024000 08:30 1199257    /usr/lib/libc.so.6
Size                                   1516 kB
Rss                                    1004 kB
Pss                                      7 kB
Shared_Clean                          1004 kB
Shared_Dirty                             0 kB
Private_Clean                            0 kB
Private_Dirty                            0 kB
Anonymous                                0 kB
THPeligible                              0
VmFlags                      rd ex mr mw me sd
```

咱们把四个代表段的关键字段放在一起看(单位是 kB):

| 段 | Size | Rss | Pss | Shared_Clean | Private_Dirty |
|---|---|---|---|---|---|
| 本程序 .text | 24 | 24 | 24 | 0 | 24 |
| [heap] | 2260 | 2076 | 2076 | 0 | 2076 |
| [stack] | 276 | 276 | 276 | 0 | 276 |
| libc .text | 1516 | 1004 | 7 | 1004 | 0 |

咱们一个字段一个字段过。**Size** 是段的虚拟总量,包含一次都没摸过的页。**Rss** 的全名是 Resident Set Size,中文的名字叫驻留集,它是此刻真在物理内存里的部分。`[heap]` 的 Size 2260kB 对 Rss 2076kB,差的 184kB 就是还没摸到的页,L02 讲过的懒分配落到计数上,指的就是这一差额。**Pss** 的全名是 Proportional Set Size,中文的名字叫比例驻留集,它是 Rss 的摊派版:共享的页按进程数摊,每个进程都各记它的几分之一。libc 的 `.text` 这 1MB 代码,全机有一百四十个上下的进程在共用,Rss 把 1004kB 全额记在咱们头上,Pss 摊完了就只剩 7kB。咱们想知道“一个进程自己真实的内存占用”,把各段的 Pss 加起来才是诚实的算法,内核在 smaps_rollup 里连这步加法都替您做好了。E3 结尾欠下的那笔也在这儿兑现:`[stack]` 整块的块尾一行是 `VmFlags rd wr mr mw me gd ac`,多出来的那个 `gd`(growsdown),说的就是栈往低地址生长。

Shared 与 Private 的分法,咱们看的是“页有没有跟别的进程共享”,Clean 与 Dirty 的分法,咱们要看两处:私有页看的是写没写过,文件页看的是页缓存回写没回写。`[heap]` 与 `[stack]` 是匿名私有的段,写过的页全落 Private_Dirty,于是 Rss=Pss=Private_Dirty 的三项归一,是最好认的一种。

真正让笔者愣了一下的是程序自己的 `.text`:它的身份是文件映射,咱们谁都没有写过它,可 smaps 报的却是 Private_Dirty 24kB。追下去咱们发现,答案出在页缓存:smaps 对文件映射的 clean/dirty,看的是**页缓存页的回写状态**,咱们这个二进制刚编译完,构成它 `.text` 的那几页还在页缓存里没写到盘上,映射进来的时候就带着一身的脏标。验证的办法也简单,[L03](../file-io/03-page-cache.md) 的老朋友 sync 上场,咱们跑一次 `sync` 再等上 8 秒,重跑的是同一个程序,同一行就变成了 Private_Clean 24kB,Private_Dirty 归了零。页缓存那一篇咱们盯的是数据文件,这回轮到的可是程序自己,用的还是同一套写回的机制。

表尾的 THPeligible=0 也交代一句:THP 的全名是 transparent huge page,中文的名字叫透明大页,它是 2MiB 大页自动聚合的机制。本机的模式是 `madvise`,只有显式递过 `MADV_HUGEPAGE` 的段才有资格,咱们的段都没递过话,所以清一色是 0,`KernelPageSize=MMUPageSize=4 kB` 说的也是同一个意思。大页的事咱们留到对齐分配与大页的那一篇再算。

::: warning pmap 的 Dirty 列与 smaps 的 Private_Dirty 不同源
`pmap -x` 是把这些数据重新排版的常用工具,它的 Kbytes 与 RSS 两列,跟 smaps 的 Size、Rss 是同源的。可它的 Dirty 列走的是 pmap 自家的口径,按 `/proc/pid/pagemap` 算出来的数,与 smaps 的 Private_Dirty 不必逐行相等,两张表一起用是没有问题的,您硬要逐行对数的话,对不齐反而是一种常态。另外 E4 的 pmap 是程序运行中 spawn 出来的,拍照比 smaps 晚了几毫秒,heap 一行的 RSS 都从 2076 涨到了 2164,对比的时候连时点的差也得算进去。
:::

## E5:ASLR:五跑全变,setarch -R 五跑全同

到这里为止咱们看的,都是同一次运行里的结构。可只要您连跑两次程序,整张图的地址就全变了,干这件事的是 **ASLR**。E5 的程序每次运行打印一行关键的基址:

```cpp
std::printf("exe=%012lx heap=%012lx stack=%012lx-%012lx libc=%012lx vvar=%012lx vdso=%012lx\n",
            find_first(0, exe),        // 程序加载基址(PIE)
            find_first(0, "[heap]"),
            find_first(0, "[stack]"),  // [stack] 起点(低地址端)
            stack_top(),
            find_first(1, "libc.so"),
            find_first(2, "[vvar"),    // [vvar] 家族
            find_first(0, "[vdso]"));
```

咱们在普通模式下连跑五次,随后用 `setarch -R` 关掉随机化的模式再连跑五次(十行全在存档里,咱们贴几行):

```text
== ASLR 开 · 第 1 次 ==
exe=600afa76f000 heap=600afe5f4000 stack=7ffcc36cc000-7ffcc36ee000 libc=707580200000 vvar=707580a02000 vdso=707580a08000
== ASLR 开 · 第 3 次 ==
exe=621b9561e000 heap=621bb7e13000 stack=7ffc575ab000-7ffc575cd000 libc=7fe7d6800000 vvar=7fe7d7053000 vdso=7fe7d7059000
== setarch -R(ASLR 关)· 第 1 次 ==
exe=555555554000 heap=55555555b000 stack=7ffffffdd000-7ffffffff000 libc=7ffff7800000 vvar=7ffff7fb6000 vdso=7ffff7fbc000
(关掉的五次输出逐字节相同,另四次删节)
```

普通五次的跨度都不小:exe 的基址跨了约 13.9 TiB,libc 连同 vvar、vdso 跨了约 15.5 TiB(它们跟 mmap 区一起走),理论的天花板都是 2^32 个页位置乘 4KiB,折出来正好是 16 TiB 的量级。栈的起点也跨了 7.09 GiB,对应的天花板是 2^22 个页位置乘 4KiB,合出的上限是 16 GiB。这些位数是内核 x86_64 侧的随机化档位,五次采样当然摸不满整个范围,可量级已经把档位亮出来了。`[heap]` 是跟着 exe 走的,间隔的随机量不小,这五次从 62MB 一路隔到了 1GB。咱们去读 `/proc/sys/kernel/randomize_va_space`,读到的取值是 2,对应的是全量随机化。

下面五行是 `setarch -R` 的世界。setarch(8) 的 -R 设的是 personality(2) 的 ADDR_NO_RANDOMIZE,五次运行的输出**逐字节相同**:exe 落回了 PIE(position independent executable,位置无关的可执行文件)的默认基址 0x555555554000,栈顶固定在了 7ffffffff000,libc 也固定在了 7ffff7800000,`[heap]` 紧紧贴着程序的映像,中间只隔了 0x7000。这套地址您多半眼熟:gdb 默认就替您关了随机化,教程与调试记录里那些整整齐齐的地址,拍的全是同一个世界。工程上它也有正用:复现一个依赖地址布局的 bug,或者想让两次运行的 maps 可以逐行 diff,拿 `setarch -R` 一裹就成了。ASLR 挡的是“猜中地址”的路,段的结构一次都没有变过,变的只是落点。

## 另一侧怎么看

Windows 没有一个把整个地址空间摊开的文本文件,想逐段看的话,咱们得用 VirtualQuery 从低地址往上一次问一段,每问一次拿回的是一条 MEMORY_BASIC_INFORMATION(里面装着状态、类型、保护属性与大小),咱们自己循环着拼出整张的表,图形化的 VMMap 与 minidump 工具做的也是同一件事。堆的归属也换了:brk 在那边是没有的,HeapAlloc 走的是 NT 堆,新式的配置里还有 segment heap 可选。倒是预留与提交的两段式(VirtualAlloc 的 `MEM_RESERVE` 与 `MEM_COMMIT`),它和咱们在 L02 见过的“mmap 只登记、缺页才给页”,是同一个思想的两种实现。等 Windows 侧的内存篇写完,咱们把 VirtualQuery 那一篇的链接补回这里。

这一篇咱们把一个普通 C++ 程序的 41 段认全了,往后的三篇都要用它打底:虚拟内存 API 篇拿 mprotect 改这些段的权限,往中间插 PROT_NONE 的 guard page,共享内存篇让两个进程的地址空间真正共用同一段,对齐分配与大页篇回头解释 THPeligible 那个 0 的来历。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="proc_pid_maps(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html"
  />
  <ReferenceItem
    :id="2"
    title="proc_pid_smaps(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_pid_smaps.5.html"
  />
  <ReferenceItem
    :id="3"
    title="malloc(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/malloc.3.html"
  />
  <ReferenceItem
    :id="4"
    title="mallopt(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/mallopt.3.html"
  />
  <ReferenceItem
    :id="5"
    title="brk(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/brk.2.html"
  />
  <ReferenceItem
    :id="6"
    title="elf(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/elf.5.html"
  />
  <ReferenceItem
    :id="7"
    title="setarch(8)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man8/setarch.8.html"
  />
  <ReferenceItem
    :id="8"
    title="getrlimit(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getrlimit.2.html"
  />
</ReferenceCard>
