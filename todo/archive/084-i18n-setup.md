---
id: "084"
title: "多语言支持准备：mkdocs-static-i18n 集成"
category: mkdocs
priority: P2
status: done
created: 2026-04-15
assignee: charliechen
depends_on: ["003"]
blocks: ["051", "102"]
estimated_effort: medium
---

# 多语言支持准备：mkdocs-static-i18n 集成

## 目标

为 MkDocs 站点集成 `mkdocs-static-i18n` 插件，建立完整的多语言支持基础设施，为后续的 AI 翻译流水线（automation/051）和双语站点（translation/102）打好基础。

具体包括：
1. 安装和配置 `mkdocs-static-i18n` 插件
2. 设计 `.en.md` 双语文件布局方案
3. 配置语言切换器 UI
4. 确保现有中文内容不受影响
5. 创建英文内容占位文件

## 验收标准

- [ ] `mkdocs-static-i18n` 插件已安装并正确配置在 `mkdocs.yml` 中
- [ ] 默认语言为中文（`zh`），支持英文（`en`）切换
- [ ] 语言切换器 UI 已启用（MkDocs Material 语言选择器）
- [ ] 双语文件布局方案已确定：使用 `.en.md` 后缀方案（如 `article.md` + `article.en.md`）
- [ ] 现有中文 `.md` 文件不受影响，站点功能正常
- [ ] 至少 2 篇文章有对应的 `.en.md` 英文版本作为验证
- [ ] 导航菜单支持中英文双语
- [ ] 搜索功能支持中英文双语搜索
- [ ] `mkdocs serve` 本地开发服务器正常工作
- [ ] `mkdocs build` 构建成功，生成双语站点

## 实施说明

### mkdocs-static-i18n 配置

```yaml
# mkdocs.yml

plugins:
  - i18n:
      docs_structure: suffix          # 使用后缀方案：file.md + file.en.md
      languages:
        - locale: zh
          name: 中文
          default: true
          build: true
        - locale: en
          name: English
          build: true
          nav_translations:
            理论基础: Theory
            平台教程: Platforms
            RTOS: RTOS
            参考资料: Reference
            其他: General
            嵌入式 C++: Embedded C++
            C++ 基础: C++ Basics
            设计模式: Design Patterns
```

### 文件布局方案

**推荐方案：后缀方案（suffix）**

```
documents/
├── theory/
│   ├── cpp-basics.md          # 中文（默认）
│   ├── cpp-basics.en.md       # 英文翻译
│   ├── memory.md              # 中文
│   └── memory.en.md           # 英文
├── platforms/
│   ├── stm32f1/
│   │   ├── gpio.md            # 中文
│   │   └── gpio.en.md         # 英文
```

优势：
- 文件并排放置，容易对应和对比
- 不需要复制整个目录结构
- 翻译流水线只需生成 `.en.md` 文件
- Git diff 可以清晰看到中英文变更

**排除方案：子目录方案（folder）**
- 使用 `zh/` 和 `en/` 子目录
- 优势：完全隔离
- 劣势：目录重复，维护成本高，不推荐

### 语言切换器 UI

MkDocs Material 内置支持 i18n 语言切换器：

```yaml
# mkdocs.yml
theme:
  name: material
  custom_dir: overrides
  features:
    - search
    - navigation.footer

# 语言切换器通过 theme.alt 选择
# mkdocs-static-i18n 自动在导航栏添加语言切换按钮
```

需要在 `overrides/partials/` 下添加或修改模板以支持语言切换器：

```html
<!-- overrides/partials/languages/zh.html -->
{% macro t(key) %}{{ {
  "search.config.lang": "zh",
}[key] }}{% endmacro %}
```

### 导航翻译

```yaml
# mkdocs.yml - i18n 配置中的 nav_translations
plugins:
  - i18n:
      languages:
        - locale: zh
          default: true
        - locale: en
          nav_translations:
            # 导航项翻译映射
            理论基础: "Theory"
            C++ 基础: "C++ Basics"
            嵌入式 C++: "Embedded C++"
            设计模式: "Design Patterns"
            平台教程: "Platform Tutorials"
            STM32F1: "STM32F1"
            ESP32: "ESP32"
            RP2040: "RP2040"
            RTOS: "RTOS"
            FreeRTOS: "FreeRTOS"
            参考资料: "Reference"
            其他: "General"
```

### 搜索双语配置

```yaml
# mkdocs.yml
plugins:
  - search:
      lang:
        - zh
        - en
  - i18n:
      # i18n 会自动为每种语言创建独立搜索索引
```

### 初始英文占位文件

为首页和关键页面创建英文占位：

```markdown
<!-- documents/index.en.md -->
---
title: Awesome Modern C++ Tutorial for Embedded Systems
---

# Awesome Modern C++ Tutorial for Embedded Systems

> This page is currently being translated. Please visit the [Chinese version](..) for the complete content.

Translation in progress...
```

### 构建验证

```bash
# 本地验证
pip install mkdocs-static-i18n
mkdocs build
# 检查 site/ 目录下是否生成 zh/ 和 en/ 子目录

# 本地预览
mkdocs serve
# 访问 http://localhost:8000 检查语言切换
```

### 与翻译流水线的集成点

此 TODO 完成后：
- automation/051 (AI 翻译流水线) 可以直接生成 `.en.md` 文件
- translation/102 (双语站点) 可以基于此配置进一步完善

## 涉及文件

- `mkdocs.yml` — i18n 插件配置
- `requirements.txt` — 添加 `mkdocs-static-i18n` 依赖
- `documents/index.en.md` — 英文首页占位
- `overrides/` — 自定义模板目录（语言切换器）

## 参考资料

- [mkdocs-static-i18n 官方文档](https://ultrabug.github.io/mkdocs-static-i18n/)
- [MkDocs Material i18n 指南](https://squidfunk.github.io/mkdocs-material/setup/changing-the-language/)
- [mkdocs-static-i18n 后缀方案](https://ultrabug.github.io/mkdocs-static-i18n/setup/using-suffixes/)
