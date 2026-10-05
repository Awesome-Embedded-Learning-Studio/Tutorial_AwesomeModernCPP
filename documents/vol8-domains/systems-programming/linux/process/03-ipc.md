---
title: "IPC:管道、FIFO 与 POSIX 消息队列"
description: "两个进程怎么交换数据:管道把字节流送进内核的环形缓冲,本篇实测写满 65536 字节停住的时序(第 17 块 write 从 +0.2 ms 卡到 +2000.3 ms,读者读走一块即放行)、读端全关时 SIGPIPE 加 EPIPE、写端全关时 read 返 0 的 EOF,pipe2(O_CLOEXEC) 三姿势与 dup2 工程版,strace 揭出 glibc 2.44 的 popen 实际走 pipe2 加 clone3(CLONE_VM|CLONE_VFORK) 的 posix_spawn 路数而非教科书写的 fork;FIFO 的 open 双向互等、O_WRONLY|O_NONBLOCK 无读者回 ENXIO 而 O_RDONLY|O_NONBLOCK 无写者成功随后 read 返 0 的陷阱、4096 字节 PIPE_BUF 内多轮零撕裂与 8192 的概率撕裂(被读者释放粒度掩盖的交错);POSIX mq 的实体落在 /dev/mqueue、三条 10/50/200 字节消息按 prio 3/2/1 收的边界保留对照 pipe 的拼团、mq_notify 的一次性注册,POSIX 信号量两名额三进程的干等与放行、sem_unlink 与 shm_unlink 同构,SCM_RIGHTS 传 fd 的复制引用语义(接收方拿到自己的新编号,fdinfo 证 pos 共享,发送方 close 后照读),收在六通道适用矩阵、决策序与带宽对照(shm 7304 ≫ pipe 2349 ≥ mq 1245 MiB/s,引内存篇基线不重测)"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 21
prerequisites:
  - "共享内存:shm_open 与映射"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
related:
  - "文件锁:flock 与 fcntl 记录锁"
  - "错误处理范式:从 errno 到 expected"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# IPC:管道、FIFO 与 POSIX 消息队列

进程章的前两篇,咱们一直在跟单个进程较劲:怎么把它造出来([fork 与 exec](01-fork-exec.md)),怎么让它换一个程序接着跑,又怎么把它收到后台去当守护进程([守护进程篇](02-daemon.md))。这一篇对手换了:两个进程,怎么把一批数据从一边送到另一边?地址空间是互相隔离的,A 进程的指针拿到 B 进程里什么都不是,所以中间必须有一个两边都够得着的地方,而这些地方全在内核手里。您也别忘了头一篇留下的线索:fork 出的子进程会原样继承 fd,连偏移都是共享的,可继承的方式只认亲缘,而且它递过去的只是访问权,咱们的数据还一个字节都没动。咱们真要把字节送到对面,就得把内核给的几条通道挨个走一遍。

内核手里的材料不少。管道(pipe)把字节流送进内核的一圈环形缓冲,咱们从写端放字节、从读端取字节。FIFO 给管道挂上了一个路径名,不相干的进程也能按名字找到同一根。POSIX 消息队列送的是一条条带 prio 值的定长消息。POSIX 信号量送的只是名额,也就是同时放几个进程进场的许可。再有一路更特别的,连 fd 本身都能装进套接字的附属数据里递过去。共享内存的那边,[共享内存](../memory/03-shm.md)(咱们按目录顺序叫它 Lmem03)已经走全了,从同一批物理页的映射,到一百万条消息零丢失的环形队列、竞态实测、吞吐对照,都是那边的活儿。所以本篇不重跑那套实验,带宽数字咱们直接引用,手头的任务是把 Lmem03 没细讲的管道、只带过一段的 fd 传递,补成完整的一张表。

实验环境咱们一次交代清楚,后面的数字都要靠它对表。全部实验出自笔者的 WSL2:内核是 6.18.33.2-microsoft-standard-WSL2,CPU 用的是 AMD Ryzen 7 9700X(WSL2 视角下 16 个逻辑核),编译器用的是 g++ 16.2.1,咱们统一按 `g++ -std=c++20 -Wall -Wextra -O2` 编译,消息队列与信号量的实验另加了 `-pthread`,glibc 用的是 2.44,strace 用的是 7.2。代码与全部的原始输出都收在仓库 `code/volumn_codes/vol8/systems-programming/linux/process/03-ipc/` 目录,README 里带着每条的复跑命令,正文里贴的输出均为节选,删掉的位置以 `...` 标出,一律以存档的原文为准。实验的编号按 E1 到 E7 排,编号跟着存档的目录走,目录里再分小项的,咱们带上字母(比如 E4a)。公共工具 `errno_code`、`sys_call` 沿用 [RAII 篇](../../thinking/01-raii-paradigm.md)与[错误处理篇](../../thinking/02-error-paradigm.md)的定义,实验把它们收进了 common/ipc_util.hpp 接着用。还有一句测量口径要提前交代:WSL2 上带宽逐轮的波动能到 ±40%,所以后文的吞吐数字咱们只报量级,而排序(shm ≫ pipe ≥ mq)在全部轮次里稳定,您引用的时候请把这句话一起带走。

## 管道:内核环形缓冲里的字节流

`pipe()` 一次交回两个 fd:pfd[0] 是只读的,pfd[1] 是只写的,数据的走向固定死了:写端进、读端出,想双向的咱就开两根。真正的存储放在内核里,是一圈环形缓冲:write 把用户空间的字节拷进去,read 再把它们拷了回来,咱们一来一回,花的就是两次系统调用加两次拷贝。Lmem03 搬 1 MiB 的时候,pipe 只有 shm 的三分之一快,差价就是这两趟拷贝来的。那缓冲到底多大?man 7 pipe 的说法是 Linux 2.6.11 起默认 16 页,咱们不用背它、拿 fcntl 一问便知,顺便把调容量的路也试了一遍(E1a):

