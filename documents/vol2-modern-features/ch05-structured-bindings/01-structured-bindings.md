---
title: "结构化绑定：一行解包多个值"
description: "用结构化绑定优雅地解包 pair、tuple、数组和结构体"
chapter: 5
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 4: std::variant"
  - "Chapter 4: std::optional"
related:
  - "if/switch 初始化器"
---

# 结构化绑定：一行解包多个值

笔者在写代码的时候，经常遇到一个很别扭的场景：函数返回了多个值，你得一个个拆开赋给变量。用 `pair` 的时候写 `result.first`、`result.second`，用 `tuple` 的时候写 `std::get<0>(t)` —— 要么语义不明，要么写法丑陋。C++11 引入了 `std::tie` 来缓解这个问题，但老实说，那语法也不算优雅：你得先声明好所有变量，再用 `tie` 往里塞。有没有那种,跟Python返回多个值的拆分写法一样爽的feature呢? 真有了孩子们!

C++17 终于给了我们一个真正的答案——结构化绑定（Structured Binding）。一行代码把 `pair`、`tuple`、数组、结构体全部拆开，直接拿到有名字的变量，语义清晰、零开销。

> 一句话总结：**结构化绑定让你把复合类型"解包"到多个命名变量中，编译器在幕后帮你完成一切。**

------

## 第一步——绑定 pair 和 tuple

### pair：最常见的多返回值

`std::pair` 是标准库中最常见的"打包两个值"的方式。`std::map::insert` 返回一个 `pair<iterator, bool>`，`std::map::find` 返回一个 `pair<const Key, Value>&`。在结构化绑定出现之前，我们只能这样写：

```cpp
auto result = m.insert({1, "one"});
if (result.second) {
    std::cout << "Inserted: " << result.first->second << '\n';
}
```

`result.second` 是什么意思？不查文档你根本不知道。结构化绑定直接把语义写进变量名里：

```cpp
auto [it, inserted] = m.insert({1, "one"});
if (inserted) {
    std::cout << "Inserted: " << it->second << '\n';
}
```

在范围 for 中遍历 map 的时候更是优雅到不行。以前写 `it->first`、`it->second`，现在直接 `[key, value]`：

```cpp
std::map<int, std::string> sensor_names = {
    {1, "Temperature"},
    {2, "Humidity"},
    {3, "Pressure"}
};

for (const auto& [id, name] : sensor_names) {
    std::cout << "Sensor " << +id << ": " << name << '\n';
}
```

> 为什么要写 `+id`？因为 `uint8_t` 的 `operator<<` 会把它当字符输出，`+` 会做整型提升（integral promotion），强制转换成 `int` 再打印。

### tuple：超过两个值的情况

当函数需要返回三个或更多值时，`std::tuple` 是自然的选择。结构化绑定的写法和 pair 完全一致：

```cpp
std::tuple<int, std::string, double> query_database(int id) {
    return {id, "sensor_" + std::to_string(id), 23.5};
}

auto [record_id, name, value] = query_database(42);
```

### 与 std::tie 的对比

C++11 的 `std::tie` 也能做类似的事情，但体验差了不少。它要求先声明好所有变量，然后用 `tie` 往里面赋值：

```cpp
int record_id;
std::string name;
double value;
std::tie(record_id, name, value) = query_database(42);
```

对比一下就很明显了：结构化绑定的变量声明和解包一步到位，而 `std::tie` 得拆成两步。虽然 `std::tie` 内部用的是引用，实际上它也能处理含有不可拷贝类型（如 `std::unique_ptr`）的 tuple——因为引用绑定不涉及拷贝。但结构化绑定的语法更简洁，而且支持按值、按引用、按转发引用等多种语义。

------

## 第二步——绑定原生数组和结构体

### 原生数组

固定大小的原生数组也能直接解包。这在处理一些固定格式的数据时非常方便：

```cpp
int rgb[3] = {255, 128, 0};
auto [r, g, b] = rgb;
```

二维数组的每一行也可以在循环中解包：

```cpp
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
for (auto& row : matrix) {
    auto [a, b, c] = row;
    std::cout << a << ' ' << b << ' ' << c << '\n';
}
```

注意结构化绑定只支持一维数组的直接解包。你不能写 `auto [a, b, c, d, e, f] = matrix`，因为 `matrix` 本质上是 `int[2][3]`，大小是 2 而不是 6。

