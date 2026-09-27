---
title: "Your very own firmware: adding a target to the repo"
description: "Three articles of theory behind us; this one is all hands-on: copy 01_blinky into your own 00_my_blinky, register it with the build system, and live through the real error messages of two mines — the sim target name collision and the forgotten registration — then step into a third mine that never makes a sound: a half-done rename that leaves the all-green simulation running someone else's firmware; then a segment-by-segment anatomy of the CMakeLists, and finally a one-line HAL_Delay change that makes the effect visible under the sampling criterion — plus an instruction-encoding Easter egg where 200 is 4 bytes cheaper than 500"
chapter: 0
order: 4
tags:
  - stm32f1
  - beginner
  - 嵌入式
  - CMake
difficulty: beginner
platform: stm32f1
reading_time_minutes: 11
related:
  - "Why C++, and by what right?"
  - "Renode observatory: no board, so who gets the final say?"
translation:
  source: documents/vol8-domains/embedded/f103/00-env-setup/04-first-firmware.md
  source_hash: de780df190df9542ff10a49e58e35efe7088b2b59def5034074ebac0ab2e6f22
  translated_at: '2026-09-27T05:27:39+00:00'
  engine: anthropic
  token_count: 5200
---
# "Three articles in, and you still haven't let me write a single line"

Well, my apologies for that. I trust that by now — phone in hand, or sitting there in your VM or WSL — you are coiled up and ready to spring. This article asks you to do the work!

> Starting with this article, please put the phone down, get yourself in front of a computer, open WSL — or any distribution you like — and let's take a deep breath and get to work!

## Getting the project rolling — by copying

Credit is due to my mentor from work: back when I was optimizing the project's CSS parser, he had me copy from Google's Blink CSS. "Copying isn't shameful; copying so badly that it doesn't even work — that would be indefensible." Not everything has to start from frame zero. And when it comes to hands-on work, it goes better with something to grab onto. The idea here: there is no need to write everything from 0. You can start by tweaking the code and understanding it as you go~

Let's copy `01_blinky` wholesale under a new name, `00_my_blinky` — it sorts to the very front, so one glance at `ls` and it is unmistakably yours:

```bash
cd libestdx
cp -r examples/01_blinky examples/00_my_blinky
```

Step inside and change the names in three places: swap every `01_blinky`/`blinky` in `CMakeLists.txt` and `renode.resc` for `00_my_blinky`/`my_blinky`, and rewrite the comment at the top of `main.cpp` in your own words. Then comes the critical step: letting the build system know it exists. Open `examples/CMakeLists.txt`; its current contents look like this:

```cmake
add_subdirectory(01_blinky)
add_subdirectory(02_gpio)
add_subdirectory(03_led)
add_subdirectory(04_button)
```

Add yours above the first line:

```cmake
add_subdirectory(00_my_blinky)
```

While our hands are still warm, let's get the directory layout straight. This project has three layers: at the top, **top-level orchestration** (the root `CMakeLists.txt`, which sets the standards and hooks up the subdirectories); below it the **family package** (`include/libestdx/boards/stm32f1/`, which compiles the official HAL into the `hal` static library — this is where the "wheels, explained but never built" part lives); and then the **firmwares** (under `examples/`, one executable target per directory, each consuming `hal`). That copy-plus-one-line you just did added a new target to the third layer.

## Hahaha, you got played, brother

All right, now run it...

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
```

Wham! CMake says no! Let me paste the real message for you:

```text
CMake Error at examples/01_blinky/CMakeLists.txt:23 (add_custom_target):
  add_custom_target cannot create target "sim" because another target with
  the same name already exists.  The existing target is a custom target
  created in source directory
  ".../libestdx/examples/00_my_blinky".
