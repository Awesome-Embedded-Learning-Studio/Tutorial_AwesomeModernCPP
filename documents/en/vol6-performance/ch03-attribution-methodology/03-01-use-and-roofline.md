---
chapter: 3
cpp_standard:
- 17
description: After measuring that something is slow, you have to answer why. We learn two complementary high-level attribution frameworks first — Brendan Gregg's USE method (check utilization, saturation, and errors for every resource to rule out system-wide causes first), then the Roofline model (use arithmetic intensity to tell at a glance whether your code needs less memory traffic or more SIMD)
difficulty: advanced
order: 1
platform: host
prerequisites:
- Memory hierarchy and the latency ladder — why sequential access is 100x faster
- Benchmark methodology reference card
reading_time_minutes: 7
related:
- TMAM's four buckets and hardware sampling — LBR / PEBS / Intel PT
- Flame graphs, the perf workflow, and COZ / eBPF
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: "The USE method and the Roofline model: system-wide first, then compute vs bandwidth"
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-01-use-and-roofline.md
  source_hash: 22792ef0ead1715614163a3333dcc09a034d3e4611f44fdcf98215fba5be2031
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 2800
---
# The USE method and the Roofline model: system-wide first, then compute vs bandwidth

## Once you've measured "slow," don't rush to change code

ch01 taught us how to measure performance accurately, and ch02 handed us the hardware base. But the moment you actually start optimizing a slow program, you run into two questions. First, **where exactly is it slow?** Is the CPU not computing fast enough, is the data not arriving fast enough, or is it not computing at all (waiting on a lock, on IO)? The second question is sneakier: **is this bottleneck even worth fixing?** You spend a day making some function 3x faster, but if it's only 2% of total time, the user won't feel a thing.

This chapter (attribution methodology) answers those two questions. It doesn't speed anything up directly; instead, it gives you a localization workflow from "slow" to "where the bottleneck is and how big its share is." Get the localization right and ch04's "optimize by bottleneck site" can prescribe the right medicine; get it wrong and you're just busy for nothing.

We split the work across three articles on three complementary tools, plus a synthesis walkthrough. This article covers two high-level frameworks (USE for the system-wide view, Roofline for compute vs bandwidth), 03-02 covers Intel's TMAM four buckets (attributing the bottleneck to a pipeline stage), 03-03 covers flame graphs (localizing down to a line of code), and 03-04 chains them into one complete workflow. The relationship between the three tools is: USE first rules out system-wide causes, Roofline tells compute vs bandwidth at a glance, TMAM drills down to a pipeline stage, and flame graphs land on the code.

> Most of the tools in this chapter are system-level profilers like `perf`, `toplev`, and flame-graph scripts. The machine I'm writing on is WSL2, with no `perf` installed and no way to run `toplev`. So the commands and outputs in this chapter are **quoted from authoritative sources (Brendan Gregg, Bakhvalov, easyperf.net) and labeled as such**, not faked as if I ran them on this machine. The commands themselves are standard Linux performance-analysis moves and work the same on a bare-metal Linux box with perf installed. This discipline of "honest about environment limits" is itself part of ch01's measurement methodology.

## The USE method: three checks per resource

USE is a system-level health-check framework coined by Brendan Gregg. The name is an acronym for three checks, applied to every resource in the system:

- **U**tilization: the fraction of time it's busy.
- **S**aturation: the queue length or wait depth, meaning work is already piling up.
- **E**rrors: error counts, hardware faults, dropped packets, retransmits, that sort of thing.

Resources include CPU, memory, disk, network, bus, mutexes, thread pools, connection pools — anything that can become a bottleneck. USE's value is exhaustive early-stage triage: you don't have to guess where the bottleneck is, you sweep U/S/E for every resource and treat whichever one is saturated first, which avoids "staring at one spot optimizing while the real bottleneck is elsewhere."

A few resource-to-metric mappings (full table at brendangregg.com/usemethod.html):

| Resource | Utilization | Saturation | Errors |
|---|---|---|---|
| CPU | `%us`+`%sy` from `vmstat 1` | `vmstat` `r` column (run queue length) > core count | — |
| Memory | `free -m` / `sar -B` | `vmstat` `si`/`so` (swap in/out) > 0 | OOM in `dmesg` |
| Network | `rxkB/s` from `sar -n DEV` | `ifconfig` drops / `netstat -L` overflow | `ifconfig` errors |
| Disk | `%util` from `iostat -xz 1` | `iostat` `avgqu-sz` / `await` | `dmesg` / smart |

One counterintuitive point to remember about USE: **low average utilization can still mean saturation.** A CPU averaging 80% over five minutes can hide second-level spikes to 100%, so saturation (queue length) surfaces problems earlier than average utilization. This same corollary showed up in ch01-03's "measurement pitfalls" (averages dragged by long tails); statistically it's the same effect.

USE is used at the very beginning of a performance investigation. A few minutes sweeping the system rules out the obvious — "memory is paging," "disk is maxed," "network is dropping" — before you sink down to the microarchitecture level covered in ch02. It doesn't answer "which line of code is slow," but it keeps you from diving into a code dead-end right at the start.

