---
chapter: 0
cpp_standard:
- 14
- 17
- 20
- 23
description: "A deep dive into mutable lambdas, init capture, C++20 lambda capture pack expansion, and generic lambdas — the core implementation techniques behind bind_once and then() in OnceCallback"
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
reading_time_minutes: 8
related:
- 'OnceCallback hands-on (III): implementing bind_once'
- 'OnceCallback hands-on (V): chaining with then'
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: 'OnceCallback prerequisite (III): advanced lambda features'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-03-once-callback-lambda-advanced.md
  source_hash: 402e8de3c2fdec5059c35265c4f4f18dc05f8b2eeba92c2252e914fb6df380db
  translated_at: '2026-09-26T00:52:10+00:00'
  engine: anthropic
  token_count: 4200
---
# OnceCallback prerequisite (III): advanced lambda features

In the previous cheat sheet we raced through the basics of lambda syntax; this piece digs in. The lambda features that actually pull the real weight in OnceCallback's implementation — `mutable`, init capture, and C++20 capture pack expansion — are anything but nice-to-have syntax sugar. If these three don't click for you, then reading the `bind_once` and `then()` code later will most likely have you cursing the author for being opaque every few lines. The truth is the author can't sidestep them either — sidestep them and OnceCallback simply cannot be built.

Let's take them apart one at a time, starting with `mutable` — and why this implementation can't spare it anywhere.

## mutable lambdas: why OnceCallback can't skip them

The `operator()` a lambda generates by default is `const`. In other words, for variables captured by value you can look but not touch inside the lambda body. Add `mutable`, and `operator()` becomes non-const — now the captured copies are yours to modify.

Here's a side-by-side:

```cpp
int x = 10;

// const lambda: captured variables cannot be modified
auto f1 = [x]() {
    // x++;  // compile error: operator() is const
    return x;
};

// mutable lambda: captured variables can be modified
auto f2 = [x]() mutable {
    x++;       // OK: operator() is non-const
    return x;
};

f2();  // returns 11, x's copy has been modified
f2();  // returns 12, the same lambda object called again, x keeps increasing
```

There's a detail that's easy to miss here — a `mutable` lambda's state **persists across calls**. The first call to `f2` returns 11, the second returns 12. The closure object holds copies of the captured variables, `mutable` hands `operator()` the right to modify those copies, and once modified they stay that way — the next call picks up where the last one left off. OnceCallback makes use of exactly this.

### The role it plays in OnceCallback

Every lambda inside `bind_once` and `then()` must be marked `mutable` — no negotiation. The reason boils down to one sentence: these lambdas' capture lists hold a `OnceCallback` object (via `self = std::move(*this)`, more on that in a moment), and the moment you call `std::move(self).run()` you have to mutate its internal state — flipping `status_` from kValid to kConsumed. If the lambda were const, `self` would be a const reference inside the body, and you'd be trying to run a state-mutating operation on a const object? The compiler would be the first to object.

```cpp
// the lambda inside then() — mutable is not optional
[self = std::move(*this), cont = std::forward<Next>(next)]
(FuncArgs... args) mutable -> NextRet {
    // self needs to be modified here (run() consumes it)
    auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
    return std::invoke(std::move(cont), std::move(mid));
}
```

---

## Init capture: moving objects into the lambda

C++14 handed us a new toy — init capture. The syntax looks like this: `name = expression`. You run an expression right there in the capture list and use its result to initialize a brand-new captured variable. It sounds unremarkable, but it fixes the single biggest pain point of C++11 lambdas.

### How it differs from simple capture

Simple capture `[x]` can only grab variables that already exist, and it's copy-or-reference, pick one. Init capture `[name = expr]` adds a layer and can do three things simple capture flatly cannot:

```cpp
auto ptr = std::make_unique<int>(42);

// 1. move capture — moves the unique_ptr into the lambda
auto f1 = [p = std::move(ptr)]() { return *p; };
// ptr out here has already been emptied

// 2. store a computed result
std::string s = "hello";
auto f2 = [len = s.size()]() { return len; };  // len is of type size_t

// 3. capture a variable that doesn't exist outside
auto f3 = [counter = 0]() mutable { return ++counter; };  // counter is the lambda's own variable
```

The first one is the killer. C++11 lambdas have no move capture, so if you wanted to stuff a `unique_ptr` into a lambda you had to take the scenic route — park it in a `std::function` or hand-roll a functor. Before P0780, Chromium's `base::Bind` carried this gap on its back with hand-written functors. Once init capture arrived, those hacks were pretty much ready for the museum.

### How OnceCallback uses it

In `then()`'s implementation, init capture shoulders two loads.

The first — moving the entire OnceCallback object into the lambda:

```cpp
self = std::move(*this)
```

`*this` is the current OnceCallback object. `std::move(*this)` turns it into an rvalue, and the init capture `self = std::move(*this)` triggers OnceCallback's move constructor in place, sweeping `func_`, `status_`, and `token_` wholesale into the lambda's closure object. After the move, the outer `*this` is a hollowed-out shell — `func_` is empty, `token_` is null, essentially "dead". This step is the core action of OnceCallback's move-only semantics: ownership slides sideways into the lambda, just like that.

The second — moving the continuation callback in:

```cpp
cont = std::forward<Next>(next)
```

`std::forward<Next>(next)` preserves `next`'s value category as-is — an rvalue coming in gets moved, an lvalue gets copied. In actual use, `then()` mostly receives temporary lambdas (rvalues), so this usually takes the move path.

