---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: A deep dive into return value optimization, from C++11's optional elision
  up to C++17's guaranteed copy elision
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 19
related:
- 'Move Semantics in Practice: From STL to Custom Types'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: "RVO and NRVO: The Compiler's Return Value Optimization"
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md
  source_hash: 3e45a30ed062b8f299f625a5771a9c2e805983e3cac37ef30a1ed65fc9345f38
  translated_at: '2026-09-25T14:10:19+00:00'
  engine: anthropic
  token_count: 5500
---
# RVO and NRVO: The Compiler's Return Value Optimization

I've met plenty of friends who came from writing C—especially MCU C, on chips with laughably tiny RAM—who would never return a big struct, that is, never write anything like `struct X GetSth(...)` (one careless move and the stack is blown). Returning a struct by value means constructing one inside the function and then copying it out to the caller; for structs that easily run to several hundred bytes, that cost is completely unacceptable in performance-sensitive code. So back in the day people invented all sorts of workarounds: out parameters via pointers, returning static local variables, malloc'ing and leaving the free to the caller...

With copy constructors and move constructors in its toolbox, C++ has already slashed the cost of returning large objects by value—but the compiler can do even better. It holds "zero-cost" trump cards:

One is called **Return Value Optimization** (RVO),
the other is called **Named Return Value Optimization** (NRVO).

The idea behind both fits in one sentence: since the final object has to live in the caller's stack frame anyway, why construct one inside the function first and then copy/move it over? Why not just construct it directly in the caller's space? That's all there is to it.

## What RVO and NRVO Actually Do

Suppose we have a simple `Point` class with a copy constructor that prints a log line:

```cpp
#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        // I know putting Chinese text in here might cause trouble, but who cares—it's just a demo
        std::cout << "  构造 Point(" << x << ", " << y << ")\n";
    }

    Point(const Point& other) : x(other.x), y(other.y)
    {
        std::cout << "  拷贝 Point(" << x << ", " << y << ")\n";
    }

    Point(Point&& other) noexcept : x(other.x), y(other.y)
    {
        std::cout << "  移动 Point(" << x << ", " << y << ")\n";
    }
};
```

Then write two factory functions, one returning a temporary object and one returning a named local variable:

```cpp
// RVO case: returning a prvalue (a temporary object)
Point make_point_rvo(double x, double y)
{
    return Point(x, y);   // returns a temporary object
}

// NRVO case: returning a named local variable
Point make_point_nrvo(double x, double y)
{
    Point p(x, y);        // a named local variable
    // ... possibly a few more operations on p ...
    return p;             // returns the named variable
}
```

Without optimization, `make_point_rvo` first constructs `Point(x, y)` inside the function, then copies (or moves) it into the caller's space. `make_point_nrvo` is the same story: construct `p`, then copy/move `p` to the caller. With RVO/NRVO, however, the compiler allocates space directly in the caller's stack frame and lets the construction inside the function happen right in that space—no intermediate object exists at all, so neither copying nor moving ever comes into the picture.

Let's draw both cases as a diagram and compare:

![RVO/NRVO copy elision comparison](./03-rvo-nrvo-elision.drawio)

Let's verify:

```cpp
int main()
{
    std::cout << "=== RVO ===\n";
    Point a = make_point_rvo(1.0, 2.0);

    std::cout << "\n=== NRVO ===\n";
    Point b = make_point_nrvo(3.0, 4.0);

    return 0;
}
```

Compile with GCC at the default optimization level:

```bash
g++ -std=c++17 -Wall -Wextra -o rvo_test rvo_test.cpp
./rvo_test
```

Output:

```text
=== RVO ===
  构造 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
```

Each `Point` is constructed exactly once—no copy, no move. That's RVO/NRVO at work: the compiler "moved" the construction itself straight into the caller's space.

## Verifying with a Compiler Switch—Turn Off Elision and See What Happens

GCC and Clang provide a compiler flag, `-fno-elide-constructors`, that forcibly disables copy elision. Let's look at the behavior with it turned off:

```bash
g++ -std=c++17 -Wall -fno-elide-constructors -o rvo_no_elide rvo_test.cpp
./rvo_no_elide
```

The output becomes (GCC 16.1.1, `-std=c++17`):

```text
=== RVO ===
  构造 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
  移动 Point(3, 4)
```

