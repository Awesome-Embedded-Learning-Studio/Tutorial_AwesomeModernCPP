---
title: "std::optional：优雅表达'可能没有值'"
description: "用 optional 替代特殊值和裸指针，安全表达可选语义"
chapter: 4
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - optional
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [17, 23]
reading_time_minutes: 15
prerequisites:
  - "Chapter 4: std::variant"
related:
  - "错误处理的现代方式"
---

# std::optional：优雅表达"可能没有值"

## 引言

笔者写过太多这样的代码了：函数返回 `-1` 表示"没找到"，返回 `nullptr` 表示"出错了"，返回空字符串表示"配置项不存在"。这些约定在写的时候觉得理所当然，三个月后回头看就开始冒冷汗——`-1` 到底是"没找到"还是"真的返回了 -1"？`nullptr` 是"可选的空值"还是"出错了"？每一个返回特殊值的函数都在给未来的自己埋雷。

`std::optional`（C++17 引入）就是来解决"如何安全表达可能没有值"这个问题的。它把"有值还是没值"这个信息编码进了类型系统——编译器和调用方都能从函数签名直接看到"这个返回值可能为空"，不需要靠注释或文档来传达。

## 第一步——"可能没有值"的传统方案

在 `optional` 出现之前，C++ 程序员主要有以下几种方式来表达"可能没有值"：

**特殊值（哨兵值）**：用某个特定的值来表示"无效"。`-1` 表示查找失败，`UINT_MAX` 表示无效索引，空字符串表示未配置。问题在于：每个函数的"特殊值"都不一样，调用方必须记住这些约定。而且有些类型根本找不到合适的特殊值——比如 `double` 的 `-1.0` 完全可能是一个合法的返回值。

**裸指针**：返回 `nullptr` 表示"没值"。这在查找函数中很常见。问题在于：指针的语义太宽泛了。`T*` 可以表示"可能为空的可选值"，也可以表示"不拥有所有权的观察指针"，还可以表示"指向动态分配的对象"。调用方无法从类型上区分这些语义。更危险的是，解引用空指针是 UB，不会给你任何友好的错误提示。

**std::pair<T, bool>**：第二个元素表示"值是否有效"。这比前两种方案好一点，但使用起来很啰嗦——每次都要检查 `.second`，而且 `first` 在 `second == false` 时的值是未定义的（默认构造可能不合法）。

```cpp
// 三种传统方案对比
int find_index_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return i;
    }
    return -1;  // 特殊值约定：调用方必须记住 -1 表示没找到
}

int* find_ptr_old(std::vector<int>& v, int target)
{
    for (auto& x : v) {
        if (x == target) return &x;
    }
    return nullptr;  // 裸指针：语义不明确
}

std::pair<int, bool> find_pair_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return {i, true};
    }
    return {0, false};  // first 的值在此处无意义
}
```

这三种方案有一个共同的缺陷：**类型签名没有表达出"可能没有值"的语义**。`int` 的返回类型不会告诉你 `-1` 是特殊值，`int*` 不会告诉你 `nullptr` 代表"没找到"而不是"出错了"。`std::optional` 直接在类型层面解决了这个问题。

## 第二步——optional 的核心语义与 API

`std::optional<T>` 表示"要么持有一个 `T` 类型的值，要么什么都没有"。它是一个值类型（不是指针），持有的对象直接嵌套在 `optional` 内部的存储中——没有动态内存分配。

### 构造

```cpp
#include <optional>
#include <string>
#include <iostream>

std::optional<int> a;                      // 空（不持有值）
std::optional<int> b = 42;                 // 持有 42
std::optional<int> c = std::nullopt;       // 显式空
std::optional<std::string> d = "hello";    // 持有 "hello"

// 就地构造（避免临时对象）
std::optional<std::string> e(std::in_place, 10, 'x');  // "xxxxxxxxxx"
```

### 检查与访问

