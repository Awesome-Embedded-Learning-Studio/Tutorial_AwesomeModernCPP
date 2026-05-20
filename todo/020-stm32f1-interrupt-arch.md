---
id: 020
title: "STM32F1 中断架构与 C++23 驱动封装模式 + 事件驱动架构"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
  - "012"
  - "013"
  - "014"
  - "015"
  - "016"
  - "017"
  - "018"
  - "019"
blocks: []
estimated_effort: large
---

# STM32F1 中断架构与 C++23 驱动封装模式 + 事件驱动架构

## 目标
编写架构层面的收束章节，整合三大主题：(1) 中断架构教程覆盖 NVIC 中断控制器、中断优先级分组与配置、局部/全局中断屏蔽、ISR 职责边界设计、中断安全编程、临界区保护；(2) C++23 驱动封装模式系统总结 enum class 强类型寄存器、std::expected 统一错误处理、std::span 安全缓冲区、constexpr 硬件配置、模板固定硬件资源映射、无捕获回调桥接、轻量 RAII 资源管理；(3) 事件驱动与状态机架构覆盖事件队列设计、状态机模式、生产者-消费者模型、主循环与中断的协作模式、非阻塞设计原则。本章节是前面所有外设教程（012-019）的架构总结与升华。

## 验收标准
- [ ] 教程分为三大板块（中断架构、C++23 封装模式、事件驱动架构），各自独立但逻辑连贯
- [ ] 中断架构部分：
  - [ ] NVIC 详解：中断向量表、优先级分组（Group/SubPriority 的 4 种分割方式）、优先级配置函数
  - [ ] 中断屏蔽：`__disable_irq()` / `__enable_irq()`、`__set_BASEPRI`、NVIC 局部使能/禁用
  - [ ] ISR 设计原则：短小精悍、无阻塞调用、只设置标志/触发事件
  - [ ] 中断安全：可重入函数、volatile 关键字正确使用、原子操作
  - [ ] 临界区保护：中断屏蔽 vs BASEPRI vs LDREX/STREX（互斥场景）
- [ ] C++23 驱动封装模式部分：
  - [ ] 总结前面所有外设教程中使用的 C++23 模式，形成统一的设计规范
  - [ ] enum class 强类型寄存器映射模式（替代 #define 和裸整数）
  - [ ] std::expected<T, E> 统一错误处理模式（替代 HAL_OK/ErrorCallback）
  - [ ] std::span 安全缓冲区传递模式（替代 裸指针+长度）
  - [ ] constexpr 硬件配置表模式（编译期确定参数，零运行时开销）
  - [ ] 模板固定硬件资源映射（TIM1 的通道不能配置到 TIM2 的引脚，编译期约束）
  - [ ] 无捕获回调桥接模式（C++ 回调到 C HAL 回调的桥接技术）
  - [ ] 轻量 RAII 模式（CsGuard、CriticalSectionGuard 等资源管理）
- [ ] 事件驱动架构部分：
  - [ ] 事件队列设计：环形缓冲区 + 事件类型 + 事件数据
  - [ ] 状态机模式：enum class State + 状态转移表 + 事件驱动的状态处理
  - [ ] 生产者-消费者模型：中断生产事件、主循环消费事件
  - [ ] 非阻塞设计原则：避免轮询、使用回调/事件通知
  - [ ] 主循环架构：`while(1) { process_events(); idle_task(); }` 的标准模式
- [ ] 提供完整的综合示例：事件驱动 + 状态机 + C++23 封装的 UART 命令处理器
- [ ] 所有代码在 STM32F103C8T6 上验证

## 实施说明
### 教程结构（三大板块）

**板块一：中断架构（interrupt/）**

1. **NVIC 深入理解**
   - 中断向量表结构（STM32F1 起始地址 0x00000000，可重定向到 0x08000000）
   - 优先级分组：`NVIC_PriorityGroupConfig` 的 4 种模式
     - Group 0: 0 位抢占 + 4 位子优先级
     - Group 1: 1 位抢占 + 3 位子优先级
     - Group 2: 2 位抢占 + 2 位子优先级（推荐）
     - Group 3: 3 位抢占 + 1 位子优先级
     - Group 4: 4 位抢占 + 0 位子优先级
   - 抢占优先级（Preemption）：高优先级可以打断低优先级
   - 子优先级（SubPriority）：同级等待排队顺序
   - 中断优先级设置：`HAL_NVIC_SetPriority` + `HAL_NVIC_EnableIRQ`

2. **中断屏蔽策略**
   - 全局屏蔽：`__disable_irq()` / `__enable_irq()`（配对使用，注意嵌套）
   - BASEPRI 屏蔽：`__set_BASEPRI(threshold << 4)`（屏蔽低于阈值的中断）
   - NVIC 局部控制：`HAL_NVIC_DisableIRQ` / `HAL_NVIC_EnableIRQ`（单个中断）
   - 各策略的适用场景与性能影响

3. **ISR 设计原则与中断安全**
   - ISR 职责边界：只做"最少的必要工作"——清除中断标志、读取数据到缓冲区、设置事件标志
   - 禁止在 ISR 中调用：HAL_Delay、printf、malloc、阻塞式 API
   - volatile 关键字的正确使用：ISR 与主循环共享的变量必须 volatile
   - 编译器屏障：`__asm volatile("" ::: "memory")`
   - 原子操作：C++11 std::atomic 或 CMSIS `__atomic` 内建函数

