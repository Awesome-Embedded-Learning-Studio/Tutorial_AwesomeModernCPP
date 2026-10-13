---
title: "C++20 std::chrono 深度:日历与时区"
description: "std::chrono 的钟怎么落在 OS 的时钟上、闰秒与时区在类型系统里长什么形状:本篇在 is_same 之外做运行期对拍(system_clock 与 clock_gettime(CLOCK_REALTIME) 背靠背差 -411 纳秒、steady_clock 对 CLOCK_MONOTONIC 差 -50 纳秒,同源),同一瞬间四把尺子(utc−system=+27 秒、TAI−UTC=+37 秒与上一篇内核侧 CLOCK_TAI−CLOCK_REALTIME 的 37 秒对上、GPS−UTC=+18 秒),闰秒进类型系统(utc_clock 轴上 2016-12-31 23:59:60 真实存在、get_leap_second_info 在闰秒本体 is_leap_second=1 而 elapsed=27、tzdb 闰秒表 27 条首条 1972-07-01 末条 2017-01-01),时区硬案例(纽约 2026-11-01 的 01:30 出现两次,choose::earliest 给 05:30Z EDT 而 latest 给 06:30Z EST,2026-03-08 的 02:30 不存在、映射出去到 03:00 回不来,不带 choose 的构造分别抛 ambiguous_local_time 与 nonexistent_local_time,异常文本完整入册,中国 1988-04-17 02:30 当年不存在、七月 CDT +0900 一月 CST +0800),tzdb 落在 OS 上(strace 证 openat 恰好 /usr/share/zoneinfo/tzdata.zi 与 leapseconds 两处、首次 get_tzdb 1.751 毫秒再次 50 纳秒同一引用),收尾一行日志四列(测量 steady、持久化 epoch、审计 utc、呈现 zoned)"
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 27
prerequisites:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "chrono：duration、时钟与 C++20 日历"
related:
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
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

# C++20 std::chrono 深度:日历与时区

[上一篇](./01-clock-sources.md)咱们把内核那一侧的时钟摸了个遍,九个 clockid 的脾气都量过了,CLOCK_TAI 与 CLOCK_REALTIME 之间差着的那 37 秒咱们也见过了,vDSO 的免票通道也走了一遭。可真到了写应用的时候,您手指敲出来的多半不是 `clock_gettime`,而是 `std::chrono`。那么问题就来了:标准库家里的两个常客 `system_clock` 与 `steady_clock`,它们跟上一篇的那几把 `CLOCK_*` 究竟是什么关系?是标准库自己另起炉灶造了一套的钟,还是就垫在了内核那几把上面?

咱们再往深处问两句。就说闰秒吧,上一篇咱们只在两个时钟的差值里见过它,可等它真的落到了时间轴上,那就是一天里真真切切多出来的那一秒。您去看 2016 年 12 月 31 日的深夜,那天夜里的 23 时 59 分 60 秒,秒数真的能是 60。这样的时刻,类型系统里装得下吗?还有时区,中国从 1986 到 1991 年间是实行过夏令时的,1988 年 4 月 17 日的凌晨两点半,在当年的上海就是一个不存在的时刻,您说 C++ 管得了这样的事吗?

地界咱们划在前面。标准库卷的 [chrono 篇](../../../../vol3-standard-library/time-numeric/58-chrono.md)已经把库本身的正课上完了,duration 的编译期分数运算,`steady_clock` 凭什么包揽了测耗时,`2026y/June/Sunday[last]` 那样的日历字面量,还有 format 与 parse 的说明符,全在那边讲过了,本篇是不重讲的。咱们这边是系统编程卷,要问的是另一类问题。E1 问的是 chrono 的钟怎么落在 OS 上,E2 看的是闰秒在类型系统里的形状,E3 把歧义时刻与不存在时刻的把关问清楚,E4 查的则是时区数据库从哪来、加载要花多少钱。日历类型在 E5 只做一轮与时间轴的对表,到了收尾的 E6,咱们把同一瞬间的四个读法放进同一行日志。

