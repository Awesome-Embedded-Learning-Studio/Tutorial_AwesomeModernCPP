---
id: 034
title: "STL 嵌入式实现专题"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: []
estimated_effort: large
---

# STL 嵌入式实现专题

## 目标
从零实现嵌入式友好的 STL 替代容器和算法。包括 fixed_vector（固定容量 vector）、static_string（固定容量 string）、ring_buffer（循环缓冲区）、intrusive_list（侵入式链表）、flat_map（紧凑有序映射）。与 ETL (Embedded Template Library) 进行对比分析，帮助读者理解容器内部原理并做出正确的选型决策。

## 验收标准
- [ ] fixed_vector 完整实现：支持迭代器、emplace_back、编译期容量
- [ ] static_string 完整实现：基本字符串操作、printf 格式化支持
- [ ] ring_buffer 完整实现：SPSC 无锁版本和通用版本
- [ ] intrusive_list 完整实现：侵入式节点、链表操作
- [ ] flat_map 完整实现：基于排序数组的有序映射
- [ ] 每个容器配备完整的使用示例和性能测试
- [ ] 与 std 原版和 ETL 库的功能对比表
- [ ] 内存占用分析（ROM/RAM）
- [ ] 异常安全策略文档（嵌入式禁用异常时的替代方案）
- [ ] 容器选型决策指南

## 实施说明
嵌入式系统中标准库容器的主要问题是动态内存分配。本教程实现的容器全部使用固定大小存储，消除堆分配和碎片化风险。

**内容结构规划：**

1. **嵌入式容器设计原则** — 为什么不用 std::vector/std::string：动态分配的不确定性。设计目标：无堆分配、固定大小、编译期确定内存、异常禁用下的安全保障。异常处理策略：断言/错误码/std::optional/std::expected。迭代器设计基础。

2. **fixed_vector** — 接口设计：尽量兼容 std::vector 接口。内部存储：std::array 风格的嵌入式存储 + size 计数器。关键方法：push_back、emplace_back、pop_back、operator[]、at（边界检查版本）、迭代器。容量溢出策略：断言截断 / 返回错误码 / oldest-discarding。模板参数：`fixed_vector<T, N>`。示例：传感器采样缓冲区、任务参数存储。

3. **static_string** — 接口设计：兼容 std::string 常用接口。内部存储：固定大小字符数组 + 长度计数器。关键方法：append、substr、find、compare、c_str。格式化支持：snprintf 封装、format_to 方法。编码考虑：ASCII vs UTF-8 子集。模板参数：`static_string<N>`。示例：日志消息构建、设备名称存储。

4. **ring_buffer** — 接口设计：push/pop/peek/front/back。单生产者-单消费者（SPSC）无锁版本：原子索引、内存屏障。通用版本：互斥保护。功率-of-2 大小优化（掩码取模）。迭代器设计（环形迭代器）。模板参数：`ring_buffer<T, N>`。示例：UART 接收缓冲区、ADC 采样缓冲区、日志缓冲区。

5. **intrusive_list** — 侵入式容器概念：节点嵌入数据结构内部而非外部包装。节点类型：list_node（prev/next 指针）。关键方法：push_front、push_back、insert、remove、splice。与 std::list 的对比：内存开销分析。容器所有权语义。示例：任务就绪队列、定时器链表、事件队列。

6. **flat_map** — 原理：基于排序连续数组的关联容器。接口设计：兼容 std::map 常用接口。内部存储：fixed_vector<std::pair<Key, Value>, N>。查找：二分查找（O(log n)）。插入：移动元素保持有序。与 std::map 的对比：缓存友好性、内存连续性。模板参数：`flat_map<Key, Value, N>`。示例：命令查找表、配置参数表。

7. **辅助算法** — 为嵌入式优化的算法：fixed_find（线性查找）、binary_search（二分查找）、min/max_element。排序算法选择：小数组用插入排序、中等数组用快速排序。

8. **与 ETL 库对比** — ETL 库功能概览。逐容器对比：接口兼容性、内存开销、代码大小。自实现的优势：教学价值、完全可控。ETL 的优势：成熟测试、广泛覆盖。选型建议：何时自实现、何时用 ETL、何时用 std。

## 涉及文件
- documents/embedded/topics/stl-embedded/index.md
- documents/embedded/topics/stl-embedded/01-design-principles.md
- documents/embedded/topics/stl-embedded/02-fixed-vector.md
- documents/embedded/topics/stl-embedded/03-static-string.md
- documents/embedded/topics/stl-embedded/04-ring-buffer.md
- documents/embedded/topics/stl-embedded/05-intrusive-list.md
- documents/embedded/topics/stl-embedded/06-flat-map.md
- documents/embedded/topics/stl-embedded/07-algorithms.md
- documents/embedded/topics/stl-embedded/08-etl-comparison.md
- codes/embedded/stl-embedded/ (配套代码和测试)

## 参考资料
- ETL (Embedded Template Library) — wellsfrog/etl
- EASTL (EA Standard Template Library) — electronicarts/eastl
- 《Effective STL》— Scott Meyers
- 《Elements of Programming》— Stepanov & McJones
- std::vector / std::string 参考实现 (libstdc++ / libc++)
- 《C++ Concurrency in Action》— Anthony Williams (ring buffer 章节)
