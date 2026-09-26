---
chapter: 7
cpp_standard:
- 20
- 23
description: '[[likely]]/[[unlikely]], [[no_unique_address]], [[assume]], and other new attributes'
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 7: Deep Dive into Standard Attributes: Making the Compiler Your Code Reviewer'
reading_time_minutes: 14
related:
- constexpr Constructors and Literal Types
tags:
- host
- cpp-modern
- intermediate
title: 'C++20-23 New Attributes: Performance-Oriented Compiler Hints'
translation:
  source: documents/vol2-modern-features/ch07-attributes/02-modern-attributes.md
  source_hash: 275036bd2d02fcf8e41a130e7bf4f7e98a6f0507ab0c8cf46e8a6522af7e6aae
  translated_at: '2026-09-25T16:03:10+00:00'
  engine: anthropic
  token_count: 3600
---
# C++20-23 New Attributes: Performance-Oriented Compiler Hints

In the previous chapter we looked at the C++11-17 standard attributes, which mostly target "code correctness"—enforcing return-value checks, eliminating warnings, marking deprecated APIs. The attributes added in C++20 and C++23 change direction: they care more about performance, handing the compiler optimization hints. `[[likely]]` and `[[unlikely]]` help the compiler optimize branch prediction (aha—I remember first running into this while reading code that used GNU C extensions), `[[no_unique_address]]` trims redundant space out of memory layouts, and `[[assume]]` lets the compiler optimize more aggressively based on assumptions.

Used well, these attributes buy real, tangible performance; used wrong, they can backfire. Let's take them apart one by one.

> One-sentence summary: **the new C++20-23 attributes shift from "helping the compiler find bugs" to "helping the compiler optimize code". Pick the right scenario and verify the results—that's the way to go.**

------

Before the one-by-one dissection, let's first put the three performance attributes side by side on one effects card:

![C++20-23 performance attributes effects card: what they do, what they save, where the pitfalls are](./02-modern-attributes-effects.drawio)

## [[likely]] and [[unlikely]] (C++20): Branch Prediction Hints

### Why Manual Hints Are Needed

Modern CPUs all have dynamic branch predictors that guess which way a branch will go based on runtime history. Most of the time the CPU's guesses are already smart enough. But manual hints still have value in these scenarios: first, when a function is called for the first time, the branch predictor has no history yet; second, in embedded systems some CPUs have rather primitive branch predictors; third, the compiler can improve instruction-cache hit rates by rearranging the code layout (grouping hot paths together).

`[[likely]]` tells the compiler "this branch is more likely to be executed", while `[[unlikely]]` says "this branch rarely executes".

### Syntax and Placement

This pair of attributes can be placed in the branch body of an `if` statement, or on a `case` label of a `switch`:

```cpp
// Placed in an if branch
if (error == ErrorCode::Ok) [[likely]] {
    // Normal path — very likely to execute
    process_data();
} else {
    // Error path — rarely executes
    handle_error();
}

// Placed on a switch case
switch (status) {
    [[likely]] case Status::Running:
        run_task();
        break;
    case Status::Error:
        recover();
        break;
    default:
        break;
}
```

Watch the attribute placement: `[[likely]]` goes before the `{` of the branch body, not on the condition expression. That's what the C++20 standard specifies.

### Analyzing the Real Effect: Assembly First, Talk Later

Many articles will tell you "add `[[likely]]` and the compiler optimizes the code layout", but what exactly did it optimize? Talk is cheap; let's read the assembly directly. The following test was compiled with GCC 15 at `-O2 -std=c++20`:

```cpp
// Without the hint
int process_no_hint(int value) {
    if (value > 0) {
        return value * 2;
    } else {
        return -value;
    }
}

// With [[likely]]
int process_likely(int value) {
    if (value > 0) [[likely]] {
        return value * 2;
    } else {
        return -value;
    }
}
```

The assembly generated for the two functions is **completely identical**:

```asm
process_no_hint:
process_likely:
    movl    %edi, %eax
    leal    (%rdi,%rdi), %edx
    negl    %eax
    testl   %edi, %edi
    cmovg   %edx, %eax
    ret
```

The compiler never emitted a conditional branch at all—it computes both paths with `cmovg` (a conditional move) and picks one based on the result of `testl`. Branch prediction? Doesn't exist here. `[[likely]]` has no effect whatsoever in this case, because the compiler already found something better than a branch.

