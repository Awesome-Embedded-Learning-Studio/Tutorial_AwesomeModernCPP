---
title: 'Part 9: HAL Clock Enable — Without a Clock, a Peripheral Is Just a Piece of
  Dormant Silicon'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 4
translation:
  source: documents/vol8-domains/embedded/01-led/04-hal-gpio-clock.md
  source_hash: c328592f0ef1a42e41e289bbdb550851ade7ae452259f0d790a27f3ebefc5f56
  translated_at: '2026-05-26T12:04:39.485053+00:00'
  engine: anthropic
  token_count: 2834
---
# Part 9: HAL Clock Enable — Without a Clock, a Peripheral Is Just a Dead Piece of Silicon

## Introduction: From Hardware Principles to Software APIs

In the previous article, we tore down the process of lighting an LED from the hardware level — what a GPIO port is, how pins are controlled by registers, the difference between push-pull and open-drain outputs, and what roles pull-up and pull-down resistors play. We now have a very clear understanding of "what happens on the pin," but that is only half the story. Hardware principles are the foundation, but you cannot build a house with a foundation alone — you also need bricks and mortar. In our scenario, the HAL library's APIs are those bricks and mortar.

Starting with this article, we officially enter the phase of learning the HAL library's APIs. We will break down the key function calls that appear in our code one by one, figuring out exactly what is happening behind every parameter, every macro, and every line of configuration. And where do we begin? Not with GPIO initialization, not with pin state setting, but with — clock enable.

You might find this strange: I just want to light an LED, what does that have to do with a clock? Everything. This is the first and biggest pitfall for embedded development beginners — **if a peripheral is not working, ninety percent of the time you forgot to enable its clock**. Back when I was learning STM32, I cannot count how many nights I spent pulling my hair out over an unlit LED board, repeatedly checking code logic, verifying pin numbers, and double-checking circuit connections, only to find the problem in a place I had completely overlooked: the clock was not enabled.

A clock is to a peripheral what a heartbeat is to a person. When the heart stops beating, the person is gone — no matter how strong, how smart, or how useful they are, once the heartbeat stops, everything is zero. The same logic applies to clocks. Every peripheral on the STM32 — GPIO, USART, SPI, I2C, timers — needs a clock signal to function. If you do not supply it with a clock signal, it is just a dead piece of silicon. No matter what registers you write to or what functions you call, it ignores you completely, and it will not even give you an error code. This silent rejection is the most terrifying kind, because your code is logically correct, there are no compilation warnings, and there are no runtime errors — the hardware simply does not move.

So the first step in this tutorial is to thoroughly understand clock enable — why it exists, how it works, what happens when you forget it, and how our C++ template system helps you solve this problem automatically.

## The Clock Is a Peripheral's Lifeline

To understand clock enable, we must first understand the STM32's design philosophy — power saving. One of the design goals of this chip is to operate in various low-power scenarios, from battery-powered sensor nodes to handheld devices, where power consumption control is a core consideration. The STM32F103C8T6 is a microcontroller with a Cortex-M3 core. Its designers faced a practical problem: the chip integrates dozens of peripherals — GPIO has five ports (A through E), there are several general-purpose timers (TIM2, TIM3, TIM4), an advanced timer (TIM1), serial ports (USART1, USART2, USART3), SPI (SPI1, SPI2, SPI3), I2C (I2C1, I2C2), two ADCs, plus a DMA controller, USB, CAN, and more. If all these peripherals simultaneously received clock signals and were all active, even if you only used one GPIO port to light an LED, the chip's standby current would be extremely high — every peripheral you are not using but that is still running would be consuming power.

Imagine your house has twenty rooms, but you are only reading in one of them. If you turned on the lights, air conditioning, and TVs in all the rooms, your electricity bill would make you cry. What is the reasonable thing to do? You turn on the lights and air conditioning only in the room you enter, and turn them off when you leave. That is exactly what the STM32 does — this is the **Clock Gating** mechanism.

The core idea of clock gating is simple: each peripheral has an independent clock switch. You manually turn on the clock for whichever peripheral you need to use; for unused peripherals, the clock is off by default, leaving them in a "powered-down" state that consumes almost no electricity. This switch is not a physical power switch, but rather a gate on the clock signal — before the clock signal reaches the peripheral, it must pass through a "gate" controlled by software. When opened, it lets the clock signal through; when closed, it blocks it. Without a clock signal input, the peripheral's internal sequential logic circuits cannot work, and write operations to its registers are silently ignored by the hardware.

