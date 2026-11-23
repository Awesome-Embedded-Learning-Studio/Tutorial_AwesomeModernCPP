---
title: "A Panorama of Timers: alarm→setitimer→timer_create→timerfd"
description: "How to choose among Linux's four generations of timer APIs (alarm, setitimer, timer_create, timerfd) and why periodic tasks must not be fudged with sleep — answered head-on by six measured experiments plus two supplementary discrimination rounds: alarm(2) fires at 2000.018 ms with whole-second granularity, one booking per process and a new booking overriding the old (after alarm(1) plus 300 ms, alarm(5) returns a remainder of 1; the rounding-mode re-test: 0.4 s left also reports 1, already expired reports 0, so round-up rules out round-half and truncation), under one workload (2 s wall, about 1 s CPU) setitimer's three rulers fire 200 times for ITIMER_REAL, 101 for ITIMER_VIRTUAL, 96 for ITIMER_PROF (VIRTUAL beating PROF falls within sampling noise at the 10 ms tick; by definition PROF's user+system includes VIRTUAL's pure user — recorded as measured, no hard verdict), all four timer_create notification forms measured (si_code=SI_TIMER with the payload arriving intact, SIGEV_NONE counting down to zero with no signal, SIGEV_THREAD's two callbacks landing on adjacent but different tids — independent proof that glibc spawns a fresh thread per notification, SIGEV_THREAD_ID delivering to a targeted thread while the main thread blocks the same signal undisturbed), with the signal blocked for a 58 ms sleep five expirations yield one sigwaitinfo receipt and timer_getoverrun=4 reporting the four merged expirations, the fork and exec boundary measured: alarm(2) wipes setitimer's 100 ms period into a 1999 ms one-shot (visible in the getitimer reading), turning the man page's claim that alarm() and setitimer(2) share the same timer into a reproducible experiment; after fork all three signal forms read zero in the child; on the exec side alarm and setitimer timers survive per the man page's preserved across execve, confirmed by re-measurement (after exec getitimer reads about 1.999 s left, alarm(0) returns the old remainder 2, and an exec'd child that does not cancel dies at exactly 2 s from the reset default SIGALRM), timer_create is disarmed and deleted per timer_create(2), and timerfd lives through both fork and exec (the exec'd child reads 12 ticks, the accumulation over two armed 600 ms windows); the headline is the drift comparison: for a 1000×1 ms periodic task, the relative sleep loop drifts +78.444 ms (rerun +78.028, per-tick overhead rolling up linearly, with the min tick at 1014.9 µs — even the shortest of a thousand ticks sits above nominal), absolute-expiry compensation lands at +0.077 ms and the timerfd periodic at +0.032 ms (order-of-magnitude statement: about 78 ms for relative sleeping versus below 0.08 ms for absolute anchoring; the short min ticks of 902.9/562.1 µs are catch-up shots) — plus full evidence for the correct way to write a periodic scheduler and a selection matrix for the four tools"
chapter: 8
order: 3
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "信号(上):sigaction 与异步信号安全"
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
related:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "C++20 std::chrono 深度:日历与时区"
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "信号(下):实时信号、signalfd 与 pidfd"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 并发
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/time/03-timer-family.md
  source_hash: 189ba50a660a0f8004c48bcb0aa696710d2697280c8f4c7078c0278726554e7a
  translated_at: '2026-11-22T15:40:38+00:00'
  engine: glm-5.3
  token_count: 18114
---

# A Panorama of Timers: alarm→setitimer→timer_create→timerfd

Suppose you have a task that does a piece of work every millisecond — sampling, refreshing state, whatever. The most intuitive implementation takes ten seconds to type out, and its skeleton is one we have all written: "sleep 1 ms, work, sleep again." A thousand ticks at a nominal 1 ms each should take exactly one second, but put a stopwatch on it — how long does it run? On this machine the author measured 1.078 seconds, and a rerun returned 1.078 again. The extra 78 milliseconds live in no line of code; they grow out of how the loop is organized. Where those 78 ms come from, and how to press them below 0.08 ms, is exactly what the comparison experiment closing this article answers head-on. Before that, we must get properly acquainted with Linux's four generations of timers — alarm, setitimer, timer_create, timerfd — and for granularity, notification forms, survival across processes, and merge semantics, every single item is settled by measurement.

