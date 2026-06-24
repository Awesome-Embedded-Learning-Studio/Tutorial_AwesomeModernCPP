---
chapter: 15
difficulty: beginner
order: 4
platform: stm32f1
reading_time_minutes: 21
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 9: HAL Clock Enable — Without Clock, Peripherals Are Just Dead Silicon'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/04-hal-gpio-clock.md
  source_hash: b9e70bfe22d6ad5c9d4ad4211efb7da512ca2194238352ec165bcc535a200c3a
  translated_at: '2026-06-24T01:11:19.608705+00:00'
  engine: anthropic
  token_count: 2841
---
# Part 9: HAL Clock Enable — Without a Clock, a Peripheral is Just a Dead Piece of Silicon

## Introduction: From Hardware Principles to Software APIs

In the previous part, we dissected the task of lighting an LED from the hardware level—what a GPIO port is, how pins are controlled by registers, the difference between push-pull and open-drain outputs, and the roles of pull-up and pull-down resistors. We now have a very clear understanding of "what is happening on the pin," but this is only half the story. Hardware principles are the foundation, but you can't build a house on a foundation alone—you also need bricks and cement. In our scenario, the HAL library APIs are those bricks and cement.

Starting with this part, we officially enter the phase of learning HAL library APIs. We will break down the key function calls that appear in the code one by one, figuring out exactly what each parameter, macro, and configuration line is doing behind the scenes. Where do we start? Not with GPIO initialization, not with pin state setting, but with—**clock enabling**.

You might find it strange: I just want to light an LED, what does that have to do with the clock? It has everything to do with it. This is the first and biggest pitfall for embedded beginners—**if a peripheral doesn't work, ninety percent of the time it's because you forgot to enable the clock**. When I was learning STM32, I spent countless nights staring at a dark LED board, pulling my hair out, checking code logic, verifying pin numbers, and confirming circuit connections, only to find the problem was in a place I hadn't even noticed: the clock wasn't enabled.

The clock is to a peripheral what a heartbeat is to a human. If the heart stops beating, the person is gone—no matter how strong, smart, or useful they are, once the heartbeat stops, everything is zero. The same logic applies to the clock. Every peripheral on the STM32—GPIO, USART, SPI, I2C, timers—needs a clock signal to work. If you don't supply it with a clock signal, it is just a dead piece of silicon. It will ignore whatever registers you write or whatever functions you call. It won't even give you an error code. This silent refusal is the most terrifying thing, because your code is logically correct, the compiler gives no warnings, and running it produces no errors, but the hardware simply won't move.

Therefore, the first step of this tutorial is to thoroughly understand clock enabling—why it exists, how it works, what happens if you forget it, and how our C++ template system helps you solve this automatically.

## The Clock is a Peripheral's Lifeline

To understand clock enabling, we must first understand the design philosophy of the STM32—power saving. One of the design goals of this chip is to work in various low-power scenarios, from battery-powered sensor nodes to handheld devices, where power consumption control is a core consideration. The STM32F103C8T6 is a microcontroller with a Cortex-M3 core. Its designers faced a practical problem: the chip integrates dozens of peripherals—GPIO has five ports (A through E), general-purpose timers are plentiful (TIM2, TIM3, TIM4), advanced timers include TIM1, serial ports include USART1, USART2, USART3, SPI includes SPI1, SPI2, SPI3, I2C includes I2C1, I2C2, there are two ADCs, plus DMA controllers, USB, CAN, and more. If all these peripherals received clock signals simultaneously and were all active, even if you only used one GPIO port to light an LED, the standby current of the chip would be very high—those peripherals you didn't use but were still running would each be consuming electricity.

Imagine your house has twenty rooms, but you are only reading in one of them. If you turned on the lights, air conditioning, and TVs in all rooms, your electricity bill would make you cry. What is the reasonable approach? You turn on the lights and AC only in the room you enter; you turn them off when you leave. The STM32 does exactly this—this is the **Clock Gating** mechanism.

The core idea of clock gating is simple: every peripheral has an independent clock switch. You manually turn on the clock for the peripheral you need to use; for unused peripherals, the clock is off by default, placing them in a "power-off" state where they consume almost no electricity. This switch is not a physical power switch, but a gate for the clock signal—before the clock signal reaches the peripheral, it passes through a "gate" controlled by software. Opening it allows the clock signal to pass; closing it blocks it. Without a clock signal input, the internal sequential logic circuits of the peripheral cannot work, and write operations to registers are directly ignored by the hardware.

