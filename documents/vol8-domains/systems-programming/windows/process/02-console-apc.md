---
title: "控制台事件与 APC"
description: "Windows 侧进程章第二篇,回答一个 Linux 有现成答案、Windows 要重新问一遍的问题:Ctrl+C 按下去,凭什么变成一次函数调用。SetConsoleCtrlHandler 的链序晚注册排头(实测 C→B→A→Z、TRUE 截断、注销即除名),NULL+TRUE 是只免 CTRL_C 不免 BREAK 的忽略位且被子进程继承;投递模型是每次事件新建一条线程,两次事件两个 tid、OpenThread 探针 gle=87 证明 handler 返回线程即逝;无 handler 的默认归宿是 ExitProcess(0xC000013A),GetExitCodeProcess 与 cmd 的 ERRORLEVEL=-1073741510 双口径一致;进程组六幕:CREATE_NEW_PROCESS_GROUP 的免疫实测就是隐式 NULL+TRUE(同一忽略位,子进程一句 NULL+FALSE 自解),定向 CTRL_C 按文档收不到、按实测收到了,矛盾如实入册;WSL interop 下 CTRL_C 不投递的根因是启动链继承了忽略属性,SetConsoleCtrlHandler(NULL,FALSE) 一句复位,并更正 W03 留下的真窗口归因;重头 APC:609ms 不可警告等待里排队两条零执行,进 SleepEx(TRUE) 同一 tick 内 FIFO 连跑并提早返回 192,APC 复用原线程对照控制台事件另起新线程,QueueUserAPC 掐醒卡死的可警告等待 407ms 拿 WAIT_IO_COMPLETION,不可警告对照掐不动、线程退出积压作废;收尾是 handler 只 SetEvent 的 self-pipe 镜像加四行对照表,ENABLE_PROCESSED_INPUT 关掉后 Ctrl+C 降级成字节 0x03"
chapter: 8
order: 2
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "结构化异常:SEH 与 VEH"
  - "进程与作业:CreateProcessW 与 Job 对象"
related:
  - "进程与作业:CreateProcessW 与 Job 对象"
  - "共享内存:页面文件后备的命名映射对象"
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

# 控制台事件与 APC

[SEH 与 VEH](../file-io/03-seh-veh.md)(后文简称 W03)的收尾处,咱们留过一个现场:咱们把 VEH 与控制台 handler 同时挂在岗上,事件发了出去,VEH 的分发计数是零,ctrl handler 却在**另一条线程**里跑了。当时只交代了半句,说控制台事件走的是新线程,压根儿就没进异常的分发器,细节留给了讲控制台的篇章。欠下的话,这一篇咱们来还上。它和[上一篇](01-createprocess.md)是接着讲的:上一篇里咱们管的是进程怎么创建、怎么用作业对象圈起来治理,这一篇管的是它的另一种结局。您在键盘上按下去的那个 Ctrl+C,在 Windows 里到底走的是哪条路,又凭什么变成了一次函数调用?

问题咱们得从零问起,因为 Windows 的字典里从头到尾就没有 SIGINT。咱们在 Linux 那边看到的 Ctrl+C,是内核投递的信号,handler 受的是一份 async-signal-safe 白名单的管束(这是 POSIX 给信号 handler 定的合法动作清单,清单里写明了哪些函数能在 handler 里调、哪些不能,它住在 man 手册的 signal-safety 页)。Windows 是不投信号的,控制台子系统在事件发生时会去调用进程里登记好的函数,咱们管这一套叫**控制台事件**(console control events),登记用的函数就是本篇头号主角 `SetConsoleCtrlHandler`,咱们顺手把 W03 提过的另一件事也接上:MinGW 的 `signal(SIGINT)` 是 CRT 在上面的模拟,它底下垫的是不是同一处,咱们没拆过,本篇咱们直接用正主。咱们请来的二号主角是 APC(Asynchronous Procedure Call 的缩写,中文的名字叫异步过程调用:别的执行流往目标线程的队列里排一个函数,目标线程要走到特定的时机才肯把它跑掉)。它跟进程其实没有直接的关系,咱们把它请进来,是因为它和控制台事件摊开的是同一道题:**系统怎么把一段代码,塞进一个正忙着别的事情的线程里**。两条线最后都收在了同一个工程答案上:您在 handler 里只做唤醒,处理的事都留给被唤醒的一方。

