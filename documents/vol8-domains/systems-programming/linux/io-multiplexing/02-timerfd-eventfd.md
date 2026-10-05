---
title: "timerfd 与 eventfd:时间与事件的 fd 化"
description: "时间与事件怎么化成 fd,与数据源进同一个 epoll 循环:本篇实测 eventfd 的计数器语义(write 是加法,1+2+5 三次写入一次 read 取走 8、再读 EAGAIN,EFD_SEMAPHORE 每次只减 1,写 5 领五次各得 1,写满 2^64-2 的上限再加 5 回 EAGAIN,缓冲区小于 8 字节回 EINVAL、大于 8 的 read 也只收 8 个字节),一次 write(5) 的通知三档(LT 加默认醒 1 次,信号量加 LT 连醒 5 次的忙通知档,读一次不等于读空,信号量加 ET 醒 1 次靠读到 EAGAIN 收尾),timerfd 的本体课(一次性 200ms 到期 read 得 1,周期 100ms 睡过 350ms 一次 read 报 3 的合并计数,fdinfo 里 ticks 停在 1 而 read 报 3、错过的档数由 read 一次报出且报后清零、下一档按原节拍续排,改 interval 后首档沿用结构体里旧 it_value 的 t+100 首档再按 30ms 走,it_value 归零撤销后 select 300ms 返回 0),1ms 档的到期精度(500 档间隔中位 999.3µs、p99 1029µs、max 1054.3µs,10ms 档中位 10000.3µs,复跑一致,对照同机 Windows 侧 Sleep(5) 实睡 12.6ms),收尾是 pipe 加 eventfd 加 timerfd 同挂一个 epoll 的单循环,151/250/401/500/651/750/901ms 三类事件交错时间线全部由同一个 epoll_wait 分发"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "信号(下):实时信号、signalfd 与 pidfd"
related:
  - "I/O 多路复用:select、poll 与 epoll 的边界与成本"
  - "io_uring:把等待 I/O 变成收割完成事件"
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "共享内存:页面文件后备的命名映射对象"
  - "inotify 文件监控:把文件系统的动静变成事件流"
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
---

# timerfd 与 eventfd:时间与事件的 fd 化

[上一篇](./01-select-poll-epoll.md)里咱们把 select、poll、epoll 三个等待 API 的脾气都量了个遍,谁醒来一次要扫的 fd 多,成本随着 N 涨成了什么形状,还有 epoll 的兴趣表怎么省下重复注册的活,都拿到了实打实的数。不过那一篇的数据源从头到尾全是管道,表里等的也就是管道里到了数据。真实的程序要等的远不止这一件,心跳的发送要按点,超时的收取也要按点,别的线程干完了活,还得跟咱们打一声招呼。而时间与事件,偏偏都不是字节流,咱们手上的 poll 表,又拿什么去等它们?

Linux 的答法还是那句老话:一切皆文件描述符。定时器也做成了 fd(timerfd),到期的时候就是它可读,read 出来的数是错过的档数。事件也做成了 fd(eventfd),您 write 它是按铃,而 read 它就是应门。它们与管道、socket、inotify 进的是同一张表、同一个循环。[信号下篇](../process/05-signal-advanced.md)里咱们已经请 timerfd 当过一回配角,150ms 的周期与信号在 poll 表里交错就位,正面的讲解当时轮不到它。[上一篇](./01-select-poll-epoll.md)的结尾倒是给 eventfd 留过话,说 E6 里露过一面的 eventfd-count 到这边当主角,现在两样都轮到了。Windows 那边走的是对象与句柄的另一套,统一的 fd 这层接口是没有的,[共享内存篇](../../windows/memory/02-shared-mem.md)的对照实验里 eventfd 出场过一回,它当的是 WSL2 侧的通知件,单条 264 纳秒的成绩咱们不重测,本篇只把语义的课上完。

实验的编号是 E1 到 E5,对应的是仓库 `code/volumn_codes/vol8/systems-programming/linux/io-multiplexing/02-timerfd-eventfd/` 存档下的 t1 到 t5 五组文件,代码与全部的原始输出都入册了。本篇的 E 只认本篇:上一篇自己的 E 系、下一篇 io_uring 的 E 系,同号的也互不相干,您翻存档的时候认目录就好。正文里的输出块多数是节选,删掉的行以 `...` 标出,拿存档对表的时候请以存档为准。

