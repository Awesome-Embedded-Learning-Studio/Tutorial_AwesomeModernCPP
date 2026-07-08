---
chapter: 0
cpp_standard:
- 23
description: "How C++23 explicit object parameters (deducing this) let OnceCallback::run() intercept lvalue calls at compile time, replacing Chromium's double-overload hack"
difficulty: intermediate
order: 6
platform: host
prerequisites:
- OnceCallback prerequisites recap: a tour of the core C++11/14/17 features
reading_time_minutes: 8
related:
- 'OnceCallback hands-on (II): the core skeleton'
- 'OnceCallback prerequisites (IV): concepts and requires constraints'
tags:
- host
- cpp-modern
- intermediate
- 模板
title: 'OnceCallback Prerequisites (VI): Deducing this (C++23)'
---
# OnceCallback Prerequisites (VI): Deducing this (C++23)

## First, that one line of declaration

`OnceCallback::run()` is the most counter-intuitive method in the whole component, and the densest spot for C++23 features. Its declaration looks like this:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;
```

That `this Self&& self` made us pause for a couple seconds the first time we saw it. A member function that writes `this` explicitly as a parameter? This is C++23's *explicit object parameter*, officially known as deducing this. It's just one line, but it lets OnceCallback pull off "lvalue call fails to compile, rvalue call runs fine" with a single function template, far cleaner than what Chromium does. This piece takes the thing apart: the syntax, the deduction rules, and how OnceCallback leans on it for compile-time interception.

## Stating the problem clearly: why `cb.run()` must not compile

OnceCallback's core semantic is one sentence: call it once, and only on an rvalue. Translated to code:

```cpp
OnceCallback<int(int)> cb([](int x) { return x * 2; });

cb.run(5);                  // must fail to compile: cb is an lvalue
std::move(cb).run(5);       // must compile: std::move(cb) is an rvalue
```

What we want is a compile-time dispatch mechanism: an lvalue call blows up immediately with an error message that actually reads like English; an rvalue call goes through.

### Chromium had no C++23, so it wrote two overloads

Back then Chromium didn't have C++23, so it had to hack it: two overloads for the same thing.

```cpp
// Rvalue version: the actual execution
R Run() && {
    // execute the callback...
}

// Lvalue version: compile error
R Run() const& {
    static_assert(!sizeof(*this),
        "OnceCallback::Run() may only be invoked on a non-const rvalue, "
        "i.e. std::move(callback).Run().");
}
```

One detail we got stuck on for a while: why not just `static_assert(false, "...")`? Because before C++23, writing a literal `false` inside a template fires unconditionally: even if this overload is never called once, the compiler blows up at the point of template definition. `!sizeof(*this)` is the workaround. It depends on the type of `*this`, so it's a dependent expression and only gets evaluated at template instantiation. In other words, it only blows up if someone actually wrote `cb.Run()`; if nobody did, it's as if it doesn't exist.

It works, but it's not elegant. Two overloads doing one job, and the `!sizeof` hack takes a second to parse each time you read it. Once C++23 landed deducing this, there was a proper answer.

---

## deducing this: writing `this` as a parameter

What deducing this does is one sentence: it takes the `this` that's normally implicit inside a member function, drags it out, and writes it explicitly as the first parameter, with template deduction bolted on.

### Syntax

```cpp
struct MyStruct {
    void f(this auto&& self) {
        // self is this — but its type is deduced
    }
};
```

The `this` keyword placed before the type is a hint to the compiler: what follows isn't a normal parameter, it's an explicit object parameter. `auto&&` is the deduction placeholder; whoever calls it and how they call it decides what the deduced type looks like.

### Deduction rules: identical to a forwarding reference

The deduction rule for `self` is the exact same mold as the forwarding reference you hit when writing templates, because `self`'s deduction context is equivalent to a template parameter. This point matters; the lvalue interception later hinges on it.

Take an lvalue call like `obj.f()`: `self` deduces to `MyStruct&`, an lvalue reference. Switch to an rvalue call like `std::move(obj).f()` or `MyStruct{}.f()`: `self` deduces to `MyStruct`, the bare type, no reference. And a const lvalue call like `std::as_const(obj).f()`: `self` dutifully deduces to `const MyStruct&`. Hard to keep in your head? Just run it and see.

### Run it to verify

```cpp
#include <iostream>
#include <type_traits>

struct Check {
    void test(this auto&& self) {
        using Self = decltype(self);
        if constexpr (std::is_lvalue_reference_v<Self>) {
            std::cout << "lvalue reference\n";
        } else {
            std::cout << "rvalue (not a reference)\n";
        }
    }
};

