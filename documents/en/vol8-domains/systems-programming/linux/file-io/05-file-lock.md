---
title: "File Locks: flock and fcntl Record Locks"
description: "Two processes touch one file at the same time — who yields to whom: this article sets flock and fcntl record locks, the two coexisting mechanisms, against each other on the same ext4 — flock's locks hang on the open file description (a re-open within the same process self-conflicts, the blocking version captured being killed by alarm after 2 seconds of waiting, release only when the last reference is closed, a forked child can release the parent's lock), while fcntl's locks hang on the process (byte ranges take effect, F_GETLK reports the other lock's own range, a later lock splits the overlap into W[0,50)+R[50,150)), plus the biggest trap of the whole article, E2d: closing any one fd of the file within the same process releases all record locks on the spot (whether that fd was opened after locking or well before), while F_OFD_SETLK since Linux 3.15 is free of the problem (l_pid reports -1, released only when the right description is closed); a field-by-field decode of /proc/locks including the hands-on lesson about the decimal inode, a master table of fork/dup/exec inheritance, RAII file_lock's try_lock_for polling timing, 8-process serialization at 8090.5 ms versus 1009.9 ms and the ~16 µs price per handoff, tmpfs's line-for-line identical output as VFS-layer evidence, and NFS honestly marked as untested"
chapter: 8
order: 5
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 46
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "错误处理范式:从 errno 到 expected"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - mutex
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/05-file-lock.md
  source_hash: d506c5724090c14fbedf2d1e2bb9ce0eabdc09875adb33faf712256931ca2a00
  translated_at: '2026-11-22T16:17:45+00:00'
  engine: glm-5.3
  token_count: 26028
---
# File Locks: flock and fcntl Record Locks

Through the first four articles, the file side of things has been single-machine work: open, read, mmap, directory traversal — one process handled everything alone. Even when [L02](./02-mmap-memory-mapping.md)'s experiments had two processes sharing the same page cache, they merely saw each other's written bytes; nobody blocked anybody's way. This article truly brings the second process onto the stage: the question becomes two processes racing to write the same file — A is halfway through when B walks in; now what? [L01](./01-posix-file-io.md) actually issued two small tools, but each covers only its own little stretch. `O_APPEND` fuses "move offset to end of file + write" into one atomic step, and what it guarantees is only that the appending instant doesn't overwrite each other. `O_CREAT|O_EXCL` fuses "check existence + create" into one atomic operation, and what it guarantees is only a single winner when racing to create a lock file. What A wants is to modify a whole stretch of the file, and B must not touch it until A is done — that kind of cross-process mutual exclusion, neither small tool can provide. What fills this gap is the file lock.

Linux holds two sets, and we must know both. `flock(2)` is BSD lineage: an API of a lone two parameters, locking the whole file; the man page positions it as placing or removing an advisory lock on an open file. `fcntl(2)`'s record locks follow the POSIX standard: through the familiar fcntl plus a batch of `F_`-prefixed command words, they can lock arbitrary byte ranges. The two look alike, and both can do mutual exclusion, but ask "who does the lock actually belong to?" and the two give different answers. A flock lock hangs on the **open file description** — the man page's original words — which [L01](./01-posix-file-io.md) introduced when discussing dup; it is a system-level object. An fcntl record lock hangs on the **process**. Every behavioral difference across close, fork, exec, and dup is a corollary of that one sentence. In this article we run experiments along that main thread, testing the semantic matrix of both lock families to the bottom, group by group.

One more property must be stated before we start: both lock families grant advisory locks. An advisory lock only blocks processes that also play the locking game — once A holds the lock, B's locking request must wait. But swap in a B that never calls the lock at all and simply fires off a `write(fd, ...)`, and the kernel will not stop it. The field table in man 5 proc_locks still keeps a slot for MANDATORY (mandatory locks), which truly could block uncooperative reads and writes too, but Linux's implementation has long been abandoned, and we don't touch it. So an advisory lock's binding force covers only the participants; someone bypassing the lock to read and write directly is beyond its reach — and that is also where it differs most from a thread mutex.

Experiments in this article are numbered E1 through E7, corresponding one-to-one with the seven directories 01 through 07 under `code/volumn_codes/vol8/systems-programming/linux/file-io/05-file-lock/` in the repository; the code and all raw outputs are archived for cross-checking anytime. The environment, as usual, stated plainly: the desktop is an AMD Ryzen 7 9700X (8 cores, 16 threads), the WSL2 kernel 6.18.33.2-microsoft-standard-WSL2, the toolchain g++ 16.2.1; everything compiles with `-std=c++20 -O2 -Wall -Wextra -Wpedantic` and yields zero warnings. Data files live on ext4 under `/home/charliechen/l05_scratch/eN/` (/dev/sdd — its device number 8:48 is a number E3 will call on), with the paths burned into constants at the head of each source file; create the directories before re-running. The old three — `unique_fd`, `sys_call`, `errno_code` — follow the definitions from [the RAII article](../../thinking/01-raii-paradigm.md) and [the error-handling article](../../thinking/02-error-paradigm.md); we only reference them. We also borrow a small function that translates an errno value into its name, `errno_name`, defined in experiment 01's source. One observation discipline about multi-process timelines belongs up front: the child processes in the experiments always exit via `_exit()`, which does not flush stdio buffers ([L03](./03-page-cache.md)'s scenario C measured this loss by hand), so every experiment starts with `setvbuf(stdout, nullptr, _IONBF, 0)` — whoever gets the lock writes the line, and the timestamps cannot lie.

## E1 flock: the lock hangs on the open file description