实验代码写的都是裸系统调用,手里的 fd 都是裸的 int,错误处理大多是省掉的,图的是把机制摆在明面上,这一手的取舍与上一篇的一致。到了咱们自己写工程代码的时候,这些 fd 的归宿是 [RAII 范式](../../thinking/01-raii-paradigm.md)里的 unique_fd,错误的归宿是[错误处理范式](../../thinking/02-error-paradigm.md)的 expected,两篇思维基石给的就是正解,本篇就不重复了。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2 的构建,CPU 用的是 AMD Ryzen 7 9700X,g++ 的版本是 16.2.1,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra`,glibc 的版本是 2.44,全部实验的警告数是零。计时走的都是 CLOCK_MONOTONIC,计时类的 E4 还复跑了一轮,第二轮的输出收在存档的 `t4_rerun.out` 里。全部 `.out` 出自 2026-10-04 的同一轮,fd 的编号、毫秒时间戳与纳秒剩余量每次复跑都会变,咱们引用的是次序与档位,而不是任何具体的数值。

## E1:eventfd:内核里的一个 8 字节计数器

### write 是加法,read 是取走

`eventfd(2)` 的参数是一个初始值加一个 flags,还您一个 fd。这个 fd 的背后没有缓冲区、没有字节流,就是内核里的一个无符号 64 位计数器,而外面还挂着一条等待队列。write 进去的都是 8 个字节,内容是要加的数,read 出来的也都是 8 个字节,内容是计数器当时的值。咱们直接跑 t1 的默认模式:

```text
[默认模式 efd=3]
  write(第1次, 1) = 8
  write(第2次, 2) = 8
  write(第3次, 5) = 8
三次 write 之后计数器 = 1+2+5, 一次 read 全取走:
  read(取值) = 8
  read(再读) = -1 errno=11 (Resource temporarily unavailable)
```

三次 write 加了 1、2、5,一次 read 就全部取走了。这里有个容易看岔的地方,得跟您提一句:write 行的 8 是写入的字节数,read 行的 8 是计数器的值,两个 8 偏偏凑在了一起,其实纯粹是因为 1+2+5 正好等于 8。您把第三次改成 4,读出来的数就变成了 7,而 write 报的字节数依旧是 8。默认模式的 read 把计数器整个取走并清零,所以紧接着的第二次 read 拿到 EAGAIN(errno 11),fd 上已经空了。这样的“读走即清零”,您在[信号下篇](../process/05-signal-advanced.md)的 signalfd 身上见过同款:读走即消费。

### EFD_SEMAPHORE:一次只减 1

咱们把 flags 里带上 `EFD_SEMAPHORE`,read 的取法就换了:

```text
[信号量模式 EFD_SEMAPHORE efs=4]
  write(一次写 5, 5) = 8
  read(第n次) = 1
  read(第n次) = 1
...
  read(第n次) = -1 errno=11 (Resource temporarily unavailable)
```

咱们发的还是一次 write(5),信号量模式下的 read 每次只把计数器减 1,返回的值固定是 1。五次 read 把 5 一份一份地领完、第六次才 EAGAIN。计数器的本体没有变,变的只是取法:默认模式一次取走全部、信号量模式一次领一个。听上去两种取法好像只差了一点口感,而等它们进了 epoll,差异会大到咱们要用一整个 E2 去看。

### 两种明确的拒绝

咱们再看两条边界,它们各有各的明确 errno。计数器的上限是 2 的 64 次方减 2、加到装不下时,write 回的是 EAGAIN:

```text
[溢出演练 efo=5]
  write(写到只差 1 满, 18446744073709551614) = 8
  write(再加 5, 5) = -1 errno=11 (Resource temporarily unavailable)
```

咱们写成 18446744073709551614(2 的 64 次方减 2、正好是上限本身),这一次成功了,再加 5 就放不下了。man 2 eventfd 写得明白:阻塞模式的 write 这时会等读方来取,非阻塞的那一档(EFD_NONBLOCK)就直接回 EAGAIN。另一个拒绝更朴素:缓冲区小于 8 字节的 read 与 write,回的都是 EINVAL:

```text
[尺寸错配 efe=6]
  write(4 字节) = -1 errno=22 (Invalid argument)
  read(4 字节)  = -1 errno=22 (Invalid argument)
