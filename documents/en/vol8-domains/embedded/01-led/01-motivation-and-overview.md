---
title: 'Part 6: Starting with Lighting Up the First LED — Why We Use Modern C++ to
  Write STM32'
description: ''
tags:
- beginner
- cpp-modern
- stm32f1
difficulty: beginner
platform: stm32f1
chapter: 15
order: 1
---
# Part 6: Starting with the First LED — Why We Use Modern C++ for STM32

> For everyone who just finished setting up their toolchain and can't wait to make the board do something.
> This is where we truly start writing hardware control code. We won't rush into code just yet — let's thoroughly discuss the "why" first.

---

## Starting with a Single LED

Every embedded developer's journey begins with the exact same thing — lighting up an LED. This is no trivial matter; it is the embedded world's "Hello World," your first successful conversation with a silent chip. Whether you later use STM32 for motor control, USB communication, or running an RTOS, GPIO (General-Purpose I/O) operations remain the foundation of everything. Just as learning programming starts with `print("Hello World")`, learning embedded development starts with driving a pin high and low — an unavoidable first step.

I remember the first time I made the LED on the Blue Pill light up. That feeling is hard to describe. It's just a tiny light bulb, but you realize that your code — through compilation, linking, format conversion, and SWD protocol transmission — ultimately becomes an electrical signal that physically changes the voltage on a pin, and then the LED turns on. This experience of "code becoming a physical phenomenon" is something pure software development can never give you. At that moment, you feel that the weekend spent wrestling with the toolchain was totally worth it.

Speaking of the toolchain, I must admit I have mixed feelings writing this. The previous five env_setup tutorials — from installing arm-none-eabi-gcc to configuring CMake, from wrestling with WSL2 USB passthrough to getting the OpenOCD debugger working — every step was a struggle. Especially that time trying to get the ST-Link recognized under WSL2, I almost gave up entirely. But when we could finally type `cmake --build build` in the terminal, then `openocd -f ...`, and see nothing happen on the board — because we hadn't written the right code yet — that feeling was actually reassuring. The environment was fine, the toolchain worked, flashing ran successfully, and now all we needed was one line of code to actually control the hardware.

Now we have finally reached this step. No more battling the compiler in the terminal, no more hunting for typos in config files — it's time to write real code that makes a chip work for us.

## Just How Painful Is Traditional Embedded Development?

But before we get to that, I want to talk about why this isn't inherently simple, and why we chose a slightly different path.

What does traditional STM32 development look like? If you've used Keil MDK or IAR, you're surely familiar with the experience — a bloated IDE taking up several gigabytes, an editor with features stuck in the last century, code completion that basically relies on guessing, and an ugly debug interface that just annoys you. What's worse is that it locks you firmly to the Windows platform. Want to develop on Linux? Sorry, either use Wine to emulate it (and face all sorts of inexplicable crashes) or dutifully fire up a virtual machine. Moreover, Keil's compiler is closed-source, its optimization behavior is opaque, and when something goes wrong, you have no idea how it optimized your code.

Of course, these are just surface-level inconveniences. What truly made me decide to abandon traditional development methods was the bad practices that the C language has accumulated over decades in the embedded field. Look at what a typical STM32 project looks like: `#define` macros everywhere, things like `GPIO_PIN_5`, `GPIOA`, these preprocessor symbols have no types, no scope, their real values are invisible during debugging, and the compiler's type checking completely fails on them. Then there are those HAL library callback functions, passing function pointers and `void*` userdata back and forth, making type safety a joke. Add layers of conditional compilation `#ifdef`, `#if defined()`, and cross-platform adaptation turns the code into spaghetti.

The most fatal issue is code reusability. You write an LED driver for the Blue Pill, hardcoding `GPIOC` and `GPIO_PIN_13` inside. Next time you switch to an STM32F407 board where the LED is on PD12, what do you do? Copy, paste, and change the parameters? What if the project has ten pins to control? Twenty? C macros and structs can solve part of the problem, but ultimately you'll still end up buried in runtime checks and switch-case statements — neither elegant nor efficient.

This isn't bashing the C language — C is a great language, and there's a reason it has dominated the embedded field for decades. But times have changed, compilers have improved, and can't we pursue better abstractions without paying a runtime cost?

## Why C++23?

This is where modern C++ enters the picture. Note that I said "modern C++," not the C-with-classes from the 90s. The features brought by the C++23 standard are exactly what embedded development has been dreaming of.

