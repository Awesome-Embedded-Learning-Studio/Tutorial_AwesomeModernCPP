---
chapter: 2
cpp_standard:
- 20
- 23
description: 'C++20 immediate functions and compile-time initialization: telling them apart from constexpr and choosing between them'
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'constexpr Basics: The Art of Compile-Time Evaluation'
- constexpr Constructors and Literal Types
reading_time_minutes: 15
related:
- constexpr Constructors and Literal Types
tags:
- host
- cpp-modern
- intermediate
- consteval
- constinit
- 编译期计算
title: 'consteval and constinit: New Tools for Compile-Time Guarantees'
translation:
  source: documents/vol2-modern-features/ch02-constexpr/03-consteval-constinit.md
  source_hash: d2738a2f08bfff4c40c014735aedd3da73422a9f89d88eca5e88845c18abe7fc
  translated_at: '2026-09-27T10:17:17+00:00'
  engine: anthropic
  token_count: 3600
---
# consteval and constinit: New Tools for Compile-Time Guarantees

Over the previous two articles we walked `constexpr` from variables to functions, and then on to constructors. The word that kept hanging on our lips was really just "can": what `constexpr` promises is "can be evaluated at compile time" — "must" was never part of the deal. When you declare a `constexpr` function, what you're saying is "this function is able to be evaluated at compile time"; the compiler never agreed to actually do so every single time.

The compiler is in fact even more proactive than we tend to assume. With optimizations on, even if the return value lands in a plain old variable, as long as the arguments are all constants and the function being called is simple enough, it may well compute the result at compile time. But the reverse situation is just as real: once the scenario gets complicated, or optimizations are turned off (say, `-O0`), the `constexpr` function can genuinely degrade into a runtime call. We can't pin down when it evaluates and when it doesn't, and the standard makes no hard promise here either. That lack of a guarantee is exactly the spot `consteval` sets out to fix.

Most of the time we welcome this elasticity: one piece of code that can take on both compile-time and runtime duty — who wouldn't love that? Some computations are different, though: for them your requirement is hard — they must finish executing at compile time, no way around it. Compile-time hashing and compile-time configuration validation, our most familiar friends, are typical members of this class. If they slip to runtime by accident, code review will almost never spot it; you only find out something went wrong once a performance profile or a runtime error rears its head.

What `consteval` does is have the compiler run a mandatory check, pushing the moment this class of problems surfaces all the way forward to the compilation stage. To this end, C++20 introduced two new keywords: functions declared `consteval` are called "immediate functions" — they must be evaluated at compile time. `constinit` governs static variables, guaranteeing that their initialization completes at compile time. Neither is a replacement for `constexpr`; they are better seen as filling two spots that `constexpr` cannot reach. With both keywords on the table, let's take them one at a time and look at each clearly.

## Step 1: consteval — Mandatory Compile-Time Evaluation

### Immediate Functions: From "Can" to "Must"

A function declared `consteval` goes by the name "immediate function" in the standard. Its semantics are blunt: every single call to this function must produce a compile-time constant. The moment the compiler finds a call whose context cannot complete evaluation at compile time, it reports the error on the spot — no room for negotiation whatsoever.

```cpp
consteval int square(int x)
{
    return x * x;
}

// OK: the argument is a constant, and the context is a constexpr variable initialization
constexpr int kResult = square(8);  // compiles, kResult == 64

// OK: the argument is a constant literal
int arr[square(5)];  // OK, square(5) == 25, used as the array size

// Error! The argument comes from runtime
int runtime_val = 42;
// int bad = square(runtime_val);  // compile error: not a constant expression
```

Now let's line up the `constexpr` version — the same function with only the keyword changed:

```cpp
constexpr int square_maybe(int x)
{
    return x * x;
}

int runtime_val = 42;
int ok = square_maybe(runtime_val);  // OK! Degrades into a runtime call
```

Put the two snippets side by side and the difference shows: a `constexpr` function handed a runtime argument quietly falls back to runtime execution, while a `consteval` function handed the same argument fails to compile, period. You can think of `consteval` as a hardened `constexpr`; the extra part is the mandatory compile-time guarantee — that "must" we keep talking about.

### Where consteval Fits

