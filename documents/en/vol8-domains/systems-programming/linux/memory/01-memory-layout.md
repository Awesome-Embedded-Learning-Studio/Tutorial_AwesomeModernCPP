---
title: "Process Memory Layout: Full Map of /proc/pid/maps"
description: "When a C++ program runs, each segment in its address space is occupied by something. In this article, we dissect a real program using /proc/self/maps into 41 segments, classifying them into six major categories (program image, heap, shared libraries, anonymous segments, stack, vvar/vdso). We identify the second r-- segment of each file module as GNU_RELRO, which is marked by the dynamic linker after relocation and made read-only by mprotect. 18 variable types are matched one by one with 18/18 consistency, revealing that the .bss section spans across segments (starting from the file's rw segment's last page and continuing into anonymous pages), and &printf is located in libc's .text. The malloc threshold is measured at 131049/131050, and strace reveals the real mechanism: the heap has extra space and is cut on the spot, chunk size is rounded up (request + 8, 16) and only when it is greater than or equal to 131072 does it use mmap. The textbook and malloc(3) differ by one level. Freeing 4MB will raise the glibc dynamic threshold to 4MB. brk will trim and shrink, and the stack's top remains fixed at the low address end, descending 3180KB. Rss and Pss allocate the 1004kB of libc .text into 7kB. The .text of the freshly compiled binary is Private_Dirty, and after sync, it becomes Clean. ASLR varies across five runs (exe spans about 13.9TiB, stack 7.09GiB), while setarch -R produces identical results across five runs, which is the world gdb defaults to."
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 31
prerequisites:
  - "Memory Mapping: Attaching Files to Address Space"
  - "Page Cache and Persistence: What Happens After write()"
related:
  - "File Mapping: CreateFileMapping and MapViewOfFile"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 内存管理
current_status:
  title: Being Polished
  detail: This batch has just been written and is currently being proofread and polished. The expressions and experimental results may be adjusted at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/memory/01-memory-layout.md
  source_hash: 13384aac050f9c76870b0310a74619fb1304e839acd4d1ac72ae06728f8d0305
  translated_at: '2026-11-22T15:09:36+00:00'
  engine: qwen3:14b
  token_count: 19368
---
# Process Memory Layout: Full Map of /proc/pid/maps

After completing the six articles on file I/O, we have changed our observation instruments several times, while /proc/self/maps has always been present: we used it to look at the mapped area itself in [L02](../file-io/02-mmap-memory-mapping.md), and meminfo took over when we looked at the dirty page count in [L03](../file-io/03-page-cache.md). In this article, we bring it to the center, zoom out to the farthest point, and look at who lives in the **entire address space** of a C++ program when it runs. We have already explained how page faults convert virtual addresses into physical pages in L02, and we will not repeat that here. Today, we are doing something else: looking at all the segments at once.

The question starts with the address of a variable in main. When you write a global array, a static local variable, a new, and the printf you call every day, where do they end up in the segments? Names like .text, .data, .bss, which we have memorized in our compiler theory class, are just names. We don't make judgments without evidence. In this article, we dissect a real C++ program, go through each of the 41 segments line by line, and match the addresses of 18 types of variables one by one. Along the way, we also measure four things: the malloc threshold, the stack growth direction, the smaps count, and the ASLR randomness.

