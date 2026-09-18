---
title: 一道题的目录与文件
description: 题目四件套各自放什么、判题类初始代码的两种形态、找 bug 题的 code.cpp,以及六种题型各需要哪些文件
chapter: 1
order: 3
reading_time_minutes: 4
tags:
  - 工程实践
---

# 一道题的目录与文件

一道题就是一个目录,咱们把它的全部都放进去:`problem.md` 题面、`quiz.json` 判题配置、`starter.cpp` 初始代码,再加一个 `solution/` 文件夹装题解。没有注册表,没有索引文件,manifest 只认一个标志——目录里有 `quiz.json`,它就是一道题;没有,整个目录被无视。

## 目录放哪、怎么命名

位置在 `code/volumn_codes/weekly-problems/<周号>/` 下面,目录名两段式:`01-count-coins`、`02-digit-distance` 这样,数字前缀加题目的英文短名。前缀决定这一页里的题目顺序(按目录名字典序),短名要求不高,kebab-case,您起的名字能看出是哪道题就行。

同目录下的 `examples/` 里有六道示范题,一种题型一道,文件长什么样都以它们为准。写新题拿不定主意时,您翻 `code/volumn_codes/weekly-problems/examples/` 里同题型的目录照着对。

## problem.md:题面

纯 markdown,不写 frontmatter,不写标题头,第一行直接进入题面。它会被站点拿到浏览器里渲染,行内代码、代码块、加粗都可用。写法上就一个要求:咱们站在没见过这道题的学员角度,把任务说完整——输入或参数是什么、要求返回什么、必须满足什么约束("必须用递归"这类硬性要求直接加粗写进去)。判题类题面最好附一两个具体例子,学员动手前全靠例子校准理解。

反面教材是把题面写成谜语。您在题面省下的每句话,都会变成 hints 里要补的提示和题解里要还的债,不划算。

## starter.cpp:判题类的初始代码

只有判题类(`judge-assert` 和 `judge-io`)需要这个文件,它就是学员打开题卡时编辑器里的初始内容。两种形态照题型分:`judge-assert` 给一个待实现的函数骨架,函数签名就是判题时要调用的签名,正文一句话说明;`judge-io` 给一个含 `main` 的程序骨架,读入部分留白。您分别对照 `examples/01-count-coins` 和 `examples/02-sum` 的 starter 就明白了。

您在骨架里只放"学员该接着写的空位",别放实现思路,也别放任何判题相关的标记。有个词值得专门点出来:判题协议里有个 `##JUDGE##` 标记,那是站点判题器自己生成、自己识别的内部记号,学员代码和 starter 里出现它反而会干扰判题。出题人从头到尾不需要知道它存在。

口答类(fill、choice、reveal)和找 bug 类(find-bug)没有 starter,学员面对的从头到尾就是题面,咱们能下的功夫全在题面上。

## code.cpp:找 bug 题的代码

`find-bug` 题型专属,放一段带 bug 的完整代码,学员的任务是在里面把有问题的行标出来。`quiz.json` 里的 `bugLines` 填 bug 所在行号,1-based,您数着 `code.cpp` 的行算。参照 `examples/05-dangling-view`。

## 哪种题型要哪些文件

题型怎么选、`quiz.json` 每个字段怎么填,是[下一篇](04-quiz-json.md)的主场;这里给咱们一张速查表,复制模板包时照着核对文件齐不齐。

| 题型 | starter.cpp | code.cpp | 说明 |
|---|---|---|---|
| `judge-assert` | 要 | 不要 | 函数骨架,tests 是断言 |
| `judge-io` | 要 | 不要 | 程序骨架,tests 是输入输出对 |
| `fill` / `choice` / `reveal` | 不要 | 不要 | 口答交互,题面即全部 |
| `find-bug` | 不要 | 要 | 标 bug 行,`bugLines` 数 code.cpp 的行 |

不管哪种题型,您的 `solution/` 都得有。题解的写法和投稿规则是[第五篇](05-solutions-and-checklist.md)的事,这里只说结构:一个人一个子文件夹,文件夹名用 GitHub 用户名,里面 `answer.md` 必有,判题类一般再带一份 `solution.cpp` 参考实现。模板包 `code/volumn_codes/weekly-problems/_template/01-your-problem/` 把这些文件都摆好了,复制目录、改文件名、照注释填内容,三步完事。

咱们要建的文件就这些,没有配置项要登记,没有清单要对。目录建对,题就在;建错,要么整题消失,要么构建报错,两种结果上一篇都讲过了。
