---
title: 'C++20-23 New Attributes: Performance-Oriented Compiler Hints'
description: '[[likely]]/[[unlikely]], [[no_unique_address]], [[assume]], and other
  new attributes'
chapter: 7
order: 2
tags:
- host
- cpp-modern
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 20
- 23
reading_time_minutes: 15
prerequisites:
- 'Chapter 7: 标准属性详解'
related:
- constexpr 构造函数与字面类型
translation:
  source: documents/vol2-modern-features/ch07-attributes/02-modern-attributes.md
  source_hash: f2a8984b78649a0904715ec0cfc829732f4a4300acc9ce5b747c822345a9f146
  translated_at: '2026-06-14T00:18:22.744952+00:00'
  engine: anthropic
  token_count: 2676
---
# C++20-23 New Attributes: Performance-Oriented Compiler Hints

In the previous chapter, we looked at standard attributes from C++11 to C++17, which primarily address "code correctness" issues—enforcing return value checks, eliminating warnings, and marking deprecated APIs. The new attributes added in C++20 and C++23 shift direction: they focus more on performance, providing optimization hints to the compiler. `[[likely]]` and `[[unlikely]]` help the compiler optimize branch prediction (aha, I recall first encountering this when looking at GNU C extensions), `[[no_unique_address]]` saves redundant space in memory layouts, and `[[assume]]` allows the compiler to perform more aggressive optimizations based on assumptions.

When used correctly, these attributes can deliver tangible performance gains, but misuse can be counterproductive. Let's break them down one by one.

> TL;DR: **New attributes in C++20-23 shift from "helping the compiler find bugs" to "helping the compiler optimize code." Using them in the right scenarios and verifying the results is the way to go.**

------

## [[likely]] and [[unlikely]] (C++20): Branch Prediction Hints

### Why manual hints are needed

Modern CPUs have dynamic branch predictors that guess branch directions based on runtime history. In most cases, the CPU is smart enough. However, manual hints are still valuable in specific scenarios: first, when a function is called for the first time, the branch predictor has no historical data; second, some CPUs in embedded systems have simpler branch predictors; and third, compilers can improve instruction cache hit rates by adjusting code layout (keeping hot paths together).

`[[likely]]` tells the compiler "this branch is more likely to be executed," while `[[unlikely]]` indicates "this branch is rarely executed."

### Syntax and placement

These attributes can be placed in the branch body of an `if` statement, or on the `case` label of a `switch` statement:

```cpp
// 1. Applied to the statement block
if (cond) {
    [[likely]] // Placed before the statement block
    do_something();
} else {
    [[unlikely]]
    handle_error();
}

// 2. Applied to switch case labels
switch (value) {
    [[unlikely]] case 0:
        handle_rare_case();
        break;
    [[likely]] default:
        handle_common_case();
        break;
}
```

⚠️ **Note on attribute placement:** `[[likely]]` is placed before the statement block of the branch, not on the conditional expression itself. This is mandated by the C++20 standard.

### Analyzing actual effects: Look at the assembly first

Many articles will tell you that "adding `[[likely]]` makes the compiler optimize code layout," but what exactly is optimized? Talk is cheap; let's look at the assembly directly. The following test uses GCC 15 with `-O2`:

```cpp
int add_if(int x, int y) {
    if (x > 0) [[likely]]
        return x + y;
    else
        return x - y;
}

int add_unlikely(int x, int y) {
    if (x > 0) [[unlikely]]
        return x + y;
    else
        return x - y;
}
```

The assembly generated for both functions is **exactly the same**:

```text
add_if(int, int):
        cmpl    $0, %edi
        movl    %edx, %eax
        leal    (%rdi,%rdx), %edx
        cmovg   %edx, %eax
        ret
add_unlikely(int, int):
        cmpl    $0, %edi
        movl    %edx, %eax
        leal    (%rdi,%rdx), %edx
        cmovg   %edx, %eax
        ret
```

