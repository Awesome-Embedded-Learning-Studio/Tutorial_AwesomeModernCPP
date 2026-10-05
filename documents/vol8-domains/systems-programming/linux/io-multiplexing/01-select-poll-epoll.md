---
title: "I/O 多路复用:select、poll 与 epoll 的边界与成本"
description: "同一批管道 fd 交给三代等待 API,行为边界与成本曲线各在哪里:本篇实测翻案 select 的 1024 上限(FD_SETSIZE 只是 glibc 位图的 128 字节尺寸,带 fortify 守卫的 FD_SET(1024) 直接 SIGABRT,手工把位图放大到 2048 位后 select(nfds=1026) 对 fd=1025 照常报告就绪,RLIMIT_NOFILE 压到 1024 也不拦 select,真正查 rlimit 的是 poll,1050 条目实测 EINVAL 且 man poll(2) 白纸黑字)、E2 扫描量对照(500 根管道 1 根就绪,select 检查 1002 个 fd、poll 500 条、epoll 1 个,字节量 128 B 进出对 4000 B 进出对每就绪项 12 B)、E3 成本曲线(2000 轮 x 5 取中位,poll 每轮 2527 到 12815 再到 104460 纳秒随 N 线性,输出表头的 us 是存档脚本笔误、单位实为纳秒且已复跑核实,epoll 三档 994/994/985 纳秒纹丝不动,select 在 500 档 14363 比 poll 的 12815 更贵的结构性原因:按 fd 号扫到 nfds 约 1000 而条目只有 500)、E4 select 改写入参(300ms 事件 2s 预算返回后 timeout 剩 1.699s,fd_set 被吃空后 B 的新事件漏掉,poll 现场证明 POLLIN 在,重建后 0ms 就绪)、E5 管道版 LT/ET 行为对照(60000 B 每醒读 4096,LT 连醒 15 次收敛,ET 只醒 1 次剩 55904 B 无人再报,新写 1 字节触发新边沿后 14 次读空到 EAGAIN)、E6 兴趣表直接可见(/proc/self/fdinfo 的 tfd 行即注册表,ADD 三行 DEL 即消,注册 EPOLLIN 实记 0x19,内核自动补 EPOLLERR 与 EPOLLHUP,eventfd-count 写 3 读走归 0),机制课不重开,兴趣表、就绪队列与 LT/ET 内核模型挂网络卷链接,fd 源一概是 pipe 呼应 IPC 篇"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 20
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "IPC:管道、FIFO 与 POSIX 消息队列"
related:
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "epoll:Linux I/O 多路复用,从 poll 的瓶颈到兴趣表与就绪队列"
  - "inotify 文件监控:把文件系统的动静变成事件流"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 异步编程
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# I/O 多路复用:select、poll 与 epoll 的边界与成本

一根管道的读端上调一次 read,数据没到的时候,咱们能做的就只有等:整条执行流被摁在了这个系统调用里,别的 fd 上发生了什么,咱们一概不知道。您要是同时看着两根管道呢?更糟了。咱们一个线程在同一时刻只能堵在一个调用上,堵在第一根管道上的时候,第二根的数据到了也没人理。I/O 多路复用解的就是这道题:把等待一批 fd 的活合并成一次系统调用,哪根有了动静,内核把哪根报给咱们,咱们再去 read。

Linux 手里的等待 API 前后有三代,咱们按资历念就是 select、poll、epoll。它们的名气一辈比一辈大,可真正的差别落在哪儿,不少教材只留了一句“select 限 1024 个 fd,epoll 高性能”。笔者把这一篇的六组实验跑完,得出的答案要修两处:1024 的界确实在,可它其实不在内核里,拿着它的是用户态的 glibc。高性能的半句也成立,可它的贵从哪儿来、另外两个的价格为什么跟着 N 涨,就得拿曲线来说话了。

