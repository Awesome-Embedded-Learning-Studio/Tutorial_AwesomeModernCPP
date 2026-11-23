---
title: "Aligned Allocation and Huge Pages: 32-Byte Alignment, 2 MiB Pages, and a Rejected 512 GiB"
description: "The closing article of the Linux side of the memory management chapter: after the allocator hands over an address, two matters remain — alignment and huge pages. In live tests, the three aligned-allocation tools aligned_alloc/posix_memalign/aligned new met the bar in every sample at align=64/4096, yet their error reporting splits three ways (NULL+errno, EINVAL as the return value, bad_alloc as an exception; glibc 2.44 accepts non-multiple sizes under C17's relaxed rule). Two pieces of physical evidence for alignment: an unaligned address fed to _mm256_load_si256 segfaults immediately with si_code=SI_KERNEL and a meaningless si_addr — a #GP, not a page fault — with the vmovdqa versus vmovdqu disassembly archived; and two counters on one cache line run 529.0 ms against 74.4 ms, a 7.11x gap. The THP section is the centerpiece: out of the box, MADV_HUGEPAGE yields only AnonHugePages=0, and following the diagnostic chain we found WSL2's init setting MMF_DISABLE_THP on the whole process tree — a third, process-level switch that overrides sysfs; once cleared via prctl, a full 1 GiB goes huge, first-touch writes speed up 5.43x while a full memset leaves only 1.02x. Explicit huge pages via MAP_HUGETLB return ENOMEM in all three settings — the pool is empty and reservation requires root. The overcommit number chain: 512/256/128 GiB all refused, 64 GiB granted; under the heuristic policy this machine's measured refusal line sits near 69 GiB of RAM plus swap, and MAP_NORESERVE lets the same 512 GiB succeed instantly with VmSize maxed out and VmRSS unmoved — plus a measurement lesson where gcc -O2 deleted a null-check-only malloc/free pair and nearly recorded a false 256 GiB success. The final OOM Killer section closes the Linux side with read-only observation plus a touch-60% safe demonstration: the relative semantics of the oom_score baseline of 666, the EACCES boundary where oom_score_adj permits self-harm but not self-protection, and zero OOM records in a full dmesg scan."
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 17
prerequisites:
  - "mmap 内存映射:把文件贴进地址空间"
  - "错误处理范式:从 errno 到 expected"
related:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 内存管理
  - 优化
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/memory/04-align-hugepage.md
  source_hash: fdb23b593e7018e8a03310d6724ff5234f4c263728d3773489bee943f77bab0f
  translated_at: '2026-11-22T15:35:00+00:00'
  engine: glm-5.3
  token_count: 19551
---

# Aligned Allocation and Huge Pages: 32-Byte Alignment, 2 MiB Pages, and a Rejected 512 GiB

On the Linux side of the memory management chapter, we have already walked through the complete maps overview in [the memory layout article](./01-memory-layout.md), `mprotect` and `madvise` in [the virtual memory API article](./02-vm-apis.md), and cross-process mapping in [the shared memory article](./03-shm.md). The malloc watershed was measured back in the layout article: small blocks go through `brk`, large blocks through `mmap`. But after the allocator hands us an address, two things remain that it does not decide for us. The first is the address itself: does it land on a multiple of 16, or of 64, or of 4096? That is **alignment**. The second is the page size: virtual memory is managed in pages, and while we habitually say 4 KiB per page, the x86-64 page tables also recognize 2 MiB and 1 GiB — those two sizes are the so-called **huge pages**.

The benefits of both grow out of the hardware. Alignment serves instructions and caches: part of the SIMD instruction set is picky about addresses, and if you have not aligned the address, those instructions simply refuse to run on the spot; the cache line is a 64-byte unit, and when two threads crowd onto the same line, the performance loss can reach several times. Huge pages serve the page tables and the TLB (the hardware block that caches address-translation results, often called the translation cache in Chinese): as pages get bigger, each entry governs more territory, and first-touch page faults get fewer. It all sounds lovely, but we do not assess their worth on paper — we measure everything with experiments. After these two topics, we follow the huge-page thread one level deeper: when malloc succeeds, has physical memory actually been set aside for us? The answer to that question leads to overcommit (a policy that permits commitments beyond physical memory) and the OOM Killer (the mechanism by which the kernel kills processes when memory runs dry). They occupy the last two sections of this article, and the memory journey ends right there — a fitting close for the Linux side.

