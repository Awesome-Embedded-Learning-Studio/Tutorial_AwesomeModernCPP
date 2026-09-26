---
chapter: 0
cpp_standard:
- 11
description: Picking up from ch00-01's "correct first, then fast", this article unpacks why a performance number carrying UB cannot be trusted, explains why sanitizers (ASan/UBSan/MSan/TSan) sit in ch00 of the performance volume as its foundation rather than being filed under debugging tools, and hands the narrative off to the three sanitizer articles and ch01.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Performance Mindset: efficiency is not performance'
reading_time_minutes: 7
related:
- 'The ASan tool family and memory safety: shadow memory, Heartbleed, and sanitizer selection'
- Why microbenchmarks lie
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存安全
title: 'From "correct first" to "then fast": why sanitizers are the foundation of the performance volume'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/02-from-correctness-to-performance.md
  source_hash: ba636b7fb40e12877c77571daaca869b512a3c465d4a810f6b30a5244b78c65d
  translated_at: '2026-09-26T05:22:59+00:00'
  engine: anthropic
  token_count: 5800
---
# From "correct first" to "then fast": why sanitizers are the foundation of the performance volume

## The line the previous article left behind

When ch00-01 laid down iron rule #1, "correct first, then fast", we dropped a very heavy line: **talking about performance numbers in the presence of undefined behavior (UB) is like putting up a building on a site where the foundation hasn't been properly laid.** This article takes that line apart so you can see exactly how UB turns a performance number into a lie. Once you've seen that, one thing that may have puzzled you becomes clear: why a volume on performance optimization opens with an entire sanitizer toolchain in its first chapter, instead of cache or SIMD.

## How UB turns a performance number into a lie

The C++ standard marks a class of behaviors as "undefined": signed integer overflow, out-of-bounds access, dereferencing a null pointer, reading an uninitialized variable, data races… For programs that do these, the standard **guarantees nothing** about their behavior: **anything can happen**, including the outcome you want least—"seems to run fine, every result wrong".

The frightening part is that compilers **actively exploit** this rule. Under `-O2`, the compiler is entitled to assume "this code won't trigger UB" and then optimize aggressively on that assumption. The standard permits it, and modern compilers do it every day. For performance measurement, the fallout falls into three main categories, and each one alone is enough to void your numbers:

**Category one: the very code you meant to measure gets deleted.** Your benchmark computes a result nobody consumes, the compiler declares it dead code and eliminates it outright (DCE), and you happily clock 0.3 nanoseconds—of nothing. This is why `volatile global_sink` had to exist in that ch00-01 benchmark.

**Category two: the compiler passes judgment on your loop.** A signed loop variable that might overflow is UB; the compiler may assume it never overflows, then derive the loop's upper bound, hoist the entire loop into a constant, or simply fold it away. You thought you measured N iterations; in reality not one ran.

**Category three, the most insidious one: you think you're measuring A, but you're actually stepping on B's memory.** Out-of-bounds writes, use-after-free, uninitialized reads—none of these crash the program; they just let your benchmark read and write the memory of "some other variable". You measure "this code takes 50 nanoseconds", when in fact half of those 50 nanoseconds went to corrupting the data structure next door. The number is meaningless, and the program is still "running normally".

Here is a minimal, classic example to make it concrete. The function below checks whether `x+1` is greater than `x`:

```cpp
bool always_bigger(int x) { return (x + 1) > x; }   // x+1 overflow = UB
```

Under `-O2`, gcc—reasoning from "signed overflow can never happen"—folds the entire function into `return true;`. The assembly is a single line, `movl $1, %eax; ret`, which couldn't care less about `x`. So even if you pass `INT_MAX` (the one value where `x+1` really does overflow), it returns `true` without blushing. This is documented behavior of gcc's `-fstrict-overflow` (on by default at `-O2`), and cppreference's undefined-behavior page uses it as the standard counterexample.

Talk is cheap, so I actually ran it on my own machine (GCC 16.1.1), feeding the function the same value, `INT_MAX`:

```text
# -O2 (UB assumptions allowed: the whole thing folds into return true)
$ g++ -O2 ub_fold.cpp -o ub_fold && ./ub_fold 2147483647
f(INT_MAX) = 1

# -O2 -fwrapv (force signed overflow to wrap as defined behavior: honest arithmetic)
$ g++ -O2 -fwrapv ub_fold.cpp -o ub_fold_wrap && ./ub_fold_wrap 2147483647
f(INT_MAX) = 0      # INT_MAX+1 wraps to INT_MIN, and INT_MIN > INT_MAX is false
```

