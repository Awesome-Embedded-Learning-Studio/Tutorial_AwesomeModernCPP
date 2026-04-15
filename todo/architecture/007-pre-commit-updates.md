---
id: 007
title: "更新 .pre-commit-config.yaml 适配新目录路径"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [002]
blocks: []
estimated_effort: small
---

# 更新 .pre-commit-config.yaml 适配新目录路径

## 目标

更新 `.pre-commit-config.yaml` 中所有引用旧目录路径的 hooks 配置，使其匹配新的 `documents/` 目录结构，确保 pre-commit hooks 在开发者提交代码时正常触发并执行检查。

## 验收标准

- [ ] `markdownlint` hook 的 `files` 正则匹配 `documents/` 目录下的 Markdown 文件
- [ ] `validate-frontmatter` hook 的 `files` 正则匹配 `documents/core-embedded-cpp/` 下的文件
- [ ] `end-of-file-fixer` 和 `trailing-whitespace` hooks 的 `files` 正则匹配 `documents/` 下的文件
- [ ] 运行 `pre-commit run --all-files` 全部通过，无错误
- [ ] 运行 `pre-commit run --hook-stage pre-commit` 模拟正常提交触发，确认 hooks 按预期过滤文件

## 实施说明

### 当前配置分析

**`.pre-commit-config.yaml` 当前内容**：

```yaml
repos:
  # Markdown linting
  - repo: https://github.com/igorshubovych/markdownlint-cli
    rev: v0.38.0
    hooks:
      - id: markdownlint
        args: ['--config', '.markdownlint.json']
        files: '^tutorial/.*\.md$'
        exclude: '^tutorial/.*\bindex\.md$'

  # Frontmatter validation (local hook)
  - repo: local
    hooks:
      - id: validate-frontmatter
        name: Validate article frontmatter
        entry: scripts/validate_frontmatter.py
        language: python
        files: '^tutorial/核心：现代嵌入式C++教程/.*\.md$'
        exclude: '(index\.md|tags\.md)$'
        pass_filenames: false

  # Check for added large files
  - repo: https://github.com/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: check-added-large-files
        args: ['--maxkb=1000']
      - id: end-of-file-fixer
      - id: trailing-whitespace
        files: '^tutorial/.*\.md$'
      - id: check-yaml
        files: '^\.github/.*\.ya?ml$'
```

### 修改详情

#### 1. markdownlint hook

```yaml
# 修改前
- id: markdownlint
  args: ['--config', '.markdownlint.json']
  files: '^tutorial/.*\.md$'
  exclude: '^tutorial/.*\bindex\.md$'

# 修改后
- id: markdownlint
  args: ['--config', '.markdownlint.json']
  files: '^documents/.*\.md$'
  exclude: '^documents/.*\bindex\.md$'
```

变更说明：
- `^tutorial/` -> `^documents/`：匹配新的文档目录
- 其余配置（args、exclude 模式逻辑）保持不变

#### 2. validate-frontmatter hook

```yaml
# 修改前
- id: validate-frontmatter
  name: Validate article frontmatter
  entry: scripts/validate_frontmatter.py
  language: python
  files: '^tutorial/核心：现代嵌入式C++教程/.*\.md$'
  exclude: '(index\.md|tags\.md)$'
  pass_filenames: false

# 修改后
- id: validate-frontmatter
  name: Validate article frontmatter
  entry: scripts/validate_frontmatter.py
  language: python
  files: '^documents/core-embedded-cpp/.*\.md$'
  exclude: '(index\.md|tags\.md)$'
  pass_filenames: false
```

变更说明：
- `^tutorial/核心：现代嵌入式C++教程/` -> `^documents/core-embedded-cpp/`：旧的中文路径更新为新的 ASCII 路径
- `entry` 路径 `scripts/validate_frontmatter.py` 不变（脚本位置未移动）
- `pass_filenames: false` 保持不变（脚本自行扫描目录）
- `exclude` 模式不需要修改，因为它匹配的是文件名而非目录路径

#### 3. trailing-whitespace hook

```yaml
# 修改前
- id: trailing-whitespace
  files: '^tutorial/.*\.md$'

# 修改后
- id: trailing-whitespace
  files: '^documents/.*\.md$'
```

变更说明：
- `^tutorial/` -> `^documents/`：匹配新的文档目录

#### 4. 不需要修改的 hooks

- `check-added-large-files`：无 `files` 过滤，检查所有暂存文件，无需修改
- `end-of-file-fixer`：无 `files` 过滤（当前配置中 `end-of-file-fixer` 没有 `files` 限制），无需修改
- `check-yaml`：`files: '^\.github/.*\.ya?ml$'` 不涉及 `tutorial/`，无需修改

### 完整修改后的配置

```yaml
repos:
  # Markdown linting
  - repo: https://github.com/igorshubovych/markdownlint-cli
    rev: v0.38.0
    hooks:
      - id: markdownlint
        args: ['--config', '.markdownlint.json']
        files: '^documents/.*\.md$'
        exclude: '^documents/.*\bindex\.md$'

  # Frontmatter validation (local hook)
  - repo: local
    hooks:
      - id: validate-frontmatter
        name: Validate article frontmatter
        entry: scripts/validate_frontmatter.py
        language: python
        files: '^documents/core-embedded-cpp/.*\.md$'
        exclude: '(index\.md|tags\.md)$'
        pass_filenames: false

  # Check for added large files
  - repo: https://github.com/pre-commit-hooks
    rev: v4.5.0
    hooks:
      - id: check-added-large-files
        args: ['--maxkb=1000']
      - id: end-of-file-fixer
      - id: trailing-whitespace
        files: '^documents/.*\.md$'
      - id: check-yaml
        files: '^\.github/.*\.ya?ml$'
```

### 验证步骤

1. **更新 pre-commit hooks**：
   ```bash
   pre-commit clean
   pre-commit autoupdate  # 可选：更新 hook 版本
   ```

2. **对所有文件运行检查**：
   ```bash
   pre-commit run --all-files
   ```
   预期结果：所有 hooks 通过（可能有少量 markdownlint 警告，取决于文件内容）。

3. **模拟提交触发**（可选）：
   ```bash
   # 修改一个 documents/ 下的文件并暂存
   echo "# test" >> documents/index.md
   git add documents/index.md
   pre-commit run
   git checkout documents/index.md  # 撤销测试修改
   ```

### 注意事项

- 如果 `validate_frontmatter.py` 脚本内部仍然引用 `tutorial/`（即 005 尚未完成），此 hook 会失败。因此 007 必须在 005 之后执行，或者与 005 同时执行。
- `markdownlint` 的 `--config .markdownlint.json` 配置文件路径是相对于仓库根目录的，不需要修改。
- 如果未来在 `documents/` 下新增了更多教程域且也需要 frontmatter 校验，可以扩展 `files` 正则或新增额外的 hook 条目。

## 涉及文件

- `.pre-commit-config.yaml`（修改 4 处 `files` 正则表达式）

## 参考资料

- 当前 `.pre-commit-config.yaml`
- [Pre-commit framework documentation](https://pre-commit.com/)
- [Pre-commit: Configuring hooks - files/exclude patterns](https://pre-commit.com/#regular-expressions)
- [igorshubovych/markdownlint-cli](https://github.com/igorshubovych/markdownlint-cli)
