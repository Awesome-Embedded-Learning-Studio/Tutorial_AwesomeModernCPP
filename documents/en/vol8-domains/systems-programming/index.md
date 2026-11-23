---
title: "System Programming"
description: "Modern C++ encapsulation of application-layer OS APIs for Linux and Windows: file I/O, processes and threads, memory mapping, synchronization primitives, with parallel comparisons between the two platforms"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
translation:
  source: documents/vol8-domains/systems-programming/index.md
  source_hash: baf0ce470eef26233846f57e725cf71b8cb09bbf6b7bd2def285b29547190cc8
  translated_at: '2026-11-22T14:18:19+00:00'
  engine: qwen3:14b
  token_count: 3107
---
# System Programming

This is the system programming subdomain of Volume 8, with the main thread being our established **parallel coverage of both camps**: for each topic, we cover it once on the Linux side using POSIX, and once on the Windows side using Win32. At the end of each topic, there is a short section titled "What's on the other side?" that explicitly connects the concepts from both sides. In the real world, you often need to write code for both platforms, and APIs may become outdated, but the concepts remain.

We recommend you start with the [Overview](./00-overview.md) to get a sense of the overall structure of this volume, and then proceed to [Thinking](./thinking/) to address two major cross-camp issues: how to ensure resources are not leaked, and how to ensure errors are reported accurately. Both sides start with file I/O: the Linux side uses `fd` and the Windows side uses `HANDLE` as the foundation for their respective worlds. Both sides are also ready for the memory chapter, and the process and asynchronous I/O chapters follow suit: the process chapter on the Linux side covers `fork/exec` all the way to signals and graceful shutdown, while the Windows side takes us into console events and APCs. The asynchronous I/O chapter on the Linux side covers three generations of wait APIs up to `io_uring`, while the Windows side includes completion ports. In the cross-platform chapter, we use concepts to constrain asynchronous backends at compile time. The three chapters on time and the three chapters on terminals (on both sides) are also complete, ranging from the nine clocks all the way to timer drift comparisons, and from the termios switchboard to the recording and playback of pseudo-terminals. Finally, the two chapters on platform abstraction wrap up the volume: the abstraction layer design moves `#ifdef` to a single point for backend selection, and `syskit` compiles all the common tools across the entire sub-volume into a library that compiles on both sides. At this point, the forty chapters of the entire volume are complete.

We have divided the work with the [Networking](../networking/index.md) volume in this series: they cover sockets and event loops (`epoll/Reactor`), while we cover general OS resources such as files, memory, processes, and terminals. The intersection between the two is `epoll`, and the learning path in the overview will directly connect to the I/O multiplexing chapter without repeating the explanation.

## Thinking Foundation

<ChapterNav variant="sub">
  <ChapterLink href="thinking/" desc="Cross-camp conceptual foundation: RAII paradigm for OS resources (fd, HANDLE, move-only skeleton for mappings) and error handling paradigm; definitions of the series-wide utility classes unique_fd/unique_handle/mapped_region">Thinking Foundation</ChapterLink>
</ChapterNav>

## Linux Side

<ChapterNav variant="sub">
  <ChapterLink href="linux/file-io/" desc="The life of an fd: open/read/write, dup2 redirection, page cache and fsync, and mmap memory mapping; the series-wide utility classes unique_fd/sys_call/errno_code reuse the definitions from the two Thinking Foundation chapters">File I/O</ChapterLink>
  <ChapterLink href="linux/memory/" desc="Process address space: full map of /proc/pid/maps, virtual memory API, shared memory and aligned huge pages">Process Memory</ChapterLink>
  <ChapterLink href="linux/process/" desc="The life of a process and the full chain of signal consumption: fork/exec/posix_spawn and COW demonstration, daemon processes, IPC seven-channel selection, sigaction and asynchronous signal safety, signalfd and pidfd for graceful shutdown">Process</ChapterLink>
  <ChapterLink href="linux/io-multiplexing/" desc="Behavioral boundaries and cost curves of three generations of wait APIs (revisiting the 1024 limit), timerfd and eventfd converting time and events into fd, io_uring's submission and completion rings, connecting to the epoll chapter in the networking volume without repeating the mechanism lesson">I/O Multiplexing and Asynchronous I/O</ChapterLink>
  <ChapterLink href="linux/time/" desc="Survey of nine clockids and resolution precision debate, vDSO tenfold difference measurement, C++20 chrono clock alignment with leap second timeline and timezone hard cases, four generations of timer APIs and periodic task drift comparison">Time and Timers</ChapterLink>
  <ChapterLink href="linux/terminal/" desc="Bit-by-bit testing of four termios flags and VMIN/VTIME character interval timer discrimination, pseudo-terminal posix_openpt four steps and script(1) replication recording and playback, all running on a custom pty platform">Terminal</ChapterLink>
</ChapterNav>

## Windows Side

<ChapterNav variant="sub">
  <ChapterLink href="windows/file-io/" desc="HANDLE, CreateFileW and synchronous read/write, and file mapping CreateFileMapping and MapViewOfFile; Windows-side utility classes unique_handle/check_win32/last_error_code reuse the definitions from the two Thinking Foundation chapters">File I/O</ChapterLink>
  <ChapterLink href="windows/memory/" desc="VirtualAlloc two-phase reservation/commit, allocation granularity, PAGE_GUARD, VirtualQuery, and named shared memory with page file backup">Memory</ChapterLink>
  <ChapterLink href="windows/process/" desc="CreateProcessW dissection, four ways of death cleanup matrix and Job object governance; console event ignore bits and delivery model, APC queuing and execution">Process</ChapterLink>
  <ChapterLink href="windows/async-io/" desc="OVERLAPPED's in-flight world and ReadFileEx completion routine, and completion port IOCP queue harvesting">Asynchronous I/O</ChapterLink>
  <ChapterLink href="windows/console/" desc="Character buffer grid direct write and cursor semantics, input event queue, VT sequence on/off read-back comparison">Console</ChapterLink>
</ChapterNav>

## Cross-Platform

<ChapterNav variant="sub">
  <ChapterLink href="cross-platform/" desc="Four backend difference matrix and concepts compile-time constraints, platform abstraction layer design (zero-diagnostic dead branch verification and #ifdef exit), syskit tool library consolidation (item-by-item comparison and unification of two near-relatives)">Cross-Platform and Wrap-up</ChapterLink>
</ChapterNav>
