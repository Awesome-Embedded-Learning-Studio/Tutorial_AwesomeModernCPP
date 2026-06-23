---
title: "错误处理与工具"
description: "optional、variant、any、expected、functional、error_code、stacktrace、source_location"
sidebar_order: 60
---

# 错误处理与工具

错误处理与运行时工具：把「可能没有」「封闭多态」「值或错误」做成类型的 optional/variant/expected，函数对象与 `std::function` 的代价，错误码体系 error_code，C++23 终于标准化的调用栈采集 stacktrace，以及编译期代码位置 source_location。

<ChapterNav variant="sub">
  <ChapterLink href="61-optional">optional：把「可能没有」做成类型</ChapterLink>
  <ChapterLink href="62-variant">variant：类型安全的联合体与 visit</ChapterLink>
  <ChapterLink href="63-any">any：能装任何类型</ChapterLink>
  <ChapterLink href="64-expected">expected：值或错误（C++23）</ChapterLink>
  <ChapterLink href="65-functional">functional：std::function 的代价</ChapterLink>
  <ChapterLink href="66-error-code">error_code：错误码体系与自定义 category</ChapterLink>
  <ChapterLink href="67-stacktrace">stacktrace：C++23 调用栈采集</ChapterLink>
  <ChapterLink href="68-source-location">source_location：编译期代码位置</ChapterLink>
</ChapterNav>
