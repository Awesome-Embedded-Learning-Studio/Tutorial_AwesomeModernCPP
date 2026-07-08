---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: "A fast review of every C++ feature the OnceCallback series leans on: move semantics, perfect forwarding, variadic templates, smart pointers, atomics, lambdas, type traits, and more. So the design articles after this one can stay focused on the design."
difficulty: intermediate
order: 0
platform: host
prerequisites:
- Volume 1: C++ basics
reading_time_minutes: 14
related:
- OnceCallback prerequisites (I): function types and template partial specialization
- OnceCallback prerequisites (III): advanced lambda features
tags:
- host
- cpp-modern
- intermediate
- 基础
- 入门
title: 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
---
# OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features

Let's get the positioning straight first: this is not a from-zero tutorial. If move semantics and smart pointers are still completely foreign to you, go work through Volume 2 and come back. This piece assumes you've seen all of this once and just got rusty. The OnceCallback series leans on a stack of C++11/14/17 features, move semantics, perfect forwarding, variadic templates, smart pointers, atomics, lambdas, type traits, and we're going to run through them all in one go. For each one we cover three things: what it is, how to use it, and where OnceCallback actually needs it. Read this and the design articles that follow won't stall you on a syntax detail.

## Move semantics and std::move

The entire foundation of OnceCallback sits here. It's a move-only type, and the core design leans on move semantics end to end, so let's quickly walk the core ideas.

### Rvalue references and the move constructor

C++11 brought in rvalue references `T&&`, which can bind to temporary objects (rvalues). The move constructor `T(T&& other)` means "steal the resources from `other` instead of copying them." After the steal, `other` is in a "valid but unspecified" state, usually emptied.

```cpp
// Minimal move-semantics example
class Buffer {
    int* data_;
    std::size_t size_;
public:
    // ordinary constructor
    Buffer(std::size_t n) : data_(new int[n]), size_(n) {}
    // move constructor: steal other's resources
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;   // empty the source
        other.size_ = 0;
    }
    ~Buffer() { delete[] data_; }
};

Buffer a(100);          // a owns 100 ints
Buffer b = std::move(a); // b stole a's resources, a is now empty
```

### What std::move actually does

`std::move` doesn't move anything. It's a `static_cast<T&&>` that unconditionally turns its argument into an rvalue reference. The ones that actually do the moving are the move constructor or move assignment operator. `std::move` just raises its hand and tells the compiler "I'm fine with this being treated as an rvalue, go ahead and steal from it."

### Landing it on OnceCallback

The OnceCallback invocation form is `std::move(cb).run(args...)`. `std::move` turns `cb` into an rvalue, and `run()` (through deducing this, a C++23 feature with its own article later) sees it's being called on an rvalue, runs the callback, then marks `cb`'s state as "consumed." Touching `cb` after that is illegal. The whole design, in plain terms, borrows the type system to enforce "called once, then invalid."

OnceCallback also `= delete`s both the copy constructor and copy assignment, leaving only the move operations. That means a OnceCallback object has exactly one owner at any moment, you can't copy it, you can only `std::move` ownership away.

---

## Perfect forwarding and std::forward

The problem perfect forwarding is after is this: you write a function template that takes some arguments and passes them, unchanged, to another function. "Unchanged" here means keeping the value category (lvalue or rvalue) and the const qualification intact, so an rvalue doesn't quietly turn into an lvalue somewhere along the way.

### Forwarding references and deduction rules

When a function template parameter is written `T&&` and `T` is a template parameter, that `T&&` is not a regular rvalue reference, it's a forwarding reference (some call it a universal reference). The compiler deduces `T` from the value category of the argument you pass in:

Pass an lvalue `x` (of type `int`), and `T` deduces to `int&`, with `T&&` collapsing to `int&`. Pass an rvalue `42` (of type `int`), and `T` deduces to `int`, with `T&&` becoming `int&&`.

### What std::forward is doing

`std::forward<T>(arg)` decides, based on the template parameter `T`, whether to return an lvalue reference or an rvalue reference:

```cpp
template<typename T>
void wrapper(T&& arg) {
    // std::forward preserves arg's original value category
    target(std::forward<T>(arg));
}

int x = 10;
wrapper(x);    // arg is an lvalue reference, forward returns an lvalue reference
wrapper(10);   // arg is an rvalue reference, forward returns an rvalue reference
```

A trap I hit when learning this: skip `std::forward` and pass `arg` straight through, and `arg` is always an lvalue inside the function (named variables are lvalues), so the rvalue-ness is just gone. `std::forward` is what recovers it.

### Landing it on OnceCallback

Perfect forwarding keeps showing up in OnceCallback. The `bind_once` function template relies on it to keep the bound arguments' value category: `std::forward<BoundArgs>(args)...` makes sure an rvalue passed in stays an rvalue, and an lvalue stays an lvalue. The deducing this implementation of `run()` also uses `std::forward<Self>(self)` to perfectly forward `self`'s value category down into `impl_run`.

---

## Variadic templates and parameter pack expansion

Variadic templates let you write a function or class that takes any number of arguments of any type. OnceCallback's own template signature `OnceCallback<R(Args...)>` carries a parameter pack.

### Basic syntax

```cpp
template<typename... Types>  // Types is a parameter pack
void print_all(Types... args) {
    // args... expands here
    // sizeof...(Types) returns the number of arguments
}
```

`Types...` is a parameter pack and holds zero or more types; `args...` is a function parameter pack, expanded at the call site. `sizeof...(Types)` is a compile-time constant giving the number of elements in the pack.

### Where packs expand

A parameter pack can expand in several places: the function parameter list, the template parameter list, initializer lists, and (since C++20) capture lists. The most important expansion site inside OnceCallback is a lambda's capture list, a feature only added in C++20, and we have a dedicated article on it later.

### Landing it on OnceCallback

The `Args...` in `OnceCallback<R(Args...)>` is a parameter pack, and it shows up all over the class implementation: the constructor's parameter types, `run()`'s parameter types, the signature of the internal `func_`, all of it traces back to that pack. `BoundArgs...` inside `bind_once` is another pack, expanded into the lambda's capture list and into the call arguments of `std::invoke`.

---

## Smart pointer cheat sheet

OnceCallback only uses two smart pointer flavors internally, so let's look at the role each one plays.

### std::unique_ptr: exclusive ownership

`unique_ptr` is the exclusive smart pointer, at most one `unique_ptr` points at the object at a time. It can't be copied, only moved, and you make one with `std::make_unique<T>(args...)`.

```cpp
auto p = std::make_unique<int>(42);
// auto p2 = p;             // compile error: not copyable
auto p3 = std::move(p);    // OK: move transfers ownership
// from here on p is nullptr
```

