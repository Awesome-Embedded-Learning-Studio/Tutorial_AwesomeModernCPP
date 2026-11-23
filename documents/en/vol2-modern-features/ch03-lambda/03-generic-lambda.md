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
- 'Chapter 3: Lambda Basics: Elegant Anonymous Functions'
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
  source_hash: 2beeb8f1783b9e892dbd6fcac0e0cb5e7e008f4f7d539f226f18c61a08ba56d3
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 13191
---
# Generic Lambdas and Template Lambdas

Along the main line of the previous two articles, most of the lambdas we wrote pinned their parameter types down: `int`, `uint32_t`, `const std::string&`. Near the end of the first article we also caught a glimpse of an `auto` parameter, and what it left hanging is exactly what this article picks up and finishes. Once you get into a real project, a lot of lambda logic is actually type-neutral: a sorting comparator only asks the type to support `<`, and an accumulator only asks it to support `+`. If we wrote a separate lambda for every type, that would put us right back on the old C++98 functor road: the same logic gets copied all over again each time the type changes, and what you produce is repetitive and redundant. C++14 gave lambdas generic capabilities (`auto` parameters). C++20 then took a step further and gave lambdas explicit template parameter lists outright. Let's start with C++14's `auto`.

---

## C++14 Generic Lambdas — auto Parameters

You remember that `add` from the first article, right: its parameters were written plainly as `auto`. The standard gives this kind of lambda a formal name — we call it a generic lambda. The `auto` parameter is syntax C++14 introduced, and from where we callers stand it behaves exactly like a function template: whenever arguments of different types come in, the compiler instantiates a separate copy of `operator()` for each type.

```cpp
// 泛型 lambda：接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) {
    return a + b;
};

int xi = add(3, 4);                         // int
double xd = add(3.14, 2.72);                // double
std::string xs = add(std::string("hi "), std::string("there"));
```

Look at the three calls above: it is the same `add`, the integers, floating-point numbers, and strings we fed it were all caught, and we did not write a single line of template syntax.

We drew out the correspondence between a lambda object and its `operator()` instantiations:

![Instantiation of a generic lambda: one lambda corresponds to multiple operator() instances](./03-generic-lambda-instant.drawio)

### Under the Hood: The Template Call Operator

So what does the closure type — the thing the compiler translates the lambda into behind the scenes — look like? Let's look at a simplified version (the closure type is the class the compiler generates on behalf of each lambda):

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

Let's count them off: each `auto` parameter corresponds to one template parameter on the closure type's `operator()`. Write two `auto`s, and `operator()` becomes a member function template with two template parameters. And since `operator()` is at heart a template, a generic lambda enjoys the whole template toolkit — things like SFINAE (short for Substitution Failure Is Not An Error: a failed substitution does not count as a compile error, it just filters the mismatched candidate out of the overload set), plus tricks like explicit instantiation.

### auto Parameters of Multiple Types

One more detail deserves our attention: each `auto` is an independent template parameter, and their deductions do not affect one another.

```cpp
auto multiply = [](auto a, auto b) {
    return a * b;
};

multiply(3, 4.5);    // int * double -> double
multiply(2.0f, 3);   // float * int -> float
```

If you want both parameters to share one type, C++14 forces a detour through the `std::common_type_t` trick for computing the common type. C++20 saves us the trouble — we can state it directly with template parameters, which we will get to later in this article.

---

## if constexpr in Lambdas

We are still one section away from C++20. The C++17 release in between was not sitting idle either: the `if constexpr` it brought can pick different code paths at compile time based on type information. Drop it into a generic lambda and it becomes especially handy: we can choose different implementations for different types based on the parameter's type traits.

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

Where we really need to stay sharp is its discard behavior: branches whose conditions are not met are discarded at compile time and take no part in the final code generation. That is what lets us use operations specific to a certain type inside the different branches (`container.size()`, say) — as long as that branch's condition fails in the current instantiation, the compiler will not check its semantic correctness. One thing to stay careful about, though: a discarded branch still goes through basic syntax checking, and it must not contain template-dependent names that cannot be parsed.

A more practical scenario is handling different iterator types: random-access iterators can be accessed with subscripts, while forward iterators leave you nothing but `++`. Picking implementations by type is precisely `if constexpr`'s day job — you will reach for it again and again when writing generic algorithms later.

