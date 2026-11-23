---
title: "Signals, Part 1: sigaction and Async-Signal Safety"
description: "How signals arrive and what a handler may do — this article answers with seven groups of live measurements: three SIGUSR1 sends during a blocked period leave one pending bit and the handler runs once after unblocking, with delivery landing precisely on the return path of the unblocking sigprocmask and handler lines always sitting between two main-loop write lines, never splitting one; the sigaction family measured live — three si_code values under SA_SIGINFO (SI_TKILL from raise, SI_USER from another process's kill, SI_KERNEL from an itimer), sa_mask masking the signal itself too, SA_NODEFER nesting the same signal, SA_RESETHAND dying to SIG_DFL on the second trigger, and signal() reading back sa_flags=0x14000000=SA_RESTART|glibc's internal SA_RESTORER (for the trampoline); the centerpiece is async-signal safety: the bulk of the extra lines from printf in a handler is well-formed whole-line duplication (an independent rerun measured 648 line numbers each appearing twice, intact — the buffer's write position wrapped back and the same line was recopied from the top, invisible to a grep for malformed output), with fragments the minority — the archived run's corrupted-frame index counted exactly 21 frames (the harshest, M 009[H, had its line number copied halfway when interrupted, and there are pure-corruption frames with no handler trace at all; the disease is the half-updated shared buffer, not interleaving); glibc 2.44's stdio lock is recursive within a thread, so the textbook deadlock never appeared — what appeared is state corruption; write's safety means it does not corrupt its own state, yet DONE still got glued into an unflushed buffered line; the reentrancy article's volatile-less busy-wait had its load hoisted out by -O2, leaving only jmp .L5 in the assembly with not even a spin counter in the loop, timeout killing it at 3 seconds with exit 124 and output stopped at busy-waiting, while the volatile version does a movl every spin and exits cleanly after 538 million spins; the engineering patterns of flag/self-pipe/siglongjmp; SIGCHLD merging three children's exits into one bit, the waitpid loop reaping 3 while the naive version reaps 1 and leaves two zombies; closing with the measured uncatchability of SIGKILL/SIGSTOP (EINVAL) and a quick-reference table of common signals."
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 30
prerequisites:
  - "错误处理范式:从 errno 到 expected"
  - "进程创建与生命周期:fork/exec/posix_spawn"
related:
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "IPC:管道、FIFO 与 POSIX 消息队列"
  - "虚拟内存 API 全景:mprotect/madvise/mlock"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/process/04-signal-basic.md
  source_hash: cbd2ab5a5f26fc9d2bebfb92502c067b33e8fe1101f540e524cb3cdbfed7f2d4
  translated_at: '2026-11-22T16:50:00+00:00'
  engine: glm-5.3
  token_count: 24840
---

# Signals, Part 1: sigaction and Async-Signal Safety

The routes for passing data between processes have been pretty well walked in the earlier articles: the kinship born of fork, the new programs swapped in by exec, the bytes flowing through pipes. This article turns to a more primitive kind of communication: signals. One `kill` from another process, or the kernel's own timer expiry or illegal memory access, can divert your program's flow of execution on the spot — to run a function you registered, and when it finishes, come back and continue the work at hand. And precisely because this function may appear between any two instructions, what it may and may not do inside becomes a discipline that must speak through live measurement — and several answers this article measured out do not match what many textbooks say.

[The error handling article](../../thinking/02-error-paradigm.md) took SA_RESTART's behavior apart with strace: a blocked read interrupted by a signal hands back -1 with EINTR, while SA_RESTART has the kernel restart it — the details of that pairing live in that article's text and will not be retold here. What that side watched was what happens to the interrupted system call; what we answer head-on today are two more fundamental questions: how exactly does a signal arrive, and what exactly can be done inside a handler. For the second question's answer, when I first ran E3 and got that output with 21 corrupted frames, I stared at the screen for a long while — the deadlock the textbooks promised never appeared, and what did appear was more worth writing down.

The experiments are numbered E1 through E7, corresponding one-to-one with the seven directories 01 through 07 under `code/volumn_codes/vol8/systems-programming/linux/process/04-signal-basic/` in the repository; the code and raw output are archived, with per-experiment rerun commands in the README, ready for cross-checking at any time. The output blocks in the text are mostly excerpts, removed lines marked with `...`; when checking against the archive, the archive is authoritative. This article's E-series belongs to this article alone; the E-series of the memory management articles have nothing to do with ours — go by directory numbers when browsing, and sub-experiments appear in narrative order, not numerical order. The series' shared utilities `unique_fd`, `sys_call`, and `errno_code` follow the Thinking Foundations articles' definitions, but this article's experiments are essentially bare calls, and those utilities never take the stage — in the handler's world we carry almost nothing of C++, not even printf; why, you will understand at E3.

The environmental baseline is stated as usual, for the later numbers to be checked against: the experiments come from my WSL2, the kernel is a 6.18.33.2-microsoft-standard-WSL2 build, g++ is 16.2.1, glibc is 2.44, everything compiled with `-std=c++20 -Wall -Wextra -O2` and not one warning produced. `-O2` is not a personal preference — E4's counterexample stands only because of it. The pids in the output change on every run, so what we cite are equalities and relations, never concrete values. One documentation note: the local man7 pages are not installed on this machine, and the safety list of man 7 signal-safety was verified against the online version at man7.org; the citations below follow it.

