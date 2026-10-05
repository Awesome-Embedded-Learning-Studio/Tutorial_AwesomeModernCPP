---
title: "信号(上):sigaction 与异步信号安全"
description: "信号怎么到达、handler 里能做什么,本篇七组实测正面回答:阻塞期连发 3 次 SIGUSR1 只 pending 1 位、解阻塞后 handler 只跑 1 次,投递精确落在解阻塞那次 sigprocmask 的返回路上,handler 行永远插在主循环两行 write 之间从不劈行;sigaction 家族实测 SA_SIGINFO 三种 si_code(raise 的 SI_TKILL、他人 kill 的 SI_USER、itimer 的 SI_KERNEL)、sa_mask 连自身一起屏蔽、SA_NODEFER 同信号嵌套、SA_RESETHAND 二次触发死于 SIG_DFL,signal() 读回 sa_flags=0x14000000=SA_RESTART|glibc 内部 SA_RESTORER(蹦床用);重头戏是异步信号安全:handler 里 printf 的增行大头是形态完好的整行重复(独立复跑实测 648 个行号各完好出现两次,缓冲写入位置回卷后同一行从头重拷,grep 完好模式一个也筛不出),少数才是碎片——存档 run 的损坏帧索引恰好 21 帧(最狠一帧 M 009[H 行号拷到一半被插,还有无 handler 痕迹的纯损坏帧,病根是共享缓冲的半更新而非交错),glibc 2.44 的 stdio 锁同线程递归、教科书式死锁没出现,出现的是状态损坏,write 安全指不损坏自身状态但 DONE 仍被粘进未 flush 的缓冲行;可重入篇缺 volatile 的忙等被 -O2 提出加载、汇编只剩 jmp .L5、循环里连圈数计数都没有,timeout 3 秒杀到 exit 124 输出停在 busy-waiting,volatile 版每圈 movl、5.38 亿圈后干净退出;工程模式 flag/self-pipe/siglongjmp 三件;SIGCHLD 三子退出合流 1 位、循环 waitpid 收 3 而 naive 只收 1 留俩僵尸;收尾 SIGKILL/SIGSTOP 不可捕获实测(EINVAL)与常用信号速览表"
chapter: 8
order: 4
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 30
prerequisites:
  - "错误处理范式:从 errno 到 expected"
  - "进程创建与生命周期:fork/exec/posix_spawn"
related:
  - "信号(下):实时信号、signalfd 与 pidfd"
  - "IPC:管道、FIFO 与 POSIX 消息队列"
  - "虚拟内存 API 全景:mprotect/madvise/mlock"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - POSIX
  - 并发
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 信号(上):sigaction 与异步信号安全

进程之间传数据的路子,咱们在前面几篇里走得差不多了,fork 出来的血缘,exec 换进来的新程序,管道里流的字节,都过了一遍。这一篇轮到另一种更原始的通信:信号。别的进程一句 `kill`,或者内核自己的一次定时到期、一次非法访存,就能让您这段程序的执行流当场改道,去跑一段您登记过的函数,跑完了再回来接着干咱们手头的事。也因为这段函数可能出现在任意的两条指令之间,它里面能做什么、不能做什么,就成了一门必须拿实测说话的学问,而本篇实测出来的几处答案,跟不少教科书的说法并不一样。

[错误处理篇](../../thinking/02-error-paradigm.md)用 strace 拆过 SA_RESTART 的行为:阻塞中的 read 被信号打断后,交回来的就是 -1 与 EINTR,而 SA_RESTART 让内核代为重启,这一对配对的细节都在那边的正文里,本篇就不再重讲了。那边看的是被打断的系统调用怎么办,咱们今天要正面回答的,是两个更根本的问题:信号到底怎么到达,以及 handler 里头到底能做什么。第二个问题的答案,笔者第一次跑出 E3 那 21 帧损坏的输出时,对着屏幕看了很久,教科书说的死锁没有出现,出现的东西反而更值得写下来。

实验的编号是 E1 到 E7,与仓库 `code/volumn_codes/vol8/systems-programming/linux/process/04-signal-basic/` 下的 01 到 07 七个目录一一对应,代码连同原始的输出都收在存档里,README 里还有逐实验的复现命令,您随时能对表。正文里的输出块多数是节选,删掉的行以 `...` 标出,拿存档对表的时候请以存档为准。本篇的 E 只认本篇,内存管理那边各篇自己的 E 系与咱们互不相干,您翻存档的时候认目录号就好,子实验的出场顺序按的是叙事,而不是编号。系列的公共工具 `unique_fd`、`sys_call`、`errno_code` 沿用思维基石两篇的定义,不过本篇的实验基本都是裸调用,它们一次都没有出场——handler 的世界里,咱们几乎一件 C++ 的东西都不带,连 printf 咱们都不碰,至于为什么,您到 E3 就明白了。

环境的口径照例交代清楚,后面的数字都要拿它对表:实验出自笔者的 WSL2,内核是 6.18.33.2-microsoft-standard-WSL2 的构建,g++ 用的是 16.2.1,glibc 用的则是 2.44,编译的口径一律是 `-std=c++20 -Wall -Wextra -O2`,咱们跑下来一个警告都没有。`-O2` 并不是笔者的偏好,E4 的反例就靠它才成立。输出里的 pid 每次运行必变,所以咱们引用的都是等式和关系,而不是哪个具体的数值。还有一条查文档的口径:本机没装 man7 的本地手册页,man 7 signal-safety 的安全名单,咱们是对着 man7.org 的在线版核过原文的,后文的引用都以它为准。

## E1:投递模型,标准信号只记一位

