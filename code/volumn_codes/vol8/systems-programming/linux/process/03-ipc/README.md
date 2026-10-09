# 03-ipc 配套实验

《IPC:管道、FIFO 与 POSIX 消息队列》(vol8 systems-programming/linux/process 进程章第 3 篇)的实验代码与原始输出存档。核心问题一句话:**两个进程交换数据,内核给了几条路,每条路的账单不一样**——管道是字节流过内核环形缓冲,mq 是带优先级的定长消息过内核,shm 是根本不过内核,SCM_RIGHTS 传的不是数据是访问权。与前篇分工:Lmem03(memory/03-shm)已做过 shm vs pipe 吞吐基线(7304 vs 2349 MiB/s)并说「pipe 细讲留 IPC 篇」,本篇接棒;SCM_RIGHTS 在 Lmem03 一句带过,本篇 E6 展开闭环。七个实验:E1 管道机制解剖、E2 管道语义家族、E3 FIFO、E4 POSIX 消息队列(重头戏)、E5 POSIX 信号量、E6 SCM_RIGHTS fd 传递、E7 选型矩阵。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -O2 -I ../common`(E4/E5 另加 `-pthread`,全档零警告) |
| glibc | 2.44——mq/sem 符号在 libc,链接不需要 `-lrt`/不必 `-pthread` 也过,但按惯例保留 |
| CPU | AMD Ryzen 7 9700X 8C/16T(WSL2 视角 16 逻辑核) |
| strace | 7.2(E2 popen 佐证) |
| scratch | `/home/charliechen/lp03_scratch`(FIFO 路径、E6 负载文件烧死在源码常量里,复跑前先 `mkdir -p ~/lp03_scratch`) |
| 内核参数 | `/proc/sys/fs/pipe-max-size`=1048576(非特权 F_SETPIPE_SZ 上限);PIPE_BUF=4096(linux/limits.h);mqueue:queues_max=256、**msg_max=10**、**msgsize_max=8192**(非特权进程的队列深度/单条上限);`/dev/mqueue` 已挂载(mqueue 文件系统) |
| 捕获日期 | 2026-10-04;`.out` 均为 stdout+stderr 合流的原样捕获 |

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-pipe-mechanics/` | E1a 容量探针 | 新管道 `F_GETPIPE_SZ`=**65536**(16 页);`F_SETPIPE_SZ` 按页向上取整(请求 5000 → 实际 8192);调到 262144 成功;请求 2 MiB > pipe-max-size(1048576)→ **EPERM** |
| `01-pipe-mechanics/` | E1b 写满阻塞 | 读者不读,写者 4096/块连写 16 块恰好 65536 字节,第 17 块 write 从 +0.2 ms 阻塞到 +2000.3 ms,读者读走一块后立即放行;总量 69632 一字节不差 |
| `01-pipe-mechanics/` | E1c SIGPIPE/EOF | 空管道+活写者:read 干等 1000 ms 才拿到 4 字节;写端全关:read 返 0(EOF,与 Windows 篇 W01「ReadFile 到文件尾返 0」同义);读端全关:信号处理器被调(SIGPIPE)+ write 返 -1 errno=32(EPIPE);SIG_IGN 后只剩 errno 一条通道 |
| `02-pipe-family/` | E2a popen | popen/pclose 读写两方向全通,`exit 7` 的退出码经 pclose 原样透传;strace 佐证内部 = `pipe2([3,4],O_CLOEXEC)` + `clone3(CLONE_VM\|CLONE_VFORK)` + 子进程 `dup2(4,1)` + `execve("/bin/sh")`——教科书说 fork+dup2+exec,glibc 2.44 实际用 posix_spawn 式 vfork,语义等价、调用路径不同 |
| `02-pipe-family/` | E2b CLOEXEC | 子进程 exec `ls -l /proc/self/fd` 自证:裸 pipe 的 fd 3/4(`pipe:[inode]`)活着穿过 exec;pipe2(O_CLOEXEC) 全消失;裸 pipe+fcntl 只补写端 FD_CLOEXEC → 只剩读端——标志位逐 fd,与 L01 文件篇结论同一条 |
| `02-pipe-family/` | E2c dup2 工程 | fork→子进程 dup2(pipefd[1], STDOUT_FILENO)→exec /bin/sh:stdout 行进管道被父进程收走,stderr 行直接落终端——dup2 精确到「这一个 fd」,这就是 popen 的手工完整版 |
| `03-fifo/` | E3a 相会 | mkfifo 出 `prw-r--r--`(p 类型位;**请求 0666 被 umask 0022 截成 0644**,与 Lmem03 的 shm_open 同款现象);读端进程 open(O_RDONLY) 干等 801.1 ms 直到写端 exec 出现——两个进程只认识路径名 |
| `03-fifo/` | E3b open 语义 | O_RDONLY 阻塞至写者(实测等 500.1 ms)、O_WRONLY 阻塞至读者(500.1 ms);`O_WRONLY\|O_NONBLOCK` 无读者 → **ENXIO**(errno 6);`O_RDONLY\|O_NONBLOCK` 无写者 → **成功**,紧接着 read 返 **0**(EOF 陷阱:poll 表现为永真 POLLIN);mkfifo(0400) 后属主自己 open(O_WRONLY) → **EACCES**(errno 13);F_GETPIPE_SZ 同为 65536,非阻塞灌满也是整 65536 字节 |
| `03-fifo/` | E3c 原子边界 | 两个并发写者 × 单读者,2×2 矩阵:记录 4096(==PIPE_BUF)阻塞/非阻塞**多轮全零撕裂**;记录 8192(>PIPE_BUF)撕裂是**概率性**的(阻塞/非阻塞都撕过,窗口数逐轮大幅浮动);man 7 pipe 原话:「POSIX.1-2001 says that writes of ≤PIPE_BUF bytes must be atomic … may be interleaved」 |
| `04-mqueue/` | E4a 基本盘 | mq_open("/lp03_e4") 的实体落在 **/dev/mqueue** 挂载点(任务书里猜的 /dev/mq 不存在,是 mqueue 文件系统;目录项 size 列显示 80,是内核 ipc/mqueue.c 写死的 FILENT_SIZE=80 常量,不是消息字节);三条 10/50/200 字节消息按 prio 1→3→2 发,按 **3/2/1** 收,长度分毫不差;同 prio 内 FIFO;mq_receive 缓冲区必须 ≥ mq_msgsize 否则 EINVAL(实测踩过) |
| `04-mqueue/` | E4b 边界对照 | 同样三条消息走 pipe:读者第一次 read(64) 就把 消息1+消息2+消息3 开头 拼成一团——字节流要自力拆包,mq 的边界是免费的 |
| `04-mqueue/` | E4c mq_notify | SIGEV_SIGNAL/SIGUSR1 注册后,队列空→非空瞬间信号到达:si_code=**-3(SI_MESGQ)**,sigev_value.sival_ptr 塞的 0xC0DE 原样带回;注册一次性:消费后第二条消息到货(mq_curmsgs=1)但信号不再来——要继续被通知得重新 mq_notify |
| `04-mqueue/` | E4d 吞吐基线 | 同 Lmem03 口径(1 MiB、逐块校验、3 轮):mq@1024B 中位 **1245 MiB/s**(Lmem03:pipe@1024 2349、shm 7304);mq@8192B 中位 **3179**(非特权 msgsize_max 顶格);同尺寸对照 pipe@8192 中位 **4753**——同尺寸下 mq 恒慢于 pipe(每条消息多付优先级队列记账),消息越大差距越小;排序全程稳定:shm ≫ pipe ≥ mq |
| `05-semaphore/` | E5a 名额时序 | 命名信号量初值 2(两个名额),三个 exec 出来的 worker 按 150 ms 梯次进场:worker0/1 等待 0.0 ms 直接入场,worker2 **干等 699.4 ms** 直到 worker0 post 放行;结束 sem_getvalue 回 2,名额一个不少——与 Lmem03 的互斥锁(一把钥匙)相对,这是计数语义(N 个名额) |
| `05-semaphore/` | E5b 杂项 | sem_timedwait(300ms) → -1 ETIMEDOUT(errno 110),实测 300.1 ms;sem_init(pshared=1) 匿名信号量放共享匿名映射,fork 后父等子的 post 恰好 300.1 ms;sem_unlink 后旧句柄照用(post 旧实体 1→2)、同名重建的新句柄从初值 7 开始——两个实体互不相干,与 Lmem03 E1 的 shm_unlink 语义同构 |
| `06-fdpassing/` | E6 SCM_RIGHTS | socketpair+sendmsg 附属数据传 fd:接收方拿到**自己的新编号**(发送方 fd=3,接收方 fd=4),`/proc/self/fdinfo` 证明 **pos 共享**(发送方读到 16,接收方接着读 16→32);发送方 close 原始 fd 后接收方**照读不误**(32→56)——传的是对同一打开文件描述的新引用(引用计数),不是移交;三条获得 fd 的路径(open/fork/SCM_RIGHTS)对照表在 .out 末尾 |
| `07-selection-matrix/` | E7 选型 | pipe/FIFO/mq/sem/shm/SCM_RIGHTS 适用矩阵 + 决策序,见该目录 README(纯素材无代码) |

