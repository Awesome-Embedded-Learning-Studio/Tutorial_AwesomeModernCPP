---
title: "IOCP 完成端口"
description: "Windows 异步章第二篇,主角只有一枚内核对象:完成端口。它把上一篇每发请求配一枚事件的收割翻了过来,所有完成汇进一条队列,GetQueuedCompletionStatus 取出的包自带身份。实测七组:e1 三件套逐一相认(乱序三发偏移读、key=4242、OVERLAPPED 指针与投递对号、空队列 813ms 回 258、第二把句柄 key=777 各认各的)、e2 六管道镜像(投递序 1..6、完成序 6 5 4 3 2 1,与上一篇 e5 同场景换收割)、e3 并发值是上限(间隔投喂时 0 与 1 两档 8 包全被同一条线程收走、空闲工线程不醒;一口气 8 包时并发值 1 仍单线同 tick 连收,补跑的并发值 0 第四档则四条工线程同 tick 分掉 8 包)、e4 PQCS 唤醒通道(关停哨兵逐条叫醒工线程、三件套 bytes=111 key=888 ov=0xABCD 原样透传,Linux 侧 eventfd 的同构物)、e5 关句柄在途下场(野路子 CloseHandle 完成包立即以 109 ERROR_BROKEN_PIPE 送达、正路子 CancelIoEx 收 995 再关)、e5b Job 挂端口(NEW_PROCESS 与 EXIT_PROCESS 与 ACTIVE_PROCESS_ZERO 三包字段落点全录:bytes=消息号、ov=pid,兑现进程篇的死讯送达)、e6 规模对照(100 发在途,事件式被 64 的上限逼成 64+36 两段轮询、每醒全量重扫 100 枚,IOCP 单端口零扫描,墙钟同量级、差别在结构的诚实口径)。句柄挂端口后不能再用 ReadFileEx 为文档口径未实测,如实标注"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "文件锁:LockFileEx"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
related:
  - "OVERLAPPED 异步 I/O 与 WaitForMultipleObjects"
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "控制台事件与 APC"
  - "异步 I/O 与事件循环"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 并发
  - 异步编程
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# IOCP 完成端口

上一篇[OVERLAPPED 异步 I/O 与 WaitForMultipleObjects](01-overlapped.md)收尾的时候,咱们手里的收割工具是 `WaitForMultipleObjects`(下面还叫它 WFMO),用法是每一发在途请求都得配上一枚属于自己的手动重置事件。六发读挂上了六枚事件,咱们也验证过它转得动。可那套写法天生就带着两处别扭:WFMO 一次至多等 64 枚(上一篇 e4 量过的硬上限),另一处是醒来之后咱们只知道自己该问了,具体是谁好了,还得咱们把事件挨个问过去。[进程与作业:CreateProcessW 与 Job 对象](../process/01-createprocess.md)里咱们点过一句,再多就得开线程、或者换 IOCP 了。欠下的这句话,咱们在本篇还上。

IOCP 的全名是 I/O 完成端口(I/O completion port),本体是一枚干干净净的内核对象。它办的事,咱们一句话就能说清:完成通知不再挂在每发请求的事件上,而是全部汇进了这枚端口自己的队列,您的线程守在队列的出口,取到的每个完成包当场就处理掉,处理完了再回去取。咱们问的话也从“哪枚事件亮了”换成了“下一个完成的是谁”,而出队的那一刻答案就已经在了,完成包是自带身份的,不用咱们再问。Windows 上扛大流量的网络服务,底下垫的大都是它,Linux 那边要到 io_uring 出现才有同级的完成式接口,咱们叫它 Windows 异步的支柱,倒也是名副其实的。

咱们这一篇七组实验,要从建端口一路看到一百发在途的规模:e1 里完成包的三件套怎么与投递相认,e3 里四条线程守一个端口、包会落进几条线的手里,e5 里在途请求没收尾就关句柄的两种下场,收尾的 e6 拿一百发在途做规模对照。另有一组 e5b 的加餐,把进程篇(讲 Job 对象的那篇)欠下的那句死讯送达,兑现成了三枚完成包。

编号与环境的口径,咱们照旧交代在开头。本篇的实验按 e1 到 e6 编号,中间还插了一组 e5b 加餐,它们与上一篇的 e 系、进程篇两篇的 e 系互不相干,您对存档目录认人就行,代码与原始输出都收在仓库 `code/volumn_codes/vol8/systems-programming/windows/async-io/02-iocp/` 的目录下面。机器是笔者的 Win11 26200(26H2 线),配了 16 颗逻辑处理器,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,编译命令统一给了 `-std=c++20 -Wall -Wextra`(零警告),只有 e5b 用了宽字符入口 wmain,加了一条 `-municode`。输出的捕获日期是 2026-10-04,计时沿系列惯例用 GetTickCount64 的毫秒时戳,打在了每行开头,它的分辨率只有十几毫秒,后文个别时戳比设定间隔宽出来的那截,是时戳量化的动静。在途场景咱们全用命名管道,写的方向从服务端到客户端(PIPE_ACCESS_OUTBOUND),e1 用的则是一份自编码数据文件:65536 字节,每个 8 字节块里存着自己的偏移量,读到 3000 就知道咱们正读在 3000,上一篇 e1 用的也是同一份。跨系统的跑法沿 [Win32 文件 I/O](../file-io/01-win32-file-io.md)(W01)交代的 WSL interop 链路,实验程序个个都是自包含的,这回咱们连公共工具都没请出场,您想单独复现哪一组,复制对应的 .cpp 编译就能跑。