Which computations deserve `consteval`? Our yardstick for picking scenarios is plain: if this computation were done at runtime, it would be either pointless or risky. The three scenarios below all pass this simple test.

The first scenario is generating compile-time IDs and hashes. In protocol handling and command dispatch we often need to map strings to integer IDs; if the hashing is left to runtime, the CPU burns cycles on it for nothing, and compile-time conflict detection is lost along with it. FNV-1a is a simple hash algorithm commonly used for this kind of job — let's look at it directly:

```cpp
#include <cstdint>
#include <cstddef>

consteval std::uint32_t fnv1a32(const char* str, std::size_t len)
{
    std::uint32_t hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<std::uint8_t>(str[i]);
        hash *= 0x01000193u;
    }
    return hash;
}

template <std::size_t N>
consteval std::uint32_t command_id(const char (&s)[N])
{
    return fnv1a32(s, N - 1);
}

// All IDs are generated at compile time, with zero runtime cost
constexpr auto kIdStart = command_id("START");
constexpr auto kIdStop  = command_id("STOP");
constexpr auto kIdReset = command_id("RESET");

// Compile-time verification: ensure there are no hash collisions
static_assert(kIdStart != kIdStop);
static_assert(kIdStart != kIdReset);
static_assert(kIdStop != kIdReset);
```

Please direct your attention to the three `static_assert` lines at the end: the IDs are generated at compile time, and we went ahead and verified that they are pairwise distinct. If a hash collision really happened, compilation would halt right there — before the program has executed a single instruction.

Deeply satisfying! If the error slipped through to runtime instead, we'd be reverse-guessing our way out of a pile of weird symptoms.

The second scenario is compile-time configuration validation. When you need a configuration value to satisfy a constraint, we let the check run to completion at compile time — runtime doesn't even get a chance to "discover the config was wrong".

```cpp
consteval int validate_buffer_size(int size)
{
    // If the constraint is not satisfied, this is a direct compile error
    return size > 0 && size <= 4096 && (size & (size - 1)) == 0
        ? size
        : throw "Buffer size must be a power of 2 between 1 and 4096";
    // In a consteval context, throw causes a compile error
}

constexpr int kBufferSize = validate_buffer_size(1024);  // OK
// constexpr int kBadSize = validate_buffer_size(1000);  // compile error! Not a power of 2
```

The third scenario is compile-time type tags and metadata. When you want to embed a piece of compile-time information into the type system (say, peripheral descriptions or protocol field definitions), `consteval` guarantees they won't quietly turn into runtime objects.

```cpp
struct PeripheralTag {
    const char* name;
    std::uint32_t base_address;
    std::uint32_t clock_mask;

    consteval PeripheralTag(const char* n, std::uint32_t addr, std::uint32_t clk)
        : name(n), base_address(addr), clock_mask(clk) {}
};

consteval PeripheralTag make_usart1_tag()
{
    return PeripheralTag{"USART1", 0x40013800, 0x00004000};
}

constexpr auto kUsart1Tag = make_usart1_tag();
static_assert(kUsart1Tag.base_address == 0x40013800);
```

### Propagation Rules of consteval

`consteval` has one propagation rule that deserves your attention when you write composing code. When a `consteval` function gets called by an outer function, it places demands on that outer function. We have exactly two ways out: mark the outer function `consteval` as well, or make the call itself land in a constant-evaluation context. Touch neither end, and what awaits you is a compile error.

```cpp
consteval int forced_compile_time(int x) { return x * x; }

// Error! Calling a consteval function inside a constexpr function,
// but the result of that call is not a constant expression
constexpr int wrapper(int x)
{
    // return forced_compile_time(x);  // compile error
    return x * x;  // has to implement the logic itself
}

// OK: a consteval function may call another consteval function
consteval int double_square(int x)
{
    return forced_compile_time(x) * 2;
}

constexpr auto kVal = double_square(3);  // OK, kVal == 18
```

