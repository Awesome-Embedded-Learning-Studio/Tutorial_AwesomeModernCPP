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
  - "std::function、std::invoke 与可调用对象"
---

# std::any 与类型擦除

笔者相信不少人头一回见到 `std::any`，反应都差不多：这不就是给 `void*` 换了个包装吗？能有什么用呢？当年笔者还吐槽标准库不干正事。后来真上手做一个插件系统的配置模块，看法才稍微的改变了（注意，只是稍微改变，我仍然觉得`std::any` 没很大的用！）

C语言的 `void*` 把类型信息全丢了，您从它手里取值，全得靠咱们猜。当然，您也可以在旁边另配一份信息记录类型，可信息和数据是分开存的，一不留神就对不上了，状态不一致的大毛病就是这么来的。但是 `std::any` 可就不一样了，什么类型咱们都能往里装，但它**记得**自己装的是什么。您拿错误的类型去取值，它抛异常提醒您，而不是塞给您一段内存垃圾。

`std::any` 是 C++17 引入的，它承诺的事情就一件：把任意类型的值存进去，需要的时候咱们再安全地取回来。撑起这套能力的，是前面讲 `std::function` 的那一篇里拆过的类型擦除。存的时候把具体类型藏起来，取的时候靠类型检查把安全性找回来。

当然，这个东西其实后面也不如 `variant`，至于为什么不如我们前面讲的 `variant`，请您继续看看（笑。

## any 要解决的问题

C++ 是一门静态类型的语言，编译器在编译期就得知道每个变量和表达式的类型。可有些需求偏偏反过来：容器里存的到底是什么类型，得等程序真的跑起来，咱们才知道。下面这些场景咱们都遇到过：

插件系统的属性映射就是一类：不同插件注册的属性，可能是整数啊、字符串啊，也可能是咱们没见过的自定义结构体。脚本引擎的变量绑定也是：脚本里的变量在运行时可以是任何类型。序列化和反序列化的框架同样算：解析 JSON 或 XML 的时候，某些字段的类型，咱们得看到具体数据才能确定。

C 语言给这类需求的传统答案是 `void*`。可 `void*` 是完全不设防的：您把一个 `int*` 转成 `void*` 存起来，取出的时候转成 `double*` 去用，编译器连一句警告都不会给咱们，运行时您拿到的就是一堆垃圾数据。`std::any` 想给的，是跟 `void*` 一样的“存什么类型都行”的灵活，同时把取值环节的类型安全也保住了。

## any 的基本用法

### 构造与赋值

`std::any` 可以持有任何可拷贝构造的类型的值，咱们直接看例子：

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

和 `variant` 不同的是，`any` 的备选类型列表完全开放：您存什么类型都行，声明的时候一个都不用枚举。它的灵活就来自这里，性能不如 `variant` 的根子也在这里。

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

咱们看 `std::any_cast` 的两种重载：传引用的版本，类型不匹配的时候会抛出 `std::bad_any_cast` 异常。传指针的版本就不抛了，类型不匹配的时候，它安安静静地还您一个 `nullptr`。您要是得频繁检查类型，指针版本就更划算了，异常的开销一点都没有。

还有一处容易吃亏的地方，咱们提前说一句：`std::any_cast<int>(a)` 返回的是值的**拷贝**而非引用。您想修改 `any` 内部的值，取引用的写法就得换成 `std::any_cast<int&>(a)`：

```cpp
std::any a = 42;
std::any_cast<int&>(a) = 100;  // 修改 any 内部的值为 100
// int copy = std::any_cast<int>(a); copy = 200;  // 只修改了拷贝，any 内部没变
```

## 类型擦除与 Small Buffer Optimization

咱们把 `any` 拆开看，类型擦除（type erasure）落到它身上是这样的：`any` 的内部养着一个“概念接口”，它知道怎么销毁手里的值、怎么复制、怎么交出自己的 `type_info`，唯独不知道值的具体类型。操作的分派，靠的是函数指针或者虚函数。

您写下 `std::any a = 42;` 的时候，`any` 的内部会创建一个“包装器”对象：包装器持有 `int` 的值，前面说的那几样操作，给出的实现也全在它身上。剩下的 `any` 本体，手里只留一个指向包装器的指针或者引用。

为了照顾小对象的性能，主流实现都做了 Small Buffer Optimization，咱们叫它 SBO（小缓冲区优化）。持有的类型足够小的时候（阈值通常只有几个指针的大小，libstdc++ 8 字节、libc++ 24 字节、MSVC 的 48 字节），值就直接放在 `any` 对象内部的缓冲区里了，连堆分配都省了。值要是大过了 SBO 的阈值，堆分配就免不了了。

