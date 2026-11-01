---
title: 'Putting the logger to work: two calling styles and two viewing surfaces'
description: 'The 69-line main of examples/06_log covers the usage face of the logger: how to choose between the concatenating and format-string styles (one Formatter foundation, compile-time checks through beacon functions, render priced per argument combination), the [501ms][Info ][led] line format matched field by field against the source, two cautions on millisecond timestamps, MinLevel trimming verified with a single strings command, and the two viewing surfaces, PTY automation for scripts and showAnalyzer for people, closing with the v1 boundaries and custom Formatters'
chapter: 6
order: 7
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 12
prerequisites:
  - 'A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly'
related:
  - 'The final verdict sits in the map file: the complete evidence chain that, at this trim level, not even the strings make it into the firmware'
  - 'From overload sets to Formatter specializations: choosing the dispatch mechanism for an embedded logger'
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/07-logger-dual-syntax-usage.md
  source_hash: 05f4c7f69a3bb3dc24cca5a1da6bb08d6b1fd503e68335fdad613fa38d31b437
  translated_at: '2026-11-01T05:53:12+00:00'
  engine: anthropic
  token_count: 3500
---

# Putting the logger to work: two calling styles and two viewing surfaces

## The opening: the internals are done, this piece is about using them

The logger series reaches its seventh piece, and the first six were all internals:

- How `Tag` carries the call-site location along: [A zero-overhead logger](01-logger-zero-overhead.md) worked through the complete derivation.
- Whether a function-local `LineBuffer` can blow the stack: [The logger's stack budget, measured](03-logger-stack-budget.md) pinned the transient frame at 376 bytes.
- Which three layers of evidence back the trimming claim: [The final verdict sits in the map file](05-map-is-the-verdict.md) delivered the ruling.
- Where all 18,200 bytes went: [The price list of zero overhead](06-zero-overhead-price-list.md) itemized the bill.

Why trust it? The first six pieces have answered that. So how do you wire it into your own project? That question is this piece's job. For material I picked libestdx's `examples/06_log`: `main.cpp` is all of 69 lines, and the two calling styles, the line format, the timestamps, trim verification, and the two viewing surfaces are all strung together inside it.

We ran it and looked at the output; what you see is the log stream captured live from Renode:

```text
[0ms][Error][boot] libestdx 07_log example
[0ms][Warn ][boot] MinLevel=Info, clock=HalTickClock
[0ms][Info ][boot] watch PTY /tmp/libestdx-log
[501ms][Info ][led] blink=1
[501ms][Info ][ledf] n=1 hex=0x1
[1002ms][Info ][led] blink=2
[1002ms][Info ][ledf] n=2 hex=0x2
```

Three things stand out in the output:

- The three boot lines use Error, Warn, and Info, and the level field is five columns wide in every one of them.
- In the main loop, each calling style prints one line.
- The timestamps step forward line by line.

The banner says `07_log` while the directory is `06_log`; do they not agree? We copied the output verbatim, not one byte touched. The banner literal has read `07_log` since the day the example was born, the header comment in `renode.resc` carries the same name, and a search through the repository history turns up no `examples/07_log` directory anywhere. It is a historical leftover; you can ignore it.

## Five lines to integrate: the alias is the configuration

All the configuration the integration takes sits in the few lines at the top of `main.cpp`:

```cpp
using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
using Log = estdx::logger::Log<estdx::logger::UartLogSink<Serial>, estdx::logger::LogLevel::Info,
                               128, estdx::stm32f1::HalTickClock>;

static_assert(estdx::logger::LogSink<estdx::logger::UartLogSink<Serial>>);
static_assert(estdx::logger::LogClock<estdx::stm32f1::HalTickClock>);
```

The four template parameters of `Log`, in one breath:

- Who the sink is: in the example, `UartLogSink<Serial>` on USART1.
- The compile-time level floor, `MinLevel`.
- The line buffer width, 128 bytes.
- The clock.

Leave the last three unwritten and you get the defaults `Info`, `128`, `NoClock`; if you do not want timestamps, you can drop the fourth parameter entirely.

That README section we cross-checked against two entry points, and behind them sits one engine: the README demonstrates the engine itself, `Logger<...>::Self().info(...)`, while the example uses the static calling face, `Log<...>`.

My reason for picking `Log` is dialect consistency: like `Uart` and `Gpio` it is all-static with no instances, so the feel of logging matches the feel of blinking an LED.

The two `static_assert` lines are worth learning as well. With the concepts the logger itself defines, the contract check lands right next to the alias:

- `LogSink`: any type that can `write(std::span<const char>)` serves as a sink.
- `LogClock`: any type that can `now_ms()` serves as a clock.

We never wait for an error to explode deep inside the instantiation chain: the compiler reports a type that does not satisfy the check on the spot.

## Two calling styles: one foundation, two feels

The example's main loop, copied over exactly as it is:

```cpp
for (std::uint32_t n = 1;; ++n) {
    Led::toggle();
    HAL_Delay(500);
    Log::info(estdx::logger::Tag("led"), "blink=", n);
    Log::infof(estdx::logger::Tag("ledf"), "n={} hex={:x}", n, n);
}
```

The differences between the two styles, as a short list:

- Concatenating `info`: the arguments enter the buffer one by one in order, a fold expression guarantees that the order is the argument order, and each argument is turned into text by its `Formatter<T>` specialization.
- Format-string `infof`: a `std::format`-style template line.
- The field vocabulary has exactly three entries: `{}` for any formattable value, `{:x}` for an integer in hexadecimal, `{:N}` for fixed width.
- `{:x}` wraps the value in `Hex` internally: the output is `0x` plus lowercase digits with no zero padding; the `hex=0xa` in the log stream is its work.
- `{:N}` pads with spaces only and never truncates.
- <span v-pre>`{{`</span> and <span v-pre>`}}`</span> are the escapes for literal braces.

Is it the same foundation underneath? We checked against the source. The format string's `format_to` still goes through `Formatter` dispatch: literal chunks on both sides end their journey at the same `append(string_view)`. The concatenating style emits `"blink="`, and the format-string style emits its literal chunks the same way. Why do both share one mechanism? [From overload sets to Formatter specializations](02-dispatch-overload-to-formatter.md) took that dispatch apart once already.

The format string hands us one thing more: compile-time checking, prototyped by the P2216R3 proposal for `std::format`. What gets rejected, listed outright:

- A field count that does not match the argument count.
- A spec outside the vocabulary: arg-ids like `{0}` are not supported.
- `{:x}` applied to a non-integer.

Where does the rejection happen? In the consteval constructor of `FormatString`. The reporting style feels rather embedded to us: the checker is made to call a non-constexpr beacon function, a function that exists only so its name can appear in the diagnostic, and the error hands that name to us verbatim. Two names to copy straight out: `format_string_field_count_mismatch`, `format_string_x_requires_integral`. Skipping `consteval throw` is deliberate: on a no-exceptions toolchain I tried it, and even a `throw` branch that provably never runs gets rejected at the syntax level.

So how to choose? My default is the concatenating style, for the absence of a rendering machine: one label and one value per line, as plain as it gets. When does the format string earn its keep? Whenever a line template is fixed and reused, and likewise when a field needs hexadecimal or fixed width; and when you want field mismatches caught at compile time, that is squarely its home turf. The cost goes on the table as well: `render` (the format string's rendering function) generates one instantiation per argument combination too, measured in [The price list of zero overhead](06-zero-overhead-price-list.md); the example's three instantiations are a mere 1.3 KB each, which is how they earned the "triplets" nickname. And the concatenating style? It inlines per combination as well, but without stacking one more layer of `render`. So my rule of thumb in one line: concatenate by default, and reach for the format string when a field actually calls for it.

Counting the entry points, we get six levels with one pair per level: `warn` pairs with `warnf`, and so on down. The boot banner used Error, Warn, and Info, so both shapes of the five-column field are on display at once: Error fills the width, while Warn and Info get one padding space.

## Line format and timestamps: field by field against the source

We take `[501ms][Info ][led] blink=1` and lay it against `append_header` from `logger.hpp`, field by field:

| Field | Content | Source |
|---|---|---|
| `[501ms]` | Millisecond timestamp | `AClock::now_ms()`; in the example, `HalTickClock` (wrapping `HAL_GetTick`) |
| `[Info ]` | Level, fixed width 5 | `LevelName` with `{:5}`; short names get right padding |
| `[led]` | tag | the text of `Tag("led")` |
| `(space)` | one space | the boundary between header and payload |
| `blink=1` | payload | whatever each calling style assembled |
| `\r\n` | line ending | fixed CRLF |

And by the way, the fixed width of the level field is exactly what `{:5}` produces; we had already bumped into it once while reading the line format.

How does configuration change the fields? Three lines:

- With no clock configured (`NoClock`), the timestamp field is never even instantiated: `if constexpr` erases it at compile time.
- `kLocation` defaults to off. Turn it on and a `[file:line]` segment follows the tag; the location lives inside every `Tag`, and how it rides along was covered in [the first piece, A zero-overhead logger](01-logger-zero-overhead.md).
- The README's line format `[123ms][Info ][tag][file:line] payload` notes in parentheses that the timestamp and location segments are trimmed by configuration; what configuration trims is exactly those two, the timestamp and `[file:line]`, while `[tag]` is always there.

Look at `HalTickClock`: it wraps `HAL_GetTick`, and the millisecond count travels as a `std::uint32_t`. How long until it wraps around? About 49.7 days by our arithmetic, so do not subtract two log lines and read the difference as an interval.

All three boot lines read `[0ms]`? We looked into that as well: the banner finishes before the first tick arrives. All zeros is confirmed normal, not a sign that your clock is misconfigured.

The line length gets its own accounting: of the 128 bytes, 3 are always reserved for the tail, so 125 bytes are your content budget, and past that the line is cut. A cut line also gains a trailing `~`, and every line makes exactly one call to `Sink::write`. When you write log lines, keep an eye on the longest one: do not let critical information end up after the `~`.

One knob still turns at run time, `Log::setLevel`, and it opens and closes only within the compile-time floor. In a `MinLevel=Info` firmware the Debug calls were never compiled in at all, so `setLevel(Trace)` at run time cannot bring anything back. Want Debug? Change `MinLevel` and rebuild.

## Verifying the trim: one strings command

We planted a sacrificial marker in the example for verification:

```cpp
Log::debug(estdx::logger::Tag("boot"), "debug-marker-must-not-survive-MinLevel-Info");
```

How do we verify that it is really gone? One command is all it takes:

```sh
arm-none-eabi-strings -a build/examples/06_log/log_example.bin | grep must-not-survive
```

Zero output counts as pass. Change `MinLevel` to `Debug` and build again, and the same command then finds exactly 1 line. We cite the ruling from [the fifth piece, The final verdict sits in the map file](05-map-is-the-verdict.md); the text delta you will see there is 508 bytes (18,200 against 18,708).

One warning bears repeating: the command only counts for the ARM binary. Why does a host build not count? I measured on x86-64, and GCC there splits short literals into `movabs` immediates folded into the instruction stream. So on the host, a strings search that comes up empty leaves two possibilities open: truly trimmed, or merely split apart. The full anatomy of that triple trap belongs to [The final verdict sits in the map file](05-map-is-the-verdict.md).

A second safety net waits at run time. We have `check_renode.py` check the stream content byte by byte and assert that `[Debug` and the marker never appear. What if compile-time trimming fails by accident through a stray build setting? The check screams the moment we run it.

## Two viewing surfaces: PTY for machines, showAnalyzer for humans

Once the log stream comes out of USART1, who reads it? We gave Renode two surfaces: one feeds scripts, the other feeds people.

Surface one is the PTY, the automation face. In `renode.resc` we found exactly two steps:

- `CreateUartPtyTerminal` attaches a host PTY.
- `connector Connect` wires it to USART1.

The default path stays at `/tmp/libestdx-log`.

By the way, the question marks in `$bin?=` and `$uart_pty?=` we confirmed separately: they mean "overridable defaults". And `check_renode.py` feeds on exactly that: we have it point the PTY at a temporary directory before including the script. Cleanup takes care of itself afterwards, because the temporary directory disappears on its own.

How does the check work? Byte-level matching, marker by marker:

```python
(b"[Error][boot] libestdx 07_log example\r\n", 10),
(b"[Warn ][boot] MinLevel=Info, clock=HalTickClock\r\n", 5),
(b"[Info ][boot] watch PTY /tmp/libestdx-log\r\n", 5),
(b"[Info ][led] blink=1\r\n", 15),
(b"[Info ][ledf] n=1 hex=0x1\r\n", 5),
```

Two details are worth pointing at. One is the trailing space inside `` `"Warn "` ``; the other is that every marker ends with `\r\n`. The trailing space is the direct consequence of fixed width 5, so writing a marker must carry it along: the match is byte for byte, and one space short means the wait never ends; the docstring at the top of the script is there precisely to inoculate against that. The timestamp field, on the other hand, goes through the regex `\[\d+ms\]`, so we deliberately make no literal assertion about millisecond values.

All of the checks run in one command: `cmake --build build --target check_log_renode`. When the command comes back quiet, the banner, both calling styles, the trim, and the timestamps have been checked in one pass.

Surface two belongs to `showAnalyzer`. Diffing `renode_console.resc` against the PTY version, we found the difference is more than one line: the three PTY lines (the `$uart_pty` variable, `CreateUartPtyTerminal`, `connector Connect`) are replaced by `showAnalyzer usart1`, and the header comment becomes the human-readable one.

And what happens once it is open? Log lines carrying host and virtual timestamps land directly in the Renode console, and we do not even need a second terminal. For a quick pass over the output, `cmake --build build --target sim_log_console`.

Watching the PTY by eye works too. Once `sim_log_example` is running, open another terminal on `/tmp/libestdx-log`; we use `screen /tmp/libestdx-log 115200`. To exit, press `Ctrl-A`, then `K`, then `Y`. On the actual development board, `flash_log_example` does the flashing, and the line format is byte-identical to the one in simulation.

## v1 boundaries and custom types

The v1 boundary in four lines; know them before you rely on it:

- Sending is blocking: the UART sink goes through `try_send` with a bounded timeout, best effort; how the timeout is derived from the baud rate is in [the fifth piece, The final verdict sits in the map file](05-map-is-the-verdict.md).
- Thread context only; disabled inside ISRs.
- No floats; wrap raw bit patterns in `Hex` if you need them.
- One sink.

The door for custom types in the log, in one sentence: specialize `Formatter<T>` and write `static format(LineAppender auto&, const T&)`. `LineAppender` is the line buffer's append interface (`format` calls `append` on it), shaped like `std::formatter`. The how and the trade-offs are fully covered in [From overload sets to Formatter specializations](02-dispatch-overload-to-formatter.md).

## Notes and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| `format_string_field_count_mismatch` compile failure | field count does not match argument count | align the counts |
| `format_string_x_requires_integral` | `{:x}` on a non-integer | pass an integer, or drop the `:x` |
| `format_string_unknown_spec` | spec outside the vocabulary (including `{0}` arg-ids) | stick to `{}`, `{:x}`, `{:N}` |
| check-script marker never arrives | the level field's trailing space and `\r\n` are bytes too | write the marker as `` `"Warn "` `` with the space and the CRLF |
| strings finds no marker on a host build | GCC splits literals into movabs immediates | run the final check on the ARM binary |
| Trace still silent after `setLevel(Trace)` | call bodies below the compile-time floor are empty | change `MinLevel` and rebuild |
| a `~` at the end of a line | payload exceeded the 125-byte content budget | shorten the content, or widen the line-buffer template parameter |
| `logger: float rejected`, or a single character rejected | floats and single characters are turned away by design | `Hex` or fixed point; for a single character pass `string_view("(")` |

## References

- [libestdx repository (examples/06_log and the logger module)](https://github.com/Awesome-Embedded-Learning-Studio/libestdx)
- [logger module source directory](https://github.com/Awesome-Embedded-Learning-Studio/libestdx/tree/main/include/libestdx/logger)
- [P2216R3: compile-time checking for std::format](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2216r3.html)
- [Renode documentation](https://renode.readthedocs.io/)
