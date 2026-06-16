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
title: 'Part 9: HAL Clock Enabling — Without a Clock, Peripherals Are Just Dead Silicon'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/04-hal-gpio-clock.md
  source_hash: 6d9da184fe953bb6295c8eb492db864dc80863ee9997a1f8e43a24667499ab32
  translated_at: '2026-06-16T04:09:30.929830+00:00'
  engine: anthropic
  token_count: 2841
---
# Part 9: HAL Clock Enable — Without Clock, Peripherals Are Just Dead Silicon

## Preface: From Hardware Principles to Software APIs

In the previous post, we dissected the act of lighting an LED from the hardware level—what GPIO ports are, how pins are controlled by registers, the difference between push-pull and open-drain outputs, and the roles of pull-up and pull-down resistors. We now have a very clear understanding of "what is happening at the pin," but this is only half the story. Hardware principles are the foundation, but you can't build a house on a foundation alone—you need bricks and cement. In our scenario, the HAL library APIs are those bricks and cement.

Starting with this post, we officially enter the phase of learning HAL library APIs. We will break down the key function calls that appear in the code one by one, figuring out exactly what each parameter, macro, and line of configuration does behind the scenes. Where do we start? Not with GPIO initialization, not with pin state setting, but—**clock enabling**.

You might find it strange: I just want to light an LED, what does that have to do with the clock? It has everything to do with it. This is the first and biggest pitfall for embedded beginners—**if a peripheral doesn't work, 90% of the time it's because you forgot to enable the clock**. When I was learning STM32, I spent countless nights staring at a dark LED board, scratching my head, checking code logic, verifying pin numbers, and checking circuit connections, only to find the problem was in a place I hadn't even noticed: the clock was not enabled.

The clock is to a peripheral what the heartbeat is to a human. When the heart stops beating, the person is gone—no matter how strong, smart, or useful they are, once the heartbeat stops, everything is zero. The same logic applies to the clock. Every peripheral on the STM32—GPIO, USART, SPI, I2C, Timers—needs a clock signal to work. Without a clock signal supply, it is just a lump of dead silicon. It ignores whatever register you write or whatever function you call; it won't even give you an error code. This silent rejection is the most terrifying part because your code is logically correct, the compiler gives no warnings, and running produces no errors, but the hardware simply won't move.

Therefore, the first step of this tutorial is to thoroughly understand clock enabling—why it exists, how it works, what happens if you forget it, and how our C++ template system helps you solve it automatically.

## The Clock is the Lifeline of Peripherals

To understand clock enabling, we must first understand the design philosophy of STM32—**power saving**. One of the design goals of this chip is to work in various low-power scenarios, from battery-powered sensor nodes to handheld devices, where power control is a core consideration. The STM32F103C8T6 is a Cortex-M3 microcontroller. Its designers faced a practical problem: the chip integrates dozens of peripherals—GPIO has five ports (A through E), general-purpose timers (TIM2, TIM3, TIM4), advanced timers (TIM1), serial ports (USART1, USART2, USART3), SPI (SPI1, SPI2, SPI3), I2C (I2C1, I2C2), two ADCs, plus DMA controllers, USB, CAN, etc. If all these peripherals received clock signals simultaneously and were all active, even if you only used one GPIO port to light an LED, the standby current of the chip would be very high—those unused but still spinning peripherals would each be consuming power.

Imagine your house has twenty rooms, but you are only reading in one of them. If you turn on the lights, air conditioning, and TVs in all rooms, your electricity bill will make you cry. What is the reasonable approach? You turn on the light and AC only in the room you enter; you turn them off when you leave. STM32 does exactly this—this is the **Clock Gating** mechanism.

The core idea of clock gating is simple: each peripheral has an independent clock switch. You manually turn on the clock for the peripheral you need to use; unused peripherals have their clocks turned off by default, putting them in a "power-off" state where they consume almost no electricity. This switch is not a physical power switch, but a gate for the clock signal—the clock signal passes through a "gate" before reaching the peripheral. This gate is controlled by software; opening it releases the clock signal, closing it blocks it. Without a clock signal input, the internal sequential logic circuits of the peripheral cannot work, and write operations to registers are silently ignored by the hardware.

