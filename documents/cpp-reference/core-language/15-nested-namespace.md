---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 用 A::B::C 语法替代多层嵌套的 namespace 大括号
difficulty: beginner
order: 15
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: 嵌套命名空间
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

# 嵌套命名空间（C++17）

## 一句话

用 `namespace A::B::C { ... }` 一行替代三层嵌套的大括号——纯粹的语法糖，但极大减少缩进层级。

## 头文件

无（语言特性）

## 核心 API 速查

| 语法 | 等价写法 |
|------|---------|
| `namespace A::B { ... }` | `namespace A { namespace B { ... } }` |
| `namespace A::B::C { ... }` | `namespace A { namespace B { namespace C { ... } } }` |
| `namespace A::inline B { ... }` | `namespace A { inline namespace B { ... } }` (C++20) |

## 最小示例

```cpp
// Standard: C++17
#include <iostream>

// 嵌套命名空间定义
namespace hardware::spi {
    void init() { std::cout << "SPI init\n"; }
}

// 等价的 C++11 写法（效果完全相同）
namespace hardware {
    namespace i2c {
        void init() { std::cout << "I2C init\n"; }
    }
}

int main() {
    hardware::spi::init(); // SPI init
    hardware::i2c::init(); // I2C init
}
```

## 嵌入式适用性：低

- 纯语法糖，不影响生成的代码，但嵌入式项目通常命名空间层级不深
- 对大型库和驱动的代码组织有帮助，减少缩进嵌套
- 嵌入式代码常使用较扁平的命名空间（如 `bsp::`、`hal::`），单一层级即够用
- C++17 编译器普遍支持，无兼容性顾虑

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7 | 3.9 | 19.1 |

## 另见

- [cppreference: Namespace](https://en.cppreference.com/w/cpp/language/namespace)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