We arranged the work as six experiments, E1 through E6, corresponding one-to-one with the six directories 01 through 06 under `code/volumn_codes/vol8/systems-programming/linux/memory/04-align-hugepage/` in the repository; the code and all raw output are archived there, so you can cross-check at any time. As usual, the environment is stated up front, because every number that follows must be read against it: the desktop runs an AMD Ryzen 7 9700X under WSL2, the kernel is a 6.18.33.2-microsoft-standard-WSL2 build, g++ is 16.2.1, glibc is 2.44, and everything is compiled with `-std=c++20 -O2 -Wall -Wextra -Wpedantic`, producing zero warnings. MemTotal is about 53 GiB, plus 16 GiB of swap. Three system settings directly shape every experiment that follows: the THP sysfs switch sits on `[madvise]`, the explicit huge-page reservation pool is zero, and overcommit_memory is 0. One privilege boundary also has to be stated honestly: my sudo requires a password, so writes to sysfs and sysctl are simply out of reach. The experiments in this article are therefore designed around what an unprivileged user can actually do, and where something cannot be done, we say so plainly. The three shared utilities `unique_fd`, `sys_call`, and `errno_code` carry over from [the RAII article](../../thinking/01-raii-paradigm.md) and [the error handling article](../../thinking/02-error-paradigm.md); the experiments here are mostly small programs, and we print failure codes directly instead of wrapping them in yet another layer.

## E1 The Three Aligned-Allocation Tools: One Requirement, Three Error Paths

We start with the interfaces. The C side provides two, and C++17 adds a third:

```c
void *aligned_alloc(size_t alignment, size_t size);
int   posix_memalign(void **memptr, size_t alignment, size_t size);
```

```cpp
void* operator new(std::size_t size, std::align_val_t alignment);
```

All three signatures take an alignment parameter, and legal values share one requirement: a **power of two** — 1, 2, 4, 8, 16, 32, 64, 4096, doubling all the way up. Values like 48 or 3 do not qualify. posix_memalign adds one more rule: alignment must also be a multiple of sizeof(void*), which on x86-64 means a multiple of 8, so passing 4 to it is likewise illegal. These requirements are written in man 3 aligned_alloc and man 3 posix_memalign, and following the documentation we deliberately scheduled the illegal values into the experiment as well, to see how each tool reports its errors.

The E1 sampling is spread evenly: alignments of 64 and 4096, sizes of 64, 100, 4096, and 10000 — where 100 and 10000 are deliberately not multiples of 64 — and each of the three tools takes a turn. In every sample, `addr % align` of the returned address was zero. The full output is a bit long, so here is an excerpt:

```text
==== align = 4096 ====
aligned_alloc(4096,    64)         -> 0x5c328280b000  %4096 = 0  [OK]
posix_memalign(&p, 4096,    64)    -> rc=0(0=success)
  address                          -> 0x5c328280b000  %4096 = 0  [OK]
operator new(   64, align_val_t(4096)) -> 0x5c328280b000  %4096 = 0  [OK]
```

That the addresses themselves qualify is no surprise; what really deserves our attention is the failure side. Feeding each tool an illegal alignment in turn, what they hand back looks like this:

```text
aligned_alloc(48, 128)    -> (nil) errno=22(Invalid argument)
posix_memalign(&p,48,128) -> rc=22(Invalid argument) p=0x1
operator new(8, align_val_t{3}) -> 抛 std::bad_alloc: std::bad_alloc
```

Look at that: the very same EINVAL (22) takes three completely different paths. aligned_alloc returns NULL and files the details in errno, which we can look up by boxing it with the `errno_code()` from [the error handling article](../../thinking/02-error-paradigm.md). posix_memalign never touches errno — the RETURN VALUE section of man 3 puts it bluntly: the error code comes back directly as the return value, and errno is not set. The third tool has yet another policy: when aligned new fails, the C++ convention is to throw std::bad_alloc, and the exception object carries nothing beyond the string `std::bad_alloc`; the EINVAL detail is nowhere to be found. The error handling article covered two error-reporting systems — one through the errno slot, one through exceptions. Error codes returned directly as values are another branch of the POSIX tradition, and we run into it live for the first time today; the three tools here line the three paths up in a row, supplying a scene for that article. One more detail worth savoring: the pointer we passed to posix_memalign had been deliberately pre-filled with `(void*)0x1`, and after the failure it read back as 0x1 — on failure the function does not touch the pointer you passed, a point written into the specification as of POSIX.1-2008 TC2.

Illegal alignment values are one category; size not being a multiple of alignment is another, which the old C standard also classified as undefined behavior. C17 removed that requirement, and the NOTES of man 3 aligned_alloc records the story: defect report N2072 judged the requirement that size be a multiple of alignment unnecessary, and a technical corrigendum removed it. glibc caught up in 2.38 — the HISTORY section of the man page states that from that version on it implements the C17 policy. So in our local tests on 2.44:

```text
aligned_alloc(64, 100)    -> 0x5c328280c000 errno=0 (size 非倍数,C17 放宽后 glibc 接受)
```

100 is not a multiple of 64, yet the call still succeeded, and errno stayed clean. On older glibc or another libc, the implementation is entitled to refuse; when porting, just keep an eye on the libc version.

There are two more automatic paths on the C++ side, and we verified both. One is a type carrying its own `alignas(64)`: when you new it, the compiler automatically routes to the aligned versions of operator new and operator delete — the old trouble with over-aligned types that C++17 fixed — and the measured address came out %64 equal to zero. The other involves std::pmr: `new_delete_resource()->allocate(1000, 4096)` also returned an address on a multiple of 4096; when the alignment requirement exceeds the default new's 16, it forwards to the aligned new. In short, once you write alignment into the type or the interface parameters, every link in the chain hooks itself up — no need to specify alignment manually, link by link.

## E2 Why Alignment Matters: A Load That Segfaults

If we are to specify alignment values explicitly, someone had better genuinely care about them. The first to care is the SIMD instruction set. SIMD means single instruction, multiple data; on x86 the pioneer of this line was SSE, with instructions that eat 128 bits, and AVX — short for Advanced Vector Extensions — later doubled the width to 256 bits. The object we deal with in the intrinsic header `<immintrin.h>` is `__m256i`, a 256-bit, 32-byte integer carrier. The functions that load it are two brothers differing by a single letter u: the version without the u requires the address to be 32-byte aligned, while the unaligned version with the u accepts anything. We wrote a probe in three steps: feed an aligned address to the aligned version, feed an arbitrary address to the unaligned version, and finally feed a guaranteed-misaligned address to the aligned version. The misaligned address is produced the honest way: start from a 32-aligned block of memory and shift the address 8 bytes to the right, landing on %32 of exactly 8.

```text
[1] alignas(32) 静态数组   addr=0x62068e8b5080  %32=0
    _mm256_load_si256 成功(vmovdqa 吃下对齐地址)
[2] 对齐块偏移 8 字节       addr=0x6206c0ed8068  %32=8
    _mm256_loadu_si256 成功(vmovdqu 对任何地址都行)
[3] 未对齐地址 + _mm256_load_si256(vmovdqa)—— 预期崩:
  ==> SIGSEGV 捕获! si_code=128(SI_KERNEL) si_addr=(nil)
```

Well, it really did crash. But look closely at the report it produced — the closer you look, the stranger it gets: si_addr reads nil. The segfaults we met in [L02](../file-io/02-mmap-memory-mapping.md), whether from accidentally writing a read-only page or from touching an unmapped address, carried si_code of SEGV_ACCERR or SEGV_MAPERR, and si_addr always pointed honestly at the real faulting address. The byte-level precision of si_addr was measured in the guard-page experiment of [the virtual memory API article](./02-vm-apis.md): whichever byte you wrote to is the byte it reports. This time the reading is different: si_code is 128, SI_KERNEL, and si_addr is altogether meaningless. The reason sits at the hardware level: the aligned load compiles to `vmovdqa`, whose instruction encoding hard-codes the alignment requirement. When the address does not qualify, what the CPU raises is a #GP (general protection exception), and it never gets anywhere near the page-fault step. The kernel translates #GP into SIGSEGV for us too, but it cannot translate a sensible faulting address out of it, so all it can return is SI_KERNEL plus a meaningless si_addr. So when you see a segfault with si_code=SI_KERNEL in your logs, the first suspect is an instruction-level violation — of which misalignment is the most common kind; do not go hunting through the page tables as if it were a page fault.

Hearing me describe vmovdqa secondhand does not count; we must inspect the assembly with our own eyes. Both function bodies are in the archived e2_disasm.txt; here is just the first line of each:

```text
0000000000001370 <_ZL12load_alignedPKDv4_x>:
    1370:  c5 fd 6f 07    vmovdqa (%rdi),%ymm0

0000000000001330 <_ZL14load_unalignedPKv>:
    1330:  c5 fe 6f 07    vmovdqu (%rdi),%ymm0
```

