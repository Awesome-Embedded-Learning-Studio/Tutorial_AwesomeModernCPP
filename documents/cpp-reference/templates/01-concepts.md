---
title: "约束与概念"
description: "在编译期对模板参数施加语义约束，提供清晰的错误信息"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - 模板
difficulty: intermediate
cpp_standard: [20, 23]
---

# 约束与概念（C++20）

## 一句话

一种为模板参数指定语义要求（如"可哈希"、"迭代器"）的机制，能在编译期尽早拦截错误类型并输出可读的错误信息。

## 头文件

`#include <concepts>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 概念定义 | `template<...> concept Name = constraint-expression;` | 定义一个命名的约束集合 |
| requires 表达式 | `requires { /* 表达式 */ }` | 检查表达式是否合法 |
| 嵌套需求 | `{ expr } -> std::convertible_to<T>;` | 要求表达式合法且结果可转换为 T |
| 简写模板形参 | `void f(Concept auto param)` | 在形参列表中直接使用概念约束 |
| requires 子句 | `template<typename T> requires Concept<T> void f(T);` | 在模板声明后追加约束 |
| 尾置 requires | `template<typename T> void f(T) requires Concept<T>;` | 在函数形参列表后追加约束 |
| 逻辑与 | `Concept1 && Concept2` | 组合多个约束（合取） |
| 逻辑或 | `Concept1 \|\| Concept2` | 组合多个约束（析取） |

## 最小示例

```cpp
#include <concepts>
#include <iostream>

template<typename T>
concept Addable = requires(T a, T b) { a + b; };

template<Addable T>
T add(T a, T b) { return a + b; }

int main() {
    std::cout << add(1, 2) << '\n';     // OK: int 满足 Addable
    // add("a", "b");                   // Error: const char* 不满足 Addable
}
```

## 嵌入式适用性：高

- 纯编译期特性，零运行时开销，适合资源受限环境
- 约束驱动的设计能将类型错误拦截在编译期，避免在目标板上触发未定义行为
- 标准库概念（如 `std::integral`, `std::same_as`）可直接用于约束硬件寄存器包装类型的接口
- 错误信息大幅缩短，能显著加快底层模板库的开发调试周期

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 10.0 | 10.0 | 19.28 |

## 另见

- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
