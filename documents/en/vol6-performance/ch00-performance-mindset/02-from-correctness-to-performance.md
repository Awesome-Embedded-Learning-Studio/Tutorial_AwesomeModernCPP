---
chapter: 0
cpp_standard:
- 11
description: Picking up from ch00-01's 'correct first, then fast', this article unpacks why a performance number measured in the presence of UB is untrustworthy, explains why sanitizer (ASan/UBSan/MSan/TSan) sits in ch00 as the foundation rather than being filed under 'debugging tools', and hands the narrative off to the three sanitizer articles and ch01.
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Performance Mindset: efficiency is not performance'
reading_time_minutes: 7
related:
- The ASan tool family and memory safety
- Why microbenchmarks lie
tags:
- host
- cpp-modern
- intermediate
- 优化
- 内存安全
title: 'From "Correct First" to "Then Fast": why sanitizer is the foundation of the performance volume'
translation:
  source: documents/vol6-performance/ch00-performance-mindset/02-from-correctness-to-performance.md
  source_hash: ba636b7fb40e12877c77571daaca869b512a3c465d4a810f6b30a5244b78c65d
  translated_at: '2026-07-05T13:10:00+00:00'
  engine: manual
  token_count: 3000
---
# From "Correct First" to "Then Fast": why sanitizer is the foundation of the performance volume

## The line we left at the end of the previous article

When ch00-01 laid down iron rule 1, "correct first, then fast", we dropped a heavy line: **talking about performance numbers in the presence of undefined behavior (UB) is like building on a foundation that hasn't been poured yet.** This article unpacks that line and shows you exactly how UB turns a performance number into a lie. Once you see this, you'll understand something that might otherwise puzzle you: why a volume on performance optimization opens not with cache, not with SIMD, but with a full sanitizer toolchain.

## How UB turns a performance number into a lie

The C++ standard marks a class of behaviors as "undefined": signed integer overflow, out-of-bounds access, dereferencing a null pointer, reading an uninitialized variable, data races… For these, the standard **guarantees nothing** about your program's behavior: **anything can happen**, including the case you least want, where it "runs fine and every result is wrong".

The scary part is that the compiler **actively exploits** this rule. Under `-O2`, the compiler is allowed to assume "this code won't trigger UB", and then optimizes aggressively based on that assumption. The standard permits it, and every modern compiler does it daily. For performance measurement, the consequences fall into three categories, each enough to invalidate your numbers:

**Category 1: the code you wanted to measure gets deleted entirely.** Your benchmark computes a result nobody uses, the compiler decides it's dead code, and eliminates it (DCE). You smugly measure 0.3 nanoseconds—and you measured nothing. This is why the `volatile global_sink` in the ch00-01 benchmark had to exist.

**Category 2: the compiler decides your loop for you.** A signed loop variable that might overflow is UB; the compiler can assume it won't overflow, then derive an upper bound, hoist the entire loop into a constant, or just fold it away. You think you ran N iterations; actually it ran zero.

**Category 3, the sneakiest one: you think you're measuring A, but you're actually stepping on B's memory.** Out-of-bounds writes, use-after-free, uninitialized reads—these don't crash your program, they just make your benchmark read/write the memory of "some other variable". You measure "this code takes 50 ns", but half of those 50 ns are spent corrupting a neighboring data structure; the number is meaningless, and the program is still "running fine".

Here's a minimal, classic example to make it concrete. This function checks whether "`x+1` is greater than `x`":

```cpp
bool always_bigger(int x) { return (x + 1) > x; }   // x+1 overflow = UB
```

Under `-O2`, gcc, reasoning from "signed overflow can't happen", folds the whole function into `return true;`—a single assembly line `movl $1, %eax; ret`, with no regard for `x`. So even if you pass `INT_MAX` (the value where `x+1` actually overflows), it returns `true` with a straight face. This is documented behavior under gcc's `-fstrict-overflow` (on by default at `-O2`); cppreference's undefined-behavior page uses it as a textbook counterexample.

Talk is cheap; I ran it on my own machine (GCC 16.1.1), feeding the function the same value `INT_MAX`:

```text
# -O2 (UB assumption allowed: the whole thing folds to return true)
$ g++ -O2 ub_fold.cpp -o ub_fold && ./ub_fold 2147483647
f(INT_MAX) = 1

# -O2 -fwrapv (force signed overflow to wrap as defined behavior: actually compute it)
$ g++ -O2 -fwrapv ub_fold.cpp -o ub_fold_wrap && ./ub_fold_wrap 2147483647
f(INT_MAX) = 0      # INT_MAX+1 wraps to INT_MIN, and INT_MIN > INT_MAX is false
```