Zero-overhead abstraction is C++'s core design philosophy — you don't pay for what you don't use. Templates are expanded at compile time, `constexpr` functions are evaluated at compile time, and `if constexpr` makes branch selections at compile time. These mechanisms give your code beautiful abstraction layers at the source level, but the compiled machine code is just as efficient as hand-written C. Your LED template class looks like an elegant type system, but the few `STR` and `LDR` instructions the compiler ultimately generates are exactly the same as if you had directly manipulated registers. No virtual function overhead, no RTTI overhead, no exception handling overhead — because we explicitly disabled these features at compile time.

Compile-time type safety is another killer advantage. In C, if you pass `13` to a function expecting a port number, the compiler won't make a peep, because they're both just integers. But in our C++ template system, `Port::C` is a distinct type. You encode port and pin information into the type system at compile time, so any wrong parameter is exposed during compilation, rather than you scratching your head over the code only after the LED on the board fails to light up.

The code reuse brought by templates goes without saying. Our GPIO template accepts the port and pin as non-type template parameters, which means `Gpio<Port::A, 5>` and `Gpio<Port::C, 13>` are two different classes, each with its own `set()` and `clear()` methods. The clock enable branching is done at compile time using `if constexpr` — if it's port A, enable the GPIOA clock; if port B, enable the GPIOB clock — all of this happens at compile time with zero runtime overhead. You never have to write those runtime lookup-table codes to find port addresses again.

Then there are those C++23 sweeteners: the `[[nodiscard]]` attribute makes the compiler warn you when you ignore a return value — this is crucial in embedded, your clock configuration failed and you didn't check it? The system goes off the rails. `enum class` wraps bare integers into strongly-typed enumerations, eliminating implicit conversions between different enum values. `constexpr` makes port address conversion a compile-time constant. Individually, these features seem unremarkable, but combined, they can take the safety and maintainability of embedded code up a major notch.

So we chose C++23 not to chase trends, but because it genuinely solves practical problems in embedded development. Later on, we will use plenty of code to prove this point.

---

## What You Need to Prepare

Before we officially begin, there are a few things we need to confirm are in place.

First are those five env_setup tutorials. If you've been skipping around, I strongly recommend going back and reading parts 01 through 05 in their entirety: toolchain installation, project structure, CMake configuration, USB flashing, and debugger configuration. Every line of code in this part is built on the environment set up in those five parts. Your `main.cpp` must compile normally, CMake must build successfully, and OpenOCD must be able to flash firmware to the board. If you haven't got these sorted out, stop now and get them done — it won't take more than half an hour.

Then there's basic programming knowledge. I won't start from "what is a variable," but I also won't assume you're a C++ template metaprogramming expert. You need to be familiar with basic C or C++ syntax: variable declarations, function definitions, basic concepts of structs and classes, and the purpose of `#include` header files. If you've written code in any programming language and understand what "function call" and "return value" mean, then you have a sufficient starting point. Advanced features like templates, CRTP (Curiously Recurring Template Pattern), and `constexpr` will be gradually introduced and explained as we use them.

On the hardware side, you only need three things for this entire article: an STM32F103C8T6 Blue Pill development board, an ST-Link V2 debug probe, and a USB cable. Blue Pills can be bought for under ten yuan on Taobao, and ST-Link V2s are even cheaper, just a few yuan. Together, these three items might cost less than a cup of milk tea, but they can take you through the entire journey from lighting an LED to understanding the modern embedded development paradigm. The ST-Link connects to the Blue Pill via three wires: SWDIO, SWCLK, and GND, plus 3.3V to power the board. We covered the specific wiring in detail in the USB section of env_setup, so we won't repeat it here.

The software environment is the same set we configured in env_setup: the `arm-none-eabi-gcc` toolchain, OpenOCD, and CMake 3.22 or higher. Use whatever editor you like; VSCode with the clangd plugin gives a decent code completion experience, but it doesn't matter if you use Vim, Neovim, or even just `cat` — since we build with CMake, it's editor-agnostic.

> ⚠️ If you're developing under WSL2, make sure USB/IP passthrough is properly configured and `lsusb` can see the ST-Link device. This is a prerequisite for flashing; if it's not set up, the subsequent `openocd` commands will definitely fail.

---

## The Road Ahead

