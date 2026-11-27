---
id: "000"
title: "legacy 规划摘录"
category: governance
priority: P1
status: archived
created: 2026-11-25
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# legacy 规划摘录

本文件是 2026 年 6 月形成的 14 份规划文件（todo 目录下的 README、000 总路线图、010 到 020 各卷与项目、031 QA）的压缩摘录，只保留仍然有价值的决策、未完成的方向、明确放弃的事项、写作原则与参考资料。各文件当时的状态描述、已完成的条目、旧 TODO 合并过程的叙述不收录，原文在 git 历史里。

本文件是素材库，不是待办清单。各项专题的大纲在同一目录下按编号排列，排期以那些文件为准。每条前面的方括号编号是原文件里的编号，查原文或 git 历史时按它找。

每份摘录开头有一段注记，说明该文件与 2026 年 11 月仓库现状的出入。出入以注记为准，正文保留原规划的说法。

## 来源索引

| 原文件 | 行数 | 合并到 |
|---|---|---|
| README.md | 100 | 总览 |
| 000-project-roadmap.md | 78 | 总览 |
| 010-vol1-fundamentals.md | 144 | 卷一到卷三 |
| 011-vol2-modern-features.md | 146 | 卷一到卷三 |
| 012-vol3-standard-library.md | 196 | 卷一到卷三 |
| 013-vol4-advanced-topics.md | 411 | 卷四与卷五 |
| 014-vol5-concurrency.md | 244 | 卷四与卷五 |
| 015-vol6-performance.md | 474 | 卷六与卷七 |
| 016-vol7-engineering.md | 251 | 卷六与卷七 |
| 017-vol8-domains.md | 578 | 卷八 |
| 018-vol9-open-source-study.md | 171 | 卷九 |
| 019-vol10-lecture-notes.md | 92 | 卷十 |
| 020-projects.md | 142 | 贯穿式实战项目 |
| 031-qa-knowledge-base.md | 57 | QA 与知识库 |

合计 3084 行。

## 总览

### README.md 与 000-project-roadmap.md（todo 入口与总路线图，原文 100 与 78 行）

**决策**

- `todo/` 只保留两类文件：内容发展 TODO（路线图、各卷路线、贯穿式项目）与社区建设 TODO。社区治理、站点质量、翻译维护、发布治理的规则说明保存在 CONTRIBUTING.md，不进入 todo 文件。
- 状态图例五档：stable（稳定资产以维护为主）、active（值得近期推进）、rebuild（需重建主线）、skeleton（只有入口暂不投入）、governance（路线图入口）。
- 旧 TODO 清理原则：过细的单篇 TODO 合并进卷级清单，不再保留独立文件。

**未完成的方向**

（000 的 P2 与 P3 里，结合 2026-11 的仓库现状仍属未来的部分）

- 卷九从 Chromium 系列扩展 TaskRunner 主线。
- 编译链接卷与参考卡的维护增强与交叉链接。
- 卷八非嵌入式子域：networking、GUI、data-storage、algorithms（当时为骨架，2026-11 时 networking 已写四篇加一个实验，GUI 与数据存储仍为占位，algorithms 的方向由 data-structure 子域的 mini STL 系列在走）。
- 贯穿式大型项目的持续推进。
- 卷十课程笔记的持续扩展。

**已放弃**

- RP2040 与 RP 平台路线（原因：嵌入式路线以 STM32F1 为主，ESP32 可作远期平台，RP 不再占规划位）。
- 000 的 P1 内容线与 P0 稳定 TODO 入口各项（原因：卷五已按七关加 cookbook 重写，卷三已按六个子目录写成，TODO 入口已建立，这些条目完成）。

**原则与约定**

- 所有大主题规划必须回答四件事：已有资产、缺口、下一步、验收标准。
- 单篇文章级任务不再新建独立 TODO，除非它阻塞整卷发布。
- C++26 与前沿内容必须标记 needs standard-status verification，除非已完成来源核验。

**参考资料**

- 无

## 卷一到卷三

### 010-vol1-fundamentals.md（卷一：基础入门，原文 144 行）

**决策**

- 卷一定位为稳定资产，不再新增大主题。理由：后续目标是降低读者摩擦，不是继续堆章节。附录型内容（常见错误索引）不算大主题，可以按需添加。
- 跨卷链接的措辞由执行时根据文章上下文决定，不在规划里写死。

**未完成的方向**

- [N-1] 跨卷前向链接：ch04 智能指针预告指向卷二，ch09 模板初步指向卷四，ch10 异常处理指向卷二与卷四，ch11 STL 初见指向卷三，ch12 内存模型指向卷六、卷七、卷八，c_tutorials 的 ARM 架构与嵌入式 C 模式两篇指向卷八。
- [N-3] 删除 `code/examples/vol1/` 遗留代码（16 个无 CMakeLists 的 .cpp，已被 `code/volumn_codes/vol1/` 覆盖），删前确认无引用。
- [M-1] 各章节 index.md 添加配套练习引导：GitHub 链接为主，附带本地路径，覆盖 13 个章节的 88 个练习文件。
- [M-2] c_tutorials 进阶专题 8 篇补 Compiler Explorer 交互链接：host 可编译的优先（C 陷阱、OOP-in-C、动态数组、链表、可复用代码），ARM 相关的用 ARM 编译器视图。
- [M-3] 卷末常见错误附录：按主题组织（指针、生命周期、数组退化、引用绑定、异常安全、内存泄漏），每个模式一两句描述加链接到卷内章节，不改动已稳定正文。
- [L-1] 「学完卷一之后」过渡指引，依赖 N-1 全部到位后再做。

**已放弃**

- 卷一大规模扩写（原因：内容已完整，重心转为维护）。

**原则与约定**

- 无。

**参考资料**

- 无

### 011-vol2-modern-features.md（卷二：现代特性，原文 146 行）

**决策**

- 卷间边界：卷二只承接 C++11 到 C++17 已稳定、工程常用的核心语言特性。受控延伸的 C++20/23 内容必须在文章中明确标注标准版本。新标准库类型优先进卷三，高阶语言机制（consteval、fold expressions、concepts）优先进卷四，并发语义优先进卷五。
- C++20 constexpr 扩展（constexpr 析构、constexpr new/delete）保留在卷二，作为 constexpr 概念的自然增量。std::expected 保留在卷二 ch10，作为错误处理演进的收尾。optional monadic 操作保留在 ch04，标注前瞻。
- consteval 与 constinit 文章连同关联代码迁移到卷四，归入「C++20 编译期保证」主题，ch02 的 index 与描述同步调整，英文镜像同步迁移。

