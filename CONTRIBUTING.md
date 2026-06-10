# 贡献指南

感谢你对现代 C++ 教程的关注！我们欢迎任何形式的贡献，包括但不限于：修正错别字、改进代码示例、完善现有内容、添加新章节等。

## 快速开始

1. Fork 本仓库
2. 创建你的特性分支 (`git switch -c feature/amazing-feature`)
3. 安装 pre-commit 提交前检查 (`pnpm hooks:install` 或 `scripts/setup_precommit.sh`)
4. 提交更改 (`git commit -m '添加某功能'`)
5. 推送到分支 (`git push origin feature/amazing-feature`)
6. 创建 Pull Request

## 提交前自动化

本仓库使用 [pre-commit](https://pre-commit.com/) 管理提交前检查，配置位于 `.pre-commit-config.yaml`。安装后，每次 `git commit` 前会自动执行：

- Markdown lint、frontmatter 校验、大文件和基础空白检查
- 对已暂存的 C/C++ 源文件和头文件运行 `clang-format -i`
- 运行 `python3 scripts/coverage.py --update` 检查英文翻译覆盖率并更新 `README.md`

如果 hook 修改了文件，pre-commit 会停止本次提交并提示重新暂存。请检查变更后重新执行 `git add` 和 `git commit`。

首次克隆仓库后运行：

```bash
pnpm install
pnpm hooks:install
```

如果不使用 pnpm，也可以直接运行：

```bash
scripts/setup_precommit.sh
```

hook 依赖本机已安装：

- `pre-commit`：用于运行提交前检查，可通过 `pipx install pre-commit` 安装
- `python3`：用于翻译覆盖率统计
- `clang-format`：用于 C/C++ 代码格式化

pre-commit 会在运行检查前临时隔离未暂存改动，避免把未准备提交的内容混入本次提交。确实需要临时跳过 hook 时，可以使用 `git commit --no-verify`，但不建议在 PR 提交中长期绕过。

日常验证请使用 `pre-commit run` 检查已暂存文件。`pre-commit run --all-files` 会对全仓文件运行自动修复型 hook，包括对所有 C/C++ 文件执行 `clang-format -i`。

## 文章规范

### 文章结构

每篇文章应遵循以下结构：

```markdown
---
# [FRONTMATTER 元数据]
---

# 标题

## 引言 / 动机
## 核心概念
## 代码示例
## 实战应用
## 注意事项
## 小结
## 练习（可选）
## 参考资源
```

### Frontmatter 元数据

每篇文章必须包含以下元数据：

| 字段 | 必填 | 说明 |
|------|------|------|
| `title` | 是 | 文章标题 |
| `description` | 是 | 一句话描述文章内容 |
| `chapter` | 是 | 所属章节 |
| `order` | 是 | 在章节中的顺序 |
| `tags` | 是 | 标签列表 |
| `difficulty` | 否 | 难度：beginner / intermediate / advanced |
| `reading_time_minutes` | 否 | 预计阅读时间（分钟） |
| `prerequisites` | 否 | 前置知识 |
| `related` | 否 | 相关文章 |
| `cpp_standard` | 否 | 涉及的 C++ 标准（如 [11, 14, 17, 20]） |

### 目录组织

文章按卷-章组织，放入对应卷目录：

```
documents/vol2-modern-features/     # 卷二目录
├── index.md                        # 卷首页
├── ch01-smart-pointers/            # 章节（可选子目录）
│   ├── 01-raii-deep-dive.md
│   ├── 02-unique-ptr.md
│   └── 03-shared-ptr.md
└── cpp17-string-view.md            # 也可直接放在卷目录下
```

### 写作风格

1. **语言**：使用清晰、简洁的中文
2. **术语**：首次出现的技术术语应附英文原文
3. **代码注释**：使用中文注释
4. **标题层级**：不超过 4 级（`####`）
5. **篇幅**：每篇文章控制在 1500-3000 字

详细写作风格请参考 `.claude/writting_style.md`。

## 自定义 Vue 组件

文档站注册了若干自定义 Vue 组件，可在 Markdown 中直接使用。

### 导航组件（全站通用）

`ChapterNav` + `ChapterLink` 用于生成章节导航卡片网格：

```html
<ChapterNav variant="main">
  <ChapterLink num="1" href="chapter-01/">第一章标题</ChapterLink>
  <ChapterLink num="2" href="chapter-02/">第二章标题</ChapterLink>
</ChapterNav>
```

| 组件 | Prop | 类型 | 说明 |
|------|------|------|------|
| `ChapterNav` | `variant` | `'main'` \| `'sub'` | 布局样式，sub 用于子目录，默认 main |
| `ChapterLink` | `num` | string \| number | 章节编号（仅 main 变体显示） |
| `ChapterLink` | `href` | string | 链接路径，**不要**以 `.md` 结尾 |

### 参考文献组件（全站通用）

`ReferenceCard` + `ReferenceItem` + `RefLink` 用于文章末尾的参考文献列表和正文中的可点击引用标记：

正文内引用：

```html
<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />
```

文章末尾参考文献：

```html
<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup"
    title="The Design and Evolution of C++"
    publisher="Addison-Wesley"
    :year="1994"
    chapter="Chapter 15: Templates"
    url="https://www.stroustrup.com/dne.html"
  />
</ReferenceCard>
```

| 组件 | Prop | 类型 | 说明 |
|------|------|------|------|
| `RefLink` | `id` | number \| string | 引用编号，与 ReferenceItem 的 id 对应 |
| `RefLink` | `preview` | string | 鼠标悬停时的预览文字 |
| `ReferenceCard` | `title` | string | 卡片标题，默认"参考文献" |
| `ReferenceItem` | `id` | number \| string | 引用编号，页内锚点 |
| `ReferenceItem` | `author` | string | 作者 |
| `ReferenceItem` | `title` | string | 文献标题（必填） |
| `ReferenceItem` | `publisher` | string | 出版者 |
| `ReferenceItem` | `year` | number \| string | 年份 |
| `ReferenceItem` | `chapter` | string | 章节或备注信息 |
| `ReferenceItem` | `url` | string | 外部链接 |

### 演讲信息卡片（卷十专用）

`TalkInfoCard` 用于卷十演讲笔记页面的演讲信息展示：

```html
<TalkInfoCard
  talkTitle="Concept-based Generic Programming"
  speaker="Bjarne Stroustrup"
  conference="cppcon"
  :year="2025"
  videoBilibili="https://www.bilibili.com/video/BV1ptCCBKEwW"
  videoYoutube="https://www.youtube.com/watch?v=VMGB75hsDQo"
/>
```

| Prop | 类型 | 说明 |
|------|------|------|
| `talkTitle` | string | 演讲标题（必填） |
| `speaker` | string | 演讲者（必填） |
| `conference` | string | 会议标识（必填）：`cppcon` \| `cppnow` \| `meetingpp` \| `course` \| `blog` |
| `year` | number \| string | 年份（必填） |
| `videoBilibili` | string | Bilibili 视频链接 |
| `videoYoutube` | string | YouTube 视频链接 |
| `slidesUrl` | string | 幻灯片链接 |

组件源码位于 `site/.vitepress/theme/components/`。

## 代码规范

### C++ 代码风格

1. 使用现代 C++ 风格（C++11 及以上）
2. 优先使用 `auto`、范围 for 循环等现代特性
3. 标注适用的 C++ 标准
4. 代码示例使用 CMake 构建，确保可独立编译

```cpp
// Standard: C++17

#include <array>
#include <span>

void process_data(std::span<const uint32_t> data) {
    for (const auto& value : data) {
        // 处理数据
    }
}
```

### 代码格式

- 使用 4 空格缩进
- 大括号另起一行（Allman 风格）
- 函数名使用 snake_case
- 类名使用 PascalCase

## 添加新文章

1. 确定文章所属的卷和章节，放入对应目录
2. 填写完整的 frontmatter
3. 更新对应卷的 `index.md`，添加新文章链接
4. 确保代码示例可编译
5. 本地预览确认渲染正确

## 社区来稿

如果你只是想先投稿一篇文章、笔记、源码阅读或工程经验，可以走更轻量的社区来稿路径。

社区来稿可以先放入：

```text
documents/community/incoming/
```

投稿者可以先提交普通 Markdown 文件，不必一开始理解完整卷结构、导航、frontmatter、英文翻译和站点组件。建议至少写清：

- 文章标题和作者署名。
- 是否原创，或是否已获得授权。
- 主要参考资料、图片来源和代码来源。
- 目标读者或适用背景。
- 是否允许维护者调整标题、格式、放置位置和部分措辞。

社区来稿流转如下：

1. 初步通过基础检查后，文章进入 `documents/community/incoming/`，可在文档站展示，并可被 TAMCPP 周报引用。
2. 经过社区讨论、语法修订和技术审阅后，文章可移动到 `documents/community/articles/` 长期收录。
3. 如果文章非常适合主线教程，维护者可进一步整理进对应卷或章节。

社区初刊不代表最终定稿，但上线前仍需满足基本要求：内容可以正常渲染、没有明显技术硬伤、来源和引用清楚、作者同意公开展示。

## 发布前检查清单

提交 PR 前，请确认：

- [ ] Frontmatter 元数据完整
- [ ] 代码示例可编译
- [ ] 无错别字
- [ ] 内部链接有效
- [ ] 标签使用规范
- [ ] 遵循文章模板结构
- [ ] 更新了卷首页索引（如适用）

## 本地预览

在提交前，建议本地预览文档：

```bash
# 安装依赖
pnpm install

# 构建后预览（更接近生产环境效果）
# 并发构建加速，建议值填写您的nproc输出结果
BUILD_CONCURRENCY=16 pnpm build && pnpm preview
# 到这里，会提示您访问 http://localhost:5173/Tutorial_AwesomeModernCPP/

# 或者：启动开发服务器（支持热更新），调试构建用这个
pnpm dev
# 访问 http://localhost:5173/Tutorial_AwesomeModernCPP/
```

## 代码审查流程

1. 所有 PR 需要至少一位维护者审核
2. CI 检查必须通过（markdown lint、链接检查）
3. 审核通过后，维护者将合并代码

## 社区协作规则

Issue、Discussion 和 PR 分工如下：

- Issue 负责可行动问题：内容错误、构建失败、站点问题、明确的新内容请求。
- Discussion 负责学习问题、路线讨论、开放式建议。
- PR 负责具体修改，请说明影响范围、验证方式和是否更新索引。
- 学习问题优先沉淀到 Discussion 和 QA，避免 Issue 列表变成临时问答区。
- 内容请求应能回到某个卷级 TODO 或项目路线，不直接变成零散任务。

Issue 和 PR 模板应保持简短，只收集维护者真正需要的信息。

## QA 与知识库规则

QA 不替代正文，只负责解答常见分叉问题和高频误区。

收录外部内容时请遵守：

- 只做摘要、解释和链接，不大段搬运。
- 不原样搬运原回答，除非原作者明确投稿或授权。
- 说明这个链接为什么值得读。
- 回链到对应卷、章节或参考卡。
- 高质量 Discussion 可以沉淀成 appendix/FAQ 条目。

## 质量、翻译与发布

新增文章前请检查 frontmatter、索引、内部链接、代码块和翻译状态。本地质量命令和 CI workflow 应保持可对应：

- Markdown link check
- Frontmatter validation
- English coverage
- VitePress build
- Host examples build
- STM32 examples build

翻译维护规则：

- 正文可以中文先行，关键页面发布前保持英文覆盖。
- 社区来稿初刊不强制同步英文翻译，稳定收录后再视情况翻译。
- TODO 文件不强制纳入英文站。
- 术语表、参考卡和导航页优先保持双语一致。
- 长篇课程笔记可以低优先级翻译。
- 翻译后必须跑链接检查。

发布规则：

- patch：修错、链接、站点修复。
- minor：一个卷或专题明显推进。
- major：TODO 结构、站点架构或内容体系发生大调整。
- 发布前检查 link check、VitePress build、example build、coverage update、changelog 和 README 状态表。
- changelog 应写用户可感知变化，不只是文件数量。
- milestone 只绑定少量 P0/P1 TODO，不把远期候选塞进近期 milestone。

## 非代码贡献

我们同样重视非代码形式的贡献！以下贡献方式均会被记录在 [CONTRIBUTORS.md](./CONTRIBUTORS.md) 中：

| 贡献类型 | 说明 | 如何参与 |
|---------|------|---------|
| 界面设计 | 文档站 UI/UX 优化 | 通过 Issue 提交设计方案 |
| 插画配图 | 教程插图、架构图 | 通过 Issue 或邮件提交 |
| 问题反馈 | 发现错误或不准确之处 | 提交 Issue 或通过微信/QQ/邮件反馈 |
| 内容建议 | 新主题建议、改进意见 | 提交 Issue 或通过微信/QQ/邮件反馈 |
| 内容审阅 | 技术校对、审阅 | 通过 Issue 或邮件参与 |
| 翻译 | 英文翻译 | 通过 PR 提交 |

### 匿名贡献

如果您希望匿名贡献，请在反馈时注明。我们会使用化名或不具名记录您的贡献。

### 贡献者记录

所有贡献者会被记录在：

- [CONTRIBUTORS.md](./CONTRIBUTORS.md) — 完整贡献者列表
- [文档站贡献者页面](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/team/) — 在线展示

## 行为准则

- 尊重所有贡献者
- 建设性的反馈和讨论
- 专注于对项目最有利的事情

## 获取帮助

如有问题，请：

- 提交 [GitHub Issue](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- 发送邮件至：725610365@qq.com

---

再次感谢你的贡献！
