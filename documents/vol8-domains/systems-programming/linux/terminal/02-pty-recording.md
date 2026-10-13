---
title: "伪终端与进程交互:终端录制与回放"
description: "伪终端(pty)这对 master 与 slave 设备是什么、script(1) 与 sshd 为什么都靠它:四步手搓 posix_openpt/grantpt/unlockpt/ptsname 实测(/dev/ptmx 5:2 与 man 一字不差,slave 节点在 posix_openpt 那一刻已出现在 /dev/pts 且属主已落好、grantpt 在本机走个过场,unlockpt 之前 open slave 报 EIO 而非 EACCES,close(master) 后节点即刻消失、slave 读到 EOF 写报 EIO),forkpty 一条龙的孩子侧验收(pid==sid、TIOCGSID 控制终端到手、isatty 0/1/2 全 1、前台组是自己,master 单流里回显与输出混流分不出方向),同一串 abc DEL z 六字节喂管道与喂 pty 的同字节对照(管道原样六字节,pty 读端到手编辑后的 abz 换行,master 另收 08 20 08 的 DEL 回显,关 ECHO 编辑照做回显消失,行编辑与回显归 tty 层的架构归属课),script(1) 最小复刻(forkpty 加 exec sh -i,138 字节 typescript 开头是 sh 5.3 括号粘贴模式的 ESC[?2004h,exit 一次输入流里露三次脸由 timing 逐层对出来,收场是 read(master) 报 EIO),按 timing 时刻表的回放(1.00 倍速 1257.1ms 对录制合计 1255.8ms、cmp 逐字节一致,2.00 倍速 629.0ms),新 pty winsize 0x0 与 TIOCSWINSZ 的 SIGWINCH 计数 1 到 2,以及 master 端读写不对称的独家判据(slave 全关后 write(master) 成功且字节被行规程回显、再 read 能收到,EIO 要队列空加对端关两个条件齐,2048 字节五连写 200ms 间隔 10/10 对 0ms 间隔 852/254 或 EIO 的复核矩阵,write 的成功证明不了任何事),与 daemon 篇 close(master) 后 SIGHUP 互为镜像"
chapter: 8
order: 2
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
related:
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 伪终端与进程交互:终端录制与回放

[上一篇](./01-termios-raw.md)咱们把行规程的开关逐个扳过了一遍,可站了整场实验的那座台子,咱们一直没正式介绍过:每阶段 openpty 开出来的那对设备,到底是个什么东西?[daemon 篇](../process/02-daemon.md)更是只把它当装置借了两回,一回借 script 的壳跑守护化,一回自己 openpty 开台、演了一场终端的死亡,机制上的事都没展开,跑完就散了。这一篇咱们把它请到正中间。它的名字是伪终端(pseudoterminal、缩写 pty),指的是一对成团的设备:咱们这一侧握 master,程序握的是 slave 那一头,夹在中间的东西您已经认识了,它就是上一篇的主角:行规程。

它解决的是一类很具体的问题。您想用 script 录一场会话、想用 ssh 登上一台远端机器、想让 xterm 画出一个窗口,这些场景里的每一方都需要一台终端,可手边偏偏没有真的:script 得让里面的 shell 以为自己在终端上,ssh 得让远端的 shell 把您本地的窗口当成它的终端。管道倒是能传字节,不过它伪造不出终端,行规程、回显、行编辑、窗口尺寸这些它一样都给不了,isatty 一问就穿了帮。pty 补的就是这个缺,man 4 pts 的承诺也写得直白,master 与 slave 都开着的时候,slave 给进程提供的接口跟真终端没有分别。

