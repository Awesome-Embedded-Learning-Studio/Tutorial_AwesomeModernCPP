---
id: "019"
title: "卷十：课程与演讲笔记 TODO"
category: content
priority: P3
status: active
created: 2026-06-10
updated: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# 卷十：课程与演讲笔记 TODO

## Current Assets

- `documents/vol10-open-lecture-notes/`：16 个 md（2 组 CppCon 2025 演讲笔记，11 篇深度文章 + 5 个 index）。
- CppCon 2025 两组内容：
  - **01-concept-based-generic-programming**（Bjarne Stroustrup）：4 篇，~3,372 行，29 个 cpp 示例。
  - **02-some-assembly-required**（Matt Godbolt）：7 篇，~4,140 行，~22 个 cpp/c 示例（含 ARM 交叉编译目标）。
- 英文翻译：15 个文件，完整覆盖。
- Vue 组件：`TalkInfoCard`（演讲信息卡片）、`ChapterNav`/`ChapterLink`（导航网格）、`ReferenceCard`/`ReferenceItem`/`RefLink`（参考文献），已注册为全局组件。
- README 已把卷十列入 10 卷体系，状态"编写中"。

## Maintenance TODO

> M1（Frontmatter 扩展字段）、M2（Vue 组件文档）已完成并归档。

### M3: 代码路径引用修复

- **优先级**：后续 PR
- **问题**：文章中 `` `code/volumn_codes/vol10/...` `` 格式的代码引用在 VitePress 中渲染为行内代码而非可点击链接。
- **交付物**：统一改为 `[代码文件](https://github.com/.../blob/main/code/...)` 格式。
- **影响范围**：所有 vol10 文章中的代码路径引用（约 5-6 处）。

### M4: 交叉链接（延伸阅读）

- **优先级**：后续 PR（配合 M3 一起做）
- **原则**：最小化，仅在文章末尾"延伸阅读"区域添加链接，不假设读者未读主教程。
- **需要添加的链接**：
  - concept 系列文章 → vol4 高级主题中的 concepts/模板内容（待 vol4 完善后建立）
  - assembly 系列文章 → vol6 AVX/AVX2 文章、vol7 编译器选项文章

## 增量 TODO

### 候选池：CppCon 演讲笔记

优先从 CppCon 演讲中选题。每组笔记应满足选题准则（见下），且能回链到本教程的现有卷。

#### CppCon 2025

| # | 演讲 | 讲者 | 关联卷 | 备注 |
|---|------|------|--------|------|
| C1 | How to Tame Packs, std::tuple, and the Wily std::integer_sequence | Andrei Alexandrescu | vol4 模板元编程 | 变参模板、integer_sequence 深度技巧 |
| C2 | API Structure and Technique: Learnings from C++ Code Review | Ben Deane | vol7 工程实践 | API 设计、代码审查经验 |
| C3 | C++ Contracts: How Contracts in C++26 Can Improve C++ Code Safety and Correctness | Timur Doumler | vol4 C++26 前沿 | C++26 Contracts 特性，SG21 主席主讲；**needs standard-status verification** |

#### CppCon 2024

| # | 演讲 | 讲者 | 关联卷 | 备注 |
|---|------|------|--------|------|
| C4 | Deciphering C++ Coroutines Part 2: Mastering Asynchronous Control Flow | Andreas Weis | vol4 协程 / vol5 并发 | 协程实战，Part 1（CppCon 2022）有 28K 播放 |
| C5 | C++ Reflection Is Not Contemplation | Andrei Alexandrescu | vol4 C++26 前沿 | 静态反射提案；**needs standard-status verification** |
| C6 | Creating a Sender/Receiver HTTP Server | Dietmar Kühl | vol5 并发 | std::execution、sender/receiver 模式实战 |

#### 其他会议

CppNow、Meeting C++、NDC TechTown 等会议也有高质量 C++ 内容（如 CppNow 2025 的"Post-Modern CMake"、Meeting C++ 2025 的"The C++ Execution Model"、NDC TechTown 2024 的"Naming is Hard"）。当前不作为优先候选，待社区征集稿时再评估引入。

#### 选题准则

每篇笔记必须满足：

1. 与现代 C++ 工程实践强相关。
2. 能回链到本教程已有或规划中的卷/章节。
3. 有可运行示例或可复现实验。
4. 不依赖大段摘录原内容——笔记是二次学习路径，不替代原始演讲。

### 发展方向

- **近期**：M3（代码路径修复）+ M4（延伸阅读链接），后续 PR 处理。
- **中期**：从 CppCon 候选池中选择 1-2 个与当前正在推进的卷高度相关的演讲做笔记。
- **远期**：其他会议的候选待社区征集稿时评估；课程笔记视社区需求和版权可行性再定。

## Acceptance

- [ ] 代码路径引用修复（M3）
- [ ] 交叉链接：延伸阅读区域（M4）
- [ ] 候选池覆盖 CppCon 2024/2025 核心演讲
- [ ] 已有两组 CppCon 2025 内容回链到相关卷（M4 完成后关闭）
