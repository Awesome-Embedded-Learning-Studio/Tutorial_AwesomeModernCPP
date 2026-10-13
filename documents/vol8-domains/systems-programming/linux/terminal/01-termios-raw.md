---
title: "termios 与 raw 模式:终端这层在替您做什么"
description: "终端不是键盘的延长线:行编辑、回显、把 ^C 翻译成信号,全是内核里 tty 这层替您的程序做的,termios 的四组标志就是这台机构的开关面板,全部实验跑在自建 pty 台架上、主终端零接触。E1 出厂普查(ICRNL|IXON、OPOST|ONLCR、ISIG|ICANON|ECHO|IEXTEN 全家,B38400+CS8,cfmakeraw 清 ICRNL/IXON/OPOST/ISIG/ICANON/ECHO/IEXTEN 却不清 ONLCR,因为 OPOST 一关它就失效,手工只清 ICANON|ECHO 的最小 raw 与 cfmakeraw 差 ISIG/IXON/ICRNL/OPOST 四处,后续逐个兑现成行为,顺带三个本机事实:master 端也是 tty 且与 slave 共享同一份行规程、glibc 2.42 起 B 常量就是真实速率值而内核 c_cflag 里存的还是老编码两头靠 glibc 翻译、出厂 VMIN=1 VTIME=0)。E2 canonical 干等换行 600.6ms 才一笔交 5 字节,对 raw 喂几块到几块,DEL 的行内编辑发生在 tty 层(回显是 08 20 08 三个字节),^U 整行抹掉。E3:^C 的两件事可以分开做,无前台进程组时排队输入照样被冲但 SIGINT 无人接收,setsid+TIOCSCTTY+tcsetpgrp 三步之后同一场 ^C 才有人接,^D 交出未换行的行、再一个返回 0 且 EOF 不粘、同一 fd 后续仍可读,raw 下 0x03/0x04/0x1c 全是普通字节。E4 VMIN/VTIME 四种组合六场实验,D+F 合判字符间计时器每收一字节重置(200ms 间隔连喂得 4 字节@600.7ms、450ms 间隔断供得 2 字节@955.7ms,只从首字节起算的说法被实测裁掉,与 termios(3) 的 interbyte timer 口径一致)。E5 逐位:ECHO 开时回显里的换行也被 ONLCR 补成 CR LF、ECHO 关 master 无痕而读端照收(输密码的机制)、OPOST 门里门外差一个 CR、ICRNL 关时 CR 不是行结束符要 ^D 补一刀。E6 RAII terminal_guard:raw 滞留跨进程存活(接力孩子零配置继承),析构在 throw 路径照样还原,TCSAFLUSH 冲掉滞留输入而 TCSANOW 保留,滞留字节切回 canonical 后免行结束符一笔交货"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 31
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
related:
  - "守护进程、会话与环境:setsid、双 fork、rlimit 与 environ"
  - "时钟源与 POSIX 时间 API:机器里的九把钟"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "信号(上):sigaction 与异步信号安全"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# termios 与 raw 模式:终端这层在替您做什么

咱们从一个您每天要做上几十遍的动作问起。您在终端里敲下了 abc 三个字母,第三个敲错了,于是按了一下退格,再补了个 d,把回车也敲了。屏幕上 c 消失了、d 顶了上去,这套流程您熟得不能再熟。那么这次删除是谁处理的?您眼前的前台程序此刻还阻塞在 read 上,拿到的字节一个都没有,它连处理的机会都没有。是 shell 吗?shell 倒是在场的,可它等的只是程序退出,插不上这次的手。真正干活的,是内核里的 tty:键盘的字节从您敲下去,到 read 把它们交出来的那一步,中间隔着的可是一整层机构,行编辑它做了,回显它做了,把 ^C 翻成 SIGINT 也是它干的。termios 就是这层机构暴露出来的开关面板,本篇咱们把面板上的开关逐个拨过去,每拨一个都亲眼看一看行为的变化。

不过动开关之前,咱们得知道谁在乎它们。编辑器、htop、ssh 这样的全屏程序,要的就是把 tty 的默认服务整个撤掉:按键要逐个地立刻到手,界面的退格、移动光标都归它们自己画。^C 要变成自己读到的普通字节,而不是半路被翻成信号。程序输出的每个字节要原样上路,不许别人顺手加工了哪怕一个字节。这套全撤的状态,就是常说的 raw 模式。而您平时敲命令时享受的行内编辑、输密码时的无回显,又是同一块面板上的另一种组合。同一台终端装得下两种相反的期待,调和全在 termios 的手里。

