---
title: 'variant: Type-Safe Unions and visit'
description: Explains why `std::variant` supersedes tagged unions—automatic destruction
  and indexing, trade-offs between `get`/`get_if`/`holds_alternative`, pattern matching
  with `std::visit` and the `overloaded` lambda pattern, and the pathological `valueless_by_exception`
  state. Also covers why `variant` offers better value semantics and memory efficiency
  than inheritance polymorphism for closed type sets.
chapter: 7
order: 62
cpp_standard:
- 17
- 20
difficulty: intermediate
platform: host
tags:
- host
- cpp-modern
- intermediate
- 类型安全
prerequisites:
- 对象大小、对齐与平凡类型
- vector 深入：三指针、扩容与迭代器失效
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
reading_time_minutes: 16
translation:
  source: documents/vol3-standard-library/error-utils/62-variant.md
  source_hash: 6ef559cb6c1f778803507e58d9e75246e644a9d909b6a5a128076dd331094323
  translated_at: '2026-06-24T00:38:23.052145+00:00'
  engine: anthropic
  token_count: 3869
---
# variant: Type-safe Unions and visit

We often write code where "one variable is sometimes A, and sometimes B." In a state machine, a connection might be `Connecting`, `Connected`, or `Error`; in a parser, a token might be a number, a string, or a symbol; a configuration item might be a scalar or a list. Traditionally, there are two approaches: either use an `enum` with a `union` and manually track "which one is active now," or build an inheritance hierarchy with `class Shape` holding `Circle`, `Square`, and `Triangle`, relying on virtual function dispatch.

Both paths have their drawbacks. A `union` doesn't track the current type—if you stuff an `int` in but read it out as a `string`, the compiler stays silent, leading to undefined behavior at runtime. Destruction is even murkier (if the `string` destructor isn't called, memory leaks). Inheritance polymorphism is type-safe, but every object must be `new`-ed onto the heap, carrying a virtual table pointer. Just to "store a value," you pay for a heap allocation and an indirect jump, plus you have to manage the object lifecycle.

C++17 offers a third path: `std::variant<Ts...>`, a **type-safe union**. Building on the `union` concept of "sharing a memory block," it adds an index tracking "which type is currently active" and handles destruction automatically. In this article, we will cover everything from "why not use a raw union" to pattern matching with `std::visit`, the strange `valueless_by_exception` state, and finally, a direct performance comparison with inheritance polymorphism.

## Why not use a raw union

Let's look at exactly where a raw `union` falls short. The code below compiles without a single warning, but it is fundamentally wrong:

```cpp
// Standard: C++98
union BadUnion {
    int i;
    std::string s;   // 带 non-trivial 成员的 union
};

void misuse() {
    BadUnion u;
    u.s = std::string("hello");   // 当 string 存
    int x = u.i;                  // 当 int 读 —— 未定义行为
    // 函数结束: 没人调 string 析构, 内存泄漏
}
```

A `union` does not **know** whether it currently holds an `int` or a `string`. Reading from a `string` as if it were an `int` is undefined behavior (UB). Since the destructor for the `string` is never called, it results in a leak. To use it correctly, the programmer must attach an external tag, manually check the type, and manually destroy the object—this entire set of boilerplate code relies entirely on human discipline for correctness. I have seen too much code where developers "thought a `union` saved memory, but ended up with a pile of memory leaks."

`std::variant` automates this entire process. It handles two things:

1. **Track the index**: It stores an index internally indicating "which alternative type is currently active." We can retrieve this via `index()` or check directly with `holds_alternative<T>()`.
2. **Automatic destruction**: Every time the type changes (via assignment or `emplace`), it destroys the old object before constructing the new one. When its lifetime ends, it destroys the currently held object.

The cost is that it consumes a small amount of extra space to store that index (usually just a few bytes), but in return, we get "reading the wrong type throws an exception instead of triggering UB, and destruction is always correct."

## Construction and Access: The Four Essentials

Let's walk through the most basic usage:

```cpp
// Standard: C++17
#include <variant>
#include <string>
#include <iostream>

int main()
{
    std::variant<int, double, std::string> v;  // 默认构造 -> 持第一个类型(int)
    std::cout << "默认构造 index=" << v.index() << " (int)\n";

    v = 3.14;                                  // 赋 double
    std::cout << "赋值 3.14 index=" << v.index() << " (double)\n";

    v = std::string("hello");                  // 赋 string
    std::cout << "赋值 hello index=" << v.index() << " (string)\n";

    // 1. holds_alternative<T>: 当前是不是 T?
    std::cout << "holds<string>=" << std::holds_alternative<std::string>(v) << "\n";
    std::cout << "holds<int>="    << std::holds_alternative<int>(v) << "\n";

    // 2. get<T>: 取值, 类型不符抛 bad_variant_access
    std::cout << "get<string>=" << std::get<std::string>(v) << "\n";
    try {
        std::cout << std::get<int>(v) << "\n";   // 当前是 string, 取 int
    } catch (const std::bad_variant_access& e) {
        std::cout << "异常: " << e.what() << "\n";
    }

    // 3. get_if<T>: 取指针, 不符返 nullptr (不抛)
    if (auto* p = std::get_if<double>(&v)) {
        std::cout << "double: " << *p << "\n";
    } else {
        std::cout << "不是 double, get_if 返回 nullptr\n";
    }

    // 4. get<I>: 按索引取(0=int, 1=double, 2=string)
    std::cout << "get<2>=" << std::get<2>(v) << "\n";
    return 0;
}
```

Here are the results from running `g++ -std=c++23 -O2` (local GCC 16.1.1):

```text
默认构造 index=0 (int)
赋值 3.14 index=1 (double)
赋值 hello index=2 (string)
holds<string>=1
holds<int>=0
get<string>=hello
异常: std::get: wrong index for variant
不是 double, get_if 返回 nullptr
get<2>=hello
```

How to choose among the four methods depends on "what you want to do when the types don't match":

- **Want to throw an exception**: Use `get<T>()`. It is clean, but incurs a branch and potential exception overhead on every access.
- **Don't want to throw, handle it yourself**: Use `get_if<T>()`. If it returns `nullptr`, the type is incorrect. In performance-sensitive code or where exceptions are disabled, this is the more robust choice.
- **Only want to check, without retrieving the value**: `holds_alternative<T>()` returns a `bool` and is the most readable approach.
- **By index instead of by type**: `get<I>()`. Occasionally useful, such as when iterating over a sequence of indices known at compile time.

::: warning The "type" used by `get` and `get_if` must be one of the alternative types
`std::get<long>(v)` on a `variant<int, double, string>` is a **compile-time error**—`long` is not in the set of alternatives. The type safety of `variant` comes precisely from the fact that "you can only retrieve the types declared," unlike a raw `union` where you can read whatever you want.
:::

## std::visit: Turning if-else Chains into Pattern Matching

At this point, you might say: the four methods are enough, so why not just write a bunch of `if (holds_alternative<A>) ... else if (holds_alternative<B>) ...`? It works, but there are a few issues. First, it's ugly—every time you add a type, you have to come back and modify this chain, and if you forget, you miss a case. Second, it's slow—every access involves a `holds_alternative` branch. Third, the compiler doesn't check "whether every type is handled."

`std::visit` solves all three of these problems. It feeds a "visitor" function object to a `variant`, requiring that this visitor can handle **every** alternative type—miss one, and compilation fails. Before we run a minimal example, let's introduce the key technique that makes it truly useful: the **overloaded lambda**.

The visitor must be an object that can call `operator()` for all alternative types. The most direct way is to hand-write a `struct`:

```cpp
// Standard: C++17
struct Describe {
    std::string operator()(int i) const { return "int:" + std::to_string(i); }
    std::string operator()(double d) const { return "double:" + std::to_string(d); }
    std::string operator()(const std::string& s) const { return "string:\"" + s + "\""; }
};
```

It works, but it's verbose. Every time we add a branch, we have to go back to this `struct` and add a member function. C++17 offers a much cleaner approach—we can inherit a group of lambda expressions together to form a function object that matches all types:

```cpp
// Standard: C++17
template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
```

These three lines are the "common incantation" of the C++ community (`using Ts::operator()...` is a C++17 pack-using declaration, and the deduction guide deduces `overloaded<L1, L2, ...>` directly from a set of lambdas). Combined with `std::visit`, the `Describe` function above can be written as a set of local lambdas:

```cpp
// Standard: C++17
#include <variant>
#include <string>
#include <vector>
#include <iostream>

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using Value = std::variant<int, double, std::string>;

std::string describe(const Value& v)
{
    return std::visit(overloaded{
        [](int i)                -> std::string { return "int:" + std::to_string(i); },
        [](double d)             -> std::string { return "double:" + std::to_string(d); },
        [](const std::string& s) -> std::string { return "string:\"" + s + "\""; }
    }, v);
}

int main()
{
    std::vector<Value> vals{42, 3.14, std::string("hello"), 7};
    for (const auto& v : vals) std::cout << describe(v) << "\n";
    return 0;
}
```

```text
int:42
double:3.140000
string:"hello"
int:7
```

The power of this snippet lies in the fact that `std::visit` knows exactly which types are in the `variant` at compile time. The set of `operator()` overloads is also known at compile time, allowing it to **compile the entire dispatch into a jump table** (typically a single indirect jump based on `index()`). This eliminates runtime chains of `holds_alternative` checks and avoids the virtual table indirection associated with inheritance. Furthermore, if you add a fourth type to `Value` but forget to handle it in `overloaded`, the code **will fail to compile directly**. The compiler is checking for completeness here, which is much safer than hand-writing a chain of `if-else` statements.

::: warning Don't misremember the three-line spell for `overloaded`
Do not omit the `...` at the end of `using Ts::operator()...;`. It signifies "bring `operator()` from *every* base class into scope"; without it, you only introduce one, resulting in incomplete dispatch. Also, don't forget the deduction guide `overloaded(Ts...) -> overloaded<Ts...>;`. Without it, you cannot construct `overloaded` in-place using `overloaded{...}`. This pattern remains valid in C++20 and is the most robust idiom in the community.
:::

## Two new tools in C++20: in-place lambdas + `visit<R>`

In C++20, we can actually skip that `overloaded` incantation—we can just use a generic lambda with `if constexpr` to write "do X when we see type Y" right on the spot:

```cpp
// Standard: C++20
#include <variant>
#include <string>
#include <vector>
#include <iostream>
#include <type_traits>

struct Connect { std::string addr; };
struct Disconnect {};
struct Data { std::vector<unsigned char> bytes; };
using Event = std::variant<Connect, Disconnect, Data>;

int main()
{
    std::vector<Event> evs{
        Connect{"10.0.0.1"},
        Data{{1, 2, 3}},
        Disconnect{},
    };
    for (const auto& e : evs) {
        std::visit([](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Connect>) {
                std::cout << "connect -> " << x.addr << "\n";
            } else if constexpr (std::is_same_v<T, Disconnect>) {
                std::cout << "disconnect\n";
            } else {
                std::cout << "data " << x.bytes.size() << " bytes\n";
            }
        }, e);
    }
    return 0;
}
```

```text
connect -> 10.0.0.1
data 3 bytes
disconnect
```

The advantage of generic lambdas combined with `if constexpr` is that "they do not require every branch to have the same return type." The downside is that we must write `is_same_v` checks for each branch, which isn't as tidy as `overloaded`. Both approaches work; we should choose based on the scenario. For fewer branches with similar return types, use `overloaded`. For branches with complex logic or different return types, use generic lambdas.

C++20 also added an explicit return type form for `std::visit`: `std::visit<R>(...)`. This is used to "force the return value of all branches to convert to a common type `R`." This is very handy when the branches naturally return different types, but we want a common type (for example, converting everything to `double`):

```cpp
// Standard: C++20
std::variant<int, double> v = 2;
double r = std::visit<double>([](auto x){ return x; }, v);  // int 分支也转成 double
```

