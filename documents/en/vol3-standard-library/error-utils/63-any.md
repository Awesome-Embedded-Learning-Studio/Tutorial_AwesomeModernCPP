---
title: "any: Holds Any Type — And Why You Probably Won't Need It"
description: "A deep dive into the type erasure machinery of `std::any` — the boundary between SBO inline storage and heap allocation, the two `any_cast` overloads and exact type matching, why `variant` should take the stage in most scenarios, and the few edge cases where `any` is truly irreplaceable"
chapter: 7
order: 63
cpp_standard:
  - 17
  - 20
difficulty: intermediate
platform: host
prerequisites:
  - "variant: Type-Safe Unions and visit"
  - 'optional: Making "Maybe Nothing" a Type'
related:
  - "variant: Type-Safe Unions and visit"
  - 'optional: Making "Maybe Nothing" a Type'
reading_time_minutes: 23
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
  - variant
  - optional
translation:
  source: documents/vol3-standard-library/error-utils/63-any.md
  source_hash: 360d15a562af55913704d41e8a9504efce6bb9b6aa10c084a5df1931b02963ca
  translated_at: '2026-09-26T00:53:47+00:00'
  engine: anthropic
  token_count: 10000
---

# any: Holds Any Type — And Why You Probably Won't Need It

C++ is a statically typed language: every variable's type is nailed down at compile time. Yet once in a while we do run into a requirement like this — we're holding a value whose type we can't pin down while writing the code. It might be an `int`, it might be a `std::string`, it might even be a user-defined type that we, the people writing the library, haven't defined yet. The standard library offers a fallback answer: `std::any` (C++17), a container that "can hold any `CopyConstructible` type".

Let's say this up front: the tone of this article is not "go use `any`" — quite the opposite. Among the standard library's three big type-erasure components (`optional` / `variant` / `any`), `any` keeps the lowest profile — in most places where you think `any` is the answer, `variant` is actually the better fit: more appropriate, safer, and faster. But `any` does have a few irreplaceable edge cases, and the mechanism behind "how do you stuff an arbitrary type into one single type" is itself worth cracking open for a look. So let's be honest about it: what `any` is, how it stores things, when it genuinely beats `variant`, and when using it is digging a pit for yourself.

## What any Actually Stores: The Plainest Form of Type Erasure

`std::any`'s outward promise is plain — one and the same `any` type can hold values of different types, one after another:

```cpp
// Standard: C++17
#include <any>
#include <iostream>

int main()
{
    std::any a = 1;           // holds an int
    std::cout << a.type().name() << ": " << std::any_cast<int>(a) << '\n';
    a = 3.14;                 // the same a, now holds a double
    std::cout << a.type().name() << ": " << std::any_cast<double>(a) << '\n';
    a = std::string("hi");    // now holds a string
    std::cout << a.type().name() << ": " << std::any_cast<std::string>(a) << '\n';
}
```

Run with `g++ -std=c++23 -O2` (local GCC 16.1.1):

```text
i: 1
d: 3.14
NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE: hi
```

Note that long, ugly `type().name()` — it's the ABI's internal mangled name: `i` is `int`, `d` is `double`, and that blob for `string` (starting with `NSt7...`) is a libstdc++ implementation detail, which changes across compilers and library versions. `type()` returns a `const std::type_info&`; you normally don't read its `name()` — you compare it against `typeid(T)`, a point we'll come back to when it's needed.

The thing to make clear here is: **how does `any` get away with letting a single static type hold different value types over time?** The answer is that it defers "which concrete type is this" from compile time to runtime, with a technique called type erasure. Inside, an `any` object stores two things: a chunk of storage for the value itself, plus a set of function pointers that "know what type this value is and how to copy/destroy it" (usually called the manager or handler in standard library implementations). At compile time you see `std::any` as one fixed type; at runtime it holds a blob of type information and dispatches through virtual functions or function pointers to invoke the right type's construction, copying, and destruction.