So who manages these gates? The answer is the **RCC (Reset and Clock Control)** module. The RCC is a very important module inside the STM32, responsible for three things: first, managing clock source selection and configuration (use the internal oscillator or an external crystal? multiply the frequency?); second, managing clock division and distribution (how many MHz for the CPU? how many MHz for each bus?); and third, managing the clock enable for each peripheral (which peripheral is on, which is off). The RCC itself is a "power dispatch center" inside the chip, and every operation we perform on the clock in our code is ultimately implemented by configuring registers within the RCC module.

In our project code, the `ClockConfig::setup_system_clock()` method in the `clock.cpp` file is used to configure the RCC module, setting the system clock source and various division parameters. The GPIO peripheral's clock enable, on the other hand, is done in the `GPIOClock::enable_target_clock()` method within `gpio.hpp`. The division of labor is clear: the former configures the entire clock tree, while the latter opens the clock gate for a specific peripheral. Below, we will first look at the clock tree to understand exactly where the GPIO's clock comes from.

## Simplified Clock Tree of the STM32F103C8T6

To understand clock enable, simply knowing "flip a switch" is not enough. We also need to know the full story of the clock signal itself. The STM32's clock system is a tree structure — starting from one source, passing through various dividers, multipliers, and selectors, and finally reaching every peripheral. Only by understanding this tree can you understand why the GPIO clock enable macro is called `__HAL_RCC_GPIOx_CLK_ENABLE` and not something else.

Below is a simplified clock tree under our project's configuration. Note that this is the **configuration we actually use**, not the complete clock tree in the STM32 reference manual that gives you a headache at first glance. We will only look at the parts relevant to us:

![STM32 simplified clock tree diagram](./04-hal-gpio-clock.drawio)

Let us look at this tree layer by layer.

**Layer 1: Clock Source — HSI (High Speed Internal)**

HSI is the chip's internal 8 MHz RC oscillator. "Internal" means you do not need to solder any external crystal on the PCB; the chip can generate an 8 MHz clock signal on its own. This is very convenient for minimal systems — a single chip can run. However, the accuracy of an RC oscillator is not as good as an external crystal. If you have strict requirements for clock accuracy (for example, USB communication requires a precise 48 MHz clock), you need to use an external crystal (HSE). But for a scenario like lighting an LED, HSI is perfectly adequate.

In our `clock.cpp`, the clock source is configured like this:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.HSIState = RCC_HSI_ON;
osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
```

These three lines of code mean: use HSI as the oscillator source, turn on HSI, and use the default calibration value.

**Layer 2: PLL Multiplication — From 8 MHz to 64 MHz**

8 MHz from HSI is too slow for a Cortex-M3. The maximum clock frequency of the STM32F103C8T6 is 72 MHz (clearly stated in the datasheet), but our configuration here chooses 64 MHz — a safe and stable frequency. To boost 8 MHz to 64 MHz, the signal must pass through a module called the **PLL (Phase-Locked Loop)**. The PLL is essentially a multiplier: you give it an input frequency, and it outputs a higher frequency.

The multiplication process happens in two steps: divide first, then multiply. The 8 MHz from HSI is first divided by 2 to become 4 MHz, and then 4 MHz is multiplied by 16 to become 64 MHz. Mathematically: 8 / 2 × 16 = 64 MHz. This configuration is clear at a glance in our code:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.PLL.PLLState = RCC_PLL_ON;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;  // 8MHz / 2 = 4MHz
osc.PLL.PLLMUL = RCC_PLL_MUL16;              // 4MHz × 16 = 64MHz
```

`RCC_PLLSOURCE_HSI_DIV2` indicates that the PLL's input source is the HSI signal after being divided by 2, and `RCC_PLL_MUL16` indicates that the PLL multiplies the input signal by 16. The 64 MHz signal output by the PLL is selected as SYSCLK — the main clock for the entire system.

**Layer 3: AHB and APB Bus Division**

The 64 MHz of SYSCLK is not used directly by all modules. It first passes through the **AHB (Advanced High-performance Bus)** divider to produce HCLK, which is the clock frequency at which the CPU itself runs, and also the core clock of the entire bus matrix. In our configuration, the AHB division factor is 1, so HCLK = SYSCLK = 64 MHz:

```cpp
clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;   // SYSCLK = PLL输出
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;          // HCLK = SYSCLK / 1 = 64MHz
```

HCLK then passes through two APB (Advanced Peripheral Bus) dividers respectively, yielding the clocks for two peripheral buses:

**APB1 bus**: The division factor is 2, so the APB1 clock frequency (PCLK1) = HCLK / 2 = 32 MHz. Why divide by 2? Because the peripherals hanging off the APB1 bus (such as USART2-3, TIM2-4, I2C, SPI2-3) can only tolerate clock frequencies up to 36 MHz. If you give it 64 MHz, it might work unstably or even be damaged. 32 MHz is well within the safe range, leaving ample margin.

