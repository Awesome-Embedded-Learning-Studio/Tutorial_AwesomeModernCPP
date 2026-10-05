---
title: "定时器全景:alarm→setitimer→timer_create→timerfd"
description: "Linux 定时器四代 API(alarm、setitimer、timer_create、timerfd)怎么选、周期任务为什么不能拿 sleep 糊,本篇六组实测加两个修订轮补测正面回答:alarm(2) 到点 2000.018ms、粒度只有整秒、每进程一笔且新约顶旧约(alarm(1) 过 300ms 再 alarm(5) 返回剩余 1,取整判别补测:剩 0.4s 也报 1、已到点报 0,向上取整排除四舍五入与截断),同一负载(2s 墙钟约 1s CPU)下 setitimer 三把尺子 ITIMER_REAL 响 200 次、ITIMER_VIRTUAL 101、ITIMER_PROF 96(VIRTUAL 大于 PROF 落在 10ms 档的采样噪声内,按口径 PROF 数的用户加系统包含 VIRTUAL 的纯用户,如实记不硬判),timer_create 四种通知形态实测(si_code=SI_TIMER 载荷原样到达、SIGEV_NONE 只倒计时到期归零无信号、SIGEV_THREAD 两次回调 tid 相邻但不同即 glibc 逐通知派新线程的独家实证、SIGEV_THREAD_ID 定向投递主线程屏蔽同信号零打扰),屏蔽信号睡 58ms 五档到期 sigwaitinfo 只收 1 个、timer_getoverrun=4 报出合并掉的四档,fork 与 exec 的分界实测:alarm(2) 把 setitimer 的 100ms 周期抹成 1999ms 一次性(getitimer 读数可见),man 里 alarm() and setitimer(2) share the same timer 的断言变成可复现实验;fork 后三件信号形态在子进程全零;exec 侧 alarm 与 setitimer 的计时器按 man 的 preserved across execve 并经补测证实(exec 后 getitimer 读到剩约 1.999s、alarm(0) 返回旧剩余 2,不撤的 exec 子进程 2 秒整死于复位的默认 SIGALRM),timer_create 按 timer_create(2) 口径撤销并删除,timerfd 活过 fork 也活过 exec(exec 子进程读到 12 档,武装起两个 600ms 窗口的累计),招牌是漂移对照:1000×1ms 的周期任务,相对 sleep 循环漂 +78.444ms(复跑 +78.028,每档开销线性滚存,min 档 1014.9µs 一千档里最短的一档也不低于标称),绝对到期补偿 +0.077ms、timerfd 周期档 +0.032ms(量级口径:相对睡眠约 78ms 对绝对锚定的 0.08ms 以下,min 902.9/562.1µs 的短档是补拍),周期调度器正确写法的全套实证与四件工具的选型矩阵"
chapter: 8
order: 3
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "信号(上):sigaction 与异步信号安全"
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
related:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "C++20 std::chrono 深度:日历与时区"
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "信号(下):实时信号、signalfd 与 pidfd"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 并发
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 定时器全景:alarm→setitimer→timer_create→timerfd

假设您手里有个每毫秒干一活的任务,不管您是采样还是刷状态,最直觉的写法十秒就能敲出来,骨架就是咱们都写过的“睡 1 毫秒,干活,再睡”。一千档的标称时长正好是一秒,可您真拿表去量,它跑了多久?笔者在本机上量到的答案是 1.078 秒,复跑了一轮,得到的还是 1.078 秒。多出来的 78 毫秒不在任何一行代码里,它长在循环的组织方式上。这 78 毫秒是打哪来的、咱们该怎么把它压到 0.08 毫秒以下,是本篇收尾那场对照实验要正面回答的事,而在那之前,咱们得把 Linux 的四代定时器挨个认全:alarm、setitimer、timer_create、timerfd,各自的粒度、通知形态、跨进程的存续与合并语义,每一项咱们都拿实测说话。

