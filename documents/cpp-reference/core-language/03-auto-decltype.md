---
title: "auto"
description: "让编译器自动推导变量或函数返回值类型的占位符"
chapter: 99
order: 3
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# auto（C++11）

## 一句话

用 `auto` 声明变量或函数返回类型，让编译器根据初始化表达式自动推导出具体类型，省去手写冗长或复杂类型的麻烦。

## 头文件

无需头文件（语言关键字）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 变量类型推导 | `auto x = init;` | 根据初始化表达式推导 `x` 的类型 |
| 带修饰符推导 | `const auto& x = init;` | 推导为基础类型并附加 `const` 或引用修饰 |
| 尾置返回类型 | `auto f() -> int;` | 结合尾置返回类型声明函数 |
| 返回类型推导 | `auto f() { return expr; }` | C++14 起，根据 return 语句推导返回类型 |
| decltype(auto) | `decltype(auto) f() { return expr; }` | C++14 起，保留表达式的值类别（引用/顶层 const）|
| 概念约束推导 | `Concept auto x = init;` | C++20 起，推导类型并检查是否满足概念约束 |
| 函数式转换 | `auto(expr)` | C++23 起，等同于 `static_cast<auto>(expr)` |

## 最小示例

```cpp
// Standard: C++14
#include <iostream>

auto add(int a, int b) {
    return a + b; // 返回类型推导为 int
}

int main() {
    auto x = 10;        // int
    const auto& r = x;  // const int&
    auto sum = add(x, 5);
    std::cout << sum << "\n";
}
```

## 嵌入式适用性：高

- 零运行时开销，`auto` 纯属编译期类型推导，不产生任何额外指令
- 简化寄存器/外设类型声明（如 `auto reg = reinterpret_cast<volatile uint32_t*>(0x40001000)`），提高可读性且不损失精度
- 配合模板和 STL 容器迭代器时，可避免手写冗长类型名，减少拼写错误

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9 | 10.0 |

## 另见

- [教程：auto 与 decltype](../../vol2-modern-features/01-auto-and-decltype.md)
- [cppreference: Placeholder type specifiers](https://en.cppreference.com/w/cpp/language/auto)

---
*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
