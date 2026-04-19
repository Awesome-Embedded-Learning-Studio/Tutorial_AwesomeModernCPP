---
title: "泛型 Lambda"
description: "允许 Lambda 表达式的参数使用 auto 占位符，编译器自动推导类型"
chapter: 99
order: 9
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [14, 17, 20, 23]
---

# 泛型 Lambda（C++14）

## 一句话

让 Lambda 表达式的参数支持 `auto`，省去为不同类型写多个重载的麻烦，相当于生成了一个模板化的 `operator()`。

## 头文件

无（语言特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 泛型参数 | `[captures](auto a, auto b) { ... }` | 使用 `auto` 声明参数，按推导类型生成模板 `operator()` |
| 转发引用参数 | `[captures](auto&&... ts) { ... }` | 结合 `auto&&` 完美转发参数包 |
| 显式模板参数 (C++20) | `[captures]<class T>(T a) { ... }` | 在方括号后用尖括号显式声明模板参数，支持约束 |
| 无捕获转换函数指针 | `using F = ret(*)(params); operator F() const;` | 无捕获泛型 Lambda 可隐式转为函数指针 (C++17 起 constexpr) |

## 最小示例

```cpp
#include <iostream>
// Standard: C++14
int main() {
    auto compare = [](auto a, auto b) { return a < b; };
    std::cout << compare(3, 4) << "\n";       // int vs int
    std::cout << compare(3.14, 2.72) << "\n"; // double vs double
}
```

## 嵌入式适用性：高

- 零运行时开销，`auto` 仅在编译期推导，生成的代码与手写模板完全一致
- 非常适合编写通用的回调函数（如排序比较、定时器回调），减少模板代码冗余
- C++14 的 `auto` 语法已广泛被 GCC 5+ / Clang 3.4+ 支持，主流嵌入式工具链均可使用

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## 另见

- [cppreference: Lambda expressions](https://en.cppreference.com/w/cpp/language/lambda)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
