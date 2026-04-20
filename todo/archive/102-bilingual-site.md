---
id: "102"
title: "双语站点设计：中英双语 MkDocs 站点"
category: translation
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["084"]
blocks: []
estimated_effort: medium
---

# 双语站点设计：中英双语 MkDocs 站点

## 目标

基于 mkdocs-static-i18n 插件（已在 mkdocs/084 中配置），设计并实现完整的中英双语 MkDocs 站点。实现语言切换器 UI、双语搜索支持、默认中文 + 可切换英文的用户体验。

此 TODO 在 mkdocs/084 (i18n-setup) 的基础上进行深化，聚焦于最终用户体验和双语站点的完整功能。

## 验收标准

- [ ] 双语站点可通过语言切换器在中英文之间无缝切换
- [ ] 默认显示中文内容
- [ ] 英文页面仅在有翻译时显示，未翻译页面自动 fallback 到中文
- [ ] 搜索功能支持中英文双语搜索（两种语言的内容都出现在搜索索引中）
- [ ] 导航菜单在英文模式下显示英文翻译
- [ ] 所有 UI 元素（搜索框、footer、admonition）在两种语言下都正确显示
- [ ] URL 结构合理：中文默认路径 + `/en/` 英文路径
- [ ] 404 页面支持双语
- [ ] `mkdocs build` 正确生成双语站点目录结构
- [ ] `mkdocs serve` 本地开发正常工作，语言切换无延迟
- [ ] 至少 10 篇核心文章有英文翻译

## 实施说明

### 双语站点架构

```
site/
├── index.html              # 中文首页
├── en/
│   └── index.html          # 英文首页
├── theory/
│   ├── cpp-basics/
│   │   ├── index.html      # 中文
│   │   └── en/
│   │       └── index.html  # 英文
│   └── ...
├── platforms/
│   └── stm32f1/
│       ├── gpio/
│       │   ├── index.html
│       │   └── en/
│       │       └── index.html
│       └── ...
├── search/
│   ├── search_index.json   # 中文搜索索引
│   └── en/
│       └── search_index.json  # 英文搜索索引
└── sitemap.xml             # 包含所有语言页面
```

### mkdocs.yml 完整配置

```yaml
# mkdocs.yml
site_name: Tutorial_AwesomeModernCPP
site_url: https://charliechen114514.github.io/Tutorial_AwesomeModernCPP/
site_description: 现代C++嵌入式系统教程
site_author: Charlie Chen

theme:
  name: material
  language: zh              # 默认语言
  custom_dir: overrides     # 自定义模板
  features:
    - navigation.tabs
    - navigation.tabs.sticky
    - navigation.path
    - navigation.indexes
    - navigation.top
    - navigation.footer
    - search.suggest        # 搜索建议
    - search.highlight      # 搜索高亮
    - content.code.copy
    - content.code.annotate
  palette:
    - media: "(prefers-color-scheme: light)"
      scheme: default
      primary: indigo
      toggle:
        icon: material/brightness-7
        name: 切换到暗色模式
    - media: "(prefers-color-scheme: dark)"
      scheme: slate
      primary: indigo
      toggle:
        icon: material/brightness-4
        name: 切换到亮色模式

plugins:
  - i18n:
      docs_structure: suffix
      languages:
        - locale: zh
          name: 中文
          default: true
          build: true
          site_name: Tutorial_AwesomeModernCPP
          site_description: 现代 C++ 嵌入式系统教程
          nav_translations:
            # 导航翻译映射
            理论基础: Theory
            C++ 基础: C++ Basics
            嵌入式 C++: Embedded C++
            平台教程: Platform Tutorials
            STM32F1: STM32F1
            RTOS: RTOS
            参考资料: Reference
        - locale: en
          name: English
          build: true
          site_name: Awesome Modern C++ for Embedded
          site_description: Modern C++ Tutorial for Embedded Systems
          nav_translations:
            理论基础: Theory
            C++ 基础: C++ Basics
            嵌入式 C++: Embedded C++
            平台教程: Platform Tutorials
            STM32F1: STM32F1
            RTOS: RTOS
            参考资料: Reference

  - search:
      lang:
        - zh
        - en
```

### Fallback 机制

当英文翻译不存在时，自动显示中文内容：

```html
<!-- overrides/partials/languages/en.html -->
<!-- 在英文模板中处理缺失翻译 -->
{% if not page.file.src_path.endswith('.en.md') %}
  <div class="admonition info">
    <p>This page is not yet translated. Showing the Chinese version.
    <a href="...">Help translate</a></p>
  </div>
{% endif %}
```

### 自定义语言切换器

如果 MkDocs Material 内置语言切换器不够，可以通过 overrides 定制：

```html
<!-- overrides/partials/header.html -->
{% extends "base.html" %}

{% block languages %}
<div class="md-header__option">
  <div class="md-select">
    {% for language in config.plugins.i18n.languages %}
    <button class="md-select__link {% if language.default %}active{% endif %}"
            href="{{ language.url }}">
      {{ language.name }}
    </button>
    {% endfor %}
  </div>
</div>
{% endblock %}
```

### 双语搜索配置

mkdocs-static-i18n 会自动为每种语言创建独立的搜索索引。确保：

1. 搜索配置中包含 `zh` 和 `en` 两种语言
2. 中文搜索使用自定义 separator 改善分词
3. 英文搜索使用默认的 stemmer

```yaml
# mkdocs.yml
plugins:
  - search:
      lang:
        - zh
        - en
      separator: '[\s\u3000\u3001\u3002\uff0c\uff1f\uff01]+'
      pipeline:
        - stemmer
        - stopWordFilter
```

### Sitemap 优化

确保 sitemap 包含所有语言版本，并添加 hreflang 标签：

```html
<link rel="alternate" hreflang="zh" href="https://example.com/page/" />
<link rel="alternate" hreflang="en" href="https://example.com/en/page/" />
<link rel="alternate" hreflang="x-default" href="https://example.com/page/" />
```

mkdocs-static-i18n 可能自动处理 hreflang，需要验证。

### 404 页面双语

创建双语 404 页面：

```html
<!-- overrides/404.html -->
{% extends "main.html" %}
{% block content %}
<h1>页面未找到 / Page Not Found</h1>
<p>
  抱歉，您访问的页面不存在。
  <a href="/">返回首页</a>
</p>
<p>
  Sorry, the page you requested was not found.
  <a href="/en/">Go to Home</a>
</p>
{% endblock %}
```

## 涉及文件

- `mkdocs.yml` — 完整的 i18n 和双语配置
- `overrides/` — 自定义模板目录（语言切换器、404 页面等）
- `overrides/partials/` — 自定义 partials
- `overrides/404.html` — 双语 404 页面

## 参考资料

- [mkdocs-static-i18n 官方文档](https://ultrabug.github.io/mkdocs-static-i18n/)
- [MkDocs Material 多语言](https://squidfunk.github.io/mkdocs-material/setup/changing-the-language/)
- [Google 多语言 SEO](https://developers.google.com/search/docs/specialty/international)
