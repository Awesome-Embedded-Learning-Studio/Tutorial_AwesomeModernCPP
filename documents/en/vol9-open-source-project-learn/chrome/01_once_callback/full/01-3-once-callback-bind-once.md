---
chapter: 1
cpp_standard:
- 23
description: "A line-by-line teardown of the bind_once parameter-binding implementation — from the motivation through the lambda capture pack expansion, ending with a complete template instantiation example unfolded by hand"
difficulty: beginner
order: 3
platform: host
prerequisites:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback prerequisite (II): std::invoke and the uniform calling protocol'
- 'OnceCallback prerequisite (III): advanced lambda features'
reading_time_minutes: 7
related:
- 'OnceCallback hands-on (IV): designing the cancellation token'
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: "OnceCallback hands-on (III): implementing bind_once"
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/01-3-once-callback-bind-once.md
  source_hash: 0038fe2a0717889372b29e8d4a56b562c78052172fb0a155352130e171cdbbea
  translated_at: '2026-09-26T00:28:59+00:00'
  engine: anthropic
  token_count: 2900
---
# OnceCallback hands-on (III): implementing bind_once

The skeleton stands, and `run()` can consume callbacks. But as we kept writing, we bumped into a very common bit of friction: every time you construct a OnceCallback you have to stuff a callable with the complete signature into it, and every argument has to be handed over at the moment of the call. Reality is rarely that tidy — nine times out of ten, a few arguments are already nailed down when the callback is created, and only the remaining one or two have to wait for the call site.

That is exactly what `bind_once` is for. It stuffs the already-decided arguments into the callback ahead of time, and the caller only has to supply the rest. In this piece we pick its implementation apart line by line, then unfold a complete template instantiation by hand, so you can see exactly what moves the compiler makes behind the curtain.

First, the picture without `bind_once`. Say we have a three-parameter function whose first two arguments can be settled at bind time:

```cpp
int compute(int x, int y, int z) {
    return x + y + z;
}

// Without bind_once: every call has to pass all three arguments
auto cb = OnceCallback<int(int, int, int)>(compute);
int r = std::move(cb).run(10, 20, 30);  // r == 60
```

If `x = 10` and `y = 20` are fixed at bind time and only `z` has to come in at call time, what we really want is a `OnceCallback<int(int)>` that accepts a single argument.

Without `bind_once`, your only option is to hand-write a lambda wrapper:

```cpp
auto wrapped = OnceCallback<int(int)>(
    [](int z) { return compute(10, 20, z); }
);
int r = std::move(wrapped).run(30);  // r == 60
```

It runs. But once the parameters pile up and the types get complicated (binding a move-only `unique_ptr`, say), the hand-written lambda starts to grate. What `bind_once` does is automate that wrap-it-in-a-lambda step.

```cpp
auto bound = bind_once<int(int)>(compute, 10, 20);
int r = std::move(bound).run(30);  // r == 60
```

## A line-by-line teardown of the full bind_once implementation

Let's put the whole source on the table first, and chew through it one segment at a time.

```cpp
template<typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& funtor, BoundArgs&&... args) {
    return OnceCallback<Signature>(
        [f = std::forward<F>(funtor),
         ...bound = std::forward<BoundArgs>(args)]
        (auto&&... call_args) mutable -> decltype(auto) {
            return std::invoke(
                std::move(f),
                std::move(bound)...,
                std::forward<decltype(call_args)>(call_args)...
            );
        }
    );
}
```

## From the template parameters to the lambda body

The template parameters come first — they are the entrance. `bind_once` carries three of them at the top: `Signature` is the target callback's signature (`int(int)`, say) — you must write it yourself, the compiler cannot deduce it; `F` is the type of the callable (a lambda closure, a function pointer, that sort of thing), deduced from the first argument; `BoundArgs...` is the pack of bound-argument types, following the trailing arguments. The latter two are CTAD's work; only the first one falls to you personally.

Next comes the capture list, the most delicate piece of the whole implementation. `f = std::forward<F>(funtor)` uses an init capture to perfectly forward the callable into the closure: an rvalue coming in gets moved in, an lvalue coming in gets copied in, and the value category is preserved the whole way down. The next line, `...bound = std::forward<BoundArgs>(args)`, is the lambda init-capture pack expansion that C++20 brought in: it hands one capture variable to every type in `BoundArgs...`, each initialized through `std::forward`. If `BoundArgs = {int, std::string}`, the finished expansion is equivalent to:

```cpp
[f = std::forward<F>(funtor),
 b1 = std::forward<int>(arg1),
 b2 = std::forward<std::string>(arg2)]
```