We start with flock; its entire signature is `int flock(int fd, int operation)`. The value of operation picks a lock type from `LOCK_EX` (exclusive) and `LOCK_SH` (shared), OR-ing in `LOCK_NB` (non-blocking) or `LOCK_UN` (release) when needed. The first sentence of man 2 flock settles the ownership: the lock recognizes only the open file description — neither the fd number nor the process catches its eye. Let's lay out again the two-level structure [L01](./01-posix-file-io.md) covered with dup: the process-private fd table, whose every entry points to a system-level open file description. Two `open` calls yield two independent descriptions. `dup` and `fork` copy the table entry, so the two fds point at the same description. All of flock's temperament can be derived from this structure.

The reasoning is done, but we still want to see it with our own eyes. E1 stages five observation groups with two kinds of actors: lock holder A, and contenders. The contenders do their own `open` — E1e will explain why: an inherited fd cannot test mutual exclusion. The skeleton looks like this:

```cpp
// flock_matrix.cpp(节选):竞争者子进程,自己 open 拿全新描述,非阻塞试锁
pid_t spawn_contender()
{
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)};
        int r = ::flock(own.get(), LOCK_EX | LOCK_NB);
        std::printf("  [%6ld ms] 竞争者(pid=%d, 自己open):flock(LOCK_EX|LOCK_NB) = %d",
                    ms(), ::getpid(), r);
        if (r == -1) {
            std::printf(", errno=%d(%s)", errno, errno_name(errno));
        }
        std::printf("\n");
        ::_exit(0);
    }
    return pid;
}
```

Here are the raw outputs of the five observation groups; timestamps are milliseconds relative to program start:

```text
$ ./e1
pid=152060, 数据文件:/home/charliechen/l05_scratch/e1/flock.bin(ext4)

==== E1a  LOCK_EX 互斥:第二进程阻塞,解锁瞬间接棒 ====
  [     0 ms] A(pid=152060):flock(LOCK_EX) = 0,持锁
  [   400 ms] A:LOCK_UN,放锁 → C 的拿锁时刻应紧贴这一行
  [   400 ms] C(pid=152062):自己 open + 阻塞 flock(LOCK_EX) 返回 0,拿到锁

==== E1b  同一进程两次 flock:同 fd / dup / 重新 open ====
  [   400 ms] fd1: flock(LOCK_EX) = 0
  [   400 ms] fd1 再 flock(LOCK_EX) = 0(同一描述,重复加锁即转换)
  [   400 ms] fd1 再 flock(LOCK_SH) = 0(同一描述上 EX→SH 转换)
  [   400 ms] dup(fd1) 上 flock(LOCK_EX|LOCK_NB) = 0(dup 共享同一描述,不冲突)
  [   400 ms] 竞争者(pid=152069, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [   401 ms] 重新 open 的 fd2:flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [   401 ms]            → 同进程两个描述也会自冲突
  [   401 ms] 子进程对 fd2 用阻塞版 flock,2 s alarm 护航…
  [  2401 ms] 子进程被 SIGALRM 击杀:阻塞版 flock 2 s 未返回 → 自锁死

==== E1c  LOCK_NB:冲突时 -1 + EWOULDBLOCK ====
  [  2401 ms] 竞争者(pid=152083, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)

==== E1d  close(fd) 隐式释放:A close 后 B 立刻拿到 ====
  [  2402 ms] A 持锁
  [  2402 ms] 竞争者(pid=152084, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [  2402 ms] A close(fd)
  [  2402 ms] 竞争者(pid=152085, 自己open):flock(LOCK_EX|LOCK_NB) = 0

==== E1e  fork 继承:锁属于打开文件描述,不属于进程 ====
  [  2402 ms] 父:持锁(fork 前)
  [  2403 ms] 子(pid=152086):继承 fd 上 flock(LOCK_EX) = 0(同一描述,不与父冲突)
  [  2403 ms] 子:LOCK_UN = 0(放的是整个描述上的锁)
  [  2403 ms] 竞争者(pid=152087, 自己open):flock(LOCK_EX|LOCK_NB) = 0
  [  2403 ms] 父:再次持锁
  [  2403 ms] 子(pid=152088):close(继承 fd)
  [  2404 ms] 竞争者(pid=152089, 自己open):flock(LOCK_EX|LOCK_NB) = -1, errno=11(EWOULDBLOCK)
  [  2404 ms] 竞争者(pid=152090, 自己open):flock(LOCK_EX|LOCK_NB) = 0
```

E1a is the baseline: A takes the lock and sleeps 400 ms while contender C waits with the blocking `flock`. The moment A's `LOCK_UN` lands, C acquires the lock in **the same millisecond**. The mutual exclusion itself is no surprise; what's worth recording is how tightly the timing fits — no visible gap between release and takeover, a price we will measure in E6. E1b is the interesting one; let's read line by line. The second `flock(LOCK_EX)` on the same fd returns 0 — the man page is clear that a repeated call on the same description is a **conversion**, not a second lock added; even the EX-to-SH downgrade goes this way (the same man page section reminds us that the conversion is not guaranteed atomic: the kernel's approach is to drop the old lock and take the new one, with a window in between). The `dup`'d fd succeeds immediately, because dup copies a new table entry pointing at the same description — the man page's original words are "refer to the same lock".

The two lines about re-opening expose flock's most accident-prone semantics: fd2 was opened by the same process, yet the lock attempt still ate -1 with EWOULDBLOCK. Two descriptions within one process — the process blocked itself. man 2 flock puts the behavior in plain sight: multiple fds are treated independently, and a lock taken via one fd may be denied by a lock held by the same process via another fd. What happens with the blocking version? We didn't wait it out; we sent a child to call the blocking `flock` on fd2 and armed a 2-second alarm to collect the corpse. The output at 2401 ms tells us: the child was killed by SIGALRM, and the blocking call never returned. The lock-holding fd1 and the waiting fd2 are in the same process's hands, and the one who would release never appears — this is what we mean by "blocking yourself," in its complete form.