### 结构体和类

如果结构体的所有非静态数据成员都是 `public` 的，它就能直接被结构化绑定解包。编译器会按声明顺序依次绑定：

```cpp
struct SensorReading {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
    bool is_valid;
};

SensorReading reading{5, 23.5f, 1234567890, true};
auto [id, val, ts, valid] = reading;
```

这恐怕是结构化绑定最直观的用法了。你甚至不需要理解任何模板元编程，只要结构体成员是公有的就能用。

结构化绑定要求数据成员按声明顺序绑定，且完全支持位域（bit field）。如果结构体里有 `mutable` 成员，行为可能需要注意：绑定到的"匿名变量"可能被 `const` 修饰，但 `mutable` 成员不受此限制，仍可修改。

------

## 第三步——理解绑定的三种语义

结构化绑定并不总是拷贝。实际上，`auto` 前面的修饰符决定了底层匿名变量的类型：

- **`auto [...]`**——按值拷贝。绑定变量引用的是这个拷贝。
- **`auto& [...]`**——绑定到左值引用。可以修改原对象。
- **`const auto& [...]`**——绑定到 const 左值引用。只读访问，不拷贝。
- **`auto&& [...]`**——转发引用。既能绑定左值也能绑定右值。

来看一个例子来区分它们：

```cpp
std::pair<int, int> range{1, 10};

// 拷贝：r1、r2 引用的是匿名拷贝，不影响 range
auto [r1, r2] = range;

// 引用：直接操作原对象
auto& [r3, r4] = range;
r3 = 5;  // range.first 变成 5
```

底层机制是这样的：编译器先声明一个匿名变量（类型由 `auto`/`auto&`/`const auto&`/`auto&&` 决定），用右侧表达式初始化它。然后每个绑定变量都是这个匿名变量的成员的引用（或者对于按值情况，是指向拷贝的成员的引用）。

```cpp
// auto [x, y] = get_point(); 大致等价于：
auto __anonymous = get_point();
auto& x = __anonymous.first;   // 引用匿名变量的成员
auto& y = __anonymous.second;
```

这意味着绑定变量本身永远是引用——它们引用的是那个隐藏的匿名对象的成员。你没法拿到"绑定变量本身"的地址，只能拿到它所引用的子对象的地址。

⚠️ 注意：`auto&` 要求右侧是左值。如果右侧是临时对象（比如 `std::make_pair(1, 2)` 的返回值），`auto&` 会编译失败，因为非 const 引用不能绑定到右值。这时应该用 `const auto&` 或直接 `auto` 按值拷贝。

```cpp
// 错误：auto& 不能绑定到临时对象
auto& [x, y] = std::make_pair(1, 2);

// 正确：const 引用可以延长临时对象生命周期
const auto& [x, y] = std::make_pair(1, 2);

// 或者直接拷贝
auto [x, y] = std::make_pair(1, 2);
```

------

## 第四步——自定义类型的绑定支持（Tuple-Like Protocol）

如果你的类有私有成员，不能直接用结构体方式解绑。但 C++ 提供了另一条路：让编译器把你的类当作 "tuple-like" 类型来处理。只需要三样东西：

1. 特化 `std::tuple_size<YourType>`，告诉编译器有多少个元素。
2. 特化 `std::tuple_element<I, YourType>`，告诉编译器第 `I` 个元素的类型。
3. 在 `YourType` 的命名空间中提供 `get<I>()` 函数，返回第 `I` 个元素。

```cpp
#include <utility>
#include <cstdint>

class SensorData {
public:
    SensorData(uint8_t id, float value) : id_(id), value_(value) {}

    template<std::size_t I>
    auto& get() {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

    template<std::size_t I>
    const auto& get() const {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

private:
    uint8_t id_;
    float value_;
};

// 特化 tuple_size：告诉编译器有 2 个元素
template<>
struct std::tuple_size<SensorData> : std::integral_constant<std::size_t, 2> {};

// 特化 tuple_element：告诉编译器每个元素的类型
template<>
struct std::tuple_element<0, SensorData> { using type = uint8_t; };

template<>
struct std::tuple_element<1, SensorData> { using type = float; };
```

现在就可以愉快地解包了：

```cpp
SensorData data{5, 23.5f};
auto [id, value] = data;    // id = 5, value = 23.5
```

