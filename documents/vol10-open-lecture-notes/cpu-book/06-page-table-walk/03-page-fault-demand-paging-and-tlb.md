---
title: "缺页的两种命运：按需分页与 TLB"
chapter: 6
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "合法的缺页由内核补页，非法的缺页升级成 SIGSEGV，用 min_flt 计数与 gdb 现场把两条路各实测一遍，再看 VmSize 与 VmRSS 怎么量出登记与分配的分离，以及 TLB 怎么救四张表的访存代价"
reading_time_minutes: 17
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 缺页的两种命运：按需分页与 TLB

上一篇的收尾停在 present 位从 0 翻成 1 的那一对原始值上，填表的手笔来自缺页处理。这一篇咱们接着走：合法的缺页由内核补页，非法的升级成信号，映射是登记、页是摸到才给的，四张表的代价还有 TLB 兜着。咱们用的环境与[第一篇](01-virtual-address-and-4k-pages.md)相同，您要复跑就照它来。

## 缺页的两种命运：补一页，或者发一个信号

上一篇里 present 位的故事引出了这一篇的主角：缺页。CPU 里管翻译的硬件叫 MMU（内存管理单元），它走页表的时候要是碰到 present 为 0 的条目，或者权限对不上的条目，访存的指令会被拦下来，内核收到一次缺页的报告。page fault 这个词里的 fault，在这里是缺了东西的意思，说的不是程序写错了。报告交上去之后的命运有两种。映射合法而页又不在的，内核就把缺页补好了，程序若无其事地继续跑，上一篇咱们看到的 present 翻位走的就是同一条路。映射根本不存在的，或者权限不允许的，内核就把报告升级成了一个信号，段错误就来了。咱们把两条路各实测一遍。

合法缺页走的路，咱们靠计数器看。`/proc/self/stat` 的第 10 个字段是 min_flt，进程吃到的次缺页计数，也就是不需要磁盘 I/O 就能补上的缺页。这个文件的解析有个小机关：第 2 个字段是进程名，包在括号里的内容还可能带空格，安全的数法是找到最后一个右括号，再从它的后面往后数字段。咱们拿 awk 独立核过口径：

```text
$ awk '{print "awk sees comm=", $2, " field10(min_flt)=", $10}' /proc/self/stat
awk sees comm= (awk)  field10(min_flt)= 240
```

程序的本体咱们贴在下面。四段读数的中间咱们夹了三步动作：mmap 了四页，摸了第一页，又摸了第三页。收尾的时候咱们还回头把第一页重摸了一遍：

```cpp
// f5_minflt.cpp —— 用 min_flt 计数观察按需分页的每页一次缺页
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

static long min_flt() {
    FILE* f = fopen("/proc/self/stat", "r");
    if (!f) { perror("fopen"); return -1; }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    char* rp = strrchr(buf, ')');
    if (!rp) return -1;
    long fld;
    int consumed;
    // tokens after ')': state(3) ppid(4) pgrp(5) session(6) tty_nr(7)
    //                   tpgid(8) flags(9) minflt(10)
    int rc = sscanf(rp + 1, "%*s %*s %*s %*s %*s %*s %*s %ld %n", &fld, &consumed);
    if (rc != 1) return -1;
    return fld;
}

int main() {
    long page = sysconf(_SC_PAGESIZE);

    long before = min_flt();
    printf("min_flt before mmap : %ld\n", before);

    char* p = (char*)mmap(nullptr, 4 * page, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    long after_mmap = min_flt();
    printf("min_flt after  mmap : %ld   (delta %ld)\n", after_mmap, after_mmap - before);

    p[0] = 1;  // first touch: demand paging fault #1
    long after_touch1 = min_flt();
    printf("min_flt after 1st touch (p[0]) : %ld   (delta %ld)\n", after_touch1, after_touch1 - after_mmap);

    p[2 * page] = 2;  // third page: fault #2
    long after_touch3 = min_flt();
    printf("min_flt after 3rd page touch   : %ld   (delta %ld)\n", after_touch3, after_touch3 - after_touch1);

    p[0] = 3;  // re-touch already-resident page: expect NO new fault
    long after_retouch = min_flt();
    printf("min_flt after re-touch p[0]    : %ld   (delta %ld)\n", after_retouch, after_retouch - after_touch3);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f5_minflt f5_minflt.cpp
$ ./f5_minflt
min_flt before mmap : 191
min_flt after  mmap : 196   (delta 5)
min_flt after 1st touch (p[0]) : 197   (delta 1)
min_flt after 3rd page touch   : 198   (delta 1)
min_flt after re-touch p[0]    : 198   (delta 0)
=== run 2 ===
min_flt before mmap : 191
min_flt after  mmap : 196   (delta 5)
min_flt after 1st touch (p[0]) : 197   (delta 1)
min_flt after 3rd page touch   : 198   (delta 1)
min_flt after re-touch p[0]    : 198   (delta 0)
=== run 3 ===
(与 run 1、run 2 逐字相同)
```

