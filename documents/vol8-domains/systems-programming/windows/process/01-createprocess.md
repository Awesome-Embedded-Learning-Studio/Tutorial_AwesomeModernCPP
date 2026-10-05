---
title: "进程与作业:CreateProcessW 与 Job 对象"
description: "Windows 侧进程章第一篇:CreateProcessW 十个参数的实测解剖——lpApplicationName=NULL 才走完整搜索链而相对名只对 CWD 解析(err=2)、未引号带空格路径埋诱饵 with.exe 即中(Program.exe 攻击复刻)、无诱饵时系统拼接后改写子进程命令行、argv[0] 与真实加载路径解耦、STARTUPINFOEX 句柄白名单外的可继承句柄根本进不了子进程、CREATE_SUSPENDED 两段式挂起出生写 token 再 ResumeThread(prev=1)而 hThread 关掉进程照跑、lpEnvironment 整块替换漏带 PATH 起不来(0xC0000135)。等待与退出码:CloseHandle 后进程仍活(句柄只是观察权)、return 42 与 abort 的 0xC0000409 fail-fast 与 TerminateProcess 原样透传、STILL_ACTIVE=259 陷阱、WFMO 三视角对照 waitpid。四种死法清理矩阵(return 三样全走、直调 ExitProcess 的 atexit 不走但动态 UCRT 仍刷缓冲而 Terminate 反证、TerminateProcess 三样全跳、CTRL_BREAK 优雅退场)。Job 对象:KILL_ON_JOB_CLOSE 显式关句柄与父进程退出两条路都是计时分辨率内处决而对照组孤儿照活、Job 句柄被孩子继承则陪葬失灵、QueryInformationJobObject 统计、40MB 限额 err=1455。嵌套与出走:Job 里生子自动入同一个 Job、breakaway 允许位加创建旗双全才脱籍、硬闯 err=5、静默出走不带旗也脱籍,父 Job 48MB 管住子 Job 256MB(失败点落在远低于父限额处,随进程自身脚印浮动)。argv 的 CRT 现拆与引号拆法分歧,五行对照表收束"
chapter: 8
order: 1
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 22
prerequisites:
  - "共享内存:页面文件后备的命名映射对象"
  - "结构化异常:SEH 与 VEH"
related:
  - "进程创建与生命周期:fork/exec/posix_spawn"
  - "控制台事件与 APC"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
  - 工程实践
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 进程与作业:CreateProcessW 与 Job 对象

内存章收官的时候咱们留过一句话:进程这个词被咱们用了两篇,却始终没正经讲过它自己的事。[共享内存](../memory/02-shared-mem.md)篇里的 `spawn_self` 拿 `CreateProcessW` 把自己按新的命令行再拉一份,那行调用陪咱们跑熟了,可它身上发生过什么,咱们一回都没细看过。本篇咱们把它放上解剖台,两个名字参数怎么分工、句柄怎么挑着继承、环境块怎么整块替换,咱们顺着一路看到进程的退场。而在退场之后,咱们还有后半篇要讲:一个进程好管,一群进程怎么管?Windows 给的答案是 Job 对象(作业对象,把一组进程圈进同一个内核对象里统一管理的机制),后半篇的主角就是它。

Linux 侧的对照从第一行代码就分岔了。那边造进程走的是 fork 加 exec 两步:fork 一次调用返回两次,父进程拿到的是孩子的 pid,孩子在同一个返回点拿到的是 0,同一行代码两边各回各的。`CreateProcessW` 却只返回了一次,父进程回来时手里多了两只句柄,而孩子已经在自己的 `main` 开头跑了,创建者与被创建者反而从没站进同一行代码里。pid 与句柄怎么对应、句柄没了进程还在不在、孩子的死讯怎么送达,这些问题的答案全由开头的分岔决定,咱们后面的大多数小节都会绕回它身上。Linux 侧的[进程创建与生命周期](../../linux/process/01-fork-exec.md)讲的是 fork、exec 与 waitpid 的实测,那边的成稿已经就位了,本篇的对照两边都有实验背书。

