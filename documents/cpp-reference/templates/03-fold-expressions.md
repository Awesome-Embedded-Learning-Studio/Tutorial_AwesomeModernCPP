---
title: "折叠表达式"
description: "将参数包按二元运算符展开归约，替代递归模板展开"
chapter: 99
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [17, 20, 23]
---

# 折叠表达式（C++17）

## 一句话

把可变参数模板的一包参数，用指定运算符"折叠"成一个表达式，省去手写递归终止条件的麻烦。

## 头文件

无需头文件（语言特性）

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 一元右折叠 | `(pack op ...)` | 展开为 `E1 op (... op (EN-1 op EN))` |
| 一元左折叠 | `(... op pack)` | 展开为 `(((E1 op E2) op ...) op EN)` |
| 二元右折叠 | `(pack op ... op init)` | 右折叠并提供初始值 |
| 二元左折叠 | `(init op ... op pack)` | 左折叠并提供初始值 |
| 空包折叠 (`&&`) | `(... && args)` | 包为空时结果为 `true` |
| 空包折叠 (`\|\|`) | `(... \|\| args)` | 包为空时结果为 `false` |
| 空包折叠 (`,`) | `(expr, ...)` | 包为空时结果为 `void()` |

> `op` 支持 32 种二元运算符：`+ - * / % ^ & | = < > << >> += -= *= /= %= ^= &= |= <<= >>= == != <= >= && || , .* ->*`

## 最小示例

```cpp
#include <iostream>
// Standard: C++17

template<typename... Args>
void print(Args&&... args) {
    (std::cout << ... << args) << '\n';
}

template<typename... Args>
bool all(Args... args) {
    return (... && args);
}

int main() {
    print(1, " + ", 2, " = ", 3);
    std::cout << all(true, true, false) << '\n';
}
```

## 嵌入式适用性：中

- 编译期纯计算（如 `static_assert` 中的条件检查）零运行时开销，非常适用
- 用于替代递归模板实例化，可减少编译期内存占用和编译时间
- 避免在频繁调用的热路径中使用复杂折叠表达式，防止代码膨胀增加 Flash 占用
- 逗号折叠展开多条语句时，需确认每条语句的开销在可接受范围内

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 6.0 | 3.6 | 19.1 |

## 另见

- [cppreference: Fold expressions](https://en.cppreference.com/w/cpp/language/fold)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
