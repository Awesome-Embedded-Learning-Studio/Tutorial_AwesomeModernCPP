---
title: "Two compiler flags cut the firmware by 64%: a field record of function-granularity reclamation with -ffunction-sections"
description: "While wiring the logger into the build system we also added -ffunction-sections -fdata-sections in passing, and all five existing example firmwares slimmed down by 37%–64% — this article hands over the full data table, a linker-perspective explanation of the mechanism, the cache pitfall where editing the toolchain file takes no effect, and the record-the-baseline-before-touching-anything methodology"
chapter: 6
order: 4
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 工具链
  - 链接器
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 5
prerequisites:
  - "A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly"
related:
  - "The logger's stack budget, measured: can a function-local LineBuffer blow the stack?"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/04-two-flags-size-cut.md
  source_hash: 5da8e4def4281df384a61a97b23fab9127127845e332be3899b6455e77b55b0f
  translated_at: '2026-09-27T06:00:18+00:00'
  engine: anthropic
  token_count: 3400
---

# Two compiler flags cut the firmware by 64%: a field record of function-granularity reclamation with -ffunction-sections

## Introduction: a counterintuitive starting point

The libestdx linker script has had `-Wl,--gc-sections` hanging on it all along — "linker garbage collection" looked like it had been on for ages. Yet when we wired the logging module into the build and, in passing, recorded each example firmware's size as a baseline, the numbers were perfectly calm: blinky sat steadily at 5500 bytes. The problem is that `--gc-sections` was on yet had almost nothing to do, because **its unit of reclamation is the section, and under the default layout the code of an entire `.o` file is squeezed into a single `.text` section** — as long as any one function in that object file is referenced, all of them board the firmware together. STM32's HAL driver files routinely pack dozens of functions, of which perhaps three to five are actually used; the rest just keep them company into flash.

Getting `--gc-sections` to actually pull its weight takes only two compiler flags:

```cmake
set(MCU_FLAGS "-mcpu=cortex-m3 -mthumb -ffunction-sections -fdata-sections")
```

## The mechanism: partitioning the "room" into "cells"

