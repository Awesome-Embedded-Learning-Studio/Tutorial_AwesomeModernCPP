---
title: "variant: Type-Safe Unions and visit"
description: "A thorough account of why std::variant replaces the tag-toting bare union—automatic destruction and index tracking, how to choose among get/get_if/holds_alternative, overloaded lambdas paired with std::visit for pattern matching, the pathological valueless_by_exception state, and why variant is more value-semantic and more memory-efficient than inheritance polymorphism for closed type sets"
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
  - "Object Size, Alignment, and Trivial Types"
  - "Deep Dive into std::vector: Three Pointers, Reallocation, and Iterator Invalidation"
related:
  - "Container Selection Guide: Choosing the Right Container Based on Operations, Memory, and Invalidation Rules"
reading_time_minutes: 16
translation:
  source: documents/vol3-standard-library/error-utils/62-variant.md
  source_hash: 6ef559cb6c1f778803507e58d9e75246e644a9d909b6a5a128076dd331094323
  translated_at: '2026-09-26T00:54:57+00:00'
  engine: anthropic
  token_count: 11000
---

# variant: Type-Safe Unions and visit

We've all written plenty of code where "one and the same variable is sometimes A, sometimes B." In a state machine, a connection might be `Connecting`, `Connected`, or `Error`; in a parser, a token might be a number, a string, or a symbol; a config entry might be a scalar, or a list. Traditionally there have been two routes: either rig up an `enum` plus a `union` and keep track of "which one is in there right now" yourself, or set up an inheritance hierarchy—`class Shape` with `Circle`, `Square`, and `Triangle` hanging underneath—and dispatch through virtual functions.

