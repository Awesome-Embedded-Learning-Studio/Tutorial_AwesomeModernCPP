---
title: "Signals (Part 2): Real-Time Signals, signalfd, and pidfd"
description: "Beyond handlers, what other ways can signals be consumed? This article measures real-time signal queuing for real (three consecutive SIGRTMIN+1 sends while blocked run the handler 3 full times, with 101/102/103 arriving in FIFO order, versus SIGUSR1 where three sends record only 1), si_value from sigqueue read back verbatim (union sigval as the same bytes read two ways), a surprise in delivery order (the kernel dequeues lower numbers first, but with an empty sa_mask releasing a batch, signal frames stack in dequeue order and the frame stacked later executes first, so the handler sees the higher number inverted, while same-number FIFO always holds), signalfd's block-first discipline (without blocking, the signal is consumed by the handler — proven by a 200ms poll with zero events and a lost signal), two-way mutual exclusion of read-then-consume, one read returning 384 bytes / 3 records, a 151/202/301/402ms interleaved timeline when hung together with timerfd, pidfd as the third way to reap (after poll reports POLLIN, waitid(P_PIDFD) gets exit code 42, and polling again after reaping yields POLLHUP), pidfd_getfd under yama=1 passing from parent to child while siblings get EPERM, a real live pid-reuse race (kill hits an innocent new process, pidfd_send_signal gets ESRCH), sigwaitinfo and signalfd contending for the same pending queue with two-way exclusion and whoever reads consumes, and a full graceful-shutdown state machine driven by signalfd inside an event loop (RUNNING/DRAINING/REAPING/EXIT, contrasted with the handler-sets-a-flag version where under SA_RESTART the flag is raised but never seen and an extra connection gets accepted), closing with a quick-reference table mapping ten signal facilities to their x86-64 syscall numbers"
chapter: 8
order: 5
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "信号(上):sigaction 与异步信号安全"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "信号(上):sigaction 与异步信号安全"
  - "控制台事件与 APC"
  - "异步 I/O 与事件循环"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
  - 状态机
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/process/05-signal-advanced.md
  source_hash: 4026796ba329c25f3482678ed2341e33c99960ea473ab7666a4b01d4e5168677
  translated_at: '2026-11-22T15:34:53+00:00'
  engine: glm-5.3
  token_count: 22631
---
# Signals (Part 2): Real-Time Signals, signalfd, and pidfd

In [the previous article](./04-signal-basic.md) we set up the world of handlers: sigaction can be installed and taken down, the only legal actions left inside a handler are the async-signal-safe minority like `write` and `_exit`, and the pairing of EINTR with SA_RESTART after a blocking call gets interrupted by a signal was also settled. In E5 of that article we also built a self-pipe: the handler only `write`s one byte into a pipe, the main loop polls the pipe, and that is how a signal becomes an event. I left a remark back then: this user-space imitation has a kernel-native replacement, and this article brings it out — and it does not come alone.

The question this article answers fits in one sentence: apart from handlers, what better ways are there to consume signals? The half-sentence left at the end of the previous article picks up right here: the real-time signal part was left for this installment. The queuing lesson arrives now: three sends of the same number are no longer merged into one record, and each can even carry a small payload. Let us push one level deeper: can we skip installing a handler altogether? signalfd turns the signal itself into an fd — you `read` it, you `poll` it, and from then on signals and timers live in the same poll table — while sigwaitinfo hands the initiative back to the main flow, which fetches signals by hand at a point it chooses. The process end has a symmetric question: can waiting for a child to exit also be done as waiting on an fd? pidfd handle-ifies that too — we poll one fd for readability and that means the child has exited; sending signals, reaping, and even taking the other side's fd all go through handles. All of these pieces finally come together in a real mini prefork echo server (prefork means forking a pool of workers up front, before the real work starts), and we run the full timeline of a graceful shutdown for you.

The experiments are numbered E1 through E6, mapping one-to-one onto the six directories 01 through 06 under `code/volumn_codes/vol8/systems-programming/linux/process/05-signal-advanced/` in the repository; the code together with all raw output is archived, so you can cross-check at any time. Most output blocks in the text are excerpts; deleted scene headers and annotation lines are marked with `...`, and when checking against the archive, the archive is authoritative. The E numbers in this article belong to this article alone — the previous article has its own E series and the two have nothing to do with each other, so just match directory numbers when browsing the archive. The experimental code in the archive is mostly written against bare syscalls, for the sake of putting the mechanism in plain sight; in engineering code these fds would simply be handed to `unique_fd`, a technique already prepared in the [RAII article](../../thinking/01-raii-paradigm.md).

As usual, the environment is stated up front, because every number later must be checked against it: the experiments ran on my WSL2, the kernel is build 6.18.33.2-microsoft-standard-WSL2, the CPU is an AMD Ryzen 7 9700X, g++ is version 16.2.1, compilation is uniformly `-std=c++20 -Wall -Wextra -O2`, glibc is version 2.44, and every experiment came back with zero warnings. Three environmental facts directly shape several of the claims. On this machine `/proc/sys/kernel/yama/ptrace_scope` is 1, and E3 computes the permission boundary of pidfd_getfd against it. The `pid_max` of the root pid namespace on this machine is 4194304, which is why reproducing the reuse race needs the help of namespaces — E3 goes into the details. The marker file path for E3 is hard-coded as `~/lp05_scratch/e3/`; if the file is missing, the program creates one with the same content itself, so reruns are unaffected. All `.out` files come from the same run on 2026-10-04; pids, port numbers, and millisecond timestamps change on every rerun — what we cite is orderings and magnitudes, never any specific value.

