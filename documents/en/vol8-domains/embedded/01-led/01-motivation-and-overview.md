---
chapter: 15
difficulty: beginner
order: 1
platform: stm32f1
reading_time_minutes: 21
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 6: Starting with Lighting Up the First LED — Why We Use Modern C++ for
  STM32'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/01-motivation-and-overview.md
  source_hash: 621b8f9c45317ad6bbfd8808c1655063e6b2435aebdd19145eb6752dff5926ef
  translated_at: '2026-06-16T04:09:20.213407+00:00'
  engine: anthropic
  token_count: 2504
---
# Part 6: Starting with the First LED — Why We Use Modern C++ for STM32

> Written for everyone who has just finished setting up their toolchain and can't wait to get their board running.
> This post marks the true starting point of our hardware control code journey. We won't rush into code; let's first talk through the "why".

---

## Starting with a Single LED

Every embedded developer's journey begins with the same task—lighting up an LED. This isn't a trivial matter; it is the "Hello World" of the embedded world, the first successful dialogue between you and a silent chip. Whether you plan to use STM32 for motor control, USB communication, or running an RTOS later, GPIO operations remain the foundation of everything. Just as learning programming starts with `printf`, learning embedded systems starts with toggling a pin high and low. It is the unavoidable first step.

I remember the first time I lit that LED on the Blue Pill. It's a hard feeling to describe. It's just a small light bulb, but you realize that the code you wrote—through compilation, linking, format conversion, and SWD protocol transmission—finally turned into electrical signals, physically changing the voltage on a pin, and the LED lit up. This experience of "code becoming physical phenomenon" is something pure software development can never give you. At that moment, you feel that the weekend spent wrestling with the toolchain was worth it.

Speaking of the toolchain, I must admit, writing this post feels quite complicated. The previous five `env_setup` tutorials, from installing `arm-none-eabi-gcc` to configuring CMake, from wrestling with WSL2 USB passthrough to getting the OpenOCD debugger working—every step was blood and tears. Especially that time getting ST-Link recognized under WSL2, I almost gave up. But when we could finally type `make` in the terminal, then `openocd`, and see nothing happen on the board—because the code wasn't right yet—that feeling was actually grounding. The environment is fine, the toolchain works, flashing runs, and now we just need one line of code to actually control the hardware.

Now we have finally reached this step. No longer fighting the compiler in the terminal, no longer hunting for typos in config files, but writing real code to make a chip work for you.

## How Painful Traditional Embedded Development Is

But before that, I want to talk about why this wasn't simple to begin with, and why we chose a slightly different path.

What does traditional STM32 development look like? If you've used Keil MDK or IAR, you must be deeply impressed by that experience—a bloated IDE taking up several GB of space, an editor whose functionality stopped in the last century, code completion that relies mostly on guessing, and a debugging interface so ugly it's annoying. Even worse, it locks you firmly to the Windows platform. You want to develop on Linux? Sorry, either use Wine to simulate (and face various esoteric crashes), or honestly open a virtual machine. Moreover, Keil's compiler is closed-source; optimization behavior is opaque, and when problems arise, you don't even know how it optimized things.

Of course, these are superficial inconveniences. What really made me determined to abandon traditional development methods is the bad smell accumulated by the C language in the embedded field over decades. Look at what a typical STM32 project looks like: `#define` macros everywhere, things like `__IO`, `__attribute`, these preprocessor symbols have no type, no scope, you can't see real values when debugging, and the compiler's type checking completely fails for them. Then there are those HAL library callback functions, passing function pointers and `void *` userdata back and forth, type safety is virtually non-existent. Then layers of conditional compilation `#ifdef`, `#ifndef`, cross-platform adaptation twists the code into spaghetti.

The most fatal issue is code reusability. You write an LED driver for Blue Pill, hardcoding `GPIOC` and `GPIO_PIN_13` inside. Next time you switch to an STM32F407 board, the LED is on PD12. What do you do? Copy-paste and change parameters? What if there are ten pins to control in the project? Twenty? C macros and structs can solve part of the problem, but ultimately you will still fall into a pile of runtime judgments and switch-cases, which is neither elegant nor efficient.

