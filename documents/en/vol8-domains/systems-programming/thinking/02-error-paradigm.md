---
title: "The Error-Handling Paradigm: From errno to expected"
description: "A failed syscall hands back only a -1; the details stay in the thread-local slots errno and GetLastError. This article measures the read window of those slots (Linux: 11 inserted operations with 3 pollutions; Windows: 29 successful calls each run past a sentinel, catching the three behaviors of clearing, noise-leaving, and preserving), defines the series-wide errno_code/sys_call/last_error_code, and freezes the slot value into an error_code on the first line of every failure branch; EINTR is retried inside sys_call, with a strace evidence chain cross-checked against SA_RESTART's kernel restart; everything then unifies into the two exits of expected at the toolkit layer and system_error at the application top, with an assembly cross-check drawing the boundary where zero overhead holds"
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 28
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
related:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "error_code：错误码体系与自定义 category"
  - "expected：值或错误，C++23 的错误处理新范式"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - expected
  - 工程实践
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/thinking/02-error-paradigm.md
  source_hash: 17ec9560c566f6ac8e18d64cb809a4656510bcac4764170bd8b55a618aaa1b7a
  translated_at: '2026-11-22T15:48:35+00:00'
  engine: glm-5.3
  token_count: 26201
---

# The Error-Handling Paradigm: From errno to expected

In the previous article, [The RAII Paradigm for OS Resources](01-raii-paradigm.md), a single move-only RAII skeleton brought fds, handles, and memory mappings under control — release written into the destructor, and resources stopped leaking. But how the failure itself gets reported — not a word yet. Call `::open()` offhand: if it fails, what comes back is only a -1 — who failed and why cannot be found in the return value at all. Where do the details stay? Linux puts them in errno, Windows in GetLastError's return value, and the two residences share a trait: each is a per-thread slot, faithful only for the short while after the failure happens.

This article handles only this slot's affairs. Where it lives, who can modify it, when reading it counts, and how the reading becomes a value safely transmissible in C++ — we proceed in exactly that order. What lands at the end are three public tools used across the series: `errno_code` and `sys_call` on the Linux side, `last_error_code` on the Windows side, plus one two-exit convention: the toolkit layer passes errors via expected, and the application's top layer upgrades errors into system_error. Experiments on both sides are measured. Linux ran on WSL2: kernel 6.18.33.2, g++ 16.2.1, glibc 2.44, strace 7.2. Windows ran on Win11 26200 with MSYS2 UCRT64's g++ 16.1.0. All code and raw output live in `code/volumn_codes/vol8/systems-programming/` under `thinking/02-error-paradigm/` on both sides, reproduction commands in each README, ready for your reconciliation at any time. Experiment numbering, stated up front: Linux experiments are numbered E1 through E4, following the archive directories, and they appear in the order E1, E4, E2, E3. Windows experiments use lowercase e, continuing after the previous article's e1 and e1b, unrelated to the Linux E-series — the Windows e4 later in this article and the Linux E4 are two different experiments. The paired articles also get short names: "POSIX File I/O" on the Linux side is L01, "Win32 File I/O" on the Windows side is W01, and mirror articles all use this naming.

## Where errno Lives: A Slot per Thread

We open on the Linux side. man 3 errno's DESCRIPTION has a direct sentence: errno is thread-local — one thread setting it does not affect the value in other threads. In glibc's implementation, the errno macro finally expands into a dereference of a function's return value; every time we read or write errno, we go through `__errno_location()` to obtain the int belonging to this thread. So multiple threads failing separately never scribble over each other's error codes.

The same passage carries a weightier sentence: the value of errno is meaningful only when the call's return value indicates an error. Success or failure is always judged by the return value; errno is a detail consulted after the failure. Read the sentence backwards and it also has flavor: after a successful call, errno may hold a stale code left by some earlier failure, and if you judge success by errno, the number you read has nothing to do with this call at all.

Since we read only after failure, we give the reading a fixed shape. Here is one of the series' Linux-side public tools; this article is its definition site, and later articles reference it without redefining:

```cpp
// 公共工具(系列沿用):失败后立刻调用,errno 的值在这一刻定格进 error_code
std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}
```

Defined, then used on the spot — this is E1, the errno-boxing and thread-locality experiment, source of the three output blocks below. Open a nonexistent path, then write to an already-closed fd; the first line of both failure branches calls `errno_code()` to complete the boxing. Copying the slot value, which may be rewritten at any instant, into error_code — an ordinary value object — is what this series calls boxing. The choice of `generic_category` has its reasons too: the POSIX errno numbering is exactly the source of the `std::errc` enum's value domain, so an error_code dressed in it gets `message()` output identical to `strerror()`'s sentence, and it compares directly against `std::errc` enumerators:

```cpp
int fd = ::open("/tmp/errpar/no_such_file_e1", O_RDONLY);
if (fd == -1) {
    std::error_code ec = errno_code();   // 失败分支的头一行
    std::printf("    ec.value() = %d   (ENOENT = %d)   equal = %s\n",
                ec.value(), ENOENT, ec.value() == ENOENT ? "yes" : "NO");
    std::printf("    ec == std::errc::no_such_file_or_directory : %s\n",
                ec == std::errc::no_such_file_or_directory ? "true" : "false");
}
```

```text
[1] open(no_such_file_e1) failed:
    ec.value() = 2   (ENOENT = 2)   equal = yes
    ec.message() = "No such file or directory"
    ec == std::errc::no_such_file_or_directory : true
[2] write to closed fd: return = -1 (expect -1)
    ec2.value() = 9   (EBADF = 9)   equal = yes
    ec2.message() = "Bad file descriptor"
```

You see it: the value matches, the text is readable, the errc comparison passes — exactly the foundation all later error handling needs. As for the thread-local sentence, E1's two-thread interleaving verifies it once more: thread A opens a nonexistent path, producing ENOENT and raising an atomic flag; thread B waits for A's flag, produces its own EBADF, and raises its flag too. Both sides wait until the other has finished failing before calling `errno_code()` to take their values. If errno were an ordinary global variable, what A saw at value time would necessarily be B's 9:

```cpp
std::atomic<bool> a_failed{false};
std::atomic<bool> b_failed{false};

std::thread ta([&] {
    int f = ::open("/tmp/errpar/no_such_file_A", O_RDONLY);  // 本线程 errno = ENOENT
    (void)f;
    a_failed.store(true, std::memory_order_release);
    while (!b_failed.load(std::memory_order_acquire)) { }   // 等 B 也失败完
    std::error_code ec = errno_code();                      // 交错之后才取值
    /* 打印 ec.value() */
});
std::thread tb([&] {
    while (!a_failed.load(std::memory_order_acquire)) { }   // 等 A 失败
    int f = ::open("/dev/null", O_WRONLY);
    ::close(f);
    ssize_t r = ::write(f, "x", 1);                         // 本线程 errno = EBADF
    (void)r;
    b_failed.store(true, std::memory_order_release);
    std::error_code ec = errno_code();
    /* 打印 ec.value() */
});
```

```text
[3] two threads, interleaved failures:
    [thread A] value = 2  (expect ENOENT=2) -> kept own value
    [thread B] value = 9  (expect EBADF=9) -> kept own value
```

We reran three rounds, and not once did the flavors cross. Thread locality guards against cross-thread bleed, but it cannot guard against the next call within the same thread — and that is exactly what the read window, coming next, is about.

## The Read Window: Every Step Between Failure and Reading the Code Can Rewrite It

A common textbook sentence says: between the failing call and reading errno, do not interpose other calls. We do not memorize sentences — we arrange the experiment (E4). Each scenario has three steps: produce a baseline failure, insert one operation, immediately read errno and compare; if the value changed, it counts as pollution. Two baselines were built, ENOENT=2 and EBADF=9 each run once, for a reason you will grasp at once: if the inserted operation changed errno to exactly the baseline value, a single baseline could not detect it. In the archived output, four probes (0/1/3/4) carry an extra stderr insertion-notice line ahead of them; we omit those and keep only the verdict lines:

```text
$ ./e4 enoent
baseline mode = enoent; probes = 11
[ 0] fprintf(stderr, ...)                 baseline= 2 after= 2  unchanged
[ 1] successful write(2, ...)             baseline= 2 after= 2  unchanged
[ 2] strerror(ENOMEM)                     baseline= 2 after= 2  unchanged
[ 3] printf to stdout (buffered)          baseline= 2 after= 2  unchanged
[ 4] std::cout << (iostreams)             baseline= 2 after= 2  unchanged
[ 5] malloc 1 MiB + free                  baseline= 2 after= 2  unchanged
[ 6] std::string 4 KiB (heap)             baseline= 2 after= 2  unchanged
[ 7] fopen("/tmp") + fclose               baseline= 2 after= 2  unchanged
[ 8] getaddrinfo("localhost") ok          baseline= 2 after= 6  POLLUTED
[ 9] SECOND FAILING open()                baseline= 2 after= 2  unchanged
[10] fprintf(stderr) with fd 2 CLOSED     baseline= 2 after= 9  POLLUTED
summary: 2 of 11 probes polluted errno in this run
```

The inserts numbered 0 through 7 — stderr printing, bare write, strerror, buffered printf, iostream, malloc, a heap string, fopen — are precisely the chores most easily smuggled onto our failure-handling paths, and in the glibc 2.44 measurements all eight left errno untouched. Rerun with the EBADF baseline and item 9 shows itself: 9 became 2, judged POLLUTED — a second failing call of course overwrites the slot; under the ENOENT baseline it merely disguised itself as unchanged. The genuine surprise is item 8, which we narrowed down on its own:

```text
numeric AI_NUMERICHOST       rc= 0 errno after=0
plain numeric (no flag)      rc= 0 errno after=0
name "localhost"             rc= 0 errno after=6
name again (cache warm)      rc= 0 errno after=6
```

`getaddrinfo` returned successfully, yet errno held 6 (ENXIO). Down the numeric-address path, errno stays 0. Down the name-resolution path, the pollution reproduces deterministically. We traced a rerun with strace: not one failed syscall appears in the trace, so this 6 never passed through the kernel — it comes from user-space code on the name-resolution path, assigned directly by one of glibc's NSS modules. NSS stands for Name Service Switch, the mechanism glibc uses to decide, per the layout of `/etc/nsswitch.conf`, whom to ask about a hostname. Which module assigns the value depends on your machine's nsswitch configuration, and we do not name names. A story circulating online attributes this to a missing `/etc/gai.conf` or an absent nscd; on the author's machine gai.conf is present and systemd-resolved answered successfully, and the pollution appeared anyway. So we accept only the measured ruling: the cast list differs across configurations — this is machine-dependent.

man 3 errno's NOTES settled this long ago; the original text says two things: successful functions are also permitted to modify errno, and no system call or library function ever sets errno to zero. The second half deserves a second look — on Linux there is simply no such thing as success clearing the slot, and reading a stale code is its chronic symptom. The rule that lands in our hands is one line: do not bet that a successful library call leaves errno untouched — POSIX's specification makes no such promise. Fixing the read on the first line of the failure branch is exactly the sentence written in `errno_code()`'s comment.

## The Windows Mirror: GetLastError's Three Shapes

We swing the camera to Windows. Failure values are worn per API, and the sentinels come in three kinds: INVALID_HANDLE_VALUE for dead handles, NULL for null pointers, FALSE for true-or-false. The full table of three-way checking we will lay out in the Windows file-I/O article (W01, agreed at the top); reconcile over there. The details likewise stay in a per-thread slot, read by GetLastError, and the official documentation's words are equally plain: error codes are maintained on a per-thread basis, and one thread does not overwrite another thread's value. The Return value section also admits frankly: most functions set the error code only on failure, but some set it on success. For functions not documented to set the error code, what it returns is merely the last value set, and some functions set the slot to 0 on success while others do not.

Since the documentation itself speaks this loosely, we invite the common Win32 calls in one by one and run them through: a fixed set of candidate probes tested in series — the testing trade calls this a test battery (a literal translation of the word), and we borrow the term. The sentinel method: `SetLastError(2)` fakes a CreateFileW failure having just left ERROR_FILE_NOT_FOUND; call exactly one target API, then read the slot. Reporting 2 means it did not touch it. Reporting 0 means the success path cleared it. Reporting anything else means the success path left its own noise. The battery laid out 30 lines — 29 successful calls plus one CreateFileW deliberately failing on an exclusive lock, mixed in as an in-line control. Two more failing calls supplement the control group (these measurements come from Win11 26200; behavior may drift after system upgrades — trust the battery output on rerun):

```text
== 成功的调用对 last-error 槽位的影响(哨兵=2)==
  GetProcessId(GetCurrentProcess)    -> err=2    保留
  CreateFileW(真成功,开空闲文件) -> err=0    清零!
  CreateFileW(撞独占锁,失败32) -> err=32   改成杂音!
  ReadFile(成功)                   -> err=2    保留
  GetComputerNameW(成功)           -> err=203  改成杂音!
  ... (其余 25 行:WriteFile、FindFirstFileW、RegOpenKeyExW、Sleep(0)、
       new/delete、fprintf(stderr) 等,全部保留)

== 失败的调用一定覆盖槽位(对照组)==
  GetProcessId(野句柄,失败)      -> err=6
  CreateFileW(又一个不存在,失败)  -> err=2
```

All three shapes were caught. The most typical sample of the clearing shape is CreateFileW itself: one step you fail to open a file, the next step you successfully open a different one, and the slot goes to zero — the error code is gone. A word on the mixed-in "hits exclusive lock, fails 32": it is a failing call deliberately placed in the success section, its 32 comes from that genuine failure, and it is not read by the other-value-means-success-noise interpretation. The 203 GetComputerNameW left behind is ERROR_ENVVAR_NOT_FOUND — the system did not find the environment variable it wanted — most likely a failure left by its internal probing of environment variables; the documentation does not write the cause, so we do not guess and only record the phenomenon. The control group makes it explicit: a failing call always overwrites the slot, replacing both value and meaning.

