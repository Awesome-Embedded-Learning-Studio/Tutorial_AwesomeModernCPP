---
title: "std::any 与类型擦除"
description: "理解 any 的类型擦除机制、适用场景与性能特征"
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
cpp_standard: [17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 4: std::variant"
  - "Chapter 4: std::optional"
related:
  - "std::function 与类型擦除"
---

# std::any 与类型擦除

## 引言

我相信不少人第一次接触 `std::any` 的时候，反应是：这不就是 `void*` 的包装吗？(因为我最快是第一反应真是整个, 还吐槽标准库怎么正事不做)有什么用？后来在做一个插件系统的配置模块时才意识到——`void*` 丢掉了所有类型信息，你从它里面取值的时候完全靠猜(我的另一个意思是多少的带一点信息,但是我们都知道额外信息表述, 很容易造成状态不一致的大毛病)；而 `std::any` 虽然也能装任何类型，但它**记得**自己装的是什么。当你用错误的类型去取值时，它会抛异常而不是给你一段内存垃圾。

`std::any`（C++17 引入）的核心能力是"存储任意类型的值，并在需要时安全地取回"。它是通过一种叫做"类型擦除"（type erasure）的技术来实现的——在存储时隐藏具体类型信息，在取出时通过类型检查来恢复安全性。这一章我们来深入理解 `any` 的机制、适用场景，以及为什么大多数时候你其实应该用 `variant` 而不是 `any`。

## 第一步——any 的设计动机

C++ 是一门静态类型语言，编译器在编译期就必须知道每个变量和表达式的类型。但有时候你确实需要"在运行时才知道存什么类型"的容器。经典的场景包括：

插件系统的属性映射——不同插件可能注册不同类型的属性（整数、字符串、自定义结构体）。脚本引擎的变量绑定——脚本中的变量在运行时可以是任何类型。序列化/反序列化框架——在解析 JSON 或 XML 时，某些字段的类型只有在看到具体数据后才能确定。

在 C 语言中，这类需求通常用 `void*` 满足。但 `void*` 是完全类型不安全的——你把一个 `int*` 转成 `void*` 存起来，取出时转成 `double*` 来用，编译器不会有任何警告，运行时你会得到一堆垃圾数据。`std::any` 的目标就是提供和 `void*` 一样的"存什么类型都行"的灵活性，同时保证取值时的类型安全。

## 第二步——any 的基本用法

### 构造与赋值

`std::any` 可以持有任何可拷贝构造的类型的值：

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

和 `variant` 不同，`any` 的备选类型列表是完全开放的——你可以存任何类型，不需要在声明时枚举出来。这正是它的灵活性所在，也是它性能不如 `variant` 的根源。

### 检查与取值

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

`std::any_cast` 有两种重载：传引用的版本在类型不匹配时抛出 `std::bad_any_cast` 异常；传指针的版本在类型不匹配时返回 `nullptr`。如果你需要频繁检查类型，指针版本更高效（不涉及异常开销）。

⚠️ 这里有一个容易踩的坑：`std::any_cast<int>(a)` 返回的是值的**拷贝**，不是引用。如果你想修改 `any` 内部的值，需要用 `std::any_cast<int&>(a)` 来获取引用：

```cpp
std::any a = 42;
std::any_cast<int&>(a) = 100;  // 修改 any 内部的值为 100
// int copy = std::any_cast<int>(a); copy = 200;  // 只修改了拷贝，any 内部没变
```

## 第三步——类型擦除与 Small Buffer Optimization

`std::any` 的实现基于类型擦除（type erasure）技术。简单来说，`any` 内部维护一个"概念接口"——它知道如何销毁所持有的值、如何复制它、如何获取它的 `type_info`——但不知道值的具体类型。这些操作通过函数指针或虚函数来分派。

当你执行 `std::any a = 42;` 时，`any` 在内部创建一个"包装器"对象，这个包装器持有 `int` 类型的值，并提供了上述操作的实现。`any` 本身只保存一个指向这个包装器的指针（或引用）。

为了优化小对象的性能，主流标准库实现都采用了 Small Buffer Optimization（SBO）。当所持有的类型足够小（通常和 `std::string` 差不多大或更小）时，值直接存储在 `any` 对象内部的缓冲区中，不需要堆分配。只有当值超过 SBO 阈值时，才会在堆上分配内存。

```cpp
std::cout << "sizeof(std::any): " << sizeof(std::any) << "\n";
// 典型输出：16 或 32（取决于实现）
// 这包括了 SBO 缓冲区 + 类型信息指针 + 管理数据

// 小对象：栈上存储（SBO 生效）
std::any small = 42;
// 大对象：堆上分配
std::any large = std::vector<int>(1000000, 0);
```

SBO 的存在意味着：对于 `int`、`double`、小型结构体这些常见类型，`any` 的性能开销是非常小的——没有堆分配，只是一次额外的间接寻址。但对于大型对象（比如大 `vector`、大 `string`），每次拷贝 `any` 都会触发一次堆分配和一次深拷贝，这个开销是不可忽略的。

## 第四步——any vs variant vs void* vs union

这四种机制都能实现"存不同类型的值"，但它们的定位和适用场景截然不同。我们用一个表格来对比：

| 特性 | `std::any` | `std::variant` | `void*` | `union` |
|------|-----------|---------------|---------|---------|
| 类型安全 | 运行时检查 | 编译期检查 | 无检查 | 无检查 |
| 备选类型 | 任意 | 固定列表 | 任意 | 固定列表 |
| 生命周期管理 | 自动 | 自动 | 手动 | 手动 |
| 堆分配 | 可能（SBO 外） | 无 | 取决于使用 | 无 |
| `visit` 支持 | 无 | 有 | 无 | 无 |
| 内存开销 | 中等 | 最大备选类型 + 元数据 | 一个指针 | 最大成员 |
| 类型查询 | `type()` + `any_cast` | `holds_alternative` | 无法查询 | 无法查询 |

从这个对比中可以看出：**如果你能在编译期枚举出所有可能的类型，`variant` 几乎总是比 `any` 更好的选择**。`variant` 提供编译期类型检查、没有堆分配、支持 `visit`。只有在类型列表无法在编译期确定的情况下（插件系统、脚本引擎等），`any` 才有不可替代的价值。

`void*` 和 `union` 在现代 C++ 中基本没有正当的使用理由了——`any` 和 `variant` 分别覆盖了它们的适用场景，而且更安全。

## 第五步——any 的性能特征

理解 `any` 的性能开销，对于正确使用它至关重要。

**构造/赋值开销**：对于 SBO 范围内的类型（通常不超过 32 字节左右），构造和赋值涉及一次值拷贝和少量元数据设置，基本和拷贝原始类型一样快。对于超过 SBO 阈值的类型，会触发一次 `new` 和一次 `delete`（在替换值时）。

**取值开销**：`std::any_cast` 需要进行一次 `typeid` 比较（检查存储的类型是否和请求的类型匹配），然后是一次 `static_cast`。这个开销非常小——就是一次指针比较加上一次类型信息查找。

**拷贝开销**：拷贝 `any` 会深拷贝它持有的值。对于大对象，这是一次完整的深拷贝。如果你需要避免这个开销，可以考虑用 `std::any` 包裹 `std::shared_ptr<T>`——这样拷贝 `any` 只是增加引用计数，不会拷贝底层对象。

```cpp
// 避免大对象拷贝：用 shared_ptr 包裹
auto big_data = std::make_shared<std::vector<int>>(1000000, 0);
std::any a = big_data;  // 拷贝 shared_ptr，不拷贝 vector

auto retrieved = std::any_cast<std::shared_ptr<std::vector<int>>>(a);
// retrieved 指向同一个 vector，引用计数增加
```

## 第六步——适用场景

### 动态配置系统

当你需要一个键值映射，而值可以是各种不同类型时，`any` 是一个自然的选择：

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

这种"任意类型的属性字典"模式在游戏引擎、GUI 框架、插件系统中非常常见。`any` 提供了足够的灵活性来存储不同类型的值，同时通过 `any_cast` 保证了取值时的类型安全。

### 属性字典 / 消息传递

在消息传递或组件系统中，实体（Entity）可能需要携带不同类型的属性。`any` 可以用来实现一个通用的属性容器：

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

### 插件接口

当你设计一个插件系统时，宿主和插件之间的接口可能需要传递"宿主和插件各自定义的类型"的数据。由于双方的类型在编译期互不可见，`any` 可以作为一个中性的传递容器：

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

## 第七步——手写一个简化版的 any

为了更深入地理解类型擦除的机制，我们来手写一个极简版的 `any`。这个实现虽然远不如标准库的版本完善，但它能帮你理解 `any` 内部到底是怎么工作的。

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

这个简化实现揭示了 `any` 的三个核心机制：

第一，`HolderBase` 是类型擦除的接口——它定义了"任何被存储的类型都必须支持的操作"（获取类型信息、克隆自身），但不暴露具体类型。

第二，`Holder<T>` 是具体的类型包装——它继承了 `HolderBase`，为每个具体类型提供了实现。当你执行 `MiniAny a = 42;` 时，内部创建的是 `Holder<int>` 实例。

第三，`mini_any_cast` 通过 `typeid` 比对来恢复类型安全——在取出值之前检查存储的类型是否和请求的类型一致。

标准库的 `std::any` 比这个实现复杂得多：它有 SBO 优化避免小对象的堆分配，有移动语义优化，还有 `emplace` 等更灵活的构造方式。但核心思想是一模一样的。

## 第八步——何时不用 any

虽然 `any` 很灵活，但大多数时候它并不是最好的选择。以下是几个"不要用 `any`"的场景：

**类型集合已知且有限**：如果你知道值只可能是 `int`、`double`、`std::string` 中的某一个，直接用 `variant<int, double, std::string>`。`variant` 提供编译期类型检查和 `visit`，性能也更好。

**只需要表达"有值或无值"**：用 `optional<T>` 而不是 `any`。`optional` 更轻量、语义更明确。

**可以用模板解决**：如果你的函数需要接受不同类型的参数，但不需要在运行时存储"不同类型的值"，模板通常是更好的选择。模板在编译期就完成了类型分派，没有任何运行时开销。

**可以用多态解决**：如果你有一组相关的类型，它们共享某个接口，虚函数可能比 `any` 更合适。虚函数提供了类型安全的接口，而 `any` 则完全放弃了接口约束。

笔者的一般原则是：**能用 `variant` 就不用 `any`，能用模板就不用运行时类型擦除**。`any` 是最后的手段——只有在所有静态方案都不适用的场景下才考虑。

## 嵌入式视角——any 在资源受限环境中的考量

在嵌入式系统中，`std::any` 通常不是首选工具。原因有三：第一，`any` 的 SBO 缓冲区会占用额外的 RAM（通常 16-32 字节），这在 RAM 只有几十 KB 的 MCU 上是不可忽视的开销。第二，大对象会触发堆分配，而很多嵌入式系统要么没有堆，要么堆空间非常有限。第三，`any_cast` 的类型检查涉及 RTTI（运行时类型信息），在某些嵌入式工具链中 RTTI 是被禁用的（为了节省代码空间）。

如果你确实需要在嵌入式项目中使用类似的"动态类型"功能，更推荐的做法是用 `variant` + `enum` 标签来实现一个受限版本——所有可能的类型在编译期就确定，不需要 RTTI，也没有堆分配。

## 小结

`std::any` 是 C++17 中最"动态"的类型安全容器。它通过类型擦除技术实现了"存储任意类型值"的能力，同时通过 `any_cast` 在取值时提供类型安全检查。Small Buffer Optimization 保证了小对象的性能不受堆分配影响。

但 `any` 的灵活性是有代价的：它放弃了编译期类型检查（所有检查都在运行时），可能触发堆分配（对大对象），而且不支持 `visit` 风格的模式匹配。在绝大多数场景下，如果你的类型集合是已知的，`variant` 是更好的选择。`any` 适合那些真正需要"运行时多态"的场景——插件系统、脚本引擎、动态配置。

理解了 `any` 之后，我们 ch04 的类型安全之旅就告一段落了。从 `enum class` 到强类型 typedef，从 `variant` 到 `optional` 再到 `any`——这些工具的共同主题是：**利用类型系统在编译期捕获尽可能多的错误，把运行时的不确定性降到最低**。

## 参考资源

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast)
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)
- [Arthur O'Dwyer: Back to Basics - Type Erasure (CppCon 2019)](https://www.youtube.com/watch?v=tbUCHifyT24)
