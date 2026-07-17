---
chapter: 0
cpp_standard:
- 11
- 14
- 17
description: Deep dive into return value optimization, from C++11 optional to C++17 guaranteed copy elision
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
  source_hash: e8ac613f490758199917c449044366ba2d7e9e2cc0925d3bc8818749f8fea714
  translated_at: '2026-07-16T00:00:00+00:00'
  engine: manual
  token_count: 3700
---
# RVO and NRVO: Compiler Return Value Optimization


I've seen quite a few folks coming from C, especially MCU C, from chips with laughably little RAM, who would never return a large struct in their code, never write anything like `struct X GetSth(...)` (the stack blows up before you notice). Returning a struct by value means constructing one inside the function and then copying it to the caller; for structs that run hundreds of bytes, that cost is flat-out unacceptable in performance-sensitive code. So back in the day people invented all kinds of workarounds: out-pointer parameters, returning static locals, `malloc`-ing and letting the caller `free`...

Once C++ had copy and move constructors, the cost of returning large objects by value dropped a lot, but the compiler can do even better. It holds a "zero-cost" card:

One is **Return Value Optimization** (RVO),
the other is **Named Return Value Optimization** (NRVO).

The idea behind both is one sentence: since the final object has to live in the caller's stack frame, why construct one inside the function and then copy/move it over? Just construct it directly in the caller's space and be done. That's the whole point.

## What RVO and NRVO Actually Do

Say we have a simple `Point` class with a copy constructor that logs:

```cpp
#include <iostream>

struct Point {
    double x, y;

    Point(double x, double y) : x(x), y(y)
    {
        // 我知道好像在这里塞中文可能会造成问题，但是怕啥，demo而已
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

Then we write two factory functions, one returning a temporary, one returning a named local:

```cpp
// RVO case: return a prvalue (temporary)
Point make_point_rvo(double x, double y)
{
    return Point(x, y);   // returns a temporary
}

