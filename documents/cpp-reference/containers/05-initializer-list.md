---
title: "std::initializer_list"
description: "用花括号 `{}` 初始化对象或传参时的轻量级代理类型"
chapter: 99
order: 5
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
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

# std::initializer_list（C++11）

## 一句话

一个轻量级的只读代理对象，让你能用花括号 `{}` 方便地给容器或自定义类传递任意数量的同类型初始值。

## 头文件

`#include <initializer_list>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `initializer_list() noexcept` | 创建空列表（通常由编译器隐式构造） |
| 元素数量 | `std::size_t size() const noexcept` | 返回列表中的元素个数 |
| 起始指针 | `const T* begin() const noexcept` | 指向首元素的指针 |
| 末尾指针 | `const T* end() const noexcept` | 指向末尾后一位置的指针 |
| 起始迭代器 | `const T* begin(std::initializer_list<T> il) noexcept` | 重载的 `std::begin` |
| 末尾迭代器 | `const T* end(std::initializer_list<T> il) noexcept` | 重载的 `std::end` |

## 最小示例

```cpp
// Standard: C++11
#include <iostream>
#include <initializer_list>
#include <vector>

struct Container {
    std::vector<int> v;
    Container(std::initializer_list<int> l) : v(l) {}
    void append(std::initializer_list<int> l) {
        v.insert(v.end(), l.begin(), l.end());
    }
};

int main() {
    Container c = {1, 2, 3}; // 隐式构造 initializer_list
    c.append({4, 5});
    for (int x : c.v) std::cout << x << ' ';
}
```

## 嵌入式适用性：高

- 底层实现通常仅包含一个指针和长度（或两个指针），内存开销极小
- 复制 `std::initializer_list` 不会复制底层数组，仅复制代理对象本身，无额外分配开销
- 底层数组可能存储在只读内存中，适合用于 ROM 化的静态配置表初始化

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 待补充 | 待补充 | 待补充 |

## 另见

- [教程：初始化列表](../vol3-standard-library/01-initializer-lists.md)
- [cppreference: std::initializer_list](https://en.cppreference.com/w/cpp/utility/initializer_list)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
