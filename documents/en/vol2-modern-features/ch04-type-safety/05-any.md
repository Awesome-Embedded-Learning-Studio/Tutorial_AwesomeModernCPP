---
chapter: 4
cpp_standard:
- 17
description: Understand the type erasure mechanism behind `any`, where it fits, and
  its performance characteristics
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'std::variant: A Type-Safe Union'
- 'std::optional: Elegantly Expressing ''A Value May Be Absent'''
reading_time_minutes: 15
related:
- 'std::function, std::invoke, and Callable Objects'
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
title: 'std::any and Type Erasure'
translation:
  source: documents/vol2-modern-features/ch04-type-safety/05-any.md
  source_hash: f8dd4d5c102a49c8adc535a24fdddd63ef4abecd8c5555fa2a5d1af200068613
  translated_at: '2026-11-22T16:36:41+00:00'
  engine: glm-5.3-refresh
  token_count: 12625
---
# std::any and Type Erasure

I believe that when many people first encounter `std::any`, the reaction is more or less the same: isn't this just a wrapper around `void*`? What's it good for? Back in the day I grumbled too, about the standard library not doing proper work. It wasn't until I actually built the configuration module of a plugin system that my view shifted—slightly (note: only slightly; I still think `std::any` isn't of great use!).

C's `void*` throws away all type information: taking a value back out of its hands is pure guesswork on our part. Of course, you can keep a separate record of the type off to the side, but information and data stored apart are one lapse away from no longer matching—and that's exactly how the big illness of inconsistent state is born. `std::any` is different: we can load any type into it, and it **remembers** what it holds. Fetch the value with the wrong type and it throws an exception to warn you, instead of shoving a pile of memory garbage at you.

`std::any` was introduced in C++17, and it promises exactly one thing: store a value of any type, and take it back out safely when needed. Holding that capability up is the type erasure we already took apart in the article on `std::function`. The concrete type is hidden on the way in; on the way out, a type check brings the safety back.

Of course, this thing actually still loses to `variant`—as for why it loses to the `variant` we covered earlier, do read on (smile).

## Step 1 — The Design Motivation Behind `any`

C++ is a statically typed language: at compile time, the compiler must already know the type of every variable and expression. But sometimes you genuinely need a container whose stored type is only known at run time. The classic scenarios include:

Property maps in plugin systems — different plugins may register properties of different types (integers, strings, custom structs). Variable bindings in scripting engines — a variable in the script can be of any type at run time. Serialization/deserialization frameworks — when parsing JSON or XML, the type of certain fields can only be determined once you see the actual data.

In C, this kind of requirement is usually met with `void*`. But `void*` is completely type-unsafe — you store an `int*` by converting it to `void*`, then convert it to `double*` on the way out and use it, and the compiler won't raise any warning at all; at run time you get a pile of garbage data. The goal of `std::any` is to offer the same "store whatever type you like" flexibility as `void*`, while guaranteeing type safety when the value is retrieved.

## Step 2 — Basic Usage of `any`

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

Unlike `variant`, `any`'s list of candidate types is completely open — you can store any type, with no need to enumerate them in the declaration. That is exactly where its flexibility comes from, and also the root of why it performs worse than `variant`.

### Checking and Retrieving Values

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

`std::any_cast` has two overloads: the by-reference version throws a `std::bad_any_cast` exception when the type doesn't match; the pointer version returns `nullptr` when the type doesn't match. If you need to check types frequently, the pointer version is more efficient (no exception overhead involved).

Here is an easy trap to step into: `std::any_cast<int>(a)` returns a **copy** of the value, not a reference. If you want to modify the value inside the `any`, you need `std::any_cast<int&>(a)` to obtain a reference:

```cpp
std::any a = 42;
std::any_cast<int&>(a) = 100;  // 修改 any 内部的值为 100
// int copy = std::any_cast<int>(a); copy = 200;  // 只修改了拷贝，any 内部没变
```