```text
$ ./e1_capacity
新建管道:fd=3(读)、4(写),F_GETPIPE_SZ = 65536 字节(16 页 × 4096)
F_SETPIPE_SZ(4096) 返回 4096 → 再探 F_GETPIPE_SZ = 4096 字节
F_SETPIPE_SZ(5000) 返回 8192 → 实际容量 8192 字节(向上取整到 2 页)
F_SETPIPE_SZ(262144) 返回 262144 → 实际容量 262144 字节(64 页)
/proc/sys/fs/pipe-max-size = 1048576(非特权 F_SETPIPE_SZ 的上限)
F_SETPIPE_SZ(2097152 超上限) 返回 -1,errno=1 (Operation not permitted)——想要更大得有 CAP_SYS_RESOURCE
```

咱们把输出里的三处挑出来看。默认的 65536 正好是 16 页,man 页诚不我欺。改容量的时候,内核是按页取整的:请求 5000,拿到的是 8192,它只肯整页整页地给。想调大也是有天花板的,当裁判的是 `/proc/sys/fs/pipe-max-size`,笔者的机器上是 1048576,非特权进程越了线就吃 EPERM(errno 1),再想要更大的,就得拿 CAP_SYS_RESOURCE 的能力位说话了。

容量为什么值得咱们关心?因为写满之后 write 会睡过去,这就是管道自带的流控,咱们拿时序把它拍了下来(E1b)。写者的每块是 4096 字节、连写 17 块,读者按兵不动地等满两秒:

```cpp
// e1_write_full.cpp(节选):写者每写一块就打一行时间戳,fork 出的子进程要手动 fflush
constexpr int kChunk = 4096;
constexpr int kChunks = 17;  // 17 × 4096 = 69632,前 16 块正好填满默认 64 KiB
for (int i = 1; i <= kChunks; ++i) {
    std::printf("[%7.1f ms] 写者:已累计 %5d 字节,即将写第 %d 块\n",
                ms_since(t0), (i - 1) * kChunk, i);
    std::fflush(stdout);
    if (write(pfd[1], buf, kChunk) != kChunk) { perror("child write"); _exit(1); }
    std::printf("[%7.1f ms] 写者:第 %d 块写完,累计 %5d 字节%s\n",
                ms_since(t0), i, i * kChunk,
                i == 16 ? "(== 容量,恰好填满)" : "");
}
```

```text
$ ./e1_write_full
[    0.2 ms] 写者:第 16 块写完,累计 65536 字节(== 容量,恰好填满)
[    0.2 ms] 写者:已累计 65536 字节,即将写第 17 块
[ 2000.3 ms] 写者:第 17 块写完,累计 69632 字节
[ 2000.3 ms] 写者:17 块全部写完,关写端退出
管道容量 F_GETPIPE_SZ = 65536 字节;写者每块写 4096 字节,共 17 块,读者按兵不动 2 秒
[ 2000.2 ms] 读者:读走 4096 字节——腾出一个块的空间
[ 2000.3 ms] 读者:EOF,共读 69632 字节(= 17 × 4096,一块不差)
结论:读者不读时,写者写到 65536 字节(默认容量)后,下一次 write 阻塞约 2000 ms,直到读者腾出空间
```

咱们把时间戳一行行对过去,要看的动静全在第 17 块身上。前 16 块每块都是 0.2 ms 就写完的,一路攒到了 65536,恰好把容量填满了。第 17 块的 write 在 0.2 ms 出发,回来的时候已经是 2000.3 ms,中间的整整两秒,它就睡在内核的缓冲里,咱们谁也没有去叫它,是父进程读走了一块,腾出了一个槽,它才醒过来把活干完了。最后读者一共拿到了 69632 字节,算下来正是 17×4096 的积,咱们核对下来一个字节都不差。写快读慢的场景里,这样的停顿看着像故障,其实是内核在替咱们背压,您要是再在应用层自己做一层节流,多半就是重复劳动了。

缓冲的两头还各有一个边界事件,出事的概率比容量还要高,咱们接着看(E1c)。读侧的等待有两种结局。管道空着而写者还活着的时候,read 干等到了 1000.3 ms 才拿到 4 个字节。写端的 fd 全关了,read 返回的就是 0,咱们管它叫文件尾(EOF),语义上和 [Win32 文件 I/O](../../windows/file-io/01-win32-file-io.md)(Windows 侧的第 1 篇,咱们叫它 W01)里 ReadFile 读到文件尾返回 0 是同一件事。写侧的事件更凶:读端全关之后,write 触发的就是 SIGPIPE,实验里咱们装了信号处理器,所以 write 还能活着回来交 errno:

```text
$ ./e1_sigpipe_eof
[1000.3 ms] read#1 返回 4:「ping」——管道空但写者活着时,read 干等了约 1000 ms
[1300.5 ms] read#2 返回 0——写端全关,这是 EOF(与 Windows 篇 W01 的 ReadFile 到文件尾返 0 同一语义)

== 第二幕:读端全关 → SIGPIPE + EPIPE ==
write(写端) 返回 -1,errno=32 (Broken pipe);信号处理器被调过:是(SIGPIPE 已被我们捕获)
SIGPIPE 设为 SIG_IGN 再来一次:write 返回 -1,errno=32 (Broken pipe)——忽略信号后错误只剩 errno 这一条通道
```

这里请您把处置方式记成一条工程习惯:SIGPIPE 的默认处置是终止进程,什么都没设防的程序多半直接死掉了,连看一眼 errno 的机会都没有。咱们把 SIGPIPE 设成 SIG_IGN(或者阻塞它),write 就改成返回 -1 加 EPIPE(errno 32)了,错误走进了您能检查的通道。网络程序里经典的猝死,十有八九就是没设防的 SIGPIPE。头一篇的孤儿实验就真吃过这一下:第一版装置里两条报告管道的读端在观察者手里关早了,孙辈的 write 直接吃了 SIGPIPE,静默地死在了半路,收养的那一行输出也凭空消失了。头一篇答应过要把它请回来当例证的,现在咱们把它的机制看全了。至于信号到底怎么投递、处理器里能干什么,那是下一篇[信号篇](04-signal-basic.md)的正题,咱们在这里只记管道侧的两件事:默认会杀进程,忽略之后错误走的就剩 errno。