这串阶梯咱们一行行读。摸第一页的时候计数加了 1，摸第三页又加了 1：每个新页的第一次被碰到，带来的恰好是一次缺页，内核在这一下里干的活是分配物理页、填 PTE。咱们回头再摸第一页，计数不动了，页已经驻留了，翻译已经畅通无阻了。三次运行的数字完全一样，跑的是同一个二进制、同一套动态加载序列，计数连起点都复现了。

中间那行的 delta 5 值得咱们单说。mmap 调用自己也吃了 5 次缺页，但它们不是数据页的缺页：内核要给新映射建 VMA（内核为每段连续映射记的一条虚拟内存区域记录）、给页表分级分配表页，这些管理的活儿自己也要碰内存。所以咱们描述 mmap 只能说它不给数据页分配内存，登记的事情归登记，数据页是一页都不动的，您可别把这句话说成 mmap 零缺页，那 5 次是实打实数出来的。

非法缺页走的路，咱们造一个现场。程序里拿一个不落在任何映射里的地址去解引用：

```cpp
// f5_segv.cpp —— 解引用一个没有映射的地址，观察 SIGSEGV 现场
#include <cstdio>

int main() {
    int* bad = (int*)0xdeadbeef;
    printf("about to dereference %p\n", (void*)bad);
    fflush(stdout);
    int v = *bad;          // not backed by any page table entry
    printf("unreachable, v=%d\n", v);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f5_segv f5_segv.cpp
$ gdb -q -batch -ex 'run' \
  -ex 'info program' -ex 'x/i $pc' \
  -ex 'info proc mappings' ./f5_segv
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".
about to dereference 0xdeadbeef

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555188 in main () at f5_segv.cpp:8
8	    int v = *bad;          // not backed by any page table entry

--- signal ---
Last stopped for thread 1 (Thread 0x7ffff7f66780 (LWP 196477)).
	Using the running image of child process 196477.
Program stopped at 0x555555555188.
It stopped with signal SIGSEGV, Segmentation fault.

--- faulting instruction ---
=> 0x555555555188 <main()+63>:	mov    (%rax),%eax

--- info proc mappings ---
process 196477
Mapped address spaces:

Start Addr         End Addr           Size               Offset             Perms File
0x0000555555554000 0x0000555555555000 0x1000             0x0                r--p  /tmp/cpu-book-06/f5_segv
0x0000555555555000 0x0000555555556000 0x1000             0x1000             r-xp  /tmp/cpu-book-06/f5_segv
0x0000555555556000 0x0000555555557000 0x1000             0x2000             r--p  /tmp/cpu-book-06/f5_segv
0x0000555555557000 0x0000555555558000 0x1000             0x2000             r--p  /tmp/cpu-book-06/f5_segv
0x0000555555558000 0x0000555555559000 0x1000             0x3000             rw-p  /tmp/cpu-book-06/f5_segv
0x0000555555559000 0x000055555558c000 0x33000            0x0                rw-p  [heap]
(libc / libm / libstdc++ / libgcc_s / ld.so 各映射区间此处省略)
...
0x00007ffffffdd000 0x00007ffffffff000 0x22000            0x0                rw-p  [stack]
```