环境与编号的口径,咱们按惯例交代在开头。本篇的实验按 e1 到 e7 编号,跟仓库 `code/volumn_codes/vol8/systems-programming/windows/process/02-console-apc/` 下面的 01 到 07 七个目录一一对应,程序输出里打的标签是 [E1] 到 [E7],正文里咱们叫它们 e1 到 e7,与前面各篇的 e 系互不相干,您对着目录名认人就不会认错了。机器还是笔者的 Win11 26200,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,编译命令统一走的是 `g++ -std=c++20 -Wall -Wextra`,跑完的 warning 计数是 0,输出的捕获日期是 2026-10-04。咱们的跑法是 WSL interop 直跑 `.exe`,stdio 挂在了管道上,进程连着一个没有窗口的控制台,启动链的怪癖,后文里有专门的小节来追究。实验程序都是自包含的小家伙,`windows.h` 配 `cstdio` 就齐了,W01 定义的 `check_win32` 与思维基石那两篇的 `unique_handle`,这一篇咱们没请出场,您想把某个实验单独复制走复现,不必拖上任何公共的头文件。

interop 环境能测什么、测不了什么,咱们也如实交代。测得了的有三样:`GenerateConsoleCtrlEvent` 的两种事件、`WriteConsoleInput` 往 CONIN$ 注入按键(CONIN$ 是控制台输入缓冲区的设备名,用法与开文件是一样的),再就是 `start /wait` 拉起的真 conhost 窗口,conhost 是控制台窗口的宿主进程。测不了的是真键盘的 Ctrl+C,咱们的环境里没有真人坐在控制台前敲键,实测注入的键记录不会被转成信号,键盘摄入的路径咱们没测。不测的是 SendInput 模拟真键的路子,它会把按键打进当时的前台窗口,打扰您会话的风险不小,咱们主动放弃了。剩下的 CTRL_CLOSE、CTRL_LOGOFF、CTRL_SHUTDOWN 三个事件,触发它们的条件分别是关窗口、注销与关机,咱们同样只录了文档的场景,实测咱们做不了。

## SetConsoleCtrlHandler:晚注册的排在链头

咱们起手把一条 handler 链搭起来。咱们挑四个函数,名字就叫 Z、A、B、C 好了,按报出来的次序注册进去:注册得最早的是 Z,兜底返回 TRUE 的活交给它,最晚注册的则是 C。e1 的舞台是同一个进程,从链序、拦截、注销到忽略位的六幕连着演了下来,咱们一次看全:

```text
$ ./e1_handlers.exe
[E1] pid=12420 main_tid=18328
[1] 链 Z,A,B,C 全 FALSE(C 先 FALSE,Z 兜底 TRUE),发 CTRL_BREAK_EVENT
    [1] 调用链: C(FALSE传递) -> B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[2] C 改返回 TRUE:同一条链再发 CTRL_BREAK,期望链在 C 截断
    [2] 调用链: C(TRUE拦截)
[3] SetConsoleCtrlHandler(C,FALSE) 注销 C -> 1,再发,期望 B->A->Z
    [3] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[4] 现在发 CTRL_C_EVENT —— 本进程由 WSL interop 链启动,忽略位被设上,期望空
    [4] 调用链: (空 —— 没有任何 handler 被调)
[5] SetConsoleCtrlHandler(NULL,FALSE) 复位忽略位 -> 1,再发 CTRL_C,期望 B->A->Z
    [5] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[6] SetConsoleCtrlHandler(NULL,TRUE) 显式忽略 Ctrl+C -> 1:发 CTRL_C 期望空;
    [6-CTRL_C] 调用链: (空 —— 没有任何 handler 被调)
    但紧接着发 CTRL_BREAK —— BREAK 不受忽略位限制,期望 B->A->Z
    [6-CTRL_BREAK] 调用链: B(FALSE传递) -> A(FALSE传递) -> Z(兜底TRUE)
[E1] done
```

第 [1] 幕就是链序的全部:注册的次序是 Z、A、B、C,调用的次序却是 C、B、A、Z,注册得最晚的那个,反而被调得最早。文档的原话咱们请出来,`its handler functions are called on a last-registered, first-called basis until one of the handlers returns TRUE`,晚注册的排在前面一路问下去,直到有人返回了 TRUE。全 FALSE 的时候问到链尾还没人认领,就轮到默认的 handler 收场,而它的身份文档也写得直白,`a default handler function that calls the ExitProcess function`,进程就直接退场了。咱们让 Z 兜底返回 TRUE,为的就是别让默认 handler 把实验的进程收走。

第 [2] 幕给 TRUE 的语义定了性。C 返回了 TRUE,系统就不再往下问了,B、A、Z 连跑的机会都没有,这就是咱们说的“已处理”:TRUE 的语义是截断,而并不是什么成功码。第 [3] 幕演的是注销,`SetConsoleCtrlHandler(C, FALSE)` 一句就把 C 从链上除了名,咱们再发事件,链就变成了 B、A、Z。还有一条文档口径咱们原样记录:`Calling AttachConsole, AllocConsole, or FreeConsole will reset the table of control handlers in the client process to its initial state`,您重新挂上一个控制台,handler 表整个重置回了默认,您换完控制台就得重新注册。