## pipe2、dup2 与 popen:把管道接给子进程

管道真正的用法,几乎都要跨一次 exec:shell 的 `a | b`,就是把 a 的 stdout 接进管道、b 的 stdin 接出来,两头各自 exec 成了新程序。咱们在 [POSIX 文件 I/O](../file-io/01-posix-file-io.md)(咱们叫它 L01)里量过 fd 能不能穿 exec:没挂 FD_CLOEXEC 的 fd 会原样穿过去,挂了的会在 exec 那一刻被内核替您关掉。管道带的是两个 fd,而标志是逐 fd 的,E2b 把三种姿势摆上了同一台机器,咱们让子进程 exec 成 `ls -l /proc/self/fd` 自己作证:

```text
$ ./e2_cloexec
== 姿势一:裸 pipe(fd=3 读、4 写) ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> pipe:[353914]
l-wx------ 1 charliechen charliechen 64 Oct  4 11:06 4 -> pipe:[353914]
== 姿势二:pipe2(O_CLOEXEC) ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> /proc/23680/fd
== 姿势三:裸 pipe + fcntl 只给写端(fd=4)补 FD_CLOEXEC ==  子进程 exec 后的 fd 表:
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 3 -> pipe:[353916]
lr-x------ 1 charliechen charliechen 64 Oct  4 11:06 4 -> /proc/23681/fd
```

三种姿势咱们对得很齐。裸 pipe 的 3 和 4 都活着,指向的还是同一个 inode(`pipe:[353914]`),exec 是拦不住它们的。姿势二用了 `pipe2(fd, O_CLOEXEC)`,创建的时候一把挂上标志,exec 之后管道的 fd 全没了,表里剩下的 3 号,是 ls 自己打开的目录。姿势三是老代码里最常见的样子:创建时没带标志,事后拿 fcntl 给写端补了一个,于是 exec 之后就只剩读端还活着了。标志长在 fd 的表项上,按 fd 一个一个地配,这正是 L01 讲 FD_CLOEXEC 时的同一条经验,只是管道的这对 fd 一起出场,反而更容易看清它是逐 fd 生效的。

接好 fd 的下一步,咱们把标准流换过去,这就是 dup2 的活儿了。E2c 是一个完整的小工程:fork 之后,子进程把 `dup2(pipefd[1], STDOUT_FILENO)` 做在 exec 的前面、再 exec 成 `/bin/sh -c`,于是这个 shell 的一切 stdout 都流进了管道,而 stderr 原封不动落终端:

```text
$ ./e2_dup2_exec
stderr-line:我还是落终端
父进程:等子进程(pid=23683)的 stdout……(stderr 那行你应该已经直接在下面看到了)
父进程:从管道拿到 49 字节的子进程 stdout:
stdout-line:我走的是被 dup2 过来的管道
waitpid:exit=0——这就是 popen 内部那套 fork+dup2+exec 的手工完整版
```

`dup2(oldfd, newfd)` 做的事,是把 newfd 这个编号重新指到 oldfd 指的同一个地方,精确到了单个 fd,所以咱们能让 stdout 进管道、stderr 留在屏幕,各走各的路。咱们看完手工版,标准库的 popen 就好认了(E2a):popen 帮您建管道、fork、dup2、exec 一条龙,pclose 负责替咱们收割,连子进程的退出码都原样透传:

```text
$ ./e2_popen
popen("exit 7") → pclose = 1792,WEXITSTATUS = 7:子进程退出状态原样透传
```

1792 就是 7 左移 8 位的结果,是 waitpid 的状态编码,咱们核对过,popen 没有动过它的一个字节。真有意思的在 strace 里,咱们把系统调用全录了下来(E2a 附档):

```text
$ strace -f -e trace=pipe,pipe2,dup,dup2,dup3,close,execve,clone,clone3,wait4 -o e2_popen_strace.txt ./e2_popen
23686 pipe2([3, 4], O_CLOEXEC)          = 0
23686 clone3({flags=CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND, exit_signal=SIGCHLD, stack=0x77470d8e6000, stack_size=0x9000}, 88 <unfinished ...>
23687 dup2(4, 1)                        = 1
23687 execve("/bin/sh", ["sh", "-c", "--", "echo popen-hello-from-shell; una"...], 0x7ffe29262838 /* 69 vars */ <unfinished ...>
```

教科书里的讲法是 fork+dup2+exec。您再细看上面的跟踪,每一步的位置都对得上,主角却换掉了:创建进程的那一步是 clone3,带着的是 CLONE_VM 和 CLONE_VFORK 两个标志,这是 posix_spawn 这一系的特征:子进程暂时借用父进程的地址空间,exec 一完成就还了回去,省掉了整份页表拷贝。dup2 和 execve 的调用一样不少,只是干活的路径从 fork 换掉了。

> 什么时候换的,咱们查到了落款:glibc 2.29 的 NEWS 写明 popen 与 system 不再执行 pthread_atfork 处理器(glibc 的 Bugzilla 编号 17490),这正是实现换成 posix_spawn 的可见后果。此后的 strace 里 fork 再没出现过,早期是一路的 clone(CLONE_VM|CLONE_VFORK),新的 glibc 在内核支持时走 clone3,笔者的 2.44 上看到的就是上面那行 clone3。对写应用的人来说语义是等价的,可您拿 strace 排查问题的时候,您的心理预期得更新,别再等一个永远不来的 fork。

## FIFO:按路径名相会

管道的两端靠 fork 继承,不相干的两个进程没有这层关系,咱们走的第二条路,是给管道起名字:mkfifo 在目录里立的是一个特殊文件,它的类型位是 p,咱们谁都能按路径 open 它。E3a 让牵引脚本把两个进程分别拉了起来(两边毫无亲缘、存档输出里自称 orchestrator 的就是它),走了一遭:

```text
$ ./e3_fifo_meet
mkfifo("/home/charliechen/lp03_scratch/e3_meet.fifo", 0666) 完成,st_mode=010644 → S_ISFIFO=true(类型位是 p)
  prw-r--r-- 1 charliechen charliechen 0 Oct  4 11:06 /home/charliechen/lp03_scratch/e3_meet.fifo
[读端 pid=23563] open(O_RDONLY) ……(此刻还没有任何写者)
[读端 pid=23563] open 返回 fd=3,干等了 801.1 ms——写者出现了
[写端 pid=23564] 写 73 字节后关闭,退出
[读端 pid=23563] EOF,共 73 字节,开头是:「hello,我是跟读端毫无亲缘的写者,咱们只认识这条路径名」
orchestrator:两个 exec 出来的进程(pid 23563 / 23564)完成了这次相会,总耗时 802.4 ms;unlink 收尾
```

输出里咱们要看两个点。头一个点跟权限有关:请求的 mode 是 0666,落进目录的却是 0644,被 umask 0022 截掉了两位,和 Lmem03 里 shm_open 的遭遇一模一样,您想给足权限就得在创建前把 umask 顶回去。咱们要看的另一个点,是 open 自己也会等:读端来得早的时候,它的 open(O_RDONLY) 就在原地干等,801.1 ms 之后写端出现了,两边就一起放行了。写端来得早的时候也一样要等,方向的另一半在 E3b 里实测互等了各约 500.1 ms。内核把会合做进了 open 调用里,两个进程就不需要任何额外的握手了。

咱们在同一个 E3b 里还把 O_NONBLOCK 下的 open 语义量了出来,里面藏着一个反直觉的陷阱:

```text
$ ./e3_open_semantics
== A. 读端先开:open(O_RDONLY) 阻塞直到写者出现 ==
[ 500.3 ms] 父进程:睡满 500 ms 后才 open(O_WRONLY) fd=3——对面立刻放行
[ 500.3 ms] 子进程:O_RDONLY 返回 fd=3(等了 500.1 ms)

== B. 写端先开:open(O_WRONLY) 阻塞直到读者出现 ==
[1001.1 ms] 父进程:睡满 500 ms 后才 open(O_RDONLY) fd=3——双向都是 open 在等对面
[1001.1 ms] 子进程:O_WRONLY 返回 fd=3(等了 500.1 ms)

== C. O_NONBLOCK:写侧 ENXIO / 读侧「成功但 read=0」的 EOF 陷阱 ==
open(O_WRONLY|O_NONBLOCK)(没有读者):返回 -1,errno=6 (No such device or address)
open(O_RDONLY|O_NONBLOCK)(没有写者):返回 3——注意,成功了!没有像写侧那样报错
紧接着 read():返回 0——不是 EAGAIN,是 0(EOF 语义)。非阻塞读 FIFO 的经典陷阱:...
```

C 段的两行要放在一起看。写侧的表现很干脆,没有读者,open 直接回的就是 ENXIO(errno 6)。读侧却成功了,紧跟着 read 就返回了 0。麻烦出在语义的分歧上:从来没有写者出现过,和写者来过又全关了,两种情况表现出来的都是 read 返 0,咱们在代码里是分不开它们的。更要命的是 poll 一类的接口上,readable 永远是真的,事件循环也就此空转烧起了 CPU。工程上常用的解法,咱们拿 O_RDWR 打开读端,自己占住一个写者的名额,EOF 就永远不会出现了,或者咱们干脆在状态机里把 0 当成普通事件处理。存档里的 D 段还有一条:mkfifo 给了 0400 之后,属主自己以写方式 open 也吃了 EACCES(errno 13),FIFO 是目录里的实体,权限位是照常参与检查的,可不是做样子的。附带的实测还有一条好消息:FIFO 的 F_GETPIPE_SZ 也是 65536,它和匿名管道用的是同一套内核缓冲,只是多了个目录项当名字。

### PIPE_BUF:多写者的原子线

咱们让两个写者同时往一条通道里灌记录,读者怎么知道一条消息没被别人写进来一半?衡量这件事的界线,是一个叫 PIPE_BUF 的常量:它在 linux/limits.h 里,Linux 取的是 4096 字节。man 7 pipe 给的保证分两截:单次 write 的字节数小于 PIPE_BUF,内核保证它不与别的写者交错。大于 PIPE_BUF 的写,内核的承诺就只剩下可以交错,而恰好等于 4096 的这一档 man 没有写死,咱们的实测把它补上了。E3c 搭的就是这个场景:两个并发的写者加一个读者,两种记录的尺寸,各测了阻塞与非阻塞两版,==4096 的两组咱们跑了多轮,撕裂的窗口全为零:

```text
$ ./e3_pipebuf_atomic
PIPE_BUF = 4096(linux/limits.h):≤ 它的写入内核保证不与别的写者交错,> 它只是「可以」交错

== 记录 4096(== PIPE_BUF) 阻塞写者,两个写者各 64 条 ==
  读者:完整窗口 128(期望 128),其中撕裂窗口 0

== 记录 4096(== PIPE_BUF) O_NONBLOCK 写者,两个写者各 64 条 ==
  读者:完整窗口 128(期望 128),其中撕裂窗口 0

== 记录 8192(> PIPE_BUF) 阻塞写者,两个写者各 128 条 ==
  读者:完整窗口 256(期望 256),其中撕裂窗口 33

== 记录 8192(> PIPE_BUF) O_NONBLOCK 写者(部分写循环),两个写者各 128 条 ==
  读者:完整窗口 256(期望 256),其中撕裂窗口 0
```