Between the encodings c5 fd 6f and c5 fe 6f, only the middle byte differs, and inside that byte just two bits — yet what they deliver are vmovdqa versus vmovdqu, the a taking the first letter of aligned and the u the first letter of unaligned. You will not find the alignment requirement in a comment, nor in any convention; it is written into the instruction encoding itself, and the CPU obeys on receipt. A word on the experiment's defensive moves as well: the load functions carry the `target("avx2")` attribute, otherwise the compiler would not emit any AVX instructions at all, and they consume all four 64-bit lanes — otherwise the load could be narrowed to the 128-bit XMM version, the alignment requirement would drop to 16 bytes, and the evidence would no longer be pure.

### Two Counters on One Cache Line: 7.11x

The second party that cares about alignment is the cache line. Modern CPU caches move data in **cache-line** units, 64 bytes per line on x86-64. In a two-thread scenario, if two variables that are each private sit crammed on the same line, the cache coherence protocol must shuttle that line back and forth between the two cores. The program shares nothing at the language level, yet performance drops several-fold — that is **false sharing**. The mechanism was covered in full in [CPU Caches and Threads](../../../../vol5-concurrency/chapter01-start-concurrency/09-what-is-a-thread.md) in vol5; we will not re-explain MESI or RFO here, only supply the local numbers. In the ring queue of [the shared memory article](./03-shm.md), the arrangement that gives head and tail one cache line each guards against exactly this.

In the experiment we start two threads, each performing 400 million volatile increments on its own private counter. In the near variant, the two `uint64_t` lie adjacent in BSS, separated by 8 bytes — same line; the far variant uses an `alignas(64)` struct to pin each counter onto its own line. Three interleaved rounds, median taken:

```text
中位数:near(同 line)=529.0 ms   far(隔开)=74.4 ms   倍差=7.11x
```

A seven-fold gap. Your code has no data race, and neither variable ever crosses the other's border — all the slowness is the hardware doing the hauling. The fix you have already seen: the far variant writes `alignas(64)` into the type; the counters in this experiment are static variables, so placing the alignment falls to the linker. Dynamically allocated objects fall to aligned new, which E1 verified — C++17 automatically places every instance on a multiple of 64, and again the change is a single line, for a very real 7x payoff. By this point alignment has two concrete credentials: one shown by vmovdqa crashing on you, the other by the cache line slowing down on you.

## E3 Transparent Huge Pages: madvise Works, Yet Zero Huge Pages

That closes the alignment business, and we turn our gaze to the page size. The ordinary page on x86-64 is 4 KiB, and the page tables additionally support 2 MiB and 1 GiB huge pages. The benefit can be stated plainly in fault counts: to touch the same 1 GiB, 4 KiB pages take 262144 faults while 2 MiB pages take only 512, and the page tables' own overhead drops by an order of magnitude as well. There are two routes to huge pages. One is explicit hugetlb: the kernel keeps a reservation pool, and the program claims whole pages from it with MAP_HUGETLB — that is E4's business. The other is this section's THP (Transparent Huge Pages): the kernel automatically merges qualifying 4 KiB pages into 2 MiB huge pages behind the scenes; to the program it is transparent, which is exactly what the transparent in the name means.

The master switch for THP lives in sysfs: `/sys/kernel/mm/transparent_hugepage/enabled` offers three settings. always means automatic globally, never means off globally, and madvise means per the program's own request. The request is made with none other than madvise, the same interface we explored in [the virtual memory API article](./02-vm-apis.md). Calling `madvise(p, len, MADV_HUGEPAGE)` hangs the intent-to-use-huge-pages on a mapping. Our local sysfs sits on `[madvise]`, so by the textbook account, once we ask, the kernel should deliver. The E3 benchmark e3_thp was written to exactly that expectation: mmap 1 GiB of anonymous memory, attach the MADV_HUGEPAGE intent, write one byte every 4096 bytes to walk the whole region, and then read AnonHugePages from smaps. The output in the factory state looks like this:

```text
HUGEPAGE    A 首触写 1 GiB(每页 1 字节):   639.5 ms  AnonHugePages=0 kB
    Size:            1048576 kB
    AnonHugePages:         0 kB
    THPeligible:           0
    VmFlags: rd wr mr mw me ac sd hg
  ==> 提示了 HUGEPAGE,大页却是 0:进程级开关压过了 sysfs(详见 e3_thp_probe)
```

Huh? Not a single line of ours is wrong, the return value of madvise is zero, and `hg` is lit up in VmFlags — that flag means our request has been recorded in this VMA's flags. Yet what the kernel reports is AnonHugePages of 0 and THPeligible of 0: not so much as a single 2 MiB page. The simultaneous appearance of hg and zero is the most valuable scene in this article.

