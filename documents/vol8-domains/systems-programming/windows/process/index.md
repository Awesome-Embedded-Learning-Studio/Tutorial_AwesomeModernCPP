---
title: "Windows 进程"
sidebar_order: 30
description: "Win32 进程与作业:CreateProcessW 解剖、退出码与四种死法、Job 对象的陪葬与限额、嵌套与出走;控制台事件的忽略位与 APC 的排队执行"
platform: host
tags:
  - cpp-modern
  - host
  - intermediate
  - 系统编程
  - Win32
---

# Windows 进程

内存章里 spawn_self 那行 CreateProcessW 已经陪咱们跑熟了,这一章把它放上解剖台:进程怎么创建、句柄怎么继承、四种死法各自的清理矩阵,最后收在 Job 对象怎么把一群进程圈成一个单元治理。第二篇转头去答 Windows 拿什么回应信号这道题:控制台事件的注册链与忽略位,以及 APC 的排队与执行。公共工具上,`unique_handle` 与 `last_error_code` 沿用思维基石两篇的定义,`check_win32` 是 W01 定义的;第二篇的实验自包含,这几个工具没请出场。

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-createprocess" desc="CreateProcessW 十个参数的实测解剖:lpApplicationName=NULL 才走完整搜索链而相对名只对 CWD 解析(err=2),未引号带空格路径埋诱饵 with.exe 即中(Program.exe 攻击复刻),无诱饵时系统拼接后改写子进程命令行,argv[0] 与真实加载路径解耦,STARTUPINFOEX 句柄白名单,CREATE_SUSPENDED 两段式,lpEnvironment 整块替换漏带 PATH 起不来(0xC0000135)。等待与退出码:CloseHandle 后进程仍活(句柄只是观察权),return 42 与 abort 的 0xC0000409 fail-fast 与 TerminateProcess 原样透传,STILL_ACTIVE=259 陷阱,WFMO 三视角。四种死法清理矩阵(return 三样全走、直调 ExitProcess 的 atexit 不走但动态 UCRT 仍刷缓冲、TerminateProcess 三样全跳、CTRL_BREAK 优雅退场)。Job 对象:KILL_ON_JOB_CLOSE 两条路都是计时分辨率内处决而对照组孤儿照活,Job 句柄被孩子继承则陪葬失灵,QueryInformationJobObject 统计,40MB 限额 err=1455,嵌套与出走(世袭、双全出走、强闯 err=5、静默出走、父 Job 48MB 压死子 Job 256MB),argv 的 CRT 现拆与引号拆法分歧,五行对照表收束">进程与作业:CreateProcessW 与 Job 对象</ChapterLink>
  <ChapterLink num="2" href="02-console-apc" desc="Windows 没有 SIGINT,Ctrl+C 靠控制台事件变成函数调用:SetConsoleCtrlHandler 链序晚注册排头(实测 C→B→A→Z、TRUE 截断、注销即除名),NULL+TRUE 是只免 CTRL_C 不免 BREAK 的忽略位且被子进程继承;投递模型是每次事件新建一条线程(两次事件两个 tid、OpenThread 探针 gle=87 证明 handler 返回线程即逝),无 handler 默认死在 ExitProcess(0xC000013A),GetExitCodeProcess 与 cmd 的 ERRORLEVEL 双口径;进程组六幕:CREATE_NEW_PROCESS_GROUP 的免疫实测就是隐式 NULL+TRUE 同一忽略位、子进程一句 NULL+FALSE 自解,定向 CTRL_C 按文档收不到、按实测收到了;WSL interop 下 CTRL_C 不投递的根因是启动链继承了忽略属性、一句复位并更正 W03 的真窗口归因;重头 APC:609ms 不可警告等待里排队两条零执行,进 SleepEx(TRUE) 同一 tick 内 FIFO 连跑并提早返回 192,复用原线程对照控制台事件另起新线程,QueueUserAPC 掐醒卡死的可警告等待 407ms 拿 WAIT_IO_COMPLETION、不可警告对照掐不动且线程退出积压作废;handler 里只 SetEvent 的 self-pipe 镜像与四行对照表,ENABLE_PROCESSED_INPUT 关掉后 Ctrl+C 降级成字节 0x03">控制台事件与 APC</ChapterLink>
</ChapterNav>
