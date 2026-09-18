---
title: 周页面 week-NN.md 怎么写
description: 周页面 frontmatter 逐字段说明、正文骨架、可直接复制的模板,以及单行书写这类源码级约束
chapter: 1
order: 2
reading_time_minutes: 4
tags:
  - 工程实践
---

# 周页面 week-NN.md 怎么写

周页面是一期的门面:栏目列表读它的标题和日期,学员进来读它的引言,您最后写的出处致谢也在这个文件里。它本身很短,难点全在 frontmatter 的字段和几条不起眼的书写约束上。模板包里的 `week-NN.md` 已经把这些都填好示范值了,直接复制过去改,比从空白文件手写省心得多。

## 文件放哪、叫什么

位置固定:`documents/weekly-problems/week-NN.md`。周号两位数字,您从 `week-01` 接着往下编,上一篇讲过没补零会发生什么。文件名去掉 `.md` 之后,就是这一期在 `code/volumn_codes/weekly-problems/` 下找题目目录的名字,两边必须一致。

## frontmatter 逐字段

| 字段 | 填什么 | 说明 |
|---|---|---|
| `title` | `"Week 2 · 主题名"` | 显示在栏目列表和周页标题,惯例带 `Week N ·` 前缀 |
| `description` | 一句话 | 这期练什么、难度什么档,显示在列表描述位 |
| `chapter` | `16` | 固定值,weekly 栏目在 frontmatter 校验器里的章节号 |
| `order` | 从 `3` 起递增 | week-01 占了 1,题解页占 2,新的一期接着往下编,只影响目录排序 |
| `dateRange` | `"2026-09-21 ~ 09-27"` | 这期覆盖的日期,显示在列表时间位,没有格式校验,照惯例写 |
| `difficulty` | 三选一 | `beginner` / `intermediate` / `advanced`,填别的值校验器直接报错 |
| `platform` | `host` | 目前栏目题目都是本机平台,照写 |
| `weeklyThanks` | 致谢名单 | 数组,每项 `github` 填 GitHub 用户名、`role` 填贡献说明,构建期校验,详见上一篇 |
| `tags` | 白名单内 | 只能从 `scripts/tags.json` 里挑,惯例 `host` + `cpp-modern` + 难度档 |

`order` 有个小历史包袱,咱们看一眼:它只用来过校验和目录排序,周列表的先后顺序完全由周号排序决定,所以两期 `order` 撞号也不影响栏目页面,但目录树里会乱,接着递增就好。

## 三条书写约束

**`title`、`description`、`dateRange` 必须各自写成单行。** manifest 生成器是拿正则按行从原文里提取这三个字段的,值一旦折行,提取到的就是残缺的一半,栏目列表上标题突然缺字、描述对不上,都查到这里。description 再长,您也把它压成一行写完。

**`sidebar: false` 和 `aside: false` 您不用写。** 构建器对周页面会自动注入这两个设置(关掉侧栏和页内目录),frontmatter 里手写反而多余。模板里没有它们,是有意的。

**tags 您别现编。** 校验器拿 `scripts/tags.json` 当白名单,不在名单里的 tag 过不了校验。想加新 tag,去那个 JSON 里补,别在文章里硬写。

## 正文骨架

正文三个部分,咱们看一期实例比看描述快,`week-01.md` 是现成参照:

开头是引言,两三段散文,讲这周为什么选这个主题、几道题的共同线索、难度怎么递进。这是您唯一一块自由发挥的正文,写给学员看,值得花心思。

中间挂题卡,一道题一行组件:

```md
<QuizProblem src="code/volumn_codes/weekly-problems/week-02/01-your-problem" />
```

`src` 是题目目录相对仓库根的路径,和下一篇讲的题目目录一一对应,您逐条对好。一页挂几道题都行,week-01 挂了三道。

结尾一条分隔线加出处致谢段,说清题目源自哪门课、哪本书或哪次讨论,出题人是谁,贴上 GitHub 链接。素材是别人的时候,您把这段当成栏目的规矩来写。

## 从模板开始

`code/volumn_codes/weekly-problems/_template/week-NN.md` 就是按上述规则填好示范值的完整周页面,您把它复制为 `documents/weekly-problems/week-02.md`,frontmatter 里带注释说明每个字段改哪里。复制为 `documents/weekly-problems/week-02.md`,改周号、标题、日期、致谢,引言和题卡路径换成自己的,删掉注释,周页面就完了。

最常见的三个错误,咱们上一篇都提过,这里对着周页面再点一次名:周号没补两位;周页面文件名和题目目录不同名(这期零题且无警告);`weeklyThanks` 的 `github` 随手填了个中文昵称(构建直接失败,必须填 GitHub 用户名)。
