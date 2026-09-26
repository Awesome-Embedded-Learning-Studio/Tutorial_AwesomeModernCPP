---
chapter: 3
cpp_standard:
- 17
description: Once you've measured "slow", the next question is why it's slow. Start with two complementary high-level attribution frameworks — Brendan Gregg's USE method (check utilization/saturation/errors for every resource, system-wide first to rule out system-level bottlenecks), and the Roofline model (arithmetic intensity tells you at a glance whether your code should cut memory traffic or add SIMD)
difficulty: advanced
order: 1
platform: host
prerequisites:
- 'Memory hierarchy and the latency ladder: why sequential access is 100x faster'
- Benchmark methodology reference card
reading_time_minutes: 7
related:
- 'The four TMAM buckets and hardware sampling: LBR / PEBS / Intel PT'
- Flame graphs, the perf workflow, and COZ / eBPF
tags:
- host
- cpp-modern
- advanced
- 优化
- 工程实践
title: 'The USE method and the Roofline model: system-wide first, then compute vs bandwidth'
translation:
  source: documents/vol6-performance/ch03-attribution-methodology/03-01-use-and-roofline.md
  source_hash: 22792ef0ead1715614163a3333dcc09a034d3e4611f44fdcf98215fba5be2031
  translated_at: '2026-09-26T06:09:55+00:00'
  engine: anthropic
  token_count: 5500
---
# The USE method and the Roofline model: system-wide first, then compute vs bandwidth

## You've measured "slow" — now hold off on touching the code

ch01 taught us how to measure performance accurately, and ch02 gave us the hardware foundation. But the moment you actually set out to optimize a "slow" program, you slam into two questions. First, **where exactly is it slow?** Is the CPU unable to keep up with the math, is the data unable to get moved in fast enough, or is it not computing at all (waiting on a lock, waiting on IO)? The second question is sneakier: **is this bottleneck even worth fixing?** You can spend a day making some function 3x faster, but if it accounts for only 2% of total time, users will never feel the difference.

This chapter (attribution methodology) exists to answer those two questions. It doesn't make anything faster by itself; what it provides is a localization pipeline from "slow" to "where the bottleneck is and how large its share is". With the localization right, ch04's "optimize by subsystem" can actually prescribe the right medicine; get it wrong, and you're just thrashing.

We split the chapter into three articles on three complementary tools, plus one hands-on walkthrough. This article covers two high-level frameworks (USE for the system-wide view, Roofline for the compute-vs-bandwidth verdict); 03-02 covers Intel's four TMAM buckets (attributing the bottleneck to a specific pipeline stage); 03-03 covers flame graphs (pinpointing the exact lines of code); 03-04 chains everything into one complete workflow. The three tools relate like this: USE first rules out system-level problems, Roofline splits compute-bound from bandwidth-bound at a glance, TMAM drills down to the pipeline stage, and flame graphs land on the code.

> Most of the tools in this chapter are system-level profilers — `perf`, `toplev`, flame graph scripts. The machine I'm writing this on runs WSL2: `perf` isn't installed, and `toplev` won't run either. So the commands and outputs in this chapter are **quoted from authoritative sources (Brendan Gregg, Bakhvalov, easyperf.net) with attribution** — not a pretense that I ran them here. The commands themselves are standard moves in Linux performance analysis; on a bare-metal Linux box with perf installed they work exactly as shown. This discipline of "being honest about environmental constraints" is itself part of ch01's measurement methodology.

## The USE method: check three things for every resource

USE is a system-level health-check framework proposed by Brendan Gregg; the name is an acronym for three things you check, for every resource in the system:

- **U**tilization: the fraction of time it stays busy.
- **S**aturation: the length of queues/waits — there is already more work than the resource can drain.
- **E**rrors: error counts — hardware faults, dropped packets, retransmissions, and the like.

Resources cover CPU, memory, disk, network, bus, mutexes, thread pools, connection pools — anything that can become a bottleneck. The strength of USE is exhaustive early triage: you don't have to guess where the bottleneck is; sweep U/S/E for every resource and treat whichever saturates first. It keeps you from "staring at one spot and optimizing it while the real bottleneck is somewhere else".

A few resource-to-metric mappings (the complete table lives at brendangregg.com/usemethod.html):

| Resource | Utilization | Saturation | Errors |
|---|---|---|---|
| CPU | `%us`+`%sy` from `vmstat 1` | `r` column of `vmstat` (run-queue length) > core count | — |
| Memory | `free -m` / `sar -B` | `si`/`so` of `vmstat` (swap in/out) > 0 | OOM in `dmesg` |
| Network | `rxkB/s` from `sar -n DEV` | drops from `ifconfig` / `netstat -L` overflow | errors from `ifconfig` |
| Disk | `%util` from `iostat -xz 1` | `avgqu-sz` / `await` from `iostat` | `dmesg` / smart |

One counterintuitive point about USE is worth committing to memory: **a resource can saturate even at low average utilization.** A CPU averaging 80% over five minutes can hide second-scale spikes pinning 100%; that is why saturation (queue length) exposes trouble earlier than average utilization. The same corollary appeared in ch01-03's "measurement pitfalls" (averages dragged around by long tails) — statistically it's the same story.