There's a detail here worth noting: the RVO part **did not change**. Even with `-fno-elide-constructors` added, `make_point_rvo` still constructs only once, with no move. That's because C++17's copy elision for prvalue returns is a guarantee of the language semantics, not something a compiler optimization switch can turn off (we'll expand on this in detail later). What actually got affected is NRVO: `make_point_nrvo` degraded from "zero cost" to one move construction.

Note that NRVO, once degraded, falls back on a move rather than a copy. Since C++11, when the compiler encounters `return local_var;`, it automatically treats `local_var` as an rvalue (implicit move), even though `local_var` is an lvalue inside the function. This is a very important guarantee: even if copy elision doesn't kick in, you still get at least the performance of move semantics.

> (If you want to observe the "fully degraded" behavior—where even RVO degenerates into a move—compile in C++14 mode: `g++ -std=c++14 -fno-elide-constructors`. Under C++14, `-fno-elide-constructors` affects both RVO and NRVO, and both functions gain an extra move.)

## Guaranteed Elision in C++17—From Allowed to Mandatory

Before C++17, both RVO and NRVO were optimizations the compiler was **allowed but not required** to perform. The standard said "the compiler may omit this copy/move," but it never said "must omit." In practice, mainstream compilers do it almost universally once optimizations are enabled, but strictly speaking it was not a guarantee.

C++17 changed the rules for one of these cases: **when the returned value is a prvalue (a pure rvalue), copy elision becomes guaranteed**. This is not an optional optimization; it is a semantic guarantee of the language. In other words, code like `return Point(x, y);` will **absolutely never** invoke a copy or move constructor in C++17.

The principle underneath this guarantee is C++17's redefinition of prvalue semantics. Before C++17, a prvalue was understood as "a temporary object": when a function returned `Point(x, y)`, a temporary `Point` object was created first and then copied/moved into the caller's space. After C++17, a prvalue was redefined as "a recipe for initialization": `Point(x, y)` is no longer an object but a set of construction instructions telling the compiler "construct a `Point` at this location with these arguments." Since a prvalue is not an object, there is no such thing as "copying the object"—and so elision is naturally guaranteed.

```cpp
// Before C++17: Point(x,y) is a temporary object
// After C++17: Point(x,y) is a "construction recipe"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 guarantees no copy/move is triggered
}
```

C++17's guaranteed elision applies only to the **prvalue** return case—writing `return Type(args...);`, which directly returns a temporary. Returning a **named local variable** (NRVO) remains an "allowed but not required" optimization; C++17 did not make NRVO guaranteed as well. So whether the `p` in `return p;` gets elided still depends on the compiler's implementation.

## When NRVO Fails

NRVO works most of the time, but certain code patterns make it fail. Understanding these patterns matters: failing means you may drop from "zero cost" to "the cost of a move"—not fatal, but on a performance-sensitive hot path it can become a bottleneck.

The most typical failure is **multiple return branches returning different named objects**. To do NRVO, the compiler needs to pre-allocate memory in the caller's space and then construct the named variable inside the function directly in that space. But if two different named variables could be returned, the compiler cannot put both variables into the same slot—they each have their own address.

```cpp
Point bad_nrvo(bool flag)
{
    Point a(1.0, 2.0);
    Point b(3.0, 4.0);
    if (flag) {
        return a;   // may block NRVO
    }
    return b;       // returns a different named object
}
```

In this situation, the compiler cannot determine whether `a` or `b` will be returned, so it cannot place either one into the caller's space in advance. The result: `a` and `b` are constructed normally, then one of them is moved into the return value depending on the condition. You can restore NRVO by rewriting the code: use a single named variable and assign it different values in different branches.

```cpp
Point good_nrvo(bool flag)
{
    Point result(0.0, 0.0);
    if (flag) {
        result = Point(1.0, 2.0);
    } else {
        result = Point(3.0, 4.0);
    }
    return result;   // NRVO can take effect
}
```

Another common failure is **returning a function parameter**. NRVO targets only local variables inside the function; a parameter is an object already constructed in the caller's stack frame, and the compiler cannot "relocate" it into the return slot.

```cpp
Point return_param(Point p)
{
    // do a few operations on p ...
    return p;   // NRVO impossible, but C++11 implicitly moves
}
```

Here `p` is a function parameter, not a local variable, so NRVO won't happen. But the good news is that C++11's implicit move rule still applies: `return p;` treats `p` as an rvalue and calls the move constructor. So you don't degrade to a copy—just to a move.

There is one more scenario—not exactly a "failure," but worth mentioning: **returning a global or static variable**. There is no NRVO to speak of here at all: globals/statics have a fixed storage location and cannot be relocated into the caller's space.

```cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // copy construction: no NRVO, and no implicit move either
}
```

Note that not even an implicit move happens here: `global_point` is not a local variable, and C++11's implicit move rule does not apply to it. So this really is copy construction. If you want a move, you have to write `return std::move(global_point);` explicitly.

## Seeing RVO's Effect in Assembly

Understanding the theory matters, but nothing makes the point better than looking at the assembly directly. Let's write two functions and compare the compiler output with and without RVO.

```cpp
// rvo_asm.cpp -- view the assembly in Compiler Explorer
// Best viewed in full at https://godbolt.org

struct Heavy {
    int data[256];
    Heavy(int v) { for (auto& d : data) d = v; }
    Heavy(const Heavy& o) { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
    Heavy(Heavy&& o) noexcept { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
};

Heavy with_rvo(int v)
{
    return Heavy(v);     // C++17 guarantees elision
}

Heavy without_rvo(Heavy h)
{
    return h;            // returns a parameter; NRVO impossible
}
```

Compiled on x86-64 with `g++ -std=c++17 -O2` (GCC 16.1.1), the assembly of `with_rvo` is as follows:

```asm
// GCC 16.1.1, -O2 -std=c++17
with_rvo(int):
    movd    %esi, %xmm1         ; load parameter v into an SSE register
    movq    %rdi, %rax          ; rdi = the return-slot address provided by the caller
    leaq    1024(%rdi), %rdx    ; loop end address = start + 1024
    pshufd  $0, %xmm1, %xmm0   ; broadcast v to all 4 int lanes of xmm0
.L2:
    movups  %xmm0, (%rax)      ; each store writes 16 bytes
    addq    $32, %rax
    movups  %xmm0, -16(%rax)
    cmpq    %rdx, %rax
    jne     .L2
    movq    %rdi, %rax
    ret
```

A few things to note: the function works directly on the caller's memory through the hidden `rdi` parameter (the address of the space provided by the caller). It broadcasts `v` into the 4 lanes of an SSE register with `pshufd`, then writes 32 bytes per loop iteration (two `movups`), looping 1024/32 = 32 times to fill the entire `data[256]` (1024 bytes in total). No `memcpy` call, no extra memory copy—construction and return are fused into one.

The assembly of `without_rvo` is much shorter and much blunter:

```asm
// GCC 16.1.1, -O2 -std=c++17
without_rvo(Heavy):
    movl    $1024, %edx
    jmp     memcpy@PLT
```

Just two instructions: put 1024 (the byte count) into `%edx`, then tail-call `memcpy`. The compiler hands the entire "copy 1024 bytes" job to libc's `memcpy` instead of inlining the loop itself. That's the cost without RVO/NRVO: a very real 1024-byte memory copy (`int data[256]` is 256 × 4 = 1024 bytes), which for large objects can become a bottleneck on a hot path.

Incidentally, older GCC (15, say) would inline this copy as a `rep movsq` instruction, shuffling the bytes in a loop inside the function body; GCC 16 switched to calling `memcpy`. Different in form, identical in essence: it's still that 1024-byte copy—and with RVO it simply doesn't exist.

## How RVO Relates to Move Semantics

Quite a few people confuse RVO with move semantics, thinking "we have moves anyway, so RVO doesn't matter." In reality they are optimizations at different levels, and RVO has the higher priority.

RVO/NRVO is **elimination**—even the move is spared. Move semantics is **demotion**—from a deep copy down to a shallow pointer transfer. Their relationship can be expressed as a priority chain:

```text
Guaranteed elision (C++17 prvalue) > NRVO (compiler optimization) > Implicit move (C++11) > Copy construction
```

The compiler tries from left to right: first, can it elide? If not, can it NRVO? If that fails, implicit move; copy construction is the last resort. So don't panic over "RVO failed—does performance just collapse now?"—even if RVO fails, move semantics is there as the safety net, far better than the pure copies of the C++03 era.

This also leads to a vitally important practical rule: **never write `return std::move(local_var);`**.

```cpp
Heavy bad_idea()
{
    Heavy h(42);
    return std::move(h);  // blocks NRVO!
}

Heavy good_idea()
{
    Heavy h(42);
    return h;  // may trigger NRVO; failing that, implicit move
}
```

`return std::move(h);` explicitly converts `h` to an rvalue reference, which means the compiler must use the move constructor—the chance at NRVO is strangled by your own hand. `return h;`, by contrast, gives the compiler maximum freedom: it can do NRVO (direct elision), or it can do an implicit move (guaranteed since C++11)—either one beats an explicit `std::move`.

