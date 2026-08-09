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
title: 'Part 2: Project Structure — HAL Library Acquisition, Startup File Pitfalls,
  and Directory Setup'
description: ''
translation:
  source: documents/vol8-domains/embedded/00-env-setup/02-project-structure.md
  source_hash: 752222e41e3368df5baface691076731d58f9822bb4066f2a6e9e341bb368e19
  translated_at: '2026-06-16T04:08:51.586544+00:00'
  engine: anthropic
  token_count: 2365
---
# Part 2: Project Structure — HAL Library Acquisition, Startup File Pitfalls, and Directory Setup

> In the previous part, we got the toolchain ready. Now, let's build the project skeleton. This post documents my entire process of obtaining the STM32 HAL library, including that baffling nested submodule issue, the hidden logic behind startup file naming conventions, and those hidden traps in `stm32f1xx_hal_conf.h` that will cause your compilation to fail halfway through.

---

## Why This Step Is Important

You might ask, isn't a project structure just a matter of creating a few folders and tossing the HAL library in? Not really. The STM32 HAL library has its own "ecosystem" — the CMSIS core layer, the HAL driver layer, startup files, and linker scripts. These must be organized in a specific way, or the compiler won't know where to find header files, and the linker won't know where to place the code in memory.

To make matters worse, ST's official HAL library is released via a Git repository that contains nested submodules. If you clone it using the standard method, you will almost certainly miss key files. When your compilation fails halfway through because it can't find a header file, troubleshooting becomes very painful. I've stumbled here myself, so in this post, I will mark all the pitfalls in advance so you can get the project skeleton right on the first try.

---

## Understand the Three-Layer Architecture of the HAL Library

Before we download the code, it is necessary to understand how ST's HAL library is designed in layers. This will help you understand why we need to create those directories and what each file does.

At the bottom is **CMSIS-Core (Cortex Microcontroller Software Interface Standard)**. This is a standard defined by ARM that specifies the register access interface for Cortex-M series cores. Simply put, CMSIS-Core tells you "this chip has a register called SCB at address 0xE000ED00," so you can use `SCB->VTOR` in your code to manipulate registers instead of memorizing magic numbers. CMSIS-Core is maintained by ARM and is common to all Cortex-M chips.

The middle layer is **CMSIS-Device**. This part is ST's specialization for the STM32F1 series. It defines what peripherals the specific F103C8T6 chip has, how many of each peripheral exist, and where their register addresses are. For example, the fact that the base address of `GPIOA` is `0x40010800` is written in the CMSIS-Device header files. You will later see a bunch of `stm32f1xx.h` files; they belong to this layer.

The top layer is the **HAL Driver Layer**. This is a set of peripheral driver APIs written in C by ST, such as `HAL_GPIO_WritePin` and `HAL_UART_Transmit`. Their purpose is to shield low-level register operations, allowing you to operate different STM32 series in a unified way. Theoretically, code you write with HAL should require only minor configuration changes to port to an STM32F4.

Above that lies your application code. The application code calls HAL APIs, HAL calls definitions from CMSIS-Device, and CMSIS-Device relies on CMSIS-Core's kernel interfaces. Once you understand this layering, you will know why so many directories are needed — each layer has its own dedicated folder.

---

## Acquiring the HAL Library: The Submodule Trap

Alright, let's get the code. ST's official STM32F1 HAL library is hosted on GitHub at `https://github.com/STMicroelectronics/STM32CubeF1.git`. Your first instinct might be to simply `git clone`, but there is a trap here. Let me walk you through it.

First, create our project root directory. I like to keep all dependencies in a `lib` directory for a clean project structure:

```bash
mkdir stm32-project && cd stm32-project
mkdir lib
```

Now let's clone the HAL library. Here is a mistake beginners often make — doing a shallow clone with `--depth 1`:

```bash
# ❌ WRONG: Shallow clone
git submodule add --depth 1 https://github.com/STMicroelectronics/STM32CubeF1.git lib/STM32CubeF1
```

This command looks reasonable: using submodule to add the library and `--depth 1` to fetch only the latest version to save time. However, the problem is that the STM32CubeF1 repository has its own submodules (the CMSIS library is included as a submodule), and `--depth 1` prevents nested submodules from being initialized correctly.

