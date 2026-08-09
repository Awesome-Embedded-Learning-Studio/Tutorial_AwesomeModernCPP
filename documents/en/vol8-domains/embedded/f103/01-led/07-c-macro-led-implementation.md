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
title: 'Part 12: LED Drivers in the Era of C Macros — Functional but Not Elegant'
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/07-c-macro-led-implementation.md
  source_hash: b1ca63e2878afb4ab894942d94d2f3a0cc8bec6851215c5d9f890f7e5da73a7d
  translated_at: '2026-06-16T04:10:04.043733+00:00'
  engine: anthropic
  token_count: 2567
---
# Part 12: LED Drivers in the Era of C Macros — It Works, But It's Not Elegant

> Written for all friends who think "C macro wrappers are good enough."
> In this post, we encapsulate an LED driver using traditional C macros, the standard approach in most STM32 tutorials. The code runs, and the logic is clear. But when we scrutinize its extensibility and safety, you will discover how many ticking time bombs lie behind those seemingly harmless macros.

---

## Preface: From "It Runs" to "It Runs Well"

In the previous post, we wrote a complete LED blinking program using pure HAL APIs. It undeniably runs—the little LED on the board blinks, proving that the toolchain, compilation process, and flashing workflow are all connected. That was a genuinely rewarding moment, considering the pitfalls encountered between setting up a cross-compilation environment from scratch and seeing the first line of code run on hardware.

But if you look back at that code, you will realize an uncomfortable fact: it is hard-bound to the PC13 pin. From the GPIO port selection and pin number assignment to the clock enable function call and level state setting, everything is a hardcoded literal. Want to move this LED to PA0? You have to find every instance of `GPIOC` in the code and change it to `GPIOA`, every instance of `GPIO_PIN_13` to `GPIO_PIN_0`, and remember to change the clock enable from `__HAL_RCC_GPIOC_CLK_ENABLE()` to `__HAL_RCC_GPIOA_CLK_ENABLE()`. Miss one spot? The LED won't light up, and you'll stare at the board, suspecting a hardware failure.

This is why most STM32 tutorials introduce C macros. By centralizing hardware parameters in header files via macro definitions, you only need to modify a few lines of `#define` when making changes, rather than searching for a needle in a haystack across the entire source file. This is a pragmatic choice and is perfectly sufficient for many practical projects—I don't intend to dismiss C macros here, as they do solve a set of problems.

However, this post also serves as the starting point for our subsequent C++ refactoring. I need to fully unfold the C macro approach first, showing you where it excels and where it falls short. This way, when we use C++ templates to solve these problems one by one later, you can understand the motivation behind every refactoring step. This is not refactoring for the sake of showing off, but driven by genuine needs.

---

## Encapsulating LED Drivers with C Macros: The Classic Approach

Let's start with the most standard C macro style LED driver. You can find this approach in any STM32 tutorial; its core concept is simple: centralize all hardware-related parameters in macro definitions in a header file, then provide a set of semantically clear functions to operate the LED.

First, the header file `BspLed.h`:

```c
#ifndef BSP_LED_H
#define BSP_LED_H

#include "stm32f1xx_hal.h"

// Hardware configuration macros
#define LED_PORT        GPIOC
#define LED_PIN         GPIO_PIN_13
#define LED_CLK_ENABLE  __HAL_RCC_GPIOC_CLK_ENABLE

// Active level definitions (Low active)
#define LED_ON_LEVEL    GPIO_PIN_RESET
#define LED_OFF_LEVEL   GPIO_PIN_SET

// API functions
void LED_Init(void);
void LED_On(void);
void LED_Off(void);
void LED_Toggle(void);

#endif // BSP_LED_H
```

Then comes the corresponding implementation file `BspLed.c`:

```c
#include "BspLed.h"

void LED_Init(void) {
    LED_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);

    // Default to off state
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);
}

void LED_On(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_ON_LEVEL);
}

void LED_Off(void) {
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, LED_OFF_LEVEL);
}

void LED_Toggle(void) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
}
```

Let's break down the design intent of this code section by section.