[上一篇](../process/05-signal-advanced.md)咱们把信号的消费走到了 signalfd 与 pidfd 手里:信号、定时器、子进程的退出,统统化成了 fd,挤进了同一张 poll 表,上一篇的 mini prefork echo 服务器,优雅关闭的状态机跑的正是它。表里各路 fd 都到齐了,负责等的那个调用自身长什么样,咱们当时一句带过。本篇就把它请到了台前:同样一批管道 fd,分别交到三代 API 的手里,咱们看行为的边界各在哪儿,每轮的成本各怎么长。

材料与边界的交代放在前面。本篇实验的 fd 源一概是 pipe,socket 咱们一个都不用:管道的脾气,咱们在[进程间通信一篇](../process/03-ipc.md)实测过了,65536 的容量、写满停住的时序、读端全关的 SIGPIPE 都收在那边了,E5 直接要用的就是头一件。epoll 的机制课也不重开:[网络卷的 epoll 一篇](../../../networking/02-epoll-io-multiplexing.md)从 poll 的瓶颈一路讲到了兴趣表(注册进内核的 fd 清单)、就绪队列与等待队列,C10K 的来龙去脉也在那边,水平触发 LT 与边缘触发 ET 的内核层差异同样是那边的正课。本篇守在 API 行为的层面上,E5 的 LT/ET 也只做管道版的行为对照。事件循环的架构与协程的衔接,[vol5 的异步 I/O 与事件循环](../../../../vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md)一篇讲过,咱们不越过去。

