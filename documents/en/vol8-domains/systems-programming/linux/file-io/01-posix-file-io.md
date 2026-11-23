---
title: "POSIX File I/O: open/read/write and the Life of an fd"
description: "The first foundation stone of file I/O on the Linux side: use strace to watch an fd live out its entire life, from being born in open, put to work by read/write, to taking its bow in close — the flags family and how umask trims permissions, why partial reads and writes are the norm, the fd table and dup2/dup3 redirection, where fcntl's two kinds of flags live, pread and its untouched offset, SEEK_DATA/SEEK_HOLE for mapping out sparse files, and the crash window between the page cache and fsync; the public tools unique_fd and sys_call carry over the definitions from the two thinking-foundation articles, and this article takes them into their first real engagement"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 32
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/linux/file-io/01-posix-file-io.md
  source_hash: a7f8375cdbb096dd9283e1ab794c5af2d20795212b068ca93c884ab866c8e17d
  translated_at: '2026-11-22T16:06:50+00:00'
  engine: glm-5.3
  token_count: 23738
---
# POSIX File I/O: open/read/write and the Life of an fd

This volume covers systems programming, and the overview article has already settled the main storyline: we follow the two camps, Linux and Windows, in parallel comparison, working our way from the application-facing OS APIs all the way to Modern C++ wrappers. This article opens the Linux side, and we picked file I/O as the starting point. The `open`, `read`, and `write` we are about to write look perfectly harmless by their names. But think again about logs, configuration files, and databases — and sockets are the same: they all take a long detour out there, and in the end everything lands back on the same group of calls and the same small integer.

That small integer we deal with every day is the **fd** (file descriptor). The kernel keeps a table of open file descriptors for every process, and an fd is simply an index into that table: 0/1/2 are taken by stdin/stdout/stderr the moment the process is born, so any fd you open fresh starts from 3. Until we look it up in the table, an fd is just a number — it looks neither like an object nor like a pointer. From here on we walk alongside an fd through its whole life: born inside `open()`, ordered around by `read`/`write`, and finally taking its bow in `close()`.

There is no rush to memorize the API manual; we turn straight to strace and take a snapshot of the scene at the syscall layer. The program below creates a file with `O_WRONLY|O_CREAT|O_TRUNC`, writes 13 bytes, closes it, then reopens it read-only with `O_RDONLY` and reads 64 bytes:

```text
$ strace -e trace=openat,read,write,close ./exp1
openat(AT_FDCWD, "/tmp/sysprog-linux01/hello.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "hello, posix\n", 13)          = 13
close(3)                                = 0
openat(AT_FDCWD, "/tmp/sysprog-linux01/hello.txt", O_RDONLY) = 3
read(3, "hello, posix\n", 64)           = 13
+++ exited with 0 +++
```

> We left out the `openat` lines where the dynamic linker opens the `.so` files. If you go look at the full trace, you will find that every one of them carries the `O_CLOEXEC` flag.

There are only five syscalls in that trace, yet two details are hiding in there, each worth a stop.

Here is the first: we clearly called `open`, yet the trace shows nothing but `openat` — huh? Don't worry, this is no mystery. Today's `open()` is a compatibility shell inside glibc; the syscall actually sent out is `openat(AT_FDCWD, ...)`, where AT_FDCWD is a sentinel value meaning that the path is resolved relative to the current working directory. It is glibc, not the kernel, that keeps this shell in user space. As of glibc 2.26, glibc's `open` wrapper uniformly goes through `openat`, and the "C library/kernel differences" section of man 2 open spells this out. The fun part is that in the x86_64 kernel syscall table `open` is still alive and well, while on newer architectures like arm64/riscv64 it is genuinely gone.

The other detail is the last line: `read(3, ..., 64) = 13`. We asked for 64 bytes and got only 13. The whole file is exactly 13 bytes; the read hit end of file, so it simply finished its job rather than reporting an error. Partial reads and writes are the POSIX norm, and we will devote a dedicated section to them later.

Let me sketch the boundaries while we are at it: C's `fopen`/`fread` was covered in volume 1 ([C File I/O and stdlib](../../../../vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md)); `std::filesystem` we leave to later articles in this volume on directories and metadata; mmap is the star of the next article ([Attaching a file to the address space](./02-mmap-memory-mapping.md)); and file locks plus inotify get their own later chapters in this volume. On the error-handling side, the type foundation this article builds on is still `std::error_code` and `std::system_error`; the full story of that machinery was told in volume 3's [error_code deep dive](../../../../vol3-standard-library/error-utils/66-error-code.md), so we will not tell it again here. As for how to use these types comfortably on the Linux side, the two thinking-foundation articles have the answers ready: `unique_fd` is defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md), and `errno_code` and `sys_call` in [Error-Handling Paradigms](../../thinking/02-error-paradigm.md). We simply pick the three tools up and use them; not one of them gets its definition rewritten in this article.