Put the two sides together and there is symmetry and asymmetry alike. Symmetric are the official rulings: man 3 errno says successful functions may modify it, Microsoft Learn's documentation says some set 0 on success and some do not — neither side promises that a successful call leaves the error code untouched. Asymmetric are the symptoms: Linux never clears, so you read a stale code; Windows has the clearing kind, so you read 0, which at first glance looks like no error happened. Neither reading gives the truth of this failure. The answer that lands in our hands is the same on both sides: take the value immediately, on the first line of the failure branch.

## Boxing Is Freezing: last_error_code and the Categories on Both Sides

What comes out of the slot is merely an int; to make its value safely transmissible, comparable, and text-producing, we box it into `std::error_code`. Inside an error_code are exactly two things: an int value plus a pointer to an error_category. The value is copied in — once boxed, no amount of slot thrashing can touch it. That is what we mean by boxing is freezing. The Windows-side boxing function, likewise defined exactly once for the whole series:

```cpp
// GetLastError 立即装箱:Win32 错误码挂 system_category
inline std::error_code last_error_code() noexcept
{
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}
```

Defined, then verified on the spot. CreateFileW opens a nonexistent file, boxed on the first line of the failure branch. Then we deliberately open a file certain to succeed — the clearing operation from the battery — and look back (the last three lines of [1] are two renderings of message, examined in the encoding section and not pasted here. The e2 in the heading's parentheses is the evidence-gathering name; the clearing behavior was measured by e2b's battery):

```text
[1] CreateFileW(不存在的文件)失败,立刻装箱
    返回值是不是 INVALID_HANDLE_VALUE:是
    ec.value()=2  ec.category().name()=system
[2] 装箱后,故意插一次会清零槽位的成功 CreateFileW(e2 实测它成功时置 0)
    此时 GetLastError()=0(成功调用已把槽位清零)
    但 ec.value() 仍是 2 —— 装箱即冻结,这就是"立即"的意义
```

The slot has already gone to zero, yet the `ec.value()` in our hand is still 2. All the perils we worried about earlier end at this line: as long as boxing happens on the first line of the failure branch, nothing that happens afterward touches the value in hand.

At the category layer, MinGW's libstdc++ has a surprise in store: `system_category().default_error_condition()` carries a built-in mapping from Win32 error codes to errno:

```text
  system(2) -> condition{value=2, category=generic, message="No such file or directory"}
  system(13) -> condition{value=22, category=generic, message="Invalid argument"}
  system(32) -> condition{value=16, category=generic, message="Resource device"}
  system(87) -> condition{value=22, category=generic, message="Invalid argument"}
```

The comparison step benefits directly: when an error_code compares against errc (essentially an error_condition), the path runs exactly through default_error_condition's mapping, so `ec == std::errc::no_such_file_or_directory` measured 1 on a Win32 error code — one body of comparison code that eats on both sides:

```text
    ec == error_code(2, system_category) : 1  (同 category 同值)
    ec == error_code(2, win32_category)  : 0  (值同,category 不同)
    ec == errc::no_such_file_or_directory : 1  (system_category 内置映射桥接)
```

Here we must sharpen our attention, because the comparison coming up depends on it: the same number does not name the same error in the two numbering systems. Win32's 13 is ERROR_INVALID_DATA, meaning invalid data; errno's 13 is EACCES, the one that actually means insufficient permission. We measured `error_code(13, system_category()) == errc::permission_denied` as 0 — the mapped 22 (EINVAL) is its counterpart in errno. So never force-compare bare numbers; compare within the same category or not at all. Two error_codes with the same value in different categories are never equal — the 0 on the second line above is the proof.

The encoding of `message()` is another matter to settle. MinGW's libstdc++ on Windows does have `system_category().message()` producing genuine Win32 text, but the bytes are in the ANSI code page — on the author's Chinese system, what we saw was GBK:

```text
  system(2) 原始字节 -> "ϵͳ�Ҳ���ָ�����ļ���"   hex: cf b5 cd b3 d5 d2 b2 bb b5 bd d6 b8 b6 a8 b5 c4 ce c4 bc fe a1 a3
  system(2) GBK转UTF8 -> "系统找不到指定的文件。"
```

> We also kept a mechanistic corroboration. Running objdump -p over libstdc++-6.dll's import table, we met the old acquaintance FormatMessageA in there, with GetLastError alongside. libstdc++ uses exactly the A-flavor FormatMessage, and the A flavor naturally produces text in the ANSI code page.

For reliable readable text we take the proper road of FormatMessageW paired with `WideCharToMultiByte(CP_UTF8)` — that is how W01's `win32_text` is written. To make `message()`'s output directly readable itself, we supply our own category: message goes through the W flavor, and the comparison bridge delegates to system_category:

```cpp
class win32_category_t : public std::error_category
{
public:
    const char* name() const noexcept override { return "win32"; }
    std::string message(int ev) const override
    {
        wchar_t* buf = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, (DWORD)ev, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
        std::wstring ws = buf ? buf : L"(unknown win32 error)";
        LocalFree(buf);
        // ...去掉尾部 \r\n,再做与 win32_text 相同的两步转换,产出 UTF-8
        return to_utf8(ws);
    }
    // 判等桥接:委托给 system_category,沿用它内置的 win32->errno 映射
    std::error_condition default_error_condition(int ev) const noexcept override
    {
        return std::system_category().default_error_condition(ev);
    }
};
inline const win32_category_t& win32_category()
{
    static win32_category_t c;
    return c;
}
```

With this, `message()` reads directly as UTF-8, the comparison ability is not lost, and `error_code(2, win32_category()) == errc::no_such_file_or_directory` likewise measured 1. At this point the errors of both sides have become the same kind of thing — an error_code — and the road above merges into one.

## sys_call: Checking, Boxing, and EINTR Retry, Taken Away Together

You have probably written the loose form of error reporting too: every syscall call followed by an `if (r == -1)`. We gather the checking and the boxing into one template — the series' second Linux-side public tool:

```cpp
int g_eintr_retries = 0;  // 实验计数器:sys_call 在内部消化掉的重试次数

// 公共工具(系列沿用):任何"返回 -1 表失败"的 syscall 都从这儿过。
// EINTR 不算错,从头再来;其余 errno 装箱抛 system_error
template <class F, class... Args>
auto sys_call(const char* what, F&& f, Args&&... args)
{
    for (;;) {
        auto r = std::forward<F>(f)(std::forward<Args>(args)...);
        if (r == -1) {
            if (errno == EINTR) {
                ++g_eintr_retries;
                continue;
            }
            throw std::system_error{errno, std::generic_category(), what};
        }
        return r;  // 0(EOF)不是 -1,原样放行
    }
}
```

Usage is `sys_call("read", ::read, fd, buf, n)`. Failure throws `std::system_error`, errno boxed into generic_category, `what` becoming the exception message's prefix — catch it in hand and you see at a glance which step blew up. EOF returns 0, not -1, and `sys_call` passes it through untouched, leaving the judgment to the caller — a deliberate design. Only one thing in the template is new: the `for` loop, which serves EINTR exclusively.

What is EINTR? You are waiting on a blocking read for data; a signal arrives, the kernel wakes your process to run the signal handler, and read can no longer keep waiting — it hands back -1, and errno is set to EINTR, meaning this wait was interrupted by a signal. It is not a device error and not missing data; the handling is one sentence: call it again from the top.

Saying it is useless; we arrange an observable experiment (E2). The parent blocks on read at the pipe's read end — a slow fd that waits forever without data. The child sleeps a full 200 ms, interrupts the parent with `kill(SIGUSR1)`, then spends another 200 ms writing `"ping\n"` into the pipe. The signal handler is installed with sigaction, POSIX's interface for registering signal dispositions — how the handler runs and whether the interrupted syscall is restarted are spoken by its sa_flags. The only difference between the two modes is whether we touched the SA_RESTART setting:

```cpp
struct sigaction sa{};
sa.sa_handler = on_signal;
sigemptyset(&sa.sa_mask);
if (restart)
    sa.sa_flags = SA_RESTART;   // 两种模式只差这一行
```

We ran both modes; in both the signal was delivered and 5 bytes were eventually read, and the only difference falls on the counter (the newline inside the quotes in the output is real — `"ping\n"`'s `\n` prints along, so the closing quote starts a new line. Each group also ends with two lines, the EOF pass-through of the second read and the exception build's error path, omitted here):

```text
$ ./e2 norestart
mode = norestart (SA_RESTART OFF: read returns -1/EINTR)
read returned 5 bytes: "ping
"
signal delivered = 1, EINTR retries inside sys_call = 1

$ ./e2 restart
mode = restart (SA_RESTART ON : kernel restarts read for us)
read returned 5 bytes: "ping
"
signal delivered = 1, EINTR retries inside sys_call = 0
```

Without SA_RESTART, sys_call retried once in user mode. With it, the retry count is 0. What actually happened in between, strace recorded in full. Here is the complete timeline of the no-SA_RESTART run, from signal to data (two pids for parent and child; loader noise lines omitted):

```text
kill(106137, SIGUSR1 ...)                # 子进程发信号
<... read resumed>, 0x7ffc..., 64) = ? ERESTARTSYS (To be restarted if SA_RESTART is set)
--- SIGUSR1 {si_signo=SIGUSR1, si_code=SI_USER, si_pid=106181, ...} ---
rt_sigreturn({mask=[]})                  = -1 EINTR (Interrupted system call)
read(3 <unfinished ...>                  # sys_call 的重试入口
<... read resumed>, "ping\n", 64) = 5
```

