---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: From `auto` parameters to template parameters, the generic programming
  capabilities of lambda expressions
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
  engine: anthropic
  source: documents/vol2-modern-features/ch03-lambda/03-generic-lambda.md
  source_hash: e2a11a6792b7308a7400fd05804263b3fd57fbb534dd4adb7451c57a4d06317f
  token_count: 3031
  translated_at: '2026-05-26T11:26:17.684374+00:00'
---
# Generic Lambdas and Template Lambdas

## Introduction

In the previous two chapters, the lambda parameter types we used were all concrete—`int`, `double`, `const std::string&`, and so on. But in real projects, many lambda implementations are type-agnostic: a sorting comparator only requires the type to support `<`, and an accumulator only requires support for `+`. If we wrote a separate lambda for every type, we would regress to the C++98 functor approach—repetitive and redundant. C++14 gave lambdas generic capabilities (`auto` parameters), and C++20 went even further by giving lambdas explicit template parameter lists. In this chapter, we will thoroughly explore the underlying mechanisms, usage patterns, and boundaries of generic lambdas.

> **Learning Objectives**
>
> - Understand the underlying implementation of C++14 generic lambdas—template call operators
> - Master the usage of `if constexpr` inside lambdas
> - Learn the syntax and concept constraints of C++20 template lambdas
> - Understand several approaches to implementing recursive lambdas and their trade-offs

---

## C++14 Generic Lambdas—auto Parameters

C++14 allows lambda parameter types to use `auto`. Such lambdas are called generic lambdas. To the caller, they behave like function templates—arguments of different types each instantiate a separate `operator()`:

```cpp
// 泛型 lambda：接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) {
    return a + b;
};

int xi = add(3, 4);                         // int
double xd = add(3.14, 2.72);                // double
std::string xs = add(std::string("hi "), std::string("there"));
```

When the same lambda object is invoked with arguments of different types, the compiler generates a separate instance of `operator()` for each combination of argument types. This behavior is completely consistent with function template instantiation.

### Underlying Implementation: Template Call Operator

Behind the scenes, the compiler translates a generic lambda into a closure type that looks roughly like this:

```cpp
// 你写的
auto add = [](auto a, auto b) { return a + b; };

// 编译器生成的（简化）
struct ClosureType {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

Each `auto` parameter corresponds to a template parameter of the closure type's `operator()`. Two `auto` parameters mean that `operator()` is a member function template with two template parameters. This understanding is crucial—it means generic lambdas enjoy all the capabilities of templates, including SFINAE (Substitution Failure Is Not An Error), explicit instantiation, and more.

### Multiple auto Parameters of Different Types

It is worth noting that each `auto` is an independent template parameter, and their deduction rules do not affect one another:

```cpp
auto multiply = [](auto a, auto b) {
    return a * b;
};

multiply(3, 4.5);    // int * double -> double
multiply(2.0f, 3);   // float * int -> float
```

If you want two parameters to be the same type, in C++14 you need to resort to some tricks (such as using `std::common_type_t`), whereas in C++20 you can express this directly using template parameters (which we will cover shortly).

---

## if constexpr in Lambdas

C++17's `if constexpr` allows you to select different code paths at compile time based on type information. In generic lambdas, this is particularly useful—you can choose different implementations based on the type traits of the arguments:

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

The key to `if constexpr` is that branches that do not satisfy the condition are discarded at compile time and do not participate in the final code generation. This means you can use operations specific to a certain type (such as `container.size()`) in different branches; as long as that branch does not satisfy the condition in the current instantiation, the compiler will not check its semantic correctness. Note that discarded branches still undergo basic syntax checking and cannot contain unresolvable template-dependent names.

A more practical scenario is handling different iterator types—random-access iterators can use subscript access, while forward iterators can only use `++`. `if constexpr` lets you elegantly handle both cases within a single lambda.

---

## C++20 Template Lambdas—Explicit Template Parameters

C++14 generic lambdas with `auto` parameters are convenient, but they have a few issues: you cannot know the name of the deduced type, you cannot impose constraints on the template parameters, and you cannot reference the type inside the lambda to declare other variables. C++20 adds explicit template parameter lists to lambdas, solving all these problems at once:

```cpp
// C++20 模板 lambda：显式声明模板参数
auto add_explicit = []<typename T>(T a, T b) {
    return a + b;
};

