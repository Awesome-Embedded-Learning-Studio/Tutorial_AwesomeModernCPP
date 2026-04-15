---
id: 004
title: "迁移所有内部 Markdown 链接"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [002]
blocks: []
estimated_effort: large
---

# 迁移所有内部 Markdown 链接

## 目标

在目录迁移（002）完成后，更新 `documents/` 目录下所有 Markdown 文件中的内部链接，使其指向新的路径。这包括文章之间的交叉引用、代码示例路径引用、以及图片资源路径。这是保证站点可用的关键步骤。

## 验收标准

- [ ] 所有 Markdown 文件中的内部链接（`[text](relative-path)`）已更新为新路径
- [ ] 所有图片引用（`![alt](image-path)`）指向正确的新位置
- [ ] `check_links.py` 脚本通过，报告 0 个 broken link 和 0 个 broken image
- [ ] `mkdocs build` 构建成功，无任何警告或错误
- [ ] 手动抽查至少 10 个跨章节链接，确认点击后跳转正确
- [ ] 代码示例引用路径（如指向 `code/examples/` 的路径）已更新

## 实施说明

### 链接类型分析

目录迁移后需要更新以下几类链接：

#### 1. 跨章节交叉引用

核心教程章节之间可能存在相互引用。例如 Chapter3 的文章可能引用 Chapter1 的内容：

```markdown
<!-- 修改前 -->
[第1章：设计与约束](../Chapter1/some-article.md)

<!-- 修改后 -->
[第1章：设计与约束](../chapter-01-design-constraints/some-article.md)
```

由于目录层级未变（都在 `documents/core-embedded-cpp/` 下），相对路径的 `../` 层次不需要调整，只需更新目标目录名。

#### 2. 代码示例引用

文档中可能引用 `codes_and_assets/` 下的代码文件。例如：

```markdown
<!-- 修改前 -->
[完整代码](../../codes_and_assets/examples/chapter02/01_zero_overhead/main.cpp)

<!-- 修改后 -->
[完整代码](../../code/examples/chapter02/01_zero_overhead/main.cpp)
```

注意：`codes_and_assets/` 不在 `documents/` 内部，这些引用需要从 `documents/` 目录出发解析相对路径。迁移后 `code/` 和 `documents/` 同级，相对路径需要重新计算。

#### 3. 图片资源引用

文章中引用的图片可能在同级目录或 `stylesheets/` 等公共目录下：

```markdown
<!-- 修改前 -->
![示意图](../images/some-diagram.png)

<!-- 修改后（如果图片位置不变） -->
![示意图](../images/some-diagram.png)
```

如果图片随目录一起迁移，则相对路径通常不需要变化。

#### 4. 导航链接（index.md 中的链接）

`documents/index.md` 首页可能包含指向各教程域的链接，需要全部更新。

### 自动化迁移方案

建议编写一个 Python 脚本 `scripts/migrate_links.py`（一次性使用），自动批量更新链接：

