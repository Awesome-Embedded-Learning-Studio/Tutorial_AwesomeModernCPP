---
id: "300"
title: "documents/ 目录重组实施方案"
category: architecture
priority: P0
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: []
blocks: ["200", "201", "202", "203", "204", "205", "206", "207", "208", "209"]
estimated_effort: large
---

# documents/ 目录重组实施方案

## 目标

将现有 documents/ 目录从嵌入式单一主题结构重组为 9 卷通用现代 C++ 教程结构。这是整个内容重写工程的前置步骤，必须在所有内容 TODO 开始前完成。

## 当前结构

```
documents/
├── core-embedded-cpp/        (81 files, 13 chapters)
├── stm32f1-challenge/        (30 files)
├── compilation-deep-dive/    (10 files)
├── cpp-features/             (5 files)
├── cpp-engineering/          (3 files)
├── environment-setup/        (2 files)
├── debugging/                (1 file)
├── parallel-computing/       (1 file)
├── cpp-templates/            (1 file, placeholder)
└── stylesheets/, javascripts/, index.md, tags.md, .pages
```

## 目标结构

```
documents/
├── vol1-fundamentals/        (卷一：C++ 基础入门)
├── vol2-modern-features/     (卷二：现代 C++ 特性)
├── vol3-standard-library/    (卷三：标准库深入)
├── vol4-advanced/            (卷四：高级主题)
├── vol5-concurrency/         (卷五：并发编程全史)
├── vol6-performance/         (卷六：性能优化)
├── vol7-engineering/         (卷七：软件工程实践)
├── vol8-domains/             (卷八：领域应用)
│   ├── embedded/
│   ├── networking/
│   ├── gui-graphics/
│   ├── data-storage/
│   └── algorithms/
├── compilation/              (编译与链接深入)
├── projects/                 (贯穿式实战项目)
├── stylesheets/
├── javascripts/
├── index.md
├── tags.md
└── .pages
```

## 验收标准

- [ ] 新目录结构已创建（包含所有子目录和 .pages 文件）
- [ ] 每个 .pages 文件配置了正确的导航标题和排序
- [ ] 旧内容已移动到 `archive/legacy-content/` 保留
- [ ] mkdocs.yml 导航配置已更新
- [ ] `mkdocs serve` 可正常启动并显示新导航
- [ ] 所有旧链接已更新或添加重定向
- [ ] CI lint 检查通过

## 实施步骤

### 步骤 1：创建目录结构

```bash
# 创建卷级目录
mkdir -p documents/vol1-fundamentals
mkdir -p documents/vol2-modern-features
mkdir -p documents/vol3-standard-library
mkdir -p documents/vol4-advanced
mkdir -p documents/vol5-concurrency
mkdir -p documents/vol6-performance
mkdir -p documents/vol7-engineering
mkdir -p documents/vol8-domains/embedded
mkdir -p documents/vol8-domains/networking
mkdir -p documents/vol8-domains/gui-graphics
mkdir -p documents/vol8-domains/data-storage
mkdir -p documents/vol8-domains/algorithms
mkdir -p documents/compilation
mkdir -p documents/projects
```

### 步骤 2：创建 .pages 文件

每个卷和子目录需要 `.pages` 文件用于 MkDocs 导航：

```yaml
# documents/.pages
nav:
  - 首页: index.md
  - 卷一 · 基础入门: vol1-fundamentals/
  - 卷二 · 现代特性: vol2-modern-features/
  - 卷三 · 标准库深入: vol3-standard-library/
  - 卷四 · 高级主题: vol4-advanced/
  - 卷五 · 并发编程: vol5-concurrency/
  - 卷六 · 性能优化: vol6-performance/
  - 卷七 · 工程实践: vol7-engineering/
  - 卷八 · 领域应用: vol8-domains/
  - 编译与链接: compilation/
  - 实战项目: projects/
```

### 步骤 3：归档旧内容

```bash
mkdir -p documents/archive/legacy-content
git mv documents/core-embedded-cpp documents/archive/legacy-content/
git mv documents/stm32f1-challenge documents/archive/legacy-content/
git mv documents/cpp-features documents/archive/legacy-content/
git mv documents/cpp-engineering documents/archive/legacy-content/
git mv documents/environment-setup documents/archive/legacy-content/
git mv documents/debugging documents/archive/legacy-content/
git mv documents/parallel-computing documents/archive/legacy-content/
git mv documents/cpp-templates documents/archive/legacy-content/
```

### 步骤 4：迁移可保留内容

```bash
# 编译链接深入（保留原位，仅改名）
git mv documents/compilation-deep-dive/* documents/compilation/
```

### 步骤 5：创建索引文件

每个卷创建 `index.md` 作为卷首页：

```markdown
---
title: "卷N：XXX"
description: "卷N概述"
---

# 卷N：XXX

> 状态：规划中 / 编写中 / 已完成

## 概述
本卷覆盖...

## 章节导航
- [ch00：XXX](ch00-xxx/)
- ...
```

### 步骤 6：更新 mkdocs.yml

更新导航配置以匹配新目录结构。

### 步骤 7：验证

```bash
# 本地验证
mkdocs serve

# 检查链接
python3 scripts/check_links.py

# Lint 检查
markdownlint documents/
```

## 风险与注意事项

1. **不要立即删除旧内容**：归档而非删除，确保不丢失任何内容
2. **mkdocs-awesome-pages 兼容**：确保 .pages 文件格式正确
3. **CI 流水线**：更新 lint 和 deploy 工作流以适应新结构
4. **搜索索引**：新结构可能影响搜索，需要验证
5. **外部链接**：GitHub Pages 上已发布的链接可能失效，考虑重定向

## 涉及文件

- `documents/.pages` — 主导航配置
- `documents/*/index.md` — 各卷首页
- `documents/*/.pages` — 各卷导航配置
- `mkdocs.yml` — 站点配置更新
- `.github/workflows/deploy.yml` — 可能需要更新
