---
title: 'Part 7: What Exactly is GPIO — The Past and Present of General-Purpose Input/Output'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 2
---
# Part 7: What Exactly is GPIO — The Past and Present of General-Purpose I/O

## Preface: From Environment Setup to Questioning the Essence

In the previous article, we discussed why we use modern C++ to program STM32 — the pain points of traditional development where C macros run rampant, and the changes that zero-overhead abstraction in modern C++ can bring. We also briefly surveyed the project's code structure and saw that the final `main.cpp` only needs a few lines of code to make an LED blink. But if you stop and think about it — we write C++ code, the code runs on a piece of silicon, and an LED is a physical device. What connects them in between? The answer is pins, or more precisely, GPIO pins.

GPIO stands for General-Purpose Input/Output. The name itself is very straightforward — it is general-purpose, not dedicated to any specific function, and it can both input and output. But the word "general-purpose" might create an illusion that it is simple, primitive, or even unimportant. The truth is exactly the opposite. GPIO is the most fundamental and direct channel for an MCU (Microcontroller Unit) to interact with the outside world. Almost every peripheral you will use later — serial communication, SPI bus, I2C bus, PWM motor control — their physical signals are ultimately output or input through GPIO pins. Understanding GPIO means understanding how the MCU "reaches out to touch the world."

You can think of GPIO as countless invisible hands reaching out from the MCU. These hands only do the simplest things — grab a high level, or let go to a low level. But when these hands act in specific timing sequences and combinations, they can accomplish extremely complex tasks like communication, control, and data acquisition. And everything starts with understanding how a single hand grabs and lets go.

What we need to do now is dive into the internal structure of this "hand" and see exactly how it works. Don't rush to look at the code just yet; let's start from the most fundamental physical questions.

## From LED Circuits to the Programming Model

Let's go back to the most fundamental physical question first: why does an LED light up?

The physical condition for an LED (Light Emitting Diode) to light up is actually very simple — as long as current flows from its positive terminal (anode) to its negative terminal (cathode), and the current is large enough (usually a few milliamps is sufficient for visibility), it will emit light. In a classic LED driver circuit, we connect VCC (positive power supply) through a current-limiting resistor to the LED's anode, and connect the LED's cathode to GND (ground). Current flows from VCC, through the resistor, through the LED, and back to GND, forming a complete circuit. The resistor's job is to limit the current to prevent the LED from burning out due to overcurrent.

This is a purely passive circuit. As long as the power is connected, the LED stays on, and you have no means of control.

Now, let's replace VCC with a pin on the MCU. When this pin outputs a high level (for STM32, that's a voltage close to 3.3V), a current path exists, and the LED turns on. When the pin outputs a low level (close to 0V), there is almost no voltage difference across the LED, no current flows, and the LED turns off. Just like that, we achieved control over the LED's on/off state by controlling the pin's level. Of course, you can also wire it the other way around — anode to pin, cathode to ground — in which case the LED only lights up when the pin outputs a high level. Both approaches are common in real projects, and the on-board LED on the STM32F103C8T6 minimum system board uses the active-low wiring, connected to the PC13 pin.

The next question is: how does the MCU pin "output" a high or low level? A pin is not a wire; it cannot generate voltage out of thin air. Behind the pin is an entire digital circuit — MOSFETs (Metal-Oxide-Semiconductor Field-Effect Transistors), registers, and multiplexers. The code we write simply writes a value to a specific memory address, and this value is translated by the hardware circuit into MOSFET conduction or cutoff. The MOSFET's conduction state determines whether the pin is at VDD (high level) or VSS (low level).

This is the GPIO programming model. We write code to tell the GPIO controller "I want this pin to output a high level," the GPIO controller operates the internal MOSFET, and the MOSFET changes the pin's physical voltage. From software to hardware, the signal goes through three layers of translation: registers, buses, and transistors. You will find that this programming model applies not only to LED control but to all digital signal interactions through GPIO. Button detection is the reverse process — an external signal changes the pin's voltage, and after sampling, GPIO tells the CPU. We will expand on this in detail shortly.

> ⚠️ Here is a pitfall that beginners are especially prone to falling into: many people assume that pins are in output mode by default and can directly control an LED right after power-on. But in reality, STM32 pins default to a floating input state after reset. If you forget to configure the pin as output before trying to control the LED, the pin will not output the level you expect, and the LED naturally won't light up. This is also why in our `led.hpp`, the LED constructor must first call `Base::setup(Base::Mode::OutputPP, ...)` to initialize the pin.

