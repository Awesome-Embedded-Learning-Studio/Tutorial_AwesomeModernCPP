---
id: "017"
title: "卷八：领域应用 TODO"
category: content
priority: P1
status: draft
created: 2026-06-10
assignee: charliechen
depends_on: []
blocks: []
estimated_effort: epic
---

# 卷八：领域应用 TODO

## Current Assets

- `documents/vol8-domains/`：71 个 md。
- 嵌入式内容最强：环境搭建、LED、Button、UART、内存管理、ETL、中断安全、zero-overhead、pointer semantics deep dive。
- `code/stm32f1-tutorials/`：0_start、1_led_control、2_button_control、3_uart_logger。
- `code/examples/chapter05-10` 有嵌入式内存、寄存器、callback、中断安全等配套示例。
- `code/volumn_codes/vol8/cpp-deep-dives/pointer-semantics/`：6 个头文件 + 4 个测试，配套 pointer-semantics 文章。
- networking/gui-graphics/data-storage/algorithms 当前主要是 index。

## Maintenance TODO

- [ ] **A. Frontmatter 补齐**：env-setup（5篇）、LED（13篇）、Button（10/12篇）的 `description` 和 `tags` 为空，需补齐。UART 系列已完成。
- [ ] **B. 补建 index.md**：`00-env-setup/`、`01-led/`、`02-button/` 缺少 index.md，sidebar 无章节着陆页。
- [ ] **C. 更新 embedded/index.md 导航**：纳入 4 个新教程系列入口（env-setup/LED/Button/UART），更新状态描述，移除「规划中」「待重写」标注。
- [ ] **E. 处理 core-embedded-cpp-index.md**：决定保留或合并进 embedded/index.md，验证跨卷链接有效性（尤其卷一重写后路径漂移）。

## Dropped

- RP2040 / RP 嵌入式路线不迁移到新 TODO。
- 跨语言互操作（interop）：不再规划，需要时再启动。

---

## 领域体系（已确认）

卷八并行推进 9 个子领域。每个领域独立成章，各有自己的节奏。

| # | 目录名 | 领域 | 定位 | 当前状态 |
|---|--------|------|------|---------|
| 1 | `embedded/` | 嵌入式开发 | STM32F1 实战 + RTOS + 外设链，项目差异化核心 | **最强**，43 篇教程 |
| 2 | `design-patterns/` | 设计模式 | 现代 C++ 实现经典设计模式，含嵌入式用例 | 待创建 |
| 3 | `networking/` | 网络编程 | 从同步 Socket 起步，渐进到异步 I/O | index 骨架 |
| 4 | `cpp-semantics/` | C++ 语义学分析 | 原 cpp-deep-dives 重命名，聚焦语言机制的语义深度分析 | pointer-semantics 已完成（7 篇） |
| 5 | `serialization/` | 序列化与持久化 | 原 data-storage 扩展，涵盖序列化格式 + 文本处理 + 持久化 | index 骨架，需重建 |
| 6a | `gui-graphics/` | GUI 与图形（桌面） | XCB 渲染基础 + GUI 核心概念（含协程） | index 骨架 |
| 6b | `embedded-gui/` | 嵌入式 GUI | OLED/LCD 显示 + 触摸 + 轻量 Widget 框架 | 待创建 |
| 7 | `algorithms/` | C++ 工程数据结构与算法 | 工程数据结构 + LeetCode 刷题解析（持续扩展） | index 骨架，需重建 |
| 8 | `systems-programming/` | 系统编程 | OS 应用层 API 全覆盖，Modern C++ 封装 | **中文全卷完工**（2026-10-05：ch00-ch07 共 40 篇全部闭环 READY，80+ 路实验入册；EN 40 篇挂账待拍板） |

### 大型项目（外置到 `todo/020-projects.md`）

以下项目规模大、跨领域，在 projects TODO 中独立规划：

- **手搓 MiniGUI 框架**（桌面 XCB + 协程 / 嵌入式 LCD，来自领域 6a/6b）
- **手写 LRU Cache**（来自领域 7）
- **手写线程安全 HashMap**（来自领域 7）
- **迷你搜索引擎：倒排索引 + TF-IDF**（来自领域 7）
- **syskit 系统编程工具库**（来自领域 8）
- **跨平台异步 I/O 事件循环库**（来自领域 8）
- **嵌入式综合项目**（传感器采集/参数管理/电机控制，来自领域 1 Phase 3）

### 领域决策记录

#### D4. 领域体系确认

- **design-patterns**：新增。与 vol4-advanced/vol4-generics-patterns 分工——vol4 做「泛型设计模式」（模板视角，编译期），卷八做「通用现代 C++ 设计模式」（工程实战视角，运行期+编译期混合，含嵌入式用例）。
- **cpp-semantics**：原 `cpp-deep-dives` 重命名。聚焦语言机制的语义深度分析。pointer-semantics 作为首个系列保留。
- **serialization**：原 `data-storage` 扩展并重命名。范围：序列化格式（JSON/Protobuf/MessagePack/TOML）、文本处理（std::format/string_view/正则/CSV/INI）、持久化（SQLite C++ 封装、嵌入式参数存储）、二进制协议设计。
- **algorithms**：重新定位为「C++ 工程数据结构与算法」。范围：工程数据结构（SBO、侵入式容器实战、环形缓冲区变体、布隆过滤器、跳表、LSM-Tree）+ LeetCode 经典题目刷题解析（用现代 C++ 特性解题，扩大受众面）。与 vol3（怎么用）、vol5（怎么并发安全）区分——卷八讲「怎么从零设计和实现」。
- **systems-programming**：新增。范围：POSIX/Win32 API 封装、文件 I/O 与内存映射、进程与线程管理、信号处理、终端与 TTY。拉进 C++ 广泛应用的领域，丰富「real engineering」覆盖面。
- **networking**：首批从同步 Socket 开始（不依赖卷五 coroutine），后续渐进到异步 I/O（select/poll/epoll）、再演进到 coroutine 异步网络。
- **gui-graphics**：桌面 GUI，用 XCB 替代 SDL2 作为平台后端，协程贯穿 GUI 主循环和异步交互。手搓 MiniGUI 框架部分独立到 projects TODO。
- **embedded-gui**：新增，从 gui-graphics 分离。OLED/LCD 显示驱动 + 触摸输入 + 轻量 Widget 框架，独立讨论嵌入式 GUI。
- **algorithms**：LeetCode 章节设计为持续扩展，不限制篇数。大型综合实战项目外置到 projects TODO。
- **systems-programming**：大型工具库项目（syskit、事件循环库）外置到 projects TODO。
- **interop**：drop，不再规划。