This isn't about dissing C—C is a great language, and it has dominated the embedded field for decades for a reason. But times are progressing, compilers are progressing, can't we pursue better abstractions without paying a runtime cost?

## Why C++23?

This is where Modern C++ comes in. Note I said "Modern C++", not the "C with Classes" from the 90s. The features brought by the C++23 standard are exactly what embedded development has been dreaming of.

Zero-overhead abstraction is the core design philosophy of C++—you don't pay for what you don't use. Templates are expanded at compile time, `constexpr` functions are evaluated at compile time, `if constexpr` does branch selection at compile time. These mechanisms allow your code to have beautiful abstraction layers at the source level, but the compiled machine code is as efficient as hand-written C. Your LED template class looks like an elegant type system, but the final `MOV` and `STR` instructions generated by the compiler are exactly the same as if you directly manipulated registers. No virtual function overhead, no RTTI overhead, no exception handling overhead—because we explicitly turned these features off during compilation.

Compile-time type safety is another killer advantage. In C, if you pass `500` to a function expecting a port number, the compiler won't make a sound, because they are both integers. But in our C++ template system, `Port` is a distinct type. You encode port and pin information into the type system at compile time, and any wrong parameter passing will be exposed at the compilation stage, rather than waiting for the LED on the board to fail to light up and then scratching your head at the code.

The code reuse brought by templates goes without saying. Our GPIO template accepts the port and pin as non-type template parameters, which means `GPIO<PortA, 5>` and `GPIO<PortC, 13>` are two different classes, each with its own `set()` and `reset()` methods. The clock enable branch is completed at compile time using `if constexpr`—if it's Port A, enable GPIOA clock; if Port B, enable GPIOB clock—all of this happens at compile time, with zero runtime overhead. You never have to write those runtime codes that look up table to find port numbers again.

Then there are those C++23 desserts: the `[[nodiscard]]` attribute makes the compiler warn you when you ignore a return value—this is too important in embedded systems, the clock configuration failed and you don't check it? The system will run away immediately. `enum class` wraps bare integers into strongly-typed enumerations, eliminating implicit conversion between different enum values. `consteval` makes port address conversion a compile-time constant. These features seem insignificant individually, but combined, they can take the safety and maintainability of embedded code up a big step.

So we chose C++23, not to be trendy, but because it really solves practical problems in embedded development. Later, we will use a lot of code to prove this.

---

## What You Need to Prepare

Before we officially start, there are a few things that need to be confirmed.

First, those five `env_setup` tutorials. If you skipped around, I strongly suggest going back and going through parts 01 to 05: toolchain installation, project structure, CMake configuration, USB flashing, debugger configuration. Every line of code in this post is built on the environment set up in those five parts. Your `arm-none-eabi-gcc` must compile normally, CMake must build successfully, and OpenOCD must be able to flash the firmware to the board. If these aren't working yet, stop now and fix them; half an hour won't hurt.

Then there's basic programming knowledge. I won't start from "what is a variable", but I also won't assume you are a C++ template metaprogramming expert. You need to be familiar with basic C or C++ syntax: variable declaration, function definition, basic concepts of structs and classes, the role of `#include` headers. If you have written code in any programming language and understand what "function call" and "return value" mean, then you have enough starting point. Templates, CRTP, `constexpr`—we will introduce and explain these advanced features as we use them.

In terms of hardware, the whole article needs just three things: an STM32F103C8T6 Blue Pill board, an ST-Link V2 debugger, and a USB cable. Blue Pill can be bought on Taobao for less than ten yuan, ST-Link V2 is even cheaper, just a few yuan. These three together might be cheaper than a cup of milk tea, but they can take you through the whole road from lighting an LED to understanding modern embedded development paradigms. ST-Link connects to Blue Pill via three wires: SWDIO, SWCLK, GND, plus 3.3V to power the board. The specific wiring method was detailed in the USB part of `env_setup`, so I won't repeat it here.

The software environment is the set we configured in `env_setup`: `arm-none-eabi-gcc` toolchain, OpenOCD, CMake 3.22 or higher. The editor is up to you; VSCode with the clangd plugin can get a good code completion experience, but you can use Vim, Neovim, or even directly `nano`—it doesn't matter since we build with CMake, which is editor-agnostic.