**APB2 bus**: The division factor is 1, so the APB2 clock frequency (PCLK2) = HCLK / 1 = 64 MHz. APB2 is the high-speed peripheral bus, and the peripherals connected to it (such as GPIOA-E, USART1, SPI1, TIM1, ADC) can tolerate higher clock frequencies. Note that GPIO hangs on this bus — meaning GPIO can respond to operations at 64 MHz, which is very important for high-speed IO operations.

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
clk.APB1CLKDivider = RCC_HCLK_DIV2;   // APB1 = 64MHz / 2 = 32MHz
clk.APB2CLKDivider = RCC_HCLK_DIV1;   // APB2 = 64MHz / 1 = 64MHz
```

Great, now we know that GPIO is connected to the APB2 bus, and the APB2 clock is 64 MHz. So what exactly are we "turning on" when we "enable the GPIO clock"? The answer is in the next section.

## Deep Dive into the `__HAL_RCC_GPIOx_CLK_ENABLE` Macro

From the clock tree analysis above, we reached a key conclusion: GPIO is connected to the APB2 bus. This means the clock enable switch for GPIO ports must reside in the APB2-related RCC registers. The HAL library encapsulates a series of macros for us to operate these switches, and their naming convention is very consistent:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();    // 使能GPIOA的时钟
__HAL_RCC_GPIOB_CLK_ENABLE();    // 使能GPIOB的时钟
__HAL_RCC_GPIOC_CLK_ENABLE();    // 使能GPIOC的时钟
__HAL_RCC_GPIOD_CLK_ENABLE();    // 使能GPIOD的时钟
__HAL_RCC_GPIOE_CLK_ENABLE();    // 使能GPIOE的时钟
```

These things that look like function calls are actually **macros**. C language macros are expanded into real code during the preprocessing phase. Taking GPIOC as an example, this macro essentially expands to:

```c
#define __HAL_RCC_GPIOC_CLK_ENABLE()  \
    do { \
        __IO uint32_t tmpreg; \
        RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; \
        tmpreg = RCC->APB2ENR; \
        (void)tmpreg; \
    } while(0)
```

Let us break down this expanded result line by line.

`RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;` is the core operation. `RCC` is a pointer to an RCC register structure, and `APB2ENR` is the APB2 Peripheral Clock Enable Register, whose physical address is `0x40021018`. `|=` is a "read-modify-write" operation — it first reads the current value of the register, performs a bitwise OR with `RCC_APB2ENR_IOPCEN` (which sets a specific bit to 1), and then writes it back to the register. `RCC_APB2ENR_IOPCEN` is a bit mask representing bit 4 (bit4); setting it to 1 enables the clock for GPIOC.

`tmpreg = RCC->APB2ENR; (void)tmpreg;` These two lines look strange — reading a value into a temporary variable and then not using it. This is not a bug, but a deliberate delay operation. Write operations on the ARM Cortex-M3 bus are buffered; when the write instruction finishes executing, the data may not have actually reached the register yet. Immediately reading the same register forces the system to wait for the previous write operation to complete, ensuring the clock enable has truly taken effect before continuing to execute subsequent code. This is a very important detail — if you operate a peripheral's registers immediately after enabling the clock, and the clock has not yet stabilized, it could lead to unpredictable behavior.

Each GPIO port corresponds to a different bit in the APB2ENR register:

- **GPIOA** = bit2 (IOPAEN), bit mask `0x00000004`
- **GPIOB** = bit3 (IOPBEN), bit mask `0x00000008`
- **GPIOC** = bit4 (IOPCEN), bit mask `0x00000010`
- **GPIOD** = bit5 (IOPDEN), bit mask `0x00000020`
- **GPIOE** = bit6 (IOPEEN), bit mask `0x00000040`

You will notice that the clock enable operation for each port uses a different register bit. This means you cannot use a single generic macro to enable the clock for all ports — you must call a different macro for each port. This seemingly insignificant detail will have a very important impact when we design our C++ template system, as we will see shortly.

Another point to note: these macros can only enable clocks; there is no commonly used scenario for `__HAL_RCC_GPIOx_CLK_DISABLE` (although the HAL library does provide disable macros). In actual development, once a clock is enabled, it is rarely turned off again — you would not typically decide at runtime, "I no longer need GPIOC, let me turn off its clock." Clock enable is essentially a one-time initialization operation.

