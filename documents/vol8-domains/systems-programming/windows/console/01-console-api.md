---
title: "Windows 控制台:字符网格、输入事件与 VT 序列"
description: "Windows 侧终端章唯一一篇,管控制台的输入输出面,事件面归进程章。方法论继承并升级:interop 下 stdio 是管道、GetConsoleMode 直接 gle=6,CreateFile CONIN$/CONOUT$ 直开无窗口控制台真句柄,验收一律 ReadConsoleOutput 把字符与属性字节读回来对表、不靠人眼。模式位全图:CONIN$ 出厂 0x1f7(QUICK_EDIT/EXTENDED/AUTO_POSITION 全在)、CONOUT$ 0x3、VT 位默认关、输入输出代码页双 936。屏幕是 120x9001 的 CHAR_INFO 网格对 120x30 可见窗,WriteConsoleOutput 整块直写不碰光标(60/60 读回全对)、WriteConsoleA 跟光标走、越右缘裁到 119、上滚填补实测。输入是一队 INPUT_RECORD:键一按一松两条记录六字段全录,点数、窥视、取走、清空四件工具,鼠标与焦点也是记录(FOCUS 文档口径系统内部用应当忽略),注入的 Ctrl+C 只是一条带 LEFT_CTRL_PRESSED 的键记录,WINDOW_BUFFER_SIZE_EVENT 在无窗口控制台上触发不了系统侧路径(缓冲区尺寸连原样重设都报 87,SetConsoleWindowInfo 改窗成功、srWindow 读回真变而队列 0 条不投)如实入册。VT 序列开关前后对照:关时 ESC[31mRED 九个字面字符属性全 07,开时只剩 RED 三格属性 04,亮红组合读回 0xc,定位序列后光标 (9,4),2J 只清屏不动光标(ANSI ED 语义,DOS ANSI.SYS 与老 conhost 的归零口径要注时间,文档示例的经典 API 两法都显式补了归位)。混用边界:VT 定位与 WriteConsoleOutput 自带坐标互不干扰,WRAP_AT_EOL 关掉后 115 列起写 8 字符反复覆盖 (119,6) 只剩 H,DISABLE_NEWLINE_AUTO_RETURN 不设时行尾 \n 连回车一起做(C 到 (0,10))、设了只下移一行(C 停在 (119,10))。收尾 Linux 对照:tty 的行编辑在内核行规程对 Windows 的行编辑在 conhost 宿主进程,VT 序列两边是近亲"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 18
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "控制台事件与 APC"
related:
  - "控制台事件与 APC"
  - "termios 与 raw 模式:终端这层在替您做什么"
  - "伪终端与进程交互:终端录制与回放"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# Windows 控制台:字符网格、输入事件与 VT 序列

[控制台事件与 APC](../process/02-console-apc.md)的收尾处,咱们留下过一个字节:咱们把 `ENABLE_PROCESSED_INPUT` 关掉,Ctrl+C 就从控制事件降级成了输入字节 0x03,当时说好的,是控制台模式的完整展开留给终端这一章。欠下的话,这一篇由咱们来还。那一篇管的是控制台的事件面,回答的是 Ctrl+C 凭什么变成一次函数调用,咱们这一篇管输入输出面,问题也换得更朴素了:您 printf 出去的字符,最后落在一块什么样的存储上?您按下去的键,在程序还没读到它的时候,又是什么形态?还有那串 `\x1b[31m` 式的 VT(Virtual Terminal,中文的名字叫虚拟终端)转义序列,它在 Windows 上凭什么能把字变红?

Windows 交出来的答案有三个,而且各有各的样子。咱们说屏幕:控制台手里攥着的并不是一串字节,它是一块带颜色标注的字符网格,行和列都是能按坐标直接寻址的。咱们说键盘:这边进来的是一条条排好了队的事件记录,字符流的影子都没有,每条记录的六项字段都填得清清楚楚。咱们还有第三条路,走的就是 VT 序列,它是 2015 年才铺进 conhost(Windows 的控制台宿主进程,传统控制台窗口背后的服务方)的兼容层,让写惯了 `\033[31m` 的跨平台程序,一行代码都不用改就跑起来了。咱们挨个看,用的路数,还是跑实验、读回证据、对表说话的老一套。

