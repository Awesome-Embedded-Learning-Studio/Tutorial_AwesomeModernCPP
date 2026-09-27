---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: From auto parameters to template parameters — the generic programming
  power of lambdas
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 3: Lambda Basics: The Elegant Expression of Anonymous Functions'
- 'Chapter 3: Deep Dive into Lambda Capture'
reading_time_minutes: 13
related:
- Functional Programming Patterns
tags:
- host
- cpp-modern
- intermediate
- lambda
- 泛型
title: Generic Lambdas and Template Lambdas
translation:
  source: documents/vol2-modern-features/ch03-lambda/03-generic-lambda.md
  source_hash: 442b6c038bab63924be4fb080eb9de71f70dd858f40c08269524e8fbf3875e9a
  translated_at: '2026-09-25T15:10:19+00:00'
  engine: anthropic
  token_count: 3400
---
# Generic Lambdas and Template Lambdas

In the previous two chapters, every lambda we wrote took concrete parameter types—`int`, `double`, `const std::string&`, and the like. In real projects, however, a lot of lambda logic is type-neutral: a sorting comparator only requires the type to support `<`, and an accumulator only requires it to support `+`. If we wrote a separate lambda for every type, we would be right back on the old C++98 functor road—repetitive and redundant. C++14 gave lambdas generic capabilities (`auto` parameters), and C++20 went further and gave lambdas explicit template parameter lists outright. In this chapter we thoroughly work out the underlying mechanisms, usage, and boundaries of generic lambdas.

---

## C++14 Generic Lambdas — auto Parameters

C++14 allows lambda parameter types to use `auto`. Such a lambda is called a generic lambda. To the caller, it behaves just like a function template—arguments of different types each get their own instantiation of `operator()`:

```cpp
// Generic lambda: accepts any type that supports operator+
auto add = [](auto a, auto b) {
    return a + b;
};

int xi = add(3, 4);                         // int
double xd = add(3.14, 2.72);                // double
std::string xs = add(std::string("hi "), std::string("there"));
```

When the same lambda object is called with arguments of different types, the compiler generates one instance of `operator()` for each combination of argument types. This behavior is exactly the same as function template instantiation.

Drawn out, the correspondence between one lambda object and three `operator()` instances looks like this:

![Generic lambda instantiation: one lambda corresponds to multiple operator() instances](./03-generic-lambda-instant.drawio)

### Under the Hood: The Template Call Operator

Behind the scenes, the compiler translates a generic lambda into a closure type roughly like this:

```cpp
// What you wrote
auto add = [](auto a, auto b) { return a + b; };

// What the compiler generates (simplified)
struct ClosureType {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

Each `auto` parameter corresponds to one template parameter of the closure type's `operator()`. Two `auto`s mean `operator()` is a member function template with two template parameters. This insight matters—it means a generic lambda enjoys every capability templates have, including SFINAE, explicit instantiation, and so on.

### auto Parameters of Different Types

Note that each `auto` is an independent template parameter; their deduction rules do not affect one another:

```cpp
auto multiply = [](auto a, auto b) {
    return a * b;
};

multiply(3, 4.5);    // int * double -> double
multiply(2.0f, 3);   // float * int -> float
```

If you want both parameters to be of the same type, in C++14 you need a few tricks (for example `std::common_type_t`), while in C++20 you can express it directly with template parameters (we will get there shortly).

---

## if constexpr Inside Lambdas

C++17's `if constexpr` can select different code paths at compile time based on type information. Inside a generic lambda it is especially useful—you can choose different implementations based on the parameter's type characteristics:

```cpp
#include <type_traits>
#include <iostream>
#include <vector>
#include <string>

auto process = [](auto& container) {
    using T = std::decay_t<decltype(container)>;

    if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "Processing string: " << container << "\n";
    } else if constexpr (std::is_same_v<T, std::vector<int>>) {
        std::cout << "Processing int vector, size: " << container.size() << "\n";
    } else {
        std::cout << "Processing unknown type\n";
    }
};

