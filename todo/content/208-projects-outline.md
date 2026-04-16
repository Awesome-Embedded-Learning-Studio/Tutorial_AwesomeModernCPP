---
id: "208"
title: "贯穿式实战项目 — 全部规划"
category: content
priority: P2
status: pending
created: 2026-04-16
assignee: charliechen
depends_on: ["201", "203", "204", "207"]
blocks: []
estimated_effort: epic
---

# 贯穿式实战项目 — 全部规划

## 总览

- **目录位置**：`documents/projects/`
- **代码位置**：`code/projects/`
- **难度范围**：intermediate → advanced

## 项目列表

### project-01：手写 STL 组件

- **难度**：advanced
- **关联卷**：卷三（标准库深入）
- **预计篇数**：6-8
- **前置**：卷一 + 卷二 + 卷三

| 编号 | 标题 | 核心内容 | 练习重点 |
|------|------|---------|---------|
| 01-01 | 手写 vector | 内存管理、增长策略、异常安全 | 完整 vector 实现 |
| 01-02 | 手写 string | SSO 实现、短字符串优化 | 字符串实现 |
| 01-03 | 手写 unique_ptr | RAII、移动语义、自定义删除器 | 智能指针实现 |
| 01-04 | 手写 optional | 存储布局、engaged 状态管理 | 可选值实现 |
| 01-05 | 手写 function | 类型擦除、SBO | 函数包装器实现 |
| 01-06 | 手写 variant | 访问者模式、never-empty 保证 | 类型安全联合体 |

### project-02：迷你 HTTP 服务器

- **难度**：intermediate → advanced
- **关联卷**：卷五（并发）+ 卷八（网络）
- **预计篇数**：4-6
- **前置**：卷一 + 卷二 + 卷五

| 编号 | 标题 | 核心内容 | 练习重点 |
|------|------|---------|---------|
| 02-01 | TCP 服务器基础 | Socket 编程、连接管理 | 基础服务器 |
| 02-02 | HTTP 协议解析 | 请求解析、响应构造 | HTTP 实现 |
| 02-03 | 路由与中间件 | 路由树、中间件链、静态文件 | Web 框架 |
| 02-04 | 异步版本 | Boost.Asio 异步、协程集成 | 高性能服务器 |

### project-03：迷你 GUI 框架

- **难度**：advanced
- **关联卷**：卷八（GUI/图形）
- **预计篇数**：4-6
- **前置**：卷一 + 卷二 + 卷四

| 编号 | 标题 | 核心内容 | 练习重点 |
|------|------|---------|---------|
| 03-01 | 窗口与事件循环 | 平台抽象、消息循环、输入处理 | 窗口系统 |
| 03-02 | Widget 系统 | 控件层次、事件分发、焦点管理 | 控件框架 |
| 03-03 | 布局引擎 | 弹性布局、约束求解、响应式 | 布局系统 |
| 03-04 | 渲染后端 | 软件光栅化、OpenGL 后端 | 渲染引擎 |

### project-04：嵌入式操作系统

- **难度**：advanced
- **关联卷**：卷八（嵌入式）
- **预计篇数**：4-6
- **前置**：卷一 + 卷二 + 卷八嵌入式

| 编号 | 标题 | 核心内容 | 练习要点 |
|------|------|---------|---------|
| 04-01 | 任务调度器 | 任务控制块、优先级调度、上下文切换 | 调度器实现 |
| 04-02 | 同步原语 | 信号量、互斥量、事件标志 | IPC 实现 |
| 04-03 | 内存管理 | 堆管理、内存保护、MPU | 内存子系统 |
| 04-04 | 设备驱动框架 | 驱动抽象、设备树、中断管理 | 驱动框架 |

### project-05：INI 解析器（已有基础）

- **难度**：intermediate
- **关联卷**：卷七（工程）+ 卷八（数据存储）
- **预计篇数**：2
- **前置**：卷一 + 卷二
- **参考**：已有 Tutorial_cpp_SimpleIniParser 仓库

### project-06：协程 Echo 服务器（已有基础）

- **难度**：advanced
- **关联卷**：卷四（协程）+ 卷五（并发）
- **预计篇数**：2
- **前置**：卷四 ch02 + 卷五 ch06
- **参考**：已有 cpp-features/coroutines/03-echo-server.md

## 项目代码组织

```
code/projects/
├── CMakeLists.txt                 # 顶层 CMake
├── project-01-handwritten-stl/    # 手写 STL
├── project-02-mini-http-server/   # HTTP 服务器
├── project-03-mini-gui-framework/ # GUI 框架
├── project-04-embedded-os/        # 嵌入式 OS
├── project-05-ini-parser/         # INI 解析器
└── project-06-echo-server/        # Echo 服务器
```

每个项目包含：
- CMakeLists.txt
- README.md
- src/
- tests/
- docs/ (详细教程文档引用)