## 吞吐汇总(同机同口径,中位数)

| 实验 | 通道 | 粒度 | r1 | r2 | r3 | 中位 |
|---|---|---|---|---|---|---|
| E4d(MiB/s) | mq(队列深 10) | 1024 B × 1024 | 1213 | 1245 | 1429 | **1245** |
| E4d(MiB/s) | mq(队列深 10) | 8192 B × 128 | 3579 | 3179 | 2491 | **3179** |
| E4d(MiB/s) | pipe | 8192 B × 128 | 5459 | 4753 | 6226 | **4753** |
| Lmem03 E5(引用) | shm SPSC 环形 | 1024 B × 1024 | 7343 | 7304 | 3917 | **7304** |
| Lmem03 E5(引用) | pipe | 1024 B × 1024 | 2349 | 2169 | 2390 | **2349** |

## 复跑

```bash
cd 01-pipe-mechanics
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_capacity.cpp -o e1_capacity && ./e1_capacity
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_write_full.cpp -o e1_write_full && ./e1_write_full
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e1_sigpipe_eof.cpp -o e1_sigpipe_eof && ./e1_sigpipe_eof
cd ../02-pipe-family
g++ -std=c++20 -Wall -Wextra -O2 e2_popen.cpp -o e2_popen && ./e2_popen
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e2_cloexec.cpp -o e2_cloexec && ./e2_cloexec
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e2_dup2_exec.cpp -o e2_dup2_exec && ./e2_dup2_exec
strace -f -e trace=pipe,pipe2,dup,dup2,dup3,close,execve,clone,clone3,wait4 -o e2_popen_strace.txt ./e2_popen
cd ../03-fifo
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_fifo_meet.cpp -o e3_fifo_meet && ./e3_fifo_meet
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_open_semantics.cpp -o e3_open_semantics && ./e3_open_semantics
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e3_pipebuf_atomic.cpp -o e3_pipebuf_atomic && ./e3_pipebuf_atomic
cd ../04-mqueue
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_basics.cpp -o e4_mq_basics && ./e4_mq_basics
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_vs_pipe.cpp -o e4_mq_vs_pipe && ./e4_mq_vs_pipe
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_notify.cpp -o e4_mq_notify -pthread && ./e4_mq_notify
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_throughput.cpp -o e4_mq_throughput -pthread && bash run_e4.sh
cd ../05-semaphore
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e5_sem_slots.cpp -o e5_sem_slots -pthread && ./e5_sem_slots
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e5_sem_misc.cpp -o e5_sem_misc -pthread && ./e5_sem_misc
cd ../06-fdpassing
g++ -std=c++20 -Wall -Wextra -O2 -I ../common e6_scm_rights.cpp -o e6_scm_rights && ./e6_scm_rights
```