实验的编号是 E1 到 E6,对应的是仓库 `code/volumn_codes/vol8/systems-programming/linux/terminal/01-termios-raw/` 存档下 e1 到 e6 六个程序,代码与全部原始输出都入了册。本篇的 E 只认本篇:正文里的编号一律写大写的 E,存档的文件名一律是小写 e 打头,本篇、下一篇、Windows 控制台篇全是一样的,同号的也各是各的,您翻存档的时候认目录就好,大小写是不用认的。正文里的输出块多数是节选,删掉的行以 `...` 标出,拿存档对表的时候请以存档为准。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核用的是 6.18.33.2-microsoft-standard-WSL2、g++ 16.2.1 与 glibc 2.44,编译的口径一律 `-std=c++20 -O2 -Wall -Wextra -Wpedantic`,结果是零警告,全部的 `.out` 都捕获于 2026-10-05。计时用的都是 CLOCK_MONOTONIC,选钟的判据[时钟源篇](../time/01-clock-sources.md)已经量过,咱们不重讲。

咱们还有一条口径要说:termios 是内核里 tty 对象的属性,不挂在任何一个进程的身上,咱们要是拿自己正在用的主终端做实验,改坏了任何一处,您的终端当场变成哑巴,能救场的只有 `stty sane` 了。所以全部实验跑在咱们自建的 pty 里:父进程 `openpty` 开出一对 master 与 slave,fork 出来的孩子把 slave 接到自己的 fd0 上,它就有了一只任咱们摆布的终端。父进程按自己的时刻表往 master 写字节,扮演敲键盘的人,孩子每完成一次 read 的时候,就把开始时刻、完成时刻、字节数、内容写上报告管道交回咱们手里。主终端的字节一个都不动。pty 本身的身世(`posix_openpt` 四步、`forkpty`、master 的读写语义)是下一篇的正题,本篇咱们把它当装置用。这一篇的实验代码走的是裸 POSIX 调用,前面几篇的 unique_fd 这回没派上用场。

## E1:四组标志的普查:出厂设置逐位翻成人话

新开的 pty 什么都没设过,咱们拿 `tcgetattr` 拍一份出厂设置。termios 结构体里真正管事的,是四组标志加一排特殊字符的组合,方向各有各的不同:c_iflag 管的是进门,输入的字节要被改写什么。c_oflag 管的是出门,输出的字节要被加工什么。c_cflag 管的是线路,位宽、波特率这类物理参数记在它的名下。c_lflag 管的是行为,行编辑、回显、信号翻译这类终端特有的服务。c_cc 里装的则是一排档位,^C、^D 每个特殊键对应的字节,全都记在这里了。

```text
[B1. 新开 pty slave 的出厂设置(内核默认)]
  c_iflag  = 0x00000500 :  ICRNL IXON
  c_oflag  = 0x00000005 :  OPOST ONLCR
  c_cflag  = 0x000f00bf : CS8 CREAD
  c_lflag  = 0x00008a3b :  ISIG ICANON ECHO ECHOE ECHOK ECHOCTL ECHOKE IEXTEN
  c_cc: VINTR=^C VQUIT=^\ VERASE=^? VKILL=^U VEOF=^D VSTART=^Q VSTOP=^S VSUSP=^Z VMIN=1 VTIME=0
```

咱们挨个认人。进门的 ICRNL 把 CR 翻成 NL,这是回车键能当行结束符的真正原因,您按下的 0x0d 在门口就被换成了 0x0a,E5 里咱们会亲眼看到它拨来拨去时的动静。IXON 让 ^S 与 ^Q 被吃掉当了流控,^S 管的是暂停输出,^Q 管的是恢复输出,它们俩永远到不了读程序的手里。出门的 OPOST 是输出加工的总开关,ONLCR 是它底下的一项,把程序写的 \n 补成 \r\n,您写一个换行符、光标却乖乖回到下一行行首,功劳得记在它的头上。c_lflag 那一长串是本篇的主角:ISIG 管的是特殊字符翻信号,ICANON 管的是行模式,ECHO 管的是回显,ECHOE、ECHOK、ECHOCTL、ECHOKE 是挂在 ECHO 底下的四个子开关:擦一个字的回显归 ECHOE,抹一行的回显归 ECHOK,控制字符印成 ^X 的样子归 ECHOCTL,整行抹掉时的屏幕擦除也归 ECHOKE。c_cc 里的档位跟键盘认知对得上:退格是 ^?(0x7f,不是有些人以为的 0x08),^U 抹的是整行,^D 送的是 EOF,^Z 管的是挂起。出厂的 VMIN=1、VTIME=0,在非 canonical 模式下它们决定 read 的返回条件,专门伺候它们的是 E4。

波特率这块值得咱们单独看上两眼,因为它夹在两套记法的中间。存档里 e1 的实测是:本机 glibc 2.44 的 B 常量已经是真实速率值,`B9600` 宏的值就是 9600,这是 glibc 2.42 起的新口径,老口径里它是一串看不懂的编码。内核的 c_cflag 里存的仍然是老编码,咱们设一次 B9600 再读回来:

```text
    c_cflag=0xf00bf 的记录:低 4 位 0xf 是输出波特率的老编码(B38400=0o17),0xf0000 那几位是输入波特率的镜像(老编码<<16);本机 glibc 的 B 常量已是真实速率值(B9600 宏==9600,glibc 2.42 起的新口径),内核里存的仍是老编码,glibc 两头翻译,round-trip 实测:
    设 B9600 后 cfgetospeed=9600 c_cflag=0xd00bd(低 4 位变 0xd=0o15 老编码,pty 上无物理意义,stty 同样报 9600)
```