## E1: The Delivery Model — Standard Signals Keep One Bit

We watch a signal's life in three steps. Generation: someone called kill, or the kernel produced it itself, and the kernel notes this fact on the process. Pending: the signal has been generated but has not yet had its turn, and what the kernel keeps for each signal is only a bit, not a queue. Delivery: the moment the kernel actually suspends the main flow and calls up the handler. The step easiest to get wrong is the second; we ground it with an experiment: fire three times in a row during a blocked period, and what appears on the bitmap is a single 1.

### Three Send Paths and One Pending Bit

The sending path is more than one. A direct `kill(getpid(), SIGUSR1)` is one road, a self-sent `raise(SIGUSR1)` is the second, and a child sending `kill(getppid(), ...)` from outside is the third. In E1.1 we walked each road once; the handler's count was 1 every time:

```text
[E1.1] three send paths, pid=15070 uid=1000
    [H] SIGUSR1 handler entered
[E1.1] path kill(getpid())      -> handler count=1
    [H] SIGUSR1 handler entered
[E1.1] path raise(SIGUSR1)      -> handler count=1
    [H] SIGUSR1 handler entered
[E1.1] path child kill(parent) -> handler count=1
```

The paths themselves are nothing exotic; what is worth remembering is the underlying route of raise, which we leave as a foreshadowing for E2. The truly important experiment is E1.3: we masked SIGUSR1 with `sigprocmask(SIG_BLOCK, ...)` — masking means the kernel holds the signal at pending without delivering it — then fired three times in a row and read back the bitmap it gives. What `sigpending` reads back is exactly that bitmap:

```text
[E1.3] block SIGUSR1, send it 3 times while blocked:
[E1.3] after 3 sends:         pending: SIGUSR1=1 SIGUSR2=0 SIGALRM=0
[E1.3] handler count while blocked = 0 (never ran)
    [H] SIGUSR1 handler entered
[E1.3] after unblock: handler count = 1  <- 3 sends, ONE delivery
[E1.3] after unblock:         pending: SIGUSR1=0 SIGUSR2=0 SIGALRM=0
    [H] SIGUSR1 handler entered
[E1.3] control run: 1 send while blocked -> handler count = 1 (same as 3 sends)
```

Three sends were made, and what stayed in pending is a single 1. And after unblocking, the handler ran only once. The control group is more interesting: we blocked once more, sent only once this time, and the handler's count was again 1. Three sends and one send produce exactly the same result. The original text of man 7 signal puts it plainly: standard signals are not queued; when the same signal is generated multiple times during a blocked period, it is marked pending only once. The bitmap holds only 0 and 1; the extra two sends were merged away at the moment of generation. The "lost signal bug" framing does not hold — this is simply the definition of a standard signal. Signals that do queue are called real-time signals, and they belong to the next article.

The timing of delivery is also in the output above. Look at the third and fourth lines: after the printf reporting a handler count of 0 finished, the [H] line appeared, and only then came the post-unblock printf. Delivery lands precisely on the return path of the unblocking `sigprocmask`. At the checkpoint where the kernel returns to user mode, it checks for newly unmasked pending signals, so the handler is called up before the next user instruction — the timing neither early nor late, and we can see with the naked eye between which two statements it wedged itself.

### [H] Always Sits Between Lines, Never Splits One

In E1.2 we shift the observation angle: what does it look like when the handler's output and the main loop's output stir together? The main loop uses one `write(2)` call per line, while the child sends SIGUSR1 every 60 milliseconds:

```text
...
MAIN iter 1 (t=0ms)
MAIN iter 2 (t=20ms)
    [H] SIGUSR1 handler entered
MAIN iter 3 (t=40ms)
MAIN iter 4 (t=60ms)
MAIN iter 5 (t=80ms)
    [H] SIGUSR1 handler entered
MAIN iter 6 (t=100ms)
MAIN iter 7 (t=120ms)
MAIN iter 8 (t=141ms)
    [H] SIGUSR1 handler entered
[E1.2] observation: handler lines always sit BETWEEN main lines, never split one
```

We ran many rounds; the [H] lines always landed whole between two main-loop lines, and not once did one get split in half. The mechanical explanation: the kernel accepts signals at instruction boundaries — precisely at checkpoints like a system call's return to user mode — never inside a write. The write system call either completes entirely or, per the EINTR contract, is discarded entirely to start over, so an output line never gets torn down the middle. This tidiness is write's gift; run the same mixed-output observation with printf in E3, and the picture changes beyond recognition.

## E2: The sigaction Family — flags, Masks, and si_code

For registering dispositions we use only the sigaction family. A disposition says what to do when the signal arrives: SIG_DFL for the default action, SIG_IGN to ignore it outright, or entry into your handler. The error handling article used it to install the plainest of handlers; this section walks the common flags one by one.

### SA_SIGINFO: The Sender Is Identifiable

Start from the handler's prototype: the simple one is `void(int)`, receiving only the signal number. With SA_SIGINFO it switches to the three-parameter version, whose second parameter, a `siginfo_t*`, holds the details of this generation: who sent it, and how. E2.1 measured three provenances live; the si_code field specializes in the how:

```text
[E2.1] SA_SIGINFO: sender is identifiable, my pid=15082 uid=1000
    [H] caught 10: si_signo=10 si_code=-6(SI_TKILL  raise/tgkill发来) si_pid=15082 si_uid=1000
    [H] caught 10: si_signo=10 si_code=0(SI_USER   kill()发来) si_pid=15083 si_uid=1000
[E2.1] child pid=15083 sent it -> si_pid must equal this
    [H] caught 14: si_signo=14 si_code=128(SI_KERNEL 内核产生) si_pid=0 si_uid=0
```

Check against the table. The first line was sent by `raise`: the si_code reads -6 (SI_TKILL), and the si_pid is ourselves — the foreshadowing planted in E1 pays off here: glibc's raise goes through the tgkill system call underneath, and the kernel marks it SI_TKILL by call origin. The second line was `kill`ed over by the child: its si_code is 0 (SI_USER), and the si_pid gives 15083, digit-for-digit equal to the child pid printed on the next line — the answer you want lives in that field. The third line is the SIGALRM the kernel itself produced at a `setitimer` expiry: the reported si_code is 128 (SI_KERNEL), and si_pid records 0 — no sender process exists. That completes the three common codes we can measure; the si_code roster goes beyond them: SI_QUEUE is for signals sent by sigqueue carrying attached data, belonging to the real-time family, and along with the rest of the real-time codes we leave it for the next article.

> A word on the experiment's own discipline: E2's handler formats with snprintf then writes, yet snprintf is in fact not on the async-signal-safe list either (a POSIX-defined list governing which functions may enter a handler; the criterion comes at the top of E3) — we did it to print the si_* fields verbatim. What the fully law-abiding version looks like, E3 will show you.

### sa_mask: During the Handler, the Signal Itself Is Also Masked

While a handler runs, the process's signal mask temporarily widens a ring: every signal listed in sa_mask joins the mask, and the very signal that triggered the handler adds itself automatically — unless you say no explicitly with SA_NODEFER. The design's intent is to let the handler run to completion in peace, uninterrupted by its own side. E2.2 read the mask and pending live from inside the handler, turning this into visible numbers:

```text
[E2.2] sa_mask={SIGUSR1}: USR1 cannot preempt USR2 handler
    [H] SIGUSR2 handler entered
    [H] mask inside handler: SIGUSR2=1 SIGUSR1=1 (self + sa_mask both blocked)
    [H] raise(SIGUSR1) inside handler -> pending=1 (NOT delivered while we run)
    [H] SIGUSR2 handler returning
    [H] SIGUSR1 handler runs only AFTER SIGUSR2 handler returned
[E2.2] back in main
```

The handler we installed for SIGUSR2 had only SIGUSR1 written into its sa_mask. Yet in the reading taken inside the handler, SIGUSR2 itself is also 1: the automatic masking counted itself in. We raised a SIGUSR1 again inside the handler; it only entered pending and could not be delivered for the whole duration of the handler's execution. Only after the SIGUSR2 handler finished and returned did the SIGUSR1 handler take its turn. The two handlers ran one after the other, no nesting — that is the combined effect of sa_mask plus automatic masking.

### SA_NODEFER and SA_RESETHAND: Two Switches, Two Tempers

SA_NODEFER turns off the automatic masking just described: when the same signal arrives again inside the handler, it enters recursively at once instead of queuing at pending.

```text
[E2.3] SA_NODEFER: same signal re-enters its own handler
    [H] enter, depth=1
    [H] enter, depth=2
    [H] leave
    [H] leave
[E2.3] done (depth back to 0; without SA_NODEFER the inner raise would stay pending)
```

Watch depth: it rose from 1 to 2 and fell back — the same signal's handler truly wrapped itself one layer. This switch must come with a sober companion thought: the nesting depth is limited only by stack capacity; recursive handlers run on the same stack, and a high-frequency signal paired with a handler fond of raising itself puts the stack in danger.

SA_RESETHAND has yet another temper: once the handler enters the door, the disposition is reset back to SIG_DFL, and on the second arrival it goes straight to the default action. SIGUSR1's default action is termination; we let all of E2.4 happen inside a child process, so the parent could reap and read the result:

```text
[E2.4] SA_RESETHAND: one-shot handler, 2nd delivery hits SIG_DFL
    [H] first delivery: handler body runs
CHILD: survived 1st; raising again with disposition reset to SIG_DFL (terminate)
[E2.4] child ended: WIFSIGNALED=1 WTERMSIG=10 (SIGUSR1=10) -> reset happened
```

On the first raise, the handler's body ran. On the second raise, that `CHILD: never printed (dead)` line never got its chance to print — the child died to SIGUSR1's default action, and `WIFSIGNALED=1 WTERMSIG=10` is the parent's autopsy report. One-shot handlers are the System V style in Unix history, and we are about to meet that style again in signal().

### The glibc Semantics of signal(): One Bit in 0x14000000 Is a Trampoline

`signal()` is a senior interface with famously heavy historical baggage: the same `signal(sig, handler)` call carries System V semantics — the handler resets after one use and is not masked during installation — or BSD semantics: the handler persists, masks itself while running, and interrupted calls restart automatically. Which side does glibc install for us? No need to dig through documentation guessing; after installing, we read the flags back with sigaction:

```text
[E2.5] signal(): glibc installs BSD semantics
[E2.5] signal() returned old disposition: (non-null)
[E2.5] readback sa_flags=0x14000000: SA_RESTART=1 SA_NODEFER=0 SA_RESETHAND=0
[E2.5] raised twice -> handler count=2; handler PERSISTS (System V semantics would reset after 1st and die on 2nd)
```

