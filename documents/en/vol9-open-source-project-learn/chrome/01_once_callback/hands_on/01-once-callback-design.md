---
chapter: 1
cpp_standard:
- 23
description: "Starting from Chromium's OnceCallback, design a C++23 move-only, single-shot callback component; part one covers the motivation analysis and API design"
difficulty: advanced
order: 1
platform: host
prerequisites:
- std::function, std::invoke, and callable objects
- Move semantics and perfect forwarding
reading_time_minutes: 19
related:
- OnceCallback and RepeatingCallback
- bind_once / bind_repeating and argument binding
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: "once_callback Design Guide (Part 1): Motivation and Interface Design"
---
# once_callback Design Guide (Part 1): Motivation and Interface Design

## Where the problem comes from: what `std::function` misses for async callbacks

The deepest hole I've fallen into over years of async programming is a callback fired twice. The scenario is textbook: register a callback for file I/O completion, expect it to run once and be done, then some retry path slips and fires it again. The resources just released inside the callback get touched a second time, and you eat a segfault. The nastiest part is that this kind of bug almost never reproduces in single-threaded unit tests. The happy path calls the callback once; the trigger hides in a race or an error path and only surfaces under real load in production.

`std::function` doesn't help us here. It's copyable, it's repeatable, and one callback object can be cloned to a dozen places with nobody watching. We took its internals apart in Volume 2 (type erasure plus SBO) and hand-wrote the `LightCallback` simplification, but that whole exercise went into squeezing type-erasure overhead. It never touched the real question: how many times should this callback be allowed to run? Leave the semantics undefined and the runtime will punish you.

Chromium's `base::OnceCallback` nails this down at the type level. `OnceCallback` is move-only, its `Run()` is callable only through an rvalue (`std::move(cb).Run()`), and one call consumes the object. A second call is a no-op or an assertion failure. This contract shoulders tens of billions of task postings every day inside Chrome, and it holds.

What this series does is extract the essence of Chromium's design and rebuild it on C++23 standard facilities (`std::move_only_function` and deducing this). The original carries real heavy lifting: hand-rolled reference counting, a `TRIVIAL_ABI` annotation, a function-pointer dispatch table. We take a different route, keep the codebase in check, and lose none of the semantics.

Let's pin down the three places `std::function` leaks in async scenarios before we touch anything.

First, it's copyable. A callback copied to several places means, in an async system, several execution paths each holding their own copy. If the callback captures something move-only like `std::unique_ptr`, the copy fails to compile, and that's the kind outcome: the compiler catches it for you. If it captures raw pointers or references, the copies all run and you've planted a race. Chrome's take is blunt: an async task callback shouldn't be copied at all, so kill copyability at the type level.

Second, it's repeatable. `std::function::operator()` has zero opinions about call count. Call one object a thousand times and it runs every time. But an async callback isn't that. A file-read completion callback fired twice is a logic error: resources freed twice, state transitioned twice, the message sent twice. The type system can't see this. You either lean on a runtime assertion or, more often, you discover it at the bug site.

The third leak is the deepest. `std::function` can't express "calling is consuming." In Chrome's task posting, the moment `PostTask(FROM_HERE, callback)` runs, ownership of `callback` transfers to the task system and the caller is done with it. But `std::function::operator()` is `const`-qualified: one call doesn't change the object's state. You can't tell the world, through the calling interface itself, "this call eats the object."

Three leaks, one root: `std::function`'s interface can't express "this callback runs once and is then dead." `OnceCallback` exists to fill that gap.

## Chromium's answer: the design intent behind `OnceCallback`

Chrome's callback system rests on one principle: message passing beats locks, serialization beats threads. Under that principle, every callback posted to the task system (a `task` in Chrome parlance) is an independent, one-shot message. Once posted, ownership transfers to the task system; once executed, it's destroyed. No sharing, no reuse, no ambiguity.

That philosophy lands directly on `OnceCallback`'s type design. The key tradeoffs:

| Tradeoff | OnceCallback's choice | What it solves |
|---|---|---|
| Copyability | Delete copy ctor/assign, move only | Type-level guarantee of a single owner at any moment |
| Value category of `Run()` | Rvalue-only (`std::move(cb).Run()`) | Syntax-level reminder: "you're consuming it, don't touch it after" |
| Call count | Internally consumes the `BindState`, later access is a no-op | Single-consumption semantics |

