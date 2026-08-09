---
chapter: 16
difficulty: intermediate
order: 12
platform: stm32f1
reading_time_minutes: 10
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 30: EXTI Interrupts + Pitfalls and Exercises'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/12-exti-interrupt-and-exercises.md
  source_hash: d3f3c202a5313e129bd36314bb44853f7dd50d05c9534e5e1f372a7c166559e1
  translated_at: '2026-06-16T04:11:27.295929+00:00'
  engine: anthropic
  token_count: 1709
---
# Part 30: EXTI Interrupts + Pitfalls and Exercises

> The final article in the button tutorial. In the previous 11 parts, we used "polling" to detect button states—the main loop repeatedly calling `read()`. This part introduces another approach: letting hardware notify the CPU when the button state changes. We conclude with a summary of common pitfalls and three exercises.

---

## Polling vs Interrupts

The polling method involves the CPU repeatedly checking the button state in the main loop. The advantage is simplicity and controllability; the disadvantage is that if the main loop is performing other time-consuming operations, it might miss button state changes.

The interrupt method involves the CPU configuring the hardware to automatically interrupt the current execution flow when the pin level changes, jumping to a pre-registered interrupt service routine (ISR) to handle the event. After processing, it returns to the interrupted location to continue execution.

These two approaches are not mutually exclusive. Our final code uses polling + state machine debouncing—which is sufficient for most button scenarios. However, understanding the interrupt mechanism is crucial for embedded development, as many peripherals (UART reception, timers, ADC conversion completion) notify the CPU via interrupts.

---

## EXTI: External Interrupt Controller

EXTI (External Interrupt/Event Controller) is the interrupt controller in STM32 specifically dedicated to handling level changes on external pins.

### EXTI Line Mapping

STM32F103 has 20 EXTI lines (EXTI0 ~ EXTI19), where EXTI0 ~ EXTI15 correspond to GPIO pins:

```text
EXTI0  -> PA0, PB0, PC0...
EXTI1  -> PA1, PB1, PC1...
...
EXTI15 -> PA15, PB15, PC15...
EXTI16 -> PVD (Programmable Voltage Detector)
EXTI17 -> RTC Alarm
EXTI18 -> USB Wakeup
EXTI19 -> Ethernet Wakeup
```

Key rule: At any given time, one EXTI line can only connect to the corresponding pin of one port. For example, EXTI0 can connect to PA0, PB0, or PC0, but not multiple simultaneously. The connection selection is configured through the AFIO (Alternate Function I/O) `EXTICR` registers.

One advantage of choosing PA0: EXTI0 has a dedicated interrupt vector `EXTI0_IRQHandler`, so it doesn't need to share with other pins. If we chose PA5, the interrupt vector `EXTI9_5_IRQHandler` is shared by EXTI5~9—after the interrupt triggers, you would need to check which specific pin triggered it.

### Trigger Modes

EXTI supports three trigger modes:

| Mode | Meaning | HAL Constant |
|------|---------|--------------|
| Rising edge trigger | Trigger when level goes from low to high | `EXTI_TRIGGER_RISING` |
| Falling edge trigger | Trigger when level goes from high to low | `EXTI_TRIGGER_FALLING` |
| Both edges trigger | Trigger on any level change | `EXTI_TRIGGER_RISING_FALLING` |

In the button pull-up scheme, pressing is a falling edge (high→low), and releasing is a rising edge (low→high). If you only care about the press, use falling edge trigger; if you care about both press and release, use both edges.

---

## EXTI Configuration Process

### C Language Configuration

```c
// 1. Enable AFIO clock (Required!)
RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

// 2. Configure GPIO input mode
GPIO_InitTypeDef GPIO_InitStruct = {0};
GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_0;
GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU; // Input Pull-up
GPIO_Init(GPIOA, &GPIO_InitStruct);

// 3. Connect EXTI Line to Pin
GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);

// 4. Configure EXTI Line
EXTI_InitTypeDef EXTI_InitStruct = {0};
EXTI_InitStruct.EXTI_Line    = EXTI_Line0;
EXTI_InitStruct.EXTI_Mode    = EXTI_Mode_Interrupt;
EXTI_InitStruct.EXTI_Trigger = EXTI_Trigger_Falling;
EXTI_InitStruct.EXTI_LineCmd = ENABLE;
EXTI_Init(&EXTI_InitStruct);

// 5. Enable and configure NVIC
NVIC_InitTypeDef NVIC_InitStruct = {0};
NVIC_InitStruct.NVIC_IRQChannel = EXTI0_IRQn;
NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 0x0F;
NVIC_InitStruct.NVIC_IRQChannelSubPriority = 0x0F;
NVIC_InitStruct.NVIC_IRQChannelCmd = ENABLE;
NVIC_Init(&NVIC_InitStruct);
```

