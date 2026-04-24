---
title: "constexpr 与编译期计算"
description: "让计算发生在编译期，实现真正的零开销抽象"
---

# constexpr 与编译期计算

如果一段计算的结果在编译期就能确定，为什么还要等到运行时再做？constexpr 让函数和变量在编译期求值成为可能，consteval 和 constinit 则进一步提供了编译期的硬性保证。这一章我们从 constexpr 基础出发，理解字面类型和 constexpr 构造函数的设计约束，掌握 C++20 的新工具，最后在实战中综合运用编译期查表、字符串处理和设计模式。

## 本章内容

- [constexpr 基础：编译期求值的艺术](01-constexpr-basics/)
- [constexpr 构造函数与字面类型](02-constexpr-ctor/)
- [consteval 与 constinit：编译期保证的新工具](03-consteval-constinit/)
- [编译期计算实战：从查表到编译期字符串](04-compile-time-practice/)
