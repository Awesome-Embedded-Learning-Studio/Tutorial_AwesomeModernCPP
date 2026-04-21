---
title: "std::variant：类型安全的联合体"
description: "用 variant 替代 union，配合 visit 实现类型安全的多态"
chapter: 4
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
  - variant
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 18
prerequisites:
  - "Chapter 3: Lambda 基础"
  - "Chapter 4: enum class"
related:
  - "std::optional"
  - "错误处理的现代方式"
---

# std::variant：类型安全的联合体

## 引言

笔者早年在做协议解析的时候，大量使用 `union` 来处理"同一块内存里有时存 `int`、有时存 `float`、有时存字符串指针"的场景。当时觉得自己很聪明——省内存嘛。直到有一次，把一个 `int` 当成 `float` 来读了，程序在某个特定输入下开始输出荒谬的数值，笔者花了整整一天才找到这个 bug。从那以后，笔者对裸 `union` 的态度就变成了：能不用就不用。

`std::variant`（C++17 引入）就是 `union` 的现代替代品。它解决的核心问题是：如何在"同一时刻只持有多种类型之一"这个约束下，保证类型安全。和裸 `union` 不同，`variant` 知道自己当前持有的是什么类型，会在你访问时进行检查，并且正确管理所持有对象的生命周期。这一章我们从 `union` 的痛点开始，一步步把 `variant` 的机制和用法搞清楚。

## 第一步——union 的致命缺陷

在讲 `variant` 之前，我们先来看看裸 `union` 为什么不安全。

```cpp
union Data {
    int i;
    float f;
    char* s;
};

Data d;
d.i = 42;
// 现在 d.f 是什么？没人知道——因为 union 不知道你上次写的是哪个成员
std::cout << d.f << "\n";  // UB（未定义行为）：把 int 的位模式当 float 读
```

这段代码的问题在于：`union` 本身**不记录**当前持有的是哪个成员。程序员必须自己维护一个"标签"来跟踪当前活跃成员。如果你忘了更新标签，或者标签和实际状态不一致，就会触发未定义行为。

更严重的是，`union` **不支持带有非平凡构造/析构函数的类型**。比如 `std::string` 就不能直接放在 `union` 里——你必须手动调用 placement new 来构造，手动调用析构函数来销毁。这种手工管理既繁琐又容易出错。

```cpp
union BadUnion {
    int i;
    std::string s;  // 编译能通过（C++11 起允许），但你必须手动管理生命周期
};

BadUnion u;
// u.s = "hello";  // UB！没有先构造 s
new (&u.s) std::string("hello");  // placement new
// ... 用完后必须手动析构
u.s.~basic_string();
```

说实话，每次写这种代码笔者都觉得像是在走钢丝——漏掉任何一步都是资源泄漏或者更糟的后果。`std::variant` 的出现让这一切变得完全不需要手工管理。

## 第二步——variant 的基本用法

### 构造与赋值

`std::variant<Types...>` 可以在同一时刻持有 `Types...` 中**恰好一种**类型的值。默认构造时，它会构造第一个备选类型（除非你用 `std::monostate` 占位）：

```cpp
#include <variant>
#include <string>
#include <iostream>

int main()
{
    // 默认构造：持有 int（第一个备选），值为 0
    std::variant<int, double, std::string> v;

    // 赋值：自动切换到对应类型
    v = 42;                        // 持有 int
    v = 3.14;                      // 持有 double
    v = std::string("hello");      // 持有 std::string

    // 构造时直接指定
    std::variant<int, std::string> v2 = std::string("world");
}
```

每次赋值时，`variant` 会自动销毁旧值、构造新值。你不需要手动管理任何生命周期——这一切都是由 `variant` 的内部机制自动完成的。

### 访问值

访问 `variant` 中的值有三种主要方式：

```cpp
std::variant<int, double, std::string> v = 3.14;

// 方式一：std::get<T> —— 类型不匹配时抛出 std::bad_variant_access
double d = std::get<double>(v);   // OK
// int bad = std::get<int>(v);    // 抛出异常！

// 方式二：std::get_if<T> —— 不抛异常，返回指针
if (auto* ptr = std::get_if<double>(&v)) {
    std::cout << "double: " << *ptr << "\n";
}

// 方式三：std::holds_alternative<T> —— 只检查类型
if (std::holds_alternative<double>(v)) {
    std::cout << "it's a double\n";
}
```

