---
title: 'std::variant: A type-safe union'
description: Replace union with variant, combined with visit to achieve type-safe
  polymorphism.
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
cpp_standard:
- 17
reading_time_minutes: 18
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 4: enum class'
related:
- std::optional
- 错误处理的现代方式
---
# std::variant: A Type-Safe Union

## Introduction

`std::variant` (introduced in C++17) is the modern replacement for `union`. The core problem it solves is how to guarantee type safety under the constraint of "holding exactly one of several types at any given time." Unlike a bare `union`, `variant` knows which type it currently holds, checks types when you access the value, and correctly manages the lifetime of the held object. In this chapter, we start with the pain points of `union` and work through the mechanisms and usage of `variant` step by step.

## Step 1 — The Fatal Flaws of union

Before diving into `variant`, let's look at why a bare `union` is unsafe.

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

The problem with this code is that `union` itself **does not track** which member is currently active. The programmer must maintain a separate "tag" to track the active member. If you forget to update the tag, or if the tag gets out of sync with the actual state, you trigger undefined behavior (UB).

Even worse, `union` **does not support types with non-trivial constructors or destructors**. For example, `std::string` cannot be placed directly inside a `union` — you must manually call placement new to construct it and manually call the destructor to destroy it. This kind of manual management is both tedious and error-prone.

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

Frankly, every time we write code like this, it feels like walking a tightrope — missing any single step leads to a memory leak or worse. The arrival of `std::variant` makes all of this completely unnecessary to manage by hand.

## Step 2 — Basic Usage of variant

### Construction and Assignment

`std::variant<Types...>` can hold a value of **exactly one** of the types in `Types...` at any given time. When default-constructed, it constructs the first alternative type (unless you use `std::monostate` as a placeholder):

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

On each assignment, `variant` automatically destroys the old value and constructs the new one. You don't need to manage any lifetimes manually — all of this is handled automatically by `variant`'s internal mechanisms.

### Accessing Values

There are three main ways to access the value inside a `variant`:

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

Our recommended approach is: if you only need to check the type, use `std::holds_alternative`; if you need a pointer to the value (and want to avoid exceptions), use `std::get_if`; if you are certain of the type and want an immediate error on mismatch, use `std::get`.

## Step 3 — std::visit and the Visitor Pattern

`std::visit` is the core access mechanism for `variant`. It accepts a callable (visitor) and one or more `variant` objects, dispatching the call based on the type currently held by the `variant`. This is safer than `switch-case` because the compiler checks whether you have handled all alternative types.

### Simple visit with a lambda

```cpp
std::variant<int, double, std::string> v = std::string("hello");

std::visit([](auto&& arg) {
    std::cout << arg << "\n";
}, v);
```

Here, `auto&&` is a forwarding reference, and `visit` instantiates this lambda based on the type currently held by `v`. When you only need to perform the same operation on all types, this approach is very concise.

### Overload sets: handling different types

A more common scenario is that different types require different handling logic. In this case, we need an "overload set" — a callable object with a corresponding overload for each alternative type. There is a classic trick in C++17 to achieve this:

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

This `Overloaded` "inherits" all the `operator()` from multiple lambdas together, forming a callable object with overloads for multiple types. In use:

```cpp
std::variant<int, double, std::string> v = 3.14;

std::visit(Overloaded{
    [](int i)         { std::cout << "int: " << i << "\n"; },
    [](double d)      { std::cout << "double: " << d << "\n"; },
    [](const std::string& s) { std::cout << "string: " << s << "\n"; }
}, v);
```

The compiler checks whether your `Overloaded` covers all alternative types of the `variant`. If you miss handling for a certain type, the compiler will error directly — this is the embodiment of compile-time type safety. In C++20, you don't even need to write `Overloaded` by hand — the standard library directly supports the visit pattern with multiple lambdas (though the formal support mechanism is still evolving).

### visit with return values

A visitor in `visit` can also return values. The return types of all lambdas must be compatible (convertible to a common type):

```cpp
std::variant<int, double, std::string> v = 42;

auto type_name = std::visit(Overloaded{
    [](int)    -> std::string { return "int"; },
    [](double) -> std::string { return "double"; },
    [](const std::string&) -> std::string { return "string"; }
}, v);

std::cout << "type is: " << type_name << "\n";  // "type is: int"
```

## Step 4 — variant as a Replacement for Runtime Polymorphism