Before moving on to the next section, let us look back at an easily confused concept. You may have noticed that besides IOPxEN (such as IOPCEN), the APB2ENR register has a similar bit called AFIOEN (Alternate Function IO clock enable). This bit controls the clock for the "Alternate Function IO" module, which is not the same thing as the GPIO port clock. The AFIO module is used for remapping pin alternate functions (for example, remapping the USART1 TX pin from PA9 to another pin), and it does not need to be enabled for simple GPIO output scenarios. Our LED project only uses the GPIO's general-purpose output function, so `__HAL_RCC_AFIO_CLK_ENABLE()` does not appear in our code.

## Symptoms and Troubleshooting of a Forgotten Clock

⚠️ **Pitfall Warning: This is the number one trap for STM32 beginners.**

This section deserves to start with a warning box, because I have fallen into this trap too many times myself, and I have seen too many beginners post on forums for help: "My code looks completely correct, but the LED just won't light up, help!" And the most common answer in the replies is: "Did you enable the clock?"

The reason a forgotten clock is such a big trap is not because it is hard to solve — the fix is just one line of code — but because **its symptoms are incredibly deceptive**. Let us describe in detail what you will encounter.

**Typical Symptoms:**

First, your code compiles without any warnings. Then you flash the program to the chip and run it — nothing happens. The LED does not light up. You think it might be a delay issue, so you add a longer delay — still nothing. You think you might have written the wrong pin number, so you carefully verify it — no problem. You even compare your code line by line with the official example and find the logic is exactly the same.

What drives you crazy the most is that every HAL function you called in your code does not return an error. `HAL_GPIO_Init()` returns `HAL_OK` (although it does not actually check the clock much), and `HAL_GPIO_WritePin()` has no exceptions either. Everything "succeeded," but if you measure the pin with an oscilloscope, there is absolutely no voltage change — it just sits there quietly, like a dead wire.

**Why Doesn't HAL Report an Error?**

This is the most confusing part. When a peripheral's clock is not enabled, your write operations to that peripheral's registers are **silently ignored** by the hardware. Note: it does not "report an error" or "return an error code" — it acts as if nothing happened at all. The reason is this: the CPU initiates a write operation to a peripheral's register address via the bus (AHB/APB). When the clock is enabled, this write operation normally reaches the peripheral's register and is latched. But when the clock is not enabled, the peripheral's internal sequential logic circuits cannot work because they have no clock drive. The write operation arrives at the address, but nobody "receives" it. From the CPU and bus's perspective, the write operation has already completed — there is no error at the bus protocol level (no timeout, no bus fault). But from the peripheral's perspective, the write operation never happened at all.

It is like talking to someone who is asleep — your words are indeed spoken, and the sound waves indeed propagate, but they do not hear you. No matter how loudly you speak or how many times you repeat yourself, they will not react. The only thing you can do is wake them up first — in our scenario, "waking them up" is enabling the clock.

**Troubleshooting Steps:**

When you encounter a situation where "the code is fine but the hardware does not move," follow these steps to troubleshoot:

Step one, check whether you called the clock enable macro for the corresponding port. If you are using GPIOC, your code must have `__HAL_RCC_GPIOC_CLK_ENABLE()`. If you are using GPIOA, it must be `__HAL_RCC_GPIOA_CLK_ENABLE()`. Do not mix them up.

Step two, check whether the port you passed in is correct. This is a more hidden error — you defined a pin on GPIOC somewhere, but wrote GPIOA in the clock enable section. The compiler will not report an error (because both are valid macro calls), but GPIOC has no clock so it naturally will not work, and GPIOA has a clock but you are not using it at all.

Step three, if you have a debug probe (ST-Link or J-Link), directly check the value of the RCC_APB2ENR register. The address of this register is `0x40021018`, and you can find it in the debugger's register window or print its value in code. If you enabled the clock for GPIOC, then bit 4 of this register should be 1. If it is 0, it means the clock enable code was not executed, or it was overwritten by subsequent code.

You will find that these three troubleshooting steps essentially all verify the same thing: did the clock enable operation actually take effect? This is why this pitfall is so hidden — because it happens in the place you are most likely to overlook.

## How Our C++ Templates Automatically Handle the Clock

After understanding the principle of clock enable and the consequences of forgetting it, let us look at how the C++ template system in our project elegantly solves this problem.