编号与环境的口径,咱们照例交代在开头。本篇的实验按 e1 到 e7 编号,Windows 侧的小写 e 系与 W01 到 W05 各篇、以及内存章两篇的 e 系互不相干,您认文件名就不会认错人。e1 解剖的是 `CreateProcessW`,e2 看的是等待与退出码,e3 的主题是四种死法,e4 的主角是 Job 对象,e5 讲的是嵌套与出走,e7 收的是命令行与 argv,代码与原始输出都收在仓库 `code/volumn_codes/vol8/systems-programming/windows/process/01-createprocess/` 下面的六个子目录里。e6 是个例外:它没有自己的子目录,内容是五行对照表的素材整理,收在存档父目录的 README 里,咱们留到末尾收束时用。机器是 Win11 26200.9457 的本机,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,编译命令统一给的是 `-std=c++20 -Wall -Wextra -municode`,拿到的全部是零警告,只有 e7 链接时加了 `-lshell32`,e3 的 DLL 另走 `-shared`。跨系统的跑法沿用 [Win32 文件 I/O](../file-io/01-win32-file-io.md)(W01)交代的 WSL interop 链路,输出的捕获日期是 2026-10-04。另有一条背景请您留意:WSL interop 拉起的 Windows 进程,它自己其实就待在一个 Job 里,e5 的实验驱动程序开场实测 `IsProcessInJob(NULL)` 返回的就是 1。好在 Win8 起一个进程就可以同时属于多个 Job 了,它对本批的实验没有干扰,只是咱们后面解读谁在不在 Job 里的时候,咱们得记得它身上原本就套着一层。公共工具照旧:`unique_handle` 与 `last_error_code` 沿用思维基石两篇的定义,`check_win32` 沿用 W01 的定义,本篇咱们只引用、不重写。

## 解剖 CreateProcessW:两个名字与一条搜索链

十个参数里顶容易混的是头两个。`lpApplicationName` 告诉内核要加载的是哪个文件,`lpCommandLine` 是孩子看到的整行命令。文档给的分界很清楚:两个都非空的时候,前者定的是加载目标,后者定的是命令行,而两者互不相干。`lpApplicationName` 给 NULL 的时候,模块名从头一个空白分隔的 token 里取,token 后面的才算参数。您要是写过 `execve`,会发现对面是另一套:那边的路径与参数在调用时就分好了家,而 Windows 把它们留在两个参数里,而且还允许各说各话。

咱们从 `lpApplicationName` 为 NULL 的搜索链说起。文件名不带路径的时候,系统按文档里的固定顺序找可执行文件:应用自己的目录、父进程的当前目录、32 位系统目录、16 位 System 目录、Windows 目录,最后才是 PATH 里的各个目录。模块名从命令行取的时候,名字里不带扩展名的,而系统还会替它补上 `.exe`。e1 的 (a) 组就是这么跑的:工作目录里没有 fakeecho.exe,系统一路找到了 PATH 里咱们提前布置的 plain 目录,孩子也就能起来了。而孩子的 `argv[0]` 仍然是裸的 `fakeecho.exe`,真实的模块落点得去问 `GetModuleFileNameW`:

```text
(a) app=NULL, cmdline='fakeecho.exe found_via_path'
    [child echo] argv[0]    = fakeecho.exe
    [child echo] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\plain\fakeecho.exe
    -> exit code=0 (0x00000000)
```

咱们换成给 `lpApplicationName` 填相对名,脾气就变了。文档写得直白:给了部分名,函数只用当前的盘符与目录补全,文档的原话是 `The function will not use the search path`,而且扩展名必须自己带,默认是没有的。e1 的 (b) 组拿同一个 `fakeecho.exe` 的相对名去调,CWD 里偏偏没有它,直接吃回来了一个 err=2——同一个名字,(a) 组的搜索能找到,(b) 组连 PATH 的边都不碰。所以您想让系统帮您搜,您就把 app 留空,您想把加载目标固定下来,您就给全路径,相对名落在中间的位置,享受不到搜索链的照顾,还比 NULL 多了一层对 CWD 的依赖。

带空格又不加引号的路径,是这组实验里最有戏的一场。文档的安全注记自己举了例子:命令行 `"C:\Program Files\MyApp -L -S"` 不加引号地传进去,系统会按 `c:\program.exe`、`c:\program files\MyApp.exe` 这样的截断名逐个尝试,要是有恶意用户在那些位置放了一个 `Program.exe`,跑起来的就是它。咱们把这场攻击原样复刻了一遍,埋的诱饵叫 `with.exe`:

```text
(d) app=NULL, UNQUOTED '<abs with space> x', WITH decoy 'with.exe' planted (= Program.exe attack)
    [child dump] argc=3 argv[0]=C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with argv[1]=space\fakeecho.exe
    [child dump] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with.exe
    -> exit code=1 (0x00000001)
```

真实的目录叫 `with space`,诱饵 `with.exe` 就躺在它的上一级。系统拿头一个 token `...\with` 补上了 `.exe`,一查倒是还真有,当场就选中了它,命令行剩下的 `space\fakeecho.exe x` 全变成了参数。孩子加载的是诱饵,`argv` 里却还挂着原路径的半截,连它自己都看不出被掉包了。(d2) 组把诱饵撤了,咱们再跑同一行,系统这回拼接完整 token 找到了真身,而且做了一件咱们没料到的事——它改写了孩子看到的命令行:

```text
(d2) decoy removed, SAME unquoted cmdline
    [child dump] real module= C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with space\fakeecho.exe
    [child dump] GetCommandLineW() = "C:\Users\CHARLI~2\AppData\Local\Temp\vol8_e1\with space\fakeecho.exe" x
    -> exit code=1 (0x00000001)
```