When you check the directory structure later, you will notice a strange phenomenon:

```bash
ls lib/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

Normally, this directory should be full of startup files (like `startup_stm32f103xb.s`), but if you used a shallow clone, it will be empty. During compilation, you will see an error like this:

```text
arm-none-eabi-gcc: error: lto1: not found
```

When you investigate why the files are missing, you will be confused — the submodule is added, so why are the files still missing?

The reason lies in Git's submodule mechanism. When you clone a repository containing submodules, Git only pulls the content of the outer repository. The submodule directories inside are just "pointers" pointing to a specific commit in another repository. You need to explicitly run `git submodule update --init --recursive` to make Git actually fetch the content of those nested submodules. A `--depth 1` shallow clone breaks this mechanism because the history of nested submodules hasn't been fully pulled.

The correct way is to do a full clone and then recursively initialize all submodules:

```bash
# ✅ CORRECT: Full clone with recursive submodules
git submodule add https://github.com/STMicroelectronics/STM32CubeF1.git lib/STM32CubeF1
cd lib/STM32CubeF1
git submodule update --init --recursive
```

If you have already added the submodule but forgot to use `--recursive`, you can fix it:

```bash
cd lib/STM32CubeF1
git submodule update --init --recursive
```

This command recursively pulls all nested submodules, ensuring the CMSIS Device directory files are complete. You can verify with the `ls` command used earlier to see if the startup files have appeared:

```bash
ls lib/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

You should see output similar to this:

```text
startup_stm32f100xb.s  startup_stm32f101xe.s  startup_stm32f103xb.s
startup_stm32f102xb.s  startup_stm32f103x6.s  startup_stm32f103xe.s
...
```

Seeing these `.s` files means the submodule pull was successful. By the way, if you are using Arch Linux, your system might not have `perl` pre-installed, so you need to `sudo pacman -S perl` first; Ubuntu users usually have git by default.

---

## The "Metaphysics" of Startup File Naming

Now we have the startup files, but a new problem arises — which one should we use?

Here is a detail that trips up countless beginners. Many online tutorials write `startup_stm32f103x8.s`, but if you look closely at the output from `ls` just now, you will find that file doesn't exist! ST's official filename is `startup_stm32f103xb.s`.

Behind this difference lies ST's chip naming rules. Let me explain: What does the "C8" in F103C8T6 stand for? C stands for Low-density, and 8 represents 64KB Flash. However, ST's startup file naming rules are not based on Flash size, but on "density category":

- `xl` = XL-density devices (Extra high-density, 768KB-1MB Flash)
- `ld` = Low-density devices (Small capacity, 16-32KB Flash)
- `md` = Medium-density devices (Medium capacity, 64-128KB Flash)
- `hd` = High-density devices (Large capacity, 256-512KB Flash)

F103C8T6 has 64KB Flash, which belongs to the medium-density category, so the corresponding startup file is `startup_stm32f103xb.s`. Here, "B" is not the hexadecimal for 8, but a density code used internally by ST.

Corresponding to compile-time macros, you need to pass `STM32F103xB` (note the uppercase B). Many tutorials mistakenly write `STM32F103x8`, which causes the conditional compilation in the header files to select the wrong branch, resulting in code that doesn't match your hardware.

You might ask, why does ST make the naming so complex? Historical reasons. The STM32F1 series was ST's first Cortex-M3 product line. At that time, they divided products into several tiers based on Flash capacity. F103xB covers both the 64KB and 128KB versions. Apart from Flash size, the hardware is virtually identical, so they use the same set of startup files and header files.

So what does the startup file actually do? Simply put, it is the first piece of code executed after the chip resets. When the STM32 powers up or resets, the CPU reads the "Initial Stack Pointer" from address 0x00000000, then reads the "Reset Vector" from 0x00000004 and jumps there to execute. The startup file defines this Vector Table, which contains the entry addresses for all interrupts and exceptions. It is also responsible for initializing the `.data` section (copying initial values from Flash to RAM) and zeroing the `.bss` section, finally jumping to your `SystemInit` and `main` functions. Without the startup file, the chip doesn't know what to do after a reset, and the program cannot run.

