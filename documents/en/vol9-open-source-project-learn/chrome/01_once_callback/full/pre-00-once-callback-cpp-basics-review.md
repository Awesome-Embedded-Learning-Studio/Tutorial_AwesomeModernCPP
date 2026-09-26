---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: A quick review of all the C++ fundamentals the OnceCallback series needs — move semantics, perfect forwarding, variadic templates, smart pointers, atomics, lambdas, type traits, and more — so you are fully prepared for the deep dives ahead
difficulty: intermediate
order: 0
platform: host
prerequisites:
- 'Volume 1: C++ Fundamentals'
reading_time_minutes: 14
related:
- 'OnceCallback prerequisite (I): function types and template partial specialization'
- 'OnceCallback prerequisite (III): advanced lambda features'
tags:
- host
- cpp-modern
- intermediate
- 基础
- 入门
title: 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-00-once-callback-cpp-basics-review.md
  source_hash: 80368a37a21d333784c060971d915f117ab32fedd4720e1c0b6fabd2063b20ab
  translated_at: '2026-09-26T00:41:06+00:00'
  engine: anthropic
  token_count: 8000
---
# OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features

Let's get this piece's positioning straight first: this is not a from-zero tutorial. If move semantics and smart pointers are still completely foreign to you, go chew through Volume Two and come back; this piece assumes you've learned all of this once and simply got rusty. The OnceCallback series will lean on a batch of C++11/14/17 features over and over — move semantics, perfect forwarding, variadic templates, smart pointers, atomics, lambdas, type traits — and we're going to run through them all in one go here. For each one we'll cover exactly three things: what it is, how to use it, and where OnceCallback needs it. After reading this, you can flip through the design articles that follow without getting stalled on the spot by some syntax detail.

## Move semantics and std::move

The entire foundation of OnceCallback sits right here. It is itself a move-only type, and its core design is held up entirely by move semantics, so in this section we'll quickly straighten out the core concepts.

### Rvalue references and the move constructor

C++11 introduced the rvalue reference `T&&`, which can bind to temporary objects (rvalues). The move constructor `T(T&& other)` carries the semantics of "steal the resources from `other` instead of copying them". After the theft, `other` is left in a "valid but unspecified" state — usually emptied out.

```cpp
// A minimal move semantics example
class Buffer {
    int* data_;
    std::size_t size_;
public:
    // ordinary constructor
    Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    // move constructor: steal other's resources
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // empty out the source object
        other.size_ = 0;
    }
    ~Buffer() { delete[] data_; }
};

Buffer a(100);          // a owns 100 ints
Buffer b = std::move(a); // b stole a's resources; a is left empty
```

### What std::move actually moves

`std::move` doesn't move anything. It is just a `static_cast<T&&>` that unconditionally converts the object you pass in to an rvalue reference. What actually performs the "move" is the move constructor or the move assignment operator. `std::move`'s role is to raise a hand and report — telling the compiler "I agree to treat this object as an rvalue; you may steal resources from it".

### Applied to OnceCallback

The way you invoke a OnceCallback is `std::move(cb).run(args...)`. `std::move` turns `cb` into an rvalue; `run()` — via deducing this (a C++23 feature, with a dedicated article later) — detects that this is an rvalue invocation, executes the callback, and then marks `cb`'s state as "consumed". Accessing `cb` after that point is illegal. The design idea, plainly put, is to lean on the type system to enforce the "invalid after one call" semantics.

OnceCallback also `= delete`s both the copy constructor and copy assignment, keeping only the move operations. That way a OnceCallback object has exactly one owner at any moment — you can't copy it; the only way out is `std::move` to hand over ownership.

---

## Perfect forwarding and std::forward

The problem perfect forwarding wants to solve looks like this: you write a function template that takes parameters and passes them along to another function untouched. "Untouched" means preserving the arguments' value category (lvalue or rvalue) and const qualification — so the rvalue you received doesn't degrade into an lvalue somewhere along the way.

### Forwarding references and deduction rules

When a function template's parameter is written `T&&` with `T` a template parameter, that `T&&` is no ordinary rvalue reference — it is a forwarding reference (some people call it a universal reference). The compiler deduces `T` from the value category of the argument you pass in:

Pass an lvalue `x` (of type `int`), and `T` is deduced as `int&`, with `T&&` collapsing to `int&`; pass the rvalue `42` (of type `int`), and `T` is deduced as plain `int`, making `T&&` an `int&&`.

### What std::forward does

