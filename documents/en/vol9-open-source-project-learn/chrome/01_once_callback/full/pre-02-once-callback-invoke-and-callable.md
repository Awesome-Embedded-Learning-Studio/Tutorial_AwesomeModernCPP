---
chapter: 0
cpp_standard:
- 17
description: "A deep dive into how std::invoke unifies the calling syntax for function pointers, member function pointers, lambdas, and functors, and the role std::invoke_result_t plays in type deduction inside OnceCallback"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
- 'OnceCallback prerequisite (I): function types and template partial specialization'
reading_time_minutes: 8
related:
- 'OnceCallback hands-on (III): implementing bind_once'
- 'OnceCallback hands-on (V): chaining with then'
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_invoke
title: 'OnceCallback prerequisite (II): std::invoke and the uniform calling protocol'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-02-once-callback-invoke-and-callable.md
  source_hash: 5b3d6bea87c0811ba5fee927085629aef4473d743399c2345a7b2cfef2ace33d
  translated_at: '2026-09-26T00:40:47+00:00'
  engine: anthropic
  token_count: 2400
---
# OnceCallback prerequisite (II): std::invoke and the uniform calling protocol

In the previous piece we got function types and template partial specialization straightened out; this one takes on an even more irritating problem: the calling syntax for callable objects.

The most natural wish when writing a callback system is that whatever gets passed in — function pointer, lambda, member function pointer — we can run it with one single spelling. But C++ refuses to grant you that courtesy. A plain function is happy with `f(args...)`; a member function pointer insists on `(obj.*pmf)(args...)`, and even a pointer to a data member has to go through the whole `obj.*pmd` routine. Ten kinds of callable objects, and you end up writing ten branches in your template to decide which family each one belongs to. Keep that up and your blood pressure will climb.

`std::invoke` (C++17) is what smears this pile of syntax into a single layer. Both `bind_once` and `then()` inside OnceCallback rely on it entirely — that's how they manage to call whatever you pass in correctly.

## The problem: fragmented calling syntax for callable objects

Let's lay a few common callable objects side by side so you can see how glaring the split is.

Plain function pointers are the best-behaved — either spelling works:

```cpp
int add(int a, int b) { return a + b; }
int (*fp)(int, int) = &add;

int result = fp(3, 4);       // direct call
int result2 = (*fp)(3, 4);   // dereference first, then call (equivalent)
```

Lambdas and functors go through `operator()`, which looks almost the same as calling a plain function, so this part is reasonably friendly:

```cpp
auto lam = [](int a, int b) { return a + b; };
int result = lam(3, 4);  // called via operator()

struct Adder {
    int operator()(int a, int b) { return a + b; }
};
Adder fn;
int result2 = fn(3, 4);  // also called via operator()
```

Things start to contort at member function pointers. They can't take a direct `()` the way plain functions do; you need an object instance first, then you bolt the pointer on with `.*` or `->*`, two operators so obscure most people forget they exist. The first time we wrote one of these, we spent half a day digging through books:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
int (Calculator::*pmf)(int, int) = &Calculator::multiply;

// The .* operator is mandatory
int result = (calc.*pmf)(3, 4);  // result == 12
```

And then there is an even more obscure category: pointers to data members. C++ lets you take a "pointer" to a data member; at heart it's an offset, and getting at it likewise goes through `.*`:

```cpp
struct Point {
    double x, y;
};

Point p{1.0, 2.0};
double Point::*pmx = &Point::x;

double val = p.*pmx;  // val == 1.0
```

You see the shape of the problem. If you're writing a template function that must call a callable object whose concrete type you know nothing about, there is no way to write down one uniform syntax. You don't know whether it's a function or a member pointer; get either one wrong and the compile fails. `std::invoke` exists to plug exactly this hole.

---

## The dispatch rules of std::invoke

What `std::invoke(f, args...)` does boils down to one sentence: watch the concrete types of `f` and `args`, and pick the correct calling syntax. The standard calls this set of rules the INVOKE expression, and it falls into three major categories.

The trickiest one — and the one most worth committing to memory — is member function pointers. When `f` is a pointer to a member function and the first element of `args` is the object itself (a reference works, a value works, and so does a pointer to the object), `std::invoke` expands it into `(obj.*pmf)(rest...)`:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;

// via reference
std::invoke(&Calculator::multiply, calc, 3, 4);        // (calc.*multiply)(3, 4)
// via pointer
std::invoke(&Calculator::multiply, &calc, 3, 4);       // ((*ptr).*multiply)(3, 4)
```

Pay attention to the `&calc` in the second line. When the first argument is a pointer, `std::invoke` dereferences it for you before applying `.*`. The behavior looks unremarkable, but it's a lifesaver when `bind_once` binds a member function — we'll see that later on.

