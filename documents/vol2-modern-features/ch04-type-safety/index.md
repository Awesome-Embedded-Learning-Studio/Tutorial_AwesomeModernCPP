---
title: "类型安全"
description: "用强类型、variant、optional 和 any 构建更安全的类型系统"
---

# 类型安全

C 时代的枚举、联合体和裸指针留下了太多类型安全的隐患——隐式转换、未定义行为、空指针解引用……现代 C++ 提供了一系列工具来堵住这些漏洞。这一章我们看看 enum class 如何终结枚举的隐式转换噩梦，强类型 typedef 如何防止参数混淆，variant 如何安全地替代 union，optional 如何优雅地表达"可能没有值"，以及 any 如何在需要时实现类型擦除。

## 本章内容

- [enum class 与强类型枚举](01-enum-class/)
- [强类型 typedef：防止混淆的类型安全](02-strong-types/)
- [std::variant：类型安全的联合体](03-variant/)
- [std::optional：优雅表达"可能没有值"](04-optional/)
- [std::any 与类型擦除](05-any/)
