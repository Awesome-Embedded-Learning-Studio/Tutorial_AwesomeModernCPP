---
title: "时钟源与 POSIX 时间 API:机器里的九把钟"
description: "九个 clockid 搬上同一台机器挨个读:时钟族普查(COARSE 两把的 getres 是 4ms、反推出内核节拍 250Hz,而 sysconf 的 CLK_TCK 报 100,两套口径分开;TAI−REALTIME 恰为 37s 的闰秒累计;BOOTTIME−MONOTONIC 只差 20ns,配 /proc/uptime 对表;RAW−MONOTONIC 的 −106.4ms 冻结分歧;adjtimex 只读报已同步),分辨率与精度的分离(getres 报 1ns,背靠背连读一百万次的相邻间隔中位 20ns、p99 21ns,COARSE 百万次只读出 3 个不同的值,单次读取代价 16.2ns 对 2.0ns),连续性(MONOTONIC 两百万次零回退;十秒窗口 1kHz 被动监测,REALTIME−MONOTONIC 偏移峰谷差 23.3µs、零步进;RAW 的分歧当下只动 2.2µs,累计却是 106.4ms,大头是开机早期的历史频率调整),vDSO 兑现总纲的免票承诺(maps 里 [vvar]/[vvar_vclock]/[vdso] 在场,vDSO 16.9ns 对真陷入 168.9ns 差 10 倍,strace 挂上 clock_gettime 零条目对两百万条),CPU 时间三口径(主线程睡 300ms 自己的线程钟只长 93µs;进程钟 298.0ms、getrusage 298.1ms、tick 口径 290ms,粒度差直接可见),墙上时间呈现链(同一 epoch 过 gmtime_r/localtime_r,切 TZ 只换规则不换时间),收尾对照 Windows 侧的 GetTickCount64/QueryPerformanceCounter 与 Sleep(5) 实睡 12.6ms 的默认 15.6ms 一档"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "timerfd 与 eventfd:时间与事件的 fd 化"
related:
  - "timerfd 与 eventfd:时间与事件的 fd 化"
  - "系统编程总纲:用户态、内核与两大阵营的地图"
  - "C++20 std::chrono 深度:日历与时区"
  - "定时器全景:alarm→setitimer→timer_create→timerfd"
  - "chrono：duration、时钟与 C++20 日历"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 时钟源与 POSIX 时间 API:机器里的九把钟

咱们在 [timerfd 篇](../io-multiplexing/02-timerfd-eventfd.md)里创建定时器的时候,`timerfd_create` 的第一个参数填的就是 CLOCK_MONOTONIC,当时被咱们一笔带过了。vol3 的 [chrono 课](../../../../vol3-standard-library/time-numeric/58-chrono.md)里,steady_clock 也是咱们张口就来的老朋友。可是您把这个参数真当成一道题来问:同一台机器里到底藏着几把钟?CLOCK_REALTIME 与 CLOCK_MONOTONIC,差的到底是什么?量耗时为什么人人都叮嘱您用单调钟?这串名字看上去只是普通的枚举,背后其实是几把用途、精度、连续性都不一样的钟,钟选错了,量出来的数就不可信了。

本篇是时间这一章的地基,咱们把 Linux 暴露的九个 clockid 全部搬上同一台机器,每个都做一次 getres 与读数的普查,再从读取的分布、连续性、CPU 时间的记录口径、墙上时间的呈现链进去,量它们真刀真枪的差别。等这些量完了,选钟的判据也就到了您手里,而且是实打实的。