时间章的前两篇走的都是读时间:[时钟源篇](./01-clock-sources.md)量的是钟,哪一把钟量哪一种时间、读数准到了什么地步,那边都有了数字,而[chrono 日历篇](./02-chrono-calendar.md)把同一个瞬间,读成了 epoch、UTC、本地呈现的四列。钟回答的是现在几点,定时器回答的是到了点怎么叫您,chrono 日历篇的收尾处说过,下一篇咱们换成按时间办事,眼下到的就是这一篇。四代工具按出生排:alarm 是老 Unix 的整秒遗产,setitimer 的出身是 BSD 年代,把粒度带到微秒还添了两把数 CPU 时间的尺,timer_create 出自 POSIX 的实时扩展,是正经的对象化定时器,timerfd 则是 Linux 2.6.25 起的 fd 化,让定时器进了 epoll 的同一张表。信号那两篇把 sigaction、sigwaitinfo、屏蔽字的机制正面讲过了,本篇把它们当现成的工具用,timerfd 的本体课(合并计数、改期语义、fdinfo、到点精度)也在[多路复用章的 timerfd 篇](../io-multiplexing/02-timerfd-eventfd.md)上完了,本篇只把它当成家族里的一个成员来做选型对照,数字咱们引那边,这儿就不重测了。

实验的编号是 E1 到 E6,跟存档里的 t1 到 t6 是一一对应的,代码连同原始的输出都收在仓库 `code/volumn_codes/vol8/systems-programming/linux/time/03-timer-family/` 下,README 里还有逐实验的复现命令,您随时能对表。本篇的 E 只认本篇,前两篇自己的 E 系同号的也互不相干,您翻存档的时候认目录就好。正文里的输出块多数是节选,删掉的行以 `...` 标出,拿存档对表的时候请以存档为准。

