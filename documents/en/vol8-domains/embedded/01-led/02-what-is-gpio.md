---
chapter: 15
difficulty: beginner
order: 2
platform: stm32f1
reading_time_minutes: 24
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 7: What is GPIO? — The Past and Present of General-Purpose I/O'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/02-what-is-gpio.md
  source_hash: 7cef3eaaa9815dfc0a9b11314d557c275abea933276231c54d6243b7a71d35ea
  translated_at: '2026-06-16T04:09:34.556614+00:00'
  engine: anthropic
  token_count: 2848
---
# Part 7: What Exactly is GPIO — The Past and Present of General-Purpose I/O

## Preface: From Environment Setup to Exploring the Essence

In the previous post, we discussed why we use modern C++ for STM32—the pain points of traditional development methods where C macros fly everywhere, and the changes that modern C++'s zero-overhead abstractions can bring. We also skimmed through the project's code structure and saw that the final `main.cpp` only needs a few lines of code to make an LED blink. But if you stop and think—we write C++ code, the code runs on a piece of silicon, and the LED is a physical device. What connects them in the middle? The answer is pins, or more accurately, GPIO pins.

GPIO stands for General Purpose Input/Output. The name itself is quite straightforward—it is general-purpose, not belonging to any specific function; it can both input and output. However, the word "general" might create an illusion that it is simple, primitive, or even somewhat unimportant. The reality is exactly the opposite. GPIO is the most fundamental and direct channel for the microcontroller to interact with the outside world. Almost all peripherals you will use later—serial communication, SPI bus, I2C bus, PWM motor control—their physical signals are ultimately output or input through GPIO pins. Understanding GPIO is understanding how the microcontroller "reaches out its hands to touch the world."

You can think of GPIO as countless invisible hands extended by the microcontroller. These hands only do the simplest things—grab a high level, or let go to a low level. But when these hands act according to specific timing and specific combinations, they can accomplish extremely complex tasks like communication, control, and acquisition. And everything starts with understanding how one hand grabs and lets go.

What we are going to do now is dive deep into the internal structure of this "hand" to see exactly how it works. Before rushing to look at the code, let's start with the most fundamental physical questions.

## From LED Circuits to the Programming Model

Let's return to the most fundamental physical question first: Why does an LED light up?

The physical condition for an LED (Light Emitting Diode) to light up is actually very simple—as long as current flows from its positive terminal (anode) to its negative terminal (cathode), and the current is large enough (usually a few milliamps is sufficient for visibility), it will emit light. In a classic LED driver circuit, we connect VCC (positive power supply) to the LED's anode through a current-limiting resistor, and the LED's cathode is connected to GND (ground). Current flows from VCC, through the resistor, through the LED, and back to GND, forming a complete loop. The resistor's job is to limit the current magnitude to prevent the LED from burning out due to overcurrent.

This is a purely passive circuit. As long as the power is connected, the LED stays lit, and you have no means of control.

Now, let's replace VCC with a pin from the microcontroller. When this pin outputs a high level (for STM32, that is a voltage close to 3.3V), a path for current is established, and the LED lights up. When the pin outputs a low level (close to 0V), there is almost no voltage difference across the LED, no current flows, and the LED turns off. Thus, by controlling the level state of the pin, we achieve control over the LED's on/off state. Of course, you can also connect it the other way around—anode to pin, cathode to ground—in which case the LED lights up when the pin outputs a high level. Both methods are common in actual projects, and the onboard LED on the STM32F103C8T6 minimum system board uses the low-level-active connection, connected to the PC13 pin.

Next question: How does a microcontroller pin "output" a high or low level? A pin is not a wire; it cannot generate voltage out of thin air. Behind the pin is a complete set of digital circuits—MOSFETs (Metal-Oxide-Semiconductor Field-Effect Transistors), registers, and multiplexers. The code we write simply writes a value to a specific memory address. This value is translated by the hardware circuit into the conduction or cutoff of MOSFETs, and the conduction state of the MOSFETs determines whether the pin is at VDD (high level) or VSS (low level).

