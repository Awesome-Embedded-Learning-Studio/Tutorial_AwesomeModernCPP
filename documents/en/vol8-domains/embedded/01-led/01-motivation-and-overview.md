---
title: 'Part 6: Starting by Lighting the First LED — Why We Use Modern C++ for STM32'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 1
translation:
  source: documents/vol8-domains/embedded/01-led/01-motivation-and-overview.md
  source_hash: 893dbb72425c37cfd9fd38832830d09b5e374ae7eb44c4ca124ac050999ae2ca
  translated_at: '2026-05-26T12:03:11.907957+00:00'
  engine: anthropic
  token_count: 2498
---
# Part 6: Lighting the First LED — Why We Use Modern C++ for STM32

> For everyone who just finished setting up their toolchain and can't wait to make the board do something.
> This is where we truly start writing hardware control code. We won't rush into code just yet — let's thoroughly discuss the "why" first.

---

## Starting with a Single LED

Every embedded developer's journey begins with the exact same thing — lighting an LED. This isn't some trivial matter; it's the embedded world's "Hello World," your first successful conversation with a silent chip. Whether you later use STM32 for motor control, USB communication, or running an RTOS, GPIO (General-Purpose I/O) operations are always the foundation. Just as learning programming starts with `printf`, learning embedded development starts with driving a pin high and low — it's an unavoidable first step.

I remember the first time I made the LED on the Blue Pill light up. That feeling is hard to describe. It's just a tiny light, after all, but you realize that your code — through compilation, linking, format conversion, and SWD protocol transmission — ultimately becomes an electrical signal that physically changes the voltage on a pin, and then the LED turns on. This experience of "code becoming a physical phenomenon" is something pure software development can never give you. In that moment, you feel that the weekend you spent wrestling with the toolchain was worth it.

Speaking of the toolchain, I must admit that writing this piece puts me in a rather complicated mood. The previous five env_setup tutorials — from installing arm-none-eabi-gcc to configuring CMake, from wrestling with WSL2 USB passthrough to getting the OpenOCD debugger working — every step was a trail of tears. Especially that time trying to get the ST-Link recognized under WSL2, I almost gave up entirely. But when we could finally type `arm-none-eabi-g++` in the terminal, then `openocd -f ...`, and see nothing happen on the board — because we hadn't written the right code yet — that feeling was actually reassuring. The environment was fine, the toolchain was connected, flashing worked, and now all we needed was a single line of code to actually control the hardware.

Now we've finally reached this step. No more battling the compiler in the terminal, no more hunting for typos in config files — it's time to write real code that makes a chip work for us.

## Just How Painful Is Traditional Embedded Development?

But before we get to that, I want to talk about why this isn't inherently simple, and why we chose a slightly different path.

What does traditional STM32 development look like? If you've used Keil MDK or IAR, you're surely familiar with that experience — a bloated IDE taking up several gigabytes, an editor with features stuck in the last century, code completion that basically relies on guessing, and an ugly debug interface that's just frustrating. What's worse is that it locks you firmly to the Windows platform. Want to develop on Linux? Sorry, either use Wine to emulate it (and face all sorts of inexplicable crashes) or dutifully fire up a virtual machine. Moreover, Keil's compiler is closed-source, its optimization behavior is opaque, and when something goes wrong, you have no idea how it optimized your code.

Of course, these are just surface-level inconveniences. What truly made me decide to abandon traditional development methods was the bad practices that the C language has accumulated over decades in the embedded field. Look at what a typical STM32 project looks like: `#define` macros everywhere, things like `GPIO_PIN_5`, `GPIOA_BASE` — these preprocessor symbols have no types, no scope, their real values are invisible during debugging, and the compiler's type checking completely fails on them. Then there are those HAL library callback functions, passing function pointers and `void*` userdata back and forth, making type safety a mere illusion. Add layer upon layer of conditional compilation `#ifdef`, `#ifndef`, and cross-platform adaptation turns the code into spaghetti.

The most fatal issue is code reusability. You write an LED driver for the Blue Pill, hardcoding `GPIOC` and `GPIO_PIN_13`. Next time you switch to an STM32F407 board where the LED is on PD12, what do you do? Copy, paste, and change the parameters? What if the project has ten pins to control? Twenty? C macros and structs can solve part of the problem, but ultimately you'll still end up buried in runtime checks and switch-case statements — neither elegant nor efficient.

This isn't about trashing C — C is a great language, and there's a reason it has dominated the embedded field for decades. But times are changing, compilers are improving, and can't we pursue better abstractions without paying a runtime cost?