**未完成的方向**

- ch00 移动语义全章 5 篇补「参考资源」小节，ch11 的 udl-basics 补「小结」。
- 标准版本准确性：constexpr-ctor 一篇的 frontmatter 需补 C++20 并在正文加分节标记。四处 C++26 前瞻需核验标准状态：CTAD 的 pair-like 推导、constexpr 的 std::sin、std::zstring_view（P3655）、optional monadic 操作。
- rvo-nrvo 一篇补 Guaranteed Copy Elision（C++17 保证复制消除）专门小节。
- 练习补充：当前 44 篇里只有 3 篇有练习。第一梯队 6 篇（移动语义、完美转发、constexpr 基础、泛型 lambda、variant、expected）各补 2 到 3 道，第二梯队 6 篇巩固型再补。
- 交叉链接方向：ch00 指向卷九 OnceCallback 与卷四完美转发，ch01 指向卷八指针语义系列，ch02 指向卷四编译期编程与卷六，ch03 指向卷九 callback 系列与卷四 Ranges，ch09 指向卷七文件复制器，ch10 指向卷八 UART 错误处理。
- ch05 与 ch07 补 CMakeLists.txt，`code/examples/vol2/` 补构建入口。

**已放弃**

- 无。

**原则与约定**

- 卷一重写稳定后，核验卷二各章前置要求的准确性。

**参考资料**

- 无

### 012-vol3-standard-library.md（卷三：标准库，原文 196 行）

**决策**

- 源码阅读以 libc++ 为锚点，涉及平台差异时对比 libstdc++ 与 MSVC STL。理由：libc++ 源码可读性最高。实际嵌入式编译仍用 libstdc++，读源码与编译分开。注意 glibc 是 C 标准库，C++ 标准库三实现是 libstdc++、libc++、MSVC STL。
- 卷间边界表：智能指针归卷二仅交叉链接，string_view 与 filesystem 基础在卷二深入在卷三，variant 与 optional 基础在卷二工程深入在卷三，ranges 与 concepts 深入归卷四，atomic 归卷五，环形缓冲、侵入式容器、type-safe register、ETL 迁移至卷八不再维护。
- 参考卡交叉链接（cpp-reference 的 containers 与 memory 参考卡同正文的双向链接）在主要内容文章完成后统一补齐。

**未完成的方向**

- 源码阅读系列：核心 4 篇（vector 的增长策略与三指针布局、string 的 SSO 与三家实现差异、unique_ptr 的零开销证明、shared_ptr 的控制块架构）中期启动，其余（list、deque、map 红黑树、unordered_map、allocator 与 iterator_traits）视资源推进。
- 参考卡交叉链接补齐（同上决策，尚未触发）。

**已放弃**

- 旧 `202-vol3-standard-library-outline.md` 的 11 章 40 到 50 篇规模（原因：规模偏大，已裁剪后吸收）。

**原则与约定**

- 每个标准库主题应包含：复杂度、生命周期、迭代器失效、异常安全、性能注意、工程选择。
- C++26 与前瞻内容全部标记 needs standard-status verification，用官方或一手资料核验后再写。

**参考资料**

- 无

（注：012 的增量内容大纲在 2026 年内已基本写完，卷三现为六个子目录约 50 篇，本摘录只保留尚未触发的源码阅读与参考卡链接两项。）

## 卷四与卷五

### 013-vol4-advanced-topics.md（卷四：高级主题，原文 411 行）

**决策**

- [M1] 两篇协程文章保留在卷四，定位为「C++20 协程 API 初探」，重写为教程风格，文首或小结处加向卷五的引导链接。理由：原为第一人称博客风格，与项目教程风格不一致，且与卷五协程一章内容重叠。分工为卷四讲语言机制（co_await、co_yield、co_return 的编译器变换，协程帧，promise_type 接口），卷五讲并发与异步应用（调度器、事件循环、Echo Server），两层覆盖互不替代。
- [M2] 模板编程保留在卷四作为独立演化轨道，卷四描述改为「C++20+ 新特性与模板编程深入」。理由：模板与 Concepts、Ranges、Coroutines 深度互连，分开会增加交叉引用负担，无需开新卷。
- [M3] 三篇已完成文章（指定初始化器、Ranges 基础与视图、Ranges 管道实战）保留在卷四不迁移。理由：均符合卷四 C++20+ 边界，卷三 TODO 的卷间边界已确认 Ranges 归卷四。
- [M4] Modules 从维护资产升级为增量资产。理由：现有文章仅覆盖 MSVC，需补充多编译器覆盖和深入讲解。
- [M5] 卷四代码资产推倒重来，推迟到增量资产阶段跟着具体文章一起规划和重建（维护者已确认）。卷十 CppCon 的 concepts 示例属卷十可交叉引用，卷五协程代码不动。
- [M6] index.md 更新推迟到子目录结构重组完成后统一做。理由：当前标注「部分内容已有（待重写）」与实际状态不符，不单独提前修改。
- [I5 内已确认] std::expected、std::print、std::flat_map 等标准库新特性归卷四不归卷三，理由是卷四边界为 C++20+。Contracts 只在路线卡中提存在和基本概念，不写独立文章，编译器支持成熟后重新规划。路线卡中模板系列已覆盖的特性（Pack Indexing、Reflection）只保留两三行概述加链接，不重复。

**未完成的方向**

