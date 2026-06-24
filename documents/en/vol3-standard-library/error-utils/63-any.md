---
title: 'any: A Type That Can Hold Anything—And Why You Probably Don''t Need It'
description: A deep dive into the type erasure mechanism of `std::any`—the boundary
  between SBO (Small Buffer Optimization) inline storage and heap allocation, the
  two overloads of `any_cast` and exact type matching, why `variant` is usually the
  better choice in most scenarios, and the few edge cases where `any` is truly indispensable.
chapter: 7
order: 63
cpp_standard:
- 17
- 20
difficulty: intermediate
platform: host
prerequisites:
- variant：类型安全的判别联合
- optional：值可能不存在
related:
- variant：类型安全的判别联合
- optional：值可能不存在
reading_time_minutes: 11
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- variant
- optional
translation:
  source: documents/vol3-standard-library/error-utils/63-any.md
  source_hash: 2f340f08acc32bdaca8d76d453b4611e0fb934a36d2e0863511be8e2c3cfabf6
  translated_at: '2026-06-24T00:39:27.985747+00:00'
  engine: anthropic
  token_count: 3555
---
# `any`: Holding Any Type — And Why You Probably Won't Need It

C++ is a statically typed language where the type of every variable is set in stone at compile time. However, we sometimes encounter a requirement: we have a value, but its type is uncertain while writing the code — it could be an `int`, a `std::string`, or even a user-defined type that we as library writers haven't defined yet. The standard library offers a catch-all solution: `std::any` (C++17), a container that "can hold any `CopyConstructible` type."

Let's state this upfront: the tone of this article is not "go use `any` right now." Quite the opposite. Among the standard library's three major type erasure tools (`optional` / `variant` / `any`), `any` has the lowest profile — in most cases where you think you need `any`, `variant` is actually more appropriate, safer, and faster. However, `any` does have a few irreplaceable edge cases, and its mechanism of "how to stuff arbitrary types into the same type" is worth dissecting. So, let's honestly discuss: what `any` is, how it stores data, when it is truly better than `variant`, and when using it is just digging a hole for yourself.

## What `any` Actually Stores: The Most Basic Approach to Type Erasure

`std::any`'s external promise is simple — a single `any` type can hold values of different types at different times:

```cpp
// Standard: C++17
#include <any>
#include <iostream>

int main()
{
    std::any a = 1;           // 装 int
    std::cout << a.type().name() << ": " << std::any_cast<int>(a) << '\n';
    a = 3.14;                 // 同一个 a，现在装 double
    std::cout << a.type().name() << ": " << std::any_cast<double>(a) << '\n';
    a = std::string("hi");    // 现在装 string
    std::cout << a.type().name() << ": " << std::any_cast<std::string>(a) << '\n';
}
```

Here are the results obtained using `g++ -std=c++23 -O2` (local GCC 16.1.1):

```text
i: 1
d: 3.14
NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE: hi
```

Note that long, ugly `type().name()` — it is the internal name mangling of the ABI. `i` stands for `int`, `d` for `double`, and the string starting with `NSt7...` represents implementation details of libstdc++, which may vary across different compilers or library versions. `type()` returns a `const std::type_info&`. Generally, we don't read its `name()` directly; instead, we compare it with `typeid(T)`. We will discuss this usage later.

The key point to clarify here is: **How does `any` allow the same static type to hold different value types sequentially?** The answer is that it defers type information from compile time to runtime using a technique called type erasure. Internally, an `any` object stores two things: a storage area for the value itself, and a set of function pointers (often called a manager or handler in standard libraries) that "know what this type is and how to copy/destroy it." At compile time, `std::any` appears as a fixed type, but at runtime, it holds type information and relies on virtual functions or function pointer dispatching to invoke the correct constructor, copy, or destructor logic for the underlying type.

This explains the first hard constraint of `any`: **it can only hold types that are `CopyConstructible`**. Since `any` itself is copyable (copying an `any` copies the object it holds), the standard library must preserve the "how to copy it" function during type erasure to handle unknown types. For non-copyable types, this function cannot be generated, resulting in a compile-time error. Let's test this by trying to stuff a `MoveOnly` type (containing a `unique_ptr`) into it:

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
    std::any a = MoveOnly{};   // 编译失败：MoveOnly 不可拷贝
    (void)a;
}
```

GCC 16.1.1 immediately rejects this:

```text
error: conversion from ‘MoveOnly’ to non-scalar type ‘std::any’ requested
   12 |     std::any a = MoveOnly{};   // 编译失败：MoveOnly 不可拷贝
