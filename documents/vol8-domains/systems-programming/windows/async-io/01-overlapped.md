---
title: "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
description: "Windows 异步 I/O 的第一块地基,同一个 ReadFile 从读满才回,到交一个 OVERLAPPED 就在途。三形态一屏对齐:同步句柄带 OVERLAPPED 调用仍阻塞但文件指针实测从 16 跟到 8008(文档原话在列),异步句柄五连发全是 FALSE+997、正对 EOF 的 GOR 回 FALSE+38 而同步侧回 TRUE+0。ReadFileEx 完成例程只在可警告等待里跑:500ms 不可警告等待例程执行数 0,一进可警告同 tick FIFO 连跑、等待以 192 提前返回,hEvent 塞哨兵读回原样。CancelIo 家族的线程归属实测:worker 线程发的读,主线程 CancelIo 撤不动、GOR 仍 996(ERROR_IO_INCOMPLETE),CancelIoEx 不点名才收 995,与 997 凑成三个码三个语义。WFMO 与 MsgWait 两堵墙分开量:WFMO 64 枚过、65 枚 WAIT_FAILED+87(索引 63 照常点名),MsgWait 63 枚过、64 枚 87,消息队列占一个名额,64 减一的说法属于 MsgWait 族。六发在途投递序 1..6、完成序 6 5 4 3 2 1,hEvent=NULL 两发在途句柄第一包到就亮、第二发仍在途分不清谁完成,这就是每发配一枚手动重置事件的实测理由,收尾把事件路数的两处吃紧交给下一篇完成端口"
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 19
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
related:
  - "控制台事件与 APC"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 异步编程
  - 并发
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# OVERLAPPED 异步 I/O 与 WaitForMultipleObjects

[Win32 文件 I/O](../file-io/01-win32-file-io.md)(咱们叫它 W01)里,咱们给 ReadFile 定过性子:同步句柄上的它会读满才回,踩到 EOF 的时候,回的是 TRUE 加 0。发一发的代价就是等一回,等待与干活都挤在了同一个调用里,您想让一把句柄同时背着几发读,它是做不到的。入口 W01 其实点过名:CreateFileW 的 dwFlagsAndAttributes 里住着 `FILE_FLAG_OVERLAPPED` 这个旗标,当时咱们认了个脸熟,说本系列的后续文章会专门请它出场。今天就是赴约的那一场。咱们把这个旗标一开,句柄就按重叠 I/O 的章法办事(overlapped I/O 是 Windows 对异步 I/O 的正式叫法):咱们交一个 OVERLAPPED 结构进去,调用立刻就回来了,读的事情转进了内核,完成的通知咱们另行收割。

OVERLAPPED 的这个结构,您在[文件锁:LockFileEx](../file-io/05-lockfileex.md)(咱们叫它 W05)里已经见过一面:LockFileEx 的区间起点装在 Offset 与 OffsetHigh 里,冲突的请求会回 997、转为排队,CancelIoEx 撤单收的则是 995。那一篇里它等的是一把锁,本篇咱们请它干回本职去读数据。结构的布局与 997、995 这对错误码的出身,W05 都讲过了,这里咱们不再重教,要看的新东西是:在途的读怎么等、怎么收割、怎么撤、等得多了上限在哪,还有 996 这枚 W05 没见过的新码。

咱们手里还攒着一句旧约定,今天也到了兑现的时候。[控制台事件与 APC](../process/02-console-apc.md)的末尾,咱们留过话:ReadFileEx 这些函数的完成通知,底层用的是 APC,把它们请回台前的活,留给了讲异步 I/O 的这一章。本篇的 e2,就是那次约定落地的地方。

编号与环境的口径,咱们照惯例交代在开头。本篇的实验编号是 e1 到 e5,五组实验收在仓库 `code/volumn_codes/vol8/systems-programming/windows/async-io/01-overlapped/` 的同一层里:e1_forms、e2_readfileex_apc、e3_cancel、e4_wfmo_limit、e5_scatter_events,各自的 .cpp 与逐字的 .out 同名成对、直接平铺,另有一份记环境口径的 README.md。e 系的编号跟 file-io 各篇、进程篇的两篇都互不相干,跟同目录下一篇讲完成端口的也是各编各的,您对着文件名认人,就不会认错了。机器是笔者的 Win11 26200,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,命令统一给的是 `g++ -std=c++20 -Wall -Wextra`,拿到的是零警告,输出的捕获日期是 2026-10-04。编译与运行咱们都从 WSL 经 interop 调起,链路的要点 W01 讲过了。计时用的是 GetTickCount64,毫秒级的时戳,每行输出前面都戴着 `[ N ms]` 的前缀。在途的场景,咱们全用了命名管道,写入的活归服务端,异步读的活归咱们这边的客户端。原因说穿了不稀奇:文件的数据就在缓存里,ReadFile 一发当场就做完了,在途的状态是留不住的,而一根不投喂就不完成的管道,天然就是在途的舞台。文件实验(e1 那一场)的数据文件是自编码的:咱们把 65536 字节按 8 字节切块,第 i 块存的就是它自己的偏移 i*8,读回的值是什么,位置就对上了号。公共工具这回咱们一个都没请,`unique_handle` 与 `check_win32` 都免了,五个实验全部是自包含的,windows.h 打头、配上几个标准库的头就能编,您想单独拎一个去复现,不必拖项目的任何公共头文件。