void demo_if_constexpr() {
    std::string s = "hello";
    std::vector<int> v = {1, 2, 3};
    double d = 3.14;

    process(s);  // Processing string: hello
    process(v);  // Processing int vector, size: 3
    process(d);  // Processing unknown type
}
```

The key to `if constexpr`: branches whose conditions are not met are discarded at compile time and take no part in final code generation. This means you can use operations specific to a certain type in different branches (for example `container.size()`)—as long as that branch's condition fails in the current instantiation, the compiler will not check its semantic correctness. Note that discarded branches still go through basic syntax checking, and must not contain template-dependent names that cannot be parsed.

A more practical scenario is handling different iterator types—random-access iterators can be accessed with subscripts, while forward iterators only give you `++`. `if constexpr` lets a single lambda handle both cases elegantly.

---

## C++20 Template Lambdas — Explicit Template Parameters

C++14 generic lambdas with `auto` parameters are convenient, but they have a few problems: you cannot know the name of the deduced type, you cannot impose constraints on the template parameters, and you cannot refer to that type inside the lambda to declare other variables. C++20 gave lambdas explicit template parameter lists, solving all of these problems in one stroke:

```cpp
// C++20 template lambda: explicitly declaring template parameters
auto add_explicit = []<typename T>(T a, T b) {
    return a + b;
};

add_explicit(3, 4);       // T = int
add_explicit(3.0, 4.0);   // T = double
// add_explicit(3, 4.0);  // Compile error: T cannot be both int and double
```

The `<typename T>` syntax here is exactly the same as for ordinary templates. Both parameters have type `T`, so both arguments must be of the same type at the call site—precisely what C++14's `auto` cannot achieve.

### Using Template Parameter Names Inside the Lambda

The template parameter name can be used freely inside the lambda body, which is far more flexible than `auto`:

```cpp
#include <vector>
#include <iostream>

// Use the template parameter name to create containers or variables of the same type
auto transform_to_vector = []<typename T>(const std::vector<T>& input) {
    std::vector<T> result;
    result.reserve(input.size());
    for (const auto& elem : input) {
        result.push_back(elem * 2);
    }
    return result;
};

void demo_template_param_name() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    auto doubled = transform_to_vector(data);
    for (int x : doubled) {
        std::cout << x << " ";   // 2 4 6 8 10
    }
    std::cout << "\n";
}
```

With a C++14 `auto` parameter, what you get is `const std::vector<int>&`, but inside the lambda you do not know that the element type is `int`—you would have to deduce it with `decltype`. With a C++20 template parameter `T`, everything is straightforward.

### Constraining with Concepts

C++20 concepts and template lambdas are natural partners. You can use a `requires` clause to constrain the template parameters, so that the lambda only accepts types satisfying a specific concept:

```cpp
#include <concepts>
#include <iostream>
#include <string>

// Accepts integer types only
auto int_only = []<std::integral T>(T a, T b) {
    return a + b;
};

// Accepts floating-point types only
auto float_only = []<std::floating_point T>(T a, T b) {
    return a + b;
};

// Custom concept: types that support serialization
template<typename T>
concept Serializable = requires(T t, std::ostream& os) {
    { serialize(t, os) } -> std::same_as<void>;
};

auto serialize_and_log = []<Serializable T>(const T& obj) {
    std::ostringstream oss;
    serialize(obj, oss);
    std::cout << "Serialized: " << oss.str() << "\n";
};

