---
title: "System Programming Overview: User Space, Kernel, and the Map of the Two Camps"
description: "The opening map of the system programming sub-volume: first, clarify the wall between user space and kernel space and the only door, the system call, and conduct a real test showing that a single system call is hundreds of times more expensive than a regular function call (two orders of magnitude); then use strace to disassemble the buffering of stdio, revealing the layered structure between library functions and system calls; finally, provide a philosophical comparison table between the two camps, POSIX and Win32, and guide you through setting up a dual laboratory environment for Windows native and WSL. This article only provides a sense and a map, not teaching specific APIs — that is the task of each subsequent article."
chapter: 8
order: 0
platform: host
difficulty: beginner
cpp_standard: [20, 23]
reading_time_minutes: 14
related:
  - "POSIX File I/O: open/read/write and the Life of fd"
  - "Win32 File I/O: Handles, CreateFileW, and Synchronous Read/Write"
tags:
  - host
  - cpp-modern
  - beginner
  - system programming
  - basics
  - introduction
current_status:
  title: Being Polished
  detail: This batch has just been written and is currently being reviewed and polished. The expressions and test results may be adjusted at any time.
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/00-overview.md
  source_hash: 58c49570403c4ef16f07eb1dd3963df31bfb8b661a1a331540d1707235456be6
  translated_at: '2026-11-22T14:12:44+00:00'
  engine: qwen3:14b
  token_count: 15272
---
# Hey! Welcome to the World of System Programming

Welcome! In fact, I have always believed that if any field is most suitable for C++ newcomers to train in after leaving the village of newbies, it's system programming. I really recommend this field to you.

> My answer is simple: in most cases, no matter where you end up — whether it's LLM, backend, frontend, client, or other fields — your code will always need to perform input, computation, and output. System programming is a way for me to use the "operating system" as a means to achieve this abstract understanding.

As I often say, every C++ program we write does not run directly on the hardware. You might find this a bit abstract at first: "Wait, are you saying my code doesn't run directly on the CPU?" That's exactly what it is. When you run your program, there's more than just your program running: browsers, music players, and a bunch of background programs all share the same CPU and memory.

We can say that your CPU is very busy, switching rapidly between processes in a mysterious little rhythm (yes, friends who know the basics of operating systems will immediately point out: the rhythm is the moment the scheduler triggers switch_context). In the previous beat, it was hurriedly handling mouse events in the browser, and in the next moment, it quickly switched to your VSCode to respond to your keyboard events, and in the next beat, it went off to execute the operating system's requirement to have the DMA firmware move data from memory to the hard disk.

It looks very busy. Your code must go through an operating system to reach the hardware. Once your machine is taken over by the operating system, all actions of your machine must be subject to the operating system. You can only watch the operating system work hard on this and that, and finally, it may handle your request.

> Friends who are developing single-chip MCUs and those who are doing assembly-level programming on the GPU are exceptions. Your code does not go through the operating system, and it cannot.

"You're saying it's really crowded!" you might say. The CPU of our machine after booting is mostly in the **user space** defined by the operating system.

"User space? Another new term," you might say,委屈 you a bit. You can think of user space as a running area allocated by the operating system for your normal programs. Yes, you can't do whatever you want, and you certainly can't directly operate the hardware without the operating system. So you have to request. You can't be arrogant and say — "I want to push that video process down because it's taking up memory." You have to go through the operating system to kill that process.

You notice that your permissions are limited. It's the same as when Windows pops up a window telling you that you need administrator permissions to access this file. When you are in the terminal or click on the small icon of your program on the desktop, you can only freely read and write the memory of this process! You want to read a file, open a network connection, start a child process, or even sleep for a millisecond? Sorry, you don't have the permission in user space. You can only fill in the parameters and **request the operating system to do it for you**.

Ah? You're suddenly alert, is there an API? Congratulations, you've just discovered the core of this topic. We basically revolve around it. The API provided by the operating system is an important core of system programming. This volume is intended to use the two world-class **operating system kernels** (Windows NT kernel and Linux kernel) to operate these two operating systems, understand them, and thus break through the bridge between your software and hardware.

