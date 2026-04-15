---
id: "090"
title: "配置 GitHub Discussions 社区论坛"
category: community
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: small
---

# 配置 GitHub Discussions 社区论坛

## 目标

启用和配置 GitHub Discussions 功能，作为项目社区的核心交流平台。Discussions 为用户提供了一个比 Issues 更适合讨论、提问和分享的场所，有助于建立活跃的学习社区。

## 验收标准

- [ ] GitHub Discussions 已在仓库设置中启用
- [ ] 创建以下讨论分类（Category）：
  - **Q&A**：问答区（默认分类，用户提问和解答）
  - **Ideas**：想法和建议（新教程主题、功能建议）
  - **Show and tell**：展示分享（用户项目展示、学习笔记）
  - **General**：通用讨论（技术交流、行业动态）
- [ ] 每个分类有清晰的描述和图标
- [ ] 创建讨论模板，引导用户正确发帖
- [ ] 发布欢迎帖（Welcome / 欢迎），包含社区规则和资源链接
- [ ] README 中添加 Discussions 链接
- [ ] Q&A 分类配置为需要标记最佳答案

## 实施说明

### 启用 Discussions

在仓库 Settings → General → Features 中勾选 Discussions。

### 分类配置

| 分类 | 图标 | 描述 |
|------|------|------|
| Q&A | 💬 | 提问和解答，遇到问题请在这里发帖 |
| Ideas | 💡 | 新教程主题建议、功能改进想法 |
| Show and tell | 🎯 | 分享你的项目、学习心得和作品 |
| General | 🗨️ | 技术交流、嵌入式行业动态、闲聊 |

### 讨论模板

创建 `.github/DISCUSSION_TEMPLATE/` 目录，为每个分类创建 YAML 模板：

```yaml
# .github/DISCUSSION_TEMPLATE/q-a.yml
title: "[Q&A] "
labels: []
body:
  - type: dropdown
    id: platform
    attributes:
      label: 相关平台
      options:
        - STM32F1
        - STM32F4
        - ESP32
        - RP2040
        - Host (通用)
        - 不适用
    validations:
      required: true
  - type: textarea
    id: question
    attributes:
      label: 问题描述
      description: 请详细描述你遇到的问题
      placeholder: 我在阅读 ... 教程时遇到了 ...
    validations:
      required: true
  - type: textarea
    id: context
    attributes:
      label: 相关代码/日志
      description: 如果有相关代码或错误日志，请粘贴
      render: shell
```

```yaml
# .github/DISCUSSION_TEMPLATE/ideas.yml
title: "[Idea] "
body:
  - type: textarea
    id: idea
    attributes:
      label: 想法描述
      description: 描述你的想法或建议
    validations:
      required: true
  - type: dropdown
    id: category
    attributes:
      label: 想法类型
      options:
        - 新教程主题
        - 现有内容改进
        - 工具/基础设施
        - 其他
```

### 欢迎帖内容

```markdown
# 欢迎！/ Welcome!

欢迎来到 Tutorial_AwesomeModernCPP 社区讨论区！

## 社区准则

- 友善和尊重：请对所有社区成员保持友善和尊重
- 清晰提问：提问时请提供足够的上下文（平台、代码、错误信息）
- 分享知识：如果你解决了问题，请分享你的解决方案
- 标记答案：在 Q&A 区提问后，请标记帮助你的最佳答案

## 快速链接

- 📖 文档站点：[链接]
- 🐛 问题反馈：[Issues](链接)
- 💻 代码仓库：[GitHub](链接)
- 🤝 贡献指南：[CONTRIBUTING.md](链接)

## 推荐学习路径

如果你是新手，建议从这里开始：
1. 阅读 [C++ 基础教程]
2. 选择你的目标平台（STM32/ESP32/RP2040）
3. 按顺序学习平台教程
4. 进入 RTOS 篇章

祝学习愉快！
```

### 自动化增强（可选）

- 配置 GitHub Actions 在新 Discussion 时自动欢迎
- 使用 `actions/discussion-notification` 通知维护者
- 定期将高质量 Q&A 总结到 FAQ 文档

## 涉及文件

- `.github/DISCUSSION_TEMPLATE/q-a.yml` — Q&A 模板
- `.github/DISCUSSION_TEMPLATE/ideas.yml` — Ideas 模板
- `.github/DISCUSSION_TEMPLATE/show-and-tell.yml` — Show and tell 模板
- `.github/DISCUSSION_TEMPLATE/general.yml` — General 模板
- GitHub 仓库设置（Discussions 启用 + 欢迎帖）

## 参考资料

- [GitHub Discussions 文档](https://docs.github.com/en/discussions)
- [Discussion 模板语法](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/configuring-issue-templates-for-your-repository)
- [GitHub 社区最佳实践](https://docs.github.com/en/communities)