⚠️ If you are developing under WSL2, make sure USB/IP passthrough is configured and `lsusb` can see the ST-Link device. This is a prerequisite for flashing; if it's not set up, the subsequent `openocd` will definitely fail.

---

## The Road We Will Take

Now that the tools and mindset are ready, I want to map out the whole road we will take, so you have a map in mind. The LED control series of tutorials isn't just one article, but a complete learning path from "understanding hardware" to "mastering the API" to "redesigning with Modern C++", totaling 13 posts. Why do we need so many posts to light an LED? Because our goal isn't "just make it light up and be done", but to understand the principles behind every line of code and the trade-offs of every design decision.

We start with the hardware principles of GPIO, which is the bottommost foundation. GPIO sounds like just five words "General-Purpose Input/Output", but the circuit structure behind it—push-pull output, open-drain output, pull-up resistor, pull-down resistor, Schmitt trigger—each directly affects how you configure the pin and how you choose the working mode. Without understanding these, writing code is just reciting mantras; change the scenario and you won't know how to do it. We have arranged 3 posts for the hardware principles part, from the GPIO internal structure block diagram to the circuit analysis of the four working modes, to the register organization of STM32F103. Don't be afraid of hardware; these things are actually easy to understand when drawn out.

Next question—knowing the hardware principles of GPIO, how do we control it with software? The official HAL library provided by ST is this bridge. HAL stands for Hardware Abstraction Layer, and it wraps low-level register operations into function calls like `HAL_GPIO_WritePin` and `HAL_GPIO_TogglePin`. We use 3 tutorials to break down the HAL's GPIO interface: initialization configuration, read/write operations, clock management. This part will write code directly in C style because HAL itself is a C interface, and we must learn the "fundamentalist" usage first before we can talk about how to build better abstractions on top of it.

Then there is 1 post on traditional C language style. Here we will string together the knowledge from the first two parts: determine configuration parameters based on hardware principles, use HAL API to write a working LED blinking program. But this C version of the code will expose the problems we mentioned earlier—hardcoded macros, lack of type safety, and inconvenient reuse. The purpose of this post is to let you see the pain points with your own eyes, paving the way for the motivation for the later C++ refactoring.

It doesn't end there. After recognizing the pain points, we enter the core C++ refactoring stage, 4 tutorials. The first introduces the CRTP singleton pattern and clock configuration encapsulation; the second dives into GPIO template design, explaining non-type template parameters, `if constexpr` branching, and safe use of `consteval`; the third builds an LED template on top of the GPIO template, showing the actual effect of zero-overhead abstraction; the fourth compares the compilation output of the C version and the C++ version, using disassembly to prove that C++ templates indeed introduce no extra overhead. These 4 posts are the highlight of this series and the core value of our tutorial set.

After that, there is 1 post on C++23 features, systematically sorting out those modern features we used in the code: `consteval`, `std::expected`, `std::span`, etc. Finally, 1 post on pitfalls, organizing all the weird problems we encountered during development—forgetting to enable the clock causing the pin not to work, PC13's special restrictions, choosing the wrong push-pull or open-drain causing incorrect signals—to help you clear the mines in advance.

The design logic of the whole path is very clear: understand hardware first to configure parameters correctly, learn the API to operate hardware, experience the pain of C to understand the value of C++ refactoring. This isn't a tutorial that "gives you the final code right away", but one that takes you through the complete cognitive process from bottom to top. After walking through it, you will not only know "how to write", but also "why write it this way".

---

## The Board in Your Hand

Before writing code officially, let's take a clear look at this Blue Pill board in front of us.

Blue Pill is the common name for the STM32F103C8T6 minimum system board, named because the board shape looks like a blue pill (although the origin of this name is a bit unspeakable). It carries an STM32F103C8T6 chip based on the ARM Cortex-M3 core, with a maximum main frequency of 72MHz, 64KB Flash, and 20KB RAM. In 2026, this configuration looks incredibly shabby—your phone has 12GB of RAM and 256GB of storage, this chip doesn't even have the memory of an icon on your phone screen. But don't forget, the design goal of this chip is real-time control and low power, not running Android. 72MHz Cortex-M3 is enough to drive motors, sample sensors, run communication protocols, and even run a lightweight RTOS.