Both routes hurt in their own way. A `union` doesn't remember which type it currently holds—stuff an `int` in, read it back out as a `string`, and the compiler doesn't make a sound while the runtime sails straight into undefined behavior; destruction is an even murkier account (the `string` destructor that should have run doesn't, and the memory leaks). Inheritance polymorphism, for its part, is type-safe, but every object has to be `new`-ed onto the heap and lug around a vtable pointer—just to "store one value" you've spent a heap allocation and an indirect jump, and now you also have to babysit lifetimes.

C++17 opened a third route: `std::variant<Ts...>`, a **type-safe union**. On top of the union's "everyone shares one block of memory," it additionally records an index of "which alternative is currently active," and it manages destruction automatically. In this article we walk from "why not a bare union" all the way through pattern matching with `std::visit` and the weird `valueless_by_exception` state, then close with a head-to-head performance comparison against inheritance polymorphism.

## Why Not a Bare union

First, let's see exactly where a bare `union` rots. The following code won't draw a single warning out of the compiler, and it is simply wrong:

```cpp
// Standard: C++98
union BadUnion {
    int i;
    std::string s;   // a union with a non-trivial member
};

void misuse() {
    BadUnion u;
    u.s = std::string("hello");   // store as string
    int x = u.i;                  // read as int — undefined behavior
    // function ends: nobody calls the string destructor; memory leaks
}
```

The `union` itself **does not know** whether it currently holds an `int` or a `string`. Reading a `string` as an `int` is UB; the `string` destructor that should run never runs—that's a leak. To use one correctly, the programmer has to hang a tag on the outside, check it by hand, destroy by hand—an entire apparatus of boilerplate whose correctness rests entirely on human discipline. We've seen far too much code that "figured a union would save memory" and left a pile of memory leaks behind.

`std::variant` automates the whole apparatus. It does two things:

1. **Record the index**: internally it stores a subscript for "which of the alternative types is current." `index()` reads it out, and `holds_alternative<T>()` asks about it directly.
2. **Automatic destruction**: every time the type changes (assignment, `emplace`), it destroys the old alternative first and then constructs the new one. When its own lifetime ends, it destroys whichever alternative it currently holds.

The price is a little extra storage for that index (usually just a few bytes); what you buy back is "reading the wrong type throws an exception instead of being UB, and destruction is always right."

## Construction and Access: The Four Tools

For the most basic usage, let's just run it straight through:

```cpp
// Standard: C++17
#include <variant>
#include <string>
#include <iostream>

int main()
{
    std::variant<int, double, std::string> v;  // default-constructed -> holds the first type (int)
    std::cout << "默认构造 index=" << v.index() << " (int)\n";

    v = 3.14;                                  // assign a double
    std::cout << "赋值 3.14 index=" << v.index() << " (double)\n";

    v = std::string("hello");                  // assign a string
    std::cout << "赋值 hello index=" << v.index() << " (string)\n";

    // 1. holds_alternative<T>: is the current alternative a T?
    std::cout << "holds<string>=" << std::holds_alternative<std::string>(v) << "\n";
    std::cout << "holds<int>="    << std::holds_alternative<int>(v) << "\n";

    // 2. get<T>: fetch the value; throws bad_variant_access on type mismatch
    std::cout << "get<string>=" << std::get<std::string>(v) << "\n";
    try {
        std::cout << std::get<int>(v) << "\n";   // currently string, fetching int
    } catch (const std::bad_variant_access& e) {
        std::cout << "异常: " << e.what() << "\n";
    }

    // 3. get_if<T>: fetch a pointer; returns nullptr on mismatch (no throw)
    if (auto* p = std::get_if<double>(&v)) {
        std::cout << "double: " << *p << "\n";
    } else {
        std::cout << "不是 double, get_if 返回 nullptr\n";
    }

    // 4. get<I>: fetch by index (0=int, 1=double, 2=string)
    std::cout << "get<2>=" << std::get<2>(v) << "\n";
    return 0;
}
```

Running it with `g++ -std=c++23 -O2` (local GCC 16.1.1):

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

How to pick among the four comes down to one question: "what do you want to happen when the type doesn't match?"

- **You want an exception thrown**: use `get<T>()`. Clean, but every access pays a branch plus possible exception overhead.
- **No throw, you handle it**: use `get_if<T>()`—a returned `nullptr` tells you the type is wrong. In performance-sensitive or exception-disabled code, this is the sturdier choice.
- **Check only, don't fetch**: `holds_alternative<T>()` returns a `bool` and reads the clearest.
- **By position rather than by type**: `get<I>()`. Occasionally useful—for instance, when walking a sequence of indices known at compile time.

::: warning The "type" given to get and get_if must be one of the alternatives
`std::get<long>(v)` on a `variant<int, double, string>` is a **compile error**—`long` is not in the alternative set. `variant`'s type safety comes precisely from "you can only fetch the types it declared," unlike a bare `union`, which you can read as whatever you please.
:::

## std::visit: Turning if-else Chains into Pattern Matching

At this point you might object: the four tools are enough—just write a pile of `if (holds_alternative<A>) ... else if (holds_alternative<B>) ...` and call it a day? It runs, but there are problems. First, it's ugly—every new type means coming back to edit that chain, and forgetting means a missed case. Second, it's slow—every access is a `holds_alternative` branch. Third, the compiler won't help you check "has every type been handled."

`std::visit` solves exactly those three. It feeds a "visitor" function object to the `variant` and requires that this visitor be able to handle **every single** alternative—miss one and compilation fails. Before we run a minimal example, let's first introduce the key technique that makes it genuinely pleasant: the **overloaded lambda**.

The visitor has to be an object whose `operator()` can be invoked for every alternative type. The most direct way to write one is a hand-rolled `struct`:

```cpp
// Standard: C++17
struct Describe {
    std::string operator()(int i) const { return "int:" + std::to_string(i); }
    std::string operator()(double d) const { return "double:" + std::to_string(d); }
    std::string operator()(const std::string& s) const { return "string:\"" + s + "\""; }
};
```

It works, but every new branch means trudging back into that `struct` to add another member function—verbose. C++17 has a much cleaner spelling: inherit a bunch of lambdas together so they add up to one function object that can match all the types:

```cpp
// Standard: C++17
template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
```

These three lines are the C++ community's "common incantation" (`using Ts::operator()...` is C++17's pack-using declaration, and the deduction guide lets a set of lambdas deduce straight to `overloaded<L1, L2, ...>`). Teamed up with `std::visit`, that `Describe` above turns into a set of in-place lambdas:

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

The power of this section: `std::visit` knows at compile time exactly which types the `variant` can hold, and the visitor's `operator()` overload set is likewise fully known at compile time, so it can **compile the entire dispatch into one jump table** (usually a single indirect jump on `index()`)—no runtime `holds_alternative` chain, and none of inheritance's vtable indirection either. Better still: the moment `Value` grows a fourth type and you forget to handle it in `overloaded`, **compilation fails outright**. That's the compiler auditing your coverage for you—far safer than a hand-written `if-else` chain.

