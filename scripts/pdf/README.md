# PDF 书稿生成管线

本目录实现从 `documents/` 原始 Markdown 到分册 PDF 的独立出版管线。它不是网页截图：
不读取 VitePress 构建产物，不复用网站导航和主题，也不向 Pages 增加下载入口。Markdown
由 VitePress 的独立渲染器处理，书稿 HTML、CSS、浏览器运行时和分页过程均在本目录维护，
最终由 Puppeteer 驱动 Chromium 输出 PDF。

## 架构与构建阶段

一次书册构建沿下面的单向数据流执行：

```text
documents/**/*.md
  -> catalog：发现、frontmatter 解析、排序、canonical/docId 建模
  -> assets：复制本地资源和固定版本浏览器运行时
  -> markdown/transform：VitePress Markdown 渲染、组件与链接纸面化
  -> validate：严格 DOM allowlist、主标题、标识、锚点和组件残留检查
  -> template：封面 + 真实页码目录 + 全册正文的单一长 HTML
  -> runtime：字体/图片/mermaid/drawio 就绪屏障，Paged.js 一次分页
  -> browser：Chromium page.pdf()
  -> validate：PDF 签名、大小及可用工具的结构/字体/文本检查
```

主要文件职责如下：

| 文件 | 职责 |
| --- | --- |
| `books.ts` | 内容单元、15 个出版书册及中英文 locale 注册表 |
| `catalog.ts` | 递归发现 Markdown、gray-matter 解析、稳定排序、canonical lookup |
| `markdown.ts` | 创建 VitePress Markdown renderer；启用 Shiki、数学、kbd、mermaid |
| `transform.ts` | 方言审计、自定义组件转换、资源与标题 ID 规范化 |
| `links.ts` | 同册、跨册、外链和仓库源码链接分类 |
| `assets.ts` | 安全复制本地资源，暂存 Paged.js、mermaid 和固定 drawio viewer |
| `template.ts` | 组装独立书稿模板、封面、目录、正文和 running title 数据 |
| `styles/book.css` | B5 书稿样式、页眉页脚、中文正文、代码与图表分页规则 |
| `runtime/book-runtime.js` | 浏览器内资源屏障、图表渲染、分页和分页后断言 |
| `browser.ts` | 仅回环地址静态服务器、网络隔离、Puppeteer 和原子 PDF 写入 |
| `validate.ts` | HTML 元素/属性安全终检、标题/锚点 preflight 和 PDF postflight |
| `build-book.ts` | 串联上述阶段并写出构建报告 |
| `cli.ts` | 本地及 CI 共用的命令行入口 |

目录发现保留各内容单元的根 `index.md` 和嵌套 `index.md`，排除 `README.md`、
`tags.md`。根 index 先于正文；目录使用其 index 的 `sidebar_order`，文章使用
`chapter`、`order`，最后以数字感知的自然文件名稳定排序。单单元书册的根 index 是
`book-index`；多单元合辑的每个单元根 index 是 `chapter-index`，会进入目录。

## 15 个出版书册

仓库当前注册 19 个内容单元，但对外生成 15 个书册：

| 书册 ID | 中文书册 | 内容单元 | 源目录 |
| --- | --- | --- | --- |
| `getting-started` | 入门指南 · 从这里开始 | `getting-started` | `documents/getting-started/` |
| `vol1` | 卷一 · C++ 基础入门 | `vol1` | `documents/vol1-fundamentals/` |
| `vol2` | 卷二 · 现代 C++ 核心特性 | `vol2` | `documents/vol2-modern-features/` |
| `vol3` | 卷三 · 标准库 | `vol3` | `documents/vol3-standard-library/` |
| `vol4` | 卷四 · 高级主题 | `vol4` | `documents/vol4-advanced/` |
| `vol5` | 卷五 · 并发编程 | `vol5` | `documents/vol5-concurrency/` |
| `vol6` | 卷六 · 性能工程 | `vol6` | `documents/vol6-performance/` |
| `vol7` | 卷七 · 工程实践 | `vol7` | `documents/vol7-engineering/` |
| `vol8` | 卷八 · 领域实践 | `vol8` | `documents/vol8-domains/` |
| `vol9` | 卷九 · 开源项目研读 | `vol9` | `documents/vol9-open-source-project-learn/` |
| `vol10` | 卷十 · 课程与演讲笔记 | `vol10` | `documents/vol10-open-lecture-notes/` |
| `compilation` | 专题册 · 编译、链接与构建系统 | `compilation` | `documents/compilation/` |
| `crash-lab` | 实验册 · 崩溃实验室 | `crash-lab` | `documents/crash-lab/` |
| `cpp-reference` | 参考册 · Modern C++ 速查手册 | `cpp-reference` | `documents/cpp-reference/` |
| `supplement` | 附录合辑 · 项目、社区与附录 | `projects`、`community`、`roadmap`、`appendix`、`team` | 对应五个 `documents/<unit>/` 目录 |