We walk the timeline by content. The first line's kill is the child sending SIGUSR1 out. The following read resumed is the blocked read being interrupted in the kernel; the ERESTARTSYS strace shows is a kernel-internal return code, and the comment on this group of values in the kernel's own errno.h is stern: they "should never be seen by user programs" — ptrace can observe them at the syscall-exit trace point, but they are never left for the traced user process. So it never escapes to user mode, and the reason we can see it is precisely the observer's channel through ptrace — the kernel comment and our observation post lock into each other. The comment beside it also spells out the semantics: if SA_RESTART is set, the kernel restarts the call. The SIGUSR1 line in the middle is the moment the signal is delivered and the handler starts running. rt_sigreturn is the syscall taken when the handler returns, responsible for restoring the interrupted context; the -1 EINTR on its right is the read return value the kernel wrote into the saved context and landed on the user-mode registers upon restoration — glibc's read wrapper sees it, sets errno, and hands back -1. The last two lines: the new read entry is issued by the `continue` inside `sys_call`, the data arrives, 5 bytes in hand, and the program's own count retries=1 corroborates it. The chain of evidence closes.

Now the control trace with SA_RESTART — its shape is nearly indistinguishable from the previous one, with only two differences (the ... inside the block elides the same interruption timeline as before — kill, read's ERESTARTSYS, SIGUSR1 delivery — differing only in fields that change every run, like pids and addresses):

```text
rt_sigaction(SIGUSR1, {..., sa_flags=SA_RESTORER|SA_RESTART, ...}) = 0
...
rt_sigreturn({mask=[]})                  = 0
read(3 <unfinished ...>                  # 内核的 rewind 重执行,不是用户态新调用
<... read resumed>, "ping\n", 64) = 5
```

Look at the right of rt_sigreturn: it shows 0, and the origin of the following read entry has changed too. The SA_RESTART restart is performed on the content of the restored context: in the signal path the kernel rewrites the saved context to look as though read had never been entered — the instruction pointer is rewound to the syscall instruction itself, and the original syscall number is placed back into register rax. Once rt_sigreturn returns, the CPU re-executes the user-mode syscall instruction and traps into the kernel again, so strace sees another read entry. On x86-64, read's syscall number `__NR_read` happens to be 0, which is exactly where the 0 on rt_sigreturn's right comes from: read itself never returned — that 0 is the syscall number the kernel stuffed back for the restart. glibc never re-issued the call from start to finish, corroborated by the program's own retries=0, and user mode never saw EINTR at any point.

Please note the discrimination problem here: judged by the shape of the read sequence alone, norestart's user-mode retry and restart's kernel restart look identical. To tell who actually did the retry, the program must count the retries itself and corroborate with the value on rt_sigreturn's right — two pieces of evidence matching before it counts. The counter was built into the experiment for precisely this breath.

One final list from man 7 signal, drawing SA_RESTART's boundary: even with it set, this batch of interfaces — poll, ppoll, select, epoll_wait, nanosleep, clock_nanosleep — still fail with EINTR when interrupted; multiplexing and timing calls cannot safely start over, and the man page lists them as the kind SA_RESTART cannot govern. The experiment's `nap()` steps squarely on the list's nanosleep: when a signal interrupts it, the unslept remainder is reported through the remaining-time parameter, and catching up on the sleep is a job only the caller can do:

```cpp
static void nap(long ms)
{
    struct timespec ts{0, ms * 1000000L};
    struct timespec rem{};
    while (nanosleep(&ts, &rem) == -1 && errno == EINTR) {
        ts = rem;   // 补足没睡完的部分:nanosleep 不吃 SA_RESTART
    }
}
```

## Two Exits: expected at the Toolkit Layer, system_error at the Application Top

At the error_code layer, an error is already an ordinary value — copyable, comparable, text-producing. How does it travel upward? The convention this series settles on is one sentence: the toolkit layer — functions and libraries that may fail — returns `std::expected<value, std::error_code>`, errors short-circuiting along the chain. The application's top layer — the one near main where failure is fatal — wraps the error_code into a thrown `std::system_error`. The whole program's throw points collapse to the top; the middle layers see only values, never exceptions.

Why not let exceptions fly the whole way? Write code that tries at every layer and you will know: a few layers down, the catch noise outweighs the logic — and systems code constantly deals with boundaries where exceptions do not conveniently pass through walls, like C interfaces and thread entry points. Then why exceptions at the top? Top-level failures are reported to humans; system_error brings its own what() and carried code, and the catch site's semantics are ready-made. The complete systems of error_code and expected were built from zero in vol3's [error_code](../../../vol3-standard-library/error-utils/66-error-code.md) and [expected](../../../vol3-standard-library/error-utils/64-expected.md) articles; here we only take them into use.

The C++23 hard boundary is also stated up front, so you do not step on it compiling with -std=c++20: measured on g++ 16.2.1, the `<expected>` header can be included under C++20, but the moment std::expected itself is used, the compiler reports `'std::expected' is only available from C++23 onwards`. So this article's code splits into two language-standard tiers: the errno boxing and read-timing experiments compile as C++20, the EINTR and expected-chain experiments as C++23 — do not grab the wrong standard tier when copying the commands.

E3's chain, stacked from the bottom. L0 is open's only exit, the fd going straight into the previous article's unique_fd (the experiment file carries an equivalent implementation, omitted here), so nobody throws:

```cpp
// L0:open 的唯一出口。成功给 fd,失败给 errno 装箱
static std::expected<unique_fd, std::error_code> open_checked(const char* path)
{
    int fd = ::open(path, O_RDONLY);
    if (fd == -1)
        return std::unexpected(errno_code());
    return unique_fd{fd};
}
```

L1's implementation body is a read loop — partial reads are the norm, so a loop must hold it, with the EINTR retry done in place exactly as in sys_call. The genuinely new thing is the shell: and_then strings opening and draining into one chain, and fd's move semantics work as usual:

```cpp
// L1 的实现体:循环读到 EOF,EINTR 就地重试
static std::expected<std::string, std::error_code> drain(unique_fd fd)
{
    std::string out;
    char buf[256];
    for (;;) {
        ssize_t n = ::read(fd.get(), buf, sizeof buf);
        if (n == -1) {
            if (errno == EINTR)
                continue;
            return std::unexpected(errno_code());
        }
        if (n == 0)
            return out;  // EOF
        out.append(buf, static_cast<size_t>(n));
    }
}

// L1:and_then 把"打开 -> 读完"串成一条链
static std::expected<std::string, std::error_code> read_text(const char* path)
{
    return open_checked(path).and_then(drain);
}
```

L2 stacks one more layer of parsing. An empty file is not a syscall error, yet `errc::invalid_argument` enters the chain just the same; what this layer wants to tell you is that expected's error channel does not check pedigree:

```cpp
// L2:空文件给 errc::invalid_argument,非 errno 错误同样进链
static std::expected<std::string, std::error_code> first_line(const char* path)
{
    return read_text(path).and_then([](std::string&& text)
                                    -> std::expected<std::string, std::error_code> {
        auto nl = text.find('\n');
        std::string line =
            (nl == std::string::npos) ? std::move(text) : text.substr(0, nl);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return line;
    });
}
```

L3 hangs an or_else on the error path; it logs exactly one line, neither rewriting nor swallowing, and passes the error through untouched:

```cpp
// L3:or_else 记日志并原样透传
static std::expected<std::string, std::error_code> first_line_logged(const char* path)
{
    return first_line(path).or_else([path](const std::error_code& ec)
                                    -> std::expected<std::string, std::error_code> {
        std::fprintf(stderr, "[config] first_line(\"%s\") failed: %d %s -- passing through\n",
                     path, ec.value(), ec.message().c_str());
        return std::unexpected(ec);
    });
}
```

The chain walked, the upgrade happens only in main — the whole program's single throw point:

```cpp
try {
    std::expected<std::string, std::error_code> r = first_line_logged(path);
    if (!r) {
        // 应用顶层:expected 在此升级成异常
        throw std::system_error(r.error(), std::string("config '") + path + "'");
    }
    std::printf("ok: first line = \"%s\"\n", r->c_str());
} catch (const std::system_error& e) {
    std::printf("caught at top: %s\n", e.what());
    std::printf("    code: value = %d, category = %s\n",
                e.code().value(), e.code().category().name());
}
```

We compiled all four scenarios and tested them in one go: the normal file walks the chain, and the three failures — missing file, directory, empty file — all reach the top untouched. The directory scenario deserves an extra look: open succeeds, the error is born in the chain's middle-layer read, and EISDIR sets out from mid-chain yet still travels all the way to main:

```text
$ ./e3 /tmp/errpar/e3.conf
loading config from: /tmp/errpar/e3.conf
ok: first line = "resolution = 1920x1080"

$ ./e3 /tmp/errpar/e3_missing.conf
[config] first_line("/tmp/errpar/e3_missing.conf") failed: 2 No such file or directory -- passing through
loading config from: /tmp/errpar/e3_missing.conf
caught at top: config '/tmp/errpar/e3_missing.conf': No such file or directory
    code: value = 2, category = generic
```

(The empty-file scenario also passes in full: the error value is 22's invalid_argument, crossing or_else without a changed line. The directory scenario's 21 likewise — you can verify line by line in the repository's output archive.)

The Windows side settles the same convention; its experiment number 4 (the lowercase e4 continuing from the previous article, unrelated to the Linux E4) demonstrated with `read_file_size`: the handle goes to the previous article's unique_handle, the failure branch's first line boxes with `last_error_code()`, and the top wraps into system_error:

```cpp
std::expected<unsigned long long, std::error_code> read_file_size(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return std::unexpected(last_error_code());  // 失败分支头一行:立刻装箱
    }
    unique_handle guard{h};  // 之后所有 return 路径都自动关句柄
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(guard.get(), &sz)) {
        return std::unexpected(last_error_code());
    }
    return (unsigned long long)sz.QuadPart;
}
```

Look at the top-level catch site: `e.code()` holds 2, and `e.code() == errc::no_such_file_or_directory` measured 1 — the comparison works cross-platform. what()'s readability follows the category, and the custom category from earlier does its work here (output taken from the archive's [3] segment; segments [1] and [2] are the direct readings of the success and failure paths, omitted):

```text
    [system_category] e.code().value()=2
      what() 原始:  "read_file_size: ϵͳ�Ҳ���ָ�����ļ���"   <-- message 是 ANSI 代码页字节
      what() 转码后:"read_file_size: 系统找不到指定的文件。"
      e.code() == errc::no_such_file_or_directory : 1
    [win32_category] what():"read_file_size: 系统找不到指定的文件。"  <-- UTF-8 直读,无需转码
      e.code() == errc::no_such_file_or_directory : 1
```

## The Boundary of Zero Overhead: What the Assembly Says and Does Not

The expected-plus-and_then style reads as though every layer must build objects and check states — what does it actually cost? We narrow the scene to this pure value domain: an argument comes in, one possibly failing step, failure yields -1. Two versions for comparison: one hand-written branch, one expected plus and_then plus value_or, each generating its own assembly with -O2:

```cpp
static int manual(int x)
{
    if (x > 0)
        return x * 2;
    return -1;
}

static int monadic(int x)
{
    std::expected<int, std::error_code> e{x};
    auto m = e.and_then([](int v) -> std::expected<int, std::error_code> {
        if (v > 0)
            return v * 2;
        return std::unexpected(std::error_code{
            static_cast<int>(std::errc::invalid_argument), std::generic_category()});
    });
    return m.value_or(-1);
}
```

We diff the two function bodies instruction by instruction; the five instructions are exactly identical:

```text
_Z11sink_manuali:
	leal	(%rdi,%rdi), %eax
	testl	%edi, %edi
	movl	$-1, %edx
	cmovle	%edx, %eax
	ret

_Z12sink_monadici:
	leal	(%rdi,%rdi), %eax
	testl	%edi, %edi
	movl	$-1, %edx
	cmovle	%edx, %eax
	ret
```

Boxing, category addressing, the monadic machinery — after inlining they all evaporate, and what the compiler sees is an instruction sequence of one comparison plus one conditional move. The zero-overhead claim, in an inlinable pure value domain, now has its assembly proof.

The boundary still deserves a full account, or "zero overhead" tells only half the story. We measured `sizeof(std::expected<int, std::error_code>)` at 24, with error_code's own body at 16. When it crosses a non-inlinable function boundary as a return value, the x86-64 calling convention routes returns that do not fit in registers through hidden-pointer memory returns — a mechanism the compiler community records as sret, short for struct return. So zero overhead holds within a scope: it holds in inlinable pure value domains, while across an opaque call boundary it becomes a very real memory round trip. Toolkit-layer functions are mostly small and usually land within one compilation, so the former covers most of our scenes. The latter reminds us: do not force expected through the interface at every single layer.

With this, the pieces this volume uses to manage errors are all in place. The Linux side's `errno_code` and `sys_call` are defined in this article, the Windows side's `last_error_code` likewise, and the `check_win32` template resides in [W01](../windows/file-io/01-win32-file-io.md). The single definition sites of `unique_fd` and `unique_handle` are both in the [previous article](01-raii-paradigm.md), and W01 uses the very same pair. Boxing is freezing, and taking the value on the failure branch's first line — the shared rule of both sides. The upward path is set: expected at the toolkit layer, system_error at the application top. The read-write loops of [POSIX File I/O](../linux/file-io/01-posix-file-io.md) and the handle management of [Win32 File I/O](../windows/file-io/01-win32-file-io.md) later use exactly today's pieces; how they earn their keep in real I/O continues in those two articles.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="errno(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/errno.3.html"
  />
  <ReferenceItem
    :id="2"
    title="GetLastError function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-getlasterror"
  />
  <ReferenceItem
    :id="3"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="4"
    title="read(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/read.2.html"
  />
  <ReferenceItem
    :id="5"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="6"
    title="std::expected"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/utility/expected"
  />
  <ReferenceItem
    :id="7"
    title="std::error_code"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/error/error_code"
  />
  <ReferenceItem
    :id="8"
    title="System Error Codes (0-499)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-"
  />
  <ReferenceItem
    :id="9"
    title="FormatMessage function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-formatmessage"
  />
</ReferenceCard>
