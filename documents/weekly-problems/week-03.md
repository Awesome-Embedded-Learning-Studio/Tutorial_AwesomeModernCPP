---
title: "Week 3 · 移动语义与所有权"
description: "std::move 专场:unique_ptr 的独占所有权、零拷贝 swap、moved-from 的「有效但未指定」、手写析构让移动消失的那条规则,以及短字符串的移动其实并不更快的 SSO 事实。"
chapter: 16
order: 4
dateRange: "2026-09-28 ~ 10-04"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: 本期题目提供者 · 移动语义专场自命题
tags:
  - host
  - cpp-modern
  - intermediate
---

# Week 3 · 移动语义与所有权

前两期咱们练的是递归和语言细节,这一期轮到现代 C++ 的核心机制:移动语义。五道题都绕着 `std::move` 展开。先把一件事说在前面:它本身不搬任何东西,只是把表达式转成右值,真正干活的是移动构造和移动赋值。这周的五道题正好把这条链走全:`unique_ptr` 为什么拷贝不了(所有权只此一份)、用移动写 swap 怎么做到一次拷贝都没有(它就是 `std::swap` 的实现方式)、被移动过的对象还剩什么(「有效但未指定」)、什么时候 `std::move` 写了也不起作用(手写析构函数会压掉移动构造的那条规则),最后压轴一问:移动,真的是免费的吗(短字符串会给出一个反直觉的答案)。

题型照旧混着来:开场一道编译多选热身,中间一道在线判题——这次的判题器会核对拷贝计数,您的 swap 里混进一次拷贝都过不了关——再加一道单选口答、一道找病根,压轴是一道不判分的思考题,留给您去 Godbolt 亲眼看看编译器到底生成了什么代码。难度从一星铺到三星,题卡的顺序就是建议的做题顺序。前两期的题出自老书和朋友们的胡思乱想,这期是笔者围绕教程 [vol2 移动语义一章](../vol2-modern-features/ch00-move-semantics/index.md) 自命题——咱们自己的第一场专场。

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/01-unique-ownership" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/02-move-swap" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/03-moved-from-state" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/04-vanished-move" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-03/05-sso-move-cost" />

---

**题目出处**:本期五题由 [笔者](https://github.com/Charliechen114514) 围绕教程 [vol2 移动语义一章](../vol2-modern-features/ch00-move-semantics/index.md) 自命题。第 1、2 题的考点出自 cppreference 对 `std::unique_ptr` 与 `std::swap` 的规格说明(拷贝构造被删除、C++11 起交换改用移动实现);第 3 题「有效但未指定」的措辞出自 C++ 标准库 [lib.types.movedfrom],与《Effective Modern C++》条款 23 相互印证;第 4 题的考点是 C++ Core Guidelines C.21(rule of five)与「声明析构则隐式移动不再生成」的语言规则;第 5 题的 SSO 内联容量(libstdc++ 与 MSVC 为 15 字符、libc++ 为 22 字符)是三大主流实现的实现事实,[string 深入](../vol3-standard-library/containers/04-string-memory-deep-dive.md)一篇有完整拆解。全部判题数据与题中断言均经本地双编译器(GCC 16 / Clang 23)实测核过。
