---
chapter: 14
difficulty: beginner
order: 1
platform: stm32f1
reading_time_minutes: 11
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 1: Building an STM32 Toolchain from Scratch — Cross-Compilation Principles
  and Installation Guide'
description: ''
translation:
  source: documents/vol8-domains/embedded/00-env-setup/01-toolchain-setup.md
  source_hash: 66aad22af656ab3fbc4cfc15515752920d78dd297f87aa0a51f38b5433e2c879
  translated_at: '2026-06-16T04:08:45.126890+00:00'
  engine: anthropic
  token_count: 1576
---
# Part 1: Building an STM32 Toolchain from Scratch — Cross-Compilation Principles and Installation Guide

> Written for all friends who want to work on STM32 under Linux but are confused by the jargon of toolchains.
> This post records the complete process of setting up an ARM cross-compilation environment from scratch, including why we cross-compile, what each tool does, and how to install it on Ubuntu and Arch Linux.

---

## Why I Wrote This Tutorial

To be honest, I can't stand Keil's antiquated workflow anymore. It's 2024, and we are still using a closed-source IDE that only runs on Windows, with crippled code suggestions and a debugging interface that looks like last century's software. The worst part is it takes up several GB of space on my C drive. The dealbreaker is that I've become accustomed to the Linux development environment — Vim/Neovim for coding, clangd for completion, and CMake for the build. This toolchain feels natural and efficient for any project.

But things aren't that simple. When I first tried to flash a program to the STM32F103C8T6 (that dirt-cheap Blue Pill board) under Linux, I found the online tutorials to be a disaster. Some still hand-write compilation rules with Makefiles, others pull out PlatformIO which encapsulates everything in a black box, and some simply say "just use Keil, it's not worth the trouble under Linux." The most ridiculous ones are those so-called "from scratch" tutorials that give you a bunch of commands to copy and paste right away, without explaining what `arm-none-eabi-gcc` is for, what `newlib` is, or why a linker script is needed. You can get it running by following them, but as soon as something goes slightly wrong, you are completely lost on where to start troubleshooting.

I spent an entire weekend messing around with this toolchain inside and out. After stepping into countless pits, I finally sorted out the entire compilation and flashing chain. Now I'm going to record this process completely. It's not a "copy-paste to run" cheat sheet, but a guide to help you truly understand what we are doing at each step and why. This way, when you encounter errors later, you'll know which part of the chain is failing, instead of searching aimlessly for answers like a headless fly.

---

## First Things First: What is Cross-Compilation?

Before we start typing commands, there is a concept we must clarify — Cross-Compilation.

If you usually write programs that run on an x86-64 CPU, the compilation process is straightforward: you use `gcc` to compile code, and the generated executable runs on the same machine. The compiler and the target platform of the program are the same; this is called "Native Compilation."

However, the STM32F103C8T6 uses an ARM Cortex-M3 core, and its instruction set is completely different from the x86-64 in your computer. Code compiled on your computer with ordinary `gcc` cannot be understood by the STM32, just like reciting Arabic to someone who only knows Chinese. So we need a "translator" — a compiler that runs on x86-64 Linux but can generate ARM machine code. This is the cross-compiler.

Why is it called `arm-none-eabi-gcc`? Let's break it down:

- `arm` is the target CPU architecture; the generated code is for ARM.
- `none` indicates no operating system vendor (more on this later).
- `eabi` stands for Embedded Application Binary Interface.
- `gcc` is our familiar GNU Compiler Collection.

Here is a detail worth expanding on. The `none` field is originally used to mark the OS vendor, for example, `arm-linux-eabi` means compiling for ARM devices running Linux. But our STM32 is a bare-metal program without an OS backing it, so we fill in `none` here. The difference between `arm-none-eabi` and `arm-none-eabihf` is that the latter supports hardware floating-point, but the F103C8T6's Cortex-M3 only has a single-precision floating-point unit, so the standard `arm-none-eabi` is sufficient.

