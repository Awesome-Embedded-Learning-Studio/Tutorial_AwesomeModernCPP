---
title: "进程创建与生命周期:fork/exec/posix_spawn"
description: "一个进程怎么生出来、怎么体面地退场。本篇把 fork 的一次调用两次返回摆上 /proc 实测(pid/ppid 两侧视角),用 smaps_rollup 的 Pss 证写时复制(子进程未写时父子各记 32915/32952 kB 共享页各半,子进程写满 64 MiB 后涨到 65684 kB、两边之和约 128 MiB 即物理复制,而同一变量地址从头到尾没变过),fd 整表继承落在共享偏移上(父子在同一偏移上接力读,自己另 open 才从 0 开始);1 GiB 父进程 fork 要 31.6 ms 而 posix_spawn 恒定 0.48 ms,贵在页表不在页(136 倍);exec 五变体在 strace 下全部落 execve、execlp 的 PATH 搜索是用户态连试 8 次 ENOENT 后命中,不带 CLOEXEC 的 fd 穿过 exec 且偏移延续;僵尸双阶段时序 S→Z→ENOENT、三种收尸姿势对照(阻塞 wait 按 200/401/601ms 收齐、WNOHANG 轮询 33 次空手 30 次、SIGCHLD 加 SA_RESTART 全程零 EINTR 而去掉实测 2 次)、退出码只有低 8 位(0x1234 到手只剩 0x34);孤儿收养者实测是 pid 249 的 Relay 而非 systemd,按 subreaper 机制写并注明教科书口径;posix_spawn 的 file_actions 三场景(addopen 重定向、adddup2 移交、addclose 连带 ls 复用 0 号 fd 的意外)与 vfork 只引 man 原文不实测危险;收在 RAII 的 child_process:构造即 spawn、析构四档处置(SIGTERM 请退、百毫秒不退 SIGKILL 强杀、再收尸)、move-only 进 vector 三孩子 3/3 清干净,detach 的代价如实记下 State 为 Z"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "共享内存:shm_open 与映射"
related:
  - "守护进程、会话与环境"
  - "进程与作业:CreateProcessW 与 Job 对象"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - RAII
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 进程创建与生命周期:fork/exec/posix_spawn

内存管理那一章讲到共享内存的时候,咱们的实验里其实早就站着一个没正式介绍过的老配角了。[共享内存篇](../memory/03-shm.md) 的竞态实验与 SPSC 队列(SPSC 说的是单生产者单消费者的队列),两边各起了一个子进程,靠的都是同一个调用:fork。它当时一句就被带过了,咱们忙着看内存的事。这一篇咱们把它请到正中间,顺着进程的一生把整条链走下来:进程是怎么生出来的,变身的时候哪些东西留任、哪些被换掉了,死了之后分几步退场,又是谁来负责收尾的。

[L01](../file-io/01-posix-file-io.md) 留了一个话头给咱们。讲 fd 表与打开文件描述的时候,咱们说过 fork 之后父子会在同一个偏移上接力写,细节的事留到进程篇再展开。今天就是来把这个话头接上的:当时看到的只是表象,这一篇咱们把背后的机制量出来。

问题从您天天在做的一件小事出发:您在 shell 里敲下一条命令,一个新进程就跑起来了。它是从哪里来的?命令跑完了,进程没了,它又是怎么走的?咱们这一篇就用 /proc 与 strace 把全程拍下来,fork 的语义、exec 的变身、僵尸的收尸、孤儿的收养,末了收进一个 RAII 的 child_process 封装里。

实验的口径交代在开头,后面的数字都要靠它对表。全部实验出自笔者的 WSL2:内核的版本是 6.18.33.2-microsoft-standard-WSL2,CPU 用的是 AMD Ryzen 7 9700X,编译器用的是 g++ 16.2.1,编译的口径统一走 `g++ -std=c++20 -Wall -Wextra -O2`,拿到的警告数为零,glibc 的版本是 2.44,strace 的版本是 7.2。本机的 PID 1 是 systemd,/proc/1/cmdline 给出的就是 /sbin/init,这个身份到了 E4 会有反转,咱们到时候再细看。计时的钟走的是 CLOCK_MONOTONIC,而 WSL2 是单 NUMA 的环境,perf 也用不了,计时的数字您按数量级读就好,别按个位数的精度去抠。代码与全部的原始输出,收在仓库 `code/volumn_codes/vol8/systems-programming/linux/process/01-fork-exec/` 下面的 e1 到 e6 六个目录里,复现命令写在各目录的 README 中(源码文件的字母尾号标的是目录里的第几个实验,像 e1d 就是 E1 目录里的第 4 个),e1d、e2b、e2c、e5b 用到的数据文件写死在 `~/lp01_scratch`,复跑之前请您把目录建好。实验的编号 E1 到 E6 跟着目录走,文件 I/O 与内存两个章节各篇自己的编号(exp1 到 exp11、小写 e1 到 e5、大写 E1 到 E6)是它们各自那篇的,与咱们这里互不相干,您翻存档的时候认目录号就好。思维基石两篇([RAII 篇](../../thinking/01-raii-paradigm.md) 与[错误处理篇](../../thinking/02-error-paradigm.md))定义的 `unique_fd` 与 `sys_call`,这一篇的实验贴着系统调用的原貌走,用的全是裸调用,到 E6 咱们再把 RAII 的骨架请回来,给进程这样的资源造一副与 `unique_fd` 同宗的骨架。

## E1:fork 的一次调用,两次返回

fork 最著名的属性,说的就是同一次调用会返回两次。这话您乍一听会有点玄,落到实处就是:调用点之后您的代码有两份执行流,各拿各的返回值。E1 的程序在调用前后把自己的 pid 与 ppid 打了一遍,原始输出咱们贴在下面(结尾的两行小结删节):

```text
[调用前   ] pid=26157 ppid=26039,准备调用 fork()
[子进程侧] fork() 返回 0 —— 返回 0 说明我是新进程: pid=26158 ppid=26157
[父进程侧] fork() 返回 26158 —— 这是子进程的 pid: pid=26157 ppid=26039
[父进程侧] 子进程 26158 已收尸: WIFEXITED=1 WEXITSTATUS=0
```

