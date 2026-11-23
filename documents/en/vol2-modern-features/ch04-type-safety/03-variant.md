---
chapter: 4
cpp_standard:
- 17
description: Replacing `union` with `variant`, combined with `visit`, for type-safe
  polymorphism
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Lambda Basics: The Elegant Expression of Anonymous Functions'
- enum class and Scoped Enums
reading_time_minutes: 13
related:
- "std::optional: Elegantly Expressing 'A Value May Be Absent'"
- Modern Approaches to Error Handling
tags:
- host
- cpp-modern
- intermediate
- variant
- 类型安全
title: 'std::variant: A Type-Safe Union'
translation:
  source: documents/vol2-modern-features/ch04-type-safety/03-variant.md
  source_hash: ccbeeaccc19a798d1aaec7307b8d7fe8c83f0d92b9a9c779431e2bf7039d6bd7
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 12421
---
# std::variant: A Type-Safe Union

`std::variant` (introduced in C++17) is the modern replacement for `union`. The core problem it solves is this: how do you stay type-safe under the constraint of "holding exactly one of several types at any given moment"? Unlike a raw `union`, a `variant` knows which type it currently holds, checks on access, and manages the held object's lifetime correctly. In this chapter we start from the pain points of `union` and work through `variant`'s mechanics and usage step by step.

## Step 1 — The Fatal Flaw of union

Before we talk about `variant`, let's first look at why a raw `union` is unsafe.

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

The problem with this code is that a `union` itself **does not record** which member it currently holds. The programmer has to maintain a separate "tag" to track the active member. If you forget to update the tag, or the tag drifts out of sync with the actual state, you get undefined behavior.

Worse still, a `union` **does not support types with non-trivial constructors/destructors**. `std::string`, for example, can't simply live in a `union` — you have to call placement new yourself to construct it and call the destructor yourself to destroy it. This manual management is both tedious and error-prone.

```cpp
union BadUnion {
    BadUnion() : i(0) {}   // 默认构造被隐式删除，得自己动手写
    ~BadUnion() {}         // 析构也一样被隐式删除
    int i;
    std::string s;         // C++11 起允许放进 union，但生命周期全归您管
};

BadUnion u;
// u.s = "hello";  // UB！没有先构造 s
new (&u.s) std::string("hello");  // placement new
// ... 用完后必须手动析构
u.s.~basic_string();
```

Honestly, writing this kind of code always feels like walking a tightrope — miss a single step and you get a resource leak or worse. `std::variant` makes all of this manual management completely unnecessary.

## Step 2 — Basic Usage of variant

### Construction and Assignment

A `std::variant<Types...>` can hold a value of **exactly one** of the `Types...` at any given time. When default-constructed, it constructs the first alternative (unless you use `std::monostate` as a placeholder):

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

On every assignment, the `variant` automatically destroys the old value and constructs the new one. You never manage any lifetime by hand — `variant`'s internal machinery does it all.

### Accessing the Value

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

