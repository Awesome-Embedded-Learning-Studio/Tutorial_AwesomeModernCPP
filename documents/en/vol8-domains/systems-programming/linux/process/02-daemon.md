---
title: "Daemons, Sessions, and the Environment: setsid, the Double Fork, rlimit, and environ"
description: "How a process that refuses to die with its terminal deals with sessions and resource limits: setsid makes pid==pgid==sid a trinity and a group leader calling it gets EPERM, while kill(-pgid,SIGHUP) delivers one copy to each of the three group members in the same millisecond and zero to the parent outside the group. The classic daemonization steps measured as a chain of changes: after setsid, TIOCGSID turns ENOTTY yet fd0's readlink still points at the old tty — descriptors do not detach themselves, which is the real reason for the redirection step; fork#1 lets the shell finish early, fork#2 ensures the grandchild can never become the session leader, closing the path where opening a tty reacquires a controlling terminal; what chdir and umask each govern; and the contrast that after daemon(0,0) returns, the process is still the session leader. After close(pty master), the session leader is executed by SIGHUP and the foreground-group members each receive a copy in the same millisecond; nohup's entire strace shows only four things plus exec and zero setsid; the criterion for disown versus setsid is whether sid equals one's own pid. rlimit's EMFILE, the hard limit as a one-way valve, and RLIMIT_CPU's two-stage timeline — soft 1 second with catchable SIGXCPU to hard 2 seconds with SIGKILL and exit code 137. environ's array and strings both live in [stack], the ownership trap where putenv does not copy so modifying the buffer changes getenv's result, the array moving from stack to heap after the first setenv, and the two routes of execle's whitelist versus execve's full inheritance. Closing with /proc's R/S/Z states and cmdline's NUL-joined records, plus the WSL2 account of the orphan adopter being 249's Relay, cross-verified with the fork article."
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 19
prerequisites:
  - "进程创建与生命周期:fork/exec/posix_spawn"
related:
  - "进程创建与生命周期:fork/exec/posix_spawn"
  - "信号(上):sigaction 与异步信号安全"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/process/02-daemon.md
  source_hash: b60108ba970d8279e0fe168c5116be89cd2934f76887dca74beb66f2ea2aeff9
  translated_at: '2026-11-22T16:10:00+00:00'
  engine: glm-5.3
  token_count: 18317
---

# Daemons, Sessions, and the Environment: setsid, the Double Fork, rlimit, and environ

[The previous article](./01-fork-exec.md) walked a process from birth to exit: how fork copies, how exec swaps the body, how zombies get reaped — we did all of it with our own hands. But the processes in those experiments each ran cradled in the shell's arms. When you type a command in the terminal, it is the shell that forked it, and from then on its life and death are bound to two things: the device behind the terminal window, and the shell's own job management. This article is about processes that do not intend to live that way. A daemon (a process that provides a service in the background over the long term) intends to outlive both the terminal and the login session — sshd and crond live exactly this way — and to do so it must rewrite, item by item, every layer of relationship inherited from the shell, and along the way also get a clear view of how big it may grow and what its environment carries.

Let us identify the inherited relationships one by one. The session is the outermost layer: every terminal window you open and every login you make causes the system to create one session for the whole crowd of related processes; the shell enters this session at startup, and the commands it forks afterwards all join the same one by default. The process group is the middle layer and the unit of signal delivery: when kill receives a negative pid, it targets an entire group. Group leader and session leader are two separate identities: a process whose pid equals its own pgid is the group leader, and a process whose pid equals its own sid is the session leader. As you will see later, whether setsid can succeed and who gets executed when the terminal dies both hinge on these two identities. The controlling terminal is the binding between the session and the terminal device — a session has at most one — and whether you press Ctrl+C or close the terminal window, the signals enter through this binding. Daemonization means taking a few APIs and swapping these four layers of relationship out one item at a time; with experiments we photograph the identities before and after each step for you.