咱们对着输出看三处。父进程的这一侧,fork 交回来的是孩子的 pid 26158,往后的 kill 与 waitpid 都拿它当凭证。子进程这一侧拿到的是 0,而且请您留意它的 pid 与 ppid:26158 的父亲正是 26157,两个进程各自眼里的世界是自洽的。第三处藏在一个容易被忽略的地方:子进程的世界从 fork 返回的那一刻开始,调用之前的代码它一行都没有执行过,可它手里攥着的是一份完整的地址空间副本,连 stdio 的用户态缓冲都复制了一份。所以 E1 的源码在 fork 之前老老实实地 fflush 了 stdout,不然缓冲里攒着的字,父子两边退出的时候就会各刷一遍,同一行就打了两回。这个缓冲的怪脾气后面还会回来找咱们,E6 造封装的时候它就是头等的麻烦。输出末行的两个宏,咱们也提前报个用途:WIFEXITED 报的是孩子正常退出了,WEXITSTATUS 取到的就是退出码,低 8 位的完整解码,E3 的时候再上。

### 写时复制:Pss 给的物证

fork 复制的东西里没有物理内存,这话说出来是轻飘飘的,咱们拿数据说话。机制的名字叫 **COW**(copy-on-write,写时复制):fork 的时候内核复制的是页表,页本身还躺在原地,两边的页表项都标成了只读。谁要是写了,缺页处理里再把这个页复制了一份,写的人带走副本,另一边守着的还是原件。

口说无凭的老问题又来了,量法咱们得交代在前面。咱们想看的是物理页有没有分家,最直接的路子是读 `/proc/self/pagemap` 里的页帧号,可它是要 root 权限的。咱们走的是免 root 的等价证据:smaps_rollup 里的 **Pss**(比例驻留集),[内存布局篇](../memory/01-memory-layout.md) 讲 smaps 的时候定义过它,共享的页按共享的进程数摊,每个进程各记的是几分之一。咱们把这个定义用在 fork 上,推论也就干净了:父子谁都没写的时候,那一大批页是两边共享的,每页记的是一半,子进程把缓冲整个写了一遍之后,COW 就把页全部复制了,子进程的 Pss 也涨回了全额。E1 的第二个实验用的正是这个办法:父进程在 fork 之前逐页写脏了一个 64 MiB 的缓冲,五个时刻咱们各读一次 smaps_rollup(Private_Clean 全程为 0 而删节):

```text
全局变量 g_shared:地址=0x5eec80aab0a0 初值=100
64 MiB 缓冲:地址=0x761d333ff010 (逐页校验和=16384)
fork 前:    Rss=  69536 kB  Pss=  65813 kB  Private_Dirty=  65768 kB
父·fork后: Rss=  69600 kB  Pss=  32952 kB  Private_Dirty=     44 kB
子·未写:   Rss=  67204 kB  Pss=  32915 kB  Private_Dirty=     36 kB
子·写后:   Rss=  67268 kB  Pss=  65684 kB  Private_Dirty=  65572 kB
父·子退后: Rss=  69600 kB  Pss=  65814 kB  Private_Dirty=  65768 kB
……(子进程出生、写完 g_shared、退出后父进程复读的三行,以及结尾的解读,删节)……
```

数字把这个故事讲全了。fork 之前的父进程独自持有这 64 MiB,Pss 记的是 65813。fork 之后、谁都没写的时候,父子两边的 Pss 各掉到了 32952 与 32915,两边各记了一半左右,Private_Dirty 也缩到了几十 kB,页在两边是共享的。子进程随后把缓冲写满了,它的 Pss 涨到了 65684,基本就是全额了,而父进程这边,等孩子退出了之后咱们再读,Pss 回到了 65814。把最后一个时刻的两边加起来,得到的约是 128 MiB,物理内存真的复制了一份。地址咱们也留着呢:`g_shared` 的地址两边从头到尾都是 0x5eec80aab0a0,子进程把它改成了 200,父进程读到的还是 100。地址没有变,值分家了,这就是写时复制的全部现场。

> 再补一个例外:咱们在 [虚拟内存 API 篇](../memory/02-vm-apis.md) 里实测过 MADV_DONTFORK,挂了这个标志的段在子进程里干脆整个消失了。它是给谁用的呢?DMA 类的硬件拿的是物理地址,而 COW 复制页的时候,页会换到新的物理地址上,直接按老地址做 DMA 的硬件就找不着数据了,咱们干脆让子进程看不见这段,麻烦也就没有了。

### fd 整表继承:表是复制品,偏移只有一份

现在该把 L01 留下的那句话接完了。fd 的两级结构咱们复习一句:进程私有的 fd 表,每一项指向系统级的**打开文件描述**(open file description),偏移量就存在后者的身上。fork 复制的是 fd 表本身,表里每一项指向的打开文件描述还是同一份。E1 的第四个实验把这件事量给您看:父进程打开文件读到偏移 16,fork 之后孩子用继承的 fd 接着读,孩子读完了父亲再读:

```text
[父] 打开 fd=3 —— 这一个 fd 背后是一份打开文件描述,偏移量只有一份
[父] 先读 16 字节:0123456789:;<=>? 偏移=16
[子] 用继承的 fd=3 读 16 字节:@ABCDEFGHIJKLMNO 偏移=32(接着父的 16 往下读)
[子] 自己另 open 的 fd=5 读 16 字节:0123456789:;<=>? 偏移=16(从 0 重新开始)
[父] 子读完后我再读 16 字节:PQRSTUVWXYZ[\]^_ 偏移=48(子进程的读把我的偏移也推走了)
```