void demo_concepts() {
    int_only(1, 2);         // OK
    // int_only(1.0, 2.0); // Compile error: double does not satisfy std::integral

    float_only(1.0, 2.0);   // OK
    // float_only(1, 2);   // Compile error: int does not satisfy std::floating_point
}
```

The benefit of concept constraints goes beyond compile-time type safety—the error messages are also far friendlier than traditional SFINAE. When you pass the wrong type, the compiler tells you directly that a "constraint was not satisfied" and points out exactly which concept failed, instead of dumping a huge stack of template instantiations. You can compile `code/volumn_codes/vol2/ch03-lambda/test_concepts_error_messages.cpp` and trigger the errors to compare the error-message quality of concepts versus SFINAE.

### Explicitly Specifying Template Arguments When Calling a Template Lambda

Sometimes you do not want the compiler to deduce the template arguments—you want to specify them yourself. Template lambdas also support explicitly specifying template arguments, though the syntax is a bit special:

```cpp
auto identity = []<typename T>(T x) { return x; };

// Normal call: the compiler deduces T = int
auto r1 = identity(42);

// Explicitly specifying the template argument
auto r2 = identity.template operator()<int>(42);
```

That `.template operator()<T>()` syntax is admittedly not pretty, but in practice you rarely need to call it explicitly—most of the time the compiler's deduction is enough. The main scenarios for explicit specification are when you want to force a conversion (for example, forcing an `int` to be treated as a `double`), or when the lambda uses `if constexpr` internally to choose different branches based on the template parameter.

---

## Recursive Lambdas

A lambda is anonymous—it has no name, so it cannot call itself from inside its own body. Yet recursion is a very common need in programming. We have several ways to get around this limitation.

### Approach 1: Wrapping with `std::function`

The most straightforward way is to store the lambda in a `std::function`, and then achieve self-invocation through the `std::function` variable's name:

```cpp
#include <functional>
#include <iostream>

void demo_recursive_std_function() {
    std::function<int(int)> factorial = [&factorial](int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    };

    std::cout << factorial(5) << "\n";   // 120
}
```

**Note**: calling through a `std::function` involves type erasure, and every recursive call is an indirect call through the virtual function table. In performance-sensitive code, this overhead needs to be taken into account. Actual measurements (see `code/volumn_codes/vol2/ch03-lambda/test_recursive_lambda_performance.cpp`) show that under -O2 optimization, the recursive `std::function` version is roughly 70-150x slower than a templated implementation (depending on recursion depth and the compiler's optimization capability).

### Approach 2: Generic Lambda + auto&& Parameter (the Y-Combinator Idea)

A more efficient approach exploits generic lambdas: pass the "reference to itself" in as an argument. This is a simplified take on the Y-combinator idea:

```cpp
#include <iostream>

// Y-combinator helper: takes a higher-order function and returns its fixed point
template<typename F>
class YCombinator {
    F f_;
public:
    explicit YCombinator(F f) : f_(std::move(f)) {}

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) {
        return f_(*this, std::forward<Args>(args)...);
    }
};

template<typename F>
YCombinator(F) -> YCombinator<F>;

void demo_y_combinator() {
    auto factorial = YCombinator([](auto&& self, int n) -> int {
        if (n <= 1) return 1;
        return n * self(n - 1);
    });

    std::cout << factorial(5) << "\n";   // 120
    std::cout << factorial(10) << "\n";  // 3628800
}
```

The key to this version: the generic lambda's first parameter, `auto&& self`, receives a reference to the `YCombinator` object itself. Inside the lambda, the recursive call happens through `self(n - 1)`. Because `YCombinator::operator()` is a function template, the compiler can inline the entire call chain.

**Performance comparison** (based on `test_recursive_lambda_performance.cpp`, measured with g++ 15.2.1 -O2, 1,000,000 calls to `factorial(10)`):

- `std::function` version: ~18,700 µs (type-erasure overhead, hard to optimize away)
- Y-combinator version: ~130-250 µs (templated, fully inlinable)
- Speedup: roughly 75-145x

In practice, if your recursion depth is small or the call frequency is low, the simplicity of `std::function` may matter more. But for performance-critical code, the Y combinator or directly passing a self-reference is the better fit.

### Approach 3: A C++14 Generic Lambda That Passes Itself

If you would rather not write a Y-combinator helper class, there is a clever shortcut—receive the self-reference through an `auto&` parameter:

```cpp
#include <iostream>