第 [4] 到 [6] 幕看的是同一位角色在变戏法。`SetConsoleCtrlHandler` 的头一个参数传 NULL 时,它管的就不再是链,而是一个进程属性:TRUE 是忽略 Ctrl+C,FALSE 恢复的是正常处理。[4] 里 CTRL_C 发了出去,链却是空的,咱们一行 handler 都没见着,这桩怪事咱们欠一个解释,记下了。到了 [5],一句 `SetConsoleCtrlHandler(NULL, FALSE)` 就把局面翻了过来,CTRL_C 走起了 B、A、Z 的全链。[6] 显式把忽略设了回去,CTRL_C 再次成了空链,可紧接着的 CTRL_BREAK 照走全链。所以忽略位有一个精确的边界:**它只免 CTRL_C,BREAK 不受它的管**,这正是后面进程组实验能成立的地基。这个忽略位是谁设上的、为什么 WSL 起的进程会带着它,咱们在进程组讲完之后专门追究。

handler 收到的事件不止两种。五种事件里咱们实测过的,只有能由 `GenerateConsoleCtrlEvent` 生成的两种,后三种本机触发不了,场景咱们从文档里抄录:

| 事件 | 值 | 触发场景 | 本篇口径 |
| --- | --- | --- | --- |
| CTRL_C_EVENT | 0 | 键盘 Ctrl+C,或 GenerateConsoleCtrlEvent | 实测(受忽略位摆布) |
| CTRL_BREAK_EVENT | 1 | 键盘 Ctrl+Break,或同上 | 实测,永远可达 |
| CTRL_CLOSE_EVENT | 2 | 用户关闭控制台窗口 | 只录文档,未实测 |
| CTRL_LOGOFF_EVENT | 5 | 用户注销,文档说只有服务进程收到 | 只录文档,未实测 |
| CTRL_SHUTDOWN_EVENT | 6 | 系统关机,文档说加载了 gdi32/user32 的进程不送达 | 只录文档,未实测 |

后三种还有时限的说法,同样出自文档的 Timeouts 表:CTRL_CLOSE 给 handler 约 5000 毫秒(系统参数 SPI_GETHUNGAPPTIMEOUT),CTRL_LOGOFF 与 CTRL_SHUTDOWN 的档位也是 5000 毫秒,服务进程的 SHUTDOWN 宽到 20000 毫秒,而 CTRL_C 与 CTRL_BREAK 没有超时,系统不催您。时限的数字咱们没法在本机关窗口验证,如实记为文档的口径。

handler 里能安全做什么,咱们放在 Linux 的镜子前面看。控制台的 handler 跑在了新线程里,不会在任意的指令边界上抢跑,约束天然比信号的 handler 宽。Windows 这边宽是宽了,文档却仍然压着两条硬性的约束。头一条的原话是 `Console functions, or any C run-time functions that call console functions, may not work reliably`,在 CLOSE、LOGOFF、SHUTDOWN 的期间,控制台的内部清理可能已经跑在了您前头,handler 里再去碰控制台的函数,靠不住了。第二条就是上面的时限,您没有从容收尾的余地。咱们把两条一叠加,工程上的正解与 Linux 的 self-pipe 完全同构:handler 里只做最小的动作,把决策与 IO 全都还给了主线程。这个正解的可执行版本,咱们在收尾的 e6 里给出。

## 每次事件,一条新线程:投递模型与默认死法

W03 只证明了 handler 跑在**新线程**里,咱们还有两个问题没落地:每次事件是复用同一条线程,还是各起各的?handler 返回了之后,线程的下场是什么?e2 的 A 段就是冲着它们去的。主线程连发了两次 CTRL_C,咱们把每次 handler 的线程号记下来,咱们再用 `OpenThread` 挨个去开:

```text
$ ./e2_delivery.exe
[E2] pid=26116 main_tid=26004
[A1] 连发两次 CTRL_C:两次 handler 的 tid 各是多少?和主线程比?
    [handler] type=0 tid=30380
    [handler] type=0 tid=11700
[A2] OpenThread 探测 handler 线程是否还活着(活着的线程应能打开):
    对照:活着的 worker tid=25620 -> OpenThread=00000000000000d0(应非空)
    handler tid#1=30380 -> OpenThread=0000000000000000 gle=87(NULL=线程已不存在)
    handler tid#2=11700 -> OpenThread=0000000000000000 gle=87(NULL=线程已不存在)
[A1/A2 小结行见存档,略]
```