```

Hitting an error is no big deal; what matters is reading it. It says — `cannot create target "sim" because another target with the same name already exists`. Read it carefully. If reading carefully isn't your thing, go ask Lord Doubao, Lord DeepSeek, or whichever of the other lords you prefer — any of them will do. My recommendation: read it yourself.

Inside the build script of our `01_blinky` there is a custom target named `sim` (the `--target sim` one-stop pipeline from the previous article), and the `00_my_blinky` you copied carries a `sim` of its own — and **CMake target names are globally unique across the entire project**: collide, and configuration fails. The fix couldn't be more direct: rename the three targets in your copy — `sim`→`my_sim`, `flash`→`my_flash`, `erase`→`my_erase` (**and remember to bring the `DEPENDS` line along to match, or eerie problems will show up; of course, solving it with CMake variable syntax is an even nicer route — but let's not digress into a CMake lecture here**).

## Get it running

Reconfigure and build:

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arch/stm32f103c8t6.cmake
cmake --build build --target my_blinky
```

The real output at the tail of the build:

```text
[13/13] Linking CXX executable examples/00_my_blinky/my_blinky; objcopy -> bin, report size
   text    data     bss     dec     hex filename
   5500      12       4    5516    158c .../my_blinky
```

Waah! It didn't cry — it just spat up its own birth weight, which in an actual infant would be grounds for outright panic. For us, it is a moment to celebrate: our first firmware has been born, 5500 bytes. The exact same weight as `01_blinky`, because `main.cpp` hasn't been touched yet — right now it is blinky's twin. Open `00_my_blinky/renode.resc` and append a sampling macro at the end of the file:

```text
macro sample
"""
    pause
    emulation RunFor "0.25"
    sysbus ReadDoubleWord 0x4001100C
    (repeat those two lines seven more times)
    start
"""
```

Two details inside the macro deserve attention: leading with `pause` is mandatory — `RunFor` refuses to run while the machine is running — and the closing `start` lets the machine carry on after the samples are taken. Macros are loaded together with the script, so adding one means restarting the simulator:

```bash
cmake --build build --target my_sim
```

Once the machine has come to rest at the monitor prompt, a single command buys us eight sample points:

```text
(machine-0) runMacro $sample
```

And with that, we get the following numbers laid out in a row:

```text
0x00002000  0x00002000  0x00000000  0x00000000
0x00002000  0x00002000  0x00000000  0x00000000
```

`0x2000` and `0x0000` each claim four slots, alternating in pairs (half-period 500 ms) — behavior confirmed, it really is blinking. The full principle behind this criterion (why 250 ms, and how aliasing sets its traps) was settled in the previous article; from this article onward, it is simply an on-call macro in your resc. When I put the LLM on odd jobs like this, it proudly declared: **your own firmware, your own script**. I rather liked the phrasing, so it stays.

And what if you forgot to add that `add_subdirectory` line to `examples/CMakeLists.txt`? The build answers with exactly this:

```text
ninja: error: unknown target 'my_blinky', did you mean 'blinky'?
```

ninja is quite polite about it — it even makes a guess for you. When you see `unknown target`, check the registration first, then the spelling.

## What's actually written in your firmware

Strike while the iron is hot: let's read `00_my_blinky/CMakeLists.txt` segment by segment — it is the complete definition of "one firmware":

```cmake
add_executable(my_blinky
    main.cpp
    stm32f1xx_it.c # interrupt service routines: which handlers exist is decided by this firmware
    syscalls.c     # newlib stubs
)
```

Let's go segment by segment. The first declares the executable target — and mind the two C files: `stm32f1xx_it.c` and `syscalls.c` **live on the firmware side, not inside the library**. References to these symbols, from the interrupt vector table and from newlib, only appear late in linking; parked inside a library, they would be dropped by the "not referenced, not pulled in" rule — whoever owns them answers for them. Next, linking:

```cmake
target_link_libraries(my_blinky PRIVATE hal)
target_link_options(my_blinky PRIVATE
    -T${CMAKE_SOURCE_DIR}/cmake/arch/stm32f103c8t6.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/my_blinky.map
)
```