您看,咱们设进去的是 9600,`cfgetospeed` 读回来的也是 9600,可 c_cflag 的低 4 位从 0xf 变成了 0xd,那是老编码里的 B9600。翻译是 glibc 两头做的,round-trip 是对得上的。出厂的 0xf 是老编码的 B38400,0xf0000 那几位是输入波特率的镜像。pty 上这些位是没有物理意义的,但普查的时候会看见它们,咱们对上号、心里有底就行。咱们顺带交代一句:存档原来的注解把改版起点写成了 2.44,那是把本机的版本号当成了起始版本,重录 .out 的时候已经改掉了,版本起点按官方公告的 2.42 算。

## 拨给 raw:cfmakeraw 清了什么、没清什么

咱们把面板拨到 raw,标准库里的一键操作就叫 `cfmakeraw`,它前后的差异咱们用 diff 亮出来:

```text
[cfmakeraw 相对原始的差异]
  c_iflag : -ICRNL -IXON
  c_oflag : -OPOST
  c_cflag : (无变化)
  c_lflag : -ISIG -ICANON -ECHO -IEXTEN
    注意:cfmakeraw 只清 OPOST、不清 ONLCR(OPOST 一关,ONLCR 失去作用,留着无害);ECHOE/ECHOK/ECHOCTL/ECHOKE 是 ECHO 的子开关,母开关 ECHO 一清它们全部失效;
    本例 CSIZE 已是 CS8,所以 c_cflag 看不出变化。
```

这四行的差异加两行注,咱们一行行过。ICRNL 与 IXON 清了,进门不再改写了、流控字节也照交了。OPOST 清了,出门也原样了。ISIG、ICANON、ECHO、IEXTEN 清了,特殊字符不再翻成信号了,read 也不等行了,按键也不回显了。IEXTEN 管的扩展输入处理也一并停了,^V 按字面收下一字符的那类逃逸归它管。ECHO 的四个子开关不用挨个清,母开关一关它们就集体失效了,cfmakeraw 也就不动它们了。c_cflag 看似是没有变化的,其实是 CSIZE 本来就是 CS8,而 cfmakeraw 会把位宽设成 CS8、VMIN 设 1、VTIME 设 0,出厂的恰好就是这个组合,所以 diff 上看不出动静。

真正值得您停下看一眼的,是 ONLCR 留下来了。输出加工的总开关 OPOST 都关了,ONLCR 作为总开关底下的一项自然失效,cfmakeraw 也就懒得动它了,留着是无害的。您手工清位的时候可以照样少动一位,但您得知道这是为什么,而不是从网上复制一份清单照着填就完事。同样的道理,cfmakeraw 的完整清单里连 IGNBRK(忽略 break 条件)、ISTRIP(把字节截成 7 位)这些本机没设的位也一起清,设了的才在 diff 里露脸。

网上更常见的路子则只手工清 ICANON 与 ECHO 两位,大家叫它最小 raw,咱们也照做了一份 diff:

```cpp
manual.c_lflag &= ~(tcflag_t)(ICANON | ECHO);   // 网上流传的最小 raw
```

```text
[手工 raw vs cfmakeraw 相对原始的差异]
  c_iflag : -ICRNL -IXON
  c_oflag : -OPOST
  c_cflag : (无变化)
  c_lflag : -ISIG -IEXTEN
    含义:手工版下 ISIG 仍在(^C 还是信号)、IXON 仍在(^S/^Q 会被吃掉)、ICRNL 仍在(CR 会被改写成 NL)、
             OPOST 仍在(程序写的 \n 出门变 \r\n)。这些差异正是 E2/E3/E5 要逐个看到的行为。
```

上面的差距表,后面咱们会逐个兑现成行为:ISIG 还在自己的位置上,^C 就还是信号而不是字节,这事 E3 演给您看。ICRNL 还在自己的位置上,回车键一进门就被改写了,咱们到 E5 看。OPOST 也留在了原地,程序写的 \n 出门还是 \r\n,同样也是 E5 的戏。IXON 的位置没变,^S 与 ^Q 依旧进不了您的手,咱们没有为它专门排实验,termios(3) 手册页的口径与它的机制一致,咱们记下不展开。

E1 还捎带了一个本机事实,跟惯常的说法不太一样,咱们摆在桌面上。openpty 开出来的 master 端,传统的说法是 master 不是 tty,本内核的实测不是这样:

```text
[A] master=3 slave=4:isatty(master)=1 isatty(slave)=1
    tcgetattr(master)=0 tcgetattr(slave)=0 两端看到的 iflag/lflag 相同:1
    经 master 清 ECHO:tcsetattr(master)=0,slave 侧 ECHO=0(0=同步清掉,两端共享同一份行规程)
```