三份证据摆在了咱们面前,答案也就齐了。两次事件的 tid 是 30380 与 11700,谁也不是谁的复用,也都不是主线程的 26004,所以**每次事件各起一条新线程,旧的绝不复用**。`OpenThread` 的探针是给“线程还在不在”用的:活着的对照线程一开就开到了句柄,两条 handler 线程却全都返回了 NULL 配 gle=87(gle 就是 GetLastError 报出的错误码,87 报的是 ERROR_INVALID_PARAMETER),它们已经不存在了。handler 的函数一返回,系统起的线程随即就撤,连复用的机会都不留。文档对此有一句直接的交代,原话说的是 `When the signal is received, the system creates a new thread in the process to execute the function`。连发不合并的事也顺带有了证据:两次 CTRL_C 换来的,是两次 handler 的调用,咱们一次都没见少,这跟 Linux 的行为正好是一对反例。Linux 那边的阻塞期连发三次同号信号,pending 的位只记一个,解了阻塞后 handler 只跑一次,投递点还精确落在了两条输出之间的指令边界上,实验同样收在 `code/volumn_codes/vol8/systems-programming/linux/process/04-signal-basic/` 的存档里。

没有 handler 的进程,结局是什么?e2 的 B 段请出了炮灰:一个与父进程共享控制台、同组的子进程,什么 handler 都不注册,出生就把继承的忽略位复位了,父进程发了事件,咱们计时收尸:

```text
[B1] 炮灰就绪,父进程发 CTRL_C_EVENT(炮灰无 handler,只能走默认 handler)
    [handler] type=0 tid=12768
    炮灰 94ms 内死透:exit code=0xC000013A(-1073741510)  [STATUS_CONTROL_C_EXIT=0xC000013A=1]
[B2] 炮灰就绪,父进程发 CTRL_BREAK_EVENT(炮灰无 handler,只能走默认 handler)
    [handler] type=1 tid=23076
    炮灰 93ms 内死透:exit code=0xC000013A(-1073741510)  [STATUS_CONTROL_C_EXIT=0xC000013A=1]
    父进程自己带着 handler 返回 TRUE,两次广播都活着 —— 这就是对照
(炮灰就绪两行与 [E2] done 行在存档,略)
```

两种事件在咱们眼前送出了同一个归宿:94 毫秒与 93 毫秒里炮灰就死透了,退出码给的都是 0xC000013A。这个数就是 winnt.h 里的 `STATUS_CONTROL_C_EXIT`,它是默认 handler 调 `ExitProcess` 时亲手填进去的参数,也是 Windows 世界里的“默认 SIGINT 动作”的落点。两边的记录方式在这里分了家:Linux 的默认动作由内核执行,收尸的 wait status 里记一笔被信号杀,shell 里看到的是 128 加信号号。Windows 把同一个意思写进了进程的退出码,拿到句柄的一方就看得见。读全码有两个口径要留意:父进程的 `GetExitCodeProcess` 读到 -1073741510,`cmd /v:on` 的 `!ERRORLEVEL!` 读到同一个数,干这个活的程序,在存档里的文件名叫 victim_selfsend。WSL 的 `$?` 只剩低 8 位,读不出完整的退出码,您复现的时候别被它骗了。

发事件的 `GenerateConsoleCtrlEvent` 值得咱们单独认识一遍,它的参数就两个。事件种类给的是 CTRL_C_EVENT 或 CTRL_BREAK_EVENT,组号给 0 的时候是广播,送到共享本控制台的全部进程,给一个具体的组号就是定向,只送给指定的那个组。广播咱们已经用过很多次了,定向的语义得配上进程组才有戏,下一节咱们把组造出来。

## 进程组:免疫的机制,就是那个忽略位

进程组(process group)这个词咱们在 Linux 里都熟:一组进程的集合,信号是能按组投递的。Windows 也有同名的概念,造它的法子是 CreateProcessW 带上 `CREATE_NEW_PROCESS_GROUP`,新进程的 pid 同时就是组号。e3 搭的是三个人的舞台:父进程 P,同组的子进程 A,开了新组的子进程 B。A 出生后自己把忽略位复位了,是正常收 Ctrl+C 的进程,B 什么都不动,把新组自带的属性原样留着。三个 handler 返回的都是 TRUE,免得默认的 handler 搅局,打印用跨进程的命名互斥体串行化,六幕的时序如下:

```text
$ ./e3_group.exe
[E3] 父进程 pid=15788
[1] 父发 CTRL_C(0):广播到共享本控制台者 —— 期望 P、A 响应,B 静默(免疫)
    [P handler] type=0 tid=11608
    [A handler] type=0 tid=18660
[2] 父发 CTRL_BREAK(0):BREAK 不受忽略位限制 —— 期望 P、A、B 全响应
    [A handler] type=1 tid=5820
    [B handler] type=1 tid=23888
    [P handler] type=1 tid=24640
[3] 通知 B 自己解除免疫(NULL+FALSE)
  [child-B] 已 SetConsoleCtrlHandler(NULL,FALSE):免疫解除
[4] 父发 CTRL_C(0, 定向 B 组 pgid=14604):文档口径"返回成功但收不到" —— 实测:
    [B handler] type=0 tid=18328
    返回值=1,下面有没有人响应?
[5] 父发 CTRL_BREAK(1, 定向 B 组 pgid=14604):能定向 —— 期望只有 B 响应(P/A 不动)
    [B handler] type=1 tid=25872
[6] 父再发 CTRL_C(0) 广播:B 已解除免疫 —— 期望 P、A、B 全响应
    [B handler] type=0 tid=21616
    [A handler] type=0 tid=11020
    [P handler] type=0 tid=19108
  [尾] A exit=0  B exit=0(都 0 = 免疫是「不投递」,不是「杀掉」)
(子进程就绪的四行、[1] 幕的括注行、[5] 的返回值行与子进程收场两行在存档,略)
[E3] done
```