`projects`、`community`、`roadmap`、`appendix`、`team` 内容较少，出版层将它们合并为
`supplement`，避免产生五本只有少量页面的小册。它们仍是独立内容单元，保留自己的 URL
命名空间和全仓链接解析能力，但不是可传给 `--book` 的独立书册 ID。

当前 `vol1` 没有拆成上下册，仍以一次完整分页生成一个 PDF。若真实构建数据证明需要拆分，
应增加明确、稳定的分册定义和内容范围；当前代码没有“按章分页后拼 PDF”的模式。

## 本地运行

基准环境是 Node.js 22 和 pnpm 10。先安装锁定依赖和 Puppeteer 对应的 Chrome：

```bash
pnpm install --frozen-lockfile
pnpm exec puppeteer browsers install chrome
```

查看书册列表：

```bash
pnpm pdf:list
pnpm pdf:list -- --json
```

生成默认中文书册；未给 `--book` 时默认是 `getting-started`：

```bash
pnpm pdf
pnpm pdf -- --book vol1
```

一次选择多个书册或全部书册：

```bash
pnpm pdf -- --book getting-started --book compilation
pnpm pdf -- --book all --language zh
```

先只组装和校验 HTML，不启动 Chromium：

```bash
pnpm pdf -- --book vol1 --html-only --keep-staging
```

为长卷调整浏览器/分页总超时，或指定已有 Chromium：

```bash
pnpm pdf -- --book vol1 --timeout 1800
pnpm pdf -- --book vol1 --executable-path /path/to/chrome
```

完整选项：

| 选项 | 当前行为 |
| --- | --- |
| `--book <id\|all>` | 可重复；默认 `getting-started` |
| `--language zh\|en` | 默认 `zh` |
| `--output <dir>` | PDF/JSON 报告目录，默认 `dist/pdf` |
| `--html-only` | 生成并校验 HTML，不运行 Chromium；暂存目录会保留 |
| `--keep-staging` | 完整 PDF 成功后仍保留 `.pdf-build/<book>-<language>/` |
| `--no-sandbox` | 关闭 Chromium sandbox；仅用于渲染受信任内容的隔离 CI 环境 |
| `--timeout <seconds>` | Puppeteer 启动、导航、分页和 PDF 输出各阶段的等待上限；默认每阶段 900 秒 |
| `--executable-path <path>` | 使用指定 Chromium 可执行文件 |

完整构建默认生成：

```text
dist/pdf/awesome-modern-cpp-<book>-<language>-<sha12>.pdf
dist/pdf/awesome-modern-cpp-<book>-<language>-<sha12>.json
```

JSON 报告记录源文档数、渲染数、转换统计、各阶段耗时、页数、PDF 字节数、工具版本和
warning。`.pdf-build/` 与 `dist/pdf/` 都在 `.gitignore` 中；产物不会进入 Git 或 Pages。
`--html-only` 是诊断构建，其报告写在
`.pdf-build/<book>-<language>/build-report.json`，不会覆盖同 revision 已发布 PDF 对应的报告。

运行单元测试：

```bash
pnpm test:pdf
```

## 方言转换规则

