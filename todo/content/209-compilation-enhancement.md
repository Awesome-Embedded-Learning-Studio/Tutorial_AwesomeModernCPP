---
id: "209"
title: "编译与链接深入系列增强计划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200"]
blocks: []
estimated_effort: medium
---

# 编译与链接深入系列增强计划

## 总览

- **目录位置**：`documents/compilation/`
- **现有**：10 篇（保留并增强）
- **新增**：2-3 篇
- **难度**：intermediate → advanced

## 现有文章保留计划

现有 10 篇编译链接深入文章质量较高，保留但需：
1. 统一 frontmatter 格式
2. 更新代码示例到现代 C++
3. 增加跨平台（Linux/Windows）说明

## 新增文章

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 11 | 11-lto-and-optimization.md | 链接时优化(LTO) | LTO 原理、ThinLTO、跨模块优化、实践 | LTO 性能测试 |
| 12 | 12-build-systems-deep.md | 构建系统深度对比 | Make/Ninja/CMake/MSBuild、依赖分析、增量构建 | 构建系统选型 |
| 13 | 13-binary-analysis.md | 二进制分析工具 | nm/readelf/objdump/strings、strip、调试符号 | 二进制分析实战 |

## 练习
- 每篇增加 2-3 道实践练习
- 新增跨平台编译链接综合练习