实验的编号是 E1 到 E6,对应的是仓库 `code/volumn_codes/vol8/systems-programming/linux/time/02-chrono-calendar/` 存档下的 t1 到 t6 六组文件,代码与全部的原始输出都入册了。本篇的 E 只认本篇:上一篇时钟源篇自己的 E 系、下一篇定时器篇的 E 系,就算是同号的,说的也不是一回事,您翻存档的时候认目录就好。正文里的输出块多数是节选,整段略去的与段头横幅直接拿掉,段内删掉的行以 `...` 标出,咱们拿存档对表的时候,还是要以存档为准的。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2 的构建,CPU 用的是 AMD Ryzen 7 9700X,g++ 的版本是 16.2.1,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra`(t6 另加 `-pthread`),glibc 的版本是 2.44,全部实验的警告数是零。时区数据库用的是发行版 tzdata 的 2026c 版,zones 收了 341 条,links 收了 257 条,闰秒表收了 27 条,本机的 `/etc/localtime`,指到的是 Asia/Shanghai。全部 `.out` 出自 2026-10-04 的同一轮,凡是跟当下时刻相关的读数,还有计时类的数字(纳秒级的差值、毫秒级的加载耗时),每次复跑都是会变的,咱们引用的是条数、档位与行为。还有一条环境限制咱们如实记下,本机装的人类语言 locale 只有 en_US.utf8,chrono 里跟本地化相关的格式化就只能给英文档了,中文月份名在本机是出不来的。当然了,这是环境的口径,不是库的缺陷。

## E1:时钟落点:chrono 的钟与内核的钟对拍

### is_same 只管类型那一半

`high_resolution_clock` 是谁的别名,这桩公案咱们在 vol3 的 chrono 篇里已经断过了,本机的 g++ 16.2.1 给出的还是同款答案:

```text
is_same<high_resolution_clock, system_clock> : 是
is_same<high_resolution_clock, steady_clock> : 否
system_clock::is_steady=0  steady_clock::is_steady=1
```

别名说的是类型层面的同一,`is_same` 在编译期就能把它回答了。可咱们真正想知道的是运行期的事:chrono 的 `now()` 落在内核的哪把钟上?标准对此只规定了语义,`system_clock` 的纪元得是 Unix 纪元,`steady_clock` 要求的是单调,至于底下读的是哪个时钟,那就是实现的自由了。libstdc++ 在 Linux 上是怎么落的,咱们拿实验来看,是不猜的。

### 背靠背读一把,差值就是读取延迟

对拍的办法很朴素,咱们在同一瞬间背靠背地读两次,一次走的是 chrono,另一次走的是 `clock_gettime`,差值要是落在了读取延迟的量级,那它们读的就是同一把钟:

```cpp
auto a = ch::system_clock::now().time_since_epoch().count(); // 纳秒
timespec ts{};
clock_gettime(CLOCK_REALTIME, &ts);
auto b = ch::system_clock::duration(
    ch::seconds(ts.tv_sec) + ch::nanoseconds(ts.tv_nsec)).count();
std::printf("system_clock − CLOCK_REALTIME = %lld ns → %s\n", (long long)(a - b),
            std::llabs(a - b) < 100000 ? "同源(都读墙上钟)" : "不同源?");
```

t1 跑出来的输出,咱们贴在下面:

```text
system_clock − CLOCK_REALTIME = -411 ns → 同源(都读墙上钟)
steady_clock  − CLOCK_MONOTONIC  = -50 ns → 同源(都读单调钟)
```

两次读数之间隔了一次函数调用,-411 纳秒与 -50 纳秒正好就是这点工夫的花费,判据用的是 100 微秒,差得再远就谈不上同源了。纳秒级的差值逐轮会变,这一轮咱们看到的是 411 与 50,复跑出来的可能是三百多,也可能是二十几的样子,稳住的只有档位。所以答案可以落下来了:libstdc++ 的 `system_clock` 读的就是 `CLOCK_REALTIME`,`steady_clock` 读的就是 `CLOCK_MONOTONIC`,标准库是没有另造钟的,它是垫在内核那两把上面的。上一篇 E4 量出来的结果是走 vDSO 的 `clock_gettime` 单次 16.9 纳秒,既然这里走的是同一次调用,免票通道对 `chrono::now()` 也就原样生效了,您是不用为标准库多付一层系统调用的钱的。

### 同一瞬间,四把尺子

到了 C++20 之后,chrono 家里多了三把新尺子:`utc_clock`、`tai_clock`、`gps_clock`。咱们要做换算,就得知道四把尺子的零刻度画在哪:`system_clock` 与 `utc_clock` 的纪元都是 1970-01-01 00:00:00 UTC,tai(国际原子时)的纪元画在 1958-01-01,gps(全球定位系统)的画在 1980-01-06,这些都是标准写死的。四把尺子量的是同一个时间流,零刻度画的位置又不同,咱们要看的物理偏移,是把零点差扣掉之后剩下的部分:

```cpp
auto u   = ch::system_clock::now();
auto utc = ch::clock_cast<ch::utc_clock>(u);
auto tai = ch::clock_cast<ch::tai_clock>(u);
auto gps = ch::clock_cast<ch::gps_clock>(u);
```

咱们把 t1 里 E1c 段的输出贴出来:

```text
system_clock 纪元 1970-01-01 UTC:  1791121304.997 s(UTC 刻度,不数闰秒)
utc_clock    纪元 1970-01-01 UTC:  1791121331.997 s(UTC 刻度,1972 以来 27 次闰秒都数进去)
tai_clock    纪元 1958-01-01 TAI:  2169812541.997 s
gps_clock    纪元 1980-01-06 GPS:  1475156522.997 s

