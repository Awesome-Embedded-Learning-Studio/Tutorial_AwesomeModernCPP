---
title: "嵌入式 clangd:让 vscode 看懂交叉编译的代码"
description: "把 host 平台三步装好的 clangd 搬到 arm-none-eabi-g++ 交叉工程里就满屏红线。这篇讲透根因,给出 query-driver 与 .clangd 的完整可粘贴配置"
chapter: 0
order: 7
tags:
  - stm32f1
  - 嵌入式
  - intermediate
  - clangd
  - 交叉编译
difficulty: intermediate
platform: stm32f1
cpp_standard: [17, 20]
reading_time_minutes: 16
prerequisites:
  - "从零搭建 STM32 开发工具链"
  - "CMake 配置:从零构建 STM32 构建系统"
related:
  - "让 vscode 看懂您的代码——装 clangd,红线消失"
  - "交叉编译和CMake简单指南"
---

# 嵌入式 clangd:让 vscode 看懂交叉编译的代码

## 开场

起步卷篇 5 咱们给 host 平台装过 clangd,流程很短:卸掉微软的 C/C++ 扩展、装 clangd 扩展、`compile_commands.json` 一喂,代码就聪明了,跳转补全一条龙。那篇结尾还特地强调一句:clangd 之所以能"看懂"代码,靠的是它从 `compile_commands.json` 里读到的每一条编译命令:用什么编译器、加了哪些 flag、`-I` 指向哪、目标平台是什么。

您把同一套搬到嵌入式工程里,大概率当场就懵。

打开 `main.cpp`,第一行 `#include "stm32f1xx.h"` 就一根红波浪线;`HAL_GPIO_WritePin` 这种 HAL 函数全找不着;`stdint.h`、`core_cm3.h` 一个个都画着红,clangd 像瞎了一样。偏偏您 `cmake --build build` 编译能过、Renode 里跑 LED 也能闪。编译器明明认识这些头,clangd 怎么就不认?

这篇就是治这个的。咱们把根因拆清楚,再给一份可以直接抄走的配置。仓库里 `code/stm32f1-tutorials/*/.vscode/settings.json` 一直在用这套配置,只是从来没文档讲过它在干什么,这篇把它讲清楚。

## 为什么会全红:clangd 在自己造路径

咱们先回忆 host 平台那套为什么能跑。host 工程里 clangd 看到的编译命令长这样:

```text
/usr/bin/g++ -std=c++20 -I/home/you/proj/include main.cpp
```

编译器是 `g++`,clangd 拿着这条命令就能干活,因为它对 `g++` 的头文件布局了如指掌,`/usr/include/c++/14`、`/usr/include` 这一套标准路径它内置了,咱们什么都不用配,它直接拿来用。

换到咱们的嵌入式工程,`compile_commands.json` 里 clangd 看到的编译命令变成了这样:

```json
{
  "directory": "/home/you/proj/build",
  "command": "/usr/sbin/arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb -I.../Drivers/CMSIS/Device/ST/STM32F1xx/Include main.cpp -c -o CMakeFiles/main.dir/main.cpp.o",
  "file": "../main.cpp"
}
```

注意,编译器从 `g++` 换成了 `arm-none-eabi-g++`。问题来了:clangd 自己是基于 clang 的,它**不认识这个 GNU 交叉编译器内部把头文件装在哪**。它对 GCC 的内置路径布局,是从咱们本机 `g++` 那里推断出来的,arm 的 newlib 头、arm 的 libstdc++ 头、CMSIS 的 `core_cm3.h`,它一个都不知道。

那它会怎么做?咱们往下看:clangd 在 14 版本之后,对这类它不认识的编译器,默认会套用一个叫 **BareMetal** 的假想 toolchain(target 是 `arm-none-eabi`)。这个假想 toolchain 会自己造一堆路径,典型长这样:

```text
clang-runtimes/arm-none-eabi/include
clang-runtimes/arm-none-eabi/include/c++
clang-runtimes/arm-none-eabi/share
```

您去仓库根目录、去 `/usr/lib`、去任何地方 `find` 一下,都找不到 `clang-runtimes/arm-none-eabi` 这个目录,因为它根本不存在,是 clangd 推测出来的"按理说应该在这"的路径。系统头 `stdint.h`、CMSIS 头 `core_cm3.h` 不在这些假路径里,clangd 自然找不到,于是全画红。

::: warning 病根不在 clangd 笨
根因是 clangd **没去问真正的交叉编译器**"您的头文件装在哪"。它在用自己内置的、基于 clang runtime 目录的猜测去套一个 GCC 工具链,而 GCC 的头文件布局和 clang runtime 完全是两套东西。猜错了,路径全是空的,头全找不到。
:::

