---
id: "013"
title: "卷四：高级主题 TODO"
category: content
priority: P1
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: epic
---

# 卷四：高级主题 TODO

## Volume Boundary

卷四聚焦 **C++20 及以上新特性**。

模板编程作为卷四的**基础纵深**保留在卷四内，独立演化——它是 Concepts / Ranges / Coroutines 等特性的前置与纵深。

## Current Assets

### 有实质内容的文章（7 篇）

| 文件 | 主题 | C++ 标准 | 状态 |
|------|------|---------|------|
| `01-coroutine-basics.md` | 协程基础 | C++20 | 需重写风格 |
| `02-coroutine-scheduler.md` | 协程调度器 | C++20 | 需重写风格 |
| `05-spaceship-operator.md` | 三路比较运算符 | C++20 | 稳定 |
| `msvc-cpp-modules.md` | C++ Modules（仅 MSVC） | C++20 | 需大力扩充 |
| `vol2-modern-cpp17/06-designated-initializers.md` | 指定初始化器 | C++20 | 稳定 |
| `vol2-modern-cpp17/07-ranges-basics-and-views.md` | Ranges 基础与视图 | C++20 | 稳定 |
| `vol2-modern-cpp17/08-ranges-pipeline-in-practice.md` | Ranges 管道实战 | C++20 | 稳定 |

### 仅骨架的文章（4 个子目录，39 个规划主题，0 篇正文）

| 子目录 | 规划主题数 | 内容范围 |
|--------|----------|---------|
| `vol1-basics-cpp11-14/` | 10 | 函数模板、类模板、特化、NTTP、依赖名、友元注入、CRTP |
| `vol2-modern-cpp17/`（模板部分） | 9 | type traits、SFINAE、if constexpr、fold expression、完美转发、CTAD |
| `vol3-metaprogramming-cpp20-23/` | 9 | Concepts、requires 表达式、TMP、编译期字符串、反射基础 |
| `vol4-generics-patterns/` | 11 | Policy、type erasure、mixin、tag dispatch、设计模式模板实现 |

### 代码资产

- 全部推倒重来，跟着具体文章一起规划和重建。
- 卷十 CppCon `01-concept-based-generic-programming/`：20+ concepts 示例，属卷十，可交叉引用。
- 卷五 `ch06-async-io-coroutine/`：协程代码，属卷五，不动。

## Maintenance Asset TODO（已确认决策）

### M1: 协程文章风格对齐 ✅ 已确认

- **依据**：两篇协程文章使用第一人称博客风格（"笔者""你怎么知道我"），与项目教程风格不一致。且与卷五 Ch06 有内容重叠。
- **决策**：保留在卷四，定位为"C++20 协程 API 初探"。重写为教程风格（按 `.claude/style/writing-style.md`），在文章开头或小结处添加引导：
  > 本文介绍 C++20 协程的语言机制与基础 API。如需了解协程在并发/异步 I/O 中的完整应用，请前往卷五 · 异步 I/O 与协程。
- **交付物**：两篇文章重写 + 交叉链接引导。
- **与卷五的关系**：卷四 = 语言机制视角（co_await/co_yield/co_return 编译器变换、协程帧、promise_type 接口）；卷五 = 并发/异步应用视角（调度器、事件循环、Echo Server）。两层覆盖，互不替代。

### M2: 模板归属 ✅ 已确认

- **依据**：模板与 Concepts / Ranges / Coroutines 深度互连，拆开增加交叉引用负担。模板可在卷四内独立演化，无需开新卷。
- **决策**：模板编程保留在卷四，作为独立演化轨道。卷四描述为"C++20+ 新特性与模板编程深入"。
- **交付物**：子目录结构重组（消除 vol1/vol2/vol3/vol4 命名冲突），模板规划从 39 篇收缩到可执行规模。
- **待定**：子目录重组方案在增量资产阶段一并讨论。

### M3: 已完成文章归属确认 ✅ 已确认

- **依据**：卷三 TODO 的 Cross-Volume Boundaries 已确认 Ranges → vol4。Designated initializers (C++20)、Ranges (C++20) 均符合卷四"C++20+"边界。
- **决策**：3 篇已完成文章保留在卷四，无需迁移。`vol2-modern-cpp17/` 子目录命名问题在整体结构重组时一并修复。

