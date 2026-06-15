---
chapter: 14
difficulty: beginner
order: 2
platform: stm32f1
reading_time_minutes: 15
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 2: Project Structure — Acquiring the HAL Library, Startup Code Pitfalls,
  and Directory Setup'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/00-env-setup/02-project-structure.md
  source_hash: 42983cc0de29f3716726931f1e2f1c8f784798697d1d4a59b379c1c11fa34fa6
  token_count: 2359
  translated_at: '2026-05-26T11:59:09.043220+00:00'
description: ''
---
# Part 2: Project Structure — Getting the HAL Library, Startup File Pitfalls, and Directory Setup

> In the previous part, we installed the toolchain. Now let's set up the project skeleton. This part documents my entire process of obtaining the STM32 HAL library, including that baffling nested submodule issue, the hidden logic behind startup file naming conventions, and those hidden pitfalls in ``stm32f1xx_hal_conf.h`` that cause errors halfway through compilation.

---

## Why This Step Matters

You might ask, isn't it just a project structure? Can't we just create a few folders, toss the HAL library in, and call it a day? Not quite. The STM32 HAL library has its own "ecosystem" — the CMSIS core layer, the HAL driver layer, startup code, and linker scripts. These must be organized in a specific way, or the compiler won't know where to find header files, and the linker won't know where to place code in memory.

To make matters worse, ST's official HAL library is distributed via a Git repository with nested submodules. If you clone it the usual way, you'll most likely miss critical files. When your build fails halfway through complaining about a missing header file, tracing back to find the root cause is incredibly painful. I've stumbled on this myself, so in this part, I'll flag all the pitfalls upfront so we can get the project skeleton right on the first try.

---

## Understanding the Three-Layer HAL Architecture

Before we download any code, we need to understand how ST's HAL library is layered. This helps clarify why we need certain directories and what each file does.

At the bottom is **CMSIS-Core (Cortex Microcontroller Software Interface Standard)**. This is a standard defined by ARM that specifies register access interfaces for the Cortex-M series cores. Simply put, CMSIS-Core tells you "this chip has a register called SCB at address 0xE000ED00," so you can manipulate registers using ``SCB->VTOR = 0x00`` in your code instead of memorizing magic numbers. CMSIS-Core is maintained by ARM and is common to all Cortex-M chips.

The middle layer is **CMSIS-Device**. This is ST's specialization for the STM32F1 series. It defines what peripherals the specific F103C8T6 chip has, how many of each, and where their register addresses are. For example, the base address of ``GPIOA`` is ``0x40010800``, and this information lives in the CMSIS-Device header files. You'll see a bunch of ``stm32f103xb.h`` files later — they belong to this layer.

The top layer is the **HAL driver layer**. This is a set of peripheral driver APIs written in C by ST, such as ``HAL_GPIO_TogglePin()`` and ``HAL_UART_Transmit()``. Their purpose is to abstract away low-level register operations, letting you control different STM32 series in a uniform way. In theory, code written with HAL should require only minor configuration changes when porting to an STM32F4.

Above that is your application code. The application code calls HAL APIs, HAL calls CMSIS-Device definitions, and CMSIS-Device depends on the CMSIS-Core kernel interfaces. Once you understand this layering, you'll know why we need so many directories — each layer has its own dedicated folder.

---

## Getting the HAL Library: The Submodule Trap

Alright, let's get the code. ST's official STM32F1 HAL library is hosted on GitHub at ``https://github.com/STMicroelectronics/STM32CubeF1``. Your first instinct might be to simply run ``git clone``, but there's a catch here. Let me walk you through it step by step.

First, let's create our project root directory. I like to keep all dependencies under a ``third_party`` directory for a clean project structure:

````bash
mkdir -p ~/stm32-f103-project/third_party
cd ~/stm32-f103-project/third_party
````

Now let's clone the HAL library. Here's a mistake beginners make most often — doing a shallow clone with ``--depth=1``:

````bash
# 错误做法！不要这样做！
git submodule add --depth=1 https://github.com/STMicroelectronics/STM32CubeF1.git STM32F1
````

