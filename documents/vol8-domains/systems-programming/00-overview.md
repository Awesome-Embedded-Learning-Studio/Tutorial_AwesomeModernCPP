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
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 嘿！欢迎来到系统编程的领域

欢迎！实际上笔者始终认为，如果哪个领域最适合刚出新手村的 C++ 新手们试炼。我还真推介系统编程这个领域。

> 笔者的答案比较简单，因为绝大部分的情况下，不管您最后是奔向 LLM，还是去了后端、前端、客户端，还是奔向其他的领域。您的代码始终需要做输入、运算、输出这些活动。系统编程笔者就认为可以借助“使用操作系统”的名义，完成对这个抽象的理解。

就像笔者常说的,咱们写下的每一个 C++ 程序,都并不是直接跑在硬件上的。您初听可能觉得有点抽象,“哈？你是说我的代码不直接跑在CPU上？”。大概就是这样的。您跑起自己程序的那一刻,同时跑着的可不止您一个:浏览器、音乐播放器、一大堆后台程序,全跟您挤在同一颗 CPU、同一块内存上。

咱们可以说，您的CPU实际上非常的忙碌，以某一个神秘的小节拍（是的，知晓操作系统基础的朋友立马就会指出：节拍是每次调度器触发switch_context的时刻）快速的在多个进程间穿梭，而上一个节拍，他还在匆忙的处理浏览器中的鼠标事件，下一秒迅速的切换到了您的vscode 响应您的键盘事件，在下一个节拍又跑去执行操作系统要求的，让 DMA 固件执行把数据从内存搬运到硬盘的操作。

看着就很忙嘛，您的代码必须要隔着一个操作系统才能抵达硬件。当您的机器经过了操作系统的接管之后,我们机器所有的动作，都必须全听操作系统的，您只能在旁边看着操作系统忙碌的处理这个、处理那个，最后可能才会处理您的。

> 单片机 MCU 开发的朋友，和直接对显卡进行汇编级编程的朋友除外，您们的代码不经过操作系统，也经过不了。

“好挤喔！”，您如是说道。咱们机器开机之后的 CPU, 绝大部分时间都待在操作系统划定的**用户态**里。

“用户态？又是新词”，委屈您一下。用户态您可以理解为操作系统给您编写的普通程序划的一块运行区域。是的，您不能为所欲为，当然不可以越过操作系统直接操作硬件，所以必须请求。您不可以霸道的说——揣下去那个某某视频进程，我嫌他占内存。必须经过操作系统的手去 kill process 掉那个进程才可以。

您注意到，您的权限是收的。就像 Windows 弹窗告诉您访问这个文件需要管理员权限一个道理。当您在终端，或者是在桌面点击你的程序的小图标的适合，您能自由读写的只有这个进程自己的那一块内存！您想读一个文件、开一条网络连接、起一个子进程,哪怕只是睡一毫秒?对不起哦,您在用户态里没有这个权限,您只能把参数填好,**请求操作系统替您干**。

欸？您马上机灵起来了，接口？是不是也有 API 呢？恭喜你，你直接发现了我们这个主题的核心，我们基本围绕着他转了。操作系统的 API 是我们系统编程的一个重要核心。这一卷笔者就是想借用世界上两个王牌级别的**操作系统内核**（Windows NT 内核和 Linux 内核）对外提供的 API，来操作这几个操作系统为您服务，理解他进而打穿您软件和硬件的桥梁。

> 有人要坐过来找我吵架了，安卓呢？苹果呢？安卓的底层仍然是 Linux 内核，您甚至可以直接激进的说他是Linux别样的发行版，但是我建议您稍微悠着点这样说，小心被喷哦~。
> 至于苹果嘛，为什么我不说？我有 Mac 电脑，但是对这个操作系统实在提不起太多的兴趣~

“你等下，你等下”，可能您又要喘着粗气说了。“把你讲的东西停一下，我没兴趣，我想问的是：我不是有 `std::fstream`、`std::thread` 吗,跟 OS API 有什么关系呢?为什么不直接用这些东西”

