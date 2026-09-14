---
title: "调试：从隔着玻璃采样，到停下来看现场"
description: "采样判据看的是结果，这一篇把程序停下来看过程：blinky_gdb 一条龙给 Renode 开 GDB 服务，VSCode 装 Cortex-Debug 连上 3333，F5 之后断点、变量、调用栈全在面板里——停在 HAL_Delay 时 Delay=500 就摆在参数格里，F10 走过 373 行 tickstart 从栈上残留 2 变 0，Watch 里 uwTick 两次命中之间从 0 走到 501；行号断点依赖 -g，引出 Release(5500)/Debug(7536) 两副面孔和 Debug 序言的栈格子解剖；断点的物理课讲 FPB 六个指令比较器与 Renode 实测八个断点全过的边界；全程配真截图与全流程实录视频"
chapter: 0
order: 5
tags:
  - stm32f1
  - beginner
  - 入门
  - 调试
  - renode
  - 工具链
difficulty: beginner
platform: stm32f1
reading_time_minutes: 10
related:
  - "第一个自己的固件:往库里加 target"
  - "Renode 观测课:没有板子,谁说了算"
  - "工作环境:您装的那四样东西,到底是什么"
---

# "灯要是哪天不闪了呢？"

问得好。到目前为止咱们的观测手段是采样：250 毫秒读一次 GPIOC_ODR，靠读数序列判断灯在不在闪。04 篇您改了一行 `HAL_Delay(500)`，采样序列整个变形——但请注意，那是在**程序没病**的时候。真到灯不闪了、闪得不对劲了、程序干脆卡死的时候，采样只能告诉您"结果不对"，说不出"哪一步不对"。您知道 0x4001100C 读出来恒等于 0x00002000，然后呢？是时钟没起来？是 GPIO 配错了模式？还是主循环压根没跑到？隔着采样这层玻璃，一个都答不了。

新手这时候的本能都一样：加打印。在可疑的地方塞一句 printf，把变量吐出来看。这个本能不算错，但它在单片机上有三个实打实的麻烦：打印要占一个串口外设，板子上的串口就那几个，调试完了还得拆；打印本身要花时间、占空间，时序敏感的代码可能因为多了一行输出就换了性格；最气人的是有些 bug 只在特定时序下发作，打印一加、时序一变，bug 消失了——您改的不是程序，是病灶。想要不惊动程序就看清它肚子里的事，咱们得换家伙：把程序**停下来**，当场看变量、看调用栈、看寄存器。这一篇就干这个。

## GDB 够不着您的程序

在电脑上调普通程序，您按 F5 就能打断点，是因为调试器（GDB 这类）和被调试的程序住在同一台机器上，操作系统给了它一套控制接口，停进程、读内存，直接伸手就够得着。固件不行：它跑在"别的地方"，在咱们这儿是 Renode 进程里的虚拟 CPU，在真板子上是一块独立的芯片。GDB 伸不过去，中间需要一个翻译：**GDB Server**。它监听一个 TCP 端口，把 GDB 发来的"第 373 行停一下""把变量 tickstart 给我"翻译成对目标 CPU 的操作。真板子路线上这个翻译由 OpenOCD 加调试探针担着（有板子的朋友 06 篇见）；模拟器路线上 Renode 自己就内置了一个，一行命令的事<RefLink :id="1" preview="Renode official documentation, Debugging with GDB" />。两条路的链条画在一起看：

![GDB 调试的两条链路：模拟器路线只有三环、Renode 内置服务；真机路线多出探针和真芯片两环](./05-gdb-topology.drawio)

还有一件事得先想明白：GDB 吃的是 **ELF，不是 bin**。03 篇咱们用 `file` 看过这俩的区别——ELF 是带地址、带符号、带调试信息的完整档案，bin 是剥得只剩指令和数据的裸二进制。GDB 要把机器码对回您的源代码，靠的就是 ELF 里那些额外的东西。喂它 bin，它连 `HAL_Delay` 是谁都查不到。

## 一条龙：把调试服务起起来

模拟器这头，libestdx 给咱们备好了一个 target，一行命令：

```bash
cmake --build build-debug --target blinky_gdb
```

