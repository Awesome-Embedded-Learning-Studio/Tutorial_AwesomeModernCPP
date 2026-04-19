---
title: "范围 for 循环"
description: "用更简洁的语法遍历容器或数组中的所有元素"
chapter: 99
order: 7
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# 范围 for 循环（C++11）

## 一句话

一种无需手写迭代器就能遍历容器或数组所有元素的语法糖，让循环代码更简洁、更不易出错。

## 头文件

无（语言特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 只读遍历 | `for (auto item : range)` | 拷贝每个元素到 `item` |
| 引用遍历 | `for (auto& item : range)` | 以左值引用访问元素（可修改） |
| 常引用遍历 | `for (const auto& item : range)` | 避免拷贝且不可修改 |
| 初始化语句 | `for (init; auto& item : range)` | C++20 起，循环前执行初始化 |
| 数组遍历 | `for (auto item : arr)` | 支持已知大小的原生数组 |

## 最小示例

```cpp
#include <vector>
#include <iostream>
// Standard: C++11
int main() {
    std::vector<int> v = {1, 2, 3};
    for (const auto& x : v) {
        std::cout << x << ' ';
    }
    return 0;
}
```

## 嵌入式适用性：高

- 零开销抽象：编译后与手写迭代器/下标循环完全等价，不产生额外运行时成本
- 语法简洁可减少因下标越界或迭代器失效导致的错误
- 配合 `constexpr` 数组在编译期遍历也非常实用
- 注意：遍历返回临时对象的成员函数时需警惕生命周期问题（C++23 前为 UB）

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 2010 |

## 另见

- [教程：范围 for 与底层优化](../../vol2-modern-features/03-range-based-for-optimization.md)
- [cppreference: Range-based for loop](https://en.cppreference.com/w/cpp/language/range-for)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