```

The error message is straightforward: `MoveOnly` cannot be converted to `std::any`. This foreshadows a fundamental difference between `any` and `variant`: `variant` only requires that its candidate types be destructible (copying/moving is optional), whereas `any` requires a type to be copyable just to "hold" it. Therefore, `any` cannot handle move-only types like `unique_ptr` at all; we must find another solution (such as type erasure wrappers like `std::move_only_function`).

## Two Overloads of make_any and any_cast

We use `make_any<T>(args...)` or direct assignment to store values, and `any_cast<T>` to retrieve them. `any_cast` has two overloads with very different behaviors, which is one of the biggest pitfalls when using `any`:

```cpp
// Standard: C++17
#include <any>
#include <iostream>
#include <string>

int main()
{
    std::any b = std::string("hello");

    // 值形式：类型不符抛 std::bad_any_cast
    auto* sp = std::any_cast<std::string>(&b);   // 指针重载：失败返回 nullptr，不抛
    auto* ip = std::any_cast<int>(&b);
    std::cout << "any_cast<string>(&b) = " << (sp ? sp->c_str() : "nullptr") << '\n';
    std::cout << "any_cast<int>(&b)    = " << (ip ? "non-null" : "nullptr") << '\n';

    try {
        [[maybe_unused]] auto v = std::any_cast<double>(b);  // 值重载：b 里是 string
    } catch (const std::bad_any_cast& e) {
        std::cout << "caught bad_any_cast: " << e.what() << '\n';
    }
}
```

Here is the output:

```text
...
```

```text
any_cast<string>(&b) = hello
any_cast<int>(&b)    = nullptr
caught bad_any_cast: bad any cast
```

Here are two rules to remember:

- **Pointer overload `any_cast<T>(&any)`**: Pass the address of the `any` object, and it returns `T*` (or `const T*` for the const overload). If the types match, it returns a pointer to the internal value; if they don't, it returns `nullptr`. **It never throws exceptions.** This is the form to use when "I want to check the type myself and handle failure myself."
- **Value overload `any_cast<T>(any)`**: Directly returns a copy (or reference) of `T`. If the types don't match, it throws `std::bad_any_cast`. This is the form to use only when "I am certain it contains `T`, and if I'm wrong, the program cannot continue."

::: warning any_cast requires exact type matching, no conversions
`any_cast` performs a strict comparison using `typeid` and **does not perform implicit conversions**. If you store an `int`, attempting to retrieve it with `any_cast<long>` returns `nullptr` (pointer form) or throws an exception (value form). Similarly, if you store an `unsigned int`, you cannot retrieve it with `any_cast<int>`. Let's verify this:

```text
stored int; any_cast<long>  -> nullptr
stored int; any_cast<double> -> nullptr
stored unsigned; any_cast<int>      -> nullptr
stored unsigned; any_cast<unsigned> -> ok
```

`int` versus `long`, `int` versus `unsigned int`, `int` versus `double`—types that undergo implicit conversions in ordinary C++ are considered **completely different types** by `any_cast`. This is the most common pitfall for beginners: storing `42u` (`unsigned`) casually, then retrieving it as `int`, only to get `nullptr` and be left confused. Remember: the type parameter of `any_cast` must match the originally stored type **exactly**.
:::

## SBO: Inline Small Objects, Heap Allocate Large Ones

When discussing type erasure, we left a question unanswered: where exactly is that "storage area for the value itself"? Is it heap allocated every time? The standard does not mandate this, but cppreference explicitly states: *"Implementations are encouraged to avoid dynamic allocations for small objects"*. The three major implementations (libstdc++, libc++, and MSVC STL) all implement Small Buffer Optimization (SBO). The mechanism is similar to the SSO in `std::string`: the `any` object reserves a small internal inline buffer. If the object fits, it goes directly inside; if it doesn't, only then is it `new`ed on the heap.

Let's look at actual measurements to see this boundary. First, let's check the size of `any` itself:

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

Here is what we get when running libstdc++ 16:

```text
sizeof(std::any)            = 16
sizeof(void*)               = 8
sizeof(std::string)         = 32
sizeof(std::array<char,64>) = 64
```

`sizeof(std::any)` is **16 bytes**. Inside these 16 bytes are packed: an internal buffer (to hold small objects directly), plus a function pointer (pointing to the "manager" that knows how to handle this value). Since these two are squeezed together, the actual size of the payload that can be stored in-place is much smaller than 16—because the pointer itself takes up space. So, how large can an object be before it inevitably allocates on the heap? We use a small probe that determines "whether the value is inside the `any` object" by checking address differences to scan through the sizes:

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
    // delta 小 => 值在 any 对象内部(SBO)；delta 大/像堆地址 => 堆分配
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

Here is the output:

```text
```

```text
N=  1  sizeof(Blob)=  1  -> INLINE (SBO)
N=  8  sizeof(Blob)=  8  -> INLINE (SBO)
N= 12  sizeof(Blob)= 12  -> HEAP
N= 16  sizeof(Blob)= 16  -> HEAP
N= 32  sizeof(Blob)= 32  -> HEAP
N= 64  sizeof(Blob)= 64  -> HEAP
```

The SBO cutoff threshold in libstdc++ 16 is straightforward: **objects sized up to one pointer (8 bytes) go inline, anything larger goes to the heap**. 12 bytes overflows the buffer. This is a fact worth keeping in mind intuitively—it means that common scalars like `int`, `double`, and raw pointers fit into `any` without allocation, but `std::string` (whose `sizeof` is 32 and which utilizes SSO itself), `std::vector`, or any struct of significant size will trigger a heap allocation when placed into `any`.

Let's quantify the cost of SBO versus heap allocation using allocation counts. The following code overloads the global `operator new` to count exactly how many times `any` allocates:

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
        for (int i = 0; i < N; ++i) as.emplace_back(i);   // int -> SBO，不该有额外分配
    }
    std::cout << "any(int)  [SBO]:  allocs during build = " << g_alloc_count << '\n';

    struct Big { std::int64_t d[8]; };   // 64 字节，必上堆
    g_alloc_count = 0;
    {
        std::vector<std::any> as;
        as.reserve(N);
        for (int i = 0; i < N; ++i) as.emplace_back(Big{i,0,0,0,0,0,0,0});
    }
    std::cout << "any(Big64)[heap]: allocs during build = " << g_alloc_count << '\n';
}
```

