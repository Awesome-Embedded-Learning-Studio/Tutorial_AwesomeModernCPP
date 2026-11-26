---
id: "002"
title: "站点主题包与可切换外观大纲"
category: content
priority: P1
status: draft
created: 2026-11-25
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: epic
---

# 站点主题包与可切换外观大纲

本大纲规划一件事：让 VitePress 站点支持多套可切换的主题包，并且读者能在浏览器里导入自己的主题包。站点只带一个默认主题，就是现有的外观，整理成「TAMCPP 默认主题」。Minecraft（下称 MC）不进站点产物，它是一份示例主题，放在仓库里给外部作者当样板，同时用来检验浏览器的导入功能。PDF 的外观不在本大纲范围内，另行讨论。

主题能接手的东西将分两类。一类是全站声明，比如颜色、字体、圆角。另一类是站点将定义的一组有名字的「主题组件」，比如封面插画、进度格、阅读进度条、首页视觉。主题组件由站点定义，不绑定某个页面，接口按全站设计。目前只有周练页面会用到其中两个，本大纲拿它当例子，这套接口不是为周练单独设计的。两类东西落地时分声明、样式、行为三层，见「方案」。

## Current Assets

- 颜色体系已经大半变量化。站点基础色亮色 31 个、暗色重定义其中 19 个，定义在 `custom.css`。正文排版有四组变量（代码块、引用、表格、列表），亮暗各一套。周练有 4 个颜色变量，其中 `--weekly-ink` 只定义未引用。首页终端有 11 个。
- 50 个组件里 35 个带样式块，全部是 scoped，其中 15 个完全走变量。
- 亮暗切换由 VitePress 自带机制负责：`<html>` 上的 `dark` 类，localStorage 键 `vitepress-theme-appearance`，构建期注入的内联脚本负责首屏恢复。
- 首屏恢复有现成先例。字号和侧栏宽度的内联脚本写在 `config/shared.ts` 和 `config/index.ts` 两处，组件启动后读同一个键。
- CSS 从 `theme/index.ts` 引入，分卷构建后 40 卷合成一份 `style.<hash>.css`。

## Gaps

绕开 CSS 变量、主题包接不到的地方：

- 封面插画组件 `WeeklyCoverArt.vue` 把插画和期号画在一起，10 个颜色写死在 CSS 和 SVG 属性里。
- 题目页周边在 `weekly.css` 有 26 处写死，包括判定色、编辑器条和主按钮。
- 首页终端 `HomeHeroVisual.vue` 有 9 处颜色写死。
- 在线编辑器 `utils/cpp-editor.ts` 的 CodeMirror 配色写死，标记为深色。
- Mermaid 固定 `theme: 'default'`，整个应用只初始化一次，不跟随亮暗。
- 在线编译器的 Shiki 高亮在运行时用 `github-light/dark`，与正文构建期的 `article-light/dark` 不同色。
- `StatusToast.vue` 和 `TalkInfoCard.vue` 把颜色放在 JS 对象里，写进内联 style。
- 侧栏底色 `--vp-c-bg-alt` 没有覆盖，仍是 VitePress 默认的冷灰。
- `<meta name="theme-color">` 是静态两条，跟系统偏好。
- drawio 插件 `darkMode: 'auto'`，跟系统偏好。

机制上的限制：

- VitePress 一个站点只有一个主题入口，没有多主题概念。
- 代码高亮在构建期只烙 `--shiki-light` 和 `--shiki-dark` 两组颜色，第三套 token 配色无法靠 CSS 切换。
- 首屏恢复脚本在两处配置里各有一份副本，新脚本也要放两处。
- localStorage 的容量通常只有 5MB，带字体和图片的主题包放不下。
- `scripts/build.ts` 的输入哈希只覆盖 `site/.vitepress`。哈希内的任何改动都会让 41 个构建任务全部重建，哈希外的改动不会触发重建。

## Scope

- 在范围内：主题包的源码工程格式与发布包格式，站点侧的主题运行时（负责读取、校验、切换和持久化主题），浏览器里导入、选择和删除主题的功能，从现有页面元素里抽出的主题组件，TAMCPP 默认主题，一份主题工程模板，一份 MC 示例主题。
- 不在范围内：PDF 外观，主题的在线分发与下载站，Mermaid 和 drawio 图内的配色，布局宽度、字号档位、侧栏宽度这些读者设置。
- 站点只内置 TAMCPP 一个主题。其他主题都走导入，MC 也不例外。
- 第四部分会起一套只改强调色的测试主题，它是临时验证用的，做完不留在仓库里。

