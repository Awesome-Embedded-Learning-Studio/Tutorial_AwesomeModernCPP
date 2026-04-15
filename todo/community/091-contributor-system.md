---
id: "091"
title: "贡献者体系建设"
category: community
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: medium
---

# 贡献者体系建设

## 目标

建立完善的贡献者体系，降低外部贡献者的参与门槛，建立自动化的贡献管理流程。遵循"宽松贡献 + CI 自动验证"的策略，让贡献过程尽可能顺畅。

具体包括：
1. **CONTRIBUTING.md**：完善的贡献指南
2. **PR/Issue 模板**：标准化的提交模板
3. **自动标签系统**：基于文件路径和内容的自动标签
4. **贡献者列表**：使用 all-contributors bot 自动维护贡献者列表
5. **里程碑管理**：建立清晰的版本里程碑

## 验收标准

- [ ] `CONTRIBUTING.md` 已创建，内容涵盖所有贡献类型和流程
- [ ] `.github/ISSUE_TEMPLATE/` 下创建至少 3 个 Issue 模板：
  - Bug 报告
  - 内容建议/新教程请求
  - 问题/疑问
- [ ] `.github/PULL_REQUEST_TEMPLATE.md` 已创建，包含清单和描述指引
- [ ] `.github/labeler.yml` 配置自动标签规则（基于修改文件路径自动打标签）
- [ ] `labeler` GitHub Action 已配置，PR 自动打标签
- [ ] all-contributors bot 已配置，`README.md` 中显示贡献者列表
- [ ] 至少定义 2 个 GitHub Milestone（如 v1.0、v1.1）
- [ ] `.github/CODE_OF_CONDUCT.md` 已创建（采用 Contributor Covenant）

## 实施说明

### Issue 模板设计

**Bug 报告模板** (`.github/ISSUE_TEMPLATE/bug_report.yml`)：

```yaml
name: Bug 报告
description: 报告文档错误或代码问题
labels: ["bug", "triage"]
body:
  - type: dropdown
    id: type
    attributes:
      label: 问题类型
      options:
        - 文档内容错误
        - 代码编译错误
        - 链接失效
        - 排版/格式问题
        - 其他
  - type: textarea
    id: description
    attributes:
      label: 问题描述
    validations:
      required: true
  - type: input
    id: location
    attributes:
      label: 问题位置
      description: 文件路径或 URL
      placeholder: "documents/stm32/gpio.md 第 42 行"
  - type: textarea
    id: suggestion
    attributes:
      label: 建议修正
```

**内容建议模板** (`.github/ISSUE_TEMPLATE/content_request.yml`)：

```yaml
name: 内容建议
description: 建议新的教程主题或内容改进
labels: ["enhancement", "content"]
body:
  - type: dropdown
    id: category
    attributes:
      label: 内容类别
      options:
        - 新教程主题
        - 现有教程补充
        - 代码示例
        - 练习题
        - 其他
  - type: textarea
    id: suggestion
    attributes:
      label: 建议内容
      description: 详细描述你希望看到的内容
    validations:
      required: true
```

### PR 模板设计

```markdown
<!-- .github/PULL_REQUEST_TEMPLATE.md -->
## 变更描述

<!-- 简要描述此 PR 的变更内容 -->

## 变更类型

- [ ] 新教程文章
- [ ] 内容修正/改进
- [ ] 代码示例更新
- [ ] 基础设施（CI/工具）
- [ ] 翻译
- [ ] 其他

## 检查清单

- [ ] 已阅读 CONTRIBUTING.md
- [ ] 文章 frontmatter 格式正确（title, date, tags）
- [ ] 代码已通过本地编译测试
- [ ] 链接和图片引用正确
- [ ] 已在本地预览（mkdocs serve）确认渲染正常

## 相关 Issue

<!-- 关联的 Issue 编号，如 Closes #123 -->

## 截图/预览

<!-- 如有视觉变更，请附截图 -->
```

### 自动标签系统

```yaml
# .github/labeler.yml
# 基于修改文件路径自动打标签

"topic: content":
  - changed-files:
    - any-glob-to-any-file: "documents/**"

"topic: code":
  - changed-files:
    - any-glob-to-any-file: "code/**"

"topic: ci":
  - changed-files:
    - any-glob-to-any-file: ".github/**"

"topic: mkdocs":
  - changed-files:
    - any-glob-to-any-file: "mkdocs.yml"

"topic: translation":
  - changed-files:
    - any-glob-to-any-file: "**/*.en.md"

"platform: stm32":
  - changed-files:
    - any-glob-to-any-file:
      - "documents/**/stm32*/**"
      - "code/**/stm32*/**"

"platform: esp32":
  - changed-files:
    - any-glob-to-any-file:
      - "documents/**/esp32/**"
      - "code/**/esp32/**"
```

```yaml
# .github/workflows/labeler.yml
name: Auto Label
on:
  pull_request:
    types: [opened, synchronize]

jobs:
  label:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/labeler@v5
        with:
          configuration-path: .github/labeler.yml
```

### all-contributors 配置

```json
// .all-contributorsrc
{
  "projectName": "Tutorial_AwesomeModernCPP",
  "projectOwner": "Charliechen114514",
  "repoType": "github",
  "contributors": [
    {
      "login": "Charliechen114514",
      "name": "Charlie Chen",
      "avatar_url": "...",
      "contributions": ["code", "doc", "maintenance", "content"]
    }
  ],
  "contributorsPerLine": 7,
  "contributions": ["code", "doc", "content", "translation", "review", "ideas", "maintenance"]
}
```

自定义贡献类型（在默认基础上添加）：
- `content`: 教程内容撰写
- `translation`: 翻译贡献
- `review`: 代码/内容审核

### 里程碑规划

| 里程碑 | 目标 | 预计时间 |
|--------|------|----------|
| v1.0 | 基础架构 + STM32F1 核心教程 + 文档站点 | Q2 2026 |
| v1.1 | RTOS 教程 + ESP32 入门 | Q3 2026 |
| v1.2 | 双语支持 + RP2040 | Q4 2026 |

## 涉及文件

- `.github/ISSUE_TEMPLATE/bug_report.yml` — Bug 报告模板
- `.github/ISSUE_TEMPLATE/content_request.yml` — 内容建议模板
- `.github/ISSUE_TEMPLATE/question.yml` — 问题模板
- `.github/PULL_REQUEST_TEMPLATE.md` — PR 模板
- `.github/labeler.yml` — 自动标签配置
- `.github/workflows/labeler.yml` — 标签工作流
- `.github/CODE_OF_CONDUCT.md` — 行为准则
- `CONTRIBUTING.md` — 贡献指南
- `.all-contributorsrc` — all-contributors 配置

## 参考资料

- [GitHub Issue 模板语法](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/syntax-for-issue-forms)
- [actions/labeler](https://github.com/actions/labeler)
- [all-contributors bot](https://allcontributors.org/)
- [Contributor Covenant](https://www.contributor-covenant.org/)
