# 02-timerfd-eventfd 配套实验

《timerfd 与 eventfd》(vol8 systems-programming/linux ch04 L02)的实验代码与原始输出存档。核心问题一句话:**时间与事件怎么化成 fd,从而与数据源进同一个 epoll 循环**。

与相邻篇卷的分工:
- ch03 信号下篇已经把 timerfd 当 signalfd 事件循环里的伙伴用过(150ms 周期与信号交错的 poll 表),本篇正面讲 timerfd 本体:一次性、周期、合并计数、改期撤销、精度。
- Windows 侧共享内存篇(02-shared-mem)在 WSL2 对照实验里用过 eventfd 当 SPSC 队列的通知件,本篇讲它的计数器语义与 epoll 配合的通知次数。
- epoll 的 LT/ET 语义归 01 篇与网络卷,本篇只用到结论。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra` |
| 计时 | CLOCK_MONOTONIC |

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `t1_eventfd_semantics.*` | E1 计数器语义 | write 是加法(1+2+5 一次 read 取走 8,再读 EAGAIN);EFD_SEMAPHORE 下 read 每次只减 1(写 5 → 五次 read 各得 1,第六次 EAGAIN);上限就是 2^64-2 本身(写它成功、再 write(1) 即 EAGAIN,.out 里"只差 1 满"的程序标签口径有误,正文已按 man 与探针双锚更正);小于 8 字节的 read/write 返回 EINVAL,大于 8 的 read 照收 8 |
| `t2_eventfd_epoll.*` | E2 通知次数 | 一次 write(5):默认+LT 醒 1 次;信号量+LT 连醒 5 次(读一次不清零,fd 仍可读,每轮 wait 都报);信号量+ET 醒 1 次(靠读到 EAGAIN 收尾)。信号量配 LT 是"读一次不等于读空"的忙通知档 |
| `t3_timerfd_ticks.*` | E3 timerfd 本体 | 一次性 200ms:到期 read=1,再读 EAGAIN;周期 100ms 睡过 350ms:一次 read=3(错过的档合并成一个数,不排队补报);改 interval 到 30ms 后下一档起按新周期(t+100 首档沿用了旧 it_value,130/160/190 间隔 30ms);it_value=0 撤销后 select 300ms 返回 0。fdinfo 里 clockid/it_value/it_interval/ticks 全程可见 |
| `t4_timerfd_precision.*` | E4 到期精度 | 1ms 档收 500 档:间隔中位 999.3µs,p99 1029µs,max 1054µs;10ms 档中位 10000.3µs,p99 10028.9/10078.5µs(两轮)。两轮复跑一致(Windows 侧同机 Sleep(5) 实睡 12.6ms,Linux 侧 1ms 档误差在 1% 上下,两世界的定时器精度差距本身就是素材) |
| `t5_one_loop.*` | E5 单循环综合 | pipe+eventfd+timerfd 同挂一个 epoll:150ms 管道来数据、250/500/750ms 定时器三响、401/651ms 门铃两次,全部由同一个 epoll_wait 分发,时间线交错互不打断 |

## E4 精度数据(两轮对照)

| 周期 | 中位 | p90 | p99 | max |
|---|---|---|---|---|
| 1ms(第一轮) | 999.3µs | 1010.3µs | 1029.0µs | 1054.3µs |
| 1ms(复跑) | 999.3µs | 1010.5µs | 1029.5µs | 1061.6µs |
| 10ms(第一轮) | 10000.3µs | 10018.1µs | 10028.9µs | 10042.4µs |
| 10ms(复跑) | 10000.3µs | 10019.9µs | 10078.5µs | 10215.0µs |

## 复现

```sh
for f in t1_eventfd_semantics t2_eventfd_epoll t3_timerfd_ticks t4_timerfd_precision t5_one_loop; do
  g++ -std=c++20 -O2 -Wall -Wextra -o $f $f.cpp
done

./t1_eventfd_semantics   # 秒级
./t2_eventfd_epoll       # 约 1 秒
./t3_timerfd_ticks       # 约 2 秒
./t4_timerfd_precision   # 约 4 秒
./t5_one_loop            # 约 1 秒
```

## 复跑注意

- t3 的 fdinfo 输出依赖内核版本,6.18 的字段有 clockid/ticks/settime flags/it_value/it_interval,更早内核可能少字段。
- t4 的精度数字是空闲进程的口径,同机跑重负载时 p99/max 会放宽。
- t5 里门铃两次按 write(1)+write(2) 发出,read 一次取走的是当时的累计值,演示的正是默认模式"读走即清零"。