Now that both tools and mindset are ready, I want to map out the entire road ahead so you have a mental map. The LED control tutorial series isn't a single article, but a complete learning path from "understanding hardware" to "mastering the API" to "redesigning with modern C++," totaling 13 parts. Why do we need so many parts just to light an LED? Because our goal isn't "just make it light up and call it done," but to understand the principles behind every line of code and the trade-offs in every design decision.

We'll start with the hardware principles of GPIO, which is the most foundational layer. GPIO sounds like just five characters for "General-Purpose I/O," but the circuit structure behind it — push-pull output, open-drain output, pull-up resistors, pull-down resistors, Schmitt triggers — each directly affects how you configure pins and choose operating modes. Without understanding these, you're just memorizing incantations when writing code, and you'll be lost when the scenario changes. We've allocated 3 parts for hardware principles, covering everything from the GPIO internal structure block diagram to circuit analysis of the four operating modes, to the register organization of the STM32F103. Don't be afraid of hardware — these things are actually quite easy to understand when drawn out as diagrams.

Then the question arises — knowing the hardware principles of GPIO, how do we control it with software? The official ST-provided HAL library is exactly this bridge. HAL stands for Hardware Abstraction Layer (HAL), and it wraps low-level register operations into function calls like `HAL_GPIO_Init()` and `HAL_GPIO_WritePin()`. We'll use 3 tutorials to break down HAL's GPIO interface: initialization configuration, read/write operations, and clock management. This part will use C language style directly for the code, because HAL itself is a C interface, and we need to learn the "fundamentalist" usage first before we can talk about building better abstractions on top of it.

Then comes 1 part on traditional C language implementation. Here we'll connect the knowledge from the previous two sections: determine configuration parameters based on hardware principles, and use the HAL API to write a working LED blink program. But this C language version of the code will expose the problems we mentioned earlier — hardcoded macros, lack of type safety, and poor reusability. The purpose of this part is to let you see the pain points with your own eyes, laying the motivational groundwork for the C++ refactoring that follows.

But we're not done yet. Once we recognize the pain points, we enter the most core C++ refactoring stage, spanning 4 tutorials. The first introduces the CRTP singleton pattern and clock configuration encapsulation; the second dives deep into GPIO template design, explaining non-type template parameters, `if constexpr` branching, and the safe use of `constexpr`; the third builds an LED template on top of the GPIO template, demonstrating the practical effects of zero-overhead abstraction; the fourth compares the compiled output of the C and C++ versions, using disassembly to prove that C++ templates truly introduce no extra overhead. These 4 parts are the main event of this series, and they represent the core value of our tutorial.

After that, there's 1 part dedicated to C++23 features, systematically reviewing the modern features we use in our code: `[[nodiscard]]`, `enum class`, `constexpr`, `if constexpr`, and more. Finally, there's 1 part on pitfall exercises, compiling all the weird issues we encountered during development — forgetting to enable the clock causing pins not to work, PC13's special limitations, choosing push-pull vs. open-drain incorrectly causing wrong signals — to help you clear mines in advance.

The design logic of this entire path is very clear: understand the hardware first to configure parameters correctly, learn the API first to operate the hardware, and experience C's pain points first to understand the value of C++ refactoring. This isn't a tutorial that "throws the final code at you right away," but one that walks you through the complete cognitive process from bottom to top. After completing it, you won't just know "how to write it," but also "why it's written this way."

---

## The Board in Your Hands

Before we write any code, let's take a clear look at the Blue Pill development board in front of us.

Blue Pill is the common name for the STM32F103C8T6 minimum system board, named because the board shape resembles a blue pill (although the origin of this name is a bit questionable). The STM32F103C8T6 chip it carries is a microcontroller based on the ARM Cortex-M3 core, with a maximum clock frequency of 72MHz, 64KB Flash, and 20KB RAM. In 2026, this spec looks incredibly meager — your phone easily has 12GB RAM and 256GB storage, and this chip doesn't even have enough memory for a single icon on your phone screen. But don't forget, this chip's design goal is real-time control and low power consumption, not running Android. A 72MHz Cortex-M3 is more than enough to drive motors, sample sensors, run communication protocols, and even run a lightweight RTOS.

