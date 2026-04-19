---
title: "std::variant"
description: "类型安全的联合体，在任意时刻持有其候选类型之一的值"
chapter: 99
order: 3
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
cpp_standard: [17, 20, 23]
---

# std::variant（C++17）

## 一句话

一个类型安全的 union 替代品，能在同一块内存中存放不同类型的值，并通过索引或类型安全地访问。

## 头文件

`#include <variant>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造函数 | `variant()` | 默认构造，持有第一个候选类型的值 |
| 赋值 | `variant& operator=(T&& t)` | 赋值并切换到对应类型 |
| 按类型访问 | `template<class T> T& get(variant& v)` | 按类型取值，类型不匹配时抛出异常 |
| 按索引访问 | `template<size_t I> T& get(variant& v)` | 按索引取值，索引越界时抛出异常 |
| 安全访问 | `template<class T> T* get_if(variant* v)` | 按类型取指针，不匹配时返回 nullptr |
| 类型检查 | `template<class T> bool holds_alternative(const variant& v)` | 检查当前是否持有指定类型 |
| 访问器 | `template<class Vis> R visit(Vis&& vis, variant& v)` | 传入可调用对象，自动分发到当前活跃类型 |
| 当前索引 | `size_t index() const` | 返回当前活跃类型的零基索引 |
| 原位构造 | `template<class T, class... Args> T& emplace(Args&&... args)` | 销毁旧值并原位构造新值 |

## 最小示例

```cpp
#include <iostream>
#include <string>
#include <variant>
// Standard: C++17
int main() {
    std::variant<int, std::string> v = 42;
    std::cout << std::get<int>(v) << '\n';
    v = "hello";
    std::cout << std::get<std::string>(v) << '\n';
    std::visit([](auto&& arg) {
        std::cout << arg << '\n';
    }, v);
}
```

## 嵌入式适用性：中

- 相比裸 union，隐含了额外的类型索引存储和运行时检查开销
- 避免了手动管理 union 脏标志位的出错风险，提升代码健壮性
- 适合资源较充足（如带 MMU 的 SoC）的应用层状态管理或消息解析
- 极度受限的裸机环境建议评估 sizeof 开销后谨慎使用

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 5.0   | 19.10 |

## 另见

- [教程：对应章节](../../vol2-modern-features/03-variant.md)
- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
