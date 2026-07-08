---
chapter: 0
cpp_standard:
- 17
description: "How std::invoke unifies the calling convention for function pointers,
  member function pointers, lambdas, and functors, and how std::invoke_result_t drives
  type deduction inside OnceCallback"
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'OnceCallback prerequisites: a C++11/14/17 refresher'
- 'OnceCallback prerequisites (I): function types and template partial specialization'
reading_time_minutes: 8
related:
- 'OnceCallback in practice (III): implementing bind_once'
- 'OnceCallback in practice (V): chaining with then()'
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_invoke
title: 'OnceCallback prerequisites (II): std::invoke and the uniform calling convention'
---
# OnceCallback prerequisites (II): std::invoke and the uniform calling convention

In the previous post we untangled function types and template partial specialization. This one tackles a more annoying problem: the calling syntax for callable objects.

When you write a callback system, the natural wish is that whatever comes in, a function pointer, a lambda, a member function pointer, you can fire it with one syntax. C++ does not oblige. A free function is fine as `f(args...)`, but a member function pointer insists on `(obj.*pmf)(args...)`, and even a pointer to a data member has to go through `obj.*pmd`. Ten kinds of callables, ten branches in your template to figure out which family it belongs to. Do that long enough and your blood pressure goes up.

`std::invoke` (C++17) is what glues this pile of syntax into one layer. OnceCallback's `bind_once` and `then()` both lean on it internally, so they can correctly call whatever you hand them.

## The problem: the calling syntax for callables is fragmented

Let's line up the common callables side by side. The split is hard to miss.

A plain function pointer is the well-behaved one, anything works:

```cpp
int add(int a, int b) { return a + b; }
int (*fp)(int, int) = &add;

int result = fp(3, 4);       // Direct call
int result2 = (*fp)(3, 4);   // Dereferenced, then called (equivalent)
```

Lambdas and functors both go through `operator()`, which looks almost like a free function call, so this part is friendly:

```cpp
auto lam = [](int a, int b) { return a + b; };
int result = lam(3, 4);  // Through operator()

struct Adder {
    int operator()(int a, int b) { return a + b; }
};
Adder fn;
int result2 = fn(3, 4);  // Also through operator()
```

Things get ugly at the member function pointer. It can't be `()`-ed like a free function. You need an object instance first, then you bolt it on with the `.*` or `->*` operators, two of the rarer sights in the language. The first time we wrote one of these we flipped through a book for half an afternoon:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
int (Calculator::*pmf)(int, int) = &Calculator::multiply;

// Must use the .* operator
int result = (calc.*pmf)(3, 4);  // result == 12
```

There's a colder cousin: the pointer to data member. C++ lets you take a "pointer" to a data member, which is really an offset, and access goes through `.*` the same way:

```cpp
struct Point {
    double x, y;
};

Point p{1.0, 2.0};
double Point::*pmx = &Point::x;

double val = p.*pmx;  // val == 1.0
```

You see the shape of the problem. If you're writing a template that has to call "a callable of a completely unknown type," there's no single syntax that works. You don't know whether it's a function or a member pointer, and one wrong guess fails to compile. `std::invoke` is what plugs that hole.

---

## The dispatch rules of std::invoke

What `std::invoke(f, args...)` does, in one sentence: it looks at the concrete types of `f` and `args`, and picks the right calling syntax. The standard calls this the INVOKE expression and splits it into three families.

The thorniest, and the one worth memorizing, is the member function pointer. When `f` is a pointer to a member function and the first element of `args` is the object itself (a reference, a value, or a pointer to the object), `std::invoke` expands it into `(obj.*pmf)(rest...)`:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;

// By reference
std::invoke(&Calculator::multiply, calc, 3, 4);        // (calc.*multiply)(3, 4)
// By pointer
std::invoke(&Calculator::multiply, &calc, 3, 4);       // ((*ptr).*multiply)(3, 4)
```

Note the second line, the `&calc` one. When the first argument is a pointer, `std::invoke` dereferences it for you before going through `.*`. It looks unremarkable, but it saves the day when `bind_once` binds a member function, as we'll see further down.