## 三种用法,一次对齐

咱们头一个要紧的问题是:同一个 ReadFile 在同步的句柄、同步句柄加 OVERLAPPED、异步的句柄三种用法下,返回的形态与位置的来源各长什么样。e1 把三种用法装进了同一个程序,数据文件就是自编码出来的 e1data.bin。

[a] 段是同步句柄、lpOverlapped 给 nullptr 的用法,这是 W01 的正主,咱们只回放几行做基线:

```text
$ ./e1_forms.exe
[     0 ms] == [a] 同步句柄 + lpOverlapped=NULL:file-io/01 的正主,回放三连读 ==
[     0 ms] 第1次裸读: ret=1 got=8 值=0 (期望 0)
[     0 ms] 第2次裸读: ret=1 got=8 值=8 (期望 8)
[     0 ms] 第3次裸读: ret=1 got=8 值=16 (期望 16)
[     0 ms] 同步+EOF(0字节可读): ret=1 got=0 (file-io/01 口径:TRUE 且 got=0)
[     0 ms] 同步+骑EOF(4字节可读): ret=1 got=4 (口径:TRUE 且 got=剩余字节数)
```

它的性子是读一次、挪一步,读回的值就是那一段的偏移。EOF 的两例咱们也一起回放了:正对 EOF 的回 TRUE 加 0,骑在 EOF 上的回 TRUE 加 4,这是 W01 已经录下的口径,后头异步版的三例,咱们要跟它们对着看。

到了 [b] 段,句柄还是同步的,咱们给它一个装了 Offset 的 OVERLAPPED。咱们要写的代码只有几行:

```cpp
// e1_forms.cpp(节选):[b] 段的主干与文件指针的探针(中间的 LOG 行略)
ReadFile(h, &val, 8, &got, nullptr);
ReadFile(h, &val, 8, &got, nullptr);

OVERLAPPED ov{};
ov.Offset = 8000;
val = 0;
ok = ReadFile(h, &val, 8, &got, &ov);
ReadFile(h, &val, 8, &got, nullptr);

DWORD file_pointer(HANDLE h)
{
    DWORD ptr = SetFilePointer(h, 0, nullptr, FILE_CURRENT);
    return ptr;
}
```

```text
[     0 ms] == [b] 同步句柄 + lpOverlapped:位置搬进 Offset,调用还是阻塞的 ==
[     0 ms] 预备:两次裸读后文件指针 = 16
[     0 ms] 带 Offset=8000 读: ret=1 got=8 值=8000 gle=0((见 gle)) —— 永远不回 997,同步完成
[     0 ms] 读完文件指针 = 8008(不是 16 了:同步句柄上 OVERLAPPED 读会把指针跟到 Offset+字节数)
[     0 ms] 接着裸读: 值=8008(顺着跟过来的新指针取)—— 同步句柄上两套定位其实共用一条指针
[     0 ms] 同步+OVERLAPPED 骑EOF: ret=1 got=4 gle=0((见 gle))
```

返回的是 TRUE,gle 给的是 0,读回的值也是 8000,位置是对的,可请您留意文件指针:这一读发出之前它明明还是 16,读出来就变成了 8008。文档的 Synchronization and File Position 一节把这件事写成了原话,咱们整句请出来:`If lpOverlapped is not NULL, the read operation starts at the offset that is specified in the OVERLAPPED structure and ReadFile does not return until the operation is complete. The system updates the OVERLAPPED offset and the file pointer before ReadFile returns.`,后半句说的就是眼前的这一幕,系统会在返回之前把 OVERLAPPED 的偏移与文件指针一起更新。所以同步句柄上的 OVERLAPPED 只是换了定位的入口,调用照样是阻塞的,997 是永远等不来的,而且两条定位通道共用的还是同一条指针,咱们带 Offset 读一次,裸读的落脚点也被拖着走了。这个观察咱们后头用得上:异步句柄之所以必须每发请求各带一个 OVERLAPPED、各写各的 Offset,根子就是共享指针的老路被放弃了。