So, who manages these gates? The answer is the **RCC (Reset and Clock Control)** module. RCC is a very important module inside STM32. It is responsible for three things: first, managing clock source selection and configuration (use internal oscillator or external crystal? To multiply or not?); second, managing clock division and distribution (how many MHz does the CPU run? How many MHz do the various buses run?); third, managing the clock enable of each peripheral (which peripheral is on, which is off). RCC is essentially the "power dispatch center" inside the chip. All operations we perform on the clock in code are ultimately implemented by configuring registers inside the RCC module.

In our project code, the ``clock.cpp`` method in the ``ClockConfig::setup_system_clock()`` file is used to configure the RCC module, setting the system clock source and various division parameters. The clock enable for GPIO peripherals is done in the ``GPIOClock::enable_target_clock()`` method within ``gpio.hpp``. The division of labor is clear: the former configures the entire clock tree, while the latter opens the specific clock gates for peripherals. Below, we first look at the clock tree to clarify exactly where the GPIO clock comes from.

## Simplified Clock Tree for STM32F103C8T6

To understand clock enabling, knowing just "flip a switch" is not enough; we also need to know the ins and outs of the clock signal itself. The STM32 clock system is a tree structure—starting from one source, passing through various dividers, multipliers, and selectors, and finally reaching every peripheral. Understanding this tree allows you to understand why the GPIO clock enable macro is named ``__HAL_RCC_GPIOx_CLK_ENABLE`` and not something else.

Below is a simplified clock tree under our project configuration. Note, this is the **configuration we actually use**, not the complete clock tree in the STM32 reference manual that gives you a headache at first glance. We will only look at the parts relevant to us:

![STM32 Clock Tree Simplified Diagram](./04-hal-gpio-clock.drawio)

Let's look at this tree layer by layer.

**Layer 1: Clock Source — HSI (High Speed Internal)**

HSI is the chip's internal 8MHz RC oscillator. "Internal" means you don't need to solder any external crystal on the circuit board; the chip can generate an 8MHz clock signal by itself. This is very convenient for a minimal system—one chip can run. However, the accuracy of an RC oscillator is not as good as an external crystal. If you have high requirements for clock accuracy (e.g., USB communication requires a precise 48MHz clock), you need to use an external crystal (HSE). But for scenarios like lighting an LED, HSI is perfectly adequate.

In our ``clock.cpp``, the clock source configuration looks like this:

````cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.HSIState = RCC_HSI_ON;
osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
````

These three lines mean: use HSI as the oscillator source, turn on HSI, and use the default calibration value.

**Layer 2: PLL Multiplication — From 8MHz to 64MHz**

8MHz HSI is too slow for a Cortex-M3. The maximum main frequency of STM32F103C8T6 is 72MHz (clearly marked in the datasheet), but our configuration here chooses 64MHz—a safe and stable frequency. To boost 8MHz to 64MHz, we need to go through a module called **PLL (Phase Locked Loop)**. Essentially, a PLL is a multiplier: you give it an input frequency, and it outputs a higher frequency.

The multiplication process happens in two steps: first divide, then multiply. The 8MHz HSI is first divided by 2 to become 4MHz, then 4MHz is multiplied by 16 to become 64MHz. Mathematically: 8 / 2 × 16 = 64MHz. This configuration is clear in our code:

````cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
osc.PLL.PLLState = RCC_PLL_ON;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;  // 8MHz / 2 = 4MHz
osc.PLL.PLLMUL = RCC_PLL_MUL16;              // 4MHz × 16 = 64MHz
````

``RCC_PLLSOURCE_HSI_DIV2`` indicates the PLL input source is the HSI signal divided by 2, and ``RCC_PLL_MUL16`` indicates the PLL multiplies the input signal by 16. The 64MHz output from the PLL is selected as SYSCLK—the main clock of the entire system.

**Layer 3: AHB and APB Bus Division**

The 64MHz SYSCLK is not directly used by all modules. It first passes through the **AHB (Advanced High-performance Bus)** divider to get HCLK, which is the clock frequency the CPU runs at and the core clock of the entire bus matrix. In our configuration, the AHB division coefficient is 1, so HCLK = SYSCLK = 64MHz:

````cpp
clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;   // SYSCLK = PLL输出
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;          // HCLK = SYSCLK / 1 = 64MHz
````

HCLK then passes through two APB (Advanced Peripheral Bus) dividers respectively to obtain the clocks for two peripheral buses:

**APB1 Bus**: The division coefficient is 2, so the APB1 clock frequency (PCLK1) = HCLK / 2 = 32MHz. Why divide by 2? Because peripherals on the APB1 bus (such as USART2-3, TIM2-4, I2C, SPI2-3) can only withstand clock frequencies up to 36MHz. If you give it 64MHz, it might work unstably or even be damaged. 32MHz is within the safe range with sufficient margin.

**APB2 Bus**: The division coefficient is 1, so the APB2 clock frequency (PCLK2) = HCLK / 1 = 64MHz. APB2 is the high-speed peripheral bus, and the peripherals on it (such as GPIOA-E, USART1, SPI1, TIM1, ADC) can withstand higher clock frequencies. Note that GPIO hangs on this bus—this means GPIO can respond to operations at 64MHz, which is crucial for high-speed IO operations.

````cpp
// 来源: code/stm32f1-tutorials/1_led_control/system/clock.cpp
clk.APB1CLKDivider = RCC_HCLK_DIV2;   // APB1 = 64MHz / 2 = 32MHz
clk.APB2CLKDivider = RCC_HCLK_DIV1;   // APB2 = 64MHz / 1 = 64MHz
````

Great, now we know GPIO is on the APB2 bus, and the APB2 clock is 64MHz. So what exactly are we "opening" when we "enable the GPIO clock"? The answer is in the next section.

## Deep Dive into ``__HAL_RCC_GPIOx_CLK_ENABLE`` Macro

In the clock tree analysis above, we reached a key conclusion: GPIO is mounted on the APB2 bus. This means the clock enable switch for GPIO ports must be located in the APB2-related RCC registers. The HAL library encapsulates a series of macros for us to operate these switches, and their naming rules are very consistent:

````c
__HAL_RCC_GPIOA_CLK_ENABLE();    // 使能GPIOA的时钟
__HAL_RCC_GPIOB_CLK_ENABLE();    // 使能GPIOB的时钟
__HAL_RCC_GPIOC_CLK_ENABLE();    // 使能GPIOC的时钟
__HAL_RCC_GPIOD_CLK_ENABLE();    // 使能GPIOD的时钟
__HAL_RCC_GPIOE_CLK_ENABLE();    // 使能GPIOE的时钟
````

These things that look like function calls are actually **Macros**. C macros are expanded into real code during the preprocessing phase. Taking GPIOC as an example, this macro essentially expands to this:

````c
#define __HAL_RCC_GPIOC_CLK_ENABLE()  \
    do { \
        __IO uint32_t tmpreg; \
        RCC->APB2ENR |= RCC_APB2ENR_IOPCEN; \
        tmpreg = RCC->APB2ENR; \
        (void)tmpreg; \
    } while(0)
````

Let's break down this expansion result line by line.

``RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;`` is the core operation. ``RCC`` is a pointer to the RCC register structure, ``APB2ENR`` is the APB2 Peripheral Clock Enable Register, and its physical address is ``0x40021018``. ``|=`` is a "read-modify-write" operation—read the current value of the register, perform a bitwise OR with ``RCC_APB2ENR_IOPCEN`` (which sets a specific bit to 1), and then write it back to the register. ``RCC_APB2ENR_IOPCEN`` is a bit mask representing bit 4; setting it to 1 enables the clock for GPIOC.

``tmpreg = RCC->APB2ENR; (void)tmpreg;`` these two lines look strange—reading out and assigning to a temporary variable that isn't used. This is not a bug, but a deliberate delay operation. Bus write operations on ARM Cortex-M3 are buffered; when the write instruction completes, the data may not have actually reached the register yet. Immediately reading the same register forces a wait for the previous write operation to complete, ensuring the clock enable is truly effective before continuing to execute subsequent code. This is a very important detail—if you operate a peripheral's register immediately after enabling the clock, but the clock isn't stable yet, it can lead to unpredictable behavior.

Each GPIO port corresponds to a different bit in the APB2ENR register:

- **GPIOA** = bit2 (IOPAEN), bit mask ``0x00000004``
- **GPIOB** = bit3 (IOPBEN), bit mask ``0x00000008``
- **GPIOC** = bit4 (IOPCEN), bit mask ``0x00000010``
- **GPIOD** = bit5 (IOPDEN), bit mask ``0x00000020``
- **GPIOE** = bit6 (IOPEEN), bit mask ``0x00000040``