实验的编号 E1 到 E6,对应的存档是仓库 `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/01-select-poll-epoll/` 下的 e1 到 e6 六组文件,代码连同逐字的输出都收在里面,README 里还写了逐组的编译与复现命令,您随时能对表。正文里的输出块有节选,删掉的行以 `...` 标出,对表的时候请您以存档为准。本目录的后面还有两篇,一篇讲的是 timerfd 与 eventfd,另一篇讲的是 io_uring,它们各自的 E 系编号与咱们这里的同号互不相干(timerfd 篇的编号到 E5,io_uring 篇的到 E6),您翻存档的时候认目录就好,存档文件名的 e、t、u 前缀也是给这件事兜底的。系列的公共工具 `unique_fd`、`sys_call`、`errno_code`,咱们这回一次都没让它们出场:要看清的恰恰是三个等待调用自身的姿势,咱们一件封装都没带,实验代码清一色的裸调用。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,CPU 用的是 AMD Ryzen 7 9700X,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,glibc 的版本是 2.44,编译器用的是 g++ 16.2.1,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2`,全部实验拿到的警告数是零。输出捕获自 2026-10-04 的同一轮,计时一律走的是 CLOCK_MONOTONIC。有两个环境事实请您现在就看一眼。头一个要交代的是 `-D_FORTIFY_SOURCE=2`:它让 glibc 对 FD_SET(往 fd_set 位图里置位的宏)的越界写具备了运行期守卫,E1 的头一幕全靠它才看得见。Ubuntu 系的 GCC 在开优化时常常默认就带上了它,咱们在这里显式写明,图的是行为可复现。另一个要交代的是本机的 RLIMIT_NOFILE:soft 与 hard 都给到了 1048576,WSL2 出厂给的就很高,跟传统发行版常见的 1024 档完全不同,E1 里咱们会拿 setrlimit 现场把它调低,回头看的就是谁在乎它。

## E1:select 的 1024,上限由谁来拦

### 位图只有 128 字节,越界写直接 abort

咱们从流传最广的说法下手:select 最多等 1024 个 fd。它的物理来历是 glibc 的 fd_set:一块 1024 位的位图,大小是固定的 128 字节,FD_SETSIZE 这个宏就是它的位数,在编译期就写死了。fd 号一旦过了 1023,位图里根本就没有存放它的位置。真写超了会怎样?咱们挪到子进程里试,为的是让主进程活着收尸:

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

您看输出的头一行:那是 glibc 守卫留在 stderr 的原话,子进程被 SIGABRT 带走了,WTERMSIG 报的是 6。这样的待遇来自带守卫的构建:它把一次本该静默的栈越界写,换成了干脆利落的硬失败。要是没有这个宏的话,FD_SET(1024) 会一声不吭地把这个位写进 set 之后的栈内存,换回一场不知道什么时候发作的栈损坏。所以 1024 的界,头一层就是 glibc 的数据结构给的,而内核到这里还没有出场。

### 手工把位图放大,内核照单全收

界既然是位图给的,咱们干脆绕开 FD_SET 宏,自己开一块够大的内存来冒充 fd_set,咱们看内核认不认。实验开了 1050 根管道,读端的 fd 号一路排到了 2101。咱们关注第 512 根管道的读端,它的 fd 号是 1025,正好压过 1024 的界。位图手工放大到了 2048 位(256 字节),只置位了 bit 1025,数据也提前写进了管道,rlimit 则现场调了两档:

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

咱们把两个 rlimit 档位分开念。压到 soft=1024 的时候(压限不会关掉已经开着的 fd,它只拦新开的),select(nfds=1026) 返回了 1(nfds 是 select 的第一个参数,给的是待查 fd 号的上界加一),fd=1025 照常报告了就绪。也就是说位图给够了内存,内核并不在乎您的 fd 号过没过 1024。同一场里 poll 的下场就不同了:1050 个条目挂上去,直接拿了 -1 加 EINVAL。man poll(2) 的 ERRORS 一节写得分明,nfds 超过了 RLIMIT_NOFILE 就是 EINVAL。man select(2) 的 ERRORS 里其实也写着同一条,可内核倒是没有兑现它:至少在笔者的 6.18 内核上,select 的路径里确实没有这道检查。两个 man 都承诺了这道检查,内核只给 poll 兑了现,E1 抓到的正是文档与实现的脱节。

咱们把 rlimit 抬回 8192,select 的返回纹丝不动,1024 的界从头到尾只在 glibc 的 fd_set 里。同一批的 1050 个 fd,在高位的 rlimit 下,poll 与 epoll 都活得很好:poll 返回了 1,条目数不受 glibc 的管。epoll 把 1050 个 fd 全部 ADD 了进去,wait(0) 报回的就绪恰好是 fd=1025。

所以那句流传最广的说法,咱们现在可以把归属填准了:1024 是 glibc 位图的尺寸,fortify 的守卫负责把越界变成 abort,教材的可移植性章节把它一代代抄了下来。而内核自己不设这道线,查 rlimit 的只有 poll 一家。这个翻案拿来讲机制倒是舒服,拿去指导工程就危险了:fd_set 的尺寸是 ABI 的一部分,真实的项目不可能靠手工放大位图再 reinterpret_cast 过日子,守卫也随时会把同样的尝试变成 abort。真到了工程里,select 的 1024 就当硬界用。咱们把它的归属弄清楚,为的是知道它由谁来拦。

## E2:一次醒来,要检查多少个 fd

三个 API 的返回值都在报告“有几个就绪”,可把就绪者找出来的路,长短的差距可就远了。select 给的只是个数,是谁就得您自己翻:从 0 到 nfds-1 逐位 FD_ISSET,一位都跳不过。poll 也得靠您自己翻,不过翻的是 pollfd 的数组:每条装的都是三样,fd 号、您关心的 events、内核回填的 revents,您要翻的就是最后一个。epoll_wait 就不一样了,它把就绪的事件直接装进您递的数组,返回了几个,您就处理几个。E2 把这件事变成了数:500 根管道,读端的 fd 号从 3 排到 1001,咱们分别让 1 根与 64 根有数据,看三个 API 各自检查了多少:

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

检查的数值得逐个念。select 两次查的都是 1002:它按 fd 号扫,500 根管道开掉了 1000 个 fd,咱们只注册了 500 个读端,可 nfds 被顶到了 1002,没注册的写端号码也躺在扫描的范围里,空位也是要走一遍的。poll 稳在 500:它查的条目数与 fd 号无关。epoll 给的是 1 和 64:wait 把就绪项直接带回来了,返回了多少个,要处理的就是多少个。就绪的比例越小,差距也就越悬殊了。

进出内核的字节数,输出里也替咱们数好了。select 的位图拷进内核是 128 字节,拷出来还是同样的 128 字节,与就绪的多少无关。poll 每轮拷进拷出的是 4000 字节(500 条 x 8 字节),revents 走的是同址改写,下一轮的数组还能接着用,可拷贝量是省不下来的。epoll 出的只有就绪项,每个 12 字节:epoll_event 这个结构在内核的 UAPI 头文件里标了 packed,四字节的 events 紧挨着八字节的 data,中间没有填充的字节,所以是 12 而不是 16。

select 按号扫描的细节,咱们眼下只当它是小别扭。到了 E3 的 500 档,它会变成 select 反而比 poll 慢的结构性原因,咱们把话放在这儿。

## E3:每轮成本,跟着 N 怎么长

E2 看的是单次醒来的扫描量,E3 把它换成稳态循环的总价:N 根管道,只有 0 号每轮有一个字节的事件,循环体的三步是 write 一个字节、wait、read 收走。每档的轮数是 2000,重复 5 次后取的是中位数。select 每轮重建位图(为什么必须重建呢?E4 马上讲),poll 的数组整个复用,epoll 的注册做一次、常驻内核。计时走的是 CLOCK_MONOTONIC,咱们看输出:

```text
每轮耗时(us/round, 2000 轮 x 5 次取中位, WSL2 口径):
     N |       select |         poll |        epoll
    64 |      2825.96 |      2527.21 |       994.39
   500 |     14363.19 |     12814.89 |       993.85
  4096 |  (fd 超界) |    104460.23 |       985.20