::: warning Don't misremember the three-line overloaded incantation
The trailing `...` in `using Ts::operator()...;` must not be dropped—it means "bring every base class's `operator()` into scope," and without it you've imported just one, leaving the dispatch incomplete. Don't forget the deduction guide `overloaded(Ts...) -> overloaded<Ts...>;` either—without it you can't construct `overloaded{...}` in place. This spelling still holds after C++20; it's the community's most battle-tested idiom.
:::

## C++20's Two New Tools: In-Place Lambdas + visit\<R\>

By C++20, the `overloaded` incantation above can actually be skipped—just take a single generic lambda with `if constexpr` and write "see a type, do the thing" in place:

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

The generic-lambda-plus-`if constexpr` approach has the advantage that "branches need not share a return type"; the disadvantage is that each branch has to spell out `is_same_v` itself, so it's less tidy than `overloaded`. Both spellings work—pick by scenario: few branches with roughly similar return types, use `overloaded`; complex per-branch logic or differing return types, use the generic lambda.

C++20 also gave `std::visit` an explicit-return-type form, `std::visit<R>(...)`, which "forces every branch's return value to convert to one common type `R`." It's handy when the branches naturally return different types but you want a common one (everything as `double`, say):

```cpp
// Standard: C++20
std::variant<int, double> v = 2;
double r = std::visit<double>([](auto x){ return x; }, v);  // the int branch converts to double too
```

Both C++20 spellings verified working on GCC 16.1.1. Note: C++23 did **not** give `variant` the monadic interface that `optional`/`expected` got (`.and_then` / `.transform` / `.or_else`). `variant` is not like `optional`, which has "empty" semantics—it always holds a value (apart from the pathological state covered below), so by design no monadic chain was crammed in. For that machinery, go read the dedicated `optional` and `expected` articles.

## valueless_by_exception: variant's Only Pathological State

We've been saying all along that "a variant always holds a value," and that's nearly true—with one exception. `variant` has a state called `valueless_by_exception()`, literally "the variant lost its value because of an exception." Sounds bizarre: how does a type that claims to always have a value end up with none?

It goes back to the exception guarantees of assignment/`emplace`. When you execute `v = new_value`, the `variant` must do two things: destroy the old value, then construct the new one. If the "construct the new value" step throws, and the implementation cannot bring the old value back, the `variant` is stranded in an awkward in-between state—the old one is gone, the new one never came to be. At that moment it is `valueless`.

Let's manufacture one on purpose:

```cpp
// Standard: C++17
#include <variant>
#include <iostream>
#include <stdexcept>

struct S {
    S() = default;
    S(const S&) { throw std::runtime_error("copy throw"); }  // the copy constructor always throws
};

int main()
{
    std::variant<double, S> v = 1.5;   // currently holds double
    std::cout << "before index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    S src;                              // default construction is fine
    try {
        v = src;                        // copy-construct S -> throws
    } catch (const std::runtime_error& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
    std::cout << "after index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    if (v.valueless_by_exception()) {
        try {
            (void)std::get<double>(v);   // even the original double is no longer retrievable
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

That intimidating `18446744073709551615` is `variant::npos` (`(size_t)-1`, i.e., 2^64-1), the sentinel value `index()` reports in the `valueless` state. Once you're in this state, not even the original `double` comes back—`get<double>` throws `bad_variant_access` too, and the error message says outright: `variant is valueless`.

How likely are you to run into this state? Honestly: rarely. It takes "constructing the new value throws + the implementation cannot roll back," and the cases where the standard library lets an implementation roll back (when the new value is nothrow-copyable, for instance) never go valueless. What actually triggers it is usually a hand-written type of your own whose copy/move constructor throws. In engineering practice you can treat this state as "shouldn't happen; if it shows up, your type's exception guarantee has a bug"—`valueless_by_exception()` is mainly a self-check hook left for people writing libraries. If business code ever sees it, fixing the throwing constructor is more right than handling valueless.

## variant vs Inheritance Polymorphism: Which One for a Closed Set

Mechanics done; now for the most practical question of all: when do you use `variant`, and when do you use inheritance polymorphism? It comes down to one phrase—**is the set of types closed or open**.

Inheritance polymorphism is strong at **openness**: the base class fixes the interface, and anyone can add a new derived class without touching existing code. You hold an array of `Shape*`; tomorrow a `Hexagon` joins, and not one line of the old code changes. The cost: every call goes through vtable indirection, objects usually live on the heap (one extra allocation), and cache locality suffers.

`variant` is strong at **closedness**: every possible type is nailed down at compile time (`variant<A, B, C>`); adding a new type means editing that declaration, and every visitor has to grow a matching branch—which, flipped around, is a **benefit**: the compiler forces you to handle the new type; nothing slips through. On top of that, `variant` is value-semantic, stored on the stack, free of virtual-function overhead, and visitor dispatch compiles down to a compact jump table—cache friendly.

Let's do a head-to-head comparison on a closed "set of shapes." Three shapes—`Circle`/`Square`/`Triangle`—compute the area: one version with inheritance + virtual functions, one with `variant` + `visit`, 4 million objects, three rounds each:

```cpp
// Standard: C++17
// Inheritance: ShapeBase virtual function area(); variant: visit + AreaVisitor
// (full code at /tmp/variant_lab/perf.cpp; key skeleton shown here)
struct CircleV { double r; };
struct SquareV { double s; };
struct TriangleV { double b, h; };
using ShapeV = std::variant<CircleV, SquareV, TriangleV>;