环境与编号的口径,咱们照例交代在开头。本篇的实验按 e1 到 e5 编号,对应仓库 `code/volumn_codes/vol8/systems-programming/windows/console/01-console-api/` 下面的 `e1_mode_bits` 到 `e5_mixing` 五组实验,各自的 .cpp 与 .out 同名成对,原始输出都入了册,复现命令写在目录的 README 里,与 W01 到 W05、内存章、进程章和异步 I/O 章的各套 e 系互不相干,您认文件名就不会认错人。实验的程序都是自包含的小家伙,W01 定义的 `check_win32` 与思维基石两篇的 `unique_handle`,这一篇咱们没请出场,您想把某个实验单独复制走复现,不必拖上任何公共的头文件。机器还是笔者的 Win11 26200,编译器是 MSYS2 UCRT64 的 g++ 16.1.0,编译时统一给的命令是 `g++ -std=c++20 -Wall -Wextra`,warning 的计数是 0,输出的捕获日期是 2026-10-05。跑法沿用 [Win32 文件 I/O](../file-io/01-win32-file-io.md)(系列里咱们简称它 W01)交代的 interop 链路:咱们在 WSL 里直跑 `.exe`,stdio 挂的是管道,进程连着一个没有窗口的控制台。这个处境[控制台事件与 APC](../process/02-console-apc.md)已经领咱们趟过一遍,这一篇咱们把它升级成方法论,下一节咱们专门讲它。

## 从管道手里拿回控制台

e1 的头一件事,就是把咱们的处境量出来。咱们对 stdin 和 stdout 各调一次 `GetConsoleMode`,两个都返回了 0,`GetLastError` 报的是 6(gle 是咱们给 GetLastError 起的短名,6 号报的就是 ERROR_INVALID_HANDLE,说的就是句柄不对路):

```text
[1] GetConsoleMode(stdin)=0 GetConsoleMode(stdout)=0 GetLastError(后一次)=6
    stdio 挂的是管道;控制台要用 CONIN$/CONOUT$ 直开
```

返回 0 的意思,是 stdin 和 stdout 压根就不是控制台的句柄。interop 的启动链把 stdin、stdout 换成了管道,`GetStdHandle` 拿回来的,自然也就是管道的句柄,咱们拿它们去调控制台 API,当然要吃瘪。进程倒是确确实实挂在一个控制台上,而且这个控制台有缓冲区、有输入队列、有模式位,咱们后面四组实验,全是在它身上做的。想拿到它的真句柄,文档给了正路,Console Handles 一页的原话是 `The CreateFile function enables a process to get a handle to its console's input buffer and active screen buffer, even if STDIN and STDOUT have been redirected`,重定向是挡不住 CreateFile 的:传 `CONIN$` 拿到的是输入缓冲区的句柄,传 `CONOUT$` 拿到的则是活动屏幕缓冲区的句柄。

```cpp
// e1_mode_bits.cpp(节选):设备名当文件名开,访问权两边都要,共享位也得给足
HANDLE cin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
HANDLE co  = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
```

节选里咱们用的是 `CreateFileA`,它跟 W01 立的现代代码一律点名 W 版的口径看着相抵,理由咱们在这里交代一句:W 版的口径管的是路径,路径里的中文要过代码页的翻译才不丢。而 CONIN$ 与 CONOUT$ 是纯 ASCII 的保留设备名,翻译是压根不经过的,A 版用在这上头是安全的。

咱们拿到句柄之后,这一篇的方法论就立起来了。屏幕上到底是什么,咱们不用人眼去瞧,咱们一律用 `ReadConsoleOutput` 把网格连字符带属性读回来,断言也是写在程序里的,读回的 60 格就该 60 格全对,差了一格都算失败。您要是写过图形程序,会发现这就是截图对拍的路数,只不过控制台的截图是现成的 API,连像素都不用咱们解释。这一篇的五组实验,验收走的都是把状态读回来对表,读回的可以是网格,也可以是队列的计数。

> 再给您一个判别手法,这一招的出处同样是 Console Handles 那页:拿不准一个句柄是不是控制台,咱们就去调 `GetFileType`,控制台句柄报的是 `FILE_TYPE_CHAR` 字符设备。您写跨平台工具、想探测自己是不是跑在终端前面的时候,它可比 try 一把控制台 API 体面多了。

stdio 被换成管道的处境,还不是 interop 独有的怪癖。您在任何一个 shell 里写 `foo | more`,foo 的 stdout 一样是管道,它一样是摸不到控制台的。CONIN$/CONOUT$ 的价值因此比应付 WSL 宽得多,凡是标准流被重定向的场合,程序想保住直接画屏的能力,靠的都是它。

## 模式位的全图

真句柄到手了,e1 的正餐开席:咱们把两边模式位的出厂态,挨位地把它们翻成人话。CONIN$ 读回的是 0x1f7,CONOUT$ 读回的是 0x3,文档里登记的位,咱们全摆在下面:

| 输入位 | 名字 | 管什么 |
| --- | --- | --- |
| 0x1 | ENABLE_PROCESSED_INPUT | Ctrl+C 等特殊键走控制事件路 |
| 0x2 | ENABLE_LINE_INPUT | 行缓冲,回车才交货 |
| 0x4 | ENABLE_ECHO_INPUT | 输入回显 |
| 0x8 | ENABLE_WINDOW_INPUT | 缓冲区尺寸变化投 WINDOW_BUFFER_SIZE_EVENT |
| 0x10 | ENABLE_MOUSE_INPUT | 鼠标事件进队列 |
| 0x20 | ENABLE_INSERT_MODE | 行编辑的插入/覆盖(EXTENDED 的子开关) |
| 0x40 | ENABLE_QUICK_EDIT_MODE | 鼠标选择与右键粘贴(EXTENDED 的子开关) |
| 0x80 | ENABLE_EXTENDED_FLAGS | 上两位的载体,想动它们必须连着设 |
| 0x100 | ENABLE_AUTO_POSITION | 窗口出现的位置交给系统 |
| 0x200 | ENABLE_VIRTUAL_TERMINAL_INPUT | 输入改走 VT 字节,不再合成键事件 |

| 输出位 | 名字 | 管什么 |
| --- | --- | --- |
| 0x1 | ENABLE_PROCESSED_OUTPUT | 处理退格、制表、响铃这类控制字符 |
| 0x2 | ENABLE_WRAP_AT_EOL_OUTPUT | 写到行尾自动回绕换行 |
| 0x4 | ENABLE_VIRTUAL_TERMINAL_PROCESSING | 把输出的 VT 转义序列当指令解释 |
| 0x8 | DISABLE_NEWLINE_AUTO_RETURN | 行尾的 \n 不自动带回车 |
| 0x10 | ENABLE_LVB_GRID_WORLDWIDE | 网格字形的属性位,罕见字体才用 |

咱们对着表数一数 0x1f7:亮着的位有八个,黑掉的占了两个,一个的名字叫 WINDOW_INPUT,另一个的名字是 VIRTUAL_TERMINAL_INPUT。行编辑与回显就住在这几个亮着的位里,LINE_INPUT 搭配的是 ECHO_INPUT,正是 Linux 那边 termios(Linux 给终端立的那套开关接口)的 ICANON 配 ECHO 在 Windows 侧的对位,您要做密码输入、想关掉回显,关掉的就是输入侧 0x4 的 ENABLE_ECHO_INPUT。输出侧的 0x3 只亮着 PROCESSED 和 WRAP 两位,值得咱们多看一眼的,是黑着的 0x4:VT 解释默认不开,这一点上文档口径与咱们的实测一致,e4 的一整节都在跟 0x4 打交道。

输入表里还有一处三位的捆绑,咱们单独说一句。表里的 INSERT 与 QUICK_EDIT 担任的是子开关,而它们的载体是 EXTENDED_FLAGS,文档的要求写在位说明里:您想动它们,就得把 EXTENDED_FLAGS 一起设了。出厂态里的 EXTENDED 是亮着的,扛的正是它上头的 INSERT 和 QUICK_EDIT,QUICK_EDIT 也是开着的,意味着鼠标框选与右键粘贴在咱们做实验的全程都是活着的,这也是控制台体验里最容易被咱们当成理所当然的一部分。

黑着的 VIRTUAL_TERMINAL_INPUT 咱们也交代一下,它管的是输入侧:设上它之后,键盘上那些会产生转义序列的键,比如方向键的输入,就不会再被合成为键事件的记录,而是把 VT 序列当原始字节放进缓冲区,解析的事交给程序。它是输出侧 0x4 的对称开关。本篇的实验没有开它,e3 看的还是经典的记录世界,您写全屏程序需要精细的按键控制时,它就是另一头的入口。

`SetConsoleMode` 改了是读得回的,e1 里咱们给 CONIN$ 整个清零,读回的就是 0x0000,又给 CONOUT$ 单独点亮了 VT 位,回来的就是 0x0007,改写的路子是双向通车的。另外咱们查了代码页(Code Page,Windows 里管字符编码的号码),输入输出两边的代码页都是 936,指的就是 GBK,字符进了网格以后,存的是 `UnicodeChar`,双字节的中文照样一格一个,这倒是 CP 936 环境下不用咱们操心的事。