What we care about most is the LED on the board. Blue Pill usually has an onboard LED connected to the PC13 pin, wired through a current-limiting resistor to VCC3.3V. Pay attention to this connection method — the LED's anode is connected to VCC through a resistor, and the cathode is connected to PC13. This means when PC13 outputs a low level, current flows from VCC through the resistor and LED into PC13, and the LED lights up; when PC13 outputs a high level (3.3V), the voltage difference across both ends is zero, no current flows, and the LED turns off. So this is an "active-low" LED, which will be reflected in the code later as `ActiveLow`. In the next part, we'll draw out the LED's circuit diagram in detail for analysis; for now, you just need to remember "PC13, lights up on low."

> ⚠️ The PC13 pin has some special limitations on the STM32F103 — it's connected to the RTC domain, the maximum output current is only 3mA, and the drive speed is limited. So you wouldn't use it to drive high-current loads, but lighting an onboard LED is more than enough. This limitation doesn't need special handling in our C++ template, because the LED template only needs to correctly output high and low levels, and doesn't involve high-current scenarios.

For debugging, the ST-Link V2 communicates with the Blue Pill through the SWD (Serial Wire Debug) interface. SWD only needs two signal lines: SWDIO (data line, bidirectional) and SWCLK (clock line, host output). Add the ground line GND, and just three wires can complete all debugging and flashing operations. The Blue Pill board has a four-pin SWD interface on the right side (labeled SWDIO, SWCLK, GND, 3.3V); just connect the corresponding pins from the ST-Link. If this interface is hard to wire to, you can also use the pin headers on the left side of the board — PA13 is SWDIO, PA14 is SWCLK, and these two pins have alternate function mappings in SWD mode.

The STM32F103C8T6 has three main GPIO port groups: GPIOA, GPIOB, and GPIOC, each with 16 pins (PA0-PA15, PB0-PB15, PC0-PC15), for a total of 48 programmable GPIO pins. GPIOA and GPIOB have relatively complete functionality, and most of their pins can be freely configured as input, output, alternate function, or analog mode. PC13 through PC15 on GPIOC have the RTC domain limitations mentioned above, while PC0 through PC12 don't have these constraints. In our later exercises, the pins you'll use are basically concentrated on GPIOA and GPIOC, with GPIOB being used relatively less.

---

## What Our Project Looks Like

Alright, we've talked enough about hardware; now let's look at the software. The code for the entire LED control project is in the `led_blink/` directory, structured as follows:

```text
led_blink/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── hal/
│   │   ├── gpio.hpp
│   │   ├── led.hpp
│   │   └── clock.hpp
│   └── system/
│       ├── clock_config.cpp
│       ├── clock_config.hpp
│       ├── fault_handler.cpp
│       ├── fault_handler.hpp
│       ├── stm32f1xx_hal_conf.h
│       ├── systick_handler.cpp
│       └── cxx_init.cpp
└── drivers/
    ├── CMSIS/
    └── STM32F1xx_HAL_Driver/
```

Let's quickly go through each file's responsibilities top-down to build an overall impression; we'll dive into each one in subsequent tutorials.

`src/main.cpp` is the entry point of the entire program, currently with less than 20 lines of code. It calls `HAL_Init()` to initialize the HAL library, configures the system clock to 64MHz, then constructs an LED object and enters an infinite blink loop. It's that simple — but behind this simplicity, the template classes in the `src/hal/` directory do a lot of heavy lifting.

`src/hal/gpio.hpp` is the absolute core of this series. It defines a `Gpio` class template that accepts two non-type template parameters: the port (a `Port` enum, with values being hardware base addresses like `GPIOA_BASE`, `GPIOC_BASE`) and the pin number (`uint8_t`, with values from `0` to `15`). Inside the template, the port address is converted to a `GPIO_TypeDef*` pointer, encapsulating initialization, read/write, and toggle operations. It also uses a nested class `ClockEnable` with `if constexpr` to implement compile-time clock enable branching. The entire template has no virtual functions and no dynamic memory allocation; the compiled code is identical to hand-written C calling HAL directly.

`src/hal/led.hpp` builds a dedicated LED template on top of the GPIO template. It inherits from `Gpio<Port, Pin>` and adds an `ActiveLevel` template parameter to indicate whether the LED is active-high or active-low. The constructor automatically calls `init()` to configure push-pull output mode, and the `on()` and `off()` methods decide whether to write high or low based on the value of `ActiveLevel`. `toggle()` simply delegates to the underlying `Gpio::toggle()`. This is a textbook example of zero-overhead abstraction — the LED template provides a semantically clear interface at the source level, but after the compiler inlines it, `led.on()` is just a single `HAL_GPIO_WritePin()` call.

