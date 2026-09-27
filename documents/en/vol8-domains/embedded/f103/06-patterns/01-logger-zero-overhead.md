---
title: "A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly"
description: "The uncut record of the first half of designing a cross-MCU logger for libestdx: how the three design decisions were made, the double crash of source_location against parameter packs, why compile-time trimming leans on a flag chain as its safety net, the two iron rules of overload-set dispatch, the three hidden pitfalls of to_chars, and the M3 disassembly face-off of branch-per-byte vs clamp-once — every claim backed by compiler output or assembly"
chapter: 6
order: 1
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - concepts
  - 模板
  - 嵌入式
  - 实战
difficulty: intermediate
platform: stm32f1
cpp_standard: [17, 20, 23]
reading_time_minutes: 25
prerequisites:
  - "Getting Started: Why C++, and by what right?"
  - "LED: bare registers under the floor tiles, modern C++ above the HAL"
related:
  - "UART: interrupt-driven, ring buffer, expected"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/01-logger-zero-overhead.md
  source_hash: 4fca6d3ba4915652887e3bcd2b1d249a757492d61ba44366366fc9face4027c1
  translated_at: '2026-09-27T05:54:21+00:00'
  engine: anthropic
  token_count: 4300
---

# A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly

## Introduction: why embedded work deserves a hand-written logger

Nobody writes their own logging on the host — install `spdlog` and you already have everything. But on a board like the BluePill with 64KB flash / 20KB RAM, the ledger for the mainstream options reads badly: the `printf` family throws away variadic type safety, and float and locale, if you are not careful, drag several KB into the firmware; `std::format` is elegant, granted, but GCC's implementation pulls in volume on the order of tens of KB — on 64KB of on-chip flash that means paying for every inch of ground in blood; the logging macros in each vendor's SDK are light enough, but their metadata leans on `__FILE__`/`__LINE__`, which grinds against the position that modern C++ does not reach for macros at every turn.