PROCESSED_INPUT 的那一行,咱们点到为止。它关掉之后 Ctrl+C 降级成字节 0x03 的整场戏,进程章[控制台事件与 APC](../process/02-console-apc.md)的 e7 已经演过了,信号怎么递、忽略位怎么继承,都是那一篇的正题,咱们这边就不重演了。

## 屏幕:一块 120x9001 的网格

现在咱们回答头一个问题:printf 的字,落在了哪里。e2 一上来就调了 `GetConsoleScreenBufferInfo`,把这块存储的尺寸端了出来:

```text
[1] 缓冲区 dwSize=120x9001(带滚动历史) 可见窗 srWindow=120x30 光标=(0,0) 默认属性=0x7
```

两个数字说的可不是同一件事。缓冲区是 120 列乘 9001 行的一整块,可见窗只是它中间 30 行高的一个观察窗口,您往上翻,看到的是缓冲区里更早的内容。9001 的高度,只是机器出厂的配置,咱们不拿它当通用常数,但它至少说明了一件事:滚动历史不是终端窗口替您记的,它就记在缓冲区的本体里。这块网格的最小单位是 `CHAR_INFO`,它的字段有两个,`Char.UnicodeChar` 装的是字符,`Attributes` 装的是一个 16 位的属性字,常用的是低 8 位:前景的蓝绿红是 0x1、0x2、0x4,加亮的位是 0x8,背景三个挪到了 0x10、0x20、0x40,背景的加亮位是 0x80。咱们把一个字节两半分,低半管的是字色,高半管的是底色,出厂的默认是 0x7,对应的正是灰白字配黑底。文中坐标咱们一律按 `(x,y)` 记,x 记的是列号,y 记的是行号,都是从 0 起算的。

往这块网格上写字的经典 API 有两种姿势,脾气是完全不同的。`WriteConsoleOutput` 是整块直写:您备好一个 CHAR_INFO 数组,您给它一个目标矩形,它就把整块的内容按坐标铺过去。e2 铺的是一块 6 行乘 10 列的彩色矩阵,前 3 行给的是亮字,后 3 行里奇数行垫的是蓝底,写完咱们用 `ReadConsoleOutput` 把同一块读回来对表:

```text
[2] WriteConsoleOutput(6x10 @ (2,0)) -> 1;光标 (0,0)->(0,0):整块直写不碰光标
    ReadConsoleOutput 读回 60 格,字符+属性全对:60/60
    第 0 行属性=RGB|---(亮白字)
    第 1 行属性=RGB|--B(亮字/蓝底)
```

咱们读回的 60 格全都是对的,颜色也是没有串的,红的就是红的,蓝的就是蓝的。而这段输出里,还藏着一个更要紧的对照:光标从头到尾都站在 `(0,0)`,是没有挪过窝的,可见整块直写的姿势,是根本不碰光标的。`WriteConsoleA` 就完全不一样了,它把光标当成了自家笔尖,写在光标的地方,写完了还推进。e2 的 [4] 段把光标摆到 `(40,8)`,写下了 hi:

```text
[4] SetConsoleCursorPosition(40,8)+WriteConsoleA("hi"):写 2 字节,光标推进到 (42,8)
```

咱们看它写 2 字节,光标就推进到了 `(42,8)`,笔尖的每一步都留了痕。这个差别到了后面的 e5,咱们还要专门拿它做文章,您记下:一边认的是坐标,一边认的是笔尖。

直写的一条脾气咱们也验到了:矩形越出了缓冲区,越出去的部分会被直接裁掉,放得下的照写。e2 的 [3] 段从 x=115 起写 10 列宽的一块,缓冲区的宽度只有 120 列:

```text
[3] 写 10 列宽 @x=115,实际落下的矩形 Right=119(裁到缓冲区右缘 119,只写进 5 列),读回首格字符='A'
```

落下的矩形,右边收在了 119,只写进去了 5 列。您做全屏界面的绘制时,边缘的对齐得咱们自己算,它提醒您的方式是悄悄改写目标矩形,而不报错。

同类的 `ScrollConsoleScreenBuffer` 咱们也一并验了:把第 0 到 5 行整体上滚 2 行,空出来的底部两行,由咱们指定的字符和属性填补:

```text
[5] 上滚 2 行后第 0 行读回:"CCCC"(原第 2 行顶上来;空出的底部两行由红字 '.' 填补)
```

咱们读回第 0 行,内容正是原第 2 行留下的 CCCC,填补的字符属性也是咱们指定的红字。滚动走的也是网格操作的路子,搬的是整行整块的 CHAR_INFO,不存在字节流里插删什么的事。

## 键盘:一队事件记录

