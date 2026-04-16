# 第2篇：项目结构篇 —— HAL 库获取、启动文件坑位与目录搭建

> 上一篇我们把工具链装好了，现在来搭项目骨架。这篇记录我获取 STM32 HAL 库的全过程，包括那个让人摸不着头脑的嵌套 submodule 问题、启动文件命名规则背后的玄学，以及 `stm32f1xx_hal_conf.h` 里那些让你编译到一半报错的隐藏坑。

---

## 为什么这一步很重要

你可能会问，不就是个项目结构吗，随便建几个文件夹，把 HAL 库扔进去不就完了？还真不是。STM32 的 HAL 库有一套自己的"生态系统" —— CMSIS 核心层、HAL 驱动层、启动文件、链接脚本，这些东西必须按照特定的方式组织，否则编译器根本不知道从哪找头文件，链接器也不知道要把代码放到内存的哪个位置。

更麻烦的是，ST 官方的 HAL 库是通过 Git 仓库发布的，而且它内部还有嵌套的 submodule。如果你用常规方式克隆，十有八九会漏掉关键文件，等你编译到一半报错说找不到某个头文件时，再回头排查就非常痛苦。我在这上面栽过跟头，所以这一篇我会把所有坑都提前标出来，让你一次就把项目骨架搭对。

---

## 先搞清楚 HAL 库的三层架构

在我们开始下载代码之前，有必要先理解一下 ST 的 HAL 库是怎么分层设计的。这能帮助你理解为什么要建立那些目录、每个文件是干嘛的。

最底层是 **CMSIS-Core（Cortex Microcontroller Software Interface Standard）**。这是 ARM 制定的一套标准，定义了 Cortex-M 系列内核的寄存器访问接口。简单来说，CMSIS-Core 告诉你"这个芯片有一个叫做 SCB 的寄存器，地址是 0xE000ED00"，这样你写代码时就可以用 `SCB->VTOR = 0x00` 这样的方式操作寄存器，而不是去记那些魔法数字。CMSIS-Core 是 ARM 官方维护的，对所有 Cortex-M 芯片都通用。

中间层是 **CMSIS-Device**。这部分是 ST 针对 STM32F1 系列芯片做的特殊化。它定义了 F103C8T6 这个具体芯片有什么外设、每个外设有多少个、寄存器地址在哪里。比如 `GPIOA` 的基地址是 `0x40010800`，这种信息就写在 CMSIS-Device 的头文件里。你以后会看到一堆 `stm32f103xb.h` 这种文件，它们就属于这一层。

最上层才是 **HAL 驱动层**。这是 ST 用 C 语言写的一套外设驱动 API，比如 `HAL_GPIO_TogglePin()`、`HAL_UART_Transmit()` 这些函数。它们的作用是屏蔽底层寄存器操作，让你用统一的方式操作不同系列的 STM32。理论上，你用 HAL 写的代码移植到 STM32F4 上应该只需要改少量配置。

再往上就是你的应用代码了。应用代码调用 HAL 的 API，HAL 调用 CMSIS-Device 的定义，CMSIS-Device 再依赖 CMSIS-Core 的内核接口。理解这个分层之后，你就会知道为什么需要建立这么多目录 —— 每一层都有自己专属的文件夹。

---

## 获取 HAL 库： submodule 的陷阱

好了，现在我们来获取代码。ST 官方的 STM32F1 HAL 库托管在 GitHub 上，仓库地址是 `https://github.com/STMicroelectronics/STM32CubeF1`。你可能第一时间会想到直接 `git clone`，但这里有个坑，让我一步步演示。

首先创建我们的项目根目录。我习惯把所有依赖都放在 `third_party` 目录下，这样项目结构清晰：

```bash
mkdir -p ~/stm32-f103-project/third_party
cd ~/stm32-f103-project/third_party
```

现在我们来克隆 HAL 库。这里有个新手最容易犯的错误 —— 用 `--depth=1` 做浅克隆：

```bash
# 错误做法！不要这样做！
git submodule add --depth=1 https://github.com/STMicroelectronics/STM32CubeF1.git STM32F1
```

这个命令看起来很合理：用 submodule 把库加进来，`--depth=1` 只拉取最新版本节省时间。但问题是，STM32CubeF1 这个仓库内部还有自己的 submodule（CMSIS 库是作为 submodule 引入的），而 `--depth=1` 会阻止嵌套的 submodule 被正确初始化。

当你以后去检查目录结构时，你会发现一个诡异的现象：