```

表头那行的 us/round,笔者得跟您老实交代:它是存档脚本里 printf 的笔误。计时函数 now_ns 返回的是纳秒,总时长除以轮数得到的本来就是每轮的纳秒数,所以 2527.21 要念成 2527 纳秒、约 2.5 微秒。为了核对单位,笔者把存档的程序在同机上重新编跑了一遍:64 档的三个数是 2916、2703、1063 纳秒,结构与量级和存档的完全一致。存档自带的首轮与复跑两份输出彼此也一致(复跑档的 poll 是 2553、12757、103076,epoll 的三档是 992、987、1044)。读错的只是单位,数倒是不用动,咱们看斜率。

poll 的斜率是本篇最贵的一条:N 从 64 到 500 再到 4096,每轮的耗时从 2527 涨到 12815、再涨到 104460 纳秒。后一段 N 翻了 8 倍,耗时跟着翻了 8.1 倍,标准的线性增长:每一轮的 poll 都要把整个数组拷进内核,内核一条条地检查,再把结果拷了回来,N=4096 时的搬运就是每轮 32768 字节,外加 4096 条的逐条过目。epoll 的三档是 994、994、985 纳秒,纹丝不动:注册住进了内核,就绪的收集内核替咱们做完了,等待调用每轮只往回拷就绪的那一项。它一档的耗时约 1000 纳秒,量级上就是 write、wait、read 三次系统调用的价,跟 N 已经没有关系了。存档的输出里还有一段每轮进内核字节数的对照,咱们把它原样请过来:

```text
每轮进内核的字节数(只算等待调用本身):
N=64: select 位图 128 B(重建后拷入), poll 512 B 拷入拷出, epoll 只出就绪项 12 B
N=500: select 位图 128 B(重建后拷入), poll 4000 B 拷入拷出, epoll 只出就绪项 12 B
N=4096: select 位图 128 B(重建后拷入), poll 32768 B 拷入拷出, epoll 只出就绪项 12 B
```

咱们看 select,它的 128 字节倒是从来不变,可那是靠每轮重建换来的,位图还是原来的那一块。poll 的字节数贴着 N 走,从 512 涨到了 32768,与耗时的斜率是同一条。epoll 的输出恒为 12 字节,因为内核每轮只交还就绪的那一项。

select 在 64 档比 poll 慢了一点(2826 对 2527),到了 500 档差距拉开(14363 对 12815)。E2 埋的伏笔在咱们这儿兑现了:select 按 fd 号扫到 nfds,500 根管道把 nfds 顶到了 1000 上下,扫描量是 poll 条目数的两倍。到了 4096 档,select 直接缺席了:一根管道吃两个 fd,4096 根的读端 fd 号已经排到 8193,fd_set 的 1024 位装不下,代码里干脆把它留了空,输出的“(fd 超界)”说的就是它。这个空格本身就是 E1 的界在工程上的分量:别的 API 还在往上跑,select 在这一档是没有出场资格的。

> 存档 README 里记着这个实验的第一版就卡过一次:循环要是没有预置首字节,第一轮的 wait 会超时返回 0,紧跟的那句 read 在空管道上无限期阻塞,整个 benchmark 就一动不动地挂在了那里。修的法子是进循环之前喂一个字节,之后每轮的 write 自己接上。您要是改造这段代码,头一个字节就是头一件要检查的事。

## E4:select 会改写您递进去的参数

E3 里的 select 每轮都要重建位图,根子在 select 的一个行为:它把您递进去的参数当草稿纸用。POSIX.1 对返回之后的 timeout 内容没有定死,选择权留给了实现,Linux 选择了把它改成“还没耗完的时间”,别的实现未必这么干,man select(2) 的可移植性说明专门提了这件事。咱们实测:事件在 300 毫秒时到达,预算给的是 2 秒,返回之后看结构体里剩什么:

```text
[i] select 返回 1, 距开始 301 ms, timeout 结构体剩余 1.699 s
    300 ms 的事件用掉 300, 剩余约 1700 -> select 改写了入参 (POSIX 未定义, Linux 的行为)
