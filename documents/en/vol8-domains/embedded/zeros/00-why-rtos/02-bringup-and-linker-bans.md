---
title: "Project Bring-up: From an Empty Repo to the First Line of Renode Output"
description: "No cloning the reference repo — reproduce the whole project by hand from git init in your own directory (reference answer anchored at 6ee1d25 and fe5a0e8): a 13-line repo foundation, with a submodule pinning the official ST SDK at an exact SHA; a toolchain file whose generator expressions disable exceptions and RTTI for C++ only; a four-line linker ban written as PROVIDE+ASSERT ld errors, verified by stuffing a new into main and getting heap new banned; zero-assembly startup: a section attribute places the vector table, slot 0 holds _estack as the initial SP, and SVC/PendSV seats are left empty; reset_handler compares addresses via uintptr_t so GCC cannot optimize a pointer comparison into an endless loop; on the Renode side, the bluepill.repl header comment is a fidelity confession, and run-renode.cmake dodges the pitfall of builders' shells eating variables; acceptance = the serial window printing the banner plus one tick per second, with firmware size 536B Flash / 8B RAM"
chapter: 0
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 工具链
  - 链接器
  - CMake
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 25
prerequisites:
  - "Why an RTOS, Part 1: From Superloop to RTOS"
  - "Embedded: STM32F103 + Renode Getting Started: Toolchain Installed, Renode Running"
related:
  - "From the Super Loop to an RTOS: Why We Need One, and How to Verify It"
translation:
  source: documents/vol8-domains/embedded/zeros/00-why-rtos/02-bringup-and-linker-bans.md
  source_hash: b908a01499043e7a352002744df1fc4ff9da921565923af839aaf1618c3f3632
  translated_at: '2026-09-25T08:04:44+00:00'
  engine: anthropic
  token_count: 16000
---

# Project Bring-up: From an Empty Repo to the First Line of Renode Output

Alright — now please go find yourself a blank spot. Oh, and if you are on Windows, first go look at how to set up WSL: for now we do not support continuing this hand-crafted-code learning on Windows natively (support is coming, as fast as we can). Time to `mkdir` a folder that belongs to you alone. For instance, your author ran `mkdir ~/ZerOS/` and then `code ~/ZerOS` at 7:29 on a Friday morning. Please kindly do the same. Pick whatever name you like, anyway.

