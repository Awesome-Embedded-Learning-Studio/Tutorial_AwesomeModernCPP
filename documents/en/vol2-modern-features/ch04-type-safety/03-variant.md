---
chapter: 4
cpp_standard:
- 17
description: Use `variant` instead of `union`, and combine with `visit` to implement
  type-safe polymorphism
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 4: enum class'
reading_time_minutes: 13
related:
- std::optional
- 错误处理的现代方式
tags:
- host
- cpp-modern
- intermediate
- variant
- 类型安全
title: 'std::variant: A Type-Safe Union'
translation:
  source: documents/vol2-modern-features/ch04-type-safety/03-variant.md
  source_hash: eda541ba28744575d65e36cea1cac2124eaaa9ea23cd3fbe643f6afef94a7a02
  translated_at: '2026-06-16T03:57:37.154900+00:00'
  engine: anthropic
  token_count: 2909
---
# std::variant: A Type-Safe Union

## Introduction

`std::variant` (introduced in C++17) is the modern successor to the C-style `union`. Its core purpose is to ensure type safety while maintaining the constraint that it "holds one of many types at any given moment." Unlike a raw `union`, `std::variant` knows exactly which type it currently holds, performs checks when you access it, and correctly manages the lifetime of the held object. In this chapter, we will start with the pain points of `union` and progressively clarify the mechanisms and usage of `std::variant`.

## Step 1 — The Fatal Flaws of `union`

Before discussing `std::variant`, let's look at why raw `union`s are unsafe.

```cpp
union Data {
    int i;
    float f;
    char str[20];
};

Data data;
data.i = 10;
// Oops! We forgot to track that we are now holding an int.
// If we read data.f here, the behavior is undefined.
```

The problem here is that the `union` itself **does not track** which member is currently active. The programmer must manually maintain a "tag" to keep track of the active member. If you forget to update the tag, or if the tag becomes inconsistent with the actual state, you trigger undefined behavior (UB).

Even worse, `union`s **do not support types with non-trivial constructors or destructors**. For example, `std::string` cannot be placed directly inside a `union`—you must manually use placement new to construct it and manually call the destructor to destroy it. This manual management is both tedious and error-prone.

```cpp
union SafeData {
    std::string str;
    int i;

    SafeData() {} // Which member is active? Neither is initialized!
    ~SafeData() {} // Who destroys the string?
};
```

Honestly, writing this kind of code feels like walking a tightrope—missing any single step leads to resource leaks or worse. The arrival of `std::variant` makes all of this manual management completely unnecessary.

## Step 2 — Basic Usage of `variant`

### Construction and Assignment

`std::variant` can hold a value of **exactly one** of the types in its template parameter list `Types...` at any given moment. Upon default construction, it constructs the first alternative type (unless you use the `std::monostate` placeholder):

```cpp
std::variant<int, double, std::string> v; // Holds int (0-initialized)
v = 3.14;   // Destroys int, constructs double
v = "hello"; // Destroys double, constructs std::string
```

Every time you assign a value, `std::variant` automatically destroys the old value and constructs the new one. You do not need to manage any lifetimes manually—this is all handled automatically by `std::variant`'s internal mechanisms.

### Accessing Values

There are three main ways to access values inside a `std::variant`:

```cpp
std::variant<int, float> v = 42;

// 1. Check type
if (std::holds_alternative<int>(v)) {
    // Safe to access
}

// 2. Get pointer (returns nullptr if type mismatch)
if (int* ptr = std::get_if<int>(&v)) {
    std::cout << *ptr << std::endl;
}

// 3. Direct access (throws std::bad_variant_access on mismatch)
try {
    int val = std::get<int>(v);
} catch (const std::bad_variant_access& e) {
    std::cout << "Bad access!" << std::endl;
}
```

Our recommended approach is: if you just need to check the type, use `std::holds_alternative`; if you need a pointer to the value (and want to avoid exceptions), use `std::get_if`; and if you are certain the type is correct and want an immediate error on mismatch, use `std::get`.

## Step 3 — `std::visit` and the Visitor Pattern

