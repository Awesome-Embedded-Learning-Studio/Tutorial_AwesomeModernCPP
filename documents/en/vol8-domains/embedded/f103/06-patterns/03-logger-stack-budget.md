---
title: "The logger's stack budget, measured: can a function-local LineBuffer blow the stack?"
description: "The stack-local LineBuffer<128> inside emit faces three soul-searching questions: why not hold it at object level? Will it blow the stack? — three measurement lessons (an empty sink gets the whole chain deleted, constant input gets folded at compile time, explicit instantiation hits a compiler ICE), two measurement methods cross-validating a 376-byte transient frame, the property that the frame grows only with nesting depth and never with the number of call sites, and the complete budget chain from the linker script to RTOS thread stacks"
chapter: 6
order: 3
tags:
  - stm32f1
  - intermediate
  - 零开销抽象
  - 嵌入式
  - 实战
  - 内存管理
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 8
prerequisites:
  - "A zero-overhead logger: the complete pitfall log, from hitting the source_location wall to acceptance by disassembly"
related:
  - "From overload sets to Formatter specializations: choosing the dispatch mechanism for an embedded logger"
translation:
  source: documents/vol8-domains/embedded/f103/06-patterns/03-logger-stack-budget.md
  source_hash: ac6a00962711d93252afc444be714422bb40161b2873d9a31853ca05948d3554
  translated_at: '2026-09-27T05:51:40+00:00'
  engine: anthropic
  token_count: 2000
---

# The logger's stack budget, measured: can a function-local LineBuffer blow the stack?

## Introduction: three soul-searching questions for a stack array

The heart of writing `Logger::emit` is a single line — open a line assembler on the stack, assemble it, write it out:

```cpp
LineBuffer<LINE_BYTES> line;
// ...assemble the segments...
Sink::write(line.finish(kTruncateMark, kLineEnd));
```

That one line got three questions fired at it in a row: **why function-local rather than held at object level?** 128 bytes on the stack — **will it blow the stack?** And what if it does? These three questions are exactly the reasonable suspicion an embedded engineer should aim at a "zero-overhead" claim — you say zero overhead, but what about the stack? This article lays out three measurement lessons, two sets of measurements that validate each other, and one complete stack-budget chain. Spoiler for the conclusion up front: function-local is right, the 376 bytes are transient, and going forward there is exactly one scenario that could truly blow the stack — the fixed-size RTOS thread stack.

## Why function-local: three reasons, each harder than the last

**First, "object-level holding" does not exist in this library.** libestdx's vocabulary is all-static interfaces: `Uart`, `Gpio`, `LED`, and `Formatter` are all types without instances, and `Logger` is no exception. Where no `logger` object exists, "the object's members" is not a thing.

**Second, a shared buffer is a reentrancy bomb.** Even taking a step back and making a `static inline` shared line buffer — main loop, callbacks, future RTOS threads, even ISRs — any two contexts logging at the same moment would tear each other's lines apart: A gets halfway through assembling, B shoves the cursor onward, and both lines come out broken. A stack local gives every call its own line buffer — concurrency-safe by nature, without spending a single cent on synchronization code.

**Third, the lifetime lands exactly right.** Under contract B, the one we settled on, `Sink::write` completes synchronously — the buffer lives inside `emit`'s stack frame from the first `append` to the final write-out: no escape, no dangling. The synchronous contract is not just error semantics; it is also a precondition of memory safety.

## Measuring: three lessons in not getting fooled by the compiler

Before any real measuring could happen, we hit three pitfalls first, each worth a record of its own.

**Lesson one: an empty sink makes the whole logging chain evaporate.** The first test version used a sink that did nothing (`write` was an empty function); one look at the disassembly showed the wrapper function down to a single `bx lr` — the entire assembly chain had been eliminated as dead code. That is not a bug: the optimizer correctly discovered that "the log has no observable effect." Lesson: **to measure the logger's overhead, the sink must have an observable side effect** (writing the line length into a `volatile` global, for example).

**Lesson two: constant input gets the whole line folded at compile time.** After making the sink observable, we measured again with a constant clock (`now_ms` returning 42) and constant arguments — and the compiler **finished computing the entire line assembly at compile time**:

```text
00000000 <_Z7call_itv>:
   0:	221a      	movs	r2, #26      @ line length 26 is a compile-time constant!
   2:	4b01      	ldr	r3, [pc, #4]
   4:	701a      	strb	r2, [r3, #0] @ g_sink_probe = 26
   6:	4770      	bx	lr
```

`movs r2, #26` — a single instruction produces the length of the entire line, the stack frame is zero bytes, and the runtime cycles are next to nothing. Another solid piece of evidence for the zero-overhead story: **a logging line fully known at compile time has its assembly folded away entirely**. But as a measurement method this is a disaster: to measure the real stack bill, the clock and the arguments must come from `volatile`s, forcing the optimizer to keep the assembly chain.

**Lesson three: explicit instantiation runs into a compiler ICE.** Trying to force a template member to instantiate so it would show up in the map, arm-none-eabi-gcc 16.2 threw an internal compiler error outright. The workaround: a free-function wrapper marked `__attribute__((noinline))` that calls it; the wrapper body inlines the member's real optimized body, and `objdump`ing the wrapper function is enough.

## The measured bill: 376 bytes, cross-validated by two methods

With every input swapped for a `volatile`, the disassembly gave the answer:

```text
   4:	e92d 47f0 	stmdb	sp!, {r4, r5, r6, r7, r8, r9, sl, lr}   @ 8 registers = 32B
   a:	b0d6      	sub	sp, #344                               @ 344B of frame space
```