实验的编号是 E1 到 E6,对应仓库 `code/volumn_codes/vol8/systems-programming/linux/time/01-clock-sources/` 存档下的 t1 到 t6 六组文件,代码与全部的原始输出都入了册。本篇的 E 只认本篇:下一篇 chrono 日历的 E 系、再下一篇定时器全景的 E 系,同号的也互不相干,您翻存档的时候认目录就好。正文里的输出块多数是节选,删掉的行以 `...` 标出,拿存档对表的时候请以存档为准。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2 的构建,CPU 用的是 AMD Ryzen 7 9700X,g++ 的版本是 16.2.1,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra`(t5 加 `-pthread`),glibc 的版本是 2.44,全部实验的警告数是零。计时走的都是 CLOCK_MONOTONIC,计时类的 E2 与 E4 还各复跑了一轮,第二轮的输出收在存档的 `t2_rerun.out` 与 `t4_rerun.out` 里。全部 `.out` 出自 2026-10-04 的同一轮,秒与纳秒的读数,每轮都换了一批,咱们引用的是档位与分布的形状,而不是任何具体的时刻。

时钟源这个词咱们还得单说两句,因为本篇的标题里就是它。clockid 是咱们用户态看到的 API,内核底下真正数时间的角色叫时钟源,也就是那套硬件计数的机构。咱们拿 `/sys/devices/system/clocksource/clocksource0/` 下的两个文件查它的选型:current_clocksource 报的是 tsc,available 里放的是 tsc、hyperv_clocksource_tsc_page、hyperv_clocksource_msr 与 acpi_pm,一共四个名字都在这儿了。其中 hyperv 打头的两个,是 Hyper-V 给半虚拟化客人准备的参考时钟源,Windows 宿主把读数的机构直接递了进来,而裸机教程里您见到的多半是 tsc 加 hpet 的组合,本机连 hpet 的影子都没有。WSL2 的出身连时钟源都带着印记,这一点咱们在 E2 里还会回来说。

## E1:时钟族普查:九个 clockid,各记各的时间

咱们把点得上号的九个 clockid 排成一列,每个都做一次 clock_getres 的查询,再读一次它的当下时刻,t1 的一次运行是这样的:

```text
== E1: clock_getres 与时钟族首次读数 ==
CLK_TCK(sysconf)=100  (COARSE 分辨率反推出的 tick 档位见下两行)
CLOCK_REALTIME         getres=0.000000001  now=1791121284.559110361  墙上时间,可被 settimeofday/adjtime 改
CLOCK_REALTIME_COARSE  getres=0.004000000  now=1791121284.556746979  墙上时间的粗读档,分辨率=1/HZ
CLOCK_MONOTONIC        getres=0.000000001  now=39943.706318194  单调钟,不受 settimeofday 步进影响
CLOCK_MONOTONIC_COARSE getres=0.004000000  now=39943.703953129  单调钟的粗读档,分辨率=1/HZ
CLOCK_MONOTONIC_RAW    getres=0.000000001  now=39943.599872894  原始硬件计数,不受 NTP 频率调整影响
CLOCK_BOOTTIME         getres=0.000000001  now=39943.706318876  单调钟,挂起期间也在走
CLOCK_TAI              getres=0.000000001  now=1791121321.559112876  国际原子时刻度,闰秒只往一个方向加
CLOCK_PROCESS_CPUTIME_ID getres=0.000000001  now=0.001620885  进程 CPU 时间(用户+系统)
CLOCK_THREAD_CPUTIME_ID getres=0.000000001  now=0.001622578  线程 CPU 时间(用户+系统)
```

读数那两列怎么来的,咱们看存档里 t1 的一个小帮手,每个 clockid 走的都是同样的一遍:

```cpp
void one_line(clockid_t id, const char* name, const char* note) {
    timespec res{}, now{};
    if (clock_getres(id, &res) != 0) {
        std::printf("%-22s getres 失败: %s\n", name, std::strerror(errno));
        return;
    }
    clock_gettime(id, &now);
    std::printf("%-22s getres=%lld.%09lld  now=%lld.%09lld  %s\n",
                name,
                (long long)res.tv_sec, (long long)res.tv_nsec,
                (long long)now.tv_sec, (long long)now.tv_nsec, note);
}
```

还有一处是可以顺手对上号的:REALTIME 的读数 1791121284,咱们拿日历一换,出来的就是 2026-10-04,与环境口径里写的捕获日期正好是同一天,咱们把它当存档成色的小旁证。

九把钟按记的东西分组就好认了。读数是 1791121284 这样的,记的是墙上时间,从 1970 年的 epoch 起算,REALTIME 与 TAI 也算这一类里的。读数是 39943 这样的,记的是开机以来的流逝时间,MONOTONIC 一家的读数全是这个量级,实验的时候本机已经开了约十一个小时。读数小到 0.0016 的,记的是进程到目前为止烧掉的 CPU 时间,咱们放到 E5 单独看。同一台机器记下了三样东西,顶着的都是时间的名号,钟也就分成了三拨。

表里最扎眼的是 COARSE 两把的 getres:都停在了 0.004000000,四毫秒就这么明晃晃地摆着。这个数的来历不玄,它就是内核节拍的长度,咱们取倒数一算,内核跑的就是 250Hz。可是您回头看第一行,sysconf 查出来的 CLK_TCK 是 100,一秒被划成一百个十毫秒的格子,那是暴露给用户态的另一套记数单位,/proc 里那些 tick 数用的就是它。两套口径经常在裸机的讨论里被搅成一团,本机恰好把它们分开了:内核的节拍是 250Hz,用户态的格子是 100Hz。这样的分家不算坏事,E5 里咱们正好拿 CLK_TCK 这套格子看出一处真实的粒度差。

差值的那段,加上拿 adjtimex(2) 只读模式问内核时钟状态的那段,咱们原样贴上:

```text
== 同期差值(背靠背读,残差来自读取顺序的先后) ==
TAI - REALTIME       = 37000000020 ns (原子时与 UTC 的整秒差,即闰秒累计数)
BOOTTIME - MONOTONIC = 20 ns (开机以来挂起的累计时长)
RAW - MONOTONIC      = -106445801 ns (0 附近=本机没有 NTP 频率纪律在拉,见 E3)
/proc/uptime         = 39943.700 s 对 BOOTTIME 39943.706323 s,差 -0.006 s
                       (挂起总时长;为 0 即本次开机以来没挂起过)