```

咱们试的是 4 字节进、4 字节出,两个方向都被拒绝了。man 页写明的条件是缓冲区小于 8 字节,咱们拿 12 字节的缓冲区又试了一把:write 回的还是 EINVAL,read 倒是成功了,只是回来的内容依旧只有 8 个字节。eventfd 自己的 fdinfo 其实也有看头,eventfd-count 与 semaphore 两个字段把计数器的状态亮在了外面,上一篇的 E6 已经摸过,咱们就不重走了。而 timerfd 那边马上就有更热闹的 fdinfo 可看。

## E2:挂进 epoll 之后,一次 write(5) 醒几次

咱们单看计数器的话,语义课只上了一半。eventfd 的正经用法是挂进事件循环当通知件,那么问题就来了:一次 write(5),epoll 会把咱们叫醒几次?醒几次取决于两件事的组合:read 的模式(默认还是信号量),与注册的触发方式(水平触发的 LT 还是边沿触发的 ET,这一对的机制课在网络卷的[epoll 篇](../../../networking/02-epoll-io-multiplexing.md),上一篇量的是行为)。t2 把三种组合各跑了一遍(默认模式配 ET 与配 LT 的醒法相同,就不单独跑了),每轮醒来咱们只读一次,而 ET 档按纪律一路读到 EAGAIN:

```text
一次 write(5) 之后, 每次醒来读一次(ET 档按纪律读空):
LT+默认:
  [semaphore=0 et=0] 醒 1 次, read 1 次, 取走计数合计 5, 收敛耗时 151 ms
LT+信号量:
  [semaphore=1 et=0] 醒 5 次, read 5 次, 取走计数合计 5, 收敛耗时 150 ms
ET+信号量:
  [semaphore=1 et=1] 醒 1 次, read 5 次, 取走计数合计 5, 收敛耗时 150 ms
```

咱们看数字之前,有一处得交代在前面:三组的收敛耗时都在 150ms 上下,不过这并不代表三档一样快。循环的退出条件统一是 epoll_wait 等满 150ms 无事可报,醒与读其实都发生在头几毫秒,而剩下的时间,全是在等那 150ms 的超时到期。咱们把拿收敛耗时比快慢的这个读法排掉之后,真正的差异在醒几次、读几次。

咱们挨个看。LT 加默认的组合醒 1 次,read 一次就把 5 全取走了,计数器清了零、fd 不再可读,接下来的都是安静。ET 加信号量的组合醒 1 次,边沿只报一次、但醒来的那一次里要连读五次、读到 EAGAIN 收尾,这是上一篇 ET 纪律的标准动作。LT 加信号量的组合醒 5 次,咱们 read 一次、计数器从 5 落到 4,而只要计数器还没回到 0,fd 就仍处在可读的状态,而 LT 的语义是只要可读就报,于是下一轮的 epoll_wait 会立刻再醒。在这样的组合下读一次不等于读空,循环就被这么一次一次地叫。

咱们把这一档单独拎出来,因为它值得您认识:信号量配 LT,凑成的是一个忙通知档。在咱们看来程序就像是在空转,其实每一步都在干活,每次的 read 都真领走一个 1,而从 epoll 的视角看过去,它与事件没处理干净导致的热循环长得一模一样。您在现场看到 epoll_wait 不停地返回同一个 fd,咱们别急着怀疑内核,咱们的 read 是不是信号量模式,这是头一个要查的。t2 的收尾把这件事说得很直白:

```text
LT+信号量那组: 计数器还剩 4 时 fd 仍处于可读状态, 每轮 epoll_wait 都再报,
读 1 次 -> 剩 4 -> 又醒 -> ... 直到清零。这不是 bug, 是 LT 的语义配上了"读不完全清零"的计数器。
```

咱们把 t2 里 read 的两种写法摘出来,三档差异的源头就在这儿:

```cpp
if (et) {
    // ET 纪律: 一次事件里读到 EAGAIN
    for (;;) {
        ssize_t r = read(efd, &v, 8);
        if (r < 0) break;
        ++res.reads; total += v;
    }
} else {
    if (read(efd, &v, 8) == 8) { ++res.reads; total += v; }
}
```

咱们看 LT 的分支,醒来后 read 一次就算处理完了,ET 的分支里得循环读到 EAGAIN 才肯放手,这是上一篇定下的纪律。所以同样是信号量模式,配 LT 的一档是五次醒来各领一份,而配 ET 是一次醒来连领五份,醒的次数差了五倍,该干的活一件都没少。

那咱们什么时候该用哪一档?咱们按要回答的问题来分。您只想知道“来活了,去处理”,用默认模式加 LT 的组合就好:通知只来一次、read 一把全取,全程是安安静静的。每个计数对应一份独立的工作、消费一次减一次的场合,就该用信号量模式了:工作池里的 worker read 一次就领走一份活,这正是想要的语义,此时的 LT 是逐份叫醒,而配 ET 就得在一次醒来里连读到底。

## E3:timerfd:内核里的一个到期计数器

`timerfd_create(2)` 的参数是一个时钟 id 加 flags,还您一个定时器的 fd。设定用的是 itimerspec:it_value 是首档多久到期,而 it_interval 是之后隔多久一档,两者都归零的时候就是撤销。到了期也不跑回调、不发信号,而是 fd 变为可读,read 出来的是 8 个字节、内容是到期次数。三段咱们挨个做:一次性的设定、周期与合并、改期与撤销,分别对应存档里的 `[i]`、`[ii]`、`[iii]`。

### fdinfo 当观察窗

这个 fd 有个便宜的观察窗:`/proc/self/fdinfo/<fd>`。[IPC 篇](../process/03-ipc.md)里咱们用它看过 SCM_RIGHTS 共享的 pos,timerfd 这边它亮出来的字段更多:clockid、ticks、it_value、it_interval,全是内核里的当前值,咱们连一次系统调用都不用花。咱们在 t3 刚创建时拍了一张:

```text
--- fdinfo(3) 刚创建, 未设定 ---
pos:	0
flags:	04002
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 0)
it_interval: (0, 0)
```

clockid 的 1,就是 CLOCK_MONOTONIC 在 Linux 里的编号。您给它设定 200ms 的一次性定时之后再拍两张:

```text
...
--- fdinfo(3) 设定后(剩约 200ms) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 199994019)
it_interval: (0, 0)
--- fdinfo(3) 60ms 时(剩约 140ms) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 139869103)
it_interval: (0, 0)
  select 报就绪, 距设定 200 ms
  read = 1
  read = -1 (Resource temporarily unavailable)