## E1: Real-Time Signals: Queuing, Payloads, Arrival Order

### Scope: 34..64, with two numbers in the middle taken by glibc

Real-time signals are the numbered range from SIGRTMIN to SIGRTMAX, and their statutory treatment is queuing — also their biggest difference from standard signals. The range probed on this machine looks like this:

```text
[a] 范围:SIGRTMIN=34 SIGRTMAX=64 __SIGRTMIN=32
    libc 的 SIGRTMIN 比内核 __SIGRTMIN 大 2:NPTL 内部占用 32/33 两号,
    用户可用的实时信号共 31 个(34..64)。本实验用 SIGRTMIN+1=35 与 SIGRTMAX=64
```

The kernel-native start of the range, `__SIGRTMIN`, is 32, yet the SIGRTMIN we read from `<signal.h>` is 34. The two numbers in between, 32 and 33, are taken by NPTL — glibc's native POSIX thread library, which uses two numbers internally for thread management. The advice from man 7 signal(7) is worth copying down: never hard-code the numbers into code; if you must write one, write `SIGRTMIN+n`, and compare against SIGRTMAX at runtime, otherwise switching to another libc implementation breaks the mapping.

### Queuing: three sends, three arrivals; a standard signal records three sends only once

Queuing deserves a precise statement: when the same number is sent repeatedly, the kernel keeps a fresh record every time — merging does not exist. In E1 of the previous article we measured the other temperament of standard signals: when SIGUSR1 is sent 3 times during a blocked interval, only one bit stands in the pending bitmap (sent, not yet delivered), and after unblocking the handler runs only 1 time. So how do real-time signals behave? We block both SIGRTMIN+1 and SIGUSR1, send each 3 times in a row, then unblock both together:

```text
[b] 阻塞期连发 3 次,解阻塞后:
    SIGRTMIN+1(sigqueue×3,编号 101/102/103) → handler 共跑 3 次
      第 1 次:signo=35 si_code=-1 sival_int=101 sival_ptr=0x65
      第 2 次:signo=35 si_code=-1 sival_int=102 sival_ptr=0x66
      第 3 次:signo=35 si_code=-1 sival_int=103 sival_ptr=0x67
    SIGUSR1(kill 语义×3,编号 1/2/3) → handler 共跑 1 次
      第 1 次:signo=10 si_code=-1 sival_int=1 sival_ptr=0x1
```

Look: the same program returned two different answers. Number 35 ran the full 3 times, all three of 101/102/103 arrived, and the arrival order matched the send order — exactly the FIFO (first in first out) temperament, where the order sent is the order of arrival. Number 10 ran only once, without a trace of the last two sends. The sending interface also matters: these three sends used `sigqueue(2)`, an upgraded kill whose third argument carries a `union sigval` — a union that can hold either an int or a pointer. As long as the handler side is installed in the SA_SIGINFO style, we can read it back verbatim from `si_value`.

There is one more detail in the payload part — when you see it, do not file it as a bug: on the same line where `sival_int=101`, the printed `sival_ptr` is `0x65`, which is exactly 101 in hexadecimal. `union sigval` is after all a union; 101 and 0x65 are the same bytes read two different ways. Scene [c] of E1 sends a pointer the other way around: `sival_ptr` carries the address of a static local variable in this process, and the handler dereferences it and reads 4242 — pointers within the same process are alive. Of course, if the sender lives in another process, the value becomes a bare address number and dereferencing it is meaningless; for cross-process communication you still want to carry an int.

### Arrival order: the kernel dequeues lower numbers first, yet handler execution comes out inverted

Ordering is the first surprise of this article, so it gets a section of its own. The intuitive picture is: kernel delivery respects order, lower numbers first, so handler execution should also run from the lowest number up to the highest. But the first batch of measured data overturns that expectation:

```text
[d] 到达顺序
  d1) 空 sa_mask,一次解阻塞 {SIGRTMAX, SIGRTMIN+1×3}:
      入队顺序:先 SIGRTMAX(=900),后 SIGRTMIN+1(=201/202/203)
      解阻塞前 sigpending:SIGRTMIN+1=pending, SIGRTMAX=pending(两号都在账上)
    实际执行顺序 → handler 共跑 4 次
      第 1 次:signo=64 si_code=-1 sival_int=900 sival_ptr=0x384
      第 2 次:signo=35 si_code=-1 sival_int=201 sival_ptr=0xc9
      第 3 次:signo=35 si_code=-1 sival_int=202 sival_ptr=0xca
      第 4 次:signo=35 si_code=-1 sival_int=203 sival_ptr=0xcb
...
```

At enqueue time 64 stood at the front, and at execution time 64 also ran first — it almost looks like higher numbers sort first? But on this single batch of data we cannot tell whether the kernel really picked the higher number, or something else was at work. I was led around by that intuition for quite a while, and only dared to settle the case after the other two lines of evidence were in.

