---
chapter: 7
cpp_standard:
- 20
- 23
description: '[[likely]]/[[unlikely]], [[no_unique_address]], [[assume]], and other
  new attributes'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 7: 标准属性详解'
reading_time_minutes: 14
related:
- constexpr 构造函数与字面类型
tags:
- host
- cpp-modern
- intermediate
title: 'C++20-23 New Attributes: Performance-Oriented Compiler Hints'
translation:
  source: documents/vol2-modern-features/ch07-attributes/02-modern-attributes.md
  source_hash: df02100cdff5cb85a3066b40f414d0d3953d26316479485a538df470c13a674f
  translated_at: '2026-06-16T03:58:28.120582+00:00'
  engine: anthropic
  token_count: 2670
---
# C++20-23 New Attributes: Performance-Oriented Compiler Hints

In the previous chapter, we looked at standard attributes from C++11-17, which primarily addressed "code correctness"—enforcing return value checks, eliminating warnings, and marking deprecated APIs. The new attributes added in C++20 and C++23 shift focus: they are more concerned with performance, providing optimization hints to the compiler. `[[likely]]` and `[[unlikely]]` help the compiler optimize branch prediction (aha, I recall first encountering this when looking at GNU C extensions), `[[no_unique_address]]` saves redundant space in memory layouts, and `[[assume]]` allows the compiler to perform more aggressive optimizations based on assumptions.

When used correctly, these attributes can yield tangible performance gains, but misuse can be counterproductive. Let's break them down one by one.

> TL;DR: **New attributes in C++20-23 shift from "helping the compiler find bugs" to "helping the compiler optimize code." Using them in the right scenarios and verifying the results is the way to go.**

------

## [[likely]] and [[unlikely]] (C++20): Branch Prediction Hints

### Why Manual Hints are Needed

Modern CPUs have dynamic branch predictors that guess branch directions based on runtime history. In most cases, the CPU's guesses are smart enough. However, manual hints still hold value in specific scenarios: first, when a function is called for the first time and the branch predictor has no historical data; second, in embedded systems where some CPUs have simpler branch predictors; and third, because compilers can improve instruction cache hit rates by adjusting code layout (keeping hot paths together).

`[[likely]]` tells the compiler "this branch is more likely to be executed," while `[[unlikely]]` indicates "this branch is rarely executed."

### Syntax and Placement

These attributes can be placed in the body of an `if` statement or on the `case` label of a `switch` statement:

```cpp
// 1. Applied to the statement body (C++20 standard)
if (condition) {
    [[likely]] // Hints that the 'then' branch is likely
    // code for likely path
} else {
    [[unlikely]] // Hints that the 'else' branch is unlikely
    // code for unlikely path
}

// 2. Applied to the condition (GCC extension, non-standard)
if ([[likely]] condition) {
    // ...
}
```

⚠️ **Note on placement:** `[[likely]]` is placed before the statement body, not on the conditional expression itself. This is mandated by the C++20 standard.

### Analyzing Actual Effects: Let's Look at Assembly

Many articles tell you that "adding `[[likely]]` makes the compiler optimize code layout," but what exactly is optimized? Talk is cheap; let's look at the assembly directly. The following test uses GCC 15 with `-O2`:

```cpp
int test_likely(int x) {
    if (x > 0) [[likely]]
        return x * 2;
    else
        return x;
}

int test_unlikely(int x) {
    if (x > 0) [[unlikely]]
        return x * 2;
    else
        return x;
}
```

The assembly generated for both functions is **exactly the same**:

```asm
test_likely(int):
  mov eax, edi
  imul eax, edi
  test edi, edi
  cmovle eax, edi
  ret

test_unlikely(int):
  mov eax, edi
  imul eax, edi
  test edi, edi
  cmovle eax, edi
  ret
```

The compiler didn't generate a conditional branch at all—it used `cmov` (conditional move) to calculate both paths and then selected one based on the result of `test`. Branch prediction? Non-existent. `[[likely]]` has no effect here because the compiler found a solution better than branching.

