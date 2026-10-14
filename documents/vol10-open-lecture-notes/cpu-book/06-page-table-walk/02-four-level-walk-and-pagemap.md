---
title: "四张表接力：48 位地址的拆解与 pagemap 里的页表项"
chapter: 6
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "走一遍 PML4、PDPT、PD、PT 四张表的接力，拿程序把三个真实地址拆成四段九位，再到 /proc/self/pagemap 把页表项逐位读出来，亲眼看 present 位在第一次触碰的瞬间从 0 翻成 1"
reading_time_minutes: 14
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 四张表接力：48 位地址的拆解与 pagemap 里的页表项

上一篇把平铺大表的代价算死了：512 GiB，比机器的内存还大十倍，所以咱们的出路是把表分级。这一篇咱们就走 x86-64 的四级页表，看 48 位地址怎么拆成四段九位、四张表怎么从 CR3 一路接到页表项，咱们再到 /proc/self/pagemap 里把页表项逐位读出来。咱们用的环境与[上一篇](01-virtual-address-and-4k-pages.md)相同，您要复跑就照它来。

## 四张表接力：48 位地址拆成四段九位

x86-64 的分级页表是四级。每张表的大小被规定成一个页：4 KiB 的表里放 512 项，每项的大小是 8 字节，512 乘 8 正好是一页的容量。512 等于 2 的 9 次方，所以每一级用掉虚拟地址里的 9 个比特当索引。四级的索引一共用掉 36 位，再算上 12 位的页内偏移，48 位就刚好用完了。四张表的名字从顶往下依次是 PML4、PDPT、PD、PT，名字带着历史沿革的痕迹，咱们按位置叫它们顶级表、上级表、中间表、末级表也行，本篇的实验输出里用的是 PT 索引、PD 索引这样的叫法。顶级的入口放在 CPU 的 CR3 寄存器里，它存的是 PML4 表的物理地址，每个进程有自己的 CR3 值，切换进程其实就是换一个 CR3，每个进程一套独立地址空间的硬件支点也就在这里。Intel 的手册原文写的是 `CR3 is used to locate the first paging-structure, the PML4 table`，咱们把它翻译过来，CR3 定位的就是第一张分页结构，也就是顶级的 PML4 表。咱们查的版本是 Intel 64 and IA-32 Architectures SDM，Vol 3A 的第 `4.5` 节，检索的日期是 2026 年 10 月 12 日。

咱们拿程序把三个真实地址拆解来看。位段的划分是固定的：bits 47 到 39 当 PML4 的索引，bits 38 到 30 当 PDPT 的索引，bits 29 到 21 当 PD 的索引，bits 20 到 12 当 PT 的索引，剩下的低 12 位就是页内偏移：

