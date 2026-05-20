---
id: 023
title: "自实现协作式调度器教程（从零实现基于 SysTick 的 RTOS 调度器）"
category: content
priority: P1
status: pending
created: 2026-04-15
assignee: charliechen
depends_on:
  - "architecture/002"
blocks: []
estimated_effort: large
---

# 自实现协作式调度器教程

## 目标
从零实现一个基于 SysTick 的协作式调度器，作为 RTOS 篇章的第一篇。教学内容包括：任务控制块（TCB）结构体设计、就绪队列管理、上下文保存与恢复（PendSV 汇编实现）、简单的 Round-Robin 时间片轮转调度算法。通过手写调度器，帮助读者深入理解 RTOS 的任务调度原理，为后续学习 FreeRTOS/RT-Thread 等商业 RTOS 打下坚实基础。

## 验收标准
- [ ] 教程遵循 9 节模板：目标 -> 原理 -> 架构设计 -> 核心实现 -> 集成 Demo -> 常见坑 -> 练习题 -> 可复用代码 -> 小结
- [ ] 原理部分详细解释协作式调度 vs 抢占式调度的区别
- [ ] 任务控制块（TCB）结构体设计：栈指针、栈底、栈大小、任务函数、优先级、状态
- [ ] 就绪队列管理：任务注册、查找下一个任务、任务状态切换
- [ ] 上下文保存与恢复的完整汇编实现（PendSV handler）：
  - [ ] 保存 R4-R11 到当前任务栈（R0-R3, R12, LR, PC, xPSR 由硬件自动保存）
  - [ ] 更新当前 TCB 的栈指针
  - [ ] 切换到下一个任务的栈指针
  - [ ] 恢复 R4-R11 从新任务栈
  - [ ] 异常返回恢复 R0-R3, R12, LR, PC, xPSR
- [ ] SysTick 配置为固定时间片（如 1ms），触发 PendSV 执行调度
- [ ] 提供 Round-Robin 调度算法的 C++ 实现
- [ ] Demo 包含：2-3 个并发任务（LED 闪烁 + UART 输出 + 计数器），验证调度正确性
- [ ] 常见坑涵盖：栈溢出检测、PendSV 优先级必须最低、任务函数不能返回
- [ ] 练习题包含：增加任务优先级支持、实现简单的互斥量、添加任务延迟函数
- [ ] 所有代码在 STM32F103C8T6 上验证

## 实施说明
### 9 节模板详细规划

**第 1 节：目标**
- 理解多任务调度的基本原理
- 掌握上下文切换的底层机制
- 从零实现一个可工作的协作式调度器
- 为后续 RTOS 学习建立正确的概念模型

**第 2 节：原理**
- **协作式 vs 抢占式调度**：
  - 协作式：任务主动让出 CPU（yield），调度器在任务切换点进行调度
  - 抢占式：调度器通过定时器中断强制切换任务，任务无法拒绝
  - 本教程实现的是定时器触发的协作式调度（介于两者之间）
- **ARM Cortex-M3 异常模型**：
  - 异常进入：自动保存 xPSR, PC, LR, R12, R3-R0 到 PSP 栈
  - 异常退出：自动恢复上述寄存器，返回线程模式
  - PendSV：可挂起的系统服务中断，专门用于上下文切换，优先级设为最低
  - MSP vs PSP：主栈（ISR 和启动用） vs 进程栈（任务用）
- **上下文切换原理**：
  - 每个任务有独立的栈空间，上下文保存在自己的栈上
  - 切换 = 保存当前任务的 CPU 状态到其栈 + 恢复下一个任务的 CPU 状态从其栈
  - TCB（Task Control Block）记录每个任务的栈指针，是调度的核心数据结构

**第 3 节：架构设计**

- **TaskState 枚举**：
  ```cpp
  enum class TaskState : uint8_t {
      Ready,      // 就绪，等待调度
      Running,    // 正在运行
      Blocked,    // 被阻塞（等待事件）
      Suspended,  // 被挂起
      Terminated  // 已终止
  };
  ```

