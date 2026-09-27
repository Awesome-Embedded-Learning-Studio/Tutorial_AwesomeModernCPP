---
title: "The final verdict sits in the map file: the complete evidence chain that, at this trim level, not even the strings make it into the firmware"
description: "Verifying compile-time trimming on the host runs into three traps (an empty sink gets the whole chain deleted, constant lines get folded into immediates, literals turn straight into movabs). From those failures this article walks to the authoritative verdict on ARM: a dual map-and-bin check, a MinLevel=Info/Debug contrast experiment (+508 bytes versus zero residue), and lands the B contract's baud-derived try_send timeout along the way"
chapter: 6
order: 5
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 工具链
  - 寄存器
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 5
prerequisites:
  - "Two compiler flags cut the firmware by 64%: a field record of function-granularity reclamation with -ffunction-sections"
related:
  - "A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/05-map-is-the-verdict.md
  source_hash: e59b9b4d0acc769169e7e4f69a672235ef93bf9cd94215bdfa111a5bf5279278
  translated_at: '2026-09-27T06:01:22+00:00'
  engine: anthropic
  token_count: 1600
---

# The final verdict sits in the map file: the complete evidence chain that, at this trim level, not even the strings make it into the firmware

## Introduction: three ways one claim died

The logger component's core claim: **log records below `MinLevel`, string literals included, never make it into the firmware**. Writing the code alone cannot make that sentence true — you have to produce evidence. Our first verification idea was the naive one: compile a test program carrying a marker string on the host, run `strings`, and the answer falls out. That idea died three times, differently each time — and those three failures are the most valuable part of this article, because they show three distinct ways the optimizer can "fool" a measurement. In the end we went to the map file and the bin of a real ARM firmware and came back with the authoritative verdict.

## Three traps: why host-side measurement cannot be trusted

**Trap one: an empty sink makes the entire logging chain evaporate.** The sink in the test was an empty function; the compiler correctly decided the log had no observable effect, and in the disassembly the wrapper function was down to a lone `bx lr`. To measure overhead at all, the sink must have a `volatile` side effect.

**Trap two: constant input folds the whole line at compile time.** Once the sink was made observable, we measured again with a constant clock — the compiler computed the entire line assembly at compile time, and a single `movs r2, #26` produced the 26-byte line length. Zero stack frame, zero cycles. A zero-overhead delight, but as a measurement method it is a disaster: the input has to come from a `volatile`.

**Trap three: the literal never lands in `.rodata` in the first place.** The sneakiest layer. We planted 23-byte and 38-byte marker strings in one trimmed level and one kept level respectively and ran `strings` on both — the result: **neither one showed up**. The disassembly explained why: on x86-64 GCC breaks the string content into `movabs` immediates written straight into the instruction stream (`movabs $0x346dc5d63886594b,%rsi`), so of course .rodata holds nothing. In other words, a host-side `strings` check can both **miss** (what should be there is not found) and **mislead** (absence does not mean the string was trimmed).

Conclusion: the host is a development environment, not a sentencing environment. **The final verdict on trimming sits in the target machine's artifacts.**

## The ARM final verdict: cross-checking the map and the bin

The verification design: plant one debug-level marker line (`debug-marker-must-not-survive-MinLevel-Info`) in the example firmware, build the real STM32 firmware twice — once with `MinLevel=Info`, once with `MinLevel=Debug` — then:

```sh
grep -c 'must-not-survive' build/examples/06_log/log_example.map
arm-none-eabi-strings -a build/examples/06_log/log_example.bin | grep -c must-not-survive
```

The verdict:

| MinLevel | text | marker in map | marker in bin |
|---|---|---|---|
| Debug | 18708 | 0 | **1** (level compiled in, +508 bytes) |
| Info | 18200 | 0 | **0** (zero residue, even the literal) |

Three facts settled in one stroke: **with the level on it goes in, with the level off it is gone, and the price difference is 508 bytes**. One detail worth noting along the way: the map file reads 0 on both sides — the map indexes symbols and sections, while a string literal is anonymous `.rodata` data that produces no symbol. So **`strings` over the bin is the correct probe at the literal level**, and the map is what answers "which function/section got trimmed".

There is a second safety net at runtime: the automated Renode verification script asserts over the byte stream that the `[Debug` prefix never appears — if compile-time trimming ever breaks because of an unexpected build configuration, the runtime check screams immediately. The evidence chain is thus three layers: disassembly (host development time), bin/map (target artifacts), stream content (runtime).

## The B contract that landed along the way: try_send and the derived timeout

The logger's sink contract is "best effort: swallow errors, never trap, never block unboundedly". The last clause hides a deep pit: when `HAL_UART_Transmit` is called with `HAL_MAX_DELAY` and the UART clock was never enabled, the status flag never arrives, and the function **hangs forever** — it never even gets to the "return an error" step. So the real guarantee of the B contract is not discarding the return value, but a **bounded timeout**.

That is why the UART driver gained `try_send`: it coexists with the original `send` (the driver contract that keeps trap semantics), and its timeout is not a constant pulled out of thin air but is **derived from the baud rate known at compile time** — 8N1 puts 10 line bits on the wire per byte, so milliseconds = byte count × 10000 ÷ baud rate, then doubled to leave margin. A fixed constant fails exactly here: at 9600 baud a 128-byte line legitimately takes about 133 ms, so a hardcoded 100 ms would wrongly kill a **link that is alive but slow**; the derived timeout stays self-consistent at any baud rate. The cost of a dead serial link changes from "the whole board hangs" to "every log line waits out one bounded timeout" — that is the honest price of the best-effort contract.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| `strings` on the host finds no literal | GCC inlines strings as movabs immediates | Put the final verdict on the ARM artifacts' map/bin |
| Searching the map for a string usually finds nothing | Literals carry no symbol; anonymous .rodata | Literals via bin+strings, symbols via map |
| A trimming check where "the numbers don't change" | Empty/constant input folded or eliminated by DSE | volatile input + observable sink |
| try_send falsely reports failure at slow baud rates | A fixed timeout constant | Derive from the baud rate (bytes × 10 bits ÷ baud × 2) |

## Summary

- A compile-time trimming claim needs three layers of evidence: host disassembly (development-time intuition), target-artifact map/bin (the authoritative verdict), runtime stream verification (the safety net);
- Host-side measurement has three traps: DSE, constant folding, movabs immediates — `strings` on the host is weak evidence;
- The contrast experiment settles it with one stroke of the gavel: two builds with `MinLevel=Info/Debug`, marker zero-residue/present, price difference 508 bytes;
- The real guarantee of "never block unboundedly" is a derived timeout, not discarding the return value — `HAL_MAX_DELAY` on dead hardware is a permanent hang.

## References

- [GNU ld manual: Options (--gc-sections)](https://sourceware.org/binutils/docs/ld/Options.html)
- [arm-none-eabi binutils: strings / objdump](https://sourceware.org/binutils/docs/binutils/strings.html)
- [libestdx repository (examples/06_log and the logger module)](https://github.com/Charliechen114514/libestdx)