This command looks reasonable: it adds the library as a submodule, and ``--depth=1`` only fetches the latest version to save time. But the problem is that the STM32CubeF1 repository itself has internal submodules (the CMSIS library is brought in as a submodule), and ``--depth=1`` prevents nested submodules from being properly initialized.

When you check the directory structure later, you'll notice a strange phenomenon:

````bash
ls third_party/STM32F1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
````

Normally, this directory should contain a bunch of startup files (like ``startup_stm32f103xb.s``), but if you used a shallow clone, it will be empty. During compilation, you'll see errors like this:

````text
error: cannot find 'startup_stm32f103xb.s'
````

When you go back to investigate why files are missing, you'll be completely baffled — the submodule was clearly added, so why are the files still missing?

The reason lies in Git's submodule mechanism. When you clone a repository containing submodules, Git only fetches the outer repository's content. The submodule directories inside are just "pointers" pointing to a specific commit in another repository. You need to additionally run ``git submodule update --init --recursive`` to make Git actually fetch those nested submodule contents. And the ``--depth=1`` shallow clone breaks this mechanism because the history of nested submodules isn't fully fetched.

The correct approach is to do a full clone, then recursively initialize all submodules:

````bash
git clone --recursive https://github.com/STMicroelectronics/STM32CubeF1.git STM32F1
````

If you've already added the submodule to your project but forgot to use ``--recursive``, you can fix it:

````bash
cd third_party/STM32F1
git submodule update --init --recursive
````

This command recursively fetches all nested submodules, ensuring the CMSIS Device directory files are complete. You can verify with the `ls` command we used earlier to check if the startup files have appeared:

````bash
ls third_party/STM32F1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
````

You should see output similar to this:

````text
startup_stm32f100xb.s  startup_stm32f103x6.s  startup_stm32f103xb.s  startup_stm32f103xe.s
startup_stm32f100xe.s  startup_stm32f101x6.s  startup_stm32f101xb.s  ...（还有很多）
````

Seeing these ``.s`` files means the submodule was fetched successfully. By the way, if you're on Arch Linux, your system might not have ``git`` preinstalled, so you'll need to run ``pacman -S git`` first; Ubuntu users typically have git installed by default.

---

## The Hidden Logic Behind Startup File Naming

Now we have the startup files, but a new problem arises — which one should we use?

Here's a detail that trips up countless beginners. Many online tutorials reference ``startup_stm32f103x8.s``, but if you look closely at the `ls` output from earlier, you'll notice this file doesn't exist at all! ST's official filename is ``startup_stm32f103xb.s``.

Behind this discrepancy lies ST's chip naming convention. Let me explain: what does "C8" in the model F103C8T6 mean? C stands for low-density, and 8 represents 64KB Flash. But ST's startup file naming isn't based on Flash size — it's based on "density category":

- ``x6`` = Low-density devices (16-32KB Flash)
- ``xB`` = Medium-density devices (64-128KB Flash)
- ``xE`` = High-density devices (256-512KB Flash)
- ``xG`` = XL-density devices (768KB-1MB Flash)

The F103C8T6 has 64KB Flash, placing it in the medium-density category, so the corresponding startup file is ``startup_stm32f103xb.s``. The "B" here isn't hexadecimal for 8, but rather an internal density code used by ST.

Correspondingly, for the compile-time macro definition, you need to pass ``-DSTM32F103xB`` (note the uppercase B). Many tutorials incorrectly write ``-DSTM32F103x8``, which causes the conditional compilation in the header files to select the wrong branch, resulting in code that doesn't match your hardware.

You might ask, why does ST use such a complex naming scheme? Historical reasons. The STM32F1 series was ST's first Cortex-M3 product line, and at the time, they divided it into several tiers based on Flash capacity. F103xB covers both the 64KB and 128KB versions, and apart from Flash size, the hardware is virtually identical, so they share the same startup file and header files.