咱们把它干的活拆开看，是 `examples/01_blinky/renode_gdb.resc` 里的五行剧本：建机器、装 bluepill 板级户口、加载固件（02 篇逐行认过），真正的新面孔只有最后一句 `machine StartGdbServer 3333`——给机器开一个 GDB 服务，等电脑这头来连。终端保持开着别关：`--console` 模式下 Renode 跟着终端的 stdin 活，想塞进脚本里自动化跑的话，重定向了 stdin 它加载完就自己退，日志里看不出半点异常。真跑起来是这样：

![blinky_gdb 一条龙的真终端：Renode 加载固件、SVD 就位、GDB 服务在 3333 待命](./server_startup.png)

日志里那行 `Loading block of 7536 bytes length`——7536 正是 Debug 构建的 text，加载的是哪份固件，字节数自己会说话，咱们看一眼就能确认口径没拿错。这个 target 还有个聪明的地方：CMake 用生成器表达式把**当前 build 目录的** ELF 喂给 renode，您从 build-debug 调它加载的就是 Debug 固件、从 build 调就是 Release，调试器跟模拟器各拿各的 ELF 这种幽灵没有出生的机会。

这个 target 的出生还有一段坎坷。它要把 renode 的变量 `$bin` 写进 CMake 的 COMMAND 里，而 ninja 的构建文件里 `$name` 恰恰是 ninja 自己的变量语法——CMake 没转义就把命令传了过去，`$bin` 被 ninja 展开成空串，传给 renode 的成了 `=@/path`，全程没有一声报错，只有 renode 莫名其妙不认参数。给 `add_custom_target` 添上 `VERBATIM` 关键字，CMake 才把 `$` 老老实实转义成 `$$`。您以后在 COMMAND 里写字面 `$` 的活，`VERBATIM` 记得备上。

## 把 VSCode 连上去