So in [libestdx](https://github.com/Charliechen114514/libestdx) we decided to write our own. The goals were plain: concatenation-style formatting, macro-free metadata, compile-time level trimming, and every "zero overhead" claim verifiable by disassembly or the map file. This article is the uncut record of the first half of that journey (the vocabulary layer + the line-assembly layer) — not "let me teach you to write it this way", but "here are the walls we hit while writing it this way, and how we used compiler output to tear them down". Every pitfall comes with a real error message or real assembly, and you can reproduce each one with the same commands.

First, the three most important design decisions, laid on the table up front — they determine what every pitfall below looks like:

| Dimension | Decision | One-line reason |
|---|---|---|
| Formatting | Concatenation + `std::to_chars` | The flash-cheapest legitimate route: no locale, no heap, no variadics |
| Metadata | `std::source_location`, no macros | With a standard facility in C++20 there is no excuse for macros |
| Trimming | Level threshold as a template parameter, short-circuited by `if constexpr` | Logs below the threshold **never make it into the firmware, not even as strings** |

The toolchain is arm-none-eabi-gcc 16.2; the library itself is C++23, no exceptions, no RTTI, no heap. Now let's start hitting walls.

## Pitfall 1: source_location does not fit into a parameter pack

The call shape we wanted looks like this — a `tag` followed by any number of formattable arguments, with the call site's file and line captured automatically:

```cpp
Log::info("led", "count=", n);   // file:line attached automatically inside
```

First instinct: make `source_location` the **first parameter**, with a default, and let the parameter pack follow:

```cpp
template <typename... Parts>
static void info(const std::source_location& loc = std::source_location::current(),
                 std::string_view tag = {}, Parts&&... parts);
```

The real error (not one character changed):

```text
error: no matching function for call to 'Log<...>::info(const char [4], const char [7], int)'
note: template argument deduction/substitution failed:
note: cannot convert '"led"' (type 'const char [4]') to type 'const std::source_location&'
```

The cause: in a positional call the first argument, `"led"`, goes to bind `loc` — a default argument only covers the "you didn't pass one" case; it is powerless against "you passed one, but in the wrong slot".

Second instinct: then change the tag type to a custom `LogTag` struct that converts from `string_view`? Into the same wall:

```text
note: cannot convert '"led"' (type 'const char [4]') to type 'LogTag'
```

This time the cause hides deeper: the chain `const char[4]` → `std::string_view` → `LogTag` carries **two user-defined conversions** (`string_view`'s converting constructor + `LogTag`'s converting constructor), and a single implicit conversion sequence allows only one. This is not a library problem, it is a language rule. In the community, [cor3ntin's classic article](https://cor3ntin.github.io/2020/06/18/nonterminal/) lays out the whole problem domain clearly: **a `source_location` with a default argument and a non-terminal parameter pack are incompatible by birth**, and [cppstories has a dedicated piece](https://www.cppstories.com/2021/non-terminal-variadic/) on the various ways around it.

The way out is to let the tag type absorb the location itself — a **constrained template constructor**, one conversion, straight in:

```cpp
struct Tag {
    std::string_view text;
    std::source_location location;

    template <TextSource S>
    Tag(S&& s, const std::source_location& l = std::source_location::current())
        : text(std::forward<S>(s)), location(l) {}
};

// Logger member:
template <typename... Parts>
static void info(Tag tag, Parts&&... parts);
```

The principle: `"led"` to `Tag` is now **one** user-defined conversion (the constrained template constructor), and it happens at the log call site — so the `current()` in the constructor's default argument captures exactly the location of the line that writes `Log::info("led", ...)`. One line of measured proof settles it:

```text
$ ./tag_smoke
file=smoke_tag.cpp line=9 all OK      # line 9 is exactly where Tag a("lit"); sits
```

::: tip Pitfall alert
Don't waste a second hour on this road: no arrangement that lines a `source_location` default argument up with a parameter pack survives compilation. Either macro injection (which we refuse), or what we did above — let one of the parameter types carry the location in when constructed at the call site. This is the most valuable shortcut in the whole article.
:::

## Pitfall 2: compile-time trimming is not a language guarantee, it is the collective credit of the flag chain

"Logs below the threshold cost nothing" — everyone says it, but most people have never verified how far the trimming actually goes. We opened up the `if constexpr` short-circuit and looked inside: what it trims away is only the **function body**; the string literal at the call site and the `source_location` object are still evaluated, their addresses still referenced. Whether they ever vanish from the firmware comes down to this chain:

```text
-O3 inlining → dead code elimination → -ffunction-sections -fdata-sections → -Wl,--gc-sections
```

One link in this chain is remarkably easy to miss: many projects (libestdx at the time included) configure only the linker's `--gc-sections` and never the compile-time `-ffunction-sections -fdata-sections` — without the latter, gc-sections can only reclaim at **object-file** granularity, and string-level removal never gets its turn. After completing the flag set we ran one hard-nosed verification: plant a unique marker string in a log call at a trimmed level —

```cpp
Log<LogLevel::Error>::info("hidden", "UNIQUE_MARKER_MUST_VANISH_7f3a", 7);
```

Compile with `-O3 -ffunction-sections -fdata-sections -Wl,--gc-sections` and inspect the artifact:

```text
$ strings a.out | grep UNIQUE_MARKER
DCE: GOOD - string stripped          # zero residue of the marker string
```

Meanwhile the file-name strings on enabled paths stay put — what belongs is in, what doesn't is out, and that is the complete meaning of "zero overhead". While you are at it, also set `-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.` — otherwise `source_location` bakes the build machine's absolute paths into the firmware, and every log site pays flash for it.

::: tip Pitfall alert
Before touching build flags, **write down each firmware's `arm-none-eabi-size` baseline first**, then change, then compare. Expect flat or down; if some firmware grew, what you want is that data, not a shrug of "should be fine".
:::

## Pitfall 3: dispatch via an overload set, and the two iron rules we crashed into for real

The line assembler `LineBuffer` needs to dispatch on the argument's type: strings copy straight in, integers go through `to_chars`, the `Hex` wrapper goes hexadecimal, and custom types go through the `append_to` extension point. Our first version was one long `if constexpr` chain — it ran, but it read like a tangle of plumbing. After switching to an overload set (the whole `operator<<` family shape), we hit two pitfalls within 30 seconds, which turned out to be exactly two iron rules.

**Iron rule 1: every template overload must take its parameter the same way (by value, across the board).** The first version's deleted fallback used `const T&`, and sitting next to the by-value integer overload, `append(42)` went straight to ambiguity:

```text
error: call of overloaded 'append(int)' is ambiguous
  • candidate 1: 'void LineBuffer<N>::append(T) [with T = int]'
  • candidate 2: 'void LineBuffer<N>::append(const T&) [with T = int]' (deleted)
```

With different parameter forms, partial ordering cannot rank the candidates, and "the more constrained one wins" cannot rescue the scene either. Unify on by-value, the parameter forms tie, and constraints take over the verdict.

**Iron rule 2: no `*this` inside a constraint expression.** To say "this type can `append_to` my buffer", the instinctive spelling is:

```cpp
template <typename T>
    requires requires(const T& t) { t.append_to(*this); }   // ✗
```

The real error:

```text
error: invalid use of 'this' at top level [-Wtemplate-body]
```

Constraints are evaluated in a context where `this` is unavailable; you need a stand-in, `std::declval<LineBuffer&>()`. With both iron rules nailed down, the overload set looks like this (excerpt):

```cpp
void append(bool b);                       // non-template: the type is fixed; don't templatize what never varies
void append(std::string_view view);

template <TextSource T>                    // literals / const char* / std::string
void append(T s) { append(std::string_view{s}); }

template <std::integral T>                 // char/bool already intercepted by the non-template overloads
void append(T v) { write_int(v, 10); }

template <typename T>                      // extension point for custom types
    requires requires { std::declval<const T&>().append_to(std::declval<LineBuffer&>()); }
void append(T t) { t.append_to(*this); }

template <std::floating_point T>
void append(T) = delete;   // logger: float rejected (FP tables cost flash)

template <typename T>
void append(T) = delete;   // logger: no formatter for T; provide append_to or Hex
```

A `= delete` with a comment is **more useful** than a `static_assert` rejection: the two refusals each get their own line and speak their own reason, and the compile error points straight at the matching line — the `float` error points at the float line, a type without `append_to` points at the fallback line, and the reader knows what to fix from the error message alone.

One more "should we templatize all the way down" question deserves its own paragraph: keeping `append(char)`/`append(bool)` **non-template** is not laziness. Templates are for occasions where "the type varies"; these two types are set in stone, and a plain function is the simplest correct solution. Besides, "non-template beats template" is the hardest tie-break rule in overload resolution — zero reasoning cost. Flip it around: write them as `template<std::same_as<char> T>` and they stand in no subsumption relation with the `integral` template, so `append('x')` goes ambiguous on the spot — we ran the experiment once more before daring to put that sentence in a comment.

## The three hidden pitfalls of to_chars

`std::to_chars` is the legitimate route for embedded formatting, but it has three pitfalls, each sneakier than the last:

**Pitfall 1: `to_chars` has a `char` overload, and it treats a character as an integer.** `to_chars('x')` outputs `"120"` — the character's code point as a number. If the `append(char)` overload is simply deleted, a single-character argument falls silently into the integer overload and **the output quietly goes wrong** — a failure mode an order of magnitude worse than a compile error. So the correct way to act on "we almost never use single characters" is not to drop the overload but to refuse it surgically:

```cpp
void append(char) = delete;   // want a single character? pass string_view("(")
```

We measured it: `to_chars(buf, buf + 8, 'x')` really does output `"120"`. That is not a guess.

**Pitfall 2: `to_chars` has no `bool` overload.** Left unblocked, the error surfaces at the call site inside our own implementation, wearing a blurry face. The non-template `append(bool)` overload prints `"true"`/`"false"` directly — fast, and easy to read.

**Pitfall 3: floating point refused, across the board.** Floating-point `to_chars` drags libstdc++'s floating-point lookup tables into flash, and the damage to resources is utterly out of proportion to the convenience it buys. For "decimals" in embedded logs, fixed-point or the raw bit pattern via `Hex` is the more honest choice. This one too is nailed down with `= delete` plus a reason in a comment.

## Closing the performance loop: a branch per byte vs clamping once

The copy loop in line assembly, as we first wrote it, was the most naive kind — every byte goes through `put()` once, and `put()` decides whether the window is full:

```cpp
for (char c : view) {          // naive version: one branch per byte
    put(c);
}
```

After the author roasted it to our faces as "performance blow-up central", we let the M3 disassembly do the talking. Under `-O3 -mcpu=cortex-m3`, the naive version's hot loop costs about 8 instructions per byte, and the `pos_` cursor **makes a register-to-memory round trip on every single byte** (the branch keeps it from residing in a register across iterations):

```text
1e: ldrb r2, [r2]          @ load byte
20: cmp  lr, r1            @ end-of-loop check
22: str.w ip, [r0, #128]   @ pos_ written back to memory — once per byte!
26: strb r2, [r0, r3]      @ store byte
2a: mov r3, ip
2c: mov r2, r1
2e: cmp r3, #124           @ window check — another one
...
```

After the switch to "clamp once, copy the whole span branch-free", GCC recognized the copy pattern and cut it into 4-byte block copies:

```text
48: ldr.w ip, [r3], #4     @ 4-byte load
4e: str.w ip, [r2], #4     @ 4-byte store
52: bne.n 48               # 3 instructions per 4 bytes, at most 3 bytes of tail cleanup
```

The ledger: ~8 instructions per byte down to ~0.75, roughly a **9x** difference; a 100-character copy drops from ~800 instructions to ~90. Here an honest system-level quantification is due: at 115200 baud, each byte takes about 87µs on the wire, so 100 characters take about 8.7ms — millisecond-scale line speed completely covers up a microsecond-scale copy optimization. Then why did it still have to change? First, this library flies the "zero overhead" flag, so the code has to look like zero overhead — the disassembly is going into the article as an exhibit. Second, under a `-O0` debug build the naive version is a real function call per byte, and the felt difference while single-stepping is enormous. Third, for future sinks like RTT or DMA that enjoy no line-speed cover, this idiom is the foundation.

The core of the clamped version is computing "how much room is left in the window" once, instead of asking once per byte:

```cpp
void append(std::string_view view) {
    const std::size_t room = content_cap() - pos_;
    const std::size_t take = view.size() < room ? view.size() : room;
    char* dst = buf_.data() + pos_;
    const char* src = view.data();
    for (std::size_t i = 0; i < take; ++i) {
        dst[i] = src[i];                 // branch-free; GCC slices it into block copies by itself
    }
    pos_ += take;
    if (take < view.size()) {
        truncated_ = true;               // the truncation flag is set here and only here
    }
}
```

## Constraints must mirror the actual expressions in the function body

One last point, small but deep. The `TextSource` concept ("this thing can be used as log text") is consumed in two places: `Tag`'s constructor and `LineBuffer`'s text-dispatch overload. The actual expressions in the two function bodies differ — in `Tag` it is `std::forward<S>(s)` (expression type `S&&`), in `append` it is `std::string_view{s}` (`s` is a by-value parameter, an **lvalue** inside the body). If the concept were written `convertible_to<S&&, string_view>`, the `append` side could develop a gap where "the constraint lets it through, but the body fails to compile".

Our call: **one concept per library, intersection semantics** — `TextSource = convertible_to<const S&, string_view>`, meaning "converts even as an lvalue". It mirrors both function bodies exactly; the only things left outside the gate are pathological types that offer only `&&`-qualified conversions — types that should be going through the `append_to` extension point anyway. The lesson, distilled into one sentence: **the constraint follows the actual expressions in the function body, and reusing one concept is legitimate only when the two consumers' expression semantics agree** — look at the function bodies first, settle the concept second, and no second variant will ever grow.

## Cautions and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| `cannot convert '"tag"' to 'const std::source_location&'` | Default-argument first parameter + parameter pack | `Tag`'s constrained template constructor absorbs the location |
| `cannot convert 'const char[N]' to 'LogTag'` | Two user-defined conversions on the chain | Same as above — one conversion, straight in |
| Strings from trimmed-level logs still in `.rodata` | Missing `-ffunction-sections -fdata-sections` | Complete the flag chain, re-check the map file |
| `invalid use of 'this' at top level` | `*this` written inside the constraint | `std::declval<LineBuffer&>()` |
| `call of overloaded 'append(int)' is ambiguous` | Mixed parameter forms among template overloads | Unify on by-value (iron rule 1) |
| `append('x')` outputs `120` | `char` fell into the integer overload | A surgical `= delete` refusing single characters |
| Log file names are absolute paths | `-ffile-prefix-map` not configured | Add the map option; paths become relative |
| arm-gcc 16.2 ICEs outright on explicit instantiation | Toolchain bug | Wrap the function in `__attribute__((noinline))` to inspect the code |

## Wrap-up

- `source_location` and parameter packs are born incompatible; `Tag`'s converting constructor is the macro-free right answer, and one user-defined conversion is the language's red line;
- "Zero overhead" must be verified down to **zero string residue**; `-O3` + the two sections flags + `--gc-sections`, none of the three expendable — and record the size baseline before touching any flag;
- The dispatch overload set's two iron rules: template parameters always by value, and `declval`, not `this`, inside constraints;
- Targeted `= delete` refusals with reasons attached yield better error quality than `static_assert`, one line of cause per refusal;
- The three `to_chars` pitfalls: `char` treated as an integer is a silent error, `bool` has no overload, and floating point drags in lookup tables, so it gets refused outright;
- Clamp the copy loop once, then move the whole span: a ~9x instruction difference on the M3, and disassembly is the only acceptance test worth trusting;
- One concept can be shared only on the premise of "constraints mirror the function body" — two consumers must agree on expression semantics before they share a concept.

The module is still being landed in libestdx piece by piece (this article covers the vocabulary layer and the line-assembly layer; the logger core, sink adaptation, and the full Renode end-to-end verification come in later installments). The code lives at [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger) — feel free to drop by the construction site.

## References

- [std::source_location - cppreference](https://en.cppreference.com/w/cpp/utility/source_location)
- [Non-terminal variadic template parameters — Corentin Jabot](https://cor3ntin.github.io/2020/06/18/nonterminal/)
- [Non-Terminal Variadic Parameters and Default Values - C++ Stories](https://www.cppstories.com/2021/non-terminal-variadic/)
- [std::to_chars - cppreference](https://en.cppreference.com/w/cpp/utility/to_chars)
- [the libestdx repository](https://github.com/Charliechen114514/libestdx)
