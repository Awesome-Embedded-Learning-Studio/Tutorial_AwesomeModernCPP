---
id: "000"
title: "项目总路线图 TODO"
category: governance
priority: P0
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: large
---

# 项目总路线图 TODO

## Current Assets

- `documents/` 已形成 10 卷主结构。
- `README.md` 有内容地图、学习路径、版本状态和质量命令。
- `site/.vitepress/config/nav.ts`、`sidebar.ts` 已自动接入各卷。
- `changelogs/` 已有 v0.1.0 到 v0.4.0。
- `scripts/` 已有链接检查、frontmatter、coverage、示例构建等工具。
- `todo/` 旧系统已有大量可复用规划，但文件过多、粒度过细、路径漂移。

## Gaps

- 旧 TODO 目录同时承担路线图、主题任务、单篇文章计划、社区任务，边界混乱。
- 部分旧 TODO 指向 `todo/content/...`，当前路径已不存在。
- 卷三、卷四、卷六、卷七需要重建主线。
- 卷五、卷八、卷九已有强资产，但旧 TODO 没准确反映当前状态。
- 贯穿式项目、QA 没有独立清晰入口。
- 发布治理、翻译治理、站点质量属于规则说明，不进入正式 TODO 文件列表。

## Worth Continuing

值得继续。项目已经不是小型教程，而是多卷工程化教程，必须有一套轻量但清晰的路线图。

## Priority Roadmap

### P0: 稳定新 TODO 入口

- 确认当前 `todo/` 覆盖旧 TODO。
- 维护 `todo/README.md`、`000-project-roadmap.md` 作为统一入口。
- 明确哪些旧 TODO 合并、废弃、保留历史。
- 社区建设 TODO 和 QA TODO 保留在新 TODO 目录。
- 站点质量、翻译、发布等规则说明保存在 `CONTRIBUTING.md`。

### P1: 补最有价值的内容线

- 卷五并发：capstone、测试、代码覆盖、前沿路线。
- 卷八嵌入式：STM32F1 外设链和 RTOS 链；丢弃 RP2040 路线。
- 卷三标准库：从平铺专题重建成标准库学习路径。
- 卷四高级主题：模板/Concepts/Ranges/协程/Modules，收敛 C++26 前沿。

### P2: 重建工程和性能线

- 卷七工程实践：测试、CI、包管理、静态分析、发布流程。
- 卷六性能优化：benchmark、profiling、cache、SIMD、汇编阅读。
- 卷九 Chrome Base：从 OnceCallback 样板扩展到 WeakPtr/TaskRunner 两条主线。
- 编译链接与参考卡：维护增强和交叉链接。

### P3: 远期扩展

- 卷八非嵌入式领域：networking、GUI、data-storage、algorithms。
- 贯穿式大型项目。
- 卷十课程笔记持续扩展。

## Development Points

- 所有大主题都必须回答“已有资产、缺口、下一步、验收标准”。
- 单篇文章级任务不再新建独立 TODO，除非它阻塞整卷发布。
- C++26/frontier 内容必须标记 “needs standard-status verification”，除非已完成来源核验。
- 旧 TODO 中 RP2040/RP 嵌入式路线不迁移。
- 社区治理规则说明不混入 TODO 文件。

## Acceptance

- [ ] 新 TODO 文件数控制在 15-20 个。
- [ ] 每个旧 TODO 有明确归宿或丢弃说明。
- [ ] 每卷都有资产、缺口、价值判断。
- [ ] 当前 `todo/` 只保留内容发展 TODO 和社区建设 TODO。
- [ ] 进入深度优化和发展规划阶段前完成一次人工审阅。