| 源元素 | 书稿处理 |
| --- | --- |
| frontmatter | 驱动标题、描述、章节、排序和目录；从正文移除 |
| `<ChapterNav>` | 连同网页导航块移除；其紧邻导航标题也会移除 |
| 独立 `<ChapterLink>` | 转为普通链接，再按同册或跨册规则重写 |
| `<OnlineCompilerDemo>` | 必须提供 `source-path`；读取仓库源码并生成静态 Shiki 代码块，可附 ARM 源码和在线版链接 |
| `<RefLink>` | 转为指向本篇参考条目的上标编号 |
| `<ReferenceCard>` / `<ReferenceItem>` | 转为参考资料 section/list；独立 item 也可转换 |
| `<TalkInfoCard>` | 转为适合纸面的讲座信息 aside |
| 站内 Markdown 链接 | 同册转 PDF 内锚点；跨册保留在线 URL，并生成去重的篇末脚注 |
| 仓库文件或目录链接 | 已存在文件转 GitHub `blob`，目录转 `tree`；兼容 VitePress 产生的 `README.html`、`foo.cpp.html` |
| 任何未解析的本地/站内链接 | 无论有无扩展名都直接失败，不降级为看似可用的外链 |
| code-fold | 书稿 renderer 不加载站点自动折叠插件，代码全部进入正常文档流 |
| `details` / code-group | details 强制 `open`；code-group 隐藏 tab 控件并展示所有代码变体 |
| mermaid | 保留源码数据，浏览器从本地模块渲染 SVG；失败或没有 SVG 则停止构建 |
| 数学公式 | VitePress `math: true` 在 Markdown 阶段渲染；展示公式禁止从中间分页 |
| `![](file.drawio)` | 复制源文件，使用固定版本本地 viewer 渲染；等待最终稳定 SVG 后才分页 |
| 本地图片 | 进行仓库边界/符号链接检查后复制到哈希命名资源目录，独立图片包装为 figure |
| 远程图片 | 不在 Chromium 中联网抓取，替换为带原 URL 的纸面占位说明 |
| `Number<T>` 等 C++ 尖括号 | 在普通文本中转义；代码围栏、缩进代码和行内代码不改动 |
| 有限的网页语境文案 | 对 OnlineCompilerDemo 邻近的已知中英文句式进行确定性纸面改写，并计入 `paperContext` |

代码使用 VitePress `createMarkdownRenderer`，启用浅色 `github-light` Shiki 主题、数学、kbd、
mermaid 及项目的 C++ 模板转义插件。书稿样式与网站主题完全分离。代码块允许在 Shiki
`.line` 之间分页，每一源代码行保持不可拆；转换阶段给每行物化稳定行号，Paged.js 克隆和
跨页后仍连续。超长行会换行，而不是裁掉或制造横向滚动区。

## Fail-closed 保证

管线的默认策略是“无法证明完整就失败”，而不是生成带缺页或残留标签的 PDF。

源和 HTML 阶段会检查：

- canonical path、docId、书内 HTML id 是否重复；
- 每篇渲染结果是否保留可用的主标题和可见内容；
- 未知成对组件、带属性组件、自闭合组件和独立 PascalCase 组件；
- 最终 DOM 是否只含允许的被动 HTML，以及 renderer 所属的 MathJax MathML/SVG；
- `on*`、`srcdoc`、Vue 指令、可联网的 URL 属性或危险链接 scheme；
- 已知组件或 Vue 指令在转换后是否仍有残留；
- 同册锚点是否存在，clean URL、`.md`、`.html` 内链是否可解析；
- demo 源码、本地图片是否存在，路径及符号链接是否仍位于仓库内。

浏览器阶段只允许回环静态服务器以及 `about:`、`blob:`、`data:` 请求。任何外部请求、
HTTP 错误、请求失败、页面异常或 console error 都会使构建失败。`window.__BOOK_READY__`
只在以下工作全部成功后完成：

1. 样式、字体和初始图片加载/解码完成；
2. mermaid 和 drawio 产生有效 SVG；
3. 图表新增的图片和字体再次就绪；
4. 全部内部目标和源文档 sentinel 存在；
5. Paged.js 显式执行一次分页；
6. 分页前后的文档尾 sentinel 未丢失，drawio 未丢失；
7. 没有空白分页、横向溢出或页数不一致。

