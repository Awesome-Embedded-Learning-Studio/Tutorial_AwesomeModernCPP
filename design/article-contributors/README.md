# 文章贡献卡

采用「页头摘要 + 文末署名」：页头是一条浅暖底色的信息栏，显示贡献者人数、小头像和“查看署名”；文末展示名字、角色、贡献说明，以及可展开的 Git 变更记录。头像悬停和键盘聚焦显示角色，点击页头头像定位到文末对应的人。

## 位置方案

```text
A 文末署名：正文 → 贡献者与历史 → 标签 → 上下篇
B 页头摘要：贡献者 [头像][头像] 查看署名 ↓ → 正文
C 侧栏卡片：正文 | 目录 + 贡献者
```

| 方案 | 优点 | 代价 |
| --- | --- | --- |
| 文末署名 | 角色、说明和多人协作信息完整；阅读结束后自然出现 | 长文章需要滚动到末尾 |
| 页头摘要 | 开篇就能看到参与者；点击可定位完整署名 | 需要克制尺寸，避免占用首屏 |
| 悬浮侧栏 | 桌面阅读时持续可见 | 与目录竞争空间；窄屏需要另一套呈现 |

推荐 A + B。默认信息栏约 54px 高，无卡片抬升动画；文末头像 32px，桌面双列、手机单列。历史默认折叠，避免变更多的老文章占据大片高度。颜色使用现有 VitePress 变量，跟随 `.dark`；所有贡献卡选择器使用 `card-` 前缀并限制作用域，不使用 `!important`。

## 文章内维护署名

不维护独立 JSON。已有文章没有 `contributors` 字段时，默认显示 Charliechen114514，角色为 `author`、`maintain`。导航页和每周栏目不会套用文章默认署名。

明确署名时，文章 frontmatter 写完整人员名单，覆盖默认名单：

```yaml
contributors:
  - github: xiaoshuaijie
    roles: [author]
    note:
      zh: 撰写并投稿原稿
      en: Wrote and submitted the original article
    pr: 276
  - github: Charliechen114514
    roles: [revision, review]
    note:
      zh: 技术校订、配图本地化与收录
      en: Technical revision, image localization, and integration
```

`name` 可以指定显示名；`note` 可以是本页语言的字符串，也可以是 `{ zh, en }`。中文原稿采用双语说明后，英文页可以直接继承署名；英文文章自己的 `contributors` 会覆盖继承名单，便于明确登记译者。`contributors: []` 隐藏整组署名。

| roles 值 | 中文 | 英文 |
| --- | --- | --- |
| author | 撰写 | Author |
| revision | 修订 | Revision |
| translation | 翻译 | Translation |
| review | 审校 | Review |
| maintain | 维护 | Maintain |

沿用已有五个枚举，避免迁移名称；`roles` 使用数组，一人兼任多个角色时只显示一次。格式化器在构建时翻译 UI 和角色名；贡献说明使用文章提供的对应语言，Git 提交说明保留原文。

## 文末组件

主题已经通过现有 `doc-before`、`doc-footer-before` 插槽自动接入，不需要逐篇插入标签。导航与布局骨架保持现有结构。全局组件注册位置是 `site/.vitepress/theme/index.ts`：

```ts
app.component('ArticleContributors', ArticleContributors)
```

需要精确指定文末位置时（WSL2 样例采用此方式），在 frontmatter 加：

```yaml
contributors_footer: inline
```

然后在文章末尾写：

```vue
<ArticleContributors />
```

自动文末署名会被抑制，避免重复；页头信息栏保留。双语 WSL2 页各自放一个组件，人员信息只写在中文源文章里。

纯展示组件 `ArticleContributorCard.vue` 接收格式化后的 `data` 和 `variant="meta" | "full"`。全局包装组件 `ArticleContributors.vue` 读取当前页数据；CSS 在 `site/.vitepress/theme/article-contributors.css`。实现不增加依赖，头像继续使用 GitHub 直链，设置 `loading="lazy"` 和尺寸，并用首字母作为图片未加载时的底层占位。

接入方式参考 [VitePress 主题插槽与全局组件文档](https://vitepress.dev/guide/extending-default-theme)。

## Git 历史与署名回填

`config/article-history.ts` 在构建期读取真实 `documents/` 文件的 Git 历史，使用 `--follow` 追踪改名，排除仅负责合并的提交，保留可识别的共同作者。日期、提交说明、PR 编号及提交链接来自实际记录；未提交文件不虚构历史。构建产物仅携带当前文章数据，不把全站历史打包进浏览器。

历史身份映射依据项目贡献者墙、仓库 remote 和 GitHub noreply 邮箱。未知身份保留 Git 作者名；工具的共同署名不自动成为 GitHub 人物。默认作者依据维护者明确指定，审校角色依据明确署名，均不从提交次数推断。

这次回填了 58 篇原有文章，涉及 xiaoshuaijie、owollz4、YukunJ、Voyagerroc-Lab 和 Voyagerroc-Pro。Git 能证实他们改过文章，迁移脚本统一标为 `revision`；具体说明可以在各文章内继续完善。WSL2 的原作者与审校角色单独按已知信息声明。每周栏目继续使用已有的致谢组件。

回填脚本默认只报告，`--write` 才更新缺少声明的文章；已有声明保持原样：

```bash
node --import tsx scripts/backfill-article-contributors.ts
node --import tsx scripts/backfill-article-contributors.ts --write
```

dev / 单体构建以及分卷构建均调用同一个格式化器。分卷构建缓存会计入 Git 历史，避免提交日期或历史改变后继续复用旧署名。Git 历史在当前构建进程中缓存；新增提交后重启 dev。部署工作流已有 `fetch-depth: 0`，能够读取完整历史。

## 成品预览与验证

直接用浏览器打开同目录的 `preview.html`。这是从真实 Vue 模板生成的独立 HTML + CSS，无 Vue 运行时；唯一的 JavaScript 是亮暗切换按钮。包含真实 WSL2 中英文样例和明确标注的四人协作假想样例，后者仅用于验证角色与布局。

重新生成预览、执行行为检查：

```bash
node --import tsx scripts/preview-article-contributors.ts
node --import tsx site/.vitepress/config/article-contributors.test.ts
pnpm dev
pnpm build
```

浏览器预览地址：`/Tutorial_AwesomeModernCPP/getting-started/07-wsl2-environment`，英文对应 `/Tutorial_AwesomeModernCPP/en/getting-started/07-wsl2-environment`。
