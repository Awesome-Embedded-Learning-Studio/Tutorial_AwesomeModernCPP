---
chapter: 1
cpp_standard:
- 23
description: Design a C++23 move-only, single-consumption callback component starting
  from Chromium's OnceCallback — part one focuses on motivation analysis and API design
difficulty: advanced
order: 1
platform: host
prerequisites:
- std::function, std::invoke, and Callable Objects
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
title: "once_callback Design Guide (I): motivation and API design"
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/hands_on/01-once-callback-design.md
  source_hash: a540208de73d471ca73928353971d2c11237b9d2f5dca5b0db668f2216e0efd4
  translated_at: '2026-09-26T01:12:29+00:00'
  engine: anthropic
  token_count: 10500
---
# once_callback Design Guide (I): motivation and API design

## Where the problem comes from: `std::function`'s leaks in async callbacks

Across our years of async programming, the deepest pit we've fallen into is a callback invoked one time too many. The scenario is all too typical: you register a callback for file I/O completion, expect it to run once and be done, and then some retry path slips and fires it again. The resources just released inside the callback get accessed a second time, and you eat an outright segfault. The most maddening part is that this kind of bug almost never reproduces in single-threaded unit tests: the happy path calls the callback exactly once, while the trigger hides in some race or error path and only surfaces in production under enough load.

`std::function` can't help us here. It's copyable and callable repeatedly; one callback object can be cloned off to several places and nothing stops it. In Volume 2 we took its internals apart (type erasure plus SBO) and hand-wrote the simplified `LightCallback`, but all of that version's effort went into squeezing the overhead of type erasure — it never touched the question of "how many times may this callback actually be invoked". Leave the semantics unmanaged and the runtime crashes for all to see.

Chromium's `base::OnceCallback` pins this down at the type level: `OnceCallback` is move-only, its `Run()` can only be invoked on an rvalue (`std::move(cb).Run()`), and one invocation consumes the callback — calling it again is a no-op or an assertion failure. This set of constraints shoulders tens of billions of task postings a day in Chrome's task system, and it has held up under that grind.

What this series sets out to do is distill the essence of that Chromium design and rebuild it with C++23 standard facilities (`std::move_only_function` and deducing this). The original implementation grinds through hard manual labor — hand-rolled reference counting, `TRIVIAL_ABI` annotations, function-pointer dispatch tables. We take a different route: keep the code volume manageable without losing one bit of the semantics.

Before we start building, let's spell out exactly where `std::function` leaks in async scenarios.

`std::function`, leak number one: it's copyable. Once a callback is cloned to several places, an async system effectively has several execution paths each clutching their own copy. If the callback captures something move-only like a `std::unique_ptr`, the copy fails to compile — which is actually the good case, the compiler stopped it for you. If it captures raw pointers or references instead, several copies run concurrently and a race is planted. Chrome's reasoning is crisp: an async task callback should never be copied in the first place, so make it non-copyable at the type level.

Leak number two: it can be invoked repeatedly. `std::function::operator()` has no opinion about counts — call it a thousand times on one object and it will happily run every time. But async callbacks aren't like that — a file-read-completion callback invoked twice is simply a logic error: resources released twice, state transitions run twice, messages sent twice. The type system can't see this kind of error; only a runtime assertion can catch it, or, more commonly, a bug report from the field.

Leak number three hides the deepest: `std::function` cannot express "invocation consumes". In Chrome's task posting, the moment `PostTask(FROM_HERE, callback)` is called, ownership of `callback` transfers to the task system, and the caller is not supposed to touch it again. But `std::function::operator()` is `const`-qualified — one call doesn't change the object's state — so the call interface itself has no way to tell the world "this invocation devoured the object".

All three come down to one thing: `std::function`'s interface cannot express the constraint "this callback may be invoked exactly once and is invalid afterwards". `OnceCallback` exists to fill that gap.

## Chromium's answer: the design stance of `OnceCallback`

Chrome's callback system rests on one principle: message passing over locks, serialization over threads. Under this principle, every callback posted to the task system (a "task" in Chrome parlance) is an independent, one-shot message — once posted, ownership belongs to the task system, and it is destroyed after execution. No sharing, no reuse, no ambiguity.

