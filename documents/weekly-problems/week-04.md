---
title: "Week 4 · 老书地基与 va_list"
description: "《C和指针》地基周:gcd 递归热身、getchar/putchar 大小写、va_list 读参数、signed char 检验和,压轴自己实现一个迷你 sprintf,五道全部在线判题,难度一到三星。"
chapter: 16
order: 5
dateRange: "2026-10-05 ~ 10-11"
difficulty: intermediate
platform: host
weeklyThanks:
  - github: owollz4
    role: 本期题目提供者 · 前四题供稿
  - github: Charliechen114514
    role: 压轴题 mini sprintf 自命题 · 落题判题
tags:
  - host
  - cpp-modern
  - intermediate
---

# Week 4 · 老书地基与 va_list

前三期咱们从递归走到语言细节,再走进移动语义,这一期回到 C 的地基本身。本周前四道题出自 owollz4 的供稿,压轴一道由笔者围绕同一本书自命题。书是 Kenneth Reek 的《C和指针》,一本把指针、函数和字节这些地基讲得结结实实的老书。五道题有一条共同的线索:C 里参数怎么传、字节怎么算。gcd 用递归算最大公约数,小写变大写用 getchar 和 putchar 一个字符一个字符地过 IO,max_list 请您用 va_list 读不定个数的参数,checksum 把累加做到 signed char 的字节级别,压轴的迷你 sprintf 把前面几条线并成一道题:这一次您不调 printf,您自己实现一个。

难度从一星铺到三星,题卡的顺序就是建议的做题顺序:gcd 热身,字符 IO 过一遍,va_list 第一次上手,检验和做到字节级别,压轴让您从调用 va_arg 的人,变成定规则的人。题型这周不混了,前两期出场过的口答题和找 bug 题这周休息,五道全部是在线判题,每一道都要您真把代码敲进去跑一遍。

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/01-gcd" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/02-lower-to-upper" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/03-max-list" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/04-checksum" />

<QuizProblem src="code/volumn_codes/weekly-problems/week-04/05-mini-sprintf" />

---

**题目出处**:本期前四题由 [owollz4](https://github.com/owollz4) 供稿,题面文字与参考答案都是他的原稿,判题接入是笔者做的。压轴题由[笔者](https://github.com/Charliechen114514)围绕同一本书第 7 章自命题。gcd、max_list 和迷你 sprintf 三题同源自《C和指针》第 7 章的课后编程练习,公开题解仓库 DragScorpio/Pointers-On-C-Solutions 的 chapter7 里能找到 gcd.c、maxlist.c 和 implementPrintf 的对应实现。其中 gcd 同时也是流传已久的经典题,一道题占了两个来头。小写变大写与 checksum 由投稿人标注,同样出自这本书。落题时有一笔如实交代:checksum 的原稿漏了题面要求的「逐字符复制输入到输出」,判题版里那行 `putchar(ch)` 是笔者补的。压轴题的底子也在第 7 章:那一章的编程练习里本就有「自己实现一个 printf」,站里把它裁成 `%d`、`%s`、`%c`、`%%` 四种转换,判题改成在线缓冲区比对。再往远说一句,C++ 把这两件事都换了:格式化交给 [std::format](../vol3-standard-library/strings/52-format.md) 和 [std::print](../vol3-standard-library/strings/53-print.md),可变参数交给[可变参数模板](../cpp-reference/templates/02-variadic-templates.md)。全部判题数据经本地双编译器(GCC 16 / Clang 22)实测核过。