// NRVO case: return a named local variable
Point make_point_nrvo(double x, double y)
{
    Point p(x, y);        // named local
    // ... maybe some operations on p ...
    return p;             // return the named variable
}
```

Without optimization, `make_point_rvo` first constructs `Point(x, y)` inside the function, then copies (or moves) it into the caller's space. `make_point_nrvo` is the same: construct `p`, then copy/move `p` to the caller. With RVO/NRVO, the compiler allocates space directly in the caller's stack frame and lets the construction inside the function happen right there. There is no intermediate object at all, so there's nothing to copy or move.

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

Compiled with GCC at the default optimization level:

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

Each `Point` is constructed exactly once, no copy, no move. That's RVO/NRVO at work: the compiler "moved" the construction straight into the caller's space.

## Verifying with a Compiler Flag: Turn Off Elision and See

GCC and Clang offer `-fno-elide-constructors` to force copy elision off. Let's see what happens then:

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

One detail to notice: the RVO part **does not change**. Even with `-fno-elide-constructors`, `make_point_rvo` still constructs only once, with no move. That's because C++17's copy elision for prvalue returns is a language semantic guarantee, not something a compiler optimization flag can turn off (more on this later). What actually gets affected is NRVO: `make_point_nrvo` degrades from "zero cost" to one move construction.

Note that NRVO's fallback is a move, not a copy. Since C++11, when the compiler sees `return local_var;`, it automatically treats `local_var` as an rvalue (an implicit move), even though `local_var` is an lvalue inside the function. That's an important guarantee: even when elision doesn't kick in, you still get move-semantics performance.

> (If you want to see "full degradation", where even RVO falls back to a move, compile in C++14 mode: `g++ -std=c++14 -fno-elide-constructors`. Under C++14, `-fno-elide-constructors` applies to both RVO and NRVO, so both functions pick up extra moves.)

## C++17 Guaranteed Elision: From "Allowed" to "Mandatory"

Before C++17, both RVO and NRVO were optimizations the compiler **was allowed but not required** to perform. The standard said "the compiler may omit this copy/move", not "must omit". In practice, mainstream compilers basically always did it with optimizations on, but strictly speaking it wasn't guaranteed.

C++17 changed the rule for one case: **when the return value is a prvalue (pure rvalue), copy elision becomes guaranteed**. This isn't an optional optimization, it's a language semantic guarantee. In other words, `return Point(x, y);` **absolutely will not** trigger a copy or move constructor in C++17.

The reason is C++17's redefinition of prvalue semantics. Before C++17, a prvalue was understood as "a temporary object": when a function returned `Point(x, y)`, it first created a temporary `Point`, then copied/moved it into the caller's space. After C++17, a prvalue was redefined as "a recipe for initialization": `Point(x, y)` is no longer an object but a set of construction instructions telling the compiler "construct a `Point` here with these arguments". Since a prvalue isn't an object, there's no "copying an object" to speak of, so elision is naturally guaranteed.

```cpp
// Before C++17: Point(x,y) is a temporary object
// After C++17: Point(x,y) is a "construction recipe"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 guarantees no copy/move
}
```

> ⚠️ **Pitfall warning**: C++17's guaranteed elision only applies when returning a **prvalue**, the `return Type(args...);` form that directly returns a temporary. Returning a **named local variable** (NRVO) is still an "allowed but not required" optimization; C++17 did not make NRVO guaranteed. So whether the `p` in `return p;` gets elided still depends on the compiler.

## When NRVO Fails

NRVO works most of the time, but some code patterns break it. Understanding these patterns matters: a failure means you may drop from "zero cost" to "move cost". Not fatal, but on a performance-sensitive hot path it can become a bottleneck.

The most typical failure is **multiple return branches returning different named objects**. For NRVO, the compiler has to pre-allocate memory in the caller's space and let the named variable inside the function construct directly there. But if two different named variables might be returned, the compiler can't place both in the same spot, they each have their own address.

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

Here the compiler can't know whether `a` or `b` will be returned, so it can't put either one in the caller's space ahead of time. The result: construct `a` and `b` normally, then move whichever one the condition picks into the return value. You can recover NRVO by rewriting: use a single named variable and assign it different values in different branches.

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

Another common failure is **returning a function parameter**. NRVO only applies to locals inside the function; a parameter is an object already constructed in the caller's stack frame, and the compiler can't "move" it into the return value's space.

```cpp
Point return_param(Point p)
{
    // do something with p ...
    return p;   // no NRVO, but C++11 implicit move
}
```

Here `p` is a parameter, not a local, so NRVO doesn't apply. The good news is that C++11's implicit move rule still holds: `return p;` treats `p` as an rvalue and calls the move constructor. So you don't fall back to a copy, just to a move.

One more case worth mentioning even though it isn't really a "failure": **returning a global or static variable**. There's no NRVO to speak of here, a global/static has a fixed storage location and can't be moved into the caller's space.

```cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // copy construction, no NRVO, no implicit move
}
```

Note that even implicit move doesn't happen here. `global_point` isn't a local, so C++11's implicit move rule doesn't apply. This really is a copy construction. If you want a move, you have to write `return std::move(global_point);` explicitly.

## Seeing RVO in Assembly

Understanding the theory matters, but nothing beats looking straight at the assembly. Let's write two functions and compare the compiler output with and without RVO.

```cpp
// rvo_asm.cpp -- inspect assembly on Compiler Explorer
// Full assembly at https://godbolt.org

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
    return h;            // returning a parameter, no NRVO
}
```

Compiled on x86-64 with `g++ -std=c++17 -O2` (GCC 16.1.1), `with_rvo` looks like this:

```asm
// GCC 16.1.1, -O2 -std=c++17
with_rvo(int):
    movd    %esi, %xmm1         ; 参数 v 加载到 SSE 寄存器
    movq    %rdi, %rax          ; rdi = 调用者提供的返回值地址
    leaq    1024(%rdi), %rdx    ; 循环终止地址 = 起始 + 1024
    pshufd  $0, %xmm1, %xmm0   ; 将 v 广播到 xmm0 的全部 4 个 int
.L2:
    movups  %xmm0, (%rax)      ; 每次写入 16 字节
    addq    $32, %rax
    movups  %xmm0, -16(%rax)
    cmpq    %rdx, %rax
    jne     .L2
    movq    %rdi, %rax
    ret
