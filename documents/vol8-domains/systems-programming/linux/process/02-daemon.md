---
title: "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
description: "一个不想跟着终端一起死的进程怎么跟会话、资源限制打交道:setsid 让 pid==pgid==sid 三位一体、组长再调报 EPERM、kill(-pgid,SIGHUP) 组内三成员同一毫秒各收一条而组外父进程零接收。守护化经典步骤的变化链实测:setsid 后 TIOCGSID 变 ENOTTY 但 fd0 的 readlink 仍指旧 tty,描述符不会自动断,这正是重定向那一步的真正理由,fork#1 让 shell 提前收工、fork#2 让孙进程永远当不上会话长、open tty 再获控制终端的路就此断掉,chdir 与 umask 各管什么,daemon(0,0) 返回后仍是会话长的对照。close(pty master) 之后会话长被 SIGHUP 处决、前台组同毫秒各收一条,nohup 的 strace 全过程只有四件事加 exec、零 setsid,disown 与 setsid 的判据是 sid 等不等于自己 pid。rlimit 的 EMFILE、硬限单向阀门、RLIMIT_CPU 软 1 秒 SIGXCPU 可捕获到硬 2 秒 SIGKILL 退出码 137 的双段时序。environ 数组与字符串都住在 [stack]、putenv 不拷贝改缓冲区 getenv 跟着变的所有权陷阱、首次 setenv 后数组从栈搬到堆、execle 白名单与 execve 全量两条路。/proc 的 R/S/Z 三态与 cmdline 的 NUL 拼接收尾,WSL2 口径的孤儿收养者是 249 的 Relay,与 fork 篇互证"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 19
prerequisites:
  - "进程创建与生命周期:fork/exec/posix_spawn"
related:
  - "进程创建与生命周期:fork/exec/posix_spawn"
  - "信号(上):sigaction 与异步信号安全"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 守护进程、会话与环境:setsid、双 fork、rlimit 与 environ

[上一篇](./01-fork-exec.md)咱们把一个进程从生到退走了一遍,fork 怎么复制,exec 怎么换身体,僵尸怎么收尸,咱们都亲手做过。可那些实验里的进程,个个都是被 shell 抱在怀里跑的。您在终端里敲下一个命令,把它 fork 出来的正是 shell,从此它的生死就跟两样东西绑在一起了,一样是终端窗口背后的设备,另一样是 shell 自己的作业管理。这一篇咱们要讲的是不打算这么活的进程。守护进程(daemon 就是长期在后台提供服务的进程)打算活得比终端和登录会话都久,sshd 和 crond 就是这么活的,那它就得把自己从 shell 那儿继承来的每层关系逐项改写,顺带把最多能长多大、环境里带了什么,也一起看明白了。

继承来的关系咱们挨个认一下。会话(session)是最大的一层:您每开一个终端窗口、每登录一次,系统就为一整摊相关的进程建一个会话,shell 起来时进了这个会话,它之后 fork 出来的命令默认全进同一个。进程组(process group)是中间的那层,也是信号投递的单位,kill 拿到负数 pid 的时候,打的就是一整个组。组长和会话长是两重身份:pid 等于自己 pgid 的进程是组长,pid 等于自己 sid 的进程是会话长,后面您会看到 setsid 能不能成、终端死的时候谁被处决,全卡在这两重身份上了。控制终端(controlling terminal)是会话与终端设备的绑定,而一个会话至多只有一个,您按 Ctrl+C 或者把终端窗口关掉,信号都是从这层绑定进来的。守护化(daemonize)就是拿几个 API 把四层关系一项一项地换掉,咱们用实验把每一步前后的身份全部拍下来给您看。

实验的编号是 E1 到 E6,与仓库 `code/volumn_codes/vol8/systems-programming/linux/process/02-daemon/` 下的 01 到 06 六个目录一一对应,代码连同全部原始输出都收在存档里了。正文里的输出块是节选,块内的删节另有标注,块首尾的裁剪您对表的时候以存档为准,E 系的效力只认本篇,内存那一章、文件 I/O 那一章各自的 E 系与咱们互不相干。这一篇的实验代码是裸 POSIX 调用,前面几篇的 [unique_fd](../../thinking/01-raii-paradigm.md) 这回没派上用场,出错了咱们就直接读返回值和 errno。信号 handler 里只允许裸 write 的纪律,最早是[mmap 内存映射篇](../file-io/02-mmap-memory-mapping.md)量 SIGBUS 时立下的,而[虚拟内存 API 篇](../memory/02-vm-apis.md)沿用到了今天,咱们本篇照旧。