#### D5. 大型项目外置

以下项目规模大、跨领域，独立到 `todo/020-projects.md` 规划：
- 手搓 MiniGUI 框架（桌面+嵌入式）
- 手写 LRU Cache、线程安全 HashMap、迷你搜索引擎（算法领域）
- syskit 系统编程工具库、跨平台异步 I/O 事件循环库（系统编程领域）
- 嵌入式综合项目（传感器采集/参数管理/电机控制）

#### D6. 系统编程镜像制与粒度口径（2026-09-28）

- 旧版路线 28 个 bullet 全 Linux 命名、零 Windows 文章槽位，与已落地的「Linux/Windows 两侧镜像」结构脱节；本节重写后口径以「领域 8：系统编程路线」节为准。
- 粒度判据改为「一篇 = 一个能动手实验的核心问题」，信号拆 2 篇、进程拆 3 篇，单篇 200-500 行。
- ch00 原 01（为什么需要系统编程）内容已被 00-overview 吸收，不再单列；02/03 合并为 2 篇概念篇，材料从 L01/W01 平移，公共工具唯一定义处随之挪入（2026-10 已落地）。
- 大型项目文件名勘误：`todo/021-projects.md` 不存在，实为 `todo/020-projects.md`，全文已改。

---

## 嵌入式开发远期规划路线

### 已有资产

**成熟教程系列（~43 篇正文，~10500 行）**：

| 系列 | 篇数 | 配套代码 | 状态 |
|------|------|---------|------|
| 00-env-setup | 5 | code/project-setup | 稳定 |
| 01-led | 13 | code/stm32f1-tutorials/1_led_control | 稳定 |
| 02-button | 12 | code/stm32f1-tutorials/2_button_control | 稳定 |
| 03-uart | 13+index | code/stm32f1-tutorials/3_uart_logger | 稳定 |

**旧独立文章（13 篇，已决定归属方案 D1）**：

| 处理 | 文章 |
|------|------|
| 保留为独立参考（重编号、补 tags） | resource-and-realtime-constraints、zero-overhead-abstraction、object-pool-pattern、crtp-vs-runtime-polymorphism、placement-new、etl、interrupt-safe-coding |
| 合并重写（3→2） | dynamic-allocation-issues + static-and-stack-allocation + fixed-pool-allocation → 两篇「嵌入式内存管理策略」 |
| 移到 vol4 | empty-base-optimization |
| 删除 | array-vs-raw-arrays |
| 归维护 | core-embedded-cpp-index |

**配套代码资产**：

| 位置 | 内容 |
|------|------|
| `code/stm32f1-tutorials/` | 4 个完整工程（start/led/button/uart） |
| `code/examples/chapter05-10` | 内存管理、智能指针、容器、类型安全、函数式、中断安全示例 |
| `code/templates/` | CMake 嵌入式模板、链接器脚本 |

### 增量路线

#### Phase 1：STM32F1 外设链（D2 决定顺序）

每个外设教程遵循统一模板：「硬件动机 → C HAL → C++ 封装 → 陷阱 → 练习」。工程代码与文章同步推进。

| 步骤 | 外设 | 核心内容 | 预估篇幅 | 预估代码 |
|------|------|---------|---------|---------|
| P1-1 | **SPI** | 协议原理、HAL SPI、传感器/Flash 读写、C++ driver wrapper | 8-10 篇 | 1 代码工程 |
| P1-2 | **I2C** | 主机通信、7/10 位地址、错误恢复（NACK/bus error）、传感器读取、C++ abstraction | 8-10 篇 | 1 代码工程 |
| P1-3 | **Timer/PWM** | 基本定时、中断定时、PWM 输出、输入捕获 | 6-8 篇 | 1 代码工程 |
| P1-4 | **ADC** | 单次/连续/扫描模式、DMA 连接、多通道采样、精度与噪声 | 5-7 篇 | 1 代码工程 |
| P1-5 | **DMA** | memory/peripheral transfer、circular mode、与 UART/ADC 结合、double buffer | 4-6 篇 | 1 代码工程 |
| P1-6 | **Flash/Watchdog** | 内部 Flash 编程、参数保存、IWDG/WWDG、故障恢复 | 3-5 篇 | 1 代码工程 |
| P1-7 | **Power/RTC** | 低功耗模式（Sleep/Stop/Standby）、唤醒源、Backup Register、RTC 日历 | 3-5 篇 | 1 代码工程 |

Phase 1 合计：约 **37-51 篇** + **7 个代码工程**。

#### Phase 2：RTOS 路线（D3 决定方案）

| 步骤 | 内容 | 预估 | 核心产出 |
|------|------|------|---------|
| P2-R1 | 协作式调度器 | 3-5 篇 + 1 工程 | SysTick 驱动、任务数组、轮转、延迟队列、C++17 模板封装 |
| P2-R2 | 抢占式调度器 | 2-3 篇 + 1 工程 | PendSV 上下文切换、TCB、栈初始化、优先级调度 |
| P2-R3 | FreeRTOS C API | 3-4 篇 | task、queue、semaphore、mutex、event group |
| P2-R4 | FreeRTOS + 现代 C++ | 3-4 篇 + 1 工程 | RAII wrapper（参考 freertos-mcpp）、expected 错误处理、静态分配 |