struct AreaVisitor {
    double operator()(const CircleV& c)    const { return 3.14159265 * c.r * c.r; }
    double operator()(const SquareV& sq)   const { return sq.s * sq.s; }
    double operator()(const TriangleV& t)  const { return 0.5 * t.b * t.h; }
};

// Inheritance version: for (auto& p : poly) acc += p->area();
// variant version: for (auto& v : vars) acc += std::visit(AreaVisitor{}, v);
```

Local GCC 16.1.1, `-O2`, two full runs:

```text
shapes: 4000000 x3 iters
inheritance (virtual): 87 ms
variant + visit:       54 ms
shapes: 4000000 x3 iters
inheritance (virtual): 78 ms
variant + visit:       55 ms
```

`variant + visit` comes out roughly 30%–40% faster. The gap comes mainly from three places: in the `variant` version the shapes sit packed shoulder to shoulder in a `vector` (the inheritance version is a `vector<unique_ptr>`, with pointers scattered all over the heap—cache misses); `visit` dispatch is a single jump-table hop on `index`, without the vtable's layer of indirection; and there are no 4-million heap allocations. Absolute times will wobble from machine to machine, but the order of magnitude of "variant is faster" is robust.

Of course, this scenario was designed for the comparison—a closed set of shapes, objects traversed densely. Swap in "plugin-style extension, external modules adding new types at any moment," and inheritance polymorphism is still the right tool for the job. The criterion is a single line: **can you list every type up front? If you can, use variant; if you can't, use inheritance.**

A word on memory while we're at it. A `variant`'s size is "largest alternative + the index" after alignment—same as a `union`, you foot the bill for the biggest one:

```text
sizeof(variant<int,double,string>) = 40   // dominated by string (32) + the index
sizeof(variant<int,int,int>)        = 8    // three ints share the space + the index
sizeof(variant<int>)                = 8    // even a single int carries the index
sizeof(string)                      = 32
sizeof(int)                         = 4
sizeof(variant<char,char>)          = 2    // char + 1-byte index
```

Note that `variant<int>` is not an `int`—even with only one alternative, that little bit of index storage cannot be shaved off. `variant<int, int, int>` is likewise 8 bytes, not 4: the three `int`s share the same memory, but the index still has to record "which one is alive right now."

## When variant Needs to Start "Empty": monostate

Here's a common need: a default-constructed `variant` holds the **first** alternative. But if that first type has no default constructor (it insists on arguments, say), the entire `variant` can no longer be default-constructed. The fix: put a placeholder type, `std::monostate`, at the head:

```cpp
// Standard: C++17
struct NoDefault {
    NoDefault() = delete;
    NoDefault(int) {}
};

