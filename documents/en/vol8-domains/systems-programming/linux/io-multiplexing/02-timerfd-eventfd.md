---
title: "timerfd and eventfd: Time and Events as File Descriptors"
description: "How to convert time and events into file descriptors and integrate them into the same epoll loop: This article tests the counter semantics of eventfd (write is additive, 1+2+5 three writes are read once as 8, then read returns EAGAIN, EFD_SEMAPHORE subtracts 1 each time, writing 5 results in five reads of 1 each, writing beyond 2^64-2 returns EAGAIN, buffer smaller than 8 bytes returns EINVAL, reads larger than 8 bytes only receive 8 bytes), the three notification levels of a single write(5) (LT with default wakes once, semaphore with LT wakes five times for busy notification, reading once does not equal reading empty, semaphore with ET wakes once and ends with EAGAIN), the timerfd's core features (single-time 200ms expiration returns 1, periodic 100ms after sleeping 350ms, read returns 3 merged counts, fdinfo shows ticks at 1 while read returns 3, missed counts are reported once and cleared, next counts continue as per original rhythm, changing interval uses the old it_value from the structure for the first count, then continues with 30ms, it_value zeroing cancels, select returns 0 after 300ms), the accuracy of 1ms intervals (median of 500 intervals is 999.3µs, p99 is 1029µs, max is 1054.3µs, 10ms median is 10000.3µs, consistent on rerun, compared with Windows Sleep(5) which actually sleeps 12.6ms), the conclusion is a single loop with pipe, eventfd, and timerfd all attached to the same epoll, 151/250/401/500/651/750/901ms three types of events are all dispatched by the same epoll_wait on an interleaved timeline"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "I/O Multiplexing: Boundaries and Costs of select, poll, and epoll"
  - "Signals (Advanced): Real-time Signals, signalfd, and pidfd"
related:
  - "I/O Multiplexing: Boundaries and Costs of select, poll, and epoll"
  - "io_uring: Turning I/O Waiting into Harvesting Completion Events"
  - "Signals (Advanced): Real-time Signals, signalfd, and pidfd"
  - "Shared Memory: Named Mapping Objects with Page Files as Backup"
  - "inotify File Monitoring: Converting File System Activity into Event Streams"
tags:
  - host
  - cpp-modern
  - advanced
  - System Programming
  - POSIX
  - Concurrency
current_status:
  title: Being Polished
  detail: This batch was just written and is now being proofread and refined. The expressions and experimental results may change at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/io-multiplexing/02-timerfd-eventfd.md
  source_hash: 2482918aeffad7e9bece58f9552eeb28caf0033ed6553f4dd61ff8fb8eb032ae
  translated_at: '2026-11-22T15:03:30+00:00'
  engine: qwen3:14b
  token_count: 13694
---
# timerfd and eventfd: Time and Events as File Descriptors

In the previous article [select, poll, and epoll](./01-select-poll-epoll.md), we measured the behavior of the three waiting APIs, how many file descriptors each wake up once, how the cost increases with N, and how epoll's interest table saves the trouble of repeated registration. We obtained concrete data. However, the data source in that article was entirely pipes, and the waiting was for data in the pipe. Real programs have to wait for much more than just one thing: heartbeats must be sent on schedule, timeouts must be received on schedule, and other threads must notify us when they finish their work. However, time and events are not byte streams, and our poll table has no way to wait for them.

Linux's answer is still the same old saying: everything is a file descriptor. Timers are also made into file descriptors (timerfd), and when they expire, they become readable. The number read is the number of missed intervals. Events are also made into file descriptors (eventfd). You ring the bell by writing to it, and reading it is answering the door. They join the same table and loop as pipes, sockets, and inotify. In the [signal advanced article](../process/05-signal-advanced.md), we had timerfd play a minor role once, where a 150ms periodic timer and signals were interleaved on the poll table, but the detailed explanation was not its turn. At the end of the previous article, we left a message for eventfd, saying that eventfd-count, which appeared briefly in E6, would be the main character here. Now both are in turn. Windows uses a different set of objects and handles, and there is no unified fd interface. In the [shared memory article](../../windows/memory/02-shared-mem.md), eventfd appeared once as a notification piece on the WSL2 side, and we did not retest the single 264 nanosecond result. This article only focuses on the semantics.

