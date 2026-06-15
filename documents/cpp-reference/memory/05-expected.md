---
chapter: 99
cpp_standard:
- 23
description: 承载正常值或错误信息的类型安全包装，替代异常和双返回值模式
difficulty: intermediate
order: 5
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
- expected
title: std::expected
---
<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# std::expected（C++23）

## 一句话

要么持有期望的正常值 `T`，要么持有意外错误 `E`——类型安全、零开销的错误传播机制，替代异常和 `std::pair<T, Error>` 模式。

## 头文件

`#include <expected>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造（成功值） | `expected(T value)` | 包装正常值 |
| 构造（错误） | `expected(unexpect_t, E err)` | 包装错误（`std::unexpected{err}`） |
| 检查是否成功 | `bool has_value() const noexcept` | 是否持有正常值 |
| 隐式 bool 转换 | `explicit operator bool() const noexcept` | 同 has_value |
| 获取值 | `T& value()` | 获取正常值的引用（失败抛异常） |
| 获取错误 | `const E& error() const` | 获取错误的引用 |
| 解引用 | `T& operator*()` | 获取正常值（未检查，行为未定义若错误） |
| 链式变换 | `auto transform(F&& f)` | 若有值，对值应用 f 并包装结果 |
| 链式错误处理 | `auto and_then(F&& f)` | 若有值，调用 f 并返回其 expected 结果 |
| 错误分支 | `auto or_else(F&& f)` | 若有错误，调用 f 处理错误 |
| 错误变换 | `auto transform_error(F&& f)` | 若有错误，对错误应用 f |
| 创建成功值 | `std::expected<T, E>(value)` | 工厂：直接构造成功 |
| 创建错误值 | `std::unexpected{err}` | 工厂：构造 unexpected 用于隐式转 expected |

## 最小示例

```cpp
// Standard: C++23
#include <expected>
#include <iostream>
#include <string>

std::expected<int, std::string> divide(int a, int b) {
    if (b == 0) return std::unexpected{"division by zero"};
    return a / b;
}

int main() {
    auto r1 = divide(10, 3);
    if (r1) std::cout << *r1 << "\n"; // 3

    auto r2 = divide(10, 0);
    if (!r2) std::cout << r2.error() << "\n"; // division by zero

    // 链式调用
    auto r3 = divide(20, 4).transform([](int v) { return v * 2; });
    std::cout << *r3 << "\n"; // 10
}
```

## 嵌入式适用性：高

- 零开销抽象：大小等于 `sizeof(T) + sizeof(E)` 加一个判别标志，无堆分配
- 替代异常处理机制，适合禁用异常的嵌入式环境（`-fno-exceptions`）
- 比 error code + output parameter 模式类型安全，强制调用方处理错误
- 链式操作（transform/and_then）可组合复杂业务流程，保持代码线性可读

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 12 | 16 | 19.36 |

## 另见

- [教程：std::expected 错误处理](../../vol2-modern-features/ch10-error-handling/03-expected-error.md)
- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