E1c completes the error picture: a call with `LOCK_NB` returns -1 on conflict, with errno 11 — the EWOULDBLOCK we've met; it shares its value with EAGAIN on Linux, so when you see EAGAIN on the fcntl side, read it as the same word. E1d performs the implicit release: A never calls `LOCK_UN` at all, does a single `close(fd)`, and the contender acquires the lock in the same millisecond. "The last reference to the description being closed" — that is the action that truly releases the lock. E1e is the closing group, where the semantics show clearest: the child inherits an fd through fork, and `flock(LOCK_EX)` on the inherited fd returns 0 outright — parent and child point at the same description, share the same lock; conflict is out of the question. Better still, one `LOCK_UN` from the child releases **the parent's lock**, and the next contender acquires it immediately. Look at it the other way: when the child only `close`s the inherited fd, the lock stays right where it was, because the parent's fd still references that description; only when the parent closes too does the contender get it. "The lock belongs to the description" — E1e hammers that sentence once from each of three directions.

## E2 fcntl record locks: byte ranges, owned by the process

Switching to the fcntl family changes the complexion immediately. Where flock locks the whole file, fcntl record locks take as their object byte range after byte range, and the structure describing them happens to be named `flock` as well:

```cpp
// fcntl_matrix.cpp(节选):lock_spec 把五元组填进 struct flock
struct lock_spec {
    short type;    // F_RDLCK / F_WRLCK / F_UNLCK
    off_t start;   // 起始偏移
    off_t len;     // 0 = 到 EOF

    ::flock fl() const
    {
        ::flock f{};              // 裸写即可:本文件未含 sys/file.h,无遮蔽
        f.l_type = type;
        f.l_whence = SEEK_SET;    // 也可以 SEEK_CUR / SEEK_END
        f.l_start = start;
        f.l_len = len;
        f.l_pid = 0;              // 只在 F_GETLK 带回的答案里有意义
        return f;
    }
};
```

Two compilation details — don't dodge them when you write your own. First, the struct name and the function name fight: after including `<sys/file.h>`, the function name `flock()` shadows the type name, so filling the struct requires writing `struct ::flock`, taking the type from the global scope. This spelling appears in the archived sources of experiments 03, 04, and 07, all of which include that header. Excerpt 02 above does not include it, and the bare `::flock` compiles fine. Second, `_GNU_SOURCE`: `F_OFD_SETLK`, used later, is the newer record-lock command that hangs the lock on the open file description (detailed in the E2d' section); these command words sit in glibc's `__USE_GNU` section, and g++ already predefines `_GNU_SOURCE` for us when compiling C++ — the `#ifndef _GNU_SOURCE` line in the source is purely defensive; manually `#define`-ing it again would actually earn a `-Wmacro-redefined` warning. Three command words to know: `F_SETLK` locks or unlocks without blocking, returning -1 with EAGAIN on conflict. The W in `F_SETLKW` is wait — on conflict it sleeps and waits. `F_GETLK` plays the probe, asking exactly one question: "I want to lock this range — who blocks me?"

Let's go straight to E2's outputs and match up the range-lock rules (E2d gets its own section next):

```text
$ ./e2
pid=153582, 数据文件:/home/charliechen/l05_scratch/e2/fcntl.bin(ext4)

==== E2a  字节区间:A 锁 [0,100),B 试 [50,150) 冲突、[100,200) 成功 ====
  [     0 ms] A(持锁者 pid=153584):F_SETLK F_WRLCK [0,100) = 0
  [     0 ms] B(pid=153585):F_SETLK F_WRLCK [50,150) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     0 ms] B(pid=153586):F_SETLK F_WRLCK [100,200) = 0
  [     0 ms] A 已 _exit(0)(见 E2f:进程退出即释放)
  [     1 ms] B(pid=153587):F_SETLK F_WRLCK [0,50) = 0

==== E2b  F_GETLK:探测冲突锁的 pid 与实际区间 ====
  [     1 ms] A(持锁者 pid=153588) 持 F_WRLCK [0,100)
  [     1 ms] 探针B(pid=153589):F_GETLK F_WRLCK [0,200) → 撞上 F_WRLCK l_pid=153588, 区间[0,100)(r=0)
  [     1 ms] 探针B(pid=153590):F_GETLK F_WRLCK [150,200) → 无冲突(l_type=F_UNLCK)
  [     2 ms] 探针B(pid=153591):F_GETLK F_WRLCK [50,60) → 撞上 F_WRLCK l_pid=153588, 区间[0,100)(r=0)

==== E2c  读共享/写排他:四种组合 ====
  A 持 F_RDLCK [0,100) 时,B 试 F_RDLCK [0,100):  [     2 ms] B(pid=153593):F_SETLK F_RDLCK [0,100) = 0
  A 持 F_RDLCK [0,100) 时,B 试 F_WRLCK [0,100):  [     3 ms] B(pid=153595):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  A 持 F_WRLCK [0,100) 时,B 试 F_RDLCK [0,100):  [     4 ms] B(pid=153597):F_SETLK F_RDLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  A 持 F_WRLCK [0,100) 时,B 试 F_WRLCK [0,100):  [     4 ms] B(pid=153599):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))

==== E2e  替换语义:后锁覆盖重叠段;同进程第二把锁不自冲突 ====
  [     8 ms] 本进程(pid=153582):fd1 上 W [0,100)
  [     8 ms] 本进程:fd2 上再 W [0,100) = 0(POSIX 锁按进程算,永不自冲突)
  [     8 ms] 本进程:fd1 上 R [50,150) = 0(重叠段被替换)
  [     8 ms] 读探针(pid=153609):F_GETLK F_RDLCK [0,50) → 撞上 F_WRLCK l_pid=153582, 区间[0,50)(r=0)
  [     8 ms] 读探针(pid=153610):F_GETLK F_RDLCK [50,150) → 无冲突(l_type=F_UNLCK)
  [     8 ms] 写探针(pid=153611):F_GETLK F_WRLCK [50,150) → 撞上 F_RDLCK l_pid=153582, 区间[50,150)(r=0)
…(收尾清场的对照探针一行,略)…

==== E2f  进程退出释放:持锁者 _exit,竞争者立刻拿得到 ====
  [     9 ms] A(持锁者 pid=153613):持 W [0,100)
  [     9 ms] 竞争者(pid=153614):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [    10 ms] A 已 _exit(0),没有任何放锁动作
  [    10 ms] 竞争者(pid=153615):F_SETLK F_WRLCK [0,100) = 0
```

