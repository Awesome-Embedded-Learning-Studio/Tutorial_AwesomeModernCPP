# 01-ctrl-handlers —— E1 SetConsoleCtrlHandler 全家福

一个进程一次跑完六幕(输出见 `e1_handlers.out`):

1. **链序**:注册 Z→A→B→C(全 FALSE,Z 最先注册当兜底返回 TRUE)。发 CTRL_BREAK,调用链实测 `C → B → A → Z`,后注册先调。
2. **TRUE 拦截**:C 改返回 TRUE 再发,链在 C 截断,B/A/Z 一个都不跑。「已处理」语义 = 系统不再问后面的 handler。
3. **注销**:`SetConsoleCtrlHandler(C, FALSE)` 移除,链变 `B → A → Z`。
4. **interop 揭秘**:发 CTRL_C,调用链**空**——本进程的「忽略 Ctrl+C」属性被启动链设上(见 00-env-probe)。
5. **复位**:`SetConsoleCtrlHandler(NULL, FALSE)` 一句,CTRL_C 立刻正常走 `B → A → Z`。与 [4] 构成 A/B 证据。
6. **忽略开关**:`NULL+TRUE` 显式忽略,CTRL_C 空链,紧接着 CTRL_BREAK 照走 `B → A → Z`——忽略位只管 C,不管 BREAK。

五种事件实测口径:CTRL_C(0)/CTRL_BREAK(1)可由 GenerateConsoleCtrlEvent 生成实测。后三种只记录触发场景,未实测:

| 事件 | 触发场景(MSDN) | 备注 |
|---|---|---|
| CTRL_CLOSE_EVENT(2) | 用户关控制台窗口 | handler 约 5 秒不返回即被终止(社区口径),进程终归要退 |
| CTRL_LOGOFF_EVENT(5) | 用户注销 | 仅服务进程收到(microsoft/terminal 文档口径) |
| CTRL_SHUTDOWN_EVENT(6) | 系统关机 | 同上,且加载了 gdi32/user32 的进程不送达 |

handler 里能安全做什么(主素材,与 Linux 信号 handler 约束镜像):handler 在**新线程**里跑,不在任意中断点上抢跑,所以比信号 handler 宽——但仍受两条约束:一是 CLOSE/LOGOFF/SHUTDOWN 期间 "Console functions, or any C run-time functions that call console functions, may not work reliably"(MSDN 原话,内部清理可能已先行),二是限时收尾。所以工程正解是 handler 只 SetEvent,IO 与打印回主线程,可执行版在 `06-setevent-mirror/`。AttachConsole/AllocConsole/FreeConsole 会把 handler 表重置回默认,重新挂控制台后要重注册。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_handlers.cpp -o e1_handlers.exe
chmod +x e1_handlers.exe && ./e1_handlers.exe
```