int main() {
    Check c;
    c.test();                  // prints: lvalue reference
    std::move(c).test();       // prints: rvalue (not a reference)
    std::as_const(c).test();   // prints: lvalue reference (const)
}
```

---

## Onto `run()`: how deducing this actually works

With the syntax in hand, let's look at `run()`'s full implementation and see how it pins lvalue calls down at compile time.

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

Three interlocking mechanisms are crammed into these few lines. We'll take them one at a time.

### Intercepting lvalue calls

`std::is_lvalue_reference_v<Self>` is asking: is `Self` an lvalue reference? The caller writes `cb.run(args)`; `cb` is an lvalue, so `Self` deduces to `OnceCallback&`, an lvalue reference. `is_lvalue_reference_v` returns `true`, the negation turns it to `false`, the `static_assert` fires on the spot, and the human-readable error message we wrote gets thrown in the caller's face: `OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead.`

Flip it around. Write `std::move(cb).run(args)`: `std::move(cb)` is an rvalue (strictly, an xvalue), `Self` deduces to `OnceCallback`, not a reference. `is_lvalue_reference_v` returns `false`, negation flips it to `true`, the assert passes, and code marches on. One deduction, one negation, and lvalue versus rvalue part ways at compile time.

### Forwarding to impl_run

Past the assert, `std::forward<Self>(self)` hands `self` off to the real execution function, `impl_run`, unchanged. Because the `static_assert` has already sealed the lvalue path, any `Self` that reaches this point has to be a non-reference rvalue, so `std::forward<Self>(self)` honestly returns an rvalue reference and guarantees `impl_run` gets an rvalue. It looks unremarkable, but it's the baton pass between "intercept" and "execute"; drop it and the semantics leak.

### A word on lazy instantiation

There's a detail we chewed on for a good while here. The `static_assert` condition carries the template parameter `Self`, so it's only evaluated at the moment of template instantiation. The other way around: if `run()` is never called at all, that `static_assert` just sits there doing nothing, regardless of whether the `OnceCallback` object itself is an lvalue or an rvalue. Only when somebody actually writes `cb.run(...)` on some line, forcing the compiler to instantiate this template, does `Self`'s concrete type get pinned down, and only then does the assert bother to look up and compute.

This is template lazy instantiation: a function template that isn't used isn't instantiated, and isn't checked. It also explains why Chromium had to reach for `!sizeof(*this)`: before C++23, `static_assert(false)` had no dependency on a template parameter, so it would blow up at the template definition point, well before instantiation ever happened.

---

## Versus the traditional ref-qualifier: when to pick which

Two methods in OnceCallback express the same idea of "rvalue-only call": `run()` uses deducing this, while `then()` uses the traditional ref-qualifier `&&`. We puzzled over this at first: if deducing this is so great, why not go all in and use it everywhere? It clicked once we laid the two scenarios side by side. The granularity they need isn't the same at all.

### then() is fine with just a ref-qualifier

```cpp
template<typename Next>
auto then(Next&& next) && -> OnceCallback<...>;
```

`then()`'s needs are plain: take rvalues, slam the door on lvalues, and don't bother explaining why. If the caller writes `cb.then(next)` (an lvalue call), the compiler just shrugs "no matching overload" and moves on. The error message is rougher and less instructive than what deducing this gives you, but it's enough. The ref-qualifier is also less to write: tack `&&` on the end, one character, done.

### run() really needs deducing this

`run()` is much pickier. Just refusing lvalues isn't enough; it also has to tell the caller "what you should write is `std::move(cb).run(...)`, not `cb.run(...)`", a human-readable error that lets them fix it on the spot. Deducing this paired with `static_assert` happens to do this very naturally: the error message is one we get to write ourselves, not the compiler's "no matching function" template.

### How to choose

Our call: if all you want is the "rvalues only" constraint, `&&` is enough and cleaner. If you also owe the caller a custom, human-readable error for lvalue calls, reach for deducing this plus `static_assert`. Which tool you pick comes down to whether you need to explain.

---

## Pitfall warnings

Here are the potholes we hit, so you can steer around them.

Explicit object parameters come with a hard rule: they cannot coexist with a cv-qualifier or a ref-qualifier. The reasoning is straightforward enough. The object type and value category are already handled by the explicit parameter, so stacking a `const` on top or tacking on `&&` confuses the compiler about who's in charge. The following won't compile:

```cpp
struct Bad {
    void f(this auto&& self) const;   // error: explicit object parameter cannot also be const
    void g(this auto&& self) &&;      // error: explicit object parameter cannot also have &&
};
```

One more thing people trip on: an explicit-object-parameter function looks like a static function, but it isn't. You still need an object instance to call it. The `this` parameter is deduced by the compiler from the call expression; it isn't something the caller hands in manually. Don't get that backwards.

Finally, toolchain. deducing this is a C++23 feature, and only GCC 14+, Clang 18+, and MSVC 19.34+ recognize it. If you're still on an older compiler, you fall back to Chromium's double-overload approach. It's a hack, but at least it runs.

---

That covers deducing this as a tool. With one function template, `OnceCallback::run()` splits lvalue and rvalue at compile time, and the grim days of Chromium's two overloads plus the `!sizeof` hack are finally over. `then()` doesn't need any of that; a `&&` on the end keeps it simple. How you pick between the tools comes down to whether you owe the caller a human-readable error.

That wraps up the prerequisites. Next piece, we start actually building OnceCallback's skeleton.

## References

- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
