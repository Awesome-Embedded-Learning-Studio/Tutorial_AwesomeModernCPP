---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: A deep dive into return value optimization, from C++11 to C++17's guaranteed
  copy elision
difficulty: intermediate
order: 4
platform: host
prerequisites:
- 'Chapter 0: Move Construction and Move Assignment'
reading_time_minutes: 19
related:
- 'Move Semantics in Practice: Standard Library Containers and Performance Benchmarks'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
title: 'RVO and NRVO: The Compiler''s Return Value Optimization'
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/04-rvo-nrvo.md
  source_hash: 9124ec4c855666018a5a7f7e638eaa134d3e3dfbf5f77be538dc551d31bf230d
  translated_at: '2026-09-27T04:34:05+00:00'
  engine: anthropic
  token_count: 6000
---
# RVO and NRVO: The Compiler's Return Value Optimization

I've met plenty of friends who came over from writing C—especially MCU C, on chips with hilariously little RAM—who absolutely refuse to return big structs by value, that is, refuse to write anything like `struct X GetSth(...)` (one careless move and the stack is blown). Returning a struct by value means the function constructs one copy internally, then copies it again to hand it out—that's where the cost comes from. For structs that easily run to several hundred bytes, that is completely unacceptable in performance-sensitive code. So back in the day people invented all kinds of workarounds: **output pointer parameters, returning static local variables, malloc'ing and letting the caller free it themselves...**

Once C++ had copy construction and move construction, the cost of returning a large object by value dropped dramatically—but the compiler can do even better. We already flagged this at the end of the previous article: it can drive the cost of returning a large object all the way to zero. It has two tools for that. One is called **return value optimization** (Return Value Optimization), which we usually just call RVO. The other is called **named return value optimization** (Named Return Value Optimization), which we usually call NRVO.

Let's put both ideas into plain words: the final object has to land in the caller's stack frame anyway, so why should the function build another copy internally and then copy or move it over? Why not just construct it directly in the caller's space and be done with it? That's all there is to it<RefLink :id="1" preview="cppreference Copy elision — RVO / NRVO / mandatory elision in C++17" />.

> One quick note on scope: the prvalue-returning half (writing `return Point(x, y);` directly) was already verified for us by `make_tracker("D")` in the first article—the output contained exactly one construction log line. How much the compiler can save when returning a named local variable was the question we postponed back then, and answering it is exactly what this article is for.

## What RVO and NRVO Actually Do

Let's bring in a simple `Point` class whose constructor, copy constructor, and move constructor each print a log line, so you can spot at a glance who showed up in the output.

```cpp
#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        // I know embedding Chinese here might cause problems, but who cares, it's just a demo
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

Next we write two factory functions: one returns a temporary object, the other a named local variable.

```cpp
// RVO scenario: returning a prvalue (a temporary object)
Point make_point_rvo(double x, double y)
{
    return Point(x, y);   // Returns a temporary object
}