The experiments are numbered E1 through E6, corresponding one-to-one with the six directories 01 through 06 under `code/volumn_codes/vol8/systems-programming/linux/process/02-daemon/` in the repository; the code together with all raw output is archived. The output blocks in the text are excerpts; elisions inside a block are marked, and for the trimming at a block's head and tail the archive is authoritative when you cross-check. The E-series is valid only within this article — the E-series of the memory chapter and the file I/O chapter have nothing to do with ours. This article's experiment code uses bare POSIX calls; the [unique_fd](../../thinking/01-raii-paradigm.md) of the earlier articles saw no use this time — on error we simply read the return value and errno. The discipline of allowing only bare write inside signal handlers was first established when [the mmap article](../file-io/02-mmap-memory-mapping.md) measured SIGBUS, carried on by [the virtual memory API article](../memory/02-vm-apis.md) to this day, and we keep it here as usual.

As usual the environment is stated in full, and the later numbers are checked against it: WSL2, kernel build 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1, everything compiled with `-std=c++20 -Wall -Wextra -Wpedantic -O2`, `ulimit -n` reporting 1048576 with soft and hard equal, and strace 7.2. Two environmental facts bear directly on your reruns. The first concerns PID 1: `/proc/1/comm` on this machine reads systemd, because `/etc/wsl.conf` says `[boot] systemd=true`; yet the measured adopter of orphans is not it — the orphan of e4 in the previous article landed on ppid 249, the `/init` relay process WSL assigns to each login session, with comm recorded as `Relay(252)`, which marked itself an adoption candidate via `prctl(PR_SET_CHILD_SUBREAPER)` (subreaper: when the kernel finds a new father for an orphan, it picks the nearest subreaper among its elders; only when none can be found does PID 1 serve as the fallback). The grandchild in this article's E2 likewise gets 249 from getppid() — the two articles' outputs corroborate each other. The other fact concerns our experiment bench: it has no tty of its own, fd0 is a pipe, and for every experiment that needs a real terminal we borrow a pty (a pseudoterminal, a paired set of fake terminal devices) by running under `script -qec '...' /dev/null`; its formal introduction comes in E3.

## E1: Sessions and Process Groups — setpgid Changes One Column, a Group Leader's setsid Gets EPERM

A child born of fork moves into its father's process group and session by default — we ground this sentence with a ps table carrying four identity columns. The experiment lays out three generations: this process, child A, and the grandchild. After its birth, child A calls `setpgid(0, 0)` to move itself to a new group, and the grandchild calls `setsid()` directly for a new session:

```text
    PID    PPID    PGID     SID COMMAND
  13030    2318   13030   13030 zsh
  13049   13030   13030   13030 e1_sessions
  13050   13049   13050   13030 e1_sessions
  13051   13050   13051   13051 e1_sessions
```

Read along the PGID and SID columns. For child A, only the PGID column left the father while the SID stayed put — only the group was changed, the session remains the original one. For the grandchild, both columns became its own pid 13051: `setsid()` does three things at once — it creates the new session and the new process group in one go, then places itself alone inside; in the words of man 2 setsid, the caller becomes the session leader of the new session and the group leader of the new group, and is the only process in the new session. The API readings getsid/getpgid match the four ps columns item by item, and they work for any process — in the experiment we asked about PID 1 with `getsid(1)`, and the answer was 1 itself.

A process that already holds an identity gets stopped at the door when it tries to switch. The experiment collected two EPERMs; let us glance at each:

```text
子A(现组长,pid==pgid): setsid() = -1, errno=1 = Operation not permitted
孙(已是会话长): setsid() = -1, errno=1 = Operation not permitted
```

The reason the group leader is refused actually hides in the numbering: setsid wants to set the numbers of both the new session and the new process group to its own pid, but the group leader's pid is already occupied as the old process group's number — one number cannot identify two groups. The grandchild's EPERM is the other half of the same restriction: a session leader is necessarily its own group leader, so a second setsid cannot pass either. This also explains why the textbooks all have you fork first and let the child call setsid: a forked child is not in its father's group, its pid is occupied by no group, and it is of course forever eligible.

The process group is the unit of signal delivery — we put this slogan to the test as well. In E1b we had the group leader stand ready with two members, and handed the firing job to the father outside the group:

```text
父进程: kill(-13171, SIGHUP) —— 负数 pid = 定向整个进程组 13171
[SIGHUP] pid=13173 t=+101ms
[SIGHUP] pid=13172 t=+101ms
[SIGHUP] pid=13171 t=+101ms
...
父进程: 枪响 300ms 后我什么都没收到(上面没有我的 pid)—— 组外不受影响
```

