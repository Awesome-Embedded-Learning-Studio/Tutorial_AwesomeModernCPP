# 🚀 Tutorial_AwesomeModernCPP

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B) ![Embedded](https://img.shields.io/badge/Embedded-STM32%20%7C%20Linux-green) ![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub stars](https://img.shields.io/github/stars/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub issues](https://img.shields.io/github/issues/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)

> 一套完整的、系统化的嵌入式现代 C++ 开发教程

<div align="center">

## 🎯 嵌入式现代C++开发教程

**从零开始，系统化学习现代 C++ 在嵌入式系统中的实战应用**

| 12 章 | 70+ 篇文章 | 50+ 示例代码 |
|:-----:|:---------:|:-----------:|
| 零开销抽象 | 内存管理 | 智能指针 | 容器 | 并发 | 函数式 |

</div>

---

## 📖 目录

- [关于本教程](#-关于本教程)
- [学习目标](#-学习目标)
- [前置知识](#-前置知识)
- [快速开始](#-快速开始)
- [目录结构](#-目录结构)
- [教程特色](#-教程特色)
- [学习路径](#-学习路径)
- [贡献指南](#-贡献指南)
- [致谢](#-致谢)
- [联系方式](#-联系方式)
- [许可证](#-许可证)

---

## 📖 关于本教程

本项目创建于 2025-12-13，作者 Charliechen。

本项目隶属于组织 [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio) 的文档教程。

这是一套完整的、系统化的嵌入式 C++ 开发教程，专注于在资源受限的环境中发挥 C++ 的最大优势。本教程不是简单的语法介绍，而是深入探讨**如何在嵌入式系统中高效使用 C++**，包括性能优化、内存管理、硬件交互等核心主题。

点击这里，获取更好的阅读体验👉 [静态网页部署](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

---

## 🎯 学习目标

完成本教程后，您将能够：

1. ✅ 掌握 C++ 在嵌入式系统中的性能优化技术
2. ✅ 理解零开销抽象和编译期编程
3. ✅ 学会使用现代 C++ 特性提升代码质量
4. ✅ 掌握硬件抽象和驱动程序开发
5. ✅ 构建可测试、可维护的嵌入式软件架构

---

## 📋 前置知识

为了更好地学习本教程，建议您具备以下知识：

- ✔️ 熟悉 C 语言编程
- ✔️ 了解基本的数据结构和算法
- ✔️ 有一定的嵌入式开发经验
- ✔️ 了解基本的电子电路知识

---

## 🚀 快速开始

### 本项目包含

- **tutorial/** - 教程 Markdown 文件，包含系统化的学习内容
- **codes_and_assets/** - 示例代码、硬件电路图、PCB 文件等资源

### 如何开始学习

1. 阅读 [tutorial/index.md](./tutorial/index.md) 了解教程结构
2. 按照章节顺序学习，从 Chapter 0 开始
3. 参考示例代码加深理解
4. 完成章节练习巩固知识

---

## ✨ 教程特色

### 🏷️ 标签分类系统

每篇文章都带有标签，方便按主题查找：

- 📚 **概念类**：RAII、移动语义、零开销抽象、编译期计算
- ⚙️ **语言特性**：constexpr、lambda、CRTP、concepts、coroutine
- 🧠 **智能指针**：unique_ptr、shared_ptr、intrusive_ptr
- 📦 **容器**：array、span、循环缓冲区、侵入式容器
- 🔒 **类型安全**：variant、optional、expected、enum class
- ⚡ **函数式**：function、lambda、ranges、invoke
- 🔗 **并发**：atomic、mutex、memory_order、无锁

### 📊 难度分级

- 🟢 **beginner** - 入门级，适合初学者
- 🟡 **intermediate** - 中级，需要一定基础
- 🔴 **advanced** - 高级，深入探讨

### 💻 完整代码示例

- **160+ 代码文件** - 所有示例代码独立可编译
- **44 个 CMake 项目** - 开箱即用的构建配置
- **133 个 Snippets 引用** - 文档与代码同步更新

代码位于 [`codes_and_assets/examples/`](./codes_and_assets/examples/)，按章节组织：

```
codes_and_assets/examples/
├── chapter02/  # 零开销抽象
├── chapter03/  # 内存与对象管理
├── chapter05/  # 内存管理策略
├── chapter06/  # RAII与智能指针
├── chapter07/  # 容器与数据结构
├── chapter08/  # 类型安全
├── chapter09/  # 函数式特性
└── chapter10/  # 并发与原子
```

### 🔍 智能导航

- 自动上一篇/下一篇导航
- 相关文章推荐
- 前置知识提示
- 预计阅读时间

---

### 🔨 计划中

以下内容仍在持续完善中：

下一步，我们将会进行的——
- 📌 上位机现代C++特性体验与代码实战（完善中）
- 📌 使用基于C++的STM32开发与实战指南（开发中），
- 📌 对于手头没有STM32的朋友，提供Docker(我了解到有Renode这个东西，有空试一下)

---

## 🛠️ 开发工具

本项目提供了一套完整的开发工具链，便于贡献者参与：

```bash
# 初始化开发环境
bash scripts/mkdoc_setup_local_dependency.sh

# 本地预览
bash scripts/local_preview.sh

# 安装 pre-commit hooks（可选）
bash scripts/setup_precommit.sh

# 验证文章元数据
python3 scripts/validate_frontmatter.py

# 检查链接有效性
python3 scripts/check_links.py

# 查看统计信息
python3 scripts/analyze_frontmatter.py
```

### 脚本说明

| 脚本 | 功能 |
|------|------|
| `mkdoc_setup_local_dependency.sh` | 安装 MkDocs 依赖 |
| `local_preview.sh` | 启动本地预览服务器 |
| `setup_precommit.sh` | 安装 pre-commit hooks |
| `validate_frontmatter.py` | 验证文章 frontmatter |
| `check_links.py` | 检查内部链接 |
| `analyze_frontmatter.py` | 分析教程统计信息 |

---

## 🤝 贡献指南

我们欢迎任何形式的贡献！

### 如何贡献

1. Fork 本仓库
2. 创建特性分支 (`git switch -c feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

(PS，如果您遇到了问题，欢迎随时联系工作室邮箱！)

### 贡献方式

嘿！我知道光顾仓库的大佬们有点子的，如果您——
- 📝 修正错别字和语法错误
- 💡 提出改进建议
- 🔧 提交代码改进
- 📖 完善文档
- 🐛 报告 Bug

欢迎速速PR！在这里 [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues) 中提交问题。

---

## 🙏 致谢

本项目参考了以下优秀资源：

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial) - 现代C++教程
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings) - C++ 那些事
- [CppCon](https://www.youtube.com/user/CppCon) - C++ 会议演讲
- [C++ Reference](https://en.cppreference.com/) - C++ 在线参考文档

---

## 📮 联系方式

- **GitHub Issues**：[提交问题](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- **Email**：725610365@qq.com

---

## 📜 许可证

本项目采用 [MIT License](./LICENSE) 开源协议。

---

<p align="center">
  <b>让嵌入式开发更现代、更高效、更优雅</b><br>
  用 C++ 重新定义嵌入式编程体验
</p>
