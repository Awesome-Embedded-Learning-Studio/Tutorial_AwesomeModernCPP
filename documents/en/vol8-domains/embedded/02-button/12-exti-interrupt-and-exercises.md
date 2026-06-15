---
chapter: 16
difficulty: intermediate
order: 12
platform: stm32f1
reading_time_minutes: 11
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 30: EXTI Interrupts + Pitfalls and Exercises'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/02-button/12-exti-interrupt-and-exercises.md
  source_hash: 7eb1b5506e65effac513f672453928868bec0ad943d2d45203153069bdca1d3b
  token_count: 1703
  translated_at: '2026-05-26T12:13:12.510434+00:00'
description: ''
---
# Part 30: EXTI Interrupts + Pitfalls and Exercises

> The final article in the button tutorial. In the previous 11 parts, we have been using "polling" to detect button presses — the main loop continuously calls `poll_events()`. This part introduces another approach: letting the hardware proactively notify the CPU when the button state changes. Then, we cover a summary of common pitfalls and three exercises.

---

## Polling vs Interrupts

The polling approach has the CPU repeatedly checking the button state in the main loop. The advantage is simplicity and controllability; the disadvantage is that if the main loop is busy with other time-consuming operations, it might miss button state changes.

The interrupt approach has the CPU configure the hardware so that when the pin level changes, the hardware automatically breaks the current execution flow and jumps to a pre-registered interrupt service routine (ISR) for processing. After handling the event, it returns to the interrupted location and continues execution.

These two approaches are not mutually exclusive. Our final code uses polling + a state machine for debounce — this is sufficient for most button scenarios. However, understanding the interrupt mechanism is crucial for embedded development, because many peripherals (UART reception, timers, ADC conversion complete) notify the CPU through interrupts.

---

## EXTI: External Interrupt Controller

EXTI (External Interrupt/Event Controller) is the interrupt controller in STM32 dedicated to handling external pin level changes.

### EXTI Line Mapping

The STM32F103 has 20 EXTI lines (EXTI0 ~ EXTI19), of which EXTI0 ~ EXTI15 correspond to GPIO pins:

```text
PA0, PB0, PC0, ... → EXTI0（共享，通过 AFIO 选择哪个端口）
PA1, PB1, PC1, ... → EXTI1
...
PA4, PB4, PC4, ... → EXTI4
PA5, PB5, PC5, ... → EXTI5  ─┐
...                           ├→ EXTI9_5_IRQn（共享中断向量）
PA9, PB9, PC9, ... → EXTI9  ─┘
PA10, PB10, PC10, ... → EXTI10 ─┐
...                              ├→ EXTI15_10_IRQn（共享中断向量）
PA15, PB15, PC15, ... → EXTI15 ─┘
```

Key rule: At any given time, one EXTI line can only connect to the corresponding pin of one port. For example, EXTI0 can connect to PA0, PB0, or PC0, but not to multiple simultaneously. The connection selection is configured through the `EXTICR` register of AFIO (Alternate Function I/O).

One advantage of choosing PA0 is that EXTI0 has an independent interrupt vector `EXTI0_IRQn`, so it does not need to share with other pins. If we chose PA5, the EXTI5 interrupt vector `EXTI9_5_IRQn` is shared by EXTI5~9 — after the interrupt triggers, we would also need to check which specific pin caused it.

### Trigger Modes

EXTI supports three trigger modes:

| Mode | Meaning | HAL Constant |
|------|---------|--------------|
| Rising edge trigger | Triggers when the level goes from low to high | `GPIO_MODE_IT_RISING` |
| Falling edge trigger | Triggers when the level goes from high to low | `GPIO_MODE_IT_FALLING` |
| Dual edge trigger | Triggers on any level change | `GPIO_MODE_IT_RISING_FALLING` |

In the pull-up button scheme, pressing is a falling edge (high→low), and releasing is a rising edge (low→high). If we only care about presses, we use falling edge trigger; if we care about both presses and releases, we use dual edge.

---

## EXTI Configuration Flow

### C Language Configuration

```c
/* 1. 使能 AFIO 时钟（EXTI 配置需要 AFIO） */
__HAL_RCC_AFIO_CLK_ENABLE();

/* 2. 使能 GPIOA 时钟 */
__HAL_RCC_GPIOA_CLK_ENABLE();

/* 3. 配置 PA0 为中断模式 + 上拉 */
GPIO_InitTypeDef init = {0};
init.Pin = GPIO_PIN_0;
init.Mode = GPIO_MODE_IT_FALLING;  // 下降沿触发（按下瞬间）
init.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &init);
// HAL_GPIO_Init 内部会自动配置 AFIO EXTICR 寄存器

/* 4. 配置 NVIC 中断优先级和使能 */
HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
HAL_NVIC_EnableIRQ(EXTI0_IRQn);
```

