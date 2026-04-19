---
title: "std::shared_ptr"
description: "通过引用计数共享对象所有权的智能指针"
chapter: 2
order: 0
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
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

# std::shared_ptr（C++11）

## 一句话

多个智能指针可以共同拥有同一个对象，当最后一个拥有者被销毁或重置时，对象才会自动释放。

## 头文件

`#include <memory>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `shared_ptr()` | 构造空指针（默认） |
| 构造（工厂） | `template<class T, class... Args> shared_ptr<T> make_shared(Args&&... args)` | 分配并构造对象（C++11） |
| 重置 | `void reset()` | 释放当前管理对象的所有权 |
| 获取原始指针 | `T* get() const noexcept` | 返回存储的指针 |
| 解引用 | `T& operator*() const noexcept` | 解引用存储的指针 |
| 箭头操作 | `T* operator->() const noexcept` | 通过指针访问成员 |
| 引用计数 | `long use_count() const noexcept` | 返回共享该对象的 shared_ptr 数量 |
| 布尔转换 | `explicit operator bool() const noexcept` | 检查是否管理非空对象 |
| 交换 | `void swap(shared_ptr& r) noexcept` | 交换两个 shared_ptr 管理的对象 |

## 最小示例

```cpp
#include <iostream>
#include <memory>
struct Foo { Foo() { std::cout << "Foo()\n"; } ~Foo() { std::cout << "~Foo()\n"; } };
int main() {
    std::shared_ptr<Foo> p1 = std::make_shared<Foo>();
    std::shared_ptr<Foo> p2 = p1; // 引用计数变为 2
    std::cout << "count: " << p1.use_count() << "\n";
    p1.reset(); // count: 1
    p2.reset(); // 析构 Foo
}
```

## 嵌入式适用性：中

- 内部维护控制块和原子引用计数，存在额外的内存和 CPU 开销
- 拷贝操作本身是线程安全的，适合多任务间共享资源
- 在 RAM 和 Flash 极度受限的单片机上应谨慎使用，优先考虑 unique_ptr

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 待补充 | 待补充 | 待补充 |

## 另见

- [教程：shared_ptr 详解](../../vol2-modern-features/03-shared-ptr.md)
- [cppreference: std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
