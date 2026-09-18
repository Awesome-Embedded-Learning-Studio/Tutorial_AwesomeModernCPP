---
title: 题解、自检与上线前演练
description: 题解目录的投稿规则与 answer.md 写法、提交前的本地自检命令,以及拿 week-99 试验周走一遍全流程的演练步骤
chapter: 1
order: 5
reading_time_minutes: 4
tags:
  - 工程实践
---

# 题解、自检与上线前演练

题解是这个栏目最欢迎的投稿:门槛低,一篇题解就是一个文件夹的事。这一篇讲题解怎么投、整个 PR 提交前怎么自检,最后带您用一个试验周把全部流程走一遍,走通了,真实出题就是重复同样的动作。

## 题解目录的规矩

位置在题目目录的 `solution/` 下,一个投稿人一个子文件夹,文件夹名用您的 **GitHub 用户名**。这不是随手约定的:题解页会把文件夹名渲染成指向 `github.com/<用户名>` 的链接,填个中文昵称,链出去就是 404。

您文件夹里的 `answer.md` 必须有,这是题解的正文;判题类题型一般再放一份 `solution.cpp` 参考实现。两件事构建期都查:子文件夹缺 `answer.md`,构建失败;`solution.cpp` 是可选的,口答类和找 bug 类通常没有它,示范目录 `code/volumn_codes/weekly-problems/examples/` 里 03、04、06 三道题就没有,照样正常上线。多人给同一道题投稿就多几个文件夹,并列展示,互不影响。

## answer.md 写成什么样

`answer.md` 是给做完题(或者做不出来)的学员看的,标准咱们只认一条:学员读完真的懂了。对着一份好题解学,最快的办法是看现成的——`code/volumn_codes/weekly-problems/week-01/01-count-coins/solution/charliechen114514/answer.md` 是栏目第一篇题解,结构值得借:开头一段把破题的思路讲清楚,讲分类怎么分、边界为什么是那样;然后对着 `solution.cpp` 把关键段落逐段解释,代码和文字交替;结尾专门有一段"排错",把学员最可能写出的错误版本拿出来讲错在哪、跑出来是什么现象。

纯 markdown,不用 frontmatter。写的时候,您把自己放回"卡在这道题上的人"的位置:他哪里想不通,就多讲哪里;代码里一行能看懂的东西,不必翻译成文字。模板包里 `solution/your-github-name/answer.md` 给了这个结构的骨架注释,从它开始改。

## 提交流程与本地自检

仓库的通用贡献流程(分支卫生、squash 合并、CI 红了怎么办)在[贡献速查手册](../dev/03-contribution-cookbook.md)里讲得很全,咱们这里不重复。出题 PR 特有的自检就下面这几条命令,提交前跑一遍,大部分 CI 往返都省了:

| 查什么 | 命令 |
|---|---|
| frontmatter(周页面字段) | `.venv/bin/python scripts/validate_frontmatter.py` |
| 文档链接 | `pnpm check:links` |
| 内容质量(链接、tag、图片) | `.venv/bin/python scripts/check_quality.py documents/` |
| weekly 机制回归测试 | `pnpm test:weekly` |
| 页面眼验 | `pnpm dev` |

Python 命令咱们一律走仓库根目录的 `.venv/bin/python`,系统 `python3` 缺 PyYAML,校验结果不可信,这件事速查手册里也强调了。眼验时记得 `pnpm dev` 下改目录刷新即生效,不用重启。

## 上线前演练:拿 week-99 走一遍全流程

真实周号很宝贵,咱们拿它试错就可惜了。全流程演练用一个注定要删掉的 `week-99`,五个动作走完,机制就吃透了:

1. 复制模板:把 `code/volumn_codes/weekly-problems/_template/week-NN.md` 复制为 `documents/weekly-problems/week-99.md`;`code/` 这边先 `mkdir week-99/`,再把 `01-your-problem/` 目录复制进去改名为 `01-hello/`(对着不存在的路径直接 `cp -r`,题目会少套一层目录,这是演练时实打实踩过的);最后把 `solution/your-github-name/` 改成您自己的用户名。
2. 填空:题面写一道最简单的题(比如读两个数输出和),`quiz.json` 照模板填,`starter.cpp`、`answer.md`、`solution.cpp` 各填几行能跑的;周页面里 `QuizProblem` 的 `src` 改成 `week-99/01-hello`,frontmatter 的注释全删掉。
3. 眼验:`pnpm dev` 起服务,打开栏目首页,`week-99` 应该出现在列表最前(周号最大);进周页,您会看到题卡正常渲染,把参考答案贴进编辑器提交判题,应该全部通过;再开题解页,您的用户名出现在这道题下面。
4. 顺手验证一次构建期校验是真的:把 `answer.md` 临时改名,刷新页面,应该看到 manifest 报错;改回来,恢复如常。
5. 删干净:眼验做完,把 `documents/weekly-problems/week-99.md` 和 `code/volumn_codes/weekly-problems/week-99/` 两处都删掉,刷新确认栏目恢复原状。`git status` 里不该留下这两个路径的任何痕迹。

演练通过,意味着您已经完整走过"模板 → 目录 → 判题 → 题解 → 校验"的每一环。之后真实出题,把周号从 99 换成下一个正式编号,内容换成真题,提 PR,等 CI 绿了合入,下一期就上线了。
