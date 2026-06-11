---
title: "网站迭代节奏"
description: "Tutorial_AwesomeModernCPP 的内容产出、站点维护、PR/Issue 处理和发版节奏"
chapter: 1
order: 1
tags: ["工程实践"]
---

# 网站迭代节奏

Tutorial_AwesomeModernCPP 的迭代以内容产出为主，版本号用于度量内容推进幅度。站点维护、PR 和 Issue 处理服务于主线内容，不反过来接管主线节奏。

## 基本节拍

维护者通常每 2 到 3 天进行一次轻量迭代。每轮只绑定一个主要目标：

- 完成一组相关内容。
- 修复一批影响阅读的问题。
- 补齐某个章节的代码、链接或翻译。
- 处理已经明确可行动的 PR 或 Issue。

一轮迭代不追求覆盖所有方向。卷级路线、长期候选和远期主题继续放在 `todo/`，不要把单篇文章级临时想法拆成新的治理文件。

## 单轮维护流程

每轮维护按以下顺序推进：

1. 查看当前 TODO 中的 P0/P1 目标，选定一个主要内容目标。
2. 快速检查 Issue 和 PR，只处理明确可行动、影响当前版本或阻塞读者的问题。
3. 完成本轮内容、示例代码、索引和必要的英文同步。
4. 运行与改动范围匹配的质量检查。
5. 如果变化对读者可感知，更新 changelog 或准备下一个版本条目。

PR 和 Issue 每轮至少检查一次。紧急问题可以随时插队，例如站点无法构建、重要页面 404、示例代码严重误导读者、外部贡献需要快速反馈。

## 版本节奏

版本号描述变化幅度，而不是强行驱动写作节奏。

- patch：修错、链接、站点修复、低风险文本修订。
- minor：一个卷或专题明显推进，读者能感知新的学习路径或完整能力。
- major：TODO 结构、站点架构或内容体系发生大调整。

patch 可以按需发布。minor 通常以 2 到 4 周为一个观察窗口，只有当某个专题形成完整增量时再发布。major 应保持克制，避免频繁改变读者和贡献者的入口认知。

## Tag 与 Release

tag 和 GitHub Release 分开使用。tag 用来标记轻量维护节点，让读者通过 README 徽章感知项目确实在持续变化；GitHub Release 只用于读者值得专门关注的内容型版本。

- patch 级修复可以只打 tag，不必创建 GitHub Release。
- minor 级专题推进通常应创建 Release，并配套 changelog。
- major 级结构调整必须创建 Release，并解释迁移影响。

这样可以保留项目活跃度信号，同时避免 Release 轰炸。

## 完成定义

一次内容迭代完成时，应尽量满足以下条件：

- 正文可以独立阅读，术语和标准版本标注清楚。
- 相关卷首页、章节索引或导航入口已更新。
- 文章中的示例代码可以编译，或明确说明平台和工具链限制。
- 中文和英文关键页面保持同步；社区初刊和低优先级长文可延后翻译。
- 内部链接可通过检查，生产构建可通过。

如果本轮只做局部修复，可以只运行相关检查；如果准备发版，应运行完整发布前检查。

## PR 和 Issue 处理

Issue 负责可行动问题，Discussion 负责开放式学习讨论，PR 负责具体修改。

处理优先级如下：

1. 阻塞构建、部署或主要阅读路径的问题。
2. 已有 PR 中清晰、低风险、容易合并的修复。
3. 与当前迭代主题直接相关的内容建议。
4. 可沉淀为 QA、附录或后续 TODO 的学习问题。

学习问题不直接塞进 Issue 列表；高质量讨论可以整理为 FAQ、附录或正文链接。

## Changelog 原则

changelog 应写读者可感知的变化，而不是简单罗列文件数量。

推荐记录：

- 新增或完成了哪条学习路径。
- 哪些示例现在可以运行或验证。
- 哪些站点入口、搜索、导航、社区流程得到改善。
- 哪些贡献者帮助修正了具体问题。

文件数、行数和提交数可以作为辅助数据，但不应替代变化说明。

## 常用检查

日常迭代按改动范围选择检查：

```bash
pnpm check:links
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

发版前建议运行：

```bash
pnpm check:links
pnpm build
pnpm coverage:update
python3 scripts/validate_frontmatter.py
python3 scripts/check_quality.py documents/
python3 scripts/build_examples.py --host
```

如果改动 STM32 示例，再运行：

```bash
python3 scripts/build_examples.py --stm32
```