嗯……关系嘛，其实不复杂，它们全都**盖在了 OS API 上面**。您可能不知道的是，`fstream` 的 Linux 底下是 open/read/write,而在 Windows 底下的实现，变成了 OpenFile 和 CloseHandle。`thread` 的底下是各家的线程接口（比如说，Posix接口和Windows的CreateThread接口）。标准库承诺的只是接口跨平台,操作系统的一半能力,它其实覆盖不到。

极端高效的实现，比如说内存映射、进程控制、终端能力（有想做TUI框架的朋友嘛？），咱们要么没有，要么必须等底下的标准库注意到您的请求后才触发。

您去写高性能工具、服务器、嵌入式,咱们迟早要掀开脚下的地板、直接跟 OS 对话。而 OS 资源(文件描述符 fd、句柄、内存映射)全是“取得后必须释放”的东西,RAII 天生就是给它们准备的,这正是 Modern C++ 的主场。所以这一卷的路线,笔者就这么定了:咱们把两侧的原生 API 认识一遍,再用 C++20/23 把这些脏活一起收拾了。

## 用户态与内核态:一道边界和唯一的入口

咱们把用户态和内核态之间的硬边界看清楚。x64 CPU 在硬件上是分特权级的,而且这层检查由 CPU 自己做,轮不到咱们插手。具体上是如何做的呢？

当我们躺在内存上的每条敏感指令执行之前,硬件会核对当前代码的权限档位,您要是不够格, CPU会直接当场就拒绝您。咱们要认的档位编号从 ring 0 排到 ring 3。咱们把 ring 直译成环来叫也不算错——它就是权限档位的编号。四档里咱们平时只要记两头:内核跑在最高的 ring 0,咱们的程序跑在最低的 ring 3,中间的 ring 1 和 ring 2 常年吃灰,您就当它们不存在。



“凭什么呀,我的程序低人一等?”您可能不服气。咱们看看 ring 3 到底被拦了什么:直接碰硬件?您说了不算。偷看页表?您也说不得。摸别的进程的内存?您更摸不着。页表是操作系统维护的内存映射记录,专门记您分到了哪几页内存,ring 3 当然碰不得。CPU 拦您的时候也不讲情面:它不弹窗口、不劝您,直接把您的指令掐掉,再顺手触发一个异常把您扭送到内核面前发落。

您别嫌它刁难,这道边界是保命的。没了它,任何一个程序写飞了一个指针,就能一路踩进别的进程的内存、踩进系统自己的数据,咱们就只能眼睁睁看着整个电脑被带崩,连闯祸的是谁都没处查,多任务也就无从谈起了。有了它,您才敢放心大胆地同时开一堆程序:您的 vscode 写飞了指针,顶多它自己崩给您看,浏览器照常放它的歌,别的程序一点事都没有。

可内核的活总得有人干,所以 CPU 只留了一条受控的切换路径:**特殊指令可以触发一次特权级切换,咱们把这一次叫做陷入(trap)**。您别指望陷入能随便跳,CPU 只会跳到内核启动时预设好的入口,再按寄存器里填的系统调用号分发。这套机制在 x86 上的三代方案,咱们按时间捋一遍:Linux 在 i386 的年代用软中断 `int 0x80`。等到了 Pentium II,Intel 加入了专用指令 `sysenter`。到了 x86-64,标准做法是 AMD 引入的 `syscall`/`sysret` 指令对。今天您在 64 位 Linux 上的每一次系统调用,走的都是 `syscall` 指令。这一点在 man 2 syscall 的架构对照表里就能对上。对了,man 手册是按类别分节的:2 节收的是系统调用,3 节收的是库函数,1 节收的则是命令。后文咱们再写 man 2、man 3,您看一眼节号,就知道查的是哪一层。

