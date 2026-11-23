---
title: "IPC: Pipes, FIFOs, and POSIX Message Queues"
description: "How two processes exchange data: the pipe sends a byte stream into the kernel's ring buffer — this article measures the timeline of a write blocking after filling 65536 bytes (the 17th chunk's write stuck from +0.2 ms to +2000.3 ms, released the moment the reader took one chunk), SIGPIPE plus EPIPE when the read end is fully closed, and read returning 0 as EOF when the write end is fully closed; pipe2(O_CLOEXEC) in three postures and the engineered dup2 version; strace revealing that glibc 2.44's popen actually goes through pipe2 plus clone3(CLONE_VM|CLONE_VFORK) — the posix_spawn route — rather than the textbook's fork. FIFO's open rendezvous both ways, the trap where O_WRONLY|O_NONBLOCK without a reader returns ENXIO while O_RDONLY|O_NONBLOCK without a writer succeeds and the following read returns 0 as EOF, zero torn windows across many rounds within the 4096-byte PIPE_BUF versus probabilistic tearing at 8192 (interleaving masked by the reader's release granularity); POSIX mq's entities landing in /dev/mqueue, the boundary-preserving contrast of three 10/50/200-byte messages received in prio 3/2/1 order against pipe's blob-merging, and mq_notify's one-shot registration; POSIX semaphores' two-slot three-process wait-and-release, sem_unlink isomorphic with shm_unlink; SCM_RIGHTS' duplicated-reference semantics for passing fds (the receiver gets its own new number, fdinfo proves pos is shared, and reads continue after the sender closes); closing with a six-channel suitability matrix, a decision order, and a bandwidth comparison (shm 7304 ≫ pipe 2349 ≥ mq 1245 MiB/s, quoting the memory article's baselines without re-measuring)."
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 47
prerequisites:
  - "共享内存:shm_open 与映射"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
related:
  - "文件锁:flock 与 fcntl 记录锁"
  - "错误处理范式:从 errno 到 expected"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/process/03-ipc.md
  source_hash: 6f50a2ddccc0974dd5dfdb02127698b39d1fe68b5d78d610a49775cf828dd6c2
  translated_at: '2026-11-22T16:30:00+00:00'
  engine: glm-5.3
  token_count: 24961
---

# IPC: Pipes, FIFOs, and POSIX Message Queues

The first two articles of the process chapter kept us wrestling with a single process: how to create it ([fork and exec](01-fork-exec.md)), how to have it swap in another program and keep running, and how to move it into the background as a daemon ([the daemon article](02-daemon.md)). This article changes opponents: two processes — how do we move a batch of data from one side to the other? Address spaces are mutually isolated; process A's pointer means nothing inside process B, so there must be a place in the middle that both sides can reach, and all such places are in the kernel's hands. Do not forget the thread the first article left behind, either: a child forked from the parent inherits fds verbatim, sharing even the offsets, but inheritance recognizes only kinship, and what it hands over is merely access — not one byte of our data has moved. To actually deliver bytes to the other side, we must walk the channels the kernel provides, one by one.

The kernel holds plenty of material. The pipe sends a byte stream into a ring buffer in the kernel; we put bytes in at the write end and take them out at the read end. The FIFO hangs a path name on a pipe, so unrelated processes can find the same one by name. The POSIX message queue delivers fixed-length messages, each carrying a prio value. The POSIX semaphore delivers nothing but slots — permits for how many processes may enter at once. And one more special route: even an fd itself can be packed into a socket's ancillary data and handed across. As for shared memory, [the shared memory article](../memory/03-shm.md) (we call it Lmem03 by directory order) has already walked it end to end — from mapping the same physical pages, to the ring queue that moved a million messages with zero loss, to race measurements and throughput comparisons — all of that is its business. So this article does not rerun those experiments; bandwidth numbers are quoted directly. The task at hand is to complete into one full table the pipes that Lmem03 never detailed, and the fd-passing it covered in only one paragraph.

