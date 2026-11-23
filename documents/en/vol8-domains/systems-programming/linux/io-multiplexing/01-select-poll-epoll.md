---
title: "I/O Multiplexing: Boundaries and Costs of select, poll, and epoll"
description: "Passing the same set of pipe fds to three generations of waiting APIs, where are the boundaries and cost curves: This article empirically overturns the 1024 limit of select (FD_SETSIZE is just the 128-byte size of the glibc bitmap, FD_SET(1024) with fortify guards directly SIGABRT, manually expanding the bitmap to 2048 bits allows select(nfds=1026) to report fd=1025 as ready, RLIMIT_NOFILE set to 1024 doesn't block select, poll actually checks rlimit, 1050 entries empirically return EINVAL and man poll(2) clearly states it), E2 scanning volume comparison (500 pipes, one ready, select checks 1002 fds, poll 500 entries, epoll 1, byte volume 128 B in/out for 4000 B in/out per ready item 12 B), E3 cost curves (2000 rounds x 5 taking median, poll per round 2527 to 12815 to 104460 nanoseconds linearly with N, the us in the table header is a typo in the archival script, the unit is actually nanoseconds and has been re-run and verified, epoll remains steady at 994/994/985 nanoseconds, select in the 500 category is 14363, which is more expensive than poll's 12815, structural reason: scanning up to nfds about 1000 while entries are only 500), E4 select parameter rewrite (300ms event with 2s budget returns, timeout remains 1.699s, fd_set is emptied, new events in B are missed, poll on-site proves POLLIN is present, rebuilding after 0ms ready), E5 pipe version LT/ET behavior comparison (60000 B wake up to read 4096, LT wakes up 15 times to converge, ET only wakes up once, 55904 B remains unreported, new write of 1 byte triggers new edge, 14 reads to EAGAIN), E6 interest table directly visible (/proc/self/fdinfo's tfd line is the registration table, ADD three lines DEL removes, registering EPOLLIN records 0x19, kernel automatically adds EPOLLERR and EPOLLHUP, eventfd-count writes 3 and reads back to 0), mechanism class not repeated, interest table, ready queue, and LT/ET kernel model links to network volume, fd source is pipe, echoing IPC article"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 20
prerequisites:
  - "POSIX File I/O: open/read/write and the life of fd"
  - "IPC: Pipes, FIFOs, and POSIX Message Queues"
related:
  - "Signals (Advanced): Real-time Signals, signalfd, and pidfd"
  - "epoll: Linux I/O Multiplexing, from poll's bottleneck to interest table and ready queue"
  - "inotify File Monitoring: Turning File System Activity into Event Streams"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 异步编程
current_status:
  title: Being Polished
  detail: This batch has just been written, and is currently being read and polished, the expression and empirical results may be adjusted at any time
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/io-multiplexing/01-select-poll-epoll.md
  source_hash: c01325540a04193d1e517ff6eaf16fc89767b2fd41519bdba437bc7910a6417b
  translated_at: '2026-11-22T15:02:06+00:00'
  engine: qwen3:14b
  token_count: 17671
---
# I/O Multiplexing: Boundaries and Costs of select, poll, and epoll

When we perform a single read on the read end of a pipe and the data hasn't arrived yet, we can only wait: the entire execution flow is blocked in this system call, and we don't know what's happening on other fds. What if we're watching two pipes? It's even worse. A single thread can only block on one call at a time, and when it's blocked on the first pipe, the data arriving on the second pipe is ignored. I/O multiplexing solves this problem: it merges the task of waiting for a batch of fds into a single system call, and the kernel reports which one has activity, and we then go on to read.

Linux has three generations of waiting APIs, and we'll list them by seniority: select, poll, and epoll. Their fame increases with each generation, but the real differences are often only summarized as "select is limited to 1024 fds, and epoll is high performance." After completing the six experiments in this article, the answer I've reached needs two corrections: the 1024 boundary is indeed there, but it's not in the kernel, it's in the user space glibc. The statement about high performance is correct, but the cost comes from where, and why the other two's costs increase with N, needs to be explained with curves.

