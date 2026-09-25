---
title: "Working environment: the four things you installed — what exactly are they?"
description: "The previous article installed the tools with a single command; this one fills in the coursework: what cross-compilation is, how to unpack the name arm-none-eabi-gcc, and which stretch of the full source-to-firmware pipeline each tool owns — every stage matched against the real commands ninja actually executes, with the roles of cmake and ninja spelled out too, so beginners are no longer left hanging"
chapter: 0
order: 3
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - 工具链
difficulty: beginner
platform: stm32f1
reading_time_minutes: 12
related:
  - "Renode observatory: no board, so who gets the final say?"
  - "Your very own firmware: adding a target to the repo"
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/03-toolchain-anatomy.md
  source_hash: c0dcd24019fcc5f8e25074fe26f6417f08ab00edc12c2e10dae0c2f464c68482
  translated_at: '2026-09-25T08:26:59+00:00'
  engine: anthropic
  token_count: 2200
---

# "The last chapter had me install four pieces of software, and to this day I have no idea what they're for"

Yes — it wasn't on purpose, and it wasn't an accident either. It was a deliberately-not-on-purpose. After all, only once you have seen the thing actually run, I believe, have you earned the seat to sit down and keep wandering through this journey of ours.

And a question bubbles right up — wait a minute, when I use Keil, STM32CubeIDE, or whatever mystery IDE the vendor bundles, I get none of this nonsense! Are you pulling my leg?

> Your author will never forget a certain group chat I used to hang around in, where a guy pointed right at my nose and declared — "What year is it, and you're still learning gcc? Do you realize that I click that one button and the code just runs? You're the one being all mystical about it, going on about 'compiling, linking, running'."

Man, what can I say. So every time I set out on a new journey, I am duty-bound to give this stuff its little popular-science moment.

## Section 1: We all say "compile" — so what exactly is this cross-compilation business?

Normally, when we write programs on our computers and compile them with `gcc`, the compiled program runs on that same computer — the CPU doing the compiling and the CPU that will later execute the binary are the same kind. That is called native compilation. But what runs on the Blue Pill is an ARM Cortex-M3, whose instruction set is **completely different** from your computer's x86-64: take an executable built by your system's stock `gcc`, throw it at an STM32, and it cannot make out a single word of it. So we need something special: **a compiler that runs on an x86 computer yet exists solely to produce ARM machine code**. That practice is called cross-compilation, and such a compiler is called a cross-compiler.

With that, we can also read that long name segment by segment. `arm-none-eabi-gcc` breaks into four parts:

- `arm`: the machine code produced is for ARM processors
- `none`: there is no operating system on the target machine (bare metal — our firmware itself is the "operating system")
- `eabi`: Embedded Application Binary Interface — the embedded application binary interface, the low-level rulebook that agrees on things like how function arguments are passed and how the stack is laid out
- `gcc`: the GNU compiler family

String the four together and we get `arm-none-eabi-gcc`. Naturally, what about the C++ compiler? `arm-none-eabi-g++`. And if I want to copy a binary around, convert its format, or check how big it is: `arm-none-eabi-objcopy`, `arm-none-eabi-size`; and of course, for debugging: `arm-none-eabi-gdb`. Same prefix, **same target machine** — one big family.

> Q: I know C — so how are you using C standard library functions like `memcpy` and `printf`? And why can't we just use the glibc from the PC?
> A: Because glibc is designed for environments that "have an operating system" — the moment it opens its mouth, it is demanding a syscall, that is, a system call. And where, pray tell, would our STM32F103C8T6 get an operating system?
>
> The bare-metal world uses **newlib** — a C standard library rewritten specifically for embedded use; the `--specs=nano.specs` we pass at link time selects newlib-nano, its even stingier, size-trimmed variant. This is also why those two empty function stubs (`_init`/`_fini`) from article 00 exist: newlib's startup flow calls them, nobody provides them on bare metal, so the firmware has to supply them itself.

## From main.cpp to my_blinky.bin

### Step one is called compiling — and cross-compiling at that

A firmware is source code when we write it down (possibly a bit hard to read), and a binary when we finally flash it (that one is definitely unreadable). As for the process in between, we can let ninja, the build system, do the talking:

```bash
ninja -C build -t commands blinky
```

ninja will spit out, verbatim, every command it would run. We pick one representative of each kind; the long, dreary `-I` header-path lists in the middle have been collapsed with `...`. If you ask me what those are — they are the header include paths you used to copy over when configuring a Keil environment.

Step one, as ninja tells it: the `.cpp`/`.c` files we wrote become object files (`.o`), one per source file:

```text
arm-none-eabi-g++ ... -mcpu=cortex-m3 -mthumb -fno-exceptions -fno-rtti \
    -O3 -DNDEBUG -std=gnu++23 \
    -o examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj \
    -c .../examples/00_my_blinky/main.cpp
```

`-c` is the compile action: "compile only, no linking". Note — no linking! As for the rest:

- `-mcpu=cortex-m3 -mthumb` tell the compiler to generate code for the Cortex-M3's instruction set (this is where the "cross" gets real)
- `-O3` is the optimization level we agreed on back in the weigh-in article
- `-fno-exceptions -fno-rtti` are those two "no paying for what you don't use" switches. C++ sources go to `g++`, C sources (the HAL library's `.c` files and our stub files) go to `gcc`, and one piece of startup assembly, `startup_stm32f103xb.s`, also gets compiled into a `.o` right here.

### Step two is linking — the scattered binary artifacts have to be put together the right way

We take all the `.o` files plus the `hal` static library and stitch them into one complete ELF:

```text
arm-none-eabi-g++ -mcpu=cortex-m3 -mthumb \
    -nostartfiles --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections \
    -T.../cmake/arch/stm32f103c8t6.ld \
    -Wl,-Map=.../my_blinky.map \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/stm32f1xx_it.c.obj \
    examples/00_my_blinky/CMakeFiles/my_blinky.dir/syscalls.c.obj \
    -o examples/00_my_blinky/my_blinky \
    include/libestdx/boards/stm32f1/libhal.a
```

The linker (the one actually doing the work is `ld`; `g++` here plays the dispatcher) answers the question "who sits next to whom, and at which address does each live". The memory map handed to it via `-T` (the 64K Flash / 20K SRAM layout) is one you saw back in article 00.

`--gc-sections` is the janitor that "tosses any function nobody references straight into the bin" — the gc stands for Garbage Collection. Coming from a language with a gc, are we? Same idea! Unused means thrown away!

`-Map` has it write out the moving inventory as a `.map` file, so when we do CI audits — or just plainly want to see what's what — we can pin down which rascal is hogging too much space!

### Step three: extract the code that actually matters — peel the flashable artifact out of the ELF

Don't be fooled by the name objcopy — what it does is exactly this. `my_blinky` here is an ELF. Besides code and data, it also carries "human-facing attachments" like the symbol table and debug info. Flash does not acknowledge those; it only wants the goods:

```text
arm-none-eabi-objcopy -O binary .../my_blinky my_blinky.bin
```

`objcopy` peels the loadable image out of the ELF as-is and hands us a `.bin` that can be poured into Flash byte by byte. Congratulations — this is the very tool we used earlier during our manual verification~

**Station four: the report**:

```text
arm-none-eabi-size --format=berkeley .../my_blinky
```

That is the three-column `text/data/bss` table you have already seen three rounds of.

At this point, it has become an art. We have walked the entire pipeline from source files to a binary ready for flashing. Enjoy~

## Hey, hey — you clearly had a blast introducing that whole pile of toolchain! So what are cmake and ninja for?

Having seen the whole pipeline, a natural question pops up: since in the end it is just these few commands, **why do we write CMakeLists.txt instead of just typing the commands directly?**

You can type them — once, maybe. But a real project is a dozen-plus source files, two sets of compiler flags (one for C, one for C++), six or seven firmware targets, and each time you touch one file you only want to rebuild that one file — a type-the-commands scheme collapses on day one. So we need a two-layer division of labor: **cmake is the designer** — it reads your `CMakeLists.txt`, works out "which targets to build, which source files go into each target, with which flags", and writes the conclusion down as a construction blueprint (`build.ninja`)

And our **ninja is the foreman**: it takes the blueprint and dispatches that pipeline above, redoing whoever changed. The `cmake -B build` you type is commissioning the designer for drawings; `cmake --build build` is telling the foreman to get to work — and that `ninja -t commands` just now was sneaking a peek at the foreman's construction checklist.

Why does the CMakeLists speak "blueprint language" like `PROJECT` and `add_executable` instead of raw commands? Because a blueprint is portable across construction sites: switch chips, and you change one toolchain file (`cmake/arch/stm32f103c8t6.cmake`, which tells cmake "the compiler is called arm-none-eabi-gcc, and here is what the linker options are") — the blueprint itself doesn't move by a single word. This design will go straight to work at the station where we switch to the F407 chip. You could say barely a line gets changed! Laziness, after all!

## Acceptance check: run your own hands along the pipeline

Just watching isn't satisfying — let's run our hands along the pipeline ourselves. In the build directory of your cloned repo, start with the compilation artifact:

```bash
file build/examples/00_my_blinky/CMakeFiles/my_blinky.dir/main.cpp.obj
```

```text
main.cpp.obj: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
```

`file` tells you this `.o` is a "relocatable ELF" — the machine code is already ARM, but the addresses are not settled yet, which is why it is called relocatable: it waits for the linker to assign them. Now go feel the artifacts below!

```bash
file build/examples/00_my_blinky/my_blinky build/examples/00_my_blinky/my_blinky.bin
```

```text
my_blinky:     ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
               statically linked, with debug_info, not stripped
my_blinky.bin: ARM Cortex-M firmware, initial SP at 0x20005000,
               reset at 0x080001a4, NMI at 0x080001ec, ...
```

`my_blinky` has been promoted to executable (addresses settled, still carrying debug info); and something outrageous about `.bin`, this "pure data" file: `file` went ahead and decoded its Cortex-M vector table — **initial SP at 0x20005000, exactly the top of the 20K SRAM** (the memory map written in the linker script, verified right here — rather handsome, if I may). As for the rest, you can unfold it and study at your leisure — a perfect warm-up lap for the ARM architecture knowledge along our embedded C++ journey!