这次切换不是白做的。咱们进内核,得保存用户态的寄存器,得跑入口的分发逻辑。返回的时候,咱们再把这一整套逆一遍,真是一步都省不掉的。若机器开了 KPTI 页表隔离(防 Meltdown,仅受影响的 CPU 默认开启),咱们进出内核,还得各做一次额外的页表切换。笔者用来实测的 AMD Ryzen 不受 Meltdown 影响,KPTI 是默认关闭的,所以这笔开销里不含切页表这一项。

这次往返做成了动画,您可以按步进键,把 CPU 档位翻转、您的代码原地等待的瞬间看个清楚:

<Anim id="sysprog-syscall-roundtrip" />

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

咱们试跑一下(环境:WSL2 Arch Linux、g++ 16.2.1、`-O2`、AMD Ryzen 7 9700X。这里的 WSL2 是 Windows 上跑真 Linux 内核的子系统,咱们下文的双实验室一节会正式介绍它):

```text
$ g++ -std=c++20 -O2 -Wall -Wextra bench.cpp -o bench && ./bench
plain call :   0.19 ns/op
getpid()   : 123.72 ns/op
ratio      : 658x
```

笔者多跑了几轮,数字都稳稳地停在这个量级:普通函数调用的耗时只有 0.2 ns 上下、连 1 纳秒都用不到。一次系统调用的耗时,则落在 120~130 ns 的区间,差着两个数量级、六百倍上下的样子。于是咱们有了一个贯穿全卷的直觉:**一次系统调用 ≈ 一两百 ns 的量级,一次普通函数调用 ≈ 1 ns 的量级**。后面您会追问的“为什么要缓冲、为什么要批量、为什么提交 I/O 要攒一批”,根子都是这一来一回的固定开销。

> 咱们多说一句,系统调用其实不是每次都得陷入。Linux 把 `clock_gettime` 这类高频调用做成了 vDSO:内核把代码和数据直接映射进您的进程,连陷入都免了,算是内核官方给出的免票方案。到了时间那一章,咱们还会遇到它。

两条路的差别,咱们在动画里各走一遍就直白了:

<Anim id="sysprog-vdso-free-pass" />

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

攒批的整个过程做成了动画,两条泳道各记各的,您按步进键自己数一遍:

<Anim id="sysprog-stdio-batch-ledger" />

结果一目了然,咱们数一数。stdio 版的 5 次 `fwrite` 只换来 **1 次** `write`,攒下的 30 个字节,在 `fclose` 的时候一次性递给内核。裸版就老实了,实打实发了 5 次 `write`。至于默认的缓冲策略,您在 man 3 setbuf 里能查到:所有的文件默认块缓冲,而 stderr 始终无缓冲,所以崩溃前的错误输出总来得及出现。顺带您也看到了,`write` 的第一个参数是个 3。这个 3 是谁?咱们接着往下看。

**fd(file descriptor、文件描述符)是 POSIX 侧的通用货币**:内核给每个进程都维护着一张“文件描述符表”,fd 就是表里的下标,一个普普通通的小整数。下标所在的这一层只是第一级,表项指向系统级的“打开文件描述”,dup(复制描述符)、fork(创建子进程)共享偏移的奥妙,就藏在了那一级。open 发给您一个新 fd,后续的 read/write/close 全认它。还有开局自带的三条:0 是标准输入、1 是标准输出、2 是标准错误。这三样您不用背,内核还把它直接挂在了 `/proc` 里,咱们现场看一眼:

```text
$ ls -l /proc/self/fd
total 0
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 0 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 1 -> /dev/pts/13
lrwx------ 1 charliechen charliechen 64 Sep 27 21:37 2 -> /dev/pts/13
lr-x------ 1 charliechen charliechen 64 Sep 27 21:37 3 -> /proc/361553/fd
```

0/1/2 都指向当前的终端(`/dev/pts/13`,您那边的编号多半不同),3 则是 ls 自己为列目录打开的。而进程打开了什么这件事,在 Linux 上也表现为一个可以 `ls` 的目录。至于 fd 完整的一生,open 怎么拿、close 何时还、dup/fork 之后会怎样,咱们留给 Linux 侧首篇展开。

## 两大阵营:POSIX 与 Win32 的哲学对照