// NRVO scenario: returning a named local variable
Point make_point_nrvo(double x, double y)
{
    Point p(x, y);        // Named local variable
    // ... possibly some operations on p ...
    return p;             // Returns the named variable
}
```

Let's walk through the worst-case assumption: with no optimization at all, we construct the temporary object inside the function, then copy (or move) it into the caller's space—that is the `make_point_rvo` path. `make_point_nrvo` takes the same road: construct `p`, then copy or move `p` into the caller's space. And with RVO/NRVO? The compiler carves out the space directly in the caller's stack frame and lets the construction inside the function happen right in that memory. The intermediate object never exists at all, so there is nothing left to copy or move.

Let's draw the two cases and put them side by side:

![Comparing copy elision in RVO and NRVO](./04-rvo-nrvo-elision.drawio)

Now let's run the program:

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

Here is the complete verification program—we packed the `Point` class and both factory functions into one source file; click "Try It Yourself" and run it directly:

<OnlineCompilerDemo
  title="Try It Yourself: rvo_nrvo_probe.cpp"
  source-path="code/examples/vol2/17_rvo_nrvo_probe.cpp"
  description="Verify RVO and NRVO online: count how many log lines each section prints, and whether any copy or move shows up. In the next section you will change the compiler options and run it again."
  run-options="-std=c++17"
  allow-run
/>

Count the output: each `Point` prints exactly one construction log line, and not a single copy or move line appears. This is exactly where RVO/NRVO do their work—the compiler placed the construction directly in the caller's space.

## Verifying with a Compiler Switch—Turn Off Elision and See What Happens

GCC and Clang offer a compiler flag, `-fno-elide-constructors`, that we can use to forcibly disable copy elision. Go back to the demo above, open its compiler options, append `-fno-elide-constructors`, and run it again—the output changes at once: the `=== RVO ===` section does not budge an inch, still just the one construction log line. The `=== NRVO ===` section, on the other hand, grows an extra `移动 Point(3, 4)` line: with elision switched off, returning the named local variable falls back to one move construction.

One detail here is worth stopping for: the RVO half conspicuously **did not change**. Even with `-fno-elide-constructors` in place, `make_point_rvo` still constructs exactly once, and no move log line appears. Why doesn't the switch affect it? Because C++17 wrote prvalue-returning copy elision into the language semantics as a guarantee—the guaranteed copy elision we mentioned in the first article—not an optimization the compiler may switch on and off at will. What the switch really affects is NRVO: `make_point_nrvo` degrades from zero cost to one move construction. We'll unpack the full story of guaranteed elision in its own section later.

Notice one more thing: when NRVO degrades, the fallback path is a move, not a copy. That is the implicit move rule from C++11 at work: the compiler treats the `local_var` in `return local_var;` as an rvalue, even though inside the function it is unmistakably an lvalue. That is a very tangible guarantee: even when copy elision doesn't kick in, you still get move-semantics performance at minimum.

> If you want to see what "full degradation" looks like—RVO degrading into a move right alongside NRVO—compile in C++14 mode: `g++ -std=c++14 -fno-elide-constructors`. Under C++14 rules, the switch affects both RVO and NRVO elision, and both functions grow extra move operations.

## C++17's Guaranteed Elision—From "Allowed" to "Mandatory"

Before C++17 arrived, both RVO and NRVO were optimizations the compiler **was allowed to perform but not required to**. The standard's wording was "the compiler may omit this copy/move"; "must omit" was nowhere in it. In practice, mainstream compilers did it pretty much whenever optimizations were on—but let's keep our wording strict: that was convention, not guarantee.

C++17 made one of those cases mandatory: **when the return value is a prvalue (a pure rvalue), copy elision goes from "allowed" to "required"**<RefLink :id="2" preview="Richard Smith, P0135R1: Wording for guaranteed copy elision through simplified value categories, WG21, 2016" />. It is no longer an optional optimization but part of the language semantics. So a statement like `return Point(x, y);` in C++17 will **never** trigger the copy or move constructor—write it with confidence.

On what grounds can the standard promise that? We need to dig one level down and see what the semantics of a prvalue were redefined into. Wind the clock back to before C++17, and a prvalue was understood as "a temporary object": when a function returned `Point(x, y)`, a temporary `Point` had to be created and then copied or moved into the caller's space. In C++17, a prvalue became "a recipe for initialization": `Point(x, y)` is no longer an object at all but a set of construction instructions—what it tells the compiler is now "construct a `Point` at this location, with these arguments". Sit with that difference for a moment. Since a prvalue is not an object in the first place, there is no such thing as "copying the object"—you cannot copy something that does not exist; there is nothing to lay your hands on—so elision is inherently part of the guarantee.

```cpp
// Before C++17: Point(x,y) is a temporary object
// After C++17: Point(x,y) is a "construction recipe"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 guarantees no copy/move is triggered
}
```

But let's draw the applicability boundary clearly: guaranteed elision only covers the prvalue-returning scenario—writing `return Type(args...);` and returning a temporary directly. NRVO, which returns a named local variable, remains an "allowed but not required" optimization in C++17. So whether the `p` in `return p;` actually gets elided still depends on the compiler's implementation. As for the situations in which NRVO fails, let's keep pulling that thread.

## When NRVO Fails

NRVO kicks in most of the time, but certain code patterns just make it fail. These patterns are worth learning to recognize: a failure means you may degrade from zero cost to the cost of one move. A failure isn't fatal, but stack it up repetition after repetition on a performance-sensitive hot path, and it can accumulate into a bottleneck.

The most typical failure scenario is **multiple return branches returning different named objects**. Recall what NRVO needs to do: allocate the memory in the caller's space up front, so the named variable inside the function constructs directly into that memory. But if both `a` and `b` are candidates for return, the compiler is stuck: each has its own address, and neither can be squeezed into the space the caller reserved for the return value.

```cpp
Point bad_nrvo(bool flag)
{
    Point a(1.0, 2.0);
    Point b(3.0, 4.0);
    if (flag) {
        return a;   // May block NRVO
    }
    return b;       // Returns a different named object
}
```

Since the compiler cannot decide which of `a` and `b` will be returned, it cannot place either one in the caller's space ahead of time. The result: `a` and `b` both construct normally, one of them is moved out conditionally at return, and the return value ends up holding the move-constructed result. There is a way to restore NRVO, too: rewrite the function with a single named variable and give it different values in different branches.

```cpp
Point good_nrvo(bool flag)
{
    Point result(0.0, 0.0);
    if (flag) {
        result = Point(1.0, 2.0);
    } else {
        result = Point(3.0, 4.0);
    }
    return result;   // NRVO can kick in
}
```

Another common failure scenario is **returning a function parameter**. NRVO only recognizes local variables inside the function; a parameter arrives already constructed, and the compiler cannot "relocate" it into the return value's space.

```cpp
Point return_param(Point p)
{
    // Do some operations on p ...
    return p;   // No NRVO, but C++11 applies the implicit move
}
```

`p`'s identity is a parameter, not a local variable, so NRVO will not fire for it. But here is the good news: C++11's implicit move rule still applies—the `p` in `return p;` is treated as an rvalue, and what gets called is still the move constructor. So your degradation lands on a move, not a copy.

One more scenario worth acknowledging—strictly speaking not a failure, but let's count it while we're at it: **returning a global or static variable**. Globals and statics have fixed storage locations and can never be moved into the caller's space, so NRVO never had a role to play here in the first place.

```cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // Copy construction—no NRVO and no implicit move either
}
```

And guess what: this time even the implicit-move treatment is off the table. `global_point` is not a local variable either, so C++11's implicit move rule does not reach it, and returning it really does go through copy construction. If you truly want to move it, you have to write `return std::move(global_point);` explicitly.

## Seeing RVO's Effect in the Assembly

With the reasoning covered, let's add one more level of evidence: the assembly itself. We'll write two functions—one that gets RVO's benefits and one that doesn't—and put their compiled output side by side.

```cpp
// rvo_asm.cpp -- view the assembly on Compiler Explorer
// Full assembly best viewed at https://godbolt.org