`-ffunction-sections` has the compiler place **every function** into its own section (`-fdata-sections` does the same for data). When the linker garbage-collects, it starts from the entry point and the KEEP-marked sections, runs reachability analysis along the reference chains, and discards unreachable cells at **function granularity**. The GNU `ld` manual and the various toolchain vendors' documents describe this pairing consistently ([GNU ld documentation](https://sourceware.org/binutils/docs/ld/Options.html), [Microchip's function-sections notes](https://onlinedocs.microchip.com/guides/content.xhtml?AID=1669576)); two engineering caveats are also on record: critical sections such as the vector table need protection via `KEEP()` or `SHF_GNU_RETAIN` ([AdaCore documentation](https://docs.adacore.com/gnat_ugx_docs/views/gnat_ugx/building_executable_programs_with_gnat.html#the-order-of-sections-and-allocation-of-variables), [MaskRay's analysis](https://maskray.me/blog/2021-01-31-metadata-sections-comdat-and-shf-link-order)), plus the leftover-debug-info problem for collected sections ([LLVM forum discussion](https://discourse.llvm.org/t/lld-how-to-get-rid-of-debug-info-of-sections-deleted-by-gc-sections/13983)).

## Measured: the slimming ledger of five firmwares

Record the baseline before touching anything — that is this article's first methodological point: **before changing build flags, archive every artifact's `arm-none-eabi-size` output**, then compare after the change. "Optimization" without a baseline is fortune-telling.

| Firmware | Baseline (text) | With the flags | Change |
|---|---|---|---|
| blinky | 5500 | 3468 | **−37%** |
| gpio_example | 5500 | 3468 | −37% |
| led_example | 5520 | 3476 | −37% |
| button_example | 5552 | 3484 | −37% |
| uart_example | 15268 | 5476 | **−64%** |

blinky only uses `HAL_Init`/`HAL_Delay`, but every other HAL function inside the linked object files such as `stm32f1xx_hal.c` was dragged in — now those cells get reclaimed whole. uart_example was hit hardest and benefited most: in the UART-related HAL and the fine-grained RCC paths, half the functions were never reached at all — **15268 → 5476, nearly 10KB cut**.

## The pitfall: the toolchain file was edited, yet not a single number budged

After the first `MCU_FLAGS` edit and a fresh `cmake --build`, all five firmwares came out **byte-for-byte identical** to the baseline — the flags had not taken effect at all. The reason: the `CMAKE_C_FLAGS_INIT` family in the CMake toolchain file is **written into the cache only when the build directory is first configured**; later edits to the toolchain file do not propagate to the existing `CMAKE_C_FLAGS`. The verification is dead simple: look up `CMAKE_C_FLAGS:STRING` in `build/CMakeCache.txt` and you will still see the old value. The fix is to delete the build directory and reconfigure. This lesson deserves a page in any project's build documentation: **editing the toolchain file = reconfigure, not an incremental build**.

While we were at it, we also slipped in `-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.` (with a `$<$<COMPILE_LANGUAGE:C,CXX>:...>` generator expression to keep it away from the assembler): by default `source_location` and `__FILE__` bake the build machine's absolute paths into the firmware, so every log site was paying flash for them; prefix-map turns them into relative paths and is likewise a prerequisite for reproducible builds.

## Regression: flags cannot be judged by size alone

Once the reclamation granularity goes finer, you must confirm that no section that **ought to stay alive** was collected along with the dead ones. We ran two layers of regression: a full rebuild of the five examples (zero warnings and zero failures across compile and link), plus the Renode automated check `check_uart_renode` — the serial console's welcome banner, command responses, line editing, and backspace were all verified byte for byte. Size optimization must always be handed over together with its functional regression report.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| Flags added, size does not budge | toolchain `*_FLAGS_INIT` only enters the cache at first configure | Delete the build directory and reconfigure, or manually clear the `CMAKE_*_FLAGS` cache |
| Firmware slims down then fails to boot / HardFault | Critical sections such as the vector table got collected | `KEEP()` in the linker script, or `SHF_GNU_RETAIN` |
| Debugger line-stepping goes haywire | Leftover debug info for collected sections | Regenerate debug symbols; strip release builds |
| `-ffile-prefix-map` warns on `.S` files | The assembler does not recognize the flag | Restrict it to C/CXX with a generator expression |

## Wrap-up

- `--gc-sections` hanging there alone is basically a decoration; `-ffunction-sections -fdata-sections` is what drops the reclamation granularity down to function level;
- Measured across the five examples, sizes slimmed down 37%–64%, with the uart example losing nearly 10KB — the big HAL object files are the biggest beneficiaries;
- Toolchain file edits only take effect after the build directory is reconfigured; when "the numbers did not change", check the cache first;
- Record the baseline before changing flags and run functional regression afterwards — two small habits that turn "optimization" into engineering.

This batch of data doubles as the logging module's seed capital: in the next article, the logger's own firmware will be measured for size standing on this freshly trimmed baseline.

## References

- [GNU ld manual: Options (--gc-sections)](https://sourceware.org/binutils/docs/ld/Options.html)
- [Microchip: Function-sections Option](https://onlinedocs.microchip.com/guides/content.xhtml?AID=1669576)
- [MaskRay: Metadata sections, COMDAT and SHF_LINK_ORDER](https://maskray.me/blog/2021-01-31-metadata-sections-comdat-and-shf-link-order)
- [LLVM Discourse: GC'd sections and debug info](https://discourse.llvm.org/t/lld-how-to-get-rid-of-debug-info-of-sections-deleted-by-gc-sections/13983)
- [libestdx repository](https://github.com/Charliechen114514/libestdx)