## Why C++23?

This is where modern C++ enters the picture. Note that I said "modern C++," not the "C with classes" from the 90s. The features brought by the C++23 standard are exactly what embedded development has been dreaming of.

Zero-overhead abstraction is C++'s most core design philosophy — you don't pay for what you don't use. Templates are expanded at compile time, `constexpr` functions are evaluated at compile time, and `if constexpr` makes branch selections at compile time. These mechanisms give your code beautiful abstraction layers at the source level, but the compiled machine code is just as efficient as hand-written C. Your LED template class looks like an elegant type system, but the few `LDR` and `STR` instructions the compiler ultimately generates are exactly the same as if you had directly manipulated the registers. No virtual function overhead, no RTTI overhead, no exception handling overhead — because we explicitly disabled these features at compile time.

Compile-time type safety is another killer advantage. In C, if you pass `13` to a function expecting a port number, the compiler won't make a peep, because they're both just integers. But in our C++ template system, `Port::C` is an independent type. You encode the port and pin information into the type system at compile time, so any wrong parameter is exposed during compilation, rather than you scratching your head staring at the code only after the LED on the board fails to light up.

The code reuse brought by templates goes without saying. Our GPIO template accepts the port and pin as non-type template parameters, which means `Gpio<Port::A, 5>` and `Gpio<Port::C, 13>` are two different classes, each with its own `set()` and `clear()` methods. The clock enable branching is done at compile time using `if constexpr` — if it's port A, enable the GPIOA clock; if it's port B, enable the GPIOB clock — all of this happens at compile time, with zero runtime overhead. You never have to write that kind of runtime table-lookup code to find port numbers again.

Then there are those C++23 sweeteners: the `[[nodiscard]]` attribute makes the compiler warn you when you ignore a return value — this is incredibly important in embedded; the clock configuration failed and you didn't check? The system runs away immediately. `enum class` wraps bare integers into strongly-typed enumerations, putting an end to implicit conversions between different enum values. `constexpr` makes port address conversion a compile-time constant. Individually, these features seem unremarkable, but combined, they can take the safety and maintainability of embedded code up a major notch.

So we chose C++23 not to be trendy, but because it genuinely solves real problems in embedded development. Later on, we'll use plenty of code to prove this point.

---

## What You Need to Prepare

Before we officially begin, there are a few things we need to confirm are in place.

First are those five env_setup tutorials. If you've been skipping around, I strongly recommend going back and reading parts 01 through 05 in their entirety: toolchain installation, project structure, CMake configuration, USB flashing, and debugger configuration. Every line of code in this part is built on the environment set up in those five parts. Your `arm-none-eabi-g++` must compile normally, CMake must build successfully, and OpenOCD must be able to flash firmware to the board. If you haven't got these sorted out yet, stop now and get them done — half an hour won't make a difference.

Then there's basic programming knowledge. I won't start from "what is a variable," but I also won't assume you're a C++ template metaprogramming expert. You need to be familiar with basic C or C++ syntax: variable declarations, function definitions, basic concepts of structs and classes, and the purpose of `#include` header files. If you've written code in any programming language and understand what "function call" and "return value" mean, then you have a sufficient starting point. Advanced features like templates, CRTP (Curiously Recurring Template Pattern), and `constexpr` will be gradually introduced and explained as we use them.

On the hardware side, you only need three things for this entire article: an STM32F103C8T6 Blue Pill development board, an ST-Link V2 debug probe, and a USB cable. Blue Pills can be bought for under ten RMB on Taobao, and ST-Link V2s are even cheaper, just a few RMB. All three together might cost less than a cup of milk tea, but they can take you through the entire journey from lighting an LED to understanding the modern embedded development paradigm. The ST-Link connects to the Blue Pill via three wires: SWDIO, SWCLK, and GND, plus 3.3V to power the board. We covered the specific wiring in detail in the USB section of env_setup, so we won't repeat it here.

The software environment is the same set we configured in env_setup: the `arm-none-eabi-gcc` toolchain, OpenOCD, and CMake 3.22 or higher. Use whatever editor you like; VSCode with the clangd plugin gives a decent code completion experience, but it doesn't matter if you use Vim, Neovim, or even just `cat` — we use CMake for building anyway, so it's editor-agnostic.

⚠️ If you're developing under WSL2, make absolutely sure that USB/IP passthrough is configured and that `lsusb` can see the ST-Link device. This is a prerequisite for flashing; if it's not set up, the subsequent `openocd` commands will definitely fail.