This is the programming model of GPIO. We write code to tell the GPIO controller "I want this pin to output a high level," the GPIO controller operates the internal MOSFETs, and the MOSFETs change the physical voltage of the pin. From software to hardware, the signal goes through three layers of translation: registers, buses, and transistors. You will find that this programming model applies not only to LED control but to all digital signal interactions via GPIO. Button detection is the reverse process—external signals change the pin voltage, and the GPIO samples it and informs the CPU. We will expand on this shortly.

⚠️ **Here is a pitfall beginners often step into:** Many people assume that pins are in output mode by default and can directly control LEDs upon power-up. But in reality, STM32 pins default to a floating input state after reset. If you forget to configure the pin as an output before controlling the LED, the pin will not output the level you expect, and the LED naturally won't light up. This is also why in our `Led` class, the LED constructor must first call `init()` to initialize the pin.

## Pin Grouping on STM32F103C8T6

The STM32F103C8T6 chip uses the LQFP48 package, meaning it has 48 physical pins distributed around the perimeter of the chip. However, if you look closely at the datasheet, you will find that not all 48 pins can function as GPIO. Among them are dedicated pins like VDD (power), VSS (ground), VBAT (backup battery), NRST (reset), BOOT0 (boot mode selection), etc. The remaining pins that can act as GPIO number about 37.

These 37 GPIO pins are divided into 5 groups, named GPIOA, GPIOB, GPIOC, GPIOD, and GPIOE. Each group can contain up to 16 pins, numbered from 0 to 15. The designers of STM32 chose the number 16 not arbitrarily—16 is exactly the width of a 16-bit register. This means a single 16-bit register can fully describe the state of every bit in a group, making the hardware design very clean.

The naming rule for pins is "Group Name + Number". For example, PA0 is pin number 0 of group GPIOA, and PC13 is pin number 13 of group GPIOC. The `GPIO_PIN_13` we use in our code is essentially a bit mask—`0x00002000`, which is `1 << 13`. The HAL library uses this mask to identify specifically which pin is being manipulated, allowing a single operation to affect multiple pins at once.

In our project code, the `Port` enum in `hal.hpp` maps each GPIO group to its base address in memory:

```cpp
enum class Port {
    A = 0x40010800,
    B = 0x40010C00,
    C = 0x40011000,
    D = 0x40011400,
    E = 0x40011800,
    // ...
};
```

You will notice that the interval between these base addresses is `0x400` (1024 bytes), indicating that each GPIO group occupies 1KB of address space in memory. Within this 1KB space, 7 registers are arranged, controlling the entire behavior of the 16 pins in this group. The two most critical configuration registers are CRL and CRH—CRL (Configuration Register Low) is responsible for Pin0 to Pin7 (the low 8 pins), and CRH (Configuration Register High) is responsible for Pin8 to Pin15 (the high 8 pins). Each pin occupies 4 bits in the configuration register (2 CNF configuration bits + 2 MODE mode bits), and 16 pins exactly use up two 32-bit registers.

Great, now we know the grouping and naming rules for pins. But what exactly can a pin do? This depends on the four working modes of GPIO.

⚠️ **A common confusion:** The chip is called STM32F103C8T6, so why is it sometimes written as STM32F103C8 and sometimes with a T6 added? Actually, C8 is the model code, indicating a flash memory capacity of 64KB; T6 is the package code, indicating LQFP48 package. If the same model has a different package (e.g., LQFP64 or LQFP100), the number of available GPIO pins will also differ. So when you check the pin assignment, make sure to confirm the package type.

## The Four Working Modes of GPIO

Although GPIO is called "General Purpose Input/Output," its versatility goes far beyond simply "outputting high/low levels and reading high/low levels." The GPIO of the STM32F1 series supports four main working modes: Input, Output, Alternate Function, and Analog. Each mode exists for a reason, corresponding to the four basic needs of the microcontroller interacting with the outside world.