That philosophy flows straight into `OnceCallback`'s type design. The key trade-offs are listed in the table below; one glance should map them for you:

| Trade-off | OnceCallback's choice | What it solves |
|---|---|---|
| Copyability | Copy construction/assignment deleted, move only | Type-level guarantee of a single holder at any moment |
| Value category of `Run()` | Invocable only on an rvalue (`std::move(cb).Run()`) | A syntactic reminder: "you are consuming it; don't use it after the call" |
| Invocation count | `BindState` consumed internally; later access is a no-op | Single-consumption semantics |

Incidentally, Chrome also has `RepeatingCallback` — the copyable, repeatedly invocable sibling. The two callback classes share one `BindState` implementation; the difference lies only in the value-category qualification of `Run()` and the ownership semantics of `BindState`. One binding infrastructure serving both "one-shot tasks" and "repeatable listeners" — we find that a rather clean piece of design.

### A peek at Chromium's internal implementation

We're not going to chew through Chromium's source line by line, but we do need a firm grasp of its core architecture — the `OnceCallback` we're about to build follows the same layered approach, just with the implementation simplified by C++23 standard facilities.

Viewed from the bottom up, Chromium's callback system stacks three layers.

The bottom layer is `BindStateBase`, the type-erasing base class, with reference counting. Here's a trade-off that stopped us cold on first read: it carries a reference count, **yet uses no virtual functions**. Instead it has three function-pointer members: `polymorphic_invoke_` handles invocation, `destructor_` handles destruction, and `query_cancellation_traits_` handles cancellation queries. The Chrome team does this to curb binary bloat: virtual functions give every template instantiation its own vtable — with a hundred `BindState<Functor, BoundArgs...>` instantiations in the project, you get a hundred vtables. The function-pointer approach reuses the same static functions with only the pointer values differing, so the code section doesn't balloon.