Four steps: enable the AFIO clock → configure GPIO interrupt mode → configure the NVIC.

⚠️ The first step is the easiest to forget. The AFIO clock is off by default. If we do not call `__HAL_RCC_AFIO_CLK_ENABLE()`, the EXTI configuration registers cannot be written to, and the interrupt will never trigger. This bug will not produce an error — `HAL_GPIO_Init()` does not know whether we have enabled the AFIO clock; it simply writes values to the registers, but if the values do not stick, it cannot detect that either.

### Interrupt Callback Chain

The call chain after a hardware interrupt triggers:

```text
物理电平变化（下降沿）
  → EXTI 硬件检测到边沿
  → NVIC 挂起 EXTI0 中断
  → CPU 暂停当前任务
  → 跳转到 EXTI0_IRQHandler()
    → HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0)
      → 清除 EXTI 挂起标志
      → 调用 HAL_GPIO_EXTI_Callback(GPIO_PIN_0)
        → 用户在这里写处理逻辑
  → 返回被中断的代码继续执行
```

Our `hal_mock.c` already defines `EXTI0_IRQHandler` and a weak `HAL_GPIO_EXTI_Callback`:

```c
void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0);
}

__attribute__((weak)) void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    (void)GPIO_Pin;
}
```

`__attribute__((weak))` is a GCC weak symbol attribute — if another `.c`/`.cpp` file defines a function with the same name, the linker will use that definition; if not, it will use this empty implementation. This allows us to override the callback function anywhere without modifying `hal_mock.c`.

---

## A Simple Interrupt-Driven Button Example

```c
/* 全局变量：中断标志 */
volatile uint8_t button_pressed = 0;

/* 覆盖弱回调 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        button_pressed = 1;
    }
}

int main(void) {
    HAL_Init();
    /* 系统时钟配置 */
    /* GPIO 和 NVIC 配置（如上） */

    while (1) {
        if (button_pressed) {
            button_pressed = 0;
            /* 处理按钮按下 */
            HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        }
        /* 其他任务 */
    }
}
```

### The Role of volatile

The `button_pressed` variable is declared as `volatile`. Why?

During compiler optimization, if it finds that `button_pressed` in the main loop is only read and not modified by other code (the compiler cannot see the interrupt context), it might cache the value of `button_pressed` in a register and never read from memory again. This way, even if the ISR modifies `button_pressed`, the main loop will not see the change.

`volatile` tells the compiler: this variable might be modified in ways the compiler cannot see (such as by an interrupt), so every read must be reloaded from memory and cannot be cached.

⚠️ `volatile` does not guarantee atomicity — it only guarantees "always read from memory." If multiple interrupts modify the same variable simultaneously, mutual exclusion protection is still needed. However, in our scenario, there is only one ISR writing and the main loop reading, so there is no race condition.

### Interrupt Debouncing

The example above has no debouncing — during the bounce period, EXTI will trigger multiple interrupts. There are two ways to debounce in an interrupt:

1. **Record a timestamp in the interrupt, confirm in the main loop**: The ISR only sets a flag and timestamp, and the main loop checks whether the time difference is sufficient.
2. **Delay directly in the interrupt**: Not recommended — the ISR should return as quickly as possible and must not block. Calling `HAL_Delay()` in an ISR is dangerous because `HAL_Delay()` relies on the SysTick interrupt, and the SysTick priority might be lower than EXTI, leading to a dead lock.

Recommended approach: set a flag in the interrupt, and use a state machine in the main loop to confirm. This is essentially the same as our previous polling approach, except the "initial trigger" changed from polling to an interrupt.

---

## Common Pitfalls Summary

### Pitfall 1: Forgetting to Enable the AFIO Clock

**Symptom**: The EXTI interrupt does not trigger, and `HAL_GPIO_EXTI_Callback()` is never called.
**Cause**: `__HAL_RCC_AFIO_CLK_ENABLE()` was not called, making the EXTI configuration registers unwritable.
**Solution**: Enable the AFIO clock before configuring EXTI.

### Pitfall 2: Setting the Debounce Time Too Short

**Symptom**: Multiple triggers still occur after debouncing.
**Cause**: `debounce_ms` is set too small (e.g., 5ms), and switches with longer bounce times are not filtered in time.
**Solution**: The default 20ms is sufficient for the vast majority of switches. If issues persist, it can be adjusted to 30-50ms.

### Pitfall 3: Confusing ReadPin Return Value with Pull-Up Logic

**Symptom**: Button logic is inverted — pressing turns the LED off instead.
**Cause**: In the pull-up scheme, pressed = low level = `GPIO_PIN_RESET`. If our code treats `GPIO_PIN_RESET` as "released," the logic is inverted.
**Solution**: Remember "pull-up scheme, low level = pressed." Or use `ButtonActiveLevel` to let the compiler handle it for us.