One kill shot went out, and the three members inside the group each received a copy in the same millisecond, while the father outside the group waited 300 ms and received nothing. When you press Ctrl+C and break both grep and wc on a pipeline, what you rely on is the terminal delivering SIGINT to the entire foreground process group — the same mechanism as this SIGHUP. The full account of the signal family is saved for the later signal articles; here we borrow only SIGHUP, because it is the protagonist of E3.

## E2: The Classic Daemonization Steps — A Measured Chain of Changes

The textbooks each give a daemonization procedure with slightly different step lists but a consistent skeleton. We ran it exactly as archived in e2_daemon_steps.cpp, taking one snapshot at each of the five crucial checkpoints, capturing six things: the four identity items pid, ppid, pgid, sid, plus where fd0 points and whether a controlling terminal exists. The steps and the item each one rewrites — the summary table sits here:

| Step | Action | What it rewrites |
|---|---|---|
| 1 | fork, parent exits | The process's father: handed to the adopter; the shell no longer waits for it |
| 2 | setsid() | Session and process group: creates a new session and group, appoints itself session leader and group leader, cuts the controlling-terminal binding |
| 3 | fork again, session leader exits | The session-leader identity: the grandchild can never hold it; opening a tty can no longer capture it back |
| 4 | chdir("/") | Working directory: no longer squatting on a mount point |
| 5 | umask(0) | The permission filter code for newly created files |
| 6 | redirect fd0/1/2 to /dev/null | Where the three standard streams go |

The last three snapshot items each answer a different question, so let us state the criteria clearly. `readlink("/proc/self/fd/0")` tells us that the fd0 descriptor is open and which file it points to. `ioctl(fd0, TIOCGSID, &sid)` tells us whether this tty recognizes our session as its master; per man 2 ioctl_tty, when the fd is a tty, is not a pty master end, and is not our controlling terminal, the answer it returns is ENOTTY. The equality relations among pgid, sid, and pid answer the identity question. Each of the three criteria minds its own business, and all the later cross-checking leans on them.

The main act is the pair of beats around setsid; the raw output is carried over verbatim:

```text
阶段0 初始                   pid=13247  ppid=13246  pgid=13246  sid=13246
    fd0 -> /dev/pts/10 ; 控制终端:有,该 tty 所属会话 sid=13246
...
阶段2 setsid 后               pid=13248  ppid=13247  pgid=13248  sid=13248
    fd0 -> /dev/pts/10 ; 控制终端:无,ioctl(fd0,TIOCGSID) 失败: Inappropriate ioctl for device
```

Before setsid, pgid and sid were still the father's 13246; afterwards they both became our own 13248 — exactly the trinity E1 described. What truly deserves our pause is the last two lines: fd0's readlink did not change one bit, still pointing at /dev/pts/10, while TIOCGSID went from success to ENOTTY. Said in full: what setsid cuts is the layer of ownership, while not a single descriptor itself is moved. If you thought that after setsid the process is even with the terminal, you thought wrong — reads and writes on fd0 are entirely unaffected, and when the terminal dies, any process holding its descriptor still gets swept in. Here lies the real reason for the classic step of redirecting fd0/1/2: the ownership changed but the descriptors did not, so the detaching must be done by our own hands, and that is exactly what the redirection step does. After the redirection `isatty(0)` returns zero; reading it afterwards always yields EOF, and writing goes silently nowhere.

We merge the whole chain of changes into one table, with all five snapshots collected:

| Moment | pid | ppid | pgid | sid | fd0 points to | ioctl(fd0,TIOCGSID) |
|---|---|---|---|---|---|---|
| 0 initial (in pty) | 13247 | 13246 | 13246 | 13246 | /dev/pts/10 | success, sid=13246 |
| 1 after fork#1 (child) | 13248 | 13247 | 13246 | 13246 | /dev/pts/10 | success (inherited) |
| 2 after setsid | 13248 | 13247 then 249 | 13248 | 13248 | /dev/pts/10 (not detached) | ENOTTY (ownership cut) |
| 3 after fork#2 (grandchild) | 13249 | 249 | 13248 | 13248 | /dev/pts/10 | ENOTTY |
| 6 after redirection | 13249 | 249 | 13248 | 13248 | /dev/null | ENOTTY |

