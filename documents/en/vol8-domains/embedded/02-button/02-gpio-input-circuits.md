---
chapter: 16
difficulty: intermediate
order: 2
platform: stm32f1
reading_time_minutes: 10
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 20: GPIO Input Mode Internal Circuitry — How the Chip "Listens" to External
  Signals'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/02-gpio-input-circuits.md
  source_hash: e9a1cb57a905dad6ae7fe31a30931a9de35380de94f343fcc68a637f1560498f
  translated_at: '2026-06-16T04:10:50.956359+00:00'
  engine: anthropic
  token_count: 1604
---
# Part 20: GPIO Input Mode Internal Circuitry — How the Chip "Hears" External Signals

> Following up on the previous post: Buttons are harder than LEDs for three reasons—reading instead of writing, physical noise, and timing management. In this post, we solve the first problem: how exactly does GPIO input mode work?

---

## From Output Path to Input Path

In the LED tutorial, we spent a lot of time understanding the internal circuitry of GPIO output mode. The core signal path for output mode is:

```text
CPU 写入 ODR → 输出驱动器（推挽/开漏） → GPIO 引脚 → 外部电路
```

When the CPU writes a 1 to a specific bit in the `ODR` (Output Data Register), the corresponding push-pull driver pulls the pin to VDD (high level); writing a 0 pulls it to VSS (low level). The signal flows from the inside of the chip to the outside world—the chip is the active agent.

Now we need to reverse this path. In button mode, the signal flows from the outside world into the chip:

```text
GPIO 引脚 → 保护二极管 → 上拉/下拉电阻（可选） → 施密特触发器 → IDR → CPU 可读
```

Note that the signal direction has changed. The voltage on the pin is no longer controlled by the CPU—it is determined by external circuitry (in our scenario, by the button being closed or open). The CPU's role changes from "writing ODR" to "reading IDR"—passively observing changes in the pin level.

---

## Every Stop on the Input Path

Let's walk along the signal path, starting from the pin and moving inward, to see what happens at each stage.

### First Stop: Protection Diodes

Immediately following the pin are two protection diodes, one connected to VDD and one to VSS. Their job is to clamp the voltage—if the voltage on the pin exceeds VDD + 0.6V, the upper diode conducts and shunts the excess voltage to VDD; if it drops below VSS - 0.6V, the lower diode conducts and shunts it to VSS.

This layer of protection isn't the focus for button scenarios—button voltages are simply 0V or 3.3V, well within range. However, if you are connecting sensors or other devices that might generate abnormal voltages, these diodes are the first line of defense preventing the chip from being damaged. STM32 pins can withstand a voltage range of -0.3V to VDD + 0.3V (beyond which the protection diodes start working), with an absolute maximum rating of 4.0V (beyond which permanent damage occurs).

### Second Stop: Pull-Up / Pull-Down Resistors

After passing the protection diodes, the signal arrives at a fork in the road. There are three options here:

- **Floating (No Pull)**: Both pull-up and pull-down resistors are disconnected. The pin level is entirely determined by external circuitry. If nothing is connected externally (the pin is floating), the level is undefined—subject to electromagnetic interference, it may randomly jump between high and low.
- **Pull-Up**: An internal resistor (approximately 30-50kΩ) connects the signal line to VDD. When no external signal is present, the pin is "pulled" to a high level.
- **Pull-Down**: An internal resistor connects to VSS. When no external signal is present, the pin is "pulled" to a low level.

An ASCII diagram makes this more intuitive:

```text
浮空输入：              上拉输入：              下拉输入：

  引脚 ──→ 后级          引脚 ──→ 后级          引脚 ──→ 后级
                         │                      │
                        [R] ~40kΩ              [R] ~40kΩ
                         │                      │
                        VDD                    VSS

  引脚悬空时：            引脚悬空时：            引脚悬空时：
  电平不确定              高电平                  低电平
```

⚠️ Pay attention to the values of these resistors. According to the STM32F103 datasheet, the range for internal pull-up/pull-down resistors is 25-60kΩ, with a typical value of about 40kΩ. This resistance isn't small—it's only sufficient to provide a "default level" when there is no external drive; it cannot be used to drive any load. For our purposes, however, a 40kΩ pull-up resistor paired with a button is perfectly adequate.

### Third Stop: Schmitt Trigger

