---
title: "虚拟内存:VirtualAlloc 与 VirtualProtect"
description: "Windows 侧内存管理第一篇,纯内存视角(没有任何文件参与):VirtualAlloc 的保留/提交两段式实测——MEM_RESERVE 1GiB 约 2~5 微秒到手而提交额度分文不动,提交 72KiB 让额度 +80KiB、触碰 18 页后工作集才 +72KiB(提交是承诺,触碰才占物理),MEM_DECOMMIT 退回保留态可再提交,MEM_RELEASE 只认整块基址加 dwSize=0,非基址与带尺寸实测都报 87 而不是直觉的 487。分配粒度双轨:返回地址一律 64KiB 对齐、RegionSize 却按页取整(1 字节→1 页、0xFFFF→16 页、0x10001→17 页、0x100001→257 页),粒度块没提交的尾巴是 FREE 不是隐藏预约,MEM_TOP_DOWN 从 0x7FF4 高地址带往下发。PAGE_GUARD 的一次性陷阱三连证据链(0x104→0x80000001→自灭 0x004→VirtualProtect 再武装又能响一次),系统栈就是 guard 带实现的:2MiB 预约里 COMMIT 段加 2~3 页 guard 带加 RESERVE,压栈 768KiB 后 guard 带下移 764KiB 而扩展期间 VEH 零命中(内核静默长栈),预约耗尽收 0xC00000FD。VirtualQuery 拿 193 步扫完 128TiB(IMAGE 99/MAPPED 16/PRIVATE 17/RESERVE 24/FREE 空洞 37),对照 /proc/self/maps 的行数与后备信息差异,探针语义三件事(BaseAddress 下取整、RegionSize 是剩余量、分组键含 AllocationBase 不合并)。堆的三层与三档阈值:malloc(32) 与 HeapAlloc(32) 同一个 AllocationBase(CRT 坐在进程堆上),≤384KiB 走主堆段、416KiB~1016KiB 开新堆段、≥1MiB 直发 VirtualAlloc 释放后整段归还"
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "结构化异常:SEH 与 VEH"
related:
  - "mmap 内存映射:把文件贴进地址空间"
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 内存管理
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 虚拟内存:VirtualAlloc 与 VirtualProtect

文件 I/O 的一章走完了,咱们手里的 HANDLE、映射视图、异常分发链都是过了手的东西了。这一章咱们换一个对象:咱们不再问文件怎么进内存,改问进程自己是怎么向系统要内存的。您写 `malloc`、写 `new` 的时候,底下总得有一层直接跟内存管理器打交道的入口,Windows 给它起的名字叫 `VirtualAlloc`。咱们其实已经见过它一面了:[文件映射](../file-io/02-file-mapping.md)那篇(后文照旧简称 W02)讲 SEC_RESERVE 的时候,保留态的区段就是靠 `VirtualAlloc(MEM_COMMIT)` 一段一段兑现的。不过 W02 的主角是文件,它看 `VirtualProtect` 也只看了映射视图的授权边界。本篇咱们把文件整个请出去,站到纯内存的视角上,把 `VirtualAlloc`、`VirtualProtect`、`VirtualQuery` 三个入口的用法从头走一遍,`VirtualFree` 管的释放,咱们跟着分配一起讲。

向系统要内存的这件事,Windows 明着拆成了两步,每一步各给的是什么,咱们到保留与提交的一节里用读数去对。这个两段式在 W02 的 SEC_RESERVE 那里演过文件后备的版本,本篇看的是它原生的样子。Linux 那边对同一个懒分配问题走的是另一条路:匿名 `mmap` 默认**超额承诺(overcommit)**,内核答应下来的地址远超物理内存,系统级的承诺合计,meminfo 里报的是一个总数,进程级的承诺额度,用户态是拿不到的。Windows 把承诺做成了显式的状态与额度,这里的分岔,后文咱们还会反复回到它身上。

编号与环境的口径,咱们照例交代在开头。本篇的实验按 e1 到 e5 编号,跟着存档的五个子目录走,代码与原始输出全部收进了仓库的 `code/volumn_codes/vol8/systems-programming/windows/memory/01-virtualalloc/` 目录,与 thinking 篇以及文件 I/O 章 W01 到 W05 的 e 系编号互不相干,您认文件名就不会认错人。机器还是 Win11 26200 的本机,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,咱们从 WSL 里跨系统调起 Windows 程序的 interop 链路,是 Win32 文件 I/O 的第一篇交代的(系列里咱们简称它 W01,简称的路数与 W02、W03 的一样)。编译统一给的是 `-std=c++20 -Wall -Wextra`,拿到了零警告。主实验全按无优化的口径跑,e3 与 e3b 这两场请您复跑时也保持 `-O0`:VEH(向量化异常处理器,工具链咱们从 [SEH 与 VEH](../file-io/03-seh-veh.md)那篇领来,那篇咱们简称 W03,VEH 借咱们接住摸页那一瞬的异常)记到的异常指令地址是证据的一部分,优化器一挪动摸页的写指令,现场就对不上号了。程序的开头一律挂着 `setvbuf(stdout, NULL, _IONBF, 0)`,崩溃前的输出才不会烂在缓冲里。输出里从 WSL 直跑收到的 `$?` 是 wait status 的低 8 位,e3b 溢出那一场的 `0xC00000FD` 会被截成 253,全码咱们得在程序里自己打。工具方面没有新面孔:`unique_handle` 的骨架定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md),而 `last_error_code` 的定义在[错误处理范式](../../thinking/02-error-paradigm.md)里,`check_win32` 的定义则留在 W01,咱们全部领来用。输出的捕获日期是 2026-10-03,输出里的地址全吃 ASLR(地址空间布局随机化,每跑一次地址就换一轮),您复跑时长得不一样才是正常的。