`git init`, please. I remember some of my good buddies manage their code via zip archives, with distributed delivery over the QQ-or-WeChat transport protocol. Let's not do that. If you don't know how, Teacher DeepSeek is happy to help you, or go to our [EmbedBox](https://github.com/Awesome-Embedded-Learning-Studio/EmbedBox) and look for a Git tutorial.

Done typing? It spits out that the repo has been initialized by git — so now, write code...? Your author says: no rush. Please whip your head around and configure the third-party repositories first. **Just as environment setup has always been the first step of our journey, configuring the initial dependencies is exactly the same.**

STMicroelectronics maintains a dependency for the STM32 on GitHub. If you came over from ALIENTEK or Puzhong style boards and still cannot bear source-code management without the zip-archive technology, now is the time to adapt —

```shell
cd ~/ZerOS      # the one you just mkdir'ed, the name is up to you
git init        # initializing the repo, just did that
git submodule add https://github.com/STMicroelectronics/STM32CubeF1.git third_party/STM32CubeF1
```

Welcome back. The third line means — add STM32's standard library as a submodule of our project. Your author checked the size: 131 MB, so hopefully your proxy is in decent shape. If it isn't, no matter — standing up for a stretch and having some tea is a fine thing too. Once it is done, you can run the following commands:

```shell
# The line below means — click, our submodule is now pinned at bb2016e
# The effect is equivalent to the zip archive from the mystery vendor: it prevents the
# problem of upstream having moved on while our code takes off and roars with laughter
git -C third_party/STM32CubeF1 checkout bb2016e

# Then add this child node to our Git hosting — we now manage the dependency,
# in a distinctly special way
git add third_party/STM32CubeF1
```

Huh? Why is this the first step? Think about it: the startup code and the UART driver we are about to write lean on every register name (`RCC->APB2ENR`, `USART1->DR`) and every bitfield macro, and all of them come from the CMSIS device header — the header file is the source of truth for registers. If it drifts, the bitfields discussed in the articles no longer match the tree in your hands, and the acceptance checks at every later station have nothing to stand on.

As for that 131 MB cost: the reference answer later squeezed it down to 13 MB with sparse-checkout, but that is a later station's business — for now we pull the full thing. Call that a Git optimization for later.

## The Toolchain File: Put All the "This Is for ARM" in One Place

The next step is a classic move that belongs to cross-compiling alone. Has anyone here built ARM32 Qt before? If so, wonderful — what we do here is exactly the same. In short: we get this done with a toolchain file!

If you have never built Qt, no worries — let's walk through the whole thing from the top.

Your computer is x86_64, but the chip on our board is an ARMv7-M Cortex-M3 — the two instruction sets do not interoperate at all. Producing "machine code for the board" on your computer is a job called cross-compiling, and the compiler doing the work is `arm-none-eabi-gcc`.

Unroll the name and it introduces itself: arm — the target architecture is ARM; none — no operating system is expected to catch anything, so it emits code without syscalls; eabi — the embedded binary interface. It runs on your computer, and the code it spits out only speaks Cortex-M. Try to execute that directly and the CPU is, naturally, utterly baffled, gifting you an `Exec Format Error`.

Then again, how does CMake know which set of compilers to call to the stage this time? After all, when your author was bragging while writing ZerOS, he said we would verify with host-side mocks — which means what? It means our code is also accepted by the host machine, right!

Yes — it all rides on the toolchain file. It is a perfectly ordinary CMake script that, before the project starts work, spells out "who compiles, and for whom". Just it.

CMake nails the compiler into its cache on the very first configure — this must be settled up front, and it deserves a file of its own: the same CMakeLists, configured with the host compiler, yields the desktop unit tests; configured with this file, it yields the board firmware. Later on, target flash goes straight onto the board — more on that later~.

Let's create `cmake/arch/arm-none-eabi.cmake`; the full contents are below, type them along:

```cmake
# What system are we? The diplomatic answer is Generic; the blunt answer is "unimportant, don't worry about it"
set(CMAKE_SYSTEM_NAME Generic)
# What's our processor architecture? ARM, obviously
set(CMAKE_SYSTEM_PROCESSOR arm)

# Here we designate our special-issue compilers — nothing else, the
# arm-none-eabi- family we set up ourselves
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Bare-metal targets have no main(), so the compiler self-check builds only a static library
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# The next three lines govern what find_xxx() searches: once you cross-compile,
# you must first declare which world a search belongs to
# Finding programs: tools that run on your machine at build time — search the host
# as usual, don't go digging through ARM directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Finding libraries and headers: only the toolchain side counts; the host's
# /usr/lib and /usr/include are shut out entirely
# This guards against the day some dependency uses find_library/find_path and
# smuggles x86 stuff into a Cortex-M firmware
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Target-side compilation baseline
# Use the Thumb instruction set, because the code comes out small and adorable
# -Os optimizes for size
# -g stays — it's for us to see what's going on when debugging
# The rest disables RTTI and exceptions; since C files take part too, we wrap
# them in $<COMPILE_LANGUAGE:CXX>:
add_compile_options(
    -mcpu=cortex-m3
    -mthumb
    -ffreestanding
    -Os
    -g
    -ffunction-sections
    -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)
```

Several spots deserve explanation. `CMAKE_SYSTEM_NAME Generic` states the fact that there is no operating system; `TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` is an old friend of bare metal — CMake's self-check tries to link an executable by default, bare metal has no `main`, and without this line the configure stage just errors out; you met it back when you first set up a bare-metal project. As for the three `CMAKE_FIND_ROOT_PATH_MODE_*` lines, the mechanism is written in the code comments; let's add one honest remark: this station's build is all explicit paths, nobody calls find_xxx yet, so right now these three lines are pure defense. They only start earning their keep once the tests and dependencies actually arrive later.

What truly deserves a second look is the last two generator expressions: `-fno-exceptions` and `-fno-rtti` take effect only for C++ compilation units. That `system_stm32f1xx.c` in the board package is a C file — ST's official code, bundled as-is — and it should not be caught in the crossfire of C++ bans.

You might ask: why hang these two flags on the toolchain file instead of the root CMakeLists? The root carries what all targets share:

```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra)
```

The C++23 baseline and general warnings — that is all. Architecture-related flags? Not a single one goes into the root: this codebase later runs unit tests on the host, the host side needs exceptions on to run Catch2, and if the root ever got tainted with `-fno-exceptions`, that retreat route would be cut off. "Target-specific goes to the toolchain file, cross-target common goes to the root" — with a split like that, I think the code stays a bit cleaner!

At configure time, we point CMake at it — a one-liner:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
```

## Linker Bans: Writing the Constitution as Error Messages

No heap, no exceptions, no RTTI. That is what your author's promotional banner says. But verbal agreements are worthless — C says you should trust the programmer, yet as a rule people slip up. So, let's invite the tooling to stand guard. For instance, we have the linker script, our `link.ld`, open with these four lines:

```ld
/* Link-time countermeasure: whoever pulls in these symbols gets a link error */
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));
```

> What even is a linker script? I've never written one!
>
> Great question. You indeed haven't, and indeed never needed to: writing programs on a PC, you have an operating system and a default set of linking rules backing you up — where the code lives, where globals live, where the program enters — all decided by someone else. gcc has actually been carrying a default script of some three hundred lines in its belly the whole time; don't believe it? Run `arm-none-eabi-ld --verbose` once — after the opening line `using internal linker script:`, out it all tumbles in one big heap — that's it. You never wrote it, but that doesn't mean it wasn't working; someone simply wrote it for you. On bare metal this service layer is gone: at power-up the chip only fetches instructions from fixed addresses, Flash sits at `0x08000000`, SRAM at `0x20000000`, and where the code lives, where the variables live, where the boundaries are drawn — the machine knows none of it; you must tell the linker yourself. This "address allocation memo" is the linker script, a small language of `ld`, and our `link.ld` does exactly this one job from head to toe.

Alright, take a look — we unpack the semantics of `PROVIDE`: only when this symbol is "referenced, and no object file defines it" does the linker evaluate the expression on the right. And the right side is `ASSERT(0, ...)`, which detonates the moment it is evaluated, and what blows out is the sentence in the quotes. In other words: nobody uses the heap, and these four lines stay as quiet as if they did not exist; anyone who dares to `new` gets `heap new banned` from the linker on the spot. `_Znwj` and `_Znwm` are the mangled names of `operator new` under 32-bit and 64-bit `size_t` respectively — plug both, and it does not matter which platform's compiler shows up. That is what poka-yoke looks like, brothers!

Let's actually run it for everyone to see. Stuff one line into `main()`, `{ auto p = new int(5); (void)p; }`, and rebuild:

```text
/usr/arm-none-eabi/bin/ld: heap new banned
collect2: error: ld returned 1 exit status
```

The error we get is exactly that sentence — no stack trace, no line number, but the direction is clear: go find who used the heap.

> Two details — don't let them slip past you. First, the `PROVIDE`-with-`ASSERT` idiom must sit outside `SECTIONS`: the ld manual says it plainly — `PROVIDE` symbols inside section definitions are unreliable with respect to when the assertion gets evaluated; the top level is the stable spot, and all four of ZerOS's lines live at the top level.
>
> Second, a blind spot your author hit in actual testing. The construction/destruction code for global objects lives in the `.init_array` section, and at this point the linker script has no `.init_array` output section yet, so `--gc-sections` reclaims entire chunks of unreferenced static initialization: your author stuffed in a global object with a destructor as a test, and the link was — unbelievably — green. The object was never constructed; not a sound. This hole stays open until the memory station plugs it with `KEEP(.init_array)` plus an assertion. Also, on ARM EABI toolchains destructor registration goes through `__aeabi_atexit`, so the `__cxa_atexit` ban never touches it on this toolchain; if you really write a function-local `static` with a destructor, what blows up is `undefined reference to '__aeabi_atexit'` — an error less friendly than the ban sentence, but the conclusion is identical: the link does not go through.

Beyond the bans, the body of `link.ld` manages the memory layout: the 64K Flash / 20K SRAM boundaries go into `MEMORY`; how each section is arranged, where symbols like `_sidata` and `_estack` come from — all inside `SECTIONS`. The whole file is pasted below, type it along; what each part does will line up one by one when we discuss the startup code below:

```ld
/* stm32f103_bluepill linker script — C8T6: 64K Flash / 20K SRAM
 * Memory constraints are real-hardware constraints (the Renode model sim/renode/bluepill.repl
 * is the same size; out-of-bounds reports immediately).
 * P1: linker script symbols must be compared as integer addresses in code;
 * pointer comparison is forbidden. */