## 方案

本节和以下各节描述的都是要做的东西。提到的 `site/themes/`、主题运行时、导入界面、切换器、生成的样式，现在都还没有建立。

### 主题包的两种形态：源码工程与发布包

主题包分源码工程和发布包两种形态。作者在源码工程里写多个文件，可以用 TypeScript 和预处理器，按组件分目录，构建之后产出一个发布包。站点和读者只认发布包。VitePress 官方文档给的自定义主题分发做法也是这个分法。

发布包是一个目录，也可以打成一个 zip：

| 文件 | 必要性 | 内容 |
|---|---|---|
| `theme.json` | 必须 | 清单：格式版本、标识、展示名、继承来源、亮暗两组声明、主题组件的覆盖 |
| `style.css` | 必须，可以是空文件 | 样式层产物，只写在站点基础样式之上的增量 |
| `main.js` | 可选 | 行为层产物，导出 `setup(ctx)` 和 `teardown()` |
| `assets/` | 可选 | 图片和字体文件，路径相对 `theme.json` |

发布包只留一个 CSS 入口和一个 JS 入口，因为运行时注入需要可预测的入口，导入也只能带进一个包。作者要分文件、用预处理器、引 npm 包，这些都在源码工程里做，由构建工具合成这一个入口。导入的包不能带异步 chunk，入口之外的文件不会被加载。

仓库将提供两样东西。`site/themes/starter/` 是模板，带库模式的 Vite 配置，一条命令产出能通过校验的发布包。`site/themes/examples/minecraft/` 是 MC 示例的源码工程，从模板起，构建出 zip 供读者导入。两处都在 `site/.vitepress` 之外，改动不会触发站点重建。

TAMCPP 走的是同一种清单，但不单独起工程。它的清单放在 `site/.vitepress/theme/tamcpp.theme.json`，样式就是站点的基础样式，不需要 `style.css`。站点构建和 dev 启动的最前面跑一条生成脚本，读这份清单，产出不带作用域的 `:root` 和 `.dark` 块，作为全站的底。现在 `custom.css` 里那两块变成生成物。

### 站点基础样式与主题包的边界

站点自己的组件样式，`custom.css`、`weekly.css`、`article-*.css` 这些，不属于任何主题包。它们始终加载，不加作用域。导入主题的 `style.css` 只写在这些基础样式之上的增量，整段包在 `html[data-theme='<id>']` 作用域里，靠选择器优先级盖过底。

这样分有三个结果。TAMCPP 不需要 `style.css`，它的身份全在声明层的变量里。一份只改颜色、字体和圆角的主题，其余样式自然沿用站点的基础样式。`extends` 在第一版只管声明层的继承，样式层的链式继承不做。

### 主题组件

站点将定义一组有名字的主题组件。每个组件有名字、站点给的默认实现（就是 TAMCPP 现在的样子）、主题能覆盖的部分、用在哪些页面。

| 组件名 | 默认实现 | 主题能覆盖 | 用在哪 |
|---|---|---|---|
| `cover-art` | `WeeklyCoverArt.vue` 的网格插画 | 亮暗各一张图片，带脚本的主题可以整个换掉组件 | 周练封面、周练首页 |
| `progress-segments` | 由使用处指定变体：首页周练卡用 `segments`（细线段），周练封面页用 `bar`（连续填充条） | 变体（`segments`、`bar`、`slots`），主题设了就作用于所有使用处。三种状态的颜色 | 首页周练卡、周练封面页的进度条 |
| `reading-progress` | `ReadingProgress.vue` 的连续线 | 变体（`line` 或 `segments`）、颜色 | 所有页面顶部 |
| `hero-visual` | `HomeHeroVisual.vue` 的代码终端 | 亮暗各一张图片，或整个换掉组件 | 首页右侧 |
| `code-block` | VitePress 代码块加 `article-code.css` | 外框、背景、字体、行号。行为层可以注册增强器 | 所有正文 |