USE belongs at the very start of a performance investigation: a few minutes sweeping the system rules out the obvious culprits — "memory is paging", "the disk is maxed out", "the network is dropping packets" — before you drop down to the microarchitecture level from ch02. It doesn't answer "which line of code is slow", but it keeps you from charging into a code dead end right at the start.

## The Roofline model: the compute roof vs the bandwidth roof

Once USE has given you the system-wide view and confirmed the bottleneck sits in CPU computation, the next split to make is: **not enough compute (Core Bound), or data that can't be fed fast enough (Backend Memory Bound)?** The two bottlenecks call for exactly opposite fixes. Compute-bound wants more SIMD and fewer instructions; bandwidth-bound wants less memory traffic and a different data layout. Get the verdict backwards and the optimization is wasted.

The Roofline model (Williams et al., CACM 2009) hands you a minimal decision criterion. Plot the program on a 2D chart:

- **Horizontal axis**: arithmetic intensity (AI) — how many operations each byte of memory traffic buys, in **ops/byte**.
- **Vertical axis**: attainable compute, in **ops/s** (or FLOPS/s).
- **Two rooflines**: the horizontal line is the CPU's peak compute; the slanted line is the peak memory bandwidth (`ops/s = bytes/s × AI`, hence a line through the origin).

Your program lands somewhere on the chart according to its arithmetic intensity, and the "roof" height that point can reach is its theoretical peak performance. The key readings:

- If the program's point **hugs the slanted line** (the bandwidth line), it is **memory-bandwidth-bound**: piling on more SIMD does nothing — you must cut memory traffic.
- If the program's point **hugs the horizontal line** (the compute line), it is **compute-bound**: cutting memory traffic does nothing — you must add compute (SIMD, fewer instructions).

The intersection of the slanted and horizontal lines is called the **ridge point (roofline point)**, and the arithmetic intensity there marks the boundary of "just saturating the bandwidth, ready to pivot to compute". Programs with AI below the ridge point are all bandwidth-bound; only above it can a program be compute-bound.

### Two worked examples by hand: dot and axpy

The nice thing about Roofline is that arithmetic intensity can be computed by hand — no profiler required. Let's run the numbers on two classic BLAS kernels:

**Dot product `dot = Σ a[i]*b[i]`**:

- Per iteration: 2 floating-point operations (one multiply, one add), reading 2 floats (8 bytes).
- Arithmetic intensity = `2 FLOP / 8 B = 0.25 FLOP/byte`.

**AXPY `y[i] = α*x[i] + y[i]`**:

- Per iteration: 2 floating-point operations (multiply, add), reading 2 floats + writing 1 float (12 bytes).
- Arithmetic intensity = `2 FLOP / 12 B ≈ 0.17 FLOP/byte`.

For a chip in the 5800H class, the ridge-point arithmetic intensity lands in the **~20-25 FLOP/byte** range (peak FP32 compute around 900 GFLOPS, peak DDR bandwidth around 30-40 GB/s — divide one by the other). dot's 0.25 and axpy's 0.17 sit far below the ridge point, so both kernels are **without a doubt memory-bandwidth-bound**, running flat against the bandwidth slope.

That conclusion points straight at the optimization direction: for dot and axpy, don't fuss over SIMD lane utilization (the compute direction) — cut memory traffic instead (the bandwidth direction): pack multiple arrays into SoA so one load brings them in, use wider loads, or change the algorithm altogether to move less data. That is what ch04-01's "backend memory" article is about.

Conversely, a matrix multiply `C += A·B` has far higher arithmetic intensity (each element gets reused many times); its AI can reach the tens, landing it on the compute line. So matrix-multiply optimization leans on SIMD / blocking to squeeze out compute, not on cutting memory traffic. Same kind of code, completely different optimization direction — it all depends on where the AI lands.

> A note on magnitudes: the 5800H peak compute and bandwidth figures above are **order-of-magnitude approximations**, not pinned-down exact numbers, because they float with turbo frequency, AVX mode, and memory configuration — pinning them would mislead. Roofline's pedagogical value is the qualitative call of "does the AI land on the slope or the horizontal line", not any one machine's exact peaks. When you do need exact peaks, look up the CPU spec page and measure the memory bandwidth (e.g., with the STREAM benchmark).

USE and Roofline are both high-level, fast frameworks you can pick up without a deep profiler. USE, at the very start of an investigation, exhaustively sweeps every system resource's utilization/saturation/errors to rule out "the problem isn't on the CPU at all"; Roofline, once the bottleneck is confirmed to be in computation, uses arithmetic intensity to tell compute-bound (add SIMD) from bandwidth-bound (cut memory traffic) at a glance.

They can shrink the battlefield to "one resource / one class of bottleneck", but they haven't yet drilled down to "which stage of the pipeline". That is precisely what the four TMAM buckets are for — in the next article we step inside the CPU pipeline and sort the bottleneck into the four buckets: Frontend / Backend / Bad Speculation / Retiring.

## References

- Brendan Gregg, *The USE Method*, brendangregg.com/usemethod.html — the original USE framework write-up and the complete metric table per resource
- Williams, Waterman, Patterson, *Roofline: An Insightful Visual Performance Model for Multicore Architectures*, CACM 2009 — the original Roofline paper
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs*, Chapter 6, *Analysis Approaches* — the engineering treatment of USE and Roofline
- Ofenbeck et al., *Applying the Roofline Model* — an engineering-computation view of arithmetic intensity