（映射表中间的 30 余行库文件映射咱们略去了，只保留程序自身的五段、heap 与 stack，任何指令咱们都没有动）

咱们再开一个 gdb 会话，把寄存器和内核报告的信号细节取出来：

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' \
    -ex 'run' -ex 'p/x $rax' -ex 'x/i $pc' \
    -ex 'p/x $_siginfo._sifields._sigfault.si_addr' \
    -ex 'p $_siginfo.si_signo' -ex 'p $_siginfo.si_code' ./f5_segv
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".
about to dereference 0xdeadbeef

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555188 in main () at f5_segv.cpp:8
8	    int v = *bad;          // not backed by any page table entry
$1 = 0xdeadbeef
=> 0x555555555188 <main()+63>:	mov    (%rax),%eax
$2 = 0xdeadbeef
$3 = 11
$4 = 1
```

现场咱们逐条对上。出事的指令是 `mov (%rax),%eax`，一个普普通通的从内存取数，rax 里装的正是 `0xdeadbeef`。MMU 把页表走到了头也没找到合法的映射，缺页的报告交上去之后由内核来查，这个地址不在任何映射区里，于是信号就来了。咱们看 `$3`，显示的信号编号是 11，它的名字是 SIGSEGV。咱们看 `$4`，显示的 si_code 是 1，它的名字叫 SEGV_MAPERR，意思是地址压根不在映射区。咱们看 `$2`，显示的 si_addr 是 `0xdeadbeef`，内核报告的故障地址与出错指令的操作数严丝合缝。咱们再看映射表，整个进程最低的映射从 `0x555555554000` 开始，`0xdeadbeef` 远在它下方的空洞里，落了个无家可归。咱们在 shell 层直接跑一遍，看到的退出码是 139，正好是 128 加 11 得来的，与信号编号的对应关系也对上了：

```text
$ ./f5_segv; echo "exit=$?"
about to dereference 0xdeadbeef
exit=139
```

咱们把两条路放在一起看，缺页用的是同一个机制，出口却分成了两个。合法映射内的缺页由内核来补页，上一篇 f3b 里 present 位的翻转和本篇 f5_minflt 里每页加一的计数，说的都是它。没有映射的缺页直接升级成了 SIGSEGV，02 和 04 里咱们见过的段错误现场，源头也就在这里了。02 研究栈溢出的时候咱们追过的崩溃取证，追到根上也是同一件事：页表走查失败了，内核把报告发了出去，进程收下了信号。

## 映射是登记，页是摸到才给的

按需分页（demand paging、页在第一次被用到时才真正分配）的威力，最直观的量尺是 /proc/self/status 里的两个字段：VmSize 是虚拟地址空间的用量，VmRSS 记录的是常驻内存集，也就是进程此刻真实占着的物理内存量。咱们拿一个 1 GiB 的匿名映射做实验，只摸其中的几页：

```cpp
// f6_rss.cpp —— mmap 1 GiB 匿名区，只触碰前 N 页，看 VmRSS 与 VmSize 各涨多少
// usage: f6_rss <npages>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

static long vm_field(const char* name) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) { perror("fopen"); exit(1); }
    char line[256];
    long result = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, name, strlen(name)) == 0) {
            sscanf(line + strlen(name), ": %ld kB", &result);
            break;
        }
    }
    fclose(f);
    return result;
}

