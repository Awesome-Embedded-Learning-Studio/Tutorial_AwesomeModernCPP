---
id: "011"
title: "卷二：现代 C++ 特性 TODO"
category: content
priority: P2
status: stable
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# 卷二：现代 C++ 特性 TODO

## Current Assets

- `documents/vol2-modern-features/`：12 章 44 篇正文 + 12 个章节 index + 1 个卷 index = 57 个 md。
- `documents/en/vol2-modern-features/`：英文翻译 100% 覆盖。
- `code/volumn_codes/vol2/`：12 个章节目录，168 个验证代码文件，10/12 章有 CMakeLists.txt。
- `code/examples/vol2/`：12 个轻量示例。
- 覆盖：移动语义、智能指针、constexpr、lambda、类型安全、结构化绑定、auto/decltype、attributes、string_view、filesystem、expected、UDL。
- Frontmatter 全部合规，标签均来自 VALID_TAGS。
- v0.1.0 标记为"已完成"，内容稳定。

## Worth Continuing

值得维护，不需要重建。卷二是现代 C++ 主干，应作为稳定资产持续校准。

## 卷间边界

- 卷二覆盖范围：C++11-17 核心语言特性 + 受控延伸的 C++20/23 增量。
- C++20 constexpr 扩展（ch02/02 中的 constexpr 析构函数、constexpr new/delete）：保留在卷二，作为 constexpr 概念的自然增量，文章内已明确标注版本。
- `std::expected`（C++23）：保留在卷二 ch10，是错误处理演进叙事的高潮，文章内已标注 C++23 并提供 C++17 fallback。
- `optional` monadic 操作（C++23）：保留在 ch04/04，已标注为前瞻。
- `consteval`/`constinit`（C++20）→ 迁移到卷四（见下方迁移计划）。
- Fold expressions 属于变参模板 → 卷四，卷二仅做前向说明。
- `function_ref`、`copyable_function` 等函数抽象 → 卷三或卷四，不纳入卷二。

## Gaps

### 内容完整性缺口

- ch00 移动语义全章 5 篇文章缺"参考资源"章节（全卷唯一空白章）。
- ch11 `01-udl-basics.md` 缺"小结"章节（全卷唯一）。

### C++ 标准版本准确性缺口

- `ch02/02-constexpr-ctor.md`：frontmatter 标注 `[11, 14, 17]`，但文章内容大量使用 C++20 特性（constexpr 析构函数、constexpr new/delete、`-std=c++20` 编译选项）。需修正为 `[11, 14, 17, 20]`，并在 C++20 内容开始处加分节标记。
- `ch04/03-variant.md`：有一处 C++20 visit 模式的文字提及，考虑标注为 `[17, 20]`。
- 4 处 C++26 前瞻提及需核验标准状态：
  - `ch06/03-ctad.md`：C++26 pair-like deduction for `std::map`
  - `ch02/01-constexpr-basics.md`：C++26 `constexpr std::sin`
  - `ch08/03-string-view-pitfalls.md`：C++26 `std::zstring_view`（P3655）
  - `ch04/04-optional.md`：C++23 monadic 操作

### 内容扩展缺口

- `ch00/03-rvo-nrvo.md` 需补充 Guaranteed Copy Elision（C++17）专门小节。当前 RVO/NRVO 文章完全没有提及 C++17 保证复制消除——这是 C++17 最重要的语义变更之一。
- ch03 使用 fold expression 的文章（`03-generic-lambda.md`、`05-functional-patterns.md`）需添加前向说明："折叠表达式将在卷四深入讲解"。

### 工程化缺口

- `ch05-structured-bindings`、`ch07-attributes` 缺 CMakeLists.txt（有 test_*.cpp 但无法 CMake 构建）。
- `code/examples/vol2/` 缺 CMakeLists.txt（12 个轻量示例没有构建入口）。

### 交叉链接缺口

- 当前卷二文章中没有具体的外卷链接。卷间交叉链接的形式由接手 TODO 者按上下文判断（在现有"参考资源"中追加，或新增独立小节）。
- 建议添加的交叉链接方向：
  - ch00 → vol9 OnceCallback（move-only callback）、vol4 完美转发深化
  - ch01 → vol8 pointer-semantics 深度系列
  - ch02 → vol4 编译期编程、vol6 性能路线
  - ch03 → vol9 callback 系列、vol4 Ranges
  - ch10 → vol8 嵌入式 UART error handling
  - ch09 → vol7 工程实践文件复制器项目

