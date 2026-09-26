---
chapter: 0
cpp_standard:
- 23
description: 'A deep dive into how the C++23 explicit object parameter (deducing this) lets OnceCallback::run() intercept lvalue calls elegantly at compile time, replacing the dual-overload hack from Chromium'
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
reading_time_minutes: 8
related:
- 'OnceCallback hands-on (II): building the core skeleton'
- 'OnceCallback prerequisite (IV): Concepts and requires constraints'
tags:
- host
- cpp-modern
- intermediate
- 模板
title: 'OnceCallback prerequisite (VI): Deducing this (C++23)'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-06-once-callback-deducing-this.md
  source_hash: 215c738b41d1aae64cf1e162d97bfb0144eb8d3e5bc7479e8da314453e601b29
  translated_at: '2026-09-26T00:53:17+00:00'
  engine: anthropic
  token_count: 3400
---
# OnceCallback prerequisite (VI): Deducing this (C++23)

## A first look at that declaration

`run()` is the most counterintuitive method in the entire OnceCallback component, and it is also the place where C++23 features are packed most densely. Its declaration looks like this:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;
```

The first time we saw the spelling `this Self&& self`, we stared at it for a good two seconds — a member function that gets to write `this` explicitly as a parameter? This is the C++23 "explicit object parameter", officially named deducing this. It is just one line, but with this single function template OnceCallback gets "lvalue invocation fails to compile, rvalue invocation runs just fine" done — a whole lot cleaner than the Chromium approach. In this piece we take the thing apart properly: the syntax, the deduction rules, and how OnceCallback borrows it to do compile-time interception.

## Framing the problem: why `cb.run()` must not compile

OnceCallback's core semantics come down to a single sentence: "invocable exactly once, and only on an rvalue". Translated into code:

```cpp
OnceCallback<int(int)> cb([](int x) { return x * 2; });

cb.run(5);                  // should fail to compile: cb is an lvalue
std::move(cb).run(5);       // should compile: std::move(cb) is an rvalue
```

What we want is a compile-time fork: an lvalue invocation blows up in red right in front of you, and the error message has to speak plain human language; an rvalue invocation gets waved through.

### Chromium had no C++23, so it wrote two overloads

Chromium at the time had no C++23 to work with, so it had to resort to a hack — write the same thing twice as a pair of overloads:

```cpp
// Rvalue version: the real execution
R Run() && {
    // Execute the callback...
}

