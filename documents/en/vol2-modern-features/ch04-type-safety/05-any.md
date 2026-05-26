---
title: std::any and Type Erasure
description: Understanding the type erasure mechanism, use cases, and performance
  characteristics of `any`
chapter: 4
order: 5
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
difficulty: intermediate
platform: host
cpp_standard:
- 17
reading_time_minutes: 15
prerequisites:
- 'Chapter 4: std::variant'
- 'Chapter 4: std::optional'
related:
- std::function 与类型擦除
translation:
  source: documents/vol2-modern-features/ch04-type-safety/05-any.md
  source_hash: 400b3379d324936f9fa3fe302532577e7467011fbcceb76e88123a68e47a6c3d
  translated_at: '2026-05-26T11:28:58.590226+00:00'
  engine: anthropic
  token_count: 3030
---
# std::any and Type Erasure

## Introduction

I believe many developers, upon their first encounter with `std::any`, react by thinking: isn't this just a wrapper around `void*`? (My immediate reaction was exactly that, and I even complained about the standard committee not focusing on more important things.) What's the point? It wasn't until I built a configuration module for a plugin system that I realized the difference—`void*` discards all type information, so retrieving a value from it is purely guesswork (my other point being that it carries a tiny bit of information, but we all know that relying on separate metadata easily leads to state inconsistency issues); whereas `std::any` can also hold any type, but it **remembers** what it holds. When you try to retrieve the value with the wrong type, it throws an exception instead of handing you a chunk of garbage memory.

The core capability of `std::any` (introduced in C++17) is "storing a value of any type and safely retrieving it when needed." It achieves this through a technique called "type erasure"—hiding specific type information during storage and restoring safety through type checking during retrieval. In this chapter, we will dive deep into the mechanisms of `std::any`, its applicable scenarios, and why most of the time you should actually use `std::variant` instead of `std::any`.

## Step 1 — The Design Motivation for any

C++ is a statically typed language; the compiler must know the type of every variable and expression at compile time. However, sometimes you genuinely need a container where "the type stored is only known at runtime." Classic scenarios include:

Property maps in plugin systems—different plugins might register properties of different types (integers, strings, custom structs). Variable binding in scripting engines—variables in a script can be of any type at runtime. Serialization/deserialization frameworks—when parsing JSON or XML, the type of certain fields can only be determined after seeing the actual data.

In C, such requirements are typically met with `void*`. But `void*` is completely type-unsafe—you cast an `int*` to `void*` to store it, and cast it to `float*` when retrieving it to use. The compiler won't issue any warnings, and at runtime, you will get a bunch of garbage data. The goal of `std::any` is to provide the same "store any type" flexibility as `void*`, while guaranteeing type safety upon retrieval.

## Step 2 — Basic Usage of any

### Construction and Assignment

`std::any` can hold a value of any copy-constructible type:

```cpp
#include <any>
#include <string>
#include <iostream>
#include <vector>

int main()
{
    std::any a = 42;                     // 持有 int
    a = 3.14;                            // 现在持有 double
    a = std::string("hello");            // 现在持有 std::string

    // 空状态
    std::any empty;                      // 不持有任何值
    std::any also_empty = std::any{};    // 同上

    // 就地构造
    std::any v(std::in_place_type<std::vector<int>>, 10, 42);
    // 构造一个包含 10 个 42 的 vector<int>
}
```

Unlike `std::variant`, the list of candidate types for `std::any` is completely open—you can store any type without having to enumerate them at the point of declaration. This is exactly where its flexibility lies, and it is also the root cause of its inferior performance compared to `std::variant`.

### Checking and Retrieval

```cpp
std::any a = 42;

// 检查是否有值
if (a.has_value()) {
    std::cout << "has value\n";
}

// 获取类型信息
std::cout << "type: " << a.type().name() << "\n";  // 实现相关（如 "i" 或 "int"）

// 取值：std::any_cast
try {
    int val = std::any_cast<int>(a);        // OK，返回 42
    std::cout << "value: " << val << "\n";

    // double bad = std::any_cast<double>(a);  // 抛出 std::bad_any_cast！
} catch (const std::bad_any_cast& e) {
    std::cout << "wrong type: " << e.what() << "\n";
}

// 指针版本：不抛异常，返回 nullptr
int* ptr = std::any_cast<int>(&a);         // OK，ptr 不为空
double* bad = std::any_cast<double>(&a);   // bad 为 nullptr
```

