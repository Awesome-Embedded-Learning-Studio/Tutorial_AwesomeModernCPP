---
id: "301"
title: "现有文章迁移与重写执行指南"
category: architecture
priority: P0
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["300"]
blocks: ["200", "201", "202", "203", "204", "205", "206", "207", "208", "209"]
estimated_effort: epic
---

# 现有文章迁移与重写执行指南

## 目标

本文件是整个内容重写工程的**执行入口**。它告诉你：
1. 现有 136 篇文章每一篇去了哪里
2. 每一步做什么、按什么顺序
3. 单篇文章的重写工作流是什么

---

## 一、执行阶段总览

```
Phase 0 ─── 目录重组 (TODO 300)          ← 第一步，必须先做
  │
Phase 1 ─── 卷二：现代 C++ 特性 (P0)     ← 核心差异化内容
  │
Phase 2 ─── 卷一：C++ 基础入门 (P1)      ← 入门基础
  │         卷三：标准库深入 (P1)          ← 可与卷一并行
  │         卷八·嵌入式：重写 (P1)        ← 现有内容最大块
  │
Phase 3 ─── 卷四：高级主题 (P1-P2)
  │         卷五：并发编程 (P2)
  │         卷六：性能优化 (P2)
  │         卷七：软件工程 (P2)
  │
Phase 4 ─── 卷八·其他领域 (P2-P3)        ← 全新内容
  │         贯穿式项目 (P2-P3)
  │         编译链接增强 (P2)
```

---

## 二、Phase 0：目录重组（TODO 300）

这是**最先执行**的步骤。按 [300-documents-restructure.md](300-documents-restructure.md) 执行：

1. 创建新目录结构
2. 创建 `.pages` 导航文件
3. 归档旧内容到 `archive/legacy-content/`
4. 迁移编译链接系列到 `compilation/`
5. 创建各卷 `index.md`（状态标记为"规划中"）
6. 更新 `mkdocs.yml`
7. `mkdocs serve` 验证

**验证通过后**，进入 Phase 1。

---

## 三、现有文章完整迁移映射

### 3.1 core-embedded-cpp/ (81 篇)

#### chapter-00-introduction (7 篇) → 多卷分散

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 00-preface.md | vol1/ch00/00-preface.md | **重写** | 去掉嵌入式限定，改为通用C++教程前言 |
| 01-resource-and-realtime-constraints.md | vol8/embedded/ch00/ | **重写** | 保留嵌入式上下文，通用化表述 |
| 02-c-language-crash-course.md | vol1/ch01/ | **重写** | 提取类型基础部分，去掉C语言速成定位 |
| 03-cpp98-basics.md | vol1/ch06/ | **重写** | 提取类基础部分 |
| 04-when-to-use-cpp.md | vol1/ch00/00-preface.md | **融入** | 融入前言 |
| 05-language-choice-performance-vs-maintainability.md | vol1/ch00/00-preface.md | **融入** | 融入前言 |
| 06-evaluating-performance-and-size.md | vol6/ch00/ | **重写** | 移至性能卷，通用化 |

#### chapter-01-design-constraints (3 篇) → vol7 + vol1

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-cross-compilation-and-cmake.md | vol7/ch00/ + vol8/embedded/ch00/ | **拆分** | CMake 部分→工程卷，交叉编译→嵌入式 |
| 02-compiler-options.md | vol7/ch00/ | **重写** | 通用化编译器选项讲解 |
| 03-linker-and-linker-scripts.md | compilation/ | **重写** | 融入编译链接系列 |

#### chapter-02-zero-overhead (4 篇) → vol2 + vol8/embedded

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-zero-overhead-abstraction.md | vol8/embedded/ch01/ | **重写** | 嵌入式零开销抽象 |
| 02-inline-and-compiler-optimization.md | vol6/ch04/ + vol6/ch05/ | **拆分** | 内联→编译器输出分析，优化→优化模式 |
| 03-constexpr.md | vol2/ch02/01-constexpr-basics.md | **重写** | 通用化 constexpr |
| 04-crtp-vs-runtime-polymorphism.md | vol4/ch09/cpp-idioms/02-crtp.md + vol8/embedded/ch01/ | **拆分** | CRTP 惯用法→卷四，嵌入式应用→卷八 |

