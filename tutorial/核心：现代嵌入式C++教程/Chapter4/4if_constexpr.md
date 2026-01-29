# `if constexpr`：把编译期分支写得像写注释 —— 工程味实战指南

笔者一直认为，在介于最近的现代C++和比较古典的C++98之间，大部分模板编程的使用方式，都是为了组合出特定目的而编写的，这种复杂性有时候并不是我们想要的。比如说，我们在之后学习的模板编程里，很多依赖模板的 `enable_if`、特化、SFINAE 花活本质上只是为了达成我们特定的编译期匹配目的。好在现在，我们有`if constexpr` 来化简绝大多数的场景了。

`if constexpr` 是 C++17 带来的一个小而强的工具：把“我们在编译期就能决定的分支”直接交给编译器处理。结果是代码更简洁、模板更易读，都能简化成一段清爽的 `if constexpr`。

------

## 为什么要关心它

- **减少模板特化/重载数量**：把不同类型的行为放在同一个模板体内，通过编译期条件分叉，逻辑集中，维护更方便。
- **提高可读性**：读代码时看到 `if constexpr` 就知道这是“类型/常量决定”的分支，不用再去找其他特化实现。
- **避免不必要的实例化错误**：被丢弃（discarded）的分支不会实例化，对某些在某类型下不成立的表达式不会导致编译失败。
- **性能清晰**：编译器在编译期就能删掉不需要的分支，最终生成的代码像你写了多份特化一样高效。

------

## `if constexpr (cond)` 怎么用？

1. `if constexpr (cond)` 要求 `cond` 在编译期可确定（常量表达式）。
2. 如果 `cond` 为 `true`，编译器仅编译 `then` 分支，`else`（如果有）会被丢弃，不会参与模板实例化（因此其内可能包含对当前类型不合法的代码）。
3. 反之亦然。
4. 如果 `cond` 不是编译期常量，会报错（因为 `if constexpr` 语义要求编译期求值）。（PS：现在有时候可以逐步退化为运行期，这个是新东西，所以不同的版本行为稍有不同）

------

## 与普通 `if` 的差别？

普通 `if` 是运行时判断，两分支都要合法；`if constexpr` 是编译期判断，只有被选择的分支需要合法（对于模板依赖条件尤其重要）。

------

### Case: 根据类型选择实现

场景：打印不同类型的值（工程中常用于日志格式化、序列化分支等）。

```cpp
#include <type_traits>
#include <iostream>

template <typename T>
void printValue(const T& v) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整型: " << v << "\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "浮点: " << v << "\n";
    } else {
        std::cout << "其他类型\n";
    }
}

// 使用
// printValue(42);      // 输出 整型: 42
// printValue(3.14);    // 输出 浮点: 3.14
```

优点：只需一个模板即可处理多种类型，逻辑集中，扩展分支也方便。

------

### Case: 替代 SFINAE / enable_if（把复杂特化变干净）

场景：为支持 `T::size()` 的类型使用 `.size()`，否则降级为其它实现。

```cpp
#include <type_traits>
#include <utility>

// 检测有无 size()（简单版）
template <typename T, typename = void>
constexpr bool has_size_v = false;

template <typename T>
constexpr bool has_size_v<T, std::void_t<decltype(std::declval<T>().size())>> = true;

template <typename T>
auto getSizeIfPossible(const T& t) {
    if constexpr (has_size_v<T>) {
        return t.size(); // 只有在 T 有 size() 时才编译
    } else {
        return std::size_t{0}; // 备用实现
    }
}
```

说明：如果用传统 SFINAE 或 `enable_if`，需要写多个重载或特化，代码量和维护成本都会上升。

------

### Case: 编译期递归（`constexpr` + `if constexpr`）

用法：编译期计算（例如元编程或常量生成）。

```cpp
constexpr uint64_t factorial(uint64_t n) {
    if constexpr (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

// constexpr auto f6 = factorial(6); // 720，编译期已计算
```

注意：这里的 `if constexpr` 与 `constexpr` 函数搭配，结果在编译期求值（如果使用场景允许）。