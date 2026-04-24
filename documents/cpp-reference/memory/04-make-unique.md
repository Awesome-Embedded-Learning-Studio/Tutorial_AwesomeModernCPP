---
title: "std::make_unique"
description: "安全构造 unique_ptr 的工厂函数，避免直接使用 new 导致的异常安全隐患"
chapter: 99
order: 4
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [14, 17, 20, 23]
---

# std::make_unique（C++14）

## 一句话

安全地创建 `std::unique_ptr`，比直接写 `new` 更安全且代码更简洁。

## 头文件

`#include <memory>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造对象 | `template<class T, class...Args> unique_ptr<T> make_unique(Args&&... args)` | 创建非数组类型的 unique_ptr (C++14) |
| 构造数组 | `template<class T> unique_ptr<T> make_unique(std::size_t size)` | 创建未知边界数组，元素值初始化 (C++14) |
| 禁止定长数组 | `template<class T, class...Args> /* unspecified */ make_unique(Args&&... args) = delete` | 已知边界数组被显式删除 (C++14) |
| 默认初始化对象 | `template<class T> unique_ptr<T> make_unique_for_overwrite()` | 创建非数组类型，默认初始化 (C++20) |
| 默认初始化数组 | `template<class T> unique_ptr<T> make_unique_for_overwrite(std::size_t size)` | 创建未知边界数组，默认初始化 (C++20) |

## 最小示例

```cpp
#include <memory>
#include <cstdio>
// Standard: C++14
struct Foo {
    Foo(int v) : val(v) { std::printf("Foo(%d)\n", val); }
    ~Foo() { std::printf("~Foo()\n"); }
    int val;
};
int main() {
    auto p1 = std::make_unique<Foo>(42);
    auto p2 = std::make_unique<Foo[]>(3);
}
```

## 嵌入式适用性：高

- 零开销抽象，编译后与直接使用 `new` 完全等价
- 显式表达独占所有权语义，避免资源泄漏
- 避免了 `new` 表达式与 `unique_ptr` 构造分离导致的异常安全隐患
- C++14 起可用，主流嵌入式编译器均已支持

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 待补充 | 待补充 | 待补充 |

## 另见

- [cppreference: std::make_unique](https://en.cppreference.com/w/cpp/memory/unique_ptr/make_unique)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
