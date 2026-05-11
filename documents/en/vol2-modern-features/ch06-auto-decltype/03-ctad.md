---
title: Class template argument deduction (CTAD)
description: C++17 CTAD Mechanism and Custom Deduction Guides
chapter: 6
order: 3
tags:
- host
- cpp-modern
- intermediate
- 泛型
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
reading_time_minutes: 15
prerequisites:
- 'Chapter 6: auto 推导深入'
related:
- decltype 与返回类型推导
---
# Class Template Argument Deduction (CTAD)

Before C++17, every class template instantiation required spelling out all the template parameters. Even when the compiler could perfectly deduce the template arguments from the constructor parameters, we still had to write them out explicitly:

```cpp
std::pair<int, double> p(1, 2.0);           // 明明能推导出来
std::tuple<int, float, std::string> t(42, 3.14f, "hi");
std::vector<int> v = {1, 2, 3};              // 这个倒是不用写太多
std::lock_guard<std::mutex> lock(mtx);       // mutex 类型写了又写
```

C++17 finally lets us drop these redundant template parameters. This feature is called CTAD (Class Template Argument Deduction). It makes class templates feel more like ordinary classes—the compiler automatically deduces the template parameters from the constructor arguments, so we no longer need to specify them manually.

> In a nutshell: **CTAD saves you from manually writing class template arguments—the compiler deduces them from constructor parameters. When needed, you can write custom deduction guides to override the default behavior.**

------

## The Motivation for CTAD

### How Annoying It Used to Be

Let's look at a few scenarios where template parameters had to be spelled out before C++17:

```cpp
// pair 的类型完全能从参数推导，但必须手写
auto p = std::pair<int, double>(1, 2.0);

// make_pair 解决了 pair 的问题，但不通用
auto p2 = std::make_pair(1, 2.0);

// tuple 也得手写全部类型
auto t = std::tuple<int, float, std::string>(42, 3.14f, "hi");

// lock_guard 的 mutex 类型也得写
std::lock_guard<std::mutex> lock(mtx);
```

`std::make_pair` and `std::make_tuple` are essentially "factory functions" designed to work around the limitation that class templates couldn't deduce arguments automatically. But they were just special-case workarounds—not every class template had a corresponding make function.

### After CTAD

```cpp
std::pair p(1, 2.0);            // 推导为 std::pair<int, double>
std::tuple t(42, 3.14f, "hi");  // 推导为 std::tuple<int, float, const char*>
std::lock_guard lock(mtx);      // 推导为 std::lock_guard<std::mutex>
```

The code is cleaner, and we no longer need a bunch of make_xxx factory functions. In fact, after C++17, the only real use case for many make functions is to work around CTAD's limitations—in most cases, using the class name directly is sufficient.

------

## CTAD in the Standard Library

C++17 added deduction guides for many standard library class templates. Here are the most commonly used ones:

### pair and tuple

These are the most intuitive CTAD use cases. The type of each element is deduced from the constructor arguments:

```cpp
std::pair p(1, 2.0);               // std::pair<int, double>
std::pair p2 = {1, 2.0};           // 同上
std::tuple t(1, 2.0, "three");     // std::tuple<int, double, const char*>
```

### vector and Other Containers

`std::vector` has a special deduction guide that deduces the element type from an iterator pair:

```cpp
std::vector v1 = {1, 2, 3};                    // std::vector<int>
std::vector v2(v1.begin(), v1.begin() + 2);    // std::vector<int>

// 从其他容器迭代
std::set<int> s = {1, 2, 3};
std::vector v3(s.begin(), s.end());             // std::vector<int>
```

⚠️ Note: `std::vector v = {1, 2, 3}` works because the standard library provides a deduction guide for `std::vector` that accepts `std::initializer_list<T>`. However, not all containers have similar deduction guides—for example, brace-initialized deduction for `std::map` was incomplete in C++17 and only received formal pair-like deduction support in C++26.

### Smart Pointers

⚠️ **Note**: `std::unique_ptr` and `std::shared_ptr` do **not** support CTAD from raw pointers. The following code will fail to compile:

```cpp
// 编译错误！智能指针不支持从 new 表达式 CTAD
// std::unique_ptr up(new int(42));
// std::shared_ptr sp(new int(42));
```

