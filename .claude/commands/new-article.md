---
description: 起一篇新文章 —— 按骨架生成 frontmatter + 章节骨架,放进对应卷目录
argument-hint: "[可选: 主题/线索;留空则交互问]"
---

# /new-article — 起新文章骨架

按项目骨架([`writing-style.md`](../style/writing-style.md) Part 1)生成一篇新文章的 frontmatter + 章节骨架。

## 输入

`$ARGUMENTS` —— 主题或线索(可选)。留空则依次问:文章类型、目标卷、主题、C++ 标准、难度。

## 流程

1. **确定类型**(四选一):通用文章 / 嵌入式平台教程 / 参考卡 / 标准库组件深入。
2. **确定位置**:目标卷目录 `documents/vol*/`,文件名 `{序号}-{kebab-case}.md`,`order` = 文件名序号。
3. **生成 frontmatter**:必填 `title` / `chapter` / `order`;`tags` **必须**来自 `scripts/validate_frontmatter.py` 的 VALID_TAGS(详见 [`documents-frontmatter.md`](../rules/documents-frontmatter.md))—— 1 个 platform + 1 个 difficulty + ≥1 个 topic。
4. **填章节骨架**:按所选类型的骨架(Part 1.2)放占位结构。
5. **提醒**:手动更新该卷 `index.md`,否则新文章在站点侧边栏不可达。

## 硬约束

- `tags` 必须来自 VALID_TAGS,否则 pre-commit / CI 校验会挂。
- `order` 与文件名序号一致。
- 生成后建议跑 `/preflight` 自检 frontmatter。