4096 的那两组,撕裂的窗口是零,阻塞与非阻塞的表现都一样,内核把不超过 PIPE_BUF 的单次 write 当成了不可分单元,保证兑现了。8192 的两组就微妙了:阻塞版撕出了 33 个窗口,非阻塞版这一轮却撕出了 0 个,而存档里的多轮重跑,窗口数逐轮大幅地浮动,有过 0 的轮次,也有过近百的轮次,它是概率性的。机制藏在内核 fs/pipe.c 的 pipe_write 里:拷贝是按页进行的,写满了就放锁去睡,醒来的时候只认有槽就填,另一个写者的页就插进了记录中间。非阻塞版那轮的 0 撕裂也别高兴太早,交错只是被释放粒度掩盖了:读者按整条记录(8192)为单位释放空间时,被唤醒的写者往往一口气吃完全部空槽,交错就显不出来了。存档的注记里还记了一轮:把 read 的粒度降到 4096、按页释放之后,交错立刻显了形。所以多写者场景的安全线只有一条:单条的消息别超过 PIPE_BUF,再大就得自己上应用层的分割与同步,或者咱们干脆换通道。

## POSIX 消息队列:边界与 prio 都在

字节流的边界要自己做,这是 pipe 和 FIFO 共同的脾气:发送方 write 三次,接收方的一次 read 可能拿到一条半,也可能拿到三条的拼团。POSIX 消息队列(下面简称 mq)把这个活儿收进了内核:一次 send 发的是一条,一次 receive 收的也是一条,长度就是发送时的长度。mq_open 的名字规则和 shm_open 是同款的:名字的开头是一个斜杠,中间的部分不许再有第二个斜杠,总长不超过 NAME_MAX(255) 的上限。那实体落在哪?咱们拿 ls 看(E4a):

```text
$ ./e4_mq_basics
mq_open("/lp03_e4") → mqd=3;命名对象落在哪?看目录:
  [ls -l /dev/mqueue/]
-rw------- 1 charliechen charliechen 80 Oct  4 11:06 lp03_e4
mq_getattr:mq_maxmsg=10 mq_msgsize=256 mq_curmsgs=0 mq_flags=0
```

咱们看到的名字空间,就是 mqueue 文件系统的挂载点 `/dev/mqueue`,和 shm 的 `/dev/shm` 同构,咱们 ls 看得见,而 rm 也删得掉。有个小地方提醒您别误会:目录项的 size 列是 80,那是内核在 mqueue 文件系统里写死的固定值(内核源码里的 FILENT_SIZE 常量),不代表消息的字节。mq_attr 是 mq_open 的第四个参数,里面装的是一份属性结构,四个字段各管一摊:mq_maxmsg 管的是队列深(最多同时压几条),mq_msgsize 管的是单条上限,mq_curmsgs 报的是当前条数,mq_flags 里藏着的还有 O_NONBLOCK。非特权的天花板低得出乎意料,咱们在 `/proc/sys/fs/mqueue` 里读到的 msg_max=10、msgsize_max=8192,正是 man 7 mq_overview 写的默认值,咱们想把队列开深一点,不带 CAP_SYS_RESOURCE 的场合,mq_open 就会直接拒绝创建的请求。

边界和 prio 的实测,咱们只要一小段代码(prio 是无符号的整数,Linux 给到的是 0..32767):

```cpp
// e4_mq_basics.cpp(节选,send_msg 的包装体略去):按 prio 1/3/2 发三条不同长度的消息
// send_msg 的参数依次是:prio、字节数、填充字符、消息 id
send_msg(mq, 1, 10, 'x', "M1");
send_msg(mq, 3, 50, 'y', "M2");
send_msg(mq, 2, 200, 'z', "M3");
char buf[512];             // 注意:mq_receive 的缓冲区必须 ≥ 队列的 mq_msgsize,否则 EINVAL
unsigned prio = 0;
ssize_t n = mq_receive(mq, buf, sizeof buf, &prio);
```

```text
按 prio 1 → 3 → 2 的顺序发三条不同长度的消息:
  发送 prio=1 长度=10 字节(id=M1,填充字符 'x')
  发送 prio=3 长度=50 字节(id=M2,填充字符 'y')
  发送 prio=2 长度=200 字节(id=M3,填充字符 'z')
  mq_curmsgs=3(排队 3 条)

连收三次(mq_receive 带 prio 出参,返回值就是本条字节数):
  第1次收到 prio=3 长度=50 字节(id=M2)——长度与发送时一字不差,边界还在,内容校验通过
  第2次收到 prio=2 长度=200 字节(id=M3)——长度与发送时一字不差,边界还在,内容校验通过
  第3次收到 prio=1 长度=10 字节(id=M1)——长度与发送时一字不差,边界还在,内容校验通过

同 prio=5 连发两条,验证同优先级内按发送序:
  先收到(8 字节):「first-in」
  后收到(9 字节):「second-in」——同优先级内 FIFO;「跳队」只发生在跨优先级之间
```

发送的次序是 1、3、2,收到的次序却是 3、2、1:prio 大的排在前面出队,跳队的事只发生在 prio 不同的消息之间。prio 相同的两条,进出的次序是一致的。长度一栏是另一份主证据:您发出去多少,收回来的就是多少,这就是分毫不差的消息边界,是字节流给不了的东西。咱们再把同一批消息换 pipe 走一遍(E4b),对照是这样的:

```text
$ ./e4_mq_vs_pipe
[mq] 连收三次,每次要 256 字节的空间,得到的却是发送时的长度:
  第1条:10 字节 → [1111111111]
  第2条:50 字节 → [22222222222222222222222222222222222222222222222222]
  第3条:200 字节 → [33333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333333]
[pipe] 读者每次 read 要 64 字节(谁也没规定消息边界,只能按字节收):
  第1次 read:64 字节 → [1111111111222222222222222222222222222222222222222222222222223333]
  第2次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第3次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第4次 read:64 字节 → [3333333333333333333333333333333333333333333333333333333333333333]
  第5次 read:4 字节 → [3333]
```