d2 sets the handler-time `sa_mask` to full blocking: while a handler executes, no other number can come in — one at a time only. The enqueue order of the same pending batch of four (SIGUSR1, SIGUSR2, SIGRTMIN, SIGRTMIN+1) was 12, 35, 10, 34, yet the actual execution order was 10, 12, 34, 35. d3 abandons the handler altogether and fetches one by one with sigwaitinfo; the same pending batch also comes out as 10, 12, 34, 35. Three lines of evidence point to the same fact: kernel dequeue puts lower numbers first. The wording in man 7 signal(7) confines this ordering to real-time signals: among distinct real-time signals, lower numbers are delivered first. The mutual order of standard signals is unspecified by the man page; our observation of 10 ahead of 12 rests on the measured backing of d2 and d3. We also noticed the two standard signals 10 and 12 ranking ahead of the two real-time signals 34 and 35 — POSIX specifies no order for mixed pending sets, and Linux's practice is to put standard signals first, which again matches the man page.

So where did the inversion in d1 come from? We look for the mechanism on the stack. An empty `sa_mask` means that on the way back from unblocking, the kernel picks pending signals one by one in order, and for each pick it builds a signal frame and pushes it onto the interrupted thread's stack. The first pick was 35 (201), and its frame went on the stack; once 35 is in delivery, the number blocks itself, so the next round of picking lands on 64, and 64's frame stacks on top of 201's. When control returns to user mode, execution lands on the frame at the top of the stack, so 64's handler ran first — that is the entire mechanism of the inversion; the kernel never kept any record of picking the higher number. After 201's handler exits and its frame is popped, 35 returns to a deliverable state, 202's frame pushes in at that moment, and 203 follows on the same beat: the three frames of one number are three independent deliveries — each arrives, each gets blocked, then each gets released, never a batch pushed onto the stack at once. That is why the order of 201/202/203 never budged, and FIFO holds strictly under both configurations.

The engineering rule to note: do not depend on handlers for cross-number execution order. If you want to consume signals in order, sigwaitinfo and signalfd are the right tools — they deliver in dequeue order, lower numbers first, as we are about to see.

## E2: signalfd: Turning a Signal into an fd

### Discipline: block first, create later

signalfd is a syscall that has existed since Linux 2.6.22; in one sentence: it hands you an fd, and once a signal arrives it no longer runs a handler — it becomes readable data on that fd, which you `read` and `poll`. man 2 signalfd states the prerequisite plainly: any signal you want to receive from the fd must be blocked in advance with sigprocmask. Why? Instead of reciting the docs, let us build the counterexample directly: install a handler for SIGUSR2, do no blocking, create the signalfd, then send one signal over:

```text
[a] 未阻塞就创建 signalfd,再发 1 个 SIGUSR2:
    handler 计数=1(信号被传统路消费了)
    poll(signalfd, 200ms) 返回 0,revents=0 → signalfd 上什么都没有
```

The signal arrived, the handler's count is 1, and `poll(signalfd, 200ms)` idled all the way to timeout. An unblocked signal took the traditional path as usual, the handler consumed it on the spot, and nothing at all happened on the signalfd side. That is exactly how lost signals happen: nobody lost the signal — it was consumed down another path, and the place where you are waiting will wait forever. So the discipline is: do the blocking first, create the fd second. Reverse those two steps and the fd becomes a decoration.

"Reading it consumes it" also has a reverse half, and we measured both directions. Scene [b] blocks by the book before creating, then sends 3 SIGUSR2 in a row — a standard signal pends only once, poll responds immediately, and one read returns 128 bytes, exactly one record. The beauty is in the unblocking that follows: the handler count is still 0:

```text
[b] 先阻塞再创建,连发 3 个 SIGUSR2(标准信号只记 1 次):
    poll 返回 1,revents=0x1(POLLIN=0x1)
      读 signalfd:一次 read 返回 128 字节 = 1 条记录
        [1] ssi_signo=12 ssi_code=SI_USER(0,来自 kill) ssi_pid=39851 ssi_uid=1000 ssi_int=0
    解阻塞后 handler 计数仍=0 —— 从 signalfd 读走的信号不会再进 handler
```

The wording in man 2 signalfd: the consequence of read is that those signals are consumed — no longer pending, no longer catchable by the handler, no longer fetchable by sigwaitinfo. Our [a] and [b] are one front and one back, and the mutual-exclusion evidence runs both ways: the handler and signalfd contend for the same pending queue, and whichever reads first consumes; there is no deal where both sides receive. The self-pipe trick in E5 of the previous article — the handler's only job being to write a byte into a pipe for the main loop to poll — was our hand-made user-space eventification. signalfd does the same thing kernel-natively: no handler, no pipe, no byte protocol to invent; a signal is born an event, no aftermarket conversion needed.

### One read, multiple records; how to read the fields

What you read from a signalfd is not a byte stream. It hands out fixed-length `signalfd_siginfo` records, 128 bytes each on this machine. In scene [c] we have a child process send one kill plus two sigqueues, deliberately sending 37 first and 36 after:

```text
[c] 字段解读:fork 子进程发信号(跨进程,ssi_pid 有看头):
      子进程发完后的 read:一次 read 返回 384 字节 = 3 条记录
        [1] ssi_signo=12 ssi_code=SI_USER(0,来自 kill) ssi_pid=39853 ssi_uid=1000 ssi_int=0
        [2] ssi_signo=36 ssi_code=SI_QUEUE(-1,来自 sigqueue) ssi_pid=39853 ssi_uid=1000 ssi_int=22
        [3] ssi_signo=37 ssi_code=SI_QUEUE(-1,来自 sigqueue) ssi_pid=39853 ssi_uid=1000 ssi_int=31
    (ssi_pid=39853 是发送方=子进程;36/37 乱序入队(先 37 后 36),出队按小号先——同 E1)
```