What we care about most is that LED on the board. Blue Pill usually has an on-board LED connected to pin PC13, connected to VCC3.3V through a current-limiting resistor. Note this connection method—the positive pole of the LED is connected to VCC through a resistor, and the negative pole is connected to PC13. This means when PC13 outputs a low level, current flows from VCC through the resistor and LED into PC13, and the LED lights up; when PC13 outputs a high level (3.3V), the voltage difference across the ends is zero, no current flows, and the LED goes off. So this is a "low level active" LED, which will be reflected in the code as `ActiveLow` in the next post. In the next post, we will draw the LED circuit diagram in detail for analysis; here you just need to remember "PC13, low level on" is enough.

⚠️ The PC13 pin has some special restrictions on STM32F103—it is connected to the RTC domain, the maximum output current is only 3mA, and the drive speed is also limited. So you won't use it to drive large current loads, but lighting an on-board LED is more than enough. This limitation doesn't need special handling in our C++ template, because the LED template only needs to correctly output high and low levels, not involving large current scenarios.

In terms of debugger, ST-Link V2 communicates with Blue Pill via the SWD interface. SWD only needs two signal lines: SWDIO (data line, bidirectional) and SWCLK (clock line, host output). Adding ground GND, a total of three wires can complete all debugging and flashing operations. The Blue Pill board has a 4-pin SWD interface on the right (marked SWDIO, SWCLK, GND, 3.3V), just connect the corresponding pins of ST-Link. If this interface is hard to wire, you can also use the pin headers on the left side of the board—PA13 is SWDIO, PA14 is SWCLK, these two pins have alternate function mapping in SWD mode.

STM32F103C8T6 has three main groups of GPIO ports: GPIOA, GPIOB, GPIOC, each with 16 pins (PA0-PA15, PB0-PB15, PC0-PC15), totaling 48 programmable GPIO pins. GPIOA and GPIOB have relatively complete functions, and most pins can be freely configured as input, output, alternate, or analog modes. PC13 to PC15 of GPIOC have the RTC domain restrictions mentioned above, while PC0 to PC12 do not have these constraints. In our later exercises, the pins you will use are basically concentrated on GPIOA and GPIOC, and GPIOB is used relatively less.

---

## What Our Project Looks Like

Okay, enough about hardware, now let's look at the software. The code for the entire LED control project is in the `led_control` directory, structured as follows:

```text
led_control/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── gpio.cpp
│   └── system_stm32f1xx.c
├── include/
│   ├── gpio.hpp
│   ├── led.hpp
│   ├── rcc.hpp
│   └── singleton.hpp
└── hal/
    └── stm32f1xx_hal_conf.h
```

Let's quickly go through the responsibilities of each file from top to bottom to establish an overall impression; each subsequent tutorial will go into detail one by one.

`main.cpp` is the entry point of the entire program, currently with less than 20 lines of code. It calls `HAL_Init()` to initialize the HAL library, configures the system clock to 64MHz, then constructs an LED object and enters an infinite blinking loop. It's that simple—but behind this simplicity, the template classes in the `include` directory do a lot of work.

`gpio.hpp` is the absolute core of this series. It defines a `Gpio` class template that accepts two non-type template parameters: port (`Port` enum, values are `PortA`, `PortB`, etc., hardware base addresses) and pin number (`Pin`, values 0 to 15). Inside the template, the port address is converted into a `GPIO_TypeDef *` pointer, encapsulating initialization, read, write, and toggle operations. It also uses a nested class `ClockEnable` with `if constexpr` to implement compile-time clock enable branching. The entire template has no virtual functions, no dynamic memory allocation, and the compiled code is identical to hand-written C calling HAL directly.

`led.hpp` builds an LED-specific template on top of the GPIO template. It inherits from `Gpio<Port, Pin>`, adding an `ActiveState` template parameter to indicate whether the LED is high-level or low-level active. The constructor automatically calls `init()` to configure as push-pull output mode, and the `on()` and `off()` methods decide to write high or low based on the value of `ActiveState`. `toggle()` directly delegates to the underlying `Gpio::toggle()`. This is a typical example of zero-overhead abstraction—the LED template provides a semantically clear interface at the source level, but after compiler inlining, `led.on()` is just one `HAL_GPIO_WritePin` call.