环境口径咱们照例交代清楚,后面的数字都拿它对表:WSL2,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,编译的口径一律 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,`ulimit -n` 给的数是 1048576,而且软硬相同,工具 strace 用的是 7.2。有两条环境事实直接影响您复跑。第一条说的是 PID 1:本机的 `/proc/1/comm` 读出来是 systemd,因为 `/etc/wsl.conf` 写了 `[boot] systemd=true`,可孤儿进程的收养者实测并不是它,上一篇 e4 的孤儿,ppid 落在了 249 身上,那是 WSL 给每个登录会话配的 `/init` 中继进程,comm 记的是 `Relay(252)`,它用 `prctl(PR_SET_CHILD_SUBREAPER)` 把自己标记成了收养候选(subreaper:内核给孤儿找新爹的时候,从它的长辈里挑最近的 subreaper,一个都找不到的时候,兜底的才是 PID 1)。本篇 E2 的孙进程 getppid() 同样是 249,两篇的输出正好互为印证。另一条说的是咱们的实验台:它本身没有 tty,fd0 接的是管道,凡是需要真终端的实验,咱们都拿 `script -qec '...' /dev/null` 借一个 pty(伪终端,一对成团的假终端设备)来跑,它的正式介绍放在 E3。

## E1:会话与进程组:setpgid 只改一列,组长 setsid 报 EPERM

fork 出来的孩子默认住进父亲的进程组和会话,这句话咱们拿一张带四列身份的 ps 表落实。实验摆了三辈:本进程、子A、孙。子A 出生后调 `setpgid(0, 0)` 给自己换了个组,孙直接调 `setsid()` 换了会话:

```text
    PID    PPID    PGID     SID COMMAND
  13030    2318   13030   13030 zsh
  13049   13030   13030   13030 e1_sessions
  13050   13049   13050   13030 e1_sessions
  13051   13050   13051   13051 e1_sessions
```

咱们顺着 PGID 和 SID 两列看。子A 只有 PGID 这一列离开了父亲,而 SID 留在原地,换掉的只是组,会话还是原来的。孙的两列都变成了自己的 pid 13051:`setsid()` 同时干了三件事,新会话和新进程组一次就建好了,然后把自己一个人放了进去,man 2 setsid 的说法是调用者成为新会话的会话长兼新组长,而且是新会话里唯一的进程。API 的读数 getsid/getpgid 跟 ps 四列逐项吻合,而且它们对任意进程都可用,实验里咱们拿 `getsid(1)` 问了一句 PID 1,回的是 1 自己。

有身份的进程再想换,门口就被拦下了。实验里吃到了两个 EPERM,咱们各看一眼:

```text
子A(现组长,pid==pgid): setsid() = -1, errno=1 = Operation not permitted
孙(已是会话长): setsid() = -1, errno=1 = Operation not permitted
```

组长被拒的道理其实藏在编号上:setsid 要把新会话和新进程组的编号都设成自己的 pid,而组长自己的 pid 已经被旧进程组占用着当编号了,一个编号可标识不了两个组。孙那个 EPERM 是同一条限制的另一半:会话长必然是自己的组长,所以第二次 setsid 也过不去。这也解释了为什么教科书都让您 fork 之后再让子进程调 setsid:fork 出来的孩子跟父亲不在同一个组,它的 pid 没被任何组占用,当然永远够格。

进程组是信号投递的单位,这句口号咱们也实测。E1b 里咱们让组长带着两个组员立好,开火的差事交给组外的父亲:

```text
父进程: kill(-13171, SIGHUP) —— 负数 pid = 定向整个进程组 13171
[SIGHUP] pid=13173 t=+101ms
[SIGHUP] pid=13172 t=+101ms
[SIGHUP] pid=13171 t=+101ms
...
父进程: 枪响 300ms 后我什么都没收到(上面没有我的 pid)—— 组外不受影响
```

咱们一发 kill 打出去,组里的三个成员在同一毫秒各收到一条,组外的父亲等了 300ms 零接收。您平时按 Ctrl+C 能把管道上的 grep 和 wc 一起打断,靠的就是终端把 SIGINT 投给整个前台进程组,机制跟这次的 SIGHUP 同源。信号家族的完整讲法留给后面的信号篇,本篇咱们只借用 SIGHUP 这一样,因为它是 E3 的主角。