Cross-checking again with GCC's `-fstack-usage` (it generates a `.su` file per function, reporting static stack usage):

```text
$ cat emit_stack.su
emit_stack.cpp:40:32:void call_it()	376	static
```

Two independent paths give the same number: **376 bytes** (with `LINE_BYTES=128`; inside the 344, beyond the buffer itself, sit the temporaries from full inlining and the call-ABI scratch — the `sub sp` figure is not the template parameter, and that is the true shape of the cost under full inlining).

The key property of these 376 bytes is **transience**: the frame exists only for the duration of `emit`. It does not add up with the number of logging call sites — one hundred call sites share the property of "at most one frame at any instant"; it adds up only with **nesting depth** — and nested logging is itself an anti-pattern to avoid (above all, never log from inside a sink: that is recursion).

## The budget chain: where the stack comes from, and whether it is enough

The last part of the bill: which budget do these 376 bytes come out of?

**Bare metal: practically unbounded.** This repository's linker script says `_estack = ORIGIN(RAM) + LENGTH(RAM)` — the stack top is pinned to the top of RAM, so the stack space is simply "all remaining RAM" (on a BluePill, about 20KB minus data/bss). Logging from the main loop means one 376B frame at a time — pocket change. A reminder while we are here: many Cube projects' startup files default to `Stack_Size EQU 0x400` (a fixed 1KB); under that configuration 376B is something to weigh carefully — the difference between the two stack models is itself a common blind spot.

**RTOS: the real constraint lives here.** Each thread's stack is fixed-size, and logging depth × LINE_BYTES has to enter the budget of every thread that might log. The knob at that point is turning `LINE_BYTES` from 128 down to 64 (a 61-byte content area, the header's longest form at about 28 bytes, still leaving thirty-plus bytes of payload — enough), plus writing "every active logging call costs `LINE_BYTES` + temporaries of stack" into the documentation.

**The toolbox** (combining static and dynamic analysis is the consensus practice): GCC's built-in [`-fstack-usage` and `-fcallgraph-info=su`](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html) can compute the worst-case stack depth across the whole call graph ([AdaCore's original paper](https://www.adacore.com/readings/gem-120-compile-time-stack-usage-analysis) and [Embedded Artistry's hands-on guide](https://embeddedartistry.com/blog/2020/08/17/three-gcc-flags-for-analyzing-memory-usage/) are good entry points; note that indirect calls, interrupts, and LTO break the static call graph); on the runtime side there are binary-analysis tools like [puncover](https://github.com/HBehrens/puncover) and [the high-water-mark painting method Memfault walks through](https://interrupt.memfault.com/blog/measuring-stack-usage-the-hard-way); FreeRTOS users have `uxTaskGetStackHighWaterMark()` — but remember it reflects only the paths **actually taken**; branches never reached do not count.

## Caveats and common mistakes

| Symptom | Cause | Fix |
|---|---|---|
| Measured stack/cycles come out zero, so perfect it is suspicious | The sink has no observable effect; the whole chain fell to DSE | Give the sink a `volatile` side effect |
| The measurement is a single `movs #26` with no stack frame | Constant input; the whole line folded at compile time | Route the clock and arguments through `volatile` |
| `internal compiler error` | An arm-gcc 16.2 bug with explicit instantiation of class-template members | A `noinline` free-function wrapper |
| The `sub sp` figure is well above LINE_BYTES | With full inlining, temporaries and ABI scratch all land in the same frame | Normal shape; budget against the measured figure |
| A Cube template project has only 1KB of stack | The `Stack_Size EQU 0x400` fixed-size model | Raise it or switch to a full-RAM stack; put the logger's stack bill into the budget |

## Summary

- The function-local line buffer is the correctness play: an all-static vocabulary has no "object level", a shared buffer is a reentrancy bomb, and contract B lands the lifetime exactly right;
- Before measuring overhead, defend against three deceptions: DSE from an empty sink, compile-time folding from constant input, and the ICE from explicit instantiation;
- The measured bill for `LINE_BYTES=128` is a 376-byte transient frame (double-verified by disassembly and `-fstack-usage`), growing only with nesting depth, never with the number of call sites;
- Under the bare-metal full-RAM stack model there is nothing to worry about; the fixed-size RTOS thread stack is the real constraint, `LINE_BYTES` is the knob, and combining static `-fstack-usage` with dynamic high-water marks is the right road to a budget;
- A constant logging line folds completely into `movs #26` — the last puzzle piece of zero overhead.

The code lives in [libestdx/logger/](https://github.com/Charliechen114514/libestdx/tree/main/include/libestdx/logger); `emit`'s implementation was written to follow this article's conclusions.

## References

- [Compile-time Stack Usage Analysis (AdaCore/GCC, the original -fstack-usage introduction)](https://www.adacore.com/readings/gem-120-compile-time-stack-usage-analysis)
- [Three GCC Flags for Analyzing Memory Usage — Embedded Artistry](https://embeddedartistry.com/blog/2020/08/17/three-gcc-flags-for-analyzing-memory-usage/)
- [GCC Developer Options (official -fstack-usage / -fcallgraph-info documentation)](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html)
- [Measuring Stack Usage the Hard Way — Memfault Interrupt](https://interrupt.memfault.com/blog/measuring-stack-usage-the-hard-way)
- [puncover — bytewise stack/code analysis](https://github.com/HBehrens/puncover)
- [the libestdx repository](https://github.com/Charliechen114514/libestdx)