struct Heavy {
    int data[256];
    Heavy(int v) { for (auto& d : data) d = v; }
    Heavy(const Heavy& o) { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
    Heavy(Heavy&& o) noexcept { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
};

Heavy with_rvo(int v)
{
    return Heavy(v);     // C++17 guaranteed elision
}

Heavy without_rvo(Heavy h)
{
    return h;            // Returning a parameter—no NRVO possible
}
```

Compiled on x86-64 with `g++ -std=c++17 -O2` (GCC 16.1.1), `with_rvo`'s assembly looks like this:

```asm
// GCC 16.1.1, -O2 -std=c++17
with_rvo(int):
    movd    %esi, %xmm1         ; parameter v loaded into an SSE register
    movq    %rdi, %rax          ; rdi = return-value address provided by the caller
    leaq    1024(%rdi), %rdx    ; loop end address = start + 1024
    pshufd  $0, %xmm1, %xmm0   ; broadcast v to all 4 int lanes of xmm0
.L2:
    movups  %xmm0, (%rax)      ; writes 16 bytes at a time
    addq    $32, %rax
    movups  %xmm0, -16(%rax)
    cmpq    %rdx, %rax
    jne     .L2
    movq    %rdi, %rax
    ret
```

Let's read it top to bottom. In `movq %rdi, %rax`, `rdi` is a hidden parameter holding the address of the space the caller prepared for the return value, while the actual argument `v` travels in `%esi`. With the address in hand, the function gets to work directly on the caller's memory.

Now the loop. `pshufd $0, %xmm1, %xmm0` broadcasts `v` to all 4 lanes of the SSE register, so one register now holds 4 identical `int`s. In the loop body, each turn of `.L2` writes 32 bytes: two `movups` instructions write 16 bytes each, and `addq $32, %rax` nudges the pointer forward one step. The endpoint is set by `leaq 1024(%rdi), %rdx` at the start address plus 1024. Divide 1024 by 32 and you get exactly 32 iterations—that is how the 1024 bytes of `data[256]` get filled in. Nowhere in the function is there a `memcpy` call, nor any extra memory copy. Construction and return have merged into one.

Now look at `without_rvo`; its assembly is much shorter and much more blunt:

```asm
// GCC 16.1.1, -O2 -std=c++17
without_rvo(Heavy):
    movl    $1024, %edx
    jmp     memcpy@PLT
```

Count the instructions and you'll find there are exactly two: `movl $1024, %edx` loads the byte count into `%edx`, and `jmp memcpy@PLT` does a tail call to `memcpy`. The compiler handed the whole "copy 1024 bytes" job to libc and washed its hands of the rest. Without RVO/NRVO, there's the cost in plain sight: one very real 1024-byte memory copy. Let's do the math on `int data[256]`: 256 × 4 = 1024 bytes. Put a return like that on a hot path, and it really can become the bottleneck.

> Earlier GCC (15, for instance) did things differently from what we just saw: the compiler back then inlined this copy into a `rep movsq` instruction and looped the bytes over right inside the function body. GCC 16 switched to calling `memcpy`—the form changed, but the substance didn't: it is still those 1024 bytes being moved. With RVO, that 1024-byte transfer never happens at all.

## How RVO Relates to Move Semantics

Plenty of people around us conflate RVO with move semantics—"we have moves anyway, so RVO doesn't matter." Let's draw the line clearly: what RVO/NRVO does is **elision**—it saves even the move step. What move semantics does is **downgrading**—it turns a deep copy into a shallow pointer transfer. Elision's benefit outranks downgrading's, and the two can be lined up into a chain:

```text
Guaranteed elision (C++17 prvalue) > NRVO (compiler optimization) > implicit move (C++11) > copy construction
```

The compiler picks along the chain from left to right: if it can elide, it elides outright; if elision isn't possible, it tries NRVO; failing that, it falls back to implicit move; and the last resort, only when nothing else works, is copy construction. So no panic: even if RVO fails, move semantics has your back behind it—far better than the pure copies of the C++03 era.

Following the chain further leads to one of the most important rules in practice: **never write `return std::move(local_var);`**.

```cpp
Heavy bad_idea()
{
    Heavy h(42);
    return std::move(h);  // Blocks NRVO!
}

Heavy good_idea()
{
    Heavy h(42);
    return h;  // May trigger NRVO; the fallback is still implicit move
}
```

What `return std::move(h);` does is explicitly convert `h` to an rvalue reference, and the compiler reads that loud and clear: the only road left is move construction—you personally strangled NRVO's chance. Write `return h;` instead, and we've left it plenty of room: two roads are open, either NRVO with direct elision, or the C++11-guaranteed implicit move. Whichever it picks beats making the decision for it.

## A General Example—A String-Building Factory

With all the concepts in hand, let's practice in a realistic scenario. Suppose you are writing a configuration-file parser and need a factory function to build configuration strings:

```cpp
#include <iostream>
#include <string>
#include <map>

using Config = std::map<std::string, std::string>;

/// @brief Convert a configuration map into a readable string
/// NRVO scenario: returning a named local variable
std::string format_config_nrvo(const Config& cfg)
{
    std::string result;
    result.reserve(256);  // Pre-allocate to avoid repeated growth

    for (const auto& [key, value] : cfg) {
        result += key;
        result += " = ";
        result += value;
        result += "\n";
    }

    return result;  // NRVO: result constructs directly in the caller's space
}

/// @brief Build a simple configuration line
/// RVO scenario: returning a prvalue
std::string make_config_line(const std::string& key, const std::string& value)
{
    return key + " = " + value + "\n";  // C++17 guaranteed elision
}

/// @brief Conditional return—an example where NRVO may fail
std::string format_with_default(
    const Config& cfg,
    const std::string& key,
    const std::string& default_value)
{
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        return it->first + " = " + it->second + "\n";  // prvalue—guaranteed elision
    }
    return key + " = " + default_value + " (default)\n";  // prvalue—guaranteed elision
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

Let's take them one by one. `format_config_nrvo` returns a named variable that went through a complex build process; NRVO lets `result` grow directly in the caller's space, saving even a single string move. `make_config_line` returns the result of an expression (a prvalue), and C++17 gives the guarantee. `format_with_default` does have conditional branches, but every branch hands out a prvalue, so guaranteed elision still applies.

## Hands-On Experiment—rvo_demo.cpp

To finish, let's run a complete experiment program—RVO, NRVO, the failure scenarios, and the misuse of `std::move`, all in one viewing.

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

/// @brief RVO: returning a prvalue
Tracker make_rvo(const std::string& name)
{
    return Tracker(name + "_rvo");
}

/// @brief NRVO: returning a named local variable
Tracker make_nrvo(const std::string& name)
{
    Tracker t(name + "_nrvo");
    return t;
}

/// @brief Failed NRVO: two return branches returning different named objects
Tracker make_bad_nrvo(const std::string& name, bool flag)
{
    Tracker a(name + "_a");
    Tracker b(name + "_b");
    if (flag) {
        return a;
    }
    return b;
}

/// @brief Cautionary example: using std::move blocks NRVO
Tracker make_bad_move(const std::string& name)
{
    Tracker t(name + "_badmove");
    return std::move(t);   // Explicit move—blocks NRVO
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

All five scenarios live in one program—run it, and let's check against the output step by step:

<OnlineCompilerDemo
  title="Hands-On Experiment: rvo_demo.cpp"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="Run online and observe the different behaviors of RVO, NRVO, failed NRVO, and std::move blocking the optimization."
  run-options="-O2 -std=c++17"
  allow-run
  allow-x86-asm
/>

Let's go segment by segment. Steps 1 and 2 are the most comfortable cases: RVO and NRVO both kicked in, each object was constructed exactly once, and not one copy or move log line appears.

Step 3: NRVO failed, for exactly the reason we dissected earlier—two branches returning different named objects. The compiler chose to implicitly move `a`; in the output you can see `C_a` turning into a move construction, while `b` gets destructed normally.

Step 4 shows the consequence of `return std::move(t)`: NRVO is blocked, and one move construction is wasted for nothing. The compiler will actually remind you—it triggers the `-Wpessimizing-move` warning ("moving a local object in a return statement prevents copy elision")<RefLink :id="3" preview="GCC Warning Options — -Wpessimizing-move and -Wredundant-move" />, saying outright that the `std::move` here strangled the elision opportunity. In GCC this warning is off by default; you have to add `-Wall` to see it.

We find step 5 the most interesting. When `return_param` receives its parameter, one move construction happens (triggered by `std::move(param)`); when it returns the parameter, another implicit move happens—two moves in total. Now look at the destruction order: `param` and `e` actually live in the same block, with `param` the one constructed earlier, so on leaving the block they destruct in reverse construction order, putting `param`'s destructor after `e`'s.

Recompile with `-fno-elide-constructors` to switch elision off, and you'll see step 2 grow one move construction while step 1 doesn't change one bit. This time you've touched the dividing line with your own hands: guaranteed elision lives at the level of language semantics and no switch can turn it off, while NRVO is a compiler-provided optimization that the switch does control.

## Practical Guidance

With the theory all grounded, let's collect a few practices you can apply directly.

Return by value; skip output parameters. `std::string build_message()` is friendlier to RVO/NRVO than `void build_message(std::string& out)`. The modern C++ way is "write natural code and let the compiler optimize it for you", and returning by value is the most natural way to write it.

We already dissected the consequences of writing `return std::move(local);`, but I've seen far too many cases of good intentions paving the road to a slowdown, so it's worth nagging about once more. When you write `return local;`, you leave the compiler maximum freedom—elision or implicit move, its pick. Writing `std::move` is an anti-optimization: you've forcibly compressed the road down to move construction.

Keep the return path simple: if a function has multiple return branches, try to have them all return the same named variable, or all return prvalues. Different branches returning different named objects, and NRVO is out of the game.

For performance-sensitive code, measure before you conclude. RVO/NRVO are, in the end, compiler optimizations—change the compiler, the version, or the optimization level, and the behavior you get may change too. If you genuinely care about the performance of a particular return, write a benchmark and measure it; don't leave the verdict to guesswork.

In the next article we tackle the twistiest part of move semantics—universal references and perfect forwarding: why `T&&` becomes "universal" inside templates, and how `std::forward` passes the lvalue/rvalue identity downstream unchanged.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    author="cppreference.com"
    title="Copy Elision"
    chapter="NRVO; Mandatory Elision (C++17)"
    url="https://en.cppreference.com/w/cpp/language/copy_elision"
  />
  <ReferenceItem
    :id="2"
    author="Richard Smith"
    title="P0135R1: Wording for Guaranteed Copy Elision through Simplified Value Categories"
    publisher="WG21 / ISO C++ Committee"
    :year="2016"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2016/p0135r1.html"
  />
  <ReferenceItem
    :id="3"
    author="GCC"
    title="Warning Options (-Wpessimizing-move)"
    publisher="gcc.gnu.org"
    url="https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html"
  />
</ReferenceCard>