In the previous article, we took the consumption of signals and handed it over to signalfd and pidfd: signals, timers, and child process exits are all converted into fds, squeezed into the same poll table. The mini prefork echo server in the previous article used this to implement its graceful shutdown state machine. All the various fds in the table have arrived, and the call responsible for waiting was only briefly mentioned. This article brings it to the forefront: the same set of pipe fds are handed to the three generations of APIs, and we look at where the boundaries of their behavior are, and how the cost of each round grows.

The materials and boundaries are explained upfront. The fds in this article's experiments are all pipes, and we don't use any sockets: we've already tested the behavior of pipes in the [IPC article](../process/03-ipc.md), including their 65536 capacity, the sequence of stopping when full, and the SIGPIPE when the read end is fully closed. E5 directly uses the first one. The mechanism class of epoll is not repeated: the [epoll article in the networking volume](../../../networking/02-epoll-io-multiplexing.md) starts from the bottleneck of poll, and goes on to explain the interest table (the list of fds registered in the kernel), the ready queue and the waiting queue, the C10K story, and the kernel-level differences between level-triggered LT and edge-triggered ET are all covered in that article. This article stays on the API behavior level, and E5's LT/ET only does a pipeline version behavior comparison. The architecture of the event loop and its connection with coroutines are covered in [vol5's asynchronous I/O and event loop article](../../../../vol5-concurrency/cookbook/b-topics/06-async-io-event-loop.md), and we won't go beyond that.

The experiments are numbered E1 to E6, and the archives are in the six files e1 to e6 under the directory `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/01-select-poll-epoll/`. The code and the exact output are all included, and the README also writes the compilation and reproduction commands for each group, so you can always refer to the archives for the table. The output blocks in the main text are excerpts, and the deleted lines are marked with `...`. When referring to the table, please use the archives as the standard. There are two more articles in this directory: one on timerfd and eventfd, and another on io_uring. Their E-numbered series are not related to ours (the timerfd article goes up to E5, and the io_uring article up to E6). When you look for archives, just recognize the directory. The prefixes e, t, and u in the archive filenames are also there to cover this. The common tools `unique_fd`, `sys_call`, and `errno_code` in the series are not used in this article: what we want to see is the posture of the three waiting calls themselves, and we don't use any encapsulation. The experimental code is all bare calls.

The environment parameters are clearly stated, and the numbers after are all to be matched with it: the experiments are from the author's WSL2, the CPU is AMD Ryzen 7 9700X, the kernel is 6.18.33.2-microsoft-standard-WSL2, the glibc version is 2.44, the compiler is g++ 16.2.1, the compilation parameters are all `-std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`, and the number of warnings obtained in all experiments is zero. The output is captured from the same round on 2026-10-04, and the timing is always done with CLOCK_MONOTONIC. There are two environmental facts that you should look at now. The first is `-D_FORTIFY_SOURCE=2`: it enables runtime guards for glibc's FD_SET (the macro that sets bits in the fd_set bitmap), and the first scene of E1 is only visible because of it. GCC in the Ubuntu system often defaults to it when optimization is enabled, and we explicitly write it here to ensure reproducible behavior. The second is the RLIMIT_NOFILE on this machine: both soft and hard are set to 1048576, which is very high in WSL2, completely different from the common 1024 in traditional distributions. In E1, we will lower it on the spot with setrlimit, and we'll look back at who cares about it.

## E1: The 1024 limit of select, who is the one to block

### The bitmap is only 128 bytes, out-of-bound writes directly abort

We start with the most widely circulated statement: select can wait for a maximum of 1024 fds. Its physical origin is glibc's fd_set: a 1024-bit bitmap, fixed at 128 bytes in size. The FD_SETSIZE macro is its bit count, written in the compile-time. Once the fd number exceeds 1023, the bitmap has no place to store it. What happens if you write beyond it? We try it in a child process, so that the parent process can collect the corpse:

```cpp
// e1_select_limit.cpp(节选):越界的 FD_SET 放在子进程里做
pid_t pid = fork();
if (pid == 0) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(1024, &set);            // 越界写位图, 带守卫的构建会在这里拦下
    _exit(0);
}
```

```text
*** bit out of range 0 - FD_SETSIZE on fd_set ***: terminated
FD_SETSIZE = 1024 (glibc 编译期常量, fd_set 位图 128 字节)
child: FD_SET(1024) -> 被 SIGABRT 终止 (WTERMSIG=6, wait status 6), glibc 守卫把越界拦成了硬失败
```

Look at the first line of output: that's the original words left by glibc's guard on stderr, the child process was taken away by SIGABRT, and WTERMSIG reported 6. This treatment comes from the build with guards: it turns a silent stack overflow into a hard failure. If this macro is not present, FD_SET(1024) will silently write this bit into the set after the stack memory, resulting in a stack corruption that may occur at any time. So the first layer of the 1024 boundary is given by glibc's data structure, and the kernel has not yet entered.

### Manually expand the bitmap, the kernel accepts it as is

Since the boundary is given by the bitmap, we simply bypass the FD_SET macro and open a large enough memory block to impersonate fd_set, and see if the kernel accepts it. The experiment opened 1050 pipes, and the read end fd numbers went up to 2101. We focus on the read end of the 512th pipe, its fd number is 1025, which is just over the 1024 boundary. The bitmap was manually expanded to 2048 bits (256 bytes), only bit 1025 was set, data was also pre-written into the pipe, and rlimit was adjusted on the spot:

```cpp
// e1_select_limit.cpp(节选):手工位图,越过 glibc 的 fd_set 尺寸
unsigned char big[256];
std::memset(big, 0, sizeof(big));
// 只把第 511 根管道的读端(fd 号 3+2*511=1025, 越过 1024)放进去
int target = pipes[511][0];
big[target / 8] |= 1u << (target % 8);
```

```text
开了 1050 根管道, 最大 fd = 2101
初始 RLIMIT_NOFILE: soft=1048576 hard=1048576
关注 fd=1025 (第 512 根管道读端), 手工置位 bit 1025

[RLIMIT_NOFILE soft=1024]
select(nfds=1026) = 1 errno=0 (Success)
  -> fd=1025 报告就绪 (bit 仍在位图里)
  select 返回 1 -> 内核对 select 的 nfds 不查 rlimit, fd>1024 照常工作
  poll(1050 个条目) = -1 errno=22 (Invalid argument)
[RLIMIT_NOFILE soft=8192]
select(nfds=1026) = 1 errno=0 (Success)
  -> fd=1025 报告就绪 (bit 仍在位图里)
  select 返回 1: 与低位档一致 -> 1024 之界只在 glibc 的 fd_set 里

poll(1050 个 fd) = 1, 无 glibc 上限
epoll: ADD 1050 个 fd 全部成功, wait(0) 返回 1 个就绪
  就绪: fd=1025 EPOLLIN
```

We separate the two rlimit levels. When it was set to soft=1024 (limiting new fds, not closing existing ones), select(nfds=1026) returned 1 (nfds is the first parameter of select, giving the upper bound of the fd number plus one), and fd=1025 reported ready as usual. That is, the bitmap has enough memory, and the kernel does not care about your fd number exceeding 1024. In the same experiment, poll had a different outcome: 1050 entries were hung on, and it directly returned -1 and EINVAL. The ERRORS section of man poll(2) clearly states that if nfds exceeds RLIMIT_NOFILE, it is EINVAL. The ERRORS section of man select(2) also states the same, but the kernel does not implement it: at least in the 6.18 kernel, the select path indeed does not have this check. Both man pages promise this check, but the kernel only implements it for poll, and E1 captures the disconnect between the documentation and the implementation.

We raised rlimit back to 8192, and select's return remained unchanged, the 1024 boundary was always in the glibc fd_set. The same 1050 fds, under the high rlimit, both poll and epoll worked well: poll returned 1, the number of entries was not controlled by glibc. epoll added all 1050 fds, and wait(0) reported the ready exactly as fd=1025.

So the most widely circulated statement can now be accurately attributed: the 1024 is the size of the glibc bitmap, the fortify guard is responsible for turning out-of-bound into abort, and the portability section of the textbook has copied it generation after generation. The kernel itself does not set this line, and only poll checks rlimit. This reversal is comfortable to explain the mechanism, but dangerous to guide engineering: the size of fd_set is part of the ABI, and real projects cannot rely on manually expanding the bitmap and reinterpret_cast to live. The guard will also turn the same attempt into abort at any time. In real engineering, the 1024 of select should be treated as a hard boundary. We clarify its attribution to know who is blocking it.

## E2: How many fds are checked in one wake-up

The return values of the three APIs all report "how many are ready", but the paths to find the ready ones vary greatly in length. select only gives a number, and you have to find out who by yourself: scanning from 0 to nfds-1 bit by bit with FD_ISSET, not skipping any bit. poll also requires you to find it yourself, but it's the pollfd array: each entry contains three things, the fd number, the events you care about, and the revents filled in by the kernel. You have to find the last one. epoll_wait is different: it directly puts the ready events into the array you provided, and returns how many, so you process how many. E2 turns this into a count: 500 pipes, the read end fd numbers from 3 to 1001, we let 1 and 64 have data, and see how many the three APIs check:

```text
N=500 根管道, 读端 fd 区间 [3, 1001]

[就绪 1 / 500]
select : 返回  1, 检查 1002 个 fd, 找到  1 (进内核位图 128 B, 出内核 128 B)
poll   : 返回  1, 检查  500 个 fd, 找到  1 (进内核 4000 B = 500 x 8, 出内核同址改写)
epoll  : 返回  1, 检查    1 个 fd (只数返回的), 出内核 12 B = 1 x 12

[就绪 64 / 500]
select : 返回 64, 检查 1002 个 fd, 找到 64 (进内核位图 128 B, 出内核 128 B)
poll   : 返回 64, 检查  500 个 fd, 找到 64 (进内核 4000 B = 500 x 8, 出内核同址改写)
epoll  : 返回 64, 检查   64 个 fd (只数返回的), 出内核 768 B = 64 x 12
```

The numbers of checks should be read one by one. select checks both times 1002: it scans by fd number, 500 pipes opened 1000 fds, we only registered 500 read ends, but nfds was topped at 1002, the unregistered write end numbers also lie in the scanning range, and the empty positions are also walked through. poll is steady at 500: the number of entries checked is unrelated to the fd number. epoll gives 1 and 64: wait brings the ready items directly back, and the number of returns is the number of items to be processed. The smaller the ratio of readiness, the greater the gap.

The number of bytes in and out of the kernel, the output also counts for us. select's bitmap is copied into the kernel as 128 bytes, and copied out as the same 128 bytes, unrelated to the number of ready items. poll copies in and out 4000 bytes per round (500 entries x 8 bytes), revents is overwritten in the same address, the array for the next round can be reused, but the copy volume cannot be saved. epoll only outputs the ready items, each 12 bytes: the epoll_event structure is marked as packed in the kernel's UAPI header file, four bytes of events are followed by eight bytes of data, with no padding bytes in between, so it's 12 instead of 16.

The details of select's number scanning are currently only considered a small annoyance. At E3's 500 level, it will become the structural reason why select is slower than poll, and we'll put the words here.

## E3: Cost per round, how it grows with N

E2 looked at the scanning volume of a single wake-up, E3 replaces it with the total cost of a steady-state loop: N pipes, only the 0th has one byte of event per round, the three steps of the loop body are write one byte, wait, and read to take it away. The number of rounds per level is 2000, and the median is taken after repeating 5 times. select rebuilds the bitmap every round (why must it be rebuilt? E4 will explain it soon), poll's array is entirely reused, and epoll's registration is done once and remains in the kernel. Timing is done with CLOCK_MONOTONIC, and we look at the output:

```text
每轮耗时(us/round, 2000 轮 x 5 次取中位, WSL2 口径):
     N |       select |         poll |        epoll
    64 |      2825.96 |      2527.21 |       994.39
   500 |     14363.19 |     12814.89 |       993.85
  4096 |  (fd 超界) |    104460.23 |       985.20
```

The header line's us/round, I have to be honest with you: it's a typo in the archival script's printf. The now_ns timing function returns nanoseconds, and the total duration divided by the number of rounds gives the nanoseconds per round, so 2527.21 should be read as 2527 nanoseconds, about 2.5 microseconds. To verify the unit, I re-ran the archival program on the same machine: the three numbers for the 64 level are 2916, 2703, and 1063 nanoseconds, the structure and magnitude are completely consistent with the archival. The first run and the re-run outputs in the archival are also consistent with each other (the re-run poll is 2553, 12757, 103076, and the three levels of epoll are 992, 987, 1044). The unit was misread, but the numbers don't need to be changed, we look at the slope.

The slope of poll is the most expensive in this article: N goes from 64 to 500 and then to 4096, the time per round increases from 2527 to 12815 and then to 104460 nanoseconds. The latter part of N is multiplied by 8, and the time increases by 8.1 times, a standard linear growth: each round of poll copies the entire array into the kernel, the kernel checks each one by one, and then copies the results back. For N=4096, the transportation is 32768 bytes per round, plus 4096 entries checked one by one. The three levels of epoll are 994, 994, and 985 nanoseconds, unchanged: registration is done in the kernel, the collection of ready items is done by the kernel, and the waiting call only copies back the ready item each round. Its cost per level is about 1000 nanoseconds, the magnitude is the price of three system calls: write, wait, and read, and it has no relation to N anymore. The archival output also has a section comparing the number of bytes entering the kernel per round, we bring it over as is:

```text
每轮进内核的字节数(只算等待调用本身):
N=64: select 位图 128 B(重建后拷入), poll 512 B 拷入拷出, epoll 只出就绪项 12 B
N=500: select 位图 128 B(重建后拷入), poll 4000 B 拷入拷出, epoll 只出就绪项 12 B
N=4096: select 位图 128 B(重建后拷入), poll 32768 B 拷入拷出, epoll 只出就绪项 12 B
```

We look at select, its 128 bytes are never changed, but that's at the cost of rebuilding each round, the bitmap is still the same block. poll's byte count follows N, from 512 to 32768, the slope is the same as the cost. epoll's output is always 12 bytes, because the kernel only returns the ready item each round.

Select is a bit slower than poll at the 64 level (2826 vs 2527), and the gap opens up at the 500 level (14363 vs 12815). The伏笔 planted in E2 is now realized: select scans up to nfds by fd number, 500 pipes top nfds at around 1000, the scanning volume is twice the number of poll entries. At the 4096 level, select is absent: one pipe takes two fds, the read end fd number of 4096 pipes has already reached 8193, the fd_set's 1024 bits can't fit, the code leaves it empty, and the output "(fd out of bounds)" is what it says. This empty space itself is the weight of E1's boundary in engineering: other APIs are still moving up, select has no qualification to participate in this level.

> The first version of this experiment in the archival README was once stuck: if the first byte is not pre-set in the loop, the first round of wait will time out and return 0, the following read will block indefinitely on the empty pipe, and the entire benchmark will be stuck there. The fix was to feed one byte before entering the loop, and then each round's write is automatically followed. If you modify this code, the first byte is the first thing to check.

## E4: select rewrites the parameters you pass in

The reason why select rebuilds the bitmap every round is due to one behavior of select: it uses the parameters you pass in as a draft paper. POSIX.1 does not fix the content of the timeout after return, leaving the choice to the implementation, Linux chooses to change it to "the remaining time that hasn't been consumed", other implementations may not do this, the man select(2) portability note specifically mentions this. We tested it: the event arrives at 300 milliseconds, the budget is 2 seconds, after returning, we look at what's left in the structure:

```text
[i] select 返回 1, 距开始 301 ms, timeout 结构体剩余 1.699 s
    300 ms 的事件用掉 300, 剩余约 1700 -> select 改写了入参 (POSIX 未定义, Linux 的行为)
```

The remaining in the timeout structure is exactly 1.699 seconds, the 300 milliseconds event exactly used 301 milliseconds. To write a loop that waits for 2 seconds per round with select, we have to reset the timeout every round, relying on it to keep the original value is not feasible. We have to say two layers about pselect: the glibc pselect function does not change the timeout you pass in, this is what POSIX requires, and the implementation is achieved by padding a local variable in the encapsulation. The kernel's pselect6 system call does change it, the man select(2) explanation is clear, and the author also tested it with a bare syscall: the timeout given is 2.000000000 seconds, and a ready fd immediately wakes it up, the remaining after return is 1.999998417 (the author's reading for this round), the glibc pselect remains unchanged. So the pselect not changing the timeout is what the glibc implements, and the kernel itself is not so polite.