```cpp
// f4_split.cpp —— 把 48 位虚拟地址拆成 12 位偏移加四段 9 位索引
#include <cstdio>
#include <cstdint>
#include <cstdlib>

int main(int argc, char** argv) {
    uint64_t addr = 0x7fffffffd0dcULL;   // F1 setarch -R 那一组的 &stack_var
    if (argc > 1) addr = strtoull(argv[1], nullptr, 0);

    printf("address            = 0x%016llx\n", (unsigned long long)addr);
    printf("bits used (48-bit) = 0x%012llx\n", (unsigned long long)addr);

    unsigned off  = addr & 0xFFF;              // bits  0-11
    unsigned l1   = (addr >> 12) & 0x1FF;      // bits 12-20  PT index
    unsigned l2   = (addr >> 21) & 0x1FF;      // bits 21-29  PD index
    unsigned l3   = (addr >> 30) & 0x1FF;      // bits 30-38  PDPT index
    unsigned l4   = (addr >> 39) & 0x1FF;      // bits 39-47  PML4 index
    unsigned top  = (addr >> 48) & 0xFFFF;     // bits 48-63  canonical hole

    printf("page offset (bits  0-11, 12 bits) = 0x%03x\n", off);
    printf("L1/PT   index (bits 12-20,  9 bits) = 0x%03x  (%u)\n", l1, l1);
    printf("L2/PD   index (bits 21-29,  9 bits) = 0x%03x  (%u)\n", l2, l2);
    printf("L3/PDPT index (bits 30-38,  9 bits) = 0x%03x  (%u)\n", l3, l3);
    printf("L4/PML4 index (bits 39-47,  9 bits) = 0x%03x  (%u)\n", l4, l4);
    printf("bits 48-63 (canonical sign ext.)   = 0x%04x\n", top);
    printf("check: %llx == %llx ? %s\n",
           (unsigned long long)(((((uint64_t)l4 << 9 | l3) << 9 | l2) << 9 | l1) << 12 | off),
           (unsigned long long)(addr & 0xFFFFFFFFFFFFULL),
           (((((uint64_t)l4 << 9 | l3) << 9 | l2) << 9 | l1) << 12 | off) == (addr & 0xFFFFFFFFFFFFULL) ? "yes" : "NO");
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f4_split f4_split.cpp
$ ./f4_split 0x7fffffffd0dc
address            = 0x00007fffffffd0dc
bits used (48-bit) = 0x7fffffffd0dc
page offset (bits  0-11, 12 bits) = 0x0dc
L1/PT   index (bits 12-20,  9 bits) = 0x1fd  (509)
L2/PD   index (bits 21-29,  9 bits) = 0x1ff  (511)
L3/PDPT index (bits 30-38,  9 bits) = 0x1ff  (511)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ff  (255)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 7fffffffd0dc == 7fffffffd0dc ? yes
===
$ ./f4_split 0x5724fca5a169
address            = 0x00005724fca5a169
bits used (48-bit) = 0x5724fca5a169
page offset (bits  0-11, 12 bits) = 0x169
L1/PT   index (bits 12-20,  9 bits) = 0x05a  (90)
L2/PD   index (bits 21-29,  9 bits) = 0x1e5  (485)
L3/PDPT index (bits 30-38,  9 bits) = 0x093  (147)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ae  (174)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 5724fca5a169 == 5724fca5a169 ? yes
===
$ ./f4_split 0x7ffd89c0f6ac
address            = 0x00007ffd89c0f6ac
bits used (48-bit) = 0x7ffd89c0f6ac
page offset (bits  0-11, 12 bits) = 0x6ac
L1/PT   index (bits 12-20,  9 bits) = 0x00f  (15)
L2/PD   index (bits 21-29,  9 bits) = 0x04e  (78)
L3/PDPT index (bits 30-38,  9 bits) = 0x1f6  (502)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ff  (255)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 7ffd89c0f6ac == 7ffd89c0f6ac ? yes
```

程序末尾的 check 行把四段索引拼回地址再比对，三个地址全部闭合，说明拆的思路和拼的思路互为逆运算，这套位段的划分在本机是自洽的。Intel 手册对每级的位段也有明文。末级表的原文是 `A page table comprises 512 64-bit entries (PTEs). ... Bits 11:3 are bits 20:12 of the linear address`，末级表的容量是 512 项，索引取线性地址的 20:12 位。顶级那层的原文则是 `Bits 11:3 are bits 47:39 of the linear address`，取的是 47:39 位。咱们实验里 l4 取 39 到 47 位、l1 取 12 到 20 位，与手册的位段逐段对得上。

咱们把第一个地址的行走过程念一遍。CPU 从 CR3 拿到 PML4 表的物理地址，用 255 号槽位（bits 47:39 的值）取出 PML4 项，项里写着一张 PDPT 表的物理地址。再用 511 号槽位（bits 38:30）在 PDPT 里取出 PD 的物理地址，用 511 号槽位（bits 29:21）在 PD 里取出 PT 的物理地址，用 509 号槽位（bits 20:12）在 PT 里取出最终的一项，也就是咱们说的页表项 PTE。PTE 里装的是物理页号，拼上低 12 位的 `0x0dc`，物理地址就到手了。一次访存咱们要摸四张表，这个代价咱们记下，下一篇讲 TLB 的时候还会回来算它。

输出里还有一个漂亮的小发现。咱们挑的地址里有两个是栈地址，一个是关掉 ASLR 的 `0x7fffffffd0dc`，一个是开着 ASLR 的 `0x7ffd89c0f6ac`，两者隔着随机化的差距，拆出来的 PML4 索引却都是 255。这不是巧合：x86-64 的惯例是用户空间住在 PML4 的 0 到 255 号槽位，内核住的则是 256 到 511 号，而 255 号是用户半区最顶上的一格。栈在用户空间里本来就住在最高的地方，所以随机化怎么挪，它都顶在 255 号的天花板下面。代码段地址拆出的 PML4 索引是 174，也在用户半区的范围里。