- **TaskControlBlock 结构体**：
  ```cpp
  struct TaskControlBlock {
      volatile uint32_t* stack_ptr;          // 当前栈指针（必须第一个成员）
      uint32_t* stack_bottom;                 // 栈底地址
      size_t stack_size;                      // 栈大小（字节）
      TaskFunction function;                  // 任务函数
      void* parameter;                        // 任务参数
      TaskState state;                        // 任务状态
      uint32_t priority;                      // 优先级（可选）
      const char* name;                       // 任务名称（调试用）
      uint32_t runtime_ticks;                 // 运行时间统计
  };
  ```

- **Scheduler 类**：
  ```cpp
  class Scheduler {
      static inline std::array<TaskControlBlock, MAX_TASKS> tasks_{};
      static inline size_t task_count_{0};
      static inline size_t current_task_{0};
      static inline volatile bool context_switch_pending_{false};
  public:
      static auto create_task(TaskFunction fn, void* param,
                              std::span<uint32_t> stack,
                              const char* name = "anon") -> std::expected<TaskId, SchedError>;
      static auto start() -> void;  // 启动调度器，永不返回
      static auto yield() -> void;  // 主动让出 CPU
      static auto get_current_task() -> TaskId;
      static auto get_task_count() -> size_t;
      static auto tick() -> void;   // SysTick 调用
  private:
      static auto schedule() -> TaskControlBlock*;  // 选择下一个任务
      static auto context_switch() -> void;          // PendSV 触发
      static auto initialize_stack(std::span<uint32_t> stack,
                                    TaskFunction fn, void* param) -> uint32_t*;
  };
  ```

- **初始化栈帧**：
  ```
  高地址
  | xPSR   = 0x01000000 (Thumb 模式)  |
  | PC     = task_function             |
  | LR     = task_exit_handler         |  // 任务函数返回时的处理
  | R12    = 0                         |
  | R3     = 0                         |
  | R2     = 0                         |
  | R1     = task_parameter            |
  | R0     = 0                         |
  | R11    = 0 (saved by PendSV)       |
  | R10    = 0                         |
  | R9     = 0                         |
  | R8     = 0                         |
  | R7     = 0                         |
  | R6     = 0                         |
  | R5     = 0                         |
  | R4     = 0                         |  <-- 初始栈指针指向这里
  低地址
  ```

**第 4 节：核心实现**

1. **SysTick_Handler**（C++）：
   ```cpp
   extern "C" void SysTick_Handler() {
       Scheduler::tick();  // 更新时间片计数，触发调度
   }
   ```

2. **PendSV_Handler**（汇编，核心中的核心）：
   ```asm
   PendSV_Handler:
       CPSID   I                   ; 关中断
       PUSH    {R4-R11}            ; 保存当前任务的 R4-R11
       LDR     R0, =current_tcb    ; 加载当前 TCB 地址
       LDR     R1, [R0]            ; 加载当前 TCB 指针
       STR     SP, [R1]            ; 保存 SP 到当前 TCB

       ; 调度：选择下一个任务
       BL      Scheduler::schedule ; 返回下一个 TCB 指针在 R0
       LDR     R1, =current_tcb
       STR     R0, [R1]            ; 更新当前 TCB
       LDR     SP, [R0]            ; 恢复 SP 从新 TCB

       POP     {R4-R11}            ; 恢复新任务的 R4-R11
       CPSIE   I                   ; 开中断
       BX      LR                  ; 异常返回，恢复 R0-R3,PC,LR 等
   ```

3. **Scheduler::schedule**（C++）：
   ```cpp
   auto Scheduler::schedule() -> TaskControlBlock* {
       // Round-Robin: 找到下一个 Ready 状态的任务
       size_t next = (current_task_ + 1) % task_count_;
       for (size_t i = 0; i < task_count_; ++i) {
           if (tasks_[next].state == TaskState::Ready) {
               tasks_[current_task_].state = TaskState::Ready;
               tasks_[next].state = TaskState::Running;
               current_task_ = next;
               return &tasks_[next];
           }
           next = (next + 1) % task_count_;
       }
       return &tasks_[current_task_];  // 没有其他就绪任务，继续运行当前
   }
   ```

4. **Scheduler::start**（C++）：
   ```cpp
   auto Scheduler::start() -> void {
       // 配置 SysTick（1ms 时间片）
       SysTick_Config(SystemCoreClock / 1000);
       // 配置 PendSV 为最低优先级
       HAL_NVIC_SetPriority(PendSV_IRQn, 0xFF, 0);
       // 切换到 PSP 并启动第一个任务
       // ... (汇编或内联汇编)
   }
   ```

