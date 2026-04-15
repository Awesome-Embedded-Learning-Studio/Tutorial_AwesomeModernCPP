---
id: "092"
title: "开放贡献指南"
category: community
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["091"]
blocks: []
estimated_effort: medium
---

# 开放贡献指南

## 目标

编写详细的开放贡献指南（CONTRIBUTING.md），作为外部贡献者的完整参考手册。指南需要覆盖所有可能的贡献场景，让新贡献者能够在 15 分钟内完成第一次贡献（从 fork 到提交 PR）。

指南应涵盖：贡献类型说明、写作风格要求、代码风格要求、frontmatter 规范、PR 审核流程、本地开发环境搭建。

## 验收标准

- [ ] `CONTRIBUTING.md` 已创建，内容完整且详细
- [ ] 指南涵盖以下贡献类型及其具体流程：
  - 教程文章撰写
  - 代码示例贡献
  - 翻译贡献
  - 错误修复（文档/代码）
  - 基础设施改进
- [ ] 包含写作风格要求（中文技术写作规范，与 `.claude/writting_style.md` 一致）
- [ ] 包含代码风格要求（命名、格式化、注释规范）
- [ ] 包含 frontmatter 规范（必需字段、标签体系、模板）
- [ ] 包含本地开发环境搭建步骤（mkdocs serve, CMake 编译）
- [ ] 包含完整的 PR 提交流程（fork → branch → commit → push → PR）
- [ ] 包含 PR 审核流程说明（CI 检查 → 维护者审核 → 合并）
- [ ] 包含常见问题解答（FAQ）
- [ ] 文档使用友善、鼓励的语气，而非冰冷的技术文档

## 实施说明

### CONTRIBUTING.md 大纲

```markdown
# 贡献指南

感谢你对 Tutorial_AwesomeModernCPP 项目的关注！
所有形式的贡献都受到欢迎和重视。

## 快速开始

### 第一次贡献？
→ 参见 [Good First Issues](链接) 寻找适合新手的任务

### 5 分钟贡献流程
1. Fork 仓库
2. 创建分支
3. 修改内容
4. 提交 PR

## 贡献类型

### 撰写教程文章
- 选择主题（参考 Issues 中的 content request）
- 使用文章模板
- 遵循写作风格
- 提交 PR

### 贡献代码示例
- 代码放在 code/ 目录
- 包含 CMakeLists.txt
- 包含 README 说明
- 编译测试通过

### 翻译贡献
- 找到需要翻译的文章
- 创建 .en.md 文件
- 遵循翻译规范
- 提交 PR

### 修复错误
- 文档错误：直接修改，提交 PR
- 代码错误：修复并确保编译通过

## 写作风格要求

### 中文技术写作规范
- 使用简体中文
- 技术术语首次出现时中英文对照
- 代码注释使用英文
- 使用"我们"而非"你"（更亲和）
- 段落之间使用空行

### 代码展示规范
- 完整可编译的代码片段
- 关键行添加注释
- 使用行号高亮标注重点
- 先展示使用方式，再展示实现

### 术语使用
- 参见 [术语表](documents/appendix/terminology.md)
- 同一术语全文使用统一译法

## 代码风格要求

### 命名规范
- 文件：snake_case (gpio_input.cpp)
- 类：PascalCase (GpioPin)
- 函数：snake_case (configure_pin)
- 常量：kPascalCase (kMaxRetry)
- 宏：UPPER_SNAKE_CASE (CONFIG_BAUDRATE)

### 格式化
- 使用 .clang-format 配置
- 缩进：4 空格
- 行宽：100 字符

### Include 顺序
1. 对应头文件
2. C++ 标准库
3. 第三方库
4. 项目内头文件

## Frontmatter 规范

每篇文章必须包含以下 frontmatter：

\```yaml
---
title: "文章标题"
date: 2026-04-15
tags:
  - platform-tag      # stm32f1/esp32/rp2040/host
  - topic-tag         # cpp-modern/peripheral/rtos/...
  - difficulty-tag    # beginner/intermediate/advanced
difficulty: beginner
reading_time: 20
---
\```

### 标签体系
（详细列出所有有效标签及说明）

## 本地开发环境

### 前置要求
- Python 3.10+
- MkDocs 及插件
- CMake + Ninja
- arm-none-eabi-gcc（STM32 编译）

### 文档预览
\```bash
pip install -r requirements.txt
mkdocs serve
# 访问 http://localhost:8000
\```

### 代码编译
\```bash
python3 scripts/build_examples.py --host
python3 scripts/build_examples.py --stm32
\```

## PR 提交流程

1. Fork → Clone → Branch
2. 开发修改
3. 本地验证
4. Push → 创建 PR
5. CI 自动检查
6. 等待审核
7. 合并

## PR 审核流程

1. CI 自动检查（编译、质量检查）
2. 维护者审核内容
3. 反馈修改建议
4. 合并

## FAQ

Q: 我不懂 C++/嵌入式，能贡献吗？
A: 当然！我们欢迎翻译、排版修复、拼写检查等贡献。

Q: 我有一个新教程的想法？
A: 请先创建 Issue 讨论，确认方向后再开始写作。

Q: PR 被要求修改怎么办？
A: 这是正常的审核过程，请根据反馈修改后 push 新的 commit。
```

### 语气和措辞原则

- 使用友善、鼓励的语气
- 避免使用"禁止"、"不允许"等强硬措辞，改用"建议"、"推荐"
- 对新手贡献者提供额外指引
- 使用"我们"而不是"你应该"的表达方式
- 在关键步骤提供截图或示例

### 与其他文档的关系

- CONTRIBUTING.md：面向贡献者的流程指南（本文档）
- `.claude/writting_style.md`：AI 辅助写作的风格参考
- `documents/appendix/terminology.md`：术语表
- `.templates/`：文章和代码模板

## 涉及文件

- `CONTRIBUTING.md` — 贡献指南主文件

## 参考资料

- [GitHub CONTRIBUTING 指南](https://docs.github.com/en/communities/setting-up-your-project-for-healthy-contributions/setting-guidelines-for-repository-contributors)
- [Rust 贡献指南](https://rustc-dev-guide.rust-lang.org/contributing.html)（优秀的贡献指南范例）
- [First Timers Only](https://www.firsttimersonly.com/)