## 建端口与挂句柄:完成包自带身份

`CreateIoCompletionPort` 一个函数就管了两种用法,咱们从参数看它怎么做到的。`FileHandle` 给的是 INVALID_HANDLE_VALUE、`ExistingCompletionPort` 给的是 NULL,这一趟造的是端口,回来的是一枚光杆的、什么都没挂的内核对象。`FileHandle` 给一把开了 FILE_FLAG_OVERLAPPED 的句柄、`ExistingCompletionPort` 给刚造的那枚,这一趟做的是挂接,回来的还是那枚端口。挂接的这一趟还要带上第三个参数 `CompletionKey`,它就是咱们后文一直叫的 key:随句柄登记的自定编号,这把句柄上的每个完成包都会带着它回来,e1 里咱们填的是 4242。文档写得很直白:一把句柄能挂的端口至多一枚,挂上了就归它管,管到咱们把句柄关掉为止,而多把句柄挂同一枚端口,正是它鼓励的用法。e1 的代码长这样,行尾两条注释是咱们为了对看后加的,存档的源码里没有:

```cpp
HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);   // [1] 造端口
// ...
HANDLE f1 = CreateFileA(kPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
ULONG_PTR key1 = 4242;
HANDLE ok_assoc = CreateIoCompletionPort(f1, port, key1, 0);                 // [2] 挂接,key=4242
```

第四个参数 `NumberOfConcurrentThreads` 管的就是并发值,咱们到 e3 再专门回来算它,眼下咱们记两件事:给 0 的含义是处理器数,挂接的那一趟里它会被无视,只有造端口时说了算。文档对它的定性是原话级的,系统允许同时处理完成包的线程数上限,`maximum number of threads that the operating system can allow to concurrently process`。上限这个词请您划下来,e3 的实测会给它一个很直白的注脚。

e1 把文件句柄挂了上去(key=4242),投三发不同偏移的读,偏移是故意乱着给的:3000、1000、2000,三发回来的全是 FALSE 加 997 在途,上一篇的老朋友了。收割这边换了主角:`GetQueuedCompletionStatus`(咱们下面简称 GQCS)循环取包,调用的样子长这样:

```cpp
DWORD bytes = 0;
ULONG_PTR key = 0;
LPOVERLAPPED pov = nullptr;
BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, 1000);
```

原始输出咱们整段贴上来:

```text
[     0 ms] [1] 建裸端口
[     0 ms] CreateIoCompletionPort(INVALID_HANDLE_VALUE,...) = 248;并发值给 0 的含义=处理器数(本机 16)
[     0 ms] [2] 挂文件句柄(key=4242),投三发不同偏移的读
[     0 ms] 挂接返回 248(就是那枚端口)
[     0 ms]   投递#1 Offset=3000: ret=0 gle=997(997 在途)
[     0 ms]   投递#2 Offset=1000: ret=0 gle=997(997 在途)
[     0 ms]   投递#3 Offset=2000: ret=0 gle=997(997 在途)
[     0 ms]   GQCS 第1包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#1 逐一相认(指针同一) 值=3000
[     0 ms]   GQCS 第2包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#2 逐一相认(指针同一) 值=1000
[     0 ms]   GQCS 第3包: ret=1 bytes=8 key=4242 OVERLAPPED* 与投递#3 逐一相认(指针同一) 值=2000
```

248 是当轮分到的句柄值,换一轮就换了值,咱们只认它非空。值得停一停的是 GQCS 那三样出参,咱们管它叫完成包的三件套。bytes 是这发 I/O 实际完成的字节数,连 GetOverlappedResult 都不用咱们再问一遍。key 是挂接时咱们自己填的 4242,哪把句柄的完成,看一眼这个字段就有了答案。

pov 是完成那发请求的 OVERLAPPED 地址,咱们在 e1 里拿它跟三份 ov 数组逐一比对,比对的结果指针同一,值也跟着对上了号:偏移 3000 读到的就是 3000。上一篇里咱们要靠哪枚事件的亮与不亮来分辨谁完成了,现在完成包自己就把家门报了:key 与 pov 都写在包里,身份是跟着完成走的,而不再跟着等待走。

三件套还给工程留了一条惯用的路子:key 管的是“哪把句柄”,粒度到的是连接,pov 管的是“哪一发请求”,粒度到的是操作,两级身份正好对应服务器里的两级上下文。惯用的做法是把 OVERLAPPED 嵌进自定义结构体的头一个成员,投递的时候连着结构体一起给,完成包回来的时候,pov 一到了手,结构体一转就找回整份的上下文,连查表都省了,winnt.h 里的 CONTAINING_RECORD 宏干的就是这个转的活。咱们本篇的实验没搭这个结构,可它的根就是 e1 验过的指针相认:pov 回来的就是投递时那个地址,地址稳,上下文也就稳了。

e1 的后半段,咱们补了两个小观察。空队列上的 GQCS 带 800ms 的限期,实测下来是 813ms 回了 FALSE,gle 给的是 258(WAIT_TIMEOUT),多出来的 13ms 就是开头交代过的时戳量化。GQCS 自己就是个带超时的等待,等待与取包合在了同一个调用里,事件式那边等待加扫描的两步,在端口这儿合成了一步。第二把句柄 f2 也挂上了同一个端口,key 换成了 777,投了一发,完成包就带着 777 回来了:

```text
[     0 ms] [3] 空队列超时 + 第二把句柄另一个 key
[   813 ms] GQCS(空队列,800ms): ret=0 历时 813 ms gle=258(258=WAIT_TIMEOUT)
[   813 ms] 第二把句柄(key=777)的一发: GQCS ret=1 key=777 值=4000 —— 完成包带着各自句柄的 key 回来
```

咱们把多把句柄挂进同一枚端口,分流这层活儿交给了 key,这正是服务器要的形态:一百个 socket 挂上了同一枚端口,一个循环就全收了,乱不了,因为每个包都带着自己的出身。还有一条文档的提醒,咱们如实标注:句柄挂上端口之后,就不能再拿它去调 ReadFileEx 与 WriteFileEx 了,理由是它们各有自己的异步完成机制。上一篇 e2 里咱们刚陪过 ReadFileEx,它的完成例程走的是 APC,APC 的路与端口的路并不并轨,挂了端口,这把句柄的完成通知就归端口管。口径是文档给的,本篇是没有实测的,您以文档为准。

挂上去的设备也不挑。文档里的 file handle 这个词,指的是一切支持 overlapped I/O 的端点:文件、命名管道、mailslot,socket 也是能挂的,文档还专门给 AcceptEx 配了例子。咱们回头盘点实验的挂接对象:e1 挂的是文件,做在途读的 e2、e5、e6 挂的全是管道,e3 与 e4 用的是裸端口:一个设备都不挂,e5b 挂上去的干脆是个 Job 对象,连 I/O 设备的身份都够不上,可取包的路是同一条,一种收割结构就罩住了所有能往端口里送完成包的东西。

## 完成次序:端口收的是完成序

e2 用的是上一篇 e5 的同一套布置:六根命名管道,读端这边按 1 到 6 投了在途读,key 直接用了投递序号,写端是反着来的,按 6 到 1 的次序、隔 60ms 投喂一手。上一篇收割用的是 WFMO,这一篇的收割换成了单线程 GQCS 六连取,用的还是同一套管道,咱们看完成序会不会换了收割工具就变脸:

```text
[    16 ms] 六发在途读按 1..6 投递,全部挂同一端口(写端将按 6..1 投喂,间隔 60ms)
[    16 ms]   投递#1: ret=0 gle=997
...
[    16 ms] 收割循环:单线程 GQCS 六连取
[    94 ms]   完成第 1 个:是投递#6(GQCS ret=1 bytes=8 key=6 值=6)
[   172 ms]   完成第 2 个:是投递#5(GQCS ret=1 bytes=8 key=5 值=5)
[   234 ms]   完成第 3 个:是投递#4(GQCS ret=1 bytes=8 key=4 值=4)
[   297 ms]   完成第 4 个:是投递#3(GQCS ret=1 bytes=8 key=3 值=3)
[   344 ms]   完成第 5 个:是投递#2(GQCS ret=1 bytes=8 key=2 值=2)
[   406 ms]   完成第 6 个:是投递#1(GQCS ret=1 bytes=8 key=1 值=1)
[   406 ms]  投递序: 1 2 3 4 5 6;完成序: 6 5 4 3 2 1
```

投递#2 到投递#6 的五行长一个模样,咱们省去了。完成序给的是 6 5 4 3 2 1,跟上一篇 WFMO 那边的记录一个数都不差。

上一篇的同一场景还留下过一条注脚:句柄信号在多发在途时是分不清谁完成的,所以事件式那边咱们必须一发配一枚手动重置事件。而端口这边,连注脚都翻篇了,取出的包自带 pov 与 key 的两级身份,一枚端口就把 N 发都管住了,事件的配额问题从根上没了。

单线程连取的时候,队列按进队的次序出队,排队的凭据是完成时刻:数据到了的那发进队,投递的次序它一概不认。文档同时在前面把例外立好了,原话的意思是包按 FIFO 的次序进队,取的时候倒可以乱序,乱序要多条线程一起取的时候才见得到,e2 这边取包的只有咱们一条线程,乱序的口子根本没开。同一套场景在两篇里各跑了一遍,咱们等于做了一组对照:完成次序由写端的投喂节奏决定,跟收割侧用了什么工具无关,差别全在收割侧的结构上,上一篇那轮配了六枚事件,这一轮的六发只占一枚端口,连哪发完成的都不用咱们再问,包里都替咱们写着呢。投递的次序是咱们排的,完成的次序是设备定的,同步的代码里这两件事天然贴在一起,异步的代码里咱们就得把它们分开想。

## 四条线程取包:并发值是上限

GQCS 还有一个事件式给不了的性质:它允许咱们让多条线程同时睡在同一个出口上,每个完成包都恰好落到一条线程的手里,拿到同一发的情况不会有。一百枚事件的路数,要么是一条线程抱着等,要么是开几条线程把数组切成几段、各等各的,一枚端口则随便咱们开几条线程一起取,内核保证的是一手交一手。咱们在 e3 里搭的台子是这样的:四条工线程全部睡死在 GQCS(INFINITE) 的等待里,主线程往端口里投的是 8 个包,投的节奏与端口的并发值各分两档,咱们一格一格换着看。包没有走真 I/O 的路,是 `PostQueuedCompletionStatus` 造出来的合成完成包,这个函数是 e4 的主角,您眼下把它理解成往队列里塞包就行。端口A的并发值给 0,本机折成了 16:

```text
[     0 ms] == 端口A:并发值=0(默认=处理器数,本机 16),4 工线程,8 包间隔 50ms ==
[   312 ms]   工3 拿到包#1 key=1 bytes=100 (tid=31360)
...
[   781 ms]   工3 拿到包#8 key=8 bytes=107 (tid=31360)
[   844 ms]   8 包全部有了主,首包被取走时刻 312 ms
[   906 ms]   工3 收到关停标记,收工 (tid=31360)
[   906 ms]   工2 收到关停标记,收工 (tid=6020)
[   906 ms]   工1 收到关停标记,收工 (tid=5588)
[   906 ms]   工4 收到关停标记,收工 (tid=15480)
```

包#2 到包#7 的六行同为工3,时戳从 375ms 排到了 703ms,块尾收完 8 包的历时小结一行,这几处都留在存档里了,咱们略去。8 个包全进了工3 一条线程的口袋,八行的 tid 都是 31360,另外三条工线程从头睡到了尾,头一枚关停标记也落在了工3 的手里。端口B把并发值改成了 1,四线程八包的配置照旧,收包的还是同一条线,只是这回的人换成了工4。

```text
[   906 ms] == 端口B:并发值=1,同样 4 工线程、8 包间隔 50ms ==
[  1219 ms]   工4 拿到包#1 key=1 bytes=100 (tid=23772)
...
[  1672 ms]   工4 拿到包#8 key=8 bytes=107 (tid=23772)
```

中间的六行同为工4,块尾的收包小结、四行关停标记与历时一行也都在存档里,咱们一并略去。端口C的样子更极端,并发值还是 1 的配置,八包是一口气全投的,咱们连一点间隔都没给它:

```text
[  1797 ms] == 端口C:并发值=1,4 工线程,8 包一口气全投(无间隔) ==
[  2109 ms]   工4 拿到包#1 key=1 bytes=100 (tid=32524)
[  2109 ms]   工4 拿到包#2 key=2 bytes=101 (tid=32524)
[  2109 ms]   工4 拿到包#3 key=3 bytes=102 (tid=32524)
[  2109 ms]   工4 拿到包#4 key=4 bytes=103 (tid=32524)
[  2109 ms]   工4 拿到包#5 key=5 bytes=104 (tid=32524)
[  2109 ms]   工4 拿到包#6 key=6 bytes=105 (tid=32524)
[  2109 ms]   工4 拿到包#7 key=7 bytes=106 (tid=32524)
[  2109 ms]   工4 拿到包#8 key=8 bytes=107 (tid=32524)
```

并发值 0 配无间隔的第四格,e3 的原轮没有排上,咱们按矩阵把它补齐:同一个 MSYS2 环境,复用同一份 worker 与投包的架子,单独起了一个进程再跑一场(时戳各自从 0 起算)。原始输出咱们全文贴上:

```text
[     0 ms] == 端口D:并发值=0(默认=处理器数,本机 16),4 工线程,8 包一口气全投(无间隔) ==
[   313 ms]   工3 拿到包#1 key=1 bytes=100 (tid=18172)
[   313 ms]   工4 拿到包#3 key=3 bytes=102 (tid=29752)
[   313 ms]   工2 拿到包#5 key=4 bytes=103 (tid=32736)
[   313 ms]   工4 拿到包#6 key=6 bytes=105 (tid=29752)
[   313 ms]   工2 拿到包#7 key=7 bytes=106 (tid=32736)
[   313 ms]   工1 拿到包#2 key=2 bytes=101 (tid=30960)
[   313 ms]   工4 拿到包#8 key=8 bytes=107 (tid=29752)
[   313 ms]   工3 拿到包#4 key=5 bytes=104 (tid=18172)
[   328 ms]   8 包全部有了主,首包被取走时刻 313 ms
[   391 ms]   工3 收到关停标记,收工 (tid=18172)
[   391 ms]   工2 收到关停标记,收工 (tid=32736)
[   391 ms]   工4 收到关停标记,收工 (tid=29752)
[   391 ms]   工1 收到关停标记,收工 (tid=30960)
[   391 ms] 端口D 收完 8 包,历时 391 ms
[   391 ms] iocp-e3d 完
```

四格凑齐了,端口却换了脾气:分水岭是包到得急不急。间隔投喂的两场(端口A与B)里,并发值 0 与 1 的下场一个样,8 包全落进了同一条线程的手里,空闲的工线程不醒。一口气投 8 包的两场里,轮到并发值说了算:端口C收包的还是同一条线、同 tick 连着收,端口D则把睡着的四条工线程全放了出来,同一个 313ms 的时戳里,四条线分掉了 8 包、tid 一共四个。并发值的语义,咱们在这儿把它说清楚:它管的是上限,而不是配额。派活的事它不管,间隔来包的时候门口有人就够,别的线程睡得好好的。放行的事它才管,包攒成了堆、上限又宽的时候,几条线程一起干是被允许的。文档那句 `maximum number of threads that the operating system can allow to concurrently process` 说的就是至多允许几条,咱们在文档里也找不到一句“保证几条一起干”的承诺。

