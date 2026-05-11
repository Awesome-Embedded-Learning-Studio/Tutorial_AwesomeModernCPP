---
title: 'Part 20: GPIO Input Mode Internal Circuitry — How the Chip "Hears" External
  Signals'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 2
---
# Part 20: GPIO Input Mode Internal Circuitry — How the Chip "Hears" External Signals

> Following up on the previous article: buttons are harder than LEDs in three ways—reading instead of writing, physical noise, and timing management. In this article, we tackle the first problem: how does GPIO input mode actually work?

---

## From the Output Path to the Input Path

In the LED tutorial, we spent a lot of time understanding the internal circuitry of GPIO output mode. The core signal path for output mode is:

```text
CPU 写入 ODR → 输出驱动器（推挽/开漏） → GPIO 引脚 → 外部电路
```

When the CPU writes a 1 to a specific bit in `ODR` (Output Data Register), the corresponding push-pull driver pulls the pin to VDD (high level); writing a 0 pulls it to VSS (low level). The signal flows from inside the chip to the outside world—the chip is the active party.

Now we need to reverse this path. In button mode, the signal flows from the outside world into the chip:

```text
GPIO 引脚 → 保护二极管 → 上拉/下拉电阻（可选） → 施密特触发器 → IDR → CPU 可读
```

Notice that the signal direction has changed. The voltage on the pin is no longer controlled by the CPU—it is determined by the external circuit (in our scenario, by the button being pressed or released). The CPU's role shifts from "writing to the ODR" to "reading the IDR"—passively observing changes in the pin's logic level.

---

## Every Stop Along the Input Path

Let's follow the signal path, starting from the pin and moving inward, to see what each stage does.

### First Stop: Protection Diodes

Immediately after the pin are two protection diodes—one connected to VDD and one to VSS. Their job is clamping—if the voltage on the pin exceeds VDD + 0.6V, the upper diode conducts and bleeds the excess voltage to VDD; if it drops below VSS - 0.6V, the lower diode conducts and bleeds to VSS.

This layer of protection isn't the focus for button scenarios—button voltages are simply 0V or 3.3V, well within range. But if you connect a sensor or other device that might generate abnormal voltages, these two diodes are the chip's first line of defense against being burned out. STM32 pins can withstand voltages in the range of -0.3V to VDD + 0.3V (beyond this, the protection diodes kick in), with an absolute maximum of 4.0V (beyond which, the chip is genuinely damaged).

### Second Stop: Pull-Up / Pull-Down Resistors

Past the protection diodes, the signal arrives at a fork in the road. There are three options here:

- **Floating (No Pull)**: Both pull-up and pull-down resistors are disconnected. The pin level is entirely determined by the external circuit. If nothing is connected externally (the pin is floating), the level is undefined—subject to electromagnetic interference, it may randomly jump between high and low.
- **Pull-Up**: An internal resistor (approximately 30-50kΩ) connects the signal line to VDD. With no external signal, the pin is "pulled" to a high level.
- **Pull-Down**: An internal resistor connects to VSS. With no external signal, the pin is "pulled" to a low level.

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

⚠️ Note the resistance values. According to the STM32F103 datasheet, the internal pull-up/pull-down resistors range from 25-60kΩ, with a typical value of about 40kΩ. This resistance isn't trivial—it's sufficient to provide a "default level" when there is no external drive, but it cannot be used to drive any load. For our purposes, however, a 40kΩ pull-up resistor paired with a button is perfectly adequate.

### Third Stop: Schmitt Trigger

After passing through the pull-up/pull-down resistors, the signal arrives at the Schmitt trigger. This is the most ingenious stage in the input path.

A Schmitt trigger is essentially a comparator with hysteresis. A standard comparator has only one threshold—if the input exceeds the threshold, it outputs high; if below, it outputs low. The problem is that if the input signal happens to fluctuate right around the threshold (even with just a few millivolts of noise), the output will rapidly toggle between 0 and 1—this is known as "ringing."

The Schmitt trigger solves this problem using two thresholds:

- **Rising threshold VT+**: When the signal transitions from low to high, it must exceed this threshold to be considered "high." For the STM32F103 at 3.3V supply, the datasheet guarantees VIH(min) = 0.49×VDD ≈ 1.62V, so VT+ is around 1.6V.
- **Falling threshold VT-**: When the signal transitions from high to low, it must fall below this threshold to be considered "low." The datasheet guarantees VIL(max) = 0.35×VDD ≈ 1.16V. The actual hysteresis (VT+ - VT-) has a typical value of about 0.06×VDD ≈ 200mV, so VT- is around 1.4V.

Between the two thresholds lies a "hysteresis window" of about 200mV. Within this window, the output holds its previous state unchanged:

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

Why is this useful? Imagine a 1.2V input signal sitting right between the two thresholds. A standard comparator might endlessly flip its output due to a few millivolts of noise. But a Schmitt trigger won't—at 1.2V, it simply holds its previous state. The signal must clearly rise above 1.64V or drop below 0.82V for the output to change. This is the meaning of "hysteresis"—the system has a certain "inertia" and does not react to small fluctuations.

The hysteresis of the Schmitt trigger and the mechanical bounce of a button are **two entirely different levels of problems**. The Schmitt trigger eliminates electrical noise near the threshold (millivolt level), whereas button bounce is a large-scale oscillation of the entire signal between 0V and 3.3V (volt level). The Schmitt trigger cannot help with button bounce—during bouncing, the signal swings back and forth between high and low levels, clearly crossing both thresholds each time. Software debouncing is mandatory, and we will cover this in detail later.

### Fourth Stop: The IDR Register

The output of the Schmitt trigger ultimately connects to `GPIOx_IDR` (Input Data Register). `IDR` is a 16-bit read-only register, where bit 0 corresponds to Pin 0, bit 1 to Pin 1, and so on up to bit 15 for Pin 15. The value of each bit is the logic level of the corresponding pin after being shaped by the Schmitt trigger—1 for high, 0 for low.

The CPU can read `IDR` at any time to determine the current input state of all pins. Under the hood, the HAL library's `HAL_GPIO_ReadPin(GPIOx, GPIO_Pin)` simply reads the `IDR` register and performs a bitwise AND operation—`IDR & Pin` extracts the logic level of the corresponding pin. It is extremely fast, completing in a single clock cycle. We will fully dissect this function in the next article.

---

## Choosing Among the Three Input Modes

Now that we understand what each stage along the input path does, the question becomes: which input mode should we use for our button?

### Floating Input — Not Recommended

Floating input does not enable the internal pull-up or pull-down resistors. When the button is released, the PA0 pin is floating, and its level is undefined. It could be high, it could be low, or it could change simply because your hand moved near the pin (the human body is conductive). This uncertainty means you cannot distinguish between "button released" and "button in an undefined state"—the read value is unreliable.

When is floating input appropriate? It suits scenarios where the external circuit provides its own definitive level drive. For example, if an output pin from another chip is connected directly, it will drive high or low on its own, and the STM32 does not need to provide a default level.

### Pull-Up Input — Our Choice

Pull-up input enables the internal pull-up resistor. When the button is released, PA0 is connected to VDD through a 40kΩ resistor, and it reads as a high level (1). When the button is pressed, PA0 is connected directly to GND, current flows from VDD through the 40kΩ resistor to GND, and the PA0 voltage is pulled to near 0V, reading as a low level (0).

Released = high, pressed = low. This is known as "Active Low," corresponding to `ButtonActiveLevel::Low` in our code. The vast majority of MCU button schemes use pull-up input because wiring to GND is more convenient than wiring to VCC—there are many GND pins on the Blue Pill board, making it easy to connect.

### Pull-Down Input — An Alternative

Pull-down input enables the internal pull-down resistor. When the button is released, the pin is at a low level; when pressed (connected to VCC), the pin is at a high level. Released = low, pressed = high, meaning "Active High," corresponding to `ButtonActiveLevel::High`.