Same optimization level (`-O2`), same input (`INT_MAX`), the only difference is whether signed overflow is UB or defined as wrapping—the result is `1` in one case and `0` in the other. This is the deadliest part of UB in performance measurement: **the thing you're measuring is itself unstable; change one compiler flag and you're no longer measuring the same thing.** Side note on a trap: you might want to use `-O0` as the control (hoping it'll just do the addition and wrap to `0`), but contemporary GCC's mid-end recognizes the `(x+1)>x` idiom even at `-O0` and folds it anyway, so the cleaner control is `-fwrapv`, not a different optimization level.

So a benchmark carrying UB, measured at `-O2` as "30% faster", may well be 30% saved by "the compiler deleting half your loop based on a UB assumption". That 30% evaporates instantly in production (different compiler flags, different input distributions), or even flips into "slower". Taking that fake number into an architecture decision is building on sand.

## So sanitizer isn't a "debugging tool", it's the credibility foundation of performance measurement

With that layer clear, you can see why this volume puts sanitizer in ch00 instead of the usual "debugging tips" mention. The four siblings each cover a class of UB that invalidates performance numbers:

- **ASan** (AddressSanitizer) covers memory errors—out-of-bounds, use-after-free, double free. These are exactly the culprits behind "category 3" above, the ones that make your benchmark secretly step on someone else's memory.
- **UBSan** (UndefinedBehaviorSanitizer) covers language-level UB—signed overflow, null-pointer dereference, wrong-type casts, illegal shifts. These are exactly the culprits behind "category 1" and "category 2", the ones that give the compiler license to rewrite your measured code into an empty shell.
- **MSan** (MemorySanitizer) covers uninitialized reads—what you read is "a random value", so what you're really measuring is a random-number generator.
- **TSan** (ThreadSanitizer) covers concurrency data races—and a data race is itself UB. This one is especially lethal in concurrency performance measurement: if your multi-threaded benchmark hasn't been through TSan, that pretty throughput number means nothing. TSan's mechanism is covered thoroughly in vol5; here we only take the "why it's a performance foundation" angle: the credibility of a concurrency performance number presupposes no data races.

Put these four together and the conclusion is hard: **a performance number that hasn't been through sanitizer, you don't know what it measured.** The precondition doesn't even hold; "precise or not" is beside the point. So the rule in this volume is: any code entering a performance comparison first runs clean under sanitizer, then we talk numbers.

## The three sanitizer articles in this chapter

How exactly to turn it on, how to read its reports, how to coexist with `-O2`, the trade-offs between debug builds and production—that's the job of the three sanitizer articles; this one is just a signpost pointing at what each covers:

- **"The ASan tool family and memory safety"**: starts from the real-world disaster of Heartbleed, unpacks the shadow-memory mechanism, measures out-of-bounds, UAF, and global overflow, and sorts out the five siblings ASan / LSan / MSan / TSan / UBSan—what each does and why most are mutually exclusive (can't be enabled together).
- **"Valgrind vs ASan"**: puts Valgrind's dynamic-binary-translation route next to ASan's compile-time-instrumentation route, and explains the real differences in performance, in what errors they catch, and in the barrier to use.
- **"The sanitizer toolchain landscape"**: goes from user-space `-fsanitize=` all the way to kernel-space KASAN / KMSAN / UBSAN / KCSAN / KFENCE, laying out the two threads of "compile-time instrumentation vs sampling" and the layered defense of "full power when debugging" vs "always-on in production".

Read those three and you have the complete toolchain for "making performance numbers trustworthy". They're the foundation of the performance volume, not a side act.

## And then: from "measuring the real thing" to "measuring it well"

Sanitizer handles the **precondition**—making sure you're actually measuring the logic you meant to measure, not a UB-rewritten empty shell or noise stepping on someone else's memory. But "measuring the real thing" is only step one; a real performance number is itself **a random variable**: CPU frequency drifts, threads get scheduled away, caches warm and cool, page tables get built on demand… run the same function twice and the numbers differ.

"Measuring the real thing" plus "the number is a random variable" together lead into ch01, Benchmark Methodology. That chapter is the volume's anchor; it's about how to turn a random variable into a trustworthy conclusion, and how to compare two numbers legitimately. Put differently: ch00 gives you a ruler that "keeps numbers honest", and the sanitizer trio is that ruler's calibration source; ch01 picks it up and teaches you how to measure real things with a calibrated ruler.

## References

- cppreference: [undefined behavior](https://en.cppreference.com/w/cpp/language/ub)
- GCC docs: `-fstrict-overflow` / `-fwrapv` (`man gcc` or gcc.gnu.org/onlinedocs/)
- The three sanitizer articles in this volume (see "The three sanitizer articles in this chapter" above)
- Bryant, R. E., O'Hallaron, D. R. *Computer Systems: A Programmer's Perspective*, Chapter 5 (the premise of "correct first, then fast")