咱们传进去的命令行没有引号,孩子 `GetCommandLineW` 读回来的却带着引号。文档其实预告过这件事,文档的原话是 `CreateProcessW` `can modify the contents of this string`,Remarks 里其实还有一句,操作系统可能给没写全路径的可执行名前面补上完整路径。所以您在孩子命令行里看到的东西,是系统加工过的版本,不等于咱们落笔的那行。工程上该做的事也简单:要么把 `lpApplicationName` 填上全路径,要么给命令行老老实实加引号,两头都不占的话,加载到的是谁,就得看目录里碰巧住了谁。

`argv[0]` 与真实加载路径的解耦,顺路也验掉了。e1 的 (c) 组把 app 填成真身的绝对路径,命令行的头一个 token 却写了 `TOTALLY_FAKE_ARGV0.EXE`,结果孩子照常起跑,`argv[0]` 老老实实报的是假名。给内核看的与给 `argv[0]` 的,本来就是两个独立的来源,文档给的建议也只是让 C 程序员把模块名重复成头一个 token——那是惯例,可不是什么保证。Linux 那边 `execve` 的 `argv[0]` 同样由调用方随手填,这一行两边倒是同构的,e7 里咱们还会看到它更极端的样子。

## 挑着继承:STARTUPINFOEX、挂起出生与环境块

`bInheritHandles` 给的是 TRUE,所有可继承的句柄全数过户给孩子。多线程的程序各自拉起子进程、各传各的句柄时,这套全量继承就坏事了。Win32 给的精细化开关住在 STARTUPINFOEX(STARTUPINFO 的扩展结构,配 `EXTENDED_STARTUPINFO_PRESENT` 旗与一张属性表)里:`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 允许咱们指定一张白名单,名单之外的句柄,就算开着可继承位也过不去了。e1 的 [0] 组开两个文件、都标成可继承,白名单里只列了 log_a,孩子那边的实测:

```text
    [child] WriteFile(h_a=232) -> 1 (err=0)
    [child] WriteFile(h_b=236) -> 0 (err=5)