Chromium 返回值还需具有 `%PDF-` 签名且大于最小尺寸。若本机存在 `qpdf`、`pdfinfo`、
`pdffonts`、`pdftotext`，postflight 会继续做结构、页数、字体和文本检查；缺少这些命令在
本地报告中是 warning，而 CI 会显式安装并执行更严格的验证。

这套断言不能自动判断所有视觉问题。例如 CJK 行首/行尾禁则依赖 Chromium 的
`line-break: strict`，仍需在验收时抽查真实页面。

## 排版和超长卷

首期采用“现代技术书 + 教材级中文细节”的书风，而不是高密度论文版式。书稿为
176 mm × 250 mm 的 B5 页面，宋体正文、无衬线标题，以及 Cascadia Mono（中文回退
Noto Sans Mono CJK SC）代码字体。封面只显示系列名、卷名、书名、工作室与年份，不显示
版本号、提交 SHA 和长 URL，也不显示页眉页脚；目录和正文使用左右页镜像边距、running book/chapter title 和页码。目录页码由
Paged.js 的 `target-counter(attr(href), page)` 从最终分页结果回填。章索引和检测到的新章可
换页，正文设置 widows/orphans；表格可在行间分页并重复表头，单行保持完整；图表受版心宽度
和可用高度限制。

当前实现刻意把一个书册组装为一份长 HTML，并调用一次 `PagedPolyfill.preview()`。这是保证
目录真实页码、全册页码计数、running header 和跨章目标一致的前提。当前没有按章分片、页码
偏移回填或 PDF 拼接代码，也不应在外部把逐章 PDF 静默拼接成“整卷”。

对于 `vol1` 等超长卷，当前可操作流程是：

1. 先用 `--html-only --keep-staging` 排除目录、组件和链接错误；
2. 再运行完整构建，并按需要提高 `--timeout`；若 Node 堆不足，可由调用环境设置合适的
   `NODE_OPTIONS=--max-old-space-size=...`；
3. 根据 JSON 报告里的 `elapsedMs`、`pageCount`、`pdfBytes` 和 warning 做真实机器/CI 基准，
   不以文章数臆测页数或时长；
4. 保留 `.pdf-build/` 排查具体 HTML、资源和分页错误。

本机在 revision `8da6d4f3aa70` 的一次完整基准为：`vol1` 共 103 篇、1199 页、
29,642,526 字节；Chromium 阶段 253.756 秒，总耗时 260.007 秒，7/7 个 drawio 均通过
分页后完整性断言。环境为 Node 26.7.0、Chrome 151；这不是 Node 22 GitHub runner 的性能
承诺，且该次没有可靠采到 Node + Chrome 进程树峰值 RSS。

这组数据支持首期继续采用“整卷一次分页”，暂不拆 `vol1` 上下册。CI 给每个书册 job
180 分钟总时限、Node 4 GiB heap，书册矩阵最多并行 3 个，浏览器各阶段默认上限 900 秒。
Node 22 CI 的耗时和峰值 RSS 仍应在首次 workflow artifact 中记录。若真实 CI 数据持续超过
单次 Chromium 能力，应先设计可验证的“上/下册”内容边界和独立 book ID，再实现各册各自
完整目录；不要牺牲完整性断言换取按章分页后静默拼接。

## 字体、浏览器和外部工具

CSS 首选字体为：

- 正文：`Noto Serif CJK SC` / `Source Han Serif SC`；
- 标题：`Noto Sans CJK SC` / `Source Han Sans SC`；
- 代码：Noto/思源等宽字体，回退到 `DejaVu Sans Mono`。

