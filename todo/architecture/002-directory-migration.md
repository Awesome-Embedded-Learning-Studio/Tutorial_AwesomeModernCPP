---
id: 002
title: "目录结构迁移：tutorial/ + codes_and_assets/ -> documents/ + code/"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [001]
blocks: [003, 004, 005, 006, 007]
estimated_effort: epic
---

# 目录结构迁移：tutorial/ + codes_and_assets/ -> documents/ + code/

## 目标

将项目从当前的 `tutorial/` + `codes_and_assets/` 双目录结构迁移到更清晰的 `documents/` + `code/` 结构。这是整个架构重构的核心任务，涉及大量文件的移动和重命名。

核心原则：
1. 使用 `git mv` 而非普通的 `mv`，以保留完整的 git 历史追踪
2. 将所有中文目录名转换为 ASCII 目录名（使用拼音或英文翻译），以避免跨平台编码问题
3. 迁移完成后，旧的 `tutorial/` 和 `codes_and_assets/` 目录应被完全移除

## 验收标准

- [ ] 所有 `tutorial/` 下的内容已按映射表迁移到 `documents/` 对应子目录
- [ ] 所有 `codes_and_assets/` 下的内容已按映射表迁移到 `code/` 对应子目录
- [ ] 中文的目录名全部转换为 ASCII 名称（如 `核心：现代嵌入式C++教程` -> `core-embedded-cpp`）
- [ ] 所有文件使用 `git mv` 操作，历史可追溯（`git log --follow <file>` 可查到完整历史）
- [ ] 旧的 `tutorial/` 和 `codes_and_assets/` 目录已不存在
- [ ] `documents/` 下包含 `index.md`（站点首页）和 `stylesheets/` 目录
- [ ] 迁移后的目录树中无空目录
- [ ] 单独执行一次 `mkdocs build` 验证基础结构可用（此时尚不需要全部链接正确）

## 实施说明

### 当前目录结构分析

**`tutorial/` 当前包含：**
```
tutorial/
  index.md                          # 站点首页
  tags.md                           # 标签索引
  Awesome-Embedded.ico              # favicon
  Awesome-Embedded.png              # logo
  stylesheets/
    extra.css                       # 自定义样式
  核心：现代嵌入式C++教程/
    Chapter0/ ~ Chapter12/          # 核心教程（13个章节目录）
  现代C++特性/
    现代C++的协程/                   # 协程专题
  现代C++模板教程/
    卷一：模板基础-C++11-14核心机制/
    卷二：现代模板技术-C++17特性/
    卷三：元编程精要-C++20-23约束与元编程/
    卷四：泛型设计模式实战-架构级应用/
  现代C++工程实践/
    文件处理/                       # 文件IO教程
  深入理解CC++编译特性指南/
    深入理解CC++的编译与链接技术2：重用概念的再阐述/
  环境配置/                         # 开发环境配置指南
  并行计算C++/                      # 并行计算教程
  调试专题/                         # 调试专题
  挑战：使用现代C_C++编写STM32F103C8T6/
    00_env_setup/
    01_led/
    02_button/
```

**`codes_and_assets/` 当前包含：**
```
codes_and_assets/
  README.md
  examples/                         # 章节代码示例（chapter02 ~ chapter12）
  project_setup/                    # 工程模板
  stm32f1_tutorials/                # STM32F1 系列教程代码
    00_env_setup/
    01_led/
    02_button/
  templates/                        # 项目模板
```

### 目录映射表

#### 文档目录映射 (`tutorial/` -> `documents/`)

