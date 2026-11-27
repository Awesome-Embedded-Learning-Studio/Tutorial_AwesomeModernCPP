---
id: "003"
title: "PDF 书稿细节打磨"
category: content
priority: P2
status: draft
created: 2026-11-25
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# PDF 书稿细节打磨

本大纲规划一件事：把 PDF 书稿现有的七处细节磨平。书稿的版式本身已经到位，这里不动版式，不重做封面，也不给 PDF 套站点的主题。排在 `002` 之后做。

证据来自 2026-11-25 的一次检查：卷一的现成产物（`dist/pdf/`，本地构建，1067 页，B5）渲染成图看了封面、目录、扉页、代码页和前 40 页总览，另外对 `book.css`、`template.ts`、`book-runtime.js` 做了一遍规则级审计。

此外，还要优化一下PDF是否存在页面Render溢出的问题。

## Current Assets

- 管线在 `scripts/pdf/`，从 Markdown 渲染 HTML，Paged.js 在 Chromium 里分页，产出 15 个书册。它不读站点产物，样式全在 `styles/book.css`。
- 版式到位的部分：封面是细线框加衬线标题。目录有引导线和页码。奇偶页的页眉镜像，页码居中。正文两端对齐，`line-break: strict` 管避头尾，`orphans` 和 `widows` 有设置。代码块带行号和边框。drawio 图渲染成矢量。
- 分页后的运行时断言已经覆盖：页数与 Paged.js 报告一致，每篇文档尾的标记落在页框内，mermaid 和 drawio 有可见 SVG，代码和表格行的分片完整性，空白页，横向和纵向溢出，目录页码等于目标实际页。标题落在页底的情况，审计扫了 673 页，零命中。
- 图片正常：入门册 18 处引用对应 19 张位图，卷一的 14 张 drawio 渲染成矢量、1 张 png 在。整本没有空白页，文字都能抽取。

## Gaps

三处重的，都在字体和标点这一层：

- 全部中文字体被嵌成 Type 3。卷一 670 个字体对象里 669 个是 Type 3，卷二 656 个里 655 个，唯一正常的是桌面回退字体 Adwaita-Mono。字体在 `book.css` 里只按 family 名引用（21 到 32 行），`scripts/pdf` 里没有 `@font-face` 也没有字体文件，全靠系统字体。CI 在 `pdf.yml` 第 186 行装的是 `fonts-noto-cjk-extra`。后果是体积：卷一每页 29KB，入门册每页 65KB，估计比正常的 CID 嵌入重四到五倍。文字能搜能复制。`validate.ts` 第 243 到 251 行只判 `emb` 列，Type 3 照过。根因现有数据分不清是合集里的 CFF 轮廓触发，还是 `.ttc` 合集本身触发，要做一次对照构建。
- 代码字体在本地和 CI 不一致。本机没装 Cascadia Mono 和 Noto Sans Mono，本地产物的代码全用了 NotoSansMonoCJKsc，正是 `book.css` 25 到 30 行注释想避开的情况。CI 装了这两个字体，所以 CI 产物不同。同一个原因让 Adwaita-Mono 混了进来。
- 全角标点后面留了 ASCII 空格。源文件里「，**粗体**」「：`代码`」这种写法，标点和后面的行内标记之间多了一个空格。卷一有 936 行，入门册 105 行。渲染层没有处理这个的规则。印出来空档比标点直接接文字宽。宽多少，以及这版 Chromium 的自动中英间距会不会和它叠加，动手时要先用一页对照样张核定。

四处轻的：

- `orphans` 和 `widows` 只有 2（`book.css` 367 到 382 行），管不到整页只剩一行。实测卷二第 130 页只有脚注一行。
- drawio 图的 SVG 被 `width: 100% !important` 拉满版心（`book.css` 795 到 800 行，`book-runtime.js` 285、302 行），小图会被放大。运行时其实已经拿到原始像素宽（`book-runtime.js` 279 到 283 行）。
- 两端对齐只写在 `p` 上（`book.css` 387 行），列表项是左对齐，右边参差。`text-justify` 在 Chromium 里不生效。
- 有小于 7pt 的字：表格里的行内代码 6.97pt，`kbd` 6.64pt，参考卡片的角标 6.34pt。

