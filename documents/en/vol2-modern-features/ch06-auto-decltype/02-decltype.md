---
chapter: 6
cpp_standard:
- 11
- 14
- 17
description: The deduction rules of decltype, decltype(auto), and trailing return
  types
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 6: Deep Dive into auto Deduction: More Than Just Laziness'
reading_time_minutes: 10
related:
- 'Class Template Argument Deduction (CTAD)'
tags:
- host
- cpp-modern
- intermediate
title: decltype and Return Type Deduction
translation:
  source: documents/vol2-modern-features/ch06-auto-decltype/02-decltype.md
  source_hash: da2d916093aa3d475a9f31ad8c2a8a95b519d125f02fa1fd4c42ccdaf53abb90
  translated_at: '2026-09-25T15:48:36+00:00'
  engine: anthropic
  token_count: 4900
---
# decltype and Return Type Deduction: Keeping an Expression's Type Exactly As Is

In the previous chapter we covered `auto`'s deduction rules in detail—it discards references and top-level const by default. But sometimes what we need is to preserve an expression's type "exactly as is," references and const included. That is `decltype`'s territory.

The biggest difference between `decltype` and `auto` is this: `auto` deduces the type of a "new variable" from an initializer expression (dropping references and const), while `decltype` "queries" the type of an existing expression (returning it exactly as is). The distinction looks simple, but it has plenty of subtleties in practice.

> One-sentence summary: **decltype queries an expression's exact type (preserving references and const), while decltype(auto) combines auto's brevity with decltype's precision.**

------

## Deduction Rules of decltype

### decltype(variable) vs decltype((variable))

The rules of `decltype` look simple, but there is one spot that is extremely easy to trip over: parentheses or no parentheses.

For an unparenthesized variable name, `decltype` returns the type the variable was declared with:

```cpp
int x = 42;
decltype(x) a = 100;      // int

const int& cr = x;
decltype(cr) b = x;        // const int&
```

But for a parenthesized variable name—`decltype((x))`—it returns the type of `x` as an expression (an lvalue expression), and the result is always an lvalue reference:

```cpp
int x = 42;
decltype((x)) c = x;       // int& (not int!)
```

The root of this difference lies in C++'s type system: `(x)` is not just a name—it is an expression, and since `x` evaluated as an expression yields an lvalue, `decltype` returns `int&`. Without the parentheses, `x` is just a variable name, and `decltype` looks up its declared type directly.

This "double parentheses" rule is `decltype`'s most famous trap and a classic interview question. We crashed right here when we were first learning—at the time it never occurred to us that adding one pair of parentheses would turn the type from `int` into `int&`.

Let's sum up these two rules together with `decltype`'s most classic use (trailing return types) in one diagram:

![The two decltype rules compared, plus trailing return types](./02-decltype-rules.drawio)

### decltype Deduction for Function Calls

When `decltype`'s operand is a function call expression, it returns the exact type of the function's return value:

```cpp
int& get_ref() {
    static int x = 42;
    return x;
}

int get_val() {
    return 42;
}

decltype(get_ref()) a = get_ref();  // int&
decltype(get_val()) b = get_val();  // int
```

This contrasts sharply with `auto`. Given the same return value of `get_ref()`, `auto` drops the reference and gets `int`, while `decltype` keeps the reference and gets `int&`.

### decltype Deduction for Expressions

For general expressions, `decltype` decides the type from the expression's value category. If the expression is an lvalue, the result is a reference; if the expression is an rvalue, the result is a non-reference:

```cpp
int x = 42;

decltype(x + 1) a = 0;    // int (x + 1 is an rvalue)
decltype(x = 10) b = x;   // int& (assignment expressions return an lvalue reference)
decltype(++x) c = x;      // int& (prefix ++ returns an lvalue reference)
decltype(x++) d = 0;      // int (postfix ++ returns an rvalue)
```

------

## decltype(auto): Precisely Preserving Reference Semantics

C++14 introduced `decltype(auto)`, which combines `auto`'s brevity (no need to write the type out explicitly) with `decltype`'s precision (preserving references and const). During deduction, the compiler applies `decltype`'s rules to deduce the `auto` part.

### Basic Usage

```cpp
int x = 42;

auto a = (x);            // int (auto drops the reference)
decltype(auto) b = (x);  // int& (decltype keeps the reference)
```

Note the parentheses in `(x)`—because `decltype` returns a reference for a parenthesized expression, `decltype(auto)` deduces `int&`. If you don't want a reference, just leave the parentheses out:

```cpp
decltype(auto) c = x;    // int (no parentheses, decltype(x) is int)
```

### Application in Function Return Types

`decltype(auto)` is especially useful in function return types, particularly when you want to perfectly forward the reference semantics of a return value:

```cpp
class Container {
public:
    decltype(auto) operator[](std::size_t index) {
        return data_[index];  // data_[index] returns int&; decltype(auto) preserves it
    }

    decltype(auto) operator[](std::size_t index) const {
        return data_[index];  // the const version returns const int&
    }

private:
    std::vector<int> data_;
};
```

If you used `auto` instead of `decltype(auto)`, `operator[]`'s return type would become `int` (a copy), and you could no longer modify the container's contents through `container[0] = 42`.

### The Danger of Dangling References

`decltype(auto)`'s precision is a double-edged sword. It can deduce a reference type, leading you to return a reference to a local variable:

```cpp
decltype(auto) get_value() {
    int x = 42;
    return (x);   // returns int&, but x is destroyed when the function ends — dangling reference!
}

decltype(auto) safe_get_value() {
    int x = 42;
    return x;     // returns int (no parentheses): a value copy, safe
}
```

