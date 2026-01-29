# 现代C++嵌入式教程——`consteval` 与 `constinit`

在嵌入式开发里，**把能做的事尽量移到编译期**，通常可以换来更小的二进制、确定性的启动行为以及更少的运行时开销。C++20 在这一方向上增加了两个非常有用但容易被误用的关键字：`consteval`（立即求值函数 / immediate functions）与 `constinit`（保证静态存储的初始化形态）。它们看起来像“多余的语法糖”，但在嵌入式场景中能解决真实的问题：生成编译期查表、保证静态生命周期变量的初始化属性、把不可变生成逻辑从固件运行时代码里剥离出去、以及以编译期断言的方式捕捉潜在的初始化顺序错误。

------

## `consteval`：什么是“立即求值”函数（immediate function）

概念上，`consteval` 用于声明**必须在编译期求值**的函数或构造函数。用更直接的话说：凡是 `consteval` 的函数，**任何被潜在求值的调用**都必须产生一个常量表达式，否则编译失败。它是 `constexpr` 的严格超集（或者说更强的版本）：`constexpr` 的函数可以在编译期或运行时求值，`consteval` 则**只允许编译期求值**。这是 `consteval` 的核心语义。

------

### 简单的 `consteval` 阶乘（用于编译期数组大小）

```cpp
// file: consteval_fact.cpp
#include <array>
#include <cstddef>

consteval std::size_t factorial_consteval(std::size_t n) {
    return n <= 1 ? 1 : n * factorial_consteval(n - 1);
}

constexpr std::size_t N = factorial_consteval(6); // 编译期求值 -> N == 720
static_assert(N == 720);

std::array<int, N> lut{}; // 使用编译期计算的大小，避免运行时计算
```

这个例子很直接：`factorial_consteval` 在编译期展开，返回常量，用于定义数组大小或非类型模板参数（NTTP）。

------

### 编译期字符串哈希（用于消息/命令 ID）

在嵌入式固件中，常见需求是把 ASCII 命令名映射为整数 ID 用于 switch/dispatch。用 `consteval` 我们可以把哈希的实现强制在编译期运行，并在编译时检测冲突（配合 `static_assert`）。

```cpp
// file: id_hash.hpp
#include <cstdint>
#include <cstddef>

consteval std::uint32_t fnv1a32_const(const char* s, std::size_t n) {
    std::uint32_t h = 0x811c9dc5u;
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint8_t>(s[i]);
        h *= 0x01000193u;
    }
    return h;
}

template <std::size_t N>
consteval std::uint32_t id_from_literal(const char (&s)[N]) {
    // N includes trailing '\0'
    return fnv1a32_const(s, N - 1);
}

// 用法示例
constexpr auto id_led_on  = id_from_literal("LED_ON");   // 在编译期计算
constexpr auto id_led_off = id_from_literal("LED_OFF");
static_assert(id_led_on != id_led_off); // 编译期保证不同
```

这个模式在嵌入式协议解析、命令表、日志 ID 等场景非常好用：既保证不在运行时做字符串哈希，也能在构建时检测重复 ID。

------

### `consteval` 构造函数（立即构造常量对象）

C++20 允许将 `consteval` 应用于构造函数，借此强制该类型只能以编译期常量构造。这在你希望某类实例仅存在于编译期（比如用于元数据或编译期描述）的场景非常有用。

```cpp
// file: meta_tag.hpp
#include <array>
#include <cstddef>

struct MetaTag {
    const char* name;
    std::uint32_t id;

    consteval MetaTag(const char* n, std::uint32_t i) : name(n), id(i) {}
};

consteval MetaTag make_tag(const char* s, std::uint32_t id) {
    return MetaTag{s, id};
}

constexpr auto TAG1 = make_tag("TAG1", 0x01);
// MetaTag runtime_tag{"RUNTIME", 0x02}; // error: constructor is consteval -> must be compile-time
```

上面 `MetaTag` 的构造被强制为编译期构造，任何试图在运行时构造对象的尝试都会导致编译失败。这对于“编译期声明的元数据”非常直接且安全。

------

### `if consteval` — 在编译期和运行期选择不同实现

C++20 引入了 `if consteval` 控制流，允许函数体在编译期和运行期使用不同代码路径。对于像 `constexpr` 函数这种既可能在编译期也可能在运行期执行的函数，这个特性很有用；在 `consteval` 中 `if consteval` 的编译期路径必须成立（因为 `consteval` 本身强制编译期）。

```cpp
#include <iostream>
#include <string_view>

constexpr std::string_view greet_impl() {
    if consteval {
        // compile-time code path —— 可用来生成编译期字符串
        return "hello, compile-time";
    } else {
        // runtime code path
        return "hello, runtime";
    }
}

int main() {
    constexpr auto s = greet_impl(); // 这里走 consteval 路径（编译期）
    std::cout << s << "\n"; // prints: hello, compile-time
}
```

