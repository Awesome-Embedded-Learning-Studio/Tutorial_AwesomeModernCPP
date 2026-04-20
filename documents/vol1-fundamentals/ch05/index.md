---
title: "数组与字符串"
description: "从 C 风格数组到 std::array 和 std::string 的演进"
---

# 数组与字符串

数据总得有地方放，而数组和字符串是最基础的容器。这一章我们先回顾一下 C 风格数组的底层机制——搞清楚它为什么这么"裸"，然后直接上 `std::array`，体验一下零开销抽象有多香。字符串部分同样从 C 风格字符串过渡到 `std::string`，你会发现现代 C++ 处理文本的体验和 C 完全不在一个时代。

## 本章内容

- [C 风格数组](01-c-arrays/)
- [std::array](02-std-array/)
- [std::string](03-std-string/)
