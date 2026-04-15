---
id: 028
title: "C++ 特性参考系统索引表"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: ["029"]
estimated_effort: large
---

# C++ 特性参考系统索引表

## 目标
创建 cppreference 风格的 C++ 特性索引页，作为本教程体系的导航中心。覆盖 C++98 到 C++23 每个标准的主要特性，每行一个特性，包含：特性名称、所属标准版本、相关头文件、一句话简述、详情页链接。索引表应支持按标准版本筛选和按类别（核心语言/标准库/并发/模板等）分组。

## 验收标准
- [ ] 索引页包含 C++98/03/11/14/17/20/23 所有主要特性条目
- [ ] 每条目包含：特性名、标准版本、头文件、简述、详情页链接
- [ ] 按标准版本分组展示，每组有版本概述
- [ ] 支持按类别筛选（核心语言 / 标准库 / 并发 / 模板 / 其他）
- [ ] 页面导航清晰，可快速跳转到目标标准/特性
- [ ] 与详情页（029）的链接结构预先定义
- [ ] 移动端友好的响应式布局

## 实施说明
索引页是 C++ 参考系统（028 + 029）的入口。设计时要考虑可扩展性和导航效率。

**内容结构规划：**

1. **页面头部** — C++ 标准演进时间线（C++98 -> C++03 -> C++11 -> C++14 -> C++17 -> C++20 -> C++23）。每个标准的发布年份、核心主题。快速筛选器（按版本、按类别）。

2. **按标准版本索引** — 每个标准版本一个区块，包含：
   - C++98：类、继承、虚函数、模板基础、STL 容器/算法、异常、RTTI、命名空间
   - C++03：值初始化修复、小改进
   - C++11：移动语义、右值引用、智能指针、lambda、auto、range-for、initializer_list、variadic templates、constexpr、nullptr、enum class、override/final、委托构造、统一初始化、std::thread/mutex/future、正则表达式、随机数库
   - C++14：泛型 lambda、返回类型推导、constexpr 放宽、make_unique、exchange
   - C++17：结构化绑定、if constexpr、折叠表达式、std::optional/variant/any、string_view、filesystem、并行算法、内联变量
   - C++20：Concepts、Ranges、Coroutines、Modules、three-way comparison、std::format、std::span、jthread、constexpr 大幅扩展
   - C++23：std::expected、std::print、std::flat_map/flat_set、deducing this、std::generator、多维下标运算符

3. **按类别索引** — 核心语言特性、标准库容器、算法与迭代器、并发与异步、模板与元编程、内存管理、I/O 与文件系统、字符串与文本、数学与数值。

4. **嵌入式相关性标注** — 每个特性标记嵌入式适用性：高（直接可用）、中（需评估开销）、低（通常避免）。作为读者快速筛选嵌入式相关特性的参考。

**数据格式：** 使用 Markdown 表格，确保可被静态站点生成器正确渲染。

## 涉及文件
- documents/cpp-reference/index.md

## 参考资料
- cppreference.com — C++ 标准参考
- ISO C++ 标准草案 (N4950 for C++23)
- C++ 标准委员会提案 (Papers)
- 《The C++ Programming Language》— Bjarne Stroustrup
- 《Effective Modern C++》— Scott Meyers
- Compiler Support 页面 (en.cppreference.com/w/cpp/compiler_support)