## Pin Grouping on the STM32F103C8T6

The STM32F103C8T6 chip uses an LQFP48 package, meaning it has 48 physical pins distributed around the chip's perimeter. But if you look closely at the datasheet, you'll find that not all 48 pins can serve as GPIO. Among them are dedicated pins like VDD (power), VSS (ground), VBAT (backup battery), NRST (reset), and BOOT0 (boot mode selection), leaving about 37 pins that can be used as GPIO.

These 37 GPIO pins are divided into five groups, named GPIOA, GPIOB, GPIOC, GPIOD, and GPIOE. Each group can contain up to 16 pins, numbered from 0 to 15. The STM32 designers chose the number 16 for a reason — 16 is exactly the width of a 16-bit register, which means a single 16-bit register can fully describe the state of every bit in one GPIO group, making the hardware design very clean.

The pin naming convention is "group name + number." For example, PA0 is pin number 0 of the GPIOA group, and PC13 is pin number 13 of the GPIOC group. The `GPIO_PIN_13` we use in our code is essentially a bit mask — `1 << 13`, which is `0x2000`. The HAL library uses this mask to identify the specific pin, allowing a single operation to affect multiple pins simultaneously.

In our project code, the `GpioPort` enum in `device/gpio/gpio.hpp` maps each GPIO group to its base address in memory:

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,  // 0x40010800
    B = GPIOB_BASE,  // 0x40010C00
    C = GPIOC_BASE,  // 0x40011000
    D = GPIOD_BASE,  // 0x40011400
    E = GPIOE_BASE,  // 0x40011800
};
```

You'll notice that the interval between these base addresses is `0x400` (1024 bytes), indicating that each GPIO group occupies 1KB of address space in memory. Within this 1KB space, seven registers are arranged that control the entire behavior of the 16 pins in that group. The two most critical configuration registers are CRL and CRH — CRL (Configuration Register Low) handles Pin0 through Pin7 (the lower 8 pins), and CRH (Configuration Register High) handles Pin8 through Pin15 (the upper 8 pins). Each pin occupies 4 bits in the configuration register (2 CNF configuration bits + 2 MODE mode bits), and 16 pins exactly consume two 32-bit registers.

Great, now we know the pin grouping and naming conventions. But what exactly can a pin do? That brings us to the four operating modes of GPIO.

> ⚠️ A common source of confusion: the chip is called STM32F103C8T6, so why is it sometimes written as STM32F103C8 and sometimes with the T6 suffix? Actually, C8 is the part number code, indicating 64KB of flash memory; T6 is the package code, indicating the LQFP48 package. The same part number with a different package (such as LQFP64 or LQFP100) will have a different number of available GPIO pins. So when you look up pin assignments, always confirm the package type.

## The Four Operating Modes of GPIO

Although GPIO stands for "General-Purpose Input/Output," its versatility goes far beyond simply "being able to output high/low levels and read high/low levels." The STM32F1 series GPIO supports four main operating modes: input, output, alternate function, and analog. Each mode exists out of necessity, corresponding to four fundamental needs of the MCU interacting with the outside world.

First, let's discuss Input mode. The core problem that input mode solves is "what is the outside world telling the MCU?" When a pin is configured as input, external signals enter the chip through the pin. The voltage on the pin first passes through a Schmitt Trigger for shaping — the Schmitt Trigger's job is to convert a potentially noisy analog signal (such as a slow rising edge with noise) into a clean digital signal, either a definitive 0 or a definitive 1, with no intermediate state. The shaped signal is then sampled into the Input Data Register (IDR). Our program can read the IDR to know whether the pin is currently at a high or low level. In input mode, you can also optionally enable the internal pull-up resistor or pull-down resistor: a pull-up resistor weakly connects the pin to VDD, making it default to a high level when floating; a pull-down resistor weakly connects the pin to VSS, making it default to a low level when floating; with neither pull-up nor pull-down enabled, the floating pin's level is indeterminate. This is crucial in button detection — if one end of your button is connected to the pin and the other end to ground, you need to enable the internal pull-up resistor, so that when the button is not pressed you read a high level, and when pressed you read a low level, yielding a clear and reliable state. Why does input mode need to exist? Because an MCU cannot always "talk to itself" by outputting signals; it must be able to sense state changes in the outside world — whether a button has been pressed, whether a sensor has issued an alarm, whether another chip has sent a ready signal — these are all use cases for input mode.

Next is Output mode. The core problem that output mode solves is "what is the MCU telling the outside world?" When a pin is configured as output, the chip actively drives the pin to a high or low level. Output mode has two subtypes: push-pull and open-drain. Push-pull mode uses two MOSFETs — a P-MOS upper transistor connected to VDD and an N-MOS lower transistor connected to VSS — to actively drive in both directions. When outputting a high level, the upper transistor conducts and the lower transistor cuts off, pulling the pin to VDD; when outputting a low level, the upper transistor cuts off and the lower transistor conducts, pulling the pin to VSS. The two transistors alternate their operation like pushing and pulling, hence the name "push-pull." Push-pull mode has strong driving capability and can source and sink relatively large currents. Open-drain mode, on the other hand, only has the N-MOS lower transistor working. When outputting a low level, the lower transistor conducts and pulls the pin to VSS, but when outputting a high level, the lower transistor also cuts off, leaving the pin in a high-impedance state (floating), unable to actively pull high. To output a high level, an external pull-up resistor must be connected. A typical application scenario for open-drain output is the I2C bus — multiple devices share the same signal line, and any device can pull the line low, but no device actively pushes the line high (to avoid bus conflicts), with the high level provided by an external pull-up resistor. LED control typically uses push-pull output, which is why we chose `Mode::OutputPP` in `led.hpp`. Why does output mode need to exist? Because the MCU must be able to actively change the state of external circuits — lighting up LEDs, driving relays, generating clock signals — all of these require the pin to have the ability to actively output a definite level.

Then there is Alternate Function mode. This mode exists because STM32 integrates a large number of on-chip peripherals — USART serial ports, SPI buses, I2C buses, timer PWM outputs, and so on — and these peripherals need physical pins to send and receive signals, but the chip's pin count is limited. The solution is pin multiplexing: the same physical pin can assume different roles at different times. When a pin is configured as alternate function mode, it is no longer directly controlled by the GPIO controller but is handed over to the corresponding on-chip peripheral to drive. For example, PA9 and PA10 can be configured as the TX (transmit) and RX (receive) pins of USART1, at which point they are no longer regular GPIO but serial communication signal lines. Once configured, the code operates on the USART peripheral's registers rather than the GPIO registers, and the pin's signals are automatically generated by the USART hardware. In `gpio.hpp`, this corresponds to `Mode::AfPP` (alternate function push-pull) and `Mode::AfOD` (alternate function open-drain). Why does alternate function mode need to exist? Because pins are a scarce resource. A 48-pin chip only has a little over 30 pins available as GPIO, but the on-chip peripherals combined might need 50 to 60 signal lines. Without multiplexing, the chip's pin count would bloat to an unacceptable level.

Finally, there is Analog mode. Analog mode is used for connecting to on-chip ADC (Analog-to-Digital Converter) or DAC (Digital-to-Analog Converter). In analog mode, the pin's digital functions are completely disabled — the Schmitt Trigger is disabled, the Input Data Register (IDR) does not update, and the analog signal on the pin goes directly through an internal path to the ADC for sampling. Why does analog mode need to exist? Because the presence of the Schmitt Trigger introduces additional current consumption and signal distortion. When you need to read precise analog voltages (such as millivolt-level signals from a temperature sensor), these digital circuits become sources of interference instead. So analog mode is essentially "turning off all digital logic and letting the pin return to its purest analog state." In `gpio.hpp`, this corresponds to `Mode::Analog`.

> ⚠️ Pitfall warning: Many beginners find that pin behavior is incorrect after configuring GPIO, only to discover that the mode was configured wrong. The most common mistake is configuring a pin that should be in alternate function mode as a regular output mode — for example, wanting to use PA9 as USART1_TX but configuring it as `GPIO_MODE_OUTPUT_PP`, resulting in the serial port being unable to send data. Alternate functions must use `GPIO_MODE_AF_PP` or `GPIO_MODE_AF_OD`, which tells the multiplexer to hand the pin over to the peripheral.

## GPIO Internal Block Diagram

We've described the four modes in text, but to truly understand how GPIO works, an internal block diagram is worth a thousand words. Below is an ASCII-art diagram of the STM32F1 series GPIO pin internal structure. Please note that this is a simplified conceptual diagram that omits some details (such as output speed control), but the core signal paths are accurate.

```text
                         VDD (3.3V)
                           |
                       [上拉电阻]
                           |      (可配置开关)
            ┌──────────────┤
            |              |
            |          +---+---+
            |          |       |
 引脚 Pin ──┤────[保护二极管]──┤
            |          |       |
            |          | [P-MOS 上管]
            |          |       |
            |          +---+---+
            |              |         ┌──────────┐
            |              +─────────┤ 输出     ├─── ODR (输出数据寄存器)
            |              |         │ 驱动器   │        ↑
            |          +---+---+     └──────────┘        |
            |          |       |              ↑    [多路选择器 MUX]
            |          | [N-MOS 下管]         |         ↑
            |          |       |        ┌─────┴─────────┤
            |          +---+---+        │               │
            |              |      [CRL/CRH       复用功能输入
            |          [下拉电阻]     配置寄存器]   ←── 片上外设
            |              |
            |             VSS (0V)
            |
            |         ┌────+────┐
            |         | 施密特   |
            +─────────┤ 触发器   |
                      └────+────┘
                           |
                           ↓
                      IDR (输入数据寄存器)