```cpp
std::optional<int> opt = 42;

// 检查是否有值
if (opt.has_value()) { /* ... */ }
if (opt) { /* ... */ }             // 等价的隐式 bool 转换

// 访问值
int x = *opt;                       // 解引用（未检查——空时是 UB）
int y = opt.value();                // 空时抛 std::bad_optional_access
int z = opt.value_or(0);            // 空时返回默认值 0

// 访问成员（对于类类型）
std::optional<std::string> name = "Alice";
if (name) {
    std::cout << "length: " << name->size() << "\n";  // operator->
}
```

⚠️ 关于 `operator*` 和 `value()` 的选择，笔者的建议是：在你**已经检查过** `has_value()` 的代码路径中，使用 `*opt` 就够了，性能更好而且语义清晰。在**没有检查**的情况下，`value()` 更安全——它会抛异常而不是 UB。但这两种方式都不如 `value_or()` 来得优雅，因为后者直接处理了"空值怎么办"的问题。

### value_or 的妙用

`value_or()` 是 `optional` 最实用的 API 之一。它接受一个默认值参数，如果 `optional` 有值就返回持有的值，否则返回默认值：

```cpp
std::optional<std::string> get_config(const std::string& key);

// 读取配置，未配置则使用默认值
std::string host = get_config("server_host").value_or("localhost");
int port = get_config("server_port")
    .transform([](const std::string& s) { return std::stoi(s); })
    .value_or(8080);
```

上面这个 `transform` 是 C++23 的新特性，我们稍后会详细介绍。

## 第三步——optional 的内存布局

`optional<T>` 的内部存储通常由两部分组成：一个用于存放 `T` 的对齐缓冲区，加上一个 `bool` 标志位表示是否有值。这意味着 `sizeof(std::optional<T>)` 通常大于 `sizeof(T)`。

```cpp
#include <optional>

std::cout << "sizeof(int):              " << sizeof(int) << "\n";            // 4
std::cout << "sizeof(optional<int>):    " << sizeof(std::optional<int>) << "\n";    // 典型：8
std::cout << "sizeof(double):           " << sizeof(double) << "\n";         // 8
std::cout << "sizeof(optional<double>): " << sizeof(std::optional<double>) << "\n"; // 典型：16
std::cout << "sizeof(string):           " << sizeof(std::string) << "\n";    // 典型：32
std::cout << "sizeof(optional<string>): " << sizeof(std::optional<std::string>) << "\n"; // 典型：40
```

实际的 `sizeof` 结果取决于标准库的实现和平台的对齐要求。但核心事实是：`optional<T>` 大约比 `T` 大一个对齐后的 `bool` 的大小。由于对齐的要求，有时候会增加得比预期多一些。这不是 `optional` 的设计缺陷——它是在栈上直接存储 `T` 的值，不涉及堆分配，所以这个额外开销是合理的。

`optional` 持有的对象和"是否有值"的标志在同一个对象内部，不涉及任何动态内存分配。析构时，如果 `optional` 持有值，就会自动调用 `T` 的析构函数。这一切都是自动的，不需要手工管理。

## 第四步——optional 与指针的区别

`optional<T>` 和 `T*` 都能表达"可能没有值"，但它们的语义截然不同。

`optional<T>` 是值语义——它持有（或打算持有）一个完整的 `T` 对象。拷贝 `optional` 会拷贝 `T` 的值（如果有值的话），析构 `optional` 会析构 `T`。它表达的是"这里有一个 `T`，或者暂时没有"。

`T*` 是引用语义——它指向某个外部的 `T` 对象（或者为空）。拷贝指针只是拷贝地址，不会拷贝对象本身。它表达的是"某个地方有一个 `T`，我可能指向它"。

```cpp
std::optional<int> opt = 42;
int* ptr = &opt.value();  // 指向 optional 内部的 int

opt = 123;                // optional 重新赋值，旧的 42 被销毁
// ptr 现在可能指向 123（取决于实现），也可能悬空——不要这么用

std::optional<int> opt2 = opt;  // 拷贝：opt2 是独立的副本，持有 123
int* ptr2 = &raw;               // 假设 raw 是某个 int 变量
std::optional<int> opt3 = *ptr2;  // 拷贝 ptr2 指向的值——与 ptr2 无关
```