void demo_self_ref() {
    // fibonacci
    auto fib = [](auto&& self, int n) -> long long {
        if (n <= 1) return n;
        return self(self, n - 1) + self(self, n - 2);
    };

    std::cout << fib(fib, 10) << "\n";   // 55
}
```

The problem with this style is that the caller must manually pass the lambda itself—`fib(fib, 10)` instead of `fib(10)`. It looks a bit odd, but it is acceptable for internal logic that never needs to be wrapped into an API.

---

## General-Purpose Examples

### A Generic Comparator

```cpp
#include <algorithm>
#include <vector>
#include <string>

// Generic comparator: sort by any field
template<typename Projection>
auto make_comparator(Projection proj) {
    return [proj = std::move(proj)](const auto& a, const auto& b) {
        return proj(a) < proj(b);
    };
}

struct Employee {
    std::string name;
    int age;
    double salary;
};

void demo_generic_comparator() {
    std::vector<Employee> employees = {
        {"Alice", 30, 85000.0},
        {"Bob", 25, 72000.0},
        {"Charlie", 35, 92000.0},
    };

    // Sort by age
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return e.age; }));

    // Sort by salary, descending
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return -e.salary; }));

    // Sort by name
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) -> const auto& { return e.name; }));
}
```

### A Generic Transformer

```cpp
#include <vector>
#include <algorithm>
#include <iterator>

// Generic transform: apply the transformation function to every element of the container
auto make_transformer = [](auto func) {
    return [f = std::move(f)](auto& container) {
        std::transform(container.begin(), container.end(),
                      container.begin(), f);
        return container;
    };
};

// Chained transforms
auto make_pipeline = [](auto... transforms) {
    return [=](auto input) {
        auto current = std::move(input);
        // Apply each transform in turn (C++17 fold expression)
        ((current = transforms(current)), ...);
        return current;
    };
};

void demo_generic_transformer() {
    auto double_it = make_transformer([](int x) { return x * 2; });
    auto add_one = make_transformer([](int x) { return x + 1; });

    std::vector<int> data = {1, 2, 3, 4, 5};
    auto result = double_it(data);    // {2, 4, 6, 8, 10}
}
```

### Polymorphic Container Operations

Generic lambdas combined with template functions let you write generic algorithms that do not depend on any concrete container type. The following example uses a generic lambda to print containers of arbitrary types, as long as the container's element type supports `operator<<`:

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <array>
#include <set>

auto print_container = [](const auto& container) {
    using T = std::decay_t<decltype(container)>;
    std::cout << "[";
    bool first = true;
    for (const auto& elem : container) {
        if (!first) std::cout << ", ";
        std::cout << elem;
        first = false;
    }
    std::cout << "]\n";
};

void demo_polymorphic_container() {
    std::vector<int> v = {1, 2, 3};
    std::list<double> l = {1.1, 2.2, 3.3};
    std::array<std::string, 2> a = {"hello", "world"};
    std::set<int> s = {5, 3, 1, 4, 2};

    print_container(v);   // [1, 2, 3]
    print_container(l);   // [1.1, 2.2, 3.3]
    print_container(a);   // [hello, world]
    print_container(s);   // [1, 2, 3, 4, 5]
}
```

The flexibility of generic lambdas makes this kind of "write once, use everywhere" generic operation feel completely natural. You do not need to write one overload per container type—an `auto` parameter combined with a range-based for loop, and a single lambda handles every container that supports iteration.

---

## References

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)

## Verification Code

This chapter's performance comparisons and proof-of-concept code are located in `code/volumn_codes/vol2/ch03-lambda/`:

- `test_recursive_lambda_performance.cpp`: benchmarks comparing the different recursive-lambda implementations
- `test_concepts_error_messages.cpp`: comparing error-message quality between concepts and SFINAE

Build and run (CMake required):

```bash
cd code/volumn_codes/vol2/ch03-lambda
cmake -B build
cmake --build build
./build/test_recursive_lambda_performance
./build/test_concepts_error_messages
```