Both C++20 implementations work correctly in GCC 16.1.1. Note: C++23 **does not** add the monadic interface (`.and_then`, `.transform`, `.or_else`) to `variant` like it does for `optional`/`expected`. Unlike `optional`, `variant` does not have an "empty" semantic—it always holds a value (except for the pathological state we are about to discuss), so the design does not include a monadic chain. If you want that feature, check out the dedicated articles for `optional` and `expected`.

## `valueless_by_exception`: The Only Pathological State of `variant`

We have been saying "a `variant` always holds a value," which is mostly true, but there is one exception. `variant` has a state called `valueless_by_exception()`, which literally means "the variant has no value because of an exception." This sounds weird—how can a type that claims to always have a value suddenly not have one?

This stems from the exception guarantees of assignment/`emplace`. When you execute `v = new_value`, the `variant` needs to do two things: destroy the old value, then construct the new value. If the "construct new value" step throws an exception, and the implementation cannot restore the old value, the `variant` enters an awkward intermediate state—the old one is gone, and the new one failed. At this point, it is `valueless`.

Let's artificially create one:

```cpp
// Standard: C++17
#include <variant>
#include <iostream>
#include <stdexcept>

struct S {
    S() = default;
    S(const S&) { throw std::runtime_error("copy throw"); }  // 拷贝构造必抛
};

int main()
{
    std::variant<double, S> v = 1.5;   // 当前持 double
    std::cout << "before index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    S src;                              // 默认构造 OK
    try {
        v = src;                        // 拷贝构造 S -> 抛
    } catch (const std::runtime_error& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
    std::cout << "after index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    if (v.valueless_by_exception()) {
        try {
            (void)std::get<double>(v);   // 连原本的 double 都取不到了
        } catch (const std::bad_variant_access& e) {
            std::cout << "get<double> 也抛: " << e.what() << "\n";
        }
    }
    return 0;
}
```

```text
before index=0 valueless=0
caught: copy throw
after index=18446744073709551615 valueless=1
get<double> 也抛: std::get: variant is valueless
```

That intimidating `18446744073709551615` is actually `variant::npos` (which is `(size_t)-1`, or $2^{64}-1$). It serves as a marker value for `index()` when the `variant` is `valueless`. Once in this state, we cannot even retrieve the original `double`—`get<double>` throws a `bad_variant_access` with the error message explicitly stating "variant is valueless".

How easy is it to stumble into this state? Honestly, it is quite difficult. It requires "constructing a new value throws an exception + the implementation cannot roll back". In the standard library, scenarios where the implementation can roll back (for instance, when the new value is `nothrow` copyable) will not result in a valueless state. What actually triggers it is usually when you write a custom type with a throwing copy or move constructor. In practice, we should treat this state as "should never occur; if it does, your type's exception guarantee has a bug." `valueless_by_exception()` is primarily an introspection interface for library authors. If you encounter it in business logic, fixing the throwing constructor is the correct approach rather than trying to handle the valueless state.

## variant vs. Inheritance Polymorphism: Choosing for Closed Sets

Now that we have covered the mechanism, let's answer a practical question: when should we use `variant`, and when should we use inheritance polymorphism? The key distinction comes down to one concept—**whether the set of types is closed or open**.

Inheritance polymorphism excels when the set is **open**: the base class defines the interface, and anyone can add a new derived class without modifying existing code. If you have a `Shape*` array, you can add a `Hexagon` tomorrow without changing a single line of old code. The trade-off is that every object incurs virtual table indirection, objects usually must be allocated on the heap (adding an allocation step), and cache locality is poor.

`variant` excels when the set is **closed**: all possible types are frozen at compile time (e.g., `variant<A, B, C>`), and adding a new type requires modifying the declaration and updating all visitors to handle the new branch. However, this is actually a **benefit**: the compiler forces you to handle the new type, ensuring nothing is missed. Furthermore, `variant` uses value semantics, stores data on the stack, and has no virtual function overhead. The visitor dispatch results in a compact jump table, which is cache-friendly.