The experiments in this article are numbered E1 to E5, corresponding one by one to the directories 01 to 05 under the repository code/volumn_codes/vol8/systems-programming/linux/memory/01-memory-layout/. The original outputs are also included there, and you can match them at any time. In [L02](../file-io/02-mmap-memory-mapping.md), we used lowercase e1 to e5, and in [L03](../file-io/03-page-cache.md), we used uppercase E1 to E6, which are the numbers for each of those respective articles. They are unrelated to us here, so please don't mix them up. The experimental environment is clear: we are running on WSL2, the kernel version is 6.18.33.2-microsoft-standard-WSL2, the glibc version is 2.44, the compiler is g++ 16.2.1, and all the compilation parameters are the same, using -std=c++20 -Wall -Wextra -Wpedantic -O2, and the number of warnings obtained is zero. The transparent huge page mode is madvise, ulimit -s reports 8192KB, the former is used for E4's THP observation, and the latter is used for E3's stack experiment. There is also a methodological discipline: each program reads its own /proc/self/maps and /proc/self/smaps while running. We look at ourselves, so there is no problem of time difference or permission when reading other processes. The unique_fd and sys_call defined in the two thinking foundation articles (the RAII article and the error handling article) are not used in this article. Our experiments are all read-only observations, and there are no resources that need to be held long-term.

## E1: One maps, 41 segments, six major categories

The six-column format of maps has been explained column by column in L02, and we will not repeat it here. Today, we are looking at the full picture of the rows. The program in E1 prints its 41 lines of maps exactly as they are, and classifies each segment. Below is the original text from the same run. The path column has too long characters, so we have shortened the repository prefix to …/01-maps, and the address and other columns are unchanged:

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

We start from the lowest address. The first five lines have the same inode of 1689135, and they all belong to the program itself. By comparing with the ELF header of the binary, the origin of the five segments becomes clear (readelf is the one we ran after recompiling the same source code today, and the layout is consistent with the archive):

```text
LOAD           0x000000 0x0000000000000000 0x0000000000000000 0x001be0 0x001be0 R   0x1000
LOAD           0x002000 0x0000000000002000 0x0000000000002000 0x0063fd 0x0063fd R E 0x1000
LOAD           0x009000 0x0000000000009000 0x0000000000009000 0x000f50 0x000f50 R   0x1000
LOAD           0x00ab10 0x000000000000ab10 0x000000000000ab10 0x000688 0x0006f0 RW  0x1000
GNU_RELRO      0x00ab10 0x000000000000ab10 0x000000000000ab10 0x0004f0 0x0004f0 R   0x1
```

The kernel maps according to four LOAD instructions. The first read-only segment contains the ELF header and metadata that the linker needs to see, followed by the executable segment, which is exactly the 28KB in maps. The next LOAD is read-only, containing .rodata. The last LOAD is writable, and it has two different sizes: the actual size in the file is 0x688, and the expected size at runtime is 0x6f0, with .data and .bss both attached to it. The four LOAD instructions result in five lines on maps, with the extra line being the "r--p 0000a000", which is worth taking a closer look at.

It is the product of **RELRO**, which stands for RELocation Read-Only, and we translate it as relocation read-only. It is the conclusion of dynamic linking. There is also a program header called "GNU_RELRO" in the ELF, which marks a small segment at the beginning of the writable LOAD, covering 0x4f0 bytes, covering .init_array, .dynamic, and .got. The dynamic linker fills in the relocations at startup, and after filling them in, it makes the marked segment read-only with mprotect. The first page of rw is changed in permissions, and naturally, maps splits into two lines: r-- and rw. This procedure is to protect .got (global offset table): the table contains a row of final addresses of functions and variables. One of the traditional attack methods is to find a way to write to the table and change the call to another place. Once the relocations are completed, it becomes read-only, and the path to write to the table is cut off. The build we have is partial RELRO, and the main body of .got.plt used for lazy binding is still in the writable page, leaving room for the first call at runtime. Therefore, the page on maps is read-only, and the next page is still writable.

One level up is [heap], about 204KB in size. We will measure it again in E3. The next 26 lines are all the territory of shared libraries. We count from libc, libm, libstdc++, all the way to libgcc_s, and even include the segments of the dynamic linker ld-linux itself. Each library has the same five segments as the main program, and most libraries also have an additional anonymous rw segment right after their own rw segment (libm is an exception, its .bss is small, and its last page can hold its own). This is the continuation page of .bss: the part of memsz that exceeds the file is filled in by the kernel with anonymous zero pages. In E2, we verify it with variable addresses. The small file /etc/ld.so.cache also occupies one line, which is the library lookup cache of the dynamic linker, and it is mapped in as well. The three lines above are small read-only segments [vvar], [vvar_vclock], and [vdso].

