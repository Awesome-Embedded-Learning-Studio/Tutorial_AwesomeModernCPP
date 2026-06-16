---
chapter: 6
cpp_standard:
- 11
- 14
- 17
description: Deduction rules for decltype, decltype(auto), and trailing return types
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 6: auto 推导深入'
reading_time_minutes: 10
related:
- 类模板参数推导
tags:
- host
- cpp-modern
- intermediate
title: decltype and Return Type Deduction
translation:
  source: documents/vol2-modern-features/ch06-auto-decltype/02-decltype.md
  source_hash: 8eabc358f5aebe524e7447c7590dace8787384dd0d84dc4cbdaf4f9304dab827
  translated_at: '2026-06-16T04:40:46.550319+00:00'
  engine: anthropic
  token_count: 1901
---
# decltype and Return Type Deduction

In the previous chapter, we covered the deduction rules of `auto` in detail—specifically how it discards references and top-level const by default. However, sometimes we need to preserve the type of an expression "exactly as is," including references and const qualifiers. This is where `decltype` comes into play.

The biggest difference between `auto` and `decltype` is this: `auto` deduces the type of a "new variable" based on an initializer (discarding references and const), whereas `decltype` "queries" the type of an existing expression (returning it exactly as is). While this distinction seems simple, it has many subtle implications in practice.

> In a nutshell: **decltype queries the exact type of an expression (preserving references and const), while decltype(auto) combines the conciseness of auto with the precision of decltype.**

------

## decltype Deduction Rules

### decltype(variable) vs decltype((variable))

The rules of `decltype` seem simple, but there is a very common pitfall: whether or not to use parentheses.

For a variable name without parentheses, `decltype` returns the type as declared:

```cpp
int x = 0;
decltype(x) y = x;  // y is of type int
```

But for a variable name with parentheses—`decltype((variable))`—it returns the type of that variable as an expression (an lvalue expression). The result is always an lvalue reference:

```cpp
int x = 0;
decltype((x)) y = x; // y is of type int&
```

The root of this difference lies in the C++ type system: `(x)` is not just a name; it is an expression. Since `(x)` evaluates to an lvalue, `decltype((x))` returns `int&`. Without parentheses, `x` is just a variable name, so `decltype(x)` directly looks up its declared type.

This "double parentheses" rule is the most famous trap in `decltype` and a classic interview question. I stumbled over this when I was learning—I never expected that adding a pair of parentheses would change the type from `int` to `int&`.

### decltype Deduction for Function Calls

When the operand of `decltype` is a function call expression, it returns the exact type of the function's return value:

```cpp
int& foo();
decltype(foo()) x = foo(); // x is of type int&
```

This stands in stark contrast to `auto`. For the same return value of `foo()`, `auto` would discard the reference and deduce `int`, while `decltype` preserves the reference and deduces `int&`.

### decltype Deduction for Expressions

For general expressions, `decltype` determines the type based on the expression's value category. If the expression is an lvalue, the result is a reference; if it is an rvalue, the result is a non-reference type:

```cpp
int x = 0;
decltype(x + 0) n = x + 0; // x + 0 is a prvalue (rvalue), n is int
decltype((x + 0)) m = x + 0; // (x + 0) is still an rvalue, m is int (not int&)

int* p = &x;
decltype(*p) q = x; // *p is an lvalue, q is int&
```

------

## decltype(auto): Precisely Preserving Reference Semantics

C++14 introduced `decltype(auto)`, which combines the conciseness of `auto` (no need to explicitly specify the type) with the precision of `decltype` (preserving references and const). During deduction, the compiler uses `decltype`'s rules to deduce the `auto` placeholder.

### Basic Usage

```cpp
int x = 0;
int& foo() { return x; }

decltype(auto) a = foo(); // a is int&
decltype(auto) b = (x);   // b is int& because (x) is an lvalue expression
decltype(auto) c = x;     // c is int
```

Note the parentheses in `b = (x)`. Because `decltype` returns a reference for parenthesized expressions, `decltype(auto)` deduces `int&`. If you don't want a reference, don't add parentheses:

```cpp
decltype(auto) c = x; // c is int
```

### Application in Function Return Types

`decltype(auto)` is particularly useful in function return types, especially when you want to perfectly forward the reference semantics of the return value:

```cpp
std::vector<int> vec{1, 2, 3};
decltype(auto) getElement(std::vector<int>& v, size_t index) {
    return v[index]; // Returns int&
}

getElement(vec, 0) = 10; // Modifies vec[0]
```

If you used `auto` instead of `decltype(auto)`, the return type of `getElement` would become `int` (a copy), and you wouldn't be able to modify the container contents via `getElement`.

### ⚠️ The Danger of Dangling References

The precision of `decltype(auto)` is a double-edged sword. It can deduce a reference type, leading to returning a reference to a local variable:

```cpp
decltype(auto) dangerous() {
    int x = 42;
    return (x); // DANGER! Returns int& to a local variable
}
```

The parentheses in `return (x)` cause `decltype` to treat `x` as an lvalue expression, deducing `int&`. After the function returns, `x` is destroyed, leaving the reference dangling. This is a very subtle bug; compilers usually issue a warning, but not all compilers can detect it in every situation.

My advice: when using `decltype(auto)` in a function return type, carefully inspect the `return` statement. If you return a reference to a local variable (whether intentionally or accidentally), it results in undefined behavior. If you are just returning a value, `auto` is safer.

