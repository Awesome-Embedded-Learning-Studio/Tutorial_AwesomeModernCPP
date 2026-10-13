# 06-setevent-mirror —— E6 handler 里只 SetEvent(self-pipe 的 Windows 同构)

handler 全部工作一句 `SetEvent`,不 printf 不碰控制台(MSDN 口径:CLOSE/LOGOFF/SHUTDOWN 期间控制台函数与调它们的 CRT 函数不可靠,handler 应尽快返回)。主线程 `WaitForSingleObject(wake, 5000)` 上等,自发自收两轮(`e6_setevent_wake.out`):

- CTRL_C 轮:79ms 后醒,ret=0
- CTRL_BREAK 轮:78ms 后醒,ret=0

与 Linux 侧 `linux/process/04-signal-basic/05-handler-patterns/self_pipe.cpp` 一一对应:handler 里最小动作(write→SetEvent),决策与 IO 全回主循环(poll→WaitForSingleObject)。四行对照表见顶层 README 的「E6 对照表素材」。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e6_setevent_wake.cpp -o e6_setevent_wake.exe
chmod +x e6_setevent_wake.exe && ./e6_setevent_wake.exe
```
