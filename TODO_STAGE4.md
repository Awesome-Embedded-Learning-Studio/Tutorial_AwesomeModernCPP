# 阶段 4：内容迁移 - 添加 Frontmatter

## 目标

为现有 86 篇文章添加 YAML frontmatter 元数据，实现标签分类、难度评级、关联推荐等功能。

---

## Frontmatter 模板

```yaml
---
title: "文章标题"
description: "一句话描述"
chapter: X
order: Y
tags:
  - 标签1
  - 标签2
difficulty: beginner|intermediate|advanced
reading_time_minutes: 10
prerequisites:
  - "前置章节"
related:
  - "相关文章"
cpp_standard: [11, 14, 17]
---
```

---

## 任务分配（按章节）

### Task 1: Chapter 0 - 前言与基础（6篇）

| 文件 | 标题建议 | 标签 |
|------|----------|------|
| 0前言.md | 前言 | 基础,入门 |
| 1嵌入式的资源与实时约束.md | 嵌入式的资源与实时约束 | 嵌入式,实时性,资源限制 |
| 2急速C语言速通复习.md | C语言速通复习 | C语言,基础语法 |
| 3快速过一下C++98的基本特性.md | C++98基本特性 | C++98,基础 |
| 4何时用C++、用哪些C++特性.md | 何时用C++ | 语言选择,权衡 |
| 5语言选择原则.md | 语言选择原则 | 性能,可维护性 |
| 6学习如何评估程序的性能和体积开销.md | 性能与体积评估 | 性能优化,体积 |

---

### Task 2: Chapter 1 - 构建工具链（3篇）

| 文件 | 标签 |
|------|------|
| 1随意聊下交叉编译和CMake简单指南.md | 交叉编译,CMake,工具链 |
| 2常见编译器选项指南.md | 编译器选项,优化 |
| 3链接器与链接器脚本.md | 链接器,链接脚本 |

---

### Task 3: Chapter 2 - 零开销抽象（4篇）

| 文件 | 标签 |
|------|------|
| 1零开销抽象.md | 零开销抽象,编译器优化 |
| 2内联与编译器优化.md | 内联,优化 |
| 3constexpr.md | constexpr,编译期 |
| 4 CRTP VS 运行时多态.md | CRTP,多态,模板 |

---

### Task 4: Chapter 3 - 内存与对象管理（5篇）

| 文件 | 标签 |
|------|------|
| 1 初始化列表.md | 初始化列表 |
| 2 移动语义.md | 移动语义,右值引用 |
| 3 RVO, NRVO.md | RVO,NRVO,返回值优化 |
| 4 空基类优化（EBO）.md | EBO,空基类 |
| 5对象大小，平凡类型.md | 对象大小,平凡类型 |

---

### Task 5: Chapter 5 - 内存管理策略（6篇）

| 文件 | 标签 |
|------|------|
| 1动态分配问题.md | 动态分配,堆 |
| 2静态存储与栈上分配策略.md | 栈,静态存储 |
| 3对象池模式.md | 对象池,模式 |
| 4禁用heap...放置new.md | placement-new |
| 5固定池分配.md | 固定池,分配器 |
| 6array vs 一般数组.md | array,数组 |

---

### Task 6: Chapter 6 - RAII与智能指针（8篇）

| 文件 | 标签 |
|------|------|
| 1 RAII在外设管理的作用.md | RAII,外设管理 |
| 2 unique_ptr.md | unique_ptr,智能指针 |
| 3 shared_ptr.md | shared_ptr,智能指针 |
| 4 unique_ptr、shared_ptr的嵌入式取舍.md | 智能指针,权衡 |
| 5 intrusive智能指针.md | intrusive,智能指针 |
| 6 自定义Deleter.md | deleter,智能指针 |
| 7 引用计数.md | 引用计数 |
| 8 Scope Guard.md | scope_guard,RAII |

---

### Task 7: Chapter 7 - 容器与数据结构（6篇）

| 文件 | 标签 |
|------|------|
| 1 array.md | array,容器 |
| 2 span.md | span,视图 |
| 3 循环缓冲区.md | 循环缓冲区 |
| 4 侵入式容器设计.md | 侵入式容器 |
| 5 ETL.md | ETL,容器库 |
| 6 自定义的分配器.md | 分配器 |

---

### Task 8: Chapter 8 - 类型安全（7篇）

