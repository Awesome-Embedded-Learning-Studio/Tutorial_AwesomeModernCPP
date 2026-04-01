


## 1. 快速索引

### 1.1 硬件模块对照表

| 硬件模块 | 章节 | 实验产出 | HAL API类型 |
|---------|------|---------|------------|
| 工具链 | 1.1 | 一键编译环境、可下载固件 | arm-none-eabi-gcc, CMake |
| freestanding策略 | 1.2 | 建立freestanding规范 | - |
| 启动文件与链接脚本 | 1.3 | 理解启动流程 | - |
| 最小BSP与板级抽象 | 1.4 | 板级资源管理 | - |
| 时钟树 | 2.1 | 系统时钟配置 | HAL时钟配置API |
| GPIO | 2.2 | LED闪烁、按键读取 | HAL_GPIO初始化/读写 |
| EXTI | 2.3 | 按键中断 | HAL EXTI相关API |
| SysTick | 2.4 | 时间基准 | HAL_GetTick |
| 基础TIM | 3.1 | 周期定时中断 | HAL定时器API |
| PWM | 3.2 | LED呼吸灯、蜂鸣器控制 | HAL PWM API |
| 输入捕获 | 3.3 | 测频器、测脉宽器 | HAL输入捕获API |
| 编码器模式 | 3.4 | 旋钮值读取 | HAL编码器API |
| UART | 4.1 | 串口Echo、日志输出 | HAL_UART收发 |
| DMA | 4.2 | DMA发送/接收/循环缓冲 | HAL DMA API |
| SPI | 4.3 | SPI读写寄存器、设备探测 | HAL SPI API |
| I2C | 4.4 | I2C设备ID读取、传感器寄存器 | HAL I2C API |
| ADC | 5.1 | 电位器采样、电压读取 | HAL ADC API |
| Flash | 6.1 | 参数保存与恢复 | HAL Flash API |
| Watchdog | 6.2 | 看门狗复位演示 | HAL IWDG/WWDG API |
| 低功耗 | 6.3 | 低功耗休眠与唤醒 | HAL_PWR API |
| RTC与Backup | 6.4 | 断电后信息保留 | HAL RTC API |
| 中断模型 | 7.1 | 中断优先级实验 | NVIC相关API |
| C++23驱动封装模式 | 7.2 | 统一驱动风格模板 | - |
| 事件驱动与状态机 | 7.3 | 事件循环、状态机示例 | - |
| 串口命令行CLI | 8.1 | 串口命令控制 | HAL_UART + 自实现 |
| 模拟量采集器 | 8.2 | ADC采样、数据打印 | HAL ADC + 自实现 |
| 参数保存系统 | 8.3 | Flash参数持久化 | HAL Flash + 自实现 |
| 低功耗唤醒演示 | 8.4 | 按键唤醒与休眠 | HAL_PWR + EXTI |

### 1.2 C++23特性索引

| C++23特性 | 首次引入章节 |
|----------|-------------|
| `std::expected` | 1.2 |
| `std::span` | 1.2 |
| `std::array` | 1.2 |
| `std::byte` | 1.2 |
| `std::to_underlying` | 1.2 |
| `constexpr` | 1.3 |
| `consteval` | 2.1 |
| `if consteval` | 2.1 |
| `enum class` | 2.2 |
| `[[nodiscard]]` | 2.2 |
| `std::byteswap` | 4.3 |
| `deducing this` | 7.3 |
| 静态lambda | 7.2 |

---

## 2. 章节总览

### 第1章：工程准备

- 工具链安装与验证
- 交叉编译基础
- CMake 工程骨架
- 启动文件与链接脚本
- HAL/CMSIS 的角色划分
- freestanding 约束说明

### 第2章：时钟与基础I/O

- 时钟树
- GPIO
- EXTI
- SysTick

### 第3章：定时器类外设

- 基础 TIM
- PWM TIM
- 输入捕获 TIM
- 编码器 TIM

### 第4章：通信外设

- UART
- DMA
- SPI
- I2C

### 第5章：模拟与数据采集

- ADC

### 第6章：系统可靠性与存储

- Flash
- Watchdog
- 低功耗
- RTC / Backup

### 第7章：中断与架构

- 中断模型
- C++23 驱动封装模式
- 事件驱动与状态机

### 第8章：综合项目

