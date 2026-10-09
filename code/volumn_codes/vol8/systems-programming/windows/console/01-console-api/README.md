# 01-console-api 配套实验

《Windows 控制台:字符网格、输入事件与 VT 序列》(`documents/vol8-domains/systems-programming/windows/console/01-console-api.md`)的实验代码与原始输出存档。`.out` 是 2026-10-05 当轮机器的原始捕获;e3 的 `.out` 同日重录过一次(补 [5] 段读数,见下)。

与前篇(`windows/process/02-console-apc`)的分工:那篇管**控制台事件面**——SetConsoleCtrlHandler 的链序、CTRL_C 不投递的继承忽略位、APC;其中 E7 速览过"关 ENABLE_PROCESSED_INPUT 后 Ctrl+C 降级为字节 0x03"。本篇管**输入输出面**——字符缓冲区、输入事件记录、VT 序列、模式位全图,不重复信号路与 Ctrl+C 那一幕,引用处已在代码注释标明。

## 环境

| 项 | 值 |
|---|---|
| 系统 | Windows 11 26200(WSL2 6.18.33.2-microsoft 内 interop 启动) |
| 编译器 | MSYS2 UCRT64 g++ (Rev 5) 16.1.0,x86_64-w64-mingw32 |
| 编译 | `g++ -std=c++20 -Wall -Wextra xxx.cpp -o xxx.exe`(0 warning;重编译后要重新 `chmod +x`) |
| 运行 | WSL bash 直跑 `.exe`,stdio 是管道,进程挂在**无窗口**控制台上 |
| 控制台底细 | 缓冲区 120x9001(带滚动历史),可见窗 120x30,默认属性 0x7,CONIN mode=0x1f7,CONOUT mode=0x3,CP 936 |

**方法论(本目录所有实验的地基)**:stdio 是管道,对它们调 GetConsoleMode 直接失败(GetLastError=6);但 `CreateFile("CONIN$"/"CONOUT$")` 能拿到这个无窗口控制台的真句柄——于是**人不用坐在屏幕前,输出一律用 ReadConsoleOutput 把缓冲区读回来验**,属性字节和字符一起读,断言写在程序里。

## 目录与实验对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `e1_mode_bits.cpp` | e1 | 输入 9 个文档位+输出 5 个位逐位翻译;出厂 CONIN=0x1f7(含 QUICK_EDIT/EXTENDED/AUTO_POSITION)、CONOUT=0x3(VT 位默认关);SetConsoleMode 改了读得回;CP 双 936 |
| `e2_buffer.cpp` | e2 | 缓冲区(120x9001)与可见窗(120x30)是两回事;6x10 彩色网格 WriteConsoleOutput 整块直写,读回 60/60 格字符+属性全对;直写**不碰光标**,WriteConsoleA 才跟光标走;越出右缘的矩形被裁(10 列宽只写进 5 列);ScrollConsoleScreenBuffer 上滚后原第 2 行顶到第 0 行 |
| `e3_input_events.cpp` | e3 | 键盘一按一松是两条 KEY_EVENT_RECORD(六字段全录:KeyDown/Repeat/KeyCode/ScanCode/字符/修饰键状态);Peek 窥不取走、Read 逐条取走、Flush 全清;鼠标/焦点也是记录;注入的 Ctrl+C 只是一条带 LEFT_CTRL_PRESSED 的键记录(信号路归前篇);[5] 把探索期两句手测升级成读数:`SetConsoleScreenBufferSize` 原样重设 120x9001 也报 87,`SetConsoleWindowInfo` 同尺寸/异尺寸都返回成功且 srWindow 读回真的缩到 120x29,队列计数仍 0 条——改窗成功也不投事件 |
| `e4_vt.cpp` | e4 | VT 关:`\x1b[31mRED\x1b[0m` 在缓冲区里留下九个字面字符(读回 `ESC[31mRED`),属性全 0x7;VT 开:同一串 12 字节只剩 "RED" 三格、属性 0x4;SGR `1;31` 读回 0xc;`\x1b[5;10H` 后光标=(9,4)(1 起算对 0 起算);`\x1b[2J` 只清屏**不动光标**(光标留在 (30,3),与 ANSI ED 定义一致——老 conhost 曾顺手归零,本机 26200 不再如此) |
| `e5_mixing.cpp` | e5 | 经典与 VT 写同一块缓冲但约束各自带:VT 定位+推进光标,WriteConsoleOutput 自带坐标不碰光标,互不干扰;WRAP_AT_EOL 关掉后 115 列起写 8 字符,E..H 反复覆盖 (119,6) 只剩 H;DISABLE_NEWLINE_AUTO_RETURN 不设时行尾 \n 连回车一起做(C 到下一行行首),设了之后只下移一行(C 停在 (119,10)) |

## interop 边界实录(本机能测什么、测不了什么)

- **WINDOW_BUFFER_SIZE_EVENT 触发不了系统侧路径**:`SetConsoleScreenBufferSize` 在这个无窗口控制台上全部被拒(ERROR_INVALID_PARAMETER=87,e3 [5] 的读数:异尺寸 120x3000 与原样重设 120x9001 都报 87);`SetConsoleWindowInfo` 能成——srWindow 读回真的从 120x30 缩到 120x29——但**不投事件**(开了 ENABLE_WINDOW_INPUT,队列计数 0 条,e3 [5] 读数)。事件记录的形态用 WriteConsoleInput 注入演示(e3 [4]),真窗口/真缓冲区改尺寸的路径未测。作为补偿,e2 用 ReadConsoleOutput 验证了缓冲区结构本身。
- 注入即搬运:本机没有真人敲键,键盘/鼠标/焦点记录一律 WriteConsoleInput 注入再读回。注入的记录会不会走"输入模式加工"(LINE/ECHO)是另一条路,本篇不展开。
- 录这五份 .out 时,控制台是全机共享的一个,前一份实验写在缓冲区里的残迹会被后一份读到——所以每个实验开头都自己清场(FillConsoleOutputCharacter/Attribute + 光标归零)。

## 复现

```sh
cd code/volumn_codes/vol8/systems-programming/windows/console/01-console-api
for e in e1_mode_bits e2_buffer e3_input_events e4_vt e5_mixing; do
  /mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra $e.cpp -o $e.exe && chmod +x $e.exe && ./$e.exe
done
```