This explains `any`'s first hard constraint: **it can only hold `CopyConstructible` types**. `any` itself is copyable (copying an `any` copies what it holds), and to copy a value of unknown type, the standard library has to pre-provision a "how to copy it" function at type-erasure time. For a non-copyable type that function simply cannot be written, so it gets blocked at compile time. Let's try it for real — stuff a `MoveOnly` (containing a `unique_ptr`) in:

```cpp
// Standard: C++17
#include <any>
#include <memory>

struct MoveOnly {
    std::unique_ptr<int> p;
    MoveOnly() : p(std::make_unique<int>(1)) {}
};

int main()
{
    std::any a = MoveOnly{};   // compile error: MoveOnly is not copyable
    (void)a;
}
```

GCC 16.1.1 rejects it on the spot:

```text
error: conversion from ‘MoveOnly’ to non-scalar type ‘std::any’ requested
   12 |     std::any a = MoveOnly{};   // compile error: MoveOnly is not copyable
```

The error is blunt — `MoveOnly` cannot be converted to `std::any`. This foreshadows an essential difference between `any` and `variant`: `variant` only needs each of its candidate types to be destructible (copying/moving is on demand), while `any` demands copyability just to get in the door. So for move-only types like `unique_ptr`, `any` flat-out cannot do it, and you need another way out (for example, more recent erasure components like `std::move_only_function`).

## make_any and the Two Overloads of any_cast

To store a value, use `make_any<T>(args...)` or plain assignment; to get it back out, use `any_cast<T>`. `any_cast` has two overloads with sharply different behavior — one of the biggest traps in using `any`:

```cpp
// Standard: C++17
#include <any>
#include <iostream>
#include <string>

int main()
{
    std::any b = std::string("hello");

    // value form: throws std::bad_any_cast on type mismatch
    auto* sp = std::any_cast<std::string>(&b);   // pointer overload: returns nullptr on failure, doesn't throw
    auto* ip = std::any_cast<int>(&b);
    std::cout << "any_cast<string>(&b) = " << (sp ? sp->c_str() : "nullptr") << '\n';
    std::cout << "any_cast<int>(&b)    = " << (ip ? "non-null" : "nullptr") << '\n';

    try {
        [[maybe_unused]] auto v = std::any_cast<double>(b);  // value overload: b holds a string
    } catch (const std::bad_any_cast& e) {
        std::cout << "caught bad_any_cast: " << e.what() << '\n';
    }
}
```

Running it:

```text
any_cast<string>(&b) = hello
any_cast<int>(&b)    = nullptr
caught bad_any_cast: bad any cast
```

Two rules to burn in:

- **Pointer overload `any_cast<T>(&any)`**: pass the address of the `any`, get back a `T*` (or `const T*` for the const overload). If the type matches, it returns a pointer to the internal value; if not, it returns `nullptr` — **it never throws**. This is the form for "I want to check the type myself and handle failure myself".
- **Value overload `any_cast<T>(any)`**: directly returns a copy of `T` (or a reference). If the type doesn't match, it throws `std::bad_any_cast` without ceremony. This is the form for only when "I'm certain it holds a `T` — and if I'm wrong, the program can't continue anyway".

::: warning any_cast requires an exact type match and performs no conversions
`any_cast` compares strictly by `typeid` and **performs no implicit conversions**. Store an `int`, fetch with `any_cast<long>`, and you get `nullptr` (pointer form) or an exception (value form); store an `unsigned`, and `any_cast<int>` can't get it out either. Let's verify:

```text
stored int; any_cast<long>  -> nullptr
stored int; any_cast<double> -> nullptr
stored unsigned; any_cast<int>      -> nullptr
stored unsigned; any_cast<unsigned> -> ok
```

`int` vs `long`, `int` vs `unsigned int`, `int` vs `double` — types that implicitly convert to each other all day long in ordinary C++ are, to `any_cast`, **all different types**. This is the trap newcomers hit most often: casually writing `42u` (`unsigned`) when storing, then fetching as `int` and getting `nullptr` back, utterly baffled. Remember: the type argument of `any_cast` must match the type originally stored **to the letter**.
:::

