---
id: "012"
title: "卷三：标准库深入 TODO"
category: content
priority: P1
status: rebuild
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: epic
---

# 卷三：标准库深入 TODO

## Current Assets

### 保留在卷三的文章（5 篇）

- `01-initializer-lists.md`：稳定，内容完整，无需改动。
- `01-array.md`：稳定，代码配套完整（`chapter07/01_array/` 7 个 .cpp）。
- `02-span.md`：稳定，代码配套完整（`chapter07/02_span/` 6 个 .cpp）。
- `05-object-size-and-trivial-types.md`：稳定，内容扎实（12 min）。
- `06-custom-allocators.md`：稳定，代码配套完整（`chapter07/06_custom_allocator/` 7 个源文件）。

### 参考卡与代码

- `documents/cpp-reference/containers/`（10 篇）和 `memory/`（5 篇）有参考卡，与正文交叉链接待统一补齐。
- 代码目录 `code/examples/chapter07/` 在迁移完成后应重组，去掉已迁走的主题子目录。

### 旧规划

- `todo/202-vol3-standard-library-outline.md`（git 历史中可恢复）：11 章 40-50 篇文章的完整旧规划，规模偏大，需裁剪后吸收。

## Maintenance Asset TODO

> M1–M4（type-safe-register / circular-buffer / intrusive-containers / ETL 迁移至 vol8、index.md 同步更新）已完成并归档。

### M5: 参考卡交叉链接 🕐 待触发

- **依据**：cpp-reference/containers/ 10 篇参考卡中 7 篇与 vol3 正文无双向链接。
- **决策**：在新增主要内容文章完成后统一补齐交叉链接。

## Anchor Implementation（已确认决策）

以 **libc++**（LLVM/Clang）为源码阅读锚点。涉及平台差异时补充对比 libstdc++（GCC）和 MSVC STL。

- libc++ 源码可读性最高（现代 C++ 风格、`__` 命名空间子目录清晰组织、宏较少）。
- 实际嵌入式编译仍用 libstdc++，但"读源码"和"编译用哪个"分开。
- 注意：glibc 是 C 标准库，不是 C++ 标准库。C++ 标准库三实现是 libstdc++、libc++、MSVC STL。

## Incremental Asset TODO

### 第一部分：数据结构与容器

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| 容器选择决策指南 | 决策树 + 复杂度表 + 内存局部性 + 迭代器失效规则速查 | 入口章 | 建议第一篇 |
| vector 深入 | 增长策略、reserve/shrink_to_fit、move 语义、三指针布局 | 高频 | 核心 |
| string 深入 | SSO/COW 历史、编码(char8_t)、resize_and_overwrite(C++23) | 高频 | |
| array / initializer_list | 已有文章，保留 | 高频 | 现成资产 |
| span | 已有文章，保留 | 高频 | 现成资产 |
| deque / list / forward_list | 分段连续 vs 节点式、fat iterator、splice | 中频 | 可合并一篇 |
| map / set | 红黑树基础、异构查找(C++14)、extract/merge(C++17) | 高频 | |
| unordered_map / unordered_set | 哈希策略、bucket 管理、load factor、自定义 hash | 高频 | |
| 容器适配器 | stack / queue / priority_queue + 底层容器选择 | 中频 | 可精简 |
| flat_map / flat_set (C++23) | 连续存储有序容器、cache 友好 | 中频新兴 | `needs standard-status verification` |
| C++26 新容器 | inplace_vector / hive / mdspan | 低频前瞻 | `needs standard-status verification` |

### 第二部分：迭代器与算法

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| 迭代器基础与 category | 5 种 category + C++20 contiguous + sentinel | 中频 | vol4 Ranges 前置 |
| 迭代器适配器 | reverse_iterator、insert_iterator、move_iterator、counted_iterator | 中频 | |
| 算法总览（上） | 非修改 + 修改序列操作（find/count/copy/transform/remove/unique） | 高频 | top-20 最常用 |
| 算法总览（下） | 排序/查找/集合/堆（sort/binary_search/merge/heap/nth_element） | 中频 | |
| 数值算法 numeric | accumulate/reduce/transform_reduce/gcd/lcm/iota | 中频 | |
| parallel algorithm | execution policy、并行版算法可用性 | 低频 | `needs standard-status verification` |

### 第三部分：其他通用设施

#### A. 字符串与文本

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| string_view 深入 | 与 string 的配合、陷阱、性能 | 高频 | vol2 已有基础，这里做深入 |
| cctype / cstring | 字符分类、C 字符串函数的 c++ 前缀使用规范 | 中频 | |
| charconv | to_chars/from_chars，最快数值转换 | 中频 | |
| format / print (C++20/23) | 格式化输出体系、自定义 formatter | 高频新兴 | `needs standard-status verification` |
| regex | 基础用法 + 性能警告 | 低频 | |
| locale / text_encoding | 本地化概述、text_encoding(C++26) | 低频 | `needs standard-status verification` |

#### B. I/O 与 filesystem

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| iostream 体系 | cin/cout/cerr、流状态、格式化操控器 | 高频 | |
| fstream / sstream | 文件读写、字符串流、内存流 | 高频 | |
| 新设施(C++20/23) | spanstream、syncstream、print | 中频新兴 | |
| filesystem 深入 | path 操作、目录遍历、权限、空间查询 | 中频 | 归入 vol3，按使用频率安排 |