```bash
ls third_party/STM32F1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

正常情况下这个目录应该有一堆启动文件（`startup_stm32f103xb.s` 之类），但如果你用了浅克隆，这里会是空的。编译时你会看到类似这样的报错：

```text
error: cannot find 'startup_stm32f103xb.s'
```

那时候你再去查为什么文件缺失，会一头雾水 —— 明明 submodule 已经加进来了，为什么文件还是缺失？

原因在于 Git 的 submodule 机制。当你 clone 一个包含 submodule 的仓库时，Git 只会拉取外层仓库的内容，里面的 submodule 目录只是一个"指针"，指向另一个仓库的某个 commit。你需要额外运行 `git submodule update --init --recursive` 才能让 Git 真正去拉取那些嵌套的 submodule 内容。而 `--depth=1` 浅克隆会破坏这个机制，因为嵌套 submodule 的历史记录没有被完整拉取。

正确的做法是完整克隆，然后递归初始化所有 submodule：

```bash
git clone --recursive https://github.com/STMicroelectronics/STM32CubeF1.git STM32F1
```

如果你已经把 submodule 加到项目里了，但忘记用 `--recursive`，可以补救一下：

```bash
cd third_party/STM32F1
git submodule update --init --recursive
```

这个命令会递归地拉取所有嵌套的 submodule，确保 CMSIS Device 目录的文件都齐全。你可以用刚才的 ls 命令验证一下启动文件是不是都出现了：

```bash
ls third_party/STM32F1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

你应该能看到类似这样的输出：

```text
startup_stm32f100xb.s  startup_stm32f103x6.s  startup_stm32f103xb.s  startup_stm32f103xe.s
startup_stm32f100xe.s  startup_stm32f101x6.s  startup_stm32f101xb.s  ...（还有很多）
```

看到这些 `.s` 文件就说明 submodule 拉取成功了。顺便一提，如果用 Arch Linux，你的系统可能没有预装 `git`，需要先 `pacman -S git`；Ubuntu 用户通常默认就有 git。

---

## 启动文件的命名玄学

现在我们有了启动文件，但新问题来了 —— 到底该用哪一个？

这里有个让无数新手踩坑的细节。网上很多教程写的是 `startup_stm32f103x8.s`，但你仔细看一下刚才 ls 的输出，会发现根本没有这个文件！ST 官方的文件名是 `startup_stm32f103xb.s`。

这个差异背后是 ST 的芯片命名规则。让我解释一下：F103C8T6 这个型号里的"C8"代表什么？C 是小容量（Low-density），8 代表 64KB Flash。但 ST 的启动文件命名规则不是按照 Flash 大小来的，而是按照"密度等级"（density category）：

- `x6` = Low-density devices（小容量，16-32KB Flash）
- `xB` = Medium-density devices（中等容量，64-128KB Flash）
- `xE` = High-density devices（大容量，256-512KB Flash）
- `xG` = XL-density devices（超大容量，768KB-1MB Flash）

F103C8T6 有 64KB Flash，属于中等容量，所以对应的启动文件是 `startup_stm32f103xb.s`。这里的"B"不是 8 的十六进制，而是 ST 内部的一个密度代码。

对应到编译时的宏定义，你需要传递 `-DSTM32F103xB`（注意是大写 B）。很多教程错误地写成了 `-DSTM32F103x8`，结果会导致头文件里的条件编译选错分支，编译出的代码可能和你的硬件不匹配。

你可能会问，为什么 ST 要搞这么复杂的命名？历史原因。STM32F1 系列是 ST 最早推出的 Cortex-M3 产品线，当时他们按照 Flash 容量分了好几个档次。F103xB 覆盖了 64KB 和 128KB 两个版本，硬件上除了 Flash 大小之外几乎一模一样，所以用同一套启动文件和头文件。

那启动文件到底是干嘛的？简单来说，它是芯片复位后执行的第一段代码。STM32 上电或者复位时，CPU 会从地址 0x00000000 读取"初始堆栈指针"，然后从 0x00000004 读取"复位向量"（Reset Handler），跳转到那里执行。启动文件就是定义了这个向量表（Vector Table），里面包含所有中断和异常的入口地址。它还负责初始化 `.data` 段（把 Flash 里的初始值复制到 RAM）和清零 `.bss` 段，最后跳转到你的 `main()` 函数。没有启动文件，芯片复位后不知道该干什么，程序就没法运行。

---

## 项目目录结构

现在我们把 HAL 库拿到了，启动文件也搞明白了，接下来要搭一个清晰的项目结构。我推荐这样的布局：