```

log_a 写进去了,而 log_b 的句柄值在孩子手里成了野值,写一下吃的是 err=5,咱们读回文件看,log_b 也是空的。白名单拦的不是继承这个动作,拦的是名单外的句柄压根没进孩子的句柄表。e4 里咱们还会回到白名单身上——Job 句柄不小心漏进孩子手里的那桩事故,根子也在句柄的继承上。

CREATE_SUSPENDED 是另一件顺手的工具:孩子挂起出生,主线程冻在了起跑线上,父进程想好了再放行。e1 的 [2] 组拿它做了个两段式:挂起出生的时候 token 文件还不存在,父进程趁孩子冻着的时候,把 token 写了下去,咱们再 `ResumeThread` 放行,函数返回的 prev suspend count 是 1,孩子醒来后读到的是 `TOKEN=0xC0FFEE`,按约定退了 7。时序上抢不着的竞态,用挂起出生就全消掉了,e4 的 Job 实验也靠这招杜绝了孩子抢跑进不了 Job 的竞态。同一组还给 `PROCESS_INFORMATION` 的双句柄分了工:`hThread` 用完就关,进程还是照跑的,`hProcess` 留到了最后等退出码。线程句柄管的是那个执行体,进程句柄管的是生死与等待,两只句柄各还各的,谁也拖累不了谁。

环境块的脾气比前两位都凶。`lpEnvironment` 给的是 NULL,孩子继承的是父的整块环境,e1 第 [3] 节的 (a) 组里孩子数出了 46 项,咱们埋的 `VOL8_E1_MARKER=inherited-from-parent` 也在其中。咱们自己建一块传进去,语义就成了整块替换,合并是没有的:第 [3] 节的 (b2) 组补齐 PATH 之后,孩子的环境恰好 3 项,全是咱们放进去的,父块的 46 项里,没有一项原样跟了过来,连 marker 都只剩了个键,值被咱们的新值顶掉了。最狠的是 (b1) 组:极简块里只放了 marker 与 SYSTEMROOT,漏了 PATH,`CreateProcessW` 本身照样成功了,孩子却死在了起跑线上,退出码给的是 3221225781,翻成十六进制就是 `0xC0000135` 了,它的名字叫 `STATUS_DLL_NOT_FOUND`,说的是加载器找不到 DLL 的 NTSTATUS。动态链接的 exe 连找 ucrt 运行库的 DLL 都要吃 PATH,缺了它的话,进程连 `main` 的门都摸不到。您想给孩子定制环境又想留 PATH,那您就得自己把 PATH 一项一项搬过去,系统是不代劳的。

## 等待与退出码:句柄只是观察权

进程拉起来了,父进程手里有什么?两只句柄加一个 pid。咱们从最反直觉的一件讲起:句柄关了,进程死没死?e2 的 [2] 组把 `hProcess` 关掉,睡了 300 毫秒,咱们再拿 pid 去 `OpenProcess`,居然一开就开到了,进程活得好好的。重开的句柄照样能等,等到退出后拿到的还是孩子自选的退出码 5。所以关句柄扔掉的是咱们的观察权,不是进程的命,内核对象要活到最后一只句柄归还的那一刻,才会真正地销毁。`spawn_self` 时代咱们凭直觉也是这么用的,现在有实测背书了:pid 是系统的编号表,句柄是咱们手里的观察权,两件事其实不挨着。

那 pid 回收之后呢?e4 的实验里咱们吃过一回:孩子退出、pid 被系统回收,咱们再拿旧 pid 去 `OpenProcess`,报的是 err=87(`ERROR_INVALID_PARAMETER`)。您想隔着代观察孙辈,就得趁它爹还活着、句柄还有效的时机,把孙句柄提前攥在咱们手里,晚一步连门牌都换人了。e4 存档的 README 里也记着同一条 err=87 的注记,咱们的三代结构实验,照的就是它。

退出码这边咱们看 e2 的 [1] 组,它摆了三种死法,读数全收在下表里了:

| 死法 | 实测退出码 | 咱们读到的是什么 |
| --- | --- | --- |
| main `return 42` | 42(0x0000002A) | 干干净净的自选码 |
| `abort()` | 3221226505(0xC0000409) | fail-fast(快速失败,运行时判定状态不可恢复、直接终止进程的那条路径)一类的码 |
| 父进程 `TerminateProcess(h, 4660)` | 4660(0x00001234) | 调用方给多少,透传多少 |

`return 42` 没什么可说的。`abort()` 的 0xC0000409 值得咱们停一停:它的名字叫 `STATUS_STACK_BUFFER_OVERRUN`,在现代 Windows 上被复用作 `__fastfail` 一类快速失败路径的通用退出码。[SEH 与 VEH](../file-io/03-seh-veh.md)(W03)讲过这套机制的地基——没人接住的异常,拿异常码本身当了退出码,32 位的 NTSTATUS 整个交出去。咱们在 W03 复验 SEH 那批实验的时候,连 0xC0000409 本尊的影子都没等到,这次 `abort()` 把它送上了门:Linux 那边 `abort()` 死掉的孩子,waitpid 读到的是 `WIFSIGNALED` 加 `WTERMSIG=6`,信号与退出码在那边是两个独立的维度,而 Windows 把它整个折叠成了一个 32 位退出码,父进程从此就丢掉了判断孩子是不是信号死的维度,只剩下一个数了。`TerminateProcess` 那行就更好读了,送进去的是 4660,读回来的也是 4660,连伪装都懒得做了。

查退出码的时候,咱们还会遇到一个自带的陷阱。孩子还没退场咱们就去查,`GetExitCodeProcess` 不报什么错,回的是 259:

```text
(d) STILL_ACTIVE 陷阱:进程还没退,GetExitCodeProcess 给 259
    queried while running     exit code=259 (259 = STILL_ACTIVE)
    after real exit:             exit code=77          (0x0000004D)  wait=signaled