The experimental environment is stated once in full, and the later numbers are checked against it. All experiments come from my WSL2: the kernel is 6.18.33.2-microsoft-standard-WSL2, the CPU an AMD Ryzen 7 9700X (16 logical cores from WSL2's viewpoint), the compiler g++ 16.2.1, everything built uniformly with `g++ -std=c++20 -Wall -Wextra -O2`, with `-pthread` added for the message-queue and semaphore experiments; glibc is 2.44 and strace 7.2. The code and all raw output are archived in `code/volumn_codes/vol8/systems-programming/linux/process/03-ipc/` in the repository, with rerun commands in the README. Outputs pasted in the text are excerpts, elisions marked with `...`, and the archived originals are always authoritative. Experiments are numbered E1 through E7, following the archive directories; sub-items within a directory carry a letter (E4a, for example). The shared utilities `errno_code` and `sys_call` follow the definitions in [the RAII article](../../thinking/01-raii-paradigm.md) and [the error handling article](../../thinking/02-error-paradigm.md); the experiments collect them into common/ipc_util.hpp and keep using them. One measurement caveat must be stated up front: on WSL2, bandwidth can fluctuate by ±40% from round to round, so the throughput numbers below are reported by order of magnitude only, while the ordering (shm ≫ pipe ≥ mq) was stable across all rounds — please carry this sentence along when you quote.

## Pipes: A Byte Stream in the Kernel's Ring Buffer

`pipe()` hands back two fds at once: pfd[0] read-only, pfd[1] write-only, with the direction of flow fixed for good — in at the write end, out at the read end; for bidirectional use we simply open two. The real storage sits in the kernel, a ring buffer: write copies bytes in from user space, read copies them back, and one round trip costs two system calls plus two copies. When Lmem03 moved 1 MiB, pipe was only a third as fast as shm — the price difference is exactly those two copies. So how big is the buffer? man 7 pipe says 16 pages by default since Linux 2.6.11. No need to memorize it; one question to fcntl answers it, and along the way we tried resizing it too (E1a):

```text
$ ./e1_capacity
新建管道:fd=3(读)、4(写),F_GETPIPE_SZ = 65536 字节(16 页 × 4096)
F_SETPIPE_SZ(4096) 返回 4096 → 再探 F_GETPIPE_SZ = 4096 字节
F_SETPIPE_SZ(5000) 返回 8192 → 实际容量 8192 字节(向上取整到 2 页)
F_SETPIPE_SZ(262144) 返回 262144 → 实际容量 262144 字节(64 页)
/proc/sys/fs/pipe-max-size = 1048576(非特权 F_SETPIPE_SZ 的上限)
F_SETPIPE_SZ(2097152 超上限) 返回 -1,errno=1 (Operation not permitted)——想要更大得有 CAP_SYS_RESOURCE
```

Three spots in the output deserve picking out. The default 65536 is exactly 16 pages — the man page did not lie to us. When resizing, the kernel rounds to whole pages: request 5000 and you receive 8192; it only deals in full pages. Raising the cap has a ceiling too: the referee is `/proc/sys/fs/pipe-max-size`, 1048576 on my machine. An unprivileged process that crosses the line eats EPERM (errno 1); for anything larger, you must speak with the CAP_SYS_RESOURCE capability bit.

Why does the capacity deserve our attention? Because once it fills, write goes to sleep — that is the flow control the pipe ships with, and we photographed it with a timeline (E1b). The writer's chunks are 4096 bytes, 17 in a row; the reader holds still for two full seconds:

```cpp
// e1_write_full.cpp(节选):写者每写一块就打一行时间戳,fork 出的子进程要手动 fflush
constexpr int kChunk = 4096;
constexpr int kChunks = 17;  // 17 × 4096 = 69632,前 16 块正好填满默认 64 KiB
for (int i = 1; i <= kChunks; ++i) {
    std::printf("[%7.1f ms] 写者:已累计 %5d 字节,即将写第 %d 块\n",
                ms_since(t0), (i - 1) * kChunk, i);
    std::fflush(stdout);
    if (write(pfd[1], buf, kChunk) != kChunk) { perror("child write"); _exit(1); }
    std::printf("[%7.1f ms] 写者:第 %d 块写完,累计 %5d 字节%s\n",
                ms_since(t0), i, i * kChunk,
                i == 16 ? "(== 容量,恰好填满)" : "");
}
```

```text
$ ./e1_write_full
[    0.2 ms] 写者:第 16 块写完,累计 65536 字节(== 容量,恰好填满)
[    0.2 ms] 写者:已累计 65536 字节,即将写第 17 块
[ 2000.3 ms] 写者:第 17 块写完,累计 69632 字节
[ 2000.3 ms] 写者:17 块全部写完,关写端退出
管道容量 F_GETPIPE_SZ = 65536 字节;写者每块写 4096 字节,共 17 块,读者按兵不动 2 秒
[ 2000.2 ms] 读者:读走 4096 字节——腾出一个块的空间
[ 2000.3 ms] 读者:EOF,共读 69632 字节(= 17 × 4096,一块不差)
结论:读者不读时,写者写到 65536 字节(默认容量)后,下一次 write 阻塞约 2000 ms,直到读者腾出空间
```

Match the timestamps line by line, and all the action sits on chunk 17. The first 16 chunks each finished writing in 0.2 ms, accumulating to 65536 — the capacity filled exactly. Chunk 17's write departed at 0.2 ms and returned at 2000.3 ms; for those two full seconds it slept inside the kernel's buffer. None of us woke it — it was the parent reading away one chunk and freeing one slot that roused it to finish the job. In the end the reader held 69632 bytes, exactly the product of 17×4096; our audit found not one byte missing. In write-fast-read-slow scenarios, a pause like this looks like a failure but is really the kernel applying backpressure for us; adding another throttling layer in your application is most likely duplicated labor.

Each end of the buffer also has a boundary event, likelier to bite you than the capacity; we continue (E1c). The read side's waiting has two endings. While the pipe is empty but the writer lives, read waited until 1000.3 ms to receive 4 bytes. When the write end's fds are all closed, read returns 0 — we call it end-of-file (EOF), semantically the same thing as ReadFile returning 0 at end-of-file in [Win32 file I/O](../../windows/file-io/01-win32-file-io.md) (the 1st article on the Windows side, which we call W01). The write side's event is fiercer: after the read end is fully closed, what write triggers is SIGPIPE. The experiment installed a signal handler, so write could come back alive to deliver its errno:

```text
$ ./e1_sigpipe_eof
[1000.3 ms] read#1 返回 4:「ping」——管道空但写者活着时,read 干等了约 1000 ms
[1300.5 ms] read#2 返回 0——写端全关,这是 EOF(与 Windows 篇 W01 的 ReadFile 到文件尾返 0 同一语义)

== 第二幕:读端全关 → SIGPIPE + EPIPE ==
write(写端) 返回 -1,errno=32 (Broken pipe);信号处理器被调过:是(SIGPIPE 已被我们捕获)
SIGPIPE 设为 SIG_IGN 再来一次:write 返回 -1,errno=32 (Broken pipe)——忽略信号后错误只剩 errno 这一条通道
```

Please file this disposal as an engineering habit: SIGPIPE's default disposition is to terminate the process, and an unguarded program mostly just dies — without even a chance to glance at errno. Set SIGPIPE to SIG_IGN (or block it), and write instead returns -1 with EPIPE (errno 32); the error has entered a channel you can inspect. The classic sudden death in network programs is, nine times out of ten, an unguarded SIGPIPE. The orphan experiment in the first article truly took this hit: in the first version of the apparatus, the read ends of the two report pipes were closed too early in the observer's hands; the grandchild's write took a SIGPIPE and died silently midway, and the adoption line of the output vanished into thin air. The first article promised to summon it back as evidence — now we have seen its mechanism in full. As for how signals are actually delivered and what a handler may do, that is the proper subject of the next article, [the signal article](04-signal-basic.md); here we record only the two pipe-side facts: by default it kills the process, and once ignored, the only remaining channel for the error is errno.

## pipe2, dup2, and popen: Wiring the Pipe to a Child Process

Real pipe usage almost always crosses an exec: the shell's `a | b` connects a's stdout into the pipe and b's stdin out of it, with both ends exec'ing into new programs. In [POSIX file I/O](../file-io/01-posix-file-io.md) (which we call L01) we measured whether fds survive exec: an fd without FD_CLOEXEC crosses verbatim, while one carrying the flag is closed for you by the kernel at the exec. A pipe brings two fds, and the flag is per-fd. E2b lined up three postures on the same bench, letting the child exec into `ls -l /proc/self/fd` to testify for itself:

```text
$ ./e2_cloexec
== 姿势一:裸 pipe(fd=3 读、4 写) ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> pipe:[353914]
l-wx------ 1 charliechen charliechen 64 Oct  4 11:06 4 -> pipe:[353914]
== 姿势二:pipe2(O_CLOEXEC) ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> /proc/23680/fd
== 姿势三:裸 pipe + fcntl 只给写端(fd=4)补 FD_CLOEXEC ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> pipe:[353916]
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 4 -> /proc/23681/fd
```

The three postures line up neatly. In the bare pipe, both 3 and 4 survive, still pointing at the same inode (`pipe:[353914]`) — exec cannot stop them. Posture two uses `pipe2(fd, O_CLOEXEC)`, hanging the flag on at creation; after the exec the pipe's fds are all gone, and the 3 remaining in the table is the directory ls itself opened. Posture three is the most common look in old code: no flag at creation, then a make-up fcntl afterwards on the write end, so after the exec only the read end survives. The flag lives on the fd's table entry, configured fd by fd — the same lesson L01 taught about FD_CLOEXEC; it is just that the pipe's pair of fds appear together, which makes it even easier to see that it takes effect per-fd.

The next step after wiring the fds is swapping the standard streams across — that is dup2's job. E2c is a complete little project: after the fork, the child performs `dup2(pipefd[1], STDOUT_FILENO)` before the exec, then execs into `/bin/sh -c`, so everything this shell writes to stdout flows into the pipe, while stderr drops onto the terminal untouched:

```text
$ ./e2_dup2_exec
stderr-line:我还是落终端
父进程:等子进程(pid=23683)的 stdout……(stderr 那行你应该已经直接在下面看到了)
父进程:从管道拿到 49 字节的子进程 stdout:
stdout-line:我走的是被 dup2 过来的管道
waitpid:exit=0——这就是 popen 内部那套 fork+dup2+exec 的手工完整版
```

What `dup2(oldfd, newfd)` does is repoint the number newfd at the same place oldfd points — precise down to the individual fd — which is how we send stdout into the pipe while stderr stays on screen, each going its own way. With the handmade version seen, the standard library's popen is easy to recognize (E2a): popen does the pipe, fork, dup2, and exec as one full service, pclose does the harvesting for us, and even the child's exit code passes through verbatim:

```text
$ ./e2_popen
popen("exit 7") → pclose = 1792,WEXITSTATUS = 7:子进程退出状态原样透传
```

1792 is exactly 7 shifted left by 8 bits — waitpid's status encoding — and we verified that popen did not touch one byte of it. The real fun lies in the strace; we recorded the whole system-call sequence (in the E2a attachment):

```text
$ strace -f -e trace=pipe,pipe2,dup,dup2,dup3,close,execve,clone,clone3,wait4 -o e2_popen_strace.txt ./e2_popen
23686 pipe2([3, 4], O_CLOEXEC)          = 0
23686 clone3({flags=CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND, exit_signal=SIGCHLD, stack=0x77470d8e6000, stack_size=0x9000}, 88 <unfinished ...>
23687 dup2(4, 1)                        = 1
23687 execve("/bin/sh", ["sh", "-c", "--", "echo popen-hello-from-shell; una"...], 0x7ffe29262838 /* 69 vars */ <unfinished ...>
```

The textbook account says fork+dup2+exec. Look closely at the trace above: every step is in its right place, yet the protagonist has been swapped. The process-creation step is clone3 carrying CLONE_VM and CLONE_VFORK — the signature of the posix_spawn lineage: the child temporarily borrows the parent's address space, returns it the moment the exec completes, and the entire page-table copy is spared. Not one dup2 or execve call is missing; only the worker's path has been switched away from fork.

> When did the switch happen? We found the signature: glibc 2.29's NEWS states that popen and system no longer run pthread_atfork handlers (glibc Bugzilla 17490) — precisely the visible consequence of the implementation moving to posix_spawn. Since then, fork never appears in strace again; early on it was clone(CLONE_VM|CLONE_VFORK) all the way, and newer glibc goes through clone3 when the kernel supports it — what I see on 2.44 is that clone3 line above. For application writers the semantics are equivalent, but when you troubleshoot with strace, update your expectations: do not keep waiting for a fork that will never come.

## FIFO: Meeting by Path Name

A pipe's two ends are inherited through fork; two unrelated processes lack that relationship. The second route is to give the pipe a name: mkfifo plants a special file in the directory, its type bit is p, and anyone of us can open it by path. E3a had a harness script bring the two processes up separately (the two sides share no kinship; the harness is the one calling itself orchestrator in the archived output) and walked through the whole affair:

```text
$ ./e3_fifo_meet
mkfifo("/home/charliechen/lp03_scratch/e3_meet.fifo", 0666) 完成,st_mode=010644 → S_ISFIFO=true(类型位是 p)
  prw-r--r-- 1 charliechen charliechen 0 Oct  4 11:06 /home/charliechen/lp03_scratch/e3_meet.fifo
[读端 pid=23563] open(O_RDONLY) ……(此刻还没有任何写者)
[读端 pid=23563] open 返回 fd=3,干等了 801.1 ms——写者出现了
[写端 pid=23564] 写 73 字节后关闭,退出
[读端 pid=23563] EOF,共 73 字节,开头是:「hello,我是跟读端毫无亲缘的写者,咱们只认识这条路径名」
orchestrator:两个 exec 出来的进程(pid 23563 / 23564)完成了这次相会,总耗时 802.4 ms;unlink 收尾
```

Two points in the output deserve attention. The first concerns permissions: the requested mode was 0666, yet what landed in the directory is 0644 — trimmed by two bits of umask 0022, exactly what shm_open suffered in Lmem03; to grant full permissions you must push the umask back before creating. The other point is that open itself waits too: when the read end arrives early, its open(O_RDONLY) waits in place, and 801.1 ms later the write end appears and the two sides are released together. The write end arriving early waits just the same — the other half of the direction, measured in E3b as each side waiting about 500.1 ms. The kernel built the rendezvous into the open call itself, so the two processes need no extra handshake.

In the same E3b we also measured open's semantics under O_NONBLOCK, which hides a counterintuitive trap:

```text
$ ./e3_open_semantics
== A. 读端先开:open(O_RDONLY) 阻塞直到写者出现 ==
[ 500.3 ms] 父进程:睡满 500 ms 后才 open(O_WRONLY) fd=3——对面立刻放行
[ 500.3 ms] 子进程:O_RDONLY 返回 fd=3(等了 500.1 ms)

== B. 写端先开:open(O_WRONLY) 阻塞直到读者出现 ==
[1001.1 ms] 父进程:睡满 500 ms 后才 open(O_RDONLY) fd=3——双向都是 open 在等对面
[1001.1 ms] 子进程:O_WRONLY 返回 fd=3(等了 500.1 ms)

== C. O_NONBLOCK:写侧 ENXIO / 读侧「成功但 read=0」的 EOF 陷阱 ==
open(O_WRONLY|O_NONBLOCK)(没有读者):返回 -1,errno=6 (No such device or address)
open(O_RDONLY|O_NONBLOCK)(没有写者):返回 3——注意,成功了!没有像写侧那样报错
紧接着 read():返回 0——不是 EAGAIN,是 0(EOF 语义)。非阻塞读 FIFO 的经典陷阱:...
```

The two lines of section C must be read together. The write side behaves crisply: with no reader present, open returns ENXIO (errno 6) outright. The read side, however, succeeds — and the read that immediately follows returns 0. The trouble lies in the semantic fork: "no writer has ever appeared" and "a writer came and all closed" both present as read returning 0, and our code cannot tell them apart. Worse, on interfaces like poll, readable is forever true, and the event loop spins its CPU idle. The common engineering fix is to open the read end with O_RDWR, occupying a writer's slot ourselves so EOF never arrives; or simply treat 0 as an ordinary event in the state machine. Section D of the archive has one more: after mkfifo with 0400, the owner's own write-mode open also ate EACCES (errno 13) — a FIFO is a real entity in the directory, its permission bits take part in checks as usual, not for decoration. One more piece of good news from the measurements: a FIFO's F_GETPIPE_SZ is also 65536 — it shares the same kernel buffer machinery with the anonymous pipe, merely gaining a directory entry as a name.

### PIPE_BUF: The Atomicity Line for Multiple Writers

When two writers pour records into one channel simultaneously, how does the reader know a message was not half-written by someone else? The line that measures this is a constant named PIPE_BUF: it lives in linux/limits.h, and Linux sets it at 4096 bytes. The guarantee of man 7 pipe comes in two halves: for a single write of fewer bytes than PIPE_BUF, the kernel guarantees it will not interleave with another writer. For writes larger than PIPE_BUF, the kernel's promise shrinks to "may interleave"; the exactly-equal-to-4096 case is left unwritten by the man page, and our measurements filled it in. E3c stages exactly this scene: two concurrent writers plus one reader, two record sizes, each tested in blocking and non-blocking versions; the two ==4096 groups we ran for multiple rounds, and the torn windows were all zero:

```text
$ ./e3_pipebuf_atomic
PIPE_BUF = 4096(linux/limits.h):≤ 它的写入内核保证不与别的写者交错,> 它只是「可以」交错

== 记录 4096(== PIPE_BUF) 阻塞写者,两个写者各 64 条 ==
  读者:完整窗口 128(期望 128),其中撕裂窗口 0

== 记录 4096(== PIPE_BUF) O_NONBLOCK 写者,两个写者各 64 条 ==
  读者:完整窗口 128(期望 128),其中撕裂窗口 0

== 记录 8192(> PIPE_BUF) 阻塞写者,两个写者各 128 条 ==
  读者:完整窗口 256(期望 256),其中撕裂窗口 33

== 记录 8192(> PIPE_BUF) O_NONBLOCK 写者(部分写循环),两个写者各 128 条 ==
  读者:完整窗口 256(期望 256),其中撕裂窗口 0
```

In the two 4096 groups the torn windows are zero, blocking and non-blocking alike — the kernel treats a single write not exceeding PIPE_BUF as an indivisible unit, and the guarantee held. The two 8192 groups are subtler: the blocking version tore 33 windows, while the non-blocking version tore 0 in this particular round; across the archived reruns, the window count swung wildly round to round — rounds with 0 and rounds nearing a hundred — it is probabilistic. The mechanism hides in pipe_write in the kernel's fs/pipe.c: the copy proceeds page by page; when full, the writer releases the lock and sleeps, and on waking it fills whatever slots exist — another writer's pages then slip into the middle of a record. Do not celebrate the non-blocking round's zero tearing too early: the interleaving was merely masked by the release granularity. When the reader frees space in whole-record (8192) units, the awakened writer often devours all free slots in one gulp, and the interleaving stays invisible. A note in the archive records another round: after dropping read's granularity to 4096 and releasing page by page, the interleaving showed its shape at once. So the safety line for multi-writer scenarios is a single one: keep each message within PIPE_BUF; anything larger demands your own application-level segmentation and synchronization — or simply switch channels.

## POSIX Message Queues: Boundaries and prio Both Preserved

Byte-stream boundaries are your own to build — the shared temperament of pipe and FIFO: after the sender writes three times, one read on the receiving side may fetch one and a half messages, or a three-message blob. The POSIX message queue (mq for short below) takes this chore into the kernel: one send sends one message, one receive receives one message, and its length is the length at send time. mq_open's naming rules are the same design as shm_open's: the name begins with one slash, the middle admits no second slash, and the total length stays within NAME_MAX (255). Where does the entity land? We look with ls (E4a):

```text
$ ./e4_mq_basics
mq_open("/lp03_e4") → mqd=3;命名对象落在哪?看目录:
  [ls -l /dev/mqueue/]
-rw------- 1 charliechen charliechen 80 Oct  4 11:06 lp03_e4
mq_getattr:mq_maxmsg=10 mq_msgsize=256 mq_curmsgs=0 mq_flags=0
```

The namespace we see is the mount point of the mqueue filesystem, `/dev/mqueue`, isomorphic to shm's `/dev/shm` — visible to ls, and removable by rm. One small spot, so you do not misread it: the directory entry's size column reads 80, a fixed value the kernel hard-codes in the mqueue filesystem (the FILENT_SIZE constant in the kernel source); it does not represent message bytes. mq_attr is mq_open's fourth parameter, an attribute structure whose four fields each mind their own plot: mq_maxmsg governs the queue depth (how many messages may pile up at once), mq_msgsize the per-message cap, mq_curmsgs reports the current count, and mq_flags hides O_NONBLOCK among other things. The unprivileged ceiling is surprisingly low: the msg_max=10 and msgsize_max=8192 we read from `/proc/sys/fs/mqueue` are exactly the defaults man 7 mq_overview documents. Want a deeper queue without CAP_SYS_RESOURCE, and mq_open flatly refuses the creation request.

For the live test of boundaries and prio, one small piece of code suffices (prio is an unsigned integer, ranging to 0..32767 on Linux):

```cpp
// e4_mq_basics.cpp(节选,send_msg 的包装体略去):按 prio 1/3/2 发三条不同长度的消息
// send_msg 的参数依次是:prio、字节数、填充字符、消息 id
send_msg(mq, 1, 10, 'x', "M1");
send_msg(mq, 3, 50, 'y', "M2");
send_msg(mq, 2, 200, 'z', "M3");
char buf[512];             // 注意:mq_receive 的缓冲区必须 ≥ 队列的 mq_msgsize,否则 EINVAL
unsigned prio = 0;
ssize_t n = mq_receive(mq, buf, sizeof buf, &prio);
```

```text
按 prio 1 → 3 → 2 的顺序发三条不同长度的消息:
  发送 prio=1 长度=10 字节(id=M1,填充字符 'x')
  发送 prio=3 长度=50 字节(id=M2,填充字符 'y')
  发送 prio=2 长度=200 字节(id=M3,填充字符 'z')
  mq_curmsgs=3(排队 3 条)

连收三次(mq_receive 带 prio 出参,返回值就是本条字节数):
  第1次收到 prio=3 长度=50 字节(id=M2)——长度与发送时一字不差,边界还在,内容校验通过
  第2次收到 prio=2 长度=200 字节(id=M3)——长度与发送时一字不差,边界还在,内容校验通过
  第3次收到 prio=1 长度=10 字节(id=M1)——长度与发送时一字不差,边界还在,内容校验通过

同 prio=5 连发两条,验证同优先级内按发送序:
  先收到(8 字节):「first-in」
  后收到(9 字节):「second-in」——同优先级内 FIFO;「跳队」只发生在跨优先级之间
```

The sending order was 1, 3, 2; the receiving order came out 3, 2, 1: the larger prio leaves the queue first, and queue-jumping happens only between messages of different prio. Two messages of equal prio keep their order in and out. The length column is the other key evidence: whatever you send is exactly what you get back — a message boundary faithful to the byte, something a byte stream cannot give you. Then we sent the same batch through a pipe (E4b); the contrast looks like this:

```text
$ ./e4_mq_vs_pipe
[mq] 连收三次,每次要 256 字节的空间,得到的却是发送时的长度:
  第1条:10 字节 → [1111111111]
  第2条:50 字节 → [22222222222222222222222222222222222222222222222222]
  第3条:200 字节 → [33333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333]
[pipe] 读者每次 read 要 64 字节(谁也没规定消息边界,只能按字节收):
  第1次 read:64 字节 → [1111111111222222222222222222222222222222222222222222222222223333]
  第2次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第3次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第4次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第5次 read:4 字节 → [3333]
```

The pipe's first read fused message 1, message 2, and the head of message 3 into one blob: nobody defined a boundary, so of course it delivers by the byte. Use a pipe to carry messages and the unpacking work all lands on the user side: fixed length, length prefix, or delimiter — pick one and write it yourself — plus your own handling of sticky and half packets. mq throws the boundary in for free; the price shows up in the throughput section. One usage note before receiving: mq_receive's buffer capacity must be at least the queue's mq_msgsize, otherwise it EINVALs outright — a comment the experiment code added only after taking that hit. It is the reverse of read's give-what-you-ask-for: declare the capacity too small and it refuses without negotiation.

### mq_notify: Register Once, Ring Once

mq has one more talent pipe lacks: at the moment the queue goes from empty to non-empty, the kernel can notify you on its own initiative, sparing you even the polling. Registration goes through mq_notify; E4c took the signal route:

```text
$ ./e4_mq_notify
mq_notify 已注册(SIGEV_SIGNAL/SIGUSR1,sigev_value=0xC0DE),fork 子进程 500 ms 后发消息……
信号到了:si_code=-3(SI_MESGQ,消息队列专属来源),sigev_value 带回 0xc0de——注册时塞的私货原样返回
收到本条:wake-1(mq_curmsgs 消费前=1)
等子进程发第二条(不重新注册)……
第二条到货了吗?mq_curmsgs=1;信号又来过吗:没有——mq_notify 的注册是一次性的,消费即失效
补收第二条:wake-2;要继续被通知,得再调一次 mq_notify(或在收货线程里重新武装)
```

Two things to see. When the signal arrives, siginfo's si_code reports -3, which is SI_MESGQ — a marker meaning the message-queue-specific source; you will not find it in the signal articles, and the man pages plus our archive are its entire provenance. The 0xC0DE stuffed into sigev_value at registration also came back with the signal verbatim, so you can tell which queue a notification came from (the common si_code sources are covered in both halves of the signal articles). The other point is the crux: registration is one-shot. The man page of mq_notify(3) says verbatim, notification occurs once. After the first message consumed the registration, the second message clearly arrived (mq_curmsgs=1 attests) — but the signal never came again. To stay notified, you must re-register with mq_notify once per reception; the customary practice is to put it at the head of the receiving path, re-arming after draining the queue — the same school of thought as the self-pipe the signal article will cover (the signal handler writes one byte into a pipe, and the main loop receives the rest from it).

### Throughput: mq versus pipe and shm Under the Same Measurement

What is the free stuff worth? E4d reuses Lmem03's measurement setup: a 1 MiB payload, verified chunk by chunk, median of 3 rounds.

```text
$ bash run_e4.sh
== E4d 同一 1 MiB 走 POSIX 消息队列(maxmsg=10,满仓阻塞),各 3 轮 ==
== 参照 Lmem03 E5 同机数字:shm SPSC 环形中位 7304 MiB/s、pipe 1024B 块中位 2349 MiB/s ==

--- 1024 B × 1024 条(与 Lmem03 的分块完全一致) ---
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.825 ms →   1213 MiB/s
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.803 ms →   1245 MiB/s
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.700 ms →   1429 MiB/s

--- 8192 B × 128 条(贴着 msgsize_max=8192 的上限,减少系统调用次数) ---
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.279 ms →   3579 MiB/s
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.315 ms →   3179 MiB/s
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.401 ms →   2491 MiB/s

--- 同尺寸对照:pipe 8192 B × 128 块(与 Lmem03 e5 同构,只换块大小) ---
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.183 ms →   5459 MiB/s
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.210 ms →   4753 MiB/s
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.161 ms →   6226 MiB/s
```

Lay the medians out. At the 1024-byte setting, shm is 7304 and pipe 2349 (both quoted from Lmem03; we do not re-measure), while mq's median is 1245 — barely more than half of pipe. At the 8192-byte setting, mq's 3179 against pipe's 4753 narrows the gap to within half. Two rules read out: at equal size, mq is consistently slower, because every message takes an extra sorting pass through the kernel's prio queue while the system-call count drops by not one. The larger the messages, the smaller the gap, because each message amortizes fewer system calls and the fixed overhead thins out. Read the opening caveat once more: on WSL2 the round-to-round swing is ±40%, these absolute values are order-of-magnitude only, and the ordering shm ≫ pipe ≥ mq was stable across all rounds.

## POSIX Semaphores: Issuing Slots for a Channel

A semaphore carries no data; it maintains nothing but a count, yet channel work is never without it: pipe depth, queue slots, how many processes may enter at once — all its jurisdiction. The named version, sem_open, crosses processes by name just like mq; the anonymous version, sem_init with pshared=1, rides along the shared-memory mapping. E5a is a slot experiment: we set the initial value to 2 — a two-slot configuration — and three exec-spawned workers enter on a 150 ms ladder:

```text
$ ./e5_sem_slots
命名信号量 /lp03_sem 已建:初值=2(两个名额)。三个 exec 出来的 worker 进场:
[worker0 pid=23751 @1854010.6] 尝试 sem_wait(此刻 sem_getvalue=2)
[worker0 pid=23751 @1854010.6] 拿到名额!sem_wait 等了    0.0 ms(剩余名额 sem_getvalue=1)
[worker1 pid=23752 @1854160.8] 尝试 sem_wait(此刻 sem_getvalue=1)
[worker1 pid=23752 @1854160.8] 拿到名额!sem_wait 等了    0.0 ms(剩余名额 sem_getvalue=0)
[worker2 pid=23753 @1854311.3] 尝试 sem_wait(此刻 sem_getvalue=0)
[worker0 pid=23751 @1855010.8] sem_post 放行(名额回到 1)
[worker2 pid=23753 @1855010.8] 拿到名额!sem_wait 等了  699.4 ms(剩余名额 sem_getvalue=0)
[worker1 pid=23752 @1855161.0] sem_post 放行(名额回到 1)
[worker2 pid=23753 @1856010.9] sem_post 放行(名额回到 2)
```

Read the timeline once through. When worker0 and worker1 arrived, slots were still open, and their sem_wait entered with zero waiting. When worker2 stepped in on the third rung, the count had hit bottom; it waited in sem_wait for a single stretch of 699.4 ms, until worker0 finished its work and its sem_post released a slot, and only then did it take the baton at once. After everything, sem_getvalue returned to 2 — not one slot lost. Set it against the mutex in Lmem03: a mutex is the special case of initial value 1, admitting only one at a time. A counting semaphore governs concurrency: N slots admit N processes; rate limiting and connection-pool caps are all governed by this count.

Three miscellaneous items from E5b, noted in passing. First, waiting with a timeout: give `sem_timedwait` 300 ms and at the deadline it returns -1 with ETIMEDOUT (errno 110) — measured waiting 300.1 ms, so you need not cobble together alarm plus signals yourself. Second, the anonymous version across processes: set sem_init's pshared to 1, place it in a MAP_SHARED|MAP_ANONYMOUS mapping, and after a fork the parent and child share the same counter — measured as the parent waiting 300.1 ms for the child's post, the same trick Lmem03's race experiment used. Third, the unlink lifecycle, cast from the same mold as shm_unlink (the archive holds one more sem_getvalue observation we skip; the 4 at the block's head is the archive's section number):

```text
== 4. unlink 生命周期:旧句柄照用,新名字从头来(与 shm_unlink 同构) ==
unlink 前旧句柄值=1;执行 sem_unlink……
旧句柄 post 后=2;新句柄(初值 7)=7——两个实体互不相干,名字已经归新实体
```

sem_unlink deletes the name. The old handle still grips the entity's reference — its post still works, and the count still rises. Once the name is vacant, a sem_open under the same name creates a new entity with initial value 7, and the two have nothing to do with each other. Lmem03 measured exactly this behavior on shm_unlink; we carry those measurements over as-is and need not prove it again.

## SCM_RIGHTS: Passing the fd Itself

Where Lmem03 discussed the two routes at mid-article, it yielded one paragraph: an fd itself can serve as a token — unix-domain sendmsg with SCM_RIGHTS can hand an already-open fd directly to another process. That side yielded only a paragraph; here we tell it in full. SCM_RIGHTS is a type of ancillary data (control message): sendmsg's primary payload carries bytes as usual, the ancillary buffer separately carries the fd, and at recvmsg the kernel copies this fd into the receiver's fd table. man 7 unix puts it bluntly: it is equivalent to our dup'ing the fd into another process's fd table. The number itself carries no cross-process meaning; what is passed is a reference to the open file description. When to use it? When what you are really passing is access, not a batch of bytes: an opened device, a connection, a stretch of memory you would rather not copy again (memfd_create's anonymous memory pairs with it perfectly). Reading the content out and writing it back would be pure waste. The sender's core code is only these lines:

```cpp
// e6_scm_rights.cpp(节选):sendmsg 的附属数据里装 fd,主载荷只带一个字节
void send_fd(int sock, int fd)
{
    char flag = 'F';
    struct iovec iov { &flag, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    sys_call("sendmsg", sendmsg, sock, &msg, 0);
}
```

The chain of evidence gathered on the receiver's side runs through E6's output in one breath; the witness is the pos field of `/proc/self/fdinfo` (the file offset recorded by the kernel):

```text
$ ./e6_scm_rights
== 路径三:SCM_RIGHTS 传递(socketpair + sendmsg 附属数据) ==
[发送方] 读 16 字节:「LINE-0-012345678」→ fdinfo pos=16
[发送方] SCM_RIGHTS 已发(fd=3 装进附属数据),等接收方确认共享 pos……
[接收方 pid=23799] recvmsg 拿到 fd=4(发送方那边它叫 fd=3,编号是各自的)
[接收方] 它的 pos=16——和发送方共享偏移!接着读:
[接收方] 读 16 字节:「9ab
LINE-1-01234」→ fdinfo pos=32
[发送方] close(fd=3)——原始 fd 关了
[接收方] 收到信号:发送方已 close 原始 fd。继续读这个 fd:
[接收方] 读 24 字节:「56789ab
LINE-2-012345678」→ fdinfo pos=56
[接收方] 照样读得到——fd 表项是复制出来的新引用,struct file 的引用计数没归零,谁都没「移交」

== 获得一个 fd 的三条路径 ==
| 路径         | fd 编号        | 打开文件描述      | pos     | 发送方 close 后 |
|--------------|----------------|-------------------|---------|-----------------|
| open()       | 本进程新分配    | 全新的一份         | 从 0 起  | ——              |
| fork 继承    | 编号原样复制    | 与父进程同一份     | 共享     | 子进程照用       |
| SCM_RIGHTS   | 接收方新分配    | 与发送方同一份     | 共享     | 接收方照用       |
```

Take the three pieces of evidence one at a time. The same fd reached both sides, numbered fd=3 in the sender's hand and fd=4 in the receiver's — each side's fd table runs its own numbering, neither borrowing the other's. The pos is the most direct evidence of sharing: the sender stopped at 16, and the receiver's starting point on taking over was also the sender's departing 16, reading on to 32 — only a single shared open file description behaves this way, the same temperament as an fd inherited through fork. The most persuasive is the third piece: after the sender closed the original fd, the receiver kept reading from 32 to 56; the struct file's reference count in the kernel had not reached zero, and the entity was alive and well. So the semantics are reference duplication: the receiver lives off the kernel's reference counting, and "transfer of ownership" simply does not exist — the sender closes its own fd, the receiver uses someone else's fd, and neither disturbs the other's days. The table at the end gathers the three paths for obtaining an fd in one place: open creates a brand-new one; fork and SCM_RIGHTS both yield references to an existing description; and SCM_RIGHTS requires no kinship — exactly the talent it holds over fork. Paired with memfd_create's anonymous memory, it needs not even a name. What Lmem03 said — the fd arriving means the entity has arrived — has its chain of evidence closed here.

## Choosing Among the Six Channels

With E1 through E6 walked, we lay the channels in hand onto one table, and the closing E7 is the selection itself. The program in one sentence: the first question to ask is whether the data should pass through the kernel. The second is the relationship between the two sides.

| Channel | Relationship between sides | Data shape | Synchronization and boundaries | Direction |
|---|---|---|---|---|
| pipe (E1/E2) | Kinship only; fds inherited via fork | Byte stream, 64 KiB by default, adjustable via F_SETPIPE_SZ | Flow control built in: blocks when full (measured exactly 65536), waits when empty, EOF and SIGPIPE | One-way; open two for bidirectional |
| FIFO (E3) | Unrelated processes, meeting by path name | Byte stream, same kernel buffering as pipe (measured the same 64 KiB) | Same as pipe; with multiple writers, atomic only for single writes ≤ PIPE_BUF | One-way |
| POSIX mq (E4) | Unrelated processes, by /dev/mqueue name | Fixed-length messages, boundaries and prio preserved | Full/empty blocking built in; mq_notify pushes a signal (one-shot) | One-way |
| POSIX sem (E5) | Named across processes, or anonymous riding shared memory | Carries no data, only a count | Is itself a synchronization primitive | Not applicable |
| shm (Lmem03) | Unrelated processes, by /dev/shm name | Raw memory, structure of your own design | Synchronization entirely your own (Lmem03 has the race evidence) | Bidirectional |
| SCM_RIGHTS (E6) | The two ends of an already-connected unix socket | Passes no bytes, passes references to open files | No buffering; one send, one receive | One-way; send again for the reverse |

For hot paths with heavy traffic and chunked or streaming data, shm with a lock-free ring queue (this is where Lmem03's measured 7304 MiB/s comes from) is the correct answer for keeping data out of the kernel, at the price of doing all synchronization yourself. When traffic is light and messages small, both pipe and mq have headroom to spare — pick whichever sits well in the hand. Message boundaries, prio, and the ability to ask at any moment how many messages the queue holds (mq_curmsgs): mq throws all of these in for free. If unpacking yourself costs you no sleep, pipe does not even need a name. When the two sides are parent and child running one errand together, pipe is enough to start. When the processes are unrelated, give a FIFO or mq a name; for bidirectional use, go to mq or sockets — do not force-bend two pipes. When what you are passing is really access rights, SCM_RIGHTS was made for exactly that, and the content need not be read out and written back. Where multiple writers share one channel, holding the PIPE_BUF line measured in E3c is enough.

The bandwidth comparison, same machine and same measurement, medians taken — one glance at the magnitudes is enough when selecting:

| Channel | Granularity | Median MiB/s | Source |
|---|---|---|---|
| shm SPSC ring queue (single producer, single consumer) | 1024 B × 1024 | 7304 | Lmem03 (quoted) |
| pipe | 1024 B × 1024 | 2349 | Lmem03 (quoted) |
| POSIX mq (queue depth 10) | 1024 B × 1024 | 1245 | this article, E4d |
| POSIX mq (queue depth 10) | 8192 B × 128 | 3179 | this article, E4d |
| pipe | 8192 B × 128 | 4753 | this article, E4d |

Once more, the opening caveat: on WSL2 the round-to-round swing is ±40%; the absolute values are for magnitude only; the ordering shm ≫ pipe ≥ mq was stable across all rounds; and the larger the messages, the smaller the gap between mq and pipe.

## How the Other Side Looks

The Windows side has no POSIX-style pipe-plus-fork-inheritance usage. The anonymous pipe's CreatePipe serves only between related processes together with handle inheritance; the standing cross-process channel is the named pipe, created by CreateNamedPipeW, with its name registered under `\\.\pipe\`; the client takes the same name and connects through CreateFileW. It holds two talents FIFO lacks: the first is bidirectional reading and writing; the second is message-framed transmission, where the unpacking is done on the kernel side. The mailslot is a one-way small-message broadcast channel — one writer writing to many readers at once — closer in spirit to mq's usage. The POSIX semaphore has its counterpart in the Semaphore object, and everything we need is there. fd passing has no direct Windows counterpart: to hand a handle to another process you go through DuplicateHandle, copied over by a middleman holding both processes' handles — there is no unified delivery channel like socket ancillary data. How to pick handles for passing to a child was measured live in the established [Process and Jobs](../../windows/process/01-createprocess.md) article, in STARTUPINFOEX's handle whitelist. The WaitFor family on the waiting side we have also used in the [Console Events and APC](../../windows/process/02-console-apc.md) article. The Windows side of named pipes and mailslots is not unfolded in this volume — it is saved for the platform-abstraction article's side-by-side. Keep this article's six channels in mind, and we will match them to their counterparts one by one then.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="pipe(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/pipe.7.html"
  />
  <ReferenceItem
    :id="2"
    title="fifo(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/fifo.7.html"
  />
  <ReferenceItem
    :id="3"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="4"
    title="popen(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/popen.3.html"
  />
  <ReferenceItem
    :id="5"
    title="mq_overview(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/mq_overview.7.html"
  />
  <ReferenceItem
    :id="6"
    title="mq_notify(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/mq_notify.3.html"
  />
  <ReferenceItem
    :id="7"
    title="sem_overview(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/sem_overview.7.html"
  />
  <ReferenceItem
    :id="8"
    title="unix(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/unix.7.html"
  />
</ReferenceCard>