咱们把信号的一生分成三步看。生成(generate):有人调了 kill,或者内核自己产生了它,内核就在进程的身上记下了这件事。pending(待处理):信号已经生成了、还没轮到跑,而内核给每个信号留下的只是一个位,而不是一个队列。投递(deliver):内核真正地暂停主流程,把 handler 叫起来的那一下。这三步里最容易想错的是第二步,咱们拿实验把它落实:阻塞期里连发三次,位图上多出来的只有一个 1。

### 三条发送路径与一位 pending

发送的路径不止一条。直接调用的 `kill(getpid(), SIGUSR1)` 是一条路,对自己发的 `raise(SIGUSR1)` 是第二条,让子进程从外面发的 `kill(getppid(), ...)` 是第三条。咱们在 E1.1 里把三条路各走了一遍,handler 的计数都是 1:

```text
[E1.1] three send paths, pid=15070 uid=1000
    [H] SIGUSR1 handler entered
[E1.1] path kill(getpid())      -> handler count=1
    [H] SIGUSR1 handler entered
[E1.1] path raise(SIGUSR1)      -> handler count=1
    [H] SIGUSR1 handler entered
[E1.1] path child kill(parent) -> handler count=1
```

路径本身倒是不稀奇,值得记的是 raise 的底层走法,咱们把它留给 E2 当伏笔。真正要紧的实验在 E1.3:咱们用 `sigprocmask(SIG_BLOCK, ...)` 把 SIGUSR1 屏蔽掉,屏蔽的意思是内核把这个信号拦在 pending、暂不投递,然后咱们连发三次,再回头读它给出的位图。`sigpending` 读回来的就是那个位图:

```text
[E1.3] block SIGUSR1, send it 3 times while blocked:
[E1.3] after 3 sends:         pending: SIGUSR1=1 SIGUSR2=0 SIGALRM=0
[E1.3] handler count while blocked = 0 (never ran)
    [H] SIGUSR1 handler entered
[E1.3] after unblock: handler count = 1  <- 3 sends, ONE delivery
[E1.3] after unblock:         pending: SIGUSR1=0 SIGUSR2=0 SIGALRM=0
    [H] SIGUSR1 handler entered
[E1.3] control run: 1 send while blocked -> handler count = 1 (same as 3 sends)
```

发了三次,pending 里留下的只有一个 1。而在解阻塞之后,handler 也只跑了一次。对照组更有意思:咱们再屏蔽一回,这一回咱们只发一次,handler 的计数同样是 1。发三次与发一次的结果,是完全一样的,man 7 signal 的原文写得很直白:标准信号不排队,同一个信号在阻塞期间生成了多次,标记成 pending 的也只有一次。位图上能写的只有 0 和 1,多出来的两次在生成的那一刻就被并掉了,丢失 bug 的说法并不成立,它就是标准信号的定义。要排队的信号叫实时信号,就归下一篇去讲了。

投递的时机也在上面的输出里。您看第三行与第四行:handler 计数为 0 的 printf 打完,[H] 行就跟着出现了,然后才是解阻塞之后的 printf。投递精确地落在解阻塞那次 `sigprocmask` 的返回路上。内核在回到用户态的关口检查有没有新解除屏蔽的 pending 信号,所以 handler 在下一条用户指令之前就被叫了起来,叫的时机不早也不晚,咱们肉眼能看到它卡在哪两条语句之间。

### [H] 永远插在两行之间,从不劈行

咱们在 E1.2 里换一个观察角度:handler 的输出与主循环的输出搅在一起,会是什么样?主循环用 `write(2)` 一行一次调用,而子进程每隔 60 毫秒发一次 SIGUSR1:

```text
...
MAIN iter 1 (t=0ms)
MAIN iter 2 (t=20ms)
    [H] SIGUSR1 handler entered
MAIN iter 3 (t=40ms)
MAIN iter 4 (t=60ms)
MAIN iter 5 (t=80ms)
    [H] SIGUSR1 handler entered
MAIN iter 6 (t=100ms)
MAIN iter 7 (t=120ms)
MAIN iter 8 (t=141ms)
    [H] SIGUSR1 handler entered
[E1.2] observation: handler lines always sit BETWEEN main lines, never split one
```

咱们跑了多轮,[H] 行永远整行整行地落在主循环的两行之间,劈成两半的事一次都没出现过。机制上的解释是:内核受理信号的位置在指令边界,准确说是系统调用返回、回到用户态的这类关口上,而不是在一次 write 的内部。write 的系统调用要么整个做完,要么按 EINTR 的约定整个作废重来,所以输出行不会被从中间撕开。这样的整齐是 write 给的,同样的混排观察,等咱们到 E3 里换成 printf,画风就会彻底变了样。

## E2:sigaction 家族:flags、屏蔽与 si_code

登记处置的活,咱们只用 sigaction 一家。处置(disposition)说的是这个信号来了该怎么办:走默认动作的 SIG_DFL,直接忽略的 SIG_IGN,还是进您写的 handler。咱们在错误处理篇用它装过最朴素的 handler,本节就把常用的 flags 挨个过一遍。

### SA_SIGINFO:谁发的,查得到

咱们从 handler 的原型看起:简单的一种是 `void(int)`,只拿得到信号的编号。加上 SA_SIGINFO 之后换成三参数的版本,第二个参数 `siginfo_t*` 里装着这次生成的细节:谁发的,怎么发的。E2.1 实测了三种来路,si_code 这个字段专门负责怎么发的:

```text
[E2.1] SA_SIGINFO: sender is identifiable, my pid=15082 uid=1000
    [H] caught 10: si_signo=10 si_code=-6(SI_TKILL  raise/tgkill发来) si_pid=15082 si_uid=1000
    [H] caught 10: si_signo=10 si_code=0(SI_USER   kill()发来) si_pid=15083 si_uid=1000
[E2.1] child pid=15083 sent it -> si_pid must equal this
    [H] caught 14: si_signo=14 si_code=128(SI_KERNEL 内核产生) si_pid=0 si_uid=0
```