### M4: Modules 扩充 → 转为增量资产 ✅ 已确认

- **依据**：当前仅覆盖 MSVC，需要大力补充多编译器覆盖和深入讲解。
- **决策**：从维护资产升级为增量资产，在增量规划阶段详细讨论。

### M5: 代码推倒重来 🕐 推迟到增量资产阶段

- **依据**：维护者确认代码资产推倒重来。
- **决策**：先完成文章决策，代码在增量资产阶段跟着具体文章一起规划和重建。

### M6: index.md 更新 🕐 待触发

- **依据**：当前 index.md 标注"部分内容已有（待重写）"，与实际状态不符。
- **决策**：在子目录结构重组完成后统一更新，不单独提前修改。

## Incremental Asset TODO

### I1: 模板编程系列（全部重写）

旧骨架（4 个子目录 39 个主题）全部废弃，按以下大纲重新规划。

#### 权威参考资料（写作时必须参考）

**⭐ 必须参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| C++ Templates: The Complete Guide (2nd Ed) | https://www.tmplbook.com/ | 模板编程圣经，覆盖 C++11/14/17 |
| Modern C++ Design — Alexandrescu | ISBN 978-0201704310 | Policy-Based Design、设计模式轨道核心参考 |
| cppreference: templates 总览 | https://en.cppreference.com/cpp/language/templates | 标准参考 |
| cppreference: 函数模板 | https://en.cppreference.com/cpp/language/function_template | 标准参考 |
| cppreference: 类模板 | https://en.cppreference.com/cpp/language/class_template | 标准参考 |
| cppreference: 模板参数与实参 | https://en.cppreference.com/cpp/language/template_parameters | 标准参考 |
| cppreference: 模板实参推导 | https://en.cppreference.com/cpp/language/template_argument_deduction | 标准参考 |
| cppreference: 约束与 Concepts | https://en.cppreference.com/cpp/language/constraints | Concepts 标准参考 |
| cppreference: `<concepts>` | https://en.cppreference.com/cpp/header/concepts | 标准库 concepts 参考 |
| cppreference: `<type_traits>` | https://en.cppreference.com/cpp/header/type_traits | type traits 参考 |
| P0734R0 — Concepts 进入 C++20 | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0734r0.pdf | Concepts 语义权威定义 |
| CppCon: Template Normal Programming — O'Dwyer | https://www.youtube.com/watch?v=vwrXHznaYLA | 模板教学结构参考 |
| CppCon: Back to Basics: Concepts — Josuttis | https://www.youtube.com/watch?v=jzwqTi7n-rg | Concepts 教学结构参考 |

**○ 推荐参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| Template Metaprogramming with C++ — Pikus | https://github.com/PacktPublishing/Template-Metaprogramming-with-CPP | 最新 TMP 专著 (2022)，覆盖 C++20 |
| C++ Template Metaprogramming — Abrahams, Gurtovoy | ISBN 978-0321227256 | Boost.MPL 权威指南 |
| cppreference: SFINAE | https://en.cppreference.com/cpp/language/sfinae | SFINAE 标准参考 |
| cppreference: 参数包 | https://en.cppreference.com/cpp/language/parameter_pack | 变参模板参考 |
| cppreference: 折叠表达式 | https://en.cppreference.com/cpp/language/fold | 折叠表达式参考 |
| cppreference: CTAD | https://en.cppreference.com/cpp/language/class_template_argument_deduction | CTAD 参考 |
| cppreference: 包索引 (C++26) | https://en.cppreference.com/cpp/language/pack_indexing | 前沿参考 |
| P0847R7 — Deducing This | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r6.html | C++23 显式对象参数 |
| P2996R13 — Reflection | https://isocpp.org/files/papers/P2996R13.html | C++26 反射 |
| CppCon: Modern C++ Metaprogramming — Brown | https://www.youtube.com/watch?v=Am2is2QCvxY | TMP 教学参考 |
| CppCon: A Bit of SFINAE — O'Dwyer | https://www.youtube.com/watch?v=ybaE9qlhHvw | SFINAE 教学参考 |
| CppCon: C++20 Templates: The Next Phase | https://www.youtube.com/watch?v=_Foq4WnrGuNU | C++20 模板演进 |
| CppCon: Template-less TMP — Jusiak | https://www.youtube.com/watch?v=yriNqhv-oM0 | 现代 TMP 趋势 |
| Arthur O'Dwyer 博客 | https://quuxplusone.github.io/blog/ | TMP、type erasure、CTAD |
| Andrzej Krzemieński 博客 | https://akrzemi1.wordpress.com/ | Concepts 实战 |
| Barry Revzin 博客 | https://brevzin.github.io/ | Reflection、C++20 深入 |
| Rainer Grimm — Modernes C++ | https://www.modernescpp.com/ | Core Guidelines 模板规则 |
| Sandor Dargo 博客 | https://www.sandordargo.com/ | Deducing this、Pack indexing |