## A General Example—A String-Building Factory

Let's put our RVO/NRVO knowledge to work in a practical scenario. Suppose we're writing a configuration-file parser and need a factory function that builds configuration strings:

```cpp
#include <iostream>
#include <string>
#include <map>

using Config = std::map<std::string, std::string>;

/// @brief Convert a config map into a human-readable string
/// NRVO case: returns a named local variable
std::string format_config_nrvo(const Config& cfg)
{
    std::string result;
    result.reserve(256);  // preallocate to avoid repeated growth

    for (const auto& [key, value] : cfg) {
        result += key;
        result += " = ";
        result += value;
        result += "\n";
    }

    return result;  // NRVO: result is constructed directly in the caller's space
}

/// @brief Build a simple configuration line
/// RVO case: returns a prvalue
std::string make_config_line(const std::string& key, const std::string& value)
{
    return key + " = " + value + "\n";  // C++17 guarantees elision
}

/// @brief Conditional returns—an example where NRVO can fail
std::string format_with_default(
    const Config& cfg,
    const std::string& key,
    const std::string& default_value)
{
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        return it->first + " = " + it->second + "\n";  // prvalue, guaranteed elision
    }
    return key + " = " + default_value + " (default)\n";  // prvalue, guaranteed elision
}

int main()
{
    Config cfg = {
        {"host", "localhost"},
        {"port", "8080"},
        {"debug", "true"},
    };

    std::string formatted = format_config_nrvo(cfg);
    std::cout << formatted;

    std::string line = make_config_line("timeout", "30");
    std::cout << line;

    std::string fallback = format_with_default(cfg, "timeout", "60");
    std::cout << fallback;

    return 0;
}
```

These three functions each showcase a different return scenario. `format_config_nrvo` returns a named variable that went through a complex build process; NRVO lets `result` grow directly in the caller's space, sparing even a single string move. `make_config_line` returns the result of an expression (a prvalue), where C++17 guarantees elision. `format_with_default` has conditional branches, but every branch returns a prvalue, so it still enjoys guaranteed elision.

## Hands-On Experiment—rvo_demo.cpp

Let's write a complete experiment program and run through RVO, NRVO, the failure scenarios, and the misuse of `std::move` all at once.

```cpp
// rvo_demo.cpp -- a complete RVO / NRVO demonstration
// Standard: C++17

#include <iostream>
#include <string>
#include <utility>

class Tracker
{
    std::string name_;

public:
    explicit Tracker(std::string name) : name_(std::move(name))
    {
        std::cout << "  [" << name_ << "] 构造\n";
    }

    Tracker(const Tracker& other) : name_(other.name_ + "_copy")
    {
        std::cout << "  [" << name_ << "] 拷贝构造\n";
    }

    Tracker(Tracker&& other) noexcept : name_(std::move(other.name_))
    {
        other.name_ = "(moved-from)";
        std::cout << "  [" << name_ << "] 移动构造\n";
    }

    ~Tracker()
    {
        std::cout << "  [" << name_ << "] 析构\n";
    }

    const std::string& name() const { return name_; }
};

/// @brief RVO: returns a prvalue
Tracker make_rvo(const std::string& name)
{
    return Tracker(name + "_rvo");
}

/// @brief NRVO: returns a named local variable
Tracker make_nrvo(const std::string& name)
{
    Tracker t(name + "_nrvo");
    return t;
}

/// @brief NRVO failing: two return branches return different named objects
Tracker make_bad_nrvo(const std::string& name, bool flag)
{
    Tracker a(name + "_a");
    Tracker b(name + "_b");
    if (flag) {
        return a;
    }
    return b;
}

/// @brief A mistake: using std::move blocks NRVO
Tracker make_bad_move(const std::string& name)
{
    Tracker t(name + "_badmove");
    return std::move(t);   // explicit move, blocks NRVO
}

/// @brief Returning a function parameter—NRVO doesn't apply, but implicit move does
Tracker return_param(Tracker t)
{
    return t;
}

int main()
{
    std::cout << "=== 1. RVO（返回 prvalue）===\n";
    {
        auto a = make_rvo("A");
        std::cout << "  结果: " << a.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 2. NRVO（返回命名变量）===\n";
    {
        auto b = make_nrvo("B");
        std::cout << "  结果: " << b.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 3. NRVO 失效（不同命名对象）===\n";
    {
        auto c = make_bad_nrvo("C", true);
        std::cout << "  结果: " << c.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 4. 错误：std::move 阻止 NRVO ===\n";
    {
        auto d = make_bad_move("D");
        std::cout << "  结果: " << d.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 5. 返回参数（隐式移动）===\n";
    {
        Tracker param("E_param");
        auto e = return_param(std::move(param));
        std::cout << "  结果: " << e.name() << "\n";
    }
    std::cout << '\n';

    std::cout << "=== 程序结束 ===\n";
    return 0;
}
```

