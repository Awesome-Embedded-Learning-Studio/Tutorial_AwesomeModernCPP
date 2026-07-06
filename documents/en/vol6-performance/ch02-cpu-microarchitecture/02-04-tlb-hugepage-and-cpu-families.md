---
chapter: 2
cpp_standard:
- 17
description: 'The ch02 finale: add the last puzzle piece — virtual-address translation. The TLB caches page-table entries, and huge pages use bigger pages to cut TLB pressure. Plus a microarchitecture-difference cheat sheet across CPU families (Intel / AMD Zen / Apple / ARM) as a desk reference for cross-platform tuning.'
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
reading_time_minutes: 8
related:
- 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
- 'Measurement traps and environment readiness: a 16-item checklist'
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存管理
title: 'TLB, huge pages, and a microarchitecture cheat sheet across CPU families'
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/02-04-tlb-hugepage-and-cpu-families.md
  source_hash: ae854453ebcd64b29fda774e5b9324ee03cb69aad919460a80fccbc1a6840e54
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2800
---
# TLB, huge pages, and a microarchitecture cheat sheet across CPU families

## There's one more translation gate

The first three articles exhausted cache, but the addresses a program uses are **virtual addresses**, while cache and main memory use **physical addresses**. Between the two sits a translation: every memory access has to translate the virtual address into a physical one before it can look in cache or DRAM. If this translation had to walk the full page table every time, the cost would be staggering. That's exactly the problem the **TLB (Translation Lookaside Buffer)** solves.

This article first lays out the TLB and huge-page mechanism, then appends a cheat sheet of microarchitecture differences across CPU families as the ch02 finale. After this, ch02's single-core hardware foundation is fully laid, and ch03 (attribution) and ch04 (optimization by bottleneck site) can build on top of it.

## The TLB: caching page-table entries, otherwise one translation costs several DRAM accesses

An x86-64 virtual address is 48 bits (newer CPUs support 57 bits, 5-level page tables), and the physical page size is **4 KB**. The page table is a 4-level tree (9 bits of index per level + 12 bits of page offset): to translate a virtual address, the CPU in principle has to walk 4 page-table pages in turn (one per level), each living in memory. **Worst case: one address translation = 4 DRAM accesses.** If it really worked that way, all the latency the cache saved upstream would be paid back and then some.

The TLB is the cache for this page table: it records recently translated "virtual page → physical page" mappings, and the next access to the same page just looks in the TLB, done in a few cycles, no page-table walk. The TLB and the data cache are two independent pieces of hardware. Your data may be in cache, but whether address translation goes through the TLB or the page table is a separate question.

The TLB is also layered: each core has an L1 dTLB (data) and an L1 iTLB (instructions), small but fast; below that sits a shared L2 TLB (AMD/Intel structures differ slightly). The L1 dTLB usually holds only tens to around a hundred entries (each governing one 4 KB page), so **when the number of pages your working set touches exceeds the dTLB capacity, TLB misses start to happen, and every miss pays a page walk, on the order of several DRAM accesses, tens to hundreds of nanoseconds**.

> The exact dTLB entry count varies by architecture and isn't the same across CPUs. For precise numbers, check the [Wikichip page for the microarchitecture](https://en.wikichip.org/wiki/amd/microarchitectures/zen_3) (separate pages for AMD / Intel) or the TLB section of Agner's microarchitecture manual. This article only covers structure and orders of magnitude.

So the question is: **how do you know whether your program is taking TLB-miss pain?** The cleanest way is the hardware counters (`perf stat -e dTLB-load-misses`); a high TLB-miss rate combined with a working set whose page count far exceeds the dTLB capacity means you're TLB-bound. Another common signal: **big working set + random access** programs (random index lookups in a database, big hash tables), even when the data is all in DRAM, have latency that runs higher than a pure DRAM access. That extra chunk is often the page walk.

## Huge pages: use bigger pages to bring the TLB entry count down

Since the TLB's capacity bottleneck is "entry count," one direct fix is to **make pages bigger** so one entry covers more memory. In addition to 4 KB pages, x86-64 also supports **2 MB** (and even 1 GB) huge pages. One 2 MB page covers as much memory as 512 4 KB pages, so the same working set needs only 1/512 as many TLB entries with 2 MB pages.

This translates to real performance in scenarios like these:

- **Databases** (PostgreSQL, MySQL) with random access to big indexes: working sets of tens of GB, billions of TLB entries needed under 4 KB pages, guaranteed to thrash; switching to 2 MB pages cuts latency significantly.
- **Big hash tables / JVM heaps**: the Java community has long debated whether enabling transparent huge pages (THP) speeds things up; the root cause is that JVM heaps run to several GB and put heavy pressure on the TLB.
- **Random access to big arrays in scientific computing**: same logic.

But huge pages aren't a free lunch. Bigger pages are harder to assemble (2 MB of contiguous physical memory is harder to find than 4 KB), which invites fragmentation; and the gain is small for **sequential-access**-dominant programs (the prefetcher and cache have already done the work, the TLB isn't the bottleneck). So the rule is: **confirm the TLB is really the bottleneck first (perf counters), then enable huge pages.** Don't turn them on blind.

### Run it yourself (and an honest negative result)

I want to demonstrate the huge-page payoff on this machine: the same 256 MB working set doing pointer chasing, one copy on plain 4 KB pages, one using `madvise(MADV_HUGEPAGE)` to request transparent huge pages, and see whether the huge-page version is faster. Core of the code:

```cpp
void* a4 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
void* a2 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
madvise(a2, SZ, MADV_HUGEPAGE);   // request transparent huge pages
// run the same pointer chase on both, compare latency
```

Three runs:

```text
Run 1: 4KB page 136.5 ns   2MB page 134.0 ns   ratio 1.02
Run 2: 4KB page 135.8 ns   2MB page 137.4 ns   ratio 0.99
```

The ratio is basically 1.0, **huge pages brought no measurable improvement**. Why? Checking `/proc/self/smaps` shows `AnonHugePages: 0 kB`: **WSL2 (the environment I'm running on) simply didn't grant transparent huge pages**, `madvise` was requested but the kernel never actually delivered. So this "no difference" is real — it's not that huge pages are useless, it's that this environment didn't give me huge pages.

I'm writing this negative result down verbatim to drive home the ch01 measurement discipline again: **the optimization you think you turned on may not have taken effect at all.** On this WSL2 box, `perf` isn't installed, THP doesn't deliver, CPU frequency can't be read. Each one will throw off your reading of performance numbers. Move to bare-metal Linux, `echo always > /sys/.../transparent_hugepage/enabled` or pre-allocate a hugetlb pool, and this same code can measure a ten-to-forty-percent improvement on a TLB-bound workload. **Conclusions can be cited from authoritative sources, but the numbers in your hand have to come from the environment you actually ran on, and you have to confirm first that the environment really meets the preconditions.**

## A microarchitecture cheat sheet across CPU families

With the TLB covered, ch02's hardware foundation is complete. But the numbers in the previous three articles centered on this machine's AMD Zen 3; switch to Intel, Apple Silicon, or ARM, and the specific numbers change. The table below gives directions and orders of magnitude — **for precise numbers, consult the corresponding [Wikichip](https://en.wikichip.org) microarchitecture page or Agner's microarchitecture manual.** It's not for memorizing; it's an entry point for "which direction to look in" when tuning across platforms.

| Dimension | Intel (recent gens) | AMD Zen (2/3/4/5) | Apple (A-series to M-series) | ARM Cortex (X / big cores) |
|---|---|---|---|---|
| **Decode width** | 6-wide (since Golden Cove) | 4-wide x86 decode, **op-cache brings it to 6-8 µops/cycle** | very wide (~8-wide), no x86 decode burden | wide (4-5+) |
| **ROB depth** | deep (~400-500+) | mid-deep (Zen3 ~256) | very deep (~600+) | moderate |
| **L1d / L2** | 48 KB / 1-2 MB | 32 KB / 512KB-1MB | 128 KB / large | 64 KB / variable |
| **LLC structure** | shared LLC (MIC / integrated) | Zen3+ single-CCD shared large L3 | system-level cache (SLC) | shared /clustered L3 |
| **TLB** | L1 dTLB + L2 TLB | L1 dTLB + L2 TLB | similar layering | similar layering |
| **Signature** | deep pipeline, strong front-end | unified CCD large cache, high frequency | ultra-wide ROB, high ILP | energy efficiency, licensable |

How to read this table: don't stare at absolute numbers (generations evolve); stare at **structural differences**. For example, "AMD Zen relies on 4-wide x86 decode + op-cache to prop up throughput, while Apple is genuinely ultra-wide decode + ultra-deep ROB" — that's one of the reasons Apple Silicon can beat x86 on IPC (instructions per cycle), and it also explains why "front-end bottlenecks" (decode can't keep up) show up more often in x86 code than ARM (ch04-07 front-end optimization covers this). Or "Zen3 merged the L3 into a single-CCD shared structure" — that's AMD's key move to drastically cut cross-core cache latency, directly shaping the multithreaded performance curve (ch05).

> This table strictly belongs to the "giving pointers" category. Register-renaming-table size, execution-port distribution, per-instruction latency/throughput — that **architecture-table-level** data — Agner volume 3 (microarchitecture) and volume 4 (instruction tables) are the desk authority, Wikichip is the online encyclopedia. vol6 stops at the depth that supports "understanding why things are fast or slow"; for deeper digging, look those two up.

## ch02 wrap-up: four hardware foundations

With that, the three layers of single-core hardware are covered:

1. **Memory hierarchy** (02-01): the latency ladder of L1/L2/L3/DRAM, a 100x gap at each step. Sequential access can approach L1 throughput because the prefetcher is helping.
2. **Cachelines and locality** (02-02): 64 bytes is the minimum transfer unit, spatial locality decides that contiguous layout is fast, row-major vs column-major differ by 6x.
3. **Pipeline and ILP / branches** (02-03): ILP decides whether execution units are fed (multiple accumulators, 2.9x); branch prediction penalizes unpredictable branches hard (sorted vs shuffled, 4.2x).
4. **TLB and huge pages** (this article): address translation is another gate, huge pages cut TLB pressure, but you have to confirm the environment actually delivered them first.

These are the hardware foundation for every recommendation in ch04 "optimize by bottleneck site": why use contiguous containers, why control the hot working set, why multiple accumulators, why branchless, why division is a bottleneck, why front-end PGO helps. Every one of them traces back to a number in these four chapters. ch03 then teaches us **how to measure which of these four blocks the current program's bottleneck lands in**, and ch04 prescribes accordingly.

## References

- Bryant & O'Hallaron, *CSAPP*, Chapter 9 *Virtual Memory*: concepts and costs of page tables, the TLB, and page-table walks
- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22 *AMD Ryzen* and the various Intel chapters: architecture-level details of the TLB, pipeline, and execution ports. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Wikichip *Microarchitectures*: precise-parameter lookup across CPU families (Intel / AMD / Apple / ARM), the desk entry point for cross-platform tuning
- Drepper, U., *What Every Programmer Should Know About Memory*: an engineering view of TLB, huge pages, and page tables
- Source for this article's measurements: `code/volumn_codes/vol6-performance/ch02/tlb_hugepage.cpp`