Phase 2 合计：约 **11-16 篇** + **3 个代码工程**。

#### Phase 3：嵌入式综合项目

Phase 1 + Phase 2 完成后，用综合项目串联全部知识。

- 候选 1：**传感器数据采集系统** — SPI/I2C 传感器 + DMA + UART 上报 + FreeRTOS 任务调度
- 候选 2：**嵌入式参数管理系统** — Flash 存储 + Watchdog 保护 + RTC 时间戳 + 命令行配置（UART）
- 候选 3：**电机控制系统** — Timer PWM + ADC 电流采样 + PID + FreeRTOS 实时任务

预估：每个项目 3-5 篇文章 + 1 个完整工程。与 `todo/020-projects.md` 交叉。

#### Phase 4：ESP32 第二平台（远期）

- ESP32 工具链搭建（比照 00-env-setup 模板）
- FreeRTOS SMP 双核调度（复用 Phase 2 的 FreeRTOS 知识，扩展到多核）
- WiFi/BLE 基础（与 networking 领域交叉）

不占近期规划位。前置条件：Phase 1-2 基本完成 + 有 ESP32 硬件条件。

#### Phase 5：前沿候选（远期，不占规划位）

- C++20 coroutine 替代 RTOS（bare-metal，参考 five-embeddev 和 vagran/pulse）
- Zephyr 架构对比（Devicetree + Kconfig + C++ 应用层）
- RT-Thread 简介（中文生态对比参考，1-2 篇）
- 嵌入式 C++ 工程化交叉主题（freestanding 约束 → 卷七；启动文件/链接脚本 → 编译链接卷；嵌入式 TDD → 卷七；嵌入式 STL/allocator → 卷三）

### 嵌入式路线总览

| Phase | 内容 | 预估文章 | 预估代码工程 | 前置条件 |
|-------|------|---------|-------------|---------|
| **P1 外设链** | SPI→I2C→Timer→ADC→DMA→Flash→Power | 37-51 | 7 | 无（可立即启动） |
| **P2 RTOS** | 协作调度→抢占调度→FreeRTOS→C++ wrapper | 11-16 | 3 | P1 至少完成 SPI+I2C |
| **P3 综合项目** | 传感器采集 / 参数管理 / 电机控制（选做） | 9-15 | 3 | P1 完成大部分 + P2 完成 R1-R2 |
| **P4 ESP32** | 工具链 + SMP + WiFi/BLE | 8-12 | 2-3 | P2 完成 + ESP32 硬件 |
| **P5 前沿** | coroutine/RTOS、Zephyr、RT-Thread | 不定 | 不定 | 视生态发展 |

### 已决定

#### D1. 旧文章归属方案

| 处理 | 文章 | 说明 |
|------|------|------|
| 保留为独立参考（重编号、补 tags） | resource-and-realtime-constraints、zero-overhead-abstraction、object-pool-pattern、crtp-vs-runtime-polymorphism、placement-new、etl、interrupt-safe-coding | 嵌入式独特价值高，新教程系列会交叉引用 |
| 合并重写（3→2） | dynamic-allocation-issues + static-and-stack-allocation + fixed-pool-allocation → 两篇「嵌入式内存管理策略」 | 三篇内容有内部重复，合并后更连贯 |
| 移到 vol4-advanced | empty-base-optimization | 纯 C++ 语言特性，非嵌入式特定，应随 vol4 的 CRTP 系列 |
| 删除 | array-vs-raw-arrays | vol3/01-array.md 已完整覆盖含嵌入式视角，本文是子集 |
| 归维护 TODO | core-embedded-cpp-index | 见 Maintenance TODO E |

保留的 7 篇文章需统一编号（消除 01×3、04×3、05×3），建议放入 `embedded/reference/` 或统一前缀。

#### D2. 外设链顺序

沿用现有顺序：**SPI → I2C → Timer/PWM → ADC → DMA → Flash/Watchdog → Power/RTC**。

#### D3. RTOS 路线

三步走，自写调度器不可跳过（教学价值不可替代：ARM Cortex-M 异常机制、栈帧布局、PendSV 上下文切换），FreeRTOS 为主力（29% 市场份额、成熟 C++ RAII 包装生态）。

#### D4. 领域体系

见上方「领域体系（已确认）」。9 个子领域并行推进。

### 待讨论

- gui-graphics 具体定位（渲染基础 vs GUI 工程实践）。
- systems-programming 具体路线规划。

---

## 领域 2：设计模式路线

**目录**：`design-patterns/` | **与 vol4 分工**：vol4 做泛型设计模式（模板视角），本系列做通用现代 C++ 设计模式（工程实战视角，含嵌入式用例）

### 第 1 章：现代 C++ 如何改变设计模式

- 01 现代 C++ 与设计模式：范式转移（lambda/std::function 替代策略/命令、variant/visit 替代访问者、RAII 替代手工资源管理）
- 02 值语义 vs 引用语义：设计模式的基础选择（SBO、move 语义对设计的影响）
- 03 编译期多态 vs 运行时多态的工程选择（concepts 约束 vs virtual 继承的性能/RAM/Flash 权衡、嵌入式场景分析）
- 04 组合优于继承：现代 C++ 实践（Mixin、has-a vs is-a、std::variant 作为类型安全的联合）

### 第 2 章：经典模式现代化

- 05 工厂模式：从简单工厂到 concepts 约束（简单工厂→工厂方法→抽象工厂、用 concepts 约束产品类型、用 variant 实现类型安全工厂）
- 06 策略模式：lambda 与 concepts 的双重替代（传统策略→lambda 策略→concepts 策略、嵌入式驱动选择策略）
- 07 观察者模式：信号槽、weak_ptr 与回调安全（传统观察者→std::function 回调→weak_ptr 防悬空、嵌入式事件通知）
- 08 命令模式：std::function 与撤销/重做（命令对象→std::function 封装→撤销栈实现、嵌入式命令解析器）
- 09 访问者模式：variant/visit 彻底重构（传统访问者→variant+visit 实现、编译期类型安全、嵌入式消息分发）

