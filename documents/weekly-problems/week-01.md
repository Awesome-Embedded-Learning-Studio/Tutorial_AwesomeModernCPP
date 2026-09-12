---
title: "Week 1 · 递归三连"
description: "数硬币、数字距离、冰雹序列。三道递归练习，把大问题拆开，把想法写成代码。"
chapter: 16
order: 1
dateRange: "2026-09-14 ~ 09-20"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: 本期题目提供者 · 栏目主要创意发起人
tags:
  - host
  - cpp-modern
  - intermediate
---

# Week 1 · 递归三连

这一波是递归！

三道题其实是一个主题，也就是大名鼎鼎的**递归**。

尽管递归在工程上比较谨慎，但是他的确是解决问题的一个重要的思路。这一次带来的三个题目源自CS61A。

前两道练「把大问题拆成子问题」的手感——数硬币按面值拆、数字距离按数位拆

第三道,我们把递归装进 C++23 的 `std::generator`,感受「递归 + 协程」的组合拳。

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/01-count-coins" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/02-digit-distance" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-01/03-hailstone" />

---

**题目出处**:本期三题源自 UC Berkeley CS61A 课程的作业与实验题(数硬币 / Digit Distance / 冰雹序列),经 [owollz4](https://github.com/owollz4) 大佬的提议与选题，笔者稍微针对 C++ 化改编——同一道题,Python 课的做法与 Modern C++ 的做法,对照着看别有味道。