输出侧的世界观是网格,输入侧对应的,是一队排好的记录,队里的元素是 `INPUT_RECORD`。e3 把这个世界的居民请出来遛了一圈。本机是没有真人坐在控制台前敲键的,所以输入记录一律用 `WriteConsoleInput` 注入。这里的口径咱们得交代清:注入是搬运,把一条填好的记录直接放进队列的队尾,它不重新走一遍键盘摄入的加工链。这个差别其实等一下就要变得有意思了。

咱们从键盘居民的长相看起。'A' 键的一按一松,是两条独立的 KEY_EVENT_RECORD,六项字段全都录下来了:

```text
[3] ReadConsoleInput 逐条读:
    KEY_EVENT  按下 vk=0x41(A) scan=0x1e char=A repeat=1 修饰:(无)
    KEY_EVENT  松开 vk=0x41(A) scan=0x1e char=A repeat=1 修饰:(无)
    KEY_EVENT  按下 vk=0x43(C) scan=0x2e char=c repeat=1 修饰:L-Ctrl
    MOUSE_EVENT 位置=(5,3) 按钮=0x1 事件=0x0 修饰:(无)
    FOCUS_EVENT  bSetFocus=1
    取完后队列余量=0
```

六项的内容,咱们逐项认:记录的是按下还是松开、重复的次数、虚拟键码、扫描码,还有翻译好的字符和修饰键的状态。虚拟键码是 Windows 键盘布局里的逻辑键号,扫描码是键盘硬件报的物理键号,同一次按 'A' 键的动作,两个码给的分别是 0x41 与 0x1e,一个管的是抽象身份,一个管的是物理位置,想做按键重映射的程序,两头的码都是要用的。修饰键的状态是一个位图,Ctrl+C 的记录里,`LEFT_CTRL_PRESSED` 就是亮着的。鼠标点击走的是 MOUSE_EVENT 记录,坐标、按钮、事件种类都是带齐的。焦点变化也有自己的记录,不过文档对 FOCUS_EVENT 的口径没有商量的余地,文档的原话是 `These events are used internally and should be ignored`,意思说的是系统内部使用,应用程序读到了就应当忽略,咱们把它注入进来,只是为了看一眼它的长相,可不是请您拿它写逻辑。

队列本身给咱们配了点数、窥视、取走、清空四件工具,行为是各管一段的。`GetNumberOfConsoleInputEvents` 管的是点数,`PeekConsoleInput` 管的是窥视,而且窥完不取走,e3 的头两段把它们各自的性格演给了咱们看:

```text
[1] 注入 5 条后,GetNumberOfConsoleInputEvents=5
[2] PeekConsoleInput 取 1 条(不取走),EventType=KEY_EVENT:1,队列余量仍 5
```

注入了 5 条,点数报的就是 5,Peek 窥完了一条,报出的余量还是 5,队列的内容一根汗毛都没少。`ReadConsoleInput` 才是真取的,一条一条地出队,五条都读完了,余量归了零。`FlushConsoleInputBuffer` 干的则是一键全清。您拿它对照 Linux 那边就明白了:tty(teletype 的缩写,Unix 世界对终端设备的叫法)的输入是字节流,这边是有结构的记录队列,Peek 这样看一眼再说的动作,字节流上根本没有它的对等物。

这一段咱们还遇上了本机的一处限制,得如实交代给您。输入的位里有个 ENABLE_WINDOW_INPUT,开了它之后,缓冲区的尺寸一有变化,该投的 WINDOW_BUFFER_SIZE_EVENT 就来了。可在这个无窗口的控制台上,`SetConsoleScreenBufferSize` 是全部被拒的,e3 的 `[4]`、`[5]` 两段把读数摆全了:

```text
[4] 开 ENABLE_WINDOW_INPUT 后 SetConsoleScreenBufferSize(120x3000) -> 0 GetLastError=87
    于是这条事件的形态用注入演示:
    WINDOW_BUFFER_SIZE_EVENT  新尺寸=100x30
[5] SetConsoleScreenBufferSize(原样 120x9001) -> 0 GetLastError=87
    SetConsoleWindowInfo(同尺寸 120x30 原样重设) -> 1 GetLastError=0 队列=0
    SetConsoleWindowInfo(异尺寸 120x29) -> 1 GetLastError=0 队列=0
    读回 srWindow=120x29(窗口真改没改,以读回为准)
```