咱们来看三段接力和一段对照。父亲读了 16 字节,偏移到了 16,孩子用继承的 fd 再读,拿到的是 16 到 31,说明它接的是父亲留下的偏移。父亲随后再读的时候,拿到了 32 到 47,孩子的读把父亲手里的偏移也推走了,因为两边写的是同一个描述里的同一个数字。对照的那一行就更有意思了:孩子自己重新 open 同一个文件,拿到的是 fd=5,读到的是 0 到 15,偏移是从零起算的。新 open 造出来的是新的打开文件描述,与父亲的各玩各的。您把这一幕与 L01 的 dup 对照着看就明白了:dup 复制表项、共享描述,那是单个槽位的事,fork 干的是同一件事的整表版本。L01 当时说的是接着对方的偏移往下写,咱们这里演示的是接力读,读与写推的都是同一个偏移,谁动了描述里的数字,另一边都是看得见的。

## fork 的成本:1 GiB 的父进程要 31.6 ms

COW 让 fork 免掉了复制页的功夫,那 fork 还贵在什么地方?咱们直接量。E1 的第三个实验把父进程喂到了 1 GiB(逐页触碰后 VmRSS=1052516 kB),然后咱们分别量四种创建方式,每种预热 3 轮、计 30 轮取的中位数:

| 创建方式 | 小父进程 | 1 GiB 父进程 |
|---|---:|---:|
| fork 仅创建 | 232.6 µs | 31585.5 µs |
| vfork 仅创建 | 71.2 µs | 74.8 µs |
| fork+exec /bin/true | — | 32117.8 µs |
| posix_spawn /bin/true | 486.2 µs | 480.7 µs |

表里最扎眼的一行是 fork:小父进程 232.6 µs,1 GiB 的父进程直接涨到了 31585.5 µs,差了 136 倍。页本身是一张都没有复制的(COW 刚证过),您问这 31.6 ms 花在了哪里?花在了页表上。1 GiB 逐页触碰过了,就是 262144 个 4 KiB 的页,每一页在页表里都有自己的表项,子进程的地址空间要能用了,这一整套页表连同内核的统计结构就得复制一份。所以 fork 的成本跟着父进程**已触碰**的内存走,跟申请量倒是没关系,只申请而不触碰的虚拟地址空间,是不会让成本涨起来的。

表的下两行把另一半故事讲完了。vfork 与 posix_spawn 对 1 GiB 的父进程几乎无感,两者的 74.8 与 480.7 µs,与小父进程的数字贴在了一起,因为它们压根是连页表都不复制的,机制咱们到 E2 的 strace 一节再对证。posix_spawn 那 480.7 µs 里还含着 exec 本身的开销,咱们可以拿表里已有的数字交叉验一下:fork+exec 的 32117.8 减去裸 fork 的 31585.5,差出来的是 532 µs,与 posix_spawn 的 480.7 同量级,两者差的正是跑一趟 /bin/true 的 exec 加动态链接。两组独立测出的数字能对上,这个表咱们就敢用了。

## E2:exec 家族:五个变体,一条接力链

fork 生出来的孩子长得跟父亲一模一样,可咱们起进程多半是为了跑**别的程序**,这就轮到 exec 登场了。exec 说的是一大家子,五个常用变体的名字有规律:结尾的 `l` 是 list,说的是参数在调用里逐个列,`v` 是 vector 的意思,参数打包成了数组,`e` 表示的是环境变量数组自己给,`p` 表示给的只是文件名,搜索的活它自己去干。E2 的头一个实验把五个变体串成了一条接力链:程序起跑用 execl 变身,新程序里又交出了下一棒 execv,一路交到了 execvp,五棒全都跑完了(输出里的程序路径咱们截短成了 …,块内的删节另有标注):

```text
起点:pid=26434,程序 …/build/e2a_exec_variants
  [probe] execlp(e2a_exec_variants) 失败:No such file or directory(PATH 不含本目录时它真的搜不到)
第 1 棒 execl:完整路径+参数逐个列+NULL 哨兵,变身开始
第 2 棒到达:经 execl 变身,pid=26434 还是同一个进程
      交棒 execv:完整路径+参数打包成 char* 数组
……(probe 的第二条证实行,第 3、4 棒的到达与交棒,第 5 棒的到达与交棒,删节)……
终点:经 execvp 变身。当前 pid=26434,出发时 pid=26434,五次变身 pid 从未变
      出发时塞进环境的变量一路带到终点:exec 换的是程序映像,进程还是那个进程
```

全程的 pid 都是 26434,这就是 exec 的身份声明:它把程序映像整个换掉,代码、数据、堆、栈全是新的,而内核眼里代表进程的那些结构原样留任,进程本身还是原来的进程。第 4 棒还有一个伏笔式的细节:execve 那一棒塞了自定义的环境变量 E02A_VIA,这个变量一路带到了终点。环境是跟着映像换的,也是跟着映像留的。开头那一行 probe 失败也值得留步:程序起跑时 PATH 里并不含它所在的目录,execlp 是找不到自己的,程序随后把自己所在的目录 prepend 进了 PATH,第 4、5 棒才搜得到了。`p` 系列认的只有 PATH,当前目录它是不认的,您拿它去跑相对路径下的程序,十有八九是要扑空的。

### CLOEXEC 的生死与偏移的延续

exec 换了映像,fd 表怎么办?[L01](../file-io/01-posix-file-io.md) 讲 FD_CLOEXEC 标志的时候给过语义,这里咱们补上 exec 前后的实拍。E2 的第二个实验对同一个数据文件开了两个 fd:fd 3 是裸的,fd 4 带的则是 O_CLOEXEC,exec 之前咱们从 fd 3 读走了 8 个字节,偏移到了 8,然后就变身了:

```text
[exec 前] pid=26436,fd 3(普通)与 fd 4(O_CLOEXEC)指向同一个文件
[exec 前] 先从 fd 3 读 8 字节=01234567,偏移=8
[exec 前] 现在调 execl 换一个全新的程序映像……
[exec 后] pid=26436 —— 与 exec 前打印的是同一个数
……(exec 前后的 /proc/self/fd 清单:0/1/2 照旧、fd 3 都在、fd 4 只在 exec 前,删节)……
[exec 后] fd 3 还能用:再读 8 字节=89:;<=>?,偏移=16(接着 exec 前的位置读)
[exec 后] 带 O_CLOEXEC 的那个 fd 不在清单里了:同一个文件,一个穿过 exec 一个被关
```

