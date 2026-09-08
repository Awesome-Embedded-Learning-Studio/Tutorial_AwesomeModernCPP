---
title: "工程搭建:从空仓库到 Renode 的第一行输出"
description: "不 clone 参考仓库,在您自己的目录里从 git init 亲手复现整套工程(参考答案锚定 6ee1d25 与 fe5a0e8):13 行的仓库地基,submodule 把 ST 官方 SDK 锁在精确 SHA;工具链文件用生成器表达式只对 C++ 关异常关 RTTI;链接器禁令四连用 PROVIDE+ASSERT 写成 ld 报错,实测往 main 里塞一个 new 就报 heap new banned;零汇编启动:section 属性写向量表、槽 0 放 _estack 当初始 SP、SVC/PendSV 留空座;reset_handler 用 uintptr_t 比地址防 GCC 把指针比较优化成死循环;Renode 侧 bluepill.repl 头注释是一份保真度坦白书,run-renode.cmake 绕开构建器 shell 吃变量的坑;验收 = 串口窗打出 banner 加每秒一条 tick,固件体积 536B Flash / 8B RAM"
chapter: 0
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 工具链
  - 链接器
  - CMake
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 25
prerequisites:
  - "为什么是 RTOS·第 1 篇:从超级循环到 RTOS"
  - "嵌入式·STM32F103 + Renode 起步站:工具链装好、Renode 会跑"
related:
  - "从超级循环到 RTOS:为什么需要,怎么验证"
---

# 工程搭建:从空仓库到 Renode 的第一行输出

好了，现在请您到一个空白地方，哦，Windows的话还请看看 WSL 的搭建。我们目前不支持 Windows 下继续我们的代码手搓的学习（尽快支持）。开始 `mkdir` 独属于你的文件夹。比如说笔者周五早上 7:29分的时候就 `mkdir ~/ZerOS/` 然后 `code ~/ZerOS` 了。也麻烦请您这样做。取一个你喜欢的名字，anyway。