#### 大纲：四阶段 + 综合项目

**第一阶段：模板基础（6-8 篇）** — 目标：能读懂模板代码

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| T01 | 函数模板 | 实例化、重载解析、显式/推导实参、ADL | tmplbook Ch1-2, cppreference |
| T02 | 类模板 | 实例化、成员函数、友元、静态成员、嵌套类型 | tmplbook Ch3-4 |
| T03 | 非类型模板参数 (NTTP) | 整数 NTTP、C++20 类类型 NTTP、编译期字符串 | tmplbook Ch5, P0732R2 |
| T04 | 模板特化与偏特化 | 全特化、偏特化、特化的选择规则 | tmplbook Ch6 |
| T05 | 模板实参推导 | 推导规则、decltype、auto 与模板推导的关系 | tmplbook Ch15, cppreference |
| T06 | 名称查找与依赖名 | 两阶段名称查找、`typename`/`template` 消歧、ADL 深入 | tmplbook Ch14 |
| T07 | 可变参数模板 | 参数包、包展开模式、递归终止 | tmplbook Ch12, cppreference |
| T08 | 折叠表达式 | 一元/二元折叠、左折叠/右折叠、实用模式 | cppreference/fold, Pikus |

**第二阶段：现代模板技术（6-8 篇）** — 目标：能写模板代码

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| T09 | type_traits 体系 | type traits 分类、实现原理、常用 traits 速查 | cppreference/type_traits, Brown 2014 |
| T10 | SFINAE 与 enable_if | SFINAE 原理、`std::enable_if`、void_t 技巧 | tmplbook Ch8, O'Dwyer SFINAE 演讲 |
| T11 | if constexpr | 编译期分支、递归模板简化、与 SFINAE 的对比 | cppreference, Pikus |
| T12 | CTAD | 推导指引、别名模板 CTAD、聚合 CTAD、常见陷阱 | tmplbook Ch17, O'Dwyer CTAD 博客 |
| T13 | constexpr/consteval 与模板 | 编译期计算、constexpr 函数模板、consteval 约束 | cppreference, Grimm 博客 |
| T14 | Concepts 入门 | `concept` 定义、`requires` 子句、标准库 concepts、错误信息改善 | Josuttis 2024, cppreference/constraints |
| T15 | Concepts 实战 | 约束组合、requires 表达式、自定义 concept、与 SFINAE 对比 | Krzemieński 博客, P0734R0 |
| T16 | Concepts 与模板设计 | 约束的模板排序、Concepts 下的重载解析、缩写函数模板 | cppreference, Josuttis 2024 |

**第三阶段：设计模式与泛型架构（4-6 篇）** — 目标：能设计模板架构

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| T17 | CRTP 与静态多态 | CRTP 原理、对象计数器、链式方法、与虚函数的对比 | Alexandrescu, tmplbook |
| T18 | Deducing This (C++23) | 显式对象参数、替代 const/非 const 重载、替代 CRTP | P0847R7, Dargo 博客 |
| T19 | Type Erasure | `std::function` 原理、手写 type erasure、小对象优化 | O'Dwyer type erasure 博客, tmplbook |
| T20 | Policy-Based Design | 策略类、组合式设计、现代改进 | Alexandrescu Ch1-4 |
| T21 | Tag Dispatch 与 if constexpr 分派 | 经典 tag dispatch、现代 if constexpr 替代、选择指南 | O'Dwyer TMP 博客 |
| T22 | 模板友元与 Barton-Nackman | 友元注入、ADL 运算符、与 Concepts 结合 | tmplbook Ch9 |

