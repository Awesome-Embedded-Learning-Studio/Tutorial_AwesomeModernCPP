---
chapter: 6
cpp_standard:
- 17
description: 'C++ exceptions follow a table-driven zero-cost model — the normal (no-throw) path runs no extra instructions, while the exception path relies on EH table lookups. Verified by measurement: code that can throw but never does runs as fast as a pure function (0.25 ns, zero-cost confirmed), but throw+catch on every call costs up to 857 ns (3400x). Conclusion: use exceptions only for truly exceptional situations, never as control flow; embedded builds can disable them with -fno-exceptions'
difficulty: advanced
order: 2
platform: host
prerequisites:
- 'Virtual functions and devirtualization: don''t rush to rewrite virtuals as templates'
- Benchmark methodology reference card
reading_time_minutes: 5
related:
- "std::function's small buffer optimization: the cost of type erasure"
tags:
- host
- cpp-modern
- advanced
- 优化
- 内存安全
title: 'The zero-cost model of exceptions: free on the normal path, expensive on the exception path'
translation:
  source: documents/vol6-performance/ch06-cpp-abstraction-cost/06-02-exceptions-zero-cost.md
  source_hash: 45496538f71b5111a30dbb13f60e9c0a12a316e2a28d853694e5f6695b528986
  translated_at: '2026-09-26T06:55:00+00:00'
  engine: anthropic
  token_count: 2300
---
# The zero-cost model of exceptions: free on the normal path, expensive on the exception path

## "Exceptions are slow" is one of the biggest misconceptions

In C++ circles, "exceptions are slow, performance code needs `-fno-exceptions`" is one of the most widespread misconceptions around. It is half right (the exception path really is expensive) and half wrong (the normal path is nearly free). The real model is called **table-driven zero-cost**:

- **Zero-cost (the normal path)**: code paths that never throw carry almost **no extra instructions**. The cost of the exception mechanism does not show up as "one extra check instruction per call" (that is the error-code cost) — it is deferred to **when an exception is actually thrown**.
- **Table-driven (the exception path)**: when an exception is thrown, the runtime consults the **EH table** (Exception Handling table, generated at compile time, recording "how to unwind the stack frame at this address, and whether there is a catch") to walk up the call stack looking for a catch, and unwinds the stack. That table lookup plus stack unwinding is **microsecond-scale**.

Let's measure this model directly.

## Hands-on: the normal path is free, the exception path is 3400x more expensive

Four paths (on this machine, average ns per op):

```text
===== Exception cost =====
  normal path (never throws):                0.250 ns/op
  error code (err via return value):         0.247 ns/op
  can throw but never does (zero-cost):      0.249 ns/op
  throw+catch every time:                    857.3 ns/op  ← the exception path is expensive
```

This table validates both halves of the zero-cost model:

**1. Zero-cost on the normal path, confirmed.** The first three rows are essentially equally fast (0.247-0.250 ns): a "pure function", "error codes (one extra err parameter per call)", and "can throw but never does" show **no measurable difference** in normal-path cost. That is the precise meaning of "zero-cost": **enabling `try`/the exception mechanism adds no cost whatsoever to the normal path**.

**2. The exception path is ~3400x more expensive.** The last row: 857 ns vs 0.25 ns. Throwing and catching one exception involves: allocating the exception object, consulting the EH table, unwinding the stack (destroying local objects one by one), finding the catch, and jumping to it. That whole sequence is microsecond-scale — three orders of magnitude more expensive than a normal return.

Together, these two points directly dictate the **usage discipline** for exceptions:

- **Use exceptions only for truly exceptional situations** (rare, unexpected, paths that genuinely warrant stack unwinding). Never use exceptions in normal control flow as a "special return value" — that path is 3400x more expensive.
- **Don't worry about exception overhead on the normal path.** Enabling `try`/the exception mechanism does not slow your normal code down (zero-cost, confirmed). "`try` blocks are slow" is a misconception: `try` itself generates no runtime instructions; what is slow is actually throwing.

