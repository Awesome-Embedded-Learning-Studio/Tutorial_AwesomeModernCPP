---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: "A close look at what the function type int(int,int) actually is, and the template partial specialization trick behind OnceCallback<R(Args...)>: how the compiler pulls a function signature apart by pattern matching"
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'OnceCallback prerequisites: a C++11/14/17 refresher'
reading_time_minutes: 8
related:
- 'OnceCallback prerequisites (V): std::move_only_function'
- 'OnceCallback hands-on (II): the core skeleton'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'OnceCallback Prerequisites (I): Function Types and Template Partial Specialization'
---
# OnceCallback Prerequisites (I): Function Types and Template Partial Specialization

The first time we ran into `OnceCallback<int(int, int)>` in the Chromium source, we stared at it for a while. `int(int, int)` looks like the wreckage of a function declaration, yet there it sits in a template parameter slot. What is this thing? And how does the compiler read "returns int, takes two ints" back out of `int(int, int)`?

We didn't figure it out at the time. It turns out this spelling is the shared foundation under `std::function`, `std::move_only_function`, and our entire `OnceCallback`. This piece works that foundation over. First we set the overlooked idea of a "function type" upright, then we walk through how primary template plus partial specialization pulls a signature apart by pattern matching. We will hand-roll a tiny `FuncTraits` along the way to make it run, and close on why the standard library collectively picked the signature form instead of the more obvious spelling.

## Function types: a C++ type that's easy to miss

Let's start with the plainest question: is `int(int, int)` a type in C++?

Yes. It has a name, a function type, and it means "a function taking two ints and returning an int." One thing worth flagging here: a function type sits lower in the stack than a function pointer. It is not the same thing as `int(*)(int, int)` (a pointer) or `int(&)(int, int)` (a reference). That "lower" position is exactly what lets partial specialization grab it, as we will see.

A `static_assert` settles it:

```cpp
#include <type_traits>

static_assert(std::is_function_v<int(int, int)>);           // passes: it's a function type
static_assert(!std::is_pointer_v<int(int, int)>);           // passes: not a pointer
static_assert(std::is_pointer_v<int(*)(int, int)>);         // passes: this one is a function pointer
```

Function types show up more often than you'd think. Take an ordinary declaration:

```cpp
int add(int a, int b);
```

The type of `add` is `int(int, int)`. You can treat it as a signature: it says exactly what the function takes and what it returns, without saying where the function itself lives.

There's an implicit conversion between function types and function pointers. In most expressions, a function name decays into a pointer to itself, the same way an array name decays into a pointer. The `arr` in `int arr[5]` becomes `int*` in most contexts; the `add` in `int add(int, int)` becomes `int(*)(int, int)`.

But once it goes in as a template argument, the function type stops decaying. The compiler takes it as is. That is the prerequisite for taking it apart with partial specialization.

## Primary template plus partial specialization: the recipe for breaking down a function type

Next, let's look at how `OnceCallback`'s template declaration is written. It's a two-step move: first throw out a primary template that takes a single type parameter, then open a separate partial specialization for the case where "that type parameter happens to be a function type."

### Step 1: the primary template declaration

```cpp
template<typename FuncSignature>
class OnceCallback;  // primary template: declaration only, no definition
```

The primary template deliberately has no implementation. This isn't an oversight. It's a compile-time safety net: if someone slips and writes `OnceCallback<int>`, passing a plain int instead of a function signature, instantiation fails on the missing definition right there.

### Step 2: the partial specialization

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // all the real code lives here
};
```

The template parameter list on this version is `<typename ReturnType, typename... FuncArgs>`, but the part that matters is the `OnceCallback<ReturnType(FuncArgs...)>` after the class name. That's the pattern-matching condition for the partial specialization, and it says one thing: when `FuncSignature` can be assembled into the shape `ReturnType(FuncArgs...)`, use this version.

### How the compiler pairs things up

When you write `OnceCallback<int(int, int)>`, the compiler does a few things.

It sees you instantiating `OnceCallback` with `int(int, int)` as the template argument. It goes to the primary template and binds `FuncSignature` to `int(int, int)` as a whole. Then it turns around and checks whether a partial specialization can match. The specialization needs `FuncSignature` to fit the pattern `ReturnType(FuncArgs...)`. `int(int, int)` breaks apart cleanly: `ReturnType = int`, `FuncArgs = {int, int}`. The match lands, and the specialization gets picked.

You can think of the whole process as pattern matching at the type level. Compare it to a regex like `(\w+)\((\w+(?:,\s*\w+)*)\)`, which digs the return value and parameter list out of the string `int(int, int)`. Partial specialization does the same job; it just operates on types instead of characters.

### The exact same trick as `std::function`

Go pull up a standard library implementation of `std::function` and you'll find the same setup:

```cpp
// simplified std::function
template<typename> class function; // primary template

