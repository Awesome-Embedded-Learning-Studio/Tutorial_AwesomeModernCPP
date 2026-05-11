# Part 2: Project Structure — Fetching the HAL Library, Startup File Pitfalls, and Directory Setup

> In the previous part, we installed the toolchain. Now let's set up the project skeleton. This post documents my entire process of fetching the STM32 HAL library, including that baffling nested submodule issue, the hidden logic behind startup file naming conventions, and the hidden pitfalls in `stm32f1xx_hal_conf.h` that will crash your build halfway through.

---

## Why This Step Matters

You might ask, isn't it just a project structure? Can't we just create a few folders, toss the HAL library in, and call it a day? Not quite. The STM32 HAL library has its own "ecosystem" — the CMSIS core layer, the HAL driver layer, startup code, and linker scripts. These must be organized in a specific way, or the compiler won't know where to find the header files, and the linker won't know where to place the code in memory.

To make matters worse, ST's official HAL library is published via a Git repository, and it contains nested submodules. If you clone it the usual way, chances are you'll miss critical files. When your build fails halfway through complaining about a missing header file, tracing back to find the root cause is incredibly painful. I've stumbled on this myself, so in this post, I'll flag all the pitfalls upfront so we can get the project skeleton right on the first try.

---

## Understanding the Three-Layer Architecture of the HAL Library

Before we download any code, it's worth understanding how ST's HAL library is layered. This will help you understand why we need to create those directories and what each file does.

At the bottom is **CMSIS-Core (Cortex Microcontroller Software Interface Standard)**. This is a standard defined by ARM that specifies register access interfaces for the Cortex-M series cores. Simply put, CMSIS-Core tells you "this chip has a register called SCB at address 0xE000ED00," so you can manipulate registers using `SCB->AIRCR = ...` instead of memorizing magic numbers. CMSIS-Core is maintained by ARM and is common to all Cortex-M chips.

The middle layer is **CMSIS-Device**. This is ST's specialization for the STM32F1 series. It defines what peripherals the specific F103C8T6 chip has, how many of each peripheral exist, and where their register addresses are. For example, the base address of `GPIOA` is `0x40010800`, and this information lives in the CMSIS-Device header files. You'll see a bunch of `stm32f103xb.h` files later — they belong to this layer.

The top layer is the **HAL driver layer**. This is a set of peripheral driver APIs written in C by ST, such as `HAL_GPIO_WritePin()` and `HAL_UART_Transmit()`. Their purpose is to abstract away low-level register operations, letting you interact with different STM32 series in a uniform way. In theory, code you write using HAL should require only minor configuration changes when porting to an STM32F4.

Above that is your application code. The application code calls HAL APIs, HAL calls CMSIS-Device definitions, and CMSIS-Device depends on the CMSIS-Core kernel interfaces. Once you understand this layering, you'll know why we need so many directories — each layer has its own dedicated folder.

---

## Fetching the HAL Library: The Submodule Trap

Alright, let's fetch the code. ST's official STM32F1 HAL library is hosted on GitHub at `https://github.com/STMicroelectronics/STM32CubeF1.git`. Your first instinct might be to simply `git clone` it, but there's a catch here. Let me walk you through it step by step.

First, let's create our project root directory. I like to keep all dependencies under a `vendor/` directory for a clean project structure:

```bash
mkdir -p embedded-stm32/vendor
cd embedded-stm32
```

Now let's clone the HAL library. Here's a mistake beginners make most often — doing a shallow clone with `--depth 1`:

```bash
git submodule add --depth 1 https://github.com/STMicroelectronics/STM32CubeF1.git vendor/STM32CubeF1
```

This command looks reasonable: it adds the library as a submodule, and `--depth 1` fetches only the latest version to save time. But the problem is that the STM32CubeF1 repository itself has internal submodules (the CMSIS library is brought in as a submodule), and `--depth 1` prevents nested submodules from being initialized correctly.

When you check the directory structure later, you'll notice a bizarre phenomenon:

```text
$ ls vendor/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

Normally, this directory should contain a bunch of startup files (like `startup_stm32f103xb.s`), but if you used a shallow clone, it will be empty. During compilation, you'll see an error like this:

```text
fatal error: startup_stm32f103xb.s: No such file or directory
```

When you go back to investigate why the files are missing, you'll be completely baffled — the submodule was clearly added, so why are the files still missing?

The reason lies in Git's submodule mechanism. When you clone a repository containing submodules, Git only fetches the outer repository's content. The submodule directories inside are just "pointers" referencing a specific commit in another repository. You need to explicitly run `git submodule update --init --recursive` for Git to actually fetch those nested submodule contents. The `--depth 1` shallow clone breaks this mechanism because the history of nested submodules isn't fully fetched.

The correct approach is to do a full clone, then recursively initialize all submodules:

```bash
git submodule add https://github.com/STMicroelectronics/STM32CubeF1.git vendor/STM32CubeF1
cd vendor/STM32CubeF1
git submodule update --init --recursive
```

If you've already added the submodule to your project but forgot to use `--recursive`, you can fix it:

```bash
cd vendor/STM32CubeF1
git submodule update --init --recursive
```

This command recursively fetches all nested submodules, ensuring the CMSIS Device directory files are complete. You can verify with the `ls` command we used earlier to check if the startup files have appeared:

```bash
ls vendor/STM32CubeF1/Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/
```

You should see output like this:

```text
startup_stm32f100xb.s  startup_stm32f103xe.s  startup_stm32f105xc.s
startup_stm32f101xe.s  startup_stm32f103xg.s  startup_stm32f107xc.s
startup_stm32f101x6.s  startup_stm32f103xg.s  startup_stm32f107xc.s
startup_stm32f102x6.s  startup_stm32f103xb.s
startup_stm32f103x6.s  startup_stm32f103xd.s
```

Seeing these `startup_stm32f103*.s` files means the submodule was fetched successfully. By the way, if you're on Arch Linux, your system might not have `git` preinstalled, so you'll need to run `sudo pacman -S git` first; Ubuntu users typically have git installed by default.

---

## The Hidden Logic Behind Startup File Naming

Now we have the startup files, but a new problem arises — which one should we use?

Here's a detail that trips up countless beginners. Many online tutorials reference `startup_stm32f103c8.s`, but if you look closely at the `ls` output from earlier, you'll notice there is no such file! ST's official filename is `startup_stm32f103xb.s`.

Behind this discrepancy lies ST's chip naming convention. Let me explain: what does the "C8" in the F103C8T6 model number mean? C stands for Low-density, and 8 represents 64KB Flash. But ST's startup file naming convention isn't based on Flash size — it's based on "density category":

- `ld` = Low-density devices (16-32KB Flash)
- `md` = Medium-density devices (64-128KB Flash)
- `hd` = High-density devices (256-512KB Flash)
- `xl` = XL-density devices (768KB-1MB Flash)

The F103C8T6 has 64KB Flash, which falls under medium-density, so the corresponding startup file is `startup_stm32f103xb.s`. The "B" here isn't the hexadecimal for 8, but rather an internal density code used by ST.

Correspondingly, for the compile-time macro definition, you need to pass `STM32F103xB` (note the uppercase B). Many tutorials incorrectly write `STM32F103xC8`, which causes the conditional compilation in the header files to select the wrong branch, resulting in code that doesn't match your hardware.

You might ask, why does ST use such a complex naming scheme? Historical reasons. The STM32F1 series was ST's first Cortex-M3 product line, and at the time, they divided it into several tiers based on Flash capacity. F103xB covers both the 64KB and 128KB versions, which are hardware-identical apart from Flash size, so they share the same startup file and header files.

So what does the startup file actually do? Simply put, it's the first piece of code executed after the chip resets. When the STM32 powers on or resets, the CPU reads the "initial stack pointer" from address 0x00000000, then reads the "reset vector" (Reset Handler) from 0x00000004 and jumps there to execute. The startup file defines this Vector Table, which contains the entry addresses for all interrupts and exceptions. It also handles initializing the `.data` section (copying initial values from Flash to RAM) and zeroing the `.bss` section, before finally jumping to your `main()` function. Without the startup file, the chip wouldn't know what to do after a reset, and the program couldn't run.

---

## Project Directory Structure

Now that we have the HAL library and understand the startup files, let's set up a clean project structure. I recommend this layout:

```text
embedded-stm32/
├── CMakeLists.txt
├── src/
│   ├── main.c
│   ├── stm32f1xx_hal_conf.h
│   └── stm32f1xx_it.c
├── vendor/
│   └── STM32CubeF1/
├── build/
└── ld/
    └── STM32F103C8Tx_FLASH.ld