Ask your way down step by step, and every step has its reason. After fork#1, the parent exits at once, and what this step earns is the shell: the shell only waits for the parent it forked with its own hands; once the parent exits, the shell considers the command finished and the prompt returns, while the service keeps running in the child — the lifecycle is decoupled from the shell from here on. A by-product of the parent's exit is that the child becomes an orphan: the measured getppid() changed from 13247 to 249, and the adopter is exactly the Relay introduced at the start. The textbook says orphans go to init — on this machine, that claim takes a discount under the subreaper rule.

The hidden hazard that fork#2 guards against lies in the session-leader identity. After setsid, the child itself is the session leader, and the session leader carries a risk no other process has: under the System V line of terminal semantics, if a session leader opens a terminal device without `O_NOCTTY`, that terminal may automatically become its controlling terminal — the very thing just escaped gets strapped back on. So we fork once more, let the session leader exit the stage, and the grandchild takes over: the grandchild's sid is still that new session, but its pid does not equal the sid; by the identity rules of E1, it can of course never become the session leader — and the session-leader identity is precisely the precondition for automatically acquiring a controlling terminal, a precondition that no longer holds for the grandchild. The snapshot at moment 3 also shows TIOCGSID still reading ENOTTY. The chdir-to-root step likewise serves convenience: if a daemon leaves its working directory on some mount point, that filesystem can no longer be unmounted. The umask step touches that filter code: umask is the permission filter applied when files are created — the mode parameter of open gets shaved by it before taking effect; the default 0022 handed down by the shell shaves off group-write and other-write. A daemon that wants file permissions decided entirely by the mode written in its code measured it cleared from 0022 down to 0000.

One link in this experimental setup deserves its own account, because it nearly took the experiment down with it. E2 needs a real terminal, so we run under script — but when the command finishes, script closes the pty it borrowed, and the kernel then sends SIGHUP to that terminal's foreground group, exactly the mechanism E3 is about to explain. Had our parent exited immediately after the fork as the textbook says, script would have closed the terminal master as soon as the command ended — while the child might not yet have finished setsid — and the half-finished daemon would have been carried off by that SIGHUP, leaving nothing in the trace but a silent disappearance. So e2's parent waits over a pipe for the child's "setsid done" notice before exiting, and the archived source comments record this story. A real shell scenario has no such window: the parent exits, but the shell is still alive after all, so the terminal does not close along with it. The experimental apparatus became a live example of the very mechanism it meant to observe — a bite that left me utterly convinced.

glibc wraps this procedure into `daemon(nochdir, noclose)`. The STANDARDS section of man 3 daemon says only None — it is an old function of 4.4BSD lineage that glibc kept. With the same style of snapshot, let us see what it does and what it does not:

```text
调用 daemon() 前          pid=13366  ppid=13365  pgid=13365  sid=13365
    fd0 -> /dev/pts/10 ; 控制终端:有,该 tty 所属会话 sid=13365
daemon() 返回后           pid=13367  ppid=249    pgid=13367  sid=13367
    fd0 -> /dev/null ; 控制终端:无,ioctl(fd0,TIOCGSID) 失败: Inappropriate ioctl for device
...
```

`daemon(0,0)` handles the fork, setsid, chdir to /, and redirection all in one step, but after it returns, pid==pgid==sid==13367 — it still wears the session-leader title. The BUGS section of the man page puts it plainly: this implementation does not use the double-fork approach, so the resulting daemon is the session leader, and under System V semantics, opening a terminal device may drag it back into being a controlling terminal. It blocks no signals and does not handle SIGHUP either, so all this insurance must be added by the caller. Set it against our hand-rolled double-fork version of E2, and the meaning of fork#2 materializes at once: the two versions differ by exactly that one fork — the handmade version's sid differs from pid, while the daemon version's still equals pid.

## E3: The Death of a Terminal — Where SIGHUP Comes From and What nohup Does