把纪元差扣掉之后,剩下的就是物理偏移:
utc − system                     = +27 s → 1972 以来插入的闰秒数
tai − system − 378,691,200(纪元差)= +37 s → TAI−UTC 当下值
gps − system + 315,964,800(纪元差) = +18 s → GPS−UTC 当下值(GPS 领先)
(TAI−UTC=37 对表篇 1 E1 的 CLOCK_TAI−CLOCK_REALTIME=37 s:内核与库读同一份闰秒数据)
```

四个读数的小数部分都是 .997,它们确实是同一瞬间的四个读法。剩下的差异全在整秒上,咱们一把一把地看。`system_clock` 的轴是 POSIX 的刻度,一天是固定 86400 秒的,闰秒是不数的,这就是它跟 `utc_clock` 差 27 秒的原因:utc 的轴把 1972 年以来插入的 27 次闰秒全数了进去,所以同一个物理时刻在它那里,读数就大了 27。TAI 和 GPS 的这两把也都不数闰秒,哥俩之间是恒差着 19 秒的,GPS 在 1980 年上岗的那天,TAI−UTC 的差恰好是 19 秒,后来 UTC 又插了 18 次闰秒,所以 TAI−UTC 涨到了 37,GPS−UTC 停在了 18。您拿 37 减 19,得到的正是 18。

这里面藏着时钟源篇与本篇之间的一条暗线,咱们把它点破:上一篇 E1 的输出里,给的是 `TAI - REALTIME = 37000000020 ns`,那是内核侧的读法,37 秒被揉在两个 clockid 的差值里给了咱们。这一篇 E1 的换算给出了同一个 37,这是库侧的读法,闰秒被做成了类型之间的 `clock_cast`。两种读法读的是同一份数据,而它的本体存在哪、又是从哪读进来的,E4 的 strace 会给咱们交代。

### steady_system_clock:无提案号可考,本机无货

标准里其实查不到 `steady_system_clock` 这个名字,C++20 新添的就是 utc、tai、gps 三把,而本机的 g++ 16.2.1 里这个名字编不过,也没有对应的特性测试宏,两样都是咱们实测过的。至于一把钟既单调又映射墙上时间的复合语义,眼下是没有提案号可查的,等真有它落地的那天再议也不迟。本篇的焦点,就放在已经落地的三把上。

## E2:闰秒在类型系统里的形状

### 23:59:60 真的在轴上

POSIX 的轴是装不下闰秒的:2016-12-31 23:59:59 的下一秒,直接就跳到了 2017-01-01 00:00:00,那一夜多出来的一秒在这把尺子上没有位置,就只能是靠标注的。而 `utc_clock` 的轴把闰秒数了进去,咱们就让加法发生在它身上:

```cpp
auto t235959 = clock_cast<utc_clock>(
    sys_days{2016y / December / 31} + 23h + 59min + 59s);
