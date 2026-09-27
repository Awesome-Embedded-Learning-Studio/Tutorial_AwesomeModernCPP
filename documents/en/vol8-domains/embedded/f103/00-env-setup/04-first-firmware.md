---
title: "Your very own firmware: adding a target to the repo"
description: "Three articles of theory down; this one is hands-on from start to finish: copy 01_blinky into your own 00_my_blinky, register it with the build system, and live through the real error messages of two mines — the sim target name collision and the forgotten registration — then step on a third mine that never beeps: a half-done rename that leaves the all-green simulation running someone else's firmware; a segment-by-segment anatomy of the CMakeLists, and finally a one-line HAL_Delay change that makes the effect visible under the sampling criterion — plus an instruction-encoding Easter egg where 200 saves 4 bytes over 500"
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
  source_hash: 149f53d0468cedf388a3671456dfe212691744d56a0d313e6316e7f15d5f5ac1
  translated_at: '2026-09-25T08:26:34+00:00'
  engine: anthropic
  token_count: 5100
---
# "Three articles in, and you still haven't let me write a single line"

My apologies for that. I trust you've been coiled up and ready to pounce — phone in hand, or sitting there in your VM or WSL with nowhere to spend the energy. This article asks you to do the work yourself!

> Starting with this article, please put your phone down, get yourself in front of a computer, open WSL — or any distribution you like — and let's take a deep breath and get to work!

## Kick off the project — by copying

Thanks are due to my mentor from work: back when I was optimizing the project's CSS parser, he had me copy from Google's Blink CSS. "Copying isn't shameful; failing to copy well — that's indefensible." Not everything has to start from frame zero. And hands-on work goes better when there's a handle to grab. Our idea: there is no need to write everything from 0. You can start by tweaking the code and understanding it~

Let's copy `01_blinky` wholesale under a new name, `00_my_blinky` — it sorts to the very front, so one glance at `ls` and it's unmistakably yours:

```bash
cd libestdx
cp -r examples/01_blinky examples/00_my_blinky
```

Step inside and change the names in three places: swap every `01_blinky`/`blinky` in `CMakeLists.txt` and `renode.resc` for `00_my_blinky`/`my_blinky`, and rewrite the comment at the top of `main.cpp` in your own words. Then comes the critical step: let the build system know it exists. Open `examples/CMakeLists.txt`; its current contents look like this:

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

While our hands are still warm, let's make sense of the directory layout. This project has three layers: at the top, **top-level orchestration** (the root `CMakeLists.txt`, which sets the standards and hooks up subdirectories); then the **family package** (`include/libestdx/boards/stm32f1/`, which compiles the official HAL into the `hal` static library — this is where the "wheels, explained but not reinvented" part lives); and the **firmwares** (under `examples/`, one executable target per directory, each consuming `hal`). That copy-plus-one-line you just did added a new resident to the third layer.

## Hahaha, you got played, buddy

Alright, now run it...

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

Hitting an error is no big deal; what matters is reading it. It says — `cannot create target "sim" because another target with the same name already exists`. Read it carefully. If you won't read carefully, go ask Lord Doubao, Lord DeepSeek, or whichever of the other lords you prefer — any of them will do. My recommendation: read it yourself.

Inside the build script of our `01_blinky` there's a custom target called `sim` (the `--target sim` full pipeline from the previous article), and the `00_my_blinky` you copied has a `sim` too — and **CMake target names are globally unique across the entire project**; a collision fails configuration. The fix is super direct: rename the three targets in your copy — `sim`→`my_sim`, `flash`→`my_flash`, `erase`→`my_erase` (**and remember to bring the `DEPENDS` line along too, or bizarre problems will show up; of course, solving it with CMake variable syntax is an even nicer way — but let's not wander off into a CMake lecture here**).

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

Waah! It didn't cry — it just spat up its own birth weight, which would be downright terrifying in an actual baby. For us, it's a moment to celebrate: our first firmware has been born, 5500 bytes. The exact same weight as `01_blinky`, because `main.cpp` hasn't been touched — right now it is blinky's twin. Open `00_my_blinky/renode.resc` and add a sampling macro at the end of the file:

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

Note two details inside the macro: `pause` at the head is mandatory — `RunFor` refuses to run while the machine is running — and the closing `start` lets the machine carry on after sampling. Macros load together with the script, so once you add one you must restart the simulator:

```bash
cmake --build build --target my_sim
```

After the machine parks at the monitor prompt, one command buys us eight sample points:

```text
(machine-0) runMacro $sample
```

And with that, we get the following numbers laid out in a row:

```text
0x00002000  0x00002000  0x00000000  0x00000000
0x00002000  0x00002000  0x00000000  0x00000000
```

`0x2000` and `0x0000` each take four slots, alternating in pairs (half-period 500 ms) — behavior confirmed, it really is blinking. The full principle behind this criterion (why 250 ms, and how aliasing trips people up) was settled in the previous article; from this article on, it's simply an on-call macro in your resc. When I handed an LLM this odd job, it proudly declared: **your own firmware, your own script**. I liked the phrasing, so it stays.

And if you forgot to add that `add_subdirectory` line to `examples/CMakeLists.txt`? The build reports exactly this:

```text
ninja: error: unknown target 'my_blinky', did you mean 'blinky'?
```

ninja is rather polite about it — it even makes a guess for you. When you see `unknown target`, check the registration first, then the spelling.

## What's written in your firmware

Strike while the iron is hot: let's read `00_my_blinky/CMakeLists.txt` segment by segment — it is the complete definition of "one firmware":

```cmake
add_executable(my_blinky
    main.cpp
    stm32f1xx_it.c # interrupt service routines: which handlers exist is decided by this firmware
    syscalls.c     # newlib stubs
)
```

Let's go segment by segment. The first declares the executable target. Mind the two C files: `stm32f1xx_it.c` and `syscalls.c` **live on the firmware side, not in the library**. The interrupt vector table's and newlib's references to these symbols only appear late in linking; parked inside a library, they'd be dropped by the "not referenced, not pulled in" rule — whoever owns them answers for them. Next, linking:

```cmake
target_link_libraries(my_blinky PRIVATE hal)
target_link_options(my_blinky PRIVATE
    -T${CMAKE_SOURCE_DIR}/cmake/arch/stm32f103c8t6.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/my_blinky.map
)
```

`hal` is the static library the family package builds; the linker script passed via `-T` fixes the memory layout (64K Flash / 20K SRAM — where code lives and where data live is entirely its call); `-Map` has the linker emit one extra artifact, a map file recording how many bytes each function takes — this `.map` will be a lead actor later in our "binary analysis" topic. Finally, the finishing pass:

```cmake
add_custom_command(TARGET my_blinky POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:my_blinky> my_blinky.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:my_blinky>
    COMMENT "objcopy -> bin, report size"
)
```

Now this finishing segment: the linked ELF can't be flashed as-is; `objcopy` strips it into the raw binary `my_blinky.bin`, and `size` prints the footprint — that three-column table you just saw. Further down, the `my_sim` segment — you already met it in the name-collision mine: it starts a headless Renode that loads this resc, with `WORKING_DIRECTORY` pinned to the repository root (why? The `@` paths in article 02 covered that).

## Fooled you — it never ran yours at all (call it a small punishment for copying the project

The problems before this at least announced themselves through a loudhailer — CMake configuration turned red right in your face, and ninja courteously made guesses for you. The real pit comes later: this next one doesn't report a single word, stays green the whole way, and runs someone else's firmware.

The scenario: a slip of the hand during the renaming. You changed `add_executable(my_blinky`, but the two `$<TARGET_FILE:blinky>` inside POST_BUILD and the `DEPENDS blinky` of `my_sim` got missed and kept the old name. Guess what CMake reports? Nothing whatsoever. We said earlier that target names are globally unique and collisions fail configuration

Run that mechanism in reverse and it hands you a big one: the name `blinky` **can be found** over in `01_blinky`, so the references you forgot to rename inside your firmware silently resolve onto someone else's target. Your `objcopy` copies out 01's ELF, your `my_flash` flashes 01's firmware onto the board, and not a single voice cries out along the way.

And it doesn't stop there. The `$bin` inside `renode.resc` is a hand-written file path; once the target is renamed, that path hangs in mid-air — and ninja never sweeps away orphaned artifacts, so the old ELF built before the rename still lies comfortably in `build/`, and LoadELF succeeds every single time. Thankfully, the log gives it away —

```text
[4/5] Linking CXX executable examples/00_my_blinky/blinky; objcopy -> bin, report size
   4716      12       4    4732    127c .../build/examples/00_my_blinky/blinky
[4/5] cd ... && renode --console --disable-xwt -e include @.../examples/01_blinky/renode.resc
...
(monitor) include @.../examples/01_blinky/renode.resc
11:38:54.6108 [INFO] sysbus: Loaded SVD: ... Name: STM32F103. Description: STM32F103.
11:38:54.6335 [INFO] sysbus: Loading block of 4716 bytes length at 0x8000000.
```

Yours truly: whoa, hold on! No, no, no! So please, everyone, always read the logs: green, and the behavior looks right — still not OK.

## Change one line, and let the criterion see it

A twin doesn't count as your firmware — change something. Let's change both `HAL_Delay(500)` calls in `main.cpp` to `HAL_Delay(200)` — the half-period goes from 500 ms to 200 ms. Rebuild, sample in Renode, still at 250 ms intervals; the real output:

```text
0x00000000  0x00002000  0x00000000  0x00000000
0x00002000  0x00000000  0x00002000  0x00000000
```

Compare with before the change: the 500 ms version showed up in pairs (two samples landing inside the same half-period); the 200 ms version no longer pairs (the half-period is now shorter than the sampling interval, so the reading jumps almost every time). **You changed one line of code, and the whole observation pattern changed shape** — that is the point of the previous lesson's criterion: any behavioral change in the firmware leaves a trace in the sample sequence.

One more gift for those of you who love sweating the details: after the change, size dropped from 5500 to 5496 — 4 bytes saved. Why? `200` fits into the immediate of the 16-bit `movs` instruction (0 to 255); `500` doesn't, so it has to use the 32-bit `mov.w` — each of the two delays grows by 2 bytes, and that is precisely the 4-byte gap. A one-line code change, priced out in plain sight in the disassembly.