The sa_flags read back is 0x14000000, which splits into two bits: 0x10000000 is SA_RESTART, and the remaining 0x04000000 is SA_RESTORER. The former is the evidence of BSD semantics — man 2 signal states it too: inside glibc's signal() it is a sigaction with SA_RESTART that gets installed. The latter is not among the public flags; man 2 sigaction's list actually contains it, marked for internal use and not for application calls — it is an internal bit glibc passes to the kernel. The sigaction wrapper stuffs the address of a small piece of code into the sa_restorer field and sets this bit as well. When the handler returns, the jump lands on that piece of code, which issues the rt_sigreturn system call, and the scene is handed back to the kernel. Such shim code lying in between, responsible for sending the flow of control back, goes by the trade name trampoline — in Chinese, literally a trampoline. That is why, firing twice in a row, the handler's count is 2: the System V-style second death does not exist on our machine. For explicitly one-shot behavior, SA_RESETHAND is the only proper road. And man 2 signal's advice passes to you as well: when you really want to install a handler, switch to sigaction, with the semantics fully in your own hands.

### The sigprocmask Trio

The mask's three operations each behave differently; lay out one measured sequence and everything is clear:

```text
[E2.6] sigprocmask: BLOCK=并集, UNBLOCK=差集, SETMASK=整个替换
    mask initial (empty)                    SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=0
    mask BLOCK{USR2}                        SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=1
    mask BLOCK{USR1,USR2}                   SIGINT=0 SIGTERM=0 SIGUSR1=1 SIGUSR2=1
    mask UNBLOCK{USR2}                      SIGINT=0 SIGTERM=0 SIGUSR1=1 SIGUSR2=0
    mask SETMASK{} (clear all)              SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=0
```

BLOCK takes the union — only adding, never subtracting. UNBLOCK takes the difference — only subtracting, never adding; the way USR1 stayed behind is the proof. SETMASK replaces the whole mask; pass in the empty set and everything is cleared. None of the trio's semantics is hard; the hard part is that UNBLOCK does not incidentally clear other bits for you — once masking logic grows complicated, this is the easiest place to take for granted.

## E3: Async-Signal Safety — What the Extra Lines Are

A sentence was left at the start: in the handler's world we carry almost nothing of C++, not even printf — why, you would understand at E3. Now it is E3. Async-signal-safety is a list POSIX provides: a function on the list means calling it in a handler comes to no harm; a function off it gets no promise whatsoever. The list's criterion, in the original words of man 7 signal-safety: the function must be either reentrant or atomic with respect to signals. Nowhere on the list can printf's shadow be found, while write sits right on the table. The textbooks, reaching this point, usually add that printf in a handler will deadlock. Let us see with experiments what actually happens on this glibc 2.44 machine.

### Two Programs of the Same Origin, Differing Only in That One Handler Line

Look at the experiment's arrangement: an itimer expires every 80 microseconds sending SIGALRM — the setitimer route E2.1 just used. The main loop prints 30000 fixed-format short lines with printf. The two programs differ only in handler and buffering strategy. The crashing version's handler uses printf; once output is redirected to a file, stdio automatically goes fully buffered, and handler and main loop share the same buffer:

```cpp
// unsafe_printf.cpp 的 handler,翻车的原因就在这一行
void on_alrm(int)
{
    g_ticks = g_ticks + 1;              // volatile sig_atomic_t,C++20 起复合赋值弃用,写全
    std::printf("[H tick %d]", (int)g_ticks);   // 不在安全名单上
}
```

Now the safe version's handler: it does only two tiny jobs — write one fixed constant string, and increment a flag. The main loop, for its part, sets stdout unbuffered, so one printf is one straight-down system call:

```cpp
// safe_write.cpp 的 handler
void on_alrm(int)
{
    g_ticks += 1;
    write(STDOUT_FILENO, "[H tick]\n", 9);   // 在安全名单上
}
```

Each version ran once. The crashing version's full output is 30585 lines: 30000 main-loop lines plus 23 handler outputs, which should have stopped around 30023 — yet 562 extra lines appeared. The safe version delivered 30254 lines, exactly 30000 main-loop lines plus 253 ticks plus the one DONE line, not one character more or less, with zero corrupted frames. The two runs' tick counts, 23 versus 253, are no contradiction: the buffering strategy determines the main loop's speed, and the number of timer landings follows; each pair of numbers belongs to its own run, and we do not subtract them against each other.

So what do those 562 extra lines in the crashing version look like? Using the archived source and the same command, we ran an independent rerun to verify the composition. The rerun's total is 30658 lines; exactly 21 lines are not well-formed, while the bulk of the inflation is 648 line numbers each appearing twice in intact whole-line form, often chained into short runs — the stretch from 001171 to 001175, for instance, has every line number twice in a row. When grep filters for damage with a well-formedness pattern, only those 21 frames come out; the bulk of the increase it cannot catch at all: those lines' form is beyond reproach, there is simply an extra complete copy of each. In the archived run's corrupted-frame index, the count likewise comes to exactly 21 frames — the two runs' corruption positions differ entirely, and the matching count is pure coincidence; the frames cited below all come from the archived run.

### More Than One Way to Be Corrupted

