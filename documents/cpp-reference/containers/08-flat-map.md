---
chapter: 99
cpp_standard:
- 23
description: 基于连续存储的有序关联容器，缓存友好的 std::map 替代品
difficulty: beginner
order: 8
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::flat_map
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

# std::flat_map（C++23）

## 一句话

用连续数组替代红黑树的有序映射——查找更快（缓存友好），内存更紧凑，但插入/删除是 O(n)。

## 头文件

`#include <flat_map>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 访问元素 | `V& operator[](const K& key)` | 按键访问，不存在则插入默认值 |
| 查找 | `iterator find(const K& key)` | 返回指向元素的迭代器 |
| 插入 | `pair<iterator, bool> insert(const value_type&)` | 插入键值对 |
| 删除 | `size_t erase(const K& key)` | 按键删除元素 |
| 元素数量 | `size_t size() const` | 返回元素个数 |
| 是否为空 | `bool empty() const` | 检查是否为空 |
| 清空 | `void clear()` | 清除所有元素 |
| 迭代 | `iterator begin()` / `end()` | 按键序遍历 |
| 下界/上界 | `iterator lower_bound(const K&)` | 有序查找边界 |
| 是否包含 | `bool contains(const K& key) const` | (C++20 起可用) 检查键是否存在 |

## 最小示例

```cpp
// Standard: C++23
#include <flat_map>
#include <iostream>

int main() {
    std::flat_map<int, const char*> m;
    m[1] = "one";
    m[3] = "three";
    m[2] = "two";

    for (const auto& [k, v] : m) {
        std::cout << k << ": " << v << "\n";
    }
    // 1: one  2: two  3: three  (按键序排列)

    std::cout << std::boolalpha << m.contains(2) << "\n"; // true
}
```

## 嵌入式适用性：中

- 连续存储对 CPU 缓存友好，小数据集的查找性能远优于 `std::map`
- 无节点分配器开销，内存碎片更少，适合堆空间受限的嵌入式环境
- 插入/删除 O(n)，不适合频繁修改的大数据集
- 编译器支持尚在推进中（GCC 15+、Clang 20+、MSVC 19.51+），生产环境需评估工具链

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 15 | 20 | 19.51 |

## 另见

- [cppreference: std::flat_map](https://en.cppreference.com/w/cpp/container/flat_map)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
