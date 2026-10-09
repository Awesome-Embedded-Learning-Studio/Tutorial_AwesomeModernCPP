# 02-console-apc 配套实验

《控制台事件与 APC》(`documents/vol8-domains/systems-programming/windows/process/`,文章待写)的实验代码与原始输出存档。`.out` 是 2026-10-04 当轮机器的原始捕获,`$` 开头的行是当时敲的命令。与前篇分工:`file-io/03-seh-veh/06-console-event`(E6)已证 Ctrl+C 不走 SEH、handler 在新线程跑,本篇展开控制台事件全机制与 APC,不重复 VEH 对照。

## 环境

| 项 | 值 |
|---|---|
| 系统 | Windows 11 26200(WSL2 6.18.33.2-microsoft 内 interop 启动) |
| 编译器 | MSYS2 UCRT64 g++ (Rev 5) 16.1.0,x86_64-w64-mingw32 |
| 编译 | `g++ -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe`(全部 0 warning) |
| 运行 | WSL bash 直跑 `.exe`(interop 桥接),stdio 是管道,进程挂在一个无窗口的控制台上 |

## interop 边界实录(本篇能测什么、测不了什么)

**headline:WSL interop 启动链给进程设了「忽略 Ctrl+C」属性,CTRL_C_EVENT 在不改代码的情况下静默不投递,CTRL_BREAK_EVENT 永远照投。** 三种启动方式实测一致:

| 启动方式 | 控制台 | GenerateConsoleCtrlEvent(CTRL_C) | CTRL_BREAK |
|---|---|---|---|
| WSL 直跑 | 无窗口,stdin/stdout=PIPE,CP 936 | 返回 1,handler 0 次 | 投递,handler 1 次 |
| `cmd.exe /c` 桥接 | 同上 | 同上,0 次 | 投递 |
| `start /wait` 真 conhost 窗口 | 有窗口,stdin=CHAR | **同样 0 次** | 投递 |

机制判定:MSDN SetConsoleCtrlHandler Remarks —— "Calling SetConsoleCtrlHandler with the NULL and TRUE arguments causes the calling process to ignore CTRL+C signals. **This attribute is inherited by child processes**"。CREATE_NEW_PROCESS_GROUP 也设同一属性(CreateProcess Remarks:"CTRL+C signals will be disabled for all processes within the new process group")。本机 A/B 证据:`SetConsoleCtrlHandler(NULL, FALSE)` 一句复位后 CTRL_C 立刻正常投递(E1 步骤 [4] vs [5],p0 三种模式全灭 vs E3 父子全亮)。没有查询该属性的 API,「WSL interop 链设的」是从行为反推的归因,证据是继承链上唯一自洽的解释。

这修正了前篇 `03-seh-veh/README.md` E6 行的归因:「WSL interop 桥接控制台会吞 CTRL_C,要真控制台窗口」——真窗口同样吞,吞因不是 ConPTY 桥接,是继承来的忽略位。该 README 待维护者顺手更新。

测不了/不测的:

- **键盘 Ctrl+C**:本环境没有真人在控制台前敲键。用 `WriteConsoleInput` 注入按键记录代替,实测注入的 Ctrl+C **不会**被转成信号(handler 0 次,记录以原始键事件留在缓冲里),两种控制台(桥接/真窗口)行为一致。即「注入绕过了信号转换」是本机口径,键盘路径未测。
- **CTRL_CLOSE/LOGOFF/SHUTDOWN**:触发条件是关控制台窗口/注销/关机,未测。触发场景与约束见 01 子目录 README 的文档摘录。
- **SendInput 模拟真键**:会把 Ctrl+C 打进当时的前台窗口,有干扰用户会话的风险,主动不做。
- WSL `$?` 读不出全 32 位退出码(低 8 位截断),全码读法用父进程 `GetExitCodeProcess` 或 `cmd /v:on /c "... & echo !ERRORLEVEL!"`(见 02 子目录)。

## 目录与实验对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `00-env-probe/` | P0 | 三种启动方式的控制台边界普查(上表原始输出) |
| `01-ctrl-handlers/` | E1 | 链序后注册先调(实测 C→B→A→Z),FALSE 传递、TRUE 截断,`NULL+TRUE/FALSE` 是继承的忽略开关 |
| `02-delivery-default-exit/` | E2 | 每次事件一条**新线程**,handler 返回线程即逝(OpenThread 探针 gle=87),无 handler 走默认 `ExitProcess(0xC000013A)` |
| `03-process-group/` | E3 | CREATE_NEW_PROCESS_GROUP 子进程对广播 Ctrl+C 免疫(忽略位实现,可自解),CTRL_BREAK 可定向发组,CTRL_C 定向发组**实测能收到,与文档 remark 矛盾** |
| `04-apc-alertable/` | E4 | APC 排队≠执行:不可警告等待期 0 次,进 alertable 立即 FIFO 两条全跑,复用原线程 |
| `05-apc-interrupt/` | E5 | QueueUserAPC 掐断可警告等待,返回 `WAIT_IO_COMPLETION`(192),不可警告等待掐不动,线程退出时积压 APC 作废 |
| `06-setevent-mirror/` | E6 | handler 里只 `SetEvent`,主线程 WaitForSingleObject 约 79ms 被唤醒(self-pipe 的 Windows 同构) |
| `07-console-mode/` | E7(速览) | 关 `ENABLE_PROCESSED_INPUT` 后 Ctrl+C 降级为输入字节 `0x03`(ReadFile 实读),控制台输入句柄本身可 WaitFor |

