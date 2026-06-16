---
description: 提交前自检 —— frontmatter 校验 + markdownlint + 链接/索引检查,汇总 pass/fail
argument-hint: "[可选: 目标文件或目录;留空则检查已暂存文件]"
---

# /preflight — 提交前自检

跑一遍 PR 前该过的检查,汇总通过项和待修项。

## 输入

`$ARGUMENTS` —— 目标文件 / 目录(可选)。留空则用 `git diff --cached --name-only` 取已暂存文件。

## 流程

1. **frontmatter 校验**:`.venv/bin/python scripts/validate_frontmatter.py`(**必须用 venv**,系统 python 缺 PyYAML 会全量误报)。
2. **markdownlint**:`markdownlint <目标 .md 文件>`。
3. **内部链接**:运行项目的 link check(若有,如 CI 用的脚本),或人工抽检新增 / 改动链接是否可达、`ChapterLink` 的 `href` 不以 `.md` 结尾等。
4. **索引检查**:若新增或移动了文章,对应卷 `index.md` 是否已更新链接。
5. **代码示例**(若涉及 `code/`):提醒确认可独立编译(无根 CMakeLists,逐目录构建)。

## 输出

```
## Preflight 报告
- [ ] frontmatter: pass / fail（细节）
- [ ] markdownlint: pass / fail
- [ ] 内部链接: pass / 待检
- [ ] 索引更新: 已更新 / 待补
- [ ] 代码示例: 不涉及 / 待编译确认
待修清单：...
```

## 硬约束

- Python 一律 `.venv/bin/python`(有 PreToolUse hook 强制)。
- 只读检查,不改文件(发现问题列出来,让用户决定怎么改)。
