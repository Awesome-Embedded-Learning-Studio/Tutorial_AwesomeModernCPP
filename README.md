# 🚀 Tutorial_AwesomeModernCPP

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B) ![Embedded](https://img.shields.io/badge/Embedded-STM32%20%7C%20Linux-green) ![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub stars](https://img.shields.io/github/stars/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub issues](https://img.shields.io/github/issues/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)

> 一套完整的、系统化的嵌入式现代 C++ 开发教程

<div align="center">

## 🎯 嵌入式现代C++开发教程

**从零开始，系统化学习现代 C++ 在嵌入式系统中的实战应用**

| 13 章 | 120+ 篇文章 | 48 个 CMake 项目 |
|:-----:|:---------:|:-----------:|
| 零开销抽象 | 内存管理 | 智能指针 | 容器 | 并发 | 函数式 | STM32 实战 | 编译链接 |

</div>

---

## 📖 目录

- [关于本教程](#-关于本教程)
- [学习目标](#-学习目标)
- [前置知识](#-前置知识)
- [快速开始](#-快速开始)
- [目录结构](#-目录结构)
- [教程特色](#-教程特色)
- [内容全景图](#-内容全景图)
- [开发工具](#-开发工具)
- [Issue清单](#-issue清单)
- [贡献指南](#-贡献指南)
- [致谢](#-致谢)
- [联系方式](#-联系方式)
- [许可证](#-许可证)

---

## 📖 关于本教程

本项目创建于 2025-12-13，作者 Charliechen。

本项目隶属于组织 [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio) 的文档教程。

这是一套完整的、系统化的嵌入式 C++ 开发教程与知识库，专注于在资源受限的环境中发挥 C++ 的最大优势。本教程不是简单的语法介绍，而是深入探讨**如何在嵌入式系统中高效使用 C++**，包括性能优化、内存管理、硬件交互等核心主题。

目前包含 **9 个独立系列**，涵盖核心嵌入式 C++ 教程、STM32F103C8T6 实战、编译与链接深入指南、现代 C++ 工程实践、模板编程等内容。

点击这里，获取更好的阅读体验👉 [静态网页部署](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

---

## 🎯 学习目标

完成本教程后，您将能够：

1. ✅ 掌握 C++ 在嵌入式系统中的性能优化技术
2. ✅ 理解零开销抽象和编译期编程
3. ✅ 学会使用现代 C++ 特性提升代码质量
4. ✅ 掌握硬件抽象和驱动程序开发
5. ✅ 构建可测试、可维护的嵌入式软件架构
6. ✅ 使用现代 C++ 进行 STM32 裸机开发实战
7. ✅ 深入理解编译、链接与库的设计原理

---

## 📋 前置知识

为了更好地学习本教程，建议您具备以下知识：

- ✔️ 熟悉 C 语言编程
- ✔️ 了解基本的数据结构和算法
- ✔️ 有一定的嵌入式开发经验
- ✔️ 了解基本的电子电路知识

---

## 🚀 快速开始

> **历史版本**: 如果你需要查看 2026 年 4 月重构之前的目录结构（`tutorial/` + `codes_and_assets/`），
> 请切换到 [`archive/legacy_20260415`](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/archive/legacy_20260415) 分支。

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

- **180+ 代码文件** - 所有示例代码独立可编译
- **48 个 CMake 项目** - 开箱即用的构建配置
- **2 个 STM32 工程** - 基于 STM32F103C8T6 的完整项目

代码位于 [`codes_and_assets/examples/`](./codes_and_assets/examples/)，按章节组织：

```
codes_and_assets/
├── examples/              # 核心教程代码示例
│   ├── chapter02/          # 零开销抽象
│   ├── chapter03/          # 内存与对象管理
│   ├── chapter05/          # 内存管理策略
│   ├── chapter06/          # RAII与智能指针
│   ├── chapter07/          # 容器与数据结构
│   ├── chapter08/          # 类型安全
│   ├── chapter09/          # 函数式特性
│   ├── chapter10/          # 并发与原子
│   └── chapter11/          # 现代C++特性
├── stm32f1_tutorials/      # STM32F103C8T6 实战工程
│   ├── 0_start_our_tutorial/
│   └── 1_led_control/
├── templates/              # 可复用的模板
└── project_setup/          # 项目创建脚本
```

### 🔍 智能导航

- 自动上一篇/下一篇导航
- 相关文章推荐
- 前置知识提示
- 预计阅读时间

---

### 内容全景图

| 系列 | 文章数 | 状态 |
|------|--------|------|
| 核心：现代嵌入式C++教程 | 80 | 已完成 |
| 挑战：使用现代C/C++编写STM32F103C8T6 | 18 | 持续更新中 |
| 深入理解C/C++编译特性指南 | 10 | 已完成 |
| 现代C++工程实践 | 3 | 持续更新中 |
| 现代C++特性 | 5 | 持续更新中 |
| 环境配置 | 2 | 持续更新中 |
| 并行计算C++ | 1 | 持续更新中 |
| 调试专题 | 1 | 持续更新中 |
| 现代C++模板教程 | - | 计划中 |

下一步计划：
- 现代C++模板教程（4卷系列：模板基础、现代模板技术、元编程精要、泛型设计模式实战）
- 对于手头没有STM32的朋友，提供 Docker/仿真方案

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

## 📌 Issue清单

| ID | Title | Status | Description |
|----|------|--------|------------|
| #1 | expected 实现问题 | 🟡 推送处理等待反馈 | union + std::string 析构问题 |


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
