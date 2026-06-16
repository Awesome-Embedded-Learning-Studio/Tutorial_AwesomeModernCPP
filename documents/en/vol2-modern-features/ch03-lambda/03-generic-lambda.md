---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: From auto parameters to template parameters, lambda's generic programming
  capabilities
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 3: Lambda 捕获机制深入'
reading_time_minutes: 13
related:
- 函数式编程模式
tags:
- host
- cpp-modern
- intermediate
- lambda
- 泛型
title: Generic Lambda and Template Lambda
translation:
  source: documents/vol2-modern-features/ch03-lambda/03-generic-lambda.md
  source_hash: cd92a40277ccf7816e5227685cacafdcb516fc98d0f439ca446cedb8b4833d7e
  translated_at: '2026-06-16T03:57:09.990614+00:00'
  engine: anthropic
  token_count: 3026
---
# Generic Lambdas and Template Lambdas

## Introduction

In the previous two chapters, the lambda parameter types we used were all concrete—`int`, `float`, `std::string`, and so on. However, in real-world projects, much lambda logic is type-agnostic: a sorting comparator only requires the type to support `operator<`, and an accumulator only requires support for `operator+`. If we write a lambda for each type, we revert to the C++98 functor path—repetitive and redundant. C++14 gave lambdas generic capabilities (`auto` parameters), and C++20 went further by allowing lambdas to have explicit template parameter lists. In this chapter, we will thoroughly clarify the underlying mechanisms, usage, and boundaries of generic lambdas.

> **Learning Objectives**
>
> - Understand the underlying implementation of C++14 generic lambdas—template call operators
> - Master the usage of `decltype(auto)` within lambdas
> - Learn C++20 template lambda syntax and concept constraints
> - Understand the implementation methods and trade-offs of recursive lambdas

---

## C++14 Generic Lambdas — `auto` Parameters

C++14 allows the use of `auto` for lambda parameter types. This kind of lambda is called a generic lambda. To the caller, it behaves like a function template—arguments of different types each instantiate a version of `operator()`:

```cpp
auto generic_add = [](auto a, auto b) {
    return a + b;
};

int i = generic_add(1, 2);        // Instantiates operator()(int, int)
double d = generic_add(1.0, 2.0); // Instantiates operator()(double, double)
```

When the same lambda object is invoked with arguments of different types, the compiler generates an instance of `operator()` for each combination of argument types. This behavior is identical to function template instantiation.

### Under the Hood: Template Call Operator

The compiler translates a generic lambda into a closure type roughly like this:

```cpp
class ClosureType {
public:
    template<typename T, typename U>
    auto operator()(T a, U b) const {
        return a + b;
    }
};
```

Each `auto` parameter corresponds to a template parameter of the closure type's `operator()`. Two `auto` parameters mean `operator()` is a member function template with two template parameters. This understanding is crucial—it implies that generic lambdas enjoy all the power of templates, including SFINAE (Substitution Failure Is Not An Error), explicit instantiation, and so on.

### Multiple `auto` Parameters

It is worth noting that each `auto` is an independent template parameter, and their deduction rules do not affect each other:

```cpp
auto print_pair = [](auto first, auto second) {
    std::cout << first << ", " << second << std::endl;
};

print_pair(1, 2.5); // T is int, U is double
```

If you want two parameters to be the same type, in C++14 you need to use some tricks (like using `std::same_as` or a generic lambda with a single parameter returning another lambda), whereas in C++20 you can use template parameters directly (we will cover this shortly).

---

## `if constexpr` in Lambdas

C++17's `if constexpr` allows selecting different code paths at compile time based on type information. In generic lambdas, this is particularly useful—you can choose different implementations based on the type traits of the arguments:

```cpp
auto describe = [](auto const& value) {
    if constexpr (std::is_integral_v<decltype(value)>) {
        std::cout << "Integer: " << value << std::endl;
    } else if constexpr (std::is_floating_point_v<decltype(value)>) {
        std::cout << "Float: " << value << std::endl;
    } else {
        std::cout << "Unknown type" << std::endl;
    }
};
```

