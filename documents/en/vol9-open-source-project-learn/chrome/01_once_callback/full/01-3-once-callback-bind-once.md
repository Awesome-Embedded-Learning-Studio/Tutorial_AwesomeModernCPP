---
chapter: 1
cpp_standard:
- 23
description: "A line-by-line teardown of bind_once's parameter binding, from the motivation through the lambda capture pack expansion, finishing with a complete template instantiation example unfolded by hand."
difficulty: beginner
order: 3
platform: host
prerequisites:
- OnceCallback hands-on (II): the core skeleton
- OnceCallback prerequisites (II): std::invoke and the uniform call protocol
- OnceCallback prerequisites (III): advanced lambda features
reading_time_minutes: 7
related:
- OnceCallback hands-on (IV): the cancellation token
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: "OnceCallback hands-on (III): implementing bind_once"
---
# OnceCallback hands-on (III): implementing bind_once

The skeleton is in place and `run()` can consume callbacks. But there's a familiar friction you hit pretty quickly: every time you build a `OnceCallback` you have to feed it a callable with the full signature, every argument handed over at the call site. The real world is rarely that tidy. More often a couple of arguments are pinned down when the callback is created, and only the remaining one or two have to wait until the call.

That's what `bind_once` is for. It bakes the "already decided" arguments into the callback ahead of time, so the caller only has to supply the rest. In this piece we'll walk through its implementation line by line, then unfold a full template instantiation by hand so you can see exactly what the compiler is doing behind your back.

Let's start with the no-`bind_once` version. Say there's a three-parameter function, and the first two arguments are already known at bind time:

```cpp
int compute(int x, int y, int z) {
    return x + y + z;
}

// Without bind_once: every call has to pass all three arguments
auto cb = OnceCallback<int(int, int, int)>(compute);
int r = std::move(cb).run(10, 20, 30);  // r == 60
```

If `x = 10` and `y = 20` are settled at bind time and only `z` has to come in at call time, what we really want is a `OnceCallback<int(int)>` that takes a single argument.

Without `bind_once`, the only option is to wrap it in a hand-written lambda:

```cpp
auto wrapped = OnceCallback<int(int)>(
    [](int z) { return compute(10, 20, z); }
);
int r = std::move(wrapped).run(30);  // r == 60
```

It works. But once the parameter list grows or the types get awkward (say, binding a move-only `unique_ptr`), writing that lambda by hand gets old fast. What `bind_once` does is automate the "wrap it in a lambda" step.

```cpp
auto bound = bind_once<int(int)>(compute, 10, 20);
int r = std::move(bound).run(30);  // r == 60
```

## A line-by-line teardown of the bind_once implementation

Let's lay the full source out, then chew through it piece by piece.

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

## From the template parameters down into the lambda body

The template parameters are the entry point, so look there first. `bind_once` carries three. `Signature` is the target callback's signature (something like `int(int)`), and it has to be written by hand; the compiler can't deduce it. `F` is the type of the callable object (a lambda closure type, a function pointer, and so on), deduced from the first argument. `BoundArgs...` is the type pack of the bound arguments, following along with the trailing arguments. The last two are CTAD's job; only the first one has to come from you.

Next is the capture list, the most intricate part of the whole implementation. `f = std::forward<F>(funtor)` uses an init capture to perfectly forward the callable into the closure: if an rvalue came in, it gets moved in; if an lvalue, it gets copied in, and the value category is preserved the whole way. The line below it, `...bound = std::forward<BoundArgs>(args)`, is the lambda init capture pack expansion that C++20 brought in. It hands each type in `BoundArgs...` its own capture variable, each initialized through `std::forward`. If `BoundArgs = {int, std::string}`, the expansion comes out equivalent to:

```cpp
[f = std::forward<F>(funtor),
 b1 = std::forward<int>(arg1),
 b2 = std::forward<std::string>(arg2)]
```

The parameter list `(auto&&... call_args)` is what receives the arguments handed in at runtime. `auto&&` here is equivalent to `T&&` for a template parameter, that is, a forwarding reference, not an rvalue reference. Beginners read past that distinction all the time.

