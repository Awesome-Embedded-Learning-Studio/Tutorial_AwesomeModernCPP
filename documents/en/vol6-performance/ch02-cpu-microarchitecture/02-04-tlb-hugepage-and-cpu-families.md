---
chapter: 2
cpp_standard:
- 17
description: 'The ch02 finale adds the last puzzle piece — virtual address translation,
  where the TLB caches page-table entries and huge pages use bigger pages to cut
  TLB pressure — plus a cheat sheet of microarchitecture differences across CPU
  families (Intel / AMD Zen / Apple / ARM) as a desk-side entry point for anyone
  doing cross-platform tuning.'
difficulty: advanced
order: 4
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- 'Cachelines and locality: the 64-byte minimum unit of transfer'
reading_time_minutes: 8
related:
- 'Backend memory bottlenecks: cache-friendly, AoS/SoA, and prefetch'
- 'Measurement pitfalls and environment readiness: a 16-item checklist'
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
  translated_at: '2026-09-26T05:54:31+00:00'
  engine: anthropic
  token_count: 4500
---
# TLB, huge pages, and a microarchitecture cheat sheet across CPU families

## One more gate to pass: translation

The previous three articles covered cache thoroughly, but everything a program works with is **virtual addresses**, while cache and main memory use **physical addresses**. Between the two sits a translation: on every memory access, the CPU must first translate the virtual address into a physical one before it can go consult cache or DRAM. If every one of those translations walked the full page table, the cost would be staggering — and that is exactly the problem the **TLB (Translation Lookaside Buffer)** exists to solve.

This article first explains how the TLB and huge pages work, then closes ch02 with a cheat sheet of microarchitecture differences across CPU families. Once we are done here, ch02's single-core hardware foundation is fully laid, and ch03 (attribution) and ch04 (optimization by bottleneck site) can build on top of it.

## The TLB: caching page-table entries, or a single translation costs several DRAM accesses

An x86-64 virtual address is 48 bits (newer CPUs support 57 bits with 5-level page tables), and the physical page size is **4 KB**. The page table is a 4-level tree (a 9-bit index per level + a 12-bit page offset): to translate a virtual address, the CPU in theory has to visit 4 page-table pages in sequence (one per level), each of them living in memory. **Worst case: one address translation = 4 DRAM accesses.** If it really worked that way, all the latency the cache saved us earlier would be paid back with interest.

The TLB is precisely the cache for this page table: it records recently translated "virtual page → physical page" mappings, so the next access to the same page looks up the TLB directly and is done in a few cycles, no page-table walk required. The TLB and the data cache are two separate pieces of hardware: your data may well be sitting in cache, but whether address translation goes through the TLB or the page table is another matter entirely.

The TLB is hierarchical too: each core has one L1 dTLB (for data) and one L1 iTLB (for instructions), small but fast; below those sits a shared L2 TLB (the AMD and Intel organizations differ slightly). An L1 dTLB typically holds only a few dozen to a hundred-odd entries (each covering one 4 KB page), so **once the number of pages your working set touches exceeds the dTLB's capacity, TLB misses start happening, and every miss pays the price of a page walk — on the order of several DRAM accesses, tens to a hundred-plus nanoseconds**.

