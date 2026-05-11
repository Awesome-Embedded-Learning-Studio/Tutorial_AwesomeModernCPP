# OnceCallback in Practice (Part 3): Implementing bind_once

## Introduction

The core skeleton is in place, and `run()` can now consume callbacks. However, every time we construct a `OnceCallback`, we must pass a callable with a signature named `R(Args...)`, and all arguments are only provided at call time. In practice, we often encounter situations where some arguments are already known when the callback is created, and only a subset of arguments needs to be deferred until invocation. `bind_once` solves this problem by pre-packing the "known arguments" into the callback, so the caller only needs to worry about the "unknown arguments."

In this post, we break down the implementation of `bind_once` line by line and manually expand a complete template instantiation example, so you can see exactly what the compiler does behind the scenes.

> **Learning Objectives**
>
> - Understand what problem argument binding solves
> - Understand the complete implementation of `bind_once` line by line
> - Manually expand a specific template instantiation to see what the compiler does
> - Understand why `Signature` must be explicitly specified

---

## What Problem Does Argument Binding Solve

First, let's look at a scenario without `bind_once`. Suppose you have a three-argument function, but the first two arguments are known at binding time:

```cpp
int compute(int x, int y, int z) {
    return x + y + z;
}

// 没有 bind_once：每次调用都得传三个参数
auto cb = OnceCallback<int(int, int, int)>(compute);
int r = std::move(cb).run(10, 20, 30);  // r == 60
```

If `x = 10` and `y = 20` are determined at binding time, and only `z` needs to be deferred until invocation, we want to get a `OnceCallback<int(int)>` that takes just one argument.

Without `bind_once`, you have to manually write a lambda wrapper:

```cpp
auto wrapped = OnceCallback<int(int)>(
    [](int z) { return compute(10, 20, z); }
);
int r = std::move(wrapped).run(30);  // r == 60
```

This works, but if there are many arguments or complex types (such as binding a move-only `unique_ptr`), hand-writing lambdas becomes very tedious. `bind_once` simply automates this "wrap in a lambda" process.

```cpp
auto bound = bind_once<int(int)>(compute, 10, 20);
int r = std::move(bound).run(30);  // r == 60
```

---

## Line-by-Line Breakdown of bind_once

Following along with the source code, let's understand what `bind_once` does line by line.

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

### Template Parameters

`bind_once` has three template parameters. `Signature` is the function signature of the target callback (e.g., `int(int)`), which must be explicitly specified by the caller. `F` is the type of the callable object (lambda closure type, function pointer type, etc.), deduced by the compiler from the first function argument. `BoundArgs...` is the type pack of the bound arguments, also deduced by the compiler.

### Lambda Capture List

The capture list is the most ingenious part of the entire implementation. `f = std::forward<F>(funtor)` uses init capture to perfectly forward the callable object into the lambda closure—if an rvalue is passed in, it is moved in; if an lvalue is passed in, it is copied in.

`...bound = std::forward<BoundArgs>(args)` is the lambda init capture pack expansion introduced in C++20. It generates a corresponding captured variable for each type in `BoundArgs...`, with each variable initialized via `std::forward` perfect forwarding. Assuming `BoundArgs = {int, std::string}`, the expansion is equivalent to:

```cpp
[f = std::forward<F>(funtor),
 b1 = std::forward<int>(arg1),
 b2 = std::forward<std::string>(arg2)]
```

### Lambda Parameters and mutable

`(auto&&... call_args)` is a forwarding reference parameter of a generic lambda—runtime arguments are received through it. `auto&&` here is equivalent to `T&&` in a template parameter, making it a forwarding reference.

The `mutable` keyword cannot be omitted—the lambda body needs to call `std::move(f)` and `std::move(bound)...`, which modify the captured variables. If the lambda is const, the captured variables are const inside it, and we cannot move from a const object.

### Lambda Body

