# 01-clock-sources 配套实验

《时钟源与 POSIX 时间 API》(vol8 systems-programming/linux ch05 L01)的实验代码与原始输出存档。核心问题一句话:**机器里有哪几把钟、每把钟读出来的是什么、为什么量耗时只能用单调钟**。

与相邻篇卷的分工:
- 00-overview 讲系统调用成本时提过 vDSO 是免票通道,本篇 E4 正面验证它(maps 映射、每次读取代价、strace 零系统调用)。
- ch04 L02(timerfd 与 eventfd)讲过到点精度(1ms 档中位 999.3µs),本篇 E2 讲的是另一件事:读时钟本身的粒度,不重测定时器。
- Windows 侧的 Sleep(5) 实睡 12.6ms(windows/memory/02-shared-mem)是定时器分辨率的对照材料,本篇不重测。
- 本篇 E6 的墙上时间呈现链(time_t/localtime/TZ)直接接下一篇 chrono 的分层。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra`(t5 加 `-pthread`) |
| glibc | 2.44 |
| 时钟源 | tsc(/sys/devices/system/clocksource/clocksource0/available 里还有 hyperv_clocksource_tsc_page / hyperv_clocksource_msr / acpi_pm) |
| 计时 | CLOCK_MONOTONIC |

`.out` 出自 2026-10-04 的同一轮;t2/t4 的计时轮各复跑过一次(`t2_rerun.out` / `t4_rerun.out`)。读数里的秒值每次都不同,引用的是档位与分布形状。

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `t1_clock_census.*` | E1 时钟族普查 | 九个 clockid 的 getres 与读数;COARSE 两把的分辨率是 4ms(=1/内核 HZ=250),而 sysconf 的 CLK_TCK=100 是另一套用户态口径;TAI−REALTIME 恰为 37s(1972 以来闰秒累计);BOOTTIME−MONOTONIC≈0 且 /proc/uptime 与 BOOTTIME 对得上=本次开机没挂起过;adjtimex 只读显示时钟处于已同步状态 |
| `t2_read_granularity.*` | E2 分辨率≠精度 | getres 报 1ns,但背靠背连读 100 万次的相邻间隔中位是 20ns、p99 21ns——纳秒是刻度单位,不是读取精度;COARSE 的 100 万次读只有 3 个不同值(台阶);每次读取代价:MONOTONIC 16.2ns、COARSE 2.0ns |
| `t3_continuity.*` | E3 连续性 | MONOTONIC 200 万次连读零回退;10s 窗口 1kHz 监测,REALTIME−MONOTONIC 偏移峰谷差 23.3µs、无步进;RAW−MONOTONIC 稳在 −106.4ms 不动、当下分歧率 0.000ppm——分歧是本次开机早期的历史累积,当下没有频率纪律在拉 |
| `t4_vdso.*`(+`t4_vdso_strace.*`) | E4 vDSO | maps 里有 [vvar]/[vvar_vclock]/[vdso];clock_gettime 走 vDSO 16.9ns/次,syscall(2) 强制真陷入 168.9ns/次,差 10.0 倍;strace 下 vDSO 版零系统调用、syscall 版恰好 200 万次 |
| `t5_cpu_clocks.*` | E5 CPU 时钟 | 主线程睡 300ms 自己的线程钟只长了约 0.1ms,工作线程忙转则进程钟长 298.0ms——睡觉不增 CPU 时间;对表:getrusage 298.1ms(µs 粒度),/proc/self/stat 的 tick 口径 290ms(10ms 粒度,粒度差可见) |
| `t6_walltime_chain.*` | E6 墙上时间呈现链 | 同一个 epoch 值,gmtime_r 给 UTC、localtime_r 给本地,切 TZ=UTC0/America/New_York/Asia/Tokyo 只换规则不换时间本身;TZ 未设时读 /etc/localtime |

## E3 的 RAW−MONOTONIC 累积分歧(独家记录)

本机开机约 11 小时后,MONOTONIC 比 RAW 慢 106.4ms(平均 −0.003ppm),但 10s 窗口内的当下分歧率是 0.000ppm、adjtimex 报已同步。结论:分歧来自开机早期的一次性频率调整历史(WSL2 宿主时间同步的动作),不是当下持续在拉。这正是 RAW「不受 NTP 频率调整影响」的可观测面——两把钟的差值就是频率调整的累计。WSL2 无法 root 步进(无 sudo),REALTIME 可跳变的结论按文档口径 + 10s 无步进的被动监测入册,不冒充做过步进实验。

## 复现

```sh
g++ -std=c++20 -O2 -Wall -Wextra -o t1_clock_census t1_clock_census.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t2_read_granularity t2_read_granularity.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t3_continuity t3_continuity.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t4_vdso t4_vdso.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t4_vdso_strace t4_vdso_strace.cpp
g++ -std=c++20 -O2 -Wall -Wextra -pthread -o t5_cpu_clocks t5_cpu_clocks.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t6_walltime_chain t6_walltime_chain.cpp

./t1_clock_census   # 秒级
./t2_read_granularity # 约 5 秒
./t3_continuity     # 约 11 秒
./t4_vdso           # 约 4 秒
./t5_cpu_clocks     # 约 1 秒
./t6_walltime_chain  # 秒级

# strace 对照(t4_vdso_strace.txt 的来源):
strace -c -e trace=clock_gettime ./t4_vdso_strace vdso    # 无 clock_gettime 行
strace -c -e trace=clock_gettime ./t4_vdso_strace syscall # 200 万次
```
