# E6 信号设施速查:全景一张表

`facility_probe.out` 是本机可用性探针:内核/glibc 版本 + 每个设施 → x86-64 系统调用编号 → 一次真实调用的返回值。表本身如下,给 ch04(事件循环)和 ch07 铺垫——信号设施到 signalfd/timerfd/pidfd 这里,全部汇入「一切皆 fd」的事件循环世界。

## 全景表(信号上下两篇覆盖过的全部设施)

| 设施 | 底层(x86-64 编号) | 角色 | 典型场景 | 篇目 |
|---|---|---|---|---|
| `signal(2)` | rt_sigaction(13) 的 glibc 包装(BSD 语义) | 兼容入口 | 老代码;新代码一律 sigaction | 上篇 E2 |
| `sigaction(2)` | rt_sigaction(13) | 装 handler 的正门 | 默认响应、改默认动作 | 上篇主线 |
| `sigqueue(2)` | rt_sigqueueinfo(129) | 带值发送(实时信号排队) | 父子带编号通信 | 本篇 E1 |
| `sigwaitinfo(2)` / `sigtimedwait(2)` | rt_sigtimedwait(128)/time64(421) | 同步点取信号,零 handler | 专职信号线程 | 本篇 E5 |
| `signalfd(2)` | signalfd4(289) | 信号变 fd | 事件循环统一调度(self-pipe 的内核原生版) | 本篇 E2/E4 |
| `timerfd_create(2)` | timerfd_create(283) | 定时器也变 fd | 与 signalfd 同班混挂 | 本篇 E2 |
| `pidfd_open(2)` | pidfd_open(434) | 进程句柄化 | poll 等退出、监督进程 | 本篇 E3 |
| `pidfd_send_signal(2)` | pidfd_send_signal(424) | 按句柄发信号 | pid 复用免疫的信号发送 | 本篇 E3 |
| `pidfd_getfd(2)` | pidfd_getfd(438) | 拿目标进程的 fd | 调试/注入,受 ptrace 权限(yama) | 本篇 E3 |
| `waitid(P_PIDFD)` | waitid(247) + idtype=P_PIDFD(3) | 句柄收尸 | poll(pidfd) 可读后拿退出状态 | 本篇 E3/E4 |

(编号均为本机 `<sys/syscall.h>` 实测打印,见 facility_probe.out。)

## 一句话分工

- **发送**:kill(裸)/ sigqueue(带值)/ pidfd_send_signal(按句柄)。
- **响应**:handler(异步,上篇)/ sigwaitinfo(同步点)/ signalfd(事件循环 fd)。
- **进程生命周期**:waitpid(pid 数字)/ SIGCHLD(信号)/ pidfd+waitid(句柄,事件循环友好)。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 facility_probe.cpp -o /tmp/e6 && /tmp/e6
```

单进程,亚毫秒;探针里的 fd 编号(3/4)取决于当时进程的 fd 表,数值会变。
