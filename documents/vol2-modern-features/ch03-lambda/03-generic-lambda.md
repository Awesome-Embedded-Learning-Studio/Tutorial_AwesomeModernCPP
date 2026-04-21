---
title: "泛型 Lambda 与模板 Lambda"
description: "从 auto 参数到模板参数，lambda 的泛型编程能力"
chapter: 3
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - lambda
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [14, 17, 20]
reading_time_minutes: 15
prerequisites:
  - "Chapter 3: Lambda 基础"
  - "Chapter 3: Lambda 捕获机制深入"
related:
  - "函数式编程模式"
---

# 泛型 Lambda 与模板 Lambda

## 引言

在前面两章里，我们用的 lambda 参数类型都是具体的——`int`、`double`、`const std::string&` 之类的。但在实际项目中，很多 lambda 的逻辑对类型是中性的：排序的比较器只要求类型支持 `<`，累加器只要求类型支持 `+`。如果我们为每种类型都写一个 lambda，那就回到了 C++98 仿函数的老路上——重复、冗余。C++14 给了 lambda 泛型的能力（`auto` 参数），C++20 更是直接让 lambda 拥有了显式模板参数列表。这一章我们就来彻底搞清楚泛型 lambda 的底层机制、用法和边界。

> **学习目标**
>
> - 理解 C++14 泛型 lambda 的底层实现——模板调用运算符
> - 掌握 `if constexpr` 在 lambda 内部的使用方式
> - 学会 C++20 模板 lambda 的语法和概念约束
> - 了解递归 lambda 的几种实现方式及其权衡

---

## C++14 泛型 lambda——auto 参数

C++14 允许 lambda 的参数类型使用 `auto`。这种 lambda 被称为泛型 lambda（generic lambda）。对调用者来说，它就像一个模板函数——不同类型的参数会各自实例化一份 `operator()`：

```cpp
// 泛型 lambda：接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) {
    return a + b;
};

int xi = add(3, 4);                         // int
double xd = add(3.14, 2.72);                // double
std::string xs = add(std::string("hi "), std::string("there"));
```

同一个 lambda 对象，被不同类型的参数调用时，编译器会为每种参数类型组合生成一份 `operator()` 的实例。这个行为和函数模板的实例化完全一致。

### 底层实现：模板调用运算符

编译器在背后把泛型 lambda 翻译成的闭包类型大致如下：

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

每个 `auto` 参数都对应闭包类型 `operator()` 的一个模板参数。两个 `auto` 意味着 `operator()` 是一个双模板参数的成员函数模板。这个认知很重要——它意味着泛型 lambda 享有模板的所有能力，包括 SFINAE、显式实例化等等。

### 多种类型的 auto 参数

值得注意的是，每个 `auto` 都是独立的模板参数，推导规则互不影响：

```cpp
auto multiply = [](auto a, auto b) {
    return a * b;
};

multiply(3, 4.5);    // int * double -> double
multiply(2.0f, 3);   // float * int -> float
```

如果你希望两个参数是同一个类型，在 C++14 中需要借助一些技巧（比如用 `std::common_type_t`），在 C++20 中可以直接用模板参数来表达（稍后我们会讲到）。

---

## if constexpr 在 lambda 中

C++17 的 `if constexpr` 可以在编译期根据类型信息选择不同的代码路径。在泛型 lambda 中，它特别有用——你可以根据参数的类型特征来选择不同的实现：

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

`if constexpr` 的关键在于：不满足条件的分支会被直接丢弃，不会参与编译。这意味着你可以在不同的分支里使用某种类型特有的操作（比如 `container.size()`），只要那个分支不是实际实例化的那个，编译器就不会报错。

一个更实用的场景是处理不同迭代器类型——对随机访问迭代器可以用下标访问，对前向迭代器只能用 `++`。`if constexpr` 让你在一个 lambda 里优雅地处理这两种情况。

---

## C++20 模板 lambda——显式模板参数

C++14 的泛型 lambda 用 `auto` 参数很方便，但有几个问题：你无法知道推导出的具体类型叫什么名字，无法对模板参数施加约束，无法在 lambda 内部引用这个类型来声明其他变量。C++20 给 lambda 加上了显式模板参数列表，一举解决了这些问题：

```cpp
// C++20 模板 lambda：显式声明模板参数
auto add_explicit = []<typename T>(T a, T b) {
    return a + b;
};

add_explicit(3, 4);       // T = int
add_explicit(3.0, 4.0);   // T = double
// add_explicit(3, 4.0);  // 编译错误：T 不能同时是 int 和 double
```