`if consteval` 的语义与 `if constexpr` 不同：`if consteval` 按“是否处于常量求值上下文”决定路径，而不是模板参数或类型特性。若你需要在一个 `constexpr` 函数在编译期/运行时选择不同实现，`if consteval` 是正确工具。

## `constinit`：保证静态存储的初始化形态

`constinit` 是为了解决静态存储持续对象的**初始化形态**问题而引入的关键字。它的核心含义是：当你把 `constinit` 应用于一个具有静态或线程存储期的变量时，**如果该变量需要动态初始化（dynamic initialization），则程序是 ill-formed（不合法）**。换句话说，`constinit` 要求该变量不能是动态初始化——它要么是常量初始化（constant initialization），要么至少不是动态初始化。用工程语言解释，就是用 `constinit` 可以把“我期望这个静态变量在加载时就确定好初始值，而不是在运行时通过构造函数初始化”这种意图固定在代码里，编译器会在编译期帮你检测。

在传统 C++（未使用 `constinit`）里，静态对象的初始化分为两类：

- **静态初始化（static initialization）**：包括零初始化与常量初始化（constant initialization），发生在程序加载阶段，顺序与链接单元无关。
- **动态初始化（dynamic initialization）**：需要运行时执行的初始化（例如非 `constexpr` 构造函数），其顺序在不同翻译单元之间是不确定的，从而引发所谓的 “静态初始化顺序灾难”（static initialization order fiasco）。

`constinit` 的价值在于：当你需要一个**可变的全局/静态变量**（不能用 `constexpr`，因为它要在运行时修改），但你又希望它**在静态初始化阶段就有确定的初始值**，那么你可以用 `constinit` 来确保这一点。若你错误地为它提供了一个需要动态初始化的表达式，编译器会给你一个错误，让你在构建阶段修正。

------

#### 示例 1：防止意外的动态初始化

```cpp
// file: constinit_example.cpp
#include <array>

// 假设 LUT 必须在加载时就存在，且随后可被修改（例如后续由 bootloader 写入）
constinit std::array<int, 4> g_table = {1, 2, 3, 4}; // OK：常量初始化（aggregate init）

// 若把初始化写成需要运行时计算的形式，编译器将拒绝
// int init_via_runtime();
// constinit std::array<int,4> g_table2 = [](){ return std::array<int,4>{ compute() }; }(); // error: dynamic init forbidden
```

`constinit` 在这里成为一种“保证” —— 它保证 `g_table` 被常量初始化（或至少不是动态初始化）。如果你试图通过 lambda 或运行时代码构造它，编译器会报错，让你改成 `constexpr` / `consteval` 生成或采用延迟 (function-local static) 访问模式。

------

#### 示例 2：与 `constexpr` 的关系

`constexpr` 变量本身会进行常量初始化（因此通常不需要 `constinit`），一个 `constexpr` 变量隐含了“常量初始化”的属性。所以 `constexpr` 和 `constinit` 的意图不同：`constexpr` 表示“值在编译时固定且不可变”；`constinit` 表示“我需要一个编译期可确定的初始化（以避免动态初始化），但我可能在运行时修改这个对象”。注意：在语法上把二者写在一起是没有意义的（`constexpr` 会隐含为常量而与 `constinit` 的检查逻辑冲突），通常不会也不需要同时使用这两个关键字。

------

#### 示例 3：避免 SIOF（Static Initialization Order Fiasco）

假设你有两个文件 `a.cpp` 和 `b.cpp`，两个静态变量互相依赖。没有 `constinit`，如果初始化其中一个依赖另一个的运行时代码，就可能在另一个还未初始化前被访问，导致未定义行为。`constinit` 能把这类错误在编译期检测到（当初始化不是常量初始化时就会报错），迫使你使用更安全的模式（比如函数内的局部静态、或把依赖改成编译期生成）。这在大型固件里非常实用，因为 SIOF 导致的错误常常只在特定链接顺序下出现，难以复现

## 最后

`consteval` 与 `constinit` 并不是“玩语法”而已——它们在嵌入式工程里能让你把“构建时可确定的东西”真正固定在镜像里，同时用编译器把很多会在运行时露出的错误前移为编译期错误。实践中，常见的好用模式是：把查表、哈希、ID 生成、协议元数据这些工作用 `consteval` 生成；对那些“需要写入镜像但又需要可写”的数据体用 `constinit` 声明（并确保初始化表达式可在编译期求值）。这样既能得到小巧、快速的固件，又能保证初始化行为在不同链接/部署环境下可预测、可复现。**当你能把东西在构建时确定，就把它放到构建时；当它必须在运行时初始化，就把初始化显式化并控制可见性与顺序。** `consteval` 与 `constinit` 就是让这条规则以语法与错误检查的形式落地的工具。