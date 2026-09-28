---
title: "系统编程总纲:用户态、内核与两大阵营的地图"
description: 系统编程子卷的开篇地图:先讲清用户态/内核态这堵墙与 syscall 这扇唯一的门,实测一次系统调用比一次普通函数调用贵数百倍(两个数量级);再用 strace 拆开 stdio 的缓冲,看清库函数与系统调用的分层;最后给出 POSIX 与 Win32 两大阵营的哲学对照表,以及 Windows 本机 + WSL 双实验室的搭建验证。本篇只给感觉、给地图,不教具体 API——那是后面每一篇的事
chapter: 8
order: 0
platform: host
difficulty: beginner
cpp_standard: [20, 23]
reading_time_minutes: 14
related:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - beginner
  - 系统编程
  - 基础
  - 入门
---

# 系统编程总纲:用户态、内核与两大阵营的地图

您写下的每一个 C++ 程序,其实都不是"直接跑在硬件上"的。机器开机之后的 CPU,绝大部分时间都待在操作系统划定的**用户态**里,您能碰的,只有自己进程的那一块内存。您想读一个文件、开一条网络连接、起一个子进程、睡一毫秒?对不起哦,您在用户态里没有这个权限,您只能把参数填好,**请求操作系统替您干**。这层请求的接口就是操作系统 API。咱们围着它写程序,这就是这一卷要谈的系统编程(system programming)。

您可能会问:我不是有 `std::fstream`、`std::thread` 吗,跟 OS API 有什么关系?关系嘛,它们全都**盖在 OS API 上面**:`fstream` 的底下是 open/read/write,`thread` 的底下是各家的线程接口。标准库承诺的只是接口跨平台,操作系统的一半能力,它其实覆盖不到。

到底缺了什么?您数数看:异步 I/O、内存映射、进程、终端,缺的这些要么压根没有、要么很晚才补上。您去写高性能工具、服务器、嵌入式,咱们迟早要掀开脚下的地板、直接跟 OS 对话。而 OS 资源(文件描述符 fd、句柄、内存映射)全是"取得后必须释放"的东西,RAII 天生就是给它们准备的,这正是 Modern C++ 的主场。所以这一卷的路线,笔者就这么定了:咱们把两侧的原生 API 认识一遍,再用 C++20/23 把这些脏活一起收拾了。本篇的任务是总纲,咱们只给感觉、给地图,不教任何具体 API 的参数,那是后面每一篇的事。

## 用户态与内核态:一道边界和唯一的入口

咱们把用户态和内核态之间的硬边界看清楚。x86 CPU 在硬件上是分特权级的,内核跑在最高的 ring 0,您的程序跑在最低的 ring 3。ring 3 想碰硬件、碰页表、碰别的进程的内存,CPU 直接就拒绝了。您别嫌它刁难,没了这道边界,任何一个程序写飞了一个指针,咱们就能看着整机被带崩,多任务也就无从谈起了。

可内核的活总得有人干,所以 CPU 只留了一条受控的切换路径:**特殊指令可以触发一次特权级切换,咱们把这一次叫做陷入(trap)**。您别指望陷入能随便跳,CPU 只会跳到内核启动时预设好的入口,再按寄存器里填的系统调用号分发。这套机制在 x86 上的三代方案,咱们按时间捋一遍:Linux 在 i386 的年代用软中断 `int 0x80`。等到了 Pentium II,Intel 加入了专用指令 `sysenter`。到了 x86-64,标准做法是 AMD 引入的 `syscall`/`sysret` 指令对。今天您在 64 位 Linux 上的每一次系统调用,走的都是 `syscall` 指令。这一点在 man 2 syscall 的架构对照表里就能对上。对了,man 手册是按类别分节的:2 节收系统调用,3 节收库函数,1 节收命令。后文咱们再写 man 2、man 3,您看一眼节号,就知道查的是哪一层。