This is because the template argument deduction rules for smart pointer constructors differ from ordinary class templates—their constructors accept a pointer type, but the template parameter cannot be deduced from a raw pointer.

**The correct approach** is to use `make_unique` and `make_shared` (recommended) or to specify the template arguments explicitly:

```cpp
// 推荐：使用 make 函数（异常安全）
auto up1 = std::make_unique<int>(42);
auto sp1 = std::make_shared<int>(42);

// 或显式指定模板参数
std::unique_ptr<int> up2(new int(42));
std::shared_ptr<int> sp2(new int(42));
```

CTAD with smart pointers is primarily useful when using custom deleters, but even then the deleter type must be specified explicitly:

```cpp
std::unique_ptr<FILE, decltype(&std::fclose)> fp(std::fopen("file.txt", "r"), &std::fclose);
// 需要显式指定模板参数，不能 CTAD
```

### optional and variant

```cpp
std::optional o = 42;          // std::optional<int>
std::optional o2 = 3.14;       // std::optional<double>

// variant 的 CTAD 比较特殊——需要通过赋值来推导
std::variant<int, double> v = 42;  // 仍然需要手写模板参数
```

### array

```cpp
std::array a = {1, 2, 3, 4, 5};  // std::array<int, 5>
// 第二个模板参数（大小）从花括号初始化列表的长度推导
```

This works in C++17 and is especially convenient—no need to manually count the number of elements.

### Summary: Standard Library CTAD at a Glance

| Class Template | CTAD Syntax | Deduced Result | Notes |
|--------|----------|---------|------|
| `std::pair` | `std::pair p(1, 2.0)` | `pair<int, double>` | ✓ Supported |
| `std::tuple` | `std::tuple t(1, 2.0, "hi")` | `tuple<int, double, const char*>` | ✓ Supported |
| `std::vector` | `std::vector v = {1,2,3}` | `vector<int>` | ✓ Supported |
| `std::array` | `std::array a = {1,2,3}` | `array<int, 3>` | ✓ Supported (deduction guide) |
| `std::optional` | `std::optional o = 42` | `optional<int>` | ✓ Supported |
| `std::unique_ptr` | `std::unique_ptr up(new T)` | — | ✗ **Not supported** |
| `std::shared_ptr` | `std::shared_ptr sp(new T)` | — | ✗ **Not supported** |
| `std::lock_guard` | `std::lock_guard lock(mtx)` | `lock_guard<mutex>` | ✓ Supported |

------

## Implicit Deduction Guides

CTAD isn't magic—the compiler uses "deduction guides" to know how to deduce template parameters. If a class template's constructor uses all template parameters, the compiler automatically generates an implicit deduction guide.

### Deduction from Constructors

```cpp
template<typename T, typename U>
struct MyPair {
    T first;
    U second;
    MyPair(T f, U s) : first(f), second(s) {}
};

MyPair p(1, 2.0);  // 隐式推导为 MyPair<int, double>
```

When the compiler sees the constructor `MyPair(T f, U s)`, it automatically generates an equivalent deduction guide: as long as `int` and `double` arguments are passed, it deduces `T` as `int` and `U` as `double`.

### Multiple Constructors

If a class template has multiple constructors, the compiler generates an implicit deduction guide for each one. When creating an object, the compiler tries all deduction guides and selects the best match:

```cpp
template<typename T>
class Wrapper {
public:
    Wrapper(T val) : value_(val) {}
    Wrapper(const T* ptr) : value_(*ptr) {}
private:
    T value_;
};

Wrapper w1(42);        // 使用第一个构造函数，推导为 Wrapper<int>
int x = 10;
Wrapper w2(&x);        // 使用第二个构造函数，推导为 Wrapper<int>
```

### Limitations of Implicit Deduction

Implicit deduction guides cannot deduce nested template parameters. For example, if you have a `Container<std::vector<T>>`, implicit deduction cannot reverse-engineer `T = int` from `std::vector<int>`. This requires a custom deduction guide to resolve.

Additionally, if a constructor has default arguments, the implicit deduction guide only considers the parameters without default values. Template parameters with defaults are not automatically deduced—unless you write a custom deduction guide.

------

## Custom Deduction Guides

When implicit deduction guides aren't enough, we can write deduction guides manually. The syntax looks a bit like a function signature:

```cpp
template<typename ...>
ClassName(params) -> ClassName<deduced types>;
```

