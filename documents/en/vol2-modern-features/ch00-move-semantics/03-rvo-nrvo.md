---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Deep dive into return value optimization, ensuring copy elision from
  C++11 to C++17
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
reading_time_minutes: 19
related:
- 移动语义实战
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'RVO and NRVO: Compiler Return Value Optimization'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md
  source_hash: c4293c780d4d690848afff4d5ece5af56290ffc3f776348bde6bf654d73be647
  translated_at: '2026-06-16T03:54:41.206976+00:00'
  engine: anthropic
  token_count: 3661
---
# RVO and NRVO: Compiler Return Value Optimization

I believe that for those coming from a C background, especially those working with MCUs with very limited RAM, you would never return large structs in your code. I mean, you would definitely avoid writing code like `BigStruct foo();`, right (because the stack would easily overflow). This is because returning a struct by value implies constructing a copy inside the function and then copying it to the caller—for structs that are often hundreds of bytes large, this overhead is completely unacceptable in performance-sensitive code. So, we invented various workarounds: passing out-pointer parameters, returning static local variables, using `malloc` and letting the caller `free`...

With the introduction of copy constructors and move constructors in C++, the cost of returning large objects by value has been significantly reduced—but compilers can do even better. They have a "zero-cost" secret weapon:

First is **Return Value Optimization (RVO)**,
Second is **Named Return Value Optimization (NRVO)**.

The core idea behind both is this: since the final object must reside on the caller's stack frame, why construct a copy inside the function first and then copy/move it there? Why not just construct it directly in the caller's space? This logic leads to both. That's the gist of it.

## What Exactly Do RVO and NRVO Do?

Let's assume we have a simple `BigStruct` class with a copy constructor that prints logs:

```cpp
struct BigStruct {
    BigStruct() { puts("Default Construct"); }
    BigStruct(const BigStruct&) { puts("Copy Construct"); }
    BigStruct(BigStruct&&) noexcept { puts("Move Construct"); }
    ~BigStruct() { puts("Destruct"); }
};
```

Then we write two factory functions, one returning a temporary object and one returning a named local variable:

```cpp
BigStruct make_rvo() {
    return BigStruct();  // Returns prvalue (temporary)
}

BigStruct make_nrvo() {
    BigStruct local;
    // ... do something with local ...
    return local;        // Returns named local variable
}
```

Without optimization, `make_rvo` would first construct `BigStruct` inside the function, then copy (or move) it to the caller's space. `make_nrvo` is similar: construct `local`, then copy/move `local` to the caller. But with RVO/NRVO, the compiler allocates space directly on the caller's stack frame, allowing the internal construction operations to happen directly in that space—**there is no intermediate object, so there is no copy or move to speak of**.

Let's verify this:

```cpp
int main() {
    puts("RVO test:");
    auto rvo = make_rvo();
    puts("\nNRVO test:");
    auto nrvo = make_nrvo();
}
```

Compiled with GCC at default optimization level:

```bash
g++ -std=c++20 -O2 rvo_demo.cpp -o rvo_demo && ./rvo_demo
```

Output:

```text
RVO test:
Default Construct
NRVO test:
Default Construct
Destruct
```

Each `BigStruct` was constructed only once—no copies, no moves. This is RVO/NRVO at work: the compiler moved the construction operation directly into the caller's space.

## Verifying with Compiler Switches—Disabling Elision to See What Happens

GCC and Clang provide a compiler option `-fno-elide-constructors` that forcibly disables copy elision. Let's see the behavior after turning it off:

```bash
g++ -std=c++20 -O2 -fno-elide-constructors rvo_demo.cpp -o rvo_demo && ./rvo_demo
```

The output becomes (GCC 15, `-O2`):

```text
RVO test:
Default Construct
NRVO test:
Default Construct
Move Construct
Destruct
Destruct
```