```

剩余时间从 199994019 纳秒走到了 139869103 纳秒,而两张快照之间过去了约 60ms,剩余时间就这么肉眼可见地往下走,比任何文档的描述都直观。到期之后 select 报了就绪,一次 read 得到 1、再 read 就成了 EAGAIN,您在这里又一次见到了“读走即清零”。

### 周期与合并:错过的到期,read 一次报完

咱们把 it_interval 也填上,就是周期模式了,存档里这段的标号是 [ii],前面的 [i] 就是刚才那次 200ms 的一次性设定。t3 设了 100ms 一档,读方则故意睡满了 350ms 才来:

```text
[ii] 周期 100ms, 读方故意睡 350ms 再来:
--- fdinfo(3) 350ms 时(约 3 档已过) ---
...
clockid: 1
ticks: 1
settime flags: 00
it_value: (0, 0)
it_interval: (0, 100000000)
  read = 3
  一次 read 报的是错过的档数: 3 (不丢数据, 但也不排队报 3 次)
  read = 1
```

这段输出里有两个值得停下来的细节。咱们看头一个:350ms 里过去了 3 档(100、200、300ms),read 报的确实是 3。错过的档不会丢,不过也不排队补报三次,而是一次 read 把 3 全部报出、报完清零。您可以拿它跟标准信号对照着记:阻塞期连发 3 次 SIGUSR1 只记 1 次,信号合并把次数都丢了,而 timerfd 保留次数、合并的只是报告。

第二个细节就更有意思了,咱们慢慢看。fdinfo 里的 ticks 是 1,it_value 也归了零。如果 ticks 报的是没读的到期次数,这里就该是 3 了,可真正的 3 是 read 给的。咱们再往下看一行:read 取走之后,代码只睡了 50ms,下一档就到了(末尾 read 得 1)。咱们把这几行放在一起,机制就清楚了:没读的到期次数不记在 fdinfo 里,ticks 只是到期未读的标志,而且计时已经停了(it_value 归零)、等读方来取。ticks 停在 1 而 read 报 3,也告诉了咱们:错过的 3 是 read 时刻按节拍补算出来的,计时早在第一档到期后就停了。而在 read 取走之后,下一档还按原来的节拍到点(相对设定时刻的第 400ms),不从 read 的时刻重新数 100ms。所以醒来晚了,错过的次数与原本的节拍都不受影响。

### 改期:settime 是整副重装

接下来的一条语义,而教材里很少把它摆出来,咱们是拿实验换来的。t3 的第三段 [iii] 把周期从 100ms 改成 30ms,代码只动了结构体里的 it_interval:

```cpp
its = {};
its.it_value = {0, 100000000};
its.it_interval = {0, 100000000};               // 100ms 一档
timerfd_settime(tfd, 0, &its, nullptr);
...
its.it_interval = {0, 30000000};
timerfd_settime(tfd, 0, &its, nullptr);          // 相对模式: 新周期从此刻起算
```

笔者把结果原样贴出来:

```text
[iii] 改 interval 100ms -> 30ms, 再撤掉:
  t+100 ms 到期 (30ms 档)
  t+130 ms 到期 (30ms 档)
  t+160 ms 到期 (30ms 档)
  t+190 ms 到期 (30ms 档)
  t+220 ms 到期 (30ms 档)