```

Note a few things: the function works directly on the caller's memory through the implicit `rdi` parameter (the address of the space the caller provided). It broadcasts `v` across the 4 lanes of an SSE register with `pshufd`, then writes 32 bytes per loop iteration (two `movups`), looping 1024/32 = 32 times to fill all of `data[256]` (1024 bytes total). No `memcpy` call, no extra memory copy, construction and return merge into one.

`without_rvo` is far shorter, and far blunter:

```asm
// GCC 16.1.1, -O2 -std=c++17
without_rvo(Heavy):
    movl    $1024, %edx
    jmp     memcpy@PLT
```

Two instructions: load 1024 (the byte count) into `%edx`, then tail-call `memcpy`. The compiler hands the whole "copy 1024 bytes" job to libc's `memcpy` instead of inlining it. That's the cost without RVO/NRVO, a real 1024-byte memory copy (`int data[256]` is 256 × 4 = 1024 bytes), which can become a hot-path bottleneck for large objects.

Worth noting: earlier GCC (15, say) inlined this copy as a `rep movsq` instruction, looping through the bytes right inside the function body; GCC 16 switched to calling `memcpy`. Different form, same essence: it's still that 1024-byte copy, and with RVO it doesn't exist at all.

## RVO and Move Semantics

Plenty of people conflate RVO with move semantics, figuring "we have moves anyway, RVO doesn't matter". They're actually optimizations at different levels, and RVO takes priority.

RVO/NRVO is **elimination**, it does away with the move too. Move semantics is a **downgrade**, from a deep copy to a shallow pointer handoff. The relationship boils down to a priority chain:

```text
Guaranteed elision (C++17 prvalue) > NRVO (compiler optimization) > Implicit move (C++11) > Copy construction
```

The compiler tries left to right: can it elide? If not, can it NRVO? If not, implicit move, and only at the very end a copy. So you don't need to worry that "if RVO fails the performance crashes"; even when RVO fails, move semantics catches you, far better than the pure copies of the C++03 era.

This also leads to a very important practical rule: **never write `return std::move(local_var);`**.

```cpp
Heavy bad_idea()
{
    Heavy h(42);
    return std::move(h);  // blocks NRVO!
}

Heavy good_idea()
{
    Heavy h(42);
    return h;  // may trigger NRVO, or at worst an implicit move
}
```

`return std::move(h);` explicitly turns `h` into an rvalue reference, which means the compiler must use the move constructor, and you've personally snuffed out the NRVO opportunity. `return h;` gives the compiler the most freedom: it can do NRVO (direct elision) or implicit move (C++11 guarantee), either of which beats an explicit `std::move`.

## Worked Example: A String-Builder Factory

Let's put RVO/NRVO to work in a real scenario. Say we're writing a config-file parser and need a factory that builds a config string:

```cpp
#include <iostream>
#include <string>
#include <map>

using Config = std::map<std::string, std::string>;

/// @brief 将配置映射转换为可读的字符串
/// NRVO 场景：返回命名局部变量
std::string format_config_nrvo(const Config& cfg)
{
    std::string result;
    result.reserve(256);  // 预分配，避免多次扩容

    for (const auto& [key, value] : cfg) {
        result += key;
        result += " = ";
        result += value;
        result += "\n";
    }

    return result;  // NRVO：result 直接在调用者空间构造
}

/// @brief 构建一条简单的配置行
/// RVO 场景：返回 prvalue
std::string make_config_line(const std::string& key, const std::string& value)
{
    return key + " = " + value + "\n";  // C++17 保证消除
}