fd 表跟着进程活过了 exec,fd 3 不但是还在的,偏移还从 8 接着走到了 16,这正是 E1 那一小节的延续:表项指向的打开文件描述没有换,偏移当然也只有一份。fd 4 就地消失了,exec 成功的那一刻内核替咱们关掉了它。同一个文件开出来的两个 fd,一个留了下来,一个被关掉了,差的只是一个标志位。这也是 L01 那句劝告的实测面:多线程程序里 open 完再补 fcntl 是有竞态窗口的,新代码直接带上 O_CLOEXEC 的写法,窗口就归了零。

### 失败是普通返回:exec 后面必须跟退出路径

exec 成功是有去无回的,新程序从 main 的开头跑,老程序的代码已经不在地址空间里了。可它要是失败了呢?咱们在 E2 的第三个实验里连着试了三种死法:

```text
活着:pid=26437,开始三次注定失败的 exec
1) execve(路径不存在) 返回 -1,errno=2(No such file or directory)
2) execv(文件在、没 x 权限) 返回 -1,errno=13(Permission denied)
3) execlp(PATH 里翻不到) 返回 -1,errno=2(No such file or directory)
三次失败后我还活着:pid=26437 —— exec 只有成功才有去无回,失败就是普通的函数返回
```

咱们看三次的结果:都返回了 -1,errno 也是各归各位的,进程毫发无伤地继续跑。所以 fork+exec 的孩子里,exec 后面跟着的 perror 加 _exit,是唯一会被执行到的失败路径。习惯法里把 127 留给找不到的,126 留给没权限的,shell 们沿用的就是这套号。您要是写了 exec 然后什么都不跟,失败的孩子就会带着父程序的内存继续往下跑,多半把父程序的后续逻辑再跑一遍,这样的事故在现场是极难看懂的。

### strace 取证:谁是真的系统调用

上面留了一个问题:execlp 的 PATH 搜索是谁做的。E2 的第四个实验被 strace 盯着跑,strace 放行的只有 execve、execveat、clone、clone3 四类调用,日志咱们挑要害贴(strace 原文里的中文参数是一串八进制转义,咱们转回了汉字,环境指针一类的字段也删节了):

```text
26571 execve("/bin/echo", ["echo", "[e2d] 经 execl 到达 echo"], …) = 0
26572 execve("/home/charliechen/.local/bin/echo", …) = -1 ENOENT (No such file or directory)
……(PATH 里各目录逐个尝试、逐个 ENOENT,共 8 次,删节)……
26572 execve("/usr/sbin/echo", ["echo", "[e2d] 经 execlp(PATH 搜到 ech"...], …) = 0
26570 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, …) = 26571
26570 clone3({flags=CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND, exit_signal=SIGCHLD, stack=0x76150d8e9000, stack_size=0x9000}, 88) = 26574
```

咱们拿到了四条证据,一条一条地看。头一条:代码里写的是 execl,内核的日志里只有 execve,l、v、e、p 四个后缀全是 libc 的包装,真正的系统调用就只剩 execve 一个了,带目录句柄的 execveat 咱们用不上。PATH 的答案在下一条:execlp 的搜索是**用户态的循环**,libc 拿着 PATH 里的每个目录拼出完整路径,挨个地试 execve,这一轮试了 8 次 ENOENT 才命中 /usr/sbin/echo。搜索倒是没劳烦内核,咱们看到的就是一连串失败的系统调用。再看 fork 与 posix_spawn:代码里的 fork(),它在系统调用层落的是 clone,flags 里两个 CLONE_CHILD 开头的标志,是线程库留下的标记,SIGCHLD 是孩子退出时发给父亲的信号。最有意思的是末一条:posix_spawn 走的是 clone3,flags 里带着的是 CLONE_VM 与 CLONE_VFORK。

**clone3** 咱们头一回正经介绍。它是 clone 的新版接口,Linux 5.3 起就进入了内核,参数不再摊成一长串了,而是打包成一个结构体传,日志里那对花括号就是 strace 替咱们展开的结构体。要害在 CLONE_VM:父子**共用同一个地址空间**,页表自然是一张都不用复制的,这也就是 posix_spawn 在 E1 计时表里无感的机制落点。CLONE_VFORK 说的则是父亲在变身完成之前就被挂起了,等的就是孩子 exec,借的是 vfork 的快通道。IPC 篇做 popen 的时候,咱们还会在 strace 里再见到这同一个 clone3,到时候您就眼熟了。

## E3:僵尸与收尸:退出分两步

子进程死了之后会发生什么,这个问题的麻烦,九成出在没人告诉过咱们它是两步走。第一步是退出本身:exit 或 _exit 释放了内存、关掉了 fd,但内核里代表进程的 task_struct 与退出码**留下了**,留下的一份等父亲来取。留下的残骸就是**僵尸**(zombie,死了但还没被 wait 的进程),ps 里看到的 `<defunct>` 与 /proc 里的状态 Z 是它的两副面孔。第二步咱们用 wait 或 waitpid:父亲把退出码取走了,task_struct 才真正地销毁,/proc 里那一页也就就地消失了。E3 的头一个实验把两步之间的三个时刻全拍了下来:

```text
[子] pid=26439,先睡 300ms,给父进程留时间读我的 State
[子] 现在调 _exit(42):释放内存、关 fd,但 task_struct 和退出码留下
[父][t1 子还活着] State:	S (sleeping)
[父][t2 子已退、未收尸] State:	Z (zombie)
[父] 同一时刻 ps 眼里:
        PID    PPID STAT COMMAND
      26439   26438 Z    e3a_zombie_life <defunct>
[父] 现在才 waitpid 收尸(第二阶段)……
[父][t3 收尸完成] WIFEXITED=1 WEXITSTATUS=42(退出码从僵尸残骸里取出)
[父] 再读 /proc/26439/status:fopen 失败(No such file or directory)——进程彻底消失,双阶段结束
```

