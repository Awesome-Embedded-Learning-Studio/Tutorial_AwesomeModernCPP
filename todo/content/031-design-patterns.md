---
id: 031
title: "嵌入式设计模式专题"
category: content
priority: P2
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["architecture/002"]
blocks: ["037"]
estimated_effort: large
---

# 嵌入式设计模式专题

## 目标
编写面向嵌入式系统的设计模式教程。覆盖状态机、观察者、策略、单例、工厂、命令、代理等经典模式，强调在资源受限、实时性要求、硬件依赖等嵌入式约束下的模式变体和实现技巧。使用现代 C++（C++14/17）实现，对比传统 OOP 方案和编译期多态方案。

## 验收标准
- [ ] 状态机模式：完整的状态机框架实现（支持层次状态机 HSM）
- [ ] 观察者模式：事件系统实现（支持编译期订阅和运行时订阅）
- [ ] 策略模式：编译期多态（模板）vs 运行时多态（虚函数）对比
- [ ] 单例模式：线程安全实现 + 硬件外设管理应用
- [ ] 工厂模式：对象创建模式，含编译期工厂
- [ ] 命令模式：任务队列和命令调度实现
- [ ] 代理模式：硬件抽象层设计
- [ ] 每个模式至少 2 个嵌入式场景示例
- [ ] 内存和 CPU 开销对比分析表
- [ ] 模式选择决策树

## 实施说明
设计模式在嵌入式开发中有独特的应用方式和约束。本教程不是简单地复述 GoF 模式，而是从嵌入式实际问题出发，展示如何用现代 C++ 优雅地解决它们。

**内容结构规划：**

1. **嵌入式设计模式概述** — 嵌入式开发中的设计挑战：资源受限、实时性要求、硬件耦合、长期维护。模式选择标准：ROM/RAM 开销、执行时间确定性、可测试性。编译期 vs 运行时多态的权衡。

2. **状态机模式** — 从 switch-case 到表格驱动状态机。现代 C++ 实现：使用 std::variant + std::visit 的编译期状态机。层次状态机（HSM）：层次化状态转换、进入/退出动作。示例：按钮防抖状态机、通信协议状态机、设备电源管理状态机。

3. **观察者模式** — 传统观察者的问题（虚函数开销、生命周期管理）。现代 C++ 方案：std::function 回调、编译期事件总线（基于 type_list）、信号槽实现。嵌入式场景：中断事件通知、传感器数据分发、系统事件广播。注意 ISR 中的安全性。

4. **策略模式** — 运行时策略（虚函数 + 多态）vs 编译期策略（模板 + CRTP）。性能对比：代码大小、执行时间、内存占用。嵌入式场景：不同传感器的数据处理策略、不同通信协议的编解码策略、不同显示驱动策略。

5. **单例模式** — 嵌入式中的单例：硬件外设管理器（每个外设一个实例）。Meyer's Singleton（C++11 保证线程安全）。静态对象初始化顺序问题及对策。替代方案：全局注册表模式。示例：GPIO 管理器、UART 管理器、系统时钟管理器。

6. **工厂模式** — 运行时工厂：基于 ID/字符串的对象创建。编译期工厂：基于 type_list 的类型注册。抽象工厂：设备族创建。嵌入式场景：传感器驱动创建、通信协议对象创建。

7. **命令模式** — 命令对象的设计：封装操作和参数。命令队列：异步执行、优先级排序。撤销/重做在嵌入式中的有限应用。嵌入式场景：串口命令解析器、定时任务调度、OTA 更新命令序列。

8. **代理模式** — 硬件抽象代理：隐藏寄存器操作细节。延迟加载代理：按需初始化硬件。保护代理：访问控制（如防止重复初始化 GPIO）。远程代理：通信接口的本地代理。示例：SPI 设备代理、Flash 存储代理。

9. **模式选择指南** — 决策树：根据场景特征推荐合适的模式。开销汇总表：各模式的 ROM/RAM/CPU 开销对比。反模式警告：嵌入式开发中应避免的模式误用。

## 涉及文件
- documents/embedded/projects/design-patterns/index.md
- documents/embedded/projects/design-patterns/01-overview.md
- documents/embedded/projects/design-patterns/02-state-machine.md
- documents/embedded/projects/design-patterns/03-observer.md
- documents/embedded/projects/design-patterns/04-strategy.md
- documents/embedded/projects/design-patterns/05-singleton.md
- documents/embedded/projects/design-patterns/06-factory.md
- documents/embedded/projects/design-patterns/07-command.md
- documents/embedded/projects/design-patterns/08-proxy.md
- documents/embedded/projects/design-patterns/09-selection-guide.md
- codes/embedded/design-patterns/ (配套代码)

## 参考资料
- 《Design Patterns: Elements of Reusable Object-Oriented Software》— GoF
- 《Modern C++ Design》— Andrei Alexandrescu
- 《Real-Time Design Patterns》— Bruce Powel Douglass
- 《Making Embedded Systems》(2nd Ed) — Elecia White
- 《Practical UML Statecharts in C/C++》— Miro Samek
- CppCon 演讲: "C++ Design Patterns for Embedded" 系列