第一期抽出前四个。`code-block` 本体是 VitePress 的，只开放样式层覆盖和行为层的增强钩子，token 配色换不了。

主题组件的覆盖写在 `theme.json` 的 `components` 字段里，按组件名分组。每个主题组件的根元素将带 `tp-<组件名>` 类，`style.css` 可以在这个类下写规则。带 `main.js` 的主题可以用 `ctx.override('<组件名>', Component)` 直接换掉组件实现。

### 清单的字段

`theme.json` 的顶层字段：`format`、`id`、`name`、`version` 必填。`extends` 可选，TAMCPP 自己不写。`modes` 必填，亮暗两组都要有 `colors`。`common` 可选，放亮暗共用的东西。

字段按用途命名，不按颜色名命名，也不按页面名命名。比如周练栏目那个琥珀色，角色名是 `accent`，TAMCPP 自己的清单把它映射到 `--weekly-amber`。没填的字段从 `extends` 指向的主题继承。字段分四组：

- `colors`：底色、文字、边框、链接、按钮、强调、成功、警告、危险这九个角色必填，其余角色可选。角色是开放集合，TAMCPP 自己的清单会把它现有的 31 个变量列全。
- `fonts`：正文与代码字体，可以带字体文件。
- `shape`：卡片和按钮圆角、边框粗细这类全站形状。
- `components`：各主题组件的覆盖，见上一节。

`colors` 和 `components` 里的图片必须写在 `modes.light` 和 `modes.dark` 下。`fonts`、`shape` 和 `components` 里的变体亮暗通常一样，可以写在 `common` 里，模式里没写就用 `common` 的。

示例（MC 主题节选）：

```json
{
  "format": 1,
  "id": "minecraft",
  "name": "Minecraft",
  "version": "1.0.0",
  "extends": "tamcpp",
  "common": {
    "fonts": { "body": { "family": "TAMCPP Pixel", "file": "assets/pixel.woff2" } },
    "shape": { "cardRadius": "0px", "borderWidth": "2px" },
    "components": {
      "progress-segments": { "variant": "slots" },
      "reading-progress": { "variant": "segments" }
    }
  },
  "modes": {
    "light": {
      "colors": { "bg": "#e8f0e8", "text1": "#1f2a1f", "link": "#3b8a3b", "accent": "#7a5a2d" },
      "components": {
        "cover-art": { "image": "assets/cover-light.svg" },
        "hero-visual": { "image": "assets/bench-light.svg" }
      }
    },
    "dark": { "...": "同一套结构，填暗色的值" }
  }
}
```

### 三层能力

1. 声明层：`colors`、`fonts`、`shape`、`components`。填了就生效。
2. 样式层：`style.css`。可以写全站规则和主题组件根类名下的规则，站点自动加作用域。导入的包要先过选择器检查，不许改布局宽度、侧栏宽度这些读者设置的变量。
3. 行为层：`main.js`。TAMCPP 不用。导入的包带了它，导入时要过授权提示，见「浏览器导入」一节。接口有五样：
   - 读当前亮暗。
   - 监听亮暗变化。
   - 监听路由切换。
   - 注册代码块增强器。
   - 按组件名换掉组件实现。

### 站点侧的运行时

- 主题入口仍然只有一个 `theme/index.ts`。它要加载主题运行时，运行时按 `<html>` 上的 `data-theme` 属性应用主题。
- 主题列表由两部分组成：TAMCPP，加上读者导入过的主题。导入过的主题从浏览器存储里读出来，启动时注册进运行时。
- 选主题和选亮暗分开保存。主题用新键 `vp-theme`，亮暗沿用 VitePress 的 `vitepress-theme-appearance` 和 `dark` 类。
- 首屏恢复脚本读 `vp-theme`，在首屏绘制前设好 `data-theme`。脚本逻辑写成一个共享常量，`config/shared.ts` 和 `config/index.ts` 各自引用这个常量。抽取之前才需要在两处各写一份。
- 切换器要挂在 `nav-bar-content-after` 和 `nav-screen-content-after` 两个布局插槽，与字号切换器并列。多个实例共用一个模块级的响应式状态，不走 `FontSizeSwitcher` 各持一份的做法。
- 切换时按顺序做四件事：
   1. 把选择写进 localStorage。
   2. 改 `<html>` 上的 `data-theme`。
   3. 调用旧主题的 `teardown()`，再调用新主题的 `setup()`。
   4. 更新 `<meta name="theme-color">`。

