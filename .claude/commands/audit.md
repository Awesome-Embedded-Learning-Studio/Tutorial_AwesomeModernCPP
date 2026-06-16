---
description: 对一篇文章跑「事实核查 + 严谨性」双审查并产出修正 —— 复用两个公开审查 prompt 的方法论
argument-hint: "[必填: 待审查文章的相对/绝对路径]"
---

# /audit — 文章双审查(事实 + 严谨)

你是**执行者**:对目标文章做两轮审查,**直接修改文章并产出报告**。

## 输入

`$ARGUMENTS` —— 待审查文章路径(必填)。为空则停下问用户要路径。

## 方法论来源

- **事实核查**:[`article-factcheck-audit.md`](../prompts/article-factcheck-audit.md)(人名 / 日期 / 规格 / 引用 / 代码声称行为)
- **严谨性核查**:[`article-rigor-audit.md`](../prompts/article-rigor-audit.md)(无证据的"编译器会优化 / 更快"类断言)

严格按这两个 prompt 的流程和约束执行,本命令只是把它们串成一次调用。

## 流程

1. **读全文**,通读目标文章。
2. **事实轮**:标记需验证声明 → web search 查权威源 → 按 P0–P3 分级。
3. **严谨轮**:标记无证据的技术 / 性能断言 → 编译验证(见硬约束)。
4. **修正**:按 factcheck 的三要素(原文摘录 / RefLink / 验证路径)插入 `:::warning` / `:::details`;rigor 轮用真实汇编 / 输出替换空口描述。验证代码写入 `code/volumn_codes/vol{N}/`。
5. **报告**:输出修正汇总表 + 新增引用 + 新增验证代码 + 验证清单。

## 硬约束(行为纪律)

- **编译只到 `/tmp/`**:`g++ -std=c++XX -O{0,2} -o /tmp/xxx /tmp/xxx.cpp`,运行 `/tmp/xxx` —— **分两次独立 Bash 调用,禁止 `&&` 串联**。
- **禁止**在项目目录跑 cmake / make / ninja 或建 build 目录。
- **禁止**把报告 / 日志写到 `/tmp/` 或任何位置 —— 报告直接作为返回文本输出。
- 只修改目标文章 + 对应 `code/volumn_codes/` 目录。
- 写作风格遵循 [`writing-style.md`](../style/writing-style.md)。
