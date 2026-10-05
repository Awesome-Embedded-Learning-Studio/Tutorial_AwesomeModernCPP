# 02-chrono-calendar 配套实验

《C++20 std::chrono 深度:日历与时区》(vol8 systems-programming/linux ch05 L02)的实验代码与原始输出存档。核心问题一句话:**标准库的时钟和日历是怎么落在 OS 的时钟与 tzdata 上的,闰秒和夏令时这两类「时间轴不均匀」在类型系统里长什么样**。

与相邻篇卷的分工:
- vol3《chrono:duration、时钟与 C++20 日历》(58-chrono)已正面讲过 duration 的编译期分数运算、steady_clock 测耗时的选型、year/month/day 字面量与 format/parse 的常规用法——本篇不重讲,只在 E1 做「chrono 时钟 ↔ POSIX 时钟」的落点对拍,日历(E5)只验证跟时间轴对表相关的行为。
- 篇 1(L01)的 E6 把墙上时间的呈现链(time_t/localtime/TZ)讲完,本篇 E3/E4 接 zoned_time 与 tzdb 这一层。
- steady_system_clock 在标准里查无此名(C++20 新添的就是 utc/tai/gps);g++ 16.2.1 无对应特性测试宏、名字编不过(均实测)——正文按「无提案号可考、本机无货」处理,不冒充实测。(2026-10-04 走查勘误:旧注安错的 P2592 号已撤,那是 chrono 哈希提案,与此时钟无关。)

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra`(t1/t4 不需线程,t6 加 `-pthread`) |
| tzdb | 版本串 2026c,zones=341 links=257 leap_seconds=27 |
| 计时 | steady_clock(即 CLOCK_MONOTONIC,见 t1 的对拍) |

`.out` 出自 2026-10-04 的同一轮;「现在」相关的读数每次都变,引用的是偏移量、条数与异常行为。

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `t1_clock_mapping.*` | E1 时钟落点 | high_resolution_clock 就是 system_clock 的别名(libstdc++);system_clock−CLOCK_REALTIME、steady_clock−CLOCK_MONOTONIC 的同期差在百纳秒档(当轮 −411ns/−50ns,逐轮变,判据 100µs)=同源;同一瞬间四把尺子:utc−system=+27s(闰秒数),TAI−UTC=+37s(与篇 1 E1 的 CLOCK_TAI−CLOCK_REALTIME 对上),GPS−UTC=+18s |
| `t2_leap_second.*` | E2 闰秒 | utc_clock 时间轴上 2016-12-31 23:59:60 真实存在(+1s 后格式化出 60 秒);get_leap_second_info 在闰秒本体上 is_leap_second=1、elapsed=27,前一秒是 26;tzdb 的 27 条闰秒表首条 1972-07-01、末条 2017-01-01 |
| `t3_zoned_dst.*` | E3 时区硬案例 | America/New_York 2026-11-01 01:30 本地时刻出现两次(choose::earliest→05:30Z EDT / latest→06:30Z EST),不带 choose 构造抛 ambiguous_local_time;2026-03-08 02:30 不存在(映射到 03:00 EDT,抛 nonexistent_local_time);中国 1988 年实行过夏令时:04-17 02:30 当年不存在、夏季 CDT +0900、一月 CST +0800 |
| `t4_tzdb_load.*`(+`t4_tzdb_strace.txt`) | E4 tzdb 落在 OS 上 | 首次 get_tzdb() 1.751ms(解析后常驻,再取 0.05µs 同一引用);strace 显示 openat 恰好两处:/usr/share/zoneinfo/tzdata.zi 与 leapseconds——时区数据不是库自带的,是发行版 tzdata 的文件;坏区名抛 `std::chrono::tzdb: cannot locate zone: ...` |
| `t5_calendar.*` | E5 日历类型 | ok() 是转入时间轴前的自查(2026-02-30/2025-02-29 不合法,2028-02-29 合法);年月合法、日子越界时转 sys_days 是定义良好的折算(sys_days(y/m/1d)+(day-1d)),实测 2026-02-30→2026-03-02,年月也无效则结果未指明;2026y/October/Sunday[last]=10-25;2000-01-01→2026-10-04 相差 9773 天,类型就是 days;hh_mm_ss 的负 duration 符号单列、域不掺假 |
| `t6_log_demo.*` | E6 打点日志实战 | 一条日志四列同瞬间的四个读法:steady 测差、system 给 epoch、utc 给闰秒安全时间轴、zoned_time 给本地呈现——四层各司其职,换算零手写除法 |

## 独家记录

- 中国夏令时(1986-1991)在 Asia/Shanghai 的 tzdata 里完整可查:1988-04-17 02:00→03:00 拨快、当年 02:30 不存在。拿「自己小时候经历过的时区」做歧义/不存在时刻的教例,比纽约的例子离读者近。
- 本机 locale 只有 en_US.utf8,没有 zh_CN——本地化格式化(L 修饰符)只能测英文档,这是 WSL2 的环境口径,不是库缺陷。

## 修订记录

- 2026-10-04 走查勘误批:t5 的 E5a 原判「转 sys_days 是未定义行为」有误,按 cppreference 改为「年月合法、日子越界时是定义良好的折算(sys_days(y/m/1d)+(day-1d)),年月也无效则未指明」,t5.cpp 增折算实测行(2026-02-30→2026-03-02)并重跑重录 .out,同轮把演示闰年 2024-02-29 换成 2028-02-29(避开 vol3 同款例值);t1 注释与分工节的 steady_system_clock 提案号(P2592)系张冠李戴,已撤;t1 行的「±350ns 内」与 .out 的 −411 不符,改「百纳秒档」;t4_tzdb_strace.txt 尾部误粘的残句已删。E5b/c/d 为确定性输出,重跑逐字节一致。

## 复现

```sh
for f in t1_clock_mapping t2_leap_second t3_zoned_dst t4_tzdb_load t5_calendar; do
  g++ -std=c++20 -O2 -Wall -Wextra -o $f $f.cpp
done
g++ -std=c++20 -O2 -Wall -Wextra -pthread -o t6_log_demo t6_log_demo.cpp

./t1_clock_mapping  # 秒级
./t2_leap_second    # 秒级
./t3_zoned_dst      # 秒级
./t4_tzdb_load      # 秒级
./t5_calendar       # 秒级
./t6_log_demo       # 约 1 秒

# strace 对照(t4_tzdb_strace.txt 的来源):
strace -e trace=openat ./t4_tzdb_load 2>&1 | grep zoneinfo
```