| 文件 | 标签 |
|------|------|
| 1 enum class.md | enum,枚举 |
| 2 类型安全的寄存器访问.md | 寄存器,类型安全 |
| 3 variant.md | variant,类型安全 |
| 4 optional.md | optional,类型安全 |
| 5 expected.md | expected,错误处理 |
| 6 类型别名与using声明.md | using,类型别名 |
| 7 字面量运算符与自定义单位.md | 字面量运算符 |

---

### Task 9: Chapter 9 - 函数式特性（8篇）

| 文件 | 标签 |
|------|------|
| 1 Lambda表达式基础.md | lambda,函数式 |
| 2 Lambda捕获与性能影响.md | lambda,捕获,性能 |
| 3 std function vs 函数指针.md | function,函数指针 |
| 4 回调机制的零开销实现.md | 回调,零开销 |
| 5 std invoke与可调用对象.md | invoke,可调用对象 |
| 6 函数式错误处理模式.md | 函数式,错误处理 |
| 7 C++20范围库基础与视图.md | ranges,视图 |
| 8 管道操作与Ranges实战.md | ranges,管道 |

---

### Task 10: Chapter 10 - 并发与原子（4篇）

| 文件 | 标签 |
|------|------|
| 1 atomic.md | atomic,原子操作 |
| 2 memory_order.md | memory_order,内存序 |
| 3 无锁数据结构设计.md | 无锁,数据结构 |
| 4 mutex与RAII守卫.md | mutex,锁,RAII |

---

### Task 11: 其他模块（~35篇）

- 现代C++特性/
- 现代C++工程实践/
- 深入理解CC++编译特性指南/
- 环境配置/
- 调试专题/
- 并行计算C++/

---

## 操作步骤

### 1. 选择任务

认领一个 Task（如 Task 5: Chapter 6）

### 2. 添加 Frontmatter

在文章开头添加 YAML frontmatter：

```markdown
---
title: "RAII 在外设管理中的作用"
description: "介绍 RAII 模式在嵌入式外设管理中的应用"
chapter: 6
order: 1
tags:
  - RAII
  - 外设管理
  - 资源管理
difficulty: intermediate
reading_time_minutes: 12
prerequisites:
  - "Chapter 5: 内存管理策略"
related:
  - "Scope Guard"
  - "unique_ptr"
cpp_standard: [11, 14, 17]
---

# 嵌入式C++开发——RAII 在驱动 / 外设管理中的应用
...
```

### 3. 验证

```bash
python3 scripts/validate_frontmatter.py
```

### 4. 提交

```bash
git add .
git commit -m "stage4: 添加 Chapter X frontmatter"
git push
```

---

## 标签分类参考

| 类别 | 标签 |
|------|------|
| 概念 | RAII, 移动语义, 零开销抽象, 编译期计算 |
| 语言特性 | constexpr, lambda, CRTP, concepts, coroutine |
| 智能指针 | unique_ptr, shared_ptr, weak_ptr, intrusive_ptr |
| 容器 | array, span, vector, map, 循环缓冲区 |
| 类型安全 | enum, variant, optional, expected |
| 函数式 | function, lambda, ranges, invoke |
| 并发 | atomic, mutex, memory_order, 无锁 |
| 工具链 | CMake, 链接器, 编译器选项 |

---

## 进度追踪

| Task | 章节 | 负责人 | 状态 |
|------|------|--------|------|
| 1 | Chapter 0 (6篇) | | ⏸️ 待分配 |
| 2 | Chapter 1 (3篇) | | ⏸️ 待分配 |
| 3 | Chapter 2 (4篇) | | ⏸️ 待分配 |
| 4 | Chapter 3 (5篇) | | ⏸️ 待分配 |
| 5 | Chapter 5 (6篇) | | ⏸️ 待分配 |
| 6 | Chapter 6 (8篇) | | ⏸️ 待分配 |
| 7 | Chapter 7 (6篇) | | ⏸️ 待分配 |
| 8 | Chapter 8 (7篇) | | ⏸️ 待分配 |
| 9 | Chapter 9 (8篇) | | ⏸️ 待分配 |
| 10 | Chapter 10 (4篇) | | ⏸️ 待分配 |
| 11 | 其他模块 (~35篇) | | ⏸️ 待分配 |

---

## 建议分工

- **10 人协作**：每人负责 1 个 Task
- **5 人协作**：每人负责 2 个 Task
- **预计时间**：每人 1-2 小时

---

## 完成标准

- [ ] 所有文章包含 frontmatter
- [ ] `validate_frontmatter.py` 无错误
- [ ] 标签使用规范分类
- [ ] 难度评级合理
