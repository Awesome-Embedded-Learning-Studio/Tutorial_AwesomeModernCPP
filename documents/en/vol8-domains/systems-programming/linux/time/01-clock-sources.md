---
title: "Clock Sources and the POSIX Time APIs: Nine Clocks in One Machine"
description: "All nine clockids brought onto one machine and read one by one: a clock-family census (the two COARSE clocks' getres being 4ms, back-inferring a 250Hz kernel tick while sysconf's CLK_TCK reports 100 — two conventions kept apart; TAI−REALTIME being exactly 37s of accumulated leap seconds; BOOTTIME−MONOTONIC differing by only 20ns, cross-checked against /proc/uptime; RAW−MONOTONIC's frozen −106.4ms divergence; adjtimex read-only reporting synced), the separation of resolution from precision (getres reporting 1ns while one million back-to-back reads show a median adjacent gap of 20ns and p99 of 21ns, COARSE yielding only 3 distinct values across a million reads, per-read cost 16.2ns versus 2.0ns), continuity (MONOTONIC showing zero regressions across two million reads; a ten-second 1kHz passive watch showing a REALTIME−MONOTONIC offset peak-to-trough of 23.3µs with zero steps; RAW's divergence moving only 2.2µs right now yet accumulating 106.4ms, the bulk from an early-boot historical frequency adjustment), vDSO cashing in the overview's ticket-free promise ([vvar]/[vvar_vclock]/[vdso] present in maps, vDSO at 16.9ns versus a real syscall at 168.9ns — a 10x gap — and strace attached showing zero clock_gettime entries against two million), CPU time in three conventions (the main thread sleeping 300ms while its own thread clock gains only 93µs; the process clock at 298.0ms, getrusage at 298.1ms, the tick convention at 290ms — granularity differences plain to see), the wall-time presentation chain (one epoch through gmtime_r/localtime_r, switching TZ swapping rules but not time), closing with the Windows-side GetTickCount64/QueryPerformanceCounter and Sleep(5) actually sleeping 12.6ms under the default 15.6ms quantum"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
related:
  - "timerfd 与 eventfd:时间与事件的 fd 化"
  - "系统编程总纲:用户态、内核与两大阵营的地图"
  - "C++20 std::chrono 深度:日历与时区"
  - "定时器全景:alarm→setitimer→timer_create→timerfd"
  - "chrono：duration、时钟与 C++20 日历"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/time/01-clock-sources.md
  source_hash: bf8032355461dac529c3f84a6c79a26fb2ece16787dc7b52f035cbab47d64eed
  translated_at: '2026-11-22T15:47:43+00:00'
  engine: glm-5.3
  token_count: 17423
---
# Clock Sources and the POSIX Time APIs: Nine Clocks in One Machine

When we created a timer in the [timerfd article](../io-multiplexing/02-timerfd-eventfd.md), the first argument to `timerfd_create` was CLOCK_MONOTONIC — passed over in a single sentence at the time. In vol3's [chrono lesson](../../../../vol3-standard-library/time-numeric/58-chrono.md), steady_clock is likewise an old friend we name without thinking. But take that parameter seriously as a question: how many clocks actually hide inside one machine? What exactly is the difference between CLOCK_REALTIME and CLOCK_MONOTONIC? Why does everyone insist you use a monotonic clock to measure elapsed time? These names look like an ordinary enum, but behind them sit several clocks differing in purpose, precision, and continuity — pick the wrong clock, and the numbers you measure stop being trustworthy.

This article is the foundation of the time chapter: we bring all nine clockids Linux exposes onto the same machine, run a getres-plus-first-read census for each, then go in through the distribution of reads, continuity, the accounting conventions for CPU time, and the presentation chain of wall time, measuring their differences with live steel. Once these measurements are in, the criteria for choosing a clock are in your hands — and earned, not recited.

The experiments are numbered E1 through E6, corresponding to the six file groups t1 through t6 under `code/volumn_codes/vol8/systems-programming/linux/time/01-clock-sources/` in the repository; the code and all raw output are on file. The E numbers in this article belong to this article alone: the next article's chrono-calendar E series and the timer-landscape article after that share numerals with ours but have nothing to do with us — just match the directory when browsing the archive. Most output blocks in the text are excerpts; deleted lines are marked with `...`, and when cross-checking against the archive, the archive is authoritative.