The compiler didn't generate a conditional branch at all—it used `cmovg` (conditional move) to calculate both paths and then select one based on the result of `cmpl`. Branch prediction? Non-existent. `[[likely]]` has no effect here because the compiler found a solution better than branching.

This isn't an isolated case. Modern compilers, even at `-O2` or `-O3`, often optimize simple conditional branches into `cmov`, bit operations, or mathematical formulas, rendering `[[likely]]` a mere "code comment." Scenarios where `[[likely]]` actually affects code layout are usually those where: the branch body is long (more than a few instructions), the branch contains function calls or memory operations, or the logic is too complex for the compiler to replace with `cmov`.

### When is it worth using

So, `[[likely]]` isn't a magic switch where "adding it makes it faster." The correct approach is: first use profiling (like `perf`) to confirm that a specific branch has a high misprediction rate, then consider adding hints. Compare the assembly before and after to ensure the compiler actually changed the code layout. If the assembly hasn't changed, it means the compiler already optimized it in a better way, and `[[likely]]` is just redundant information noise.

Typical effective scenarios include: error checking branches (normal path `[[likely]]`, error path `[[unlikely]]`), boundary condition handling, and complex logic where the compiler cannot substitute with `cmov`.

### Comparison with compiler built-ins

Before `[[likely]]` existed, GCC/Clang used `__builtin_expect` for branch prediction hints:

```cpp
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

if (UNLIKELY(err != success)) {
    // ...
}
```

`[[likely]]` is much more readable, and being a standardized attribute means it works on all compilers supporting C++20.

------

## [[no_unique_address]] (C++20): Empty Base Optimization

### The problem: Empty classes still take up 1 byte

The C++ standard requires every complete object to have a unique address. This means that even "empty classes" with no data members have a `sizeof` at least 1. When you use an empty class as a member of another class, it wastes a whole byte:

```cpp
struct Empty {}; // sizeof(Empty) is 1

struct Widget {
    int data;
    Empty e; // Wastes 1 byte here
};
// sizeof(Widget) is likely 8 (4 + 1 + 3 padding)
```

For most applications, wasting 1 byte is negligible. However, in generic programming, policy classes (allocator, mutex policy, etc.) are often empty. If multiple policy classes are members simultaneously, each taking 1 byte, the waste adds up. More critically, this makes `sizeof` results unexpected, affecting optimizations like cache line alignment.

### The traditional EBO solution

The traditional solution is Empty Base Optimization (EBO)—holding empty classes via inheritance instead of membership, so the compiler doesn't need to allocate separate space for them:

```cpp
// Base class optimization
struct Widget : private Empty {
    int data;
};
// sizeof(Widget) is likely 4
```

