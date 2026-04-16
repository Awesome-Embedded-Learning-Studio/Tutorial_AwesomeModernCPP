---
id: "083"
title: "搜索和 SEO 优化"
category: mkdocs
priority: P1
status: done
created: 2026-04-15
assignee: charliechen
depends_on: ["003"]
blocks: []
estimated_effort: small
---

# 搜索和 SEO 优化

## 目标

优化 MkDocs 站点的搜索引擎体验和 SEO 表现，包括站内搜索质量改善和搜索引擎可发现性提升：

1. **搜索结果排序优化**：改进站内搜索结果的相关性排序
2. **中文分词改善**：解决 MkDocs Material 默认搜索对中文分词不准确的问题
3. **Meta description 自动生成**：为每篇文章生成合适的 meta description
4. **Sitemap 优化**：确保 sitemap 结构正确，包含所有页面
5. **Open Graph 标签**：社交媒体分享时显示正确的预览信息
6. **结构化数据**：添加 JSON-LD 结构化数据帮助搜索引擎理解内容

## 验收标准

- [ ] 中文搜索功能正常工作，能正确匹配中文关键词（不分词也能匹配）
- [ ] 搜索结果按相关性合理排序
- [ ] 每篇文章都有 meta description（frontmatter 中的 `description` 字段或自动从正文提取）
- [ ] `sitemap.xml` 生成正确，包含所有页面，排除不需要索引的页面
- [ ] Open Graph 标签 (`og:title`, `og:description`, `og:image`, `og:url`) 正确配置
- [ ] 文章页面包含结构化数据（Article schema）
- [ ] `mkdocs.yml` 中的 `site_url`, `site_name`, `site_description` 配置完整
- [ ] `robots.txt` 配置正确
- [ ] 验证 Google Search Console 可以正常索引站点

## 实施说明

### 搜索配置优化

MkDocs Material 使用 lunr.js 作为默认搜索引擎，对中文支持需要额外配置：

```yaml
# mkdocs.yml

# 搜索插件配置
plugins:
  - search:
      lang:
        - zh          # 中文支持
        - en          # 英文支持
      separator: '[\s\u3000\u3001\u3002\uff0c\uff1f\uff01\uff1b\uff1a\u201c\u201d\u2018\u2019\uff08\uff09\u300a\u300b\uff0e\uff0f]+'
      pipeline:
        - stemmer     # 词干提取
        - stopWordFilter  # 停用词过滤
        - trimmer     # 空格修剪
```

**中文分词方案评估**：

| 方案 | 优点 | 缺点 |
|------|------|------|
| lunr.js + zh lang | 内置支持，零配置 | 分词精度一般 |
| 自定义 separator | 可控性强 | 需要手动调优正则 |
| 替换为 Pagefind | 性能好，支持中文 | 需要额外集成 |
| 替换为 Algolia DocSearch | 专业级搜索 | 需要申请，有使用门槛 |

**推荐方案**：先使用 lunr.js + zh lang + 自定义 separator，如果效果不理想再考虑 Pagefind。

### Meta Description 策略

优先级：
1. 使用 frontmatter 中的 `description` 字段
2. 如果没有，自动截取正文前 160 个字符
3. 如果正文以标题开始，跳过标题后截取

```yaml
# mkdocs.yml
# meta 描述模板（通过 hook 脚本实现）
hooks:
  - docs/hooks/meta.py  # 自动生成 meta description
```

```python
# docs/hooks/meta.py（如果需要自动化）
# 在 on_page_markdown 钩子中自动提取 description
import re

def on_page_markdown(markdown, page, **kwargs):
    if not page.meta.get('description'):
        # 从正文提取前 160 字符
        text = re.sub(r'[#*`\[\]()]', '', markdown)
        text = text.strip()[:160]
        page.meta['description'] = text + '...'
```

### Sitemap 配置

```yaml
# mkdocs.yml
site_url: https://charliechen114514.github.io/Tutorial_AwesomeModernCPP/

plugins:
  - search
  - minify:
      minify_html: true
  - git-revision-date-localized:
      type: date
      fallback_to_build_date: true
```

排除不需要索引的页面：

```yaml
# 在 frontmatter 中标记
---
search:
  exclude: true
---
```

### Open Graph 标签

通过 `meta` 扩展和模板定制：

```yaml
# mkdocs.yml
extra:
  social:
    - icon: fontawesome/brands/github
      link: https://github.com/Charliechen114514/Tutorial_AwesomeModernCPP
```

在文章 frontmatter 中：

```yaml
---
title: "STM32 GPIO 输入输出教程"
description: "学习使用现代 C++ 操作 STM32F1 的 GPIO 外设..."
social_image: images/stm32-gpio-preview.png
---
```

### 结构化数据

为教程文章添加 JSON-LD Article schema：

```html
<script type="application/ld+json">
{
  "@context": "https://schema.org",
  "@type": "TechArticle",
  "headline": "STM32 GPIO 输入输出教程",
  "description": "学习使用现代 C++ 操作 STM32F1 的 GPIO 外设",
  "author": {
    "@type": "Person",
    "name": "Charlie Chen"
  },
  "programmingLanguage": "C++",
  "proficiencyLevel": "Beginner"
}
</script>
```

可以通过 MkDocs hook 自动注入结构化数据。

### robots.txt

```txt
User-agent: *
Allow: /

Sitemap: https://charliechen114514.github.io/Tutorial_AwesomeModernCPP/sitemap.xml
```

## 涉及文件

- `mkdocs.yml` — 搜索、SEO 相关插件和配置
- `docs/hooks/meta.py` —（可选）meta description 自动生成钩子
- `documents/robots.txt` — robots.txt 文件

## 参考资料

- [MkDocs Material 搜索配置](https://squidfunk.github.io/mkdocs-material/setup/setting-up-site-search/)
- [lunr.js 中文支持](https://lunrjs.com/guides/language_support.html)
- [Pagefind 搜索方案](https://pagefind.app/)
- [Open Graph Protocol](https://ogp.me/)
- [Schema.org TechArticle](https://schema.org/TechArticle)