add_explicit(3, 4);       // T = int
add_explicit(3.0, 4.0);   // T = double
// add_explicit(3, 4.0);  // 编译错误：T 不能同时是 int 和 double
```

Here, the syntax of `<typename T>` is completely consistent with ordinary templates. Both parameters are of type `T`, so the two arguments must be of the same type when called—this is exactly what C++14's `auto` cannot achieve.

### Using Template Parameter Names Inside Lambdas

Template parameter names can be freely used inside the lambda body, which is much more flexible than `auto`:

```cpp
#include <vector>
#include <iostream>

// 用模板参数名创建同类型的容器或变量
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

If you use C++14's `auto` parameter, you get an `const std::vector<int>&`, but inside the lambda you do not know what the element type `int` is—you would have to use `decltype` to deduce it. With C++20 template parameters like `T`, everything becomes straightforward.

### Applying Constraints with Concepts

C++20 concepts and template lambdas are natural partners. You can use a `requires` clause to impose constraints on template parameters, making the lambda accept only types that satisfy a specific concept:

```cpp
#include <concepts>
#include <iostream>
#include <string>

// 只接受整数类型
auto int_only = []<std::integral T>(T a, T b) {
    return a + b;
};

// 只接受浮点类型
auto float_only = []<std::floating_point T>(T a, T b) {
    return a + b;
};

// 自定义概念：支持序列化的类型
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
    // int_only(1.0, 2.0); // 编译错误：double 不满足 std::integral

    float_only(1.0, 2.0);   // OK
    // float_only(1, 2);   // 编译错误：int 不满足 std::floating_point
}
```

The benefit of concept constraints lies not only in compile-time type safety—the error messages are also much friendlier than traditional SFINAE. When you pass the wrong type, the compiler will directly tell you that "constraints not satisfied" and point out exactly which concept failed, rather than outputting a massive template instantiation stack. You can compile `code/volumn_codes/vol2/ch03-lambda/test_concepts_error_messages.cpp` and trigger errors to compare the error message quality between concepts and SFINAE.

### Explicitly Specifying Template Arguments When Calling Template Lambdas

Sometimes you do not want the compiler to deduce the template parameters and prefer to specify them explicitly. Template lambdas also support explicit template argument specification, though the syntax is a bit unusual:

```cpp
auto identity = []<typename T>(T x) { return x; };

// 正常调用，编译器推导 T = int
auto r1 = identity(42);

// 显式指定模板参数
auto r2 = identity.template operator()<int>(42);
```

That `.template operator()<T>()` syntax is admittedly not very pretty, but in practice you rarely need to call it explicitly—most of the time, the compiler's deduction is sufficient. Scenarios that require explicit specification are mainly when you want to force a certain conversion (such as treating a `int` explicitly as a `double`), or when the lambda internally uses `if constexpr` to select different branches based on the template parameter.

---

## Recursive Lambdas

Lambdas are inherently anonymous—they have no name, so they cannot call themselves within the function body. However, recursion is a very common requirement in programming. We have several ways to work around this limitation.

### Approach 1: Wrapping with `std::function`

The most intuitive approach is to store the lambda in a `std::function` and then achieve self-invocation through the `std::function` variable name:

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

**Note**: Invoking a `std::function` involves type erasure, and each recursive call requires an indirect call through a virtual function table. In performance-sensitive code, this overhead must be considered. Actual testing (see `code/volumn_codes/vol2/ch03-lambda/test_recursive_lambda_performance.cpp`) shows that at the -O2 optimization level, the `std::function` version of recursive calls is about 70-150 times slower than a templated implementation (depending on recursion depth and compiler optimization capabilities).