## 保留在前,提交在后

两个词咱们分开认。**保留(reserve)** 对应 `VirtualAlloc` 的 `MEM_RESERVE`,文档的原话是 `"Reserves a range of the process's virtual address space without allocating any actual physical storage in memory or in the paging file on disk"`,登记的是地址范围,而物理内存与页面文件一个字节都不动。**提交(commit)** 对应的是 `MEM_COMMIT`,文档对它的说法是 `"Allocates memory charges (from the overall size of memory and the paging files on disk) for the specified reserved memory pages"`,花掉的是承诺意义上的额度,而紧跟着还有一句要紧的话:`"Actual physical pages are not allocated unless/until the virtual addresses are actually accessed"`。咱们手里正好有两个现成的读数可以对上它:**工作集(working set)** 是 Windows 划给进程的那批物理页,W02 讲预取的时候咱们已经碰过它,咱们从 `GetProcessMemoryInfo` 的 `WorkingSetSize` 字段读。**提交额度(commit charge)** 是系统按进程统计的承诺总量,同一个调用里的 `PagefileUsage` 字段给的正是它,任务管理器里的提交大小读的也是这个数。

e1 的头一步就冲着两段式来:一次性保留 1 GiB,咱们用三段采样看住它——预约前、提交后未触碰、触碰后的读数。程序里量耗时用的是 `QueryPerformanceCounter`,采样与查询的封装与 W02 同款,咱们直接看输出:

```text
== 步骤1:MEM_RESERVE 1 GiB(只预约地址,不占提交额度)==
  [采样 预约前 ] WorkingSet=3668 KiB   CommitCharge(PagefileUsage)=536 KiB
  VirtualAlloc(NULL,1GiB,MEM_RESERVE) = 0000024200000000  耗时 2 微秒
  [采样 预约后 ] WorkingSet=3676 KiB   CommitCharge(PagefileUsage)=536 KiB
  [VQ base          ] 0000024200000000..0000024240000000 State=RESERVE Protect=000000 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x40000000
```

1 GiB 的范围,2 微秒就到手了,这一轮跑了几遍,落点都在 2 到 5 微秒的区间里。您再看两个采样:工作集只晃了 8 KiB,那是测量代码自己的动静,而提交额度则是分文未动。`VirtualQuery`(后文咱们简称 VQ)读回来的,是整整 0x40000000 的范围全挂着 `State=RESERVE`。咱们把这一步跟 Linux 对照起来看,就有意思了:那边的匿名 mmap 要来同样一段,内核记在自家的 VMA(虚拟内存区域,内核按段登记地址区间的管理单元)里,咱们在用户态看不到承诺被谁记了几笔,而 Windows 这边的预约是一个显式的、可查询的状态。

咱们第二步把承诺补上。咱们在这 1 GiB 里挑两块小地方提交:区头 64 KiB 加上 1 MiB 位置处的 8 KiB,合计 72 KiB 的量,然后咱们每页都摸一下:

```text
== 步骤2:子区间提交 + 触碰 ==
  提交 base..base+64KiB(RW) -> 成功
  提交 base+1MiB..+8KiB(RW) -> 成功
  [采样 提交后未触碰] WorkingSet=3680 KiB   CommitCharge(PagefileUsage)=616 KiB
  [VQ base          ] 0000024200000000..0000024200010000 State=COMMIT  Protect=0x0004 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x10000
  [采样 触碰后 ] WorkingSet=3752 KiB   CommitCharge(PagefileUsage)=616 KiB
  (提交 +72KiB 在先,WS 只长了被摸过的 ~18 页 —— 提交是承诺,触碰才占物理)
```

咱们把三段采样连起来读,承诺与物理的分家就看清楚了。提交完还没碰的时候,提交额度从 536 涨到了 616 KiB,涨的 80 KiB 里,72 KiB 是咱们要的(0x04 是 `PAGE_READWRITE` 的裸值,取整到页后的 16 页加 2 页,合起来正是 72 KiB 的提交量),多出来的零头是同进程里 stdio 与堆的噪声,这个量级的抖动后面咱们还会见到。而工作集的读数还是没动。等咱们把 18 页全摸过一遍,工作集涨了 72 KiB,提交额度的数字反而一个没变。所以提交只是把承诺记下,物理页要等咱们真去摸的那一刻,才一页一页地发下来。还有一句文档里的话,值得一并记下:`VirtualAlloc` 的文档开头就写了 `"Memory allocated by this function is automatically initialized to zero"`,咱们拿到手就能直接用,不用咱们自己清零。

释放的这边有两个词,两个词的语义差得很远,咱们挨个试。`MEM_DECOMMIT` 干的是把提交了的页退回保留态,另一个干的,是把整个保留区归还成 FREE 的活。e1 的第四步把两种错用与一次正确用法摆在一起:

```text
== 步骤4:释放语义 ==
  VirtualFree(base+64KiB,64KiB,MEM_RELEASE) -> 失败 GetLastError=87(ERROR_INVALID_PARAMETER)
  VirtualFree(base,4KiB,MEM_RELEASE)(dwSize 非 0) -> 失败 GetLastError=87(ERROR_INVALID_PARAMETER)
  VirtualFree(base,64KiB,MEM_DECOMMIT) -> 成功
  [VQ base(退提交后)] 0000024200000000..0000024200100000 State=RESERVE Protect=000000 Type=PRIVATE  AllocBase=0000024200000000 RegionSize=0x00100000
  [采样 退提交后] WorkingSet=3704 KiB   CommitCharge(PagefileUsage)=752 KiB
  再提交 base..+64KiB(退提交可逆) -> 成功
  VirtualFree(base,0,MEM_RELEASE)(整块归还,含仍在提交的 +1MiB 子区间) -> 成功
```

两笔失败值得您多看一眼。`MEM_RELEASE` 的要求文档写得很硬:地址必须是当初 `VirtualAlloc` 保留时返回的基址,`dwSize` 的要求则是 0,文档的原话是 `"The function fails if either of these conditions is not met"`。可条件不满足时报什么码,咱们翻遍文档也找不到这个码。咱们的直觉会顺着地址不对的方向猜 487(`ERROR_INVALID_ADDRESS`),毕竟 W02 的 e2 刚给过 487 的样本:往已保留的空洞里再走一次带保留的分配,回的就是 487。而实测呢,非基址与带尺寸两笔给的统统是 87(`ERROR_INVALID_PARAMETER`)。咱们不猜了,按实测记:这一层的检查把这类错用归进了参数错误,没有归进地址错误。`MEM_DECOMMIT` 那一路倒是宽厚得多,文档明说不合页的区间会自动取整,退过的页还能再提交,输出里退回了 RESERVE 又提交回 COMMIT,一个来回走得都挺顺的。引用块里那行退提交后的采样,咱们也多看一眼:CommitCharge 反而到了 752,比触碰后的 616 还多出 136 KiB,咱们放掉的页没让总量降下来,因为这个数是进程级的总量,同进程里的 stdio 与堆还在动,前面说的那个量级的抖动,咱们在这里见着了第二回,所以咱们读它看的是阶段差,不做单点对单点的比。最后那句括号请您留意:`MEM_RELEASE(base, 0)` 归还的是整块,里面还在提交状态的 1 MiB 子区间跟着一起没,分批归更是没有入口的,整块进出是文档写死的形态,库作者想在它上面包一层分块释放的话,包出来的也只能是整块的集合。

步骤 3 还埋着一个后面 VQ 一节要用到的事实,咱们现在就把它记下:直接 `MEM_COMMIT` 连发三笔 p1、p2、p3,相邻的两笔即使属性完全相同,VQ 也把它们认成三个独立的区段,依据是 VQ 读数里的 `AllocationBase` 各归各(这个字段记的是哪一次分配创建了这里,与探测的起点是两回事,VQ 的一节里细说)。咱们释放 p1 之后,p2 的读数纹丝不动,它只放掉了自己那 0x10000,这几笔的实测输出在存档的 01 目录里。所以相邻的两笔并不天然是一体,每笔 `VirtualAlloc` 都是一次独立的预约。

> 有朋友可能要问:这一篇怎么不给 `VirtualAlloc` 包个 RAII?笔者也想过,可释放这一层的规则本身正是本篇要观察的对象,`MEM_RELEASE` 只认整块的基址加零尺寸,包裹层得把基址与边界都记全了才敢进析构。这件事本身是值得做的,等咱们走到平台抽象章,把两侧的封装一起收进工具库的时候再细说。思维基石的 `unique_handle` 认的是 HANDLE 与 `CloseHandle`,VirtualAlloc 还的是裸指针,两边是对不上的,本篇咱们就老实用裸调用。

## 粒度的双轨:地址认 64 KiB,大小认页

`GetSystemInfo` 一口气给咱们两个数:页大小给的是 4096,另一个数咱们叫它**分配粒度(allocation granularity)**,这一档的数值是 65536。粒度这个词 W02 已经领教过一次:MapViewOfFile 的偏移不按 4 KiB 页对齐、按 64 KiB 粒度对齐,页对齐的 4096 照样吃 1132 的错误码。本篇要看的深一层:粒度管的是地址,页管的是大小,两套标准是同时在场的,而且它们量的不是同一个东西。文档的原话摆在前面:保留时 `"the specified address is rounded down to the nearest multiple of the allocation granularity"`,而 `dwSize` 在地址给 NULL 时 `"rounded up to the next page boundary"`。地址往粒度上靠,而尺寸往页上取整,咱们用 e2 的六行实测把这句话对上:

```text
== 步骤1:请求尺寸 vs 实得地址/RegionSize(MEM_RESERVE|MEM_COMMIT)==
  请求       返回地址       低16位 64K对齐  RegionSize(VQ)
  0x1          177ba5c0000        0        是        0x00001000(1 页)
  0x1000       177ba5d0000        0        是        0x00001000(1 页)
  0xffff       177ba5e0000        0        是        0x00010000(16 页)
  0x10000      177ba5f0000        0        是        0x00010000(16 页)
  0x10001      177ba600000        0        是        0x00011000(17 页)
  0x100001     177ba620000        0        是        0x00101000(257 页)
```

六个返回地址的低 16 位全是 0,这就是粒度的手笔:不管您要几字节,发给咱们的地址一律落在 64 KiB 的边界上。`RegionSize` 那一列就完全是页的口径了:您要 1 字节,发给咱们的只有 1 页。您要 0xFFFF,而它不进位到粒度,而是向上取整到 16 页,凑出的恰好是 64 KiB 的整。咱们要 0x10001,多出的那 1 个字节把咱们推进了第 17 页,`RegionSize` 就成了 0x11000。0x100001 是同样的道理,1 MiB 加 1 个字节的请求,换来的正是 257 页的量。您要是从 mmap 的世界搬代码过来,最容易栽的正是这里:Linux 只有一根 4 KiB 的尺子,Windows 的地址与大小各认各的,连要 1 字节实得 1 页、粒度块里剩下的 60 KiB 归谁这类问题,咱们都得重新对表。

尾巴归谁?咱们让 e2 探了那一笔 1 字节分配的块内偏移:

```text
  探针:上面 1 字节那笔(00000177ba5c0000)块内偏移 +4KiB 处:
  [VQ +4KiB           ] State=FREE    Protect=0x0001 AllocBase=0000000000000000 RegionSize=0x0000f000
  [VQ +64KiB(块外)  ] State=COMMIT  Protect=0x0004 AllocBase=00000177ba5d0000 RegionSize=0x00001000
```

粒度块里没提交的尾巴,读出的状态是 FREE,不是什么隐藏的预约。咱们要 1 页,系统发给的也就 1 页,同一个 64 KiB 块里剩下的 60 KiB 谁都可以来拿,所以连续小分配的地址常常连号,e2 的步骤 2 连发四笔 64 KiB(四笔的实测输出在存档的 02 目录),相邻间隔精确的 0x10000。连号是会被插队打断的:e1 步骤 3 的 p1 与 p2 之间隔了 0x230000,中间是 printf 自己也要内存、把堆撑出来的新段插了空(堆怎么长出新段,e5 的一节细看),粒度的规则没有变,只是排队的人多了。

地址从哪里开始发,Windows 还给咱们留了一杆开关:`MEM_TOP_DOWN`,文档对它的说法是 `"Allocates memory at the highest possible address"`,还提醒了一句这可能更慢。e2 的第三步把两种模式摆在一起:

```text
== 步骤3:MEM_TOP_DOWN(从高往低发)==
  默认     low =00000177ba770000
  TOP_DOWN td1=00007ff48a850000
  TOP_DOWN td2=00007ff48a750000
  td1-low=0x7e7cd00e0000(跨了半个用户态空间,量级对比)
  td1-td2=0x100000(第二次 TOP_DOWN 更低 —— 高水位往下走,间隔恰 1MiB)
```

默认模式把地址发在了低地址带,本轮 e1 的落点在 0x02 段,e2 这一场的落点更低,落到了 0x01 段。而 `TOP_DOWN` 一开,直接跳上了 0x7FF4 的高带。引用块末行括注里写的跨了半个用户态空间,咱们替它把数算准:`td1-low=0x7e7cd00e0000`,换算下来约有 126.5 TiB 的跨度,128 TiB 的用户态空间几乎被从头穿到了尾,接近了全程的百分之九十九,而不是刚好一半。连发两笔还能看到高水位的走法:两笔各要 1 MiB 的尺寸,第二笔紧贴着头一笔的下沿往下发,间隔恰好是 1 MiB 的请求量。什么时候咱们在意它?您想做地址布局探测,或者想离某些高危区域远一点的时候,它就是现成的旋钮。

## PAGE_GUARD:响一次就自灭的页

页保护那一栏咱们已经用过 `PAGE_READWRITE` 了,`VirtualProtect` 平时改的也就是这些档位。现在请出本篇最有 Windows 味的一位:`PAGE_GUARD`。咱们把它叠在某个基础保护上用,它自己是不单独占一档保护的。文档给它的定性咱们原样抄来:`"Guard pages thus act as a one-time access alarm"`,一次性的警报。触碰它的那一瞬间,系统抛的异常是 `STATUS_GUARD_PAGE_VIOLATION`(0x80000001),同时就把 GUARD 位自己抹掉了,文档的下一句是 `"When an access attempt leads the system to turn off guard page status, the underlying page protection takes over"`,底下垫着的基础保护从此接管,再摸就是普通的读写。

光这样说当然不算数,咱们用 e3 把完整的三连证据拍下来。实验的做法很直接:拿一页 `PAGE_READWRITE|PAGE_GUARD`(VQ 实读的 Protect=0x104,0x100 就是 GUARD 的位标记),VEH 的工具链与退出码读法都是 W03 的,handler 里认准这一页的地址就回 `EXCEPTION_CONTINUE_EXECUTION`:

```text
== 第 1 次写:应当响 ==
  [VQ 写前  ] Protect=0x0104(READWRITE+GUARD) State=COMMIT
  [VEH 命中 1] code=0x80000001 info[0]=1(写) info[1]=0x273639F0000 异常指令=00007ff7009a17d5
     是 guard 页的这一笔 → CONTINUE_EXECUTION,原指令重放
  重放成功,*g='A'
  [VQ 写后  ] Protect=0x0004(READWRITE) State=COMMIT

== 第 2 次写同页同地址:零异常(GUARD 已自灭)==
  *g='B',VEH 新增命中=0(写前 1 → 写后 1)
  [VQ 再写后] Protect=0x0004(READWRITE) State=COMMIT

== 再武装:VirtualProtect 塞回 GUARD 位 ==
  VirtualProtect(...RW|GUARD) -> 1(旧保护=0x4)
  [VQ 再武装后] Protect=0x0104(READWRITE+GUARD) State=COMMIT
  [VEH 命中 2] code=0x80000001 info[0]=1(写) info[1]=0x273639F0000 异常指令=00007ff7009a18d3
  *g='C'(这一笔应当又响一次)
  [VQ 第三次写后] Protect=0x0004(READWRITE) State=COMMIT
```

咱们顺着三段走一遍。头一次的写入,VEH 收到的是 0x80000001,info 的第 0 个字段标记了写访问,第 1 个字段精确到了字节,这一套字段的口径 W03 讲过。handler 回了 `EXCEPTION_CONTINUE_EXECUTION`,原指令重放的这一下能成功,靠的正是自灭:抛异常的同时内核已经把 GUARD 位清了,重放那一下面对的是一页普普通通的 READWRITE,写就进去了。VQ 紧跟着读出来的 0x0004,把这件事写在了现场。第二次咱们写同页同地址,VEH 的命中计数一动不动,这就是零异常的对照。第三段咱们拿 `VirtualProtect` 把 GUARD 位塞回去,出参如实带回了旧保护 0x4,咱们再写,又响了一次,响完之后又落回了 0x04。咱们看着它响、灭、再武装、再响,三段拼成一个完整的回合,这才叫见过它的一次性。

与它对照的是 `PAGE_NOACCESS`,那是持续封路的性子:不改回去,每次访问收到的都是 `0xC0000005`。Linux 侧的 guard page 恰好是持续这一派,[虚拟内存 API 篇](../../linux/memory/02-vm-apis.md)的实测也是这么说的,`PROT_NONE` 页每次越界访问送来的都是 `SEGV_ACCERR`,您摸多少次它拦多少次。咱们想模仿这套语义的话,得在 SIGSEGV 的 handler 里自己把保护改回去再重新埋,Linux 没有原生的一次性警报。两边的分岔谈不上谁强谁弱,只是把警报用在了不同的地方,往下看您就明白 Windows 为什么需要它自灭。

它存在的理由,咱们到栈上找。Creating Guard Pages 的文档自己招了:`"there are operating systems that use guard pages to implement automatic stack checking"`,Windows 正是这么干的。e3b 的 layout 模式给主线程的栈拍了一张全身像,咱们拿 VQ 从栈顶往下逐页采样:

```text
$ ./e3b_stack_guard.exe layout; echo "exit=$?"
mode=layout  栈上取样点=00000073f2dff734(主线程,预约总量以 VQ 读数为准)
  -- 压栈前 --
  0073f2df9000..0073f2e00000 COMMIT  0x0004(7页) AllocBase=00000073f2c00000
  0073f2df6000..0073f2df9000 COMMIT  0x0104+GUARD(3页) AllocBase=00000073f2c00000  <-- 栈的 guard 带
  0073f2c00000..0073f2df6000 RESERVE 000000(502页) AllocBase=00000073f2c00000
  guard 带=00000073f2df6000  本栈预约合计=2048 KiB

递归 48 层(每层 16 KiB,共 768 KiB)平安回来,期间 VEH 命中=0

  -- 压栈后 --
  0073f2d3a000..0073f2e00000 COMMIT  0x0004(198页) AllocBase=00000073f2c00000
  0073f2d37000..0073f2d3a000 COMMIT  0x0104+GUARD(3页) AllocBase=00000073f2c00000  <-- 栈的 guard 带
  0073f2c00000..0073f2d37000 RESERVE 000000(311页) AllocBase=00000073f2c00000
  guard 带=00000073f2d37000,较压栈前 下移 764 KiB —— 提交推进到哪,guard 重埋到哪
exit=0
```

主线程的栈,骨子里就是一次 2 MiB 的保留:顶上的 7 页是已提交的,是正在用的栈页。往下 3 页的 0x0104 就是 guard 带,请您留意它是 2 到 3 页的一条带,不是教科书里孤零零的单页,Win11 上实测出来的就是这个形态。再往下的 502 页全是保留态,预约的家底都在这里。真正的看点在两次采样之间:咱们用 16 KiB 一层的递归把栈压下去 768 KiB,递归平安回来了,而 VEH 的命中数是 0。栈确实长了 764 KiB,guard 带下移的距离与压栈量对得上,差的 4 KiB 正好是一页的量,COMMIT 段从 7 页涨到了 198 页,但用户态的 VEH 一个异常都没报。长栈的那一下异常确实发生了,只是内核自己把它消化了:摸到 guard 带之后的清位、提交新页、重埋 guard,把现场顶回去重放了一遍,全程不进用户态的视野。自灭的设计在这一刻就通了:栈的 guard 带要的就是摸一次、让一次、退一页,要是它是持续封路的,栈就没法自动生长了。