ENTRY(reset_handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

/* Link-time countermeasure: whoever pulls in these symbols gets a link error */
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));

SECTIONS
{
    .isr_vector :
    {
        KEEP(*(.isr_vector))
    } > FLASH

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.exidx :
    {
        *(.ARM.exidx*)
    } > FLASH

    /* Initialized data: LMA in Flash, copied to RAM at startup */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ :
    {
        *(.comment)
    }

    _estack = ORIGIN(RAM) + LENGTH(RAM);
}
```

## Huh? You Really Write the Startup Like This? You Bet

By convention this step calls for a `startup.s`; ZerOS flat-out refuses — the startup file is `startup.cpp`, and the vector table we see is a C++ array with attributes:

```cpp
using Isr = void (*)();

// (section(".isr_vector"), used) is a little trick that tells the compiler:
// big brother, please place the global array below into the .isr_vector
// section, because ld needs it. Also, since it is never referenced directly,
// add used, so the linker doesn't kick our reset vector dead at link time.
__attribute__((section(".isr_vector"), used))
const Isr vectors[] = {
    reinterpret_cast<Isr>(&_estack),             // 0  initial SP
    &reset_handler,                              // 1  Reset
    &fault_handler,                              // 2  NMI
    &fault_handler,                              // 3  HardFault
    &fault_handler,                              // 4  MemManage
    &fault_handler,                              // 5  BusFault
    &fault_handler,                              // 6  UsageFault
    nullptr,                                     // 7-10 reserved
    nullptr,
    nullptr,
    nullptr,
    nullptr,                                     // 11-14 (SVC/PendSV belong to the kernel port)
    nullptr,
    nullptr,
    nullptr,
    &SysTick_Handler,                            // 15 SysTick (application-defined)
};
```

Let's start from the three attributes: `section(".isr_vector")` has the compiler put the array in its own section, `KEEP(*(.isr_vector))` in `link.ld` catches it, and the `used` attribute guards against `--gc-sections` scooping out the table because "nobody calls it". The array indices are the hardware slot numbers: at power-up, the hardware itself reads slot 0 from address `0x0` as the initial SP and slot 1 from `0x4` as the Reset entry — where the CPU's first two steps after power-up jump is entirely this table's call, and the layout is nailed down by the ARMv7-M architecture: slot 11 SVCall, slot 14 PendSV, slot 15 SysTick.

Slot 0 is the most special: it holds `_estack`, the top of RAM — there is no function for it to point at, period. `reinterpret_cast<Isr>(&_estack)` casts a data address into a function-pointer slot; from the abstract machine's viewpoint this is out of bounds, while on bare metal it is the standard Cortex-M posture. The hardware simply grabs its 8 bytes from here to use as the stack pointer and does not care how you wrote your types.

You might want to ask: NMI, HardFault, all of them pointing at `fault_handler` — that's it? That's it. It prints one line, `[FAULT] halted`, then halts: a minimal fallback; full context capture is the job of the kernel's HardFault module later on. And those four `nullptr` from 11 to 14: SVC and PendSV belong to the kernel port, the scheduler is not born until the third station, but the seats have been reserved from the very first vector table on — the comments say so plainly.

Beyond the vector table, don't miss the skeleton of the startup file: three include lines at the top (`<cstdint>`, `stm32f1xx.h`, `uart.hpp`), plus an `extern "C"` block holding the linker-symbol declarations, forward declarations of `main` and `SysTick_Handler`, and the fallback `fault_handler` itself. `extern "C"` is not decoration: without it, `reset_handler` would get renamed by C++ name mangling, and the linker script's `ENTRY(reset_handler)` would find nobody home on the spot:

```cpp
extern "C" {
// Linker script symbols (P1: compare as integer addresses only, pointer comparison forbidden)
extern std::uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;

int main();
void SysTick_Handler();

// Minimal fallback: print one line then halt; full context capture belongs to the kernel HardFault module (roadmap 3.1)
void fault_handler() {
    ZerOS::board::print("\r\n[FAULT] halted\r\n");
    for (;;) {
        __WFI();
    }
}
} // extern "C"
```

Whether this table is laid out right is for the machine to say. When we run Renode for acceptance later, the `Setting initial values` line at the start of the log will loudly read out the initial SP and PC; we will come back to this table then and check: SP should be `_estack`, PC should be `reset_handler`. Hold that thought for now — finish writing the startup code.

## reset_handler: Two Loops and One Trap

```cpp
void reset_handler() {
    // Why uintptr_t? Because uint32_t tells the compiler this variable
    // is a number, and it then makes some runaway optimizations
    auto src = reinterpret_cast<std::uintptr_t>(&_sidata);
    auto dst = reinterpret_cast<std::uintptr_t>(&_sdata);
    const auto dend = reinterpret_cast<std::uintptr_t>(&_edata);
    for (; dst < dend; dst += 4, src += 4) {
        *reinterpret_cast<std::uint32_t*>(dst) = *reinterpret_cast<std::uint32_t*>(src);
    }
    auto b = reinterpret_cast<std::uintptr_t>(&_sbss);
    const auto bend = reinterpret_cast<std::uintptr_t>(&_ebss);
    for (; b < bend; b += 4) {
        *reinterpret_cast<std::uint32_t*>(b) = 0;
    }

    SystemCoreClock = 72'000'000; // board main clock; to be replaced by real clock-tree init
    main();
    for (;;) {
        __WFI();
    }
}
```

At power-up SRAM holds random values; someone has to get the work started. Variables in `.data` have initial values, and those initial values live in Flash: the linker script uses `> RAM AT > FLASH` to separate the load address from the run address, and `_sidata = LOADADDR(.data)` records the source in Flash; the first loop copies it into RAM. Variables in `.bss` have no initial value, and the second loop zeroes them. Then comes `main()`; if it returns, `__WFI()` sleeps forever — there is no operating system to catch your return.

Hey! Notice that I am using `std::uintptr_t` rather than pointers. The linker script symbols `_sbss` and `_ebss` are declared in code as two independent `uint32_t` objects; if you carelessly write the pointer comparison `_sbss < _ebss`, then in the standard's eyes these two are unrelated objects, comparing them is undefined behavior, and GCC's pointer provenance analysis dares to conclude "the loop condition is always true" on that basis: the generated loop never stops, writes its way straight through the 20K of RAM, and the chip goes into lockup.

One more thing to confess to you: a conventional STM32 project's startup calls `SystemInit()` to configure the clock; here there is none — `SystemCoreClock` is hard-written to `72'000'000`, and the comment says plainly "to be replaced by real clock-tree init". So the 408-line `system_stm32f1xx.c` in the board package comes along for nothing? Not quite — the `SystemCoreClock` variable it provides is needed globally, and the real clock tree only stands up at the performance station. In Renode this is not a problem (the SysTick frequency is written in the platform model); on real hardware HSI defaults to 8 MHz, and your author will do the clock-tree init in the board-level file at that time.

## Polling UART: How to Type in a World Without printf

The `-nostdlib` world has no `printf`; if we want to talk, we do it with our own hands. `uart.hpp` is a polling-mode UART1, occupying PA9:

```cpp
inline void uart1_putc(char c) {
    while ((USART1->SR & USART_SR_TXE) == 0) {
    }
    USART1->DR = static_cast<std::uint8_t>(c);
}
```

Only when the transmit register is empty do we write the next byte — dumb waiting, but for the goal of "printing the first banner line", dumb waiting is just right. Printing numbers is a bit more troublesome than printing characters; let's look at `printdec`:

```cpp
inline void printdec(std::uint32_t n) {
    char buf[11];
    char* p = buf + sizeof(buf);
    *--p = '\0';
    do {
        *--p = static_cast<char>('0' + n % 10);
        n /= 10;
    } while (n != 0);
    print(p);
}
```

An 11-byte stack buffer, filled from the tail toward the front: dividing by 10 and taking the remainder yields the lowest digit, and filled that way the result is naturally in forward order. We use `do-while`, not a `while` that checks up front, because `n = 0` must also print a `'0'` — otherwise 0 prints nothing at all; `uint32_t` is at most 10 decimal digits, plus the terminating 0, and 11 bytes fit exactly.

Here are the initialization and whole-line printing as well; add the `#pragma once` at the top of the file, the three includes, and the `namespace ZerOS::board` wrapper, and `uart.hpp` is complete:

```cpp
inline void uart1_init() {
    // RCC is a fake value in Renode (ready bits always 1); this line mainly serves real hardware
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_IOPAEN;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE;
}

inline void print(const char* s) {
    while (*s != '\0') {
        uart1_putc(*s++);
    }
}
```

That comment inside `uart1_init` is worth copying down: "RCC is a fake value in Renode (ready bits always 1); this line mainly serves real hardware". Renode has no RCC model, so the clock-enable line reads and writes SVD fake values — a known divergence between the simulator and real hardware, and you are about to see the other half of the evidence.

## The Three Renode Pieces: Platform Model, Script, Launcher

Three files live under `sim/renode/`; let's look at them one by one — they turn "running a simulation" into a proper engineering asset.

`bluepill.repl` is the platform model, derived from Renode's built-in `stm32f103.repl` (a high-density superset) and trimmed to the C8T6's actual configuration. At this point, a thank-you to AI: your author honestly never quite mastered Renode's arcane script syntax, and it left behind the following AI slop for me —

```text
// Trimmed to the C8T6's actual configuration so the simulator enforces real-hardware
// constraints (out-of-bounds reports immediately, no longer relying on the linker
// script's good behavior):
//   Flash 64K @ 0x08000000 (upstream is a 512M superset window starting at 0x0, cut)
//   SRAM  20K @ 0x20000000 (upstream declares 256M, cut)
//   D-G are ghost ports: the upstream STM32F1AFIO constructor hard-requires exactly
//   7 ports (source-verified: gpioPorts[3..6] are indexed directly); give it fewer
//   and it crashes. The real C8T6 does not have these four ports — firmware that
//   touches them BusFaults on real hardware but silently succeeds in this model;
//   a known fidelity divergence.
//   RCC has no model; Tag fake value 0x0A020083 (ready bits always 1); timer
//   frequency follows upstream at 10MHz;
//   DMA has no model (not shipped with the 1.16.1 platform; the real C8T6 only has DMA1).
```