- 串口命令行
- 传感器采集
- 参数保存
- 低功耗唤醒
- 简易控制台

---

## 3. 第1章：工程准备

### 3.1：工具链与构建系统

### 目标

让读者完成交叉编译环境的基本搭建，并理解"为什么需要独立工具链和构建系统"。

### 内容

- arm-none-eabi-gcc 的作用
- binutils、objdump、size 的作用
- CMake 的交叉编译配置
- VS Code 的任务和调试配置
- 编译产物：`.elf`、`.bin`、`.hex`
- 烧录与下载方式

### 产出

- 能够一键编译
- 能够生成可下载固件
- 能够在板子上跑通最小程序

### C++23 关联

- 无需引入复杂语言特性，重点是建立"可编译的现代 C++ 工程基础"
- 建议开启：`-std=c++23`
- 建议关闭：异常、RTTI（按需）

### 验收

- 工程成功编译
- 输出文件可被下载
- 板子能进入最小可运行状态

---

### 3.2：freestanding 策略

### 目标

告诉读者在裸机环境下，哪些 C++ 用法应当优先，哪些应当谨慎。

### 内容

- freestanding 与 hosted 的区别
- 为什么裸机教程不建议一开始就依赖完整标准库生态
- 固定容量容器优先
- 运行时分配最小化
- 错误处理优先返回值 / `std::expected`

### 建议输出规范

- 主路径不依赖 `new/delete`
- 主路径不依赖异常
- 主路径不依赖 RTTI
- 主路径不依赖 iostream

### C++23 关联

- `std::expected`
- `std::span`
- `std::array`
- `std::byte`
- `std::to_underlying`
- `constexpr` / `consteval` / `if consteval`

### 验收

- 读者能够明确：哪些库在教程中是"默认可用"，哪些是"可选增强"
- 读者能够理解为什么这样设计更适合 STM32F103

---

### 3.3：启动文件与链接脚本

### 目标

让读者知道程序为什么能从 Flash 跑起来，以及启动阶段发生了什么。

### 内容

- 启动文件 `startup.s`
- 向量表
- `Reset_Handler`
- 数据段、BSS 段初始化
- 链接脚本中的 Flash / RAM 布局
- 栈与堆的概念

### C++23 关联

- 无直接语言特性为主，重点是"编译期配置"思想
- 可用 `constexpr` 定义内存布局常量

### 验收

- 读者能解释 `main()` 之前的流程
- 读者知道为什么 `.data` 需要搬运、`.bss` 需要清零
- 读者知道链接脚本为什么不是可有可无

---

### 3.4：最小 BSP 与板级抽象

### 目标

建立"板级差异收口"的基本架构。

### 内容

- `board/` 目录职责
- Blue Pill 的板级资源分配
- LED / 按键 / 串口 / 调试口的命名规范
- 板级初始化入口

### C++23 关联

- `enum class` 用于硬件资源枚举
- `std::to_underlying` 用于枚举索引和表驱动结构
- `[[nodiscard]]` 标记初始化结果

### 验收

- 板级资源定义集中管理
- 上层应用不直接关心 pin 号细节

---

## 4. 第2章：时钟与基础I/O

### 4.1：时钟树

### 目标

让读者理解 STM32F103 的时钟系统如何工作。

### 内容

- HSI / HSE / PLL
- AHB / APB1 / APB2
- 系统主频与外设时钟
- 为什么定时器时钟有"倍频规则"
- HAL 时钟配置入口

### 产出

- 一个稳定的系统时钟配置
- 一个可重复说明的时钟树图

### C++23 关联

- `constexpr` 用于描述时钟参数
- `consteval` 可用于生成固定配置
- `if consteval` 可用于编译期/运行期分支
- `std::to_underlying` 可用于时钟源枚举转换

### 验收

- 读者能说清 `SYSCLK`、`HCLK`、`PCLK1`、`PCLK2`
- 读者知道改频率会影响什么

---

### 4.2：GPIO

### 目标

独立讲清 GPIO 的工作方式，并让读者完成第一条真正的外设驱动。

### 内容

- 输入 / 输出 / 复用 / 模拟模式
- 推挽 / 开漏
- 上拉 / 下拉
- GPIO 速度
- 端口与引脚概念
- HAL GPIO 初始化与读写

### 产出

- LED 闪烁
- 按键读取
- 简单输出控制