## E2:守护化的经典步骤:一条实测的变化链

教科书给守护化的流程,各本书的步骤表略有出入,骨架是一致的。咱们按存档的 e2_daemon_steps.cpp 原样跑一遍,在要紧的五个关口各拍一张快照,拍的是六样东西:身份的四样 pid、ppid、pgid、sid,再加 fd0 的指向和控制终端的有无。步骤和它们各自改的项,总表咱们摆在这儿:

| 步 | 动作 | 改写的是哪一项 |
|---|---|---|
| 1 | fork,父进程退出 | 进程的爹:交给收养者,shell 从此不等它 |
| 2 | setsid() | 会话与进程组:新建会话与进程组,自任会话长兼组长,切掉控制终端归属 |
| 3 | 再 fork,会话长退出 | 会话长身份:孙永远当不上,open tty 不会再被抓 |
| 4 | chdir("/") | 工作目录:不占着挂载点 |
| 5 | umask(0) | 新建文件的权限过滤码 |
| 6 | fd0/1/2 重定向 /dev/null | 三条标准流的去向 |

快照的后三样各自回答一个不一样的问题,咱们把判据说清楚。`readlink("/proc/self/fd/0")` 告诉咱们 fd0 这个描述符开着,而且指向哪个文件。`ioctl(fd0, TIOCGSID, &sid)` 告诉咱们这个 tty 认不认咱们所在会话是它的主人,按 man 2 ioctl_tty 的口径,fd 是个 tty、不是 pty 的 master 端、而且不是咱们的控制终端时,它回的就是 ENOTTY。pgid、sid 与 pid 的相等关系回答的则是身份。三条判据各管各的事,后面的对表全靠它们。

重头戏在 setsid 的前后两拍,原始输出咱们原样搬来:

```text
阶段0 初始                   pid=13247  ppid=13246  pgid=13246  sid=13246
    fd0 -> /dev/pts/10 ; 控制终端:有,该 tty 所属会话 sid=13246
...
阶段2 setsid 后               pid=13248  ppid=13247  pgid=13248  sid=13248
    fd0 -> /dev/pts/10 ; 控制终端:无,ioctl(fd0,TIOCGSID) 失败: Inappropriate ioctl for device
```

setsid 之前 pgid 和 sid 还是父亲的 13246,之后一起变成了自己的 13248,正是 E1 讲过的三位一体。真正值得咱们停下来的,是后两行:fd0 的 readlink 一点没变,指的还是 /dev/pts/10,而 TIOCGSID 却从成功变成了 ENOTTY。咱们把话说全:setsid 切断的是归属这层关系,而描述符本身一根都没动。您要是以为 setsid 之后进程就跟终端两清了,那可就想岔了,fd0 的读写一点不受影响,终端死的时候,持有它描述符的进程照样被卷进去。经典步骤里重定向 fd0/1/2 的那一步,真正的理由就在这儿:归属改了而描述符没动,断开这件事得咱们自己动手,那一步重定向干的就是这件事。重定向之后 `isatty(0)` 就归了零,咱们再读它,拿到的一直是 EOF,而再写,也就无声无息了。

整条变化链咱们并成一张表,五张快照都收进来了:

| 时点 | pid | ppid | pgid | sid | fd0 指向 | ioctl(fd0,TIOCGSID) |
|---|---|---|---|---|---|---|
| 0 初始(pty 里) | 13247 | 13246 | 13246 | 13246 | /dev/pts/10 | 成功,sid=13246 |
| 1 fork#1 后(子) | 13248 | 13247 | 13246 | 13246 | /dev/pts/10 | 成功(继承) |
| 2 setsid 后 | 13248 | 13247 变 249 | 13248 | 13248 | /dev/pts/10(没断) | ENOTTY(归属切断) |
| 3 fork#2 后(孙) | 13249 | 249 | 13248 | 13248 | /dev/pts/10 | ENOTTY |
| 6 重定向后 | 13249 | 249 | 13248 | 13248 | /dev/null | ENOTTY |

咱们逐步问下来,每一步都有它的理由。fork#1 之后父进程立刻退了出去,这一步赚的是 shell:shell 只等它亲手 fork 的那个父进程,父进程退了,shell 就认为命令结束了,提示符跟着就回来了,而服务在子进程里接着跑,生命周期从此和 shell 解耦了。父进程退出的副产品是孩子成了孤儿,实测的 getppid() 从 13247 变成了 249,收养者正是开头交代过的 Relay,教科书的说法是孤儿归 init,这个说法在本机要按 subreaper 的口径打折。

