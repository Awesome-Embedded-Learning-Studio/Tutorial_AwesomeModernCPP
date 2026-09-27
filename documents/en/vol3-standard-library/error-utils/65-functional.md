---
title: "functional: The Cost of std::function and C++23's move_only_function"
description: "A thorough look at the true nature of three things in <functional> — why std::function's type erasure inevitably costs you an indirect call and possible heap allocation (with measurements), how reference_wrapper lets containers store references, and how C++23's move_only_function fixes std::function's hard limitation of being unable to store move-only callables."
chapter: 7
order: 65
cpp_standard:
- 11
- 17
- 20
- 23
difficulty: intermediate
platform: host
reading_time_minutes: 14
prerequisites:
- 'optional: Making "Maybe Nothing" a Type'
- 'variant: Type-Safe Unions and visit'
- 'Ranges Algorithms and the C++23 Newcomers: fold, contains, and New Adapters'
related:
- 'expected: Value or Error, C++23''s New Error Handling Paradigm'
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_function
- std_invoke
- lambda
translation:
  source: documents/vol3-standard-library/error-utils/65-functional.md
  source_hash: a47ad663992ac484c4176aa21f363357da603f02f679d191f4acc6b506a39e01
  translated_at: '2026-09-26T00:53:52+00:00'
  engine: anthropic
  token_count: 11700
---

# functional: The Cost of std::function and C++23's move_only_function

After you've written C++ for a while, you will almost certainly run into this need: you have a bunch of "callable things" on your hands and want to store them uniformly. Maybe it's a plain function, maybe a lambda capturing some state, maybe a member function of some class, maybe a functor. They share a signature (all `int(int)`), but their **types are completely different** — and containers and member variables must know the element type at compile time. Now we're stuck: `std::vector</* what do I even write here? */>` simply cannot be spelled.

The `<functional>` header exists to answer this question. Its core component, `std::function`, uses **type erasure** to wrap everything that is "callable with a consistent signature" into one single type, so heterogeneous callables can live in the same container or the same member. That capability is valuable — but it isn't free. In this article we take it apart to see exactly what price type erasure charges, and which gap C++23 fills with `std::move_only_function`.

