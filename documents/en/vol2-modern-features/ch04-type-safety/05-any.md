# std::any and Type Erasure

## Introduction

I believe many developers, upon their first encounter with `std::any`, react by thinking: isn't this just a wrapper around `void*`? (My own immediate reaction was exactly this, and I even complained about the standard committee not focusing on more important things.) What's the point? It wasn't until I built a configuration module for a plugin system that I realized—`void*` discards all type information, so retrieving a value from it is entirely guesswork (my other point is that it carries a little bit of information, but we all know that expressing extra information can easily lead to the major problem of state inconsistency); whereas `std::any` can also hold any type, but it **remembers** what it holds. When you try to retrieve the value using the wrong type, it throws an exception instead of handing you a chunk of garbage memory.

The core capability of `std::any` (introduced in C++17) is "storing a value of any type and safely retrieving it when needed." It achieves this through a technique called "type erasure"—hiding specific type information during storage and restoring safety through type checking during retrieval. In this chapter, we will dive deep into the mechanisms and use cases of `any`, and explore why most of the time you should actually use `variant` instead of `any`.

## Step 1 — The Design Motivation for any

C++ is a statically typed language; the compiler must know the type of every variable and expression at compile time. However, sometimes you genuinely need a container where "the type to store is only known at runtime." Classic scenarios include:

Property maps in plugin systems—different plugins might register properties of different types (integers, strings, custom structs). Variable binding in script engines—variables in a script can be of any type at runtime. Serialization/deserialization frameworks—when parsing JSON or XML, the type of certain fields can only be determined after seeing the actual data.

In C, such requirements are typically met using `void*`. But `void*` is completely type-unsafe—you can cast a `int*` to a `void*` to store it, and cast it to a `double*` when retrieving it, without the compiler issuing any warnings. At runtime, you will just get a bunch of garbage data. The goal of `std::any` is to provide the same "store any type" flexibility as `void*`, while guaranteeing type safety upon retrieval.

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

Unlike `variant`, the alternative type list for `any` is completely open—you can store any type without needing to enumerate them at the point of declaration. This is exactly where its flexibility lies, and it is also the root cause of its inferior performance compared to `variant`.

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

`std::any_cast` has two overloads: the reference-passing version throws a `std::bad_any_cast` exception on type mismatch; the pointer-passing version returns `nullptr` on type mismatch. If you need to check types frequently, the pointer version is more efficient (it involves no exception overhead).

⚠️ There is a common pitfall here: `std::any_cast<int>(a)` returns a **copy** of the value, not a reference. If you want to modify the value inside the `any`, you need to use `std::any_cast<int&>(a)` to obtain a reference:

```cpp
std::any a = 42;
std::any_cast<int&>(a) = 100;  // 修改 any 内部的值为 100
// int copy = std::any_cast<int>(a); copy = 200;  // 只修改了拷贝，any 内部没变
```

## Step 3 — Type Erasure and Small Buffer Optimization

The implementation of `std::any` is based on the type erasure technique. Simply put, `any` internally maintains a "concept interface"—it knows how to destroy the held value, how to copy it, and how to get its `type_info`—but it does not know the value's specific type. These operations are dispatched through function pointers or virtual functions.

When you execute `std::any a = 42;`, `any` internally creates a "wrapper" object. This wrapper holds a value of type `int` and provides implementations for the aforementioned operations. `any` itself only stores a pointer (or reference) to this wrapper.

To optimize performance for small objects, mainstream standard library implementations adopt Small Buffer Optimization (SBO). When the held type is small enough (usually around the size of `std::string` or smaller), the value is stored directly in a buffer inside the `any` object, requiring no heap allocation. Heap memory is only allocated when the value exceeds the SBO threshold.

```cpp
std::cout << "sizeof(std::any): " << sizeof(std::any) << "\n";
// 典型输出：16 或 32（取决于实现）
// 这包括了 SBO 缓冲区 + 类型信息指针 + 管理数据

// 小对象：栈上存储（SBO 生效）
std::any small = 42;
// 大对象：堆上分配
std::any large = std::vector<int>(1000000, 0);
```

The existence of SBO means that for common types like `int`, `double`, and small structs, the performance overhead of `any` is very minimal—there is no heap allocation, just an extra level of indirection. However, for large objects (such as large `vector`s or large `string`s), copying an `any` will trigger a heap allocation and a deep copy every time, and this overhead is not negligible.

## Step 4 — any vs variant vs void* vs union

These four mechanisms can all achieve "storing values of different types," but their positioning and applicable scenarios are entirely different. Let's compare them using a table:

| Feature | `std::any` | `std::variant` | `void*` | `union` |
|---------|-----------|---------------|---------|---------|
| Type Safety | Runtime check | Compile-time check | No check | No check |
| Alternative Types | Any | Fixed list | Any | Fixed list |
| Lifetime Management | Automatic | Automatic | Manual | Manual |
| Heap Allocation | Possible (outside SBO) | None | Depends on usage | None |
| `visit` Support | No | Yes | No | No |
| Memory Overhead | Medium | Largest alternative type + metadata | One pointer | Largest member |
| Type Query | `type()` + `any_cast` | `holds_alternative` | Cannot query | Cannot query |

From this comparison, we can see that: **if you can enumerate all possible types at compile time, `variant` is almost always a better choice than `any`**. `variant` provides compile-time type checking, incurs no heap allocation, and supports `visit`. Only when the type list cannot be determined at compile time (such as in plugin systems or script engines) does `any` have irreplaceable value.

`void*` and `union` basically have no legitimate use cases in modern C++ anymore—`any` and `variant` respectively cover their applicable scenarios, and do so more safely.

## Step 5 — Performance Characteristics of any