So, who manages these gates? The answer is the **RCC** (Reset and Clock Control) module. The RCC is a very important module inside the STM32. It is responsible for three things: first, managing clock source selection and configuration (use the internal oscillator or an external crystal? Do we need frequency multiplication?); second, managing clock division and distribution (how many MHz does the CPU run at? How many MHz do the various buses run at?); third, managing the clock enabling of each peripheral (which peripheral is on, which is off). The RCC is essentially the "power dispatch center" inside the chip. All operations we perform on the clock in code are ultimately implemented by configuring registers inside the RCC module.

In our project code, the `ClockConfig::setup_system_clock()` method in the `clock.cpp` file is used to configure the RCC module; it sets the system clock source and various division parameters. The clock enabling for GPIO peripherals is done in the `GPIOClock::enable_target_clock()` method in `gpio.hpp`. The two have a clear division of labor: the former configures the entire clock tree, while the latter opens the clock gate for a specific peripheral. Below, we will first look at the clock tree to figure out exactly where the GPIO clock comes from.

## Simplified Clock Tree Diagram for STM32F103C8T6

To understand clock enabling, just knowing "flip a switch" is not enough; we also need to know the origin and flow of the clock signal itself. The STM32 clock system is a tree structure—starting from a source, passing through various dividers, multipliers, and selectors, and finally reaching every peripheral. Understanding this tree allows you to understand why the GPIO clock enable macro is called `__HAL_RCC_GPIOx_CLK_ENABLE` and not something else.

Below is a simplified clock tree under our project configuration. Note that this is the **configuration we actually use**, not the complete clock tree in the STM32 reference manual that gives you a headache at first glance. We will only look at the parts relevant to us:

![STM32 Simplified Clock Tree Diagram](./04-hal-gpio-clock.drawio)

Let's look at this tree layer by layer.

**Layer 1: Clock Source — HSI (High Speed Internal)**

The HSI is an 8 MHz RC oscillator inside the chip. "Internal" means you don't need to solder any external crystal on the circuit board; the chip can generate an 8 MHz clock signal by itself. This is very convenient for a minimal system—one chip can run. However, the accuracy of an RC oscillator is not as good as an external crystal. If you have high requirements for clock accuracy (for example, USB communication requires a precise 48 MHz clock), you need to use an external crystal (HSE). However, for scenarios like lighting an LED, the HSI is perfectly sufficient.

In our `clock.cpp`, the clock source configuration looks like this:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.HSIState = RCC_HSI_ON;
osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
```

These three lines of code mean: use the HSI as the oscillator source, enable the HSI, and use the default calibration value.

**Layer 2: PLL Multiplication – from 8 MHz to 64 MHz**

An 8 MHz HSI is too slow for a Cortex-M3. The maximum main frequency for the STM32F103C8T6 is 72 MHz (as clearly stated in the datasheet), but our configuration here selects 64 MHz—a safe and stable frequency. To boost 8 MHz to 64 MHz, we must go through a module called the **PLL** (Phase Locked Loop). Essentially, the PLL is a frequency multiplier: you give it an input frequency, and it outputs a higher frequency.

The multiplication process happens in two steps: first division, then multiplication. The 8 MHz HSI is first divided by two to become 4 MHz, and then 4 MHz is multiplied by 16 to become 64 MHz. Mathematically, this is: 8 / 2 × 16 = 64 MHz. This configuration is clearly visible in our code:

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.PLL.PLLState = RCC_PLL_ON;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;  // 8MHz / 2 = 4MHz
osc.PLL.PLLMUL = RCC_PLL_MUL16;              // 4MHz × 16 = 64MHz
```

`RCC_PLLSOURCE_HSI_DIV2` indicates that the PLL input source is the HSI signal divided by two, and `RCC_PLL_MUL16` indicates that the PLL multiplies the input signal by 16. The resulting 64 MHz signal from the PLL is selected as SYSCLK—the main clock for the entire system.

**Layer 3: AHB and APB Bus Clock Division**

The 64 MHz SYSCLK is not fed directly to all modules. It first passes through the **AHB** (Advanced High-performance Bus) divider to generate HCLK, which is the operating clock frequency for the CPU and the core clock for the entire bus matrix. In our configuration, the AHB divider is set to 1, so HCLK = SYSCLK = 64 MHz:

```cpp
clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;   // SYSCLK = PLL输出
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;          // HCLK = SYSCLK / 1 = 64MHz
```

HCLK then passes through two APB (Advanced Peripheral Bus) dividers to generate the clock signals for two peripheral buses:

**APB1 Bus**: The divider coefficient is 2, so the APB1 clock frequency (PCLK1) = HCLK / 2 = 32MHz. Why divide by two? Because the peripherals connected to the APB1 bus (such as USART2-3, TIM2-4, I2C, SPI2-3) can only withstand a maximum clock frequency of 36MHz. If we give it 64MHz, it might work unstably or even get damaged. 32MHz is within the safe range, leaving sufficient margin.

**APB2 Bus**: The divider coefficient is 1, so the APB2 clock frequency (PCLK2) = HCLK / 1 = 64MHz. APB2 is the high-speed peripheral bus, and the peripherals connected to it (such as GPIOA-E, USART1, SPI1, TIM1, ADC) can withstand higher clock frequencies. Note that GPIO is connected to this bus—this means GPIO can respond to operations at 64MHz, which is crucial for high-speed I/O operations.

```cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
clk.APB1CLKDivider = RCC_HCLK_DIV2;   // APB1 = 64MHz / 2 = 32MHz
clk.APB2CLKDivider = RCC_HCLK_DIV1;   // APB2 = 64MHz / 1 = 64MHz
```

Great, we now know that the GPIO is attached to the APB2 bus, and the APB2 clock runs at 64MHz. So, what exactly are we turning on when we "enable the GPIO clock"? The answer is in the next section.

## `__HAL_RCC_GPIOx_CLK_ENABLE` Macro Explained

In our previous analysis of the clock tree, we reached a key conclusion: the GPIO is attached to the APB2 bus. This means that the clock enable switch for the GPIO port must be located within the RCC registers associated with APB2. The HAL library encapsulates a series of macros for us to operate these switches, and their naming convention is very consistent:

```c
__HAL_RCC_GPIOA_CLK_ENABLE();    // 使能GPIOA的时钟
__HAL_RCC_GPIOB_CLK_ENABLE();    // 使能GPIOB的时钟
__HAL_RCC_GPIOC_CLK_ENABLE();    // 使能GPIOC的时钟
__HAL_RCC_GPIOD_CLK_ENABLE();    // 使能GPIOD的时钟
__HAL_RCC_GPIOE_CLK_ENABLE();    // 使能GPIOE的时钟
```

These look like function calls, but they are actually **macros**. C macros are expanded into real code during the preprocessing phase. Taking `GPIOC` as an example, this macro essentially expands to the following:

```c
#define __HAL_RCC_GPIOC_CLK_ENABLE()  \
    do { \
        __IO uint32_t tmpreg; \
        RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; \
        tmpreg = RCC->APB2ENR; \
        (void)tmpreg; \
    } while(0)
```

Let's break down this expansion result line by line.

`RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;` is the core operation. `RCC` is a pointer to the RCC register structure, and `APB2ENR` is the APB2 Peripheral Clock Enable Register, located at physical address `0x40021018`. The `|=` operator performs a "read-modify-write" operation—it reads the current register value, performs a bitwise OR with `RCC_APB2ENR_IOPCEN` (setting specific bits to 1), and writes the result back to the register. `RCC_APB2ENR_IOPCEN` is a bit mask representing bit 4; setting it to 1 enables the clock for GPIOC.

`tmpreg = RCC->APB2ENR; (void)tmpreg;` These two lines look strange—reading into a temporary variable and then not using it. This is not a bug, but a deliberate delay operation. Bus write operations on the ARM Cortex-M3 are buffered. When a write instruction completes, the data may not have actually reached the register yet. Reading the same register immediately after forces the processor to wait for the previous write operation to complete, ensuring the clock is actually enabled before subsequent code executes. This is a critical detail—if you manipulate peripheral registers immediately after enabling the clock, but the clock hasn't stabilized yet, it can lead to unpredictable behavior.

Each GPIO port corresponds to a different bit in the APB2ENR register:

- **GPIOA** = bit2 (IOPAEN), bit mask `0x00000004`
- **GPIOB** = bit3 (IOPBEN), bit mask `0x00000008`
- **GPIOC** = bit4 (IOPCEN), bit mask `0x00000010`
- **GPIOD** = bit5 (IOPDEN), bit mask `0x00000020`
- **GPIOE** = bit6 (IOPEEN), bit mask `0x00000040`

You will notice that the clock enable operation for each port uses different register bits. This means you cannot use a generic macro to enable clocks for all ports—you must call specific macros for specific ports. This seemingly minor detail will have a significant impact when we design our C++ template system, as we will see later.

Another point to note: these macros only enable clocks. There is no common scenario for a corresponding `__HAL_RCC_GPIOx_CLK_DISABLE` (although the HAL library does provide disable macros). In actual development, once the clock is enabled, it is rarely turned off—you rarely decide at runtime, "I don't need GPIOC anymore, so I'll turn off its clock." Clock enabling is essentially a one-time initialization operation.

Before we move to the next section, let's look back at a confusing concept. You may have noticed that besides IOPxEN (like IOPCEN), there is a similar bit in the APB2ENR register called AFIOEN (Alternate Function IO clock enable). This bit controls the clock for the "Alternate Function IO" module, which is distinct from the GPIO port clock. The AFIO module is used for remapping pin alternate functions (e.g., remapping USART1 TX from PA9 to another pin). In simple GPIO output scenarios, you do not need to enable the AFIO clock. Our LED lighting project only uses the standard GPIO output function, so `__HAL_RCC_AFIO_CLK_ENABLE()` does not appear in the code.

## Symptoms and Troubleshooting of Forgotten Clocks

⚠️ **Pitfall Warning: This is the number one trap for STM32 beginners.**

This section deserves a warning box because I have fallen into this trap too many times myself, and I have seen too many beginners post on forums for help: "My code looks completely correct, but the LED won't light up, help!" The most common reply is: "Did you enable the clock?"

The reason forgetting to enable the clock is such a big pitfall isn't because it's hard to solve—the solution is just one line of code—but because **the symptoms are deceptive**. Let's describe in detail what you will encounter.

**Typical Symptoms:**

First, your code compiles without any warnings. Then you flash the program to the chip and run it—nothing happens. The LED doesn't light up. You think it might be a delay issue, so you add a longer delay—still nothing. You think you might have the wrong pin number, so you check it carefully—no problem. You even compare your code line-by-line with the official example and find the logic is exactly the same.