Match up E2a's three lock attempts: whether there's conflict depends on whether the ranges overlap — `[50,150)` and `[0,100)` share 50 bytes, so it blocked. `[100,200)`, measure again, is adjacent but non-overlapping, so it passed. This is exactly where record locks beat one whole-file lock: different regions of one file can be managed simultaneously by different processes; in read-heavy scenarios, sharding by range raises throughput. The last two lines of E2a also carry E2f's answer: the moment A exits, B immediately gets `[0,50)`.

E2b teaches us to see the opponent clearly with F_GETLK: fill the probe with the type and range you want to lock, make the call, and the criterion is the returned `l_type` — if it was overwritten to `F_UNLCK`, nobody is blocking us. What remains in the structure is the information of **the other side's lock**: type, owner pid, and range all right there. One detail deserves a second look: the probe reached only ten bytes into `[50,60)`, yet the range reported back was the owner's own `[0,100)`. It reports the other lock's full range; the intersection we must subtract ourselves. The read probe in E2e's output block is the cross-type case: probing for R, the reported `l_type` is W — we can still read off the opponent.

Line up E2c's four combinations, and the rule we see is isomorphic to a reader-writer lock: read-read coexists, anything involving write conflicts. E2f performs the process's exit: the lock-holding child simply calls `_exit(0)`, doing neither a release nor a close, and the contender still gets the lock immediately — the lock hangs on the process, and with the process gone, the lock naturally goes too.

Two semantics hide inside E2e, and we read them with three probes. First: fd2 was re-opened by the same process, yet taking W `[0,100)` through it actually returned 0 — POSIX record locks are per-process; lock requests submitted through any fd by the same process merge under its own name, and self-conflict never happens. Set this against E1b: with flock, a re-open blocks itself; with fcntl, a re-open is business as usual — two mechanisms giving opposite answers on the same tiny experiment. The other semantic is replacement: the later R `[50,150)` from the same process swapped the overlapping segment `[50,100)` from a write lock to a read lock, and the kernel split the original W `[0,100)` into W `[0,50)` plus R `[50,150)`. We verify segment by segment with three probes: the read probe finds W blocking the way in `[0,50)` but passes freely through `[50,150)`, and the write probe reads nothing but R in `[50,150)`. Between one process's own lock requests there is no negotiation; the later arrival directly rewrites the overlapping segment.

## E2d: closing any one fd releases all record locks on the spot

Now the heaviest group of the whole article. Take a guess first: within one process, fd1 holds W `[0,100)`; meanwhile some unrelated stretch of code opens fd2 on the same file and casually closes it when done — fd1 is still open, and we never called `F_UNLCK` once. Is the lock still there? By the "locks hang on fds" intuition, you would probably say yes. By the "released only when the last reference closes" rule E1d taught, we would say it should be even more so. Here is the measured output:

```text
==== E2d  陷阱:同进程 close 任意一个 fd → 全部记录锁释放 ====
  [     5 ms] 本进程(pid=153582):fd1 上 F_SETLK W [0,100) = 0
  [     5 ms] 竞争者(pid=153600):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     5 ms] 本进程:open 第二个 fd(只读都行),随即 close(fd2)
  [     5 ms] 本进程:fd2 已 close,fd1 还开着,一次 LOCK_UN 都没调
  [     5 ms] 竞争者(pid=153601):F_SETLK F_WRLCK [0,100) = 0
…(存档里这组还排了反过来的次序:close 加锁之前就 open 的 fd,四行输出同型,略)…

==== E2d'  对照:F_OFD_SETLK(锁挂在打开文件描述上)没有这个陷阱 ====
  [     6 ms] 本进程:fd1 上 F_OFD_SETLK W [0,100) = 0
  [     6 ms] 探针(pid=153604):F_GETLK F_WRLCK [0,100) → 撞上 F_WRLCK l_pid=-1, 区间[0,100)(r=0)
  [     7 ms] OFD探针(pid=153605):F_OFD_GETLK F_WRLCK [0,100) → 撞上 F_WRLCK l_pid=-1, 区间[0,100)(r=0)
  [     7 ms] 竞争者(pid=153606):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     7 ms] 本进程:open 第二个 fd 并 close —— 锁不受影响?
  [     7 ms] 竞争者(pid=153607):F_SETLK F_WRLCK [0,100) = -1, errno=11(EAGAIN(即 EWOULDBLOCK))
  [     7 ms] 本进程:close(持锁的 fd1)
  [     7 ms] 竞争者(pid=153608):F_SETLK F_WRLCK [0,100) = 0
```

