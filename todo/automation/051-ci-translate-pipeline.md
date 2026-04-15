---
id: "051"
title: "AI 翻译流水线：增量翻译与人工审核"
category: automation
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["002", "084"]
blocks: ["102"]
estimated_effort: large
---

# AI 翻译流水线：增量翻译与人工审核

## 目标

构建完整的 AI 翻译 CI/CD 流水线，在推送到 main 分支时自动检测变更的中文 Markdown 文件，调用 AI API（OpenAI / Claude / DeepL）将中文内容翻译为英文，生成 `.en.md` 文件，并自动创建翻译 PR 供人工审核。

核心要求：
- **增量翻译**：仅翻译本次推送中变更的文件，避免全量翻译导致的高成本和长耗时
- **上下文感知**：翻译时提供术语表和写作风格指南，确保翻译质量
- **人工审核**：翻译结果通过 PR 提交，由维护者审核后合并
- **幂等性**：同一文件的翻译可以安全地重复执行

## 验收标准

- [ ] `.github/workflows/translate.yml` 工作流在 main 分支推送时触发
- [ ] 工作流能正确检测本次推送中变更的 `.md` 文件（排除 `.en.md` 和其他非中文文件）
- [ ] `scripts/translate.py` 脚本调用 AI API 将中文翻译为英文
- [ ] 翻译结果保存为对应的 `.en.md` 文件（如 `article.md` → `article.en.md`）
- [ ] 自动创建翻译 PR，标题格式：`i18n: auto-translate YYYY-MM-DD`
- [ ] PR 中列出所有翻译的文件及变更摘要
- [ ] 支持增量翻译：仅处理 `git diff` 中变更的文件
- [ ] 翻译 API key 通过 GitHub Secrets 安全存储
- [ ] 代码块、表格、图片引用等特殊内容正确保留不翻译
- [ ] frontmatter 正确处理（翻译 title/description，保留其他字段）
- [ ] 本地可通过 `python3 scripts/translate.py --file path/to/article.md` 测试单个文件翻译
- [ ] 包含费用估算和 API 调用限制保护机制

## 实施说明

### 工作流架构

```yaml
# .github/workflows/translate.yml
name: AI Translate

on:
  push:
    branches: [main]
    paths:
      - 'documents/**/*.md'
      - '!documents/**/*.en.md'

jobs:
  detect-and-translate:
    runs-on: ubuntu-latest
    permissions:
      contents: write
      pull-requests: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 2  # 需要 diff HEAD~1
      - name: Detect changed files
        id: changes
        run: |
          # 获取变更的中文 .md 文件列表
          CHANGED=$(git diff --name-only --diff-filter=ACM HEAD~1 HEAD -- 'documents/**/*.md' ':!documents/**/*.en.md')
          echo "files=$CHANGED" >> "$GITHUB_OUTPUT"
      - name: Translate
        if: steps.changes.outputs.files != ''
        run: python3 scripts/translate.py --files ${{ steps.changes.outputs.files }}
        env:
          TRANSLATE_API_KEY: ${{ secrets.TRANSLATE_API_KEY }}
      - name: Create Translation PR
        if: steps.changes.outputs.files != ''
        run: |
          # 创建分支，提交翻译文件，创建 PR
```

### translate.py 脚本设计

核心模块：

```python
class TranslationPipeline:
    def __init__(self, api_key: str, engine: str = "openai"):
        self.engine = engine
        self.client = self._init_client(api_key)
    
    def translate_file(self, input_path: Path, output_path: Path) -> None:
        """翻译单个 Markdown 文件。"""
        content = input_path.read_text(encoding='utf-8')
        frontmatter, body = self._split_frontmatter(content)
        
        # 翻译 frontmatter 中的 title/description
        translated_fm = self._translate_frontmatter(frontmatter)
        
        # 提取代码块、表格等不需要翻译的内容
        body, placeholders = self._extract_preserved_blocks(body)
        
        # 翻译正文
        translated_body = self._translate_text(body)
        
        # 还原保留的内容
        translated_body = self._restore_preserved_blocks(translated_body, placeholders)
        
        # 写入输出文件
        output_path.write_text(translated_fm + translated_body, encoding='utf-8')
```

### 特殊内容处理策略

1. **代码块** `` ```...``` ``：完全保留，不翻译代码和注释
2. **表格**：翻译表格中的文本内容，保留 Markdown 表格格式
3. **图片引用** `![alt](url)`：保留 URL，翻译 alt 文本
4. **链接** `[text](url)`：翻译 text，保留 url
5. **Admonition** `!!! note "标题"`：翻译标题和内容
6. **Frontmatter**：仅翻译 `title` 和 `description` 字段
7. **Mermaid 代码块**：保留不翻译

### 翻译 Prompt 设计

```python
SYSTEM_PROMPT = """You are a professional technical translator specializing in C++ and embedded systems.
Translate the following Chinese Markdown content to English.

Guidelines:
- Maintain the exact Markdown structure and formatting
- Use precise technical terminology (see terminology reference)
- Keep code blocks, URLs, and image paths unchanged
- Translate naturally, not literally
- Follow the writing style guide for English content

Terminology reference:
{terminology}

Writing style guide:
{style_guide}
"""
```

### 费用控制

- 设置单次翻译最大 token 数（建议 100K tokens/次运行）
- 单文件超过 10K tokens 时分块翻译
- 记录每次翻译的 token 消耗到日志
- 支持通过 `--dry-run` 模式预估费用

## 涉及文件

- `.github/workflows/translate.yml` — 翻译工作流定义
- `scripts/translate.py` — 翻译核心脚本

## 参考资料

- [OpenAI API 文档](https://platform.openai.com/docs/api-reference/chat)
- [Anthropic API 文档](https://docs.anthropic.com/en/docs/api-reference)
- [GitHub Actions PR 创建](https://docs.github.com/en/actions/using-workflows/triggering-a-workflow)
- [mkdocs-static-i18n 文件布局](https://ultrabug.github.io/mkdocs-static-i18n/)