```

timeout 结构体里剩下的正是 1.699 秒,300 毫秒的事件正好用掉了 301。想拿 select 写出每轮等满 2 秒的循环,咱们就得每轮重设 timeout,指望它保持原值是不行的。pselect 那边咱们要分两层说:glibc 的 pselect 函数不动您递进去的 timeout,POSIX 要求的正是这一层,兑现靠的是封装里垫的一个局部变量。内核的 pselect6 系统调用倒是照样改写,man select(2) 的说明写得明白,笔者也拿裸 syscall 探过一遍:超时给的是 2.000000000 秒,让一个就绪的 fd 立刻唤醒它,返回后剩的是 1.999998417(笔者这一轮的读数),glibc 的 pselect 则原封不动。所以 pselect 的不改 timeout,是 glibc 兑现的,而内核本身没有这么客气。

被改写的还有 fd_set 本身。咱们让位图同时挂上 A(fd=3)与 B(fd=5),第一轮里两个管道都是空的,200 毫秒超时返回了 0。返回之后咱们数一数位图里还剩几个 fd:0 个。select 把位图改成了“本次就绪名单”,这一轮没有就绪的 fd,名单就是空的。接下来是本实验最要紧的一幕:等到 t+700ms,B 里真的有了字节(咱们拿 poll 现场验过,revents 报的就是 POLLIN),咱们拿着被吃空的位图再调一次 select,结果等了 300 毫秒,返回了 0,B 的事件就这么漏掉了。位图重建之后的对照,输出替咱们说了话:

```text
[ii] 位图初始挂 A(fd=3) 与 B(fd=5), 先看第一轮后位图剩什么
第一轮(挂 A+B, 都无数据): 返回 0, 耗时 200 ms, 位图里剩的 fd 数 = 0
此刻 B 里有数据吗: poll 说 revents=POLLIN(有)
不重建直接再 select: 返回 0, 耗时 300 ms -> B 的事件被漏掉 (位图已被上一轮吃空)
重建后再 select: 返回 1, 耗时 0 ms -> B 立刻就绪 (fd=5, B 在位)
```

两处行为凑在一起了,select 的循环就只能老老实实每轮重设 timeout、重建位图,重复的劳动在 E3 里已经替咱们计过价了。poll 的对照行为温和得多:内核同址改写 revents,events 的字段不动,下一轮的数组接着用。epoll 那就更干脆了,兴趣表是常驻内核的,压根没有每轮递一遍名单的环节。

## E5:LT 与 ET,同一根管道上的行为对照

epoll 的触发模式有两种,水平触发的 LT 与边缘触发的 ET。它们的内核层差异,就绪队列怎么收、回调挂在哪儿,是[网络卷 epoll 一篇](../../../networking/02-epoll-io-multiplexing.md)的正课,咱们这里只做一件事:同一根管道、同一批数据,把两种模式的行为并排摆出来。实验的布置很简单:60000 字节一次写进管道(默认容量 65536,一次是放得下的,IPC 篇实测过的就是它),读端写端都设了 O_NONBLOCK,每次醒来读走的都是 4096 字节,咱们故意不读空,看两种模式各自怎么把剩下的数据送出门。

两种模式在代码里的差别,咱们一眼就能看全:注册的时候在 events 里多写一个 EPOLLET,行为就换了一副:

```cpp
// e5_lt_et_pipe.cpp(节选):LT 段与 ET 段的注册,只差一个 EPOLLET
// LT 段:
epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = p[0];
epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
// ET 段(另一段作用域里,同样的三步):
epoll_event ev{}; ev.events = EPOLLIN | EPOLLET; ev.data.fd = p[0];
epoll_ctl(ep, EPOLL_CTL_ADD, p[0], &ev);
```

咱们从 LT 看起。就绪的条件不消失,每一次的 wait 都报。管里的数据只要没耗尽,epoll_wait 就醒咱们一次,读走了 4096,下一轮的 wait 又报,咱们再读 4096,连着醒了 15 次,把 60000 字节全部收敛完了,最后一轮的 200 毫秒没动静,循环就收了工。

ET 只认一次跳变:从空到有的边沿。同样的 60000 字节,只醒了 1 次,读完了 4096,剩下的 55904 字节就再没有人报了,咱们再等 300 毫秒,返回的是 0。怎么收走?写入 1 个字节,制造一次新的边沿,wait 立刻返回了 1,这一次的循环读按纪律走到 EAGAIN,一共读满了 14 次。读空之后又等了 200 毫秒,输出里写的是安静。全程醒来的次数是 2,读到的字节是 60001。

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

ET 的两个标配,这组对照里都露了脸:非阻塞的 fd,加上把数据读到 EAGAIN 的循环。少了非阻塞,循环里读空之后的那一次 read 会阻塞而不是返回 EAGAIN,咱们就会眼看着事件循环卡死在这一步上。少了循环,一次没读完的剩余数据从此无人再报,55904 字节就是眼前的现场。网络卷的 epoll 篇拿 socket 复现过同一件事,丢字节的机制是同一个:socket 的现场一次丢掉 87KB,管道这边丢的是 55904 字节。

## E6:兴趣表躺在内核里,fdinfo 直接看

epoll_ctl 的每一次 ADD,都是往内核的兴趣表里挂一条。注册表咱们平时只能隔着 API 摸,其实 /proc/self/fdinfo/<epfd> 把它整个摊开了:tfd 行就是在册的 fd,一条注册占的是一行。咱们注册三件东西:一根管道的读端挂 EPOLLIN,另一根的读端挂 EPOLLOUT,再有 eventfd 挂的是 EPOLLIN 加 EPOLLET,然后读 epfd 自己的 fdinfo:

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

DEL 之后:pb 的行当场消失了,注册表里剩下的只有两条。咱们不调任何接口,兴趣表的进出就这么直接可见。更有意思的是 events 字段:咱们注册的明明是 EPOLLIN,内核记下的却是 0x19。咱们按位对照下来,0x19 = 0x01(EPOLLIN)| 0x08(EPOLLERR)| 0x10(EPOLLHUP):内核给每一条注册都自动补上了它们,所以您永远不用自己写,出了错内核一定报。挂 EPOLLOUT 的记录是 0x1c,即 0x04 | 0x08 | 0x10 的组合。挂了 ET 的 eventfd 记 0x80000019,最高位的 0x80000000 就是 EPOLLET 的位。

tfd 行里的 data 字段,也值得咱们停下来看一眼:它是注册时咱们塞进 epoll_event 的载荷,内核把它原样地保管、原样地奉还,wait 报回就绪的时候,咱们靠它认出这个事件属于谁。本篇的实验存的都是 fd 号,工程里存指针的也大有人在,而内核对此不闻不问,干的只是搬运的活。fdinfo 里看到的 data: 7,就是注册 eventfd 时咱们交过去的那个 7。

eventfd 自己的 fdinfo 也顺路看一眼:eventfd-count 就是它内核态的计数值,写入 3 之后 count 变成了 3,read 读走了它,计数归了 0。旁边的 eventfd-id 与 eventfd-semaphore 是另外两个字段,semaphore 一项 6.5 起的内核就有(2023 年合入的),fdinfo 的格式跟着内核版本走,复跑的时候请您以本机为准。

fdinfo 的观察在本卷不是头一回。咱们在[进程间通信一篇](../process/03-ipc.md)里,已经拿 fdinfo 的 pos 字段,证过 SCM_RIGHTS 传 fd 的偏移共享了。一切皆 fd、皆可 fdinfo 的线,在本卷还能延伸:想知道内核替咱们记着什么状态,procfs 顺路就给了,不必等专门的观测接口。

## 工程上的取舍

等咱们把六组实验看完,选型的判断可以落在数字上了。select 的 1024 是 glibc 的界,工程里就当硬界用:位图尺寸是 ABI 的一部分,fortify 的守卫把越界变成 abort,参数被改写的副作用又逼着每轮重建。它换来的东西也有实价:可移植性是三代里最好的,从各类 POSIX 平台到 Windows 的 sockets 都给了一份,fd 数量少而固定的场合,它依然还是够用的。

poll 把位图换成了条目数组。1024 没了,条目数不受 glibc 的管,咱们实测的 1050 个照常工作,revents 的改写依旧是同址,数组整个儿地复用。代价在 E3 的斜率里:每轮全量进出,N=4096 就是每轮 32768 字节加逐条检查的线性成本。

epoll 是 Linux 专属的,别的系统各有对位的设施,BSD 的 kqueue、Windows 的 IOCP。它换来的是注册常驻、等待成本与 N 无关,E3 里 epoll 的平线就是它的底气,fdinfo 还白送了一条看内核状态的路。边界也再念一遍:普通文件进不了 epoll,epoll_ctl 报的是 EPERM,[文件 I/O 的头一篇](../file-io/01-posix-file-io.md)照 man 的原话交代过,本目录第三篇的 E4 还会拿它当面实测,多路复用本来就是 socket 与管道的世界。fd 只有几个的场合:三个都行,您别为了 epoll 平添一层 epfd 的管理。

另一侧的方向也交代一句。Windows 的等待 API 是 WaitForMultipleObjects,它的句柄上限 MAXIMUM_WAIT_OBJECTS 写在 SDK 头文件里,写死的值是 64,按文档的口径,传超了调用直接失败。两个都算是文档写明的上限,可这 64 由 API 门口的硬检查来拦,而咱们 E1 里的 1024,其实落在 glibc 的数据结构里。到大数量上规模化的时候,Windows 的路是异步的 IOCP,模式上与本目录第三篇的 io_uring 同属一类,到时候咱们再碰面。新内核里的 IoRing 走的也是同一思路,这里就不展开了,咱们只认方向。

下一篇咱们把时间与事件也化成 fd:timerfd 把定时器变成 fd,eventfd 把通知也化成了 fd,E6 里露过一面的 eventfd 计数器到那边当主角。它们与管道挂进同一个 epoll 循环、在一条时间线上交错收事件的样子,是那边的综合实验。三代等待 API 的边界与成本,咱们今天就看到这儿,全部的数字都在存档里,您随时能亲手复跑。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="select(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/select.2.html"
  />
  <ReferenceItem
    :id="2"
    title="poll(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/poll.2.html"
  />
  <ReferenceItem
    :id="3"
    title="epoll(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/epoll.7.html"
  />
  <ReferenceItem
    :id="4"
    title="epoll_ctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/epoll_ctl.2.html"
  />
  <ReferenceItem
    :id="5"
    title="epoll_wait(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/epoll_wait.2.html"
  />
  <ReferenceItem
    :id="6"
    title="eventfd(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/eventfd.2.html"
  />
  <ReferenceItem
    :id="7"
    title="proc(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc.5.html"
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
