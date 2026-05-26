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
translation:
  source: documents/vol8-domains/embedded/00-env-setup/01-toolchain-setup.md
  source_hash: 3e0fe3078ac0320a7d603f5acb8bb8d4d1dce68ee183250e7477e7fc45171b5f
  translated_at: '2026-05-26T11:57:24.345125+00:00'
  engine: anthropic
  token_count: 1569
---
# Part 1: Building an STM32 Toolchain from Scratch — Cross-Compilation Principles and Installation Guide

> For everyone who wants to work with STM32 on Linux but feels dizzy from the barrage of toolchain jargon.
> This article documents our complete process of setting up an ARM cross-compilation environment from scratch, including why we cross-compile, what each tool does, and how to install everything on Ubuntu and Arch Linux.

---

## Why I'm Writing This Tutorial

To be honest, I couldn't stand Keil's antiquated workflow anymore. It's 2024, and we're still stuck with a closed-source IDE that only runs on Windows, featuring half-baked code completion and a debugger UI that looks like it belongs in the last century. Worst of all, it hogs several gigabytes of my C: drive. The dealbreaker for me is that I've grown completely accustomed to my Linux development environment — writing code with Vim/Neovim, getting completions from clangd, and managing builds with CMake. This toolchain feels natural and effortless on any project.

But things aren't that simple. When I first tried to flash a program to the STM32F103C8T6 (that classic, dirt-cheap Blue Pill board) from Linux, I found that the online tutorials were an absolute disaster. Some still hand-write compilation rules in Makefiles, others pull out PlatformIO as a black box that hides everything, and some just flat out say, "Just use Keil, it's not worth the hassle on Linux." The most absurd ones are the so-called "from scratch" guides that throw a wall of commands at you to copy-paste, without ever explaining what `arm-none-eabi-gcc` does, what newlib is, or why a linker script is necessary. If you follow along, things might work, but the moment something breaks, you have absolutely no idea where to start troubleshooting.

I spent an entire weekend tearing this toolchain apart from the inside out. After falling into countless pitfalls, I finally mapped out the entire compile-and-flash pipeline. Now I'm documenting this process in full — not to give you a "copy-and-paste" cheat sheet, but to walk you through what each step does and why we do it. That way, when you hit an error down the road, you'll know exactly which stage went wrong, instead of searching for answers like a headless fly.

---

## Let's Clear This Up First: What Is Cross-Compilation

Before we start typing commands, there's one concept we need to nail down — cross-compilation.

If you usually write programs that run on an x86-64 CPU, the compilation process is straightforward: you use `gcc` to compile your code, and the resulting executable runs on the very same machine. The compiler and the target platform are identical; this is called "native compilation."

But the STM32F103C8T6 uses an ARM Cortex-M3 core, and its instruction set is completely different from the x86-64 in your computer. Code compiled with your standard `gcc` is complete gibberish to the STM32 — it's like reading Arabic to someone who only understands Chinese. So we need a "translator" — a compiler that runs on x86-64 Linux but generates ARM machine code. This is the cross-compiler.

So why is it called ``arm-none-eabi-gcc``, such a long and strange name? It makes perfect sense once we break it down:

- ``arm`` is the target CPU architecture; the generated code is for ARM
- ``none`` means no OS vendor (we'll get to this shortly)
- ``eabi`` stands for Embedded Application Binary Interface
- ``gcc`` is our familiar GNU Compiler Collection

There's a detail here worth expanding on. The ``none`` field was originally meant to specify the OS vendor — for example, ``arm-linux-eabi`` means compiling for an ARM device running Linux. But our STM32 runs bare-metal without an OS backing it up, so we put ``none`` here. The difference between ``eabi`` and ``eabihf`` is that the latter supports hardware floating point, but the F103C8T6's Cortex-M3 only has a single-precision floating-point unit, so the standard ``eabi`` is sufficient.

Once you understand cross-compilation, you'll see why you can't just use the system's built-in `gcc`, and why you need a dedicated toolchain: the compiler, linker, debugger, _objcopy_ (for converting ELF to binary), _size_ (for checking firmware size) — all of these tools must be "cross" versions.

---

## What Does the Toolchain Look Like

Before we dive into installation, I want to lay out the big picture so you know exactly which pieces we need to put together.

Compiling an STM32 program and flashing it to the board requires a pipeline roughly like this:

First, at the source code level. Your C/C++ code goes through preprocessing, compilation, and assembly to become individual object files (``.o`` files). This step uses ``arm-none-eabi-gcc`` (for C code) and ``arm-none-eabi-g++`` (for C++ code).

But object files alone aren't enough; they need "glue" to hold them together. That glue is the linker (``arm-none-eabi-ld``), whose job is to stitch all object files and libraries into a complete program according to specific rules. For STM32, the linking process is especially unique — you need to tell it where Flash starts, where RAM is located, and how to allocate the heap and stack. These rules are written in a linker script (``.ld`` file). The linker places the code and data sections in the correct locations based on this "map."

After linking, you get an ELF (Executable and Linkable Format) file (``.elf``), which contains code, data, symbol tables, and a bunch of other information. But STM32 Flash only understands raw binary data — it has no use for symbol tables. So we use ``arm-none-eabi-objcopy`` to extract the "meat" from the ELF file, generating a ``.bin`` binary file. This is the actual payload that gets flashed into the chip.

There are several options for the flashing tool. The most common is ST-Link V2, ST's official debugger/programmer, which communicates with the STM32 via SWD (Serial Wire Debug). On Linux, we need software to drive the ST-Link, and that software is OpenOCD (Open On-Chip Debugger). It plays two roles: writing firmware to Flash (flashing), and acting as a GDB Server so you can debug programs on the board using GDB.

Speaking of libraries, there's a point that often trips up beginners. ARM bare-metal programs can't directly use your computer's glibc (GNU C Library), because glibc is designed for OS environments and relies on a bunch of system calls. Embedded environments need newlib — a C standard library implementation designed specifically for bare-metal and embedded systems. More specifically, we use newlib-nano, a stripped-down version of newlib optimized for code size. After installing ``arm-none-eabi-newlib``, the compiler can find headers like ``<stdint.h>`` and ``<string.h>``, and the linker can pull in the necessary library function implementations.

The final piece is debugging. OpenOCD can run in GDB Server mode, listening on a specific port (3333 by default). You connect to it with ``arm-none-eabi-gdb``, and you can single-step, set breakpoints, and inspect variables just like debugging a regular program. VSCode's Cortex-Debug plugin simply puts this entire workflow behind a graphical interface, so you don't have to type GDB commands manually.

Stringing it all together, the complete chain is: **Source Code → Cross-Compilation → Linking (with linker script) → objcopy extracts binary → OpenOCD flashes → GDB debugs**. Once you understand this chain, you'll know exactly which tool acts at which stage, and you can quickly pinpoint whether a problem occurred during compilation, linking, or flashing.

---

## Alright, Let's Get Our Hands Dirty

After laying out all that conceptual groundwork, we can finally get to work. I'll cover both Ubuntu and Arch, but you'll quickly notice that the commands are pretty much the same — it's all just package manager stuff.

Let's start with Ubuntu. I'm using 22.04 LTS here, but the commands for 20.04 and 24.04 are basically identical, since they share the same package repositories. Open a terminal and update the package index first — it's a good habit:

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

Let me explain what each of these packages does. ``gcc-arm-none-eabi`` is a bundle that includes the cross-compiler, linker, objcopy, size, and a whole suite of tools. ``gdb-arm-none-eabi`` is the ARM version of GDB, used for debugging embedded programs. ``openocd`` we covered earlier — it handles flashing and acts as a GDB Server. ``cmake`` and ``build-essential`` are build tools, with the latter including make and other fundamental compilation utilities.

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

Your version number might differ, but as long as it prints the version info, the installation was successful. Here's a small detail: Ubuntu's package name is ``gcc-arm-none-eabi``, without a version number — the repository automatically selects a "stable and widely used" version. If you need a specific version (say, you want the latest GCC 14), you'll need to download a prebuilt toolchain from ARM's official website, manually extract it to a directory, and add that path to your ``PATH`` environment variable. But for an older chip like the F103C8T6, GCC 11 is more than enough — there's no need to chase the newest version.

---

## The Arch Linux Route

If you're using Arch Linux (or Manjaro, which I use), package management is even more straightforward. Arch's advantage is fast package updates, so you get a fairly recent toolchain version.

The installation command is a bit shorter than Ubuntu's:

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb openocd cmake make
```

There's one difference from Ubuntu here: Arch splits the tools into multiple packages. ``arm-none-eabi-gcc`` is the compiler itself, ``arm-none-eabi-binutils`` includes ld, objcopy, size, and other utilities, and ``arm-none-eabi-gdb`` is the debugger. Ubuntu bundles all of these into ``gcc-arm-none-eabi``, so you need to install fewer packages.

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

There's a pitfall I need to warn you about in advance. After installing ``arm-none-eabi-gcc`` on Arch, you might find that headers like ``<stdint.h>`` are missing during compilation, or you get a ``cannot read spec file 'nano.specs'`` error at link time. The reason is the same in both cases — Arch's ``arm-none-eabi-gcc`` package doesn't include newlib, and you need to install an extra package from the AUR:

```bash
yay -S arm-none-eabi-newlib
```

If you don't have ``yay`` installed, you'll need to set up this AUR helper first, or manually clone the PKGBUILD from the AUR to build it. I won't expand on that process — anyone using Arch should already be familiar with it.

Once newlib is installed, headers like ``<stdint.h>`` and ``<string.h>`` will be available, and ``nano.specs`` and ``nosys.specs`` will work properly. What do these two specs files do? ``nano.specs`` tells the linker to use newlib-nano (the stripped-down C library), while ``nosys.specs`` provides empty system call implementations — after all, in a bare-metal environment there's no OS, so functions like ``read()`` and ``write()`` can't actually be implemented. Using nosys.specs prevents linker errors.

---

## Where Are We Now

At this point, our toolchain installation is complete. Your system should now have:

- Cross-compiler (`arm-none-eabi-gcc/g++`)
- Linker and toolchain utilities (`arm-none-eabi-ld`, `objcopy`, `size`)
- Debugger (`arm-none-eabi-gdb`)
- Flashing tool (OpenOCD)
- Build system (CMake)
- C standard library (newlib)

But tools alone aren't enough. In the next article, we'll cover project structure — how to get ST's official HAL library, that tricky submodule problem, which startup file to pick, and how to write a linker script. That's where the real minefield is, but for now, let's make sure the foundation is solid.

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

If all of these commands print their version information, congratulations — you've cleared the toolchain installation hurdle. In the next article, we'll jump straight into project structure and start building a real STM32 C++ project.