环境的口径照例交代清楚,后面的数字,您都得拿它来对表:实验出自笔者的 WSL2,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,CPU 用的是 AMD Ryzen 7 9700X,g++ 的版本是 16.2.1,glibc 用的是 2.44,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra`,t2 到 t5 是涉线程的实验,编译时各自加上了 `-pthread`,t1 与 t6 是不带的,t3 还单加了一个 `-D_GNU_SOURCE`(缘由咱们在 E3d 里细说),全部实验的警告数是零。计时走的都是 CLOCK_MONOTONIC,这是时钟源篇量出来的选型,本篇也全部沿用了它。全部 `.out` 出自 2026-10-04 的同一轮,t6 另外复跑了一轮,输出收在存档的 `t6_rerun.out` 里,t1b 与 t5b 是修订轮补测的两个判别实验,出自同一台机器的同一天。信号计数与档数是每轮稳定的,微秒级的时刻逐轮在变,咱们引用的是档位与形状,而不是哪一次的具体数值。

## E1:alarm:整秒粒度,每进程一笔

alarm 是四件里最老的一件,man 2 alarm 的功能一句话念得完:“隔指定的秒数,给本进程发一个 SIGALRM”。咱们拿 t1 量它的到点:

```text
== E1a: alarm(2) 到点有多准 ==
alarm(2) 从下达到 SIGALRM: 2000.018 ms(粒度只有整秒,亚秒需求它管不了)
```

到点是准的,2000.018 毫秒没什么可挑剔的。可它的参数是整秒,您要 2.5 秒、要 500 毫秒,接口上就没有地方写。亚秒的需求,咱们得等 setitimer 出场才谈得上。

### 改约不是加约,撤销就是归零

alarm 的返回值是旧约定剩下的秒数,咱们看它怎么报这笔剩余:下好 `alarm(1)` 后睡满了 300 毫秒,再下 `alarm(5)`、读它的返回值,最后再拿 `alarm(0)` 把它撤了:

```text
== E1b: 再约一次是「改约」不是「加约」 ==
alarm(1) 过 300ms 后再 alarm(5),返回剩余=1 s(内核向上取整到整秒,300ms 后
约剩 0.7s,报成 1)
alarm(0) 撤销后过了 1.2s,SIGALRM 次数=0(0=撤销生效,第二个约定从未到点)
```

咱们看读数:真剩的约 0.7 秒,报回来的却是 1。单这一点其实还区分不了取整的规则,四舍五入给出的也是 1,咱们在 t1b 里补了两个判别点,外加一个到点的对照:

```text
[1] alarm(2) 后过 300.1 ms,实际剩约 1.70 s,alarm(5) 返回 2(向上取整给 2,截断会给 1)
[2] alarm(2) 后过 1600.1 ms,实际剩约 0.40 s,alarm(5) 返回 1(向上取整给 1,四舍五入会给 0)
[3] alarm(1) 后过 1200ms(约定已到点),alarm(5) 返回 0,SIGALRM 到货 1 次
```

三点头上的答案是一致的,咱们看到内核把剩余向上取整到了整秒,而不是四舍五入或截断的口径。这个返回值拿来当参考是可以的,拿它做精确的续约计算是靠不住的。更要紧的是它的行为:新约顶掉旧约,`alarm(5)` 生效的同时第一笔就没了,而 `alarm(0)` 是撤销,后面的 1.2 秒里零信号,收得干干净净的。

### 一进程一笔

alarm 的状态记在进程头上,同一时刻能挂的只有一笔。您想并行两个不相干的超时,它是做不到的,这是它的第二个硬限制。它今天还活跃的位置,一个是整秒粒度的看门狗,另一个是关掉 SA_RESTART 之后的 read 超时,老代码里到处是它的身影。咱们新写的代码,多半直接往后三件走了。

## E2:setitimer:同一负载,三把尺子

setitimer 把粒度从整秒带到了微秒,在 itimerval 的结构里,`it_value` 管的是首档多久到期,`it_interval` 管的是之后隔多久一档,两个字段一起归了零,撤销就完成了。它还一次给了咱们三把尺子,`which` 参数挑的就是尺子:ITIMER_REAL 数的是墙钟,连进程睡着的时间都数,到点发的是 SIGALRM。ITIMER_VIRTUAL 只数本进程用户态的 CPU 时间,到点发的是 SIGVTALRM。ITIMER_PROF 数的是用户态加系统态,到点发的是 SIGPROF。

咱们设计了同一个负载跑三遍:负载是 2 秒的墙钟、约 1 秒的 CPU,忙转的 10 毫秒与睡眠的 10 毫秒交替,三把尺子都配的是 10 毫秒一档:

```text
== E2a: 同一负载,三种计时口径各自的计数 ==
ITIMER_REAL    墙钟 2000 ms,CPU 1002 ms,信号 200 次 → 每 10ms 一档,数的是墙钟(睡也数)
ITIMER_VIRTUAL 墙钟 2002 ms,CPU 1000 ms,信号 101 次 → 每 10ms 一档,数的是本进程用户态(睡不数,系统态也不数)
ITIMER_PROF    墙钟 2010 ms,CPU 1002 ms,信号 96 次 → 每 10ms 一档,数的是用户态+系统态(睡不数)
```

REAL 的数字是整整齐齐的,它数的是墙钟,2000 毫秒的墙钟配 10 毫秒一档,正好响了 200 次。REAL 的 200 次一丝不差,周期档锚在墙钟的绝对刻度上,E6 里咱们还会见到这个锚的本事。VIRTUAL 与 PROF 数的是同一段约 1000 毫秒的 CPU 时间,落在了一百档的上下,实测的是 101 与 96,而它们数的 CPU 时间,本身就带着小幅的波动。这里有一处咱们要如实交代:按口径,PROF 数的用户加系统包含 VIRTUAL 数的纯用户,单次跑出来的却是 96 小于 101,差的 5 档咱们记在 10ms 档宽量级的采样噪声里,真要长期地数下来,按口径来说 PROF 是不该小于 VIRTUAL 的,咱们不拿一次的数字下硬判词。时钟源篇的 E5 里 tick 口径的 290 毫秒对纳秒口径的 298 毫秒,已经让咱们见过 CPU 时间两套口径的粒度差,这里的几档波动就是同一件事在信号上的样子。

咱们把用途也点上:VIRTUAL 与 PROF 是性能剖析的地基,gprof 采样的就是它们。一次性的语义咱们也验了:

```text
== E2b: 一次性(it_interval=0)与撤销 ==
一次性 50ms 后睡 150ms: 信号 1 次(1=响一次自停,不自动续期)
设 300ms 后立刻撤销,再睡 400ms: 信号 0 次(0=撤销生效)
```

咱们看语义:把 `it_interval` 留成零,得到的就是一次性,响了一次就自己停。撤销的正路是重设一个全零的 itimerval,t2 里还用了 Linux 的私有写法,把 `new_value` 直接传了 NULL,内核把它按全零处理了。man 对这个写法的劝告写得直白,原话给的是 `Don't use this Linux misfeature: it is nonportable and unnecessary.`,说的就是它不可移植也没必要,咱们入册只是为了演示,您自己写的时候走全零结构体的正路就好。归属上的限制是,每进程的每种 which 也只有一笔,满打满算的三笔,到这儿也就封顶了。

## E3:timer_create:一个定时器,四种通知形态

