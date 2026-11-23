---
title: "Linux Process Memory"
sidebar_order: 20
description: "The process address space: the complete /proc/pid/maps picture, virtual memory APIs, shared memory, and aligned allocation with huge pages"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
  - 内存管理
translation:
  source: documents/vol8-domains/systems-programming/linux/memory/index.md
  source_hash: be148902ec7f86fd375c9fc196616bde9d33ca5ae89ef2d3bb8d31ad705cd670
  translated_at: '2026-11-22T15:45:00+00:00'
  engine: glm-5.3
  token_count: 2750
---

# Linux Process Memory

This memory chapter starts from the address space itself: the first article identifies all 41 segments of a C++ program in `/proc/self/maps`, and the later articles on virtual memory APIs, shared memory, and aligned allocation with huge pages all build on that foundation. The series-wide shared utilities (`unique_fd`, `sys_call`, `errno_code`) carry over from the two articles under [Thinking Foundations](../../thinking/); the articles in this directory only reference them and do not redefine them.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-memory-layout" desc="A running C++ program, and who lives in each segment of its address space: 41 segments fall into six major classes; the second r-- segment of every file-backed module turns out to be the page fenced off by GNU_RELRO, which the dynamic linker makes read-only with mprotect after finishing relocation; 18 classes of variables match the table 18/18, with .bss spanning segments (its head borrows the last page of the file's rw segment to land on, its middle continues into anonymous follow-on pages) and &printf landing in libc's .text; the malloc watershed measures 131049/131050, and a follow-up strace reveals the real mechanism — carving from the heap top on the spot when it has slack, going to mmap only when the chunk size roundup(request+8,16) reaches 131072, while freeing 4MB even pushes glibc's dynamic threshold up to 4MB; brk shrinks back via trim while the stack's top holds still and the low-address end probes downward; Rss and Pss amortize libc .text's 1004kB into 7kB, a freshly compiled binary's .text turns out to be Private_Dirty and goes Clean after sync; ASLR changes across all five runs, while setarch -R reproduces byte-identical layouts across five runs — exactly the world gdb sees by default">Process Memory Layout: The Complete /proc/pid/maps Picture</ChapterLink>
  <ChapterLink num="2" href="02-vm-apis" desc="Three layers of management once you hold a stretch of virtual memory: mprotect changes page permissions, with si_addr following the faulting byte exactly (correcting L02's page-start rough reading); in the W^X trifecta Linux permits RWX while macOS returns EPERM, widening text/rodata/heap all returns rc=0 via COW, and the real refusals are [vvar]'s EACCES and [vdso]'s EINVAL; after de-privileging a .data page, the first cold call dies in ld.so's lazy binding writing the GOT, and the handler's first act on entry is restoring the page's permissions; the whole madvise family — RANDOM yields exactly zero readahead, NORMAL adaptively oscillates between 3088/8192 KiB, DONTNEED reads anonymous pages back as zero and halves Rss, DONTFORK makes the whole segment vanish in the child, REMOVE genuinely punches holes even on ext4; mlock pre-faults along the way and measures ENOMEM right at the RLIMIT_MEMLOCK boundary; the signature guarded_buffer<T> uses a tail-aligned guard page to name the exact offending byte of an out-of-bounds access on the spot, mincore's 1 has three readings (zero page / page cache / truly resident), process_vm_readv takes one cross-process read, and the ending previews Windows PAGE_GUARD's one-shot trap">Virtual Memory API Panorama: mprotect/madvise/mlock</ChapterLink>
  <ChapterLink num="3" href="03-shm" desc="Two unrelated processes sharing the same physical pages: shm_open's namespace is exactly the directory entries of /dev/shm; a new object has size 0 and needs ftruncate; creating the same name again before unlink eats EEXIST while rebuilding after unlink sails through, the old mapping reads 0x1111 and the new entity writes 0x2222 with neither visible to the other — name and entity are thereby decoupled; umask clips permissions (root's 0600 blocks user, CAP_DAC_OVERRIDE lets it through the other way); three live-tested versions of the race: nosync loses 9.62%~23.70% over three rounds with assembly confirming the lock-prefix-less addq, the PTHREAD_PROCESS_SHARED mutex hits 44.7 ms every time versus 58.1 ms for the inter-process semaphore; two sharing routes (anonymous mapping with fork inheritance, named objects with stranger processes, SCM_RIGHTS fd-passing in one sentence); the signature SPSC lock-free ring queue moves one million items with zero loss and zero reordering, sched_yield backpressure beats CPU spinning by more than 60%, attributed to cache-line ping-pong with core-pinning controls ruling out same-core scheduling; a 1 MiB transfer at 7304 versus 2349 MiB/s, about three times a pipe; memfd_create leaves no name and vanishes on close">Shared Memory: shm_open and Mapping</ChapterLink>
  <ChapterLink num="4" href="04-align-hugepage" desc="The three aligned-allocation tools aligned_alloc/posix_memalign/aligned new pass every sample yet split error reporting three ways (NULL+errno, EINVAL as return value, bad_alloc exception); an unaligned address fed to _mm256_load_si256 segfaults immediately — that is a #GP, not a page fault; THP is the centerpiece: out of the box MADV_HUGEPAGE yields only AnonHugePages=0, the diagnostic chain uncovers WSL2's init setting MMF_DISABLE_THP on the process tree, a third switch overriding sysfs, and once cleared with prctl a full 1 GiB goes huge with first-touch writes 5.43x faster; explicit huge pages via MAP_HUGETLB return ENOMEM in all three settings; the overcommit number chain: 512 GiB refused, MAP_NORESERVE granted on the spot, VmSize maxed out while VmRSS never moves; the OOM Killer closes the chapter with read-only observation plus a safe demonstration">Aligned Allocation and Huge Pages: 32-Byte Alignment, 2 MiB Pages, and a Rejected 512 GiB</ChapterLink>
</ChapterNav>

With this, the four Linux-side memory articles are complete, and the route aligns with the learning path in [the overview](../../00-overview.md).