The five frames laid out below are taken from the archived run's corrupted-frame index; the line numbers are positions grep counted out in the archived full output:

```text
1111:[H tick  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
2531:[H tick 2]M 002530 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
3950:M 003949 abcdefghijklmnopqrstunopqrstuvwxyz0123456789ABCDEFGHIJ
9582:M 009[H  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
30574:defghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
```

Take the frames one by one. Line 1111 is the handler's own output with the number gone; only [H tick and two spaces remain. Line 2531 lost its newline, the handler's tail glued into one line with the main loop's next line. Line 30574 is an orphaned fragment: its original line was cut at the waist, and the back half became a line of its own. These are all still the direct consequences of two printfs fighting over the write.

### The Harshest Frame and the Coldest Frame

The frame that shows the mechanism most clearly is line 9582; let us read it with a few lines of context on each side:

```text
...
M 009433 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009434 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009[H  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009436 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009437 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
```

Watch the main loop's side: it was copying the line number of the line M 009435, and upon reaching 009 the signal arrived. The handler's printf took over the same buffer and wrote [H plus a space. When the main loop resumed, it continued copying from the position it had noted for itself, and the handler's number was buried under the main loop's bytes. Each side's output was left with half a body: the same line holds both the main loop's severed head and the handler's surviving limb — the harshest frame of corruption.

Now line 3950, the coldest frame: the main loop's own line, characters missing into thin air, with no trace of the handler anywhere in the line. This frame is where the real disease lives. It cannot be explained by two printfs fighting to write — a fight at least requires both parties on stage, and this frame has only the main loop. The actual mechanism: every line printf prints updates the stream's internal buffer position pointer and counters; the signal interrupted precisely in this half-updated state, and the handler's printf then advanced the same batch of state according to its own understanding. When the main loop resumed, the pointer in its hand already pointed to the wrong place. The wrong direction comes in two kinds: with the pointer jumped forward, the subsequent copy skips characters and lands misplaced, growing fragments; with the pointer wrapped backward, the same line gets copied again from the top, growing one well-formed duplicate — the 648 duplicated line numbers in the rerun are the wrap-around's legacy, and grep's well-formedness pattern catches not a single one of them. man 7 signal-safety's verdict on stdio is exactly this sentence: they maintain static storage with associated counters and indices (or pointers). So the disease root of printf's unsafety is shared state; fragment interleaving and intact duplication are both merely its symptoms. Swap the handler's printf for anything else that moves this state, and the corruption will arrive all the same.

### The Textbook Says Deadlock; Measured: No Death, but Deeper Damage

The textbook-style claim is that printf in a handler deadlocks: the main flow's printf holds stdio's lock, the signal interrupts it, and the handler's printf goes for the same lock — waiting for itself. In our 30585-line measurement, that deadlock never showed its face; the process ran from head to tail. The reason lies in glibc's lock implementation: its stdio lock is a recursive lock reentrant within the same thread, recording the holder and a recursion count; a second acquisition by the same thread merely increments the count and does not shut itself out. So on this glibc 2.44 machine, the deadlock appears wearing a different face: the lock does not stop you, but the shared buffer state still gets split apart, and the 21 corrupted frames are the scene it left behind. The full measure must also be stated: printf internally goes through malloc too, and if the handler interrupts some other lock-holding path — the heap's lock, say — deadlock remains possible; our arrangement did not meet it, and we do not assert it cannot happen. Crossing deadlock out of the account and writing state corruption in — this is the first textbook-level claim this article overturns. The lost-signal bug corrected in E1 was only our common misreading and does not rise to the textbook tier.

### write Is Safe — On Which Side

The control version's safety had its numbers laid out earlier: 30254 lines, matched exactly, not one corrupted frame. Why write works — the answer is still the safety list's criterion: it is a single system call; the buffering, locking, and position accounting are all done in one pass on the kernel side, and user space holds no half-updated state for a signal to split down the middle. It belongs to the atomic-with-respect-to-signals class, and with printf, the reentrant counterexample, the two halves of the criterion are complete.

One more detail in the safe version deserves an extra glance: the handler's [H tick] always occupies its own line, and the main loop's lines are never touched:

```text
...
M 000104 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 000105 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
[H tick]
M 000106 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
M 029999 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
DONE ticks=253 (flag observed by main loop: 253)
```

E1.2 said write's lines never split; here we verified it again at the scale of 30000 lines — not a single split one.

### DONE Glued Into Someone Else's Line

The boundary of the sentence "write is safe" also needs drawing. The crashing version's program, at its close, switched to writing the DONE line directly with write — printf being undependable by then — and the line did not get a row of its own; it got glued into a main-loop line:

```text
...
M 029987 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 029988 abcDONE ticks=23
defghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 029989 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
```

At that moment the stdio buffer still held unflushed main-loop content, and the bytes written directly by write landed at an earlier position in the file; DONE thus wedged itself into the belly of the M 029988 line, whose back half was squeezed out into a line of its own. write's safety says it does not corrupt its own state; it never says it will queue for you against the stdio buffer: mixing two output channels on the same fd leaves the ordering to you. The safe version set stdout unbuffered precisely so the main loop also takes the direct-write road — two roads merged into one, and only then are the timings comparable. This is the second common claim this article corrects by measurement: write is safe — do not read it as write is almighty.

## E4: Reentrancy — Two Guarantees for One Flag, Two Lines of Assembly

One word in E3's list criterion has not yet been faced head-on: reentrant. It means that when the same piece of code, halfway through execution, is entered again, the second entry does not trample the first's progress. Handlers exist precisely to manufacture such entries, so reentrancy directly decides whether a function or a variable may enter a handler. We start from the most basic shared flag, and here the compiler delivers a textbook-grade counterexample.

### volatile sig_atomic_t: Miss Either One and It Breaks

The plainest carrier for messages between handler and main loop is a global flag. The C standard keeps a dedicated type for it, sig_atomic_t, guaranteeing that reads and writes of this integer complete as a whole — indivisible. The volatile qualifier guarantees that the compiler genuinely reads memory every time, not making do with a stale register value. Each guarantee minds half; combined, they are volatile sig_atomic_t. With one source file and two compilations we ran E4a, proving the fate of the missing volatile:

```text
busy-waiting for g_flag (SIGALRM at +100ms)...     <- 两种编译的开头一样
```

Watch the plain version: it has sig_atomic_t's protection but no volatile, and under -O2 it was killed by `timeout 3`, exit code 124, its output forever stopped at the busy-waiting line. The volatile version exited cleanly:

```text
busy-waiting for g_flag (SIGALRM at +100ms)...
observed g_flag=1 after 538301099 spins -> clean exit
```

Same source compiled: the volatile version spun 538 million rounds and saw the flag; and the plain version? Its loop body had even the spin count optimized away; it spun until timeout killed it, never receiving even that one moment of seeing. The handler ran, the memory was written, and the plain loop simply could not see it. The assembly spells out the reason for us. The plain version's loop body:

```asm
	movl	g_flag(%rip), %eax      # 加载一次,提出循环
	testl	%eax, %eax
	jne	.L4
.L5:
	jmp	.L5                     # 死循环本体,再也不读 g_flag
```

And the volatile version's loop body:

```asm
.L5:
	movl	g_flag(%rip), %eax      # 每一圈都重新加载
	addq	$1, %rsi
	testl	%eax, %eax
	je	.L5
```

On one side, the load is hoisted out of the loop: the entire busy-wait reduces to a single `jmp .L5`, and within the loop the flag effectively does not exist. On the other side, every round has one movl genuinely reading memory. The compiler does not know the handler exists; in its eyes nobody writes g_flag in the loop body, and optimizing the read away is fully conformant. So the compiler is not wrong — what is wrong is our failure to tell it that someone else writes this memory; that is exactly what volatile says. sig_atomic_t governs indivisible reads and writes; volatile governs genuinely reading every time; neither substitutes for the other.

### strtok and localtime: Progress Living in Static Storage

At the function level, reentrancy's dangers often grow in static storage. Some functions keep their in-progress state in static variables inside the function; on a second entry, the first's progress is overwritten. E4b uses a call sequence that is deterministically legal in a single thread to simulate the effect of handler interruption — the behavior is deterministic, and we see it plainly:

```text
== strtok: the in-progress pointer lives in static storage ==
[strtok] main strtok #1 -> alpha
[strtok] (handler-style) strtok -> X   <- 静态进度被覆盖
[strtok] main strtok #2 (expect beta) -> Y   <- CORRUPTED

== localtime: the result struct itself is static ==
[localtime] first  localtime -> 2001-09-09 01:46:40
[localtime] second localtime -> 2014-05-13 16:53:20
[localtime] re-read pointer a   -> 2014-05-13 16:53:20   <- a silently became t2
```

strtok's progress pointer lives in static storage. The main flow had just taken alpha when another strtok arrived in the interruption, and the static pointer was redirected to the Y of another string. When the main flow resumed and fetched again, the call that expected beta actually received Y. localtime is even more covert: the pointer it returns aims at a static struct tm inside the function; the second call rewrites the same storage directly, and although the pointer a in your hand has not moved one byte, what it points to has silently become t2's moment.

### The _r Suffix and the Safety List

Each has a reentrant version — `strtok_r` and `localtime_r` with their `_r` — moving the state into a caller-provided buffer; the `_r` in the name was invented for exactly this. But one common sentence cannot be repeated as-is: carrying `_r` does not mean entry onto the safety list. We checked the original text of man 7 signal-safety's list: strtok_r is on the table, strtok falls off it; and the localtime family is absent entirely — even localtime_r lands outside. So inside a handler, only a checked table counts; the suffix offers only a clue and guarantees nothing.

## E5: Handler Engineering Patterns — flag, self-pipe, siglongjmp

With this many constraints told, we gather the affirmative patterns into three, from lightest to heaviest.

### Pattern One: A Flag Plus the Main Loop

The handler does exactly one thing: set a bit on a volatile sig_atomic_t and return at once, leaving all the work to the main loop. E5's pattern one demonstrates graceful shutdown: the child sends SIGINT after 100 milliseconds — under the default disposition that is an on-the-spot death with core — but the flag version:

```text
t=   0ms work loop starts (SIGINT will arrive ~100ms)
...
t=  91ms working iter 4
t= 100ms g_stop seen at iter 5 -> break
t= 100ms cleanup: flush buffers, close files, say goodbye
```

The fact of the signal's arrival is consumed only at the moment the main loop checks the flag, and by that moment we are back in an ordinary context: printf is free to use, destructors run as usual, and the exit code is a clean 0. The handler's entire duty is that one assignment — the cheapest way to live under the constraints.

### Pattern Two: self-pipe, Turning Signals into Bytes on an fd

Nor should we forget the flag's natural short leg: the main loop must poll it. If our main loop is already an event loop circling around poll or epoll, the more elegant move is turning signals into readable events on an fd. The technique's name is the self-pipe trick, a named and time-honored device: the handler only writes one byte into a pipe, the main loop polls the pipe's read end, and signals are scheduled together with every other fd in the same loop:

```text
t=   0ms event loop starts, poll(pipe_r, 200ms timeout)
t= 100ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 301ms (heartbeat, nothing readable)
t= 501ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 551ms poll woke up -> byte 't' = SIGTERM -> shutdown
t= 551ms event loop exited cleanly
```

Read the timeline above carefully. SIGINT became the byte i and SIGTERM the byte t — both signals travel the same channel. The 301 ms wake is poll's 200 ms timeout, the heartbeat slot; in a real program, periodic tasks run here. At 551 ms the t arrived, and the loop exited the stage with grace. The pipe is created with `pipe2(O_NONBLOCK | O_CLOEXEC)`; the non-blocking bit carries one meaning at each of two places: the handler's write returns EAGAIN at once when the pipe is full, and that byte is discarded — a handler must never block; if it sticks, the whole program sticks with it. When the main loop drains the pipe, read hitting EAGAIN tells it the pipe is clean. And poll itself may still be interrupted by a signal; the loop's handling of EINTR is simply to try again — an old friend. This apparatus continues in the multiplexing article of ch04, where epoll will take in the pipe along with timerfd, which turns timer expiries into readable fd events too — you will meet it again. One sentence worth leaving here now: this event-ification is our hand-made imitation in user space; the kernel keeps a native substitute whose name is signalfd, and the next article will summon it to the stage.

### Pattern Three: siglongjmp, Breaking Out

Some scenes are unfit for cleanup — a SIGSEGV handler that wants to hand the flow of execution straight back to the main flow, say. sigsetjmp paired with siglongjmp exists for exactly this; we set savemask to 1 so the signal mask enters the saved set as well. The memory management article's guarded_buffer already played the complete scene: the tail-aligned guard page, the handler's write reporting which byte went out of bounds, and siglongjmp returning the flow to the main loop, with the later experiments continuing in the same process. Go see E4 in [that article](../memory/02-vm-apis.md); we will not redo it here.

> Let us also state C++'s position here clearly: inside a handler, do not count on C++'s runtime services. Destructors will not run just because you jumped away from within a handler; exceptions have no channel of their own; and new goes through malloc, which is off the safety table. The sturdiest posture inside a handler is C's bare calls plus one flag, with all object-lifetime work left to the main loop.

## E6: SIGCHLD — One Merged Bit, Reaping in a Loop

When a child exits, what the kernel sends the parent is SIGCHLD. E1 proved that standard signals do not queue, and once that property meets SIGCHLD, it produces one of the most classic patterns in Unix programming. waitpid's own blocking semantics, status macros, and the use of WNOHANG all belong to [the process creation article](01-fork-exec.md); here we cover only signal-driven reaping.

### Three Children Merging into One Handler Entry

E6.B's arrangement: we masked SIGCHLD, forked three short-lived children and let them exit at once; the three exit signals all squeezed into the same pending bit during the masked period. With the mask lifted:

```text
[E6.B] loop waitpid (classic): 3 kids exit while SIGCHLD blocked
    3 children dead; pending SIGCHLD=1 (one bit, not three)
    handler entries=1, reaped in handler=3 (of 3 kids)
```

Watch the numbers: pending holds a single 1, the handler entered only once, and inside that one entry a `while (waitpid(-1, &st, WNOHANG) > 0)` loop reaped all three children's corpses in one breath. WNOHANG, by the way: it is the switch that refuses to wait dumbly when there is no corpse, returning 0 at once, and the loop therefore exits cleanly.

### The Naive Version Leaves Two Zombies

The counterexample changes to a single waitpid inside the handler, everything else untouched:

```text
[E6.B] single waitpid (naive): 3 kids exit while SIGCHLD blocked
    3 children dead; pending SIGCHLD=1 (one bit, not three)
    handler entries=1, reaped in handler=1 (of 3 kids)
    kid 0 (pid 15165): /proc state = '?'
    kid 1 (pid 15166): /proc state = 'Z'  <- ZOMBIE left behind
    kid 2 (pid 15167): /proc state = 'Z'  <- ZOMBIE left behind
```

Reading on: the handler still entered only once and reaped only one; kid 0 is already gone from /proc, its state reading ?. kid 1 and kid 2, however, are stuck at Z forever. Z is short for zombie — the stage where a process is dead but its exit status remains unclaimed. No next SIGCHLD will ever come: the three exits long merged into the same bit, and a make-up signal does not exist. So the loop is a necessity forced by signals' refusal to queue, not a matter of stylistic preference.

### SA_NOCLDSTOP and SA_NOCLDWAIT

SIGCHLD has two flags of its own. By default, a child's stop, resume, and exit all send SIGCHLD; E6.A's measured counts were three increments at 1, 2, 3. With SA_NOCLDSTOP added, stop and resume both fell silent and the counts stayed at 0, 0; only the exit still fires, and the count returned to 1. SA_NOCLDWAIT takes another road — this stretch in the archive carries the [E6.C] label: the child's exit never lands in zombie at all but simply vanishes; in the experiment /proc's state read ?, waitpid returned -1 with ECHILD (10), and the parent had no corpse to reap. man 2 sigaction adds one more note: POSIX does not specify whether SIGCHLD is still sent under SA_NOCLDWAIT, and on Linux it still is. Setting SIGCHLD's disposition wholesale to SIG_IGN has the same auto-reaping effect on Linux — the sigaction(2) account states it has been legal since POSIX.1-2001. But after SIG_IGN you lose even the chance to reap: the exit code becomes a mystery forever. Whether to do it depends on how much you care.

## E7: Quick-Reference Table and Hard Facts

The hard facts of the signal world — everything measurable has been measured; the rest of the table follows man 7 signal's account, with numbers from this machine's `kill -l` snapshot. Under the x86-64 account we count 62 signals; the real-time portion is left for the next article.

SIGKILL and SIGSTOP can be neither caught nor ignored; the kernel refuses you at sigaction's door:

```text
[E7.1] catch/ignore SIGKILL & SIGSTOP: kernel refuses at sigaction time
    sigaction(SIGKILL , SIG_IGN) -> -1 errno=22 (Invalid argument)
    sigaction(SIGSTOP , SIG_IGN) -> -1 errno=22 (Invalid argument)
    sigaction(SIGTERM , SIG_IGN) -> 0 errno=0 (OK, accepted)
    sigaction(SIGSEGV , SIG_IGN) -> 0 errno=0 (OK, accepted)
```

The gentle closing request, however, can be refused. The SIGTERM that the kill command sends by default was wholly ignored by the child; 300 milliseconds later the child was alive and well and said so. We sent SIGKILL instead — `WIFSIGNALED=1 WTERMSIG=9`, not an inch of room for negotiation:

```text
[E7.2] child ignores SIGTERM, still dies to SIGKILL
    CHILD 15171: SIGTERM ignored, ready
    CHILD: survived a SIGTERM at t=300ms
[E7.2] child end: WIFSIGNALED=1 WTERMSIG=9 (SIGKILL=9)
```

The next hard fact is SIGPIPE; in the archive it is [E7.3]'s measurement. Writing to a pipe whose read end is closed, with the default action, kills the process outright — the [E7.3] line the dead child brought back reads `WIFSIGNALED=1 WTERMSIG=13 (SIGPIPE=13)`. After setting SIGPIPE to SIG_IGN, the same write brings back -1 with EPIPE (errno 32); the process survives, and the error becomes an inspectable error code. Setting SIGPIPE to SIG_IGN and checking EPIPE yourself is the standard idiom in network programs. The world of pipes and FIFOs was unfolded in [the IPC article](03-ipc.md) — that is its home turf.

| Signal | Number | Default action | Catchable/ignorable | Typical use |
|---|---|---|---|---|
| SIGHUP | 1 | Terminate | Yes | Controlling terminal disconnected. The whole group receives a copy when the terminal dies; [the daemon article](02-daemon.md) covers how nohup blocks it |
| SIGINT | 2 | Terminate | Yes | Ctrl-C, the polite exit request to the foreground process group |
| SIGKILL | 9 | Terminate | No | The last resort; this article measured sigaction returning EINVAL outright |
| SIGSEGV | 11 | Terminate + core | Yes | Illegal memory access; see the memory management article for a handler's precise reporting |
| SIGUSR1/2 | 10/12 | Terminate | Yes | User-defined; this article's experimental workhorse |
| SIGPIPE | 13 | Terminate | Yes | Writing to a closed pipe/socket; often set to SIG_IGN with EPIPE checked instead |
| SIGALRM | 14 | Terminate | Yes | alarm/setitimer expiry; E3's experimental workhorse |
| SIGTERM | 15 | Terminate | Yes | The generic termination request; what kill sends by default |
| SIGCHLD | 17 | Ignore | Yes | Child stop/continue/exit; drives E6's reaping |
| SIGSTOP | 19 | Stop | No | Unconditional pause; pair with SIGCONT to resume |

## How the Other Side Looks

Windows holds no async-signal apparatus of this kind; the nearest counterpart is the console event: `SetConsoleCtrlHandler` registers handlers for events like CTRL_C_EVENT, doing a job equivalent to the handler's. The two sides' constraints mirror each other while their starting points are opposite. A Linux handler may wedge itself between any two instructions at any moment — which is why the async-signal-safe list polices it. Windows dispatches the handler into a thread built specifically for it, and the main flow is not interrupted in place — but a deadline follows: events like console closing give the handler a bounded amount of time, and a process that exceeds it is taken away all the same. As for the strike with no room for negotiation, on the Windows side its name is TerminateProcess, a peer of SIGKILL. This comparison lands in two articles on the Windows side: the process article covers the TerminateProcess-versus-SIGKILL half, and the console-events article covers the delivery model and the deadline half; here we note only the direction.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="2"
    title="signal(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/signal.2.html"
  />
  <ReferenceItem
    :id="3"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="4"
    title="signal-safety(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal-safety.7.html"
  />
  <ReferenceItem
    :id="5"
    title="sigprocmask(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigprocmask.2.html"
  />
  <ReferenceItem
    :id="6"
    title="setitimer(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setitimer.2.html"
  />
  <ReferenceItem
    :id="7"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