六幕咱们顺着看。第 [1] 幕广播的是 CTRL_C,P 与 A 都响应了,B 的输出一行都没有,新组对广播的 Ctrl+C 免疫。第 [2] 幕换成的是 CTRL_BREAK 广播,三个全响了,B 的免疫挡不住 BREAK。第 [3] 幕 B 收到了父进程的通知,通知走的是一根命名事件、不是控制台事件,它自己调了一句 `SetConsoleCtrlHandler(NULL, FALSE)`,免疫就解除了。第 [6] 幕再广播 CTRL_C 的时候,三个又全响了,B 与普通进程再无分别了。

免疫的机制是什么?CreateProcess 文档的 Remarks 里有一句原文,直接把两件事接在了同一句话里:`an implicit call to SetConsoleCtrlHandler(NULL, TRUE) is made on behalf of the new process`,新进程出生的时候,系统替它隐式调了一句 NULL 加 TRUE。您认出来了,这正是 e1 第 [6] 幕咱们手动做过的那个调用。所以**新组的免疫不是一个独立的开关,它其实就是那个忽略位**,同一个位、同样地被子进程继承、同样能被一句 NULL 加 FALSE 解除,e3 的 [3] 与 [6] 两幕就是解除与解除之后的实证。它的免疫是给 shell 准备的,文档的下一句原文是 `This lets shells handle CTRL+C themselves, and selectively pass that signal on to sub-processes`,shell 开新组把子进程护了起来、Ctrl+C 留给自己处理,咱们想让哪个组中断,定向把 BREAK 发了过去,后台服务隔离 Ctrl+C 的场景,用的也是同一个开关。

第 [4] 幕是本批实验里咱们与文档打架的一处。GenerateConsoleCtrlEvent 的参数表,给 CTRL_C 的条目写着 `cannot be limited to a specific process group`,组号非零的时候 `this function will succeed, but the CTRL+C signal will not be received by processes within the specified process group`,返回的是成功,但组里的进程收不到。实测不是这样:免疫已经解除的 B,收到了定向发来的 CTRL_C,type=0 的 handler 真跑了,复跑了两轮,结果一致。

::: warning 文档与实测打架,按实测写
GenerateConsoleCtrlEvent 的参数表条目声称非零组号的 CTRL_C “成功但收不到”,在笔者的 Win11 26200 上,免疫解除后的 B 组实收了定向的 CTRL_C,两轮的结果一致。您写可移植的代码,两边咱们都别依赖:定向的 Ctrl+C 到底收不收得到,行为以您目标机器的实测为准。文档与咱们手上的机器,总有一边是咱们不能全信的。
:::

[尾] 行还替免疫定了性质:A 与 B 都以退出码 0 正常收场。免疫干的事只有一件——不投递,进程毫发无损地活着,想收的时候解除就是。咱们再看一眼第 [5] 幕,定向的 BREAK 只有 B 响应,P 与 A 的输出一行都没有,定向是真的定向、没有溅出去半个字。

## interop 起动链的忽略位:真窗口也没躲过

现在回头收拾 e1 第 [4] 幕欠下的怪事:WSL 里起来的进程,CTRL_C 为什么空链。开工之前咱们做了一次普查,`00-env-probe` 把三种启动方式各跑了一遍:

| 启动方式 | 控制台长相 | CTRL_C 事件 | CTRL_BREAK 事件 |
| --- | --- | --- | --- |
| WSL 直跑 | 无窗口,stdin/stdout 是管道,CP=936(GBK 代码页) | 返回 1,handler 0 次 | 投递,handler 1 次 |
| cmd.exe /c 桥接 | 同上 | 同样 0 次 | 投递 |
| start /wait 真窗口 | 有窗口,stdin 是字符设备 | 同样 0 次 | 投递 |

三种方式咱们都跑过了,真 conhost 窗口那一轮的 GetConsoleWindow 已经不是 NULL,stdin 也是真的字符设备了,CTRL_C 的 handler 照样 0 次。连输入缓冲的 mode 咱们都查过,`ENABLE_PROCESSED_INPUT` 本来就是开的,咱们强设一遍再发,拿到的还是 0 次的结果。可见送不进来的原因不在窗口,也不在控制台的输入模式。