As usual, the environment is stated up front, because the numbers later must be checked against it: the experiments come from my WSL2, the kernel is build 6.18.33.2-microsoft-standard-WSL2, the CPU is an AMD Ryzen 7 9700X, g++ is version 16.2.1, compilation is uniformly `-std=c++20 -O2 -Wall -Wextra` (t5 adds `-pthread`), glibc is version 2.44, and every experiment's warning count is zero. Timing all travels CLOCK_MONOTONIC; the timing-heavy E2 and E4 were each rerun once more, with the second round's output stored in the archive as `t2_rerun.out` and `t4_rerun.out`. All `.out` files come from the same run on 2026-10-04; second and nanosecond readings get a fresh batch every round — what we cite is the magnitude and the shape of distributions, never any specific moment.

The term clock source deserves a couple of words on its own, since it sits in this article's title. clockid is the API we see from user space; the character underneath that actually counts time in the kernel is called the clock source — the machinery of hardware counters. We query its selection through two files under `/sys/devices/system/clocksource/clocksource0/`: current_clocksource reports tsc, and available lists tsc, hyperv_clocksource_tsc_page, hyperv_clocksource_msr, and acpi_pm — four names, all present. The two hyperv-prefixed ones are reference clock sources Hyper-V prepares for paravirtualized guests — the Windows host hands the reading machinery straight in — while on bare metal you mostly meet the tsc-plus-hpet combination; this machine shows not a shadow of hpet. WSL2's origins stamp even the clock source, a point we return to in E2.

## E1: A Clock-Family Census: Nine clockids, Each Keeping Its Own Time

We line up the nine clockids that answer to roll call, query each with clock_getres, and read its current moment once; one run of t1 looks like this:

```text
== E1: clock_getres 与时钟族首次读数 ==
CLK_TCK(sysconf)=100  (COARSE 分辨率反推出的 tick 档位见下两行)
CLOCK_REALTIME         getres=0.000000001  now=1791121284.559110361  墙上时间,可被 settimeofday/adjtime 改
CLOCK_REALTIME_COARSE  getres=0.004000000  now=1791121284.556746979  墙上时间的粗读档,分辨率=1/HZ
CLOCK_MONOTONIC        getres=0.000000001  now=39943.706318194  单调钟,不受 settimeofday 步进影响
CLOCK_MONOTONIC_COARSE getres=0.004000000  now=39943.703953129  单调钟的粗读档,分辨率=1/HZ
CLOCK_MONOTONIC_RAW    getres=0.000000001  now=39943.599872894  原始硬件计数,不受 NTP 频率调整影响
CLOCK_BOOTTIME         getres=0.000000001  now=39943.706318876  单调钟,挂起期间也在走
CLOCK_TAI              getres=0.000000001  now=1791121321.559112876  国际原子时刻度,闰秒只往一个方向加
CLOCK_PROCESS_CPUTIME_ID getres=0.000000001  now=0.001620885  进程 CPU 时间(用户+系统)
CLOCK_THREAD_CPUTIME_ID getres=0.000000001  now=0.001622578  线程 CPU 时间(用户+系统)
```

How the two reading columns came about — look at a small helper in the archive's t1; every clockid walks the same pass:

```cpp
void one_line(clockid_t id, const char* name, const char* note) {
    timespec res{}, now{};
    if (clock_getres(id, &res) != 0) {
        std::printf("%-22s getres 失败: %s\n", name, std::strerror(errno));
        return;
    }
    clock_gettime(id, &now);
    std::printf("%-22s getres=%lld.%09lld  now=%lld.%09lld  %s\n",
                name,
                (long long)res.tv_sec, (long long)res.tv_nsec,
                (long long)now.tv_sec, (long long)now.tv_nsec, note);
}
```

One more thing can be matched in passing: REALTIME's reading of 1791121284, converted through a calendar, comes out as 2026-10-04 — the very day named in the environment statement — which we take as a small credential of the archive's freshness.

