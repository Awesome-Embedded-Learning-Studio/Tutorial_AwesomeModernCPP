---
title: "if constexpr"
description: "编译期条件分支，根据模板参数在编译时选择性地编译代码路径"
chapter: 99
order: 13
tags:
  - host
  - cpp-modern
  - intermediate
  - if_constexpr
difficulty: intermediate
cpp_standard: [17, 20, 23]
---

<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# if constexpr（C++17）

## 一句话

在模板中根据编译期条件选择性地编译某个分支，被丢弃的分支甚至不需要通过语法检查——编译期多态的利器。

## 头文件

无（语言特性）

## 核心 API 速查

| 语法形式 | 说明 |
|---------|------|
| `if constexpr (cond) { ... }` | 若 `cond` 为 `true`，编译 then 分支 |
| `if constexpr (cond) { ... } else { ... }` | 二选一编译 |
| `if constexpr (cond1) { ... } else if constexpr (cond2) { ... } else { ... }` | 多分支链 |
| `if constexpr` 搭配概念 | `if constexpr (std::integral\<T\>)` 类型特征判断 |
| `if constexpr` 搭配 `requires` | (C++20) 更推荐用 concepts 重载替代 |

## 最小示例

```cpp
// Standard: C++17
#include <iostream>
#include <type_traits>

template <typename T>
auto print_type(const T& val) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "integral: " << val << "\n";
    } else if constexpr (std::is_floating_point_v<T>) {
        std::cout << "float: " << val << "\n";
    } else {
        std::cout << "other\n";
    }
}

int main() {
    print_type(42);     // integral: 42
    print_type(3.14);   // float: 3.14
    print_type("hi");   // other
}
```

## 嵌入式适用性：高

- 零运行时开销：条件在编译期求值，不满足的分支完全不生成代码
- 替代 SFINAE 和标签分派，大幅简化模板元编程的可读性
- 适合根据硬件平台、外设类型等编译期常量选择不同代码路径
- C++17 即可用，GCC 7+ 和 ARM Clang 6+ 均已支持

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## 另见

- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