地址位宽还有一段边角要交代。手册说 IA-32e 分页把 48 位线性地址翻译到最多 52 位物理地址，本机 cpuinfo 报的却是 48 位物理，52 在咱们这里只是架构允许的上限，咱们按本机的 48 位讲。至于 48 位以上的 16 位，它们的填法是跟着 bit 47 做符号扩展，咱们三个地址的高 16 位都是 0，本篇就不往那边展开了。

咱们在这里给四级行走留一个图位，图待补：内容为从 CR3 指着的 PML4 起，四张表逐级下探到物理页的整条链路，标出 47:39、38:30、29:21、20:12 四段索引各自取自虚拟地址的哪几位，页内偏移如何原样接到物理页号的后面。

分级为什么省，咱们粗算一笔就够。顶级的表永远只有一张，占的也就 4 KiB。它的一项管多大地盘？手册的原文是 `Because a PML4E is identified using bits 47:39 of the linear address, it controls access to a 512-GByte region of the linear-address space`，一项 PML4E 对应 512 GiB 的线性地址区域。没用到的 512 GiB 区域，顶级表里的那一项空着，下面的三张整表就都不用存在。一个普通进程真正用到的地址，摊在几个 512 GiB 的区域里，每条路径上的表加起来也就几十张，每张的代价是 4 KiB，总开销从 512 GiB 掉到了几十 KiB 的量级。下一篇里 f6_rss 的实验会给出一个实测数：映射 1 GiB 的内存区域，页表加管理结构的开销总共才 64 kB。

## 到 /proc/self/pagemap 把页表项读出来

页表项 PTE 里到底装了什么，咱们不用猜，Linux 把它摊开给咱们看了。`/proc/self/pagemap` 是内核提供的一个接口文件，每个虚拟页在里面占一个 8 字节的条目，按虚拟页号的次序排，所以想读某个地址的 PTE，就把地址除以页大小得到页号，再乘以 8 得到文件内的字节偏移。咱们写程序去读一个栈变量：

```cpp
// f3_pagemap.cpp —— 从 /proc/self/pagemap 读栈变量的 PTE 并拆解各位
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

static void dump_pte(const char* label, uintptr_t vaddr) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t vpn = vaddr / page_size;          // virtual page number
    uint64_t off  = vpn * sizeof(uint64_t);     // byte offset into pagemap

    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("open /proc/self/pagemap"); exit(1); }

    uint64_t pte = 0;
    if (lseek(fd, (off_t)off, SEEK_SET) == (off_t)-1) { perror("lseek"); exit(1); }
    if (read(fd, &pte, sizeof(pte)) != (int)sizeof(pte)) { perror("read"); exit(1); }
    close(fd);

    uint64_t pfn     = pte & ((1ULL << 55) - 1);  // bits 0..54
    int soft_dirty   = (pte >> 55) & 1;
    int exclusive    = (pte >> 56) & 1;           // "exclusively mapped"
    int file_page    = (pte >> 61) & 1;
    int swapped      = (pte >> 62) & 1;
    int present      = (pte >> 63) & 1;

    printf("%s vaddr=0x%012lx\n", label, (unsigned long)vaddr);
    printf("  vpn (vaddr>>12)            = %lu (0x%lx)\n", (unsigned long)vpn, (unsigned long)vpn);
    printf("  pagemap byte offset        = %lu (0x%lx)\n", (unsigned long)off, (unsigned long)off);
    printf("  raw 64-bit entry           = 0x%016llx\n", (unsigned long long)pte);
    printf("  bit63 present              = %d\n", present);
    printf("  bit62 swapped              = %d\n", swapped);
    printf("  bit61 file-backed          = %d\n", file_page);
    printf("  bit56 exclusively-mapped   = %d\n", exclusive);
    printf("  bit55 soft-dirty           = %d\n", soft_dirty);
    printf("  bits0-54 PFN               = %llu (0x%llx)\n",
           (unsigned long long)pfn, (unsigned long long)pfn);
    printf("  -> phys addr (PFN<<12)     = 0x%013llx%s\n",
           (unsigned long long)(pfn << 12),
           pfn == 0 ? "   [PFN field is ZERO - not readable at this privilege]"
                    : "");
}

int main(int argc, char** argv) {
    int on_stack = 0x1234;
    uintptr_t stack_addr = (uintptr_t)&on_stack;

    printf("uid = %d\n", getuid());
    if (argc > 1) printf("argv[1] = %s\n", argv[1]);

    printf("--- before touching dedicated mmap page ---\n");
    dump_pte("stack-before", stack_addr);

    on_stack = on_stack + 1;
    printf("--- after touching stack var ---\n");
    dump_pte("stack-after ", stack_addr);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f3_pagemap f3_pagemap.cpp
$ ./f3_pagemap
uid = 1000
--- before touching dedicated mmap page ---
stack-before vaddr=0x7ffcce28160c
  vpn (vaddr>>12)            = 34356388481 (0x7ffcce281)
  pagemap byte offset        = 274851107848 (0x3ffe671408)
  raw 64-bit entry           = 0x8180000000000000
  bit63 present              = 1
  bit62 swapped              = 0
  bit61 file-backed          = 0
  bit56 exclusively-mapped   = 1
  bit55 soft-dirty           = 1
  bits0-54 PFN               = 0 (0x0)
  -> phys addr (PFN<<12)     = 0x0000000000000   [PFN field is ZERO - not readable at this privilege]
--- after touching stack var ---
stack-after  vaddr=0x7ffcce28160c
  (与 before 完全相同：raw 0x8180000000000000，present=1，PFN=0)
```

