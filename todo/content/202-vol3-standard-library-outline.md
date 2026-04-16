---
id: "202"
title: "卷三：标准库深入 — 全部章节大纲与文章规划"
category: content
priority: P1
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200", "201"]
blocks: ["203"]
estimated_effort: epic
---

# 卷三：标准库深入 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol3-standard-library
- **难度范围**：intermediate → advanced
- **预计文章数**：40-50 篇（含 8-10 篇源码分析）
- **前置知识**：卷一 + 卷二
- **C++ 标准覆盖**：C++11-23
- **目录位置**：`documents/vol3-standard-library/`
- **源码分析**：使用 third_party/ 下的 libstdc++, libc++, MSVC STL submodule

## 章节大纲

### ch00：容器深入

- **难度**：intermediate → advanced
- **预计篇数**：8
- **核心知识点**：vector/string/deque/list/forward_list 深入

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-vector-deep-dive.md | vector 深入 | 内存增长策略(1.5x/2x)、reserve/capacity、push_back vs emplace_back | 自定义 allocator 支持 |
| 00-02 | 02-string-deep-dive.md | string 深入 | SSO(Small String Optimization)、COW(旧实现)、移动语义、性能 | 字符串拼接优化 |
| 00-03 | 03-deque.md | deque 分段结构 | 分段连续存储、push_front/push_back 复杂度、与 vector 对比 | 实现 chunk deque |
| 00-04 | 04-list-forward-list.md | list 与 forward_list | 双向/单向链表、splice 操作、内存开销分析 | 链表操作实战 |
| 00-05 | 05-container-adaptors.md | 容器适配器 | stack/queue/priority_queue、底层容器选择、自定义比较 | 实现自定义适配器 |
| 00-06 | 06-container-performance.md | 容器性能全面对比 | 插入/删除/查找 benchmark、缓存友好性、内存局部性 | 性能测试套件 |
| 00-07 | 07-container-selection-guide.md | 容器选择指南 | 决策树、常见场景、权衡分析 | 场景选型练习 |
| 00-08 | 08-emplace-vs-push.md | emplace vs push | 原位构造、完美转发、性能差异、何时使用 | emplace 实践 |

### ch01：关联容器

- **难度**：intermediate → advanced
- **预计篇数**：6

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-ordered-containers.md | 有序关联容器 | 红黑树原理、map/set/multimap/multiset、比较器 | 自定义比较器 |
| 01-02 | 02-unordered-containers.md | 无序关联容器 | 哈希表原理、桶/负载因子、冲突解决、rehash | 自定义 hash 函数 |
| 01-03 | 03-custom-key.md | 自定义键类型 | hash 特化、operator==、transparent comparator | 异构查找 |
| 01-04 | 04-flat-containers.md | flat_map/flat_set (C++23) | 基于 sorted vector 的关联容器、缓存友好 | 性能对比 |
| 01-05 | 05-associative-performance.md | 关联容器性能对比 | 有序 vs 无序 benchmark、内存/时间权衡 | 全面性能测试 |
| 01-06 | 06-associative-patterns.md | 关联容器常用模式 | 计数/分组/索引/缓存/LRU | 实现小型缓存 |

### ch02：迭代器

- **难度**：advanced
- **预计篇数**：4

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-iterator-categories.md | 迭代器分类 | Input/Output/Forward/Bidirectional/RandomAccess/Contiguous(C++20) | 实现各类迭代器 |
| 02-02 | 02-custom-iterators.md | 自定义迭代器 | iterator_traits、迭代器适配器、sentinel(C++20) | 自定义容器迭代器 |
| 02-03 | 03-iterator-adaptors.md | 迭代器适配器 | reverse_iterator、back_insert_iterator、stream_iterator | 适配器组合使用 |
| 02-04 | 04-iterator-invalidation.md | 迭代器失效 | 各容器失效规则、调试技巧、safe iterator | 失效场景分析 |

### ch03：算法库

- **难度**：intermediate → advanced
- **预计篇数**：6

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-sorting-searching.md | 排序与查找 | sort/stable_sort/binary_search/equal_range、复杂度分析 | 自定义排序策略 |
| 03-02 | 02-transforming.md | 变换与复制 | transform/copy/remove/replace/unique、惰性求值预告 | 数据管道构建 |
| 03-03 | 03-numeric.md | 数值算法 | accumulate/reduce/inner_product/partial_sum、并行版本 | 统计计算实战 |
| 03-04 | 04-heap-permutation.md | 堆与排列 | make_heap/sort_heap、next_permutation、排列生成 | 排列算法应用 |
| 03-05 | 05-parallel-algorithms.md | 并行算法 (C++17) | execution policies(par/seq/par_unseq)、并行 transform/sort | 并行性能测试 |
| 03-06 | 06-constrained-algorithms.md | 约束算法 (C++20) | ranges 算法、投影(projection)、C++20 std::ranges 命名空间 | ranges 算法使用 |

### ch04：字符串深入

- **难度**：advanced
- **预计篇数**：4

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-string-encoding.md | 字符编码 | ASCII/UTF-8/UTF-16/UTF-32、char/wchar_t/char8_t/char16_t/char32_t | 编码转换 |
| 04-02 | 02-string-performance.md | 字符串性能 | SSO 深入、copy-on-write(历史)、字符串视图 vs string、分配策略 | 性能优化实践 |
| 04-03 | 03-string-format.md | std::format (C++20) | format 语法、自定义 formatter、性能 vs sprintf/iostream | 自定义格式化 |
| 04-04 | 04-text-processing.md | 文本处理实战 | 分词、解析、正则预告、多语言文本处理 | 实现简单解析器 |

### ch05：流与 I/O