`std::forward<T>(arg)` decides, based on the type of the template parameter `T`, whether to return an lvalue reference or an rvalue reference:

```cpp
template<typename T>
void wrapper(T&& arg) {
    // std::forward preserves arg's original value category
    target(std::forward<T>(arg));
}

int x = 10;
wrapper(x);    // arg is an lvalue reference; forward returns an lvalue reference
wrapper(10);   // arg is an rvalue reference; forward returns an rvalue reference
```

When we first learned this material, we stepped straight into a pit: if you pass `arg` along directly without `std::forward`, `arg` is always an lvalue inside the function (named variables are all lvalues), and the rvalue-ness is simply lost. `std::forward` is what retrieves that information.

### Applied to OnceCallback

Perfect forwarding shows up all over OnceCallback. The `bind_once` function template relies on it to preserve the value category of the bound arguments — `std::forward<BoundArgs>(args)...` makes sure an rvalue you pass in stays an rvalue and an lvalue stays an lvalue. The deducing this implementation of `run()` likewise uses `std::forward<Self>(self)` to perfectly forward `self`'s value category to the internal `impl_run`.

---

## Variadic templates and parameter pack expansion

Variadic templates let you write a function or class that accepts any number of parameters of any type. OnceCallback's template signature `OnceCallback<R(Args...)>` itself carries a parameter pack.

### Basic syntax

```cpp
template<typename... Types>  // Types is a parameter pack
void print_all(Types... args) {
    // args... expands here
    // sizeof...(Types) returns the number of parameters
}
```

`Types...` is called a parameter pack; it can hold zero or more types. `args...` is a function parameter pack, expanded at the call site. `sizeof...(Types)` is a compile-time constant returning the number of elements in the pack.

### Where packs expand

A parameter pack can expand in quite a few places: function parameter lists, template parameter lists, initializer lists, and — as of C++20 — capture lists. The most crucial expansion site inside OnceCallback is the lambda capture list; that capability only arrived in C++20, and we have a dedicated article on it later.

### Applied to OnceCallback

The `Args...` in `OnceCallback<R(Args...)>` is a parameter pack, and it keeps showing up throughout the class's implementation — the constructor's parameter types, `run()`'s parameter types, and the signature of the internal `func_` all come from this pack. The `BoundArgs...` in `bind_once` is another parameter pack, expanded into the lambda's capture list and the call arguments of `std::invoke`.

---

## Smart pointer cheat sheet

Only two kinds of smart pointers show up inside OnceCallback; let's take a look at the role each one plays.

### std::unique_ptr: exclusive ownership

`unique_ptr` is the exclusive-ownership smart pointer: at any moment, only one `unique_ptr` points at the object. It cannot be copied, only moved, and you create one with `std::make_unique<T>(args...)`.

```cpp
auto p = std::make_unique<int>(42);
// auto p2 = p;             // compile error: not copyable
auto p3 = std::move(p);    // OK: the move transfers ownership
// p is nullptr from here on
```