**vdso** stands for virtual dynamic shared object, and its Chinese name is virtual dynamic shared object. It is a small piece of code that the kernel maps into each process. When we call gettimeofday and clock_gettime, we only need to read the kernel's time, and using vdso allows us to get the answer in the form of a regular function call, saving the cost of a system call to enter and exit the kernel. The kernel data it reads is placed in the [vvar] page. On our machine, part of it is also separately marked as [vvar_vclock]. Older kernels do not have such segment names in maps. There is also a [vsyscall] in older x86_64 materials, but the WSL2 kernel configuration does not leave it, and we cannot wait for it to appear in WSL2. The highest [stack] is the main thread's stack, which is the most familiar segment for us.

The 41 segments can be classified into six major categories: the program itself occupies 5 segments, [heap] occupies 1 segment, the segments of shared libraries are the most, occupying 26 segments, the anonymous segments without file backing occupy 5 segments, the stack occupies 1 segment, and the segments related to vvar and vdso occupy 3 segments. The statistics on permissions are more worth looking at: r--p occupies 21 segments, the segments with execution permissions r-xp only occupy 7 segments, and the segments with write permissions rw-p occupy 13 segments. When we usually talk about how big a program is, looking at the statistics on permissions, the majority of the bytes are metadata and read-only data, and only three-tenths of the segments are actually writable, and more than half of these three-tenths are the data segments of various libraries.

## E2: 18 types of variables, matched one by one

After recognizing the maps, we change direction and start from the beginning of the code. The program in E2 declares 18 typical variables, takes their addresses, and reads the maps from the **same run**, sending each address back to the segment it falls into. **ASLR** (Address Space Layout Randomization) only scrambles the addresses between different runs, and within the same run, it is self-consistent, so the matching is valid. The variables being probed look like this:

```cpp
int g_init = 0x42;                                  // .data
char g_data_buf[64 * 1024] = {1};                   // .data(放大让段可见)
char g_bss_buf[1 << 20];                            // .bss(1MB,必然顶进匿名接续页)
int g_bss_small;                                    // .bss(小变量,见下方说明)
const int g_const = 7;                              // .rodata
const char* g_lit = "lm01-rodata-literal";          // 指向 .rodata 的字符串字面量

__attribute__((noinline)) int probe_target(int x) { return x + 1; }  // .text
```

The part of taking addresses also includes stack variables, malloc, new, and &printf. We have saved them in the archive, and the main text only picks a few lines with significance:

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

All 18 items match, and we pick three lines to look at in detail. The first line is actually two faces of .bss, and it is the main discovery of E2. g_bss_buf is a 1MB uninitialized array, its starting point falls in the **last page of the file's rw segment**, and the middle 512KB falls into the **anonymous continuation page** immediately after. The same array we wrote is thus cut into two parts by the page boundary. The textbook says that .bss does not occupy the file and is zeroed at runtime, which is correct, but it only covers the first half. The two sizes mentioned in E1 are useful here, and their difference is .bss. The kernel only maps the file's rw segment up to the page boundary, zeros the bytes after the filesz in the last page, which exactly fits the beginning of .bss, and the remaining space is filled with anonymous zero pages. The destination of small bss variables is clear: g_bss_small and static local s_bss (the static local in the archive without the initialization expression) are placed after .data, and they have not exceeded the boundary of the last page, so they are mixed in the same page as .data, and they cannot be distinguished on maps.