### 第 3 章：创建与生命周期模式

- 10 单例模式的现代替代方案（为什么单例有问题、模块级全局变量、依赖注入、嵌入式中的单例替代）
- 11 对象池模式：从通用到嵌入式专用（通用对象池→ISR 安全对象池、placement new 结合）
- 12 原型模式与 clone：多态复制的正确方式（虚 clone→CRTP clone→variant 基于 index 的构造）

### 第 4 章：结构型模式

- 13 适配器与外观模式：接口统一的工程实践（适配器封装第三方 C API、外观模式简化子系统、嵌入式 HAL 抽象层）
- 14 代理模式：延迟加载、访问控制与远程代理（智能指针作为代理、延迟初始化代理、嵌入式中的寄存器代理）

### 第 5 章：嵌入式特定模式

- 15 状态机模式：从 switch-case 到 variant 状态（传统 switch 状态机→状态表→variant/visit 状态机、嵌入式按钮/协议状态机）
- 16 环形缓冲区与生产者-消费者模式（SPSC 无锁队列、双缓冲、DMA 循环模式、ISR-safe 通信通道）

### 第 6 章：综合实战

- 17 综合实战：嵌入式传感器驱动框架（工厂创建传感器→策略选择采样模式→观察者通知数据→状态机管理生命周期）
- 18 综合实战：命令驱动的设备控制系统（命令模式封装操作→串行命令队列→撤销/重做→UART 命令解析器集成）

---

## 领域 3：网络编程路线

**目录**：`networking/` | **首批可立即启动**：阶段 A（同步 Socket，5 篇，无外部依赖）

### 阶段 A：同步 Socket 基础（可立即启动）

- 01 TCP/IP 基础与 Socket 概述（协议栈核心概念、Socket 抽象层、文件描述符统一模型）
- 02 第一个 TCP 客户端与服务端（完整走通 socket→bind→listen→accept→recv/send→close、字节序、sockaddr_in）
- 03 UDP Socket 与数据报通信（TCP vs UDP 语义对比、消息边界保留、丢包/乱序影响）
- 04 Socket 选项、错误处理与 RAII 封装（SO_REUSEADDR/TCP_NODELAY/SO_KEEPALIVE、EINTR/EAGAIN、unique_fd RAII）
- 05 跨平台 Socket 编程与 Windows 备注（POSIX vs Winsock2 差异、轻量跨平台抽象层）
- **实战**：多线程聊天室（同步阻塞 + thread-per-connection，自然引出「千连接线程爆炸」痛点）

### 阶段 B：I/O 多路复用（依赖阶段 A）

- 06 非阻塞 I/O 与 poll()（O_NONBLOCK + EAGAIN、poll 监控多 fd）
- 07 epoll（Linux）与 kqueue（BSD）深入（epoll 完整生命周期、水平触发 vs 边缘触发、C10K 问题）
- 08 Reactor 模式与事件循环架构（事件驱动+回调分发、EventHandler 接口、timerfd/signalfd、自研框架骨架）
- 09 I/O 多路复用对比与基准测试（select/poll/epoll 性能量化对比、wrk/ab 压测、决策指南）
- **实战**：基于 Reactor + epoll 的 HTTP/1.1 静态文件服务器

### 阶段 C：异步模式与现代发展（依赖阶段 B；12-14 需卷五 coroutine）

- 10 Boost.Asio 入门：同步与异步基础（io_context、Proactor 模式、strand、buffer）
- 11 Asio 回调链与异步操作组合（回调金字塔、async_read/async_read_until、shared_ptr 生命周期管理）
- 12 Asio + C++20 协程：从回调到 co_await（awaitable\<T\>、use_awaitable、co_spawn） ⚠️ 需卷五 ch06
- 13 实战协议设计：从裸 TCP 到应用层协议（TLV 消息定界、粘包/半包处理、请求-响应匹配、心跳机制）
- 14 展望：io_uring、std::execution 与 C++ 网络未来（io_uring 环形缓冲区、C++26 Senders/Receivers、Networking TS 状态）
- **实战**：分布式键值存储（迷你 Redis，主从架构 + Asio + 协程 + 自定义二进制协议）

---

## 领域 4：C++ 语义学分析路线

**目录**：`cpp-semantics/`（重命名自 `cpp-deep-dives`）

遵循 pointer-semantics 模型：**令人惊讶的行为 → 机制分析 → 标准规则 → 实际影响 → 设计原则**

### 已完成

- **指针语义与弱引用设计**（6 篇）：pointer-semantics/ 目录

### 系列 2：值类别与对象生命周期（优先级最高）

- 01 值类别全景：从 C 的左/右值到 C++11 五分类法（两属性模型、lvalue/xvalue/prvalue 历史演变）
- 02 临时物化：prvalue 如何「变成」对象（C++17 prvalue 不再是对象而是「初始化配方」、guaranteed copy elision 是语义必然）
- 03 引用绑定与生命周期扩展的精确规则（直接绑定才能延长、递归扩展、子对象绑定）
- 04 生命周期扩展失效的陷阱（range-for 临时对象、函数返回绑定、成员引用悬空）
- 05 NRVO 与 RVO 的语义深度对比（C++17 guaranteed RVO vs 仍可选的 NRVO、P2025 guaranteed NRVO 提案）
- 06 std::move 与 std::forward 的语义本质（move 不移动任何东西、两次 move 是 UB、move ctor 抛异常时）
- 07 设计原则：编写对值类别安全的代码

