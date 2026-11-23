---
title: "Console Events and APC"
description: "Second article of the Windows-side process chapter, answering a question Linux has a ready answer for but Windows forces you to ask again: when Ctrl+C is pressed, by what right does it become a function call? SetConsoleCtrlHandler's chain order puts the latest registration at the head (measured C→B→A→Z; TRUE truncates; unregistering strikes the name off), and NULL+TRUE is an ignore bit that exempts only CTRL_C, not BREAK, and is inherited by child processes. The delivery model spawns a fresh thread per event — two events, two tids, and an OpenThread probe returning gle=87 proves the handler's thread dies the moment the handler returns. With no handler, the default destination is ExitProcess (0xC000013A), with GetExitCodeProcess and cmd's ERRORLEVEL=-1073741510 agreeing across both readings. Process groups in six scenes: CREATE_NEW_PROCESS_GROUP's immunity measures as exactly the implicit NULL+TRUE (the same ignore bit, undone by one NULL+FALSE inside the child); the targeted CTRL_C that the documentation says cannot be received but that measurement says was — the contradiction recorded as found. Under WSL interop, CTRL_C fails to deliver because the startup chain inherited the ignore attribute; one line, SetConsoleCtrlHandler(NULL,FALSE), resets it, and this retracts W03's real-window attribution. The centerpiece is APC: two queued calls sit unexecuted through a 609 ms non-alertable wait; on entering SleepEx(TRUE) both run FIFO within the same tick and the wait returns early with 192 — APC reuses the original thread, in contrast with console events' fresh threads. QueueUserAPC nudges a stuck alertable wait awake in 407 ms with WAIT_IO_COMPLETION; the non-alertable control cannot be nudged, and backlogged APCs die voided with the thread. Closing with a self-pipe mirror whose handler only calls SetEvent, a four-row comparison table, and Ctrl+C demoted to the byte 0x03 once ENABLE_PROCESSED_INPUT is switched off."
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "结构化异常:SEH 与 VEH"
  - "进程与作业:CreateProcessW 与 Job 对象"
related:
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "共享内存:页面文件后备的命名映射对象"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 并发
  - 异步编程
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/process/02-console-apc.md
  source_hash: dd6414c9238c2f878e3e349ae8519e2df56272506bc272c4a7aa1e3c32f1df9c
  translated_at: '2026-11-22T15:43:55+00:00'
  engine: glm-5.3
  token_count: 20951
---
# Console Events and APC

At the close of [SEH and VEH](../file-io/03-seh-veh.md) (W03 hereafter) we left a scene behind: we kept both VEH and a console handler on duty, fired an event, and VEH's dispatch count stayed at zero while the ctrl handler ran — **on another thread**. Only half an explanation was given at the time: console events travel by way of a new thread and never enter the exception dispatcher at all; the details were left to the console chapter. This article pays that debt. It continues directly from [the previous one](01-createprocess.md): there we governed how processes are created and herded under job objects; here we govern another of their possible endings. That Ctrl+C you press on the keyboard — which road does it take inside Windows, and by what right does it become a function call?