| 原路径 | 新路径 | 说明 |
|--------|--------|------|
| `tutorial/index.md` | `documents/index.md` | 站点首页 |
| `tutorial/tags.md` | `documents/tags.md` | 标签索引页 |
| `tutorial/Awesome-Embedded.ico` | `documents/Awesome-Embedded.ico` | favicon |
| `tutorial/Awesome-Embedded.png` | `documents/Awesome-Embedded.png` | logo |
| `tutorial/stylesheets/` | `documents/stylesheets/` | CSS 样式 |
| `tutorial/核心：现代嵌入式C++教程/` | `documents/core-embedded-cpp/` | 核心嵌入式C++教程 |
| `tutorial/核心：现代嵌入式C++教程/Chapter0/` | `documents/core-embedded-cpp/chapter-00-introduction/` | 第0章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter1/` | `documents/core-embedded-cpp/chapter-01-design-constraints/` | 第1章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter2/` | `documents/core-embedded-cpp/chapter-02-zero-overhead/` | 第2章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter3/` | `documents/core-embedded-cpp/chapter-03-types-containers/` | 第3章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter4/` | `documents/core-embedded-cpp/chapter-04-compile-time/` | 第4章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter5/` | `documents/core-embedded-cpp/chapter-05-memory-management/` | 第5章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter6/` | `documents/core-embedded-cpp/chapter-06-ownership-raii/` | 第6章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter7/` | `documents/core-embedded-cpp/chapter-07-error-handling/` | 第7章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter8/` | `documents/core-embedded-cpp/chapter-08-concurrency/` | 第8章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter9/` | `documents/core-embedded-cpp/chapter-09-rtos-threads/` | 第9章 |
| `tutorial/核心：嵌入式C++教程/Chapter10/` | `documents/core-embedded-cpp/chapter-10-interrupts-isr/` | 第10章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter11/` | `documents/core-embedded-cpp/chapter-11-peripherals-drivers/` | 第11章 |
| `tutorial/核心：现代嵌入式C++教程/Chapter12/` | `documents/core-embedded-cpp/chapter-12-design-patterns/` | 第12章 |
| `tutorial/现代C++特性/` | `documents/cpp-features/` | C++特性专题 |
| `tutorial/现代C++特性/现代C++的协程/` | `documents/cpp-features/coroutines/` | 协程 |
| `tutorial/现代C++模板教程/` | `documents/cpp-templates/` | 模板教程 |
| `tutorial/现代C++模板教程/卷一：模板基础-C++11-14核心机制/` | `documents/cpp-templates/vol1-basics-cpp11-14/` | 卷一 |
| `tutorial/现代C++模板教程/卷二：现代模板技术-C++17特性/` | `documents/cpp-templates/vol2-modern-cpp17/` | 卷二 |
| `tutorial/现代C++模板教程/卷三：元编程精要-C++20-23约束与元编程/` | `documents/cpp-templates/vol3-metaprogramming-cpp20-23/` | 卷三 |
| `tutorial/现代C++模板教程/卷四：泛型设计模式实战-架构级应用/` | `documents/cpp-templates/vol4-generics-patterns/` | 卷四 |
| `tutorial/现代C++工程实践/` | `documents/cpp-engineering/` | 工程实践 |
| `tutorial/现代C++工程实践/文件处理/` | `documents/cpp-engineering/file-io/` | 文件处理 |
| `tutorial/深入理解CC++编译特性指南/` | `documents/compilation-deep-dive/` | 编译特性指南 |
| `tutorial/环境配置/` | `documents/environment-setup/` | 环境配置 |
| `tutorial/并行计算C++/` | `documents/parallel-computing/` | 并行计算 |
| `tutorial/调试专题/` | `documents/debugging/` | 调试专题 |
| `tutorial/挑战：使用现代C_C++编写STM32F103C8T6/` | `documents/stm32f1-challenge/` | STM32F1 挑战 |
| `tutorial/挑战：使用现代C_C++编写STM32F103C8T6/00_env_setup/` | `documents/stm32f1-challenge/00-env-setup/` | 环境搭建 |
| `tutorial/挑战：使用现代C_C++编写STM32F103C8T6/01_led/` | `documents/stm32f1-challenge/01-led/` | LED 教程 |
| `tutorial/挑战：使用现代C_C++编写STM32F103C8T6/02_button/` | `documents/stm32f1-challenge/02-button/` | 按钮教程 |

#### 代码目录映射 (`codes_and_assets/` -> `code/`)