**第四阶段：前沿展望（2-3 篇）** — 目标：建立方向感

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| T23 | Pack Indexing 与结构化绑定包 (C++26) | `Pack...[I]` 语法、消除递归 TMP、`template for` 展望 | P2662R3, Dargo 博客 |
| T24 | Reflection 预览 (C++26) | `^` 运算符、`std::meta`、对 TMP 的范式转变 | P2996R13, Revzin 博客 |
| T25 | 模板编程的未来 | Contracts 与模板、TMP 简化趋势总结 | P2632R0, P2900R14 |

**综合项目：**

| 项目 | 插入时机 | 教学目标 |
|------|---------|---------|
| `fixed_vector<T, N>` | 第一阶段末 | 综合运用类模板、NTTP、特化、SFINAE/Concepts 约束 |
| 类型安全的 `any` / `function_ref` | 第三阶段末 | 综合运用 type erasure、SFINAE→Concepts 演进、小对象优化 |

**总计：~25 篇 + 2 个综合项目**

#### 子目录重组

- 废弃 `vol1-basics-cpp11-14/`、`vol2-modern-cpp17/`、`vol3-metaprogramming-cpp20-23/`、`vol4-generics-patterns/` 四个旧子目录。
- 新子目录方案待维护者确认后执行（在 M2 结构重组时一并处理）。

### I2: Concepts → 已由 I1 模板系列覆盖

Concepts（T14-T16）已融入模板编程大纲的第二阶段。Concepts 是模板编程的自然演进，不独立成系列。

### I3: Modules 深入（大力扩充）

当前仅 1 篇 MSVC 文章（`msvc-cpp-modules.md`），需全面重写扩充为多编译器覆盖的完整教程。

#### 权威参考资料（写作时必须参考）

**⭐ 必须参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| cppreference: Modules | https://en.cppreference.com/w/cpp/language/modules | 标准参考 |
| MSVC 官方教程 | https://learn.microsoft.com/en-us/cpp/cpp/tutorial-import-stl-named-module?view=msvc-170 | MSVC 实践 |
| Clang Modules 文档 | https://clang.llvm.org/docs/StandardCPlusPlusModules.html | Clang 实践 |
| GCC Modules 文档 | https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Modules.html | GCC 实践 |
| CMake cxxmodules 手册 | https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html | 构建系统 |
| P2465R3 — std/std.compat 模块 | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2465r3.pdf | `import std;` 定义 |

**○ 推荐参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| Are We Modules Yet | https://arewemodulesyet.org | 编译器/CMake 支持状态追踪 |
| Chuanqi Xu 实践指南 (2025) | https://chuanqixu9.github.io/c++/2025/08/14/C++20-Modules.en.html | GCC modules 实战 |
| Mathieu Ropert: Modules in 2026 | https://mropert.github.io/2026/04/13/modules_in_2026/ | 生态现状评估 |

#### 大纲：~4 篇（近期）+ 1 篇远期

**近期交付：**

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| M01 | Modules 原理与动机 | `#include` 的根本缺陷（重复解析、宏污染、传染依赖）、Modules 如何解决、BMI 概念 | MSVC 文档, Ropert 2026 |
| M02 | 模块接口与实现单元 | `export`、`module`、`module :private`、模块分区、头文件单元、全局模块片段 | cppreference, Clang 文档 |
| M03 | `import std;` 与标准库模块 | C++23 `import std;` / `import std.compat;`、三大编译器支持状态 | P2465R3, MSVC 教程 |
| M04 | 迁移策略与常见陷阱 | 从头文件迁移的路线图、增量迁移策略、编译时间对比、宏隔离、ABI、ODR | Chuanqi Xu 指南, Ropert 2026 |

**远期（CMake modules 支持更稳定后）：**

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| M05 | 构建系统与工程实践 | CMake modules 支持（`target_sources(FILE_SET cxx_modules TYPE CXX_MODULES)`）、Ninja 支持、混合头文件/模块项目策略 | CMake 手册, Are We Modules Yet |

