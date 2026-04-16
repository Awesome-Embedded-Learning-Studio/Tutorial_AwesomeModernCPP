---
id: 022
title: "核心理论教程扩展至 28 章（Ch13-Ch28 新增内容规划与实施）"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
# NOTE: 本 TODO 内容已拆分到多个卷：
#   Ch13-Ch14 (中断/寄存器) → vol8-domains/embedded/ch03 + ch04
#   Ch15-Ch16 (数据结构/设计模式) → vol3-standard-library + vol4-advanced/ch09
#   Ch17-Ch18 (性能/工具链) → vol6-performance + vol7-engineering
#   Ch19-Ch21 (能耗/网络/序列化) → vol8-domains/embedded + networking + data-storage
#   Ch22-Ch23 (安全/测试) → vol7-engineering/ch02 + ch03
#   Ch24-Ch28 (Linux/协程/高级) → vol8-domains/embedded + vol4-advanced/ch02
# 详细大纲见各卷对应的 TODO 200-209 文件
blocks: []
estimated_effort: epic
---

# 核心理论教程扩展至 28 章

## 目标
将现有的核心理论教程（Ch0-Ch12）扩展至 28 章。现有 13 章（Ch0 入门 + Ch1-Ch12 基础内容）保持不变，新增 Ch13-Ch28 共 16 章。内容基于 `drafts/Content-Table-Draft.md` 的详细规划，分 6 个批次推进实施。目标是构建从基础到高级的完整嵌入式 C++ 理论知识体系。

## 验收标准
- [ ] 新增 16 章内容全部编写完成并通过审校
- [ ] 每章遵循统一的教程模板（目标 -> 概念 -> 代码示例 -> 练习 -> 小结）
- [ ] 所有代码示例可编译运行（使用 godbolt 或本地编译验证）
- [ ] 每章至少包含 3 个可运行的代码示例和 3 道练习题
- [ ] 章节之间有明确的依赖关系和交叉引用
- [ ] 分 6 个批次按序推进，每批次完成后进行一次审校

## 实施说明
### 批次规划与章节概要

**批次 1：Ch13-Ch14（中断与寄存器外设访问）**

- **Ch13：中断与 ISR 编程模型**
  - 中断向量表与中断服务程序
  - ARM Cortex-M 中断处理流程（异常进入/退出、堆栈帧、中断返回）
  - ISR 的 C++ 实现注意事项（volatile、naked 函数、链接器脚本）
  - 中断安全编程：临界区、原子操作、内存屏障
  - NVIC 配置与优先级管理
  - 示例：SysTick 中断、EXTI 外部中断、UART 接收中断

- **Ch14：寄存器级外设访问**
  - 从 HAL 到寄存器：为什么需要理解寄存器
  - CMSIS 设备头文件结构与使用
  - volatile 指针与寄存器映射原理
  - 位操作技巧：置位、清零、翻转、读取特定位
  - C++23 的寄存器封装模式：BitField、RegisterProxy
  - 示例：直接操作 GPIO 寄存器实现 LED 闪烁、按钮检测

**批次 2：Ch15-Ch16（数据结构与设计模式）**

- **Ch15：嵌入式常用数据结构**
  - 环形缓冲区（Ring Buffer）：实现、线程安全变体、DMA 双缓冲应用
  - 队列（FIFO）、栈（LIFO）：静态分配实现
  - 链表：侵入式链表（intrusive list）与内存池
  - 位图（Bitmap）：用于资源管理和状态标记
  - 优先级队列/堆：用于事件调度
  - 所有数据结构使用 `std::array` 固定大小，无动态内存分配
  - 示例：UART 接收环形缓冲区、事件队列、任务调度器

- **Ch16：嵌入式设计模式**
  - 状态机模式：FSN（有限状态机）、状态转移表、层次状态机（HSM）
  - 观察者模式：事件通知、发布-订阅
  - 策略模式：运行时算法切换（滤波算法、通信协议）
  - 模板方法模式：HAL 抽象层的骨架-细节分离
  - CRTP（Curiously Recurring Template Pattern）：静态多态，零开销
  - RAII 模式：资源管理（GPIO、SPI CS、临界区）
  - 示例：带状态机的按键消抖、多传感器管理器