Same optimization level (`-O2`), same input (`INT_MAX`); the only difference is whether signed overflow counts as UB or is defined to wrap—and the results are `1` versus `0`. This is UB at its most lethal in performance measurement: **the thing you're measuring is itself unstable; one compiler flag changes, and you're no longer measuring the same thing.** A related trap, while we're here: you might reach for `-O0` as the control (hoping it dutifully does the addition and wraps out a `0`), but contemporary GCC's middle end recognizes the `(x+1)>x` idiom even at `-O0` and folds it anyway—so build this control with `-fwrapv` instead; it's cleaner than switching optimization levels.

So when a UB-carrying benchmark measures "30% faster" at `-O2`, odds are that's just the 30% saved by "the compiler deleted half of your loop based on a UB assumption". In production (different compiler flags, different input distributions) that 30% evaporates on the spot, or even flips into "slower". Taking that fake number into an architecture decision is building on sand.

## So sanitizers aren't "debugging tools", they are the credibility foundation of performance measurement

With that layer cleared up, you can see why this volume stations sanitizers in ch00 instead of the usual drive-by mention under "debugging tips" like elsewhere. The four siblings each police one class of UB that voids performance numbers:

- **ASan** (AddressSanitizer) polices memory errors—out-of-bounds, use-after-free, double free. These are exactly the culprits behind the "category three" sneak above: they let your benchmark quietly step on someone else's memory.
- **UBSan** (UndefinedBehaviorSanitizer) polices language-level UB—signed overflow, null-pointer dereference, mis-typed casts, illegal shifts. These are exactly the culprits behind "category one" and "category two": they give the compiler license to rewrite the code you're measuring into an empty shell.
- **MSan** (MemorySanitizer) polices uninitialized reads—what you read is "a random value", so what you're really measuring is a random-number generator.
- **TSan** (ThreadSanitizer) polices concurrent data races—and a data race is itself UB. This one is especially deadly in concurrency performance measurement: if your multi-threaded benchmark hasn't passed TSan, that pretty throughput figure means nothing. vol5 already covered TSan's mechanism in depth; here we take exactly one angle, "why it is a performance foundation": the credibility of a concurrency performance number presupposes the absence of data races.

Put the four together and the conclusion is rock hard: **for a performance number that hasn't passed sanitizers, you don't know what it measured.** If the precondition doesn't even hold, precision is beside the point. Hence this volume's rule: any code entering a performance comparison first runs clean under sanitizers—only then do we talk numbers.

## The three sanitizer articles in this chapter

How exactly to enable them, how to read their reports, how to coexist with `-O2`, the trade-offs between debug builds and shipping to production—that's the job of the three sanitizer articles; this one is just the signpost at the crossroads, pointing at what each of them covers:

- **"The ASan tool family and memory safety"**: starts from Heartbleed, a real-world disaster, takes the shadow-memory mechanism apart, runs live tests of out-of-bounds, UAF, and global overflow, and sorts out the five siblings ASan / LSan / MSan / TSan / UBSan—who does what, and why most of them are mutually exclusive (you can't enable them together).
- **"Valgrind vs ASan"**: sets Valgrind's dynamic-binary-translation route beside ASan's compile-time-instrumentation route, and explains the essential differences between the two paths—in performance, in the errors they can catch, and in the barrier to use.
- **"The sanitizer toolchain landscape"**: runs from user-space `-fsanitize=` all the way to in-kernel KASAN / KMSAN / UBSAN / KCSAN / KFENCE, spells out the two tracks of "compile-time instrumentation vs sampling", and the layered defense of "everything on while debugging" versus "resident in production".

Once you've read those three, you hold the complete toolchain for "making performance numbers trustworthy". They are the performance volume's foundation, not a side act.

## And then: from "measuring the real thing" to "measuring it accurately"

Sanitizers settle the **precondition**—guaranteeing that what you're measuring really is the piece of logic you meant to measure, not a UB-rewritten empty shell or noise stepping on someone else's memory. But "measuring the real thing" is only the first step: a real performance number is itself **still a random variable**. CPU frequency drifts, threads get scheduled away, caches warm up and cool down, page tables get built on demand… run the same function twice, and the numbers will differ.

"Measuring the real thing" plus "the number is a random variable"—those two together lead into ch01, Benchmark Methodology. That chapter is this volume's anchor, devoted to how to measure a random variable into a trustworthy conclusion and what it takes for a comparison of two numbers to count. Put another way: ch00 hands you a ruler that "keeps numbers from lying", and the three sanitizer articles are that ruler's calibration source; ch01 takes it from there—how to measure real things with this calibrated ruler.

## References

- cppreference: [undefined behavior](https://zh.cppreference.com/w/cpp/language/ub)
- GCC docs: `-fstrict-overflow` / `-fwrapv` (`man gcc` or gcc.gnu.org/onlinedocs/)
- The volume's three sanitizer articles (see "The three sanitizer articles in this chapter" above)
- Bryant, R. E., O'Hallaron, D. R., *Computer Systems: A Programmer's Perspective*, Chapter 5 (the premise of "correct first, then fast")