`git init`，请。我记得我的一些好兄弟管理代码是基于压缩包的，基于 QQ or 微信通讯协议的分布式分发。咱们别这样了，如果您不会，Deepseek 老师很乐意帮助您，或者到咱们的[EmbedBox](https://github.com/Awesome-Embedded-Learning-Studio/EmbedBox)找找Git教程。


敲完了？吐出来这个仓库已经被 git 初始化之后，写代码。。。？笔者说不着急。还请你猛回头的先把第三方仓库配置好。**就像环境配置从来是我们征程的第一步，配置初始依赖，也是这样的。**

STM32在Github上维护了一份依赖。您要是是正点 or 普中等等打过来的，可能还是受不了不使用压缩包管理源码技术的话，还请早日适应——

```shell
cd ~/ZerOS      # 就是刚才 mkdir 的那个，名字随您
git init        # 初始化仓库刚刚做了
git submodule add https://github.com/STMicroelectronics/STM32CubeF1.git third_party/STM32CubeF1
```

回来，第三行的意思是——添加STM32的标准库作为我们项目的一个子模块，笔者看了一下大小，131MB，可能需要您的代理比较好。不太好也没关系，伸个懒腰起来喝茶也是一件美事。如果完事了，可以执行下面的指令：

```shell
# 下面这一行的意思是——把咱们的子模块咔哒盯住在bb2016e
# 效果等价于神秘厂商提供的压缩包一样，防止上游已更新
# 我们代码起飞爆笑如雷的问题
git -C third_party/STM32CubeF1 checkout bb2016e

# 然后把这个子节点，加入到我们的Git托管商，我们现在把依赖管理起来了
# 用一种独特的方式
git add third_party/STM32CubeF1
```

诶？为什么第一步是干这个？您想啊，后面咱们马上要写的启动代码、UART 驱动，每一个寄存器名（`RCC->APB2ENR`、`USART1->DR`）、每一个位段宏，全部来自 CMSIS 设备头——头文件就是寄存器的真理源。它要是漂了，文章里讲的位段跟您手里的树对不上，咱们后面每一站的验收都无从谈起。

那 131MB 的代价，参考答案后来用 sparse-checkout 压到了 13MB，那是后面站的事，咱们先全量拉。这个算Git的优化操作了

## 工具链文件:把"这是给 ARM 的"集中写在一个文件里

下一步，是独属于我们交叉编译的经典一步，各位编译过ARM32的Qt没有过？如果有那太棒了，咱们这里也是一样的。简单的说，是使用工具链文件完成这个功能！

没编过 Qt 也不要紧，咱们把这事从头说一遍。

您的电脑是 x86_64 的，但是咱们的板子上的芯片是 ARMv7M Cortex-M3，两边指令集根本不通，所以在电脑上生产"给板子跑的机器码"，这个活叫交叉编译，干活的编译器就是 `arm-none-eabi-gcc`。

这个名字念开就是自我介绍：arm，说的是目标架构是arm；none，不指望任何操作系统接盘，生成无syscall的代码；eabi，嵌入式那套二进制接口。它跑在您的电脑上，吐出来的码只认 Cortex-M。直接执行那自然是CPU一脸懵逼，送你一个`Exec Format Error`

话又说回来了，CMake咋知道这回该请哪套编译器出场？毕竟笔者写ZerOS吹牛逼的时候，说了我们会上位机Mock验证，说明啥，说明我们的代码还是被上位机所接受的对吧！

嗯，就是靠靠工具链文件。它就是一个普普通通的 CMake 脚本，赶在工程开工之前把"用谁编、编给谁"交代清楚。Just it。

CMake 是第一次配置就把编译器定死进缓存，这事必须提前说，而且值得单独成一个文件：同一份 CMakeLists，配 host 编译器出来的是桌面单测，配上这个文件出来的就是板子固件。之后咔咔target flash就走到了板子上，之后再说~。

咱们新建 `cmake/arch/arm-none-eabi.cmake`，内容全部如下，您照着敲：

```cmake
# 咱们是啥系统，高情商是Generic，低情商就是不重要你别管
set(CMAKE_SYSTEM_NAME Generic)
# 咱们的处理器架构是啥，ARM呗
set(CMAKE_SYSTEM_PROCESSOR arm)

# 这里是指定一下我们特供的编译器，不是别的，就是我们自己搞的
# arm-none-eabi- 簇编译器
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# 裸机目标没有 main(),编译器自检只编静态库
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 下面三行管 find_xxx() 的搜索范围:交叉编译之后,搜什么得先说清认哪个世界
# 找程序:构建期要在您电脑上跑的工具,照常搜主机,别往 ARM 目录里翻
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# 找库、找头文件:只认工具链这一侧,主机的 /usr/lib、/usr/include 一律挡在门外
# 防的是哪天某个依赖用 find_library/find_path,把 x86 的东西混进 Cortex-M 的固件
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# 目标侧编译基线
# 使用Thumb指令集，因为这样代码出来小巧可爱
# -Os是做体积上的优化
# -g留着，这是调试的时候给咱们看情况用的
# 剩下的就是禁用RTTI和异常，因为我们也会有C语言文件参加，所以的话套一层
# $<COMPILE_LANGUAGE:CXX>:
add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -ffreestanding
    -Os
    -g
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
```

几处都有讲究。`CMAKE_SYSTEM_NAME Generic` 说的是"没有操作系统"这个事实;`TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` 是裸机的老朋友,CMake 自检默认要链接一个可执行文件,裸机没有 `main`,不这么写配置阶段就报错,您之前搭裸机工程就见过它。还有那三行 `CMAKE_FIND_ROOT_PATH_MODE_*`,机制写在代码注释里了,咱们再补一句实话:这一站的构建全是显式路径,还没人调 find_xxx,这三行眼下是纯防御,等后面测试和依赖真的进场,它们才开始干活。

真正值得咱们多看一眼的是最后两行生成器表达式:`-fno-exceptions` 和 `-fno-rtti` 只对 C++ 编译单元生效。板包里那个 `system_stm32f1xx.c` 是 C 文件,ST 官方代码,原样内置,不该被 C++ 的禁令波及。

您可能要问,这两个 flags 为什么挂工具链文件、不挂根 CMakeLists?根上挂的是所有目标共享的东西:

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra)
```

C++23 基线、通用警告,仅此而已。架构相关的 flags,咱们一条都不往根里放:这套代码后面要在 host 上跑单测,host 侧要开着异常跑 Catch2,根上要是沾了 `-fno-exceptions`,后路就断了。"目标侧归工具链文件,跨目标通用归根"，我想这样的分层，代码干净一些！

到配置的时候,咱们把它指给 CMake,一句话的事:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
```

