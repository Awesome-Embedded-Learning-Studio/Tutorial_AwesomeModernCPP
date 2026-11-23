---
title: "Processes and Jobs: CreateProcessW and the Job Object"
description: "First article of the Windows-side process chapter: a measured dissection of CreateProcessW's ten parameters — only lpApplicationName=NULL walks the full search chain while a relative name resolves against the CWD alone (err=2); an unquoted path containing spaces plus a planted decoy named with.exe reproduces the Program.exe attack; with no decoy present, the system splices the full token and rewrites the child's command line; argv[0] is decoupled from the actually loaded path; with the STARTUPINFOEX handle whitelist, inheritable handles outside the list never enter the child at all; CREATE_SUSPENDED gives a two-phase suspended birth — write the token, then ResumeThread (prev=1) — and closing hThread leaves the process running; lpEnvironment replaces the whole block, and leaving out PATH means the child never gets up (0xC0000135). Waiting and exit codes: after CloseHandle the process still lives (a handle is only the right to observe); return 42, abort's 0xC0000409 fail-fast, and TerminateProcess passed through verbatim; the STILL_ACTIVE=259 trap; WFMO examined from three angles against waitpid. A four-ways-to-die cleanup matrix (return walks all three paths; a direct ExitProcess skips atexit yet the dynamic UCRT still flushes the buffers, with Terminate as the counter-evidence; TerminateProcess skips all three; CTRL_BREAK exits gracefully). The Job object: with KILL_ON_JOB_CLOSE, both the explicit handle close and the parent's exit execute everyone within timer resolution while the control group's orphans live on; a Job handle inherited by the child disables the collective death; QueryInformationJobObject statistics; a 40 MB limit yields err=1455. Nesting and breaking away: a child born inside a Job joins the same Job automatically; breakaway requires both the allow flag and the creation flag; forcing it collects err=5; silent breakaway sheds membership with no flag at all; a parent Job's 48 MB caps a child Job's 256 MB (the failure point lands well below the parent limit and floats with the process's own footprint). The CRT's on-the-spot argv splitting and the quoted-parsing divergence, wrapped up by a five-row comparison table."
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "共享内存:页面文件后备的命名映射对象"
  - "结构化异常:SEH 与 VEH"
related:
  - "进程创建与生命周期:fork/exec/posix_spawn"
  - "控制台事件与 APC"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 工程实践
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/process/01-createprocess.md
  source_hash: 668aafdcfeb523ecfe823811101e9e92f964b67b3ab24ea6327d5b1feca68469
  translated_at: '2026-11-22T15:39:27+00:00'
  engine: glm-5.3
  token_count: 19880
---
# Processes and Jobs: CreateProcessW and the Job Object

When the memory chapter closed, we left a line behind: we had used the word "process" for two articles without ever properly discussing the process itself. In [Shared Memory](../memory/02-shared-mem.md), `spawn_self` used `CreateProcessW` to pull up another copy of itself under a new command line; that call ran familiar under our fingers, yet we never once examined closely what happened inside it. This article puts it on the dissection table: how the two name parameters divide their labor, how handles are selectively inherited, how the environment block is replaced wholesale — we follow the trail all the way to the process's exit. And after the exit, the second half awaits: one process is easy to manage, but how do you manage a crowd? Windows' answer is the Job object (the mechanism that herds a group of processes into one kernel object for unified management), and it stars in the second half.

The Linux-side contrast forks from the very first line of code. Over there, building a process takes fork plus exec, two steps: one fork call returns twice — the parent receives the child's pid, the child receives 0 at that same return point; one line of code, and the two sides each go home from it. `CreateProcessW` returns only once; when the parent comes back it holds two more handles, and the child is already running at the top of its own `main` — creator and created never once stand in the same line of code. How pid maps to handle, whether the process survives the handle, how the child's death notice gets delivered — the answers to all of these are decided by the fork at the outset, and most of the sections that follow circle back to it. The Linux-side [process creation and lifecycle](../../linux/process/01-fork-exec.md) article covers fork, exec, and waitpid with measurements; that piece is finished, so both sides of this article's comparisons have experiments behind them.