`std::visit` is the core access mechanism for `std::variant`. It accepts a callable object (a visitor) and one or more `variant` objects, dispatching the call based on the type currently held by the `variant`. This is safer than `if-else` chains because the compiler checks if you have handled all alternative types.

### Simple `visit` with Lambdas

```cpp
std::variant<int, float, std::string> v = 42;

std::visit([](auto&& arg) {
    std::cout << arg << std::endl;
}, v);
```

Here, `auto&& arg` is a forwarding reference. The compiler instantiates this lambda based on the type currently held by `v`. When you need to perform the same operation on all types, this syntax is very concise.

### Overload Sets: Handling Different Types

A more common scenario is that different types require different handling logic. In this case, we need an "overload set"—a callable object with a corresponding overload for each alternative type. There is a classic trick in C++17 to achieve this:

```cpp
template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
```

This `overloaded` template "inherits" the `operator()` from multiple lambdas, combining them into a single callable object with overloads for multiple types. Usage looks like this:

```cpp
std::variant<int, float, std::string> v = 3.14f;

std::visit(overloaded {
    [](int arg) { std::cout << "int: " << arg << std::endl; },
    [](float arg) { std::cout << "float: " << arg << std::endl; },
    [](const std::string& arg) { std::cout << "string: " << arg << std::endl; }
}, v);
```

The compiler checks if your `overloaded` set covers all alternative types of the `variant`. If you miss handling for a specific type, the compiler will error immediately—this is the embodiment of compile-time type safety. In C++20, you don't even need to write the `overloaded` helper—the standard library directly supports the `visit` pattern with multiple lambdas (though the formal support method is still evolving).

### `visit` with Return Values

A `std::visit` visitor can also return values. The return types of all lambdas must be compatible (convertible to a common type):

```cpp
std::variant<int, float> v = 42;

std::string result = std::visit([](auto&& arg) -> std::string {
    if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, int>) {
        return "int";
    } else {
        return "float";
    }
}, v);
```

## Step 4 — `variant` as a Substitute for Runtime Polymorphism

An important use of `std::variant` is replacing polymorphism implemented via virtual functions (known as "closed hierarchies" or "visit-based polymorphism"). Traditional virtual function polymorphism requires heap allocation, virtual table pointers (vtable), and reference semantics—whereas `std::variant` can store values directly on the stack without virtual function call overhead.

```cpp
// Traditional approach (Virtual functions)
struct Shape { virtual void draw() const = 0; virtual ~Shape() = default; };
struct Circle : Shape { void draw() const override { /* ... */ } };
struct Rectangle : Shape { void draw() const override { /* ... */ } };

// Variant approach
using Shape = std::variant<Circle, Rectangle>;
```

Usage comparison:

```cpp
// Virtual function
void drawShape(const Shape& s) { s.draw(); }

// Variant
void drawShape(const Shape& s) {
    std::visit([](const auto& shape) { shape.draw(); }, s);
}
```

The advantage of the `variant` approach lies in: value semantics (no `new`/`delete`), contiguous memory (stored directly in the `variant`, cache-friendly), and compile-time type checking (all branches of `std::visit` are determined at compile time). However, it comes with a cost: every time you add a new shape, you must modify the `Shape` `variant` definition—which is inflexible in some scenarios. If your type hierarchy is "open" (third parties can extend it), virtual functions are still the better choice.

## Step 5 — Exception Safety and `valueless_by_exception`

`std::variant` has a special state called `valueless_by_exception`. When a `variant` is switching types (e.g., during assignment or `emplace`), if the constructor of the new type throws an exception after the old value has already been destroyed, the `variant` enters this "valueless" state.

```cpp
struct Thrower {
    Thrower() { throw std::runtime_error("Oops"); }
};

std::variant<int, Thrower> v = 42;
try {
    v = Thrower{}; // int is destroyed, Thrower() throws
} catch (...) {}

// v is now valueless_by_exception
std::cout << v.index(); // Output: std::variant_npos
```

In this state, `std::get` will throw `std::bad_variant_access`, and `std::visit` will also throw an exception. Therefore, if your code might encounter this situation, it is best to check before accessing.