setitimer 与 timer_create 之间隔了一次代际更新,定时器从进程属性变成了对象。`timer_t` 拿在咱们的手里,咱们要建几个就能建几个,man 2 timer_create 写明了,每个定时器都会在内核里预占一个排队的实时信号,数量的上限归 RLIMIT_SIGPENDING 管。周期的设定换成了 itimerspec,结构跟 itimerval 是同款的,it_value 与 it_interval 的字段都换成了纳秒,挂哪把时钟也是能挑的,挑法跟时钟源篇量钟的选型是同一个问题:量周期的活,要的是不受跳变影响的钟,咱们全程 MONOTONIC。最大的一处升级,落在通知的形态上,sigevent(随 timer_create 传入、告诉内核到点后怎么通知咱们的结构体)里给了四种形态,咱们挨个跑。

### SIGEV_SIGNAL:信号带载荷

```text
== E3a: SIGEV_SIGNAL(100ms × 3,载荷 20261004) ==
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
  SIGEV_SIGNAL 到货: si_code=SI_TIMER si_value.sival_int=20261004
```

[信号上篇](../process/04-signal-basic.md)的 E2.1 里,咱们实测过三种来路的 si_code,raise 给的是 SI_TKILL,外部 kill 给的是 SI_USER,setitimer 到期给的是 SI_KERNEL。timer_create 的 SIGEV_SIGNAL 又添了一种,SI_TIMER 是它专属的码。si_value 才是它跟 alarm 拉开差距的地方,出发时塞进去的 20261004,原封不动地到达了。有了载荷,多个定时器就能共用同一个信号号了,handler 拿 si_value 就能分流了,这正是 alarm 做不到的多路定时。

### SIGEV_NONE:只倒计时,不吭声

```text
== E3b: SIGEV_NONE(300ms 一次性,每 50ms 问一次剩多少) ==
  墙钟  50.1 ms: it_value 剩 0.249 ms
...
  墙钟 250.5 ms: it_value 剩 0.049 ms
  墙钟 300.6 ms: it_value 剩 0.000 ms
  墙钟 350.7 ms: it_value 剩 0.000 ms
  到期后 it_value 归零、无任何信号 —— 到没到点,全靠自己看表
```

存档的打印把秒和毫秒拼在了同一个标签里,咱们把 0.249 读作 0 秒 249 毫秒。剩余量跟着墙钟一格一格地往下走,过了 300 毫秒的到点,读数就归了零,之后咱们再问,得到的还是零,全程没发过任何的信号。到了没到点,全得靠咱们自己拿 `timer_gettime` 看表。本来就在轮询循环里转的程序用它正合适,轮询的手本来也没闲着。

### SIGEV_THREAD:回调跑在每次现起的线程里

```text
== E3c: SIGEV_THREAD(120ms × 2) ==
  SIGEV_THREAD 回调: 载荷=77,跑在 tid 1046441
  SIGEV_THREAD 回调: 载荷=77,跑在 tid 1046442
```

SIGEV_THREAD 的字面承诺是到点跑一个回调函数。man 2 timer_create 只说这套功能大半实现在了 glibc 里,内核并没有原生的线程通知,至于线程是不是复用的,咱们不猜,拿 tid 量:两次回调跑在了相邻的两个 tid 上,偏偏不是同一个。至少在 glibc 2.44 的这套实现里,每个通知都现起了新线程去跑回调。工程上的含义跟着就来了:回调里要是摸共享状态,请按多线程的写法来,该上的锁、该用的 atomic,一样都不能省了,它可不是单线程世界里的回调函数。

### SIGEV_THREAD_ID:定向投给一个线程

第四种形态是 Linux 专有的,`sigev_notify_thread_id` 字段在头文件里藏在 GNU 扩展的后面,代码里它露出的真身是宏展开后的 `_sigev_un._tid`。g++ 是默认带着 `_GNU_SOURCE` 的,t3 显式写上是为了保险,gcc 编 C 的时候才真离不开它。这个形态让信号不再是进程里随便哪个线程都能接的,而只投给咱们指定的线程。定向的写法,咱们从 t3 里摘几行核心出来:

```cpp
sev.sigev_notify = SIGEV_THREAD_ID;   // Linux 专有,字段要 _GNU_SOURCE
sev.sigev_signo   = SIGUSR1;
sev._sigev_un._tid = target_tid.load();   // sigev_notify_thread_id 宏展开后的本体,tid 写在这里
// 主线程把 SIGUSR1 屏蔽,证明信号只进目标线程
sigset_t block{};
sigemptyset(&block);
sigaddset(&block, SIGUSR1);
pthread_sigmask(SIG_BLOCK, &block, nullptr);
```