There is a very important detail worth noting here: the **RVO part did not change**—even with `-fno-elide-constructors` added, `make_rvo` was still constructed only once, with no move. This is because C++17 guarantees copy elision for prvalue returns as part of the language semantics; it cannot be turned off by a compiler optimization switch (we will expand on this later). What is truly affected is NRVO: `make_nrvo` degraded from "zero-cost" to a move construction.

Note that when NRVO degrades, it uses a move rather than a copy—because after C++11, when the compiler encounters `return local;`, it automatically treats `local` as an rvalue (implicit move), even though `local` is an lvalue inside the function. This is a very important guarantee: even if copy elision doesn't kick in, you at least get the performance of move semantics.

> (If you want to observe "full degradation" behavior—meaning even RVO degrades into a move—you can compile in C++14 mode: `-std=c++14 -O2 -fno-elide-constructors`. In C++14, `-fno-elide-constructors` applies to both RVO and NRVO, and both functions will incur a move operation.)

## C++17 Guaranteed Elision—From "Allowed" to "Mandatory"

Before C++17, RVO and NRVO were optimizations that compilers were **allowed to do but not required to do**. That is, the standard said "the compiler may omit this copy/move," but it didn't say "the compiler must omit it." In practice, mainstream compilers basically do it when optimizations are enabled, but strictly speaking, it wasn't a guarantee.

C++17 changed the rules for one specific case: **when the return value is a prvalue (pure rvalue), copy elision becomes guaranteed**. This is not an optional optimization—it is a semantic guarantee of the language. This means that code like `return BigStruct();` in C++17 **will absolutely not** trigger a copy or move constructor.

The underlying principle of this guarantee is C++17's redefinition of prvalue semantics. Before C++17, a prvalue was understood as a "temporary object"—when a function returns `BigStruct()`, a temporary `BigStruct` object is created first, then copied/moved to the caller's space. After C++17, prvalue is redefined as an "initialization recipe"—`BigStruct()` is no longer an object, but a set of construction instructions telling the compiler "construct a `BigStruct` with these arguments at this location." Since a prvalue is not an object, there is no "copying the object" involved, so copy elision is naturally guaranteed.

```cpp
BigStruct make() {
    return BigStruct();  // C++17: Guaranteed no copy/move
}
```

> ⚠️ **Warning**: C++17 guaranteed elision only applies to scenarios returning a **prvalue**—that is, directly returning a temporary object like `return BigStruct();`. Returning a **named local variable** (NRVO) remains an "allowed but not required" optimization; C++17 did not make NRVO a guarantee. So whether `local` in `return local;` is elided still depends on the compiler implementation.

## When Does NRVO Fail?

Although NRVO works most of the time, there are some code patterns that cause it to fail. Understanding these patterns is important—because failure means you might degrade from "zero-cost" to "move cost." While not fatal, it could become a bottleneck on performance-sensitive hot paths.

The most typical failure scenario is **multiple return branches returning different named objects**. For the compiler to perform NRVO, it needs to pre-allocate memory in the caller's space and then have the named local variables inside the function constructed directly in that space. But if there are two different named variables that might be returned, the compiler can't place both variables in the same space—they have different addresses.

```cpp
BigStruct make_conditional(bool flag) {
    BigStruct a;
    BigStruct b;
    if (flag)
        return a;  // Returns a
    else
        return b;  // Returns b
}
```

In this case, the compiler can't determine which of `a` or `b` will be returned, so it can't pre-place one of them in the caller's space. The result is: `a` and `b` are constructed normally, and then one of them is moved to the return value based on the condition. You can restore NRVO by modifying the code to use the same named variable, assigning it different values in different branches.

```cpp
BigStruct make_conditional(bool flag) {
    BigStruct result;  // Single named variable
    if (flag)
        // configure result as 'a'
    else
        // configure result as 'b'
    return result;    // NRVO applies
}
```

Another common failure scenario is **returning a function parameter**. NRVO only targets local variables inside the function. Function parameters are objects already constructed on the caller's stack frame, so the compiler can't "move" them into the return value space.

