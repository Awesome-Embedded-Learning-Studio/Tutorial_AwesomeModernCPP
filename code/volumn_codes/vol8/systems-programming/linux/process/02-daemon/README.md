# 02-daemon 配套实验

《守护进程、会话与环境》(vol8 systems-programming/linux/process L02)的实验代码与原始输出存档。核心问题一句话:**进程的命本来拴在会话、进程组、控制终端、shell 和环境变量上,守护进程化的每一步都是在剪其中一根线,而 rlimit 决定它最多能长多大**。E1-E6 分别给出归属关系、经典守护化步骤、终端恩怨、资源上限、环境变量、/proc 观察位的实测证据。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -Wpedantic -O2` |
| **PID 1** | **`/proc/1/comm` = `systemd`**(本机 `/etc/wsl.conf` 写了 `[boot] systemd=true`)。WSL2 默认是无 systemd 的 `/sbin/init` 兜底,本机不是——文章若写"WSL2 下 /proc/1 是 init",以本条为准修正 |
| 孤儿收养者 | pid 249,`/init`(WSL 每个登录会话的 Relay 进程,是 subreaper)——所以本机孤儿 getppid() 是 249 不是 1,`systemd --user` 不在这条链上 |
| ulimit -n | 1048576(软=硬) |
| 工具 | strace 7.2、util-linux script、procps ps、openpty(util) 全部可用 |
| 运行台 | 实验在无 tty 的管道环境跑;要看 tty 关联的实验用 `script -qec '...' /dev/null` 借一个 pty |

**E2/E2b/E3_tty 的 .out 是程序写进专用 trace 文件的原始输出**(stdout 要么即将被重定向进 /dev/null、要么被 dup 到 pty slave 上,不能再当输出通道);其余 .out 是原始 stdout。每个 .cpp 头部注释写了精确复现命令。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-session-pgrp/` | E1 会话与进程组 | setpgid(0,0) 只改 PGID 一列、SID 不动;setsid 让 PGID+SID 一起变成自己的 pid;组长或会话长再调 setsid 都报 EPERM(errno=1);ps 四列与 getsid/getpgid API 读数逐项吻合 |
| `01-session-pgrp/` | E1b 进程组广播 | kill(-pgid, SIGHUP) 一次投递全组:组长+两个组员在同一毫秒(t=+101ms)各收到一条,组外父进程零接收 |
| `02-daemonize/` | E2 经典守护化 | 六步全链落在 trace 里:sid/pgid/tty 变化链见下表;关键细节——setsid 后 fd0 的 readlink 仍指旧 tty(描述符不会自动断,必须显式重定向) |
| `02-daemonize/` | E2b daemon(0,0) | libc daemon() 一步到位但只 fork 一次:返回后 pid==pgid==sid 仍是会话长,与手工双 fork 版(sid≠pid)一对照,第二次 fork 的意义立刻显形 |
| `03-terminal/` | E3 tty 挂断 | close(master) 模拟终端死亡:会话长被 SIGHUP 缺省动作处决(waitpid 证实 "Hangup"),前台组两个组员同毫秒(t=+151ms)捕获到 SIGHUP |
| `03-terminal/` | E3 nohup 拆解 | strace 全过程只有四件事:stdin→/dev/null、stdout→nohup.out、stderr 跟随、rt_sigaction(SIGHUP, SIG_IGN),然后 exec;**一个 setsid/setpgid 都没有**——nohup 不搬家,只穿防弹衣。disown/setsid 对照:普通后台作业留在 shell 会话里,setsid 的作业 SID 等于自己 pid |
| `04-rlimit/` | E4a NOFILE | 软限压到 4:open 第 1 次拿 fd=3,第 2 次起 errno=24(EMFILE);软限可逆(升回硬限成功) |
| `04-rlimit/` | E4b CPU | 软限 1s/硬限 2s 满载循环:wall=+999ms cpu=+998ms 收到第 1 次 SIGXCPU(可捕获的警告);wall≈+2000ms 被 SIGKILL 处决,退出码 137 |
| `04-rlimit/` | E4c STACK | rlim_cur=8388608 字节(8 MiB),maps 里 [stack] 当时只映射 136 KiB——软限是"允许长到的上限"不是"已用" |
| `04-rlimit/` | E4d 硬限边界 | 软限升到硬限:成功;硬限 +1:EPERM;硬限降到 1024:成功;再升回原值:EPERM——硬限是单向阀门,root(CAP_SYS_RESOURCE)才能升 |
| `05-environ/` | E5a 环境家族 | environ 数组与环境字符串都在 [stack] 映射区内(argv[0] 与 environ[0] 相距 13 字节);putenv 不拷贝——只改缓冲区内容 getenv 就跟着变;第一次 setenv 后 libc 把数组搬去堆(地址实测从 0x7fff… 变到 0x63a8…),栈上只剩字符串本体 |
| `05-environ/` | E5b exec 两条路 | execle 带 2 条白名单 envp → 子进程环境只剩这 2 条(HOME/TERM 全没);execve(…, environ) → 70 条全量在场且带着刚 setenv 的 E5_EXTRA |
| `06-procfs/` | E6 /proc 速览 | 同一个二进制被抓到 R/S/Z 三态;cmdline 就是 exec 时 argv 逐项 NUL 拼接(22 字节 = `./e6_proc\0hello\0world\0`);僵尸的 cmdline 为空(地址空间已释放);waitpid 后 /proc/pid 目录直接消失 |