> Someone might come to argue with me, what about Android and Apple? Android's underlying system is still the Linux kernel. You can even be radical and say it's a different version of Linux. But I suggest you be a bit careful with this statement, or you might get sprayed.
> As for Apple, why don't I say anything? I have a Mac computer, but I just can't get too interested in this operating system.

"Wait, wait," you might say, panting. "Hold on, I don't want to hear what you're saying. I'm not interested. I want to ask: I have `std::fstream`, `std::thread`, what do they have to do with OS API? Why not just use these things directly?"

Well... the relationship is actually not complicated. They are all **built on top of the OS API**. You might not know that under Linux, `fstream` is implemented using `open/read/write`, while under Windows, it becomes `OpenFile` and `CloseHandle`. The `thread` is implemented using each vendor's thread interface (for example, the Posix interface and the Windows `CreateThread` interface). The standard library only promises interface cross-platform, but it can't cover half of the operating system's capabilities.

For extremely efficient implementations, such as memory mapping, process control, and terminal capabilities (do you have friends who want to do a TUI framework?), we either don't have them or must wait for the standard library to notice your request before triggering them.

When you write high-performance tools, servers, or embedded systems, we will eventually have to lift the floor under our feet and directly communicate with the OS. And OS resources (file descriptors fd, handles, memory mapping) are all things that "must be released after obtaining." RAII is naturally prepared for them, and this is exactly the home field of Modern C++. So the route of this volume is set: we will first get to know the native APIs on both sides, and then use C++20/23 to clean up these dirty work together.

## User Space and Kernel Space: A Boundary and a Single Entry

Let's clearly understand the hard boundary between user space and kernel space. The x64 CPU is divided into privilege levels on the hardware, and this check is done by the CPU itself, not by us. Specifically, how is it done?

Before each sensitive instruction is executed in memory, the hardware checks the permission level of the current code. If you are not qualified, the CPU will directly refuse you. We recognize the level numbers from ring 0 to ring 3. Translating ring directly as "ring" is not wrong — it is the number of the permission level. We only need to remember the two ends among the four levels: the kernel runs at the highest ring 0, and our programs run at the lowest ring 3. The middle rings 1 and 2 are rarely used, so you can just ignore them.

"Why should my program be treated as inferior?" you might be不服气. Let's see what ring 3 is actually blocked from: directly touching the hardware? You don't have the say. Peeking at the page table? You can't say that either. Touching the memory of other processes? You can't even touch it. The page table is the memory mapping record maintained by the operating system, specifically recording which pages of memory you have been allocated. Ring 3 obviously can't touch it. When the CPU blocks you, it doesn't give you any mercy: it doesn't pop up a window, doesn't persuade you, it directly cuts off your instruction and then triggers an exception to send you to the kernel for punishment.

Don't think it's being difficult. This boundary is life-saving. Without it, any program that writes a pointer incorrectly could step into the memory of other processes or into the system's own data, and we would only be able to watch helplessly as the entire computer crashes, with no way to find out who caused the trouble, and multitasking would be impossible. With it, you can safely open a bunch of programs at the same time: if your VSCode writes a pointer incorrectly, it will only crash itself for you to see, the browser will continue to play its songs, and other programs will not be affected at all.

But the kernel's work must be done by someone, so the CPU only leaves one controlled switching path: **special instructions can trigger a privilege level switch, and this is called a trap**. Don't expect the trap to jump anywhere you want. The CPU will only jump to the entry point preset by the kernel at startup, and then distribute based on the system call number in the register. This mechanism has three generations on x86, and we'll go through them in chronological order: Linux used the soft interrupt `int 0x80` in the i386 era. When it came to Pentium II, Intel added the dedicated instruction `sysenter`. When it came to x86-64, the standard practice was the `syscall`/`sysret` instruction pair introduced by AMD. Today, every system call on your 64-bit Linux uses the `syscall` instruction. This can be confirmed in the architecture comparison table in man 2 syscall. By the way, the man manual is divided by category: section 2 collects system calls, section 3 collects library functions, and section 1 collects commands. We'll write man 2 and man 3 later, and you can just look at the section number to know which layer you're looking at.