#### chapter-03-types-containers (5 篇) → vol1 + vol2 + vol3

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-initializer-lists.md | vol3/ch00/ (容器深入) | **融入** | 融入容器初始化部分 |
| 02-move-semantics.md | vol2/ch00/02-move-semantics.md | **重写** | 移动语义通用化 |
| 03-rvo-nrvo.md | vol2/ch00/03-rvo-nrvo.md | **重写** | RVO 通用化 |
| 04-empty-base-optimization.md | vol4/ch09/cpp-idioms/02-crtp.md | **融入** | 作为 CRTP/EBO 惯用法的一部分 |
| 05-object-size-and-trivial-types.md | vol3/ch00/ (容器深入) | **融入** | 融入类型大小分析 |

#### chapter-04-compile-time (4 篇) → vol2

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-constexpr-and-design-techniques.md | vol2/ch02/01-constexpr-basics.md | **重写** | 通用化 |
| 02-consteval-constinit.md | vol2/ch02/03-consteval-constinit.md | **重写** | 通用化 |
| 03-compile-time-in-practice.md | vol2/ch02/04-compile-time-practice.md | **重写** | 通用化 |
| 04-if-constexpr.md | vol4/ch06/01-if-constexpr.md | **重写** | 移至高级卷编译期编程 |

#### chapter-05-memory-management (6 篇) → vol1 + vol8/embedded

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-dynamic-allocation-issues.md | vol8/embedded/ch02/ | **重写** | 嵌入式内存上下文保留 |
| 02-static-and-stack-allocation.md | vol1/ch12/ + vol8/embedded/ch02/ | **拆分** | 基础→卷一，嵌入式深入→卷八 |
| 03-object-pool-pattern.md | vol8/embedded/ch02/ | **重写** | 嵌入式对象池 |
| 04-placement-new.md | vol1/ch12/ | **重写** | 通用化内存管理 |
| 05-fixed-pool-allocation.md | vol8/embedded/ch02/ | **重写** | 嵌入式固定池 |
| 06-array-vs-raw-arrays.md | vol3/ch10/01-std-array.md | **融入** | 融入 std::array 深入 |

#### chapter-06-ownership-raii (8 篇) → vol2

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-raii-in-peripheral-management.md | vol2/ch01/01-raii-deep-dive.md | **重写** | RAII 通用化（去外设化） |
| 02-unique-ptr.md | vol2/ch01/02-unique-ptr.md | **重写** | 通用化 |
| 03-shared-ptr.md | vol2/ch01/03-shared-ptr.md | **重写** | 通用化 |
| 04-smart-ptr-embedded-tradeoffs.md | vol8/embedded/ch02/ | **重写** | 嵌入式智能指针权衡 |
| 05-intrusive-ptr-and-ref-counting.md | vol2/ch01/03-shared-ptr.md | **融入** | 融入 shared_ptr 深入 |
| 06-custom-deleter.md | vol2/ch01/05-custom-deleter.md | **重写** | 通用化 |
| 07-reference-counting.md | vol2/ch01/03-shared-ptr.md | **融入** | 融入引用计数原理 |
| 08-scope-guard.md | vol2/ch01/06-scope-guard.md | **重写** | 通用化 |

#### chapter-07-containers-spans (6 篇) → vol3

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-array.md | vol3/ch10/01-std-array.md | **重写** | 通用化 |
| 02-span.md | vol3/ch10/02-std-span.md | **重写** | 通用化 |
| 03-circular-buffer.md | vol8/embedded/ch02/ 或 vol3 容器深入 | **重写** | 通用化环形缓冲区 |
| 04-intrusive-containers.md | vol3/ch00/ (容器深入) | **重写** | 通用化侵入式容器 |
| 05-etl.md | vol8/embedded/ | **重写** | 嵌入式 STL 替代品 |
| 06-custom-allocators.md | vol3/ch09/02-custom-allocators.md | **重写** | 通用化分配器 |