```python
#!/usr/bin/env python3
"""
一次性脚本：迁移所有 Markdown 文件中的内部链接
根据 002 的目录映射表，批量替换路径中的旧目录名为新目录名。
"""
import re
from pathlib import Path

# 旧路径到新路径的映射（目录名替换对）
PATH_REPLACEMENTS = {
    # 核心教程
    '核心：现代嵌入式C++教程': 'core-embedded-cpp',
    'Chapter0': 'chapter-00-introduction',
    'Chapter1': 'chapter-01-design-constraints',
    'Chapter2': 'chapter-02-zero-overhead',
    'Chapter3': 'chapter-03-types-containers',
    'Chapter4': 'chapter-04-compile-time',
    'Chapter5': 'chapter-05-memory-management',
    'Chapter6': 'chapter-06-ownership-raii',
    'Chapter7': 'chapter-07-error-handling',
    'Chapter8': 'chapter-08-concurrency',
    'Chapter9': 'chapter-09-rtos-threads',
    'Chapter10': 'chapter-10-interrupts-isr',
    'Chapter11': 'chapter-11-peripherals-drivers',
    'Chapter12': 'chapter-12-design-patterns',
    # 其他域
    '现代C++特性': 'cpp-features',
    '现代C++的协程': 'coroutines',
    '现代C++模板教程': 'cpp-templates',
    '卷一：模板基础-C++11-14核心机制': 'vol1-basics-cpp11-14',
    '卷二：现代模板技术-C++17特性': 'vol2-modern-cpp17',
    '卷三：元编程精要-C++20-23约束与元编程': 'vol3-metaprogramming-cpp20-23',
    '卷四：泛型设计模式实战-架构级应用': 'vol4-generics-patterns',
    '现代C++工程实践': 'cpp-engineering',
    '文件处理': 'file-io',
    '深入理解CC++编译特性指南': 'compilation-deep-dive',
    '深入理解CC++的编译与链接技术2：重用概念的再阐述': 'compilation-linkage-reuse',
    '环境配置': 'environment-setup',
    '并行计算C++': 'parallel-computing',
    '调试专题': 'debugging',
    '挑战：使用现代C_C++编写STM32F103C8T6': 'stm32f1-challenge',
    # 代码目录
    'codes_and_assets': 'code',
}

# Markdown 链接正则
LINK_PATTERN = re.compile(r'\[([^\]]*)\]\(([^)]+)\)')
IMAGE_PATTERN = re.compile(r'!\[([^\]]*)\]\(([^)]+)\)')

def replace_path_in_link(link_url: str) -> str:
    """替换链接 URL 中的旧路径为新路径。"""
    if link_url.startswith(('http://', 'https://', 'mailto:', '#', 'ftp://')):
        return link_url  # 跳过外部链接和锚点

    new_url = link_url
    for old, new in PATH_REPLACEMENTS.items():
        new_url = new_url.replace(old, new)
    return new_url

def process_file(filepath: Path):
    """处理单个 Markdown 文件。"""
    content = filepath.read_text(encoding='utf-8')
    original = content

    def replace_link(match):
        text = match.group(1)
        url = match.group(2)
        new_url = replace_path_in_link(url)
        return f'[{text}]({new_url})'

    content = LINK_PATTERN.sub(replace_link, content)

    if content != original:
        filepath.write_text(content, encoding='utf-8')
        return True
    return False
```

### 手动验证步骤

1. **运行 check_links.py**：迁移脚本执行后，立即运行 `python scripts/check_links.py`（注意此脚本本身也需要先更新路径，见 005），检查所有链接。

2. **逐域检查**：按教程域逐一检查链接：
   - `documents/core-embedded-cpp/` - 检查 Chapter 间的交叉引用
   - `documents/cpp-templates/` - 检查卷之间的引用
   - `documents/stm32f1-challenge/` - 检查环境搭建到 LED/按键教程的引用
   - `documents/index.md` - 检查首页到所有域的链接

3. **构建验证**：执行 `mkdocs build --clean`，检查输出中是否有 `WARNING` 关于找不到文件的提示。

4. **本地浏览**：执行 `mkdocs serve`，在浏览器中手动点击抽查链接。

### 常见陷阱

- **URL 编码**：旧链接中的中文字符可能经过 URL 编码（如 `%E6%A0%B8%E5%BF%83`），需要在替换时同时处理编码和未编码两种形式。
- **片段标识符**：链接中可能包含 `#anchor` 片段，替换路径时需要保留片段部分。
- **大小写敏感**：新路径全部使用小写和连字符，确保替换后路径与实际文件名大小写完全匹配（Linux 文件系统大小写敏感）。
- **代码块中的链接**：在 `` ``` 代码块中的文本不应被替换。自动脚本需要跳过代码块内容（参考 `check_links.py` 中的 `in_code_block` 逻辑）。

## 涉及文件

- `documents/**/*.md`（所有 Markdown 文件，约 100+ 个）
- 具体数量取决于文章中包含内部链接的密度

## 参考资料

- `scripts/check_links.py` - 现有的链接检查脚本，其中 `LinkChecker` 类的链接提取和路径规范化逻辑可作为参考
- [CommonMark Spec - Links](https://spec.commonmark.org/0.30/#links)
- [MkDocs - Writing your docs](https://www.mkdocs.org/user-guide/writing-your-docs/)
