---
title: 'Part 1: Building an STM32 Development Toolchain from Scratch — Cross-Compilation
  Principles and Installation Guide'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 14
order: 1
---
# Part 1: Building an STM32 Toolchain from Scratch — Cross-Compilation Principles and Installation Guide

> For everyone who wants to work with STM32 on Linux but feels dizzy from the barrage of toolchain jargon.
> This article documents our complete process of setting up an ARM cross-compilation environment from scratch, including why we cross-compile, what each tool does, and how to install everything on Ubuntu and Arch Linux respectively.

---

## Why I'm Writing This Tutorial

To be honest, I couldn't stand Keil's antiquated workflow anymore. It's 2024, and we're still stuck with a closed-source IDE that only runs on Windows, featuring half-baked code completion and a debugger UI that looks like it belongs in the last century. Worst of all, it eats up several gigabytes of my C: drive. The dealbreaker for me is that I've grown completely accustomed to my Linux development environment — writing code with Vim/Neovim, getting completions from clangd, and managing builds with CMake. This toolchain feels natural and effortless on any project.

But things aren't that simple. When I first tried to flash a program to the STM32F103C8T6 (that dirt-cheap Blue Pill board) from Linux, I found that the online tutorials were an absolute disaster. Some were still hand-writing compilation rules in Makefiles, others pulled out PlatformIO as a black box that hides everything, and some simply said, "Just use Keil, it's not worth the hassle on Linux." The most absurd ones were the so-called "from scratch" tutorials that immediately threw a wall of commands at you to copy-paste, without explaining what `arm-none-eabi-gcc` does, what newlib is, or why a linker script is needed. If you follow along, things might work, but the moment something goes slightly wrong, you'll have absolutely no idea where to start troubleshooting.

I spent an entire weekend tearing this toolchain apart from the inside out. After falling into countless pitfalls, I finally mapped out the entire compile-and-flash pipeline. Now I'm documenting this process in full — not to give you a "copy-and-paste" cheat sheet, but to walk you through truly understanding what each step does and why we do it. That way, when you encounter errors later, you'll know exactly which stage went wrong, instead of searching for answers like a headless fly.

---

## Let's Be Clear: What Is Cross-Compilation

Before we start typing commands, there's one concept we must nail down — cross-compilation.

If you usually write programs that run on an x86-64 CPU, the compilation process is straightforward: you use `gcc` to compile your code, and the resulting executable runs on the very same machine. The compiler and the target platform of the program are identical; this is called "native compilation."

But the STM32F103C8T6 uses an ARM Cortex-M3 core, and its instruction set is completely different from the x86-64 in your computer. Code compiled with the regular `gcc` on your computer is gibberish to the STM32 — like speaking Arabic to someone who only understands Chinese. So we need a "translator" — a compiler that runs on x86-64 Linux but generates ARM machine code. This is the cross-compiler.

So why is it called `arm-none-eabi-gcc` such a long, weird name? Breaking it down makes it perfectly clear:

- `arm` is the target CPU architecture; the generated code is for ARM
- `none` means no OS vendor (more on this later)
- `eabi` stands for Embedded Application Binary Interface
- `gcc` is our familiar GNU Compiler Collection

There's a detail here worth expanding on. The `none` field was originally meant to specify the OS vendor — for example, `arm-linux-eabi` means compiling for ARM devices running Linux. But our STM32 runs bare-metal programs without an OS backing it up, so we put `none` here. The difference between `eabi` and `eabihf` is that the latter supports hardware floating point, but the F103C8T6's Cortex-M3 only has a single-precision floating-point unit, so the regular `eabi` is sufficient.

Once you understand cross-compilation, you'll see why you can't just use the system's built-in `gcc`, and why you need an entire dedicated toolchain: the compiler, linker, debugger, _objcopy_ (for converting ELF to binary), _size_ (for checking the firmware size) — all of these tools must be "cross" versions.

---

## What Does the Toolchain Look Like

Before we actually install anything, I want to lay out the overall framework so you know which pieces we ultimately need to assemble.

Compiling an STM32 program and flashing it to the board requires roughly this pipeline:

First, at the source code level. Your C/C++ code goes through preprocessing, compilation, and assembly to become individual object files (`.o` files). This step uses `arm-none-eabi-gcc` (for C code) and `arm-none-eabi-g++` (for C++ code).

But object files alone aren't enough; they need "glue" to hold them together. This glue is the linker (`arm-none-eabi-ld`), whose job is to stitch all object files and libraries into a complete program according to specified rules. For STM32, the linking process is especially unique — you need to tell it where Flash starts, where RAM is, and how to allocate the heap and stack. These rules are written in a linker script (`.ld` file). The linker places the code and data sections in the correct positions according to this "map" in the script.

After linking is complete, you get an ELF (Executable and Linkable Format) file (`.elf`), which contains code, data, symbol tables, and a bunch of other information. But STM32's Flash only understands raw binary data; it doesn't need any symbol tables. So we use `arm-none-eabi-objcopy` to extract the "meat" from the ELF file, generating a `.bin` binary file. This file is what actually gets flashed into the Flash.

There are several choices for the flashing tool. The most common is the ST-Link V2, ST's official debugger/programmer, which communicates with the STM32 via SWD (Serial Wire Debug). On Linux, we need software to drive the ST-Link, and that software is OpenOCD (Open On-Chip Debugger). It can play two roles: first, writing firmware to Flash (flashing), and second, acting as a GDB Server so you can debug programs on the board using GDB.

