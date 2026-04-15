# TODO 追踪系统

本目录是 Tutorial_AwesomeModernCPP 仓库的 TODO 追踪系统，按方向分类管理所有待办事项。

## 目录结构

```
todo/
├── architecture/    # 架构和重构 TODO（P0 优先）
├── content/         # 内容创建 TODO（P0-P1 优先）
├── automation/      # CI/CD 和自动化 TODO（P1 优先）
├── branding/        # 品牌和推广 TODO（P2 优先）
├── mkdocs/          # MkDocs 优化 TODO（P1 优先）
├── community/       # 社区和贡献 TODO（P2 优先）
├── translation/     # 翻译流水线 TODO（P2 优先）
└── interactive/     # 交互式元素 TODO（P3 优先）
```

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

## 编号规则

- `001-009`：架构相关（architecture/）
- `010-049`：内容相关（content/）
- `050-069`：自动化相关（automation/）
- `070-079`：品牌相关（branding/）
- `080-089`：MkDocs 相关（mkdocs/）
- `090-099`：社区相关（community/）
- `100-109`：翻译相关（translation/）
- `110-119`：交互相关（interactive/）

## 模板

新建 TODO 文件时，使用 `.templates/todo-template.md` 模板。

## 归档

完成的 TODO 文件从对应分类目录移入 `todo/archive/`，保留原始内容便于回溯。归档文件的 status 字段标记为 `done`，验收标准中已完成的项标记 `[x]`。

已完成：001（创建归档分支 archive/legacy_20260415）