```cpp
std::cout << "sizeof(std::any): " << sizeof(std::any) << "\n";
// 典型输出：16、32 或 64（libstdc++ / libc++ / MSVC 依次对应）
// 这包括了 SBO 缓冲区 + 类型信息指针 + 管理数据

// 小对象：栈上存储（SBO 生效）
std::any small = 42;
// 大对象：堆上分配
std::any large = std::vector<int>(1000000, 0);
```

两种情况下的存储布局，咱们画成图对比：

![std::any 的存储布局：小对象内联（SBO）与大对象堆分配](./05-any-layout.drawio)

有了 SBO，`int`、`double` 这些放得进 SBO 缓冲区的小类型，装进 `any` 的开销就非常小了，堆分配是一次都没有的，多的只是一次间接寻址。可大型对象（比如大 `vector`、大 `string`）的待遇就反过来了，咱们每拷贝一次 `any`，就得付一次堆分配加一次深拷贝的代价，开销再也忽略不下去了。

## 四种机制对比

`any`、`variant`、`void*`、`union` 都能“存不同类型的值”，可它们的定位和适用场景截然不同。咱们拉一张表来对比：

| 特性         | `std::any`            | `std::variant`        | `void*`    | `union`  |
| ------------ | --------------------- | --------------------- | ---------- | -------- |
| 类型安全     | 运行时检查            | 编译期检查            | 无检查     | 无检查   |
| 备选类型     | 任意                  | 固定列表              | 任意       | 固定列表 |
| 生命周期管理 | 自动                  | 自动                  | 手动       | 手动     |
| 堆分配       | 可能（SBO 外）        | 无                    | 取决于使用 | 无       |
| `visit` 支持 | 无                    | 有                    | 无         | 无       |
| 内存开销     | 中等                  | 最大备选类型 + 元数据 | 一个指针   | 最大成员 |
| 类型查询     | `type()` + `any_cast` | `holds_alternative`   | 无法查询   | 无法查询 |

从表里咱们能读出一条清楚的判断：您要是能在编译期把所有可能的类型枚举出来，`variant` 几乎总是比 `any` 更好的选择。`variant` 给的是编译期的类型检查，没有堆分配的负担，`visit` 的支持也是现成的。咱们补一句限定：编译期定下来的，是备选列表和 `visit` 的穷尽。真到用 `get` 取值的时候类型对不上，抛出来的照样是运行时的 `bad_variant_access` 异常。只有类型列表没法在编译期确定的时候（插件系统、脚本引擎这些），`any` 的价值才真正不可替代。

至于剩下的 `void*` 和 `union`，`any` 和 `variant` 已经把它们的活儿接了过去，给出的还都是更安全的用法。除了 `variant` 那一篇提过的、内存极端受限时手写 `union` 加 `enum` 标签的老方案，在现代 C++ 里咱们基本找不到用它们的正当理由了。

## any 的性能特征

您想把 `any` 用对，就得把它的性能开销摸清楚，咱们分三块看。

**构造和赋值的开销**：SBO 范围内的类型（阈值就是前面说的几个指针大小），咱们要付的就是一次值拷贝加少量的元数据设置，跟拷贝原始类型的速度差不多。类型要是超过了 SBO 的阈值，替换值的时候就得付一次 `new` 和一次 `delete`。

**取值的开销**：咱们取值用的 `std::any_cast`，做的是一次 `typeid` 比较（确认存着的类型和请求的类型匹配），之后的 `static_cast` 也只有一次。具体就是一次指针比较加一次类型信息的查找，开销小到咱们平时不用惦记。

**拷贝的开销**：拷贝 `any` 的时候，它持有的值会被深拷贝一份，大对象遇到的就是完整的深拷贝。您想躲开的话，咱们可以拿 `std::any` 去包一层 `std::shared_ptr<T>`，这样咱们再拷贝 `any`，增加的只是引用计数，底层对象也就不会被拷贝了。

```cpp
// 避免大对象拷贝：用 shared_ptr 包裹
auto big_data = std::make_shared<std::vector<int>>(1000000, 0);
std::any a = big_data;  // 拷贝 shared_ptr，不拷贝 vector

auto retrieved = std::any_cast<std::shared_ptr<std::vector<int>>>(a);
// retrieved 指向同一个 vector，引用计数增加
```