咱们对着表看。第一行是 `raise` 发的,读到的 si_code 是 -6(SI_TKILL),而 si_pid 就是咱们自己,E1 埋的伏笔在这儿兑现:glibc 的 raise 底层走的是 tgkill 系统调用,内核按调用来源把它标成了 SI_TKILL。第二行是子进程 `kill` 过来的,它的 si_code 是 0(SI_USER),si_pid 给的是 15083,和下一行打印的 child pid 逐位相等,您要的答案就在这个字段里。第三行是 `setitimer` 定时到期、内核自己产生的 SIGALRM,报上来的 si_code 是 128(SI_KERNEL),si_pid 记的是 0,没有发送者的进程。咱们测得到的三种常用码到这里就齐了,而 si_code 的名单不止这些:SI_QUEUE 是 sigqueue 发送、带附加数据的信号,属于实时信号的那一类,连同其余实时信号的码,咱们都留给下一篇。

> 交代一下实验自身的分寸:E2 的 handler 里用了 snprintf 格式化再 write,而 snprintf 其实也不在异步信号安全的名单上(一张 POSIX 定的名单,管的是哪些函数进得了 handler,判据咱们到 E3 开头给),咱们为了把 si_* 字段原样打出来才这么干。守法到底的写法长什么样,E3 演给您看。

### sa_mask:handler 期间,自己也在屏蔽之列

handler 跑的时候,进程的屏蔽字会临时扩一圈:sa_mask 里列出的信号全部加进屏蔽,触发 handler 的那个信号自己也自动加了进去,除非您用 SA_NODEFER 明确说不。这套设计的用意,是让 handler 安心地跑完,而不被自己人打断。E2.2 在 handler 里现场读了屏蔽字和 pending,把这件事变成了看得见的数字:

```text
[E2.2] sa_mask={SIGUSR1}: USR1 cannot preempt USR2 handler
    [H] SIGUSR2 handler entered
    [H] mask inside handler: SIGUSR2=1 SIGUSR1=1 (self + sa_mask both blocked)
    [H] raise(SIGUSR1) inside handler -> pending=1 (NOT delivered while we run)
    [H] SIGUSR2 handler returning
    [H] SIGUSR1 handler runs only AFTER SIGUSR2 handler returned
[E2.2] back in main
```

咱们给 SIGUSR2 装的 handler,sa_mask 里只写了 SIGUSR1。可 handler 内部读出的结果里,SIGUSR2 自己也是 1:自动屏蔽把自己也算进去了。咱们在 handler 里再 raise 一个 SIGUSR1,它只进了 pending,整个 handler 执行的期间都投不进来。等 SIGUSR2 的 handler 干完返回,SIGUSR1 的 handler 才补上这一场。两个 handler 一前一后地跑完,嵌套是没有的,这就是 sa_mask 加自动屏蔽合起来的效果。

### SA_NODEFER 与 SA_RESETHAND:两个开关,两种脾气

SA_NODEFER 则把咱们刚才说的自动屏蔽关掉了:同一个信号在 handler 里再来,它立刻就递归地进来了,而不去 pending 排队。

```text
[E2.3] SA_NODEFER: same signal re-enters its own handler
    [H] enter, depth=1
    [H] enter, depth=2
    [H] leave
    [H] leave
[E2.3] done (depth back to 0; without SA_NODEFER the inner raise would stay pending)
```

咱们看 depth:它从 1 涨到了 2,再退了回来,同信号的 handler 真的套了自己一层。这个开关要配套一个清醒的认识:嵌套的层数只受栈容量的限制,递归进来的 handler 跑在同一个栈上,一个高频信号配上一个爱 raise 自己的 handler,栈就危险了。

而 SA_RESETHAND 又是另一种脾气:handler 进门一次,处置就被重置回了 SIG_DFL,到了第二次再来,就直接走默认的动作。SIGUSR1 的默认动作是终止,咱们让 E2.4 的这一切发生在一个子进程里,为的是方便父进程收尸读结果:

```text
[E2.4] SA_RESETHAND: one-shot handler, 2nd delivery hits SIG_DFL
    [H] first delivery: handler body runs
CHILD: survived 1st; raising again with disposition reset to SIG_DFL (terminate)
[E2.4] child ended: WIFSIGNALED=1 WTERMSIG=10 (SIGUSR1=10) -> reset happened
```

第一次 raise 的时候,handler 的正文跑了。到了第二次 raise,`CHILD: never printed (dead)` 那一行永远没有机会再打印了,孩子死于 SIGUSR1 的默认动作,`WIFSIGNALED=1 WTERMSIG=10` 是父进程验尸的报告。一次性的 handler 在 Unix 的历史上是 System V 那一系的作风,马上咱们就会在 signal() 里再遇到它。

### signal() 的 glibc 语义:0x14000000 里有一位是蹦床

`signal()` 是老资格的接口,历史包袱出名地重:同一条 `signal(sig, handler)`,System V 的语义是 handler 用一次就重置,装的时候也不屏蔽自己。BSD 的语义是 handler 持久,期间把自己也屏蔽了,被打断的调用还会自动重启。glibc 替咱们装的是哪一边?咱们不用翻文档猜,装完之后咱们拿 sigaction 把 flags 读回来:

```text
[E2.5] signal(): glibc installs BSD semantics
[E2.5] signal() returned old disposition: (non-null)
[E2.5] readback sa_flags=0x14000000: SA_RESTART=1 SA_NODEFER=0 SA_RESETHAND=0
[E2.5] raised twice -> handler count=2; handler PERSISTS (System V semantics would reset after 1st and die on 2nd)
```