```

Don't let this diagram intimidate you; let's break it down block by block.

**Protection diodes** are the pin's first line of defense, and also the most easily overlooked part. They are connected between the pin and VDD/VSS, forming a clamping circuit. Under normal operating conditions, the pin voltage is between 0V and 3.3V, and neither protection diode conducts, having no effect on the circuit. But if an abnormality occurs in the external circuit — for example, if 5V is applied to the pin — the upper protection diode will conduct, shunting the excess energy to the VDD power rail and preventing the internal circuitry from being damaged by overvoltage. Similarly, if the pin is pulled to a negative voltage, the lower protection diode will conduct, clamping the pin to VSS. This is a very simple but highly effective protection mechanism. However, the current that protection diodes can withstand is limited, typically specified as injection current in the datasheet, and sustained high current could burn out the diodes. The correct approach is to use a level-shifting chip or a current-limiting resistor for isolation.

**Pull-up and pull-down resistors** are two configurable internal resistors. Note that they are not always connected — whether they are enabled is determined by the configuration bits in the CRL/CRH registers. When a pin is configured in "input pull-up" mode, the switch for the pull-up resistor between VDD and the pin is closed, and the pin is connected to VDD through an internal resistor of approximately 40K ohms. This means the pin will be weakly pulled to a high level when floating. Similarly, in "input pull-down" mode, the pin is connected to VSS through a similar resistor. The resistance of these two resistors is relatively large (in the 30K-50K range), so the pulling force is weak — if there is a stronger external driver (such as a button press directly connecting to GND), the external drive will easily override the effect of the internal pull-up.

**The Schmitt Trigger** is located on the input signal path. Its role is crucial. Signals from the outside world are rarely perfect square waves — they may rise slowly, have glitches, or oscillate near the threshold. If such signals were used to directly trigger digital circuits, it would lead to serious misjudgments. The Schmitt Trigger solves this problem by introducing hysteresis: its rising threshold (for example, 1.7V) and falling threshold (for example, 0.9V) are different. A signal going from low to high must exceed 1.7V to be considered "high," and going from high to low must fall below 0.9V to be considered "low." The region between 0.9V and 1.7V is the "uncertain zone," where the output holds its last determined state unchanged. This design greatly improves noise margin. In analog mode, the Schmitt Trigger is turned off, and the analog signal connects directly to the ADC without being digitized.

**The output driver** is the core of push-pull output. It consists of a P-MOS upper transistor and an N-MOS lower transistor, with the gates of both transistors controlled by the corresponding bit of the Output Data Register (ODR) (after passing through the multiplexer). When a certain bit in the ODR is written as 1, the upper transistor conducts and the lower transistor cuts off, driving the pin to VDD (high level). When a certain bit in the ODR is written as 0, the upper transistor cuts off and the lower transistor conducts, driving the pin to VSS (low level). In open-drain output mode, the P-MOS upper transistor is permanently cut off, and only the N-MOS lower transistor works. The output speed control (MODE bits) actually controls the slew rate of the output driver — the faster the speed, the more rapidly the MOSFETs switch, the steeper the signal edges, but this also generates greater EMI (Electromagnetic Interference) and power supply noise. This is also why we chose `Speed::Low` in `led.hpp` — LED blinking doesn't need high-speed toggling, and a low speed also reduces unnecessary electromagnetic emissions.

**The multiplexer (MUX)** is the "traffic cop" for pin control authority. It decides where the pin's output drive signal comes from: from the GPIO controller's ODR register (regular GPIO output), or from an on-chip peripheral (alternate function output). This selection is determined by the CNF bits in the CRL/CRH registers. When CNF is configured for alternate function, the MUX connects the peripheral's output signal to the driver, and the ODR's control is bypassed. This is why, after configuring an alternate function, you no longer need to manually manipulate the ODR — the peripheral hardware automatically controls the pin's signal.

**The CRL/CRH configuration registers** are the "control center" of the entire GPIO. Every 4 bits control one pin's MODE (speed/output enable) and CNF (specific mode configuration). We will analyze the bit field meanings of these registers in detail shortly.

## The Relationship Between Pins and Registers

After understanding the internal structure of GPIO, let's now turn our attention to the registers that are actually manipulated by the program. Each GPIO group (GPIOA through GPIOE) has seven 32-bit registers in the memory address space, arranged at fixed offsets. Let's use GPIOC as an example — because our LED is connected to PC13.

The base address of GPIOC is `0x40011000`. This address is not arbitrarily assigned — it lies within the STM32's APB2 (Advanced Peripheral Bus) address space, and all GPIO peripherals are attached to the APB2 bus. Starting from the base address, the seven registers are arranged as follows.

**The CRL register (offset 0x00, full address 0x40011000)** is responsible for configuring Pin0 through Pin7, the eight lower-numbered pins. This is a 32-bit register where every 4 bits control one pin, corresponding to Pin0, Pin1, ..., Pin7 from least significant bit to most significant bit. Within each 4-bit group, the lower 2 bits are called MODE, and the upper 2 bits are called CNF. The MODE bits determine the pin's output speed (in output mode) or input mode flag (in input mode, MODE=00). The CNF bits determine the specific sub-mode — for example, in input mode whether it is floating input or pull-up input, and in output mode whether it is push-pull or open-drain.

**The CRH register (offset 0x04, full address 0x40011004)** is completely symmetrical to CRL, except that it handles Pin8 through Pin15, the eight higher-numbered pins. The structure is identical — every 4 bits control one pin, corresponding to Pin8, Pin9, ..., Pin15 from least significant bit to most significant bit.

Let's do the math using our PC13 as an example. PC13 is pin number 13 of the GPIOC group, and since 13 >= 8, it is controlled by the CRH register. In CRH, Pin8 occupies bits [3:0], Pin9 occupies bits [7:4], and so on. PC13 corresponds to the (13-8)=5th group of 4 bits, which is bits [23:20] of CRH. If we want to configure PC13 as push-pull output at 2MHz, the MODE bits should be `10` (2MHz), and the CNF bits should be `00` (general-purpose push-pull output), combined as `0010`, written to bits [23:20] of CRH. The `HAL_GPIO_Init()` function in the HAL library is essentially doing these bit-field operations for us under the hood. The `Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low)` we call in `gpio.hpp` ultimately writes these values to bits [23:20] of CRH through the HAL library.

**The IDR register (offset 0x08, full address 0x40011008)** is the Input Data Register, a read-only register. Its lower 16 bits correspond to the current level state of Pin0 through Pin15, respectively. If Pin13 is currently at a high level, bit 13 of the IDR is 1; if it is at a low level, bit 13 is 0. When you read a button state in input mode, the underlying operation is reading this register. Regardless of what mode the pin is configured in (except analog mode), the IDR continuously reflects the actual level state on the pin.

**The ODR register (offset 0x0C, full address 0x4001100C)** is the Output Data Register, which is both readable and writable. In GPIO output mode, each bit of the ODR directly controls the level of the corresponding pin. Writing 1 outputs a high level, and writing 0 outputs a low level. But directly modifying the ODR has a hidden danger — a read-modify-write operation on the ODR is not atomic. If your program is interrupted while modifying Pin13, and the interrupt handler modifies another pin in the same group (such as Pin12), then Pin12's modification may be overwritten when the interrupt returns. To solve this problem, STM32 designed the BSRR and BRR registers.

**The BSRR register (offset 0x10, full address 0x40011010)** is the Port Bit Set/Reset Register, which provides an atomic way to modify the ODR. The lower 16 bits (bit0 to bit15) of BSRR are "set bits" — writing a 1 to a certain bit sets the corresponding ODR bit to 1 (pin outputs high level), while writing 0 has no effect. The upper 16 bits (bit16 to bit31) of BSRR are "reset bits" — writing a 1 to a certain bit clears the corresponding ODR bit to 0 (pin outputs low level), while writing 0 has no effect. The key point is that this operation is atomic — no read-modify-write is needed, and a single write can precisely control the specified bits without affecting other bits.

For example, to make PC13 output a high level, we can write `0x2000` to BSRR (setting bit 13 to 1), and to output a low level we write `0x20000000` (setting bit 29, which is 13+16, to 1). This is the underlying implementation logic of `HAL_GPIO_WritePin()`, and also the hardware operation ultimately called by the `set_gpio_pin_state()` method in our `gpio.hpp`.

**The BRR register (offset 0x14, full address 0x40011014)** is the Port Bit Reset Register, functionally equivalent to taking the upper 16 bits of BSRR out separately — writing a 1 to the lower 16 bits clears the corresponding ODR bit. It was commonly used in early firmware libraries, but with BSRR available, BRR became redundant because BSRR already covers both set and reset operations.

**The LCKR register (offset 0x18, full address 0x40011018)** is the Configuration Lock Register. Its purpose is to lock the GPIO configuration — once locked, the corresponding CRL/CRH bits cannot be modified again until the next system reset. This is very useful in production-level code: after initialization is complete, lock the configuration to prevent the program from accidentally modifying GPIO settings and causing hardware damage if it runs astray. The locking operation requires following a specific write sequence, which is a kind of anti-misoperation protection mechanism in the hardware design.

> ⚠️ Pitfall warning: When using the BSRR register, remember the rule "writing 1 takes effect, writing 0 has no effect." This means you can safely write any value to BSRR without worrying about accidentally affecting other pins. But if you directly manipulate the ODR register, you must use a read-modify-write approach, which is unsafe in multithreaded or interrupt environments. Therefore, a good habit in embedded development is to prefer using BSRR to control output pins.

## Wrapping Up and a Preview

At this point, we have traversed the complete chain of GPIO, from physical circuits to the programming interface. We know that GPIO has four operating modes — input, output, alternate function, and analog — and each mode corresponds to a specific hardware signal path and register configuration, with each mode's existence serving an irreplaceable purpose. Through the internal block diagram, we saw how hardware units like protection diodes, the Schmitt Trigger, the push-pull driver, and the multiplexer work together. We also went through the addresses, offsets, and functions of the seven key registers (CRL, CRH, IDR, ODR, BSRR, BRR, LCKR) one by one, and using PC13 as a concrete example, we traced the complete path from C++ code to the underlying registers — from the `0x2000` bit mask of `GPIO_PIN_13`, to bits [23:20] of CRH, to the atomic operations of BSRR, every step corresponding to actual hardware behavior.

GPIO is the foundation of embedded development. The serial communication, SPI bus, I2C protocol, PWM control, and ADC sampling that we will cover later are all built on top of GPIO. Alternate function mode allows pins to "transform" into channels for various peripherals, and analog mode allows pins to handle continuous voltage signals, but regardless of the mode, the pin's physical structure, protection mechanisms, and configuration methods are all interconnected. Once you understand GPIO, you hold the key to understanding the entire STM32 peripheral system.

In the next article, we will focus on the specific scenario of LED control. We will dive deep into the working details of push-pull output mode — how the P-MOS and N-MOS alternately conduct, what the output speed setting means, and why `Speed::Low` is sufficient for LED control. More importantly, we will look at the special circuit design of PC13 on the Blue Pill development board — why is the on-board LED active-low instead of active-high? What kind of circuit considerations lie behind this seemingly counterintuitive design? Once you understand these, you will see why we need the `ActiveLevel::Low` template parameter in our `led.hpp`, and how it cleverly encapsulates hardware differences.