Compile and run:

```bash
g++ -std=c++17 -Wall -Wextra -O2 -o rvo_demo rvo_demo.cpp
./rvo_demo
```

Actual output (GCC 16.1.1, `-std=c++17 -O2`):

```text
=== 1. RVO（返回 prvalue）===
  [A_rvo] 构造
  结果: A_rvo
  [A_rvo] 析构

=== 2. NRVO（返回命名变量）===
  [B_nrvo] 构造
  结果: B_nrvo
  [B_nrvo] 析构

=== 3. NRVO 失效（不同命名对象）===
  [C_a] 构造
  [C_b] 构造
  [C_a] 移动构造
  [C_b] 析构
  [(moved-from)] 析构
  结果: C_a
  [C_a] 析构

=== 4. 错误：std::move 阻止 NRVO ===
  [D_badmove] 构造
  [D_badmove] 移动构造
  [(moved-from)] 析构
  结果: D_badmove
  [D_badmove] 析构

=== 5. 返回参数（隐式移动）===
  [E_param] 构造
  [E_param] 移动构造
  [E_param] 移动构造
  [(moved-from)] 析构
  结果: E_param
  [E_param] 析构
  [(moved-from)] 析构
```

Let's go through this output carefully. Steps 1 and 2 are the perfect cases: RVO and NRVO both kicked in, each object was constructed exactly once, and no copy or move happened. In step 3, NRVO failed because the two branches return different named objects; the compiler chose to implicitly move `a` (`C_a` became a move construction), while `b` was destroyed normally. Step 4 shows the consequence of `return std::move(t)`: NRVO is blocked and an extra move construction appears. The compiler will actually warn you about it, too: this code triggers the `-Wpessimizing-move` warning ("moving a local object in a return statement prevents copy elision"), spelling out plainly that the `std::move` here strangles the elision opportunity. Step 5 is the more interesting one: `return_param` incurs one move construction when receiving its argument (triggered by `std::move(param)`) and another implicit move when returning the parameter—two moves in total. Note the destruction order: `param` is destroyed after `e`, because `param` is declared in the outer scope, which ends later than `e`'s scope.

If you recompile with elision disabled via `-fno-elide-constructors`, you'll find that step 2 (NRVO) now shows a move construction, while step 1 (RVO) is unaffected—this is exactly the difference between C++17's guaranteed elision and a non-guaranteed optimization. Step 1 is guaranteed elision under C++17; `-fno-elide-constructors` has no power over it (guaranteed elision is language semantics, not something a compiler optimization switch controls). NRVO is still an "allowed but not required" optimization, so `-fno-elide-constructors` can turn it off.

## Practical Guidance

To land the theory in real code, here are a few simple rules that help you maximize the payoff of RVO/NRVO.

First, **return by value; don't use output parameters**. `std::string build_message()` is friendlier to RVO/NRVO than `void build_message(std::string& out)`. The philosophy of modern C++ is "write natural code and let the compiler optimize it for you," and returning by value is the most natural way to write it.

Second, **don't write `return std::move(local);`**. I've stressed this rule several times already, because I've seen far too many cases of good intentions going wrong. `return local;` gives the compiler the greatest room to optimize: it can do NRVO, or it can do an implicit move. `return std::move(local);` forcibly degrades things to a move construction—that's a pessimization.

Third, **keep return paths simple**. If there are multiple return branches, try to have them return the same named variable, or return prvalues in all of them. Avoid different branches returning different named objects—that blocks NRVO.

Fourth, **measure performance-sensitive code**. RVO/NRVO are compiler optimizations, and behavior may differ across compilers, versions, and optimization levels. If you truly care about the performance of a particular return, write a benchmark and measure it—don't guess.

## Run It Online

Run the RVO/NRVO examples online and observe how copy elision behaves across different return scenarios:

<OnlineCompilerDemo
  title="RVO/NRVO Comparison: 5 Return Scenarios"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="Run it online and observe the different behaviors of RVO, NRVO, failing NRVO, and std::move blocking the optimization."
  allow-x86-asm
/>