### Basic Example

Suppose we have a strong-typed wrapper used to distinguish numeric values of different units:

```cpp
template<typename T, typename Tag>
class StrongType {
public:
    explicit StrongType(T value) : value_(value) {}
    T get() const { return value_; }
private:
    T value_;
};

struct MeterTag {};
struct SecondTag {};

using Meter  = StrongType<double, MeterTag>;
using Second = StrongType<double, SecondTag>;
```

This class has only one template parameter, `T`, that appears in the constructor, while `Tag` doesn't appear in the constructor at all. Implicit deduction can only deduce `T`, not `Tag`. In this case, CTAD isn't a great fit—we should use a `using` alias directly.

But if we change the design so that Tag also participates in deduction:

```cpp
template<typename T, typename Tag>
class StrongType {
public:
    explicit StrongType(T value) : value_(value) {}
    T get() const { return value_; }
private:
    T value_;
};

// 自定义推导指引：从值类型推导
template<typename T>
StrongType(T) -> StrongType<T, struct DefaultTag>;

StrongType s(42);  // StrongType<int, DefaultTag>
```

### A Practical Deduction Guide Example

A more practical scenario is a custom container. Suppose we have a simple fixed-size buffer:

```cpp
template<typename T, std::size_t N>
class FixedBuffer {
public:
    FixedBuffer(std::initializer_list<T> init) {
        std::copy(init.begin(), init.begin() + N, data_.begin());
    }

    // ... 其他成员

private:
    std::array<T, N> data_;
};

// 自定义推导指引：从花括号列表推导 T 和 N
template<typename T, typename... Args>
FixedBuffer(T, Args...) -> FixedBuffer<T, 1 + sizeof...(Args)>;
```

With this deduction guide, we can create buffers like this:

```cpp
FixedBuffer buf = {1, 2, 3, 4, 5};  // FixedBuffer<int, 5>
```

Deduction guides work similarly to function template overload resolution. The compiler considers all deduction guides (both implicitly generated and user-defined), and selects the best match. If a custom deduction guide is a better match than an implicit one, the compiler chooses the custom one.

### Custom Deduction Guides in the Standard Library

The standard library itself makes extensive use of custom deduction guides. For example, the guide that deduces `std::vector` from an iterator pair:

```cpp
// 大致等价于标准库中的推导指引
template<typename InputIt>
vector(InputIt, InputIt) -> vector<typename iterator_traits<InputIt>::value_type>;
```

This deduction guide allows `std::vector v(it1, it2)` to correctly deduce the element type, rather than trying to treat the iterator type itself as the element type.

------

## CTAD Limitations and Pitfalls

### Aggregate Types Don't Support CTAD in C++17

C++17's CTAD does not support aggregate types. Aggregate types are classes with no user-declared constructors, no private/protected members, and no base classes. The underlying type of something like `std::array` is an aggregate, and it only supports CTAD because the standard library specifically wrote a deduction guide for it.

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];
    // 没有构造函数——是聚合类型
};

MyArray a = {1, 2, 3};  // C++17：编译错误！聚合不支持 CTAD
```

### C++20: Limited Aggregate CTAD

⚠️ **Important clarification**: C++20 did **not** add generic CTAD support for all aggregate types. The following code **still fails to compile** in C++20:

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];  // 没有构造函数，是聚合类型
};

MyArray a = {1, 2, 3};  // C++20：仍然编译错误！
```

C++20's support for aggregate CTAD is very limited—the main improvement allows deduction in certain specific scenarios, but it is not a generic aggregate CTAD. To make the code above work, we still need to write a deduction guide manually or add a constructor.

**Why does `std::array` work with CTAD?**

`std::array` supports `std::array a = {1, 2, 3}` because the standard library wrote a dedicated deduction guide for it, not because of C++20's aggregate CTAD:

```cpp
// 标准库中的推导指引（简化版）
template<typename T, typename... Args>
array(T, Args...) -> array<T, 1 + sizeof...(Args)>;
```

If we need our own aggregate types to support CTAD, the most reliable approach is to add a deduction guide or provide a constructor.

### Alias Templates Don't Support CTAD

We cannot directly use alias templates to deduce parameters—an alias template is not a class template, and CTAD only applies to class templates:

```cpp
template<typename T>
using MyVec = std::vector<T, MyAllocator<T>>;

MyVec v = {1, 2, 3};  // 编译错误：别名模板不支持 CTAD
```

C++20 introduced deduction guide support for alias templates, but the rules are fairly complex and many compilers have incomplete support.

### Forwarding References and CTAD

When a constructor accepts a forwarding reference, CTAD may deduce an unexpected type. Because a forwarding reference can match any type, including reference types:

```cpp
template<typename T>
struct Wrapper {
    Wrapper(T&& val) : value_(std::forward<T>(val)) {}
    T value_;
};

int x = 42;
Wrapper w(x);  // T 推导为 int&（不是 int！）
```

Here, under forwarding reference rules, when the lvalue `x` is passed, `T` is deduced as `int&`. So the type of `Wrapper w(x)` is `Wrapper<int&>`, and the type of its member `value_` is `int&`. This is likely not the desired behavior. The solution is to use `std::remove_reference_t` or a custom deduction guide to constrain the deduced result.

### Copy Initialization vs. Direct Initialization

CTAD may behave differently with copy initialization (`=`) and direct initialization (`()`):

```cpp
std::vector v1{1, 2, 3};        // 直接初始化，CTAD 工作
std::vector v2 = {1, 2, 3};     // 拷贝初始化，CTAD 工作（有专门的推导指引）

// 某些自定义类型可能只在其中一种情况下工作
```

Tip: if we encounter a situation where CTAD doesn't work with one form of initialization, try switching to the other. Alternatively, check whether our deduction guides cover that particular initialization style.

------

## In Practice: Deduction Guides for Strong-Typed Wrappers

Let's write a complete example showing how CTAD makes strong-typed wrappers more natural to use.

```cpp
#include <cstdint>
#include <utility>

/// @brief 强类型包装器，防止不同语义的类型混用
template<typename T, typename Tag>
class StrongTypedef {
public:
    explicit constexpr StrongTypedef(T value) : value_(value) {}
    constexpr T& get() { return value_; }
    constexpr const T& get() const { return value_; }
private:
    T value_;
};

// 标签类型（空类，不占空间）
struct MeterTag {};
struct KilometerTag {};
struct CelsiusTag {};

// 别名
using Meter     = StrongTypedef<double, MeterTag>;
using Kilometer = StrongTypedef<double, KilometerTag>;
using Celsius   = StrongTypedef<double, CelsiusTag>;

// 自定义推导指引：字面量自动推导为对应类型
// （这个例子中其实不太需要，因为已经有 using 别名了）
// 但展示了语法
template<typename T>
StrongTypedef(T) -> StrongTypedef<T, struct GenericTag>;

// 使用
int main() {
    Meter distance(100.0);
    Celsius temp(23.5);

    // distance + temp 编译错误——不同的 Tag，不能混用
    // 这是强类型的核心价值
}
```

This example illustrates the design philosophy behind CTAD: for types that already have aliases defined via `using` (such as `Meter`), just use the alias directly—CTAD isn't needed. CTAD is more useful for scenarios where template parameters can be naturally deduced from constructor arguments.

------

## Summary

CTAD is a practical "reduce boilerplate" feature in C++17. It makes class template instantiation feel much closer to using ordinary classes. Standard library types like `pair`, `tuple`, `vector`, `array`, `optional`, and `lock_guard` all support CTAD, which covers most everyday development needs.

There are three key takeaways: first, implicit deduction guides are automatically generated from constructors, covering most scenarios; second, when implicit deduction falls short, we can write custom deduction guides to extend the deduction behavior; third, **be aware that not all class templates support CTAD**—smart pointers and aggregate types, for instance, have notable limitations.

Key limitations to keep in mind: smart pointers (`unique_ptr`/`shared_ptr`) don't support CTAD from raw pointers, aggregate types still don't support generic CTAD in C++20, alias templates don't support CTAD, and forwarding references can lead to unexpected reference type deduction. As long as we know about these pitfalls, we can quickly identify them when they arise.

## References

- [cppreference: Class template argument deduction](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction)
- [CTAD in C++17 - Simon Toth](https://medium.com/@simontoth/daily-bit-e-of-c-class-template-argument-deduction-ctad-f0886131c129)
- [C++17's CTAD - Andreas Fertig](https://andreasfertig.com/blog/2022/11/cpp17s-ctad-a-sometimes-underrated-feature/)