| 原路径 | 新路径 | 说明 |
|--------|--------|------|
| `codes_and_assets/examples/` | `code/examples/` | 章节代码示例 |
| `codes_and_assets/project_setup/` | `code/project-setup/` | 工程模板 |
| `codes_and_assets/stm32f1_tutorials/` | `code/stm32f1-tutorials/` | STM32F1 教程代码 |
| `codes_and_assets/templates/` | `code/templates/` | 项目模板 |
| `codes_and_assets/README.md` | `code/README.md` | 代码目录说明 |

### 执行脚本模板

建议编写一个迁移脚本（一次性使用），按以下模式执行：

```bash
#!/bin/bash
set -euo pipefail

# 创建目标根目录
mkdir -p documents code

# === 顶层文件 ===
git mv tutorial/index.md documents/index.md
git mv tutorial/tags.md documents/tags.md
git mv tutorial/Awesome-Embedded.ico documents/Awesome-Embedded.ico
git mv tutorial/Awesome-Embedded.png documents/Awesome-Embedded.png
git mv tutorial/stylesheets documents/stylesheets

# === 核心 C++ 教程 ===
mkdir -p documents/core-embedded-cpp
git mv "tutorial/核心：现代嵌入式C++教程/Chapter0" documents/core-embedded-cpp/chapter-00-introduction
git mv "tutorial/核心：现代嵌入式C++教程/Chapter1" documents/core-embedded-cpp/chapter-01-design-constraints
# ... 按映射表继续

# === 代码目录 ===
git mv codes_and_assets/examples code/examples
git mv codes_and_assets/project_setup code/project-setup
git mv codes_and_assets/stm32f1_tutorials code/stm32f1-tutorials
git mv codes_and_assets/templates code/templates
git mv codes_and_assets/README.md code/README.md

# === 清理空目录 ===
# git mv 不会自动删除空目录
find tutorial -type d -empty -delete 2>/dev/null || true
find codes_and_assets -type d -empty -delete 2>/dev/null || true
rmdir tutorial codes_and_assets 2>/dev/null || true
```

### 关键注意事项

1. **中文路径问题**：`git mv` 在 Linux 上可以处理 UTF-8 中文路径，但务必在终端中确认 locale 设置为 `en_US.UTF-8` 或 `C.UTF-8`。在脚本中用双引号包裹所有含中文的路径。

2. **`git mv` vs 普通移动**：必须使用 `git mv` 以确保 Git 能够追踪文件重命名历史。移动完成后可通过 `git log --follow documents/core-embedded-cpp/chapter-01-design-constraints/some-file.md` 验证历史完整性。

3. **分阶段提交**：建议将迁移拆分为多次提交，便于 review 和回滚：
   - 提交 1：创建 `documents/` 和 `code/` 目录，移动顶层文件
   - 提交 2：迁移核心嵌入式 C++ 教程（Chapter0-12）
   - 提交 3：迁移模板教程、工程实践等其余文档
   - 提交 4：迁移 `codes_and_assets/` 到 `code/`
   - 提交 5：清理空目录和旧目录

4. **大文件警告**：`codes_and_assets/examples/` 下可能有编译产物或二进制文件，迁移前确认 `.gitignore` 已覆盖。

5. **子模块检查**：确认项目没有使用 git submodules（经检查确认没有）。

## 涉及文件

- `tutorial/*` -> `documents/*`（约 100+ 个 markdown 文件和资源文件）
- `codes_and_assets/*` -> `code/*`（代码示例、模板、工程文件）
- 所有在中文目录下的 `.md`、`.cpp`、`.h`、`.png`、`.css` 文件

## 参考资料

- [drafts/Content-Table-Draft.md](../../drafts/Content-Table-Draft.md) - 完整的内容目录规划
- [drafts/Conten-STM32F103C8T6-Draft.md](../../drafts/Conten-STM32F103C8T6-Draft.md) - STM32F1 教程内容规划
- [Git - git-mv Documentation](https://git-scm.com/docs/git-mv)
- [Pro Git: Rewriting History](https://git-scm.com/book/en/v2/Git-Tools-Rewriting-History)
