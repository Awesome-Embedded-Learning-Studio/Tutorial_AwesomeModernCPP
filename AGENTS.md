# AGENTS.md

给**任何 AI coding agent**(Claude Code / Cursor / Copilot / Codex / Windsurf …)的项目入口,人也适用。一个 vendor-neutral 文件,所有 agent 都读。Claude 专属补充另见 [CLAUDE.md](CLAUDE.md)。

## 这是什么

**Tutorial_AwesomeModernCPP** —— 从 C++ 基础到嵌入式实战的系统化 Modern C++ 教程(C++11–C++23,9 卷约 155 篇),VitePress 构建 + CMake 代码示例(支持 host / STM32F1 / STM32F4),部署 GitHub Pages。主语言中文。

```
documents/                vol1..vol9 + compilation + projects + images + community  ← 文档源
site/.vitepress/          VitePress 配置(nav / sidebar / theme / plugins)
scripts/                  build.ts(分卷并行构建)、validate_frontmatter.py …
code/volumn_codes/        各卷可编译示例(无根 CMakeLists,逐目录构建)
```

## 通用 essentials(所有 agent 必读)

- **Python 一律用 `.venv/bin/python`**(根目录 `.venv`,Python 3.14 + PyYAML)。**严禁裸 `python` / `python3`** —— 系统 python 缺 PyYAML,会让 `validate_frontmatter.py` 把全仓文件误报成 error。(Claude Code 侧有 PreToolUse hook 强制拦截。)
- **金科玉律 · 信息校验**:凡断言 C++ 行为,**先编译实测或查 cppreference 并标标准版本**。C++ 语义随版本 / 实现变化,**禁止凭记忆断言**。验证代码只写 `/tmp/`,**不要在项目目录跑 cmake / make / build**(会污染仓库)。
- **构建 / 校验**:`pnpm install` → `pnpm dev`(热更新预览)/ `pnpm build`(分卷并行);`markdownlint documents/**/*.md`;`.venv/bin/python scripts/validate_frontmatter.py`。
- **谨慎区**:`compilation/` 卷已完成,改动需特别谨慎;`vol1-fundamentals/` 正在重写,注意与已有内容衔接。

## 你来做什么?(按场景路由)

| 场景 | 去哪 |
|---|---|
| 贡献文章 / 代码示例 | [CONTRIBUTING.md](CONTRIBUTING.md) —— 流程一条龙 |
| 写作人格 / 语气 / 文章骨架 / 代码风格 | [.claude/style/writing-style.md](.claude/style/writing-style.md) |
| Frontmatter 字段与标签体系 | [.claude/rules/documents-frontmatter.md](.claude/rules/documents-frontmatter.md) |
| 辅助 C++ 学习者(学习辅助 track) | [.github/learning-with-agents.md](.github/learning-with-agents.md) + [FAQ](.github/faq.md) |
| 复用本项目 Claude 资产(commands / prompts / hooks) | [CLAUDE.md](CLAUDE.md) → `.claude/` |

> 写作流程里的两条通用纪律也适用所有 agent:**写作前先 web search 核准确性**;**完成后自查并告知如何验证**,别把检查留给作者。
