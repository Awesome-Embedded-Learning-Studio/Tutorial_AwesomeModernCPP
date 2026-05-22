---
id: 030
title: "模板编程四卷系列"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002", "203"]
# NOTE: 本 TODO 为模板编程四卷系列的父级索引，详细清单见子 TODO 038-041
blocks: ["038", "039", "040", "041"]
estimated_effort: epic
---

# 模板编程四卷系列（父级索引）

## 目标
编写完整的模板编程四卷系列教程，从模板基础到高级元编程技术，再到嵌入式场景的实战应用，系统性地覆盖 C++ 模板的方方面面。每卷独立成章，循序渐进。

本文件为父级索引，详细验收标准和实施说明已拆分至子 TODO：

| 子 TODO | 卷 | 标题 | 章节数 | 综合项目 |
|---------|-----|------|--------|----------|
| 038 | 卷一 | 模板基础（C++11/14 核心机制） | T1.0-T1.8 (9节) | `fixed_vector<T, N>` |
| 039 | 卷二 | 现代模板技术（C++17 核心特性） | T2.1-T2.8 (8节) | 类型安全 `any` |
| 040 | 卷三 | 元编程精要（C++20/23 约束与元编程） | T3.1-T3.8 (8节) | mini-STL 算法库 |
| 041 | 卷四 | 泛型设计模式实战（架构级应用） | T4.1-T4.10 (10节) | 嵌入式事件系统 |

## 全局验收标准
- [ ] 四卷全部完成，每卷至少 8 个章节
- [ ] 每章配备完整代码示例与 static_assert 验证
- [ ] 嵌入式场景示例覆盖 GPIO/UART/SPI/中断等常见场景
- [ ] 每卷含综合项目，项目代码可编译运行
- [ ] 附录：代码膨胀控制、编译时间优化、嵌入式友好模板库选择

## 附录规划（四卷共用）

### A.1 代码膨胀控制
- extern template 实践
- 共享基类技术
- 模板 vs 运行时多态的权衡

### A.2 编译时间优化
- 头文件组织策略
- 预编译头（PCH）
- 模板实例化优化

### A.3 嵌入式友好的模板库选择
- ETL（Embedded Template Library）简介
- 自研模板库的最佳实践

## 参考资料
- 《C++ Templates: The Complete Guide》(2nd Edition) — David Vandevoorde et al.
- 《Modern C++ Design》— Andrei Alexandrescu
- 《C++ Template Metaprogramming》— Abrahams & Gurtovoy
- cppreference.com Templates 章节
- Meeting C++ 和 CppCon 模板相关演讲
