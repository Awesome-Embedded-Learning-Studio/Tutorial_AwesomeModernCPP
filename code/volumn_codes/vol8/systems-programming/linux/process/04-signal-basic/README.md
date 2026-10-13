# 04-signal-basic 配套实验

《信号(上):sigaction 与异步信号安全》(`documents/vol8-domains/systems-programming/linux/process/04-signal-basic.md`,ch03 第 04 篇,规划见 `todo/017-vol8-domains.md`,文章已成文)的实验代码与原始输出存档。七组实验对应文章主线:投递模型、sigaction 全家福、异步信号安全(重头戏)、可重入性、handler 工程模式、SIGCHLD 收尸、常用信号速览。

## 与前章分工(防重叠)

| 话题 | 归属 | 本篇动作 |
|---|---|---|
| EINTR / SA_RESTART 的 strace 证据链 | `thinking/02-error-paradigm/02-eintr-retry`(已入册) | 引用,不重做 |
| SIGSEGV handler 精确报错 + sigsetjmp/siglongjmp 恢复 | `memory/02-vm-apis/04-guarded-buffer`(已入册,E5 模式三) | 引用 |
| waitpid 本身(阻塞/WNOHANG/状态宏) | Lproc01 进程创建与生命周期(已成文) | E6 只讲信号驱动的收尸 |
| 信号机制本身:投递模型、handler 约束、可重入性 | 本篇 | 主体 |

## 环境

| 项 | 值 |
|---|---|
| 内核 | 6.18.33.2-microsoft-standard-WSL2(WSL2) |
| CPU | AMD Ryzen 7 9700X 8-Core Processor |
| g++ | 16.2.1 20260810(GCC) |
| glibc | 2.44 |
| 编译 | `-std=c++20 -Wall -Wextra -O2`(全部实验,-O2 是 E4 反例的必要条件,不是偏好) |

## 目录与实验对照

| 目录 | 实验 | 内容 |
|---|---|---|
| `01-delivery-model/` | E1 | kill/raise/子进程 kill 三条发送路径、handler 在指令边界插入、阻塞期连发只记 1 次、sigpending 位图 |
| `02-sigaction-family/` | E2 | SA_SIGINFO 的 si_pid/si_uid/si_code、sa_mask 屏蔽实测、SA_NODEFER 嵌套、SA_RESETHAND 一次性、signal() 的 glibc BSD 语义读回、sigprocmask 三件套 |
| `03-async-safety/` | E3 | handler 里 printf 的交错损坏实测 vs write(2)+标志的安全版,机制落点 |
| `04-reentrancy/` | E4 | 缺 volatile 的忙等反例(-O2 提出加载,timeout 124)+ 汇编旁证、strtok/localtime 静态状态机制模拟 |
| `05-handler-patterns/` | E5 | 模式一 flag+主循环优雅关停、模式二 self-pipe trick 完整事件循环(模式三 siglongjmp 引 Lmem02) |
| `06-sigchld-reap/` | E6 | 3 退出合流成 1 个 SIGCHLD、循环 waitpid vs 单次(僵尸对照)、SA_NOCLDSTOP、SA_NOCLDWAIT |
| `07-signal-facts/` | E7 | SIGKILL/SIGSTOP 不可捕获不可忽略、SIGTERM 可忽略 SIGKILL 不可、SIGPIPE 默认杀 vs 忽略后 EPIPE、kill -l 快照、速览表 |

## 每实验结论(摘录原始输出)

### E1:标准信号不排队,pending 只有一位

三条发送路径(handler 计数均为 1):`kill(getpid(), SIGUSR1)`、`raise(SIGUSR1)`(glibc 走 tgkill)、子进程 `kill(getppid(), SIGUSR1)`。

阻塞期实测(SIG_BLOCK 后连发 3 次):

```
[E1.3] after 3 sends:         pending: SIGUSR1=1 SIGUSR2=0 SIGALRM=0
[E1.3] handler count while blocked = 0 (never ran)
    [H] SIGUSR1 handler entered
[E1.3] after unblock: handler count = 1  <- 3 sends, ONE delivery
[E1.3] control run: 1 send while blocked -> handler count = 1 (same as 3 sends)
```

两个可引的细节:①`[H]` 行出现在"handler count while blocked = 0"那行 printf 之后——投递发生在解阻塞的那次 `sigprocmask` 返回处,不早不晚。②[E1.2] 主循环与 handler 都用 `write(2)`,handler 行永远落在主循环两行之间,从不把一行劈成两半——信号在指令边界(系统调用返回点)被受理,不发生在 write 系统调用内部。