```

259 就是 `STILL_ACTIVE` 的哨兵值,ExitProcess 文档的收尾清单里写着,进程的终止状态要从 STILL_ACTIVE 翻成退出值。您想拿 259 当死活的判据,那是靠不住的,孩子真退了,也可能自选了 259 当退出码。判死活的正路是等句柄,退出码咱们只在等到之后取一次。

一群孩子怎么等?e2 的 [3] 组给了 `WaitForMultipleObjects`(下面咱们简称 WFMO)的三种用法,三个孩子睡的时长是 1800、500、1200 毫秒,各自带的码是 11、22、33。咱们逐个拿 `WaitForSingleObject(h, 0)` 轮询,轮出来的真实完成序是 22、33、11,对应的时点是 t+547ms、t+1250ms、t+1844ms。咱们再单发一次 WFMO、把 `bWaitAll` 给 FALSE,它会阻塞到任意一个就绪的瞬间,返回的 0x1 就是 `WAIT_OBJECT_0+1`,直接点名的是头一个完成的孩子索引,时间落在了 t+516ms。传 TRUE 的用法则一把等全,咱们再按句柄序逐个取码对数。这些毫秒数是当轮的快照,复跑时会有几毫秒到几十毫秒的浮动,量级是不变的。与 waitpid 对照着看就很有意思:那边 `waitpid(-1)` 只说有一个结束了,是谁还得咱们再去问,而 WFMO 的返回值直接给了索引。它一次能等的对象顶多 `MAXIMUM_WAIT_OBJECTS` 个,再多就得开线程、或者换 IOCP 了。IOCP 的全名是 I/O 完成端口,它是 Windows 的异步完成通知机制,后面还会跟咱们打交道。这边的分寸与 select 的 1024,是同一类的约束。

## 四种死法:谁有机会告别

清理与告别的部分,是本篇最值得慢慢看的一场。咱们给 e3 的每个孩子安排了同样的三件事:注册一个往 stderr 写日志的 atexit 善后函数,`LoadLibrary` 一个 `DllMain` 里带 attach/detach 日志的 DLL,再往 stdout 留一条不带换行也不 fflush 的标记。唯一的例外在第四案:CTRL_BREAK 案的孩子装的是控制台 handler,收尾的活儿交给 handler 去做,atexit 的注册咱们就没安排。stdout 被实验的驱动程序重定向到了文件,走的是全缓冲,标记只有在真 flush 的那一刻才进得了文件。然后咱们让孩子分别以四种方式退场,善后跑了没有、DLL 的 `DLL_PROCESS_DETACH` 收到了没有、缓冲里的标记活下来没有,三样证据咱们一并回收:

| case | atexit 善后 | DLL_PROCESS_DETACH | stdout 标记 | 退出码 |
| --- | --- | --- | --- | --- |
| `return 0`(CRT exit 路径) | **跑了** | 收到 | 37 字节,写进了文件 | 0 |
| 直调 `ExitProcess(0)` | **没跑** | 收到 | 落进了文件 | 0 |
| 父进程 `TerminateProcess(h, 31337)` | 没跑 | **没收到** | **0 字节,缓冲全灭** | 31337 原样 |
| CTRL_BREAK 事件,handler 里 `ExitProcess(5)` | (该案未注册) | 收到 | 落进了文件 | 5(handler 自选) |

`return` 走的是最完整的路:CRT 的 exit 路径跑 atexit,加载器给所有 DLL 发了 detach,流缓冲也刷了。这里有个文档级的衔接:ExitProcess 的说明写着 `returning from the main function of an application results in a call to ExitProcess`,main 平常的 return,底下就是 CRT 替咱们收的尾。

直调 `ExitProcess` 是最微妙的一档。atexit 的善后没跑。atexit 登记的位置在 CRT 那边,直调 `ExitProcess` 绕开了 CRT 的 exit 路径,登记的事情自然没人执行。但 DLL 的 detach 照发,ExitProcess 文档的清单写得明白,所有已加载 DLL 的入口,会以 `DLL_PROCESS_DETACH` 的名义被调用。真正意外的是缓冲:这套动态链接的 UCRT 工具链上,37 字节的标记仍然写进了文件。机制上是说得通的:ucrtbase 也是以 DLL 的身份挂进来的,它收到 detach 时顺手做了流清理。反向的证据来自 Terminate 那案:跳过 detach 的话,缓冲就成了 0 字节,flush 的活儿是跟着 detach 路径走的。口径咱们按实测写:本机的这套动态 UCRT 上,detach 兜住了它。至于静态链接 CRT 或者 MSVC 的场合会不会丢缓冲,咱们没有测,咱们也不敢替它们打包票——尤其 MSVC 的 /MD 档同样以 DLL 的形式挂着 ucrtbase,同一套 detach 的机制多半也在,丢不丢的答案,得各自实测了才算数。您写必须落进文件的日志,这样的路就别赌了,该 fflush 的时候就 fflush。

`TerminateProcess` 那一列全是空的。文档对它的定性,咱们原样引来,说的是 `used to unconditionally cause a process to exit`,DLL 的全局数据状态可能因此受损。还有一句更干脆的原话:`A process cannot prevent itself from being terminated`。进程没有任何收到通知的机会,善后、detach、flush 三样全跳过了,退出码 31337 是原样透传的。它还是异步的,函数只是发起终止就返回了,您真想知道死透没有,咱们还得拿句柄去等。

第四种是咱们在 Windows 侧能找到的、最接近“优雅杀”的东西。孩子出生的时候带上 `CREATE_NEW_PROCESS_GROUP`,父进程拿 `GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)` 定向地给它发控制台事件,孩子装的 `SetConsoleCtrlHandler` 处理器接到 CTRL_BREAK,从容地做完清理,然后自选了 `ExitProcess(5)` 退场,而 detach 收到、缓冲写进了文件、退出码自选,三样就全齐了。为什么用 CTRL_BREAK 不用 CTRL_C?CreateProcessW 文档交代了:带 `CREATE_NEW_PROCESS_GROUP` 出生时,系统替孩子隐式调了 `SetConsoleCtrlHandler(NULL, TRUE)`,CTRL_C 对它默认是禁用的,而 CTRL_BREAK 不受影响,专门留着当可投递的打断用。

收尾的哲学在两侧就此分岔。Linux 那边是两档界限分明的,SIGTERM 是默认可捕获的,handler 里想怎么收拾就怎么收拾,而 SIGKILL 不可拦。Windows 这边的 `TerminateProcess` 对位 SIGKILL 且更彻底,可咱们往上找“可捕获的那一档”,是没有的:SIGTERM 在这边是没有对应物的,CTRL_BREAK 又有一串自己的前提,咱们得让双方共享同一个控制台,孩子的 handler 也得提前装好,严格说它是控制台事件而不是信号。自备 IPC 通知孩子自杀的做法,才是更通用的工程答案。咱们把这一行对照也记进末尾的五行表。

## Job 对象:一群进程圈进一个把手

单个进程讲完了,后半篇开讲怎么管一群。Job 对象是可命名、可设安全描述的内核对象,创建时是空的,咱们拿 `AssignProcessToJobObject` 把进程一只一只挂进去,之后对 Job 的操作就落在全体成员的头上。文档还有一句要紧的定性,文档的原话是 `After a process is associated with a job, the association cannot be broken`,挂进去就解不开了,咱们想脱离的话,也只有在创建的时候动动脑筋,那是 e5 的主题。

咱们 e4 的统一姿势是挂起出生:孩子以 CREATE_SUSPENDED 的方式拉起,Assign 进了 Job 之后,咱们再 `ResumeThread` 放行,杜绝了孩子抢在 Assign 之前跑掉的竞态。挂了进去之后,头一件事咱们看世袭:孩子在 Job 里、不带任何旗地生孙,孙自动进的是同一个 Job,`IsProcessInJob` 实测回来的是 1。成员资格是顺着进程树往下传的。通行的一种说法是 Chrome 当年就被这层世袭卡住过:自家的进程想再给渲染进程开一层 Job,外面却已经有人把它的启动器套进了 Job,而 Win7 那会儿一个进程能进的 Job 只有唯一一个,在这上头是走不通的,解法咱们留到嵌套一节。

咱们的招牌实验落在 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` 这枚标志上。文档的原话:挂着它的 Job,最后一只句柄关掉的时候,所有关联的进程被终止,之后 Job 对象自己也跟着销毁了。e4 的 [1] 组让心跳孩子跳了 6 拍,随后咱们关掉当时唯一的一只 Job 句柄:

```text
child alive, heartbeats=6; closing the ONLY job handle now
job handle closed -> child wait=0x0 after 0ms, exit code=0 (0x00000000)
heartbeats frozen at 6 (no more lines after death)
```

等待在 0 毫秒内就返回了,延迟落在 `GetTickCount64` 的计时分辨率之内,退出码给的是 0,咱们实测读回来的就是它,心跳冻在了第 6 行,后面一行都没有了。第二条路更贴近真实的工程:父进程退出的时候,它持有的句柄会被系统自动关闭,效果跟显式关闭是一样的。e4 的 [2] 组搭了三代结构,让中间进程建 Job、拉起孙、然后退场:(a) 组挂了标志,咱们等中间进程一退,孙辈的等待 0 毫秒就返回了,心跳停在了 4 行。(b) 组是不挂标志的对照,都过了 1.5 秒,孙辈还活得好好的,心跳涨到了 18 行——孤儿照活(心跳的行数是当轮的快照,浮动的幅度随负载走,咱们判定看的是死活)。两侧治理孤儿的分岔点就在这里:Linux 上父死子亡是没有内建机制的,孤儿会过继给离得最近的 subreaper 收养者(subreaper 是 Linux 那边愿意代管孤儿的祖辈进程,拿 prctl 打上了标记才算数)。Linux 侧进程篇的那批实验咱们记作 Lproc 系,那边连收养者是谁都实测出了意外,而 Windows 这边一枚标志,全体成员就跟着最后一只句柄一起退场了。

> 咱们还实测出一个反例,工程上顶要紧:Job 句柄要是被孩子继承了一份,这套全体跟着退场的陪葬就失灵了。e4 的 [3] 组把 Job 句柄复制成可继承,孩子连它一起拿了一份,驱动程序关掉自己的句柄之后过了 1.2 秒,孩子居然还活得好好的,因为它的手里也握着一只 Job 句柄,Job 永远等不到最后一只句柄的关闭,全体的处决自然也就没发生。您挂 KILL_ON_JOB_CLOSE 的时候,千万别让 Job 句柄漏进孩子的继承名单,STARTUPINFOEX 的白名单在这里正好用得上。

Job 对咱们来说还是现成的观察站,e4 的 [4] 节验的就是它。`QueryInformationJobObject` 报回来的统计里,空 Job 报的全是 0,而两个孩子都活着的时候,`JobObjectBasicProcessIdList` 报的是 2 个 pid,驱动程序自身是不在列的。两个都退场了之后,ActiveProcesses 归了 0,TotalProcesses 记的是 2,TotalPageFaults 累计到了 2783(当轮的快照,数字是随负载浮动的),TotalTerminated 给的是 0。其中自然退出的成员不计入它。文档给的口径,是因违反限额被 Job 终止的成员才计数,咱们本批只验了自然退出不计的这半边,另半边咱们没有造出来测。咱们从生到死都问得着 pid 名单、进程计数与页错误,任务管理器里按 Job 分组的那些数,源头就在它的身上。