改缓冲区尺寸的尝试是全被拒的:异尺寸的 120x3000 被拒,连照着 120x9001 的原样重设,报的也都是 gle=87。`SetConsoleWindowInfo` 那边倒是另一副脾气:同尺寸、异尺寸两次调用都返回了成功,而且 srWindow 读回真的从 120x30 变成了 120x29,改动是实打实落下去的。可窗口缩了一行,队列的计数还是 0,WINDOW_BUFFER_SIZE_EVENT 是一条没投的。系统侧的触发路径,在咱们的台架上走不通,咱们只能注入一条记录,演示它的形态,记录里带的字段,就是新尺寸的 xy。真窗口里拖边改尺寸的路径,本机测不了,您手上有真窗口的机器,您可以补上这一块。

最后回到咱们注入的 Ctrl+C 记录。它进了队,也被读出了,自始至终只是一条带 L-CTRL 修饰的键记录,是没有被转成控制事件的,handler 倒是一次都没跑。原因就藏在前面的口径里:按两篇合起来的口径,信号转换应当长在键盘摄入的加工链上(真键盘的路径,本机是没测过的),而注入直写队列,把加工链绕开了。Ctrl+C 从键记录到函数调用的完整旅程,连同 PROCESSED_INPUT 位怎么当闸门,都是[控制台事件与 APC](../process/02-console-apc.md)的正题,那一篇的 e7 管到底。

> 还有一层关系咱们也点一下:LINE_INPUT、ECHO_INPUT 这些输入模式位,管的是 `ReadFile` 和 `ReadConsole` 看到的字节视图,回车的交货、输入的回显,都是发生在那一层的。`ReadConsoleInput` 读的是记录视图,它直接越过了行编辑。两种读法各在各的层,您选了记录这个视图,行编辑的便利也就同时放弃了,后面得您自己接。

## 第三条路:VT 序列

网格和记录的世界观,是 Windows 的原生发明,可咱们平常见到的命令行程序,大多数发的都是字节流,Linux 世界更是从小只会 `\033[31m` 的方言。Windows 10 的 1511 更新(build 10586,2015 年 11 月)把 VT 解释层铺进了 conhost,字节流里的转义序列,从此是可以被当成指令翻译的,开关就是输出侧那个默认黑着的 0x4 位。咱们拿 e4 的开关做一场前后对照,验收走的还是咱们的老办法,咱们把缓冲区读回来,让它自己给咱们说话:

```text
[1] VT 关:WriteConsoleA 写了 12 字节,序列没被吃——
    第 0 行读回:"ESC[31mREDESC[0m"
    前 9 格属性: 07 07 07 07 07 07 07 07 07
[2] VT 开:同一串 12 字节,只剩 R E D 三个字符进了缓冲,属性=红色——
    第 0 行读回:"RED         "
    前 9 格属性: 04 04 04 07 07 07 07 07 07
```

同一串 12 字节的 `\x1b[31mRED\x1b[0m`,得到了两种完全不同的下场。开关关着的时候,12 个字节是 12 格的普通字符,ESC、`[`、`3`、`1`、`m` 一路当正文铺进了网格(读回打印时 0x1b 显示为 ESC 三个字母),属性是清一色的 07。开关打开了之后,序列被吃掉了,只留下了正文,前 3 格的属性变成了 0x4,正是 FOREGROUND_RED 的值。您平时在 Windows 终端里看到 ANSI 颜色不生效、源代码原样滚一屏,头一个该查的就是输出侧 0x4 的 ENABLE_VIRTUAL_TERMINAL_PROCESSING 位,尤其是程序被重定向了之后,VT 位是不会跟着句柄走的,得您在真控制台的句柄上重新开。

组合参数咱们也验了,亮红的 `\x1b[1;31m`,读回的属性与光标定位的读数是这两行:

```text
[3] \x1b[1;31mX 读回:字符='X' 属性=0xc(RED=0x4|INTENSITY=0x8 -> 0xc)
[4] \x1b[5;10H 后光标=(9,4)(VT 是 1 起算,缓冲区坐标是 0 起算,9,4 对上了)
```

咱们看 0x8 的加亮,配上 0x4 的红,在位运算上是正好对得上的。这里咱们把 e2 和 e4 接通:SGR(Select Graphic Rendition,VT 里管颜色与加亮的那类序列)31 要的红,落进缓冲区就是属性字节的 0x4,与 `WriteConsoleOutput` 直接填的 FOREGROUND_RED 是同一个位。VT 序列翻译到了最后,改的还是 CHAR_INFO 的属性,它给咱们的只是一块网格上的另一种写法。所以经典与 VT 混用,颜色是打不起架的,家底是只有一份的。