The key to `if constexpr` is that branches not satisfying the condition are discarded at compile time and do not participate in final code generation. This means you can use operations specific to a type (like `.push_back()` for vectors) in different branches; as long as that branch doesn't meet the condition for the current instantiation, the compiler won't check its semantic validity. Note that discarded branches still undergo basic syntax checking and cannot contain unresolvable template-dependent names.

A more practical scenario involves handling different iterator types—random access iterators can use subscript access, while forward iterators can only use `++`. `if constexpr` allows you to handle both cases elegantly within a single lambda.

---

## C++20 Template Lambdas — Explicit Template Parameters

C++14 generic lambdas using `auto` parameters are convenient, but they have limitations: you cannot know the name of the deduced type, you cannot impose constraints on template parameters, and you cannot reference the type inside the lambda to declare other variables. C++20 adds explicit template parameter lists to lambdas, solving these problems in one stroke:

```cpp
auto add_same = []<typename T>(T a, T b) {
    return a + b;
};

add_same(1, 2);    // OK, T is int
add_same(1.0, 2.0); // OK, T is double
// add_same(1, 2.0); // Error: T cannot be both int and double
```

Here, the `template<typename T>` syntax is identical to normal templates. Both parameters are of type `T`, so the two arguments must be of the same type when called—something C++14's `auto` cannot do.

### Using Template Parameter Names Inside Lambdas

Template parameter names can be used freely inside the lambda body, which is much more flexible than `auto`:

```cpp
auto get_element = []<typename T>(std::vector<T> const& vec) {
    // We can use T directly
    T default_val{};
    return vec.empty() ? default_val : vec[0];
};
```

If you use C++14's `auto` parameter, you get `std::vector<U> const&`, but inside the lambda you don't know if the element type is `T`—you have to use `typename U::value_type` to deduce it. With C++20 template parameter `T`, everything is straightforward.

### Constraining with Concepts

C++20 Concepts and template lambdas are natural partners. You can use the `requires` clause to impose constraints on template parameters, making the lambda accept only types that satisfy specific concepts:

```cpp
auto numeric_add = []<std::integral T>(T a, T b) {
    return a + b;
};

numeric_add(10, 20); // OK
// numeric_add(10.5, 20.5); // Error: double does not satisfy std::integral
```

The benefit of Concepts constraints lies not only in compile-time type safety—error messages are much friendlier than traditional SFINAE. When you pass the wrong type, the compiler tells you directly "constraint not satisfied" and points out which specific concept failed, rather than outputting a massive template instantiation stack. You can compile with `-fconcepts-diagnostics-depth=2` and trigger an error to compare the error message quality of Concepts and SFINAE.

### Explicitly Specifying Template Arguments When Invoking Template Lambdas

Sometimes you don't want the compiler to deduce the template parameters and want to specify them explicitly. Template lambdas also support explicit template argument calls, though the syntax is a bit special:

```cpp
auto cast_to_int = []<typename T>(T val) -> int {
    return static_cast<int>(val);
};

double d = 3.14;
int i = cast_to_int.operator()<double>(d); // Explicitly specify T as double
```

The `.operator()<...>` syntax is indeed not very pretty, but in practice, you rarely need to call it explicitly—most of the time compiler deduction is sufficient. Scenarios requiring explicit specification are mainly when you want to force a specific conversion (like forcing a `float` to be treated as a `long`), or when the lambda uses `if constexpr` to select different branches based on template parameters.

---

## Recursive Lambdas

Lambdas are anonymous by nature—they have no name, so they cannot call themselves within their body. However, recursion is a common requirement in programming. We have several ways to work around this limitation.

### Method 1: Wrapping with `std::function`

The most intuitive way is to store the lambda in a `std::function`, then achieve self-invocation through the variable name:

```cpp
std::function<int(int)> fibonacci = [&](int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
};
```

**Note**: `std::function` invocation involves type erasure, and every recursive call requires an indirect call through a virtual function table. In performance-sensitive code, this overhead needs to be considered. Actual tests (see `bench_recursive.cpp`) show that at `-O2` optimization, the `std::function` version of recursive calls is about 70-150 times slower than a templated implementation (depending on recursion depth and compiler optimization capabilities).

### Method 2: Generic Lambda + `auto&&` Parameter (Y Combinator Idea)