电脑这头，咱们给 VSCode 装上 Cortex-Debug 扩展（作者 marus25）就齐了。它只管调试，不碰智能感知——编辑器那条线是 clangd 的主场（07 篇见），互不打架。连接配置是仓库根的 `.vscode/launch.json`（`.vscode` 在 gitignore 里，纯本地配置，不进仓库）：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Renode blinky (外接 GDB Server)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "external",
            "gdbTarget": "localhost:3333",
            "executable": "${workspaceFolder}/third_party/libestdx/build-debug/examples/01_blinky/blinky",
            "gdbPath": "/usr/sbin/arm-none-eabi-gdb",
            "runToEntryPoint": "main"
        }
    ]
}
```

咱们认三个关键字段：`servertype: "external"` 告诉 Cortex-Debug"GDB Server 我自己管着呢，你别另起"；`gdbTarget` 就是 Renode 那个 3333 端口；`executable` 指向 Debug 构建的 ELF——为什么必须是它，下面构建口径那一节专门说。

然后您按 F5。头一回连上，DEBUG CONSOLE 里大概会刷两行怪话：

```text
Program stopped, probably due to a reset and/or halt issued by debugger
⚠️ warning: Invalid state, unable to determine sp alias, assuming msp.
```

都不是病。前一行是 Cortex-Debug 的口头禅，目标一停它就猜这么一句；后一行是 gdb 在函数入口分不清主栈/进程栈时的老毛病，裸机 main 走的就是 msp（主栈），它猜得对。您真正会看到的画面长这样：

![VSCode 调试会话：断点停在 main.cpp，变量、监视、调用栈面板全在](./vscode-debug.png)

程序停在 main 入口，之后就是面板的天下：行号旁点一下下断点，F5 继续、F10 单步、F11 步入、Shift+F11 步出，鼠标悬停就能看变量，左侧调用栈面板把谁调谁列得清清楚楚。底下那根线没变过——调试器连着 Renode 的 GDB 服务，只是命令一个都不用您敲。整个流程笔者录了一份实录，从起服务到按按钮一镜到底：

<video controls muted src="./debug-session.mp4" width="640"></video>

## 停在断点上，现场随便看

拿主循环那句 `HAL_Delay(500)` 练手：在 `main.cpp` 里那行调用旁点一个断点，F5 命中后按 F11 步入，编辑器跳进 HAL 源码，变量面板立刻给您看家底——参数 `Delay=500`，您在 `main.cpp` 里写的那个 500，一路上到了这里；调用栈面板里 `main () at main.cpp:41`，调用点精确到行。程序卡死的时候这两块面板往往就是破案的第一现场：停在哪个函数、被谁调的，一目了然。

变量面板的底气从哪来？咱们把这份 Debug 固件里 `HAL_Delay` 开头几条真指令解剖出来（`arm-none-eabi-objdump` 的真输出）：

```text
080004cc <HAL_Delay>:
 80004cc:  b580       push {r7, lr}
 80004ce:  b084       sub  sp, #16
 80004d0:  af00       add  r7, sp, #0
 80004d2:  6078       str  r0, [r7, #4]             ← 参数 Delay 存进栈格子
 80004d4:  f7ff ffb4  bl   8000440 <HAL_GetTick>    ← 断点落在这一条(373 行头一句)
 80004d8:  60b8       str  r0, [r7, #8]             ← tickstart 存进另一格
```

一条条看：`push` 把帧指针 r7 和**返回地址 lr** 一起压栈，调试器能列出"是谁调进来的"，靠的就是栈里这份返回地址；`sub sp, #16` 给局部变量开出 16 字节的栈格子；`str r0, [r7, #4]` 把参数 Delay 摆进自己的格子——所以断点一停，面板就能报出 `Delay=500`，那不是猜的，是从这格栈里读的。断点本身落在 373 行的第一条指令 `bl` 上：栈帧已经搭好、参数已经归位，您停下的瞬间看到的就是一个完整的现场。这也顺带解释了 Debug 构建 7536 比 Release 的 5500 大两千字节的另一层来由：每个变量都要有自己的格子、每次读写都是实打实的访存指令，不开优化就是这个排场。

按 F10 单步走过 373 行，还有个细节值得咂摸：刚停下时 `tickstart` 显示 2，走完这行变成 0。2 不是它的值，是栈格子里残留的旧数据——断点停在这一行，**这一行还没执行**；F10 之后 `bl HAL_GetTick` 的返回值 0 才写进格子。为什么是 0？`HAL_GetTick()` 读的是全局节拍 `uwTick`（02 篇对拍过的老熟人），从复位到主循环第一次延时，仿真时间还没走过 1 毫秒，SysTick 一次都没响过。您在 Watch 面板里加一个 `uwTick`，每按一次 F5 命中一次断点，看它 0→501→1001 地涨：两次 `HAL_Delay(500)` 之间正好五百个节拍的等待，加上主循环翻转 GPIO 的那点开销。`HAL_Delay` 的工作机制（死等 `uwTick` 追上起点加 500）就这样在变量现场里现了形，一行汇编不用读。这条时间线画出来给您看：

![uwTick 时间线：两次 HAL_Delay 之间，宽格子是 500 拍的轮询等待，窄格子是 1 拍上下的循环开销](./05-uwtick-timeline.drawio)

## 两副面孔：构建口径

回头说 launch.json 里那个 `executable`：为什么死咬着 build-debug 不放？行号断点要靠 ELF 里的 `-g` 调试信息：变量表、行号表，调试器把"这格栈是哪个变量""这条指令对应源码第几行"对上号，全靠它。而咱们日常的默认构建（04 篇起的那套）按 Release 走，全开优化、不带 `-g`：ELF 里有函数符号，往函数上打断点还使得；变量和行号干脆没有。您要是拿默认构建的固件连进来，行号断点找不到落点、变量面板空空如也——这不是操作错了，是那份 ELF 里压根没记这些。

所以调试的时候咱们用 Debug 口径，另起一个 build 目录，两边互不干扰：

```bash
cmake -B build-debug -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --target blinky
```

构建尾部的真输出：

```text
   text    data     bss     dec     hex filename
   7536      12       4    7552    1d80 .../build-debug/examples/01_blinky/blinky
```

7536 对 5500，多出来的两千字节就是不开优化的代价；00 篇四份固件上秤称过同一件事，咱们在这里又见了一面。还有一处不同值得记下：函数的地址排布两个口径不一样，Release 版 `HAL_Delay` 在 `0x08000350`，Debug 版在 `0x080004cc`。**讨论地址、贴输出，永远先说清楚是哪份构建**——两份 ELF 混着看，看到的现象全是在骗您。您在断点里看到的超长文件路径也是 `-g` 干的：编译时机器上的绝对路径被它原样记进 ELF，所以每个人看到的都是自己机器的样子，不用奇怪。

什么时候用哪副面孔，现在可以说清楚了：

| | 默认（Release） | Debug |
|---|---|---|
| text | 5500 | 7536 |
| 调用栈 | 有函数名，无行号 | 函数名 + 文件:行号 |
| 变量 | 无 | 参数、局部变量全在 |
| 用途 | 日常构建、体积对照 | 抓虫、单步、看现场 |

⚠️ 您抓完虫记得切回默认口径再上秤，拿 Debug 版的 7536 去谈零开销，是给自己挖坑。

## 断点本身也有物理课

断点不是无限的。真 Cortex-M3 芯片里管断点的硬件叫 FPB（Flash Patch and Breakpoint），规格是六个指令比较器加两个字面量比较器<RefLink :id="2" preview="ARM Cortex-M3 TRM (DDI 0337), FPB" />。机器码从 Flash 里取出来的时候，六个比较器各自盯一个地址，取指地址对上就触发停机。这是物理稀缺：真机上 Flash 断点最多六个，下第七个要么报错要么静默失效。另一类叫软件断点，原理是把断点处那条指令当场改写成一条 BKPT 停机指令——但 Flash 是写之前必须先擦的，跑在 Flash 里的代码改不动，所以咱们的固件在真机上走的全是硬件通道。习惯上断点用完就删，别攒。

那 Renode 呢？笔者一口气下了八个断点，从 `HAL_Delay` 到 `HAL_RCC_ClockConfig`，八个全部生效，一个没被拒绝。原因和 02 篇讲的功能级边界是同一件事：Renode 是功能级模拟，断点由仿真器自己实现，不占用 FPB 这块硬件，模拟器里没有这个限制。这算模拟器给的便利，但习惯别在这养成：断点攒成八个的肌肉记忆带上真机，第七个就开始装聋作哑，您还得当它是玄学。您把两边摆在一起看：

![FPB 六个指令比较器与第七个断点撞墙，对照 Renode 仿真器拦截没有数量限制](./05-fpb-breakpoints.drawio)

## 排错速查

连不上 3333，先看 Renode 活着没：那两行 "GDB server ... started on port :3333" 在不在日志里；再看端口被谁占着，`ss -tln | grep 3333` 一查便知。断点下了不命中，先核对调试器加载的 ELF 和机器里跑的是不是同一份：04 篇那个"跑的还是别人的固件"的幽灵，在调试器里同样存在；再考虑口径：Release 构建里函数可能被优化器揉进调用者，行号断点找不到落点，换 Debug 口径再试。至于 F5 按下去毫无动静、调试器报 "Cannot execute this command while the target running" 这类话，是仿真压根没开起来：这台 Renode（1.17）默认 GDB 一连上就自动开跑，但 `machine StartGdbServer` 有个 autostart 参数，笔者把它显式掰成 `false` 试过，连上时 CPU 停在 `0x00000000`，等到来生也等不来断点——遇到这种情况，在 Renode 终端里敲一句 `start` 立竿见影，这一句顶多白敲，不敲就要赌版本默认行为。

到这儿，咱们手里的观测家伙就全了：采样判据看结果，断点调试看过程，两副构建口径按需切换。模拟器这套练熟了，真板子上的流程结构一模一样，只是 GDB Server 那个位置换成 OpenOCD 加调试探针。有板子的朋友，06 篇见；没板子的也不亏，接下来 07 篇咱们先让编辑器看懂这套交叉编译的代码，跳转补全一条龙。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Renode Project"
    title="Debugging with GDB"
    :year="2026"
    url="https://renode.readthedocs.io/en/latest/debugging/gdb.html"
    chapter="StartGdbServer 与 autostartEmulation 选项"
  />
  <ReferenceItem
    :id="2"
    author="ARM"
    title="Cortex-M3 Technical Reference Manual — About the Flash Patch and Breakpoint Unit"
    :year="2026"
    url="https://developer.arm.com/documentation/ddi0337/h/debug/about-the-flash-patch-and-breakpoint-unit--fpb-"
    chapter="FPB:六个指令比较器与两个字面量比较器"
  />
</ReferenceCard>