fork#2 防的隐患出在会话长这个身份上。setsid 之后孩子自己成了会话长,而会话长有一个别的进程没有的风险:按 System V 一系的终端语义,会话长 open 一个终端设备而没带 `O_NOCTTY`,那个终端就可能自动变成它的控制终端,刚挣脱的东西又给套回去了。于是咱们再 fork 一次,让会话长退了场,孙进程接手:孙的 sid 还是那个新会话,可它的 pid 不等于 sid,按 E1 讲过的身份判定,它当然永远当不上会话长,而会话长身份正是自动获得控制终端的前提,孙这边的前提已经不成立,时点 3 的快照里 TIOCGSID 的读数也依旧是 ENOTTY。chdir 到根目录的这一步,行的也是方便:守护进程要是把工作目录留在某个挂载点上,那个文件系统就卸载不成了。umask 的这一步,动的是那个过滤码:umask 是新建文件时的权限过滤码,open 的 mode 参数会经它削一遍才生效,shell 传下来的默认值 0022 会把组写和其他写削掉,守护进程想让文件权限完全由代码里写明的 mode 决定,实测把它从 0022 清到了 0000。

这套实验里有一个环节值得咱们单独交代,因为它差点把实验自己给搭了进去。E2 要的是真终端,咱们借 script 跑,而 script 在命令结束时会关掉自己借来的 pty,内核跟着就给这个终端的前台组发 SIGHUP,正是 E3 要讲的那个机制。咱们的父进程若是照教科书 fork 完立刻退,script 看命令一结束就关掉了终端的 master,而那时孩子可能还没来得及 setsid,半成品的守护进程会被这发 SIGHUP 顺手带走,trace 里只剩无声的消失。所以 e2 的父进程拿一根管道等孩子发来“setsid 已完成”的通知再退,存档的源码注释记了这段来历。真实 shell 的场景没有这个窗口:父进程退了,毕竟 shell 还活着嘛,终端也就不会跟着关掉了。实验装置成了它要观察的机制的活例子,这一口咬得笔者心服口服。

glibc 把这套流程包了一个 `daemon(nochdir, noclose)`。man 3 daemon 的 STANDARDS 一栏只写着 None,它是 4.4BSD 出身的老函数,glibc 把它留了下来。咱们用同款快照看它做了什么、没做什么:

```text
调用 daemon() 前          pid=13366  ppid=13365  pgid=13365  sid=13365
    fd0 -> /dev/pts/10 ; 控制终端:有,该 tty 所属会话 sid=13365
daemon() 返回后           pid=13367  ppid=249    pgid=13367  sid=13367
    fd0 -> /dev/null ; 控制终端:无,ioctl(fd0,TIOCGSID) 失败: Inappropriate ioctl for device
...
```

`daemon(0,0)` 一步把 fork、setsid、chdir 到 / 和重定向全办了,可返回之后的 pid==pgid==sid==13367,它还顶着会话长的头衔。man 页 BUGS 一节写得直白:这个实现没有用双 fork 的写法,得到的守护进程就是会话长,System V 的语义下 open 一个终端设备,还可能被抓回去当了控制终端。它什么信号都没屏蔽,而且 SIGHUP 也没处理,所以这些保险都得调用者自己加。跟咱们 E2 的手工双 fork 版一对照,fork#2 那一步的意义立刻显形:两边就差那一次 fork,手工版的 sid 不等于 pid,daemon 版的还等于 pid。

## E3:终端的死亡:SIGHUP 从哪来,nohup 做了什么

E2 反复说终端死了会发 SIGHUP,现在咱们把这句话本身验掉。咱们验它得靠 pty,环境口径里咱们提过它的名字,现在咱们把它正式认全。pty 的名字是 pseudoterminal,中文的名字是伪终端。pty 是一对成团的设备,咱们握住 master 一端,跑着的程序握住 slave 一端,而程序打开 slave 这头,用起来的手感跟真终端没有差别,您的终端窗口背后就是一个 pty,script、sshd 靠的也是它。

