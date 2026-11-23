---
title: "Windows Memory"
sidebar_order: 20
description: "Win32 virtual memory and the heap: VirtualAlloc's reserve/commit two-phase model, allocation granularity, PAGE_GUARD, VirtualQuery, and page-file-backed named shared memory"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
  - 内存管理
translation:
  source: documents/vol8-domains/systems-programming/windows/memory/index.md
  source_hash: a8959c3d4ea4412ae7cbfe0eb61caedf41abe3372217ef7cb74715d2c5c5b37e
  translated_at: '2026-11-22T15:38:54+00:00'
  engine: glm-5.3
  token_count: 1895
---
# Windows Memory

Back in the file I/O chapter, W02 already introduced file-backed mappings and the SEC_RESERVE two-phase model; this chapter shows the file out and takes up the pure-memory viewpoint: how a process asks the system for memory, how commitment and physical pages go their separate ways, how the out-of-bounds alarm is laid, and finally how two processes share the same block of memory. The common toolkit stays as before: `unique_handle` and `last_error_code` carry over their definitions from the two thinking-cornerstone articles, and `check_win32` carries over W01's definition.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-virtualalloc" desc="The entry point by which a Windows process asks the system for memory directly: the reserve/commit two-phase model, measured (MEM_RESERVE of 1 GiB completes in about 2–5 microseconds while the commit charge does not move a byte; committing 72 KiB raises the charge by 80 KiB, and only after 18 pages are touched does the working set grow by 72 KiB — commit is a promise, physical memory is only occupied when touched; MEM_DECOMMIT returns pages to the reserved state and they can be recommitted; MEM_RELEASE only accepts the whole-block base address with dwSize=0, and both the non-base address and the nonzero size fail with 87 in practice, not the intuitive 487); allocation granularity on two tracks (addresses always 64 KiB aligned while RegionSize rounds by pages: 1 byte → 1 page, 0xFFFF → 16 pages, 0x10001 → 17 pages, 0x100001 → 257 pages; the granularity block's tail is FREE, not a hidden reservation; MEM_TOP_DOWN lands in the 0x7FF4 high address band); a three-part evidence chain for PAGE_GUARD's one-shot trap (0x104 → 0x80000001 → self-disarm to 0x004 → re-arm for one more hit), with the system stack built exactly on a guard band (a 2 MiB reservation holding a COMMIT section plus a 2–3 page guard band; after a 768 KiB push the guard band moved down 764 KiB while VEH recorded zero hits during growth; when the reservation runs out, 0xC00000FD is what you collect); VirtualQuery sweeping the whole 128 TiB in 193 steps (IMAGE 99 / MAPPED 16 / PRIVATE 17 / RESERVE 24 / FREE holes 37, contrasted with /proc/self maps on row count and backing information; probe semantics: BaseAddress rounds down, RegionSize is the remaining length, the grouping key includes AllocationBase so adjacent allocations never merge); the malloc→HeapAlloc→VirtualAlloc three tiers and three size thresholds (≤384 KiB through the main heap segment, 416–1016 KiB through a new heap segment, ≥1 MiB straight to VirtualAlloc with the whole segment returned on free; malloc(32) and default-heap HeapAlloc(32) share the same AllocationBase — the CRT sits on the process heap)">Virtual Memory: VirtualAlloc and VirtualProtect</ChapterLink>
  <ChapterLink num="2" href="02-shared-mem" desc="How two processes share a block of memory: the answer hides in the INVALID_HANDLE_VALUE that W02 half-revealed — the page-file-backed named mapping object takes the lead role here. A same-name Create again collects err=183 while the requested size is silently ignored; the name dies with the last handle and the object dies with the last reference, a two-stage death. Cross-process views measured with base addresses 502 GiB apart; the offset aligns to the 64 KiB granularity while the length rounds only by pages; the parent's pointer value reads VirtualQuery=MEM_FREE in the child, and a peer probe process that dereferences it collects 0xC0000005. The named synchronization trio (lock-free, two processes lose 83411/200000; a named mutex guards exactly two hundred thousand; when the holder dies holding the lock, the waiter receives WAIT_ABANDONED and the lock still works); the signature SPSC ring queue delivers one million messages with zero loss and zero reordering, spin at 288M msgs/s versus per-message notification at 4.71M msgs/s, wrapped up by a same-machine WSL2 comparison against shm_open plus an eight-row comparison table">Shared Memory: Page-File-Backed Named Mapping Objects</ChapterLink>
</ChapterNav>
