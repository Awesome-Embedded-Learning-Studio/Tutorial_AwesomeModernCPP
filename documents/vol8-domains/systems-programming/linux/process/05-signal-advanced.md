---
title: "信号(下):实时信号、signalfd 与 pidfd"
description: "handler 之外信号还有哪些消费方式:本篇实测实时信号排队(阻塞期连发 3 次 SIGRTMIN+1,handler 跑满 3 次且 101/102/103 按 FIFO 到账,对照 SIGUSR1 三发只记 1 次)、sigqueue 的 si_value 原样读回(union sigval 同一字节两种读法)、投递顺序的意外(内核出队小号在前,空 sa_mask 批量放行时信号帧按出队序叠栈、后叠的执行在前,handler 看到的是大号倒挂,同号 FIFO 恒成立)、signalfd 的阻塞在前纪律(不阻塞则信号被 handler 消费、poll 200ms 零事件的漏信号实证)、读走即消费的双向互斥、一次 read 384 字节 3 条记录、与 timerfd 混挂的 151/202/301/402ms 交错时间线、pidfd 的第三种收尸(poll 得 POLLIN 后 waitid(P_PIDFD) 拿退出码 42,收尸后再 poll 得 POLLHUP)、pidfd_getfd 在 yama=1 下父到子通、兄弟 EPERM、pid 复用竞态的真实现场(kill 误伤新进程,pidfd_send_signal 拿 ESRCH)、sigwaitinfo 与 signalfd 同一条 pending 队列双向互斥、谁读谁消费、signalfd 进事件循环的优雅关闭全状态机(RUNNING/DRAINING/REAPING/EXIT,对照 handler 设旗版在 SA_RESTART 下旗子立了没人看还多接一条连接),收尾是十个信号设施到 x86-64 系统调用编号的速查表"
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
---

# 信号(下):实时信号、signalfd 与 pidfd

[上一篇](./04-signal-basic.md)咱们把 handler 的世界立起来了:sigaction 装得上也拆得下,handler 里合法的动作只剩 `write`、`_exit` 这类异步信号安全的少数派,阻塞的调用被信号打断之后,EINTR 与 SA_RESTART 的配对也交代清楚了。上篇的 E5 咱们还搭过 self-pipe:handler 里只 `write` 一个字节进管道,主循环 poll 的是管道,信号就这么变成了事件。当时笔者留了一句,这套用户态的仿制品是有内核原生替代品的,这一篇咱们把它请出来,而且不止它一位。

本篇要回答的问题只有一句:handler 之外,信号还有哪些更好的消费方式?上篇结尾留的那半句正好从这儿接上:实时信号的那部分,留给了下一篇。排队的课现在补:同一个号连发三次的记录不再被合并,而且还能捎带一份小的数据。咱们再往下问一层:handler 本身能不能干脆不装?signalfd 把信号本身变成了一个 fd,您 read 它、poll 它,信号与定时器从此进了同一张 poll 表,sigwaitinfo 则把取信号的主动权交还给主流程,在指定的点亲手取。进程那一头也有对等的题:等子进程退出,能不能也按 fd 等?pidfd 把这件事句柄化了,咱们 poll 一个 fd 可读,就是子进程退出了,发信号、收尸、拿对方的 fd 全都走句柄。这几样最后攒进一台真的 mini prefork echo 服务器(prefork 说的是在干活以前提前 fork 好一组 worker),把优雅关闭的全时序跑给您看。

实验的编号是 E1 到 E6,与仓库 `code/volumn_codes/vol8/systems-programming/linux/process/05-signal-advanced/` 下的 01 到 06 六个目录一一对应,代码连同全部原始输出都收进了存档,您随时能对表。正文里的输出块多数是节选,被删掉的场景头与说明行以 `...` 标出,拿存档对表的时候请以存档为准。本篇的 E 只认本篇,上篇自己的 E 系与咱们互不相干,您翻存档的时候认目录号就好。存档里的实验代码大多贴着裸系统调用写,图的是把机制摆在明面上,而工程里这些 fd 交给 `unique_fd` 管就好,那一手在 [RAII 篇](../../thinking/01-raii-paradigm.md)早就备好了。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2 的构建,CPU 用的是 AMD Ryzen 7 9700X,g++ 的版本是 16.2.1,编译的口径一律 `-std=c++20 -Wall -Wextra -O2`,glibc 的版本是 2.44,全部实验拿到的警告数是零。有三条环境事实直接决定了几处说法的形状。本机 `/proc/sys/kernel/yama/ptrace_scope` 的值是 1,E3 里 pidfd_getfd 的权限边界就按它算。本机根 pid namespace 的 `pid_max` 是 4194304,复用竞态的复现因此要借 namespace,E3 里咱们细说。E3 的 marker 文件路径写死在 `~/lp05_scratch/e3/`,程序缺了会自建同内容的文件,复跑的流程不受影响。全部 `.out` 出自 2026-10-04 的同一轮,pid、端口号、毫秒时间戳的数值每次复跑都会变,咱们引用的是次序与档位,不是任何具体的数值。

