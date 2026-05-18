---
title: "三路比较运算符 (<=>)"
description: "一次定义自动生成全部六种比较运算符的 C++20 语言特性"
chapter: 99
order: 12
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [20, 23]
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

# 三路比较运算符 <=>（C++20）

## 一句话

定义 `operator<=>` 即可让编译器自动生成 `<`、`<=`、`>`、`>=`、`==`、`!=` 全部六种比较运算符，告别手写比较代码。

## 头文件

`#include <compare>`（使用预定义比较类别时）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 三路比较 | `auto operator<=>(const T&) const = default;` | 编译器自动生成比较逻辑 |
| 手写三路比较 | `std::strong_ordering operator<=>(const T& rhs) const;` | 自定义比较语义 |
| 强序 | `std::strong_ordering` | 等价元素不可区分（如 int） |
| 弱序 | `std::weak_ordering` | 等价元素可区分但比较等价（如大小写不敏感字符串） |
| 偏序 | `std::partial_ordering` | 存在不可比较情况（如 NaN） |
| 相等运算符 | `bool operator==(const T&) const = default;` | 单独 defaulted 可自动生成 != |

## 最小示例

```cpp
// Standard: C++20
#include <compare>
#include <iostream>

struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
};

int main() {
    Point a{1, 2}, b{1, 3};
    std::cout << (a < b)  << "\n"; // true  (自动生成)
    std::cout << (a == b) << "\n"; // false (自动生成)
    std::cout << (a != b) << "\n"; // true  (自动生成)

    auto cmp = a <=> b;
    std::cout << (cmp < 0) << "\n"; // true (strong_ordering::less)
}
```

## 嵌入式适用性：中

- 编译期特性，零运行时开销——默认生成的比较代码与手写等价
- 适合传感器数据、协议头等需要字典序比较的结构体
- 需 C++20 支持（GCC 10+），部分嵌入式工具链尚未完全就绪
- 比较类别（strong/weak/partial）概念较抽象，团队需统一理解

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 10 | 19.20 |

## 另见

- [cppreference: Default comparisons](https://en.cppreference.com/w/cpp/language/default_comparisons)
- [cppreference: std::strong_ordering](https://en.cppreference.com/w/cpp/utility/compare/strong_ordering)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