`bluepill.resc` is the run script — 22 lines, pasted in full:

```text
# Blue Pill @ Renode run script
# Usage (headless, log to disk):
#   renode --console --disable-xwt -e "logFile @run.log" \
#     -e '$bin=@build/zeros-bluepill.elf' \
#     -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start" -e "sleep 5" -e "quit"
# Setting $bin before the include overrides the default path; must be run from the repo root.

using sysbus

mach create "bluepill"
machine LoadPlatformDescription @src/board/stm32f103_bluepill/sim/renode/bluepill.repl

$bin?=@build/zeros-bluepill.elf

showAnalyzer usart1

macro reset
"""
    sysbus LoadELF $bin
"""

runMacro $reset
```

Two spots are worth a look: `$bin?=@build/zeros-bluepill.elf` with its question mark is a conditional assignment — if the outside set `$bin` first, it is not overridden, so CI can run a different ELF without touching the script; `showAnalyzer usart1` pops up the serial window, and that is where your banner lives.

`run-renode.cmake` is the launcher, and it exists because of a pitfall — let's lay out the scene: writing `-e "$bin=@..."` directly in an `add_custom_target`'s `COMMAND` gets `$b` eaten as a single-character variable by the Makefile generator, renode receives `in=@...` instead, and reports `No such command`. The workaround is to have the target call only `cmake -P`: `execute_process` arguments go straight to the process argv, bypassing any builder's shell. 17 lines, also pasted for you:

```cmake
# Renode launcher: invoked with cmake -P by the board package's renode-bluepill target.
# Arguments reach the process argv directly via execute_process, skipping the builder
# shell — dodging $bin being eaten as a variable by make/ninja (see docs/simulation.md P12).
# Usage parameters: -DRENODE_ROOT=<repo root> -DRENODE_ELF=<absolute ELF path>
#                   -DRENODE_RESC=<resc path relative to repo root> -DRENODE_SECONDS=<seconds to run>

execute_process(
    COMMAND renode --console --disable-xwt
            -e "logFile @${RENODE_ROOT}/run.log"
            -e "\$bin=@${RENODE_ELF}"
            -e "include @${RENODE_RESC}"
            -e "start" -e "sleep ${RENODE_SECONDS}" -e "quit"
    WORKING_DIRECTORY ${RENODE_ROOT}
    RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "renode exited with ${_rc}")
endif()
```

And so running the simulation is a proper build command:

```shell
cmake --build build --target renode-bluepill
```

## clangd: Why Is My `<cstdint>` All Red Squiggles

The project is built up; you open your editor, and `#include <cstdint>` is a wall of red — anyone would start questioning their life choices. The three comment lines in `.vscode/settings.json` spell out the pathology:

```json
{
    // For the arm-none-eabi target, clangd guesses a libc++ layout (c++/v1) by default,
    // but the local toolchain is actually a libstdc++ layout (c++/<version>); without
    // asking the real compiler it cannot find <cstdint> and other standard headers.
    // --query-driver lets the arm gcc/g++ through (installed in /usr/sbin), letting
    // clangd probe the real system header paths. It is a process flag — it cannot go
    // into .clangd, so it has to live here.
    "clangd.arguments": [
        "--query-driver=**/arm-none-eabi-g*"
    ]
}
```