## 适用场景

### 动态配置系统

您要的如果是一个键值映射，而值又可能是各种不同的类型，`any` 就是很自然的选择：

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

“任意类型的属性字典”这样的模式，您在游戏引擎、GUI 框架、插件系统里都会读到它。存各种类型的值，`any` 给的灵活性是够的，`any_cast` 也在取值的时候把类型安全保住了。

### 属性字典 / 消息传递

咱们再看消息传递或者组件系统：实体（Entity）常常需要携带不同类型的属性，咱们拿 `any` 做通用的属性容器正合适：

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

您设计插件系统的时候，宿主和插件之间的接口，可能要传递“宿主和插件各自定义的类型”的数据。双方的类型在编译期互相看不见，`any` 就能当一个中性的传递容器：

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

## 手写一个简化版的 any

为了把类型擦除的机制看得更透，咱们就亲手写一个极简版的 `any`。它的完善程度当然远不如标准库，但 `any` 的内部到底怎么转，写完这一遍您就清楚了。

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

写完咱们回头看，`any` 赖以工作的三样东西全在这个简化版里露了面：

咱们跟着 `MiniAny a = 42;` 这一行往下走：构造走的是那个模板构造函数，`42` 被搬进堆上的 `Holder<int>`。存进去的类型换成别的也一样，`Holder<T>` 照着模板给每个具体类型现做一份对应的包装，值和对应的实现都由它自己带着。

可 `holder_` 的类型明明是 `unique_ptr<HolderBase>`，而它能指住任何一种 `Holder<T>`，您想过这是为什么吗？答案就是 `HolderBase`：它只规定了两件事，交出自己的类型信息、克隆自身，值的具体类型，它倒是一概不问。类型擦除的接口，说的就是它了。

还有取值用的 `mini_any_cast`，类型安全是靠 `typeid` 比对找回来的：咱们取值之前，它确认过存着的类型和请求的类型一致。

标准库的 `std::any` 当然比咱们这版复杂得多：小对象靠 SBO 免掉了堆分配，有移动语义的优化，还有 `emplace` 这些更灵活的构造方式。但咱们手写版表达的思路，和标准库的一模一样。

## 什么时候不该用 any

`any` 倒是灵活，大多数时候它并不是最好的选择。咱们挨个看几种“别用 `any`”的情况：

- **类型的集合已知而且有限**：您知道值只可能是 `int`、`double`、`std::string` 里的某一个，咱们就直接写 `variant<int, double, std::string>`。编译期的类型检查、`visit`、还有更好的性能，`variant` 全都提供了。
- **要表达的只是“有值或无值”**：咱们用 `optional<T>` 就好，`any` 就不用请了。`optional` 的量级更轻，想表达的语义也更明确。
- **模板就能解决的情况**：您的函数要接受不同类型的参数，但您不需要在运行时存“不同类型的值”，模板通常就是更好的选择。类型分派在编译期就完成了，运行时连分派这道工序都省下了。
- **多态能解决的情况**：您手里要是一组共享接口的相关类型，虚函数可能就比 `any` 合适了。虚函数给的是类型安全的接口，`any` 则干脆放弃了接口的约束。

`any` 是笔者留在最后的手段，所有静态方案都不适用的场景，咱们才会考虑它。

## 小小的彩蛋——嵌入式视角：any 在资源受限环境中的考量

到了嵌入式系统里，`std::any` 通常就不是首选了，原因咱们数得出三条。头一条是 SBO 缓冲区的 RAM 开销（就是前面 sizeof 量出来的 16 到 64 字节），RAM 只有几十 KB 的 MCU 上，咱们没法当它不存在。然后是堆的问题：大对象会带来堆的分配，而很多嵌入式系统要么根本没有堆，要么堆的空间小得可怜。最后是 RTTI 的问题，`any_cast` 的类型检查依赖 RTTI（运行时类型信息），有的工具链出于代码空间的考虑，干脆把 RTTI 禁用了。

您要真在嵌入式项目里需要类似的“动态类型”功能，更稳妥的做法是拿 `variant` 加 `enum` 标签做一个受限的版本。所有可能的类型在编译期就定下来，咱们既不需要 RTTI，也没有堆分配的问题，两头都省了。

## 参考资源

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any)
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast)
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast)
- [Arthur O'Dwyer: Back to Basics - Type Erasure (CppCon 2019)](https://www.youtube.com/watch?v=tbUCHifyT24)
