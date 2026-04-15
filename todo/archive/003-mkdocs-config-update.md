---
id: 003
title: "更新 mkdocs.yml 适配新目录结构与导航体系"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [002]
blocks: []
estimated_effort: medium
---

# 更新 mkdocs.yml 适配新目录结构与导航体系

## 目标

更新 `mkdocs.yml` 配置文件以适配新的 `documents/` 目录结构，重新设计导航层次，并添加新的插件和 Markdown 扩展以支持多域内容组织和增强功能。

## 验收标准

- [ ] `docs_dir` 从 `"tutorial"` 更改为 `"documents"`
- [ ] `nav` 导航结构按照新的多域层次组织（core-embedded-cpp、cpp-templates、cpp-features、cpp-engineering、compilation-deep-dive、stm32f1-challenge、environment-setup、parallel-computing、debugging）
- [ ] 新增 `mkdocs-redirects` 插件，为旧 URL 设置重定向规则（防止外部链接 404）
- [ ] 新增 `mkdocs-git-authors-plugin` 插件（显示页面作者信息）
- [ ] 新增 `mkdocs-static-i18n` 插件配置（为未来国际化做准备）
- [ ] 新增 `mkdocs-minify-plugin` 插件（压缩输出 HTML/CSS/JS）
- [ ] 新增 `pymdownx.tasklist` Markdown 扩展（支持任务列表语法）
- [ ] 新增 `pymdownx.arithmatex` Markdown 扩展（支持数学公式渲染）
- [ ] 主题配置中的 `logo` 和 `favicon` 路径仍然有效（相对于 `documents/`）
- [ ] `extra_css` 路径更新后仍然有效
- [ ] 本地 `mkdocs serve` 可以正常启动且导航栏显示正确

## 实施说明

### 1. 更新 docs_dir

```yaml
# 修改前
docs_dir: "tutorial"

# 修改后
docs_dir: "documents"
```

### 2. 新增显式 nav 导航

当前项目使用 `awesome-pages` 插件自动生成导航，不依赖显式 `nav` 配置。重构后由于中文目录名变为英文，自动生成的导航标签会变成英文路径名，需要显式定义中文导航标签以保持用户友好性：

```yaml
nav:
  - 首页: index.md
  - 核心嵌入式C++教程:
    - core-embedded-cpp/index.md
    - "第0章 简介": core-embedded-cpp/chapter-00-introduction/index.md
    - "第1章 设计与约束": core-embedded-cpp/chapter-01-design-constraints/index.md
    - "第2章 零开销抽象": core-embedded-cpp/chapter-02-zero-overhead/index.md
    - "第3章 类型与容器": core-embedded-cpp/chapter-03-types-containers/index.md
    - "第4章 编译期技术": core-embedded-cpp/chapter-04-compile-time/index.md
    - "第5章 内存管理": core-embedded-cpp/chapter-05-memory-management/index.md
    - "第6章 所有权与RAII": core-embedded-cpp/chapter-06-ownership-raii/index.md
    - "第7章 错误处理": core-embedded-cpp/chapter-07-error-handling/index.md
    - "第8章 并发": core-embedded-cpp/chapter-08-concurrency/index.md
    - "第9章 RTOS与线程": core-embedded-cpp/chapter-09-rtos-threads/index.md
    - "第10章 中断与ISR": core-embedded-cpp/chapter-10-interrupts-isr/index.md
    - "第11章 外设与驱动": core-embedded-cpp/chapter-11-peripherals-drivers/index.md
    - "第12章 设计模式": core-embedded-cpp/chapter-12-design-patterns/index.md
  - C++模板教程:
    - cpp-templates/index.md
    - "卷一 模板基础": cpp-templates/vol1-basics-cpp11-14/index.md
    - "卷二 现代模板技术": cpp-templates/vol2-modern-cpp17/index.md
    - "卷三 元编程精要": cpp-templates/vol3-metaprogramming-cpp20-23/index.md
    - "卷四 泛型设计模式": cpp-templates/vol4-generics-patterns/index.md
  - C++特性专题:
    - cpp-features/index.md
    - "协程": cpp-features/coroutines/index.md
  - C++工程实践:
    - cpp-engineering/index.md
    - "文件处理": cpp-engineering/file-io/index.md
  - 编译特性指南: compilation-deep-dive/index.md
  - 环境配置: environment-setup/index.md
  - 并行计算: parallel-computing/index.md
  - 调试专题: debugging/index.md
  - STM32F1实战:
    - stm32f1-challenge/index.md
    - "环境搭建": stm32f1-challenge/00-env-setup/index.md
    - "LED": stm32f1-challenge/01-led/index.md
    - "按键": stm32f1-challenge/02-button/index.md
  - 标签: tags.md
```