The second line we look at is &printf. 0x712c3625b8b0 falls in the executable segment of libc, corresponding to the segment 712c36224000-712c3639f000. The printf you call, not a single byte of code is in your binary, it is in the .text of libc. Your call instruction has to go through the dynamic linking jumpboard to get there, and the .got that E1 mentioned, which is protected by the RELRO, is the address table that dynamic linking needs to look up. However, under partial RELRO, the .got.plt that needs to be rewritten by the first call is still in the writable page. We also look at the line for new char[4MB], which falls outside of [heap], and is in the same anonymous area as the 2MB interval we directly mmap, located below all the shared libraries. When does the request of malloc stay in [heap] and when does it go out, this is the main topic of E3.

## E3: Moving Boundaries: The Threshold of malloc, the Descent of the Stack, and the Return of free

Before we get our hands dirty, we need to explain a term: **brk** is the system call that moves the top boundary of the heap, and the [heap] marked in maps is the segment it manages. glibc's malloc uses it as the main ingredient. The idea of E3 is to incrementally malloc, starting from 1KB and increasing up to 64MB. Each time we allocate a block, we read maps back to determine whether the pointer falls in [heap] or in the anonymous mapping. The logic for determining is as follows. All allocated blocks are kept in hand throughout the process without freeing them, to avoid confusing glibc's dynamic threshold:

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

We paste the output near 128KiB (we keep the pointer column, and delete the earlier lines from 1KB to 64KB):

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

We can see clearly that 131049 is still within the range of [heap], and 131050 moves into the anonymous area, and the threshold seems to be exactly between the two. We don't rush to copy this number into our notes, because it can't withstand a different question. On the day I wrote this, I did two more tests, which are not included in the archive, and the compilation parameters are the same as E3. The first group of tests had each size start a new process, and only do one malloc allocation. The results were not stable: I once ran 131049 directly into mmap, and after rerunning twenty times, it all returned to [heap]. The remaining free space at the top of the new process's heap varies with the size of the binary's .bss and the initial allocation, and the landing point of a single allocation is naturally drifting. The group that really gives a definite answer is the second one, where we replicate the sequence from E3's start exactly, and in the strace lens, only brk and mmap calls are left. It captured the calls caused by malloc as follows (the startup library mapping lines have been deleted):

```text
brk(0x6494a20c6000)                     = 0x6494a20c6000
brk(0x6494a20e8000)                     = 0x6494a20e8000
mmap(NULL, 135168, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7e9c64891000
mmap(NULL, 135168, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0x7e9c64870000
```

Both brk calls are for requests before 131050, which push the top of the heap up. The following two mmap calls start from 131050. But where did the 131049 line go? There are no calls before or after it, and the pointer is exactly in [heap]. When we put the two pieces of evidence together, the truth of the matter is divided into two layers.

The upper layer is simple: **malloc is a library, not a system call**. When the request reaches glibc, it looks for a place in its own heap. If there is still some free space at the top of the heap, it cuts a piece for you on the spot, without even disturbing the kernel. In the first round, the 131048 request expanded the top of the heap by 256KB, and after cutting, there was still a large chunk left at the top. When it came to 131049, the free space was just enough, and it quietly moved into [heap], and the gap in strace was exactly it.

The lower layer only appears when the top of the heap is not enough. We follow glibc through the two paths in front of it. glibc will convert the request into an internal **chunk size**, the formula is nb = roundup( request + 8, 16), 8 bytes is the size of the chunk header, and 16 is the chunk's alignment. The criterion is that when nb is greater than or equal to the mmap threshold, it will go to mmap. The default threshold is 131072 bytes, which is exactly the 128KiB in the textbook. If it is not met, brk will expand the heap by a section, and the amount of expansion is with some free space, and the name in mallopt(3) is M_TOP_PAD, the default value is also 128KiB. The original text of mallopt(3) writes both conditions: the original text says that the allocation amount is greater than or equal to M_MMAP_THRESHOLD, and the free list is still not enough. When both conditions are met, malloc will switch to the path of mmap(2).