读回的 sa_flags 是 0x14000000,分开看是两位:其中的 0x10000000 是 SA_RESTART,剩下的 0x04000000 是 SA_RESTORER。前者就是 BSD 语义的证据,man 2 signal 也写明了,glibc 的 signal() 里面,装的就是带 SA_RESTART 的 sigaction。后者不进公开的 flags 之列,man 2 sigaction 的清单里其实有它,标注写的是内部使用、不供应用调用,它是 glibc 传给内核的内部位。sigaction 的包装函数会把一段小代码的地址塞进 sa_restorer 字段,再把这个位也置了上去。而 handler 返回之后,跳到的是那段代码,它负责发起 rt_sigreturn 的系统调用,现场就这样交还给了内核。这类垫在中间、负责把控制流送回去的小代码,行话的名字叫 trampoline,中文的名字就是蹦床。所以咱们连发两次,handler 的计数是 2,System V 式的第二次死亡在咱们的机器上并不存在。想显式要一次性的行为,SA_RESETHAND 是唯一的正路。man 2 signal 的劝告也转给您:真要装 handler,请您改用 sigaction,语义全在您自己手里。

### sigprocmask 三件套

屏蔽字的三个操作,行为是各不相同的,咱们把一份实测的序列摆出来,就全说清楚了:

```text
[E2.6] sigprocmask: BLOCK=并集, UNBLOCK=差集, SETMASK=整个替换
    mask initial (empty)                    SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=0
    mask BLOCK{USR2}                        SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=1
    mask BLOCK{USR1,USR2}                   SIGINT=0 SIGTERM=0 SIGUSR1=1 SIGUSR2=1
    mask UNBLOCK{USR2}                      SIGINT=0 SIGTERM=0 SIGUSR1=1 SIGUSR2=0
    mask SETMASK{} (clear all)              SIGINT=0 SIGTERM=0 SIGUSR1=0 SIGUSR2=0
```

BLOCK 做的是并集,只添而不减。UNBLOCK 做的是差集,只减而不添,USR1 留下来的样子就是证明。SETMASK 做的是整个替换,传进去的要是空集,就是全清了。三件套的语义都不难,难的是 UNBLOCK 并不会顺手替您清掉别的位,屏蔽逻辑写复杂了之后,这里头最容易想当然。

## E3:异步信号安全:多出来的行是什么

开头咱们留过一句话:handler 的世界里,咱们几乎一件 C++ 的东西都不带,连 printf 咱们都不碰,至于为什么,您到 E3 就明白。现在到 E3 了。异步信号安全(async-signal-safe)是 POSIX 给的一张名单:一个函数在名单上,意味着您在 handler 里调它不会出事。而不在的,就没有任何承诺了。名单的判据,man 7 signal-safety 的原话是:函数要么可重入,要么对信号而言是原子的。名单里远远找不着 printf 的影子,write 倒是排在表上。教科书讲到了这里,跟着补的通常还有一句,说 handler 里的 printf 会死锁,咱们拿实验看看,在 glibc 2.44 的本机上,实际发生的到底是什么?

### 两份同源的程序,只差 handler 里的那一行

咱们看实验的编排:itimer 每 80 微秒到期一次发 SIGALRM,setitimer 定时到期的通道 E2.1 刚用过。主循环用 printf 打 30000 行固定格式的短行。两份程序的差别,只在 handler 与缓冲的策略。翻车版的 handler 里是 printf,输出重定向到了文件里之后,stdio 自动进入了全缓冲,handler 与主循环用的就是同一个缓冲:

```cpp
// unsafe_printf.cpp 的 handler,翻车的原因就在这一行
void on_alrm(int)
{
    g_ticks = g_ticks + 1;              // volatile sig_atomic_t,C++20 起复合赋值弃用,写全
    std::printf("[H tick %d]", (int)g_ticks);   // 不在安全名单上
}
```

咱们看安全版的 handler:里面只干两件极小的活,write 一个固定的常量串,外加把标志位加了一。主循环那边则把 stdout 设成了无缓冲,一行 printf 就是一次直落的系统调用:

```cpp
// safe_write.cpp 的 handler
void on_alrm(int)
{
    g_ticks += 1;
    write(STDOUT_FILENO, "[H tick]\n", 9);   // 在安全名单上
}
```

两版各跑了一次。翻车版的全量输出是 30585 行:30000 条主循环行加 23 条 handler 输出,本该停在 30023 行上下的,却多出来了 562 行。安全版给的是 30254 行,恰好等于 30000 行的主循环,加上 253 次的 tick,再加 1 行的 DONE,多一个字符、少一个字符的事都没有,损坏的帧数是零。两个 run 的 tick 数一为 23 一为 253,这并不矛盾:缓冲的策略决定了主循环的快慢,timer 落点的数量跟着变,两组数字各对各的 run,咱们不把它们拼在一起算差。

那翻车版多出来的 562 行,长的都是什么样子?咱们按存档的源码和同一条命令,又独立复跑了一遍来验构成。复跑的全量是 30658 行,形态不完好的行恰好只有 21 行,而膨胀的大头,是 648 个行号各以完好的形态整行出现了两次,还常常连成了一小段,比如从 001171 到 001175 的一段,连着每个行号都来了两遍。grep 按完好模式筛损伤的时候,能筛出来的只有那 21 帧,增量的大头它一个也筛不出来:那些行的形态挑不出任何毛病,只是整个多了一份。存档 run 的损坏帧索引里,数出来的同样恰好是 21 帧——两次 run 的损坏位置各不相同,数字撞成了同值,结果纯属巧合,咱们后文引用的帧,也都出自存档的那次 run。

### 损坏的姿势不止一种