This isn't an isolated case. Modern compilers, even at `-O2` or `-O3`, often optimize simple conditional branches into `cmov`, bitwise operations, or mathematical formulas, rendering `[[likely]]` a mere "code comment." Scenarios where `[[likely]]` actually affects code layout usually involve: longer branch bodies (more than a few instructions), function calls or memory operations inside branches, or complex logic that the compiler cannot replace with `cmov`.

### When is it Worth Using?

So, `[[likely]]` isn't a magic switch where "adding it makes it faster." The correct approach is: first, use profiling (like `perf`) to confirm that a specific branch has a high misprediction rate, then consider adding hints. Before adding one, compare the assembly to ensure the compiler actually changed the code layout. If the assembly hasn't changed, it means the compiler already optimized it in a better way, and `[[likely]]` is just redundant information noise.

Typical effective scenarios include: error checking branches (normal path `[[likely]]`, error path `[[unlikely]]`), boundary condition handling, and logic with complex branch bodies that the compiler cannot replace with `cmov`.

### Comparison with Compiler Built-ins

Before `[[likely]]` existed, GCC/Clang used `__builtin_expect` for branch prediction hints:

```cpp
// GCC/Clang built-in way
if (__builtin_expect(x > 0, 1)) { ... } // likely
if (__builtin_expect(x > 0, 0)) { ... } // unlikely
```

`[[likely]]` is much more readable, and being a standardized attribute means it works on all compilers supporting C++20.

------

## [[no_unique_address]] (C++20): Empty Base Optimization

### The Problem: Empty Classes Still Take 1 Byte

The C++ standard requires every complete object to have a unique address. This means that even an "empty class" with no data members has a `sizeof` at least 1. When you use an empty class as a member of another class, it wastes a whole byte for nothing:

```cpp
struct Empty {};
struct Holder {
    int data;
    Empty e; // Wastes 1 byte here!
};

// sizeof(Holder) is usually 8 (4 padding + 4 int), not 4.
```

For most applications, wasting 1 byte is negligible. However, in generic programming, policy classes (allocators, mutex policies, etc.) are often empty. If multiple policy classes are members simultaneously, each taking 1 byte, the waste adds up. More critically, this causes `sizeof` results to deviate from expectations, affecting optimizations like cache line alignment.

### The Traditional EBO Solution

The traditional solution is Empty Base Optimization (EBO)—holding the empty class via inheritance rather than as a member, so the compiler doesn't need to allocate separate space for it:

```cpp
// Traditional EBO: Use inheritance
template<typename Alloc, typename Mutex>
struct Optimized : private Alloc, private Mutex {
    int data;
    // No space wasted for Alloc or Mutex if they are empty
};
```