> 注意：显式 nav 与 `awesome-pages` 插件可能冲突。如果 `awesome-pages` 的自动发现功能仍有用（各章节下的子页面），可以保留该插件但不再依赖 `.pages` 文件。如果完全使用显式 nav，则可以移除 `awesome-pages`。建议保留 `awesome-pages` 作为 fallback，让它在 nav 未覆盖的子页面中生效。

### 3. 新增插件

```yaml
plugins:
  # ... 保留现有 search、tags、awesome-pages、git-revision-date-localized ...

  # URL 重定向 - 旧 URL 自动跳转到新 URL
  - redirects:
      redirect_maps:
        # 旧路径（相对于旧 docs_dir）到新路径的映射
        # 示例（具体映射需根据实际文件名确定）：
        # "核心：现代嵌入式C++教程/Chapter1/index.md": "core-embedded-cpp/chapter-01-design-constraints/index.md"
        # 注意：redirects 插件的 redirect_maps 中的键是相对于 docs_dir 的路径
        # 由于旧目录名是中文，可能需要为每个有外部链接的页面设置重定向
        # 这部分需要在迁移完成后通过 404 日志逐步补充

  # Git 作者信息
  - git-authors:
      show_line_count: false
      show_contribution: false
      sort_authors_by: "name"
      authorship_threshold_percent: 5

  # 静态站点国际化（为未来准备，暂不启用多语言内容）
  - i18n:
      default_language: zh
      languages:
        zh: "中文"
        en: "English"
      # 暂时不创建翻译文件，仅设置默认语言

  # HTML/CSS/JS 压缩
  - minify:
      minify_html: true
      minify_css: true
      minify_js: true
      htmlmin_opts:
        remove_comments: true
        remove_empty_space: true
      cssmin_opts: {}
      jsmin_opts: {}
```

### 4. 新增 Markdown 扩展

```yaml
markdown_extensions:
  # ... 保留现有全部扩展 ...

  # 任务列表语法（- [ ] / - [x]）
  - pymdownx.tasklist:
      custom_checkbox: true
      clickable_checkbox: true

  # 数学公式渲染（LaTeX）
  - pymdownx.arithmatex:
      generic: true
```

同时在 `extra_javascript` 中添加 MathJax 配置（如果启用了 arithmatex）：

```yaml
extra_javascript:
  - javascripts/mathjax.js
  - https://unpkg.com/mathjax@3/es5/tex-mml-chtml.js
```

需要在 `documents/javascripts/mathjax.js` 中创建 MathJax 配置文件：

```javascript
window.MathJax = {
  tex: {
    inlineMath: [["\\(", "\\)"]],
    displayMath: [["\\[", "\\]"]],
  },
  options: {
    skipHtmlTags: ['script', 'noscript', 'style', 'textarea', 'pre'],
  },
};
```

### 5. 更新 pyproject.toml 依赖

新增的插件需要添加到 `scripts/pyproject.toml` 的依赖列表中：

```toml
dependencies = [
    # ... 现有依赖 ...
    "mkdocs-redirects>=1.2.0",
    "mkdocs-git-authors-plugin>=0.9.0",
    "mkdocs-static-i18n>=1.2.0",
    "mkdocs-minify-plugin>=0.8.0",
]
```

### 6. 注意事项

- `awesome-pages` 插件通过 `.pages` 文件控制导航标题。迁移后如果各子目录中存在 `.pages` 文件（含中文标题），需要更新其中的标题或删除它们以让显式 nav 接管。
- `git-revision-date-localized` 插件在 `fetch-depth: 0` 的 CI 环境中正常工作，无需额外修改。
- `redirects` 插件的 `redirect_maps` 键是源文件路径（相对于 `docs_dir`），值是目标文件路径。旧路径中的中文目录名需要与实际迁移前的路径完全匹配。
- `i18n` 插件的启用可能会要求每个 `.md` 文件有对应的语言后缀（如 `index.zh.md`、`index.en.md`）。由于当前只有中文内容，初始阶段只设置 `default_language: zh`，不创建翻译文件。如果此插件导致问题，可以先注释掉。

## 涉及文件

- `mkdocs.yml`（主配置文件，主要修改对象）
- `scripts/pyproject.toml`（新增 pip 依赖）
- `documents/javascripts/mathjax.js`（新建，MathJax 配置）

## 参考资料

- 当前 `mkdocs.yml` 配置（项目根目录）
- [mkdocs-redirects 插件文档](https://github.com/datarobot/mkdocs-redirects)
- [mkdocs-git-authors-plugin 文档](https://github.com/timvink/mkdocs-git-authors-plugin)
- [mkdocs-static-i18n 文档](https://ultrabug.github.io/mkdocs-static-i18n/)
- [mkdocs-minify-plugin 文档](https://github.com/byrnereese/mkdocs-minify-plugin)
- [PyMdown Extensions - Tasklist](https://facelessuser.github.io/pymdown-extensions/extensions/tasklist/)
- [PyMdown Extensions - Arithmatex](https://facelessuser.github.io/pymdown-extensions/extensions/arithmatex/)