== adjtimex 只读: 内核时钟纪律状态 ==
status=0x0  freq=-23324 ppm  maxerror=2006 us  esterror=1 us
STA_UNSYNC(0x40) 置位? 否 —— 置位说明内核没有活跃的时间纪律源在调频
```

三个差值各有各的戏。咱们头一个看 TAI 与 REALTIME 的差:读数是 37000000020 纳秒,整数部分的差是 37 秒,尾巴上的 20 纳秒,是背靠背两次读取之间隔着几条指令的残差。这 37 秒是有来历的:1972 年 UTC 与原子时建立换算的时候,两者已经差了 10 秒,后来 27 次闰秒又各加了一秒,攒到今天就成了 37。CLOCK_TAI 走的是国际原子时的刻度,闰秒在它的世界里不存在,所以两边的差永远是个整数。您把这个数记下,下一篇的 tai_clock 会报出同一个 37,读的是同一份闰秒数据,只是换了一种读法。

BOOTTIME 减 MONOTONIC 的读数只有 20 纳秒,咱们直接当零看了。按文档的语义,BOOTTIME 等于单调钟再加上挂起的累计时长:笔记本合盖的那段时间,MONOTONIC 停走而 BOOTTIME 照走,差值记的就是挂起的累计。WSL2 的宿主睡眠走的是关机路线,挂起在它身上几乎是观测不到的,咱们拿 /proc/uptime 对个表:39943.700 秒对 BOOTTIME 的 39943.706323 秒,差出来的 0.006 秒,是格式化截断加上两次读取错开的动静。两边记的是同一件事,本次开机以来的挂起总时长是零。挂起照走的语义,咱们按文档口径认下,本机这边实在造不出挂起的现场。

第三行是 RAW 与 MONOTONIC 的分歧,读数是负的 106445801 纳秒:开机以来的十一个小时里,RAW 比 MONOTONIC 慢了 106 毫秒。分歧是冻结的,来历咱们放到 E3 专门去看。收尾的 adjtimex(内核里调钟走速与步进的接口),咱们拿只读模式问它要状态:status 报的是 0x0,STA_UNSYNC 的标志位没有置上,说明内核里有活跃的纪律源在管着钟的走速。freq 那一栏咱们不取数:内核的原始字段按 65536 缩放,程序把它直接标成了 ppm(百万分之一的意思,谈走速偏差的惯用单位),是标错了,折算回来真实的走速修正不到半个 ppm,倒是与 E3 里安安静静的偏移对得上。

## E2:getres 报的 1ns,是刻度不是精度

普查表里除 COARSE 外的钟,getres 报的都是 0.000000001。看上去本机真能读出纳秒级的时间?咱们拿实验说话:背靠背连读一百万次,统计相邻两次读数的间隔分布,分布才是这把钟真实的读取粒度。t2 的输出:

```text
== E2a: 背靠背连读的相邻间隔分布(每次读 kN=1000000) ==
getres 都报 1ns(E1),下面的分布才是真读取粒度:
CLOCK_MONOTONIC        零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=316268 ns
CLOCK_REALTIME         零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=43272 ns
CLOCK_MONOTONIC_RAW    零差=  0.0%  最小非零=10 ns  中位=20 ns  p99=21 ns  max=106358 ns
CLOCK_THREAD_CPUTIME_ID 零差=  0.0%  最小非零=330 ns  中位=341 ns  p99=410 ns  max=85031 ns

