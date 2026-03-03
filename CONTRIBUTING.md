# 贡献指南

感谢你对《现代嵌入式 C++ 教程》的关注！我们欢迎任何形式的贡献，包括但不限于：修正错别字、改进代码示例、完善现有内容、添加新章节等。

## 快速开始

1. Fork 本仓库
2. 创建你的特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m '添加某功能'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建 Pull Request

## 文章规范

### 文章结构

每篇文章应遵循以下结构（详见 `.templates/article-template.md`）：

```markdown
---
# [FRONTMATTER 元数据]
---

# 标题

## 引言
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
| `chapter` | 是 | 所属章节 (0-10) |
| `order` | 是 | 在章节中的顺序 |
| `tags` | 是 | 标签列表 |
| `difficulty` | 否 | 难度：beginner/intermediate/advanced |
| `reading_time_minutes` | 否 | 预计阅读时间（分钟） |
| `prerequisites` | 否 | 前置知识 |
| `related` | 否 | 相关文章 |
| `cpp_standard` | 否 | 涉及的 C++ 标准 |

### 标签规范

使用以下标签分类：

**概念类**：
- `RAII`、`移动语义`、`零开销抽象`、`编译期计算`、`类型安全`

**语言特性**：
- `constexpr`、`lambda`、`CRTP`、`concepts`、`coroutine`

**模式**：
- `对象池`、`状态机`、`工厂模式`、`策略模式`

**容器**：
- `array`、`span`、`vector`、`map`

**智能指针**：
- `unique_ptr`、`shared_ptr`、`weak_ptr`、`intrusive_ptr`

### 写作风格

1. **语言**：使用清晰、简洁的中文
2. **术语**：首次出现的技术术语应附英文原文
3. **代码注释**：使用中文注释
4. **标题层级**：不超过 4 级（`####`）
5. **篇幅**：每篇文章控制在 1500-3000 字

## 代码规范

### C++ 代码风格

1. 使用现代 C++ 风格（C++11 及以上）
2. 优先使用 `auto`、范围 for 循环等现代特性
3. 遵循嵌入式最佳实践：
   - 避免动态内存分配（除非明确说明）
   - 注意代码体积和性能影响
   - 标注适用的 C++ 标准

```cpp
// 好的示例
// Platform: STM32F4
// Standard: C++17

#include <array>

void process_data(const std::array<uint32_t, 10>& data) {
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

1. 复制 `.templates/article-template.md` 作为起点
2. 填写完整的 frontmatter
3. 更新对应章节的 `index.md`，添加新文章链接
4. 确保代码示例可编译

## 文件命名

文章文件名应清晰描述内容：

```
tutorial/核心：现代嵌入式C++教程/Chapter6/
├── 1 RAII在外设管理的作用.md
├── 2 unique_ptr.md
└── 3 shared_ptr.md
```

## 发布前检查清单

提交 PR 前，请确认：

- [ ] Frontmatter 元数据完整
- [ ] 代码示例可编译
- [ ] 无错别字
- [ ] 内部链接有效
- [ ] 标签使用规范
- [ ] 遵循文章模板结构
- [ ] 更新了章节索引（如适用）

## 本地预览

在提交前，建议本地预览文档：

```bash
# 安装依赖
pip install mkdocs-material mkdocs-awesome-pages-plugin mkdocs-git-revision-date-localized-plugin

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
- 提交 Issue
- 查看 [GitHub Discussions](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/discussions)
- 发送邮件至：725610365@qq.com

---

再次感谢你的贡献！