咱们还得交代一下输出里的标签：before touching dedicated mmap page 那行是打印函数里写死的统一文案，f3 阶段它对应的其实是栈变量所在的页，专门 mmap 的干净页要到 f3b 才登场，after 那行写的就是栈变量，反倒是对得上的。

咱们拿原始值 `0x8180000000000000` 手工验一遍，免得咱们信程序信得太快。这个数的最高 16 位是 `0x8180`，它的二进制是 `1000 0001 1000 0000`，咱们从 bit 63 往下数，立着的有 bit 63、bit 56 与 bit 55，其余的高位都是 0。程序打印的 present、exclusively-mapped、soft-dirty 三个 1，与手算的结果一致。

这些位各自的含义，内核文档 [pagemap 手册页](https://docs.kernel.org/admin-guide/mm/pagemap.html)（对应内核源码树的 `Documentation/admin-guide/mm/pagemap.rst`，检索日期 2026 年 10 月 12 日）给了逐位定义。咱们摘几条关键的：`Bits 0-54 page frame number (PFN) if present`，说的是 bits 0 到 54 为物理页号。`Bit 55 pte is soft-dirty`，说的是 bit 55 标记脏页。`Bit 56 page exclusively mapped`，说的是 bit 56 表示这页没有被共享。`Bit 63 page present`，说的是 bit 63 表示页在物理内存里。对照咱们读到的值：栈变量所在的页 present 为 1，说明它已经在物理内存里了。exclusive 的取值为 1，说明它没有被别的映射共享。soft-dirty 的取值为 1，说明它被写过了。

真正的戏在 PFN 上。按文档的位定义，bits 0 到 54 放的应该是物理页号，咱们读到的却是 0。实验到这儿其实没有失败，打码是内核故意安排的。文档的原文写得很直白：`Since Linux 4.0 only users with the CAP_SYS_ADMIN capability can get PFNs`，说的是从 Linux 4.0 起，只有持有 CAP_SYS_ADMIN 能力的用户能拿到物理页号。`Starting from 4.2 the PFN field is zeroed if the user does not have CAP_SYS_ADMIN`，说的是从 4.2 起，没有这个能力的读者看到的 PFN 字段是被清零的。为什么藏？文档也给了理由：`information about PFNs helps in exploiting Rowhammer vulnerability`，物理页号的信息能帮上 Rowhammer 这类漏洞的利用。Rowhammer（行锤）是一类针对 DRAM 的硬件攻击手法，靠的是对同一行反复读写、去干扰相邻行，攻击者要是知道了各页在物理内存里的相邻关系，瞄准的难度就低得多。所以咱们普通用户能读到这一页在不在物理内存里，却读不到它住的位置。地址翻译的另一半，只对有特权的读者开放。

咱们顺手做了两个对照，两个都值得咱们记下。头一个用 `unshare -Ur` 把程序放进新的用户命名空间里跑，程序眼里的 uid 是 0，自己看自己就是 root 了：

```text
$ unshare -Ur ./f3_pagemap 2>&1 | head -20
uid = 0
--- before touching dedicated mmap page ---
stack-before vaddr=0x7ffe2c0cc24c
  vpn (vaddr>>12)            = 34357821644 (0x7ffe2c0cc)
  pagemap byte offset        = 274862573152 (0x3fff160660)
  raw 64-bit entry           = 0x8180000000000000
  bit63 present              = 1
  bit62 swapped              = 0
  bit61 file-backed          = 0
  bit56 exclusively-mapped   = 1
  bit55 soft-dirty           = 1
  bits0-54 PFN               = 0 (0x0)
  -> phys addr (PFN<<12)     = 0x0000000000000   [PFN field is ZERO - not readable at this privilege]
--- after touching stack var ---
stack-after  vaddr=0x7ffe2c0cc24c
  vpn (vaddr>>12)            = 34357821644 (0x7ffe2c0cc)
  pagemap byte offset        = 274862573152 (0x3fff160660)
  raw 64-bit entry           = 0x8180000000000000
  bit62 swapped              = 0
  bit61 file-backed          = 0
exit=0
```

（`head -20` 把 after 段的最后几行裁掉了，截断处咱们如实保留。）uid 明明已经是 0 了，PFN 的字段却还是 0。原因在于内核查的是初始用户命名空间里的 CAP_SYS_ADMIN，您进程里看到的那个 uid 不在它的检查范围里，新命名空间里自封的 root 在它眼里不作数。打码看的是特权，不看 uid 的数字，咱们从这个对照里把边界看得很清楚。第二个对照咱们想上真正的特权，用 sudo 试了一把：

```text
$ sudo -n ./f3_pagemap
sudo: a password is required
exit=1
```

笔者做实验的终端是非交互会话，密码给不了，特权形态的数字这一轮咱们拿不到。所以本篇里 PFN 的真值一个数字都不会出现，咱们不编。您手头要是有人工终端，`sudo ./f3_pagemap` 跑一次就能看到打码揭开的样子，内核文档给的承诺是那时的 PFN 字段会如实填上物理页号。

## present 位翻起来的瞬间，是缺页处理填的表

咱们回头再想一件事：f3 的输出里，before 和 after 两段是一模一样的。咱们本来是想在触碰变量前后各读一次 PTE，看 present 位从 0 翻成 1 的。可栈变量在第一次读数之前就已经被 printf 摸过了，轮到咱们看的时候 present 早就是 1，翻位的瞬间压根没被拍到。咱们想拍的正是这个瞬间，那就得拿一页肯定没人碰过的干净页。咱们专门 mmap 一页新的匿名内存，读了它的 PTE，写了一个字节进去，再读了一遍：

```cpp
// f3b_present.cpp —— 在没人碰过的全新匿名页上观察 present 位 0 到 1 的翻转
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static uint64_t read_pte(uintptr_t vaddr) {
    long page = sysconf(_SC_PAGESIZE);
    uint64_t off = (vaddr / page) * 8;
    int fd = open("/proc/self/pagemap", O_RDONLY);
    uint64_t pte = 0;
    lseek(fd, (off_t)off, SEEK_SET);
    if (read(fd, &pte, 8) != 8) { perror("read"); _exit(1); }
    close(fd);
    return pte;
}

int main() {
    long page = sysconf(_SC_PAGESIZE);
    char* p = (char*)mmap(nullptr, page, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    uint64_t before = read_pte((uintptr_t)p);
    p[0] = 7;                                  // first touch: page faults in
    uint64_t after = read_pte((uintptr_t)p);

    printf("fresh anonymous page at %p\n", (void*)p);
    printf("PTE before touch = 0x%016llx  (bit63 present = %llu)\n",
           (unsigned long long)before, (unsigned long long)(before >> 63 & 1));
    printf("PTE after  touch = 0x%016llx  (bit63 present = %llu)\n",
           (unsigned long long)after, (unsigned long long)(after >> 63 & 1));
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f3b_present f3b_present.cpp
$ ./f3b_present
fresh anonymous page at 0x7ea607119000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)

$ ./f3b_present
fresh anonymous page at 0x721d30fcc000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)

$ ./f3b_present
fresh anonymous page at 0x767a3d2ec000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)
```

咱们跑了三次，三次看到的都是同一个翻转。咱们没碰它的时候，PTE 的值是 `0x0080000000000000`，bit 63 的值为 0，条目里立着的只有 bit 55（soft-dirty），那是内核做脏页跟踪的标记。等咱们写完一个字节，PTE 变成了 `0x8180000000000000`，bit 63 立起来了。mmap 完成的时候页表项里还没有这页的登记，第一次的写入触发了一次缺页处理，内核在这一下里干的活是分配物理页、把 PFN 和标志位填进 PTE，然后才轮到咱们的写指令真正落进内存。咱们看到的这一对原始值，就是页表项被缺页处理填写的直接观测。缺页往后的两条路怎么分，咱们下一篇接着走。
