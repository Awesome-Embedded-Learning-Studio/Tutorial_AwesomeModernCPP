---
id: "020"
title: "贯穿式实战项目 TODO"
category: content
priority: P2
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: ["012", "014", "017"]
blocks: []
estimated_effort: epic
---

# 贯穿式实战项目 TODO

## Current Assets

### 已有实质实现的资产

- **Mini Concurrent Runtime**：卷五 capstone（`documents/vol5-concurrency/exercises/06-capstone-mini-runtime.md`）已完成，含 4 里程碑、前置知识、验收标准。7 个 Lab（Lab 0~5 + capstone）构成完整渐进式练习体系。
- **Coroutine Echo Server**：卷五 ch06（`documents/vol5-concurrency/ch06-async-io-coroutine/05-coroutine-echo-server.md`）已有 54KB 完整教程 + 1255 行可编译代码 + 性能基准测试。
- **OnceCallback 完整系列**：卷九（`documents/vol9-open-source-project-learn/chrome/01_once_callback/`）已有 16 篇教程 + 代码，可作为"开源项目研读 → 可复用组件"范例。
- **卷五实验系列**：`documents/vol5-concurrency/exercises/` 含 Lab 0~5 共 7 个练习文档 + index 导航，已有完整依赖图。
- **卷五项目设计大纲**：`.claude/chapter-projects-outline.md`（27KB CS144 风格设计文档）。

### 已有代码的资产

- **STM32F1 工程**：`code/stm32f1-tutorials/` 含 LED（C++23 模板）、Button（事件处理）、UART（中断驱动）三个可构建工程。
- **卷三 allocator 代码**：`code/examples/chapter07/06_custom_allocator/` 含 bump_allocator、stack_allocator 等 7 个文件。
- **ETL demo**：`code/examples/chapter07/05_etl/` 含 ETL vector 参考实现。
- **卷五协程代码**：`code/volumn_codes/vol5/ch06-async-io-coroutine/` 含协程 echo server 基础代码。

### 仅规划入口的资产

- `documents/projects/index.md`：列出 6 个项目名称，无实质内容，状态标注"规划中"。
- `documents/en/projects/index.md`：英文翻译版，同上。

### 旧规划来源（已合并）

- `208-projects-outline.md`：6 个项目完整规划（STL、HTTP、GUI、Embedded OS、INI、Echo Server）。
- `021-stm32f1-projects.md`：4 个 STM32F1 综合项目（Serial CLI、模拟采集器、参数保存、低功耗唤醒），含详细架构图和验收标准。
- `037-iot-project.md`：IoT 网关项目，8 章教程结构，已降级为远期候选。

## Gaps

### 项目基础设施缺失

- 缺项目卡模板：目标、前置知识、里程碑、代码位置、验收标准。
- 缺 `code/projects/` 目录：所有项目代码统一存放于此，当前不存在。
- 缺项目和卷章节的依赖图。
- `documents/projects/index.md` 仅为项目名列表，读者无法判断项目规模和准备状态。

### 已有资产未接入项目页

- Echo Server 已有完整实现，但项目页仍标注"规划中"，无链接。
- capstone 已有完整文档，但项目页未提及。
- OnceCallback 可作为 reusable component 范例，但项目页未提及。
- allocator 代码可作为 Mini STL 基础素材，但项目页未提及。

### 前置内容未就绪

- CLI Framework 的持久化能力依赖 Flash 读写教程（卷八 Flash 在嵌入式外设链后续位置）。
- small_vector / fixed_vector 在卷三中无任何内容。

## Development Principles

- 项目应建立在成熟内容上，不应抢在前置卷关键资产之前。
- 项目卡必须比文章更严格：能构建、能测试、能验收。
- 已有实质实现的资产（Echo Server、capstone）通过交叉链接接入，不做内容重复。
- 所有保存在本项目的项目代码统一放 `code/projects/`。
- 远期项目不拆独立 TODO。
- IoT 网关项目合并为远期候选，不保留单独文件。

## Concrete Improvements

### Phase 1: 三张项目卡

- **Mini Concurrent Runtime**：
  - 来源：卷五 capstone。
  - 目标：线程池、队列、定时器、channel、shutdown。
  - 依赖：卷五 ch01-ch08。
  - 代码仓：`code/projects/mini-concurrent-runtime/`。
  - 状态：卷五内容最成熟，可直接从 capstone 提取项目卡。

- **General CLI Framework**：
  - 来源：UART + Button + 事件驱动架构。
  - 目标：通用命令行框架——命令解析器、命令注册接口、帮助系统、参数处理。可在 STM32F1 上通过 UART 后端同样跑起（LED 控制、状态查询、参数保存）。
  - 依赖：卷八 embedded 01-03；持久化能力等 Flash 教程就绪后补齐。
  - 代码仓：`code/projects/general-cli-framework/`。
  - 参考：旧 `021-stm32f1-projects.md` 的 CLI 架构设计（`UartRx → LineBuffer → CommandParser → CommandDispatcher`）。

- **Mini STL Components**：
  - 来源：卷三标准库 + allocator 代码。
  - 目标：small_vector / fixed_vector、span、ring_buffer、allocator。
  - 依赖：卷三容器和 allocator 章节。不等卷三重建，直接启动。
  - 代码仓：`code/projects/mini-stl-components/`。

### Phase 2: 候选池

- **Mini HTTP Server**：等卷五 async/coroutine 和卷八 networking 成熟。
- **Mini GUI Framework**：远期，无前置内容就绪。
- **Embedded OS**：等 RTOS 路线成熟。
- **IoT Gateway**：远期，已从旧 TODO 037 降级。

### External References

- **INI Parser**：外部仓库 `Tutorial_cpp_SimpleIniParser`，不在本项目内创建独立项目。项目页添加外部引用链接即可。

### Maintenance: 已有资产接入

- 为 Echo Server 在项目页添加交叉链接（标注"基础实现见卷五 ch06"）。
- 为 capstone 在项目页添加交叉链接（指向卷五 exercises）。
- 充实 `documents/projects/index.md`：每个项目一张简要卡片（目标、前置知识、状态、代码位置）。
- 同步更新 `documents/en/projects/index.md`。

## Cross-Volume Dependency Map

```
卷三 (012, rebuild)  ──→ Mini STL Components（就绪：不等卷三，直接启动）
卷五 (014, active)   ──→ Mini Concurrent Runtime（就绪：可直接提取）
                       ──→ Echo Server（已完成：交叉链接）
卷八 (017, active)   ──→ General CLI Framework（部分就绪：LED/Button/UART 有代码，Flash 待补）
```

## Old TODO Merge

- `208-projects-outline.md`
- `021-stm32f1-projects.md`
- `037-iot-project.md`

## Acceptance

- [ ] 至少建立 3 个项目卡。
- [ ] 每个项目卡有依赖章节和验收标准。
- [ ] 已有资产通过交叉链接接入项目页。
- [ ] 远期项目保留候选池，不污染近期 roadmap。
- [ ] `documents/projects/index.md` 从"规划中"升级为有意义的导航页。
- [ ] 所有项目代码放 `code/projects/`，不在卷内目录重复存放。
- [ ] INI Parser 仅保留外部引用链接，不作为本项目内项目。