The most frustrating part is that every HAL function you call in your code returns no errors. `HAL_GPIO_Init()` returns `HAL_OK` (although it doesn't really check the clock), and `HAL_GPIO_WritePin()` throws no exceptions. Everything "succeeds," but measuring the pin with an oscilloscope shows absolutely no voltage change—it just sits there, like a dead wire.

**Why doesn't the HAL report an error?**

This is the most confusing part. When a peripheral's clock is not enabled, write operations to that peripheral's registers are **silently ignored** by the hardware. Note that it doesn't "report an error" or "return an error code"; it's just like nothing happened. The reason is this: The CPU initiates a write operation to a peripheral register address via the bus (AHB/APB). If the clock is enabled, the write operation reaches the peripheral's register normally and is latched. If the clock is not enabled, the peripheral's internal sequential logic circuit cannot work because it has no clock drive. The write operation reaches the address, but no one is there to "receive" it. From the CPU and bus perspective, the write operation is complete—there is no error at the bus protocol level (no timeout, no bus fault). But from the peripheral's perspective, the write operation never happened.

It's like talking to someone who is asleep—your words are spoken, the sound waves travel, but they don't hear you. No matter how loud you speak or how many times you repeat, they won't react. The only thing you can do is wake them up first—in our scenario, "waking up" is enabling the clock.

**Troubleshooting Steps:**

When you encounter a situation where "the code is fine but the hardware doesn't respond," follow these steps:

1. **Check if the corresponding port's clock enable macro was called.** If you are using GPIOC, the code must contain `__HAL_RCC_GPIOC_CLK_ENABLE()`. If you are using GPIOA, it must be `__HAL_RCC_GPIOA_CLK_ENABLE()`. Don't mix them up.

2. **Check if the port passed in is correct.** This is a more subtle error—you defined a pin using GPIOC somewhere, but wrote GPIOA in the clock enable section. The compiler won't complain (both are valid macro calls), but GPIOC won't work without a clock, and while GPIOA has a clock, you aren't using it.

3. **If you have a debugger (ST-Link or J-Link), directly view the value of the RCC_APB2ENR register.** The address of this register is `0x40021018`. You can find it in the debugger's register window or print its value in code. If you enabled the GPIOC clock, bit 4 of this register should be 1. If it is 0, it means the clock enable code was not executed, or it was overwritten by subsequent code.

You will find that these three troubleshooting steps essentially verify the same thing: did the clock enable operation actually take effect? This is why this pitfall is so hidden—because it happens in the place you are most likely to overlook.

## How Our C++ Templates Automatically Handle Clocks

Now that we understand the principle of clock enabling and the consequences of forgetting it, let's see how the C++ template system in our project elegantly solves this problem.

In our project's `device/gpio/gpio.hpp` file, clock enabling is encapsulated in the `setup()` method of the `GPIO` template class. Whenever a user calls `setup()` to initialize a GPIO pin, the clock enable is automatically executed as the first step:

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

Pay close attention to the first line of the `setup()` method: `GPIOClock::enable_target_clock()`. This call is hidden within the `private` section of the `GPIO` class, so the user doesn't need to worry about it at all. Whether we are initializing Pin 5 on GPIOA or Pin 13 on GPIOC, as long as `setup()` is called, the corresponding port clock is automatically enabled.

How is this automatic selection implemented? The answer lies in the `GPIOClock` nested class, which uses C++17's `if constexpr` to implement compile-time conditional branching:

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

`if constexpr` is compile-time conditional judgment introduced in C++17. Unlike a regular `if` statement, the condition in `if constexpr` is evaluated at compile time. Only the branch where the condition evaluates to `true` is compiled into the final code, while the other branches are discarded entirely. Since `PORT` is a non-type template parameter (a `GpioPort` enum value), it is fixed at compile time, allowing the compiler to determine exactly which clock enable macro to call.

This means that when you write the template instantiation `GPIO<GpioPort::C, GPIO_PIN_13>`, the compiler automatically generates an `enable_target_clock()` function that contains only `__HAL_RCC_GPIOC_CLK_ENABLE()`—with no runtime `if-else` overhead, no function pointers, and no superfluous baggage. The resulting machine code is completely equivalent to hand-writing a single line of `__HAL_RCC_GPIOC_CLK_ENABLE()`.

This is the charm of C++ template metaprogramming—**zero-overhead abstraction**. At the source code level, you gain the safety of "never forgetting to enable the clock" (because `setup()` handles it for you), while the compiled binary incurs absolutely no additional overhead.

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

When we instantiate an object like `device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>`, its constructor calls `GPIO<GpioPort::C, GPIO_PIN_13>::setup()`. This `setup()` method automatically calls `GPIOClock::enable_target_clock()`, which is resolved at compile time to `__HAL_RCC_GPIOC_CLK_ENABLE()`. The entire chain fits together perfectly; the user does not need to write a single line of clock-related code in `main.cpp`.

The key point is: with this template system, it is **impossible** to forget to enable the clock. As long as your initialization path goes through the `setup()` method, the clock enable is guaranteed to execute. This is excellent engineering design: encapsulating error-prone manual steps into automated infrastructure so that developers cannot make mistakes, rather than relying on developer memory and discipline.

## Wrapping Up

Clock enabling is the most fundamental and critical step in STM32 development. In this article, we started with STM32's power-saving design philosophy to understand the necessity of clock gating; used a simplified clock tree diagram to clarify the complete clock chain from HSI to PLL, to SYSCLK, and finally to the APB2 bus; dissected the underlying implementation of the `__HAL_RCC_GPIOx_CLK_ENABLE` macro to understand that it essentially manipulates specific bits in the RCC_APB2ENR register; and spent considerable time discussing the symptoms and troubleshooting methods for the "forgotten clock" issue, the number one pitfall for beginners. Finally, we saw how our C++ template system uses `if constexpr` to automatically select the correct clock enable macro at compile time, achieving zero-cost safety.

With clock enabling covered, the power supply to the GPIO is established. What is the next step? The clock is enabled, but the pin does not yet know its mode—output or input? Push-pull or open-drain? Do we need pull-up or pull-down? What speed should be set? These configurations are handled via the `HAL_GPIO_Init()` function and the `GPIO_InitTypeDef` structure. In the next article, we will dissect this initialization process to see exactly how these electrical properties are configured into hardware registers through code.