## Step 3 — Type Erasure and the Small Buffer Optimization

The implementation of `std::any` is based on the type erasure technique. Put simply, `any` maintains a "conceptual interface" internally — it knows how to destroy the value it holds, how to copy it, and how to fetch its `type_info` — but not what the value's concrete type is. These operations are dispatched through function pointers or virtual functions.

When you write `std::any a = 42;`, the `any` internally creates a "wrapper" object that holds the `int` value and provides implementations of the operations above. The `any` itself stores only a pointer (or reference) to this wrapper.

To optimize the performance of small objects, mainstream standard library implementations all adopt the Small Buffer Optimization (SBO). When the held type is small enough (typically about the size of a `std::string` or smaller), the value is stored directly in a buffer inside the `any` object itself, with no heap allocation. Only when the value exceeds the SBO threshold is memory allocated on the heap.

```cpp
std::cout << "sizeof(std::any): " << sizeof(std::any) << "\n";
// 典型输出：16、32 或 64（libstdc++ / libc++ / MSVC 依次对应）
// 这包括了 SBO 缓冲区 + 类型信息指针 + 管理数据

// 小对象：栈上存储（SBO 生效）
std::any small = 42;
// 大对象：堆上分配
std::any large = std::vector<int>(1000000, 0);
```

The storage layouts of the two cases compare like this:

![Storage layout of std::any: small objects inlined (SBO) vs. large objects allocated on the heap](./05-any-layout.drawio)

The existence of SBO means that for common types like `int`, `double`, and small structs, `any`'s performance overhead is very small — no heap allocation, just one extra level of indirection. But for large objects (say, a big `vector` or a big `string`), every copy of the `any` triggers a heap allocation plus a deep copy, and that overhead is not negligible.

## Step 4 — any vs variant vs void* vs union

All four mechanisms can "store values of different types", but their positioning and applicable scenarios are completely different. Let's compare them in a table:

| Feature | `std::any` | `std::variant` | `void*` | `union` |
|------|-----------|---------------|---------|---------|
| Type safety | Run-time check | Compile-time check | No check | No check |
| Candidate types | Any | Fixed list | Any | Fixed list |
| Lifetime management | Automatic | Automatic | Manual | Manual |
| Heap allocation | Possible (outside SBO) | None | Depends on usage | None |
| `visit` support | No | Yes | No | No |
| Memory overhead | Medium | Largest alternative + metadata | One pointer | Largest member |
| Type query | `type()` + `any_cast` | `holds_alternative` | Not queryable | Not queryable |

From this comparison one thing is clear: **if you can enumerate all the possible types at compile time, `variant` is almost always the better choice than `any`**. `variant` offers compile-time type checking, no heap allocation, and `visit` support. Only when the list of types cannot be determined at compile time (plugin systems, scripting engines, and the like) does `any` have irreplaceable value.

In modern C++, `void*` and `union` have essentially no legitimate use case left — `any` and `variant` each cover their applicable scenarios, and more safely at that.

## Step 5 — Performance Characteristics of `any`

Understanding `any`'s performance overhead is crucial for using it correctly.

**Construction/assignment overhead**: For types within SBO range (usually no more than about 32 bytes), construction and assignment involve one value copy plus a little metadata setup — essentially as fast as copying a raw type. For types beyond the SBO threshold, a `new` and a `delete` get triggered (when replacing the value).

**Retrieval overhead**: `std::any_cast` needs to perform one `typeid` comparison (checking whether the stored type matches the requested type), followed by a `static_cast`. This overhead is very small — just a pointer comparison plus a type-info lookup.

**Copy overhead**: Copying an `any` deep-copies the value it holds. For a large object, that is a full deep copy. If you need to avoid this overhead, consider wrapping a `std::shared_ptr<T>` inside the `std::any` — copying the `any` then just increments the reference count instead of copying the underlying object.