This is not an isolated case. Under `-O2` and even `-O1`, modern compilers frequently turn simple conditional branches into `cmov`, bit operations, or mathematical formulas, reducing `[[likely]]` to a pure "code comment". The scenarios where you can actually see `[[likely]]` affect the code layout are usually: fairly long branch bodies (more than a few instructions), branches containing function calls or memory operations, or logic too complex for the compiler to replace with a `cmov`.

### When It's Worth Using

So `[[likely]]` is not a magic "add it and go faster" switch. The correct way to use it: first confirm through profiling (for example `perf stat -e branch-misses`) that a branch really has a high misprediction rate, and only then consider adding the hint. Before adding it, compare the assembly to confirm the compiler actually changed the code layout. If the assembly didn't change, the compiler has already optimized things in a better way, and `[[likely]]` is just redundant information noise.

Typical scenarios where it pays off include: error-check branches (normal path `[[likely]]`, error path `[[unlikely]]`), boundary-condition handling, and logic with branch bodies complex enough that the compiler can't substitute a `cmov`.

### Comparison with Compiler Builtins

Before `[[likely]]` existed, GCC/Clang used `__builtin_expect` for branch-prediction hints:

```cpp
// Old way
if (__builtin_expect(error == ErrorCode::Ok, 1)) {
    process_data();
}

// New way
if (error == ErrorCode::Ok) [[likely]] {
    process_data();
}
```

`[[likely]]` reads far better, and being a standardized attribute means it works on every compiler that supports C++20.

------

## [[no_unique_address]] (C++20): Empty Base Optimization

### The Problem: Even an Empty Class Takes 1 Byte

The C++ standard requires every complete object to have a unique address, which means even an "empty class" with no data members at all has a `sizeof` of at least 1. When you make an empty class a member of another class, it burns a byte for nothing:

```cpp
struct Empty {
    void foo() {}   // Only member functions, no data members
};

struct Container {
    // Common memory layout on x86-64:
    // offset   0: e       (1 byte)
    // offset 1~3: padding (3 bytes)
    // offset 4~7: x       (4 bytes)
    // Because int is 4 bytes, the compiler usually
    // places it at an address that is a multiple of 4
    Empty e;
    int x;
};

struct [[gnu::packed]] PackedContainer {
    // offset   0: e       (1 byte)
    // offset 1~4: x       (4 bytes)
    Empty e;
    int x;
};

static_assert(sizeof(Empty) == 1);
static_assert(sizeof(Container) == 8); // Almost certainly has padding
static_assert(sizeof(PackedContainer) == sizeof(int) + 1); // Tells the compiler not to add padding
```

For most applications, wasting 1 byte is nothing. But in generic programming, policy classes (allocators, mutex policies, and so on) are frequently empty classes. If several policy classes sit as members at the same time, each taking 1 byte, the total adds up before you notice. More critically, this makes `sizeof` come out different from what you'd expect, interfering with optimizations such as cache-line alignment.

### The Traditional EBO Solution

The traditional solution is Empty Base Optimization (EBO)—hold the empty class through inheritance instead of a member, so the compiler no longer needs to allocate separate space for it:

```cpp
struct Empty {};

// Traditional EBO: via inheritance
struct Container : private Empty {
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // Empty takes no space
```