### E2:sigaction 全家福

**SA_SIGINFO 能查到发送者**(sigaction_family.out):

```
    [H] caught 10: si_signo=10 si_code=-6(SI_TKILL  raise/tgkill发来) si_pid=15082 si_uid=1000
    [H] caught 10: si_signo=10 si_code=0(SI_USER   kill()发来) si_pid=15083 si_uid=1000
[E2.1] child pid=15083 sent it -> si_pid must equal this
    [H] caught 14: si_signo=14 si_code=128(SI_KERNEL 内核产生) si_pid=0 si_uid=0
```

三种 si_code:raise 出来是 SI_TKILL 且 si_pid=自己,别的进程 kill 出来是 SI_USER 且 si_pid=对方 pid(与下一行打印的 child pid 精确相等),内核 itimer 出来的 SIGALRM 是 SI_KERNEL 且 si_pid=0(没有发送者)。SI_QUEUE(sigqueue)属实时信号,留给《信号(下)》。

**sa_mask + 自身自动屏蔽**(handler 内读屏蔽字与 pending):

```
    [H] mask inside handler: SIGUSR2=1 SIGUSR1=1 (self + sa_mask both blocked)
    [H] raise(SIGUSR1) inside handler -> pending=1 (NOT delivered while we run)
    [H] SIGUSR2 handler returning
    [H] SIGUSR1 handler runs only AFTER SIGUSR2 handler returned
```

**SA_NODEFER 同信号嵌套**:`depth=1 → depth=2 → leave → leave`(handler 里 raise 自己立刻递归进入,而非 pending)。**SA_RESETHAND 一次性**:第二次 raise 后 `WIFSIGNALED=1 WTERMSIG=10`——处置已被重置回 SIG_DFL,SIGUSR1 默认动作杀死进程。

**signal() 的 glibc 语义**(读回 + 持久性):

```
[E2.5] readback sa_flags=0x14000000: SA_RESTART=1 SA_NODEFER=0 SA_RESETHAND=0
[E2.5] raised twice -> handler count=2; handler PERSISTS (System V semantics would reset after 1st and die on 2nd)
```

glibc 的 `signal()` 是 `sigaction` 的薄封装:装的是 BSD 语义(带 SA_RESTART,handler 不自重置)。System V 语义(一次性 handler)在这台机器上不复存在,只有 SA_RESETHAND 能显式要出来。0x14000000 = SA_RESTART(0x10000000) | SA_RESTORER(0x04000000,后者是 glibc 内部位,给 rt_sigreturn 蹦床用,man 2 sigaction 的 flags 清单里有它,标注内部使用、不供应用调用)。

**sigprocmask 三件套**(实测序列):`BLOCK{USR2}` → USR2=1。`BLOCK{USR1,USR2}` → 两位都 1(并集)。`UNBLOCK{USR2}` → 只剩 USR1(差集,不会替你清别的)。`SETMASK{}` → 全清(整体替换)。SA_RESTART 的重启语义证据链在 thinking/02 的 eintr_strace_*.txt,本篇引用。

### E3:handler 里 printf 翻车实录(重头戏)

两版同源:itimer 每 80µs 一次 SIGALRM,主循环 30000 行 `printf("M %06d abcdef...")`。差别只在 handler 与缓冲策略。

| | unsafe_printf(handler 里 printf,stdout 重定向=全缓冲) | safe_write(handler 里 write(2)+标志,stdout 无缓冲) |
|---|---|---|
| 总行数 | 30585(超出 30000+23 达 562 行) | 30254 = 30000 + 253 tick + 1 行 DONE,**精确一致** |
| 损坏帧 | 21 帧(索引见 unsafe_printf_corrupt_frames.txt) | 0 |
| DONE 行 | `M 029988 abcDONE ticks=23`——粘在主循环行中间 | 独立一行 `DONE ticks=253` |

损坏不是一种,是一族(摘自 unsafe_printf_corrupt_frames.txt):

```
M 009[H  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ      <- 主循环行号打了一半被插入
[H tick 2]M 002530 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ  <- 换行丢失,两行粘连
[H tick  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ     <- handler 自己的数字被抹掉
M 003949 abcdefghijklmnopqrstunopqrstuvwxyz0123456789ABCDEFGHIJ   <- 主循环自己的行少了字符,无 handler 痕迹
defghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ                 <- 孤儿碎片(一行的后半截)
```