咱们跑起来,看到的观测是这样的:

```text
== E3d: SIGEV_THREAD_ID(定向投递,150ms × 2) ==
  SIGEV_THREAD_ID 到货: 目标线程自己处理 tid 1046443
  SIGEV_THREAD_ID 到货: 目标线程自己处理 tid 1046443
  两次都由目标线程处理;主线程全程屏蔽同一个信号、一次没被吵醒 —— 定向投递成立
  (计数核对: 主线程侧 g_sig_hits=3 是 E3a 的 3 次,工作线程侧 g_tid_hits=2)
```

主线程把 SIGUSR1 整个地屏蔽了起来,两次到货全落在工作线程的 tid 上,咱们回头一核对,主线程那边是一次都没被吵醒的。在多线程的程序里,这是把定时器的影响圈进指定线程的手段,事件循环所在的线程就不会被 EINTR 搅扰了。

## E4:错过的到期:不排队,折成一个整数

[信号上篇](../process/04-signal-basic.md)的 E1 量过:标准信号不排队,阻塞期里连发了三次,pending 里留下的只有一个位。定时器的信号形态落进同一条限制,落出来的形状就是到期合并,咱们拿 t4 看它长什么样:10 毫秒一档的 SIGEV_SIGNAL,咱们把 SIGUSR1 屏蔽起来,让第一次的到期悬着,然后睡满了 58 毫秒:

```text
10ms 一档,屏蔽信号睡 58.1 ms 后取走信号:
  sigwaitinfo 收到 1 次(si_code=SI_TIMER) —— 5 档到期只送来 1 个信号
  timer_getoverrun = 4(这一信号到货时,被合并掉的多余档数)
  核对: 已到期的档数 5 = 送出的 1 + 合并掉的 4
...
```

咱们把数字对上:58 毫秒里理论上到期了五档,sigwaitinfo 只收到了一个信号,`timer_getoverrun` 报的是 4,送出的 1 加合并掉的 4,正好对上了 5。字面上就是这个语义:信号是有限的资源,内核不做补送的排队,您迟到了多久,折成一个整数交还给您。要一档不漏地补全,咱们选 timerfd,read 的返回值是累计档数,[timerfd 篇](../io-multiplexing/02-timerfd-eventfd.md)的 E3 实测过 350 毫秒睡过 100 毫秒的周期、一次 read 报 3,那边就不重测了。

## E5:fork 与 exec 之后:谁还活着

### alarm 和 setitimer 共用同一个内核计时器

man 2 alarm 的 NOTES 里躺着一句断言:`alarm() and setitimer(2) share the same timer; calls to one will interfere with use of the other.` 文档说的是两件工具共用同一个计时器、彼此干扰,咱们把它变成看得见的实验:咱们用 setitimer 武装 100 毫秒的周期,拿 `getitimer` 读出的读数是 `it_value=99ms、it_interval=100ms`,接着咱们只调一句 `alarm(2)`。再读的时候,咱们看到的是:

```text
== E5a: 同一个内核计时器 —— alarm 和 setitimer(ITIMER_REAL) 共用一笔 ==
setitimer 武装后: it_value=99ms it_interval=100ms
调 alarm(2) 之后: it_value=1999ms it_interval=0ms —— 周期被抹掉,
两件工具共用同一个计时器,后写的顶掉先写的
```

咱们喊一声好家伙:`it_interval` 被抹成了 0,100 毫秒的周期没了,`it_value` 变成 1999 毫秒的一次性。alarm 的真身就是 ITIMER_REAL 的秒级一次性写法,您调它,写的其实是同一笔内核计时器。工程上的教训很具体:同一进程里,一边是 alarm 做的看门狗、一边是 setitimer(ITIMER_REAL) 做的周期源,后写的一笔会把前面的顶掉,而且顶得一点声息都没有,连错都不给咱们报一个。

### fork 之后:三件信号形态全零

fork 与 exec 的探针 t5 分三步走,头两步问的是 fork,第三步问的才是 exec。第一步咱们单独问 alarm,在 `alarm(2)` 之后做了一次 fork,子进程里用 `alarm(0)` 撤销的同时查了剩余,返回的 0 才说明它没继承到在走的约定。第二步咱们给 setitimer 与 timer_create 各自武装了 100 毫秒的周期再 fork,父子在同一个窗口里各观测了 600 毫秒:

```text
...
[fork 子进程 A] 子进程里 alarm(0) 返回 0 s —— 0 说明 alarm 没跟过来
...
[fork 子进程 B] SIGALRM(setitimer) 0 次,SIGUSR1(timer_create) 0 次 —— 都是 0,两件都没跟过来
[fork 父进程] 同窗口内 SIGALRM(setitimer) 6 次,SIGUSR1(timer_create) 6 次 —— 武装只留在父进程
```

子进程读到的都是零,父进程各响了 6 次,武装只留在了父进程,与 man 的口径对上:计时器是记在进程身上的,fork 出来的子进程是新建的一份,不带旧的定时器。timerfd 在探针二里也武装了,它走的不是信号,咱们放到 exec 那一步一起看。

### exec 之后:计时器保留,处置复位

第三步咱们把信号形态清了场,只留下了一个 timerfd(flags=0,建的时候没带 TFD_CLOEXEC),fork 之后拿 execl 重新拉起了自己,子进程睡满了 600 毫秒,再去 read 继承来的 fd:

```text
...
[exec 子进程] 继承的 timerfd fd=3,读到档数 12 —— fd 活过了 exec
```

咱们看到 12 档的读数,fd 活过了 fork 也活过了 exec。12 档的算术也交代上:这个 fd 从探针二武装起就一直在数,exec 前后两个 600 毫秒的窗口,12 档是正好对得上的。

t5 当时的编排把信号形态清了场,默认它们过不了 exec,走查把这一层默认翻了出来:man 给的答案正好相反。alarm(2) 的 NOTES 写着 `Alarms created by alarm() are preserved across execve(2) and are not inherited by children created via fork(2).`,getitimer(2) 的 NOTES 是同款:`A child created via fork(2) does not inherit its parent's interval timers. Interval timers are preserved across an execve(2).` alarm 与 itimer 的计时器本身,是都活得过 exec 的。咱们在 t5b 里单独量了 exec 这一跳(武装放在了 fork 之后的子进程里,fork 不继承的干扰就排掉了):

```text
[A·exec 后] setitimer 武装的 2s 一次性:getitimer 读到剩 1.999111 s(interval=0ms)—— itimer 活过了 exec
[B·exec 后] alarm(2) 武装:alarm(0) 返回旧剩余 2 s —— alarm 活过了 exec
[C·父进程] 不撤的 exec 子进程结局:WIFSIGNALED=1 WTERMSIG=14(SIGALRM=14)—— 计时器活着跨过了 exec,处置复位成默认,到点就是它
```

exec 之后的子进程里,getitimer 读回的剩余还有约 1.999 秒,alarm(0) 返回的旧剩余是 2,两件都活着跨过了 exec。真正没跟过来的是处置:execve(2) 也写明了,被捕获信号的处置会在 exec 里复位成默认,于是跨过 exec 的 SIGALRM 落在默认动作上,到点的下场就是终止。t5b 的探针 C 演的就是这个下场,不撤的子进程在两秒整死于 SIGALRM。计时器活了下来,反而成了新程序里一处到点的默认终止,您在 exec 之后接手的程序里,头一件要确认的就是定时器还在不在走。

timer_create 的 POSIX 定时器,咱们得单拎出来说:timer_create(2) 写明的是它在 execve 里会被撤销并删除,保留的名单上没有它。

咱们把四件工具跨 fork 与 exec 的去留摆成一张表,实测与文档的口径分开标:

| 工具 | fork 之后 | exec 之后 |
|---|---|---|
| alarm | 子进程拿不到(实测:alarm(0) 返回 0) | 保留,处置复位(alarm(2) 口径,实测:旧剩余 2) |
| setitimer | 子进程拿不到(实测:600ms 零信号) | 保留,处置复位(getitimer(2) 口径,实测:剩约 1.999s) |
| timer_create | 子进程拿不到(实测:600ms 零信号) | 撤销并删除(timer_create(2) 口径) |
| timerfd | fd 随子进程复制,计时不停 | 存活(实测:exec 子进程读到 12 档),除非建时带 TFD_CLOEXEC |

定时器该不该跨进程地活下来,是咱们选型里实打实的一票。

## E6:周期任务为什么不能拿 sleep 糊:漂移对照