Grouped by what they record, the nine clocks are easy to tell apart. Readings like 1791121284 record wall time, counted from the 1970 epoch; REALTIME and TAI belong to this class. Readings like 39943 record time elapsed since boot; the whole MONOTONIC family reads in that magnitude — the machine had been up about eleven hours during the experiments. Readings as small as 0.0016 record the CPU time a process has burned so far; we give those their own section, E5. One machine records three different things, all under the name of time, and the clocks fall into three camps.

What jumps out of the table is the two COARSE clocks' getres: both sit at 0.004000000 — four milliseconds, sitting right there in the open. The number's origin is not mysterious: it is the length of the kernel tick; take the reciprocal and the kernel runs at 250Hz. But look back at the first line: sysconf reports CLK_TCK as 100 — one second cut into a hundred ten-millisecond cells. That is a different counting unit exposed to user space, and it is what the tick counts in /proc use. The two conventions often get stirred together in bare-metal discussions; this machine happens to keep them apart: the kernel tick is 250Hz, the user-space cells are 100Hz. Such a split is no bad thing — in E5 we will use the CLK_TCK cells exactly to expose a real granularity gap.

The differences section, plus the passage asking the kernel's clock state via adjtimex(2) in read-only mode, we paste verbatim:

```text
== 同期差值(背靠背读,残差来自读取顺序的先后) ==
TAI - REALTIME       = 37000000020 ns (原子时与 UTC 的整秒差,即闰秒累计数)
BOOTTIME - MONOTONIC = 20 ns (开机以来挂起的累计时长)
RAW - MONOTONIC      = -106445801 ns (0 附近=本机没有 NTP 频率纪律在拉,见 E3)
/proc/uptime         = 39943.700 s 对 BOOTTIME 39943.706323 s,差 -0.006 s
                       (挂起总时长;为 0 即本次开机以来没挂起过)

== adjtimex 只读: 内核时钟纪律状态 ==
status=0x0  freq=-23324 ppm  maxerror=2006 us  esterror=1 us
STA_UNSYNC(0x40) 置位? 否 —— 置位说明内核没有活跃的时间纪律源在调频
```

Each of the three differences has its own show. First, TAI versus REALTIME: the reading is 37000000020 nanoseconds; the integer part differs by 37 seconds, and the 20-nanosecond tail is the residual of two back-to-back reads separated by a few instructions. Those 37 seconds have a pedigree: when UTC and atomic time established their conversion in 1972, they already differed by 10 seconds; 27 later leap seconds each added one more, accumulating to today's 37. CLOCK_TAI runs on the international atomic time scale, where leap seconds do not exist, so the difference between the two is always an integer. Keep this number in mind: the next article's tai_clock will report the same 37, reading the same leap-second data, merely in a different notation.

BOOTTIME minus MONOTONIC reads only 20 nanoseconds, which we simply treat as zero. By the documented semantics, BOOTTIME equals the monotonic clock plus accumulated suspend time: during a laptop's lid-closed stretch, MONOTONIC stops while BOOTTIME keeps walking, and the difference records the accumulated suspension. WSL2's host sleep travels the shutdown route, so suspension is nearly unobservable here; we cross-check with /proc/uptime: 39943.700 seconds against BOOTTIME's 39943.706323 — the differing 0.006 seconds being formatting truncation plus the two reads straying apart in time. Both sides record the same affair: total suspend time since this boot is zero. The keeps-walking-during-suspend semantics we accept per the documentation; this machine simply cannot produce a live suspension scene.

