---
title: "Week 6 · 类型说了算"
description: "主题类型说了算:enum class 关掉隐式转换、有符号撞无符号、整型提升左移出两个答案、按行判型,后半周 optional 与 variant 管住「有没有」和「此刻是谁」,周日 optional<bool> 压轴,七道全部在线判题。"
chapter: 16
order: 7
dateRange: "2026-11-02 ~ 11-08"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: 本期七题自命题 · 出题与判题数据实测
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
---

# Week 6 · 类型说了算

节奏和上周一样:周一到周日每天一题,周一拿一星热身,难度总体往上走,周五特地放一道一星让您缓口气,周日三星压轴收场。题目都不大,一题一考点,做完当天那一个知识点就算吃透了——咱们的建议是别攒,每天的题当天清。

本周主题「类型说了算」,一条线索从「数怎么转」走到「值怎么存」。前半周盯着转换:老式 enum 和 enum class 谁能悄悄变整数,负数撞上无符号是谁先被转过去,一个字节左移一位为什么算出两个答案;周四把「先判型再取值」落到按行分型上;后半周看存值:optional 管「有没有」,variant 管「此刻是谁」,周日 optional<bool> 把这两层撞在一起,三行代码,答案反直觉,留给您先口答再展开。难度排布 1-2-2-2-1-2-3,周二到周四连着三道二星。

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/01-which-fails" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/02-unsigned-compare" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/03-promote-shift" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/04-int-or-text" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/05-first-even" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/06-variant-ask" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-06/07-optional-bool" />

---

**题目出处**:本期七道题全部由 [笔者Charliechen](https://github.com/Charliechen114514)自命题,是围绕类型系统的一条线现写的:前三题管数怎么转(enum class 关掉的隐式转换、有符号撞无符号的常规算术转换、整型提升与窄类型回存),第四题把「先判型再取值」落到按行分型,后三题管值怎么存。全部判题数据(断言与输入输出比对)经 GCC 16.2.1、-std=c++20 本地实测核过。
