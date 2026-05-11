# Embedded Resource and Real-Time Constraints

## 1. Introduction: Why We Can't Just "Write Whatever" in Embedded

In PC or server development, we are used to a "default" premise: if memory is insufficient, we can add more; if compute power is lacking, we can scale up; if system scheduling gets messy, the OS has our back. The goal of a program often just needs to satisfy "functional correctness + acceptable average performance."

But embedded systems do not live in this world. In an embedded environment, resources are strictly quantified: Flash might be only a few dozen KB, RAM only a few KB, and the CPU clock frequency only a few dozen MHz, yet the system bears responsibilities like real-time control, device safety, and industrial or consumer-grade reliability. Here, a program is not enough just because it "runs." It must also:

- Complete tasks within a specified time
- Behave correctly even in the worst-case scenario
- Maintain long-term stable operation under limited resources

The essence of embedded engineering is pursuing deterministic system behavior in a resource-constrained world. Of course, this conflicts somewhat with C++'s tendency to hide things under the hood, but using most C++ features properly can indeed drive significant performance improvements.

## 2. Flash / ROM Constraints: Code Is Not "Free"

### 2.1 The Reality of Flash Sizes

In embedded systems, program storage space is first and foremost strictly limited by Flash / ROM capacity:

- STM32F103: 64KB ~ 128KB Flash
- STM32F4 series: 512KB ~ 2MB Flash
- Low-end MCUs: even as little as 16KB

Compared to PC programs whose executables easily reach tens of MB, this capacity difference is a chasm of orders of magnitude.

### 2.2 How Flash Constraints Affect Software Design

In such an environment, "what code to write" is itself an engineering decision. Code size directly determines whether the system is deployable, and feature redundancy means real storage waste. Introducing a library is no longer a question of "is it easy to use," but "**can it even fit**" (yes, the author has truly seen a binary explode in size just by pulling in `printf`). Therefore, embedded engineers must master some common compiler flags:

- Compiler flags (like `-Os` for optimizing code size)
- Function and section-level garbage collection (`-ffunction-sections`, `-fdata-sections` combined with `--gc-sections`)
- Precise control over linking behavior

Don't rush this; we have a dedicated chapter later to dive into it properly.

## 3. RAM Constraints: Memory Is Not "Use It and Forget It"

If Flash constraints limit "how much functionality we can write," then RAM constraints directly affect whether the system can run stably.

### 3.1 The Order-of-Magnitude Reality of RAM

In embedded systems, RAM is often only: 2KB / 8KB / 20KB / 64KB. In such an environment, we can genuinely trigger a stack overflow, send the SP pointer flying, and if our memory management algorithm is poorly designed, our real-time system might crash after hours or days because heap fragmentation leaves the system unable to find a suitable buffer for allocation behavior.

### 3.2 Stack Risks

Stack space is primarily consumed by: function call depth, interrupt nesting, and local variables. In embedded systems, the following behaviors are often strictly limited or even prohibited.