auto leap  = t235959 + 1s;  // utc_clock 的加法:这一秒是闰秒
auto after = leap + 1s;     // 越过闰秒
```

t2 的输出,咱们贴出 utc 这边的四行:

```text
...
utc_clock: 2016-12-31 23:59:59
utc_clock +1s = 2016-12-31 23:59:60   ← 23:59:60 真的在轴上
utc_clock +2s = 2017-01-01 00:00:00
闰秒所在秒的亚秒位: 2016-12-31 23:59:60 60
```

秒位打到了 60,格式化器也是认的,`%S` 在闰秒上给出的就是 60。同样的 `+1s` 若是发生在 sys 轴上,读数是会直接跳到 2017-01-01 00:00:00 的,两把尺子的差别在这一次加法里看得清清楚楚。您要是做过跟授时对表的系统,应该马上能感到这件事的分量:23:59:60 从此不再是一个要靠 `if` 特判的字符串,它是轴上一格正经的时刻。

### get_leap_second_info:这一秒是不是闰秒

光是能格式化还不够的,咱们还得能问:某个时刻是不是闰秒?从纪元到现在,一共数过几个了?C++20 的 `get_leap_second_info` 就是干这个的,本机的 g++ 16.2.1 里有货。t2 在四个时刻各问了一遍:

```cpp
auto a = get_leap_second_info(t235959); // 闰秒前一秒
auto b = get_leap_second_info(leap);    // 闰秒本体
auto c = get_leap_second_info(clock_cast<utc_clock>(sys_days{2017y / January / 2}));
auto d = get_leap_second_info(clock_cast<utc_clock>(sys_days{2015y / January / 2}));
```

```text
2016-12-31 23:59:59 → is_leap_second=0, 已经历闰秒=26
2016-12-31 23:59:60 → is_leap_second=1, 已经历闰秒=27(含当次)
2017-01-02          → is_leap_second=0, 已经历闰秒=27
2015-01-02          → is_leap_second=0, 已经历闰秒=25
```

读法很直白:输出里标着已经历闰秒的那一栏,打的就是 `elapsed`,它告诉咱们到此为止轴上数过了几个闰秒,闰秒本体的那一秒 `is_leap_second=1`,经历的个数含当次是 27,而前一秒是 26,跨过 2017 年的那个凌晨之后就稳定在 27 了,而 2015 年的年初是 25。您把表里的数跟 E1 的 `utc−system=+27` 对一下,两处说的其实是同一件事:一个是从换算的差值里读出来的,一个是从逐时刻的查询里读出来的。

### 表上 27 条,10 加 27 才是 37

闰秒的记录本体,咱们在 tzdb(chrono 进程里的时区数据库,由 `get_tzdb()` 取)里翻出来了:

```text
tzdb 版本 2026c,leap_seconds 条数=27
首条 1972-07-01,末条 2017-01-01
...
```

表里的第一条落在 1972 年 7 月,那是闰秒机制开局的那年,末条则落在 2017 年的 1 月,也就是 2016 年岁末插入的那一次,从那以后就再没插过了,负闰秒更是从来没发生过的。这里有个容易对不齐的地方,咱们把它算平:TAI−UTC 当下是 37 秒,表上却只收了 27 条,差出来的 10 秒是 1972 年机制开局时已经存在的初始偏移。10 加 27 的和是 37,内核那边 `CLOCK_TAI` 快出的 37 秒,库这边 `tai−utc` 的 37 秒,跟表上的 27 条加上初始偏移,三处是对得上的。

那什么时候真的该用 `utc_clock` 呢?咱们给一个工程判断:时间轴要经得起闰秒拷问的场合,审计的日志,还有跟授时系统的对表,咱们就用它。一般业务要持久化的 epoch(纪元)值,`system_clock` 是足够的,到了 E6,咱们会把这套分工收拢清楚。

## E3:歧义与不存在:local_time 反向构造的硬案例

### 从墙上时刻往回构造,才见真章

`zoned_time(zone, sys_time)` 这个方向的构造是永远不会出事的:您给的是绝对时刻,套上时区只是换了个呈现,vol3 里演示的就是它。硬的是反方向:您从日志里、从用户的输入里读到了一个本地写法的时刻,拿它去构造 `zoned_time` 的时候,咱们就在逼类型系统回答一个问题,这句本地时间对应的是哪个绝对时刻?多数日子里这句话是没有歧义的,可一到夏令时的拨快拨慢之夜,答案就不止一个了,有时连一个答案都是没有的。

纽约时区是教科书里的常客,咱们拿它开场,中国自己的例子压轴。

### 拨慢之夜:同一个 01:30 来了两次

咱们来看拨慢之夜。到了 2026 年的 11 月 1 日,纽约要把 EDT 拨回 EST 了,凌晨两点往回拨成了一点,于是本地的 01:30 就要过上两遍:

```cpp
local_seconds ambiguous = local_days{2026y / November / 1} + 1h + 30min;
zoned_time earliest = zoned_time("America/New_York", ambiguous, choose::earliest);
zoned_time latest   = zoned_time("America/New_York", ambiguous, choose::latest);
```

咱们看 t3 的输出:

```text
01:30 choose::earliest(第 1 次,EDT): 2026-11-01 01:30:00 EDT -0400  (对应 UTC 2026-11-01 05:30:00)
01:30 choose::latest(第 2 次,EST):   2026-11-01 01:30:00 EST -0500  (对应 UTC 2026-11-01 06:30:00)
...
```

同一句本地时间,给出的是两个 UTC 答案,彼此差了一小时。`choose::earliest` 取的是拨慢之前的那一次,那时还是夏令时的 EDT,对应 UTC 的减四,`choose::latest` 取的是拨慢之后的那一次,变成 EST 了,对应 UTC 的减五。不带 `choose` 的直接构造,库是不会替您掷硬币的:

```text
不带 choose 的构造直接抛: 2026-11-01 01:30:00 is ambiguous.  It could be
2026-11-01 01:30:00 EDT == 2026-11-01 05:30:00 UTC or
2026-11-01 01:30:00 EST == 2026-11-01 06:30:00 UTC
```

`ambiguous_local_time` 异常的文本写得相当体贴,它把两个候选连同各自的 UTC 等价全打出来了,您拿去给用户看都不用改写。选边的事,静默处理的写法是明写 `choose`,想让它暴露的写法是让异常自己走,两条路咱们都认。唯独没想过问题的第三种,是谁也救不了的。

### 拨快之夜:02:30 根本不存在

咱们再来看拨快之夜。到了 2026 年的 3 月 8 日,纽约要把 EST 拨到 EDT 了,凌晨两点直接跳到了三点,本地的 02:30 是谁也对不上的:

```text
02:30 映射出去(不存在的时刻): 2026-03-08 03:00:00 EDT -0400  (对应 UTC 2026-03-08 07:00:00)
回看: 该 UTC 时刻的本地呈现是 2026-03-08 03:00:00 EDT → 原 02:30 回不来
不带 choose 的构造直接抛: 2026-03-08 02:30:00 is in a gap between
2026-03-08 02:00:00 EST and
2026-03-08 03:00:00 EDT which are both equivalent to
2026-03-08 07:00:00 UTC
```

带上了 `choose::earliest`,这个不存在的时刻就被映射到了 gap 之后的第一个有效时刻 03:00。验证的办法是回程再看一眼:把这个 UTC 时刻转回本地,得到的是 03:00,原来那句 02:30 是回不来的,这也就是不存在的操作性含义了。不带 `choose` 的构造抛的是 `nonexistent_local_time`,异常文本把 gap 的两个边界时刻也交代了。歧义与不存在的两类病,各配各的异常类型与异常文本,您在 catch 里就能分诊。

### 中国也实行过夏令时

咱们讲歧义与不存在,材料是不必去借纽约的。中国在 1986 到 1991 年间是实行过夏令时的,这段历史就收在 Asia/Shanghai 的 tzdata 里,t3 把 1988 年的记录翻了出来:

```cpp
local_seconds cn_ghost = local_days{1988y / April / 17} + 2h + 30min;
zoned_time cn = zoned_time("Asia/Shanghai", cn_ghost, choose::earliest);
```

```text
1988-04-17 02:30(拨快,不存在):    1988-04-17 03:00:00 CDT +0900  (对应 UTC 1988-04-16 18:00:00)
1988-04-17 03:30(存在,CDT +09):      1988-04-17 03:30:00 CDT +0900  (对应 UTC 1988-04-16 18:30:00)
1988-07-01 12:00(夏令时中段):     1988-07-01 12:00:00 CDT +0900  (对应 UTC 1988-07-01 03:00:00)
1988-01-15 12:00(冬令时):           1988-01-15 12:00:00 CST +0800  (对应 UTC 1988-01-15 04:00:00)
```

1988 年 4 月 17 日的凌晨,钟从两点拨到了三点,那天的 02:30 在中国是不存在的,行为跟纽约的 gap 一模一样。七月的正午是 CDT 加九,一月的正午是 CST 加八。两处的缩写都值得您多看一眼:CST 在这里是 China Standard Time,它跟美国中部标准时间的缩写是重了名的,CDT 指的则是 China Daylight Time,同样跟美国中部夏令时是重名的,您读日志的时候可别认错了门。而在那六年里,夏天的中国比现在快了一小时,这段历史不只是冷知识:凡是处理八十年代末九十年代初中国本地时间的数据,拨快拨慢的边界都得过一遍 E3 的检查。

## E4:tzdb 从哪来,花多少钱

### 首次 1.751 毫秒,再次 50 纳秒

前面两节咱们反复在查 tzdb,这个时区数据库在进程里是个什么形态,是得量一量的。t4 对 `get_tzdb` 计了两回:

```text
首次 get_tzdb(): 1.751 ms(解析时区数据库,之后常驻)
再次 get_tzdb(): 0.000050 ms(同一份引用,指针是 同一个)
...
locate_zone("Asia/Shanghai"): 0.922 us → Asia/Shanghai
current_zone(): Asia/Shanghai(读 TZ 环境变量,未设则 /etc/localtime,机制同篇 1 E6)
locate_zone("Mars/Olympus_Mons"): 抛 std::chrono::tzdb: cannot locate zone: Mars/Olympus_Mons
```

首次的 1.751 毫秒是解析整份时区数据库的价钱,解析完了就常驻,之后再取 `get_tzdb` 就是 50 纳秒了,还给您的是同一个引用,取指针这件事咱们验证过,确实就是同一份的。单个 `locate_zone` 是不到一微秒的。所以说,时区查询在咱们的进程里是进程级单例加一次解析的成本,热路径上您随便用,是不用担心反复构造的。`current_zone` 找本机时区的路数,跟上一篇 E6 里 `localtime_r` 找 `/etc/localtime` 用的是同一套环境约定,那边读它的是 libc,这边读它的是 libstdc++。坏区名的下场咱们也交代了:异常文本原样贴在上面,火星时区就暂不发货了。

### strace 看它读的是 OS 的哪两个文件

它的数据到底是从哪来的?咱们 strace 一把就有答案了:

```text
$ strace -e trace=openat ./t4_tzdb_load 2>&1 | grep zoneinfo
openat(AT_FDCWD, "/usr/share/zoneinfo/tzdata.zi", O_RDONLY) = 3
openat(AT_FDCWD, "/usr/share/zoneinfo/leapseconds", O_RDONLY) = 4
```

整个加载过程恰好 `openat` 了两个文件,规则与闰秒表分别收在了 `tzdata.zi` 与 `leapseconds` 里。E2 里闰秒表的来历,E1 四把尺子背后的换算数据,源头到这儿都交代了,libstdc++ 是不自带时区数据的,它解析的是发行版 tzdata 包的文件。这件事的工程含义比它看起来要重:两台机器的 tzdata 版本不同,对历史时刻的转换结果就可能不同,中国 1988 年那段夏令时要是哪天被新版本改了口径,两台机器算出来的绝对时刻就会分岔。vol3 的 chrono 篇提醒过精简容器里没有 tzdata 会抛异常,本篇补上的是正面的证据:容器镜像是该装上 tzdata 的,而且部署矩阵里各台机器的版本是值得对表的,咱们本机用的是 2026c,zones 收的是 341 条。

## E5:日历类型与时间轴对表

日历类型的正课在 vol3,字面量怎么写、类型怎么组合,那边是讲全了的。咱们在本篇只做一轮与主线的对表:日历值要进时间轴,进轴之前的把关是谁来做?咱们直接看 t5 的头一段:

```text
2026-02-30 .ok()=0  2025-02-29 .ok()=0  2028-02-29 .ok()=1  2026-03-32 .ok()=0
2026-02-30 转 sys_days = 2026-03-02(折算: 2026-02-01 + 29 天)
不合法的值还能存(ok() 只是标记),转 sys_days 不报错也不跳变,年月也无效时结果未指明,
合法性要在转入时间轴之前自己把关
```

`ok()` 就是咱们要的那道关。二月的三十号,平年二月的二十九,还有三十二号这样的值,咱们都造得出来,值也是存得下的,`ok()` 给的只是标记。转 `sys_days` 也不会替您挡,更不会乱来:年月合法、日子越界的值,标准规定折算的公式是当月 1 日加上 day 减 1 天,2026-02-30 就这样落成了 2026-03-02,不报错也不跳变的,而年月也无效的时候,结果就未指明了。所以说,合法性不是在构造的时候挡您的,它是您进时间轴之前要自查的,这跟 E3 的 `local_time` 是一个设计味道:类型系统把可能不对这件事摆在了明处,把什么时候查留给了您。

过了关的值,进轴之后就是咱们对表的料:

```text
2026 年 10 月的最后一个周日 = 2026-10-25(Sunday)
2026 年 10 月的第 1 个周一  = 2026-10-05
2026-10-04 的 weekday = Sunday(与 date 命令输出对表)
sys_days ↔ year_month_day 往返: 2026-10-04
2000-01-01 → 2026-10-04 共 9773 天(类型是 days,不是自己除 86400)
2026-10-04 → 年底之间的周日数 = 13(日历运算直接落在循环里)
```

写实验的当天是 2026 年的 10 月 4 日,刚好赶上了星期天,`weekday` 的输出跟 `date` 命令对上了。咱们把两个日期一相减,出来的类型就是 `days`,9773 这个数里是没有一个手写除法的。顺手咱们还数了数从今天到年底还有多少个周日,循环里用的就是日历类型本身:

```cpp
int sundays = 0;
for (sys_days d = today; d <= sys_days{2026y / December / 31d}; d += days{1})
    if (weekday{d} == Sunday) ++sundays;   // 数出来是 13