E2 kept saying that a dying terminal sends SIGHUP; now we verify that sentence itself. Verification requires a pty — the name came up in the environment baseline, and now we make its full acquaintance. pty is short for pseudoterminal. A pty is a pair of mated devices: we hold the master end, the running program holds the slave end, and from the program's side, opening the slave feels no different from a real terminal. Behind your terminal window sits exactly one pty, and script and sshd rely on it too.

We arrange the experiment with the parent as bystander: `openpty` opens a pty pair; the child becomes the session leader via setsid, then calls `ioctl(sfd, TIOCSCTTY, 0)` to claim the slave as its controlling terminal — the forward operation of exactly the binding that E2 cut — sets its own group as the foreground group, and forks two members fitted with handlers to record timestamps. Once everything is ready, the parent does `close(master)`: to everyone hanging off the slave, this is the terminal dying.

```text
父 pid=13448(旁观者)开了一对 pty:master=4 slave=5
会话长 pid=13449: 控制终端=/dev/pts/10, sid=13449 pgid=13449(前台组)
...
[SIGHUP] pid=13451 t=+151ms
[SIGHUP] pid=13450 t=+151ms
父: waitpid(会话长) -> 被 Hangup 处决(缺省动作如约而至)
```

The session leader, having installed no handler, was executed by the default action; we retrieved the cause of death through the parent's waitpid, and it reported exactly Hangup. The two foreground-group members each received a copy in the same millisecond (t=+151ms), and only their installed handlers left timestamped last words. The kernel's behavior is this: when the terminal connection breaks, SIGHUP goes to the controlling process — the session leader himself — and the members of the foreground process group each get a share as well. That kill(-pgid) shot in E1b was demonstrating exactly the second half. The reason daemonization is a must lands right here: the process at moment 0 of E2 had its life tied to the terminal, and when the terminal died, it had to go along.

Then what does someone reach for who wants no daemonization but only a command that survives a hangup? We summon nohup. Guessing does not pay; we record its entire event sequence with strace. The locale-file attempts at the beginning are noise; the lines worth seeing are these:

```text
openat(AT_FDCWD, "/dev/null", O_WRONLY) = 3
dup2(3, 0)                              = 0
close(3)                                = 0
openat(AT_FDCWD, "nohup.out", O_WRONLY|O_CREAT|O_APPEND, 0600) = 3
dup2(3, 1)                              = 1
close(3)                                = 0
dup2(1, 2)                              = 2
rt_sigaction(SIGHUP, {sa_handler=SIG_IGN, ...}, {sa_handler=SIG_DFL, ...}, 8) = 0
...
execve("/usr/sbin/true", ["true"], 0x7ffffa195740 /* 69 vars */) = 0
```

Everything it does, read straight off the strace, amounts to this: stdin is connected to /dev/null, stdout is rerouted to nohup.out in append mode with 0600 permissions, stderr follows stdout, SIGHUP's disposition is set to SIG_IGN, and at the end it still execs the target command you gave. Search the whole sequence: not a single shadow of setsid or setpgid, and the text of man 1 nohup never mentions either. So a process started by nohup still lives in the original session and process group; when the terminal dies, SIGHUP still arrives — SIG_IGN merely bounces it off. Our verification is equally direct: kill -HUP a nohup-started process and it lives happily, its stdout having long fallen into nohup.out. Its scope of application is now clear: if you only need to survive a hangup, nohup suffices; to truly change session identity, you must turn to setsid.

The shell's disown is a third road at your fingertips; what it changes is the shell's own job table: the job is crossed off, so the shell no longer sends it SIGHUP when it exits, yet the process's sid and pgid remain untouched. The criterion is really one sentence: check whether sid equals the process's own pid:

```text
    PID    PPID    PGID     SID COMMAND
  13950   13949   13947   13947 sleep
...
  13951   13949   13951   13951 sleep
```

Look at the first row: an ordinary background job, with sid 13947 being the shell's session while the process itself stays put. The second row is a job started by setsid, its sid 13951 being exactly its own pid — the session truly changed. disown edits the shell's records; setsid swaps the session identity; the boundary between the two lies right here.

## E4: rlimit — the Soft Limit Warns, the Hard Limit Executes

With identity rewritten, we move on to what else the process carries on its back: resource limits, the rlimits. Every process hangs a set of them; `struct rlimit` has just two fields — rlim_cur records the currently effective soft limit, and rlim_max records the hard limit, the soft limit's ceiling — read and written with `getrlimit` and `setrlimit`.