这里 `<typename T>` 的语法和普通模板完全一致。两个参数都是 `T` 类型，所以调用时两个参数必须是同一类型——这正是 C++14 的 `auto` 做不到的事情。

### 在 lambda 内部使用模板参数名

模板参数名可以在 lambda 体内自由使用，这比 `auto` 灵活多了：

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

如果用 C++14 的 `auto` 参数，你拿到的是 `const std::vector<int>&`，但在 lambda 内部你不知道元素类型是 `int`——你得用 `decltype` 去推。有了 C++20 模板参数 `T`，一切都直截了当。

### 配合 Concepts 进行约束

C++20 的 Concepts 和模板 lambda 是天然的搭档。你可以用 `requires` 从句对模板参数施加约束，让 lambda 只接受满足特定概念的类型：

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

 Concepts 约束的好处不仅在于编译期类型安全——错误信息也比 SFINAE 友好太多了。当你传了错误的类型，编译器会直接告诉你"这个类型不满足某某概念"，而不是吐出一屏幕模板实例化堆栈。

### 调用模板 lambda 时显式指定模板参数

有时候你不想让编译器推导模板参数，想自己显式指定。模板 lambda 也支持显式模板参数调用，不过语法有点特殊：

```cpp
auto identity = []<typename T>(T x) { return x; };

// 正常调用，编译器推导 T = int
auto r1 = identity(42);

// 显式指定模板参数
auto r2 = identity.template operator()<int>(42);
```

那个 `.template operator()<T>()` 的语法确实不太好看，但实际上你很少需要显式调用它——大部分时候编译器的推导就够用了。需要显式指定的场景主要是你想强制某种转换（比如把 `int` 强制作为 `double` 处理），或者 lambda 内部用 `if constexpr` 根据模板参数选择不同的分支。

---

## 递归 Lambda

Lambda 本身是匿名的——它没有名字，所以不能在函数体里调用自己。但递归又是编程中很常见的需求。我们有几种方式来绕过这个限制。

### 方式 1：用 `std::function` 包装

最直观的方式是把 lambda 存进 `std::function`，然后通过 `std::function` 的变量名来实现自调用：

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

这个方案简单易懂，但有一个性能问题：`std::function` 的调用是间接的，每次递归都要经过类型擦除的分发逻辑。对于深度递归来说，这个开销可能不可忽视。

### 方式 2：泛型 lambda + auto&& 参数（Y 组合子思路）

更高效的方式是利用泛型 lambda 的特性，把"自身引用"作为参数传入。这是一种简化版的 Y 组合子思路：

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

这个版本的关键在于：泛型 lambda 的第一个参数 `auto&& self` 接收的是 `YCombinator` 对象本身的引用。lambda 内部通过 `self(n - 1)` 来实现递归调用。因为 `YCombinator::operator()` 是模板函数，编译器可以内联整个调用链——性能比 `std::function` 版本好得多。

### 方式 3：C++14 泛型 lambda 直接传自身

如果不想写 Y 组合子辅助类，还有一个取巧的办法——用 `auto&` 参数接收自身引用：

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

这个写法的问题是调用者必须手动把 lambda 自身传进去——`fib(fib, 10)` 而不是 `fib(10)`。虽然看起来有点怪，但在不需要封装到 API 里的内部逻辑中是可以接受的。

---

## 通用示例

### 通用比较器

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

### 通用变换器

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

### 多态容器操作

泛型 lambda 配合模板函数，可以写出不依赖具体容器类型的通用算法。下面这个例子用泛型 lambda 来打印任意类型的容器，只要容器的元素支持 `operator<<`：

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

泛型 lambda 的灵活性让这种"写一次、到处用"的通用操作变得非常自然。你不需要为每种容器类型各写一个重载——`auto` 参数配合范围 for 循环，一个 lambda 搞定所有支持迭代的容器。

---

## 小结

泛型 lambda 让 lambda 表达式从"一段固定的代码"进化成了"一段参数化的代码"。核心要点回顾：

- C++14 泛型 lambda 的 `auto` 参数对应闭包类型 `operator()` 的模板参数
- `if constexpr` 让泛型 lambda 可以根据类型信息选择不同的代码路径
- C++20 模板 lambda 用 `[]<typename T>` 语法提供了显式模板参数和 Concepts 约束
- 递归 lambda 可以通过 `std::function`（简单但有开销）或 Y 组合子模式（高效但语法稍复杂）来实现
- 泛型 lambda 在通用比较器、变换器、容器操作等场景下极其有用

## 参考资源

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)