## SBO: Small Objects Inline, Large Objects on the Heap

When we discussed type erasure, we buried a question: where exactly is that "storage for the value itself"? Does it heap-allocate every single time? The standard doesn't mandate anything, but cppreference states it plainly: *"Implementations are encouraged to avoid dynamic allocations for small objects"*. All three major implementations (libstdc++ / libc++ / MSVC STL) implement small buffer optimization (SBO), with the same ancestry as `std::string`'s SSO: the `any` object reserves a small inline buffer internally — if the value fits, it goes straight in; only when it doesn't fit does `any` go `new` on the heap.

Let's look at this boundary with real measurements. First, how big is `any` itself:

```cpp
// Standard: C++17
#include <any>
#include <array>
#include <iostream>
#include <string>

int main()
{
    std::cout << "sizeof(std::any)            = " << sizeof(std::any) << '\n';
    std::cout << "sizeof(void*)               = " << sizeof(void*) << '\n';
    std::cout << "sizeof(std::string)         = " << sizeof(std::string) << '\n';
    std::cout << "sizeof(std::array<char,64>) = " << sizeof(std::array<char,64>) << '\n';
}
```

Running it on libstdc++ 16:

```text
sizeof(std::any)            = 16
sizeof(void*)               = 8
sizeof(std::string)         = 32
sizeof(std::array<char,64>) = 64
```

`sizeof(std::any)` is **16 bytes**. Packed into those 16 bytes: an inline buffer (to hold small objects directly), plus one function pointer (pointing to the manager that "knows how to manage this value"). The two share quarters, so the payload size that can actually be stored inline is far smaller than 16 — the pointer itself takes up room. So how large an object can fit before hitting the heap? We'll sweep it with a small probe that decides "is the value inside the `any` object" from the address difference:

```cpp
// Standard: C++17
#include <any>
#include <array>
#include <cstdio>
#include <cstddef>

template <std::size_t N>
struct Blob {
    std::array<unsigned char, N> data{};
};

template <std::size_t N>
void probe()
{
    std::any a = Blob<N>{};
    auto* p = std::any_cast<Blob<N>>(&a);
    // small delta => value lives inside the any object (SBO); large/heap-looking delta => heap allocation
    long delta = (long)((char*)p - (char*)&a);
    std::printf("N=%3zu  sizeof(Blob)=%3zu  -> %s\n",
                N, sizeof(Blob<N>),
                (delta >= 0 && delta < 32) ? "INLINE (SBO)" : "HEAP");
}

int main()
{
    probe<1>();  probe<8>();  probe<12>();  probe<16>();  probe<32>();  probe<64>();
}
```

Running it:

```text
N=  1  sizeof(Blob)=  1  -> INLINE (SBO)
N=  8  sizeof(Blob)=  8  -> INLINE (SBO)
N= 12  sizeof(Blob)= 12  -> HEAP
N= 16  sizeof(Blob)= 16  -> HEAP
N= 32  sizeof(Blob)= 32  -> HEAP
N= 64  sizeof(Blob)= 64  -> HEAP
```

libstdc++ 16's SBO cutoff is refreshingly blunt: **objects no bigger than one pointer (8 bytes) go inline; anything larger goes to the heap**. 12 bytes already overflows. This is a fact worth internalizing — it means common scalars like `int`, `double`, and raw pointers enter `any` without allocating, but `std::string` (whose `sizeof` is 32, and which carries its own SSO), `std::vector`, or any struct with some heft triggers a heap allocation the moment you stuff it into an `any`.

Now let's quantify the cost of SBO versus heap allocation with an allocation counter. The following snippet overloads global `operator new` to count exactly how many times `any` allocates:

```cpp
// Standard: C++17
#include <any>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

static std::size_t g_alloc_count = 0;
void* operator new(std::size_t n) { ++g_alloc_count; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    constexpr int N = 1'000'000;

    g_alloc_count = 0;
    {
        std::vector<std::any> as;
        as.reserve(N);
        for (int i = 0; i < N; ++i) as.emplace_back(i);   // int -> SBO, should involve no extra allocations
    }
    std::cout << "any(int)  [SBO]:  allocs during build = " << g_alloc_count << '\n';

    struct Big { std::int64_t d[8]; };   // 64 bytes, guaranteed to hit the heap
    g_alloc_count = 0;
    {
        std::vector<std::any> as;
        as.reserve(N);
        for (int i = 0; i < N; ++i) as.emplace_back(Big{i,0,0,0,0,0,0,0});
    }
    std::cout << "any(Big64)[heap]: allocs during build = " << g_alloc_count << '\n';
}
```

Running it:

```text
any(int)  [SBO]:  allocs during build = 1
    (that 1 allocation is the vector's own capacity reserve)
any(Big64)[heap]: allocs during build = 1000001
    (1 reserve + 1 heap allocation per element)
```

Numbers don't lie. With `int`, a million elements cost 0 extra allocations (SBO inlined them all); with 64-byte objects, **every single element gets its own heap allocation** — one million of them. That's the real cost of storing large objects in `any`: it's not just slower access — construction alone hammers the allocator flat. If you're putting large objects in `any` on a hot path, that's a genuine performance problem.

## Compared with variant: Why You Should Usually Use variant

At this point we can answer the opening question head-on: if `any` is so flexible, why do we say you'll "mostly never need it"? Because it trades away the compile-time type safety of `optional` / `variant` for runtime type erasure — and the bill lands entirely on you. Let's put `variant` and `any` side by side.

First, **is the set of types open or closed**. `variant<int, double, string>` pins the candidates down to three — a "closed set": when you write the code, you know the value can only be one of these three, and the compiler knows too. That's how `std::visit` can guarantee you've handled every branch, and how accessing via `std::get<T>` is, to a large extent, checkable at compile time (a miss throws `bad_variant_access`, but because you wrote out the type list, mistakes are far less likely). `any` is an "open set" — any `CopyConstructible` type can go in, the compiler can't cross-check for you, and whether the type is right is known **only at that one runtime `any_cast` moment** — get it wrong and you eat an exception or a `nullptr`.

Second, **can you traverse all the possibilities**. `variant` paired with `std::visit` lets you write code that says "whichever candidate is in there right now, I handle it uniformly":

```cpp
// Standard: C++17
#include <iostream>
#include <variant>

int main()
{
    std::variant<int, double, std::string> v = std::string("hi");
    std::visit([](auto&& x) { std::cout << "variant holds: " << x << '\n'; }, v);
}
```

```text
variant holds: hi
```

`any` has no equivalent — it has no idea what the "set of candidate types" even is, so there's nothing to traverse. Either you **spell out the type explicitly at the call site** (`any_cast<std::string>(a)`), or you play guessing games with `if (a.type() == typeid(X)) ... else if ...` one at a time. This is precisely the cost of type erasure: the type information vanished from the signature, so every job that "uses type information" has to be done over by you, by hand, at the call site.

Third, **performance**. Let's compare: storing a million `int`s, `variant<int,double>` retrieval vs `any` retrieval, running `get` / `any_cast` a million times each:

```text
variant<int,double>: access 1000000 ints = 1259 us
any(int) [SBO]:      any_cast<int> access 1000000 ints = 1340 us
```

(Absolute values fluctuate per machine; here we only look at order of magnitude.) Access time is actually about the same for both — they both follow the "read a type tag, then dispatch" routine. `variant`'s advantage isn't faster single access; it's the **zero extra allocation and compile-time checkability that come from a known type set**: `variant` never allocates (its size is just "largest candidate + one index"), while `any` heap-allocates for large objects; with `variant`, the compiler can warn about a wrong type — with `any`, a wrong type only blows up at runtime.