First is `LED_PORT`, which defines the GPIO port connected to the LED as a macro. This is much more flexible than hardcoding `GPIOC` directly in the code—if the hardware is revised and the LED moves from PC13 to PB5, you only need to change `LED_PORT` in the header file from `GPIOC` to `GPIOB`, and every place referencing `LED_PORT` will update automatically. This is the most basic and effective use of C macros: centralized management of configuration constants.

Next is `LED_PIN`, which extracts the pin number. The same logic applies: changing the pin requires touching only this line.

Clock enabling is a detail often overlooked. STM32 peripherals have their clocks turned off by default after power-up; you need to manually enable the corresponding port's clock to use GPIO functions. `LED_CLK_ENABLE` encapsulates clock enabling as a macro. In the `LED_Init` function, we simply call `LED_CLK_ENABLE()` to turn on the clock; the caller doesn't need to know which port's clock is at the bottom.

Then comes the level definition. The LED on the Blue Pill board is active-low—meaning pulling PC13 low (`GPIO_PIN_RESET`) turns it on, and pulling it high (`GPIO_PIN_SET`) turns it off. This hardware detail is encapsulated in the `LED_ON_LEVEL` and `LED_OFF_LEVEL` macros. Why do this? Because if you write `GPIO_PIN_RESET` directly in the `LED_On` function, three months later when you review this code, you will wonder, "Why is RESET turning the light on?" Encapsulating hardware characteristics into clearly named macros significantly improves code readability.

Finally, there are four functions. `LED_Init` handles initialization, including clock activation and GPIO configuration; `LED_On` and `LED_Off` control the state; `LED_Toggle` flips the current state. The naming of these four functions is completely self-explanatory; anyone seeing `LED_On()` knows it means turning on the light, without needing to look at the internal implementation.

Overall, this encapsulation has clear logic and a reasonable structure. If you only have one LED and the hardware doesn't change frequently, this solution is perfectly sufficient. In many embedded projects in companies, this style of writing is standard practice, and no one sees any issue with it.

---

## Main Program: Looks Very Clean

With `BspLed.h` and `BspLed.c`, our `main.c` becomes exceptionally concise:

```c
#include "stm32f1xx_hal.h"
#include "BspLed.h"

int main(void) {
    // 1. HAL Library Initialization
    HAL_Init();

    // 2. System Clock Configuration
    SystemClock_Config();

    // 3. LED Initialization
    LED_Init();

    while (1) {
        LED_On();
        HAL_Delay(500);
        LED_Off();
        HAL_Delay(500);
    }
}
```

You see, the `main` function is now very clean. Initialization in three steps: HAL library init, system clock config, LED init. Then enter the main loop: light on, wait 500ms, light off, wait 500ms. Anyone reading this code can understand what it's doing in a second—making the LED blink once per second.

Compared to the version in the previous post that directly called HAL APIs, the readability improvement of this version is obvious. You don't need to know what a GPIO port is, what the pin number is, or whether it's active-low or active-high—all hardware details are encapsulated by macros in the header file and functions in the implementation file. There are no exposed hardware operations in `main.c`; it only deals with semantically clear interfaces.

This code is completely acceptable in most embedded projects. To be honest, if your project just involves controlling one or two LEDs for status indication, this step is enough. There's no suspicion of over-design, maintenance costs are low, and any engineer with embedded experience can take over and understand it instantly.

But the question arises—what if we want to add another LED on PA0?

You might say, "Just write another `BspLed2.h` and `BspLed2.c`, right?" That's correct, that is the standard approach. But let's see what this "standard approach" actually brings.

---

## Problems Exposed: When Requirements Get Complex

### Scenario 1: The Absurd Theater of Adding a Second LED

Suppose the product manager suddenly says, "We need a red LED for power indication and a green LED for running status. Red is on PC13, green is on PA0, and the green one is active-high."

Using the C macro approach, you need to add a set of almost identical files. First, `BspLed2.h`:

```c
#ifndef BSP_LED2_H
#define BSP_LED2_H

#include "stm32f1xx_hal.h"

#define LED2_PORT        GPIOA
#define LED2_PIN         GPIO_PIN_0
#define LED2_CLK_ENABLE  __HAL_RCC_GPIOA_CLK_ENABLE

// Active high
#define LED2_ON_LEVEL    GPIO_PIN_SET
#define LED2_OFF_LEVEL   GPIO_PIN_RESET

void LED2_Init(void);
void LED2_On(void);
void LED2_Off(void);
void LED2_Toggle(void);

#endif
```

Then `BspLed2.c`:

```c
#include "BspLed2.h"

void LED2_Init(void) {
    LED2_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED2_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED2_PORT, &GPIO_InitStruct);

    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, LED2_OFF_LEVEL);
}

void LED2_On(void) {
    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, LED2_ON_LEVEL);
}

void LED2_Off(void) {
    HAL_GPIO_WritePin(LED2_PORT, LED2_PIN, LED2_OFF_LEVEL);
}

void LED2_Toggle(void) {
    HAL_GPIO_TogglePin(LED2_PORT, LED2_PIN);
}
```

The problem is already visible: we almost duplicated the entire content of `BspLed.c` and changed a few macro names and values. What's the difference between `LED_Init` and `LED2_Init`? Different ports, different pins, otherwise identical. What about `LED_On` and `LED2_On`? Only the macro names differ. If you have 10 LEDs, you need 10 groups of almost identical code, totaling 40 functions, each a product of copy-pasting and changing a few letters.

This isn't a theoretical worry—in real embedded projects, it's perfectly normal to have three to five LEDs on a board for status indication. Add peripherals like buzzers and relays that are also controlled via GPIO, and you might end up writing dozens of such groups. Each group looks similar, each has subtle differences, and each can be error-prone during copy-pasting.