One read brought back 384 bytes, all 3 records in place. On cross-process sends, `ssi_pid` is the sender's pid, `ssi_code` distinguishes kill's SI_USER from sigqueue's SI_QUEUE, and the carried `ssi_int` is right there too. The enqueue order was 37 first, 36 after, yet what was read came out 36 first — dequeue goes by lower number first, exactly consistent with the sigwaitinfo line in E1. The mapping between field names and `siginfo_t` in the handler, laid out as a table:

| siginfo_t (read by handler/sigwaitinfo) | signalfd_siginfo | Value in this experiment |
| --- | --- | --- |
| `si_signo` | `ssi_signo` | 12/36/37 |
| `si_code` | `ssi_code` | SI_USER=0 (kill) / SI_QUEUE=-1 (sigqueue) |
| `si_pid` | `ssi_pid` | sender's pid |
| `si_uid` | `ssi_uid` | 1000 |
| `si_value.sival_int` | `ssi_int` | the int carried by sigqueue |
| `si_value.sival_ptr` | `ssi_ptr` | the pointer value carried by sigqueue |

The remaining `ssi_errno`, `ssi_fd`, `ssi_tid` and friends in the struct correspond to other sources — timers, async IO, hardware faults — none of which this experiment triggers; their individual meanings are in the man page.

### Hung together with timerfd: signals inside an event loop

Looking at signalfd alone, with only it on the table, there is not yet an event loop; in scene [d] we hang it into the same poll as a timerfd. timerfd is the fd-ification of timers, ticking once per 150ms period, while the child process sigqueues signals in at the 200ms and 400ms marks:

```text
[d] signalfd+timerfd 混挂事件循环(时间戳相对循环启动):
    t=151ms 事件循环:timerfd 第 1 次到期
    t=202ms 事件循环:signalfd 读到 signo=12 ssi_int=7 ssi_pid=39854
    t=301ms 事件循环:timerfd 第 2 次到期
    t=402ms 事件循环:signalfd 读到 signo=12 ssi_int=8 ssi_pid=39854
    t=451ms 事件循环:timerfd 第 3 次到期
    t=601ms 事件循环:timerfd 第 4 次到期
    (同一循环里定时器与信号各自就位——self-pipe 的内核原生版)
```

Look at the beats 151, 202, 301, 402 — a record of the timer and the signals taking their seats in the same loop, interleaved, neither interrupting the other. What the previous article's self-pipe needed a handler, a pipe, and a byte encoding to achieve is settled here by one poll table. poll is the plainest tier we use here; the full epoll landscape and event-loop architecture are the main subject of the multiplexing article, and vol5's [Async IO and Event Loops](../../../../vol5-concurrency/cookbook/b-topics/06-async-io-event-loop.md) paved that road long ago — we will not reopen the class here, only leave one sentence: by the time a signal reaches signalfd, it is already a pollable fd, just like timerfd.

Scene [e] adds a usage detail: the mask is alive. Call `signalfd(sfd, &mask, flags)` again on an existing sfd, add numbers to the set, and the fd does not even need replacing — in the experiment, the newly added SIGRTMIN+4 hit on first send. The measured flags behavior is also worth a note; I probed it separately with fcntl: create with SFD_CLOEXEC but leave it out on the mask-change call, and the `FD_CLOEXEC` bit is still there; create without it but pass it during the mask change, and the retrofit never happens. The close-on-exec property is fixed at the moment of creation, and changing the mask cannot touch it — when writing code, give the full flags according to your intent at creation, and do not count on patching later.

## E3: pidfd: Turning a Process into an fd Too

With signals fd-ified, we turn the camera to processes. The idea of pidfd is simple to state: a process can also be an fd. It is a whole road made of three syscalls plus one waiting option: `pidfd_open` (x86-64 number 434) hands you a handle pointing at the target process, `pidfd_send_signal` (424) sends signals by handle, `pidfd_getfd` (438) takes descriptors from the target process's fd table, and once `waitid` is paired with the `P_PIDFD` idtype, reaping also goes by handle.

> The man 2 pidfd_open page says glibc provides no wrapper, so a real call has to go through `syscall(2)`. That sentence is outdated: this machine's glibc 2.44 actually ships `<sys/pidfd.h>` (it has been there since glibc 2.36), and from C this header works out of the box; but this version's header is not wrapped in `extern "C"`, so calling it directly from C++ fails to link, and we have to wrap it ourselves to make it link. The experimental code still goes through bare syscalls, for two reasons: first, we want the three numbers 434/424/438 in plain sight, since E6's quick-reference table cross-checks against them; second, it happens to sidestep the wrapper awkwardness. When documentation and reality each hold half the truth, we speak by measurement.

### The third way to reap: poll plus waitid(P_PIDFD)

We already made our peace with reaping in [the opening article of the process series](./01-fork-exec.md): the parent waitpid obtains the exit status, and only then does the child's zombie state leave the stage. The traditional roads are two: waitpid blocks or polls on the pid number, while SIGCHLD works through a handler as an async notification, followed by a waitpid loop once the notification arrives. pidfd is a third road. In scene [a] we fork a child, have it exit on its own after 300ms, while the parent polls nothing but the pidfd the whole time:

```text
[a] 第三种收尸:pidfd + poll + waitid(P_PIDFD)
    fork 出 c1(pid=678699),pidfd_open → fd=3;子进程 300ms 后退出
    t= 51ms poll(pidfd, 50ms)=0(活着时无事件)
    t=301ms poll 就绪,waitid(P_PIDFD) → 退出码 42(si_code 路径:WEXITED)
    收尸后再 poll(pidfd)=1,revents=0x11(=0x1|0x10 POLLIN|POLLHUP:人已注销,句柄等价于挂断)
    二次 waitid → No child processes(尸已收过;waitpid 二次收也是这个错)
```

While the child lives, poll idles; at the moment of exit the pidfd becomes readable, waitid(P_PIDFD) obtains exit code 42, and the whole run has neither SIGCHLD in the cast nor any polling. After the reap there are two more things to see: polling again yields `POLLIN|POLLHUP` — the process the handle points to has been deregistered, the equivalent of a line hangup. A second waitid returns ECHILD, identical to what waitpid reports when reaping the same zombie twice: dead is dead, and the handle will not let you reap twice.

Scene [b] verifies `pidfd_send_signal` along the way: send SIGRTMIN+1 by handle with `sival_int=777` attached, and the receiving side's sigwaitinfo receives it verbatim, the carried si_value intact down to the last digit. Here is a small measured reef: when the info parameter is non-null, the kernel does not fill in `si_pid` for you — leave it unwritten, and the receiver sees sender 0. The experiment added one `getpid()`, and only then did 678698 appear on the other end. man 2 pidfd_send_signal only says the caller fills in info per rt_sigqueueinfo(2); that half-sentence of silence, we fill in with measurement.

### pidfd_getfd: taking someone else's fd, yama has the say

What `pidfd_getfd` does sounds a bit like trespassing: it copies a descriptor out of the target process's fd table and delivers it into your own hands. In scene [c] we have a child open the marker file read-only (that line of text is pre-placed target content; if the file is missing, the program creates one with the same content itself), the parent takes its fd by pidfd, and `pread` reads out `PIDFD-GETFD-MARKER-31415926` directly. Doing the same thing between sibling processes yields EPERM:

```text
[c] pidfd_getfd(本机 yama/ptrace_scope:1):
    父进程:pidfd_getfd(c3 的 fd=3) → 5,pread 出:「PIDFD-GETFD-MARKER-31415926
」
    兄弟 c4:pidfd_getfd(c3 的 fd) → -1,errno=Operation not permitted(非后代,被 yama 拦)
```

yama is a Linux security module, and `/proc/sys/kernel/yama/ptrace_scope` is its tier knob; this machine's value is 1, meaning attach-style ptrace abilities may only be used on direct descendants. man 2 pidfd_getfd states that the permission check goes through the same set as ptrace (PTRACE_MODE_ATTACH_REALCREDS), with the cross-uid gate being CAP_SYS_PTRACE, the grounds written in the ptrace(2) it references. So parent-to-child passes and siblings get blocked, the boundary following yama's tier exactly — before rerunning on your own machine, that knob's value is worth a look.

### The pid reuse race: numbers can mistake a person, handles cannot

The problem pidfd truly solves is in [d], and we stage the whole play. kill(2) takes a pid number and finds people by number, but numbers are handed out by the kernel cyclically: a process dies, the corpse is reaped, and the number is recycled and reassigned. The window lives right there: you are holding the old number 301, 301 has already been reassigned to a new process, and the moment you kill, the one hit is an innocent passerby. Easy to say, but reproducing it takes some doing: the pid_max of this machine's root pid namespace is 4194304, and pids increase monotonically, so a full lap is on the order of twenty minutes — too long to wait. So the experiment borrows the strength of namespaces: a user namespace plus a pid namespace is the kernel's mechanism for isolating process numbering into an independent world; inside the new pidns we pressed pid_max down to 302, whose legal minimum is 301. There is one more wrinkle: once the allocator's cursor crosses RESERVED_PIDS (the reserved floor of 300 the kernel keeps for its pid allocator), the lower bound fixes at 300, and the low range 2..299 is never allocated again — so we had to burn through the low range to make the target process land in the wrapping 300-to-301 segment:

```text
[d] pid 复用竞态:新 user+pid namespace,pid_max 调到 302:
    [ns 内] pid_max=302;先烧掉低号段(2..299 共 298 个),让 A 落进会循环的段:
    [ns 内] A(pid=301)已退出并收尸,pidfd=4 还在手上
    [ns 内] 第 2 个候选 B 拿到了老数字 pid=301 —— 数字回来了,人不是 A
    [ns 内] kill(数字 301) 返回 0 → 误伤 B;pidfd_send_signal(老句柄) 返回 -1,errno=No such process
    [ns 内] 数字会被复用,句柄不会认错人:这就是 pidfd 的立身之本
    [ns 内] B(pid=301)收到 signo=10 si_pid=1 —— 我不是 A,被误伤了!
```

