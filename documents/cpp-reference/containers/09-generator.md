---
chapter: 99
cpp_standard:
- 23
description: 基于协程的同步生成器，用 co_yield 惰性产生值序列
difficulty: intermediate
order: 9
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- coroutine
title: std::generator
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

# std::generator（C++23）

## 一句话

用 `co_yield` 惰性产生值序列的协程生成器——替代手写迭代器，零堆分配（分配器可定制），代码量减少一个数量级。

## 头文件

`#include <generator>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 生成器类型 | `template<class T> class generator` | 惰性值序列，满足 `view` 概念 |
| 产值 | `co_yield expr;` | 产出一个值并挂起 |
| 完成生成 | `co_return;` | 结束生成器 |
| 迭代 | `generator::iterator` | 输入迭代器，用于 range-for |
| 范围适配 | 直接用于 `ranges::` 管道 | 生成器是 view，可组合 |
| 引用类型 | `generator<const T&>` | 按引用产出（避免拷贝） |
| 分配器 | `template<class T, class Alloc> class generator` | 可定制协程帧分配器 |

## 最小示例

```cpp
// Standard: C++23
#include <generator>
#include <iostream>

std::generator<int> fibonacci() {
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto tmp = a;
        a = b;
        b = tmp + b;
    }
}

int main() {
    for (int v : fibonacci() | std::views::take(8)) {
        std::cout << v << " "; // 0 1 1 2 3 5 8 13
    }
}
```

## 嵌入式适用性：中

- 惰性求值：只在需要时计算下一个值，不预分配整个序列的内存
- 协程帧可使用自定义分配器，适合静态内存池
- 替代手写迭代器和回调函数，代码可读性大幅提升
- C++23 特性，编译器支持仍在推进中（GCC 14+、Clang 17+、MSVC 19.34+）
- 生成器生命周期管理需注意：生成器销毁后访问产出值是未定义行为

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.34 |

## 另见

- [cppreference: std::generator](https://en.cppreference.com/w/cpp/coroutine/generator)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