#### chapter-08-type-safety (7 篇) → vol2 + vol3

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-enum-class.md | vol2/ch04/01-enum-class.md | **重写** | 通用化 |
| 02-type-safe-register-access.md | vol8/embedded/ch03/ | **重写** | 嵌入式寄存器访问 |
| 03-variant.md | vol2/ch04/03-variant.md | **重写** | 通用化 |
| 04-optional.md | vol2/ch04/04-optional.md | **重写** | 通用化 |
| 05-expected.md | vol2/ch10/03-expected-error.md + vol4/ch07/01-expected.md | **拆分** | C++17 optional→卷二，C++23 expected→卷四 |
| 06-type-aliases-and-using.md | vol1/ch01/ 或 vol2/ch06/ | **重写** | 通用化 |
| 07-literal-operators-and-custom-units.md | vol2/ch11/01-udl-basics.md | **重写** | 通用化 |

#### chapter-09-lambdas-functional (8 篇) → vol2

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-lambda-basics.md | vol2/ch03/01-lambda-basics.md | **重写** | 通用化 |
| 02-lambda-capture-and-performance.md | vol2/ch03/02-lambda-capture.md | **重写** | 通用化 |
| 03-std-function-vs-function-ptr.md | vol2/ch03/04-std-function.md | **重写** | 通用化 |
| 04-zero-overhead-callbacks.md | vol8/embedded/ | **重写** | 嵌入式回调优化 |
| 05-std-invoke-and-callables.md | vol2/ch03/05-functional-patterns.md | **融入** | 融入函数式模式 |
| 06-functional-error-handling.md | vol2/ch10/ (错误处理) | **融入** | 融入错误处理章节 |
| 07-ranges-basics-and-views.md | vol4/ch01/01-ranges-basics.md | **重写** | 移至高级卷 Ranges |
| 08-ranges-pipeline-in-practice.md | vol4/ch01/05-ranges-practice.md | **重写** | 移至高级卷 |

#### chapter-10-concurrency (6 篇) → vol5

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-atomic.md | vol5/ch02/01-atomic-operations.md | **重写** | 通用化 |
| 02-memory-order.md | vol5/ch02/02-memory-ordering.md | **重写** | 通用化 |
| 03-lock-free-data-structures.md | vol5/ch03/01-lock-free-basics.md | **重写** | 通用化 |
| 04-mutex-and-raii-guards.md | vol5/ch01/02-mutex-lock.md | **重写** | 通用化 |
| 05-interrupt-safe-coding.md | vol8/embedded/ch04/ | **重写** | 嵌入式中断安全 |
| 06-critical-section-protection.md | vol8/embedded/ch04/ | **重写** | 嵌入式临界区 |

#### chapter-11-modern-features (7 篇) → vol2

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 01-auto-and-decltype.md | vol2/ch06/01-auto-deep-dive.md + ch06/02-decltype.md | **拆分** | auto 和 decltype 各独立一篇 |
| 02-structured-bindings.md | vol2/ch05/01-structured-bindings.md | **重写** | 通用化 |
| 03-range-based-for-optimization.md | vol1/ch02/03-range-for.md | **重写** | 移至基础卷 |
| 04-attributes.md | vol2/ch07/01-standard-attributes.md | **重写** | 通用化 |
| 05-spaceship-operator.md | vol4/ch04/01-spaceship-operator.md | **重写** | 移至高级卷 |
| 06-designated-initializers.md | vol1/ch06/ (类基础) | **融入** | 融入类初始化 |
| 07-user-defined-literals.md | vol2/ch11/01-udl-basics.md | **重写** | 通用化 |

#### chapter-12-templates (9 篇) → vol4/ch05