The experiments are numbered E1 to E5, corresponding to the five groups of files t1 to t5 in the repository `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/02-timerfd-eventfd/`. All the code and original outputs are recorded. This article's E only refers to this article: the previous article's E series and the next article's E series on io_uring, even if they have the same number, they are unrelated. When you refer to the archives, just recognize the directory.

Most of the output blocks in the main text are excerpts, and the deleted lines are marked with `...`. When cross-referencing with the archives, please refer to the archives.

The experimental code is written with raw system calls, and the file descriptors are all raw ints. Most of the error handling is omitted, aiming to clearly present the mechanism, which is consistent with the previous article. When we write our own engineering code, these file descriptors will be handled by [RAII paradigm](../../thinking/01-raii-paradigm.md)'s unique_fd, and the errors will be handled by [error handling paradigm](../../thinking/02-error-paradigm.md)'s expected. The two articles provide the correct foundation, and this article does not repeat them.

As usual, the environment parameters are clearly stated, and the following numbers must be matched accordingly: the experiments were conducted on the author's WSL2, using the 6.18.33.2-microsoft-standard-WSL2 kernel build, with an AMD Ryzen 7 9700X CPU, g++ version 16.2.1, and all compilation parameters are `-std=c++20 -O2 -Wall -Wextra`. The glibc version is 2.44, and the number of warnings in all experiments is zero. All timing uses CLOCK_MONOTONIC, and the timing-related E4 was rerun once, with the second round's output stored in the archive's `t4_rerun.out`. All `.out` files are from the same round on October 4, 2026. The file descriptor numbers, millisecond timestamps, and nanosecond remainders change with each rerun. We refer to the order and intervals, not any specific numbers.

## E1: eventfd: An 8-byte counter in the kernel

### write is addition, read is taking away

The parameters of `eventfd(2)` are an initial value and flags, and it returns a file descriptor. There is no buffer or byte stream behind this file descriptor; it is an unsigned 64-bit counter in the kernel, and there is a waiting queue outside. The 8 bytes written are the number to be added, and the 8 bytes read are the value of the counter at that time. We run t1 in default mode directly:

```text
[默认模式 efd=3]
  write(第1次, 1) = 8
  write(第2次, 2) = 8
  write(第3次, 5) = 8
三次 write 之后计数器 = 1+2+5, 一次 read 全取走:
  read(取值) = 8
  read(再读) = -1 errno=11 (Resource temporarily unavailable)
```

Three writes added 1, 2, and 5, and one read took them all away. There is a place where it is easy to misread, so I need to mention it to you: the 8 in the write line is the number of bytes written, and the 8 in the read line is the value of the counter. The two 8s happen to be together, but it is purely because 1+2+5 is exactly 8. If you change the third one to 4, the number read becomes 7, while the number of bytes written by write is still 8. The default mode read takes the entire counter and clears it, so the second read after that gets EAGAIN (errno 11), the fd is already empty. This "read and clear" is the same as what you saw in the signalfd in the [signal advanced article](../process/05-signal-advanced.md): read and consume.

### EFD_SEMAPHORE: subtract 1 at a time

We bring `EFD_SEMAPHORE` into the flags, and the way of reading changes:

```text
[信号量模式 EFD_SEMAPHORE efs=4]
  write(一次写 5, 5) = 8
  read(第n次) = 1
  read(第n次) = 1
...
  read(第n次) = -1 errno=11 (Resource temporarily unavailable)
```

We still send one write(5), but in the semaphore mode, each read only subtracts 1 from the counter, and the returned value is fixed at 1. Five reads take the 5 one by one, and the sixth time it gets EAGAIN. The counter itself hasn't changed, only the way of taking has changed: the default mode takes everything at once, and the semaphore mode takes one at a time. It sounds like the two ways of taking are only slightly different in taste, but when they enter epoll, the difference is so big that we need an entire E2 to see it.

### Two clear rejections

We look at two boundaries, each with its own clear errno. The upper limit of the counter is 2^64 - 2. When adding to a point where it can't fit, write returns EAGAIN:

```text
[溢出演练 efo=5]
  write(写到只差 1 满, 18446744073709551614) = 8
  write(再加 5, 5) = -1 errno=11 (Resource temporarily unavailable)
```

We write 18446744073709551614 (which is exactly the upper limit of 2^64 - 2), and this time it was successful. Adding 5 again would not fit. The man page for eventfd clearly states: in blocking mode, write will wait for the read side to take it, while in non-blocking mode (EFD_NONBLOCK), it directly returns EAGAIN. Another rejection is more straightforward: read and write with a buffer smaller than 8 bytes both return EINVAL:

```text
[尺寸错配 efe=6]
  write(4 字节) = -1 errno=22 (Invalid argument)
  read(4 字节)  = -1 errno=22 (Invalid argument)
```

We tried 4 bytes in and 4 bytes out, and both directions were rejected. The man page clearly states the condition that the buffer is smaller than 8 bytes. We tried it again with a 12-byte buffer: write still returns EINVAL, but read succeeds, although the content returned is still only 8 bytes. The fdinfo of eventfd itself is also worth looking at, with two fields, eventfd-count and semaphore, revealing the state of the counter. We have already touched on it in the previous E6, so we won't go over it again. And timerfd will have more interesting fdinfo to see shortly.

## E2: After being attached to epoll, how many times will a single write(5) wake up?

If we look only at the counter, the semantics class is only half done. The proper use of eventfd is to attach it to the event loop as a notification piece, so the question arises: how many times will a single write(5) wake us up? The number of times we wake up depends on the combination of two things: the read mode (default or semaphore), and the registration trigger method (level-triggered LT or edge-triggered ET, the mechanism class for this pair is in the [epoll article](../../../networking/02-epoll-io-multiplexing.md) of the network volume, and the behavior was measured in the previous article). t2 ran all three combinations (the wake-up methods for default mode with ET and LT are the same, so it was not run separately), and in each round, we only read once, while ET mode reads to EAGAIN according to discipline:

```text
一次 write(5) 之后, 每次醒来读一次(ET 档按纪律读空):
LT+默认:
  [semaphore=0 et=0] 醒 1 次, read 1 次, 取走计数合计 5, 收敛耗时 151 ms
LT+信号量:
  [semaphore=1 et=0] 醒 5 次, read 5 次, 取走计数合计 5, 收敛耗时 150 ms
ET+信号量:
  [semaphore=1 et=1] 醒 1 次, read 5 次, 取走计数合计 5, 收敛耗时 150 ms
```

Before we look at the numbers, there is one thing we need to mention upfront: the convergence time of the three groups is all around 150ms, but this does not mean that the three modes are equally fast. The exit condition of the loop is unified as epoll_wait waiting for 150ms with nothing to report, and the wake-up and read actually happen in the first few milliseconds, while the remaining time is all waiting for the 150ms timeout to expire. After we eliminate the method of comparing convergence time to measure speed, the real difference lies in how many times we wake up and read.

We look at them one by one. The combination of LT and default wakes up once, read once takes away the 5, the counter is cleared, and the fd is no longer readable, and the rest are quiet. The combination of ET and semaphore wakes up once, the edge only reports once, but the wake-up time requires reading five times in a row, ending with EAGAIN, which is the standard action of ET in the previous article. The combination of LT and semaphore wakes up five times, we read once, the counter drops from 5 to 4, and as long as the counter hasn't returned to 0, the fd remains readable, and LT's semantics is to report as long as it is readable, so the next round of epoll_wait will wake up immediately. In this combination, reading once does not equal reading empty, and the loop is called like this, one by one.

We take this one out separately because it's worth your knowing: semaphore with LT forms a busy notification mode. From our perspective, the program seems to be idling, but in fact, every step is working, and each read really takes away a 1, while from the perspective of epoll, it looks exactly like a hot loop caused by unprocessed events. If you see epoll_wait returning the same fd repeatedly, don't rush to doubt the kernel, the first thing to check is whether our read is in semaphore mode. t2 ends with a very straightforward statement on this matter:

```text
LT+信号量那组: 计数器还剩 4 时 fd 仍处于可读状态, 每轮 epoll_wait 都再报,
读 1 次 -> 剩 4 -> 又醒 -> ... 直到清零。这不是 bug, 是 LT 的语义配上了"读不完全清零"的计数器。
```

We extract the two ways of reading in t2, and the source of the three differences is here:

```cpp
if (et) {
    // ET 纪律: 一次事件里读到 EAGAIN
    for (;;) {
        ssize_t r = read(efd, &v, 8);
        if (r < 0) break;
        ++res.reads; total += v;
    }
} else {
    if (read(efd, &v, 8) == 8) { ++res.reads; total += v; }
}
```

We look at the LT branch, after waking up, reading once is considered as processed, in the ET branch, we have to read until EAGAIN to let go, this is the discipline set in the previous article. So, the same semaphore mode, the LT branch wakes up five times, each taking one, while the ET branch wakes up once and takes five in a row, the number of times waking up differs by five times, but no work is missed.