咱们从最底下的四步手搓起步,一路走到照 script(1) 的骨架录一场会话、再按时刻表放回去。实验的编号是 E1 到 E6,与仓库 `code/volumn_codes/vol8/systems-programming/linux/terminal/02-pty-recording/` 下的 e1 到 e6 六份源码一一对应,代码连同全部的原始输出都收进了存档,会话产物 session.typescript 与 session.timing 也一并入册了:session.typescript 记字节,session.timing 记的是时刻,它们是 E4 当场录出来的,E5 回放的时候拿它们当输入。正文里的输出块是节选,块内的删节咱们用 ... 标了出来,块首尾的裁剪就不一一标注了,您对表的时候以存档为准。本篇的 E 只认本篇,上一篇的 E 跟咱们同号却互不相干,您翻存档的时候认目录就好。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,glibc 的版本是 2.44,编译的口径一律 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,拿到的警告数是零,全部 `.out` 与两份会话产物出自 2026-10-05 的同一轮。实验的进程自己没有 tty,跑实验的台架给咱们的 stdio 是三根管道,所以凡是要终端的地方,咱们都像上一篇那样自建 pty,主终端咱们一个字节都不碰。给 /dev/pts 供货的那个内核文件系统 devpts,在本机是单实例的挂载,gid=5、mode=620、ptmxmode=000 这套参数咱们在 E1 的输出里看得到,/dev/pts/ptmx 的入口被 ptmxmode 封着,所以开 master 的正路是 /dev/ptmx。/bin/sh 用的是 sh-5.3,它其实是 bash 以 sh 的名字在跑,咱们 E4 的主角就是它。录制与回放的计时一律走 CLOCK_MONOTONIC,选钟的理由[时钟源篇](../time/01-clock-sources.md)讲过了,咱们这里不重开课。

## E1:四步开一对:posix_openpt、grantpt、unlockpt 与 ptsname

man 4 pts 给的开台流程拢共就几句话:开 /dev/ptmx 拿 master,slave 的节点会出现在 /dev/pts 里,可开门以前还得过 grantpt 与 unlockpt 的两道手续。咱们照着搓,顺手把每一步之后的 /dev/pts 目录内容与节点权限都拍了照,下面的几行骨架出自 e1_four_steps.cpp 的节选:

```cpp
int mfd = posix_openpt(O_RDWR | O_NOCTTY);  // 拿 master,节点此刻已出现在 /dev/pts
char* sn = ptsname(mfd);                    // 问出 slave 的路径
grantpt(mfd);                               // 名义上管节点的属主与权限
unlockpt(mfd);                              // 解锁
int sfd = open(sn, O_RDWR | O_NOCTTY);      // 现在才开得了门
```

头两行的输出交代的是环境实况,咱们原样搬上来:

```text
[0] 环境:/dev/ptmx 主次设备号 5:2(mode 0666);
    devpts 挂载:devpts /dev/pts devpts rw,nosuid,noexec,noatime,gid=5,mode=620,ptmxmode=000 0 0
  /dev/pts 内容(开新 pty 前): 7 5 4 9 3 2 0 1 ptmx
[1] posix_openpt -> master fd=3
    ptsname -> /dev/pts/6(isatty(master)=1)
  /dev/pts 内容(posix_openpt 之后、grantpt 之前): 6 7 5 4 9 3 2 0 1 ptmx
```

/dev/ptmx 报的 5:2、mode 0666,man 4 pts 写的就是这俩数,咱们本机对出来一字不差。fd 拿到的是 3,因为 0/1/2 都让台架的管道占着。真正值得您多看一眼的是前后两张目录清单:slave 的节点 /dev/pts/6 在 posix_openpt 返回的那一刻就已经在了,grantpt 的戏份还没到。节点的生灭跟着 master 走,这句话收尾的时候还会兑现。输出里的 isatty(master) 报 1 也值得记一笔,这个冷知识咱们在上一篇 E1 盘过了:Linux 的 master 端也是 tty,E3 马上就要靠它了。

咱们故意抢在 unlockpt 之前硬开 slave:

```text
[2] unlockpt 之前 open(/dev/pts/6) -> -1 errno=5(Input/output error)
[3] grantpt 之后  stat /dev/pts/6: uid=1000 gid=5 mode=0620
    unlockpt 之后  stat /dev/pts/6: uid=1000 gid=5 mode=0620
[4] open(/dev/pts/6) -> 4 isatty=1 ttyname=/dev/pts/6
```

报的是 EIO(errno 5),而不是 EACCES。这个错误码有它的讲究:锁的不是权限位,节点跟权限都好好的,不过 open 就是进不去,真正把您拦下来的,是设备本身的那把锁。内核里的那把锁有个名字叫 TIOCSPTLCK,ioctl_tty(2) 的 ioctl 清单里躺着它,unlockpt 干的事情就是把它拨开。grantpt 与 unlockpt 前后的两张 stat 一模一样,它们动的东西压根不在节点属性这一层。