Worth noting: Chrome also ships `RepeatingCallback`, the copyable, repeatable sibling. Both callback classes share the same `BindState` machinery; the only differences are the value-category qualification on `Run()` and the ownership semantics of `BindState`. One binding backbone serves "one-shot task" and "repeating listener" at once, and I find that genuinely clean.

### Chromium's internal layout

We're not going to read Chromium's source line by line, but we do need the core architecture in hand. The `OnceCallback` we're about to build walks the same layered spine, just with the implementation simplified through C++23 standard facilities.

Reading bottom up, Chromium's callback system stacks three layers.

The bottom is `BindStateBase`, the type-erased base class with a reference count. One tradeoff stopped me cold the first time I read it: it carries a refcount, **yet uses no virtual functions**. Instead it holds three function-pointer members: `polymorphic_invoke_` for calling, `destructor_` for destruction, `query_cancellation_traits_` for cancellation queries. The Chrome team did this to crush binary bloat. A virtual function generates a separate vtable per template instantiation; if the project has a hundred `BindState<Functor, BoundArgs...>` instantiations, you get a hundred vtables. The function-pointer route reuses the same static functions and only the pointer values differ, so the code segment doesn't bloat.

The middle layer is `BindState<Functor, BoundArgs...>`, a templated concrete class inheriting from `BindStateBase`. Think of it as "the box that holds everything": your lambda, the bound arguments, and the function pointers the base needs. Its lifetime is managed by `scoped_refptr` (Chromium's own intrusive reference-counted smart pointer). `OnceCallback` releases its reference on `Run()`; `RepeatingCallback` holds the reference across every `Run()`.

On top sit `OnceCallback<Signature>` and `RepeatingCallback<Signature>`, the types users actually touch. They're thin wrappers around `BindStateHolder`, which is itself a `scoped_refptr<BindStateBase>` annotated with `TRIVIAL_ABI`. `TRIVIAL_ABI` is a Clang extension attribute that tells the compiler "this type can ride in a register like an `int`," so `OnceCallback` is exactly one pointer wide (8 bytes), and moving it is just copying a pointer. Stupidly cheap.

One sentence on the three layers: the top-level callback object is just a pointer to the middle-layer box, and the box holds the function pointers the bottom needs plus the real data. The `OnceCallback` we design next keeps the "outer interface plus middle storage plus type erasure" skeleton, but the bottom swaps Chromium's hand-written `BindState` + `scoped_refptr` for `std::move_only_function`, and swaps the `const&` overload with `static_assert` hack for deducing this.

---

## Environment and prerequisites

Before we start, confirm the toolchain. This `OnceCallback` series eats a few bites of C++23: `std::move_only_function` (in `<functional>`, the move-only type-erased callable wrapper introduced in C++23, our core building block), deducing this (the explicit object parameter `this auto&& self` that lets a member function deduce the value category of `this`), and occasionally `if consteval` for compile-time branching.

On the compiler side, GCC 12+ or Clang 16+ fully support the above. Compile with `-std=c++23`. This snippet verifies the environment fast:

```cpp
#include <functional>

// Verify std::move_only_function is available
static_assert(__cpp_lib_move_only_function >= 202110L);

// Verify deducing this is available (compiles only if supported)
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If this compiles, the environment is set. Honest caveat: as I write this, some compilers' `std::move_only_function` implementations still have bugs (early GCC 12 fails in certain SFINAE scenarios). For safety, use the stable GCC 13+ or Clang 17+.

On the prerequisites side, we assume you're already comfortable with the following (all covered in Volume 2). Move semantics and perfect forwarding: the core of `OnceCallback` is move-only, and the implementation will hurt if `std::move` and `std::forward` aren't second nature (Vol 2 ch00, the move semantics series). Type erasure and SBO in `std::function`: we build straight on top of `std::move_only_function`, so you need to know what type erasure is and why small buffer optimization matters (Vol 2 ch03). `std::invoke` and the uniform calling convention: `bind_once` uses it to handle function pointers, member function pointers, and functors uniformly (same Vol 2 ch03). Variadic templates and parameter-pack expansion: the template specialization of `OnceCallback<R(Args...)>` and the argument binding in `bind_once` both lean on pack syntax (Vol 2 ch00 on perfect forwarding, Vol 4 on template basics).

---

## Designing the interface: what API do we want

Let's pin the target API down first, then walk back through each decision. This is how engineers work: figure out "what I want" before "how to do it."

### Core usage

```cpp
#include "once_callback/once_callback.hpp"

// 1. Construct: from a lambda
using namespace tamcpp::chrome;
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 2. Call: must go through an rvalue (std::move)
int result = std::move(cb).run(3, 4);  // result == 7

// 3. After the call, cb is consumed
// std::move(cb).run(1, 2);  // runtime assertion failure: callback already consumed
```

### Argument binding

```cpp
// bind_once: pre-bind some arguments, return a OnceCallback
using namespace tamcpp::chrome;
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // pre-bind the first two arguments
);