- [I1] 模板编程系列全部重写，四阶段加大纲：第一阶段模板基础 6 到 8 篇（函数模板、类模板、NTTP 含 C++20 类类型与编译期字符串、特化与偏特化、实参推导、两阶段查找与依赖名、可变参数模板、折叠表达式）。第二阶段现代模板技术 6 到 8 篇（type_traits、SFINAE 与 enable_if、if constexpr、CTAD、constexpr 与 consteval 和模板、Concepts 入门与实战与模板设计）。第三阶段设计模式与泛型架构 4 到 6 篇（CRTP、Deducing This、type erasure、Policy-Based Design、tag dispatch 与 if constexpr 分派、模板友元与 Barton-Nackman）。第四阶段前沿展望 2 到 3 篇（Pack Indexing 与结构化绑定包、Reflection 预览、模板编程的未来）。综合项目两个：fixed_vector<T, N>（第一阶段末）、类型安全的 any 与 function_ref（第三阶段末）。总计约 25 篇加 2 个项目。
- [I3] Modules 深入：近期 4 篇（原理与动机、模块接口与实现单元、import std 与标准库模块、迁移策略与常见陷阱），远期 1 篇构建系统与工程实践，等 CMake modules 支持更稳定后写。
- [I4] Ranges 进阶：近期 5 篇（Range 概念与类别体系、Sentinel 与 borrowed range、Projections、性能与陷阱、C++23 新特性）。远期 3 篇各有触发条件：自定义 view 与 adaptor（近期五篇交付且参考素材充分后）、std::generator（Clang 支持后）、C++26 展望（需核验标准状态）。
- [I5] C++23/26 前沿特性按教程就绪度分级。A 级可立即编写：if consteval、多维 operator[]、auto(x) 衰变复制、std::expected、std::print 与 println、static operator()。B 级标注最低版本：flat_map 与 flat_set、stacktrace、function_ref。C 级只做路线卡：std::simd、std::linalg、= delete 带原因字符串。建议按主题分组为 2 篇语言特性加 2 篇库特性加 1 篇路线卡共约 5 篇，不按标准版本平铺。
- 子目录重组：废弃 vol1-basics-cpp11-14、vol2-modern-cpp17、vol3-metaprogramming-cpp20-23、vol4-generics-patterns 四个旧子目录，新方案待维护者确认后执行。

**已放弃**

- [I2] Concepts 独立系列（原因：已由 I1 模板系列的 Concepts 部分覆盖，Concepts 是模板编程的自然演进）。
- 旧模板骨架四个子目录 39 个规划主题（原因：全部废弃，按 I1 新大纲重新规划）。

**原则与约定**

- 卷四边界为 C++20 及以上新特性，模板编程作为基础纵深保留在卷四内独立演化。
- 跨卷分工：协程的应用视角归卷五，Concepts 研读笔记归卷十，容器与迭代器基础及 type_traits 与 concepts 速查归卷三，嵌入式设计模式归卷八，并发模式归卷五不覆盖，编译加速（Modules 效益）归卷七，均以交叉链接衔接。
- C++26 等前沿内容统一标记 needs standard-status verification，核验后再展开。
- 参考资料分「必须参考」与「推荐参考」两级，写作时必须参考前者。

**参考资料**

- C++ Templates: The Complete Guide 第 2 版（tmplbook.com），模板编程主线参考。Modern C++ Design（Alexandrescu），Policy-Based Design 与设计模式轨道核心参考。Template Metaprogramming with C++（Pikus，2022，覆盖 C++20）。
- 提案文件：P0734R0（Concepts 语义）、P0847R7（Deducing This）、P2996R13（Reflection）、P2465R3（import std）、P2214R2 与 P2760R1（Ranges 计划）、P2387R3（range_adaptor_closure）、P3329R0（filter_view 修复）。
- CppCon 演讲：Josuttis 的 Back to Basics: Concepts 与 Taming the Filter View（原文标注 filter_view 陷阱必看）、Brindle 的 C++20 Ranges in Practice、O'Dwyer 的 Template Normal Programming 与 A Bit of SFINAE。
- cppreference 对应条目（templates、constraints、concepts 头文件、modules、ranges、编译器支持矩阵等）。
- 博客：Arthur O'Dwyer（TMP、type erasure、CTAD）、Andrzej Krzemieński（Concepts 实战）、Barry Revzin（Reflection、Ranges）、Rainer Grimm、Sandor Dargo。Hannes Hauswedell 的 Writing Views from Scratch 系列，原文称为自定义 view 教程的黄金标准。
- Modules 生态：Mathieu Ropert 的 Modules in 2026（生态现状评估）、Chuanqi Xu 的 GCC modules 实战指南（2025）、arewemodulesyet.org（支持状态追踪）。

### 014-vol5-concurrency.md（卷五：并发编程，原文 244 行）

（注：本文件写于卷五按「七关加 cookbook」重写之前，M1 到 M7 针对的旧十章结构与 exercises 已在 v0.14.0 下线。下面保留的是其中仍然成立的教学决策与未做的选题，章节编号按旧结构，重新启用时对应到新结构的相应关。）

**决策**

- [M1 内] 线程池为教学简化版（基础任务队列加工人线程），不追求 work-stealing。
- [M2 内] 异步 I/O 与协程的网络示例以 Linux epoll 为主，不追求跨平台。
- [M6] 分布式桥接一章定性为概念导览，不补代码，不引入外部依赖。理由：分布式实战留待卷八扩展。
- [I5 内] 引入 NVIDIA/stdexec 作为 sender/receiver 教学的代码依赖。理由：header-only，零构建侵入。文章需明确标注基于参考实现而非标准库。

**未完成的方向**

- [I1] Hazard Pointer 完整实现，独立新文章。理由：现有内容停在概念与伪代码之间，P2530 已进 C++26 草案，Folly 参考实现证明生产可用。路径：概念回顾到完整实现（acquire、retire、release）到应用于 lock-free stack 再到与引用计数的性能对比。
- [I1b] Epoch-Based Reclamation，独立新文章。理由：EBR 与 HP 并列为无锁内存回收两大方案，HP 适合写少读多，EBR 适合批量回收，现有内容已点名未实现。
- [I2] 结构化并发设计原则，独立新文章，纯设计原则不依赖库。理由：CppCon 2024 与 2025 连续两届核心主题，现有内容教了线程、锁、atomic、future，缺「怎么组织一整棵并发任务树」的设计层。路径：裸线程与裸 future 的不足到四约束（local、nested、bounded、deterministic）到 jthread 加 stop_token 实现最简结构化并发，再引出 sender/receiver 的动机。
- [I3] Sanitizer 组合策略，扩展调试文章或新增一篇。理由：现有只覆盖 TSan 加 Helgrind，缺 TSan 加 ASan 组合、Clang Thread Safety Analysis 注解、UBSan 对并发 UB 的捕获。
- [I4] 高级并发性能测试，扩展 benchmark 文章。理由：现有 GBench 加 perf stat 方法论扎实，缺百分位延迟（p50、p99、p999）、微基准与宏基准区分、置信区间、跨平台可重复性，原文称之为从学生水平到工程水平的关键差距。
- [I5] Sender/Receiver 实战入门，独立新文章。理由：P2300 已并入 C++26 工作草案，已有生产使用案例。路径：三核心概念（scheduler、sender、receiver）到 stdexec 组合管道到与协程互操作到「从 future 到 sender」迁移思路。风险为 API 可能在最终标准中微调，P2300 最终 API 是否与 stdexec 当前版本一致需核验。
- [I6] async_scope 与结构化并发实战，依赖 I5 完成后推进。理由：P3149 是 sender/receiver 框架中 spawn 加 join 非顺序结构化并发的组件，仍在活跃修订。
- [I7] Coroutine Task Type（P3552），远期候选。C++20 给了协程语言特性但无标准 task 类型，未来需覆盖手写 Task<T> 与标准的对应关系。当前只在文末加一条前瞻注释，不投入资源。
- [I8] RCU 概念导览，远期扩展。P2545 未确认进入 C++26 CD，与 HP、EBR 并列的第三种内存回收方案，暂不投入。
- [I9] Atomic Reduction Operations（P3111），远期扩展。一类不返回旧值的原子读改写操作，用于并行归约，提案活跃，暂不投入。
- [I10] 并发模式选择参考卡，横跨各章的「何时用什么」决策树，等其余增量全部完成后作为毕业总结。
- 原文判断 I2 结构化并发是最高价值增量，为 I5、I6 铺垫动机。