## 踩坑记录(写文章时可引用)

1. **mq 名字空间是 /dev/mqueue,不是 /dev/mq**——mqueue 文件系统挂载点;目录项 ls 的 size 列是 80(内核 ipc/mqueue.c 写死的 FILENT_SIZE=80 常量,LP64 上 sizeof(mq_attr)=32,两者无关),不代表消息字节。
2. **mq_receive 的缓冲区必须 ≥ 队列的 mq_msgsize**,否则 EINVAL——与 read「要多少给多少」的直觉相反,容量声明错了直接拒收。
3. **非特权 mq 上限低得惊人**:msg_max=10(队列深)、msgsize_max=8192(单条),都在 /proc/sys/fs/mqueue/;想开深队列要 root 调,或换别的 IPC。
4. **O_RDONLY|O_NONBLOCK 开 FIFO 会成功,随后 read 返 0**——「从没出现过写者」与「写者全关」在 read=0 上撞车,poll 表现为永真 POLLIN;非阻塞读 FIFO 的标准解法是 O_RDWR 打开或状态机里区分。
5. **umask 会截 mkfifo 的 mode**:请求 0666,WSL 默认 umask 0022 下落盘 0644(与 Lmem03 shm_open 同款)。
6. **PIPE_BUF 原子性的实测形状**:≤PIPE_BUF 的窗口零撕裂在所有轮次成立(保证);>PIPE_BUF 的撕裂是概率性的——读者按整条记录为单位释放空间时,被唤醒的写者常一口气吃完全部空槽,交错被掩盖(若干轮 0 撕裂);把 read 粒度降到 4096(页粒度)交错立刻显形。给文章的口径:安全线只有一条,单条消息别超过 PIPE_BUF。
7. **popen 的教科书说法要更新**:glibc 2.44 的 popen 走 pipe2(O_CLOEXEC)+clone3(CLONE_VM|CLONE_VFORK)(posix_spawn 式 vfork)+dup2+execve,不是经典 fork;对学习者语义等价,但 strace 看到的就是这个。
8. **E4d 的吞吐数字逐轮波动大(WSL2 ±40%)**:绝对值只报量级,排序(shm ≫ pipe ≥ mq,消息越大差距越小)在全部轮次稳定。
9. **fork 出的子进程 printf 必须 fflush 或先 setvbuf 行缓冲**,否则 `_exit` 丢缓冲、多进程输出顺序乱(Lmem03 已踩,本篇全档 `setvbuf(stdout, nullptr, _IOLBF, 0)` 预防)。