Speaking of libraries, there's a point that often confuses beginners. ARM bare-metal programs can't directly use your computer's glibc (GNU C Library), because glibc is designed for OS environments and relies on a bunch of system calls. Embedded environments need newlib — a C standard library implementation designed specifically for bare-metal/embedded systems. More specifically, we use newlib-nano, a stripped-down version of newlib optimized for code size. After installing `arm-none-eabi-newlib`, the compiler can find headers like `<stdint.h>` and `<string.h>`, and the linker can get the necessary library function implementations.

The final piece is debugging. OpenOCD can run in GDB Server mode, listening on a certain port (3333 by default). You connect to it with `arm-none-eabi-gdb`, and you can single-step, set breakpoints, and inspect variables just like debugging a regular program. VSCode's Cortex-Debug plugin simply puts this entire workflow behind a graphical interface, so you don't have to type GDB commands manually.

Stringing it all together, the complete chain is: **Source code → Cross-compilation → Linking (with linker script) → objcopy extracts binary → OpenOCD flashes → GDB debugs**. Once you understand this chain, you'll know which stage each tool operates in, and when something goes wrong, you can quickly pinpoint whether it's a compilation, linking, or flashing issue.

---

## Alright, Let's Get Started

After laying out all those concepts, we can finally get our hands dirty. I'll cover both Ubuntu and Arch, but you'll quickly notice that the commands are actually pretty similar — it's all just package manager stuff.

Let's start with Ubuntu. I'm using 22.04 LTS here, but the commands for 20.04 and 24.04 are basically identical, since they share the same software repositories. Open a terminal and update the package index first — it's a good habit:

```bash
sudo apt update
```

Then install all the packages we need in one go:

```bash
sudo apt install -y \
    gcc-arm-none-eabi \
    gdb-arm-none-eabi \
    openocd \
    cmake \
    build-essential
```

Let me explain what each of these packages does. `gcc-arm-none-eabi` is a bundle that includes the cross-compiler, linker, objcopy, size, and a whole set of tools. `gdb-arm-none-eabi` is the ARM version of GDB, used for debugging embedded programs. `openocd` we covered earlier; it handles flashing and acts as a GDB Server. `cmake` and `build-essential` are build tools, with the latter including essentials like make.

After installation, we can verify that the toolchain is actually in place:

```bash
arm-none-eabi-gcc --version
```

If everything is normal, you'll see output similar to this:

```text
arm-none-eabi-gcc (Ubuntu 11.3.0-1ubuntu1~22.04) 11.3.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is no warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

The version number might differ, but as long as it prints the version info, the installation was successful. There's a small detail here: Ubuntu's package name is `gcc-arm-none-eabi`, without a version number — the repository automatically selects a "stable and widely used" version. If you need a specific version (like wanting the latest GCC 14), you'd have to download a prebuilt toolchain from ARM's official website, manually extract it to a directory, and add the path to your `PATH` environment variable. But for an older chip like the F103C8T6, GCC 11 is more than enough; there's no need to bother with a newer version.

---

## The Arch Linux Route

If you're using Arch Linux (or Manjaro, which I use), package management is even more straightforward. Arch's advantage is fast software updates, so you get a fairly recent toolchain version.

The install command is a bit shorter than Ubuntu's:

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb openocd cmake make
```

There's one difference from Ubuntu here: Arch splits the tools into multiple packages. `arm-none-eabi-gcc` is the compiler itself, `arm-none-eabi-binutils` includes ld, objcopy, size, and other tools, and `arm-none-eabi-gdb` is the debugger. Ubuntu bundles all of these into `gcc-arm-none-eabi`, so fewer packages need to be installed.

Verify that the installation succeeded:

```bash
arm-none-eabi-gcc --version
```

On Arch, you'll most likely see GCC 13 or 14, since it rolls fast:

```text
arm-none-eabi-gcc (GCC) 13.2.0
Copyright (C) 2023 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is no warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

There's a pitfall here I need to warn you about in advance. After installing `arm-none-eabi-gcc` on Arch, you might find that headers like `<stdint.h>` can't be found during compilation, or you get a `cannot read spec file 'nano.specs'` error at link time. The reason is the same in both cases — Arch's `arm-none-eabi-gcc` package doesn't include newlib, and you need to install an extra package from the AUR:

```bash
yay -S arm-none-eabi-newlib
```

If you don't have `yay` installed, you'll need to install this AUR helper first, or manually clone the PKGBUILD from the AUR to build it. I won't expand on this process — anyone using Arch should already be familiar with it.

Once newlib is installed, headers like `<stdint.h>` and `<string.h>` will be available, and `nano.specs` and `nosys.specs` will work properly. What do these two specs files do? `nano.specs` tells the linker to use newlib-nano (the stripped-down C library), while `nosys.specs` provides an empty system call implementation — after all, in a bare-metal environment there's no OS, so functions like `read()` and `write()` can't actually be implemented. Using nosys.specs prevents link errors.

---

## Where Are We Now

At this point, our toolchain installation is complete. Your system should now have:

- Cross-compiler (`arm-none-eabi-gcc/g++`)
- Linker and toolchain utilities (`arm-none-eabi-ld`, `objcopy`, `size`)
- Debugger (`arm-none-eabi-gdb`)
- Flashing tool (OpenOCD)
- Build system (CMake)
- C standard library (newlib)

But tools alone aren't enough. In the next article, we'll cover project structure — how to get ST's official HAL library, that tricky submodule issue, which startup file to pick, and how to write a linker script. That part is the real "pitfall minefield," but for now, let's make sure the foundation is solid.

You can go ahead and verify that all tools can be invoked normally:

```bash
# 验证编译器
arm-none-eabi-gcc --version

# 验证调试器
arm-none-eabi-gdb --version

# 验证烧录工具
openocd --version

# 验证 CMake
cmake --version
```

If all of these commands print version information, congratulations — you've cleared the toolchain installation hurdle. In the next article, we'll dive straight into project structure and start building a real STM32 C++ project.