An important use case for `variant` is replacing polymorphism implemented with virtual functions (known as a "closed hierarchy" or "visit-based polymorphism"). Traditional virtual function polymorphism requires heap allocation, virtual function table pointers, and reference semantics — whereas `variant` can store values directly on the stack with no virtual function call overhead.

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

Usage comparison:

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

The advantage of the `variant` approach lies in: value semantics (no need for `new`/`delete`), contiguous memory (stored directly in the `vector`, cache-friendly), and compile-time type checking (all branches of `visit` are determined at compile time). But it comes with a cost: every time you add a new shape, you must modify the `variant` definition of the `Shape` — which is inflexible in certain scenarios. If your type hierarchy is "open" (third parties can extend it with new types), virtual functions remain the better choice.

## Step 5 — Exception Safety and valueless_by_exception

`variant` has a rather special state called `valueless_by_exception`. When `variant` is in the process of switching types (e.g., during assignment or `emplace`), the constructor of the new type throws an exception, and the old value has already been destroyed, the `variant` enters this "valueless" state.

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

In this state, `std::visit` throws `std::bad_variant_access`, and `std::get` also throws an exception. So if `variant` in your code might encounter this situation, it's best to check before accessing.

⚠️ In practice, `valueless_by_exception` rarely appears during normal use. It is only triggered in the specific scenario where "constructing a new value throws an exception." If the constructors of all your alternative types are `noexcept` (or you don't use exceptions), you don't need to worry about this state at all.

## Practical Application — Message Type System

One of the most suitable scenarios for `variant` is a messaging system. In event-driven architectures, messages in a message queue can have multiple types, each with a different payload. `variant` + `visit` can handle this pattern very elegantly:

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

The benefit of this code is: if you add a new message type (e.g., `FileTransfer`), the compiler will error directly at the `visit` call site in `Overloaded` — you must add a corresponding overload in `handle`. This ability to have "the compiler find all the places you need to modify when adding a new type" is one of the biggest advantages of `variant` over `switch-case` or virtual functions.

## Practical Application — Configuration Values and AST Nodes

### Configuration Values

Configuration systems often need to store values of different types: integers, floating-point numbers, strings, and booleans. `variant` is a natural fit:

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

### AST Nodes

In the frontend of a compiler or interpreter, the node types of an abstract syntax tree (AST) are also naturally suited to representation with `variant`:

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

⚠️ Note that we use `std::unique_ptr<BinaryExpr>` here instead of a direct `BinaryExpr`, because `variant` cannot directly contain incomplete types. Recursive data structures must break the circular dependency through pointers (or `std::unique_ptr`).

## Memory Layout and Performance Considerations

The size of `variant` equals the size of the largest alternative type plus a small metadata field (used to record the index of the currently held type). This means that even if you currently only hold a `int`, the `variant<int, std::string>` is still at least as large as `sizeof(std::string) + sizeof(size_t)`.

```cpp
std::cout << "sizeof(variant<int, double, string>): "
          << sizeof(std::variant<int, double, std::string>) << "\n";
// 典型输出：32（64 位平台上，string 占 32 字节，int 和 double 各 8 字节）
std::cout << "sizeof(string): " << sizeof(std::string) << "\n";
// 典型输出：32
```

This size is completely acceptable for most applications. But in memory-constrained embedded scenarios, you may need to evaluate whether it's worth using `variant` to replace a hand-written `union` + `enum` tag scheme. The type safety benefits brought by `variant` usually far outweigh the overhead of a few bytes of memory.

## Summary

`std::variant` is one of the most important type safety tools in C++17. It solves three core problems of the bare `union`: not knowing what type it currently holds (solved via an internal tag), not managing object lifetimes (automatically calling constructors/destructors), and not supporting non-trivial types (no restrictions whatsoever).

`std::visit` is the core access mechanism for `variant`, and combined with the `Overloaded` idiom, it enables type-safe pattern matching. When your type set is finite and known (message types, configuration values, AST nodes, etc.), `variant` is more efficient and safer than virtual functions. But if the type set is open (third parties can extend it), virtual functions remain the more appropriate choice.

`valueless_by_exception` is a state you need to be aware of but usually don't need to worry about — it only occurs in the extreme scenario where constructing a new value throws an exception. Simply knowing this state exists is enough; there's no need to be overly defensive about it in actual code.

The `std::optional` we discuss next can be seen as a special case of `variant` — when your "type set" has only two possibilities ("has a value" and "does not have a value"), `optional` is the more concise choice.

## References

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit)
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access)
- [C++ Core Guidelines: C++ union](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c181-prefer-using-variant-over-union)