笔者的一般原则是：**如果你需要表达"值可能存在也可能不存在"，用 `optional`；如果你需要表达"指向某个外部对象的可空引用"，用指针**。不要用 `optional` 来模拟指针，也不要用指针来模拟 `optional`——它们的职责不同。

## 第五步——optional 作为返回值

`optional` 最常见的用途是作为函数返回值。它的语义非常明确：函数可能返回一个有效值，也可能返回"无值"。调用方必须在类型系统层面处理"无值"的情况。

### 查找操作

```cpp
#include <optional>
#include <vector>
#include <string>

std::optional<std::size_t> find_index(
    const std::vector<int>& v, int target)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return i;
    }
    return std::nullopt;
}

// 调用方
auto idx = find_index(data, 42);
if (idx) {
    std::cout << "found at index " << *idx << "\n";
} else {
    std::cout << "not found\n";
}
```

对比之前用 `-1` 做哨兵值的版本，`optional` 的优势在于：调用方**不可能忘记**检查返回值。如果你直接写 `data[*find_index(data, 42)]` 而不检查 `has_value()`，在空值情况下解引用是 UB，但至少从 API 的设计意图上是明确的——类型签名已经告诉了你"这个值可能为空"。

### 工厂函数

```cpp
class Connection {
public:
    static std::optional<Connection> create(const std::string& addr)
    {
        // 尝试建立连接
        if (addr.empty()) return std::nullopt;  // 无效参数
        // ... 实际连接逻辑
        return Connection(addr);
    }

private:
    explicit Connection(std::string addr) : addr_(std::move(addr)) {}
    std::string addr_;
};

// 使用
auto conn = Connection::create("192.168.1.1");
if (conn) {
    // 连接成功
} else {
    // 连接失败
}
```

## 第六步——optional 作为参数

`optional` 也可以用作函数参数，表示"这个参数是可选的"。这比函数重载或者默认参数更灵活，因为调用方可以在运行时决定是否提供值：

```cpp
void print_greeting(const std::string& name,
                    std::optional<std::string> title = std::nullopt)
{
    if (title) {
        std::cout << "Hello, " << *title << " " << name << "!\n";
    } else {
        std::cout << "Hello, " << name << "!\n";
    }
}

print_greeting("Alice");                    // Hello, Alice!
print_greeting("Bob", std::string("Dr."));  // Hello, Dr. Bob!
```

不过笔者要提醒一点：不要过度使用 `optional` 参数。如果一个参数在大多数情况下都需要提供，那用默认值可能比 `optional` 更合适。`optional` 参数适合那种"有时有、有时没有，而且两种情况的含义完全不同"的场景。

## 第七步——C++23 monadic 操作预告

C++23 为 `std::optional` 引入了三个 monadic 操作：`and_then`、`transform` 和 `or_else`。这些操作借鉴了函数式编程的概念，让 `optional` 的链式处理变得更加优雅。

### transform：对值做变换

`transform` 接受一个函数，如果 `optional` 有值，就用这个函数对值做变换，返回一个包含变换结果的 `optional`；如果 `optional` 为空，返回一个空的 `optional`。

```cpp
std::optional<int> parse_int(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// C++20 风格：手动检查
std::optional<std::string> input = get_input();
std::optional<int> result;
if (input) {
    result = parse_int(*input);
}

// C++23 风格：链式 transform
auto result2 = get_input().transform([](const std::string& s) -> int {
    return std::stoi(s);  // 简化示例，实际应处理异常
});
```

### and_then：链式组合可能失败的操作

`and_then` 接受一个返回 `optional` 的函数。如果当前 `optional` 有值，就调用这个函数并返回其结果；否则直接返回空 `optional`。这比 `transform` 更适合"上一步的结果是下一步的输入，且每步都可能失败"的场景。

```cpp
std::optional<User> find_user(int id);
std::optional<std::string> get_email(const User& u);

// C++20 风格：嵌套 if
auto user = find_user(42);
if (user) {
    auto email = get_email(*user);
    if (email) {
        std::cout << "Email: " << *email << "\n";
    }
}

// C++23 风格：链式 and_then
find_user(42)
    .and_then(get_email)
    .transform([](const std::string& email) {
        std::cout << "Email: " << email << "\n";
        return email;
    });
```

