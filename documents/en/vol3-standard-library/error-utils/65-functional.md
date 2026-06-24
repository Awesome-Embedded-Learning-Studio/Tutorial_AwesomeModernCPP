---
title: 'functional: The Cost of std::function and C++23''s move_only_function'
description: 'We thoroughly explore the true nature of three components in `<functional>`:
  why `std::function`''s type erasure inevitably leads to virtual calls and potential
  heap allocation (with measured overhead), how `reference_wrapper` allows containers
  to store references, and how `move_only_function` in C++23 resolves the critical
  limitation of `std::function` being unable to store move-only callables.'
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
- optional：把「可能没有」做成类型
- variant：类型安全的联合体与 visit
- ranges 算法与 C++23 新件：fold、contains 与新适配器
related:
- expected：值或错误，C++23 的错误处理新范式
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
  translated_at: '2026-06-24T00:40:40.525500+00:00'
  engine: anthropic
  token_count: 4075
---
# functional: The Cost of std::function and C++23's move_only_function

After writing C++ for a while, you will inevitably run into this requirement: you have a bunch of "callable things" and want to store them uniformly. It might be a regular function, a lambda that captured some state, a member function of a class, or a functor. They share the same signature (all `int(int)`), but their **types are completely different**—and containers and member variables must know the type of elements at compile time. This is awkward: `std::vector</* what exactly goes here? */>` cannot be written.

The `<functional>` header exists to answer this question. Its core component, `std::function`, wraps objects that are "callable with matching signatures" into a single type using **type erasure**. This allows heterogeneous callable objects to be stuffed into the same container or member variable. This capability is valuable, but it isn't free—in this post, we will take it apart to see exactly what price type erasure exacts, and what gap `std::move_only_function` in C++23 fills.

While we are at it, we will also thoroughly cover two other high-frequency tools from `<functional>`: `reference_wrapper` (which allows containers to store references) and `std::hash` (the cornerstone of unordered containers). Finally, we will provide criteria for "when to use `std::function` and when to avoid it if possible." The deep dive into lambdas and closure mechanisms is outside the scope of this article (that is covered in vol2); here, we only treat lambdas as a tool to "generate a callable object."

## Three Types of Callable Objects: What They Actually Are

Before discussing `std::function`, we must distinguish the various faces of "callable objects"; otherwise, the discussion on their costs will become muddled.

The first category consists of **ordinary functions** and **function pointers**. A function itself is an address, and a function pointer is a variable storing that address. Calling it involves an indirect jump—the compiler generally cannot inline through a pointer, which is a key point in the performance comparison later.

The second category is **function objects (functors)**, which are class instances that overload `operator()`:

```cpp
// Standard: C++11
struct Multiplier {
    int factor;
    explicit Multiplier(int f) : factor{f} {}
    int operator()(int x) const { return x * factor; }
};

Multiplier times3{3};
int r = times3(10);   // 30 —— 调用 operator()
```

A functor has two characteristics: it carries state (`factor` is a member), and **the type is a class you define yourself**.

The third category is the **lambda**. A lambda looks lightweight and is written like an anonymous function, but during compilation, it is actually translated into "a unique, compiler-generated class." Specifically, each lambda corresponds to a **closure type**, and each lambda expression is a distinct type—even if two lambda expressions are written identically:

```cpp
// Standard: C++11
auto f1 = []() { return 1; };
auto f2 = []() { return 1; };   // 看起来和 f1 完全相同
// 但 f1、f2 类型不同
static_assert(not std::is_same_v<decltype(f1), decltype(f2)>);
```

Run it to verify:

```text
f1 和 f2 类型是否相同: no
```

The capture list becomes members of the closure class, and the lambda body becomes the `operator()`. So, fundamentally, **a lambda is just syntactic sugar that saves you from writing a functor class manually**—it is the exact same thing as a functor, except the compiler generates the class for you. This is a crucial point to remember, as it directly explains why `std::function` requires type erasure: these three categories of callable objects have distinct types, yet share the same call signature, so you cannot hold them all with a single concrete C++ type.

As a side note, here is a common conclusion: **a zero-capture lambda can be implicitly converted to a function pointer** (because it has no state members), but one that captures variables cannot:

```text
零捕获 lambda -> 函数指针: 1
```

## std::function: Type-Erased Callable Wrappers

Let's return to the challenge we started with. How do we wrap three different kinds of callable objects using a single type? The answer provided by `std::function` is **type erasure**—we hide the information about "what the specific callable object is" until runtime, exposing only "what the signature is" to the outside.

```cpp
// Standard: C++11
#include <functional>

int free_fn(int x) { return x + 1; }

struct Doubler { int operator()(int x) const { return x * 2; } };

int main() {
    std::function<int(int)> f;   // 一个能装任何 int(int) 的槽

    f = free_fn;                 // 装函数指针
    f = Doubler{};               // 装 functor
    f = [](int x){ return x * 3; };   // 装 lambda
    int cap = 10;
    f = [cap](int x){ return x + cap; };  // 装带捕获的 lambda

    return f(5);   // 调用 —— 无关它现在装的是什么
}
```

The `<int(int)>` in `std::function<int(int)>` is the call signature preserved for the external interface after type erasure. Whether it stores a function pointer, a functor, or a closure internally is invisible to the outside world. This is exactly why it can be stored in containers and used as a member variable—containers only need a fixed element type.

So, how exactly is "hiding it until runtime" implemented? Peeling back a layer, `std::function` generally looks like this internally: it holds an **invoker** function pointer and a **manager**. The actual callable object is stored in a fixed-size, inline small buffer (in libstdc++, the `sizeof` of `std::function<int(int)>` is 32 bytes). Only when the target is too large to fit into this small buffer does it allocate a block on the heap to store it, keeping only a pointer internally. Every time you call `f(5)`, it actually **indirectly jumps** to the real invocation code through that function pointer.

Under this mechanism, both "holding any type" and "calling them one by one" are achieved, but we have also touched on the cost—an indirect call, plus a potential heap allocation. Let's measure these one by one.

## Measurement: How High is the Cost of std::function?

Let's be clear first: the cost of `std::function` isn't a "fixed number." It consists of two parts, and their weight varies depending on usage patterns. We will measure each item to avoid drawing vague conclusions.

### Cost One: Potential Heap Allocation

`std::function` has a fixed-size SBO (Small Buffer Optimization) buffer internally. If the capture is small enough to fit in the buffer, it is stored inline with zero allocation. If the capture is too large, a heap allocation occurs. We intercept the global `operator new` to count directly:

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
    // 小捕获:1 个 int,塞得进 SBO
    {
        int x = 42;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [x](int a){ return a + x; };
        // (用一下 f,别让编译器优化掉)
    }
    // 大捕获:int[64] ≈ 256B,塞不进 SBO
    {
        int big[64]{};
        big[0] = 7;
        g_alloc_count = 0; g_alloc_bytes = 0;
        std::function<int(int)> f = [big](int a){ return a + big[0]; };
    }
    return 0;
}
```

**Running it:**

```text
小捕获(1 个 int): std::function 构造时堆分配次数 = 0, 字节 = 0
大捕获(int[64]): std::function 构造时堆分配次数 = 1, 字节 = 256
sizeof(std::function<int(int)>) = 32
```

The conclusion is straightforward: small captures result in zero heap allocations, while large captures result in a single heap allocation. This means that when `std::function` stores a lambda with large captures, there is one heap operation during construction and one during destruction. This is a tangible cost on hot paths or when constructing many instances within a container. Meanwhile, the `sizeof` is 32 bytes, meaning that even if you store a one-byte callable object, `std::function` itself occupies 32 bytes. If you store a large number of them in a container, the memory overhead is significant.

### Cost Two: Indirect Invocation (No Inlining)

This is the performance aspect that is truly critical. The invocation of `std::function` happens via an indirect jump through an internal function pointer, and the compiler **cannot inline across this indirect call**. We will first use assembly to confirm that "it is indeed an indirect `call`," and then use a micro-benchmark to measure the timing.

```cpp
// 编译:g++ -std=c++23 -O2 -S
int main() {
    std::function<int(int)> f = target;   // target 是个 noinline 外部函数
    // ...
    return f(3);
}
```

In assembly, `f(3)` corresponds to:

```text
call *%rax       ; 间接调用,目标地址运行时才能定
```

The asterisk in `*%rax` represents indirection—the call target is fetched from within the `function` at runtime, invisible to the compiler, so it cannot cross this boundary to inline. This is fundamentally different from calling a normal function directly, where the compiler sees the implementation and can inline it.

How much does this differ in terms of timing? Let's run two scenarios—one that can be inlined and one that cannot—to clarify the comparison. First, we look at a scenario where the "computation body is extremely lightweight and can be inlined," ensuring that the fixed overhead of the indirect call isn't masked by the calculation itself:

```cpp
// Standard: C++23
#include <chrono>
#include <functional>
#include <iostream>