## E1:实时信号:排队、带数据、到达顺序

### 范围:34..64,中间两位让 glibc 用掉了

实时信号(real-time signal)说的是编号区间 SIGRTMIN 到 SIGRTMAX 的那一段,它法定的待遇是排队,这也是它与标准信号最大的区别。咱们本机探出来的范围长这样:

```text
[a] 范围:SIGRTMIN=34 SIGRTMAX=64 __SIGRTMIN=32
    libc 的 SIGRTMIN 比内核 __SIGRTMIN 大 2:NPTL 内部占用 32/33 两号,
    用户可用的实时信号共 31 个(34..64)。本实验用 SIGRTMIN+1=35 与 SIGRTMAX=64
```

内核原生的区间起点 `__SIGRTMIN` 是 32,而咱们从 `<signal.h>` 里读到的 SIGRTMIN 却是 34。中间的 32、33 两号让 NPTL 用掉了,它是 glibc 的原生 POSIX 线程库,内部拿两号去做线程方面的管理。man 7 signal(7) 的叮嘱也值得抄下来:编号别硬编码进代码,您要写就写 `SIGRTMIN+n`,运行的时候还得跟 SIGRTMAX 比一比,不然换个 libc 实现就对不上了。

### 排队:三发三到,标准信号三发只记一次

排队(queuing)咱们得说清楚:同一个号连发多次的时候,内核每次留下的都是一条新记录、不存在合并这回事。上篇 E1 咱们实测过标准信号的另一副脾气:阻塞期连发 3 次 SIGUSR1 的时候,pending(已发出、还没投递的挂起状态)的位图上只立了一位,解阻塞之后 handler 也只跑了 1 次。那实时信号的表现呢?咱们把 SIGRTMIN+1 与 SIGUSR1 都阻塞住、各连发 3 次、再一起解阻塞:

```text
[b] 阻塞期连发 3 次,解阻塞后:
    SIGRTMIN+1(sigqueue×3,编号 101/102/103) → handler 共跑 3 次
      第 1 次:signo=35 si_code=-1 sival_int=101 sival_ptr=0x65
      第 2 次:signo=35 si_code=-1 sival_int=102 sival_ptr=0x66
      第 3 次:signo=35 si_code=-1 sival_int=103 sival_ptr=0x67
    SIGUSR1(kill 语义×3,编号 1/2/3) → handler 共跑 1 次
      第 1 次:signo=10 si_code=-1 sival_int=1 sival_ptr=0x1
```

咱们看,同一段程序交出了两种答案。35 跑满了 3 次,101/102/103 三条全都到了,到达的次序也与发出的次序一致,这正是 FIFO(first in first out)的脾气,发出的次序就是到达的次序。10 只跑了 1 次,后两发的影子都没留下。发送的接口也有讲究:这三发用的是 `sigqueue(2)`,它是 kill 的加强版,第三个参数带的是一个 `union sigval`,联合体里能装的要么是一个 int,要么是一个指针。handler 那头只要装的是 SA_SIGINFO 风格,咱们就能从 `si_value` 原样读回。

带数据的部分还有一个细节,您看见了别当成 bug:`sival_int=101` 的同一行,`sival_ptr` 打出来的却是 `0x65`,而它就是 101 的十六进制。而 `union sigval` 本身就是个联合体,101 与 0x65 是同一份字节换了种读法。E1 的 [c] 场景反过来发指针:`sival_ptr` 装的是本进程一个静态局部变量的地址,handler 里解引用读到了 4242,同进程内的指针是活的。当然,发送方若在别的进程,这个值就成了单纯的地址数字,解引用是没有意义的,跨进程通信您还是得带 int。

### 到达顺序:出队小号在前,handler 的执行却倒挂

顺序问题是本篇的第一个意外,咱们给它单独立一节。直觉里的画面是:内核投递讲次序,号小的排前面,那 handler 的执行也该从小号一路排到大号。可实测的第一组数据就把这个预期打翻了:

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

入队的时候 64 排在最前面,执行时 64 也跑在了最前面,看起来倒像大号排前面?可单凭这一组数据,咱们分不清到底是内核挑了大号,还是别的什么在起作用。笔者在这里被直觉带着走了好一阵,直到把另外两路的证据补齐了才敢定案。

d2 把 handler 期间的 `sa_mask` 设成了全屏蔽,handler 执行的时候其他号一概进不来、一次只放一个进来。同一批 pending 的四个号(SIGUSR1、SIGUSR2、SIGRTMIN、SIGRTMIN+1)入队的次序是 12、35、10、34,实际执行的次序却是 10、12、34、35。d3 干脆不走 handler 了,用 sigwaitinfo 逐个地取,同一批 pending 取出的也是 10、12、34、35。三路证据指向了同一件事:内核的出队是小号在前。man 7 signal(7) 的原话把这次序限定在实时信号之间:不同实时信号之间的投递,号小的排在前面。标准信号相互的次序 man 页没有规定,咱们看到的 10 排在 12 前面,靠的是 d2/d3 两路的实测背书。咱们还注意到 10、12 两个标准信号排在了 34、35 两个实时信号前面,POSIX 对混合 pending 的次序没规定,而 Linux 的做法是让标准信号排前面,这与 man 页的说法也对上了。