/// @brief 条件返回——NRVO 可能失效的例子
std::string format_with_default(
    const Config& cfg,
    const std::string& key,
    const std::string& default_value)
{
    auto it = cfg.find(key);
    if (it != cfg.end()) {
        return it->first + " = " + it->second + "\n";  // prvalue，保证消除
    }
    return key + " = " + default_value + " (default)\n";  // prvalue，保证消除
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

These three functions show different return scenarios. `format_config_nrvo` returns a named variable built through a complex process, and NRVO lets `result` grow directly in the caller's space, saving even a single string move. `make_config_line` returns an expression result (prvalue), which C++17 guarantees to elide. `format_with_default` has conditional branches, but every branch returns a prvalue, so it still gets guaranteed elision.

## Hands-On Experiment: rvo_demo.cpp

Let's write a complete experiment that runs RVO, NRVO, the failure cases, and the `std::move` misuse all at once.

```cpp
// rvo_demo.cpp -- RVO / NRVO 完整演示
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

/// @brief RVO：返回 prvalue
Tracker make_rvo(const std::string& name)
{
    return Tracker(name + "_rvo");
}

/// @brief NRVO：返回命名局部变量
Tracker make_nrvo(const std::string& name)
{
    Tracker t(name + "_nrvo");
    return t;
}

/// @brief 失效的 NRVO：两个返回分支返回不同命名对象
Tracker make_bad_nrvo(const std::string& name, bool flag)
{
    Tracker a(name + "_a");
    Tracker b(name + "_b");
    if (flag) {
        return a;
    }
    return b;
}

/// @brief 错误示范：用 std::move 阻止了 NRVO
Tracker make_bad_move(const std::string& name)
{
    Tracker t(name + "_badmove");
    return std::move(t);   // 显式移动，阻止 NRVO
}

/// @brief 返回函数参数——NRVO 不适用，但有隐式移动
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

Let's look closely at this output. Steps 1 and 2 are the perfect case: RVO and NRVO both kick in, each object is constructed once, no copy or move at all. Step 3 is where NRVO fails, because two branches return different named objects; the compiler picks an implicit move of `a` (`C_a` becomes a move construction), while `b` is destroyed normally. Step 4 shows the consequence of `return std::move(t)`: NRVO is blocked, an extra move construction happens. The compiler will actually tip you off too: this code triggers a `-Wpessimizing-move` warning ("moving a local object in a return statement prevents copy elision"), spelling out that the `std::move` here kills the elision. Step 5 is the interesting one: `return_param` does one move construction when receiving the parameter (triggered by `std::move(param)`), then another implicit move when returning it, two moves total. Note the destruction order: `param` is destroyed after `e`, because `param` is declared in the outer scope and outlives `e`.

If you recompile with `-fno-elide-constructors` to turn elision off, you'll see step 2 (NRVO) pick up a move construction, but step 1 (RVO) is unaffected. That's the difference between C++17 guaranteed elision and non-guaranteed optimization. Step 1 is guaranteed elision under C++17, and `-fno-elide-constructors` has no effect on it (guaranteed elision is a language semantic, not something a compiler flag controls). NRVO is still an "allowed but not required" optimization, so `-fno-elide-constructors` can turn it off.

## Practical Guidelines

Turning the theory into actual coding, here are a few simple rules to help you get the most out of RVO/NRVO.

First, **return by value, don't use out-parameters**. `std::string build_message()` is friendlier to RVO/NRVO than `void build_message(std::string& out)`. The modern C++ philosophy is "write natural code and let the compiler optimize for you", and returning by value is the most natural form.

Second, **don't write `return std::move(local);`**. I've said this a few times already, because I've seen too many "meant well, made it worse" cases. `return local;` gives the compiler the most room: it can do NRVO, or an implicit move. `return std::move(local);` forces a fallback to move construction, which is an anti-optimization.

Third, **keep return paths simple**. If you have multiple return branches, try to have them all return the same named variable, or all return prvalues. Avoid different branches returning different named objects, that blocks NRVO.

Fourth, **measure performance-sensitive code**. RVO/NRVO is a compiler optimization, and different compilers, versions, and optimization levels can behave differently. If you genuinely care about the performance of a particular return, write a benchmark and measure it, don't guess.

## Run Online

Run the RVO/NRVO example online and observe copy elision across different return scenarios:

<OnlineCompilerDemo
  title="RVO/NRVO Comparison: 5 Return Scenarios"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="Run online and observe the different behaviors of RVO, NRVO, NRVO failure, and std::move blocking optimization."
  allow-x86-asm
/>