```

Let me explain the purpose of each directory:

`vendor/STM32CubeF1/` is the HAL library we just cloned. You don't need to manually modify anything in this directory — just reference it. The CMSIS and HAL_Driver inside will be added to the compilation path through CMake's `target_include_directories`.

`src/` holds your application code. `main.c` is the program entry point, `stm32f1xx_hal_conf.h` is the HAL library configuration file (we'll dive into its pitfalls below), and `stm32f1xx_it.c` is for interrupt service routines. Certain HAL peripherals (like UART) require user-defined interrupt handler functions, and these go in `stm32f1xx_it.c`.

`build/` is the CMake output directory. We use an "out-of-source" build approach to avoid polluting the source directory with generated files. Build artifacts (`.o` files, `.elf`, `.hex`, `.bin`) will all end up here.

`ld/` stores the linker script. We'll cover how to write this file in detail in the next post; for now, just know that it defines the memory layout.

You might notice I used `STM32F103C8Tx_FLASH.ld` as the linker script name. There's no hard rule for this naming, but I like to embed the chip model in the filename so I can tell at a glance which chip it's for. The only difference between the F103C8 and F103CB (128KB version) is the Flash size — you just change the `LENGTH` parameter in the linker script, and everything else stays the same.

---

## stm32f1xx_hal_conf.h: The Hidden Pitfalls

Now we arrive at the first "minefield" — the HAL configuration file. ST's official HAL library doesn't include a ready-to-use `stm32f1xx_hal_conf.h`; it only provides a `stm32f1xx_hal_conf_template.h` template. You need to copy the template into your project, rename it, and modify it.

Why not use CubeMX? If you use ST's STM32CubeMX graphical tool to generate a project, it automatically generates this file for you. But since we're taking the "pure hand-written CMake" route, we have to handle it manually.

First, copy the template over:

```bash
cp vendor/STM32CubeF1/Drivers/STM32F1xx_HAL_Driver/Inc/stm32f1xx_hal_conf_template.h src/stm32f1xx_hal_conf.h
```

Then open the file in your editor and start modifying. The first pitfall is **module selection**. Near the top of the file, there's a huge block of `#define HAL_XXX_MODULE_ENABLED`, with all modules enabled by default. This causes all HAL drivers to be compiled in, bloating the firmware size significantly. For our LED blink program, we only need to enable these modules:

```c
#define HAL_GPIO_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
```

Comment out the `#define` for all other modules. This way, the compiler only compiles the HAL functions you need, and the linker can do a better job of dead code elimination.

The second pitfall is **clock macro definitions**. Scroll down a few lines and you'll see a bunch of macros like `HSE_VALUE`, `HSI_VALUE`, and `LSI_VALUE`. These represent external/internal crystal frequencies, and the HAL library's RCC module needs to know these frequencies to calculate the system clock.

The most critical one is `HSE_VALUE`. In the template file, this macro is conditionally defined with `#if !defined(HSE_VALUE)`. If you don't define this macro, compiling certain HAL modules (like RTC or the watchdog) will throw errors:

```text
error: 'HSE_VALUE' undeclared (first use in this function)
```