We did not guess — the archived diagnostic probe e3_thp_probe exists precisely for this, in division of labor with the timing benchmark e3_thp, and we worked through the eliminations with it, step by step. Step one, check sysfs: the global setting is `[madvise]`, the 2048kB class chose `[inherit]` to follow the global, and shmem is set to never — all normal. Step two, check whether the madvise reached the kernel: hg is in VmFlags, it did. Step three, check whether the kernel tried: for the thp_fault_alloc and thp_fault_fallback counters in /proc/vmstat, the probe's baseline at startup showed zero increments, and after the factory-state round the program's delta section produced nothing until the switch was cleared. The zero-attempts claim is established by the later control: after clearing the switch and rerunning the same operation, the counters grew by exactly that round's own share, while the factory-state round never contributed once. Then one more cut: we issued MADV_COLLAPSE over the same region — the synchronous collapse interface the kernel has provided since 6.1, which can knead resident 4 KiB pages into 2 MiB — and it returned EINVAL. At this point in the diagnosis, only the process itself remains under suspicion.

## The Third Layer of THP Switches: WSL2 Pins It on the Process

The probe's last line is the answer; let us look at it together:

```text
prctl(PR_GET_THP_DISABLE) = 1   <-- WSL2 的 init 给整个进程树设了它
```

`PR_GET_THP_DISABLE` reads a flag bit on the process named MMF_DISABLE_THP; once set, this process is done with THP for good. man 2 PR_SET_THP_DISABLE describes it in no uncertain terms: once set, THP is completely disabled for the process, and neither the global switch nor MADV_COLLAPSE can bring it back. The flag has existed since Linux 3.15, intended as a manual off-handle for job programs whose source cannot be changed. And the flag on us was inherited: it rode all the way down through fork and exec. Before systemd, WSL2 had Microsoft's own init chain, which set it on the entire process tree — our experiment programs were born wearing it. What sysfs says, `[madvise]`, is the kernel's global policy; the layer of switch sitting on the process is invisible to it and beyond its reach, so the hg flag is still dutifully recorded while the actual huge-page allocation is still waived. Textbooks give THP two layers, sysfs one and madvise one; WSL2 slipped a third underneath.

Is there any rescue for us, then? The man page says passing zero to prctl(PR_SET_THP_DISABLE) means clearing the flag. We tried it: the local 6.18 kernel allows an unprivileged process to clear it on itself. With that done, we ran the same probe again:

```text
② prctl(PR_SET_THP_DISABLE, 0) 后:PR_GET=0
    mmap+MADV_HUGEPAGE+首触(0x7a126c800000):
    [复活] AnonHugePages:     32768 kB
    [复活] THPeligible:           1
---- ② 之后(相对本进程启动的增量) ----
    thp_fault_alloc                +16
```

Over the 32 MiB region, AnonHugePages jumped to 32768 kB and the thp_fault_alloc counter grew by 16 — exactly 16 × 2 MiB. MADV_COLLAPSE came back to life too: over a region carrying no madvise at all, purely first-touched at 4 KiB, one COLLAPSE brought rc to zero, AnonHugePages rose from 0 to 32768 kB, and the thp_collapse_alloc counter also grew by 16. With the positive and negative controls captured in one pass, the root cause is confirmed. If you run on bare-metal Linux, no init sets the flag on you, and THP obeys only the two layers of sysfs and madvise. If instead you run THP experiments inside WSL2 and get a string of zeros, check the prctl reading before rushing to suspect your own code. One caveat must remain on record: clearing the flag relies on this kernel's permission. If Microsoft tightens it one day, we cannot stop them — let your own machine's measurements be the judge.

With the switch cleared and the huge pages actually landing as 512 × 2 MiB, the performance difference should now come to trial. The post-clearing comparison was again done in the benchmark e3_thp, first-touching the same 1 GiB; it ran three rounds in total, and the median is the middle round:

```text
中位数(3 轮):
  A 首触  HUGEPAGE=55.4 ms  NOHUGEPAGE=300.5 ms  倍差=5.43x
  B memset HUGEPAGE=39.7 ms  NOHUGEPAGE=40.7 ms  倍差=1.02x
```

The first-touch round reaped 5.43x: faults fell from 262144 to 512, AnonHugePages in smaps read a full 1048576 kB, the entire 1 GiB went huge — a benefit won clean and neat. The memset round, though, got only 1.02x, essentially a tie inside the noise. Both rows of numbers are laid out above, and the reason deserves equal clarity: memset is a pure streaming write over data that is already resident. Every byte written asks the page tables to translate an address, and the translation results are cached in the TLB; 4 KiB pages do cost more TLB entries than 2 MiB pages, but the bulk of streaming-write bandwidth is spent in memory itself, and the TLB's small advantage vanishes once amortized into it. Huge-page gains concentrate in fault counts and translation-cache density; expecting them to speed up an already-warm streaming write is unrealistic. Now and then the internet sells THP as a cure-all — these two rows of numbers are a ready-made counterexample.