---

## C++20 Template Lambdas — Explicit Template Parameters

The `auto` parameters of generic lambdas are genuinely convenient, but `auto` has a few awkward corners: there is no way for you to write down the name of the concrete type that gets deduced. You cannot impose constraints on the template parameters, and you cannot reference the deduced type inside the lambda to declare other variables. C++20 simply gave lambdas explicit template parameter lists and solved all of these problems in one stroke:

```cpp
// C++20 模板 lambda：显式声明模板参数
auto add_explicit = []<typename T>(T a, T b) {
    return a + b;
};

add_explicit(3, 4);       // T = int
add_explicit(3.0, 4.0);   // T = double
// add_explicit(3, 4.0);  // 编译错误：T 不能同时是 int 和 double
```

Look at that `<typename T>` hanging off the square brackets: the syntax is identical to an ordinary template parameter list — however you usually write a function template is exactly how you write it here. With both parameters declared as `T`, a call must pass the same type for both. See the commented-out `add_explicit(3, 4.0)` line: that is the case that fails to compile. C++14's `auto` parameters, as it happens, cannot express this same-type constraint.

### Using Template Parameter Names Inside the Lambda

With the template parameter name `T` in hand, we can use it inside the lambda body with confidence — far more flexible than `auto`:

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

With a C++14 `auto` parameter, what you receive is a `const std::vector<int>&`, but the element type is something you do not know inside the lambda — we would have to deduce it through `decltype`. With the C++20 template parameter `T`, everything is direct: just write the `std::vector<T>` declaration down.

### Constraining with Concepts

C++20 concepts (the mechanism for adding compile-time constraints to template parameters) team up with template lambdas perfectly. We can use a `requires` clause to constrain the template parameters, so the lambda accepts only types that satisfy a particular concept:

```cpp
#include <concepts>
#include <iostream>
#include <sstream>
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

What concept constraints bring is more than compile-time type safety — the error messages are also far friendlier than the traditional SFINAE approach. Pass the wrong type, and the compiler tells you straight out "constraints not satisfied" and points out exactly which concept failed, instead of dumping a pile of template instantiation stack on you. You don't need a local compiler for this comparison: the demo below packs three "integers only" spellings (concepts, `static_assert`, SFINAE) into one file that compiles by default; uncomment the erroneous calls in main one at a time, hit "Run", and the result panel shows the raw diagnostics — three of them side by side:

<OnlineCompilerDemo
  title="Hands-On Comparison: error-message quality across three constraint styles"
  source-path="code/examples/vol2/56_concepts_sfinae_errors.cpp"
  description="Compiles as-is, printing 3/7/11. Uncomment an erroneous call (one at a time) and hit Run: the concepts version says outright that integral<T> is not satisfied and shows which deduction step failed; static_assert gives the message you wrote; SFINAE only says no matching function, plus a pile of enable_if candidates."
  run-options="-std=c++20"
  allow-run
/>

### Explicitly Specifying Template Parameters When Calling a Template Lambda

Sometimes you do not want the compiler to deduce the template parameter — you want to specify it yourself explicitly. Template lambdas do support an explicit call form; the syntax just looks a bit special:

```cpp
auto identity = []<typename T>(T x) { return x; };

// 正常调用，编译器推导 T = int
auto r1 = identity(42);

