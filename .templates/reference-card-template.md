---
title: "特性名称"
description: "一句话摘要"
chapter: 99
order: 0
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17]
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

# 特性名称（C++XX）

## 一句话

用一句人话说清楚这是什么、解决什么问题。

## 头文件

`#include <...>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| ...  | `...` | ... |

## 最小示例

```cpp
// 完整可编译的最小示例，不超过 20 行
// Standard: C++XX
```

## 嵌入式适用性：高/中/低

- 要点 1
- 要点 2

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| X.Y | X.Y   | 19.X |

## 另见

- [教程：对应章节](相对路径)
- [cppreference: 特性名](https://en.cppreference.com/w/cpp/...)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
