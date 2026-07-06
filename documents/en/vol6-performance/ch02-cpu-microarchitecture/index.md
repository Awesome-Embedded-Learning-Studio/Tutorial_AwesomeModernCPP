---
title: "CPU microarchitecture and the memory hierarchy"
description: "ch02 lays out the full single-core hardware foundation: the memory-hierarchy latency ladder, cachelines and locality, the pipeline / ILP / branch-prediction trio, and the TLB with huge pages. Each piece is backed by measurements run on the author's own machine, providing the hardware base for ch04's optimize-by-bottleneck-site advice."
---

# CPU microarchitecture and the memory hierarchy

ch00 established "correctness first, then speed" and "measure first, then optimize." ch01 turned "measure first" into a complete methodology. But between "measure first" and "actually optimize," there's one piece of knowledge still missing: which kind of cost are you actually optimizing away? If unrolling a loop makes it 3x faster, is that because of cache, instruction-level parallelism, or branch prediction? Without the root cause, optimization is just guessing.

This chapter splits the single core into four layers, and for each layer uses on-machine measurements to explain "what happens on the hardware that makes this fast or slow":

- **Memory hierarchy**: the L1/L2/L3/DRAM latency ladder falls off by 100x at each step; sequential access can approach L1 throughput thanks to the prefetcher.
- **Cachelines and locality**: 64 bytes is the minimum unit of cache transfer, spatial locality is why contiguous layout is fast, and row-major vs column-major traversal differs by 6x.
- **Pipeline / ILP / branch prediction**: instruction-level parallelism decides whether execution units are fed (multiple accumulators are 3x faster), and unpredictable branches get penalized hard (sorted vs shuffled is a 4x gap).
- **TLB and huge pages**: virtual-address translation is another gate, huge pages cut TLB pressure, but you have to confirm the environment actually delivered them first.

The numbers in this chapter (100x, 6x, 3x, 4x) are the physical basis for every recommendation in ch04's "optimize by bottleneck site" — why use contiguous containers, why control the hot working set, why multiple accumulators, why branchless, why division is a bottleneck, why front-end PGO helps. Every one of them traces back to here. On depth we stop at "enough to support judgment"; ROB / register renaming / execution-port scheduling and the deeper content get pointers to Agner's microarchitecture manual and Wikichip.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="02-01-memory-hierarchy">Memory hierarchy and the latency ladder: why sequential access is 100x faster</ChapterLink>
  <ChapterLink href="02-02-cacheline-and-locality">Cachelines and locality: the 64-byte minimum unit of transfer</ChapterLink>
  <ChapterLink href="02-03-pipeline-ilp-branch">Pipeline, ILP, and branch prediction</ChapterLink>
  <ChapterLink href="02-04-tlb-hugepage-and-cpu-families">TLB, huge pages, and a microarchitecture cheat sheet across CPU families</ChapterLink>
</ChapterNav>