Using the new parameters to go back and check, the popular saying has its own deviation. The textbook says "requests greater than or equal to 128KiB will go to mmap", but the comparison is wrong: it compares the chunk size, not the number of bytes requested. 131049 is 23 bytes smaller than 128KiB, but the converted chunk is already 131072, which is enough to reach the threshold. I did get mmap when I ran it alone that day, but we just saw that after running it twenty times, all of them returned to [heap]. Whether the single run lands in mmap depends on how much free space is left at the top of the heap at that moment. The man page of malloc(3) says "larger than MMAP_THRESHOLD", the boundary is half a level off, and mallopt(3) says greater than or equal, which is consistent with the behavior of the binary. And the 131050 in the experiment is not a constant. How much free space was expanded at the top of the heap in the previous run determines whether the next request lands in [heap] or the anonymous area. The "threshold" you read from maps is drifting by one level, and you will fall into a trap if you treat it as a physical constant.

> There is also a detail in the table that is easy to misread: the "mapped location" of the 131050 line shows 152KB, but its chunk is only 131072 bytes. The actual mmap given by the kernel is rounded up to 132KB in pages. The extra 20KB is from two small anonymous mmap (12KiB and 8KiB) at the start, which are adjacent to this mapping and are merged into the same **VMA** by the kernel. Mappings with the same permissions and adjacent addresses are merged by the kernel (VMA stands for virtual memory area, and its Chinese name is virtual memory area, which we saw in L02). Therefore, the number of lines in maps is not equal to the number of mmap calls. When we count the number of allocations by the number of lines, we need to include such merges.

The latter half of E3 looks at whether the address space returns after free. We lay out three observations: freeing a 64MB mmap block makes the mapping disappear immediately, and munmap returns the address space immediately. 256 small blocks of 48KB feed [heap] from 1128KB to 13504KB, and after all are freed, it shrinks back to 1260KB, **brk also shrinks**, but it leaves a margin of M_TOP_PAD. The address above the top of the heap is returned to the kernel as it shrinks, but the written pages remaining in [heap] are not returned to the kernel by free. The line printed by the program in the archive, "pages that have been written are no longer returned to the kernel," refers to this latter part. The most interesting observation is the third one: freeing a 4MB mmap block, and then the malloc(3MB) pointer returns to [heap] (the top of the heap is exactly raised by 0x300000), while malloc(5MB) still goes to mmap.

This is the **dynamic mmap threshold** of glibc: freeing an mmap block larger than the current threshold raises the threshold to the size of that block, and all subsequent requests smaller than it will go to brk. The upper limit on 64-bit platforms is 4MiB × sizeof(long), which is 32MiB. We can easily guess its motivation: when an mmap block is freed, the entire segment is returned to the kernel, and if another block of the same size is allocated, it will have to go through a round of page faults again. brk can reuse the memory on the side, so it doesn't need to go through this trouble. Please also note another boundary: if you explicitly call mallopt(M_MMAP_THRESHOLD, ...), the dynamic adjustment is permanently closed, so E3 does not touch mallopt at all, preserving the original state of this switch.

Finally, we look at the stack. We use alloca to allocate 4KB each round, and after 200 rounds, the buffer address keeps going down. 12000 frames of deep recursion raise [stack] from 136KB to 3316KB, and the 3180KB descent is all recorded on the low address end, but not a single byte has moved on the high address end. The proverb "stack grows downward" translates to [stack] starting to decrease, with the end unchanged. The growth has boundaries: ulimit -s reports RLIMIT_STACK as 8192KB, and the main thread stack on our machine can grow up to 8MB. When it hits the limit, it will trigger a SIGSEGV. In E4, you will also see smaps adding an extra 'gd' (growsdown) to the VmFlags of [stack], which is exactly what the kernel is saying.

## E4: smaps: Twenty-Plus Counters for Each Segment

maps only tells us "which segments are there", and /proc/self/smaps expands twenty or so counter fields after the block header of each segment, with the unit in kB. The program in E4 makes the state available for observation: 64×32KB of dirty writes on the heap, 256KB of touch on the stack, and then pastes the original smaps for four representative segments. The original text of the libc executable segment is as follows (the entire block has 24 lines, and the fields have been deleted):

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