| 现有文章 | 去向 | 动作 | 说明 |
|----------|------|------|------|
| 00-template-overview.md | vol4/ch05/vol1/ | **重写** | 融入模板4卷系列 Vol1 |
| 01-function-templates.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 02-class-templates.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 03-template-specialization.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 04-non-type-template-params.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 05-template-args-and-name-lookup.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 06-template-friends-and-barton-nackman.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 07-template-aliases-and-using.md | vol4/ch05/vol1/ | **重写** | 同上 |
| 08-templates-and-inheritance-crtp.md | vol4/ch05/vol1/ + vol4/ch09/cpp-idioms/02-crtp.md | **拆分** | 基础→模板系列，CRTP 深入→惯用法 |

#### core-embedded-cpp/index.md → vol8/embedded/index.md

---

### 3.2 stm32f1-challenge/ (30 篇) → vol8/embedded/ch05

| 现有目录/文章 | 去向 | 动作 |
|---------------|------|------|
| environment-setup/* (5 篇) | vol8/embedded/ch05/05-01/ | **重写** |
| led-control/* (13 篇) | vol8/embedded/ch05/05-02/ | **重写** |
| button-input/* (12 篇) | vol8/embedded/ch05/05-03/ | **重写** |

---

### 3.3 compilation-deep-dive/ (10 篇) → compilation/

**全部保留，微调格式**：统一 frontmatter，更新代码示例到现代 C++。

| 现有文章 | 去向 | 动作 |
|----------|------|------|
| 全部 10 篇 | compilation/ (直接移入) | **保留+微调** |

新增 2-3 篇（见 TODO 209）。

---

### 3.4 cpp-features/ (5 篇) → vol2 + vol4

| 现有文章 | 去向 | 动作 |
|----------|------|------|
| cpp17-string-view.md | vol2/ch08/ (拆分为 3 篇) | **拆分重写** |
| msvc-cpp-modules.md | vol4/ch03/03-module-build.md | **重写** |
| coroutines/01-coroutine-basics.md | vol4/ch02/01-coroutine-mechanics.md | **重写** |
| coroutines/02-coroutine-scheduler.md | vol4/ch02/04-coroutine-scheduler.md | **重写** |
| coroutines/03-coroutine-echo-server.md | vol5/ch06/02-asio-networking.md | **重写** |

---

### 3.5 cpp-engineering/ (3 篇) → vol7

| 现有文章 | 去向 | 动作 |
|----------|------|------|
| 文件 I/O 系列 (3 篇) | vol7/ch00/ 或 vol3/ch05/ | **重写** |

---

### 3.6 其他 (4 篇) → 分散

| 现有文章 | 去向 | 动作 |
|----------|------|------|
| environment-setup/* (2 篇) | vol1/ch00/ + vol7/ch00/ | **拆分重写** |
| debugging/* (1 篇) | vol7/ch03/ (静态分析) | **重写** |
| parallel-computing/* (1 篇) | vol6/ch02/ | **重写** |
| cpp-templates/* (1 篇占位符) | 废弃 | **删除** |

---

## 四、单篇文章重写工作流

每篇文章的重写遵循以下标准流程：

### Step 1：读取旧文
```bash
# 从归档中读取旧文章
cat documents/archive/legacy-content/core-embedded-cpp/chapter-06-ownership-raii/02-unique-ptr.md
```

### Step 2：确定目标
- 查看对应的卷大纲 TODO（如 vol2 的 201-vol2-modern-features-outline.md）
- 确认文章编号、标题、核心内容、练习重点
- 确认前置知识（prerequisites）和关联文章

### Step 3：创建目录和文件
```bash
# 创建章节目录
mkdir -p documents/vol2-modern-features/ch01-smart-pointers/
# 创建文章文件
touch documents/vol2-modern-features/ch01-smart-pointers/02-unique-ptr.md
```

### Step 4：编写 frontmatter
```markdown
---
title: "unique_ptr 详解"
description: "独占所有权智能指针的原理、用法与最佳实践"
chapter: 1
order: 2
tags:
  - 智能指针
  - RAII
  - 内存管理
difficulty: intermediate
reading_time_minutes: 20
prerequisites:
  - "卷一 ch12：内存模型基础"
  - "卷二 ch01/01：RAII 深入理解"
related:
  - "卷二 ch01/03：shared_ptr 详解"
  - "卷二 ch01/05：自定义删除器"
cpp_standard: [11, 14, 17, 20]
---
```

### Step 5：按写作风格编写内容
遵循 `.claude/writting_style.md` 的风格：
- 前言/动机段（为什么需要这个）
- 环境说明段（如有平台差异）
- 分阶段推进（目标→为什么→代码→验证）
- 踩坑预警块
- 收尾

### Step 6：编写代码示例
```bash
# 在 code/ 下创建对应的 CMake 项目
mkdir -p code/vol2-modern-features/ch01-smart-pointers/02-unique-ptr/
# 创建 CMakeLists.txt + 示例代码
```

### Step 7：编写练习题
每篇文章末尾添加 3-5 道练习：
```markdown
## 练习

1. **基础**：创建一个 unique_ptr 管理动态数组，并实现自定义删除器
2. **进阶**：实现一个简单的 unique_ptr<T[]> 包装器
3. **思考**：为什么 unique_ptr 的拷贝构造函数是 deleted 的？
```

### Step 8：验证
```bash
# 本地预览
mkdocs serve
# 代码编译验证
cd code/vol2-modern-features/ch01-smart-pointers/02-unique-ptr/ && cmake -B build && cmake --build build
# Lint
markdownlint documents/vol2-modern-features/ch01-smart-pointers/02-unique-ptr.md
```

### Step 9：提交
```bash
git add documents/vol2-modern-features/ch01-smart-pointers/02-unique-ptr.md
git add code/vol2-modern-features/ch01-smart-pointers/02-unique-ptr/
git commit -m "content: vol2 ch01/02 unique_ptr 详解"
```

### Step 10：更新卷 index.md
将卷首页中对应章节的状态从"规划中"改为"编写中"或"已完成"。

---

## 五、Phase 1 详细执行计划

Phase 1 是卷二（现代 C++ 特性），P0 最高优先级。建议执行顺序：

| 批次 | 章节 | 篇数 | 旧文参考 |
|------|------|------|---------|
| 1-1 | ch00 移动语义 (5 篇) | 5 | core-embedded-cpp/ch03 的 2 篇 |
| 1-2 | ch01 智能指针与 RAII (6 篇) | 6 | core-embedded-cpp/ch06 的 8 篇 |
| 1-3 | ch02 constexpr (4 篇) | 4 | core-embedded-cpp/ch04 的 4 篇 |
| 1-4 | ch03 Lambda 与函数式 (5 篇) | 5 | core-embedded-cpp/ch09 的 8 篇 |
| 1-5 | ch04 类型安全 (5 篇) | 5 | core-embedded-cpp/ch08 的 7 篇 |
| 1-6 | ch05-ch07 结构化绑定/auto/属性 (7 篇) | 7 | core-embedded-cpp/ch11 的 7 篇 |
| 1-7 | ch08 string_view (3 篇) | 3 | cpp-features/cpp17-string-view.md |
| 1-8 | ch09-ch11 文件系统/错误处理/UDL (9 篇) | 9 | 少量旧文参考 |

**每批次完成后**：
- 更新卷 index.md 状态
- `mkdocs serve` 验证导航和渲染
- 考虑提交一次 PR

---

## 六、统计

| 动作 | 数量 | 说明 |
|------|------|------|
| **重写** | ~105 篇 | 大部分现有文章需要重写 |
| **融入** | ~20 篇 | 多篇旧文融入新文章 |
| **拆分** | ~8 篇 | 一篇拆为多篇 |
| **保留+微调** | 10 篇 | 编译链接系列 |
| **删除** | 1 篇 | cpp-templates 占位符 |
| **新增** | ~230 篇 | 全新内容（无旧文参考） |
