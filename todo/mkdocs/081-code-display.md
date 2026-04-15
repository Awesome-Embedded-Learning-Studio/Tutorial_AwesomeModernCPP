---
id: "081"
title: "代码展示增强：行号、复制、注释与高亮"
category: mkdocs
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["003"]
blocks: ["112"]
estimated_effort: medium
---

# 代码展示增强：行号、复制、注释与高亮

## 目标

增强 MkDocs 站点中代码块的展示效果，提升读者的代码阅读体验。具体包括：

1. **行号显示优化**：配置代码行号，支持行号高亮（特定行标记）
2. **代码复制按钮改进**：优化复制按钮的样式和交互
3. **代码块注释支持**：支持在代码块中添加行内标注和注释
4. **语法高亮主题定制**：定制 Pygments 主题，区分亮色和暗色模式
5. **Compiler Explorer 嵌入评估**：评估通过 mkdocs-code-validator 或自定义 JS 嵌入 Godbolt 的可行性

## 验收标准

- [ ] `mkdocs.yml` 中 `pymdownx.highlight` 配置完成，行号默认开启
- [ ] 支持 `linenums="1"` 和 `hl_lines="3 5-8"` 语法，正确显示行号和行高亮
- [ ] 代码复制按钮已启用（`pymdownx.snippets` 或 Material 内置），样式优化
- [ ] 代码块标题支持：`title="filename.cpp"` 显示在代码块顶部
- [ ] `documents/stylesheets/extra.css` 中添加代码块相关样式定制
- [ ] 亮色/暗色模式下代码高亮主题分别配置，切换时无闪烁
- [ ] 支持代码块内标注（使用 `# (1)` 注释标记，Material Code Annotations 功能）
- [ ] 评估了 Compiler Explorer 嵌入可行性并记录结论
- [ ] 所有现有代码块在新配置下渲染正常

## 实施说明

### mkdocs.yml 配置

```yaml
# mkdocs.yml
theme:
  name: material
  palette:
    - media: "(prefers-color-scheme: light)"
      scheme: default
      primary: indigo
    - media: "(prefers-color-scheme: dark)"
      scheme: slate
      primary: indigo

markdown_extensions:
  - pymdownx.highlight:
      anchor_linenums: true        # 行号锚点（可链接到特定行）
      line_spans: __span           # 行 span 类名
      pygments_lang_class: true    # 语言 class 标记
      linenums_style: pymdownx-inline  # 行号样式
  - pymdownx.inlinehilite         # 行内代码高亮
  - pymdownx.snippets             # 代码片段
  - pymdownx.superfences          # 增强代码块
  - pymdownx.tabbed:              # 代码块标签页
      alternate_style: true
  - attr_list                     # 属性列表（支持代码块属性）
  - md_in_html                    # HTML 内 Markdown

theme:
  features:
    - content.code.copy           # 代码复制按钮
    - content.code.annotate       # 代码注释标注
    - content.code.select         # 代码行选择
```

### 代码块使用示例

```markdown
``` cpp title="gpio.cpp" linenums="1" hl_lines="3 5-7"
#include "gpio.h"

void GpioPin::configure(Direction dir) {   // (1)
    if (dir == Direction::Output) {
        register_->MODER &= ~(3U << (pin_ * 2));
        register_->MODER |= (1U << (pin_ * 2));
    }
}
```

1. :man_raising_hand: 配置 GPIO 引脚方向，使用位操作修改寄存器
```

### 样式定制 (extra.css)

```css
/* 代码块样式增强 */
.highlight {
  border-radius: 6px;
  margin: 1em 0;
}

/* 代码块标题栏 */
.highlight .filename {
  background-color: var(--md-code-bg-color);
  border-bottom: 1px solid var(--md-default-fg-color--lightest);
  padding: 0.5em 1em;
  font-size: 0.85em;
}

/* 行高亮 */
.highlight .hll {
  background-color: rgba(255, 235, 59, 0.15);
}

/* 暗色模式行高亮 */
[data-md-color-scheme="slate"] .highlight .hll {
  background-color: rgba(255, 235, 59, 0.1);
}

/* 复制按钮样式 */
.md-clipboard {
  color: var(--md-default-fg-color--lighter);
  transition: color 0.2s;
}
.md-clipboard:hover {
  color: var(--md-primary-fg-color);
}

/* 行号样式 */
.highlight .linenos {
  color: var(--md-default-fg-color--lighter);
  user-select: none;
  padding-right: 1em;
}
```

### Compiler Explorer 嵌入评估

**方案 A：iframe 嵌入**
- 优点：简单直接，无需后端
- 缺点：加载慢，依赖外部服务，可能被 CSP 阻止
- 评估结论：适合作为"可选"功能，不建议作为默认

**方案 B：自定义 JS 组件**
- 使用 Compiler Explorer API 获取汇编输出
- 在页面中渲染简化的汇编对比视图
- 优点：可控性强，可延迟加载
- 缺点：开发成本高

**方案 C：使用 mkdocs-code-validator 插件**
- 评估插件成熟度和维护状态
- 如果可用，集成到构建流程

**建议**：先完成基础代码展示增强，Compiler Explorer 嵌入作为后续独立 TODO (interactive/112)。

### C++ 语法高亮增强

为嵌入式常用关键字添加自定义高亮：

```css
/* 寄存器类型高亮 */
.token.keyword.register {
  color: #E91E63;
}

/* 宏定义高亮 */
.token.macro {
  color: #7C4DFF;
}
```

## 涉及文件

- `mkdocs.yml` — Pygments 和代码相关扩展配置
- `documents/stylesheets/extra.css` — 代码块样式定制

## 参考资料

- [MkDocs Material 代码块配置](https://squidfunk.github.io/mkdocs-material/reference/code-blocks/)
- [Pygments 代码高亮](https://pygments.org/)
- [pymdownx 扩展文档](https://facelessuser.github.io/pymdown-extensions/extensions/highlight/)
- [Compiler Explorer API](https://github.com/compiler-explorer/compiler-explorer/blob/main/docs/API.md)