We put the key fields of the four representative segments together (unit is kB):

| Segment | Size | Rss | Pss | Shared_Clean | Private_Dirty |
|---|---|---|---|---|---|
| This program's .text | 24 | 24 | 24 | 0 | 24 |
| [heap] | 2260 | 2076 | 2076 | 0 | 2076 |
| [stack] | 276 | 276 | 276 | 0 | 276 |
| libc .text | 1516 | 1004 | 7 | 1004 | 0 |

We go through each field one by one. **Size** is the total virtual size of the segment, including pages that have never been touched. **Rss** stands for Resident Set Size, its Chinese name is resident set, which is the part that is currently in physical memory. The Size of [heap] is 2260kB, and the Rss is 2076kB, the difference of 184kB is the pages that have not been touched yet, and the lazy allocation mentioned in L02 refers to this difference. **Pss** stands for Proportional Set Size, its Chinese name is proportional resident set, which is the allocated version of Rss: shared pages are allocated according to the number of processes, and each process records its fraction. The 1MB of code in libc's .text is shared by about 140 processes on the entire machine, Rss records the full 1004kB on our head, and Pss is only 7kB after allocation. If we want to know "the actual memory usage of a single process", the honest algorithm is to add up the Pss of each segment, and the kernel even does this addition for you in smaps_rollup. The debt left at the end of E3 is also settled here: the last line of [stack] is VmFlags rd wr mr mw me gd ac, the extra 'gd' (growsdown) refers to the stack growing downward.

The division between Shared and Private is based on whether the page is shared with other processes. The division between Clean and Dirty is based on two places: for private pages, it's whether they have been written, and for file pages, it's whether the page cache has been written back. [heap] and [stack] are anonymous private segments, and the pages that have been written are all in Private_Dirty, so Rss = Pss = Private_Dirty is the easiest to recognize.

What really made me pause was the program's own .text: its identity is a file mapping, and none of us have written to it, but smaps reports it as Private_Dirty 24kB. Following up, we found the answer in the page cache: smaps's clean/dirty for file mappings looks at the **page cache's write-back status**. Our binary was just compiled, and the pages that make up its .text are still in the page cache and have not been written to the disk, and when mapped in, they come with a full set of dirty tags. The verification method is also simple: the old friend from [L03](../file-io/03-page-cache.md), sync, comes on stage. We run a sync and wait for 8 seconds, and rerun the same program, and the same line becomes Private_Clean 24kB, and Private_Dirty is zero. The page cache article we focused on was on data files, and this time it's the program itself, using the same write-back mechanism.

The THPeligible=0 at the end of the table also needs to be mentioned: THP stands for transparent huge page, its Chinese name is transparent huge page, which is the mechanism for automatically aggregating 2MiB huge pages. The mode on this machine is madvise, and only segments that explicitly pass MADV_HUGEPAGE are eligible, and our segments have not passed the message, so they are all 0, and KernelPageSize=MMUPageSize=4 kB also refers to the same meaning. We will leave the topic of huge pages for the article on alignment allocation and huge pages.

::: warning pmap's Dirty column and smaps's Private_Dirty are not from the same source
`pmap -x` is a commonly used tool that rearranges this data. Its Kbytes and RSS columns are from the same source as smaps's Size and Rss. However, its Dirty column uses pmap's own criteria, calculated from /proc/pid/pagemap, and it does not have to be equal to smaps's Private_Dirty line by line. Using both tables together is fine, but if you insist on matching them line by line, it is normal for them to be mismatched. In addition, the pmap in E4 is spawned by the program during runtime, and the photo is taken a few milliseconds after smaps, and the RSS of the heap line has increased from 2076 to 2164, so the time difference also needs to be considered when comparing.
:::

## E5: ASLR: Five Runs All Change, setarch -R Five Runs All the Same