**已放弃**

- [I11] memory_order_consume 增量内容（原因：现有内容已正确标注 consume 为建议不使用、C++26 正式弃用，无需增量。未来 consume 被移除或出现替代方案再评估）。

**原则与约定**

- 增量资产分三层推进：第一层现在可教、有真实工具支撑。第二层值得教但依赖标准演进，用参考实现教学并标注非标准库。远期候选只做路线卡，等标准或生态成熟后再评估。
- 概念导览类章节不配可运行代码，不引入外部依赖。
- 依赖未定稿标准的内容标记 needs standard-status verification。

**参考资料**

- 无

## 卷六与卷七

### 015-vol6-performance.md（卷六：性能优化，原文 474 行）

（注：卷六现有八章 38 篇，是按测量方法论重排的，章节名与本文件的 13 篇大纲对不上。重排后独立成章的是性能思维、基准测试方法论、CPU 微架构、归因方法论、按瓶颈调优、多核性能、C++ 抽象成本、编译器与代码大小。本大纲里另有六块内容在现有目录中没有独立成章：编译器优化深度剖析、汇编阅读与 Compiler Explorer、perf 与火焰图、Valgrind 内存分析、SIMD 基础与自动向量化、AVX 与 AVX2 实战。这六块连同设计原则、跨卷分工与参考资料一并保留，其余逐篇大纲不收录。）

**决策**

- 写作顺序按依赖排：先测量基础（思维、方法论、工具），再编译器理解，再 CPU 与内存优化，再 profiling，最后 SIMD 与综合评估。理由：后续文章都依赖 benchmark 能力，SIMD 需要前面全部知识铺垫。
- 编译器选项在卷六只做自包含的速查，完整指南放卷七，用延伸阅读链接过去。
- 英文翻译等中文内容稳定后统一更新。

**未完成的方向**

- [03] 编译器优化深度剖析：inline 关键词的真实语义，去虚化，constexpr 与 consteval 优化，LTO 原理与实战，PGO 流程与实测对比。
- [04] 汇编阅读与 Compiler Explorer：x86_64 汇编最小知识集，AT&T 与 Intel 语法，常见 C++ 结构对应的汇编模式，怎样判断优化有没有生效。用运行卡嵌入 host 与 ARM 双平台示例。
- [05] Linux perf 与火焰图：perf stat、perf record、perf report、perf top，火焰图的生成与读法，Off-CPU 分析简述。平台限定 Linux。
- [06] Valgrind 工具套件与内存分析：Callgrind 与 Cachegrind 为主，Memcheck、DHAT、Massif 简要提及，perf 与 Valgrind 的选用对比。Valgrind 只支持 Linux x86，开销 10 到 50 倍，不支持 ARM。
- [10] SIMD 基础与自动向量化：x86 SIMD 演进，ARM NEON 与 SVE 简要提及，自动向量化的条件与阻碍因素，向量化报告，C++26 std::simd（P1928）展望。
- [11] AVX 与 AVX2 intrinsics 实战：浮点与整数 intrinsics，对齐要求，AVX 与 SSE 转换惩罚，AVX-512 降频。ARM NEON 延伸到卷八。
- 下列五块在现有目录里是否已有对应文章，重启卷六规划时逐块核一次：内存对齐与结构体布局（alignof、alignas、成员排序、[[no_unique_address]]），分支预测与 branchless 编程（[[likely]] 与 [[unlikely]]、查表、cmov），Cache 友好的数据布局（AoS 与 SoA、热冷数据分离），性能与代码大小综合评估（代码膨胀的来源与量化、extern template、-Os），卷六代码示例索引一篇。
- Compiler Explorer 的 14 个 host 与 ARM 双平台示例要全部被索引到卷六文章。
- 卷六代码示例索引与使用指南一篇：索引表列出文件路径、对应文章、Compiler Explorer 链接，另讲本地构建方法、运行卡嵌入方法、ARM 版本使用说明。

**已放弃**

- 旧的 8 章、18 到 22 篇大纲不再沿用，由 13 篇大纲替代。
- 嵌入式 DSP 基础不进卷六，归卷八。

**原则与约定**

- 自包含优先：每篇文章是完整阅读单元，读者不需要跳到其他卷就能理解，跨卷链接只出现在「延伸阅读」，不作前置必读。
- 读者路径固定为：为什么优化，如何测量，编译器做了什么，如何找到瓶颈，硬件感知优化，向量化，综合评估。
- 可复现性能工程：每篇文章必须回答「怎么测」和「不能得出什么结论」，不做玄学优化清单。
- 打碎重建：现有文章保留核心实测数据，全部规范化改写并重新编号。
- 涉及 C++26 的内容（std::simd、饱和算术）编译器支持尚早，写作时标记 needs standard-status verification。

**参考资料**

