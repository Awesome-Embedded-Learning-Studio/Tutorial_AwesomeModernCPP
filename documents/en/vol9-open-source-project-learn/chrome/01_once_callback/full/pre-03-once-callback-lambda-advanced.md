---
chapter: 0
cpp_standard:
- 14
- 17
- 20
- 23
description: "A deep look at mutable lambdas, init capture, C++20 lambda capture pack
  expansion, and generic lambdas: the core techniques behind bind_once and then() in
  OnceCallback."
difficulty: intermediate
order: 3
platform: host
prerequisites:
- OnceCallback prerequisites cheat sheet: a recap of core C++11/14/17 features
reading_time_minutes: 8
related:
- OnceCallback hands-on (III): implementing bind_once
- OnceCallback hands-on (V): chaining with then
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: 'OnceCallback Prerequisites (Part 3): Advanced Lambda Features'
---
# OnceCallback Prerequisites (Part 3): Advanced Lambda Features

The last cheat sheet ran through lambda's basic syntax in a hurry. This one digs in. A handful of lambda features do the real heavy lifting in `OnceCallback`'s implementation: `mutable`, init capture, and C++20 capture pack expansion. These are not garnish syntax. If you don't get a handle on them, reading `bind_once` and `then()` later will have you cursing the author for being opaque. Truth is, there's no way around them; without them OnceCallback simply can't be built.

Let's take them one at a time, starting with `mutable`, and why it can't be dropped from a single line in this implementation.

## mutable lambda: why OnceCallback can't do without it

The `operator()` a lambda generates is `const` by default. In other words, value-captured variables are look-don't-touch inside the body. Add `mutable` and `operator()` becomes non-const, and the captured copies are yours to modify.

Here's the contrast:

```cpp
int x = 10;

// const lambda: cannot modify captured variables
auto f1 = [x]() {
    // x++;  // compile error: operator() is const
    return x;
};

// mutable lambda: can modify captured variables
auto f2 = [x]() mutable {
    x++;       // OK: operator() is non-const
    return x;
};

f2();  // returns 11; x's copy has been modified
f2();  // returns 12; same lambda object called again, x keeps climbing
```

There's a detail that's easy to miss: a `mutable` lambda's state **persists across calls**. `f2` returns 11 the first time, 12 the second. The closure object holds copies of the captured variables, and `mutable` hands `operator()` the right to mutate them. Once changed, the change stays put, and the next call picks up where the last one left off. OnceCallback happens to need exactly this.

### Its role inside OnceCallback