The `mutable` keyword is not something you can drop. Inside the lambda body we call `std::move(f)` and `std::move(bound)...`, and both of those need to modify the captured variables. A lambda without `mutable` is const, and the captures inside are const along with it. You can't move out of a const object, and the compiler will throw it right back at you.

The last layer is the lambda body:

```cpp
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

`std::invoke` was covered in prerequisites (II); it's the catch-all that handles every shape of callable object, member function pointers included. `std::move(f)` and `std::move(bound)...` fling the captured values out as rvalues. Captured variables inside a `mutable` lambda are lvalues in their own right, so turning them into rvalues on the way out takes an explicit `std::move`. The `call_args...` line just perfect-forwards the runtime arguments as they came.

There's an ordering here worth watching: bound arguments first, runtime arguments after. It isn't arbitrary. It directly decides which arguments are "pre-bound" and which are held back for the call. Get it backwards and the signature won't line up with the arguments.

## Unfolding a concrete example by hand

Reading the source only gets you so far. Let's take one concrete call and lay out what the template turns into once it's instantiated, so we can see exactly what the compiler generated. Suppose:

```cpp
struct Calc {
    int multiply(int a, int b) { return a * b; }
};

Calc calc;
auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

## Stepping through the template expansion

First, the parameter deduction. `Signature = int(int)` is what you wrote; no argument there. `F = int (Calc::*)(int, int)`, the member function pointer type the compiler reads off `&Calc::multiply`. `BoundArgs = {Calc*, int}`, an object pointer plus the first argument.

The capture list expands into:

```cpp
[f = std::forward<int (Calc::*)(int, int)>(&Calc::multiply),
 b1 = std::forward<Calc*>(&calc),
 b2 = std::forward<int>(5)]
```

`f` grips the member function pointer, `b1` grips the object pointer, `b2` grips the bound integer 5.

Now look at what happens when `bound.run(8)` is actually called. At that moment `call_args = {8}`, and the `std::invoke` inside the lambda body receives:

```cpp
std::invoke(std::move(f), std::move(b1), std::move(b2), 8)
```

Which is:

```cpp
std::invoke(&Calc::multiply, &calc, 5, 8)
```

`std::invoke` sees that the first argument is a member function pointer and the second is a pointer to an object, and it applies the member-call rule:

```cpp
((*(&calc)).*(&Calc::multiply))(5, 8)
```

That's equivalent to `calc.multiply(5, 8)`, which gives `40`. The whole trick is just `std::invoke`'s member-function-pointer overload doing its job.

## A lifetime trap hiding here

`b1 = std::forward<Calc*>(&calc)` captures a raw pointer, `&calc`. `bind_once` doesn't manage `calc`'s lifetime for you at all. If `calc` gets destroyed before the callback runs, the lambda is left holding a dangling pointer, and `std::invoke` reaches through it into freed memory. That's undefined behavior, a textbook use-after-free.

Chromium patches this three ways: `base::Unretained` to mark "I vouch for it being alive" explicitly, `base::Owned` to take ownership outright, and `base::WeakPtr` so the callback invalidates itself the moment the object destructs. Our stripped-down version takes the easy road and pushes the responsibility onto the caller. In real production code you'd want one of those three in place.

## Why the signature has to be spelled out

You've probably noticed that the `int(int)` in `bind_once<int(int)>(...)` has to be written by hand. Ideally the compiler would take the callable's signature and the bound argument count and figure out the remaining signature on its own. In C++ that turns out to be a great deal harder than it looks.

A function pointer `R(*)(Args...)` is the easy case: a template partial specialization can dig out the parameter list, and a compile-time "type list slice" chops off the first N. A functor with a fixed call signature is also fine, `decltype(&T::operator())` cracks it in one shot. The real wall is the generic lambda (`[](auto x) { ... }`). Its `operator()` is itself a template, so it has no single determined signature, and at the type level there's no way to ask "what arguments does this lambda take."

Chromium wrote hundreds of lines of template metaprogramming to paper over those edge cases. There's no point in following it down that road for a teaching version. Making the caller type one extra `int(int)` is the best value for the effort.

In the next piece we look at how to build the cancellation token, a lightweight cancellation mechanism stitched together from `shared_ptr` and `atomic<bool>`.

## References

- [Chromium bind_internal.h source](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
