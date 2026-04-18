---
title: "std::optional"
description: "一个可能包含值也可能不包含值的包装器，用于安全表达“无值”语义"
chapter: 99
order: 3
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [17, 20, 23]
---

# std::optional（C++17）

## 一句话

一个用来表示“值可能不存在”的容器，比返回 `bool` 加指针或输出参数更安全、更直观。

## 头文件

`#include <optional>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `optional()` | 默认构造，不包含值 |
| 赋空值 | `optional& operator=(nullopt_t)` | 将状态置为无值 |
| 检查含值 | `explicit operator bool() const` | 存在值时返回 `true` |
| 检查含值 | `bool has_value() const` | 同上 |
| 取值 | `T& operator*()` | 解引用获取值（未定义无值时行为） |
| 安全取值 | `T& value()` | 取值，无值时抛出 `bad_optional_access` |
| 取值或默认 | `T value_or(const T& default_value) const` | 有值返回值，无值返回默认值 |
| 就地构造 | `T& emplace(Args&&... args)` | 在原位构造值 |
| 重置 | `void reset() noexcept` | 销毁已包含的值 |

## 最小示例

```cpp
#include <iostream>
#include <optional>
#include <string>

std::optional<std::string> find(bool b) {
    return b ? std::optional<std::string>{"found"} : std::nullopt;
}

int main() {
    auto res = find(false);
    std::cout << res.value_or("not found") << '\n';

    if (auto val = find(true))
        std::cout << *val << '\n';
}
```

## 嵌入式适用性：高

- 零开销抽象，无值时仅占用一个 `bool` 大小的存储空间，不涉及堆分配
- 可替代裸指针作为可能失败的函数返回值，避免空指针解引用风险
- C++17 起即完全支持，C++23 后成员函数全面 `constexpr`，进一步拓宽适用场景

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 待补充 | 待补充 | 待补充 |

## 另见

- [教程：optional](../../vol2-modern-features/04-optional.md)
- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