`singleton.hpp` is a CRTP (Curiously Recurring Template Pattern) singleton base class. It makes any subclass automatically gain singleton semantics through template inheritance—`instance()` returns a reference to a static local variable, ensuring thread-safe lazy initialization and avoiding global variable initialization order issues. Copy and move constructors are explicitly deleted. Currently, only `Rcc` uses this base class, but more hardware abstraction classes will inherit it later.

The files in the `hal` directory are all system-level infrastructure. `rcc.hpp` and `rcc.cpp` encapsulate RCC clock configuration: first use the HSI internal oscillator to multiply to 64MHz (HSI 8MHz ÷ 2 × 16 = 64MHz), then configure AHB/APB1/APB2 dividers. If clock configuration fails, it calls the `error_handler()` function in `error.hpp` to make the system loop infinitely—in a bare-metal environment without exception handling mechanisms, "stopping" is the safest error response. `stm32f1xx_it.c` only does one thing: provide a `SysTick` interrupt service routine to drive HAL's time base. `syscalls.c` provides an empty `_sbrk` function to satisfy C++ runtime linking requirements—without an operating system, these initialization stub functions must be provided by us.

`CMakeLists.txt` is the build configuration we detailed in the `env_setup` series. It sets up the cross-compilation toolchain, introduces HAL driver source code, configures compiler options (`-O3`), disables exceptions and RTTI (`-fno-exceptions -fno-rtti`), and defines CMake custom targets for flashing and erasing. The C++23 standard is enabled here via `-std=c++23`, which is the prerequisite for the entire project to use modern C++ features.

Now let's not look at the specific implementation, just the final effect. This is the complete code for our `main.cpp`:

```cpp
#include "led.hpp"
#include "rcc.hpp"

int main() {
    HAL_Init();
    SystemClock_Config();

    Led<PortC, 13, ActiveLow> led;
    while (true) {
        led.on();
        HAL_Delay(500);
        led.off();
        HAL_Delay(500);
    }
}
```

Look closely at this code. `HAL_Init()` and `SystemClock_Config()` are system initialization, must-do for every STM32 project, nothing special here. The exciting part is the third line—`Led<PortC, 13, ActiveLow> led;`. This line completes three things at once: telling the compiler we are using pin 13 of GPIOC port, automatically calling `init()` in the constructor to configure the pin as push-pull output mode, and automatically enabling the GPIOC peripheral clock. And as a caller, you only need to declare a variable of the correct type, leaving the rest to the template to process at compile time.

The subsequent blink loop is so straightforward it needs no explanation: delay 500ms, light on, delay 500ms, light off. The method names `led.on()` and `led.off()` are self-documenting—you know what the code is doing without looking at any comments. Compare this to the traditional C `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`, which is easier to understand is clear at a glance.

Of course, I am just showing the final effect now. This "simplicity" is built on the carefully designed templates in `gpio.hpp` and `led.hpp`. Our goal is to let everyone reading this eventually fully understand the design motivation, implementation details, and underlying C++23 features of these templates. By then, you will also be able to design similar hardware abstraction templates and promote this method to any peripherals like UART, SPI, I2C, etc.

---

## Where to Go Next

Hardware and software are ready, the learning roadmap is drawn, and the project structure has been reviewed. From the next post, we will dive headfirst into the hardware principles of GPIO.

The next post will answer a seemingly simple but actually deep question: What is GPIO? It's not just a wire. Inside a GPIO pin, there are input data registers, output data registers, push-pull drivers, open-drain drivers, pull-up resistors, pull-down resistors, Schmitt triggers, alternate function selectors—these things together form a quite exquisite circuit structure, and STM32F103's GPIO supports four working modes: General Input, General Output, Alternate Function, and Analog Mode. Understanding these internal structures is the prerequisite for correctly configuring and using GPIO. We will start from the internal structure block diagram of GPIO in the next post, explaining the difference between push-pull and open-drain output, the role of pull-up and pull-down resistors, and the meaning of pin speed settings.

To do a good job, one must first sharpen one's tools. Eat the hardware thoroughly first, and writing code won't be panic.