The solution is simple: make sure all clock macros are defined in `stm32f1xx_hal_conf.h`. The Blue Pill board typically uses an 8MHz external crystal (HSE), the internal high-speed oscillator (HSI) is 8MHz, the internal low-speed oscillator (LSI) is approximately 40kHz, and the external low-speed crystal (LSE) is usually 32.768kHz (if present on the board). Write them all out:

```c
#define HSE_VALUE    8000000U
#define HSI_VALUE    8000000U
#define LSI_VALUE    40000U
#define LSE_VALUE    32768U
```

Note that the unit is Hertz, and the uppercase `U` suffix denotes an "unsigned integer." Getting these values right matters a lot — if `HSE_VALUE` is wrong, the system clock frequency calculated by RCC will be off, the UART baud rate will be wrong as well, and your serial output will be garbled.

The third pitfall is the **assert_param macro**. Near the end of the file, there's a macro definition like this:

```c
#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif
```

The HAL library uses `assert_param` everywhere to check whether function parameters are valid. For example, if you call `HAL_GPIO_WritePin()` with an invalid pin number, the assert will catch the error. If you define `USE_FULL_ASSERT`, a failed assert jumps to the `assert_failed()` function (which you need to implement yourself); otherwise, it does nothing (empty macro).

Many beginners forget to define `USE_FULL_ASSERT`, leading to compilation errors about an "undefined macro." The fix: either add the code block above in `stm32f1xx_hal_conf.h` (it's already in the template, just make sure it's not commented out), or add `-DUSE_FULL_ASSERT` in CMake.

The fourth pitfall is the **module callback macros**. The second half of the file has a bunch of `HAL_XXX_REGISTER_CALLBACKS` definitions. These enable HAL's "callback registration" feature (a more flexible approach to interrupt handling). The default value is 0, and for simple applications, keeping it at 0 is fine. If you change it to 1, you'll need to implement callback functions for each peripheral, increasing code complexity.

One last detail: `stm32f1xx_hal_conf.h` must be findable by the HAL library header files. The usual approach is to place it in the `src/` directory, then add `src/` to the include path via CMake's `target_include_directories`. Alternatively, you can place it directly in the project root and specify it at compile time with `-I.`. The HAL library header files reference it via `#include "stm32f1xx_hal_conf.h"` (note the quotes, not angle brackets), so it must be in the search path.

---

## The Template File Pitfall: A Preview

Before we wrap up, I want to give an early warning about a pitfall we'll encounter in the CMake post. If you feed the entire `Src/` directory of the HAL library directly to CMake for compilation, you'll get an error like this:

```text
multiple definition of 'SystemInit'
```

This is because the HAL library contains several `template` files, such as `system_stm32f1xx.c`. These template files aren't meant to be compiled directly — they're meant for you to copy into your project and modify for your own implementation. If you compile them as-is, they'll conflict with your own implementation (both files define `SystemInit`).

The solution is to use CMake's `list(FILTER ... EXCLUDE ...)` to remove these template files from the source file list. I'll save the specific CMake syntax for the next post; for now, you just need to know: don't blindly add all `.c` files from the HAL library into compilation — the ones with the `template` suffix must be excluded.

---

## Where We Are Now

In this post, we finished setting up the project structure. You should now have:

1. A correctly cloned HAL library (with all submodules initialized)
2. The knowledge that F103C8T6 requires the `startup_stm32f103xb.s` startup file and the `STM32F103xB` macro
3. A clean project directory layout
4. A properly configured `stm32f1xx_hal_conf.h` (clock macros and module selection are all set)

But we're not done yet. In the next post, we'll cover the linker script and CMake configuration — that's what will actually get the code to compile. The linker script needs to tell the linker that the STM32F103C8T6's Flash starts at 0x08000000 with a size of 64KB, and RAM starts at 0x20000000 with a size of 20KB. If you get this file wrong, the program will compile but won't run, because the code will be placed at the wrong memory addresses.

Before that, you can go ahead and set up the project structure and copy and modify `stm32f1xx_hal_conf.h`. In the next article, we'll write the CMakeLists.txt and linker script, aiming to get you to compile your first `.elf` firmware file.