文章开头欠下的那 78 毫秒,现在咱们有了把它量清楚的全部工具。t6 给了同一个周期任务的三种写法,各自跑 1000 档、标称的每档 1 毫秒:写法 A 是相对睡眠循环,每档用 `clock_nanosleep` 的相对模式睡 1 毫秒再干活。写法 B 走的是绝对到期补偿,维护 `next += 1ms` 的绝对刻度,带着 TIMER_ABSTIME 这个 flag 睡到了刻度上。写法 C 是 timerfd 的周期档,咱们 poll 它的可读、read 走档数:

```text
== E6a: 相对睡眠循环(sleep 1ms × 1000) ==
A: 相对睡眠        总长  1078.44 ms(标称 1000 ms,漂 +78.444 ms)
                      档间隔: min=1014.9 中位=1078.3 max=1307.3 µs
...
== E6b: 绝对到期补偿(下一档 = 起点 + n×1ms) ==
B: 绝对到期        总长  1000.08 ms(标称 1000 ms,漂  +0.077 ms)
                      档间隔: min=902.9 中位=1000.0 max=1092.4 µs
...
== E6c: timerfd 周期档(1ms interval × 1000) ==
C: timerfd             总长  1000.03 ms(标称 1000 ms,漂  +0.032 ms)
                      档间隔: min=562.1 中位=999.8 max=1434.7 µs
  内核按绝对刻度摆档、错过的档折数补报(本次累计 ticks=1000,理论 1000,
  多出的部分是读间隔内合并的补拍)
```

复跑的一轮收在 `t6_rerun.out` 里,三种写法的漂移分别变成了 +78.028、+0.075、+0.029 毫秒,量级与形状都和第一轮的一致。所以口径咱们按量级说:相对睡眠的漂移约 78 毫秒,两种绝对锚定的写法都在 0.08 毫秒以下,中间隔着三个数量级的差距。

差距的机制,咱们从 A 的中位读起。相对睡眠每一档的真实间隔中位是 1078.3µs,多出来的约 78µs 是唤醒、调度加循环本身的开销。相对模式的要害在于:这一档晚醒多久,下一档的起点就整体后移多久,开销就这么一路滚存了下来,一千档线性地累积成了 78 毫秒。咱们拿数字验一遍:每档约 78µs 的开销乘上一千档,算出来的正好是 78 毫秒,与总漂移是对得上的。咱们再看 min:1014.9µs,一千档里最短的一档也比标称长,因为它做的是加法,不做任何的补偿。

咱们把写法 B 的锚换到了绝对的格子上:下一档的到点不再从上一档的实际醒来时刻起算,格子就是起点加 n×1ms 的刻度。晚醒的那几微秒不滚进下一档,下一档的到点,照旧是整格的位置,偶尔还能把上档的亏空补拍回来。总漂移缩到了 0.077 毫秒,min 902.9µs 低于 1000µs 正是补拍的痕迹:某档醒晚了,下一档到的还是原刻度的点,间隔就缩了回来。

写法 C 里咱们把摆档的活整个交给了内核,timerfd 的周期档同样锚在绝对刻度上,漂移落在了 0.032 毫秒。min 562.1µs 的短档比 B 更深,是 poll 加 read 的一档偶尔超过 1 毫秒之后、下一档按原刻度补上的形状,存档输出里说的读间隔内合并的补拍,指的就是它,两轮的累计 ticks 都恰好是 1000,每一档都对上了号。到点的精度咱们不重测,[timerfd 篇](../io-multiplexing/02-timerfd-eventfd.md)的 E4 量过 1ms 档的中位 999.3µs,复跑给的是同一个值。口径咱们也交代一下:这些是空载进程的数字,负载重了之后,max 的数字也会放宽,咱们的对照只在同为空载的条件下成立。

所以周期调度器的正确写法,咱们从 t6 的写法 B 里摘出来:

```cpp
uint64_t next = t0;                     // 锚定在起点的绝对刻度上
for (int i = 0; i < kN; ++i) {
    next += kPeriodNs;                  // 第 n 档 = 起点 + n × period
    timespec req{};
    req.tv_sec  = time_t(next / 1000000000ull);
    req.tv_nsec = long(next % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, nullptr) == EINTR) ;
    // 干活:晚醒的微秒不滚进下一档
}
```