This switch is not free. When we enter the kernel, we need to save the user space registers and run the entry distribution logic. When returning, we need to reverse this entire process, not missing a single step. If the machine has KPTI page table isolation (to prevent Meltdown, only affected CPUs have it enabled by default), we need to do an additional page table switch when entering and exiting the kernel. The AMD Ryzen I used for testing is not affected by Meltdown, and KPTI is disabled by default, so this cost does not include the page table switch.

This round trip is animated, and you can press the step-by-step button to clearly see the moment when the CPU gear flips and your code waits in place:

<Anim id="sysprog-syscall-roundtrip" />

Words are empty, so let's do a live test. `getpid()` is one of the cheapest system calls, not touching any I/O at all, even saving parameters. Let's compare it with a regular function call, each running ten million times.

```cpp
// bench.cpp — getpid() 系统调用 vs 普通函数调用,各跑一千万次
#include <chrono>
#include <cstdio>

#include <unistd.h>

static long plain_add(long x) noexcept
{
    return x + 1;
}

int main()
{
    constexpr int kCount = 10'000'000;
    volatile long sink = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        sink += plain_add(i);   // 纯用户态函数调用
    }
    auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        sink += ::getpid();     // 每次都是真实的系统调用
    }
    auto t2 = std::chrono::steady_clock::now();
    (void)sink;   // 只写不读的 volatile 会触发 -Wunused-but-set-variable,读一次压掉

    const double ns_plain = std::chrono::duration<double, std::nano>(t1 - t0).count() / kCount;
    const double ns_sys   = std::chrono::duration<double, std::nano>(t2 - t1).count() / kCount;
    std::printf("plain call : %6.2f ns/op\n", ns_plain);
    std::printf("getpid()   : %6.2f ns/op\n", ns_sys);
    std::printf("ratio      : %.0fx\n", ns_sys / ns_plain);
    return 0;
}
```

Let's try to run it (environment: WSL2 Arch Linux, g++ 16.2.1, `-O2`, AMD Ryzen 7 9700X. WSL2 is a subsystem on Windows that runs a real Linux kernel. We'll formally introduce it in the next section on the dual laboratory):

```text
$ g++ -std=c++20 -O2 -Wall -Wextra bench.cpp -o bench && ./bench
plain call :   0.19 ns/op
getpid()   : 123.72 ns/op
ratio      : 658x
```

I ran several rounds, and the numbers are consistently in this range: the time for a regular function call is only about 0.2 ns, less than 1 ns. The time for a system call is in the range of 120-130 ns, a difference of two orders of magnitude, about six hundred times. Thus, we have an intuitive understanding that runs through the entire volume: **a single system call ≈ 100-200 ns, a regular function call ≈ 1 ns**. Later, you will ask why we need to buffer, why we need to batch, and why submitting I/O needs to accumulate a batch. The root of all these questions is this fixed cost of going back and forth.

> We'll add a note: system calls don't always have to trap. Linux has made `clock_gettime` and other high-frequency calls into vDSO: the kernel maps the code and data directly into your process, even skipping the trap, which is the official free ticket from the kernel. We'll meet it again in the time chapter.

The difference between the two paths is clear in the animation:

<Anim id="sysprog-vdso-free-pass" />

::: warning Measuring the cost with getpid, please check your glibc version
In versions of glibc (the most common C standard library implementation on Linux) between 2.3.4 and 2.24, the result of getpid() was cached by the library (to save system calls), and the speed measured would be the same as a regular function, making the data completely inaccurate. man 2 getpid clearly states that this cache was removed from glibc 2.25 onwards, and every call after that is a real system call. The glibc used in this test is 2.44, which is much newer than 2.25, so we won't encounter this old cache.
:::

## Library Functions Are Not System Calls: stdio Is Saving You Money

The concept we're going to clarify next is related to things you use every day: `printf`/`fopen`/`fwrite`, all of which are **C standard library functions, not system calls**. They are shells wrapped around system calls: `fopen` calls `open` to get the fd, and `fwrite` calls `write` to write the fd. Since a single `write` takes about a hundred nanoseconds, the purpose of stdio is very clear: it adds a **user-space buffer** between you and the kernel, accumulating your multiple small writes and batching them into a single `write` when they are full, thus spreading out this fixed cost.