咱们看到的三个时刻,State 从 S 走到了 Z,最后到整个 /proc 条目的消失。咱们想想内核为什么要留着这具残骸:退出码只有一份,父亲可能是想要的,内核是不能替父亲做主扔掉的。其实僵尸不是缺陷,它只是机制的一部分,真正出缺陷的是漏了收尸。僵尸占的 pid 是要还的,父亲要是不来收的话,孩子就一直挂在进程表里了。服务器上起了成百上千个孩子又一个不收的失控程序,进程表就被僵尸挂满了,栽的就是这一步。

### 三种收尸姿势,同一个场景

知道了要收尸,怎么收就是工程问题。E3 的第二个实验搭了同一个场景:三个子进程错峰退出,间隔是 200ms 的节奏,咱们分别用三种姿势收,输出咱们各贴一段:

```text
=== 姿势一:阻塞 wait —— 父进程什么都干不了,子进程死一个醒一次 ===
  t=200ms 收到 pid=26470 exit=11
  t=401ms 收到 pid=26471 exit=22
  t=601ms 收到 pid=26472 exit=33

=== 姿势二:waitpid(WNOHANG) 轮询 —— 不阻塞,但要自己一遍遍问 ===
  t=802ms 第 11 次轮询:收到 pid=26473 exit=11
  ……(t=1003ms 与 t=1204ms 的两轮同构,删节)……
  合计轮询 33 次,其中空手而归 30 次——轮询的代价是空转与延迟的折中

=== 姿势三:SIGCHLD 处理器 + SA_RESTART(read 自动重启) ===
  t=1405ms read 拿到字节 a(SA_RESTART 把被信号打断的 read 自动续上了)
    [SIGCHLD 处理器] 收尸 pid=26476 exit=20
  ……(字节 b、c 的两轮同构,以及无 SA_RESTART 的对照组与 SIG_IGN 附送场景,删节)……
  管道活干完(3 字节),处理器共收尸 3 个,EINTR 发生 0 次,僵尸清零
```

咱们看姿势一:父亲用的是阻塞的 wait,孩子按 200/401/601ms 的节奏死一个、被收走一个,代价是父亲在两次死亡之间什么都干不了。姿势二换了个代价:waitpid 带上 WNOHANG 就不阻塞了,立刻就返回了,没死的话它立刻返回零,于是父亲得自己一遍遍地问,这一轮问了 33 次才收齐三个,其中 30 次是空手的,空转与收尸延迟之间找平衡的正是轮询间隔。姿势三是事件驱动的路子:孩子退出时内核给父亲发 SIGCHLD,父亲本来在 read 管道里干自己的正事,信号一来处理器顺手把尸收了,处理器里一个 `while (waitpid(-1, …, WNOHANG) > 0)` 的循环一网打尽,一起来几个就一并收掉了。

姿势三里藏着 [错误处理篇](../../thinking/02-error-paradigm.md) 讲过的 SA_RESTART 与 EINTR,那对老相识一齐到场了。处理器打断的是父亲的 read,带了 SA_RESTART,内核在处理器返回后自动替咱们把 read 重新发起,父亲的代码根本看不见这次打断,全程的 EINTR 是零次。实验还做了去掉 SA_RESTART 的对照组,read 被打断后返回了 -1、errno 是 EINTR,实测发生了 2 次,就只能靠手动的循环重试了。另外咱们记一条信号处理器的纪律:处理器里只能用异步信号安全的函数,write 与 waitpid 是白名单里的,printf 则是进不去的,实验源码里拼数字用的就是手工 write。这套纪律的完整清单,信号篇里咱们再专门开一桌。

### 退出码只有低 8 位

尸收到了,取出来的码您该怎么读?E3 的第三个实验列了四种死法,用的都是同一个 waitpid,分辨靠的是几个宏:

```text
四种死法,同一个 waitpid,靠宏分辨:
  _exit(42)                pid=26505   WIFEXITED=1   WEXITSTATUS=42 (0x2A)
  _exit(0x1234) 截断     pid=26506   WIFEXITED=1   WEXITSTATUS=52 (0x34)
  [子] abort() 前先喊一嗓子
  abort() 自毁           pid=26507   WIFSIGNALED=1  WTERMSIG=6 (SIGABRT)
  父进程 kill -9        pid=26508   WIFSIGNALED=1  WTERMSIG=9 (SIGKILL)
```

咱们看第二行的主角:程序 _exit(0x1234),父亲手里的 WEXITSTATUS 是 52,换成十六进制的话就是 0x34。man 3 exit 的原文写得很直白,传给 exit 的 status,只有最低的一个字节,也就是 status 与 0xFF 按位与的结果,会交到父亲的手里,0x1234 的 0x12 那一截在半路上就没了。您要是拿退出码传超过 255 的信息,比如把字节数当成退出码的时候,高八位会无声地丢掉,这类错误编译器是一句都不会提醒您的。shell 的 `$?` 也是同一个 8 位的口径,exit 256 与 exit 0 在脚本看来是难以区分的。

第三、四行是另一类死法:进程被信号处决了,读法就换成了 WIFSIGNALED 加 WTERMSIG,abort() 对应的是信号 6,kill -9 对应的是信号 9。waitpid 交回的原始状态字被退出码与信号复用着,您要是不查 WIFSIGNALED 直接上 WEXITSTATUS,被信号杀死的孩子就会被读成一个古怪的退出码。解码的顺序永远是让 WIFEXITED 与 WIFSIGNALED 挑一个命中的,再取对应的字段。

### SIGCHLD 设成 SIG_IGN:内核替咱们收

还有一条一刀切的路子:把 SIGCHLD 明确设成 SIG_IGN,这里说的是明确的设置,而不是默认的忽略。man 2 wait 的原文说,这之后孩子退出就不会再变僵尸了,wait 与 waitpid 的等待会持续到所有孩子都死光,然后交回 -1 与 ECHILD 的失败。实测下来与它也是一致的:孩子 `_exit(55)` 之后 200ms,/proc 里已经没有它了,咱们一个 wait 都没调过,再调 waitpid 拿到的是 -1 加 ECHILD。代价也是明摆着的:退出码永远拿不到了。E6 讲 detach 的时候,咱们还要回头再提它一次。