So here's a very practical rule of thumb: **whenever you can list all the possible types, always use `variant`**. `variant`'s type set is closed, visible at compile time, and allocation-free; `any` only means anything when "even you don't know what types might show up".

## So When Is any Really the Right Call

After all this "don't use it", `any` is by no means a useless ornament. The scenarios where it's genuinely irreplaceable share one trait: **the set of types is open, and the consuming side doesn't care which specific type it is**. The two most typical:

**Property tables / configuration tables**. A configuration system has to hold values of all sorts of types — the timeout is an `int`, the hostname is a `string`, the retry count is an `unsigned`, some toggle is a `bool` — and whoever writes the configuration framework can't possibly foresee the types of every config item. For this "key-value pairs with wildly varied value types" situation, `map<string, any>` is a natural landing spot:

```cpp
// Standard: C++17
#include <any>
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, std::any> props;
    props["timeout"] = 30;                       // int
    props["host"]    = std::string("localhost"); // string
    props["retries"] = 3u;                       // unsigned

    auto get_int = [&](const std::string& key) -> int {
        auto it = props.find(key);
        if (it == props.end()) return -1;
        auto* p = std::any_cast<int>(&it->second);   // pointer overload, safe retrieval
        return p ? *p : -1;
    };

    std::cout << "timeout=" << get_int("timeout")
              << "  host=" << std::any_cast<std::string>(props["host"])
              << "  retries-as-int=" << get_int("retries") << '\n';
}
```

Running it:

```text
timeout=30  host=localhost  retries-as-int=-1
```

Note the `retries-as-int=-1` slot — `retries` holds an `unsigned`, `any_cast<int>` can't pull it out, the pointer overload returns `nullptr`, and we safely fall back to `-1`. This is exactly the right way to consume an `any` property table: the consumer does defensive retrieval with the **pointer overload**, so a type mismatch has clear failure semantics instead of one exception taking down the whole program. It also echoes the earlier warning — `int` and `unsigned` are two different types inside `any`, and storage and retrieval must correspond strictly.

**A "value envelope" crossing a boundary**. When a value has to cross a boundary you can't intervene in — some message system, some scripting binding layer, some plugin interface — and you just want to pass "a value" through without caring what it specifically is, `any` is a type-agnostic envelope. Whoever receives it unpacks it afterwards in whatever way they know. In this scenario both conditions hold — "open type set" plus "nobody cares about the specific type" — and `any` is genuinely the right fit.

Conversely, the following scenarios are **not** reasons to use `any` — switch to `variant`:

- "This value might be A or B or C" — the types are listable, so use `variant<A,B,C>`.
- "This value might be absent" — use `optional<T>`.
- "I want to store a bunch of objects of different types" — if you can list them while writing the code, use `variant`; only when you truly can't (say, a configuration framework) does `any` get its turn.

## Compared with void* and Templates: Three Roads to Type Erasure

Putting `any` back into the larger context of "type erasure" makes its position clearer. C++ has three roads for hiding the concrete type:

- **`void*`**: the most primitive, the most dangerous. Any pointer can convert to `void*` and back, but the type information is **utterly lost** — cast to the wrong type and the compiler won't say a word, and runtime hands you straight-up undefined behavior. You can read `any` as "a safe `void*` that carries its type information" — it remembers internally what the original type was, `any_cast` cross-checks against `typeid`, and a mismatch throws instead of silently invoking UB.
- **Templates**: types stay at compile time — zero runtime overhead, zero type erasure — but the price is that "with templated code, every type must be known at the call site", and template code instantiates into multiple copies. Templates suit "types fully pinned down at compile time"; `any` suits "can't pin the type down at compile time". The two aren't in conflict.
- **`any` / `variant` / `function`**: the type-erasure components the standard library provides. `any` erases "the concrete type of a single value"; `variant` writes out the type set and then erases the discriminant; `function` erases "the concrete type of a callable object". What they share: fix a signature/shell at compile time, defer the "which concrete type" detail to runtime, yet keep type-safe access (a miss throws rather than UB).

