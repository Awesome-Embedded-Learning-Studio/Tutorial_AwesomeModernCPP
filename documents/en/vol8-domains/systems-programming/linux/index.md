---
title: "Linux System Programming"
sidebar_order: 10
description: "Modern C++ encapsulation of POSIX application layer APIs: file I/O, memory mapping, processes and threads with synchronization"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
translation:
  source: documents/vol8-domains/systems-programming/linux/index.md
  source_hash: cf0431c18d109edd92dac05ebf776a2a0b035db3a966cf5ff76aebd80c07b9c7
  translated_at: '2026-11-22T15:00:18+00:00'
  engine: qwen3:14b
  token_count: 1968
---
# Linux System Programming

On the Linux side, we start with file I/O as the foundation: fd is the handle base for all resources in Linux, and the socket, epoll, and mmap we will encounter later all rely on it. The entire series of common tools (`unique_fd`, `sys_call`, `errno_code`) follow the definitions from the two articles on [Thinking Foundation](../thinking/). In subsequent articles, we will only reference them and not redefine them.

<ChapterNav variant="sub">
  <ChapterLink href="file-io/" desc="The life of open/read/write and fd, dup2 redirection, pread, page cache and fsync; mmap memory mapping">File I/O</ChapterLink>
  <ChapterLink href="memory/" desc="/proc/pid/maps full map and 41 segment dissection, variable layout table, malloc watershed and dynamic threshold, smaps Rss/Pss, ASLR; virtual memory API, shared memory and aligned large page allocation">Process Memory</ChapterLink>
  <ChapterLink href="process/" desc="The life of a process and the full chain of signal consumption: fork/exec/posix_spawn and COW Pss verification, zombie two-phase and three types of corpse collection, orphan adoption subreaper mechanism, RAII child_process; setsid double fork six groups of tests, rlimit and environ; pipe/FIFO/POSIX mq and SCM_RIGHTS seven channels plus selection matrix; sigaction and asynchronous signal safety, real-time signals, signalfd and pidfd for graceful shutdown">Process</ChapterLink>
  <ChapterLink href="io-multiplexing/" desc="Behavior boundaries and cost curves of three generations of waiting APIs (select, poll, epoll), including the re-examination of the 1024 limit, scanning volume and cost curve tests; timerfd and eventfd convert time and events into fd; and io_uring's submission and completion ring, connecting with the epoll section in the network volume, and not repeating the mechanism class">I/O Multiplexing and Asynchronous I/O</ChapterLink>
  <ChapterLink href="time/" desc="Census of nine clockids and resolution accuracy, vDSO tenfold difference test, C++20 chrono clock alignment, leap second timeline, timezone hard case (including China's 1988 daylight saving time), four generations of timer APIs and periodic task drift comparison (relative sleep drift about 78ms vs absolute expiration below 0.08ms)">Time and Timers</ChapterLink>
  <ChapterLink href="terminal/" desc="Four groups of termios flags tested bit by bit: canonical line buffering and ^C signal translation (^C's queue and signal can only do one thing), VMIN/VTIME character interval timer resetting on each byte received with D+F combined judgment, RAII terminal_guard and TCSAFLUSH residual input processing, all experiments run on a self-built pty platform, main terminal zero contact; pseudo-terminal posix_openpt four steps and forkpty, script(1) replication and timing replay (1257.1ms vs 1255.8ms, cmp byte-by-byte consistent), master read/write asymmetry EIO criteria">Terminal and Console</ChapterLink>
</ChapterNav>

Six articles on file I/O, four on memory, five on processes, three on I/O multiplexing, three on time, and two on terminals are all in place, completing the six chapters on the Linux side, aligning with the learning path in the [Overview](../00-overview.md). The I/O multiplexing section uses the same set of pipe fd to measure the boundaries and costs of three generations of APIs, connecting with the epoll kernel mechanism class covered in the network volume without repeating the mechanism class. The time section covers all nine clocks, chrono's calendar time zones, and four generations of timer APIs. The terminal section covers all the switches and pseudo-terminal recording and playback of termios, with the Windows console section in another subtree. Finally, both Linux and Windows sides are collected into platform abstraction.