template<typename R, typename... Args>
class function<R(Args...)> {        // partial specialization
    // ...
};
```

`std::move_only_function` (C++23) is the same. The "primary template plus function-type partial specialization" pair shows up at least three times in the standard library. It's a design that's been validated over and over. When we write our own `OnceCallback`, there's no reason to start from scratch.

## Hands-on: rolling a FuncTraits

Reading alone won't stick. Let's write the smallest function-signature extraction tool ourselves and hammer the idea down. The goal: hand it a function type `R(Args...)` and have it hand back the return type `R` and the parameter pack `Args...`.

```cpp
#include <type_traits>

// primary template: no definition for non-function types
template<typename T>
struct FuncTraits;

// partial specialization: peel apart R(Args...)
template<typename R, typename... Args>
struct FuncTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;

    static constexpr std::size_t kArity = sizeof...(Args);
};

// checks
static_assert(std::is_same_v<FuncTraits<int(double, char)>::ReturnType, int>);
static_assert(std::is_same_v<FuncTraits<void()>::ReturnType, void>);
static_assert(FuncTraits<int(int, int, int)>::kArity == 3);
```

`FuncTraits` walks the same partial-specialization path as `OnceCallback`. There's one difference: `FuncTraits` stores the extracted types as `using` aliases and a `static constexpr` constant for outside callers, while `OnceCallback` takes those types directly and uses them inside the specialization class to define its data members and methods.

Let's compile and run the example. If the `static_assert`s all pass (no compilation errors), the specialization split the function type correctly. You can throw a few harder types at it too:

```cpp
// harder checks
static_assert(std::is_same_v<
    FuncTraits<std::string(const std::string&, int)>::ReturnType,
    std::string>);
static_assert(std::is_same_v<
    FuncTraits<void(int&&)>::ArgsTuple,
    std::tuple<int&&>>);
```

---

## Why not write it as `OnceCallback<R, Args...>`?

You might be wondering: if all we want is the return type plus the parameter list, why not just spell it `OnceCallback<R, Args...>` and be done? Something like:

```cpp
template<typename R, typename... Args>
class OnceCallback {
    // ...
};

// usage: OnceCallback<int, int, int> cb([](int a, int b) { return a + b; });
```

This compiles fine. The ergonomics are worse, though. Let's set the two calls side by side:

```cpp
// signature form: one template argument, reads like a function signature
OnceCallback<int(int, int)> cb1([](int a, int b) { return a + b; });

// parameter-list form: return type and parameters written separately
OnceCallback<int, int, int> cb2([](int a, int b) { return a + b; });
```

The first reads naturally. `int(int, int)` is a complete function signature, clear at a glance. The second makes you do a mental split: the first `int` is the return type, the next two `int, int` are the parameters. That's a tax on the reader for no payoff. The standard library made the same call: `std::function<int(int, int)>`, not `std::function<int, int, int>`.

The signature form has a subtler benefit too. It lines up better with the C++ type system. `int(int, int)` is an actual type; "a return type plus a pile of parameter types" is not a type, it's several types sitting next to each other. Taking a function type as the template argument means we're working with the type system itself, not patching syntactic sugar on top.

There's one corner where the signature form bites, though: the compiler can't deduce the full signature from a callable object on its own. That's why `bind_once`'s first template parameter, `Signature`, has to be written out by hand. We'll save that trade-off for the `bind_once` implementation piece.

## References

- [cppreference: function type](https://en.cppreference.com/w/cpp/language/function)
- [cppreference: template partial specialization](https://en.cppreference.com/w/cpp/language/template_specialization)
- [cppreference: std::is_function](https://en.cppreference.com/w/cpp/types/is_function)
