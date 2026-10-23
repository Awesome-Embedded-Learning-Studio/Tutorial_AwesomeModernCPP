---
title: "Week 5 · 容器的手感"
description: "主题容器的手感:range-for 改不动元素、set 去重、find 的 end()、accumulate 初始值截断、map 查询即写入、vector<bool> 代理,erase-remove 压轴,七道全部在线判题,难度一到三星。"
chapter: 16
order: 6
dateRange: "2026-10-26 ~ 11-01"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: Charliechen114514
    role: 本期七题自命题 · 出题与判题数据实测
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
---

# Week 5 · 容器的手感

本周主题叫「容器的手感」,练的是 STL 上手最容易翻车的地方:文档里人人念过一遍、真写起来照样错的那几种。主角是容器,也捎带两条算法的通用约定。您天天写 C++,正好一天一道当口算;刚学容器,这七道也会替您把最容易错的地方先踩一遍。

一周有一条线索:从「改一个元素」走到「删一类元素」。难度这么爬:前三天一星,把容器的基本脾气摸熟;中间三天两星,专收反直觉行为;周日三星压轴。每道题都不大,咱们的指望只有一条:您亲手踩一遍,比读十遍文档记得牢。

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/01-double-all" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/02-dedupe-sort" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/03-find-miss" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/04-accumulate-init" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/05-map-bracket" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/06-vector-bool" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-05/07-erase-remove" />

---

**题目出处**:本期七道题全部为自命题,出题人是 GitHub 用户 [Charliechen114514](https://github.com/Charliechen114514),栏目里自称的笔者就是他。七道围绕「容器的手感」现写:range-for 里 auto 与 auto& 的差别、set 插入即去重遍历即升序、find 未命中返回 end()、accumulate 初始值决定累加类型、map 的 operator[] 只读查询也会写入、vector<bool> 的下标代理,周日压轴收在 erase-remove 惯用法上。全部判题数据(断言、输入输出用例、选项答案、填空答案、找错行号)经 GCC 16.2.1(-std=c++20 -Wall -Wextra)本地实测核过。
