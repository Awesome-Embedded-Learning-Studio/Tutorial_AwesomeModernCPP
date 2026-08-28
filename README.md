# Tutorial_AwesomeModernCPP

[English](README.en.md) | 中文

> 一个持续建设的，面向工程实践的现代 C++ 系统教程：从 C/C++ 基础、到现代C++语言特性，再到标准库、并发、性能、工程化、领域应用与开源项目研读。持续的进步中！
> 试一试点一下下面的图片？
<p align="center">
  <a href="https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/">
    <img src="documents/images/screenshots/01-home.png" alt="在线文档站首页预览 · 点击进入" width="860">
  </a>
</p>

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B)
![Release](https://img.shields.io/github/v/release/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Tag](https://img.shields.io/github/v/tag/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP?sort=semver&label=tag)
![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Build](https://img.shields.io/github/actions/workflow/status/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/deploy.yml?branch=main)
![AI agent ready](https://img.shields.io/badge/AI%20agent-ready-7C3AED)

---

<!-- COVERAGE_START -->
![English Coverage](https://img.shields.io/badge/en_coverage-97%25-green.svg) 578/593 docs translated
<!-- COVERAGE_END -->

## 嘿！这是什么？

<p align="center"><em>这是一套正在持续进步的，系统化的现代 C++ 教程——从语法到芯片，把现代 C++ 写进桌面、STM32 嵌入式与工业级开源项目。</em></p>

10 卷、从 C/C++ 基础一路讲到并发、性能、工程与领域实战，我们尝试将每一个概念转化为具体的代码，丢到CI机器验证（孩子们真没出错）

<p align="center">
  <img src="https://img.shields.io/badge/articles-430%2B-blue" alt="articles">
  <img src="https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-009688" alt="C++ standard">
  <img src="https://img.shields.io/badge/embedded-STM32%20F1-FFC107" alt="embedded">
  <img src="https://img.shields.io/badge/examples-CMake%20%7C%20CI%20verified-3F51B5" alt="examples">
</p>

## 谁可以来？

正在打算系统的学 C/C++ 的 bro 们 · 有 C 或嵌入式经验的 bro 们 · 已会 C++ 想补齐工程能力的 bro 们

**适合谁？**

## 特色亮点

<table>
  <tr>
    <td width="50%" align="center"><h4>🔧 从语法到芯片</h4>深入 STM32F1 嵌入式——寄存器访问、中断安全、零开销抽象、交叉编译与链接脚本，打通裸机。</td>
    <td width="50%" align="center"><h4>🧪 崩溃实验室</h4>故意写崩的代码、修好的版本、一步步排查的实录——空指针、释放后使用，先崩给您看，再带您破案。</td>
  </tr>
  <tr>
    <td align="center"><h4>💻 不买板子，先点灯</h4>嵌入式线以 Renode 模拟器为主验证环境：一条命令让固件在虚拟 Blue Pill 上跑起来，寄存器采样证明 LED 真的在闪。</td>
    <td align="center"><h4>📇 C++ 特性参考卡</h4>C++98→23 一特性一卡，忘了语法随手查，不用翻回整章教程。</td>
  </tr>
  <tr>
    <td align="center"><h4>🔍 读真源码 · 读真会议</h4>卷九研读 Chromium（如 OnceCallback），卷十是 CppCon 等会议演讲的读书笔记。</td>
    <td align="center"><h4>🌐 工程化 + 双语</h4>VitePress（搜索 / 暗色 / GitHub Pages 自动部署）+ 中文主线 + 英文翻译。</td>
  </tr>
</table>

## 马上开始

最快的方式是直接阅读在线文档：

- [在线文档站](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

本地预览文档站：

```bash
git clone https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP.git
cd Tutorial_AwesomeModernCPP

pnpm install
pnpm dev
# 访问 http://localhost:5173/Tutorial_AwesomeModernCPP/
# 如果你是在WSL中，可能需要pnpm dev --host！
```

生产构建与预览：

```bash
# 如果您的CPU核心大于4，我们推介加速构建，默认的构建脚本是为CI服务的，Github分配的机器有点小qaq
BUILD_CONCURRENCY=8 pnpm build
pnpm preview
# 访问 http://localhost:4173/Tutorial_AwesomeModernCPP/
```

每个示例都是独立 CMake 工程、CI 编译验证过——不是文章里跑不起来的伪代码。任选一个目录即可构建：

```bash
cmake -S code/examples/chapter05/06_array_vs_stdarray -B build && cmake --build build -j${nproc}
```

## 内容导览

可视化路线图（十卷内容地图 + 按背景选择学习路径）已整合进在线文档站首页的「项目路线图」区：

→ [在线查看可视化路线图](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/#roadmap)

### 各卷一览

主线卷已成型，进阶卷持续补充——不藏进度：

| 卷   | 主题                                          | 成熟度     |
| ---- | --------------------------------------------- | ---------- |
| 卷一 | 基础入门（含 C 速通）                         | ✅ 成型     |
| 卷二 | 现代特性（RAII / 智能指针 / 移动 / lambda）   | ✅ 成型     |
| 卷三 | 标准库深入                                    | ✅ 成型     |
| 卷四 | 高级主题（concepts / 协程 / 模板 / 设计模式） | 🔨 在建     |
| 卷五 | 并发编程                                      | ✅ 成型     |
| 卷六 | 性能优化                                      | ✅ 成型     |
| 卷七 | 工程实践（CMake / 工具链 / 调试）             | 🔨 在建     |
| 卷八 | 领域应用（嵌入式 / TinyML / 网络等）          | 🔨 在建     |
| 卷九 | 开源项目研读（Chromium 等）                   | 📚 持续更新 |
| 卷十 | 课程与演讲笔记（CppCon 等）                   | 📚 持续更新 |

> 另含新手起步、「编译与链接」专题、崩溃实验室、社区文章与 C++ 特性参考卡。
>
> 📋 各卷内容与进度见 [项目总路线图](todo/000-project-roadmap.md)，版本变更见 [changelogs/](changelogs/)。

## 本地开发与质量检查

<details>
<summary>常用命令</summary>

| 命令 / 脚本                                            | 功能                                    |
| ------------------------------------------------------ | --------------------------------------- |
| `pnpm dev`                                             | 启动 VitePress 开发服务器，支持热更新   |
| `pnpm build`                                           | 生产构建，按分卷并行构建并合并搜索索引  |
| `pnpm build:single`                                    | 使用 VitePress 单体构建                 |
| `pnpm check:links`                                     | 检查 Markdown 与组件内部链接有效性      |
| `pnpm preview`                                         | 预览生产构建结果                        |
| `pnpm hooks:install` / `scripts/setup_precommit.sh`    | 安装 pre-commit 提交前检查              |
| `pnpm coverage`                                        | 查看英文翻译覆盖率                      |
| `pnpm coverage:update`                                 | 更新 `README.md` 中的英文翻译覆盖率徽章 |
| `.venv/bin/python scripts/validate_frontmatter.py`     | 验证文章 frontmatter                    |
| `.venv/bin/python scripts/check_quality.py documents/` | 内容质量检查                            |
| `.venv/bin/python scripts/build_examples.py --host`    | 编译主机侧 CMake 示例                   |
| `.venv/bin/python scripts/build_examples.py --stm32`   | 编译 STM32 示例工程                     |

</details>

<details>
<summary>项目结构、版本与分支</summary>

**项目结构**

- `documents/` — 10 卷教程内容（中英双语），含 community / cpp-reference / compilation / projects 等区
- `code/` — 示例代码、STM32F1 工程与可复用模板
- `site/` — VitePress 站点配置、主题与插件
- `scripts/` — 构建、检查、覆盖率与内容工具
- `todo/`、`changelogs/` — 内容路线图与版本变更记录

> 完整目录与站点导航见[在线文档站](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)侧边栏。

**版本历史**

完整变更记录见 [changelogs/](changelogs/)。

</details>

## 如果你是维新派。。。可以使用 AI Agent 辅助！

本项目对 AI coding agent 开箱即用，也欢迎用 Agent 辅助学习与贡献！

- 🤖 [AGENTS.md](./AGENTS.md) —— 跨 agent 通用入口（Claude Code / Cursor / Copilot / Codex 等都读它）
- 📚 [用 agent 辅助 C++ 学习](./.github/learning-with-agents.md) + [C++ 常见误解 FAQ](./.github/faq.md)
- ✍️ Claude 专属资产（写作风格 / 审查命令 / hooks）见 [CLAUDE.md](./CLAUDE.md)

## 贡献

欢迎修正文档、改进示例、补充章节、校对翻译、提交问题、提出内容建议，或向 [社区文章](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/community/) 投稿。请先阅读 [CONTRIBUTING.md](./CONTRIBUTING.md)。

快速流程：Fork --> 特性分支 --> 提交 --> Push --> Pull Request

如有问题，欢迎在 [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues) 中提交。

## 贡献者

这里感谢所有为本项目做出贡献的人！详见 [CONTRIBUTORS.md](./CONTRIBUTORS.md)。

> 贡献方式不限于代码，包括界面设计、插画、问题反馈、内容建议等。详见 [CONTRIBUTING.md](./CONTRIBUTING.md)。

## 致谢

这个项目极大的受到下面这些项目和网站的启发，致敬前辈！

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial)
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings)
- [CppCon](https://www.youtube.com/user/CppCon)
- [C++ Reference](https://en.cppreference.com/)

## 许可证与联系方式

- **许可证**：[MIT License](./LICENSE)，随便改，fork，分发
- **Issues**：[提交问题](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)，遇到任何疑问，或者发现代码存在显著问题，随意提，第一时间响应。
- **Email**：<725610365@qq.com>
- **组织**：隶属于[Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)