isatty 对 master 报的是 1,咱们经 master 调 tcsetattr 清 ECHO,slave 那头同步清掉了。原因其实不玄:两端共享的是同一份行规程(line discipline,内核里 tty 上负责行编辑、回显、信号翻译的那层逻辑),master 与 slave 只是这同一份逻辑对外的两个名字。master 不是 tty 的说法在别的实现上是成立的,POSIX 对 master 端的 isatty 本来就没有规定,Solaris 的 master 就不是 tty,咱们按 Linux 的实测口径记:两端都算 tty,改动也是互通的。

## E2:canonical 与 raw:read 什么时候才肯交货

面板咱们说完了,接下来看的是行为。咱们在 E2 用同一条时刻表喂两种模式,看 read 的耐心差多少。canonical(行模式)的那一场,咱们 300ms 一块,喂的是 ab、cd、换行:

```text
[阶段1 canonical:行没写完,read 一直干等]
  喂 t=   0ms 61 62 (ab)
  喂 t= 300ms 63 64 (cd)
  喂 t= 600ms 0a (\n)
  读 t=[    0.2..  600.6]ms n=5   61 62 63 64 0a (abcd\n)
  master 收到的回显:61 62 63 64 0d 0a (abcd\r\n)
```

其实孩子从 0.2ms 起就阻塞在 read 上,干等到了 600.6ms 换行符才进门,一口气拿走了 5 个字节。前两块喂进去的字节没有丢,它们躺在 tty 的行缓冲里攒着,这就是 canonical 的交货条件:read 要等的就是一整行。回显那一行也请您留意,咱们喂的是 \n(0a),master 收到的回显却是 0d 0a,回显走的也是输出的路,它也过了 ONLCR 的门,咱们到 E5 再细说。

第二场咱们把退格键(0x7f、DEL)排进时刻表:

```text
[阶段2 canonical:DEL(0x7f) 的行内编辑发生在 tty 层,读端拿不到]
  喂 t=   0ms 61 62 63 (abc)
  喂 t= 200ms 7f (DEL)
  喂 t= 400ms 64 0a (d\n)
  读 t=[    0.3..  400.8]ms n=4   61 62 64 0a (abd\n)
  master 收到的回显:61 62 63 08 20 08 64 0d 0a (abc^H ^Hd\r\n)
```

您看读端拿到的:是 abd\n,那个 c 被划掉了,而 DEL 自己更是一点影子都没有。开场的那个问题在这里有了实测答案:退格的行内编辑发生在 tty 层,赶在 read 交货之前就做完了,读程序从始至终只见过编辑完的结果。回显的那串也很有意思:abc 与 d 之间的三个字节是 08 20 08,退一格、空格盖掉 c、再退一格的三个动作,演完了一次删除,这正是 ECHOE 这个子开关给咱们演的。

第三场咱们换 ^U(0x15、VKILL) 上场,它的戏份是整行抹掉:

```text
[阶段3 canonical:NAK(^U) 整行抹掉,读端只看到新行]
  喂 t=   0ms 61 62 63 (abc)
  喂 t= 200ms 15 (NAK)
  喂 t= 400ms 78 79 0a (xy\n)
  读 t=[    0.3..  400.6]ms n=3   78 79 0a (xy\n)
  master 收到的回显:61 62 63 08 20 08 08 20 08 08 20 08 78 79 0d 0a (abc^H ^H^H ^H^H ^Hxy\r\n)
```

abc 整行消失了,回显的是三个 08 20 08,一个字符配一套擦除的动作,这正是 ECHOKE 演的场面。咱们回来看读端:只看到了后来者 xy\n,前面的那行仿佛没存在过。

第四场咱们把 termios 换成 cfmakeraw 的 raw,时刻表倒是原封不动:

```text
[阶段4 raw(cfmakeraw,VMIN=1):喂几块到几块,没有行概念]
  喂 t=   0ms 61 62 (ab)
  喂 t= 300ms 63 64 (cd)
  喂 t= 600ms 0a (\n)
  读 t=[    0.2..    0.3]ms n=2   61 62 (ab)
  读 t=[    0.3..  300.5]ms n=2   63 64 (cd)
  读 t=[  300.5..  600.6]ms n=1   0a (\n)
  master 收到的回显:(无)
```

结果是咱们喂几块、它交几块,三笔的量是 2、2、1,字节一进门就交了货,没有一点行的概念。回显一栏空了,ECHO 已经被 cfmakeraw 清掉了。canonical 模式给您的服务拢共两层,把字节攒成行、替您回显,raw 把两层一起撤了,编辑器们要的正是这个:拿到退格、方向键、组合键的原始字节,界面上的一切它们自己处理。

## E3:特殊字符与 ISIG:^C 做的两件事,可以只做一件

ISIG 开着的时候,VINTR(^C)、VQUIT(^\)、VSUSP(^Z) 这些字节在 tty 层就被拦了下来,翻成了信号递走。咱们把书里通常合在一起讲的两件事,摆到一次 ^C 的头上看:一边冲掉排队的输入与输出,一边把 SIGINT 递到前台组的手里。有意思的是,咱们居然可以让 tty 只做其中的头一件,前提是 tty 的名下没有前台进程组。实验排了两个剧本,第一个剧本里的孩子没有给自己立会话,pty 那边也没有自己的控制会话:

```text
[阶段1 canonical 无会话:^C 冲掉排队的 ab,但信号没有收件人(SIGINT=0)]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 03 (^C)
  喂 t= 400ms 63 64 0a (cd\n)
  读 t=[    0.2..  400.6]ms n=3   63 64 0a (cd\n) 计数:SIGINT=0 SIGQUIT=0
  master 收到的回显:61 62 5e 43 63 64 0d 0a (ab^Ccd^M\n)   孩子退出码=0
```

您看读端拿到的:到手的只有 cd\n,排在前面的 ab 被 ^C 冲掉了。可孩子的 SIGINT 计数是 0,信号却没有了收件人。队列冲掉了,而信号没递出去,因为 tty 的名下没有前台组可以递。咱们看第二个剧本:孩子在 read 之前多走了三步,把前台组的身份补上:

```cpp
setsid();                   // 脱离原会话,自立门户
ioctl(0, TIOCSCTTY, 0);     // 认 slave 为自己的控制终端
tcsetpgrp(0, getpgrp());    // 把自己这一组设为这只 tty 的前台组
```

```text
[阶段1b canonical 前台组:setsid+TIOCSCTTY+tcsetpgrp 三步之后,同一场 ^C 有人接了]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 03 (^C)
  喂 t= 400ms 63 64 0a (cd\n)
  读 t=[    0.4..  400.5]ms n=3   63 64 0a (cd\n) 计数:SIGINT=1 SIGQUIT=0
...
```

同样的时刻表,同样的一场 ^C,SIGINT 的计数就到了 1。冲队与递信号是两个独立动作:冲队是 tty 对特殊字符的固定反应,NOFLSH 没设的时候它就冲,而递信号要认的却是前台组,没人认的时候就没法收。会话、控制终端、前台组这三层关系的挂法,[daemon 篇](../process/02-daemon.md)用六组实验从反方向讲过了,那边演的是终端死掉的时候谁被处决,咱们这边借的是同一套机制,就不重开课了。另外回显里的 5e 43 就是屏幕上那个字面的 ^C,ECHOCTL 把控制字符印成两字符的帽子写法,您平时看到的 ^C 长相,是回显层给的,不是键盘给的。

^\\ 走的是同一条路。第三场的同款时刻表把 ^C 换成 ^(0x1c),SIGQUIT 的计数就从 0 变成了 1,排队的输入照样被冲掉。输出就不整段重贴了,您拿存档 e3 的第三场对表。

^D 则给咱们演另一种身份:排队的字节它不动,专门负责的就是催交货:

```text
[阶段2 canonical:^D 交出行、再 ^D 是 EOF,EOF 之后还能接着读]
  喂 t=   0ms 61 62 (ab)
  喂 t= 200ms 04 (^D)
  喂 t= 400ms 04 (^D)
  喂 t= 600ms 78 0a (x\n)
  读 t=[    0.3..  200.5]ms n=2   61 62 (ab) 计数:SIGINT=0 SIGQUIT=0
  读 t=[  200.5..  400.6]ms n=0   () 计数:SIGINT=0 SIGQUIT=0
  读 t=[  400.6..  600.7]ms n=2   78 0a (x\n) 计数:SIGINT=0 SIGQUIT=0
...
```

第一个 ^D 把没写完的 ab 立刻交了出去,连个换行符都不带的,n 报的是 2。第二个 ^D 赶的是空行,read 就返回了 0。这个 0 的长相像 EOF,行为却完全不是一回事:管道和文件的 EOF 意思是关了,之后读到的永远是 0,而 tty 的这个 0 意思是这一下没有行,说的是一次事件,而不是一种状态。读端的第三笔就是证据,同一个 fd 的后续读取,后面喂的 x\n 照样读到,EOF 不粘说的就是这个 0。您在 shell 里敲 ^D 会退出登录,那是因为 shell 的 read 拿到 0 之后自己决定不再读了,不是 fd 出了什么毛病。

raw 的那一场,咱们把三个特殊字节当普通饲料一起喂:

```text
[阶段4 raw:0x03/0x04/0x1c 全是普通字节,一次 read 原样全收]
  喂 t=   0ms 61 62 03 63 64 04 1c 0a (ab^Ccd^D^\\n)
  读 t=[    0.3..    0.3]ms n=8   61 62 03 63 64 04 1c 0a (ab^Ccd^D^\\n) 计数:SIGINT=0 SIGQUIT=0
  master 收到的回显:(无)   孩子退出码=0
```

咱们数一数:同一笔 read 里收了 8 个字节,0x03、0x04、0x1c 咱们全数得着,而两个信号计数器纹丝不动。ISIG 的一清,特殊字符的翻译停了,字节就按字节交了货。