### 浏览器导入

导入功能进第一版，MC 示例就是它的测试用例。

- 入口在切换器里。「导入主题」打开文件选择，接受 zip 或单个 `theme.json`。「试试 MC 示例」从仓库下载示例的发布包，地址待定。
- 校验用和构建期读 TAMCPP 清单的是同一份代码。检查 `format` 版本、必填字段、颜色格式，`assets` 里的路径不许越出包目录，`style.css` 过选择器检查。校验不过就拒绝，并说明是哪个字段。
- 存储用 IndexedDB，键是主题 `id`。不用 localStorage 是因为字体和图片会超过它的容量。同一个 `id` 再导入时提示是否替换。
- 首屏：声明层生成的变量另存一份到 localStorage，首屏脚本把这份变量内联写到 `<html>` 上，这样颜色在首屏就是对的。`style.css`、图片、字体和 `main.js` 要等应用启动后才注入，所以导入主题的自定义样式和图片会晚一拍出现，这是已知的第一版限制。
- 脚本：含 `main.js` 的包在导入时明确提示「包含脚本」，读者确认后才保存，切换到它的时候才执行。主题列表里带脚本的主题有标记，随时可以删除。MC 示例带 `main.js`，所以这条路会被测到。`main.js` 以模块脚本加 blob 地址的方式加载，拿到的 `ctx` 与清单里定义的一致。
- 卸载：删除主题时清掉它的存储记录。删的是当前选中的主题，就回到 TAMCPP。

### 站点侧要先留接口的地方

分两组。第一组是从现有页面元素里抽出主题组件：

| 位置 | 现在 | 要改成 |
|---|---|---|
| `WeeklyCoverArt.vue` | 插画和期号在同一个组件 | 抽成 `cover-art`，期号和题名留在站点层叠上去 |
| `.weekly-widget__segments`、`.weekly-progress-track` | 两处的尺寸和形状各自写死 | 抽成 `progress-segments`，两处共用，变体按使用处给默认值 |
| `ReadingProgress.vue` | 形状写死为连续线 | 抽成 `reading-progress`，加变体 |
| `HomeHeroVisual.vue` | 终端画在组件里，9 处颜色写死 | 抽成 `hero-visual`，默认实现保留终端 |

第二组是把写死的颜色收进变量，或者改成读变量：

| 位置 | 现在 | 要改成 |
|---|---|---|
| `QuizProblem.vue` 周边 | `weekly.css` 里 26 处写死 | 收进变量 |
| `utils/cpp-editor.ts` | CodeMirror 配色写死 | 初始化前从 CSS 变量读值 |
| `mermaid-client.ts` | 固定浅色，只初始化一次 | 渲染前读变量传给 `themeVariables` |
| `shiki.ts` | 运行时主题名写死 | 从主题配置读，先与正文对齐 |
| `StatusToast.vue`、`TalkInfoCard.vue` | JS 色表写内联 style | 改成读 CSS 变量 |
| `<meta name="theme-color">` | 静态两条 | 切换器里跟着主题和亮暗改 |
| `--vp-c-bg-alt` | 没有覆盖 | TAMCPP 先补上暖色 |
| drawio `darkMode` | `'auto'` | 插件能否动态切没查，待定 |

### 第一版不开放的

- 正文代码块的 token 配色。原因是构建期只烙亮暗两组颜色。
- Mermaid 和 drawio 图内的配色。
- 布局宽度、字号档位、侧栏宽度、大纲宽度，这些归读者设置。
- 页面文案和链接。
- 样式层的链式继承。`extends` 只管声明层。
- 导入包的异步 chunk。

## 工作顺序

### 一、整理 TAMCPP 默认主题