The third line is the RAW versus MONOTONIC divergence, reading −106445801 nanoseconds: over the eleven hours since boot, RAW has fallen 106 milliseconds behind MONOTONIC. The divergence is frozen; its origin we examine in E3. The closing adjtimex (the kernel's interface for adjusting clock rate and stepping) we interrogate read-only for status: status reports 0x0, the STA_UNSYNC flag is not set, meaning an active discipline source inside the kernel is managing the clock's rate. The freq column we decline to take: the kernel's raw field is scaled by 65536, and the program labeled it directly as ppm (parts per million — the customary unit for rate deviation), which is a mislabel; converted back, the true rate correction is under half a ppm, which does square with the quiet offset in E3.

## E2: getres Reports 1ns — a Scale Mark, Not a Precision

In the census table, every clock except the COARSE pair reports getres as 0.000000001. Does this machine really read nanosecond-grade time? Let the experiment speak: read back-to-back one million times and tally the distribution of gaps between adjacent reads — the distribution is the clock's true read granularity. t2's output:

```text
== E2a: 背靠背连读的相邻间隔分布(每次读 kN=1000000) ==
getres 都报 1ns(E1),下面的分布才是真读取粒度:
CLOCK_MONOTONIC        零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=316268 ns
CLOCK_REALTIME         零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=43272 ns
CLOCK_MONOTONIC_RAW    零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=106358 ns
CLOCK_THREAD_CPUTIME_ID 零差=  0.0%  最小非零=330 ns  中位=341 ns  p99=410 ns  max=85031 ns

== E2b: COARSE 的值是台阶 ==
CLOCK_MONOTONIC_COARSE 1000000 次读里只有 3 个不同值;相邻台阶平均间隔 3 ms(getres 见 E1)

== E2c: 每次读取的代价(整段计时/kN) ==
CLOCK_MONOTONIC        1000000 次读总耗 16165887 ns → 16.2 ns/次
CLOCK_MONOTONIC_RAW    1000000 次读总耗 16470994 ns → 16.5 ns/次
CLOCK_MONOTONIC_COARSE 1000000 次读总耗 2002692 ns → 2.0 ns/次
```

The three high-precision clocks share one mold: the median gap is 20 nanoseconds, the smallest nonzero gap 10 nanoseconds, and even p99 lands at just 21 nanoseconds. Read twice in a row, and what separates the two readings is a step of about twenty nanoseconds. The 1 nanosecond getres reports is the scale of the timespec representation: one cell of tv_nsec is one nanosecond, so the high-precision clocks declare their resolution as one nanosecond. The scale mark is a cell of tv_nsec; precision speaks of how far apart readings can truly separate — and separating those two is exactly our job here. The tens to hundreds of thousands of nanoseconds in the max column are scheduling-interruption noise, unrelated to the clock's quality. THREAD_CPUTIME_ID alone runs a grade coarser, with the median at 341 nanoseconds — CPU-time readings are naturally coarser; we note the shape, and it returns in E5.

This machine's clock source — the environment section named tsc. The counter inside the CPU ticks finer than a nanosecond, so the granularity bottleneck is not the counter. The twenty nanoseconds we measure is mostly the labor of the whole road of one read: entering the vDSO (short for virtual Dynamic Shared Object — a small stretch of code and data pages the kernel maps into our process; we face it head-on in E4), reading the counter's value, and doing one multiplication with the conversion parameters — walking that whole road is what the twenty nanoseconds buys.

The truly spectacular line is E2b: one million consecutive reads stepping on only 3 distinct values — a rerun counted merely 4. COARSE's values are staircases: the kernel updates a cached instant once per tick, and between two ticks you may read ten thousand times and get the same number back; the nominal gap between stairs is the same four milliseconds from E1. The reported 3ms average is small-sample jitter: a million consecutive reads plus per-read comparisons span a window of only a few milliseconds, crossing only two or three stairs, so the average drifts. Its cheapness has a reason too: in E2c, MONOTONIC's per-read cost is 16.2 nanoseconds while COARSE reports 2.0 — an eightfold gap. The coarse tier skips the trip to the hardware counting machinery and its conversion on site; it hands you a pre-cached value, and the man page's positioning for it is exactly a fast-and-coarse timestamp. So the palate for clock choice lies here: the fine-read road is MONOTONIC, twenty nanoseconds of granularity per read; the cheap road is COARSE, with the read cost cut by nearly ninety percent.

## E3: Continuity: The Monotonic Clock Never Regresses; RAW and MONOTONIC Carry a Frozen Divergence

When measuring elapsed time, a clock's continuity is the most vital property. We verify it in three layers: is MONOTONIC truly monotonic; is REALTIME steady when nobody touches it; and what story does the difference of RAW and MONOTONIC — that inseparable pair — tell. t3's output follows:

```text
== E3a: MONOTONIC 回退扫描 ==
2000000 次连读,回退次数=0(settimeofday 步进不进 MONOTONIC,这正是它当计时钟的资格)

== E3b: 10 s 窗口偏移监测(1 kHz 采样) ==
REALTIME-MONOTONIC 偏移: min=1791081340852770.5 us  max=1791081340852793.8 us  峰谷差=23.3 us
偏移瞬移(单步变化超 ±100us)次数=0 —— 0 次=窗口内没有发生墙上时间步进
窗口实长 9796.7 ms;RAW-MONOTONIC 从 -106.446 ms 走到 -106.444 ms,变化 2.2 us
  → 当下分歧率 = 0.000 ppm(RAW 不受 NTP 调频,分歧率就是内核频率修正的力度)

== E3c: 开机以来的累计分歧 ==
MONOTONIC=39953.5 s,RAW-MONOTONIC=-106.444 ms → 平均分歧率 -0.003 ppm
```

The monitoring method is not complicated: every millisecond we do the same thing; the sampling loop in the archive's t3 is:

```cpp
timespec rt{}, mono{}, raw{};
clock_gettime(CLOCK_REALTIME, &rt);
clock_gettime(CLOCK_MONOTONIC, &mono);
clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
```

Three clocks read back-to-back, one take each; the offset is the pairwise difference, and the few tens of nanoseconds of read-order skew sit an order below the microsecond steps we watch for.

E3a is the qualifier: two million back-to-back reads, zero regressions. CLOCK_MONOTONIC's monotonicity — the documentation promises it is unaffected by settimeofday-style steps and only ever marches forward — now carries two million measured endorsements. Measure elapsed time with it, and the difference is trustworthy forever; that is the grounds for insisting on a monotonic clock when timing.

REALTIME enjoys no such treatment: the documentation states plainly it can be dialed directly by settimeofday or corrected in steps by adjtimex, so measuring elapsed time with it is dangerous. Without root on this machine, an active clock-dialing experiment was not possible; what we ran instead is a ten-second, one-sample-per-millisecond passive watch: REALTIME's offset against MONOTONIC shows a peak-to-trough difference of 23.3 microseconds, and single-step changes beyond ±100 microseconds — teleportations — we never saw once. The two astronomical numbers for min and max are the 1970-epoch base mixed into the offset; what we read is the peak-to-trough difference, and the 23.3-microsecond jitter mostly comes from the occasional scheduling interruption during sampling. This round of monitoring attests only to the window's calm — steps remain a genuinely possible event, arriving the instant NTP (the network time protocol) corrects, and we state that account honestly.

The tastier lines are RAW's. Within the ten-second window, RAW's offset against MONOTONIC moved from −106.446 to −106.444 milliseconds — a shift of just 2.2 microseconds — while E3c pulls the camera back to boot: over the eleven hours since boot, the accumulated divergence is −106.444 milliseconds. The present movement and the accumulated divergence are completely out of proportion: the bulk was banked early in boot, not being earned now — most likely from the one frequency adjustment WSL2 performed syncing with the host at startup. The mechanism is one sentence: MONOTONIC obeys NTP's frequency discipline and has its rate trimmed, while RAW records hardware counts that synchronize with nobody; the difference between the two clocks is the accumulation of frequency corrections. Textbook treatments of RAW versus MONOTONIC usually can only quote the documentation — here we hold an observable numeric face: that adjustment left a permanent 106 milliseconds between the two clocks.

> A confession about the two ppm lines in the output: t3's formula divided the ratio by an extra factor of one thousand, so the printed 0.000 and −0.003 are each one-thousandth of the true values — do not use them as calibration. This is not the same class of error as E1's freq column: that one erred in dimension, this one in formula. Fortunately this article's judgments do not rest on it but on the raw microseconds and milliseconds above; when you recompute, please work from the raw readings too.

By now the main criteria for choosing a clock are all in; we lay them on the table in passing: for elapsed-time and timeout work, we take MONOTONIC, its continuity carrying measured endorsement. For programs that must count suspension, we take BOOTTIME, whose semantics throw in the suspended stretch. For comparative measurements wanting pure hardware rate, we take RAW — the price being that it syncs with nobody. For massive cheap timestamps, we take COARSE, remembering its freshness is only tick-grade. REALTIME's place is reserved for wall time itself; E6 shows its proper use.

## E4: vDSO: The Ticket-Free Passage, Redeemed

While discussing syscall costs, the [master overview](../../00-overview.md) left a remark: Linux turns high-frequency calls like clock_gettime into a vDSO — the kernel maps code and data straight into your process, sparing even the trap — an official kernel-issued ticket-free scheme, and one we would meet again in the time chapter. This is the time chapter now, so we redeem that remark into three pieces of visible evidence: the mappings' presence, an order-of-magnitude cost gap, and zero calls once strace is attached.

Evidence one examines the mappings themselves: we read our own `/proc/self/maps` and picked out the lines starting with vdso and vvar:

```text
== E4a: 本进程地址空间里的内核映射(/proc/self/maps) ==
78e194ba3000-78e194ba7000 r--p 00000000 00:00 0                          [vvar]
78e194ba7000-78e194ba9000 r--p 00000000 00:00 0                          [vvar_vclock]
78e194ba9000-78e194bab000 r-xp 00000000 00:00 0                          [vdso]
...
```

Not one of these mapped blocks was requested by us: the kernel laid them down at process startup. The r-xp [vdso] is a code page holding a small kernel-issued clock_gettime. The r--p [vvar] is a data page holding the clock source's conversion parameters, kept up to date by the kernel. The middle [vvar_vclock] is the data page newer kernels split out per clock. glibc's clock_gettime jumps into the vdso's code and computes the time in user space straight from vvar's parameters — the trap spared entirely.

Evidence two examines cost: same function before us, run ten million times in two spellings — one through ordinary `clock_gettime`, the other issuing the same request via `syscall(SYS_clock_gettime, ...)`. The latter is the direct-spelling form that traps by syscall number, bypassing the vDSO by construction — perfect as the real-trap control group.

> The library function syscall(2) exists precisely as the direct-spelling way to hand-issue a syscall by number. New kernel interfaces sometimes wait in vain for a glibc wrapper, and then it is what we use — we already made its acquaintance in the [io_uring article](../io-multiplexing/03-io-uring.md).

```cpp
auto t0 = now_ns();
for (int i = 0; i < kN; ++i) clock_gettime(CLOCK_MONOTONIC, &ts); // vDSO 路径
auto t1 = now_ns();
for (int i = 0; i < kN; ++i)
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); // 强制走 syscall(2) 真陷入
auto t2 = now_ns();
```

```text
== E4b: 每次读取的代价(10000000 次) ==
clock_gettime(vDSO)   : 16.9 ns/次
syscall(SYS_clock_gettime): 168.9 ns/次(强制真陷入)
倍数: 10.0x —— 同一个函数,免陷入的差距就在这里
...
```

The trap-free gap is a full tenfold; a rerun gives 16.6 versus 169.1 — still parked firmly in that magnitude. The two numbers each find a counterpart: the real trap's 168.9 nanoseconds sits in the same order as getpid's 123.72 nanoseconds from the overview — the volume-level intuition planted there checks out here, and we will not re-lecture the cost curve. The vDSO's 16.9 nanoseconds meshes with E2c's 16.2-nanosecond MONOTONIC read cost: the same road measured twice, and the numbers agree.

Evidence three summons strace: it can only intercept real traps, so using it to test whether real traps happen is the most persuasive. For the same program's two million reads, we attach `strace -c -e trace=clock_gettime` once per spelling; the output follows.

```text
$ strace -c -e trace=clock_gettime ./t4_vdso_strace vdso
done: 2000000 次读取,走法=clock_gettime(vDSO),最后一次=38681.365261548

$ strace -c -e trace=clock_gettime ./t4_vdso_strace syscall
done: 2000000 次读取,走法=syscall(2) 真陷入,最后一次=38899.933588504
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00   41.354410          20   2000000           clock_gettime
...
```

In the vdso round, strace spared even the summary table — because not one clock_gettime syscall ever happened. In the syscall round, the table's clock_gettime entry counts exactly two million, matching the read count precisely. A blank summary table is the most direct physical evidence of ticket-free passage. The table's 20-microsecond usecs/call is worth one glance too: that is the cost of strace's own interception-and-logging, dragging a hundred-odd-nanosecond call into microsecond territory — so when measuring performance on a strace-attached program, keep it in mind; we know what we are dealing with.

## E5: CPU Time: Sleeping Does Not Grow It; Burning CPU Does

The remaining two clocks record CPU time: whoever burns more CPU sees their reading grow faster — a different road from time flowing on the wall. t5's arrangement: the main thread sleeps a full 300 milliseconds alongside one purely busy-spinning worker thread, then we cross-check four readings: the process clock, the thread clock, getrusage (the interface that asks the kernel for a process's resource usage), plus a fourth road via /proc/self/stat.