8 包落进几条线的手里,这是本轮实测读到的现象:端口凭什么这么调度,咱们手上的是文档口径加机制解读,不是实测的定论。醒谁的规则,概念页里有现成的原文:阻塞的线程按 LIFO 的次序被放出来,原文说的是 `the system releases the last (most recent) thread associated with that port`,新包入队的时候,系统会数一数在跑的线程数,不到并发值就放一条等待的线程进来。间隔投喂的两场读的就是同一条规则:取包的线程处理完了一个包、立刻回到 GQCS 门口排下一位,下一包来的时候门口有人,内核顺手就给了它,别的线程连被叫醒的理由都没有,省下的是一次完整的线程唤醒,连带省下了一次上下文切换,头一枚关停标记归了工3、端口B里换工4 顶班,读的都是同一条规则。一口气 8 包的两场也各有各的文档背书:并发值 1 的端口C对上了概念页的专门一段,队列里一直有包等着的时候,在跑的线程再调 `GQCS` 是不会阻塞的,直接就把下一包取走了、一次上下文切换都不发生。并发值 0 的端口D走的则是放行的路,包成堆地涌进来,在跑的线程数远没到 16,睡着的工线程就一条接一条被放了出来。实验里咱们对包的处理只是打印一行的小事,单包的耗时近乎没有,解读里没量到的部分,比如包处理要是耗上几毫秒会不会放更多的线程,咱们不当成实测的定论,您想验证的话,把 worker 里加一段耗时的活再跑,tid 的分布怎么变,您眼见为实。咱们投的虽然是合成包,可它进的队列与真 I/O 的完成包是同一条,排队与唤醒走的都是同一条路。

工程上这给咱们两条实在的提醒。线程池的条数,咱们得看负载与包处理的耗时来定,设计的时候别拿“并发值是 N 就有 N 条线程干活”当成设计的依据,端口守的只是上限这一道,文档给的经验值是池子里的线程至少备到处理器数的两倍,因为取包的线程会被别的等待挡住,挡住了就得有人顶上。另一条马上就派上了用场:关停的包要按线程数发,咱们给四条线程发四枚,每条醒来认领自己的一枚,领了就退场,端口A与端口D的收尾都是这么做的,四枚关停标记把四条工线程全都送走了。

## 裸端口当唤醒通道:PostQueuedCompletionStatus

e4 咱们干脆把 I/O 整个扔掉了,只留下了端口本身。不挂任何设备的裸端口,它的本体就是一条队列:`PostQueuedCompletionStatus`(下面简称 PQCS)负责往里塞包,取的活儿归 `GQCS`,没有 I/O 的时候它也照样转。这一下端口的身份就宽了,它从完成通知的汇合点,兼职成了自定义消息队列,而这个兼职在线程池里有一个标准用途,专门把睡死了的工线程叫醒,或者给它们递收工的通知。e4 是这么搭的:三条工线程睡死在 GQCS(INFINITE) 的等待里,主线程投的则是三枚关停哨兵,也就是 e3 里用过的那类关停标记,咱们把 key 约定成了 -2。工线程的循环咱们原样贴上来,哨兵的认法就藏在里面:

```cpp
void worker(HANDLE port, int id)
{
    g_workers_up.fetch_add(1);
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL g = GetQueuedCompletionStatus(port, &bytes, &key, &pov, INFINITE);
        std::printf("[%6ld ms]   工%d 醒来: GQCS ret=%d bytes=%lu key=%s ov=0x%llx (tid=%lu)\n",
                    ms_now(), id, (int)g, bytes,
                    key == kShutdownKey ? "关停哨兵" : "普通",
                    (unsigned long long)(UINT_PTR)pov, GetCurrentThreadId());
        std::fflush(stdout);
        if (key == kShutdownKey) {
            std::printf("[%6ld ms]   工%d 认出关停,退场\n", ms_now(), id);
            std::fflush(stdout);
            return;
        }
    }
}
```

咱们看它跑起来的样子:

```text
[     0 ms] 裸端口就绪(一枚句柄,没挂任何 I/O 设备)—— 端口自己就是一条完成队列
[     0 ms] [1] 三条工线程睡进 GQCS,主线程逐个 PQCS 叫醒
[   219 ms] 三条线程都已睡进 GQCS,开始投关停包
[   219 ms]   PQCS 关停包 #1: ret=1
[   219 ms]   PQCS 关停包 #2: ret=1
[   219 ms]   工2 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=10548)
[   219 ms]   工2 认出关停,退场
[   219 ms]   PQCS 关停包 #3: ret=1
[   219 ms]   工1 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=22732)
[   219 ms]   工1 认出关停,退场
[   219 ms]   工3 醒来: GQCS ret=1 bytes=0 key=关停哨兵 ov=0xdeadbeef (tid=16416)
[   219 ms]   工3 认出关停,退场
[   219 ms] 三条工线程全部退场,join 干净返回
[   219 ms] [2] 三件套原样透传:bytes=111 key=888 ov=0xABCD
[   344 ms]   收到: bytes=111 key=888 ov=0xabcd —— 塞什么,收什么
[   344 ms] [3] 收尾后再投一枚没人读的包,端口不吃亏
[   344 ms]   PQCS: ret=1 —— 端口没有活线程也接得住,包在队列里等着
```

投下去的三枚关停包,三条线程逐一醒了过来、认出 key、退了场,join 干干净净地返回,全程落在同一个 219ms 的时戳里。哨兵是怎么认出来的?key 是咱们的自留地,咱们跟工线程约好了 -2 是关停,其余的键值一律当正经活,这一层的语义,系统是一无所知的,全靠咱们两边自己约定。e3 用的关停标记是 key=-1,开出来的值不一样,道理是一模一样的,恰恰说明了值本身不重要,约好了就行。

PQCS 的三件套是原样透传的,咱们塞进去的是什么,收出来的就是什么,咱们在 e4 里专门验了一场:bytes 塞的是 111,key 塞的是 888,ov 塞的是 0xABCD,收到的一字不差,连收到的 0xabcd 变成了小写,都是 printf 的 %llx 干的,不是端口动的手。还有一条值得记下的观察:三条线程全都退场了之后再投一枚,返回的是 TRUE,包安安稳稳地排在队列里,咱们不取,它就在那儿一直等着咱们。

