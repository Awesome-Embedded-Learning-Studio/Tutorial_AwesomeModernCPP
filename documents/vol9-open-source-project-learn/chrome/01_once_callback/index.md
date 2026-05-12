# OnceCallback：从 Chromium 学到的回调设计

本目录通过实现 Chromium 风格的 `OnceCallback` 组件，系统讲解现代 C++ 回调系统的设计。内容分为两个学习路径：

## 新手完整教程（full/）

面向零基础读者，从 C++ 基础特性复习开始，逐步引导到完整的组件实现。

**前置知识（7 篇）：**

<ChapterNav variant="sub">
  <ChapterLink href="full/pre-00-once-callback-cpp-basics-review">OnceCallback 前置知识速查：C++11/14/17 核心特性回顾</ChapterLink>
  <ChapterLink href="full/pre-01-once-callback-function-type-and-specialization">OnceCallback 前置知识（一）：函数类型与模板偏特化</ChapterLink>
  <ChapterLink href="full/pre-02-once-callback-invoke-and-callable">OnceCallback 前置知识（二）：std::invoke 与统一调用协议</ChapterLink>
  <ChapterLink href="full/pre-03-once-callback-lambda-advanced">OnceCallback 前置知识（三）：Lambda 高级特性</ChapterLink>
  <ChapterLink href="full/pre-04-once-callback-concepts-and-requires">OnceCallback 前置知识（四）：Concepts 与 requires 约束</ChapterLink>
  <ChapterLink href="full/pre-05-once-callback-move-only-function">OnceCallback 前置知识（五）：std::move_only_function (C++23)</ChapterLink>
  <ChapterLink href="full/pre-06-once-callback-deducing-this">OnceCallback 前置知识（六）：Deducing this (C++23)</ChapterLink>
</ChapterNav>

**动手实践（6 篇）：**

<ChapterNav variant="sub">
  <ChapterLink href="full/01-1-once-callback-motivation-and-api-design">OnceCallback 实战（一）：动机与接口设计</ChapterLink>
  <ChapterLink href="full/01-2-once-callback-core-skeleton">OnceCallback 实战（二）：核心骨架搭建</ChapterLink>
  <ChapterLink href="full/01-3-once-callback-bind-once">OnceCallback 实战（三）：bind_once 实现</ChapterLink>
  <ChapterLink href="full/01-4-once-callback-cancellation-token">OnceCallback 实战（四）：取消令牌设计</ChapterLink>
  <ChapterLink href="full/01-5-once-callback-then-chaining">OnceCallback 实战（五）：then 链式组合</ChapterLink>
  <ChapterLink href="full/01-6-once-callback-testing-and-perf">OnceCallback 实战（六）：测试与性能对比</ChapterLink>
</ChapterNav>

## 进阶设计指南（hands_on/）

面向有 C++ 模板经验的读者，快速走读设计动机、实现策略和测试验证。

<ChapterNav variant="sub">
  <ChapterLink href="hands_on/01-once-callback-design">once_callback 设计指南（一）：动机与接口设计</ChapterLink>
  <ChapterLink href="hands_on/02-once-callback-implementation">once_callback 设计指南（二）：逐步实现</ChapterLink>
  <ChapterLink href="hands_on/03-once-callback-testing">once_callback 设计指南（三）：测试策略与性能对比</ChapterLink>
</ChapterNav>
