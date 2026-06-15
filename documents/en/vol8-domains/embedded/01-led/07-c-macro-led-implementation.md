---
chapter: 15
difficulty: beginner
order: 7
platform: stm32f1
reading_time_minutes: 21
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 12: LED Drivers in the C Macro Era — Works, But Not Elegant'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/01-led/07-c-macro-led-implementation.md
  source_hash: c964b0dca0544a4195b04c0cce76c6064075cbe1983c3fcbd1fd11237e9983af
  token_count: 2561
  translated_at: '2026-05-26T12:07:22.220897+00:00'
description: ''
---
# Part 12: LED Drivers in the C Macro Era — It Works, But It Isn't Elegant

> For everyone who thinks "C macro wrappers are good enough."
> In this part, we wrap an LED driver using traditional C macros, the standard approach in most STM32 tutorials. The code works, and the logic is clear. But when we closely examine its extensibility and safety, you will discover the ticking time bombs hidden behind those seemingly harmless `#define`.

---

## Preface: From Working to Working Well

In the previous part, we wrote a complete LED blinking program using raw HAL APIs. It genuinely works—the little light on the board blinks, proving that the entire toolchain, compilation process, and flashing workflow are functional. That moment is genuinely rewarding; after all, the pitfalls you navigate from setting up a cross-compilation environment from scratch to seeing your first line of code run on hardware are known only to you.

But if you look back at that code, you will notice an uncomfortable truth: it is hard-bound to the PC13 pin. From selecting the GPIO port, specifying the pin number, and calling the clock enable function, to setting the logic level—everything is hardcoded as a literal. Want to move this LED to PA0? You have to find every occurrence of GPIOC in the code and change it to GPIOA, change every GPIO_PIN_13 to GPIO_PIN_0, and remember to change the clock enable from `__HAL_RCC_GPIOC_CLK_ENABLE()` to `__HAL_RCC_GPIOA_CLK_ENABLE()`. Miss a single spot? The LED won't light up, you will stare blankly at the board, and you might even think the hardware is broken.

This is why most STM32 tutorials introduce C macros. By using macro definitions to centralize hardware parameters in a header file, you only need to modify a few lines of `#define` when making changes, rather than searching for a needle in a haystack across the entire source file. This is a pragmatic choice that is perfectly adequate in many real-world projects—I do not intend to dismiss C macros as worthless here, because they genuinely solve a subset of the problem.

However, this part also serves as the starting point for our subsequent C++ refactoring. I need to fully lay out the C macro approach first, letting you see both its strengths and its weaknesses. That way, when we use C++ templates to solve these problems one by one later, you can understand the motivation behind each refactoring step. We are not refactoring to show off, but are being genuinely driven by real needs.

---

## Wrapping an LED Driver with C Macros: The Classic Approach

Let us start with the most standard C macro-style LED driver. You can find this approach in any STM32 tutorial, and its core idea is simple: centralize all hardware-related parameters in macro definitions within a header file, then provide a set of functions with clear semantics to operate the LED.

First, the header file `led.h`:

```c
/* led.h —— C宏风格LED驱动头文件 */
#ifndef LED_H
#define LED_H

#include "stm32f1xx_hal.h"

/* 硬件定义：端口和引脚 */
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

/* LED电平定义：低电平点亮 */
#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET

/* LED操作函数 */
void led_init(void);
void led_on(void);
void off(void);
void led_toggle(void);

#endif /* LED_H */
```

Then the corresponding implementation file `led.c`:

```c
/* led.c —— C宏风格LED驱动实现 */
#include "led.h"

void led_init(void) {
    LED_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin   = LED_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &g);
}

void led_on(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_ON_LEVEL);
}

void led_off(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);
}

void led_toggle(void) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}
```

Let us break down the design intent of this code section by section.

First is `#define LED_PORT GPIOC`, which defines the GPIO port connected to the LED as a macro. This is much more flexible than hardcoding GPIOC directly in the code—if the hardware is revised and the LED moves from PC13 to PB5, you only need to change `GPIOC` to `GPIOB` in the header file, and everywhere that references `LED_PORT` will automatically update. This is the most basic and effective use of C macros: centralized management of configuration constants.

