# 00-env-probe —— interop 控制台边界普查(P0)

开工前先弄清:本机(WSL interop 启动链)跑 Windows .exe,进程挂在一个什么控制台上,GenerateConsoleCtrlEvent 两种事件各到不到 handler。三个 `.out` 对应三种启动方式:

| 文件 | 启动方式 |
|---|---|
| `p0_direct.out` | WSL bash 直跑(默认口径,后续 E1-E7 都是这个) |
| `p0_cmd_bridge.out` | `cmd.exe /c` 桥接 |
| `p0_real_window.out` | `start /wait` 开真 conhost 窗口(`run_real_window.bat`,窗口在桌面一闪而过) |

## 结论

1. interop 进程**有控制台但无窗口**:GetConsoleWindow=NULL,stdin/stdout 是 PIPE,CP=936,CONIN$ 可开,mode=0x1F7(PROCESSED_INPUT 本来就开)。
2. `GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0)` 三种方式都**返回 1 但 handler 0 次**——包括真窗口。强设 PROCESSED_INPUT 再发,照样 0 次。归因:启动链设了继承的「忽略 Ctrl+C」属性(SetConsoleCtrlHandler(NULL,TRUE) 等效态),复位见 01 子目录 E1 步骤 [5] 的 A/B。
3. `CTRL_BREAK_EVENT` 三种方式都正常投递,handler 在新线程跑(tid 与主线程不同)。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra p0_probe.cpp -o p0_probe.exe
chmod +x p0_probe.exe && ./p0_probe.exe
# 另两种:拷到 C:\msys64\tmp 后分别用 cmd.exe /c 与 start /wait(见顶层 README)
```