光标类的序列,同样落在网格的坐标系上,只有一个起算的差别:`\x1b[5;10H` 用的是 VT 的 1 起算,落到 0 起算的缓冲区坐标,就落在了 `(9,4)`,行号从 5 变成了 4,列号从 10 变成了 9。跨平台程序在这上头栽跟头的不少,Linux 那边 0 起算的坐标想搬进 VT 序列,您得记得行和列都加一。

清屏也值得咱们单独拎出来。笔者把光标摆到 `(30,3)`,把 `\x1b[2J` 写了进去,咱们再去读光标:

```text
[5] 光标先放 (30,3),\x1b[2J 之后光标=(30,3) —— 2J 只清屏不动光标,与 ANSI 的 ED 定义一致(老 conhost 曾顺手归零,本机 26200 不再如此);缓冲区读回 "    "(清空成空格)
```

2J 把整个可见区擦成了空格,光标却在原地没动。这正合 ANSI 对 ED(Erase in Display)的定义:它管的是擦字,光标是不管的。可这件事是有历史包袱的,DOS 年代的 ANSI.SYS,用的就是清屏时把光标一并送回原点的老口径,microsoft/terminal 仓库的 issue #60 里,有人掰扯过这段新旧标准的分野。文档的态度,其实藏在示例里:Clearing the Screen 一页给了三种清屏法,VT 的示例写完 `\x1b[2J` 就收了场,后两种经典 API 的示例,却都显式多补了一句归位,注释的原话是 `Move the cursor to the top left corner too`,示例二的注释还说了,这一法匹配的就是 cmd 里 cls 的行为,收尾同样是滚动加归位的两步。咱们的实测站在 ANSI 这一边,不过您要是指望着 2J 清屏时把光标一并送回原点,就请您补上一句 `\x1b[H`,要么您就干脆调 `SetConsoleCursorPosition(0,0)` 把光标摆回去,别去赌 conhost 的版本脾气。

## 两条路混着走,行尾的两种解释

在真实的程序里,经典 API 和 VT 序列常常是并存的,它们写的缓冲是同一块,用的笔法却有两套,e5 问的就是它们打不打架。咱们拿三个小实验,把答案一个个地落实。

头一个是坐标的归属。e5 的 [1] 段让两套写法同台了一次:咱们用 `WriteConsoleOutput` 把一个 # 直写到 `(40,2)`,咱们再用 VT 定位 `\x1b[2;5H`,把 AB 连着写了上去。

```text
[1] A 落在 (4,1),B 落在 (5,1)(跟光标走);'#' 落在 (40,2)(WriteConsoleOutput 自带坐标)
    此刻光标=(6,1) —— 两套写法互不干扰,但光标只认 VT/WriteConsole 一系
```

那个井号落在它自带的 `(40,2)`,A、B 顺着 VT 定位的笔尖走,落在了 `(4,1)` 和 `(5,1)`,光标停在了 `(6,1)`。两套坐标是互不干涉的,原因就在咱们 e2 验过的事实里:整块直写根本不碰光标,所以它没法打扰笔尖,而笔尖也不认识它。混用是可以的,不过您脑内得随时清楚,自己此刻握的是哪一支笔。

咱们看第二个实验,说的是行尾。`ENABLE_WRAP_AT_EOL_OUTPUT` 它默认是开着的,写到了行尾,会自动回绕到下一行的行首。关掉它又会怎样?e5 从 115 列起写 ABCDEFGH 共 8 个字符,缓冲区的行宽是 120,注定是要越界的:

```text
[2] WRAP_AT_EOL 开:光标起 (115,6) 写 "ABCDEFGH",A=(115,6) H=(2,7) —— 越过行尾回绕到下一行,E..H 在第二行
[2] WRAP_AT_EOL 关:光标起 (115,6) 写 "ABCDEFGH",A=(115,6) H=(119,6) —— 越过行尾不回绕,E..H 反复覆盖 (119,6),只剩 H
```

开着的场合,A 到 E 填满了 115 到 119,回绕到下一行去的是 F、G、H,H 落在了 `(2,7)`,一切都是如常的。关掉的场合就怪了:H 居然在 `(119,6)`,可中间的 E、F、G 去哪了?答案是全都原地待过,只是被后来者一个个压掉了。不回绕的时候,笔尖越过了行尾,就被按在了最后一格上不动,后续的每个字符,都写到了 `(119,6)`,后来的字符把前面的盖掉了,8 个字符最后就只剩下了一个还在原地的 H。(存档那两行尾巴上的 E..H 注得不严谨,回绕过去的头一个字符是 F,E 从头到尾待在行尾的 119 格里没动过,咱们按读回的坐标说话。)您要是碰到长字符串输出、最后只剩末尾一个字符的现场,查的就是这个位。