int r = std::move(bound).run(30);  // r == 60
```

### Cancellation check

```cpp
using namespace tamcpp::chrome;
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// Check whether the callback is still valid
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid: an optimistic check, for cross-sequence scenarios
if (cb.maybe_valid()) {
    // "possibly" valid, not guaranteed
    std::move(cb).run(42);
}
```

### Chained composition

```cpp
using namespace tamcpp::chrome;
// then(): pass the current callback's return value to the next callback
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14  (3+4)*2
```

### Walking through the interface decisions

The API is fixed. Now we dig through the decisions behind it, one at a time.

First, why `run()` and not `operator()`? Chromium uses `Run()` (Google C++ style wants the leading capital); we follow snake_case and use `run()`. But this isn't just a naming-convention difference. `operator()` is too generic: every callable object has one. `run()` says "execute the task" outright, so during code review you see at a glance that this is consuming a `OnceCallback`, not invoking some random callable. The semantic boundary is sharp, and the reader saves the mental cost.

The real load-bearing choice: why must `run()` go through an rvalue? This is the most critical piece of the whole design. We want a mechanism where `cb.run(args)` (an lvalue call) fails to compile and `std::move(cb).run(args)` (an rvalue call) compiles. Chromium's implementation pulls this off with two overloads: one `Run() &&` is the real execution, and one `Run() const&` carries a `static_assert(!sizeof(*this))` to block the lvalue. The hack works, but honestly it's ugly.

C++23's deducing this (explicit object parameter) lets us do it more cleanly. In short, it lets a member function write `this` explicitly as a template parameter, and the compiler deduces that parameter's type from whether the call site uses an lvalue or an rvalue. With that, `run(this auto&& self, Args... args)` deduces the value category of `self` and rejects illegal uses at compile time:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    // ... actual call logic
}
```

When the caller writes `cb.run(args)`, `Self` deduces to `OnceCallback&` (an lvalue reference), the `static_assert` fires, and the error message tells them exactly how to fix it. When they write `std::move(cb).run(args)`, `Self` deduces to `OnceCallback` (an rvalue) and it compiles. How deducing this works in detail, and a close comparison with Chromium's hack, we save for the next implementation piece.

Next, a smaller but easy-to-trip choice: why split `is_cancelled()` and `maybe_valid()`? This design comes straight from Chromium's `CancellationQueryMode`, and the difference is in how strong the safety guarantee is. `is_cancelled()` gives a definitive answer: it can only be called on the sequence the callback is bound to, and the result is accurate. `maybe_valid()` gives an optimistic estimate: callable from any thread, but the result may be stale. In practice, `is_cancelled()` backs the "is it still worth posting?" check, while `maybe_valid()` backs the "quick cross-thread peek, is it worth the post?" optimization path. In our simplified implementation, both methods query a `CancelableToken`: `is_cancelled()` checks state validity and whether the token still exists, and `maybe_valid()` is just a thin wrapper around `!is_cancelled()`. If you later need finer-grained thread-safety semantics, split these two methods further.

Last, why does `then()` also have to eat `*this`? The semantics of `then()` are "pass the current callback's result to the next callback," which requires the current callback to be captured wholesale inside the new one. If `then()` didn't consume `*this`, the same callback would live in two places at once, the original spot and the new callback returned by `then()`, and move-only semantics would break on the spot. So `then()` is declared as an rvalue-qualified member (`then(...) &&`), and the original callback enters the consumed state after the call.