Numbering and environment conventions, stated up front as usual. The experiments here are numbered e1 through e7; the lowercase e-series on the Windows side is independent of W01–W05 and of the memory chapter's two e-series — go by file names and you cannot mistake one for another. e1 dissects `CreateProcessW`; e2 examines waiting and exit codes; e3's subject is the four ways to die; e4's lead is the Job object; e5 covers nesting and breaking away; e7 gathers the command line and argv. Code and raw outputs live under `code/volumn_codes/vol8/systems-programming/windows/process/01-createprocess/` in six subdirectories. e6 is the exception: it has no subdirectory of its own — its content is the material organized into the five-row comparison table, kept in the archive parent directory's README, and we use it at the closing wrap-up. The machine is the local Win11 26200.9457; the compiler is MSYS2 UCRT64 g++ 16.1.0; the compile command is uniformly `-std=c++20 -Wall -Wextra -municode`, all with zero warnings; only e7 adds `-lshell32` at link time, and e3's DLL builds separately with `-shared`. The cross-system way of running things carries over the WSL interop chain introduced in [Win32 File I/O](../file-io/01-win32-file-io.md) (W01); outputs were captured on 2026-10-04. One more piece of background deserves your attention: a Windows process pulled up by WSL interop actually sits inside a Job itself — e5's driver measured `IsProcessInJob(NULL)` returning 1 right at startup. Fortunately, since Win8 a process may belong to multiple Jobs at once, so this does not disturb this batch of experiments; it only means that later, when we interpret who is or is not in a Job, we must remember this layer was already wrapped around them. The common toolkit stays as before: `unique_handle` and `last_error_code` carry over their definitions from the two thinking-cornerstone articles, and `check_win32` carries over W01's definition — we reference, not rewrite.

## Dissecting CreateProcessW: Two Names and One Search Chain

Of the ten parameters, the easiest to confuse are the first two. `lpApplicationName` tells the kernel which file to load; `lpCommandLine` is the whole command line the child sees. The documentation draws the boundary clearly: when both are non-null, the former fixes the load target and the latter the command line, and the two have nothing to do with each other. When `lpApplicationName` is NULL, the module name is taken from the first whitespace-delimited token, and everything after the token counts as arguments. If you have written `execve`, you will find the other side runs a different system: there, path and arguments part ways at the call site, while Windows keeps them in two parameters — and even allows each to tell a different story.

We start with the search chain for a NULL `lpApplicationName`. When the file name carries no path, the system looks for the executable in the documentation's fixed order: the application's own directory, the parent's current directory, the 32-bit system directory, the 16-bit System directory, the Windows directory, and only then the directories in PATH. When the module name comes from the command line and the name carries no extension, the system supplies `.exe` on its behalf. Group (a) of e1 ran exactly this way: the working directory held no fakeecho.exe, and the system followed the chain all the way to the plain directory we had prepared beforehand in PATH, so the child came up. Yet the child's `argv[0]` remained the bare `fakeecho.exe` — the real module location had to be asked of `GetModuleFileNameW`:

```text
(a) app=NULL, cmdline='fakeecho.exe found_via_path'
    [child echo] argv[0]    = fakeecho.exe
    [child echo] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\plain\fakeecho.exe
    -> exit code=0 (0x00000000)
```

Things change temper when we fill `lpApplicationName` with a relative name. The documentation puts it plainly: given a partial name, the function completes it using only the current drive and directory — the documentation's own words, `The function will not use the search path` — and the extension must be carried along; there is no default. Group (b) of e1 called with the same relative name `fakeecho.exe` while the CWD happened not to contain it, and straightaway ate an err=2 — the same name that group (a)'s search found, while group (b) never even touched the edge of PATH. So: if you want the system to search for you, leave app null; if you want the load target pinned down, give the full path. A relative name lands in between, enjoying neither the search chain's care nor freedom — it adds a dependency on the CWD on top of what NULL would have cost.

A path with spaces and no quotes is the most theatrical run of this group. The documentation's security note gives its own example: pass the command line `"C:\Program Files\MyApp -L -S"` unquoted, and the system tries truncated names like `c:\program.exe` and `c:\program files\MyApp.exe` one by one; if a malicious user has placed a `Program.exe` at one of those spots, that is what runs. We reproduced this attack exactly, with a decoy named `with.exe`:

```text
(d) app=NULL, UNQUOTED '<abs with space> x', WITH decoy 'with.exe' planted (= Program.exe attack)
    [child dump] argc=3 argv[0]=C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with argv[1]=space\fakeecho.exe
    [child dump] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with.exe
    -> exit code=1 (0x00000001)
```

The real directory is called `with space`, and the decoy `with.exe` lies one level above it. The system took the first token `...\with`, appended `.exe`, checked — and it really existed — and selected it on the spot; everything left in the command line, `space\fakeecho.exe x`, became arguments. The child loaded the decoy while `argv` still carried half of the original path; even the child itself could not see it had been swapped. Group (d2) removed the decoy and we ran the same line again; this time the system spliced the complete token and found the real body — and did something we had not anticipated: it rewrote the command line the child sees:

```text
(d2) decoy removed, SAME unquoted cmdline
    [child dump] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with space\fakeecho.exe
    [child dump] GetCommandLineW() = "C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with space\fakeecho.exe" x
    -> exit code=1 (0x00000001)
```

The command line we passed in carried no quotes, yet the child's `GetCommandLineW` read back a quoted one. The documentation had actually foreshadowed this; its original words say `CreateProcessW` `can modify the contents of this string`, and the Remarks add that the operating system may prepend the full path to an executable name that lacks one. So what you see in the child's command line is the system's processed version, not the line we wrote down. The engineering fix is simple: either fill `lpApplicationName` with the full path, or honestly quote the command line. Do neither, and who gets loaded depends on who happens to live in the directory.

The decoupling of `argv[0]` from the actually loaded path got verified along the way. Group (c) of e1 set app to the real body's absolute path while writing `TOTALLY_FAKE_ARGV0.EXE` as the command line's first token; the child started up as usual, and `argv[0]` dutifully reported the fake name. What goes to the kernel and what goes to `argv[0]` are two independent sources to begin with; the documentation's advice merely asks C programmers to repeat the module name as the first token — that is convention, not guarantee. On the Linux side, `execve`'s `argv[0]` is likewise whatever the caller writes; the two sides are isomorphic on this line, and in e7 we will see its more extreme form.

## Selective Inheritance: STARTUPINFOEX, Suspended Birth, and the Environment Block

Give `bInheritHandles` TRUE, and every inheritable handle transfers wholesale to the child. When a multi-threaded program has different threads each pulling up children and each passing its own handles, this blanket inheritance turns into a liability. Win32's fine-grained switch lives in STARTUPINFOEX (the extended STARTUPINFO structure, paired with the `EXTENDED_STARTUPINFO_PRESENT` flag and an attribute list): `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` lets us specify a whitelist; handles outside the list cannot get through even with the inheritable bit set. Group [0] of e1 opened two files, both marked inheritable, listed only log_a in the whitelist, and measured on the child's side:

```text
    [child] WriteFile(h_a=232) -> 1 (err=0)
    [child] WriteFile(h_b=236) -> 0 (err=5)
```

log_a got written, while log_b's handle value turned into a dangling number in the child's hand — one write ate err=5 — and reading the file back confirmed log_b was empty. The whitelist does not block the act of inheritance; it blocks handles outside the list from entering the child's handle table at all. We will come back to the whitelist in e4 — the accident where a Job handle leaked into the child's hands has its roots in exactly this handle inheritance.

CREATE_SUSPENDED is another handy tool: the child is born suspended, its main thread frozen at the starting line, and the parent releases it once its plans are settled. Group [2] of e1 used it for a two-phase trick: at suspended birth the token file did not yet exist; while the child was frozen, the parent wrote the token down; then we released it with `ResumeThread` — the function returned a previous suspend count of 1 — and the child woke to read `TOKEN=0xC0FFEE` and exited with 7 as agreed. Races that timing cannot win, suspended birth erases entirely; e4's Job experiments used the same move to eliminate the race of the child sprinting past the Assign and escaping the Job. The same group also divided labor between `PROCESS_INFORMATION`'s two handles: `hThread` was closed as soon as it was used and the process kept running; `hProcess` stayed until the end to wait for the exit code. The thread handle governs that one executor; the process handle governs life, death, and waiting. Each handle returns its own debt, and neither drags the other down.

The environment block has a fiercer temper than the previous two. Give `lpEnvironment` NULL and the child inherits the parent's whole block — in group (a) of section [3], the child counted 46 entries, including our planted `VOL8_E1_MARKER=inherited-from-parent`. Build our own block and pass it in, and the semantics become whole-block replacement — there is no merging: in group (b2) of section [3], after PATH was supplied, the child's environment held exactly 3 entries, all ours; of the parent block's 46, not one came through intact — even the marker survived only as a key, its value displaced by our new one. The harshest was group (b1): a minimal block holding only the marker and SYSTEMROOT, with PATH missing — `CreateProcessW` itself still succeeded, but the child died at the starting line; the exit code read 3221225781, which in hexadecimal is `0xC0000135`, named `STATUS_DLL_NOT_FOUND` — the NTSTATUS saying the loader could not find a DLL. A dynamically linked exe needs PATH even to find the ucrt runtime's DLL; without it, the process never even touches the door of `main`. If you want to tailor the child's environment while keeping PATH, you must carry PATH over item by item yourself — the system does not do it for you.