pipe 的第一次 read,就把消息 1、消息 2 和消息 3 的开头拼成了一团:谁也没规定边界,它当然按字节给。您想用 pipe 传消息,拆包的活儿全落在用户侧:定长、长度前缀、分隔符,咱们三选一自己写,还要自己处理粘包与半包的问题。mq 把边界白送了,代价咱们到吞吐那段见。收之前还有一条使用须知:mq_receive 的缓冲区容量必须不小于队列的 mq_msgsize,否则它就直接 EINVAL 了,实验代码里遇到过这一下才补的注释。它和 read 的要多少给多少是反着的,容量声明小了,连拒收都不带商量的。

### mq_notify:注册一次,响一声

mq 还有一样 pipe 没有的本事:队列从空变非空的那一刻,内核能主动通知您,轮询都省了。注册靠的是 mq_notify,E4c 走的信号方式:

```text
$ ./e4_mq_notify
mq_notify 已注册(SIGEV_SIGNAL/SIGUSR1,sigev_value=0xC0DE),fork 子进程 500 ms 后发消息……
信号到了:si_code=-3(SI_MESGQ,消息队列专属来源),sigev_value 带回 0xc0de——注册时塞的私货原样返回
收到本条:wake-1(mq_curmsgs 消费前=1)
等子进程发第二条(不重新注册)……
第二条到货了吗?mq_curmsgs=1;信号又来过吗:没有——mq_notify 的注册是一次性的,消费即失效
补收第二条:wake-2;要继续被通知,得再调一次 mq_notify(或在收货线程里重新武装)
```

咱们看两处。信号到的时候,siginfo 里的 si_code 报的是 -3、也就是 SI_MESGQ,这个标记的意思,是消息队列专属的来源,您在信号篇里找不到它,man 页与存档就是它的全部出处。注册时塞进 sigev_value 的 0xC0DE,也原样随信号带了回来,您可以用它分辨通知来自哪个队列(si_code 的常见来源,信号篇的上、下两篇里给到)。另一处是要害:注册是一次性的,mq_notify(3) 的 man 页原话是 notification occurs once,第一条消息把注册消耗掉了之后,第二条消息明明到货了(mq_curmsgs=1 作证),但是信号没有再来。您想持续被通知,您收一条就得重新 mq_notify 一次,惯用的做法是把它放进收货路径的开头、读完队列再武装,和信号篇要讲的 self-pipe(信号处理器只往管道里写一个字节,剩下的事主循环从管道里收)是一类思路。

### 吞吐:mq 对 pipe、shm 的同一口径对照

白送的东西值多少钱?咱们让 E4d 沿用 Lmem03 的那套口径,负载是 1 MiB 的体量,逐块地校验,3 轮取的中位。

```text
$ bash run_e4.sh
== E4d 同一 1 MiB 走 POSIX 消息队列(maxmsg=10,满仓阻塞),各 3 轮 ==
== 参照 Lmem03 E5 同机数字:shm SPSC 环形中位 7304 MiB/s、pipe 1024B 块中位 2349 MiB/s ==

--- 1024 B × 1024 条(与 Lmem03 的分块完全一致) ---
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.825 ms →   1213 MiB/s
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.803 ms →   1245 MiB/s
mq     1 MiB(1024 B × 1024 条,队列深 10):校验通过 耗时   0.700 ms →   1429 MiB/s

--- 8192 B × 128 条(贴着 msgsize_max=8192 的上限,减少系统调用次数) ---
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.279 ms →   3579 MiB/s
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.315 ms →   3179 MiB/s
mq     1 MiB(8192 B × 128 条,队列深 10):校验通过 耗时   0.401 ms →   2491 MiB/s

--- 同尺寸对照:pipe 8192 B × 128 块(与 Lmem03 e5 同构,只换块大小) ---
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.183 ms →   5459 MiB/s
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.210 ms →   4753 MiB/s
pipe   1 MiB(8192 B × 128 块):校验通过 耗时   0.161 ms →   6226 MiB/s
```

咱们把中位数摆开。1024 字节的口径下,shm 是 7304、pipe 是 2349(两个都是 Lmem03 的引用,咱们不重测),mq 的中位是 1245,只有 pipe 的一半多一点。8192 字节的口径下,mq 的 3179 对 pipe 的 4753,差距缩到了一半以内。咱们读出两条规律:同尺寸下 mq 恒慢,因为每条消息都要在内核的 prio 队列里多走一趟排序,系统调用的次数还一条没少。消息越大的时候差距越小,因为每条消息摊到的系统调用变少了,固定开销被摊薄了。再念一遍开头的口径:WSL2 逐轮 ±40%,这些绝对值咱们只报量级,shm ≫ pipe ≥ mq 的排序,是在全部轮次里稳定的。

## POSIX 信号量:给通道配名额

信号量搬运的不是数据,它维护的只是一个计数,但配通道的时候少不了它:管道的深度、队列的名额、同时放几个进程进场,管事的都是它。命名版的 sem_open 和 mq 一样靠名字跨进程,匿名版的 sem_init 配上 pshared=1,贴着共享内存的映射走。E5a 是一个名额实验:咱们给的初值是 2,也就是两个名额的配置,三个 exec 出来的 worker 按 150 ms 的梯次进场:

```text
$ ./e5_sem_slots
命名信号量 /lp03_sem 已建:初值=2(两个名额)。三个 exec 出来的 worker 进场:
[worker0 pid=23751 @1854010.6] 尝试 sem_wait(此刻 sem_getvalue=2)
[worker0 pid=23751 @1854010.6] 拿到名额!sem_wait 等了    0.0 ms(剩余名额 sem_getvalue=1)
[worker1 pid=23752 @1854160.8] 尝试 sem_wait(此刻 sem_getvalue=1)
[worker1 pid=23752 @1854160.8] 拿到名额!sem_wait 等了    0.0 ms(剩余名额 sem_getvalue=0)
[worker2 pid=23753 @1854311.3] 尝试 sem_wait(此刻 sem_getvalue=0)
[worker0 pid=23751 @1855010.8] sem_post 放行(名额回到 1)
[worker2 pid=23753 @1855010.8] 拿到名额!sem_wait 等了  699.4 ms(剩余名额 sem_getvalue=0)
[worker1 pid=23752 @1855161.0] sem_post 放行(名额回到 1)
[worker2 pid=23753 @1856010.9] sem_post 放行(名额回到 2)
```