After A exited and we reaped it, the old number 301 was assigned to the new process B. At that moment `kill(301)` returns 0 — the signal really went out, but the recipient is B, which innocently prints si_pid=1; that 1 is the sender's number: process 1 of the new pid namespace, which is precisely the experiment process that executed the kill — the innocence is all written in the output. At the same moment, the old pidfd in our hand pointing at A goes through `pidfd_send_signal` and returns -1 plus ESRCH. The man 2 pidfd_send_signal explanation of ESRCH fits exactly; the original words say the target process does not exist. My translation: it has terminated, and it has been waited on. A pidfd holds a reference to the kernel's process object; the person is gone, the handle points at a piece of deregistered history, and it will never mistake a new owner. waitpid(pid) and kill(pid) take numbers just the same, and they carry the same defect — the window is simply small in daily life, so most people never run into it.

The three reaping paths, laid side by side, each in its place:

| Path | Basis | When exit is known | Getting the exit status | Typical scenario |
| --- | --- | --- | --- | --- |
| `waitpid(pid,...)` | pid number | blocking wait or polling | return value plus status macros | sequential scripts, a parent managing a few children |
| SIGCHLD handler | signal (may coalesce) | async notification | loop `waitpid(WNOHANG)` | traditional services, shells |
| pidfd | handle (immune to number reuse) | poll readable | `waitid(P_PIDFD)` | unified scheduling in an event loop |

The possibility of SIGCHLD coalescing is a measurement from E6 of the previous article: three children exiting simultaneously stand only one bit in the pending bitmap, the handler is entered once, so inside a handler you must reap in a loop. pidfd carries no such burden: one handle per child, each reaping its own. The detailed treatment of waitpid and the status macros belongs to the opening article of the process series; here we only cover pidfd's new road.

## E5: sigwaitinfo: No Handler — Fetch by Hand at a Chosen Point

At this point in the text, stage order and numbering part ways once: E4 is the grand-finale integration experiment, and we placed it after E5, with the numbering following the archive directories — do not get lost when cross-checking.

sigwaitinfo is the third road for consuming signals: the handler's road is an async jump-in that nobody can time, while sigwaitinfo flips the initiative over: the signal is held outside, the main flow walks to a point of its own choosing and reaches out to take it back — installing a handler never happens at all. In scene [a] we block SIGUSR1 and SIGUSR2; the child sends one sigqueue with a payload plus one kill, and the parent fetches twice:

```text
[a] 阻塞 {SIGUSR1, SIGUSR2},子进程(pid=677793)发了两个信号,主进程同步取:
    第 1 次 取到 signo=10 si_code=SI_QUEUE si_pid=677793 si_value=11
    第 2 次 取到 signo=12 si_code=SI_USER si_pid=677793 si_value=0
    (没装任何 handler:信号被挡在门外,由我在指定的点亲手取)
```

si_pid and si_value all arrived in hand, the same information readable in a handler — only the timing is now yours to set. The timeout variant is called sigtimedwait; in scene [b] we let it idle 200ms, it returns -1 plus EAGAIN, and the measured elapsed time is exactly 200ms. Passing nullptr as the timeout degenerates into sigwaitinfo's eternal wait; passing a zero length is a non-blocking inventory check. Scene [c] re-runs E1's queuing verdicts: the real-time signal's three sends are fetched three times in FIFO order, while the standard signal's three sends inventory to one — change the consuming interface, and not one word of our claims changes.

What truly deserves a second look is [d]: sigwaitinfo and signalfd contend for the same pending queue, and whichever reads first consumes — we measured both directions:

```text
[d] sigwaitinfo 与 signalfd 抢的是同一条 pending 队列:
  d1) 先 sigwaitinfo 一条,再 read(signalfd):
      sigwaitinfo 取到 si_value=21
      read(signalfd) 返回 1 条,ssi_int=22 —— 剩下那条也在这儿
  d2) 反过来,先 read(signalfd) 一条,再 sigtimedwait:
      read(signalfd) 返回 1 条,ssi_int=31
      sigtimedwait 取到 si_value=32 —— 队列不重复,谁读谁消费
```

Two records were enqueued: sigwaitinfo ate one, and only the other remained at the signalfd; the reverse holds identically — no duplication, no fighting. The engineering meaning is one sentence: for the same blocked set, never run two consumers at once — pick one and stay with it. By now all three exits are in place: the handler's async jump-in (previous article), sigwaitinfo's synchronous fetch at a fixed point, and signalfd's fd usage inside an event loop. One blocked set, three consumption postures; which one you choose depends on the program's shape: a dedicated signal thread uses sigwaitinfo, event-loop settings use signalfd, and old code needing the fewest changes stays with the handler.

## E4: Graceful Shutdown: signalfd and pidfd Assembled into a Real Server

The finale experiment is a real mini prefork echo server: three workers are forked up front, the parent does nothing but accept incoming connections and dispatch them to workers via SCM_RIGHTS (the mechanism for sending the fd itself across a Unix-domain socket), and a worker's daily routine is reading one request line, doing 250ms of work, replying one line. The shutdown signals SIGTERM and SIGINT are blocked first by the book and hung on a signalfd; the sender of SIGTERM is a supervisor child process, timed at t=300ms, and the client child's script places an order at t=50 and t=150 each, then attempts one more connection at t=500. Not a single handler lives in this whole machine.

### The control group: a handler sets a flag — SA_RESTART on and off, two different endings