`std::any_cast` has two overloads: the reference-passing version throws a `std::bad_any_cast` exception when the type doesn't match; the pointer-passing version returns `nullptr` when the type doesn't match. If you need to check types frequently, the pointer version is more efficient (it doesn't involve exception overhead).

⚠️ There is a common pitfall here: `std::any_cast` returns a **copy** of the value, not a reference. If you want to modify the value inside the `std::any`, you need to use `std::any_cast<T&>` to obtain a reference:

```cpp
std::any a = 42;
std::any_cast<int&>(a) = 100;  // 修改 any 内部的值为 100
// int copy = std::any_cast<int>(a); copy = 200;  // 只修改了拷贝，any 内部没变
```

## Step 3 — Type Erasure and Small Buffer Optimization

The implementation of `std::any` is based on the type erasure technique. Simply put, `std::any` internally maintains a "concept interface"—it knows how to destroy the held value, how to copy it, and how to get its `std::type_info`—but it doesn't know the specific type of the value. These operations are dispatched through function pointers or virtual functions.

When you execute `std::make_any<int>(42)`, `std::any` internally creates a "wrapper" object. This wrapper holds a value of type `int` and provides implementations for the aforementioned operations. `std::any` itself only stores a pointer (or reference) to this wrapper.

To optimize the performance of small objects, mainstream standard library implementations all use Small Buffer Optimization (SBO). When the held type is small enough (usually around the same size as `std::string` or smaller), the value is stored directly in a buffer inside the `std::any` object, requiring no heap allocation. Only when the value exceeds the SBO threshold is memory allocated on the heap.

```cpp
std::cout << "sizeof(std::any): " << sizeof(std::any) << "\n";
// 典型输出：16 或 32（取决于实现）
// 这包括了 SBO 缓冲区 + 类型信息指针 + 管理数据

// 小对象：栈上存储（SBO 生效）
std::any small = 42;
// 大对象：堆上分配
std::any large = std::vector<int>(1000000, 0);
```

The existence of SBO means that for common types like `int`, `double`, and small structs, the performance overhead of `std::any` is very small—there is no heap allocation, just an extra level of indirection. But for large objects (such as large `std::vector`s or large `std::string`s), every time you copy an `std::any`, it triggers a heap allocation and a deep copy, and this overhead is not negligible.

## Step 4 — any vs variant vs void* vs union

These four mechanisms can all achieve "storing values of different types," but their positioning and applicable scenarios are entirely different. Let's compare them with a table:

| Feature | `std::any` | `std::variant` | `void*` | `union` |
|---------|-----------|---------------|---------|---------|
| Type Safety | Runtime check | Compile-time check | No check | No check |
| Candidate Types | Any | Fixed list | Any | Fixed list |
| Lifetime Management | Automatic | Automatic | Manual | Manual |
| Heap Allocation | Possible (outside SBO) | None | Depends on usage | None |
| `visit` Support | No | Yes | No | No |
| Memory Overhead | Medium | Largest candidate type + metadata | One pointer | Largest member |
| Type Query | `type()` + `any_cast` | `holds_alternative` | Cannot query | Cannot query |

From this comparison, we can see: **if you can enumerate all possible types at compile time, `std::variant` is almost always a better choice than `std::any`**. `std::variant` provides compile-time type checking, has no heap allocation, and supports `std::visit`. Only when the type list cannot be determined at compile time (such as in plugin systems, scripting engines, etc.) does `std::any` have irreplaceable value.

`void*` and `union` basically have no legitimate use cases in modern C++ anymore—`std::any` and `std::variant` respectively cover their applicable scenarios, and do so more safely.

## Step 5 — Performance Characteristics of any

Understanding the performance overhead of `std::any` is crucial for using it correctly.

**Construction/assignment overhead**: For types within the SBO range (usually no larger than about 32 bytes), construction and assignment involve one value copy and a small amount of metadata setup, basically as fast as copying the raw type. For types that exceed the SBO threshold, a `new` allocation and a `delete` (when replacing a value) are triggered.

**Retrieval overhead**: `std::any_cast` requires a `type_info` comparison (checking whether the stored type matches the requested type), followed by a `static_cast`. This overhead is very small—it's just one pointer comparison plus one type information lookup.

**Copy overhead**: Copying an `std::any` deep-copies the value it holds. For large objects, this is a complete deep copy. If you need to avoid this overhead, you can consider wrapping `std::any` with `std::shared_ptr`—this way, copying the `std::any` just increments a reference count and doesn't copy the underlying object.

```cpp
// 避免大对象拷贝：用 shared_ptr 包裹
auto big_data = std::make_shared<std::vector<int>>(1000000, 0);
std::any a = big_data;  // 拷贝 shared_ptr，不拷贝 vector

auto retrieved = std::any_cast<std::shared_ptr<std::vector<int>>>(a);
// retrieved 指向同一个 vector，引用计数增加
```

## Step 6 — Applicable Scenarios

### Dynamic Configuration Systems

When you need a key-value map where values can be of various different types, `std::any` is a natural choice:

```cpp
#include <any>
#include <string>
#include <unordered_map>
#include <iostream>

class Config {
public:
    template <typename T>
    void set(const std::string& key, T value)
    {
        entries_[key] = std::move(value);
    }

    template <typename T>
    std::optional<T> get(const std::string& key) const
    {
        auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;

        // 尝试获取正确类型的值
        const T* ptr = std::any_cast<T>(&it->second);
        if (!ptr) return std::nullopt;

        return *ptr;
    }

    bool has(const std::string& key) const
    {
        return entries_.count(key) > 0;
    }

private:
    std::unordered_map<std::string, std::any> entries_;
};

// 使用
Config cfg;
cfg.set("server_host", std::string("192.168.1.1"));
cfg.set("server_port", 8080);
cfg.set("verbose", true);
cfg.set("max_retries", 3);

auto host = cfg.get<std::string>("server_host");    // optional<string> = "192.168.1.1"
auto port = cfg.get<int>("server_port");            // optional<int> = 8080
auto bad  = cfg.get<double>("server_host");         // optional<double> = nullopt（类型不匹配）
auto missing = cfg.get<int>("nonexistent");         // optional<int> = nullopt（键不存在）
```

This "property dictionary of arbitrary types" pattern is very common in game engines, GUI frameworks, and plugin systems. `std::any` provides enough flexibility to store values of different types, while guaranteeing type safety upon retrieval through `std::any_cast`.

### Property Dictionaries / Message Passing

In message passing or component systems, an entity might need to carry attributes of different types. `std::any` can be used to implement a generic attribute container:

```cpp
#include <any>
#include <unordered_map>
#include <string>
#include <functional>
#include <iostream>

class Entity {
public:
    template <typename T>
    void set_attribute(const std::string& name, T value)
    {
        attrs_[name] = std::move(value);
    }

    template <typename T>
    std::optional<T> get_attribute(const std::string& name) const
    {
        auto it = attrs_.find(name);
        if (it == attrs_.end()) return std::nullopt;
        const T* ptr = std::any_cast<T>(&it->second);
        if (!ptr) return std::nullopt;
        return *ptr;
    }

    void list_attributes() const
    {
        for (const auto& [name, value] : attrs_) {
            std::cout << "  " << name << " (type: "
                      << value.type().name() << ")\n";
        }
    }

private:
    std::unordered_map<std::string, std::any> attrs_;
};

// 使用
Entity player;
player.set_attribute("health", 100);
player.set_attribute("name", std::string("Alice"));
player.set_attribute("position", std::make_pair(3.0f, 7.5f));

auto hp = player.get_attribute<int>("health");  // optional<int> = 100
```

### Plugin Interfaces

When designing a plugin system, the interface between the host and the plugin might need to pass data of "types defined by either the host or the plugin." Since the types on both sides are invisible to each other at compile time, `std::any` can serve as a neutral passing container:

```cpp
// 宿主定义
using PluginData = std::any;

class PluginHost {
public:
    // 插件通过这个接口发送"任意类型"的数据给宿主
    virtual void on_plugin_data(const std::string& key, const PluginData& data) = 0;
};

// 插件端
class MyPlugin {
public:
    void send_custom_data(PluginHost& host)
    {
        // 插件可以发送任何类型的数据
        struct CustomResult { int code; std::string message; };
        host.on_plugin_data("result", CustomResult{0, "success"});
    }
};
```

## Step 7 — Writing a Simplified Version of any by Hand

To gain a deeper understanding of the type erasure mechanism, let's write a minimal version of `std::any` by hand. Although this implementation is far less complete than the standard library version, it can help you understand how `std::any` actually works internally.

```cpp
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <utility>

class MiniAny {
public:
    MiniAny() = default;

    // 从任意类型构造
    template <typename T>
    MiniAny(T value) : holder_(new Holder<T>(std::move(value)))
    {}

    // 拷贝构造
    MiniAny(const MiniAny& other)
        : holder_(other.holder_ ? other.holder_->clone() : nullptr)
    {}

    // 移动构造
    MiniAny(MiniAny&& other) noexcept = default;

    // 赋值
    MiniAny& operator=(MiniAny other) noexcept
    {
        swap(holder_, other.holder_);
        return *this;
    }

    bool has_value() const noexcept { return holder_ != nullptr; }

    const std::type_info& type() const noexcept
    {
        return holder_ ? holder_->type() : typeid(void);
    }

    // 内部概念接口
    struct HolderBase {
        virtual ~HolderBase() = default;
        virtual const std::type_info& type() const noexcept = 0;
        virtual std::unique_ptr<HolderBase> clone() const = 0;
    };

    // 具体类型包装
    template <typename T>
    struct Holder : HolderBase {
        T value;

        explicit Holder(T v) : value(std::move(v)) {}

        const std::type_info& type() const noexcept override
        {
            return typeid(T);
        }

        std::unique_ptr<HolderBase> clone() const override
        {
            return std::make_unique<Holder>(value);
        }
    };

    std::unique_ptr<HolderBase> holder_;
};

// 类型安全的取值函数
template <typename T>
T mini_any_cast(const MiniAny& a)
{
    if (!a.has_value()) {
        throw std::runtime_error("bad any cast: empty");
    }
    if (a.type() != typeid(T)) {
        throw std::runtime_error("bad any cast: type mismatch");
    }
    // 向下转型：安全，因为已经验证了类型
    auto* holder = dynamic_cast<MiniAny::Holder<T>*>(a.holder_.get());
    return holder->value;
}
```

This simplified implementation reveals the three core mechanisms of `std::any`:

First, the base class is the interface for type erasure—it defines "the operations that any stored type must support" (getting type information, cloning itself), without exposing the specific type.

Second, the derived class is the concrete type wrapper—it inherits from the base class and provides implementations for each specific type. When you execute `make_any`, what is created internally is an instance of this derived class.

Third, type safety is restored through `type_info` comparison—before retrieving the value, it checks whether the stored type is consistent with the requested type.

The standard library's `std::any` is much more complex than this implementation: it has SBO optimization to avoid heap allocation for small objects, move semantics optimizations, and more flexible construction methods like `in_place_type`. But the core idea is exactly the same.

## Step 8 — When Not to Use any

Although `std::any` is flexible, most of the time it is not the best choice. Here are a few scenarios where you "should not use `std::any`":

**The type set is known and limited**: If you know the value can only be one of `int`, `double`, or `std::string`, just use `std::variant`. `std::variant` provides compile-time type checking and `std::visit`, and has better performance.

**You only need to express "has a value or does not have a value"**: Use `std::optional` instead of `std::any`. `std::optional` is lighter and has clearer semantics.

**It can be solved with templates**: If your function needs to accept parameters of different types, but doesn't need to store "values of different types" at runtime, templates are usually the better choice. Templates complete type dispatch at compile time, with zero runtime overhead.

**It can be solved with polymorphism**: If you have a group of related types that share an interface, virtual functions might be more appropriate than `std::any`. Virtual functions provide a type-safe interface, whereas `std::any` completely abandons interface constraints.

My general principle is: **use `std::variant` if you can instead of `std::any`, and use templates instead of runtime type erasure if you can**. `std::any` is a last resort—only consider it in scenarios where all static approaches are inapplicable.

## An Embedded Perspective — Considerations for any in Resource-Constrained Environments

In embedded systems, `std::any` is usually not the first choice. There are three reasons: first, the SBO buffer of `std::any` consumes extra RAM (usually 16–32 bytes), which is non-negligible overhead on an MCU with only a few dozen KB of RAM. Second, large objects trigger heap allocation, and many embedded systems either have no heap or have very limited heap space. Third, the type checking of `std::any_cast` involves RTTI (Run-Time Type Information), and in some embedded toolchains RTTI is disabled (to save code space).

If you truly need "dynamic typing" functionality in an embedded project, a more recommended approach is to use `std::variant` + an enum tag to implement a restricted version—all possible types are determined at compile time, requiring no RTTI and no heap allocation.

## Summary

`std::any` is the most "dynamic" type-safe container in C++17. It achieves the ability to "store values of any type" through type erasure, and provides type safety checks upon retrieval through `std::any_cast`. Small Buffer Optimization ensures that the performance of small objects is not affected by heap allocation.

But the flexibility of `std::any` comes at a cost: it gives up compile-time type checking (all checks happen at runtime), it may trigger heap allocation (for large objects), and it does not support `std::visit`-style pattern matching. In the vast majority of scenarios, if your type set is known, `std::variant` is the better choice. `std::any` is suited for scenarios that truly require "runtime polymorphism"—plugin systems, scripting engines, and dynamic configuration.

With our understanding of `std::any`, our type safety journey in ch04 comes to a close. From `std::optional` to strong-typedefs, from `std::variant` to `std::expected` and then to `std::any`—the common theme of these tools is: **leverage the type system to catch as many errors as possible at compile time, minimizing runtime uncertainty**.

## Reference Resources

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast)
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)
- [Arthur O'Dwyer: Back to Basics - Type Erasure (CppCon 2019)](https://www.youtube.com/watch?v=tbUCHifyT24)