Let's compare these approaches directly using a closed "shape set" example. We have three shapes—`Circle`, `Square`, and `Triangle`—and we need to calculate their areas. We will implement one version using inheritance + virtual functions, and another using `variant` + `visit`, running a benchmark of 4 million objects over three rounds:

```cpp
// Standard: C++17
// 继承: ShapeBase 虚函数 area(); variant: visit + AreaVisitor
// (完整代码见 /tmp/variant_lab/perf.cpp, 这里给关键骨架)
struct CircleV { double r; };
struct SquareV { double s; };
struct TriangleV { double b, h; };
using ShapeV = std::variant<CircleV, SquareV, TriangleV>;

struct AreaVisitor {
    double operator()(const CircleV& c)    const { return 3.14159265 * c.r * c.r; }
    double operator()(const SquareV& sq)   const { return sq.s * sq.s; }
    double operator()(const TriangleV& t)  const { return 0.5 * t.b * t.h; }
};

// 继承版: for (auto& p : poly) acc += p->area();
// variant 版: for (auto& v : vars) acc += std::visit(AreaVisitor{}, v);
```

Native GCC 16.1.1, `-O2`, running twice:

```text
shapes: 4000000 x3 iters
inheritance (virtual): 87 ms
variant + visit:       54 ms
shapes: 4000000 x3 iters
inheritance (virtual): 78 ms
variant + visit:       55 ms
```

The `variant` + `visit` approach is approximately 30% to 40% faster. This performance gap stems from three main factors: the shapes in the `variant` version are stored contiguously within the `vector` (whereas the inheritance version uses `vector<unique_ptr>`, scattering pointers across the heap and causing cache misses); `visit` dispatches via a jump table based on the `index`, avoiding the level of indirection associated with virtual tables; and it avoids 4 million heap allocations. While absolute timings vary by machine, the magnitude of "variant is faster" remains robust.

Of course, this scenario was designed for comparison—a closed set of shapes with dense traversal. If we switch to a "plugin-style extension where external modules add new types dynamically," inheritance polymorphism is still the way to go. The criterion is simple: **Can you list all types upfront? If yes, use variant; if not, use inheritance.**

A quick note on memory. The size of a `variant` equals the size of the largest alternative plus the index, aligned appropriately. Just like a `union`, you pay the memory cost for the largest member:

```text
sizeof(variant<int,double,string>) = 40   // 被 string(32) 主导 + 索引
sizeof(variant<int,int,int>)        = 8    // 三个 int 共用空间 + 索引
sizeof(variant<int>)                = 8    // 单个 int 也要带索引
sizeof(string)                      = 32
sizeof(int)                         = 4
sizeof(variant<char,char>)          = 2    // char + 1 字节索引
```

Note that `variant<int>` is not equivalent to `int`—even with a single alternative, the space for the index cannot be omitted. Similarly, `variant<int, int, int>` is 8 bytes, not 4: the three `int` types share the same memory, but the index must still record "which one is currently active."

## variant wants to "start empty": monostate

A common requirement is that the default constructor of a `variant` initializes the **first** alternative. However, if the first type lacks a default constructor (for example, if it requires arguments), the entire `variant` cannot be default constructed. In this case, we use a placeholder type, `std::monostate`, at the beginning:

```cpp
// Standard: C++17
struct NoDefault {
    NoDefault() = delete;
    NoDefault(int) {}
};

std::variant<std::monostate, NoDefault, int> v;  // 默认持 monostate, 可默认构造
std::cout << "default index=" << v.index() << " (0=monostate)\n";
v.emplace<2>(42);
std::cout << "emplace<2>(42) index=" << v.index() << "\n";
```

```text
default index=0 (0=monostate)
emplace<2>(42) index=2
```

`monostate` is an empty, default-constructible type whose sole purpose is to serve as an "empty state placeholder" for a `variant`. Note that this is different from `valueless_by_exception`—when holding a `monostate`, the `variant` is still considered to "have a value," and that value is `monostate`. `valueless` is the pathological state of "truly having no value." If you want "possibly no value" semantics, you should use `std::optional<T>` instead of cobbling together a `variant<monostate, T>`—`optional` has clearer semantics and a more convenient API (see the dedicated chapter on `optional`).