```cpp
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

`std::invoke` uniformly handles all types of callable objects—as covered in Prerequisite Knowledge (Part 2). `std::move(f)` passes the callable object out as an rvalue, `std::move(bound)...` passes all bound arguments out as rvalues (because captured variables inside a `mutable` lambda are lvalues, requiring `std::move` to convert them to rvalues), and `std::forward<decltype(call_args)>(call_args)...` perfectly forwards the runtime arguments.

Bound arguments come first (`std::move(bound)...`), followed by runtime arguments (`call_args...`). This order is crucial—it determines which arguments are "pre-bound" and which are deferred until invocation.

---

## Manually Expanding a Concrete Example

Let's use a concrete call example to manually expand the complete code after template instantiation. Suppose:

```cpp
struct Calc {
    int multiply(int a, int b) { return a * b; }
};

Calc calc;
auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

### Template Argument Deduction

`Signature = int(int)` (explicitly specified), `F = int (Calc::*)(int, int)` (member function pointer type), `BoundArgs = {Calc*, int}` (object pointer + first argument).

### Lambda Capture Expansion

```cpp
[f = std::forward<int (Calc::*)(int, int)>(&Calc::multiply),
 b1 = std::forward<Calc*>(&calc),
 b2 = std::forward<int>(5)]
```

`f` captures the member function pointer, `b1` captures the object pointer, and `b2` captures the bound integer 5.

### std::invoke Expansion Inside the Lambda Body

When `bound.run(8)` is called, `call_args = {8}`. `std::invoke` receives:

```cpp
std::invoke(std::move(f), std::move(b1), std::move(b2), 8)
```

Which is:

```cpp
std::invoke(&Calc::multiply, &calc, 5, 8)
```

`std::invoke` detects that the first argument is a member function pointer and the second argument is a pointer to an object, so it expands to:

```cpp
((*(&calc)).*(&Calc::multiply))(5, 8)
```

This is equivalent to `calc.multiply(5, 8)`, yielding a result of `40`.

### Lifetime Pitfall

Note that `b1 = std::forward<Calc*>(&calc)` captures a raw pointer `&calc`. `bind_once` does not manage the lifetime of `calc`. If `calc` is destroyed before the callback is invoked, the lambda holds a dangling pointer, and `std::invoke` accesses freed memory through it—undefined behavior (UB).

Chromium uses `base::Unretained` to explicitly mark the safety of raw pointers, `base::Owned` to take ownership, and `base::WeakPtr` to automatically cancel the callback when the object is destructed. Our simplified version temporarily leaves the safety responsibility to the caller.

---

## Why the Signature Must Be Explicitly Specified

You may have noticed that `int(int)` in `bind_once<int(int)>(...)` must be written manually. Ideally, the compiler should be able to automatically deduce the remaining signature from the callable's signature and the number of bound arguments. But in C++, this is harder than it sounds.

For a function pointer `R(*)(Args...)`, we could extract the parameter list through template partial specialization, then use compile-time "type list slicing" to drop the first N types. For functors with a determined signature, we could extract the signature via `decltype(&T::operator())`. But for **generic lambdas** (`[](auto x) { ... }`), its `operator()` is itself a template, meaning there is no single determined signature—the compiler cannot obtain information about "what arguments this lambda accepts" at the type level.

Chromium wrote hundreds of lines of template metaprogramming code to handle various edge cases. For teaching purposes, having the caller write one extra template parameter `int(int)` is the more pragmatic choice.

---

## Summary

In this post, we broke down the implementation of `bind_once` line by line. It uses C++20's lambda capture pack expansion to expand bound arguments into the lambda's capture list, uses `std::invoke` to uniformly handle various callable objects (especially member function pointers), and uses the `mutable` keyword to allow the lambda to modify captured variables internally. We manually expanded a complete template instantiation process for member function binding, seeing exactly how `std::invoke` expands a member function pointer plus an object pointer into an ordinary member function call. Finally, we discussed why `Signature` must be explicitly specified—the existence of generic lambdas makes automatic deduction extremely complex.

In the next post, we will look at the cancellation token design—a lightweight cancellation mechanism implemented with `shared_ptr` and `atomic<bool>`.

## References

- [Chromium bind_internal.h source code](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