## Content Migration

### consteval/constinit → 迁移到卷四

- **源文件**：`documents/vol2-modern-features/ch02-constexpr/03-consteval-constinit.md`
- **目标**：卷四（C++20-26 高级特性），可归入"C++20 编译期保证"主题
- **关联代码**：`code/volumn_codes/vol2/ch02/consteval_constinit.cpp`、`test_consteval_pointer.cpp`、`test_constinit_threadlocal.cpp`、`test_if_consteval.cpp` 需同步迁移到 vol4
- **迁移后调整**：
  - ch02 index.md 移除 `03-consteval-constinit` 的 ChapterLink
  - ch02 index.md description 更新，去掉"consteval 和 constinit 则进一步提供了编译期的硬性保证"
  - ch02/02 文章末尾添加前向链接："如需编译期强制保证，请参阅卷四的 consteval/constinit 章节"
  - 英文翻译同步迁移
- **卷四 TODO 联动**：在 `todo/013-vol4-advanced-topics.md` 中记录接收此文章

## Exercise Supplementation

当前仅 3/44 篇有练习（6.8%）。以下按优先级分两梯队补充。

### 第一梯队（核心概念，强烈建议补充）

每篇补充 2-3 道练习题：

- `ch00/02-move-semantics.md`：手写移动构造/赋值的陷阱识别
- `ch00/04-perfect-forwarding.md`：转发场景分析
- `ch02/01-constexpr-basics.md`：编译期 vs 运行期判断
- `ch03/02-generic-lambda.md`：泛型 lambda + 概念约束实践
- `ch04/03-variant.md`：visit 模式 + 类型安全访问
- `ch10/03-expected-error.md`：错误传播链构建

### 第二梯队（巩固理解，建议补充）

- `ch01/02-unique-ptr.md`：所有权转移场景
- `ch01/03-shared-ptr.md`：循环引用排查
- `ch02/04-compile-time-practice.md`：constexpr 实战综合
- `ch03/01-lambda-basics.md`：捕获陷阱识别
- `ch04/04-optional.md`：optional 链式操作
- `ch10/04-error-patterns.md`：expected + optional 混合模式

## Development Points

- 卷二只承接 C++11-17 已稳定、工程常用特性。
- 受控延伸的 C++20/23 内容必须在文章中明确标注标准版本，不让读者误以为只需 C++17 编译器。
- 新标准库类型优先进入卷三参考路径。
- 高阶语言机制（consteval/constinit、fold expressions、concepts）优先进入卷四。
- 并发语义优先进入卷五。
- 卷一重写稳定后，需核验卷二各章 prerequisites 的准确性。

## Old TODO Merge

- 吸收 `022-core-theory-expansion.md` 中与现代特性维护有关的残余项。
- 不迁移独立旧 TODO。

## Acceptance

- [ ] ch00 移动语义全章 5 篇各补"参考资源"章节
- [ ] ch11 `01-udl-basics.md` 补"小结"章节
- [ ] `ch02/02-constexpr-ctor.md` 修正 cpp_standard 为 `[11, 14, 17, 20]`，C++20 内容处加分节标记
- [ ] `ch04/03-variant.md` 考虑标注为 `[17, 20]`
- [ ] 4 处 C++26 前瞻内容标记 `needs standard-status verification` 并核验
- [ ] `ch00/03-rvo-nrvo.md` 补 Guaranteed Copy Elision 专门小节
- [ ] ch03 fold expression 使用处添加"将在卷四深入讲解"前向说明
- [ ] consteval/constinit 文章及关联代码迁移到卷四（联动 `todo/013-vol4-advanced-topics.md`）
- [ ] ch02 index.md 移除 consteval/constinit 链接，更新 description
- [ ] 第一梯队 6 篇文章各补 2-3 道练习题
- [ ] 每个卷二大章至少 1-2 条后续阅读链接（指向外卷），具体形式由接手者按上下文判断
- [ ] ch05、ch07 补 CMakeLists.txt
- [ ] `code/examples/vol2/` 补 CMakeLists.txt
- [ ] 英文翻译同步更新（迁移、新增内容）
- [ ] 卷二保持 stable 状态
