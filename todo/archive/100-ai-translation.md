---
id: "100"
title: "AI 翻译架构设计：引擎评估与 Prompt 工程"
category: translation
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: ["051", "101"]
estimated_effort: large
---

# AI 翻译架构设计：引擎评估与 Prompt 工程

## 目标

完成 AI 翻译系统的架构设计阶段，包括翻译引擎评估、system prompt 设计、特殊内容处理策略、翻译质量评估机制。此 TODO 的产出将直接用于 automation/051 (CI 翻译流水线) 的实现。

核心任务：
1. 评估并选择最佳翻译引擎（OpenAI GPT-4 / Claude / DeepL）
2. 设计针对技术文档翻译的 system prompt
3. 设计代码块、表格、图片等特殊内容的处理策略
4. 建立翻译质量评估机制

## 验收标准

- [ ] 翻译引擎评估报告已完成，对比至少 3 个引擎在技术文档翻译上的表现
- [ ] 评估维度包括：翻译质量、成本、速度、API 稳定性、中文→英文能力
- [ ] 最终选定引擎并记录理由
- [ ] System prompt 已设计并测试，至少翻译 5 篇不同类型的文章进行验证
- [ ] System prompt 基于 `.claude/writting_style.md` 中的风格要求，转译为英文写作风格
- [ ] 特殊内容处理策略已定义并通过测试：
  - 代码块保留不翻译
  - 表格格式保持，内容翻译
  - 图片引用保留路径，翻译 alt 文本
  - frontmatter 仅翻译 title/description
  - Mermaid 图表保留不翻译
- [ ] 翻译质量评估机制已设计（BLEU 分数 / 人工评审 / 差异对比）
- [ ] `scripts/translate.py` 核心翻译模块已实现（可本地测试）

## 实施说明

### 翻译引擎评估

**评估矩阵**：

| 维度 | OpenAI GPT-4o | Claude Sonnet | DeepL API |
|------|---------------|---------------|-----------|
| 翻译质量 | ? | ? | ? |
| 技术术语准确性 | ? | ? | ? |
| 代码块处理 | ? | ? | ? |
| 格式保持 | ? | ? | ? |
| 成本（$/1K tokens） | $2.50/$10 | $3/$15 | $5.49/M chars |
| 速度 | ? | ? | ? |
| 上下文窗口 | 128K | 200K | N/A |
| API 稳定性 | 高 | 高 | 高 |

**评估方法**：
1. 准备 5 篇测试文章（不同类型：理论、平台教程、RTOS、API 参考）
2. 使用相同 prompt 分别用 3 个引擎翻译
3. 人工评审翻译质量（准确性、流畅度、术语一致性）
4. 综合评分选择最佳引擎

**预期选择**：Claude Sonnet 或 GPT-4o（DeepL 不擅长处理含代码的技术文档）

### System Prompt 设计

```python
TRANSLATION_SYSTEM_PROMPT = """You are a professional technical translator specializing in modern C++ 
and embedded systems development. Your task is to translate Chinese Markdown documentation 
into natural, idiomatic English.

## Context
This is a tutorial project teaching modern C++ for embedded systems (STM32, ESP32, RP2040). 
The content ranges from C++ language features to hardware peripheral programming and RTOS concepts.

## Translation Rules

### General
- Translate naturally, not literally. Prioritize readability for English-speaking developers.
- Maintain the exact Markdown structure and formatting.
- Use precise technical terminology consistently.

### Terminology
- Use the established English terms from the project's terminology reference.
- When introducing a term for the first time, format as: English Term (中文原文)

### Code Blocks
- Do NOT translate code inside ``` code blocks ```.
- Do NOT translate inline code `like this`.
- Translate code comments only if they contain Chinese text.
- Preserve the language specifier (```cpp, ```python, etc.).

### Tables
- Translate cell content, preserve table structure.
- Keep numeric values unchanged.

### Images and Links
- In ![alt text](url), translate alt text but keep the URL unchanged.
- In [link text](url), translate link text but keep the URL unchanged.

### Admonitions
- Translate admonition titles and content.
- Keep admonition types (note, warning, tip, etc.) unchanged.

### Frontmatter (YAML between ---)
- Translate ONLY the 'title' and 'description' fields.
- Keep all other fields (date, tags, difficulty, etc.) unchanged.

### Mermaid Diagrams
- Do NOT translate Mermaid diagram code blocks.
- Keep them exactly as they are.

### Style Guide
- Use active voice.
- Use "we" instead of "you" for a collaborative tone.
- Keep sentences concise.
- Use serial comma (Oxford comma).
- Spell out numbers below 10 (one, two, three).
"""
```

### 特殊内容处理架构

```python
import re
from dataclasses import dataclass
from typing import Optional

@dataclass
class PreservedBlock:
    """需要保留不翻译的内容块。"""
    placeholder: str
    original: str
    block_type: str  # 'code', 'mermaid', 'math', 'image_url'

class ContentPreprocessor:
    """预处理器：提取需要保留的内容，替换为占位符。"""
    
    def extract_code_blocks(self, text: str) -> tuple[str, list[PreservedBlock]]:
        """提取 ```...``` 代码块。"""
        ...
    
    def extract_inline_code(self, text: str) -> tuple[str, list[PreservedBlock]]:
        """提取 `...` 行内代码。"""
        ...
    
    def extract_image_urls(self, text: str) -> tuple[str, list[PreservedBlock]]:
        """提取图片 URL。"""
        ...
    
    def extract_mermaid_blocks(self, text: str) -> tuple[str, list[PreservedBlock]]:
        """提取 Mermaid 图表。"""
        ...

class FrontmatterTranslator:
    """翻译 frontmatter 中的特定字段。"""
    
    TRANSLATABLE_FIELDS = {'title', 'description'}
    
    def translate(self, fm: dict, source: str) -> dict:
        """仅翻译 title 和 description 字段。"""
        ...
```

### 翻译质量评估机制

**自动化评估**：
- 使用 difflib 对比翻译结果的结构保持度
- 检查 Markdown 标记完整性（链接、图片、表格语法）
- 检查代码块是否被翻译（不应翻译）
- 统计术语一致性（同一中文词是否翻译为同一英文词）

**人工评审**：
- 随机抽样 10% 翻译结果进行人工评审
- 评审维度：准确性 (1-5)、流畅度 (1-5)、术语一致性 (1-5)
- 评审结果记录用于优化 prompt

**A/B 测试**：
- 对同一文章使用不同 prompt 版本翻译
- 比较翻译质量得分
- 持续迭代优化 prompt

### 本地测试流程

```bash
# 测试单文件翻译
python3 scripts/translate.py --file documents/stm32/gpio.md --dry-run

# 测试批量翻译
python3 scripts/translate.py --directory documents/ --limit 5 --dry-run

# 指定引擎
python3 scripts/translate.py --file documents/stm32/gpio.md --engine openai
python3 scripts/translate.py --file documents/stm32/gpio.md --engine claude
python3 scripts/translate.py --file documents/stm32/gpio.md --engine deepl
```

## 涉及文件

- `scripts/translate.py` — 翻译核心模块（引擎接口、prompt 管理、内容预处理）

## 参考资料

- [OpenAI Chat API](https://platform.openai.com/docs/api-reference/chat)
- [Anthropic Messages API](https://docs.anthropic.com/en/api/messages)
- [DeepL API 文档](https://www.deepl.com/pro-api)
- [BLEU Score 评估](https://en.wikipedia.org/wiki/BLEU)