这次切换不是白做的。咱们进内核,得保存用户态的寄存器,得跑入口的分发逻辑。返回的时候,咱们再把这一整套逆一遍,真是一步都省不掉的。若机器开了 KPTI 页表隔离(防 Meltdown,仅受影响的 CPU 默认开启),咱们进出内核,还得各做一次额外的页表切换。笔者用来实测的 AMD Ryzen 不受 Meltdown 影响,KPTI 是默认关闭的,所以这笔开销里不含切页表这一项。

口说无凭嘛,咱们现场实测。`getpid()` 是最便宜的系统调用之一,是一点 I/O 都不碰的,连参数都省了,咱们让它跟一次普通的函数调用对比、两边各跑一千万次。

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

咱们试跑一下(环境:WSL2 Arch Linux、g++ 16.2.1、`-O2`、AMD Ryzen 7 9700X;这里的 WSL2 是 Windows 上跑真 Linux 内核的子系统,咱们下文的双实验室一节会正式介绍它):

```text
$ g++ -std=c++20 -O2 -Wall -Wextra bench.cpp -o bench && ./bench
plain call :   0.19 ns/op
getpid()   : 123.72 ns/op
ratio      : 658x
```

笔者多跑了几轮,数字都稳稳地停在这个量级:普通函数调用的耗时只有 0.2 ns 上下、连 1 纳秒都用不到。一次系统调用的耗时,则落在 120~130 ns 的区间,差着两个数量级、六百倍上下的样子。于是咱们有了一个贯穿全卷的直觉:**一次系统调用 ≈ 一两百 ns 的量级,一次普通函数调用 ≈ 1 ns 的量级**。后面那些"为什么要缓冲、为什么要批量、为什么提交 I/O 要攒一批"的追问,根子都是这一来一回的固定开销。

> 咱们多说一句,系统调用其实不是每次都得陷入。Linux 把 `clock_gettime` 这类高频调用做成了 vDSO:内核把代码和数据直接映射进您的进程,连陷入都免了,算是内核官方给出的免票方案。到了时间那一章,咱们还会遇到它。

::: warning 拿 getpid 测开销,请看一眼您的 glibc 版本
在 glibc(Linux 上最常见的 C 标准库实现)2.3.4 到 2.24 之间的版本里,getpid() 的结果曾被库缓存(为了省系统调用),测出来的速度会跟普通函数一样,数据就完全失真了。man 2 getpid 里说得清楚:这段缓存从 glibc 2.25 起就移除了,此后的每一次调用,都是真实的系统调用。本篇实测用的 glibc 是 2.44,比 2.25 新了太多,咱们碰不上这个老缓存。
:::

## 库函数不是系统调用:stdio 在替您省钱

咱们接下来要捋清的概念,跟您天天在用的东西有关:`printf`/`fopen`/`fwrite`,统统**是 C 标准库函数、而不是系统调用**。它们是包在系统调用外面的壳,`fopen` 的底层调 open 拿 fd,`fwrite` 的底层调 write 写 fd。既然一次 write 落在百纳秒的量级,stdio 存在的意义就很直白了:它在您和内核之间垫了一块**用户态缓冲区**,把您的多次小块写攒起来,凑够了再一次 write 递进去,这笔固定的开销,就这么被摊薄了。

咱们拿 strace 验证,它会把这个进程发出的系统调用全部记下来。咱们准备同一个程序、分两种模式:stdio 版用 `fopen`/`fwrite` 写 5 次,裸版的 `open`/`write` 也写 5 次,写下去的都是同一个 `"hello\n"`:

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

咱们编译之后把两种模式各跑一遍,只跟踪 `write` 的调用:

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

结果一目了然,咱们数一数。stdio 版的 5 次 `fwrite` 只换来 **1 次** `write`,攒下的 30 个字节,在 `fclose` 的时候一次性递给内核。裸版就老实了,实打实发了 5 次 `write`。至于默认的缓冲策略,您在 man 3 setbuf 里能查到:所有的文件默认块缓冲,而 stderr 始终无缓冲,所以崩溃前的错误输出总来得及出现。顺带您也看到了,`write` 的第一个参数是个 3。这个 3 是谁?咱们接着往下看。