那 d1 的倒挂是怎么来的?咱们到栈上找机制。空 `sa_mask` 意味着解阻塞的返回路上,内核会按次序逐个挑 pending 的信号,每挑中一个就构造一个信号帧压上被中断线程的栈。头一个挑中的是 35(201),它的帧压了进去,而 35 一旦处于投递中,这个号就被自身挡住了,下一轮的挑拣只能落在 64 头上,64 的帧就叠在了 201 的上面。回到用户态的时候,执行权落在栈顶的那一帧,于是 64 的 handler 跑到了最前面,这就是倒挂的全部机制,内核并没有挑大号的记录。201 的 handler 退场、帧被弹出之后,35 才回到可投递的状态,202 的帧这时压进来,203 也照这个节拍随后到:同一个号的三帧是三次独立的投递、各自到达、各自被挡、再各自放行、从来不曾一批压进栈里,所以 201/202/203 的次序纹丝不动,FIFO 在两种配置下都是严格成立的。

工程上的守则咱们记下:跨号的执行次序别去依赖 handler,您要按次序消费信号的话,sigwaitinfo 与 signalfd 才是合适的工具,它们交付的时候按出队次序,号小的在前,咱们马上就见到。

## E2:signalfd:把信号变成 fd

### 纪律:阻塞在前,创建在后

signalfd 是 Linux 2.6.22 起就有的系统调用,一句话:给您一个 fd,信号到达以后就不再跑 handler 了,而是变成这个 fd 上可读的数据,您 `read` 它、`poll` 它。man 2 signalfd 对使用的前提写得明白,按它的说法,您想从 fd 收下来的信号,您得提前拿 sigprocmask 把它阻塞住。为什么要这样?咱们不背文档,直接把反例做出来:咱们给 SIGUSR2 装上 handler、不做阻塞、就创建 signalfd、然后发一个信号过去:

```text
[a] 未阻塞就创建 signalfd,再发 1 个 SIGUSR2:
    handler 计数=1(信号被传统路消费了)
    poll(signalfd, 200ms) 返回 0,revents=0 → signalfd 上什么都没有
```

信号来了,handler 的计数是 1,而 `poll(signalfd, 200ms)` 空等到超时。没被阻塞的信号照旧走了传统路,handler 当场把它消费掉了,而 signalfd 那边什么都没有发生。漏信号就是这么来的:信号没有被谁弄丢、它是被另一条路消费走的,您等的那个地方永远等不来。所以纪律是把阻塞做在前头、创建放在后头。这两步要是颠倒了,fd 就成了摆设。

读走即消费这话还有反向的一半,咱们两头都实测了。[b] 场景按纪律阻塞了再创建,连发了 3 个 SIGUSR2,标准信号只 pending 了一次,poll 立刻就有了响应,一次 read 返回了 128 字节、恰好一条记录。妙就妙在随后的解阻塞,handler 的计数仍然是 0:

```text
[b] 先阻塞再创建,连发 3 个 SIGUSR2(标准信号只记 1 次):
    poll 返回 1,revents=0x1(POLLIN=0x1)
      读 signalfd:一次 read 返回 128 字节 = 1 条记录
        [1] ssi_signo=12 ssi_code=SI_USER(0,来自 kill) ssi_pid=39851 ssi_uid=1000 ssi_int=0
    解阻塞后 handler 计数仍=0 —— 从 signalfd 读走的信号不会再进 handler
```

man 2 signalfd 的原话是:read 的后果是这些信号被消费,不再 pending 了,handler 抓不到它们了,sigwaitinfo 也取不到了。咱们手里 [a] 与 [b] 一正一反,互斥的证据是双向的:handler 与 signalfd 抢的是同一条 pending 队列,读得早的一方就消费掉了,不存在两边都收到的好事。上篇 E5 的 self-pipe trick,handler 里的活只是往管道 write 字节,主循环 poll 的是管道,那是咱们在用户态手工仿制的事件化。signalfd 把同一件事做成了内核原生:handler 不用了,管道也省了,字节协议也不用编了,信号生来就是事件、不用咱们再动手脚。

### 一次 read 多条记录,字段怎么看

signalfd 上读到的不是字节流。它给出的是一条条定长的 `signalfd_siginfo` 记录,本机每条的长度是 128 字节。咱们在 [c] 场景里让子进程发一个 kill 加两个 sigqueue,而且故意把 37 发在前面、36 发在后面:

```text
[c] 字段解读:fork 子进程发信号(跨进程,ssi_pid 有看头):
      子进程发完后的 read:一次 read 返回 384 字节 = 3 条记录
        [1] ssi_signo=12 ssi_code=SI_USER(0,来自 kill) ssi_pid=39853 ssi_uid=1000 ssi_int=0
        [2] ssi_signo=36 ssi_code=SI_QUEUE(-1,来自 sigqueue) ssi_pid=39853 ssi_uid=1000 ssi_int=22
        [3] ssi_signo=37 ssi_code=SI_QUEUE(-1,来自 sigqueue) ssi_pid=39853 ssi_uid=1000 ssi_int=31
    (ssi_pid=39853 是发送方=子进程;36/37 乱序入队(先 37 后 36),出队按小号先——同 E1)
```

一次 read 拿回了 384 字节,3 条记录全都到齐了。跨进程发送时 `ssi_pid` 是发送方的 pid,`ssi_code` 分得出 kill 的 SI_USER 与 sigqueue 的 SI_QUEUE,带过来的 `ssi_int` 也原样在。入队的次序是 37 在前、36 在后,读到的却是 36 在前,出队按的是小号在前,与 E1 的 sigwaitinfo 那一路完全一致。字段名与 handler 里 `siginfo_t` 的对应关系,咱们列成了一张表:

| siginfo_t(handler/sigwaitinfo 读) | signalfd_siginfo | 本实验的取值 |
| --- | --- | --- |
| `si_signo` | `ssi_signo` | 12/36/37 |
| `si_code` | `ssi_code` | SI_USER=0(kill)/ SI_QUEUE=-1(sigqueue) |
| `si_pid` | `ssi_pid` | 发送方 pid |
| `si_uid` | `ssi_uid` | 1000 |
| `si_value.sival_int` | `ssi_int` | sigqueue 带的 int |
| `si_value.sival_ptr` | `ssi_ptr` | sigqueue 带的指针值 |

结构体里其余的 `ssi_errno`、`ssi_fd`、`ssi_tid` 一众字段,对应的是定时器、异步 IO、硬件错误这些别的来源,本实验是触发不到它们的,您在 man 页里能查到它们各自的含义。

### 与 timerfd 混挂:事件循环里的信号

单看 signalfd 的话,表里挂着的只有它一个,还成不了事件循环,咱们在 [d] 场景把它与 timerfd 挂进了同一个 poll。timerfd 是定时器的 fd 化,周期走的是 150ms 一档,而子进程在 200ms、400ms 两个点 sigqueue 信号进来:

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

您看 151、202、301、402 这几拍,是定时器与信号在同一个循环里交错就位的记录、谁也不打断谁。上篇 self-pipe 要 handler、要管道、要字节编码才做到的事,这里一张 poll 表就收下了。poll 在咱们这儿只是最朴素的一档,epoll 的全景与事件循环的架构是多路复用篇的正题,vol5 的[异步 IO 与事件循环](../../../../vol5-concurrency/ch06-async-io-coroutine/04-async-io-and-event-loop.md)也早就铺过路,咱们在这儿不重开课、只留一句:信号到了 signalfd 这里,就已经和 timerfd 一样是能 poll 的 fd 了。

[e] 场景补一个使用细节:掩码是活的。对已有的 sfd 再调一次 `signalfd(sfd, &mask, flags)`,往集合里加号就行了,fd 也不用换新的,实验里新加的 SIGRTMIN+4 一发即中。flags 的实测行为也值得记一笔,笔者拿 fcntl 单独探过:创建的时候带 SFD_CLOEXEC、改掩码那次不带,`FD_CLOEXEC` 的位还在,创建的时候不带、改掩码的时候带上,补上的事也没有发生。close-on-exec 的属性在创建那一刻就定了性,改掩码是动不了它的,您写代码的时候按创建的意图给足 flags 就好、别指望后补。

## E3:pidfd:把进程也变成 fd

信号 fd 化了之后,咱们把镜头转向进程。pidfd 的思路说来简单:进程也可以是一个 fd。它是三个系统调用加一个等待选项凑成的一整条路:`pidfd_open`(x86-64 编号 434)拿一个指向目标进程的句柄,`pidfd_send_signal`(424)管的是按句柄发信号,`pidfd_getfd`(438)拿目标进程 fd 表里的描述符,而 `waitid` 配上 `P_PIDFD` 这档 idtype 之后,收尸也就按句柄走了。

> man 2 pidfd_open 的页面说 glibc 没有提供封装,真要调用就得走 `syscall(2)` 了。这句话已经过时了:本机的 glibc 2.44 实际带着 `<sys/pidfd.h>`(glibc 2.36 起就有),C 里这套头文件是开箱即用的,可这版的头文件没包 `extern "C"`,C++ 直接拿去调用是链不上的,咱们得自己包一层才链得上。实验代码走的仍然是裸 syscall,一来咱们想把 434/424/438 三个编号摆在明面上,E6 的速查表要跟它们对表,二来也正好绕开了包裹的别扭。文档与现实各对一半的时候,咱们就按实测说话。

### 第三种收尸:poll 加 waitid(P_PIDFD)

