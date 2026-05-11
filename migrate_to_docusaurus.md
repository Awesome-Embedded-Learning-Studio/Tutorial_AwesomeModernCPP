# 文档引擎迁移评估报告

> 评估日期：2026-05-10
> 评估结论：**推荐迁移，目标引擎通过 POC 对比决定**
> POC 候选：**VitePress** vs **Astro + Starlight**

---

## 一、迁移动机

### 1. MkDocs Material 已进入维护模式（2025.11）

- 作者 squidfunk 已离开，转做新项目 Zensical
- Insiders 付费计划取消，功能释放到 9.7.x
- 9.7.5 锁定 `mkdocs < 2`，不跟进 MkDocs v2
- 不再有新功能，仅安全/兼容修复
- Material 主题视觉天花板已定，无法跟随现代 Web 设计趋势

### 2. MkDocs 核心前景不明

- v1.6.1 后开发显著放缓
- Tom Christie 于 2026.3 回归宣布 v2.0 计划
- 但 Material 已锁定 `< 2`，v2.0 生态能否成型存疑

### 3. 阅读体验追求最顶级

Docusaurus 虽好，但在阅读体验上不是最优选。2026 年实测排名：

| 排名 | 引擎 | 阅读体验核心优势 | 不足 |
|------|------|----------------|------|
| 1 | **VitePress** | 最快加载、最优移动端、最佳代码块呈现 | Vue 生态，定制需 Vue 知识 |
| 2 | **Astro + Starlight** | 最美排版、零 JS、Lighthouse 满分、最强无障碍 | Astro 生态较新 |
| 3 | Docusaurus | 功能最全（版本管理、博客、i18n） | webpack 最慢、移动端一般、排版需调 |
| 4 | MkDocs Material | 基准线 | 已 sunset |

---

## 二、POC 验证方案（低入侵）

### 验证策略：Fork 仓库

**这是最安全的验证方式**：

1. Fork `Tutorial_AwesomeModernCPP` 到 `Tutorial_AwesomeModernCPP-poc`
2. 在 fork 中创建两个分支：`poc/vitepress` 和 `poc/starlight`
3. 各自迁移 **vol3-standard-library**（最小卷，84KB）作为测试内容
4. 各自部署到 fork 的 GitHub Pages（可用不同 base path）
5. **用真实内容对比阅读体验**，满意后再决定全量迁移

### 安全保障

| 措施 | 说明 |
|------|------|
| Fork 隔离 | 主仓库完全不受影响，零风险 |
| 单卷内容 | 只迁移 84KB 内容，工作量小，回滚容易 |
| 独立部署 | fork 的 gh-pages 和主站互不干扰 |
| 可随时删除 | 验证完毕删除 fork，干干净净 |

### POC 评估清单

每个 POC 都需要验证以下维度（用同一卷内容）：

- [ ] 排版质量（行距、字号、内容密度）
- [ ] 代码块呈现（高亮、行号、diff 模式、复制按钮）
- [ ] Mermaid 图表渲染
- [ ] 数学公式渲染
- [ ] 提示框/标签页
- [ ] 搜索体验（中文分词）
- [ ] 暗色模式质量
- [ ] 移动端阅读体验
- [ ] 页面加载速度
- [ ] Frontmatter 兼容性
- [ ] 导航/侧边栏可用性
- [ ] 构建速度

---

## 三、VitePress POC 方案

### 技术栈
- VitePress（Vue 生态，Vue 核心团队出品）
- Node.js >= 20
- pnpm

### 项目结构
```
Tutorial_AwesomeModernCPP-poc/
├── docs/                          # VitePress 文档根目录
│   ├── .vitepress/
│   │   ├── config.ts              # 主配置
│   │   └── theme/                 # 自定义主题
│   ├── vol3-standard-library/     # POC 测试卷
│   │   ├── _category_.json
│   │   ├── index.md
│   │   └── ... (迁移后的文章)
│   └── index.md                   # 首页
├── package.json
└── pnpm-lock.yaml
```

### 功能对应

| MkDocs 功能 | VitePress 方案 |
|------------|---------------|
| 代码高亮 | Shiki（内置，支持行高亮、diff） |
| Mermaid | vitepress-plugin-mermaid |
| 数学公式 | markdown-it-katex 或 @mathjax |
| 提示框 | 内置 custom containers（`::: tip`） |
| 标签页 | vitepress-plugin-tabs |
| 搜索 | 内置 MiniSearch（离线搜索） |
| 暗色模式 | 内置，双主题配置 |
| i18n | 内置多语言支持 |
| 导航 | 自动侧边栏 + 手动配置 |

### 核心配置骨架
```ts
// .vitepress/config.ts
export default defineConfig({
  title: '现代 C++ 教程',
  lang: 'zh-CN',
  themeConfig: {
    nav: [...],
    sidebar: { ... },
    search: { provider: 'local' },
    editLink: { ... },
    lastUpdated: true,
  },
  markdown: {
    lineNumbers: true,
    math: true,  // KaTeX
  },
})
```

### 参考站点
- Vue.js 官方文档（https://vuejs.org）
- Vite 官方文档（https://vite.dev）
- Vitest 文档（https://vitest.dev）

---

## 四、Astro + Starlight POC 方案

### 技术栈
- Astro + Starlight 主题
- Node.js >= 20
- pnpm

### 项目结构
```
Tutorial_AwesomeModernCPP-poc/
├── src/
│   ├── content/
│   │   └── docs/
│   │       └── vol3-standard-library/
│   │           └── ... (迁移后的文章)
│   └── components/           # 自定义组件
├── astro.config.mjs
├── package.json
└── pnpm-lock.yaml
```