根因咱们是从机制文档里抠出来的。SetConsoleCtrlHandler 的参数说明有一句原文,`This attribute of ignoring or processing CTRL+C is inherited by child processes`,忽略或处理 Ctrl+C 的属性,被子进程继承了。咱们没有 API 能查询这个位,所以“WSL 的 interop 启动链设上了它”是从行为反推的归因,证据是它是继承链上唯一自洽的解释,配上 e1 的第 [4] 与第 [5] 幕就对上了:第 [4] 幕的空链,是继承来的忽略位在挡,第 [5] 幕的一句复位,CTRL_C 立刻走起了全链。e2 的炮灰也一样,复位了之后 CTRL_C 才收得着、才死得成,复位这个动作本身就是免疫机制的又一次展示。

W03 那边的话,咱们还有一句要收回。它的收尾处,笔者写过“您要复现,得在真正的控制台窗口里做”。本批实验把这句话推翻了——真窗口同样收不到,当初的归因猜在桥接层,实测把它洗清了,根因落在的是继承来的忽略位。笔者在这里把那句归因正式收回:本篇 interop 节的实测就是依据,W03 正文与存档 README 的修正,等这批整理完了再统一落上去,您要是读过那篇,在那之前请以本篇的口径为准。

## APC:复用原线程的异步调用

控制台事件的路数咱们摸清了:系统另起一条新线程替您跑 handler。Windows 还有第二条塞代码进线程的路子,路数却是完全相反的,它不起什么新线程,借用的是目标线程自己。APC 的入口是 `QueueUserAPC`,文档的机制原话咱们整段请出来,它值得您逐句读:`Each thread has its own APC queue. The queuing of an APC is a request for the thread to call the APC function. The operating system issues a software interrupt to direct the thread to call the APC function.`,每条线程都有自己的 APC 队列,排一个 APC 的意思是“请求目标线程去调这个函数”,发号施令的是操作系统的软件中断。“请求”两个字是全部的要害:您排的函数什么时候跑,主动权不在您这边,是握在目标线程手里的,它得踏进的是**可警告等待**(alertable wait,文档对 bAlertable=TRUE 那类等待的叫法,意思是线程在等待里愿意被打断去清空 APC 的队列,`SleepEx`、`WaitForSingleObjectEx`、`WaitForMultipleObjectsEx` 带 TRUE 的都算)。

排队与执行的这道时间差,咱们请 e4 用打点计时拍成了时序。worker 线程头一段睡的是 `SleepEx(600, FALSE)`,那是一段不可警告的睡眠,睡完紧接着进的是 `SleepEx(5000, TRUE)`,这一段则是可警告的。两条 APC 是主线程赶在它睡头一觉之前排进去的:

```text
$ ./e4_apc.exe
[E4] main tid=8040
[worker] tid=1320 就绪,先卡在 WaitForSingleObject(start)(不可警告)
[main] t=91861281 两条 APC 排完(q1=1 q2=1):worker 正卡在不可警告等待,一条都没跑
[worker] t=91861281 进入 SleepEx(600, FALSE) —— 不可警告睡眠 600ms
[worker] t=91861890 醒来(ret=0),此刻 APC 执行数=0(排队≠执行,1 的证据)
[worker] t=91861890 进入 SleepEx(5000, TRUE) —— 可警告睡眠:队列里的 APC 立刻开闸
    [APC#1] t=91861890 tid=1320 arg=111(与 worker tid 同 = 复用原线程)
    [APC#2] t=91861890 tid=1320 arg=222
[worker] t=91861890 返回 ret=192(WAIT_IO_COMPLETION=192),实际只睡了 609ms,APC 执行数=2
[E4] done —— 读输出顺序:APC#1 恒在 APC#2 前(队列 FIFO);
      两次 APC 的 tid == worker tid(对照控制台事件的【新】线程,APC 复用原线程)
```

咱们把时间线摆开。t=91861281 的时候,两条 APC 排进了队列,worker 随即睡了 609 毫秒的不可警告觉,t=91861890 醒来的时候,APC 的执行数是 0,排了队的函数一条都没跑。下一行是全部的戏码:咱们的 worker 把一脚迈进了 `SleepEx(5000, TRUE)`,APC#1 与 APC#2 跑在了同一个 tick 里连着收尾,排得早的跑在了前面,这正是队列的 FIFO 规则,5 秒的等待根本没睡满,提早带着 192 返回了。192 的身份就是 `WAIT_IO_COMPLETION`,数值给的是 0xC0,它在 NTSTATUS 体系里的另一个名字,写的是 STATUS_USER_APC。名字里带的是 IO,跑的却是 APC 的收尾,文档给 APC 等待的返回用的正是这个码,咱们照着认就行。文档把整段时序写成了原则,原文说的是 `When a user-mode APC is queued, the thread is not directed to call the APC function unless it is in an alertable state. After the thread is in an alertable state, the thread handles all pending APCs in first in, first out (FIFO) order, and the wait operation returns WAIT_IO_COMPLETION`,没进可警告的状态就不执行,一进就按排队的次序清空队列,等待则用 WAIT_IO_COMPLETION 提早返回了,字字都对上了咱们的打点。

