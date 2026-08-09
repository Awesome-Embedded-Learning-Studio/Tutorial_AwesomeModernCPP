---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Introduces resource constraints (Flash, RAM, CPU) and real-time requirements
  in embedded systems
difficulty: beginner
order: 1
platform: stm32f1
prerequisites: []
reading_time_minutes: 10
related: []
tags:
- cpp-modern
- intermediate
- stm32f1
title: Embedded Resource and Real-Time Constraints
translation:
  source: documents/vol8-domains/embedded/01-resource-and-realtime-constraints.md
  source_hash: 94c5d21983a8c8f31593372235fc9b123a79f017d9b5f647d940e4677ab901f9
  translated_at: '2026-06-16T04:10:29.188000+00:00'
  engine: anthropic
  token_count: 1514
---
# Resources and Real-Time Constraints in Embedded Systems

## 1. Introduction: Why "Just Writing Code" Doesn't Work in Embedded

In PC or server development, we are accustomed to a "default" premise: if memory is insufficient, we add more; if computing power is lacking, we scale up; the operating system handles system scheduling. The goal of a program is often simply to satisfy "functional correctness + average acceptable performance."

However, embedded systems do not exist in such a world. In the embedded environment, resources are strictly quantified: Flash might be only a few dozen KB, RAM only a few KB, CPU frequency only a few dozen MHz, yet the system bears responsibilities for real-time control, device safety, and industrial or consumer-grade reliability. Here, a program is not enough just to "run"; it must also:

- Complete tasks within a specified time.
- Behave correctly even in the worst-case scenario.
- Maintain long-term stable operation within limited resources.

The essence of embedded engineering is the pursuit of system determinism in a resource-constrained world. Of course, this conflicts somewhat with C++'s tendency to hide things, but using most C++ features effectively can indeed drive significant performance improvements.

## 2. Flash / ROM Constraints: Code Is Not "Free"

### 2.1 The Reality of Flash Capacity

In embedded systems, program storage space is first strictly limited by Flash / ROM capacity:

- STM32F103: 64KB ~ 128KB Flash
- STM32F4 Series: 512KB ~ 2MB Flash
- Low-end MCUs: Even only 16KB

Compared to PC programs with executables often reaching tens of MB, this capacity difference is a massive gap.

### 2.2 How Flash Constraints Affect Software Design

In such an environment, "what code to write" is itself an engineering decision. Code size directly determines whether the system can be deployed, and functional redundancy means real storage waste. Introducing a library is no longer a question of "is it easy to use," but "**can it fit**" (yes, the author has truly seen binary sizes explode as soon as `printf` is pulled in). Therefore, embedded engineers must master common compiler optimization options:

- Compiler optimization flags (e.g., `-Os` for code size)
- Function and section-level garbage collection (`-ffunction-sections`, `-fdata-sections` paired with `--gc-sections`)
- Precise control over linking behavior

Don't worry, we have a dedicated chapter later to thoroughly understand this.

## 3. RAM Constraints: Memory Isn't "Just Use It and Forget It"

If Flash constraints limit "how much functionality can be written," then RAM constraints directly affect whether the system can run stably.

### 3.1 The Quantitative Reality of RAM

In embedded systems, RAM is often only: 2KB / 8KB / 20KB / 64KB. In such an environment, we can genuinely trigger a stack overflow, causing the SP (Stack Pointer) to go astray. Moreover, if the memory management algorithm is poor, our real-time system might crash after hours or days due to heap fragmentation (the `allocate` behavior cannot find a suitable buffer).

### 3.2 The Risks of the Stack

Stack space is primarily consumed by: function call depth, interrupt nesting, and local variables. In embedded systems, the following behaviors are often strictly restricted or even prohibited.