## 链接器禁令:把宪法写成报错信息

无堆、无异常、无 RTTI。笔者的宣传Banner就是这样说的。但是口头约定没用,C语言说要信任程序员，但是大家一般都会疏忽。所以的话，还是邀请工具防一下。比如说得让我们在链接器的脚本，也就是`link.ld` 加上开头四行:

```ld
/* 链接期反制:谁引入这些符号谁链接报错 */
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));
```

> 啥是链接器脚本，我咋没写过？
>
> 问得好，您确实没写过，也确实不需要写过：在电脑上写程序，背后有操作系统和一套默认的链接规则兜底，代码放哪、全局变量放哪、程序从哪进来，全是别人定好的。gcc 肚子里其实一直揣着一份三百来行的默认脚本，不信您跑一把 `arm-none-eabi-ld --verbose`，开头一句 `using internal linker script:`，哗啦吐出来一大坨就是它——您没写过，不代表它没在干活，只是有人替您写了。裸机上这层服务没了：芯片上电只会从固定地址取指令，Flash 在 `0x08000000`、SRAM 在 `0x20000000`，代码住哪、变量住哪、边界划在哪，机器一概不知，得您亲口告诉链接器。这份"住址分配说明"就是链接器脚本，`ld` 的一门小语言，咱们的 `link.ld` 通篇就干这一件事。


好，看看就是——`PROVIDE` 的语义咱们拆开看:这个符号只有"被引用、且没有任何目标文件定义它"时,链接器才去求值右边的表达式。而右边是 `ASSERT(0, ...)`,求值必炸,炸出来的就是引号里那句话。也就是说:没人用堆,这四行安静得像不存在;谁敢 `new`,链接器当场报 `heap new banned`。`_Znwj` 和 `_Znwm` 分别是 32 位和 64 位 `size_t` 下 `operator new` 的修饰名,两条都堵上,哪个平台的编译器来都一样。这就是防呆措施啊兄弟们！

真跑给咱们看。往 `main()` 里塞一行 `{ auto p = new int(5); (void)p; }`,重新构建:

```text
/usr/arm-none-eabi/bin/ld: heap new banned
collect2: error: ld returned 1 exit status
```

咱们拿到的报错就是那句话,没有堆栈没有行号,但方向明确:去查谁用了堆。

> 两个细节,您别放过。其一,`PROVIDE` 挂 `ASSERT` 的写法必须放在 `SECTIONS` 外面:ld 手册明说,section 定义内部 `PROVIDE` 的符号在断言求值时机上不可靠,顶层才稳,ZerOS 这四行全在顶层。
>
> 其二,是笔者实测的一个盲点。全局对象的构造析构代码住在 `.init_array` 节里,而这时的链接脚本还没有 `.init_array` 输出节,`--gc-sections` 会把没人引用的静态初始化整段回收:笔者塞了个带析构的全局对象进去试,链接居然是绿的,对象从未构造,一声不吭。这个洞要到讲内存的那一站才被 `KEEP(.init_array)` 加一条断言堵上。另外,ARM EABI 工具链上析构注册走的是 `__aeabi_atexit`,`__cxa_atexit` 那条禁令在这条工具链上碰不到它;真写出函数局部 `static` 带析构,炸出来的是 `undefined reference to '__aeabi_atexit'`,报错不如禁令那句友好,但结论一样:链接过不去。