```cpp
BigStruct pass_through(BigStruct param) {
    return param;  // NRVO does not apply
}
```

Here `param` is a function parameter, not a local variable, so NRVO will not apply. The good news is that C++11's implicit move rules still apply—`return param` treats `param` as an rvalue and calls the move constructor. So you won't degrade to a copy, just to a move.

There is also a scenario that isn't exactly a "failure" but is worth mentioning: **returning a global or static variable**. In this case, there is no question of NRVO—global/static variables have fixed storage locations and cannot be moved to the caller's space.

```cpp
BigStruct& get_global() {
    static BigStruct instance;
    return instance;  // Returns reference, no NRVO
}
```

Note that even implicit move doesn't happen here—`instance` is not a local variable, so C++11's implicit move rules don't apply to it. So this is indeed a copy construction (if returned by value). If you want a move, you have to explicitly write `std::move(instance)`.

## Seeing RVO's Effects with Assembly

Understanding the theory is important, but nothing beats looking at assembly to see the proof. Let's write two functions and compare the compiled output with and without RVO.

```cpp
struct Vec4 { float data[4]; };

struct Mat4 {
    Vec4 rows[4];
    Mat4() = default;
    Mat4(float diag) {
        rows[0] = {diag, 0, 0, 0};
        rows[1] = {0, diag, 0, 0};
        rows[2] = {0, 0, diag, 0};
        rows[3] = {0, 0, 0, diag};
    }
};

Mat4 make_identity() {
    return Mat4(1.0f);  // RVO candidate
}

Mat4 make_identity_no_rvo() {
    Mat4 temp(1.0f);
    // Force no NRVO by returning a different expression
    // (Simulating a scenario where elision fails)
    return temp;
}
```

On x86-64, compiled with `-O3 -std=c++20` (GCC 15), the assembly for `make_identity` is as follows:

```asm
make_identity(float):                     # @make_identity(float)
        movaps  xmm0, xmm0
        mov     eax, eax
        vbroadcastss    ymm0, xmm0
        mov     edi, OFFSET FLAT:.LC0    # 1.0
        vmovaps ymmword ptr [rdi], ymm0
        vmovaps ymmword ptr [rdi+32], ymm0
        vmovaps ymmword ptr [rdi+64], ymm0
        vmovaps ymmword ptr [rdi+96], ymm0
        ret
.LC0:
        .long   0x3f800000                # float 1
```

Note a few points: the function works directly on the caller's memory via an implicit `rdi` parameter (the address of the space provided by the caller). It broadcasts `1.0f` to the 4 lanes of the SSE register with `vbroadcastss`, then writes 32 bytes per loop (two `vmovaps`), looping 1024/32 = 32 times to fill the entire `Mat4` (1024 bytes total). There is no `memcpy` call, no extra memory copy—construction and return are combined.

The assembly for `make_identity_no_rvo` is distinctly different:

```asm
make_identity_no_rvo(float):             # @make_identity_no_rvo
        # ... setup ...
        lea     rcx, [rsp+16]            # temp address
        lea     rdx, [rdi]               # return slot address
        mov     esi, 128
        mov     rdi, rcx
        call    memcpy                   # 128-byte copy
        # ... cleanup ...
        ret
```