时间线咱们读一遍。worker0 和 worker1 到的时候名额还在,sem_wait 零等待直接入场了。worker2 踩着第三梯进来的时候,计数已经见了底,它在 sem_wait 里干等的时长一次就是 699.4 ms,直到 worker0 干完了活,它的 sem_post 把名额放了行,它才立刻接上了棒。全部结束后 sem_getvalue 回到了 2,咱们的名额一个没丢。和 Lmem03 里那把互斥锁对照着记:互斥锁是初值 1 的特例,同一时刻放的只有一个。计数信号量管的是并发度,N 个名额放的是 N 个进程,限流、连接池的并发数,管它们的都是这个计数。

E5b 的三件杂事也顺手交代。头一件是带超时的等待:`sem_timedwait` 给 300 ms,到点回的就是 -1 加 ETIMEDOUT(errno 110),实测等了 300.1 ms,您就不用自己拼 alarm 加信号了。第二件是匿名版的跨进程:sem_init 的 pshared 给 1,放进 MAP_SHARED|MAP_ANONYMOUS 的映射,fork 之后父子就共用同一个计数器了,实测父进程等子进程的 post 等了 300.1 ms,和 Lmem03 竞态实验用的是同一招。第三件是 unlink 的生命周期,和 shm_unlink 用的是同一个模子(存档里还有一段 sem_getvalue 的观察咱们略过,块头的 4 号就是存档的分段号):

```text
== 4. unlink 生命周期:旧句柄照用,新名字从头来(与 shm_unlink 同构) ==
unlink 前旧句柄值=1;执行 sem_unlink……
旧句柄 post 后=2;新句柄(初值 7)=7——两个实体互不相干,名字已经归新实体
```

sem_unlink 删的是名字。旧句柄还攥着实体的引用,它的 post 照用、计数也照涨。名字空了之后,同名的 sem_open 建出来的是初值 7 的新实体,两边是互不相干的。Lmem03 在 shm_unlink 上量过一模一样的行为,咱们把那边量出的东西原样搬过来,就不用再证一遍了。

## SCM_RIGHTS:把 fd 本身递过去

Lmem03 在中段讲两条途径的地方带过一段:fd 本身也能当信物,unix 域套接字的 sendmsg 配上 SCM_RIGHTS,能把一个已打开的 fd 直接递给另一个进程。那边只让渡了一段,咱们在这里把它讲完整。SCM_RIGHTS 是附属数据(control message)的一种类型:sendmsg 的主载荷照常装字节,附属的缓冲里另外装的是 fd,recvmsg 的时候,内核会把这个 fd 复制进接收方的 fd 表。man 7 unix 的说法很直白,等效于咱们拿 dup 把 fd 复制进另一个进程的 fd 表,编号本身没有跨进程的意义,传过去的是对打开文件描述的引用。什么时候用它?要传的东西其实是访问权而不是一批字节:一个已打开的设备、一条连接、一段不想再拷一遍的内存(memfd_create 建的匿名内存配它正合适),您要是把内容读出来再写回去,就白费了。发送方的核心代码只有这几行:

```cpp
// e6_scm_rights.cpp(节选):sendmsg 的附属数据里装 fd,主载荷只带一个字节
void send_fd(int sock, int fd)
{
    char flag = 'F';
    struct iovec iov { &flag, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    struct msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof cbuf;
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    sys_call("sendmsg", sendmsg, sock, &msg, 0);
}
```

咱们在接收方一侧拿到的证据链,E6 的输出一气呵成,证人是 `/proc/self/fdinfo` 里的 pos 字段(内核记的文件偏移):

```text
$ ./e6_scm_rights
== 路径三:SCM_RIGHTS 传递(socketpair + sendmsg 附属数据) ==
[发送方] 读 16 字节:「LINE-0-012345678」→ fdinfo pos=16
[发送方] SCM_RIGHTS 已发(fd=3 装进附属数据),等接收方确认共享 pos……
[接收方 pid=23799] recvmsg 拿到 fd=4(发送方那边它叫 fd=3,编号是各自的)
[接收方] 它的 pos=16——和发送方共享偏移!接着读:
[接收方] 读 16 字节:「9ab
LINE-1-01234」→ fdinfo pos=32
[发送方] close(fd=3)——原始 fd 关了
[接收方] 收到信号:发送方已 close 原始 fd。继续读这个 fd:
[接收方] 读 24 字节:「56789ab
LINE-2-012345678」→ fdinfo pos=56
[接收方] 照样读得到——fd 表项是复制出来的新引用,struct file 的引用计数没归零,谁都没「移交」

== 获得一个 fd 的三条路径 ==
| 路径         | fd 编号        | 打开文件描述      | pos     | 发送方 close 后 |
|--------------|----------------|-------------------|---------|-----------------|
| open()       | 本进程新分配    | 全新的一份         | 从 0 起  | ——              |
| fork 继承    | 编号原样复制    | 与父进程同一份     | 共享     | 子进程照用       |
| SCM_RIGHTS   | 接收方新分配    | 与发送方同一份     | 共享     | 接收方照用       |
```

三处证据咱们分别看。同一个 fd 到了两边,发送方手里的编号是 fd=3,接收方拿到的是 fd=4,两边的 fd 表各排各的,谁也不沾谁的光。pos 是共享最直接的证据:发送方读到 16 停了手,接收方接手时的起点,也是发送方离开的 16,一路读到了 32,只有同一份打开文件描述才有这样的表现,和 fork 继承的 fd 是一个脾气。最有说服力的是第三处:发送方 close 了原始 fd,接收方照样从 32 读到了 56,内核里 struct file 的引用计数没有归零,实体还活得好好的。所以语义是复制引用,接收方靠内核的引用计数活着,移交这回事是不存在的:发送方关自己的 fd,接收方用别人的 fd,谁也不影响谁的日子。末尾的表格把三条获得 fd 的路径收在了一处:open 造的是全新的一份,fork 与 SCM_RIGHTS 拿到的都是既有描述的引用,而 SCM_RIGHTS 不需要亲缘,这正是它比 fork 多出来的本事。memfd_create 的匿名内存配上它连名字都不用起,Lmem03 说的 fd 到了实体就到了,证据链到这里就闭合了。