补一刀。clangd 哪怕猜到了路径,它也不知道交叉编译器内置的那些宏。咱们验证一下 `arm-none-eabi-g++` 在没指定 `-mcpu` 时内置哪些宏:

```bash
$ arm-none-eabi-g++ -E -dM -xc++ /dev/null | grep -E "__ARM_ARCH|__arm__|__thumb__"
#define __ARM_ARCH_ISA_ARM 1
#define __ARM_ARCH_ISA_THUMB 1
#define __ARM_ARCH_4T__ 1
#define __ARM_ARCH 4
#define __arm__ 1
```

您看,它默认是 `__ARM_ARCH_4T__`、ARMv4,而不是 Cortex-M3 对应的 ARMv7-M。Cortex-M3 是 ARMv7-M、只支持 Thumb-2 指令,和这个默认 target 完全不是一回事。`core_cm3.h`、`cmsis_gcc.h` 这些头会检查 `__ARM_ARCH_7M__` 之类的宏来决定走哪条代码路径,clangd 不带这些宏去解析,解析出来的结果和真实编译的不一样,有些头里的 `#error` 就会触发,屏幕更红。

## query-driver:让 clangd 真去问编译器

clangd 有个机制正好治这个,叫 **query-driver**。咱们把它拆开看。

它的原理很直接:clangd 不再自己瞎猜,而是**真的去执行您指定的编译器**,跑这么一条命令:

```bash
arm-none-eabi-g++ -E -xc++ -v /dev/null
```

这是让交叉编译器做一次"预处理空文件并打印详细信息"的操作。GCC 会把内部的头文件搜索路径、内置宏定义全部吐到 stderr 里。咱们本机跑一下(`arm-none-eabi-g++ 16.1.0`):

```text
#include "..." search starts here:
#include <...> search starts here:
 /usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/include/c++/16.1.0
 /usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/include/c++/16.1.0/arm-none-eabi
 /usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/include/c++/16.1.0/backward
 /usr/lib/gcc/arm-none-eabi/16.1.0/include
 /usr/lib/gcc/arm-none-eabi/16.1.0/include-fixed
 /usr/lib/gcc/arm-none-eabi/16.1.0/../../../../arm-none-eabi/include
End of search list.
```

这些路径才是**真实存在**的:`/usr/arm-none-eabi/include` 是 newlib 的 C 头,`/usr/.../include/c++/16.1.0` 是 newlib 配套的 libstdc++ 头。clangd 把这些路径抓过来当系统头,加上从 `compile_commands.json` 读到的 `-mcpu=cortex-m3 -mthumb -I.../Drivers/...` 一起喂给内部的 clang,咱们这边的代码就解析对了。

::: warning 为什么默认不开
query-driver 等于让 clangd **执行任意二进制**。设想一下:您 clone 一个来历不明的工程,它 `.clangd` 里写了 `Compiler: /tmp/evil.sh`,clangd 一启动就把这玩意儿当编译器跑一遍——这事不能让它默默发生。所以 clangd 默认拒绝 query-driver,必须由您显式 allowlist 哪些编译器路径可以执行。这是安全考虑,不是 bug。
:::

### 配置三件套

要让 query-driver 真正生效,需要三处一起配。咱们一件一件来。

### 第一件:VS Code 的 clangd.arguments 加 --query-driver

您打开工程的 `.vscode/settings.json`,加上 `--query-driver` 参数:

```json
{
    "clangd.arguments": [
        "--query-driver=/usr/sbin/arm-none-eabi-g++,/usr/sbin/arm-none-eabi-gcc"
    ]
}
```

等号后面是一串**逗号分隔的绝对路径**,支持 glob(`*`、`?`)。clangd 只会执行路径匹配这串 glob 之一的编译器,别的全拒。这里咱们 allowlist 了 `arm-none-eabi-g++` 和 `arm-none-eabi-gcc` 两个,正好覆盖 C++ 工程和纯 C 工程。

::: warning 这里写绝对路径,不是命令名
`--query-driver` 必须是绝对路径或绝对路径的 glob,写成 `--query-driver=arm-none-eabi-g++` 是不生效的:clangd 不去 `PATH` 里找,会直接判定无匹配、拒绝执行。您本机装的位置不同,路径要相应改(下面会讲仓库里为什么是 `/usr/sbin/`)。
:::

### 第二件:工程根 .clangd 配 CompileFlags.Compiler 和 BuiltinHeaders

