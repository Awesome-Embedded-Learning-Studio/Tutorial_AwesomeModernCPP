---
title: "Renode 观测课：没有板子，谁说了算"
description: "工具链四件套的最小安装，Renode 系统级模拟器的工作原理与功能级边界，resc 脚本逐行对号与 bluepill.repl 板级户口的三处增量；重头戏是判据课——250ms 采样、1 秒整周期的假静止、uwTick 对拍，全部真输出，外加一个 WSLg 被 DISPLAY 旧配置坑掉窗口的排查实录"
chapter: 0
order: 2
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - renode
difficulty: beginner
platform: stm32f1
reading_time_minutes: 15
related:
  - "为什么是 C++,凭什么?"
  - "C++ 简史:这些坏印象是哪来的"
---

# "你说灯在闪，可板子呢？"

说的道理，板子呢？啊哈，上一篇您佬这就是没仔细看，咱们说了，Renode!这是何方神圣呢？为什么邀请出来这位老哥呢？

突发奇想这个的，是跟 @Leon19960120 大佬聊天的时候，他问：

> 欸你这个教程看起来好像还行（那个时候是TAMCPP和隔壁imx-forge的极早期），不过我有个疑惑啊，万一咱们这没板子的，你拒之门外嘛？

虽然说嵌入式没板子真的不行，但是，咱们也必须照顾刚来的各位。但是就算如此，我们也有理由说——得，板子买不起，不管了！（虽然笔者完全不赞同，但是这的确够堵上我嘴了）

真正另外下定决心探索 Renode 模拟方案的，是另一个话题。

> 证明您的嵌入式程序是逻辑可靠的。

笔者从事的是互联网的客户端开发。尽管不那么界面好测，UI也被处处为难，但是终归我们选择逻辑层和界面层剥离（当然，MVP，MVVM等那些GUI编程的术语，还请留给GUI支线上），剥离开来进行测试。这里呢？我们也能拿到我们的ARM Binaries，进行回归，逻辑测试嘛？

可以，Renode实际上就是做这个的——证明现象为真，只是我们刚好可以服务于没有板子的朋友！

> 前排声明：笔者的所有输出都在本机真跑过，您跟着敲**应该（环境配置是选学）**能逐行复现。要是复现不出来，先怀疑笔者的环境，再怀疑您 的，最后才是 Renode 的——Issue 请~

## 好家伙！你小子让我安装的工具现在上门是吧

YES，是的。终于咱们可以启航了。别担心，其实安装工具链在Linux / WSL上完全不复杂。当您的同学还在焦虑的安装编译器的时候，您只需要邪魅一笑，打下这一行指令

```bash
sudo apt install gcc-arm-none-eabi cmake ninja-build
```

就可以戴上墨镜，成为低调的黑客。

好了好了，墨镜摘下来吧，防止到时候链接错误（我相信您不至于犯编译错误，点个灯不难）和运行的时候灯死在那里的时候在其他人面前社会性死亡。