Within OnceCallback, the point of `unique_ptr` isn't that we use it directly — it's that OnceCallback must support lambdas that capture move-only objects. If some lambda captures a `unique_ptr`, then the `std::move_only_function` holding that lambda (OnceCallback's internal storage) has to be move-only right along with it. `std::function` can't manage that, and it's one of the reasons we pick `std::move_only_function`.

### std::shared_ptr: shared ownership

`shared_ptr` manages the object's lifetime with reference counting. All the `shared_ptr`s pointing at the same object share one reference count; when the last `shared_ptr` is destroyed, the object goes with it.

```cpp
auto p1 = std::make_shared<int>(42);
auto p2 = p1;   // OK: a copy; the reference count goes up by one
// p1 and p2 both point at the same int
```

Inside OnceCallback, `shared_ptr` manages the cancellation token `CancelableToken`. The token has to be shared between the OnceCallback object and the external controller — the external controller calls `invalidate()` to invalidate the token, and OnceCallback checks the token's state through its own `shared_ptr` copy before executing the callback. The reference count guarantees one thing: as long as anyone still holds the token, the underlying `Flag` object won't be destroyed.

---

## std::atomic and memory_order

The cancellation token's internal implementation uses `std::atomic<bool>` together with `memory_order_acquire`/`memory_order_release`, so let's walk through both at once.

### Atomic operations

`std::atomic<T>` provides atomic access to a variable of type `T` — reads and writes can't be interrupted by other threads' operations. The basic operations are `load()` (read) and `store()` (write), and you can specify the memory order too.

```cpp
std::atomic<bool> flag{true};

// Thread A: write
flag.store(false, std::memory_order_release);

// Thread B: read
if (flag.load(std::memory_order_acquire)) {
    // flag is still true
}
```

### The acquire/release pair

`memory_order_release` and `memory_order_acquire` are a matched pair of memory orders. Put simply, a `release` store guarantees that all writes before the store become visible to other threads; an `acquire` load guarantees that all reads after the load will see the writes that happened before the release store. Pair the two up correctly and you've established happens-before.

Applied to OnceCallback's cancellation token: `invalidate()` uses a `release` store to set `valid` to `false`, and `is_valid()` uses an `acquire` load to read `valid` — which guarantees that whenever `is_valid()` returns `true`, all of the token's associated state is visible to the current thread.

---

## enum class

`enum class` is the scoped enumeration introduced in C++11, and it cures two old ailments of the old-style `enum`: name pollution and implicit conversion.

```cpp
// Old-style enum: the names pollute the global namespace, and it implicitly converts to int
enum Color { Red, Green, Blue };
int x = Red;  // OK, implicit conversion

// enum class: the names are confined to the enum's scope, no implicit conversion
enum class Status : uint8_t {
    kEmpty,    // never assigned anything
    kValid,    // holds a valid callable
    kConsumed  // already consumed by run()
};
Status s = Status::kValid;
// int y = s;  // compile error: no implicit conversion
```

OnceCallback uses `enum class Status` to distinguish the callback's three states. The underlying type is specified as `uint8_t` to save memory — the whole enum occupies a single byte.

---

## Lambda basics

Lambdas are, for all practical purposes, everywhere in OnceCallback — constructing callbacks, `bind_once`, and the internals of `then()` all rest on them. Let's quickly review the basic syntax here.

```cpp
auto add = [](int a, int b) { return a + b; };
// add's type is a unique closure class generated by the compiler

int x = 10;
// capture by value: copies x
auto f1 = [x]() { return x; };
// capture by reference: refers to x (watch the lifetime)
auto f2 = [&x]() { return x; };
// init capture (C++14): makes move captures possible
auto f3 = [p = std::make_unique<int>(42)]() { return *p; };
```

There's a pit here we didn't clock at the time: the closure class a lambda generates has a `const` `operator()` by default, so you can't modify by-value captured variables inside the lambda unless you add the `mutable` keyword. In OnceCallback's `bind_once` and `then()` implementations, the lambda must be declared `mutable`, because internally it calls `std::move(self).run()` and thereby mutates `self`'s state. We'll unpack that detail in the advanced lambda features article.

Generic lambdas (since C++14) allow `auto` parameters:

```cpp
auto generic = [](auto x, auto y) { return x + y; };
// the compiler generates a templated version of operator()
```

The lambda inside `bind_once` takes runtime arguments with `(auto&&... call_args)`; here `auto&&` is a forwarding reference (because `auto` behaves like a template parameter).

---

## Type traits

Type traits are the tools for querying and manipulating type information at compile time. A few key traits show up in OnceCallback; let's run through them quickly.

```cpp
#include <type_traits>

// std::decay_t<T>: strips references and const/volatile qualifiers from T, turns arrays into pointers, functions into function pointers
using T1 = std::decay_t<const int&>;       // T1 = int
using T2 = std::decay_t<OnceCallback&&>;   // T2 = OnceCallback (reference stripped)

// std::is_same_v<A, B>: whether A and B are the same type
static_assert(std::is_same_v<int, int>);           // passes
static_assert(!std::is_same_v<int, double>);       // passes

// std::is_lvalue_reference_v<T>: whether T is an lvalue reference type
static_assert(std::is_lvalue_reference_v<int&>);      // passes
static_assert(!std::is_lvalue_reference_v<int>);      // passes
static_assert(!std::is_lvalue_reference_v<int&&>);    // passes

// std::is_void_v<T>: whether T is void
static_assert(std::is_void_v<void>);            // passes
static_assert(!std::is_void_v<int>);            // passes
```

Inside OnceCallback, `std::decay_t` and `std::is_same_v` together build the `not_the_same_t` concept — it checks "whether the template argument, after decay, is the same type as `OnceCallback` itself", blocking the template constructor from hijacking calls meant for the move constructor. `std::is_lvalue_reference_v` appears in the deducing this implementation of `run()`, detecting whether the caller passed an lvalue and firing a `static_assert` error if so. And `std::is_void_v` shows up in `impl_run()` and `then()`, separating the compile-time branches for void and non-void return types.

---

## if constexpr

`if constexpr` is the compile-time conditional branch introduced in C++17. It differs from an ordinary `if` in that the condition must be a compile-time constant expression, and the branch not taken isn't compiled at all — it doesn't even get syntax-checked. This feature is exceptionally handy when handling void return types.

```cpp
template<typename R>
R do_something() {
    if constexpr (std::is_void_v<R>) {
        // void return: perform the action, return no value
        perform_action();
        return;  // void return
    } else {
        // non-void return: perform the action, return the result
        return perform_action();
    }
}
```

Without `if constexpr` — with a plain `if` — both branches have to compile. At that point the `return result` in the void branch fails outright, because void isn't a type you can assign. `if constexpr` guarantees that the void case only generates the `return;` code and the non-void case only generates the `return result;` code, and the two sides never disturb each other.

Inside OnceCallback, `if constexpr (std::is_void_v<ReturnType>)` shows up in two places: the callback-execution logic of `impl_run()`, and the chaining logic of `then()`. Both spots face the same problem — a void return type can't be assigned and returned the conventional way.

---

## decltype(auto)

`decltype(auto)` is the return type deduction mechanism introduced in C++14. The difference from `auto` lies in reference handling: `auto` drops references and top-level const, while `decltype(auto)` keeps them.

```cpp
int x = 10;
int& ref = x;

auto f1() { return ref; }           // returns int (reference dropped)
decltype(auto) f2() { return ref; } // returns int& (reference preserved)
```

Inside OnceCallback, the lambdas in `bind_once` and `then()` use `-> decltype(auto)` as their trailing return type. The point of writing it that way is to perfectly forward the callable's return value — if the invoked function returns `int&&`, `decltype(auto)` returns `int&&` too, and not one bit of value category information is lost.

---

## The [[nodiscard]] attribute

`[[nodiscard]]` is the attribute standardized in C++17; it tells the compiler "this function's return value should not be ignored". If a caller writes `cb.is_cancelled();` and never uses the return value, the compiler fires a warning.

```cpp
[[nodiscard]] bool is_cancelled() const noexcept;
[[nodiscard]] bool maybe_valid() const noexcept;
[[nodiscard]] bool is_null() const noexcept;
```

All three of OnceCallback's query methods wear `[[nodiscard]]`. The reason is plain: you call these methods precisely to make decisions on their return values, and a call that ignores the return value is most likely a slip of the hand — say, `if (!cb.is_cancelled())` degenerating into `cb.is_cancelled();`. The `explicit` in `explicit operator bool()` plays a similar role, blocking unexpected behavior caused by implicit conversion to `bool`.

---

## Ref-qualified member functions

C++11 lets you attach a reference qualifier (ref-qualifier) to a non-static member function, marking `&` or `&&` after the function's parameter list. `&` means it can only be invoked through an lvalue; `&&` means only through an rvalue.

```cpp
class Widget {
public:
    void process() & {
        // invocable only through an lvalue: Widget w; w.process();
    }
    void process() && {
        // invocable only through an rvalue: Widget().process(); or std::move(w).process();
    }
};
```

Inside OnceCallback, the `then()` method is declared `auto then(Next&& next) &&` — the trailing `&&` means `then()` can only be invoked through an rvalue (`std::move(cb).then(next)`, or `.then(next)` on a temporary). This is another way of expressing consumption semantics, and unlike `run()`, which uses deducing this, `then()` has no need to distinguish lvalue from rvalue callers with different error messages, so going straight to a ref-qualifier is simply cleaner.

---

And with that, we've run through the C++ fundamentals the OnceCallback series will use. For every one of them we covered the three things: what it is, how to use it, and where OnceCallback needs it. If any of these features still feels unfamiliar, go back and chew through the relevant volume's chapters systematically — the articles that follow won't re-explain this basic syntax.

Next we move into the deep end. The first stop is "function types and template partial specialization" — the key to understanding the strange-looking notation `OnceCallback<R(Args...)>`, and the entry point where we raise the entire template skeleton.

## References

- [cppreference: move semantics and rvalue references](https://en.cppreference.com/w/cpp/language/reference)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
- [cppreference: variadic templates](https://en.cppreference.com/w/cpp/language/parameter_pack)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: Type traits](https://en.cppreference.com/w/cpp/header/type_traits)