The middle layer is `BindState<Functor, BoundArgs...>`, the templated concrete class inheriting from `BindStateBase`. You can think of it as "a box holding everything": your lambda, the bound arguments, and the function pointers the base class wants. Its lifetime is managed by `scoped_refptr` (Chromium's own intrusive reference-counting smart pointer): `OnceCallback` releases its reference when `Run()` executes, while `RepeatingCallback` keeps the reference on every `Run()`.

On top sit `OnceCallback<Signature>` and `RepeatingCallback<Signature>`, the types users deal with directly. They are really thin wrappers over `BindStateHolder`, and `BindStateHolder` is nothing more than a `scoped_refptr<BindStateBase>` tagged with the `TRIVIAL_ABI` annotation. `TRIVIAL_ABI` is a Clang extension attribute that tells the compiler "this type can be passed in registers just like an int". As a result, `OnceCallback` is literally one pointer in size (8 bytes), and moving it is copying one pointer — absurdly light.

The three layers in one sentence: the top-level callback object is just a pointer to the middle-layer box, and the box holds the function pointers the bottom layer needs plus the actual data. The `OnceCallback` we design next keeps this layered skeleton — "outer interface + middle storage + type erasure" — but replaces the foundation: `std::move_only_function` takes over from Chromium's hand-written `BindState` + `scoped_refptr`, and deducing this replaces the `const&`-overload-plus-`static_assert` hack.

---

## Environment and prerequisites

Before we start, confirm the toolchain. This `OnceCallback` series takes a few bites of C++23: `std::move_only_function` (in `<functional>`, the move-only type-erased callable wrapper introduced in C++23 — our core building block), deducing this (the explicit object parameter `this auto&& self`, which lets a member function deduce the value category of `this`), and occasionally `if consteval` for compile-time conditionals.

On the compiler side, GCC 12+ or Clang 16+ fully supports the features above; just compile with `-std=c++23`. This snippet quickly verifies your environment:

```cpp
#include <functional>

// Verify std::move_only_function is available
static_assert(__cpp_lib_move_only_function >= 202110L);

// Verify deducing this is available (compiling through means it's supported)
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

If this compiles, your environment is set. Though, honestly, as we write this article some compilers' `std::move_only_function` implementations still have bugs (early GCC 12 releases fail to compile in certain SFINAE scenarios), so to be safe use a stable GCC 13+ or Clang 17+.

As for prerequisite knowledge, we assume you're already comfortable with the following (all covered in Volume 2): move semantics and perfect forwarding — `OnceCallback` is move-only at its core, and if the mechanics of `std::move` and `std::forward` aren't solid for you, the implementation process will hurt (the Volume 2 ch00 move-semantics series); `std::function`'s type erasure and SBO — we build directly on top of `std::move_only_function`, so you need to understand what type erasure does and why small-object optimization matters (Volume 2 ch03); `std::invoke` and the unified invocation protocol — `bind_once` uses it to uniformly handle function pointers, member-function pointers, functors, and the other distinct callable shapes (also Volume 2 ch03); and variadic templates and parameter-pack expansion — the template specialization of `OnceCallback<R(Args...)>` and `bind_once`'s argument binding both lean on parameter-pack syntax (Volume 2 ch00 on perfect forwarding, plus the Volume 4 template basics).

---

## Designing the interface: what API do we want

First we pin down the target API, then go back and scrutinize each decision. That's how engineers work — figure out "what I want" first, then "how to do it".

### Core usage

```cpp
#include "once_callback/once_callback.hpp"

// 1. Construct: create from a lambda
using namespace tamcpp::chrome;
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 2. Invoke: must go through an rvalue (std::move)
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

### Cancellation checks

```cpp
using namespace tamcpp::chrome;
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// Check whether the callback is still valid
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid: an optimistic check, suited to cross-sequence scenarios
if (cb.maybe_valid()) {
    // "possibly" valid, no guarantee
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

### Analyzing the API design decisions

With the API settled, let's scrutinize the decisions behind it one by one.

First, why `run()` instead of `operator()`. Chromium uses `Run()` (Google C++ style demands initial capitals); we go with snake_case and use `run()`. But this isn't just a naming-convention difference: `operator()` is far too generic — any callable can have one — while the word `run()` explicitly says "execute a task". During code review, you can see at a glance that this consumes a `OnceCallback` rather than invoking an ordinary callable. A clearly drawn semantic boundary spares whoever reads the code.

Now the truly critical one: why must `run()` be invoked on an rvalue? This is the linchpin of the whole design. We want a mechanism where `cb.run(args)` (an lvalue invocation) fails to compile while `std::move(cb).run(args)` (an rvalue invocation) compiles. Chromium achieves this with two overloads: `Run() &&` is the real executing version, and `Run() const&` stuffs in a `static_assert(!sizeof(*this))` to block lvalues. The hack works, but honestly it's ugly.

C++23's deducing this (the explicit object parameter) lets us do this more gracefully. In short, it allows a member function to spell out `this` explicitly as a template parameter, and the compiler deduces that parameter's type from whether the object is an lvalue or an rvalue at the call site. With that, `run(this auto&& self, Args... args)` can reject illegal usage at compile time based on the value category deduced for `self`:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    // ... actual invocation logic
}
```

When the caller writes `cb.run(args)`, `Self` deduces to `OnceCallback&` (an lvalue reference), the `static_assert` fires, and the error message tells you directly how to fix it. When they write `std::move(cb).run(args)`, `Self` deduces to `OnceCallback` (an rvalue) and compilation goes through. How deducing this actually works, and its detailed comparison against Chromium's approach, we'll save for the next installment, the implementation article.

Next, a small but easy-to-stumble-on trade-off: why are `is_cancelled()` and `maybe_valid()` two separate methods? The design comes straight from Chromium's `CancellationQueryMode`, and the difference is the strength of the safety guarantee. `is_cancelled()` gives a definitive answer — it may only be called on the sequence the callback was bound to, and the result is exact. `maybe_valid()` gives an optimistic estimate — callable from any thread, but the result may be stale. In practice, `is_cancelled()` is for decisions like "check whether posting still makes sense before posting", while `maybe_valid()` is for the optimized "quick cross-thread glance at whether it's worth posting" path. In our simplified implementation, both methods query the `CancelableToken`: `is_cancelled()` checks status validity and whether the token still exists, while `maybe_valid()` is just a thin wrapper over `!is_cancelled()`. Later, if you need finer-grained thread-safety semantics, making the distinction on these two methods is the place.

Finally, why `then()` must also devour `*this`. The semantics of `then()` are "pass the current callback's result to the next callback", which requires the current callback to be captured whole inside the new one. If `then()` didn't consume `*this`, the same callback would live in two places at once — one copy at the original spot, one inside the new callback returned by `then()` — and the move-only semantics would break on the spot. Hence `then()` is declared as an rvalue-qualified member function (`then(...) &&`), and after the call the original callback enters the consumed state.

---

## Internal mechanics: a two-layer type-erasure architecture

With the interface settled, it's time to look at how the inside is organized. Chromium's combo of `BindStateBase` + `scoped_refptr` + a function-pointer table does type erasure effectively, but the amount of code is genuinely staggering. Our route is to let `std::move_only_function` shoulder the dirty work — type erasure and small-object optimization — so we can concentrate our energy on the meaty parts: consumption semantics, argument binding, and chained composition.

### Why `std::move_only_function`

`std::move_only_function<R(Args...)>` was introduced in C++23, positioned exactly as "the move-only version of `std::function`". It has type erasure and SBO built in, behaves much like `std::function`, and simply deletes the copy operations.

You may have already noticed the `OnceCallback<R(Args...)>` spelling — `R(Args...)` looks like a function declaration, but in the context of a template parameter it's a perfectly legal C++ type: a function type. `int(int, int)` describes exactly "a function taking two ints and returning an int". We take this type apart via template partial specialization; the next article covers the technique in detail.

Using `std::move_only_function` as internal storage stacks several benefits together. First, it absorbs all the hand-written type-erasure work: recall `LightCallback` from Volume 2, where we spent an entire chapter hand-writing the function-pointer table, the SBO buffer, move and destruction — `std::move_only_function` wraps all of that up, ready to use. Second, it natively supports move-only callables: if a callback captures a `std::unique_ptr`, `std::function` flat-out fails to compile because of its copy requirements, while `std::move_only_function` has no such affliction. And its SBO implementation has been carefully tuned by the standard-library authors — no heap allocation in the vast majority of cases, which is plenty of performance for a lambda capturing a few arguments.

### Three-state management

Once `std::move_only_function` is in, one design question must be settled: how do we tell an "empty callback" apart from a "consumed callback"?

A `std::move_only_function` can itself be empty (default-constructed or constructed from `nullptr`), but "empty" and "consumed by `run()`" are two different things. Empty means "never assigned a value" — invoking it should raise a specific error ("callback is null"). Consumed means "it once held a value but has already been invoked" — that should also error ("callback already consumed"), with a different message. That small difference pays off during debugging: one glance tells you which step the callback actually died on. So our internal state must be three-way:

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

    // cancellation token (optional)
    std::shared_ptr<CancelableToken> token_;
};
```

On move construction, `func_` and `status_` travel over together and the source object's state is set to `kEmpty`. When `run()` executes, it first checks that `status_` is `kValid`; after execution it empties `func_` and sets `status_` to `kConsumed`. During debugging, the value of `status_` is enough to produce a precise error message.

### Trade-offs against Chromium's original

Using `std::move_only_function` as the underlying storage buys a clean implementation, but the price isn't zero. Chromium's `OnceCallback` is a single pointer in size (8 bytes), thanks to the `TRIVIAL_ABI` annotation plus reference-counted `BindState` — the callback object itself is just a pointer to a heap-allocated `BindState`. Our `OnceCallback` wraps a `std::move_only_function` (typically 32 bytes), plus the `Status` enum and the optional `CancelableToken` pointer (16 bytes), for a total of roughly 56-64 bytes.

The other difference is reference counting. Chromium's `BindState` is reference-counted, allowing multiple callbacks to share one binding state (essential for `RepeatingCallback`'s copy semantics). In our implementation, the `std::move_only_function` owns exclusively and supports no sharing. For `OnceCallback`'s move-only semantics that's no obstacle, but when we get to `RepeatingCallback` later, this piece will need rethinking.

On balance, we consider these trade-offs worth it: we spend object size and reference-counting flexibility to buy a large drop in implementation complexity. In real use, a 56-64-byte callback object is not the bottleneck in the vast majority of scenarios, and the code structure is clear, with far lower costs to maintain and extend.

In the next article we move into the implementation stage: starting from the core skeleton `run()`, we'll add `bind_once`, cancellation checks, and `then()` chaining step by step.

## References

- [Chromium Callback documentation](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