------

## Trailing Return Types

### Motivation in C++11

In C++11, if a function's return type depended on its parameter types, you had to use a trailing return type. The most common scenario is returning the result of an operation on two parameters:

```cpp
template <typename T, typename U>
auto add(T t, U u) -> decltype(t + u) {
    return t + u;
}
```

Why can't we put the return type at the beginning? Because at the position of the function signature, the parameters `t` and `u` haven't been declared yet, so the compiler doesn't know their types. The trailing return type postpones the declaration of the return type until after the parameter list, allowing parameters to be used in the return type.

### Simplification in C++14

C++14 allows using `auto` directly as a return type, with the compiler deducing it from the `return` statement. In most cases, trailing return types are no longer needed:

```cpp
template <typename T, typename U>
auto add(T t, U u) {
    return t + u;
}
```

However, if you need to precisely preserve reference semantics (for example, if `t + u` might return a reference), you still need `decltype(auto)` or the C++11 trailing return type syntax.

### Lambda Return Types in C++11

In C++11, if a lambda's return type couldn't be deduced automatically, you needed to explicitly specify a trailing return type:

```cpp
auto lambda = [](int x) -> int { return x * 2; };
```

Since C++14, lambda return types can almost always be deduced automatically, removing the need for explicit specification.

------

## Using decltype in Templates

### Perfectly Forwarding Return Values

The most common use of `decltype` in templates is implementing perfect forwarding of return values—allowing a wrapper function to return the exact same type (including references) as the wrapped function:

```cpp
template <typename F, typename... Args>
decltype(auto) wrapper(F&& func, Args&&... args) {
    return std::forward<F>(func)(std::forward<Args>(args)...);
}
```

This `wrapper` function precisely forwards the result of calling `func`. If `func` returns `T&`, `wrapper` returns `T&`; if `func` returns `T`, `wrapper` returns `T` (since C++14, `decltype(auto)` supports deducing reference types).

### decltype in Type Traits

`decltype` is very useful when writing type traits. Combined with `decltype`, you can obtain the type of an expression without evaluating it:

```cpp
template <typename T>
auto has_begin_test(T t) -> decltype(t.begin(), std::true_type{});

auto has_begin_test(...) -> std::false_type;

template <typename T>
struct has_begin : decltype(has_begin_test(std::declval<T>())) {};
```

The trick here is SFINAE (Substitution Failure Is Not An Error): if `T` has a `begin` method, the return type of the first `has_begin_test` overload is successfully deduced; otherwise, deduction fails, and the compiler selects the second overload. `decltype` is used here to "probe" the validity of the expression without actually evaluating it.

### The Purpose of std::declval

`std::declval` is a utility function that can only be used in an unevaluated context. It returns an rvalue reference of the specified type without requiring the type to have a default constructor. This allows you to construct "hypothetical" objects in contexts like `decltype`, `noexcept`, `sizeof`, and `static_assert` to probe type information:

```cpp
template <typename T>
auto get_type() -> decltype(std::declval<T>().foo()) {
    // ...
}
```

⚠️ Note: `std::declval` can only be used in unevaluated contexts (such as `decltype`, `noexcept`, `sizeof`, and `static_assert`). If you call it in runtime code, it will trigger a compilation error because it has a declaration but no definition.

------

## Other Practical Techniques with decltype

### Obtaining Member Types

`decltype` can be used with `std::void_t` to obtain member types of containers or classes without needing to know the container's specific type:

```cpp
template <typename T>
using value_type_t = typename T::value_type;

std::vector<int> vec;
value_type_t<decltype(vec)> x = 0; // x is int
```

The benefit of this approach is that when the type of `vec` changes from `std::vector<int>` to `std::vector<double>`, all type aliases obtained via `decltype` update automatically.

### Using in constexpr

`decltype` from C++11 can be used in `constexpr` contexts because it is a pure compile-time operation:

```cpp
constexpr int x = 10;
constexpr decltype(x) y = x; // y is int
```

### Working with range-based for

Sometimes you need to know the exact type of an element in a range-based for loop. While `auto` is usually sufficient, `decltype` can come in handy in certain metaprogramming scenarios:

```cpp
std::vector<int> vec{1, 2, 3};
for (decltype(auto) elem : vec) {
    // elem is int&
}
```

------

## Summary

The core value of `decltype` lies in "precisely preserving the type of an expression," without discarding references and const. Its deduction rules can be summarized in three points: for unparenthesized variable names, it returns the declared type; for parenthesized variable names or lvalue expressions, it returns an lvalue reference; and for rvalue expressions, it returns a non-reference type.

`decltype(auto)` is a convenience tool introduced in C++14 that allows function return type deduction to preserve reference semantics, but be wary of the dangling reference trap with `decltype(auto)`. Trailing return types were the only way to handle parameter-dependent return types in C++11, but since C++14, they have been largely replaced by `auto` and `decltype(auto)` in most scenarios.

In templates and metaprogramming, `decltype` combined with `std::declval` is a foundational tool for building type traits and SFINAE constraints. Understanding these concepts will give you much greater confidence when reading and writing generic code.

## References

- [cppreference: decltype specifier](https://en.cppreference.com/w/cpp/language/decltype)
- [Effective Modern C++ - Scott Meyers, Item 3](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [decltype and std::declval - cppreference](https://en.cppreference.com/w/cpp/utility/declval)