第三个是 `\n` 的两种解释,咱们接着看。`DISABLE_NEWLINE_AUTO_RETURN` 不设的时候,行尾碰到了 `\n`,回车换行是一起做的,光标会到下一行的行首。设上了之后,`\n` 换的只是行,列号在原地不动:

```text
[3] DISABLE_NEWLINE_AUTO_RETURN 不设:B=(118,9) C=(0,10) —— 行尾 \n 连回车一起做,C 到下一行行首
[3] DISABLE_NEWLINE_AUTO_RETURN 设:B=(118,9) C=(119,10) —— \n 只下移一行不回零,C 落在 (119,10)
```

同样的一串 AB 加换行加 C,咱们从 117 列写起,您把开关一变,C 的落点就从 `(0,10)` 变到了 `(119,10)`。这个位是给 VT 程序预备的:VT 序列自己管光标的定位,系统要是再自作主张地帮您回零,反而会打乱 `\x1b[<n>E` 这类相对定位的算盘,咱们把它设上,换行的语义就交还给您的序列了。

## 另一侧怎么看

这一章的 Linux 侧有两篇,一篇讲的是 termios 与 raw 模式,另一篇讲的是伪终端与进程交互,咱们这边收尾之前,把两边的机制摆在一起对一对。

它们的行编辑和回显住在哪里,两边的答案不一样,却同样不在您的程序里。Linux 把它们放在了内核的 tty 行规程里,termios 的 ICANON 与 ECHO,是给那一层立的开关,Windows 那边把它们放在了控制台子系统里,模式位的 LINE_INPUT 与 ECHO_INPUT 是同一类闸门。咱们这一篇的 e3 注入,还有 Linux 侧拿管道对拍 pty(伪终端)的实验,验出的是同一件事:读程序是管不着的,shell 也一样是管不着的,管这事的,是中间的那一层,差别只是住址的不同:Linux 的住在内核里,Windows 的住在 conhost 这个宿主进程里。

VT 序列则是两边的近亲,亲近到您写的 `\x1b[31m` 不用改一个字节。不过方向是反的:Linux 世界从来就是程序发字节流、终端仿真器负责解释,VT 是它的原生语言,Windows 的原生语言却是网格和记录,VT 是 2015 年才加上的翻译层,把字节流翻译成网格的操作。所以您在这边要多操一份心,确认翻译层是开着的,就是咱们前面说的那个 0x4 位,而 Linux 那边多操的另一份心,是终端仿真器的方言差异。真到了要写跨平台的终端代码,两边各读各的模式位、各关各的行编辑,关的法子不同,目的却是一样的,咱们这套对位,在 Linux 侧的那两篇里,已经从 termios 的方向走了一遍。另一侧的完整故事,还请您移步 [termios 与 raw 模式](../../linux/terminal/01-termios-raw.md) 与 [伪终端与进程交互](../../linux/terminal/02-pty-recording.md)。

实验的程序们收摊之前,都把现场清了:模式位按出厂态还了回去,玩过的网格区域填回了空格,光标也归了零。控制台是全机共享的一块,咱们的实验在上面留了字,下一个跑进来的程序,头一眼看到的就是它,这样的整洁,也值得您写自己的终端程序时带上。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="Console Handles(CONIN$ 与 CONOUT$ 的正路)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/console-handles"
  />
  <ReferenceItem
    :id="2"
    title="SetConsoleMode function(全部模式位的定义)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/setconsolemode"
  />
  <ReferenceItem
    :id="3"
    title="WriteConsoleOutput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/writeconsoleoutput"
  />
  <ReferenceItem
    :id="4"
    title="ReadConsoleOutput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/readconsoleoutput"
  />
  <ReferenceItem
    :id="5"
    title="INPUT_RECORD structure(事件记录的六字段)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/input-record-str"
  />
  <ReferenceItem
    :id="6"
    title="ReadConsoleInput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/readconsoleinput"
  />
  <ReferenceItem
    :id="7"
    title="WriteConsoleInput function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/writeconsoleinput"
  />
  <ReferenceItem
    :id="8"
    title="FOCUS_EVENT_RECORD structure(系统内部用、应当忽略的口径)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/focus-event-record-str"
  />
  <ReferenceItem
    :id="9"
    title="Console Virtual Terminal Sequences"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences"
  />
  <ReferenceItem
    :id="10"
    title="Clearing the Screen(三种清屏法与光标归位)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/clearing-the-screen"
  />
</ReferenceCard>
