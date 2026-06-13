---
id: "readme"
title: "TODO 入口"
category: governance
priority: P0
status: active
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# TODO 入口

本目录是 Tutorial_AwesomeModernCPP 的当前 TODO 规划入口。

旧 TODO 文件已合并为更小的文件集：内容发展 TODO、社区建设 TODO 和项目总路线图。

本目录只保留两类 TODO：

- 内容发展 TODO：项目路线图、各卷路线、贯穿式项目。
- 社区建设 TODO：社区入口、QA/知识库建设。

社区治理、站点质量、翻译维护、发布治理等规则说明已经并入 `CONTRIBUTING.md`，不进入 `todo/` 文件列表。

## 状态图例

| 状态 | 含义 |
|------|------|
| stable | 已是稳定资产，以维护为主 |
| active | 值得近期继续推进 |
| rebuild | 需要重建结构或主线 |
| skeleton | 只有入口或规划，暂不投入主线资源 |
| governance | 路线图入口；流程规则另见 `CONTRIBUTING.md` |

## 文件列表

| 文件 | 作用 |
|------|------|
| `000-project-roadmap.md` | 项目总路线图和近期优先级 |
| `010-vol1-fundamentals.md` | 卷一维护计划 |
| `011-vol2-modern-features.md` | 卷二维护和更新计划 |
| `012-vol3-standard-library.md` | 卷三标准库重建计划 |
| `013-vol4-advanced-topics.md` | 卷四高级主题、模板和前沿计划 |
| `014-vol5-concurrency.md` | 卷五并发收尾、capstone 和前沿计划 |
| `015-vol6-performance.md` | 卷六性能工程路线 |
| `016-vol7-engineering.md` | 卷七工程实践和质量闭环 |
| `017-vol8-domains.md` | 卷八领域应用，嵌入式优先 |
| `018-vol9-open-source-study.md` | 卷九开源项目研读路线 |
| `019-vol10-lecture-notes.md` | 卷十课程笔记治理 |
| `020-projects.md` | 贯穿式项目路线 |
| `031-qa-knowledge-base.md` | QA 和知识库 |

## 旧 TODO 合并映射

| 旧 TODO | 新归宿 |
|---------|--------|
| `010-esp32-toolchain.md` | `017-vol8-domains.md` |
| `012-020-stm32f1-*.md` | `017-vol8-domains.md` |
| `021-stm32f1-projects.md` | `017-vol8-domains.md`, `020-projects.md` |
| `022-core-theory-expansion.md` | 已由卷级 TODO 吸收；不保留独立任务 |
| `023-027-rtos-*.md` | `017-vol8-domains.md` |
| `030-template-series.md`, `038-041-template-*.md` | `013-vol4-advanced-topics.md` |
| `031-design-patterns.md` | `013-vol4-advanced-topics.md`，嵌入式部分交叉到 `017` |
| `032-tdd-embedded.md` | `016-vol7-engineering.md`，嵌入式上下文交叉到 `017` |
| `033-custom-allocators.md` | `012-vol3-standard-library.md`，嵌入式用法交叉到 `017` |
| `034-stl-embedded.md` | `012-vol3-standard-library.md`，领域约束交叉到 `017` |
| `035-startup-linker.md` | `020-compilation-and-reference.md`，STM32 启动上下文交叉到 `017` |
| `036-dsp-basics.md` | `015-vol6-performance.md` |
| `037-iot-project.md` | `020-projects.md`，领域上下文交叉到 `017` |
| `042-stm32f1-engineering-prep.md` | `016-vol7-engineering.md`，STM32 上下文交叉到 `017` |
| `090-community-issue-pr-templates.md` | `030-community.md` |
| `091-community-qa-knowledge-base.md` | `031-qa-knowledge-base.md` |
| `200-project-roadmap-and-volume-audit.md` | `000-project-roadmap.md` |
| `202-vol3-standard-library-outline.md` | `012-vol3-standard-library.md` |
| `203-vol4-advanced-outline.md` | `013-vol4-advanced-topics.md` |
| `204-vol5-concurrency-outline.md` | `014-vol5-concurrency.md` |
| `205-vol6-performance-outline.md` | `015-vol6-performance.md` |
| `206-vol7-engineering-outline.md` | `016-vol7-engineering.md` |
| `207-vol8-domains-outline.md` | `017-vol8-domains.md` |
| `208-projects-outline.md` | `020-projects.md` |
| `209-compilation-enhancement.md` | `020-compilation-and-reference.md` |
| `210-vol9-chrome-base-outline.md` | `018-vol9-open-source-study.md` |

## 不迁移项

| 来源 | 处理 |
|------|------|
| RP2040 / RP 平台路线 | 丢弃，不进入新 TODO。当前项目嵌入式路线以 STM32F1 为主，ESP32 可作为远期平台，RP 不再占规划位。 |
| `todo/content/...` 旧路径引用 | 不迁移原路径，只迁移任务意图。 |
| 过细的单篇 TODO | 合并到卷级 checklist，不再保留独立文件。 |
| 站点质量、翻译维护、发布治理规则 | 不作为 TODO 文件迁移；保存在 `CONTRIBUTING.md`。 |

## 建议审阅顺序

1. 先看 `000-project-roadmap.md` 判断整体优先级。
2. 再看 `014-vol5-concurrency.md`、`017-vol8-domains.md`、`012-vol3-standard-library.md` 三个高价值文件。
3. 确认旧 TODO 映射是否遗漏。
4. 基于新的 TODO 文件进入深度优化和发展规划阶段。