- 通用：Agner Fog《Optimizing Software in C++》（agner.org/optimize/optimizing_cpp.pdf）。Fedor Pikus《The Art of Writing Efficient Programs》（Packt）。Kurt Guntheroth《Optimized C++》（O'Reilly）。AwesomePerfCpp（github.com/fenbf/AwesomePerfCpp）。Johnny's Software Lab（johnnysswlab.com）。
- 测量：Abseil Performance Tip #75 How to Microbenchmark（abseil.io/fast/75），Google Benchmark 用户指南，nanobench 框架对比（nanobench.ankerl.com/comparison.html）。
- 编译器：Johnny's Software Lab 的 LTO 文章，LLVM LTO Design（llvm.org/docs/LinkTimeOptimization.html），GCC LTO 博客系列（hubicka.blogspot.com）。
- 汇编：The Faker's Guide to Reading x86 Assembly（timdbg.com），Matt Godbolt 在 CppCon 2025 的 Features You Never Knew，How Compiler Explorer Works in 2025（xania.org）。
- 性能分析：Brendan Gregg 的 Linux perf Examples、CPU Flame Graphs 与 FlameGraph，Valgrind Callgrind Manual，Julia Evans 的 Profiling with perf（jvns.ca）。
- Cache 与布局：CppCon 2025 Jonathan Müller 的 Cache-Friendly C++，Eric S. Raymond 的 The Lost Art of Structure Packing（catb.org/esr/structure-packing/），Algorithmica 的 CPU cache 章节（en.algorithmica.org/hpc/cpu-cache/）。
- 分支预测：Johnny's Software Lab 的 How Branches Influence Performance，John Farrier 的 Branch Prediction Definitive Guide，P0479R0（likely 与 unlikely 提案）。
- SIMD：Intel Auto-Vectorization Guide，LLVM Auto-Vectorization（llvm.org/docs/Vectorizers.html），Intel Intrinsics Guide，P1928 与 VcDevel/std-simd 参考实现。
- 代码大小：Sandor Dargo 的 Binary Sizes and Templates，C++ on Sea 2024 的 How to Keep C++ Binaries Small，ThePhD 的 sol3 二进制大小文章，Where C++ Code Bloat Occurs（ykiko.me）。

### 016-vol7-engineering.md（卷七：工程实践，原文 251 行）

（注：卷七仍在建，现有 12 篇正文，ch00 的 CMake 基础四篇是按本大纲的 ch01 写的。本摘录保留全部决策与章节规划，章节编号沿用原文。）

**决策**

- 卷七以主体重建为主，旧素材选择性回收，不做增量维护。
- CMake 单独深挖成一章 9 篇。理由：CMake 是事实标准。CMake 最低版本推荐 3.28 以上，支持完整的 Preset 与 C++20 Modules。
- 测试主推 Catch2 v3，GoogleTest 只作对比性覆盖。理由：仓库卷九已在用 Catch2。
- 包管理做平铺对比，FetchContent、CPM、vcpkg、Conan 并列，严肃处理。
- 新增调试专题三篇（GDB 与 LLDB、高级调试、性能分析）。
- 嵌入式边界取方案 C：通用工程为主线，嵌入式案例只保留在交叉编译和链接器两篇。
- 重叠主题的分工。Modules：卷四讲语义，卷七讲 CMake 工具配置。编译器优化：卷六讲编译器怎么优化，卷七讲怎么配置编译器选项。链接器：compilation 卷讲库与符号机制，卷七讲嵌入式链接脚本实践。并发测试：卷五是并发特化，卷七是通用测试方法论。嵌入式工程准备：卷七讲通用交叉编译，卷八讲 STM32 板级细节。
- 旧文处置：三篇回收进 CMake 章（交叉编译与 CMake、编译器选项、链接器与链接脚本）。两篇 file-copier 文章迁到 projects。WSL 开发一篇降级为附录或工具链笔记。VS2026 Modules 一篇合并进 CMake 章的 Modules 段落或交叉链接到卷四。MSVC 调试原理一篇作为素材回收到调试章。

**未完成的方向**

- [ch01 构建系统与 CMake 深入，9 篇] Modern CMake 项目结构。传递依赖与生成器表达式。CMake Preset 工程化。依赖管理，FetchContent 与依赖提供者。库的安装与分发。构建性能优化（Unity builds、预编译头、ccache、Ninja）。交叉编译与工具链文件。编译器选项与诊断。链接器实践。
- [ch02 测试，Catch2 体系，5 篇] 测试框架选型。Catch2 实战入门。高级测试技巧。Mock 与测试隔离。Coverage 与嵌入式测试。
- [ch03 代码质量，3 篇] 静态分析（clang-tidy、cppcheck）。Sanitizer（ASan、UBSan、TSan）。代码格式化与自动化（clang-format、pre-commit）。
- [ch04 调试，3 篇] GDB 与 LLDB。高级调试技术（OpenOCD 远程调试、core dump、rr）。性能分析入门。
- [ch05 包管理，5 篇] 包管理全景。FetchContent 与 CPM.cmake。vcpkg 实战。Conan 2.x 实战。包管理选型决策。
- [ch06 CI/CD 与发布，4 篇] CI/CD 基础与 GitHub Actions。以本仓库自己的 workflow 与脚本为案例的 CI/CD。版本管理与发布流程。文档站点质量环。
- [写作顺序] 分四批。第一批 ch01 的项目结构、生成器表达式、Preset、依赖管理、编译器选项，理由是 CMake 章是核心基础。第二批测试章的选型与入门、Sanitizer、Mock 与隔离、Coverage 与嵌入式测试，其中 Sanitizer 与编译器选项自然衔接。第三批调试章的 GDB 与性能分析、CMake 章的安装分发、包管理全章。第四批 CMake 章的构建性能、交叉编译、链接器，CI/CD 全章。
- 每篇配 CMake 示例工程，29 篇都要有配套 code 示例。

**已放弃**

- 无。

**原则与约定**

- 卷七教的是一个 C++ 工程师从「能写代码」到「能交付项目」的实践能力，覆盖构建、测试、质量保障、调试、包管理、CI/CD。
- 各章定位。CMake 章从「会用 CMake」到「精通 Modern CMake」。测试章是从零到一的测试能力建设。质量章在代码到达用户之前拦截问题。调试章在代码行为不符预期时系统性地找到原因。包管理章是严肃的平铺对比。CI/CD 章以真实项目为案例。
- 卷间依赖：compilation 卷是链接器一篇的前置，在该篇顶部加「前置阅读」链接。卷四的 Modules 语言特性与卷七的工具配置双向链接。卷八强依赖卷七的交叉编译。性能分析入门深入时交叉链接到卷六。卷五的 TSan 与 benchmark 引用卷七的质量章与调试章。卷九使用 Catch2、CPM、CMake，由卷七提供教程化内容。
- 嵌入式工程准备和卷八互链，不重复维护。
- 写作时要核验标准状态的内容：CPS 的版本和 CMake 支持状态，C++20 Modules 在 CMake 4.x 的成熟度，vcpkg 和 Conan 的当前版本与 API，Catch2 v3 的最新版本与集成方式，各 sanitizer 在 GCC 与 Clang 最新版的支持情况（原文注明 GCC 不支持 MSan）。