## E2 的 sid/pgid/tty 变化链(重头戏,原始数据)

| 阶段 | pid | ppid | pgid | sid | fd0 指向 | ioctl(fd0,TIOCGSID) |
|---|---|---|---|---|---|---|
| 0 初始(script 的 pty 里) | 13247 | 13246 | 13246 | 13246 | /dev/pts/10 | 成功,sid=13246 |
| 1 fork#1 后(子) | 13248 | 13247 | 13246 | 13246 | /dev/pts/10 | 成功(继承) |
| 2 setsid 后 | 13248 | 13247→249 | **13248** | **13248** | /dev/pts/10(没断!) | **ENOTTY(归属切断)** |
| 3 fork#2 后(孙) | 13249 | 249 | 13248 | 13248 | /dev/pts/10 | ENOTTY |
| 6 重定向后 | 13249 | 249 | 13248 | 13248 | **/dev/null** | ENOTTY |

三条判据各管一事:readlink 看"描述符开着指向哪",TIOCGSID 看"这 tty 认不认我这个会话",sid/pgid 与 pid 的相等关系看"身份"。最终态 sid≠pid 且 pgid≠pid 且无控制终端——教科书断言逐项对上。对照 E2b:daemon() 返回后 pid==pgid==sid==13367,只差第二次 fork。

## 复现

```sh
# 单发编译(在本目录下)
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 01-session-pgrp/e1_sessions      01-session-pgrp/e1_sessions.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 01-session-pgrp/e1_pgrp_sighup   01-session-pgrp/e1_pgrp_sighup.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 02-daemonize/e2_daemon_steps     02-daemonize/e2_daemon_steps.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 02-daemonize/e2b_daemon_libc     02-daemonize/e2b_daemon_libc.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 03-terminal/e3_tty_hangup        03-terminal/e3_tty_hangup.cpp -lutil
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 04-rlimit/e4_nofile              04-rlimit/e4_nofile.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 04-rlimit/e4_cpu                 04-rlimit/e4_cpu.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 04-rlimit/e4_stack               04-rlimit/e4_stack.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 04-rlimit/e4_hardlimit           04-rlimit/e4_hardlimit.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 05-environ/e5_environ            05-environ/e5_environ.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 05-environ/e5_exec               05-environ/e5_exec.cpp
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -o 06-procfs/e6_proc                06-procfs/e6_proc.cpp

# 运行(E2/E2b 要套 script 借 pty;E3 nohup 的两个 .out 同理)
01-session-pgrp/e1_sessions > 01-session-pgrp/e1_sessions.out 2>&1
01-session-pgrp/e1_pgrp_sighup > 01-session-pgrp/e1_pgrp_sighup.out 2>&1
script -qec './02-daemonize/e2_daemon_steps 02-daemonize/e2_daemon_steps.out; sleep 1' /dev/null
script -qec './02-daemonize/e2b_daemon_libc 02-daemonize/e2b_daemon_libc.out; sleep 1' /dev/null
./03-terminal/e3_tty_hangup 03-terminal/e3_tty_hangup.out
./04-rlimit/e4_nofile > 04-rlimit/e4_nofile.out 2>&1
04-rlimit/e4_cpu_run.sh > 04-rlimit/e4_cpu.out 2>&1        # 驱动脚本补记 SIGKILL 退出码
./04-rlimit/e4_stack > 04-rlimit/e4_stack.out 2>&1
./04-rlimit/e4_hardlimit > 04-rlimit/e4_hardlimit.out 2>&1
./05-environ/e5_environ > 05-environ/e5_environ.out 2>&1
./05-environ/e5_exec > 05-environ/e5_exec.out 2>&1
./06-procfs/e6_proc hello world > 06-procfs/e6_proc.out 2>&1

# nohup 拆解(stdout 得是真 tty,nohup 才会改道 nohup.out,所以套 script)
script -qec 'strace -o e3_nohup_strace.out -e trace=execve,rt_sigaction,openat,dup2,dup,close,setsid,setpgid nohup true' /dev/null

# 或整套 CMake(注意 e3 需要 util 库,CMakeLists 已链)
cmake -S . -B build && cmake --build build
```