Here is the output:

```text
...
```

```text
any(int)  [SBO]:  allocs during build = 1
    （那 1 次是 vector 自己 reserve 容量）
any(Big64)[heap]: allocs during build = 1000001
    （reserve 1 次 + 每个元素堆分配 1 次）
```

Numbers don't lie. Storing an `int` in one million elements results in zero extra allocations (SBO inlined them all); storing a 64-byte large object results in **a separate heap allocation for every single element**—one million times. This is the real cost of storing large objects in `any`—not only is access slow, but construction alone hammers the allocator. If you use `any` to store large objects on a hot path, this is a tangible performance problem.

## Comparison with variant: Why you should usually use variant

Now we can answer the opening question directly: since `any` is so flexible, why do we say it's "mostly unnecessary"? Because it trades the compile-time type safety of `optional` / `variant` for runtime type erasure, and you pay the full price. Let's compare `variant` and `any` side by side.

First, **is the type set open or closed**. `variant<int, double, string>` locks the candidate types down to three. This is a "closed set"—when you write the code, you know the value can only be one of these three types, and the compiler knows too. Therefore, `std::visit` ensures you handle every branch, and when accessing with `std::get<T>`, the type correctness is largely checkable at compile time (if wrong, it throws `bad_variant_access`, but since you listed the types, the probability of error is much lower). `any` is an "open set"—any `CopyConstructible` type can be stuffed in, and the compiler cannot help you verify. Whether the type is correct is **only known at the moment of runtime `any_cast`**; if wrong, it throws an exception or returns `nullptr`.

Second, **can we traverse all possibilities**. With `variant` and `std::visit`, we can write code that "handles whatever candidate is currently stored uniformly":

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