The answer, of all things, is no. Between the contender being blocked and acquiring the lock, exactly one thing happened: the close of an fd that was **not used for locking**. And it has nothing to do with when fd2 was opened. The same experiment group also staged the reversed order: one fd was opened before locking, the lock hung on a newly opened fd, and closing the early-opened fd released all the locks just the same. POSIX wrote this semantics into the standard, and man 2 fcntl_locking's original sentence is unambiguous: closing **any one** fd referring to the file releases **all locks** the process holds on that file — the any in the original is in italics, no less. We won't dig for the historical causes; only the consequence matters: record locks hang under the process's name, the kernel must catch close events on every fd the process holds, and so the closing of any single fd becomes the trigger for a full release.

::: warning The sneakiest silent failure of cross-process locks
Any unrelated stretch of code in the same process, so long as it opens and then closes the same file, wipes out every record lock under your name. The config reader, the log writer, the code that just stats and leaves — each could be that fd2 inside your process. Only two defenses are available: either guarantee the whole process opens the file exactly once and grips that fd start to finish, or switch to the OFD locks below. The proper road for multithreaded programs is another one: each thread opens the file itself and locks with F_OFD_SETLK, the locks hang on their own descriptions, and nobody accidentally harms anybody.
:::

The E2d' control is refreshingly clean. `F_OFD_SETLK` (short for open file description lock, an extension available since Linux 3.15) moves the lock's anchor point from the process back to the open file description. man 2 F_OFD_SETLK's comparison puts it plainly: traditional record locks are associated with the process, while OFD locks are associated with the description that obtained them, and for the release timing we quote the man page: "on the last close". Released only when the last reference closes — no single close can reach such a lock. Match the output line by line: after closing the unrelated fd2, the contender stays blocked; only when the truly lock-holding fd1 is closed does the lock finally go. There is also an identifying trait: OFD locks have no owner pid — both `F_GETLK` and `F_OFD_GETLK` probing one report `l_pid=-1`; the man page's words are "_l_pid_ is set to -1". Try to find your opponent by pid, and it tells you there is no opponent process: the lock belongs to an open file description, not to any process. In new code, unless you must support old kernels or old platforms, range locks should default to the OFD versions, and the E2d trap is bypassed entirely.

## E3 /proc/locks: both lock families laid out in the open

What locks look like inside the kernel, we have an instrument for: `/proc/locks`, which lists the system's current locks as text. Behind all those -1s and 0s that E1 through E4 saw, everything here gains a name and a face. E3's layout: one lock holder puts two things on the same file — a flock exclusive lock and an fcntl write lock `[0,100)`. We then dispatch two waiters, blocking in the flock and `F_SETLKW` wait queues respectively. While the whole cast is on stage, the main process reads `/proc/locks` and filters out this file's lines by inode:

```text
$ ./e3
pid=167262, 文件:/home/charliechen/l05_scratch/e3/locks.bin
fstat: inode=1690193(十进制), 设备 8:48(十进制) = 08:30(十六进制)

  [     0 ms] 持锁者(pid=167263):flock(LOCK_EX) + F_SETLK W [0,100) 都已上身
[   300 ms] /proc/locks 中 inode 1690193 的行:
---- 锁全部在场 + 两个等待者被挡 ----
19: POSIX  ADVISORY  WRITE 167263 08:30:1690193 0 99
19: -> POSIX  ADVISORY  WRITE 167265 08:30:1690193 50 149
20: FLOCK  ADVISORY  WRITE 167263 08:30:1690193 0 EOF
20: -> FLOCK  ADVISORY  WRITE 167264 08:30:1690193 0 EOF
…(程序随后还打印了四行逐字段解码,与咱们下面的逐字段讲解重复,略)…
  [  1000 ms] 持锁者:退场(两把锁随进程释放)
  [   999 ms] F_SETLKW 等待者(pid=167265):拿到 [50,150),退场
  [   999 ms] flock 等待者(pid=167264):拿到锁,退场

[  1000 ms] 持锁者退场后,两个等待者都拿到了锁(见上)

---- 全部退场后(应为空) ----
(没有命中的行)
```

Take the first line as the specimen and decode the fields one by one. The leading `19:` is this read's ordinal, renumbered from scratch on every read — don't take it for a stable id. The second field, `POSIX`, is the lock family; the two other common values are `FLOCK` and `OFDLCK` (OFD locks). The fcntl and flock families are registered separately in the kernel and don't talk to each other — the FLOCK and POSIX entries hanging on the same file in E3 are living proof. `ADVISORY` says advisory lock. `WRITE` and `READ` are the lock types. `167263` gives the owner pid; OFD locks show -1 here. `08:30:1690193` gives the device number plus inode. The two trailing fields `0 99` are the lock's byte range.

The range field has its own subtleties; look closely. With fcntl's l_start=0 and l_len=100, what we say aloud is the half-open range `[0,100)`, but `/proc/locks` prints **closed-interval endpoints** 0 and 99 — 100 bytes, the last at offset 99. When l_len is 0 (lock to EOF), the right side prints `EOF`, and the FLOCK family always prints `0 EOF` here. Blocked waiting requests don't get their own line either; they hang indented with `->` under the ordinal of whoever blocks them: under POSIX lock 19 hangs 167265's wait, and under FLOCK 20 hangs 167264. The range printed on a waiter's line, `50 149`, is again its own requested range, for the same reason as F_GETLK's reporting above. Once the holder leaves the stage and both waiters acquire in the same millisecond, a re-read of `/proc/locks` comes back clean.

