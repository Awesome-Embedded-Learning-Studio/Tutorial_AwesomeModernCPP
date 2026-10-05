# 01-createprocess:《进程与作业:CreateProcessW 与 Job 对象》配套实验

拟新增文章(Windows 侧 process 站)的真机实验与原始输出存档。`.out` 全部是当轮机器的原始捕获(`> xxx.out 2>&1`,子进程输出经句柄继承/管道汇入同份转录)。七个实验:CreateProcessW 全解剖、退出码与等待、TerminateProcess 与优雅退出的差距、Job 对象(重头)、嵌套与 breakaway、五行对照表素材、命令行与 argv。

与前章的分工:W01/memory 章的 `spawn_self` 只把 CreateProcessW 当工具用;本批把它放上解剖台。镜像对照面是 Linux 侧的 fork/exec/waitpid(Lproc 系,并行在写)。

## 环境(实测口径,捕获日 2026-10-04)

- Windows 11,`cmd.exe /c ver` 报 `10.0.26200.9457`
- 编译器:MSYS2 UCRT64 g++ 16.1.0(Rev5),`/mnt/c/msys64/ucrt64/bin/g++.exe`,`-std=c++20 -Wall -Wextra -municode`(全部零 warning 编译;E7 另加 `-lshell32`,E3 的 DLL 用 `-shared`)
- 宿主:WSL2 interop 编译运行,cwd 在源码目录(WSL 文件系统),参数全用相对路径,`chmod +x` 后 `./xxx.exe > xxx.out 2>&1`
- 观察者注记:WSL interop 拉起的 Windows 进程**本身就在一个 Job 里**(E5 驱动开场的 `IsProcessInJob(NULL)=1`),对本批所有 Job 实验无干扰(Win8+ 多 Job 成员合法),但解读"进程在不在 Job 里"时要记得这层背景

## 编译运行的确切命令

```sh
cd code/volumn_codes/vol8/systems-programming/windows/process/01-createprocess/<子目录>
GXX=/mnt/c/msys64/ucrt64/bin/g++.exe
$GXX -std=c++20 -Wall -Wextra -municode eX_xxx.cpp -o eX_xxx.exe        # E7 加 -lshell32
chmod +x eX_xxx.exe && ./eX_xxx.exe > eX_xxx.out 2>&1

# 03-terminate 是三件套,先建 DLL 再建子进程最后驱动:
$GXX -std=c++20 -shared e3_dll.cpp -o e3_dll.dll
$GXX -std=c++20 -Wall -Wextra -municode e3_child.cpp -o e3_child.exe
$GXX -std=c++20 -Wall -Wextra -municode e3_driver.cpp -o e3_driver.exe
chmod +x e3_child.exe e3_driver.exe && ./e3_driver.exe > e3_driver.out 2>&1
```

## 子目录与结论速览

| 目录 | 主题 | 一句话结论 |
|---|---|---|
| [01-anatomy/](01-anatomy/) | CreateProcessW 全解剖 | 句柄白名单继承(STARTUPINFOEX)只放行清单内句柄;lpApplicationName=NULL 才走"应用目录→CWD→系统目录→PATH"搜索,给了相对名就**只查 CWD**(err=2);**未加引号的带空格路径不是失败**:先试截断名+`.exe`(诱饵 `with.exe` 直接被选中,Program.exe 攻击复刻),没有诱饵就拼接后命中、且**子进程命令行被系统改写成带引号版**;CREATE_SUSPENDED 两段式先写 token 再 ResumeThread(prev count=1),hThread 关了进程照跑;自建 lpEnvironment 是**整块替换**:漏带 PATH 子进程 0xC0000135 起不来(DLL 加载也吃 PATH) |
| [02-exit-wait/](02-exit-wait/) | 退出码与等待 | 三种形态:return 42→42;abort()→**3221226505(0xC0000409,fail-fast)**,Linux 上这是 WIFSIGNALED+SIGABRT(6) 根本不是退出码;TerminateProcess(h,4660)→原样 4660;没退时查码给 259(STILL_ACTIVE);**CloseHandle(hProcess) 后进程活着**,OpenProcess 重开照样等到;WFMO 三视角:轮询得真实完成序、bWaitAll=FALSE 返回最先信叼的最小索引、TRUE 一把收完按句柄序对账 |
| [03-terminate/](03-terminate/) | Terminate vs 优雅退出 | 四种死法清理矩阵:return→atexit✓/DLL detach✓/stdout flush✓;直调 ExitProcess→atexit **✗**、detach✓、flush **在这套动态 UCRT 工具链上仍然 ✓**(TerminateProcess 对照证明 flush 依赖 detach 路径);TerminateProcess→三样全 ✗、退出码原样透传(31337);CTRL_BREAK 事件=可捕获的"信号",handler 里 ExitProcess(5) 优雅退场 |
| [04-job/](04-job/) | Job 对象 | KILL_ON_JOB_CLOSE:显式 CloseHandle(job)→孩子 **0ms 内死**(GetTickCount64 分辨率内,退出码 0,心跳冻在 6 行);mid 进程退出句柄自动关→孙辈同陪葬;对照组没旗→1.5s 后仍活(孤儿);**反例:Job 句柄被孩子继承→陪葬失灵**;账本可查(ActiveProcesses/TotalPageFaults/id 清单);40MB 限额:VirtualAlloc 在 36MB 处 err=1455(ERROR_COMMITMENT_LIMIT)、new 抛 bad_alloc,对照组 256MB 全过 |
| [05-nested-breakaway/](05-nested-breakaway/) | 嵌套与出走 | 世袭:Job 里生的孩子自动进同一 Job;breakaway=允许位(BREAKAWAY_OK)+CREATE_BREAKAWAY_FROM_JOB 双全→出走成功;**强闯(job 无允许位)→CreateProcessW 直接失败 err=5**;静默位(SILENT_BREAKAWAY_OK)不带旗也走;嵌套**没有专用 API**(hProcess 传 Job 句柄→err 6 被拒),正路是同一进程**先 Assign 根 Job 再 Assign 子 Job**,层级自动成;父 48MB 压死子 256MB(40MB 处失败)——子 Job 只能再收紧,不能放宽 |
| [06-argv/](06-argv/) | 命令行与 argv | main(argc,argv) 是 CRT 拿 GetCommandLineW 现拆的;argv[0] 由父进程随手写(app 指真身、cmdline 首 token 写 TOTALLY_NOT_ME.EXE,子进程 argv[0] 照单全收);**内嵌引号场景 MinGW CRT 的 argv 与 CommandLineToArgvW 拆法不同**:`"quoted ""inner"" text"` CRT 拆成一个参数、CommandLineToArgvW 拆成两个 |