## E4:孤儿与收养:教科书说归 PID 1,咱们量到的是 249

父亲死在了孩子前面,孩子就成了孤儿,教科书的标准答案是孤儿会被 init(PID 1)收养。咱们不背书上的答案,咱们自己量。实验的队形是三代人:A 是观察者,fork 出了临时父 B,B 再 fork 出 C 之后立刻就退了场,C 从此没了爹:

```text
[A] 观察者 pid=26512
[A] 本机 PID 1 是什么:/proc/1/comm = systemd
[A]               /proc/1/cmdline = /sbin/init
[C] 出生:pid=26514 ppid=26513(临时父 B)
[B] 临时父 pid=26513,C 已出生,我退场——C 从此没爹
[A] B 已被 A 收尸。A 试着多管闲事去 wait 孙辈 C(pid=26514):返回 -1,errno=10(ECHILD:不是我的孩子,没资格)
[A] 只能旁观:
[A] t+0.0s:State:	S (sleeping)
……(孤儿 C 的一句自述删节)……
[C] ppid 变成了 249,收养者的 comm = Relay(252)
[A] t+1.1s:/proc/26514 消失(ENOENT)——C 退出后没以僵尸滞留,收养者收了尸
[A] 机制:C 的 ppid 实测变成 249(Relay(252)),不是 PID 1(systemd)!
……(机制段的续四行,删节)……
```

本机的 PID 1 明明白白是 systemd,可孤儿的 ppid 实测变成了 249,收养者的 comm 是 Relay(252)。249 是谁?它是 WSL 会话级的 /init 中继,咱们这个会话里的 shell 全挂在它的名下。它凭什么收养?凭的是 **subreaper** 这个身份。咱们把词解开:prctl 有一个 PR_SET_CHILD_SUBREAPER 的选项,内核从 3.4 版起就支持了,被标上的进程就有了收养的资格。man 2 prctl 的原文说,此后它的后代一旦成了孤儿,就会被过继给最近的、还活着的 subreaper。PID 1 只是找不到任何 subreaper 时的兜底。WSL 的会话 /init 恰好就标记了自己,所以咱们的孤儿没有轮到 systemd。教科书那句话在直连 init、没有中间 subreaper 的环境里是对的,写代码的时候请您别把 ppid 等于 1 当成收养的判据,容器与桌面会话这样的环境里,subreaper 是到处都有的。

孤儿为什么没变僵尸?咱们看输出给的答案:C 退出后 1.1 秒,/proc 里报的就是 ENOENT,全程没有 Z 的滞留。僵尸的定义是死了没人 wait,而收养者是一个永远会 wait 的进程,孩子到了它的手里,退出与收尸几乎是无缝的。A 吃到的 ECHILD 也是有用的:观察者想替孙辈收尸,内核直接拒绝了,收尸的资格只认直接的父子关系,隔了一代都不行。

> 这个实验的第一版死得蹊跷:两条报告管道的读端在 A 手里关早了,孙辈往管道里 write 直接吃了 SIGPIPE,静默地死在了半路,输出里收养的那一行就凭空消失了。笔者挂上 strace 才看清了它的死因,修法是把读端牢牢地攥到第二次读完为止。实验的装置自己成了 SIGPIPE 机制的活例子,IPC 篇讲管道的时候咱们把它请回来当例证。

## E5:posix_spawn:一发完成,以及 vfork 为什么不该亲手用

fork 与 exec 的机制都量过了,咱们回头选工具。E5 的头一个实验让同一个任务走两条路:任务一带自定义环境跑 printenv,任务二跑的 `sh -c "exit 7"` 要看退出码,fork+execve 与 posix_spawn 又各跑了一遍。结果两边完全一致,printenv 都交回了 hello-spawn-env,退出码都拿到了 7,行为上是没有什么差别的。差别全在代码的样子上:

```cpp
// fork + execve 三件套(节选):分叉点、失败路径、收尸,一样都不能少
pid_t pid = fork();
if (pid == 0) {
    execve(path, argv, envp);   // 只有失败才会返回
    _exit(127);                 // 127 = 找不到,习惯法
}
if (pid < 0) { /* fork 本身失败 */ }
int raw = 0; waitpid(pid, &raw, 0);   // 第三件:收尸

// posix_spawn:一个调用,上面三件事都在里面
// (e5a 的实际源码里 file_actions 与 attr 两个参数传的都是 nullptr,全形态下一节出场)
int rc = posix_spawn(&pid, path, nullptr, nullptr, argv, envp);
```

三件套咱们都写过一遍了,里面的每一件都有岔路,子进程的代码与父进程的代码挤在同一个函数里,靠 pid 的值分岔。posix_spawn 把这些收进了一个调用,速度借的是 E2 里见过的 clone3 快通道,E1 的计时表也替它背了书,快与父进程内存的大小无关。错误回传咱们按 glibc 文档的口径交代、本篇没有实测:错误码的来源换成了**返回值**,不走 errno 的约定,按文档的说法,连孩子在 exec 阶段的失败也会报回给这个返回值,回传走的是 glibc 内部的机制。

### file_actions:重定向长在孩子身上

咱们再看 posix_spawn 的另一手:file_actions,这是 fork+exec 不好学的一手。它是咱们在调用里交的一张清单,写的是**孩子出生之后、exec 之前**替它做的 fd 操作,于是重定向的活全落在了孩子身上,父进程自己的 fd 一个都不用开、不用关、更不用复原。E5 的第二个实验摆了三个场景(输出里的文件路径咱们截短成 …,ls 各行的权限、属主与日期列也删节了,fd 号与链接目标是原样的):

```text
场景一 addopen:子退出码=0,stdout 被换进了文件:
  …/e5b_out1.txt 的内容:场景一:我明明是 echo,输出却进了文件
场景二 adddup2:子退出码=0(父进程的 fd 3 没动,还是开着的):
  …/e5b_out2.txt 的内容:场景二:输出经父进程开的 fd 落地
场景三 对照一,不做任何动作,子进程 ls 自己的 fd 清单:
  0 -> /dev/null   1 -> …/e5b.out   2 -> …/e5b.out   3 -> /proc/26523/fd
场景三 对照二,addclose(0) 之后,同一清单:
  0 -> /proc/26524/fd   1 -> …/e5b.out   2 -> …/e5b.out
```

