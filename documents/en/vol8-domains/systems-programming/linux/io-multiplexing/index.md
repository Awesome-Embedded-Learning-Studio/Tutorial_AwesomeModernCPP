---
title: "Linux I/O Multiplexing and Asynchronous I/O"
sidebar_order: 40
description: "The behavioral boundaries and cost curves of three generations of waiting APIs (select, poll, and epoll), timerfd and eventfd converting time and events into file descriptors, and the submission and completion rings of io_uring"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - POSIX
translation:
  source: documents/vol8-domains/systems-programming/linux/io-multiplexing/index.md
  source_hash: ebad1739ca2bc5d554d26e2aa48f834c7bde9235fc99e6fc3df87fb1e9e715c6
  translated_at: '2026-11-22T15:07:40+00:00'
  engine: qwen3:14b
  token_count: 2047
---
# Linux I/O Multiplexing and Asynchronous I/O

We wait for a batch of file descriptors in a loop, and Linux provides three generations of APIs for this: select's bitmaps, poll's entry arrays, and epoll's resident registry. This section uses the same set of pipe file descriptors to quantify the boundaries and cost curves of the three generations. timerfd, eventfd, and io_uring are covered in subsequent sections. The kernel mechanisms of epoll will be fully explained in the networking volume, and each section here only provides behavioral comparisons and transitions.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-select-poll-epoll" desc="Passing the same set of pipe file descriptors to three generations of waiting APIs: empirically overturning select's 1024 limit (FD_SETSIZE is only 128 bytes in glibc's bitmap, manually expanding to 2048 bits allows fd=1025 to be ready as usual, the kernel does not check rlimit, but actually checks poll and returns EINVAL), comparing the number of scanned file descriptors upon waking (select checks 1002 file descriptors, poll checks 500 entries, and epoll only takes the ready 1), cost curves (poll's time per round increases linearly from 2527 to 104460 nanoseconds with N, epoll remains steady at 994/994/985 nanoseconds, the structural reason why 500 select outperforms poll), select's event loss due to rewriting timeout and bitmap parameters, pipe-based LT/ET behavior comparison (LT wakes 15 times, ET wakes only once, leaving 55904 bytes unreported), directly viewing the interest table via /proc/self/fdinfo (registering EPOLLIN records 0x19, the kernel automatically adds EPOLLERR and EPOLLHUP), all fd sources are pipes, corresponding to the IPC section, mechanism lessons are linked to the networking volume">I/O Multiplexing: Boundaries and Costs of select, poll, and epoll</ChapterLink>
  <ChapterLink num="2" href="02-timerfd-eventfd" desc="How time and events are converted into file descriptors and integrated into the same loop: eventfd's counter semantics (write adds once, read removes once, EFD_SEMAPHORE reads once and subtracts one, semaphore with LT's busy notification wakes 5 times, the upper limit is 2^64 - 2), timerfd's expiration counter (sleeping for 350ms, read returns 3 merged, changing interval uses the old it_value, textbook misses semantics, in the 1ms档, the median is 999.3 microseconds, compared to Windows' Sleep(5) which actually sleeps 12.6ms), reading three fields directly from fdinfo, ending with a pipe, eventfd, and timerfd all attached to one epoll, with an interleaved timeline from 151 to 901ms">timerfd and eventfd: Converting Time and Events into File Descriptors</ChapterLink>
  <ChapterLink num="3" href="03-io-uring" desc="Two shared memory rings for submission and completion, turning waiting for I/O into harvesting completion events: building the rings with raw syscalls (x86-64 starting from number 425, three mmap segments, manually filling SQE, linking to the signal section's number table), engineering practices with liburing 2.15, 64 reads with one submit and one wait compared to 64 calls to read(2), chained requests with read, write, and fsync using IOSQE_IO_LINK, order and failure propagation handled in the kernel (head failure changes fd to EBADF, downstream ECANCELED), epoll ADD on regular files returns EPERM, while unified asynchronous I/O is only available for disk files with io_uring, clock time on the page cache hot path is flattened with read(2), system call count differs by 128 times, timeouts are also requests compared to timerfd's fd in the table, graceful shutdown is left for the cross-platform abstraction section">io_uring: Submission Ring and Completion Ring</ChapterLink>
</ChapterNav>

The second section discusses timerfd and eventfd, converting timers and notifications into file descriptors and integrating them into the same loop. The third section discusses the submission and completion rings of io_uring. The three sections' main texts and experimental archives are all ready, and we continue following the learning path outlined in the [overview](../../00-overview.md) for cross-platform asynchronous abstractions.