**第 5 节：集成 Demo**

- Demo 1：双任务 LED 交替闪烁（Task1: LED1 亮 500ms / 灭 500ms，Task2: LED2 亮 300ms / 灭 700ms）
- Demo 2：三任务并发（Task1: LED 闪烁，Task2: UART 周期输出 "Tick N"，Task3: 按键检测）
- Demo 3：任务间协作（生产者任务往队列写数据，消费者任务从队列读数据并通过 UART 输出）

**第 6 节：常见坑**

- **PendSV 优先级必须最低**：否则会阻塞其他中断，导致系统异常
- **任务函数不能返回**：任务函数应该是无限循环，返回会导致 HardFault（或提供 exit handler）
- **栈大小必须足够**：每个任务独立栈，需考虑局部变量和中断嵌套的栈需求（建议至少 256 字）
- **MSP vs PSP 混淆**：ISR 使用 MSP，任务使用 PSP，启动第一个任务时需要从 MSP 切换到 PSP
- **volatile 关键字**：TCB 的 stack_ptr 必须是 volatile，防止编译器优化掉对它的读写
- **启动第一个任务的特殊处理**：第一次上下文切换没有"当前任务"需要保存，需要特殊处理
- **栈对齐**：初始栈帧必须 8 字节对齐（ARM AAPCS 要求）
- **调试困难**：上下文切换导致单步调试混乱，建议使用 GPIO 翻转和逻辑分析仪调试

**第 7 节：练习题**

- 基础：修改时间片大小（从 1ms 改为 10ms），观察任务行为变化
- 进阶：实现 `task_delay(uint32_t ms)` 函数（将任务设为 Blocked 状态，由 SysTick 唤醒）
- 进阶：为 TCB 增加优先级字段，实现优先级调度（高优先级任务优先运行）
- 挑战：实现简单的二值信号量（Binary Semaphore），用于任务间同步

**第 8 节：可复用代码**
- `scheduler.hpp` / `scheduler.cpp`：完整的 Scheduler 类
- `pendsv_handler.s`：PendSV 汇编处理函数
- `task.hpp`：任务相关类型定义
- `stack.hpp`：栈初始化工具函数
- `scheduler_config.hpp`：编译期配置（最大任务数、时间片大小、默认栈大小）

**第 9 节：小结**
- 回顾调度器的核心组件：TCB + 就绪队列 + 上下文切换 + 调度算法
- 本实现 vs FreeRTOS 的对比：缺少什么（信号量、互斥量、消息队列、内存管理、时间管理）
- 下一步学习路径：FreeRTOS / RT-Thread 源码阅读指南
- 强调：理解原理比使用 API 更重要

## 涉及文件
- documents/embedded/rtos/01-scratch-scheduler/index.md
- code/examples/embedded-rtos/01-scratch-scheduler/
- code/examples/embedded-rtos/01-scratch-scheduler/Core/Src/main.cpp
- code/examples/embedded-rtos/01-scratch-scheduler/app/scheduler.hpp
- code/examples/embedded-rtos/01-scratch-scheduler/app/scheduler.cpp
- code/examples/embedded-rtos/01-scratch-scheduler/app/task.hpp
- code/examples/embedded-rtos/01-scratch-scheduler/app/pendsv_handler.s
- code/examples/embedded-rtos/01-scratch-scheduler/app/stack.hpp
- code/examples/embedded-rtos/01-scratch-scheduler/app/scheduler_config.hpp
- code/examples/embedded-rtos/01-scratch-scheduler/demo/dual_task_blink.cpp
- code/examples/embedded-rtos/01-scratch-scheduler/demo/three_task_demo.cpp
- code/examples/embedded-rtos/01-scratch-scheduler/demo/producer_consumer.cpp

## 参考资料
- ARM Cortex-M3 Technical Reference Manual: Exception Model, Stack Pointer
- STM32F1 Reference Manual (RM0008) Chapter 8: NVIC
- "Making Embedded Systems" by Elecia White - Task Scheduling Chapter
- FreeRTOS 任务调度器源码（tasks.c）作为参考对比
- RT-Thread 调度器源码（scheduler.c）作为参考对比
- ARM AAPCS (Procedure Call Standard for the ARM Architecture)