## E4:VMIN 与 VTIME:read 的返回条件自己定

ICANON 关了之后,行的概念没了,read 什么时候返回的问题,咱们就得另找答案,答案就写在 c_cc 的 VMIN 与 VTIME 两格里:VMIN 是每次 read 至少要收的字节数,VTIME 是以 0.1 秒为单位的计时器。设置的代码就这么两行:

```cpp
raw.c_cc[VMIN]  = vmin;    // read 至少收几个字节才肯返回
raw.c_cc[VTIME] = vtime;   // 计时器,单位 0.1 秒
```

基础三档咱们各跑一场,输出都是节选的:

```text
[A: MIN=1 TIME=0 —— 来一个给一个]
  喂 t=   0ms 78 (x)
  喂 t= 300ms 79 (y)
  喂 t= 600ms 7a (z)
  读 t=[    0.2..    0.3]ms n=1  78 (x)
  读 t=[    0.3..  300.5]ms n=1  79 (y)
  读 t=[  300.5..  600.7]ms n=1  7a (z)
...
[B: MIN=4 TIME=0 —— 凑够 4 才交货,散喂也一次拿 4(两轮各 4 字节)]
  喂 t=   0ms 61 (a)
...
  喂 t= 750ms 64 (d)
  读 t=[    0.2..  750.7]ms n=4  61 62 63 64 (abcd)
...
[C: MIN=0 TIME=5 —— 没数据 0.5s 返回 0,有数据立刻拿走]
  喂 t=   0ms ()
  喂 t= 600ms 61 62 (ab)
  喂 t=1200ms ()
  读 t=[    0.2..  530.8]ms n=0  (计时到点,EOF 长相)
  读 t=[  530.8..  600.4]ms n=2  61 62 (ab)
  读 t=[  600.4.. 1102.8]ms n=0  (计时到点,EOF 长相)
```

A 档的脾气是来一个给一个,就是 E2 第四场的样子。B 档凑够了 4 才交,咱们每 250ms 喂一个字母,它硬是攒到了第 4 个才一次交出来,两轮交的都是整 4 字节。C 档把 MIN 归了零,read 变成带超时的轮询,空着的时候 0.5 秒返回 0,又是一个 EOF 的长相,有数据的时候立刻拿走。这三档咱们看着都不难,难的是第四种组合:MIN 与 TIME 都大于 0 的档位。这一档的计时器语义,坊间流传着两个版本的讲法。版本一说的是计时器从首字节起算,计时一到就交货了。版本二说的则是每收一个字节就重置一次计时器,量的其实是字节与字节的间隔。两个版本说的话不一样,咱们摆两场实验让它们当面对质。

第一场是这么安排的,咱们每 200ms 喂 1 个字节、连喂 4 个,彼此的间隔小于 0.5 秒:

```text
[D: MIN=4 TIME=5 —— 每 200ms 喂 1 字节,计时器重不重置,看返回]
  喂 t=   0ms 61 (a)
  喂 t= 200ms 62 (b)
  喂 t= 400ms 63 (c)
  喂 t= 600ms 64 (d)
  读 t=[    0.2..  600.7]ms n=4  61 62 63 64 (abcd)
```

版本一的预言是计时器 500ms 就到点,那时手里攒着的只有 3 个字节,应当交的是 3。版本二的预言是每收一个字节计时器就重置,200ms 的间隔喂不完 0.5 秒的耐心,一路续命到 600ms 第 4 个字节进了门,凑满了 MIN=4,交的就是 4。咱们实测的 n=4,交货的时刻是 600.7ms,版本二赢了。

中间还垫着一场 E:咱们只喂 1 个字节就断供,521.2ms 时计时器到了点,把手里的 1 个字节交了出来。这一场说明的是 read 不会死等 MIN 凑满,计时器到点的时候有多少交多少,保的正是断供这一头。

咱们再看第二场,换个喂法把版本一的退路再堵上一道:

```text
[F: MIN=4 TIME=5 —— a@0ms、b@450ms 后断供:计时器从第 2 字节重起则 ~950ms 交 2 字节;只从第 1 字节起算则 ~500ms 交 1 字节]
  喂 t=   0ms 61 (a)
  喂 t= 450ms 62 (b)
  读 t=[    0.2..  955.7]ms n=2  61 62 (ab)
```

a 是 0ms 进的门,b 是 450ms 进的门,之后就断供了。版本一的预言是 500ms 交 1 个字节,因为计时器只认 a 的起点。版本二的预言是 b 进门那一刻重置,950ms 到了点,交出的会是 2 个。实测的 n=2、时刻 955.7ms,版本二又胜了一局。man 3 termios 对这一档的用词是 interbyte timer,中文译名就是字节间的计时器,它说的本来就是收到每个字节都重置,只是这句话在转述里经常被换成了从首字节起算。对写交互程序的您,这一档的分量在于:用户一个键一个键地慢慢敲,只要键与键的间隔不超过 VTIME,read 就会安安静静地等凑满 MIN,不会半路把半截的输入截走。