光 allowlist 还不够。clangd 还得知道:这个工程要用 `arm-none-eabi-g++` 来解析,它的内置头要走 query-driver 而不是 clangd 自己的。咱们在工程根目录建一个 `.clangd` 文件:

```yaml
CompileFlags:
  Compiler: arm-none-eabi-g++
  Add:
    - -mcpu=cortex-m3
    - -mthumb
  BuiltinHeaders: QueryDriver
```

咱们逐行解释这四行在干嘛。

`Compiler: arm-none-eabi-g++` 告诉 clangd:这个工程的编译命令,把 executable 这一项**替换**成 `arm-none-eabi-g++`(写在 PATH 里能找到的名字就行,不需要绝对路径)。这样即便 `compile_commands.json` 里写的是别的(比如 CMake 给的相对路径),clangd 也会强制用咱们指定的这个交叉编译器。

`Add` 是给所有编译命令**追加**的 flag。嵌入式工程通常 CMake 里已经写了 `-mcpu=cortex-m3 -mthumb`,`compile_commands.json` 里就自带,这行其实有点冗余——但咱们写上更稳,因为有些 CMake 老脚本不一定把这俩 flag 透传到每个 target。`-mcpu=cortex-m3` 决定 target 是 Cortex-M3,`-mthumb` 强制走 Thumb 指令集,这俩缺一不可。

`BuiltinHeaders: QueryDriver` 是点睛之笔。它把内置头(`stdint.h`、`stddef.h` 这些 GCC 自带的头)的来源从"clangd 自己造的 `clang-runtimes/...` 假路径"切换到"通过 query-driver 问编译器拿到的真实路径"。咱们前面看到的那一地红波浪线,主要就是被这一行治好的。

### 第三件:compile_commands.json 要有交叉 flags

光配 clangd 这边还不够,咱们得保证它读的 `compile_commands.json` 是交叉编译版的。上一篇 CMake 里已经开了 `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`,生成的 json 自带 `-mcpu=cortex-m3/-mthumb/-I.../Drivers/...`,clangd 读到就知道这是给 arm 编的,不会再往 host 那边猜。

## 沉淀项目已有的配置

讲了半天原理,其实仓库里 `code/stm32f1-tutorials/` 下每个工程都已经配好了。咱们挑 `0_start_our_tutorial` 这个工程看,`.vscode/settings.json` 就这五行:

```json
{
    "clangd.arguments": [
        "--query-driver=/usr/sbin/arm-none-eabi-g++,/usr/sbin/arm-none-eabi-gcc"
    ]
}
```

`1_led_control`、`2_button_control`、`3_uart_logger` 这几个工程,`.vscode/settings.json` 内容一模一样。**仓库自己在用的就是这套,您照抄即可。**

这里有个细节咱们得讲清楚:为什么路径是 `/usr/sbin/` 而不是 `/usr/bin/`?

这跟工具链装的方式有关。本机用的是 WSL2 + pacman(Arch 系),`arm-none-eabi-gcc` 这个包把编译器实际装在 `/usr/sbin/` 下。咱们 `ls` 验证一下:

```bash
$ ls -l /usr/sbin/arm-none-eabi-g++
-rwxr-xr-x 2 root root 1.7M arm-none-eabi-g++ 16.1.0

$ which arm-none-eabi-g++
/usr/sbin/arm-none-eabi-g++
```

::: details Ubuntu / Arch / Homebrew 路径都不一样
您那边的工具链装在哪,看这张表:

| 平台 | 典型路径 |
|---|---|
| MSYS2 / WSL2 + pacman | `/usr/sbin/arm-none-eabi-g++` |
| Ubuntu apt(`gcc-arm-none-eabi` 包) | `/usr/bin/arm-none-eabi-g++` |
| Arch pacman | `/usr/bin/arm-none-eabi-g++` |
| macOS Homebrew | `/opt/homebrew/bin/arm-none-eabi-g++` |

您跑一下 `which arm-none-eabi-g++`,把输出填进 `--query-driver` 就行。拿不准就写 glob:`--query-driver=/usr/*/arm-none-eabi-g*,/opt/*/arm-none-eabi-g*`,把几个常见路径都覆盖上。

:::