NOFILE (the number of file descriptors that can be open) is the most intuitive; we press the soft limit down to 4 and then open files:

```text
初始           rlim_cur=1048576    rlim_max=1048576
setrlimit 后    rlim_cur=4          rlim_max=1048576
软限=4 且 fd0/1/2 已占 3 个名额 -> 还能开 1 个(fd=3),之后:
  open #1 -> fd=3(成功)
  open #2 -> -1, errno=24 = Too many open files
  open #3 -> -1, errno=24 = Too many open files
```

The three standard streams already occupy three slots; the 4th open still obtained fd=3, and everything after that uniformly received errno=24, EMFILE. Per man 2 getrlimit, open, pipe, and dup that cross the line all report this — the message says too many files, but the root of the illness lies in the limit. The soft limit itself is bidirectional; in the experiment we raised it back to 1048576 and it succeeded in one try.

The hard limit has a much harder temper; we probed it in four steps:

```text
① 软限升到硬限(1048576)        -> 成功
② 硬限升到 1048577(越权)      -> 失败 EPERM 如约:(errno=1 Operation not permitted)
③ 硬限降到 1024            -> 成功
④ 再升回 1048576(降过之后)   -> 失败 回不来了:(errno=1 Operation not permitted)
```

Let us dig out the man page's account: the soft limit may be adjusted freely within the range from 0 to the hard limit, but raising the hard limit without the CAP_SYS_RESOURCE capability in hand yields only EPERM, and after lowering it, trying to raise it back to the original value meets the same EPERM — it is a one-way valve. That is why the common daemon practice is to pull the soft limit straight up to the hard limit early in startup: the window for maxing out opens only this once, and there is no such shop after this village.

RLIMIT_STACK we single out for a look, because it is the easiest for us to misread:

```text
getrlimit: rlim_cur=8388608 字节 (8192 KiB = 8.0 MiB), rlim_max=unlimited(RLIM_INFINITY)
maps [stack]: 7ffc2fa16000-7ffc2fa38000 rw-p 00000000 00:00 0                          [stack]
当前已映射大小 = 136 KiB(远小于软限,软限是"允许长到的上限")
```

The soft limit's 8388608 bytes converts to exactly 8 MiB, while [stack] in maps had only 136 KiB mapped at the time. With the two numbers side by side, the meaning of the soft limit becomes clear: it is the ceiling the stack is allowed to grow to, not what is already occupied. This also connects to the behavior described in [the memory layout article](../memory/01-memory-layout.md): accessing an unmapped address on the stack does not immediately SIGSEGV — the kernel grows the stack on demand, and it is the access that growth up to rlim_cur refuses that turns into SIGSEGV. The unit is bytes, and I have a selfish motive for spelling it out: the first archived version read 8388608 bytes as KiB, computed an absurd 8192 MiB stack allowance, and got caught by the review. One more thing to record: this rlimit governs only the main thread's stack; in multithreaded programs, other threads' stacks go by the pthread_attr default size.

The CPU two-stage timeline is the most dramatic scene in E4. We set the soft limit to 1 second and the hard limit to 2 seconds, then entered a full-load loop:

```text
主循环: wall=+966ms cpu=+962ms SIGXCPU 已命中 0 次 sum=519999997400000000
[SIGXCPU] wall=+999ms cpu=+998ms (第 1 次命中;软限是警告,硬限 2s 将是 SIGKILL)
主循环: wall=+1003ms cpu=+1002ms SIGXCPU 已命中 1 次 sum=539999997300000000
...
./e4_cpu_run.sh: line 4: 14657 Killed                     ./e4_cpu
驱动观察: 退出码 137 —— 137 = 128+9,即被 SIGKILL 处决(硬限不可协商)
```