The experiment conventions also go up front, so nothing catches you off guard later: experiments in this article are numbered exp1 through exp11, and the numbering does not always match the order of appearance — exp5 shows up before exp4, and you will see more of that later. The three experiments from exp9 to exp11 — fcntl flags, the dup family plus close-on-exec, and lseek's SEEK family — have their code and raw outputs archived in the repository under `code/volumn_codes/vol8/systems-programming/linux/file-io/01-posix-file-io-supplement/`, in the three directories 01-fcntl, 02-dup, and 03-seek. Reproduction commands live in each directory's README; everything compiles with g++ 16.2.1 and `-std=c++20 -Wall -Wextra -Wpedantic -O2`, with zero warnings. The archive uses its own labels internally: the three experiments are recorded as catch-up segments E1, E2, and E3 in their respective source files and READMEs, and the artifact and data directories use the short names e1, e2, and e3 — so exp9, exp10, and exp11 in this article's body map to the archive's E1, E2, and E3. Please don't measure the sibling articles with the wrong ruler either: [L02](./02-mmap-memory-mapping.md) uses lowercase e1 through e5, and from L03 onward the articles use uppercase E; those series and our archive's E1, E2, E3 each count separately and have nothing to do with each other.

## open(): the full story of flags, plus the umask cut

`open()` takes three parameters: a path, a blob of bitwise-OR'd flags, and mode in the third slot. mode only means anything when `O_CREAT` is present. The lowest two bits of flags are the access mode, and we pick one of three: `O_RDONLY` (0), `O_WRONLY` (1), `O_RDWR` (2). Their values literally are 0/1/2, so don't get clever and write `O_RDONLY|O_WRONLY`: the bitwise OR still comes out as `O_WRONLY`, and the kernel will read it as a write-only open. Beyond the access mode, the commonly used ones are these:

- `O_CREAT`: create if it does not exist; mode participates in setting the permissions (how umask cuts into it, coming right up)
- `O_EXCL`: used together with `O_CREAT`; fail if the file exists. The doesn't-exist-so-create versus exists-so-error decision is a single atomic step in the kernel — safe lock-file creation rests entirely on it, and it shields us from symlink ambushes as well
- `O_TRUNC`: truncate the regular file to zero length on open. The precondition is an access mode that allows writing; open it read-only and you can forget about this one
- `O_APPEND`: before every write, the kernel atomically moves the offset to the end of the file — the right way for multiple processes to append to one log; we expand on this in the fsync section
- `O_CLOEXEC`: automatically close this fd the instant an exec succeeds; see the warning block right below
- `O_DIRECT`: bypass the page cache and write straight through; comes with a pile of alignment requirements. Not expanded on in this article — knowing it exists is enough

::: warning O_CLOEXEC: on by default in new code
An fd without `O_CLOEXEC` is inherited as-is by child processes spawned via `fork()`. In multithreaded programs this is a disaster zone: in the gap between your `open` returning and your `fcntl` call to set `FD_CLOEXEC` after the fact, another thread happens to complete a `fork`+`exec`, and the new program starts up carrying an fd you never heard of — the classic fd leak. `O_CLOEXEC` has the kernel close it for us at the moment of exec, which drives the race window straight to zero. The author's habit is to include it unconditionally in library code; this article's examples close their fds when done, so it is occasionally omitted here.
:::

mode has a way of surprising people: you pass in 0666, yet `ls -l` shows 644 — where did the difference go? We ran it for real (the environment is WSL, g++ is 16.2.1, the kernel is 6.18). The program starts by calling `umask(022)`, then creates a file with mode 0666, reads back the actual permissions with `fstat`, and afterwards attempts a second open of the same file with `O_CREAT|O_EXCL`:

```text
$ ./exp2
umask=022, request=0666, real=0644
second open with O_EXCL: fd=-1, errno=17 (File exists)
```

Why? man 2 open says it plainly: with `O_CREAT`, the final permissions are computed as **mode & ~umask**. The umask is a per-process attribute; the 022 set by the shell's `umask` command cuts off the group-write and other-write bits, and that is how 0666 becomes 0644. You can think of it as a safety gate: even if the program slips and passes 0666, the process has no authority to create a file writable by everyone. If you truly want 0666, your program has to call `umask(0)` itself — but please think it through before you do. As for the second line of output, errno 17 is exactly `EEXIST`. What we tested here is a single process opening the same file twice: the file is sitting right there, and `O_CREAT|O_EXCL` bounced it back on the spot. Switch to two processes racing to create the same lock file and there is only one winner; the loser gets this very `EEXIST`. The check-not-exists-then-create step is a single atomic operation in the kernel — nobody can cut in line.

## read/write: partial reads and writes are the norm

That strace run just now asked for 64 and got only 13 — not an accident. man 2 read puts it bluntly: **reading fewer bytes than requested is not an error**. It may be because you are near end of file, or because the pipe or terminal on the other side simply has no more data for now. We hand a 100-byte file to a 16-byte buffer in a `read` loop, printing one line per round for the return value (rounds 0 through 4 all say `got 16`, so we skip them):

```text
$ ./exp3
round 5: want 16, got 16
round 6: want 16, got 4
round 7: want 16, got 0
```

