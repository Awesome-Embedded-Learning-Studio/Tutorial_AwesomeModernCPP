---
title: "Windows Processes"
sidebar_order: 30
description: "Win32 processes and jobs: CreateProcessW dissected, exit codes and the four ways to die, the Job object's collective death and limits, nesting and breaking away; the console-event ignore bit and APC's queued execution"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
translation:
  source: documents/vol8-domains/systems-programming/windows/process/index.md
  source_hash: 049e85e55176d5e19841378c2bb30ff7d42cb54e89f50877d9cd6f29cb788922
  translated_at: '2026-11-22T15:45:43+00:00'
  engine: glm-5.3
  token_count: 2060
---
# Windows Processes

By the end of the memory chapter, that one CreateProcessW line inside spawn_self had run familiar under our fingers; this chapter puts it on the dissection table: how a process is created, how handles are inherited, the cleanup matrix of each of the four ways to die, closing with how the Job object herds a crowd of processes into one governed unit. The second article turns to answer what Windows offers in place of signals: the console events' registration chain and ignore bit, and APC's queuing and execution. On the common toolkit, `unique_handle` and `last_error_code` carry over their definitions from the two thinking-cornerstone articles, and `check_win32` is W01's; the second article's experiments are self-contained, and those tools were not invited onto the stage.

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-createprocess" desc="A measured dissection of CreateProcessW's ten parameters: only lpApplicationName=NULL walks the full search chain while a relative name resolves against the CWD alone (err=2); an unquoted path containing spaces plus a planted decoy named with.exe reproduces the Program.exe attack; with no decoy present, the system splices the full token and rewrites the child's command line; argv[0] is decoupled from the actually loaded path; the STARTUPINFOEX handle whitelist; the CREATE_SUSPENDED two-phase birth; lpEnvironment replacing the whole block, where leaving out PATH means the child never gets up (0xC0000135). Waiting and exit codes: after CloseHandle the process still lives (a handle is only the right to observe); return 42, abort's 0xC0000409 fail-fast, and TerminateProcess passed through verbatim; the STILL_ACTIVE=259 trap; WFMO from three angles. The four-ways-to-die cleanup matrix (return walks all three paths; a direct ExitProcess skips atexit yet the dynamic UCRT still flushes the buffers; TerminateProcess skips all three; CTRL_BREAK exits gracefully). The Job object: with KILL_ON_JOB_CLOSE, both roads execute everyone within timer resolution while the control group's orphans live on; a Job handle inherited by the child disables the collective death; QueryInformationJobObject statistics; a 40 MB limit yields err=1455; nesting and breaking away (inheritance, dual-conditional breakaway, forced break with err=5, silent breakaway, a parent Job's 48 MB crushing a child Job's 256 MB); the CRT's on-the-spot argv splitting and the quoted-parsing divergence, wrapped up by a five-row comparison table">Processes and Jobs: CreateProcessW and the Job Object</ChapterLink>
  <ChapterLink num="2" href="02-console-apc" desc="Windows has no SIGINT; Ctrl+C becomes a function call through console events: SetConsoleCtrlHandler's chain order puts the latest registration at the head (measured C→B→A→Z; TRUE truncates; unregistering strikes the name off); NULL+TRUE is an ignore bit that exempts only CTRL_C, not BREAK, and is inherited by child processes. The delivery model spawns a fresh thread per event (two events, two tids; an OpenThread probe returning gle=87 proves the handler's thread dies the moment it returns); with no handler, the default death is ExitProcess (0xC000013A), read identically by GetExitCodeProcess and cmd's ERRORLEVEL. Process groups in six scenes: CREATE_NEW_PROCESS_GROUP's immunity measures as exactly the implicit NULL+TRUE — the same ignore bit, undone by one NULL+FALSE inside the child; the targeted CTRL_C the documentation says cannot be received but measurement says was. Under WSL interop, CTRL_C fails to deliver because the startup chain inherited the ignore attribute; one line resets it, retracting W03's real-window attribution. The centerpiece APC: two queued calls sit unexecuted through a 609 ms non-alertable wait; on entering SleepEx(TRUE), both run FIFO within the same tick and the wait returns early with 192 — reusing the original thread, in contrast with console events' fresh threads; QueueUserAPC nudges a stuck alertable wait awake in 407 ms with WAIT_IO_COMPLETION, while the non-alertable control cannot be nudged and backlogged APCs die voided with the thread; a self-pipe mirror whose handler only calls SetEvent and a four-row comparison table; and Ctrl+C demoted to the byte 0x03 once ENABLE_PROCESSED_INPUT is switched off">Console Events and APC</ChapterLink>
</ChapterNav>
