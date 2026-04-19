---
title: "std::exchange"
description: "将旧值替换为新值并返回旧值"
chapter: 99
order: 10
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [14, 17, 20, 23]
---

# std::exchange（C++14）

## 一句话

给一个变量赋新值的同时拿到它的旧值，避免手写临时变量。

## 头文件

`#include <utility>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 替换并返回旧值 | `template<class T, class U = T> T exchange(T& obj, U&& new_value);` | 将 `obj` 替换为 `new_value`，返回 `obj` 的旧值 |

## 最小示例

```cpp
// Standard: C++14
#include <iostream>
#include <utility>

int main() {
    int a = 10, b = 20;
    // 交换 a 和 b，无需临时变量
    a = std::exchange(b, a);
    std::cout << a << " " << b << "\n"; // 输出: 10 10

    // 打印斐波那契数列前几项
    for (int x{0}, y{1}; x < 50; x = std::exchange(y, x + y))
        std::cout << x << " ";
}
```

## 嵌入式适用性：中

- 本身是纯内联函数，无额外堆分配或系统调用开销
- 依赖移动语义，对自定义类型使用时需确认移动构造/赋值的实际开销
- 在实现移动构造函数和状态机切换时非常简洁，适合资源充足的场景
- C++20 起支持 constexpr，可在编译期使用

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 5.0 | 3.4   | 19.0 |

## 另见

- [cppreference: std::exchange](https://en.cppreference.com/w/cpp/utility/exchange)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