First, **Input Mode**. The core problem solved by input mode is "what does the outside world tell the microcontroller?" When a pin is configured as input, external signals enter the chip through the pin. The voltage on the pin first passes through a Schmitt Trigger for shaping—the role of the Schmitt trigger is to convert potentially dirty analog signals (like a slow rising edge with noise) into clean digital signals, either a definite 0 or a definite 1, with no intermediate state. The shaped signal is sampled into the Input Data Register (IDR). Our program can know whether the pin is currently high or low by reading the IDR. In input mode, you can also choose to enable internal pull-up resistors or pull-down resistors: a pull-up resistor weakly connects the pin to VDD, making it default to high when floating; a pull-down resistor weakly connects the pin to VSS, making it default to low when floating; if neither is enabled, the floating level is uncertain. This is crucial in button detection—if your button has one end connected to the pin and the other to ground, you need to enable the internal pull-up resistor, so you read a high level when the button is not pressed, and a low level when it is pressed, keeping the state clear and reliable. Why does input mode need to exist? Because the microcontroller cannot always "monologue" by outputting signals; it must be able to perceive state changes in the outside world—whether a button is pressed, whether a sensor has issued an alarm, whether another chip has sent a ready signal—these are all use cases for input mode.

Next, **Output Mode**. The core problem solved by output mode is "what does the microcontroller tell the outside world?" When a pin is configured as output, the chip actively drives the pin to a high or low level. Output mode has two sub-types: Push-Pull Output and Open-Drain Output. Push-pull mode uses two MOSFETs—a P-MOS upper transistor connected to VDD and an N-MOS lower transistor connected to VSS—to actively drive in both directions. When outputting high, the upper transistor conducts and the lower cuts off, pulling the pin to VDD; when outputting low, the upper cuts off and the lower conducts, pulling the pin to VSS. The two transistors work alternately like pushing and pulling, hence the name "push-pull." Push-pull mode has strong driving capability and can output and sink relatively large currents. Open-drain mode only has the N-MOS lower transistor working; when outputting low, the lower transistor conducts and pulls the pin to VSS, but when outputting high, the lower transistor also cuts off, leaving the pin in a high-impedance state (floating), unable to actively pull high. To output a high level, an external pull-up resistor must be connected. The typical application scenario for open-drain output is the I2C bus—multiple devices share the same signal line, any device can pull the line low, but no device will actively push the line high (to avoid bus conflicts), and the high level is provided by an external pull-up resistor. LED control usually uses push-pull output, which is why we chose `GPIO_MODE_OUTPUT_PP` in `Led::init()`. Why does output mode need to exist? Because the microcontroller must be able to actively change the state of external circuits—lighting LEDs, driving relays, generating clock signals—these all require the pin to have the ability to actively output a definite level.

Then there is **Alternate Function Mode**. This mode exists because STM32 integrates a large number of peripherals—USART serial ports, SPI buses, I2C buses, timer PWM outputs, etc.—and these peripherals need physical pins to send and receive signals, but the number of chip pins is limited. The solution is pin multiplexing: the same physical pin can play different roles at different times. When a pin is configured as alternate function mode, the pin is no longer directly controlled by the GPIO controller but is handed over to the corresponding on-chip peripheral to drive. For example, PA9 and PA10 can be configured as the TX (transmit) and RX (receive) pins of USART1. At this point, they are no longer normal GPIOs but signal lines for serial communication. Once configured, you operate the USART peripheral registers in your code, not the GPIO registers, and the pin signals are generated automatically by the USART hardware. In `hal.hpp`, this corresponds to `GPIO_MODE_AF_PP` (alternate push-pull) and `GPIO_MODE_AF_OD` (alternate open-drain). Why does alternate function mode need to exist? Because pins are a scarce resource. A 48-pin chip has only thirty-something pins that can act as GPIO, but the on-chip peripherals might need fifty or sixty signal lines in total. Without multiplexing, the number of chip pins would balloon to an unacceptable degree.

Finally, **Analog Mode**. Analog mode is used to connect on-chip ADCs (Analog-to-Digital Converters) or DACs (Digital-to-Analog Converters). In analog mode, the digital functions of the pin are completely turned off—the Schmitt trigger is disabled, the Input Data Register (IDR) does not update, and the analog signal on the pin is sent directly to the ADC through internal paths for sampling. Why does analog mode need to exist? Because the presence of the Schmitt trigger introduces extra current consumption and signal distortion. When you need to read precise analog voltages (like millivolt signals from a temperature sensor), these digital circuits are actually sources of interference. So analog mode essentially "turns off all digital logic and lets the pin return to its purest analog state." In `hal.hpp`, this corresponds to `GPIO_MODE_ANALOG`.

