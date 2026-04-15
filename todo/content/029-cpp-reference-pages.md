---
id: 029
title: "C++ 特性详情页内容"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["028"]
blocks: []
estimated_effort: epic
---

# C++ 特性详情页内容

## 目标
为索引页（028）中的每个 C++ 特性创建独立的详情页。每个页面包含：语法说明、代码示例（含嵌入式适用性标注）、编译器支持情况、与嵌入式开发的关系分析。按标准版本组织到子目录中（cpp98/ 到 cpp23/）。

## 验收标准
- [ ] 每个标准版本有独立的子目录（cpp98, cpp03, cpp11, cpp14, cpp17, cpp20, cpp23）
- [ ] 每个详情页包含统一模板：标题、语法、示例、编译器支持、嵌入式相关性
- [ ] C++11 至少覆盖 20 个核心特性的详情页
- [ ] C++17/20 至少各覆盖 15 个核心特性的详情页
- [ ] 每个示例代码可编译运行（标注编译命令）
- [ ] 嵌入式适用性评级（高/中/低）和详细说明
- [ ] GCC/Clang/MSVC 三大编译器的支持版本标注
- [ ] 页面间交叉引用链接正确

## 实施说明
详情页是 C++ 参考系统的核心内容层。每个页面遵循统一模板，确保一致性。

**页面模板：**

```markdown
# 特性名称 (C++XX)

## 语法
正式语法说明

## 描述
特性的详细描述，设计动机，解决的问题

## 代码示例
### 基础示例
通用代码示例

### 嵌入式应用示例
在嵌入式场景中的使用示例（如适用）

## 编译器支持
| 编译器 | 最低版本 |
|--------|---------|
| GCC    | X.Y     |
| Clang  | X.Y     |
| MSVC   | 19.XX   |

## 嵌入式相关性
评级：高/中/低
分析：在嵌入式开发中的适用性、注意事项、替代方案

## 另见
- 相关特性链接
- 外部参考链接
```

**优先编写的详情页（按重要性排序）：**

1. **C++11 核心**：移动语义与右值引用、智能指针（unique_ptr/shared_ptr/weak_ptr）、lambda 表达式、auto 类型推导、constexpr、variadic templates、nullptr、enum class、range-for、override/final、初始化列表、std::thread/mutex/future、std::atomic、强类型枚举、委托构造、默认/删除函数。

2. **C++14/17 核心**：泛型 lambda、constexpr 放宽、make_unique、结构化绑定、if constexpr、折叠表达式、std::optional、std::variant、string_view、filesystem、内联变量、嵌套命名空间。

3. **C++20 核心**：Concepts、Ranges（基础）、Coroutines（基础）、Modules（概念介绍）、std::format、std::span、std::jthread、三路比较（<=>）。

4. **C++23 特性**：std::expected、std::print、deducing this、std::generator、std::flat_map/flat_set。

**嵌入式相关性分级标准：**
- 高：直接在嵌入式开发中使用，无额外开销或开销可控（如 constexpr、unique_ptr、enum class）
- 中：可用但需注意开销，适合资源充足的场景（如 std::thread、std::string_view）
- 低：通常在裸机嵌入式开发中避免（如 RTTI、异常、std::iostream）

## 涉及文件
- documents/cpp-reference/cpp98/*.md
- documents/cpp-reference/cpp03/*.md
- documents/cpp-reference/cpp11/*.md
- documents/cpp-reference/cpp14/*.md
- documents/cpp-reference/cpp17/*.md
- documents/cpp-reference/cpp20/*.md
- documents/cpp-reference/cpp23/*.md

## 参考资料
- cppreference.com — 各特性详情页
- ISO C++ 标准草案
- 《Effective Modern C++》— Scott Meyers
- 《C++ Templates: The Complete Guide》— Vandevoorde et al.
- Compiler Support 页面 (cppreference)
- Godbolt Compiler Explorer (godbolt.org) — 编译器行为验证
