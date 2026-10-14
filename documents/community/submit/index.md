---
title: "社区投稿指南"
description: "从 Markdown 草稿到社区初刊，了解投稿、署名与审阅流程"
---

# 社区投稿指南

有一篇学习笔记、工程经验、源码阅读或问答整理想分享？准备好 Markdown 草稿就可以开始，维护者会协助整理站点元数据和展示。

[提交文章草稿](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues/new?template=article-submission.yml) · [查看社区文章](../)

## 两种投稿方式

1. **直接交稿**：通过上面的投稿表单粘贴正文或草稿链接，说明来源和授权。维护者协助将稿件整理进仓库。
2. **提交 PR**：将 Markdown 放入 `documents/community/incoming/`，使用[文章投稿 PR 模板](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/compare?expand=1&template=article-submission.md)。

请提供文章标题、目标读者、参考资料，以及图片和代码来源。包含代码时，注明编译器、适用标准和实际验证情况。代投或转载时请说明原作者及授权。

## 署名与提交来源

文章页的信息区会自动显示最早可追溯的 Git 提交身份，链接到对应提交。这是提交来源记录；代投、整理和迁移都可能由原作者之外的人完成，Git 提交身份不作为原创证明。源码压缩包或浅克隆可能缺少完整历史，页面只展示能追溯到的记录。

如果需要显示原作者，维护者将署名放入 frontmatter 的 `contributors`，正文无需重复写作者栏。没有单独署名时，页面只显示提交来源，不会默认把维护者标成作者。匿名或化名投稿请在交稿时说明；当前独立作者卡片需要 GitHub 账号，纯文字署名可先保留在稿件中，由维护者确认展示方式。

```yaml
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: 游翰灵
    roles: [author]
```

撰写、修订、翻译、审校、维护分别使用 `author`、`revision`、`translation`、`review`、`maintain`。[作者页](../authors/)展示作者的 GitHub 头像和文章，只统计 `author` 署名；提交来源和变更记录单独展示。

## 栏目和收录状态

| 栏目 | 元数据值 | 适合的内容 |
| --- | --- | --- |
| 学习笔记 | `learning-notes` | 概念学习、库的使用、实验记录 |
| 工程经验 | `engineering` | 构建、调试、工具链与项目实践 |
| 源码阅读 | `source-reading` | 实现分析、设计取舍与源码导读 |
| 问答整理 | `qa` | 有资料来源的常见问题与解答 |

栏目由 `community_category` 决定，和技术标签各有用途。[社区首页](../)展示最近发布的文章，[栏目页](../topics/)提供各栏目的独立入口。维护者补齐元数据后，文章自动出现在对应列表，无需再手写目录。

收录状态由文章所在目录决定：`incoming/` 表示初刊、开放审阅；`articles/` 表示已审阅收录。移动文章后，目录中的状态也会随之更新。

## 从初刊到收录

1. 提交草稿，说明原创、授权和资料来源。
2. 维护者做基础检查，补齐元数据后进入初刊区。
3. 根据社区讨论和技术审阅继续修订。
4. 审阅完成后进入长期收录区；适合主线教程的文章再与作者讨论整理方向。

初刊表示文章可以公开阅读和讨论。完成技术审阅后才会标记为已审阅收录。出题或投稿题解请使用[出题与题解手册](../weekly-guide/)。