A more efficient approach utilizes the characteristics of generic lambdas, passing a "self-reference" as an argument. This is a simplified version of the Y combinator concept:

```cpp
auto y_combinator = [](auto&& self) {
    return [&](auto&&... args) {
        return self(std::forward<decltype(args)>(args)...);
    };
};

// Usage
auto factorial = y_combinator([](auto&& self, int n) -> int {
    if (n <= 1) return 1;
    return n * self(self, n - 1);
});
```

The key to this version is that the first parameter `self` of the generic lambda receives a reference to the lambda object itself. Inside the lambda, recursion is implemented by calling `self(self, ...)`. Because `operator()` is a template function, the compiler can inline the entire call chain.

**Performance Comparison** (based on `bench_recursive.cpp` with g++ 15.2.1 -O2, 1,000,000 `fibonacci(10)` calls):

- `std::function` version: ~18,700 µs (type erasure overhead, hard to optimize)
- Y Combinator version: ~130-250 µs (templated, fully inlinable)
- Performance improvement: ~75-145x

In practice, if your recursion depth is shallow or the call frequency is low, the simplicity of `std::function` may be more important. But for performance-critical code, the Y combinator or directly passing the self-reference is more appropriate.

### Method 3: C++14 Generic Lambda Passing Self Directly

If you don't want to write a Y combinator helper class, there is a hack—using an `auto` parameter to receive the self-reference:

```cpp
auto recursive_lambda = [](auto&& self, int n) -> int {
    if (n <= 1) return 1;
    return n * self(self, n - 1);
};

// Call
recursive_lambda(recursive_lambda, 5);
```

The problem with this style is that the caller must manually pass the lambda itself into it—`recursive_lambda(recursive_lambda, 5)` instead of just `recursive_lambda(5)`. Although it looks a bit weird, it is acceptable for internal logic that doesn't need to be encapsulated into an API.

---

## General Examples

### Generic Comparator

```cpp
auto compare = [](auto const& a, auto const& b) {
    return a < b;
};

std::vector<int> v = {3, 1, 4, 1, 5};
std::sort(v.begin(), v.end(), compare);
```

### Generic Transformer

```cpp
auto transform = [](auto const& input) {
    using T = std::decay_t<decltype(input)>;
    // Perform some transformation logic
    return input;
};
```

### Polymorphic Container Operations

Generic lambdas combined with template functions allow for writing generic algorithms that do not depend on specific container types. The following example uses a generic lambda to print any container type, as long as the container's elements support `operator<<`:

```cpp
auto print_container = [](auto const& container) {
    for (auto const& elem : container) {
        std::cout << elem << " ";
    }
    std::cout << std::endl;
};

std::vector<int> v1 = {1, 2, 3};
std::list<double> l1 = {1.1, 2.2, 3.3};

print_container(v1); // Works for vector
print_container(l1); // Works for list
```

The flexibility of generic lambdas makes these "write once, use everywhere" generic operations very natural. You don't need to write an overload for each container type—`auto` parameters combined with range-based for loops handle all containers that support iteration with a single lambda.

---

## Summary

Generic lambdas evolve lambda expressions from "a fixed piece of code" into "a parameterized piece of code." Core takeaways:

- C++14 generic lambda `auto` parameters correspond to template parameters of the closure type's `operator()`
- `if constexpr` allows generic lambdas to select different code paths based on type information
- C++20 template lambdas use `template<>` syntax to provide explicit template parameters and Concepts constraints
- Recursive lambdas can be implemented via `std::function` (simple but with overhead) or the Y combinator pattern (efficient but slightly more complex syntax)
- Generic lambdas are extremely useful in scenarios like generic comparators, transformers, and container operations

## Reference Resources

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)

## Verification Code

The performance comparisons and proof-of-concept code for this chapter are located in the `generic_lambda_demo` directory:

- `bench_recursive.cpp`: Performance benchmarks for different recursive lambda implementations
- `concepts_vs_sfinae.cpp`: Comparison of error message quality between Concepts and SFINAE

Compile and run (requires CMake):

```bash
cd generic_lambda_demo
mkdir build && cd build
cmake ..
make
./bench_recursive
./concepts_vs_sfinae
```