### or_else：处理空值情况

`or_else` 接受一个函数，当 `optional` 为空时调用它。通常用于日志记录或提供替代方案：

```cpp
auto email = find_user(42)
    .and_then(get_email)
    .or_else([] {
        std::cerr << "Failed to get email\n";
        return std::optional<std::string>("fallback@example.com");
    });
```

这三个操作组合起来，可以让你写出非常流畅的链式代码，避免多层嵌套的 `if` 语句。如果你的编译器还不支持 C++23，可以参考之前的 `optional_map` 辅助函数来实现类似的效果。

## 实战应用——延迟初始化

`optional` 还可以用来实现延迟初始化（lazy initialization）：对象的构造推迟到真正需要的时候。这在对象构造代价较高、但"是否需要"在编译期无法确定的场景中非常有用：

```cpp
class ExpensiveResource {
public:
    ExpensiveResource() { /* 耗时的初始化 */ }
    void do_work() { /* ... */ }
};

class Service {
public:
    void process()
    {
        if (!resource_) {
            resource_.emplace();  // 首次使用时才构造
        }
        resource_->do_work();
    }

private:
    std::optional<ExpensiveResource> resource_;  // 初始为空
};
```

这比用 `std::unique_ptr` 实现延迟初始化更优，因为 `optional` 不涉及堆分配——对象直接存储在 `optional` 内部的缓冲区中。

## 嵌入式实战——配置项与传感器读取

在嵌入式系统中，传感器数据不一定每次都能成功读取（传感器可能未就绪、总线可能超时），配置项也不一定总是存在的。`optional` 可以优雅地表达这些"可能失败"的操作：

```cpp
#include <optional>
#include <cstdint>

struct SensorReading {
    float temperature;
    uint32_t timestamp;
};

class TemperatureSensor {
public:
    std::optional<SensorReading> read()
    {
        if (!is_ready()) return std::nullopt;

        SensorReading r;
        r.temperature = read_raw_value() * kScale;
        r.timestamp = get_tick();
        return r;
    }

private:
    bool is_ready();
    float read_raw_value();
    uint32_t get_tick();

    static constexpr float kScale = 0.0625f;
};

// 使用
void print_temperature(TemperatureSensor& sensor)
{
    auto reading = sensor.read();
    if (reading) {
        std::printf("Temp: %.1f C (at %u)\n",
                    reading->temperature,
                    static_cast<unsigned>(reading->timestamp));
    } else {
        std::printf("Sensor not ready\n");
    }
}
```

`optional` 在这个场景中的价值是：它把"读取失败"编码成了返回类型的一部分。调用方不可能忘记处理"读取失败"的情况——因为你必须先检查 `has_value()` 才能访问温度值。这比返回一个 `0.0f` 然后靠调用方"记住 0.0 可能表示失败"要安全得多。

## 小结

`std::optional` 是 C++17 中表达"可能没有值"的标准方式。它比哨兵值更安全（不会和合法值混淆），比裸指针语义更清晰（值语义 vs 引用语义），比 `std::pair<T, bool>` 更优雅（API 专门为此设计）。

`optional` 的核心 API 非常简洁：`has_value()` 检查、`operator*` 解引用、`value_or()` 提供默认值。它不涉及动态内存分配，对象直接存储在 `optional` 内部。C++23 的 `transform`、`and_then`、`or_else` 则为链式处理提供了更优雅的语法。

使用 `optional` 的关键原则是：用它表达"缺少值"的语义，而不是"出错了"的语义。如果你需要传递错误信息（错误码、错误描述），请使用 `std::expected`（C++23）或自定义的 `Result` 类型。`optional` 只负责"有还是没有"，不负责"为什么没有"。

下一篇我们要讨论的 `std::any`，和 `optional` 属于同一族——"可以持有某种值或什么都不持有"——但 `any` 的能力更强，代价也更大。

## 参考资源

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access)
- [C++23 Monadic operations for std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Optional](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