**批次 3：Ch17-Ch18（性能分析与编译器优化）**

- **Ch17：性能分析与优化**
  - 性能分析方法：SysTick 计时、GPIO 翻转法、逻辑分析仪
  - 常见性能瓶颈：中断延迟、总线竞争、缓存未命中（对有缓存的 MCU）
  - 算法复杂度选择：O(1) vs O(n) 在嵌入式中的实际影响
  - 内存优化：减少 RAM/Flash 占用的技巧
  - 功耗优化：时钟门控、休眠策略、外设使用优化
  - 示例：ADC 采样率瓶颈分析、UART 吞吐量优化

- **Ch18：编译器优化与工具链**
  - GCC 优化选项详解：-O0/-O1/-O2/-Os/-Og/-O3 的区别与适用场景
  - Link-Time Optimization（LTO）：原理与使用
  - 链接脚本（Linker Script）基础：内存布局、段分配、自定义段
  - 映射文件（.map）分析：Flash/RAM 占用、符号地址
  - 编译器警告：开启 -Wall -Wextra -Wpedantic 的必要性
  - 静态分析：cppcheck、clang-tidy 在嵌入式中的应用
  - 示例：优化一个 FFT 实现、分析 Flash 占用并优化

**批次 4：Ch19-Ch21（能耗/网络/序列化）**

- **Ch19：能耗管理策略**
  - 功耗预算与电池寿命计算
  - 动态电压频率调整（DVFS）概念
  - 外设使用策略：按需使能时钟、低功耗外设配置
  - 采样策略：间歇采样 vs 连续采样
  - 无线通信能耗优化：短包、低占空比
  - 示例：计算电池供电传感器节点的工作寿命

- **Ch20：嵌入式网络基础**
  - 串行通信协议对比：UART/SPI/I2C/CAN/1-Wire
  - CAN 总线：帧格式、仲裁机制、错误处理
  - 网络协议栈概览：物理层 -> 数据链路层 -> 网络层 -> 传输层 -> 应用层
  - MQTT 协议：发布/订阅模型、QoS 级别、嵌入式客户端
  - CoAP 协议：RESTful 风格的轻量级协议
  - 示例：ESP32 MQTT 客户端连接云平台

- **Ch21：数据序列化**
  - 结构体打包与对齐：`#pragma pack`、`__attribute__((packed))`
  - 字节序问题：大端/小端、网络字节序、`std::byteswap`/`htonl`/`ntohl`
  - TLV（Type-Length-Value）编码格式
  - Protocol Buffers（nanopb）：嵌入式友好的序列化方案
  - JSON 序列化（轻量库）：ArduinoJson / cJSON
  - 自定义二进制协议设计：帧头、负载、校验、转义
  - 示例：设计一个传感器数据采集的二进制通信协议

**批次 5：Ch22-Ch23（安全与测试 CI）**

- **Ch22：嵌入式安全基础**
  - 威胁模型：物理攻击、旁路攻击、固件逆向
  - 安全启动（Secure Boot）：链式信任验证
  - 固件加密：AES-256 加密存储、运行时解密
  - 安全通信：TLS/DTLS 在嵌入式中的应用
  - 密钥管理：OTP、Flash 保护、调试接口禁用
  - 代码防御：栈保护（-fstack-protector）、DEP、ASLR
  - 示例：STM32 Flash 读写保护配置、简单的 AES 加密演示

- **Ch23：测试与持续集成**
  - 单元测试框架：Catch2 / Google Test / doctest
  - 硬件抽象层（HAL）的 mock/stub 测试策略
  - 在主机上测试嵌入式代码：编译期条件编译、模拟层
  - 硬件在环测试（HIL）：自动化测试框架设计
  - CI/CD 流水线：GitHub Actions 自动编译、测试、生成报告
  - 代码覆盖率：gcov / lcov 在嵌入式项目中的应用
  - 示例：为 UART 驱动编写单元测试、配置 GitHub Actions CI