This "copy-paste programming" has a famous acronym: WET (Write Everything Twice, or the more vicious interpretation: We Enjoy Typing). It runs completely counter to one of the most basic principles of software engineering: DRY (Don't Repeat Yourself). Duplicate code is a breeding ground for bugs: you fix a bug in `LED_Init`, but forget to fix it in `LED2_Init`, resulting in one LED working normally and the other having issues. Troubleshooting this is very painful.

### Scenario 2: The Ghost Bug of Mismatched Ports and Clocks

While the copy-paste issue above is annoying, it is at least a "problem you know you have." The following scenario is truly insidious—the kind of bug where you have no idea you made a mistake.

Suppose when writing `BspLed2.c`, you habitually copy it from `BspLed.c` and modify. You changed the port to `GPIOA`, the pin to `GPIO_PIN_0`, but—you forgot to change the clock enable macro:

```c
// ... inside LED2_Init ...
LED2_CLK_ENABLE();  // Expands to __HAL_RCC_GPIOC_CLK_ENABLE();
// ...
```

Look closely: the port is `GPIOA`, but the clock being enabled is still for `GPIOC`. The compiler won't complain—after macro expansion, `__HAL_RCC_GPIOC_CLK_ENABLE()` is a completely legal function call. Compilation passes, flashing succeeds, the program runs. Then you find that LED2 just won't light up.

You start troubleshooting: wiring is fine, checking PA0 with a multimeter shows it is indeed low, the GPIO initialization code looks correct. You suspect a hardware problem, a broken LED, a bad solder joint... Half an hour later, you finally remember to check the clock enable, only to find that the GPIOA clock was never turned on.

The horror of this bug is that it is "logically correct but semantically wrong" code. The compiler doesn't understand your intent—it doesn't know that "LED2_PORT being GPIOA implies the clock should enable GPIOA"—so it can't give any warning. You can only rely on your own carefulness and code review. But at 3 AM rushing a deadline, is your carefulness really reliable?

The deeper problem is that the correspondence between the port and clock enable is maintained entirely by human memory. No compile-time checks, no runtime validation, only the implicit convention that "you should know GPIOA corresponds to `__HAL_RCC_GPIOA_CLK_ENABLE()`". This "convention over constraint" design is okay in small projects, but almost destined to fail in large collaborative projects.

### Scenario 3: The Unreadable Book in the Debugger

When macros are nested multiple layers, debugging becomes a nightmare. You single-step to the `LED_On()` line in the debugger, wanting to see what happens at the bottom, but the debugger shows you the preprocessed expanded code:

```text
HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
```

If there's a problem here—say you wrote the wrong macro value—the debugger won't tell you "LED_PORT is defined incorrectly"; it will just show a bunch of bare numeric constants. You have to do the reverse transformation in your head: `GPIOC` corresponds to which port? `GPIO_PIN_13` corresponds to which pin? If your macro definitions are nested several layers (e.g., `LED_PORT` references `BOARD_LED_PORT`, which references the specific port), tracing the source of the problem is simply a nightmare.

Compiler error messages present the same dilemma. If your macro definition has a syntax error, the line number reported by the compiler might point to the expanded code rather than your source file. You will see a long, incomprehensible error message filled with expanded macro content, requiring you to deduce the original code location yourself. The deeper the nesting, the more severe the problem—you might see a long string of expanded code in the error message, having no idea which macro definition it came from.

---

## Root of the Problem: Five Ticking Time Bombs

Summarizing the scenarios above, the core problems of the C macro approach can be summarized into five aspects. I don't want to list them like a textbook—these problems are interrelated, and it's worth discussing them in paragraphs.

The first problem lies with type safety. `LED_PORT` is a macro that expands to `GPIOC`. In the HAL library, `GPIOC` is essentially a pointer constant pointing to a specific memory address. But macros have no type—they are just text replacement. This means you can write something like `LED_Init(123, "abc")` (if the function signature allowed it), and the compiler would happily pass it to the function until the hardware accesses an illegal address at runtime and the program crashes with a HardFault. Nothing stops you from passing a random integer, a string, or any type of value to a function expecting a GPIO port pointer. The compiler won't check for you, and runtime won't fail gracefully—the chip just freezes, and you see no error message. This "anything compiles" characteristic is a huge hazard in large projects.

The second problem is the danger of manual clock management. There is no mandatory association between the port macro and the clock enable macro. You defined `LED_PORT` as `GPIOA`, but `LED_CLK_ENABLE` can call any port's clock enable function. Correctness relies entirely on the programmer's memory and carefulness. If your project has a dozen GPIO devices, each requiring the correct port and clock match, can you guarantee every single one is right? This problem is also hard to spot in code reviews—because there are no syntax-level errors, only semantic-level errors, which machines cannot check.

The third problem is the lack of code reuse. Every time you add a new GPIO device (whether an LED, button, relay, or something else), you need to write a full set of almost identical initialization and operation functions. The difference between these functions is only a few macro values, but their structure, logic, and even most code lines are identical. This is typical "copy-paste programming" and the most direct violation of the DRY principle. When you find a common bug in all LED init functions—say a field in `GPIO_InitTypeDef` is set wrong—you need to modify every copy individually; missing one means a new bug. This maintenance cost, which grows linearly with the number of devices, becomes a real burden as the project scales.

The fourth problem is the difficulty of debugging macros. This is not just about "not seeing macro names in the debugger." The deeper trouble is that macros are expanded in the preprocessing stage, meaning the compiler sees your original code no longer during syntax analysis and type checking. When the compiler reports an error, it reports the position in the expanded code, and you need to deduce back to the source file yourself. If macros reference other macros (common in embedded projects), you might see several layers of nested expansion results in the error message. Tracing the source is like peeling an onion layer by layer. For complex macro definitions, sometimes you even need to expand them manually to understand what happened—it's like running the preprocessor in your head every time a bug appears.

The fifth problem is the manual consistency maintenance brought by the lack of abstraction layers. Take "active-low" as a hardware characteristic example. In the C macro approach, you need to maintain two macros simultaneously: `LED_ON_LEVEL` and `LED_OFF_LEVEL`. If you swap the LED for an active-high type, you need to modify both macros—one to `GPIO_PIN_SET`, the other to `GPIO_PIN_RESET`. If you only change one, the LED behavior is completely reversed: calling `LED_On()` actually turns it off, calling `LED_Off()` turns it on. This design of "manually maintaining consistency between multiple definitions" is very fragile because no mechanism guarantees consistency—only your memory and attention. Ideally, you should only declare "this LED is active-low," and the mapping of on/off to levels should be automatically derived by the abstraction layer.

These five problems are not independent—they share a common root: macros are text replacement, not language-level abstraction. They have no types, no scope, no encapsulation, and are completely expanded in the preprocessing stage, leaving no trace. These characteristics are advantages in simple scenarios (flexible, zero overhead), but become burdens in complex scenarios requiring structured management.

---

## Calming Down: Are C Macros Really That Bad?

Having discussed so many problems, I feel it's necessary to give a fair assessment of the C macro approach.

The C macro approach works. In the vast majority of embedded projects, it is a widely used and practice-verified standard approach. Many electronic products you use daily—routers, air conditioner controllers, automotive ECUs—likely have firmware using C macros to manage hardware configuration. These products run stably year after year, and no one causes a system crash due to C macro type safety issues.

The reason is simple: in "single maintainer, relatively fixed requirements" projects, the downsides of C macros don't really hurt you. You know your board only has two LEDs, you know which clock enable function corresponds to GPIOA, and you can see port-clock mismatches during code review. This model of "relying on human knowledge and discipline for correctness" is entirely feasible in small teams.

Moreover, C macros have undeniable advantages: zero runtime overhead (macros are expanded at compile time), extreme flexibility (anything can be a macro), and strong universality (any C compiler supports them). In resource-constrained embedded environments, zero overhead is a very important feature—you won't consume an extra byte of Flash or RAM by introducing an abstraction layer.

So, if your project scale isn't large, peripherals are limited, and the team is stable, the C macro approach is perfectly sufficient. There is no need to introduce more complex abstractions for the sake of "elegance." This isn't laziness, but a pragmatic engineering decision.

But if your project is growing—more peripherals, more complex hardware configurations, more developers involved—those small issues will snowball. Every new LED brings not just a few lines of code, but a full set of macro definitions and function implementations that need manual consistency. Every new team member needs to understand the unwritten rule "ports must match clock enables." Every hardware revision requires synchronizing configuration changes across a dozen files. At that stage, you will start to wonder: is there a way to maintain C's performance (zero runtime overhead) while gaining type safety and code reuse?

---

## Leading to the Next Step: The Progressive Path from C to C++

The answer is C++ templates. But I don't want to scare you off by pulling out a bunch of template metaprogramming right away—that would be irresponsible and unnecessary. Starting from the next post, we will refactor this C code into modern C++23 template design step by step, with every step being progressive and clearly motivated.

First, we will use `enum class` to replace macro definitions, taking the first step toward type safety. You will immediately see how a simple enum class prevents you from passing `GPIOA` to a function expecting a GPIO port—the compiler will error out directly, rather than waiting for runtime to discover the LED won't light.

Second, we will use template parameters to implement compile-time port and pin binding. Template parameters are fixed at compile time, and the compiler can automatically derive which clock enable function to call—you will never again write a bug where "port is A but clock is C," because it will be detected at the compilation stage.

Third, we will abstract the LED's "active level" into a template parameter, allowing it to automatically derive the GPIO state corresponding to on and off. You only need to declare "this LED is active-low," and the type system guarantees the correctness of the on/off mapping, completely eliminating the need to manually maintain the consistency of two macros.

None of these steps appear out of thin air—each is designed to solve a specific problem we created with our own hands in this post. That is why I spent the entire post demonstrating the "crime scene" of the C macro approach: only when you truly feel the pain can you understand the value of every subsequent refactoring step.

---

## Conclusion

In this post, we fully demonstrated the C macro style LED driver—it is concise, effective, and the standard practice in most STM32 projects. Then, through three specific scenarios, we saw the problems exposed by the C macro approach when requirements become complex: type unsafety, clock matching hazards, inability to reuse code, and debugging difficulties.

This is not dismissing C macros—it is a technical choice for a specific stage that works but isn't elegant. Its problem isn't that it "can't be used," but that it's "error-prone when extending." Understanding these pain points gives us a clear target for our subsequent C++ refactoring.

In the next post, we take the first step of refactoring: using C++'s `enum class` to replace macro definitions, seeing what changes type safety can bring to embedded development.