The rule was later loosened by one notch (by P2564R3 in C++23 — the "P plus a number" is a standard committee proposal number). We need to read the scope of that loosening carefully: the ones entitled to the relaxation are `constexpr` functions instantiated from templates, plus things like lambda call operators not marked `consteval`. When they call a `consteval` function, as long as the call ultimately lands in a constant-evaluation context, no error comes your way. The non-template `constexpr` functions we write ourselves are not on the list — the `wrapper` in the code above happens to be non-template; try uncommenting that line, and it still won't compile. The standard actually lists a third category too, functions synthesized by `= default`, but their bodies aren't written by you, so we seldom run into them. Combining `consteval` with `constexpr` inside templates got considerably more flexible from then on.

> The P2564R3 relaxation was later handled as a DR20: DR20 means "defect report", so its effect reaches back retroactively to C++20. From now on, whenever you see a number starting with DR, think "retroactive" and you'll have it right.

### if consteval: Compile-Time/Runtime Dispatch

C++23 also introduced `if consteval`, with the companion negative form `if !consteval`. It lets a function pick one of two paths based on "is this call currently in a constant-evaluation context" — let's see an example:

```cpp
#include <cstdio>
#include <cstddef>

constexpr std::size_t compute_hash(const char* str, std::size_t len)
{
    if consteval {
        // Compile-time path: use a pure constexpr algorithm
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        return hash;
    } else {
        // Runtime path: other implementation strategies are fair game here
        std::size_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < len; ++i) {
            hash ^= static_cast<std::size_t>(str[i]);
            hash *= 0x100000001b3ull;
        }
        // On the runtime path, if the compiler supports inline SIMD instructions,
        // it may auto-vectorize this loop; a SIMD library can also be called explicitly
        return hash;
    }
}

constexpr auto kCompileTimeHash = compute_hash("test", 4);  // takes the compile-time path
```

Let's be upfront about one thing: both paths in the example still use the same algorithm — the difference is left in the comments. In a real program, one side could swap in vectorization or some other optimization strategy. `if consteval` decides only "which path to take"; how the code on each path is written is none of its business.

We do need to keep `if consteval` and `if constexpr` apart — they look entirely too much alike. `if constexpr` looks at template parameters and settles on a branch at compile time. `if consteval` looks at "is this call inside constant evaluation". When you want to keep one implementation for compile time and another for runtime, the one you reach for is the latter.

## Step 2: constinit — Solving the Static Initialization Problem

### The Static Initialization Order Fiasco

Now it's `constinit`'s turn. We have to lay out the old problem it solves before we can understand what it does. Static storage duration objects in C++ — the global variables and `static` class members we write all the time — get initialized in two stages.

The first stage we call static initialization, and it covers two things: zero initialization and constant initialization. Both complete during the program loading phase, before `main` has even started running. The order is fixed as well: zero initialization first, constant initialization after.

The second stage, dynamic initialization, needs runtime code to participate. And that is exactly where the trouble we want to talk about lives: the order of dynamic initialization across translation units is unspecified. If you let a global object in `a.cpp` depend on the value of some global object in `b.cpp`, you may run headlong into the "static initialization order fiasco" — formally known as the Static Initialization Order Fiasco, or SIOF in shop talk, which is what we'll call it for the rest of this article.

```cpp
// a.cpp
#include <vector>
std::vector<int> g_data{1, 2, 3};  // Dynamic initialization: calls vector's constructor

// b.cpp
extern std::vector<int> g_data;
int g_first_element = g_data[0];  // May read g_data before it is initialized!
```

The most headache-inducing part of this class of bug is its instability: with some link orders everything is fine, swap to a different link order and it breaks — and of all things, it strikes precisely at that one instant of program startup. When you do run into it, the clues in your hands are few, and tracking it down is genuinely exhausting.

We turned the startup process under both link orders into an animation. You can press the step key to step through and see which comes first, the construction of g_data or the read in b:

<Anim id="constinit-siof" />

### Semantics of constinit

The semantics of `constinit` fit in one sentence: it goes on variable declarations with static or thread storage duration, and asserts that this variable must receive constant initialization. If the compiler finds that it needs dynamic initialization, it errors out without a second word. Let's look at three declarations — the ones that compile and the one that doesn't are all in there:

```cpp
#include <array>

// OK: aggregate initialization of std::array is constant initialization
constinit std::array<int, 4> g_table = {1, 2, 3, 4};

// OK: initialized with the return value of a constexpr function
constexpr int compute_value() { return 42; }
constinit int g_value = compute_value();

// Error! get_runtime_value is not a constant expression and needs dynamic initialization
// int get_runtime_value();
// constinit int g_bad = get_runtime_value();  // compile error
```

### What constinit and constexpr Each Promise

Both `constinit` and `constexpr` brush up against compile time, but the dimensions they promise differ. A `constexpr` variable demands two things: the value is settled at compile time, and the object itself is `const` — you cannot modify it. A `constinit` variable also demands that its initial value be settled at compile time, but the object itself stays modifiable.

```cpp
constexpr int kConstVal = 42;        // Compile-time value + not modifiable
// kConstVal = 100;                  // Error! A constexpr variable is const

constinit int gMutableVal = 42;      // Compile-time initialization + modifiable
gMutableVal = 100;                   // OK! The value can be changed at runtime
```

This difference looks small, but in real projects it is exactly what comes in handy. Take a global configuration buffer: you want its initial value pinned down at compile time, steering clear of SIOF, and once the program is up and running you also want to update its contents. When a requirement like that lands in your lap, `constinit` is there to catch it.

> One more note: `constinit` and `constexpr` are mutually exclusive — writing both in a single declaration doesn't fly. A `constexpr` variable already folds constant initialization and `const` semantics into itself; adding a `constinit` on top would be redundant.

### constinit and thread_local

`constinit` has another practical side effect: applied to a `thread_local` variable, it does away with the runtime initialization-check overhead. Let's look at the two side by side:

```cpp
// Without constinit: every access has to check whether the thread-local storage is initialized
thread_local int tl_counter = 42;

// With constinit: the compiler knows initialization completed at load time,
// so no runtime guard variable is needed
constinit thread_local int tl_fast_counter = 42;
```

An ordinary `thread_local` variable has to be checked once for "has it been initialized yet" on every access; to this end the compiler plants a guard variable behind the scenes, and the access may even carry atomic operations. With `constinit` added, the compiler knows this variable already has a settled initial value at program load, so in theory it can drop the runtime check. The actual gain, well, that depends on the compiler's implementation. I measured it on GCC 15.2.1 (`-O2`): the optimization came out to about 5%, which counts as fairly limited. When I saw that number, I reined my expectations back in — if you were hoping it saves you a big chunk, don't set your expectations too high. On a different compiler or in a different scenario, the improvement may be more noticeable.

### constinit in extern Declarations

`constinit` also works on non-initializing declarations, and `extern` declarations are the classic use: they tell the compiler that this variable has already been defined with `constinit` somewhere else, so the runtime initialization check is likewise waived. One line in the header and one in the source file:

```cpp
// header.h
extern constinit int g_shared_value;  // Tells users: this is constant-initialized

// source.cpp
#include "header.h"
constinit int g_shared_value = 100;   // The actual definition
```

This trick slots nicely into large projects. An `extern constinit` declaration in a header file makes things plain on its own: you, reading that line, know that this global variable's initialization behavior is settled, with no dynamic initialization mixed in.

## Step 3: Comparing the Three Keywords and Choosing Between Them

We have now met all three keywords, so let's line them up in one table. The comparison is also an animation: you can play it, pause it, or press the step key to move through it frame by frame, getting a clear view of what each of the three keywords governs:

<Anim id="consteval-constinit" />

| Feature | `constexpr` | `consteval` | `constinit` |
|------|-------------|-------------|-------------|
| Applies to | Variables, functions, constructors | Functions, constructors | Static/thread storage duration variables |
| Compile-time guarantee | "Can" be evaluated at compile time | "Must" be evaluated at compile time | Initialization must be constant initialization |
| Runtime behavior | May degrade into a runtime call | Runtime calls are not allowed | Variable may be modified at runtime |
| Mutability | Not modifiable (implicitly `const`) | N/A | Modifiable |
| Problem it solves | Flexibility of compile-time computation | Mandatory compile-time evaluation | Avoiding SIOF |