## Waiting and Exit Codes: A Handle Is Only the Right to Observe

The process is up; what does the parent hold? Two handles and a pid. We begin with the most counterintuitive item: the handle is closed — is the process dead? Group [2] of e2 closed `hProcess`, slept 300 milliseconds, then went to `OpenProcess` with the pid — and opened it on the first try; the process was alive and well. The reopened handle could still wait, and after exit it still collected the child's self-chosen exit code 5. So closing a handle throws away our right to observe, not the process's life; the kernel object lives until the moment its last handle is returned, and only then is it truly destroyed. We used it that way by intuition back in the `spawn_self` days, and now measurement backs it: the pid is the system's numbering table, the handle is our observation right — the two things are simply not attached.

And after the pid is recycled? We ate that once in e4's experiments: the child exits, the pid gets recycled by the system, and opening the old pid with `OpenProcess` reports err=87 (`ERROR_INVALID_PARAMETER`). To observe a grandchild across the generation gap, you must seize the grandchild's handle while its parent still lives and the handle is still valid — one step late, and even the house number has changed owners. The README in e4's archive records the same err=87 note; our three-generation experiments were built on exactly that.

On exit codes, look at group [1] of e2, which laid out three ways to die; the readings are all collected in this table:

| Way of dying | Measured exit code | What we read |
| --- | --- | --- |
| main `return 42` | 42 (0x0000002A) | A clean, self-chosen code |
| `abort()` | 3221226505 (0xC0000409) | A fail-fast-class code (fail-fast: the path where the runtime judges the state unrecoverable and terminates the process outright) |
| Parent `TerminateProcess(h, 4660)` | 4660 (0x00001234) | Whatever the caller supplies gets passed through |

There is little to say about `return 42`. `abort()`'s 0xC0000409 deserves a pause: its name is `STATUS_STACK_BUFFER_OVERRUN`, repurposed on modern Windows as the generic exit code for `__fastfail`-style fast-fail paths. [SEH and VEH](../file-io/03-seh-veh.md) (W03) covered this mechanism's foundation — an exception nobody catches becomes the exit code itself, with the whole 32-bit NTSTATUS handed over. When we re-verified the SEH experiments in W03, we never even saw 0xC0000409's shadow; this time `abort()` delivered it to our door. On the Linux side, a child that dies by `abort()` reads from waitpid as `WIFSIGNALED` plus `WTERMSIG=6` — signal and exit code are two independent dimensions there — while Windows folds the whole thing into one 32-bit exit code. The parent thereby loses the dimension that would tell whether the child died by signal; only a number remains. The `TerminateProcess` row reads even easier: 4660 goes in, 4660 comes out, not even a disguise attempted.

When querying exit codes we also meet a built-in trap. Query before the child exits, and `GetExitCodeProcess` reports no error — it returns 259:

```text
(d) STILL_ACTIVE 陷阱:进程还没退,GetExitCodeProcess 给 259
    queried while running     exit code=259 (259 = STILL_ACTIVE)
    after real exit:             exit code=77          (0x0000004D)  wait=signaled
```

259 is `STILL_ACTIVE`'s sentinel value; the ExitProcess documentation's closing checklist says the process's termination status must be translated out of STILL_ACTIVE into an exit value. Using 259 as a liveness criterion is unreliable — the child may truly have exited and chosen 259 as its exit code. The proper road for liveness is waiting on the handle; we read the exit code exactly once, after the wait.

How do you wait on a crowd of children? Group [3] of e2 demonstrated `WaitForMultipleObjects` (WFMO hereafter) in three uses, with three children sleeping 1800, 500, and 1200 milliseconds, carrying codes 11, 22, and 33. Polling each with `WaitForSingleObject(h, 0)`, the true completion order came out 22, 33, 11, at t+547ms, t+1250ms, and t+1844ms. Then one WFMO with `bWaitAll` FALSE: it blocks until any child is ready, and the returned 0x1 is `WAIT_OBJECT_0+1`, naming by index the first child to finish, with the time landing at t+516ms. Passing TRUE waits for the whole set at once, and we then collected codes one by one in handle order to check the count. These millisecond figures are one round's snapshot; re-runs float by a few to a few dozen milliseconds, with the magnitude unchanged. Set against waitpid it gets interesting: `waitpid(-1)` over there only says one has ended — which one we must ask again — while WFMO's return value hands over the index directly. It can wait on at most `MAXIMUM_WAIT_OBJECTS` objects at a time; beyond that you open threads or switch to IOCP. IOCP's full name is the I/O completion port, Windows' asynchronous completion notification mechanism, which we will deal with again later. The scale of this limit and select's 1024 are the same class of constraint.

