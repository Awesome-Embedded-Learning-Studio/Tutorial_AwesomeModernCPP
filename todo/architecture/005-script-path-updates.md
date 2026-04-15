---
id: 005
title: "更新脚本中的目录路径引用"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [002]
blocks: []
estimated_effort: small
---

# 更新脚本中的目录路径引用

## 目标

更新 `scripts/` 目录下所有 Python 和 Shell 脚本中硬编码的目录路径，使其从旧的 `tutorial/` 和 `codes_and_assets/` 引用指向新的 `documents/` 和 `code/` 路径。确保所有脚本工具链在新的目录结构下正常工作。

## 验收标准

- [ ] `scripts/validate_frontmatter.py` 使用 `documents/` 作为扫描目录，运行成功
- [ ] `scripts/check_links.py` 使用 `documents/` 作为链接检查根目录，运行成功并报告 0 个 broken link
- [ ] `scripts/analyze_frontmatter.py` 使用 `documents/` 作为分析目录，运行成功并输出正确的统计报告
- [ ] `scripts/mkdocs_dev.sh` 的 serve、build、clean 命令在新的 `documents/` 配置下正常工作
- [ ] 所有脚本中不再包含字符串 `tutorial/` 或 `codes_and_assets/` 的路径引用（注释中提及旧名称用于说明历史是可以的）

## 实施说明

### 1. validate_frontmatter.py

**当前代码** (`scripts/validate_frontmatter.py` 第 229-233 行)：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'tutorial'
```

**修改方案**：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    docs_dir = project_root / 'documents'  # 更新：tutorial -> documents
```

同时需要将整个文件中的 `tutorial_dir` 变量重命名为 `docs_dir`（涉及 `FrontmatterValidator.__init__` 参数和所有引用处）。具体修改点：

- 第 63 行 `__init__` 方法签名：`self.tutorial_dir` -> `self.docs_dir`
- 第 177 行 `self.tutorial_dir.rglob` -> `self.docs_dir.rglob`
- 第 253 行的 `tutorial_dir` 局部变量
- 变量名 `tutorial_dir` 全部替换为 `docs_dir`（约 5 处）

### 2. check_links.py

**当前代码** (`scripts/check_links.py` 第 250-253 行)：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'tutorial'
```

**修改方案**：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    docs_dir = project_root / 'documents'  # 更新：tutorial -> documents
```

`LinkChecker` 类中所有引用 `tutorial_dir` 的地方也需要更新。具体修改点：

- 第 31 行 `__init__` 方法签名：`tutorial_dir` -> `docs_dir`
- `self.tutorial_dir` -> `self.docs_dir`（约 12 处：build_file_index、normalize_path、check_file、print_orphaned_files 等方法）
- 类文档字符串中的 "tutorial" 描述更新
- `main()` 函数中的 `tutorial_dir` 变量

此外，`check_links.py` 的 `SKIP_FILES` 和路径规范化逻辑不依赖目录名本身，只依赖相对路径解析，因此核心逻辑不需要修改。

### 3. analyze_frontmatter.py

**当前代码** (`scripts/analyze_frontmatter.py` 第 185-188 行)：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'tutorial'
```

**修改方案**：

```python
def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    docs_dir = project_root / 'documents'  # 更新：tutorial -> documents
```

同样需要更新 `FrontmatterAnalyzer.__init__` 参数和所有 `tutorial_dir` 引用。具体修改点：

- 第 35 行 `__init__`：`self.tutorial_dir` -> `self.docs_dir`
- 第 90 行 `self.tutorial_dir.rglob` -> `self.docs_dir.rglob`
- 第 97 行 `filepath.relative_to(self.tutorial_dir)` -> `filepath.relative_to(self.docs_dir)`
- `main()` 函数中的变量名

### 4. mkdocs_dev.sh

**当前代码** (`scripts/mkdocs_dev.sh`)：

该脚本本身没有硬编码 `tutorial/` 路径。它通过 `mkdocs serve` 和 `mkdocs build` 命令间接使用 `mkdocs.yml` 中的 `docs_dir` 配置。因此，只要 `mkdocs.yml` 的 `docs_dir` 已更新（见 003），此脚本不需要修改。

但需要确认以下几点：

1. `cmd_clean()` 中清理 `$SITE_DIR` 时，`site/` 目录不受影响
2. `cmd_build()` 中的 `mkdocs build --clean` 会自动读取新的 `docs_dir`
3. 依赖安装（`pip install -e ./scripts`）不涉及路径问题

**结论**：`mkdocs_dev.sh` 不需要修改。

### 5. 其他脚本

- `scripts/remove_manual_nav.py` - 需检查是否引用了 `tutorial/` 路径
- `scripts/setup_precommit.sh` - 需检查是否引用了旧路径
- `scripts/pyproject.toml` - 这是依赖配置文件，不包含路径引用，无需修改

### 执行策略

建议一次性修改所有 Python 脚本，使用全局查找替换：

```bash
# 在 scripts/ 目录下查找所有 tutorial_dir 引用
grep -rn "tutorial_dir" scripts/*.py
grep -rn "'tutorial'" scripts/*.py
grep -rn "codes_and_assets" scripts/*.py
```

对于 Python 文件中的变量重命名，可以使用 IDE 的重构功能（Rename Symbol）确保所有引用点都被更新，避免遗漏。

### 测试命令

```bash
cd /home/charliechen/Tutorial_AwesomeModernCPP

# 验证 frontmatter 校验
python scripts/validate_frontmatter.py

# 验证链接检查
python scripts/check_links.py

# 验证 frontmatter 分析
python scripts/analyze_frontmatter.py

# 验证开发服务器
./scripts/mkdocs_dev.sh build
```

## 涉及文件

- `scripts/validate_frontmatter.py`（更新 `tutorial_dir` -> `docs_dir`，约 5 处）
- `scripts/check_links.py`（更新 `tutorial_dir` -> `docs_dir`，约 12 处）
- `scripts/analyze_frontmatter.py`（更新 `tutorial_dir` -> `docs_dir`，约 5 处）
- `scripts/remove_manual_nav.py`（待检查）
- `scripts/setup_precommit.sh`（待检查）

## 参考资料

- 各脚本的源代码（`scripts/` 目录）
- Python `pathlib.Path` 文档 - 用于理解路径操作