When the wall clock reached +999ms, SIGXCPU hit for the 1st time; we looked back and the handler had finished logging while the process was still alive and well — the soft limit grants a warning window, and SIGXCPU is catchable and handleable. The man page adds that after crossing the line, one reissue per second will follow; yet in our loop it hit exactly this once, then the loop ran another full second, and as the wall clock reached around 2000ms, SIGKILL arrived — uncatchable, unignorable. The process exited with code 137, and 137 is exactly 128+9. Those `Killed` lines you have seen in machine logs are most likely it. You may wonder why SIGXCPU hit only once; the answer sits in the timing: the 2nd soft-limit expiry coincided exactly with the hard limit at 2 seconds — SIGKILL and the 2nd SIGXCPU arrived simultaneously, and the latter never got to be delivered. To watch the repeated warnings with your own eyes, raise the hard limit to soft-plus-3-seconds or more and run again.

## E5: Environment Variables — environ Lives on the Stack, putenv Does Not Copy

Identity and limits both verified, one thing remains of what the process inherits wholesale from the shell: the environment variables. main's third parameter envp and the global variable `environ` refer to the same thing: a NULL-terminated `char*` array whose every entry is a string of the form `NAME=VALUE`. And where it lives, we check against maps:

```text
environ 指针数组条目数 = 69
maps [stack]: 7fff3886b000-7fff3888d000 rw-p 00000000 00:00 0                          [stack]
environ 数组本身在     0x7fff3888ad98
environ[0] 字符串在    0x7fff3888b6ab
argv[0] 字符串在       0x7fff3888b69e
...
判定: environ 数组在 [stack] 内? 是 ; environ[0] 字符串在 [stack] 内? 是
```

Both the array and the strings land inside the [stack] range, with argv[0] and environ[0]'s strings only 13 bytes apart. This is what the kernel laid on the top of the stack by its own hand at exec time. In the previous article's exec family, argv and envp appeared only in the role of parameters; where they finally land — what you see here are the measured addresses.

The story of putenv begins with ownership. Ownership asks which piece of memory belongs to whom, and who is responsible for keeping it alive:

```text
putenv 陷阱(man 3 putenv: 字符串归环境所有,glibc 不做拷贝):
putenv(buf 内容 "E5_PUT=aaa") 后 getenv = aaa
只把 buf 改成 "E5_PUT=zzz"     后 getenv = zzz  <-- 环境直接引用了 buf
```

putenv takes a pointer and makes no copy — the original words of man 3 putenv, "the string becomes part of the environment", put it plainly: from then on the string belongs to the environment. In the live test, we putenv a buffer and getenv read aaa; we touched nothing in the environment, only changed the buffer's content to zzz, and getenv immediately followed with zzz — the pointer in the environment array points at our buffer. The inference follows smoothly: pass an array on the stack or a temporary buffer, and once the function returns that memory is gone; environ thereafter carries a dangling pointer, and the next getenv reads reclaimed memory. setenv has no such problem — it takes care of the copying itself, takes name and value separately, and carries an overwrite switch; in the experiment, with overwrite=0 the existing variable stayed put, and with 1 it got overwritten. The behavior matched on every count.

setenv also brought us a relocation:

```text
setenv/putenv 折腾完: 条目数=70, environ 数组现在在 0x63a8d7c5f940
判定: 还在 [stack] 内? 否,已搬到堆上(第一次扩容时 libc 在堆上另建了数组)
```

After our first setenv, the array moved from the stack (0x7fff prefix) to the heap (0x63a8 prefix), the entry count going from 69 to 70, while the string bodies stayed on the stack. This is glibc's implementation behavior: on growth it builds a new array on the heap. POSIX has never promised this; another libc may do it differently, and we record it under this machine's account.

As for how the environment reaches the child, the exec family offers two roads, and we walk each once:

```text
--- 路 1: execle + envp 白名单数组(子进程环境被整个替换) ---
E5_FROM=execle
PATH=/usr/bin:/bin

--- 路 2: execve + environ(子进程原样继承,能看到 E5_EXTRA) ---
...
E5_EXTRA=inherited-via-environ
```

execle's envp is a whitelist: however many entries you give, that is all the new process holds — not even the parent's HOME and TERM are present; the whole world beyond those entries is shut outside the door. execve with environ, on the other hand, is full inheritance: all 70 entries carried over without one missing, including the freshly setenv'ed E5_EXTRA. Each road has its use: building a sandbox or scrubbing the environment takes road 1; ordinary passing takes road 2.

## E6: A Quick Tour of /proc — the R/S/Z States and cmdline

