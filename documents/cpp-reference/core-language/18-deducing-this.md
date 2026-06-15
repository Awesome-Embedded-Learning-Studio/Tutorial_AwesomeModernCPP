---
chapter: 99
cpp_standard:
- 23
description: 显式对象参数推导：让成员函数的第一个参数自动推导为 *this 的类型与值类别
difficulty: intermediate
order: 18
reading_time_minutes: 2
tags:
- host
- cpp-modern
- intermediate
title: Deducing this
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

# Deducing this（C++23）

## 一句话

成员函数的首个参数写 `this Self` 或 `this Self&&`，编译器根据调用对象的值类别（左值/右值/const）自动推导——消灭 `const`/非 `const`/右值引用的重载三连。

## 头文件

无（语言特性）

## 核心 API 速查

| 语法 | 说明 |
|------|------|
| `void func(this Self&& self)` | 右值引用对象参数 |
| `void func(this const Self& self)` | const 左值引用（只读） |
| `void func(this Self& self)` | 非 const 左值引用（可修改） |
| `void func(this auto&& self)` | 完美转发，一次定义覆盖所有值类别 |
| 搭配模板 | `template<class Self> void func(this Self&& self)` 模板化显式对象参数 |
| CRTP 简化 | 显式对象参数可直接替代 CRTP，减少基类开销 |

## 最小示例

```cpp
// Standard: C++23
#include <iostream>
#include <utility>

struct Wrapper {
    int value;

    // 一个函数覆盖 const/非 const/右值三种场景
    template <typename Self>
    auto&& get(this Self&& self) {
        return std::forward<Self>(self).value;
    }
};

int main() {
    Wrapper w{42};
    const Wrapper cw{99};

    std::cout << w.get() << "\n";   // 42 (非 const 左值)
    std::cout << cw.get() << "\n";  // 99 (const 左值)
    std::cout << Wrapper{7}.get() << "\n"; // 7 (右值)
}
```

## 嵌入式适用性：中

- 减少样板代码：一个显式对象参数替代 `const`/非 `const`/右值三个重载
- 简化 CRTP：直接在成员函数中推导类型，消除基类间接调用开销
- 对递归 lambda 和链式调用 API 尤其有用
- C++23 特性，编译器支持尚在推进中（GCC 14.1+、Clang 18+、MSVC 19.34+）
- 嵌入式工具链升级周期较长，短期内不适用于需要广泛兼容性的项目

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 14.1 | 18 | 19.34 |

## 另见

- [cppreference: Deducing this](https://en.cppreference.com/w/cpp/language/member_functions#Explicit_object_parameter)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
