---
id: "052"
title: "内容质量自动检查 CI"
category: automation
priority: P1
status: done
created: 2026-04-15
assignee: charliechen
depends_on: ["002"]
blocks: []
estimated_effort: medium
---

# 内容质量自动检查 CI

## 目标

建立内容质量自动检查 CI 流水线，在每次 PR 和推送到 main 分支时，对所有 Markdown 文档执行全面的质量检查。检查项涵盖 frontmatter 规范、代码引用完整性、链接有效性、标签一致性、阅读时间合理性、图片引用有效性等方面。

通过自动化的质量门禁，确保新提交的内容符合项目规范，减少人工审阅负担，维持文档质量的一致性。

## 验收标准

- [ ] `.github/workflows/content-quality.yml` 工作流已创建并正常运行
- [ ] `scripts/check_quality.py` 脚本实现以下所有检查项
- [ ] **Frontmatter 检查**：所有 `.md` 文件包含必需的 frontmatter 字段（`title`, `date`），格式为有效 YAML
- [ ] **代码引用检查**：文档中引用的代码文件路径（`` ```cpp 文件引用 `` `` 或相对路径链接）实际存在
- [ ] **死链检查**：内部链接（相对路径）指向的文件存在；外部链接（http/https）返回 2xx 状态码
- [ ] **标签一致性**：frontmatter 中 `tags` 字段的标签值来自预定义标签集，无拼写错误或非标标签
- [ ] **阅读时间检查**：`reading_time` 字段与实际字数计算值偏差不超过 20%
- [ ] **图片引用检查**：`![...]()` 中的图片文件路径存在，且图片格式为支持格式（png/jpg/svg/gif/webp）
- [ ] 检查结果以清晰的表格形式输出到 GitHub Actions 日志
- [ ] 支持通过 `--strict` 模式将警告也视为错误（PR 检查用）
- [ ] 支持通过 `--fix` 模式自动修复部分问题（如自动计算 reading_time）
- [ ] 脚本可在本地运行：`python3 scripts/check_quality.py [path]`

## 实施说明

### 工作流设计

```yaml
# .github/workflows/content-quality.yml
name: Content Quality

on:
  pull_request:
    paths:
      - 'documents/**/*.md'
  push:
    branches: [main]
    paths:
      - 'documents/**/*.md'

jobs:
  quality-check:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'
      - name: Install dependencies
        run: pip install pyyaml requests
      - name: Run quality checks
        run: python3 scripts/check_quality.py documents/ --strict
```

### check_quality.py 架构

```python
import abc

class QualityChecker(abc.ABC):
    """所有检查器的基类。"""
    
    @abc.abstractmethod
    def check(self, filepath: Path) -> list[Issue]:
        """对单个文件执行检查，返回问题列表。"""
        ...

class FrontmatterChecker(QualityChecker): ...
class CodeReferenceChecker(QualityChecker): ...
class LinkChecker(QualityChecker): ...
class TagConsistencyChecker(QualityChecker): ...
class ReadingTimeChecker(QualityChecker): ...
class ImageReferenceChecker(QualityChecker): ...

def run_all_checkers(directory: Path, strict: bool = False) -> Report:
    """扫描目录下所有 .md 文件，运行全部检查器，生成报告。"""
    checkers = [
        FrontmatterChecker(),
        CodeReferenceChecker(),
        LinkChecker(),
        TagConsistencyChecker(),
        ReadingTimeChecker(),
        ImageReferenceChecker(),
    ]
    ...
```

### 各检查器详细设计

**1. FrontmatterChecker**
- 解析文件开头 `---` 之间的 YAML 内容
- 检查必需字段：`title`（非空字符串）、`date`（有效日期格式）
- 可选字段验证：`tags`（列表类型）、`reading_time`（正整数）、`description`（非空）
- 输出：缺失字段、无效值、YAML 解析错误

**2. CodeReferenceChecker**
- 匹配模式：`[引用文件](path/to/file)`、`` ```cpp --title="path" `` `、`--8<-- "path"` 等
- 检查引用路径相对于 Markdown 文件所在目录是否存在
- 忽略外部 URL（http/https）
- 输出：引用路径、文件是否存在

**3. LinkChecker**
- 内部链接：检查 `[text](relative/path)` 中路径是否存在
- 外部链接：发送 HEAD 请求（超时 5 秒），检查状态码
- 对外部链接采用批量并发检查（`asyncio` + `aiohttp`）
- 缓存外部链接检查结果，避免重复请求
- 输出：链接地址、状态码/错误信息

**4. TagConsistencyChecker**
- 从配置文件或已知文章中提取有效标签集
- 检查每篇文章的 `tags` 字段是否都在有效标签集内
- 发现新标签时输出建议（可能是拼写错误或需要新增）
- 输出：无效标签列表、建议的替代标签

**5. ReadingTimeChecker**
- 统计文件中文字符数 + 英文单词数（混合内容）
- 中文按 300 字/分钟计算，英文按 200 词/分钟
- 代码块按 50% 速度计算
- 与 frontmatter 中的 `reading_time` 对比
- 输出：声明值、计算值、偏差百分比

**6. ImageReferenceChecker**
- 匹配所有 `![alt](path)` 和 `![alt][ref]` 引用
- 检查图片文件是否存在
- 检查图片格式是否为 png/jpg/jpeg/svg/gif/webp
- 检查图片大小是否合理（警告超过 2MB 的图片）
- 输出：图片路径、是否存在、文件大小

### 报告格式

```
=== Content Quality Report ===
Files scanned: 45
Total issues: 12 (3 errors, 9 warnings)

ERRORS:
  ❌ documents/stm32/gpio.md:15: code reference not found: "code/stm32f1/gpio/main.cpp"
  ❌ documents/stm32/uart.md: frontmatter missing required field "title"
  ❌ documents/intro.md: external link broken (404): https://example.com/broken

WARNINGS:
  ⚠️ documents/stm32/spi.md: reading_time=15 but estimated=22 (偏差 32%)
  ⚠️ documents/rtos/intro.md: unknown tag "schedulor" (did you mean "scheduler"?)
  ...
```

## 涉及文件

- `.github/workflows/content-quality.yml` — 工作流定义
- `scripts/check_quality.py` — 质量检查脚本

## 参考资料

- [markdown-link-check](https://github.com/tcort/markdown-link-check) — 链接检查参考实现
- [GitHub Actions 工作流语法](https://docs.github.com/en/actions/using-workflows/workflow-syntax-for-github-actions)
- [Python yaml 模块](https://pyyaml.org/wiki/PyYAMLDocumentation)