最狠一帧是 `M 009[H  `(unsafe_printf_excerpt2.out):主循环的行号字段拷到 "009" 被打断,handler 的 "[H " 接上来又被主循环恢复的拷贝覆盖,双方的输出各剩半截。更值得写进正文的是**没有 handler 文字的损坏帧**(如 `M 003949 ...opqrstu|nop...`):这不是"两个 printf 抢着写",是缓冲指针/计数器半更新被冻结后,恢复执行的那次 printf 按错误位置继续拷——printf 不安全的病根是共享状态,交错只是症状之一。

**为什么 write 安全**:它是一次系统调用,锁、缓冲、位置计数全在内核侧,用户态没有能被打断的半更新状态。man 7 signal-safety 的表述:异步信号安全函数"要么可重入,要么对信号而言是原子的"。write 属于后者,printf 属于"维护静态分配的缓冲与计数器/索引"的前者反例(本机未装 man7 页,引 man7.org 在线版)。

两个采证事实:①glibc 的 stdio 锁是递归锁,同线程重入不死锁——教科书式的"handler 里 printf 卡死"在这台 glibc 2.44 上不出现,出现的是上面的状态损坏。②两次运行 tick 数不同(unsafe 23 / safe 253)是缓冲策略决定主循环快慢的真实差异,引用数字时对准 run(损坏帧索引与三个 excerpt 来自 ticks=23 的那次,safe 两份来自 ticks=253 的那次)。

### E4:可重入性

**volatile sig_atomic_t 反例**(flag_spin.cpp,同一份源码两种编译):

| 编译 | 结果 |
|---|---|
| `-O2`(缺 volatile) | `timeout 3` 杀进程,**exit 124**。输出永远停在 `busy-waiting for g_flag...`——handler 跑了,内存也写了,循环看不见 |
| `-O2 -DUSE_VOLATILE` | `observed g_flag=1 after 538301099 spins -> clean exit` |

汇编旁证(flag_spin_plain_O2_loop.asm,加载被提出循环,整个忙等只剩一条跳转):

```asm
	movl	g_flag(%rip), %eax      # 循环外加载一次
	testl	%eax, %eax
	jne	.L4
.L5:
	jmp	.L5                     # 死循环本体,再也不会读 g_flag
```

volatile 版(flag_spin_volatile_O2_loop.asm)的 `.L5` 内每圈 `movl g_flag(%rip), %eax`。结论:`sig_atomic_t` 保证"读写不可分割",`volatile` 保证"每次真的去读",两者缺一不可,-O2 下缺 volatile 必翻。

**strtok / localtime 静态状态**(static_state.cpp,单线程确定性模拟,非 UB 实验):

```
[strtok] main strtok #1 -> alpha
[strtok] (handler-style) strtok -> X   <- 静态进度被覆盖
[strtok] main strtok #2 (expect beta) -> Y   <- CORRUPTED
[localtime] first  localtime -> 2001-09-09 01:46:40
[localtime] second localtime -> 2014-05-13 16:53:20
[localtime] re-read pointer a   -> 2014-05-13 16:53:20   <- a silently became t2
```

strtok 的进度指针、localtime 的返回结构都住在函数内部静态存储,第二次进入覆盖第一次的进度。man 7 signal-safety 里 strtok 不在安全表(strtok_r 在),localtime 一类整个不在表上,连 localtime_r 也落在表外——同根同源:带 _r 后缀的可重入版本就是为此存在。

### E5:handler 里能做什么(工程模式)

模式一 flag+主循环(flag_graceful.out):子进程 100ms 发 SIGINT,handler 只置 `g_stop=1`,主循环在 `t= 100ms g_stop seen at iter 5 -> break`,然后从容走 cleanup,exit 0(默认处置下 SIGINT 是直接暴毙)。

模式二 self-pipe trick(self_pipe.cpp,完整事件循环,self_pipe.out 时序):

```
t=   0ms event loop starts, poll(pipe_r, 200ms timeout)
t= 100ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 301ms (heartbeat, nothing readable)
t= 501ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 551ms poll woke up -> byte 't' = SIGTERM -> shutdown
t= 551ms event loop exited cleanly
```

handler 只 `write` 一个字节进 pipe(`pipe2(O_NONBLOCK|O_CLOEXEC)`),信号从"打断执行流的东西"变成"管道里一个可读事件",主循环 `poll` 统一调度,超时槽位还能跑周期任务——这就是 ch04 多路复用的钩子。O_NONBLOCK 两处意义:handler 的 write 在管道满时立刻 EAGAIN 丢弃而不是把 handler 卡死,主循环 drain 靠 EAGAIN 知道读干净了。

