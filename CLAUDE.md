# CLAUDE.md

Claude Code 在本项目的**专属补充**。通用项目信息(架构 / 命令 / 校验 / 金科玉律)见 [AGENTS.md](AGENTS.md) —— **先读它**。本文件只放 Claude 特有的。

## 写作纪律(必须遵守)

1. 写作前 **MUST** 先查 [.claude/style/writing-style.md](.claude/style/writing-style.md)(人格、语气、文章骨架、代码风格)
2. 写作前 **MUST** 先 web search,准确性优先,token 成本其次
3. 完成后 **MUST** 自查,而非留给作者;自查后告知如何验证
4. 完成一个文件夹下的文章,**MUST** 顺手建 `index.md`,确保每篇都能被 VitePress 访问
5. 计划模式用中文写计划文件
6. 审查文章准确性,用 [.claude/prompts/](.claude/prompts/) 下的 prompt:
   - `article-factcheck-audit.md` —— 事实准确性 + 引用验证(人名 / 日期 / 规格 / 代码行为)
   - `article-rigor-audit.md` —— 编译器行为 + 技术断言验证(强制编译 / 汇编佐证)

## 规划命令(skills,规划者模式 —— 只规划不执行)

- [`/patch`](.claude/commands/patch.md) —— 小迭代规划(开发原子单位,累积制)
- [`/minor`](.claude/commands/minor.md) —— 发版打包规划(攒够后手工判定触发)

确认讨论后产出【总结 + 迭代 Issue】到聊天框,套 [`.claude/templates/iteration-issue.md`](.claude/templates/iteration-issue.md)。不调 `gh`、不开 issue。

## 实用命令(skills,执行型)

- [`/audit`](.claude/commands/audit.md) —— 对一篇文章跑事实 + 严谨双审查并修正
- [`/verify-claim`](.claude/commands/verify-claim.md) —— 编译最小例子 + 查 cppreference,验证一条 C++ 断言
- [`/explain`](.claude/commands/explain.md) —— 给学习者讲一个概念(定位 → 验证 → 项目声音讲解)
- [`/new-article`](.claude/commands/new-article.md) —— 按骨架起一篇新文章
- [`/preflight`](.claude/commands/preflight.md) —— 提交前自检(frontmatter / lint / 链接 / 索引)

## 记忆系统

持久记忆在 `~/.claude/projects/-home-charliechen-Tutorial-AwesomeModernCPP/memory/`,每条一个文件 + `MEMORY.md` 索引,跨会话保留用户偏好 / 项目进展 / 踩坑。

## 添加贡献者

用户说"添加贡献者"时:① 问 GitHub URL + 贡献内容 + 是否匿名;② 同步更新 `CONTRIBUTORS.md` + `documents/team/index.md` + `documents/en/team/index.md`;③ 卡片格式(`<a class="contributor-card">`)参照 `documents/team/index.md` 现有条目。

## 配置

- [.claude/settings.json](.claude/settings.json) —— 权限 allowlist + PreToolUse hook
- [.claude/hooks/block-bare-python.sh](.claude/hooks/block-bare-python.sh) —— 强制 `.venv/bin/python`
- `.claude/settings.local.json` —— 本地覆盖(不入库)

## 参考文件索引

| 文件 | 用途 |
|---|---|
| [AGENTS.md](AGENTS.md) | 跨 agent 通用入口(架构 / 命令 / 金科玉律) |
| [.claude/style/writing-style.md](.claude/style/writing-style.md) | 写作人格 / 语气 / 骨架 / 代码风格 |
| [.claude/rules/documents-frontmatter.md](.claude/rules/documents-frontmatter.md) | Frontmatter 字段与标签 |
| [.claude/commands/](.claude/commands/) | `/patch` `/minor` 规划 skills |
| [.claude/prompts/](.claude/prompts/) | 文章审查 prompt(事实 / 严谨) |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 贡献流程一条龙 |
| `site/.vitepress/config/` | VitePress 配置(nav / sidebar / theme) |
| `scripts/build.ts` | 分卷并行构建 |
