---
chapter: 5
cpp_standard:
- 11
- 14
- 17
- 20
description: Comparing `std::array` with traditional arrays
difficulty: intermediate
order: 6
platform: stm32f1
prerequisites:
- 'Chapter 3: 内存与对象管理'
reading_time_minutes: 5
tags:
- cpp-modern
- intermediate
- stm32f1
title: std::array vs C-Style Arrays
translation:
  source: documents/vol8-domains/embedded/06-array-vs-raw-arrays.md
  source_hash: 5ba1a0a5b190946c88d042ddd4962f04ee54aa4d95df3de90b371f337ca695ae
  translated_at: '2026-05-26T12:23:23.370605+00:00'
  engine: anthropic
  token_count: 696
---
# Embedded C++ Tutorial — `std::array` vs C Arrays, Do You Know the Difference?

When writing embedded code, we often hesitate between two approaches: `int buf[16];` and `std::array<int, 16> buf;`. If we care about both performance and elegance, we naturally want to know: which one is more "embedded-friendly"?

------

## Why `std::array` Looks Like "a C Array Wearing a Coat" — But Is Actually Smarter

On the surface, `std::array<T, N>` is simply an aggregate type containing a `T elems[N]`: the elements are contiguous in memory, and the layout has no mysterious overhead. In many scenarios, `std::array` is equivalent to a raw array in terms of performance and memory footprint. In other words, we don't pay any extra runtime cost for switching to `std::array`.

But `std::array` wraps the array in a type: it has value semantics (it can be copied and assigned), provides `.size()`, offers `.data()`, includes `begin()`/`end()`, integrates seamlessly with STL algorithms, supports `constexpr` (with modern compilers), and can be better deduced as a template parameter. Most importantly, it makes "length is part of the type" explicit, making it much harder to lose size information when calling interfaces.

In short: `std::array` is a "safer, more modern" array.

------

## The Blunt Honesty and Fatal Naivety of Raw C Arrays

The advantage of raw arrays is "zero abstraction" — we have complete control over memory. This is crucial in startup code, driver layers, and buffers located in specific address spaces (such as those mapped to peripheral register addresses). Raw arrays don't pose challenges regarding ABI, the linker, or alignment — as long as we know what we are doing, they are highly reliable.

However, raw arrays also bring a host of common pitfalls: they decay into pointers in function parameters (so `sizeof` yields a pointer size inside a function), cannot be directly copied or assigned (`b = a;` will fail to compile), and offer no bounds or size protection. In embedded code, these "missing conveniences" force us to frequently write `memcpy`, constantly double-check if `N` is correct, and make rookie mistakes like "forgetting to pass the length" during code reviews.

A real-world scenario: we pass a raw array to a C API for DMA, but forget to tell the caller the length. As a result, DMA writes out of bounds and overwrites our most precious variables. Raw arrays don't warn us about these low-probability, high-cost errors.

------

## Advantages of `std::array`: Safer, More Readable, and More Modern C++ Friendly

The everyday advantages of `std::array` can be summarized as: clear semantics, friendly interfaces, and direct compatibility with algorithms. For example, `std::sort(a.begin(), a.end())` or `std::span(a)` are readily available benefits. `std::array` can be `=`, copied, or even safely returned as a function return value (without decaying), which makes code in mid-level logic more concise and less prone to memory manipulation bugs.

In an embedded context, this means test code, unit test stubs, and buffer wrappers will be much cleaner: we can write functions that return `std::array` instead of a messy pile of `memcpy`. Furthermore, when the compiler supports `constexpr`, `std::array` can construct constant tables at compile time, resulting in code that is both efficient and safe.

------

## So When Should We Stick with Raw C Arrays?

`std::array` is great, but it's not invincible. In the following scenarios, raw arrays remain the more appropriate choice:

1. **Initialization phases or early boot code (startup / crt0)**: Before `main()`, C++ global construction rules and runtime support can be troublesome. Raw arrays are more straightforward and reliable in such code, especially when we need to absolutely guarantee that no constructors or runtime code are involved.
2. **Placing objects in specific linker sections / at fixed addresses**: Things like interrupt vector tables, device-mapped buffers, and bootloader tables often require precise declaration of object location and byte order in the linker script. Raw arrays map more directly to the desired memory layout, reducing unnecessary abstraction.
3. **Strict ABI or interoperability with external C APIs where raw pointers are required**: Although `std::array` has `.data()`, in scenarios that are highly particular about binary compatibility, using raw arrays is more intuitive during audits (especially in legacy codebases).
4. **Extreme resource constraints where any extra compiler-generated metadata must be avoided**: Such situations are rare, but they do exist in some ultra-embedded or lowest-level kernel code.

------

## The Bottom Line

Raw arrays are simple, reliable tools suited for the layer closest to the hardware; `std::array` is a more modern, safer container that aligns better with C++ philosophy, suited for business logic, algorithm layers, and the vast majority of embedded application code. Treat them as two different knives in our toolbox: use the survival knife (raw array) to fix chip pins, and use the precision knife (`std::array`) to write protocol parsing and buffer logic.

One final piece of advice: when we can express the array size as a template parameter of `std::array<T, N>`, we should use `std::array`; when we must control every single byte precisely in a linker script or the earliest boot code, we should fall back to raw arrays without hesitation. Embedded development isn't about "staying pure" — it's about using the right tool for the actual need. `std::array` will often result in less code and fewer bugs, but occasionally we still need to roll up our sleeves and reach into raw memory to fix the lowest levels.

------

## Run Online

Run the `std::array` vs C array comparison example online to verify zero-overhead abstraction:

<OnlineCompilerDemo
  title="std::array vs C Array: Zero-Overhead Comparison"
  source-path="code/examples/compiler_explorer/array_vs_carray_host.cpp"
  arm-source-path="code/examples/compiler_explorer/array_vs_carray_arm.cpp"
  description="Run online to verify sizeof consistency between std::array and C arrays, and observe the pointer decay issue."
  allow-run
  allow-x86-asm
  allow-arm-asm
/>

## Code Examples