---

## The Road Ahead

Now that the tools and mindset are ready, I want to map out the entire road ahead so you have a mental map. The LED control tutorial series isn't a single article, but a complete learning path from "understanding hardware" to "mastering the API" to "redesigning with modern C++," totaling 13 parts. Why do we need so many parts just to light an LED? Because our goal isn't "just make it light up and call it done," but to understand the principles behind every line of code and the trade-offs in every design decision.

We'll start with the hardware principles of GPIO, which is the most foundational layer. GPIO sounds like just five characters for "general-purpose input/output," but the circuit structure behind it — push-pull output, open-drain output, pull-up resistors, pull-down resistors, Schmitt triggers — each directly affects how you should configure pins and choose operating modes. Without understanding these, writing code is just memorizing incantations; change the scenario and you're lost. We've allocated three parts for hardware principles, starting from the GPIO internal structure block diagram, to circuit analysis of the four operating modes, and then to the register organization of the STM32F103. Don't be afraid of hardware — these things are actually quite easy to understand when drawn as diagrams.

Then the question arises — knowing the hardware principles of GPIO, how do we control it with software? The official ST-provided HAL library is exactly this bridge. HAL stands for Hardware Abstraction Layer, and it wraps low-level register operations into function calls like `HAL_GPIO_Init` and `HAL_GPIO_WritePin`. We'll use three tutorials to break down HAL's GPIO interface: initialization configuration, read/write operations, and clock management. This part will use C language style directly, because HAL itself is a C interface, and we need to learn the "fundamentalist" usage first before we can talk about building better abstractions on top of it.

Then comes one part on traditional C language implementation. Here we'll connect the knowledge from the previous two parts: determine configuration parameters based on hardware principles, and use the HAL API to write a working LED blink program. But this C language version of the code will expose the problems we mentioned earlier — hardcoded macros, lack of type safety, and poor reusability. The purpose of this part is to let you see the pain points with your own eyes, laying the motivational groundwork for the C++ refactoring that follows.

But we're not done yet. Once we recognize the pain points, we enter the most core C++ refactoring stage, spanning four tutorials. The first introduces the CRTP singleton pattern and clock configuration encapsulation; the second dives deep into GPIO template design, explaining non-type template parameters, `if constexpr` branching, and the safe use of `reinterpret_cast`; the third builds an LED template on top of the GPIO template, demonstrating the practical effects of zero-overhead abstraction; the fourth compares the compiled output of the C and C++ versions, using disassembly to prove that C++ templates truly introduce no extra overhead. These four parts are the main event of this series, and they represent the core value of our tutorial.

After that, there's one part dedicated to C++23 features, systematically organizing the modern features we use in our code: `constexpr`, `enum class`, `[[nodiscard]]`, `if constexpr`, and so on. Finally, there's one part on pitfall exercises, compiling all the weird issues we encountered during development — forgetting to enable the clock causing pins not to work, the special limitations of PC13, choosing push-pull vs. open-drain incorrectly causing wrong signals — to help you clear the mines in advance.

The design logic of this entire path is very clear: understand the hardware first to configure parameters correctly, learn the API first to operate the hardware, and experience C's pain points first to understand the value of C++ refactoring. This isn't a tutorial that "throws the final code at you right away," but one that walks you through the complete cognitive process from bottom to top. After completing it, you won't just know "how to write it," but also "why it's written this way."

---

## The Board in Your Hands

Before we write any code, let's take a clear look at the Blue Pill development board in front of us.

Blue Pill is the common name for the STM32F103C8T6 minimum system board, named because the board's shape resembles a blue pill (although the origin of this name is a bit hard to explain). The STM32F103C8T6 chip it carries is a microcontroller based on the ARM Cortex-M3 core, with a maximum clock frequency of 72MHz, 64KB Flash, and 20KB RAM. In 2026, this spec looks downright pitiful — your phone easily has 12GB RAM and 256GB storage, and this chip doesn't even have enough memory for a single icon on your phone screen. But don't forget that this chip's design goal is real-time control and low power consumption, not running Android. A 72MHz Cortex-M3 is more than enough to drive motors, sample sensors, run communication protocols, and even run a lightweight RTOS.