## The trade-off against error codes

Exceptions vs error codes is not a question of "which is faster" — it is "which error distribution suits which mechanism":

| Model | Normal path | Error path | Fits |
|---|---|---|---|
| **Error codes** | Slightly more expensive (check err every call) | Same as normal | Errors are **common** (the per-call check is not wasted)|
| **Exceptions** | Free (zero-cost) | Very expensive (microsecond-scale) | Errors are **rare** (the normal path stays unpolluted)|

Corollary: **the rarer the error, the better exceptions pay off**. If an API's "errors" are actually the norm (say `parse` routinely hits invalid input), error codes are the better fit; if errors are genuinely exceptional (say `vector::at` out-of-bounds, allocation failure, a network drop), exceptions are better — they keep the normal-path code clean (no `if (err)` scattered everywhere), and when a real exception does happen, that bit of overhead does not matter.

The C++ standard library embodies exactly this trade-off: `vector::operator[]` does not check bounds (fast), while `vector::at` checks and throws (the normal path still costs nothing — out-of-bounds is a true exception).

## `-fno-exceptions`: when to turn exceptions off

A few contexts disable exceptions wholesale with `-fno-exceptions`:

- **Embedded / games / real-time**: determinism requirements are strict and stack-unwinding time is unbounded (microsecond-scale jitter); or binary size is tightly budgeted (EH tables take up space).
- **Squeezing out every last byte**: EH tables and unwind information occupy a fair amount of space, and turning exceptions off saves it (along with some code-generation constraints, as a bonus).
- The cost: **losing RAII-based error propagation**. Exceptions are C++'s "leaves-nothing-out" mechanism for propagating errors across functions (guaranteed by the destructor chain); with them off, you have to hand-write error-code plumbing that is easy to get wrong. Some container behavior (`vector` and friends) degrades too (for instance, `at` becomes `abort`).

So `-fno-exceptions` is a **deliberate engineering trade-off**, not a "for speed" silver bullet. Ordinary C++ code (backends, desktop apps, most libraries) should **keep exceptions on**: they keep the normal path clean, and that path is zero-cost anyway.

## The implementation: Itanium C++ ABI EH

Under the hood, exception handling follows the **Itanium C++ ABI EH specification** (common to x86-64 Linux and macOS): `throw` goes through `__cxa_throw`, `catch` goes through a personality function consulting `.gcc_except_table`, and stack unwinding uses `_Unwind_RaiseException`. The mechanism is "table-driven": at compile time an EH table is generated for every region of code that can throw, and at runtime the table is consulted **only when an exception is actually thrown**. The depth (two-phase exception handling, handler search) is beyond vol6's scope — knowing the architectural fact of "table-driven zero-cost" is enough to guide decisions.

One sentence to wrap it up: exceptions are a table-driven zero-cost model — the normal path runs no extra instructions (measured 0.25 ns, same as a pure function), while the exception path pays for the EH table lookup plus stack unwinding (measured 857 ns, 3400x more expensive); "try is slow" is a misconception — what is slow is actually throwing, and the `try` block itself is zero-cost; the rarer the error, the better exceptions pay off, and when errors are the norm, use error codes; `-fno-exceptions` is a trade-off for embedded/size/determinism, not a performance silver bullet, and its price is losing RAII error propagation. The **depth of the mechanism** (Itanium ABI EH, two-phase handling) is beyond vol6 — knowing the zero-cost model is enough.

## References

- Itanium C++ ABI *Exception Handling* (itanium-cxx-abi.github.io/cxx-abi-eh.html) — the specification for EH tables, personality functions, and stack unwinding
- CppCoreGuidelines *Errors and Exception Handling* (Stroustrup & Sutter) — discipline for using exceptions
- Agner Fog, *Optimizing software in C++*, the exceptions section (local copy)
- This article's measurement code: `code/volumn_codes/vol6-performance/ch06/exception_cost.cpp`