到了 [c] 段,咱们把 `FILE_FLAG_OVERLAPPED` 给上,句柄就换成了异步的。接下来咱们连发两读,拿到的回的全是 FALSE 加 997:

```text
[     0 ms] == [c] 异步句柄(FILE_FLAG_OVERLAPPED):Offset 独立定位,返回二态 ==
[     0 ms] 读 Offset=2000: ret=0 gle=997(ERROR_IO_PENDING) —— 异步句柄上这轮直接回 997 在途(内核没当场做完)
[     0 ms] 读 Offset=1000: ret=0 gle=997(ERROR_IO_PENDING)
[     0 ms] GOR(ovA): ret=1 got=8 值=2000 —— 一把句柄两处偏移,数据各归各
[     0 ms] GOR(ovB): ret=1 got=8 值=1000 —— 投递序 2000→1000,定位靠 Offset 不靠指针
(满读、骑 EOF、正对 EOF 的三发同为 997,贴在下一块)
```

咱们收割在途结果用的 GOR,是 GetOverlappedResult 的简称,那个函数负责取回在途请求的结果,bWait 给 TRUE 的意思就是一直等到完。997 的名字是 ERROR_IO_PENDING,意思是请求收下了、还没做完,它是异步世界的标准回执而不是失败。两发的投递次序是 2000 然后 1000,收回来各归各的值,定位完全听 Offset 的,投递的次序在这里不参与定位。

咱们把 EOF 的三例换到异步句柄上再跑,ReadFile 回的还是清一色的 997,[c] 段的五连发到这儿全数是在途的,真正的分晓要等 GOR:

```text
[     0 ms] 异步+满读(8字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=1 got=8 gle=0((见 gle))
[     0 ms] 异步+骑EOF(4字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=1 got=4 gle=0((见 gle))
[     0 ms] 异步+正对EOF(0字节可读): ReadFile ret=0 gle=997(ERROR_IO_PENDING); GOR ret=0 got=0 gle=38(ERROR_HANDLE_EOF)
```

骑在 EOF 上的那一发,GOR 回的是 TRUE 加 4,跟同步侧的同款实验对得上。正对 EOF 的那一发就分家了:同步侧回的是 TRUE 加 0,异步侧的 GOR 回 FALSE 加 38,38 的名字是 ERROR_HANDLE_EOF。文档把这对行为写成了原话:`If a read operation on a file begins at or beyond the end of the file, then the read operation fails with the error ERROR_HANDLE_EOF. If a read operation on a file begins before the end of the file, but the read operation extends past the end of the file, then the read operation succeeds, and the number of bytes read is the number of bytes that were read before the end of file was reached.` 起点落在 EOF 之前而终点越过 EOF 的读算成功,拿到的字节数是到 EOF 为止的剩余,骑 EOF 那一发得到的 4,依据写在了后半句。起点正对或者越过 EOF 的读,异步的世界里直接判失败。您写跨同步与异步的代码,EOF 的判定姿势得跟着换:同步侧看的是 TRUE 加 got=0,异步侧看 GOR 的 FALSE 加 38。

咱们在 [c] 段的尾巴上还挂了两个探针,一个打的是裸读,另一个打的是文件指针:

```text
[     0 ms] 异步句柄裸读(不给 OVERLAPPED): ret=0 gle=87(ERROR_INVALID_PARAMETER) —— 文档口径:必须给
[     0 ms] 指针推到 8000 后从 Offset=0 读: 值=0(不是 8000)—— 异步句柄只认 OVERLAPPED.Offset,文件指针形同虚设
```

咱们在异步句柄上不给 OVERLAPPED 直接裸读,拿回的是 87(ERROR_INVALID_PARAMETER),参数表的原文写得很硬:`A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED`,写明是必填的。更说明问题的还落在第二行:咱们拿 SetFilePointer 把指针推到 8000,然后从 Offset=0 又发了一读,读回的值是 0,也就是第 0 块的内容。指针推了也白推,异步句柄认的只有 Offset。所以它跟 [b] 段正好是一对:同步句柄上的两套定位共用一条指针,异步句柄上的指针干脆退了场,位置只活在每一发的 OVERLAPPED 里。