咱们下面摆的五帧,取自存档 run 的损坏帧索引,行号是 grep 在存档全量输出里数出来的位置:

```text
1111:[H tick  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
2531:[H tick 2]M 002530 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
3950:M 003949 abcdefghijklmnopqrstunopqrstuvwxyz0123456789ABCDEFGHIJ
9582:M 009[H  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
30574:defghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
```

咱们一帧一帧看过去。1111 行是 handler 自己的输出,数字没了,剩下的只有 [H tick 和两个空格。2531 行丢了换行符,handler 的尾巴和主循环的下一行粘成了一行。30574 行是一个孤儿的碎片,它本来的那一行被拦腰截断了,后半截自己单独成了一行。这些还都是两个 printf 交错抢写留下的直接后果。

### 最狠的一帧与最冷的一帧

机制看得最清楚的一帧是 9582 行,咱们把它前后各几行一起看:

```text
...
M 009433 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009434 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009[H  abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009436 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 009437 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
```

咱们看主循环这边:它正在拷 M 009435 这一行的行号,拷到 009 信号就到了。handler 的 printf 接手同一个缓冲,写下了 [H 和一个空格。而主循环恢复之后,按自己记下的位置继续拷,handler 的数字又被主循环的字节压掉了。双方的输出各剩半截,同一行里既有主循环的断头,又有 handler 的残肢,这就是损坏最狠的一帧。

咱们再来看 3950 行,最冷的一帧:主循环自己的行,字符凭空少了几个,整行里没有任何 handler 的痕迹。这一帧才是真正的病根所在。它是没法用两个 printf 抢着写来解释的,抢写好歹要有两方的出场,而这一帧里只有主循环一个人。真实的机制是:printf 每打一行,都在更新流内部的缓冲位置指针和计数器,信号打断在这个半更新的状态上,handler 的 printf 又按自己的理解推进了同一批状态。主循环恢复执行的时候,它手里的指针已经指错了地方。而指错的方向,咱们分两种看:指针跳向前面的,接下来的拷贝就漏了字符、错了位置,长出来的就是碎片。指针卷回后面的,同一行就被从头再拷了一遍,长出来的就是一行形态完好的重复,复跑里那 648 个行号的重复就是回卷留下的,grep 的完好模式在它们面前一个也抓不着。man 7 signal-safety 对 stdio 的判词正是这一句:它们维护着静态分配的缓冲,以及相关的计数器和索引(或指针)。所以 printf 不安全的病根是共享状态,碎片的交错与完好的重复,都只是它的症状。把 handler 里的 printf 换成任何别的会动这块状态的东西,损坏照样还是会来的。

### 教科书说会死锁,实测没死,坏得更深

教科书式的说法是 handler 里 printf 会死锁:主流程的 printf 拿着 stdio 的锁,信号打断了它,handler 的 printf 再去拿同一把锁,结果自己等自己。咱们那 30585 行的实测里,这个死锁倒是没露面,进程从头跑到了尾。原因在 glibc 的锁实现里:它的 stdio 锁是同线程可重入的递归锁,锁里记着持锁者和递归的计数,同一线程的再次拿锁只是计数加一,并不会把自己挡在了门外。所以在 glibc 2.44 的本机上,那个死锁换了一副面孔出现:锁不挡您,共享的缓冲状态照样被劈坏,21 帧损坏就是它留下的现场。分寸咱们也要交代全:printf 内部还会走 malloc,handler 若打断的是别的持锁路径,比如堆的锁,死锁依然是有可能的,咱们的编排没有遇上它,咱们也就不断言它不会发生。把死锁从说法里划掉了,把状态损坏写了进去,这是本篇翻的第一处教科书级说法。E1 纠正过的丢失 bug 只算咱们的常见误读,还够不上教科书的那一级。

### write 安全,安全在哪一侧

对照版的安全,咱们把数字摆在前面了:30254 行,精确地对上了,一帧也没有损坏的。write 为什么行,答案还是安全名单的判据:它是一次系统调用,缓冲、锁、位置计数的活全在内核侧一次做完,用户态里没有能被信号劈在中间的半更新状态。它属于对信号而言是原子的那一类,与 printf 那个可重入的反面,正好凑成了判据的两半。

安全版里还有一处细节值得您多看一眼:handler 打的 [H tick] 永远独占一行,主循环的行永远不会被碰:

```text
...
M 000104 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 000105 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
[H tick]
M 000106 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
M 029999 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
DONE ticks=253 (flag observed by main loop: 253)
```

E1.2 说过 write 的行不会被劈开,这里咱们又用 30000 行的体量验了一遍,连一条劈开的都没有。

### DONE 被粘进别人的行

write 安全这句话的边界,咱们也要画清楚。翻车版的程序收尾时改用 write 直写了一行 DONE,因为此刻的 printf 已不可依赖,结果它没有独占一行,而是粘在了主循环的行里:

```text
...
M 029987 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 029988 abcDONE ticks=23
defghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
M 029989 abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ
...
```

当时 stdio 缓冲里还压着没 flush 的主循环内容,write 直写的字节落到了文件里更靠前的位置,于是 DONE 插进了 M 029988 那一行的肚子,那一行的后半截被挤出来单独成行。write 的安全说的是它不损坏自身的状态,从来不是说它会替您和 stdio 缓冲排队:同一个 fd 上混用两条输出通道,顺序得您自己保证。safe 版把 stdout 设成了无缓冲,正是为了让主循环也走直写的道,两条路并成了一条,时序才是可比的。这是本篇按实测修正的第二处常见说法:write 安全,您别把它读成 write 万能。

## E4:可重入性:一个标志的两层保证,两行汇编

E3 的名单判据里有个词咱们还没正面讲:可重入(reentrant)。它说的是,同一段的代码执行到一半,又被再次进入了,第二次不会踩坏第一次的进度。而 handler 天生就在制造这样的进入,所以可重入性直接决定了一个函数、一个变量能不能进 handler。咱们从最基础的共享标志讲起,这里有一个编译器送上的、教科书级别的反例。

### volatile sig_atomic_t:缺一个就翻

handler 与主循环之间传的消息,最朴素的载体是一个全局的标志。C 标准为此备了一个专门的类型 sig_atomic_t,它保证这个整数的读取和写入作为整体一次完成,劈开是劈不开的。而 volatile 修饰保证的是,编译器每次都真的去读内存,不拿寄存器里的旧值凑合。两个保证各管了一半,合起来的才是 volatile sig_atomic_t。咱们用同一份源码、两种编译跑了 E4a,证明缺 volatile 的下场:

```text
busy-waiting for g_flag (SIGALRM at +100ms)...     <- 两种编译的开头一样
```

咱们看 plain 版,它有 sig_atomic_t 的保护、却没有 volatile,在 -O2 下被 `timeout 3` 杀掉了,退出码给的是 124,输出永远地停在了 busy-waiting 那一行。volatile 版则干净地退出了:

```text
busy-waiting for g_flag (SIGALRM at +100ms)...
observed g_flag=1 after 538301099 spins -> clean exit
```

编译的是同一份源码,volatile 版转满 5.38 亿圈等到了标志,plain 版呢?它的循环体连圈数计数都被优化没了,转到 timeout 把它杀掉了,连那一次的看见都没有等来。handler 明明跑了,内存也明明写了,plain 版的循环就是看不见。汇编替咱们把原因写明白了。plain 版的循环体:

```asm
	movl	g_flag(%rip), %eax      # 加载一次,提出循环
	testl	%eax, %eax
	jne	.L4
.L5:
	jmp	.L5                     # 死循环本体,再也不读 g_flag
```

咱们再看 volatile 版的循环体:

```asm
.L5:
	movl	g_flag(%rip), %eax      # 每一圈都重新加载
	addq	$1, %rsi
	testl	%eax, %eax
	je	.L5
```

一边是加载被提出了循环,整个的忙等只剩一条 `jmp .L5`,flag 在循环里就等于不存在了。而另一边每一圈都有一次 movl 真读内存。编译器并不知道 handler 的存在,它眼里的循环体没人写 g_flag,把读优化掉是完全合规的。所以编译器没有错,错的是咱们没把还有别人会写这块内存的事实告诉它,volatile 说的就是这句话。sig_atomic_t 管的是读写不可分割,volatile 管的是每次真的去读,谁也替代不了谁。

### strtok 与 localtime:住进静态存储的进度

函数级的可重入性,危险常常出在静态的存储上。有些函数把进行中的状态存在了函数内部的静态变量里,到了第二次进入,第一次的进度就被覆盖掉了。E4b 用单线程下确定合法的调用序列,模拟了 handler 打断的效果,行为是确定的,咱们看得明明白白:

```text
== strtok: the in-progress pointer lives in static storage ==
[strtok] main strtok #1 -> alpha
[strtok] (handler-style) strtok -> X   <- 静态进度被覆盖
[strtok] main strtok #2 (expect beta) -> Y   <- CORRUPTED

== localtime: the result struct itself is static ==
[localtime] first  localtime -> 2001-09-09 01:46:40
[localtime] second localtime -> 2014-05-13 16:53:20
[localtime] re-read pointer a   -> 2014-05-13 16:53:20   <- a silently became t2
```

strtok 的进度指针住在静态存储里。主流程刚取完了 alpha,中断里又来了一次 strtok,静态指针就被改得指向了另一条串的 Y。而主流程恢复后接着取,以为该拿 beta 的那一次,实际拿到的是 Y。而 localtime 还要更隐蔽,它返回的指针指向函数内部的静态 struct tm,第二次的调用直接改写同一块存储,您手里的指针 a 没动过一个字节,它指向的内容却已经无声地变成了 t2 的时刻。

### _r 后缀与安全名单

它们各自都有可重入的版本,也就是带 `_r` 的 `strtok_r` 与 `localtime_r`,把状态搬到了调用方给的缓冲里,名字里的 `_r` 就是为此发明的。但这里有一句常见的话不能照说:并不是带 `_r` 就进了安全名单。咱们对着 man 7 signal-safety 的名单核过原文:strtok_r 在表上,strtok 落在了表外。而 localtime 一类整个不在表上,连 localtime_r 也落在了表外。所以进了 handler,查了表才算数,后缀能给的只是线索,它担保不了什么。

## E5:handler 工程模式:flag、self-pipe、siglongjmp

约束讲了这么多,咱们把正面的写法收拢成三个模式,从轻的到重的。

### 模式一:flag 加主循环

handler 做的事只有一件:给 volatile sig_atomic_t 置了位、立刻返回,活全留给了主循环。E5 的模式一给咱们演的是优雅关停:子进程 100 毫秒后发 SIGINT,默认处置下这是当场带 core 的暴毙,而 flag 版:

```text
t=   0ms work loop starts (SIGINT will arrive ~100ms)
...
t=  91ms working iter 4
t= 100ms g_stop seen at iter 5 -> break
t= 100ms cleanup: flush buffers, close files, say goodbye
```

信号到达的事实,在主循环检查标志的那一刻才被消费,而那一刻咱们已经回到了普通的上下文:printf 随便用,析构照常地跑,退出码是干干净净的 0。handler 的全部职责就是那次赋值,这是约束之下最便宜的活法。

### 模式二:self-pipe,信号变成 fd 上的字节

咱们也别忘了 flag 天然的短板:主循环得轮询它。如果咱们的主循环本来就是围着 poll 或 epoll 转的事件循环,那更优雅的做法,是把信号也变成 fd 上可读的事件。这招的名字叫 self-pipe trick,有名有姓的老手法:handler 里只 write 一个字节进管道,主循环 poll 管道的读端,信号与别的 fd 在同一个循环里统一调度:

```text
t=   0ms event loop starts, poll(pipe_r, 200ms timeout)
t= 100ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 301ms (heartbeat, nothing readable)
t= 501ms poll woke up -> byte 'i' = SIGINT (graceful handling)
t= 551ms poll woke up -> byte 't' = SIGTERM -> shutdown
t= 551ms event loop exited cleanly
```

上面的时序,咱们细读一遍。SIGINT 变成了字节 i,SIGTERM 变成了字节 t,两种信号走的是同一个通道。301 毫秒那次是 poll 的 200 毫秒超时,心跳的槽位,真实程序里这里跑的是周期任务。551 毫秒收到了 t,循环体面地退了场。管道的建立用的是 `pipe2(O_NONBLOCK | O_CLOEXEC)`,非阻塞的位在两处各有一份意义:handler 的 write 在管道写满时立刻返回 EAGAIN,这个字节就被丢弃了,handler 是绝不能阻塞的,它要是卡住了,全程序就跟着卡死了。主循环排空管道的时候,read 读到 EAGAIN 就知道读干净了。而 poll 本身还可能被信号打断,循环里对 EINTR 的处理就是再来一次,老朋友了。这套东西到 ch04 的多路复用一篇接着讲,到时候的 epoll 会把管道、把定时器到期也变成 fd 上可读事件的 timerfd 一并收编,您会再见到它。还有一句值得现在就留下的:这一套事件化是咱们在用户态手工仿制的,内核其实备着原生的替代品,它的名字叫 signalfd,下一篇咱们就请它出场。

### 模式三:siglongjmp,跳出重围

有些现场是不适合善后的,比如 SIGSEGV 的 handler 想把执行流直接接回主流程,sigsetjmp 配 siglongjmp 就是干这个的,咱们把 savemask 置成 1,屏蔽字也一起进了保存的范围。内存管理篇的 guarded_buffer 已经把完整的一幕演过了:尾对齐的 guard 页,handler 里的 write 报出越界第几字节,siglongjmp 再把执行流接回了主流程,后面的实验在同一个进程里接着跑。您到[那一篇](../memory/02-vm-apis.md)的 E4 看,本篇就不重做了。

> 咱们也把 C++ 的位置在这儿交代清楚:handler 里,别指望 C++ 的运行时服务。析构函数不会因为您从 handler 里跳走而运行,异常没有自己的通道,new 走的 malloc 也不在安全表上。handler 里最稳的姿态,就是 C 的裸调用加一个标志,一切需要对象生命周期的活,留给了主循环去干。

## E6:SIGCHLD:合流的一位,循环的收尸

子进程退出的时候,内核给父进程发的就是 SIGCHLD。咱们在 E1 里证明过标准信号不排队,这个性质一遇上了 SIGCHLD,就产生了 Unix 编程里最经典的一个模式。waitpid 本身的阻塞语义、状态宏、WNOHANG 的用法,都归[进程创建那一篇](01-fork-exec.md)讲了,咱们这里只讲信号驱动的收尸。

### 三个孩子合流成一次 handler

E6.B 的编排:咱们把 SIGCHLD 屏蔽起来,再 fork 出三个短命的子进程,让它们立刻退出了,三个退出的信号在屏蔽期里全挤进了同一个 pending 位。解除了屏蔽之后:

```text
[E6.B] loop waitpid (classic): 3 kids exit while SIGCHLD blocked
    3 children dead; pending SIGCHLD=1 (one bit, not three)
    handler entries=1, reaped in handler=3 (of 3 kids)
```

咱们看数字:pending 只有一个 1,handler 只进来了一次,而这一次里面用 `while (waitpid(-1, &st, WNOHANG) > 0)` 循环,把三个孩子的尸首一口气收完了。WNOHANG 的意思也顺带交代一下:它是没有尸首也不傻等、立刻返回 0 的开关,循环因此能干净地退出。

### naive 版留下两个僵尸

反例改成了 handler 里只 waitpid 一次,别的咱们什么都不动:

```text
[E6.B] single waitpid (naive): 3 kids exit while SIGCHLD blocked
    3 children dead; pending SIGCHLD=1 (one bit, not three)
    handler entries=1, reaped in handler=1 (of 3 kids)
    kid 0 (pid 15165): /proc state = '?'
    kid 1 (pid 15166): /proc state = 'Z'  <- ZOMBIE left behind
    kid 2 (pid 15167): /proc state = 'Z'  <- ZOMBIE left behind
```

咱们接着看:handler 还是只进来了一次,只收走了一个,kid 0 在 /proc 里已经查无此人了,状态读作 ?。kid 1 与 kid 2 则永远停在了 Z。Z 是 zombie 的缩写,说的就是进程已死、退出状态还没人领走的僵尸阶段。下一次 SIGCHLD 不会再来了:三个退出早就合流成了同一个位,补发的信号并不存在。所以循环是信号不排队逼出来的必需品,谈不上风格上的偏好。

### SA_NOCLDSTOP 与 SA_NOCLDWAIT

SIGCHLD 还有两个专属的 flags。默认的情况下,子进程的停止、恢复、退出都会发 SIGCHLD,E6.A 实测的计数是 1、2、3 三次递增。加上了 SA_NOCLDSTOP 之后,停止与恢复都静默了,计数也就停在了 0、0,只有退出的一次仍然会发,计数回到了 1。SA_NOCLDWAIT 则换了一条路,存档里这一段挂着 [E6.C] 的编号:子进程的退出根本不落僵尸,直接就消失了,实验里 /proc 的状态读作 ?,waitpid 返回的是 -1 与 ECHILD(10),父进程这边没有尸首可收了。man 2 sigaction 补了一条口径:POSIX 没规定 SA_NOCLDWAIT 下还发不发 SIGCHLD,而在 Linux 上仍然发。另外把 SIGCHLD 的处置整个设成 SIG_IGN,在 Linux 上也是同样的自动回收效果,sigaction(2) 的说明写明,它自 POSIX.1-2001 起就是合法的。不过 SIG_IGN 之后您连收尸的机会都没有了,退出码成了永远的谜,要不要这么干,就看您在不在乎它了。

## E7:速览表与硬事实

信号世界的硬事实,能实测的咱们都实测了,表格里其余的条目按 man 7 signal 的口径整理,编号来自本机 `kill -l` 的快照,在 x86-64 的口径下咱们数出 62 个信号,实时信号的那部分,留给下一篇去讲了。

SIGKILL 与 SIGSTOP 是既不可捕获、也不可忽略的,内核在 sigaction 的门口就拒绝了您:

```text
[E7.1] catch/ignore SIGKILL & SIGSTOP: kernel refuses at sigaction time
    sigaction(SIGKILL , SIG_IGN) -> -1 errno=22 (Invalid argument)
    sigaction(SIGSTOP , SIG_IGN) -> -1 errno=22 (Invalid argument)
    sigaction(SIGTERM , SIG_IGN) -> 0 errno=0 (OK, accepted)
    sigaction(SIGSEGV , SIG_IGN) -> 0 errno=0 (OK, accepted)
```

温和的关门请求则可以拒绝。kill 命令默认发的 SIGTERM,被子进程整个忽略了,300 毫秒之后孩子还活得好好的,还报了平安。咱们换 SIGKILL 再发,`WIFSIGNALED=1 WTERMSIG=9`,没有一点商量的余地:

```text
[E7.2] child ignores SIGTERM, still dies to SIGKILL
    CHILD 15171: SIGTERM ignored, ready
    CHILD: survived a SIGTERM at t=300ms
[E7.2] child end: WIFSIGNALED=1 WTERMSIG=9 (SIGKILL=9)
```

再往后的硬事实是 SIGPIPE,存档里它是 [E7.3] 的实测。写一个读端已关的管道,默认的动作是直接杀进程,死了的孩子带回来的 [E7.3] 行写着 `WIFSIGNALED=1 WTERMSIG=13 (SIGPIPE=13)`。把 SIGPIPE 设成了 SIG_IGN 之后,同样的 write 带回 -1 与 EPIPE(errno 32),进程倒是活着,错误变成了可查的错误码。把 SIGPIPE 设成 SIG_IGN、再自己查 EPIPE 的做法,在网络程序里是标配的写法。管道与 FIFO 的世界,咱们在[进程间通信那一篇](03-ipc.md)展开过,那边才是它的主场。

| 信号 | 编号 | 默认动作 | 可捕获/忽略 | 典型用途 |
|---|---|---|---|---|
| SIGHUP | 1 | 终止 | 可 | 控制终端断开。终端死亡时全组各收一条,[守护进程篇](02-daemon.md)讲 nohup 怎么挡 |
| SIGINT | 2 | 终止 | 可 | Ctrl-C,前台进程组的礼貌退出请求 |
| SIGKILL | 9 | 终止 | 不可 | 最后的手段,本篇实测 sigaction 直接 EINVAL |
| SIGSEGV | 11 | 终止+core | 可 | 非法访存,handler 精确报错见内存管理篇 |
| SIGUSR1/2 | 10/12 | 终止 | 可 | 用户自定义,本篇的实验载体 |
| SIGPIPE | 13 | 终止 | 可 | 写已关管道/socket,常设 SIG_IGN 改查 EPIPE |
| SIGALRM | 14 | 终止 | 可 | alarm/setitimer 到期,E3 的实验载体 |
| SIGTERM | 15 | 终止 | 可 | 通用的终止请求,kill 默认发它 |
| SIGCHLD | 17 | 忽略 | 可 | 子进程 stop/continue/exit,驱动 E6 的收尸 |
| SIGSTOP | 19 | 停止 | 不可 | 无条件暂停,配 SIGCONT 恢复 |

## 另一侧怎么看

Windows 手里并没有异步信号的这一套,离得最近的对照是控制台事件:`SetConsoleCtrlHandler` 登记 CTRL_C_EVENT 一类事件的处理器,干的就是与 handler 相当的职责。两边的约束互为镜像,而出发点却是相反的。Linux 的 handler 随时可能插进任意指令之间,所以才有异步信号安全的名单管着它。Windows 把处理器派到一个专门为它建的线程里跑,主流程并不被就地打断,可限期跟着来了:控制台关闭一类事件留给处理器的时间是有上限的,超时了的进程照样被带走。没有商量余度的那一击,而 Windows 那边,它的名字叫 TerminateProcess,地位与 SIGKILL 对得上。这套对照在 Windows 侧分两篇落地:进程篇讲了 TerminateProcess 对位 SIGKILL 的那一半,控制台事件一篇讲投递模型与时限的另一半,咱们这里只认方向。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
  <ReferenceItem
    :id="2"
    title="signal(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/signal.2.html"
  />
  <ReferenceItem
    :id="3"
    title="signal(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal.7.html"
  />
  <ReferenceItem
    :id="4"
    title="signal-safety(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/signal-safety.7.html"
  />
  <ReferenceItem
    :id="5"
    title="sigprocmask(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigprocmask.2.html"
  />
  <ReferenceItem
    :id="6"
    title="setitimer(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/setitimer.2.html"
  />
  <ReferenceItem
    :id="7"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