`hal` is the static library the family package builds; the linker script passed via `-T` fixes the memory layout (64K Flash / 20K SRAM — where code lives and where data live is entirely its call); `-Map` has the linker emit one extra artifact, a map file that records how many bytes each function takes — and that `.map` will be a leading actor later in our "binary analysis" topic. Finally, the finishing pass:

```cmake
add_custom_command(TARGET my_blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:my_blinky> my_blinky.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:my_blinky>
    COMMENT "objcopy -> bin, report size"
)
```

On this finishing segment: the linked ELF cannot be flashed as-is; `objcopy` strips it down to the raw binary `my_blinky.bin`, and `size` prints the footprint — that three-column table you just saw. Further down, the `my_sim` segment is an old acquaintance from the name-collision mine: it starts a headless Renode that loads this resc, with `WORKING_DIRECTORY` pinned to the repository root (why? The `@` paths in article 02 covered that).

## Fooled you — it never ran yours at all (call it a small punishment for copying a project

The problems before this at least announced themselves through a megaphone — CMake configuration turned red right in your face, and ninja courteously made guesses for you. The real pit comes later: this next one doesn't report a single word, stays green the whole way, and runs someone else's firmware.

The scenario: a slip of the hand during the renaming. You fixed `add_executable(my_blinky`, but the two `$<TARGET_FILE:blinky>` inside POST_BUILD and the `DEPENDS blinky` of `my_sim` got missed and kept the old name. Guess what CMake reports? Nothing whatsoever. We said earlier that target names are globally unique and that a collision fails configuration

Run that mechanism in reverse and it hands you a big one: the name `blinky` **can be found** over in `01_blinky`, so the references you forgot to rename inside your firmware silently resolve onto someone else's target. Your `objcopy` copies out 01's ELF; your `my_flash` flashes 01's firmware onto the board — and not a single voice raises the alarm along the way.

And it doesn't stop there. The `$bin` inside `renode.resc` is a hand-written file path; once the target is renamed, that path is left hanging in mid-air — and ninja never sweeps away orphaned artifacts, so the old ELF built before the rename still lies comfortably in `build/`, and LoadELF succeeds every single time. Thankfully, the log gives it away —

```text
[4/5] Linking CXX executable examples/00_my_blinky/blinky; objcopy -> bin, report size
   4716      12       4    4732    127c .../build/examples/00_my_blinky/blinky
[4/5] cd ... && renode --console --disable-xwt -e include @.../examples/01_blinky/renode.resc
...
(monitor) include @.../examples/01_blinky/renode.resc
11:38:54.6108 [INFO] sysbus: Loaded SVD: ... Name: STM32F103. Description: STM32F103.
11:38:54.6335 [INFO] sysbus: Loading block of 4716 bytes length at 0x8000000.
```

Yours truly: whoa, hold on — no, no, no! So, everyone, please do read the logs: green, and the behavior looks right, is still not OK.

## Change one line and let the criterion see it

A twin doesn't count as your firmware — change something. Let's turn both `HAL_Delay(500)` calls in `main.cpp` into `HAL_Delay(200)`, taking the half-period from 500 ms down to 200 ms. Rebuild, sample in Renode, still at 250 ms intervals; the real output:

```text
0x00000000  0x00002000  0x00000000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

Compare with before the change: the 500 ms version showed up in pairs (two samples landing inside the same half-period); the 200 ms version no longer pairs (the half-period is now shorter than the sampling interval, so the reading jumps almost every time). **You changed one line of code, and the entire observation pattern changed shape** — that is what the previous lesson's criterion was for: any behavioral change in the firmware leaves a trace in the sample sequence.

One more gift for those of you who love sweating the details: after the change, size dropped from 5500 to 5496 — 4 bytes saved. Why? `200` fits into the immediate field of the 16-bit `movs` instruction (0 to 255); `500` doesn't, so it has to ride the 32-bit `mov.w` — each of the two delays grows by 2 bytes, and that is precisely the 4-byte gap. A one-line code change, priced in plain figures in the disassembly.