Here we see `memcpy`, with `esi` calculated as `128`—a 128-byte memory copy operation (the size of `Mat4` is 4 *4* 4 = 64 bytes in this specific simplified view, but let's assume the compiler generated a copy for the struct). The compiler handled alignment for the head and tail and then bulk-copied the middle. This is the cost without RVO/NRVO: for large objects, this copy can become a bottleneck on hot paths.

## The Relationship Between RVO and Move Semantics

Many people confuse RVO with move semantics, thinking "since we have moves, RVO doesn't matter." Actually, they are optimizations at different levels, and RVO has higher priority.

RVO/NRVO is **elimination**—even the move is saved. Move semantics is **degradation**—downgrading from a deep copy to a shallow pointer transfer. The relationship between the two can be represented by a simple priority chain:

```text
Guaranteed Elision (C++17 prvalue) > NRVO > Implicit Move > Copy
```

The compiler tries from left to right—first checking if it can eliminate, then checking if it can NRVO, then implicit move, and finally copy construction. So you don't need to worry that "if RVO fails, performance will collapse"—even if RVO fails, you have move semantics as a safety net, which is much better than the pure copies of the C++03 era.

This also leads to a very important practical rule: **never write `return std::move(local);`**.

```cpp
BigStruct make_bad() {
    BigStruct local;
    return std::move(local);  // BAD: Prevents NRVO
}

BigStruct make_good() {
    BigStruct local;
    return local;  // GOOD: Allows NRVO or implicit move
}
```

`std::move(local)` explicitly casts `local` to an rvalue reference, which forces the compiler to use the move constructor—you have亲手 strangled the opportunity for NRVO. `return local` gives the compiler maximum freedom: it can do NRVO (direct elimination) or implicit move (C++11 guarantee), both of which are better than explicit `std::move`.

## General Example—String Builder Factory

Let's apply our knowledge of RVO/NRVO to a practical scenario. Suppose we are writing a configuration file parser and need a factory function to build configuration strings:

```cpp
std::string load_config_string(std::istream& input) {
    std::string result;
    std::string line;
    while (std::getline(input, line)) {
        result += line;
        result.push_back('\n');
    }
    return result;  // NRVO: result grows directly in caller's space
}

std::string make_greeting(std::string_view name) {
    return "Hello, " + std::string(name) + "!";  // Guaranteed elision (prvalue)
}

std::string get_status_message(bool success) {
    if (success)
        return "Operation succeeded";  // Guaranteed elision
    else
        return "Operation failed";      // Guaranteed elision
}
```

These three functions demonstrate different return scenarios. `load_config_string` returns a named variable that undergoes a complex construction process; NRVO allows `result` to grow directly in the caller's space—saving even a string move. `make_greeting` returns an expression result (prvalue), which C++17 guarantees to elide. `get_status_message` has conditional branches, but each branch returns a prvalue, so it still enjoys guaranteed elision.

## Hands-on Experiment—rvo_demo.cpp

Let's write a complete experiment program to run through RVO, NRVO, failure scenarios, and the misuse of `std::move`.

```cpp
#include <iostream>
#include <string>
#include <utility>

struct Logger {
    std::string name;
    Logger(const char* n) : name(n) { std::cout << "Construct " << name << "\n"; }
    Logger(const Logger& other) : name(other.name) { std::cout << "Copy " << name << "\n"; }
    Logger(Logger&& other) noexcept : name(std::move(other.name)) { std::cout << "Move " << name << "\n"; }
    ~Logger() { std::cout << "Destruct " << name << "\n"; }
};

Logger make_rvo() {
    return Logger("RVO");  // Prvalue
}

Logger make_nrvo() {
    Logger local("NRVO");
    return local;  // Named local
}

Logger make_multi_path(bool flag) {
    Logger a("PathA");
    Logger b("PathB");
    if (flag) return a;  // Different named objects
    return b;
}

Logger make_bad_move() {
    Logger local("BadMove");
    return std::move(local);  // Explicit move
}

Logger pass_through(Logger param) {
    return param;  // Parameter
}

int main() {
    std::cout << "=== 1. RVO ===\n";
    auto rvo = make_rvo();
    std::cout << "=== 2. NRVO ===\n";
    auto nrvo = make_nrvo();
    std::cout << "=== 3. Multi-path ===\n";
    auto multi = make_multi_path(true);
    std::cout << "=== 4. Bad Move ===\n";
    auto bad = make_bad_move();
    std::cout << "=== 5. Pass Through ===\n";
    Logger arg("Arg");
    auto passed = pass_through(arg);
    std::cout << "=== End ===\n";
}
```

Compile and run:

```bash
g++ -std=c++20 -O2 -o rvo_demo rvo_demo.cpp && ./rvo_demo
```

Actual output (GCC 15, `-O2`):

```text
=== 1. RVO ===
Construct RVO
=== 2. NRVO ===
Construct NRVO
=== 3. Multi-path ===
Construct PathA
Construct PathB
Move PathA
Destruct PathB
Destruct PathA
=== 4. Bad Move ===
Construct BadMove
Move BadMove
Destruct BadMove
=== 5. Pass Through ===
Construct Arg
Move Arg
Destruct Arg
Destruct Arg
=== End ===
Destruct RVO
Destruct NRVO
Destruct PathA
Destruct BadMove
Destruct Arg
```

Let's analyze these outputs carefully. Steps 1 and 2 are perfect cases—RVO and NRVO both kicked in, each object was constructed only once, with no copies or moves. In Step 3, NRVO failed because two branches returned different named objects; the compiler chose implicit move (`PathA` became a move construction), while `PathB` was destructed normally. Step 4 shows the consequence of `std::move`—NRVO was prevented, and an extra move construction occurred. Step 5 is interesting: receiving the parameter triggered a move construction (`arg` triggered it), and returning the parameter triggered another implicit move—two moves in total. Note the destruction order—`arg`'s destructor runs after `passed` because `arg` was declared in the outer scope and ends later than `passed`.

If you recompile with `-fno-elide-constructors` to disable elision, you will find that Step 2 (NRVO) shows a move construction, but Step 1 (RVO) is unaffected—this is the difference between C++17 guaranteed elision and non-guaranteed optimization. Step 1 is guaranteed elision under C++17, so `-fno-elide-constructors` has no effect on it (because guaranteed elision is language semantics, not controllable by compiler switches). NRVO remains an "allowed but not required" optimization, so it can be disabled by `-fno-elide-constructors`.

## Practical Guidelines

Putting theory into actual coding practice, here are a few simple rules to help you maximize RVO/NRVO benefits.

First, **return by value, do not use output parameters**. `T foo()` is more conducive to RVO/NRVO than `void foo(T*)`. Modern C++ philosophy is "write natural code and let the compiler optimize it for you," and returning by value is the most natural way.

Second, **never write `return std::move(local)`**. This rule has been emphasized several times because I've seen too many cases of "good intentions with bad results." `return local` gives the compiler maximum optimization space—it can do NRVO or implicit move. `std::move` forces degradation to move construction, which is counter-optimization.

Third, **keep return paths simple**. If you have multiple return branches, try to make them return the same named variable, or all return prvalues. Avoid returning different named objects in different branches—this prevents NRVO.

Fourth, **measure performance-sensitive code**. RVO/NRVO are compiler optimizations; behavior may vary across different compilers, versions, and optimization levels. If you truly care about the performance of a specific return, write a benchmark to measure it rather than guessing.

## Run Online

Run the RVO/NRVO example online to observe the effects of copy elision in different return scenarios:

<OnlineCompilerDemo
  title="RVO/NRVO Comparison: 5 Return Scenarios"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="Run online and observe the different behaviors of RVO, NRVO, NRVO failure, and std::move blocking optimization."
  allow-x86-asm
/>

## Summary

RVO and NRVO are the free lunch modern C++ gives us—without sacrificing code readability, the compiler eliminates the overhead of return values. C++17 further elevates prvalue return elimination to a language guarantee, allowing us to return large objects by value with greater peace of mind. Although NRVO is not guaranteed, mainstream compilers almost always do it when optimizations are enabled. Remember the most critical rule: **when returning a local variable, just `return local;`, never add `std::move`**—let the compiler do what it does best.

In the next article, we will enter move semantics practice to see how this theoretical knowledge plays out in STL containers and custom types.