So when should we use which mode? We divide it according to the question we want to answer. If you just want to know "work has come, go process it," use the combination of default mode and LT: notification comes once, read once takes all, and the whole process is quiet. For each count corresponding to an independent piece of work, consumption once at a time, use the semaphore mode: the worker in the work pool reads once and takes one piece of work, which is exactly the semantics we want, and at this time, LT is waking up piece by piece, while ET needs to read all the way to EAGAIN in one wake-up.

## E3: timerfd: A timer counter in the kernel

`timerfd_create(2)` takes a clock id and flags as parameters and returns a timer's file descriptor. The setup uses itimerspec: it_value is the time for the first interval, and it_interval is the time for subsequent intervals. When both are zero, the timer is canceled. When it's time, no callback is run, no signal is sent, but the fd becomes readable, and reading gives 8 bytes, the content is the number of expired intervals. We do three parts in turn: one-time setup, periodic and merging, changing the interval and canceling, corresponding to the archives' [i], [ii], [iii].

### fdinfo as an observation window

This fd has a cheap observation window: `/proc/self/fdinfo/<fd>`. In the [IPC article](../process/03-ipc.md), we used it to look at the shared pos of SCM_RIGHTS, and here, timerfd reveals more fields: clockid, ticks, it_value, it_interval, all current values in the kernel, and we don't need to spend a single system call. We took a snapshot when t3 was just created:

```text
--- fdinfo(3) 刚创建, 未设定 ---
pos:	0
flags:	04002
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 0)
it_interval: (0, 0)
```

clockid 1 is the number of CLOCK_MONOTONIC in Linux. After setting it to a one-time timer of 200ms, we took two more snapshots:

```text
...
--- fdinfo(3) 设定后(剩约 200ms) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 199994019)
it_interval: (0, 0)
--- fdinfo(3) 60ms 时(剩约 140ms) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 139869103)
it_interval: (0, 0)
  select 报就绪, 距设定 200 ms
  read = 1
  read = -1 (Resource temporarily unavailable)
```

The remaining time went from 199994019 nanoseconds to 139869103 nanoseconds, and about 60ms passed between the two snapshots, the remaining time visibly decreased, more intuitive than any description in the documentation. After expiration, select reported readiness, one read got 1, and another read became EAGAIN, you saw "read and clear" again here.

### Periodic and merging: missed intervals, read once and report all

We fill in it_interval, and it becomes a periodic mode, the archive's label for this section is [ii], and [i] is the previous one-time setup of 200ms. t3 set it to 100ms per interval, and the reader intentionally slept for 350ms before coming:

```text
[ii] 周期 100ms, 读方故意睡 350ms 再来:
--- fdinfo(3) 350ms 时(约 3 档已过) ---
...
clockid: 1
ticks: 1
settime flags: 00
it_value: (0, 0)
it_interval: (0, 100000000)
  read = 3
  一次 read 报的是错过的档数: 3 (不丢数据, 但也不排队报 3 次)
  read = 1
```

There are two details worth pausing in this output. Let's look at the first one: 350ms passed, and 3 intervals (100, 200, 300ms) passed, and read reported 3. Missed intervals are not lost, but they are not reported three times in a row, but once read reports all 3, and clears after reporting. You can compare it with standard signals: during the blocking period, three SIGUSR1 signals are sent but only one is recorded, signal merging loses the count, while timerfd retains the count, merging only the report.

The second detail is more interesting, let's look at it slowly. In fdinfo, ticks is 1, it_value is also zero. If ticks reports the number of un-read expired intervals, it should be 3 here, but the real 3 is given by read. Let's look at the next line: after read takes it away, the code only sleeps for 50ms, and the next interval arrives (the last read gets 1). Let's put these lines together, the mechanism becomes clear: un-read expired intervals are not recorded in fdinfo, ticks is only the indicator of un-read expiration, and the timing has already stopped (it_value is zero), waiting for the reader to take it. Ticks stops at 1 while read reports 3, which tells us: the missed 3 is calculated by the read moment according to the rhythm, the timing has already stopped after the first interval expired. And after read takes it away, the next interval still arrives on the original rhythm (the 400ms mark relative to the set time), not starting from the read moment again. So, waking up late, the missed count and the original rhythm are unaffected.