限额是 Job 的另一只手。咱们 e4 的 [5] 组给 Job 设了 `JOB_OBJECT_LIMIT_PROCESS_MEMORY` 40MB,读回确认了之后,孩子在里面按 4MB 步进 `VirtualAlloc`:36MB 之后吃的是 err=1455(`ERROR_COMMITMENT_LIMIT`,页面文件的提交额度不足),接着 `new unsigned char[64MB]` 抛了 `std::bad_alloc`,孩子以自选的 42 退场。失败点落在了 36MB,随进程自身的脚印会浮动几 MB,咱们判依据的是限额以内,不抠精确的数字。对照组是不在 Job 里跑的,同样地跑,256MB 是全部通过的,64MB 的 new 也照样成功,失败的原因确实是限额,而不是因为机器。给一组进程封住内存的上限,防住失控的进程吃垮机器,这就是 Job 的日常差事,Chrome 的官方沙箱设计文档写着,每个渲染进程住的都是自家的 Job,咱们把它的链接列进了篇末的参考里。

还有一件 Job 能做而本篇只点到的事:把它关联到 IOCP,进程的创建与退出会以完成包的形式异步送达。Linux 那边 SIGCHLD 是内核推过来的死讯,而 Windows 没有这路推送,句柄本身就是可等待的对象,您想异步等,IOCP 挂 Job 的做法就是正路,细节咱们留到[总纲](../../00-overview.md)的路线里,讲 I/O 多路复用与异步 I/O 的那一章再展开。

## 嵌套与出走

进程挂进了 Job 就解不开,可咱们现实里的进程树偏偏要分层:外层的管理器想圈住全部,内层的组件又想自己开 Job 管自己的人。上一节 Chrome 的窘境卡住的正是这层需求,而 Win8 的嵌套 Job 把它解开了,一个进程可以同时属于层级里的多个 Job。

出走的路有三条,e5 全给咱们试了一遍。世袭是默认的:Job 里生的孩子,自动进的是同一个 Job,咱们拿 `IsProcessInJob` 一验就回来了 1。正路的名字叫双全出走,要的是两样齐:Job 挂上 `JOB_OBJECT_LIMIT_BREAKAWAY_OK` 的允许位,创建方再带上 `CREATE_BREAKAWAY_FROM_JOB` 的旗,孙辈的 `IsProcessInJob` 实测为 0,成功脱了籍。强闯是硬带上旗、但 Job 没给允许位的做法,结果 `CreateProcessW` 当场失败,报的是 err=5,也就是访问被拒的 `ERROR_ACCESS_DENIED`,连进程都不给建了。第三条路走的是静默出走,挂的是 `JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK`,这枚允许位放行的时候不声不响,创建方什么旗都不带,孙辈照样测出的是 0,走掉了。管理器想要的效果是“我管的孩子、孩子的孩子不管”,用的就是它,代价是树从此就盯不全了,文档自己也提醒了这一层。

嵌套本身是没有专用 API 的。想当然的姿势是把子 Job 的句柄当 hProcess 传给 `AssignProcessToJobObject`,e5 第 [5] 节的 (a) 组实测 ret=0、报的 err=6(`ERROR_INVALID_HANDLE`),那个参数认的只是进程句柄。正路写在文档的 Nested Jobs 页里:同一个进程,头一次 Assign 给的是根 Job,第二次 Assign 给的是子 Job,两次的顺序就是层级,咱们拿 `IsProcessInJob` 对两只 Job 分别验,验回来的都是 1。

层级立起来了之后,限额怎么算?[5] 的 (b) 组给父 Job 设了 48MB、子 Job 设了 256MB,孩子按 8MB 的步进往上申请,结果孩子在 40MB 处吃 err=1455 倒下,父 Job 的 48MB,管住了子 Job 的 256MB。离 48MB 还差的 8MB,正是进程自身的脚印,失败点也是随脚印浮动的,判依据的还是限额以内。提交类限额取的是全链最紧值,子 Job 能做的只是把限制再收紧,永远放宽不了父 Job 划下的界。您设计分层限额的时候,从外往里的原则是只减不加,这套语义才算用对了。

## argv 是谁拆的:命令行与两份解析器

收尾之前咱们还剩一件小事,牵出的问题其实不小:main 的 argc/argv 是从哪来的?Linux 那边,`execve` 的参数数组由调用方备好、内核原样转交,孩子拿到的 argv 就是父亲写的数组。而 Windows 内核根本不传数组,它递给孩子的是一根原始的命令行字符串——`GetCommandLineW` 的返回值,把它拆成 argc/argv 的活儿,是 CRT 启动时自己干的。

