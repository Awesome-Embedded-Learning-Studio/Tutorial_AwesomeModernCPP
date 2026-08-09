---
title: "Embedded clangd: making VS Code understand cross-compiled code"
description: "Port the host-platform clangd you set up in three steps to an arm-none-eabi-g++ cross project and the editor drowns you in red squiggles. This piece digs into the root cause and hands you a copy-pasteable query-driver and .clangd config."
chapter: 14
order: 6
platform: stm32f1
difficulty: intermediate
cpp_standard: [17, 20]
tags:
  - stm32f1
  - 嵌入式
  - intermediate
  - clangd
  - 交叉编译
reading_time_minutes: 16
prerequisites:
  - "Chapter 14: 第1篇 从零搭建 STM32 开发工具链"
  - "Chapter 14: CMake 配置篇"
related:
  - "让 vscode 看懂您的代码——装 clangd，红线消失"
  - "交叉编译和CMake简单指南"
---

# Embedded clangd: making VS Code understand cross-compiled code

## Opening

In the getting-started volume, piece 5, we set up clangd for the host platform, and the flow was short: ditch Microsoft's C/C++ extension, install the clangd extension, feed it `compile_commands.json`, and your code gets smart—jump-to-definition and completion all in one go. The end of that piece hammered one point home: clangd "understands" your code because of what it reads from `compile_commands.json`, every single compile command—which compiler, which flags, where `-I` points, what the target platform is.

Take that same setup into an embedded project, and odds are you'll be staring at the screen within seconds.

Open `main.c`, and the very first line `#include "stm32f1xx.h"` gets a red squiggly. `HAL_GPIO_WritePin` and the rest of the HAL functions are nowhere to be found. `stdint.h`, `core_cm3.h`, one after another, all painted red—clangd looks blind. Meanwhile `cd build && ninja` builds fine, you flash the board, and the LED blinks. The compiler clearly knows about these headers. Why doesn't clangd?

This piece is the cure. We'll tear the root cause apart, then hand you a config you can copy verbatim. The repo's `code/stm32f1-tutorials/*/.vscode/settings.json` has been using this setup the whole time, but there's never been a doc explaining what it actually does. This is that doc.

## Why everything goes red: clangd is making paths up

First, recall why the host setup works. In a host project, the compile command clangd sees looks like this:

```text
/usr/bin/g++ -std=c++20 -I/home/you/proj/include main.cpp
```

The compiler is `g++`, and clangd can work with that command because it knows `g++`'s header layout inside out—`/usr/include/c++/14`, `/usr/include`, that whole set of standard paths is baked in, ready to use.

Move to an embedded project, and the compile command clangd reads from `compile_commands.json` becomes this:

```json
{
  "directory": "/home/you/proj/build",
  "command": "/usr/sbin/arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb -I.../Drivers/CMSIS/Device/ST/STM32F1xx/Include main.cpp -c -o CMakeFiles/main.dir/main.cpp.o",
  "file": "../main.cpp"
}
```

Notice the compiler changed from `g++` to `arm-none-eabi-g++`. Here's the catch: clangd is built on clang, and it **does not know where this GNU cross-compiler keeps its headers internally**. What it knows about GCC's built-in path layout, it inferred from the local `g++`. ARM's newlib headers, ARM's libstdc++ headers, the CMSIS `core_cm3.h`—none of it is on its radar.

So what does it do? Since version 14, for a compiler it "doesn't recognize," clangd falls back to a fictional toolchain called **BareMetal** (target `arm-none-eabi`). This fictional toolchain fabricates a pile of paths that typically look like this:

```text
clang-runtimes/arm-none-eabi/include
clang-runtimes/arm-none-eabi/include/c++
clang-runtimes/arm-none-eabi/share
```

Go `find` that under the repo root, under `/usr/lib`, anywhere—`clang-runtimes/arm-none-eabi` doesn't exist. It's a path clangd hallucinated as "where this ought to live." The system headers `stdint.h`, the CMSIS header `core_cm3.h`—they aren't in these phantom paths, so clangd can't find them, and everything goes red.

::: warning The disease isn't that clangd is dumb
The root cause is that clangd **never asked the actual cross-compiler** where its headers live. It's overlaying its own built-in, clang-runtime-based guess onto a GCC toolchain, and GCC's header layout is a completely different beast from the clang runtime directory. The guess is wrong, the paths are empty, the headers are gone.
:::

One more knife-twist. Even if clangd guessed the paths, it still wouldn't know the cross-compiler's built-in macros. Let's check what `arm-none-eabi-g++` predefines when no `-mcpu` is given:

```bash
$ arm-none-eabi-g++ -E -dM -xc++ /dev/null | grep -E "__ARM_ARCH|__arm__|__thumb__"
#define __ARM_ARCH_ISA_ARM 1
#define __ARM_ARCH_ISA_THUMB 1
#define __ARM_ARCH_4T__ 1
#define __ARM_ARCH 4
#define __arm__ 1
```

Notice it defaults to `__ARM_ARCH_4T__`, ARMv4, not the ARMv7-M that Cortex-M3 actually is. Cortex-M3 is ARMv7-M, Thumb-2 only—nothing like this default target. Headers like `core_cm3.h` and `cmsis_gcc.h` branch on macros like `__ARM_ARCH_7M__` to pick a code path. clangd parses without those macros, gets a result that doesn't match what the real compiler produces, and trips the occasional `#error` inside a header. The screen gets even redder.

## query-driver: make clangd actually ask the compiler

clangd has a mechanism that's exactly the cure for this. It's called **query-driver**.

The principle is blunt: instead of guessing, clangd **actually executes the compiler you specified**, running this command:

```bash
arm-none-eabi-g++ -E -xc++ -v /dev/null
```

That tells the cross-compiler to preprocess an empty file and print verbose info. GCC dumps its internal header search paths and built-in macro definitions to stderr. Let's run it locally (`arm-none-eabi-g++ 16.1.0`):

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

These paths **actually exist**. `/usr/arm-none-eabi/include` is newlib's C headers, `/usr/.../include/c++/16.1.0` is the libstdc++ that ships with newlib. clangd grabs these as system headers, pairs them with the `-mcpu=cortex-m3 -mthumb -I.../Drivers/...` it read from `compile_commands.json`, feeds all of that to its internal clang, and the code parses correctly.

::: warning Why it's off by default
query-driver means letting clangd **execute an arbitrary binary**. Picture this: you clone a project of unknown provenance, its `.clangd` says `Compiler: /tmp/evil.sh`, and clangd happily runs that thing as a "compiler" the moment it starts. That can't happen silently. So clangd refuses query-driver by default; you have to explicitly allowlist which compiler paths may be executed. This is a security call, not a bug.
:::

### The three-piece config

To make query-driver actually take effect, three places have to be set up together. Let's go one by one.

### Piece one: add --query-driver to VS Code's clangd.arguments

Open the project's `.vscode/settings.json` and add the `--query-driver` argument:

```json
{
    "clangd.arguments": [
        "--query-driver=/usr/sbin/arm-none-eabi-g++,/usr/sbin/arm-none-eabi-gcc"
    ]
}
```

After the equals sign comes a **comma-separated list of absolute paths**, and globs (`*`, `?`) are supported. clangd will only execute compilers whose path matches one of these globs; everything else is refused. Here we've allowlisted `arm-none-eabi-g++` and `arm-none-eabi-gcc`, covering both C++ projects and pure-C projects.