## 复跑注意(踩过的坑,都在 .out 和源码注释里)

- **stdio 缓冲 + fork + _exit 会无声丢输出**:e1 第一版子进程的 printf 全没了——重定向到文件后 stdout 全缓冲,fork 把缓冲复制了一份,子进程 `_exit` 不刷缓冲,那几行就地蒸发。与 L03 page-cache README 记的是同一条,这里用 `setvbuf(stdout, nullptr, _IONBF, 0)` 在 fork 前关掉缓冲。信号处理器里则一律只用 `write` + 手工排版(async-signal-safe)。
- **实验台没有 tty**:无包装直跑时 fd0 是管道,TIOCGSID 永远 ENOTTY,看不到"有过控制终端"的前状态。E2/E2b/E3_nohup 都用 `script -qec '...' /dev/null` 借 pty;`.out` 里 fd0 才会指向 /dev/pts/N。
- **script 拆 pty 会向前台组发 SIGHUP——E3 讲的机制反咬了实验自己**:E2 的外层 script 在命令退出时就拆终端,若父进程先退而子进程还没 setsid,守护进程半成品会被 SIGHUP 误杀。所以 e2_daemon_steps 里父进程用管道等"子已 setsid"的通知才退(源码注释有说明);真实 shell 场景父进程 fork 完立刻退,shell 活着,不存在这个窗口。e2b 的 daemon() 内部 fork 后父立刻退,只能靠 `-c` 命令尾部加 `sleep 1` 兜底。
- **孤儿收养者不是 1**:本机 E2 的孙进程 getppid()=249——WSL 每个会话的 `/init`(Relay)是 subreaper。文章写"reparent 到 init(1)"时要带这句口径,教科书结论在 WSL2 登录会话里要打折。
- **SIGXCPU 只命中一次**:软 1s/硬 2s 的口径里,超软限后第 2 次到期正好和硬限的 SIGKILL 同点,看不到 man 7 signal 说的"每秒再发";想看重复把硬限拉大到软限 +3s 以上。
- **rlimit 的单位是字节**:e4_stack 第一版把 8388608 字节标成"KiB"(算出 8192 MiB),已修——RLIMIT_STACK 的 8 MiB 就是 8388608,不带单位换算。
- **strace 的 `-e trace=` 名单里写错一个 syscall 名(如 setpgist)整个 strace 直接失败**,而且输出重定向时错误被吞,表象是"没有输出文件"。排错过一次。
- **E1b/E3 的就绪握手必须做信号屏蔽**:组员"报就绪"和"开始等"之间有窗口,signal 若在窗口里到达,pause 会睡过站。先用 sigprocmask 挡住、报完就绪再 sigsuspend 原子放行。
- **e4_cpu 的满载循环别被优化掉**:累加变量要 volatile,否则 -O2 直接把循环删了;C++20 里 volatile 禁止 `++`,要写 `hits = hits + 1`。