禁令之外,`link.ld` 的正文管内存布局:64K Flash / 20K SRAM 的边界写进 `MEMORY`,各段怎么摆、`_sidata`、`_estack` 这些符号从哪来,全在 `SECTIONS` 里。整份贴出,您照着敲,每一段是干什么的,下面讲启动代码时逐一对上:

```ld
/* stm32f103_bluepill 链接脚本 —— C8T6: 64K Flash / 20K SRAM
 * 内存约束即真机约束(Renode 模型 sim/renode/bluepill.repl 同尺寸,越界即报)。
 * P1:链接脚本符号在代码里必须按整数地址比较,禁止指针比较。 */

ENTRY(reset_handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* 链接期反制:谁引入这些符号谁链接报错 */
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    /* 初始化数据:LMA 在 Flash,启动时拷贝到 RAM */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ :
    {
        *(.comment)
    }

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

## 哈？你真这样写Startup啊，又寸

按惯例这一步该写 `startup.s`;ZerOS 偏不,启动文件是 `startup.cpp`,咱们看到的向量表就是一个带属性的 C++ 数组:

```cpp
using Isr = void (*)();

// (section(".isr_vector"), used) 是一个小trick，告诉编译器
// 大哥您请把下面的全局数组，放到.isr_vector段，因为ld要用
// 此外，因为他是没有被直接引用的，所以，放一个used，防止链接的时候
// 一脚踹死我们的复位向量。
__attribute__((section(".isr_vector"), used))
const Isr vectors[] = {
    reinterpret_cast<Isr>(&_estack),             // 0  初始 SP
    &reset_handler,                              // 1  Reset
    &fault_handler,                              // 2  NMI
    &fault_handler,                              // 3  HardFault
    &fault_handler,                              // 4  MemManage
    &fault_handler,                              // 5  BusFault
    &fault_handler,                              // 6  UsageFault
    nullptr,                                     // 7-10 保留
    nullptr,
    nullptr,
    nullptr,
    nullptr,                                     // 11-14 (SVC/PendSV 归内核 port)
    nullptr,
    nullptr,
    nullptr,
    &SysTick_Handler,                            // 15 SysTick(应用定义)
};
```

咱们从三个属性看起:`section(".isr_vector")` 让编译器把数组单独放节,`link.ld` 里 `KEEP(*(.isr_vector))` 接住它,`used` 属性防着 `--gc-sections` 把"没人调用"的表掏掉。数组下标就是硬件槽位号:芯片上电,硬件自己从地址 `0x0` 读走槽 0 当初始 SP,从 `0x4` 读走槽 1 当 Reset 入口,CPU 上电后头两步跳到哪,全由这张表说了算,布局是 ARMv7-M 架构定死的:11 号槽 SVCall、14 号 PendSV、15 号 SysTick。

槽 0 最特殊:它放的是 `_estack`,RAM 的顶,压根没有函数可指。`reinterpret_cast<Isr>(&_estack)` 把一个数据地址铸进函数指针槽位,抽象机视角这是越界,裸机上这是 Cortex-M 的标准姿势,硬件只管从这里取 8 个字节当栈指针用,不在乎您类型怎么写。

您可能想问:NMI、HardFault 这些全指向 `fault_handler`,就这?就这。它打印一行 `[FAULT] halted` 然后停机,是个最小兜底;完整现场捕获是后面内核 HardFault 模块的活。还有 11 到 14 那四个 `nullptr`:SVC 和 PendSV 归内核 port,调度器要到第三站才出生,但座位从第一份向量表起就留好了,注释写得分明。

向量表之外,启动文件的骨架您也别漏:文件头三行 include(`<cstdint>`、`stm32f1xx.h`、`uart.hpp`),外加一个 `extern "C"` 块,装着链接符号声明、`main` 与 `SysTick_Handler` 的前置声明,还有兜底的 `fault_handler` 本体。`extern "C"` 不是装饰,少了它,`reset_handler` 会被 C++ 名字修饰改成别的名字,链接脚本的 `ENTRY(reset_handler)` 当场找不到人:

```cpp
extern "C" {
// 链接脚本符号(P1:只按整数地址比较,禁止指针比较)
extern std::uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main();
void SysTick_Handler();

// 最小兜底:打印一行后停机;完整现场捕获属内核 HardFault 模块(roadmap 3.1)
void fault_handler() {
    ZerOS::board::print("\r\n[FAULT] halted\r\n");
    for (;;) {
        __WFI();
    }
}
} // extern "C"
```

这张表摆得对不对,机器说了算。等会儿验收跑 Renode 时,日志开头那行 `Setting initial values` 会把 SP 和 PC 的初值给呱呱念出来,咱们到时候回到这张表上对:SP 该是 `_estack`,PC 该是 `reset_handler`。先按下,把启动代码写完。

## reset_handler:两段循环,一个坑

```cpp
void reset_handler() {
    // 为什么是uintptr_t？因为uint32_t告诉编译器这个变量是数字
    // 会做出一些失控的优化
    auto src = reinterpret_cast<std::uintptr_t>(&_sidata);
    auto dst = reinterpret_cast<std::uintptr_t>(&_sdata);
    const auto dend = reinterpret_cast<std::uintptr_t>(&_edata);
    for (; dst < dend; dst += 4, src += 4) {
        *reinterpret_cast<std::uint32_t*>(dst) = *reinterpret_cast<std::uint32_t*>(src);
    }
    auto b = reinterpret_cast<std::uintptr_t>(&_sbss);
    const auto bend = reinterpret_cast<std::uintptr_t>(&_ebss);
    for (; b < bend; b += 4) {
        *reinterpret_cast<std::uint32_t*>(b) = 0;
    }

    SystemCoreClock = 72'000'000; // 板上主频;待真时钟树初始化取代
    main();
    for (;;) {
        __WFI();
    }
}
```

上电时 SRAM 里是随机值,得有人把活干起来。`.data` 段的变量有初值,初值存在 Flash 里,链接脚本用 `> RAM AT > FLASH` 把加载地址和运行地址分开,`_sidata = LOADADDR(.data)` 记下 Flash 里的源头,第一段循环把它拷进 RAM;`.bss` 段的变量没初值,第二段循环清零。然后是 `main()`,返回了就 `__WFI()` 睡死,没有操作系统给您兜底返回。

欸！您注意到我在用 `std::uintptr_t` 而非指针。链接脚本符号 `_sbss`、`_ebss` 在代码里声明成两个独立的 `uint32_t` 对象,要是不小心写成指针比较 `_sbss < _ebss`,标准眼里这俩是互不相干的对象,比大小是未定义行为,GCC 的指针来源分析敢据此下结论"循环条件恒真",生成的循环停不下来,一路把 20K RAM 写穿,芯片进 lockup。

还有一处要跟您坦白:常规 STM32 工程的启动会调 `SystemInit()` 配时钟,这里没有,`SystemCoreClock` 直接写死 `72'000'000`,注释明说"待真时钟树初始化取代"。那板包里 408 行的 `system_stm32f1xx.c` 岂不是白带?也不算——它提供的 `SystemCoreClock` 变量本体是全局要用的,真正的时钟树要到性能那一站才立起来。Renode 里这不是问题(SysTick 频率写在平台模型里),真机上 HSI 默认 8MHz, 笔者会在那个时候，在板机的文件上做时钟树初始化

## 轮询 UART:没有 printf 的世界怎么打字

`-nostdlib` 的世界没有 `printf`,咱们想说话就得自己动手。`uart.hpp` 是轮询式 UART1,占用 PA9:

```cpp
inline void uart1_putc(char c) {
    while ((USART1->SR & USART_SR_TXE) == 0) {
    }
    USART1->DR = static_cast<std::uint8_t>(c);
}
```

发送寄存器空了才写下一个字节,傻等,但对"打出第一行 banner"这个目标,傻等刚刚好。打数字比打字符麻烦一点,咱们看 `printdec`:

```cpp
inline void printdec(std::uint32_t n) {
    char buf[11];
    char* p = buf + sizeof(buf);
    *--p = '\0';
    do {
        *--p = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n != 0);
    print(p);
}
```

11 字节的栈缓冲,咱们从尾往前填:除 10 取余拿到的是最低位,填完天然正序。这里用 `do-while`、不用开头就判断的 `while`,是因为 `n = 0` 也得打出一个 `'0'`,不然 0 就什么都不打了;`uint32_t` 最多 10 位十进制,加结尾 0,11 字节正好。

初始化和整行打印也一并给您,加上文件头的 `#pragma once`、三个 include 和 `namespace ZerOS::board` 的包裹,`uart.hpp` 就齐了:

```cpp
inline void uart1_init() {
    // RCC 在 Renode 为假值(就绪位恒 1),此行主要服务真机
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

inline void print(const char* s) {
    while (*s != '\0') {
        uart1_putc(*s++);
    }
}
```

`uart1_init` 里那行注释值得您抄下来:"RCC 在 Renode 为假值(就绪位恒 1),此行主要服务真机"。Renode 没有 RCC 的模型,使能时钟那行读写的都是 SVD 假值,这属于仿真器和真机的已知差异,马上您就会看到它的另一半证据。

## Renode 三件套:模型、脚本、启动器

`sim/renode/` 下三个文件,咱们挨个看,它们把"跑仿真"变成正规工程资产。

`bluepill.repl` 是平台模型,派生自 Renode 自带的 `stm32f103.repl`(高密度超集),按 C8T6 实际配置裁剪。这里感谢一下AI，我实在有点不会Renode的神秘脚本语法，他给我留下了如下的AI Slop

```text
// 按 C8T6 实际配置裁剪,让模拟器强制真机约束(越界即报,不再靠链接脚本自觉):
//   Flash 64K @ 0x08000000(上游是 0x0 起 512M 超集窗口,已砍)
//   SRAM  20K @ 0x20000000(上游声明 256M,已砍)
//   D-G 为幽灵口:上游 STM32F1AFIO 构造函数硬性要求恰好 7 个口(源码实证
//   gpioPorts[3..6] 直接索引),不给就崩。真机 C8T6 没有这四个口——
//   固件若访问它们,真机会 BusFault,本模型会静默成功,属已知失真。
//   RCC 无模型,Tag 假值 0x0A020083(就绪位恒 1);定时器频率按上游取 10MHz;
//   DMA 无模型(1.16.1 平台未带;真机 C8T6 只有 DMA1)。
```