Round 6 asks for 16, but only 4 remain readable. Round 7 returns 0 — that is EOF, the stream is exhausted. `write` works the same way: the return value is the number of bytes actually written, which may be fewer than requested. What lands in our hands is really a single rule: read/write calls must loop. One successful call does not mean the job is done; the `write_all` later on is precisely that loop packaged up. One more reminder: don't rush to hand these fds to epoll. Regular files cannot enter epoll — `epoll_ctl` will hand you `EPERM` outright, and the exact wording is preserved in the man 2 epoll_ctl entry among the reference resources. Regular files are always ready; multiplexing was always the world of sockets and pipes. For how epoll itself works, see [the epoll article in the networking volume](../../../networking/02-epoll-io-multiplexing.md).

## Error handling: three public tools, picked up as-is

When a syscall fails, it returns -1 and leaves all the details in errno. Since every call can fail and every failure means checking errno, the series has already folded this work into three public tools: `errno_code` and `sys_call` are defined in [Error-Handling Paradigms](../../thinking/02-error-paradigm.md), and `unique_fd` in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md). Learning the definitions once is enough; from this article on we use them as ready-made vocabulary.

One sentence to refresh the usage: when `sys_call("open", ::open, path, flags, mode)` fails, it throws `std::system_error`, with errno boxed into `generic_category` (POSIX errno happens to be exactly the value domain of `std::errc`), and the `what` string becomes the exception message's prefix — catch it, and you know at a glance which step blew up. One thing to note: read returning 0 (EOF) is not -1, and `sys_call` deliberately lets it pass through untouched, leaving EOF for the caller to judge. That is by design.

For settings that avoid exceptions, the same boxing has a non-throwing form, and we again act on the first line of the failure branch:

```cpp
// 非抛路径的用法示意:裸调用的失败分支里,errno 就地装箱,不惊动异常
int fd = ::open(path, O_RDONLY);
if (fd == -1) {
    std::error_code ec = errno_code();   // 此刻定格,后面再发生什么都不影响 ec
}
```

This is where `errno_code` earns its keep for us. The system_error thrown by `sys_call` carries an error_code using the same boxing, and when the tooling layer wants to pass errors via `std::expected`, what travels down the chain is still this same type.

One more character deserves a dedicated mention: EINTR. Picture us waiting on a blocking read when a signal arrives; the kernel wakes our process to run the handler, the read can no longer keep waiting, so it hands back -1, and errno now holds EINTR. Nothing is actually wrong with the device — when we run into it, simply restarting the call from the top works. The full version of `sys_call` builds this retry inside: if errno is EINTR, it retries the round; only the other failures get boxed and thrown. For how it cross-examines SA_RESTART's kernel-level restart, and what the chain of evidence looks like in strace, the E2 experiment in [Error-Handling Paradigms](../../thinking/02-error-paradigm.md) stages the complete show — we won't re-run it here. You only need to remember one thing: for calls that pass through `sys_call`, being interrupted by a signal is no longer your problem.

## unique_fd: writing close into the destructor

If you have written any programs, you have probably tasted the trouble with a bare `int fd`: the function has three early returns, `close` sits on the fourth path, and the no-leak guarantee rests entirely on human review. You already hold the solution: the move-only `unique_fd` from [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md) writes `close` into the destructor, so from then on the leak question depends not on memory but on the type. You already walked line by line through the full class there — why copy is deleted entirely, the release/reset/swap release semantics — so we won't copy the code again here; we'll just re-check one detail that is easy to misread.

The explicit in `explicit operator bool` blocks exactly the opposite of what many people assume. Contextual conversions like `if (fd)` are not blocked at all: contextual conversion leaves the door open for explicit by design, and `if (fd)` compiles just fine. What it actually stops is the implicit conversion that uses the object as a value — for `bool ok = fd`, GCC immediately answers cannot convert to bool in initialization, and `fd == 0` stops compiling as well. Remove that explicit, and both lines quietly compile. The RAII article captured the exact error text for us; flip back and you can match it.

Talking about what accidents unique_fd prevents is no fun; let's deliberately leak one for you to watch. The program starts by walking `/proc/self/fd` to print the existing fds, uses `setrlimit` to squeeze RLIMIT_NOFILE down to 8, then loops `open` on the same file — pointedly without close:

```text
$ ./exp5
fds at start: 0 1 2 3 5 10
leaked fd 3
leaked fd 4
leaked fd 6
leaked fd 7
open() failed after 4 leaked fds: errno=24 (Too many open files)
```

These few lines carry a lot of information; let's go through them one by one. In the opening list, 3 is the slot that `opendir("/proc/self/fd")` occupies for itself; `closedir` releases it once the listing is done, so the first leak reclaims 3 — fd allocation always takes the lowest free slot. 5 and 10 are inherited fds that the WSL startup chain stuffed into the process; we never opened them, and `/proc/self/fd` lists them all the same. So your fd table may well contain things somebody else put there. From now on, when troubleshooting fd issues, start from this directory.

Then 4, 6, and 7 leak one after another (5 is occupied, so it is skipped), and the fifth open runs into RLIMIT_NOFILE=8. errno 24 is exactly `EMFILE`. For the real-world quota, `ulimit -n` tells you: the author's WSL measured 1048576 (taken 2026-10-02; the RAII article's experiment read the same number from getrlimit), while older distributions commonly show 1024. A long-running service that leaks one fd per request eats through its quota exactly this way, bit by bit. And a leaked fd is only released when the process exits — nothing gives it back along the way.