合起来的这几条性质,正是唤醒通道该有的样子,而它在 Linux 侧有一个咱们的老熟人:eventfd。本卷 Linux 侧的[timerfd 与 eventfd:时间与事件的 fd 化](../../linux/io-multiplexing/02-timerfd-eventfd.md)篇里,咱们把管道、eventfd、timerfd 一起挂上 epoll,拿 eventfd 把睡在 epoll_wait 里的循环叫醒,好让关停与跨线程的活能插进事件循环。同样的活儿换到 Windows 这边,睡在 GQCS 里的循环用 PQCS 叫醒,连设备都不用咱们挂。[控制台事件与 APC](../process/02-console-apc.md)里 handler 只做一句 SetEvent 的 self-pipe 镜像,换到端口的世界里,正路换成了它。上一篇 e2 留下的那场对拍,咱们到这儿正好补上:完成例程的路子,投递的线程得周期性地进可警告等待、例程才肯跑,而端口这边,线程睡在 GQCS 的出口上、等待与取包本来就是一个动作,不用再绕可警告状态的圈子。

## 在途没收尾就关句柄:两种下场

文档对关句柄这件事的劝告写得很重:在途的 I/O 没收尾之前咱们别关句柄,更别提释放 OVERLAPPED 了,因为驱动可能还在往那块内存里写它的东西。劝告咱们记下了,可完成通知的下场如何,咱们得拿实测来补。

e5 用两根永不投喂的管道:服务端 connect 过后照样睡它的觉,客户端这边投一发 8 字节的在途读,数据是永远等不来的,然后咱们分别走野路子与正路子。OVERLAPPED 咱们全程全局保活,不给驱动写已释放内存的机会,咱们只看通知语义。野路子的走法:读挂着的时候直接 `CloseHandle`。

```text
[     0 ms] == [1] 野路子:读在途,直接 CloseHandle ==
[     0 ms] 投一发在途读: ret=0 gle=997(ERROR_IO_PENDING),然后直接关句柄
[     0 ms] CloseHandle 已返回(句柄值这会儿已经作废),GQCS 等它的完成包,限期 6000ms
[     0 ms] GQCS: ret=0 历时 0 ms gle=109(ERROR_BROKEN_PIPE) pov=就是那发读 —— 野路子的下场,实测口径
```

完成包立即就到了、历时 0ms,GQCS 回的是 FALSE,gle 给的数字是 109(ERROR_BROKEN_PIPE),pov 认的还是那发读。咱们把猜得着的下场摆一摆。头一种猜的下场是石沉大海,等满 6000ms 收一个 258 的超时,可完成包 0ms 就到了,头一种猜法落空了。第二种猜的是撤单回执 995,gle 给的数对不上,也一样落了空。Win11 26200 的命名管道上,关句柄的这个动作本身就把管道判了断,驱动把在途的那发读以断管完成,完成包照常排进了队列,身份也照常相认了,至少在咱们这轮与这个设备类型上,野路子是不丢包的。正路子扮演对照组的角色,咱们拿 `CancelIoEx` 定向撤单,GQCS 收到 995(ERROR_OPERATION_ABORTED) 的完成包,pov 也认了出来,然后咱们才去 CloseHandle,收尾是干净的:

```text
[     0 ms] == [2] 正路子:CancelIoEx 撤单 → 等 995 完成包 → 再关 ==
[     0 ms] 投一发在途读: ret=0 gle=997(ERROR_IO_PENDING)
[     0 ms] CancelIoEx(定向): ret=1
[     0 ms] GQCS: ret=0 历时 0 ms gle=995(ERROR_OPERATION_ABORTED) pov=就是那发读 key=2 —— 撤单也有完成包,995 送到
[     0 ms] 收完 995 再 CloseHandle:干干净净
```

GQCS 回 FALSE 的两种含义,读输出的时候最容易混。pov 为 NULL 的情形,说明咱们压根没取到包,gle 报的是取包本身的失败,e1 空队列里那个 258 走的是头一条路。pov 不为 NULL 的时候,取到的就是一个失败的完成包,gle 报的则是那发 I/O 的死因,109 与 995 走的都是后者。要是把两种 FALSE 混在了一起,排查异步错误的时候,咱们会把超时当成断管,反过来把断管当成了超时。

995 这个码是咱们的老熟人了:[文件锁:LockFileEx](../file-io/05-lockfileex.md)(W05)里它跟着撤掉的锁走,上一篇 e3 里它跟着撤掉的读走。在途请求的几个错误码到这儿凑齐了。997(ERROR_IO_PENDING) 是投递的回执,ReadFile 回 FALSE 时带的就是它,意思是这一发已经在途了。996(ERROR_IO_INCOMPLETE) 是在途的探针,上一篇 e3 里咱们拿 `GetOverlappedResult` 问状态,对在途的答复就是它。995(ERROR_OPERATION_ABORTED) 是撤单的回执。109 又是另一种身份:设备断掉了、请求以失败收场。关设备前的正路顺序就一句:`CancelIoEx` 撤干净、收完了 995、再去关句柄。

## Job 挂端口:死讯以完成包送达

进程篇欠下的另一句话,咱们到 e5b 来还。[进程与作业:CreateProcessW 与 Job 对象](../process/01-createprocess.md)讲 Job 对象的那会儿说过:Linux 那边 SIGCHLD 是内核推过来的死讯,Windows 是没有这路推送的,咱们想异步地等一群进程,正路就是 Job 挂端口的办法,进程的创建与退出会以完成包的形式异步送达,细节留给了本章。现在机器就位了,咱们把它跑出来。