## E4 Explicit Huge Pages: An Empty Reservation Pool

The other route is explicit huge pages, whose mechanism is thoroughly non-transparent: the administrator pre-reserves a pool of 2 MiB huge pages in the kernel, and the program claims whole pages from the pool by mmap'ing with MAP_HUGETLB. Which page size to claim can be stated with MAP_HUGE_2MB or MAP_HUGE_1GB; unstated, the default page size applies, and the Hugepagesize field of /proc/meminfo tells you what that default is. The state of our local pool is the first thing the E4 program reads when it runs:

```text
nr_hugepages          = 0
    HugePages_Total:       0
    Hugepagesize:       2048 kB
```

The pool is empty. We still tried all three settings, plus a control group without HUGETLB:

```text
MAP_HUGETLB 尝试(池为空,预期 ENOMEM):
MAP_HUGETLB|MAP_HUGE_2MB, 2 MiB        -> MAP_FAILED  errno=12 (Cannot allocate memory)
MAP_HUGETLB|MAP_HUGE_1GB, 1 GiB        -> MAP_FAILED  errno=12 (Cannot allocate memory)
MAP_HUGETLB(默认页大小), 2 MiB    -> MAP_FAILED  errno=12 (Cannot allocate memory)

对照组(不带 HUGETLB,普通匿名映射):
普通 MAP_ANONYMOUS, 2 MiB            -> 0x74f0fc800000  成功
```

All three settings came back ENOMEM. There is an easy misconception here: the ERRORS of man 2 mmap does list an EPERM for MAP_HUGETLB, conditioned on the caller lacking CAP_IPC_LOCK, but all three of our live attempts returned ENOMEM. Per the kernel source, that EPERM check hangs on paths like SysV shared memory (SHM_HUGETLB); the anonymous MAP_HUGETLB route we took never passes through it. What the measurements can support is one sentence: the pool is empty, the reservation fell through, and the failure code therefore stopped at ENOMEM. The ways to fill the pool all belong to root: a hugepages=N boot parameter, writing the reserved page count into /proc/sys/vm/nr_hugepages, or mounting a hugetlbfs filesystem. We tried the write as well; open itself was turned away:

```text
open(nr_hugepages, O_WRONLY) -> 失败 errno=13 (Permission denied)  [无 root,如实记录]
```

So the honest statement for this unprivileged machine is: the explicit huge-page route is a dead end, and the only huge pages within reach are E3's THP. The trade-off between the two follows naturally: THP occupies no reserved memory and merges on demand, at the cost of khugepaged (the kernel's background thread that merges ordinary pages into huge ones) working behind the scenes, striking at times you do not decide — it has a temperament of its own. Explicit huge pages, by contrast, are one page managed at a time, with stable latency; the trouble is that opening the pool is the administrator's job. Long-resident heavyweights like databases use exactly this, and E4's ENOMEM is direct evidence of an unreserved pool.

## E5 Overcommit: Commitment and Occupation Are Two Different Things

The talk of huge pages and reservation pools leads us to the last fundamental question of virtual memory: by what right does our malloc of 512 GiB succeed — or fail? Linux's policy on memory commitments is called **overcommit**: the kernel allows virtual memory whose total exceeds physical memory plus swap to be handed out, betting that a program holding an address may never actually use it. The policy knob is `/proc/sys/vm/overcommit_memory`, and man 5 proc_sys_vm lists three settings: 0 is the heuristic setting — also the default — which only blocks obviously absurd single requests; 1 always grants; 2 is the strict setting, capping the system-wide committed total at CommitLimit, computed as `(RAM - huge page reservations) × overcommit_ratio / 100 + swap`. Our local setting is 0, and CommitLimit reads about 42.5 GiB — but that is mode 2's yardstick, which mode 0 does not use.

E5 begins with a descent down a ladder, malloc's amount halving from 512 GiB all the way down:

```text
  malloc(512 GiB) -> (nil) errno=12 (Cannot allocate memory)
  malloc(256 GiB) -> (nil) errno=12(被拒)
  malloc(128 GiB) -> (nil) errno=12(被拒)
  malloc( 64 GiB) -> 0x733e09fff010 errno=0(成功)
```

