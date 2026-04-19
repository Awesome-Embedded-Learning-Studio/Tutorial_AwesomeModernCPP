---
title: "nullptr"
description: "类型安全的空指针字面量，替代 NULL 和 0"
chapter: 99
order: 4
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# nullptr（C++11）

## 一句话

类型为 `std::nullptr_t` 的空指针字面量，能安全区分整数重载，彻底解决宏 `NULL` 和整数 `0` 在模板和函数重载中带来的歧义。

## 头文件

无需头文件（语言关键字），类型定义在 `<cstddef>`。

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 空指针字面量 | `nullptr` | 类型为 `std::nullptr_t` 的纯右值 |
| 隐式转换 | → 任意指针类型 | 转换为对应类型的空指针值 |
| 隐式转换 | → 任意成员指针类型 | 转换为对应类型的空成员指针值 |

## 最小示例

```cpp
#include <iostream>
void f(int) { std::cout << "int\n"; }
void f(int*) { std::cout << "int*\n"; }

int main() {
    f(0);        // 调用 f(int)，可能非预期
    f(nullptr);  // 调用 f(int*)，精确匹配
    int* p = nullptr;
    if (p == nullptr) { std::cout << "null\n"; }
}
```

## 嵌入式适用性：高

- 零开销抽象，编译期直接生成空指针值，与 `0` 或 `NULL` 产生相同指令
- 避免寄存器操作函数（如操作硬件寄存器的重载）中整数与指针的重载歧义
- 在模板元编程（如静态断言、类型特征）中表现正确，`NULL` 和 `0` 会失败
- 完全兼容 C 风格的底层硬件操作代码，可无风险逐步替换

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0 | 2010 |

## 另见

- [cppreference: nullptr](https://en.cppreference.com/w/cpp/language/nullptr)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