收尸(reap)咱们在[进程篇的开篇](./01-fork-exec.md)已经打过交道:父进程 waitpid 拿到了退出状态,子进程的僵尸态这才算退场。传统的路子有两条:waitpid 拿 pid 数字做的是阻塞等或轮询,而 SIGCHLD 走 handler 做异步提醒,提醒到了再循环 waitpid。而 pidfd 是第三条路。咱们在 [a] 场景 fork 了一个子进程、让它 300ms 之后自己退出,而父进程全程只 poll 那个 pidfd:

```text
[a] 第三种收尸:pidfd + poll + waitid(P_PIDFD)
    fork 出 c1(pid=678699),pidfd_open → fd=3;子进程 300ms 后退出
    t= 51ms poll(pidfd, 50ms)=0(活着时无事件)
    t=301ms poll 就绪,waitid(P_PIDFD) → 退出码 42(si_code 路径:WEXITED)
    收尸后再 poll(pidfd)=1,revents=0x11(=0x1|0x10 POLLIN|POLLHUP:人已注销,句柄等价于挂断)
    二次 waitid → No child processes(尸已收过;waitpid 二次收也是这个错)
```

活着的时候 poll 空等,退出的那一刻 pidfd 变得可读,waitid(P_PIDFD) 拿到的退出码是 42,全程既没有 SIGCHLD 的戏份、也没有轮询。收尸之后还有两笔可看的:再 poll 一次得到的是 `POLLIN|POLLHUP`,句柄指向的进程已经注销,等价于线路的挂断。再 waitid 拿到的会是 ECHILD,与 waitpid 对同一具僵尸收两次的报错一个样,死就是死了,句柄不会让您收两回。

[b] 场景顺手验证了 `pidfd_send_signal`:按句柄发 SIGRTMIN+1、带上了 `sival_int=777`,收方的 sigwaitinfo 原样收到了,连带的 si_value 也分毫未少。这里有一个实测的小暗礁:info 参数非空的时候,内核是不会代填 `si_pid` 的,您不写,收方看到的发送方就是 0。实验里补了一句 `getpid()`,678698 这才出现在了对端。man 2 pidfd_send_signal 只说 info 按 rt_sigqueueinfo(2) 的口径由调用方自己填,这半句的沉默,咱们用实测补上了。

### pidfd_getfd:拿别人的 fd,yama 说了算

`pidfd_getfd` 干的事听起来有点越界:它把目标进程 fd 表里的描述符复制一份,送到您自己手里。咱们在 [c] 场景让子进程只读打开 marker 文件(那行字是预置的靶子内容,程序缺了文件会自建同内容的),父进程凭 pidfd 把它的 fd 拿了过来,`pread` 直接读出了 `PIDFD-GETFD-MARKER-31415926`。兄弟进程之间做同样的事,拿到的却是 EPERM:

```text
[c] pidfd_getfd(本机 yama/ptrace_scope:1):
    父进程:pidfd_getfd(c3 的 fd=3) → 5,pread 出:「PIDFD-GETFD-MARKER-31415926
」
    兄弟 c4:pidfd_getfd(c3 的 fd) → -1,errno=Operation not permitted(非后代,被 yama 拦)
```

yama 是 Linux 的一个安全模块,`/proc/sys/kernel/yama/ptrace_scope` 就是它的档位旋钮,本机的值是 1,含义是只许对直系后代动用 ptrace 一类的挂接能力。man 2 pidfd_getfd 写明权限检查走的就是 ptrace 的同一套(PTRACE_MODE_ATTACH_REALCREDS),跨 uid 的门槛 CAP_SYS_PTRACE,依据写在它转引的 ptrace(2) 里。所以父到子通、兄弟被拦,边界完全按 yama 的档位来,您在自己机器上复跑以前,值得看一眼这个旋钮的值。

### pid 复用竞态:数字会认错人,句柄不会

pidfd 真正要解的题在 [d],咱们把那场戏完整搬出来。kill(2) 拿的是 pid 数字、靠数字找人,可数字是内核循环分配的,进程死了、尸收了,数字是会被回收再分配的。窗口就在这儿:您手里攥着 301 这个旧数字,而 301 已经换成了新的进程,这时 kill 一声、挨打的就是个无辜的路人。说起来倒是容易,复现却有讲究:本机根 pid namespace 的 pid_max 是 4194304,pid 又是单调递增的,绕完一圈要二十分钟的量级、等不起。所以实验借了 namespace 的力:user namespace 加 pid namespace,是内核把进程编号隔离出一套独立世界的机制,咱们在新 pidns 里把 pid_max 压到了 302,合法的最小值是 301。还有一层讲究:分配器的游标一旦越过 RESERVED_PIDS(内核给 pid 分配器留的保留下限 300),下限就固定在了 300,低号段 2..299 从此不再分配了,所以咱们得把低号段烧掉,让目标进程落进会循环的 300 到 301 段:

```text
[d] pid 复用竞态:新 user+pid namespace,pid_max 调到 302:
    [ns 内] pid_max=302;先烧掉低号段(2..299 共 298 个),让 A 落进会循环的段:
    [ns 内] A(pid=301)已退出并收尸,pidfd=4 还在手上
    [ns 内] 第 2 个候选 B 拿到了老数字 pid=301 —— 数字回来了,人不是 A
    [ns 内] kill(数字 301) 返回 0 → 误伤 B;pidfd_send_signal(老句柄) 返回 -1,errno=No such process
    [ns 内] 数字会被复用,句柄不会认错人:这就是 pidfd 的立身之本
    [ns 内] B(pid=301)收到 signo=10 si_pid=1 —— 我不是 A,被误伤了!
```

A 退出并被咱们收尸之后,老数字 301 分给了新的进程 B。此刻 `kill(301)` 返回的是 0,信号真的发出去了,收到的却是 B,它一脸无辜地打印出 si_pid=1,这个 1 是发送方的编号:新 pid namespace 里的 1 号,正是执行 kill 的实验进程自己,清白都写在输出里了。而同一时刻,咱们手里那个指向 A 的老 pidfd 走 `pidfd_send_signal`,返回的是 -1 加 ESRCH。man 2 pidfd_send_signal 对 ESRCH 的解释正合适,原文说的是目标进程不存在。笔者的翻译是:它已经终止,并且被 wait 过了。pidfd 拿的是对内核进程对象的引用,人没了,句柄就指向一段注销的历史,绝不会认错新的主人。waitpid(pid) 与 kill(pid) 拿的同样是数字,同样的毛病它们也都有,只是平日里的窗口小,大家没碰上而已。

三条收尸路径咱们并排放着,各自的位置一目了然:

| 路径 | 依据 | 何时知道退出 | 拿退出状态 | 典型场景 |
| --- | --- | --- | --- | --- |
| `waitpid(pid,...)` | pid 数字 | 阻塞等或轮询 | 返回值加 status 宏 | 顺序脚本、父进程管少数孩子 |
| SIGCHLD handler | 信号(可能合流) | 异步提醒 | 循环 `waitpid(WNOHANG)` | 传统服务、shell |
| pidfd | 句柄(数字复用免疫) | poll 可读 | `waitid(P_PIDFD)` | 事件循环统一调度 |

SIGCHLD 可能合流这件事是上篇 E6 的实测:三个子进程同时退出,pending 的位图只立了一位,handler 只进了一次,所以 handler 里面必须循环收。而 pidfd 没有这个负担,一人配一个句柄、各收各的。waitpid 与状态宏本身的细讲,咱们留给进程篇的开篇,这里只讲 pidfd 的新路。

## E5:sigwaitinfo:不开 handler,在指定的点亲手取

正文走到这儿的时候,出场顺序与编号脱了一次节:E4 是压轴的整合实验,咱们把它排在了 E5 的后面,编号跟着存档的目录走,您对表的时候别迷路。

sigwaitinfo 是消费信号的第三条路:handler 的路子是异步抢跑,谁也不知道它什么时候插进来,而 sigwaitinfo 把主动权翻转了过来:信号被挡在了外面,主流程走到自己选定的点、伸手把它取回来,装 handler 的这件事全程没有发生。咱们在 [a] 场景阻塞了 SIGUSR1 与 SIGUSR2,子进程发一个带值的 sigqueue 加一个 kill、主进程取两次:

```text
[a] 阻塞 {SIGUSR1, SIGUSR2},子进程(pid=677793)发了两个信号,主进程同步取:
    第 1 次 取到 signo=10 si_code=SI_QUEUE si_pid=677793 si_value=11
    第 2 次 取到 signo=12 si_code=SI_USER si_pid=677793 si_value=0
    (没装任何 handler:信号被挡在门外,由我在指定的点亲手取)
```

si_pid、si_value 全都到了手,与 handler 里能读到的信息一模一样,只是时机换成了您来定。带超时的版本叫 sigtimedwait,咱们在 [b] 场景让它空等 200ms,返回的是 -1 加 EAGAIN,实测的耗时恰好 200ms。timeout 传 nullptr 就退化成 sigwaitinfo 的永久等,传零时长就是一次非阻塞的清点。[c] 场景把 E1 的排队判据又过了一遍:实时信号的三发三取都按 FIFO,标准信号的三发只清点出一次,换了个消费的接口,咱们的说法一个字都没变。

真正值得您多看一眼的是 [d]:sigwaitinfo 与 signalfd 抢的是同一条 pending 队列,读走的那一方就消费掉了,咱们双向都实测了:

```text
[d] sigwaitinfo 与 signalfd 抢的是同一条 pending 队列:
  d1) 先 sigwaitinfo 一条,再 read(signalfd):
      sigwaitinfo 取到 si_value=21
      read(signalfd) 返回 1 条,ssi_int=22 —— 剩下那条也在这儿
  d2) 反过来,先 read(signalfd) 一条,再 sigtimedwait:
      read(signalfd) 返回 1 条,ssi_int=31
      sigtimedwait 取到 si_value=32 —— 队列不重复,谁读谁消费
```

