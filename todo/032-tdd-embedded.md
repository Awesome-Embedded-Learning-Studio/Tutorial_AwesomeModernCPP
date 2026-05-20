---
id: 032
title: "嵌入式 TDD 完整教程"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: ["037"]
estimated_effort: large
---

# 嵌入式 TDD 完整教程

## 目标
编写嵌入式领域测试驱动开发（TDD）的完整教程。从测试框架选型到 Host-based testing 策略，从 Mock/Fake 对象设计到 CI 集成，全面覆盖嵌入式软件测试的方法论和实践。读者将学会如何在没有硬件的情况下高效开发和测试嵌入式代码。

## 验收标准
- [ ] 测试框架对比分析（Google Test / Unity / CppUTest / Catch2）
- [ ] Host-based testing 策略完整文档
- [ ] Mock 和 Fake 对象设计模式教程
- [ ] 至少 3 个完整的 TDD 循环演示（Red-Green-Refactor）
- [ ] 硬件模拟层（HAL Mock）实现
- [ ] CI/CD 集成指南（GitHub Actions）
- [ ] 代码覆盖率配置教程（gcov/lcov）
- [ ] 测试驱动的驱动开发完整流程演示
- [ ] 可测试的代码设计原则总结

## 实施说明
嵌入式 TDD 是提高代码质量和开发效率的关键实践。本教程的核心思想是：将硬件依赖与业务逻辑分离，在主机上快速迭代测试。

**内容结构规划：**

1. **嵌入式测试概述** — 为什么嵌入式需要 TDD：传统嵌入式开发流程的痛点（烧录-调试循环慢、硬件不稳定、难以重现 Bug）。Host-based vs Target-based 测试策略。测试金字塔：单元测试 -> 集成测试 -> 系统测试 -> 硬件在环测试。

2. **测试框架选型与搭建** — 框架对比：Google Test（功能全面、C++ 原生）、Unity（C 语言、轻量）、CppUTest（嵌入式友好、C/C++ 兼容）、Catch2/Nonius（现代 C++、头文件优先）。推荐方案：Google Test + Google Mock 用于 C++ 单元测试。CMake 集成：使用 FetchContent 自动获取测试框架。测试目录结构设计。

3. **Host-based Testing 策略** — 核心原理：在 PC 上编译和运行嵌入式代码。编译隔离：将硬件无关逻辑编译为 Host 目标。条件编译：使用 `#ifdef HOST_TEST` 区分平台。硬件抽象层（HAL）的接口设计：纯虚函数接口使 Mock 成为可能。构建系统双目标配置。

4. **Mock 和 Fake 对象** — 概念区分：Stub（存根）、Fake（伪造）、Mock（模拟）、Spy（间谍）。Google Mock 使用：MOCK_METHOD 宏、EXPECT_CALL 匹配器。手写 Fake 对象：FakeGPIO、FakeUART、FakeTimer。硬件寄存器模拟方案。测试数据的注入和提取。

5. **TDD 循环实战演示** — 示例 1：LED 控制器（Red: 写测试 -> Green: 实现功能 -> Refactor: 优化接口）。示例 2：UART 命令解析器（从解析协议到命令执行）。示例 3：温度传感器驱动（ADC 读取 + 温度转换 + 报警逻辑）。每个示例展示完整的 TDD 节奏。

6. **测试驱动的驱动开发** — 从接口设计开始（先写测试定义接口行为）。分层测试策略：测试硬件抽象接口 -> 测试业务逻辑 -> 测试集成。边界条件测试：溢出、空输入、错误状态。回归测试保障。

7. **CI 集成** — GitHub Actions 工作流配置：自动编译、运行测试、生成覆盖率报告。代码覆盖率目标设定和门禁。覆盖率工具配置：gcov + lcov 的 CMake 集成。测试报告生成（JUnit XML 格式）。

8. **高级话题** — 性能测试（基准测试）：使用 Google Benchmark。模糊测试：使用 libFuzzer。静态分析集成：clang-tidy、cppcheck。硬件在环（HIL）测试简介。

## 涉及文件
- documents/embedded/topics/tdd-embedded/index.md
- documents/embedded/topics/tdd-embedded/01-testing-overview.md
- documents/embedded/topics/tdd-embedded/02-framework-setup.md
- documents/embedded/topics/tdd-embedded/03-host-based-testing.md
- documents/embedded/topics/tdd-embedded/04-mock-and-fake.md
- documents/embedded/topics/tdd-embedded/05-tdd-cycles.md
- documents/embedded/topics/tdd-embedded/06-driver-tdd.md
- documents/embedded/topics/tdd-embedded/07-ci-integration.md
- documents/embedded/topics/tdd-embedded/08-advanced-topics.md
- codes/embedded/tdd-examples/ (配套代码和 CMake 配置)

## 参考资料
- 《Test Driven Development for Embedded C》— James Grenning
- 《Working Effectively with Legacy Code》— Michael Feathers
- Google Test 文档 (github.com/google/googletest)
- Google Mock 文档 (github.com/google/googletest/tree/main/googlemock)
- Unity Test API (ThrowTheSwitch/Unity)
- 《Embedded Testing with Unity and CMock》— ThrowTheSwitch
- James Grenning 的博客 (wingman-sw.com)