Pointers to data members take the same route, just with "access" where "call" used to be:

```cpp
struct Point { double x, y; };
Point p{1.0, 2.0};

double val = std::invoke(&Point::x, p);    // p.*&Point::x == p.x
```

The rest, function pointers, lambdas, functors, anything you can slap `()` onto, `std::invoke` faithfully calls as `f(args...)`:

```cpp
std::invoke([](int a, int b) { return a + b; }, 3, 4);  // lambda(3, 4)
```

Put the three families together and the takeaway is one line: no matter which one your `f` falls into, you always write `std::invoke(f, args...)`. Your template code no longer has to know what kind of thing `f` is. Dispatch is `std::invoke`'s problem, done for you internally.

---

## std::invoke_result_t: deducing the return type at compile time

Unified calling alone is not enough. Sometimes you have to ask, at compile time, "what type does `std::invoke(f, args...)` return?" The chained implementation of `then()` is the textbook case. You feed the previous callback's return value into the next callback, and the compiler has to compute the final type of the chain up front, or you can't even write the type signature.

That's what `std::invoke_result_t<F, Args...>` computes. Hand it a callable type `F` and a pack of argument types `Args...`, and at compile time it gives you back the return type of `std::invoke(f, args...)`:

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

## How the two of them show up in the OnceCallback source

Reading it straight out of the OnceCallback source is clearer than any abstract explanation. `std::invoke` appears twice in there, once for `bind_once` and once for `then()`.

Here's the `bind_once` piece:

```cpp
// Inside bind_once's lambda
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

That `f` accepts anything: a lambda, a member function pointer, even a pointer to a data member. If you skipped `std::invoke` and wrote `f(bound..., call_args...)` directly, the moment `f` is a member function pointer it would not compile. Member function pointers cannot be `()`-ed directly, period.

The `then()` piece follows the same logic:

```cpp
// The non-void branch of then()
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

Here `cont` (the continuation callback) is by design a plain callable, usually a lambda, so in theory `cont(mid)` would mostly work. So why wrap it in `std::invoke`? As a defensive habit. The day someone slips in a member function pointer as the continuation, the direct-call syntax dies on the spot, while `std::invoke` won't. Routing everything through it saves you the trouble of carving out special cases for odd types.

As for how `then()` uses `std::invoke_result_t` to deduce the return type, the need is concrete: in a chain, the next callback `next` takes the previous callback's return value and produces something in turn. The code reads like this:

```cpp
// In the non-void branch of then()
using NextRet = std::invoke_result_t<NextType, ReturnType>;
// NextRet is "what type you get when you hand a ReturnType value to next"
```

In the `void` branch, the continuation takes no argument, so the call shape simplifies:

```cpp
// In the void branch of then()
using NextRet = std::invoke_result_t<NextType>;
// next takes no arguments, just call it
```

## Trap warning: the lifetime pitfall of binding a member function

`std::invoke` unifies the calling syntax, but it does not manage one thing: when the object dies. This is an easy trap to fall into, because the convenience of unified calling makes you forget there's a raw pointer hiding underneath.

When you bind a member function in `bind_once`, it looks like this:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
```

That `&calc` is a raw pointer, and `bind_once` stores it verbatim in the lambda's capture. If `calc` destructs before the callback actually runs, the lambda is clutching a dangling pointer. `std::invoke` still reaches through it to grope at memory, undefined behavior, segfault nine times out of ten. `std::invoke` can't help you here. It has no idea whether the pointer you handed it is even valid.

Chromium handles this carefully in `//base`: `base::Unretained` lets you explicitly declare "I vouch for this raw pointer's lifetime myself," `base::Owned` hands object ownership to the callback framework, and `base::WeakPtr` automatically voids the callback when the object destructs. Our OnceCallback here is a simplified teaching version, so we don't ship that layer of protection yet. The safety burden stays on the caller. We'll come back to this trade-off in the hands-on chapters, and then it'll be clear why Chromium felt it had to invent something like `WeakPtr`.

Next, we move on to advanced lambda features, especially the C++20 init-capture pack expansion, which is the trick that lets `bind_once` be written this cleanly.

## References

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