== E2b: COARSE 的值是台阶 ==
CLOCK_MONOTONIC_COARSE 1000000 次读里只有 3 个不同值;相邻台阶平均间隔 3 ms(getres 见 E1)

== E2c: 每次读取的代价(整段计时/kN) ==
CLOCK_MONOTONIC        1000000 次读总耗 16165887 ns → 16.2 ns/次
CLOCK_MONOTONIC_RAW    1000000 次读总耗 16470994 ns → 16.5 ns/次
CLOCK_MONOTONIC_COARSE 1000000 次读总耗 2002692 ns → 2.0 ns/次
```

三把高精度钟的分布一个模子:中位的间隔是 20 纳秒,最小的非零间隔是 10 纳秒,p99 的读数也只是 21 纳秒。您连着读两次,两次读数之间隔的就是这二十纳秒上下的一步。getres 报的那个 1 纳秒,是 timespec 这套表示的刻度:tv_nsec 的一格就是一纳秒,高精度钟就说自己的分辨率是一纳秒。刻度是 tv_nsec 的一格,精度说的是读数真正能分开多远,咱们要分的正是这两样。max 那一列的几万到几十万纳秒,是被调度打断的噪声,与钟的素质无关。THREAD_CPUTIME_ID 单独粗了一档,中位落在了 341 纳秒,CPU 时间的读数粒度天然粗些,咱们把形状记下,E5 里它还有出场的时候。

本机的时钟源,开头交代环境的时候选的是 tsc。CPU 里的那个计数器,它的节拍比纳秒还细,所以粒度的瓶颈不在计数器身上。咱们量到的二十纳秒,大头是读一次钟的整条路的功夫:进 vDSO(virtual Dynamic Shared Object 的缩写,内核映射进咱们进程的一小段代码与数据页,咱们到 E4 再正面看它),读计数的值,拿换算的参数再做一次乘法,咱们把整条路走下来,花的就是这二十纳秒。

E2b 那一行才是真的壮观:一百万次的连读,只踩到 3 个不同的值,复跑一轮数出来的也只是 4 个。COARSE 的值是台阶式的,内核每个节拍更新一次缓存的时刻,两个节拍之间您读一万次,拿回来的都是同一个数,台阶与台阶之间隔的,名义上就是 E1 里出现过的四毫秒。输出里报的平均 3 毫秒是小样本的抖动:一百万次连读加上逐次的比较,整段的窗口也就几毫秒,跨得动的也就两三个台阶,平均数自然就飘了。它便宜也是有道理的:E2c 里,MONOTONIC 一次读取的代价是 16.2 纳秒,COARSE 报的是 2.0 纳秒,差了八倍。粗读档不用去硬件的计数机构现场换算,拿的本来就是缓存好的值,man 页给它的定位也就是快而粗的时间戳。所以选钟的口味就摆在这儿了:细读的路子走 MONOTONIC,一次读取就是二十纳秒的粒度,便宜的路子走 COARSE,读取代价砍掉了将近九成。

## E3:连续性:单调不回退,RAW 与 MONOTONIC 之间冻结的分歧

量耗时的时候,钟的连续性是最要紧的性质。咱们分三层去验:MONOTONIC 真的单调吗,REALTIME 在没人动它的时候稳不稳,RAW 与 MONOTONIC 这对形影不离的钟,差值讲的是什么故事。t3 的输出就在下面:

```text
== E3a: MONOTONIC 回退扫描 ==
2000000 次连读,回退次数=0(settimeofday 步进不进 MONOTONIC,这正是它当计时钟的资格)

== E3b: 10 s 窗口偏移监测(1 kHz 采样) ==
REALTIME-MONOTONIC 偏移: min=1791081340852770.5 us  max=1791081340852793.8 us  峰谷差=23.3 us
偏移瞬移(单步变化超 ±100us)次数=0 —— 0 次=窗口内没有发生墙上时间步进
窗口实长 9796.7 ms;RAW-MONOTONIC 从 -106.446 ms 走到 -106.444 ms,变化 2.2 us
  → 当下分歧率 = 0.000 ppm(RAW 不受 NTP 调频,分歧率就是内核频率修正的力度)

