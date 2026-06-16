---
description: documents/ 下文章的 frontmatter 字段参考与标签体系，编辑文章时必须遵守
globs: documents/**/*.md
---

# Frontmatter 字段参考

编辑 `documents/` 下的 Markdown 文章时，必须遵守以下 frontmatter 规范。本文件由 `scripts/validate_frontmatter.py` 在 pre-commit 和 CI 中强制校验。

## 字段定义

### 必填字段（Missing = Error）

| 字段 | 类型 | 说明 |
|------|------|------|
| `title` | string | 文章标题 |
| `chapter` | int (0–100) | 章节编号 |
| `order` | int (≥0) | 章内排序序号，应与文件名序号一致 |

### 推荐字段（Missing = Warning）

| 字段 | 类型 | 说明 |
|------|------|------|
| `description` | string | 一句话摘要，缺失时由 `documents/hooks/meta.py` 自动生成，但建议手动填写以控制质量 |
| `description` | string | 一句话摘要，建议手动填写以控制质量 |
| `tags` | list[string] | 分类标签，必须来自下方 VALID_TAGS 集合 |

### 可选字段

| 字段 | 类型 | 有效取值 | 说明 |
|------|------|----------|------|
| `difficulty` | string | `beginner` \| `intermediate` \| `advanced` | 难度等级 |
| `platform` | string | `host` \| `stm32f1` \| `stm32f4` | 目标平台 |
| `cpp_standard` | list[int] | 子集 `[11, 14, 17, 20, 23, 26]` | 涉及的 C++ 标准版本 |
| `reading_time_minutes` | int | — | 预估阅读时长（分钟） |
| `prerequisites` | list[string] | — | 前置知识章节列表 |
| `related` | list[string] | — | 相关文章标题 |

## 标签体系（VALID_TAGS）

标签分为以下类别，所有标签必须来自此集合。如果需要新标签，须先在 `scripts/validate_frontmatter.py` 的 `VALID_TAGS` 中添加。

### 概念类
`RAII` `移动语义` `零开销抽象` `编译期计算` `类型安全` `内存管理` `异步编程` `模板元编程`

### 语言特性类
`constexpr` `consteval` `constinit` `lambda` `CRTP` `concepts` `coroutine` `if_constexpr` `模板` `泛型`

### 设计模式类
`对象池` `状态机` `工厂模式` `策略模式` `单例模式` `观察者模式` `RAII守卫` `回调机制`

### 容器类
`array` `span` `vector` `map` `unordered_map` `循环缓冲区` `侵入式容器` `容器`

### 智能指针类
`unique_ptr` `shared_ptr` `weak_ptr` `intrusive_ptr` `智能指针` `引用计数`

### 类型安全类
`enum` `enum_class` `variant` `optional` `expected` `类型别名` `字面量`

### 函数式类
`函数对象` `std_function` `std_invoke` `Ranges`

### 并发类
`atomic` `memory_order` `mutex` `无锁`

### 嵌入式类
`嵌入式` `单片机` `外设管理` `寄存器` `链接器` `交叉编译` `工具链` `CMake`

### 通用类
`基础` `入门` `进阶` `实战` `优化`

### 平台与难度类
`host` `stm32f1` `beginner` `intermediate` `advanced` `cpp-modern`

## 标签使用规则

1. 每篇文章必须包含 1 个 platform 标签（或 `host` 表示平台无关）
2. 每篇文章必须包含 1 个 difficulty 标签
3. 每篇文章至少包含 1 个 topic 标签
4. 标签使用英文小写，中划线分隔

## 免检文件

以下文件不需要 frontmatter，被 validate_frontmatter.py 跳过：
- `index.md`（各目录导航页）
- `tags.md`
- `documents/images/` 目录下的所有文件

## Frontmatter 示例

### 通用文章

```yaml
---
title: "RAII 与资源管理"
description: "深入理解 RAII 原则及其在嵌入式开发中的应用"
chapter: 2
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - RAII
difficulty: beginner
platform: host
reading_time_minutes: 15
prerequisites:
  - "Chapter 1: C++ 基础入门"
related:
  - "智能指针与所有权"
cpp_standard: [11, 14, 17]
---
```

### 嵌入式平台教程

```yaml
---
title: "GPIO 输入与按键消抖"
description: "在 STM32F1 上实现 GPIO 输入读取与软件消抖"
chapter: 3
order: 5
tags:
  - stm32f1
  - peripheral
  - beginner
  - 嵌入式
  - 外设管理
difficulty: beginner
platform: stm32f1
cpp_standard: [17, 20]
---
```