The parameter list `(auto&&... call_args)` takes in the ones that are passed in only at run time. Here `auto&&` is the same thing as `T&&` for a template parameter — a forwarding reference, not an rvalue reference. Newcomers misread that all the time.

Whatever you do, do not drop the `mutable` keyword. The lambda body calls `std::move(f)` and `std::move(bound)...`, and both of those operations modify the captured variables. A lambda without `mutable` is const, and the captures inside it are const along with it — you cannot move from a const object, and the compiler will shoot you down on the spot.

The last layer is the lambda body:

```cpp
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

`std::invoke` was covered in Prerequisite (II): it uniformly catches every shape of callable, member function pointers included. `std::move(f)` and `std::move(bound)...` fling the captured objects out as rvalues — because captured variables inside a `mutable` lambda are themselves lvalues, shipping them out as rvalues takes an explicit `std::move` — while the `call_args...` line perfect-forwards the run-time arguments as they came.

There is one ordering detail to keep an eye on: bound arguments first, run-time arguments after. That is not an arbitrary arrangement — it directly decides which parameters get "pre-bound" and which ones wait for the moment of the call. Get it backwards, and the signature and the arguments no longer line up.

## Unfolding a concrete example by hand

Staring at the source still leaves a layer between you and the machinery. So let's take one concrete call, spread out what the template instantiation looks like by hand, and see what the compiler actually generates. Suppose:

```cpp
struct Calc {
    int multiply(int a, int b) { return a * b; }
};

Calc calc;
auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

## Laying the template out step by step

Deduce the parameters first. `Signature = int(int)` is what you wrote, no arguing there; `F = int (Calc::*)(int, int)` — the member-function-pointer type the compiler deduced from `&Calc::multiply`; `BoundArgs = {Calc*, int}`, an object pointer plus the first argument.

The capture list expands to this:

```cpp
[f = std::forward<int (Calc::*)(int, int)>(&Calc::multiply),
 b1 = std::forward<Calc*>(&calc),
 b2 = std::forward<int>(5)]
```

`f` clamps onto the member function pointer, `b1` onto the object pointer, and `b2` onto the bound integer 5.

Then let's watch what happens when `bound.run(8)` actually gets called. At that moment `call_args = {8}`, and the `std::invoke` in the lambda body receives the arguments:

```cpp
std::invoke(std::move(f), std::move(b1), std::move(b2), 8)
```

That is:

```cpp
std::invoke(&Calc::multiply, &calc, 5, 8)
```

`std::invoke` sees a member function pointer in the first slot and a pointer to the object in the second, and expands it automatically under the member-invocation rules:

```cpp
((*(&calc)).*(&Calc::multiply))(5, 8)
```

which is equivalent to `calc.multiply(5, 8)`, giving `40`. The whole magic is just `std::invoke`'s member-function-pointer overload catching the fall.

## There is a lifetime trap here

`b1 = std::forward<Calc*>(&calc)` captures the raw pointer `&calc`. `bind_once` does not manage `calc`'s life and death for you at all. If `calc` gets destroyed before the callback runs, what sits inside the lambda is a dangling pointer, and `std::invoke` follows it into freed memory — undefined behavior, a textbook use-after-free.

Chromium applies three patches to this spot: `base::Unretained` explicitly marks "I vouch that it is alive", `base::Owned` takes ownership over outright, and `base::WeakPtr` invalidates the callback automatically the moment the object is destroyed. Our simplified edition takes the easy road for now and dumps the burden on the caller — but once you head for production, you really should bolt on one of the three.

## Why the signature must be specified explicitly

You have probably noticed that the `int(int)` in `bind_once<int(int)>(...)` has to be written in by hand. Ideally, the compiler would deduce the remaining signature automatically from the callable's signature and the number of bound arguments. In C++, though, that job turns out to be far more troublesome than it sounds.

A function pointer `R(*)(Args...)` is still the easy case: template partial specialization digs the parameter list out, and a compile-time "type-list slice" chops off the first N. Functors with a fixed signature work too — `decltype(&T::operator())` and you are done in one shot. The real hard nut is the generic lambda (`[](auto x) { ... }`): its `operator()` is itself a template, so no single determined signature exists at all, and at the type level the compiler cannot obtain the piece of information "what arguments does this lambda actually take".

To catch all these edge cases, Chromium wrote a solid few hundred lines of template metaprogramming. A teaching edition has no reason to compete in that arms race — letting the caller type one extra `int(int)` is the best value-for-effort arrangement.

In the next piece we will look at how to build the cancellation token — a lightweight cancellation mechanism stitched together from a `shared_ptr` and an `atomic<bool>`.

## References

- [The Chromium bind_internal.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