Our recommendation: if you only need to check the type, use `std::holds_alternative`; if you need a pointer to the value (and don't want to deal with exceptions), use `std::get_if`; if you're sure the type is right and want an immediate error on mismatch, use `std::get`.

## Step 3 — std::visit and the Visitor Pattern

`std::visit` is `variant`'s most central access mechanism. It takes a callable (the visitor) and one or more `variant` objects, and dispatches the call based on the type the `variant` currently holds. This is safer than `switch-case`, because the compiler checks whether you've handled all the alternatives.

### A Simple visit with a Lambda

```cpp
std::variant<int, double, std::string> v = std::string("hello");

std::visit([](auto&& arg) {
    std::cout << arg << "\n";
}, v);
```

Here `auto&&` is a forwarding reference, and `visit` instantiates this lambda for whichever type `v` currently holds. When all you need is to perform the same operation on every type, this form is wonderfully compact.

### Overload Sets: Handling Different Types

The more common scenario is that different types need different handling logic. What we want then is an "overload set" — a callable that has a matching overload for every alternative type. C++17 has a classic trick for building one:

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

This `Overloaded` "inherits" the `operator()` of multiple lambdas into one, forming a callable with overloads for many types. In use:

```cpp
std::variant<int, double, std::string> v = 3.14;

std::visit(Overloaded{
    [](int i)         { std::cout << "int: " << i << "\n"; },
    [](double d)      { std::cout << "double: " << d << "\n"; },
    [](const std::string& s) { std::cout << "string: " << s << "\n"; }
}, v);
```

The loading and dispatching process is animated below: you can play and pause it, or use the step controls to single-step through and watch how the index changes and which branch the arrow points to:

<Anim id="variant-visit" />

The compiler checks whether your `Overloaded` covers all of the `variant`'s alternatives. Leave one out and the build fails: the error lands right on the `visit` call line, pointing at the call that couldn't be matched.

> Don't count on the standard library adopting this spelling for us: ever since `std::visit` was finalized in C++17, its signature has been one visitor plus any number of `variant`s—it has never accepted the multiple-callable-object form. The `std::overload` proposal (P0051R3) was not adopted, so hand-writing `Overloaded` remains the mainstream to this day. The real news in the visit family is C++26 giving `variant` a member form, `variant::visit`—which has nothing to do with multi-lambda; just knowing it exists is enough.

### A visit That Returns a Value

A `visit` visitor can return a value too. All the lambdas' return types must be compatible (convertible to a common type):

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

One important use of `variant` is replacing polymorphism built on virtual functions (this is known as a "closed hierarchy", or "visit-based polymorphism"). Traditional virtual-function polymorphism requires heap allocation, a vtable pointer, and reference semantics — while `variant` can store the value directly on the stack, with no virtual-call overhead.

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

The advantages of the `variant` approach: value semantics (no `new`/`delete`), contiguous memory (stored directly in the `vector`, cache-friendly), and compile-time type checking (all `visit` branches are resolved at compile time). But it comes at a cost: every time you add a new shape, you must modify the `Shape` `variant` definition — which is inflexible in some scenarios. If your type hierarchy is "open" (third parties can extend it with new types), virtual functions are still the better choice.

## Step 5 — Exception Safety and valueless_by_exception

`variant` has a rather special state called `valueless_by_exception`. When the `variant` is in the middle of switching types (say, during assignment or `emplace`), and the new type's constructor throws while the old value has already been destroyed, the `variant` enters this "no value" state.

```cpp
struct ThrowCopy {
    ThrowCopy() = default;
    ThrowCopy(const ThrowCopy&) { throw std::runtime_error("boom"); }
};

std::variant<int, ThrowCopy> v = 42;
try {
    ThrowCopy tc;
    v = tc;  // 切换到 ThrowCopy：拷贝构造抛异常，切换没走完
} catch (const std::runtime_error&) {
    // v 现在是 valueless_by_exception 状态
    std::cout << "valueless: " << v.valueless_by_exception() << "\n";  // 1（true）
}
```

The output of the code above was measured by the author on this machine: the printed valueless is 1. Out of curiosity I also probed `index()`—at that moment it hands back `variant_npos` as well (-1 viewed as a signed value).

Interestingly, the other end of the same libstdc++ tells the same story: for small, trivially copyable types, `emplace` constructs the new value in a temporary `variant` first and moves it in only after construction succeeds—if an exception is thrown midway, the old value in our hands stays exactly where it was. The criteria are libstdc++'s own (the type must be trivially copyable and not too large); the word "possibly" in the standard is precisely the leeway left for these implementation differences.

In this state, `std::visit` throws `std::bad_variant_access`, and `std::get` throws as well. So if a `variant` in your code could run into this situation, it's best to check before accessing it.

In practice, `valueless_by_exception` almost never appears in normal use. It only triggers in the specific scenario of "an exception thrown while constructing the new value". If all your alternatives' constructors are `noexcept` (or you don't use exceptions), you don't need to worry about this state at all.

## Practical Application — A Message Type System

One of the scenarios `variant` fits best is message-passing systems. In event-driven architectures, the messages in a message queue can come in several types, each with a different payload. `variant` + `visit` handles this pattern very elegantly:

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

The benefit of this code: if you add a new message type (say, `FileTransfer`), the compiler errors out directly at the `visit` call with `Overloaded` — you must add the corresponding overload to `handle`. This ability of "when a new type is added, the compiler finds every place that needs modifying for you" is one of `variant`'s biggest advantages over `switch-case` or virtual functions.

## Practical Application — Config Values and AST Nodes

### Config Values

Config systems often need to store values of different types: integers, floating-point numbers, strings, booleans. `variant` is a natural fit:

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

In the front end of a compiler or interpreter, the node types of an abstract syntax tree (AST) are also a natural fit for `variant`:

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

Note that this uses `std::unique_ptr<BinaryExpr>` instead of a plain `BinaryExpr`, because a `variant` cannot directly contain incomplete types. Recursive data structures must break the circular dependency through a pointer (or `std::unique_ptr`).

## Memory Layout and Performance Considerations

A `variant`'s size equals "the size of the largest alternative" plus a small metadata field (used to record the index of the currently held type). This means that even if you currently hold just an `int`, a `variant<int, std::string>` is still at least `sizeof(std::string) + sizeof(size_t)` big.

```cpp
std::cout << "sizeof(variant<int, double, string>): "
          << sizeof(std::variant<int, double, std::string>) << "\n";
// 典型输出：40（64 位平台上，string 占 32 字节，int 占 4 字节，double 占 8 字节）
std::cout << "sizeof(string): " << sizeof(std::string) << "\n";
// 典型输出：32
```

> A quick aside on the size of int: you can read the details on [cppreference: Fundamental types](https://en.cppreference.com/cpp/language/types). The short version: int is required to be at least 16 bits, that is, 2 bytes; on every other platform it is 4 bytes. Of course, don't memorize this as rote trivia.
> Credit to [YukunJ](https://github.com/YukunJ) for providing the [example](https://godbolt.org/z/sbvEMW56G).

This size is perfectly acceptable for most applications. But in severely memory-constrained embedded scenarios, you may want to evaluate whether `variant` is worth using instead of a hand-written `union` + `enum` tag scheme. The type-safety benefit `variant` brings usually far outweighs a few bytes of memory overhead.

## References

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit)
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access)
- [C++ Core Guidelines: C++ union](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c181-prefer-using-variant-over-union)