## dup/dup2/dup3: this is where shell redirection comes from

To see the fd table clearly, we need its two-level structure: one level is the process-private fd table, whose every entry points to a system-level **open file description** — the offset and the status flags both live on the latter. Two `open` calls yield two independent descriptions, each with its own offset. `dup` and `fork` duplicate the fd table entry, so the two fds point at the same description and share the offset. After a fork, parent and child keep writing from each other's offset — and that is where the subtlety lies; we leave the details for the process article.

What `dup2(fd, 1)` does is more direct; let's read the code: it atomically swaps slot 1 of the fd table to point at fd's file, and from then on every `write(1, ...)` and `printf` goes into the file. The shell's `> file` is exactly that moment after fork completes but before exec runs (an excerpt from exp4_dup2.cpp; we omit the includes and the main shell):

```cpp
int fd = open("/tmp/sysprog-linux01/out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
int saved_stdout = dup(1);     // 留一条回终端的路
dup2(fd, 1);                   // 1 号槽位原子地换成文件
std::printf("this line goes into out.txt\n");
std::fflush(stdout);
dup2(saved_stdout, 1);         // 换回终端
```

```text
$ ./exp4
back to terminal
$ cat /tmp/sysprog-linux01/out.txt
this line goes into out.txt
```

We also staged an exp10 that walks the whole dup family one by one, and along the way caught two bystander discoveries. One concerns where stdio buffering lands: during redirection we printf'd a line without fflush, so it kept lying in the user-space buffer until dup2 swapped fd 1 back to the terminal, at which point later output dragged it out — its landing spot already the terminal. So you see, where a flush lands follows fd 1 at the moment of the flush, not the moment printf was called. The other discovery concerns the price of atomicity: if you close(1) and then dup, then during the gap between close and dup, any open in the process (in a multithreaded program, quite possibly another thread's doing) grabs slot 1 — in our measurement the next open indeed got 1, and to recover we had to close and dup again; the whole back-and-forth is nothing but race window. dup2 merges the two steps into a single system call, and the window closes.

One more cold fact that is easy to miss: a new fd produced by dup does **not** carry `FD_CLOEXEC`, even if the original fd had it. man 2 dup states it explicitly — close-on-exec is not shared. If you plan to take it across an exec boundary, this flag needs a fresh check.

There are two remedies. One patches after the copy: using fcntl(2)'s F_SETFD command, we re-set FD_CLOEXEC on the new fd — this is the supplement we worked out ourselves. The other is dup3(2), which man 2 dup points out to us in plain words; it originated on Linux and has now made it into POSIX.1-2024. We let exp10's e segment set the two brothers side by side:

```text
$ ./exp10(节选:e 段)
dup2(fd,10)  F_GETFD=0  dup3(fd,11,O_CLOEXEC) F_GETFD=1
dup2(fd,fd)  = 3(检查后发现是自己,直接返回,no-op)
dup3(fd,fd,0)= -1, errno=22(EINVAL)  <- dup3 明确拒绝
```

For the differences between dup3 and dup2, remember two. First, the flags parameter can carry `O_CLOEXEC`, so the copy is born with it — the F_GETFD=1 on fd 11 in the first line is its doing. The other spot, easily surprising, is the case where oldfd and newfd are the same: dup2 notices it is the same fd and harmlessly returns, while dup3 hands you back an EINVAL, telling you that writing it this way is not allowed.

Now for exp10's main event: the life and death of close-on-exec, verified by hand with fork plus exec. We prepare three fds in one go. Number 3 comes from a bare open carrying no flags at all. Number 4 is also bare-opened, and we afterwards patch FD_CLOEXEC onto it with F_SETFD. Number 20 is copied from 4 using F_DUPFD — fcntl's duplication command, equivalent to dup, which can also specify a lower bound for the copy; 20 exists precisely for the lower bound. Then we fork a child; the child lists `/proc/self/fd`, and re-execs itself with execl, passing the three fd numbers to the new program through argv:

```text
$ ./exp10(节选:f 段)
[child exec 前] /proc/self/fd: 0 1 2 3 4 20(pid=320407)

[child exec 后] /proc/self/fd: 0 1 2 3 20(pid=320407)
(exec 前是 0 1 2 3 4 20;对照上面,4 没了、3 和 20 还在)
read(3, ...)          = 10:"0123456789"  <- 裸 fd 活过了 execve
fcntl(4, F_GETFD)     = -1, errno=9(EBADF)  <- 带 CLOEXEC 的 fd,exec 前被内核关了
read(20, ...)          = 10:"0123456789"  <- 它复制自带 CLOEXEC 的 4,但 fd 标志不随 dup 走,又活了
```

