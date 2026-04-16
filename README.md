# Tutorial_AwesomeModernCPP

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B) ![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub stars](https://img.shields.io/github/stars/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP) ![GitHub issues](https://img.shields.io/github/issues/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)

> 一套完整的、系统化的现代 C++ 开发教程

**从零开始，系统化学习现代 C++ — 从基础语法到嵌入式实战，从标准库深入到并发编程**

---

## 教程结构

本教程共 **8 卷 + 编译深入 + 实战项目**，覆盖 C++ 学习的完整路径：

| 卷 | 主题 | 文章数 | 难度 | 状态 |
|:--:|------|:------:|:----:|:----:|
| 一 | [C++ 基础入门](documents/vol1-fundamentals/) | 50-60 | beginner | 编写中 |
| 二 | [现代 C++ 特性](documents/vol2-modern-features/) | 35-40 | intermediate | 编写中 |
| 三 | [标准库深入](documents/vol3-standard-library/) | 40-50 | intermediate | 规划中 |
| 四 | [高级主题](documents/vol4-advanced/) | 50-60 | advanced | 规划中 |
| 五 | [并发编程](documents/vol5-concurrency/) | 25-30 | advanced | 规划中 |
| 六 | [性能优化](documents/vol6-performance/) | 18-22 | advanced | 规划中 |
| 七 | [软件工程实践](documents/vol7-engineering/) | 30-35 | intermediate | 规划中 |
| 八 | [领域应用](documents/vol8-domains/) | 80-100 | intermediate | 编写中 |
| - | [编译与链接深入](documents/compilation/) | 10+ | intermediate | 已完成 |
| - | [贯穿式实战项目](documents/projects/) | - | advanced | 规划中 |

卷八包含五个子领域：嵌入式开发、网络编程、GUI 与图形、数据存储、算法与数据结构。

---

## 关于本教程

本项目创建于 2025-12-13，作者 Charliechen，隶属于 [Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)。

本教程不是简单的语法介绍，而是系统化的 C++ 学习路径 — 从零基础入门到高级主题，从标准库深入到各领域的实战应用。每篇文章都注重"为什么"而非仅仅是"怎么做"，强调理解底层机制和设计理念。

点击这里，获取更好的阅读体验：[静态网页部署](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

---

## 学习路径

```
零基础
  │
  ▼
卷一：C++ 基础入门 ───────── 类型、控制流、函数、指针、类、模板初步、STL 初见
  │
  ▼
卷二：现代 C++ 特性 ──────── 移动语义、智能指针、constexpr、Lambda、类型安全
  │                          (P0 核心卷：区分"现代 C++"和"旧 C++")
  │
  ├─▶ 卷三：标准库深入 ──── 容器、迭代器、算法、字符串、分配器、STL 源码分析
  ├─▶ 卷四：高级主题 ────── Concepts、Ranges、协程、模块、模板元编程、C++23/26
  ├─▶ 卷五：并发编程 ────── 线程原语、原子操作、无锁编程、协程异步 I/O
  ├─▶ 卷六：性能优化 ────── CPU 缓存、SIMD、汇编阅读、基准测试
  ├─▶ 卷七：软件工程 ────── CMake、测试、静态分析、DevOps
  └─▶ 卷八：领域应用 ────── 嵌入式 / 网络 / GUI / 数据存储 / 算法
```

---

## 快速开始

> **历史版本**：如需查看 2026 年 4 月重构之前的目录结构，请切换到 [`archive/legacy_20260415`](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/archive/legacy_20260415) 分支。

### 仓库结构

```
Tutorial_AwesomeModernCPP/
├── documents/                  # 教程 Markdown 文件
│   ├── vol1-fundamentals/      # 卷一：C++ 基础入门
│   ├── vol2-modern-features/   # 卷二：现代 C++ 特性
│   ├── vol3-standard-library/  # 卷三：标准库深入
│   ├── vol4-advanced/          # 卷四：高级主题
│   ├── vol5-concurrency/       # 卷五：并发编程
│   ├── vol6-performance/       # 卷六：性能优化
│   ├── vol7-engineering/       # 卷七：软件工程实践
│   ├── vol8-domains/           # 卷八：领域应用
│   │   ├── embedded/           #   嵌入式开发
│   │   ├── networking/         #   网络编程
│   │   ├── gui-graphics/       #   GUI 与图形
│   │   ├── data-storage/       #   数据存储
│   │   └── algorithms/         #   算法与数据结构
│   ├── compilation/            # 编译与链接深入
│   ├── projects/               # 贯穿式实战项目
│   └── index.md                # 教程首页
├── code/                       # 示例代码
├── todo/                       # 内容规划与进度追踪
├── scripts/                    # 开发工具脚本
└── mkdocs.yml                  # MkDocs 站点配置
```

### 本地预览

```bash
# 安装依赖
bash scripts/mkdoc_setup_local_dependency.sh

# 启动本地预览
bash scripts/local_preview.sh

# 访问 http://127.0.0.1:8000
```

---

## 教程特色

- **系统化路径**：8 卷从入门到高级，每卷有明确的前置知识要求
- **实战驱动**：每篇文章配合可编译的 CMake 项目代码示例
- **难度分级**：beginner / intermediate / advanced，循序渐进
- **标签导航**：支持按主题、C++ 标准、难度等维度查找文章
- **贯穿项目**：手写 STL 组件、迷你 HTTP 服务器、嵌入式 OS 等综合实战

---

## 开发工具

```bash
# 验证文章元数据
python3 scripts/validate_frontmatter.py

# 检查链接有效性
python3 scripts/check_links.py

# 安装 pre-commit hooks
bash scripts/setup_precommit.sh
```

| 脚本 | 功能 |
|------|------|
| `mkdoc_setup_local_dependency.sh` | 安装 MkDocs 依赖 |
| `local_preview.sh` | 启动本地预览服务器 |
| `setup_precommit.sh` | 安装 pre-commit hooks |
| `validate_frontmatter.py` | 验证文章 frontmatter |
| `check_links.py` | 检查内部链接 |
| `analyze_frontmatter.py` | 分析教程统计信息 |

---

## 贡献指南

我们欢迎任何形式的贡献！请阅读 [CONTRIBUTING.md](./CONTRIBUTING.md) 了解详情。

快速流程：

1. Fork 本仓库
2. 创建特性分支 (`git switch -c feature/amazing-feature`)
3. 提交更改 (`git commit -m '添加某功能'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 开启 Pull Request

如有问题，欢迎在 [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues) 中提交。

---

## 致谢

本项目参考了以下优秀资源：

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial)
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings)
- [CppCon](https://www.youtube.com/user/CppCon)
- [C++ Reference](https://en.cppreference.com/)

---

## 联系方式

- **GitHub Issues**：[提交问题](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- **Email**：725610365@qq.com

---

## 许可证

本项目采用 [MIT License](./LICENSE) 开源协议。

---

<p align="center">
  <b>系统化学习现代 C++，从基础到实战</b>
</p>
