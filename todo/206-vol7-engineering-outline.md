---
id: "206"
title: "卷七：软件工程实践 — 全部章节大纲与文章规划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["200"]
blocks: []
estimated_effort: large
---

# 卷七：软件工程实践 — 全部章节大纲与文章规划

## 总览

- **卷名**：vol7-engineering
- **难度范围**：intermediate
- **预计文章数**：30-35 篇
- **前置知识**：卷一
- **目录位置**：`documents/vol7-engineering/`

## 章节大纲

### ch00：CMake 深入

- **预计篇数**：5

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 00-01 | 01-cmake-modern.md | 现代 CMake | target-based、target_link_libraries、target_include_directories | 现代 CMake 项目 |
| 00-02 | 02-cmake-properties.md | 目标与属性 | PUBLIC/PRIVATE/INTERFACE、生成器表达式、传递依赖 | 库项目设计 |
| 00-03 | 03-cmake-fetchcontent.md | FetchContent 与 ExternalProject | 第三方依赖获取、Git 集成、vs submodules | 集成第三方库 |
| 00-04 | 04-cmake-install-cpack.md | 安装与打包 | install 命令、CMake 包配置、CPack 打包 | 可安装项目 |
| 00-05 | 05-cmake-advanced.md | CMake 高级 | 自定义命令/目标、交叉编译工具链、预设文件(CMakePresets) | 跨平台构建 |

### ch01：包管理器

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 01-01 | 01-vcpkg.md | vcpkg | 安装/使用、manifest 模式、 triplet、私有注册表 | vcpkg 项目 |
| 01-02 | 02-conan.md | Conan 2.0 | conanfile、profile、recipe、远程 | Conan 项目 |
| 01-03 | 03-package-comparison.md | 包管理器对比 | vcpkg vs conan vs FetchContent vs submodule 选型指南 | 选型分析 |

### ch02：测试体系

- **预计篇数**：6

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 02-01 | 01-tdd-philosophy.md | TDD 理念 | 红-绿-重构、测试金字塔、FIRST 原则 | TDD 实践 |
| 02-02 | 02-gtest-gmock.md | Google Test/Mock | TEST/TEST_F、匹配器、Mock 框架、参数化测试 | GTest 项目 |
| 02-03 | 03-catch2-doctest.md | Catch2 & doctest | SECTION/BDD、快速编译、表驱动测试 | Catch2 项目 |
| 02-04 | 04-mock-techniques.md | Mock 技术 | 接口 mock、依赖注入、测试替身类型 | Mock 设计 |
| 02-05 | 05-embedded-testing.md | 嵌入式测试技巧 | 主机模拟、HAL mock、硬件在环(HIL)、QEMU 测试 | 嵌入式测试 |
| 02-06 | 06-coverage-ci.md | 覆盖率与 CI 集成 | gcov/lcov、Codecov、CI 测试流水线 | 完整测试流水线 |

### ch03：静态分析

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 03-01 | 01-clang-tidy.md | clang-tidy | 配置、自定义检查、自动修复、CI 集成 | 代码质量提升 |
| 03-02 | 02-cppcheck.md | cppcheck 与其他工具 | cppcheck、cpplint、include-what-you-use | 多工具分析 |
| 03-03 | 03-sanitizers.md | Sanitizers | AddressSanitizer、ThreadSanitizer、UBSanitizer、MemorySanitizer | 错误检测 |

### ch04：DevOps

- **预计篇数**：3

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 04-01 | 01-github-actions.md | GitHub Actions | workflow 配置、矩阵构建、缓存、发布 | CI 流水线 |
| 04-02 | 02-docker-dev.md | Docker 开发环境 | 开发容器、Dockerfile 最佳实践、多阶段构建 | 容器化开发 |
| 04-03 | 03-cicd-pipeline.md | 完整 CI/CD 流水线 | 构建→测试→分析→部署、自动化发布 | 端到端流水线 |

### ch05：项目结构

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 05-01 | 01-project-layout.md | 目录组织 | include/src/test 分离、库 vs 可执行文件、CMake 结构 | 项目骨架 |
| 05-02 | 02-api-design.md | 接口与实现分离 | Pimpl、显式实例化、ABI 稳定性、头文件组织 | API 设计 |

### ch06：第三方库实战

- **预计篇数**：6

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 06-01 | 01-boost-essentials.md | Boost 精选 | Asio/Program_Options/Spirit/Range/Algorithm | Boost 集成 |
| 06-02 | 02-spdlog.md | spdlog 日志 | 异步日志、格式化、性能、自定义 sink | 日志系统 |
| 06-03 | 03-nlohmann-json.md | nlohmann/json | 解析/序列化、自定义转换、Schema 验证 | JSON 处理 |
| 06-04 | 04-cli11.md | CLI11 命令行 | 子命令、选项、验证、帮助生成 | CLI 工具 |
| 06-05 | 05-protobuf-flatbuffers.md | 序列化库对比 | Protobuf vs FlatBuffers vs cereal 选型 | 数据序列化 |
| 06-06 | 06-other-libs.md | 其他实用库 | fmt、expected-lite、tl-expected、outcome 库生态概览 | 库选型 |

### ch07：跨平台开发

- **预计篇数**：2

| 编号 | 文件名 | 标题 | 核心内容 | 练习重点 |
|------|--------|------|---------|---------|
| 07-01 | 01-cross-platform-basics.md | 跨平台基础 | 条件编译、平台宏、CMake 工具链文件 | 跨平台项目 |
| 07-02 | 02-platform-abstraction.md | 平台抽象层 | 接口设计、编译期分派、Docker 环境一致性 | 抽象层设计 |

## 现有内容映射

| 现有文章 | 重写去向 | 备注 |
|----------|---------|------|
| documents/cpp-engineering/* (3篇) | ch00 CMake | 扩展重写 |
| documents/environment-setup/* (2篇) | ch00 CMake | 融入 |
| documents/debugging/* (1篇) | ch03 静态分析 | 扩展 |
| core-embedded-cpp/ch01/* (3篇) | ch00 CMake | 通用化 |
| todo/content/032-tdd-embedded.md | ch02/05 嵌入式测试 | 对齐 |