**批次 6：Ch24-Ch28（嵌入式 Linux + 高级主题）**

- **Ch24：嵌入式 Linux 入门**
  - 嵌入式 Linux vs 裸机：何时需要 Linux
  - Linux 启动流程：Bootloader -> Kernel -> RootFS -> User Space
  - 交叉编译工具链：crosstool-NG / Buildroot / Yocto
  - 设备树（Device Tree）：硬件描述与驱动匹配
  - 示例：在 Raspberry Pi 上交叉编译并运行 C++ 程序

- **Ch25：嵌入式 Linux 外设编程**
  - Linux 设备模型：字符设备、块设备、网络设备
  - sysfs / devfs：用户空间外设访问
  - GPIO：libgpiod 库使用
  - I2C/SPI：`/dev/i2c-N`、`/dev/spidevN.M`
  - PWM：sysfs PWM 接口
  - 用户空间中断：GPIO 中断的 poll/epoll 机制
  - 示例：Linux 用户空间 I2C 传感器驱动

- **Ch26：协程与异步编程**
  - C++20 协程基础：co_await / co_yield / co_return
  - 协程机制：Promise Type、Awaitable、Coroutine Handle
  - 嵌入式协程应用：异步 I/O 操作、非阻塞延迟、状态机编码
  - 轻量级调度器：基于定时器的协程调度
  - 性能考量：协程的内存开销、栈式 vs 无栈协程
  - 示例：用协程实现异步 UART 收发、协程驱动的 LED 动画序列

- **Ch27：高级功耗优化**
  - 功耗分析工具：电流探头、功耗分析仪
  - 细粒度功耗管理：逐外设、逐任务的功耗优化
  - 动态功耗管理（DPM）：运行时策略切换
  - 能耗感知调度：任务调度考虑功耗
  - 超低功耗设计：亚微安级待机、能量收集
  - 示例：设计一个功耗优化的传感器采集系统（完整功耗分析与优化过程）

- **Ch28：高级安全与 OTA**
  - 安全固件更新（OTA）：A/B 分区、回滚机制
  - 固件签名与验证：RSA/ECDSA 签名校验
  - 安全通信端到端：TLS 1.3 在嵌入式中的实现（mbedTLS）
  - 硬件安全模块（HSM）：STM32 TrustZone、加密加速器
  - 安全认证：FIPS 140、Common Criteria 概览
  - 示例：实现一个简单的安全 OTA 更新流程

## 涉及文件
- documents/embedded/theory/ch13-interrupt-isr/index.md
- documents/embedded/theory/ch14-register-access/index.md
- documents/embedded/theory/ch15-data-structures/index.md
- documents/embedded/theory/ch16-design-patterns/index.md
- documents/embedded/theory/ch17-performance/index.md
- documents/embedded/theory/ch18-compiler-toolchain/index.md
- documents/embedded/theory/ch19-energy-management/index.md
- documents/embedded/theory/ch20-networking/index.md
- documents/embedded/theory/ch21-serialization/index.md
- documents/embedded/theory/ch22-security/index.md
- documents/embedded/theory/ch23-testing-ci/index.md
- documents/embedded/theory/ch24-embedded-linux-intro/index.md
- documents/embedded/theory/ch25-linux-peripherals/index.md
- documents/embedded/theory/ch26-coroutines/index.md
- documents/embedded/theory/ch27-advanced-power/index.md
- documents/embedded/theory/ch28-advanced-security/index.md
- code/examples/theory/（每章对应的示例代码子目录）

## 参考资料
- drafts/Content-Table-Draft.md（完整的章节规划草稿）
- ISO C++23 标准文档
- ARM Cortex-M Programming Guide
- MISRA C++ Guidelines
- Embedded C++ (EC++) 相关资料