### C++23 关联

- 模板参数固定端口与引脚
- `constexpr` pin 配置
- `enum class` 表达模式
- `[[nodiscard]]` 表达初始化结果
- `std::to_underlying` 表达枚举底层值

### 建议封装形式

- `GpioPin<Port, Pin, Mode>`
- `GpioOutput`
- `GpioInput`

### 验收

- 读者能够独立配置一个 GPIO 引脚
- 读者能够解释推挽与开漏的差异
- 读者能够用 GPIO 做 LED 与按键实验

---

### 4.3：EXTI

### 目标

只讲外部中断与事件触发，不混入定时器内容。

### 内容

- EXTI 触发源
- 上升沿 / 下降沿
- 中断与事件的区别
- 按键中断
- 中断里应做什么，不应做什么
- 消抖策略

### 产出

- 按键中断点亮 / 熄灭 LED
- 中断触发日志输出

### C++23 关联

- `std::expected` 用于中断注册、线路绑定结果
- `constexpr` 用于中断线路映射
- 静态 lambda / 无捕获回调用于 ISR 转发

### 验收

- 读者知道 EXTI 的触发条件
- 读者知道中断服务函数和主循环分工
- 读者能独立实现按键中断

---

### 4.4：SysTick

### 目标

只讲系统节拍器与基础时间基准。

### 内容

- SysTick 是什么
- 1ms tick 的建立
- 延时函数的实现思路
- 非阻塞计时的基础
- `HAL_GetTick` 的作用与限制

### 产出

- 固定节拍的时间基准
- 基于 tick 的简单任务调度雏形

### C++23 关联

- `constexpr` 时间常量
- `std::chrono` 可作为"可选增强"而非核心依赖
- `std::expected` 可用于 tick 服务初始化

### 验收

- 读者能使用节拍做周期任务
- 读者能理解阻塞延时与非阻塞延时的差别

---

## 5. 第3章：定时器类外设

### 5.1：基础 TIM

### 目标

只讲通用定时器的最基础用途：计数、定时、中断。

### 内容

- 计数器
- 预分频器
- 自动重装载
- 更新中断
- 定时器启动与停止

### 产出

- 周期定时中断
- 软件节拍器

### C++23 关联

- `constexpr` 计算预分频与重装载
- `std::expected` 返回定时器初始化结果
- `enum class` 表示定时器状态

### 验收

- 读者能配置一个基础定时器
- 读者知道定时器计数和中断如何发生

---

### 5.2：PWM

### 目标

只讲 PWM 输出，不混入输入捕获、编码器等其他模式。

### 内容

- PWM 基本概念
- 占空比
- 频率
- 通道
- 极性
- HAL PWM 输出

### 产出

- LED 呼吸灯
- 蜂鸣器控制
- 简单电机/舵机输出基础

### C++23 关联

- `constexpr` 计算 PWM 参数
- 模板固定通道与频率
- `std::expected` 反馈配置错误

### 验收

- 读者能够让一个通道输出 PWM
- 读者能够调节占空比与频率

---

### 5.3：输入捕获

### 目标

只讲 TIM 输入捕获模式。

### 内容

- 上升沿 / 下降沿捕获
- 计数值读取
- 周期测量
- 频率测量
- 脉宽测量

### 产出

- 测频器
- 测脉宽器

### C++23 关联

- `std::expected` 作为测量结果返回方式
- `std::span` 用于缓存多次测量值
- `constexpr` 用于阈值和换算参数

### 验收

- 读者能够测量一个外部信号周期
- 读者能够解释输入捕获与外部中断的差别

---

### 5.4：编码器模式

### 目标

只讲定时器编码器接口模式。

### 内容

- A/B 相编码器原理
- 计数方向
- 速度与位移计算
- 机械编码器与光电编码器差异

### 产出

- 旋钮值读取
- 计数器随编码器转动变化

### C++23 关联

- `constexpr` 定义编码器换算因子
- `std::expected` 用于初始化结果

### 验收

- 读者能够读取编码器计数
- 读者能够进行基础位置/速度计算

---

## 6. 第4章：通信外设

### 6.1：UART

### 目标

只讲串口基础收发与常见工程用法。

### 内容

- UART 波特率
- 起始位 / 数据位 / 校验位 / 停止位
- 阻塞收发
- 中断收发
- 基本调试输出

