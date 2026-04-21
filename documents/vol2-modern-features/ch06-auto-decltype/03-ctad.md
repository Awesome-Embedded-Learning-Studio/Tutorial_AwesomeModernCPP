---
title: "类模板参数推导 (CTAD)"
description: "C++17 的 CTAD 机制与自定义推导指引"
chapter: 6
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
reading_time_minutes: 15
prerequisites:
  - "Chapter 6: auto 推导深入"
related:
  - "decltype 与返回类型推导"
---

# 类模板参数推导 (CTAD)

在 C++17 之前，每次实例化类模板都得把模板参数写全。哪怕编译器完全能从构造函数的参数推导出模板参数，你也得老老实实写一遍：

```cpp
std::pair<int, double> p(1, 2.0);           // 明明能推导出来
std::tuple<int, float, std::string> t(42, 3.14f, "hi");
std::vector<int> v = {1, 2, 3};              // 这个倒是不用写太多
std::lock_guard<std::mutex> lock(mtx);       // mutex 类型写了又写
```

C++17 终于让我们省掉了这些冗余的模板参数。这个特性叫 CTAD（Class Template Argument Deduction，类模板参数推导）。它让类模板用起来更像普通类——编译器从构造函数的参数自动推导模板参数，你不用再手动指定。

> 一句话总结：**CTAD 让你省去手写类模板参数的麻烦，编译器从构造函数参数中自动推导。需要时你还可以写自定义推导指引来覆盖默认行为。**

------

## CTAD 的动机

### 以前有多烦

先看几个 C++17 之前必须写全模板参数的场景：

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

`std::make_pair`、`std::make_tuple` 这些"工厂函数"本质上就是为了绕过类模板不能自动推导参数的限制。但它们只是特殊方案，不是每个类模板都有对应的 make 函数。

### CTAD 之后

```cpp
std::pair p(1, 2.0);            // 推导为 std::pair<int, double>
std::tuple t(42, 3.14f, "hi");  // 推导为 std::tuple<int, float, const char*>
std::lock_guard lock(mtx);      // 推导为 std::lock_guard<std::mutex>
```

代码更简洁了，而且不再需要一堆 make_xxx 工厂函数。事实上，C++17 之后很多 make 函数的唯一用途就是配合 CTAD 的局限性场景——大部分情况下你直接用类名就够了。

------

## 标准库中的 CTAD

C++17 为标准库的很多类模板添加了推导指引。这里列出最常用的几个：

### pair 和 tuple

这是最直观的 CTAD 用例。从构造函数参数推导每个元素的类型：

```cpp
std::pair p(1, 2.0);               // std::pair<int, double>
std::pair p2 = {1, 2.0};           // 同上
std::tuple t(1, 2.0, "three");     // std::tuple<int, double, const char*>
```

### vector 和其他容器

`std::vector` 有一个特殊的推导指引：从迭代器对推导元素类型：

```cpp
std::vector v1 = {1, 2, 3};                    // std::vector<int>
std::vector v2(v1.begin(), v1.begin() + 2);    // std::vector<int>

// 从其他容器迭代
std::set<int> s = {1, 2, 3};
std::vector v3(s.begin(), s.end());             // std::vector<int>
```

⚠️ 注意：`std::vector v = {1, 2, 3}` 能推导是因为标准库为 `std::vector` 提供了接受 `std::initializer_list<T>` 的推导指引。但不是所有容器都有类似的推导指引——比如 `std::map` 的花括号初始化推导在 C++17 中并不完善，到 C++26 才有正式的 pair-like 推导支持。

### smart pointers

`std::unique_ptr` 和 `std::shared_ptr` 也支持 CTAD，但用法有限制：

```cpp
// 从 new 表达式推导
std::unique_ptr up(new int(42));           // std::unique_ptr<int>
std::shared_ptr sp(new int(42));           // std::shared_ptr<int>

// 但 make_unique/make_shared 仍然推荐使用（异常安全）
auto up2 = std::make_unique<int>(42);      // 仍然推荐
auto sp2 = std::make_shared<int>(42);      // 仍然推荐
```

对于 `unique_ptr` 和 `shared_ptr`，`make_unique` 和 `make_shared` 仍然是首选，因为它们提供了异常安全的内存分配。CTAD 主要在你必须使用 `new` 的场景（比如自定义删除器）中有用：