So what does the startup file actually do? Simply put, it's the first piece of code executed after the chip resets. When the STM32 powers on or resets, the CPU reads the "initial stack pointer" from address 0x00000000, then reads the "reset vector" (Reset Handler) from 0x00000004 and jumps there to execute. The startup file defines this Vector Table, which contains the entry addresses for all interrupts and exceptions. It also handles initializing the ``.data`` section (copying initial values from Flash to RAM) and zeroing the ``.bss`` section, before finally jumping to your ``main()`` function. Without the startup file, the chip wouldn't know what to do after a reset, and the program couldn't run.

---

## Project Directory Structure

Now that we have the HAL library and understand the startup files, let's set up a clean project structure. I recommend this layout:

````text
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
````

Let me explain what each directory does:

``third_party/STM32F1`` is the HAL library we just cloned. You don't need to manually modify this directory — just reference it. The CMSIS and HAL_Driver inside will be added to the compilation path through CMake's ``target_include_directories``.

``src/`` holds your application code. ``main.cpp`` is the program entry point, ``stm32f1xx_hal_conf.h`` is the HAL library configuration file (we'll dive into its pitfalls below), and ``stm32f1xx_it.c/h`` is for interrupt service routines. Certain HAL peripherals (like UART) require user-defined interrupt handler functions, and these go in ``_it.c``.

``build/`` is the CMake output directory. We use an "out-of-source" build approach to avoid polluting the source directory with generated files. Build artifacts (``.o``, ``.elf``, ``.bin``) will all end up here.

``linker/`` stores the linker script. We'll cover how to write this file in detail in the next part; for now, just know that it defines the memory layout.

You might notice I used ``STM32F103xC8.ld`` as the linker script name. There's no hard rule for this naming, but I like to include the chip model in the filename so I can tell at a glance which chip it's for. The only difference between the F103C8 and F103CB (128KB version) is the Flash size — you just need to change the ``LENGTH`` parameter in the linker script, and everything else stays the same.

---

## stm32f1xx_hal_conf.h: The Hidden Pitfalls

Now we arrive at the first "minefield" — the HAL configuration file. ST's official HAL library doesn't include a ready-to-use ``stm32f1xx_hal_conf.h``; it only provides a ``stm32f1xx_hal_conf_template.h`` template. You need to copy the template into your project, rename it, and modify it.

Why not use CubeMX? If you use ST's STM32CubeMX graphical tool to generate a project, it automatically generates this file for you. But since we're taking the "pure hand-written CMake" route, we must handle it manually.

First, copy the template over:

````bash
cp third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_conf_template.h \
   src/stm32f1xx_hal_conf.h
````

Then open this file in your editor and start modifying. The first pitfall is **module selection**. Near the top of the file, there's a bunch of ``#define HAL_XXX_MODULE_ENABLED`` defines, with all modules enabled by default. This causes all HAL drivers to be compiled in, bloating the firmware size significantly. For our LED blink program, we only need to enable these modules:

````c
#define HAL_MODULE_ENABLED         // HAL 核心
#define HAL_GPIO_MODULE_ENABLED    // GPIO（控制 LED）
#define HAL_RCC_MODULE_ENABLED     // 时钟配置
#define HAL_CORTEX_MODULE_ENABLED  // Cortex-M3 内核函数
````

Comment out the ``#define`` for all other modules. This way, the compiler only compiles the HAL functions you need, and the linker can do a better job of dead code elimination.

The second pitfall is **clock macro definitions**. Scroll down a bit and you'll see a bunch of macros like ``HSE_VALUE``, ``HSI_VALUE``, and ``LSI_VALUE``. These represent external/internal crystal frequencies, and the HAL library's RCC module needs to know these frequencies to calculate the system clock.

The most critical one is ``LSI_VALUE``, which is conditionally defined with ``#if !defined (LSI_VALUE)`` in the template file. If you don't define this macro, compiling certain HAL modules (like RTC or the watchdog) will throw errors:

````text
error: 'LSI_VALUE' undeclared
````

The solution is simple: ensure all clock macros are defined in ``stm32f1xx_hal_conf.h``. The Blue Pill board typically uses an 8MHz external crystal (HSE), the internal high-speed oscillator (HSI) is 8MHz, the internal low-speed oscillator (LSI) is approximately 40kHz, and the external low-speed crystal (LSE) is usually 32.768kHz (if present on the board). Write them all out:

````c
#define HSE_VALUE    8000000U   // 8MHz 外部晶振
#define HSI_VALUE    8000000U   // 8MHz 内部高速振荡器
#define LSI_VALUE    40000U     // 40kHz 内部低速振荡器
#define LSE_VALUE    32768U     // 32.768kHz 外部低速晶振（如果没有就用这个默认值）
````

Note that the unit is Hertz, using the uppercase ``U`` suffix to denote an "unsigned integer." Getting these values right matters a lot — if HSE_VALUE is wrong, the system clock frequency calculated by RCC will be off, the UART baud rate will be wrong as well, and serial output will be garbled.

The third pitfall is the **assert_param macro**. Near the end of the file, there's this macro definition:

````c
#ifdef  USE_FULL_ASSERT
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif
````

The HAL library uses ``assert_param()`` everywhere to check whether function parameters are valid. For example, if you call ``HAL_GPIO_Init()`` with an invalid pin number, the assert will catch this error. If you define ``USE_FULL_ASSERT``, assert failure jumps to the ``assert_failed()`` function (which you need to implement yourself); otherwise, it does nothing (empty macro).

Many beginners forget to define ``assert_param``, leading to "undefined macro" errors during compilation. The fix: either add the code block above in ``stm32f1xx_hal_conf.h`` (it's already in the template, just make sure it's not commented out), or add ``-DUSE_FULL_ASSERT=0`` in CMake.

The fourth pitfall is **module callback macros**. In the second half of the file, there are a bunch of ``USE_HAL_XXX_REGISTER_CALLBACKS`` defines. These enable HAL's "callback function registration" feature (a more flexible approach to interrupt handling). The default value is 0, and for simple applications, keeping it at 0 is fine. If you change it to 1, you'll need to implement callback functions for each peripheral, increasing code complexity.

One final detail: ``stm32f1xx_hal_conf.h`` must be findable by the HAL library header files. The usual approach is to place it in the ``src/`` directory, then add ``src/`` to the include path via CMake's ``target_include_directories``. Alternatively, you can place it directly in the project root and specify it at compile time with ``-I.``. The HAL library header files reference it via ``#include "stm32f1xx_hal_conf.h"`` (note the quotes, not angle brackets), so it must be in the search path.

---

## Template File Pitfalls: A Preview

Before we wrap up, I want to give an early warning about a pitfall we'll encounter in the CMake part. If you feed the entire HAL library ``Src/`` directory directly to CMake for compilation, you'll get errors like this:

````text
multiple definition of 'HAL_MspInit'
````

This is because the HAL library contains several ``*_template.c`` files, such as ``stm32f1xx_hal_msp_template.c``. These template files aren't meant to be compiled directly — they're meant for you to copy into your project and modify into your own implementation. If you compile them as-is, they'll conflict with your implementation (both files define ``HAL_MspInit()``).

The solution is to use ``list(FILTER)`` in CMake to exclude these template files from the source file list. I'll cover the specific CMake syntax in the next part; for now, you just need to know: don't blindly add all ``.c`` files from the HAL library into compilation — the ones with the ``template`` suffix must be filtered out.

---

## Where We Are Now

In this part, we finished setting up the project structure. You should now have:

1. A correctly cloned HAL library (with all submodules initialized)
2. Knowledge that F103C8T6 needs the ``startup_stm32f103xb.s`` startup file and the ``-DSTM32F103xB`` macro
3. A clean project directory layout
4. A properly configured ``stm32f1xx_hal_conf.h`` (clock macros and module selection are all set)

But we're not done yet. In the next part, we'll cover the linker script and CMake configuration — that's what actually makes the code compilable. The linker script needs to tell the linker that the STM32F103C8T6's Flash starts at 0x08000000 with a size of 64KB, and RAM starts at 0x20000000 with a size of 20KB. If you get this file wrong, the program will compile but won't run, because the code will be placed at incorrect memory addresses.

Before that, you can go ahead and set up the project structure and copy and modify ``stm32f1xx_hal_conf.h``. In the next article, we'll write the CMakeLists.txt and linker script, aiming to get you to compile your first ``.bin`` firmware file.