Four steps: Enable AFIO clock → Configure GPIO interrupt mode → Configure EXTI → Configure NVIC.

⚠️ The first step is the easiest to forget. The AFIO clock is disabled by default. If you don't call `RCC_APB2PeriphClockCmd`, the EXTI configuration registers cannot be written, and the interrupt will never trigger. This bug won't throw an error—the C compiler doesn't know if you've enabled the AFIO clock; it just writes values to registers, but if the values don't stick, it can't detect that.

### Interrupt Callback Chain

The call chain after a hardware interrupt triggers:

```text
Hardware Interrupt
  └─> EXTI0_IRQHandler() [Startup startup.s]
       └─> EXTI0_IRQHandler() [Weak definition in stm32f1xx_it.c]
            └─> HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0) [stm32f1xx_hal_gpio.c]
                 └─> HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) [Weak definition in stm32f1xx_hal_gpio.c]
```

Our `stm32f1xx_hal_gpio.c` already defines `HAL_GPIO_EXTI_IRQHandler` and a weak `HAL_GPIO_EXTI_Callback`:

```c
void HAL_GPIO_EXTI_IRQHandler(uint16_t GPIO_Pin) {
    if (EXTI->PR & (uint32_t)GPIO_Pin) {
        EXTI->PR = (uint32_t)GPIO_Pin; // Clear interrupt flag
        HAL_GPIO_EXTI_Callback(GPIO_Pin); // Call user callback
    }
}

__weak void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    // Prevent unused argument warning
    UNUSED(GPIO_Pin);
}
```

`__weak` is a GCC weak symbol attribute—if a function with the same name is defined in another `.c`/`.cpp` file, the linker will use that definition; if not, it uses this empty implementation. This allows you to override the callback function anywhere without modifying the HAL library.

---

## Simple Example of Interrupt-Driven Button

```cpp
volatile bool button_pressed = false;

extern "C" void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_0) {
        button_pressed = true;
    }
}

int main() {
    // ... Hardware init ...

    while (true) {
        if (button_pressed) {
            button_pressed = false;
            // Toggle LED
            // ...
        }
    }
}
```

### The Role of volatile

The `button_pressed` variable is declared as `volatile`. Why?

During compiler optimization, if the compiler discovers that `button_pressed` is only read in the main loop and not modified by other code (the compiler cannot see the interrupt context), it might cache the value of `button_pressed` in a register and never read from memory again. This way, even if the ISR modifies `button_pressed`, the main loop won't see the change.

`volatile` tells the compiler: "This variable might be modified in ways the compiler can't see (like by an interrupt), so every read must be reloaded from memory; do not cache it."

⚠️ `volatile` does not guarantee atomicity—it only guarantees "always read from memory." If multiple interrupts modify the same variable simultaneously, mutual exclusion protection is still needed. However, in our scenario, we have one ISR writing and the main loop reading, so there is no race condition.

### Interrupt Debouncing

The example above lacks debouncing—during the bouncing period, EXTI will trigger multiple interrupts. There are two ways to debounce in an interrupt:

1. **Record timestamp in ISR, confirm in main loop**: The ISR only sets a flag and timestamp; the main loop checks if the time difference is sufficient.
2. **Delay directly in the ISR**: Not recommended—ISRs should return as soon as possible and must not block. Calling `HAL_Delay` in an ISR is dangerous because `HAL_Delay` relies on the SysTick interrupt, and SysTick priority might be lower than EXTI, leading to a deadlock.

Recommended approach: ISR sets flag, main loop confirms with state machine. This is essentially the same as our previous polling solution, except the "initial trigger" changed from polling to interrupt.

---

## Common Pitfalls Summary

### Pitfall 1: Forgetting to Enable AFIO Clock

**Symptom**: EXTI interrupt does not trigger, `HAL_GPIO_EXTI_Callback` is never called.
**Cause**: Did not call `RCC_APB2PeriphClockCmd`, EXTI configuration registers are not writable.
**Solution**: Enable the AFIO clock before configuring EXTI.

### Pitfall 2: Debounce Time Set Too Short

**Symptom**: Still triggering multiple times after debouncing.
**Cause**: `DEBOUNCE_TIME` set too small (e.g., 5ms), some switches with long bounce times aren't filtered out.
**Solution**: The default 20ms is sufficient for the vast majority of switches. If issues persist, adjust to 30-50ms.

### Pitfall 3: Confusing ReadPin Return Value with Pull-up Logic