std::variant<std::monostate, NoDefault, int> v;  // holds monostate by default; default-constructible
std::cout << "default index=" << v.index() << " (0=monostate)\n";
v.emplace<2>(42);
std::cout << "emplace<2>(42) index=" << v.index() << "\n";
```

```text
default index=0 (0=monostate)
emplace<2>(42) index=2
```

`monostate` is an empty, default-constructible type whose sole reason to exist is to serve as the `variant`'s "empty-state placeholder." Note that it is not the same thing as `valueless_by_exception`—while holding `monostate`, the `variant` "has a value," and that value is `monostate`; `valueless` is the pathological "truly no value." If what you're after is "maybe nothing" semantics, you should really reach for `std::optional<T>` instead of jury-rigging `variant<monostate, T>`—`optional` has more direct semantics and a handier API (see the dedicated `optional` article).

## A Few Pitfalls You'll Actually Hit

Let's gather up, in one place, the spots where this whole route tends to go off the road:

::: warning variant requires at least one alternative
`std::variant<>` (an empty parameter pack) is ill-formed and fails to compile. A `variant` must list at least one type—its "always holds a value" guarantee is built precisely on "there is at least one alternative."
:::

::: warning get's type must be in the alternative set
`std::get<long>(variant<int, double, string>)` is a **compile error**, not a runtime exception. `variant`'s type safety is implemented as "only declared types can be fetched." To fetch by the current index, use `get<I>()`—an out-of-range `I` is likewise a compile error.
:::

::: warning Don't use variant<monostate, T> in place of optional
It compiles and runs, but the semantics are roundabout. `optional<T>` says "present or absent" more directly, and its API (`has_value()`/`value()`/`value_or()`) is more convenient. A `variant` is "one of these types"; swapping one of them out for `monostate` to fake "absent" is a sledgehammer for a walnut—and harder to read.
:::

::: warning Memorize the overloaded incantation in full
The trailing `...` in `using Ts::operator()...;` must not be dropped, and the deduction guide `overloaded(Ts...) -> overloaded<Ts...>;` must not be dropped either. Miss either and you get a compile failure or an incomplete dispatch. These three lines are a fixed form—copy them as they are.
:::

::: warning Seeing valueless means your type's exception guarantee has a bug
Healthy code should almost never see `valueless_by_exception() == true`. It appears only when "constructing the new value throws and there is no rollback," which usually means one of your types throws in its copy/move constructor. Fix that constructor; don't go writing piles of `if (v.valueless_by_exception())` defensive code.
:::

## Summary

`std::variant`'s position is clear: a **type-safe union that brings value-semantic polymorphism to closed type sets**. The key conclusions, collected:

- Versus a bare `union`: `variant` records an index and manages destruction automatically; reading the wrong type throws an exception instead of being UB, and the cost is a few extra bytes of index storage.
- The four access tools: `holds_alternative<T>()` to check, `get<T>()` to fetch the value (throws on mismatch), `get_if<T>()` to fetch a pointer (returns `nullptr` on mismatch, no throw), `index()` to see the current position. The type in `get` must be in the alternative set, or it's a compile error.
- `std::visit` + the `overloaded` lambda is C++'s pattern matching: a visitor that leaves one type unhandled fails to compile, dispatch compiles into a jump table, and there is no `if-else` chain or vtable indirection. C++20 further lets you skip `overloaded` and go straight to a generic lambda + `if constexpr`, and adds `visit<R>` to force a common return type.
- `valueless_by_exception()` is variant's only pathological state: triggered when constructing the new value throws with no rollback possible, at which point `index()` becomes `variant::npos`. Healthy code shouldn't see it; if you do, your type's exception guarantees are suspect.
- `variant` vs inheritance: **closed** type set, pick `variant` (value semantics, on the stack, no virtual-function overhead, cache friendly—measured traversal 30%–40% faster than virtual dispatch); **open**, pick inheritance (add derived classes at any time, old code untouched).
- For "maybe no value," use `optional`; don't jury-rig `variant<monostate, T>`.

Next up we go look at `std::any`—the other way to "hold an arbitrary type"—and the fundamental divide between it and `variant`: whether the set of types is known or unknown.

## References

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) — construction, access, `index`, and an overview of the exception guarantees
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit) — visitor dispatch and the C++20 `visit<R>` form
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access) — the exception thrown when `get` type-mismatches or the variant is `valueless`
- [cppreference: std::variant::valueless_by_exception](https://en.cppreference.com/w/cpp/utility/variant/valueless) — how the pathological state arises, and `variant::npos`
- [cppreference: std::monostate](https://en.cppreference.com/w/cpp/utility/monostate) — the placeholder type that lets a variant with non-default-constructible alternatives still be default-constructed
