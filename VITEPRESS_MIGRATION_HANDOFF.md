# VitePress 迁移交接文档

## 项目背景

将 TAMCPP_Exp_VitePress（现代 C++ 教程）从 MkDocs 迁移到 VitePress。已在 main 分支上完成大部分基础设施搭建。

## 用户要求

1. VitePress 读取 `documents/` 下的文档，**中文内容文件不动**
2. 站点工程文件放在 `site/` 下
3. 英文翻译文件（原 `.en.md`）已移动到 `documents/en/`，使用 VitePress 原生 i18n
4. MkDocs 专有语法直接改掉，不写兼容层

## 已完成的工作

### 目录结构
```
site/.vitepress/
  config/
    index.ts    # 主配置（srcDir: '../documents', locales, markdown 等）
    nav.ts      # 导航栏（中文 + 英文）
    sidebar.ts  # 侧边栏（自动扫描目录生成）
  plugins/
    kbd-plugin.ts   # ++key++ → <kbd> 转换插件
  theme/
    index.ts    # DefaultTheme + custom.css
    custom.css  # 中文排版优化
  public/
    favicon.ico, logo.svg
```

### 已完成的文件变更
- `package.json` — VitePress 依赖和 scripts
- `site/.vitepress/config/index.ts` — 主配置
- `site/.vitepress/config/nav.ts` — 导航
- `site/.vitepress/config/sidebar.ts` — 自动生成侧边栏
- `site/.vitepress/plugins/kbd-plugin.ts` — 键盘快捷键插件
- `site/.vitepress/theme/index.ts` + `custom.css` — 主题
- `.github/workflows/deploy.yml` — Node.js + pnpm 部署
- `.gitignore` — VitePress 条目
- 40 个 `.en.md` 已 `git mv` 到 `documents/en/` 对应目录
- 3 个文件的 MkDocs 语法已转换（`!!!` → `:::`, `===` → 代码组/标题）
- 227 个 .md 文件中反引号内的 `<` `>` 已转义为 `&lt;` `&gt;`（防止 Vue 误解析 C++ 头文件名如 `<stdio.h>`）
- MkDocs 清理：删除 `mkdocs.yml`, `overrides/`, `documents/hooks/`, `scripts/pyproject.toml`, `scripts/mkdocs_dev.sh`

## 当前阻塞问题：构建失败

运行 `pnpm build` 失败，错误类似：
```
[vite:vue] Element is missing end tag.
```

**根因**：Vue 模板编译器将 markdown 中的 `<...>` 模式解析为 HTML 标签。已用脚本转义了 227 个文件中反引号内的 `<>`，但仍有部分未覆盖到（如不在反引号内的 `<>`，或代码块内的特殊模式）。

### 调试方法
1. `pnpm build` 看报错的文件和行号（注意：行号是 Vue SFC 处理后的，不完全对应原始 markdown）
2. 在报错文件中搜索不在反引号/代码块内的 `<` 字符
3. 将其转义为 `&lt;` 和 `&gt;`

### 注意事项
- 转义脚本只处理了反引号内的 `<>`，未处理代码块（` ``` `）内的 —— 代码块内的不应需要转义
- 如果 `<>` 出现在普通文本中（不在反引号也不在代码块），也需要手动转义
- 已在 config 中添加了 `vue.template.compilerOptions.isCustomElement`，可调整该函数来忽略更多标签模式

## 配置关键点

### site/.vitepress/config/index.ts
- `srcDir: '../documents'` — 相对于 site/ 解析
- `srcExclude: ['en/**']` — **构建时临时排除 en/，修复所有构建错误后需移除此行**
- `locales` — root (zh-CN) + en (en-US)，en 的 link 为 `/en/`
- `markdown.math: true` — MathJax 支持
- `markdown.config` — 注册了 kbd-plugin

### site/.vitepress/config/sidebar.ts
- 使用 `import.meta.dirname` 计算路径，DOCS_ROOT = `join(import.meta.dirname, '../../../documents')`
- `buildSidebar()` 自动扫描各卷目录生成侧边栏
- 英文侧边栏只包含 `en/` 下已有的文件

### 部署
- GitHub Actions: `peaceiris/actions-gh-pages@v4`，输出目录 `site/.vitepress/dist`
- Base URL: `/Tutorial_AwesomeModernCPP/`

## 剩余工作

1. **修复构建错误** — 找到并修复所有导致 `Element is missing end tag` 的 `<>` 模式
2. **移除 `srcExclude: ['en/**']`** — 构建通过后，恢复 en/ locale 到 config
3. **恢复 locales 中的 en 配置** — 当前已注释掉
4. **运行 `pnpm dev`** — 本地验证所有页面渲染正常
5. **验证 i18n** — 语言切换器工作正常
6. **验证 kbd 插件** — `++ctrl+c++` 渲染为 `<kbd>ctrl</kbd>+<kbd>c</kbd>`
7. **验证 MathJax** — LaTeX 公式渲染
8. **提交代码** — 所有变更在 main 分支上