Once clangd gets a cross-compilation command, it guesses where the standard headers are based on its own built-in assumptions by default; guess wrong, and the headers "cannot be found". `--query-driver` lets it admit the arm compiler and actually go ask it for the system header paths — add it, and the red squiggles all disappear. This thing is a process flag; it cannot be written into the `.clangd` file, it can only go into `settings.json`. If you stepped in this pit before, take it as a refresher; if you haven't, this time you walk around it.

## Acceptance: Make It Light Up

All the source files are here; the one that glues them into the build is the board package's `src/board/stm32f103_bluepill/CMakeLists.txt` — 60 lines, type it along:

```cmake
# Blue Pill board package (D14, one package per board): firmware target + Renode simulation target
# Simulation assets live in sim/renode/ (bluepill.repl/.resc); relative paths resolve against the
# launch cwd, so the repo root must be the cwd (P10)

set(CMAKE_EXECUTABLE_SUFFIX .elf)

set(CMSIS ${CMAKE_SOURCE_DIR}/third_party/STM32CubeF1/Drivers/CMSIS)

# ---- Firmware target ----
add_executable(
    zeros-bluepill
    ${CMAKE_SOURCE_DIR}/example/main.cpp
    startup.cpp
    system_stm32f1xx.c
)
target_include_directories(
    zeros-bluepill
    PRIVATE ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMSIS}/Include
            ${CMSIS}/Core/Include
            ${CMSIS}/Device/ST/STM32F1xx/Include
)
target_compile_definitions(zeros-bluepill PRIVATE STM32F103xB)
target_link_options(
    zeros-bluepill
    PRIVATE -nostdlib
            -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/link.ld
            -Wl,--gc-sections
            -Wl,--print-memory-usage
            -Wl,-Map,${CMAKE_BINARY_DIR}/zeros-bluepill.map
)
add_custom_command(
    TARGET zeros-bluepill
    POST_BUILD
    COMMAND arm-none-eabi-size $<TARGET_FILE:zeros-bluepill>
    COMMENT "size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)"
)

# ---- Simulation target: cmake --build build --target renode-bluepill ----
set(ZEROS_SIM_ELF "" CACHE STRING "覆盖仿真 ELF(空=本板默认产物)")
set(ZEROS_SIM_SECONDS 5 CACHE STRING "仿真运行秒数")

if(ZEROS_SIM_ELF)
    set(_elf ${ZEROS_SIM_ELF})
else()
    set(_elf $<TARGET_FILE:zeros-bluepill>)
endif()

add_custom_target(
    renode-bluepill
    COMMAND ${CMAKE_COMMAND}
            -DRENODE_ROOT=${CMAKE_SOURCE_DIR}
            -DRENODE_ELF=${_elf}
            -DRENODE_RESC=src/board/stm32f103_bluepill/sim/renode/bluepill.resc
            -DRENODE_SECONDS=${ZEROS_SIM_SECONDS}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/sim/renode/run-renode.cmake
    DEPENDS zeros-bluepill
    USES_TERMINAL
)
```