**C++26 Module 相关：** `needs standard-status verification` — 暂无重大新 module 语言特性，主要改进在工具链和库侧。

### I4: Ranges 进阶

现有 2 篇入门文章（基础与视图、管道实战）已完成。需扩展进阶内容。

#### 权威参考资料（写作时必须参考）

**⭐ 必须参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| cppreference: Ranges 库 | https://en.cppreference.com/cpp/ranges | 标准参考 |
| P2214R2 — C++23 Ranges 计划 | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p2214r2.html | C++23 ranges 设计意图 |
| P2387R3 — range_adaptor_closure | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p2387r3.html | 自定义 view 管道支持 |
| CppCon: Taming the Filter View — Josuttis | https://www.youtube.com/watch?v=c1gfbbEzts | filter_view 陷阱必看 |
| CppCon: C++20 Ranges in Practice — Brindle | https://www.youtube.com/watch?v=d_E-VLyUnzc | 实战教学结构参考 |

**○ 推荐参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| P2760R1 — C++26 Ranges 计划 | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2023/p2760r1.html | C++26 前瞻 |
| Hannes Hauswedell: Writing Views from Scratch | https://hannes.hauswedell.net/post/2018/04/11/view1/ | 自定义 view 教程（黄金标准） |
| Barry Revzin: Ranges 博客 | https://brevzin.github.io/tags/ranges/ | Projections、enumerate 深入 |
| Bartlomiej Filipek: Ranges 性能基准 | https://www.cppstories.com/2022/ranges-perf/ | 性能对比数据 |
| Tristan Brindle: Rvalue Ranges | https://tristanbrindle.com/posts/rvalue-ranges-and-views | 悬垂引用分析 |
| Simon Toth: Implementing Custom Views | https://medium.com/@simontoth/daily-bit-e-of-c-implementing-custom-views-bb21e63a2d4f | 自定义 view 实战 |
| P3329R0 — Healing the Filter View | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/p3329r0.pdf | filter_view 修复提案 |
| C++Now 2025: Custom Views — Sorkin | https://www.youtube.com/watch?v=5iXUCcFP6H4 | 现代 view 实现 |
| Marius Bancila: Custom Range View | https://mariusbancila.ro/blog/2020/06/06/a-custom-cpp20-range-view/ | 自定义 view 实战 |

#### 大纲：~5 篇（近期）+ 3 篇远期

**近期交付：**

| # | 主题 | 核心内容 | 主要参考 |
|---|------|---------|---------|
| R01 | Range 概念与类别体系 | `range` → `input_range` → ... → `contiguous_range` 层次、`sized_range`、`viewable_range`、view 如何传播/降级类别 | cppreference, Brindle |
| R02 | Sentinel 与 Borrowed Range | sentinel/iterator 解耦、`default_sentinel`、`unreachable_sentinel`、`borrowed_range` 概念、`dangling` | cppreference, Grimm |
| R03 | Projections 深入 | `ranges::sort(v, {}, &Person::age)` 原理、projection 作为函数适配器、与 view 的交互 | Revzin 博客 |
| R04 | 性能与陷阱 | 零开销的真相（benchmark）、debug 性能、编译时间、`transform\|filter` 双重计算、`filter_view` 缓存陷阱、悬垂引用模式 | Filipek 基准, Josuttis 2024, Brindle |
| R05 | C++23 Ranges 新特性 | `zip`、`enumerate`、`chunk`、`slide`、`stride`、`join_with`、`repeat`、`cartesian_product`、`ranges::to`、编译器支持表 | P2214R2, cppreference |

**远期（条件成熟后）：**

| # | 主题 | 核心内容 | 触发条件 |
|---|------|---------|---------|
| R06 | 自定义 View 与 View Adaptor | `view_interface`、迭代器/sentinel 设计、C++23 `range_adaptor_closure`、`bind_back`、逐步实现完整自定义 view | R01-R05 交付后，参考素材充分时 |
| R07 | `std::generator` 与 Ranges | `std::generator<T>` 基础、`co_yield`、generator 作为 `input_range`、与 view 组合 | Clang 支持后 |
| R08 | C++26 Ranges 展望 | P2760R1 计划、`cache_last`、`concat`、`scan`、`transform_join`、`as_input` | `needs standard-status verification` |