Let us walk through the naive version now, saving the protagonist's entrance for the next section: the main loop blocks in `accept()`, and the SIGTERM handler does only two safe things — raise the `g_stop` flag and write one byte. The naive version runs a script of its own, with two connections at t=50 and t=400 and SIGTERM at t=200, independent of the signalfd version's script — match timestamps against its own when cross-checking. Face one enables SA_RESTART:

```text
[面孔一] SA_RESTART:信号打断 accept 后内核自动重启,旗子没人看
...
handler 跑了:g_stop=1(handler 里只有赋值+write 两件安全的事)
t=503ms [面孔一] 接了第 2 条连接,干完 50ms 活,回了 ack
t=503ms [面孔一] 循环回来看旗:g_stop=1 → 退场(共接了 2 条)
...
```

The signal arrived at t=200 and the handler really did run, but SA_RESTART made the kernel restart the interrupted accept in place, so the main loop never got a chance to look at the flag. It slept all the way to t=503 when the next connection came, accepted the connection it should have refused, finished it, replied, and only then saw the flag and exited. Shutdown was dragged out by 300ms, and one extra request got served. Face two disables SA_RESTART: accept returns EINTR, the main loop looks at the flag on the spot and exits, and the new connection at t=956 is refused — correct. But do not be satisfied too quickly: face two still requires the full pairing of handler, flag, and a flag check at every blocking point — accept, read, recv each have such a post, and missing one reproduces face one. And this pairing tax has to be paid again every time a blocking call is added.

### The state machine: RUNNING to DRAINING to REAPING to EXIT

The signalfd version turns signals into one fd in the poll table, so shutdown becomes an ordinary state transition. To put a state machine plainly: a set of named states plus transition conditions, and at any moment the program lives in exactly one of them. The four states here are RUNNING, DRAINING, REAPING, and EXIT; one `g_state` variable plus a poll table that changes with the state is the entire estate. The full timeline deserves a line-by-line read:

```text
t=  1ms [RUNNING ] 服务器就绪:127.0.0.1:40978,3 个 worker(pid 655995/655996/655997),
    SIGTERM/SIGINT 已阻塞并挂上 signalfd;supervisor 将在 t=300ms 发 SIGTERM
t= 52ms [RUNNING ] accept 新连接 → 派发给 w0
t= 52ms [w0 pid=655995] 接单:「req-1」开始处理(250ms 在途)
t=152ms [RUNNING ] accept 新连接 → 派发给 w1
t=152ms [w1 pid=655996] 接单:「req-2」开始处理(250ms 在途)
t=301ms [RUNNING ] signalfd 读到 signo=15(SIGTERM)——信号作为事件进入循环,不是 handler
t=301ms [DRAINING] 状态机:close(listen_fd),停止接新连接
t=301ms [DRAINING] 状态机:向 3 个 worker 发 drain 指令(干完在途就退)
t=301ms [w2 pid=655997] 收到 drain:队列已清空,退场
t=301ms [DRAINING] w2(pid=655997)的 pidfd 可读 → waitid(P_PIDFD) 收尸完成
t=302ms [w0 pid=655995] 完工:已回「ack(req-1) by w0
」
t=302ms [w0 pid=655995] 收到 drain:队列已清空,退场
t=302ms [DRAINING] w0(pid=655995)的 pidfd 可读 → waitid(P_PIDFD) 收尸完成
t=302ms [client] 第 1 条连接收到:「ack(req-1) by w0」
t=402ms [w1 pid=655996] 完工:已回「ack(req-2) by w1
」
t=402ms [w1 pid=655996] 收到 drain:队列已清空,退场
t=402ms [DRAINING] w1(pid=655996)的 pidfd 可读 → waitid(P_PIDFD) 收尸完成
t=402ms [REAPING ] 状态机:3 个 worker 全部退场,收尸完毕
t=402ms [EXIT    ] 清理退场:关 signalfd、收 supervisor/client
t=403ms [client] 第 2 条连接收到:「ack(req-2) by w1」(SIGTERM 时在途,drain 放它做完)
t=503ms [client] 关停后再连一条:connect → -1(Connection refused)
t=504ms [EXIT    ] 服务进程退出(优雅关闭完成)
```

Let us zoom in on the moment t=301. SIGTERM travels through the signalfd — an event in the event loop, with no standing difference from a new connection or a timer expiry. The loop read signo=15, the state moved from RUNNING to DRAINING, and two things needed doing: one, close the listen fd so that connections outside the door get refused from now on; the other, send the drain command to the three workers. The semantics of drain fit in one line: finish the in-flight work at hand, take no new work. w2 had nothing in hand and exited on the spot; w0's job happened to be finished, and it followed; w1's in-flight job was only finished after SIGTERM, and drain let it finish, reply with ack, and only then exit. Each of the three workers is paired with a pidfd, which is exactly what the DRAINING state's poll table hangs on: whoever becomes readable has exited, waitid(P_PIDFD) reaps them one by one, and once everyone is in, the machine moves to REAPING, then cleans up into EXIT. When the client connects again at t=503, connect returns Connection refused — the door is truly shut — while the t=403 in-flight request's ack also arrived, accurate to the last digit.

Across the entire timeline, not one line of code lives under a handler's constraints. No picking async-signal-safe functions, no checking flags, no EINTR pairings. We translated the signal problem into a state-transition problem — and that is the point of signalfd plus pidfd assembled together.

### Two spots actually hit during development