预约总有见底的时候。overflow 模式开一个 256 KiB 栈的工作线程往深里无限递归,这里有两个口径请您留意:`CreateThread` 的 `dwStackSize` 给的是初始提交量,所以顶层 64 页直接是 COMMIT,而预约总量继承自 PE 头登记的 2 MiB(PE 是 Windows 可执行文件的格式,头部里存着链接器给的默认栈保留量,mingw 给的默认值就是 2 MiB),VQ 读出来的预约还是 2048 KiB。程序还调了 `SetThreadStackGuarantee` 给 VEH 留 64 KiB 的保底栈,溢出现场的 printf 才活得到输出:

```text
$ ./e3b_stack_guard.exe overflow; echo "exit=$?"
mode=overflow
  工作线程开跑,先拍自己的栈布局:
  -- 工作线程栈(请求预约 256 KiB) --
  00a2751c0000..00a275200000 COMMIT  0x0004(64页) AllocBase=000000a275000000
  00a2751af000..00a2751c0000 COMMIT  0x0104+GUARD(17页) AllocBase=000000a275000000  <-- 栈的 guard 带
  00a275000000..00a2751af000 RESERVE 000000(431页) AllocBase=000000a275000000
  预约合计=2048 KiB —— 实际以 VQ 读数为准(CreateThread 的取整口径见 README)
  开始无限递归...
  [VEH] code=0xC00000FD info[1]=0xA275011280 depth=122
  [VEH] STATUS_STACK_OVERFLOW —— 栈预约耗尽,放行让它收场(进程将崩)
exit=253
```

咱们递归到第 122 层,guard 带一路退到了预约的底,后面再没有可让的页,这一回 VEH 终于看见了:code 是 `0xC00000FD`,`STATUS_STACK_OVERFLOW`。输出里还有一处值得您留意:工作线程的 guard 带厚到了 17 页,跟主线程的 2~3 页差着一个量级,也跟咱们设的 16 页保底栈只差 1 页,存档记下的只是形态,机制的因果咱们点到为止,内核的内部咱们不替它下断言。它跟 0x80000001 不是一路的码,NTSTATUS 的最高两位标记严重级别,8 开头的是警示,C 开头的才是错误:guard 响的那一下是警报,响完了还能继续跑,栈耗尽的这一下是事故,收场的是整个进程。shell 里收到的 exit=253,就是开头交代过的低 8 位截断,全码 0xC00000FD 还得咱们在程序里自己打。回看 0x80000001 的 8,您就更能体会 guard 的定位了,它天生就不是拿来杀进程的,服务的是摸到即处理的使用场景。

## VirtualQuery:给 128 TiB 的地址空间做普查

三个入口里最后出场的一位,管的是观察。`VirtualQuery` 一次只问一个区段:给它任意一个地址,它把包含该地址的、属性连续的那一段量出来,连同字段塞给咱们:`BaseAddress` 给的是探测页往下取整后的页首,`RegionSize` 给的是从这一页到区段尾的剩余量,`State` 是 COMMIT/RESERVE/FREE 的三态,`Protect` 是页保护的档位,`Type` 是出身的分类。头两个字段的读数还藏着三处讲究,咱们看完全景回头专门细说。其中 `Type` 的三档咱们正式认一下:`MEM_IMAGE` 说的是映射自可执行映像(EXE 与 DLL)的段,`MEM_MAPPED` 说的是映射自数据文件或页面文件后备对象的视图,`MEM_PRIVATE` 说的是私有内存,咱们 `VirtualAlloc` 出来的全是这一类。`AllocationBase` 则记着当初哪一次分配创建了这里,前面 p1、p2 相邻不合并的判断,靠的就是它。

咱们想看全景的话,就从地址 0 开始,每一步跳到下一区段的开头,一路问到用户态的上限。e4 跑出来的全景是这样的:

```text
$ ./e4_maps_scan.exe; echo "exit=$?"
用户态上限 lpMaximumApplicationAddress=0x7ffffffeffff
扫描步数(VirtualQuery 次数)=193,时间 <1 秒 —— FREE 段一跳就是一个大空洞

== State/Type 组合统计(非 FREE 区段)==
  State/Type           个数           字节
  COMMIT/IMAGE             99         16252928 (0.02 GiB)
  COMMIT/MAPPED            16          1777664 (0.00 GiB)
  COMMIT/PRIVATE           17           385024 (0.00 GiB)
  RESERVE/IMAGE            10           167936 (0.00 GiB)
  RESERVE/MAPPED            2          1032192 (0.00 GiB)
  RESERVE/PRIVATE          12       4340690944 (4.04 GiB)
  FREE(空洞)             37  140733127983104 (128.00 TiB,用户态总空间 128 TiB)
```