But EBO has a few drawbacks: you can only inherit one empty base of the same type (you can't inherit two `Empty`s at once); inheritance is a very strong coupling relationship, and changing your inheritance structure just to save memory is unreasonable; and some coding standards forbid private inheritance.

### The [[no_unique_address]] Solution

`[[no_unique_address]]`, introduced in C++20, lets you achieve the same optimization through a member variable (instead of inheritance):

```cpp
struct Empty {
    void foo() {}
};

struct Container {
    [[no_unique_address]] Empty e;   // If Empty is an empty class, e takes no space
    int x;
};

static_assert(sizeof(Container) == sizeof(int));  // e is optimized away
```

### Application in Policy-Based Design

`[[no_unique_address]]` is especially useful in policy-based design. Suppose you have a container class that takes an allocator policy and a locking policy as template parameters. In a single-threaded scenario, the locking policy is an empty class (every method is a no-op), and you don't want it burning space for nothing:

```cpp
struct NullMutex {
    void lock() {}
    void unlock() {}
};

struct StdMutex {
    void lock()   { mtx_.lock(); }
    void unlock() { mtx_.unlock(); }
private:
    std::mutex mtx_;
};

template<typename T, typename Mutex = NullMutex>
class ThreadSafeBuffer {
public:
    void push(const T& item) {
        mutex_.lock();
        // ... add the element
        mutex_.unlock();
    }

private:
    [[no_unique_address]] Mutex mutex_;
    T* data_;
    std::size_t size_;
    std::size_t capacity_;
};

// Single-threaded version: NullMutex takes no space
ThreadSafeBuffer<int> single_thread_buf;
static_assert(sizeof(single_thread_buf) == sizeof(void*) + sizeof(std::size_t) * 2);

// Multi-threaded version: std::mutex takes real space
ThreadSafeBuffer<int, StdMutex> multi_thread_buf;
static_assert(sizeof(multi_thread_buf) == sizeof(std::mutex) + sizeof(void*) + sizeof(std::size_t) * 2);
```

This design lets you switch policies flexibly through template parameters without sacrificing memory efficiency: the single-threaded scenario doesn't waste a single byte, while the multi-threaded scenario uses a real mutex.

### Caveats

`[[no_unique_address]]` has a few details to watch. Multiple `[[no_unique_address]]` members of the same type may share the same address (they're all empty classes, so there's nothing to distinguish), and the exact behavior depends on the compiler implementation:

```cpp
struct A {
    [[no_unique_address]] Empty e1;
    [[no_unique_address]] Empty e2;
    int x;
};

A a;
// &a.e1 == &a.e2 can be true! (Not necessarily in GCC 15.2.1, but the first empty member may share an address with a later non-empty member)
```

> **Verified**: tested on GCC 15.2.1, multiple `[[no_unique_address]]` empty members do not necessarily share the same address, but the first empty member's address may be the same as a later non-empty member's. The `sizeof` savings are deterministic and significant.

If you need to take the addresses of these members or point references at them, be extra careful—their addresses may be identical. Besides, this attribute only works for empty classes. If the class has data members, adding it does nothing:

```cpp
struct NotEmpty { int data; };

struct Test {
    [[no_unique_address]] NotEmpty e;   // e still occupies sizeof(int)
    int x;
};
static_assert(sizeof(Test) == 2 * sizeof(int));
```

Also, some MSVC versions have bugs in their `[[no_unique_address]]` support—even empty classes may not get optimized. This needs special attention in cross-platform projects; it's recommended to verify the `sizeof` result on each target platform.

------

## [[assume]] (C++23): Compiler Assumptions

### Semantics

`[[assume(expression)]]`, introduced in C++23, tells the compiler "please assume `expression` is true", and the compiler can optimize more aggressively based on that assumption. If `expression` actually evaluates to false at runtime, the behavior is undefined.

This is different from `assert`. `assert` checks the condition at runtime and terminates the program on failure; `[[assume]]` does no runtime checking at all—it simply lets the compiler optimize with full confidence.

### Example

```cpp
int divide(int a, int b) {
    [[assume(b != 0)]];
    return a / b;
}
```

In this example, the compiler can in principle drop the divide-by-zero code path and generate a faster division. But if you pass `b == 0`, the consequences are undefined—it might crash, might return garbage, might look fine while quietly doing damage.

> **Verified**: at the `-O2` optimization level on GCC 15.2.1, a simple division function generates identical assembly with or without `[[assume]]`. For a scenario this simple, the compiler already optimizes well enough. The value of `[[assume]]` mainly shows up in more complex scenarios, where the compiler cannot infer the invariant through static analysis.

### Comparison with __builtin_assume

Before `[[assume]]`, MSVC used `__assume` and GCC used `__builtin_assume` (though the more common GCC idiom is `if (cond) __builtin_unreachable()`):

```cpp
// MSVC
__assume(b != 0);

// GCC
if (b == 0) __builtin_unreachable();

// The standard C++23 spelling
[[assume(b != 0)]];
```

### Use Cases

The typical use case for `[[assume]]` is: you have definitive knowledge about certain runtime conditions that the compiler cannot infer through static analysis. For example, you know an array access never goes out of bounds, or you know a pointer is never null:

```cpp
void process_array(int* data, std::size_t size) {
    [[assume(data != nullptr)]];
    [[assume(size > 0)]];

    for (std::size_t i = 0; i < size; ++i) {
        // The compiler can omit the null check and bounds check
        data[i] *= 2;
    }
}
```

A warning: `[[assume]]` is the most dangerous of all the attributes. If your assumption is wrong, the program's behavior is completely unpredictable. My recommendation is to use it only after thorough profiling has confirmed the bottleneck, and only when you can 100% guarantee the condition always holds. In 99% of code, you don't need it.

------

## C++20 [[nodiscard]] Enhancements

The previous chapter already mentioned that C++20 gave `[[nodiscard]]` the ability to carry a custom message. Here is a bit of supplementary explanation.

### nodiscard Extensions in the Standard Library

C++20 also extended where `[[nodiscard]]` is applied in the standard library. The following standard library functions are marked `[[nodiscard]]`:

- `std::vector::empty()` (since C++20)
- `std::string::empty()` (since C++20)

> **Verified**: tested with libstdc++ 15.2.1, the `empty()` method does indeed produce a nodiscard warning. But the article's claim that the `std::unique_ptr` and `std::shared_ptr` types themselves are marked `[[nodiscard]]` is not accurate in the current implementation—at least `std::make_unique()` and the constructors produce no warning. Different standard library implementations (libstdc++, libc++, MSVC STL) may differ in this support.

This means that if you write `vec.empty();` instead of `if (vec.empty())`, a C++20 compiler will warn you. This used to be a common source of bugs—`empty()` looks like it "empties" the container, but it actually "checks for emptiness". With `[[nodiscard]]`, misused code at least gets a warning.

```cpp
std::vector<int> vec = {1, 2, 3};

// Before C++20: return value unchecked, passes silently
vec.empty();  // Looks like it empties the container; actually does nothing

// C++20: the compiler emits a nodiscard warning
vec.empty();  // warning: ignoring return value of 'empty()'
```

### Using nodiscard Messages in Your Own Code

For library authors, `[[nodiscard("reason")]]` is extremely practical. The message can explain why the return value should not be ignored, and what the correct usage looks like:

```cpp
// Tell callers why the return value must be checked
[[nodiscard("Memory leak: returned pointer must be freed")]]
void* allocate_buffer(std::size_t size);

// Tell callers how to use it correctly
[[nodiscard("Store the lock_guard to keep the mutex locked")]]
std::unique_lock<std::mutex> acquire_lock();
```

------

## Comparison with the C++11-17 Attributes

Put the C++11-17 attributes and the new C++20-23 attributes side by side, and a clear trajectory emerges: the early attributes focus on code correctness and maintainability, the later ones focus more on performance optimization.

| Attribute | Version | Focus | Risk |
|------|------|--------|------|
| `[[noreturn]]` | C++11 | Correctness | Low |
| `[[carries_dependency]]` | C++11 | Performance | Low |
| `[[deprecated]]` | C++14 | Maintainability | Low |
| `[[nodiscard]]` | C++17 | Correctness | Low |
| `[[fallthrough]]` | C++17 | Correctness | Low |
| `[[maybe_unused]]` | C++17 | Readability | Low |
| `[[likely]]/[[unlikely]]` | C++20 | Performance | Low |
| `[[no_unique_address]]` | C++20 | Performance | Low |
| `[[assume]]` | C++23 | Performance | **High** |

Of these, only `[[assume]]` is truly "dangerous"—if the assumption is wrong, the consequence is undefined behavior. The other attributes, even when the "hint" is wrong, at worst cost slightly worse performance; they won't crash your program.

------

## Recommendations for Measuring the Performance Impact

For performance-oriented attributes like `[[likely]]`/`[[unlikely]]` and `[[assume]]`, my advice is: always measure after adding them. The optimization effect depends heavily on the specific hardware, compiler, and code context. Some scenarios show a clear gain; others show no difference at all.

The test method can be simple: use `perf stat` or `valgrind --tool=cachegrind` to compare instruction counts, branch-misprediction rates, and cache hit rates before and after adding the attribute. If the numbers don't improve significantly, it isn't worth adding—because attributes increase the code's "information density", making readers absorb one more concept.

For `[[no_unique_address]]`, verification is more direct—just look at the `sizeof` result. If the empty policy class really takes no space, the attribute is doing its job.

------

## References

- [cppreference: assume (C++23)](https://en.cppreference.com/w/cpp/language/attributes/assume)
- [cppreference: likely/unlikely (C++20)](https://en.cppreference.com/w/cpp/language/attributes/likely)
- [cppreference: no_unique_address (C++20)](https://en.cppreference.com/w/cpp/language/attributes/no_unique_address)
- [Don't use [[likely]] or [[unlikely]] - Aaron Ballman](https://blog.aaronballman.com/2020/08/dont-use-the-likely-or-unlikely-attributes/)