- You are definitely not allowed to use recursion—we all know the essence of recursion is calling itself, and accidentally stacking the stack too deep will directly crash the system (after all, we have no way to predict exactly how many iterations will occur; no matter how well you calculate, other tasks and user stacks won't care).
- Do not declare large local arrays either—for the same reason: stacking the stack too deep will directly crash the system.

A single unpredictable stack growth can directly corrupt the system.

If you really need a large array, do it this way:

```c
// 避免大型局部数组
void process_data(void) {
    // 不建议：uint8_t buffer[4096]; // 可能溢出
    // 建议：使用静态或全局内存，或分段处理
    static uint8_t buffer[256]; // 或从内存池分配
}

```

### 3.3 Heap Risks

Dynamic memory allocation at runtime has always been a high-risk operation in embedded systems:

- The time complexity of `malloc`/`free` is unpredictable
- Long-term operation produces memory fragmentation
- Errors are difficult to reproduce and debug

Mature embedded systems typically adopt:

- One-time allocation during the startup phase
- Memory pools / object pools
- Completely static memory models

```c
#define POOL_SIZE 1024
#define BLOCK_SIZE 32
#define NUM_BLOCKS (POOL_SIZE / BLOCK_SIZE)

static uint8_t memory_pool[POOL_SIZE];
static bool block_used[NUM_BLOCKS] = {0};

void* mempool_alloc(void) {
    for (int i = 0; i < NUM_BLOCKS; i++) {
        if (!block_used[i]) {
            block_used[i] = true;
            return &memory_pool[i * BLOCK_SIZE];
        }
    }
    return NULL; // 无可用内存
}

```

In embedded systems, memory management serves determinism first, not convenience.

## 4. CPU Constraints: Compute Power Is Precisely Budgeted

In the PC/server world, we are used to treating the CPU as an "almost inexhaustible" resource:
Algorithm is a bit slow? Add a cache. Too many branches? Leave it to out-of-order execution. Floating-point math too heavy? Hardware has your back. The CPU is more like a backdrop—as long as it's not too slow, it's fine. But in embedded, the question is not "is it fast or slow," **the CPU is a resource that needs to be precisely measured and precisely budgeted**. Of course, with modern chips, if resources aren't very tight, there's no need to go to such lengths, but the cost is right there, and your boss will definitely demand that you squeeze every last drop out of it, right?

------

### 4.1 Compute Characteristics of MCUs

The compute characteristics of a typical MCU and a desktop CPU exist in almost two different worlds:

- Limited clock frequency (tens to hundreds of MHz)
- No out-of-order execution, basically strict in-order pipeline
- Weak branch prediction capabilities, or none at all
- Extremely small Cache, or no Cache at all

The conclusion is straightforward: on an MCU, code behavior **can almost be directly mapped to the instruction stream**. Every `if` `if` you write, every loop, every function call, ultimately turns into real, tangible instructions executed in order.

------

### 4.2 "Engineering" Time Complexity

In the embedded world, time complexity is often not a mathematical discussion like `O(n)`. The real question is:

> **Can this code finish running within one control cycle?**

For example:

- On an MCU without an FPU, a single floating-point operation might take dozens of cycles.
- A single integer division is often more expensive than dozens of additions and subtractions.
- Interrupt response time depends on the instruction path the CPU is executing at that moment.

So embedded engineers do things that might seem "counterintuitive" to desktop programmers:

- Analyze **Worst-Case Execution Time (WCET)**
- Avoid unpredictable loop counts
- Control the number of branches to reduce uncertainty in execution paths
- When necessary, look at the disassembly and manually estimate cycle counts

The following example looks like only a minor refactor, but it is of great significance on an MCU:

```c
// 优化前：条件判断在循环内
for (int i = 0; i < n; i++) {
    if (condition) {
        process_a(data[i]);
    } else {
        process_b(data[i]);
    }
}

```

The problem is not a logic error, but rather: **every single iteration of the loop must go through a branch check**. On a CPU without branch prediction, this is a stable and considerable performance loss. The fix is also quite simple:

```c
// 优化后：减少分支预测失败
if (condition) {
    for (int i = 0; i < n; i++) {
        process_a(data[i]);
    }
} else {
    for (int i = 0; i < n; i++) {
        process_b(data[i]);
    }
}

```

The optimization is not about being "smarter," but rather: **replacing one uncertain branch with one deterministic execution path**. In embedded, this kind of "seemingly verbose" writing style is often the truly safe and analyzable code in engineering practice.

------

## 5. Power Consumption Constraints: Programs "Consume Energy"

Many beginners assume power consumption is entirely a hardware matter: chip model, supply voltage, manufacturing process. But the truth is, **software behavior plays a direct and significant role in power consumption**.

To sum it up in one sentence:

> **Every second your program is running, it is genuinely consuming energy.**

------

### 5.1 Software Behavior Determines Power Consumption

The following seemingly "harmless" software behaviors all directly translate into current consumption:

- Busy loops
- High-frequency polling of peripheral status
- Peripherals left permanently enabled
- The system being frequently and meaninglessly woken up

Even if the CPU is "doing nothing," as long as it is still executing instructions and the clock is still running, power consumption continues. In other words: **"the CPU is busy" is itself a state of energy consumption.

------

### 5.2 Software Design for Low Power

The core of embedded low-power design is not "computing faster," but rather:

> **Wake up when you need to, sleep when you don't.**

Common strategies include:

- Replacing polling with event-driven architectures
- Using interrupts instead of while-loops
- Properly entering Sleep / Stop / Standby modes
- Consolidating scattered work into batch processing

A typical low-power main loop looks like this:

```c
void main_loop(void) {
    while (1) {
        // 检查是否有事件待处理
        if (!event_pending()) {
            // 无事件时进入低功耗模式
            enter_sleep_mode();
            wait_for_interrupt(); // 硬件特定指令
        }
        // 处理所有待处理事件
        process_all_events();
    }
}

```

The sophistication lies not in complex logic, but in explicitly telling the system: **don't force it when there's nothing to do, let the hardware save power for you**. In embedded, code that is written "smarter" is often more power-efficient than code that is written "faster."

------

## 6. Boot Time Constraints: From Power-On to Usable

In many embedded scenarios, "boot complete" is not a vague concept, but a **hard requirement written into the specs**: the system must enter a usable state within a limited time.

------

### 6.1 Why Boot Time Matters

These scenarios are particularly sensitive:

- Industrial control (must enter control state immediately upon power-on)
- Automotive electronics (cannot "take its time thinking")
- Consumer electronics (user experience)

You cannot just "show a loading spinner" like on a PC; the system must become usable in a specified time, in a predictable manner.

------

### 6.2 The Cost of the Boot Chain

A typical boot chain:

1. Power-on reset
2. BootROM execution
3. Bootloader initialization
4. Peripheral and memory initialization
5. Entering main control logic

Every step in the chain consumes boot time. The principle is: **only do what must be done, and defer complex or non-critical initialization as much as possible**.

```c
// 只初始化必要的外设，延迟初始化其他
void system_init(void) {
    init_clock();       // 必须首先初始化
    init_watchdog();    // 尽早启用看门狗
    init_critical_io(); // 关键 IO 初始化

    // 非关键外设延迟初始化
    // init_uart();    // 移到需要时初始化
    // init_spi();     // 同上
}

```

This kind of "restrained" initialization approach is often the key to meeting boot time targets.

------

## 7. Real-Time Performance and Determinism: The Soul of Embedded Systems

### 7.1 Real-Time Does Not Mean "Fast"

Beginners often equate "real-time" with "faster," but real-time systems are actually more concerned with:

> **Whether time constraints can be met.**

- Hard Real-Time: once a deadline is missed, the system is considered failed.
- Soft Real-Time: occasional deadline misses are allowed, but they must be controllable.

Whether a system is real-time depends on whether it can still complete tasks on time **in the worst-case scenario**.

------

### 7.2 Determinism

Determinism means that given the same inputs and states, the program's execution path, time consumption, and results are all **predictable**. Looking back at the constraints discussed earlier, you will find they all point to the same goal:

- Flash constraints limit the scale of functionality
- RAM strategies avoid runtime uncertainty
- CPU constraints force analyzable execution paths
- Power and boot constraints limit the system behavior model

The true value of an embedded system lies not in "how fast it runs," but in:

> **Remaining controllable even in the worst-case scenario.**

Below is a minimal yet deterministic scheduler example:

```c
// 简单的周期任务调度器
typedef struct {
    void (*task)(void);
    uint32_t period_ticks;
    uint32_t last_run;
} scheduled_task_t;

void scheduler_run(void) {
    uint32_t now = get_system_tick();

    for (int i = 0; i < NUM_TASKS; i++) {
        if ((now - tasks[i].last_run) >= tasks[i].period_ticks) {
            tasks[i].task();            // 执行任务
            tasks[i].last_run = now;    // 更新执行时间
        }
    }
}

```

It is neither complex nor flashy, but its behavior is **analyzable, derivable, and verifiable**—and these are exactly the traits that embedded values most.