We have to ask this question from zero, because Windows' dictionary never contained SIGINT to begin with. The Ctrl+C we know from Linux is a kernel-delivered signal, with the handler bound by an async-signal-safe whitelist (POSIX's list of legal actions for signal handlers — which functions may be called inside a handler and which may not; it lives in the man pages' signal-safety page). Windows delivers no signals; when an event happens, the console subsystem calls the functions the process has registered. We call this arrangement **console control events**, and the registration function is this article's lead actor: `SetConsoleCtrlHandler`. We also pick up another thread W03 left hanging: MinGW's `signal(SIGINT)` is the CRT's simulation layered on top — whether it sits on the same foundation we never took apart; this article uses the real thing directly. The second lead we invite is APC (short for Asynchronous Procedure Call: another flow of execution enqueues a function into the target thread's queue, and the target thread consents to run it only at particular moments). It has no direct relation to processes; we invite it because it spreads out the same problem the console events do: **how the system stuffs a piece of code into a thread that is busy with something else**. Both storylines close on the same engineering answer: inside the handler you do nothing but wake someone, and the handling is left entirely to the awakened side.

Environment and numbering conventions, stated up front as usual. The experiments are numbered e1 through e7, in one-to-one correspondence with the seven directories 01 through 07 under `code/volumn_codes/vol8/systems-programming/windows/process/02-console-apc/` in the repository; the tags in program output read [E1] to [E7], in the body we call them e1 to e7, independent of every earlier article's e-series — match them by directory name and you cannot mistake one for another. The machine is still the author's Win11 26200; the compiler is MSYS2 UCRT64 g++ 16.1.0; the compile command is uniformly `g++ -std=c++20 -Wall -Wextra`, with a final warning count of 0; outputs were captured on 2026-10-04. We run by way of WSL interop invoking the `.exe` directly, with stdio attached to pipes and the process connected to a windowless console — a quirk of the startup chain that a dedicated section below pursues. The experiment programs are self-contained little things: `windows.h` plus `cstdio` and they are complete. W01's `check_win32` and the thinking-cornerstone articles' `unique_handle` are not invited onto the stage this time; if you want to copy a single experiment away to reproduce it, you need not drag along any common headers.

What the interop environment can and cannot test, we also state honestly. Testable are three things: both event kinds of `GenerateConsoleCtrlEvent`; `WriteConsoleInput` injecting keystrokes into CONIN$ (CONIN$ is the device name of the console input buffer, used exactly like opening a file); and a real conhost window pulled up via `start /wait` — conhost is the console window's host process. Not testable is a real keyboard Ctrl+C: our environment has no human sitting at the console pressing keys, and keys injected in measurement are not converted to signals, so the keyboard-ingestion path went untested. Deliberately untested is the SendInput route of simulating real keys — it would type into whatever window is in the foreground, with a non-trivial risk of disturbing your session, so we abstained. As for the remaining CTRL_CLOSE, CTRL_LOGOFF, and CTRL_SHUTDOWN events, whose triggers are respectively closing the window, logging off, and shutting down: we likewise recorded only the documented scenarios, as staging them here is beyond us.

## SetConsoleCtrlHandler: Last Registered, First Called

We open by assembling a handler chain. We pick four functions — call them Z, A, B, and C — and register them in the order announced: Z registers earliest and takes the fallback job of returning TRUE; C registers last. e1's stage is a single process, and it plays six scenes in a row — chain order, interception, unregistration, the ignore bit — all in one viewing:

```text
$ ./e1_handlers.exe
[E1] pid=12420 main_tid=18328
[1] 链 Z,A,B,C 全 FALSE(C 先 FALSE,Z 兜底 TRUE),发 CTRL_BREAK_EVENT
    [1] 调用链: C(FALSE传递) -> B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[2] C 改返回 TRUE:同一条链再发 CTRL_BREAK,期望链在 C 截断
    [2] 调用链: C(TRUE拦截)
[3] SetConsoleCtrlHandler(C,FALSE) 注销 C -> 1,再发,期望 B->A->Z
    [3] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[4] 现在发 CTRL_C_EVENT —— 本进程由 WSL interop 链启动,忽略位被设上,期望空
    [4] 调用链: (空 —— 没有任何 handler 被调)
[5] SetConsoleCtrlHandler(NULL,FALSE) 复位忽略位 -> 1,再发 CTRL_C,期望 B->A->Z
    [5] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[6] SetConsoleCtrlHandler(NULL,TRUE) 显式忽略 Ctrl+C -> 1:发 CTRL_C 期望空;
    [6-CTRL_C] 调用链: (空 —— 没有任何 handler 被调)
    但紧接着发 CTRL_BREAK —— BREAK 不受忽略位限制,期望 B->A->Z
    [6-CTRL_BREAK] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[E1] done
```

Scene [1] is the whole of the chain order: registered in the order Z, A, B, C, yet called in the order C, B, A, Z — the one registered latest is called earliest. We invite the documentation's original words out: `its handler functions are called on a last-registered, first-called basis until one of the handlers returns TRUE` — the late registrants stand at the head of the line and are asked on down until someone returns TRUE. When all return FALSE and the chain's tail arrives with nobody claiming the event, the default handler steps in to close the show, and its identity the documentation also states plainly: `a default handler function that calls the ExitProcess function` — the process exits outright. We had Z return TRUE as the fallback precisely so the default handler would not carry off the experiment's process.

Scene [2] pins down TRUE's semantics. C returned TRUE, and the system stopped asking further: B, A, and Z never even got the chance to run. That is what we mean by "handled": TRUE's semantics are truncation — it is not some success code. Scene [3] performs the unregistration: one `SetConsoleCtrlHandler(C, FALSE)` struck C off the chain; firing the event again, the chain had become B, A, Z. One more documentation rule we record verbatim: `Calling AttachConsole, AllocConsole, or FreeConsole will reset the table of control handlers in the client process to its initial state` — reattach a console and the whole handler table resets to its default; after switching consoles you must register anew.

Scenes [4] through [6] watch the same character working transformations. When `SetConsoleCtrlHandler`'s first parameter is NULL, it governs no chain but a process attribute: TRUE ignores Ctrl+C; FALSE restores normal processing. In [4] the CTRL_C went out and the chain came back empty — not one line of handler did we see. This oddity owes an explanation, and the debt is noted. Come [5], a single `SetConsoleCtrlHandler(NULL, FALSE)` turned the tables, and CTRL_C walked the full B, A, Z chain. [6] set the ignore back explicitly; CTRL_C again met an empty chain, but the CTRL_BREAK fired immediately after walked the full chain as usual. So the ignore bit has a precise boundary: **it exempts CTRL_C only — BREAK is not under its jurisdiction** — and this is precisely the foundation the later process-group experiments stand on. Who set this bit, and why processes started by WSL carry it, we pursue in a dedicated section after the process groups.

The handler receives more than two kinds of events. Of the five, we measured only the two `GenerateConsoleCtrlEvent` can produce; the last three cannot be triggered on this machine, and we transcribe their scenarios from the documentation:

| Event | Value | Trigger scenario | Status in this article |
| --- | --- | --- | --- |
| CTRL_C_EVENT | 0 | Keyboard Ctrl+C, or GenerateConsoleCtrlEvent | Measured (subject to the ignore bit) |
| CTRL_BREAK_EVENT | 1 | Keyboard Ctrl+Break, or as above | Measured, always deliverable |
| CTRL_CLOSE_EVENT | 2 | The user closes the console window | Documented only, not measured |
| CTRL_LOGOFF_EVENT | 5 | The user logs off; the documentation says only service processes receive it | Documented only, not measured |
| CTRL_SHUTDOWN_EVENT | 6 | System shutdown; the documentation says processes that loaded gdi32/user32 are skipped | Documented only, not measured |

The last three also carry timeout stories, likewise from the documentation's Timeouts table: CTRL_CLOSE gives the handler about 5000 milliseconds (the system parameter SPI_GETHUNGAPPTIMEOUT); CTRL_LOGOFF and CTRL_SHUTDOWN sit at the same 5000-millisecond tier; a service process's SHUTDOWN widens to 20000 milliseconds; while CTRL_C and CTRL_BREAK have no timeout — the system does not rush you. We cannot verify the timeout numbers by closing windows on this machine, so they are recorded honestly as the documentation's word.

What may safely be done inside a handler we examine before Linux's mirror. The console handler runs on a new thread and cannot preempt at arbitrary instruction boundaries, so its constraints are naturally looser than a signal handler's. Loose as the Windows side is here, the documentation still presses down two hard constraints. The first, in its original words: `Console functions, or any C run-time functions that call console functions, may not work reliably` — during CLOSE, LOGOFF, and SHUTDOWN, the console's internal cleanup may already be running ahead of you, and touching console functions inside the handler is no longer dependable. The second is the timeouts above: you are granted no leisurely closing. Stack the two together, and the engineering answer is fully isomorphic to Linux's self-pipe: inside the handler, perform the minimal action and hand every decision and every IO back to the main thread. The executable version of that answer is delivered in e6 at the close.

## Every Event, a Fresh Thread: The Delivery Model and the Default Death

W03 proved only that the handler runs **on a new thread**; two questions remain unsettled: does each event reuse the same thread, or does each spin up its own? And what becomes of the thread after the handler returns? Segment A of e2 aims straight at them. The main thread fired CTRL_C twice in a row; we noted each handler's thread id, then probed each with `OpenThread`:

```text
$ ./e2_delivery.exe
[E2] pid=26116 main_tid=26004
[A1] 连发两次 CTRL_C:两次 handler 的 tid 各是多少?和主线程比?
    [handler] type=0 tid=30380
    [handler] type=0 tid=11700
[A2] OpenThread 探测 handler 线程是否还活着(活着的线程应能打开):
    对照:活着的 worker tid=25620 -> OpenThread=00000000000000d0(应非空)
    handler tid#1=30380 -> OpenThread=0000000000000000 gle=87(NULL=线程已不存在)
    handler tid#2=11700 -> OpenThread=0000000000000000 gle=87(NULL=线程已不存在)
[A1/A2 小结行见存档,略]
```

With three pieces of evidence laid before us, the answers are complete. The two events' tids were 30380 and 11700 — neither a reuse of the other, and neither the main thread's 26004 — so **every event spins up a fresh thread of its own, and old ones are never reused**. The `OpenThread` probe exists to ask "does the thread still exist": the living control worker opened to a handle on the first try, while both handler threads returned NULL with gle=87 (gle is the error code GetLastError reports; 87 is ERROR_INVALID_PARAMETER) — they no longer exist. The moment the handler function returns, the system's thread is withdrawn, without even offering reuse. The documentation has a direct sentence on this, original words: `When the signal is received, the system creates a new thread in the process to execute the function`. The no-coalescing-of-bursts matter gained its evidence along the way: two CTRL_C firings bought two handler invocations — not one fewer did we see — which pairs with Linux's behavior as mutual counter-examples. Over there, firing the same signal number three times during a blockage records only one pending bit; after unblocking, the handler runs once, with the delivery point landing precisely at an instruction boundary between two output lines; that experiment is likewise archived under `code/volumn_codes/vol8/systems-programming/linux/process/04-signal-basic/`.

And what is the fate of a process with no handler? Segment B of e2 brought in the cannon fodder: a child process sharing the parent's console, same group, registering no handler at all, resetting the inherited ignore bit at birth. The parent fired the event, and we timed the aftermath:

```text
[B1] 炮灰就绪,父进程发 CTRL_C_EVENT(炮灰无 handler,只能走默认 handler)
    [handler] type=0 tid=12768
    炮灰 94ms 内死透:exit code=0xC000013A(-1073741510)  [STATUS_CONTROL_C_EXIT=0xC000013A=1]
[B2] 炮灰就绪,父进程发 CTRL_BREAK_EVENT(炮灰无 handler,只能走默认 handler)
    [handler] type=1 tid=23076
    炮灰 93ms 内死透:exit code=0xC000013A(-1073741510)  [STATUS_CONTROL_C_EXIT=0xC000013A=1]
    父进程自己带着 handler 返回 TRUE,两次广播都活着 —— 这就是对照
(炮灰就绪两行与 [E2] done 行在存档,略)
```

Both event kinds delivered the same destination before our eyes: the cannon fodder was dead within 94 and 93 milliseconds, both exit codes reading 0xC000013A. That number is winnt.h's `STATUS_CONTROL_C_EXIT` — the parameter the default handler personally fills into `ExitProcess`, and the landing point of the Windows world's "default SIGINT action." The two sides' bookkeeping parts ways here: on Linux the kernel executes the default action and the reaped wait status records a signal death, with the shell seeing 128 plus the signal number. Windows writes the same meaning into the process's exit code, visible to whoever holds the handle. Two conventions to mind when reading the full code: the parent's `GetExitCodeProcess` reads -1073741510, and `cmd /v:on`'s `!ERRORLEVEL!` reads the same number — the program doing that work is archived under the file name victim_selfsend. WSL's `$?` keeps only the low 8 bits and cannot carry the full exit code; do not be fooled by it when you reproduce.

The event-firing `GenerateConsoleCtrlEvent` deserves a separate introduction; it takes just two parameters. The event kind is CTRL_C_EVENT or CTRL_BREAK_EVENT; a group id of 0 means broadcast, delivered to every process sharing this console, while a concrete group id means targeted delivery, reaching only the specified group. We have used broadcast many times already; targeted semantics needs process groups before it gets interesting — the next section builds the groups.

## Process Groups: The Immunity Mechanism Is That Ignore Bit

The term process group is familiar to all of us from Linux: a set of processes to which signals can be delivered as a group. Windows has a concept by the same name, built by passing `CREATE_NEW_PROCESS_GROUP` to CreateProcessW; the new process's pid doubles as its group id. e3 sets a three-player stage: parent process P, same-group child A, and new-group child B. A resets its own ignore bit after birth — a normal Ctrl+C-receiving process — while B touches nothing, keeping the new group's built-in attribute exactly as it came. All three handlers return TRUE to keep the default handler out of the play, and printing is serialized through a cross-process named mutex. The six scenes run in this order:

```text
$ ./e3_group.exe
[E3] 父进程 pid=15788
[1] 父发 CTRL_C(0):广播到共享本控制台者 —— 期望 P、A 响应,B 静默(免疫)
    [P handler] type=0 tid=11608
    [A handler] type=0 tid=18660
[2] 父发 CTRL_BREAK(0):BREAK 不受忽略位限制 —— 期望 P、A、B 全响应
    [A handler] type=1 tid=5820
    [B handler] type=1 tid=23888
    [P handler] type=1 tid=24640
[3] 通知 B 自己解除免疫(NULL+FALSE)
  [child-B] 已 SetConsoleCtrlHandler(NULL,FALSE):免疫解除
[4] 父发 CTRL_C(0, 定向 B 组 pgid=14604):文档口径"返回成功但收不到" —— 实测:
    [B handler] type=0 tid=18328
    返回值=1,下面有没有人响应?
[5] 父发 CTRL_BREAK(1, 定向 B 组 pgid=14604):能定向 —— 期望只有 B 响应(P/A 不动)
    [B handler] type=1 tid=25872
[6] 父再发 CTRL_C(0) 广播:B 已解除免疫 —— 期望 P、A、B 全响应
    [B handler] type=0 tid=21616
    [A handler] type=0 tid=11020
    [P handler] type=0 tid=19108
  [尾] A exit=0  B exit=0(都 0 = 免疫是「不投递」,不是「杀掉」)
(子进程就绪的四行、[1] 幕的括注行、[5] 的返回值行与子进程收场两行在存档,略)
[E3] done
```

We walk the six scenes in turn. Scene [1] broadcasts CTRL_C: P and A both responded, while B printed not one line — the new group is immune to broadcast Ctrl+C. Scene [2] switches to a CTRL_BREAK broadcast: all three rang — B's immunity does not block BREAK. Scene [3]: B received the parent's notification — carried by a named event, not a console event — and itself called `SetConsoleCtrlHandler(NULL, FALSE)`; the immunity was lifted. When scene [6] broadcasts CTRL_C again, all three ring once more: B is now indistinguishable from an ordinary process.

What is the mechanism of the immunity? The Remarks of the CreateProcess documentation carry an original sentence that joins both matters in one breath: `an implicit call to SetConsoleCtrlHandler(NULL, TRUE) is made on behalf of the new process` — at the new process's birth, the system implicitly calls NULL plus TRUE on its behalf. You recognize it: this is exactly the call we performed by hand in e1's scene [6]. So **the new group's immunity is not an independent switch — it is that ignore bit**: the same bit, inherited by child processes the same way, liftable by the same single NULL plus FALSE; e3's scenes [3] and [6] are the measured proof of the lifting and of what follows. The immunity was prepared for shells; the documentation's next original sentence is `This lets shells handle CTRL+C themselves, and selectively pass that signal on to sub-processes` — the shell opens a new group to shield its children and keeps Ctrl+C for itself; when we want a particular group interrupted, we target the BREAK at it, and the scenario of a background service isolating itself from Ctrl+C rides the same switch.

Scene [4] is one place this batch of experiments picked a fight with the documentation. GenerateConsoleCtrlEvent's parameter table, in its CTRL_C entry, writes `cannot be limited to a specific process group`, and with a nonzero group id, `this function will succeed, but the CTRL+C signal will not be received by processes within the specified process group` — the call succeeds, but the group's processes receive nothing. Measurement says otherwise: B, its immunity already lifted, received the targeted CTRL_C; the type=0 handler genuinely ran. We re-ran two rounds; the result held.

::: warning The documentation and measurement disagree; we go with measurement
GenerateConsoleCtrlEvent's parameter-table entry claims a nonzero-group-id CTRL_C "succeeds but is not received"; on the author's Win11 26200, group B with immunity lifted actually received the targeted CTRL_C, consistently across two rounds. If you write portable code, rely on neither side: whether a targeted Ctrl+C arrives should be settled by measurement on your target machine. Between the documentation and the machine in our hands, one of them is not to be fully trusted.
:::

The [tail] line also characterized the immunity: A and B both closed normally with exit code 0. The immunity does exactly one thing — non-delivery — leaving the process alive and unharmed; lift it when you want delivery. One more look at scene [5]: the targeted BREAK reached B alone; P and A printed not one line. The targeting is genuinely targeted — not half a word splashed out.

## The interop Startup Chain's Ignore Bit: Even a Real Window Doesn't Escape

Now we return to tidy up the oddity e1's scene [4] owes us: why did CTRL_C meet an empty chain in a process started under WSL? Before setting to work we ran a census — `00-env-probe` ran each of three launch methods:

| Launch method | What the console looks like | CTRL_C event | CTRL_BREAK event |
| --- | --- | --- | --- |
| Direct WSL run | No window; stdin/stdout are pipes; CP=936 (GBK code page) | Returns 1; handler 0 times | Delivered; handler once |
| cmd.exe /c bridge | Same as above | Likewise 0 times | Delivered |
| start /wait real window | A window exists; stdin is a character device | Likewise 0 times | Delivered |

We ran all three methods; in the real-conhost-window round, GetConsoleWindow was no longer NULL and stdin was a true character device — and the CTRL_C handler still ran 0 times. We even checked the input buffer's mode: `ENABLE_PROCESSED_INPUT` was already on; we forced it once more and fired again, and still got 0 calls. So the reason the event cannot get through lies neither in the window nor in the console's input mode.

We dug the root cause out of the mechanism documentation. SetConsoleCtrlHandler's parameter notes carry an original sentence: `This attribute of ignoring or processing CTRL+C is inherited by child processes` — the attribute of ignoring or processing Ctrl+C is inherited by child processes. We have no API to query this bit, so "the WSL interop startup chain set it" is an attribution reverse-engineered from behavior; the evidence is that it is the only self-consistent explanation along the inheritance chain, and it squares with e1's scenes [4] and [5]: scene [4]'s empty chain was the inherited ignore bit blocking the way, and scene [5]'s single reset line put CTRL_C straight onto the full chain. e2's cannon fodder was the same: only after resetting could it receive CTRL_C and die — and that reset was, in itself, one more demonstration of the immunity mechanism.

One sentence from W03 we must also retract. At its close the author wrote, "to reproduce this, you must work inside a real console window." This batch of experiments overturns that sentence — a real window receives nothing either. The original attribution guessed at the bridging layer; measurement cleared it of charge, and the root cause landed on the inherited ignore bit. The author hereby formally retracts that attribution: this article's interop section is the evidence, and the corrections to W03's body and its archived README will be applied in one pass once this batch is consolidated — if you have read that piece, until then please take this article's account as authoritative.

## APC: Asynchronous Calls That Reuse the Original Thread

We have mapped the console-event approach: the system spins up a new thread to run your handler. Windows has a second road for stuffing code into a thread, and its approach is the exact opposite — it starts no new thread and borrows the target thread itself. APC's entry point is `QueueUserAPC`; we bring out the documentation's mechanism passage whole, as it deserves your sentence-by-sentence reading: `Each thread has its own APC queue. The queuing of an APC is a request for the thread to call the APC function. The operating system issues a software interrupt to direct the thread to call the APC function.` — every thread has its own APC queue; queuing an APC means "requesting the target thread to call this function," and the order-giver is the operating system's software interrupt. The word "request" is the entire crux: when your queued function runs is not under your authority — it rests in the target thread's hands, and what it must step into is an **alertable wait** (the documentation's name for bAlertable=TRUE-style waits, meaning the thread, while waiting, is willing to be interrupted to drain its APC queue; `SleepEx`, `WaitForSingleObjectEx`, and `WaitForMultipleObjectsEx` with TRUE all count).

We had e4 photograph the time gap between queuing and execution with stamped timing. The worker's first sleep is `SleepEx(600, FALSE)` — a non-alertable stretch — and on waking it immediately enters `SleepEx(5000, TRUE)`, an alertable one. The two APCs were queued by the main thread before the worker's first sleep:

```text
$ ./e4_apc.exe
[E4] main tid=8040
[worker] tid=1320 就绪,先卡在 WaitForSingleObject(start)(不可警告)
[main] t=91861281 两条 APC 排完(q1=1 q2=1):worker 正卡在不可警告等待,一条都没跑
[worker] t=91861281 进入 SleepEx(600, FALSE) —— 不可警告睡眠 600ms
[worker] t=91861890 醒来(ret=0),此刻 APC 执行数=0(排队≠执行,1 的证据)
[worker] t=91861890 进入 SleepEx(5000, TRUE) —— 可警告睡眠:队列里的 APC 立刻开闸
    [APC#1] t=91861890 tid=1320 arg=111(与 worker tid 同 = 复用原线程)
    [APC#2] t=91861890 tid=1320 arg=222
[worker] t=91861890 返回 ret=192(WAIT_IO_COMPLETION=192),实际只睡了 609ms,APC 执行数=2
[E4] done —— 读输出顺序:APC#1 恒在 APC#2 前(队列 FIFO);
      两次 APC 的 tid == worker tid(对照控制台事件的【新】线程,APC 复用原线程)
```

Lay the timeline out. At t=91861281 the two APCs entered the queue; the worker then slept its non-alertable 609 milliseconds, and at t=91861890 awoke with the APC execution count at 0 — not one queued function had run. The next line is the whole play: the worker stepped one foot into `SleepEx(5000, TRUE)`, and APC#1 and APC#2 ran back-to-back inside the same tick, the earlier-queued one running first — exactly the queue's FIFO rule. The 5-second wait never slept to full term; it returned early carrying 192. 192's identity is `WAIT_IO_COMPLETION`, whose value is 0xC0 and whose other name in the NTSTATUS system is STATUS_USER_APC. IO sits in the name, but what ran was the APC drain; the documentation uses exactly this code for the return of APC waits, and we recognize it accordingly. The documentation wrote our whole timeline into principle, original words: `When a user-mode APC is queued, the thread is not directed to call the APC function unless it is in an alertable state. After the thread is in an alertable state, the thread handles all pending APCs in first in, first out (FIFO) order, and the wait operation returns WAIT_IO_COMPLETION` — no execution outside the alertable state; upon entering it, the queue drains in queuing order and the wait returns early with WAIT_IO_COMPLETION. Word for word, it matches our timestamps.

The two APCs' tid was 1320 — the same as the worker's own. The fundamental difference between APC and console events lands on exactly this contrast of evidence: a console event **spins up a new thread** to run for you — e2's two handler tids differed from the main thread's — while APC **reuses the original thread**: the function runs on the target thread's own stack, inside its own synchronization context. The initiative splits accordingly: the console event's thread is the system's, and the handler runs whenever the system pleases; APC must wait for the target thread to walk itself into an alertable wait — you may queue, but if it never sleeps alertably, all you can do is wait with a dry mouth. One more crossing confirmation sits at the system's bottom layer: while a process is being debugged, Ctrl+C becomes an exception named DBG_CONTROL_C; the documentation says it is shown to the debugger alone, and once the debugger has handled it, the debugged process never notices the CTRL_C — with one appended original clause: `an application will not notice the CTRL+C, with one exception: alertable waits will terminate`. The sole exception is that alertable waits get cut short. The two mechanisms — alertable waits and console events — are hooked together at the system layer.

APC's most proper use, e5 delivered in a matched pair of scenes: gently waking a thread stuck inside a wait. w1 is stuck in `WaitForSingleObjectEx`'s alertable wait on a handle that will never signal; after 400 milliseconds, main queues one APC for it. w2 plays the control, stuck in a non-alertable `WaitForSingleObject`, and we queue one APC for it just the same:

```text
$ ./e5_apc_interrupt.exe
[E5] main tid=30648
[1] w1 卡在可警告等待;main 睡 400ms 后 QueueUserAPC(wake_w1, reason=42)
[w1] tid=16944 卡进 WaitForSingleObjectEx(never, INFINITE, /*alertable=*/TRUE)
    [w1 的 APC] t=91862375 在 w1 原线程里跑,reason=42 —— 它把等待掐断
[w1] t=91862375 提前返回 ret=192(WAIT_IO_COMPLETION=192),原计划等到世界末日,只撑了 407ms
[w1] 收尾返回 0 —— 自己退的,没人 TerminateThread
    main 观察:w1 join=0(0=回来了),线程退出码=0,reason_w1=42(42=APC 真跑了)
[2] 对照 w2 卡在【不可警告】等待;main 排 APC(reason=7)再观察 800ms
[w2] tid=6800 卡进 WaitForSingleObject(never, INFINITE) —— 不可警告
    800ms 后:w2 状态=258(258=STILL_WAITING 还卡着),reason_w2=0(0=APC 没跑)
[w2] t=91863578 醒了 ret=0:唤醒我的是 SetEvent,不是 APC(g_reason_w2 仍是 0)
    SetEvent 后 w2 join=0 退出码=0,reason_w2=0(仍 0:线程退出时积压 APC 作废)
[E5] done —— APC 只掐得动可警告等待;线程没进过可警告点就退出,排进去的 APC 白排
```

As we see it, w1's script is textbook: the APC ran in its original thread, the wait was cut short by it, and at 407 milliseconds it came back early carrying 192 — cleanup done, checks done, `return 0` done, exit code spotless, without a shadow of TerminateThread anywhere. For nudging a stuck thread awake, APC is the only gentle external means, with the precondition written in the script's first line: the thread must be waiting alertably. w2's control delivered two negative results, each more piercing than the last: through the 800-millisecond observation window w2 never budged — the queued APC could not enter execution; after SetEvent released it, w2 woke and exited, and the backlogged APC never ran once — voided outright. The voiding has the documentation's original backing: `When the thread is terminated using the ExitThread function or the TerminateThread function, the APCs in its APC queue are lost` — the moment the thread exits, the whole queue is lost. So before queueing an APC to a thread, please confirm its loop truly cycles through an alertable wait; otherwise the queueing is for nothing.

One more sign that the APC mechanism lives closer to us than it seems: the documentation names a string of old acquaintances — `ReadFileEx`, `SetWaitableTimer`, `WriteFileEx` — whose completion notifications, in the original words, "are implemented using an APC as the completion notification callback mechanism." The work of bringing them back onstage belongs to the chapter on I/O multiplexing and asynchronous I/O along [the overview](../../00-overview.md)'s route; the completion routines you will meet there run on exactly today's machinery.

## Only SetEvent Inside the Handler: Comparing With self-pipe

With all those constraints told, our executable answer is a single handler. We do not printf inside it, we touch no console; its entire job is one `SetEvent`, and every decision and every IO goes back to the main thread:

```cpp
// e6_setevent_wake.cpp(节选):handler 的全部工作;计数器一行(输出里"共被调 N 次"的来源)略
static HANDLE g_wake;

static BOOL WINAPI handler(DWORD) {
    SetEvent(g_wake);   // 全部工作就这一句;不 printf、不碰控制台
    return TRUE;
}
// 主线程:WaitForSingleObject(g_wake, 5000) 上等,醒来后的活儿全在这边做
```

```text
$ ./e6_setevent_wake.exe
[E6] main tid=17924
[1] main 进 WaitForSingleObject(wake, 5000),然后自己给自己发 CTRL_C_EVENT
    79ms 后醒:ret=0(0=句柄有信号)handler 共被调 1 次 —— 它只干了 SetEvent
[2] main 进 WaitForSingleObject(wake, 5000),然后自己给自己发 CTRL_BREAK_EVENT
    78ms 后醒:ret=0(0=句柄有信号)handler 共被调 1 次 —— 它只干了 SetEvent
[E6] done —— 事件线程只 SetEvent,决策与 IO 全回主线程:与 self-pipe 同构
```

The two wakeup rounds clocked 79 and 78 milliseconds, the main thread waking cleanly off `WaitForSingleObject`. If you have read the Linux-side signals article, the structure here will look familiar: the handler's action is minimal — over there, the practice writes one byte into a pipe and has the main loop's poll read it back, under the name self-pipe. The two sides' main lines are fully isomorphic; we lay out four rows of comparison, with the Linux-side evidence archived in the 04 and 05 directories under `code/volumn_codes/vol8/systems-programming/linux/process/`:

| Dimension | Linux (signals) | Windows (console events) | Evidence |
| --- | --- | --- | --- |
| Delivery model | Any unblocked thread, inserted at instruction boundaries; a burst during a blockage records only once | Each event spawns a fresh thread to call the handler; a burst spawns one each | 04-signal-basic/01 vs. e2 |
| What the handler may do | The async-signal-safe whitelist; printf measurably corrupted 21 frames (a frame being a splintered output line) | Runs on a new thread, constraints loose; but during CLOSE/LOGOFF/SHUTDOWN console functions are unreliable and time-limited — the right answer is only SetEvent | 04-signal-basic/03 vs. e1 and e6 |
| Waking the main loop | self-pipe: write(2) in the handler, poll in the main loop | SetEvent in the handler; the main thread's WaitForSingleObject wakes in about 79 ms | 04-signal-basic/05 vs. e6 |
| Turning events into waitable objects | signalfd/pidfd turn signals into fds, unified into the event loop | The console input handle can be waited on directly; the WaitFor* family uniformly waits on everything waitable | 05-signal-advanced/02, 03 vs. e7 |

The last row's Windows evidence comes from e7's quick tour, which also carried one more matter along: Ctrl+C's signal identity is a switch granted by the console input mode. The input buffer's initial mode is 0x1F7 with the `ENABLE_PROCESSED_INPUT` bit on, and Ctrl+C travels as a signal. We switched it off and injected Ctrl+C: the handler's call count was 0, `WaitForSingleObject(CONIN$, 1500)` returned 0 — the buffer held goods — and `ReadFile` actually read one byte, whose value was 0x03. Ctrl+C was demoted from signal to ordinary input byte, and the documentation's word — `CTRL+C is reported as keyboard input rather than as a signal` — landed on a byte. The fact that the console input handle enters the WaitFor family fills in the comparison table's last row along the way. A full unfolding of console modes we leave to the not-yet-written terminal-and-console chapter; what stays behind here is this one byte.

And herewith the two articles of the process chapter's Windows side are complete. From chain order, fresh threads, exit codes, and the ignore bit to every link of process groups, we have archived measurements. APC's story has only opened — the completion routines of the ReadFileEx family still wait in the asynchronous-I/O chapter along the overview's route, and we will invite them out of the queue when we get there.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="SetConsoleCtrlHandler function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler"
  />
  <ReferenceItem
    :id="2"
    title="HandlerRoutine callback function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/handlerroutine"
  />
  <ReferenceItem
    :id="3"
    title="GenerateConsoleCtrlEvent function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent"
  />
  <ReferenceItem
    :id="4"
    title="CreateProcessA function(CREATE_NEW_PROCESS_GROUP)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa"
  />
  <ReferenceItem
    :id="5"
    title="QueueUserAPC function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc"
  />
  <ReferenceItem
    :id="6"
    title="SleepEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex"
  />
  <ReferenceItem
    :id="7"
    title="WaitForSingleObjectEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobjectex"
  />
  <ReferenceItem
    :id="8"
    title="SetConsoleMode function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolemode"
  />
  <ReferenceItem
    :id="9"
    title="signal-safety(7) — Linux man-pages"
    publisher="man7.org"
    url="https://man7.org/linux/man-pages/man7/signal-safety.7.html"
  />
</ReferenceCard>
