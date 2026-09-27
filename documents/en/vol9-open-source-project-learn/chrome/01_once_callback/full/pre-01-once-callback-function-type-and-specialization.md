---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 'A close look at what the function type int(int,int) actually is, and at the template partial specialization trick behind OnceCallback<R(Args...)>: how the compiler pulls a function signature apart by pattern matching'
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'OnceCallback prerequisite cheat sheet: a review of C++11/14/17 core features'
reading_time_minutes: 8
related:
- 'OnceCallback prerequisite (V): std::move_only_function (C++23)'
- 'OnceCallback hands-on (II): building the core skeleton'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 'OnceCallback prerequisite (I): function types and template partial specialization'
translation:
  source: documents/vol9-open-source-project-learn/chrome/01_once_callback/full/pre-01-once-callback-function-type-and-specialization.md
  source_hash: 81e406447246cbeaa52166fe6c1002f2b025c8647d6d57d9b403ab5ad684de84
  translated_at: '2026-09-26T00:38:33+00:00'
  engine: anthropic
  token_count: 5000
---
# OnceCallback prerequisite (I): function types and template partial specialization

The first time we ran into the `OnceCallback<int(int, int)>` spelling in the Chromium source, we stared at it for a good while. `int(int, int)` looks like the wreckage of a function declaration, yet there it sits in a template parameter slot. What on earth is this thing? And how does the compiler read "returns int, takes two ints" back out of `int(int, int)`?

We didn't figure it out at the time. It later turned out that this spelling is the shared foundation under `std::function`, `std::move_only_function`, and indeed our entire `OnceCallback`. In this piece we take that foundation apart properly—first we set the long-overlooked concept of a "function type" upright, then we watch the primary-template-plus-partial-specialization pattern matching pull a signature apart. We will hand-roll a minimal `FuncTraits` along the way to make it actually run, and close with why the standard library collectively picked the signature form instead of the more obvious spelling.

## Function types: a C++ type that's easy to miss

Let's start from the plainest question possible: does `int(int, int)` count as a type in C++?

Yes. It even has a name—the function type—and it denotes "a function that takes two ints and returns an int". One thing worth flagging here: a function type sits at a lower level than a function pointer. It is not the same thing as a pointer like `int(*)(int, int)` or a reference like `int(&)(int, int)`. As we will see later, that "lower level" is exactly what lets partial specialization grab hold of it.

One `static_assert` settles it:

```cpp
#include <type_traits>

static_assert(std::is_function_v<int(int, int)>);           // passes: it is a function type
static_assert(!std::is_pointer_v<int(int, int)>);           // passes: not a pointer
static_assert(std::is_pointer_v<int(*)(int, int)>);         // passes: this is a function pointer
```

Function types show up in real code more often than we tend to notice. Write down a function declaration off the cuff:

```cpp
int add(int a, int b);
```

The type of `add` is `int(int, int)`. You can treat it as a kind of signature: it spells out completely what the function takes in and what it spits out, while saying nothing about where the function itself lives.

There is also an implicit conversion between function types and function pointers: in most expressions, a function name automatically decays into a pointer to itself. This works exactly like array names decaying into pointers. In `int arr[5]`, `arr` becomes `int*` in most contexts; in `int add(int, int)`, `add` becomes `int(*)(int, int)`.

But the moment it is passed in as a template parameter, the function type stops decaying—the compiler takes the type in exactly as it is. That is precisely the precondition for pulling it apart with partial specialization later on.

## Primary template plus partial specialization: the recipe for taking a function type apart

Next, let's look at how `OnceCallback`'s template declaration is written. It takes two steps: first throw out a primary template that accepts a single type parameter, then carve out a separate partial specialization for the case where "that type parameter happens to be a function type".

### Step 1: the primary template declaration

```cpp
template<typename FuncSignature>
class OnceCallback;  // Primary template: declaration only, no definition
```

The primary template deliberately provides no implementation. That is not us forgetting to write it—it is intentional. If someone slips and writes `OnceCallback<int>`—passing in a plain int instead of a function signature—instantiation fails outright with a "definition not found" error. Call it a compile-time safety net.