int main(int argc, char** argv) {
    long page = sysconf(_SC_PAGESIZE);
    long npages = (argc > 1) ? atol(argv[1]) : 3;
    size_t sz = (size_t)1 << 30;   // 1 GiB

    printf("touching %ld page(s) of a %zu-byte (1 GiB) anonymous mapping\n", npages, sz);

    long rss0 = vm_field("VmRSS");
    long vsz0 = vm_field("VmSize");
    printf("before mmap : VmRSS=%ld kB  VmSize=%ld kB\n", rss0, vsz0);

    char* p = (char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    long rss1 = vm_field("VmRSS");
    long vsz1 = vm_field("VmSize");
    printf("after  mmap : VmRSS=%ld kB (delta %+ld)  VmSize=%ld kB (delta %+ld)\n",
           rss1, rss1 - rss0, vsz1, vsz1 - vsz0);

    for (long i = 0; i < npages; i++)
        p[i * page] = 1;

    long rss2 = vm_field("VmRSS");
    printf("after touch : VmRSS=%ld kB (delta %+ld)  VmSize=%ld kB\n",
           rss2, rss2 - rss1, vsz1);
    printf("pages touched=%ld -> in theory %ld kB (%.2f MiB)\n",
           npages, npages * page / 1024, npages * page / 1048576.0);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f6_rss f6_rss.cpp
$ ./f6_rss 3
touching 3 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=3968 kB  VmSize=7296 kB
after  mmap : VmRSS=4032 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=4044 kB (delta +12)  VmSize=1055872 kB
pages touched=3 -> in theory 12 kB (0.01 MiB)

$ ./f6_rss 3     (第 2 次)
touching 3 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4000 kB  VmSize=7296 kB
after  mmap : VmRSS=4064 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=4076 kB (delta +12)  VmSize=1055872 kB
pages touched=3 -> in theory 12 kB (0.01 MiB)

$ ./f6_rss 1000
touching 1000 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4004 kB  VmSize=7296 kB
after  mmap : VmRSS=4068 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=8068 kB (delta +4000)  VmSize=1055872 kB
pages touched=1000 -> in theory 4000 kB (3.91 MiB)

$ ./f6_rss 1000     (第 2 次)
touching 1000 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4000 kB  VmSize=7296 kB
after  mmap : VmRSS=4064 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=8064 kB (delta +4000)  VmSize=1055872 kB
pages touched=1000 -> in theory 4000 kB (3.91 MiB)
```

咱们把两列数字分开读。咱们做完 mmap，VmSize 涨了 1048576 kB，一字节不差地正好是 1 GiB，登记就算完成了。VmRSS 只涨了 64 kB，那点开销是页表页和 VMA 一类的管理结构，呼应咱们上一篇里算过的分级红利：要是平铺的大表，光是表就得吃 512 GiB 的内存。然后就轮到触碰了，咱们摸 3 页，VmRSS 就涨了 12 kB，摸到 1000 页就涨了 4000 kB。物理内存严格按摸过的页数乘 4 KiB 增长。咱们摸 3 页的时候，连 64 kB 的管理结构带 12 kB 的数据页一起算上，总的增量是 76 kB，映射的却是 1 GiB，隔在中间的就是登记与分配的分离。这里有个环境上的前提要交代：本机的透明大页是 madvise 模式，匿名的 mmap 全按 4 KiB 的基页走，您的机器要是 always 模式，大页就掺进来了，增量就不会这么整齐地跟着页数走了。mmap 这个调用本身还有很多可讲的，参数怎么配、文件映射怎么工作，咱们在 [vol8 的 mmap 专文](../../../vol8-domains/systems-programming/linux/file-io/02-mmap-memory-mapping.md)里有完整的实测。

## 翻译也要缓存：TLB

还记得上一篇咱们讲四级行走时记下的代价吗：每次地址翻译都要摸四张表，也就是四次额外的内存访问，比访问数据本身的动作还贵。真让 CPU 每次都这么走，任何程序都会慢得没法用了。实际的救兵是 TLB，它的全名是翻译后备缓冲（Translation Lookaside Buffer），它是 CPU 内部缓存近期翻译结果的小部件：最近翻译过的虚拟页号到物理页号的对应关系记在里面，等到下次访问同一页的时候，翻译就直接从它那里拿了，四张表就一步都不用走了。TLB 的容量有限，装不下的翻译就得重新走页表，咱们用一个轻量实验感受一下装不下的后果。实验的设计是每页只碰一个字节，数据的量小到可以一直住在缓存里，这样耗时的差异就主要落在翻译这一层：

```cpp
// f7_tlb.cpp —— TLB 压力演示（节选：计时三连与数组准备的核心；now_ms 读
// CLOCK_MONOTONIC 折算毫秒，make_perm 用固定种子洗出 0..n-1 的一个随机排列，
// 这两个帮手函数没有别的花样，此处省去）
//   A) 1 GiB 足迹，按页顺序走
//   B) 1 GiB 足迹，同样的页、随机次序走
//   C) 8 MiB 足迹，随机次序走（足迹整体落在 TLB 覆盖内）
static unsigned char* make_array(size_t sz) {
    unsigned char* p = (unsigned char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    madvise(p, sz, MADV_NOHUGEPAGE);   // keep 4 KiB base pages
    return p;
}

int main() {
    const long PS = sysconf(_SC_PAGESIZE);
    const size_t BIG = (size_t)1 << 30;        // 1 GiB
    const size_t SMALL = (size_t)8 << 20;      // 8 MiB
    const size_t nb = BIG / PS, ns_ = SMALL / PS;

    unsigned char* a_big = make_array(BIG);
    unsigned char* a_small = make_array(SMALL);
    auto perm_big = make_perm(nb, 42);
    auto perm_small = make_perm(ns_, 43);

    for (size_t i = 0; i < nb; i++) a_big[i * PS] = 1;      // warmup
    for (size_t i = 0; i < ns_; i++) a_small[i * PS] = 1;

    volatile unsigned long sink = 0;
    double t0;

    t0 = now_ms();
    for (int r = 0; r < 100; r++)
        for (size_t i = 0; i < nb; i++) sink += a_big[i * PS];
    double seq_big = now_ms() - t0;

    t0 = now_ms();
    for (int r = 0; r < 100; r++)
        for (size_t i = 0; i < nb; i++) sink += a_big[perm_big[i] * PS];
    double rnd_big = now_ms() - t0;

    t0 = now_ms();
    for (int r = 0; r < 1000; r++)
        for (size_t i = 0; i < ns_; i++) sink += a_small[perm_small[i] * PS];
    double rnd_small = now_ms() - t0;

    printf("A) 1 GiB, %zu pages, sequential order : %7.1f ms  (%5.1f ns/access)\n",
           nb, seq_big, seq_big * 1e6 / (100.0 * nb));
    printf("B) 1 GiB, %zu pages, random order     : %7.1f ms  (%5.1f ns/access)\n",
           nb, rnd_big, rnd_big * 1e6 / (100.0 * nb));
    printf("C) 8 MiB, %zu pages, random order     : %7.1f ms  (%5.1f ns/access)\n",
           ns_, rnd_small, rnd_small * 1e6 / (1000.0 * ns_));
    printf("B/A = %.1fx, B/C = %.1fx   (sink=%lu)\n",
           rnd_big / seq_big, rnd_big / (rnd_small * (100.0 * nb) / (1000.0 * ns_)),
           (unsigned long)sink);
    return 0;
}
```

声明咱们写在数字前面：这是演示级的测量，咱们在 WSL2 环境里跑、机器上没有 perf、单次运行、无进程绑定，数字说明的只是量级差异，它可算不上什么基准测试。咱们用 -O2 编译，连着跑了三回：

```text
$ g++ -O2 -Wall -Wextra -o f7_tlb f7_tlb.cpp
$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   228.0 ms  (  8.7 ns/access)
B) 1 GiB, 262144 pages, random order     :   370.6 ms  ( 14.1 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.6 ms  (  4.2 ns/access)
B/A = 1.6x, B/C = 3.4x   (sink=54476800)

$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   219.8 ms  (  8.4 ns/access)
B) 1 GiB, 262144 pages, random order     :   369.2 ms  ( 14.1 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.7 ms  (  4.2 ns/access)
B/A = 1.7x, B/C = 3.3x   (sink=54476800)

$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   220.5 ms  (  8.4 ns/access)
B) 1 GiB, 262144 pages, random order     :   367.6 ms  ( 14.0 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.8 ms  (  4.3 ns/access)
B/A = 1.7x, B/C = 3.3x   (sink=54476800)
```

三次运行的数字很稳。咱们一组组读。B 对 A 慢约 1.6 到 1.7 倍：同样的 262144 页，顺序走页时页表本身的访存也是顺序落在页表页里的，硬件的预取跟得上。咱们换成随机次序，262144 个页的翻译远超 TLB 的覆盖（咱们这块 9700X 是 AMD 的 Zen 5 架构，它的 L2 TLB 大约几千项、配上 4 KiB 的页也就覆盖十几 MiB），大部分的访问都得重新走页表。最能说明问题的是 C：跳法与 B 完全相同，足迹缩到了 8 MiB，2048 个页整个住进了 TLB 与低级缓存的覆盖，单次的访问从 14.1 ns 掉到了 4.2 ns，比 B 快了 3.3 倍。不过 C 的红利咱们得诚实拆分：它的 2048 个缓存行总共 128 kB、全部住得进 L1 和 L2，而 B 的 262144 行有 16 MiB、散在 L3 里，所以 B/C 的 3.3 倍里同时装着 TLB 命中与否和缓存层级两份贡献，两份贡献咱们拆不开，咱们不能把它精确归因给 TLB。实验的另一个版本还能帮咱们排除一个干扰：笔者头一版用的是 256 MiB 数组，触碰的行总共 4 MiB，数据都住进了 L3，顺序对随机同样慢了 1.6 倍上下（实测的数据是 58.6 ms 对 93.0 ms，折算的每访问耗时是 8.9 对 14.2 ns），说明这样的差距在数据完全驻留缓存的时候也稳定存在，可以放心归给翻译这一层的访问次序，而不是内存带宽。TLB 的容量与用 2 MiB 大页扩大覆盖的完整讲法，在 [vol6 的 TLB 专文](../../../vol6-performance/ch02-cpu-microarchitecture/02-04-tlb-hugepage-and-cpu-families.md)里，那边的视角比咱们在这里的演示完整得多。

## 链路回放

等咱们走完这一路，再把开头的那个 `%p` 从头到尾念一遍。printf 打出来的是 48 位的虚拟地址。它的低 12 位是页内偏移，翻译前后它的值都不动。高 36 位则拆成四段的九位，是四张表的接力路标。CPU 从 CR3 拿到 PML4 的物理地址，咱们顺着四张表逐级下探，末级表里的 PTE 给出物理页号，拼上偏移就是内存条上的真实位置。PTE 里除了页号还有标志位：present 说这页在不在物理内存里，页不在了就触发缺页，合法的缺页由内核补页，上一篇 f3b 那个 `0x0080` 到 `0x8180` 的翻转就是补页的手笔，非法的缺页升级成 SIGSEGV，f5_segv 的 si_code 就是判决书。映射与分配是分离的，1 GiB 的登记只动 64 kB 的管理结构，物理内存按摸过的页数给。翻译这一层有 TLB 给咱们兜着，覆盖之外的访问要付四张表的代价，随机跳跃的 1.6 倍差距就是它的量尺。

咱们这个系列走到这里，链条也算接完了。01 和 02 把指令与栈帧搭了起来，03 讲清了参数怎么进寄存器，04 与 05 把越界的后果和对象的本体摆到了眼前，咱们这几篇把指针值接到了内存条上的真实位置。往后您调试的时候，您看到 `0x7ffd...` 开头的地址，就知道它住在栈区域了。您碰上段错误的时候，页表走查失败的根子也在您手里了。再看程序占了多少内存的问题，VmSize 量的是登记了多大的空间，VmRSS 记录的才是真正占用的量，两边也就不会再混了。

收尾把最省事的自证留给您：您把 f6_rss 编一份出来，一回传了 3，一回传了 1000。您会看到 VmSize 两回都涨 1048576 kB，VmRSS 却一回只涨了 12 kB、一回涨了 4000 kB，您摸几页，物理内存就给您几页，一个字节都不会多给您。
