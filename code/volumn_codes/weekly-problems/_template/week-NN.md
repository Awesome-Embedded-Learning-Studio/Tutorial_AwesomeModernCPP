---
# 周页面模板:复制为 documents/weekly-problems/week-02.md(周号两位数字)
# title / description / dateRange 必须各自单行,折行了栏目首页就读不到
title: "Week 2 · 主题名"
description: "一句话:这几道题练什么、难度什么档。"
chapter: 16
order: 3
# order 从 3 起递增(week-01 占 1,题解页占 2),只影响目录排序
dateRange: "2026-09-21 ~ 09-27"
difficulty: intermediate
platform: host
weeklyThanks:
  # github 填出题人的 GitHub 用户名,构建期按用户名规则校验,乱填会构建失败
  - github: octocat
    role: 本期题目提供者
tags:
  # 只能从 scripts/tags.json 白名单里挑
  - host
  - cpp-modern
  - intermediate
---

# Week 2 · 主题名

<!-- 引言:两三段话讲这周为什么选这个主题、几道题的共同线索、难度梯度。
     写给学员看,口语一点,像开场白。下面的 QuizProblem 一道题一条。 -->

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/01-your-problem" />

<!-- 多道题就多贴几个 QuizProblem,src 改成对应题目目录 -->

---

<!-- 出处致谢:题目源自哪门课/哪本书/哪次讨论,出题人是谁,贴 GitHub 链接 -->

**题目出处**:本期题目源自……,经 [出题人](https://github.com/octocat) 提议与选题。