The fd_set itself is also rewritten. We let the bitmap hang on A(fd=3) and B(fd=5) at the same time, in the first round, both pipes are empty, and 200 milliseconds timeout returns 0. After returning, we count how many fds are left in the bitmap: 0. Select changes the bitmap to "the list of ready items for this round", and since there are no ready fds in this round, the list is empty. Next is the most important part of this experiment: after waiting for t+700ms, B really has bytes (we tested it with poll, revents reports POLLIN), we take the bitmap that has been emptied and call select again, the result is that after waiting 300 milliseconds, it returns 0, and B's event is missed. The comparison after rebuilding the bitmap, the output speaks for us:

```text
[ii] 位图初始挂 A(fd=3) 与 B(fd=5), 先看第一轮后位图剩什么
第一轮(挂 A+B, 都无数据): 返回 0, 耗时 200 ms, 位图里剩的 fd 数 = 0
此刻 B 里有数据吗: poll 说 revents=POLLIN(有)
不重建直接再 select: 返回 0, 耗时 300 ms -> B 的事件被漏掉 (位图已被上一轮吃空)
重建后再 select: 返回 1, 耗时 0 ms -> B 立刻就绪 (fd=5, B 在位)
```

The two behaviors come together, and the select loop can only honestly reset the timeout and rebuild the bitmap every round, the repeated labor has already been priced in E3. Poll's comparative behavior is much gentler: the kernel overwrites revents in the same address, the events field remains unchanged, and the array for the next round is reused. Epoll is even more straightforward, the interest table is resident in the kernel, and there is no round of passing the list.