```cpp
std::unique_ptr fp(std::fopen("file.txt", "r"), &std::fclose);
// 推导为 std::unique_ptr<FILE, int(*)(FILE*)>
```

### optional 和 variant

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

这个在 C++17 中就能工作，特别方便——不用手数元素个数了。

### 小结：标准库 CTAD 一览

| 类模板 | CTAD 写法 | 推导结果 |
|--------|----------|---------|
| `std::pair` | `std::pair p(1, 2.0)` | `pair<int, double>` |
| `std::tuple` | `std::tuple t(1, 2.0, "hi")` | `tuple<int, double, const char*>` |
| `std::vector` | `std::vector v = {1,2,3}` | `vector<int>` |
| `std::array` | `std::array a = {1,2,3}` | `array<int, 3>` |
| `std::optional` | `std::optional o = 42` | `optional<int>` |
| `std::unique_ptr` | `std::unique_ptr up(new T)` | `unique_ptr<T>` |
| `std::lock_guard` | `std::lock_guard lock(mtx)` | `lock_guard<mutex>` |

------

## 隐式推导指引

CTAD 并不是魔法——编译器通过"推导指引"（deduction guide）来知道如何推导模板参数。如果一个类模板的构造函数使用了所有模板参数，编译器会自动生成一个隐式推导指引。

### 从构造函数推导

```cpp
template<typename T, typename U>
struct MyPair {
    T first;
    U second;
    MyPair(T f, U s) : first(f), second(s) {}
};

MyPair p(1, 2.0);  // 隐式推导为 MyPair<int, double>
```

编译器看到构造函数 `MyPair(T f, U s)`，会自动生成一个等价的推导指引：只要传入 `int` 和 `double` 参数，就把 `T` 推导为 `int`，`U` 推导为 `double`。

### 多个构造函数的情况

如果一个类模板有多个构造函数，编译器会为每个构造函数生成一个隐式推导指引。当创建对象时，编译器会尝试所有的推导指引，选择最匹配的那个：

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

### 隐式推导的局限

隐式推导指引不能推导嵌套的模板参数。比如你有一个 `Container<std::vector<T>>`，隐式推导无法从 `std::vector<int>` 反推出 `T = int`。这需要自定义推导指引来解决。

此外，如果构造函数有默认参数，隐式推导指引只考虑没有默认值的参数。带默认值的模板参数不会被自动推导——除非你写自定义推导指引。

------

## 自定义推导指引

当隐式推导指引不够用时，你可以手动写推导指引。语法看起来有点像函数签名：

```cpp
template<typename ...>
ClassName(params) -> ClassName<deduced types>;
```

### 基本例子

假设我们有一个强类型包装器，用于区分不同单位的数值：

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

这个类只有一个模板参数 `T` 出现在构造函数中，`Tag` 完全不在构造函数里。隐式推导只能推导 `T`，推导不了 `Tag`。这种情况下 CTAD 不太适用——应该直接用 `using` 别名。

但如果我们换一个设计，让 Tag 也能参与推导：

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

### 实用的推导指引例子

一个更实用的场景是自定义容器。假设我们有一个简单的固定大小缓冲区：

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

有了这个推导指引，你可以这样创建缓冲区：

```cpp
FixedBuffer buf = {1, 2, 3, 4, 5};  // FixedBuffer<int, 5>
```

推导指引的工作方式类似于函数模板重载解析。编译器会考虑所有推导指引（包括隐式生成的和用户自定义的），选择最匹配的那个。如果自定义推导指引比隐式推导指引更匹配，编译器会选择自定义的。

### 标准库中的自定义推导指引

标准库自己也大量使用了自定义推导指引。比如 `std::vector` 从迭代器对推导的指引：

```cpp
// 大致等价于标准库中的推导指引
template<typename InputIt>
vector(InputIt, InputIt) -> vector<typename iterator_traits<InputIt>::value_type>;
```

这个推导指引让 `std::vector v(it1, it2)` 能正确推导元素类型，而不是试图把迭代器类型当作元素类型。

------

## CTAD 的限制与陷阱

### 聚合类型在 C++17 中不支持 CTAD

