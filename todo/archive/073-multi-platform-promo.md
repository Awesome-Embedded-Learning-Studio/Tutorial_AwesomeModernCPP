---
id: "073"
title: "多平台推广计划"
category: branding
priority: P2
status: done
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: small
---

# 多平台推广计划

## 目标

6 个月内达到 500+ GitHub stars，通过国内外多平台推广提升项目知名度。

---

## 第一阶段：GitHub 优化（W1-2）

### 仓库设置操作清单

**Settings → General:**

| 设置项 | 值 |
|--------|-----|
| Description | `系统化现代 C++ 教程 — 从基础入门到嵌入式实战，每个概念配有可编译代码示例` |
| Website | `https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/` |
| Topics | `cpp`, `cpp17`, `cpp20`, `embedded`, `embedded-systems`, `stm32`, `stm32f1`, `rtos`, `tutorial`, `modern-cpp`, `education`, `chinese`, `mkdocs` |
| Social Preview | 使用 `documents/images/social-preview.png`（待生成，临时用 `logo.png`） |

**Milestones:**

| Milestone | 描述 |
|-----------|------|
| `v1.0 - 基础教程完成` | 卷一+卷二+编译深入+嵌入式入门全部完成 |
| `v1.1 - 多平台覆盖` | ESP32 + RP2040 教程完成 |

**已完成项:**
- [x] README 徽章（Build Status + Docs + Stars + License）
- [x] README 中英双语
- [x] 项目 Logo 和 Favicon
- [x] 统一标签体系
- [ ] GitHub Topics 设置（手动操作）
- [ ] Social Preview 上传（手动操作）
- [ ] Milestone 创建（手动操作）

---

## 第二阶段：国内平台推广（W3-6）

### 知乎（叙事型长文）

**专栏名建议**：嵌入式现代 C++ 实战

**文章 1** — "2026 年，嵌入式开发者为什么要学现代 C++？"

- 要点：C++11/17/23 如何解决嵌入式开发中的痛点（裸指针 → 智能指针、宏 → constexpr、回调 → Lambda）
- 受众：有 C + 嵌入式背景，对 C++ 有好奇但未深入的开发者
- 姿态：行业趋势分析 + 个人实战体验
- 引流：文末附项目链接和文档站

**文章 2** — "从寄存器操作到 C++ 抽象：STM32 GPIO 的现代写法"

- 要点：展示从 HAL 裸调用到类型安全 C++ 封装的完整演进路径
- 受众：正在用 STM32 的开发者
- 姿态：代码对比 + 编译输出验证
- 引流：代码来自项目教程，附链接

**文章 3** — "FreeRTOS + 现代 C++：类型安全的 RTOS 封装"

- 要点：用 RAII 包装任务/信号量，避免资源泄漏
- 受众：使用 RTOS 的嵌入式开发者
- 姿态：实战技巧 + 踩坑记录

### 掘金（代码型教程）

- 侧重完整代码展示和运行结果
- 利用掘金的 `C++` 和 `嵌入式` 标签
- 标题格式："用 C++17 重写 STM32 外设驱动，代码量减少 60%"
- 可复用知乎文章的代码部分，调整叙事风格为更偏技术讲解

### CSDN（SEO 型入门内容）

- 标题使用 SEO 友好格式："STM32 C++ 开发入门教程 2026 最新版"
- 内容入门级，覆盖环境搭建 + 点灯
- 引流到 GitHub 和文档站

---

## 第三阶段：国际平台推广（W4-8）

### Reddit

**r/cpp** (130K+ 成员):
- 标题: "I'm writing a systematic modern C++ tutorial with embedded systems focus — 8 volumes from basics to RTOS"
- 要点: 强调 C++ 现代特性在资源受限环境中的应用
- 附: 文档站截图 + 代码片段

**r/embedded** (100K+ 成员):
- 标题: "Modern C++ for embedded systems — a free open-source tutorial covering STM32, ESP32, RP2040"
- 要点: 强调实战性和多平台覆盖

**r/STM32** (30K+ 成员):
- 标题: "C++17/23 approach to STM32 programming — type-safe GPIO, DMA, and interrupt abstractions"
- 要点: 展示具体的 C++ 封装代码对比

> 注意: 发帖前先在社区互动，避免纯自我推广。每个 subreddit 发帖间隔至少 1 周。

### Hacker News

- 标题: `Show HN: A Systematic Modern C++ Tutorial – from Fundamentals to Embedded Systems`
- 发布时间: 美国东部时间周二至周四上午 8-10 点（对应北京时间晚 8-10 点）
- 准备好回答技术问题，提前整理 FAQ
- 在项目 README 和文档站中确保 Show HN 前体验流畅

---

## 第四阶段：视频内容（可选，W8+）

- B 站 3-5 分钟教程预告
- 选题: LED 点灯的 C++ 写法 vs C 写法对比
- 展示硬件连接 + 示波器/逻辑分析仪波形
- 配合 todo/interactive 中的视频计划执行

---

## 推广内容日历

| 周次 | 平台 | 内容 | 状态 |
|------|------|------|:----:|
| W1-2 | GitHub | Topics / Social Preview / Milestones | - |
| W3 | 知乎 | 文章 1：为什么学现代 C++ | - |
| W4 | 掘金 | 文章 1：代码对比型教程 | - |
| W4 | Reddit r/cpp | 项目介绍帖 | - |
| W5 | 知乎 | 文章 2：GPIO 现代写法 | - |
| W5 | Reddit r/embedded | 教程分享帖 | - |
| W6 | Hacker News | Show HN | - |
| W6 | CSDN | 入门文章 | - |
| W7 | 知乎 | 文章 3：RTOS + 现代 C++ | - |
| W8 | Reddit r/STM32 | STM32 教程帖 | - |
| W8+ | B 站 | 视频预告（可选） | - |

---

## 流量追踪模板

每周记录以下指标：

| 指标 | W1 | W2 | W3 | W4 | W5 | W6 | W7 | W8 |
|------|:--:|:--:|:--:|:--:|:--:|:--:|:--:|:--:|
| GitHub Stars | | | | | | | | |
| GitHub Clones | | | | | | | | |
| 文档站 UV | | | | | | | | |
| 知乎阅读量 | - | - | | | | | | |
| 掘金阅读量 | - | - | - | | | | | |
| Reddit upvotes | - | - | - | | | | | |
| HN points | - | - | - | - | - | | | |

数据来源:
- GitHub Insights → Traffic
- Cloudflare Analytics（如果配置了自定义域名）
- 各平台后台数据
