---
id: "101"
title: "翻译工作流设计：增量翻译与人工审核"
category: translation
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["100"]
blocks: []
estimated_effort: medium
---

# 翻译工作流设计：增量翻译与人工审核

## 目标

设计完整的翻译工作流，定义从源文件变更到翻译完成的端到端流程。重点关注：

1. **增量翻译策略**：仅翻译变更文件，避免全量翻译
2. **双语文件布局**：确定 `.en.md` 后缀方案的文件组织
3. **人工审核流程**：翻译 PR 的创建、审核、合并流程
4. **翻译记忆库**：维护已翻译内容的缓存，避免重复翻译未变更部分

## 验收标准

- [ ] 增量翻译策略已设计：基于 `git diff` 检测变更文件，仅翻译变更部分
- [ ] 双语文件布局方案已确定：使用 `.en.md` 后缀方案（与 mkdocs-static-i18n 兼容）
- [ ] 翻译 PR 创建流程已定义：自动创建分支、提交翻译、创建 PR
- [ ] 人工审核流程已文档化：审核标准、审核清单、反馈模板
- [ ] 翻译记忆库设计已完成：缓存机制、键值设计（文件路径 + 内容 hash → 翻译结果）
- [ ] `.github/workflows/translate.yml` 工作流逻辑已设计（可与 automation/051 协同实现）
- [ ] 翻译冲突处理策略已定义（当多人同时修改同一文件的翻译时）
- [ ] 翻译回滚机制已设计（质量不合格时可快速回退）

## 实施说明

### 增量翻译策略

**变更检测逻辑**：

```bash
# 检测 main 分支上变更的中文 .md 文件
git diff --name-only --diff-filter=ACM HEAD~1 HEAD -- 'documents/**/*.md' ':!documents/**/*.en.md'
```

**变更类型处理**：

| 变更类型 | 处理方式 |
|----------|----------|
| 新增文件 (A) | 翻译整个文件 |
| 修改文件 (M) | 翻译整个文件（覆盖已有翻译） |
| 重命名文件 (R) | 翻译新文件，删除旧翻译 |
| 删除文件 (D) | 删除对应翻译文件 |

**部分翻译优化（未来改进）**：
- 对比文件 diff，仅提取变更段落
- 翻译变更段落，合并到已有翻译中
- 当前阶段先做全文件翻译（简单可靠），后续优化

### 双语文件布局

**文件对应关系**：

```
documents/
├── theory/
│   ├── cpp-basics.md           # 中文源文件
│   ├── cpp-basics.en.md        # 英文翻译
│   ├── memory.md               # 中文
│   └── memory.en.md            # 英文（可能不存在 = 未翻译）
```

**文件命名规则**：
- 中文文件：`{name}.md`
- 英文翻译：`{name}.en.md`
- 翻译文件必须与源文件在同一目录

**元数据追踪**：
在翻译文件头部添加翻译元信息注释：

```markdown
---
title: "English Title"
description: "English description"
date: 2026-04-15
tags:
  - stm32f1
  - gpio
  - beginner
difficulty: beginner
reading_time: 20
translation:
  source: "documents/stm32/gpio.md"
  source_hash: "abc123..."     # 源文件内容 hash
  translated_at: "2026-04-15"
  engine: "openai"
---
```

### 翻译 PR 流程

```
main 分支推送（变更中文 .md）
    │
    ▼
检测变更文件列表
    │
    ▼
对每个文件执行翻译
    │
    ▼
创建翻译分支 i18n/auto-YYYY-MM-DD
    │
    ▼
提交翻译文件到分支
    │
    ▼
创建翻译 PR
    │
    ▼
人工审核
    │
    ├── 通过 → 合并到 main
    ├── 需修改 → 评论反馈 → 更新翻译
    └── 拒绝 → 关闭 PR
```

**PR 标题格式**：`i18n: auto-translate YYYY-MM-DD (N files)`

**PR 正文模板**：

```markdown
## 自动翻译 PR

### 翻译文件列表
| 源文件 | 翻译文件 | 状态 |
|--------|----------|------|
| documents/stm32/gpio.md | documents/stm32/gpio.en.md | 新增 |

### 翻译引擎
- 引擎：OpenAI GPT-4o
- Token 消耗：约 5,000 tokens

### 审核要点
- [ ] 术语翻译是否准确
- [ ] 代码块是否正确保留
- [ ] 链接和图片引用是否正确
- [ ] 语言是否流畅自然
```

### 翻译记忆库设计

```python
class TranslationMemory:
    """翻译记忆库：缓存已翻译内容，避免重复翻译。"""
    
    def __init__(self, cache_dir: Path):
        self.cache_dir = cache_dir
    
    def get_cache_key(self, source_path: str, content: str) -> str:
        """生成缓存键：文件路径 + 内容 hash。"""
        content_hash = hashlib.sha256(content.encode()).hexdigest()[:16]
        return f"{source_path}:{content_hash}"
    
    def get(self, key: str) -> Optional[str]:
        """获取缓存的翻译结果。"""
        cache_file = self.cache_dir / f"{key}.en.md"
        if cache_file.exists():
            return cache_file.read_text()
        return None
    
    def put(self, key: str, translation: str) -> None:
        """存储翻译结果到缓存。"""
        cache_file = self.cache_dir / f"{key}.en.md"
        cache_file.write_text(translation)
    
    def is_up_to_date(self, source_path: str, translation_path: Path) -> bool:
        """检查翻译是否是最新的。"""
        if not translation_path.exists():
            return False
        # 读取翻译文件中的 source_hash
        # 与源文件当前 hash 对比
        ...
```

**缓存存储位置**：`.cache/translations/`（gitignore）

### 冲突处理

- 翻译 PR 始终从最新的 main 创建
- 如果多个翻译 PR 同时存在，按创建时间顺序合并
- 后合并的 PR 需要 rebase 解决冲突
- 翻译工作流不会在同一文件上并发运行（使用 concurrency group）

```yaml
# GitHub Actions 并发控制
concurrency:
  group: translation-${{ github.ref }}
  cancel-in-progress: false  # 不取消，排队等待
```

### 回滚机制

- 翻译质量不合格时，可以直接 revert 翻译 PR
- 翻译文件的 `translation.source_hash` 可用于验证是否与源文件匹配
- 支持通过 `--force-retranslate` 参数强制重新翻译

## 涉及文件

- `.github/workflows/translate.yml` — 翻译工作流定义
- `.cache/translations/` — 翻译缓存目录（gitignore）

## 参考资料

- [mkdocs-static-i18n 后缀方案](https://ultrabug.github.io/mkdocs-static-i18n/setup/using-suffixes/)
- [GitHub Actions 并发控制](https://docs.github.com/en/actions/using-workflows/workflow-syntax-for-github-actions#concurrency)
- [翻译记忆库原理](https://en.wikipedia.org/wiki/Translation_memory)