::: warning Use absolute paths here, not command names
`--query-driver` has to be absolute paths or globs over absolute paths. Writing `--query-driver=arm-none-eabi-g++` does nothing—clangd doesn't search `PATH`; it just decides there's no match and refuses to run. Where the toolchain lives on your machine varies, so adjust the path (we'll get to why the repo uses `/usr/sbin/` below).
:::

### Piece two: CompileFlags.Compiler and BuiltinHeaders in the project-root .clangd

Allowlisting alone isn't enough. clangd also needs to know "this project should be parsed with `arm-none-eabi-g++`," and "its built-in headers come from query-driver, not from clangd's own." Create a `.clangd` file at the project root:

```yaml
CompileFlags:
  Compiler: arm-none-eabi-g++
  Add:
    - -mcpu=cortex-m3
    - -mthumb
  BuiltinHeaders: QueryDriver
```

Line by line, here's what these four do.

`Compiler: arm-none-eabi-g++` tells clangd: in this project's compile commands, **replace** the executable with `arm-none-eabi-g++` (any name resolvable on PATH works; absolute path not required). That way, even if `compile_commands.json` says something else (a relative path CMake produced, say), clangd forces the cross-compiler.

`Add` **appends** these flags to every compile command. Embedded projects usually already have `-mcpu=cortex-m3 -mthumb` written into CMake, so `compile_commands.json` carries them, and this line is somewhat redundant—but it's safer to keep, because older CMake scripts don't always propagate both flags to every target. `-mcpu=cortex-m3` sets the target to Cortex-M3; `-mthumb` forces the Thumb instruction set. Both are non-negotiable.

`BuiltinHeaders: QueryDriver` is the punchline. It switches the source of built-in headers (`stdint.h`, `stddef.h`, the ones GCC ships) from "the `clang-runtimes/...` phantom paths clangd makes up" to "the real paths query-driver pulls from the compiler." Those red squiggles from before are mostly healed by this single line.

### Piece three: compile_commands.json has to carry the cross flags

Configuring clangd alone isn't enough; the `compile_commands.json` it reads has to be the cross-compiled version too. The next piece covers this in detail, but in one sentence: CMake uses a toolchain file plus `CMAKE_EXPORT_COMPILE_COMMANDS`, and the resulting JSON carries `-mcpu=cortex-m3/-mthumb/-I.../Drivers/...` natively. clangd reads that, knows it's compiling for ARM, and stops guessing toward the host side.

## Where compile_commands.json comes from

The big difference between an embedded project's CMake and a host project's is that the embedded one passes a **toolchain file**. That file looks like this (`arm-none-eabi.cmake`):

```cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m3)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS}")
```

`CMAKE_SYSTEM_NAME Generic` tells CMake "the target has no OS" (bare metal). `CMAKE_C_COMPILER` / `CMAKE_CXX_COMPILER` pick the cross-compiler. The `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` line is easy to miss—by default CMake builds a try-run executable during configure to validate the compiler, but an ARM executable produced by the cross-compiler can't run on the host, so the try-run fails. This line switches it to a static library and skips the run.

Add one line to the project-root `CMakeLists.txt`:

```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Remember to pass the toolchain when you configure:

```bash
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

::: details Full build commands (collapsible)

```bash
# Wipe the old build and re-configure so compile_commands.json is the cross version
rm -rf build
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ninja -C build

# Check whether the generated commands carry -mcpu
grep -m1 "mcpu" build/compile_commands.json
```

:::

In the generated `build/compile_commands.json`, the `command` for every `.cpp` carries `-mcpu=cortex-m3 -mthumb`. clangd reads that, knows the target is Cortex-M3 with the Thumb instruction set, and combined with the newlib headers from query-driver, the whole parsing loop closes.

::: warning Don't let CMake eat the flags
Some CMake templates write `-mcpu=cortex-m3 -mthumb` as `target_compile_options(... PRIVATE -mcpu=cortex-m3 -mthumb)`, which is correct and lands in `compile_commands.json`. But if you write it via `add_compile_options` layered with `interface`, or stash it in `CMAKE_<LANG>_FLAGS` and let a generator expression swallow it, the flags might not propagate. After configuring, always run `grep "mcpu" build/compile_commands.json` to verify. If the flag didn't make the JSON, clangd doesn't get it.
:::

## The config the project already ships

All that theory out of the way—the repo's `code/stm32f1-tutorials/` projects already have this set up. Take `0_start_our_tutorial`; its `.vscode/settings.json` is just five lines:

```json
{
    "clangd.arguments": [
        "--query-driver=/usr/sbin/arm-none-eabi-g++,/usr/sbin/arm-none-eabi-gcc"
    ]
}
```

`1_led_control`, `2_button_control`, `3_uart_logger`—their `.vscode/settings.json` is identical. **This is what the repo itself uses; just copy it.**

One detail to clear up: why is the path `/usr/sbin/` and not `/usr/bin/`?

It depends on how the toolchain got installed. This machine uses the MSYS2-style package manager (WSL2 + pacman), and the `arm-none-eabi-gcc` package puts the actual compiler under `/usr/sbin/`, while `/usr/bin/` holds the more commonly used tools. A quick `ls` confirms:

```bash
$ ls -l /usr/sbin/arm-none-eabi-g++
-rwxr-xr-x 2 root root 1.7M arm-none-eabi-g++ 16.1.0

$ which arm-none-eabi-g++
/usr/sbin/arm-none-eabi-g++
```

::: details Ubuntu / Arch / Homebrew paths all differ

| Platform | Typical path |
|---|---|
| MSYS2 / WSL2 + pacman | `/usr/sbin/arm-none-eabi-g++` |
| Ubuntu apt (`gcc-arm-none-eabi` package) | `/usr/bin/arm-none-eabi-g++` |
| Arch pacman | `/usr/bin/arm-none-eabi-g++` |
| macOS Homebrew | `/opt/homebrew/bin/arm-none-eabi-g++` |

Run `which arm-none-eabi-g++` and paste the output into `--query-driver`. If you're not sure, just use a glob: `--query-driver=/usr/*/arm-none-eabi-g*,/opt/*/arm-none-eabi-g*` covers the common spots.

:::

Note that this `.vscode/settings.json` **only configures query-driver** and ships no `.clangd`. The reason is that the `command` field in these projects' `compile_commands.json` already spells out `/usr/sbin/arm-none-eabi-g++` directly (CMake generated it with an absolute path). clangd sees the executable is the ARM toolchain, query-driver is on, and the header paths come straight from GCC. A `.clangd` with `BuiltinHeaders: QueryDriver` is essentially redundant once query-driver is on—clangd defaults to substituting the queried headers for its own built-ins. The `Compiler:` and `Add: [-mcpu...]` lines in a `.clangd` are the fallback you reach for only when `compile_commands.json` isn't clean enough, when the executable path or flags are wrong.

## sysroot and --gcc-install-dir

With the setup above, the red squiggles vanish in most cases. But every now and then a stray header still can't be found—typically when your `compile_commands.json` **carries no sysroot**, and clangd can't locate part of the newlib headers on its own. In that case, you patch in the sysroot.

`--gcc-install-dir` is a clang flag that flat-out tells it "the GNU toolchain's libstdc++ lives in this directory," and clang derives header locations from there. Append it in `.clangd`:

```yaml
CompileFlags:
  Compiler: arm-none-eabi-g++
  Add:
    - -mcpu=cortex-m3
    - -mthumb
    - --gcc-install-dir=/usr/lib/gcc/arm-none-eabi/16.1.0
  BuiltinHeaders: QueryDriver
```

Or do it the old way with `-isystem` to explicitly add a system header directory (newlib's C headers, for instance):

```yaml
CompileFlags:
  Add:
    - -isystem/usr/arm-none-eabi/include
```

When troubleshooting, crank up clangd's logging and see where it actually looks for headers:

```text
View → Command Palette → Clangd: Open Log
```

Or pick clangd in VS Code's Output panel and search the log for `Search starts here` to check whether the search paths clangd ends up with actually cover the newlib directory. You should see a line like:

```text
Query driver arm-none-eabi-g++ for include paths
```

That means query-driver really ran. If it's missing, the `--query-driver` glob probably didn't match, and clangd silently skipped the query. Go back to `.vscode/settings.json` and check the path.

::: warning clangd has to be new enough
query-driver and `BuiltinHeaders: QueryDriver` need clangd **17 or newer**. The `gcc-install-dir` flag needs clangd **18+** (underneath, clang 18 has to recognize it). This machine runs clangd 19 with no problem; the clangd 14 or 15 that ships with older distros won't make this work, and you'll get stuck in the mystical state of "the config looks right but the log is dead silent." Run `clangd --version` first.
:::

## Verifying

After configuration, close VS Code and reopen it (or Command Palette → `Clangd: Restart language server`), then open `main.c`. Here's what you should see:

1. The red squiggly on the first line `#include "stm32f1xx.h"` is gone. The file `stm32f1xx.h` lives under `Drivers/CMSIS/Device/ST/STM32F1xx/Include/`, and once query-driver supplies the sysroot plus CMake's `-I`, clangd finds it.
2. Hold `Ctrl` and click `HAL_GPIO_WritePin`; the cursor jumps to its declaration in `stm32f1xx_hal_gpio.h`.
3. Type `HAL_`, and the completion list pops up with `HAL_GPIO_WritePin`, `HAL_Delay`, `HAL_Init`, and friends.
4. The clangd log shows the line `Query driver arm-none-eabi-g++ for include paths`.

At this point the cross-project clangd experience is on par with host projects—red squiggles gone, jump-to-definition and completion all there.

::: details Verification checklist (consult this when something breaks)

- [ ] `clangd --version` is 17+, ideally 18+
- [ ] The `--query-driver` path glob matches the output of `which arm-none-eabi-g++`
- [ ] `grep "mcpu" build/compile_commands.json` returns something—confirms the cross flags made the JSON
- [ ] The clangd log shows `Query driver ... for include paths`
- [ ] `arm-none-eabi-g++ -E -xc++ -v /dev/null` prints the real include paths
- [ ] `.clangd`'s `Compiler:` and `BuiltinHeaders: QueryDriver` are correct (only needed when compile_commands isn't clean enough)

:::

## Closing

The full-red meltdown clangd throws on embedded projects traces back to one thing: by default it never asks the actual cross-compiler where its headers live. The host-platform setup from getting-started piece 5 hid this, because clangd already knows the host compiler natively. The moment you switch to the ARM toolchain, you have to explicitly make clangd query the driver, pull in GCC's real paths, and close the parsing loop.

The next piece links up with vol7's [A short guide to cross-compilation and CMake](/vol7-engineering/01-cross-compilation-and-cmake), which approaches cross-compilation from the host angle as an engineering discipline (multi-target builds, reusing toolchain files). This piece is the clangd-specific chapter on the embedded line, filling in the IDE side of things. If you haven't read [getting-started piece 5: install clangd, kill the red squiggles](/getting-started/05-vscode-clangd), it's worth going back to first—the "why" of this piece builds directly on the "how" of piece 5.