## E5:ECHO、OPOST 与 ICRNL:进门出门两个方向各改各的

tty 是个双向的过滤器,进门的方向有 c_iflag 把关,出门的方向有 c_oflag 加工,回显恰好夹在两者的中间。咱们在 E5 里逐个拨的开关有三位,每次只动其中一位、单看它的动静。

头一个咱们看 ECHO。咱们让台架喂 hi\n 进 master,接着咱们把 ECHO 关掉,喂的换成 secret\n:

```text
[阶段1 ECHO:回显是 tty 干的,关掉它,读端照收、屏幕无痕]
  喂(ECHO 开) t=0ms hi\n
    master 侧(回显):68 69 0d 0a (hi\r\n)
  喂(ECHO 关) t=150ms secret\n
    master 侧(回显):(无)
    读端收到 n=3 68 69 0a (hi\n)
    读端收到 n=7 73 65 63 72 65 74 0a (secret\n)
```

ECHO 开着的时候,喂的是 hi\n,master 收到的回显是 hi 加 0d 0a,起作用的又是 ONLCR,回显走的也是输出的路,它也经过了出门的加工。而 ECHO 一关,master 里就干净了,读端的收成却一点没少。您登录时输密码不显示,干的就是关 ECHO 这件事:程序把 ECHO 关掉了,读完了再把它打开。至于回显、行编辑这套服务的归属,E2 已经看到了不归读程序的那半边,开场替 shell 摘干净的那半边,下一篇拿管道与 pty 喂同一串的字节对照,咱们正面看一次。

OPOST 咱们接着看,方向反了过来,管的是程序写出去的字节。孩子在 slave 上两次写同样的三个字节 A\nB,中间咱们把 OPOST 清掉:

```text
[阶段2 OPOST:孩子两次写同样的 A\nB,门里门外差一个 \r]
    master 侧(OPOST 开,第一轮):41 0d 0a 42 (A\r\nB)
    (t=200ms 清 OPOST)
    master 侧(OPOST 关,第二轮):41 0a 42 (A\nB)
```

同样的三个字节,门开的时候 \n 出门变 \r\n,门一关就是原样的三个字节。您的程序只写了一个 \n,终端上却换行了又回到行首,补上的这个 \r,是 OPOST 底下的 ONLCR 干的。咱们把 OPOST 清掉之后,\n 就只剩下换行的语义,光标所在的列是不动的,所以 raw 模式的程序要是不自己发 \r\n,输出会排出阶梯的样子,常在串口上调试的朋友对这样的画面一定不陌生。

最后咱们看 ICRNL,它管的是进门方向的改写:

```text
[阶段3 ICRNL:进门方向,CR 要不要被翻成 NL]
  喂(ICRNL 关) y\r ^D
  喂(ICRNL 开) x\r
    读端收到 n=2 79 0d (y\r)
    读端收到 n=2 78 0a (x\n)
```

ICRNL 关着的时候,咱们喂 y\r,读端是什么都拿不到的,因为 CR 没有行结束符的身份,这行在缓冲里等不来自己的收尾,咱们只得再补一个 ^D 把它催出来,到手的还是原样的 y\r。ICRNL 开着的时候,咱们喂 x\r,0x0d 在门口就被翻成了 0x0a,自己就成了行结束符,直接交了货,读端拿到的是 x\n。所以回车能结束一行,靠的是 ICRNL 替它办的翻译,回车键自己并没有天生的资格。E1 里的最小 raw 清单没清 ICRNL,照着清单写出来的程序读键盘,用户按下回车的时候,您拿到的会是 0x0a 而不是 0x0d,如果您的协议按字节算键,这就是一个对不上的地方。

## E6:terminal_guard:设置挂在 tty 上,谁改的谁收回

termios 不挂在进程身上的这件事,E1 里咱们从 master 与 slave 共享行规程看过一遍,E6 换的角度更扎眼:进程死了,设置却留了下来。阶段A 里的孩子把 tty 设成 raw 之后就直接 `_exit`,咱们不给它安排任何善后:

```text
[阶段A 无 guard:改完就跑,raw 留在 tty 上]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子A(_exit 无善后)之后 lflag=0xa30 ICANON=0 ECHO=0 ISIG=0
  孩子B(没设过任何东西)读到 n=1 (a) —— 继承了 raw,没换行也到货
  孩子B(没设过任何东西)读到 n=1 (b) —— 继承了 raw,没换行也到货
```

孩子A 退了,lflag 从 0x8a3b 变成了 0xa30,raw 就原样滞留了下来。接力的孩子B 什么都没设,喂进去的 a 和 b 各自成笔到货,没等到换行就交了货,它继承了一只 raw 的终端。真终端上这一幕您多半见过:程序崩在 raw 模式里,终端不回显了、^C 也失灵了,救回来的指望只有 `stty sane`。咱们台架上救它只需要一次还原,但还原的这件事,指望人肉是不牢靠的,该交给的是析构函数。存档 e6 里的写法就是本篇的 C++ 落点:

```cpp
class terminal_guard {
public:
    explicit terminal_guard(int fd, int restore_actions = TCSAFLUSH)
        : fd_(fd), restore_actions_(restore_actions) {
        if (tcgetattr(fd_, &saved_) != 0)
            throw std::runtime_error(std::string("tcgetattr: ") + std::strerror(errno));
    }
    void apply_raw() {
        termios t = saved_;
        cfmakeraw(&t);
        if (tcsetattr(fd_, TCSANOW, &t) != 0)
            throw std::runtime_error(std::string("tcsetattr: ") + std::strerror(errno));
    }
    ~terminal_guard() { tcsetattr(fd_, restore_actions_, &saved_); }
    terminal_guard(const terminal_guard&) = delete;
    terminal_guard& operator=(const terminal_guard&) = delete;

private:
    int fd_;
    int restore_actions_;
    termios saved_{};
};
```

构造的时候 `tcgetattr` 把现场拍下来存底,`apply_raw` 在存底的基础上做 cfmakeraw 再下发,析构时按构造时选定的动作还原,拷贝则直接删掉了。这套骨架您在[思维基石的 RAII 篇](../../thinking/01-raii-paradigm.md)里见过同款:资源是终端的 termios 状态,获取的动作是 tcgetattr,释放的动作是 tcsetattr,异常安全靠的仍是析构在栈展开时照跑。实验给它排了正常与异常两条路:

```text
[阶段B 有 guard,正常返回:析构还原]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子正常返回后 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1

[阶段C 有 guard,中途 throw:栈展开照样还原]
  初始 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子抛异常被接住后 lflag=0x8a3b ICANON=1 ECHO=1 ISIG=1
  孩子报告的异常:模拟业务炸了
```

咱们看两条路走完后的 lflag:都回到了 0x8a3b,与初始的值逐位相同。业务炸了,终端还好好地活着。

还原动作还有一个选择藏在构造参数里:tcsetattr 的第三个参数一共三个值,除了咱们用到的 TCSAFLUSH 与 TCSANOW,剩下的第三位叫 TCSADRAIN,它等已写的输出排空才生效,还原终端的时候用不上它。咱们用到的两个值,差别在于还原那一刻冲不冲没读的输入,TCSAFLUSH 冲的时候还会等输出排空,本篇的输出量小,现场是看不出来的。阶段D 专门排了对照:孩子握着 raw 的终端睡 200ms,期间咱们往队列里塞了 4 个字节的 junk 却没人读,等孩子 throw、guard 把状态收了回来,接力的读端就在还原后的 canonical 里等行:

```text
[阶段D1 guard 用 TCSAFLUSH:还原时冲掉没读的输入]
  接力读端读到 n=2 (z
) —— junk 被 TCSAFLUSH 冲了

[阶段D2 guard 用 TCSANOW:还原时保留没读的输入]
  接力读端读到 n=4 (junk) —— junk 滞留,混进了这一行
```

D1 那两行的原始长相咱们原样贴上了,中间断的行,就是读到的 \n 本身。

D1 冲掉了 junk,接力读端只读到咱们后来补喂的 z\n。D2 里它被保留了,而且请您注意交货的方式:4 个字节的 junk,后面咱们只补了 z\n 作为行结束,可接力的一笔 read 拿到的是 n=4 的 junk,z\n 是压根没进这次 read 的。canonical 的等行条件只约束切换之后收到的字节,切换之前已经躺在队列里的存货,切回 canonical 的那一刻就是一笔立等可取的交货,连行结束符都给免了。这就是 guard 的还原默认选 TCSAFLUSH 的理由,它防的是 raw 期间滞留的字节。这些字节的来历是程序自己的按键协议,它们会原样混进切换后的第一笔 read,后面接手的可是 shell 这样相信行内容的程序。

## 下一站:pty 与另一侧的控制台

本篇的台架把 openpty 当装置用了六场,下一篇轮到装置本身:master 为什么能喂键、slave 为什么算终端,`posix_openpt` 到 `grantpt`、`unlockpt`、`ptsname` 的四步各做什么,`forkpty` 一行怎么顶咱们十几行,一场会话怎么录成 typescript 加 timing 再一比一放回来,这些是下一篇的正题。E2 与 E5 里回显和行编辑的最终归属,也就是 tty 层的服务、读程序与 shell 都不沾边的问题,那边会拿管道与 pty 喂同一串的字节对照着看,比咱们在这儿下判语结实。

Windows 的那一侧没有 termios,同型的面板叫控制台模式,`SetConsoleMode` 是按句柄设置的,行模式、回显、处理输入的开关各占一位,Windows 侧的这一章在[总纲](../../00-overview.md)的 ch06 里也挂着号。^C 在那一侧怎么变成事件递给进程,Windows 进程篇的[控制台事件与 APC](../../windows/process/02-console-apc.md)已经讲过,控制台模式位的实测,咱们留给本卷 Windows 侧的控制台篇。termios 的这块面板,咱们今天算是每个开关都亲手拨过了一遍。