== E3c: 开机以来的累计分歧 ==
MONOTONIC=39953.5 s,RAW-MONOTONIC=-106.444 ms → 平均分歧率 -0.003 ppm
```

监测的做法也不复杂,咱们每一毫秒做一遍同样的事,存档里 t3 的采样循环就是:

```cpp
timespec rt{}, mono{}, raw{};
clock_gettime(CLOCK_REALTIME, &rt);
clock_gettime(CLOCK_MONOTONIC, &mono);
clock_gettime(CLOCK_MONOTONIC_RAW, &raw);
```

三把钟的读数背靠背各取一次,偏移就是两两的差,读取次序错开的几十纳秒,与咱们要看的微秒级步进还隔着一层。

E3a 是资格赛:两百万次的背靠背连读,回退的次数是 0。CLOCK_MONOTONIC 的单调性,文档承诺的是它不受 settimeofday 一类步进的影响,只会一个劲地往前走,这里咱们拿到了两百万次的实测背书。您拿它量耗时,差值才是永远可信的,这就是量耗时必须挑单调钟的根据。

REALTIME 就没有这个待遇了,文档写明了它可以被 settimeofday 直接拨,也可以被 adjtimex 以步进的方式纠正,拿它量耗时是危险的。本机没有 root 的权限,主动拨钟的实验做不了,咱们做的是十秒窗口、一毫秒一采的被动监测:REALTIME 对 MONOTONIC 的偏移,峰谷的差是 23.3 微秒,单步变化超过正负一百微秒的瞬移,咱们一次都没有见到。min 与 max 的两个天文数字,是 1970 纪元的底座混在偏移里的部分,咱们看的是峰谷差,23.3 微秒的抖动,多半来自采样时被调度偶尔打断的那一下。这一轮监测说明的只是窗口内的安稳,步进本身是真实存在的可能,NTP(网络对时协议)纠偏的瞬间它就会来,口径咱们如实交代。

更有味道的是 RAW 那两行。十秒的窗口里,RAW 对 MONOTONIC 的偏移从 −106.446 毫秒走到了 −106.444 毫秒,只挪了 2.2 微秒,E3c 又把镜头拉到了开机以来:开机以来的十一个小时,累计的分歧是 −106.444 毫秒。当下的移动量与累计的分歧完全不成比例,大头是开机早期攒下的,不是当下正在挣的,多半来自 WSL2 开机时与宿主对时留下的那一次频率调整。机制其实只有一句话:MONOTONIC 受 NTP 的调频纪律,它的走速会被微调,而 RAW 记的是不跟任何人同步的硬件计数,两把钟的差值,就是频率修正的累计。讲 RAW 与 MONOTONIC 的区别,教科书上通常只能引用文档的说法,这里咱们拿到了可观测的数字面:那次调整在两把钟之间,留下了永久的 106 毫秒。

> 输出里的那两行 ppm,笔者得自首一下:t3 的算式把比值多除了一个一千,t3 打出来的 0.000 与 −0.003,都是真值的千分之一,数字是不能当标定值用的。这跟 E1 里 freq 那一栏的错不是同一类:那边的错在量纲,这边的错在算式。好在本篇的判断不靠它,靠的是上面那些原始的微秒与毫秒,您复算的时候也请按原始读数来。

到这里选钟的主要判据都齐了,咱们顺手把它们摆上桌面:量耗时与超时的路,咱们走 MONOTONIC,它的连续性有实测背书。挂起也要计数的程序,咱们走 BOOTTIME,语义里多送一段挂起的时长。对比测量要纯硬件速率的,咱们走 RAW,代价嘛,它是不跟任何人同步的。要的是海量又便宜的时戳,咱们走 COARSE,记得它只有节拍级的鲜度就好。REALTIME 的位置留给墙上时间本身,E6 里咱们看它的正经用法。

## E4:vDSO:免票通道的兑现

[总纲](../../00-overview.md)讲系统调用成本的那阵子留过一段话:Linux 把 clock_gettime 这类高频调用做成了 vDSO,内核把代码和数据直接映射进您的进程,连陷入都免了,算是内核官方给出的免票方案,到了时间那一章,咱们还会遇到它。现在就是时间这一章了,咱们把那句话兑现成三样看得见的证据:映射的在场,代价上差出的一个数量级,还有 strace 挂上之后的零调用。

证据一看的是映射本身,咱们读自己的 `/proc/self/maps`,把 vdso 与 vvar 开头的行挑了出来:

```text
== E4a: 本进程地址空间里的内核映射(/proc/self/maps) ==
78e194ba3000-78e194ba7000 r--p 00000000 00:00 0                          [vvar]
78e194ba7000-78e194ba9000 r--p 00000000 00:00 0                          [vvar_vclock]
78e194ba9000-78e194bab000 r-xp 00000000 00:00 0                          [vdso]
...
```

映进来的这几块,没有一块是咱们自己申请的:内核在进程启动时就把它们铺好了。r-xp 的 [vdso] 是代码页,放着内核版的一小段 clock_gettime。r--p 的 [vvar] 是数据页,装着时钟源的换算参数,更新它们的是内核。中间的 [vvar_vclock],是新版内核给各把钟分出来的数据页。glibc 的 clock_gettime 会跳进 vdso 里的代码,在用户态直接拿 vvar 的参数算出时间,一次陷入都省了。

证据二看的是代价,咱们面对的还是同一个函数,拿两种写法各跑一千万次:一种走普通的 `clock_gettime`,另一种拿 `syscall(SYS_clock_gettime, ...)` 发同样的请求。后者是拿系统调用号直接陷入的写法,写法上绕开了 vDSO,正好当真陷入的对照组。

> syscall(2) 这个库函数本身的用处,就是拿调用号手工发起系统调用的直写法。新内核的接口有时等不来 glibc 的包装,咱们就得用它,[io_uring 篇](../io-multiplexing/03-io-uring.md)里咱们已经打过交道。

```cpp
auto t0 = now_ns();
for (int i = 0; i < kN; ++i) clock_gettime(CLOCK_MONOTONIC, &ts); // vDSO 路径
auto t1 = now_ns();
for (int i = 0; i < kN; ++i)
    syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts); // 强制走 syscall(2) 真陷入
