---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 将 tuple、pair、struct 或数组的元素一次性解构到多个变量
difficulty: beginner
order: 11
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: 结构化绑定
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

# 结构化绑定（C++17）

## 一句话

一行语法把 tuple、pair、struct 或数组的元素同时解构到独立变量中，省去 `std::get` 和逐字段访问。

## 头文件

无（语言特性）

## 核心 API 速查

| 绑定形式 | 语法 | 说明 |
|---------|------|------|
| 按值绑定 | `auto [a, b] = expr;` | 拷贝元素到新变量 |
| 左值引用 | `auto& [a, b] = expr;` | 绑定到原对象的引用 |
| 只读引用 | `const auto& [a, b] = expr;` | 常量引用，避免拷贝 |
| 转发引用 | `auto&& [a, b] = expr;` | 完美转发语义 |
| 数组解构 | `auto [a, b, c] = arr;` | 绑定到数组元素（数量须匹配） |
| pair 解构 | `auto [key, val] = *map_iter;` | 绑定到 pair 的 first/second |
| tuple 解构 | `auto [x, y, z] = tup;` | 绑定到 tuple-like 的 `get<I>` |
| struct 解构 | `auto [x, y] = point;` | 绑定到公开数据成员（声明序） |

## 最小示例

```cpp
// Standard: C++17
#include <iostream>
#include <map>
#include <tuple>

struct Point { double x, y; };

int main() {
    // struct 解构
    Point p{1.0, 2.0};
    auto [px, py] = p;
    std::cout << px << ", " << py << "\n"; // 1, 2

    // pair 解构（map 迭代）
    std::map<int, const char*> m{{1, "one"}, {2, "two"}};
    for (const auto& [key, val] : m) {
        std::cout << key << ": " << val << "\n";
    }

    // tuple 解构
    auto [a, b, c] = std::make_tuple(10, 20, 30);
    std::cout << a + b + c << "\n"; // 60
}
```

## 嵌入式适用性：高

- 纯编译期语法糖，零运行时开销，生成的代码与手动取字段完全等价
- 简化寄存器组、传感器数据等多字段结构的解包，提高可读性
- 搭配 `const auto&` 避免拷贝，适合只读访问硬件映射结构体
- C++17 在主流嵌入式工具链（GCC 7+、ARM Clang 6+）中已充分支持

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 4.0 | 19.1 |

## 另见

- [教程：结构化绑定](../../vol2-modern-features/ch05-structured-bindings/01-structured-bindings.md)
- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