What we care about most is the LED on the board. Blue Pills typically have an onboard LED connected to the PC13 pin, wired through a current-limiting resistor to VCC3.3V. Pay attention to this wiring — the LED's anode goes through the resistor to VCC, and the cathode connects to PC13. This means that when PC13 outputs a low level, current flows from VCC through the resistor and LED into PC13, and the LED lights up; when PC13 outputs a high level (3.3V), the voltage difference across both ends is zero, no current flows, and the LED turns off. So this is an "active-low" LED, which will be reflected in the code later as `ActiveLow`. In the next part, we'll draw the LED's circuit diagram in detail for analysis; for now, you just need to remember "PC13, lights up on low."

⚠️ The PC13 pin has some special limitations on the STM32F103 — it's connected to the RTC domain, the maximum output current is only 3mA, and its drive speed is limited. So you wouldn't use it to drive high-current loads, but lighting an onboard LED is more than enough. This limitation doesn't need special handling in our C++ template, because the LED template only needs to correctly output high and low levels, and doesn't involve high-current scenarios.

On the debugger side, the ST-Link V2 communicates with the Blue Pill through the SWD (Serial Wire Debug) interface. SWD only needs two signal lines: SWDIO (data line, bidirectional) and SWCLK (clock line, host output). Add the ground line GND, and just three wires can complete all debugging and flashing operations. The Blue Pill board has a four-pin SWD interface on the right side (labeled SWDIO, SWCLK, GND, 3.3V); just connect the corresponding ST-Link pins to it. If this interface is hard to wire to, you can also use the pin headers on the left side of the board — PA13 is SWDIO, PA14 is SWCLK, and these two pins have alternate function mappings in SWD mode.

The STM32F103C8T6 has three main GPIO port groups: GPIOA, GPIOB, and GPIOC, each with 16 pins (PA0-PA15, PB0-PB15, PC0-PC15), for a total of 48 programmable GPIO pins. GPIOA and GPIOB have relatively complete functionality, and most of their pins can be freely configured as input, output, alternate function, or analog mode. PC13 through PC15 on GPIOC have the RTC domain limitations mentioned above, while PC0 through PC12 don't have these constraints. In our later exercises, the pins you'll use are basically concentrated on GPIOA and GPIOC, with GPIOB being used relatively less.

---

## What Our Project Looks Like

Alright, we've talked enough about hardware; now let's look at the software. The code for the entire LED control project is in the `led_blink` directory, structured as follows:

```text
1_led_control/
├── device/
│   ├── gpio/
│   │   └── gpio.hpp        # GPIO泛化模板（本系列核心中的核心）
│   └── led.hpp             # LED专用模板
├── base/
│   └── simple_singleton.hpp # CRTP单例基类
├── system/
│   ├── clock.h             # 系统时钟配置（头文件）
│   ├── clock.cpp           # 系统时钟配置（实现）
│   ├── dead.hpp            # 错误处理：死循环挂起
│   ├── hal_mock.c          # HAL中断桥接
│   └── syscall.c           # C运行时最小实现
├── main.cpp                # 程序入口：LED闪烁
├── CMakeLists.txt          # CMake构建配置
├── STM32F103C8TX_FLASH.ld  # 链接脚本
└── stm32f1xx_hal_conf.h    # HAL配置头文件
```

Let's quickly go through each file's responsibilities from top to bottom to build an overall impression first; we'll dive into each one in subsequent tutorials.

`main.cpp` is the entry point for the entire program, currently with less than 20 lines of code. It calls `HAL_Init()` to initialize the HAL library, configures the system clock to 64MHz, then constructs an LED object and enters an infinite blink loop. It's just that simple — but behind this simplicity, the template classes in the `drivers` directory are doing a lot of heavy lifting.

`gpio.hpp` is the absolute core of this series. It defines a `Gpio` class template that accepts two non-type template parameters: the port (a `Port` enum, with values like `GPIOA_BASE`, `GPIOB_BASE` — these hardware base addresses) and the pin number (`uint8_t`, with values from `0` to `15`). Inside the template, the port address is converted to a `GPIO_TypeDef*` pointer, encapsulating initialization, read/write, and toggle operations. It also uses a nested class `ClockEnable` with `if constexpr` to implement compile-time clock enable branching. The entire template has no virtual functions and no dynamic memory allocation; the compiled code is identical to hand-written C directly calling HAL.

`led.hpp` builds an LED-specific template on top of the GPIO template. It inherits from `Gpio<Port, Pin>` and adds an `ActiveLevel` template parameter to indicate whether the LED is active-high or active-low. The constructor automatically calls `set_mode(Mode::PushPull)` to configure push-pull output mode. The `on()` and `off()` methods decide whether to write high or low based on the value of `ActiveLevel`. `toggle()` directly delegates to the underlying `Gpio::toggle()`. This is a textbook example of zero-overhead abstraction — the LED template provides a semantically clear interface at the source level, but after the compiler inlines it, `led.on()` is just a single `HAL_GPIO_WritePin` call.

