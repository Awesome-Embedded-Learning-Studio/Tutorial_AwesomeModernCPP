---
title: "错误处理的现代方式"
description: "从错误码到 expected，错误处理方案的演进与选择"
---

# 错误处理的现代方式

错误处理是每个 C++ 程序员都要面对的核心问题——C 风格错误码可忽略、异常在嵌入式受限、裸指针表示"没有值"语义不清。现代 C++ 提供了 optional、variant、expected 等类型安全的替代方案。这一章我们回顾错误处理的演进历程，掌握每种方案的适用场景，最后给出一份场景化的选择指南。

## 本章内容

- [错误处理的演进：从错误码到类型安全](01-error-handling-evolution/)
- [optional 用于错误处理](02-optional-error/)
- [std::expected<T, E>：类型安全的错误传播](03-expected-error/)
- [错误处理模式总结：选择指南与最佳实践](04-error-patterns/)
