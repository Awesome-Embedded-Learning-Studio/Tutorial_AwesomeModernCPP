---
title: "Lambda 表达式"
description: "就地定义匿名函数对象，可捕获作用域内的变量"
chapter: 99
order: 2
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# Lambda 表达式（C++11）

## 一句话

Lambda 允许在代码中就地定义一个匿名函数对象，常用于将简短逻辑作为参数传递给算法或回调。

## 头文件

无（语言特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 无捕获 lambda | `[captures](params) { body }` | 基本语法，生成闭包类型 |
| 无参 lambda | `[captures] { body }` | 省略参数列表的简写 |
| 值捕获 | `[x, y]` | 按值复制捕获变量 |
| 引用捕获 | `[&x, &y]` | 按引用捕获变量 |
| 全部值捕获 | `[=]` | 按值捕获所有已使用的自动变量 |
| 全部引用捕获 | `[&]` | 按引用捕获所有已使用的自动变量 |
| 可变 lambda | `[captures](params) mutable { body }` | 允许修改按值捕获的副本 |
| 泛型 lambda | `[captures](auto a, auto b) { body }` | 参数使用 auto，模板化 operator() |
| 显式模板参数 | `[captures]<typename T>(T a) { body }` | C++20，显式指定模板参数列表 |
| 静态 lambda | `[captures](params) static { body }` | C++23，operator() 为静态成员函数 |

## 最小示例

```cpp
#include <algorithm>
#include <vector>
#include <iostream>
// Standard: C++11
int main() {
    std::vector<int> v = {3, 1, 4, 1, 5};
    int threshold = 3;
    auto count = std::count_if(v.begin(), v.end(),
        [threshold](int x) { return x > threshold; });
    std::cout << count << "\n"; // 输出: 2
}
```

## 嵌入式适用性：高

- 编译期生成闭包类型，无堆分配开销，零额外运行时成本
- 替代函数指针和裸写仿函数，使回调代码更紧凑、可读性更好
- 注意引用捕获在异步或中断场景下的生命周期风险，嵌入式回调推荐值捕获
- C++14 泛型 lambda 可在无模板开销的前提下编写通用排序/查找比较逻辑

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.5 | 3.1   | 19.0 |

## 另见

- [教程：Lambda 基础](../../vol2-modern-features/01-lambda-basics.md)
- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---
*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