⚠️ **Note:** In practice, `valueless_by_exception` appears extremely rarely. It is only triggered in the specific scenario where "constructing a new value throws an exception." If the constructors of all your alternative types are `noexcept` (or you don't use exceptions), you don't need to worry about this state at all.

## Real-World Application — Message Type Systems

One of the best scenarios for `std::variant` is a message passing system. In event-driven architectures, messages in a queue may have multiple types, each with a different payload. `std::variant` + `std::visit` handles this pattern very elegantly:

```cpp
using Message = std::variant<
    struct Start { int id; },
    struct Stop { int id; },
    struct Data { std::string payload; }
>;

void handleMessage(const Message& msg) {
    std::visit(overloaded {
        [](const Start& m) { /* Handle start */ },
        [](const Stop& m) { /* Handle stop */ },
        [](const Data& m) { /* Handle data */ }
    }, msg);
}
```

The benefit of this code is: if you add a new message type (e.g., `Log`), the compiler will error directly at the `std::visit` call site—you must add a corresponding overload to the `overloaded` set. This ability—"the compiler helps you find all places that need modification when adding a type"—is one of the biggest advantages of `std::variant` compared to `union` or virtual functions.

## Real-World Application — Configuration Values and AST Nodes

### Configuration Values

Configuration systems often need to store values of different types: integers, floats, strings, and booleans. `std::variant` is naturally suited for this:

```cpp
using ConfigValue = std::variant<int, double, std::string, bool>;

ConfigValue timeout = 30;
ConfigValue host = "localhost";
```

### AST Nodes

In the frontend of a compiler or interpreter, Abstract Syntax Tree (AST) node types are also naturally suited for representation by `std::variant`:

```cpp
using Expr = std::variant<
    struct IntLiteral { int value; },
    struct FloatLiteral { double value; },
    struct BinaryOp {
        std::unique_ptr<Expr> left, right;
        char op;
    }
>;
```

⚠️ **Note:** Here we use `std::unique_ptr` instead of direct `Expr`, because `std::variant` cannot directly contain incomplete types. Recursive data structures must use pointers (or smart pointers) to break circular dependencies.

## Memory Layout and Performance Considerations

The size of a `std::variant` equals the "size of the largest alternative type" plus a small metadata field (used to record the index of the currently held type). This means that even if you currently only hold a `char`, the `variant` is at least as large as the largest type (e.g., `std::string`).

```cpp
static_assert(sizeof(std::variant<char, std::string>) == sizeof(std::string) + sizeof(size_t));
```

> Here is a quick supplement regarding `int` size. You can read about it at [cppreference](https://en.cppreference.com/cpp/language/types). Simply put, `int` is specified to be at least 16 bits (2 bytes), though it is 4 bytes on most modern platforms. Of course, don't just memorize this as a dogma.
> You can refer to the [example](https://godbolt.org/z/sbvEMW56G) provided by [YukunJ](https://github.com/YukunJ).

This size is completely acceptable for most applications. However, in memory-constrained embedded scenarios, you may need to evaluate whether it is worth using `std::variant` instead of a hand-written `union` + tag scheme. The type safety benefits of `std::variant` usually far outweigh the cost of a few bytes of memory overhead.

## Summary

`std::variant` is one of the most important type-safety tools in C++17. It solves the three core problems of raw `union`s: not knowing what type is currently held (solved by an internal tag), not managing object lifetimes (automatic constructor/destructor calls), and not supporting non-trivial types (no restrictions).

`std::visit` is the core access mechanism for `std::variant`. Combined with the `overloaded` idiom, it enables type-safe pattern matching. When your set of types is finite and known (message types, configuration values, AST nodes, etc.), `std::variant` is more efficient and safer than virtual functions. However, if the type set is open (third parties can extend it), virtual functions remain the more appropriate choice.

`valueless_by_exception` is a state worth knowing about but usually not something to worry about—it only appears in the extreme scenario where constructing a new value throws an exception. Knowing this state exists is enough; there is no need to be overly defensive about it in actual code.

The next topic we will discuss, `std::optional`, can be seen as a special case of `std::variant`—when your "type set" has only two possibilities ("has value" and "does not have value"), `std::optional` is the more concise choice.

## References

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit)
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access)
- [C++ Core Guidelines: C++ union](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c181-prefer-using-variant-over-union)