512, 256, and 128 were all refused; 64 GiB succeeded. Where does the refusal line sit? Our local MemTotal of 55522928 kB plus SwapTotal of 16777216 kB totals about 69 GiB. 128 sits above it and was refused; 64 sits below it and was let through. The heuristic's yardstick is the order of RAM plus swap: once a single request presses past it, what comes back is NULL. The man page gives mode 0 only the one word heuristic, with no fixed formula. The 69 GiB figure is this machine's measured account; run this set of experiments on your own machine, and you will know where your own line sits.

The same 512 GiB, asked for differently, meets an entirely different fate. An mmap carrying MAP_NORESERVE — telling the kernel plainly to make no reservation for the commitment — succeeds immediately. After the success we read /proc/self/status stage by stage, and the chain of numbers is this. The stage numbers in the output jump from 0 straight to 3; it is not that we trimmed away the middle — the program only ever had four stage labels, and the missing 1 and 2 never existed:

```text
  [阶段0 基线]
    VmSize:    7304 kB      VmRSS:    4036 kB
  mmap(512 GiB, MAP_NORESERVE) -> 成功 errno=0
  [阶段3 阶段映射后,一字节没碰:VmSize +512 GiB,RSS 不动]
    VmSize: 536878216 kB    VmRSS:    4044 kB
  [阶段4 只写 1 GiB:VmRSS 涨到 ~1 GiB 量级,承诺的其余 511 GiB 不占物理页]
    VmSize: 536878216 kB    VmRSS: 1052620 kB
  [阶段5 munmap 后:回到基线]
    VmSize:    7304 kB      VmRSS:    4044 kB
```

VmSize grew by 512 GiB while VmRSS did not budge; we wrote 1 GiB, and it grew by exactly 1 GiB; after munmap both returned to baseline. Virtual commitment and physical occupation are accounted separately. This convenience has a price, which we come to in E6.

> Midway through the numbers, I have to insert an account of a stumble of my own. In the first version of the ladder program, each malloc return was merely null-checked and freed, and only success or failure was printed. Its run recorded 256 GiB as a success — far more optimistic than this version's records. Tracing with strace revealed an embarrassing truth: behind those mallocs there were no corresponding system calls at all; we had never asked the kernel for memory. `gcc -O2` judged the malloc/free pair as null-check-only with no observable effect and optimized the pair away entirely — the success the program printed was the compiler's hallucination on our behalf. The fix was to print the pointer itself with %p — the compiler cannot fold away a pointer's value — and to re-verify with strace that every rung actually issued its mmap. When you run allocation-measuring experiments in the future, carry your wariness with you: the optimizer does not only optimize your program; it will also, without a sound, optimize away your experiment.

## E6 OOM Killer: An Approach Within Safe Margins

Commitments have gone out, but physical memory is finite. When physical memory and swap truly bottom out and the kernel has no pages left to reclaim, its last resort is the **OOM Killer** (Out Of Memory Killer, the execution mechanism for when memory is exhausted): the kernel scores every process, picks the highest-scoring one to kill, and seizes its memory back. The score lives in `/proc/pid/oom_score`, ranging from 0 to 1000; the higher a process scores, the earlier its turn to die. In E6 we do not actually trigger it — if the WSL2 machine got taken down, the experiments of the following chapters would be off the table. What we do is read-only observation, plus one occupy-and-release cycle kept within a safe margin.

The read-only part delivers a surprise right at the start:

```text
==== ① 只读观察 ====
  /proc/self/oom_score     = 666 (0-1000,越大越先被杀)
  /proc/self/oom_score_adj = 0 (基线 0)
```

Our tiny process occupies memory on the order of a few megabytes, yet its score hangs at 666. I read through the machine's other processes as well, and the small ones are almost uniformly 666. So the score does not track the absolute memory footprint you might imagine; it works on a relative scale, where only the ordering between processes is meaningful and the absolute value itself says little. Beside the score sits a writable knob, oom_score_adj, ranging from -1000 to +1000, whose effect is added onto the score. We tested its writability live:

```text
write(oom_score_adj, "250") -> 3 成功
write(oom_score_adj, "-1000") -> -1 失败
  负值写入被拒(errno=13 Permission denied)
```

Writing a positive value onto ourselves succeeded within a few bytes — the kernel is at its most generous when we ask to become more killable. Writing a negative value, which moves us toward exemption from death, demands the CAP_SYS_RESOURCE capability, and unprivileged as we are, we received errno 13, EACCES. Savor this asymmetry: willing to harm yourself? Go right ahead. Wanting to protect yourself? Come back with privilege.