**参考资料**

- CMake 4.3.3 官方文档（cmake.org）。Craig Scott《Professional CMake: A Practical Guide》第 22 版。
- CppCon 2025：Bret Brown 的 CMake Doesn't Have to Be Painful。Bill Hoffman 的 The Evolution of CMake: 25 Years。
- C++Now 2025：Daniel Pfeifer 的 Effective CTest。Bill Hoffman 的 CPS in CMake。
- Catch2 v3.10.0、GoogleTest v1.17.0、doctest v2.5.2 的 GitHub 与官方文档。
- vcpkg 2026.06.01 registry。Conan 2.29.0 changelog。CPM.cmake v0.40.3 的 GitHub。
- CPS v0.14.1 spec（cps-org.github.io）。WG21 P2656R2（C++ Ecosystem IS）。

## 卷八

### 017-vol8-domains.md（卷八：领域应用，原文 578 行）

（注：卷八是 2026 年 11 月的主战场，现状与本文有出入。F103 教程线已走完环境搭建、LED、按键、UART、时间、模式六站，I2C 站与 F407 部分仍为占位。ZerOS 手搓 RTOS 十站已上线，取代了本文 RTOS 路线里 FreeRTOS 为主力的安排。系统编程子域八章已完工。本文的领域表里 design-patterns、cpp-semantics、serialization、embedded-gui 四个目录名在仓库中不存在，实际目录见各条注记。下面按「落过地的只留决策，没动工的连大纲一起留」摘录。）

**决策**

- [D1] 旧独立文章的归属：资源与实时约束、零开销抽象、对象池、CRTP 与运行时多态、placement new、ETL、中断安全编码七篇保留为独立参考并统一编号。动态分配问题、静态与栈分配、固定池分配三篇合并重写为两篇。空基类优化移到卷四。array 对比裸数组一篇删除（卷三已完整覆盖）。
- [D2] 外设链顺序：SPI，I2C，Timer 与 PWM，ADC，DMA，Flash 与看门狗，电源与 RTC。每个外设教程遵循统一模板：硬件动机，C 的 HAL，C++ 封装，易错点，练习。工程代码与文章同步推进。
- [D3] RTOS 路线三步走：先协作式调度器，再抢占式调度器，然后才到现成 RTOS。自写调度器不可跳过，理由是教学价值不可替代（ARM Cortex-M 异常机制、栈帧布局、PendSV 上下文切换）。
- [D4] 领域体系：卷八并行推进九个子域，各领域独立成章，各有自己的节奏。设计模式与卷四的分工是卷四做泛型设计模式（模板视角），卷八做通用现代 C++ 设计模式（工程实战视角，含嵌入式用例）。语义学分析沿用指针语义系列的结构：令人惊讶的行为，机制分析，标准规则，实际影响，设计原则。工程数据结构与算法的定位是「怎么从零设计和实现」，与卷三（怎么用）、卷五（怎么并发安全）区分。
- [D5] 大型项目外置到贯穿式项目规划：手搓 MiniGUI 框架，手写 LRU Cache、线程安全 HashMap、迷你搜索引擎，syskit 工具库，跨平台异步 I/O 事件循环库，嵌入式综合项目。
- [D6] 系统编程的写法：一篇等于一个能动手实验的核心问题，每个机制配可复现实验与真实输出，单篇 200 到 500 行。共享机制在 Linux 深讲一次，Windows 写镜像篇对照，机制分叉的两侧都全量讲，Windows 独有能力独立成篇。公共工具（unique_fd、unique_handle、mapped_region、sys_call、errno_code）只在思维基石篇定义一次，其余篇引用。错误工具必须定义即用。
- [D6] ch00 原「为什么需要系统编程」不再单篇成文，内容已被总纲吸收，两篇概念篇的材料从文件 I/O 首篇平移而来。
- [D6] 实验编号口径：Linux 用大写 E 系，Windows 用小写 e 系，跨篇出现相同编号时靠一句「互不相干」的桥接句说明。系统编程子卷里「契约」一词是作者确认的禁用词，全子卷改称「公共工具」，章节名统一为「思维基石」。
- [D3 的修正] RTOS 以 FreeRTOS 为主力这条，在 ZerOS 手搓 RTOS 十站上线后已被取代，FreeRTOS 在路线里的位置待重新安排。D3 里「自写调度器不可跳过」的判断被加强。

**未完成的方向**