Once you understand cross-compilation, you will know why you can't use the system's default `gcc` directly, and why you need a whole dedicated set of tools: compiler, linker, debugger, `objcopy` (to convert ELF to binary), `size` (to check firmware size). These tools must all be "cross" versions.

---

## What Does the Toolchain Look Like?

Before we officially install, I want to set up the overall framework so you know what parts we eventually need to collect.

Compiling an STM32 program and flashing it onto the board roughly requires this pipeline:

First is the source code level. Your C/C++ code needs to go through preprocessing, compilation, and assembly to become individual object files (`.o` files). This step uses `arm-none-eabi-gcc` (for C code) and `arm-none-eabi-g++` (for C++ code).

But object files alone aren't enough; they need to be glued together. This glue is the linker (`arm-none-eabi-ld`). Its job is to piece all object files and library files into a complete program according to specific rules. For STM32, the linking process is particularly special — you need to tell it where Flash starts, where RAM is, and how the heap and stack are allocated. These rules are written in the Linker Script (`.ld` file). The linker places code segments and data segments in the correct locations according to the "map" in the script.

After linking is complete, you get an ELF format file (`.elf`), which contains a bunch of information like code, data, and symbol tables. But STM32's Flash only recognizes pure binary data and doesn't need symbol tables. So we need `arm-none-eabi-objcopy` to extract the "meat" from the ELF file and generate a `.bin` binary file. This file is what actually gets flashed into the Flash.

There are several choices for flashing tools. The most common is ST-Link V2, ST's official debugger/programmer, which communicates with the STM32 via SWD (Serial Wire Debug) protocol. Under Linux, we need software to drive the ST-Link, and that software is OpenOCD (Open On-Chip Debugger). It can play two roles: writing firmware to Flash (flashing), and acting as a GDB Server so you can debug the program on the board with GDB.

Speaking of library files, there is a point where beginners often get confused. ARM bare-metal programs cannot directly use the `glibc` (GNU C Library) on your computer because `glibc` is designed for OS environments and relies on a bunch of system calls. Embedded environments need `newlib` — a C standard library implementation designed specifically for bare-metal/embedded systems. More specifically, we use `newlib-nano`, a stripped-down version of `newlib` optimized for code size. After installing `arm-none-eabi-newlib`, the compiler can find `stdio.h`, `stdlib.h` and other headers, and the linker can get the necessary library function implementations.

The last link is debugging. OpenOCD can run in GDB Server mode, listening on a port (default 3333). You connect with `arm-none-eabi-gdb` to single-step, set breakpoints, and view variables just like debugging a normal program. VSCode's Cortex-Debug plugin just visualizes this whole process so you don't have to type GDB commands manually.

Putting these together, the complete chain is: **Source Code → Cross-Compilation → Linking (with Linker Script) → objcopy Extract Binary → OpenOCD Flash → GDB Debug**. Once you understand this chain, you will know which tool plays a role in which stage, and you can quickly locate whether the problem is in compilation, linking, or flashing.

---

## Alright, Let's Get Started

With all those concepts laid out, we can finally get our hands dirty. I will cover both Ubuntu and Arch lines, but you will soon find that the commands are actually quite similar; they are all just package manager stuff.

First, Ubuntu. I'm using 22.04 LTS here, but commands for 20.04 and 24.04 are basically the same since they use the same software sources. Open a terminal and update the package index first; it's a good habit:

```bash
sudo apt update
```

Then install all the packages we need in one go:

```bash
sudo apt install gcc-arm-none-eabi gdb-multiarch openocd cmake build-essential
```

Let me explain what these packages do. `gcc-arm-none-eabi` is a big gift pack containing the cross-compiler, linker, `objcopy`, `size`, and a whole set of tools. `gdb-multiarch` is the multi-architecture GDB—it can debug ARM as well as RISC-V and other architectures. Note that the executable it installs is called `gdb-multiarch`, not `arm-none-eabi-gdb` (Ubuntu removed the legacy `gdb-arm-none-eabi` package from the repos a while ago). So wherever this tutorial says `arm-none-eabi-gdb`, Ubuntu users should substitute `gdb-multiarch` at the command line; Arch users keep `arm-none-eabi-gdb`. `openocd` we mentioned earlier, for flashing and GDB Server. `cmake` and `build-essential` are build tools, with the latter containing basic compilation tools like `make`.

