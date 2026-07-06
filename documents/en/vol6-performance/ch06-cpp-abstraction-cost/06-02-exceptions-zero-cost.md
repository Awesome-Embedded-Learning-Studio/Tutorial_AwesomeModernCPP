---
chapter: 6
cpp_standard:
- 17
description: 'C++ exceptions are a table-driven zero-cost model — the normal path (no-throw) has no extra instructions, the exception path relies on EH table lookup. Verified by measurement: with the ability to throw but never throwing is just as fast as a pure function (0.25 ns, zero cost nailed down); but throwing plus catching every time reaches 857 ns (3400x). Conclusion: use exceptions only for genuinely exceptional cases, not for control flow; embedded can use -fno-exceptions'
difficulty: advanced
order: 2
platform: host
prerequisites:
- Virtual functions and devirtualization
- Benchmark methodology reference card
reading_time_minutes: 5
related:
- std::function's small buffer optimization
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
  translated_at: '2026-07-06T00:00:00+00:00'
  engine: manual
  token_count: 3400
---
# The zero-cost model of exceptions: free on the normal path, expensive on the exception path

## "Exceptions are slow" is one of the biggest misconceptions

In C++ circles, "exceptions are slow, performance code should `-fno-exceptions`" is one of the most widely-spread misconceptions. It's partly right (the exception path really is expensive) and partly wrong (the normal path is nearly free). The real model is called **table-driven zero-cost**:

- **Zero-cost (normal path)**: a code path that doesn't throw has **almost no extra instructions**. The cost of the exception mechanism doesn't show up as "one extra check instruction per call" (that's the cost of error codes); it's deferred to **when an exception is actually thrown**.
- **Table-driven (exception path)**: when an exception is thrown, the runtime consults the **EH table** (Exception Handling table, generated at compile time, recording "how to unwind this address's stack frame, is there a catch") to walk up the call stack looking for a catch and unwind the stack. This table lookup plus stack unwind is **microsecond-level**.

Let's measure this model directly.

## Run it: normal is free, exceptions are 3400x more expensive

Four paths (this machine, average ns per op):

```text
===== Exception cost =====
  normal path (never throws):            0.250 ns/op
  error code (return value carries err):        0.247 ns/op
  has throw ability but never throws (zero cost): 0.249 ns/op
  throw+catch every time:           857.3 ns/op  ← the exception path is expensive
```

This table verifies both halves of the zero-cost model:

**1. The normal path's zero cost is nailed down**. The first three rows are nearly identical in speed (0.247-0.250 ns): "pure function", "error code (carrying one more err parameter per call)", "has throw ability but never throws" have **no measurable difference** in normal-path latency. That's the precise meaning of "zero cost": **turning on `try`/the exception mechanism adds no cost to the normal path**.

**2. The exception path is about 3400x more expensive**. The last row is 857 ns vs 0.25 ns. Throwing plus catching one exception has to: allocate the exception object, consult the EH table, walk up the stack (destructing locals one by one), find the catch, jump. That whole sequence is microsecond-level, three orders of magnitude more expensive than a normal return.

These two together directly drive the **usage discipline** around exceptions:

- **Use exceptions only for genuinely exceptional cases** (rare, unexpected, needs stack unwinding). Never use exceptions as a "special return value" on the normal control-flow path: that path is 3400x more expensive.
- **Don't worry about exception overhead on the normal path**. Turning on `try`/the exception mechanism doesn't slow your normal code down (zero cost nailed down). "`try` blocks are slow" is a misunderstanding: `try` itself produces no runtime instruction; what's slow is "actually throwing".

## Weighing exceptions against error codes

Exceptions vs error codes isn't "which is faster", it's "which error distribution fits which":

| Model | Normal path | Error path | Fits |
|---|---|---|---|
| **Error code** | Slightly more expensive (checks err every time) | Same as normal | Errors are **common** (checking every time isn't wasted)|
| **Exception** | Free (zero cost) | Very expensive (microsecond-level) | Errors are **rare** (the normal path isn't polluted)|

Corollary: **the rarer the error, the better exceptions pay off**. If an API's "error" is actually the norm (say `parse` frequently hits invalid input), error codes are better; if the error is a genuine exception (say `vector::at` out-of-range, memory allocation failure, network interruption), exceptions are better: they keep the normal-path code clean (no `if (err)` everywhere), and when a real exception happens that cost doesn't matter.

The C++ standard library makes exactly this trade: `vector::operator[]` doesn't bounds-check (fast); `vector::at` checks and throws (the normal path is still zero-cost; out-of-range is a genuine exception).

## `-fno-exceptions`: when to turn it off

Some scenarios `-fno-exceptions` the whole thing off:

- **Embedded / games / real-time**: deterministic requirements are high, stack-unwind time is uncontrollable (microsecond-level jitter); or binary size is constrained (the EH table takes space).
- **Extreme size**: the EH table and unwind info take non-trivial size, turning them off saves bytes (and also lifts some code-gen constraints).
- The cost: **losing RAII error propagation**. Exceptions are C++'s "no-omissions" mechanism for propagating errors across functions (the destructor chain guarantees it); turn them off and you have to hand-write error-code pass-through, which is easy to get wrong. Some container (`vector` etc.) behaviors also degrade (for example `at` becomes `abort`).

So `-fno-exceptions` is **a tradeoff engineering decision**, not a "for speed" silver bullet. Normal C++ code (backends, desktop, most libraries) should **keep exceptions**, because they keep the normal path clean and the normal path is zero-cost.

## Implementation: the Itanium C++ ABI EH

The underlying implementation of exceptions follows **the Itanium C++ ABI EH spec** (x86-64 Linux plus macOS, in general): `throw` uses `__cxa_throw`, `catch` uses a personality function to look up `.gcc_except_table`, stack unwinding uses `_Unwind_RaiseException`. This mechanism is "table-driven": at compile time it generates an EH table for every section that can throw, and at runtime **only consults it when an exception is thrown**. The depth (two-phase exception handling, handler search) is beyond vol6's scope; knowing the architectural fact "table-driven zero-cost" is enough to guide decisions.

In one sentence: exceptions are a table-driven zero-cost model, the normal path has no extra instructions (measured 0.25 ns, same as a pure function), the exception path relies on EH table lookup plus stack unwinding (measured 857 ns, 3400x more expensive); "try is slow" is a misunderstanding, what's slow is "actually throwing", and the `try` block itself is zero-cost; the rarer the error the better exceptions pay off, and when errors are the norm use error codes; `-fno-exceptions` is an embedded/size/determinism tradeoff, not a performance silver bullet, and the cost is losing RAII error propagation. The **mechanism depth** of exceptions (Itanium ABI EH, two-phase handling) is beyond vol6; knowing the zero-cost model is enough.

## References

- Itanium C++ ABI *Exception Handling* (itanium-cxx-abi.github.io/cxx-abi-eh.html) — the spec for EH tables, personality functions, and stack unwinding
- CppCoreGuidelines *Errors and Exception Handling* (Stroustrup & Sutter) — exception-usage discipline
- Agner Fog, *Optimizing software in C++*, exceptions section (local)
- The measurement code for this article: `code/volumn_codes/vol6-performance/ch06/exception_cost.cpp`