**与 vol2 边界**：vol2 教「怎么用 move/RVO」，本系列分析「值类别系统背后的语义」

### 系列 3：对象模型与类型语义（优先级高）

- 01 「C++ 对象」到底是什么（标准定义「a region of storage」、对象创建、完整对象 vs 子对象、lifetime 前后访问）
- 02 Trivial、Standard Layout、POD 的精确语义（trivial 含义、standard layout 含义、POD=C++20 中弃用原因）
- 03 隐式类型转换的完整规则（三段转换序列、重载决议排序、两个用户定义转换歧义、explicit 阻止什么）
- 04 初始化语义全景（C++ 的 N 种初始化、Most Vexing Parse、花括号初始化的 initializer_list 偏好）
- 05 设计原则：利用类型系统编写正确代码

### 系列 4：名字查找与重载决议（优先级高）

- 01 从 #include 到函数调用：名字查找的全过程（无限定/限定查找、overload set 如何构建）
- 02 ADL 的精确语义（关联命名空间和类、hidden friend 惯用法、ADL 二次查找）
- 03 两阶段查找：模板与名字解析（阶段 1 非依赖名、阶段 2 依赖名+ADL、this-> 改变查找行为）
- 04 重载决议排序：编译器如何选择「最佳」函数（隐式转换序列排名、tiebreaker rules、令人惊讶的歧义案例）
- 05 设计原则：编写可预测的查找行为代码（CPO vs ADL 自定义点、hidden friends 最佳实践）

**与 vol4 边界**：vol4 从模板用户角度讲 two-phase lookup，本系列从语言语义角度讲全部查找机制

### 系列 5：未定义行为深度解析（优先级中高）

- 01 「Undefined Behavior」到底意味着什么（标准定义、implementation-defined vs unspecified vs undefined 的区别）
- 02 UB 的主要类别（内存安全、类型系统 strict aliasing、对象生命周期、整数溢出、控制流）
- 03 编译器如何利用 UB 进行优化（假设 UB 不会发生、空指针优化、有符号溢出优化、无限循环消除）
- 04 严格别名规则：最不被理解的 UB 来源（char* 例外、memcpy 合法类型双关、std::bit_cast C++20）
- 05 设计原则：在 UB 面前存活（UBSan/ASan/MSan、用 well-defined 替换 UB-prone patterns、C++26 Contracts）

---

## 领域 5：序列化与持久化路线

**目录**：`serialization/`（重命名自 `data-storage`）

### 第 1 章：序列化格式基础

- 01 序列化全景：为什么你的项目需要它（格式选型决策树、性能维度对比：体积/CPU/易用性/可演进性、全格式对比矩阵）
- 02 JSON 与 Modern C++：nlohmann/json 完全指南（API 设计哲学、to_json/from_json 自定义、宏 vs 自由函数、simdjson 对比）
- 03 Protocol Buffers：Schema 驱动的序列化与版本演进（proto3 语法、C++ 代码生成 API、字段编号与向前/向后兼容）
- 04 MessagePack 与 CBOR：紧凑二进制序列化（msgpack-c API、QCBOR 嵌入式应用 176 字节编码上下文、TinyCBOR 对比）
- **实战**：配置文件解析器（JSON + TOML 输入、schema 校验、std::expected 错误处理）

### 第 2 章：文本处理与格式化

- 05 告别 printf：std::format 与 std::print（C++20 format 完整语法、自定义 formatter、C++23 print）
- 06 string_view 与零拷贝文本解析（生命周期陷阱、基于 string_view 的 tokenizer/parser 设计）
- 07 正则的现代替代：CTRE 编译期模式匹配（std::regex 性能瓶颈、CTRE DFA 编译期正则、named captures）
- 08 实战：CSV、INI 与 TOML 配置解析（用 string_view + CTRE 构建解析器、toml++ 库用法）
- **实战**：嵌入式传感器数据日志格式（零拷贝解析 + TOML 配置参数）

### 第 3 章：数据持久化

- 09 SQLite 的现代 C++ 封装（sqlite_orm 类型安全 DSL、SQLiteCpp RAII 用法、prepared statement 封装）
- 10 嵌入式参数持久化：Flash 存储与磨损均衡（Flash page/sector 布局、键值存储引擎、磨损均衡、CRC 校验、STM32 Flash 编程）
- 11 I/O 错误处理：std::expected 与异常无关的错误传播（expected vs optional vs exception、monadic 操作链）
- **实战**：带持久化的设备配置管理系统（Flash/SQlite 双路径 + 版本迁移）

### 第 4 章：二进制协议设计

- 12 二进制协议基础：字节序、对齐与填充（C++20 std::endian、std::bit_cast、为什么不应直接 fwrite 结构体）
- 13 嵌入式 Protobuf：Nanopb 实战与无堆序列化（.proto→C 结构体映射、固定大小配置、流式编解码、COBS 帧同步）
- 14 零拷贝序列化：FlatBuffers 与 Cap'n Proto（零拷贝原理、wire format=memory format、Protobuf/FlatBuffers/Cap'n Proto 性能对比）
- 15 协议演进：版本管理、向前兼容与实战设计（magic header+version+payload+CRC 帧结构、TLV 编码、从零设计完整通信协议）
- **实战**：STM32F1 传感器数据采集通信协议（Nanopb + COBS + CRC + UART）

---

## 领域 6a：GUI 与图形（桌面）

**目录**：`gui-graphics/` | **定位**：XCB 渲染基础 + GUI 核心概念（含协程），手搓框架部分独立到 projects TODO

### 阶段 A：XCB 渲染基础（4 篇）