> 这里的关键是 `get<I>()` 函数必须定义在类所在的命名空间中（ADL 规则），这样编译器才能找到它。对于标准命名空间 `std` 中的特化，你需要在 `std` 命名空间中写 `tuple_size` 和 `tuple_element` 的特化，但 `get` 函数放在类所在的命名空间即可。

这套机制被称为 "tuple-like protocol"，标准库的 `std::pair`、`std::tuple`、`std::array` 都是靠它实现结构化绑定支持的。

------

## C++20 的增强

C++20 对结构化绑定做了一些增强，主要与 constexpr 上下文相关。

结构化绑定可以在 `constexpr` 函数内部使用，这意味着编译期计算函数也能返回多值并用结构化绑定接收：

```cpp
constexpr auto get_point() {
    return std::make_pair(3, 4);
}

constexpr bool test_structured_binding() {
    auto [x, y] = get_point();
    return x == 3 && y == 4;
}

static_assert(test_structured_binding());
```

不过要注意，你不能在命名空间作用域直接声明 `constexpr` 的结构化绑定（比如 `constexpr auto [x, y] = get_point();` 是编译错误的）。这是因为结构化绑定本质上是一组引用变量的声明，而不是单个变量声明。

在 lambda 捕获方面，C++17 其实就支持直接捕获结构化绑定变量。下面的代码在 C++17 中就能工作：

```cpp
std::map<int, std::string> m = {{1, "one"}, {2, "two"}};

for (const auto& [k, v] : m) {
    auto callback = [k, v] {  // C++17 就支持直接捕获
        std::cout << k << ": " << v << '\n';
    };
    callback();
}
```

C++20 新增的是初始化捕获语法（`key = k`），这在某些情况下更灵活。但需要注意，`[=]` 默认捕获不会自动捕获结构化绑定变量，你需要显式列出它们。

------

## 性能：零开销的语法糖

结构化绑定本身没有任何运行时开销。它纯粹是编译期的语法变换——编译器会在幕后创建匿名变量，然后让绑定变量引用匿名变量的成员。生成的汇编代码和你手写的"取出成员再赋值"完全一致。

```cpp
// 这两种写法生成的汇编代码完全一样
auto [x, y] = get_point();

// 等价于
auto __tmp = get_point();
auto x = __tmp.first;
auto y = __tmp.second;
```

性能方面的建议很简单：对于大结构体用 `const auto&` 避免拷贝，对于小型类型（内置类型、小 struct）直接用 `auto` 按值拷贝。`auto&&` 在泛型代码中很有用，但在具体类型已知的场景下，明确写 `auto` 或 `const auto&` 更清晰。

------

## 常见的坑

### 生命周期问题

`auto&&` 绑定到临时对象时，匿名变量的生命周期会被延长到绑定变量的作用域结束，所以用 `auto&&` 或 `const auto&` 是安全的。但如果你拿到了绑定变量的指针或引用然后传出去，就有悬空风险：

```cpp
const auto& [x, y] = std::make_pair(1, 2);
// x, y 在这个作用域内有效，安全
// 但如果 &x 被存到外部，作用域结束后就悬空了
```

### 不能直接当返回值

结构化绑定的变量名不能直接用于函数返回。如果你想返回解包后的值，需要重新打包：

```cpp
auto [x, y] = get_point();
// 不能 return x, y; 必须重新打包
return std::make_pair(x, y);

// 或者直接返回函数结果
return get_point();
```

### 不能用于类成员声明

你不能在类的成员声明中使用结构化绑定：

```cpp
class MyClass {
    auto [x, y] = get_point();  // 编译错误
};
```

如果你需要存储解包后的值，用结构体或者 `pair`/`tuple` 成员代替。

------

## 小结

结构化绑定是 C++17 中最实用的特性之一。它支持的类型覆盖了日常开发的绝大部分场景：`pair`、`tuple`、原生数组、公有成员结构体，以及实现了 tuple-like protocol 的自定义类型。绑定的语义完全由 `auto` 前面的修饰符决定——`auto` 是拷贝，`auto&` 是引用，`const auto&` 是只读引用，`auto&&` 是转发引用。

在实战中，笔者最常用的是在范围 for 中遍历 map（`for (const auto& [k, v] : m)`）和处理多返回值函数。配合下一章要讲的 if/switch 初始化器，结构化绑定能让代码的简洁度和可读性都上一个台阶。

## 参考资源

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