```text
== E5: CPU 时间记在谁的时钟上 ==
安排: 主线程睡 300ms,工作线程忙转 300ms;进程 CPU 应≈工作线程的量

主线程睡满 300ms,它自己的 CPU 时钟只长了 93346 ns(睡觉不增 CPU 时间)
工作线程纯忙转,进程 CPU 时钟长了 298004743 ns ≈ 298.0 ms
对表: getrusage(RUSAGE_SELF) 记 298.1 ms;/proc/self/stat(tick 口径)记 290 ms(1 tick=1/100 s)

主线程也忙转一段: 线程钟 +183.8 ms,进程钟 +183.8 ms(此时两者同源同速)
用途对表: 限制 CPU 配额(CPULIMIT 类工具)、线程级性能剖析,读的就是这两个 clockid
```

Look at the first line's contrast — that is CPU time's nature: the main thread slept a full 300 milliseconds while its own thread clock gained only 93346 nanoseconds, a few dozen microseconds — the system time charged to it by chores like thread setup and synchronization. Sleep itself grows it by not one nanosecond: three hundred wall milliseconds were spent, and what got booked to CPU time is the loose change. The busy-spinning worker pushed the process clock to 298.0 milliseconds, because it was the only CPU burner in that window, so the process clock's ledger is all its work.