- 01 像素的起点：XCB 连接与 Framebuffer 抽象（XCB 连接生命周期、window/pixmap/gc、Canvas 类封装、双缓冲 via pixmap）
- 02 2D 图元光栅化（Bresenham 画线、填充矩形、中点圆、三角形扫描线、XCB_image 像素操作、XCB_put_image 提交）
- 03 位图渲染与 Alpha 混合（XCB render 扩展 vs 软件混合、精灵图集、预乘 alpha、DrawCmd 批处理、脏矩形）
- 04 字体渲染：从 TrueType 到像素（stb_truetype 字形图集、文字排版度量、Font 类、XCB glyph cache）
- **里程碑**：MiniDraw 渲染库（XCB 后端 + 全部图元 + 位图 + 文字）

### 阶段 B：GUI 核心概念（6 篇，协程贯穿）

- 05 信号槽与回调安全（Signal\<Args...\> + Connection + ScopedConnection、weak_ptr 防悬空、与 Qt/ImGui 对比）
- 06 事件捕获与冒泡（XCB 事件→内部事件转换、Widget 树命中测试、capture→bubble 路由、variant\<Mouse/Key/FocusEvent\> + visit）
- 07 布局引擎设计（Measure + Arrange 两遍算法、HBoxLayout/VBoxLayout/GridLayout、stretch 分配、dirty flag）
- 08 协程驱动的异步 UI（co_await 等待用户输入、协程化对话框/向导、async file dialog、与 callback 模式对比）
- 09 模型、视图与数据驱动 + undo/redo（Property\<T\> 可观察属性、数据绑定、Command\<T\> + CommandHistory、Model-View 分离）
- 10 主题与风格系统（Style 属性集、Theme 查询、widget 状态切换、CSS 式继承、Painter 抽象层）

### 独立项目（外置到 `todo/020-projects.md`）

- **手搓 MiniGUI 框架**（Widget 基类 + Label/Button/TextInput + 容器布局 + undo/redo + Mini Notepad 完整 Demo）
  - 桌面端：XCB 后端 + 协程主循环
  - 嵌入式端：STM32 LCD 后端 + 传感器仪表盘 Demo

---

## 领域 6b：嵌入式 GUI

**目录**：`embedded-gui/` | **定位**：OLED/LCD 图形与嵌入式 GUI 框架，独立于桌面 GUI 讨论

### 阶段 A：嵌入式显示基础（3 篇）

- 01 OLED 显示驱动：SSD1306/SH1106 从零到像素（I2C/SPI 通信、帧缓冲区设计、页寻址 vs 列寻址、Canvas 抽象适配小屏）
- 02 TFT LCD 驱动：STM32 LTDC/FSMC + 帧缓冲（RGB565 帧缓冲、DMA2D 加速填充/拷贝、双缓冲+VSync、RGB888 vs RGB565 转换）
- 03 触摸输入处理（电阻屏/电容屏驱动、触摸校准、去抖、坐标映射为 GUI 事件、touch→mouse 事件转换）

### 阶段 B：嵌入式 GUI 框架（3 篇）

- 04 轻量 Widget 系统（Widget 基类、Label/Button/Image、constexpr 主题、Flash 常量图片/字体）
- 05 嵌入式布局与事件分发（HBoxLayout/VBoxLayout、Measure+Arrange、touch 命中测试+事件分发）
- 06 实战：传感器仪表盘 Demo（STM32 LCD+触摸、传感器数据实时显示、Button 切换页面、图表绘制）

---

## 领域 7：C++ 工程数据结构与算法路线

**目录**：`algorithms/` | **定位**：工程数据结构实现 + LeetCode 刷题解析（持续扩展），扩大受众面

与 vol3（怎么用）、vol5（怎么并发安全）区分——卷八讲「怎么从零设计和实现」。

### 第 1 章：工程数据结构实现（6 篇）

- 01 小缓冲优化（SBO）：原理与手写实现（std::string SBO 分析、小型对象直接存储 vs 堆分配、SBO vector/string 手写实现）
- 02 侵入式容器实战（侵入式链表/侵入式红黑树节点设计、vs std::container 的内存模型差异、嵌入式内存池友好场景）
- 03 环形缓冲区变体全家桶（SPSC 无锁、MPSC、动态扩容 ring、DMA 循环模式、ISR-safe 通道）
- 04 布隆过滤器（哈希函数选择、误判率分析、最优位数组大小计算、嵌入式小资源布隆过滤器）
- 05 跳表（SkipList）：从链表到 O(log n)（随机层数、概率平衡、并发友好性分析、vs 红黑树对比）
- 06 LSM-Tree 原理与迷你实现（MemTable→SSTable→Compaction、WAL、布隆过滤器加速查询、嵌入式键值存储场景）

### 第 2 章：LeetCode 刷题解析（持续扩展，不限制篇数）

> 每篇独立成章，按需追加。初始批次如下，后续可扩展位运算、数学、并查集、线段树、树状数组、字典树等。

- 07 数组与双指针（经典题目用 ranges/view 重解、span 避免拷贝、constexpr 编译期验证）
- 08 链表与智能指针安全（unique_ptr 管理、原地操作技巧、现代 C++ 安全链表实现）
- 09 栈、队列与单调结构（用 std::vector 替代裸栈、单调栈/队列经典题、表达式求值）
- 10 二叉树与 DFS/BFS（variant 实现类型安全树节点、递归 vs 迭代转换、Morris 遍历）
- 11 图算法：BFS/DFS/拓扑排序/最短路径（邻接表用 span 避免拷贝、Dijkstra 用 priority_queue、拓扑排序 Kahn 算法）
- 12 动态规划：状态压缩与空间优化（滚动数组、位运算状态压缩、典型 DP 题的现代 C++ 写法）
- 13 贪心与回溯（区间调度、N皇后、排列组合、concepts 约束回溯模板）
- 14 字符串算法（KMP/Rabin-Karp/Z-algorithm、string_view 零拷贝匹配、CTRE 编译期正则）

### 独立项目（外置到 `todo/020-projects.md`）