入队了两条:sigwaitinfo 吃掉了一条,signalfd 那里就只剩另一条了,反过来也是一样的、不重复也不打架。工程上的含义就一句:同一个阻塞集合,您别同时开两个消费者、选一个用到底就好。到这里三条出路都齐了:handler 的异步抢跑(上篇),sigwaitinfo 的定点同步取,signalfd 在事件循环里的 fd 用法。同一个阻塞集合对应三种消费的姿势,您选哪种看程序形态:专职的信号线程用 sigwaitinfo,事件循环的场景用 signalfd,改动最少的老代码就留给 handler。

## E4:优雅关闭:把 signalfd 与 pidfd 攒进一台真的服务器

压轴实验是一台真的 mini prefork echo 服务器:三个 worker 是提前 fork 好的,父进程只做 accept 收连接的活,把连接经 SCM_RIGHTS(unix 域套接字上把 fd 本身发给对面的机制)派给 worker,而 worker 的日常是读一行请求、干 250ms 活、回一行。咱们把关停信号 SIGTERM 与 SIGINT 按纪律阻塞在前、挂上 signalfd,发 SIGTERM 的是 supervisor 子进程,时间定在了 t=300ms,client 子进程的剧本是在 t=50/150 各下一单,t=500 的时候再试图连一条。咱们整台机器里一个 handler 都没有用。

### 对照组:handler 设旗,SA_RESTART 开与不开的两种结果

咱们现在把朴素版过一遍,主角的登场留到下一节:主循环阻塞在 `accept()`,SIGTERM 的 handler 只做两件安全的事,把 `g_stop` 的旗置起来、再写一个字节。朴素版另跑了一套自己的剧本,两条连接定在了 t=50 与 t=400,SIGTERM 定在了 t=200,与 signalfd 版的那套互不相干,对表的时候请认它自己的时戳。面孔一开了 SA_RESTART:

```text
[面孔一] SA_RESTART:信号打断 accept 后内核自动重启,旗子没人看
...
handler 跑了:g_stop=1(handler 里只有赋值+write 两件安全的事)
t=503ms [面孔一] 接了第 2 条连接,干完 50ms 活,回了 ack
t=503ms [面孔一] 循环回来看旗:g_stop=1 → 退场(共接了 2 条)
...
```

t=200 信号就到了,handler 也真跑过了,可 SA_RESTART 让内核把打断的 accept 原地重启,主循环就没机会看旗了。它一路睡到了 t=503 下一条连接来,把不该接的连接接了、干完了、回完了才看见旗子退场。关停被拖了 300ms,还多服务了一条请求。面孔二不开 SA_RESTART:accept 返回的是 EINTR,主循环当场看了旗就退场,t=956 的新连接被拒,这是对的。可您别急着满意:面孔二仍然要 handler、flag、每个阻塞点都查旗的完整配对,accept、read、recv 各有一处这样的岗,漏了一处就是面孔一的翻版。这套配对的税,每加一个阻塞调用都得再交一遍的。

### 状态机:RUNNING 到 DRAINING 到 REAPING 到 EXIT

signalfd 版把信号变成了 poll 表里的一个 fd,关停就成了普通的状态迁移。状态机(state machine)咱们说得直白些:一组命名的状态加迁移的条件,程序在任何时刻都只住在其中的一个状态里。本篇的四个状态是 RUNNING、DRAINING、REAPING、EXIT,一个 `g_state` 变量加一张跟状态变的 poll 表,就是全部的家当。完整时序您值得逐行读:

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

咱们把 t=301 这一刻放大了看。SIGTERM 走的是 signalfd,它是事件循环里的一个事件,与一条新连接、一次定时器到期没有任何地位的差别。循环读出了 signo=15,状态从 RUNNING 迁到了 DRAINING,咱们要做的两件事:一件是 close 掉 listen fd、让门外的连接从此吃 refused,另一件是给三个 worker 发 drain 的指令。drain(排干)的语义就一行字:干完手头的在途活,不接新的活。w2 手上没活、当场就退了场,w0 的单恰好干完也跟着退,w1 的在途单是 SIGTERM 之后才做完的,drain 放它做完、回了 ack 才退。三个 worker 各配了一个 pidfd,DRAINING 状态的 poll 表挂的就是它们,谁的可读谁退出,waitid(P_PIDFD) 把它们逐个收了尸,全员到齐了就迁 REAPING、再清理进 EXIT。t=503 client 再连的时候,connect 返回的是 Connection refused,门是真关上了,而 t=403 的在途请求 ack 也一分不差地送到了。

整条的时序里,没有任何一行代码活在 handler 的约束之下。咱们不用挑异步信号安全的函数、不用查旗、也不用配对 EINTR。咱们把信号问题翻译成了状态迁移的问题,这就是 signalfd 加 pidfd 攒在一起的意义。

### 开发中真踩到的两处