But EBO has downsides: you can only inherit from one base class of the same type (you can't inherit from two `Empty` bases directly); inheritance is a strong coupling relationship, and modifying inheritance just to save memory is unreasonable; and some coding standards prohibit private inheritance.

### The [[no_unique_address]] solution

The `[[no_unique_address]]` attribute introduced in C++20 allows you to achieve the same optimization via member variables (instead of inheritance):

```cpp
struct Widget {
    int data;
    [[no_unique_address]] Empty e;
};
// sizeof(Widget) is likely 4
```

### Application in the Strategy pattern

`[[no_unique_address]]` is particularly useful in the Strategy pattern. Suppose you have a container class that accepts an allocator strategy and a lock strategy as template parameters. In a single-threaded scenario, the lock strategy is an empty class (all methods are no-ops), and you don't want it to waste space:

```cpp
struct NullMutex {
    void lock() {}
    void unlock() {}
};

struct RealMutex {
    void lock() { /* ... */ }
    void unlock() { /* ... */ }
    std::mutex m;
};

template<typename MutexPolicy>
class Container {
    // In single-threaded mode, NullMutex takes up 0 space
    [[no_unique_address]] MutexPolicy mutex_;
    // ... other data members ...
};
```

This design allows you to flexibly switch strategies via template parameters without sacrificing memory efficiency. In single-threaded mode, not a single byte is wasted; in multi-threaded mode, a real mutex is used.

### Caveats

There are some details to watch out for with `[[no_unique_address]]`. Multiple `[[no_unique_address]]` members of the same type might share the same address (since they are all empty and don't need distinction), and the specific behavior depends on the compiler implementation:

```cpp
struct Widget {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int data;
};
// It is possible that &e1 == &e2 == &data
```

> **Verification**: Tested on GCC 15.2.1, multiple `[[no_unique_address]]` empty members do not necessarily share the same address, but the first empty member's address may be the same as subsequent non-empty members. The optimization effect of `[[no_unique_address]]` is definite and significant.

If you need to take the address of these members or point to them with references, be extremely careful—their addresses might be identical. Additionally, this attribute only works for empty classes. If the class has data members, adding it has no effect:

```cpp
struct NotEmpty {
    [[no_unique_address]] int x; // No effect, x takes up space
};
```

Also, MSVC in some versions has bugs regarding `[[no_unique_address]]` support—even empty classes might not be optimized. This requires special attention in cross-platform projects; it is recommended to verify `sizeof` results on the target platform.

------

## [[assume]] (C++23): Compiler Assumptions

### Semantics

The `[[assume]]` attribute introduced in C++23 tells the compiler "please assume this expression is true," allowing the compiler to perform more aggressive optimizations based on this assumption. If the expression is actually false at runtime, the behavior is undefined.

This differs from `assert`. `assert` checks the condition at runtime and terminates the program if it fails; `[[assume]]` performs no runtime check at all, simply letting the compiler optimize boldly.

### Example

```cpp
int safe_divide(int a, int b) {
    [[assume: b != 0]]; // Tell the compiler b is never 0
    return a / b;
}
```

In this example, the compiler can theoretically omit the divide-by-zero check code path and generate faster division instructions. But if you pass `0` for `b`, the consequences are undefined—it might crash, return garbage, or look normal while secretly corrupting state.

> **Verification**: Under GCC 15.2.1's `-O3` optimization level, a simple division function generates the same assembly code whether or not `[[assume]]` is used. This indicates that for simple scenarios, the compiler has already done sufficient optimization. The value of `[[assume]]` is mainly seen in more complex scenarios where the compiler cannot deduce invariants through static analysis.

### Comparison with __builtin_assume

Before `[[assume]]`, MSVC used `__assume`, GCC used `__builtin_assume` (though GCC's more common way is `if (cond) __builtin_unreachable();`):

```cpp
// GCC style
void func(int* p) {
    if (!p) __builtin_unreachable(); // p is not null
    // ...
}

// C++23 style
void func(int* p) {
    [[assume: p != nullptr]];
    // ...
}
```

### Usage scenarios

Typical use cases for `[[assume]]` are: you have definitive knowledge of certain runtime conditions that the compiler cannot infer through static analysis. For example, you know an array access will never go out of bounds, or you know a pointer is never null:

```cpp
void process_buffer(const int* arr, size_t size) {
    [[assume: size % 16 == 0]]; // Alignment guarantee
    [[assume: arr != nullptr]];
    // Compiler can now auto-vectorize more aggressively
}
```

⚠️ **Warning**: `[[assume]]` is the most dangerous of all attributes. If your assumption is wrong, the program's behavior is completely unpredictable. I recommend using it only after full profiling, confirming a bottleneck, and when you can 100% guarantee the condition always holds. In 99% of code, you don't need it.

------

## C++20 [[nodiscard]] Enhancements

The previous chapter mentioned that C++20 added the ability to include custom messages with `[[nodiscard]]`. Here is a brief supplement.

### Extension of nodiscard in the standard library

C++20 also expanded the application scope of `[[nodiscard]]` in the standard library. The following standard library functions are marked with `[[nodiscard]]`:

- `std::atomic::try_lock` (since C++20)
- `std::vector::empty` (since C++20)

> **Verification**: Tested in libstdc++ 15.2.1, the `empty()` method does produce a `nodiscard` warning. However, the article's claim that `std::vector` and `std::string` types themselves are marked `[[nodiscard]]` is not accurate in current implementations—at least `std::vector` constructors do not produce warnings. Support for this varies across standard library implementations (libstdc++, libc++, MSVC STL).

This means if you write `vec.empty()` instead of `vec.clear()`, a C++20 compiler will issue a warning. Previously, this was a common source of bugs—`empty()` looks like "clear", but actually means "is empty". With `[[nodiscard]]`, misused code at least gets a warning reminder.

```cpp
std::vector<int> vec = {1, 2, 3};
vec.empty(); // Warning: ignoring return value of 'empty' [-Wunused-result]
```

### Using nodiscard messages in your own code

For library authors, `[[nodiscard("reason")]]` is very practical. You can explain in the message why the return value shouldn't be ignored and how to use it correctly:

```cpp
[[nodiscard("Returning a raw pointer requires manual memory management; consider using std::unique_ptr")]]
int* create_data();
```

------

## Comparison with C++11-17 Attributes

Comparing attributes from C++11-17 with the new attributes in C++20-23 reveals a clear development trajectory: early attributes focused on code correctness and maintainability, while later attributes focus more on performance optimization.

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

Only `[[assume]]` is a truly "dangerous" attribute—if the assumption is wrong, the consequence is undefined behavior. With other attributes, even if the "hint" is wrong, the worst case is slightly worse performance; it won't crash the program.

------

## Performance Impact Testing Recommendations

For performance-oriented attributes like `[[likely]]`/`[[unlikely]]` and `[[assume]]`, my advice is: always test after adding them. Optimization effectiveness depends heavily on specific hardware, compilers, and code context. Some scenarios show significant gains, while others show no difference at all.

Testing methods can be simple: use tools like `perf` or `VTune` to compare instruction count, branch misprediction rate, and cache hit rate before and after adding the attribute. If there is no significant improvement in data, it's not worth adding—because attributes increase the "information density" of the code, requiring readers to understand one more concept.

For `[[no_unique_address]]`, verification is more direct—just look at the `sizeof` results. If empty policy classes indeed take up no space, the attribute is working.

------

## Summary

New attributes in C++20-23 extend compiler hint capabilities from "finding bugs" to "doing optimizations." `[[likely]]` and `[[unlikely]]` help the compiler with branch prediction, `[[no_unique_address]]` eliminates memory waste from empty class members, and `[[assume]]` lets the compiler perform more aggressive optimizations based on deterministic assumptions.

The risks of these three attributes vary. `[[no_unique_address]]` is mostly harmless—the worst case is the optimization doesn't kick in, and `sizeof` remains unchanged. `[[likely]]`/`[[unlikely]]` risks are also low—the worst case is a wrong branch prediction hint, leading to slightly worse performance. `[[assume]]` is the only truly dangerous attribute—a wrong assumption leads to undefined behavior and must be used with caution.

In practice, `[[no_unique_address]]` can be used almost without thinking in generic code (strategy pattern), `[[likely]]`/`[[unlikely]]` are recommended after profiling confirms hotspots, and `[[assume]]` should only be used in extreme performance-sensitive scenarios, accompanied by corresponding assertions or tests to ensure assumptions always hold.

## Reference Resources

- [cppreference: assume (C++23)](https://en.cppreference.com/w/cpp/language/attributes/assume)
- [cppreference: likely/unlikely (C++20)](https://en.cppreference.com/w/cpp/language/attributes/likely)
- [cppreference: no_unique_address (C++20)](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [Don't use [[likely]] or [[unlikely]] - Aaron Ballman](https://blog.aaronballman.com/2020/08/dont-use-the-likely-or-unlikely-attributes/)