`any` has no equivalent—because it has no idea what the "set of candidate types" is, so it cannot iterate over them. You must either **explicitly state the type** at the call site (`any_cast<std::string>(a)`), or manually guess using `if (a.type() == typeid(X)) ... else if ...`. This is the cost of type erasure: type information disappears from the signature, so you have to manually restore all type-dependent logic at the call site.

Third, **performance**. Let's make a comparison: storing one million `int` values in `variant<int, double>` versus `any`, and running `get` or `any_cast` one million times respectively:

```text
variant<int,double>: access 1000000 ints = 1259 us
any(int) [SBO]:      any_cast<int> access 1000000 ints = 1340 us
```

(The absolute values fluctuate depending on the machine, so we only look at the order of magnitude here.) The access times are actually about the same for both—since they both follow the pattern of "read a type tag and then dispatch." The advantage of `variant` isn't about faster individual access, but rather the **zero extra allocation and compile-time verifiability brought by a known type set**: `variant` never allocates (its size is simply "max candidate size + one index"), whereas `any` requires heap allocation for large objects. With `variant`, the compiler can warn you about type mismatches; with `any`, a type mismatch causes a crash at runtime.

Therefore, here is a practical rule of thumb: **When you can list all possible types, always use `variant`**. The type set of a `variant` is closed, visible at compile time, and allocation-free; `any` only makes sense when "even you don't know what types might appear."

## When `any` should actually be used

After discussing so many reasons "not to use it," `any` is not useless. The scenarios where it is truly indispensable share a common characteristic: **the type set is open, and the consumer side doesn't care about the specific type**. The two most typical examples are:

**Property tables / Configuration tables**. A configuration system needs to store values of various types—timeout is an `int`, hostname is a `string`, retry count is an `unsigned`, a certain switch is a `bool`—and the person writing the configuration framework cannot foresee the types of all configuration items. In these "key-value pair with diverse value types" scenarios, `map<string, any>` is a natural fit:

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
        auto* p = std::any_cast<int>(&it->second);   // 指针重载，安全取值
        return p ? *p : -1;
    };

    std::cout << "timeout=" << get_int("timeout")
              << "  host=" << std::any_cast<std::string>(props["host"])
              << "  retries-as-int=" << get_int("retries") << '\n';
}
```

Here is the output:

```text
timeout=30  host=localhost  retries-as-int=-1
```

Note the `retries-as-int=-1` entry. Since `retries` stores an `unsigned` value, we cannot retrieve it using `any_cast<int>`. The pointer overload returns `nullptr`, so we safely fall back to `-1`. This is the correct way to use an `any` property table: the consumer uses the **pointer overload** for defensive value access. If the types don't match, there is a clear failure semantic instead of an exception crashing the entire program. This echoes the earlier warning—`int` and `unsigned` are distinct types inside `any`; storage and retrieval must strictly correspond.

**"Value Envelope" Across Boundaries**. When a value needs to cross a boundary you cannot control—such as a messaging system, a scripting binding layer, or a plugin interface—and you only want to pass through "a value" without caring what it specifically is, `any` serves as a type-agnostic envelope. The receiver can then unpack it in a way they understand. In this scenario, both conditions hold: "open set of types" and "consumer doesn't care about the specific type," making `any` truly appropriate.

Conversely, the following scenarios are **not** valid reasons to use `any`; use `variant` instead:

- "This value could be A, B, or C"—if you can list the types, use `variant<A, B, C>`.
- "This value might be missing"—use `optional<T>`.
- "I want to store a bunch of different objects"—if you can list them at coding time, use `variant`. Only when you truly cannot list them (e.g., a configuration framework) should you turn to `any`.

## Comparison with `void*` and Templates: Three Paths to Type Erasure

Placing `any` back into the broader context of "type erasure" clarifies its position. In C++, there are three ways to hide concrete types:

- **`void*`**: The most primitive and dangerous. Any pointer can be cast to `void*` and back, but type information is **completely lost**. If you cast to the wrong type, the compiler stays silent, and you get undefined behavior at runtime. `any` can be understood as a "safe `void*` with type information"—it remembers the original type internally, and `any_cast` checks against `typeid`. On mismatch, it throws an exception rather than silently invoking UB.

- **Templates**: Keep types at compile time. This offers zero runtime overhead and zero type erasure, but the cost is that "types must be known at the call site," and template code is instantiated into multiple copies. Templates are suitable for "types known at compile time," while `any` is suitable for "types unknown at compile time." They are not contradictory.

- **`any` / `variant` / `function`**: Type erasure utilities provided by the standard library. `any` erases the "specific type of a single value," `variant` lists a set of types and then erases the discriminant, and `function` erases the "specific type of a callable object." Their commonality is: they fix a signature/shell at compile time and delay the "specific type" detail to runtime, while retaining type-safe access (throwing exceptions on mismatch rather than UB).

Therefore, `any` is not just a trendy wrapper for `void*`; it is a safety utility with runtime type checking. It is not the opposite of templates, but rather fills the gap where templates cannot reach: "unknown types at compile time." Once you understand its position among these three paths, you will know when to choose it and when not to.

## Common Pitfalls

Let's collect the pitfalls encountered along the way; each has been verified through testing above:

::: warning any_cast requires exact types, no conversions
Casting `int` to `long`, `unsigned` to `int`, or `int` to `double`—**all fail** in `any_cast`. The pointer overload returns `nullptr`, and the value overload throws `bad_any_cast`. Remember that the template parameter for `any_cast` must match the stored type exactly. If you casually wrote a literal when storing (`42u` is `unsigned`, `42` is `int`), you must match it when retrieving.
:::

::: warning any only holds CopyConstructible types
Non-copyable types (including those with `unique_ptr` or deleted copy constructors) will not even compile. `variant` does not have this limitation—it only requires candidate types to be destructible. To hold move-only types, `any` cannot help you; consider `std::move_only_function` (C++23) or writing your own type erasure layer.
:::

::: warning Holding large objects = heap allocation per element
libstdc++'s SBO only accommodates a payload the size of a pointer (8 bytes). Anything larger goes to the heap. Storing `string`, `vector`, or sizable structs triggers a heap allocation for each. Be careful on hot paths. If you know the type in advance, don't use `any`; `variant` has zero allocation.
:::

::: warning Don't use any as a substitute for variant
This is the most common and insidious misuse. Whenever you can list candidate types, use `variant` + `visit`/`get`: compile-time checking, zero allocation, and traversable. Leave `any` for edge cases where the "type set is open and the consumer doesn't care about the specific type" (e.g., property tables, cross-boundary value envelopes).
:::

## Summary

`std::any` is the catch-all container in the standard library that "can hold any `CopyConstructible` type," but among the `optional` / `variant` / `any` trio, it is the one that requires the most caution. Here are the key takeaways:

- `any` uses type erasure to delay "what is the specific type" until runtime: it uses internal storage + a set of management function pointers. Therefore, **it can only hold `CopyConstructible` types**; non-copyable types (like those containing `unique_ptr`) are blocked at compile time.
- `any_cast<T>` has two overloads: the value form throws `bad_any_cast` on mismatch, and the pointer form (passing `&any`) returns `nullptr` without throwing. Consumers should use the pointer overload for defensive access. Regardless of the form, **exact type matching is required; no implicit conversion is performed**.
- SBO: libstdc++ 16's inlining threshold is 8 bytes (one pointer). `int`/`double`/raw pointers are inlined with zero allocation, while `string` and larger objects each hit the heap. Testing shows storing 1 million 64-byte objects = 1 million heap allocations.
- Most scenarios should use `variant`: closed candidate types, compile-time checking, zero allocation, and `visit` traversal. `any`'s irreplaceable scenario is "open type set and consumer doesn't care about the specific type"—typically for configuration/property tables and cross-boundary value envelopes.
- In the spectrum of type erasure, `any` is a "safe `void*` with runtime type checking," with a clear division of labor from templates (compile-time, zero overhead). It is not an either/or choice.

To wrap it up in one sentence: **Before writing `any`, ask yourself "Can I list all possible types?"—if yes, use `variant`; only if no, use `any`.** This habit will prevent 90% of `any` misuse.

## Reference Resources

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any) — Specification for the type-erasure container, CopyConstructible requirements, and SBO notes encouraging avoidance of dynamic allocation for small objects.
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast) — Two sets of overloads: value form throws `bad_any_cast`, pointer form returns `nullptr`.
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast) — Exception thrown on value overload type mismatch.
- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) — Discriminated union for closed type sets, the superior alternative to `any` in most scenarios.