### Pitfall 4: Forgetting to Handle Boot-Lock

**Symptom**: If the button is held down during power-on, the LED state is abnormal after release.
**Cause**: There is no boot-lock mechanism, and the system treats "button already held at power-on" as a normal event.
**Solution**: Our state machine already handles this — the `BootSync` and `BootPressed` states ensure that the button state at power-on does not trigger an event.

### Pitfall 5: Doing Time-Consuming Operations in the ISR

**Symptom**: The system freezes or responds abnormally.
**Cause**: Time-consuming operations such as `HAL_Delay()`, print functions, or complex calculations are called in the ISR. The ISR should return as quickly as possible — usually within a few microseconds.
**Solution**: Only set flags and timestamps in the ISR, and put all logic processing in the main loop.

### Pitfall 6: Polling Interval Too Long

**Symptom**: Fast press-and-release actions are missed by the state machine.
**Cause**: There are long-blocking operations in the main loop (e.g., `HAL_Delay(500)` blinking the LED), causing the interval between `poll_events()` calls to exceed the duration of the button press.
**Solution**: Avoid using long-blocking calls in the main loop. Manage all timed tasks in a non-blocking way.

---

## Exercises

### Exercise 1: Adjust the Debounce Time

Modify the `debounce_ms` parameter of `poll_events()` to 50ms, and observe what changes in the button response. Then change it to 5ms — what happens now?

**Goal**: Understand the trade-off between debounce time, response latency, and reliability. A longer time is more reliable but makes the response sluggish; a shorter time makes the response faster but might not filter cleanly.

### Exercise 2: Switch to the PB5 Button

Change the button from PA0 to PB5. What do you need to modify?

**Hints**:

- Change the template parameter to `GpioPort::B, GPIO_PIN_5`
- The EXTI line becomes EXTI5
- The interrupt vector becomes `EXTI9_5_IRQn` (a shared vector)
- We need to add `EXTI9_5_IRQHandler` in `hal_mock.c`
- The shared vector needs to check which specific pin triggered it

**Goal**: Understand how to handle EXTI shared vectors, and experience the zero-code-change aspect of modifying template parameters (only the type parameter needs to change).

### Exercise 3: Hybrid Approach — Interrupt Trigger + State Machine Confirmation

Implement a solution where the EXTI interrupt wakes up the state machine, and the state machine completes debouncing and event confirmation in the main loop.

**Hints**:

- Set `volatile bool exti_triggered = true` and a timestamp in the ISR
- Check `exti_triggered` in the main loop; if true, call `poll_events()`
- `poll_events()` works normally and does not need to know whether the trigger came from an interrupt or polling

**Goal**: Understand that interrupts and polling can be used together — the interrupt is responsible for "notifying a change," and the state machine is responsible for "confirmation and debouncing."

---

## Button Tutorial Review

We have completed 12 articles. Let us review our learning path:

**Phase One: Hardware Basics (01-03)**

- The paradigm shift from output to input
- GPIO input mode internal circuitry: pull-up/pull-down/floating, Schmitt trigger, IDR register
- Button wiring (PA0 pull-up to GND) and the physical principles of mechanical bouncing

**Phase Two: HAL + C in Practice (04-06)**

- The underlying implementation of `HAL_GPIO_ReadPin()`
- Pure C polling button, seeing the bouncing problem firsthand
- `HAL_GetTick()` non-blocking debouncing

**Phase Three: State Machine (07)**

- Complete walkthrough of the 7-state debouncing state machine
- Boot-lock boundary handling

**Phase Four: C++ Refactoring (08-12)**

- `enum class`: `ButtonActiveLevel` and private `State`
- `std::variant` + `std::visit`: a type-safe event system
- Button template class: four NTTP parameters, `if constexpr`, `static_assert`
- Concepts: `requires std::invocable` constraining callbacks
- EXTI interrupts: configuration flow, callback chain, volatile semantics

Summary of C++ features used:

- `enum class` (C++11) — introduced in the LED tutorial, expanded in the button tutorial
- Non-type template parameters (NTTP) (C++11) — introduced in the LED tutorial, added parameters in the button tutorial
- `if constexpr` (C++17) — introduced in the LED tutorial, new scenarios in the button tutorial
- `static_assert` (C++11) — newly added in the button tutorial
- `[[nodiscard]]` (C++17/23) — introduced in the LED tutorial, expanded in the button tutorial
- `std::variant` + `std::visit` (C++17) — newly added in the button tutorial
- Concepts `std::invocable` (C++20) — newly added in the button tutorial
- Forwarding references `Callback&&` (C++11) — introduced in the button tutorial

None of these features are "flashy syntactic sugar" — they all solve practical problems in the specific scenario of embedded button control. This is the value of modern C++ in the embedded domain: using the compiler's capabilities to replace human vigilance, writing safer and more maintainable code without paying a runtime cost.