## E6 对照表素材(五行,两侧都有本批实验背书)

| 对照轴 | Linux 侧(Lproc 并行在写,文字对照) | Windows 侧 | Windows 证据行 |
|---|---|---|---|
| 造进程 | fork() 复制 + exec() 换心,两步;子进程从同一点返回两次 | CreateProcessW 一步:全新进程,回来给 hProcess/hThread 双句柄,各管各的 | 01-anatomy [2](双句柄分工、挂起两段式) |
| 收尸 | waitpid 族:wstatus 还要 WIFEXITED/WIFSIGNALED 分拣 | WaitForSingleObject/WFMO:返回值/索引直接点名,GetExitCodeProcess 拿码(259=未退) | 02-exit-wait [1][3] |
| 死讯送达 | SIGCHLD 内核异步推 | 没有推送;句柄本身就是"可等待对象",异步化要 IOCP 挂 Job(本批未展开 IOCP) | 02-exit-wait [2] + 04-job [4] |
| 孤儿治理 | prctl(PR_SET_CHILD_SUBREAPER) 收养 + 手动 wait;无内建"父死子亡" | Job + KILL_ON_JOB_CLOSE:最后一只 Job 句柄一关全体陪葬,Linux 无直接对应物 | 04-job [1][2](0ms 实测)+ [3](继承坑) |
| 优雅退出 | SIGTERM 默认可捕获,handler 收尾;SIGKILL 不可拦 | TerminateProcess 不可拦(≈SIGKILL);**没有 SIGTERM 对应物**,最接近的是 CTRL_BREAK 控制台事件(可捕获)或自备 IPC | 03-terminate 全部四案 + E2 abort 码 |

abort 形态对照(塞进"退出码"行):Linux waitpid 见 WIFSIGNALED+WTERMSIG=6;Windows 全折叠成一个退出码 3221226505(0xC0000409)——Windows 把"异常死"也编码进 32 位退出码空间,父进程不再有"是不是信号死"这个维度。

## 复跑注意

- 本批全部实验自清理:临时目录在 `%TEMP%\vol8_e1|e3|e4\`,起手 DeleteFileW/CreateDirectoryW,反复跑不留脏;命名对象(Local\vol8e3-*/vol8e5-*)带 pid,进程退即消失
- E2/E3 家长进程先 SetErrorMode(SEM_NOGPFAULTERRORBOX)(子进程继承),abort 的 fail-fast 才不会弹 WER 挂住无人值守跑批;时序数字(0ms、532ms 这类)每轮浮动,量级不变
- E4 的陪葬延迟给的是 GetTickCount64 上界(0ms = 计时分辨率内);心跳行数随机器负载浮动
- E5 的内存失败点(40MB)随进程自身脚印浮动几 MB;判定看"≤父限额"而非精确值
- 归档只收 .cpp/.out/README,二进制(.exe/.dll)按仓库惯例不入册,复跑用上面的命令重建