In our project's `device/gpio/gpio.hpp` file, clock enable is encapsulated in the `setup()` method of the `GPIO` template class. Whenever a user calls `setup()` to initialize a GPIO pin, clock enable is automatically executed as the first step:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/device/gpio/gpio.hpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
    GPIOClock::enable_target_clock();  // 第一步：自动使能对应端口的时钟
    GPIO_InitTypeDef init_types{};
    init_types.Pin = PIN;
    init_types.Mode = static_cast<uint32_t>(gpio_mode);
    init_types.Pull = static_cast<uint32_t>(pull_push);
    init_types.Speed = static_cast<uint32_t>(speed);
    HAL_GPIO_Init(native_port(), &init_types);
}
```

Notice the first line of the `setup()` method — `GPIOClock::enable_target_clock()`. This call is hidden inside the `private` section of the `GPIO` class, and the user does not need to care about it at all. Whether you are initializing Pin 5 of GPIOA or Pin 13 of GPIOC, as long as you call `setup()`, the corresponding port's clock will be automatically enabled.

And how is this automatic selection implemented? The answer lies in the `GPIOClock` nested class, which uses C++17's `if constexpr` to implement compile-time conditional branching:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/device/gpio/gpio.hpp
class GPIOClock {
  public:
    static inline void enable_target_clock() {
        if constexpr (PORT == GpioPort::A) {
            __HAL_RCC_GPIOA_CLK_ENABLE();
        } else if constexpr (PORT == GpioPort::B) {
            __HAL_RCC_GPIOB_CLK_ENABLE();
        } else if constexpr (PORT == GpioPort::C) {
            __HAL_RCC_GPIOC_CLK_ENABLE();
        } else if constexpr (PORT == GpioPort::D) {
            __HAL_RCC_GPIOD_CLK_ENABLE();
        } else if constexpr (PORT == GpioPort::E) {
            __HAL_RCC_GPIOE_CLK_ENABLE();
        }
    }
};
```

`if constexpr` is a compile-time conditional introduced in C++17. Unlike a regular `if` statement, the condition of `if constexpr` is evaluated at compile time, and only the branch whose condition is `true` gets compiled into the final code; the other branches are discarded entirely. Because `PORT` is a non-type template parameter (an `GpioPort` enum value), it is determined at compile time, so the compiler can know exactly which clock enable macro to call.

This means that when you write the template instantiation `GPIO<GpioPort::C, GPIO_PIN_13>`, the compiler automatically generates a `enable_target_clock()` function that only contains `__HAL_RCC_GPIOC_CLK_ENABLE()` — there is no runtime `if-else` branching overhead, no function pointers, and nothing superfluous. The resulting machine code is exactly equivalent to you hand-writing a single `__HAL_RCC_GPIOC_CLK_ENABLE()`.

This is the charm of C++ template metaprogramming — **zero-overhead abstraction**. At the source code level, you gain the safety of "it is impossible to forget to enable the clock" (because `setup()` does it for you automatically), and at the compiled binary level, there is zero extra overhead.

Returning to our `main.cpp`:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/main.cpp
int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

When you instantiate the `device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>` object, its constructor calls `GPIO<GpioPort::C, GPIO_PIN_13>::setup()`, which in turn automatically calls `GPIOClock::enable_target_clock()`, and the latter is determined at compile time to be `__HAL_RCC_GPIOC_CLK_ENABLE()`. The entire chain fits together seamlessly, and the user does not need to write a single line of clock-related code in `main.cpp`.

The key point is: after using this template system, it is **impossible** to forget to enable the clock — as long as your initialization path goes through the `setup()` method, the clock enable will definitely be executed. This is excellent engineering design: encapsulating error-prone manual steps into automated infrastructure so that developers cannot make mistakes, rather than relying on developers' memory and discipline.

## Wrapping Up

Clock enable is the most fundamental and important step in STM32 development. In this article, starting from the STM32's power-saving design philosophy, we understood the necessity of the clock gating mechanism; through a simplified clock tree diagram, we clarified the complete clock path from HSI to PLL to SYSCLK to the APB2 bus; we deeply dissected the underlying implementation of the `__HAL_RCC_GPIOx_CLK_ENABLE` macro, figuring out that it essentially operates on a specific bit of the RCC_APB2ENR register; we then spent considerable time discussing the symptoms and troubleshooting methods for the number one beginner pitfall of "forgetting to enable the clock"; and finally, we saw how our C++ template system uses `if constexpr` to automatically select the correct clock enable macro at compile time, achieving zero-overhead safety.

That covers clock enable, and the GPIO's clock supply is now connected. What is the next step? The clock is enabled, but the pin does not yet know what mode it should be in — output or input? Push-pull or open-drain? Do we need pull-up or pull-down? What speed should we set? These are all configured through the `HAL_GPIO_Init()` function and the `GPIO_InitTypeDef` structure. In the next article, we will dissect this initialization process and see exactly how those electrical properties are configured into hardware registers through code.