static volatile int g_sink = 0;

int main() {
    const int N = 1'000'000'000;

    auto lambda = [](int x){ return x + 1; };          // 能被内联
    int (*fptr)(int) = +[](int x){ return x + 1; };    // 函数指针,不能内联
    std::function<int(int)> func = lambda;             // 类型擦除,不能内联

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

1 billion iterations, local GCC 16.1.1, `-O2`:

```text
N = 1000000000 次极轻调用(x+1),总耗时(毫秒):
  直接 lambda        : 0 ms
  函数指针            : 1594 ms
  std::function      : 1886 ms
```

The numbers here are quite revealing. **The direct lambda takes 0ms** — not because it is absurdly fast, but because the compiler realized the entire loop could be strength-reduced into a constant (there is a closed-form formula for summing `x+1`), and the whole block was optimized away. The function pointer and `std::function`, however, involve indirection that prevents this optimization, so they faithfully executed a billion iterations, taking around 1.6 seconds and 1.9 seconds respectively.

This reveals the core cost of **type erasure**: **it forces a call that the compiler could otherwise see through and eliminate entirely into a concrete, indirect call**. On hot paths where the function body is lightweight and called frequently (e.g., per-pixel or per-element callbacks), this difference can shift from "free" to "consuming an entire CPU core."

But what about scenarios where **the function body has significant work to do and cannot be inlined anyway**? Let's switch the target to an external `noinline` function, forcing all three callers to actually invoke it:

```text
N = 1000000000 次调用(调用 noinline 外部函数),总耗时(毫秒):
  lambda -> noinline fn  : 1794 ms
  函数指针                : 1993 ms
  std::function          : 2124 ms
```

When the target function itself cannot be inlined, the performance gap between the three narrows—function pointers and lambdas are roughly equivalent (both incur a call), while `std::function` is slightly more expensive (an extra layer of indirection, costing about 10%–30%, depending on the machine and workload). This suggests a rule of thumb: **the heavier the callable body and the less likely it is to be inlined, the smaller the relative cost of `std::function`; conversely, the lighter the body and the more it relies on inlining for performance, the greater the relative cost of `std::function`**. While absolute numbers vary by machine and workload, the fact that "direct calls can be optimized away while indirect calls cannot" remains constant.

### Cost 3: Size

As we saw earlier, `sizeof(std::function<int(int)>)` is 32 bytes. Even if it holds a one-byte callable object, `function` still occupies 32 bytes. In scenarios involving "storing thousands of `function` objects in a container" (such as one callback per event slot in an event system), this size multiplied by the quantity must be factored into the memory budget.

## std::bind: Avoid if Possible

There is an older component in `<functional>` called `std::bind`, which is used to "fix specific arguments of a multi-parameter function to create a callable object with fewer parameters." For example, given a `power(base, exp)`, if we want a "square" function, we can use `bind` to fix `exp` to 2:

```cpp
// Standard: C++11
#include <functional>
int power(int base, int exp);

auto square_bind = std::bind(power, std::placeholders::_1, 2);
// 调用: square_bind(5) == power(5, 2) == 25
```

`std::placeholders::_1` is a placeholder, indicating "this position will be filled when called." This was useful in the C++11 era when lambda expressions were not yet widespread, and writing functors required a lot of boilerplate. However, since C++14, **lambdas are superior to `bind` in almost every aspect**:

```cpp
// Standard: C++14
auto square_lam = [](int base){ return power(base, 2); };
```

Lambda expressions are more intuitive (no need to memorize placeholder syntax), have local types (which facilitates better inlining), are easier to debug by inspecting the source code, and are friendly to move-only parameters. The return type of `bind` is some unspecified type internal to the standard library. When stuffing it into `std::function`, it is easy to fall into the trap of "pass-by-value vs. pass-by-reference" (requiring a `std::ref` wrapper to pass by reference). Therefore, `bind` is now considered a legacy component in modern code—essentially, "use lambda instead of bind whenever possible." You only need to know it exists to read old code; when writing new code, just use lambdas directly.

## reference_wrapper: Storing References in Containers

The next frequently used tool is `std::reference_wrapper`, along with its companions `std::ref` and `std::cref`. It solves the hard limitation that "containers cannot directly store references":

```cpp
// Standard: C++11
std::vector<int&> v;   // 编不过 —— 元素类型必须是 Erasable / 可对象化的
```

The C++ standard requires container elements to be proper object types. Since references are not objects (they have no address and cannot be assigned), `vector<T&>` is rejected outright. However, in practice, we often want a container to "reference a group of external variables." `reference_wrapper` is a thin wrapper that "acts like a reference but is itself an object"—it holds a pointer and supports implicit conversion back to `T&`:

```cpp
// Standard: C++23
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

int main() {
    int a = 1, b = 2, c = 3;
    std::vector<std::reference_wrapper<int>> refs{std::ref(a), std::ref(b), std::ref(c)};

    for (int& x : refs) {   // reference_wrapper 隐式转 int&
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

When iterating, `reference_wrapper<int>` implicitly converts back to `int&`, so modifications are written back to the original variable. `ref()` is the factory function for `reference_wrapper`, and `cref()` is the `const` version. Another classic use of `reference_wrapper` is to pass references where "capture/pass-by-value" is expected—for example, passing references to `std::bind` (otherwise `bind` copies by value), or in algorithm calls where "you cannot change the signature but need to output parameters."

## std::hash: The Cornerstone of Unordered Containers

`std::hash` is the underlying dependency that allows `unordered_map` and `unordered_set` to work—unordered containers rely on hash values to locate buckets, and calculating hash values relies on `std::hash<T>`. The standard library provides pre-specialized `std::hash` for fundamental types (integers, floating-point numbers, pointers) and common types like `std::string` and `std::string_view`:

```cpp
// Standard: C++11
std::hash<int>{}(42);                 // 算 42 的哈希
std::hash<std::string>{}("hello");    // 算字符串的哈希
```

```text
hash<int>(42)        = 42
hash<std::string>("hi") = 11290347552884584064
```

Note that `hash<int>(42)` results in `42` itself in libstdc++. For integer types, the Standard does not mandate a specific hash function implementation, but libstdc++ implements it as an identity mapping (the "hash" of an integer is the integer itself, because an integer is already a uniformly distributed, fixed-width value). This is merely an implementation detail; you **should not rely on the specific hash value**. Instead, rely on the guarantee that "identical inputs yield identical outputs, and distinct inputs are sufficiently dispersed".

If you use a custom type as a key in `unordered_map`, the standard library does not know how to calculate its hash. You must provide a specialization of `std::hash<YourType>` (or use a utility like `boost::hash_combine` to combine the hashes of individual fields). This is a frequently overlooked aspect of `std::hash`: it is **extensible**, and is not limited to built-in types.

## std::invoke: Unified Call Syntax

The final two components are small but critical. `std::invoke` (introduced in C++17) addresses the problem of "how to call any callable object using a uniform syntax". Ordinary functions and functors can be called directly with `f(args)`, but member functions and member pointers require the awkward syntax of `(obj.*pmf)(args)` or `obj.*pmd`. `invoke` unifies these:

```cpp
// Example usage (implied by context, usually added here)
// std::invoke(obj, args...);
```

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

Member functions, pointers to member data, and plain callable objects can all be invoked using a single syntax: `invoke(callable, args...)`. This is particularly valuable in generic code. When writing a template, you might not know whether the passed argument is a function or a member pointer, but `invoke` handles it correctly. `std::invoke_r<R>` (C++23) is a version of `invoke` that specifies the return type. It forces the result to convert to `R`, which is useful when dealing with strict callback signatures.

## C++23's `move_only_function`: Closing the move-only gap

This is where the truly new content of this article begins. `std::function` has a long-standing limitation: **it requires the target callable object to be copyable**. However, if you try to store a lambda that has captured a `std::unique_ptr`, this requirement falls apart—the closure holds a `unique_ptr` member, making the entire closure move-only and non-copyable:

```cpp
// Standard: C++23
std::unique_ptr<int> up = std::make_unique<int>(100);
auto lam = [up = std::move(up)](int x){ return *up + x; };   // 闭包是 move-only
std::function<int(int)> f = std::move(lam);   // 编不过
```

The compiler explicitly rejects this, static assertion failed:

```text
/usr/include/c++/16.1.1/bits/std_function.h:429:69:
  error: static assertion failed: std::function target must be copy-constructible
```

Internally, `std::function` mandates that the target be `is_copy_constructible` to support copying (when you copy the `function`, it must copy the contained target). This constraint often becomes a stumbling block in practical scenarios where we "store callbacks in containers and the callbacks hold exclusive resources."

C++23's `std::move_only_function` fills this gap. Like `std::function`, it performs type erasure, **but it only requires move, not copy**—allowing us to store move-only callables:

```cpp
// Standard: C++23
#include <functional>
#include <iostream>
#include <memory>

int main() {
    // 一个工厂:返回捕获了 unique_ptr 的 move-only lambda
    auto make_processor = [](std::unique_ptr<int> owner){
        return [owner = std::move(owner)](int x){
            return *owner + x;
        };
    };

    std::unique_ptr<int> up = std::make_unique<int>(100);
    std::move_only_function<int(int)> mof = make_processor(std::move(up));
    std::cout << "move_only_function 调用: " << mof(5) << "\n";
    std::cout << "  仍持有: " << (mof ? "yes" : "no") << "\n";

    // move_only_function 本身也是 move-only(不能拷贝)
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

It works. This is the most fundamental difference between it and `std::function`: **`std::function` requires Copyable, while `move_only_function` only requires Movable**. The tradeoff is that `move_only_function` itself is also non-copyable (move-only), which is reasonable—since it may store move-only types internally, the entire wrapper naturally cannot be copied.

In other respects, the two are quite similar: `move_only_function` also performs type erasure, utilizes SBO, and incurs indirect call overhead. Our benchmarks show its invocation overhead is on par with `std::function` (even slightly faster, as it eliminates the code path for copying):

```text
N=1000000000 次间接调用(同样 noinline 目标):
  std::function          : 1871 ms
  std::move_only_function: 1666 ms
```

In terms of size, `move_only_function` is slightly larger (`sizeof` is 40 bytes vs 32 bytes for `function`, in libstdc++). So the selection rule is clear: **if the callable object is copyable and you need to copy the wrapper (for example, copying a container), use `std::function`; if the callable object is move-only (holding exclusive resources like `unique_ptr`, `promise`, or file handles), use `move_only_function`**.

::: warning function_ref didn't make it into C++23
You may have heard of a lightweight, non-owning, zero-allocation, reference-only callable wrapper called `std::function_ref`. It was discussed during the C++23 timeline but ultimately didn't make the cut and was deferred to C++26. So, under C++23, you only have two "owning" options: `std::function` and `move_only_function`. If you want a non-owning lightweight view, you currently have to write it yourself or use a third-party library (like `tl::function_ref`). Don't let outdated documentation mislead you—we verified that under GCC 16.1.1, `std::function_ref` directly errors with `'function_ref' is not a member of 'std'`.
:::

## When to use it and when not to

Let's distill the experience from these sections into a few judgment criteria.

**Scenarios where you should use `std::function` (or `move_only_function`):**

- **Storage of heterogeneous callable objects**: If a callback slot needs to accept function pointers, functors, and lambdas, type erasure is the only solution.
- **Runtime replacement of callable objects**: If the same `function` variable needs to hold A first and then B later, this "reassignable" semantic is something templates cannot provide.
- **Crossing ABI boundaries**: If a library interface needs to expose a callback type, and templates can't be placed in headers or need to work with virtual functions, `function` provides a stable type-erasure boundary.
- **Callable objects hold exclusive resources**: Use `move_only_function` (starting from C++23).

**Scenarios where you should NOT use it—don't erase if a lighter method works:**

- **Use templates instead of `function` if possible**. Template arguments deduce the specific type, allowing calls to be inlined with zero overhead. An algorithm accepting a callback written as a template `template <typename F> void algo(F f)` is almost always better than `void algo(std::function<...> f)`—unless `algo` is a virtual function or you need to store `F`.
- **Zero-capture, single-signature use cases**. Use a function pointer `int(*)(int)` directly. The overhead is smaller than `function`, it's copyable, and it's sufficient.
- **High-frequency callbacks on hot paths**. As we saw in the benchmarks, the lighter the call body, the larger the relative overhead of the `function` indirection. Swap hot path callbacks to templates or function pointers.

To summarize in one sentence: **Type erasure is a cost you pay for "heterogeneity, reassignability, and crossing boundaries," not for everyday callbacks**. When you don't need its capabilities, it only introduces an extra indirection and potential heap allocation for no reason.

## Summary

The core of `<functional>` boils down to these few things; let's capture the key conclusions:

- Three types of callable objects—functions/function pointers, functors, and lambdas. Lambdas are translated by the compiler into "unique closure types" (each lambda expression is a distinct type); they are essentially the same as functors, just with the class generated for you by the compiler. Zero-capture lambdas can be converted to function pointers.
- `std::function` uses type erasure to wrap heterogeneous callable objects into a single type. The costs are threefold: ① Potential heap allocation (small captures use SBO for zero allocation; large captures trigger heap allocation—benchmarks show capturing `int[64]` allocates 256 bytes); ② Indirect calls, meaning the compiler cannot inline—benchmarks show 1 billion ultra-light calls take ~1.9s with `function`, ~1.6s with function pointers, and inlined direct lambdas are optimized to 0s; ③ Fixed size of 32 bytes (`sizeof`), so you must calculate the memory budget if storing them in large quantities.
- `std::bind` is basically obsolete in the face of C++14 lambdas—avoid using it if possible; write lambdas in new code.
- `std::reference_wrapper` (`ref`/`cref`) allows containers to "store references," solving the hard limitation that `vector<T&>` won't compile; it also allows passing references to contexts like `bind` that take arguments by value.
- `std::hash` is the cornerstone of unordered containers, with pre-specialized versions for basic types and `string`; custom types used as keys require manual specialization.
- `std::invoke` (C++17) unifies call syntax, making generic code calls to member functions/member pointers less awkward; `std::invoke_r<R>` (C++23) adds a return type.
- **`std::move_only_function` (C++23) is the highlight of this article**: It only requires Movable, not Copyable, so it can store lambdas capturing move-only resources like `unique_ptr`, which `std::function` (strictly requires Copyable) cannot do. We verified it compiles and works correctly under GCC 16.1.1. The tradeoff is that it cannot be copied itself.
- Selection strategy: Use `function` (or `move_only_function` for move-only resources) for heterogeneous storage, runtime replacement, or crossing ABI boundaries. If you can use templates or function pointers, avoid type erasure, especially on hot paths.

## References

- [cppreference: std::function](https://en.cppreference.com/w/cpp/utility/functional/function) — Type-erasing callable wrapper, including SBO and Copyable requirements
- [cppreference: std::move_only_function (C++23)](https://en.cppreference.com/w/cpp/utility/functional/move_only_function) — Move-only version, does not require Copyable
- [cppreference: std::reference_wrapper](https://en.cppreference.com/w/cpp/utility/functional/reference_wrapper) — Reference wrapper, allowing containers to store references
- [cppreference: std::invoke / std::invoke_r](https://en.cppreference.com/w/cpp/utility/functional/invoke) — Unified invocation syntax
- [cppreference: std::hash](https://en.cppreference.com/w/cpp/utility/hash) — Hashing foundation for unordered containers
- [P0288: move_only_function](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0288r9.html) — move_only_function proposal, explaining the motivation for move-only semantics