### The ownership chain

Look at these two steps together: the new lambda that `then()` builds holds complete ownership of both the original callback and the continuation. That lambda then gets stuffed into a new `OnceCallback`'s `std::move_only_function`. The whole ownership chain nests layer inside layer:

```mermaid
graph LR
    A["new OnceCallback"] --> B["move_only_function"] --> C["lambda closure"] --> D["original OnceCallback + continuation"]
```

Every layer passes ownership along through move semantics — no sharing anywhere, no copies anywhere. OnceCallback's move-only discipline propagates from the outside all the way to the bottom inside `then()`, with no leaks along the way.

---

## C++20 lambda capture pack expansion: the secret to bind_once's brevity

This is the feature that genuinely lets `bind_once` get away with a few lines of code. Before C++20, a variadic template's parameter pack couldn't be expanded directly into a lambda's capture list — you had to pack the arguments into a `std::tuple` first, then unpack the call with `std::apply` inside the lambda. Roundabout, but there was no alternative.

### The old approach (C++17): tuple + apply

```cpp
template<typename F, typename... BoundArgs>
auto bind_old(F&& f, BoundArgs&&... args) {
    // pack all bound arguments into a tuple
    return [f = std::forward<F>(f),
            tup = std::make_tuple(std::forward<BoundArgs>(args)...)]
        (auto&&... call_args) mutable -> decltype(auto) {
        // expand the tuple with std::apply and call
        return std::apply([&](auto&... bound) -> decltype(auto) {
            return f(bound..., std::forward<decltype(call_args)>(call_args)...);
        }, tup);
    };
}
```

It works, but the code is impressively bloated — a tuple wedged in the middle, `std::apply` wrapped around it, and yet another nested lambda inside to handle the expansion. The full three-piece set.

### The new syntax (C++20): expanding the pack right in the capture list

C++20 finally relented and allows pack expansion in a lambda's init capture. The syntax is `...name = expression`, and the effect is one separately generated capture variable per type in the parameter pack.

```cpp
template<typename F, typename... BoundArgs>
auto bind_new(F&& f, BoundArgs&&... args) {
    return [f = std::forward<F>(f),
            ...bound = std::forward<BoundArgs>(args)]  // ← pack expansion!
        (auto&&... call_args) mutable -> decltype(auto) {
        return std::invoke(std::move(f),
                          std::move(bound)...,         // ← expand the captured variables
                          std::forward<decltype(call_args)>(call_args)...);
    };
}
```

### Hand-expanding a concrete example

Let's take a concrete call and see what the compiler actually does behind the scenes. Suppose we call `bind_new([](int a, std::string b, int c) { ... }, 10, std::string("hello"))`, so `BoundArgs = {int, std::string}`. The compiler expands the pack `...bound = std::forward<BoundArgs>(args)` into:

```cpp
[f = std::forward<F>(f),
 b1 = std::forward<int>(arg1),              // int forwarded as-is
 b2 = std::forward<std::string>(arg2)]      // std::string move-forwarded
(auto&&... call_args) mutable -> decltype(auto) {
    return std::invoke(std::move(f),
                      std::move(b1), std::move(b2),    // expand the captured variables
                      std::forward<decltype(call_args)>(call_args)...);
}
```

Each bound argument transforms into an independent member variable inside the lambda's closure. When the lambda gets called, `std::move(bound)...` expands them all in one sweep and hands them to `std::invoke`.

### Why std::move instead of std::forward

There's a trap here that nearly got the author on first read. Inside the lambda we use `std::move(bound)...`, not `std::forward<BoundArgs>(bound)...`. Why?

The key is that the lambda is `mutable`, so the captured variable `bound` is an **lvalue** inside the lambda body — a named variable is always an lvalue, no way around it. We want the bound arguments to go out as rvalues when the callback fires (triggering moves), so we need `std::move` to convert them. If your hand slips and you write `std::forward<BoundArgs>(bound)`, since `bound` is already an lvalue, `std::forward` won't touch its value category at all — it still returns an lvalue reference, and the move semantics evaporate on the spot. OnceCallback is move-only; losing the move here means losing ownership, and everything downstream falls apart.

---

## Generic lambdas: auto&& as a forwarding reference

One last thing: the signature of `bind_once`'s inner lambda, `(auto&&... call_args)`. This spelling exists to receive the arguments passed in at call time. Here `auto&&` is a forwarding reference — `auto` in a lambda parameter is equivalent to a template parameter, so `auto&&` gets exactly the same deduction rules as `T&&` (when T is a template parameter).

```cpp
auto f = [](auto&& x) {
    // x is a forwarding reference
    // lvalue passed in: auto = int&, x's type is int& (lvalue reference)
    // rvalue passed in: auto = int, x's type is int&& (rvalue reference)
};

int v = 10;
f(v);       // x binds to an lvalue
f(10);      // x binds to an rvalue
```

The `auto&&...` combination opens the lambda wide open: any number of arguments of any type can be pushed in, and the value category of each one (lvalue or rvalue) is remembered. Pair that with `std::forward<decltype(call_args)>(call_args)...`, and those arguments get perfectly forwarded to the final callable, with not one bit of information lost.

---

Next up we'll look at Concepts and `requires` constraints — the key line of defense protecting OnceCallback's templated constructors from matching the wrong things.

## References

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
- [P0780R2 - Pack Expansion in Lambda Init-Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
