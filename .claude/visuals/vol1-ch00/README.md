# 卷一第 0 章视觉化试做

本轮从主线 `vol1-fundamentals/ch00` 开始。产物是文章内的 drawio 图与可播放的 Vue 短动画；尚未制作 MP4，也没有完成全仓铺设。状态见 [state.yaml](./state.yaml)。

## 选点与文章审阅

| 文章 | 本轮处理 | 原因与后续 |
| --- | --- | --- |
| `00-preface.md` | 学习路线 drawio；修正文案 | 路线适合静态总览。修正 C/C++ 项目归类、零开销绝对化、无来源排名数字；明确 C 教程是可选补充、RAII 并非 C++11 才出现。保留作者自述。 |
| `01-setup-linux.md` | 初步扫描，暂不插图 | 安装操作先保留命令块。后续可画“编辑器 → CMake → 构建工具 → 编译器”的职责关系；不能把已有的多发行版“验证通过”当成本轮实测。 |
| `02-setup-windows.md` | 初步扫描，暂不插图 | 原文同时出现“没精力仔细验证”和“所有命令和截图已验证”，需要独立实测核对。当前无 Windows 验证环境，不为安装界面制作动画。 |
| `03-first-program.md` | 编译流程 drawio + Vue 动画；修正文案 | 动画逐阶段改变输入、产物和命令，强调构建与运行的边界；静态图保留完整关系。统一文件名与 C++17 命令，修正入口、头文件路径、输出刷新、未初始化值说明，替换三组诊断为 GCC 16.1.1 实际输出。 |

## 动画维护

文章使用 `<Anim id="vol1/ch00/03-first-program/compilation-pipeline" />`，内容位于 `animations/scenes/vol1/ch00/03-first-program/compilation-pipeline.ts`，由共用的 `animations/players/StepPlayer.vue` 播放。目录规范见 `site/.vitepress/theme/components/animations/README.md`。六幕依次为源码、预处理、编译、汇编、链接、运行；每幕约 7 秒，总计约 42 秒，时长用于阅读，不表达真实编译速度。支持暂停、前后单步、阶段跳转、复位、重播、0.5×/1×/2×、窄屏、减少动态效果；页面切后台时暂停，组件卸载时清理计时器。初次加载不自动播放，SSG 输出第一幕。

两张 `.drawio` 放在本章 `assets/00-preface/` 和 `assets/03-first-program/` 下，是可直接用 diagrams.net 编辑的未压缩 XML，使用仓库已有的 Markdown 图片接入方式。Vue 动画适合看时序，drawio 适合回看全貌；修改阶段名称或产物时，要一起检查图、动画和正文命令。

按用户对目录的反馈，生成的播放器与数据整体移入 `animations/generated/`，14 个文件逐字节保持原样，旧文章 ID 不变。浏览器已验证新旧动画均可播放、同页双实例进度独立、切换 ID 复位以及未知 ID 提示。后续同类文章只添加场景数据；不复制播放器，不为每篇文章注册全局组件。

## animy_maker 试跑与限制

实际读取并试用了主工作区 `.claude/tools/animy_maker`（`512fef0`）。该工具是忽略目录下的独立仓库，worktree 不会自动带上；本轮没有改动它。

从教程 worktree 根目录运行下面的探针；将 `ANIMY_SOURCE` 设成该工具的 `src` 绝对路径：

```bash
ANIMY_SOURCE=/home/charliechen/Tutorial_AwesomeModernCPP/.claude/tools/animy_maker/src
PYTHONPATH="$ANIMY_SOURCE" .venv/bin/python -m animation_maker compile \
  .claude/visuals/vol1-ch00/pipeline-probe.yaml --backend web \
  -o /tmp/tamcpp-ch00-probe
```

这个探针**预期失败**，并非可发布 DSL：`root.actors: 至少需要一个 cell_row actor`。当前 schema 还要求 counter 和至少一次 grow_to，不能直接表示纯文件流水线。硬塞数组格子会给初学者错误暗示，因此这次用共用的 Vue 分步播放器和文章场景数据落地，不修改编译器、不手改生成的 AnimPlayer。后续进入数组/容器内容时再使用现有 DSL。

## 验证

- C++：Linux / GCC 16.1.1，C++17 四阶段命令实际运行，得到 `Hello, C++!`；同一 Hello 示例通过 C++11/14/17/20/23 编译运行；三个错误示例确实编译失败。验证源码和中间文件均放 `/tmp/tamcpp-ch00-visuals/`。
- 文章：`markdownlint documents/vol1-fundamentals/ch00/00-preface.md documents/vol1-fundamentals/ch00/03-first-program.md`。
- 元数据：`.venv/bin/python scripts/validate_frontmatter.py`。
- 链接：`.venv/bin/python scripts/check_links.py`。
- 页面：`pnpm dev`，打开 `/Tutorial_AwesomeModernCPP/vol1-fundamentals/ch00/03-first-program` 和 `00-preface`。
- 构建：`BUILD_CONCURRENCY=4 pnpm build`。

2026-09-06 验证结果：Markdown lint 通过；993 篇 frontmatter 无警告、无错误；1223 个 Markdown 文件的站内链接通过；全站分卷构建成功（目录整理后复验：38 个任务，206.2 秒），生成的 HTML 包含完整 Vue 首帧。

浏览器检查通过：两张 drawio 实际生成 SVG，前后单步、倍速推进、暂停保持、重播、自动结束、键盘操作、320px/375px 无横向溢出、深色模式、减少动态效果；未发现页面运行异常和水合警告。原始日志与截图留在 `/tmp/tamcpp-ch00-visuals/`，不进入发布资源。

首次在 worktree 中运行需要安装依赖。不要仅假设主工作区 `node_modules` 完整；本次先复用时缺 `@vueuse/core` 直接入口，之后使用 `pnpm install --offline --frozen-lockfile --ignore-scripts` 从本机缓存补齐，无锁文件改动。Python 沿用主工作区的 `.venv/bin`，不调用系统 Python。