- [嵌入式 P1] 外设链里尚未动工的部分：SPI（协议原理、传感器与 Flash 读写、C++ driver wrapper），I2C（主机通信、错误恢复、传感器读取），Timer 与 PWM（基本定时、中断定时、PWM 输出、输入捕获），ADC（单次、连续、扫描模式，DMA 连接，精度与噪声），DMA（memory 与 peripheral 传输，circular 模式，double buffer），Flash 与看门狗（内部 Flash 编程、参数保存、IWDG 与 WWDG、故障恢复），电源与 RTC（低功耗模式、唤醒源、RTC 日历）。每站三到十篇加一个代码工程。
- [嵌入式 P3] 综合项目三个候选：传感器数据采集系统（SPI 或 I2C 传感器加 DMA 加 UART 上报加 RTOS 任务调度），嵌入式参数管理系统（Flash 存储加看门狗加 RTC 时间戳加 UART 命令行），电机控制系统（Timer PWM 加 ADC 电流采样加 PID 加实时任务）。每个项目三到五篇加一个完整工程。
- [嵌入式 P4] ESP32 第二平台，远期：工具链搭建，FreeRTOS SMP 双核调度，WiFi 与 BLE 基础。前置条件是外设链基本完成并且有硬件。
- [嵌入式 P5] 前沿候选，远期：C++20 协程替代 RTOS（bare-metal，参考 five-embeddev 与 vagran/pulse），Zephyr 架构对比，RT-Thread 简介。嵌入式 C++ 工程化的交叉主题分给别的卷：freestanding 约束归卷七，启动文件与链接脚本归编译链接卷，嵌入式 TDD 归卷七，嵌入式 STL 与 allocator 归卷三。
- [系统编程] Windows 独有篇候补两篇：安全描述符与 ACL，注册表。英文版 40 篇的翻译节奏待定。
- [RTOS 路线，原 Phase 2] 协作式调度器（SysTick 驱动、任务数组、轮转、延迟队列、C++17 模板封装），抢占式调度器（PendSV 上下文切换、TCB、栈初始化、优先级调度），FreeRTOS 的 C API（task、queue、semaphore、mutex、event group），FreeRTOS 加现代 C++（RAII wrapper 参考 freertos-mcpp，expected 错误处理，静态分配）。前两步 ZerOS 已覆盖，后两步待重新安排。
- [设计模式，18 篇大纲，未动工] 第 1 章现代 C++ 如何改变设计模式（范式转移，值语义与引用语义，编译期多态与运行时多态的工程选择，组合优于继承）。第 2 章经典模式现代化（工厂，策略，观察者，命令，访问者）。第 3 章创建与生命周期模式（单例的现代替代，对象池，原型与 clone）。第 4 章结构型模式（适配器与外观，代理）。第 5 章嵌入式特定模式（状态机，环形缓冲区与生产者消费者）。第 6 章综合实战（传感器驱动框架，命令驱动的设备控制系统）。
- [语义学分析，四个系列未动工，已完成指针语义系列] 系列 2 值类别与对象生命周期（优先级最高）：值类别全景，临时物化，引用绑定与生命周期扩展，生命周期扩展失效的情形，NRVO 与 RVO 的语义对比，std::move 与 std::forward 的语义本质，设计原则。与卷二的边界是卷二教怎么用，本系列分析值类别系统背后的语义。系列 3 对象模型与类型语义：对象的标准定义，Trivial、Standard Layout、POD 的精确语义，隐式类型转换的完整规则，初始化语义全景。系列 4 名字查找与重载决议：名字查找的全过程，ADL 的精确语义，两阶段查找，重载决议排序。与卷四的边界是卷四从模板用户角度讲，本系列从语言语义角度讲全部查找机制。系列 5 未定义行为深度解析：UB 到底意味着什么，UB 的主要类别，编译器如何利用 UB 优化，严格别名规则，在 UB 面前存活。
- [序列化与持久化，15 篇加五个实战，未动工，目录现名 data-storage] 第 1 章序列化格式基础（序列化全景，JSON 与 nlohmann/json，Protocol Buffers，MessagePack 与 CBOR，实战是配置文件解析器）。第 2 章文本处理与格式化（std::format 与 std::print，string_view 零拷贝解析，CTRE 编译期正则，实战是 CSV、INI 与 TOML 解析器，实战是嵌入式传感器数据日志格式）。第 3 章数据持久化（SQLite 的现代 C++ 封装，嵌入式 Flash 参数持久化与磨损均衡，std::expected 的 I/O 错误处理，实战是带持久化的设备配置管理系统）。第 4 章二进制协议设计（字节序、对齐与填充，嵌入式 Protobuf 与 Nanopb，零拷贝序列化 FlatBuffers 与 Cap'n Proto，协议演进，实战是 STM32F1 传感器数据采集通信协议）。
- [网络编程，14 篇加三个实战，部分已写完] 阶段 A 同步 Socket 基础五篇，实战是多线程聊天室。阶段 B I/O 多路复用四篇（非阻塞 I/O 与 poll，epoll 与 kqueue，Reactor 模式与事件循环，多路复用对比与基准测试），实战是基于 Reactor 与 epoll 的 HTTP 静态文件服务器。阶段 C 异步模式五篇（Boost.Asio 入门，Asio 回调链，Asio 与 C++20 协程，实战协议设计，io_uring 与 std::execution 展望），实战是分布式键值存储。仓库的 networking 子域现有传统 socket、现代 socket 封装、epoll 多路复用、Reactor 模式四篇加 mini-reactor 实验，覆盖到阶段 B 的后三篇。阶段 A 的 UDP、Socket 选项与跨平台三篇，阶段 B 的多路复用对比与基准测试，以及整个阶段 C，待核对是否已有对应内容。
- [GUI 与图形，桌面，10 篇，未动工，仍为占位页] 阶段 A XCB 渲染基础四篇（XCB 连接与 Framebuffer 抽象，2D 图元光栅化，位图渲染与 Alpha 混合，字体渲染），里程碑是 MiniDraw 渲染库。阶段 B GUI 核心概念六篇，协程贯穿（信号槽与回调安全，事件捕获与冒泡，布局引擎设计，协程驱动的异步 UI，模型视图与数据驱动加 undo 与 redo，主题与风格系统）。后端选 XCB，不选 SDL2。
- [嵌入式 GUI，6 篇，未动工，无目录] 阶段 A 嵌入式显示基础三篇（OLED 显示驱动 SSD1306 与 SH1106，TFT LCD 驱动，触摸输入处理）。阶段 B 嵌入式 GUI 框架三篇（轻量 Widget 系统，嵌入式布局与事件分发，实战是传感器仪表盘）。
- [工程数据结构与算法，未动工，仍为占位页] 第 1 章工程数据结构实现六篇：小缓冲优化，侵入式容器，环形缓冲区变体，布隆过滤器，跳表，LSM-Tree。第 2 章 LeetCode 刷题解析持续扩展不限篇数，初始八篇（数组与双指针，链表与智能指针安全，栈队列与单调结构，二叉树与 DFS 与 BFS，图算法，动态规划，贪心与回溯，字符串算法），后续可扩展位运算、数学、并查集、线段树、树状数组、字典树。仓库的 data-structure 子域现有 mini STL 手搓系列五篇，是这条线的另一种走法。
- 待讨论：GUI 的具体定位（渲染基础还是 GUI 工程实践）。

**已放弃**

- 跨语言互操作（interop）：不再规划，需要时再启动。
- 以 FreeRTOS 为主力的 RTOS 路线（原因：ZerOS 手搓 RTOS 上线后取而代之，D3 里「自写调度器不可跳过」的判断被加强，FreeRTOS 的位置待重新安排）。

**原则与约定**

- 系统编程求全不求简，分批推进。
- epoll 机制与事件循环已在卷五与网络子域深讲，系统编程子域只做全景衔接，不重开课。
- 大型综合项目外置到贯穿式项目规划，不在领域路线里重复。

**参考资料**

- freertos-mcpp：RTOS 路线里 FreeRTOS 加现代 C++ 一步的 RAII wrapper 参考。
- five-embeddev 与 vagran/pulse：bare-metal 上用 C++20 协程替代 RTOS 的参考。

## 卷九

### 018-vol9-open-source-study.md（卷九：开源项目学习，原文 171 行）

**决策**

- 卷九定位为前置八卷的总结与实战，不限于 Chrome Base，任何有教育价值的开源项目均可纳入。
- 每个项目遵循 full（完整教学路径）加 hands_on（设计导向快速路径）双路径模式，两条路径互不引用，各自独立成线。