**板块二：C++23 驱动封装模式（driver-pattern/）**

1. **类型安全设计模式总结**
   ```cpp
   // 模式 1：enum class 强类型寄存器
   enum class UartBaudRate : uint32_t { B9600 = 9600, B115200 = 115200 };
   enum class SpiMode { Mode0, Mode1, Mode2, Mode3 };
   // 编译期禁止混用不同 enum class 的值

   // 模式 2：std::expected 统一错误处理
   auto result = uart.send(data);  // std::expected<void, UartError>
   if (!result) { handle_error(result.error()); }

   // 模式 3：std::span 安全缓冲区
   auto send(std::span<const std::byte> data) -> std::expected<size_t, Error>;
   // 长度自动从数组推导，不会传错长度

   // 模式 4：constexpr 硬件配置表
   struct UartConfig {
       USART_TypeDef* instance;
       uint32_t baud_rate;
       constexpr UartConfig(USART_TypeDef* inst, uint32_t br) : instance(inst), baud_rate(br) {}
   };
   static constexpr UartConfig uart1_config{USART1, 115200};

   // 模式 5：模板固定硬件资源
   template<typename TimerInstance>
   struct TimerTraits;  // 特化定义每个定时器的通道数、引脚映射等
   ```

2. **回调桥接技术**
   - C HAL 回调（弱函数覆盖）到 C++ 成员函数的桥接
   - 方案 A：全局/静态函数 + 全局驱动指针
   - 方案 B：CRTP 模式静态分发
   - 方案 C：无捕获 lambda 转函数指针（C++23 改进）
   - 各方案的优缺点对比

3. **RAII 资源管理模式**
   - `CriticalSectionGuard`：构造时进入临界区，析构时退出（即使异常安全）
   - `CsGuard`：SPI 片选 RAII 管理
   - `DmaTransaction`：DMA 传输的 RAII 生命周期管理

**板块三：事件驱动架构（event-driven/）**

1. **事件系统设计**
   ```cpp
   enum class EventType : uint8_t {
       UartDataReady, UartLineReceived,
       AdcConversionComplete,
       TimerExpired,
       ButtonPressed, ButtonReleased,
       SpiTransferComplete,
       I2cTransferComplete,
   };

   struct Event {
       EventType type;
       uint8_t data[8];  // 小对象优化，内联存储事件数据
       uint32_t timestamp;
   };

   template<size_t N>
   class EventQueue {
       RingBuffer<Event, N> buffer_;
   public:
       auto push(const Event& e) -> bool;  // 从 ISR 调用
       auto pop(Event& e) -> bool;          // 从主循环调用
       auto empty() const -> bool;
   };
   ```

2. **状态机模式**
   ```cpp
   enum class SystemState { Idle, Receiving, Processing, Transmitting, Error };

   class StateMachine {
       SystemState current_state_{SystemState::Idle};
   public:
       auto handle_event(const Event& e) -> void;
       auto get_state() const -> SystemState { return current_state_; }
   private:
       using Handler = void(StateMachine::*)(const Event&);
       static constexpr Handler state_handlers[] = {
           &StateMachine::on_idle,
           &StateMachine::on_receiving,
           &StateMachine::on_processing,
           &StateMachine::on_transmitting,
           &StateMachine::on_error,
       };
   };
   ```

3. **主循环架构**
   ```cpp
   int main() {
       // 硬件初始化
       SystemInit();
       // 主循环
       while (true) {
           Event e;
           while (event_queue.pop(e)) {
               state_machine.handle_event(e);
           }
           idle_task();  // 低优先级后台任务
           __WFI();      // 无事件时进入 Sleep 节能
       }
   }
   ```

## 涉及文件
- documents/embedded/platforms/stm32f1/14-interrupt/index.md
- documents/embedded/platforms/stm32f1/15-driver-pattern/index.md
- documents/embedded/platforms/stm32f1/16-event-driven/index.md
- code/platforms/stm32f1/14-interrupt/
- code/platforms/stm32f1/15-driver-pattern/
- code/platforms/stm32f1/15-driver-pattern/enum_class_registers.hpp
- code/platforms/stm32f1/15-driver-pattern/expected_error_handling.hpp
- code/platforms/stm32f1/15-driver-pattern/span_buffer_pattern.hpp
- code/platforms/stm32f1/15-driver-pattern/constexpr_config.hpp
- code/platforms/stm32f1/15-driver-pattern/callback_bridge.hpp
- code/platforms/stm32f1/15-driver-pattern/raii_guards.hpp
- code/platforms/stm32f1/16-event-driven/
- code/platforms/stm32f1/16-event-driven/event_queue.hpp
- code/platforms/stm32f1/16-event-driven/state_machine.hpp
- code/platforms/stm32f1/16-event-driven/main_loop_demo.cpp

## 参考资料
- drafts/Conten-STM32F103C8T6-Draft.md sections 9.1-9.3（中断/封装/事件驱动草稿内容）
- STM32F1 Reference Manual (RM0008) Chapter 8: NVIC / Chapter 9: EXTI
- ARM Cortex-M3 Technical Reference Manual: Exception Model
- C++23 标准：std::expected, std::span, constexpr, enum class
- "Making C++ Work for Embedded" - Various C++ embedded best practices
