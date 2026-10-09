# 07-console-mode —— E7(速览)ENABLE_PROCESSED_INPUT:Ctrl+C 的信号/字节开关

详细留 ch06 终端篇,这里一句话实测:关掉 `ENABLE_PROCESSED_INPUT` 后,Ctrl+C 不再是信号,降级为输入字节 `0x03`。

方法:没有真键盘,用 `WriteConsoleInputW` 往 CONIN$ 注入一对 Ctrl+C 按键记录(按下+抬起)。两种控制台(桥接/真 conhost 窗口)行为一致,各一份输出:

- 场景 B(PROCESSED_INPUT 关,顺手关 LINE/ECHO):注入后 `WaitForSingleObject(CONIN$, 1500)` 返回 0(**控制台输入句柄本身可等**,WaitFor* 家族一员,顺手给 E6 对照表第 4 行供货),`ReadFile` 实读 **1 字节 `03`**——文档口径「CTRL+C is reported as keyboard input rather than as a signal」落到了字节上。handler 调用数 0。
- 场景 A(PROCESSED_INPUT 开,默认):注入的 2 条记录**原样留在输入缓冲里**,handler 0 次。也就是说本机上 WriteConsoleInput 注入的按键不经过「键→信号」转换,该转换只发生在真键盘摄入路径上(未测,见顶层 README 边界)。信号那一半的证据由 E1/E2 的 GenerateConsoleCtrlEvent 路径承担。

初始 mode=0x1F7(ENABLE_PROCESSED_INPUT|LINE_INPUT|ECHO_INPUT|WINDOW_INPUT|MOUSE_INPUT|QUICK_EDIT|EXTENDED),与 Win11 默认一致。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e7_console_mode.cpp -o e7_console_mode.exe
chmod +x e7_console_mode.exe && ./e7_console_mode.exe
```
