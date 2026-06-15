---
id: “018”
title: “卷九：开源项目学习 TODO”
category: content
priority: P2
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: large
---

# 卷九：开源项目学习 TODO

## 定位

卷九是前置八卷的**总结和实战**：通过学习开源项目来理解 C++ 能做什么、代码规范和设计模式。不限于 Chrome Base，任何有教育价值的开源项目均可纳入。

- `full`（完整教学路径）和 `hands_on`（设计导向快速路径）是项目特色，**两条路径互不引用**，各自独立成线。
- 后续新项目同样遵循 full + hands_on 双路径模式。

## Current Assets

### 文档

- `documents/vol9-open-source-project-learn/`：20 个 .md（4 个 index + 16 篇正文）
- OnceCallback full 路径：13 篇正文（7 前置知识速查 + 6 实现步骤）+ 1 index
- OnceCallback hands_on 路径：3 篇设计指南 + 1 index
- 英文翻译：`documents/en/vol9-open-source-project-learn/` 全量覆盖，100%
- `.pages`：正常

### 代码

- `code/volumn_codes/vol9/chrome_design/once_callback/once_callback.hpp`（117 行）
- `code/volumn_codes/vol9/chrome_design/once_callback/once_callback_impl.hpp`（81 行）
- `code/volumn_codes/vol9/chrome_design/cancel_token/cancel_token.hpp`（18 行）
- `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp`（~105 行，12 个 Catch2 测试）
- `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`（11 个 C++ 教学示例）
- CMake 构建系统：3 个 CMakeLists.txt，CPM + Catch2，C++23 标准

### 资产确认状态（2026-06-10）

| 检查项 | 状态 |
|--------|------|
| 文章与代码接口签名一致性 | ✅ 全部一致（run、bind_once、then、cancel、查询方法） |
| 三态逻辑一致性 | ✅ 一致（kEmpty/kValid/kConsumed） |
| 取消行为一致性 | ✅ 一致（void 静默 / non-void 抛 bad_function_call） |
| 移动语义一致性 | ✅ 一致 |
| 测试覆盖度 | ✅ 12 个测试覆盖 A-F 六类不变量 |
| 英文翻译 | ✅ 全量覆盖 |
| markdownlint | ✅ 通过（full/index.md 代码块已加语言标记） |

## Maintenance Tasks

- [x] **代码同步 `Status : uint8_t`**：已完成，`once_callback.hpp` 的 `enum class Status : uint8_t` 与文章 01-2 一致
- [x] **修正测试计数**：已完成，文章 01-6 已写 “12 个”，代码 12 个 TEST_CASE
- [x] **lint 修复**：已完成，`full/index.md` 代码块已加 `text` 语言标记
- [ ] **补充 Google Benchmark**：为 01-6 性能对比章节补充 benchmark 代码，将”预估”升级为实测数据

## Incremental Candidates

> 2026-06-10 维护者确认结论。按启动时机排列。

### 近期

| 增量项 | 判定 | 理由 |
|--------|------|------|
| I1: 源码阅读方法论导论 | **先做** | 卷九入口基建，工作量小（1 篇），放大后续所有项目；合并原”可迁移经验”+”不要照搬”索引页 |
| I2: WeakPtr / pointer safety 主线 | I1 之后启动 | 连接面最广（卷二、卷五、卷八），OnceCallback 取消令牌已铺路，规模可控 |

### 中期

| 增量项 | 判定 | 理由 |
|--------|------|------|
| I3: TaskRunner / sequence / thread pool 主线 | I2 完成后评估启动时机 | 价值最高但规模风险大；依赖卷五并发主线成熟度 |
| I8: 工业级并发专题（Asio、Folly/oneTBB、moodycamel） | 纳入卷九，卷五成熟后启动 | 卷九定位涵盖任何有教育价值的开源项目；放在 `concurrency/` 目录，遵循 full+hands_on 双路径 |

### cross-link only

| 增量项 | 判定 | 处理 |
|--------|------|------|
| I4: logging / CHECK / DCHECK | cross-link only | 核心知识属卷七，卷七推进时提及 Chrome 做法 |
| I5: span / buffer safety | cross-link only | 卷三 span 文章加延伸段落提及 Chrome 迁移经验 |

### 远期候选

| 增量项 | 判定 | 理由 |
|--------|------|------|
| I6: containers / string / time | 远期 | 等卷三重建、卷六推进后再评估读者需求 |

### 已放弃

| 增量项 | 处理 |
|--------|------|
| I9: 可复用组件库（10 个 header-only 组件） | 放弃，不迁移。个别组件如教学需要，作为对应项目主线的一部分自然产出 |
| I7: “可迁移经验”+”不要照搬”索引页 | 合并到 I1 源码阅读方法论导论 |