- **难度**：intermediate
- **预计篇数**：4

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 05-01 | 01-stream-architecture.md | 流架构 | streambuf/ios/ostream/istream、locale、自定义流 | 理解流层次 |
| 05-02 | 02-file-io.md | 文件 I/O | fstream、二进制读写、随机访问、错误处理 | 文件处理工具 |
| 05-03 | 03-custom-manipulators.md | 自定义 manipulator | 格式化输出、自定义操纵器、流状态管理 | 日志格式化 |
| 05-04 | 04-io-performance.md | I/O 性能 | sync_with_stdio(false)、缓冲区管理、scanf vs cin | I/O 性能优化 |

### ch06：时间库

- **难度**：intermediate → advanced
- **预计篇数**：3

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-chrono-basics.md | chrono 基础 | duration/time_point/clock、运算、字面量 | 时间测量工具 |
| 06-02 | 02-chrono-advanced.md | chrono 进阶 | 自定义时长、时钟类型、C++20 日历/时区 | 实现定时器 |
| 06-03 | 03-time-practice.md | 时间实战 | 性能计时、超时控制、日期处理 | benchmark 框架 |

### ch07：正则表达式

- **难度**：intermediate
- **预计篇数**：2

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-regex-basics.md | regex 基础 | 正则语法、match/search/replace、迭代器、性能注意事项 | 文本匹配工具 |
| 07-02 | 02-regex-practice.md | regex 实战 | 输入验证、日志解析、配置文件处理、性能替代方案 | 实用正则场景 |

### ch08：随机数

- **难度**：intermediate
- **预计篇数**：2

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 08-01 | 01-random-basics.md | 随机数库 | engine/distribution/device、seed、常见分布 | 随机数据生成 |
| 08-02 | 02-random-practice.md | 随机数实战 | 蒙特卡洛模拟、测试数据生成、随机采样 | 蒙特卡洛应用 |

### ch09：分配器

- **难度**：advanced
- **预计篇数**：3

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 09-01 | 01-allocator-interface.md | 分配器接口 | allocator 要求、value_type/pointer/rebind、C++11 简化接口 | 自定义分配器 |
| 09-02 | 02-custom-allocators.md | 自定义分配器实战 | 栈分配器、池分配器、arena 分配器、追踪分配器 | 实现追踪分配器 |
| 09-03 | 03-pmr.md | PMR (C++17) | polymorphic memory resource、memory_resource、synchronized_pool/unsynchronized_pool | PMR 实战 |

### ch10：span 与 array

- **难度**：intermediate
- **预计篇数**：3

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 10-01 | 01-std-array.md | std::array 深入 | 聚合类型、结构化绑定、tuple 接口、性能 | array 工具函数 |
| 10-02 | 02-std-span.md | std::span (C++20) | 视图语义、固定/动态大小、与 string_view 对比、安全性 | span API 设计 |
| 10-03 | 03-mdspan.md | std::mdspan (C++23) | 多维 span、layout policy、accessor policy | 多维数组处理 |

### source-analysis：STL 源码分析专区

- **难度**：advanced
- **预计篇数**：8-10
- **核心知识点**：阅读和分析主流 STL 实现源码

#### 文章列表

| 编号 | 文件名 | 标题 | 核心内容 | 分析目标 |
|------|--------|------|---------|---------|
| sa-01 | 01-stl-overview.md | STL 实现概览 | libstdc++/libc++/MSVC STL 架构对比、代码组织 | 选择分析目标 |
| sa-02 | 02-unique-ptr-source.md | unique_ptr 源码分析 | 模板结构、删除器设计、空基类优化 | 零开销抽象 |
| sa-03 | 03-shared-ptr-source.md | shared_ptr 源码分析 | 控制块、引用计数、make_shared 优化、线程安全 | 共享所有权机制 |
| sa-04 | 04-vector-source.md | vector 源码分析 | 内存管理、增长策略、异常安全、move vs copy | 动态数组实现 |
| sa-05 | 05-string-source.md | string 源码分析 | SSO 实现、copy-on-write(历史)、移动语义 | 字符串优化 |
| sa-06 | 06-tuple-source.md | tuple 源码分析 | 递归继承实现、空基类优化、结构化绑定支持 | 变参模板技巧 |
| sa-07 | 07-optional-source.md | optional 源码分析 | 存储布局、平凡性保证、engaged 状态管理 | 可选值实现 |
| sa-08 | 08-type-traits-source.md | type_traits 源码分析 | SFINAE 技巧、编译期检测、is_detected | 模板元编程基础 |
| sa-09 | 09-functional-source.md | std::function 源码分析 | 类型擦除、small buffer optimization、callable 包装 | 类型擦除模式 |
| sa-10 | 10-chrono-source.md | chrono 源码分析 | duration/time_point 模板、ratio 计算、类型安全 | 编译期算术 |

## 练习与项目

### 文章末尾练习
- 每篇 3-5 道，重点关注源码阅读理解和实现原理
- 包含"手写简化版"练习

### 实战项目
1. **手写 mini STL**：实现简化版 vector/string/unique_ptr/optional
2. **自定义分配器套件**：栈分配器 + 池分配器 + 追踪分配器

### third_party 管理
```
third_party/
├── libstdc++/    # git submodule: gcc/libstdc++-v3
├── libcxx/       # git submodule: llvm/llvm-project/libcxx
└── stl/          # git submodule: microsoft/STL
```

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| core-embedded-cpp/ch07/* (6篇) | ch10(span/array) + ch09(allocators) | 通用化并扩展 |
| core-embedded-cpp/ch03/01-initializer-lists.md | ch00 容器深入 | 融入 |
| core-embedded-cpp/ch03/05-object-size-trivial.md | ch00 容器深入 | 融入 |
| core-embedded-cpp/ch08/02-type-safe-register.md | ch10(span) | 通用化 |