auto t2 = now_ns();
```

```text
== E4b: 每次读取的代价(10000000 次) ==
clock_gettime(vDSO)   : 16.9 ns/次
syscall(SYS_clock_gettime): 168.9 ns/次(强制真陷入)
倍数: 10.0x —— 同一个函数,免陷入的差距就在这里
...
```

免陷入的差距就是十倍,复跑一轮给的是 16.6 对 169.1,还是稳稳地停在这个量级。拿到手的两个数,咱们还能各找一个对头:真陷入的 168.9 纳秒,与总纲里 getpid 的 123.72 纳秒是同一个量级,那次立的卷级直觉在这儿对上了,成本曲线咱们不重讲。vDSO 的 16.9 纳秒,又与 E2c 里 MONOTONIC 的读取代价 16.2 纳秒咬合,量的是同一条路,量了两次,数是对得上的。

证据三咱们请出 strace,它只拦得住真的陷入,拿它验有没有真陷入是最有说服力的。同一个程序的两百万次读取,咱们给两种走法各挂一次 `strace -c -e trace=clock_gettime`,输出咱们摆在下面。

```text
$ strace -c -e trace=clock_gettime ./t4_vdso_strace vdso
done: 2000000 次读取,走法=clock_gettime(vDSO),最后一次=38681.365261548

$ strace -c -e trace=clock_gettime ./t4_vdso_strace syscall
done: 2000000 次读取,走法=syscall(2) 真陷入,最后一次=38899.933588504
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
100.00   41.354410          20   2000000           clock_gettime
...
```

vdso 的那一场,strace 连统计表都省了,因为一次 clock_gettime 的系统调用都没有发生过。syscall 的那一场,表里 clock_gettime 的条数是两百万整,与读取的次数刚好对上。一张空白的统计表,就是免票最直接的物证。表格里 usecs/call 的 20 微秒倒也值得看一眼:那是 strace 自己拦下来记一笔的代价,把一百多纳秒的调用拖成了微秒级,所以拿挂了 strace 的程序量性能,您得留个心眼,咱们心里有数就好。

## E5:CPU 时间:睡觉不长,烧 CPU 才长

剩下的两把钟,记的是 CPU 时间:谁烧的 CPU 多,谁的读数就长得快,跟墙上流逝的时间走的不是一条路。t5 的安排是主线程睡满 300 毫秒,配一个纯忙转的工作线程,然后咱们拿四路读数对表:进程钟、线程钟、getrusage(问内核要进程资源用量的接口),外加 /proc/self/stat 的第四路。

```text
== E5: CPU 时间记在谁的时钟上 ==
安排: 主线程睡 300ms,工作线程忙转 300ms;进程 CPU 应≈工作线程的量