So `any` isn't a fashionable wrapper around `void*` — it's a safety component with runtime type cross-checking. Nor is it the opposite of templates; it fills in the patch of ground templates can't reach: "type unknown at compile time". Once you understand where it sits among these three roads, you know when to pick it and when not to.

## Pitfalls You Will Actually Hit

Let's collect the pitfalls from along the way — each one verified by the measurements above:

::: warning any_cast demands the exact type and performs no conversions
Fetching `int` as `long`, `unsigned` as `int`, `int` as `double` — inside `any_cast`, **all of them fail**. The pointer overload returns `nullptr`; the value overload throws `bad_any_cast`. Remember that `any_cast`'s template parameter must match the stored type to the letter: if you casually wrote a literal when storing (`42u` is `unsigned`, `42` is `int`), you'd better match it when fetching.
:::

::: warning any only holds CopyConstructible types
Non-copyable types (those containing `unique_ptr`, or with deleted copy constructors) won't even compile. `variant` doesn't have this restriction — it only asks that candidate types be destructible. For move-only types, `any` can't help you; consider `std::move_only_function` (C++23) or writing your own erasure layer.
:::

::: warning Storing large objects = one heap allocation per element
libstdc++'s SBO only fits a pointer-sized (8-byte) payload; anything bigger goes to the heap. Storing `string`/`vector`/sizable structs means one allocation each at construction — beware on hot paths. If the type is predictable, don't use `any`; `variant` allocates zero times.
:::

::: warning Don't use any as a variant replacement
This is the most common and best-camouflaged misuse. Whenever the candidate types can be listed, use `variant` + `visit`/`get`: compile-time checkable, zero allocation, traversable. Save `any` for the edge cases where "the type set is open and the consumer doesn't care about the specific type" (property tables, cross-boundary value envelopes).
:::

## Summary

`std::any` is the standard library's catch-all container that "can hold any `CopyConstructible` type", but within the `optional` / `variant` / `any` trio, it's the one to treat with the most caution. The key takeaways:

- `any` uses type erasure to defer "which concrete type it is" to runtime: internal storage plus a set of manager function pointers — and therefore **only holds `CopyConstructible` types**; non-copyable types (e.g. those containing `unique_ptr`) are blocked at compile time.
- `any_cast<T>` has two overloads: the value form throws `bad_any_cast` on a type mismatch; the pointer form (passing `&any`) returns `nullptr` without throwing. Consumers doing defensive retrieval use the pointer overload. Either way, **exact type matching is required — no implicit conversions whatsoever**.
- SBO: libstdc++ 16's inline cutoff is 8 bytes (one pointer); `int`/`double`/raw pointers inline with zero allocation, while `string` and anything bigger each hit the heap. Measured: storing 1 million 64-byte objects = 1 million heap allocations.
- Most scenarios call for `variant`: closed candidate set, compile-time checkable, zero allocation, `visit`-traversable. `any`'s irreplaceable niche is "open type set and a consumer that doesn't care about the specific type" — typified by configuration/property tables and cross-boundary value envelopes.
- In the type-erasure spectrum, `any` is "a safe `void*` with runtime type cross-checking", with a clear division of labor from templates (compile time, zero overhead) — not an either/or choice.

One sentence to close: **before writing `any`, ask yourself "can I list every possible type?" — if you can, use `variant`; only if you can't, reach for `any`.** That one habit blocks nine out of ten misuses of `any`.

## References

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any) — the specification of the type-erasure container, the CopyConstructible requirement, and the SBO note that implementations are encouraged to avoid dynamic allocation for small objects
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast) — the two overload families: the value form throws `bad_any_cast`, the pointer form returns `nullptr`
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast) — the exception thrown by the value overload on a type mismatch
- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) — the discriminated union with a closed type set, the better replacement for `any` in most scenarios
