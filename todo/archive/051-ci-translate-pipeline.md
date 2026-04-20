---
id: "051"
title: "AI 翻译脚本：本地增量翻译"
category: automation
priority: P2
status: done
created: 2026-04-15
updated: 2026-04-19
assignee: charliechen
depends_on: ["002", "084"]
blocks: ["102"]
estimated_effort: medium
---

# AI 翻译脚本：本地增量翻译

## 目标

构建本地运行的 AI 翻译脚本，由维护者手动触发，自动检测变更的中文 Markdown 文件，调用 AI API（OpenAI / Claude / DeepL）将中文内容翻译为英文，生成 `.en.md` 文件。翻译完成后由维护者自行提交和审核。

> **不使用 CI/CD 流水线**：翻译内容在 CI 中运行存在泄露风险（日志、第三方服务等），改为纯本地脚本方案。API key 保存在本地环境变量或 `.env` 文件中，不进入仓库。

核心要求：
- **本地运行**：所有翻译操作在本地执行，不依赖 CI 环境，避免内容泄露
- **手动触发**：由维护者主动运行，支持翻译指定文件或自动检测变更文件
- **增量翻译**：仅翻译有变更的文件，避免全量翻译导致的高成本和长耗时
- **上下文感知**：翻译时提供术语表和写作风格指南，确保翻译质量
- **幂等性**：同一文件的翻译可以安全地重复执行

## 验收标准

- [ ] `scripts/translate.py` 脚本可在本地通过 `.venv` 环境直接运行
- [ ] 支持 `--changed` 模式：自动检测相对于 main 分支有变更的 `.md` 文件（排除 `.en.md`）
- [ ] 支持 `--file <path>` 模式：翻译单个指定文件
- [ ] 支持 `--all` 模式：翻译所有中文 `.md` 文件（全量，需二次确认）
- [ ] 翻译结果保存为对应的 `.en.md` 文件（如 `article.md` → `article.en.md`）
- [ ] 代码块、表格、图片引用等特殊内容正确保留不翻译
- [ ] frontmatter 正确处理（翻译 title/description，保留其他字段）
- [ ] API key 从环境变量或 `.env` 文件读取，脚本提示缺失时给出配置说明
- [ ] 包含费用估算和 `--dry-run` 预估模式
- [ ] 包含 token 调用限制保护机制（单次运行上限可配置）

## 实施说明

### 使用方式

```bash
# 翻译单个文件
python3 scripts/translate.py --file documents/vol1-fundamentals/01-introduction.md

# 翻译相对 main 分支所有变更的中文 .md 文件
python3 scripts/translate.py --changed

# 翻译所有中文 .md 文件（会有二次确认提示）
python3/ scripts/translate.py --all

# 预估翻译费用，不实际调用 API
python3 scripts/translate.py --changed --dry-run

# 指定翻译引擎
python3 scripts/translate.py --changed --engine openai
```

### 变更检测逻辑

```bash
# --changed 模式：对比当前分支与 main 的差异
git diff --name-only --diff-filter=ACM main...HEAD -- 'documents/**/*.md' ':!documents/**/*.en.md'
```

### API Key 管理

```bash
# 方式 1：环境变量
export TRANSLATE_API_KEY="sk-..."
python3 scripts/translate.py --changed

# 方式 2：项目根目录 .env 文件（已在 .gitignore 中）
echo "TRANSLATE_API_KEY=sk-..." >> .env
python3 scripts/translate.py --changed
```

`.gitignore` 中已包含 `.env`，确保 key 不会被提交到仓库。

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

- `scripts/translate.py` — 翻译核心脚本（唯一新增文件）

## 参考资料

- [OpenAI API 文档](https://platform.openai.com/docs/api-reference/chat)
- [Anthropic API 文档](https://docs.anthropometric.com/en/docs/api-reference)
- [mkdocs-static-i18n 文件布局](https://ultrabug.github.io/mkdocs-static-i18n/)