笔者推荐的做法是：如果你只是需要检查类型，用 `std::holds_alternative`；如果你需要获取值的指针（且不想处理异常），用 `std::get_if`；如果你确定类型是对的并且希望不匹配时立刻报错，用 `std::get`。

## 第三步——std::visit 与访问者模式

`std::visit` 是 `variant` 最核心的访问机制。它接受一个可调用对象（visitor）和若干个 `variant` 对象，根据 `variant` 当前持有的类型来分派调用。这比 `switch-case` 更安全，因为编译器会检查你是否处理了所有备选类型。

### 使用 lambda 的简单 visit

```cpp
std::variant<int, double, std::string> v = std::string("hello");

std::visit([](auto&& arg) {
    std::cout << arg << "\n";
}, v);
```

这里 `auto&&` 是一个万能引用（forwarding reference），`visit` 会根据 `v` 当前持有的类型来实例化这个 lambda。当你只需要对所有类型执行同一种操作时，这种写法非常简洁。

### 重载集合：处理不同类型

更常见的场景是：不同类型需要不同的处理逻辑。这时候我们需要一个"重载集合"——一个对每种备选类型都有对应重载的可调用对象。C++17 中有一个经典的技巧来实现它：

```cpp
// 重载集合工具（C++17 惯用法）
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// C++17 推导指引
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
```

这个 `Overloaded` 把多个 lambda 的 `operator()` 全部"继承"到一起，形成一个对多种类型都有重载的可调用对象。使用时：

```cpp
std::variant<int, double, std::string> v = 3.14;

std::visit(Overloaded{
    [](int i)         { std::cout << "int: " << i << "\n"; },
    [](double d)      { std::cout << "double: " << d << "\n"; },
    [](const std::string& s) { std::cout << "string: " << s << "\n"; }
}, v);
```

编译器会检查你的 `Overloaded` 是否覆盖了 `variant` 的所有备选类型。如果你漏掉了某个类型的处理，编译器会直接报错——这就是编译期类型安全的体现。在 C++20 中，你甚至不需要手写 `Overloaded`——标准库直接支持了带多个 lambda 的 visit 模式（不过正式的支持方式还在演进中）。

### 返回值的 visit

`visit` 的 visitor 也可以返回值。所有 lambda 的返回类型必须兼容（能转换为同一个类型）：

```cpp
std::variant<int, double, std::string> v = 42;

auto type_name = std::visit(Overloaded{
    [](int)    -> std::string { return "int"; },
    [](double) -> std::string { return "double"; },
    [](const std::string&) -> std::string { return "string"; }
}, v);

std::cout << "type is: " << type_name << "\n";  // "type is: int"
```

## 第四步——variant 替代运行时多态

`variant` 的一个重要用途是替代虚函数实现的多态（这被称为"闭式层次结构"或者"基于 visit 的多态"）。传统的虚函数多态需要堆分配、虚函数表指针、引用语义——而 `variant` 可以直接在栈上存储值，没有虚函数调用开销。

```cpp
#include <variant>
#include <iostream>
#include <memory>
#include <vector>

// ---- 方式一：传统虚函数多态 ----
struct ShapeBase {
    virtual ~ShapeBase() = default;
    virtual double area() const = 0;
};

struct CircleV : ShapeBase {
    double radius;
    explicit CircleV(double r) : radius(r) {}
    double area() const override { return 3.14159 * radius * radius; }
};

struct RectangleV : ShapeBase {
    double width, height;
    RectangleV(double w, double h) : width(w), height(h) {}
    double area() const override { return width * height; }
};

// ---- 方式二：variant + visit ----
struct Circle {
    double radius;
    explicit Circle(double r) : radius(r) {}
};

struct Rectangle {
    double width, height;
    Rectangle(double w, double h) : width(w), height(h) {}
};

using Shape = std::variant<Circle, Rectangle>;

double area(const Shape& s)
{
    return std::visit(Overloaded{
        [](const Circle& c)    { return 3.14159 * c.radius * c.radius; },
        [](const Rectangle& r) { return r.width * r.height; }
    }, s);
}
```