#### C. 时间与数值

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| chrono 基础 | duration / time_point / clock / duration_cast / time_point_cast | 高频 | 展开到子主题 |
| chrono 高级 (C++20) | calendar types(year/month/day/weekday) / timezone / 与 format 配合 | 中频 | `needs standard-status verification` |
| cmath / numbers | 数学函数、C++20 数学常量(pi/e/sqrt2) | 高频 | 单独一篇 |
| random | mt19937、分布、seed、常见陷阱 | 中频 | 单独一篇 |
| complex / valarray | 复数、数值数组（简述） | 低频 | 可合并一篇 |
| C++26 数值前瞻 | linalg / simd / stdckdint | 低频 | `needs standard-status verification` |

#### D. 工具与诊断

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| pair / tuple / 结构化绑定 | 基础工具类型 | 高频 | |
| optional / variant / any / expected | vocabulary types 工程深入 | 高频 | vol2 已有基础，这里做工程深入 |
| functional | function / hash / invoke / move_only_function(C++23) | 高频 | vol2 已有基础 |
| bit 操作 | bit_cast / popcount / countl_zero / bitset (C++20) | 中频 | |
| error_code / system_error | 错误处理体系 | 中频 | |
| stacktrace (C++23) | 调用栈捕获 | 中频新兴 | `needs standard-status verification` |
| source_location (C++20) | 日志和断言的现代化 | 中频 | |
| allocator / PMR | 扩展现有文章，补 PMR 章节 | 中频 | 现成资产扩展 |

#### E. 语言支持速查

| 章节 | 覆盖内容 | 频率 | 备注 |
|------|---------|------|------|
| 语言支持头文件 | new/delete、exception、limits、cstddef/cstdint、version、compare | 中频 | 可合并一篇速查 |
| C 兼容头文件 | c 前缀 vs .h 形式的规范、嵌入式中的选择 | 中频 | |
| type_traits / concepts | 概览 + 常用 traits 速查 | 中频 | vol4 有深入，这里做概览 |

### 第四部分：源码阅读（中期）

锚点：libc++ 为主，涉及差异时对比 libstdc++ 和 MSVC STL。

| 优先级 | 组件 | 核心教学点 | 难度 |
|--------|------|----------|------|
| 1 | vector | 增长策略、placement new、三指针布局、摊销 O(1) | 入门 |
| 2 | string (SSO) | SSO 机制、union 存储、三家实现差异 | 入门 |
| 3 | unique_ptr | 零开销证明、自定义 deleter、EBO | 中级 |
| 4 | shared_ptr | 控制块架构、引用计数、make_shared 优化 | 中级 |
| 5 | list / deque | 节点结构、sentinel node、分段连续 | 中级 |
| 6 | map (rb-tree) | 红黑树旋转、着色规则 | 高级 |
| 7 | unordered_map | 链地址法、素数桶数、rehash 策略 | 高级 |
| 8 | allocator / iterator_traits | rebind、tag dispatch、SFINAE | 高级 |

核心 4 篇（vector/string/unique_ptr/shared_ptr）中期启动，其余视资源推进。

## Cross-Volume Boundaries

| 主题 | 归属 | 卷三角色 |
|------|------|---------|
| smart_pointers | vol2 | 仅交叉链接 |
| string_view 基础 | vol2 | vol3 做深入 |
| filesystem 基础 | vol2 | vol3 做深入 |
| variant/optional/any 基础 | vol2 | vol3 做工程深入 |
| ranges/views | vol4 | 仅交叉链接 |
| atomic/lock-free | vol5 | 仅交叉链接 |
| concepts 深入 | vol4 | vol3 仅速查概览 |
| coroutine | vol4 | 不覆盖 |
| circular buffer / intrusive container | 迁移至 vol8 | 不再维护 |
| type-safe register access | 迁移至 vol8 | 不再维护 |
| ETL | 迁移至 vol8 | 不再维护 |

## Development Points

- 新增内容按三部分顺序推进：数据结构 → 迭代器/算法 → 其他通用设施。
- 源码阅读作为中期目标，核心 4 篇（vector/string/unique_ptr/shared_ptr）优先。
- 每个标准库主题应包含：复杂度、生命周期、迭代器失效、异常安全、性能注意、工程选择。
- 代码示例优先复用 `code/examples/chapter07` 现有资产，缺口再补。
- C++26/frontier 内容全部标记 `needs standard-status verification`，使用官方或一手资料核验后再写。
- 具体文章粒度和预估篇幅在后续展开时单独讨论。

## Old TODO Merge

- `202-vol3-standard-library-outline.md`：11 章 40-50 篇，规模偏大，以上大纲已吸收其合理部分。
- `033-custom-allocators.md`：已由现有 `06-custom-allocators.md` 和 PMR 扩展计划覆盖。
- `034-stl-embedded.md`：嵌入式 STL 使用准则已通过交叉链接方案解决，不再独立建文。
- `022-core-theory-expansion.md`：数据结构/STL 相关内容已由本大纲覆盖。

## Acceptance

- [ ] 新卷三目录从平铺专题改为章节化结构。
- 第一部分（容器与数据结构）进度：
  - [x] vector 深入（`01-vector-deep-dive.md` 已落地）
  - [x] string 深入（`02-string-memory-deep-dive.md` 已落地）
  - [ ] 容器选择指南（决策树 + 复杂度表 + 内存局部性）
  - [ ] map / set
  - [ ] unordered_map / unordered_set
- [ ] 第二部分：至少完成迭代器基础、算法总览（上）。
- [ ] 第三部分：至少完成 chrono 基础、cmath/numbers、error_code/system_error。
- [ ] 源码阅读：至少完成 vector + string 源码阅读（中期）。
- [ ] 主要文章完成后统一补齐参考卡交叉链接（M5）。