⚠️ **Pitfall Warning:** Many beginners find that the pin behavior is incorrect after configuring GPIO, only to find out that the mode was configured wrong. The most common error is configuring a pin that should be an alternate function as a normal output mode—for example, wanting to use PA9 as USART1_TX but configuring it as `GPIO_MODE_OUTPUT_PP`, resulting in the serial port being unable to send data. Alternate functions must use `GPIO_MODE_AF_PP` or `GPIO_MODE_AF_OD`, which tells the multiplexer to hand the pin over to the peripheral.

## GPIO Internal Structure Diagram

The text described four modes, but to truly understand how GPIO works, an internal structure diagram is worth a thousand words. Below is an ASCII character drawing of the internal structure of an STM32F1 series GPIO pin. Please note that this is a simplified conceptual diagram; some details (like output speed control) are omitted, but the core signal paths are accurate.

```text
          Protection Diodes
                 |
           +-----+-----+
           |           |
          VDD         VSS
           |           |
    +------+------+------+------+
    |      |      |      |      |
  Pull   Pull   Schmitt  P-MOS  N-MOS
   Up    Down   Trigger  (Upper) (Lower)
    |      |      |      |      |
    +--+---+------+--+---+------+
       |            |  |
       |         MUX  +----> To Pin Pad
       |            |
    +--+------------+---+
    |  |     Control   |
    |  +----------------+
    |
Configuration Registers (CRL/CRH)
```

Don't be intimidated by this diagram; let's break it down block by block.

**Protection Diodes** are the first line of defense for the pin and the most easily overlooked part. They are connected between the pin and VDD/VSS, forming a clamping circuit. Under normal working conditions, the pin voltage is between 0V and 3.3V, and neither protection diode conducts, having no effect on the circuit. However, if an anomaly occurs in the external circuit—for example, 5V is applied to the pin—the upper protection diode will conduct, shunting the excess energy to the VDD power rail and preventing the internal circuit from being broken down by overvoltage. Similarly, if the pin is pulled to a negative voltage, the lower protection diode will conduct, clamping the pin to VSS. This is a very simple but effective protection mechanism. However, the current protection diodes can withstand is limited, usually marked as Injection Current in the datasheet, and sustained high current can burn out the diode. The correct approach is to use a level shifter chip or a current-limiting resistor for isolation.

**Pull-up and Pull-down Resistors** are two configurable internal resistors. Note that they are not always connected—whether they are enabled is determined by the configuration bits in the CRL/CRH registers. When the pin is configured as "input pull-up" mode, the switch for the pull-up resistor between VDD and the pin is turned on, and the pin is connected to VDD through an internal resistor of approximately 40K ohms. This means the pin is weakly pulled high when floating. Similarly, in "input pull-down" mode, the pin is connected to VSS through a similar resistor. The resistance of these two resistors is relatively large (in the 30K-50K range), so the pulling force is weak—if there is a stronger external driver (like a button pressed connecting directly to GND), the external driver will easily override the effect of the internal pull-up.

**Schmitt Trigger** is located on the input signal path. Its role is crucial. Signals from the outside world are rarely perfect square waves—they may rise slowly, have glitches, or oscillate near the threshold. If such signals are used to trigger digital circuits directly, serious misjudgments can occur. The Schmitt trigger solves this problem by introducing hysteresis: its rising threshold (e.g., 1.7V) and falling threshold (e.g., 0.9V) are different. A signal going from low to high must exceed 1.7V to be considered "high," and going from high to low must fall below 0.9V to be considered "low." The area between 0.9V and 1.7V is the "uncertain zone," where the output maintains the last definite state. This design greatly improves noise immunity. In analog mode, the Schmitt trigger is turned off, and the analog signal goes directly to the ADC without being digitized.

**Output Driver** is the core of push-pull output. It consists of a P-MOS upper transistor and an N-MOS lower transistor. The gates of the two transistors are controlled by the corresponding bit of the Output Data Register (ODR) (after passing through the multiplexer). When a bit in the ODR is written to 1, the upper transistor conducts and the lower cuts off, driving the pin to VDD (high level). When a bit in the ODR is written to 0, the upper cuts off and the lower conducts, driving the pin to VSS (low level). In open-drain output mode, the P-MOS upper transistor is permanently cut off, and only the N-MOS lower transistor works. Output speed control (MODE bits) actually controls the slew rate of the output driver—the faster the speed, the faster the MOSFET switches, the steeper the signal edge, but it also generates greater EMI (Electromagnetic Interference) and power supply noise. This is also why we chose `GPIO_SPEED_FREQ_LOW` in `Led::init()`—LED blinking does not need high-speed toggling, and low speed reduces unnecessary electromagnetic radiation.