**fd(file descriptor、文件描述符)是 POSIX 侧的通用货币**:内核给每个进程都维护着一张"文件描述符表",fd 就是表里的下标,一个普普通通的小整数。下标所在的这一层只是第一级,表项指向系统级的"打开文件描述",dup(复制描述符)、fork(创建子进程)共享偏移的奥妙,就藏在那一级。open 发给您一个新 fd,后续的 read/write/close 全认它。还有开局自带的三条:0 是标准输入、1 是标准输出、2 是标准错误。这三样您不用背,内核还把它直接挂在了 `/proc` 里,咱们现场看一眼:

```text
$ ls -l /proc/self/fd
total 0
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 0 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 1 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 2 -> /dev/pts/13
lr-x------ 1 charliechen charliechen 64 Sep 27 21:37 3 -> /proc/361553/fd
```

0/1/2 都指向当前的终端(`/dev/pts/13`,您那边的编号多半不同),3 则是 ls 自己为列目录打开的。连"进程打开了什么"这件事,在 Linux 上也表现为一个可以 `ls` 的目录。至于 fd 完整的一生,open 怎么拿、close 何时还、dup/fork 之后会怎样,咱们留给 Linux 侧首篇展开。

## 两大阵营:POSIX 与 Win32 的哲学对照

到这里都是 Linux 的画面。咱们把镜头拉远一点,您会看到世界上的原生 OS API,主流的谱系就两支。一边的 **POSIX**,是 Linux、macOS、BSD 家族共享的标准接口。另一边的 **Win32**,是 Windows 自家的那套。它们解决的是同一批问题(文件、进程、内存、设备),哲学上的差异却极大,大到值得您提前混个脸熟:

| 维度     | POSIX(Linux/macOS/BSD)                 | Win32(Windows)                                                              |
| -------- | -------------------------------------- | --------------------------------------------------------------------------- |
| 资源句柄 | fd:一个小 int,进程文件描述符表的下标   | HANDLE:不透明指针型,文件/进程/信号量等内核对象通用                          |
| 失败报告 | 返回 -1,细节查 errno(线程局部)         | 返回哨兵值(INVALID_HANDLE_VALUE/NULL/FALSE),细节查 GetLastError()(线程局部) |
| 接口风格 | 几乎无类型的 C 接口:int、void*、位标志 | 大量类型别名与宏:DWORD、LPCWSTR,宽字符版 API 名字带 W                       |
| 权威文档 | man 手册(man7.org 在线版)              | Microsoft Learn                                                             |

咱们点到为止,每一行展开起来都是一整篇的体量。咱们就拿"打开文件"来说:POSIX 的 `open()` 返回 int。Win32 的 `CreateFileW()`(宽字符路径)返回 HANDLE,失败的时候,Microsoft Learn 的文档要求返回的哨兵值是 INVALID_HANDLE_VALUE,并让您接着调 GetLastError。两侧首篇会把各自的做法就地带出 RAII 封装:Linux 侧从 [POSIX 文件 I/O](./linux/file-io/01-posix-file-io.md) 开始,Windows 侧的第一篇从 [Win32 文件 I/O](./windows/file-io/01-win32-file-io.md) 开始。第二步的两篇同样互为镜像,讲的同样是内存映射:[mmap 内存映射](./linux/file-io/02-mmap-memory-mapping.md) 对着 [文件映射](./windows/file-io/02-file-mapping.md)。

这里得给您打个预防针,免得您跨阵营的时候翻车。fd 这样的 int,您随手 `printf("%d", fd)` 都没问题。HANDLE 就不一样了,它是不透明的指针,失效的判定方式、配套的清理函数(CloseHandle),全都是另一套的东西。而错误报告,更是两台引擎:POSIX 的 errno、Win32 的 GetLastError(),各自都是线程局部的、互不相通,咱们要是混着查,两边都会被咱们查错。咱们在一个阵营里把做法练熟,再去谈跨阵营的抽象,那是后面"平台抽象"一章(ch07)的事。