During development the server really took two tumbles, both worth recording exactly as they happened. The first was about inheritance: the forked workers, supervisor, and client all inherited the parent's listen fd, and a listening socket counts as an open file description whose reference count is shared among all holders — the parent's `close(listen)` only dropped one reference, so listening never actually stopped; the connect at t=503 should still have succeeded, only to find nobody accepting, with the client hanging in read. The fix is simple: the first thing each child does on entry is close its own copy of the listen fd. The second tumble was about SIGPIPE: after shutdown the connect failed, but the client's test code wrote onward anyway, writing into a broken connection — delivering SIGPIPE, whose default action is to terminate the process outright; every subsequent client log line vanished, and it died without a sound. The previous article's E7 covered SIGPIPE's default action and the way out where write gets EPIPE after SIG_IGN; this is the live scene of it actually happening in engineering — one line of `signal(SIGPIPE, SIG_IGN)` in the client settled it.

## E6: Facility Quick Reference: A Syscall Number Table

The ten facilities fd-ified or handle-ified across the two articles, gathered by underlying syscall into one table; old facilities that do not travel the fd road — kill, waitpid, SIGCHLD — take no rows, as their work has been covered in the body of both articles. The x86-64 numbers were all probed and printed live with `<sys/syscall.h>` on this machine (rt_sigtimedwait is natively 128 on x86-64; the time64(421) variant that only 32-bit user space takes is not admitted to the table), and in the archive's `06-facility-map/facility_probe.out` most facilities are accompanied by one real call's return value; the waitid row cites the reaping chain already measured in E3 — you can use it as an availability probe:

| Facility | Underlying (x86-64 number) | Role | Article |
| --- | --- | --- | --- |
| `signal(2)` | glibc wrapper over rt_sigaction(13) | compatibility entry | previous article, E2 |
| `sigaction(2)` | rt_sigaction(13) | the front door for installing handlers | previous article, main line |
| `sigqueue(2)` | rt_sigqueueinfo(129) | send with payload, real-time queuing | this article, E1 |
| `sigwaitinfo(2)`/`sigtimedwait(2)` | rt_sigtimedwait(128) | fetch signals at a synchronous point | this article, E5 |
| `signalfd(2)` | signalfd4(289) | a signal becomes an fd | this article, E2/E4 |
| `timerfd_create(2)` | timerfd_create(283) | a timer also becomes an fd | this article, E2 |
| `pidfd_open(2)` | pidfd_open(434) | a process as a handle | this article, E3 |
| `pidfd_send_signal(2)` | pidfd_send_signal(424) | send a signal by handle | this article, E3 |
| `pidfd_getfd(2)` | pidfd_getfd(438) | take the target process's fd | this article, E3 |
| `waitid(P_PIDFD)` | waitid(247) plus idtype=P_PIDFD(3) | reap by handle | this article, E3/E4 |

Grouped by role: the sending side has three — kill, sigqueue, pidfd_send_signal; the receiving side has three roads — handler, sigwaitinfo, signalfd; the lifecycle side has waitpid and SIGCHLD, plus the pidfd-and-waitid combination. You have probably already seen it: the far end of the receiving side and the far end of the lifecycle side point in the same direction — everything is an fd. Filesystem activity turned into an fd the inotify way, walked through in [that article](../file-io/06-inotify.md); a timer turned into an fd is called timerfd, a signal turned into an fd is called signalfd, and a process turned into an fd is called pidfd. In the multiplexing article's full epoll view, they will all enter the same poll table, scheduled by the same loop — and that is also the next step for E4's server as it scales up.

## How the other side sees it

Windows has no such thing as signals. The C runtime's `signal()` covers only a handful of simulated ones — SIGSEGV, SIGFPE, and the like — and the POSIX family of signal semantics simply does not exist there. Ctrl+C on Windows travels as a console event: the system turns it into CTRL_C_EVENT and hands it to the handler chain registered with SetConsoleCtrlHandler, and every event spins up a fresh thread to run the handler — an entirely different model from Linux signals interrupting the main flow. The Windows-side isomorph of self-pipe and signalfd is a handler that only Signals one event object with SetEvent, while the main thread waits on WaitForSingleObject and wakes the moment it lands; our sister experiment (E6 of the Console Events and APC article) measured it — awake in 79ms, nearly one-to-one with the previous article's E5 timing. As for the pid reuse race that pidfd worries about, Windows never has this problem in the first place: process management there runs on HANDLEs, a handle-based system from birth; the WaitFor family of functions waits on processes, events, and timers by handle, a unified pedigree even older than poll. The full unfolding of this comparison awaits you in the Windows-side article [Console Events and APC](../../windows/process/02-console-apc.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="2"
    title="signalfd(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/signalfd.2.html"
  />
  <ReferenceItem
    :id="3"
    title="sigqueue(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigqueue.2.html"
  />
  <ReferenceItem
    :id="4"
    title="sigwaitinfo(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigwaitinfo.2.html"
  />
  <ReferenceItem
    :id="5"
    title="pidfd_open(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/pidfd_open.2.html"
  />
  <ReferenceItem
    :id="6"
    title="pidfd_send_signal(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/pidfd_send_signal.2.html"
  />
  <ReferenceItem
    :id="7"
    title="pidfd_getfd(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/pidfd_getfd.2.html"
  />
  <ReferenceItem
    :id="8"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