使用对比：

```cpp
// 虚函数方式：需要指针/引用，需要堆分配
std::vector<std::unique_ptr<ShapeBase>> shapes_v;
shapes_v.push_back(std::make_unique<CircleV>(5.0));
shapes_v.push_back(std::make_unique<RectangleV>(3.0, 4.0));

for (const auto& s : shapes_v) {
    std::cout << s->area() << "\n";
}

// variant 方式：值语义，栈上存储
std::vector<Shape> shapes;
shapes.push_back(Circle(5.0));
shapes.push_back(Rectangle(3.0, 4.0));

for (const auto& s : shapes) {
    std::cout << area(s) << "\n";
}
```

`variant` 方式的优势在于：值语义（不需要 `new`/`delete`）、连续内存（`vector` 中直接存储，缓存友好）、编译期类型检查（所有 `visit` 的分支都在编译期确定）。但它也有代价：每新增一种形状，你必须修改 `Shape` 的 `variant` 定义——这在某些场景下是不灵活的。如果你的类型层次是"开放"的（第三方可以扩展新类型），虚函数仍然是更好的选择。

## 第五步——异常安全与 valueless_by_exception

`variant` 有一个比较特殊的状态叫做 `valueless_by_exception`。当 `variant` 在切换类型的过程中（比如赋值、`emplace`），新类型的构造函数抛出了异常，而旧值已经被销毁了，`variant` 就会进入这个"无值"状态。

```cpp
struct ThrowingType {
    ThrowingType() { throw std::runtime_error("construction failed"); }
};

std::variant<int, ThrowingType> v = 42;
try {
    v = ThrowingType();  // 旧值（42）被销毁，新值构造抛异常
} catch (const std::runtime_error&) {
    // v 现在是 valueless_by_exception 状态
    std::cout << "valueless: " << v.valueless_by_exception() << "\n";  // true
}
```

在这个状态下，`std::visit` 会抛出 `std::bad_variant_access`，`std::get` 也会抛异常。所以如果你的代码中 `variant` 可能遇到这种情况，最好在访问前检查一下。

⚠️ 实际上，在正常使用中 `valueless_by_exception` 极少出现。它只在"构造新值时抛异常"这个特定场景下才会触发。如果你所有的备选类型的构造函数都是 `noexcept` 的（或者你不用异常），那就完全不用担心这个状态。

## 实战应用——消息类型系统

`variant` 最适合的场景之一是消息传递系统。在事件驱动架构中，消息队列里的消息可能有多种类型，每种类型的载荷（payload）不同。`variant` + `visit` 可以非常优雅地处理这种模式：

```cpp
#include <variant>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <queue>

// 消息类型定义
struct Heartbeat {
    uint32_t source_id;
};

struct TextMessage {
    uint32_t source_id;
    std::string content;
};

struct DataPacket {
    uint32_t source_id;
    std::vector<uint8_t> payload;
};

struct Disconnect {
    uint32_t source_id;
    std::string reason;
};

using Message = std::variant<Heartbeat, TextMessage, DataPacket, Disconnect>;

// 消息处理器
class MessageHandler {
public:
    void on_message(const Message& msg)
    {
        std::visit([this](auto&& m) { handle(m); }, msg);
    }

    void process_queue()
    {
        while (!queue_.empty()) {
            on_message(queue_.front());
            queue_.pop();
        }
    }

    void push(Message msg) { queue_.push(std::move(msg)); }

private:
    std::queue<Message> queue_;

    void handle(const Heartbeat& h)
    {
        std::cout << "Heartbeat from " << h.source_id << "\n";
    }

    void handle(const TextMessage& t)
    {
        std::cout << "Text from " << t.source_id << ": " << t.content << "\n";
    }

    void handle(const DataPacket& d)
    {
        std::cout << "Data from " << d.source_id
                  << ", size=" << d.payload.size() << "\n";
    }

    void handle(const Disconnect& dc)
    {
        std::cout << "Disconnect from " << dc.source_id
                  << ": " << dc.reason << "\n";
    }
};
```