咱们还有一处口径要如实交代。异步句柄上的 ReadFile,存在内联完成的可能:数据已经就绪的时候,内核当场就做完了,ReadFile 当场就回了 TRUE。文档的 Note 原话是 `subsequent calls to functions such as ReadFile using that handle generally return immediately, but can also behave synchronously with respect to blocked execution`,页面还挂着一篇老文号的链接,文章的标题就叫 Asynchronous disk I/O appears as synchronous,讲的是盘 I/O 在哪些条件下看起来像同步。本机的这一轮里,文件与管道的读全部走了 997 在途的形态,连一次内联的 TRUE 都没有捕捉到。二态是存在的,咱们的实测口径是清一色 997,您在别的机器上跑出 TRUE 也是正常的,那正是二态的另一头。

## ReadFileEx 的完成例程:只在可警告等待里跑

现在该兑现[控制台事件与 APC](../process/02-console-apc.md)末尾的那笔约定了。那一篇引过文档的原话,ReadFileEx、SetWaitableTimer、WriteFileEx 这些函数的完成通知,`are implemented using an APC as the completion notification callback mechanism`,垫在底下的就是 APC。APC 的队列、可警告二态、FIFO、192 那些机制,那一篇早就深讲过了,本篇咱们只把 ReadFileEx 这个正主用户请上台,看它的完成例程在什么条件下才肯跑。

咱们给 e2 布的场景是两根命名管道,写入方分别在放行后的 120 与 160 毫秒投喂数据,主线程投的是两个 ReadFileEx,等待分成了两段。头一段是 500 毫秒的不可警告等待:

```text
$ ./e2_readfileex_apc.exe
[    16 ms] 阶段0: 投递两个 ReadFileEx(投递序 A→B),放行写入方(120/160 ms 后投喂)
[    16 ms] ReadFileEx 返回: A=1 B=1;此刻例程执行数=0(投递不等于执行)
[    16 ms] 阶段1: 500ms 不可警告等待(FALSE)—— 数据会在这 500ms 里到,例程该一个都不跑
[   516 ms] WaitForSingleObjectEx(...,500,FALSE) 返回 258(258=超时),此刻例程执行数=0 —— 数据在管道里,例程在睡觉
```

咱们看到数据在 120 与 160 毫秒就落进了管道,500 毫秒的等待睡满超时,例程的执行数却是 0。这就是 APC 那套主动权的现场版:排在队列里的例程,线程不进可警告的等待就一条都不跑,数据其实到了也一样。ReadFileEx 返回 TRUE 的意思也只是收下了请求,不代表例程已经跑过了。

第二段的等待,咱们一脚踏进可警告状态:

```text
[   516 ms] 阶段2: 进入可警告等待(TRUE,限期 3000)—— 两条积压例程该同 tick 连跑
[   516 ms]   例程#1 跑起来了: err=0 bytes=8 (tid=22928)
[   516 ms]   例程#2 跑起来了: err=0 bytes=8 (tid=22928)
[   516 ms] 可警告等待在 516 ms 返回 192(192=WAIT_IO_COMPLETION),等待历时 0 ms,例程执行数=2
[   516 ms] 到达序: A=1 B=2(FIFO);A 例程时刻=516 ms,B 例程时刻=516 ms;vA=120 vB=160
```

两条例程在同一个 tick 里就连着跑完了,到达的次序 A 在 B 前,排队的次序就是执行的次序,这正是 APC 队列的 FIFO 规则。这一等待的历时是 0 毫秒,回来的码给的是 192,它的名字叫 WAIT_IO_COMPLETION,等待被队列的清空打断了。例程的 tid 与投递线程是同一条,APC 复用原线程的证据,《控制台事件与 APC》那一篇已经拍过了,这里咱们不再重演。单发的那一场(数据约 250 毫秒后到)也录在存档里,例程就跟着数据醒了,等待同样是以 192 返回的,咱们就不重复贴了。

输出的中间还有一行哨兵探针,值得咱们单独一说。笔者往 OVERLAPPED.hEvent 里塞了 1、2、3 当例程的编号,例程跑完之后咱们再读回来:

```text
[   516 ms] 哨兵探针: ovA.hEvent 读回 1, ovB.hEvent 读回 2 —— ReadFileEx 没碰过它们
```