After installation, we can verify if the toolchain is actually installed:

```bash
arm-none-eabi-gcc --version
```

Normally, you will see output similar to this:

```text
arm-none-eabi-gcc (Ubuntu 11.4.0-1ubuntu1~22.04) 11.4.0
Copyright (C) 2021 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO warranty;
not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

The version number might be different, but as long as it prints the version info, the installation is successful. Here is a small detail: Ubuntu's package name is `gcc-arm-none-eabi` without a version number, and the software source automatically selects a "stable and mostly used" version. If you need a specific version (like wanting the latest GCC 14), you have to go to ARM's official website to download the precompiled toolchain, manually unpack it to a directory, and add the path to the `PATH` environment variable. However, for an old chip like F103C8T6, GCC 11 is sufficient, so there's no need to struggle with too new a version.

---

## The Arch Linux User Route

If you are using Arch Linux (or Manjaro, which I use), package management is even more direct. Arch's advantage is fast software updates, so you can get relatively new toolchain versions.

The installation command is a bit shorter than Ubuntu's:

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-binutils arm-none-eabi-gdb openocd cmake
```

Here is a difference from Ubuntu: Arch splits the tools into multiple packages. `arm-none-eabi-gcc` is the compiler itself, `arm-none-eabi-binutils` contains `ld`, `objcopy`, `size`, and other tools, and `arm-none-eabi-gdb` is the debugger. Ubuntu bundles the compiler and binutils into `gcc-arm-none-eabi`, while the debugger is the separate multi-architecture package `gdb-multiarch`—so the debugger command differs between the two distros (Arch: `arm-none-eabi-gdb`, Ubuntu: `gdb-multiarch`), as noted in the Ubuntu section above.

Verify if the installation was successful:

```bash
arm-none-eabi-gcc --version
```

On Arch, you will most likely see GCC 13 or 14, because it rolls fast:

```text
arm-none-eabi-gcc (GCC) 14.2.1 20250110
Copyright (C) 2024 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO warranty;
not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

There is a pit here that needs a warning in advance. After installing `arm-none-eabi-gcc` on Arch, you might find that headers like `stdio.h` cannot be found during compilation, or you get a linker error about `libc.a`. The reason is the same — Arch's `arm-none-eabi-gcc` package doesn't include newlib, and you need to install an extra package from AUR:

```bash
yay -S arm-none-eabi-newlib
```

If you haven't installed `yay`, you need to install this AUR helper first, or manually clone the PKGBUILD from AUR to install. I won't expand on this process; Arch users should be familiar with it.

After installing newlib, headers like `stdio.h` and `stdlib.h` are available, and `nano.specs` and `nosys.specs` can be used normally. What are these two specs files for? `nano.specs` tells the linker to use newlib-nano (the stripped-down C library), while `nosys.specs` provides an empty system call implementation — after all, in a bare-metal environment without an OS, functions like `read()` and `write()` cannot be implemented at all. Using `nosys.specs` prevents linker errors.

---

## Where Are We Now

At this point, our toolchain installation is complete. You should now have on your system:

- Cross-compiler (`arm-none-eabi-gcc/g++`)
- Linker and utilities (`arm-none-eabi-ld`, `objcopy`, `size`)
- Debugger (`arm-none-eabi-gdb`)
- Flashing tool (OpenOCD)
- Build system (CMake)
- C Standard Library (newlib)

But having tools isn't enough. The next article will cover project structure — how to get ST's official HAL library, that annoying submodule problem, which startup file to choose, and how to write the linker script. That part is the real "pit concentration camp," but let's lay the foundation solid first.

You can verify that all tools can be called normally:

```bash
arm-none-eabi-gcc --version && arm-none-eabi-gdb --version && openocd --version
```

If these commands all print version information, congratulations, you've passed the toolchain installation level. In the next article, we will dive directly into the project structure and start building a real STM32 C++ project.