`src/hal/clock.hpp` is a CRTP (Curiously Recurring Template Pattern) singleton base class. It gives any subclass automatic singleton semantics through template inheritance — `instance()` returns a reference to a static local variable, ensuring thread-safe lazy initialization while avoiding global variable initialization order issues. Copy and move constructors are explicitly deleted. Currently, only `SystemClock` uses this base class, but more hardware abstraction classes will inherit from it later.

The files in the `src/system/` directory are all system-level infrastructure. `clock_config.cpp` and `clock_config.hpp` encapsulate the RCC clock configuration: first using the HSI internal oscillator to multiply up to 64MHz (HSI 8MHz ÷ 2 × 16 = 64MHz), then configuring the AHB/APB1/APB2 dividers. If clock configuration fails, it calls the `halt()` function in `fault_handler.cpp` to put the system into an infinite loop — in a bare-metal environment without exception handling mechanisms, "stopping" is the safest error response. `systick_handler.cpp` does only one thing: provide the `SysTick_Handler` interrupt service routine (ISR) to drive HAL's timebase. `cxx_init.cpp` provides an empty `__libc_init_array()` function to satisfy the C++ runtime's linking needs — in an environment without an operating system, these initialization stub functions must be provided by us.

`CMakeLists.txt` is the build configuration we broke down in detail in the env_setup series. It sets up the cross-compile toolchain, brings in HAL driver source code, configures compiler flags (`-Wall -Wextra`), disables exceptions and RTTI (`-fno-exceptions -fno-rtti`), and defines CMake custom targets for flashing and erasing. The C++23 standard is enabled here through `-std=c++23`, which is the prerequisite for the entire project to use modern C++ features.

For now, let's not look at the specific implementation, just the final result. Here is the complete code for our `src/main.cpp`:

```cpp
#include "hal/led.hpp"
#include "system/clock_config.hpp"

int main() {
    HAL_Init();
    SystemClock::instance().init();

    Led<Port::C, 13, ActiveLevel::Low> led;
    while (true) {
        HAL_Delay(500);
        led.on();
        HAL_Delay(500);
        led.off();
    }
}
```

Look closely at this code. `HAL_Init()` and `SystemClock::instance().init()` are system initialization that every STM32 project must do; there's nothing special about this part. The exciting part is the third line — `Led<Port::C, 13, ActiveLevel::Low> led;`. This single line accomplishes three things simultaneously: it tells the compiler we're using pin 13 of the GPIOC port, automatically calls the constructor's `init()` to configure the pin as push-pull output mode, and automatically enables the GPIOC peripheral clock. And as the caller, you only need to declare a variable with the correct type, leaving everything else to the template to handle at compile time.

The blink loop that follows is so straightforward it needs no explanation: delay 500 milliseconds, turn on, delay 500 milliseconds, turn off. The method names `led.on()` and `led.off()` are self-documenting — you can tell what the code is doing without reading any comments. Compare this with the traditional C approach of `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET)`, and it's obvious at a glance which is easier to understand.

Of course, I'm just showing the final result right now. This "simplicity" is built on the carefully designed templates in `gpio.hpp` and `led.hpp`. Our goal is to ensure that everyone reading this can eventually fully understand the design motivation, implementation details, and underlying C++23 features of these templates. By that time, you'll be able to design similar hardware abstraction templates yourself and extend this approach to any peripheral like UART, SPI, or I2C.

---

## Where to Next

Both hardware and software are ready, the learning path is mapped out, and we've gone through the project structure. Starting from the next part, we're going to dive headfirst into the hardware principles of GPIO.

In the next part, we'll answer a question that seems simple but actually has quite a bit of depth: what exactly is GPIO? It's not just a wire. Inside a GPIO pin, there's an input data register, an output data register, a push-pull driver, an open-drain driver, a pull-up resistor, a pull-down resistor, a Schmitt trigger, and an alternate function selector — all of these together form a rather exquisite circuit structure. The STM32F103's GPIO supports four operating modes: general-purpose input, general-purpose output, alternate function, and analog mode. Understanding these internal structures is a prerequisite for correctly configuring and using GPIO. In the next part, we'll start with the GPIO internal structure block diagram, clearly explaining the differences between push-pull and open-drain output, the roles of pull-up and pull-down resistors, and the meaning of pin speed settings.

To do a good job, one must first sharpen their tools. Once we thoroughly understand the hardware, writing code becomes a breeze.
