# 03-terminate:E3 TerminateProcess 与优雅退出的差距

文件:`e3_dll.cpp`(DLL,DllMain 只用裸 WriteFile 记 attach/detach)、`e3_child.cpp`(四种死法)、`e3_driver.cpp`(驱动,转录者)、`e3_driver.out`。

## 实验布置

每个 case 的孩子做同样三件事:注册写 stderr 的 atexit 善后、LoadLibrary 带 DllMain 日志的 DLL、往 stdout 留一条**不带换行不 fflush** 的标记(stdout 被重定向到文件=全缓冲,标记只在真 flush 时落盘)。stderr 走管道,孩子死后驱动整段取回(转录不串行)。ready 握手用命名事件。四种死法:return / 直调 ExitProcess / 被父 TerminateProcess(31337) / 收到 CTRL_BREAK 事件。

## 清理矩阵(全部来自 e3_driver.out 转录)

| case | atexit | DLL_PROCESS_DETACH | stdout 标记落盘 | 退出码 |
|---|---|---|---|---|
| return 0(CRT exit 路径) | **✓** ran | ✓ | ✓(37 字节) | 0 |
| 直调 ExitProcess(0) | **✗ 没跑** | ✓ | **✓ 落盘了** | 0 |
| TerminateProcess(31337) | ✗ | **✗ 没有** | **✗ 缓冲陪葬**(0 字节) | 31337 原样 |
| CTRL_BREAK→handler→ExitProcess(5) | (未注册) | ✓ | ✓ | 5(handler 自选) |

## 三个要点

- **ExitProcess 直调跳过 CRT exit 路径**(atexit 没跑),但 loader 仍给所有 DLL 发 detach;这套动态 UCRT 工具链(g++ UCRT64,ucrtbase.dll)上 **stdout 缓冲仍被刷出**——ucrtbase 自己收到 detach 时做了流清理。TerminateProcess 案的 0 字节是反向证明:detach 一并跳过时 flush 就没了。"ExitProcess 丢缓冲"的口径要按工具链分:静态链接 CRT 或 MSVC 语义下仍会丢,文章引用时按"本机实测+机制解释"表述
- **TerminateProcess 不可拦**:进程没有任何收到通知的机会,detach/atexit/flush 三样全跳,退出码原样透传——Windows 上最接近 SIGKILL 的东西,且**没有 SIGTERM 对应物**
- **CTRL_BREAK 是可捕获的那一个**:`GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid)` 定向发给孩子(出生时带 CREATE_NEW_PROCESS_GROUP),handler 里可以从容 ExitProcess(5)——Windows 侧"优雅杀"的惯用替代品(Linux kill -TERM 的镜像素材)

## 复现

```sh
cd 03-terminate
GXX=/mnt/c/msys64/ucrt64/bin/g++.exe
$GXX -std=c++20 -Wall -Wextra -shared e3_dll.cpp -o e3_dll.dll
$GXX -std=c++20 -Wall -Wextra -municode e3_child.cpp -o e3_child.exe
$GXX -std=c++20 -Wall -Wextra -municode e3_driver.cpp -o e3_driver.exe
chmod +x e3_child.exe e3_driver.exe && ./e3_driver.exe > e3_driver.out 2>&1
```

注意:三个产物必须同目录(驱动按自身路径找 e3_child.exe/e3_dll.dll);输出文件在 `%TEMP%\vol8_e3\`;hang 模式的孩子有 60s 自了断上限,不会留孤儿。