If you're asking how to choose, we can gather it all into one sentence. For a value that never changes, a `constexpr` variable is enough. For a function that absolutely must execute at compile time, we hand it to `consteval`. As for a global variable that must be initialized at compile time yet stay modifiable at runtime, what you write is `constinit`. For functions, our default is `constexpr` — it is the most flexible — and when the day comes that you truly need mandatory compile-time evaluation, upgrading to `consteval` is all it takes.

### Common Combination Patterns

In real projects we often use the three keywords in combination. Next, three common pairings.

Let's start with the first one: a `consteval` function generating `constexpr` values. Its call results are constant expressions by nature, so catching them with a `constexpr` variable fits perfectly.

```cpp
consteval std::uint32_t hash_string(const char* s)
{
    std::uint32_t h = 0x811c9dc5u;
    while (*s) {
        h ^= static_cast<std::uint8_t>(*s++);
        h *= 0x01000193u;
    }
    return h;
}

constexpr auto kHashStart = hash_string("START");  // Mandatory compile-time evaluation
constexpr auto kHashStop  = hash_string("STOP");
```

The second pairing down the list is a `constexpr` function combined with `constinit` global state. The function itself does not force compile-time evaluation, but the moment we use it to initialize a `constinit` variable, the compiler has to execute that call at compile time.

```cpp
constexpr int lookup_value(int index)
{
    constexpr int kTable[] = {10, 20, 30, 40, 50};
    return index >= 0 && index < 5 ? kTable[index] : 0;
}

constinit int g_first = lookup_value(0);   // Evaluated at compile time
constinit int g_third = lookup_value(2);   // Evaluated at compile time
```

The last pairing puts `consteval` to work on compile-time validation. With the validation function itself marked `consteval`, its execution is pinned to compile time. The job of producing the compile error can simply be handed to `static_assert`. If you'd rather have the error message bubble up straight from inside the function, a `throw` does it — a trick we already saw earlier in `validate_buffer_size`.

```cpp
consteval bool check_config(int baud_rate, int data_bits)
{
    if (baud_rate <= 0 || baud_rate > 4000000) return false;
    if (data_bits < 5 || data_bits > 9) return false;
    return true;
}

// Compile-time configuration validation with static_assert + a consteval function
static_assert(check_config(115200, 8), "Invalid UART config");
// static_assert(check_config(0, 8));  // compile error: validation fails
```

## A Few Spots Where Things Go Wrong

### A consteval Function's Address Cannot Be Used at Runtime

At runtime we cannot get a function pointer to a `consteval` function, and so we cannot call it either. Its address can be used at compile time — passed around inside a `consteval` context, for instance — but it cannot "escape" to runtime: take its address in a non-constant-evaluation context, and what awaits you is a compile error. The reason is actually plain: `consteval` functions have no runtime entity; they are fully expanded and inlined away at compile time.

### constinit Does Not Imply const

One spot is the easiest to mix up, so let's single it out. `constinit` speaks only to initialization: the initialization must be constant initialization, but the object itself need not be `const`. If what you want is a global variable that is initialized at compile time and also unmodifiable, just use `constexpr` — what you write comes out cleaner, too. `constinit const` does work, but there's no point taking the roundabout way.

### How consteval Interacts with Templates

Using `consteval` on function templates is fine, but there is one thing to keep in mind. If an instantiation of the template cannot meet `consteval`'s demands, the compiler will report an error — an internal call to a non-`constexpr` function, for instance, is a typical case. This bar differs from the one for `constexpr` function templates: a `constexpr` template only needs one set of arguments that works at compile time. In front of `consteval` there is no such slack: every call must complete at compile time.

## Run It Online

You can also run the consteval and constinit examples online and see C++20's compile-time guarantees with your own eyes:

<OnlineCompilerDemo
  title="consteval and constinit: C++20 Compile-Time Guarantees"
  source-path="code/examples/vol2/06_consteval_constinit.cpp"
  description="Run online and observe consteval's mandatory compile-time hashing and constinit's mutable global variables."
  allow-run
/>

## References

- [cppreference: consteval specifier (C++20)](https://en.cppreference.com/w/cpp/language/consteval)
- [cppreference: constinit specifier (C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- [C++ Stories: const vs constexpr vs consteval vs constinit in C++20](https://www.cppstories.com/2022/const-options-cpp20/)