The time chapter's first two articles were both about reading time: the [clock-sources article](./01-clock-sources.md) measured the clocks — which clock measures which kind of time, and how accurate the readings are, all with numbers — while the [chrono calendar article](./02-chrono-calendar.md) read one instant into four columns: epoch, UTC, and local presentation. Clocks answer what time it is now; timers answer how you get called when the moment arrives. The chrono article's closing said the next one would switch to acting on time, and this is it. The four tools in order of birth: alarm is an old-Unix whole-second legacy; setitimer comes from the BSD era, brought granularity down to microseconds and added two rulers that count CPU time; timer_create comes from POSIX's real-time extensions and is a proper object-style timer; timerfd, since Linux 2.6.25, turns the timer into an fd so it can join the same table as epoll. The two signal articles covered sigaction, sigwaitinfo, and signal masks head-on, so this article treats them as ready-made tools; timerfd's own course (merge counting, rescheduling semantics, fdinfo, expiry precision) was finished in the [multiplexing chapter's timerfd article](../io-multiplexing/02-timerfd-eventfd.md), so here it appears only as one member of the family for the selection comparison — numbers are quoted from there and not re-measured.

The experiments are numbered E1 through E6, mapping one-to-one onto t1 through t6 in the archive; the code and raw output live under `code/volumn_codes/vol8/systems-programming/linux/time/03-timer-family/` in the repository, and the README carries per-experiment reproduction commands, so you can reconcile at any time. Each E belongs to this article alone — same-numbered E-series in the previous two articles are unrelated — so when browsing the archive, trust the directory. Output blocks in the body are mostly excerpts with deleted lines marked `...`; when reconciling, the archive is authoritative.

The environment baseline, stated as usual so every later number can be checked against it: the experiments ran on the author's WSL2 with the 6.18.33.2-microsoft-standard-WSL2 kernel build, an AMD Ryzen 7 9700X CPU, g++ 16.2.1, glibc 2.44, compiled uniformly with `-std=c++20 -O2 -Wall -Wextra`; t2 through t5 involve threads and each adds `-pthread` at compile time, t1 and t6 do not, and t3 additionally adds `-D_GNU_SOURCE` (the reason is detailed in E3d); zero warnings across all experiments. Timing everywhere uses CLOCK_MONOTONIC — the choice measured out in the clock-sources article — carried over wholesale here. All `.out` files come from the same run on 2026-10-04; t6 got an extra rerun whose output is archived as `t6_rerun.out`, and t1b and t5b are two discrimination experiments added in a revision round, from the same machine on the same day. Signal counts and tick counts are stable across runs while microsecond-scale instants shift run to run; what we cite are orders of magnitude and shapes, not any single run's exact values.

## E1: alarm: Whole-Second Granularity, One Booking per Process

alarm is the oldest of the four; man 2 alarm's function reads out in one sentence: "after the specified number of seconds, send this process a SIGALRM." We measure its punctuality with t1:

```text
== E1a: alarm(2) 到点有多准 ==
alarm(2) 从下达到 SIGALRM: 2000.018 ms(粒度只有整秒,亚秒需求它管不了)
```

Punctual it is — 2000.018 ms leaves nothing to pick at. But its parameter is whole seconds: for 2.5 seconds or 500 milliseconds there is simply nowhere in the interface to write it. Sub-second needs have to wait for setitimer to take the stage.

### Rebooking is not stacking; canceling zeroes it out

alarm's return value is the seconds remaining on the old booking, so let us see how it reports that remainder: set `alarm(1)`, sleep a full 300 ms, then call `alarm(5)` and read its return value, and finally cancel it with `alarm(0)`:

```text
== E1b: 再约一次是「改约」不是「加约」 ==
alarm(1) 过 300ms 后再 alarm(5),返回剩余=1 s(内核向上取整到整秒,300ms 后
约剩 0.7s,报成 1)
alarm(0) 撤销后过了 1.2s,SIGALRM 次数=0(0=撤销生效,第二个约定从未到点)
```

Look at the reading: what actually remained was about 0.7 s, yet it reported 1. That alone cannot distinguish the rounding rule — round-half-up would also give 1 — so t1b added two discrimination points plus an already-expired control:

```text
[1] alarm(2) 后过 300.1 ms,实际剩约 1.70 s,alarm(5) 返回 2(向上取整给 2,截断会给 1)
[2] alarm(2) 后过 1600.1 ms,实际剩约 0.40 s,alarm(5) 返回 1(向上取整给 1,四舍五入会给 0)
[3] alarm(1) 后过 1200ms(约定已到点),alarm(5) 返回 0,SIGALRM 到货 1 次
```

All three points agree: the kernel rounds the remainder up to whole seconds, not round-half and not truncation. The return value works as a reference but cannot be trusted for precise renewal arithmetic. More important is the behavior: a new booking displaces the old — the moment `alarm(5)` takes effect, the first one is gone — and `alarm(0)` is a cancellation, with zero signals in the following 1.2 seconds, cleaned up completely.

### One booking per process

alarm's state is recorded on the process, and only one booking can hang there at a time. It cannot run two unrelated timeouts in parallel — its second hard limit. The places it still works today are whole-second-granularity watchdogs and read timeouts with SA_RESTART turned off; old code is full of it. Newly written code mostly moves straight on to the latter three tools.

## E2: setitimer: One Workload, Three Rulers

setitimer brings granularity from whole seconds down to microseconds. In the itimerval structure, `it_value` governs how long until the first expiration and `it_interval` how long between subsequent ticks; zeroing both fields together completes a cancellation. It also hands us three rulers at once, chosen by the `which` parameter: ITIMER_REAL counts wall-clock time, including time the process spends asleep, and sends SIGALRM on expiry; ITIMER_VIRTUAL counts only this process's user-mode CPU time and sends SIGVTALRM; ITIMER_PROF counts user mode plus system mode and sends SIGPROF.

We designed one workload and ran it three times: 2 seconds of wall clock with about 1 second of CPU, alternating 10 ms of busy spinning with 10 ms of sleep, all three rulers configured to a 10 ms tick:

```text
== E2a: 同一负载,三种计时口径各自的计数 ==
ITIMER_REAL    墙钟 2000 ms,CPU 1002 ms,信号 200 次 → 每 10ms 一档,数的是墙钟(睡也数)
ITIMER_VIRTUAL 墙钟 2002 ms,CPU 1000 ms,信号 101 次 → 每 10ms 一档,数的是本进程用户态(睡不数,系统态也不数)
ITIMER_PROF    墙钟 2010 ms,CPU 1002 ms,信号 96 次 → 每 10ms 一档,数的是用户态+系统态(睡不数)
```

REAL's number is immaculate: it counts wall clock, and 2000 ms of wall clock at a 10 ms tick fires exactly 200 times — not a hair off, because the periodic tick is anchored to the absolute wall-clock scale, an anchor whose power we meet again in E6. VIRTUAL and PROF both counted the same stretch of roughly 1000 ms of CPU time and landed around one hundred ticks — 101 and 96 as measured — and the CPU time they count itself fluctuates slightly. One thing to record honestly: by definition, PROF's user-plus-system includes VIRTUAL's pure user time, yet this single run produced 96 below 101; we book the 5-tick gap into sampling noise at the 10 ms tick width. Counted over the long run, PROF should not fall below VIRTUAL by definition, and we do not issue a hard verdict off one run's numbers. The clock-sources article's E5 already showed us the granularity gap between the two CPU-time scales — 290 ms in ticks versus 298 ms in nanoseconds — and the few ticks of fluctuation here are the same phenomenon showing up in signals.

A word on uses: VIRTUAL and PROF are the foundation of performance profiling — gprof samples by them. The one-shot semantics were also verified:

```text
== E2b: 一次性(it_interval=0)与撤销 ==
一次性 50ms 后睡 150ms: 信号 1 次(1=响一次自停,不自动续期)
设 300ms 后立刻撤销,再睡 400ms: 信号 0 次(0=撤销生效)
```

The semantics: leaving `it_interval` at zero yields a one-shot that stops itself after firing once. The proper cancellation is to set an all-zero itimerval; t2 additionally used Linux's private spelling, passing NULL directly as `new_value`, which the kernel treats as all zeros. The man page's advice on this spelling is blunt — the original words are `Don't use this Linux misfeature: it is nonportable and unnecessary.` — nonportable and unnecessary; we archived it purely for demonstration, and in your own code take the proper road of the all-zero struct. The ownership limit: one booking per which per process, three at the absolute most, and that is the ceiling.

## E3: timer_create: One Timer, Four Notification Forms

Between setitimer and timer_create lies a generational update: the timer went from a process attribute to an object. A `timer_t` sits in our hand, and we can create as many as we like; man 2 timer_create states that each timer pre-reserves one queued real-time signal in the kernel, with the count capped by RLIMIT_SIGPENDING. Period configuration moves to itimerspec — structurally the same as itimerval, with it_value and it_interval now expressed in nanoseconds — and the clock it hangs on is choosable too, the same question as clock selection in the clock-sources article: for measuring periods you want a clock immune to jumps, so we use MONOTONIC throughout. The biggest upgrade lands on the notification forms: sigevent (the structure passed along with timer_create that tells the kernel how to notify us at expiry) offers four, and we run them one by one.

### SIGEV_SIGNAL: the signal carries a payload

```text
== E3a: SIGEV_SIGNAL(100ms × 3,载荷 20261004) ==
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
```

In the [first signal article](../process/04-signal-basic.md)'s E2.1 we measured si_code from three origins: raise gives SI_TKILL, an external kill gives SI_USER, and setitimer expiry gives SI_KERNEL. timer_create's SIGEV_SIGNAL adds a fourth — SI_TIMER is its exclusive code. si_value is where it pulls away from alarm: the 20261004 packed in at departure arrived untouched. With a payload, multiple timers can share one signal number and the handler can demultiplex on si_value — precisely the multi-way timing alarm cannot do.

### SIGEV_NONE: counts down silently

```text
== E3b: SIGEV_NONE(300ms 一次性,每 50ms 问一次剩多少) ==
  墙钟  50.1 ms: it_value 剩 0.249 ms
...
  墙钟 250.5 ms: it_value 剩 0.049 ms
  墙钟 300.6 ms: it_value 剩 0.000 ms
  墙钟 350.7 ms: it_value 剩 0.000 ms
  到期后 it_value 归零、无任何信号 —— 到没到点,全靠自己看表
```

The archive's printout glues seconds and milliseconds into one label; we read 0.249 as 0 seconds 249 milliseconds. The remainder marches down step by step with the wall clock; once the 300 ms expiry passes, the reading hits zero, and every later query still returns zero — no signal was ever sent. Whether the moment has arrived is entirely ours to check with `timer_gettime`. It fits programs already spinning in a polling loop — that hand is never idle anyway.

### SIGEV_THREAD: the callback runs in a freshly spawned thread each time

```text
== E3c: SIGEV_THREAD(120ms × 2) ==
  SIGEV_THREAD 回调: 载荷=77,跑在 tid 1046441
  SIGEV_THREAD 回调: 载荷=77,跑在 tid 1046442
```

SIGEV_THREAD's literal promise is to run a callback function at expiry. man 2 timer_create only says the machinery is mostly implemented in glibc — the kernel has no native thread notification — and whether threads are reused is not something we guess at; we measure tids: the two callbacks ran on two adjacent tids, pointedly not the same one. At least in this glibc 2.44 implementation, every notification spawns a new thread to run the callback. The engineering consequence follows immediately: if the callback touches shared state, write it as multithreaded code — the locks and atomics it deserves, none omitted. This is not a callback from a single-threaded world.

### SIGEV_THREAD_ID: delivering to one targeted thread

The fourth form is Linux-specific. The `sigev_notify_thread_id` field hides behind a GNU extension in the header; in code its true body is the macro-expanded `_sigev_un._tid`. g++ defines `_GNU_SOURCE` by default, and t3 spells it out explicitly for safety — it is when gcc compiles C that you genuinely cannot do without it. This form stops the signal from being receivable by any thread in the process and delivers it only to the thread we designate. The targeted spelling, a few core lines excerpted from t3:

```cpp
sev.sigev_notify = SIGEV_THREAD_ID;   // Linux 专有,字段要 _GNU_SOURCE
sev.sigev_signo   = SIGUSR1;
sev._sigev_un._tid = target_tid.load();   // sigev_notify_thread_id 宏展开后的本体,tid 写在这里
// 主线程把 SIGUSR1 屏蔽,证明信号只进目标线程
sigset_t block{};
sigemptyset(&block);
sigaddset(&block, SIGUSR1);
pthread_sigmask(SIG_BLOCK, &block, nullptr);
```

We ran it, and the observation:

```text
== E3d: SIGEV_THREAD_ID(定向投递,150ms × 2) ==
  SIGEV_THREAD_ID 到货: 目标线程自己处理 tid 1046443
  SIGEV_THREAD_ID 到货: 目标线程自己处理 tid 1046443
  两次都由目标线程处理;主线程全程屏蔽同一个信号、一次没被吵醒 —— 定向投递成立
  (计数核对: 主线程侧 g_sig_hits=3 是 E3a 的 3 次,工作线程侧 g_tid_hits=2)
```

The main thread blocked SIGUSR1 entirely; both deliveries landed on the worker thread's tid, and a cross-check afterward confirmed the main thread was never woken once. In multithreaded programs this is the way to corral a timer's impact into a designated thread, so the thread running the event loop is never stirred by EINTR.

## E4: Missed Expirations: No Queueing, Folded into One Integer

The [first signal article](../process/04-signal-basic.md)'s E1 measured this: standard signals do not queue — three sent back to back during a block leave just one pending bit. The timer's signal forms fall under the same restriction, and the shape it takes is expiration merging. t4 shows what it looks like: with SIGEV_SIGNAL ticking every 10 ms, we block SIGUSR1, leave the first expiration hanging, and sleep a full 58 ms:

```text
10ms 一档,屏蔽信号睡 58.1 ms 后取走信号:
  sigwaitinfo 收到 1 次(si_code=SI_TIMER) —— 5 档到期只送来 1 个信号
  timer_getoverrun = 4(这一信号到货时,被合并掉的多余档数)
  核对: 已到期的档数 5 = 送出的 1 + 合并掉的 4
...
```

Reconcile the numbers: in 58 ms, five ticks should theoretically have expired; sigwaitinfo received exactly one signal, `timer_getoverrun` reported 4, and 1 delivered plus 4 merged adds up to 5. That is the semantics, spelled out: signals are a finite resource, the kernel does not queue make-up deliveries, and however late you were, the loss is folded into one integer handed back to you. For make-up delivery without a single tick lost, we choose timerfd — read returns the accumulated tick count, and the [timerfd article](../io-multiplexing/02-timerfd-eventfd.md)'s E3 measured a 350 ms sleep over a 100 ms period reporting 3 in one read — not re-measured here.

## E5: After fork and exec: Who Survives

### alarm and setitimer share one kernel timer

The NOTES of man 2 alarm carry an assertion: `alarm() and setitimer(2) share the same timer; calls to one will interfere with use of the other.` The documentation says the two tools share one timer and interfere with each other, so we turned it into a visible experiment: we armed a 100 ms period with setitimer — `getitimer` read `it_value=99ms, it_interval=100ms` — then made exactly one call, `alarm(2)`. Reading again, this is what we saw:

```text
== E5a: 同一个内核计时器 —— alarm 和 setitimer(ITIMER_REAL) 共用一笔 ==
setitimer 武装后: it_value=99ms it_interval=100ms
调 alarm(2) 之后: it_value=1999ms it_interval=0ms —— 周期被抹掉,
两件工具共用同一个计时器,后写的顶掉先写的
```

Look at that: `it_interval` was wiped to 0, the 100 ms period was gone, and `it_value` turned into a 1999 ms one-shot. alarm's true body is just the seconds-scale one-shot spelling of ITIMER_REAL — calling it actually writes the same kernel timer. The engineering lesson is concrete: in one process, an alarm watchdog on one side and a setitimer(ITIMER_REAL) period source on the other — the later write displaces the earlier one silently, without even an error for our trouble.

### After fork: all three signal forms read zero

The fork-and-exec probe t5 walks three steps: the first two ask about fork, the third asks about exec. Step one asks alarm alone: after `alarm(2)` we forked once, and in the child we both canceled with `alarm(0)` and read the remainder — the returned 0 is what proves it inherited no live booking. Step two armed a 100 ms period each for setitimer and timer_create before forking, and parent and child each observed a 600 ms window:

```text
...
[fork 子进程 A] 子进程里 alarm(0) 返回 0 s —— 0 说明 alarm 没跟过来
...
[fork 子进程 B] SIGALRM(setitimer) 0 次,SIGUSR1(timer_create) 0 次 —— 都是 0,两件都没跟过来
[fork 父进程] 同窗口内 SIGALRM(setitimer) 6 次,SIGUSR1(timer_create) 6 次 —— 武装只留在父进程
```

The child read zeros across the board while the parent fired 6 of each: the arming stayed with the parent, matching the man page's ruling — timers are recorded on the process, and a forked child is a freshly created one that carries no old timers. timerfd was armed in probe two as well, but it does not travel by signal, so we look at it together with the exec step.

### After exec: timers preserved, dispositions reset

Step three cleared the signal forms off the stage, leaving only a timerfd (flags=0, created without TFD_CLOEXEC); after the fork we re-launched the program with execl, the child slept a full 600 ms, then read the inherited fd:

```text
...
[exec 子进程] 继承的 timerfd fd=3,读到档数 12 —— fd 活过了 exec
```

We saw a reading of 12 ticks: the fd lived through fork and through exec. The arithmetic of 12 also adds up: this fd had been counting ever since probe two armed it, and two 600 ms windows straddling the exec account for exactly 12.

t5's original staging had cleared the signal forms away, assuming they would not survive exec; the review dug up that assumption, and the man pages answer the opposite way. alarm(2)'s NOTES say `Alarms created by alarm() are preserved across execve(2) and are not inherited by children created via fork(2).`, and getitimer(2)'s NOTES match: `A child created via fork(2) does not inherit its parent's interval timers. Interval timers are preserved across an execve(2).` The alarm and itimer timers themselves both live through exec. We measured the exec hop alone in t5b (the arming placed in the child after the fork, which rules out the noise of fork non-inheritance):

```text
[A·exec 后] setitimer 武装的 2s 一次性:getitimer 读到剩 1.999111 s(interval=0ms)—— itimer 活过了 exec
[B·exec 后] alarm(2) 武装:alarm(0) 返回旧剩余 2 s —— alarm 活过了 exec
[C·父进程] 不撤的 exec 子进程结局:WIFSIGNALED=1 WTERMSIG=14(SIGALRM=14)—— 计时器活着跨过了 exec,处置复位成默认,到点就是它
```

In the post-exec child, getitimer read back about 1.999 seconds remaining and alarm(0) returned the old remainder 2: both lived across exec. What truly did not come along is the disposition: execve(2) also states that dispositions of caught signals reset to default during exec, so a SIGALRM crossing exec lands on the default action, and at expiry the outcome is termination. t5b's probe C demonstrated exactly that outcome — the un-cancelled child died of SIGALRM at the two-second mark. The timer survived, only to become a default termination at expiry inside the new program; in a program you take over after an exec, the first thing to confirm is whether a timer is still running.

timer_create's POSIX timer needs to be singled out: timer_create(2) states plainly that it is disarmed and deleted in execve — it is not on the survival list.

We lay the four tools' fate across fork and exec into one table, labeling measurement and documentation separately:

| Tool | After fork | After exec |
|---|---|---|
| alarm | child gets nothing (measured: alarm(0) returns 0) | preserved, disposition reset (per alarm(2); measured: old remainder 2) |
| setitimer | child gets nothing (measured: zero signals over 600 ms) | preserved, disposition reset (per getitimer(2); measured: about 1.999 s left) |
| timer_create | child gets nothing (measured: zero signals over 600 ms) | disarmed and deleted (per timer_create(2)) |
| timerfd | fd duplicated into the child, timing never stops | survives (measured: exec'd child reads 12 ticks), unless created with TFD_CLOEXEC |

Whether a timer should survive across processes is a real, weighted vote in our selection.

## E6: Why Periodic Tasks Must Not Be Fudged with Sleep: The Drift Comparison

The 78 milliseconds owed at the article's opening — we now hold every tool needed to measure them properly. t6 gives three implementations of the same periodic task, each running 1000 ticks at a nominal 1 ms per tick: implementation A is a relative-sleep loop, sleeping 1 ms per tick via `clock_nanosleep`'s relative mode and then working; implementation B is absolute-expiry compensation, maintaining an absolute scale of `next += 1ms` and sleeping onto that mark with the TIMER_ABSTIME flag; implementation C is timerfd's periodic tick, polling for readability and reading off the tick count:

```text
== E6a: 相对睡眠循环(sleep 1ms × 1000) ==
A: 相对睡眠        总长  1078.44 ms(标称 1000 ms,漂 +78.444 ms)
                      档间隔: min=1014.9 中位=1078.3 max=1307.3 µs
...
== E6b: 绝对到期补偿(下一档 = 起点 + n×1ms) ==
B: 绝对到期        总长  1000.08 ms(标称 1000 ms,漂  +0.077 ms)
                      档间隔: min=902.9 中位=1000.0 max=1092.4 µs
...
== E6c: timerfd 周期档(1ms interval × 1000) ==
C: timerfd             总长  1000.03 ms(标称 1000 ms,漂  +0.032 ms)
                      档间隔: min=562.1 中位=999.8 max=1434.7 µs
  内核按绝对刻度摆档、错过的档折数补报(本次累计 ticks=1000,理论 1000,
  多出的部分是读间隔内合并的补拍)
```

The rerun round, archived as `t6_rerun.out`, moved the three drifts to +78.028, +0.075, and +0.029 ms — same order of magnitude, same shape as the first round. So we state it by order of magnitude: relative sleep drifts about 78 ms, both absolute-anchored implementations stay below 0.08 ms, and three orders of magnitude separate them.

The mechanism behind the gap, read from A's median: the true per-tick interval in relative sleep has a median of 1078.3 µs, and the extra ~78 µs is the cost of wakeup, scheduling, and the loop itself. The crux of relative mode: however late this tick wakes, the next tick's start shifts later by exactly that much, so the cost rolls forward all the way down, accumulating linearly over a thousand ticks into 78 ms. Verify with the numbers: about 78 µs per tick times one thousand ticks computes to exactly 78 ms, matching the total drift. And the min: 1014.9 µs — even the shortest of a thousand ticks exceeds nominal, because this implementation only adds and never compensates.

Implementation B moves the anchor onto absolute grid positions: the next tick's expiry is no longer measured from the previous tick's actual wakeup — the grid is the scale point at start plus n×1 ms. The few microseconds of late waking do not roll into the next tick; its expiry stays at the whole-grid position, occasionally even catching up the previous tick's shortfall. Total drift shrinks to 0.077 ms, and min 902.9 µs falling below 1000 µs is exactly the trace of catch-up: one tick wakes late, the next still arrives at the original scale point, and the interval contracts back.

Implementation C hands the whole job of placing ticks to the kernel: timerfd's periodic tick is likewise anchored to the absolute scale, and drift lands at 0.032 ms. The min of 562.1 µs digs deeper than B — the shape of an occasional poll-plus-read tick exceeding 1 ms with the next tick caught up at the original scale point; what the archive output calls catch-up merged within the read interval refers to exactly this. Both rounds' accumulated ticks came to exactly 1000, every tick accounted for. Expiry precision is not re-measured — the [timerfd article](../io-multiplexing/02-timerfd-eventfd.md)'s E4 measured a median of 999.3 µs at the 1 ms tick, and the rerun gave the same value. One caveat stated plainly: these are numbers from an unloaded process; under heavier load the max figures widen, and our comparison holds only under equally unloaded conditions.

So here is the correct way to write a periodic scheduler, excerpted from t6's implementation B:

```cpp
uint64_t next = t0;                     // 锚定在起点的绝对刻度上
for (int i = 0; i < kN; ++i) {
    next += kPeriodNs;                  // 第 n 档 = 起点 + n × period
    timespec req{};
    req.tv_sec  = time_t(next / 1000000000ull);
    req.tv_nsec = long(next % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, nullptr) == EINTR) ;
    // 干活:晚醒的微秒不滚进下一档
}
```

The EINTR retry is an old move we already met in the [error-handling article](../../thinking/02-error-paradigm.md). The timerfd implementation already ran complete in E6c; how it cooperates with signals inside an event loop was shown once by the 150 ms heartbeat in the [second signal article](../process/05-signal-advanced.md), and how it shares one table with pipes and eventfd was shown in the [timerfd article](../io-multiplexing/02-timerfd-eventfd.md)'s E5. When is A good enough? Coarse timing — a UI refreshing every 500 ms where nobody begrudges a few tens of milliseconds of drift — is easiest to write as a sleep loop. But wherever the beat matters, where sampling demands alignment and protocols demand punctuality, do not fudge it with sleep: entrust the rule that tick n should expire at n×period to the monotonic clock's absolute scale, or to the kernel.

## How to Choose Among the Four Tools

We have now seen all four generations, and every selection criterion lives in the measurements above; gathered into one table:

| Dimension | alarm | setitimer | timer_create | timerfd |
|---|---|---|---|---|
| Granularity | whole seconds | microseconds | nanoseconds | nanoseconds |
| Bookings per process | one | three (one per accounting mode) | many, capped by RLIMIT_SIGPENDING | many, one per fd |
| How expiry is announced | bare SIGALRM | three fixed signals | four forms, with payload and targeting | fd becomes readable, read reports tick count |
| Across fork/exec | not inherited by fork, preserved across exec with disposition reset (measured) | not inherited by fork, preserved across exec with disposition reset (measured) | neither survives; exec also disarms (per documentation) | stopped by neither (without CLOEXEC, measured) |
| Missed ticks | merged and lost | merged and lost | getoverrun reports the merged count | read accumulates, not one tick lost |

Read the table by need. For coarse whole-second timeouts and maintaining old code, alarm does the job — just remember it shares one kernel timer with setitimer. For profiling that counts CPU time, the proper road is VIRTUAL and PROF; modern profilers mostly ride perf's event counting, and VIRTUAL and PROF are the earliest shape of that idea. If you need nanosecond granularity, many concurrent timers, payloads, and targeted delivery, timer_create is the most complete of the four, at the price of never escaping the signal machinery's constraints. If the timer must join an event loop, make up missed ticks, or survive fork or exec, timerfd takes all of it by itself — which is why it is the default first choice among the four generations.

## How the Other Side Sees It

Windows' most handy sleep is Sleep, and its precision is governed by the system timer resolution: the [shared-memory article](../../windows/memory/02-shared-mem.md) measured Sleep(5) on the Windows side of this same machine actually sleeping about 12.6 ms — the default resolution is one 15.6 ms step, timeBeginPeriod was not adjusted during the experiment, and the 5 ms request was rounded up to the next step. Against this article's E6: on the Linux side, a 1 ms periodic task holds drift below 0.08 ms, and timerfd's median expiry is 999.3 µs.

The waitable timer object is SetWaitableTimer, whose notification channel runs over APC (Asynchronous Procedure Call); the [console-events-and-APC article](../../windows/process/02-console-apc.md) quoted the documentation's own words, and ReadFileEx's completion routines were cashed in in the [OVERLAPPED article](../../windows/async-io/01-overlapped.md). SetWaitableTimer itself has never had a head-on experiment on either side, so we record it by documentation: its handle is waitable and can join a WaitForMultipleObjects wait array — the same design orientation as timerfd joining epoll — and since Windows 10 1803 there is a high-resolution variant `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, likewise recorded by documentation.

The time chapter's three articles are now complete: the clock-sources article measured the clocks, the chrono calendar article read time into human-readable columns, and this one took full stock of the four tools that act at an appointed moment. The overview route's chapter on tty and raw mode is our next stop. The Windows-side mirror articles have not been started; for now, the cross-side comparison is carried by each article's "how the other side sees it" section.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="alarm(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/alarm.2.html"
  />
  <ReferenceItem
    :id="2"
    title="getitimer(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getitimer.2.html"
  />
  <ReferenceItem
    :id="3"
    title="timer_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timer_create.2.html"
  />
  <ReferenceItem
    :id="4"
    title="timer_getoverrun(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timer_getoverrun.2.html"
  />
  <ReferenceItem
    :id="5"
    title="timerfd_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timerfd_create.2.html"
  />
  <ReferenceItem
    :id="6"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
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