## 六条通道怎么选

咱们把 E1 到 E6 走完,手里的通道摆上一张表,收尾的 E7 就是选型本身。总纲一句话:头一个要问的,是数据要不要过内核的问题。第二个要问的,是两边的关系。

| 通道 | 两边关系 | 数据形态 | 同步与边界 | 方向 |
|---|---|---|---|---|
| pipe(E1/E2) | 只认亲缘,fd 靠 fork 继承 | 字节流,默认 64 KiB,F_SETPIPE_SZ 可调 | 流控自带:写满阻塞(实测 65536 整)、读空等待、EOF 与 SIGPIPE | 单向,双向开两根 |
| FIFO(E3) | 无关进程,按路径名相会 | 字节流,与 pipe 同一套内核缓冲(实测同 64 KiB) | 同 pipe;多写者时单条 ≤PIPE_BUF 才保原子 | 单向 |
| POSIX mq(E4) | 无关进程,按 /dev/mqueue 的名字 | 定长消息,边界与 prio 保留 | 满/空阻塞自带,mq_notify 主动递信号(一次性) | 单向 |
| POSIX sem(E5) | 命名跨进程,或匿名贴共享内存 | 不承载数据,只有计数 | 本身就是同步原语 | 不适用 |
| shm(Lmem03) | 无关进程,按 /dev/shm 的名字 | 裸内存,结构自定 | 同步全自理(Lmem03 有竞态实证) | 双向 |
| SCM_RIGHTS(E6) | 已连好的 unix 套接字两端 | 不传字节,传打开文件的引用 | 无缓冲,一寄一收 | 单向,反向再传一次 |

流量大、数据成块成流的热路径,shm 配无锁环形队列(Lmem03 量出的 7304 MiB/s 就是这么来的)是让数据不过内核的正解,代价是同步的活儿全自理。流量不大的场景、消息也小,pipe 和 mq 的余量都绰绰有余,挑一条顺手的就好。要消息边界、要 prio、要随时问一句队列里压了几条(mq_curmsgs),这几样 mq 都是白送的。咱们自己拆包不心疼的话,pipe 就连名字都不用起了。两边是跑一趟活儿的父子进程,pipe 起手就够了。进程之间不相干的话,咱们给 FIFO 或 mq 起个名字,要双向的话就上 mq 或者套接字,咱们别硬掰两根管道。要传的其实是访问权,SCM_RIGHTS 就是为此准备的,内容就不必读出来再写回去了。多个写者共用一条通道的场合,守住 E3c 量出的 PIPE_BUF 界线就够了。

带宽的对照,同机同口径、取的中位数,您选型的时候对一眼量级就够:

| 通道 | 粒度 | 中位 MiB/s | 出处 |
|---|---|---|---|
| shm SPSC 环形队列(单生产者单消费者) | 1024 B × 1024 | 7304 | Lmem03(引用) |
| pipe | 1024 B × 1024 | 2349 | Lmem03(引用) |
| POSIX mq(队列深 10) | 1024 B × 1024 | 1245 | 本篇 E4d |
| POSIX mq(队列深 10) | 8192 B × 128 | 3179 | 本篇 E4d |
| pipe | 8192 B × 128 | 4753 | 本篇 E4d |

还是开头的口径:WSL2 逐轮 ±40%,绝对值咱们只供量级,shm ≫ pipe ≥ mq 的排序在全部轮次稳定,消息越大的时候,mq 与 pipe 的差距就越小。

## 另一侧怎么看

Windows 那边没有 POSIX 这套 pipe 配 fork 继承的用法,匿名管道的 CreatePipe 只在亲缘进程间配合句柄继承用,跨进程的常驻通道是命名管道,建它的调用是 CreateNamedPipeW,名字挂在 `\\.\pipe\` 的名下,客户端拿的也是名字,走 CreateFileW 就连上来了。它比 FIFO 多了两样本事:头一样是双向的读写,第二样是按消息成帧的传输,拆包的活儿在内核侧就做掉了。邮槽是单向小消息的广播通道,一个写者一次写给多个读者,量级上更接近 mq 的用法。POSIX 信号量那边有对应的 Semaphore 对象,咱们要用的也都有。fd 的传递在 Windows 没有直接的对应物,咱们的句柄要交给另一个进程,走的是 DuplicateHandle,由一个两边都握有对方进程句柄的中转者复制过去,不存在套接字附属数据这样统一的递交通道。句柄怎么挑着传给子进程,已落地的[进程与作业](../../windows/process/01-createprocess.md)篇,在 STARTUPINFOEX 的句柄白名单里实测过。等待侧的 WaitFor 家族,咱们在[控制台事件与 APC](../../windows/process/02-console-apc.md)篇里也用过。命名管道与邮槽的 Windows 侧,本卷就不展开了,留给平台抽象篇的对拍再收,您把本篇的六条通道记下来,到时候咱们一个个对号入座。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="pipe(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/pipe.7.html"
  />
  <ReferenceItem
    :id="2"
    title="fifo(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/fifo.7.html"
  />
  <ReferenceItem
    :id="3"
    title="fcntl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fcntl.2.html"
  />
  <ReferenceItem
    :id="4"
    title="popen(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/popen.3.html"
  />
  <ReferenceItem
    :id="5"
    title="mq_overview(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/mq_overview.7.html"
  />
  <ReferenceItem
    :id="6"
    title="mq_notify(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/mq_notify.3.html"
  />
  <ReferenceItem
    :id="7"
    title="sem_overview(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/sem_overview.7.html"
  />
  <ReferenceItem
    :id="8"
    title="unix(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/unix.7.html"
  />
</ReferenceCard>
