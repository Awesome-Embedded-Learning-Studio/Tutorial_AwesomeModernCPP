# Tutorial_AwesomeModernCPP

[English](README.en.md) | 中文

![C++](https://img.shields.io/badge/C%2B%2B-11%20%7C%2014%20%7C%2017%20%7C%2020%20%7C%2023-blue?logo=c%2B%2B)
![Release](https://img.shields.io/github/v/release/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![License](https://img.shields.io/github/license/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Stars](https://img.shields.io/github/stars/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Issues](https://img.shields.io/github/issues/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP)
![Build](https://img.shields.io/github/actions/workflow/status/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/deploy.yml?branch=main)
[![Docs](https://img.shields.io/badge/docs-online-blue)](https://awesome-embedded-learning-studio.github.io/Tutorial_AwesomeModernCPP/)

> 一套系统化的现代 C++ 教程 -- 从基础语法到嵌入式实战，从标准库深入到并发编程，每个概念配有可编译的代码示例

---

## 特色亮点

- **系统化学习路径** -- 8 卷从入门到高级，每卷有明确前置知识，循序渐进
- **实战驱动** -- 每个概念配合可编译的 CMake 项目，而非孤立代码片段
- **多平台覆盖** -- STM32 / ESP32 / RP2040 嵌入式实战，不止于桌面端
- **标签导航** -- 按主题、C++ 标准、难度、平台等维度检索文章
- **在线阅读** -- MkDocs 文档站，支持搜索与导航

---

## 内容架构

```mermaid
graph LR
    V1["卷一 基础"] --> V2["卷二 现代特性"]
    V2 --> V3["卷三 标准库"] & V4["卷四 高级"] & V5["卷五 并发"] & V6["卷六 性能"] & V7["卷七 工程"]
    V2 --> V8["卷八 领域应用"]
    V8 --> E["嵌入式"] & N["网络"] & G["GUI"] & D["数据"] & A["算法"]
```

### 教程结构

| 卷 | 主题 | 文章数 | 难度 | 状态 |
|:--:|------|:------:|:----:|:----:|
| 一 | [C++ 基础入门](documents/vol1-fundamentals/) -- 类型、控制流、函数、指针、类、模板初步 | 49 | beginner | 已完成 |
| 二 | [现代 C++ 特性](documents/vol2-modern-features/) -- 移动语义、智能指针、constexpr、Lambda | 44 | intermediate | 已完成 |
| 三 | [标准库深入](documents/vol3-standard-library/) -- 容器、迭代器、算法、字符串、分配器 | 40-50 | intermediate | 规划中 |
| 四 | [高级主题](documents/vol4-advanced/) -- Concepts、Ranges、协程、模块、模板元编程 | 50-60 | advanced | 规划中 |
| 五 | [并发编程](documents/vol5-concurrency/) -- 线程原语、原子操作、无锁编程、异步 I/O | 25-30 | advanced | 规划中 |
| 六 | [性能优化](documents/vol6-performance/) -- CPU 缓存、SIMD、汇编阅读、基准测试 | 18-22 | advanced | 规划中 |
| 七 | [软件工程实践](documents/vol7-engineering/) -- CMake、测试、静态分析、DevOps | 30-35 | intermediate | 规划中 |
| 八 | [领域应用](documents/vol8-domains/) -- 嵌入式 / 网络 / GUI / 数据存储 / 算法 | 80-100 | intermediate | 编写中 |
| - | [编译与链接深入](documents/compilation/) -- 预处理、汇编、链接、调试符号 | 10+ | intermediate | 已完成 |
| - | [贯穿式实战项目](documents/projects/) -- 手写 STL、迷你 HTTP 服务器、嵌入式 OS | - | advanced | 规划中 |

---

## 学习路径

```mermaid
flowchart TD
    subgraph PathA["Path A -- 有 C 和嵌入式经验"]
        A1["卷二：现代 C++ 特性"] --> A2["卷八：嵌入式开发"]
    end
    subgraph PathB["Path B -- 有 C++ 经验"]
        B1["卷八：基础回顾"] --> B2["平台教程"] --> B3["RTOS 实战"]
    end
    subgraph PathC["Path C -- 两者都会"]
        C1["直接跳到感兴趣的主题"]
    end
    subgraph PathD["Path D -- 零基础新手"]
        D1["卷一：C++ 基础入门（含 C 语言速通）"] --> D2["卷二：现代 C++ 特性"]
    end
    Start(["你的起点?"]) -->|"C + 嵌入式"| PathA
    Start -->|"C++ 经验"| PathB
    Start -->|"两者都会"| PathC
    Start -->|"零基础"| PathD

    style PathA fill:#dbeafe,stroke:#3b82f6,color:#1e3a5f
    style PathB fill:#dcfce7,stroke:#22c55e,color:#14532d
    style PathC fill:#fff7ed,stroke:#f97316,color:#7c2d12
    style PathD fill:#f3e8ff,stroke:#a855f7,color:#581c87
```

---

## 快速开始

```bash
git clone https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP.git
cd Tutorial_AwesomeModernCPP
./scripts/mkdocs_dev.sh install   # 创建虚拟环境并安装依赖
./scripts/mkdocs_dev.sh serve     # 构建并启动本地预览
# 访问 http://127.0.0.1:8000
```

<details>
<summary>更多开发工具</summary>

| 脚本 | 功能 |
|------|------|
| `mkdocs_dev.sh install` | 创建虚拟环境并安装 MkDocs 依赖 |
| `mkdocs_dev.sh serve` | 构建并启动本地预览服务器 |
| `mkdocs_dev.sh build` | 生产模式构建静态站点 |
| `mkdocs_dev.sh clean` | 清理构建产物 |
| `mkdocs_dev.sh reset` | 重建虚拟环境 |
| `setup_precommit.sh` | 安装 pre-commit hooks |
| `validate_frontmatter.py` | 验证文章 frontmatter |
| `check_links.py` | 检查内部链接有效性 |
| `check_nav_reachability.py` | 检查章节导航完整性 |
| `analyze_frontmatter.py` | 分析教程统计信息 |
| `build_examples.py` | 编译所有 CMake 示例项目 |
| `check_quality.py` | 内容质量检查 |

</details>

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| [v0.1.0](changelogs/v0.1.0.md) | 2026-04-29 | 首个公开版本，卷一/卷二/编译卷/嵌入式教程 |

完整变更记录见 [changelogs/](changelogs/) 目录。

---

## 分支说明

| 分支 | 用途 | 状态 |
|------|------|------|
| `main` | 主开发分支 | Active |
| `archive/legacy_20260415` | 重构前存档 | Read-only |
| `gh-pages` | 自动部署的文档站 | Auto-generated |

---

<details>
<summary>项目目录结构</summary>

```
Tutorial_AwesomeModernCPP/
├── documents/                  # 教程 Markdown 文件
│   ├── vol1-fundamentals/      # 卷一：C++ 基础入门（ch00-ch12 + C 语言速通）
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
│   ├── volumn_codes/vol1/      #   卷一代码与练习
│   └── examples/               #   历史代码示例
├── scripts/                    # 开发工具脚本
├── todo/                       # 内容规划与进度追踪
└── mkdocs.yml                  # MkDocs 站点配置
```

</details>

---

## 贡献

我们欢迎任何形式的贡献！请阅读 [CONTRIBUTING.md](./CONTRIBUTING.md) 了解详情。

快速流程：Fork --> 特性分支 --> 提交 --> Push --> Pull Request

如有问题，欢迎在 [GitHub Issues](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues) 中提交。

---

## 致谢

本项目参考了以下优秀资源：

- [modern-cpp-tutorial](https://github.com/changkun/modern-cpp-tutorial)
- [CPlusPlusThings](https://github.com/Light-City/CPlusPlusThings)
- [CppCon](https://www.youtube.com/user/CppCon)
- [C++ Reference](https://en.cppreference.com/)

---

## 许可证与联系方式

- **许可证**：[MIT License](./LICENSE)
- **Issues**：[提交问题](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues)
- **Email**：725610365@qq.com
- **组织**：[Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio)

---

<p align="center">
  <b>系统化学习现代 C++，从基础到实战</b>
</p>