### 产出

- 串口 Echo
- 串口日志输出

### C++23 关联

- `std::span` 用于发送缓冲
- `std::expected` 用于发送/接收结果
- `std::byte` 用于原始字节数据
- `std::to_underlying` 用于状态码

### 验收

- 读者能够发送字符串与原始数据
- 读者能够收发调试信息

---

### 6.2：DMA

### 目标

只讲 DMA 的基本搬运能力，不掺入具体外设应用。

### 内容

- DMA 的职责
- 外设到内存、内存到外设、内存到内存
- 传输完成中断
- 半传输中断
- 环形模式
- 双缓冲思路（如果适用）

### 产出

- DMA 发送
- DMA 接收
- DMA 循环缓冲

### C++23 关联

- `std::span` 表示 DMA 缓冲区
- `std::array` 作为固定缓冲区载体
- `std::expected` 表达配置结果

### 验收

- 读者知道 DMA 为什么能减轻 CPU 负担
- 读者能够把一个 DMA 通道跑起来

---

### 6.3：SPI

### 目标

只讲 SPI 通信接口。

### 内容

- SCK / MOSI / MISO / NSS
- 主机 / 从机
- CPOL / CPHA
- HAL SPI 发送接收
- 常见时序问题

### 产出

- SPI 读写寄存器
- SPI 设备探测

### C++23 关联

- `std::span` 表示收发数据块
- `std::expected` 表示设备探测结果
- `std::byteswap` 用于多字节数据处理

### 验收

- 读者能够和一个 SPI 外设完成通信
- 读者能够解释 SPI 模式配置

---

### 6.4：I2C

### 目标

只讲 I2C 通信接口。

### 内容

- SCL / SDA
- 开漏与上拉
- 地址机制
- ACK / NACK
- 起始 / 停止条件
- HAL I2C 读写

### 产出

- 读取 I2C 设备 ID
- 读取传感器寄存器

### C++23 关联

- `std::span` 用于数据缓冲
- `std::expected` 用于通讯结果
- `std::to_underlying` 用于寄存器地址表达

### 验收

- 读者能够完成一次 I2C 读写
- 读者能够解释为什么 I2C 需要上拉

---

## 7. 第5章：模拟与数据采集

### 7.1：ADC

### 目标

只讲 ADC 的采样机制与数据读取。

### 内容

- 模拟输入
- 分辨率
- 采样时间
- 单次转换 / 连续转换
- 通道扫描
- 校准思路

### 产出

- 电位器采样
- 电压读取
- 简单模拟量展示

### C++23 关联

- `std::expected` 返回采样初始化结果
- `std::span` 用于采样缓冲
- `constexpr` 用于量化参数与换算公式

### 验收

- 读者能够读取模拟电压值
- 读者能够解释 ADC 原始值与工程值的关系

---

## 8. 第6章：系统可靠性与存储

### 8.1：Flash

### 目标

只讲片上 Flash 的读取、擦除、写入与参数保存。

### 内容

- Flash 结构
- 页擦除
- 半字/字写入限制
- 参数区设计
- 校验与版本号

### 产出

- 参数保存与恢复
- 重启后参数保留

### C++23 关联

- `std::span` 表示待写数据
- `std::expected` 表示 Flash 操作结果
- `std::array` 作为参数块载体
- `std::byteswap` 用于持久化数据格式处理

### 验收

- 读者能够把配置写进 Flash
- 读者能够在复位后恢复配置

---

### 8.2：Watchdog

### 目标

只讲独立看门狗与窗口看门狗的基础使用。

### 内容

- IWDG
- WWDG
- 喂狗机制
- 故障恢复
- 程序卡死后的自动复位

### 产出

- 看门狗复位演示
- 故障恢复演示

### C++23 关联

- `std::expected` 用于喂狗前的状态检查
- `constexpr` 用于喂狗周期配置

### 验收

- 读者能够解释看门狗用途
- 读者能够演示超时复位

---

### 8.3：低功耗

### 目标

只讲低功耗模式本身，不混入 RTC 细节。

### 内容

- Sleep / Stop / Standby
- 唤醒来源
- 进入与退出低功耗流程
- 功耗与响应速度的权衡

### 产出

- 低功耗休眠演示
- 外部中断唤醒演示

### C++23 关联