After passing through the pull-up/pull-down resistors, the signal arrives at the Schmitt trigger. This is the most sophisticated stage on the input path.

A Schmitt trigger is essentially a comparator with hysteresis. A standard comparator has a single threshold—if the input exceeds the threshold, it outputs high; if below, it outputs low. The problem is that if the input signal hovers near the threshold (even with just a few millivolts of noise), the output will toggle rapidly between 0 and 1—this is called "ringing."

The Schmitt trigger solves this problem with two thresholds:

- **Rising Threshold VT+**: When the signal changes from low to high, it must exceed this threshold to be considered "high". For the STM32F103 at 3.3V supply, the datasheet guarantees VIH(min) = 0.49×VDD ≈ 1.62V, so VT+ is around 1.6V.
- **Falling Threshold VT-**: When the signal changes from high to low, it must drop below this threshold to be considered "low". The datasheet guarantees VIL(max) = 0.35×VDD ≈ 1.16V. The actual hysteresis (VT+ - VT-) is typically about 0.06×VDD ≈ 200mV, so VT- is around 1.4V.

There is a "hysteresis window" of about 200mV between the two thresholds. Within this window, the output maintains its previous state:

```text
        VT+ ≈ 1.6V
        ──────────────  上升时，超过此阈值 → 输出变高
        | 迟滞窗口 |
        |  ≈ 200mV |
        ──────────────  下降时，低于此阈值 → 输出变低
        VT- ≈ 1.4V

  输入电压:  0V ─────────── 1.4V ── 1.6V ────── 3.3V
  输出:      低              保持    保持         高
```

Why is this useful? Imagine a 1.2V input signal sitting exactly between the two thresholds. A standard comparator might toggle its output constantly due to a few millivolts of noise. But a Schmitt trigger won't—at 1.2V, it maintains the previous state. The signal must clearly rise above 1.64V or fall below 0.82V for the output to change. This is the meaning of "hysteresis"—the system has a certain amount of "inertia" and does not react to small fluctuations.

The hysteresis of the Schmitt trigger and the mechanical bouncing of the button are **two different levels of problems**. The Schmitt trigger eliminates electrical noise near the threshold (millivolt level), whereas button bounce is a large-amplitude oscillation between 0V and 3.3V (volt level). The Schmitt trigger can't help with button bounce—during bouncing, the signal jumps between high and low levels, clearly exceeding both thresholds each time. Software debouncing is essential, and we will cover this in detail later.

### Fourth Stop: IDR Register

The output of the Schmitt trigger is ultimately connected to the `GPIOx_IDR` (Input Data Register). The `IDR` is a 16-bit read-only register, where bit 0 corresponds to Pin 0, bit 1 to Pin 1, and so on up to bit 15 for Pin 15. The value of each bit represents the level of the corresponding pin after being shaped by the Schmitt trigger—1 indicates high, 0 indicates low.

The CPU can read the `IDR` at any time to determine the current input state of all pins. The HAL library's `HAL_GPIO_ReadPin(GPIOx, GPIO_Pin)` function essentially reads the `IDR` register and performs a bitwise AND operation—`IDR & Pin` extracts the level value of the corresponding pin. It is very fast, completing in just one clock cycle. In the next post, we will fully dissect this function.

---

## Choosing Between Three Input Modes

Now that we understand the function of each stage on the input path, the question is: which input mode should we choose for our button?

### Floating Input — Not Recommended

Floating input does not enable internal pull-up or pull-down resistors. When the button is released, the PA0 pin is left floating, and the level is undefined. It could be high, low, or change simply because your hand moved near the pin (the human body is a conductor). This uncertainty means you cannot distinguish between "button released" and "button in an undefined state"—the read value is unreliable.

When is floating input suitable? It is suitable when external circuitry provides a definite level drive. For example, if an output pin from another chip is directly connected, it will drive the high or low level itself, and the STM32 does not need to provide a default level.

### Pull-Up Input — Our Choice

Pull-up input enables the internal pull-up resistor. When the button is released, PA0 is connected to VDD through a 40kΩ resistor, resulting in a high level (1). When the button is pressed, PA0 is connected directly to GND; current flows from VDD through the 40kΩ resistor to GND, pulling the PA0 voltage down to nearly 0V, resulting in a low level (0).

