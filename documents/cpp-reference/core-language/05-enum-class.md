---
title: "enum class"
description: "作用域枚举，防止枚举值污染外部命名空间且禁止隐式类型转换"
chapter: 99
order: 5
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# enum class（C++11）

## 一句话

带作用域的枚举类型，解决传统 enum 枚举值污染全局命名空间和隐式转换为整数的问题。

## 头文件

无需头文件（语言关键字）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 声明 | `enum class Name { A, B, C };` | 基础作用域枚举，默认底层类型为 int |
| 指定底层类型 | `enum class Name : uint8_t { A, B };` | 固定底层类型，节省内存 |
| 访问枚举值 | `Name::A` | 必须通过作用域运算符访问 |
| 转为整数 | `static_cast<int>(Name::A)` | 必须显式转换，无隐式转换 |
| 不透明声明 | `enum class Name : uint8_t;` | 前向声明，需指定底层类型 |
| using enum | `using enum Name;` | (C++20) 将枚举值引入当前作用域 |

## 最小示例

```cpp
// Standard: C++11
#include <iostream>

int main() {
    enum class Color : uint8_t { red, green = 20, blue };
    Color r = Color::blue;

    switch (r) {
        case Color::red:   std::cout << "red\n";   break;
        case Color::green: std::cout << "green\n"; break;
        case Color::blue:  std::cout << "blue\n";  break;
    }

    // int n = r; // error
    int n = static_cast<int>(r);
    std::cout << n << '\n'; // 21
}
```

## 嵌入式适用性：高

- 指定底层类型（如 `uint8_t`、`uint32_t`）可精确控制内存占用，适合协议解析和寄存器映射
- 零运行时开销，编译期完全展开
- 消除命名冲突，适合大型嵌入式项目的模块化开发
- 显式类型转换避免意外的整数比较，提升代码安全性

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.1 | 2010 |

## 另见

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