模式三 siglongjmp 跳出:见 `memory/02-vm-apis/04-guarded-buffer/e4.cpp`(sigsetjmp/savemask=1 + handler 内 write + siglongjmp 回主流程),本篇不重做。

### E6:SIGCHLD 的 reap 模式

**为什么必须循环**:3 个子进程在 SIGCHLD 阻塞期退出,信号合流成 1 位,解锁后 handler 只进来一次——里面必须把尸首一口气收完:

```
    3 children dead; pending SIGCHLD=1 (one bit, not three)
    handler entries=1, reaped in handler=3 (of 3 kids)          <- 循环 waitpid(WNOHANG)
```

反例(naive 版,handler 只 waitpid 一次,sigchld_reap_naive.out):

```
    handler entries=1, reaped in handler=1 (of 3 kids)
    kid 1 (pid 15166): /proc state = 'Z'  <- ZOMBIE left behind
    kid 2 (pid 15167): /proc state = 'Z'  <- ZOMBIE left behind
```

**SA_NOCLDSTOP**:默认处置下 stop/continue/exit 各发一次 SIGCHLD(计数 1→2→3),加 SA_NOCLDSTOP 后 stop 与 continue 静默(计数 0→0),退出仍发(1)。**SA_NOCLDWAIT**:子进程退出不变成僵尸,`/proc/<pid>` 直接消失(state='?'),`waitpid` 返回 -1 且 errno=10(ECHILD)。对照默认路径:state='Z',waitpid 拿到 WEXITSTATUS=7。

### E7:常用信号速览

三条硬事实实测(signal_facts.out):

```
    sigaction(SIGKILL , SIG_IGN) -> -1 errno=22 (Invalid argument)
    sigaction(SIGSTOP , SIG_IGN) -> -1 errno=22 (Invalid argument)
[E7.2] child end: WIFSIGNALED=1 WTERMSIG=9 (SIGKILL=9)      <- SIGTERM 被 SIG_IGN 挡了,SIGKILL 挡不住
[E7.3] child-A end: WIFSIGNALED=1 WTERMSIG=13 (SIGPIPE=13)   <- 默认动作:写已关管道,直接死
    CHILD-B: write returned -1 errno=32 (Broken pipe) -> alive, error is visible  <- SIG_IGN 后变成可查的错误码
```

速览表(编号来自本机 `kill -l` 快照即 kill_l.txt,默认动作与语义按 man 7 signal,x86-64):

| 信号 | 编号 | 默认动作 | 捕获/忽略 | 典型用途 |
|---|---|---|---|---|
| SIGHUP | 1 | 终止 | 可 | 控制终端断开。终端死亡时全组各收一条,守护进程篇讲 nohup 怎么挡 |
| SIGINT | 2 | 终止 | 可 | Ctrl-C,前台进程组"请退出"的礼貌版 |
| SIGKILL | 9 | 终止 | **否** | 最后手段。不可捕获不可忽略,本篇实测 sigaction 直接 EINVAL |
| SIGSEGV | 11 | 终止+core | 可 | 野指针/越界。handler 精确报错见 Lmem02 guarded_buffer |
| SIGUSR1/2 | 10/12 | 终止 | 可 | 用户自定义,本篇实验载体 |
| SIGPIPE | 13 | 终止 | 可 | 写已关闭的管道/socket。网络程序常 SIG_IGN 改查 EPIPE(本篇实测) |
| SIGALRM | 14 | 终止 | 可 | alarm/setitimer 计时到期,E3 的实验载体 |
| SIGTERM | 15 | 终止 | 可 | 通用终止请求,kill 默认发它,systemd stop 也走它 |
| SIGCHLD | 17 | 忽略 | 可 | 子进程 stop/continue/exit,驱动 reap(E6) |
| SIGSTOP | 19 | 停止 | **否** | 无条件暂停(配 SIGCONT 恢复),不可捕获不可忽略 |

## 复现命令