```text
stm32-f103-project/
├── third_party/
│   └── STM32F1/                    # HAL 库（刚才克隆的）
│       ├── Drivers/
│       │   ├── CMSIS/
│       │   │   ├── Core/           # CMSIS-Core（ARM 标准）
│       │   │   └── Device/ST/STM32F1xx/  # CMSIS-Device（F1 系列）
│       │   └── STM32F1xx_HAL_Driver/    # HAL 驱动层
│       └── ...
├── src/                            # 你的源代码
│   ├── main.cpp
│   ├── stm32f1xx_hal_conf.h       # HAL 配置文件（从模板复制）
│   ├── stm32f1xx_it.c             # 中断服务函数（HAL 需要）
│   └── stm32f1xx_it.h
├── build/                          # CMake 构建目录（生成后）
├── CMakeLists.txt                  # 构建配置
└── linker/                        # 链接脚本
    └── STM32F103xC8.ld
```

让我解释一下每个目录的作用：

`third_party/STM32F1` 是我们刚才克隆的 HAL 库，这个目录不需要你手动修改，只管引用就行。它里面的 CMSIS 和 HAL_Driver 会通过 CMake 的 `target_include_directories` 加入到编译路径里。

`src/` 存放你的应用代码。`main.cpp` 是程序入口，`stm32f1xx_hal_conf.h` 是 HAL 库的配置文件（下面会详细讲这个坑），`stm32f1xx_it.c/h` 是中断服务函数。HAL 库的某些外设（比如 UART）需要用户定义中断处理函数，这些函数就写在 `_it.c` 里。

`build/` 是 CMake 的输出目录。我们用"out-of-source"构建方式，不把生成的文件污染到源码目录里。编译产物（`.o`、`.elf`、`.bin`）都会放在这里。

`linker/` 存放链接脚本。我们下一篇会详细讲怎么写这个文件，现在先知道它定义了内存布局就行。

你可能注意到我用了 `STM32F103xC8.ld` 作为链接脚本名。这个命名没有硬性规定，但我习惯把芯片型号写进文件名，这样一眼就知道是给哪个芯片用的。F103C8 和 F103CB（128KB 版本）的区别只在于 Flash 大小，链接脚本里改一下 `LENGTH` 参数就行，其他都一样。

---

## stm32f1xx_hal_conf.h：那些隐藏的坑

现在我们来到第一个"重灾区" —— HAL 配置文件。ST 官方的 HAL 库并不包含一个现成的 `stm32f1xx_hal_conf.h`，只有一个 `stm32f1xx_hal_conf_template.h` 模板。你需要把模板复制到项目里，重命名，然后修改。

为什么不用 CubeMX？如果你用 ST 的 STM32CubeMX 图形化工具生成项目，它会自动帮你生成这个文件。但我们走"纯手写 CMake"路线，就必须手动搞定。

首先把模板复制过来：

```bash
cp third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_conf_template.h \
   src/stm32f1xx_hal_conf.h
```

然后用编辑器打开这个文件，开始修改。第一个坑是**模块选择**。文件开头有一大堆 `#define HAL_XXX_MODULE_ENABLED`，默认所有模块都被启用了。这会导致编译时把所有 HAL 驱动都编译进去，固件体积膨胀得厉害。对于我们的 LED 闪烁程序，只需要启用这几个模块：

```c
#define HAL_MODULE_ENABLED         // HAL 核心
#define HAL_GPIO_MODULE_ENABLED    // GPIO（控制 LED）
#define HAL_RCC_MODULE_ENABLED     // 时钟配置
#define HAL_CORTEX_MODULE_ENABLED  // Cortex-M3 内核函数
```

其他模块的 `#define` 都注释掉。这样编译器只会把你需要的 HAL 函数编译进去，链接器也能更好地做死代码消除（dead code elimination）。

第二个坑是**时钟宏定义**。往下翻几行，你会看到一堆 `HSE_VALUE`、`HSI_VALUE`、`LSI_VALUE` 之类的宏。这些是外部/内部晶振频率，HAL 库的 RCC 模块需要知道这些频率才能计算系统时钟。

最关键的是 `LSI_VALUE`，这个宏在模板文件里是 `#if !defined (LSI_VALUE)` 条件定义的。如果你没有定义这个宏，编译 HAL 的某些模块（比如 RTC 或看门狗）时会报错：

```text
error: 'LSI_VALUE' undeclared
```