The same batch of fd numbers meets three different fates; let's take them one by one. Number 3 is bare and survives execve uneventfully, still reading 10 bytes after the exec. Number 4 carries FD_CLOEXEC; when we probe with F_GETFD after the exec, we get EBADF — it no longer exists in the new program. The most interesting one is number 20: it was copied from 4, which carries CLOEXEC, yet it lives on happily. Why? The cold fact from the dup section explains exactly this: fd flags live on the fd table entry, and FD_CLOEXEC does not travel with the entry-copying process.

So who closed number 4? We call on strace to watch the whole thing, and what we see may not match what you expect:

```text
320037 fcntl(4, F_SETFD, FD_CLOEXEC)    = 0
320037 fcntl(4, F_DUPFD, 20)            = 20
320038 execve("/proc/self/exe", ["e2_dup(child)", "--child", "3", "4", "20"], 0x7ffe9768ea08 /* 72 vars */) = 0
320038 openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 4
320038 close(4)                         = 0
```

Between fcntl and execve, there is not a single close(4) to be found. The closing is not a separate system call — it happens inside execve's own kernel path: before exec swaps in the new program image, the kernel sweeps the entire fd table and clears every entry carrying FD_CLOEXEC in place. The two lines right after offer a casual observation: the first thing the dynamic linker does upon starting is an openat on ld.so.cache, it gets exactly the freshly vacated slot 4, and closes it right after use. So if you see a number 4 after an exec, don't rush to declare that it survived the exec — it may be one the new program opened itself. To verify an fd's fate, we don't bet on the fd number; we read from it or F_GETFD it as above, and let the behavior give the answer. By the way, the execve line's argv[0] in the trace reads e2_dup(child) — that is the binary name the archive gave exp10, again following the archive's own e2 short name; don't mistake it for L02's lowercase e series.

## fcntl: two kinds of flags, living on two levels

The previous section kept using F_SETFD and F_DUPFD; now let's get a clear look at fcntl(2) itself. It is a jack-of-all-trades interface that works by command word: we hand it an fd plus an F_-prefixed command, and the remaining arguments follow the command. The command list is long, but for now the two most common kinds are enough — and the boundary between the two lands exactly on fd's two-level structure.

One kind manages the **fd flags** — all one bit of them: FD_CLOEXEC, living in the process-private fd table entry; we read and write it with F_GETFD/F_SETFD. The other kind manages the **file status flags** — O_APPEND, O_NONBLOCK, O_ASYNC and friends — living on the system-level open file description, and the commands become F_GETFL/F_SETFL. Where a flag lives decides whether it travels with a copy; we let exp9's h segment compare the same fd against its duplicate:

```text
$ ./exp9(节选:h 段)
            f(原件)   g(F_DUPFD 复制件)
O_NONBLOCK  有      有      <- 状态标志在描述上,两个 fd 共享
FD_CLOEXEC  有      无      <- fd 标志在表项上,复制件不带走
```

Lay the comparison table next to the cold fact from the dup section, and the mechanism speaks for itself: dup and F_DUPFD copy the fd table entry, FD_CLOEXEC on the new entry is always cleared, and the old and new entries point at the same open file description — so the status flags are shared on both sides, while close-on-exec stays only on the original. One step further ahead: flock locks hang on the open file description, and fcntl record locks hang on the process; these ownership differences also fall out of the two-level structure, and we expand on them in the [file locks](./05-file-lock.md) article.

Looking at the duplication side, fcntl has its own duplication commands; exp9's b segment lays the three paths side by side for us:

```text
$ ./exp9(节选:b 段)
dup(fd=3)               = 4   (最低空闲)
fcntl(fd, F_DUPFD, 20)       = 20   (>=20 的最低空闲)
fcntl(fd, F_DUPFD_CLOEXEC,20)= 21   (一步到位)
```

dup always takes the lowest free slot; F_DUPFD lets us specify a lower bound — the number 20 in exp10 came through it — and F_DUPFD_CLOEXEC takes CLOEXEC along as well, finishing the copy-plus-flag-patch job in a single call.

The return value of F_GETFL hides one fixed skew; see exp9's c segment:

```text
$ ./exp9(节选:c 段)
F_GETFL(O_RDWR open)   raw=0100002  accmode=O_RDWR
F_SETFL 加 O_APPEND 后 raw=0102002  accmode=O_RDWR |O_APPEND
```

raw is what F_GETFL returns verbatim, in octal. If you test it for equality against the flags you passed to open, you are wrong every time: raw always carries one extra bit, 0100000 — the O_LARGEFILE bit the kernel brings back, marking the 64-bit off_t style of the open. On the glibc side the macro is named __O_LARGEFILE, defined to 0 on 64-bit platforms, so user-space O_LARGEFILE exists in name only; hence the kernel's answer has one extra bit that never appears in our arguments. So don't begrudge the effort when checking the access mode: mask it out with `fl & O_ACCMODE` and compare that, and always test individual flag bits with a bitwise AND.

F_SETFL's temperament deserves an even firmer place in memory: it does a **wholesale overwrite**, not a bitwise OR. If you want to add O_NONBLOCK and you fire off a bare `F_SETFL(fd, O_NONBLOCK)`, the existing O_APPEND gets wiped out — exp9's d segment stages exactly this accident for us:

```text
$ ./exp9(节选:d 段)
F_SETFL(O_NONBLOCK) 后 raw=0104002  accmode=O_RDWR |O_NONBLOCK
O_APPEND 没了——正确姿势是 F_GETFL 读出来、按位或、再 F_SETFL:
补救后              raw=0106002  accmode=O_RDWR |O_APPEND |O_NONBLOCK
```

The correct posture is written in the output's second line: read F_GETFL out, OR in the new flag, write it back with F_SETFL. One more boundary you should know: F_SETFL cannot change the access mode. man 2 fcntl states it clearly — after open, the access mode is fixed, and F_SETFL only recognizes the status bits. The interesting part is here: push O_RDWR onto a read-only fd, and the call actually returns 0, succeeding without a word, yet write still eats an EBADF:

```text
$ ./exp9(节选:e 段)
fcntl(rd, F_SETFL, O_RDWR|O_NONBLOCK) = 0(居然成功)
再看 F_GETFL         raw=0104000  accmode=O_RDONLY |O_NONBLOCK
write(rd,...) = -1, errno=9(EBADF)  <- 访问模式纹丝不动,白塞
```

So when is F_SETFL useful day to day? It is the only way to change O_APPEND or O_NONBLOCK after open, and in quite a few situations we specifically cannot re-open: a re-open yields a new open file description whose offset starts from scratch, with no recognition between it and the original fd. exp9's f segment puts the two paths side by side:

```text
$ ./exp9(节选:f 段)
fd   当前偏移 lseek(fd,0,SEEK_CUR)   = 10
fd2  重新 open 的偏移 = 0  <- 各是各的描述,和 fd 的 10 互不相认
     (fd2 带着 O_APPEND 出生,lseek 看到的偏移要到 write 那刻才被内核挪到文件尾)
fd   F_SETFL 摘掉 O_APPEND 后偏移      = 10  <- 还是那个描述,偏移没动
```

Mutating flags on the same description leaves the offset exactly where it was — that is why you change flags instead of re-opening. For O_NONBLOCK's actual effect, exp9's g segment verified it on the spot with a FIFO. A FIFO is a named pipe; we make one with mkfifo, it is born blocking, and a read with no data waits there forever. Once F_SETFL adds O_NONBLOCK, the very same read returns -1 immediately, with errno set to EAGAIN (on Linux it has the same value as EWOULDBLOCK) — no more waiting. Trace the non-blocking style of socket programming back to its source, and this is it.

> Looking back at the advice from the beginning of this article: bring `O_CLOEXEC` along at open time. Patching the flag with F_SETFD splits the job into two steps, while opening with `O_CLOEXEC` has it from birth — and it is precisely this that closes the fork+exec race window in multithreaded programs.

## lseek/pread/pwrite: the offset is state on the kernel's side

The offset does not live in your code; it lives in the kernel's open file description: read/write calls advance it, `lseek` calls change it. Concurrent reads from multiple threads sharing one fd amount to sharing an invisible cursor that the others keep moving. `pread`/`pwrite` turn the offset into a call parameter: one call is equivalent to an atomic lseek+read, and the cursor stays put afterwards. We test on a file whose content is "0123456789": first we `pread(fd, buf, 4, 2)`, reading 4 bytes starting at offset 2; then we query the fd's current offset with `lseek(fd, 0, SEEK_CUR)`, and follow with a plain `read(fd, buf, 4)` as the control:

```text
$ ./exp8
pread(4, off=2) -> "2345", fd offset now 0
read(4)         -> "0123", fd offset now 4
```

`pread` fetched "2345" from offset 2, and the fd's offset did not budge — it stays at 0. It is the plain `read` right after that starts from 0. This is exactly what makes pread the most worry-free posture when multiple threads concurrently read the same file: no locking hassle, and no stepping on anyone else's cursor. Linux does have one small quirk here: on an fd opened with `O_APPEND`, `pwrite` ignores the passed-in offset and appends anyway. man 2 pread says it outright — this does not conform to POSIX's requirements but is Linux's own historical behavior.

Inside lseek's whence parameter hides a pair of characters dedicated to serving sparse files: `SEEK_DATA` and `SEEK_HOLE`. The whence argument says where the new offset counts from, and its possible values are the whole SEEK_-prefixed family. A sparse file is a file that is large in size while big stretches of its middle are holes — not one byte of a hole is ever written to disk, and reading one yields all zeros. We make one the laziest way possible: use ftruncate (the call that cuts a file straight to a given length) to pull its size up to 1 MiB, then pwrite a little data at three chosen positions, leaving everything in between untouched. One stat and the truth is out; see exp11's a segment:

```text
$ ./exp11(节选:a 段)
st_size     = 1048576 字节(1 MiB)
st_blocks   = 24 × 512 = 12288 字节  <- 磁盘实占,st_size 只是「账面」
```

st_blocks is the field of the stat structure that records actual disk usage, counted in 512-byte blocks. Look: st_size reports 1 MiB, while actual disk usage is a mere 12 KiB — 24 blocks of 512 bytes, spread over the three data stretches at 8 blocks each, and 8 blocks times 512 bytes is exactly one 4 KiB filesystem block apiece. SEEK_DATA finds the next stretch of data counting forward from a given position; SEEK_HOLE finds the next hole. Starting from 0, we use DATA to find the start of each data stretch and HOLE to find its end, all the way to the end of the file — and just like that we have mapped out where the file is holes and where it is data:

```text
$ ./exp11(节选:b 段)
hole  [      0,    4096)  长度    4096
data  [   4096,    8192)  长度    4096
hole  [   8192,   69632)  长度   61440
data  [  69632,   73728)  长度    4096
hole  [  73728, 1040384)  长度  966656
data  [1040384, 1044480)  长度    4096
hole  [1044480, 1048576)  长度    4096(尾部全洞,SEEK_DATA 报 ENXIO)
洞里 pread(2048, 8) = 8 字节: 00 00 00 00 00 00 00 00  <- 零页,内核现造的,不占磁盘
```

At the end of the output we also pread 8 bytes from inside a hole: the all-zero bytes we got back never touch the disk — they are zero pages the kernel conjures up on the spot, taking no disk space either. Knowing where the holes are, what is that good for? Hole-aware copying in tools like cp and tar relies on exactly this pair of whence values: data stretches get copied as usual, while holes are simply seeked past, so the copy does not fill the holes with solid zeros and waste disk for nothing. Two more boundaries to remember. One is precision: the detection granularity is the filesystem block — 4 KiB on our ext4 here. The first stretch's pwrite wrote only 8 bytes, yet the reported data stretch is a full 4096; a hole smaller than one block simply does not exist to this probing. When you make your own sparse file for demos, the holes must be at least one block in size, or you won't measure what you meant to. The other is failure: when nothing is found, the error is ENXIO — an all-hole file is one case, a starting point at end of file is the other, and both hand us the same errno:

```text
$ ./exp11(节选:c 段)
全洞文件 lseek(0, SEEK_DATA)      = -1, errno=6(ENXIO)
从文件尾 lseek(1048576, SEEK_DATA)  = -1, errno=6(ENXIO)
```

Three small matters remain to be settled together. First, the offset boundaries: SEEK_CUR and SEEK_END both accept negative offsets — we tried `lseek(-4, SEEK_END)`, it landed on 6, and the read returned "6789"; we tried `lseek(-1, SEEK_CUR)` next, the cursor stepped back one, and the same bytes could be read again. SEEK_SET's result, though, must be a non-negative number; negatives are rejected outright.

Second, seeking past end of file is legal too: `lseek(+100, SEEK_END)` returned 110 for us, while st_size was still 10 — we had positioned ourselves inside a hole whose bytes were not on disk yet; the file grows only at the next write, and this is precisely another way to make a sparse file. Third comes the pipe's turn: SET, CUR, END, DATA, and HOLE — all five whence values go down together, uniformly with ESPIPE (errno 29, illegal seek): pipes are streaming, there is no notion of position inside them, and anything that wants positioning goes back to regular files.

## Page cache and fsync: write returning does not mean the data is on disk

A successful return from `write()` only guarantees that the data made it into the **page cache**; the kernel writes it back at a time of its own choosing. man 2 write is unambiguous: **a successful write provides no guarantee that the data has reached disk, and the only way to confirm is to call fsync(2)**. The three functions that actually push data to disk divide the labor. `fsync` pushes data plus metadata and blocks until the device reports the transfer complete. `fdatasync` pushes only the part that affects subsequent reads — mtime and the like can be skipped, but a change in file size must go. `sync` is whole-disk level; POSIX allows it to return once the request is queued, while Linux's implementation waits for the I/O to complete. And actually writing data to disk has a unit price, which we measured for you. Two runs each perform 200 `write(4KiB)` calls; run B adds one `fsync` per write. The data file must sit on real-disk ext4 — /tmp is tmpfs, and putting it there wastes the test:

```text
$ ./exp6
A: 200 x write(4KiB)         :     0.8 ms
B: 200 x (write(4KiB)+fsync) :   229.7 ms
```

Run A's 0.8 ms averages out to 4 μs per call — the price of a pure memory copy. Run B adds one fsync per write at roughly 1.15 ms per call — **nearly 300 times slower**. What you are looking at is the unit price of guaranteeing every single write to be on disk (environment: WSL2's ext4-on-VHDX, compiled with g++ -O2; the numbers jitter from run to run, so read the magnitudes).

As for going deeper, we stop here: how long dirty pages linger in the Dirty counter, whether data is really lost when the process dies, the metadata price gap between fsync and fdatasync, and the respective timings of O_SYNC and O_DIRECT — these are the main subject of an entire other article, [Page Cache and Persistence](./03-page-cache.md), whose six experiments are devoted to exactly this. For this article, holding up a few essentials is enough: write hands off to the page cache and calls it done; if you want guarantees, call fsync; and for a newly created file, follow up with an fsync on the directory as well.

::: warning The crash window
Between `write()` returning and the kernel actually flushing the dirty pages lies what we call the crash window: lose power or hit a panic during it, and the data is gone. After the filesystem journal replays, what you will see is a valid but stale file. If you want a data-on-disk guarantee, call `fsync()`. And to make the creation of a new file itself reliable, we must also fsync the **directory**'s fd once more — otherwise the file may vanish entirely after a power loss.
:::