The last source file is the star of the acceptance, `example/main.cpp` — 27 lines, type it with your own hands:

```cpp
// ZerOS baseline smoke test: banner + 1kHz SysTick — acceptance = both visible in Renode (bluepill.resc)
#include <cstdint>

#include "stm32f1xx.h"
#include "uart.hpp"

extern "C" void SysTick_Handler() {
    static std::uint32_t ticks = 0;
    if (++ticks % 1000 == 0) {
        ZerOS::board::print("tick ");
        ZerOS::board::printdec(ticks / 1000);
        ZerOS::board::print("\r\n");
    }
}

int main() {
    ZerOS::board::uart1_init();
    ZerOS::board::print("\r\nZerOS baseline: C++23 @ STM32F103C8T6 (Blue Pill, Renode)\r\n");
    ZerOS::board::print("cc: gcc ");
    ZerOS::board::printdec(__GNUC__);
    ZerOS::board::print("\r\n");

    SysTick_Config(SystemCoreClock / 1000); // 1 kHz
    for (;;) {
        __WFI();
    }
}
```

Look at the `static` counter inside `SysTick_Handler`: every full thousand it prints one line — that is where one tick per second comes from; `SysTick_Config(SystemCoreClock / 1000)` configures the heartbeat to 1 kHz, and CMSIS internally subtracts one from the passed value and stuffs it into the reload register. Three steps to run it:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build
cmake --build build --target renode-bluepill
```

The tail of the build comes with its own artifact report:

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:         536 B        64 KB      0.82%
             RAM:           8 B        20 KB      0.04%
size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)
```

In the serial window (for headless runs, check `run.log`), you will see:

```text
cpu: Setting initial values: PC = 0x8000165, SP = 0x20005000.
[WARNING] sysbus: ReadDoubleWord from an unimplemented register RCC:APB2ENR ...
[WARNING] sysbus: WriteDoubleWord of value 0x4004 to an unimplemented register RCC:APB2ENR ...
usart1: ZerOS baseline: C++23 @ STM32F103C8T6 (Blue Pill, Renode)
usart1: cc: gcc 16
usart1: tick 1
usart1: tick 2
usart1: tick 3
```

The first line answers exactly the question we shelved in the vector-table section: SP = 0x20005000, precisely `0x20000000 + 20K`, the top of RAM — slot 0's `_estack` is in effect; PC = 0x8000165 lands in Flash — that is `reset_handler`. The machine's first two steps at boot fit our table like a glove. The two WARNING lines that follow are the other half of the evidence for that uart.hpp comment: RCC has no model in Renode, reads and writes are all SVD fake values — the confession wrote it down. Banner, gcc version number, one tick per second: all three present, and this station counts as passed. From the next station on, every station walks in with this as the regression baseline: break something, and the serial window tells you first thing.

## The Skeleton Is Up — What's Next?

At the next station we hand the kernel its memory: a bitmap plus fixed-size block pools, the `new` of the heapless world — the kernel's first piece of real code will grow out from under your own hands. The `std::expected` error channel promised in the previous article and the concept-constrained interface both get put to real use at that station; and the `.init_array` blind spot this bring-up station leaves behind also gets plugged by hand when the memory line reaches real hardware.

Take a break! We'll be right back!
