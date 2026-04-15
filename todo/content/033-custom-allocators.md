---
id: 033
title: "自定义内存分配器专题"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: []
estimated_effort: large
---

# 自定义内存分配器专题

## 目标
深入讲解嵌入式场景下的自定义内存分配器。实现并对比多种分配器策略：固定大小对象池（Pool Allocator）、Slab 分配器、Arena/Region 分配器、栈分配器。分析内存碎片、分配延迟、线程安全等关键指标。演示 C++ placement new 和自定义删除器的使用，以及如何将自定义分配器集成到 STL 容器中。

## 验收标准
- [ ] 固定大小对象池（Pool Allocator）完整实现和性能测试
- [ ] Slab 分配器实现和与 Pool 分配器的对比
- [ ] Arena/Region 分配器实现（批量释放场景）
- [ ] 栈分配器实现（作用域分配）
- [ ] 内存碎片分析工具/方法文档
- [ ] 分配延迟基准测试数据
- [ ] 线程安全版本的实现和开销分析
- [ ] placement new 和自定义删除器使用教程
- [ ] STL 容器自定义分配器接口实现
- [ ] 每种分配器的适用场景选择指南

## 实施说明
内存管理是嵌入式系统的核心挑战。通用堆分配器（malloc/free）在嵌入式中有碎片化、不确定性等问题。本教程提供多种替代方案的实现和选型指导。

**内容结构规划：**

1. **嵌入式内存管理挑战** — 为什么不能用 malloc：碎片化、不确定性、内存泄漏风险。嵌入式内存布局回顾（Flash/SRAM 结构）。静态分配 vs 动态分配的权衡。C++ 中的内存管理：new/delete、allocator trait、placement new。

2. **固定大小对象池（Pool Allocator）** — 原理：预分配固定大小内存块，空闲链表管理。实现：模板化的 Pool 类，支持编译期指定块大小和数量。O(1) 分配和释放。内部碎片分析：块大小选择策略。使用场景：任务控制块、消息对象、固定大小数据包。线程安全版本：使用关中断或自旋锁保护。与 STL allocator 接口对接。

3. **Slab 分配器** — 原理：Linux 内核 Slab 分配器的嵌入式简化版。多大小类别管理：2^n 大小的多个 Pool。外部碎片避免策略。实现：SlabAllocator 类，管理多个 Pool。与纯 Pool 分配器的对比。使用场景：多种大小的动态对象分配。

4. **Arena/Region 分配器** — 原理：线性分配、整体释放。实现：Arena 类，指针递增分配。零碎片保证。reset() 整体释放 vs 单对象释放的限制。与 RAII 结合：ArenaGuard 作用域自动 reset。使用场景：事件处理循环、协议解析、临时数据计算。C++ 对象析构处理策略。

5. **栈分配器** — 原理：类似 Arena 但支持 LIFO 释放。实现：StackAllocator 类，支持 checkpoint/restore。嵌套作用域分配。使用场景：递归数据结构、嵌套临时计算。

6. **placement new 和自定义删除器** — placement new 语法和语义。与分配器配合使用。自定义删除器（用于 unique_ptr 的第二个模板参数）。allocate_unique 工厂函数实现。对齐要求处理（alignas、std::align）。

7. **STL 自定义分配器接口** — C++ Allocator trait 详解。实现一个完整的 STL 兼容分配器。与 std::vector、std::map 等容器集成。propagate_on_container_copy_assignment 等属性。实际效果：使用 Pool Allocator 的 std::vector。

8. **性能对比与选型指南** — 基准测试框架设计。对比维度：分配/释放延迟（最坏/平均）、内存利用率、碎片率、代码大小。对比表：malloc vs Pool vs Slab vs Arena vs Stack。决策树：根据应用场景选择分配器。混合策略：多种分配器组合使用。

## 涉及文件
- documents/embedded/topics/custom-allocators/index.md
- documents/embedded/topics/custom-allocators/01-memory-challenges.md
- documents/embedded/topics/custom-allocators/02-pool-allocator.md
- documents/embedded/topics/custom-allocators/03-slab-allocator.md
- documents/embedded/topics/custom-allocators/04-arena-allocator.md
- documents/embedded/topics/custom-allocators/05-stack-allocator.md
- documents/embedded/topics/custom-allocators/06-placement-new.md
- documents/embedded/topics/custom-allocators/07-stl-integration.md
- documents/embedded/topics/custom-allocators/08-benchmark-guide.md
- codes/embedded/allocators/ (配套代码和基准测试)

## 参考资料
- 《Effective C++》Item 49-52 (定制 new/delete) — Scott Meyers
- 《Effective Modern C++》Item 42 (placement new) — Scott Meyers
- cppreference.com — std::allocator, std::pmr
- 《The Art of Software Security Assessment》内存管理章节
- Linux Kernel Slab Allocator 设计论文
- Boost.Pool 文档
- std::pmr (C++17 多态内存资源) 参考资料