### Changing the interval: settime is a full reinstall

The next semantic, which is rarely presented in the textbook, we got through the experiment. t3's third part [iii] changes the period from 100ms to 30ms, and the code only changes the it_interval in the structure:

```cpp
its = {};
its.it_value = {0, 100000000};
its.it_interval = {0, 100000000};               // 100ms 一档
timerfd_settime(tfd, 0, &its, nullptr);
...
its.it_interval = {0, 30000000};
timerfd_settime(tfd, 0, &its, nullptr);          // 相对模式: 新周期从此刻起算
```

The author pastes the result as is:

```text
[iii] 改 interval 100ms -> 30ms, 再撤掉:
  t+100 ms 到期 (30ms 档)
  t+130 ms 到期 (30ms 档)
  t+160 ms 到期 (30ms 档)
  t+190 ms 到期 (30ms 档)
  t+220 ms 到期 (30ms 档)
```

The code prints the label "30ms interval" hard-coded, but the first line is t+100. The print is not wrong, the timer really rings at 100ms, and then enters the 130, 160, 190, 220 30ms rhythm. The comment in the archive says "relative mode: new period starts from this moment," which is the author's understanding at the time. The first half of the relative mode is correct, the second half is wrong: the moment from which it starts is the old value still left in it_value, not the new period we wrote. The real mechanism is one sentence: timerfd_settime does not have a call to "only change the period," it does a full reinstall. Whatever it_value you write in the itimerspec you pass in, the first interval is set to that. Our structure comes from the previous section, it_value still has the old 100ms value, we only changed it_interval, and the kernel installed the first interval with it_value=100ms, and subsequent intervals with 30ms. We often refer to this phenomenon as "changing the interval, the first interval uses the old it_value," but the subject is actually the structure in your hand, not the kernel remembering the old phase. POSIX's timer_settime has always been defined this way, and timerfd inherits it directly. If you want the first interval to start immediately with the new period, just write it_value as 30ms as well.

### Canceling: both fields zero together

The easiest way to cancel is to clear the entire structure and settime, which is what we did in t3, and both ends confirmed it:

```text
--- fdinfo(3) 撤销后(剩余时间为 0) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 0)
it_interval: (0, 0)
  撤销后等 300ms: select 返回 0 (不再有到期事件)
```

select waited for 300ms before returning 0, no expired events. After cancellation, the fd is still there, can still poll, but will never be ready again, unless you settime again to reset it.

## E4: 1ms interval, how stable is it on time

After we have covered all the semantics, it's time to ask about the accuracy. t4 provides two intervals with a batch of samples each, recording the interval between two consecutive read wakeups: 1ms interval collects 500, 10ms interval collects 300:

```text
周期 1000 us, 收 500 档, 相邻间隔(us): min=945.4 中位=999.3 p90=1010.3 p99=1029.0 max=1054.3
周期 10000 us, 收 300 档, 相邻间隔(us): min=9956.3 中位=10000.3 p90=10018.1 p99=10028.9 max=10042.4
```

We reran a round, and the results are stored in the archive's `t4_rerun.out`:

```text
周期 1000 us, 收 500 档, 相邻间隔(us): min=950.5 中位=999.3 p90=1010.5 p99=1029.5 max=1061.6
周期 10000 us, 收 300 档, 相邻间隔(us): min=9705.4 中位=10000.3 p90=10019.9 p99=10078.5 max=10215.0
```

The median of the two rounds is exactly the same (999.3 and 10000.3), and the shape of the tail is stable. Let's look at the first round of the 1ms interval: the median is only 0.7µs away from the nominal 1ms, the p90 is only 1% more than the nominal, the worst case is 1054.3µs, but that's only 5.4%. The min is even smaller than 1000µs (945.4), don't take it as the timer running early, it's actually a catch-up: the previous interval woke up late, the next interval still arrives on the original grid, so the adjacent interval naturally shrinks back. And the semantics of E3 on the interval distribution, exactly this shape. The 10ms interval is the same, the relative fluctuations are smaller, and the median is at 10000.3µs, the worst case in the first round is 10042.4µs (deviation 0.4%), and the rerun is relaxed to 10215µs, no more than 2.2%.

