---
id: "010"
title: "卷一：基础入门 TODO"
category: content
priority: P2
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# 卷一：基础入门 TODO

## Current Assets

### 文档

- `documents/vol1-fundamentals/`：103 篇中文文章，ch00-ch12 完整。
- `documents/en/vol1-fundamentals/`：103 篇英文翻译，1:1 覆盖（v0.4.0 完成）。
- 内容覆盖：C 语言速通（16 篇基础 + 8 篇进阶）、C++98 系列（6 篇）、类型系统、控制流、函数、指针与引用、数组与字符串、类与 OOP、运算符重载、继承与多态、模板初步、异常处理、STL 初见、内存模型基础。
- 根目录补充文章 9 篇：preface、C 语言速通、C++98 系列 03A-03F、语言选型 2 篇。
- v0.4.0 为 vol1 多章文章批量添加了 Compiler Explorer 交互链接。

### 代码

- `code/volumn_codes/vol1/ch00-ch12/`：13 个章节目录，每目录含 CMakeLists.txt + 多个 .cpp（约 100 个源文件）。
- `code/volumn_codes/vol1/exercise/`：13 个子目录（ch00-ch12），共约 88 个练习文件，含顶层 CMakeLists.txt。
- `code/volumn_codes/vol1/c_tutorials/`：16 个子目录对应基础篇，每目录含 CMakeLists.txt + 源码。

### 导航

- `index.md` 使用 `<ChapterNav>` / `<ChapterLink>` Vue 组件，ch00-ch12 + 补充材料完整列出。
- sidebar 由 `site/.vitepress/config/sidebar.ts` 自动扫描生成。

### 状态

- `README.md` 标记卷一为"已完成"。
- 旧 `code/examples/vol1/`（16 个 .cpp）为重构前遗留，已被 `code/volumn_codes/vol1/` 完整覆盖。

## Gaps

### 跨卷链接缺失

卷一作为全项目入口，以下章节缺乏向后续卷的前向引导：

| 卷一章 | 应导向 | 当前状态 |
|--------|--------|---------|
| ch04/04 智能指针预告 | → 卷二（RAII、移动语义、智能指针主线） | 无链接 |
| ch09 模板初步 | → 卷四（模板主线、CRTP、concepts） | 无链接 |
| ch10 异常处理 | → 卷二（错误处理）/ 卷四（异常 + noexcept 演进） | 无链接 |
| ch11 STL 初见 | → 卷三（标准库深入） | 无链接 |
| ch12 内存模型 | → 卷六（性能）、卷七（链接脚本）、卷八（嵌入式内存） | 无链接 |
| c_tutorials/07 嵌入式 C 模式 | → 卷八（嵌入式开发主线） | 无链接 |
| c_tutorials/01 ARM 架构 | → 卷八（STM32 平台） | 无链接 |

### 教学 TODO 标记噪音

c_tutorials 下约 55 处 `// TODO:` 标记（分布在 13、14、15、16 基础篇和 advanced_feature 01、03、05 中），全部为教学练习型（如 `// TODO: 自行实现`），但会被全仓 TODO 搜索误判为项目规划任务。

### advanced_feature 代码缺失

c_tutorials 基础篇 16 篇均有配套 CMake 代码目录，但进阶专题 8 篇无配套代码。使用 Compiler Explorer 交互链接弥补（与 v0.4.0 已有模式一致）。

### exercise 可访问性

88 个练习代码存在但章节 index 中未引导。读者可能不知道有配套练习。

### 遗留代码清理

`code/examples/vol1/`（16 个无 CMakeLists.txt 的 .cpp）为重构前遗留，已被 `code/volumn_codes/vol1/` 完整覆盖，应删除。

## Worth Continuing

值得维护，不建议大规模扩写。卷一已经是稳定资产，后续目标是降低读者摩擦，而不是继续堆章节。

## Concrete Improvements

### 近期维护（低成本高收益）

#### N-1: 添加跨卷前向链接

- 在 ch04/04、ch09、ch10、ch11、ch12 五处章节末尾添加"继续阅读"段落，链接到对应后续卷。
- 链接措辞（"继续阅读"或"延伸阅读"）留给执行时根据文章上下文决定。
- c_tutorials/07、c_tutorials/01 也应添加到卷八的引导。
- 影响范围：5-7 篇文章末尾，约 20-30 行。

#### N-2: 重命名教学 TODO 标记

- 将 c_tutorials 下 55 处 `// TODO:` 统一改为 `// 练习：`。
- 影响范围：7 篇文章。

#### N-3: 删除遗留代码

- 删除 `code/examples/vol1/` 目录（16 个 .cpp）。
- 确认无文章或脚本引用该路径后执行。

### 中期补充（增量建设）

#### M-1: exercise 引导接入

- 在各章节 index.md 末尾添加到 exercise 代码的引导。
- 格式：GitHub 链接为主（在线读者），附带本地路径（本地读者）。
- 示例：`💡 本章配套练习见 [exercise/ch04](https://github.com/.../tree/main/code/volumn_codes/vol1/exercise/ch04)`
- 影响范围：13 个章节 index.md。

#### M-2: advanced_feature Compiler Explorer 链接

- 为 c_tutorials/advanced_feature 8 篇进阶专题中的关键代码块添加 Compiler Explorer 交互链接。
- 优先处理 host 可编译的代码（03-C 陷阱、04-OOP-in-C、05-动态数组、06-链表、08-可复用代码）。
- ARM/嵌入式相关（01、02、07）使用 Compiler Explorer 的 ARM 编译器视图。
- 与 v0.4.0 已有的 Compiler Explorer 集成模式保持一致。

#### M-3: 卷末常见错误附录

- 在卷一目录下新增 `appendix-common-pitfalls.md`，作为独立附录。
- 内容：按主题（指针、生命周期、数组退化、引用绑定、异常安全、内存泄漏）组织常见错误模式，每个模式 1-2 句描述 + 链接到卷内对应章节。
- 在 `index.md` 补充材料区添加附录入口链接。
- 不修改现有已稳定正文。

### 远期候选

#### L-1: "学完卷一之后"过渡指引

- 在 index.md 底部添加路径化引导文字。
- 依赖 N-1（跨卷链接）全部到位后再做。

## Old TODO Merge

- 旧 `022-core-theory-expansion.md` 中仍适合基础维护的残余项已由卷级 TODO 吸收，不保留独立任务。
- 其余核心理论扩展已拆到卷三、卷四、卷六、卷七、卷八的 TODO。

## Development Points

- 不再为卷一新增大主题。
- 附录型内容（常见错误索引）不算大主题，可以按需添加。
- 跨卷链接的措辞由执行时的 AI 根据文章上下文决定。

## Acceptance

- [ ] N-1: 关键跨卷前向链接完成（ch04→卷二、ch09→卷四、ch10→卷二/四、ch11→卷三、ch12→卷六/七/八）
- [ ] N-2: 教学 TODO 标记全部重命名为 `// 练习：`
- [ ] N-3: 遗留 `code/examples/vol1/` 已删除
- [ ] M-1: 各章节 index.md 已添加 exercise 引导
- [ ] M-2: advanced_feature 文章已添加 Compiler Explorer 链接
- [ ] M-3: 常见错误附录已创建并接入 index.md
- [ ] 卷一仍保持 stable 状态，不引入新的大规划文件