## Four Ways to Die: Who Gets a Chance to Say Goodbye

The cleanup-and-farewell part is this article's most rewarding slow read. We arranged the same three tasks for every child in e3: register an atexit cleanup function that writes a log to stderr; `LoadLibrary` a DLL whose `DllMain` carries attach/detach logging; and leave one marker on stdout without a newline and without fflush. The single exception is case four: the CTRL_BREAK child installs a console handler and leaves the closing work to the handler, so no atexit registration was arranged. stdout was redirected by the experiment's driver to a file — fully buffered — so the marker only enters the file at a genuine flush. Then we had the children exit in four different ways, and collected three pieces of evidence together: did the cleanup run, did the DLL receive `DLL_PROCESS_DETACH`, did the buffered marker survive:

| Case | atexit cleanup | DLL_PROCESS_DETACH | stdout marker | Exit code |
| --- | --- | --- | --- | --- |
| `return 0` (CRT exit path) | **ran** | received | 37 bytes, written to the file | 0 |
| Direct `ExitProcess(0)` | **did not run** | received | landed in the file | 0 |
| Parent `TerminateProcess(h, 31337)` | did not run | **not received** | **0 bytes, buffer annihilated** | 31337 verbatim |
| CTRL_BREAK event, handler calls `ExitProcess(5)` | (not registered in this case) | received | landed in the file | 5 (handler's own choice) |

`return` walks the most complete road: the CRT's exit path runs atexit, the loader sends detach to every DLL, and the stream buffers get flushed. One documentation-grade connection lives here: ExitProcess's page says `returning from the main function of an application results in a call to ExitProcess` — main's ordinary return is, underneath, the CRT closing the books for us.

A direct `ExitProcess` is the subtlest tier. The atexit cleanup did not run. atexit registrations live on the CRT's side; a direct `ExitProcess` bypasses the CRT's exit path, so naturally nobody executes what was registered. But the DLL detach went out just the same — ExitProcess's documentation lists it plainly: every loaded DLL's entry point gets called with `DLL_PROCESS_DETACH`. The genuinely surprising part is the buffer: on this dynamically linked UCRT toolchain, the 37-byte marker still reached the file. The mechanism holds together: ucrtbase itself hangs into the process as a DLL, and when it receives detach it does stream cleanup on the way. The reverse evidence comes from the Terminate case: skip detach, and the buffer comes up 0 bytes — the flushing travels with the detach path. We record the claim as measured: on this machine's dynamic UCRT, detach caught it. Whether statically linked CRT or MSVC setups lose the buffer we did not test, and we will not vouch for them — MSVC's /MD tier likewise carries ucrtbase as a DLL, so the same detach mechanism is most likely present, but whether the buffer survives has to be measured on each setup before it counts. If you write logs that must reach the file, do not gamble on this road; fflush when it is time to fflush.

The `TerminateProcess` column is empty across the board. We quote the documentation's characterization verbatim: `used to unconditionally cause a process to exit`; the DLLs' global data state may be damaged as a result. One more blunt sentence, original words: `A process cannot prevent itself from being terminated`. The process receives no notification of any kind; cleanup, detach, and flush are all skipped, and the exit code 31337 passes through verbatim. It is asynchronous too — the function merely initiates the termination and returns; if you truly want to know whether death is complete, we still have to wait on the handle.

The fourth way is the closest thing to a "graceful kill" we can find on the Windows side. The child is born with `CREATE_NEW_PROCESS_GROUP`; the parent uses `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)` to send it a console event in a targeted way; the child's installed `SetConsoleCtrlHandler` handler receives the CTRL_BREAK, calmly finishes its cleanup, then exits by its own choice with `ExitProcess(5)` — detach received, buffer written to the file, exit code self-chosen: all three in place. Why CTRL_BREAK and not CTRL_C? The CreateProcessW documentation explains: a child born with `CREATE_NEW_PROCESS_GROUP` has the system implicitly call `SetConsoleCtrlHandler(NULL, TRUE)` on its behalf, so CTRL_C is disabled for it by default, while CTRL_BREAK is unaffected — reserved precisely as the deliverable interrupt.

At this point the closing philosophy forks between the two sides. Linux has two cleanly separated tiers: SIGTERM is catchable by default, and the handler tidies up however it pleases; SIGKILL cannot be blocked. Windows' `TerminateProcess` maps to SIGKILL and goes further — but searching upward from it for "the catchable tier" finds nothing: SIGTERM has no counterpart here, and CTRL_BREAK carries a string of preconditions of its own — both sides must share the same console, and the child's handler must be installed in advance; strictly speaking it is a console event, not a signal. Bringing your own IPC to notify the child to terminate itself is the more general engineering answer. We record this contrast too in the closing five-row table.

## The Job Object: Herding Many Processes Into One Handle

With single processes covered, the second half opens the question of managing a crowd. The Job object is a kernel object that can be named and given a security descriptor; it starts empty, and we hang processes into it one by one with `AssignProcessToJobObject` — from then on, operations on the Job fall on the heads of all its members. The documentation carries one crucial characterization, original words: `After a process is associated with a job, the association cannot be broken`. Once hung in, there is no unhooking; if we want out, the only room to maneuver is at creation time — which is e5's subject.

Our uniform posture in e4 is suspended birth: the child is pulled up with CREATE_SUSPENDED, Assigned into the Job, and only then released with `ResumeThread` — eliminating the race of the child sprinting off before the Assign. Once membership is settled, the first thing we look at is inheritance: a child inside the Job, bearing no flags, births a grandchild — and the grandchild automatically enters the same Job; `IsProcessInJob` measured back 1. Membership passes down the process tree. A story in circulation says Chrome was once stuck on exactly this inheritance: its own process wanted to open another layer of Job for its renderer processes, but someone outside had already wrapped its launcher in a Job — and in the Win7 era a process could belong to only one Job, so this road was blocked. The solution waits for the nesting section.

Our signature experiment lands on the `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` flag. The documentation's words: with a Job carrying it, when the last handle closes, all associated processes are terminated, and afterwards the Job object destroys itself too. Group [1] of e4 let the heartbeat child beat 6 times, and then we closed the one and only Job handle:

```text
child alive, heartbeats=6; closing the ONLY job handle now
job handle closed -> child wait=0x0 after 0ms, exit code=0 (0x00000000)
heartbeats frozen at 6 (no more lines after death)
```

The wait returned within 0 milliseconds — the latency fell inside `GetTickCount64`'s timer resolution — the exit code read 0, which is exactly what we measured, and the heartbeat froze at line 6 with not one line after. The second road is closer to real engineering: when the parent process exits, the handles it holds are closed automatically by the system, with the same effect as closing them explicitly. Group [2] of e4 built a three-generation structure, with the middle process creating the Job, pulling up the grandchild, and then exiting: in group (a), with the flag set, the moment the middle process exited, the grandchild's wait returned in 0 milliseconds and the heartbeat stopped at 4 lines. In group (b), the control without the flag, after 1.5 seconds the grandchild was still alive and well, its heartbeat up to 18 lines — the orphan lives on (heartbeat line counts are one round's snapshot; the float follows load, and our verdict reads life or death, not counts). The fork in orphan governance between the two sides sits exactly here: on Linux there is no built-in parent-dies-child-dies mechanism; orphans get adopted by the nearest subreaper (a subreaper is a Linux ancestor process willing to adopt orphans, made one by marking it with prctl). We recorded that batch of Linux-side process experiments as the Lproc series; over there, even who the adopter turned out to be held surprises — while on this side, one flag, and every member exits with the last handle.

> We also measured a counterexample, and it matters greatly in practice: if the Job handle is inherited by the child, this collective exit-on-death breaks. Group [3] of e4 duplicated the Job handle as inheritable, the child received a copy along with everything else, and after the driver closed its own handle, 1.2 seconds later the child was still alive and well — because its own hand also held a Job handle, the Job never sees its last handle close, and the collective execution naturally never happens. When you set KILL_ON_JOB_CLOSE, never let the Job handle leak into the child's inheritance list; the STARTUPINFOEX whitelist is exactly the tool for this.

The Job is also a ready-made observation post for us, and section [4] of e4 verified exactly that. Among the statistics `QueryInformationJobObject` reports back, an empty Job reads all zeros; with two children alive, `JobObjectBasicProcessIdList` reports 2 pids — the driver itself is not on the list. After both exited, ActiveProcesses returned to 0, TotalProcesses recorded 2, TotalPageFaults accumulated to 2783 (one round's snapshot; the number floats with load), and TotalTerminated gave 0 — members that exit naturally are not counted in it. The documentation's rule counts only members the Job terminated for violating limits; this batch verified only the half where natural exits do not count; we did not stage the other half. From birth to death we can query the pid roster, process counts, and page faults — the numbers Task Manager groups by Job draw from exactly this well.

Limits are the Job's other hand. In group [5] of e4 we set `JOB_OBJECT_LIMIT_PROCESS_MEMORY` to 40 MB on the Job, read it back to confirm, and had the child step up `VirtualAlloc` by 4 MB inside: past 36 MB it ate err=1455 (`ERROR_COMMITMENT_LIMIT`, insufficient paging-file commit charge); then `new unsigned char[64MB]` threw `std::bad_alloc`, and the child exited with its self-chosen 42. The failure point landed at 36 MB and floats a few MB with the process's own footprint; our verdict relies on "within the limit," not on the exact digit. The control group ran the same program outside the Job: 256 MB passed in full, and the 64 MB new succeeded too — the failure truly came from the limit, not from the machine. Capping a group of processes' memory to keep a runaway process from eating the machine alive is the Job's daily job; Chrome's official sandbox design document states that every renderer process lives in a Job of its own, and we listed its link in the references at the end.

One more thing a Job can do that this article only points at: associate it with an IOCP, and process creation and exit arrive asynchronously as completion packets. On the Linux side, SIGCHLD is the kernel pushing the death notice over; Windows has no such push channel — the handle itself is the waitable object, and if you want to wait asynchronously, hanging a Job on an IOCP is the proper road. The details wait for the route in [the overview](../../00-overview.md), in the chapter on I/O multiplexing and asynchronous I/O.

## Nesting and Breaking Away

Once a process hangs in a Job it cannot be unhooked — yet real-world process trees insist on layering: the outer manager wants to fence in everything, while inner components want to open Jobs of their own to manage their own people. Chrome's predicament in the last section was stuck on exactly this need, and Win8's nested Jobs resolved it: one process can belong to multiple Jobs in a hierarchy at the same time.

There are three roads out of a Job, and e5 tried them all for us. Inheritance is the default: a child born inside the Job automatically enters the same Job — one `IsProcessInJob` check and it reads back 1. The proper road is named dual-conditional breakaway, requiring both: the Job carries the `JOB_OBJECT_LIMIT_BREAKAWAY_OK` allow flag, and the creator brings the `CREATE_BREAKAWAY_FROM_JOB` flag; the grandchild's `IsProcessInJob` then measures 0 — membership successfully shed. Forcing through means hard-carrying the flag while the Job gave no allow flag; the result is `CreateProcessW` failing on the spot with err=5, the access-denied `ERROR_ACCESS_DENIED` — it will not even build the process. The third road is silent breakaway, carrying `JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK`: this allow flag lets go without a sound — the creator carries no flag at all, and the grandchild still measures 0; it walked away. The effect a manager wants — "I manage my children, but not my children's children" — uses exactly this, at the price that from then on the tree can never be fully watched, a caution the documentation itself raises.

Nesting itself has no dedicated API. The presumptive posture is to pass the child Job's handle as hProcess to `AssignProcessToJobObject`; group (a) of e5's section [5] measured ret=0 with err=6 (`ERROR_INVALID_HANDLE`) — that parameter accepts process handles only. The proper road is written on the documentation's Nested Jobs page: the same process, Assigned first to the root Job and then Assigned again to the child Job — the two calls' order is the hierarchy. We verified with `IsProcessInJob` against both Jobs, and both read back 1.

Once the hierarchy stands, how do limits compute? Group (b) of [5] gave the parent Job 48 MB and the child Job 256 MB; the child requested upward in 8 MB steps and fell at 40 MB with err=1455 — the parent Job's 48 MB held down the child Job's 256 MB. The 8 MB short of 48 MB is exactly the process's own footprint; the failure point floats with the footprint too, and again the verdict relies on "within the limit." Commit-class limits take the tightest value along the whole chain; a child Job can only tighten further, never loosen what the parent Job drew. When you design layered limits, the outside-in principle is subtract-only, never add — that is using these semantics correctly.

## Who Splits argv: The Command Line and Two Parsers

Before the wrap-up, one small matter remains whose implications are not small: where do main's argc/argv come from? On Linux, `execve`'s argument array is prepared by the caller and handed over verbatim by the kernel — the child's argv is the array the father wrote. The Windows kernel passes no array at all; what it hands the child is one raw command-line string — the return value of `GetCommandLineW` — and the job of splitting it into argc/argv is done by the CRT itself at startup.

e7 verified two scenes for us. The first is argv[0] forgery: app was filled with the real body's path, the command line's first token read `TOTALLY_NOT_ME.EXE`, and both the child's argv[0] and item 0 of `CommandLineToArgvW`'s split came back the fake name — the two sides agree, and the child has no channel at all to recover its true name from argv. In e1 it was still a cute demonstration; placed in a security context, it is the habitual entrance for argument forgery.

The second scene is the two parsers' divergence. `CommandLineToArgvW` is the official splitting function in shell32; it and the CRT each split their own way, and we put them on the same stage:

```text
父进程发: "…exe" argv plain "two words" "quoted ""inner"" text" tail
- 空格参数 "two words":父进程的原意、CRT 与 CommandLineToArgvW 三方一致,一个参数
- 内嵌引号 "quoted ""inner"" text":CRT 拆成单个参数 quoted "inner" text
  CommandLineToArgvW 拆成 quoted "inner 与 text tail 两个 —— 判定 NO (content differs)
```

At the embedded-quote boundary, MinGW's CRT splits by MSVC's rules (`""` yields one quote, and the quoting state persists), while `CommandLineToArgvW` splits by its own rules — and the two answers differ. The claim that main's argv equals what CommandLineToArgvW produces no longer holds in general on this toolchain. If you write code that needs precise quote semantics — a shell, argument forwarding, a test framework's command-line replay — either state explicitly which splitter you use, or implement the rules yourself; leave both to their own defaults and the divergence is already planted.

## The Two Sides Side by Side: A Five-Row Wrap-Up

This article has run its course; let us align the two sides' process machinery once through. Every row's Windows side — except the half-sentence about IOCP in the death-notice row, pointed at but not unfolded — is evidenced in this article's e-series experiments, and the Linux-side measurements come from the same batch's Lproc series; only what matches on both sides entered the table:

| Axis | Linux side | Windows side |
| --- | --- | --- |
| Building a process | fork copies, exec swaps the core; the child returns twice from the same point | CreateProcessW in one step; the parent gets the hProcess/hThread pair, the child starts from main |
| Reaping | the waitpid family; wstatus still needs WIFEXITED/WIFSIGNALED sorting | WaitForSingleObject/WFMO on handles; the return names the index directly; GetExitCodeProcess fetches the code (259 = not yet exited) |
| Death notice delivery | SIGCHLD, pushed asynchronously by the kernel | No push; the handle itself is the waitable object; going async means hanging a Job on an IOCP |
| Orphan governance | No built-in parent-dies-child-dies; orphans pass to subreapers, governance by manual wait | Job plus KILL_ON_JOB_CLOSE: last handle closes, everyone exits; no Linux counterpart |
| Graceful exit | SIGTERM catchable by default, handler tidies up; SIGKILL unblockable | TerminateProcess unblockable (maps to SIGKILL); SIGTERM has no counterpart; nearest are CTRL_BREAK or do-it-yourself IPC |

We pull abort's shape out of the table for a separate look: the same `abort()` reads over there as `WIFSIGNALED` plus `WTERMSIG=6`, and over here folds into exit code 3221226505. Windows encodes even exceptional death into the 32-bit exit-code space, at the price of the parent losing the is-it-signal-death dimension; Linux keeps the dimension, at the price of wstatus passing through a sorting-macro workshop.

The process's birth and death, herding and escape, are complete with this article. But CTRL_BREAK has remained only a tool in our hands: how console events get delivered, on which threads the installed handlers run, and how APC (asynchronous procedure call — the mechanism for stuffing a function into a target thread's execution queue) wakes a sleeping thread — all of that is scheduled for the next article, [Console Events and APC](02-console-apc.md), where CTRL_BREAK is promoted from tool to protagonist. See you in the next article.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="CreateProcessW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw"
  />
  <ReferenceItem
    :id="2"
    title="Job Objects"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects"
  />
  <ReferenceItem
    :id="3"
    title="Nested Jobs"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/procthread/nested-jobs"
  />
  <ReferenceItem
    :id="4"
    title="ExitProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess"
  />
  <ReferenceItem
    :id="5"
    title="TerminateProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess"
  />
  <ReferenceItem
    :id="6"
    title="GetExitCodeProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess"
  />
  <ReferenceItem
    :id="7"
    title="GenerateConsoleCtrlEvent function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent"
  />
  <ReferenceItem
    :id="8"
    title="CommandLineToArgvW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw"
  />
  <ReferenceItem
    :id="9"
    title="Chromium sandbox: Design of the sandbox"
    publisher="Chromium Project"
    url="https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md"
  />
</ReferenceCard>