But EBO has downsides: you can only inherit from one base class of the same type (you can't inherit from two `Mutex` policies simultaneously); inheritance is a strong coupling, and modifying inheritance relationships just to save memory is unreasonable; and some coding standards prohibit private inheritance.

### The [[no_unique_address]] Solution

C++20's `[[no_unique_address]]` allows you to achieve the same optimization via member variables (instead of inheritance):

```cpp
struct Holder {
    [[no_unique_address]] Empty e;
    int data;
};
// sizeof(Holder) is now 4. 'e' shares the same address as 'data'.
```

### Application in Policy Pattern

`[[no_unique_address]]` is particularly useful in the policy pattern. Suppose you have a container class that accepts an allocator policy and a lock policy as template parameters. In a single-threaded scenario, the lock policy is an empty class (all methods are no-ops), and you don't want it to waste space:

```cpp
struct NullMutex { void lock() {} void unlock() {} }; // Empty class
struct RealMutex { std::mutex m; void lock() {} void unlock() {} }; // Has data

template<typename MutexPolicy>
class Container {
    [[no_unique_address]] MutexPolicy mutex;
    int data[100];
};

// Single-threaded usage
Container<NullMutex> c1; // sizeof(c1) == 400, no space wasted on mutex
// Multi-threaded usage
Container<RealMutex> c2; // sizeof(c2) == 408 (400 + 8 for std::mutex)
```

This design allows you to flexibly switch policies via template parameters without sacrificing memory efficiency. In single-threaded mode, not a single byte is wasted; in multi-threaded mode, a real mutex is used.

### Caveats

There are some details to watch out for with `[[no_unique_address]]`. Multiple `[[no_unique_address]]` members of the same type might share the same address (since they are all empty and need not be distinguished), depending on the compiler implementation:

```cpp
struct Test {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    [[no_unique_address]] Empty e3;
};
// It is implementation-defined whether e1, e2, e3 have the same address.
```

> **Verification**: Tested on GCC 15.2.1, multiple `[[no_unique_address]]` empty members do not necessarily share the same address, but the first empty member may share the same address as a subsequent non-empty member. The optimization effect of `[[no_unique_address]]` is definite and significant.

If you need to take the address of these members or point to them with references, be extremely careful—their addresses might be identical. Also, this attribute only works for empty classes. If the class has data members, adding it has no effect:

```cpp
struct NotEmpty { int x; };
struct Holder {
    [[no_unique_address]] NotEmpty e; // Attribute ignored, takes up space
    int data;
};
```

Additionally, MSVC in some versions has bugs regarding `[[no_unique_address]]`—even empty classes might not be optimized. This requires special attention in cross-platform projects; it is recommended to verify `sizeof` results on the target platform.

------

## [[assume]] (C++23): Compiler Assumptions

### Semantics

C++23's `[[assume]]` tells the compiler "please assume this expression is true." The compiler can perform more aggressive optimizations based on this assumption. If the expression is actually false at runtime, the behavior is undefined.

This differs from `assert`. `assert` checks the condition at runtime and terminates the program if it fails; `[[assume]]` performs no runtime check at all, simply allowing the compiler to optimize boldly.

### Example

```cpp
void safe_divide(int a, int b) {
    [[assume: b != 0]]; // Tell compiler: b is never 0
    // Compiler may omit the divide-by-zero check
    int result = a / b;
}
```

In this example, the compiler can theoretically omit the code path for the zero-divide check, generating faster division instructions. But if you pass `0` for `b`, the consequences are undefined—it might crash, return garbage, or appear normal while silently corrupting data.

> **Verification**: Under GCC 15.2.1 at `-O2` optimization, a simple division function generates the same assembly whether or not `[[assume]]` is used. This indicates that for simple scenarios, the compiler has already done sufficient optimization. The value of `[[assume]]` is mainly seen in more complex scenarios where the compiler cannot infer invariants through static analysis.

### Comparison with `__builtin_assume`

Before `[[assume]]`, MSVC used `__assume`, and GCC used `__builtin_assume` (though GCC's more common way is `if (cond) __builtin_unreachable();`):

```cpp
// MSVC
__assume(b != 0);

// GCC
__builtin_assume(b != 0);
// Or the classic trick:
if (!(b != 0)) __builtin_unreachable();
```

### Use Cases

Typical use cases for `[[assume]]` are: when you have definitive knowledge of certain runtime conditions that the compiler cannot infer through static analysis. For example, if you know an array access will never go out of bounds, or that a pointer is never null:

```cpp
void process_array(int* arr, size_t size) {
    [[assume: size == 16]]; // Optimization hint for fixed-size processing
    [[assume: arr != nullptr]];
    // Compiler can vectorize or unroll loops more aggressively
}
```

⚠️ **Warning:** `[[assume]]` is the most dangerous of all attributes. If your assumption is wrong, the program's behavior is completely unpredictable. The author suggests using it only after thorough profiling, confirming a bottleneck, and when you can 100% guarantee the condition always holds. In 99% of code, you don't need it.

------

## C++20 [[nodiscard]] Enhancements

The previous chapter mentioned that C++20 added the ability for `[[nodiscard]]` to carry custom messages. Here is a brief supplement.

### Extension of nodiscard in the Standard Library

C++20 also expanded the scope of `[[nodiscard]]` in the standard library. The following standard library functions are marked with `[[nodiscard]]`:

- `std::atomic::try_lock` (since C++20)
- `std::vector::empty` (since C++20)

> **Verification**: Tested in libstdc++ 15.2.1, the `empty` method indeed produces a nodiscard warning. However, the claim in the article that `std::vector` and `std::string` types themselves are marked `[[nodiscard]]` is not accurate in the current implementation—at least `std::vector` constructors do not produce warnings. Support for this varies across standard library implementations (libstdc++, libc++, MSVC STL).

This means if you write `vec.empty()` instead of `vec.clear()`, a C++20 compiler will issue a warning. This used to be a common source of bugs—`empty` looks like "clear," but it actually means "is empty." With `[[nodiscard]]`, misused code at least gets a warning reminder.

```cpp
std::vector<int> vec = {1, 2, 3};
vec.empty(); // Warning: ignoring return value of 'empty' [-Wunused-result]
```

### Using nodiscard Messages in Your Own Code

For library authors, `[[nodiscard("reason")]]` is very practical. You can explain in the message why the return value shouldn't be ignored and how to use it correctly:

```cpp
[[nodiscard("Returning a raw pointer requires manual memory management")]]
int* get_data();
```

------

## Comparison with C++11-17 Attributes

Comparing attributes from C++11-17 with the new ones in C++20-23 reveals a clear evolutionary path: early attributes focused on code correctness and maintainability, while later attributes focus more on performance optimization.

| Attribute | Version | Focus | Risk |
|-----------|---------|-------|------|
| `[[noreturn]]` | C++11 | Correctness | Low |
| `[[carries_dependency]]` | C++11 | Performance | Low |
| `[[deprecated]]` | C++14 | Maintainability | Low |
| `[[nodiscard]]` | C++17 | Correctness | Low |
| `[[maybe_unused]]` | C++17 | Correctness | Low |
| `[[fallthrough]]` | C++17 | Readability | Low |
| `[[likely]]` / `[[unlikely]]` | C++20 | Performance | Low |
| `[[no_unique_address]]` | C++20 | Performance | Low |
| `[[assume]]` | C++23 | Performance | **High** |

Only `[[assume]]` is truly "dangerous"—if the assumption is wrong, the consequence is undefined behavior. For other attributes, even if the "hint" is wrong, the worst case is slightly worse performance; it won't crash the program.

------

## Recommendations for Measuring Performance Impact

For performance-oriented attributes like `[[likely]]`/`[[unlikely]]` and `[[assume]]`, the author's advice is: always measure after adding them. Optimization effectiveness depends heavily on specific hardware, compilers, and code context. Some scenarios show clear gains, while others show no difference at all.

Testing methods can be simple: use tools like `perf` or `VTune` to compare instruction count, branch misprediction rates, and cache hit rates before and after adding the attribute. If there is no significant improvement, it's not worth adding—because attributes increase the "information density" of the code, requiring the reader to understand one more concept.

For `[[no_unique_address]]`, verification is more direct—just look at the `sizeof` results. If the empty policy class indeed takes no space, the attribute is working.

------

## Summary

New attributes in C++20-23 extend compiler hints from "finding bugs" to "performing optimizations." `[[likely]]` and `[[unlikely]]` help the compiler with branch prediction, `[[no_unique_address]]` eliminates memory waste from empty class members, and `[[assume]]` allows the compiler to make more aggressive optimizations based on deterministic assumptions.

The risks of these three attributes vary. `[[no_unique_address]]` is mostly harmless—the worst case is the optimization doesn't kick in, and `sizeof` remains unchanged. `[[likely]]`/`[[unlikely]]` are also low risk—the worst case is a wrong hint, leading to slightly worse performance. `[[assume]]` is the only truly dangerous attribute—a wrong assumption leads to undefined behavior and must be used with caution.

In practice, `[[no_unique_address]]` can be used almost mindlessly in generic code (policy pattern), `[[likely]]`/`[[unlikely]]` are recommended after profiling confirms hotspots, and `[[assume]]` should only be used in extreme performance-sensitive scenarios, accompanied by corresponding assertions or tests to ensure assumptions always hold.

## Reference Resources

- [cppreference: assume (C++23)](https://en.cppreference.com/w/cpp/language/attributes/assume)
- [cppreference: likely/unlikely (C++20)](https://en.cppreference.com/w/cpp/language/attributes/likely)
- [cppreference: no_unique_address (C++20)](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [Don't use [[likely]] or [[unlikely]] - Aaron Ballman](https://blog.aaronballman.com/2020/08/dont-use-the-likely-or-unlikely-attributes/)