### Step 2: the partial specialization

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // All the real code lives here
};
```

This version's template parameter list is `<typename ReturnType, typename... FuncArgs>`, but the key part is what follows the class name: `OnceCallback<ReturnType(FuncArgs...)>`. That is the partial specialization's pattern-matching condition, and it says exactly one thing: whenever `FuncSignature` can be shaped into the form `ReturnType(FuncArgs...)`, use this version.

### How the compiler makes the match

When you write `OnceCallback<int(int, int)>`, the compiler does a few things.

First it sees that you want to instantiate `OnceCallback` with the template argument `int(int, int)`. It then matches against the primary template, binding `FuncSignature` to the whole of `int(int, int)`. Next it turns back and checks whether any partial specialization applies. The partial specialization requires `FuncSignature` to match the pattern `ReturnType(FuncArgs...)`; `int(int, int)` splits apart cleanly into `ReturnType = int` and `FuncArgs = {int, int}`, the match succeeds, and the partial specialization gets selected.

You can treat the whole process as pattern matching at the type level. By analogy: the regex `(\w+)\((\w+(?:,\s*\w+)*)\)` can dig the return value and the parameter list out of the string `int(int, int)`. Template partial specialization does the same job—except that it operates on types, not characters.

### The exact same trick std::function uses

Go read your standard library's implementation of `std::function`, and you will find it uses the very same setup:

```cpp
// A simplified implementation of std::function
template<typename> class function; // primary template

template<typename R, typename... Args>
class function<R(Args...)> {        // partial specialization
    // ...
};
```

`std::move_only_function` (C++23) does the same. The primary-template-plus-function-type-partial-specialization combo has shown up in the standard library at least three times now—it is a design proven over and over. When we write our own `OnceCallback`, there is no reason to reinvent the wheel.

## Hands-on: rolling our own FuncTraits

Watching without practicing is a fast way to forget. Let's write a minimal function-signature-decomposition tool ourselves and hammer the understanding we just built solidly into place. The goal is this: hand it a function type `R(Args...)`, and it hands back both the return type `R` and the parameter pack `Args...`.

```cpp
#include <type_traits>

// Primary template: no definition for non-function types
template<typename T>
struct FuncTraits;

// Partial specialization: decomposes the function type R(Args...)
template<typename R, typename... Args>
struct FuncTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;

    static constexpr std::size_t kArity = sizeof...(Args);
};

// Verification
static_assert(std::is_same_v<FuncTraits<int(double, char)>::ReturnType, int>);
static_assert(std::is_same_v<FuncTraits<void()>::ReturnType, void>);
static_assert(FuncTraits<int(int, int, int)>::kArity == 3);
```

`FuncTraits` follows exactly the same partial-specialization playbook as `OnceCallback`. There is a single difference: `FuncTraits` stores the decomposed types as `using` aliases and `static constexpr` constants for the outside world to consume, while `OnceCallback` uses those types directly inside the partial specialization class to define its data members and methods.

Let's compile and run this example. All the `static_assert`s passing (no compile errors) means the partial specialization decomposed the function type correctly. Feel free to throw a few more complex types at it as well:

```cpp
// Verification with more complex types
static_assert(std::is_same_v<
    FuncTraits<std::string(const std::string&, int)>::ReturnType,
    std::string>);
static_assert(std::is_same_v<
    FuncTraits<void(int&&)>::ArgsTuple,
    std::tuple<int&&>>);
```

---

## Why not write it as OnceCallback<R, Args...>

You might wonder: since all we want is a return type plus a parameter list, why not spell it out directly as `OnceCallback<R, Args...>`, the more obvious style? Like this:

```cpp
template<typename R, typename... Args>
class OnceCallback {
    // ...
};

// Usage: OnceCallback<int, int, int> cb([](int a, int b) { return a + b; });
```

This spelling runs perfectly well technically. But the user experience is a notch worse. Compare the two usages:

```cpp
// Signature form: one template parameter, reads like a function signature
OnceCallback<int(int, int)> cb1([](int a, int b) { return a + b; });

// List form: return type and parameters written separately
OnceCallback<int, int, int> cb2([](int a, int b) { return a + b; });
```

The first reads naturally. `int(int, int)` is a complete function signature—you take it in at a glance. The second forces a mental split: the first `int` is the return type, and only the trailing `int, int` are the parameters—cognitive overhead conjured out of thin air. The standard library picked the signature form too: `std::function<int(int, int)>`, not `std::function<int, int, int>`.

The signature form has one more subtle advantage: it lines up better with the C++ type system. `int(int, int)` is a type that genuinely exists; "a return type plus a pile of parameter types" is not a type at all—just several types placed side by side. Passing a function type as a template parameter operates on the type system itself, not a patch glued on top of syntactic sugar.

Still, the signature form has one sore spot: the compiler cannot deduce a complete signature from a callable object on its own. That is why the first template parameter of `bind_once`, `Signature`, has to be written by hand. We will save that trade-off for the `bind_once` implementation piece.

## References

- [cppreference: function types](https://en.cppreference.com/w/cpp/language/function)
- [cppreference: template partial specialization](https://en.cppreference.com/w/cpp/language/template_specialization)
- [cppreference: std::is_function](https://en.cppreference.com/w/cpp/types/is_function)
