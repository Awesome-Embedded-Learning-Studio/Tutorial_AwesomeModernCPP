---
id: 001
title: "创建 archive/v0-legacy 分支保留旧结构快照"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: [002]
estimated_effort: small
---

# 创建 archive/v0-legacy 分支保留旧结构快照

## 目标

在开始目录重构之前，从当前 `main` 分支创建一个名为 `archive/v0-legacy` 的分支，作为旧内容结构的永久快照。任何想要查看重构前目录布局（`tutorial/` + `codes_and_assets/` 结构）的贡献者或读者，都可以切换到该分支浏览。

这是整个架构重构的前置安全网，确保历史内容不会因为后续的大量 `git mv` 操作而难以回溯。

## 验收标准

- [ ] `archive/v0-legacy` 分支已从当前 `main` 的 HEAD 创建
- [ ] 分支已推送到远程仓库 (`origin/archive/v0-legacy`)
- [ ] 主仓库 `README.md` 中新增一段说明，指引访问者前往 `archive/v0-legacy` 查看旧版内容结构
- [ ] 在分支创建后，验证该分支上的目录结构完整（`tutorial/`、`codes_and_assets/`、`scripts/` 等目录均存在）
- [ ] GitHub 上可以通过分支选择器看到 `archive/v0-legacy`

## 实施说明

### 创建分支

```bash
# 确保当前在 main 分支且工作区干净
git checkout main
git pull origin main
git status  # 确认 clean

# 创建归档分支
git branch archive/v0-legacy

# 推送到远程
git push origin archive/v0-legacy
```

### 更新 README.md

在 `README.md` 中合适位置（建议在"项目结构"或"快速开始"章节之前）添加如下提示框：

```markdown
> **历史版本**: 如果你需要查看 2026 年 4 月重构之前的目录结构（`tutorial/` + `codes_and_assets/`），
> 请切换到 [`archive/v0-legacy`](https://github.com/Charliechen114514/Tutorial_AwesomeModernCPP/tree/archive/v0-legacy) 分支。
```

### 验证步骤

1. 在浏览器中访问 `https://github.com/Charliechen114514/Tutorial_AwesomeModernCPP/tree/archive/v0-legacy`，确认文件结构完整
2. 确认分支保护：考虑在 GitHub 仓库设置中将该分支设为 protected（禁止 force push 和删除），防止意外丢失
3. 确认本地 `git branch -r` 输出中包含 `origin/archive/v0-legacy`

### 注意事项

- 此操作不涉及任何文件移动或修改（除 README.md 的提示文本），风险极低
- 分支命名使用 `archive/` 前缀，符合常见的归档分支命名惯例
- 不要在此分支上做任何后续开发，它仅作为历史快照

## 涉及文件

- `README.md`（仅添加归档分支提示文本）
- 无其他文件变更（纯 git 操作）

## 参考资料

- [Git Branching - Branch Management](https://git-scm.com/book/en/v2/Git-Branching-Branch-Management)
- [GitHub: About protected branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches)