值是原样的、一个字节都没动过。ReadFileEx 的完成报告走的是例程,根本不需要事件的参与,文档把这件事写成了原话:`The ReadFileEx function ignores the OVERLAPPED structure's hEvent member. An application is free to use that member for its own purposes in the context of a ReadFileEx call.` 在 ReadFileEx 的语境里,hEvent 这个成员归您自己使唤,所以拿它当哨兵是安全的,这也是咱们能把例程编号塞进去的依据。

完成例程这一路的好处是回调自带上下文,数据到手的时候,代码已经站在处理函数里了。代价咱们也看清了:线程得周期性地进可警告等待,宿主的形状就是一个带 TRUE 的等待循环,投递的线程得负责回来睡觉。代价的对拍,咱们等下一篇的完成端口出场再说。

## 撤单:CancelIo 只认本线程,CancelIoEx 才是全量

在途的请求等不起了,咱们怎么把它撤下来?W05 里咱们撤过排队的锁请求,CancelIoEx 做的是定向点名,GOR 收的就是 995,读请求是同一对函数的另一个用户。咱们除了在读请求上复验一遍,还要量一件 W05 没量过的事,CancelIo 与 CancelIoEx 的线程归属。e3 的管道干脆不投喂,投喂的延迟写的是 100 秒,在途的状态想留多久就留多久。

咱们看头两组的输出:

```text
$ ./e3_cancel.exe
[     0 ms] == [1] 定向撤单:CancelIoEx 指名道姓 ==
[    16 ms] 投读: gle=997(ERROR_IO_PENDING) —— 无数据,在途
[    16 ms] CancelIoEx(定向): ret=1
[    16 ms] GOR: ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 请求被撤,以 995 收场
[    16 ms] == [2] 全量撤单:CancelIo 清光这把句柄上的在途 ==
[    31 ms] 两发在途读: A gle=997 B gle=997
[    31 ms] CancelIo(不带 OVERLAPPED,全量): ret=1
[    31 ms] GOR(A): ret=0 gle=995(ERROR_OPERATION_ABORTED); GOR(B): ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 一勺烩,都是 995
```

定向的 CancelIoEx 会指名某一发,全量的 CancelIo 不带 OVERLAPPED,同一把句柄上的在途请求就一勺烩了。995 的名字是 ERROR_OPERATION_ABORTED,是请求被撤下的收场码,W05 的锁语境里咱们收过它,这里它是读请求上的正主用户。定向是不是真的定向,[3] 组拿一发邻居做了对照:

```text
[    31 ms] == [3] 定向撤 A,放过 B:B 照常吃数据完成 ==
[    47 ms] 两发在途读: A gle=997(无事件) B gle=997(带事件)
[    47 ms] CancelIoEx 只点 A
[   219 ms] GOR(A): ret=0 gle=995(ERROR_OPERATION_ABORTED); B 的事件等待 0 后 GOR ret=1 got=8 vB=150 —— 撤单不影响邻居
```

B 带了自己的事件,150 毫秒后数据到了,事件也亮了,GOR 取回的是 TRUE 加 vB=150。咱们撤了 A,却没伤着 B 的分毫。接着 [3b] 组的探针咱们单独看,它问的是一个后面还要用到的问题:hEvent 没给事件的在途读,GOR(bWait=TRUE) 靠的是哪路信号。

```text
[   219 ms] == [3b] 探针:OVERLAPPED.hEvent=NULL 的在途读,GOR(bWait=TRUE) 靠什么等 ==
[   437 ms] GOR(bWait=TRUE) 等了 203 ms 返回: ret=1 gle=0((其他)) —— 单发在途时它落在句柄信号上,真等到了数据
(句柄信号的注记一行在存档,略)
```

GOR 把 203 毫秒等满了,等到了数据。它等的东西,是句柄本身的信号位。单发在途的时候这样是可用的,多发的场合共享这一枚信号行不行,咱们把问号记在这里,量法放在 e5 的第二部分。[4] 组还补了一句放心话:撤单是不伤句柄的。

```text
([4] 组的段头在存档,略)
[   453 ms] 第一发已撤(995),同句柄再投一发
[   578 ms] 第二发: 投递 gle=997(ERROR_IO_PENDING),GOR ret=1 got=8 v2=80 —— 句柄没被取消弄坏
```

咱们撤完之后再投一发,它还是照常完成了,取消是不在句柄上留残骸的。下面咱们请出本实验的重头,[5] 组量的是线程归属。安排是 worker 线程发读、咱们的主线程去撤:

```text
[   578 ms] == [5] 线程归属:别的线程发的读,CancelIo(仅本线程)撤不动,CancelIoEx(全线程)才撤得动 ==
[   594 ms] worker(tid=12336)投读: gle=997(ERROR_IO_PENDING)
[   609 ms] 主线程 CancelIo: ret=1;GOR(FALSE): ret=0 gle=996(ERROR_IO_INCOMPLETE(尚未完成,996)) —— 在途纹丝不动,CancelIo 只认调用线程的请求
[   609 ms] 主线程 CancelIoEx(不点名): ret=1;GOR(TRUE): ret=0 gle=995(ERROR_OPERATION_ABORTED) —— 跨线程也照撤
```

主线程的 CancelIo 返回了 TRUE,看着像撤成了,咱们拿 GOR(FALSE) 一探,回的是 996,请求还在途的状态里纹丝不动。换成 CancelIoEx 不点名的形态,一撤就成了,GOR 收的就是 995。文档把两兄弟的分工写得很明白,ReadFile 的 Remarks 里有现成的原话:`CancelIo: This function only cancels operations issued by the calling thread for the specified file handle. CancelIoEx: This function cancels all operations issued by the threads for the specified file handle.` 只认调用线程发的请求,与认这把句柄上全部线程发的请求,差别就是一个字的事。您要是写了投递线程与收割线程分开的结构,想从收割线程撤掉投递线程的在途请求,手里就必须拿的是 CancelIoEx。

996 就是本篇新见的那枚码,咱们把它凑进队伍。997 是 ReadFile 投递那一刻的回执,意思是收下了、没做完。996 的名字是 ERROR_IO_INCOMPLETE,是 GOR(FALSE) 探在途请求时专用的“还没完”。995 是撤单之后请求的收场码。三个码都跟在三个动作的后面,认码的时候,咱们得连动作一起认。

## 上限的两堵墙:WFMO 的 64 与 MsgWait 的 63

收割侧的主力工具是 WaitForMultipleObjects(咱们简称 WFMO,[进程与作业](../process/01-createprocess.md)里等一群孩子的时候用过它)。那一篇咱们留过一句话,说它一次能等的对象顶多 `MAXIMUM_WAIT_OBJECTS` 个,再多就得开线程、或者换 IOCP 了。IOCP 是 I/O 完成端口(I/O Completion Port)的简称,Windows 异步 I/O 的下一站,也是下一篇的正主,咱们收尾时再去赴约。这句话里有两个数是没量过的:常量的具体数值,超了以后返回的又是什么。咱们用 e4 把这道墙实测出来,还审了一桩流传很广的说法,说的是一次最多等 64 减 1、折成 63 个。准不准得看它说的是哪一家函数。

实验的做法是开 130 枚手动重置的事件(设了信号就一直亮到有人亲手 ResetEvent 为止,为什么必须用它咱们引文档时再交代),全部都是无信号的,咱们只在索引 5 与 70 各点一枚信号,然后把 nCount 从 1 一路加到了 130:

```text
$ ./e4_wfmo_limit.exe
[     0 ms] MAXIMUM_WAIT_OBJECTS 编译期常量 = 64
[     0 ms] == WaitForMultipleObjects:nCount 从 63 加到 130 ==
[   203 ms] nCount=  1 → 返回 WAIT_TIMEOUT, gle=0
[   203 ms] nCount= 63 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   203 ms] nCount= 64 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   203 ms] nCount= 65 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
[   203 ms] nCount= 66 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
(100 与 130 两行同为 WAIT_FAILED+87,在存档,略)
[   203 ms] == 边界复核:nCount=64、把信号放在最末一枚(索引 63) ==
[   203 ms] nCount=64、信号在索引 63 → 返回 WAIT_OBJECT_0+63 (点名索引 63)
```

咱们量到,MAXIMUM_WAIT_OBJECTS 编译期的值就是 64。nCount 为 63 与 64 的两档都正常点名返回,65 起回的才是 WAIT_FAILED 配 87。边界复核那一段专门把信号挪到了索引 63,nCount=64 的那一轮照样点名返回,所以 64 这个名额是实打实的。

扫完 WFMO 咱们不换事件,把等待的函数换成 `MsgWaitForMultipleObjectsEx`(咱们跟着叫它 MsgWait)再扫一遍。这一类函数是带消息等待的,咱们给 dwWakeMask 的是 QS_ALLINPUT:

```text
[   422 ms] nCount=  1 → 返回 WAIT_TIMEOUT, gle=0
[   422 ms] nCount= 63 → 返回 WAIT_OBJECT_0+5 (点名索引 5), gle=0
[   422 ms] nCount= 64 → 返回 WAIT_FAILED, gle=87(87=ERROR_INVALID_PARAMETER)
(段头与结论注记两行在存档,略)
```