- You are definitely not allowed to use recursion—we know the essence of recursion is calling oneself. Carelessly stacking too deep can directly crash the system (after all, we cannot predict exactly how many iterations are needed; your calculations won't stop the stack of other tasks and users from overflowing).
- Do not create large local arrays, for the same reason—stacking too deep will directly crash the system.

One unpredictable stack growth can directly destroy the system.

If you really need a large array, do it this way:

```cpp
// Bad: Large array on stack
uint8_t buffer[4096];

// Good: Static/global allocation
static uint8_t buffer[4096];
```

### 3.3 The Risks of the Heap

Dynamic memory allocation at runtime has always been a high-risk operation in embedded systems:

- `malloc`/`free` time complexity is unpredictable.
- Long-term operation generates memory fragmentation.
- Errors are hard to reproduce and debug.

Mature embedded systems usually adopt:

- One-time allocation during the startup phase.
- Memory pools / object pools.
- Completely static memory models.

```cpp
// Preferred: Static allocation or pool
class Driver {
    static Driver& instance() {
        static Driver inst; // Allocated once, never freed
        return inst;
    }
};
```

In embedded systems, memory management serves determinism first, not convenience.

## 4. CPU Constraints: Computing Power is Precisely Counted

In the PC/server world, we are used to treating CPU as an "almost inexhaustible" resource:
Algorithm is slow? Add a cache. More branches? Leave it to out-of-order execution. Floating point too heavy? Hardware handles it. The CPU acts more like a backdrop—as long as it's not too slow. But in embedded, it's not a question of "fast or slow," **the CPU is a resource that needs to be precisely measured and budgeted**. Of course, with modern chips, if resources aren't very tight, there's no need to do this, but given the cost, your boss will surely demand you squeeze every bit out of it, right?

### 4.1 Computing Characteristics of MCUs

The computing characteristics of typical MCUs are almost in a different world compared to desktop CPUs:

- Limited frequency (tens to hundreds of MHz).
- No out-of-order execution, basically strictly sequential pipelines.
- Weak branch prediction capabilities, or none at all.
- Extremely small Cache, or no Cache.

The conclusion is direct: on an MCU, code behavior **can almost be directly mapped to the instruction stream**. Every `if`, every loop, and every function call you write eventually turns into actual instructions executing in sequence.

### 4.2 "Engineering" Time Complexity

In the embedded world, time complexity is often not a mathematical discussion like $O(n)$; the real question is:

> **Can this code finish running within one control cycle?**

For example:

- On an MCU without an FPU, a floating-point operation might take dozens of cycles.
- An integer division is often more expensive than dozens of additions/subtractions.
- Interrupt response time depends on the instruction path the CPU is executing at that moment.

So embedded engineers do things that seem "counter-intuitive" to desktop programmers:

- Analyze **Worst-Case Execution Time (WCET)**.
- Avoid unpredictable loop counts.
- Control the number of branches to reduce uncertainty in execution paths.
- When necessary, look at disassembly and manually estimate cycle counts.

The following example looks like a minor refactor, but it is significant on an MCU:

```cpp
// Naive version: Branch inside loop
for (int i = 0; i < n; ++i) {
    if (i % 2 == 0) {
        process_even(i);
    } else {
        process_odd(i);
    }
}
```

The problem isn't a logic error, but: **Every loop iteration experiences a branch judgment**. On a CPU without branch prediction, this is a stable and considerable performance loss. The fix is also simple:

```cpp
// Optimized version: Unroll/Decouple
for (int i = 0; i < n; i += 2) {
    process_even(i);
    if (i + 1 < n) process_odd(i + 1);
}
```

The optimization point isn't being "smarter," but: **Swapping one uncertain branch for one deterministic execution path**. In embedded, this kind of "wordy-looking" code is often what is truly safe and analyzable in engineering.

## 5. Power Constraints: The Program "Consumes Energy"

Many novices think power consumption is purely a hardware matter: chip model, supply voltage, process technology. But the fact is, **software behavior plays a direct and significant role in power consumption**.

To summarize in one sentence:

> **Every second your program runs, it is consuming real energy.**

### 5.1 Software Behavior Determines Power Consumption

The following seemingly "harmless" software behaviors all translate directly into current consumption:

- Busy loops.
- High-frequency polling of peripheral status.
- Peripherals kept on all year round.
- The system being woken up frequently and meaninglessly.

Even if the CPU is "doing nothing," as long as it is executing instructions and the clock is running, power consumption continues. In other words: **"The CPU is busy" is itself a state of energy consumption.**

### 5.2 Software Design for Low Power

The core of embedded low-power design is not "calculating faster," but:

> **Wake when you need to, sleep when you should.**

Common strategies include:

- Replacing polling with event-driven models.
- Using interrupts instead of `while` loops.
- Properly entering Sleep / Stop / Standby modes.
- Merging scattered work into batch processing.

A typical low-power main loop looks like this:

```cpp
void main_loop() {
    while (true) {
        if (event_pending()) {
            handle_events(); // Do work quickly
        }
        enter_low_power_mode(); // Sleep otherwise
    }
}
```

The sophistication lies not in complex logic, but in explicitly telling the system: **Don't hold on when there's nothing to do; let the hardware save power for you.** In embedded, "smarter" code is often more power-efficient than "faster" code.

## 6. Boot Time Constraints: From Power-On to Ready

In many embedded scenarios, "boot complete" is not a vague concept, but a **hard indicator written into requirements**: the system must enter a usable state within a limited time.

### 6.1 Why Boot Time Matters

These scenarios are particularly sensitive:

- Industrial control (must enter control state immediately upon power-up).
- Automotive electronics (cannot "think slowly").
- Consumer electronics (user experience).

You cannot "spin to load" like a PC; the system must become available in a specified time and in a predictable manner.

### 6.2 The Cost of the Boot Chain

Typical boot chain:

1. Power-on reset.
2. BootROM execution.
3. Bootloader initialization.
4. Peripheral and memory initialization.
5. Enter main control logic.

Every step in the chain consumes boot time. The principle is: **Do only what is necessary, and delay complex or non-critical initialization as much as possible.**

```cpp
// Lazy initialization strategy
void System::init() {
    init_core();       // Must be done now
    // init_gui();     // Heavy, defer to later
    // init_network(); // Non-critical, lazy load
}
```

This "restrained" initialization method is often the key to meeting boot time indicators.

## 7. Real-Time and Determinism: The Soul of Embedded Systems

### 7.1 Real-Time Does Not Mean "Fast"

Novices often equate "real-time" with "faster," but real-time systems are actually more concerned with:

> **Can time constraints be met?**

- Hard Real-Time: Once a timeout occurs, the system is judged as a failure.
- Soft Real-Time: Occasional timeouts are allowed, but must be controllable.

Whether it is real-time depends on whether the task can still be completed on time **in the worst case**.

### 7.2 Determinism

Determinism means: given the same input and state, the program's execution path, time consumption, and results are **predictable**. Looking back at the previous constraints, you will find they all point to the same goal:

- Flash constraints limit functional scale.
- RAM strategies avoid runtime uncertainty.
- CPU constraints force analyzable execution paths.
- Power and boot constraints limit system behavior models.

The true value of an embedded system lies not in "how fast it runs," but in:

> **Still being controllable in the worst case.**

Below is a minimalist but deterministic scheduler example:

```cpp
void simple_scheduler() {
    while (true) {
        task1(); // Fixed execution time
        task2(); // Fixed execution time
        // No dynamic scheduling, no heap allocation
    }
}
```

It is not complex, not flashy, but its behavior is **analyzable, derivable, and verifiable**—which is exactly the trait most valued in embedded systems.