This next field the author must stop and address specially, because we fell flat on our face right here: inside `08:30:1690193`, the device number is hexadecimal while the inode is **decimal**. man 5 proc_locks describes this field only as "colon-separated subfields, major:minor device number plus inode" and never mentions the bases. The truth lives in the kernel's fs/locks.c, in `lock_get_status()`; we copy the format string verbatim: `"%d %02x:%02x:%llu"` — the device number goes through %02x, the inode through %llu. The first version of the experiment grepped with a hexadecimal inode converted via `printf '%x'` and came up empty; we nearly wrote down the false verdict "the lock isn't in /proc/locks" on the spot. Matching directly against fstat's decimal `st_ino` afterwards hit all four lines. So your troubleshooting posture should be: take the raw decimal value from `stat -c %i file`, but convert the device number to hexadecimal with `printf '%02x:%02x'`. One more thing: the machine's resident dotnet and VS Code family of processes hold locks of their own, so `/proc/locks` mixes in other people's lines — filter by inode, and don't go counting total lines.

## E4 fork, dup, exec: who inherits, who releases

We have seen the individual behaviors; now we arrange them into one master table. E4 ran six scenarios with the same binary, each with contenders doing their own open, and the raw output records are all in the archive; here we gather them into a table and pick a few measured originals to vouch for them:

| Dimension | flock(2) | fcntl(2) record locks (F_SETLK family) |
|---|---|---|
| Scope | The whole file | Arbitrary byte range; l_len=0 means to EOF |
| Lock belongs to | The open file description | The process; the F_OFD_SETLK variant belongs to the description (since Linux 3.15) |
| close(the locking fd) | Released only when the description's last reference closes | All record locks released on the spot, including those via other fds |
| close(another fd) | Lock unaffected | Same full release |
| fork | Parent and child share one lock; the child can release the parent's lock | Not inherited; the child is an independent owner and conflicts with the parent |
| exec | Survives if the fd stays open; released on the spot with O_CLOEXEC | Locks survive exec with the process |
| Second open in the same process | Self-conflict; the blocking version waits forever | Merged under its own name; the later lock rewrites the overlap |
| Probing | None; you can only try | F_GETLK reports pid and range; OFD locks report l_pid=-1 |
| tmpfs | Same semantics as ext4 (measured in E7) | Same semantics as ext4 (measured in E7) |
| NFS | Client emulates with a whole-file range lock | Risk of losing locks (lease expiry) |

The output originals for the two fork rows carry the most force; we quote them directly. On the flock side, the child locking on the inherited fd returned 0, and the child's line `LOCK_UN = 0` — releasing the parent's lock — means the contender then got it. On the fcntl side, the child locking via the inherited fd ate -1 with errno 11, and `F_GETLK` reported `l_pid=168327` — exactly the father's pid. What blocked it was the father, because fcntl's locks recognize processes, and the forked child is somebody else. One sentence from man 2 fcntl_locking settles both rows: a forked child receives no inheritance, while locks survive across execve.

Let's glance at both exec scenes too. On the flock side: the lock-holding child execs itself (the fd lacks O_CLOEXEC, and it also calls `fd.release()` so RAII's destructor doesn't race to close the fd); the contender stays blocked through the window, the post-exec process holds the lock firmly for 600 ms, and it is released the moment it exits. The same scene with an `O_CLOEXEC` fd: at the instant of exec the kernel closes the fd, the lock vanishes on the spot, and the contender in the window acquires directly. On the fcntl side: the locking child execs itself, and the contender remains blocked just the same, because the lock follows the process — the image changed, but the owner didn't. One small phenomenon to flag in advance so the archive doesn't puzzle you: after exec, the child's timestamps restart from 0, because t0 is a static variable inside the process image; exec swapped the image and the clock went with it. That is expected behavior, not a mis-shot.

For the two NFS rows, we confess honestly: untested. The author's WSL2 has no NFS mounts, and the table's statements all come from the man pages' theoretical line:

> We copy man 2 flock's line verbatim: on kernels 2.6.11 and earlier, flock() did not lock at all on NFS. Since 2.6.12, the client emulates flock() as an fcntl byte-range lock over the whole file, and the man page's original text even stresses do interact in italics — meaning the two lock families do interact on NFS. Since 2.6.37, nfs further offers the `local_lock` mount option, treating them as local locks.
>
> The NFS section of man 2 fcntl_locking also lists "lost locks" for us: administrative action on the server or a network partition can make locks disappear, and since Linux 3.12, continuing I/O after losing a lock may earn you an EIO. NFSv4 leases default to 90 seconds, and `nfs.recover_lost_locks` defaults to 0 due to the data-corruption risk — locks lost are not returned.

Locks on files shared across machines have softer semantics than local ones; factor this discount into your design.

## E5 file_lock: writing flock into the destructor

Enough of watching mechanisms; time to gather them into a type. The fd is an old customer of [the RAII article](../../thinking/01-raii-paradigm.md); this time we also bind "the life of one flock lock" to the life of an object. The skeleton of `file_lock` follows, with the complete demo stored in the 05-raii-file-lock/ directory:

```cpp
// file_lock.hpp(节选):构造加锁,析构放锁,move-only
class file_lock
{
public:
    explicit file_lock(const char* path, bool exclusive = true)
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)}
    {
        lock(exclusive);
    }

    file_lock(const char* path, defer_lock_t)     // 只 open 不上锁
        : fd_{sys_call("open", ::open, path, O_RDWR | O_CREAT, 0666)}
    {
    }

    ~file_lock()
    {
        if (fd_) {
            ::flock(fd_.get(), LOCK_UN);   // 显式放锁;紧随的 close 是第二道保险
        }
    }

    file_lock(file_lock&& other) noexcept             // fd 所有权移交
        : fd_{other.fd_.release()}, exclusive_{other.exclusive_}
    {
    }
    file_lock& operator=(file_lock&& other) noexcept; // 同思路,见存档
    file_lock(const file_lock&) = delete;
    file_lock& operator=(const file_lock&) = delete;

    bool try_lock(bool exclusive = true) noexcept
    {
        int r = ::flock(fd_.get(), (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB);
        if (r == 0) { exclusive_ = exclusive; return true; }
        if (errno == EWOULDBLOCK) { return false; }   // 冲突不算错误
        return false;   // 其余 errno 理应抛,noexcept 里只能吞,注释记着这个取舍
    }

    template <class Rep, class Period>
    bool try_lock_for(std::chrono::duration<Rep, Period> d, bool exclusive = true)
    {
        const auto deadline = std::chrono::steady_clock::now() + d;
        for (;;) {
            if (try_lock(exclusive)) { return true; }
            if (std::chrono::steady_clock::now() >= deadline) { return false; }
            ::usleep(1000);            // 1 ms 轮询:分辨率即轮询间隔
        }
    }

private:
    unique_fd fd_;
    bool exclusive_ = true;
};
```