---

## Internal mechanism: the two-layer type-erasure architecture

The interface is set. Now the internals. Chromium's `BindStateBase` + `scoped_refptr` + function-pointer table combo does type erasure well, but the code volume is genuinely staggering. Our route is to let `std::move_only_function` carry the dirty work of type erasure and small-object optimization, and to focus our energy on the parts with real bite: consumption semantics, argument binding, and chained composition.

### Why `std::move_only_function`

`std::move_only_function<R(Args...)>` landed in C++23, positioned as "the move-only `std::function`." It does type erasure and SBO internally, behaves like `std::function`, and simply deletes the copy operations.

You may have already noticed the `OnceCallback<R(Args...)>` spelling. `R(Args...)` looks like a function declaration, but in the template-parameter context it's a legitimate C++ type: a function type. `int(int, int)` describes "a function taking two ints and returning one int." We deconstruct this type through template partial specialization; we'll cover that technique in detail in the next article.

Using `std::move_only_function` for internal storage pays off in a few ways at once. First, it takes over the type-erasure work entirely. Recall the `LightCallback` from Volume 2: we spent a whole chapter hand-writing the function-pointer table, the SBO buffer, the move and destructor logic. `std::move_only_function` packages all of that and hands it to you. Second, it natively supports move-only callables: if a callback captures a `std::unique_ptr`, `std::function` refuses to compile over its copy requirement, while `std::move_only_function` has no such problem. Its SBO is also carefully tuned by the standard-library authors; in the vast majority of cases there's no heap allocation, and for lambdas capturing a handful of arguments the performance is more than enough.

### Three-state management

Once `std::move_only_function` is in, a design question has to be settled: how do we tell apart "an empty callback" from "a consumed callback"?

`std::move_only_function` can be empty on its own (default-constructed or built from `nullptr`), but "empty" and "consumed by `run()`" are not the same thing. Empty means "never assigned a value," and calling it should report a clear error ("callback is null"). Consumed means "it once held a value, but it's been called," and that should also report an error ("callback already consumed"), with a different message. That difference pays off in debugging: at a glance you can tell which step the callback is stuck on. So the internal state needs to be three-valued:

```cpp
enum class Status : uint8_t {
    kEmpty,     // default-constructed, never assigned
    kValid,     // holds a valid callable
    kConsumed   // consumed by run()
};
```

Paired with `std::move_only_function`, the internal storage looks roughly like this:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    std::move_only_function<FuncSig> func_;
    Status status_ = Status::kEmpty;

    // Cancellation token (optional)
    std::shared_ptr<CancelableToken> token_;
};
```

On move construction, `func_` and `status_` move together and the source object is set to `kEmpty`. When `run()` fires, it first checks that `status_` is `kValid`; after the call, it nulls out `func_` and sets `status_` to `kConsumed`. During debugging, the value of `status_` gives you a precise error message.

### Tradeoffs against Chromium's original

Using `std::move_only_function` as the backing storage buys a clean implementation, and the cost is real. Chromium's `OnceCallback` is one pointer wide (8 bytes), thanks to the `TRIVIAL_ABI` annotation on top of a refcounted `BindState`; the callback object is just a pointer to a heap-allocated `BindState`. Our `OnceCallback` wraps `std::move_only_function` (typically 32 bytes), plus the `Status` enum and the optional `CancelableToken` pointer (16 bytes), landing at roughly 56 to 64 bytes total.

The other difference is reference counting. Chromium's `BindState` is refcounted, so multiple callbacks can share one bound state (this is required for `RepeatingCallback`'s copy semantics). In our implementation, `std::move_only_function` owns its state exclusively and doesn't support sharing. For `OnceCallback`'s move-only semantics that's no obstacle, but when we get to `RepeatingCallback` later, this will need another look.

I think these tradeoffs are worth taking. We trade size and the flexibility of reference counting for a steep drop in implementation complexity. In practice, a 56-to-64-byte callback object is not the bottleneck in the vast majority of scenarios, the code structure is clear, and the cost of maintaining and extending it is far lower.

In the next piece we enter the implementation phase: start from the core `run()` skeleton and build `bind_once`, the cancellation check, and `then()` chaining on top of it.

## References

- [Chromium Callback documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