## Common Pitfalls

Let's round up the places where it's easy to run into trouble:

::: warning variant needs at least one alternative
`std::variant<>` (empty parameter pack) is illegal and will fail to compile. A `variant` must list at least one type—its guarantee of "always having a value" is built upon the premise of "having at least one alternative."
:::

::: warning get type must be in the alternative set
`std::get<long>(variant<int, double, string>)` is a **compile-time error**, not a runtime exception. `variant`'s type safety is achieved by ensuring "you can only retrieve declared types." To get by runtime index, use `get<I>()`; an out-of-bounds `I` is also a compile-time error.
:::

::: warning Don't use variant<monostate, T> to replace optional
It compiles and runs, but the semantics are convoluted. `optional<T>` expresses "has value or not" more directly, and the API (`has_value()`/`value()`/`value_or()`) is more ergonomic. `variant` means "one of these types"; using `monostate` to simulate "nothing" is overkill and harder to read.
:::

::: warning Memorize the overloaded incantation completely
Don't miss the `...` at the end of `using Ts::operator()...;`, and don't miss the deduction guide `overloaded(Ts...) -> overloaded<Ts...>;`. Missing them will either cause compilation failure or incomplete dispatch. These three lines are a boilerplate pattern; just copy them as-is.
:::

::: warning valueless appearing means your type's exception guarantee has a bug
Normal code should almost never see `valueless_by_exception() == true`. It only appears when "constructing a new value throws an exception and cannot roll back," which usually means one of your type's copy/move constructors threw an exception. Fix that constructor, don't write defensive code like `if (v.valueless_by_exception())` everywhere.
:::

## Summary

`std::variant` has a clear purpose—**a type-safe union providing value-semantics polymorphism for a closed set of types**. Let's wrap up with the key conclusions:

- Compared to a raw `union`: `variant` stores an extra index and manages destruction automatically. Reading the wrong type throws an exception instead of causing UB, at the cost of a few extra bytes for the index.
- Access quartet: `holds_alternative<T>()` to check, `get<T>()` to retrieve (throws on mismatch), `get_if<T>()` to get a pointer (returns `nullptr` on mismatch, no throw), and `index()` to see the current position. The type in `get` must be in the alternative set, or it is a compile error.
- `std::visit` + `overloaded` lambda is C++'s pattern matching: missing a type in the visitor results in a compile error, dispatch compiles to a jump table, avoiding `if-else` chains and virtual table indirection. In C++20, we can omit `overloaded` and use generic lambdas + `if constexpr`, as well as `visit<R>` to enforce a common return type.
- `valueless_by_exception()` is the only pathological state for a variant: triggered when constructing a new value throws and cannot roll back, causing `index()` to become `variant::npos`. Normal code shouldn't see this; if you do, your type's exception guarantees are flawed.
- `variant` vs. inheritance: Choose `variant` for a **closed** type set (value semantics, stack-based, no virtual function overhead, cache-friendly; benchmarks show traversing is 30-40% faster than virtual functions). Choose inheritance for an **open** set (add derived classes anytime without changing old code).
- For "possibly no value" semantics, use `optional`. Don't use `variant<monostate, T>` as a substitute.

Next, we will look at `std::any`—another way to "hold any type," and the fundamental difference between it and `variant` regarding "known vs. unknown type sets."

## References

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) — Overview of constructors, access, `index`, and exception guarantees
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit) — Visitor dispatch and the C++20 `visit<R>` form
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access) — Exception thrown on `get` type mismatch or when `valueless`
- [cppreference: std::variant::valueless_by_exception](https://en.cppreference.com/w/cpp/utility/variant/valueless) — Causes of the pathological state and `variant::npos`
- [cppreference: std::monostate](https://en.cppreference.com/w/cpp/utility/monostate) — Placeholder type allowing default construction of variants with non-default-constructible alternatives
