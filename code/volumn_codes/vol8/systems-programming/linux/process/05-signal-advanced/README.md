# 05-signal-advanced 配套实验

《信号(下):实时信号、signalfd 与 pidfd》(vol8 systems-programming/linux/process 第 05 篇,文章撰写中)的实验代码与原始输出存档。主线一句话:**信号处理的三条出路**——实时信号排队(带数据)、信号化为 fd(signalfd)、进程句柄化(pidfd);招牌是 E3 的「pidfd 收尸」与 E4 的「signalfd 事件循环优雅关闭」。

## 与上篇分工(防重叠)

| 话题 | 归属 | 本篇动作 |
|---|---|---|
| sigaction/handler 约束/异步信号安全 | `process/04-signal-basic`(上篇) | 引用,不重讲 |
| SIGCHLD reap、信号合流 | 上篇 E6 | E3 对比表里带一句,不重做 |
| self-pipe trick | 上篇 E5 | E2 的 signalfd 是它的内核原生版,对照衔接 |
| waitpid 本身(阻塞/WNOHANG/状态宏) | Lproc01(规划中) | E3 只讲 pidfd 这条新路 |
| 实时信号/signalfd/sigwaitinfo/pidfd | 本篇 | 主体 |

## 环境(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 内核 | 6.18.33.2-microsoft-standard-WSL2(WSL2) |
| CPU | AMD Ryzen 7 9700X 8-Core Processor |
| g++ | 16.2.1 20260810(GCC),`-std=c++20 -Wall -Wextra -O2`(全部实验,零警告) |
| glibc | 2.44 |
| yama | `/proc/sys/kernel/yama/ptrace_scope` = 1(影响 E3 的 pidfd_getfd 权限结论) |
| pid_max | 4194304(E3 的复用复现因此要借 pid namespace,见该目录 README) |
| scratch | `~/lp05_scratch/eN/`(仅 E3 用到 marker 文件,程序缺了会自建) |

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-rt-signal/` | E1 | SIGRTMIN=34/SIGRTMAX=64(glibc 占 32/33);实时信号排队不丢(3 发 3 到,同号 FIFO)、标准信号 3 发只记 1 次;**出队顺序=小号优先,但空 sa_mask 一次放行一批时 handler 执行顺序倒挂(大号先跑)——帧叠加,不是「内核挑大号」** |
| `02-signalfd/` | E2 | 先阻塞再创建,不阻塞的信号 signalfd 永远看不到(实测漏信号);一次 read 可拿多条 signalfd_siginfo;读走即消费,handler 不再触发(两条出路互斥);与 timerfd 混挂一个 poll——self-pipe 的内核原生版 |
| `03-pidfd/` | E3 | poll(pidfd) 可读=进程退出,waitid(P_PIDFD) 拿退出状态——不用 SIGCHLD 不用 waitpid 的第三种收尸;pidfd_send_signal 按句柄发信号;**pid 复用竞态真实复现:kill(老数字) 误伤新进程,pidfd_send_signal(老句柄) ESRCH** |
| `04-graceful-shutdown/` | E4 | mini prefork echo 服务器:SIGTERM 从 signalfd 进事件循环,状态机 RUNNING→DRAINING(close listen→drain)→REAPING(poll pidfd+waitid)→EXIT 完整时序;朴素版对照:SA_RESTART 下旗子立了没人看(关停被拖 300ms 还多接一条连接) |
| `05-sigwaitinfo/` | E5 | 不开 handler,在指定点同步取信号(sigtimedwait 超时 200ms 实测);与 signalfd 是同一 pending 队列的两个消费者,谁读谁消费、双向验证 |
| `06-facility-map/` | E6 | 本系列信号设施全景:每个设施 → x86-64 系统调用编号 → 本机一次真实调用(表在该目录 README) |

## 复现

```sh
# 各目录下(编译命令与 .cpp 头注释一致)
g++ -std=c++20 -Wall -Wextra -O2 rt_signal.cpp -o /tmp/e1 && /tmp/e1
```

E3 需要 unprivileged user namespace 可用(本机 `/proc/sys/user/max_user_namespaces`=216843);E4 两个程序各跑约 1s。

## 输出档案口径

全部 `.out` 是同一轮(2026-10-04)的原始 stdout。多进程实验统一 `setvbuf(stdout, nullptr, _IONBF, 0)`,所以父子输出按真实时序交错。pid、端口号、毫秒时间戳每次复跑都会变,规律不在具体数值;E2[d]/E4 的时序结构(相对次序与档位)是稳定结论。