主线程睡满 300ms,它自己的 CPU 时钟只长了 93346 ns(睡觉不增 CPU 时间)
工作线程纯忙转,进程 CPU 时钟长了 298004743 ns ≈ 298.0 ms
对表: getrusage(RUSAGE_SELF) 记 298.1 ms;/proc/self/stat(tick 口径)记 290 ms(1 tick=1/100 s)

主线程也忙转一段: 线程钟 +183.8 ms,进程钟 +183.8 ms(此时两者同源同速)
用途对表: 限制 CPU 配额(CPULIMIT 类工具)、线程级性能剖析,读的就是这两个 clockid
```

咱们看头一行的反差,那就是 CPU 时间的性质:主线程睡满了 300 毫秒,它自己的线程钟只长了 93346 纳秒,九十几个微秒的量,那是起线程、做同步这一类杂活在它头上记下的系统时间。睡觉本身嘛,是一纳秒都不长的,墙上的三百毫秒花出去了,记到 CPU 时间头上的只是零头。忙转的工作线程把进程钟顶到了 298.0 毫秒,因为这个窗口里烧 CPU 的只有它一个,进程钟记的也就全是它的量。

E2 里那句预告也该兑现了:THREAD_CPUTIME_ID 的中位粒度停在 341 纳秒,比单调钟粗了一整档。到这里咱们知道它粗在哪了:CPU 时间的读数要从累计的记录里来,不是单调钟的直路,粒度的代价就差在这一段。

对表的那几个数才是正戏,同一个 298 毫秒的 CPU 时间,三路口径记出来的是三个数:纳秒粒度的进程钟记 298.0,getrusage 的微秒粒度记 298.1,而 `/proc/self/stat` 的 tick 口径记 290。tick 的一格是十毫秒,298 毫秒的活,落进十毫秒的格子里只能记下 29 格,290 就是这么来的。读 tick 的代码还藏着一个小机关:comm 字段里是可能带空格的,咱们解析的时候得从最后一个右括号往后数。

> man 5 proc 给 /proc/self/stat 列了几十个子字段,utime 排的是第 14 个,咱们代码里从右括号之后数过 11 个 %*s 才轮到它,差了一个字段,读出来的就是隔壁的进程属性。

```cpp
char* p = std::strrchr(buf, ')'); // 跳过 comm 字段(里面可能带空格)
unsigned long ut = 0, st = 0;
// p+2 起是第 3 字段 state;utime/stime 是第 14/15,前面要跳过 11 个
if (p) std::sscanf(p + 2, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu", &ut, &st);
return double(ut + st) / sysconf(_SC_CLK_TCK);
```

E1 里 CLK_TCK 与内核节拍的分家,在这儿落到了实处:格子的粗细由 CLK_TCK 的 100 决定,与内核 250Hz 的节拍是两回事,三路口径的粒度粗细,直接写在了读数上,咱们一眼就能对出来。

后半段的实验,咱们把主线程也拉去忙转,线程钟与进程钟同步长了 183.8 毫秒,两把钟是同源同速的,差别只有记的范围:线程钟记的是一根线程,进程钟记的是进程里所有线程的总和。用途也就分开了,线程级的性能剖析,读的是 CLOCK_THREAD_CPUTIME_ID,看的是哪根线程在烧 CPU。配额类的工具,像限制整个进程只能用多少 CPU 的那些,读的是 CLOCK_PROCESS_CPUTIME_ID。

## E6:墙上时间的呈现链:epoch 不动,动的是解释规则

最后咱们看给人读的时间。CLOCK_REALTIME 读回来的 time_t,是 1970 年起算的 UTC epoch 秒,它自己不带任何的时区信息,要变成屏幕上的本地时间,咱们得走一趟呈现链:localtime_r 查的是规则,strftime 管的是排版。呈现链的代码就几行,咱们看 t6 的 stamp,每一档走的都是同样的一遍:

```cpp
std::time_t t = ts.tv_sec;
std::tm gm{}, lc{};
gmtime_r(&t, &gm);
localtime_r(&t, &lc);
char g[64], l[64];
std::strftime(g, sizeof g, "%Y-%m-%d %H:%M:%S UTC", &gm);
std::strftime(l, sizeof l, "%Y-%m-%d %H:%M:%S %Z %z", &lc);
```

gmtime_r 与 localtime_r 都带着 _r 的后缀,是给可重入准备的版本,结果写进调用方给的 struct tm,多个线程各查各的也就不打架。时区的切换咱们看 t6 的后一半,咱们让同一个 epoch 秒把 TZ 环境变量切上四档:

```cpp
setenv("TZ", "UTC0", 1);      tzset(); stamp("TZ=UTC0");
setenv("TZ", "America/New_York", 1); tzset(); stamp("TZ=America/New_York");
setenv("TZ", "Asia/Tokyo", 1);        tzset(); stamp("TZ=Asia/Tokyo");
```

```text
== E6: 同一 epoch 值的两种呈现 ==
默认 TZ      epoch=1791121296.748978340
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 21:41:36 CST +0800  (TZ=(未设,读 /etc/localtime))
TZ=UTC0        epoch=1791121296.749025930
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 13:41:36 UTC +0000  (TZ=UTC0)
TZ=America/New_York epoch=1791121296.749062289
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 09:41:36 EDT -0400  (TZ=America/New_York)
TZ=Asia/Tokyo  epoch=1791121296.749075103
               gmtime_r   → 2026-10-04 13:41:36 UTC
               localtime_r→ 2026-10-04 22:41:36 JST +0900  (TZ=Asia/Tokyo)
...
```

四行 localtime_r 的读数给出的 21:41:36、13:41:36、09:41:36、22:41:36,指的是同一个瞬间:epoch 秒是没动过的,动的只是解释它的规则库。规则的来源是有次序的:设置了 TZ 环境变量,咱们就按 TZ 走,没设的话,咱们就去读 /etc/localtime,本机的 /etc/localtime 指向 Asia/Shanghai。setenv 改了 TZ 之后,咱们还得配一次 tzset,不然加载的还是老一套的规则。gmtime_r 的那一列始终给的是 UTC,咱们把两边一对照,规则做了什么也就一目了然了。

所以时区问题在 POSIX 的世界里是呈现层的事。您把日志的时间戳存成 epoch 秒,存的是绝对时刻,您把它拿到哪个时区去打开,指的都是同一个瞬间。要是把本地时间的字符串存了进去,夏令时的歧义就再也甩不掉了,凌晨那重复的一个钟头,存进去之后就分不出前后了。这样的分层,下一篇会被 chrono 收进类型系统:epoch 对上的是 sys_time,规则库对上的是 zoned_time,连闰秒都会有一个不跳变的 utc_clock 接着,TAI 的那 37 秒也会再出场。咱们到那边接着走。

## 另一侧怎么看

Windows 那边没有 clockid 这样的统一入口,钟是散在几个 API 里的,分工倒是都对得上。GetTickCount64 是开机起算的毫秒计数,单调而便宜,咱们在 [IOCP 篇](../../windows/async-io/02-iocp.md)里拿它当毫秒时戳用,个别时戳比设定间隔宽出来的那一截,就是它十几毫秒量化的动静。QueryPerformanceCounter 是高精度的单调计数器,咱们在 [文件映射篇](../../windows/file-io/02-file-mapping.md)里拿它当计时器用。墙上时间那边的粗精两档,给的是 GetSystemTimeAsFileTime 与 GetSystemTimePreciseAsFileTime 两个函数,与咱们 COARSE 对细尺的分层同型,这一对咱们按文档口径引用,本卷的实验还没轮到它们。

实测的锚点还是 [共享内存篇](../../windows/memory/02-shared-mem.md)那一场:Sleep(5) 实睡了约 12.6 毫秒,因为 Windows 默认的定时器分辨率是 15.6 毫秒一档,五毫秒的请求向上取整到下一格,实验里是没有调 timeBeginPeriod 的,如实地入了档。咱们对照着看,两边粗档的身份就不一样了:Linux 的 COARSE 是个显式的粗读档,主动权在您的手里,Windows 的 15.6 毫秒是定时器系统的默认粒度,Sleep 这类等待是绕不开它的,想细化的话,您得全局调一次 timeBeginPeriod。下一篇咱们进 C++20 chrono 的日历与时区,把本篇的 epoch、TAI 的 37 秒、时区规则逐一接进类型系统,定时器的全景,咱们留给最后一篇。