### 新开源项目候选（2026-06-10 调研）

> 卷九定位不限于 Chrome Base。以下候选按梯队排列，调研完成。

#### 第一梯队：强烈推荐纳入

| 候选 | 核心教学价值 | 连接卷 | 规模 | 可简化性 |
|------|-------------|--------|------|---------|
| **spdlog** | Strategy 模式（pluggable sink）、Singleton/Registry、Facade、异步 producer-consumer、变参模板 | 卷二（RAII、智能指针）、卷五（异步）、卷七（CMake） | full 6-8 + hands_on 2-3 | 极好：mini-spdlog ~200 行 |
| **fmtlib ({fmt})** | 变参模板 + fold expression、编译期格式字符串解析、类型擦除、SBO；C++20 `std::format` 先行者 | 卷二（模板、constexpr）、卷四（TMP、concepts）、卷六（零开销） | full 5-7 + hands_on 2 | 好：mini-fmt ~300 行，但内部 TMP 深度大 |
| **ETL (Embedded Template Library)** | 无堆分配设计、侵入式容器、Observer/FSM/消息总线/单例等设计模式、SPSC 队列；嵌入式典范 | 卷八（嵌入式）、卷三（容器）、卷五（SPSC） | 选 4-5 组件：full 5-8 + hands_on 3 | 极好：每个组件独立小头文件 |
| **moodycamel ConcurrentQueue** | `std::atomic` + 显式 memory ordering 实战、CAS 循环、per-block 原子索引、thread-local token | 卷五（atomic、memory order、无锁） | full 5-7 + hands_on 2-3 | 从 SPSC ~100 行起步，渐进到 MPMC |

#### 第二梯队：有价值，视时机纳入

| 候选 | 核心教学价值 | 判定 |
|------|-------------|------|
| **ankerl::unordered_dense** | 开放寻址 + Robin Hood 哈希 + 连续内存布局，缓存友好数据结构教学案例 | 规模偏小（3-4 篇），建议和另一个”性能容器”配对；远期 |
| **nlohmann/json** | variant 判别联合、运算符重载、模板元编程 | 补充候选，教学增量不如 Tier 1 |

#### 第三梯队：条件性纳入

| 候选 | 判定 | 理由 |
|------|------|------|
| **ctre** | 条件纳入 | TMP 巅峰，适合卷四高级读者；可考虑 hands_on only |
| **Boost.Asio** | 中期（已在 I8 规划） | Proactor + 协程有价值，但规模大（8-12 篇） |
| **concurrencpp** | Asio 轻量替代 | executor + coroutine 更聚焦，社区较小 |
| **folly** | 仅选择性提取组件 | 构建复杂，不整体研读；可提取 `small_vector` 等单独做 |
| **abseil** | 低优先级 | Bazel 构建、Google 风格，教学摩擦大 |
| **ranges-v3** | 低优先级 | C++20 `<ranges>` 已标准化，历史价值 > 教学价值 |
| **llhttp** | 不纳入 | 纯 C 库 |
| **cpp-httplib** | 补充候选 | 适合卷八网络编程，卷九优先级低 |
| **Catch2** | 补充候选 | REQUIRE 宏的表达式模板技巧有教学价值 |

---

## 完整增量路线图

> 2026-06-10 维护者确认。合并已有规划和新调研候选。

### 第一批：近期（OnceCallback 巩固之后）

1. **I1 源码阅读方法论导论** — 卷九入口基建
2. **I2 WeakPtr 主线**（Chrome）— 连接卷二/卷五/卷八
3. **spdlog 主线** — 独立于 Chrome 的首个非 Chrome 项目

### 第二批：中期

4. **I3 TaskRunner 主线**（Chrome）— 依赖卷五成熟度
5. **moodycamel ConcurrentQueue**（即 I8 并发专题第一篇）
6. **fmtlib** — 和 spdlog 自然配对，模板深度需要前置知识

### 第三批：远期

7. **ETL 精选组件** — 嵌入式方向，连接卷八
8. **ankerl::unordered_dense** — 性能容器，配对
9. I8 剩余并发专题（Asio / Folly / concurrencpp）

## Development Points

- 每个开源研读主题必须产出：最小背景、源码设计动机、教学版简化实现、可迁移经验、不应照搬的边界
- 不追求 Chrome Base 全覆盖
- 每个新项目遵循 full + hands_on 双路径模式，两条路径互不引用

## Old TODO Merge

- `210-vol9-chrome-base-outline.md`

## Acceptance

- [ ] OnceCallback 路线成为示范模板（文章与代码一致、测试完整、性能有实测数据）
- [ ] 下一阶段按路线图顺序从第一批启动
- [ ] Chrome Base 大纲从 20+ 篇压缩为可执行候选池