`bluepill.resc` 是运行脚本,22 行,一并贴出:

```text
# Blue Pill @ Renode 运行脚本
# 用法(无头,日志落盘):
#   renode --console --disable-xwt -e "logFile @run.log" \
#     -e '$bin=@build/zeros-bluepill.elf' \
#     -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start" -e "sleep 5" -e "quit"
# $bin 在 include 之前设置即可覆盖默认路径;须在仓库根目录下运行。

using sysbus

mach create "bluepill"
machine LoadPlatformDescription @src/board/stm32f103_bluepill/sim/renode/bluepill.repl

$bin?=@build/zeros-bluepill.elf

showAnalyzer usart1

macro reset
"""
    sysbus LoadELF $bin
"""

runMacro $reset
```

两处值得看:`$bin?=@build/zeros-bluepill.elf` 带问号是条件赋值,外部先设了 `$bin` 就不覆盖,CI 想跑别的 ELF 不用改脚本;`showAnalyzer usart1` 把串口窗弹出来,您的 banner 就住这儿。

`run-renode.cmake` 是启动器,存在的原因是一个坑,咱们把现场摆出来:直接在 `add_custom_target` 的 `COMMAND` 里写 `-e "$bin=@..."`,Makefile 生成器会把 `$b` 当单字符变量吃掉,renode 收到的变成 `in=@...`,报 `No such command`。绕法是让目标只调 `cmake -P`,`execute_process` 的参数直达进程 argv,不经过任何构建器的 shell。17 行,也贴给您:

```cmake
# Renode 启动器:被板包的 renode-bluepill 目标以 cmake -P 调用。
# 参数经 execute_process 直达进程 argv,不经构建器 shell——规避 $bin 被 make/ninja 当变量吃掉(见 docs/simulation.md P12)。
# 用法参数:-DRENODE_ROOT=<仓库根> -DRENODE_ELF=<ELF 绝对路径>
#          -DRENODE_RESC=<相对仓库根的 resc 路径> -DRENODE_SECONDS=<运行秒数>

execute_process(
    COMMAND renode --console --disable-xwt
            -e "logFile @${RENODE_ROOT}/run.log"
            -e "\$bin=@${RENODE_ELF}"
            -e "include @${RENODE_RESC}"
            -e "start" -e "sleep ${RENODE_SECONDS}" -e "quit"
    WORKING_DIRECTORY ${RENODE_ROOT}
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "renode exited with ${_rc}")
endif()
```

于是跑仿真就是一条正经构建命令:

```shell
cmake --build build --target renode-bluepill
```

## clangd:为什么我的 `<cstdint>` 全是红线

工程搭完了,您打开编辑器,`#include <cstdint>` 一片红,换谁都怀疑人生。`.vscode/settings.json` 里那三行注释把病理写清楚了:

```json
{
    // clangd 对 arm-none-eabi 目标默认猜 libc++ 布局(c++/v1),本机工具链
    // 实为 libstdc++ 布局(c++/<版本>),不问真编译器就找不到 <cstdint>
    // 等标准头。--query-driver 放行 arm gcc/g++(装在 /usr/sbin),让
    // clangd 探测真实系统头路径。它是进程旗标,进不了 .clangd,只能放这。
    "clangd.arguments": [
        "--query-driver=**/arm-none-eabi-g*"
    ]
}
```

clangd 拿到交叉编译命令后,默认按自己的内置假设猜标准头在哪儿,猜错了,头文件就"找不到"。`--query-driver` 让它放行 arm 编译器、真的去问人家要系统头路径,您加上它,红线全消。这东西是进程旗标,`.clangd` 文件里写不了,只能进 `settings.json`。之前踩过这坑的朋友就当复习,没踩过的,这回直接绕开。

## 验收:让它亮

源文件都齐了,把它们黏进构建的这一份,是板包的 `src/board/stm32f103_bluepill/CMakeLists.txt`,60 行,您贴着敲:

```cmake
# Blue Pill 板包(D14 一板一包):固件目标 + Renode 仿真目标
# 仿真资产在 sim/renode/(bluepill.repl/.resc);相对路径按启动 cwd 解析,须以仓库根为 cwd(P10)

set(CMAKE_EXECUTABLE_SUFFIX .elf)

set(CMSIS ${CMAKE_SOURCE_DIR}/third_party/STM32CubeF1/Drivers/CMSIS)

# ---- 固件目标 ----
add_executable(
    zeros-bluepill
    ${CMAKE_SOURCE_DIR}/example/main.cpp
    startup.cpp
    system_stm32f1xx.c
)
target_include_directories(
    zeros-bluepill
    PRIVATE ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMSIS}/Include
            ${CMSIS}/Core/Include
            ${CMSIS}/Device/ST/STM32F1xx/Include
)
target_compile_definitions(zeros-bluepill PRIVATE STM32F103xB)
target_link_options(
    zeros-bluepill
    PRIVATE -nostdlib
            -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/link.ld
            -Wl,--gc-sections
            -Wl,--print-memory-usage
            -Wl,-Map,${CMAKE_BINARY_DIR}/zeros-bluepill.map
)
add_custom_command(
    TARGET zeros-bluepill
    POST_BUILD
    COMMAND arm-none-eabi-size $<TARGET_FILE:zeros-bluepill>
    COMMENT "size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)"
)

# ---- 仿真目标:cmake --build build --target renode-bluepill ----
set(ZEROS_SIM_ELF "" CACHE STRING "覆盖仿真 ELF(空=本板默认产物)")
set(ZEROS_SIM_SECONDS 5 CACHE STRING "仿真运行秒数")

if(ZEROS_SIM_ELF)
    set(_elf ${ZEROS_SIM_ELF})
else()
    set(_elf $<TARGET_FILE:zeros-bluepill>)
endif()

add_custom_target(
    renode-bluepill
    COMMAND ${CMAKE_COMMAND}
            -DRENODE_ROOT=${CMAKE_SOURCE_DIR}
            -DRENODE_ELF=${_elf}
            -DRENODE_RESC=src/board/stm32f103_bluepill/sim/renode/bluepill.resc
            -DRENODE_SECONDS=${ZEROS_SIM_SECONDS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/sim/renode/run-renode.cmake
    DEPENDS zeros-bluepill
    USES_TERMINAL
)
```

