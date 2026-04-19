---
title: "std::unique_ptr"
description: "独占所有权的智能指针，零开销自动释放资源"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20]
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

# std::unique_ptr（C++11）

## 一句话

通过独占所有权语义管理动态对象生命周期的智能指针，离开作用域时自动销毁对象，且大小和原生指针完全一致。

## 头文件

`#include <memory>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 创建对象 | `template<class T> unique_ptr<T> make_unique(Args&&... args)` | (C++14) 异常安全地创建 unique_ptr |
| 构造函数 | `constexpr unique_ptr(pointer p = pointer())` | 接管原生指针 |
| 析构函数 | `~unique_ptr()` | 释放所管理的对象 |
| 释放所有权 | `pointer release() noexcept` | 放弃所有权并返回原生指针 |
| 重置指针 | `void reset(pointer p = pointer())` | 释放当前对象并接管新指针 |
| 获取原生指针 | `pointer get() const noexcept` | 返回所管理的原生指针 |
| 检查是否为空 | `explicit operator bool() const noexcept` | 判断是否持有对象 |
| 解引用 | `T& operator*() const` | 访问被管理的对象 |
| 成员访问 | `T* operator->() const` | 通过指针访问成员 |
| 数组下标 | `T& operator[](size_t i) const` | (数组特化) 访问数组元素 |

## 最小示例

```cpp
// Standard: C++14
#include <iostream>
#include <memory>
struct Foo { ~Foo() { std::cout << "destroyed\n"; } };
int main() {
    std::unique_ptr<Foo> p = std::make_unique<Foo>();
    std::unique_ptr<Foo> q = std::move(p); // 转移所有权
    std::cout << std::boolalpha << (p == nullptr) << "\n"; // true
} // "destroyed"
```

## 嵌入式适用性：高

- 零开销抽象：编译后与原生指针大小相同，无额外内存占用
- 确定性析构：作用域结束时立即释放，契合嵌入式对实时性和确定性内存的要求
- 完美支持 pImpl 惯用法，可隐藏实现细节并缩短编译依赖链
- 不引入控制块，无 `shared_ptr` 的线程安全与内存碎片开销

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.4 | 2.9   | 2010 |

## 另见

- [教程：unique_ptr 详解](../../vol2-modern-features/02-unique-ptr.md)
- [cppreference: std::unique_ptr](https://en.cppreference.com/w/cpp/memory/unique_ptr)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