```

代码里打印的标签写死了“30ms 档”,而第一行却是 t+100。打印其实是没出错的,定时器是真的到了 100ms 才响的,之后才进入 130、160、190、220 的 30ms 节奏。存档里的那行注释,写的是“相对模式: 新周期从此刻起算”,它就是笔者当时的理解。相对模式的半句没错,错的是后半句:从此刻起算的,是 it_value 里还留着的旧值,不是咱们新写的周期。真正的机制其实一句话:timerfd_settime 没有“只改周期”这样的调用,它做的是整副重装。您递进来的 itimerspec 里 it_value 写的是什么,首档装的就是什么。咱们的结构体从上一段过来,it_value 里还留着 100ms 的旧值,咱们只改了 it_interval,内核就按 it_value=100ms 装了首档,而后续的档按 30ms 排。咱们常把这一现象说成“改 interval 首档沿用旧 it_value”,而它的主语其实是您手里的结构体,不是内核替您记着旧相位。POSIX 的 timer_settime 一直就是这么定义的,timerfd 把它原样继承了下来。您想让首档立即按新周期走,it_value 也一起写成 30ms 就好了。

### 撤销:两个字段一起归零

撤销最省事的写法是把结构体整个清零再 settime,咱们在 t3 里就是这么做的,fdinfo 与行为两头都确认了:

```text
--- fdinfo(3) 撤销后(剩余时间为 0) ---
...
clockid: 1
ticks: 0
settime flags: 00
it_value: (0, 0)
it_interval: (0, 0)
  撤销后等 300ms: select 返回 0 (不再有到期事件)