两条 APC 的 tid 是 1320,与 worker 自己的 tid 相同。APC 与控制台事件的根本差别,落到证据上就是这个样子的对照:控制台事件**另起一条新线程**替您跑,e2 的两条 handler tid 与主线程互不相同。APC **复用的是原线程**,函数就在目标线程自己的栈上、自己的同步环境里跑。主动权也跟着分了家:控制台事件的线程是系统起的,handler 想什么时候跑就什么时候跑,APC 却得等目标线程自己走进可警告的等待,您排了队,它不睡可警告的觉,您就只能干等。系统底层还有一处交叉的印证:进程在被调试的时候,Ctrl+C 会变成一个叫 DBG_CONTROL_C 的异常,文档的说法是它只给调试器看,调试器把它处理掉了,被调试的进程才不会注意到 CTRL_C,原话补了一句 `an application will not notice the CTRL+C, with one exception: alertable waits will terminate`,唯一的例外是可警告等待会被打断。可警告等待与控制台事件的两套机制,在系统层就是勾连着的。

APC 最正的用法,e5 给了正反两幕:把卡在等待里的线程温和地叫醒。w1 卡在 `WaitForSingleObjectEx` 的可警告等待上,等的句柄永远不会触发,main 400 毫秒后给它排了一条 APC。w2 当的是对照组,卡在不可警告的 `WaitForSingleObject` 上,咱们同样排一条:

```text
$ ./e5_apc_interrupt.exe
[E5] main tid=30648
[1] w1 卡在可警告等待;main 睡 400ms 后 QueueUserAPC(wake_w1, reason=42)
[w1] tid=16944 卡进 WaitForSingleObjectEx(never, INFINITE, /*alertable=*/TRUE)
    [w1 的 APC] t=91862375 在 w1 原线程里跑,reason=42 —— 它把等待掐断
[w1] t=91862375 提前返回 ret=192(WAIT_IO_COMPLETION=192),原计划等到世界末日,只撑了 407ms
[w1] 收尾返回 0 —— 自己退的,没人 TerminateThread
    main 观察:w1 join=0(0=回来了),线程退出码=0,reason_w1=42(42=APC 真跑了)
[2] 对照 w2 卡在【不可警告】等待;main 排 APC(reason=7)再观察 800ms
[w2] tid=6800 卡进 WaitForSingleObject(never, INFINITE) —— 不可警告
    800ms 后:w2 状态=258(258=STILL_WAITING 还卡着),reason_w2=0(0=APC 没跑)
[w2] t=91863578 醒了 ret=0:唤醒我的是 SetEvent,不是 APC(g_reason_w2 仍是 0)
    SetEvent 后 w2 join=0 退出码=0,reason_w2=0(仍 0:线程退出时积压 APC 作废)
[E5] done —— APC 只掐得动可警告等待;线程没进过可警告点就退出,排进去的 APC 白排
```

在咱们看来,w1 的剧本是教科书式的:APC 在它的原线程里跑,等待被它掐断了,407 毫秒就带着 192 提早回来了,收尾、检查、`return 0` 都做完了,退出码也干干净净的,全程连 TerminateThread 的影子都没有。掐醒一个卡死的线程,APC 是唯一温和的外部手段,前提写在了剧本的第一行:它得等在可警告的等待上。w2 的对照给了两个负结果,一个比一个扎心:800 毫秒的观察窗口里 w2 纹丝不动,排队的 APC 进不了执行,SetEvent 放行之后 w2 醒了、退了,积压的 APC 一次都没跑,直接就作废了。作废的事文档有原文背书,`When the thread is terminated using the ExitThread function or the TerminateThread function, the APCs in its APC queue are lost`,线程一退出的时刻,队列就全丢了。所以给线程排 APC 之前,请您确认它的循环里真有可警告的等待在转,不然排了也是白排。

APC 这套机制离咱们不远的另一个证据,是文档点名的一串老熟人:`ReadFileEx`、`SetWaitableTimer`、`WriteFileEx` 的完成通知,原话说的是它们“are implemented using an APC as the completion notification callback mechanism”,底层用的就是 APC。把它们请回台前的活,留给了[总纲](../../00-overview.md)路线里讲 I/O 多路复用与异步 I/O 的那一章,您到时候看到的完成例程,机制用的就是今天这一套。

## handler 里只 SetEvent:与 self-pipe 对拍

约束讲了这么多,咱们可执行的正解就一个 handler。咱们不在里面 printf,也不碰什么控制台,它的全部工作是一句 `SetEvent`,决策与 IO 全都还给了主线程:

```cpp
// e6_setevent_wake.cpp(节选):handler 的全部工作;计数器一行(输出里"共被调 N 次"的来源)略
static HANDLE g_wake;

static BOOL WINAPI handler(DWORD) {
    SetEvent(g_wake);   // 全部工作就这一句;不 printf、不碰控制台
    return TRUE;
}
// 主线程:WaitForSingleObject(g_wake, 5000) 上等,醒来后的活儿全在这边做
```

```text
$ ./e6_setevent_wake.exe
[E6] main tid=17924
[1] main 进 WaitForSingleObject(wake, 5000),然后自己给自己发 CTRL_C_EVENT
    79ms 后醒:ret=0(0=句柄有信号)handler 共被调 1 次 —— 它只干了 SetEvent
[2] main 进 WaitForSingleObject(wake, 5000),然后自己给自己发 CTRL_BREAK_EVENT
    78ms 后醒:ret=0(0=句柄有信号)handler 共被调 1 次 —— 它只干了 SetEvent
[E6] done —— 事件线程只 SetEvent,决策与 IO 全回主线程:与 self-pipe 同构
```

两轮唤醒的成绩是 79 与 78 毫秒,主线程从 `WaitForSingleObject` 上干干净净地醒了过来。您要是读过 Linux 侧的信号篇,这里的结构会看着眼熟:handler 里做的动作是最小的,Linux 那边的做法是往管道 write 一个字节,主循环的 poll 再读回来,起的名字叫 self-pipe。两边的主线完全同构,咱们把四行对照摆开,Linux 侧的证据都在 `code/volumn_codes/vol8/systems-programming/linux/process/` 的 04 与 05 两目录的存档里:

| 对照维度 | Linux(信号) | Windows(控制台事件) | 证据 |
| --- | --- | --- | --- |
| 投递模型 | 任意未屏蔽线程,指令边界插入,阻塞期连发只记 1 次 | 每次事件新建一条线程调 handler,连发各起一条 | 04-signal-basic/01 对 e2 |
| handler 里能做什么 | async-signal-safe 白名单,printf 实测损坏 21 帧(帧即被劈碎的输出行) | 新线程里跑,约束宽,但 CLOSE/LOGOFF/SHUTDOWN 期间控制台函数不可靠且限时,正解只 SetEvent | 04-signal-basic/03 对 e1 与 e6 |
| 唤醒主循环 | self-pipe:handler 里 write(2),主循环 poll | handler 里 SetEvent,主线程 WaitForSingleObject 约 79ms 醒 | 04-signal-basic/05 对 e6 |
| 事件变成可等待对象 | signalfd/pidfd 把信号化成 fd,统一进事件循环 | 控制台输入句柄可直接 WaitFor,WaitFor* 家族统一等一切可等待对象 | 05-signal-advanced/02、03 对 e7 |

末行的 Windows 证据来自 e7 的速览,它还捎带了另一件事:Ctrl+C 的信号身份,是控制台输入模式给的开关。输入缓冲的初始 mode 是 0x1F7,`ENABLE_PROCESSED_INPUT` 的位是开的,Ctrl+C 走的是信号。咱们把它关掉再注入 Ctrl+C,handler 的调用数是 0,`WaitForSingleObject(CONIN$, 1500)` 返回了 0,说明缓冲里有了货,`ReadFile` 实读到了一个字节,读到的值就是 0x03,Ctrl+C 从信号降级成了普通的输入字节,文档口径 `CTRL+C is reported as keyboard input rather than as a signal` 落到了字节上。控制台输入句柄能进 WaitFor 家族的这件事,顺路把对照表的最后一行补齐了。控制台模式的完整展开,咱们留给还没写的终端与控制台那一章,留下的只有这个字节。

进程章 Windows 侧的两篇,到这里就走完了。从链序、新线程、退出码、忽略位到进程组的每一环,咱们都拿实测入了档。APC 的故事刚开了个头,ReadFileEx 们的完成例程,还在总纲路线里讲异步 I/O 的那一章等着,到时候咱们再把它从队列里请出来。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="SetConsoleCtrlHandler function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler"
  />
  <ReferenceItem
    :id="2"
    title="HandlerRoutine callback function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/handlerroutine"
  />
  <ReferenceItem
    :id="3"
    title="GenerateConsoleCtrlEvent function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent"
  />
  <ReferenceItem
    :id="4"
    title="CreateProcessA function(CREATE_NEW_PROCESS_GROUP)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessa"
  />
  <ReferenceItem
    :id="5"
    title="QueueUserAPC function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc"
  />
  <ReferenceItem
    :id="6"
    title="SleepEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex"
  />
  <ReferenceItem
    :id="7"
    title="WaitForSingleObjectEx function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobjectex"
  />
  <ReferenceItem
    :id="8"
    title="SetConsoleMode function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolemode"
  />
  <ReferenceItem
    :id="9"
    title="signal-safety(7) — Linux man-pages"
    publisher="man7.org"
    url="https://man7.org/linux/man-pages/man7/signal-safety.7.html"
  />
</ReferenceCard>