You will find that the clock enable operation for each port is a different register bit. This means you cannot use a generic macro to enable the clock for all ports—you must call different macros for different ports. This seemingly insignificant detail will have a very important impact when we design our C++ template system, as we will see later.

There is another point to note: these macros can only enable clocks; there is no corresponding common scenario for ``__HAL_RCC_GPIOx_CLK_DISABLE`` (although HAL does provide disable macros). In actual development, once the clock is enabled, it is usually not turned off again—you rarely decide at runtime "I don't need GPIOC anymore, let's turn off its clock." Clock enabling is essentially a one-time initialization operation.

Don't rush, before entering the next section, let's look back at an easily confused concept. You may have noticed that besides IOPxEN (like IOPCEN), there is a similar bit in the APB2ENR register called AFIOEN (Alternate Function IO clock enable). This bit controls the clock of the "Alternate Function IO" module, which is not the same thing as the GPIO port clock. The AFIO module is used for remapping pin alternate functions (e.g., remapping the USART1 TX pin from PA9 to another pin). In simple GPIO output scenarios, you do not need to enable the AFIO clock. Our LED lighting project only uses the standard output function of GPIO, so ``__HAL_RCC_AFIO_CLK_ENABLE()`` does not appear in the code.

## Symptoms and Troubleshooting of Forgetting Clock Enable

⚠️ **Pitfall Warning: This is the number one pitfall for STM32 beginners.**

This section deserves a warning box because I have fallen into this pit too many times myself, and I have seen too many beginners post on forums for help: "My code looks perfectly correct, but the LED won't light up, help!" And the most common answer in the replies is: "Did you enable the clock?"

The reason forgetting the clock is a big pit is not because it's hard to solve—the solution is just one line of code—but because **its symptoms are too deceptive**. Let's describe in detail what you will encounter.

**Typical Symptoms:**

First, your code compiles without any warnings. Then you flash the program to the chip, run it—nothing happens. The LED doesn't light up. You think it might be a delay issue, so you add a longer delay—still nothing. You think you might have written the wrong pin number, so you check it carefully—no problem. You even compare your code line by line with the official example and find the logic is exactly the same.