// 显式指定模板参数
auto r2 = identity.template operator()<int>(42);
```

We have to admit, the `.template operator()<T>()` syntax is on the ugly side, but in practice you rarely need to invoke it explicitly — most of the time the compiler's deduction is enough. The situations where we truly need explicit specification fall mainly into two classes: one is wanting to force a conversion (treating an `int` as a `double`, say); the other is a lambda that uses `if constexpr` internally to choose between branches based on the template parameter.

---

## Recursive Lambdas

A lambda is anonymous by nature, and without a name it naturally has no way to call itself from inside its own body. Yet recursion is a very common need in programming — write factorial, write Fibonacci, and you are counting on recursion the whole way. We have a few ways to route around this.

### Approach 1: Wrapping with `std::function`

The most intuitive approach we have is to store the lambda in a `std::function` (the general-purpose callable-object wrapper from `<functional>`). Once it is stored, the lambda can call itself through that variable name:

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

My actual measurements (code at `code/volumn_codes/vol2/ch03-lambda/test_recursive_lambda_performance.cpp`) show that under -O2 optimization, the recursive calls in the `std::function` version run about 75-145x slower than a templated implementation — exactly how much depends on the recursion depth and the compiler's optimization ability. Where does the slowness come from? Calling through a `std::function` involves type erasure (the technique of hiding concrete types behind a uniform interface), so every level of recursion pays one indirect call through the virtual function table. In performance-sensitive code, we have to weigh this cost carefully.

### Approach 2: Generic Lambda + auto&& Parameter (the Y combinator idea)

A more efficient approach exploits the generic lambda's properties: pass the "self reference" in as a parameter. This is a simplified version of the Y combinator idea. You may well be hearing of the Y combinator for the first time: it is a fixed-point combinator from lambda calculus, coming out of the mathematician Haskell Curry's work, built precisely to let functions without names recurse.

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

What we really need to see clearly is the first parameter: the generic lambda's `auto&& self` catches a reference to the `YCombinator` object itself, and inside the lambda it is `self(n - 1)` that completes the recursive call. Meanwhile, `YCombinator::operator()` being a template function means the compiler can inline the entire call chain away.

I took the same benchmark code and actually ran a round under g++ 15.2.1 -O2 (`1,000,000` calls to `factorial(10)`). The numbers I got:

- `std::function` version: ~18,700 µs (type erasure overhead, hard to optimize away)
- Y combinator version: ~130-250 µs (templated, fully inlinable)
- Speedup: about 75-145x

So how do we choose in a real project? Where recursion depth is small or call frequency is low, the simplicity of `std::function` may matter more. Code where performance counts is a better fit for the Y combinator, or for passing the self reference directly.

### Approach 3: A C++14 Generic Lambda That Passes Itself

If you would rather not write a Y combinator helper class, there is one more clever way out: give the lambda an `auto&&` parameter, and pass the lambda itself in when calling:

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

The cost is just as plain: the caller has to pass the lambda itself in by hand, writing `fib(fib, 10)` instead of `fib(10)`. The notation is admittedly a little weird, but in internal logic that never needs to be wrapped up as an API, we can accept it.

We turned the call chains of the three recursive styles into an animation you can step through: `std::function` loops back through the wrapper at every level, the Y combinator passes the self reference all the way down, and self-passing writes out as `fib(fib, 10)`.

<Anim id="recursive-lambda-ways" />

---

## General-Purpose Examples

### A Generic Comparator

Let's start with comparators. All `std::sort` asks for is a function that can compare two elements; which field we compare by gets left to the caller to decide.

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

### A Generic Transformer

With transformers we push even further: even "what to do to the container" itself becomes a parameter.

```cpp
#include <vector>
#include <algorithm>
#include <iterator>

// 通用变换：对容器中的每个元素应用变换函数
auto make_transformer = [](auto func) {
    return [f = std::move(func)](auto& container) {
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

Look at the line `((current = transforms(current)), ...)`: that is a C++17 fold expression. It lets us slip a whole pack of transforms onto `current` one by one.

### Polymorphic Container Operations

Finally, container operations. With generic lambdas working alongside function templates, we can write generic algorithms that do not depend on any concrete container type. The example below uses a generic lambda to print containers of arbitrary types, as long as the container's elements support `operator<<`:

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

Look at the four calls in `demo_polymorphic_container`: the same `print_container` catches all four container types, and none of the four output lines in the comments is missing. Pair an `auto` parameter with a range-based for loop, and a single lambda swallows every container that supports iteration — no need to write another overload for any particular container.

---

## References

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)

## Verification Code

The performance comparisons and proof-of-concept code for this article live at `code/volumn_codes/vol2/ch03-lambda/`:

- `test_recursive_lambda_performance.cpp`: performance benchmarks of the different recursive lambda implementations
- `test_concepts_error_messages.cpp`: comparing error-message quality between Concepts and SFINAE

We build and run with CMake:

```bash
cd code/volumn_codes/vol2/ch03-lambda
cmake -B build
cmake --build build
./build/test_recursive_lambda_performance
./build/test_concepts_error_messages
```