Up to now, we have looked at the structure within the same run. However, as long as you run the program twice, the entire map's address will change completely, and this is done by **ASLR**. The program in E5 prints one line of key base addresses each time it runs:

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

We run it five times in normal mode, and then run it five more times with `setarch -R` to turn off the randomization mode (ten lines are all in the archive, we paste a few lines):

```text
== ASLR 开 · 第 1 次 ==
exe=600afa76f000 heap=600afe5f4000 stack=7ffcc36cc000-7ffcc36ee000 libc=707580200000 vvar=707580a02000 vdso=707580a08000
== ASLR 开 · 第 3 次 ==
exe=621b9561e000 heap=621bb7e13000 stack=7ffc575ab000-7ffc575cd000 libc=7fe7d6800000 vvar=7fe7d7053000 vdso=7fe7d7059000
== setarch -R(ASLR 关)· 第 1 次 ==
exe=555555554000 heap=55555555b000 stack=7ffffffdd000-7ffffffff000 libc=7ffff7800000 vvar=7ffff7fb6000 vdso=7ffff7fbc000
(关掉的五次输出逐字节相同,另四次删节)
```

The span of the five normal runs is not small: the base address of the executable spans about 13.9 TiB, and the libc along with vvar, vdso spans about 15.5 TiB (they go with the mmap area), and the theoretical ceiling is 2^32 page positions multiplied by 4KiB, which is exactly 16 TiB. The starting point of the stack also spans 7.09 GiB, and the ceiling is 2^22 page positions multiplied by 4KiB, which is 16 GiB. These bits are the randomization levels on the x86_64 side of the kernel, and five samples naturally cannot cover the entire range, but the scale has already revealed the levels. [heap] follows the executable, and the random amount is not small, and these five times range from 62MB to 1GB. We read /proc/sys/kernel/randomize_va_space, and the value we get is 2, which corresponds to full randomization.

The next five lines are the world of setarch -R. setarch(8) sets the personality(2) to ADDR_NO_RANDOMIZE with the -R option. The output of the five runs is **exactly the same byte by byte**: the executable returns to the default base address of PIE (position independent executable) at 0x555555554000, the stack top is fixed at 7ffffffff000, libc is also fixed at 7ffff7800000, and [heap] is tightly attached to the program's image, with only 0x7000 between them. This set of addresses is probably familiar to you: gdb defaults to turning off randomization for you, and the neatly arranged addresses in the tutorial and debugging records are all taken from the same world. In engineering, it also has a legitimate use: reproducing a bug that depends on the address layout, or wanting to make the maps of two runs comparable by line, wrapping it with `setarch -R` becomes possible. ASLR blocks the path of "guessing the address", and the structure of the segments has never changed once, only the landing points have changed.

## What the Other Side Sees

Windows does not have a text file that lays out the entire address space. If you want to look at it segment by segment, we have to use VirtualQuery to ask one segment at a time from the low address upwards. Each time we ask, we get a MEMORY_BASIC_INFORMATION (which contains the status, type, protection attributes, and size), and we loop ourselves to piece together the entire table. The graphical VMMap and minidump tools also do the same thing. The ownership of the heap has also changed: brk is not there, HeapAlloc uses the NT heap, and in the new configuration, there is also an option for segment heap. However, the two-stage reservation and commitment (VirtualAlloc's MEM_RESERVE and MEM_COMMIT) is the same idea as the "mmap only registers, and page faults give pages" we saw in L02, which is two implementations of the same idea. After the Windows side's memory article is written, we will add the link to the VirtualQuery article here.

In this article, we have fully recognized the 41 segments of a normal C++ program. The next three articles will all use it as a base: the virtual memory API article will use mprotect to change the permissions of these segments, inserting a PROT_NONE guard page in the middle; the shared memory article will make two processes' address spaces actually share the same segment; and the alignment allocation and large page article will go back to explain the origin of the 0 in THPeligible.

<ReferenceCard title="Reference Resources">
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