- `constexpr` 用于模式配置
- `std::expected` 用于模式切换结果

### 验收

- 读者知道三种低功耗模式的差异
- 读者能够让板子进入并退出低功耗

---

### 8.4：RTC 与 Backup

### 目标

只讲 RTC 与备份域，不混入一般低功耗章节。

### 内容

- RTC 基本用途
- 备份寄存器
- 断电保持数据的设计思路
- 时间戳、唤醒计数、状态恢复

### 产出

- 断电后信息保留
- 简单时间记录

### C++23 关联

- `std::expected` 表达访问结果
- `std::array` 存储备份数据模板
- `constexpr` 定义固定布局

### 验收

- 读者知道备份域可以保存什么
- 读者能够把最小状态保存到备份区

---

## 9. 第7章：中断与架构

### 9.1：中断模型

### 目标

只讲中断机制本身，不讲具体外设。

### 内容

- NVIC
- 中断优先级
- 局部屏蔽与全局屏蔽
- ISR 的职责边界
- 中断安全与临界区

### 产出

- 中断优先级实验
- 简单中断嵌套观察

### C++23 关联

- `constexpr` 定义优先级表
- `std::expected` 统一外设中断注册返回
- 静态回调映射

### 验收

- 读者能够解释为什么 ISR 里不适合做重活
- 读者能够配置基本中断优先级

---

### 9.2：C++23 驱动封装模式

### 目标

把前面学的外设封装成可维护的 C++ 驱动风格。

### 内容

- `enum class` 表达状态与模式
- `std::expected` 表达初始化和运行错误
- `std::span` 表达缓冲区
- `constexpr` 参数驱动
- 模板固定硬件资源
- 无捕获回调适配 ISR
- 轻量 RAII 初始化/释放

### 产出

- 一个统一的驱动风格模板
- 一套可复用的 error/result 约定

### 建议说明的 C++23 特性

- `std::expected`
- `std::span`
- `std::byte`
- `std::to_underlying`
- `constexpr`
- `consteval`
- `if consteval`
- `deducing this`
- 静态 lambda
- `[[nodiscard]]`

### 验收

- 读者能看懂"为什么这样封装更适合嵌入式"
- 读者能基于统一风格实现一个新外设驱动

---

### 9.3：事件驱动与状态机

### 目标

只讲事件驱动模型，不再展开具体外设。

### 内容

- 事件队列
- 状态机
- 生产者/消费者
- 主循环与中断协作
- 非阻塞设计

### 产出

- 一个简单事件循环
- 一个状态机驱动示例

### C++23 关联

- `constexpr` 状态表
- `std::span` 事件缓冲
- `std::expected` 状态转移结果
- `deducing this` 改善对象风格 API

### 验收

- 读者能把"中断只投递事件，主循环负责处理"说清楚
- 读者能写出一个非阻塞状态机

---

## 10. 第8章：综合项目

### 10.1：串口命令行 CLI

### 目标

把 UART、命令解析、状态管理串起来，但仍保持重点是 UART 主线。

### 功能

- 输入命令
- 查看帮助
- 控制 LED
- 调整参数
- 读取状态

### 建议实现

- 固定长度命令行缓冲
- 轻量命令表
- 只用 freestanding 友好的数据结构

### C++23 关联

- `std::span`
- `std::expected`
- `constexpr` 命令表
- `std::to_underlying`

---

### 10.2：模拟量采集器

### 目标

围绕 ADC 构建一个完整采集示例。

### 功能

- ADC 采样
- 周期采样
- 数据打印
- 简单滤波

### C++23 关联

- `std::array`
- `std::span`
- `constexpr`
- `std::expected`

---

### 10.3：参数保存系统

### 目标

围绕 Flash 构建参数持久化能力。

### 功能

- 读取配置
- 修改配置
- 写入 Flash
- 断电恢复
- 配置版本兼容

### C++23 关联

- `std::array`
- `std::span`
- `std::byteswap`
- `std::expected`

---

### 10.4：低功耗唤醒演示

### 目标

围绕低功耗与外部中断做完整案例。

### 功能

- 按键唤醒
- 进入休眠
- 退出后恢复状态

### C++23 关联

- `constexpr`
- `std::expected`

---

## 11. 每章统一模板

为了让整套教程风格一致，建议每个章节都按下面模板写：

## 章节标题

### 1）本章目标