```

select 等满了 300ms 才返回 0,一个到期的事件都没有。撤销之后的 fd 还在、也还能 poll、只是永远不再就绪,除非您再 settime 重新设定一次。

## E4:1ms 一档,到点有多稳

咱们把语义都对完之后,该问精度了。t4 给了两档周期各一批的采样,记录的是相邻两次 read 醒来的间隔:1ms 的档收 500 个、10ms 的档收 300 个:

```text
周期 1000 us, 收 500 档, 相邻间隔(us): min=945.4 中位=999.3 p90=1010.3 p99=1029.0 max=1054.3
周期 10000 us, 收 300 档, 相邻间隔(us): min=9956.3 中位=10000.3 p90=10018.1 p99=10028.9 max=10042.4
```

咱们复跑了一轮,成绩收在存档的 `t4_rerun.out` 里:

```text
周期 1000 us, 收 500 档, 相邻间隔(us): min=950.5 中位=999.3 p90=1010.5 p99=1029.5 max=1061.6
周期 10000 us, 收 300 档, 相邻间隔(us): min=9705.4 中位=10000.3 p90=10019.9 p99=10078.5 max=10215.0
```

两轮的中位一模一样(999.3 与 10000.3),尾部的形状也稳住了。咱们看 1ms 档的第一轮:中位离标称的 1ms 只差 0.7µs,p90 也只比标称多了百分之一,最差的一次 1054.3µs,不过也就 5.4%。而 min 反而比 1000µs 还小(945.4),您别把它当成定时器抢跑,它其实在补拍:上一档醒晚了,下一档还按原节拍的格子到点,相邻的间隔自然就缩了回来。而 E3 的语义落到间隔分布上,长出的正是这样的形状。10ms 的档也是同理,相对的波动更小,而中位停在 10000.3µs,第一轮的尾部最差是 10042.4µs(偏差 0.4%),复跑放宽到了 10215µs,也不过 2.2%。

咱们这些数字得配上参照物才显得出分量。同一台机器的 Windows 侧,[共享内存篇](../../windows/memory/02-shared-mem.md)实测过 Sleep(5):实际睡了约 12.6 毫秒。Windows 默认的定时器分辨率是 15.6ms 一档,5ms 的请求向上取整到下一格,那篇把原因也挑明了:一百次歇脚每次都多睡了 7ms 上下,两边的总时差也就正好对上了。而 Linux 这边的 hrtimer 走的是高精度时钟,没有这道全局的粗档,1ms 的请求就真按 1ms 上下排。同一个硬件上的两个世界,定时器精度的差距本身就是平台差异最直观的一课。口径也得交代:这是空闲进程的数字,咱们同机跑重负载的话,p99 与 max 的数字也都会放宽,咱们的对照只在同为空闲的条件下成立。

## E5:一张 epoll 表,收三类事件源

语义、通知次数、精度都过完了,收尾时咱们把 eventfd 与 timerfd 攒进同一个循环。t5 挂了三样东西:一根 pipe 当数据源,一个 eventfd 当别的线程的门铃,一个 250ms 周期的 timerfd 当心跳。注册的代码就这么几行:

```cpp
int ep = epoll_create1(0);
epoll_event ev{};
ev.events = EPOLLIN; ev.data.fd = pd[0]; epoll_ctl(ep, EPOLL_CTL_ADD, pd[0], &ev);
ev.events = EPOLLIN; ev.data.fd = efd;    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &ev);
ev.events = EPOLLIN; ev.data.fd = tfd;    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &ev);
```

主循环的骨架咱们也摘一段,按 fd 分发的形状就在这儿:

```cpp
for (;;) {
    epoll_event evs[8];
    int k = epoll_wait(ep, evs, 8, 1000);
    for (int i = 0; i < k; ++i) {
        int fd = evs[i].data.fd;
        if (fd == tfd) {
            uint64_t v; (void)!read(tfd, &v, 8);
            ++n_timer;
            std::printf("t=%4ld ms  timerfd 第 %2d 次到期\n", now_ms() - t0, n_timer);
        } else if (fd == efd) {
            uint64_t v; (void)!read(efd, &v, 8);
            ++n_event;
            std::printf("t=%4ld ms  eventfd 门铃, 本次取走计数 %llu\n", now_ms() - t0, (unsigned long long)v);
        } else if (fd == pd[0]) {
            char buf[16] = {};
            ssize_t r = read(pd[0], buf, sizeof buf - 1);
            ++n_pipe;
            std::printf("t=%4ld ms  pipe 收到 %zd 字节: \"%s\"\n", now_ms() - t0, r, buf);
```

咱们的三个分支各管一路:timerfd 到期了就把那次的计数 read 掉,eventfd 响了就取走累计值,而 pipe 来了数据,读出来看的是收工信号在不在。代码里那几个 `(void)!` 是故意把返回值丢掉的写法,演示代码图的是时间线干净,您自己写的时候,这里的错误该接的还是得接。咱们安排的 worker 线程在 150ms 写管道、400ms 与 650ms 两处按铃、900ms 写 quit 收工,整场的时间线是这样的:

```text
t= 151 ms  pipe 收到 5 字节: "hello"
t= 250 ms  timerfd 第  1 次到期
t= 401 ms  eventfd 门铃, 本次取走计数 1
t= 500 ms  timerfd 第  2 次到期
t= 651 ms  eventfd 门铃, 本次取走计数 2
t= 750 ms  timerfd 第  3 次到期
t= 901 ms  pipe 收到 4 字节: "quit"

循环结束: timerfd 3 次, eventfd 2 次, pipe 2 次, 全部由同一个 epoll_wait 分发
```

您看 151 的数据、250/500/750 的心跳、401/651 的门铃,在时间线上交错地就位、谁也不打断谁。门铃用的是默认模式加 LT,write(1) 与 write(2) 各通知了一次,read 当场取走当时的累计值,这正是 E1 的语义在循环里的样子,也是它与 E2 忙通知档的对照面。epoll_wait 挂了 1000ms 的超时兜底,不过整场一次都没用上,每一步都是被事件叫醒的。

咱们把三类事件源收进了同一个循环,t5 想说的也就这一句。回看这一卷的来路:[inotify 篇](../file-io/06-inotify.md)把文件系统的动静化成了 fd,信号下篇把信号与进程化成了 signalfd 与 pidfd,本篇把时间与事件也补齐了。它们在内核里的出身各不相同,而到了 poll 表里,它们就都成了同一种东西:一个可等待的 fd。fd 化的等法也有到头的地方:磁盘上的普通文件,epoll 是根本不让挂的,咱们想异步读它,就得把等 fd 的姿势换成提交请求、再等完成事件的姿势。那是下一篇 io_uring 的正题,咱们到那边接着走。
