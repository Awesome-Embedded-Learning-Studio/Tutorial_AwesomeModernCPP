# 03-timer-family 配套实验

《定时器全景:alarm→setitimer→timer_create→timerfd》(vol8 systems-programming/linux ch05 L03)的实验代码与原始输出存档。核心问题一句话:**从 alarm 到 timerfd 四代定时器各自的表达力与语义边界,以及周期任务为什么会漂、怎么不漂**。

与相邻篇卷的分工:
- timerfd 本体(epoll 配合、合并计数、改期、1ms 档精度 999.3µs)在 ch04 L02 已正面讲过,本篇把 timerfd 当「定时器家族的一个成员」选型对照,E6 只量漂移语义、不重测到点精度。
- 篇 1(L01)的时钟选型结论(量耗时用 CLOCK_MONOTONIC)在本篇全部沿用。
- Windows 侧对照:Sleep(5) 实睡 12.6ms(windows/memory/02-shared-mem,默认 15.6ms 一档的定时器分辨率)、SetWaitableTimer 的完成例程走 APC(windows/process/02 与 async-io/01 的 APC 链)——正文以「另一侧怎么看」引用,不重测。
- 信号处理的机制(sigaction/sigwaitinfo/signal mask)归 ch03 进程与信号两篇,本篇只用到结论。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -O2 -Wall -Wextra`(t2-t5 另加 `-pthread`,t3 还加 `-D_GNU_SOURCE`;t1/t6 与两个补测不带) |
| 计时 | CLOCK_MONOTONIC |

`.out` 出自 2026-10-04 的同一轮;t6 的漂移数字另跑了一轮收在 `t6_rerun.out`(相对睡眠 +78.0ms 对第一轮 +78.4ms,形状一致)。信号计数、档数每次稳定,微秒级时刻会变。t1b 与 t5b 是修订轮的补测(同一台机器、同一天):t1b 给 alarm 返回值的取整规则补判别点,t5b 单独量 exec 那一跳的存亡。

## 目录与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `t1_alarm.*` | E1 alarm | alarm(2) 从下达到 SIGALRM 2000.018ms(粒度只有整秒,表达不了亚秒);alarm(1) 过 300ms 后再 alarm(5) 返回剩余 1(内核按整秒向上报);alarm(0) 撤销生效;每进程一笔,新约顶旧约 |
| `t1b_alarm_rounding.*` | E1 补测(修订轮) | alarm 返回值的取整判别:剩约 1.70s 报 2(截断会给 1)、剩约 0.40s 也报 1(四舍五入会给 0)、已到点报 0——向上取整成立,不是四舍五入 |
| `t2_setitimer_trio.*` | E2 setitimer 三兄弟 | 同一负载(2s 墙钟/1s CPU):ITIMER_REAL 响 200 次(数墙钟),ITIMER_VIRTUAL 响 101 次(只数用户态),ITIMER_PROF 响 96 次(用户+系统)——量级都对得上 1000ms CPU/10ms 档,二者之差在 10ms 档的采样噪声内;it_interval=0 响一次自停;重设即撤销 |
| `t3_timer_create.*` | E3 四种通知形态 | SIGEV_SIGNAL 走 SA_SIGINFO,si_code=SI_TIMER、si_value 载荷原样到达;SIGEV_NONE 只倒计时,it_value 自己查、到期归零无信号;SIGEV_THREAD 由 glibc 派辅助线程跑回调(两次回调 tid 不同——每次通知一个新线程);SIGEV_THREAD_ID 定向投给目标线程,主线程屏蔽同一信号零打扰 |
| `t4_overrun.*` | E4 到期合并 | 10ms 一档屏蔽信号睡 58ms:sigwaitinfo 只收到 1 个信号,timer_getoverrun=4——5 档到期=1 送出+4 合并;信号形态不排队补送,要补全选 timerfd(read 值是累计档数) |
| `t5_fork_exec.*` | E5 存亡矩阵 | alarm 和 setitimer(ITIMER_REAL)共用同一个内核计时器:alarm(2) 把 100ms 周期抹成 2s 一次性(getitimer 可见);fork 后子进程 alarm(0) 返回 0、600ms 内 SIGALRM/SIGUSR1 双零=三件信号形态全不继承;timerfd 活过 fork 也活过 exec(flags=0 无 CLOEXEC),exec 子进程读到 12 档(武装起两个 600ms 窗口的累计) |
| `t5b_exec_preserve.*` | E5 补测(修订轮) | alarm 与 setitimer 跨 exec 都保留(alarm(2)/getitimer(2) NOTES 的 preserved across execve):fork 之后的子进程里武装再 exec,exec 后 getitimer 读到剩约 1.999s、alarm(0) 返回旧剩余 2;不撤的 exec 子进程 2 秒整死于 SIGALRM(WIFSIGNALED=1 WTERMSIG=14)——计时器活着跨过了 exec,复位的是处理器 |
| `t6_scheduler_drift.*` | E6 漂移对照(招牌) | 1000×1ms 周期:相对睡眠循环总长 1078.44ms(漂 +78.444ms,每档开销滚进下一档);绝对到期补偿 1000.08ms(漂 +0.077ms,晚醒不滚存,min 902.9µs 是补拍);timerfd 1000.03ms(漂 +0.032ms,min 562.1µs 补拍更明显,ticks 恰 1000)。复跑 +78.0/+0.075/+0.029,同形 |

## E6 漂移对照(核心数据,两轮)

| 写法 | 第一轮漂移 | 复跑漂移 | 档间隔中位 |
|---|---|---|---|
| A: 相对 sleep(1ms) 循环 | +78.444ms | +78.028ms | 1078.3µs |
| B: 绝对到期(next+=1ms) | +0.077ms | +0.075ms | 1000.0µs |
| C: timerfd 1ms interval | +0.032ms | +0.029ms | 999.8µs |

## 复现

```sh
g++ -std=c++20 -O2 -Wall -Wextra -o t1_alarm t1_alarm.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t1b_alarm_rounding t1b_alarm_rounding.cpp
g++ -std=c++20 -O2 -Wall -Wextra -pthread -o t2_setitimer_trio t2_setitimer_trio.cpp
g++ -std=c++20 -O2 -Wall -Wextra -pthread -D_GNU_SOURCE -o t3_timer_create t3_timer_create.cpp
g++ -std=c++20 -O2 -Wall -Wextra -pthread -o t4_overrun t4_overrun.cpp
g++ -std=c++20 -O2 -Wall -Wextra -pthread -o t5_fork_exec t5_fork_exec.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t5b_exec_preserve t5b_exec_preserve.cpp
g++ -std=c++20 -O2 -Wall -Wextra -o t6_scheduler_drift t6_scheduler_drift.cpp

./t1_alarm              # 约 4 秒
./t1b_alarm_rounding    # 约 4 秒
./t2_setitimer_trio     # 约 7 秒
./t3_timer_create       # 约 3 秒
./t4_overrun            # 秒级
./t5_fork_exec          # 约 2 秒
./t5b_exec_preserve     # 约 4 秒(探针 C 等 2s 到点)
./t6_scheduler_drift    # 约 4 秒
```
