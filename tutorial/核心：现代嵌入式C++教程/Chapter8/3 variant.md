# 现代嵌入式C++教程：`std::variant`

------

写这篇文章前请想像一个场景：你有一个盒子，有时候装 `int`，有时候装 `std::string`，有时候装别的东西。传统 `union` 是那种老派盒子——节省空间但没有标签，容易把 `int` 当成 `std::string` 来读，后果就是 undefined behavior。`std::variant` 就是现代的智能盒子：带标签、会照顾持有对象的构造/析构，并且在你试图错误读取时会大声给你抛出异常。

------

## 基本概念

`std::variant<Ts...>` 表示“在一时刻仅持有 `Ts...` 中某一个类型”的类型安全联合体。它记录当前持有哪一个类型，构造/析构正确处理，并且提供访问、检查和访问者（visitor）机制。

------

## 最简单的例子

```cpp
#include <variant>
#include <string>
#include <iostream>

int main() {
    std::variant<int, std::string> v;      // 默认构造，持有 int（索引0）
    v = 42;                                // 现在持有 int
    std::cout << std::get<int>(v) << "\n"; // 42

    v = std::string("hello variant");     // 现在持有 std::string
    // 安全访问：
    if (std::holds_alternative<std::string>(v)) {
        std::cout << std::get<std::string>(v) << "\n";
    }

    // 推荐方式：使用 std::visit（统一处理所有可能类型）
    std::visit([](auto &&x) { std::cout << x << "\n"; }, v);
}
```

- 默认构造会构造第一个备选类型（上例中是 `int`）。
- `std::get<T>(v)`：尝试以 `T` 访问，若 `v` 当前不是 `T`，会抛 `std::bad_variant_access`。
- `std::holds_alternative<T>(v)`：检查当前类型是否为 `T`。
- `std::visit(visitor, v)`：由 visitor（可调用对象）处理当前持有的值，是最推荐也最安全的访问方式。

------

## 为什么优于裸 `union`

`std::variant` 自动管理对象生命周期（会调用析构函数），在访问时进行类型检查（抛异常而不是 UB），并与 `std::visit` 配合能写出清晰的模式匹配风格代码。它也比 `std::any` 更“精确”——`std::any` 可以装任意类型，但类型信息查找不如 `variant` 明确，且没有 `visit` 的静态分支优势。

------

## 推荐访问方式：`std::visit` 与重载集合

`std::visit` 是 `variant` 的中心舞台。要同时处理多种类型，一个常见用法是利用“重载集”技巧来把多个 lambda 拼成一个可调用对象：

```cpp
#include <variant>
#include <iostream>
#include <string>

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>; // C++17 方便的模板推导

int main() {
    std::variant<int, double, std::string> v = 3.14;
    std::visit(overloaded{
        [](int i) { std::cout << "int: " << i << "\n"; },
        [](double d) { std::cout << "double: " << d << "\n"; },
        [](const std::string &s) { std::cout << "string: " << s << "\n"; }
    }, v);
}
```

好处是清晰、类型安全，编译器会在你漏写某个类型时给出提示（视情况而定），读代码的人也能一眼看出每个分支要干什么。

------

## 常见误区与坑（务必读）

1. **不要用 `std::get<T>` 在不确定类型时访问** —— 它会抛 `std::bad_variant_access`。用 `std::get_if<T>` 或 `std::visit` 更安全。

2. **默认构造选第一个类型** —— `std::variant<int, std::string> v;` 会构造 `int`（值为 0），不是空的。需要“空值”语义可用 `std::monostate` 作为第一种类型。

3. **不能直接做递归 `variant`** —— `std::variant<int, std::variant<...>>` 会引起不完整类型问题。用 `std::unique_ptr` 或 `std::shared_ptr` 包装递归类型：

   ```cpp
   struct Node {
       std::variant<int, std::unique_ptr<Node>> next;
   };
   ```

4. **引用类型问题** —— `std::variant<int&, double&>` 是可写的但容易错。通常用 `std::reference_wrapper<T>` 或保存指针更明确。

5. **异常与析构** —— 如果替换当前持有类型时新类型的构造抛异常，`variant` 保证要么保持原值，要么变成 `valueless_by_exception`（可以用 `v.valueless_by_exception()` 检查）。此状态下 `std::visit` 将抛出 `bad_variant_access`。

6. **大小（内存）** —— `variant` 的大小由最“宽”类型决定（加上一点元数据），有时比单个类型要大。权衡内存与便利性。

------

## 进阶技巧

- **安全获取指针**：`std::get_if<T>(&v)` 返回 `T*` 或 `nullptr`，避免异常。
- **按索引访问**：`std::get<0>(v)` 直接按索引（不推荐，容易错位，但在模板编程中有用）。
- **in-place 构造**：`v.emplace<std::string>("hello")` 可以避免拷贝/临时并直接在 `variant` 内构造目标类型。
- **元编程查看类型**：`std::variant_size_v<decltype(v)>`，`std::variant_alternative_t<I, decltype(v)>`。
- **处理无值状态**：调用 `v.valueless_by_exception()` 检查是否处于异常导致的无值状态（极少见，但处理代码中可以考虑）。
- **noexcept/移动语义**：`variant` 的移动/拷贝/析构是否 `noexcept` 取决于备选类型的对应操作是否 `noexcept`。注意移动/拷贝开销可能来自某个昂贵备选类型。

------

## 把 `std::variant` 用到项目里去

假设你在处理一个消息队列，消息有三种：心跳（`Heartbeat`）、文本（`Text`），和二进制（`Blob`）。`std::variant` 非常适合：

```cpp
#include <variant>
#include <string>
#include <vector>
#include <iostream>

struct Heartbeat { int id; };
struct Text { std::string s; };
struct Blob { std::vector<uint8_t> data; };

using Message = std::variant<Heartbeat, Text, Blob>;

void process(Message const& m) {
    std::visit(overloaded{
        [](Heartbeat const& h) { std::cout << "HB " << h.id << "\n"; },
        [](Text const& t)      { std::cout << "Text: " << t.s << "\n"; },
        [](Blob const& b)      { std::cout << "Blob size: " << b.data.size() << "\n"; }
    }, m);
}
```

优势是清晰、类型安全；如果增加一种消息类型，你需要在 `visit` 处显式处理（这通常是好事）。

------

## `std::variant` 与 `std::any`、`boost::variant` 的比较

- `std::variant`：**类型列表固定**、编译时已知、支持 `visit`，最适合有限且已枚举的类型集合。
- `std::any`：可装任意类型，运行时类型检查繁琐，不适合做“有穷多种可能”的替代。
- `boost::variant`：`std::variant` 的前身，功能类似，历史项目可能还在用，但新代码优先选标准库的 `std::variant`。