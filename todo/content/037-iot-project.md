---
id: 037
title: "IoT 网关综合项目"
category: content
priority: P3
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["021", "025", "031", "032"]
blocks: []
estimated_effort: epic
---

# IoT 网关综合项目（远期规划）

## 目标
设计并实现一个 IoT 网关综合项目，作为嵌入式教程体系的终极综合实践。项目结合多传感器采集、屏幕显示、远程通信于一体，涉及 RTOS 多任务调度、网络协议栈、文件系统等多个技术领域。读者将综合运用前面所有教程中学到的知识和技能。

## 验收标准
- [ ] 项目需求文档和系统架构设计完成
- [ ] 硬件选型文档（传感器/显示器/通信模块）
- [ ] RTOS 多任务架构设计文档
- [ ] 传感器采集任务实现（温度/湿度/光照等）
- [ ] 显示任务实现（OLED/LCD 数据展示）
- [ ] 网络通信任务实现（MQTT/HTTP 数据上报）
- [ ] 完整的 TDD 开发流程演示
- [ ] 设计模式应用说明
- [ ] 系统性能分析报告（CPU/内存/功耗）
- [ ] 项目构建和烧录指南

## 实施说明
本项目是嵌入式教程体系的综合实践篇，读者需要运用 RTOS (025)、设计模式 (031)、TDD (032) 等前置知识。同时需要使用到传感器教程 (021) 的硬件驱动基础。

**项目规划：**

**系统架构：**
```
传感器层 -> 采集任务 -> 数据处理任务 -> [显示任务 / 通信任务]
                                    \-> 存储任务
```

**功能模块：**

1. **多传感器采集** — 温湿度传感器（DHT22/SHT30）、光照传感器（BH1750）、气压传感器（BMP280）。I2C/SPI/单总线多协议并存。RTOS 任务封装每个传感器的采集周期。

2. **数据显示** — OLED (SSD1306) 或 TFT LCD 显示。仪表盘式数据展示。实时曲线图。状态指示（网络连接/告警）。使用 SPI/I2C 驱动屏幕。

3. **远程通信** — Wi-Fi (ESP8266 AT 指令) 或 Ethernet (W5500 SPI)。MQTT 协议上报传感器数据到云平台。远程控制命令接收（LED/继电器控制）。JSON 数据格式封装。

4. **本地存储** — Flash 或 SD 卡存储历史数据。简单的文件系统（LittleFS/FatFS）。数据记录和回读。

5. **系统管理** — RTOS 任务监控和看门狗。系统日志记录。OTA 升级支持（进阶）。

**教程内容结构：**

1. **项目概述与需求分析** — IoT 网关的概念和应用场景。功能需求列表。非功能需求（实时性、可靠性、功耗）。硬件选型和 BOM。

2. **系统架构设计** — 整体架构图。RTOS 任务划分和优先级设计。任务间通信方案（队列/信号量/事件）。数据流图。错误处理策略。

3. **开发环境搭建** — 项目 CMake 结构设计。测试环境配置（延续 TDD 流程）。CI 流水线配置。

4. **硬件驱动层开发（TDD）** — 使用 TDD 流程开发每个传感器驱动。Host-based 测试策略。HAL 抽象层设计。

5. **RTOS 任务实现** — 各任务的实现细节。任务同步和通信的代码实现。内存管理策略（静态分配为主）。

6. **网络通信实现** — 通信协议栈选择和集成。MQTT 客户端实现。数据格式和协议设计。

7. **系统集成与测试** — 集成测试策略。系统级测试。性能分析和优化。长期稳定性测试。

8. **项目总结** — 回顾使用的技术和设计决策。项目扩展方向。从原型到产品的差距分析。

**设计模式应用：**
- 状态机：系统状态管理（初始化/正常/告警/错误）
- 观察者：传感器数据分发
- 策略：不同传感器的数据处理策略
- 命令：远程控制命令
- 工厂：传感器驱动创建
- 代理：硬件抽象

## 涉及文件
- documents/embedded/projects/iot-gateway/index.md
- documents/embedded/projects/iot-gateway/01-requirements.md
- documents/embedded/projects/iot-gateway/02-architecture.md
- documents/embedded/projects/iot-gateway/03-setup.md
- documents/embedded/projects/iot-gateway/04-driver-tdd.md
- documents/embedded/projects/iot-gateway/05-rtos-tasks.md
- documents/embedded/projects/iot-gateway/06-network.md
- documents/embedded/projects/iot-gateway/07-integration.md
- documents/embedded/projects/iot-gateway/08-summary.md
- codes/embedded/projects/iot-gateway/ (完整项目代码)

## 参考资料
- FreeRTOS 官方示例项目
- MQTT 协议规范 (mqtt.org)
- LittleFS 文件系统 (ARMmbed/littlefs)
- ESP8266 AT 指令集
- W5500 数据手册和 ioLibrary
- 《Designing Embedded Systems and the Internet of Things》— Perry Lea
- AWS IoT / Aliyun IoT 设备接入文档（作为云平台参考）