What makes you崩溃 the most is that every HAL function you call in your code returns no error. ``HAL_GPIO_Init()`` returns ``HAL_OK`` (although it doesn't really check the clock much), and ``HAL_GPIO_WritePin()`` has no exceptions. Everything is "successful," but if you measure the pin with an oscilloscope, there is absolutely no voltage change—it just sits there quietly, like a dead wire.

**Why doesn't HAL report an error?**

This is the most confusing part. When a peripheral's clock is not enabled, your write operations to that peripheral's registers are silently **ignored** by the hardware. Note, not "report an error," not "return an error code," but just like nothing happened. The reason is this: the CPU initiates a write operation to a peripheral's register address via the bus (AHB/APB). When the clock is enabled, this write operation normally reaches the peripheral's register and is latched. But when the clock is not enabled, the internal sequential logic circuit of the peripheral cannot work because there is no clock drive, so the write operation reaches the address, but no one is there to "receive" it. From the perspective of the CPU and the bus, this write operation has completed—there is no error at the bus protocol level (no timeout, no bus fault). But from the perspective of the peripheral, this write operation simply never happened.

It's like talking to a sleeping person—your words are indeed spoken, the sound waves indeed propagate, but he doesn't hear you. No matter how loud you speak or how many times you repeat, he won't react. The only thing you can do is wake him up first—in our scenario, "waking up" is enabling the clock.

**Troubleshooting Methods:**

When you encounter "code is fine but hardware won't move," follow these steps to troubleshoot:

Step 1, check if the corresponding port's clock enable macro was called. If you are using GPIOC, there must be ``__HAL_RCC_GPIOC_CLK_ENABLE()`` in the code. If you are using GPIOA, it must be ``__HAL_RCC_GPIOA_CLK_ENABLE()``. Don't get them mixed up.

Step 2, check if the port passed in is correct. This is a more hidden error—you defined using a GPIOC pin somewhere, but wrote GPIOA in the clock enable. The compiler won't report an error (because both are legal macro calls), but GPIOC has no clock so it won't work, and although GPIOA has a clock, you aren't using it.

Step 3, if you have a debugger (ST-Link or J-Link), directly check the value of the RCC_APB2ENR register. The address of this register is ``0x40021018``, you can find it in the debugger's register window, or print its value in code. If you enabled the clock for GPIOC, bit 4 of this register should be 1. If it is 0, it means the clock enable code was not executed, or was overwritten by subsequent code.

You will find that these three troubleshooting steps are essentially verifying the same thing: did the clock enable operation actually take effect? This is why this pit is so hidden—because it happens in the place you are most likely to overlook.

## How Our C++ Template Automatically Handles Clocks

After understanding the principle of clock enabling and the consequences of forgetting it, let's look at how the C++ template system in our project elegantly solves this problem.

In our project's ``device/gpio/gpio.hpp`` file, clock enabling is encapsulated in the ``setup()`` method of the ``GPIO`` template class. Whenever the user calls ``setup()`` to initialize a GPIO pin, clock enabling is automatically executed as the first step:

````cpp
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
````

Notice the first line of the ``setup()`` method—``GPIOClock::enable_target_clock()``. This call is hidden in the ``private`` area of the ``GPIO`` class; the user doesn't need to care about it at all. Whether you are initializing Pin5 of GPIOA or Pin13 of GPIOC, as long as you call ``setup()``, the corresponding port clock will be automatically enabled.

And how is this automatic selection implemented? The answer lies in the ``GPIOClock`` nested class, which uses C++17's ``if constexpr`` to implement compile-time conditional branching:

````cpp
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
````

``if constexpr`` is compile-time conditional judgment introduced in C++17. Unlike a normal ``if`` statement, the condition of ``if constexpr`` is evaluated at compile time, and only the branch where the condition is ``true`` will be compiled into the final code; other branches are discarded directly. Because ``PORT`` is a non-type template parameter (an ``GpioPort`` enum value), it is determined at compile time, so the compiler can fully determine which clock enable macro to call.

This means that when you write the template instantiation ``GPIO<GpioPort::C, GPIO_PIN_13>``, the compiler automatically generates a ``enable_target_clock()`` function containing only ``__HAL_RCC_GPIOC_CLK_ENABLE()``—no runtime ``if-else`` judgment overhead, no function pointers, absolutely nothing extra. The final generated machine code is exactly equivalent to you hand-writing a line of ``__HAL_RCC_GPIOC_CLK_ENABLE()``.

This is the charm of C++ template metaprogramming—**Zero-Cost Abstraction**. You gain the safety of "impossible to forget to enable clock" at the source code level (because ``setup()`` does it for you automatically), and at the compiled binary level, there is no extra overhead.

Back to our ``main.cpp``:

````cpp
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
````

When you instantiate the ``device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>`` object, its constructor calls ``GPIO<GpioPort::C, GPIO_PIN_13>::setup()``, which automatically calls ``GPIOClock::enable_target_clock()``, and the latter is determined at compile time to be ``__HAL_RCC_GPIOC_CLK_ENABLE()``. The whole chain fits together seamlessly, and the user doesn't need to write a single line of clock-related code in ``main.cpp``.

The key point is: after using this template system, you **cannot** forget to enable the clock—as long as your initialization path goes through the ``setup()`` method, the clock enable will definitely be executed. This is a very good engineering design: encapsulating error-prone manual steps into automated infrastructure, making it impossible for developers to make mistakes, rather than relying on the developer's memory and discipline.

## Conclusion

Clock enabling is the most foundational and important step in STM32 development. In this article, starting from STM32's power-saving design philosophy, we understood the necessity of the clock gating mechanism; through the simplified clock tree diagram, we clarified the complete clock path from HSI to PLL to SYSCLK to APB2 bus; we deeply dissected the underlying implementation of the ``__HAL_RCC_GPIOx_CLK_ENABLE`` macro, figuring out that it essentially operates specific bits of the RCC_APB2ENR register; then we spent a lot of time discussing the symptoms and troubleshooting methods for the "forgetting to enable clock" pitfall; finally, we saw how our C++ template system uses ``if constexpr`` to automatically select the correct clock enable macro at compile time, achieving zero-cost safety.

Clock enabling is done, and the GPIO power supply is connected. What's the next step? The clock is on, but the pin doesn't know what mode it should be in—output or input? Push-pull or open-drain? Pull-up or pull-down? What speed? These are configured through the ``HAL_GPIO_Init()`` function and the ``GPIO_InitTypeDef`` structure. In the next post, we will dissect this initialization process to see exactly how those electrical properties are configured into hardware registers through code.