C++17 的 CTAD 不支持聚合类型（aggregate types）。聚合类型是指没有用户声明构造函数、没有私有/保护成员、没有基类的类。像 `std::array` 的底层就是一个聚合，它之所以支持 CTAD 是因为标准库专门为它写了推导指引。

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];
    // 没有构造函数——是聚合类型
};

MyArray a = {1, 2, 3};  // C++17：编译错误！聚合不支持 CTAD
```

### C++20：聚合 CTAD

C++20 终于让聚合类型也能享受 CTAD 了。编译器会根据聚合初始化的元素自动推导模板参数：

```cpp
template<typename T, std::size_t N>
struct MyArray {
    T data[N];
};

MyArray a = {1, 2, 3};  // C++20：推导为 MyArray<int, 3>
```

这个改进让很多轻量级的数据结构不再需要手写推导指引。

### 别名模板不支持 CTAD

你不能直接用别名模板来推导参数——别名模板不是类模板，CTAD 只适用于类模板：

```cpp
template<typename T>
using MyVec = std::vector<T, MyAllocator<T>>;

MyVec v = {1, 2, 3};  // 编译错误：别名模板不支持 CTAD
```

C++20 引入了别名模板的推导指引支持，但规则比较复杂，很多编译器的支持也不完善。

### 转发引用和 CTAD

当构造函数接受转发引用时，CTAD 可能推导出意想不到的类型。因为转发引用可以匹配任何类型，包括引用类型：

```cpp
template<typename T>
struct Wrapper {
    Wrapper(T&& val) : value_(std::forward<T>(val)) {}
    T value_;
};

int x = 42;
Wrapper w(x);  // T 推导为 int&（不是 int！）
```

这里 `T&&` 在转发引用的规则下，当传入左值 `x` 时 `T` 被推导为 `int&`。所以 `Wrapper w(x)` 的类型是 `Wrapper<int&>`，其成员 `value_` 的类型是 `int&`。这可能不是你想要的行为。解决方法是使用 `std::remove_reference_t` 或自定义推导指引来约束推导结果。

### 拷贝初始化 vs 直接初始化

CTAD 在拷贝初始化（`=`）和直接初始化（`()`) 中的行为可能不同：

```cpp
std::vector v1{1, 2, 3};        // 直接初始化，CTAD 工作
std::vector v2 = {1, 2, 3};     // 拷贝初始化，CTAD 工作（有专门的推导指引）

// 某些自定义类型可能只在其中一种情况下工作
```

建议：如果你遇到 CTAD 在某种初始化方式下不工作的情况，尝试换成另一种。或者检查你的推导指引是否覆盖了这种初始化方式。

------

## 实战：强类型包装器的推导指引

让我们写一个完整的示例，展示 CTAD 如何让强类型包装器用起来更自然。

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

这个例子展示了 CTAD 的设计哲学：对于已经通过 `using` 定义了别名的类型（如 `Meter`），直接用别名构造就好，不需要 CTAD。CTAD 更多用于那些模板参数能从构造函数参数自然推导的场景。

------

## 小结

CTAD 是 C++17 中一个实用的"减少样板代码"的特性。它让类模板的实例化更接近普通类的使用方式。标准库中的 `pair`、`tuple`、`vector`、`array`、`optional`、`lock_guard` 等都支持 CTAD，日常开发中已经非常够用。

核心要点有三条：第一，隐式推导指引从构造函数自动生成，覆盖了大部分场景；第二，当隐式推导不够用时，可以写自定义推导指引来扩展推导行为；第三，C++20 为聚合类型添加了 CTAD 支持，补上了 C++17 的一个缺口。

需要注意的限制包括：C++17 中聚合类型不支持 CTAD、别名模板不支持 CTAD、转发引用可能导致意外的引用类型推导。这些坑只要知道就好，遇到的时候能快速定位。

## 参考资源

- [cppreference: Class template argument deduction](https://en.cppreference.com/w/cpp/language/class_template_argument_deduction)
- [CTAD in C++17 - Simon Toth](https://medium.com/@simontoth/daily-bit-e-of-c-class-template-argument-deduction-ctad-f0886131c129)
- [C++17's CTAD - Andreas Fertig](https://andreasfertig.com/blog/2022/11/cpp17s-ctad-a-sometimes-underrated-feature/)
