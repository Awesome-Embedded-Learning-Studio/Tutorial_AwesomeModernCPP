---
title: "std::array"
description: "固定大小的连续容器，零开销封装 C 风格数组"
chapter: 99
order: 4
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# std::array（C++11）

## 一句话

一个不会退化成指针的固定大小数组，拥有 C 风格数组的性能，同时支持 size()、迭代器和赋值等标准容器接口。

## 头文件

`#include <array>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 元素访问 | `reference at(size_type pos)` | 带边界检查的元素访问 |
| 元素访问 | `reference operator[](size_type pos)` | 无边界检查的元素访问 |
| 首元素 | `reference front()` | 访问第一个元素 |
| 末元素 | `reference back()` | 访问最后一个元素 |
| 底层指针 | `T* data() noexcept` | 直接访问底层数组指针 |
| 填充 | `void fill(const T& value)` | 用指定值填充所有元素 |
| 大小 | `constexpr size_type size() noexcept` | 返回元素个数（编译期常量） |
| 判空 | `constexpr bool empty() noexcept` | 检查是否为空（N==0 时为 true） |
| 交换 | `void swap(array& other)` | 交换两个数组的内容 |
| 起始迭代 | `iterator begin() noexcept` | 返回指向开头的迭代器 |

## 最小示例

```cpp
#include <array>
#include <iostream>
// Standard: C++11
int main() {
    std::array<int, 3> arr = {1, 2, 3};
    arr.fill(0);
    arr[0] = 42;
    for (const auto& v : arr)
        std::cout << v << ' '; // 输出: 42 0 0
    std::cout << "\nsize: " << arr.size(); // 输出: size: 3
}
```

## 嵌入式适用性：高

- 零开销抽象，编译后与 C 风格数组完全一致，不引入堆分配
- `size()` 为编译期常量，可在模板元编程和静态断言中使用
- 支持 `constexpr`，适合在编译期构建查找表
- 内置边界检查的 `at()` 方便调试，Release 阶段可移除

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 3.1 | 19.0 |

## 另见

- [教程：std::array 详解](../vol3-standard-library/01-array.md)
- [cppreference: std::array](https://en.cppreference.com/w/cpp/container/array)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