Our button tutorial does not use the pull-down scheme. However, our Button template class supports both polarities—if you later encounter an active-high button, you simply need to change the template parameter to `ButtonActiveLevel::High`.

### Summary Table

| Mode | Internal Resistor | Default Level | Use Case |
|------|---------|---------|---------|
| Floating | None | Undefined | External circuit provides a definitive signal source |
| Pull-Up | Connected to VDD ~40kΩ | High level | Button→GND (Active Low)|
| Pull-Down | Connected to VSS ~40kΩ | Low level | Button→VCC (Active High)|

---

## CRL/CRH Registers: Low-Level Configuration

The HAL library encapsulates low-level register operations into `HAL_GPIO_Init()`, so you don't need to manipulate registers directly. However, understanding the low level helps with debugging—when pin behavior doesn't match expectations, checking the register configuration often quickly pinpoints the issue.

Each GPIO port on the STM32F103 has two configuration registers: `CRL` controls Pin 0-7, and `CRH` controls Pin 8-15. Each pin occupies 4 bits: `MODE[1:0]` (2 bits) + `CNF[1:0]` (2 bits).

Configuration for input modes:

| MODE[1:0] | CNF[1:0] | Meaning |
|-----------|----------|------|
| 00 | 00 | Analog input (for ADC) |
| 00 | 01 | Floating input |
| 00 | 10 | Pull-up/pull-down input (direction determined by the corresponding bit in ODR) |

The complete configuration for pull-up input: `MODE=00, CNF=10, ODR bit=1` (ODR=1 means pull-up, ODR=0 means pull-down).

Note an easily confused point: in input mode, the bits in `ODR` are used to select the pull-up or pull-down direction, not to control the output level. This bit controls the output level in output mode, but controls the pull direction in input mode—the same register has different meanings in different modes.

When PA0 is configured as a pull-up input, the low 4 bits of `GPIOA->CRL` should be `1000` (CNF=10, MODE=00), and bit 0 of `GPIOA->ODR` should be 1. HAL's `HAL_GPIO_Init()` handles these bit-field operations for you; you simply need to pass in the correct `GPIO_InitTypeDef` structure.

---

## Correspondence with gpio.hpp

Let's map the hardware knowledge to the code. In `device/gpio/gpio.hpp`, the `setup()` method of the `GPIO` template is responsible for configuring the pin:

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

When used with a button, we call `setup(Mode::Input, PullPush::PullUp, Speed::Low)`. `Mode::Input` corresponds to `GPIO_MODE_INPUT` (0x00), and `PullPush::PullUp` corresponds to `GPIO_PULLUP` (0x01). Internally, HAL translates these two values into the CRL/CRH bit-field configuration described above.

The newly added `read_pin_state()` method directly encapsulates reading from `IDR`:

```cpp
[[nodiscard]] State read_pin_state() const {
    return static_cast<State>(HAL_GPIO_ReadPin(native_port(), PIN));
}
```

`HAL_GPIO_ReadPin()` reads `IDR`, and `static_cast` converts `GPIO_PIN_SET`/`GPIO_PIN_RESET` into our `State::Set`/`State::UnSet` enums. We added `[[nodiscard]]` because if you don't use the result of reading the pin state, the call is pointless—most likely, you forgot to write the assignment.

---

## Looking Back

In this article, starting from the pin, we traced the path through the protection diodes, pull-up/pull-down resistors, Schmitt trigger, and `IDR` register to fully understand the complete signal chain of GPIO input mode. Three key takeaways:

1. **Pull-up input** is our button scheme—high level when released, low level when pressed
2. The **Schmitt trigger** eliminates electrical noise near the threshold, but it cannot eliminate the mechanical bounce of a button
3. The **`IDR` register** is the window through which the CPU reads pin states, and `HAL_GPIO_ReadPin()` reads it under the hood

In the next article, we will apply our GPIO input knowledge to an actual button circuit—drawing the wiring diagram, calculating current, and observing bounce waveforms. Once the hardware knowledge is in place, we can start writing code.