```

最后咱们看 `hh_mm_ss`:

```text
9025ms+234us → 00:00:09.025234(to_dur 回去还是同一个 duration)
负 duration -5min → is_negative=1,钟面 00:05:00(符号单列,域不掺假)
```

咱们再看负的时长,`is_negative` 把符号单列了出来,时分秒的三个域也不掺假,`to_dur` 还能把摆开的钟面原样变回同一个 duration,这就是它比在字符串前面拼一个负号的写法可靠的地方。

## E6:一行日志,四个读法

四问都问完了,收尾咱们把四个读法放进同一行日志。t6 模拟了一条从 connect 到 close 的事件流,五条事件打的都是四列,每条记录里存的就是两个时刻:

```cpp
struct LogLine {
    ch::nanoseconds since_start;        // steady 计的相对时长
    ch::system_clock::time_point wall;  // 墙上的绝对时刻
    const char* event;
};
```

到了打印的时刻,咱们拿一份记录就能换出四个读法:

```cpp
ch::nanoseconds elapsed = ch::steady_clock::now() - t0;          // 测量
auto wall    = ch::system_clock::now();                          // 持久化
auto utc_tp  = clock_cast<ch::utc_clock>(wall);                  // 审计
zoned_seconds local =
    zoned_seconds("Asia/Shanghai", floor<seconds>(wall));        // 呈现
