---
title: "std::span"
description: "连续序列的非拥有视图，零开销替代指针+长度传参"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [20, 23]
---

# std::span（C++20）

## 一句话

一种轻量级的非拥有视图，用于安全地引用一段连续内存，替代传统的指针加长度参数传递方式。

## 头文件

`#include <span>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `template<class T, size_t E = dynamic_extent> class span` | 模板类，支持静态或动态长度 |
| 获取指针 | `T* data() const` | 访问底层连续存储 |
| 元素个数 | `size_t size() const` | 返回元素数量 |
| 字节大小 | `size_t size_bytes() const` | 返回序列占用的字节数 |
| 是否为空 | `bool empty() const` | 检查序列是否为空 |
| 下标访问 | `reference operator[](size_t idx) const` | 访问指定元素（无边界检查） |
| 首元素 | `reference front() const` | 访问第一个元素 |
| 末元素 | `reference back() const` | 访问最后一个元素 |
| 取前 N 个 | `template<size_t C> constexpr span<element_type, C> first() const` | 获取前 N 个元素的子视图 |
| 取子视图 | `template<size_t O, size_t C> constexpr span<element_type, C> subspan() const` | 获取指定偏移和长度的子视图 |

## 最小示例

```cpp
// Standard: C++20
#include <iostream>
#include <span>

void print(std::span<const int> s) {
    for (int v : s) std::cout << v << ' ';
    std::cout << '\n';
}

int main() {
    int arr[] = {1, 2, 3, 4, 5};
    std::span<int> s(arr);
    print(s);            // 1 2 3 4 5
    print(s.first(3));   // 1 2 3
    print(s.subspan(2)); // 3 4 5
}
```

## 嵌入式适用性：高

- 零开销抽象：仅包含指针和长度（或编译期常量长度），无堆分配
- 完美替代裸指针传参：统一数组、`std::array`、`std::vector` 的接口，提升安全性
- `TriviallyCopyable` 类型（C++23 起明确要求，此前主流实现已满足），可安全用于中断与 DMA 缓冲区操作
- `size_bytes()` 与 `as_bytes()` 极大简化硬件寄存器映射和底层字节级数据处理

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 待补充 | 待补充 | 待补充 |

## 另见

- [教程：span 详解](../../vol3-standard-library/02-span.md)
- [cppreference: std::span](https://en.cppreference.com/w/cpp/container/span)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