1. 抽出 `cover-art`、`progress-segments`、`reading-progress`、`hero-visual` 四个主题组件，默认实现与现在的页面一致。验收标准是改造前后页面没有肉眼可见的变化。
2. 第二组表里的前七项按表改掉，drawio 那项留待定。
3. 两处首屏脚本抽成共享常量。
4. 把现有 `custom.css` 等文件里的站点变量整理成 `site/.vitepress/theme/tamcpp.theme.json`，写生成脚本，挂在 `pnpm build` 和 `pnpm dev` 的最前面。生成的 `:root` 和 `.dark` 块与现在 `custom.css` 里的逐值一致，`custom.css` 里那两块变成生成物，改之前备份一份。
5. 加主题运行时、存储键、首屏脚本和切换器。此时列表里只有 TAMCPP。
6. 写 `site/themes/starter/` 模板和校验发布包的脚本。校验脚本要同时给构建期读 TAMCPP 清单和浏览器导入用。

### 二、浏览器导入

1. IndexedDB 的存取，主题注册进运行时的流程，切换器里的导入和删除界面。
2. 首屏变量快照：声明层生成的变量存 localStorage，首屏脚本内联写到 `<html>`。
3. 含脚本的授权提示，`main.js` 的加载、`setup` 和 `teardown` 的调用。

### 三、MC 示例主题

1. 从模板起 `site/themes/examples/minecraft/`，先只改颜色、字体和圆角。
2. 给 `cover-art` 做亮暗两张插画，给 `hero-visual` 做亮暗两张图。
3. 把 `progress-segments` 设成 `slots`，再在 `style.css` 里把格子画成物品槽。
4. 用 `main.js` 给代码块注册一个增强器（比如 hover 高亮一行）。
5. 构建成 zip，用导入功能装进站点，逐项验收：每个字段都在页面上生效，MC 的规则没有泄漏到 TAMCPP，TAMCPP 的基础样式在 MC 下原样生效，`setup` 和 `teardown` 在切换时被正确调用，Mermaid、drawio、代码块在 MC 亮暗两种背景下都可读。

### 四、一份只改强调色的测试主题

从模板起一个只填了 `link` 角色的最小工程，构建后走导入装进站点，确认它能用，且不需要改任何站点组件。这一步用来证明接口是通用的，做完不留在仓库里。

## Dependencies

- 第二部分依赖第一部分的运行时和校验脚本。
- 第三部分依赖第一部分的模板和第二部分的导入功能。
- 第四部分依赖第三部分验证过的导入流程。
- 第一部分第 1 项的四个组件可以并行抽，互不依赖。
- 第一部分第 2 项里的 Mermaid 和 CodeMirror 改造可以独立进行，不阻塞其他项。
- 示例和模板在 `site/.vitepress` 之外，改它们不触发站点重建。`site/.vitepress` 里的任何改动都会让 41 个任务全量重建，迭代时要预留构建时间。

## Acceptance

- [ ] 不选主题时，页面与改造前一致。桌面和 390 像素宽的手机首页、一篇正文、周练首页各截一张对比图。
- [ ] 导入 MC 的 zip 后，首页、正文、周练页、做题页在亮色和暗色下都可读，代码块的 `::` 和 `;` 能分清。
- [ ] 含脚本的包导入时出现授权提示，确认前不生效。
- [ ] 刷新页面后导入的主题还在，颜色在首屏就是对的，不闪默认主题。
- [ ] 从 TAMCPP 暗色切到 MC 直接进 MC 暗色，字号、书签、做题记录不变。
- [ ] 删除当前主题后回到 TAMCPP，存储记录一并清掉。
- [ ] 只改强调色的最小主题不需要改任何站点组件就能用。
- [ ] 从 `site/themes/starter/` 起的新工程，一条命令产出能通过校验的发布包。
- [ ] `pnpm build` 通过，`validate_frontmatter.py`、`check_links.py` 保持全绿。

## 附一：审计材料

2026-11-25 做了一轮审计，四份材料在 `.cache/theme-audit/`：`css-audit.md`、`component-audit.md`、`build-audit.md`、`vitepress-notes.md`。大纲初稿的另一份审稿记录在同一目录的 `review-033.md`。`.cache` 不入库，需要长期保留的内容已经摘进本文件的 Current Assets 和 Gaps。

## 附二：顺带发现，不在本大纲范围

分卷构建里 `scripts/build.ts` 生成的配置盖掉了 `shared.ts` 的 `transformPageData`，线上产物的 `lastUpdated` 是 `null`，周练页的侧栏没有按预期关掉。详见 `build-audit.md` 第 6.1 节。