For the part that actually touches memory, our dial was set at 60% of MemAvailable: at startup the program reads MemAvailable, callocs sixty percent of it, then writes for real at a cadence of one byte per 4096 bytes so that the kernel genuinely hands over physical pages, and then we free. The three readings:

```text
  [touch 前基线]
    VmRSS:  4208 kB      MemAvailable=46558532 kB   oom_score=666
  [touch 后:RSS 涨到目标量级,MemAvailable 同步下降]
    VmRSS: 27939332 kB   MemAvailable=19415616 kB   oom_score=924
  [free 后:RSS 落回,MemAvailable 回升 —— 占用/回收链条闭合]
    VmRSS:  4212 kB      MemAvailable=46788072 kB   oom_score=666
```

VmRSS rose to about 26.6 GiB and fell back; MemAvailable opened and closed in step; oom_score walked a lap of 666, 924, 666. The occupy-and-release chain closed, and throughout, memory exhaustion stayed far away — the margin held steady the whole time. We also did a full scan of dmesg: across 694 lines of output, zero matches for oom, out of memory, or killed process — no record of the killer showing up, and the experiment ended cleanly. These readings complete the other half of the verification left open by E5: a successful calloc is only a commitment; the touch is what actually occupies physical pages; and after free, the large mmap-backed block returns immediately — the malloc watershed we saw in [the layout article](./01-memory-layout.md) governs exactly this return path on the free side. So what is overcommit's price after all? That nobody guarantees the gap between commitment and occupation: if that 512 GiB mapping really were written full, page by page, then past the bottom line new pages would be denied, and the OOM Killer would have to step in to clean up — and the process it picks may not be the one you think most deserves to die.

## How the Other Side Looks

On the Windows side, the huge-page route shares the temperament of Linux's explicit huge pages and likewise skips the transparent-merging route. VirtualAlloc and CreateFileMappingW recognize MEM_LARGE_PAGES and SEC_LARGE_PAGES; the granularity is given by `GetLargePageMinimum()`, but it demands the SeLockMemoryPrivilege privilege. [The file mapping article](../../windows/file-io/02-file-mapping.md) probed this threshold back in the day: the local user has not been granted this privilege, and `VirtualAlloc(MEM_LARGE_PAGES)` and SEC_LARGE_PAGES both received 1314, the ERROR_PRIVILEGE_NOT_HELD error code, while the ordinary-page control allocation succeeded as usual. Set E4's ENOMEM against this 1314: the two sides block you at different layers, one at an empty pool, one at missing entitlement, yet the outcome is identical — only with the administrator's configuration done in advance do huge pages become usable. The commitment-versus-occupation divide has its own Windows edition too: VirtualAlloc's two-step MEM_RESERVE and MEM_COMMIT keep reservation and commit separate, and the SEC_RESERVE experiment in [the file mapping article](../../windows/file-io/02-file-mapping.md) already walked through it against Linux's overcommit once. The formal Windows-side memory management chapters are already in place — the first one covers exactly VirtualAlloc — and we will unfold the details in [the virtual memory article](../../windows/memory/01-virtualalloc.md).

Linux-side memory management reaches its end here at the OOM Killer. Looking back, we started from one full maps picture, learned the origin of every mapping, measured the malloc watershed, probed the depths of mprotect and madvise, crossed inter-process sharing, and finally landed on the three closing matters of alignment, huge pages, and commitment. And the one closing the account is the OOM Killer: what it picks out and kills are concrete, individual processes. How a process is born from fork, what gets replaced when it transforms — the program image, while the fd table follows along by default — how zombies are reaped, how a signal is delivered into its hands: all of that belongs to the next chapter, and we will pick up the thread there.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="aligned_alloc(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/aligned_alloc.3.html"
  />
  <ReferenceItem
    :id="2"
    title="posix_memalign(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_memalign.3.html"
  />
  <ReferenceItem
    :id="3"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="4"
    title="madvise(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/madvise.2.html"
  />
  <ReferenceItem
    :id="5"
    title="PR_SET_THP_DISABLE(2const)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/PR_SET_THP_DISABLE.2const.html"
  />
  <ReferenceItem
    :id="6"
    title="proc_sys_vm(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_sys_vm.5.html"
  />
  <ReferenceItem
    :id="7"
    title="proc(5) — oom_score / oom_score_adj"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc.5.html"
  />
  <ReferenceItem
    :id="8"
    title="Transparent Huge Support (kernel documentation)"
    publisher="Linux kernel documentation"
    url="https://docs.kernel.org/admin-guide/mm/transhuge.html"
  />
  <ReferenceItem
    :id="9"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