## 咱们的双实验室:Windows 本机 + WSL

咱们要学两大阵营,最贵的其实是环境。可您手里的一台 Windows 机器,天然就是咱们的两个实验室。本机装好的 MSYS2 MinGW-w64 工具链,g++ 能直接编出 Win32 的原生程序;它在 MSYS2 里现行的默认环境是 UCRT64,后文咱们写 MSYS2 UCRT64,说的就是这套 g++。WSL2 里跑的,则是一套真正的 Linux 内核加完整用户态。它不做指令级的模拟,内核是真的,跑在一个轻量的虚拟机里。咱们拿同一个 `hello.cpp` 到两边都编一遍:

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

咱们在 Windows 本机这侧(MSYS2 UCRT64,g++ 16.1.0)的任意终端里:

```text
$ g++ -std=c++20 -Wall -Wextra hello.cpp -o hello.exe
$ ./hello.exe
hello from Windows host (MinGW-w64 g++)
```

到了 WSL 那边,您在 Windows 终端里就能直接调(g++ 16.2.1):

```text
$ wsl -e bash -lc 'cd /tmp/sysprog-overview && g++ -std=c++20 -Wall -Wextra hello.cpp -o hello && ./hello'
hello from WSL Linux (g++)
```

当然，如果您有 Linux 原生的操作系统，那就更棒了，测试的结果也将会更为准确。任何发行版都可以，发行版的差异不会阻碍我们系统编程的旅程！

## 学习地图:这一卷打算怎么走

最后咱们把全景铺开。ch00 打头的是本篇总纲。ch01 到 ch06 的每一章,都是 Linux 侧与 Windows 侧互为镜像的成对文章,每侧一到数篇。到了 ch07 收尾,咱们把两侧收成一套 C++ 接口。整卷的铺排,咱们列在下面:

| 章   | 主题                | 里面有什么                                                                                                |
| ---- | ------------------- | --------------------------------------------------------------------------------------------------------- |
| ch00 | 思维基石            | 本篇:用户态/内核态、系统调用成本、两大阵营地图                                                            |
| ch01 | 文件 I/O 与文件系统 | open/read/write、fd 的一生、内存映射、目录与元数据                                                        |
| ch02 | 内存                | 虚拟内存、mmap 与堆、分配器                                                                               |
| ch03 | 进程与 IPC          | 进程创建与等待、管道、信号                                                                                |
| ch04 | I/O 多路复用        | select/poll/epoll 的取舍(与[网络卷的 epoll 篇](../networking/02-epoll-io-multiplexing.md)衔接,不重复展开) |
| ch05 | 时间                | 时钟与定时器、vDSO 的免票通道                                                                             |
| ch06 | 终端                | tty、原始模式、终端工具的输入处理                                                                         |
| ch07 | 平台抽象            | 把两侧收成一套 C++ 接口:错误、句柄、缓冲的统一封装                                                        |

这一卷目前开工的是 ch01 的地基:**两侧文件 I/O 与文件映射**,就是上面链接的 01/02 四篇。后续咱们按表推进:进程、I/O 多路复用、内存管理这些主题,表里都排着;异步 I/O 也在计划里等着咱们。socket 五步、epoll、reactor 一类的网络编程,在网络子卷里已经讲明白了,咱们这里就不重复了,需要时咱们直接链接过去。

至于怎么学,笔者就定了一条:**两大侧并行地走、同主题互为镜像**。咱们每学完一个 Linux 的主题,您就去看一眼 Windows 镜像篇。您会发现,API 的长相完全不一样,要面对的问题却永远是同一批:资源怎么表示,失败怎么报告,缓冲又垫在了哪一层。咱们把两个答案都见过,您对"操作系统到底提供了什么"的理解,会比只待在一个阵营里的理解深一层。

足够了，祝您有一段开心的系统编程的体验！

<ReferenceCard title="参考资源">
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
</ReferenceCard>