```cpp
// 避免大对象拷贝：用 shared_ptr 包裹
auto big_data = std::make_shared<std::vector<int>>(1000000, 0);
std::any a = big_data;  // 拷贝 shared_ptr，不拷贝 vector

auto retrieved = std::any_cast<std::shared_ptr<std::vector<int>>>(a);
// retrieved 指向同一个 vector，引用计数增加
```

## Step 6 — Where `any` Fits

### Dynamic Configuration Systems

When you need a key–value mapping where the values can be of various different types, `any` is a natural choice:

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

This "property dictionary of arbitrary types" pattern is extremely common in game engines, GUI frameworks, and plugin systems. `any` provides enough flexibility to store values of different types, while `any_cast` guarantees type safety on retrieval.

### Property Dictionaries / Message Passing

In message-passing or component systems, an entity may need to carry attributes of different types. `any` can be used to build a generic attribute container:

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

When you design a plugin system, the interface between the host and the plugins may need to pass data "of types that the host and the plugins each define on their own". Since neither side's types are visible to the other at compile time, `any` can serve as a neutral transfer container:

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

## Step 7 — Writing a Simplified `any` by Hand

To understand the machinery of type erasure more deeply, let's write a bare-bones `any` by hand. This implementation is far less polished than the standard library's version, but it will help you understand what actually goes on inside `any`.

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

This simplified implementation reveals the three core mechanisms of `any`:

First, `HolderBase` is the type-erased interface — it defines "the operations that any stored type must support" (fetch its type info, clone itself) without exposing the concrete type.

Second, `Holder<T>` is the concrete type wrapper — it inherits from `HolderBase` and provides the implementation for each concrete type. When you write `MiniAny a = 42;`, what gets created internally is a `Holder<int>` instance.

Third, `mini_any_cast` restores type safety through a `typeid` comparison — before the value is taken out, it checks whether the stored type matches the requested type.

The standard library's `std::any` is far more sophisticated than this implementation: it has the SBO optimization to avoid heap allocation for small objects, move-semantics optimizations, and more flexible construction methods such as `emplace`. But the core idea is exactly the same.

## Step 8 — When Not to Use `any`

Flexible as `any` is, most of the time it is not the best choice. Here are several scenarios where you should not use `any`:

**The set of types is known and finite**: If you know the value can only be one of `int`, `double`, or `std::string`, just use `variant<int, double, std::string>` directly. `variant` provides compile-time type checking and `visit`, and performs better too.

**You only need to express "has a value or doesn't"**: Use `optional<T>` rather than `any`. `optional` is more lightweight and semantically clearer.

**Templates can solve it**: If your function needs to accept parameters of different types but does not need to store "values of different types" at run time, templates are usually the better choice. Templates complete the type dispatch at compile time, with zero run-time overhead.

**Polymorphism can solve it**: If you have a family of related types that share a common interface, virtual functions may fit better than `any`. Virtual functions provide a type-safe interface, whereas `any` abandons interface constraints entirely.

Our general rule is: **if `variant` works, don't use `any`; if templates work, don't use run-time type erasure**. `any` is the last resort — consider it only in scenarios where no static approach applies.

## The Embedded Perspective — Considering `any` in Resource-Constrained Environments

In embedded systems, `std::any` is usually not the tool of first choice. There are three reasons. First, `any`'s SBO buffer occupies extra RAM (typically 16–32 bytes), an overhead that cannot be ignored on an MCU with only a few tens of KB of RAM. Second, large objects trigger heap allocation, and many embedded systems either have no heap at all or a very limited one. Third, the type check inside `any_cast` involves RTTI (run-time type information), and in some embedded toolchains RTTI is disabled (to save code space).

If you genuinely need similar "dynamic typing" functionality in an embedded project, the recommended approach is a restricted version built from `variant` + an `enum` tag — all possible types are pinned down at compile time, with no need for RTTI and no heap allocation.

## References

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast)
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)
- [Arthur O'Dwyer: Back to Basics - Type Erasure (CppCon 2019)](https://www.youtube.com/watch?v=tbUCHifyT24)
