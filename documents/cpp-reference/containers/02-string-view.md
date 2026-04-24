---
title: "std::string_view"
description: "轻量级非拥有字符串视图，零拷贝引用连续字符序列"
chapter: 99
order: 2
tags:
  - host
  - cpp-modern
  - beginner
difficulty: beginner
cpp_standard: [17, 20, 23]
---

# std::string_view（C++17）

## 一句话

一种只读的字符串"视图"，不拷贝不分配内存，仅持有指针和长度，适合替代 `const std::string&` 做函数参数。

## 头文件

`#include <string_view>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `constexpr basic_string_view(const CharT* s, size_type count)` | 从指针和长度构造 |
| 构造 | `constexpr basic_string_view(const CharT* s)` | 从 C 字符串构造 |
| 长度 | `constexpr size_type size() const` | 返回字符数量 |
| 空检查 | `constexpr bool empty() const` | 检查是否为空 |
| 元素访问 | `constexpr const CharT& operator[](size_type pos) const` | 访问指定位置字符 |
| 数据指针 | `constexpr const CharT* data() const` | 返回底层字符数组指针 |
| 截取前缀 | `constexpr void remove_prefix(size_type n)` | 起始位置前移 n |
| 截取后缀 | `constexpr void remove_suffix(size_type n)` | 末尾位置前移 n |
| 子串 | `constexpr basic_string_view substr(size_type pos = 0, size_type count = npos) const` | 返回子串视图 |
| 查找 | `constexpr size_type find(basic_string_view v, size_type pos = 0) const` | 查找子串位置 |

## 最小示例

```cpp
#include <iostream>
#include <string_view>
// Standard: C++17

void print(std::string_view sv) {
    std::cout << sv << "\n";
}

int main() {
    std::string s = "hello";
    print(s);                    // 接受 std::string
    print("world");              // 接受字符串字面量
    std::string_view sv = s;
    sv.remove_prefix(1);         // 变为 "ello"
    print(sv.substr(0, 2));      // 输出 "el"
}
```

## 嵌入式适用性：高

- 零堆分配，仅有指针和长度两个成员，内存开销极小（通常 16 字节）
- TriviallyCopyable 类型，可安全用于中断上下文或 DMA 传输缓冲区解析
- 替代 `const std::string&` 避免隐式 `std::string` 构造带来的堆分配
- 需注意生命周期：绝不将临时 `std::string` 绑定到 `string_view`

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 7.1 | 4.0   | 19.10 |

## 另见

- [cppreference: std::basic_string_view](https://en.cppreference.com/w/cpp/string/basic_string_view)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
