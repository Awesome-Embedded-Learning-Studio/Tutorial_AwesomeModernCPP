# TODO 追踪系统

本目录是 Tutorial_AwesomeModernCPP 仓库的 TODO 追踪系统，所有待办事项以扁平化 Markdown 文件管理。

## 文件组织

所有 TODO 文件直接存放于 `todo/` 根目录，按编号区间区分类别：

| 编号区间 | 类别 | 说明 |
|----------|------|------|
| 001–009 | 架构 | 架构和重构 TODO |
| 010–049 | 内容 | 内容创建 TODO |
| 050–069 | 自动化 | CI/CD 和自动化 TODO |
| 070–079 | 品牌 | 品牌和推广 TODO |
| 080–089 | 文档系统 | 文档系统优化 TODO |
| 090–099 | 社区 | 社区和贡献 TODO |
| 100–109 | 翻译 | 翻译流水线 TODO |
| 110–119 | 交互 | 交互式元素 TODO |
| 200–299 | 大纲 | 各卷内容大纲 |

## 优先级定义

| 级别 | 含义 | 示例 |
|------|------|------|
| P0 | 必须先做，阻塞其他工作 | 目录迁移、MkDocs 配置更新 |
| P1 | 重要，影响下一个 Release | STM32F1 外设教程、RTOS 调度器、CI 编译 |
| P2 | 显著价值提升 | ESP32/RP2040、品牌建设、翻译流水线 |
| P3 | 锦上添花 | 在线编译器、汇编查看器、视频内容 |

## TODO 文件规范

每个 TODO 是一个独立的 Markdown 文件，使用以下 frontmatter：

```yaml
---
id: XXX                          # 唯一编号，与文件名编号一致
title: "描述性标题"
category: architecture|content|automation|branding|mkdocs|community|translation|interactive
priority: P0|P1|P2|P3
status: pending|in-progress|blocked|done
created: YYYY-MM-DD
assignee: charliechen
depends_on: []                   # 依赖的 TODO ID 列表
blocks: []                       # 被本 TODO 阻塞的 TODO ID 列表
estimated_effort: small|medium|large|epic
---
```

文件体包含：目标、验收标准（可勾选）、实施说明、涉及文件、参考资料。

## 状态说明

| 状态 | 含义 |
|------|------|
| pending | 未开始 |
| in-progress | 进行中 |
| blocked | 被阻塞（等待依赖完成） |
| done | 已完成 |

## 模板

新建 TODO 文件时，使用 `.templates/todo-template.md` 模板。

## 归档

完成的 TODO 文件可直接删除或保留于本目录，status 字段标记为 `done`。

已完成：001（创建归档分支 archive/legacy_20260415）、201（卷二现代 C++ 特性大纲，44 篇文章全部到位）