咱们把剧本分成三步。Job 建好了之后,`SetInformationJobObject` 的信息类别给的是 `JobObjectAssociateCompletionPortInformation`,咱们把端口与自定的 key 填进 `JOBOBJECT_ASSOCIATE_COMPLETION_PORT` 结构,挂接就算成了:

```cpp
JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp{};
acp.CompletionKey = (PVOID)(UINT_PTR)0x99;
acp.CompletionPort = port;
SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &acp, sizeof acp);
```

第二步咱们用 CREATE_SUSPENDED 拉起子进程,孩子入组走的是 `AssignProcessToJobObject`,放行用的则是 `ResumeThread`。

挂起出生的两段式是进程篇用熟的工具,在这儿它防的是孩子抢跑:子进程跑的是 cmd /c exit 7,放行之后活个几毫秒就退了,要是它在挂接落定之前就退了,消息就赶不上了,文档也提醒了同一件事,最好趁 Job 安静的时候做挂接,挂起出生正好连这一层也一起照顾到了。程序用的宽字符入口 wmain,编译加了 `-municode`,环境口径在开头交代过了。收消息的循环也交代一句:实验里咱们用限期 3000ms 的 GQCS 连收最多 12 枚,认到了 ACTIVE_PROCESS_ZERO 就收队,超时空手而归的那一路是留给万一的保险,本轮的保险没派上用场。原始输出咱们全文贴上:

```text
[     0 ms] Job 挂端口: SetInformationJobObject ret=1(key=0x99)
[     0 ms] 子进程已建(pause 状态,pid=29036),已入 Job,现在放行
[     0 ms] GQCS: ret=1 bytes=6 key=0x99 ov=0x716c → NEW_PROCESS(新进程进组)
[     0 ms]   ↑ lpOverlapped 里装的是 pid=29036(十进制)
[     0 ms] GQCS: ret=1 bytes=7 key=0x99 ov=0x716c → EXIT_PROCESS(组内进程退场)
[     0 ms]   ↑ lpOverlapped 里装的是 pid=29036(十进制)
[     0 ms] GQCS: ret=1 bytes=4 key=0x99 ov=0x0 → ACTIVE_PROCESS_ZERO(组里没有活进程了)
[     0 ms] ACTIVE_PROCESS_ZERO 到了,收队
[     0 ms] 子进程退场码 = 7(建的时候让它 exit 7);已知 pid=29036 —— 拿这两个数对上面的字段落点
[     0 ms] iocp-e5b 完
```

三连包咱们拿到了手,对着文档的表格逐格核过一遍,字段的落点收进一张表:

| GQCS 出参 | 装的是什么 | 咱们收到的三包 |
| --- | --- | --- |
| lpNumberOfBytes | 消息号,报的是哪一类事件 | 6、7、4 |
| lpOverlapped | 消息附带的进程号,或 NULL | 0x716c、0x716c、NULL |
| lpCompletionKey | 挂接时咱们自定的 key | 三包都是 0x99 |

三枚消息号咱们挨个认。6 号的 NEW_PROCESS 报的是新进程进组,7 号的 EXIT_PROCESS 报的是组内进程退场,4 号的消息是 ACTIVE_PROCESS_ZERO,说的是组里没有活进程了。0x716c 的十进制是 29036,正是创建时的 pid,前两包说的是同一个人,第三包按文档的口径给 NULL,因为这枚消息是不带进程号的。

还有一条值得记下的文档细节:挂接那一刻已经在组里的进程,也会补发一条 NEW_PROCESS 的消息,收包的窗口把挂接之前入组的存量也罩住了,存量进程的进出咱们同样不会漏看。这几包的送达方式,文档里有个很妙的说法,说的是就像 Job 自己调了 `PostQueuedCompletionStatus`。咱们在 e4 里刚验过 `PQCS` 的三件套透传,这儿的通道还是同一条,只是字段的语义换成了系统定死,不像咱们的关停哨兵那样自己约。

有两样东西是完成包不给的,咱们单独交代。头一样缺的是退场码,EXIT_PROCESS 报的只有 pid,退场码是不带的,cmd 那个 7 是拿进程句柄 WaitForSingleObject 加 GetExitCodeProcess 另核的,e5b 输出的倒数第二行做的就是这件事。死讯的送达归端口,收尸的活儿归句柄,两个维度是分开走的,跟 Linux 那边 `SIGCHLD` 报丧、`waitpid` 收尸的分工正好对上。送达保证是第二样:文档原话的意思是,除 `JobObjectNotificationLimitInformation` 挂的那一类限额通知外,消息的定位只是通知,送达是不保证的,没收到的时候,事件也可能已经发生了。所以 ACTIVE_PROCESS_ZERO 适合作收队的信号,不适合作正确性的地基,真要可靠地判断,句柄的等待才是硬依据。文档的提醒还有一条,pid 是会被回收复用的,咱们想拿 pid 反查进程,手里就得一直攥着进程的句柄。

## 一百发在途:挨个问与取出即所得