### I5: C++23/26 前沿特性

对不属于模板/Modules/Ranges 三条主线的 C++23/26 特性进行规划。

#### 权威参考资料（写作时必须参考）

**⭐ 必须参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| cppreference: C++23 编译器支持 | https://en.cppreference.com/cpp/compiler_support/23 | C++23 特性支持矩阵 |
| cppreference: C++26 编译器支持 | https://en.cppreference.com/cpp/compiler_support/26 | C++26 特性支持矩阵 |
| P0847R7 — Deducing This | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r6.html | 已由 I1 T18 覆盖 |

**○ 推荐参考：**

| 资源 | 链接 | 用途 |
|------|------|------|
| P2900R14 — Contracts | https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p2900r14.pdf | C++26 Contracts |
| Sandor Dargo 博客 | https://www.sandordargo.com/ | 多个 C++23/26 特性详解 |
| Rainer Grimm — Modernes C++ | https://www.modernescpp.com/ | C++23/26 特性系列 |

#### 大纲：按教程就绪度分级

**A 级（稳定，三大编译器均支持，可立即编写）：**

| # | 主题 | C++ 标准 | 核心内容 | 编译器支持 |
|---|------|---------|---------|-----------|
| F01 | `if consteval` | C++23 | 替代 `std::is_constant_evaluated()`、与 `if constexpr` 的区别 | GCC 12+ / Clang 14+ / MSVC 19.44+ |
| F02 | 多维 `operator[]` | C++23 | `operator[](size_t i, size_t j)` 语法、多维数组/矩阵应用 | GCC 12+ / Clang 15+ / MSVC 19.35+ |
| F03 | `auto(x)` 衰变复制 | C++23 | `auto(x)` / `auto{x}` 语法、按值传递的模式、与模板推导的关系 | GCC 12+ / Clang 15+ / MSVC 19.50+ |
| F04 | `std::expected<T, E>` | C++23 | 值或错误、与 `optional` / `variant` / 异常的对比、函数式组合 | GCC 12+ / Clang 16+ / MSVC 19.33+ |
| F05 | `std::print` / `std::println` | C++23 | 类型安全格式化输出、自定义 formatter、与 `std::format` 的关系 | GCC 15+ / Clang 18+ / MSVC 19.37+ |
| F06 | `static operator()` | C++23 | 无状态函数对象优化、与 lambda 的交互 | GCC 13+ / Clang 16+ / MSVC 19.44+ |

**B 级（大部分稳定，2/3 编译器支持，编写时标注最低版本）：**

| # | 主题 | C++ 标准 | 核心内容 | 编译器支持 |
|---|------|---------|---------|-----------|
| F07 | `std::flat_map` / `std::flat_set` | C++23 | 连续存储有序容器、cache 友好、接口兼容 `map`/`set` | GCC 15+ / Clang 19+ / MSVC 19.38+ |
| F08 | `std::stacktrace` | C++23 | 调用栈捕获、调试与日志应用、与 `source_location` 配合 | GCC 12+ / Clang 18+ / MSVC 19.35+ |
| F09 | Pack Indexing | C++26 | `Pack...[I]` 语法、消除递归 TMP | 已由 I1 T23 覆盖 |
| F10 | Contracts 预览 | C++26 | 前置条件/后置条件、`contract_assert`、与模板的交互 | `needs standard-status verification`，GCC 16 初始支持 |
| F11 | `std::function_ref` | C++26 | 非拥有类型擦除可调用引用、无堆分配、与 `std::function` 对比 | GCC 16+ / Clang/MSVC 尚不支持 |

**C 级（实验性，仅做路线卡式介绍）：**

| # | 主题 | C++ 标准 | 核心内容 | 备注 |
|---|------|---------|---------|------|
| F12 | Reflection 路线卡 | C++26 | `^` 运算符、`std::meta`、编译器支持路线图 | 已由 I1 T24 覆盖深入教程 |
| F13 | `std::simd` 路线卡 | C++26 | 数据并行 SIMD 抽象、与 vol6 性能优化交叉 | `needs standard-status verification` |
| F14 | `std::linalg` 路线卡 | C++26 | 线性代数库、BLAS 接口 | `needs standard-status verification` |
| F15 | `= delete("reason")` | C++26 | 删除函数的原因字符串 | GCC 15+ / Clang 19+，小特性 |