The forecast from E2 comes due as well: THREAD_CPUTIME_ID's median granularity sits at 341 nanoseconds, a full grade coarser than the monotonic clock. Now we know where the coarseness lives: CPU-time readings must come out of accumulated records, not the monotonic clock's straight road, and the granularity price is paid in that stretch.

The cross-check numbers are the main act: one and the same 298 milliseconds of CPU time, recorded three ways, yields three numbers — the nanosecond-grade process clock says 298.0, getrusage's microsecond granularity says 298.1, and `/proc/self/stat`'s tick convention says 290. One tick cell is ten milliseconds; 298 milliseconds of work dropped into ten-millisecond cells can fill only 29 — hence 290. The tick-reading code hides one small trap: the comm field may contain spaces, so parsing must count from the last closing parenthesis onward.

> man 5 proc lists dozens of subfields for /proc/self/stat; utime ranks 14th, and our code skips 11 %*s's after the closing parenthesis before reaching it — off by one field, and you would be reading the neighboring process attribute.

```cpp
char* p = std::strrchr(buf, ')'); // 跳过 comm 字段(里面可能带空格)
unsigned long ut = 0, st = 0;
// p+2 起是第 3 字段 state;utime/stime 是第 14/15,前面要跳过 11 个
if (p) std::sscanf(p + 2, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", &ut, &st);
return double(ut + st) / sysconf(_SC_CLK_TCK);
```

