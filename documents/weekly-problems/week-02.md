---
title: "Week 2 · 语言细节与经典老题"
description: "中文命名、逗号表达式两道口答题,加上 copy_n、八皇后、最长平台三道在线判题——老书经典换判题玩法。"
chapter: 16
order: 3
dateRange: "2026-09-21 ~ 09-27"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: 本期题目提供者 · 解答题与思考题首发
tags:
  - host
  - cpp-modern
  - intermediate
---

# Week 2 · 语言细节与经典老题

这一波题型扩容。Week 1 咱们交的全是判题代码,这周「解答题」和「思考题」进场:有两道题不用打开编辑器,把想法说清楚、把数算明白就算过关。想清楚再动笔,恰好是这两类题想练的东西。

前两道考的是语言与工程细节:一道问「中文命名为什么工程上不被待见」——编译器明明点头,为什么真实项目里没人这么写;另一道算「`x = (a++, b++)` 到底得几」——顺带还有一问,出题人自己一开始都答错了。后三道换回写代码,题目都出自老书和经典题:`copy_n` 挑自《C和指针》,「最长平台」是同一类教科书练习,八皇后则是回溯算法绕不开的入门题。难度从一星铺到三星,题卡的顺序就是建议的做题顺序,您从上往下正好越做越有嚼头。

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/01-chinese-identifiers" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/02-comma-expression" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/03-copy-n" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/04-eight-queens" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-02/05-longest-plateau" />

---

**题目出处**:「中文命名」来自 [owollz4](https://github.com/owollz4) 的胡思乱想;「逗号表达式」是他朋友抛出的原题;`copy_n` 出自 Kenneth Reek《C 和指针》的课后练习;「八皇后」与「最长平台」是流传已久的经典题。本期五题经 owollz4 整理供稿,笔者把它们落成在线判题与口答的格式,判题数据均经本地实测核过。