// Lvalue version: compile error
R Run() const& {
    static_assert(!sizeof(*this),
        "OnceCallback::Run() may only be invoked on a non-const rvalue, "
        "i.e. std::move(callback).Run().");
}
```

One detail here kept us stuck for a while: why not just write `static_assert(false, "...")`? Because before C++23, a hardcoded `false` inside a template fires indiscriminately — even if that overload goes its whole life without being called once, the compiler blows up on you at the point of template definition. `!sizeof(*this)` is the detour: it depends on the type of `*this`, which makes it a dependent expression, so its evaluation has to wait until the moment of template instantiation. In other words, it only explodes if someone actually wrote `cb.Run()`; if nobody did, it might as well not exist.

It works, but it is genuinely inelegant. Two overloads doing one job is only half of it — the `!sizeof` hack also takes a good two seconds to parse every time you read it. Once C++23 landed deducing this, this business finally got a proper solution.

---

## deducing this: writing `this` as a parameter

What deducing this does boils down to a single move: take the `this` that used to hide implicitly inside the member function, drag it out, and write it explicitly as the first parameter — and along the way, give it template deduction.

### The syntax

```cpp
struct MyStruct {
    void f(this auto&& self) {
        // self is this — but its type is deduced
    }
};
```

Placing the `this` keyword in front of the type amounts to tipping off the compiler: what follows is not an ordinary parameter, it is the explicit object parameter. `auto&&` is the deduction placeholder — whoever calls the function, and on what, decides what the deduced type looks like.

### The deduction rules: identical to a forwarding reference

The deduction rules for `self` are cast from exactly the same mold as the forwarding reference we run into when writing templates — because the deduction context of `self` is equivalent to a template parameter. This point is critical; the lvalue interception later on leans on it.

Take an lvalue invocation like `obj.f()`: `self` deduces to `MyStruct&`, an lvalue reference. Switch to an rvalue invocation like `std::move(obj).f()` or `MyStruct{}.f()`, and `self` deduces to `MyStruct` — the bare type, no reference. Try a const lvalue invocation like `std::as_const(obj).f()`, and `self` obediently deduces to `const MyStruct&`. No shame in failing to memorize this — run it once and it falls out.

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

## Onto `run()`: how deducing this does its work

With the syntax out of the way, let's go straight to the full implementation of `run()` and watch it pin lvalue invocations down at compile time.

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

These few lines pack three interlocking mechanisms, and we will take them apart one by one.

### Intercepting lvalue calls

`std::is_lvalue_reference_v<Self>` is asking whether `Self` is an lvalue reference. The caller writes `cb.run(args)`; `cb` is an lvalue, so `Self` deduces to `OnceCallback&` — an lvalue reference. `is_lvalue_reference_v` returns `true`, the negation turns it into `false`, and the `static_assert` blows up on the spot, flinging the plain-language error message we wrote straight into the caller's face: `OnceCallback::run() must be called on an rvalue. Use std::move(cb).run(...) instead.`

Flip it around: write `std::move(cb).run(args)`. `std::move(cb)` is an rvalue (strictly speaking, an xvalue), `Self` deduces to `OnceCallback`, a non-reference, `is_lvalue_reference_v` returns `false`, the negation turns it into `true`, the assertion passes, and the code carries on. One push and one pull, and the fates of lvalue and rvalue fork apart at compile time.

### Forwarding to impl_run

Past the assertion, `std::forward<Self>(self)` hands `self` over unchanged to the real execution function, `impl_run`. Because the `static_assert` has already sealed off the lvalue path, any `Self` that makes it this far is necessarily a non-reference rvalue, so `std::forward<Self>(self)` dutifully returns an rvalue reference, guaranteeing that what `impl_run` receives is an rvalue. This step looks unremarkable, but it is the relay baton between "intercept" and "execute" — drop it and the semantics leak.

### A word on lazy instantiation

There is a detail here we chewed on for quite a while: the condition of the `static_assert` hangs off the template parameter `Self`, so it is only evaluated at the moment of template instantiation. The other way around: if `run()` is never called at all, this `static_assert` just lies there doing nothing — whether that `OnceCallback` object is itself an lvalue or an rvalue is none of its business. Only when somebody actually writes `cb.run(...)` on some line, forcing the compiler to instantiate this template, does the concrete type of `Self` get pinned down, and only then does the assertion deign to look up and evaluate.

This is lazy instantiation of templates — a function template that is not used is neither instantiated nor checked. It also explains why Chromium had no choice but `!sizeof(*this)`: before C++23, `static_assert(false)` had no dependency on template parameters, so it would blow up right at the template definition point, never living to see instantiation.

---

## Versus the traditional ref-qualifier: when to pick which

Two methods in OnceCallback both express the idea of "rvalue-only invocation" — `run()` uses deducing this, while `then()` uses the traditional ref-qualifier `&&`. We were puzzled at first too: if deducing this is so great, why not go all in and use it everywhere? It was only after laying the two scenarios' requirements side by side that it clicked — the level of finesse they need is simply different.

### then() is fine with a ref-qualifier

```cpp
template<typename Next>
auto then(Next&& next) && -> OnceCallback<...>;
```

What `then()` wants is plain: accept rvalues, slam the door on lvalues, and don't bother explaining the rejection. If a caller really writes `cb.then(next)` (an lvalue invocation), the compiler just tosses out a "no matching overload" and that is the end of it. The error message is rougher and less instructive than what deducing this gives, but it is serviceable. The ref-qualifier is also cheap to write — hang an `&&` on the tail and one character settles it.

### run() really needs deducing this

`run()` is far pickier. Merely rejecting lvalues is not enough — it also has to tell the caller "what you should have written is `std::move(cb).run(...)`, not `cb.run(...)`": a plain-language error message a person can act on immediately. deducing this paired with `static_assert` happens to do this very naturally: the message is one we stuff in ourselves, not the compiler's stock "no matching function" boilerplate.

### How to choose

So our verdict is this: if all you are after is the "rvalues only" constraint, `&&` is enough, and concise. If you additionally want to fling a custom, plain-language error at lvalue invocations, bring in deducing this with `static_assert`. Which tool to pick comes down to whether you owe the caller an explanation.

---

## Pitfall warnings

These are the pits we have fallen into here; steer around them while you are at it.

The explicit object parameter has one hard rule: it cannot appear together with a cv-qualifier or a ref-qualifier. The reasoning is easy enough to follow — the object type and the value category are already taken over by the explicit parameter, so if you stack a `const` on top or hang an `&&` off the end, the compiler is left bewildered: which of the two is actually in charge? So the following simply will not compile:

```cpp
struct Bad {
    void f(this auto&& self) const;   // compile error: an explicit object parameter cannot coexist with const
    void g(this auto&& self) &&;      // compile error: an explicit object parameter cannot coexist with &&
};
```

One more spot people misread: a function with an explicit object parameter looks like a static function, but it is not — you still need an object instance to call it. The `this` parameter is deduced by the compiler from the call expression; it is not something the caller stuffs in by hand. Don't get that backwards.

Finally, the toolchain threshold. deducing this is a C++23 feature; only GCC 14+, Clang 18+, and MSVC 19.34+ recognize it. If you are still on an older compiler, the only option is to fall back to Chromium's dual-overload approach — hackish as it is, at least it runs.

---

With that, the deducing this blade is fully sharpened. With this one function template, OnceCallback's `run()` forks lvalues and rvalues apart at compile time, and the bitter days of Chromium's two overloads plus the `!sizeof` hack are finally over. `then()` has no need for such ceremony — it just hangs an `&&` on the end for the sake of simplicity. How you pick between the tools comes down to whether you want to fling a plain-language error at the caller.

That wraps up the prerequisites. In the next piece we formally roll up our sleeves and build OnceCallback's skeleton.

## References

- [P0847R7 - Deducing this proposal](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [C++23's Deducing this (Microsoft C++ Blog)](https://devblogs.microsoft.com/cppblog/cpp23-deducing-this/)
- [cppreference: Explicit object parameter](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)
