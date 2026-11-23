---
title: "Windows Asynchronous I/O"
sidebar_order: 40
description: "The in-flight world of OVERLAPPED and FILE_FLAG_OVERLAPPED: the three forms of ReadFile, ReadFileEx completion routines, the CancelIo family, WFMO's handle cap and out-of-order completions, plus the completion port IOCP"
platform: host
tags:
  - cpp-modern
  - host
  - advanced
  - 系统编程
  - Win32
  - 异步编程
translation:
  source: documents/vol8-domains/systems-programming/windows/async-io/index.md
  source_hash: 7fe64317c17b3f2c9ad143a9368364c5bbadaa121ec4aed2a454f86288688d05
  translated_at: '2026-11-22T15:31:51+00:00'
  engine: glm-5.3
  token_count: 1608
---

# Windows Asynchronous I/O

This chapter cashes in the foreshadowing the file I/O chapter banked: `FILE_FLAG_OVERLAPPED`, which W01 introduced by sight; the OVERLAPPED structure with its 997 and 995 codes, first staged in W05; the ReadFileEx completion routine that the processes chapter left as a promise; and the WaitForMultipleObjects cap of at most 64 handles — all of them get measured numbers here. The first article walks the two paths of events and completion routines; the second folds both paths into the completion port's queue.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-overlapped" desc="One ReadFile going from return-only-when-full to in-flight once you hand over an OVERLAPPED: three forms aligned on one screen (an OVERLAPPED call on a synchronous handle still blocks while the file pointer measurably follows from 16 to 8008; five back-to-back async issues all FALSE+997; a GOR facing EOF returns FALSE+38 while the synchronous side returns TRUE+0; a bare read gets 87 with the pointer left hanging), ReadFileEx completion routines only run inside alertable waits (routine execution count 0 during a 500ms non-alertable wait; entering alertable runs them FIFO within the same tick; the wait returns early with 192; the hEvent sentinel reads back untouched), thread ownership in the CancelIo family (a read issued by a worker cannot be undone by CancelIo on the main thread, the GOR still reads 996; only CancelIoEx without naming the handle gets 995; the three codes 997/996/995 carry three distinct meanings), the WFMO and MsgWait walls measured separately (64/65 versus 63/64; the 64-minus-one story belongs to the MsgWait family), six in-flight issues in submission order 1..6 completing in order 6 5 4 3 2 1, and with hEvent=NULL two in-flight handles light up as soon as the first packet lands so you cannot tell which one finished — the measured reason each issue gets its own manual-reset event">OVERLAPPED Asynchronous I/O and WaitForMultipleObjects</ChapterLink>
  <ChapterLink num="2" href="02-iocp" desc="How the completion port turns waiting on a pile of in-flight requests into dequeuing completion packets: the key and OVERLAPPED pointer of the triple acknowledging each other one by one (three out-of-order offset reads; an empty queue returns 258 after 813ms; a second handle with key=777 each matched to its own), the six-pipe mirror swap still harvesting completion order 6 5 4 3 2 1, four worker threads across four combinations: interval-fed rounds single-threaded end to end, while with eight packets in one burst concurrency value 0 wakes all four threads to share the load and concurrency value 1 still receives single-file (the cap does not assign work, it only admits), PostQueuedCompletionStatus letting a bare port act as a wakeup channel (the Windows analogue of eventfd and the standard signal for graceful shutdown), closing a handle with an in-flight read delivers the completion packet immediately with 109 broken-pipe while the proper route collects 995 via CancelIoEx before closing, a Job attached to the port with NEW_PROCESS, EXIT_PROCESS, and ACTIVE_PROCESS_ZERO packet fields landing all recorded (delivering on the death-notice promise from the processes chapter), 100 in-flight event-style issues forced by the 64 cap into 64+36 two-segment polling that rescans everything on every wakeup while a single IOCP port scans nothing — wall-clock time in the same ballpark, the difference is in the structure">IOCP Completion Ports</ChapterLink>
</ChapterNav>