解决方案很简单：在 `stm32f1xx_hal_conf.h` 里确保所有时钟宏都有定义。Blue Pill 开发板通常用 8MHz 外部晶振（HSE），内部高速振荡器（HSI）是 8MHz，内部低速振荡器（LSI）大约 40kHz，外部低速晶振（LSE）通常是 32.768kHz（如果板子上有的话）。把这些都写上：

```c
#define HSE_VALUE    8000000U   // 8MHz 外部晶振
#define HSI_VALUE    8000000U   // 8MHz 内部高速振荡器
#define LSI_VALUE    40000U     // 40kHz 内部低速振荡器
#define LSE_VALUE    32768U     // 32.768kHz 外部低速晶振（如果没有就用这个默认值）
```

注意单位是赫兹，用大写 `U` 后缀表示"无符号整数"。这里的值对不对影响很大 —— 如果 HSE_VALUE 写错，RCC 计算出的系统时钟频率就会错，UART 的波特率也会跟着错，串口输出就是乱码。

第三个坑是**assert_param 宏**。文件快结尾的地方有这样一个宏定义：

```c
#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif
```

HAL 库里到处都在用 `assert_param()` 来检查函数参数是否合法。比如你调用 `HAL_GPIO_Init()` 时传入了一个无效的引脚号，assert 会捕获这个错误。如果你定义了 `USE_FULL_ASSERT`，assert 失败时会跳转到 `assert_failed()` 函数（这个函数需要你自己实现），否则就什么都不做（空宏）。

很多新手忘记定义 `assert_param`，导致编译时报错说"undefined macro"。解决办法：要么在 `stm32f1xx_hal_conf.h` 里把上面那段代码加上（模板里已经有了，确认没被注释掉），要么在 CMake 里加 `-DUSE_FULL_ASSERT=0`。

第四个坑是**模块的 callback 宏**。文件后半部分有一大堆 `USE_HAL_XXX_REGISTER_CALLBACKS`，这些是为了启用 HAL 的"回调函数注册"功能（一种更灵活的中断处理方式）。默认值是 0，对于简单应用保持 0 就行。如果你改成 1，就需要为每个外设实现回调函数，代码复杂度会上升。

最后还有一个细节：`stm32f1xx_hal_conf.h` 必须能被 HAL 库的头文件找到。通常的做法是把它放到 `src/` 目录，然后通过 CMake 的 `target_include_directories` 把 `src/` 加到包含路径里。或者你可以直接放到项目根目录，编译时用 `-I.` 指定。HAL 库的头文件会通过 `#include "stm32f1xx_hal_conf.h"` 来引用它（注意是引号不是尖括号），所以它必须在搜索路径里。

---

## template 文件的坑：预告

在结束之前，我要提前预警一个 CMake 篇才会遇到的坑。如果你直接把整个 HAL 库的 `Src/` 目录都扔给 CMake 编译，会报类似这样的错误：

```text
multiple definition of 'HAL_MspInit'
```

这是因为 HAL 库里有几个 `*_template.c` 文件，比如 `stm32f1xx_hal_msp_template.c`。这些模板文件不是用来直接编译的，而是让你复制到项目里修改成自己的实现。如果你把它们也编译进去，就会和你的实现冲突（两个文件都定义了 `HAL_MspInit()`）。

解决办法是在 CMake 里用 `list(FILTER)` 把这些 template 文件从源文件列表里排除掉。具体的 CMake 写法留到下一篇讲，现在你只需要知道：不要盲目地把 HAL 库的所有 `.c` 文件都加进来编译，那些带 `template` 后缀的要剔除出去。

---

## 到哪一步了

这篇我们完成了项目结构的搭建。你现在应该有：

1. 一个正确克隆的 HAL 库（submodule 都初始化了）
2. 知道 F103C8T6 要用 `startup_stm32f103xb.s` 启动文件和 `-DSTM32F103xB` 宏
3. 一个清晰的项目目录布局
4. 一个配置好的 `stm32f1xx_hal_conf.h`（时钟宏、模块选择都没问题）

但还没完。下一篇我们会讲链接脚本和 CMake 配置，这才是让代码真正能编译出来的关键。链接脚本要告诉链接器 STM32F103C8T6 的 Flash 起始地址是 0x08000000、大小 64KB、RAM 从 0x20000000 开始、大小 20KB。写错了这个文件，程序能编译通过但运行不起来，因为代码被放到了错误的内存地址。

在那之前，你可以先把项目结构建起来，把 `stm32f1xx_hal_conf.h` 复制并修改好。下一篇文章我们开始写 CMakeLists.txt 和链接脚本，争取让你编译出第一个 `.bin` 固件文件。
