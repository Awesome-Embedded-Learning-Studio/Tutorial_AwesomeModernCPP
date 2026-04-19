---
title: "override 说明符"
description: "在成员函数声明后使用，确保该函数确实覆盖了基类的虚函数，否则编译报错"
chapter: 99
order: 6
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# override 说明符（C++11）

## 一句话

在虚函数声明末尾加上 `override`，让编译器帮你检查是否真的成功覆盖了基类的虚函数，签名不匹配或基类非虚函数都会直接报错。

## 头文件

无（语言关键字级别特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 函数声明 | `return_type func_name(params) override;` | 声明时使用，确保覆盖基类虚函数 |
| 函数定义（类内） | `return_type func_name(params) override { ... }` | 类内定义时使用 |
| 纯虚函数覆盖 | `return_type func_name(params) override = 0;` | `override` 出现在 `= 0` 之前 |
| 与 final 组合 | `return_type func_name(params) override final;` | 可与 `final` 以任意顺序组合使用 |
| 析构函数覆盖 | `~Derived() override;` | 可用于虚析构函数的覆盖检查 |

## 最小示例

```cpp
// Standard: C++11
#include <iostream>
struct Base { virtual void foo() { std::cout << "Base\n"; } };
struct Derived : Base {
    // void foo(int) override; // 编译错误：签名不匹配
    void foo() override { std::cout << "Derived\n"; }
};
int main() {
    Derived d;
    d.foo();
}
```

## 嵌入式适用性：高

- 零运行时开销，仅在编译期做静态检查
- 嵌入式代码中常有多层继承的硬件抽象层（HAL），`override` 能有效防止因基类接口修改导致的静默错误
- 不影响代码体积和执行速度，适合对资源敏感的场景

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.7 | 3.0 | 2012 |

## 另见

- [cppreference: override specifier](https://en.cppreference.com/w/cpp/language/override)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