Understanding the performance overhead of `any` is crucial for using it correctly.

**Construction/Assignment Overhead**: For types within the SBO range (typically no larger than about 32 bytes), construction and assignment involve a single value copy and a small amount of metadata setup, making them about as fast as copying the raw type. For types exceeding the SBO threshold, a `new` and a `delete` (when replacing a value) will be triggered.

**Retrieval Overhead**: `std::any_cast` requires a `typeid` comparison (checking whether the stored type matches the requested type), followed by a `static_cast`. This overhead is very small—just a pointer comparison plus a type information lookup.

**Copy Overhead**: Copying an `any` deeply copies the value it holds. For large objects, this is a complete deep copy. If you need to avoid this overhead, you can consider wrapping `std::shared_ptr<T>` in a `std::any`—this way, copying the `any` merely increments a reference count and does not copy the underlying object.

```cpp
// 避免大对象拷贝：用 shared_ptr 包裹
auto big_data = std::make_shared<std::vector<int>>(1000000, 0);
std::any a = big_data;  // 拷贝 shared_ptr，不拷贝 vector

auto retrieved = std::any_cast<std::shared_ptr<std::vector<int>>>(a);
// retrieved 指向同一个 vector，引用计数增加
```

## Step 6 — Applicable Scenarios

### Dynamic Configuration Systems

When you need a key-value map where values can be of various different types, `any` is a natural choice:

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

This "property dictionary of arbitrary types" pattern is very common in game engines, GUI frameworks, and plugin systems. `any` provides enough flexibility to store values of different types, while guaranteeing type safety upon retrieval through `any_cast`.

### Property Dictionaries / Message Passing

In message passing or component systems, an entity might need to carry attributes of different types. `any` can be used to implement a generic attribute container:

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

When designing a plugin system, the interface between the host and the plugin might need to pass data of "types defined by either the host or the plugin." Since the types on both sides are invisible to each other at compile time, `any` can serve as a neutral transport container:

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

To gain a deeper understanding of the type erasure mechanism, let's write a minimal version of `any` by hand. Although this implementation is far less refined than the standard library version, it can help you understand exactly how `any` works internally.

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

First, `HolderBase` is the type-erased interface—it defines "the operations that any stored type must support" (getting type information, cloning itself), without exposing the specific type.

Second, `Holder<T>` is the concrete type wrapper—it inherits from `HolderBase` and provides implementations for each specific type. When you execute `MiniAny a = 42;`, what gets created internally is a `Holder<int>` instance.

Third, `mini_any_cast` restores type safety through `typeid` comparison—checking whether the stored type matches the requested type before retrieving the value.

The standard library's `std::any` is much more complex than this implementation: it has SBO optimization to avoid heap allocation for small objects, move semantics optimizations, and more flexible construction methods like `emplace`. But the core idea is exactly the same.

## Step 8 — When Not to Use any

Although `any` is flexible, most of the time it is not the best choice. Here are a few scenarios where you "should not use `any`":

**The set of types is known and limited**: If you know the value can only be one of `int`, `double`, or `std::string`, just use `variant<int, double, std::string>`. `variant` provides compile-time type checking and `visit`, and has better performance.

**You only need to express "has a value or does not"**: Use `optional<T>` instead of `any`. `optional` is lighter and has clearer semantics.

**It can be solved with templates**: If your function needs to accept parameters of different types, but does not need to store "values of different types" at runtime, templates are usually the better choice. Templates complete type dispatch at compile time, with zero runtime overhead.

**It can be solved with polymorphism**: If you have a group of related types that share an interface, virtual functions might be more appropriate than `any`. Virtual functions provide a type-safe interface, whereas `any` completely abandons interface constraints.

My general principle is: **use `variant` if you can instead of `any`, and use templates instead of runtime type erasure**. `any` is a last resort—only consider it in scenarios where all static approaches are inapplicable.

## An Embedded Perspective — Considerations for any in Resource-Constrained Environments

In embedded systems, `std::any` is usually not the first choice of tool. There are three reasons: first, the SBO buffer of `any` consumes extra RAM (typically 16–32 bytes), which is non-negligible overhead on an MCU (Microcontroller Unit) with only a few dozen KB of RAM. Second, large objects trigger heap allocation, and many embedded systems either have no heap or have very limited heap space. Third, the type checking of `any_cast` involves RTTI (Runtime Type Information), and in some embedded toolchains RTTI is disabled (to save code space).

If you truly need similar "dynamic type" functionality in an embedded project, a more recommended approach is to use `variant` + `enum` tags to implement a restricted version—all possible types are determined at compile time, requiring no RTTI and incurring no heap allocation.

## Summary

`std::any` is the most "dynamic" type-safe container in C++17. It achieves the ability to "store values of any type" through type erasure, and provides type safety checks upon retrieval through `any_cast`. Small Buffer Optimization ensures that the performance of small objects is not affected by heap allocation.

But the flexibility of `any` comes at a cost: it abandons compile-time type checking (all checks happen at runtime), it may trigger heap allocation (for large objects), and it does not support `visit`-style pattern matching. In the vast majority of scenarios, if your set of types is known, `variant` is the better choice. `any` is suited for scenarios that truly require "runtime polymorphism"—plugin systems, script engines, and dynamic configuration.

With our understanding of `any`, our type safety journey in ch04 comes to a close. From `enum class` to strong-typedefs, from `variant` to `optional` and then to `any`—the common theme of these tools is: **leverage the type system to catch as many errors as possible at compile time, minimizing runtime uncertainty**.

## Reference Resources

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast)
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)
- [Arthur O'Dwyer: Back to Basics - Type Erasure (CppCon 2019)](https://www.youtube.com/watch?v=tbUCHifyT24)