场景一用的是 addopen,孩子在出生与 exec 之间自己 open 了输出文件并装进了 fd 1,echo 对此是一无所知的,输出就进了文件。场景二用的是 adddup2,父进程开好了 fd 交给清单,清单在孩子身上执行的是 dup2,装到 1 号位之后再把原来的 fd 关掉,父进程自己的 fd 3 全程没动。场景三做的是对照组,两处意外现场咱们都替您留在存档的输出里了。头一处的意外与 sh 有关:咱们想演示 addclose(0) 的时候,却不能图省事地拿 sh -c 包一层,shell 启动时发现标准的三个 fd 有缺口,会自动地把它们补到 /dev/null,演示就被 shell 自愈掉了,所以场景三直跑 ls 绕开了 shell。第二处藏在上面清单的第二份里:addclose(0) 把 0 号位空了出来,ls 自己的 opendir 要开目录句柄,挑的正是最低的空闲号,0 号被它复用了,链接目标 /proc/26524/fd 把底细照了出来,那已经不是继承来的 stdin 了。

### vfork:快得诱人,man 页自己劝退

E1 的计时表里,vfork 对 1 GiB 的父进程也只要 74.8 µs,与 posix_spawn 走的是同一条 CLONE_VM 快通道。可它的行为咱们这一篇不实测,危险的部分咱们只引 man 2 vfork 的原文,四句咱们整句整句抄在下面,句句都是带着警告的:

```text
"vfork() differs from fork(2) in that the calling thread is suspended until the child terminates (either normally, by calling _exit(3), or abnormally, after delivery of a fatal signal), or it makes a call to execve(2)."
"Until that point, the child shares all memory with its parent, including the stack."
"The behavior is undefined if the process created by vfork() either modifies any data other than a variable of type pid_t used to store the return value from vfork(), or returns from the function in which vfork() was called, or calls any other function before successfully calling _exit(3) or one of the exec(3) family of functions."
"This system call will be eliminated when proper system sharing mechanisms are implemented. Users should not depend on the memory sharing semantics of vfork()."
```

咱们一句句看。头一句说的是挂起:调用的线程就这么被挂起到孩子终止或者 exec 为止。第二句说的是共享:在 exec 成功之前的窗口里,孩子与父亲共用的是全部的内存,连栈都是共用的一副。最狠的是第三句:孩子只要改了存放返回值用的 pid_t 之外的数据,或者从调用 vfork 的那个函数里 return,或者在变身成功之前调了别的函数,行为都是未定义的。第四句是 4.2BSD 手册的判词,翻译过来就是等正经的共享机制实现之后,这个系统调用将来是会被淘汰的,后面紧跟的一句还劝大家别依赖它的内存共享语义。共用一副栈是什么概念?孩子在 exec 之前多调用了一层函数,写的就是父亲挂起时的栈,返回值与局部变量就全在一根绳上了。POSIX.1-2008 也已经把 vfork 的规格删掉了。所以咱们的路子跟着 man 页走:您要隔离的时候用 fork,要快的时候用 posix_spawn,快通道让 glibc 替咱们走,应用代码里裸调 vfork 的正当场景,笔者是一个都想不出来。

## E6:child_process:把整个生命周期包进类型

机制讲全了,收尾的活是咱们的老本行。[RAII 篇](../../thinking/01-raii-paradigm.md) 给 fd、句柄、内存映射造过同一副 move-only 的骨架,进程这样的资源,咱们照着同一副骨架来造。E6 造的 child_process,构造做的就是 spawn,析构把 E3 的收尸做齐了,核心咱们看两段:

```cpp
class child_process {
public:
    child_process(const std::string& path, std::vector<std::string> args, bool detached = false) {
        std::fflush(stdout);  // 孩子直写同一个 fd,咱们的用户态缓冲不冲出去就会反超
        // ……(argv 从 string 到 char* 的搬运,与 attr 的设置,删节)……
        int rc = posix_spawn(&pid_, path.c_str(), nullptr, &attr, argv.data(), environ);
        if (rc != 0)
            throw std::system_error(rc, std::generic_category(), "posix_spawn(" + path + ")");
        detached_ = detached;
    }

    ~child_process() { dispose(); }

    child_process(const child_process&) = delete;      // 进程句柄是独占的,与 unique_fd 同理
    child_process& operator=(const child_process&) = delete;
    child_process(child_process&& other) noexcept;     // 移动交接句柄,源对象归 -1
    // ……(wait/try_wait/detach/terminate/alive,删节)……
private:
    void dispose() noexcept {
        if (pid_ <= 0) return;                       // 第一档:手里没孩子,没事
        if (detached_) { /* 第二档:已解除承诺,不管 */ }
        int raw = 0;
        if (waitpid(pid_, &raw, WNOHANG) == pid_) {  // 第三档:已死未收,只差收尸
            pid_ = -1; return;
        }
        (void)kill(pid_, SIGTERM);                   // 第四档:还活着,礼貌地请它退场
        for (int i = 0; i < 10; ++i) {               // 给 100ms 优雅退场
            if (waitpid(pid_, &raw, WNOHANG) == pid_) { pid_ = -1; return; }
            usleep(10000);
        }
        (void)kill(pid_, SIGKILL);                   // 不肯退,强制手段
        if (waitpid(pid_, &raw, 0) == pid_) { /* 收尸 */ }
        pid_ = -1;
    }
    pid_t pid_ = -1;
    bool detached_ = false;
};
```