The point of `unique_ptr` inside OnceCallback isn't that we use it directly. It's that OnceCallback has to support lambdas that capture move-only objects. If a lambda captures a `unique_ptr`, then the `std::move_only_function` holding that lambda (OnceCallback's internal storage) is forced to be move-only too. `std::function` can't do that, which is one of the reasons we go with `std::move_only_function`.

### std::shared_ptr: shared ownership

`shared_ptr` manages an object's lifetime through reference counting. Every `shared_ptr` pointing at the same object shares one reference count, and when the last `shared_ptr` dies, the object dies with it.

```cpp
auto p1 = std::make_shared<int>(42);
auto p2 = p1;   // OK: copy, refcount +1
// p1 and p2 both point at the same int
```

Inside OnceCallback, `shared_ptr` manages the cancellation token `CancelableToken`. The token has to be shared between the OnceCallback object and an external controller: the controller calls `invalidate()` to void the token, and OnceCallback checks the token's state through its own `shared_ptr` copy before running the callback. The reference count guarantees one thing: as long as somebody still holds the token, the underlying `Flag` object won't be destroyed.

---

## std::atomic and memory_order

The cancellation token's internal implementation uses `std::atomic<bool>` together with `memory_order_acquire/release`, so let's cover both at once.

### Atomic operations

`std::atomic<T>` gives atomic access to a variable of type `T`, reads and writes can't be torn apart by another thread's operations. The basic ops are `load()` (read) and `store()` (write), and you can specify the memory order.

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

`memory_order_release` and `memory_order_acquire` are a matched pair of memory orders. Loosely, a `release` store guarantees that every write before the store is visible to other threads; an `acquire` load guarantees that every read after the load sees the writes that happened before the release store. Get the pair right and you have happens-before.

On OnceCallback's cancellation token, `invalidate()` does a `release` store to set `valid` to `false`, and `is_valid()` does an `acquire` load on `valid`. That guarantees: as long as `is_valid()` returns `true`, every state tied to the token is visible to the current thread.

---

## enum class

`enum class` is the scoped enumeration C++11 added, and it fixes two old habits of plain `enum`: name pollution and implicit conversion.

```cpp
// Old enum: pollutes the surrounding namespace, implicitly converts to int
enum Color { Red, Green, Blue };
int x = Red;  // OK, implicit conversion

// enum class: names scoped inside the enum, no implicit conversion
enum class Status : uint8_t {
    kEmpty,    // never assigned a value
    kValid,    // holds a valid callable
    kConsumed  // already consumed by run()
};
Status s = Status::kValid;
// int y = s;  // compile error: no implicit conversion
```

OnceCallback uses `enum class Status` to split the callback into three states. The underlying type is pinned to `uint8_t` to save memory, the whole enum takes one byte.

---

## Lambda basics

Lambdas are everywhere in OnceCallback, building callbacks, `bind_once`, the internals of `then()`, all of it. Quick refresher on the basic syntax.

```cpp
auto add = [](int a, int b) { return a + b; };
// add's type is a unique closure class generated by the compiler

int x = 10;
// by-value capture: copies x
auto f1 = [x]() { return x; };
// by-reference capture: refers to x (mind the lifetime)
auto f2 = [&x]() { return x; };
// init capture (C++14): capture by move
auto f3 = [p = std::make_unique<int>(42)]() { return *p; };
```

One thing tripped me up here for a while: the closure class's `operator()` is `const` by default, so you can't mutate a by-value-captured variable inside the lambda unless you add `mutable`. In OnceCallback's `bind_once` and `then()` implementations, the lambda has to be `mutable`, because internally it calls `std::move(self).run()` and that mutates `self`'s state. We'll unpack that in the advanced lambda article.

Generic lambdas (since C++14) let parameters use `auto`:

```cpp
auto generic = [](auto x, auto y) { return x + y; };
// the compiler generates a templated operator()
```

The lambda inside `bind_once` takes runtime args with `(auto&&... call_args)`, and here `auto&&` is a forwarding reference (since `auto` behaves like a template parameter).

---

## Type traits

Type traits are tools for querying and manipulating type information at compile time. OnceCallback uses a handful of the key ones, quick run-through:

```cpp
#include <type_traits>

// std::decay_t<T>: strips references and const/volatile from T, decays arrays to pointers, functions to function pointers
using T1 = std::decay_t<const int&>;       // T1 = int
using T2 = std::decay_t<OnceCallback&&>;   // T2 = OnceCallback (reference removed)

// std::is_same_v<A, B>: are A and B the same type
static_assert(std::is_same_v<int, int>);           // passes
static_assert(!std::is_same_v<int, double>);       // passes

// std::is_lvalue_reference_v<T>: is T an lvalue reference type
static_assert(std::is_lvalue_reference_v<int&>);      // passes
static_assert(!std::is_lvalue_reference_v<int>);      // passes
static_assert(!std::is_lvalue_reference_v<int&&>);    // passes

// std::is_void_v<T>: is T void
static_assert(std::is_void_v<void>);            // passes
static_assert(!std::is_void_v<int>);            // passes
```

Inside OnceCallback, `std::decay_t` and `std::is_same_v` together build the `not_the_same_t` concept. It checks "is the template parameter, after decay, the same type as `OnceCallback` itself," and it's what stops the template constructor from hijacking move constructor calls. `std::is_lvalue_reference_v` shows up in `run()`'s deducing this implementation to detect whether the caller passed an lvalue, and if so it fires a `static_assert`. `std::is_void_v` shows up in `impl_run()` and `then()` to pick the right compile-time branch between void and non-void return types.

---

## if constexpr

`if constexpr` is the compile-time conditional branch C++17 added. The difference from a plain `if` is that the condition has to be a compile-time constant expression, and the branch not taken doesn't get compiled at all, not even syntax-checked. That's especially handy when you're dealing with void return types.

```cpp
template<typename R>
R do_something() {
    if constexpr (std::is_void_v<R>) {
        // void return: do the work, no return value
        perform_action();
        return;  // void return
    } else {
        // non-void return: do the work, return the result
        return perform_action();
    }
}
```

Without `if constexpr`, with a plain `if`, both branches get compiled. Now the `return result;` inside the void branch blows up immediately, void isn't a type you can return a value of. `if constexpr` makes sure the void case only generates `return;` and the non-void case only generates `return result;`, and the two stay out of each other's way.

In OnceCallback, `if constexpr (std::is_void_v<ReturnType>)` shows up in two places: the callback execution logic in `impl_run()`, and the chaining logic in `then()`. Both hit the same problem, void return types can't be assigned and returned the normal way.

---

## decltype(auto)

`decltype(auto)` is the return type deduction form C++14 added. Where it differs from `auto` is in reference handling: `auto` drops references and top-level const, `decltype(auto)` keeps them.

```cpp
int x = 10;
int& ref = x;

auto f1() { return ref; }           // returns int (reference dropped)
decltype(auto) f2() { return ref; } // returns int& (reference kept)
```

Inside OnceCallback, the lambdas in `bind_once` and `then()` use `-> decltype(auto)` as a trailing return type. The point is to perfectly forward the callable's return value: if the function being called returns `int&&`, `decltype(auto)` returns `int&&` too, no value category information lost.

---

## The [[nodiscard]] attribute

`[[nodiscard]]` is the attribute C++17 standardized, and it tells the compiler "the return value of this function shouldn't be ignored." If a caller writes `cb.is_cancelled();` and throws away the result, the compiler warns.

```cpp
[[nodiscard]] bool is_cancelled() const noexcept;
[[nodiscard]] bool maybe_valid() const noexcept;
[[nodiscard]] bool is_null() const noexcept;
```

All three of OnceCallback's query methods carry `[[nodiscard]]`. The reason is blunt: you call these to get a return value and branch on it, and a call that ignores the return value is almost always a typo, something like writing `cb.is_cancelled();` when you meant `if (!cb.is_cancelled())`. The `explicit` on `explicit operator bool()` plays a similar role, blocking the surprises that implicit conversion to `bool` can cause.

---

## Ref-qualified member functions

C++11 lets you put a reference qualifier (ref-qualifier) on a non-static member function, marking it `&` or `&&` after the parameter list. `&` means it can only be called on an lvalue, `&&` only on an rvalue.

```cpp
class Widget {
public:
    void process() & {
        // only callable on an lvalue: Widget w; w.process();
    }
    void process() && {
        // only callable on an rvalue: Widget().process(); or std::move(w).process();
    }
};
```

In OnceCallback, the `then()` method is declared `auto then(Next&& next) &&`. The trailing `&&` means `then()` can only be called on an rvalue (`std::move(cb).then(next)`, or `.then(next)` on a temporary). That's another way to express consume semantics, different from how `run()` uses deducing this: `then()` doesn't need to tell lvalue and rvalue callers apart with different error messages, so a plain ref-qualifier is the cleaner choice.

---

That's the lot, every C++ feature the OnceCallback series is going to lean on. For each one we covered what it is, how to use it, and where it shows up inside OnceCallback. If any of them still feels shaky, go back to the matching chapter in the earlier volumes and work through it properly, the articles after this won't re-explain the basic syntax.

Next we move into the deep dive. First stop is "function types and template partial specialization," the key to the odd-looking `OnceCallback<R(Args...)>` notation, and the entry point where we start putting the whole template skeleton together.

## References

- [cppreference: move semantics and rvalue references](https://en.cppreference.com/w/cpp/language/reference)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
- [cppreference: variadic templates](https://en.cppreference.com/w/cpp/language/parameter_pack)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)
- [cppreference: std::atomic](https://en.cppreference.com/w/cpp/atomic/atomic)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: Type traits](https://en.cppreference.com/w/cpp/header/type_traits)