进程篇留下的那句“再多就得开线程、或者换 IOCP 了”,咱们用 e6 兑现成数字。同一件事咱们做两遍:一根命名管道,读端挂上 100 发 1 字节的在途读、写端一口气投 100 字节,咱们各连跑 5 轮。事件式的那一遍,咱们按上一篇的正路写:100 枚手动重置事件,每一发都配上自己的一枚。麻烦立刻就上了门:WFMO 一次至多 64 枚,上一篇 e4 量过墙的位置,64 枚的时候能过、65 枚换回 WAIT_FAILED 加 87(87=ERROR_INVALID_PARAMETER),所以 100 发只得分成两段轮:64 加 36 的两段。IOCP 的那一遍只要一枚端口,咱们循环 `GQCS` 取 100 次。事件式那遍的收割循环,咱们把存档里的原文贴上来,结构性代价全都写在了这段代码里:

```cpp
while (collected < kN) {
    // 两段轮等:64 + 36,这是 64 墙逼出来的结构
    for (DWORD base = 0; base < (DWORD)kN; base += kChunk) {
        DWORD cnt = (kN - base) > kChunk ? (DWORD)kChunk : (DWORD)(kN - base);
        WaitForMultipleObjects(cnt, &evs[base], FALSE, 100);
        ++st.wake_rounds;
    }
    // 全量扫描:醒来只说明「某一段里有好的」,谁好还得挨个问
    for (int i = 0; i < kN; ++i) {
        ++st.scans;
        if (WaitForSingleObject(evs[i], 0) == WAIT_OBJECT_0) {
            DWORD got = 0;
            if (!taken[i] && GetOverlappedResult(P.cli, &ovs[i], &got, FALSE)) {
                taken[i] = 1;
                ++collected;
            }
            ResetEvent(evs[i]);
        }
    }
}
```

两边的原始记录咱们贴出来:

```text
[     0 ms] 100 发在途 1 字节读,事件式(WFMO 分段 64+36)与 IOCP(单端口循环)各收 5 轮
[     0 ms] 每轮:写端一次性投 100 字节,读端从投满那一刻起计时
[    15 ms] 第1轮: 事件式   0 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  15 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[    47 ms] 第2轮: 事件式  16 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[    78 ms] 第3轮: 事件式  15 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[   109 ms] 第4轮: 事件式  15 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  16 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
[   140 ms] 第5轮: 事件式  16 ms / 扫描 100 次探针 / 收割轮 2 / 完成 100; IOCP  15 ms / 零扫描 / 完成 100 / 句柄 100 事件 vs 1 端口
```

存档的末尾还有一行口径注记,说的就是这层意思,原文咱们略去了。口径咱们老实交代:墙钟是同量级的,事件式的读数在 0 到 16ms,IOCP 的读数在 15 到 16ms。这么小的负载里,管道的 I/O 才是大头,把 100 枚事件扫一遍的那点开销,整个淹没在管道的时延里了,您要是指着表里的墙钟说 IOCP 快,这轮的数据是不撑这个说法的。差别落在了结构上,表里看得见的有三处。头一处是句柄:事件式的开销是 100 枚事件加 1 把管道句柄,IOCP 的开销是 1 枚端口加 1 把管道,请求的规模再涨,前者的句柄跟着涨,后者是不动的。第二处是扫描:事件式的每一轮醒来,只知道自己该问一圈了,把两段各等了一遍,再把 100 枚事件挨个地问过去,上面那段循环里 `++st.scans` 的位置就是探针的位置,5 轮的读数里每轮都是 100 次探针,而 IOCP 的扫描次数是零,取出的每个包就是完成的那发。第三处是分段:100 发多于 64 枚的上限,事件式结构性地绕不开两段轮询,涨到 1000 发就是 16 段的等待,IOCP 的循环还是同一个。

还有一样差别是表里看不见的,咱们单独说:醒来之后拿到的东西不一样。事件式醒来拿到的是“谁就绪了”,数据还得您自己去读。而端口这边,取出的包里连读到的字节数都带着、取出即所得,不用咱们再去读一遍。两种行为各有自己的名字:完成式的那一派叫 Proactor,咱们这一篇的 IOCP 是它的代表。就绪式的那一派叫 Reactor,Linux 的 epoll 是它的代表。两派的完整对照矩阵,连同 epoll 与 io_uring 怎么接进 C++ 的抽象,总纲路线里讲跨平台异步抽象的那一篇会拿它当正题,咱们在这儿点到为止。

批量取出的接口还有一枚 `GetQueuedCompletionStatusEx`,一次调用就能取走一整筐的完成包,大流量的服务里常与端口搭配,本篇是没有测的,您在文档里能查到它。

进程的死讯也进了同一条队列,两篇的地基到这儿齐了。跨平台抽象的那一篇里,咱们把这枚端口带上台面,跟 Linux 侧的 epoll、io_uring 同台对拍,把 Reactor 与 Proactor 的差异收进一张矩阵,再看看 C++ 的抽象怎么把两边都接住,咱们那一篇见。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="CreateIoCompletionPort function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/createiocompletionport"
  />
  <ReferenceItem
    :id="2"
    title="GetQueuedCompletionStatus function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus"
  />
  <ReferenceItem
    :id="3"
    title="PostQueuedCompletionStatus function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/postqueuedcompletionstatus"
  />
  <ReferenceItem
    :id="4"
    title="CancelIoEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/cancelioex"
  />
  <ReferenceItem
    :id="5"
    title="JOBOBJECT_ASSOCIATE_COMPLETION_PORT structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_associate_completion_port"
  />
  <ReferenceItem
    :id="6"
    title="I/O Completion Ports"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/FileIO/i-o-completion-ports"
  />
  <ReferenceItem
    :id="7"
    title="GetQueuedCompletionStatusEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatusex"
  />
</ReferenceCard>