Identity, limits, and environment all covered; for the close, we walk through /proc as an observation post. [The memory layout article](../memory/01-memory-layout.md) in this series drew the full address-space picture with /proc/pid/maps; this article adds a few files about the process itself.

We captured the same binary in three states:

```text
[self status(此刻在运行,State 应为 R)]
State:	R (running)
...
[子进程活着(在 sleep): State 应为 S(可中断睡眠)]
State:	S (sleeping)
...
[子进程已死未收尸(waitpid 前): State 应为 Z(僵尸)]
State:	Z (zombie)
```

R means running on the CPU, S means interruptible sleep, and Z means zombie — that short stretch of being dead with nobody yet having reaped. The three photographs capture two processes of the same program: the R in the first is the parent itself, and the S and Z in the other two are the child it forked — differing only in their respective states. Grab a snapshot with ps, and which photo you catch depends entirely on the instant you press the shutter.

A few words on the cmdline file by itself: it is the argv recorded verbatim by the kernel at exec time, joined item by item with NUL:

```text
[self] /proc/15018/cmdline 共 22 字节, NUL 显示为 \0:
    "./e6_proc\0hello\0world\0"
```

Counting them up, 22 bytes — exactly the length of `./e6_proc\0hello\0world\0`. Reading it requires splitting on \0; with an ordinary string function you would get only the first item. A zombie's cmdline is empty: the address space has been returned, and even argv is gone; and once waitpid has reaped, this pid's directory in /proc disappears entirely. The full two-stage affair of zombies and reaping was explained mechanically in the previous article's E3; what this adds is the /proc-side observation surface — two roads mapping to the same thing.

Three more observation posts for the record. /proc/pid/exe is a symbolic link to the running binary; after the file is rm'ed, it takes on the `(deleted)` suffix. /proc/pid/fd holds one link per open descriptor — the fd0 reading at moment 6 of E2 was read from right here, and one ls tells you where a daemon's three standard streams went. The Uid quadruple in status is, in order, real, effective, saved, and fs; in a setuid program the effective one changes, and at a later exec the saved one decides whether it can change back. This experiment ran as an ordinary user throughout, so all four are equal. The work ps and top do is really just reading these /proc files into tidy form — when strace is not at hand, /proc is the observation post that never closes.

## The Other Side: Windows Without the Session Layer

Windows lacks the entire POSIX organization of sessions, process groups, and controlling terminals. Ctrl+C travels as a console event: the system sends CTRL_C_EVENT to the processes attached to the same console, and each process's own handler (registered with SetConsoleCtrlHandler) decides how to respond; one without a handler exits by default. Something with process group in its name exists over there too, but it is a different thing: a group started by CreateProcessW with the CREATE_NEW_PROCESS_GROUP flag, whose processes are immune to Ctrl+C by default; and over there GenerateConsoleCtrlEvent can deliver in a group-targeted way. Our Windows-side measurements filled in the ground under this default immunity: it is really an implicit ignore bit the process carries, not a delivery boundary the system draws by group membership — one line of the process's own code can lift it, and once lifted, the targeted CTRL_C is received in earnest. Where it mirrors E1b is that a broadcast always has its non-recipients: on our side, the father outside the group received nothing; on their side, the default-state new-group processes do not respond. There is no setsid counterpart over there either — for long-term background services, Windows has its own registration mechanism. The full measurements of console events and APCs are unfolded in the console-events article under windows/process/.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="setsid(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setsid.2.html"
  />
  <ReferenceItem
    :id="2"
    title="setpgid(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setpgid.2.html"
  />
  <ReferenceItem
    :id="3"
    title="credentials(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/credentials.7.html"
  />
  <ReferenceItem
    :id="4"
    title="ioctl_tty(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ioctl_tty.2.html"
  />
  <ReferenceItem
    :id="5"
    title="daemon(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/daemon.3.html"
  />
  <ReferenceItem
    :id="6"
    title="nohup(1)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man1/nohup.1.html"
  />
  <ReferenceItem
    :id="7"
    title="getrlimit(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getrlimit.2.html"
  />
  <ReferenceItem
    :id="8"
    title="environ(7) and putenv(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/environ.7.html"
  />
  <ReferenceItem
    :id="9"
    title="proc(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc.5.html"
  />
</ReferenceCard>