开发的过程中服务器真栽过两个跟头,都值得咱们原样记下。头一个跟头出在继承上:fork 出来的 worker、supervisor、client 全都继承了父进程的 listen fd,而 listen socket 属于打开文件描述,引用的计数在所有持有者之间共享,父进程的 `close(listen)` 只减了一次引用,监听其实根本没停,t=503 的 connect 本该照样成功,然后就没人 accept 了、客户端挂死在 read 上。修法很简单:每个子进程进门的时候头一件事,就 close 掉自己手上的 listen fd。第二个跟头出在 SIGPIPE 上:关停后 connect 失败了,client 的测试代码照样往下 write,写进的是一条已断开的连接、送来 SIGPIPE,默认的动作是直接终止进程,客户端后面的日志一行都没了,悄无声息地就死了。上篇 E7 讲过 SIGPIPE 的默认动作与 SIG_IGN 之后 write 拿 EPIPE 的出路,这里就是它在工程里真会发生的现场,client 里一句 `signal(SIGPIPE, SIG_IGN)` 就解决了。

## E6:设施速查:一张 syscall 编号表

上下两篇里 fd 化与句柄化了的十件设施,咱们按底层系统调用归拢成了一张表,kill、waitpid、SIGCHLD 这些不走 fd 路的老设施不占表行,它们的活儿上篇与本篇的正文都交代过了。x86-64 的编号都在本机用 `<sys/syscall.h>` 实测打印过(rt_sigtimedwait 在 x86-64 原生就是 128,32 位用户态才走的 time64(421) 变体不收进表里),存档的 `06-facility-map/facility_probe.out` 里绝大多数设施配了一次真实调用的返回值,waitid 那一行转引的是 E3 已实测的收尸链路,您可以拿它当可用性探针:

| 设施 | 底层(x86-64 编号) | 角色 | 篇目 |
| --- | --- | --- | --- |
| `signal(2)` | rt_sigaction(13) 的 glibc 包装 | 兼容入口 | 上篇 E2 |
| `sigaction(2)` | rt_sigaction(13) | 装 handler 的正门 | 上篇主线 |
| `sigqueue(2)` | rt_sigqueueinfo(129) | 带值发送,实时信号排队 | 本篇 E1 |
| `sigwaitinfo(2)`/`sigtimedwait(2)` | rt_sigtimedwait(128) | 同步点取信号 | 本篇 E5 |
| `signalfd(2)` | signalfd4(289) | 信号变 fd | 本篇 E2/E4 |
| `timerfd_create(2)` | timerfd_create(283) | 定时器也变 fd | 本篇 E2 |
| `pidfd_open(2)` | pidfd_open(434) | 进程句柄化 | 本篇 E3 |
| `pidfd_send_signal(2)` | pidfd_send_signal(424) | 按句柄发信号 | 本篇 E3 |
| `pidfd_getfd(2)` | pidfd_getfd(438) | 拿目标进程的 fd | 本篇 E3 |
| `waitid(P_PIDFD)` | waitid(247) 加 idtype=P_PIDFD(3) | 句柄收尸 | 本篇 E3/E4 |

咱们按职能来分,发送侧的三件是 kill、sigqueue、pidfd_send_signal,响应侧的三路是 handler、sigwaitinfo、signalfd,生命周期侧的三条是 waitpid、SIGCHLD,再配上 pidfd 与 waitid 的组合。您应该已经看出来了:响应侧的尽头与生命周期侧的尽头,指向的是同一个方向:一切皆 fd。文件系统的动静变 fd 是 inotify 的路子,咱们在[那一篇](../file-io/06-inotify.md)走过,定时器变 fd 的叫 timerfd,信号变 fd 的叫 signalfd,进程变 fd 的叫 pidfd。到多路复用篇的 epoll 全景里、它们就会进同一张 poll 表,调度它们的也是同一个循环,那也是 E4 的服务器往规模化走的下一步。

## 另一侧怎么看

Windows 是没有信号这回事的。C 运行库里的 `signal()` 只覆盖 SIGSEGV、SIGFPE 等寥寥几个信号的模拟,POSIX 语义的信号族在那边并不存在。Ctrl+C 在 Windows 上走的是控制台事件:系统把它变成 CTRL_C_EVENT、交给 SetConsoleCtrlHandler 注册的处理链,而且每次事件都起一条新的线程去跑 handler,与 Linux 信号打断主流程的模型完全是两回事。Windows 侧与 self-pipe、signalfd 同构的做法是 handler 里只 SetEvent 一个事件对象、主线程在 WaitForSingleObject 上等到了就醒,咱们的姊妹实验(《控制台事件与 APC》那篇的 E6)量过它,79ms 就醒了,与上篇 E5 的时序几乎一对一。至于 pidfd 操心的 pid 复用竞态,Windows 从头到尾就没有这个题目:那边管理进程靠的是 HANDLE,生来就是句柄的制度,WaitFor 系列的函数按句柄等进程、等事件、等定时器,统一的资历比 poll 还老。这一整套对照的完整展开,在 Windows 侧的[控制台事件与 APC](../../windows/process/02-console-apc.md)那篇等您。

<ReferenceCard title="参考资源">
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