e7 给咱们验了两场。头一场是 argv[0] 的造假:app 填的是真身路径,命令行的头一个 token 写 `TOTALLY_NOT_ME.EXE`,孩子的 argv[0] 与 `CommandLineToArgvW` 拆出的第 0 项双双是假名,双方是一致的,孩子没有任何渠道从 argv 里还原出自己的真名。e1 里它还是个可爱的演示,放到安全的语境里,它就是参数伪造的惯用入口。

第二场是两份解析器的分歧。`CommandLineToArgvW` 是 shell32 里官方的拆分函数,与 CRT 各拆各的,咱们让它们同台:

```text
父进程发: "…exe" argv plain "two words" "quoted ""inner"" text" tail
- 空格参数 "two words":父进程的原意、CRT 与 CommandLineToArgvW 三方一致,一个参数
- 内嵌引号 "quoted ""inner"" text":CRT 拆成单个参数 quoted "inner" text
  CommandLineToArgvW 拆成 quoted "inner 与 text tail 两个 —— 判定 NO (content differs)
```

内嵌引号的边界上,MinGW 的 CRT 按 MSVC 的规则(`""` 产出的还是一个引号,而且维持着引号态)拆,而 `CommandLineToArgvW` 按自家的规则拆,两边给出的答案不同。main 的 argv 就是 CommandLineToArgvW 的结果,这句话在这套工具链上就不总成立了。您写需要精确引号语义的代码,比如 shell、参数转发、测试框架的命令行回放,要么指明用的是哪一份,要么自己实现一份规则,两头默认各拆各的,分歧就埋下了。

## 两侧对照:五行收束

本篇走完了,咱们把两侧的进程机制对齐着收一遍。每行的 Windows 侧,除死讯送达行里点到即止的那半句 IOCP 外,证据都在本篇的 e 系实验里,而 Linux 侧的实测出自同批的 Lproc 系实验,两边对得上号的才进了表:

| 对照轴 | Linux 侧 | Windows 侧 |
| --- | --- | --- |
| 造进程 | fork 复制加 exec 换心,子进程从同一点返回两次 | CreateProcessW 一步,父进程拿 hProcess/hThread 双句柄,孩子从 main 起跑 |
| 收尸 | waitpid 族,wstatus 还要 WIFEXITED/WIFSIGNALED 分拣 | WaitForSingleObject/WFMO 等句柄,索引直接点名,GetExitCodeProcess 拿码(259=未退) |
| 死讯送达 | SIGCHLD 内核异步推 | 没有推送,句柄本身就是可等待对象,异步化要 IOCP 挂 Job |
| 孤儿治理 | 无内建父死子亡,孤儿过继 subreaper,治理靠手动 wait | Job 加 KILL_ON_JOB_CLOSE,最后一只句柄一关全体退场,Linux 无对应物 |
| 优雅退出 | SIGTERM 默认可捕获,handler 收尾,SIGKILL 不可拦 | TerminateProcess 不可拦(对位 SIGKILL),SIGTERM 无对应物,最近的是 CTRL_BREAK 或自备 IPC |

abort 的形态咱们把它从表外单独拎出来看:同一个 `abort()`,那边 waitpid 读到的是 `WIFSIGNALED` 加 `WTERMSIG=6`,这边折叠成的是退出码 3221226505。Windows 把异常死也编码进了 32 位退出码空间,代价是父进程丢掉了判断是不是信号死的维度,而 Linux 保留了这个维度,代价是 wstatus 要过一遍分拣宏的工序。

进程的生与死、圈与逃,到本篇就齐了。可 CTRL_BREAK 在咱们手里始终只是个工具:控制台事件怎么递、handler 装在哪些线程上跑、APC(它的中文名是异步过程调用,说的就是把一个函数塞进目标线程执行队列的机制)怎么把沉睡的线程叫醒,这些咱们都排在下一篇[控制台事件与 APC](02-console-apc.md)里,在那边 CTRL_BREAK 从工具升成了主角。咱们下一篇见。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="CreateProcessW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw"
  />
  <ReferenceItem
    :id="2"
    title="Job Objects"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects"
  />
  <ReferenceItem
    :id="3"
    title="Nested Jobs"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/procthread/nested-jobs"
  />
  <ReferenceItem
    :id="4"
    title="ExitProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess"
  />
  <ReferenceItem
    :id="5"
    title="TerminateProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess"
  />
  <ReferenceItem
    :id="6"
    title="GetExitCodeProcess function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess"
  />
  <ReferenceItem
    :id="7"
    title="GenerateConsoleCtrlEvent function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent"
  />
  <ReferenceItem
    :id="8"
    title="CommandLineToArgvW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-commandlinetoargvw"
  />
  <ReferenceItem
    :id="9"
    title="Chromium sandbox: Design of the sandbox"
    publisher="Chromium Project"
    url="https://chromium.googlesource.com/chromium/src/+/main/docs/design/sandbox.md"
  />
</ReferenceCard>