断言没覆盖的：孤行寡行，页底落单的标题，短页，字体类型，图与图注是否同页。前三项现在的产物没出问题，只是没人盯着。

## Scope

- 在范围内：`styles/book.css`、`runtime/book-runtime.js`、`markdown.ts` 与 `transform.ts` 里的文本处理、`validate.ts` 的断言、字体文件的引入。
- 不在范围内：版式重做，封面设计，给 PDF 套站点主题，正文内容的修改。
- 标点空格倾向在渲染层兜底，不去改 936 行源文件。具体做不做，等对照样张的数字。

## 方案

### 一、字体

1. 先做对照构建定根因。用单文件 TrueType 轮廓的 Noto 和单文件 CFF 轮廓的 Noto 各构建一次入门册，看 `pdffonts` 的类型列。
2. 把衬线、黑体、等宽三套字体以单文件形式随书稿暂存，`assets.ts` 负责复制，`book.css` 从按 family 名引用改成 `@font-face`。字体来源和许可写进 `scripts/pdf/README.md`。
3. `validate.ts` 加字体类型断言，`pdffonts` 的类型列不许出现 Type 3。
4. 把改动前后的体积写进构建报告。

### 二、标点后面的空格

1. 先做一页对照样张。同一段文字，标点后带空格和不带空格各渲染一次，量出空档宽度，顺便看 Chromium 的自动中英间距有没有叠加。
2. 对照结果确认要吃掉空格，就在 Markdown 渲染层加一条文本规则：全角标点（逗号、句号、冒号、分号、叹号、问号、顿号）与紧随其后的行内标记（粗体、行内代码、`kbd`）之间的 ASCII 空格吃掉。代码块内不处理。
3. 给这条规则加单测，放进 `scripts/pdf/test/`。

### 三、四处轻的

1. `orphans` 和 `widows` 提到 3，运行时加短页断言：一页里正文行数少于阈值就警告，章末页除外。
2. drawio 的 SVG 改成写原始 `width` 再配 `max-width: 100%`，去掉 `!important`。
3. `.book-content li` 补 `text-align: justify`。
4. 低于 7pt 的字号抬到 7pt：表格行内代码、`kbd`、参考卡片角标。

### 四、断言补齐

- 字体类型（第一部分第 3 项）。
- 短页（第三部分第 1 项）。
- 页底落单的标题。现在零命中，加断言是为了防回归。
- 图与图注同页。

## Dependencies

- 第一部分第 2 项依赖第 1 项的对照结果，先定根因再选字体文件。
- 第二部分第 2 项依赖第 1 项的对照样张，先量再改。
- 第三、四部分和前两部分互不依赖，可以并行。
- 每一处改完都要跑 `pnpm test:pdf`，并至少重新构建一次入门册对照。

## Acceptance

- [ ] `pdffonts` 的类型列里 Type 3 为 0，三套字体都按 `@font-face` 嵌入。
- [ ] 本地和 CI 构建出来的字体列表一致，不再出现 Adwaita-Mono，代码字体是清单里指定的那一个。
- [ ] 构建报告里有改动前后的体积对比。
- [ ] 全角标点后空格的处理有对照样张和单测，样例页上标点接粗体或代码的空档，和标点直接接文字时一致。
- [ ] 入门册和卷二重新分页后，没有只剩一行的页，章末页除外，短页断言能拦住它。
- [ ] 小尺寸的 drawio 图不再被拉满版心。
- [ ] 列表项两端对齐。
- [ ] 全书没有小于 7pt 的字。
- [ ] `pnpm test:pdf` 通过，`pdf.yml` 的预检全绿。

## 附：证据位置

渲染图在 `.cache/pdf-visual/vol1/`，规则级审计在 `.cache/theme-audit/pdf-css-audit.md`。`.cache` 不入库，要长期留的事实已经摘进本文件的 Current Assets 和 Gaps。