**Symptom**: Button logic inverted—pressing turns the LED off.
**Cause**: In the pull-up scheme, Pressed = Low Level = `0`. If your code treats `0` as "released", the logic is reversed.
**Solution**: Remember "Pull-up scheme, low level = pressed". Or use `GPIO_PIN_SET`/`GPIO_PIN_RESET` to let the compiler handle it for you.

### Pitfall 4: Forgetting to Handle Boot-lock

**Symptom**: If the button is held during power-on, the LED state is abnormal after release.
**Cause**: No boot-lock mechanism; the system treated "button held at power-on" as a normal event.
**Solution**: Our state machine already handles this—the `Idle` and `Pressed` states ensure the button state at power-on does not trigger an event.

### Pitfall 5: Doing Time-Consuming Operations in ISR

**Symptom**: System freezes or responds abnormally.
**Cause**: Called `HAL_Delay`, print functions, or complex calculations in the ISR. ISRs should return as quickly as possible—usually within microseconds.
**Solution**: Only set flags and timestamps in the ISR; put all logic processing in the main loop.

### Pitfall 6: Polling Interval Too Long

**Symptom**: Rapid press-release is missed by the state machine.
**Cause**: Long blocking operations in the main loop (e.g., `HAL_Delay` blinking LED), causing `update()` call intervals to exceed the button press duration.
**Solution**: Avoid long blocking calls in the main loop. Manage all timed tasks in a non-blocking way.

---

## Exercises

### Exercise 1: Adjust Debounce Time

Modify the `DEBOUNCE_TIME` parameter in `main.cpp` to 50ms and observe how the button response changes. Then change it to 5ms—what happens now?

**Goal**: Understand the trade-off between debounce time, response latency, and reliability. Longer time is more reliable but sluggish; shorter time is faster but might not filter cleanly.

### Exercise 2: Switch to PB5 Button

Change the button from PA0 to PB5. What do you need to modify?

**Hint**:

- Change template parameter to `BtnPB5`
- EXTI line becomes EXTI5
- Interrupt vector becomes `EXTI9_5_IRQHandler` (shared vector)
- `MX_GPIO_Init` needs to add `GPIO_InitTypeDef` for PB5
- Need to check which specific pin triggered in the shared vector

**Goal**: Understand how to handle EXTI shared vectors and the zero-code change nature of modifying template parameters (only need to change type parameters).

### Exercise 3: Hybrid Scheme—Interrupt Trigger + State Machine Confirmation

Implement a scheme where the EXTI interrupt wakes up the state machine, and the state machine completes debouncing and event confirmation in the main loop.

**Hint**:

- Set `flag` and timestamp in ISR
- Main loop checks `flag`, if true calls `update()`
- `update()` works normally, no need to know if the trigger came from interrupt or polling

**Goal**: Understand that interrupts and polling can be mixed—interrupts are responsible for "notify change", state machine is responsible for "confirm and debounce".

---

## Button Tutorial Review

We've completed 12 articles. Let's review our learning path:

**Phase 1: Hardware Basics (01-03)**

- Paradigm shift from output to input
- GPIO input mode internal circuitry: pull-up/pull-down/floating, Schmitt trigger, IDR register
- Button wiring (PA0 pull-up to GND) and mechanical bounce physics

**Phase 2: HAL + C Practice (04-06)**

- Underlying implementation of `HAL_GPIO_ReadPin`
- Pure C polling button, seeing the bounce problem firsthand
- `HAL_GetTick` non-blocking debouncing

**Phase 3: State Machine (07)**

- Complete breakdown of the 7-state debouncing state machine
- Boot-lock boundary handling

**Phase 4: C++ Refactoring (08-12)**

- `class Button`: constructor and private `update()`
- `enum class` + `std::function`: type-safe event system
- Button template class: NTTP four parameters, `if constexpr`, `requires`
- Concepts: `std::invocable` constraining callbacks
- EXTI interrupts: configuration flow, callback chain, volatile semantics

Summary of C++ features used:

- `constexpr` (C++11) — Introduced in LED tutorial, expanded in button tutorial
- Non-type template parameters NTTP (C++11) — Introduced in LED tutorial, added parameters in button tutorial
- `if constexpr` (C++17) — Introduced in LED tutorial, new scenarios in button tutorial
- `std::optional` (C++23) — New in button tutorial
- `std::expected` (C++23) — Introduced in LED tutorial, expanded in button tutorial
- `enum class` + `std::function` (C++17) — New in button tutorial
- Concepts `requires` (C++20) — New in button tutorial
- Forwarding references `T&&` (C++11) — Introduced in button tutorial

None of these features are "fancy syntactic sugar"—in the specific scenario of embedded button control, they all solve practical problems. This is the value of modern C++ in the embedded field: using the compiler's capabilities to replace human vigilance, writing safer and more maintainable code without paying a runtime cost.