**Multiplexer (MUX)** is the "traffic police" of pin control. It decides where the output drive signal for the pin comes from: from the GPIO controller's ODR register (normal GPIO output) or from an on-chip peripheral (alternate function output). This choice is determined by the CNF bits in the CRL/CRH registers. When CNF is configured as alternate function, the MUX connects the peripheral's output signal to the driver, and the control of the ODR is bypassed. This is why after configuring alternate functions, you no longer need to manipulate the ODR manually—the peripheral hardware automatically controls the pin signals.

**CRL/CRH Configuration Registers** are the "control center" of the entire GPIO. Every 4 bits control a pin's MODE (speed/output enable) and CNF (specific mode configuration). We will analyze the bit meanings of these registers shortly.

## The Relationship Between Pins and Registers

After understanding the internal structure of GPIO, let's now turn our attention to the registers actually manipulated by the program. Each GPIO group (GPIOA to GPIOE) has 7 32-bit registers in the memory address space, arranged at fixed offsets. Let's take GPIOC as an example—because our LED is connected to PC13.

The base address of GPIOC is `0x40011000`. This address is not arbitrarily assigned—it lies within the address space of STM32's APB2 bus, and all GPIO peripherals hang on the APB2 bus. Starting from the base address, 7 registers are arranged as follows.

**CRL Register (Offset 0x00, Full Address 0x40011000)** is responsible for configuring Pin0 to Pin7, the 8 low-numbered pins. This is a 32-bit register where every 4 bits control one pin, corresponding to Pin0, Pin1, ..., Pin7 from low to high. In each 4 bits, the lower 2 bits are called MODE, and the upper 2 bits are called CNF. MODE bits determine the output speed of the pin (in output mode) or input mode flag (MODE=00 in input mode). CNF bits determine the specific sub-mode—such as floating input or pull-up input in input mode, push-pull or open-drain in output mode.

**CRH Register (Offset 0x04, Full Address 0x40011004)** is completely symmetrical to CRL, just responsible for Pin8 to Pin15, the 8 high-numbered pins. The structure is identical—every 4 bits control one pin, corresponding to Pin8, Pin9, ..., Pin15 from low to high.

Let's calculate using our PC13 as an example. PC13 is pin number 13 of the GPIOC group. Since 13 >= 8, it is controlled by the CRH register. In CRH, Pin8 occupies bits [3:0], Pin9 occupies bits [7:4], and so on. PC13 corresponds to the 5th group of 4 bits (since (13-8)=5), which is bits [23:20] of CRH. If we want to configure PC13 as push-pull output with a speed of 2MHz, the MODE bits should be `10` (2MHz), and the CNF bits should be `00` (general-purpose push-pull output), combining to form `0010` (`0x2`). This is written to bits [23:20] of CRH. The `HAL_GPIO_Init` function in the HAL library essentially performs these bit operations for us at the bottom layer. The `GPIO_InitTypeDef` we called in `Led::init` ultimately writes these values to bits [23:20] of CRH through the HAL library.

**IDR Register (Offset 0x08, Full Address 0x40011008)** is the Input Data Register, a read-only register. Its lower 16 bits correspond to the current level state of Pin0 to Pin15. If Pin13 is currently high, bit 13 of the IDR is 1; if low, bit 13 is 0. When you read button states in input mode, the bottom layer is reading this register. Regardless of the mode the pin is configured to (except analog mode), the IDR continuously reflects the actual level state on the pin.

**ODR Register (Offset 0x0C, Full Address 0x4001100C)** is the Output Data Register, readable and writable. In GPIO output mode, each bit of the ODR directly controls the level of the corresponding pin. Write 1 to output high, write 0 to output low. However, directly modifying the ODR has a hidden danger—read-modify-write operations on the ODR are not atomic. If your program is interrupted while modifying Pin13, and the interrupt modifies another pin in the same group (like Pin12), then the modification to Pin12 might be overwritten when the interrupt returns. To solve this problem, STM32 designed the BSRR and BRR registers.