- **手写 LRU Cache**（双向链表 + 哈希表、LeetCode #146 现代实现、侵入式设计变体）
- **手写线程安全 HashMap**（开放寻址 vs 链式、分段锁策略、atomic 指针操作）
- **迷你搜索引擎：倒排索引**（文本分词→倒排索引构建→TF-IDF 排名→Top-K 查询）

---

## 领域 8：系统编程路线

**目录**：`systems-programming/` | **定位**：Linux 主线深讲 + Windows 对照篇 + Windows 独有篇，Modern C++ 视角的系统编程，求全不求简（分批推进）

### 口径与镜像制（2026-09-28 对齐，取代旧版全 Linux 单列路线）

- **一篇 = 一个能动手实验的核心问题**。每个机制配可复现实验与真实输出，机制讲透再走；单篇 200-500 行，超出就拆。
- **平台组织三分类**：共享机制在主平台（Linux）深讲一次；Windows 镜像篇小节同名对照，API 拼写对照可薄；**机制分叉**（如截短拦截 vs SIGBUS、64KiB 分配粒度、读满语义）两侧都全量讲。
- **Windows 独有能力**（IOCP、ACL、注册表、SEH、Console API）独立成篇，开篇声明自己是哪一层的扩展，不当缩水镜像写。
- **公共工具唯一定义处**：unique_fd / unique_handle / mapped_region / sys_call / errno_code 全家族的定义挪到 ch00 概念篇，L01/W01 修订时改为引用，防止两侧各定义一遍（2026-10 已落地）。错误工具必须「定义即用」（L01 的 errno_code 定义后全篇零调用是反面教训）。
- 与兄弟子域划界：epoll 机制与事件循环已在 vol5 ch06 与 networking 深讲，本域 ch04 只做全景衔接不重开课。

### 现状（2026-10-02：ch00+ch01 全部交付）

- 已交付 13 篇全部闭环 READY（分支 feat/system_programming，未 commit）：`thinking/01-02`（思维基石）、`linux/file-io/01-06`、`windows/file-io/01-05`，加总纲与各级 index；13 路实验入册 `code/volumn_codes/vol8/systems-programming/`（WSL2 真机 + MSYS2 interop 跑 Windows 原生 .exe），实验简报在 `.claude/drafts/sysprog-briefs/`。
- 既定修订全部落地：L01 补课（EINTR/fcntl/dup3-CLOEXEC/SEEK 家族/exp 编号双映射）、L02 整篇重写（2026-09）+ 存量 lint 清零（2026-10）、W01 补课（指针尺寸/FlushFileBuffers/dwShareMode 矩阵）、W02 扩写（VirtualProtect/SEC_RESERVE/选型实测）、00-总纲刷新（进度句/章表/思维基石统一）。
- 口径变更记录：「契约」是作者确认的禁词——全子卷改「公共工具」，章节名统一「思维基石」；实验编号口径为 Linux 大写 E 系、Windows 小写 e 系、L01 用 exp1-11↔存档 E1-E3 双映射，跨篇撞号靠「互不相干」桥接句。
- EN 全空白（挂账，节奏待定）。

### ch00 思维基石（2 篇）✅ 已交付

- 01 OS 资源的 RAII 范式 ✅（539 行——公共工具唯一定义处之一）
- 02 错误处理范式：从 errno 到 expected ✅（598 行——sys_call/errno_code/last_error_code 定义处）

### ch01 文件 I/O（Linux 6 篇 + Windows 5 篇）✅ 已交付

Linux（`linux/file-io/`）：

- 01 POSIX 文件 I/O ✅（468 行——补课四主题全落、定义改引用思维基石）
- 02 mmap 内存映射 ✅（622 行——2026-09 整篇重写，2026-10 存量清零）
- 03 页缓存与持久性 ✅（345 行——E1-E6 六实验）
- 04 std::filesystem：目录与元数据 ✅（425 行——含双树 diff 实战）
- 05 文件锁：flock 与 fcntl 记录锁 ✅（467 行——E2d 全释放陷阱+OFD 对照）
- 06 inotify 文件监控 ✅（493 行——合并四元组实测+溢出 16384）

Windows（`windows/file-io/`）：

- 01 Win32 文件 I/O ✅（517 行——补课三主题+dwShareMode 全矩阵）
- 02 文件映射 ✅（534 行——扩写四节+W03 钩子）
- 03 结构化异常：SEH 与 VEH ✅（496 行——UCRT 变体 filter ABI 独家逆向）
- 04 目录枚举与 NTFS 家族 ✅（425 行——宽字符九姿势+家族矩阵）
- 05 文件锁：LockFileEx ✅（472 行——第三种答案三发现+三方对照表）

### ch02 内存管理：OS 层面（Linux 4 篇 + Windows 2 篇）✅ 已交付（2026-10-03）

Linux（`linux/memory/`）：

- 01 进程内存布局与 /proc/pid/maps ✅（292 行——41 段全图、malloc 分水岭两层机制、ASLR）
- 02 虚拟内存 API 全景 ✅（446 行——guarded_buffer 招牌、MADV 全家、si_addr 字节精度纠正）
- 03 共享内存 shm_open ✅（380 行——竞态三版、SPSC 无锁队列、与 Windows 两段死镜像互验）
- 04 对齐分配与大页 ✅（331 行——WSL2 进程级关死 THP 的独家发现、Overcommit 数字链）

Windows（`windows/memory/`）：

- 01 虚拟内存 VirtualAlloc ✅（314 行——64KB 粒度双轨、PAGE_GUARD 一次性三连、VQ 全扫）
- 02 共享内存 ✅（431 行——名字/对象两段死、四模式同机对比、八行对照表）

### ch03 进程与信号（Linux 5 篇 + Windows 2 篇）✅（2026-10-04 全部闭环 READY）

Linux（`linux/process/`、`linux/signal/`）：