---

## Project Directory Structure

Now that we have the HAL library and figured out the startup files, let's build a clear project structure. I recommend this layout:

```text
stm32-project/
├── CMakeLists.txt
├── build/               # Build output directory
├── lib/
│   └── STM32CubeF1/     # HAL library (submodule)
└── src/
    ├── main.c
    ├── stm32f1xx_hal_conf.h
    ├── stm32f1xx_it.c
    └── stm32f1xx_it.h
```

Let me explain the role of each directory:

`lib/STM32CubeF1` is the HAL library we just cloned. You don't need to modify this directory manually; just reference it. The CMSIS and HAL_Driver inside it will be added to the compilation path via CMake's `target_include_directories`.

`src` stores your application code. `main.c` is the program entry. `stm32f1xx_hal_conf.h` is the HAL library configuration file (I'll detail this pitfall below). `stm32f1xx_it.c` and `stm32f1xx_it.h` are interrupt service routines. Some HAL peripherals (like UART) require the user to define interrupt handling functions, which are written in `stm32f1xx_it.c`.

`build` is the output directory for CMake. We use an "out-of-source" build method to avoid polluting the source directory with generated files. Build artifacts (`*.o`, `*.elf`, `*.hex`) will be placed here.

`linker/ stores linker scripts. We will cover how to write this file in detail in the next post; for now, just know it defines the memory layout.

You might notice I used `STM32F103C8.ld` as the linker script name. There is no hard rule for this naming, but I habitually write the chip model into the filename so I can see at a glance which chip it's for. The difference between F103C8 and F103CB (128KB version) is only in Flash size; you just need to change the `LENGTH` parameter in the linker script, everything else is the same.

---

## stm32f1xx_hal_conf.h: The Hidden Pitfalls

Now we arrive at the first "minefield" — the HAL configuration file. ST's official HAL library does not contain a ready-to-use `stm32f1xx_hal_conf.h`, only a `stm32f1xx_hal_conf_template.h` template. You need to copy the template into your project, rename it, and modify it.

Why not use CubeMX? If you use ST's graphical tool STM32CubeMX to generate a project, it will generate this file for you automatically. But since we are taking the "pure handwritten CMake" route, we must handle it manually.

First, copy the template over:

```bash
cp lib/STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_conf_template.h src/stm32f1xx_hal_conf.h
```

Then open this file with an editor and start modifying. The first pitfall is **Module Selection**. At the beginning of the file, there is a huge list of `HAL_MODULE_ENABLED`, and all modules are enabled by default. This causes all HAL drivers to be compiled, making the firmware size bloated. For our LED blinking program, we only need to enable these modules:

```c
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
```

Comment out the `HAL_XXX_MODULE_ENABLED` for all other modules. This way, the compiler only compiles the HAL functions you need, and the linker can better perform dead code elimination.

The second pitfall is **Clock Macro Definitions**. Scroll down a few lines, and you will see a bunch of macros like `HSE_VALUE`, `HSI_VALUE`, `LSE_VALUE`. These are external/internal crystal frequencies, and the HAL library's RCC module needs to know these frequencies to calculate the system clock.

The most critical one is `HSE_VALUE`. This macro is conditionally defined with `#if !defined (HSE_VALUE)` in the template file. If you don't define this macro, compiling certain HAL modules (like RTC or watchdog) will result in an error:

```text
error: "HSE_VALUE" is not defined
```

The solution is simple: ensure all clock macros are defined in `stm32f1xx_hal_conf.h`. The Blue Pill board usually uses an 8MHz external crystal (HSE), the internal high-speed oscillator (HSI) is 8MHz, the internal low-speed oscillator (LSI) is approximately 40kHz, and the external low-speed crystal (LSE) is usually 32.768kHz (if present on the board). Write them all down:

```c
#if !defined  (HSE_VALUE)
  #define HSE_VALUE    8000000U  /*!< Value of the External oscillator in Hz */
#endif

#if !defined  (HSI_VALUE)
  #define HSI_VALUE    8000000U  /*!< Value of the Internal oscillator in Hz*/
#endif

#if !defined  (LSE_VALUE)
  #define LSE_VALUE    32768U    /*!< Value of the External Low Speed oscillator in Hz */
#endif

#if !defined  (LSI_VALUE)
  #define LSI_VALUE    40000U    /*!< Value of the Internal Low Speed oscillator in Hz*/
#endif
```

Note the unit is Hertz, using the uppercase `U` suffix for "unsigned integer". The correctness of these values matters greatly — if `HSE_VALUE` is wrong, the system clock frequency calculated by RCC will be wrong, and the UART baud rate will also be wrong, resulting in garbled serial output.

The third pitfall is the **assert_param Macro**. Near the end of the file, there is such a macro definition:

```c
  #ifdef  USE_FULL_ASSERT
    #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  #else
    #define assert_param(expr) ((void)0U)
  #endif
```

The HAL library uses `assert_param` everywhere to check if function parameters are valid. For example, if you call `HAL_GPIO_WritePin` and pass an invalid pin number, the assert will catch this error. If you defined `USE_FULL_ASSERT`, assert failure jumps to the `assert_failed` function (which you need to implement yourself), otherwise it does nothing (empty macro).

Many beginners forget to define `USE_FULL_ASSERT`, leading to compilation errors saying "undefined macro". The solution: either add the code above in `stm32f1xx_hal_conf.h` (it's already in the template, just make sure it's not commented out), or add `USE_FULL_ASSERT` in CMake.

The fourth pitfall is the **Module Callback Macros**. In the second half of the file, there are a bunch of `USE_HAL_XXX_SUBMODULE_CALLBACKS`. These are to enable HAL's "callback registration" feature (a more flexible interrupt handling method). The default is 0; keeping it at 0 is fine for simple applications. If you change it to 1, you need to implement callback functions for each peripheral, increasing code complexity.

Finally, there is a detail: `stm32f1xx_hal_conf.h` must be findable by the HAL library header files. The usual practice is to put it in the `src` directory, then add `src` to the include path via CMake's `target_include_directories`. Or you can put it directly in the project root and specify it with `-I` during compilation. The HAL library header files will reference it via `#include "stm32f1xx_hal_conf.h"` (note the quotes, not angle brackets), so it must be in the search path.

---

## The Template File Trap: A Preview

Before finishing, I want to give an early warning about a pitfall you will encounter in the CMake section. If you throw the entire `Src` directory of the HAL library at CMake to compile, you will get an error like this:

```text
multiple definition of 'SystemInit'; ...
```

This is because there are several `template` files in the HAL library, such as `stm32f1xx_hal_conf_template.c`. These template files are not meant to be compiled directly; they are for you to copy into your project and modify into your own implementation. If you compile them as well, they will conflict with your implementation (both files define `SystemInit`).

The solution is to use `list(FILTER ... EXCLUDE ...)` in CMake to exclude these template files from the source list. I'll leave the specific CMake syntax for the next post; for now, you just need to know: don't blindly add all `.c` files from the HAL library to the compilation; those with the `_template` suffix must be excluded.

---

## Where Are We Now

In this post, we completed the project structure setup. You should now have:

1. A correctly cloned HAL library (submodules initialized).
2. Knowledge that F103C8T6 uses the `startup_stm32f103xb.s` file and the `STM32F103xB` macro.
3. A clear project directory layout.
4. A configured `stm32f1xx_hal_conf.h` (clock macros and module selection are correct).

But we aren't done yet. The next post will cover linker scripts and CMake configuration, which is the key to actually getting the code to compile. The linker script needs to tell the linker that STM32F103C8T6's Flash starts at 0x08000000 with a size of 64KB, and RAM starts at 0x20000000 with a size of 20KB. If you write this file incorrectly, the program will compile successfully but won't run because the code is placed at the wrong memory addresses.

Before that, you can set up the project structure and copy/modify `stm32f1xx_hal_conf.h`. In the next article, we will start writing `CMakeLists.txt` and the linker script, aiming to get you to compile your first `.hex` firmware file.