### 功能对应

| MkDocs 功能 | Starlight 方案 |
|------------|---------------|
| 代码高亮 | Expressive Code（开箱即用，业界最佳） |
| Mermaid | @lorenzo_lewis/starlight-mermaid |
| 数学公式 | rehype-katex |
| 提示框 | 内置 callouts |
| 标签页 | starlight-tabs 插件 |
| 搜索 | PageFind（内置，离线，零配置） |
| 暗色模式 | 内置，精心调优 |
| i18n | 原生支持，目录方案 |
| 导航 | 自动侧边栏 + 手动配置 |

### 核心配置骨架
```js
// astro.config.mjs
import { defineConfig } from 'astro/config'
import starlight from '@astrojs/starlight'

export default defineConfig({
  site: 'https://awesome-embedded-learning-studio.github.io',
  integrations: [
    starlight({
      title: '现代 C++ 教程',
      defaultLocale: 'zh-CN',
      locales: { 'zh-CN': { label: '简体中文' } },
      sidebar: [{ autogenerate: { directory: 'docs' } }],
    }),
  ],
})
```

### 参考站点
- Astro 官方文档（https://docs.astro.build）
- Starlight 官方文档（https://starlight.astro.build）

---

## 五、POC 执行步骤

### Step 1：Fork 仓库
```bash
# 在 GitHub 上 fork Tutorial_AwesomeModernCPP
git clone https://github.com/<user>/Tutorial_AwesomeModernCPP-poc.git
cd Tutorial_AwesomeModernCPP-poc
```

### Step 2：创建 POC 分支
```bash
git checkout -b poc/vitepress
git checkout -b poc/starlight
```

### Step 3：各分支独立搭建

#### VitePress 分支
1. `pnpm init` + 安装 vitepress
2. 创建 `.vitepress/config.ts`
3. 迁移 vol3 的 frontmatter 和内容
4. 配置搜索、Mermaid、数学公式
5. `pnpm docs:dev` 本地预览
6. `pnpm docs:build` 构建测试

#### Starlight 分支
1. `pnpm create astro --template starlight`
2. 配置 `astro.config.mjs`
3. 迁移 vol3 的 frontmatter 和内容
4. 配置搜索、Mermaid、数学公式
5. `pnpm dev` 本地预览
6. `pnpm build` 构建测试

### Step 4：对比评估
用 POC 评估清单逐项打分，重点关注：
1. **移动端阅读体验**（手机打开两个 POC 站点对比）
2. **代码块呈现质量**（C++ 代码高亮、行号、复制）
3. **暗色模式对比度**
4. **中文排版质量**（字体、行距、段间距）
5. **页面加载速度**（Lighthouse 测试）

### Step 5：决策
- 满意某个引擎 → 在主仓库创建迁移分支，全量迁移
- 都不满意 → 考虑 Docusaurus 或继续观望
- 随时可删 fork，零损失

---

## 六、全量迁移参考（POC 通过后）

### 当前项目规模

| 指标 | 数值 |
|------|------|
| Markdown 文件数 | 688 个 |
| 内容总量 | 12.5 MB / 121,797 行 |
| `.pages` 导航文件 | 55 个 |
| i18n 翻译文件 | 40 个 `.en.md` |
| 自定义 Hooks | 2 个（meta.py, i18n_banner.py） |
| 自定义 CSS/JS | extra.css (9.8KB) + mathjax.js |
| CI/CD | GitHub Actions（Python 3.11 → gh-pages） |

### 迁移步骤（确定引擎后）

1. **项目初始化** — 在主仓库新分支上搭建选定引擎
2. **内容搬迁** — AI 批量转换 frontmatter 和导航文件
3. **Markdown 语法适配** — 批量替换引擎特有语法
4. **功能重建** — 搜索、Mermaid、KaTeX、i18n、CI/CD
5. **视觉定制** — 品牌化配色和排版
6. **验证** — 全量链接检查、内容完整性、构建部署

### Markdown 语法转换对照

| MkDocs 语法 | 目标语法 |
|------------|---------|
| `!!! note` | `::: note` |
| `--8<-- "file"` | MDX import 或自定义插件 |
| `==text==` | `<mark>text</mark>` |
| `++key++` | `<kbd>key</kbd>` |
| `:emoji:` | Unicode emoji |

### 风险与缓解

| 风险 | 缓解措施 |
|------|----------|
| Markdown 语法批量转换有遗漏 | AI 转换后做 diff 审查，逐卷验证 |
| PyMdown snippets 广泛使用 | 先统计实际使用量再决定方案 |
| i18n 重构影响翻译文件 | 40 个文件量小，可控 |
| 构建链 Python → Node | CI 并行运行两套，确认无误后切换 |
| 团队不熟悉新生态 | POC 阶段已完成学习 |

---

## 七、Docusaurus 备选方案

如果 POC 发现 VitePress/Starlight 不满足需求，Docusaurus 仍是可靠备选：
- PenguinLab 已有成熟模板可复用
- 功能最全面（版本管理、博客、i18n）
- Meta 企业支撑，长期稳定

PenguinLab 参考文件：

| 文件 | 用途 |
|------|------|
| `PenguinLab/site/docusaurus.config.ts` | 完整配置模板 |
| `PenguinLab/site/package.json` | 依赖版本参考 |
| `PenguinLab/site/src/css/custom.css` | 151 行品牌化 CSS |
| `PenguinLab/site/sidebars.ts` | 侧边栏配置 |
| `PenguinLab/.github/workflows/deploy.yml` | CI/CD 配置 |