We'll verify this with strace, which will record all system calls made by this process. We'll prepare the same program, in two modes: the stdio version uses `fopen`/`fwrite` to write five times, and the bare version uses `open`/`write` to write five times, writing the same `"hello\n"`:

```cpp
// stdio_vs_raw.cpp — 同样写 5 次 "hello\n":stdio 缓冲 vs 裸 write
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    const bool use_stdio = (argc > 1 && std::strcmp(argv[1], "stdio") == 0);

    if (use_stdio) {
        FILE* f = std::fopen("out_stdio.txt", "w");
        if (!f) return 1;
        for (int i = 0; i < 5; ++i) {
            std::fwrite("hello\n", 1, 6, f);   // 先落在用户态缓冲区
        }
        std::fclose(f);                        // 关闭时一次性刷进内核
    } else {
        int fd = ::open("out_raw.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return 1;
        for (int i = 0; i < 5; ++i) {
            ::write(fd, "hello\n", 6);         // 每次 write 都是一次系统调用
        }
        ::close(fd);
    }
    return 0;
}
```

After compiling, we'll run each mode once, tracking only the `write` calls:

```text
$ g++ -std=c++20 -Wall -Wextra stdio_vs_raw.cpp -o stdio_vs_raw
$ strace -e trace=write ./stdio_vs_raw stdio
write(3, "hello\nhello\nhello\nhello\nhello\n", 30) = 30
+++ exited with 0 +++
$ strace -e trace=write ./stdio_vs_raw raw
write(3, "hello\n", 6)                  = 6
write(3, "hello\n", 6)                  = 6
write(3, "hello\n", 6)                  = 6
write(3, "hello\n", 6)                  = 6
write(3, "hello\n", 6)                  = 6
+++ exited with 0 +++
```

The entire process of batching is animated, with two lanes each recording their own. You can press the step-by-step button and count yourself:

<Anim id="sysprog-stdio-batch-ledger" />

The results are clear at a glance. Let's count. The five `fwrite` calls in the stdio version only result in **one** `write`, saving 30 bytes, which are handed over to the kernel when `fclose` is called. The bare version is honest and sends five `write` calls. As for the default buffering strategy, you can find it in man 3 setbuf: all files are block buffered by default, while stderr is always unbuffered, so error output before a crash always appears in time. By the way, you also saw that the first parameter of `write` is 3. Who is this 3? We'll continue to look down.

**fd (file descriptor, file descriptor) is the general currency on the POSIX side**: the kernel maintains a "file descriptor table" for each process, and fd is the index in the table, a simple small integer. This index is only the first level, and the table entry points to the system-level "open file description". The subtleties of dup (copying the descriptor) and fork (creating a child process) sharing the offset are hidden in that level. `open` gives you a new fd, and subsequent `read`/`write`/`close` all recognize it. There are also three that come with the start: 0 is standard input, 1 is standard output, and 2 is standard error. You don't need to memorize these, the kernel has directly mounted them in `/proc`. Let's take a look on-site:

```text
$ ls -l /proc/self/fd
total 0
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 0 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 1 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 2 -> /dev/pts/13
lr-x------ 1 charliechen charliechen 64 Sep 27 21:37 3 -> /proc/361553/fd
```

0/1/2 all point to the current terminal (`/dev/pts/13`, your number is probably different), and 3 is the one ls opened itself for listing the directory. And the fact that a process has opened something is also reflected in Linux as a directory that can be `ls`ed. As for the full life of fd, how `open` gets it, when `close` returns it, and what happens after `dup`/`fork`, we'll leave it to the first article on the Linux side to expand.

## The Two Camps: Philosophical Comparison Between POSIX and Win32

So far, everything has been Linux's picture. Let's pull back the lens a bit, and you'll see that the mainstream native OS APIs in the world are only two branches. One side is **POSIX**, the standard interface shared by Linux, macOS, and BSD families. The other side is **Win32**, the set of APIs from Windows itself. They solve the same set of problems (files, processes, memory, devices), but their philosophical differences are huge, so big that you should get to know them in advance:

| Dimension | POSIX (Linux/macOS/BSD) | Win32 (Windows) |
| --- | --- | --- |
| Resource Handle | fd: a small int, the index in the process's file descriptor table | HANDLE: an opaque pointer, a generic type for kernel objects such as files, processes, and semaphores |
| Failure Reporting | Returns -1, details checked in errno (thread-local) | Returns sentinel values (INVALID_HANDLE_VALUE/NULL/FALSE), details checked in GetLastError() (thread-local) |
| Interface Style | Almost no-typed C interfaces: int, void*, bit flags | A lot of type aliases and macros: DWORD, LPCWSTR, wide-character API names with W |
| Authoritative Documentation | man pages (online version at man7.org) | Microsoft Learn |

We'll stop here, as expanding each row would take up an entire article. Let's take "opening a file" as an example: POSIX's `open()` returns an int. Win32's `CreateFileW()` (wide-character path) returns a HANDLE, and when it fails, Microsoft Learn's documentation requires the sentinel value INVALID_HANDLE_VALUE, and you're asked to call GetLastError() next. RAII encapsulation and error boxing public tools are established in the two foundational articles of the thinking cornerstone, and both first articles use the ready-made ones: the Linux side starts from [POSIX File I/O](./linux/file-io/01-posix-file-io.md), and the Windows side's first article starts from [Win32 File I/O](./windows/file-io/01-win32-file-io.md). The second step's two articles are also mirror images, both talking about memory mapping: [mmap Memory Mapping](./linux/file-io/02-mmap-memory-mapping.md) vs [File Mapping](./windows/file-io/02-file-mapping.md).

Here, I'll give you a warning in advance to prevent you from flipping over when crossing camps. For an int like fd, you can safely `printf("%d", fd)`. HANDLE is different, it's an opaque pointer, the way to determine if it's invalid, and the corresponding cleanup function (CloseHandle), are all a different set of things. And error reporting is even more of two engines: POSIX's errno and Win32's GetLastError(), both are thread-local and not connected to each other. If we mix them, both sides will be checked wrong. We'll practice the approach in one camp first, and then talk about cross-camp abstraction, which is the topic of the "Platform Abstraction" chapter (ch07).

## Our Dual Laboratories: Windows Native + WSL

We need to learn the two camps, and the most expensive thing is the environment. But the Windows machine in your hands is naturally our two laboratories. The MSYS2 MinGW-w64 toolchain installed on the native side can directly compile Win32 native programs with g++. The default environment in MSYS2 is currently UCRT64, and when we refer to MSYS2 UCRT64 in the following text, it refers to this set of g++. In WSL2, we run a real Linux kernel with a complete user space. It doesn't simulate at the instruction level, the kernel is real, and it runs in a lightweight virtual machine. We'll compile the same `hello.cpp` on both sides:

```cpp
// hello.cpp — 同一个文件,两侧各自编译,验证双实验室就绪
#include <cstdio>

int main()
{
#ifdef _WIN32
    std::puts("hello from Windows host (MinGW-w64 g++)");
#else
    std::puts("hello from WSL Linux (g++)");
#endif
}
```

In any terminal on the native side (MSYS2 UCRT64, g++ 16.1.0):

```text
$ g++ -std=c++20 -Wall -Wextra hello.cpp -o hello.exe
$ ./hello.exe
hello from Windows host (MinGW-w64 g++)
```

On the WSL side, you can directly call it in the Windows terminal (g++ 16.2.1):

```text
$ wsl -e bash -lc 'cd /tmp/sysprog-overview && g++ -std=c++20 -Wall -Wextra hello.cpp -o hello && ./hello'
hello from WSL Linux (g++)
```

Of course, if you have a native Linux operating system, it will be even better, and the test results will be more accurate. In fact, any distribution is fine, and the differences between distributions will not hinder our journey of system programming!

## Learning Map: How This Volume Will Proceed

Finally, let's lay out the full picture. ch00 starts with this article's overview, followed by the two foundational concept articles of the thinking cornerstone. ch01 to ch06 each chapter is a pair of mirrored articles between the Linux side and the Windows side, with one to several articles on each side. ch07 wraps up the two sides into a C++ interface. The entire volume's layout is listed below:

| Chapter | Topic | What's Inside |
| --- | --- | --- |
| ch00 | Thinking Cornerstone | This article's overview: user space/kernel space, system call cost, the map of the two camps; two foundational concept articles of the thinking cornerstone: RAII paradigm, error handling paradigm |
| ch01 | File I/O and File System | open/read/write, the life of fd, memory mapping, page cache, directory and metadata, file locks and inotify |
| ch02 | Memory | Process memory layout, virtual memory API, shared memory, alignment and large pages, VirtualAlloc and heap |
| ch03 | Process and IPC | Process creation and waiting, daemon process, pipes and IPC channels, signals and graceful shutdown, console events and APC |
| ch04 | I/O Multiplexing and Asynchronous I/O | Select/poll/epoll considerations and timerfd/eventfd (connected to the epoll chapter in the networking volume, not repeated), io_uring, OVERLAPPED and IOCP, collected in cross-platform asynchronous abstraction |
| ch05 | Time | Clocks and timers, vDSO free ticket channel |
| ch06 | Terminal | termios and raw mode, pseudo terminals and recording playback, Windows Console API and VT sequences |
| ch07 | Platform Abstraction | Platform detection and abstraction layer design (#ifdef to concepts), syskit tool library integration and unification of the two close relatives |

So far, we have started work on ch00's **Thinking Cornerstone** (the two articles on RAII paradigm and error handling paradigm are both in progress), ch01's foundation **File I/O and File System on both sides** (six articles on the Linux side, five on the Windows side, the four articles linked above are the starting points for both sides), ch02's **Memory on both sides** (four articles on the Linux side, two on the Windows side, we have gone from process memory layout to shared memory), ch03's **Process and Signals** (five articles on the Linux side from fork/exec to graceful shutdown, two articles on the Windows side leading us into console events and APC), ch04's **I/O Multiplexing and Asynchronous I/O** (three articles on the Linux side from the three generations of waiting APIs to io_uring, two articles on the Windows side leading us into completion ports, and one cross-platform article where we use concepts to constrain asynchronous backends at compile time), ch05's **Time** (from the census of nine clockids and vDSO testing, through std::chrono's leap seconds and time zones, to the drift comparison of four generations of timer APIs), and ch06's **Terminal** (two articles on the Linux side from the termios switch panel to pseudo terminal recording and playback, one article on the Windows side leading us into Console API and VT sequences). Finally, ch07's **Platform Abstraction** has also been completed: the abstraction layer design uses the empirical evidence of dead branches with zero diagnostics to move #ifdef to the backend selection, and syskit has compiled all the common tools of the entire sub-volume into a library that can be compiled on both sides. We write here, and all forty articles of the entire volume are complete. Socket five steps, epoll, reactor, and other network programming have already been explained in the network sub-volume, and we will not repeat them here. When needed, we will directly link to them.

As for how to learn, I have set one rule: **both sides are walked in parallel, and the same topic is mirrored**. After we finish learning a Linux topic, you will look at the mirrored article on the Windows side. You will find that the APIs look completely different, but the problems you face are always the same: how to represent resources, how to report failures, and where the buffering is padded. After seeing both answers, your understanding of "what the operating system actually provides" will be deeper than if you only stayed in one camp.

That's enough. I wish you a pleasant experience in system programming!

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="syscall(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/syscall.2.html"
  />
  <ReferenceItem
    :id="2"
    title="getpid(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getpid.2.html"
  />
  <ReferenceItem
    :id="3"
    title="setbuf(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/setbuf.3.html"
  />
  <ReferenceItem
    :id="4"
    title="strace(1)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man1/strace.1.html"
  />
  <ReferenceItem
    :id="5"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
  <ReferenceItem
    :id="6"
    author="Jeffrey Richter & Christophe Nasarre"
    title="Windows via C/C++"
    publisher="Microsoft Press"
    :year="2007"
  />
  <ReferenceItem
    :id="7"
    title="Win32 API"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/"
  />
  <ReferenceItem
    :id="8"
    title="CreateFileW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew"
  />
  <ReferenceItem
    :id="9"
    title="Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3A"
    publisher="Intel"
    url="https://www.intel.com/content/www/us/en/content-details/868146/intel-64-and-ia-32-architectures-software-developer-s-manual-volume-3a-system-programming-guide-part-1.html"
  />
</ReferenceCard>