```sh
# E1
g++ -std=c++20 -Wall -Wextra -O2 01-delivery-model/delivery_model.cpp -o /tmp/e1 && /tmp/e1

# E2
g++ -std=c++20 -Wall -Wextra -O2 02-sigaction-family/sigaction_family.cpp -o /tmp/e2 && /tmp/e2

# E3(unsafe 全量跑法,~1.8MB 不入档,损坏帧靠 grep 提取)
g++ -std=c++20 -Wall -Wextra -O2 03-async-safety/unsafe_printf.cpp -o /tmp/e3u && /tmp/e3u > /tmp/unsafe_full.out
grep -n -v "^M [0-9]\{6\} abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ$" /tmp/unsafe_full.out | grep -v DONE
g++ -std=c++20 -Wall -Wextra -O2 03-async-safety/safe_write.cpp -o /tmp/e3s && /tmp/e3s > /tmp/safe_full.out

# E4(两种编译 + 汇编旁证;124 = timeout 杀掉,正是反例的证据)
g++ -std=c++20 -Wall -Wextra -O2 04-reentrancy/flag_spin.cpp -o /tmp/e4p
g++ -std=c++20 -Wall -Wextra -O2 -DUSE_VOLATILE 04-reentrancy/flag_spin.cpp -o /tmp/e4v
timeout 3 /tmp/e4p; echo "exit=$?"
g++ -std=c++20 -Wall -Wextra -O2 04-reentrancy/static_state.cpp -o /tmp/e4st && /tmp/e4st

# E5
g++ -std=c++20 -Wall -Wextra -O2 05-handler-patterns/flag_graceful.cpp -o /tmp/e5a && /tmp/e5a
g++ -std=c++20 -Wall -Wextra -O2 05-handler-patterns/self_pipe.cpp -o /tmp/e5b && /tmp/e5b

# E6(loop 与 naive 各一遍)
g++ -std=c++20 -Wall -Wextra -O2 06-sigchld-reap/sigchld_reap.cpp -o /tmp/e6
/tmp/e6 loop; /tmp/e6 naive

# E7
g++ -std=c++20 -Wall -Wextra -O2 07-signal-facts/signal_facts.cpp -o /tmp/e7 && /tmp/e7
kill -l    # 62 个信号,快照在 07-signal-facts/kill_l.txt
```

## 备注(采证时的意外与限制)

- **`.out` 全部是原始 stdout**,未改动一个字节。E3 的 unsafe 全量输出约 1.8MB、safe 约 1.2MB,不入档:入档的是 excerpt(用 sed 从对应 run 切的**连续窗口**,切片不改内容)与 corrupt_frames.txt(带行号的损坏帧索引,是 grep 衍生数据,不是原始输出)。切窗口的行号写进了文件名对应关系,复现命令能重新生成全量。
- **tick 数随 run 变化**(unsafe 23 / safe 253):缓冲策略决定主循环耗时,timer 落点数量不同。引用行数/tick 数时对准对应 run,两次 run 的数字不可拼在一起算差。
- **E3 unsafe 的 DONE 被粘进主循环行**(`M 029988 abcDONE ticks=23`):程序收尾用 write(2) 直写 fd,而 stdio 缓冲里还有未 flush 的主循环内容,先 write 的字节落在文件更靠前的位置。这是"write 安全"的另一面——安全说的是不会损坏状态,不是自动和 stdio 缓冲排队。safe 版把 stdout 设成无缓冲,正是为了让两条路径时序一致。
- **printf 与 handler 内 write 混排的顺序**:所有程序(除 E3 两版与 E4 plain)开头 `setvbuf(stdout, nullptr, _IOLBF, 0)`,否则管道/文件下 printf 整段缓冲,handler 的 write 会整体跑到 printf 前面,.out 时序错乱(E1 首版采证就遇到,handler 行全部涌到文件头)。
- **E2 的 SA_RESTORER**:读回的 sa_flags=0x14000000 比 man 文档多出 0x04000000,是 glibc 传给内核的内部标志(rt_sigreturn 蹦床),POSIX 不定义。正文引用读回值时要么注明,要么只引 SA_RESTART 位。
- **E6.A 的 continue 计数**:默认处置下 WIFCONTINUED 与 SIGCHLD 计数+1 同一次 waitpid 轮询里读到,行号上同毫秒——是真实的(信号先于轮询到达),不是漏采。
- **E7 的 kill -l 共 62 个信号**(含 RTMIN..RTMAX 区间),实时信号语义归《信号(下)》。
- 采证复跑:E1/E5/E6 各 3 遍关键行逐字一致。E3 的损坏帧位置与数量每 run 不同(时序决定),复现命令每次都会产出一份新的损坏帧索引,这是实验性质,不是不稳定。