咱们把实验布置成父进程当旁观者:`openpty` 开一对 pty,孩子用 setsid 当了会话长,再调 `ioctl(sfd, TIOCSCTTY, 0)` 把 slave 认作自己的控制终端,这正是 E2 里被切断的那层绑定的正向操作,再把自己的组设为前台组,fork 两个装着 handler 的组员记时间戳。等都就绪了,父进程 `close(master)`:对挂在 slave 上的所有人来说,这就是终端死了。

```text
父 pid=13448(旁观者)开了一对 pty:master=4 slave=5
会话长 pid=13449: 控制终端=/dev/pts/10, sid=13449 pgid=13449(前台组)
...
[SIGHUP] pid=13451 t=+151ms
[SIGHUP] pid=13450 t=+151ms
父: waitpid(会话长) -> 被 Hangup 处决(缺省动作如约而至)
```

而会话长这边没装 handler,缺省动作把它处决了,咱们靠父进程的 waitpid 拿回了死因,报的正是 Hangup。前台组的两个组员在同一毫秒(t=+151ms)各收到一条,装了 handler 才留下了带时间戳的遗言。内核的行为是这样的:终端连接断开的时候,SIGHUP 发给的是控制进程,指的就是会话长本人,前台进程组的成员也各得一份。E1b 那发 kill(-pgid) 演的正是后半句。守护化非做不可的原因,到这儿就落地了:E2 时点 0 的那个进程,命就拴在终端上了,终端死的时候它就得陪着。

那不想守护化、只想让一条命令扛住挂断的人,拿什么?咱们请出 nohup。靠猜是不划算的,咱们拿 strace 把它的整个事件序列录下来看,开头那些 locale 文件的尝试都是噪音,值得看的就是这么几行:

```text
openat(AT_FDCWD, "/dev/null", O_WRONLY) = 3
dup2(3, 0)                              = 0
close(3)                                = 0
openat(AT_FDCWD, "nohup.out", O_WRONLY|O_CREAT|O_APPEND, 0600) = 3
dup2(3, 1)                              = 1
close(3)                                = 0
dup2(1, 2)                              = 2
rt_sigaction(SIGHUP, {sa_handler=SIG_IGN, ...}, {sa_handler=SIG_DFL, ...}, 8) = 0
...
execve("/usr/sbin/true", ["true"], 0x7ffffa195740 /* 69 vars */) = 0
```

它做的全部事情,咱们顺着 strace 念下来就这几样:stdin 接到了 /dev/null,stdout 改道到了 nohup.out,用的是追加式、0600 权限,stderr 跟的是 stdout,然后把 SIGHUP 的处置设成 SIG_IGN,末了 exec 的还是您给的目标命令。咱们翻遍全过程,setsid 和 setpgid 的影子一次都没找到,man 1 nohup 的文字里压根没有这两样。所以 nohup 起的进程还住在原来的会话和进程组里,终端死了 SIGHUP 照样来,只是 SIG_IGN 把它弹开了。咱们的验证也很直接:对 nohup 起的进程 kill -HUP,它活得好好的,而 stdout 早已落进 nohup.out 里了。它的适用面就清楚了:您只要扛挂断,nohup 就够了,而想真正换掉会话身份,就得靠 setsid 了。

shell 的 disown 是您手边的第三条路,它改的是 shell 自己的作业表:作业从表里划掉了,shell 退出时也就不再给它发 SIGHUP 了,可进程的 sid、pgid 一个没动。判据其实就一句话,看 sid 等不等于自己的 pid:

```text
    PID    PPID    PGID     SID COMMAND
  13950   13949   13947   13947 sleep
...
  13951   13949   13951   13951 sleep
```

您看第一行,普通的后台作业,sid 13947 是 shell 的会话,而进程本身留在原地。第二行是 setsid 启动的作业,sid 13951 正是它自己的 pid,会话真换了。disown 改的是 shell 的记录,setsid 换的是会话身份,两件事的分界就在这儿了。

## E4:rlimit:软限是警告,硬限是处决

身份改完了,咱们接着看进程背上剩下的东西:资源上限 rlimit 是什么。而每个进程都挂着一组 rlimit,`struct rlimit` 的字段就俩,rlim_cur 记的是当前生效的软限,rlim_max 记的是硬限,也就是软限的天花板,咱们用 `getrlimit` 和 `setrlimit` 一读一写。

NOFILE(能打开的文件描述符数)最直观,咱们把软限压到 4 再开文件:

```text
初始           rlim_cur=1048576    rlim_max=1048576
setrlimit 后    rlim_cur=4          rlim_max=1048576
软限=4 且 fd0/1/2 已占 3 个名额 -> 还能开 1 个(fd=3),之后:
  open #1 -> fd=3(成功)
  open #2 -> -1, errno=24 = Too many open files
  open #3 -> -1, errno=24 = Too many open files
```

三条标准流已经占了三个名额,第 4 个 open 还拿到了 fd=3,再往后就一律吃到 errno=24 的 EMFILE。按 man 2 getrlimit 的说法,越线的 open、pipe、dup 都报这个,文案说的是文件太多,而病根在限额这儿。软限本身是双向的,实验里咱们把它升回 1048576,一次就成功了。

硬限的脾气就硬多了,咱们四步探下来:

```text
① 软限升到硬限(1048576)        -> 成功
② 硬限升到 1048577(越权)      -> 失败 EPERM 如约:(errno=1 Operation not permitted)
③ 硬限降到 1024            -> 成功
④ 再升回 1048576(降过之后)   -> 失败 回不来了:(errno=1 Operation not permitted)
```

咱们把 man 的口径翻出来:软限在 0 到硬限的区间里随便调,而想把硬限抬高,手里没有 CAP_SYS_RESOURCE 能力的人,拿到手的就只有 EPERM,而降过之后再想抬回原值,吃到的同样是 EPERM,它是一道单向的阀门。所以守护进程的常见写法是趁启动早期把软限直接拉到硬限,拉满的窗口只开这一次,过了这村就没这店了。

RLIMIT_STACK 咱们单独拎出来看,因为它最容易被咱们读歪:

```text
getrlimit: rlim_cur=8388608 字节 (8192 KiB = 8.0 MiB), rlim_max=unlimited(RLIM_INFINITY)
maps [stack]: 7ffc2fa16000-7ffc2fa38000 rw-p 00000000 00:00 0                          [stack]
当前已映射大小 = 136 KiB(远小于软限,软限是"允许长到的上限")
```

软限的 8388608 字节换算下来恰好是 8 MiB,而 maps 里 [stack] 当时只映射了 136 KiB。两个数摆在了一起,软限的意思就清楚了:它说的是允许长到的上限,而不是已经占用。这也接上了[内存布局篇](../memory/01-memory-layout.md)讲过的行为,访问栈上未映射的地址不立刻 SIGSEGV,而内核会按需扩栈,扩到 rlim_cur 拒绝的那次访问才变成 SIGSEGV。单位记的是字节,笔者把它单写出来是有私心的:存档的头一版就把 8388608 字节读成了 KiB,算出 8192 MiB 的天大栈额,被复核抓了回来。咱们还得记一件事:这一项 rlimit 只管主线程的栈,多线程程序里其他线程的栈走 pthread_attr 的默认大小。

CPU 的双段时序是 E4 里最有戏的一幕。咱们把软限定 1 秒、硬限定 2 秒,然后进满载的循环:

```text
主循环: wall=+966ms cpu=+962ms SIGXCPU 已命中 0 次 sum=519999997400000000
[SIGXCPU] wall=+999ms cpu=+998ms (第 1 次命中;软限是警告,硬限 2s 将是 SIGKILL)
主循环: wall=+1003ms cpu=+1002ms SIGXCPU 已命中 1 次 sum=539999997300000000
...
./e4_cpu_run.sh: line 4: 14657 Killed                     ./e4_cpu
驱动观察: 退出码 137 —— 137 = 128+9,即被 SIGKILL 处决(硬限不可协商)
```

wall 到 +999ms 的时候,SIGXCPU 命中了第 1 次,咱们回头一看,handler 把日志记完了,进程还好好地活着,软限给的是警告窗口,SIGXCPU 是可以捕获、可以处理的。man 页还说了,超线之后每秒一次的补发也会跟上,可咱们的循环里它就命中了这 1 次,接着又跑满了一秒,wall 到了 2000ms 上下,SIGKILL 就到场了,咱们捕获不了它,也忽略不了它,进程退了场,退出码记的是 137,而 137 正好等于 128+9。您在机器上见过 `Killed` 字样的日志,多半就是它了。您可能纳闷 SIGXCPU 为何只命中一次,而答案就在时点上:第 2 次软限到期的时间恰好与硬限 2 秒重合,SIGKILL 和第 2 发 SIGXCPU 是同时到的,而后者根本没等到送达。想亲眼看重复的警告,您把硬限拉大到软限加 3 秒以上再跑。