The parting of CLK_TCK and the kernel tick from E1 lands on solid ground here: cell width is decided by CLK_TCK's 100 and is a separate affair from the kernel's 250Hz tick; the three conventions' granularity grades are written directly on the readings, matched at a glance.

In the second half, we draft the main thread into busy-spinning too: the thread clock and the process clock grew in lockstep by 183.8 milliseconds — the two clocks share source and rate, differing only in scope: the thread clock records one thread, the process clock the sum over all threads in the process. Their uses part accordingly: thread-level profiling reads CLOCK_THREAD_CPUTIME_ID, watching which thread burns CPU; quota-style tools — the ones capping a whole process's CPU — read CLOCK_PROCESS_CPUTIME_ID.

## E6: Wall Time's Presentation Chain: The Epoch Stands Still; What Moves Is the Rules of Interpretation

Finally, time for humans to read. The time_t read back from CLOCK_REALTIME is UTC epoch seconds counted from 1970, carrying no timezone information of its own; to become local time on a screen it must walk a presentation chain: localtime_r consults the rules, strftime does the typesetting. The chain's code is a few lines; look at t6's stamp — every tier walks the same pass:

```cpp
std::time_t t = ts.tv_sec;
std::tm gm{}, lc{};
gmtime_r(&t, &gm);
localtime_r(&t, &lc);
char g[64], l[64];
std::strftime(g, sizeof g, "%Y-%m-%d %H:%M:%S UTC", &gm);
std::strftime(l, sizeof l, "%Y-%m-%d %H:%M:%S %Z %z", &lc);
```