Every lambda inside `bind_once` and `then()` is marked `mutable`. No exceptions. The reason comes down to one line: their capture lists stash a `OnceCallback` object (via `self = std::move(*this)`, which we'll get to in a moment), and once you call `std::move(self).run()` you have to mutate its internal state, flipping `status_` from kValid to kConsumed. If that lambda were const, `self` inside the body would be a const reference, and you'd be trying to run a state-mutating operation on a const object. The compiler is the first thing that won't stand for that.

```cpp
// the lambda inside then(): mutable is non-negotiable
[self = std::move(*this), cont = std::forward<Next>(next)]
(FuncArgs... args) mutable -> NextRet {
    // self has to be mutated here (run() consumes it)
    auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
    return std::invoke(std::move(cont), std::move(mid));
}
```

---

## Init capture: moving objects into the lambda

C++14 handed us a new toy: init capture. The syntax is `name = expression`. You run an expression right there in the capture list and use the result to initialize a fresh capture variable. It sounds minor, but it patches the single biggest pain point C++11 lambdas had.

### How it differs from simple capture

A simple capture `[x]` can only grab a variable that already exists, and you choose copy or reference, full stop. An init capture `[name = expr]` adds a layer; it can do three things simple capture flat-out cannot:

```cpp
auto ptr = std::make_unique<int>(42);

// 1. Move capture: move the unique_ptr into the lambda
auto f1 = [p = std::move(ptr)]() { return *p; };
// ptr out here has been emptied

// 2. Store a computed result
std::string s = "hello";
auto f2 = [len = s.size()]() { return len; };  // len has type size_t

// 3. Capture a variable that doesn't exist outside
auto f3 = [counter = 0]() mutable { return ++counter; };  // counter is the lambda's own variable
```

The first one is the killer. C++11 lambdas have no move capture. To get a `unique_ptr` into a lambda you had to take a long detour: stuff it into a `std::function` or hand-roll a function object. Before P0780, Chromium's `base::Bind` carried this gap on its back with manually written functors. Once init capture arrived, those hacks basically belonged in a museum.

### How OnceCallback uses it

In `then()`'s implementation, init capture pulls double duty.

The first job is moving the entire OnceCallback object into the lambda:

```cpp
self = std::move(*this)
```

`*this` is the current OnceCallback object. `std::move(*this)` casts it to an rvalue, and the init capture `self = std::move(*this)` fires OnceCallback's move constructor in place, dragging `func_`, `status_`, and `token_` wholesale into the lambda's closure object. After the move, the outer `*this` is a hollowed-out shell: `func_` is empty, `token_` is null, effectively "dead." This is the core action behind OnceCallback's move-only semantics; ownership slides sideways into the lambda.

The second job is moving the continuation callback in:

```cpp
cont = std::forward<Next>(next)
```

`std::forward<Next>(next)` preserves `next`'s value category as-is: if an rvalue came in, it moves; if an lvalue, it copies. In real use of `then()`, what gets passed is usually a temporary lambda (an rvalue), so this generally takes the move path.

### The ownership chain

Looking at both steps together, the new lambda `then()` produces holds the complete ownership of both the original callback and the continuation. That lambda then gets tucked into a fresh `OnceCallback`'s `std::move_only_function`. The whole ownership chain nests layer by layer:

```mermaid
graph LR
    A["New OnceCallback"] --> B["move_only_function"] --> C["lambda closure"] --> D["Original OnceCallback + continuation"]
```

Every layer hands ownership down through move semantics: no sharing, no copying. OnceCallback's move-only discipline, inside `then()`, threads from the outside all the way in with no gaps.

---

## C++20 lambda capture pack expansion: the secret to bind_once's brevity

This is the one feature in this post that lets `bind_once` come together in a few lines of code. Before C++20, a variadic template's parameter pack could not be expanded directly into a lambda's capture list. You had to bundle the arguments into a `std::tuple` first, then unpack the call inside the lambda with `std::apply`. Roundabout, but there was no other way.

### The old way (C++17): tuple + apply

```cpp
template<typename F, typename... BoundArgs>
auto bind_old(F&& f, BoundArgs&&... args) {
    // pack every bound argument into a tuple
    return [f = std::forward<F>(f),
            tup = std::make_tuple(std::forward<BoundArgs>(args)...)]
        (auto&&... call_args) mutable -> decltype(auto) {
        // unpack the tuple with std::apply and call
        return std::apply([&](auto&... bound) -> decltype(auto) {
            return f(bound..., std::forward<decltype(call_args)>(call_args)...);
        }, tup);
    };
}
```

It works, but the code is bloated to say the least: a tuple in the middle, `std::apply` on the outside, and another nested lambda inside to handle the expansion. The whole three-piece kit.

### The new syntax (C++20): expand the pack right in the capture list

C++20 finally relented and allows pack expansion inside a lambda's init capture. The syntax is `...name = expression`, and the effect is to generate a separate capture variable for each type in the parameter pack.

```cpp
template<typename F, typename... BoundArgs>
auto bind_new(F&& f, BoundArgs&&... args) {
    return [f = std::forward<F>(f),
            ...bound = std::forward<BoundArgs>(args)]  // ← pack expansion!
        (auto&&... call_args) mutable -> decltype(auto) {
        return std::invoke(std::move(f),
                          std::move(bound)...,         // ← expand the capture variables
                          std::forward<decltype(call_args)>(call_args)...);
    };
}
```

### Manually expanding a concrete example

Let's take a specific call and watch what the compiler actually does behind the scenes. Suppose we call `bind_new([](int a, std::string b, int c) { ... }, 10, std::string("hello"))`, so `BoundArgs = {int, std::string}`. The compiler expands the pack `...bound = std::forward<BoundArgs>(args)` into:

```cpp
[f = std::forward<F>(f),
 b1 = std::forward<int>(arg1),              // int, forwarded directly
 b2 = std::forward<std::string>(arg2)]      // std::string, move-forwarded
(auto&&... call_args) mutable -> decltype(auto) {
    return std::invoke(std::move(f),
                      std::move(b1), std::move(b2),    // expand the capture variables
                      std::forward<decltype(call_args)>(call_args)...);
}
```

Each bound argument turns into an independent member variable in the lambda closure. When the lambda gets invoked, they all expand together through `std::move(bound)...` and feed into `std::invoke`.

### Why std::move and not std::forward

There's a trap here that nearly tripped me up the first time I read it. Inside the lambda body it's `std::move(bound)...`, not `std::forward<BoundArgs>(bound)...`. Why?

The key is that the lambda is `mutable`, so the captured variable `bound` is an **lvalue** inside the body: a named variable is always an lvalue, no exceptions. We want the bound arguments to go out as rvalues when the callback fires (to trigger a move), so we need `std::move` to cast them to rvalues. If your hand slips and you write `std::forward<BoundArgs>(bound)`, since `bound` is already an lvalue, `std::forward` won't touch its value category at all: it still returns an lvalue reference, and move semantics evaporate on the spot. OnceCallback is move-only, so dropping the move here equals dropping ownership, and everything downstream goes sideways.

---

## Generic lambda: auto&& as a forwarding reference

One last thing about the signature of the lambda inside `bind_once`: `(auto&&... call_args)`. This form is there to receive the arguments passed in at runtime. Here `auto&&` is a forwarding reference: `auto` in a lambda parameter is equivalent to a template parameter, so `auto&&` gets the exact same deduction rules as `T&&` (when T is a template parameter).

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

The `auto&&...` combination opens the lambda up to accept any number of arguments of any type, all while remembering each one's value category (lvalue or rvalue). Paired with `std::forward<decltype(call_args)>(call_args)...`, those arguments get perfectly forwarded to the final callable object, with no information lost along the way.

---

Next we'll look at Concepts and `requires` constraints: the key defense that keeps OnceCallback's template constructor from matching the wrong things.

## References

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)
- [P0780R2 - Pack Expansion in Lambda Init-Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