Let's settle O_APPEND's benefit here: when multiple processes write the same log, why does everyone use it? Because the kernel fuses moving the offset to the end of the file and completing the write into a **single atomic step** (man 2 open's wording), so concurrent appends never overwrite each other. Doing our own lseek to the end and then writing is two steps: both processes seek to the same spot and stomp on each other. Note also that it guarantees atomicity only for the append step — a single write may still be partial, which is why a logging library should batch one log line into a single write before sending it out.

## Wrapping up: assembling a minimal file tool with unique_fd and sys_call

The foundation is laid; time to assemble: `write_all` is the answer to partial writes, `read_to_string` is the answer to looping until EOF, and fds are handed to `unique_fd` across the board. Below is the second half of exp7_final.cpp; its first half holds nothing new — it carries over the three public tools defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md) and [Error-Handling Paradigms](../../thinking/02-error-paradigm.md) unchanged:

```cpp
void write_all(int fd, std::string_view data)
{
    while (!data.empty()) {
        ssize_t n = sys_call("write", ::write, fd, data.data(), data.size());
        data.remove_prefix(static_cast<std::size_t>(n));
    }
}

std::string read_to_string(const char* path)
{
    unique_fd fd{sys_call("open", ::open, path, O_RDONLY)};
    std::string out;
    char buf[4096];
    for (;;) {
        ssize_t n = sys_call("read", ::read, fd.get(), buf, sizeof(buf));
        if (n == 0) {
            break;  // EOF 不是错误,sys_call 只拦 -1
        }
        out.append(buf, static_cast<std::size_t>(n));
    }
    return out;
}

int main()
{
    const char kPath[] = "/tmp/sysprog-linux01/note.txt";

    {
        unique_fd fd{sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
        write_all(fd.get(), "fd born, write, auto-close.\n");
    }  // 离开作用域,~unique_fd() 里 close(fd_)

    std::printf("read back: %s", read_to_string(kPath).c_str());

    try {
        unique_fd bad{sys_call("open", ::open, "/tmp/sysprog-linux01/no_such_file", O_RDONLY)};
    } catch (const std::system_error& e) {
        std::printf("caught: %s (errno=%d)\n", e.what(), e.code().value());
    }
}
```

```text
$ g++ -std=c++23 -O2 -Wall -Wextra exp7_final.cpp -o exp7 && ./exp7
read back: fd born, write, auto-close.
caught: open: No such file or directory (errno=2)
```

Here the error path and the happy path converge on a single exit: look at the caught line — the `open:` prefix at the head of the message is exactly `sys_call`'s first argument, and which step failed is already marked on the screen for us. With this, the Linux-side foundation is complete. The two thinking-foundation articles supplied the definitions and design of `unique_fd`, `sys_call`, and `errno_code`; this article took them through their first round of real combat, and you will keep seeing them on stage in the sibling articles to come.

## How the other side sees it

The Windows side has no fd-as-small-integer; `CreateFileW` returns a HANDLE — an opaque index into the kernel object table. Conceptually you can treat HANDLE as the Windows edition of the fd: stdin/stdout/stderr still have reserved default handles, and shell redirection likewise works by having the child process inherit a swapped handle.

There are a few more mirrored differences to grow familiar with, one by one. errno becomes GetLastError, likewise thread-local — when debugging across camps, don't get the two systems mixed up. read/write become ReadFile/WriteFile: a ReadFile on a synchronous file handle either fills the number of bytes we requested or stops at the EOF position; short reads do not happen on regular files. Practically speaking, a loop like `write_all` is nearly useless for regular files on Windows synchronous file handles, whereas on the POSIX side, without that loop, a single write might send only half the data and quietly drop the rest.

The inheritance semantics are exactly inverted: a Unix fd is inherited by child processes by default, and if we don't want it crossing an exec, we must explicitly mark it with `O_CLOEXEC`. Windows handles are inherited not at all by default — every handle we want to pass down must be marked explicitly, one by one. The remaining details wait for us in the mirror article; see [Win32 File I/O: Handles, CreateFileW, and Synchronous Reads/Writes](../../windows/file-io/01-win32-file-io.md).

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="open(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/open.2.html"
  />
  <ReferenceItem
    :id="2"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="3"
    title="write(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/write.2.html"
  />
  <ReferenceItem
    :id="4"
    title="dup(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/dup.2.html"
  />
  <ReferenceItem
    :id="5"
    title="pread(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/pread.2.html"
  />
  <ReferenceItem
    :id="6"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="7"
    title="lseek(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/lseek.2.html"
  />
  <ReferenceItem
    :id="8"
    title="fsync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fsync.2.html"
  />
  <ReferenceItem
    :id="9"
    title="sync(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sync.2.html"
  />
  <ReferenceItem
    :id="10"
    title="epoll_ctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/epoll_ctl.2.html"
  />
  <ReferenceItem
    :id="11"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="12"
    title="std::system_error"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/system_error"
  />
</ReferenceCard>