最后一份源文件是验收的主角,`example/main.cpp`,27 行,您亲手敲:

```cpp
// ZerOS 基线冒烟:banner + 1kHz SysTick —— 验收 = Renode(bluepill.resc)里两者可见
#include <cstdint>

#include "stm32f1xx.h"
#include "uart.hpp"

extern "C" void SysTick_Handler() {
    static std::uint32_t ticks = 0;
    if (++ticks % 1000 == 0) {
        ZerOS::board::print("tick ");
        ZerOS::board::printdec(ticks / 1000);
        ZerOS::board::print("\r\n");
    }
}

int main() {
    ZerOS::board::uart1_init();
    ZerOS::board::print("\r\nZerOS baseline: C++23 @ STM32F103C8T6 (Blue Pill, Renode)\r\n");
    ZerOS::board::print("cc: gcc ");
    ZerOS::board::printdec(__GNUC__);
    ZerOS::board::print("\r\n");

    SysTick_Config(SystemCoreClock / 1000); // 1 kHz
    for (;;) {
        __WFI();
    }
}
```

咱们看 `SysTick_Handler` 里那个 `static` 计数器,每满一千打印一条,一秒钟一条 tick 就是这么来的;`SysTick_Config(SystemCoreClock / 1000)` 把心跳配成 1kHz,CMSIS 内部会把传入值减一塞进重载寄存器。三步跑起来:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build
cmake --build build --target renode-bluepill
```

构建的尾巴自带产物说明：

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:         536 B        64 KB      0.82%
             RAM:           8 B        20 KB      0.04%
size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)
```

串口窗里(无头跑法看 `run.log`),您会看到:

```text
cpu: Setting initial values: PC = 0x8000165, SP = 0x20005000.
[WARNING] sysbus: ReadDoubleWord from an unimplemented register RCC:APB2ENR ...
[WARNING] sysbus: WriteDoubleWord of value 0x4004 to an unimplemented register RCC:APB2ENR ...
usart1: ZerOS baseline: C++23 @ STM32F103C8T6 (Blue Pill, Renode)
usart1: cc: gcc 16
usart1: tick 1
usart1: tick 2
usart1: tick 3
```

第一行回答的正是向量表那一节按下的问题:SP = 0x20005000,正好是 `0x20000000 + 20K`,RAM 的顶,槽 0 的 `_estack` 生效了;PC = 0x8000165 落在 Flash 里,是 `reset_handler`。机器开机的头两步,跟咱们摆的表严丝合缝。接着两行 WARNING 是 uart.hpp 那条注释的另一半证据:RCC 在 Renode 里没有模型,读写都是 SVD 假值,坦白书里写了的。banner、gcc 版本号、每秒一条 tick,三样都在,这一站就算过。从下一站起,每站进门都拿这个当回归基准:改坏了什么,串口窗第一时间告诉您。

## 骨架搭建好了，下一步是？

下一站咱们给内核发内存:位图加定长块池,无堆世界里的 `new`,内核第一块真代码就从您手底下长出来。上一篇承诺过的 `std::expected` 错误通道、concept 约束的接口,都在那一站真正用起来;工程搭建这一站留下的 `.init_array` 盲区,也会在内存这条线走到真机时被亲手堵上。

休息一下！我们马上回来！