The parentheses in `return (x);` make `decltype` treat `(x)` as an lvalue expression and deduce `int&`. Once the function returns, `x` is destroyed and the reference dangles. This is a very sneaky bug; compilers usually warn about it, but not every compiler can detect it in every situation.

Our advice: when using `decltype(auto)` as a function return type, scrutinize the `return` statements—if what you return is a reference to a local variable (whether on purpose or by accident), you get undefined behavior. If you are just returning a value, `auto` is the safer choice.

------

## Trailing Return Types

### Motivation in C++11

In C++11, if a function's return type depends on its parameter types, you must use a trailing return type. The most common scenario is returning the result of an operation on two parameters:

```cpp
template<typename T, typename U>
auto add(T t, U u) -> decltype(t + u) {
    return t + u;
}
```

Why can't the return type go up front? Because at the position of the function signature, the parameters `t` and `u` haven't been declared yet, so the compiler doesn't know their types. A trailing return type defers the declaration of the return type until after the parameter list, so the parameters become usable inside the return type.

### Simplification in C++14

C++14 allows `auto` as the return type directly, with the compiler deducing it from the `return` statement. In most cases the trailing return type is no longer needed:

```cpp
// C++14 simplified version
template<typename T, typename U>
auto add(T t, U u) {
    return t + u;
}
```

But if you need to preserve reference semantics precisely (say, in cases where `t + u` might return a reference), you still need `decltype` or `decltype(auto)`.

### Lambda Return Types in C++11

In C++11, when a lambda's return type cannot be deduced automatically, you have to specify the trailing return type explicitly:

```cpp
auto get_size = [](const std::vector<int>& v) -> std::size_t {
    return v.size();
};
```

From C++14 on, a lambda's return type can almost always be deduced automatically, so explicit specification is no longer needed.

------

## decltype in Templates

### Perfectly Forwarding Return Values

The most common use of `decltype` in templates is implementing perfect forwarding of return values—letting a wrapper function return exactly the same type as the wrapped function, references included:

```cpp
template<typename Callable, typename... Args>
decltype(auto) perfect_forward(Callable&& f, Args&&... args) {
    return std::forward<Callable>(f)(std::forward<Args>(args)...);
}
```

This `perfect_forward` function forwards the result of invoking `f` exactly. If `f` returns `int&`, `perfect_forward` returns `int&` too; if `f` returns `void`, `perfect_forward` returns `void` as well (since C++14, `decltype(auto)` supports deducing `void`).

### decltype in Type Traits

`decltype` is extremely useful when writing type traits. Combined with `std::declval`, you can obtain an expression's type without evaluating it:

```cpp
#include <type_traits>
#include <vector>

// Check whether type T has a push_back method
template<typename T, typename Arg>
struct has_push_back {
private:
    template<typename U>
    static auto test(int) -> decltype(
        std::declval<U>().push_back(std::declval<Arg>()),
        std::true_type{}
    );

    template<typename>
    static auto test(...) -> std::false_type;

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

static_assert(has_push_back<std::vector<int>, int>::value);
static_assert(!has_push_back<int, int>::value);
```

The trick here is SFINAE (Substitution Failure Is Not An Error): if `U` has a `push_back` method, the first `test` overload's return type deduces successfully; otherwise deduction fails, and the compiler picks the second `test` overload. Here `decltype` serves to "probe" whether an expression is valid without actually evaluating it.

### The Purpose of std::declval

`std::declval<T>()` is a utility function that may only be used in unevaluated contexts. It returns an rvalue reference `T&&`, without requiring `T` to have a default constructor. That way you can conjure up a "hypothetical" object inside unevaluated contexts such as `decltype`, `sizeof`, and `noexcept` to probe type information:

```cpp
#include <utility>

// Without needing to know Container's default constructor,
// we can still get its iterator type
template<typename Container>
using iterator_t = decltype(std::declval<Container>().begin());

// Get the result type of adding two values
template<typename T, typename U>
using add_result_t = decltype(std::declval<T>() + std::declval<U>());
```

Note: `std::declval` can only be used in unevaluated contexts (such as `decltype`, `sizeof`, `noexcept`, and `typeid`). If you call it in runtime code, you will trigger a compilation error, because it is declared but never defined.

------

## Other Practical decltype Techniques

### Obtaining Member Types

`decltype` can be combined with `auto` to obtain the member types of a container or a class without needing to know the container's concrete type:

```cpp
extern std::vector<int> global_data;
using value_t = decltype(global_data)::value_type;  // int
using iter_t  = decltype(global_data)::iterator;    // std::vector<int>::iterator
```

The benefit of this style: when `global_data`'s type changes from `std::vector<int>` to `std::deque<int>`, every type alias obtained through `decltype` updates automatically.

### Using decltype in constexpr

C++11's `decltype` could already be used in `constexpr` contexts, because it is a purely compile-time operation:

```cpp
constexpr int x = 42;
constexpr decltype(x) y = x + 1;  // constexpr int
```

### Working with range-based for

Sometimes you need to know the exact type of an element in a range-based for loop. Usually `auto` is enough, but `decltype` can come in handy in certain metaprogramming scenarios:

```cpp
template<typename Range>
void process_range(Range&& r) {
    for (auto&& elem : r) {
        // what is the type of elem?
        using elem_t = decltype(elem);
        process_element(std::forward<elem_t>(elem));
    }
}
```

------

## References

- [cppreference: decltype specifier](https://en.cppreference.com/w/cpp/language/decltype)
- [Effective Modern C++ - Scott Meyers, Item 3](https://www.oreilly.com/library/view/effective-modern-c/9781491908419/)
- [decltype and std::declval - cppreference](https://en.cppreference.com/w/cpp/utility/declval)