注意这套 `.vscode/settings.json` **只配了 query-driver**,没配 `.clangd`。原因是这些工程的 `compile_commands.json` 里 `command` 字段已经直接写明了 `/usr/sbin/arm-none-eabi-g++`(CMake 用绝对路径生成的),clangd 一看 executable 是 arm 工具链,又开了 query-driver,头文件路径就自动从 GCC 那里拿到了,`BuiltinHeaders: QueryDriver` 这种 `.clangd` 配置其实是省了:query-driver 开启之后,clangd 默认就用 query 到的头替代自己的 builtin。`Compiler:` 和 `Add: [-mcpu...]` 这种 `.clangd` 配置,是当您的 `compile_commands.json` 不够干净、executable 路径或 flag 不对时才需要补的兜底。

## sysroot 和 --gcc-install-dir

配完上面这套,大部分情况红线就消了。但偶尔会有一两个头还是找不到,典型场景是您的 `compile_commands.json` 里**没带 sysroot**,clangd 自己解析时找不到 newlib 的部分头。这种情况要补一下 sysroot。

`--gcc-install-dir` 是 clang 的一个 flag,直接告诉它 GNU 工具链的 libstdc++ 装在哪个目录,clang 会从那里推算头文件位置。咱们在 `.clangd` 里追加:

```yaml
CompileFlags:
  Compiler: arm-none-eabi-g++
  Add:
    - -mcpu=cortex-m3
    - -mthumb
    - --gcc-install-dir=/usr/lib/gcc/arm-none-eabi/16.1.0
  BuiltinHeaders: QueryDriver
```

或者咱们用老办法 `-isystem` 显式补一个系统头目录(比如 newlib 的 C 头):

```yaml
CompileFlags:
  Add:
    - -isystem/usr/arm-none-eabi/include
```

排查的时候,咱们让 clangd 把日志打详细一点,看它实际去哪找头:

```text
View → Command Palette → Clangd: Open Log
```

或者在 VS Code 输出面板选 clangd,咱们在日志里搜 `Search starts here`,看 clangd 实际拿到的搜索路径有没有覆盖到 newlib 的目录。日志里能看到这样一行:

```text
Query driver arm-none-eabi-g++ for include paths
```

有这行,说明 query-driver 真的执行了;如果您看不到这行,多半是 `--query-driver` 的 glob 没匹配上,clangd 静默跳过了 query。回到 `.vscode/settings.json` 检查路径写对了没。

::: warning clangd 版本要够新
query-driver 和 `BuiltinHeaders: QueryDriver` 这套,需要 clangd **17 或更高**。`gcc-install-dir` 这个 flag 需要 clangd **18+**(底层 clang 18 才认)。老发行版自带的 clangd 14、15 跑不通这套,会卡在"配置都对但日志没动静"的玄学状态。您用 `clangd --version` 确认一下。
:::

## 验证

您配完之后,关掉 VS Code 重开(或者 Command Palette → `Clangd: Restart language server`),打开 `main.cpp`。该看到的几件事:

1. 第一行 `#include "stm32f1xx.h"` 的红波浪线消失。
2. 按住 `Ctrl` 点 `HAL_GPIO_WritePin`,光标跳到 `stm32f1xx_hal_gpio.h` 里的声明。
3. 您敲 `HAL_`,补全列表弹出来,列出 `HAL_GPIO_WritePin`、`HAL_Delay`、`HAL_Init` 这些。
4. clangd 日志里能看到 `Query driver arm-none-eabi-g++ for include paths` 这一行。

到这一步,您手上的交叉工程 clangd 体验就和 host 工程持平了——红线消失、跳转补全一条龙,该有的都有。

::: details 验证清单(出了问题您对照查)

- `clangd --version` 是 17+,最好 18+
- `--query-driver` 路径 glob 匹配上 `which arm-none-eabi-g++` 的输出
- `grep "mcpu" build/compile_commands.json` 有结果,确认交叉 flag 进了 json
- clangd 日志里出现 `Query driver ... for include paths`
- `arm-none-eabi-g++ -E -xc++ -v /dev/null` 能输出真实的 include 路径
- `.clangd` 里 `Compiler:`、`BuiltinHeaders: QueryDriver` 写对(只在 compile_commands 不够干净时需要)

:::

环境篇到这篇就齐了:工具链、模拟器、工程结构、构建、烧录(选修)、调试、IDE。下一站进 LED 章,先把"地砖下面"掀开看一眼裸寄存器,再回到 HAL 之上写现代 C++。如果您还没看过 [起步卷篇 5:装 clangd,红线消失](/getting-started/05-vscode-clangd),建议先回去看一遍,这篇的"为什么"建立在篇 5 的"怎么做"之上;想站在 host 角度再看一遍交叉编译的工程化,接 [vol7 的交叉编译和 CMake 指南](/vol7-engineering/01-cross-compilation-and-cmake)。
