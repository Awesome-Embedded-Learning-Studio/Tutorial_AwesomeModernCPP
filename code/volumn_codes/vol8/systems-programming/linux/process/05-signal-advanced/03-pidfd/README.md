# E3 pidfd:进程句柄化

pidfd 是「把进程当 fd 用」的三个系统调用:`pidfd_open`(434)拿句柄、`pidfd_send_signal`(424)按句柄发信号、`pidfd_getfd`(438)拿对方的 fd;配上 `waitid(P_PIDFD)` 就是一条完整的收尸路。代码里 glibc 2.36+ 其实有 `<sys/pidfd.h>` 封装,实验走裸 syscall 是为了把三个编号摆在明面上(E6 表)。

## 结论(对照 pidfd_lab.out)

1. **第三种收尸**:[a] `pidfd_open` 子进程 → 活着时 `poll(pidfd,50ms)`=0;退出后 POLLIN(t≈300ms)→ `waitid(P_PIDFD, WEXITED)` 拿到退出码 42——不用 SIGCHLD、不用 waitpid 轮询。收尸后再 poll 得 `POLLIN|POLLHUP`(0x11,人已注销、句柄挂断);二次 waitid → ECHILD(尸已收过,waitpid 二次收也是这个错)。
2. **pidfd_send_signal**:[b] 按句柄发 SIGRTMIN+1 带 `sival_int=777`,收方 sigwaitinfo 原样到账。**坑:info 非空时内核不代填 `si_pid`**,要显示发送方得自己写(实验里实测:填了 getpid() 收方才看得到,不填就是 0)。
3. **pidfd_getfd**:[c] 父进程凭句柄拿到子进程 fd 表里的文件 fd,`pread` 直接读出子进程写的内容(「PIDFD-GETFD-MARKER-31415926」);兄弟进程之间同样操作 → **EPERM**——本机 `yama/ptrace_scope=1`,只许对后代用,同级不行。跨 uid 则要 CAP_SYS_PTRACE(未测,引 man 2 pidfd_getfd)。
4. **pid 复用竞态(真实复现)**:[d] `kill(2)` 拿数字找人,数字会被内核回收再分配;pidfd 拿的是对 `struct pid` 的引用,死就是死。复现手法:新 user+pid namespace 里把 `pid_max` 调到 302(合法最小 301),烧掉低号段后让 A=301 退出收尸,第 2 个候选 B 就复得 301——`kill(301)` 返回 0 **误伤无辜的 B**,`pidfd_send_signal(老句柄)` 返回 **ESRCH**。这就是 pidfd 的立身之本。

### 复现手法的前置知识(为什么这么绕)

- 根命名空间 `pid_max=4194304`,pid 单调递增(实测 5000 fork span=5035),复用一轮要 20 分钟级别——所以借 namespace。
- 新 pidns 里分配器从 2 往上爬;游标一旦越过 `RESERVED_PIDS`(300),下限就固定在 300,**低号段 2..299 从此不再分配**(实测:294/295/296 轮拿到 297/298/299,随后永远 300)。所以要先烧掉低号段,让 A 落进会循环的 [300,301] 段。
- 需要未特权 user namespace(本机 `max_user_namespaces`=216843,可用);不可用的环境 [d] 会打「本项跳过」。

## 三种收尸路径对比

| 路径 | 依据 | 何时知道退出 | 拿退出状态 | 典型场景 |
|---|---|---|---|---|
| `waitpid(pid,...)` | pid 数字 | 阻塞等/轮询 | 返回值+status 宏 | 顺序脚本、父进程管少数孩子 |
| SIGCHLD handler | 信号(可能合流,上篇 E6) | 异步提醒 | 循环 `waitpid(WNOHANG)` | 传统服务、shell |
| pidfd | 句柄(数字复用免疫) | `poll/epoll` 可读 | `waitid(P_PIDFD)` | 事件循环统一调度(E4 拿它收 worker) |

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 pidfd_lab.cpp -o /tmp/e3 && /tmp/e3
```

marker.txt 是 pidfd_getfd 的靶子文件,已随目录归档一份;源码里的路径烧死在 `~/lp05_scratch/e3/`(沿用本卷先例),缺了程序会自建同内容文件。约 0.5s 跑完。