## E5: LT and ET, behavior comparison on the same pipe

epoll has two trigger modes, level-triggered LT and edge-triggered ET. Their kernel-level differences, how the ready queue is collected, and where the callback is hooked, are the main lessons in the [epoll article in the networking volume](../../../networking/02-epoll-io-multiplexing.md), we only do one thing here: the same pipe, the same batch of data, and we put the behavior of the two modes side by side. The experiment setup is simple: 60000 bytes are written into the pipe in one go (default capacity 65536, one go is enough, it's what we tested in the IPC article), the read and write ends are both set to O_NONBLOCK, each time we wake up and read 4096 bytes, we intentionally don't read it empty, and see how the two modes deliver the remaining data.

The difference between the two modes in the code is easy to see: when registering, we add one more EPOLLET to the events, and the behavior changes:

```cpp
// e5_lt_et_pipe.cpp(节选):LT 段与 ET 段的注册,只差一个 EPOLLET
// LT 段:
epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = p[0];
epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
// ET 段(另一段作用域里,同样的三步):
epoll_event ev{}; ev.events = EPOLLIN | EPOLLET; ev.data.fd = p[0];
epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
```

We start with LT. The readiness condition doesn't disappear, and every wait reports it. As long as the data in the pipe isn't exhausted, epoll_wait wakes us up once, reads 4096, the next round of wait reports again, we read 4096, and wakes up 15 times in a row, converging all 60000 bytes, the last round of 200 milliseconds has no activity, and the loop finishes.

ET only recognizes one change: from empty to full edge. The same 60000 bytes, only wakes up once, reads 4096, the remaining 55904 bytes are no longer reported, we wait another 300 milliseconds, returns 0. How to collect it? Write 1 byte, create a new edge, wait immediately returns 1, this round of loop reads to EAGAIN, reads a total of 14 times. After reading empty, we wait another 200 milliseconds, the output writes it as quiet. The total number of wake-ups is 2, and the bytes read are 60001.

```text
LT: 写入 60000 字节, 每次醒来只读 4096
LT: 被叫醒 15 次, 累计读到 60000 字节, 收敛耗时 200 ms

ET: 写入 60000 字节, 醒来只读一次 4096
ET: 醒来 1 次后再等 300 ms -> 返回 0 (管内仍剩 55904 字节, 无人再报)
ET: 再写入 1 字节 -> epoll_wait 返回 1, 这一次按纪律循环读到 EAGAIN:
      read -> -1 (Resource temporarily unavailable), 共 14 次读完
ET: 读空后再等 200 ms -> 返回 0, 安静
ET: 总计醒来 2 次, 读到 60001 字节, 耗时 501 ms
```

The two ET standards are both exposed in this comparison: non-blocking fd, plus the loop that reads to EAGAIN. Without non-blocking, the read after the loop would block instead of returning EAGAIN, and we would see the event loop stuck at this step. Without the loop, the remaining data that wasn't read would never be reported again, 55904 bytes is the current scene. The networking volume's epoll article reproduced the same thing with sockets, the mechanism of losing bytes is the same: the socket scene loses 87KB in one go, the pipe scene loses 55904 bytes.

## E6: The interest table is in the kernel, fdinfo can be viewed directly

Every epoll_ctl ADD is a registration on the kernel's interest table. The registration table is usually only accessible through the API, but /proc/self/fdinfo/<epfd> actually lays it all out: the tfd line is the registered fd, one registration takes up one line. We register three things: the read end of a pipe with EPOLLIN, the read end of another with EPOLLOUT, and eventfd with EPOLLIN plus EPOLLET, then read epfd's own fdinfo:

```cpp
// e6_fdinfo_interest.cpp(节选):fdinfo 的读取就是普通的 open 加 read
static void dump_fdinfo(int fd, const char* when) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/self/fdinfo/%d", fd);
    std::printf("--- fdinfo(%d) %s ---\n", fd, when);
    int f = open(path, O_RDONLY);
```

```text
注册: pa读端(fd=3, EPOLLIN) pb读端(fd=5, EPOLLOUT) eventfd(fd=7, EPOLLIN|EPOLLET)
--- fdinfo(8) ADD 三个之后 ---
pos:	0
flags:	02
mnt_id:	16
ino:	1041
tfd:        7 events: 80000019 data:                7  pos:0 ino:411 sdev:10
tfd:        5 events:       1c data:                5  pos:0 ino:5c969a sdev:f
tfd:        3 events:       19 data:                3  pos:0 ino:5c9699 sdev:f
tfd 行 = 兴趣表里在册的 fd, events 是十六进制: 0x19=EPOLLIN|EPOLLERR|EPOLLHUP, 0x1c=EPOLLOUT|ERR|HUP, 0x80000019 再加 EPOLLET
内核给每个注册都自动补上 EPOLLERR|EPOLLHUP, 所以看到的不是裸的 1 与 4
--- fdinfo(8) DEL pb 之后 ---
pos:	0
flags:	02
...
tfd:        7 events: 80000019 data:                7  pos:0 ino:411 sdev:10
tfd:        3 events:       19 data:                3  pos:0 ino:5c9699 sdev:f
--- fdinfo(7) eventfd 写入 3 之后 (counter 是内核态, fdinfo 可见) ---
pos:	0
flags:	02
...
eventfd-count:                3
eventfd-id: 240
eventfd-semaphore: 0
read(eventfd) = 3, 读走即清零
--- fdinfo(7) 读走之后 ---
pos:	0
flags:	02
...
eventfd-count:                0
eventfd-id: 240
eventfd-semaphore: 0
```

After DEL: the pb line disappears on the spot, and the registration table is left with only two entries. We don't call any interface, and the in and out of the interest table are directly visible. More interesting is the events field: we registered EPOLLIN, but the kernel records 0x19. We compare it bit by bit, 0x19 = 0x01(EPOLLIN)| 0x08(EPOLLERR)| 0x10(EPOLLHUP): the kernel automatically adds them to each registration, so you never have to write them yourself, and the kernel will definitely report an error if something goes wrong. The record with EPOLLOUT is 0x1c, which is the combination of 0x04 | 0x08 | 0x10. The eventfd with ET records 0x80000019, the highest bit 0x80000000 is the bit for EPOLLET.

The data field in the tfd line is also worth a look: it's the payload we put into the epoll_event when registering, the kernel keeps it as is and returns it as is, when the wait reports ready, we use it to identify which event belongs to whom. The experiments in this article store fd numbers, and in engineering, there are also many who store pointers, and the kernel is indifferent, it only does the work of moving. The data in fdinfo: 7 is the 7 we passed when registering eventfd.

We also take a look at eventfd's own fdinfo: eventfd-count is its kernel state count, after writing 3, count becomes 3, read reads it away, and the count returns to 0. The eventfd-id and eventfd-semaphore are two other fields next to it, the semaphore item has been in the kernel since 6.5 (merged in 2023), the format of fdinfo follows the kernel version, when you rerun, please use the one on your machine.

Observing fdinfo is not the first time in this volume. In the [IPC article](../process/03-ipc.md), we have already used the pos field in fdinfo to prove the offset sharing of passing fds via SCM_RIGHTS. The line that everything is fd and can be fdinfo can be extended further in this volume: if you want to know what state the kernel is keeping for you, procfs gives it on the way, you don't have to wait for a dedicated observation interface.

## Engineering Trade-offs

After we've finished the six experiments, the selection judgment can be based on the numbers. The 1024 limit of select is from glibc, and in engineering, it's treated as a hard limit: the bitmap size is part of the ABI, the fortify guard turns out-of-bound into abort, and the side effect of the parameter being rewritten forces a rebuild every round. What it brings in exchange is also real value: portability is the best among the three generations, it's available from various POSIX platforms to Windows sockets, and it's still usable in situations where the number of fds is small and fixed.

Poll replaces the bitmap with an array of entries. 1024 is gone, the number of entries is not controlled by glibc, we tested 1050 and it worked normally, revents is still overwritten in the same address, and the array is entirely reused. The cost is in the slope of E3: every round is fully in and out, for N=4096 it's 32768 bytes per round plus the linear cost of checking each entry one by one.

Epoll is exclusive to Linux, other systems have their own equivalents, BSD's kqueue, Windows's IOCP. It brings registration to stay resident, waiting cost is unrelated to N, the flat line in E3 is its confidence, and fdinfo also gives a free way to see the kernel's state. The boundary is repeated: regular files can't enter epoll, epoll_ctl returns EPERM, the first article on file I/O [man's original words](../file-io/01-posix-file-io.md) explains it, the third article in this directory's E4 will test it on-site, multiplexing is originally the world of sockets and pipes. In situations where there are only a few fds: all three are acceptable, don't add an extra epfd management just for epoll.

Another direction is also mentioned. Windows's waiting API is WaitForMultipleObjects, its handle limit MAXIMUM_WAIT_OBJECTS is written in the SDK header file, the hard-coded value is 64, according to the documentation, passing it over directly fails. Both are documented upper limits, but this 64 is blocked by a hard check at the API entrance, while our E1's 1024 actually lies in the glibc data structure. When scaling up to large numbers, Windows's path is asynchronous IOCP, which is the same category as the third article in this directory's io_uring, we'll meet again then. The new kernel's IoRing also follows the same idea, we won't expand it here, we only recognize the direction.

In the next article, we'll also turn time and events into fds: timerfd turns timers into fds, eventfd turns notifications into fds, the eventfd counter that appeared in E6 will be the main character there. They will be hooked into the same epoll loop, and the way they intersect to collect events on a single timeline is the comprehensive experiment there. The boundaries and costs of the three generations of waiting APIs, we've seen them here today, all the numbers are in the archives, you can always run them yourself.