Next is `#define LED_PIN GPIO_PIN_13`, which extracts the pin number. The same logic applies; changing the pin only requires modifying this single line.

Clock enabling is a detail that is often overlooked. STM32 peripherals have their clocks disabled by default after power-on, and you need to manually enable the corresponding port's clock before using the GPIO function. `#define LED_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()` wraps the clock enable as a macro as well. In the `led_init()` function, we simply call `LED_CLK_ENABLE()` to turn on the clock, and the caller does not need to know which port's clock is being enabled at the lower level.

Then comes the logic level definitions. The LED on the Blue Pill board is active-low—meaning pulling PC13 low (GPIO_PIN_RESET) turns the LED on, and pulling it high (GPIO_PIN_SET) turns it off. This hardware detail is encapsulated in the `LED_ON_LEVEL` and `LED_OFF_LEVEL` macros. Why do this? Because if you directly write `HAL_GPIO_WritePin(..., GPIO_PIN_RESET)` in the `led_on()` function, three months later when you revisit this code, you will wonder, "Why is turning on the light RESET?" Encapsulating hardware characteristics in clearly named macros greatly improves code readability.

Finally, there are four functions. `led_init()` handles initialization, including turning on the clock and configuring the GPIO; `led_on()` and `led_off()` control the on and off states; `led_toggle()` toggles the current state. The naming of these four functions is completely self-explanatory—anyone seeing `led_on()` knows it means turning on the light, without needing to look at the internal implementation.

Overall, this set of wrappers has clear logic and a reasonable structure. If you only have one LED and the hardware will not change frequently, this approach is perfectly adequate. In many companies' embedded projects, this style of coding is standard practice, and no one sees any issue with it.

---

## The Main Program: Looks Clean

With `led.h` and `led.c` in place, our `main.c` becomes exceptionally clean:

```c
#include "led.h"
#include "stm32f1xx_hal.h"

extern void SystemClock_Config(void);

int main(void) {
    HAL_Init();
    SystemClock_Config();
    led_init();

    while (1) {
        led_on();
        HAL_Delay(500);
        led_off();
        HAL_Delay(500);
    }
}
```

You see, the `main` function is now very clean. Initialization follows three steps: HAL library initialization, clock configuration, and LED initialization. Then it enters the main loop: turn on the LED, wait 500 milliseconds, turn off the LED, wait 500 milliseconds. Anyone reading this code can understand what it does in a second—making the LED blink once per second.

Compared to the version in the previous part that directly called HAL APIs, the readability improvement here is obvious. You do not need to know what the GPIO port is, what the pin number is, or whether the LED is active-low or active-high—all hardware details are encapsulated by the macros in the header file and the functions in the implementation file. There are no bare hardware operations in `main.c`; it only interacts with clearly named interfaces.

This code is completely acceptable in most embedded projects. Frankly, if your project just controls one or two LEDs for status indication, stopping here is enough. There is no suspicion of over-engineering, the maintenance cost is low, and any engineer with embedded experience can understand it at a glance.

But here comes the question—what if we want to add another LED on PA0?

You might say, "Just write another `led2.h` and `led2.c`, right?" True, that is the standard approach. But let us see what this "standard approach" actually leads to.

---

## Problems Exposed: When Requirements Get Complex

### Scenario 1: The Absurd Theater of Adding a Second LED

Suppose the product manager suddenly says, "We need a red LED for power indication and a green LED for running status. The red one is on PC13, the green one is on PA0, and the green one is active-high."

Using the C macro approach, you need to add an almost identical set of files. First, `led2.h`:

```c
/* led2.h —— 第二个LED */
#define LED2_PORT       GPIOA
#define LED2_PIN        GPIO_PIN_0
#define LED2_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
#define LED2_ON_LEVEL   GPIO_PIN_SET    /* 这个LED是高电平有效 */

void led2_init(void);
void led2_on(void);
void led2_off(void);
void led2_toggle(void);
```

Then `led2.c`:

```c
/* led2.c */
#include "led2.h"

void led2_init(void) {
    LED2_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = LED2_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED2_PORT, &g);
}

void led2_on(void) {
    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, LED2_ON_LEVEL);
}

void led2_off(void) {
    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, LED2_OFF_LEVEL);
}

void led2_toggle(void) {
    HAL_GPIO_TogglePin(LED2_PORT, LED2_PIN);
}
```

