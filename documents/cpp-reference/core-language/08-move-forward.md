---
title: "std::move"
description: "将左值转换为右值引用，触发移动语义以实现资源的高效转移"
chapter: 99
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [11, 14, 17, 20, 23]
---

# std::move（C++11）

## 一句话

把一个左值强转为右值引用，告诉编译器"这个对象的资源可以偷走"，从而触发移动构造或移动赋值，避免深拷贝。

## 头文件

`#include <utility>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 移动转换 (C++14起) | `template<class T> constexpr std::remove_reference_t<T>&& move(T&& t) noexcept;` | 将对象 `t` 转换为右值引用 (xvalue) |
| 完美转发 | `template<class T> T&& forward(typename std::remove_reference<T>::type& t) noexcept;` | 转发引用场景下保留值类别，需配合 `std::move` 使用 |
| 条件移动 | `template<class T> typename std::conditional<...>::type move_if_noexcept(T& t) noexcept;` | 移动构造不抛异常时转为右值，否则返回左值 |

## 最小示例

```cpp
#include <iostream>
#include <string>
#include <utility>
#include <vector>
// Standard: C++11
int main() {
    std::string str = "Hello";
    std::vector<std::string> v;
    v.push_back(str);              // 拷贝
    v.push_back(std::move(str));   // 移动，str 变为有效但未指定的状态
    std::cout << v[0] << " " << v[1] << "\n";
    std::cout << "str empty: " << str.empty() << "\n";
}
```

## 嵌入式适用性：高

- 零开销抽象：`std::move` 本质是 `static_cast`，编译期完成，无运行时成本
- 避免深拷贝：在传递大块缓冲区（如 `std::vector<uint8_t>`、`std::string`）时显著减少 RAM 占用和 CPU 开销
- 配合自定义资源类：可用于转移裸指针所有权（需配合 RAII），替代手动资源移交
- 注意被移动后的对象处于"有效但未指定"状态，不可再读取其值，只能赋值或销毁

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.0   | 19.0 |

## 另见

- [教程：移动语义](../vol2-modern-features/02-move-semantics.md)
- [cppreference: std::move](https://en.cppreference.com/w/cpp/utility/move)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