**注意**：F09（Pack Indexing）和 F12（Reflection）已在 I1 模板系列 T23/T24 中深入覆盖，I5 中不重复，只做交叉链接。

#### I5 建议结构

将 A/B 级特性按主题分组，而非按标准版本平铺：

| 组 | 包含 | 篇数 |
|----|------|------|
| C++23 语言特性 | F01 + F02 + F03 + F06 | 可合并为 2 篇（语言特性上/下） |
| C++23 库特性 | F04 + F05 + F07 + F08 | 可合并为 2 篇（库特性上/下） |
| C++26 前沿路线卡 | F10 + F11 + F13 + F14 + F15 + I1 已覆盖特性的 2-3 行概述 | 1 篇路线卡汇总 |

**总计：~5 篇**（2 语言 + 2 库 + 1 路线卡）

**已确认决策：**
- `std::expected`、`std::print`、`std::flat_map` 等标准库新特性归卷四（卷四边界 = C++20+），不归卷三。
- Contracts（F10）只在路线卡中提及存在和基本概念，不写独立文章。编译器支持成熟后重新规划。
- 路线卡中 I1 已覆盖的特性（Pack Indexing、Reflection）保留 2-3 行概述 + 链接，不重复。

## Cross-Volume Boundaries

| 主题 | 归属 | 卷四角色 |
|------|------|---------|
| 协程（并发/异步应用） | vol5 Ch06 | 卷四仅做语言机制初探，交叉链接到卷五 |
| Concepts 研读笔记 | vol10 CppCon | 卷四做教程，交叉链接到卷十研读 |
| 容器/Ranges 操作对象 | vol3 | 卷四 Ranges 文章的前置，交叉链接 |
| 迭代器基础 | vol3 | vol4 Ranges 前置，交叉链接 |
| 嵌入式设计模式 | vol8 | 卷四泛型设计保留通用视角，嵌入式视角交叉到卷八 |
| 并发模式 | vol5 | 卷四不覆盖 |
| 编译加速（Modules 效益） | vol7 | 交叉链接 |
| type_traits / concepts 概览 | vol3 | vol3 做速查，vol4 做深入教程 |

## Old TODO Merge

- `030-template-series.md` → 本文件模板轨道
- `038-template-vol1-basics.md` → 本文件模板轨道
- `039-template-vol2-modern.md` → 本文件模板轨道
- `040-template-vol3-metaprogramming.md` → 本文件模板轨道
- `041-template-vol4-design-patterns.md` → 本文件模板轨道
- `031-design-patterns.md` → 本文件泛型设计轨道，嵌入式部分交叉到 `017-vol8-domains.md`
- `203-vol4-advanced-outline.md` → 本文件

## Acceptance

- [ ] M1 协程文章重写完成，风格统一，交叉链接到位。
- [ ] 模板子目录结构重组完成，消除命名冲突。
- [ ] 模板规划从 39 篇收缩到可执行规模。
- [ ] Modules 文章扩充完成（多编译器覆盖）。
- [ ] 代码推倒重来完成，所有 vol4 代码有 CMakeLists.txt。
- [ ] index.md 在结构重组后统一更新。
- [ ] C++26/frontier 内容均标记 `needs standard-status verification`，核验后再展开。

## Volume Summary

| 增量方向 | 近期篇数 | 远期篇数 | 状态 |
|---------|---------|---------|------|
| I1: 模板编程系列 | ~25 篇 + 2 项目 | — | 大纲完成，待执行 |
| I2: Concepts | 已由 I1 T14-T16 覆盖 | — | — |
| I3: Modules 深入 | 4 篇 | 1 篇（CMake 集成） | 大纲完成，待执行 |
| I4: Ranges 进阶 | 5 篇 | 3 篇（自定义 View / generator / C++26） | 大纲完成，待执行 |
| I5: C++23/26 前沿 | 5 篇 | — | 大纲完成，待执行 |
| **总计新增** | **~39 篇 + 2 项目** | **4 篇** | |