构造函数走的是 posix_spawn,这是 E5 替咱们验过的安全通道,失败的时候抛 system_error,不会留下一个构造到一半的对象。开头的那句 fflush,是被 stdio 的缓冲逼出来的:孩子继承的是同一个输出 fd,咱们的缓冲里攒着没冲的字,孩子的输出又直写 fd,两边的输出就会串位了。E1 那次的 fflush,与[文件锁篇](../file-io/05-file-lock.md)里开头的那句 `setvbuf(stdout, nullptr, _IONBF, 0)`,对付的是同一个麻烦:stdio 的用户态缓冲不知道进程的边界,缓冲里攒的字什么时候落笔,得咱们自己管。析构的 dispose 分了四档,手里没孩子的、已解除承诺的、已死未收的、还活着的各走各的路,活着的那档,起手发的是 SIGTERM,给足 100ms 的退场时间,不退的话就 SIGKILL 强杀,末了用阻塞的 waitpid 把尸收干净,哪条异常路径都漏不了。wait 与 try_wait 把 E3 的状态字解码成 exit_status,WIFEXITED 与 WIFSIGNALED 的判断封装在了里面,调用方拿到的直接是人话。

五个演示咱们挑三个看:

```text
=== 演示 1:作用域结束自动收尸,不留僵尸 ===
spawn /bin/sleep 30 → pid=26526
  作用域内:State:	S (sleeping)
  [~child_process] pid=26526 还活着 → SIGTERM
  [~child_process] pid=26526 SIGTERM 生效,收尸完成
  作用域已出,验证 /proc/26526:进程没了,僵尸零残留

=== 演示 3:move-only,进程句柄进容器 ===
  static_assert 三条全过:拷贝删除,移动可用
  第 1 个孩子 pid=26528,被移动后原对象 pid()=-1(句柄已交接)
  ……(第 2、3 个孩子的同构出生行,删节)……
  离开作用域,vector 析构逐个清理:
  [~child_process] pid=26528 还活着 → SIGTERM
  [~child_process] pid=26528 SIGTERM 生效,收尸完成
  ……(26529、26530 各两条同构收尸行,删节)……
  验证:3/3 个孩子全部从 /proc 消失,零僵尸

=== 演示 5:detach——放它走,以及它必须知道的代价 ===
spawn detached /bin/sleep 2 → pid=26532(已在新进程组)
  作用域内:State:	S (sleeping)
  [~child_process] pid=26532 已 detach,析构不管它
  作用域出了,它还活着吗?还在跑
  等 2.5s 让它自然退出。我们承诺过不 wait,它死在我们前面会怎样:
  它退出后的状态:State:	Z (zombie)
  僵尸出现了:detach 只是解除承诺,内核不会替我们 wait
  ……(尾行的两条免责建议删节,正文下一段展开)……
```

咱们看演示 1 的日常:spawn 出来的孩子活得比作用域长,析构函数发了 SIGTERM、收了尸,/proc 里就干干净净了。演示 3 验的是 move-only,static_assert 把拷贝删除写进了编译期,三个孩子靠 std::move 进了 vector,原对象的 pid 归了 -1,等作用域结束了,容器的析构就逐个清理了,3/3 全部消失了。这正是 RAII 篇那套移动语义在进程上的重演:句柄是独占的,能交接而不能共享。

演示 5 的输出笔者特意原样保留。detach 解除的是收尸的承诺(顺手用 POSIX_SPAWN_SETPGROUP 把孩子送进了新进程组自立门户),可内核不会因为咱们解除了承诺就替咱们 wait。孩子死在咱们前面、又没人收尸的时候,它就变僵尸了,State 一栏的 Z 是实测的,咱们如实记下。想把 detach 这一段走干净的话,正解倒是有两条:一条是 E3 的附送场景,全局地把 SIGCHLD 设成 SIG_IGN,内核会替咱们自动收,代价是所有孩子的退出码都不要了,另一条是保证 detached 的孩子永远比咱们活得久,让收养者替咱们兜底。封装能替咱们补上忘记的收尸,detach 之后的那次 wait 谁也替不了,这一段是 child_process 唯一需要您带着手册用的地方。

## 另一侧怎么看

Windows 那一侧是没有 fork 的,本节的断言咱们也交代口径:它们出自文档与 Windows 侧的[进程与作业篇](../../windows/process/01-createprocess.md),不是本篇的实测口径,对照的两侧都有实验背书。CreateProcessW 一次调用就把进程创建并跑了起来,返回的次数只有一次,也不存在 fork 的两次返回,创建与 exec 的活合并在了同一个调用里。资源的形态也换了:拿回来的是进程与主线程两个句柄,等待走的是 WaitForSingleObject,退出码走的是 GetExitCodeProcess,而且那边交回的是完整的 32 位,没有 8 位截断的那回事。CLOEXEC 的对应物也不存在:句柄的继承在创建的那一刻用 bInheritHandles 一次性声明,想要按句柄挑选的粒度,咱们得请出 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`,它是按句柄挑选的扩展属性。收尸的那一侧,Windows 的 Job 对象能把一批进程圈进同一个内核对象里统一管理,与咱们这一篇的 child_process 一对多,又是两种设计的对照,细节都收在那篇里了。

这一篇把进程的出生、变身与退场走全了,后面的三处都要回来用它。[守护进程篇](./02-daemon.md)拿的是 fork 配 setsid 的组合,去组出后台服务的骨架,顺路把环境变量与资源限制讲完了。IPC 篇的管道两端各握一个 fd,生命周期的对齐要靠咱们这一篇的收尸。信号篇则把 SIGCHLD 扩成整套 sigaction 的机制,subreaper 的 249 也会在守护进程篇再出场一次。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="fork(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/fork.2.html"
  />
  <ReferenceItem
    :id="2"
    title="execve(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/execve.2.html"
  />
  <ReferenceItem
    :id="3"
    title="wait(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/wait.2.html"
  />
  <ReferenceItem
    :id="4"
    title="exit(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/exit.3.html"
  />
  <ReferenceItem
    :id="5"
    title="posix_spawn(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/posix_spawn.3.html"
  />
  <ReferenceItem
    :id="6"
    title="vfork(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/vfork.2.html"
  />
  <ReferenceItem
    :id="7"
    title="prctl(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/prctl.2.html"
  />
  <ReferenceItem
    :id="8"
    title="sigaction(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/sigaction.2.html"
  />
</ReferenceCard>