### Approach 2: Generic Lambda + auto&& Parameter (Y Combinator Idea)

A more efficient approach leverages the characteristics of generic lambdas by passing a "self-reference" as a parameter. This is a simplified version of the Y combinator idea:

```cpp
#include <iostream>

// Y 组合子辅助函数：接受一个高阶函数，返回它的不动点
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

The key to this version is that the first parameter of the generic lambda, `auto&& self`, receives a reference to the `YCombinator` object itself. Inside the lambda, the recursive call is achieved through `self(n - 1)`. Because `YCombinator::operator()` is a template function, the compiler can inline the entire call chain.

**Performance Comparison** (based on `test_recursive_lambda_performance.cpp` test results under g++ 15.2.1 -O2, with 1,000,000 `factorial(10)` calls):

- `std::function` version: ~18,700 µs (type erasure overhead, difficult to optimize)
- Y Combinator version: ~130-250 µs (templated, fully inlinable)
- Performance improvement: approximately 75-145 times

In practice, if your recursion depth is small or the call frequency is low, the simplicity of `std::function` may be more important. But for performance-critical code, the Y combinator or the approach of directly passing a self-reference is more appropriate.

### Approach 3: C++14 Generic Lambda Directly Passing Self

If you do not want to write a Y combinator helper class, there is another trick—using an `auto&` parameter to receive a self-reference:

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

The problem with this approach is that the caller must manually pass the lambda itself—`fib(fib, 10)` instead of `fib(10)`. Although it looks a bit odd, it is acceptable in internal logic that does not need to be encapsulated in an API.

---

## General Examples

### Generic Comparator

```cpp
#include <algorithm>
#include <vector>
#include <string>

// 通用比较器：按任意字段排序
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

    // 按年龄排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return e.age; }));

    // 按薪资降序排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return -e.salary; }));

    // 按名字排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) -> const auto& { return e.name; }));
}
```

### Generic Transformer

```cpp
#include <vector>
#include <algorithm>
#include <iterator>

// 通用变换：对容器中的每个元素应用变换函数
auto make_transformer = [](auto func) {
    return [f = std::move(f)](auto& container) {
        std::transform(container.begin(), container.end(),
                      container.begin(), f);
        return container;
    };
};

// 链式变换
auto make_pipeline = [](auto... transforms) {
    return [=](auto input) {
        auto current = std::move(input);
        // 依次应用每个变换（C++17 fold expression）
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

Generic lambdas combined with template functions allow us to write generic algorithms that do not depend on specific container types. The following example uses a generic lambda to print any type of container, as long as the container's elements support `operator<<`:

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

The flexibility of generic lambdas makes this kind of "write once, use everywhere" generic operation very natural. You do not need to write a separate overload for each container type—`auto` parameters combined with a range-based for loop let one lambda handle all iterable containers.

---

## Summary

Generic lambdas evolve lambda expressions from "a fixed piece of code" into "a parameterized piece of code." Here is a review of the key points:

- C++14 generic lambda `auto` parameters correspond to template parameters of the closure type's `operator()`
- `if constexpr` allows generic lambdas to select different code paths based on type information
- C++20 template lambdas use `[]<typename T>` syntax to provide explicit template parameters and concept constraints
- Recursive lambdas can be implemented via `std::function` (simple but with overhead) or the Y combinator pattern (efficient but slightly more complex syntax)
- Generic lambdas are extremely useful in scenarios such as generic comparators, transformers, and container operations

## References

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)

## Verification Code

The performance comparisons and concept verification code for this chapter are located in `code/volumn_codes/vol2/ch03-lambda/`:

- `test_recursive_lambda_performance.cpp`: Performance benchmarks for different recursive lambda implementations
- `test_concepts_error_messages.cpp`: Error message quality comparison between concepts and SFINAE

To compile and run (requires CMake):

```bash
cd code/volumn_codes/vol2/ch03-lambda
cmake -B build
cmake --build build
./build/test_recursive_lambda_performance
./build/test_concepts_error_messages
```