同一个进程的同一轮里,WFMO 的 64 枚好使,MsgWait 的 64 枚就死了,63 枚以内才是正常的。差别就落在 dwWakeMask 的身上:QS_ALLINPUT 把消息队列也挂进了等待,消息队列自己占了一个名额。文档给 nCount 的说明原话是 `The maximum number of object handles is MAXIMUM_WAIT_OBJECTS minus one`,减掉的那个一,就是消息队列的座。所以 64 减一的说法是有主的,它说的是 MsgWait 这一类带消息等待的函数,WFMO 本尊的上限就是 64。您在别处读到一次只能等 63 个,可别急着搬到 WFMO 的头上,得看它说的是哪一家。

这道墙的工程代价,咱们记在了同一目录下一篇 IOCP 篇的 e6 里:那一场用一根管道背着 100 发在途的读,100 枚事件被 64 的上限逼成 64 加 36 的两段轮询,每段醒来还欠一次全量的重扫,而完成端口那边单条队列零扫描。同一处上限的两种过法,实验的存档已经在仓库里了。

## 散着完成:六发在途与事件的两种给法

异步 I/O 最值钱的性质,笔者认为是投递序与完成序的分家。e5 搭的是六根管道:读端按 1 到 6 的次序投递,写端按 6 到 1 的次序反着投喂,每根的间隔是 60 毫秒,每一发 OVERLAPPED 配一枚手动重置的事件,收割用 WFMO 的任一模式:

```text
$ ./e5_scatter_events.exe
[    15 ms] 六发在途读按投递序 1..6 发出(写端将按 6..1 投喂,间隔 60ms)
[    15 ms]   投递#1: ret=0 gle=997(997=在途)
[    15 ms]   投递#2: ret=0 gle=997(997=在途)
(投递#3 到 #6 同为 997,在存档,略)
[    15 ms] 收割循环:WFMO(六事件,任一,bWaitAll=FALSE)
[    94 ms]   完成第 1 个:是投递#6(GOR ret=1 got=8 值=6)
[   156 ms]   完成第 2 个:是投递#5(GOR ret=1 got=8 值=5)
[   219 ms]   完成第 3 个:是投递#4(GOR ret=1 got=8 值=4)
[   281 ms]   完成第 4 个:是投递#3(GOR ret=1 got=8 值=3)
[   359 ms]   完成第 5 个:是投递#2(GOR ret=1 got=8 值=2)
[   406 ms]   完成第 6 个:是投递#1(GOR ret=1 got=8 值=1)
[   406 ms] 投递序: 1 2 3 4 5 6;完成序:
[   406 ms]  6 5 4 3 2 1 (与写端投喂次序一致,与投递序相反)
```

咱们拿到的完成序是 6 5 4 3 2 1,完成的时刻分别是 94、156、219、281、359、406 毫秒,与写端投喂的次序一一对上。投递序在这里完全丢掉了话语权:发得最早的第 1 发,偏偏等到最后才完成。同步的世界里读序等于发序,那是调用阻塞给担保的,异步把这个担保取消了,数据到得早的那一发完成得也早。收割循环的骨架值得看一眼,事件路数的工程形状都在这几行里:

```cpp
// e5_scatter_events.cpp(节选):收割循环(LOG 行略)
while (done < kN) {
    DWORD w = WaitForMultipleObjects(kN, evs.data(), FALSE, 5000);
    if (w == WAIT_FAILED || w == WAIT_TIMEOUT) { LOG("WFMO 异常: %lu gle=%lu", w, GetLastError()); break; }
    int idx = (int)(w - WAIT_OBJECT_0);
    DWORD got = 0;
    BOOL g = GetOverlappedResult(pipes[idx].cli, &ovs[idx], &got, FALSE);
    ++done;
    order.push_back(idx + 1);
    ResetEvent(evs[idx]);   // 手动重置,收完亲手归零
}
```

六根管道的同一套场景,IOCP 篇的 e2 里换完成端口收割,收回的完成序还是 6 5 4 3 2 1,两篇是互为镜像的,您到时候可以对着看。

事件的另一种给法是干脆不给事件。hEvent 留的是 NULL,完成信号就落在句柄自己的身上,单发在途的时候直接等句柄就行,e3 的 [3b] 已经见过它把 203 毫秒等满。多发在途的时候呢?e5 的第二部分专门量了这个:两发读都带 NULL 的 hEvent,第一包的数据 150 毫秒到,第二包的数据要 450 毫秒才到,咱们去等句柄:

```text
(第二部分的段头在存档,略)
[   422 ms] 单发在途: ret=0 gle=997,直接等【句柄本身】
[   640 ms] WaitForSingleObject(管道句柄): 218 ms 处返回 0 —— hEvent=NULL 时句柄自己当信号
[   640 ms] GOR: ret=1 got=8 值=77
[   640 ms] 两发在途(都 hEvent=NULL): gle=997/997;第一包 150ms 到,第二包 450ms 到
[   797 ms] WaitForSingleObject(句柄)在 157 ms 返回 0 —— 对齐两包:亮在全部完成还是任一完成,数字说了算
[   797 ms] GOR: 第一发 ret=1 值=1;第二发 ret=0 值=0
```

咱们要的答案在末尾两行:句柄在第 157 毫秒就亮了,那一刻第二包的路还差 290 多毫秒。GOR 的探针也作证,第一发回的是 TRUE、值是 1,第二发回的是 FALSE、仍在途中的状态。所以句柄的信号位是任一发完成就置位的,句柄的信号整把只有一枚,醒来只知道有事完成了,偏偏分不清是哪一发。单发的场合它是够用的,多发的场合它就指不了名了,这就是每一发请求配一枚事件的实测理由:咱们给每发各配一枚事件,信号才认得出是哪一发完成的。

事件的第二个讲究是必须手动重置。文档在 OVERLAPPED 的页面上写了警告,警告的前半句说等待函数会把自动重置的事件当场归零,原话给的是 `Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the nonsignaled state.`,接着给的劝告是 `Therefore, you should use a manual reset event`,理由写的是后果,后果的那半句原文是 `if you use an auto-reset event, your application can stop responding if you wait for the operation to complete and then call GetOverlappedResult with the bWait parameter set to TRUE.`。等您再调 GOR(TRUE) 的时候,事件已经不亮了,程序就卡死在那一步了。咱们收割循环里那句亲手 ResetEvent,配的正是手动重置这个前提:事件自己是不会熄的,收完了,咱们自己来熄。

咱们走到这里,事件路数的全套都过了一遍:每发请求一个 OVERLAPPED、一枚手动重置的事件,在途认的是 997 与 996,撤单认的是 995,EOF 认的是 38。这套结构在几发的规模里是够用的,可它有两处天生吃紧的地方,咱们都在实验里摸到了。头一处咱们醒来只知有事、不知是谁,得靠事件的编号去认,而发数一多,WFMO 的 64 就把您逼成分段轮询,每段醒来还欠一次全量的重扫。另一处吃紧的是宿主的形状:ReadFileEx 的完成例程这一路倒是自带上下文,可它要求线程周期性地进可警告等待,循环里离不开带 TRUE 的等待。完成例程与事件的两条路,下一篇的完成端口一起接走。

咱们要的答案是完成端口,挂靠的 CreateIoCompletionPort 与收取的 GetQueuedCompletionStatus 是两个正主函数:完成不再是事件亮了等您认,而是排成一个一个的完成包,包里带着字节数、带着您投递时挂的 key、带着那发 OVERLAPPED 的指针,取出的是哪一个,哪一个就是完成的那发,单条队列也没有 64 的上限。同一套六管道咱们换端口再收一遍,同一处 64 的上限做 100 发在途的规模对照,还有关句柄时在途请求的下场,都在同一目录的下一篇 [IOCP 完成端口](02-iocp.md) 里,实验的存档已经在仓库里等着了。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="ReadFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile"
  />
  <ReferenceItem
    :id="2"
    title="ReadFileEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfileex"
  />
  <ReferenceItem
    :id="3"
    title="OVERLAPPED structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-overlapped"
  />
  <ReferenceItem
    :id="4"
    title="GetOverlappedResult function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getoverlappedresult"
  />
  <ReferenceItem
    :id="5"
    title="WaitForMultipleObjects function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects"
  />
  <ReferenceItem
    :id="6"
    title="MsgWaitForMultipleObjectsEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex"
  />
  <ReferenceItem
    :id="7"
    title="CancelIo function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelio"
  />
  <ReferenceItem
    :id="8"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex"
  />
  <ReferenceItem
    :id="9"
    title="Asynchronous disk I/O appears as synchronous on Windows"
    publisher="Microsoft Support(旧 KB 文号,ReadFile 文档页挂链)"
    url="https://learn.microsoft.com/en-us/troubleshoot/windows/win32/asynchronous-disk-io-synchronous"
  />
</ReferenceCard>