那 grantpt 到底干了什么?man 3 grantpt 交代的职责是改 slave 节点的属主与权限,可存档的快照只拍了它之后的样子,功劳就分不清了。笔者写这篇的时候在 /tmp 补了一枚探针,用的同样是那套编译口径,在 posix_openpt 之后立刻 stat 了一次:

```text
posix_openpt 后:  uid=1000 gid=5 mode=0620
grantpt 后:      uid=1000 gid=5 mode=0620
```

节点在 posix_openpt 那一刻就已经是 uid=1000、gid=5、mode=0620 了,grantpt 跑了个空。属主 1000 就是咱们自己,devpts 在开 master 的时候就把属主落好了,gid=5 与 mode 0620 正对着挂载参数里的 gid=5、mode=620,这个 0620 里 tty 组拿到的只有写,write(1) 一类程序靠的就是它,好往您的终端上递话,读的权限只留给属主自己,别人的会话您摸不着。man 交代的差事在本机由 devpts 代劳了,glibc 的 grantpt 在 Linux 上走个过场,老系统的行为未必跟它一样,您按本机的口径记就好。

开了门咱们就验通路:master 写五个字节进去,slave 那头一笔 read 就原样拿到了,字节在中间过了一遍行规程。咱们把好戏留在收尾,留给 master 关掉的那一刻:

```text
[5] master 写 "ping\n",slave read -> n=5 (ping
):字节过了一遍行规程
[6] close(master) 之后  stat /dev/pts/6 失败:No such file or directory
    slave 再 read -> n=0 (EOF)  <- master 没了,slave 读到 EOF(read 返回 0)
    slave 再 write -> n=-1 Input/output error  <- 写也一样,对端没人了
  /dev/pts 内容(master 关掉后): 7 5 4 9 3 2 0 1 ptmx
```

节点当场就消失了,posix_openpt 之后清单里多出来的 6 现在没了,节点的生灭跟着 master 走,这就兑现了。slave 的这头还开着 fd,read 拿到的是 0(EOF),write 吃到的是 EIO。pts(4) 对这半场没写一个字的承诺,咱们按实测的口径记:master 没了,读端见到的是 EOF,写端见到的是 EIO。到了 E6 咱们再站到 master 那一侧把收场盘一遍,那边的量法又不一样了。

收尾的时候还有个打包好的选择,openpty 一个调用就把这四步连同 open(slave) 全包了。还有个姿势值得咱们现在认下:posix_openpt 这批函数出自 UNIX 98 标准,man 3 posix_openpt 开出来的条件是 `_XOPEN_SOURCE` 不小于 600,同门的 grantpt 一家只要 500。C 的严格模式里少了这句宏就编不过,咱们拿一个只含必要头文件的小文件试过,无宏与 500 的两档都吃到了 implicit declaration,600 那档的编译才干净。不过 g++ 在 Linux 上默认就定义了 `_GNU_SOURCE`,声明全都是亮着的,咱们删掉 e1 源码顶上那句 `#define _XOPEN_SOURCE 600`,再按本篇的口径编了一遍,出来的零警告照样能过,留着的那句 define 就算一道保险。

## E2:forkpty 一条龙:一个调用办齐 setsid、控制终端与 0/1/2

四步只开出了一对设备,咱们要是想让孩子像在真终端上那样干活,后头还有一串的活要干。这些布置咱们其实都见过:daemon 篇 E2 的 setsid,E3 的 `ioctl(sfd, TIOCSCTTY, 0)`,上一篇 E3 的 tcsetpgrp,再算上把 slave 接到 0/1/2 的 dup2。forkpty 把 openpty、fork 与孩子侧的活折进了一个调用,man 3 openpty 说的是,孩子那边的活归 login_tty:建新会话、认控制终端,dup2 接管的是三条标准流。咱们逐项验收:

```text
孩子报告: pid=1089214 sid=1089214(pid==sid:1,会话长) pgid=1089214 isatty0/1/2=1/1/1 ttyname(0)=/dev/pts/6 TIOCGSID=0(sid=1089214,控制终端到手) TIOCGPGRP=0(前台组=1089214,自己)
父进程 t=0.1ms 收齐 master 流:(ping\r\npong\r\n) —— 里面混着孩子的回显与输出,一条流分不出方向
孩子退出:0
```

单子咱们逐项过。报上来的 pid 等于 sid,孩子自己就是新会话的会话长。isatty(0/1/2) 报的全是 1,ttyname 给出的是 /dev/pts/6,三条标准流都落在了 slave 上。TIOCGSID 也成功返回了,控制终端也到手了,咱们说的这层绑定,它正是 daemon 篇 E2 里 setsid 之后被切走的东西,重定向也补不回来了。TIOCGPGRP 报的前台组就是孩子自己,^C 的收件人齐了。

第二行的输出是本篇后半场的舞台。父进程往 master 写了 `ping\n`,孩子读走、回了 `pong\n`,而父进程从 master 读到的是 `ping\r\npong\r\n` 一整条:敲进去的与打出来的混在了同一股流里,连 \n 都被输出处理补成了 \r\n,上一篇 E5 的 ONLCR 干的。master 的流里没有方向位也没有分界,E4 的录制物会把它放大给您看。还有一处小设计值得咱们点一下:forkpty 的后两个参数能顺手设置 termios 与 winsize,咱们传的都是 nullptr,开出来的就是出厂态,winsize 的那个空档咱们留给 E6 来填。

孩子那侧还有个小动作值得咱们交代,forkpty 一返回的当口,孩子就 close 掉了 master 的 fd。其实孩子这一下不是图整洁,master 的 fd 关不关得干净,牵着一台终端的生死:内核给 pty 记的是引用,谁的手里还留着 master 的 fd,这对设备就还不算断了气。daemon 篇 E3 里 close(master) 能触发 SIGHUP 的那场戏,前提正是父进程已经成了最后一个握着 master 的人,咱们的孩子要是把 master 也攥着,父进程再怎么 close 都没用了,终端也就永远死不透了。孩子把验收单写上去、交回咱们手里的那根报告管道,它的建立也得赶在 forkpty 之前,fd 是跟着 fork 继承下去的,咱们看孩子手里握着的写端与父进程手里的读端,它们本来就是同一根管子的两头。

## E3:同一串字节,管道与 pty 两个下场

回到开场的那个问题:凭什么非要 pty,管道哪里对不起您?装置还是同一个读程序:同一个可执行文件带上 reader 参数再跑一份自己当读端,读的是 stdin,对读到的内容不做任何加工,每笔 read 的收获都写上报告管道交回咱们手里。喂它的字节串定死成六个字节:abc、一个 DEL(0x7f)、z、换行。咱们让它分别挂上管道,挂上出厂态的 pty(canonical 加 ECHO),挂上关了 ECHO 的 pty。咱们看管道这一场:

```text
[场景1 管道]喂 61 62 63 7f 7a 0a (abcDELz\n)
  READ n=6 61 62 63 7f 7a 0a (abcDELz\n)
READ n=0
```

管道递过来的还是六个字节、一个都没少,咱们里外没看见谁动过它,DEL 也不过是个 0x7f 的普通字节。管道的两头都没有行编辑这回事,您想用管道,编辑的活就得您自己写。咱们再把同一串字节改喂 pty:

```text
[场景2 pty 出厂态(canonical+ECHO)]
  喂 61 62 63 7f 7a 0a (abcDELz\n)
  master 收到的回显:61 62 63 08 20 08 7a 0d 0a (abc^H ^Hz\r\n)
  READ n=4 61 62 7a 0a (abz\n)
```

咱们再看读端,到手的是四个字节 `abz\n`,中间的 c 让 DEL 吃掉了。编辑发生在哪?回显的那一行给得最直白:master 另外收到了九个字节,abc 是敲进去的回显,08 20 08 是 DEL 的回显姿势(退格、空格、退格),专门用来把屏幕上的 c 擦干净,结尾的 0d 0a 是换行过了输出处理的样子。读端拿到手的一份与 master 看见的一份都出自行规程之手,咱们的读程序一行编辑代码都没有、甚至不知道发生过编辑。咱们把 ECHO 关掉再喂同一串:

```text
[场景3 pty 关 ECHO(编辑仍在)]
  喂 61 62 63 7f 7a 0a (abcDELz\n)
  master 收到的回显:(无)
  READ n=4 61 62 7a 0a (abz\n)
```

编辑照旧做了,到手的 `abz\n` 一个字节没变,回显却没了。关的姿势也值得看一眼:代码里的 tcgetattr 与 tcsetattr 走的都是 master 的 fd、也都用得动,靠的正是咱们在 E1 记下的事实:Linux 的 master 也是 tty,两端共享的是同一份行规程。编辑与回显由此分成了两件事,一件管您敲进去的东西最终长什么样,一件管屏幕上给您看什么。密码提示下您敲字屏幕纹丝不动,靠的就是这一关,您的那一行照样被编辑、照样被送进读端。

咱们把三个场景并排一看,归属就摆正了:行编辑与回显住的是 tty 层,读程序的代码里一行都不用写,给它喂字节的也不用写。man 4 pts 点名的另一类主顾更能说明问题,su 与 passwd 压根拒收管道的输入,喂它们的正路就是 pty。script(1) 的手册还专门配了个 -E 开关,管的就是 slave 端的 ECHO 位,never 模式下的录制物里就少了输入的回显,咱们场景 3 演的是同一件事。至于 shell 嘛,您也别急着替它谢行规程,交互式的 shell 另有一套自己的行编辑,E4 里它会亲口亮给您看。

## E4:script(1) 的最小复刻:录一场 sh 会话

util-linux 里的 script(1),干的就是把一场终端会话原样记进文件的活,3.0BSD 的年代它就干这个了,默认的输出文件名就叫 typescript,存档里两个会话产物的名字也是照它起的。它的骨架咱们已经全部备齐:forkpty 开台,孩子 exec 的是交互式 sh,父进程扮演的是键盘加屏幕,往 master 写的就是敲进去的键,从 master 读到的就是该上屏的字节,读到的每一笔都原样记进文件。咱们照这个骨架复刻,孩子请的是 sh -i、没有真人敲键盘,咱们的日程写在代码里:echo hello,睡一秒的 `sleep 1; echo done`,收尾的是 exit。

开台之后父进程的头一件事就是把尺寸设成 24x80,靠的是 `ioctl(mfd, TIOCSWINSZ)`。为什么非设不可?E6 里有它的实测,这会儿咱们只说一半:新开的 pty 没有窗口的概念,咱们要是不主动设,按尺寸排界面的程序会直接趴窝。然后咱们进循环,poll 等 master 可读、读到的字节与间隔记进两份文件,喂键的事跟着日程走。循环的骨架长这样(e4_recorder.cpp 节选):

```cpp
struct pollfd pfd{mfd, POLLIN, 0};
char buf[4096];
for (;;) {
    // ...按日程决定这一轮发不发输入:第0条等 shell 起来,第1条等 hello,第2条等 done
    int r = poll(&pfd, 1, fed[2] ? 500 : 50);
    if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
        ssize_t n = read(mfd, buf, sizeof buf);
        if (n <= 0) break;                    // EIO:会话收场
        double t = now_ms();
        fprintf(timing, "%.1f %zd\n", t - t_last, (size_t)n);   // 时刻表:间隔+字节数
        t_last = t;
        fwrite(buf, 1, (size_t)n, script);    // 字节流:原样追加
        seen.append(buf, (size_t)n);
    }
    // ...(poll 出错即收场,键的 write 与 exit 后的 waitpid 收尸也排在这里)exit 晚了半秒的来历就在这
}
```

会话怎么收场?exit 之后 sh 退了,slave 侧连一个 fd 都不剩了,咱们这头再 read(master) 拿到的是 EIO,循环也就此打住了:

```text
read(master) -> n=-1 Input/output error —— 会话收场
录制完成:时长 1256ms,12 笔共 138 字节
```

录下来的这场会话拢共 138 个字节,咱们全量搬上来。ESC 是替 0x1b 写的名字,提示符前后的空隙是为了好读,字节层面以存档的 session.typescript 为准:

```text
ESC[?2004h sh-5.3$ echo hello\r\n
ESC[?2004l \r hello\r\n
ESC[?2004h sh-5.3$ sleep 1; echo done\r\n
ESC[?2004l \r exit\r\n
done\r\n
ESC[?2004h sh-5.3$ exit\r\n
ESC[?2004l \r exit\r\n
```

开头的前八个字节 `1b 5b 3f 32 30 30 34 68` 写的是 ESC[?2004h。它是 sh 的行编辑库 readline 跟终端仿真器打的招呼(仿真器说的就是握着 master 画屏幕的那一方、xterm 与 Windows Terminal 都是,本实验里干这个活的正是父进程):请对方打开括号粘贴模式,粘贴一大段文本的时候用特殊标记包起来,粘贴里的换行不再被当成一串回车挨个执行。这正好接上了 E3 留的话头:交互式的 shell 嫌行规程自带的编辑太素,把终端调成自己的模式,编辑、历史、补全的活全归 readline 接管。行编辑的差事于是有了两层,行规程给普通的读程序兜底,讲究的程序自己另盖一层,这场会话里咱们会看到两层都出场。

接下来看 exit 在流里的三遍出现。日程里它只喂了一遍,流里却出现了三遍,咱们得配上时刻表才分得清谁是谁。事情出在咱们自己的日程逻辑上:代码的等待条件是流里出现 `done` 这个词、一出现就喂 exit,而第二条命令的回显里正好带着 echo done,关键字提前命中了,exit 比原计划早进了队列。真正喂进去的时刻又拖了半秒,因为代码里喂键的 write 排在 poll 后面,那一轮 poll 的手里没有数据,坐满了一个 500ms 的超时才回来,timing 文件里那笔 500.6 记的就是这段拖延。

咱们把字节序和时刻表对上,三个 exit 就能一层层地认出来。拖延的半秒里 sh 正在睡、readline 不在场,终端被 readline 交还成了出厂态,还是 canonical 加 ECHO 的那套。行规程把队列里的 exit 回显了一遍,这是咱们认出来的第一个。一秒到点的时候,done 打了出来,新提示符也亮了起来,readline 也回来接手了,把队列里那行捡走又回显了一遍,这是咱们认出来的第二个。最后 sh 收到 exit 退了场,交互式的 bash 在退出前会自己打一句 exit 道别,这是它的老习惯、公认的行为,咱们没单独做实验隔离这一层,而 sh-5.3 背后正是 bash 的 sh 模式,这层归因的线也就接上了。从第二条命令的回显到 done 出现,timing 记了 500.6 加 501.0 共 1001.6ms,正好是 sleep 1 的时长加点调度毛边,这三层谁在哪个时刻出的场,是咱们拿字节序加时刻表推出来的排班,文件里写明的只有间隔与字节数。

咱们回放要用的时刻表就是 session.timing、每笔一行两栏,记的是距上一笔的毫秒数和这笔的字节数:

```text
2.8 8
0.2 8
250.7 21
0.1 7
0.1 16
0.1 29
500.6 6
501.0 6
0.0 8
0.1 12
0.0 11
0.1 6
```

十二笔的间隔加起来是 1255.8ms。咱们这个两栏的格式跟 script 的 timing 同思路,它那两栏记的是秒与字符数,咱们记毫秒与字节数,您跨界对表的时候留意单位。还有最后一件事得跟您说明白:typescript 里躺着的只有一条流,没有方向的信息,哪笔是敲进去的回显、哪笔是程序打的输出,文件里没有一个字的交代。script(1) 的 BUGS 一节自己都交代了,换行与退格全都进了录制物,跟新手想的不一样,这是 master 的本性,并不是谁偷了懒。

## E5:回放:按时刻表把会话再演一遍

录都录了,咱们得把它放得回去才算数。player(回放器)的逻辑拢共几行:逐行读 timing,睡够距上一笔的间隔,咱们还能给间隔除一个倍速,再把 typescript 里对应的字节写给 stdout。核心的循环就这一段(e5_player.cpp 节选):

```cpp
while (fgets(line, sizeof line, timing)) {
    double delta = 0; size_t n = 0;
    if (std::sscanf(line, "%lf %zu", &delta, &n) != 2) continue;
    sleep_ms(delta / speed);              // 倍速就除在间隔上
    if (fread(buf, 1, n, script) != n) { std::fprintf(stderr, "typescript 短了一截\n"); break; }
    write(1, buf, n);                     // 这笔字节交给 stdout
}
```

咱们按 1.00 与 2.00 倍速各放一遍:

```text
$ ./e5_player session.timing session.typescript > replay.out
回放完成:12 笔 138 字节,计划睡眠合计 1255.8ms,实际睡眠合计 1256.9ms,全程墙钟 1257.1ms(含写 stdout 的开销),倍速=1.00
$ cmp replay.out session.typescript && echo 一致
一致
$ ./e5_player session.timing session.typescript 2.0 > /dev/null
回放完成:12 笔 138 字节,计划睡眠合计 627.9ms,实际睡眠合计 628.9ms,全程墙钟 629.0ms(含写 stdout 的开销),倍速=2.00
```

1.00 倍速的全程墙钟是 1257.1ms,对录制侧 timing 合计的 1255.8ms,差了 1.3ms,里面有 nanosleep 的粒度,也有写 stdout 的开销,这个量级咱们认。2.00 倍速的那轮是 629.0ms,间隔减了半,笔数与字节都是原样的。字节的这一关咱们交给 cmp:拿 replay.out 跟 session.typescript 对着过一遍,它一声不吭地通过了,换来的退出码是 0,跟着的 echo 把 `一致` 打了出来,138 个字节逐位都对上了。util-linux 那边现成的一对,是 script 的 --log-timing 记时刻,scriptreplay 的 -d/--divisor 放。--timing 是 script 的旧写法,现行手册已经标了弃用,咱们按实名记。divisor 的意思就是除数,拿它除在间隔上就提了速,跟咱们 `sleep_ms(delta / speed)` 里把间隔除以 speed 的做法是同一个思路。

有一处咱们得主动交代。验收的时候 player 的 stdout 被重定向进了文件,躲开了一层处理。您要是把它直接往自家终端上放,录制物里的 \r\n 会再过一遍行规程的输出处理,ONLCR 又会给出厂的 \n 补一个 \r,屏幕上就成了 \r\r\n。咱们真做回放器的时候,得按上一篇的办法把自家 tty 调成 raw 再写。笔者在这一步没有上实测,是从上一篇 OPOST 的实测直接推的,您写播放器的时候拿它当个起点。

## E6:尺寸与收场:winsize、SIGWINCH 与 master 的 EIO

E4 埋下的线,咱们现在来收。咱们新开一对 pty,问的头一件事是尺寸:

```text
[1] 新开 pty 的 winsize: rows=0 cols=0 —— 0x0,没人替你设
```

报的是 0x0。pty 的身上没有屏幕、没有窗口、尺寸也得有人告诉它,能告诉它的只有握 master 的终端仿真器,forkpty 的第四个参数本来就能在开台时一并设好,e4 用 ioctl 补设是同一件事的另一种姿势。咱们改两次尺寸,孩子在 slave 侧每 100ms 上报一回 TIOCGWINSZ 与 SIGWINCH 的计数:

```text
[2] t=200ms TIOCSWINSZ(30x100)
[2] t=500ms TIOCSWINSZ(40x120)
    孩子报告 t=  100ms TIOCGWINSZ=0x0 SIGWINCH 计数=0
    孩子报告 t=  200ms TIOCGWINSZ=30x100 SIGWINCH 计数=1
    ...
    孩子报告 t=  501ms TIOCGWINSZ=40x120 SIGWINCH 计数=2
    ...
[3] 孩子退场(slave 全关),父进程这边:
    read(master)  -> n=-1 errno=5(Input/output error)  <- 队列空+对端关,才报 EIO
    write(master) -> n=1 errno=0(Success)  <- 写不报错
    再 read(master) -> n=1 (回显)  <- 刚写的 x 被行规程回显回来了(ECHO 还开着)
```

尺寸这边验到了两条机制。TIOCSWINSZ 一落、slave 侧的 TIOCGWINSZ 立刻就是新值,t=200ms 的那笔对得严丝合缝。咱们每改一次尺寸,挂在终端上的前台进程组就收一条 SIGWINCH,两次 ioctl 之后计数从 0 走到了 2。您拖动终端窗口的那一瞬,发信号的正是仿真器对 master 的同一个调用,咱们的实验里没有真人拖窗口,咱们拿 ioctl 替它按了两下,机制走的是同一条路。ncurses 一类的全屏程序靠 SIGWINCH 重画界面,winsize 停在 0x0 的话它们连第一屏都排不出来,E4 预设 24x80 的理由到这儿就收拢了。

真正值得单独立住的是 [3] 那三行。孩子退了场,slave 侧连一个 fd 都不剩了,父进程的 read(master) 拿到的是 EIO,这一半在咱们的意料之中,daemon 篇 E3 从另一头演过同一件事的另一半。意外的部分在后头:write(master) 报了成功(n=1),连一个 errno 都没给咱们。咱们再 read 一次,居然读回了刚写的那个 x,行规程把它的回显送回来了,这个 tty 的 ECHO 还开着,slave 没了人也照样发,发进 master 的读队列,咱们就收到了。

咱们不放心只凭一次实验下判语,复核的矩阵摆在存档的 README 里:单笔 2048 字节连写五次、间隔 200ms 再读的这一套动作,整轮复跑了十遍,十遍全都读到了回显。间隔压到 0ms 立即读的这一档,读到的就杂了,有时是回显还在途中的半截(852 或 254 字节),有时 read 抢在回显进队之前、直接就报了 EIO。咱们把这些合在一起,判据浮出来了:read 想报 EIO,得等队列空着、对端也关了,两个条件一起凑齐了才成。写路径压根没有这个检查、write 的成功也就证明不了任何事。对写代码的咱们来说就一句话,想知道 pty 的对面还在不在,只有 read 的 EIO 可信,咱们 e4 录制器的收场判定靠的正是它。

与 daemon 篇 E3 的镜像关系到这儿拼完整了。那边咱们 `close(master)`,从 slave 那一侧看到的是:前台组同毫秒各收一条 SIGHUP,会话长则被缺省动作处决了。这边咱们让 slave 退了场,从 master 这一侧看到的却是:read 报的是 EIO,而 write 照样成功。同一对设备的两头,退场的那一头不同,看到的信号也就不一样,咱们写程序的时候两头都得会读。

## 另一侧:Windows 的 ConPTY

POSIX 这边的 master 与 slave,Windows 那边一度没有它的正主,想给管道上的程序装终端,winpty 的年代全靠模拟。2018 年的 Windows 10 1809 把 ConPTY(Windows 伪控制台)送进了系统,以 CreatePseudoConsole 为首的这套 API 开出来的同样是一对通道,Windows Terminal 与 Windows 上的 OpenSSH 都靠它,E6 的改尺寸在那边对应 ResizePseudoConsole。ConPTY 不在[Windows 控制台篇](../../windows/console/01-console-api.md)的实验台里,console 的字符缓冲区、输入事件与 VT 序列才是那边的主课,这里咱们只认个亲。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="pts(4)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man4/pts.4.html"
  />
  <ReferenceItem
    :id="2"
    title="pty(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/pty.7.html"
  />
  <ReferenceItem
    :id="3"
    title="posix_openpt(3) 与 grantpt(3)、unlockpt(3)、ptsname(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_openpt.3.html"
  />
  <ReferenceItem
    :id="4"
    title="openpty(3)、login_tty(3) 与 forkpty(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/openpty.3.html"
  />
  <ReferenceItem
    :id="5"
    title="ioctl_tty(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/ioctl_tty.2.html"
  />
  <ReferenceItem
    :id="6"
    title="script(1)"
    publisher="util-linux (man7.org 镜像)"
    url="https://man7.org/linux/man-pages/man1/script.1.html"
  />
  <ReferenceItem
    :id="7"
    title="scriptreplay(1)"
    publisher="util-linux (man7.org 镜像)"
    url="https://man7.org/linux/man-pages/man1/scriptreplay.1.html"
  />
  <ReferenceItem
    :id="8"
    title="Creating a Pseudoconsole Session(ConPTY)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session"
  />
</ReferenceCard>