一句话说明这一章要解决什么问题。

### 2）硬件原理

只讲这一章对应 feature 的硬件原理。

### 3）HAL 接口

只讲这一章对应 feature 的 HAL API。

### 4）最小 demo

给出可以直接跑的最小例子。

### 5）C++23 封装

演示如何用现代 C++ 改善接口。

### 6）常见坑

列出最常见的问题。

### 7）练习题

让读者自己扩展。

### 8）可复用代码片段

沉淀成 `drivers/` 中的模块。

### 9）本章小结

总结本章唯一核心 feature。

---

## 12. C++23 feature 对照建议

下面是建议你在教程中逐步引入的 C++23 特性清单。

### 12.1 优先讲解

- `std::expected`
- `std::span`
- `std::array`
- `std::byte`
- `std::to_underlying`
- `constexpr`
- `consteval`
- `if consteval`
- `[[nodiscard]]`
- `enum class`

### 12.2 进阶讲解

- `std::byteswap`
- `deducing this`
- 静态 lambda
- 静态 `operator()`
- `constexpr` 更深入的编译期策略

### 12.3 可选增强

- `std::move_only_function`
- `std::ranges`
- 更复杂的模板元编程
- 更高级的概念约束

---

## 13. freestanding 使用约束建议

### 13.1 默认推荐

- 使用固定容量容器
- 使用静态存储期对象
- 使用编译期配置
- 使用无捕获回调
- 使用显式错误返回

### 13.2 默认谨慎

- `new/delete`
- `exception`
- `RTTI`
- `iostream`
- 过度依赖动态分配的容器与算法

### 13.3 可以作为可选扩展

- 某些 STL 算法
- 某些轻量 `<utility>` 能力
- 某些工具库能力

前提是它们不会破坏你教程的"可移植、可解释、可教学"目标。

---

## 14. 推荐的知识递进顺序

1. 工具链与最小工程
2. 启动文件与链接脚本
3. 时钟树
4. GPIO
5. EXTI
6. SysTick
7. 基础 TIM
8. PWM
9. 输入捕获
10. 编码器
11. UART
12. DMA
13. SPI
14. I2C
15. ADC
16. Flash
17. Watchdog
18. 低功耗
19. RTC / Backup
20. 中断模型
21. C++23 封装模式
22. 综合项目

---

## 15. 附录

### 15.1 附录A：详细硬件模块对照表

| 硬件模块 | 章节 | HAL API类型 | 实验产出 |
|---------|------|------------|---------|
| 工具链 | 1.1 | arm-none-eabi-gcc, CMake | 一键编译环境、可下载固件、最小程序运行 |
| freestanding策略 | 1.2 | - | 建立freestanding编码规范 |
| 启动文件与链接脚本 | 1.3 | - | 理解启动流程、数据段搬运、BSS清零 |
| 最小BSP与板级抽象 | 1.4 | - | 板级资源集中管理、LED/按键/串口命名规范 |
| 时钟树 | 2.1 | HAL时钟配置API | 系统时钟配置、时钟树图 |
| GPIO | 2.2 | HAL_GPIO初始化/读写 | LED闪烁、按键读取、简单输出控制 |
| EXTI | 2.3 | HAL EXTI相关API | 按键中断点亮/熄灭LED、中断日志输出 |
| SysTick | 2.4 | HAL_GetTick | 固定节拍时间基准、简单任务调度雏形 |
| 基础TIM | 3.1 | HAL定时器API | 周期定时中断、软件节拍器 |
| PWM | 3.2 | HAL PWM API | LED呼吸灯、蜂鸣器控制、简单电机/舵机输出 |
| 输入捕获 | 3.3 | HAL输入捕获API | 测频器、测脉宽器 |
| 编码器模式 | 3.4 | HAL编码器API | 旋钮值读取、计数器随转动变化 |
| UART | 4.1 | HAL_UART收发 | 串口Echo、串口日志输出 |
| DMA | 4.2 | HAL DMA API | DMA发送、DMA接收、DMA循环缓冲 |
| SPI | 4.3 | HAL SPI API | SPI读写寄存器、SPI设备探测 |
| I2C | 4.4 | HAL I2C API | I2C设备ID读取、传感器寄存器读取 |
| ADC | 5.1 | HAL ADC API | 电位器采样、电压读取、模拟量展示 |
| Flash | 6.1 | HAL Flash API | 参数保存与恢复、重启后参数保留 |
| Watchdog | 6.2 | HAL IWDG/WWDG API | 看门狗复位演示、故障恢复演示 |
| 低功耗 | 6.3 | HAL_PWR API | 低功耗休眠演示、外部中断唤醒 |
| RTC与Backup | 6.4 | HAL RTC API | 断电后信息保留、简单时间记录 |
| 中断模型 | 7.1 | NVIC相关API | 中断优先级实验、中断嵌套观察 |
| C++23驱动封装模式 | 7.2 | - | 统一驱动风格模板、error/result约定 |
| 事件驱动与状态机 | 7.3 | - | 事件循环、状态机驱动示例 |
| 串口命令行CLI | 8.1 | HAL_UART + 自实现 | 命令输入、帮助、LED控制、参数调整、状态读取 |
| 模拟量采集器 | 8.2 | HAL ADC + 自实现 | ADC采样、周期采样、数据打印、简单滤波 |
| 参数保存系统 | 8.3 | HAL Flash + 自实现 | 配置读写、Flash写入、断电恢复、版本兼容 |
| 低功耗唤醒演示 | 8.4 | HAL_PWR + EXTI | 按键唤醒、休眠进入、状态恢复 |

