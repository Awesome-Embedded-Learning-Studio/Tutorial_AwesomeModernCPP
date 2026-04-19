---
id: 029
title: "C++ 特性参考卡（Feature Reference Cards）"
category: content
priority: P2
status: in-progress
created: 2026-04-15
updated: 2026-04-18
assignee: charliechen
depends_on: ["028"]
blocks: []
estimated_effort: medium
---

# C++ 特性参考卡（Feature Reference Cards）

## 目标

为索引页（028）中的关键 C++ 特性创建精炼的结构化参考卡。每张参考卡提供 1 分钟可扫完的速查信息：核心 API 签名、最小可编译示例、嵌入式适用性评级、编译器支持。内容以 cppreference 为事实基础，提取改编为中文速查格式（不逐字翻译）。

## 设计决策

- **不做教程风格重写**：与 vol2/vol3 已有教程内容互补而非重复
- **按功能类别分目录**（而非按 C++ 标准版本），因为读者查找时按功能分类更直觉
- **chapter 统一编号 99**，侧边栏独立分区
- **CC-BY-SA 4.0 标注**：每页底部标注 cppreference 归属
- **工具链驱动**：用爬取器脚本 (`scripts/cppref_crawler.py`) 抓取 cppreference 内容，用 Claude API 生成器 (`scripts/cppref_card_generator.py`) 自动生成参考卡，保证准确性和风格统一

## 工具链

```
scripts/cppref_manifest.json      → 特性清单（URL、分类、输出路径）
scripts/cppref_crawler.py         → 抓取 cppreference → JSON 缓存
scripts/cppref_cache/raw/*.json   → 中间产物（可复用、可增量更新）
scripts/cppref_card_generator.py  → JSON + Claude API → .md 参考卡
```

## 验收标准

- [ ] 参考卡模板文件存在（`.templates/reference-card-template.md`）
- [ ] 按功能类别分目录：memory/、containers/、concurrency/、core-language/、templates/
- [ ] 每张参考卡包含统一模板：一句话、头文件、API 速查表、最小示例、嵌入式适用性、编译器支持、另见
- [ ] 第一批覆盖 10 个高频特性
- [ ] 每页底部包含 CC-BY-SA 4.0 归属声明
- [ ] 与 vol2/vol3 对应教程文章的交叉链接正确
- [ ] 所有页面通过 markdownlint 和 validate_frontmatter.py 校验
- [ ] MkDocs 渲染正常，侧边栏独立分区可见

## 目录结构

```
documents/
  cpp-reference/
    index.md                    # 028 索引页
    memory/                     # 内存管理
      01-unique-ptr.md
      02-shared-ptr.md
      03-optional.md
    containers/                 # 容器与视图
      01-span.md
      02-string-view.md
      03-variant.md
    concurrency/                # 并发
    core-language/              # 核心语言特性
      01-constexpr.md
      02-lambda.md
      03-auto-decltype.md
    templates/                  # 模板与元编程
      01-concepts.md
```

## 实施批次

**第一批（10 页，验证模板）：**
1. memory/01-unique-ptr.md — std::unique_ptr
2. memory/02-shared-ptr.md — std::shared_ptr + weak_ptr
3. memory/03-optional.md — std::optional
4. containers/01-span.md — std::span
5. containers/02-string-view.md — std::string_view
6. containers/03-variant.md — std::variant
7. core-language/01-constexpr.md — constexpr / consteval / constinit
8. core-language/02-lambda.md — lambda 表达式
9. core-language/03-auto-decltype.md — auto / decltype / decltype(auto)
10. templates/01-concepts.md — C++20 Concepts

**第二批（15 页，C++11/14 核心）：**
std::atomic, std::thread, std::move, nullptr, enum class, range-for, override/final, decltype, variadic templates, initializer_list, std::array, std::make_unique, std::exchange, 泛型 lambda, 折叠表达式

**第三批（15 页，C++17/20）：**
结构化绑定, if constexpr, std::filesystem, std::format, std::jthread, 三路比较(<=>), coroutines(基础), modules(概念), std::expected, std::generator, deducing this, std::flat_map, std::print, 内联变量, 嵌套命名空间

**第四批（按需补充）：** C++98/03 基础特性、边缘特性

## 涉及文件

- `.templates/reference-card-template.md`
- `scripts/cppref_manifest.json`
- `scripts/cppref_crawler.py`
- `scripts/cppref_card_generator.py`
- `documents/cpp-reference/` 下各子目录
- `todo/content/028-cpp-reference-index.md`（索引页，同步更新）

## 参考资料

- cppreference.com — 各特性详情页（CC-BY-SA 4.0）
- 本项目 vol2-modern-features/ 和 vol3-standard-library/ 中对应的教程文章