回来！别开玩笑了，Renode呢？答案是我们自己装。小生为您铺好红毯，这边请： [renode.io](https://renode.io/) 下载 `.deb` 包装，安装细节以[官方安装文档](https://renode.readthedocs.io/en/latest/introduction/installing.html)为准（它对 .NET 运行时有依赖，文档里写得清楚）。Arch 用户笔者就不写命令了，免得哪天包名一变，这篇文章就成了事故现场，您到Issue中投诉这个Charliechen114514就是个王八蛋骗我下了404，我又好含泪pacman -Syyu一下我的issue了

这个世界上验证您是否安装成功，不是只有求助别人点开您那古老的界面弹出来才算是成功。打印版本也是。

```bash
arm-none-eabi-gcc --version && cmake --version && ninja --version && renode --version
```

都能出版本号就齐活。装不上、装坏了的排错咱们这篇不展开，环境问题千奇百怪，拷问我issue，问AI都更好。

## 喂喂，Renode是啥啊？

Renode是啥？**系统级模拟器**。另外的说辞就是：它模拟的不是"一颗芯片"，而是**一整台机器**。

Cortex-M3架构的CPU，GPIO矩阵，上面的Timer，中断控制器，和咱们的Memory以及链接这一切的Bus！一个系统！嵌入式系统！兄弟们！

之后的日子，上面这些足够写一本有一本小册子的东西，将在Renode看来，两个文件搞定。

- `.repl` 文件写"机器上有哪些器件、各自身份是什么"（Renode 管它叫平台描述，platform description）
- `.resc` 写"这台机器这次怎么跑"（启动脚本）。

您回头翻 libestdx 的 `examples/01_blinky/`，这两个文件都在那躺着。笔者没有撒谎~

那凭什么信它？咱们必须诚实地说清楚它的边界：**Renode 是功能级模拟，不是周期精确模拟**<RefLink :id="1" preview="Renode official documentation, Introduction" />。功能级的意思是"程序逻辑走对了它就对了"：灯亮没亮、串口吐了什么字节、中断来没来，它说了算；但"这条指令跑了几个周期、频率精确到多少兆"它不保证。所以任何真的需要验证到这个地步的朋友，我很抱歉，私密马赛！您真的需要离开一下您的电脑，去找您的逻辑分析仪和示波器等几位老朋友了。

## 我点亮的是LED！不是芯片

到这里，还请您打开终端，或者是任何您觉得您干活舒服的IDE，确认一下您的几个工具打下--version的时候，仍然可以笑嘻嘻的吐出来版本号。

上号！

在 libestdx 仓库根目录：

```bash
cmake --build build --target sim
```

构建、起 Renode、加载固件、开跑，一条命令全包。然后 monitor 停在提示符上，屏幕开始滚 GPIOC 输出寄存器的值。这条命令的"剧本"就是 `examples/01_blinky/renode.resc`，咱们把它的主体逐行对号（完整版在仓库里，还有一个打诊断快照的 `$status` 宏，先按下不表）：

```text
using sysbus

mach create
machine LoadPlatformDescription @sim/stm32f1/bluepill.repl

$bin?=@build/examples/01_blinky/blinky

macro reset
"""
    pause
    sysbus LoadELF $bin
    start
"""

watch "sysbus ReadDoubleWord 0x4001100C" 700
```

啊呀！一股CSharp的风味！

`mach create` 造一台虚拟机出来。这是我们的起手动作，当作您的IT支持朋友给您搬来了电脑，拍拍您的肩膀——小伙子年轻，该干活了。

`machine LoadPlatformDescription` 把上面说的那份"机器户口"装进来。机器一个探头，小朋友原来是你这个STM32F103C8T6啊

`sysbus LoadELF $bin` 把固件倒进虚拟 Flash。啥意思呢？大哥，虚拟机要执行你的代码，需要啥啊？您往单片机烧录啥，他就需要啥，我们这里让虚拟机顿顿干饭的就是咱们的固件。

当然每天都写这个，实在是唐，我们依旧发挥我们的抽象神力，设计一个叫做 `$reset` 的宏，前后各垫了 `pause` 和 `start`——所以固件重新编译之后，您在 monitor 里敲一句 `runMacro $reset` 就能重载续跑，不用退出重来。

> 两个坑值得点名。头一个是 `@` 路径：它按 **Renode 进程的当前目录**解析，不按 resc 文件自己所在的目录。所以 CMake 的 sim 目标把 `WORKING_DIRECTORY` 固定在仓库根，`@sim/...` 和 `@build/...` 才都对得上；您要是自己从别的目录手动起 Renode 去 include 这份脚本，路径全断。第二个是 `$bin?=` 里那个问号：条件赋值，"还没定义才赋值"，留了个从命令行覆盖固件路径的口子。
> 上面这个是GLM说的，我写了半天GLM 5.3实在看不下去了，我双手释放我的键盘，GLM告诉我您写的脚本就是一坨屎，顺便嘲讽了我上面两点，让我很气愤。各位朋友注意一下也。

最底下那行 `watch` 让 monitor 每空闲 700 毫秒替咱们读一次输出寄存器。注意"空闲"两个字：watch 是 monitor 空闲时才驱动的，您要是脚本里连轴跑命令，它一声不吭。

这是笔者跑出来的真输出，嗯，长这样（700ms 一次，截前八个），八个数字实在没有任何意义上的美感，没有LED小灯泡，只有跳变的GPIO读取的值。

```text
0x00000000  0x00000000  0x00002000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

咱们看懂这些数字：`0x4001100C` 是 GPIOC 的 ODR（输出数据寄存器）地址，`0x2000` 展开二进制是第 13 位，值在两个状态之间滚，看着像灯在闪。

## bluepill.repl：给板子写户口

Renode 发行版里有官方的 stm32f103 芯片级描述，但没有 Blue Pill 这块板的。啥意思呢？意思就是。您翻一下STM32F103这个系列的芯片，他会说——小朋友，咱们有 GPIO A 到 C、有 USART1 到 3"。

但是他丝毫没办法说我们这里有一个LED，他在 PC13、低电平亮"。大哥别拉错电平了。

这就是咱们的 `sim/stm32f1/bluepill.repl` 就是自己维护的板级户口，骨架长这样：

```text
// flash 同时映射在真地址和启动别名 0x0(复位时从 0x0 取 SP/PC)
flash: Memory.MappedMemory @ {
    sysbus 0x08000000;
    sysbus 0x00000000
}
    size: 0x10000

nvic: IRQControllers.NVIC @ sysbus 0xE000E000
    systickFrequency: 64000000

// 板载 LED:PC13,低电平点亮
led: Miscellaneous.LED @ gpioPortC 13

// RCC —— 官方无 F1 RCC 行为模型,这份用 PythonPeripheral 补握手协议
rcc: Python.PythonPeripheral @ sysbus 0x40021000
    size: 0x100

// 外设位带别名区:HAL 开 PLL 走位带写,别名区不映射会静默丢失
bitBand: Miscellaneous.BitBanding @ sysbus <0x42000000, +0x2000000>
```

三百多行里最要紧的是三处增量，咱们一处一处过。**flash 的双地址映射**：Cortex-M 复位后从地址 0 取初始栈指针和复位向量，没有 0x0 这个别名，启动代码第一步就找不到门。结局就是兄弟你怎么噶了兄弟。

第二一个就是**systickFrequency 对齐 64 MHz**：SysTick 的参考频率是构造期参数，咱们上一篇的骨架把 HCLK 拉到了 64M，这里必须跟上，不然毫秒计数全是错的。就这个的话，我在期待有没有大佬做一个集成的Framework呢。哈哈！

**RCC 的 PythonPeripheral**：官方 F1 的时钟控制器没有行为模型，只有预置的寄存器标签，固件想把时钟切到 PLL，读回的状态位永远是 0，等来的是 `HAL_TIMEOUT`；这份用一段 Python 脚本把握手协议演真（就绪位跟随使能位），最后还有位带别名区——Cortex-M3 给外设寄存器的每一位发了别名地址，HAL 改 RCC 开关用的就是这种写法，不映射这块区域，那些写会**静默丢失**，不报错，就是没生效。

您翻原文件会发现每一行注释都在讲为什么。这份 repl 本身就是教材，以后按键站加按键、UART 站看波特率，我们都会再次回到这个文件，做一个沉思者，构思我们如何让他越发的像一块bluepill。

## 好，数字在变，就是它真的在闪？

重头戏来了。咱们盯着 watch 那几行十六进制说"灯在闪"，一个较真的工程师应该当场拍桌子：**你就看了几个数字，凭什么说它在闪？** 问得好（意义不明的鼓掌），回答。

### 试验一：采样窗口要塞得进跳变

watch 每次读一个瞬时值，相邻两次之间灯翻转了没有，咱们其实不知道。可靠的做法是拿**虚拟时间**开采样窗：`pause` 之后用 `RunFor` 把机器精确推进 250 毫秒再读一次，一拍是一拍。固件半周期 500 毫秒，采样间隔 250 毫秒小于半周期，连读八次的真输出：

```text
0x00000000  0x00000000  0x00002000  0x00002000
0x00000000  0x00000000  0x00002000  0x00002000
```

两个值成对出现、都在滚。结论这就可以下了吗？咱们先别急，看试验二。

### 试验二：假静止，混叠的坑

这回咱们换个采样间隔：**整整 1 秒**，正好一个周期，连采五次的真输出：

```text
0x00002000  0x00002000  0x00002000  0x00002000  0x00002000
```

常值！五秒里这灯一刻没停地翻，采样读数却纹丝不动。每次采样都落在周期的同一个相位上，信号在动、采样点不动，这个现象叫**混叠**（aliasing）。光看这五行，您会斩钉截铁地断言"灯没闪"——这就是观测手段本身在撒谎。它不是模拟器特产：逻辑分析仪采样率不够、示波器时基设错，真机上一样给您演这出。

### 试验三：用两个钟互相对拍

还有更阴的怀疑：万一翻转的"500 毫秒"本身就是假的呢？咱们拉 HAL 的毫秒计数器 `uwTick`（SysTick 中断每毫秒加一）来对拍：让机器先精确跑 1.1 秒虚拟时间，再读它的值。`uwTick` 在这份固件里住在 `0x2000000C`（拿 `arm-none-eabi-nm` 一查便知），真输出：

```text
0x00000452
```

咱们把数字读出来：`0x452` 是 1106。虚拟时间推进 1100 毫秒，毫秒计数器走了 1106，多出来的一点点是脚本 `pause` 之前机器先跑的一小段。两个独立的钟对上了，"半周期 500 毫秒"这个纸面值这才落了地。

## GUI 给人看，以及一个笔者的翻车现场

无头模式输出全在终端里，适合脚本和 CI；咱们想亲眼看见灯，就从仓库根起 GUI：

```bash
renode examples/01_blinky/renode.resc
```

WSL2 用户理论上 WSLg 会直接把窗口送到 Windows 桌面。**理论上**。笔者在这里翻过一辆真车：敲完命令，终端静悄悄，窗口死活不弹。排查过程给您摆出来，WSL2 玩家迟早撞上：

```text
$ echo $DISPLAY
localhost:0
```

WSLg 正常的值应该是 `:0`。`localhost:0` 意思是"通过 TCP 连 Windows 侧的 X server"——那是笔者多年前配 VcXsrv 留在 `.zshrc` 里的遗产，而 VcXsrv 压根没在跑。验证也简单：`/tmp/.X11-unix/X0` 这个 socket 在，说明 WSLg 的 X server 活着；TCP 6000 端口没人监听，说明旧配置指的路是断的。临时的解法是当场 `export DISPLAY=:0` 再跑，窗口立刻出来；永久的解法是把 `.zshrc` 里那行遗产删掉或改成 `:0`。一行环境变量，坑掉笔者一整晚的窗口，这个故事告诉咱们：**排查环境问题，从"显示环境到底是什么"开始查，别从"软件是不是坏了"开始。**

## 舒一口气，咱们下面干活咯~

下一站，欢迎我们的LED！

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Renode Project"
    title="Renode Documentation — Introduction"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/"
    chapter="系统级模拟器定位与功能级边界"
  />
  <ReferenceItem
    :id="2"
    author="Renode Project"
    title="Installing Renode"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/introduction/installing.html"
    chapter="官方安装文档(.deb 包与 .NET 依赖)"
  />
</ReferenceCard>
