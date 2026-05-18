---
title: "std::format"
description: "类型安全、可扩展的格式化输出库，替代 printf 和 stringstream"
chapter: 99
order: 7
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
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

# std::format（C++20）

## 一句话

类型安全的 `printf` 替代品——用 `{}` 占位符格式化字符串，编译期检查参数数量，支持自定义类型格式化。

## 头文件

`#include <format>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 格式化字符串 | `string format(fmt, args...)` | 返回格式化后的字符串 |
| 格式化到输出 | `void vformat_to(out_it, fmt, args)` | 输出到迭代器 |
| 格式化到缓冲区 | `size_t formatted_size(fmt, args...)` | 预计算输出长度 |
| 格式化到 stdout | (C++23) `void print(fmt, args...)` | 直接输出到标准输出 |
| 位置参数 | `"{0} {1} {0}"` | 按序号引用参数 |
| 宽度/精度 | `"{:>10.2f}"` | 右对齐、宽度 10、精度 2 |
| 自定义格式化 | `template<> struct formatter<T>` | 特化 `std::formatter` 支持自定义类型 |

## 最小示例

```cpp
// Standard: C++20
#include <format>
#include <iostream>
#include <string>

int main() {
    std::string s = std::format("Hello, {}!", "world");
    std::cout << s << "\n"; // Hello, world!

    int version = 2;
    double pi = 3.14159265;
    std::cout << std::format("v{}. pi={:.2f}", version, pi) << "\n";
    // v2. pi=3.14

    // 位置参数
    std::cout << std::format("{0} + {0} = {1}", 3, 6) << "\n";
    // 3 + 3 = 6
}
```

## 嵌入式适用性：中

- 替代 `printf`，消除格式字符串与参数类型不匹配的运行时崩溃风险
- 替代 `std::stringstream`，避免堆分配开销
- 编译期检查参数数量，但格式说明符的完整编译期验证需要 C++23 的 `std::is_constant_evaluated` 配合
- Flash 开销可能较大（格式化引擎代码量），资源极度受限设备需评估
- 可用 [{fmt}](https://github.com/fmtlib/fmt) 库作为 C++11 起的后备方案

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 13 | 17 | 19.29 |

## 另见

- [cppreference: std::format](https://en.cppreference.com/w/cpp/utility/format)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