These numbers need to be compared with a reference to show their weight. On the same machine, the Windows side [shared memory article](../../windows/memory/02-shared-mem.md) tested Sleep(5): it actually slept about 12.6 milliseconds. The default timer resolution on Windows is 15.6ms per interval, and a 5ms request is rounded up to the next interval. That article also clarified the reason: each of the 100 naps added about 7ms, and the total time difference between the two sides was exactly matched. On the Linux side, hrtimer uses a high-precision clock, there is no global coarse interval, and a 1ms request is really arranged at 1ms. Two worlds on the same hardware, the difference in timer accuracy is the most intuitive lesson in platform differences. The parameters also need to be stated: these are the numbers from idle processes, if we run heavy loads on the same machine, the p99 and max numbers will also be relaxed, and our comparison only holds under the same idle conditions.

## E5: One epoll table, receiving three types of event sources

After covering the semantics, notification counts, and accuracy, we conclude by putting eventfd and timerfd into the same loop. t5 has three things: a pipe as a data source, an eventfd as a doorbell from other threads, and a 250ms periodic timerfd as a heartbeat. The registration code is just a few lines:

```cpp
int ep = epoll_create1(0);
epoll_event ev{};
ev.events = EPOLLIN; ev.data.fd = pd[0]; epoll_ctl(ep, EPOLL_CTL_ADD, pd[0], &ev);
ev.events = EPOLLIN; ev.data.fd = efd;    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
ev.events = EPOLLIN; ev.data.fd = tfd;    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);
```

We also extract a segment of the main loop's skeleton, and the shape of the distribution by fd is here:

```cpp
for (;;) {
    epoll_event evs[8];
    int k = epoll_wait(ep, evs, 8, 1000);
    for (int i = 0; i < k; ++i) {
        int fd = evs[i].data.fd;
        if (fd == tfd) {
            uint64_t v; (void)!read(tfd, &v, 8);
            ++n_timer;
            std::printf("t=%4ld ms  timerfd 第 %2d 次到期\n", now_ms() - t0, n_timer);
        } else if (fd == efd) {
            uint64_t v; (void)!read(efd, &v, 8);
            ++n_event;
            std::printf("t=%4ld ms  eventfd 门铃, 本次取走计数 %llu\n", now_ms() - t0, (unsigned long long)v);
        } else if (fd == pd[0]) {
            char buf[16] = {};
            ssize_t r = read(pd[0], buf, sizeof buf - 1);
            ++n_pipe;
            std::printf("t=%4ld ms  pipe 收到 %zd 字节: \"%s\"\n", now_ms() - t0, r, buf);
```

Our three branches each handle one path: when timerfd expires, the count is read, when eventfd rings, the accumulated value is taken, and when pipe data comes, it is read to see if the work signal is present. The `(void)!` in the code is intentionally written to discard the return value, the demonstration code aims for a clean timeline, but when you write your own code, the errors here should still be handled. We arranged the worker thread to write the pipe at 150ms, ring the bell at 400ms and 650ms, and write quit at 900ms to finish the work, the entire timeline is like this:

```text
t= 151 ms  pipe 收到 5 字节: "hello"
t= 250 ms  timerfd 第  1 次到期
t= 401 ms  eventfd 门铃, 本次取走计数 1
t= 500 ms  timerfd 第  2 次到期
t= 651 ms  eventfd 门铃, 本次取走计数 2
t= 750 ms  timerfd 第  3 次到期
t= 901 ms  pipe 收到 4 字节: "quit"

循环结束: timerfd 3 次, eventfd 2 次, pipe 2 次, 全部由同一个 epoll_wait 分发
```

You see the data at 151, the heartbeat at 250/500/750, the bell at 401/651, all interleaved on the timeline, none interrupting each other. The bell uses default mode with LT, write(1) and write(2) each notify once, read takes the accumulated value at the time, which is the semantics of E1 in the loop, and also the counterpart of the busy notification mode in E2. epoll_wait is set with a 1000ms timeout as a fallback, but none of the events used it, each step was awakened by the event.

We have put the three types of event sources into the same loop, and that's what t5 wants to say. Looking back at the path of this volume: [inotify article](../file-io/06-inotify.md) converted the dynamics of the file system into fd, the signal advanced article converted signals and processes into signalfd and pidfd, and this article also completed the conversion of time and events. They have different origins in the kernel, but in the poll table, they all become the same thing: a waitable fd. The method of waiting with fd also has its limits: normal files on the disk, epoll will not allow them to be attached, if we want to read them asynchronously, we need to change the posture of waiting for fd to submit requests and wait for completion events. That is the main topic of the next article on io_uring, we will continue there.