> The exact dTLB entry count varies by architecture, and not all CPUs are the same. When you need exact numbers, check [Wikichip's page for the microarchitecture](https://en.wikichip.org/wiki/amd/microarchitectures/zen_3) (AMD / Intel each have dedicated pages) or the TLB section of Agner's microarchitecture manual; this article sticks to structure and orders of magnitude.

Which raises the question: **how do you tell whether your program is losing out to TLB misses?** The cleanest test is the hardware counters (`perf stat -e dTLB-load-misses`): a high TLB miss rate plus a working set whose page count far exceeds the dTLB capacity means you are TLB-bound. Another common signal: programs with a **large working set + random access** (random index lookups in a database, big hash tables) show latency a notch above pure DRAM access even when all the data lives in DRAM — and that extra notch is usually the page walk.

## Huge pages: bigger pages that bring the TLB entry requirement down

Since the TLB capacity bottleneck is "entry count", one direct fix is to **make pages bigger**, so that a single entry covers more memory. Besides 4 KB pages, x86-64 also supports **2 MB** (and even 1 GB) huge pages. A 2 MB page covers as much memory as 512 4 KB pages, so the same-size working set needs only 1/512 as many TLB entries with 2 MB pages.

This buys real, measurable performance in scenarios like these:

- **Databases** (PostgreSQL, MySQL) randomly accessing large indexes: a working set of tens of GB needs tens of millions of TLB entries under 4 KB pages — guaranteed thrashing; switching to 2 MB pages cuts latency significantly.
- **Big hash tables / JVM heaps**: the Java community has long debated whether enabling transparent huge pages (THP) speeds things up, and the root cause is simply that JVM heaps routinely run to several GB, which puts heavy pressure on the TLB.
- **Random access over large arrays in scientific computing**: same reasoning.

But huge pages are no free lunch: bigger pages are harder to scrounge up (2 MB of contiguous physical memory is much harder to find than 4 KB) and tend to cause fragmentation; and programs dominated by **sequential access** see little benefit (the prefetcher and the cache have already done the work — the TLB isn't the bottleneck). So the rule is: **first confirm the TLB really is the bottleneck (perf counters), then enable huge pages** — don't turn them on blindly.

### Try it yourself (and an honest negative result)

I wanted to demonstrate the huge-page payoff on my own machine: take the same 256 MB working set through a pointer chase, one copy on ordinary 4 KB pages, one requesting transparent huge pages via `madvise(MADV_HUGEPAGE)`, and see whether the huge-page version runs faster. The core of the code:

```cpp
void* a4 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
void* a2 = mmap(nullptr, SZ, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
madvise(a2, SZ, MADV_HUGEPAGE);   // request transparent huge pages
// run the same pointer chase over both copies and compare latency
```

The results of three runs:

```text
第 1 次: 4KB 页 136.5 ns   2MB 页 134.0 ns   比值 1.02
第 2 次: 4KB 页 135.8 ns   2MB 页 137.4 ns   比值 0.99
```

The ratio is essentially 1.0 — **the huge pages delivered no measurable gain**. Why? A look into `/proc/self/smaps` turns up `AnonHugePages: 0 kB`: **WSL2 (the environment I ran on) never granted transparent huge pages in the first place** — `madvise` requested them, but the kernel never actually delivered. So this "no difference" is genuine: it isn't that huge pages are useless, it's that this environment never handed me any huge pages.

I am recording this negative result here as-is because I want to re-emphasize that measurement discipline from ch01: **an optimization you believe you enabled may never have taken effect at all.** On this WSL2 box, `perf` isn't installed, THP was never delivered, and the CPU frequency can't be read — any one of these can pull your interpretation of a performance number away from reality. On bare-metal Linux, after `echo always > /sys/.../transparent_hugepage/enabled` or pre-allocating a hugetlb pool, this very same code does measure gains of ten-plus to tens of percent on TLB-bound workloads. **Conclusions can be cited from authoritative sources, but the numbers in your hands must come from the environment you actually ran in — and you must first confirm that the environment genuinely meets the preconditions.**

## A microarchitecture cheat sheet across CPU families

With the TLB covered, ch02's hardware foundation is complete. But the numbers in the previous three articles centered on this machine's AMD Zen 3 — move to Intel, Apple Silicon, or ARM, and the specific numbers change. The table below gives directions and orders of magnitude — **for exact numbers, consult [Wikichip](https://en.wikichip.org)'s page for the microarchitecture or Agner's microarchitecture manual**. It isn't meant to be memorized; it's an entry point telling you which direction to look when you tune across platforms.

| Dimension | Intel (recent generations) | AMD Zen (2/3/4/5) | Apple (A-series through M-series) | ARM Cortex (X / big cores) |
|---|---|---|---|---|
| **Decode width** | 6-wide (since Golden Cove) | 4-wide x86 decode, **topped up to 6-8 µops/cycle by the op-cache** | extremely wide (~8-wide), and no x86 decode burden | wide (4-5+) |
| **ROB depth** | deep (~400-500+) | medium-deep (Zen3 ~256) | extremely deep (~600+) | moderate |
| **L1d / L2** | 48 KB / 1-2 MB | 32 KB / 512KB-1MB | 128 KB / large | 64 KB / varies |
| **LLC structure** | shared LLC (MIC / on-die) | Zen3+ single-CCD shared large L3 | system-level cache (SLC) | shared / per-cluster L3 |
| **TLB** | L1 dTLB + L2 TLB | L1 dTLB + L2 TLB | similarly tiered | similarly tiered |
| **Distinguishing traits** | deep pipelines, strong front end | unified single-CCD large cache, high clocks | ultra-wide ROB, high ILP | efficiency, licensable |

How to read this table: don't stare at the absolute numbers (generations keep evolving); stare at the **structural differences**. For example, "AMD Zen pairs 4-wide x86 decode with an op-cache to top up throughput, while Apple goes genuinely ultra-wide on decode plus an ultra-deep ROB" — that is one root reason Apple Silicon can go toe-to-toe with x86 on IPC (cycles per instruction), and it also explains why a "front-end bottleneck" in x86 code (decode can't keep up) shows up more often than on ARM (ch04-07 covers front-end optimization). Or take "Zen3 consolidated the L3 into a single-CCD shared cache" — that was the key move behind AMD sharply cutting cross-core cache latency, and it directly shapes the multithreaded performance curve (ch05).

> Strictly speaking, this table belongs to the "pointers" category. For data at the **architecture-datasheet level** — register renaming table sizes, execution-port layouts, per-instruction latency/throughput — Agner's volume 3 (microarchitecture) and volume 4 (instruction tables) are the desk-side authority, and Wikichip is the online encyclopedia. vol6 stops at just enough for you to understand why things are fast or slow; for deeper digging, go to those two.

## ch02 wrap-up: four hardware foundations

At this point, all three layers of single-core hardware have been covered:

1. **Memory hierarchy** (02-01): the L1/L2/L3/DRAM latency ladder — level by level, a 100x gap. Sequential access approaching L1 throughput is the prefetcher helping out.
2. **Cachelines and locality** (02-02): 64 bytes is the minimum unit of transfer, spatial locality is why contiguous layouts are fast, and row-major vs column-major differs by 6x.
3. **Pipeline and ILP / branches** (02-03): ILP decides whether the execution units stay fed (multiple accumulators, 2.9x), and branch prediction punishes unpredictable branches hard (sorted vs shuffled, 4.2x).
4. **TLB and huge pages** (this article): address translation is one more gate; huge pages lower TLB pressure, but you must first confirm the environment actually delivers them.

These are the hardware foundation behind every recommendation in ch04, "optimize by bottleneck site": why use contiguous containers, why control the hot working set, why multiple accumulators, why branchless, why division is a bottleneck, why front-end PGO helps — every one of them traces back to some number in these four articles. Next, ch03 teaches us **how to measure which of these four blocks the current program's bottleneck falls into**, and then ch04 prescribes the right remedy.

## References

- Bryant & O'Hallaron, *CSAPP*, chapter 9 *Virtual Memory*: the concepts and costs of page tables, the TLB, and page-table walks
- Agner Fog, *The microarchitecture of Intel, AMD and VIA CPUs*, §22 *AMD Ryzen* and the various Intel chapters: architecture-level detail on the TLB, pipeline, and execution ports. Local copy: `.claude/drafts/books/optimazation_in_cpp/microarchitecture.md`
- Wikichip *Microarchitectures*: exact parameters for every CPU family (Intel / AMD / Apple / ARM); the desk-side entry point for cross-platform tuning
- Drepper, U., *What Every Programmer Should Know About Memory*: an engineering perspective on the TLB, huge pages, and page tables
- The code measured for this article: `code/volumn_codes/vol6-performance/ch02/tlb_hugepage.cpp`
