---
title: "CPU microarchitecture and the memory hierarchy"
description: "ch02 lays down the full single-core hardware foundation: the memory hierarchy's latency ladder, cachelines and locality, the pipeline with ILP and branch prediction, and the TLB with huge pages — every piece backed by measurements run on our own machine, providing the hardware base for ch04's optimize-by-bottleneck-site advice"
translation:
  source: documents/vol6-performance/ch02-cpu-microarchitecture/index.md
  source_hash: 40d4632d4791ec76b3e2fe3701b94417ff678ae2e03aedbbcd0492c534c90fd9
  translated_at: '2026-09-26T05:54:25+00:00'
  engine: anthropic
  token_count: 600
---

# CPU microarchitecture and the memory hierarchy

ch00 laid down "correct first, then fast" and "measure first, then optimize", and ch01 built "measure first" into a complete methodology. But between "measure first" and "actually optimizing" there is still one missing piece of knowledge: which kind of cost are you actually optimizing away? If unrolling a loop made it 3x faster, was that because of the cache, instruction-level parallelism, or branch prediction? Without the root cause in hand, optimization is blind guesswork.

This chapter breaks the single-core hardware into four layers, and for each layer uses measurements run on our own machine to spell out "what happens on the hardware that makes it fast or slow":

- **Memory hierarchy**: the L1/L2/L3/DRAM latency ladder opens up a 100x gap level by level, and sequential access can approach L1 throughput thanks to the prefetcher.
- **Cachelines and locality**: 64 bytes is the cache's minimum unit of transfer, spatial locality is why contiguous layouts are fast, and row-major vs column-major traversal differs by 6x.
- **Pipeline and ILP / branch prediction**: instruction-level parallelism decides whether the execution units stay fed (multiple accumulators are 3x faster), and unpredictable branches get punished hard (sorted vs shuffled differs by 4x).
- **TLB and huge pages**: virtual-address translation is another gate to pass; huge pages lower TLB pressure, but you first have to confirm the environment actually delivered them.

The numbers in this chapter (100x, 6x, 3x, 4x) are the physical basis for every recommendation in ch04's "optimize by bottleneck site" — why use contiguous containers, why control the hot data set, why multiple accumulators, why branchless, why division is the bottleneck, why front-end PGO helps... every one of them traces back to here. On depth, we stop at "enough to support a judgment"; for the deeper material — ROB / register renaming / execution-port scheduling — we point you to Agner's microarchitecture manual and Wikichip.

## In this chapter

<ChapterNav variant="sub">
  <ChapterLink href="02-01-memory-hierarchy">Memory hierarchy and the latency ladder: why sequential access is 100x faster</ChapterLink>
  <ChapterLink href="02-02-cacheline-and-locality">Cachelines and locality: the 64-byte minimum unit of transfer</ChapterLink>
  <ChapterLink href="02-03-pipeline-ilp-branch">Pipeline, ILP, and branch prediction</ChapterLink>
  <ChapterLink href="02-04-tlb-hugepage-and-cpu-families">TLB, huge pages, and a microarchitecture cheat sheet across CPU families</ChapterLink>
</ChapterNav>