Along the way we also cover two other high-frequency tools from `<functional>` in depth: `reference_wrapper` (letting containers store references) and `std::hash` (the foundation of the unordered containers), and we close with a set of rules for when you should use `std::function` and when you should avoid it if you can. Lambdas themselves and the deep mechanics of closures are out of scope here (that's covered in vol2) — we only treat lambda as a tool that produces a callable object.

## Three Kinds of Callables: What They Really Are

Before talking about `std::function`, we need to sort out the several faces of "callable objects"; otherwise they will blur together once we discuss costs.

The first kind is **plain functions** and **function pointers**. A function itself is an address; a function pointer is a variable storing that address, and calling through it is one indirect jump — the compiler generally cannot inline through a pointer. This is a key point in the performance comparisons later.

The second kind is the **function object (functor)** — an instance of a class with an overloaded `operator()`:

```cpp
// Standard: C++11
struct Multiplier {
    int factor;
    explicit Multiplier(int f) : factor{f} {}
    int operator()(int x) const { return x * factor; }
};

Multiplier times3{3};
int r = times3(10);   // 30 — calls operator()
```

What characterizes a functor: it carries state (`factor` is a member), and its **type is a class you defined yourself**.

The third kind is the **lambda**. A lambda looks featherweight and reads like an anonymous function, but at compile time it is actually translated into "a unique, compiler-generated class". Concretely, every lambda corresponds to a **closure type**, and every lambda expression is a distinct type — even when two lambda snippets are character-for-character identical:

```cpp
// Standard: C++11
auto f1 = []() { return 1; };
auto f2 = []() { return 1; };   // looks identical to f1
// but f1 and f2 have different types
static_assert(not std::is_same_v<decltype(f1), decltype(f2)>);
```

Running it confirms:

```text
f1 和 f2 类型是否相同: no
```

The capture list becomes the closure class's members, and the lambda body becomes `operator()`. So at heart, **a lambda is just syntactic sugar that saves you from hand-writing the functor class** — it is the same kind of thing as a functor, except the compiler generates the class for you. Remember this point, because it directly explains why `std::function` needs type erasure: these three kinds of callables all have different types but share a call signature, and no single concrete C++ type can hold them all.

One useful corollary: **a capture-less lambda converts implicitly to a function pointer** (there are no state members), while one that captures something cannot:

```text
零捕获 lambda -> 函数指针: 1
```

## std::function: A Type-Erased Callable Wrapper

Back to the pain point from the beginning. Three kinds of callables with different types — how do you box them into one type? `std::function`'s answer is **type erasure**: hide "which callable this actually is" down at runtime, and expose only "what the signature is".

```cpp
// Standard: C++11
#include <functional>

int free_fn(int x) { return x + 1; }

struct Doubler { int operator()(int x) const { return x * 2; } };

int main() {
    std::function<int(int)> f;   // a slot that can hold any int(int)

    f = free_fn;                 // store a function pointer
    f = Doubler{};               // store a functor
    f = [](int x){ return x * 3; };   // store a lambda
    int cap = 10;
    f = [cap](int x){ return x + cap; };  // store a lambda with captures

    return f(5);   // invoke — doesn't matter what it currently holds
}
```

In `std::function<int(int)>`, the `<int(int)>` is the call signature preserved on the outside after erasure; whether a function pointer, a functor, or a closure is stored inside is invisible from the outside. That's exactly why it can go into containers and serve as a member variable — the container only needs one fixed element type.

So how is "hidden at runtime" implemented concretely? Peel off one layer: internally, `std::function` roughly holds an **invoker function pointer** and a **manager**, while the callable it actually stores sits in a fixed-size inline small buffer (in libstdc++, `sizeof` of `std::function<int(int)>` is 32 bytes); when the target is too big to fit in the small buffer, a block is allocated on the heap to hold it, and only a pointer stays inside. Every time you call `f(5)`, it actually **jumps indirectly** through that function pointer to the real calling code.

Under this mechanism, "can hold any type" and "can invoke each one" are both achieved — but we've now touched the edge of the price: one indirect call, plus possibly one heap allocation. Let's measure each in turn.

## Measured: How Big Is std::function's Cost, Really

Let's be precise up front: the cost of `std::function` is not "one fixed number". It is composed of two parts whose weights differ across usage patterns. We'll measure them one at a time to avoid blanket conclusions.

### Cost 1: Possible Heap Allocation

`std::function` has a fixed-size SBO (Small Buffer Optimization) buffer inside. If the capture payload is small (fits in the buffer), it is stored inline with zero allocations; if the payload is large (doesn't fit), a block is allocated on the heap. We intercept the global `operator new` and count directly:

```cpp
// Standard: C++23
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>

static std::size_t g_alloc_count = 0;
static std::size_t g_alloc_bytes = 0;

void* operator new(std::size_t n) {
    ++g_alloc_count;
    g_alloc_bytes += n;
    void* p = std::malloc(n);
    if (!p) throw std::bad_alloc{};
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }

int main() {
    // small capture: 1 int, fits in the SBO
    {
        int x = 42;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [x](int a){ return a + x; };
        // (use f somehow, so the compiler can't optimize it away)
    }
    // large capture: int[64] ≈ 256B, doesn't fit in the SBO
    {
        int big[64]{};
        big[0] = 7;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [big](int a){ return a + big[0]; };
    }
    return 0;
}
```

The run prints:

```text
小捕获(1 个 int): std::function 构造时堆分配次数 = 0, 字节 = 0
大捕获(int[64]): std::function 构造时堆分配次数 = 1, 字节 = 256
sizeof(std::function<int(int)>) = 32
```

The conclusion is blunt: small capture, zero allocations; large capture, one heap allocation. This means that when `std::function` stores a large-capture lambda, construction and destruction each perform a heap operation — on hot paths, or when constructing them en masse inside containers, that is a real cost. Meanwhile, the `sizeof` of 32 bytes means that even if you store a one-byte callable, the `std::function` itself still occupies 32 bytes — store a whole pile of them in a container and the memory footprint is not negligible.

### Cost 2: Indirect Calls (No Inlining)

This is the one that really hurts performance. A `std::function` call jumps indirectly through an internal function pointer, and the compiler **cannot inline across that indirect call**. First we use the assembly to confirm that it really is an indirect call, then we microbenchmark the time.

```cpp
// Compile: g++ -std=c++23 -O2 -S
int main() {
    std::function<int(int)> f = target;   // target is a noinline external function
    // ...
    return f(3);
}
```

In the assembly, `f(3)` corresponds to exactly this:

```text
call *%rax       ; indirect call - target address known only at runtime
```

That asterisk in `*%rax` is the indirection — the call target is fetched from inside the `function` at runtime, invisible to the compiler at compile time, so nothing can be inlined across that boundary. This is a different world from calling a plain function directly, where the compiler sees the implementation and can inline it.

So how much time is on the table? We run two scenarios — one inlinable, one not — to make the comparison clear. First, the "computation body is featherweight and can be inlined" scenario, so the fixed overhead of the indirect call isn't drowned out by the computation itself:

```cpp
// Standard: C++23
#include <chrono>
#include <functional>
#include <iostream>

static volatile int g_sink = 0;

int main() {
    const int N = 1'000'000'000;

    auto lambda = [](int x){ return x + 1; };          // can be inlined
    int (*fptr)(int) = +[](int x){ return x + 1; };    // function pointer, cannot be inlined
    std::function<int(int)> func = lambda;             // type erasure, cannot be inlined

    auto bench = [&](auto& c){
        long long acc = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < N; ++i) acc += c(i);
        auto t1 = std::chrono::steady_clock::now();
        g_sink = acc;
        return std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    };

    g_sink = lambda(0) + fptr(0) + func(0);   // warmup

    std::cout << "N = " << N << " 次极轻调用(x+1),总耗时(毫秒):\n";
    std::cout << "  直接 lambda        : " << bench(lambda) << " ms\n";
    std::cout << "  函数指针            : " << bench(fptr)   << " ms\n";
    std::cout << "  std::function      : " << bench(func)   << " ms\n";
    return 0;
}
```

One billion iterations, on this machine with GCC 16.1.1 and `-O2`:

```text
N = 1000000000 次极轻调用(x+1),总耗时(毫秒):
  直接 lambda        : 0 ms
  函数指针            : 1594 ms
  std::function      : 1886 ms
```

These numbers tell the story. **The direct lambda is 0 ms** — not because it is absurdly fast, but because the compiler recognized that the whole loop can be strength-reduced to a constant (the sum of `x+1` has a closed form) and eliminated the entire thing. The function pointer and `std::function` carry an indirect call that blocks that optimization, so they honestly ran a billion iterations, landing around 1.6 seconds and 1.9 seconds respectively.

This reveals the core cost of type erasure: **it forcibly turns a call the compiler could otherwise see through — even eliminate entirely — into a real, honest-to-goodness indirect call**. On hot paths where the call body is featherweight and calls are extremely frequent (per-pixel or per-element callbacks, say), the gap can go from "free" to "eating an entire CPU core".

What about the scenario where "the call body already does a certain amount of work and couldn't be inlined anyway"? We swap the target for an external `noinline` function so that all three callers genuinely have to call it:

```text
N = 1000000000 次调用(调用 noinline 外部函数),总耗时(毫秒):
  lambda -> noinline fn  : 1794 ms
  函数指针                : 1993 ms
  std::function          : 2124 ms
```

When the target function itself cannot be inlined, the gap between the three narrows — the function pointer and the lambda are about even (both boil down to one call), while `std::function` is slightly more expensive (one extra layer of indirection, roughly 10%–30% more, varying with machine and run-to-run noise). This gives us a rule of thumb: **the heavier the call body and the less it could be inlined anyway, the smaller std::function's relative cost; the lighter the call body and the more it relies on inlining to save time, the larger std::function's relative cost**. Absolute numbers fluctuate with machine and workload, but the property "direct calls can be optimized away, indirect calls cannot" is stable.

### Cost 3: Size

We saw earlier that `sizeof(std::function<int(int)>)` is 32 bytes. Even when holding a one-byte callable, the `function` still takes 32 bytes. In scenarios where you "store thousands of `function`s in a container" (an event system with one callback per event slot, say), this size times the count has to go into the memory budget.

## std::bind: Avoid It If You Can

`<functional>` also contains an old component, `std::bind`, whose job is "pin some of a multi-parameter function's arguments and produce a callable with fewer parameters". Say you have `power(base, exp)` and want a "square" function: you can use `bind` to pin `exp` to 2:

```cpp
// Standard: C++11
#include <functional>
int power(int base, int exp);

auto square_bind = std::bind(power, std::placeholders::_1, 2);
// call: square_bind(5) == power(5, 2) == 25
```

`std::placeholders::_1` is a placeholder meaning "this position gets its argument at call time". That was useful in the days when C++11 lambdas hadn't yet spread and functors had to be hand-written by the pile. But since C++14, **a lambda beats `bind` on almost every axis**:

```cpp
// Standard: C++14
auto square_lam = [](int base){ return power(base, 2); };
```

A lambda is more readable (no placeholder syntax to memorize), has a local type (inlines better), shows you real source when debugging, and plays nicely with move-only arguments. `bind`'s return type is some unspecified type internal to the standard library, and stuffing it into `std::function` invites the "pass-by-value vs pass-by-reference" trap (to pass a reference you have to wrap it in `std::ref`). So these days `bind` is basically a legacy component in new code — "wherever a lambda can replace bind, let it". Knowing it exists is enough to read old code; when writing new code, just use a lambda.

## reference_wrapper: Letting Containers Store References

The next high-frequency tool is `std::reference_wrapper`, together with its companions `std::ref` / `std::cref`. It solves the hard limitation that "containers cannot store references directly":

```cpp
// Standard: C++11
std::vector<int&> v;   // won't compile — the element type must be Erasable / a real object type
```

The C++ standard requires container elements to be genuine object types; a reference is not an object (it has no address and cannot be assigned), so `vector<T&>` is rejected outright. Yet in practice you often do want "a container referencing a group of external variables". `reference_wrapper` is a thin wrapper that "behaves like a reference but is itself an object" — it holds a pointer and supports implicit conversion back to `T&`:

```cpp
// Standard: C++23
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

int main() {
    int a = 1, b = 2, c = 3;
    std::vector<std::reference_wrapper<int>> refs{std::ref(a), std::ref(b), std::ref(c)};

    for (int& x : refs) {   // reference_wrapper implicitly converts to int&
        x *= 10;
    }
    std::cout << "通过 ref 修改后: a=" << a << " b=" << b << " c=" << c << "\n";
    std::cout << "显式 get(): " << refs[0].get() << "\n";
    return 0;
}
```

```text
通过 ref 修改后: a=10 b=20 c=30
  显式 get(): 10
```

During iteration, `reference_wrapper<int>` converts implicitly back to `int&`, so the modifications write through to the original variables. `ref()` is the factory function for `reference_wrapper`, and `cref()` is the const version. Another classic use of `reference_wrapper` is smuggling references into contexts that "capture/pass by value" — for instance passing a reference to `std::bind` (otherwise `bind` copies by value), or in algorithm calls where you "can't change the signature but still want an out-parameter".

## std::hash: The Foundation of Unordered Containers

`std::hash` is the dependency sitting underneath `unordered_map` / `unordered_set` — unordered containers locate buckets by hash value, and computing that hash value is exactly what `std::hash<T>` does. The standard library pre-provides `std::hash` specializations for the fundamental types (integers, floating point, pointers) and for common types like `std::string` and `std::string_view`:

```cpp
// Standard: C++11
std::hash<int>{}(42);                 // hash 42
std::hash<std::string>{}("hello");    // hash the string
```

```text
hash<int>(42)        = 42
hash<std::string>("hi") = 11290347552884584064
```

Note that in libstdc++, `hash<int>(42)` yields `42` itself — for integer types the standard doesn't mandate any particular hash implementation, but libstdc++'s happens to be the identity map (an integer's "hash" is itself, since an integer is already a uniformly distributed fixed-width value). This is just an implementation detail; you **should not depend on concrete hash values**. What you depend on is "same input gives same output, different inputs spread apart".

If you put your own type into an `unordered_map` as a key, the standard library has no idea how to hash it — you have to write a `std::hash<YourType>` specialization yourself (or use a tool like `boost::hash_combine` to stitch the per-field hashes together). This is the truly overlooked side of `std::hash`: it is **extensible**, not reserved for built-in types only.

## std::invoke: Unified Call Syntax

Finally, two small but crucial components. `std::invoke` (C++17) solves the problem of "how to call any callable with one uniform syntax". Plain functions and functors work with a direct `f(args)`, but member functions and member pointers have to be written `(obj.*pmf)(args)` / `obj.*pmd` — awkward syntax. `invoke` unifies them:

```cpp
// Standard: C++23
#include <functional>
#include <iostream>

struct Adder {
    int base{10};
    int add(int x) const { return base + x; }
};

int main() {
    Adder ad{100};
    auto lam = [](int x){ return x * 2; };

    std::cout << "invoke(成员函数): " << std::invoke(&Adder::add, ad, 5) << "\n";
    std::cout << "invoke(成员指针): " << std::invoke(&Adder::base, ad) << "\n";
    std::cout << "invoke(普通):    " << std::invoke(lam, 5) << "\n";
    return 0;
}
```

```text
invoke(成员函数): 105
invoke(成员指针): 100
invoke(普通):    10
```

Member functions, member pointers, plain callables — everything uses the single spelling `invoke(callable, args...)`. This is particularly valuable in generic code — you write a template without knowing whether what comes in is a function or a member pointer, and `invoke` calls it correctly either way. `std::invoke_r<R>` (C++23) is the fixed-return-type version of `invoke`, forcing the result into `R`; useful where callback signatures are strict.

## C++23's move_only_function: Filling the Move-Only Gap

Now we get to what is genuinely new in this article. `std::function` has a long-standing flaw: **it requires the target callable to be copyable**. The moment you want to store a lambda capturing a `std::unique_ptr`, that requirement collapses — the closure holds a `unique_ptr` member, which makes the whole closure move-only and impossible to copy:

```cpp
// Standard: C++23
std::unique_ptr<int> up = std::make_unique<int>(100);
auto lam = [up = std::move(up)](int x){ return *up + x; };   // the closure is move-only
std::function<int(int)> f = std::move(lam);   // won't compile
```

The compiler refuses bluntly, with a static assertion failure:

```text
/usr/include/c++/16.1.1/bits/std_function.h:429:69:
  error: static assertion failed: std::function target must be copy-constructible
```

To support copying internally (when you copy a `function`, it has to copy the target inside), `std::function` hard-requires the target to be `is_copy_constructible`. That constraint trips people up constantly in the real-world scenario of "storing callbacks in containers while the callbacks own exclusive resources".

C++23's `std::move_only_function` exists precisely to fill this gap. It performs type erasure just like `std::function`, **but requires only move, not copy** — so it can store move-only callables:

```cpp
// Standard: C++23
#include <functional>
#include <iostream>
#include <memory>

int main() {
    // a factory: returns a move-only lambda capturing a unique_ptr
    auto make_processor = [](std::unique_ptr<int> owner){
        return [owner = std::move(owner)](int x){
            return *owner + x;
        };
    };

    std::unique_ptr<int> up = std::make_unique<int>(100);
    std::move_only_function<int(int)> mof = make_processor(std::move(up));
    std::cout << "move_only_function 调用: " << mof(5) << "\n";
    std::cout << "  仍持有: " << (mof ? "yes" : "no") << "\n";

    // move_only_function itself is also move-only (not copyable)
    auto mof2 = std::move(mof);
    std::cout << "move 后 mof2(5) = " << mof2(5) << "\n";
    std::cout << "move 后源 mof 是否空: " << (mof ? "no" : "yes(被掏空)") << "\n";
    return 0;
}
```

```text
move_only_function 调用: 105
  仍持有: yes
move 后 mof2(5) = 105
move 后源 mof: yes(被掏空)
```

It runs. And that is the most essential difference from `std::function`: **`std::function` requires Copyable; `move_only_function` only requires Movable**. The price is that `move_only_function` itself cannot be copied either (move only) — which is actually quite reasonable: since it may hold something move-only inside, the wrapper as a whole naturally can't be copied.

In other respects the two are alike: `move_only_function` does the same type erasure, has the same SBO, and carries the same indirect-call overhead. We measured its call cost as roughly the same order as `std::function` (even slightly faster, because it doesn't have to maintain a copy path):

```text
N=1000000000 次间接调用(同样 noinline 目标):
  std::function          : 1871 ms
  std::move_only_function: 1666 ms
```

Size-wise, `move_only_function` is slightly larger (`sizeof` 40 bytes vs the `function`'s 32, in libstdc++). So the selection rule is clear: **if the callable is copyable and you need to copy the whole wrapper (copying a container, say), use `std::function`; if the callable is move-only (owning exclusive resources like a `unique_ptr`, a `promise`, or a file handle), use `move_only_function`**.

::: warning function_ref didn't make it into C++23
You may have heard of `std::function_ref`, a lightweight callable wrapper that is "non-owning, zero-allocation, pure reference". It was discussed during the C++23 time window but ultimately didn't catch that train and was deferred to C++26. So under C++23 you only have the two "owning" options, `std::function` and `move_only_function`; for a non-owning lightweight view, you currently have to write your own or use a third-party one (such as `tl::function_ref`). Don't let older materials mislead you here — we verified that under GCC 16.1.1, `std::function_ref` outright fails with `'function_ref' is not a member of 'std'`.
:::

## When to Use It, and When Not To

Let's distill the lessons of these sections into a few rules.

**Scenarios where you should use `std::function` (or `move_only_function`):**

- **Storing heterogeneous callables**: one callback slot that must accept function pointers, functors, and lambdas — type erasure is the only answer.
- **Replacing the callable at runtime**: the same `function` variable holding A now and B later — that "re-assignable" semantics is something templates cannot give you.
- **Crossing ABI boundaries**: when a library interface must expose a callback type and templates can't go in a header, or must cooperate with virtual functions, `function` is a stable type-erasure boundary.
- **The callable owns exclusive resources**: use `move_only_function` (since C++23).

**Scenarios where you shouldn't — when a lighter tool works, don't erase:**

- **If a template works, don't use `function`**. Template argument deduction recovers the concrete type, the call can be inlined, and the overhead is zero. An algorithm taking a callback is almost always better written as a template `template <typename F> void algo(F f)` than as `void algo(std::function<...> f)` — unless `algo` is virtual, or you need to store `F`.
- **Capture-less, single signature**. Just use a function pointer `int(*)(int)` — cheaper than `function`, copyable, and sufficient.
- **High-frequency callbacks on hot paths**. The measurements above showed it: the lighter the call body, the larger the share eaten by `function`'s indirection. Swap hot-path callbacks for templates or function pointers.

One sentence to sum it up: **type erasure is the premium you pay for "heterogeneous, replaceable, cross-boundary" — not something to buy for everyday callbacks**. When you don't need its capabilities, it does nothing but add one indirect call and one possible heap allocation for no benefit.

## Summary

The core of `<functional>` is just these few things; here are the key conclusions:

- Three kinds of callables — functions/function pointers, functors, and lambdas. A lambda is translated at compile time into "a unique closure type" (every lambda expression is a distinct type); at heart it is the same kind of thing as a functor, except the compiler generates the class. Capture-less lambdas can convert to function pointers.
- `std::function` uses type erasure to wrap heterogeneous callables into one type, at a cost with three components: (1) possible heap allocation (small captures go through SBO with zero allocation; large captures trigger heap allocation — measured, an `int[64]` capture allocates 256 bytes); (2) an indirect call the compiler cannot inline — measured over 1 billion featherweight calls, `function` took about 1.9 s, the function pointer about 1.6 s, and the inlinable direct lambda was optimized down to 0 s; (3) a fixed size of 32 bytes (`sizeof`), which has to go into the memory budget when stored in bulk in containers.
- `std::bind` is basically obsolete next to the C++14 lambda — avoid it when you can; write lambdas in new code.
- `std::reference_wrapper` (`ref`/`cref`) lets containers "store references", lifting the hard `vector<T&>` compile failure; it also smuggles references into by-value contexts such as `bind`.
- `std::hash` is the foundation of the unordered containers; it ships specializations for the fundamental types and `string`, and custom key types require your own specialization.
- `std::invoke` (C++17) unifies call syntax, so generic code no longer has to wrestle with member-function/member-pointer calls; `std::invoke_r<R>` (C++23) adds a fixed return type.
- **`std::move_only_function` (C++23) is this article's new material**: it requires Movable, not Copyable, so it can store lambdas capturing move-only resources such as `unique_ptr` — something `std::function` (which hard-requires Copyable) cannot do; we verified that it compiles and works correctly under GCC 16.1.1. The price is that it itself cannot be copied either.
- Selection: heterogeneous storage / runtime replacement / crossing ABI boundaries → `function` (or `move_only_function` for move-only resources); when a template or function pointer works, don't erase — especially on hot paths.

## References

- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function) — the type-erased callable wrapper, including SBO and the Copyable requirement
- [cppreference: std::move_only_function (C++23)](https://en.cppreference.com/w/cpp/utility/functional/move_only_function) — the move-only flavor, no Copyable requirement
- [cppreference: std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper) — the reference wrapper that lets containers store references
- [cppreference: std::invoke / std::invoke_r](https://en.cppreference.com/w/cpp/utility/functional/invoke) — unified call syntax
- [cppreference: std::hash](https://en.cppreference.com/w/cpp/utility/hash) — the hashing foundation of the unordered containers
- [P0288: move_only_function](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html) — the move_only_function proposal, explaining the motivation for move-only semantics