Three design points to state. The explicit `LOCK_UN` in the destructor is actually redundant — close releases a flock lock anyway — but we keep it so that "release" sits in a visible spot in the code; when something goes wrong and you're reconstructing the timeline, there is one explicit release line to check against. `try_lock`'s policy: EWOULDBLOCK is the normal answer "the lock is held"; it returns false and throws nothing. Other errnos deserve escalation into exceptions, but under a `noexcept` roof, throwing means terminate; the header's comments record this compromise honestly, and a real project may loosen it into a throwing interface. `try_lock_for` is the most resigned of the three: flock has no native "wait this long" parameter, the blocking version waits indefinitely, and `F_SETLKW` is the same, so the only option is `LOCK_NB` plus small 1-millisecond polling steps — the wait's resolution is the polling interval. The lock-release-instant takeover timing we saw in E1a pays a delay of up to 1 millisecond here; for finer waiting, we would have to split it into two stages and add a semaphore — already another article's job.

Let's run the two-process timeline for you: A holds the lock right after construction, writes ticket=42, and grips the lock for a full 700 ms. B starts in the `defer_lock` posture and tries each of the three postures in turn:

```text
$ ./e5
pid=169508, 文件:/home/charliechen/l05_scratch/e5/raii.bin(ext4)

==== 双进程时序:A 拿写锁写数据,B 有限等待接棒 ====
  [     0 ms] A(pid=169508):构造 file_lock,已写 "ticket=42",持锁 700 ms
  [     0 ms] B(pid=169509):try_lock() = false(锁在 A 手里)
  [   201 ms] B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
  [   700 ms] A:作用域将尽,file_lock 析构在即
  [   700 ms] A:已放锁
  [   700 ms] B:try_lock_for(3s) = true(等了 499 ms —— A 一放锁就拿到)
  [   700 ms] B:读到 "ticket=42"(临界区数据完好)

==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====
  [   701 ms] 探针(lk1 持锁中):flock = -1(锁被占)
  [   701 ms] move 后:lk1 还活着吗?否(moved-from);lk2 持有 fd=3
  [   701 ms] 探针(lk1 析构后):flock = -1(锁被占)
  [   702 ms] 探针(lk2 reset 后):flock = 0(拿到锁)
```

On the timing half, B's three attempts spread out the three waiting postures: ask once immediately, answered false. The 200-ms-bounded attempt timed out on schedule. The 3-second-bounded attempt acquired at 499 ms — the instant A released — and read the data A had written into the critical section. On the move half, when the moved-from lk1 is destroyed, the probe is still blocked — it has already handed over the fd, and its destructor cannot touch the new owner's lock. Only after lk2 explicitly resets does the probe acquire. The move constructor transfers the other side's fd via `release()`, and the moved-from becomes an empty shell from then on — the line "probe (after lk1's destructor) still blocked" in the output verifies exactly that. The whole skeleton is isomorphic to the `unique_lock` mental model, so we won't explain further.

## E6 contention measured: eight processes in one queue

The locks work well, and we must own the price. E6's setup: fork 8 children, each doing an independent open; align the starting line with pipes; every round is three steps — take the lock, sleep 10 ms (simulating a critical section), release the lock — for 100 such rounds. The control group sleeps the same without locking. We add one more group with an empty critical section: 8 processes each running 2000 rounds of pure lock/unlock. Each of the three modes runs three rounds, and we take the middle round's numbers:

```text
$ ./e6
pid=169910, 文件:/home/charliechen/l05_scratch/e6/contention.bin(ext4), 8 工作进程 × 100 轮,临界区 usleep(10ms),3 轮取中位

模式        r1            r2            r3
locked      8090.4 ms    8090.5 ms    8091.1 ms  | 中位   8090.5 ms,10113.1 µs/人次
free        1009.8 ms    1009.9 ms    1009.9 ms  | 中位   1009.9 ms,1262.4 µs/人次

空临界区(纯锁传递开销):8 进程 × 2000 轮 lock/unlock
micro        252.3 ms     255.7 ms     270.0 ms  | 中位    255.7 ms
```

For locked's 8090.5, let's lay out the arithmetic: 8 processes × 100 rounds × 10 ms = 8000 ms, the theoretical total once the eight critical sections are fully serialized; the measurement exceeds it by only 90 ms, and the drift across the three rounds stays within 0.7 ms — the serialized path is in fact extremely steady, because it never suffers from scheduling jitter. free's 1009.9 is the same batch of processes sleeping without the lock: eight stretches of 10 ms × 100 rounds run fully in parallel, so the total equals one stretch's duration. Compare the two numbers and the eightfold gap measures not flock being slow but mutual exclusion itself: the sleeps are lined up into one queue that nobody can skip. The extra 90 ms spread over 800 handoffs is about 113 µs each, and that price includes the journey of waking up a process that slept its full 10 ms. The micro group, where nobody sleeps, did 16000 handoffs in 255.7 ms — about 16 µs apiece, the true floor price of a pure handoff. The README in 06-contention-bench records one more corroboration: on the same machine, 1000000 uncontended lock/unlock pairs on a single fd take 0.675 µs per pair — the same call, with 8-way contention multiplying the unit price about 24-fold; everything above that floor is the journey of sleeping and waking, and the lock-table operation itself was already measured in that 0.675 µs bracket.