EINTR 的重试,是咱们在[错误处理篇](../../thinking/02-error-paradigm.md)里就见过的老动作。timerfd 的写法在 E6c 里已经全须全尾地跑过了,它在事件循环里和信号打配合的样子,[信号下篇](../process/05-signal-advanced.md)的 150 毫秒心跳也演过一遍了,和管道、eventfd 同挂一张表的样子,[timerfd 篇](../io-multiplexing/02-timerfd-eventfd.md)的 E5 里也演过了。什么时候 A 也够用?粗定时的场合,比如人机界面里隔 500 毫秒的刷新,漂上几十毫秒的事没人苛求,sleep 的循环写起来最省事。可只要节拍是要紧的,采样要的是对齐、协议要的是守时,咱们就别拿 sleep 糊:把第 n 档该在 n×period 时刻到点这件事,交给单调钟的绝对刻度或者内核去守。

## 四件工具怎么挑

四代工具全见完了,选型的依据都在上面的实测里,咱们把它收成一张表:

| 维度 | alarm | setitimer | timer_create | timerfd |
|---|---|---|---|---|
| 粒度 | 整秒 | 微秒 | 纳秒 | 纳秒 |
| 同进程几笔 | 一笔 | 三笔(三种口径各一笔) | 多笔,受 RLIMIT_SIGPENDING 管 | 多笔,一个 fd 一笔 |
| 到点怎么知道 | 裸 SIGALRM | 三种固定信号 | 四种形态,带载荷可定向 | fd 可读,read 报档数 |
| 跨 fork/exec | fork 不继承,exec 保留但处置复位(实测) | fork 不继承,exec 保留但处置复位(实测) | 都不继承,exec 还要撤销(文档口径) | 都挡不住(无 CLOEXEC 时,实测) |
| 错过的档 | 合并丢失 | 合并丢失 | getoverrun 报合并数 | read 累计,一档不漏 |

读法咱们按需求走。整秒的粗超时、维护老代码,alarm 是顶用的,只是要记得它与 setitimer 共用的是同一个内核计时器。做剖析要数 CPU 时间的,正路就在 VIRTUAL 与 PROF 的身上,现代剖析器更多走 perf 的事件计数,VIRTUAL 与 PROF 就是这套想法最早的形态。要的是纳秒粒度、多笔定时、带载荷、定向投递,timer_create 是形态最全的一件,代价是绕不开信号的那套约束。定时器要进事件循环、要补全错过的档、要跨 fork 或 exec,timerfd 一件就全占了,这也是它成了四代里默认首选的原因。

## 另一侧怎么看

Windows 手边最常用的睡眠是 Sleep,而它的精度被系统定时器分辨率管着:[共享内存篇](../../windows/memory/02-shared-mem.md)在同一台机器的 Windows 侧实测过 Sleep(5),实睡了约 12.6 毫秒,默认的分辨率是 15.6 毫秒一档,做实验的时候没调 timeBeginPeriod,5 毫秒的请求向上取整到下一格。对照咱们本篇的 E6,Linux 侧 1 毫秒一档的周期任务,漂移被压在了 0.08 毫秒以下,timerfd 到点的中位是 999.3µs。

可等待的定时器对象是 SetWaitableTimer,通知的通道走的是 APC(全称为 Asynchronous Procedure Call 的异步过程调用),[控制台事件与 APC 篇](../../windows/process/02-console-apc.md)引过文档的原话,ReadFileEx 的完成例程在[OVERLAPPED 篇](../../windows/async-io/01-overlapped.md)里兑现过,SetWaitableTimer 的本体,在两侧咱们都没做过它的正面实验,按文档口径咱们记:它的句柄可等待,能进 WaitForMultipleObjects 的等待数组,与 timerfd 进 epoll 用的是同一个设计取向,Windows 10 1803 起另有高精度的变体 `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`,口径同样按的是文档。

时间章的三篇,到这里就走完了:时钟源篇量了钟,chrono 日历篇把时间读成了给人看的列,本篇把到点办事的四件工具认齐。总纲路线里讲 tty 与原始模式的那一章,是咱们的下一站。Windows 侧的镜像篇还没动工,两侧的对照,咱们暂且用各篇里另一侧怎么看的小节顶着。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="alarm(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/alarm.2.html"
  />
  <ReferenceItem
    :id="2"
    title="getitimer(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getitimer.2.html"
  />
  <ReferenceItem
    :id="3"
    title="timer_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timer_create.2.html"
  />
  <ReferenceItem
    :id="4"
    title="timer_getoverrun(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timer_getoverrun.2.html"
  />
  <ReferenceItem
    :id="5"
    title="timerfd_create(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/timerfd_create.2.html"
  />
  <ReferenceItem
    :id="6"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="7"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