## The Roofline model: compute ceiling vs bandwidth ceiling

After USE has given you the system-wide view and confirmed the bottleneck is on CPU compute, the next split to make is: **is it compute-bound (Core Bound) or is the data not being fed fast enough (Backend Memory Bound)?** The two bottlenecks call for opposite fixes. Compute-bound wants more SIMD and fewer instructions; bandwidth-bound wants less memory traffic and a different data layout. Get the call wrong and the optimization is wasted.

The Roofline model (Williams et al., CACM 2009) gives an extremely simple criterion. Plot a program on a 2D chart:

- **X axis**: arithmetic intensity (AI), how many operations per byte of memory traffic, in **ops/byte**.
- **Y axis**: attainable compute throughput, **ops/s** (or FLOPS/s).
- **Two rooflines**: the horizontal line is peak CPU compute; the slanted line is peak memory bandwidth (`ops/s = bytes/s × AI`, so it's a line through the origin).

Your program lands at a point on this chart based on its arithmetic intensity, and the height of the "roof" it can reach is its theoretical peak performance. The key reading:

- If the program point **hugs the slanted line** (the bandwidth line), it's **memory-bandwidth-bound**, and adding SIMD won't help; you need to cut memory traffic.
- If the program point **hugs the horizontal line** (the compute line), it's **compute-bound**, and cutting memory traffic won't help; you need more compute (SIMD, fewer instructions).

The intersection of the slanted line and the horizontal line is called the **roofline point**, and the arithmetic intensity at that point is the boundary where you "saturate bandwidth and start tipping into compute." Programs with AI below the roofline point are all bandwidth-bound; only those above it can be compute-bound.

### Two examples by hand: dot and axpy

The nice thing about Roofline is that arithmetic intensity can be computed by hand, no profiler needed. Let's work two classic BLAS kernels:

**Dot product `dot = Σ a[i]*b[i]`**:

- Per iteration: 2 floating-point ops (one multiply, one add), reads 2 floats (8 bytes).
- Arithmetic intensity = `2 FLOP / 8 B = 0.25 FLOP/byte`.

**AXPY `y[i] = α*x[i] + y[i]`**:

- Per iteration: 2 floating-point ops (multiply, add), reads 2 floats + writes 1 float (12 bytes).
- Arithmetic intensity = `2 FLOP / 12 B ≈ 0.17 FLOP/byte`.

For a chip in the 5800H class, the roofline-point arithmetic intensity sits around **~20-25 FLOP/byte** (peak FP32 throughput around 900 GFLOPS, peak DDR bandwidth around 30-40 GB/s, divide the two). Dot's 0.25 and axpy's 0.17 are far below the roofline point, so these two kernels are **unambiguously memory-bandwidth-bound**, riding the bandwidth slope.

That conclusion directly sets the optimization direction: for dot and axpy, don't try to squeeze SIMD lane utilization (the compute direction), cut memory traffic instead (the bandwidth direction). For example, merge multiple arrays into SoA for one big load, use wider loads, or switch algorithms entirely to move less data. That's exactly what ch04-01 "backend memory" covers.

Conversely, a matrix multiply `C += A·B` has much higher arithmetic intensity (each element is reused many times), with AI in the tens, landing on the compute line. So matrix-multiply optimization is about SIMD and tiling to extract compute, not about cutting traffic. Same source code, completely different optimization direction, all determined by where AI lands.

> Magnitude note: the 5800H peak compute and bandwidth figures above are **order-of-magnitude approximations**, not pinned exact numbers, because they float with turbo frequency, AVX mode, and memory configuration; pinning them would be misleading. The pedagogical value of Roofline is in the qualitative call "does AI land on the slope or the horizontal line," not in any one machine's exact peaks. When you do need exact peaks, look up the CPU spec page and measure memory bandwidth (e.g., the STREAM benchmark).

USE and Roofline are both high-level, fast frameworks you can pick up without a deep profiler. USE, at the very start of an investigation, exhaustively sweeps resource utilization, saturation, and errors to rule out "the problem isn't even on the CPU." Roofline, once the bottleneck is confirmed to be compute, uses arithmetic intensity to tell compute-bound (add SIMD) from bandwidth-bound (cut traffic) at a glance.

They narrow the battlefield down to "a resource / a class of bottleneck," but they haven't drilled down to "which stage of the pipeline." That's exactly what the TMAM four buckets do, and the next article walks into the CPU pipeline to attribute the bottleneck to Frontend / Backend / Bad Speculation / Retiring.

## References

- Brendan Gregg, *The USE Method*, brendangregg.com/usemethod.html — the original USE framework and the full metric tables per resource.
- Williams, Waterman, Patterson, *Roofline: An Insightful Visual Performance Model for Multicore Architectures*, CACM 2009 — the original Roofline paper.
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 6 *Analysis Approaches* — the engineering treatment of USE and Roofline.
- Ofenbeck et al., *Applying the Roofline Model* — the engineering-compute view of arithmetic intensity.