Released = High, Pressed = Low. This is known as "Active Low", corresponding to `GPIO_PIN_RESET` in our code. The vast majority of MCU button solutions use pull-up input because wiring to GND is more convenient than VCC—there are many GND pins on the Blue Pill board, making it easy to connect.

### Pull-Down Input — Alternative

Pull-down input enables the internal pull-down resistor. When the button is released, the pin is at a low level; when the button is pressed (connected to VCC), the pin is at a high level. Released = Low, Pressed = High, i.e., "Active High", corresponding to `GPIO_PIN_SET`.

Our button tutorial does not use the pull-down scheme. However, our Button template class supports both polarities—if you encounter an active-high button later, you only need to change the template parameter to `true`.

### Summary Table

| Mode | Internal Resistor | Default Level | Use Case |
|------|-------------------|---------------|----------|
| Floating | None | Undefined | External circuit provides definite signal |
| Pull-Up | To VDD ~40kΩ | High | Button→GND (Active Low) |
| Pull-Down | To VSS ~40kΩ | Low | Button→VCC (Active High) |

---

## CRL/CRH Registers: Low-Level Configuration

The HAL library encapsulates low-level register operations into `HAL_GPIO_Init`, so you don't need to manipulate registers directly. However, understanding the low level helps with debugging—when pin behavior doesn't meet expectations, checking the register configuration often quickly locates the problem.

Each GPIO port on the STM32F103 has two configuration registers: `CRL` controls Pin 0-7, and `CRH` controls Pin 8-15. Each pin occupies 4 bits: `MODE[1:0]` (2 bits) + `CNF[1:0]` (2 bits).

Configuration in input mode:

| MODE[1:0] | CNF[1:0] | Meaning |
|-----------|----------|---------|
| 00 | 00 | Analog input (for ADC) |
| 00 | 01 | Floating input |
| 00 | 10 | Pull-up / Pull-down input (direction determined by ODR bit) |

Complete configuration for pull-up input: `MODE=00, CNF=10, ODR bit=1` (ODR=1 means pull-up, ODR=0 means pull-down).

Note a point of confusion: in input mode, the bits in `ODR` are used to select the pull-up or pull-down direction, not to control the output level. This bit controls the output level in output mode, but controls the pull direction in input mode—the same register has different meanings in different modes.

When PA0 is configured as pull-up input, the low 4 bits of `GPIOA->CRL` should be `1000` (CNF=10, MODE=00), and bit 0 of `GPIOA->ODR` should be 1. HAL's `HAL_GPIO_Init` handles these bit field operations for you; you only need to pass in the correct `GPIO_InitTypeDef` structure.

---

## Correspondence with gpio.hpp

Let's map the hardware knowledge to the code. In `device/gpio/gpio.hpp`, the `GPIO` template's `setup()` method is responsible for configuring the pin:

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIOClock::enable_target_clock();
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

When using a button, `GPIO::Mode::Input` is called. `GPIO::Mode::Input` corresponds to `GPIO_MODE_INPUT` (0x00), and `GPIO::Pull::Up` corresponds to `GPIO_PULLUP` (0x01). HAL internally translates these two values into the CRL/CRH bit field configurations mentioned above.

The newly added `read()` method directly encapsulates the reading of `IDR`:

```cpp
[[nodiscard]] State read_pin_state() const {
    return static_cast<State>(HAL_GPIO_ReadPin(native_port(), PIN));
}
```

`GPIO::read()` reads `IDR`, and `GPIO::Level` converts `GPIO_PIN_SET`/`GPIO_PIN_RESET` into our `Level::High`/`Level::Low` enums. The `[[nodiscard]]` attribute is added because if you don't use the result of reading the pin state, the call is pointless—most likely, you forgot to write the assignment.

---

## Looking Back

In this post, starting from the pin, we traced the path through protection diodes, pull-up/pull-down resistors, the Schmitt trigger, and the `IDR` register to fully understand the signal chain of GPIO input mode. Three key takeaways:

1. **Pull-up input** is our button solution—high level when released, low level when pressed.
2. **Schmitt trigger** eliminates electrical noise near the threshold but cannot eliminate mechanical button bounce.
3. The **`IDR` register** is the window through which the CPU reads pin states; `HAL_GPIO_ReadPin` essentially reads it.

In the next post, we will apply our GPIO input knowledge to a real button circuit—drawing wiring diagrams, calculating current, and observing bounce waveforms. Once our hardware knowledge is ready, we can start writing code.
