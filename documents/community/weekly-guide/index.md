---
title: "出题与题解手册"
description: "「每周一些题」栏目怎么出题、怎么投稿题解:栏目机制、模板包、判题配置与上线演练,给维护者和社区贡献者"
---

# 出题与题解手册

每周一些题是站内的在线做题栏目,题目和题解都来自社区。这本手册讲的,是您参与它的全部方式:内容放在哪、构建怎么发现它们、一道题怎么配判题、题解怎么投、上线前怎么自检。

参与不需要读完五篇再动手,按您的情况挑入口:

您只想提个选题,或者手头只有题面和参考答案,发 issue 或交流群说一声就行;材料怎么交、署名怎么给,[第一篇](01-how-it-works.md)里说清了。

您要完整出一期题,不用从零写:仓库里有现成的模板包 `code/volumn_codes/weekly-problems/_template/`,复制、改名、照注释填空,再对照第二到第四篇核对细节。

您只想投稿题解,这是最轻的参与方式,一个文件夹的事,看[第五篇](05-solutions-and-checklist.md)。

您想让 AI 协作落题,`code/volumn_codes/weekly-problems/AGENTS.md` 是给 AI agent 的执行规程,喂给它就行。

<ChapterNav variant="main">
  <ChapterLink num="1" href="01-how-it-works">栏目怎么运转,出题怎么参与</ChapterLink>
  <ChapterLink num="2" href="02-week-page">周页面 week-NN.md 怎么写</ChapterLink>
  <ChapterLink num="3" href="03-problem-directory">一道题的目录与文件</ChapterLink>
  <ChapterLink num="4" href="04-quiz-json">quiz.json 与六种题型</ChapterLink>
  <ChapterLink num="5" href="05-solutions-and-checklist">题解、自检与上线前演练</ChapterLink>
</ChapterNav>