```

输出咱们做了节选:

```text
elapsed    epoch(ns)                UTC(闰秒安全)              本地(Asia/Shanghai)
00:00:00.050596687 1791121305060672272      2026-10-04 13:41:45.060672272  2026-10-04 21:41:45 CST
00:00:00.103023497 1791121305113094954      2026-10-04 13:41:45.113094954  2026-10-04 21:41:45 CST
00:00:00.153700993 1791121305163772560      2026-10-04 13:41:45.163772560  2026-10-04 21:41:45 CST
...
```

四列的分工,咱们挨个收。elapsed 的一列来自 `steady_clock`,算的是相对开工过了多久,它的单调性有上一篇 E3 的两百万次连读零回退保着,测量咱们就用它。epoch 的一列来自 `system_clock`,存的是个纳秒整数,是可换算、可持久化、也可跨机器对时的,您要落库就用它。UTC 的一列来自 `utc_clock`,时间轴是数闰秒的,给人看的同时经得起审计的拷问,真到了 23:59:60 的那一夜,这一列是不会把那一秒吞掉的。本地的一列来自 `zoned_time`,规则出自 tzdb 的数据,E3 的全套检查护着它,它是给值班的人看的。

四列是同一瞬间的四个读法,0.050、0.103、0.153 的间隔排得整整齐齐,换算是全部走在类型系统里的,是没有一个手写除法的。您以后设计日志、设计事件流,这一行就是您的模板:四件事各有各的来源,咱们别让一把钟干四份活。

本篇讲的是读时间。下一篇咱们换成按时间办事:alarm、setitimer、timer_create、timerfd 组成的定时器家族,四种形态的语义与存亡,还有周期调度里相对睡眠与绝对锚定的漂移对照,咱们到那边接着走。