这段代码的好处是：如果你新增了一种消息类型（比如 `FileTransfer`），编译器会在 `Overloaded` 的 `visit` 调用处直接报错——你必须在 `handle` 中新增对应的重载。这种"新增类型时编译器帮你找到所有需要修改的地方"的能力，是 `variant` 相比 `switch-case` 或虚函数最大的优势之一。

## 实战应用——配置值与 AST 节点

### 配置值

配置系统经常需要存储不同类型的值：整数、浮点数、字符串、布尔值。`variant` 天然适合：

```cpp
using ConfigValue = std::variant<int, double, std::string, bool>;

struct ConfigEntry {
    std::string key;
    ConfigValue value;
};

// 读取配置
ConfigValue parse_value(const std::string& s)
{
    // 尝试解析为 int
    try {
        std::size_t pos;
        int i = std::stoi(s, &pos);
        if (pos == s.size()) return i;
    } catch (...) {}

    // 尝试解析为 double
    try {
        std::size_t pos;
        double d = std::stod(s, &pos);
        if (pos == s.size()) return d;
    } catch (...) {}

    // 尝试解析为 bool
    if (s == "true")  return true;
    if (s == "false") return false;

    // 默认作为字符串
    return s;
}
```

### AST 节点

在编译器或解释器的前端中，抽象语法树（AST）的节点类型也天然适合用 `variant` 表示：

```cpp
struct NumberLiteral { double value; };
struct StringLiteral { std::string value; };
struct BinaryExpr;
struct UnaryExpr;

using Expr = std::variant<
    NumberLiteral,
    StringLiteral,
    std::unique_ptr<BinaryExpr>,
    std::unique_ptr<UnaryExpr>
>;

struct BinaryExpr {
    Expr left;
    std::string op;
    Expr right;
};

struct UnaryExpr {
    std::string op;
    Expr operand;
};
```

⚠️ 注意这里使用了 `std::unique_ptr<BinaryExpr>` 而不是直接的 `BinaryExpr`，因为 `variant` 不能直接包含不完整类型。递归数据结构必须通过指针（或 `std::unique_ptr`）来打破循环依赖。

## 内存布局与性能考量

`variant` 的大小等于"最大备选类型的大小"加上一个小的元数据字段（用于记录当前持有的类型索引）。这意味着即使你当前只持有一个 `int`，`variant<int, std::string>` 也至少有 `sizeof(std::string) + sizeof(size_t)` 那么大。

```cpp
std::cout << "sizeof(variant<int, double, string>): "
          << sizeof(std::variant<int, double, std::string>) << "\n";
// 典型输出：32（64 位平台上，string 占 32 字节，int 和 double 各 8 字节）
std::cout << "sizeof(string): " << sizeof(std::string) << "\n";
// 典型输出：32
```

这个大小对于大多数应用来说完全可接受。但在内存极端受限的嵌入式场景中，你可能需要评估一下是否值得用 `variant` 替代手写的 `union` + `enum` 标签方案。`variant` 带来的类型安全收益通常远大于几个字节的内存开销。

## 小结

`std::variant` 是 C++17 中最重要的类型安全工具之一。它解决了裸 `union` 的三个核心问题：不知道当前持有什么类型（通过内部标签解决）、不会管理对象生命周期（自动调用构造/析构函数）、不支持非平凡类型（没有任何限制）。

`std::visit` 是 `variant` 的核心访问机制，配合 `Overloaded` 惯用法可以实现类型安全的模式匹配。当你的类型集合是有限且已知的（消息类型、配置值、AST 节点等），`variant` 比虚函数更高效、更安全。但如果类型集合是开放的（第三方可以扩展），虚函数仍然是更合适的选择。

`valueless_by_exception` 是一个需要了解但通常不用担心的问题——它只在构造新值时抛异常的极端场景下出现。了解这个状态的存在就够了，在实际代码中不必为此过度防御。

下一篇我们要讨论的 `std::optional`，可以看作 `variant` 的一个特例——当你的"类型集合"只有两种可能（"有值"和"没有值"）时，`optional` 就是更简洁的选择。

## 参考资源

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit)
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access)
- [C++ Core Guidelines: C++ union](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c181-prefer-using-variant-over-union)
