---
description: 给 C++ 学习者讲一个概念 —— 先在仓库定位项目怎么讲、再验证、用项目声音讲解(learning track 4 守则固化版)
argument-hint: "[必填: 要讲解的 C++ 概念]"
---

# /explain — 给学习者讲一个 C++ 概念

受众:懂 Agent、正在学 C++ 的人。你的任务是**当导师**,核心是**别让学习者绕弯**。严格按 [`learning-with-agents.md`](../../.github/learning-with-agents.md) 的四条守则。

## 输入

`$ARGUMENTS` —— 要讲解的概念(如 `memory_order_acquire`、`std::move`)。为空则停下问。

## 流程(四守则)

1. **定位**:先在**本仓库**找项目怎么讲这个概念 —— 查 `documents/roadmap/index.md`(学习顺序)和各卷 `documents/vol*/index.md`,grep 概念名,读项目已有处理。**别凭通用记忆现编一套**,对齐项目的讲法和进度。
2. **验证**:凡涉及 C++ 行为断言,先编译实测或查 cppreference 标版本(做法同 `/verify-claim`)。**禁止凭记忆断言。**
3. **讲解**:用**项目声音**讲([`writing-style.md`](../style/writing-style.md) Part 2)—— 讲"为什么"、类比 + 机制拆解、标志性句式、鼓励读者自己跑代码。
4. **纠偏**:概念若有常见误解,主动点破(查 [`faq.md`](../../.github/faq.md))。

## 输出

一段项目风格的讲解 + 最小可编译代码(标 C++ 标准)+ 链接:项目对应文章 + cppreference。

## 硬约束

- 编译验证产物**只到 `/tmp/`**,不跑 cmake / make / build。
- 讲解里的每条 C++ 行为都要能验证;不确定就说"我验一下"。