## E6 对照表素材(信号 vs 控制台事件,四行,Linux 侧已入册)

| 维度 | Linux(实验在档) | Windows(本篇实验) |
|---|---|---|
| 投递模型 | 任意未屏蔽线程,handler 在指令边界插入,阻塞期连发只记 1 次(`linux/process/04-signal-basic/01`) | 每次事件**新建一条线程**调 handler,返回后线程即逝(E2,tid 实证) |
| handler 约束 | async-signal-safe 白名单,handler 里 printf 实测交错损坏(`03-async-safety`) | 新线程里跑,约束比信号宽,但 CLOSE/LOGOFF/SHUTDOWN 期间控制台函数不可靠(MSDN 原话),且限时,正解只 SetEvent(E6) |
| 唤醒模式 | self-pipe:handler 里 write(2),主循环 poll 读(`05-handler-patterns/self_pipe.cpp`) | handler 里 SetEvent,主线程 WaitForSingleObject 约 79ms 醒(E6) |
| 事件转句柄统一等 | signalfd/pidfd 把信号变 fd(并行篇在写) | 控制台输入句柄可直接 WaitFor(E7 实测 Wait=0 有货),WaitFor* 家族统一等一切可等待对象(E5) |

APC 与控制台事件对照的哲学差(文章主线素材):控制台事件 = **另起一条线程**替你跑(E2),APC = **借目标线程自己**的躯壳跑,代价是必须等到它进可警告等待(E4/E5)。MSDN 有一处交叉印证:被调试进程的 CTRL_C 若被调试器吃掉,"an application will not notice the CTRL+C, with one exception: **alertable waits will terminate**"——可警告等待与控制台事件在系统层就勾连着。

## 文档口径备查(2026-10-04 取自 Microsoft Learn)

- SetConsoleCtrlHandler Remarks:链序原话 "last-registered, first-called basis until one of the handlers returns TRUE. If none of the handlers returns TRUE, the default handler is called",默认 handler 即 ExitProcess。AttachConsole/AllocConsole/FreeConsole 会重置 handler 表。gdi32/user32 加载后 LOGOFF/SHUTDOWN 不再送达(Win7+ 口径,未测)。
- GenerateConsoleCtrlEvent Remarks:"CTRL_C_EVENT … This signal cannot be generated for process groups. If dwProcessGroupId is nonzero, this function will succeed, but the CTRL+C signal will not be received by processes within the specified process group"——**E3 步骤 [4] 实测与本条矛盾**(免疫解除后定向 CTRL_C 收到了,复跑一致),文章引用时标注。
- CreateProcess CREATE_NEW_PROCESS_GROUP:"CTRL+C signals will be disabled for all processes within the new process group",等价隐式 `SetConsoleCtrlHandler(NULL, TRUE)`,子进程可 NULL+FALSE 解除(E3 步骤 [3]/[6] 实证)。
- 超时:CTRL_CLOSE 等 5 秒不返回即终止(社区与 terminal 仓口径,未实测)。DBG_CONTROL_C:被调试时 Ctrl+C 先给调试器,仅当调试器放行才到进程(SetConsoleCtrlHandler Remarks 原话,未实测)。

## 工程备忘(实验过程遇到的,读者复现会用到)

- `OpenEventA(EVENT_MODIFY_STATE, …)` 打开的句柄**不能等**,WaitForSingleObject 立即 WAIT_FAILED,要 `EVENT_MODIFY_STATE | SYNCHRONIZE`。
- 多进程共享管道 stdout,printf 会交错(e3 第一轮 184MB 事故的另一半原因),命名互斥体包一层解决(见 `e3_group.cpp` 的 PF 宏)。
- Windows g++ 输出重编译后要重新 `chmod +x`(WSL 侧执行位不保留)。
- 编译命令带管道截断(`| head`)会 SIGPIPE 杀掉 g++.exe,产出半截 exe。

## 复现

```sh
# 全部实验(在 02-console-apc 对应子目录里)
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe && chmod +x xxx.exe && ./xxx.exe

# e2 的 cmd 退出码交叉验证(victim_selfsend)
/mnt/c/Windows/System32/cmd.exe /v:on /c "C:\\msys64\\tmp\\victim_selfsend.exe < nul & echo !ERRORLEVEL!"

# 真 conhost 窗口口径(把 exe 拷到 C:\msys64\tmp 后)
/mnt/c/Windows/System32/cmd.exe /c start /wait "" C:\\msys64\\tmp\\run_p0.bat
```

注意 e3 首轮若拿旧版跑会无限刷屏(状态机把退出事件当 reenable 重入),存档版已修。
