---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 在头文件中定义全局变量而不违反 ODR，编译器保证单一实例
difficulty: beginner
order: 14
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: 内联变量
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

# 内联变量（C++17）

## 一句话

用 `inline` 修饰命名空间作用域的变量，允许在头文件中定义全局变量而不会产生多重定义链接错误——编译器保证整个程序只有一个实例。

## 头文件

无（语言特性）

## 核心 API 速查

| 语法 | 说明 |
|------|------|
| `inline T var = val;` | 命名空间作用域的内联变量定义 |
| `inline constexpr T var = val;` | `constexpr` 变量隐式 `inline`，无需重复标注 |
| `inline static T var = val;` | 类内静态成员变量，C++17 起可在类内直接初始化 |
| `inline thread_local T var = val;` | 配合线程局部存储 |

## 最小示例

```cpp
// Standard: C++17
// header.h
#pragma once
#include <string>

inline const std::string kVersion = "1.0.0";
inline int kMaxRetries = 3;

// 多个翻译单元 include 此头文件，
// 链接时保证只有一个 kVersion 和 kMaxRetries 实例
```

```cpp
// main.cpp
#include <iostream>
#include "header.h"

int main() {
    std::cout << kVersion << "\n";     // 1.0.0
    std::cout << kMaxRetries << "\n";  // 3
}
```

## 嵌入式适用性：高

- 头文件库（header-only library）的理想搭档，替代 extern 全局变量模式
- `constexpr` 变量隐式 `inline`，嵌入式常用的编译期常量表天然受益
- 消除"头文件中声明 + 源文件中定义"的样板代码
- 零运行时开销，仅影响链接阶段的符号合并

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## 另见

- [cppreference: inline specifier](https://en.cppreference.com/w/cpp/language/inline)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