- 01 进程创建与生命周期：fork/exec/posix_spawn ✅（443 行——COW 的 Pss 实证、1 GiB fork 31.6ms 对 posix_spawn 0.48ms、孤儿收养者实测 pid 249 Relay、RAII child_process）
- 02 守护进程、会话与环境 ✅（360 行——setsid 六组实测变化链、孤儿被 249 收养与 01 互证、rlimit 双段时序、environ 所有权陷阱）
- 03 IPC：管道、FIFO 与 POSIX mq/sem ✅（471 行——popen 走 clone3(vfork) 纠正教科书、PIPE_BUF 4096 零撕裂/8192 撕裂、SCM_RIGHTS 传 fd 引用复制、六通道选型矩阵）
- 04 信号（上）：sigaction 与异步信号安全 ✅（509 行——printf 损坏 562 行双口径翻案（完好整行重复为大头）、volatile 汇编旁证、self-pipe 三模式）
- 05 信号（下）：实时信号、signalfd 与 pidfd ✅（390 行——pid 复用竞态真实现场（新 namespace 绕 RESERVED_PIDS）、SFD_CLOEXEC 创建时定性探针、prefork echo 优雅关闭全时序）

Windows（`windows/process/`）：

- 01 进程与作业：CreateProcessW 与 Job 对象（对照 linux/01）✅（257 行——未引号路径 Program.exe 攻击复刻、四种死法清理矩阵、Job 嵌套出走与限额）
- 02 控制台事件与 APC（SetConsoleCtrlHandler 是 Windows 对信号的最近对照；QueueUserAPC）✅（316 行——CTRL_C 不投递根因是继承忽略位（推翻 ConPTY 归因）、APC 排队零执行对 SleepEx FIFO 连跑）

### ch04 I/O 多路复用与异步 I/O（Linux 3 篇 + Windows 2 篇 + 跨平台 1 篇）✅（2026-10-04 全部闭环 READY）

- linux/01 select→poll→epoll 全景 ✅（333 行——1024 上限归属翻案（glibc 位图/内核不查 rlimit/两 man 都承诺只 poll 兑现）、pselect 保 const 是 glibc 垫的双层归属、成本曲线纳秒口径）
- linux/02 timerfd 与 eventfd ✅（319 行——上限 2^64−2 纠正、信号量+LT 忙通知档连醒 5 次、改 interval 首档沿用旧 it_value、1ms 档中位 999.3µs）
- linux/03 io_uring ✅（357 行——双轨（裸 syscall 425 衔接编号表→liburing 2.15）、SQPOLL 特权门 5.13（内核源码核）、enter 的 SQ_WAKEUP 实测收窄、页缓存热路径墙钟打平 syscall 差 128 倍）
- windows/01 OVERLAPPED 异步 I/O ✅（367 行——同步句柄+OVERLAPPED 指针 16→8008、两堵墙 64/65 与 63/64 分开量、CancelIo 线程归属 996 不动、hEvent=NULL 句柄信号分不清）
- windows/02 IOCP 完成端口 ✅（404 行——第四档补跑推翻原结论（并发值 0+一口气四线全醒，上限不派活只放行）、CloseHandle 后完成包 109 立即送达、Job 挂端口三包字段落点）
- 03 跨平台异步 I/O 抽象 ✅（353 行——四后端八维矩阵锚点全引两侧实测、AsyncBackend concept 双侧编译零警告+负例诊断原文、kqueue 全文档口径、string_view 过 %s 段错误返工教学点）

### ch05 时间与定时器（3 篇，平台无关为主）✅（2026-10-04 全部闭环 READY）

- 01 时钟源与 POSIX 时间 API ✅（305 行——RAW−MONOTONIC 冻结分歧 −106.4ms、vDSO 16.9 对 168.9ns 十倍、tick 三口径 298.0/298.1/290）
- 02 C++20 std::chrono 深度 ✅（342 行——四钟纪元 +27/+37/+18s、闰秒 23:59:60 实存、中国 1988 夏令时教例、tzdb 落 OS 的 strace 证据；sys_days UB 误说经保真翻案改定义良好折算）
- 03 定时器全景 ✅（355 行——exec 保留翻转（man+补测双证：alarm/itimer 跨 execve 存活）、SIGEV_THREAD 每次派新线程、漂移对照约 78ms 对 0.08ms 以下）

### ch06 终端与控制台（Linux 2 篇 + Windows 1 篇）✅（2026-10-05 全部闭环 READY）

- linux/01 termios 与 raw 模式 ✅（402 行——^C 冲队与递信号可拆、VMIN/VTIME 字符间计时器 D+F 合判、glibc 2.42 存档重录）
- linux/02 伪终端（PTY）与进程交互 ✅（328 行——script(1) 复刻与 timing 回放逐字节一致、master 读写不对称 EIO 判据）
- windows/01 Console API ✅（319 行——读回验收方法论、VT 开关对照、Win11 2J 不归零光标、87 断言补实验重录）

### ch07 平台抽象与工具对账（2 篇）✅（2026-10-05 全部闭环 READY，全卷收官）

- 01 平台抽象层设计 ✅（376 行——死分支藏三错 Linux 零警告/Windows 全现形、CMake 身份与能力两线分叉、_open_osfhandle 真桥收 uintptr_t 口、虚表 1-5ns 占系统调用 1-4%）
- 02 syskit 工具库工程化 ✅（336 行——两套近亲逐项对照与统一方案（syskit:: 蛇形+error_code 双出口，影响面含 lab0 拷贝）、INTERFACE 库零平台分支双平台全过、getpid 假基准 1.3ns 纠正）

### Windows 独有篇候补（占位，按需启动）

- 安全描述符与 ACL（lpSecurityAttributes 不能永远 nullptr）
- 注册表（配置持久化的 Windows 正道）

### 独立项目（外置到 `todo/020-projects.md`）

- **syskit 系统编程工具库**（ch07/02 的完整工程化产出）
- **跨平台异步 I/O 事件循环库**（ch04/03 的完整工程化产出）
