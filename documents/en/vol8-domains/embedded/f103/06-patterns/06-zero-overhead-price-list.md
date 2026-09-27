---
title: "The price list of zero overhead: dissecting the 18,200-byte logger firmware"
description: "We cut the logger example firmware open with nm --size-sort: the single function main eats 8.2KB — the assembly chain is inlined into every call site keyed by argument-type combination; the render triplets run 1.3KB each and write_integral 724B... 'zero overhead' does not mean zero size, it means zero abstraction tax; this article lays every item's real price out on one list"
chapter: 6
order: 6
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 嵌入式
  - 实战
  - 优化
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 4
prerequisites:
  - "The final verdict sits in the map file: the complete evidence chain that, at this trim level, not even the strings make it into the firmware"
related:
  - "Two compiler flags cut the firmware by 64%: a field record of function-granularity reclamation with -ffunction-sections"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/06-zero-overhead-price-list.md
  source_hash: bae48fbfb178024f2df3a34bd6832d498bdf3f0756d127bb77f2e993047fa440
  translated_at: '2026-09-27T06:00:16+00:00'
  engine: anthropic
  token_count: 3000
---

# The price list of zero overhead: dissecting the 18,200-byte logger firmware

## Introduction: "zero overhead" does not mean "zero size"

The logger example firmware measures in at 18200 bytes of text, while the neighboring handwritten serial console, uart_example, is only 5476 — a 12.7KB gap. Someone will ask: wasn't the whole point zero overhead? Which makes this exactly the moment to pin down what "zero-overhead abstraction" actually claims: **paying no runtime tax for the abstraction machinery itself** (virtual calls, type erasure, dynamic dispatch, interpreter loops) — not a zero-size artifact. Templates compile all of the "dispatch" into straight-line code, and straight-line code has size. This article cuts the firmware open with `nm --size-sort` and draws up an honest price list for "zero overhead" — what every feature costs, at a glance.

## Dissection: who is eating the flash

```sh
arm-none-eabi-nm --size-sort -t d build/examples/06_log/log_example | tail -12
```

The top of the size-sorted roster (an excerpt, in bytes):

| Symbol | Size | What it is |
|---|---|---|
| `main` | **8224** | The scene after the entire log assembly chain was inlined in |
| `render<string_view,...>` ×3 | 1368/1364/1300 | Three instantiations of format-string rendering (different argument combinations) |
| `HAL_RCC_OscConfig` | 1004 | HAL clock tree |
| `HAL_GPIO_Init` | 1060 | HAL GPIO |
| `write_integral<uint32_t>` | 724 | The to_chars integer path |
| `HAL_RCC_ClockConfig` | 384 | HAL |
| `HAL_UART_Transmit` | 290 | HAL transmit |
| `append_escaped` | 284 | Escape folding |

Two facts jump out at once:

**First, the single function `main` accounts for 45% of the firmware.** It is not that some "library component" is big — it is **inlining** that spreads the logger's entire machinery into the call sites. The example has five logging calls, each with a different combination of argument types (the concatenation-style `blink=n` and the format-style `n={} hex={:x}` each expand on their own), and at every call site the `emit`/`append_header`/`render` set is instantiated against its own type combination and fully inlined. COMDAT deduplication can only rescue **same-signature instantiations across translation units**; different argument combinations inside the same main were never the same piece of code to begin with.

**Second, the "triplets" phenomenon.** The three `render`s run 1.3KB each — one and the same template source, generating one copy each for three different argument-type sets (no-argument tail, string_view head, uint32_t head...). This is the bedrock of the template size model: **the price follows the number of type combinations, not the number of source lines**.

## What the 12.7KB price gap buys

The unfair part of comparing against a handwritten console is that the price lists differ. Here is the 12.7KB price list: two syntaxes (concatenation plus format strings), compile-time field validation (count/spec/per-field type, with errors that point straight at the offending field), millisecond timestamps, macro-free `source_location` metadata, the level-trimming mechanism, truncation safety, and an open extension point for custom types (`Formatter<T>` specializations). The handwritten console, meanwhile, is "one hand-written string per response". **What you are buying is "logging infrastructure", not "a few lines of output"** — whether this price list is acceptable on a 64KB BluePill depends on how much weight logging carries in the project; and a board with 20KB of RAM at least never has to lose sleep over the 1KB of lookup tables `std::format` drags in (our entire integer path is 724 bytes).

## The price-adjustment knobs (a v2 topic — booked, not turned)

With the price list in hand, the cost-cutting directions are clear as well; here we only book them, no touching anything yet:

- **`-Os`, or splitting optimization per target**: switching the tutorial library's default `-O3` to `-Os` makes inlining decisions far more conservative, and shapes like "main eats 8KB" will converge significantly;
- **`noinline` at key nodes**: keeping the shared skeleton of `emit`/`render` out of inlining lets multiple call sites share one copy of the machinery, at the price of per-call call overhead and the partial constant folding that gets given up;
- **Fewer type combinations**: keeping argument types regular when logging (standardize on `uint32_t` instead of mixing int/unsigned) drops the number of instantiations directly;
- **Smaller `LINE_BYTES`**: the line buffer is stack, not flash, but truncation kicks in earlier.

Each knob's payoff has to be measured afresh, and the price list will move with them — what does not change is the method: **`nm --size-sort` first, optimization talk second**.

## Summary

- "Zero overhead" = zero abstraction tax, not zero size; template dispatch compiles into straight-line code, and straight-line code has a price;
- The top of the price list is inlining: `main` at 8.2KB (45%), because every logging call generates its own set of machinery keyed to its argument-type combination;
- Multiple instantiations of one template (the render triplets) are the bedrock of the template size model: the price follows the number of type combinations;
- The 12.7KB buys logging infrastructure (dual syntax/compile-time validation/timestamps/trimming/extension points), not a few lines of output;
- The cost-cutting knobs (-Os, noinline, regularized types) get booked first and experimented on after — measure, then move.

## References

- [binutils: nm and --size-sort](https://sourceware.org/binutils/docs/binutils/nm.html)
- [GCC optimize options (-O3/-Os/inline limits)](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
- [the libestdx repository (the logger module and examples/06_log)](https://github.com/Charliechen114514/libestdx)
