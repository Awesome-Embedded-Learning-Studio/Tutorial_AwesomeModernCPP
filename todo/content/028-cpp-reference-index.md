---
id: 028
title: "C++ 特性参考系统索引表"
category: content
priority: P2
status: pending
created: 2026-04-15
updated: 2026-04-18
assignee: charliechen
depends_on: ["architecture/002"]
blocks: ["029"]
estimated_effort: large
---

# C++ 特性参考系统索引表

## 目标

创建 C++ 特性索引页，作为参考卡系统（029）的导航中心。覆盖 C++98 到 C++23 每个标准的主要特性，每行一个特性，包含：特性名称、所属标准版本、相关头文件、一句话简述、参考卡链接。索引表支持按标准版本筛选和按功能类别分组。

## 目录结构

参考卡按功能类别组织（非按 C++ 标准版本），索引页需反映此结构：

```
documents/cpp-reference/
  index.md                    # 本索引页
  memory/                     # 内存管理（unique_ptr, shared_ptr, optional, ...）
  containers/                 # 容器与视图（span, string_view, variant, ...）
  concurrency/                # 并发（atomic, jthread, ...）
  core-language/              # 核心语言特性（constexpr, lambda, auto, ...）
  templates/                  # 模板与元编程（concepts, ranges, ...）
```

## 验收标准

- [ ] 索引页包含 C++98/03/11/14/17/20/23 所有主要特性条目
- [ ] 每条目包含：特性名、标准版本、头文件、简述、参考卡链接
- [ ] 按标准版本分组展示，每组有版本概述
- [ ] 按功能类别分组（与 029 目录结构对应）
- [ ] 页面导航清晰，可快速跳转到目标标准/特性
- [ ] 嵌入式适用性标注（高/中/低）
- [ ] 移动端友好的响应式布局

## 涉及文件

- documents/cpp-reference/index.md

## 参考资料

- cppreference.com — C++ 标准参考
- ISO C++ 标准草案 (N4950 for C++23)
- C++ 标准委员会提案 (Papers)
- 《The C++ Programming Language》— Bjarne Stroustrup
- 《Effective Modern C++》— Scott Meyers
- Compiler Support 页面 (en.cppreference.com/w/cpp/compiler_support)