128 TiB 的空间,咱们 193 步就问完了,用时不过 1 秒,诀窍在 FREE 的行为:空洞是连片的,一次查询跳过的就是一整个空洞,37 个空洞的合计约是 128 TiB——程序打出来的 128.00 是舍入读数,刨去程序自己占的几段,剩下的全是空洞——非 FREE 的区段有 156 个,COMMIT/IMAGE 一家就占了 99 个,约 30 个 DLL 的映像段拆出来就是这个数,每个 DLL 的代码、数据、导入表各自成段,互相之间是不合并的。

咱们把上面的表跟 Linux 的 `/proc/self/maps` 对着看,分岔一下就显出来了。行数的差距就是一个量级:同体量的程序在那边,maps 的行数通常落在 25 到 55 行,每行对应的是一个 VMA,咱们这边数出来的是 156 个区段。信息量更是分了家:maps 的每一行后面挂着后备文件的路径,哪段是谁映射的,咱们看一眼就知道。VQ 给的只有 `Type` 这一档粗分类,主人的名字是不给的。统计里最大的那笔 4 GiB 预约,VQ 就说不出它是谁的:每个 Win11 进程身上都挂着两笔 PRIVATE 大预约,一笔在实测里读到的是 4GiB 加 128KiB(4 GiB 出头),另一笔是 32 MiB 的整块,咱们做了两组对照,一组是绕开 WSL interop 直连 cmd 跑的,另一组是用 `-static` 剥掉 mingw 运行库跑的,两组对照的运行里它俩都在,所以它们跟咱们的程序无关,来自系统 DLL 链的预约。VQ 对此是见形不见主的,咱们想问到归属的话,得下到 NT 层的 `NtQueryVirtualMemory`,那是比本篇更深的一层了。数字两边对不齐的,也是同一类现象:扫描合计的 COMMIT/PRIVATE 是 376 KiB,进程的 CommitCharge 却是 1004 KiB,差额是 DLL 与映射段的共享提交加内核侧的统计口径,对照 Linux 那边 VmSize 与 Private 的差,形态上属于同一类的现象。

现在回头说上面三处读数的讲究,咱们一处一处看过去。头一处是 `BaseAddress` 的读数,它是探测页往下取整的结果,不是咱们传进去的指针原样。第二件最反直觉:`RegionSize` 量的是从探测页到区段尾的剩余量,整段的大小它并不报。文档的例子其实给得很明白:`"if there is a 40 megabyte (MB) region of free memory, and VirtualQuery is called on a page that is 10 MB into the region, the function will obtain a state of MEM_FREE and a size of 30 MB"`,40 MiB 的空洞,咱们从 10 MiB 处问它,读数就是 30 MiB 的剩余。咱们探针要是没落在区段头,读到的数就比整段小,e3b 沿着栈往下探的时候,相邻两次查询读出过嵌套的数字,一查才发现是探针的位置在动。第三件在分组上:区段的合并键里有 `AllocationBase`,相邻、同 State、同 Protect 的两笔独立分配不会并成一段,文档把这一点写进了区段的定义里,属于同一笔初始分配的页才算一段。e1 的 p1、p2 与 e2 的块内探针,都在反复地验它。

## malloc 的底下:堆的三层

地基看完了,咱们回到日常:写 C++ 的人天天用的是 `malloc` 与 `new`,它们离 `VirtualAlloc` 有几层?e5 给出的答案是三层:`malloc` 在 CRT(C 运行时,给 C 与 C++ 程序垫底的那层库)的实现里,往下走到 Win32 的堆 API(`HeapAlloc`/`HeapFree`),堆管理器不够用了再往下,就直接找 `VirtualAlloc` 要新段了。这话空口说是站不住的,咱们拿 `AllocationBase` 判断每笔块的归属,把三层的亲缘关系验出来:

```text
== 步骤1:小块三家同台(32 字节)==
  malloc(32)       req=0x00000020 p=0000019e4e4ceca0 VQ: COMMIT  RegionSize=0x00004000 AllocBase=0000019e4e4c0000 块偏移=0xeca0 主堆段内
  HeapAlloc(默认堆) req=0x00000020 p=0000019e4e4cee20 VQ: COMMIT  RegionSize=0x00004000 AllocBase=0000019e4e4c0000 块偏移=0xee20 主堆段内
  HeapAlloc(私有堆) req=0x00000020 p=0000019e4e900860 VQ: COMMIT  RegionSize=0x00002000 AllocBase=0000019e4e900000 块偏移=0x0860 新段(专属)
  (malloc 与默认堆 HeapAlloc 同一个 AllocBase=0000019e4e4c0000 —— CRT malloc 坐在进程堆上;私有堆另起炉灶)
```

`malloc(32)` 与 `HeapAlloc(GetProcessHeap(), ..., 32)` 的指针落在同一个 `AllocationBase` 里,两个指针只差了 0x180 字节,这就是 CRT 坐在进程堆上的直接证据,咱们装的 UCRT 是 CRT 在 Windows 上的一带实现(微软的通用 C 运行时),它的 malloc 没有另起私有堆。而咱们 `HeapCreate` 出来的私有堆,拿到的是自己的新段。堆的个数也顺路看清了:`GetProcessHeaps` 建私有堆之前就报 2 个,默认堆加上 CRT 或启动链建的,建完变成了 3 个。

块的大小一变,落点就分档了。e5 的尺寸从 4 KiB 一路问到 2 MiB,释放时咱们再看每个落点变成什么样,把档位整理成下面的表,边界是本机本轮的读数:

| 档位 | 落点 | 释放后 |
| --- | --- | --- |
| ≤384 KiB | 主堆段内,与默认堆同一个 AllocationBase | 段不受影响,块回到堆自己的空闲管理 |
| 416 KiB ~ 1016 KiB | 新开堆段,同一新段可装多块 | 段保留,页退订(RESERVE)或缩成 1 页头 |
| ≥1 MiB | 独立区段,堆管理器直发 VirtualAlloc | 整段 FREE,地址空间整个归还 |

三档的证据链里,最硬的是释放后的下场。1 MiB 与 2 MiB 的两笔,`HeapFree` 之后 VQ 读出来的是整段 FREE,`AllocationBase` 也归了零,这说明它们压根不是堆的段,堆管理器替咱们直接走了 `VirtualAlloc`,释放走的也就是整块归还的路,与 e1 步骤 4 的整块语义严丝合缝。中档那批则是标准的新堆段:416 KiB 与 480 KiB 落在同一个新段里,416 KiB 释放后段缩成了 1 页头,480 KiB 释放后整段退订成了 RESERVE,段留在了原地,页还了回去,下次的大块来了还能接着用。主堆段内的小块最平淡,释放只是回到堆自己的空闲管理,段的状态一个字不变。`malloc` 走的同一条链:`malloc(1MiB)` 拿到的是独立新段,free 之后整段都成了 FREE。所以您在 Windows 上写大缓冲区,超过 1 MiB 之后的尺寸,走 `malloc` 的路还是直接走 `VirtualAlloc` 的路,底下落到的都是同一层,差别只在上面有没有堆的簿记。另外有个与错误处理范式相关的冷知识请您记下:HeapAlloc 的文档明说失败时 `"it does not call SetLastError"`,GetLastError 在这一层是问不出东西的,不开 `HEAP_GENERATE_EXCEPTIONS` 的话,判空就是唯一的失败信号(带上它的堆,失败改抛 `STATUS_NO_MEMORY` 一类的异常,e5 里咱们没带)。对齐倒是有承诺,64 位上返回的指针按 `MEMORY_ALLOCATION_ALIGNMENT` 对齐到 16 字节,`malloc` 那一侧的对齐由 CRT 另行保证。

## 另一侧怎么看

咱们把两边对齐着看。Windows 把懒分配做成了显式的两步,承诺记在 CommitCharge 的数字里,预约态是可查可退的,Linux 那边则藏在 overcommit 的默认里,mmap 答应下来就完事了,进程级的承诺数,用户态是拿不到的,两段式在 W02 的 SEC_RESERVE 里还能拼出文件后备的版本。对齐的尺子也分了家,这边是地址与大小的双轨,64 KiB 的粒度加 4 KiB 的页,那边一根 4 KiB 页的尺子量到底,从 mmap 搬代码过来的朋友最容易栽的正是这里。越界的警报差得更远:这边 `PAGE_GUARD` 响完一次就自灭,后续的响要靠再武装,栈的自动生长吃的就是它这一口,那边的 `PROT_NONE` 却是持续封路,虚拟内存 API 篇实测出来的是摸多少次拦多少次,咱们想模仿一次性的话,重埋的活儿得在信号 handler 里自己干。全景的查询这边是 `VirtualQuery` 一段一段地问,咱们 193 步扫完了 128 TiB,Type 给的是分类,主人是给不出的,那边的 maps 一口气全给,行数少了一个量级,每行还带后备文件的路径。malloc 的层次倒是两边同构:Linux 的 glibc 同样在小块走主堆,主堆靠老式的 brk 调用(伸缩数据段末尾的旧接口)长出来,大块则走 mmap 直发的路,阈值也是动态的,跟 e5 的三档对得上号,具体的数字两边都得看本机。

还有一件 Windows 这边没讲完的事,留给下一篇的正文:咱们把 `INVALID_HANDLE_VALUE` 当成文件句柄传出去,映射就不挂任何文件了,拿系统的页面文件当后备,起个名字它就成了跨进程的共享内存。页面文件后备的命名映射对象,就是本篇的 PRIVATE 提交在进程之间的延伸,Linux 侧的 shm_open 在那边等着跟它对表。咱们下一篇见。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="VirtualAlloc function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc"
  />
  <ReferenceItem
    :id="2"
    title="VirtualFree function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree"
  />
  <ReferenceItem
    :id="3"
    title="VirtualProtect function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualprotect"
  />
  <ReferenceItem
    :id="4"
    title="VirtualQuery function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery"
  />
  <ReferenceItem
    :id="5"
    title="Memory Protection Constants (WinNT.h)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/memory/memory-protection-constants"
  />
  <ReferenceItem
    :id="6"
    title="Creating Guard Pages"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/memory/creating-guard-pages"
  />
  <ReferenceItem
    :id="7"
    title="GetSystemInfo function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getsysteminfo"
  />
  <ReferenceItem
    :id="8"
    title="HeapAlloc function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc"
  />
  <ReferenceItem
    :id="9"
    title="GetProcessHeaps function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-getprocessheaps"
  />
  <ReferenceItem
    :id="10"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
</ReferenceCard>