Pointers to data members take the same route, except "calling" is replaced by "accessing":

```cpp
struct Point { double x, y; };
Point p{1.0, 2.0};

double val = std::invoke(&Point::x, p);    // p.*&Point::x == p.x
```

The remaining crowd — function pointers, lambdas, functors, anything you can slap `()` onto directly — `std::invoke` dutifully turns into `f(args...)` for you:

```cpp
std::invoke([](int a, int b) { return a + b; }, 3, 4);  // lambda(3, 4)
```

Put the three categories together and the whole point is one sentence: whichever of the three families your `f` falls into, what you write is always the single shape `std::invoke(f, args...)`. Your template code no longer needs to know what exactly `f` is; the dispatch is handled for you inside `std::invoke`.

---

## std::invoke_result_t: deducing the return type at compile time

Unified calling alone isn't enough. Sometimes you have to ask at compile time "what type does `std::invoke(f, args...)` actually return?" — the chained implementation of `then()` being the canonical case. You feed the previous callback's return value into the next callback, and the compiler has to work out in advance what type the whole chain ultimately spits out; otherwise you can't even put the type signature down on paper.

`std::invoke_result_t<F, Args...>` is what computes this. Hand it a callable type `F` plus a set of argument types `Args...`, and it works out the return type of `std::invoke(f, args...)` at compile time:

```cpp
#include <type_traits>
#include <functional>

auto add(int a, int b) -> int { return a + b; }

// Deduce the return type of add(1, 2) at compile time
using R = std::invoke_result_t<decltype(add), int, int>;
static_assert(std::is_same_v<R, int>);

// Works for lambdas too
auto lam = [](double x) { return std::to_string(x); };
using R2 = std::invoke_result_t<decltype(lam), double>;
static_assert(std::is_same_v<R2, std::string>);
```

## How the pair is actually used in the OnceCallback source

Let's read straight off the OnceCallback source — clearer than talking in the abstract. `std::invoke` appears in it twice, once serving `bind_once` and once `then()`.

First, this snippet inside `bind_once`:

```cpp
// Inside bind_once's lambda
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

That `f` takes all comers: it might be a lambda, might be a member function pointer, might even be a pointer to a data member. Skip `std::invoke` and write `f(bound..., call_args...)` directly, and the moment a member function pointer arrives the build fails on the spot — member function pointers can't take a bare `()` at all.

The snippet in `then()` follows the same logic:

```cpp
// The non-void branch of then()
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

Here `cont` (the continuation) is by design an ordinary callable, most likely a lambda, so in theory `cont(mid)` would probably run fine too. Then why wrap it in `std::invoke`? As defensive writing. The day somebody's hand slips and a member function pointer gets passed in as the continuation, the direct-call syntax dies on the spot, while `std::invoke` doesn't. Routing everything through it spares us the trouble of carving out holes for special types.

As for how `then()` uses `std::invoke_result_t` to deduce the return type, the requirement is very specific: in a chain, after the next callback `next` receives the previous callback's return value, what does it itself return? In code it reads like this:

```cpp
// In the non-void branch of then()
using NextRet = std::invoke_result_t<NextType, ReturnType>;
// NextRet is "the type you get back when you pass a value of type ReturnType to next"
```

On the void branch the continuation takes no arguments at all, so the calling shape simplifies:

```cpp
// In the void branch of then()
using NextRet = std::invoke_result_t<NextType>;
// next takes no arguments; just call it
```

## Pitfall warning: the lifetime trap in member function binding

`std::invoke` unifies the calling syntax, but there is one thing it doesn't look after: when the object dies. This is an especially easy place to trip, because the convenience of unified calling lulls you into forgetting there's still a raw pointer hiding underneath.

Binding a member function in `bind_once` looks like this:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
```

That `&calc` is a raw pointer, and `bind_once` stores it into the lambda's capture list as-is. If `calc` destructs before the callback actually runs, what the lambda is left clutching is a dangling pointer; `std::invoke` keeps reaching through it to touch memory all the same — undefined behavior, and nine times out of ten a segfault. `std::invoke` can't help you here; it has no idea whether the pointer you passed in is valid in the first place.

Chromium handles this thoroughly in `//base`: `base::Unretained` lets you explicitly declare "I'll answer for this raw pointer's lifetime myself"; `base::Owned` simply hands the object's ownership over to the callback framework to manage; and `base::WeakPtr` automatically invalidates the callback when the object destructs. Our OnceCallback is a simplified teaching edition, so we hold this layer of protection back for now — the safety burden stays on the caller. We'll come back to this trade-off in the hands-on articles later, and then you'll understand why Chromium insisted on inventing a mechanism like `WeakPtr`.

In the next piece we'll look at advanced lambda features, especially C++20 init-capture pack expansion — that's the key to why `bind_once` can be written so cleanly.

## References

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