Translated into engineering judgment, these numbers say: with a 10-ms critical section, the lock's overhead is about 1% of the total — hard to lose however you write it. As the critical section approaches zero, the 16-µs-per-handoff floor shows through, and with 8 processes frantically fighting over an empty lock, throughput jams on the handoff fee. So in usage: put the whole stretch of reads and writes you actually need to protect inside the critical section, and let each 16-µs handoff guard enough work — don't use it as a high-frequency tiny lock.

## E7 tmpfs control: both lock implementations live in the VFS layer

Let's settle the last question too: are these semantics ext4's doing, or the kernel's uniformly? We ran the same probe (fs_probe.cpp, four small observations) twice, once against ext4 and once against tmpfs; this machine's /tmp is mounted tmpfs (per df -T). The two outputs sit side by side, ext4 on the left, tmpfs on the right:

```text
$ /tmp/e7 ~/l05_scratch/e7/ext4.bin                    $ /tmp/e7 /tmp/l05_e7_tmpfs.bin
文件:/home/charliechen/l05_scratch/e7/ext4.bin         文件:/tmp/l05_e7_tmpfs.bin
st_dev=8:48(十进制)= 08:30(十六进制),inode=1716962    st_dev=0:77(十进制)= 00:4d(十六进制),inode=11972

[P1] flock 互斥:A 持锁 300 ms,B 阻塞版接棒           [P1] flock 互斥:A 持锁 300 ms,B 阻塞版接棒
  B(pid=170777):阻塞 300 ms 后拿到锁                   B(pid=170788):阻塞 300 ms 后拿到锁

[P2] 同进程重新 open:flock(LOCK_EX|LOCK_NB)           [P2] 同进程重新 open:flock(LOCK_EX|LOCK_NB)
  fd2 试锁 = -1, errno=11(EWOULDBLOCK)                 fd2 试锁 = -1, errno=11(EWOULDBLOCK)

[P3] fcntl:close 无关 fd → 记录锁全释放?              [P3] fcntl:close 无关 fd → 记录锁全释放?
  竞争者:F_SETLK = -1(被占)                           竞争者:F_SETLK = -1(被占)
  已 close 无关的 fd2                                   已 close 无关的 fd2
  竞争者:F_SETLK = 0(拿到)                             竞争者:F_SETLK = 0(拿到)

[P4] 持 flock 时 /proc/locks:                         [P4] 持 flock 时 /proc/locks:
  /proc/locks: 13: FLOCK  ADVISORY  WRITE 170776 08:30:1716962 0 EOF   /proc/locks: 6: FLOCK  ADVISORY  WRITE 170787 00:4d:11972 0 EOF
```

Normalize the paths, pids, and leading ordinals, and look again: the two outputs agree line for line. P1's mutual-exclusion timing, P2's self-conflict, P3's release-everything trap — tmpfs replayed every one of them without omission. The reason is not deep: both flock and fcntl record locks are implemented in the **VFS** (virtual file system, the layer where Linux unifies the interface for all filesystems); ext4 and tmpfs are merely concrete filesystems hanging beneath it, and even /proc/locks sees tmpfs locks as usual. The only difference between the two outputs is the device number field in P4: the ext4 line shows 08:30 (the block device number of /dev/sdd), while the tmpfs line shows 00:4d — tmpfs has no block device; that is the anonymous device number the kernel assigned it, which also drags the inode magnitude down a notch.

Now look back at this article's opening question: A is halfway through writing when B walks in — what now? Both lock families hold answers. flock is simple and burly; using the whole file as the critical section, paired with a lock file claimed via `O_CREAT|O_EXCL`, is the classic single-instance daemon pattern. fcntl's record locks shard by bytes but carry E2d's close-releases-everything trap; new code switched to `F_OFD_SETLK`, and the trap is gone. Which family we pick still comes down to that opening sentence: who does the lock belong to. When you can't reason it through, come back and flip through E1 to E4's outputs — the archive holds ready answers.

## How the other side sees it

Windows holds neither flock nor fcntl-style record locks; it hands the same job to `LockFileEx` and `UnlockFileEx`: the unit of locking is a byte range, exclusive versus shared is chosen by `LOCKFILE_EXCLUSIVE_LOCK`, and non-blocking requires adding `LOCKFILE_FAIL_IMMEDIATELY` — the outline of the semantics is closer to fcntl record locks. There is also one layer Linux lacks: `CreateFileW`'s `dwShareMode` declares sharing restrictions at the very moment of opening, so another handle trying to open the same file in a conflicting way is rejected right at the door of open. The full comparison (including LockFileEx's waiting semantics and handle inheritance) waits for the Windows-side mirror article on file locks ([File Locks: LockFileEx](../../windows/file-io/05-lockfileex.md)); for the foundation, see [Win32 File I/O](../../windows/file-io/01-win32-file-io.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="flock(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/flock.2.html"
  />
  <ReferenceItem
    :id="2"
    title="fcntl_locking(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl_locking.2.html"
  />
  <ReferenceItem
    :id="3"
    title="F_OFD_SETLK(2const)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/F_OFD_SETLK.2const.html"
  />
  <ReferenceItem
    :id="4"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="5"
    title="proc_locks(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc_locks.5.html"
  />
  <ReferenceItem
    :id="6"
    title="open(2) — where open file description is defined"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <ReferenceItem
    :id="7"
    title="nfs(5) — the local_lock option"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/nfs.5.html"
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