gmtime_r and localtime_r both carry the _r suffix — the versions prepared for reentrancy, writing results into a caller-supplied struct tm, so multiple threads each consulting their own never collide. For timezone switching, see t6's second half: we take one epoch second and switch the TZ environment variable through four settings:

```cpp
setenv("TZ", "UTC0", 1);      tzset(); stamp("TZ=UTC0");
setenv("TZ", "America/New_York", 1); tzset(); stamp("TZ=America/New_York");
setenv("TZ", "Asia/Tokyo", 1);        tzset(); stamp("TZ=Asia/Tokyo");
```

```text
== E6: 同一 epoch 值的两种呈现 ==
默认 TZ      epoch=1791121296.748978340
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 21:41:36 CST +0800  (TZ=(未设,读 /etc/localtime))
TZ=UTC0        epoch=1791121296.749025930
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 13:41:36 UTC +0000  (TZ=UTC0)
TZ=America/New_York epoch=1791121296.749062289
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 09:41:36 EDT -0400  (TZ=America/New_York)
TZ=Asia/Tokyo  epoch=1791121296.749075103
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 22:41:36 JST +0900  (TZ=Asia/Tokyo)
...
```

The four localtime_r rows — 21:41:36, 13:41:36, 09:41:36, 22:41:36 — all name the same instant: the epoch second never moved; only the rule library interpreting it changed. The rules have an order of precedence: with TZ set, we follow TZ; without it, we read /etc/localtime, which on this machine points to Asia/Shanghai. After setenv changes TZ, a tzset call must follow, or the old rules stay loaded. gmtime_r's column always gives UTC; set the two sides side by side and what the rules did becomes plain.

So in the POSIX world, the timezone question belongs to the presentation layer. Store your log timestamps as epoch seconds and you store absolute instants — opened in any timezone, they name the same moment. Store local-time strings instead, and daylight-saving ambiguity never washes out: the repeated small-hours hour, once stored, can never be told before from after. This layering is what the next article's chrono admits into the type system: epoch maps to sys_time, the rule library to zoned_time, even leap seconds get a non-jumping utc_clock to catch them, and TAI's 37 seconds return to the stage. We continue over there.

## How the Other Side Sees It

Windows has no unified entry like clockid; its clocks are scattered across several APIs, though the division of labor maps cleanly. GetTickCount64 is a millisecond count from boot — monotonic and cheap; we used it as a millisecond timestamp in the [IOCP article](../../windows/async-io/02-iocp.md), and the extra sliver by which occasional timestamps exceeded the set interval is exactly its ten-odd-millisecond quantization at work. QueryPerformanceCounter is the high-precision monotonic counter; we used it as a timer in the [file mapping article](../../windows/file-io/02-file-mapping.md). Wall time offers a coarse-and-fine pair, GetSystemTimeAsFileTime and GetSystemTimePreciseAsFileTime, the same shape as our COARSE-versus-fine-ruler layering; we cite that pair per the documentation, as this volume's experiments have not reached them.

The measured anchor remains that scene from the [shared memory article](../../windows/memory/02-shared-mem.md): Sleep(5) actually slept about 12.6 milliseconds, because Windows' default timer resolution is one 15.6-millisecond quantum — a five-millisecond request rounds up to the next cell; the experiment did not call timeBeginPeriod, and the archive says so honestly. Set the two sides side by side and the two coarse tiers differ in standing: Linux's COARSE is an explicit coarse-read tier with the initiative in your hands, while Windows' 15.6 milliseconds is the timer system's default granularity that waits like Sleep cannot sidestep — to refine it, you must call timeBeginPeriod globally. The next article enters C++20 chrono's calendar and timezones, wiring this article's epoch, TAI's 37 seconds, and the timezone rules into the type system one by one; the full timer landscape we leave for the final article.