到这里都是 Linux 的画面。咱们把镜头拉远一点,您会看到世界上的原生 OS API,主流的谱系就两支。一边的 **POSIX**,是 Linux、macOS、BSD 家族共享的标准接口。另一边的 **Win32**,是 Windows 自家的那套。它们解决的是同一批问题(文件、进程、内存、设备),哲学上的差异却极大,大到值得您提前混个脸熟:

| 维度     | POSIX(Linux/macOS/BSD)                 | Win32(Windows)                                                              |
| -------- | -------------------------------------- | --------------------------------------------------------------------------- |
| 资源句柄 | fd:一个小 int,进程文件描述符表的下标   | HANDLE:不透明指针型,文件/进程/信号量等内核对象通用                          |
| 失败报告 | 返回 -1,细节查 errno(线程局部)         | 返回哨兵值(INVALID_HANDLE_VALUE/NULL/FALSE),细节查 GetLastError()(线程局部) |
| 接口风格 | 几乎无类型的 C 接口:int、void*、位标志 | 大量类型别名与宏:DWORD、LPCWSTR,宽字符版 API 名字带 W                       |
| 权威文档 | man 手册(man7.org 在线版)              | Microsoft Learn                                                             |

咱们点到为止,每一行展开起来都是一整篇的体量。咱们就拿“打开文件”来说:POSIX 的 `open()` 返回 int。Win32 的 `CreateFileW()`(宽字符路径)返回 HANDLE,失败的时候,Microsoft Learn 的文档要求返回的哨兵值是 INVALID_HANDLE_VALUE,并让您接着调 GetLastError。RAII 封装与错误装箱的公共工具,在思维基石的两篇里就立好了唯一定义处,两侧首篇用的都是现成的:Linux 侧从 [POSIX 文件 I/O](./linux/file-io/01-posix-file-io.md) 开始,Windows 侧的第一篇从 [Win32 文件 I/O](./windows/file-io/01-win32-file-io.md) 开始。第二步的两篇同样互为镜像,讲的同样是内存映射:[mmap 内存映射](./linux/file-io/02-mmap-memory-mapping.md) 对着 [文件映射](./windows/file-io/02-file-mapping.md)。

这里得给您打个预防针,免得您跨阵营的时候翻车。fd 这样的 int,您随手 `printf("%d", fd)` 都没问题。HANDLE 就不一样了,它是不透明的指针,失效的判定方式、配套的清理函数(CloseHandle),全都是另一套的东西。而错误报告,更是两台引擎:POSIX 的 errno、Win32 的 GetLastError(),各自都是线程局部的、互不相通,咱们要是混着查,两边都会被咱们查错。咱们在一个阵营里把做法练熟,再去谈跨阵营的抽象,而那是后面“平台抽象”一章(ch07)的事。

## 咱们的双实验室:Windows 本机 + WSL

咱们要学两大阵营,最贵的其实是环境。可您手里的一台 Windows 机器,天然就是咱们的两个实验室。本机装好的 MSYS2 MinGW-w64 工具链,g++ 能直接编出 Win32 的原生程序。它在 MSYS2 里现行的默认环境是 UCRT64,后文咱们写 MSYS2 UCRT64,说的就是这套 g++。WSL2 里跑的,则是一套真正的 Linux 内核加完整用户态。它不做指令级的模拟,内核是真的,跑在一个轻量的虚拟机里。咱们拿同一个 `hello.cpp` 到两边都编一遍:

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

当然，如果您有 Linux 原生的操作系统，那就更棒了，测试的结果也将会更为准确。其实任何发行版都可以，发行版的差异不会阻碍我们系统编程的旅程！

## 学习地图:这一卷打算怎么走

最后咱们把全景铺开。ch00 打头的是本篇总纲,再跟上思维基石的两篇概念篇。ch01 到 ch06 的每一章,都是 Linux 侧与 Windows 侧互为镜像的成对文章,每侧摆了一到数篇。到了 ch07 收尾,咱们把两侧收成一套 C++ 接口。整卷的铺排,咱们列在下面:

| 章   | 主题                | 里面有什么                                                                                                |
| ---- | ------------------- | --------------------------------------------------------------------------------------------------------- |
| ch00 | 思维基石            | 本篇总纲:用户态/内核态、系统调用成本、两大阵营地图;思维基石两篇:RAII 范式、错误处理范式                  |
| ch01 | 文件 I/O 与文件系统 | open/read/write、fd 的一生、内存映射、页缓存、目录与元数据、文件锁与 inotify                              |
| ch02 | 内存                | 进程内存布局、虚拟内存 API、共享内存、对齐与大页、VirtualAlloc 与堆                                       |
| ch03 | 进程与 IPC          | 进程创建与等待、守护进程、管道与 IPC 通道、信号与优雅关闭、控制台事件与 APC                              |
| ch04 | I/O 多路复用与异步 I/O | select/poll/epoll 的取舍与 timerfd/eventfd(与[网络卷的 epoll 篇](../networking/02-epoll-io-multiplexing.md)衔接,不重复展开)、io_uring、OVERLAPPED 与 IOCP,收在跨平台的异步抽象 |
| ch05 | 时间                | 时钟与定时器、vDSO 的免票通道                                                                             |
| ch06 | 终端                | termios 与 raw 模式、伪终端与录制回放、Windows Console API 与 VT 序列                                   |
| ch07 | 平台抽象            | 平台检测与抽象层设计(#ifdef 到 concepts)、syskit 工具库收编与两套近亲的统一                              |

目前咱们已经动工的,是 ch00 的**思维基石**(RAII 范式、错误处理范式两篇都在),ch01 的地基**两侧文件 I/O 与文件系统**(Linux 侧六篇、Windows 侧五篇,上面链接的 01/02 四篇是两侧各自的开头),ch02 的**两侧内存**(Linux 侧四篇、Windows 侧两篇,咱们从进程内存布局一路讲到了共享内存),ch03 的**进程与信号**(Linux 侧五篇从 fork/exec 的一路走到优雅关闭,Windows 侧的两篇把咱们带进了控制台事件与 APC),ch04 的**I/O 多路复用与异步 I/O**(Linux 侧三篇从三代等待 API 走到 io_uring,Windows 侧的两篇把咱们带进了完成端口,跨平台一篇咱们再用 concepts 把异步后端约束在编译期),ch05 的**时间**(从九个 clockid 的普查与 vDSO 实测,经 std::chrono 的闰秒与时区,收在定时器四代 API 的漂移对照),以及 ch06 的**终端**(Linux 侧两篇从 termios 的开关面板走到伪终端的录制回放,Windows 侧的一篇,把咱们带进了 Console API 与 VT 序列)。最后 ch07 的**平台抽象**也收了卷:抽象层设计拿死分支零诊断的实证把 #ifdef 退到选后端的一处,syskit 把全子卷的公共工具收编成两侧都编译得起来的库。咱们写到这里,全卷四十篇就都齐了。socket 五步、epoll、reactor 一类的网络编程,在网络子卷里已经讲明白了,咱们这里就不重复了,需要时咱们直接链接过去。

至于怎么学,笔者就定了一条:**两大侧并行地走、同主题互为镜像**。咱们每学完一个 Linux 的主题,您就去看一眼 Windows 镜像篇。您会发现,API 的长相完全不一样,要面对的问题却永远是同一批:资源怎么表示,失败怎么报告,缓冲又垫在了哪一层。咱们把两个答案都见过,您对“操作系统到底提供了什么”的理解,会比只待在一个阵营里的理解深一层。

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
  <ReferenceItem
    :id="9"
    title="Intel 64 and IA-32 Architectures Software Developer's Manual, Volume 3A"
    publisher="Intel"
    url="https://www.intel.com/content/www/us/en/content-details/868146/intel-64-and-ia-32-architectures-software-developer-s-manual-volume-3a-system-programming-guide-part-1.html"
  />
</ReferenceCard>