## E5:环境变量:environ 住在栈上,putenv 不拷贝

身份与限额都验完了,进程从 shell 那儿整份继承的东西还剩最后一样:环境变量。main 的第三个参数 envp 和全局变量 `environ` 指的是同一份东西:一个以 NULL 收尾的 `char*` 数组,每一项都是一条 `NAME=VALUE` 形式的字符串。而它住在哪儿,咱们拿 maps 对表:

```text
environ 指针数组条目数 = 69
maps [stack]: 7fff3886b000-7fff3888d000 rw-p 00000000 00:00 0                          [stack]
environ 数组本身在     0x7fff3888ad98
environ[0] 字符串在    0x7fff3888b6ab
argv[0] 字符串在       0x7fff3888b69e
...
判定: environ 数组在 [stack] 内? 是 ; environ[0] 字符串在 [stack] 内? 是
```

数组和字符串全落在了 [stack] 区间里,argv[0] 和 environ[0] 的字符串相距才 13 字节。这是 exec 的时候内核亲手铺在栈顶的,上一篇的 exec 家族里,argv 和 envp 只是以参数的身份出场,而它们最终落在哪儿,这里您看到的,就是实测的地址。

putenv 的故事咱们从所有权讲起。所有权说的是一块内存归谁管、谁负责让它活着:

```text
putenv 陷阱(man 3 putenv: 字符串归环境所有,glibc 不做拷贝):
putenv(buf 内容 "E5_PUT=aaa") 后 getenv = aaa
只把 buf 改成 "E5_PUT=zzz"     后 getenv = zzz  <-- 环境直接引用了 buf
```

putenv 收的是指针,而不做拷贝,man 3 putenv 的原话 the string becomes part of the environment 说得很直白,字符串从此就归环境所有了。实测咱们 putenv 一段 buf,getenv 读到的是 aaa,环境咱们一个没动,只把 buf 的内容改成 zzz,getenv 就立刻跟着变成了 zzz,环境数组里那个指针指的就是咱们的 buf。推论也就顺出来了:传栈上的数组、传临时的缓冲区,函数一返回那块内存就没了,environ 里从此就挂了一个悬垂指针,下一个 getenv 读的就是已经回收的内存。setenv 倒是没有这个问题,拷贝这件事它自己包了,name 和 value 是分开传的,而且带 overwrite 开关,实验里 overwrite=0 时已存在的变量不动,给 1 的时候就覆盖了,行为都对上了。

setenv 还给咱们带来了一次搬家:

```text
setenv/putenv 折腾完: 条目数=70, environ 数组现在在 0x63a8d7c5f940
判定: 还在 [stack] 内? 否,已搬到堆上(第一次扩容时 libc 在堆上另建了数组)
```

咱们头一回 setenv 之后,数组从栈上(0x7fff 开头)搬到了堆上(0x63a8 开头),条目数从 69 变成了 70,而字符串本体还留在栈上。这是 glibc 的实现行为:扩容时它在堆上另建数组,而 POSIX 没承诺过这一点,别的 libc 可以做得不一样,咱们按本机口径记。

环境怎么传给孩子,exec 家族给了两条路,咱们两条路各走一遍:

```text
--- 路 1: execle + envp 白名单数组(子进程环境被整个替换) ---
E5_FROM=execle
PATH=/usr/bin:/bin

--- 路 2: execve + environ(子进程原样继承,能看到 E5_EXTRA) ---
...
E5_EXTRA=inherited-via-environ
```

execle 的 envp 是白名单,您给几条,新进程手里就只剩这几条了,连父进程的 HOME、TERM 都不在场,两条之外的世界整个被挡在门外。而 execve 配 environ 走的是全量继承,70 条一条不落地全继承了下去,连刚 setenv 的 E5_EXTRA 也在。两条路各有各的用处:您要构造沙箱、要清洗环境的时候,走的是路 1,普通的传递,走的是路 2。

## E6:/proc 速览:R/S/Z 三态与 cmdline

身份、限额、环境都讲过了,收尾咱们把 /proc 这个观察位过一遍。本系列的[内存布局篇](../memory/01-memory-layout.md)拿 /proc/pid/maps 画过地址空间全图,这一篇补的是进程本身的几个文件。

咱们把同一个二进制抓拍到三种状态:

```text
[self status(此刻在运行,State 应为 R)]
State:	R (running)
...
[子进程活着(在 sleep): State 应为 S(可中断睡眠)]
State:	S (sleeping)
...
[子进程已死未收尸(waitpid 前): State 应为 Z(僵尸)]
State:	Z (zombie)
```

R 说的是正在 CPU 上跑,S 说的是可中断的睡眠,Z 说的是僵尸,死了还没人收尸的那一小段。三张照片拍的是同一份程序的两个进程:第一张的 R 是父进程自己,后两张的 S 与 Z 是它 fork 出来的孩子,不同的只是各自的状态。您拿 ps 抓一把,捕到哪张全看您快门按在哪个瞬间。

cmdline 这个文件咱们单独说说,它是 exec 时内核原样记下的 argv,而且逐项用 NUL 拼接:

```text
[self] /proc/15018/cmdline 共 22 字节, NUL 显示为 \0:
    "./e6_proc\0hello\0world\0"
```

咱们数下来是 22 个字节,正是 `./e6_proc\0hello\0world\0` 的长度。咱们读它得按 \0 切分,要是拿普通的字符串函数直接去读,能拿到的只有第一项。僵尸的 cmdline 是空的,地址空间已经还了回去,连 argv 都没了,而 waitpid 收尸之后,/proc 里这个 pid 的目录整个消失。僵尸与收尸的完整两阶段,上一篇的 E3 已经讲过机制,这里补的是 /proc 侧的观察面,两条路对的是同一件事。

咱们再补记三个观察位。/proc/pid/exe 是指向正在运行的二进制的符号链接,文件被 rm 之后它会变成带 `(deleted)` 后缀的样子。/proc/pid/fd 里每个打开的描述符一条链接,E2 时点 6 的那行 fd0 读数,咱们就是从这儿读出来的,守护进程的三条标准流去了哪,您 ls 一眼便知。status 里的 Uid 四元组依次是 real、effective、saved、fs,setuid 的程序里 effective 会变,后续 exec 时 saved 决定的是还能不能变回去,本实验全程用的普通用户,所以四项相同。ps 和 top 干的活,其实就是把 /proc 里这些文件读整齐,您手边没有 strace 的时候,/proc 是永远开着的那个观察位。

## 另一侧:没有会话这层组织的 Windows

Windows 没有 POSIX 的会话、进程组与控制终端这一整套组织。Ctrl+C 走的是控制台事件:系统把 CTRL_C_EVENT 发给挂在同一个控制台上的进程,每个进程自己的 handler(SetConsoleCtrlHandler 注册)决定怎么响应,不装 handler 的默认退出。名字里带 process group 的东西那边也有一个,不过它是另一个东西:CreateProcessW 带 CREATE_NEW_PROCESS_GROUP 标记开出的组,组里的进程默认对 Ctrl+C 免疫,而且那边还有 GenerateConsoleCtrlEvent,还能按组定向地投递。咱们 Windows 侧的实测给这层默认免疫补了底:它其实是进程带着的一个隐式忽略位,不是系统侧按组成员身份划出的投递边界,靠进程自己的一句代码就能解除,而解除之后,定向发来的 CTRL_C 也实打实收到了。它与 E1b 相映的地方在于,一次群发总有收不到的人:咱们这边组外的父亲零接收,那边默认状态下的新组进程不响应。那边也没有 setsid 的对应物,长期后台的服务,Windows 另有自己的注册机制。控制台事件与 APC 的完整实测,咱们到 windows/process/ 的控制台事件篇再展开。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="setsid(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setsid.2.html"
  />
  <ReferenceItem
    :id="2"
    title="setpgid(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setpgid.2.html"
  />
  <ReferenceItem
    :id="3"
    title="credentials(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/credentials.7.html"
  />
  <ReferenceItem
    :id="4"
    title="ioctl_tty(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ioctl_tty.2.html"
  />
  <ReferenceItem
    :id="5"
    title="daemon(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/daemon.3.html"
  />
  <ReferenceItem
    :id="6"
    title="nohup(1)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man1/nohup.1.html"
  />
  <ReferenceItem
    :id="7"
    title="getrlimit(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getrlimit.2.html"
  />
  <ReferenceItem
    :id="8"
    title="environ(7) 与 putenv(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/environ.7.html"
  />
  <ReferenceItem
    :id="9"
    title="proc(5)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man5/proc.5.html"
  />
</ReferenceCard>