**BSRR Register (Offset 0x10, Full Address 0x40011010)** is the Bit Set/Reset Register. It provides an atomic way to modify the ODR. The lower 16 bits (bit0 to bit15) of BSRR are "set bits"—writing a 1 to a bit sets the corresponding ODR bit to 1 (pin outputs high), writing 0 has no effect. The upper 16 bits (bit16 to bit31) of BSRR are "reset bits"—writing a 1 to a bit clears the corresponding ODR bit to 0 (pin outputs low), writing 0 has no effect. The key is that this operation is atomic—no read-modify-write is needed, just a single write to precisely control the specified bit without affecting others.

For example, to make PC13 output high, we can write `0x00002000` (bit 13 set to 1) to BSRR; to output low, we write `0x20000000` (bit 29, which is 13+16, set to 1). This is the underlying implementation logic of `HAL_GPIO_WritePin`, and also the hardware operation ultimately called by our `Led::toggle()` method.

**BRR Register (Offset 0x14, Full Address 0x40011014)** is the Bit Reset Register, functionally equivalent to taking the upper 16 bits of BSRR separately—writing 1 to the lower 16 bits clears the corresponding ODR bit. It was often used in early firmware libraries, but with BSRR, BRR became redundant because BSRR already covers both setting and clearing operations.

**LCKR Register (Offset 0x18, Full Address 0x40011018)** is the Configuration Lock Register. Its function is to lock the configuration of the GPIO—once locked, the corresponding CRL/CRH bits cannot be modified again until the next system reset. This is very useful in product-level code: after initialization is complete, lock the configuration to prevent the program from accidentally modifying the GPIO configuration if it runs away, which could cause hardware damage. The locking operation requires a specific write sequence to execute, which is a protection mechanism against accidental operation in the hardware design.

⚠️ **Pitfall Warning:** When using the BSRR register, remember the rule "write 1 takes effect, write 0 has no effect." This means you can safely write any value to BSRR without worrying about accidentally affecting other pins. But if you manipulate the ODR register directly, you must use a read-modify-write approach, which is unsafe in multi-threaded or interrupt environments. Therefore, a good habit in embedded development is: prioritize using BSRR to control output pins.

## Conclusion and Preview

At this point, we have traversed the full path of GPIO from physical circuits to programming interfaces. We know that GPIO has four working modes—Input, Output, Alternate Function, and Analog—each corresponding to specific hardware signal paths and register configurations, and each exists for an irreplaceable reason. Through the internal structure diagram, we saw how hardware units like protection diodes, Schmitt triggers, push-pull drivers, and multiplexers collaborate. We also looked at the 7 key registers (CRL, CRH, IDR, ODR, BSRR, BRR, LCKR) one by one for their addresses, offsets, and functions. Specifically using PC13 as an instance, we traced the complete path from C++ code to underlying registers—from the bit mask `GPIO_PIN_13` of `Led`, to bits [23:20] of CRH, to the atomic operations of BSRR, every link corresponds to actual hardware behavior.

GPIO is the foundation of embedded development. Later, we will cover serial communication, SPI buses, I2C protocols, PWM control, and ADC sampling, all built on the foundation of GPIO. Alternate function mode allows pins to "transform" into channels for various peripherals, and analog mode allows pins to process continuous voltage signals. But regardless of the mode, the physical structure, protection mechanisms, and configuration methods of the pins are universal. Understanding GPIO gives you the key to understanding the entire STM32 peripheral system.

In the next post, we will focus on the specific scenario of LED control. We will analyze in detail the workings of push-pull output mode—how P-MOS and N-MOS conduct alternately, what output speed settings mean, and why `GPIO_SPEED_FREQ_LOW` is sufficient for LED control. More importantly, we will look at the special circuit design of PC13 on the Blue Pill development board—why is the onboard LED low-level active instead of high-level active? What circuit considerations lie behind this seemingly counterintuitive design? Understanding this, you will realize why we need the `ActiveLevel` template parameter in `Led`, and how it cleverly encapsulates hardware differences.
