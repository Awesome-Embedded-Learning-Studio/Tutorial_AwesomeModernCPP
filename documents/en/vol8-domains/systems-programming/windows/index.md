---
title: "Windows Systems Programming"
sidebar_order: 20
description: "Modern C++ wrappers over Win32 application-level APIs: file I/O, file mapping, virtual memory and shared memory, processes, threads, and synchronization"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
translation:
  source: documents/vol8-domains/systems-programming/windows/index.md
  source_hash: 97adf550b0805e8b035f11689b2c3195afc1f6390c5d8d61ac7aff21f0d138a2
  translated_at: '2026-11-22T15:50:12+00:00'
  engine: glm-5.3
  token_count: 1367
---
# Windows Systems Programming

The Windows side likewise lays its foundation with file I/O: HANDLE is the handle base under every Windows kernel object — files, processes, and events are all held through it. The first chapter picks the common tools up ready-made: `unique_handle` and `last_error_code` reuse the Thinking Foundation chapters' definitions, while the Windows-side `check_win32` is defined in that very chapter — later chapters only reference them and never redefine.

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="Handles, CreateFileW, and synchronous reads and writes, plus file mapping with CreateFileMapping and MapViewOfFile">File I/O</ChapterLink>
  <ChapterLink href="memory/" desc="VirtualAlloc's reserve/commit two phases, the 64 KiB allocation-granularity dual track, PAGE_GUARD's one-shot trap, VirtualQuery's full picture and the heap's three layers, plus named shared memory backed by the paging file">Memory</ChapterLink>
  <ChapterLink href="process/" desc="CreateProcessW's ten parameters dissected, the cleanup matrix of four ways to die and Job-object governance, the console event's ignore bit and delivery model, and APC's queuing and execution">Process</ChapterLink>
  <ChapterLink href="async-io/" desc="OVERLAPPED's three forms and in-flight harvesting, ReadFileEx completion routines, the CancelIo family's thread ownership, WFMO's and MsgWait's two walls of limits, plus out-of-order completion and the measured reasons for manual-reset events">Async I/O</ChapterLink>
  <ChapterLink href="console/" desc="Direct writes into the character-buffer grid and cursor semantics, the input event queue's Peek/Read/Flush, VT-sequence read-back comparisons before and after the switch, and Win11 26200's \x1b[2J not homing the cursor">Console</ChapterLink>
</ChapterNav>

File I/O, the memory chapter's two pieces, the process chapter's two, the async I/O pair, and the console chapter are all in place — all six chapters of the Windows side are complete. The process chapter covers process creation, exit, Job-object governance, and Windows' answer to the signal question: console events and APC. The async I/O chapter starts from OVERLAPPED and event harvesting and then gathers in the completion port's queue, keeping its junction with the networking volume unchanged and unexpanded. The console chapter walks the whole input-output surface: character buffers, event queues, and VT sequences. In the end, the Linux and Windows sides fold together into the platform abstraction.