`singleton.hpp` is a CRTP (Curiously Recurring Template Pattern) singleton base class. It uses template inheritance to give any subclass automatic singleton semantics — `instance()` returns a reference to a static local variable, guaranteeing thread-safe lazy initialization while avoiding global variable initialization order issues. Copy and move constructors are explicitly deleted. Currently, only `SystemClock` uses this base class, but more hardware abstraction classes will inherit from it later.

The files in the `system` directory are all system-level infrastructure. `system_clock.hpp` and `system_clock.cpp` encapsulate RCC clock configuration: first using the HSI internal oscillator to multiply up to 64MHz (HSI 8MHz ÷ 2 × 16 = 64MHz), then configuring the AHB (Advanced High-performance Bus)/APB1/APB2 dividers. If the clock configuration fails, it calls the `halt()` function in `error_handler.hpp` to put the system into an infinite loop — in a bare-metal environment without exception handling mechanisms, "stopping" is the safest error response. `stm32f1xx_it.cpp` does only one thing: provide the `SysTick` interrupt service routine (ISR) to drive HAL's timebase. `syscalls.cpp` provides an empty `_sbrk` function to satisfy the C++ runtime's linking requirements — in an environment without an operating system, these initialization stub functions must be provided by us.

`CMakeLists.txt` is the build configuration we dissected in detail in the env_setup series. It sets up the cross-compile toolchain, brings in HAL driver source code, configures compiler flags (`-Wall -Wextra`), disables exceptions and RTTI (`-fno-exceptions -fno-rtti`), and defines CMake custom targets for flashing and erasing. The C++23 standard is enabled here through `-std=c++23`, which is the prerequisite for the entire project to use modern C++ features.

For now, let's not look at the specific implementation, but only at the final result. Here is the complete code for our `main.cpp`:

```cpp
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
    /* led setups! */
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;

    while (1) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

Look closely at this code. `HAL_Init()` and `SystemClock::instance().init()` are system initialization that every STM32 project must do; there's nothing special about this part. The exciting part is the third line — `Led<Port::C, 13, ActiveLow> led{};`. This single line accomplishes three things simultaneously: it tells the compiler we're using pin 13 of the GPIOC port, it automatically calls the constructor's `set_mode()` to configure the pin as push-pull output, and it automatically enables the GPIOC peripheral clock. And as the caller, you only need to declare a variable with the correct type, leaving everything else to the template to handle at compile time.

The blink loop that follows is so straightforward it needs no explanation: delay 500 milliseconds, turn on, delay 500 milliseconds, turn off. The method names `led.on()` and `led.off()` are self-documenting — you can tell what the code is doing without reading any comments. Compare this with the traditional C approach of `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`, and it's obvious at a glance which is easier to understand.

Of course, I'm just showing the final result right now. This "simplicity" is built on the carefully designed templates in `gpio.hpp` and `led.hpp`. Our goal is to ensure that everyone who reads this will eventually fully understand the design motivation, implementation details, and underlying C++23 features of these templates. By that time, you'll be able to design similar hardware abstraction templates yourself and extend this approach to any peripheral like UART, SPI, or I2C.

---

## Where to Next

Both hardware and software are ready, the learning path is mapped out, and we've gone through the project structure. Starting from the next part, we're going to dive headfirst into the hardware principles of GPIO.

In the next part, we'll answer a question that seems simple but actually has quite a bit of depth: what exactly is GPIO? It's not just a wire. Inside a GPIO pin, there's an input data register, an output data register, a push-pull driver, an open-drain driver, a pull-up resistor, a pull-down resistor, a Schmitt trigger, and an alternate function selector — all of these together form a rather exquisite circuit structure. The STM32F103's GPIO supports four operating modes: general-purpose input, general-purpose output, alternate function, and analog mode. Understanding these internal structures is a prerequisite for correctly configuring and using GPIO. We'll start from the GPIO internal structure block diagram in the next part, clearly explaining the difference between push-pull and open-drain output, the role of pull-up and pull-down resistors, and the meaning of pin speed settings.

The craftsman who wishes to do good work must first sharpen his tools. Once we thoroughly understand the hardware, writing code becomes a breeze.
