---
title: 'RVO and NRVO: Compiler Return Value Optimization'
description: Deep dive into the return value optimization mechanism, guaranteeing
  copy elision from C++11 to C++17.
chapter: 0
order: 3
tags:
- host
- cpp-modern
- intermediate
- 移动语义
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 14
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 0: 移动构造与移动赋值'
related:
- 移动语义实战
translation:
  source: documents/vol2-modern-features/ch00-move-semantics/03-rvo-nrvo.md
  source_hash: 8d67d746d9f8272674ab3ce99886ea907654c8affe26a59d7d2dfabc512f452e
  translated_at: '2026-05-26T11:18:10.686224+00:00'
  engine: anthropic
  token_count: 3665
---
# RVO and NRVO: Compiler Return Value Optimization


If you come from a C background, especially from programming MCUs, and particularly from MCUs with tiny RAM, you would never return a large struct from a function. I mean, you would never write ``struct X GetSth(...)``, right? (The stack would blow up in a heartbeat.) This is because returning a struct by value means constructing it inside the function and then copying it to the caller—for structs that are often hundreds of bytes, this overhead is completely unacceptable in performance-sensitive code. So we invented all sorts of workarounds: passing out pointer parameters, returning static local variables, using `malloc` and letting the caller `free`...

With the introduction of copy and move constructors in C++, the cost of returning large objects by value has dropped significantly—but compilers can do even better. They have a "zero-cost" secret weapon:

The first is **Return Value Optimization (RVO)**,
and the second is **Named Return Value Optimization (NRVO)**.

The core idea behind both is simple: since the final object will end up on the caller's stack frame anyway, why construct it inside the function first and then copy/move it over? Why not just construct it directly in the caller's space? This question gives rise to both techniques. That's all there is to it.

## What Exactly Do RVO and NRVO Do?

Suppose we have a simple ``Point`` class with a copy constructor that prints a log message:

````cpp
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
````

Then we write two factory functions—one returning a temporary object, and one returning a named local variable:

````cpp
// RVO 场景：返回 prvalue（临时对象）
Point make_point_rvo(double x, double y)
{
    return Point(x, y);   // 返回一个临时对象
}

// NRVO 场景：返回命名局部变量
Point make_point_nrvo(double x, double y)
{
    Point p(x, y);        // 命名局部变量
    // ... 可能还有一些对 p 的操作 ...
    return p;             // 返回命名变量
}
````

Without optimization, ``make_point_rvo`` would first construct ``Point(x, y)`` inside the function, then copy (or move) it to the caller's space. ``make_point_nrvo`` does the same: it constructs ``p``, then copies/moves ``p`` to the caller. But with RVO/NRVO, the compiler allocates space directly on the caller's stack frame and makes the internal construction happen right there—**there is no intermediate object at all, so there is nothing to copy or move**.

Let's verify this:

````cpp
int main()
{
    std::cout << "=== RVO ===\n";
    Point a = make_point_rvo(1.0, 2.0);

    std::cout << "\n=== NRVO ===\n";
    Point b = make_point_nrvo(3.0, 4.0);

    return 0;
}
````

Compile with GCC at the default optimization level:

````bash
g++ -std=c++17 -Wall -Wextra -o rvo_test rvo_test.cpp
./rvo_test
````

Output:

````text
=== RVO ===
  构造 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
````

Each ``Point`` is constructed only once—no copies, no moves. This is RVO/NRVO at work: the compiler literally "moves" the construction into the caller's space.

## Verifying with Compiler Flags—What Happens When We Disable Elision?

GCC and Clang provide a compiler flag ``-fno-elide-constructors`` that forcibly disables copy elision. Let's see what happens when we turn it off:

````bash
g++ -std=c++17 -Wall -fno-elide-constructors -o rvo_no_elide rvo_test.cpp
./rvo_no_elide
````

The output becomes (GCC 15, ``-std=c++17``):

````text
=== RVO ===
  构造 Point(1, 2)

=== NRVO ===
  构造 Point(3, 4)
  移动 Point(3, 4)
````

