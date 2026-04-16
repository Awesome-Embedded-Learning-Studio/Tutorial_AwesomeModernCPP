# 贡献指南

感谢你对现代 C++ 教程的关注！我们欢迎任何形式的贡献，包括但不限于：修正错别字、改进代码示例、完善现有内容、添加新章节等。

## 快速开始

1. Fork 本仓库
2. 创建你的特性分支 (`git switch -c feature/amazing-feature`)
3. 提交更改 (`git commit -m '添加某功能'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建 Pull Request

## 文章规范

### 文章结构

每篇文章应遵循以下结构：

```markdown
---
# [FRONTMATTER 元数据]
---

# 标题

## 引言 / 动机
## 核心概念
## 代码示例
## 实战应用
## 注意事项
## 小结
## 练习（可选）
## 参考资源
```

### Frontmatter 元数据

每篇文章必须包含以下元数据：

| 字段 | 必填 | 说明 |
|------|------|------|
| `title` | 是 | 文章标题 |
| `description` | 是 | 一句话描述文章内容 |
| `chapter` | 是 | 所属章节 |
| `order` | 是 | 在章节中的顺序 |
| `tags` | 是 | 标签列表 |
| `difficulty` | 否 | 难度：beginner / intermediate / advanced |
| `reading_time_minutes` | 否 | 预计阅读时间（分钟） |
| `prerequisites` | 否 | 前置知识 |
| `related` | 否 | 相关文章 |
| `cpp_standard` | 否 | 涉及的 C++ 标准（如 [11, 14, 17, 20]） |

### 目录组织

文章按卷-章组织，放入对应卷目录：

```
documents/vol2-modern-features/     # 卷二目录
├── index.md                        # 卷首页
├── ch01-smart-pointers/            # 章节（可选子目录）
│   ├── 01-raii-deep-dive.md
│   ├── 02-unique-ptr.md
│   └── 03-shared-ptr.md
└── cpp17-string-view.md            # 也可直接放在卷目录下
```

### 写作风格

1. **语言**：使用清晰、简洁的中文
2. **术语**：首次出现的技术术语应附英文原文
3. **代码注释**：使用中文注释
4. **标题层级**：不超过 4 级（`####`）
5. **篇幅**：每篇文章控制在 1500-3000 字

详细写作风格请参考 `.claude/writting_style.md`。

## 代码规范

### C++ 代码风格

1. 使用现代 C++ 风格（C++11 及以上）
2. 优先使用 `auto`、范围 for 循环等现代特性
3. 标注适用的 C++ 标准
4. 代码示例使用 CMake 构建，确保可独立编译

```cpp
// Standard: C++17

#include <array>
#include <span>

void process_data(std::span<const uint32_t> data) {
    for (const auto& value : data) {
        // 处理数据
    }
}
```

### 代码格式

- 使用 4 空格缩进
- 大括号另起一行（Allman 风格）
- 函数名使用 snake_case
- 类名使用 PascalCase

## 添加新文章

1. 确定文章所属的卷和章节，放入对应目录
2. 填写完整的 frontmatter
3. 更新对应卷的 `index.md`，添加新文章链接
4. 确保代码示例可编译
5. 本地预览确认渲染正确

## 发布前检查清单

提交 PR 前，请确认：

- [ ] Frontmatter 元数据完整
- [ ] 代码示例可编译
- [ ] 无错别字
- [ ] 内部链接有效
- [ ] 标签使用规范
- [ ] 遵循文章模板结构
- [ ] 更新了卷首页索引（如适用）

## 本地预览

在提交前，建议本地预览文档：

```bash
# 安装依赖
pip install -r scripts/requirements.txt

# 启动本地服务器
mkdocs serve

# 访问 http://127.0.0.1:8000
```

## 代码审查流程

1. 所有 PR 需要至少一位维护者审核
2. CI 检查必须通过（markdown lint、链接检查）
3. 审核通过后，维护者将合并代码

## 行为准则

- 尊重所有贡献者
- 建设性的反馈和讨论
- 专注于对项目最有利的事情

## 获取帮助

如有问题，请：

- 提交 [GitHub Issue](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- 发送邮件至：725610365@qq.com

---

再次感谢你的贡献！