The problem is already visible to the naked eye: we copied almost the entire content of `led.c` and changed a few macro names and values. What is the difference between `led2_init` and `led_init`? Different ports, different pins, but otherwise completely identical. What about the difference between `led2_on` and `led_on`? Only the macro names differ. If you have 10 LEDs, you need 10 nearly identical sets of code, totaling 40 functions, each a product of copy-pasting and changing a few letters.

This is not a theoretical concern—in real embedded projects, having three to five LEDs on a board for status indication is perfectly normal. Add buzzers, relays, and other GPIO-controlled peripherals, and you might end up writing dozens of such groups. Each group looks very similar, each has subtle differences, and each is prone to errors during copy-pasting.

This "copy-paste programming" has a famous acronym: WET (Write Everything Twice, or the more toxic version, We Enjoy Typing). It runs completely counter to one of the most fundamental principles in software engineering: DRY (Don't Repeat Yourself). Duplicate code is a breeding ground for bugs: you fix a bug in `led.c` but forget to fix it in `led2.c`, resulting in one LED working fine while the other has issues, making troubleshooting extremely painful.

### Scenario 2: The Phantom Bug of Mismatched Ports and Clocks

While the copy-paste problem above is annoying, it is at least a problem where "you know it has issues." The following scenario is truly insidious—the kind of bug where you have no idea you made a mistake.

Suppose that when writing `led2.h`, you habitually copy from `led.h` and modify it. You change the port to GPIOA, change the pin to GPIO_PIN_0, but—you forget to change the clock enable macro:

```c
/* 谁能保证LED2_PORT是GPIOA时，LED2_CLK_ENABLE调的是__HAL_RCC_GPIOA_CLK_ENABLE？ */
#define LED2_PORT       GPIOA
#define LED2_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()  /* 悄悄写错了！编译器不会报错！ */
```

Notice this: the port is GPIOA, but the clock being enabled is still for GPIOC. The compiler will not report an error—after macro expansion, `__HAL_RCC_GPIOC_CLK_ENABLE()` is a perfectly valid function call. Compilation passes, flashing succeeds, and the program runs. Then you find that LED2 just will not light up.

You start troubleshooting: the wiring is fine, you use a multimeter to measure PA0 and it is indeed low, and the GPIO initialization code looks correct. You might suspect a hardware issue, a broken LED, or a cold solder joint... Half an hour later, you finally remember to check the clock enable, only to find that GPIOA's clock was never turned on.

The terrifying thing about this kind of bug is that it is completely "logically correct but semantically wrong" code. The compiler does not understand your intent—it does not know that "LED2_PORT being GPIOA means the clock should enable GPIOA"—so it cannot give any warning. All you can rely on is your own carefulness and code reviews. But at three in the morning when rushing to meet a deadline, is your carefulness really reliable?

The deeper issue is that the correspondence between the port and the clock enable is maintained entirely by human memory. There is no compile-time check, no runtime validation, only the implicit convention that "you should know GPIOA corresponds to `__HAL_RCC_GPIOA_CLK_ENABLE()`." This "convention over constraint" design is fine in small projects, but in large-scale, multi-person collaborative projects, it is almost guaranteed to cause problems.

### Scenario 3: The Unintelligible Gibberish in the Debugger

When macros are nested multiple layers deep, debugging becomes a nightmare. You single-step to the line with `led_on()` in the debugger, wanting to see what actually happens at the lower level, but the debugger shows you the preprocessed, expanded code:

```c
led_on();
// 展开后：
HAL_GPIO_WritePin(
    ((GPIO_TypeDef *)0x40011000UL),  // LED_PORT -> GPIOC -> ((GPIO_TypeDef *)0x40011000UL)
    ((uint16_t)0x2000U),             // LED_PIN  -> GPIO_PIN_13 -> ((uint16_t)0x2000U)
    ((GPIO_PinState)0x00U)           // LED_ON_LEVEL -> GPIO_PIN_RESET -> ((GPIO_PinState)0x00U)
);
```

If there is a problem here—for example, if you wrote the wrong macro value—the debugger will not tell you "LED_PORT is defined incorrectly." It will only display a bunch of bare numeric constants. You have to mentally reverse the transformation yourself: which port does `0x40011000` correspond to? Which pin does `0x2000` correspond to? If your macro definitions are nested several layers deep (for example, LED_PORT references BOARD_LED_PORT, which in turn references the specific port), tracing the source of the problem is literally a nightmare.

Compiler error messages present the same dilemma. If there is a syntax error in your macro definition, the line number reported by the compiler might point to the expanded code rather than your source file. You will see a long, incomprehensible error message filled with expanded macro content, and you have to deduce the location in the original code yourself. The deeper the nesting, the more severe this problem becomes—you might see a long string of expanded code in the error message, with no idea which macro definition it came from.

---

## Root Causes: Five Ticking Time Bombs

Summarizing the scenarios above, the core problems of the C macro approach can actually be distilled into five aspects. I do not want to list them as a bullet list—that feels too much like a textbook, and these problems are inherently interconnected, making them worth discussing in connected paragraphs.

The first problem lies in type safety. `LED_PORT` is a macro that expands to `GPIOC`, and `GPIOC` in the HAL library is essentially a pointer constant pointing to a specific memory address. But macros have no type—they are purely text replacement. This means you could perfectly well write something like `#define LED_PORT 42`, and the compiler will happily pass it to `HAL_GPIO_Init()`, until runtime when the hardware accesses an illegal address and the program crashes with a HardFault. Nothing stops you from passing a random integer, a string, or any type of value to a function expecting a GPIO port pointer. The compiler will not check it for you, and the runtime will not report an error gracefully—the chip simply freezes right there, and you will not even see an error message. This "everything compiles" characteristic is a massive hidden danger in large projects.

The second problem is the hidden danger brought by manual clock management. There is no enforced associative relationship between the port macro and the clock enable macro. You define `LED_PORT` as `GPIOA`, but `LED_CLK_ENABLE()` can call any port's clock enable function. Correctness relies entirely on the programmer's memory and carefulness. If your project has over a dozen GPIO devices, each requiring a correctly matched port and clock, do you really think you can guarantee every single one is correct? This problem is also very hard to catch during code reviews—because the code has no syntactic errors; the error exists only at the semantic level, and semantics cannot be checked by a machine.

The third problem is the lack of code reuse. Every time you add a new GPIO device (whether it is an LED, a button, a relay, or anything else), you need to write an almost entirely identical set of initialization and operation functions. The only difference between these functions is a few macro values, but their structure, logic, and even most lines of code are exactly the same. This is typical "copy-paste programming" and the most direct violation of the DRY principle. When you discover a common bug in all LED initialization functions—for example, a certain field in `GPIO_InitTypeDef` is set incorrectly—you need to modify each copy one by one. Missing one means a new bug. This maintenance cost, which grows linearly with the number of devices, becomes a real burden as the project scales.

The fourth problem is the debugging difficulty of macros. This is not simply a matter of "not seeing macro names in the debugger." The deeper frustration is that macros are expanded during the preprocessing stage, meaning the compiler sees your original code no longer when it performs syntax analysis and type checking. When the compiler reports an error, it reports the location in the expanded code, and you need to reverse-engineer it back to the source file yourself. If macros reference other macros (which is very common in embedded projects), you might see several layers of nested expansion results in the error message, making tracing the problem source like peeling an onion, layer by layer. For complex macro definitions, sometimes you even need to manually expand them to understand what actually happened—it is as if you have to run a preprocessor in your head every time a bug occurs.

The fifth problem is the manual consistency maintenance caused by the lack of abstraction layers. For example, the "active-low" hardware characteristic requires simultaneously maintaining both the `LED_ON_LEVEL` and `LED_OFF_LEVEL` macros in the C macro approach. If you replace the LED with an active-high model, you need to modify both macros at the same time—change one to `GPIO_PIN_SET` and the other to `GPIO_PIN_RESET`. If you only change one, the LED's behavior will be completely inverted: calling `led_on()` actually turns the LED off, and calling `led_off()` actually turns it on. This design, which requires manually maintaining consistency between multiple definitions, is very fragile because there is no mechanism to guarantee consistency—only your memory and attention. Ideally, you would only need to declare "this LED is active-low," and the abstraction layer would automatically deduce what logic levels "on" and "off" correspond to.

These five problems are not independent—they share a common root cause: macros are text replacement, not language-level abstractions. They have no types, no scope, and no encapsulation. They are completely expanded during the preprocessing stage, leaving no trace. These characteristics are advantages in simple scenarios (flexible, zero overhead), but they become a burden in complex scenarios that require structured management.

---

## Calming Down: Are C Macros Really That Bad?

After discussing so many problems, I feel it is necessary to fairly evaluate the C macro approach.

The C macro approach works. In the vast majority of embedded projects, it is a widely used, practically validated standard practice. Many electronic products you use daily—routers, air conditioner controllers, automotive ECUs—likely use C macros to manage hardware configurations in their firmware. These products run stably year after year, and nobody causes a system crash due to C macro type safety issues.

The reason is simple: in projects characterized by "single maintainer, relatively fixed requirements," the drawbacks of C macros will not truly hurt you. You know your board only has two LEDs, you know which clock enable function corresponds to GPIOA, and you can spot mismatched ports and clocks during code review. This model of "relying on human knowledge and discipline to ensure correctness" is completely viable in small teams.

Moreover, C macros have some undeniable advantages: zero runtime overhead (macros are expanded at compile time), extreme flexibility (anything can be defined as a macro), and strong universality (supported by any C compiler). In resource-constrained embedded environments, zero overhead is a very important characteristic—you will not consume an extra byte of Flash or RAM by introducing an abstraction layer.

Therefore, if your project is not large in scale, the number of peripherals is limited, and the team personnel are stable, the C macro approach is perfectly adequate. There is no need to introduce more complex abstractions for the sake of "elegance." This is not laziness, but a pragmatic engineering decision.

But if your project is growing—more peripherals, more complex hardware configurations, more developers joining—those small problems will snowball. Each new LED does not bring just a few lines of additional code, but an entire set of macro definitions and function implementations that must be manually kept consistent. Each new person joining the team needs to understand the unwritten rule that "ports must match their clock enables." Every hardware revision requires synchronizing configuration changes across a dozen files. When you reach that stage, you will start to wonder: is there a way to retain C's performance (zero runtime overhead) while gaining type safety and code reuse?

---

## Leading to the Next Step: The Gradual Path from C to C++

The answer is C++ templates. But I do not want to pull out a bunch of template metaprogramming right from the start and scare people away—that would be both irresponsible and unnecessary. Starting from the next part, we will refactor this C code into a modern C++23 template design step by step, with each step being gradual and having a clear motivation.

In the first step, we will use `enum class` to replace macro definitions, taking the first step toward type safety. You will immediately see how a simple enum class prevents you from passing `42` to a function expecting a GPIO port—the compiler will directly report an error, rather than waiting until runtime to discover the LED is not lighting up.

In the second step, we will use template parameters to achieve compile-time port and pin binding. Template parameters have their values determined at compile time, and the compiler can automatically deduce which clock enable function should be called—you will never again be able to write the kind of bug where "the port is A but the clock enabled is C," because it will be caught at the compilation stage.

In the third step, we will abstract the LED's "active level" into a template parameter, letting it automatically deduce the GPIO states corresponding to on and off. You only need to declare "this LED is active-low," and the type system guarantees the correctness of the on/off mapping, completely eliminating the need to manually maintain the consistency of two macros.

None of these steps will appear out of thin air—each is designed to solve a specific problem we created with our own hands in this part. This is why I spent an entire part showcasing the "crime scene" of the C macro approach: only when you truly feel the pain points can you understand the value of each subsequent refactoring step.

---

## Wrapping Up

In this part, we fully demonstrated the C macro-style LED driver approach—it is concise, effective, and standard practice in most STM32 projects. Then, through three specific scenarios, we saw the problems exposed by the C macro approach when requirements become complex: lack of type safety, hidden clock matching dangers, inability to reuse code, and debugging difficulties.

This is not about dismissing C macros—it is a technical choice for a specific stage that works but is not elegant. Its problem is not that it "cannot be used," but that it "is prone to errors when scaling." Understanding these pain points gives us a clear target for our subsequent C++ refactoring.

In the next part, we take the first step of refactoring: replacing macro definitions with C++'s `enum class`, to see what kind of changes type safety can bring to embedded development.