Ubuntu/WSL2 推荐与 CI 对齐：

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends -y fonts-noto-cjk-extra poppler-utils qpdf
fc-cache -f
fc-match 'Noto Serif CJK SC'
```

Chromium 在 `page.pdf()` 时嵌入实际使用的字体。CI 使用 `pdffonts` 要求所有字体行显示已嵌入，
并用 `pdftotext` 检查文本不是空壳。不要只依靠浏览器字体回退；缺少衬线 CJK 字体会改变
换行和页码。

Paged.js 和 mermaid 从 `node_modules` 复制到暂存目录。包含 drawio 的书册第一次构建时，
Node 阶段会下载固定版本 `viewer-static.min.js`，校验硬编码 SHA-256 后缓存到 `.cache/pdf/`；
浏览器阶段仍不访问网络。离线构建可预置该精确文件，并设置：

```bash
DRAWIO_VIEWER_PATH=/absolute/path/to/viewer-static.min.js pnpm pdf -- --book <id>
```

版本或校验和不一致会直接失败，不能用任意 viewer 替代。

## GitHub Actions 和 rolling Release

`.github/workflows/pdf.yml` 支持两种触发方式。手动 `workflow_dispatch` 的输入为：

- `book`：一个书册 ID 或 `all`，默认 `all`；
- `language`：`zh` 或 `en`；
- `publish`：是否更新固定 tag `pdf-latest`。

PR 可由维护者添加 `export-pdf` 标签，自动构建全部 15 本中文书并上传为 Actions artifacts，
但不会更新 `pdf-latest`。标签是一次性触发器：PR 新提交或重新打开不会自动重建；需要再次导出时，
先移除再重新添加该标签。同一 PR 正在运行的旧导出会自动取消。未触发标签事件的 PR 不会创建
PDF workflow。

plan job 从 `pnpm pdf:list --json` 动态生成矩阵并校验 ID。build job 使用 `ubuntu-latest`、
Node 22、pnpm 10，安装匹配 Chrome、Cascadia Mono、CJK 衬线字体、qpdf 和 Poppler；每个矩阵项只构建一本，
最多并行 3 本。每本 PDF 必须通过 `qpdf --check`、有效页数、字体嵌入和非空文本检查，随后
连同 JSON 报告作为保留 14 天的 Actions artifact 上传。

首期 `publish=true` 还有额外约束：

1. `book` 必须是 `all`、`language` 必须是 `zh`，保证首期 15 本中文书来自同一 revision；
2. 只能从默认分支发布；候选上传前和固定 tag 移动前都会拒绝已落后的 revision；
3. 所有预期 PDF 必须完整且无重复；PDF 的 Release 资源名保持人类可读
   （`<book>-<language>-<yyyymmdd>-r<run>.pdf`：日期是源提交的 committer 日期
   （UTC），`r<run>` 是 PDF workflow 的 run number——Chromium 输出跨次构建不可
   复现，靠它保证每次发布都拿到全新名字，同名只可能来自同一次 run 的字节一致
   产物；精确的 revision SHA 记录在 manifest 里），完整性由 manifest 与
   `SHA256SUMS` 中的文件级 SHA-256 绑定，校验步骤按 manifest 核对远端 digest；
4. 严格按 PDF、`SHA256SUMS`、manifest 的顺序上传，并核对远端名称、大小及可用的 digest；
5. 首次发布先建不可见 draft；已有公开 Release 则保持上一版可用，直到候选集完整通过；
6. 所有检查通过后才移动固定 tag `pdf-latest`；失败只删除本次成功上传并正向记录的资源，
   不使用 `--clobber`，也不触碰上一版资源；
7. tag 提交成功后再以 best-effort 删除管线拥有的旧 PDF、manifest 和 checksum 资源。

Release 标记为 rolling、不是 GitHub 的 “latest release”。CI artifact 和 Release 都与 Pages
部署分离。英文进入正式发布后，应显式调整 plan 的语言策略并重新定义同一固定 Release 是
“单语言完整集”还是“中英双语同 revision 完整集”，不能把不同 revision 静默混放。

## 英文开关

英文使用相同的 15 册映射和管线：

```bash
pnpm pdf -- --book vol3 --language en
pnpm pdf -- --book all --language en
```

它读取 `documents/en/<sourceDir>/`，canonical 路径带 `/en/` 前缀；中文绝对链接若残留在翻译稿
中，链接 resolver 会优先尝试本 locale 的 `/en/...` 目标。英文继续使用同一书稿设计，但 CSS
启用自动断词并改为左对齐。当前 workflow 允许英文单册/全册构建和 artifact，但首期明确拒绝
英文 `publish=true`。支持开关不等于内容已完成编辑验收；英文书仍应逐册执行下面的验收流程。

## 新增自定义组件

不要只把组件名加入 allowlist。新增组件必须同时完成打印语义和残留断言：

1. 盘点源语法、属性、自闭合/成对形式、是否含文件路径或交互状态；
2. 在 `transform.ts` 的 `KNOWN_COMPONENTS` 注册名称，并实现明确的静态 HTML 转换；
3. 如需统计，在 `model.ts` 的 `TransformStats`、空统计和报告汇总中加入字段；
4. 在 `validate.ts` 的组件名单加入名称，保证 PDF 文本/HTML 残留可检测；
5. 为已知形式、未知近似形式、自闭合展开和最终无残留增加 `scripts/pdf/test/` 测试；
6. 在 `styles/book.css` 增加纸面样式和分页规则；
7. 若组件读取资源，复用仓库边界和 canonical symlink 检查，禁止任意路径或浏览器联网；
8. 更新本文转换规则，并完成真实书册验收。

普通 HTML 也不是任意放行。只有 `PASSIVE_HTML` 中的静态标签可直接保留，并继续拒绝事件处理
属性和 `srcdoc`。无法判断是 C++ 模板还是组件的尖括号语法必须先加测试，不能通过扩大正则
静默绕过。

## 验收与排错

推荐从小到大执行：

```bash
pnpm test:pdf
pnpm pdf -- --book getting-started --html-only --keep-staging
pnpm pdf -- --book getting-started --keep-staging
```

合入前至少确认：

- 单元测试通过；
- HTML 阶段没有未知组件、Vue 残留、重复 ID、失效锚点或未解析内链；
- PDF 报告没有未解释 warning，页数与 Paged.js DOM 一致；
- qpdf、pdfinfo、pdffonts、pdftotext 检查通过；
- 抽查目录页码、页眉章名、同册跳转、跨册脚注；
- 抽查 Shiki 代码、长代码跨页、mermaid、公式、drawio、本地及远程图片；
- 抽查 CJK 行首/行尾禁则、空白页、裁切和横向溢出；
- CI 单册 artifact 通过；只有完整 15 册同 revision 时才启用 publish。

常见错误定位：

| 错误特征 | 处理方向 |
| --- | --- |
| `unknown component ... file:line` | 确认是 C++ 尖括号还是新组件；为新组件实现转换和测试，不直接跳过 |
| `unresolved internal link` | 修正相对路径、扩展名或 canonical；仓库文件/目录必须真实存在 |
| `missing demo source` / `Missing local asset` | 检查路径、大小写、翻译镜像及符号链接边界 |
| drawio 下载或 checksum 失败 | 使用匹配固定版本和哈希的缓存，或设置正确的 `DRAWIO_VIEWER_PATH` |
| `window.__BOOK_READY__ timed out` | 保留 staging，先看具体资源/图表/分页异常；仅总分页较慢时提高 `--timeout` |
| `blocked external requests` | 将运行期依赖暂存为本地资源；不要放宽 Chromium 网络策略 |
| `Horizontal overflow after pagination` | 检查报告所列页面和元素，修正源长 token、表格或专用书稿 CSS |
| sentinel、空页或页数不一致 | 视为内容丢失风险，不发布；用小册/HTML staging 缩小触发范围 |
| 字体 warning 或页码漂移 | 安装并确认 Noto Serif CJK SC，刷新字体缓存后重建 |
| 本地缺少 qpdf/Poppler | 安装工具后重跑；CI 不会跳过这些检查 |

`--timeout` 调整 Puppeteer 各主要阶段的等待上限，并不是覆盖全流程的一只总计时器；浏览器
运行时对单项资源还有自己的 60 秒屏障。
若报错明确指向资源加载或 drawio，单纯放大 `--timeout` 不会修复根因。