There is a very important detail to note here: the RVO part **did not change**—even with ``-fno-elide-constructors`` added, ``make_point_rvo`` is still constructed only once, with no move. This is because C++17 guarantees copy elision for prvalue returns as a language semantic, not as a compiler optimization that can be toggled by a flag (we'll dive into this in detail later). What is actually affected is NRVO: ``make_point_nrvo`` degrades from "zero-cost" to a move construction.

Notice that when NRVO degrades, it uses a move rather than a copy—because since C++11, when the compiler encounters ``return local_var;``, it automatically treats ``local_var`` as an rvalue (implicit move), even though ``local_var`` is an lvalue inside the function. This is a crucial guarantee: even if copy elision doesn't kick in, you at least get the performance of move semantics.

> (If you want to observe "full degradation"—where even RVO degrades to a move—you can compile in C++14 mode: ``g++ -std=c++14 -fno-elide-constructors``. Under C++14, ``-fno-elide-constructors`` affects both RVO and NRVO, and both functions will incur an extra move operation.)

## C++17 Guaranteed Elision—From "Allowed" to "Mandatory"

Before C++17, both RVO and NRVO were optimizations that compilers were **allowed to perform but not required to**. In other words, the standard said "the compiler may omit this copy/move," but it didn't say "the compiler must omit it." In practice, mainstream compilers would almost always do it with optimizations enabled, but strictly speaking, it wasn't guaranteed.

C++17 changed the rules for one of these cases: **when the return value is a prvalue, copy elision becomes guaranteed**. This is not an optional optimization—it is a semantic guarantee of the language. This means that a statement like ``return Point(x, y);`` in C++17 will **never** trigger a copy or move constructor.

The underlying principle of this guarantee is C++17's redefinition of prvalue semantics. Before C++17, a prvalue was understood as a "temporary object"—when a function returned ``Point(x, y)``, a temporary ``Point`` object was first created, then copied/moved to the caller's space. After C++17, a prvalue was redefined as a "recipe for initialization"—``Point(x, y)`` is no longer an object, but a set of construction instructions telling the compiler "construct a ``Point`` at this location with these arguments." Since a prvalue is not an object, there is no "copying an object" to speak of, and copy elision is naturally guaranteed.

````cpp
// C++17 之前：Point(x,y) 是一个临时对象
// C++17 之后：Point(x,y) 是一个"构造配方"
Point make_point(double x, double y)
{
    return Point(x, y);  // C++17 保证不触发拷贝/移动
}
````

> ⚠️ **Pitfall Warning**: C++17's guaranteed elision only applies to scenarios where the return value is a **prvalue**—that is, directly returning a temporary object like ``return Type(args...);``. Returning a **named local variable** (NRVO) remains an "allowed but not required" optimization; C++17 did not make NRVO guaranteed either. So whether ``p`` in ``return p;`` gets elided still depends on the compiler's implementation.

## When Does NRVO Fail?

Although NRVO works most of the time, certain code patterns can cause it to fail. Understanding these patterns is important—because failure means you might degrade from "zero-cost" to "move-cost," which isn't fatal but can become a bottleneck on performance-sensitive hot paths.

The most typical failure scenario is **multiple return branches returning different named objects**. For the compiler to perform NRVO, it needs to pre-allocate memory in the caller's space and then have the named variable inside the function construct directly into that space. But if two different named variables might be returned, the compiler can't place both variables in the same block of space—they each have their own address.

````cpp
Point bad_nrvo(bool flag)
{
    Point a(1.0, 2.0);
    Point b(3.0, 4.0);
    if (flag) {
        return a;   // 可能阻止 NRVO
    }
    return b;       // 返回不同的命名对象
}
````

In this case, the compiler can't determine whether ``a`` or ``b`` will be returned, so it can't pre-place either one in the caller's space. The result is: ``a`` and ``b`` are constructed normally, and then one of them is moved to the return value based on the condition. You can restore NRVO by modifying the code to use a single named variable, assigning it different values in different branches.

````cpp
Point good_nrvo(bool flag)
{
    Point result(0.0, 0.0);
    if (flag) {
        result = Point(1.0, 2.0);
    } else {
        result = Point(3.0, 4.0);
    }
    return result;   // NRVO 可以生效
}
````

Another common failure scenario is **returning a function parameter**. NRVO only applies to local variables inside the function. Function parameters are objects already constructed on the caller's stack frame, and the compiler can't "move" them into the return value's space.

````cpp
Point return_param(Point p)
{
    // 对 p 做一些操作 ...
    return p;   // 无法 NRVO，但 C++11 会隐式移动
}
````

Here, ``p`` is a function parameter, not a local variable, so NRVO won't apply. The good news is that C++11's implicit move rule still applies—``return p;`` treats ``p`` as an rvalue and invokes the move constructor. So you won't degrade to a copy, just to a move.

There is also a scenario that isn't exactly a "failure" but is worth mentioning: **returning a global or static variable**. In this case, there is no NRVO to speak of—global/static variables have fixed storage locations and cannot be relocated to the caller's space.

````cpp
Point global_point(1.0, 2.0);

Point return_global()
{
    return global_point;   // 拷贝构造，没有 NRVO，也没有隐式移动
}
````

Note that not even implicit move happens here—``global_point`` is not a local variable, so C++11's implicit move rule doesn't apply to it. This is indeed a copy construction. If you want a move, you need to explicitly write ``return std::move(global_point);``.

## Seeing RVO in Action Through Assembly

Understanding the theory is important, but nothing beats looking at assembly to see the proof. Let's write two functions and compare the compiled output with and without RVO.

````cpp
// rvo_asm.cpp -- 用 Compiler Explorer 查看汇编
// 建议在 https://godbolt.org 上查看完整汇编

struct Heavy {
    int data[256];
    Heavy(int v) { for (auto& d : data) d = v; }
    Heavy(const Heavy& o) { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
    Heavy(Heavy&& o) noexcept { for (int i = 0; i < 256; ++i) data[i] = o.data[i]; }
};

Heavy with_rvo(int v)
{
    return Heavy(v);     // C++17 保证消除
}

Heavy without_rvo(Heavy h)
{
    return h;            // 参数返回，无法 NRVO
}
````

Compiled on x86-64 with ``g++ -std=c++17 -O2`` (GCC 15), the assembly for ``with_rvo`` looks like this:

````asm
// GCC 15, -O2 -std=c++17
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
````

Notice a few things: the function works directly on the caller's memory through an implicit ``rdi`` parameter (the address of the space provided by the caller). It broadcasts ``v`` into the 4 lanes of an SSE register using ``pshufd``, then writes 32 bytes per loop iteration (two ``movups`` instructions), looping 1024/32 = 32 times to fill the entire ``data[256]`` (1024 bytes total). There is no ``memcpy`` call, no extra memory copy—construction and return are one and the same.

The assembly for ``without_rvo`` is noticeably different:

````asm
// GCC 15, -O2 -std=c++17
without_rvo(Heavy):
    movq    (%rsi), %rax
    movq    %rdi, %rdx
    leaq    8(%rdi), %rdi
    movq    %rax, -8(%rdi)
    movq    1016(%rsi), %rax
    movq    %rax, 1008(%rdi)
    andq    $-8, %rdi
    movq    %rdx, %rax
    subq    %rdi, %rax
    leal    1024(%rax), %ecx   ; 待复制的字节数：1024
    subq    %rax, %rsi
    movq    %rdx, %rax
    shrl    $3, %ecx           ; 1024 / 8 = 128 个四字（qword）
    rep movsq                  ; 复制 128 * 8 = 1024 字节
    ret
````

Here we have ``rep movsq``, where ``ecx`` is calculated as ``1024 / 8 = 128``—a 1024-byte memory copy operation (the size of ``int data[256]`` is exactly 256 * 4 = 1024 bytes). The compiler handles the 8-byte alignment at the beginning and end, then bulk-copies the middle section with ``rep movsq``. This is the cost of not having RVO/NRVO: for large objects, this 1024-byte copy can become a bottleneck on hot paths.

## The Relationship Between RVO and Move Semantics

Many people confuse RVO with move semantics, thinking "since we have moves anyway, RVO doesn't matter." In reality, they are optimizations at different levels, and RVO has higher priority.

RVO/NRVO is **elision**—even the move is eliminated. Move semantics is **degradation**—from a deep copy down to a shallow pointer transfer. Their relationship can be expressed as a simple priority chain:

````text
保证消除（C++17 prvalue） > NRVO（编译器优化）> 隐式移动（C++11）> 拷贝构造
````

The compiler tries from left to right—first checking if it can elide (RVO), then if it can do NRVO, then falling back to implicit move, and finally resorting to copy construction. So you don't need to worry that "if RVO fails, performance will collapse"—even if RVO fails, you still have move semantics as a safety net, which is far better than the pure copies of the C++03 era.

This leads to a critically important practical rule: **never write ``return std::move(local_var);``**.

````cpp
Heavy bad_idea()
{
    Heavy h(42);
    return std::move(h);  // 阻止了 NRVO！
}

Heavy good_idea()
{
    Heavy h(42);
    return h;  // 可能触发 NRVO，退一步也是隐式移动
}
````

``return std::move(h);`` explicitly converts ``h`` to an rvalue reference, which means the compiler is forced to use move construction—you've single-handedly killed the opportunity for NRVO. ``return h;``, on the other hand, gives the compiler maximum freedom: it can perform NRVO (direct elision) or implicit move (guaranteed since C++11), and either option is better than an explicit ``std::move``.

## Practical Example—A String Building Factory

Let's apply our knowledge of RVO/NRVO to a real-world scenario. Suppose we're writing a configuration file parser and need a factory function to build configuration strings:

````cpp
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
````

These three functions demonstrate different return scenarios. ``format_config_nrvo`` returns a named variable that has gone through a complex construction process; NRVO allows ``result`` to grow directly in the caller's space—saving even a single string move. ``make_config_line`` returns an expression result (a prvalue), so C++17 guarantees elision. ``format_with_default`` has conditional branches, but each branch returns a prvalue, so it still enjoys guaranteed elision.

## Hands-On Experiment—rvo_demo.cpp

Let's write a complete experiment program that runs through RVO, NRVO, failure scenarios, and the misuse of ``std::move``.

````cpp
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
````

Compile and run:

````bash
g++ -std=c++17 -Wall -Wextra -O2 -o rvo_demo rvo_demo.cpp
./rvo_demo
````

Actual output (GCC 15, ``-std=c++17 -O2``):

````text
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
````

Let's carefully analyze this output. Steps 1 and 2 are the ideal cases—both RVO and NRVO are in effect, each object is constructed only once, with no copies or moves. In step 3, NRVO fails because the two branches return different named objects; the compiler chose implicit move for ``a`` (``C_a`` became a move construction), and ``b`` was destructed normally. Step 4 demonstrates the consequence of ``return std::move(t)``—NRVO is blocked, and an extra move construction occurs. Step 5 is rather interesting: receiving the parameter triggers a move construction (``std::move(param)`` fires), and returning the parameter triggers another implicit move—for a total of two moves. Note the destruction order—``param`` is destructed after ``e``, because ``param`` is declared in the outer scope and its lifetime ends later than ``e``'s scope.

If you recompile with ``-fno-elide-constructors`` to disable elision, you'll find that step 2 (NRVO) incurs a move construction, but step 1 (RVO) is unaffected—this is the difference between C++17 guaranteed elision and non-guaranteed optimization. Step 1 is guaranteed elision under C++17, and ``-fno-elide-constructors`` has no effect on it (because guaranteed elision is a language semantic, not a compiler optimization toggle). NRVO, however, remains an "allowed but not required" optimization, so it can be disabled by ``-fno-elide-constructors``.

## Practical Guidelines

To put theory into practice, here are a few simple rules to help you maximize the benefits of RVO/NRVO.

First, **return by value; don't use output parameters**. ``std::string build_message()`` is more conducive to RVO/NRVO than ``void build_message(std::string& out)``. The philosophy of modern C++ is "write natural code and let the compiler optimize for you," and returning by value is the most natural approach.

Second, **never write ``return std::move(local);``**. This rule has been emphasized several times because the author has seen too many cases of "no good deed goes unpunished." ``return local;`` gives the compiler maximum optimization space—it can perform NRVO or implicit move. ``return std::move(local);`` forces degradation to move construction, which is an anti-optimization.

Third, **keep return paths simple**. If you have multiple return branches, try to have them all return the same named variable, or have them all return prvalues. Avoid having different branches return different named objects—this blocks NRVO.

Fourth, **measure performance-sensitive code**. RVO/NRVO are compiler optimizations, and behavior may vary across different compilers, versions, and optimization levels. If you truly care about the performance of a specific return, write a benchmark to measure it rather than guessing.

## Run Online

Run the RVO/NRVO example online to observe copy elision effects across different return scenarios:

<OnlineCompilerDemo
  title="RVO/NRVO Comparison: 5 Return Scenarios"
  source-path="code/examples/vol2/03_rvo_nrvo.cpp"
  description="Run online and observe the different behaviors of RVO, NRVO, NRVO failure, and std::move blocking optimization."
  allow-x86-asm
/>

## Summary

RVO and NRVO are the free lunch that modern C++ gives us—without sacrificing code readability, the compiler eliminates the overhead of return values. C++17 further elevates prvalue return elision to a language guarantee, letting us return large objects by value with greater peace of mind. Although NRVO is not guaranteed, mainstream compilers almost always perform it when optimizations are enabled. Remember the single most important rule: **when returning a local variable, just ``return``—don't add ``std::move``**—let the compiler do what it does best.

In the next article, we'll dive into move semantics in practice and see how this theoretical knowledge comes to life in STL containers and custom types.