**未完成的方向**

- I1 源码阅读方法论导论：卷九入口基建，工作量一篇，合并原「可迁移经验」与「不要照搬」两个索引页。
- I3 TaskRunner 与 sequence 主线：价值最高但规模风险大，依赖卷五并发主线成熟度。
- I8 工业级并发专题：Asio、Folly、moodycamel 等，卷五成熟后启动，放独立目录，遵循双路径。
- 新项目候选池（第一梯队）：spdlog（策略模式、异步 producer-consumer，可简化为约 200 行）、fmtlib（变参模板、编译期格式解析，可简化为约 300 行）、ETL 精选组件（无堆分配、侵入式容器）、moodycamel ConcurrentQueue（atomic 实战，从 SPSC 起步渐进到 MPMC）。
- 第二、三梯队：ankerl::unordered_dense、nlohmann/json、ctre、concurrencpp、folly 选择性提取组件等，各自附判定理由（原文有完整表）。
- 补充 OnceCallback 01-6 性能对比的 benchmark 实测。
- I4 logging 与 CHECK/DCHECK、I5 span 与 buffer safety 按 cross-link only 处理：核心知识属卷七与卷三，在对应卷的文章里提及 Chrome 做法。

**已放弃**

- I9 可复用组件库十个 header-only 组件（原因：个别组件在对应项目主线里自然产出，不做独立库）。
- I7 可迁移经验与不要照搬索引页（原因：合并进 I1 导论）。
- ranges-v3 纳入（原因：C++20 标准库已含 ranges，历史价值高于教学价值）。llhttp 纳入（原因：纯 C 库）。abseil 整体研读（原因：Bazel 构建与 Google 风格教学摩擦大）。

**原则与约定**

- 每个开源研读主题必须产出五样：最小背景、源码设计动机、教学版简化实现、可迁移经验、不应照搬的边界。
- 不追求 Chrome Base 全覆盖。

**参考资料**

- 无

（注：I2 WeakPtr 主线已写成，卷九现有 WeakPtr 全系列，本摘录不再列入。）

## 贯穿式实战项目

### 020-projects.md（贯穿式实战项目，原文 142 行）

**决策**

- 项目建立在成熟内容上，不抢在前置卷关键资产之前。
- 项目卡必须比文章更严格：能构建、能测试、能验收。已有实质实现的资产通过交叉链接接入项目页，不做内容重复。
- 所有项目代码统一放 `code/projects/`，不在卷内目录重复存放。远期项目不单列 TODO 文件。INI Parser 只保留外部仓库引用，不作为本仓库内项目。

**未完成的方向**

- 项目卡模板与 `code/projects/` 目录尚未建立。
- General CLI Framework（通用命令行框架）：命令解析器、命令注册接口、帮助系统、参数处理，可在 STM32F1 上经 UART 跑起。架构参考旧稿的 UartRx 到 LineBuffer 到 CommandParser 到 CommandDispatcher 链。依赖卷八嵌入式前三站，持久化等 Flash 教程就绪后补。
- Mini STL Components：small_vector、fixed_vector、span、ring_buffer、allocator，不等卷三重建直接启动。
- 候选池：Mini HTTP Server（等卷五与卷八网络成熟）、Mini GUI（远期）、Embedded OS（等 RTOS 路线）、IoT 网关（远期）、syskit 系统编程工具库（卷八系统的文件描述符与错误模型等工具的工程化整合，启动前先做与 networking 子域的命名与错误模型核对）、跨平台异步事件循环库（epoll、IOCP、io_uring 差异之上的最小事件循环，建议在 syskit 之后启动以复用其工具）。
- 项目页维护：从项目名列表升级为每项目一张卡片（目标、前置知识、状态、代码位置），已有资产加交叉链接，英文镜像同步。

**已放弃**

- IoT 网关独立 TODO（原因：降级为远期候选，合并进候选池）。

**原则与约定**

- 无（项目纪律已并入决策）。

**参考资料**

- 外部仓库 Tutorial_cpp_SimpleIniParser（INI 解析器）作为外部引用。

（注：卷五旧版 exercises 与 capstone 在 v0.14.0 重写时下线，Mini Concurrent Runtime 与 Echo Server 两张项目卡的原型随之失效。Echo Server 的现版本在卷五第七关。Mini STL 的实战已由卷八 data-structure 系列承接一部分。）

## 卷十

### 019-vol10-lecture-notes.md（卷十：课程与演讲笔记，原文 92 行）

**决策**

- 选题优先从 CppCon 演讲中选。其他会议（CppNow、Meeting C++、NDC TechTown）暂不作为优先候选，待社区征集稿时再评估。

**未完成的方向**

- [M3] 文章里的代码路径引用渲染为行内代码，统一改成指向 GitHub 的链接。
- [M4] 延伸阅读交叉链接：concept 系列指向卷四的模板内容，assembly 系列指向卷六 AVX 与卷七编译器选项。原则是最小化，只在文章末尾添加，不假设读者没读过主教程。
- CppCon 候选池：Alexandrescu 的包与 integer_sequence（关联卷四模板元编程）、Ben Deane 的 API 设计与代码审查（卷七）、Timur Doumler 的 C++26 Contracts（卷四，需核验标准状态）、Andreas Weis 的协程第二部（卷四与卷五）、Alexandrescu 的反射（卷四，需核验）、Dietmar Kühl 的 sender/receiver HTTP 服务器（卷五）。

**已放弃**

- 无。

**原则与约定**

- 演讲笔记选题四准则：与现代 C++ 工程实践强相关，能回链到已有或规划中的卷，有可运行示例或可复现实验，不依赖大段摘录原内容（笔记是二次学习路径，不替代原始演讲）。

**参考资料**

- 无

## QA 与知识库

### 031-qa-knowledge-base.md（QA 与知识库，原文 57 行）

**决策**

- QA 收录规则与社区治理说明保存在 CONTRIBUTING.md，todo 文件只保留 QA 栏目的建设任务。

**未完成的方向**

- 新增 FAQ 栏目，五个方向：C++ 学习路线、编译与链接常见错误、指针与生命周期常见误区、并发常见误区、嵌入式 C++ 常见取舍。
- 新增 QA 索引，收录 StackOverflow、知乎、WG21 论文、cppreference、项目 Discussion 精华。

**已放弃**

- 无。

**原则与约定**

- 无。

**参考资料**

- 无