### 15.2 附录B：C++23特性章节对照

#### 3. 第1章：工程准备
- **1.2 freestanding策略**：`std::expected`, `std::span`, `std::array`, `std::byte`, `std::to_underlying`, `constexpr`, `consteval`, `if consteval`
- **1.3 启动文件与链接脚本**：`constexpr`
- **1.4 最小BSP与板级抽象**：`enum class`, `std::to_underlying`, `[[nodiscard]]`

#### 4. 第2章：时钟与基础I/O
- **2.1 时钟树**：`constexpr`, `consteval`, `if consteval`, `std::to_underlying`
- **2.2 GPIO**：模板参数, `constexpr`, `enum class`, `[[nodiscard]]`, `std::to_underlying`
- **2.3 EXTI**：`std::expected`, `constexpr`, 静态lambda
- **2.4 SysTick**：`constexpr`, `std::expected`

#### 5. 第3章：定时器类外设
- **3.1 基础TIM**：`constexpr`, `std::expected`, `enum class`
- **3.2 PWM**：`constexpr`, 模板参数, `std::expected`
- **3.3 输入捕获**：`std::expected`, `std::span`, `constexpr`
- **3.4 编码器模式**：`constexpr`, `std::expected`

#### 6. 第4章：通信外设
- **4.1 UART**：`std::span`, `std::expected`, `std::byte`, `std::to_underlying`
- **4.2 DMA**：`std::span`, `std::array`, `std::expected`
- **4.3 SPI**：`std::span`, `std::expected`, `std::byteswap`
- **4.4 I2C**：`std::span`, `std::expected`, `std::to_underlying`

#### 7. 第5章：模拟与数据采集
- **5.1 ADC**：`std::expected`, `std::span`, `constexpr`

#### 8. 第6章：系统可靠性与存储
- **6.1 Flash**：`std::span`, `std::expected`, `std::array`, `std::byteswap`
- **6.2 Watchdog**：`std::expected`, `constexpr`
- **6.3 低功耗**：`constexpr`, `std::expected`
- **6.4 RTC与Backup**：`std::expected`, `std::array`, `constexpr`

#### 9. 第7章：中断与架构
- **7.1 中断模型**：`constexpr`, `std::expected`, 静态回调映射
- **7.2 C++23驱动封装模式**：`enum class`, `std::expected`, `std::span`, `std::byte`, `std::to_underlying`, `constexpr`, `consteval`, `if consteval`, `deducing this`, 静态lambda, `[[nodiscard]]`
- **7.3 事件驱动与状态机**：`constexpr`, `std::span`, `std::expected`, `deducing this`

#### 10. 第8章：综合项目
- **8.1 串口命令行CLI**：`std::span`, `std::expected`, `constexpr`, `std::to_underlying`
- **8.2 模拟量采集器**：`std::array`, `std::span`, `constexpr`, `std::expected`
- **8.3 参数保存系统**：`std::array`, `std::span`, `std::byteswap`, `std::expected`
- **8.4 低功耗唤醒演示**：`constexpr`, `std::expected`
