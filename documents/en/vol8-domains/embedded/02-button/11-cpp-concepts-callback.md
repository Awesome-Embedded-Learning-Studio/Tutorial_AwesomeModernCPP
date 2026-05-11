---
title: 'Part 29: Concepts Constrained Callbacks + Complete Code Walkthrough'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 11
---
# Part 29: Constraining Callbacks with Concepts + Full Code Walkthrough

> Continuing from the previous article: we have set up the skeleton for the `Button` template class. In this article, we tackle the final C++ feature—using concepts to constrain the callback parameter types—and then do a complete walkthrough of the entire ``main.cpp`` call chain from start to finish.

---

## The Callback Type Problem

``poll_events()`` accepts a callback function as a parameter, invoking it whenever a confirmed button state change occurs. The problem is that the C++ template parameter ``Callback`` can be any type—a function pointer, a lambda, a function object, or even an integer (if you make a coding mistake).

Without concepts, what happens if we pass a callback with the wrong signature?

```cpp
// 错误的回调：接受 int 而不是 ButtonEvent
button.poll_events([](int x) { /* ... */ }, HAL_GetTick());
```

The compiler attempts to instantiate the ``poll_events()`` code, discovers that ``int`` cannot be constructed from ``Pressed`` when calling ``cb(Pressed{})``, and then reports an error. But the error message might look like this:

```text
error: no match for call to '(lambda) (Pressed)'
note: candidate expects 1 argument of type 'int', got 'Pressed'
  in instantiation of 'void Button::poll_events(Callback&&, uint32_t, uint32_t)
    [with Callback = main()::<lambda(int)>; ...]'
```

A few lines of template instantiation stack trace paired with obscure type information. While this is much better than the SFINAE (Substitution Failure Is Not An Error) errors of C++98, it still isn't intuitive enough.

---

## Concepts: One-Line Constraint, Clear Errors

```cpp
template <typename Callback>
    requires std::invocable<Callback, ButtonEvent>
void poll_events(Callback&& cb, uint32_t now_ms, uint32_t debounce_ms = 20) {
```

``requires std::invocable<Callback, ButtonEvent>`` is a concepts constraint. It tells the compiler: an object of type ``Callback`` must be callable with a ``ButtonEvent`` argument.

If we pass a callback with the wrong signature:

```cpp
button.poll_events([](int x) { /* ... */ }, HAL_GetTick());
```

The compiler reports the error **before template instantiation**:

```text
error: constraint 'std::invocable<lambda, ButtonEvent>' not satisfied
note: the expression 'std::invocable<lambda, ButtonEvent>' evaluated to 'false'
```

One sentence explains it all: your callback does not satisfy the ``std::invocable<Callback, ButtonEvent>`` constraint. There is no need to dig through template instantiation stacks—a constraint failure directly tells you where the problem lies.

### What Does std::invocable Mean?

``std::invocable<F, Args...>`` is a concept defined in the C++20 ``<concepts>`` header. It checks whether ``f(args...)`` is a valid call expression, given an object ``f`` of type ``F``.

For ``std::invocable<Callback, ButtonEvent>``:

- ``Callback`` is the lambda or function object you passed in
- ``ButtonEvent`` is ``std::variant<Pressed, Released>``
- The constraint requires: ``cb(ButtonEvent{})`` must be a valid call

Examples of valid callbacks:

```cpp
// Lambda 接受 ButtonEvent
button.poll_events([](device::ButtonEvent e) { /* ... */ }, HAL_GetTick());

// Lambda 接受 auto（泛型 lambda）
button.poll_events([](auto&& e) { /* ... */ }, HAL_GetTick());

// Lambda 接受 Pressed（variant 的一个选项）— 这不行！
// std::invocable<Callback, ButtonEvent> 检查的是用 ButtonEvent 调用，不是 Pressed
button.poll_events([](device::Pressed e) { /* ... */ }, HAL_GetTick());  // 编译错误
```

### Concepts vs. SFINAE

Before concepts, constraining template parameters relied on SFINAE (Substitution Failure Is Not An Error):

```cpp
// SFINAE 方式 — 丑陋且难以理解
template <typename Callback,
          typename = std::enable_if_t<std::is_invocable_v<Callback, ButtonEvent>>>
void poll_events(Callback&& cb, uint32_t now_ms, uint32_t debounce_ms = 20);
```

The principle behind SFINAE is that if the condition in ``std::enable_if_t`` is false, the template is silently removed from the candidate list, and the compiler looks for other matching overloads. Only if no match is found at all does it report a "no matching function" error—and this error is usually accompanied by dozens of lines of template instantiation stack traces.

Concepts elevate constraints to first-class citizens of the language: the ``requires`` clause directly declares the constraint, the compiler directly checks it, and a constraint failure directly reports the constraint's name. There is no need to understand how SFINAE works under the hood.

---

## Is Callback&& an Rvalue Reference?

```cpp
void poll_events(Callback&& cb, ...)
```

``Callback&&`` looks like an rvalue reference, but it is actually a **forwarding reference**. When ``Callback`` is a template parameter, the meaning of ``Callback&&`` depends on the argument passed in:

- If an lvalue is passed (such as a named lambda variable): ``Callback`` is deduced as ``Lambda&``, and ``Callback&&`` becomes ``Lambda& &&``, which collapses to ``Lambda&`` (an lvalue reference).
- If an rvalue is passed (such as a temporary lambda): ``Callback`` is deduced as ``Lambda``, and ``Callback&&`` is simply ``Lambda&&`` (an rvalue reference).

Therefore, ``Callback&&`` can accept anything—lvalues, rvalues, const, and non-const. This is exactly what we want: users can pass a temporary lambda or a named function object.

Why not use ``const Callback&``? Because a ``const`` reference cannot invoke a non-const ``operator()``. Although our lambda does not modify captured variables, maintaining generality is safer.

In this scenario, we did not use ``std::forward<Callback>(cb)``—because the callback is only invoked once inside ``poll_events()``, so perfect forwarding is unnecessary. If ``cb`` is an lvalue, we just call it directly; if it is an rvalue, we also just call it directly. The role of the forwarding reference here is simply to "accept any callable object type," rather than to "perfectly forward" it.

---

## Full Code Walkthrough

Now let's walk through the execution flow of ``main.cpp`` from start to finish, and see what each line of code is doing.

```cpp
#include "device/button.hpp"
#include "device/button_event.hpp"
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}
```

Header file inclusions. ``button.hpp`` indirectly includes ``gpio.hpp``. ``extern "C"`` wraps the HAL header files to ensure the C++ compiler uses C linkage rules when looking up HAL functions (as covered in Part 12 of the LED tutorial).

```cpp
int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
```

System initialization. Exactly the same as the LED tutorial: initialize the HAL library, and configure the system clock to 64MHz.

```cpp
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;
```

Object construction. These two lines each do three things:

**LED Construction:**

1. ``GPIOClock::enable_target_clock()`` — ``if constexpr`` enables the GPIOC clock
2. ``setup(Mode::OutputPP, NoPull, Low)`` — configures PC13 as push-pull output
3. The ``led`` object is ready, providing the ``on()``, ``off()``, and ``toggle()`` interfaces

**Button Construction:**

1. ``GPIOClock::enable_target_clock()`` — ``if constexpr`` enables the GPIOA clock
2. ``setup(Mode::Input, PullUp, Low)`` — configures PA0 as input with pull-up resistor
3. ``static_assert`` validates the pin number — passes at compile time
4. The ``button`` object is ready, with the state machine's initial state set to ``BootSync``

```cpp
    while (1) {
        button.poll_events(
            [&](device::ButtonEvent event) {
                std::visit(
                    [&](auto&& e) {
                        using T = std::decay_t<decltype(e)>;
                        if constexpr (std::is_same_v<T, device::Pressed>) {
                            led.on();
                        } else {
                            led.off();
                        }
                    },
                    event);
            },
            HAL_GetTick());
    }
```

Main loop. Each iteration does one thing: calls ``button.poll_events()``.

**``HAL_GetTick()``** gets the current timestamp (in milliseconds) and passes it to the state machine for time-based evaluation.

**The callback lambda** ``[&](device::ButtonEvent event)`` captures ``led`` by reference. When the state machine confirms a state change, it invokes this lambda, where the parameter ``event`` is ``std::variant<Pressed, Released>``.

**``std::visit``** dispatches based on the type held by ``event``:

- If it is ``Pressed``: calls ``led.on()``
- If it is ``Released`` (the ``else`` branch): calls ``led.off()``

**The Complete Call Chain:**

```text
main() 循环
  → poll_events(lambda, HAL_GetTick())
    → is_pressed() → read_pin_state() → HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)
    → switch(state_) 状态机判断
    → 确认变化时: cb(Pressed{}) 或 cb(Released{})
      → lambda 被调用，event = ButtonEvent
      → std::visit(lambda2, event)
        → if constexpr: led.on() 或 led.off()
          → HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, ...)
```

From the moment the user presses the button to the LED lighting up, the sequence is: physical level change → IDR register update → ``HAL_GPIO_ReadPin()`` read → state machine debounce confirmation → ``Pressed`` event triggered → ``std::visit`` dispatch → ``led.on()`` → ``HAL_GPIO_WritePin()`` → ODR register update → LED turns on.

The entire process involves no virtual functions, no heap allocation, and no exception handling. Every layer is a compile-time resolved inline call.

---

## Looking Back

This article completes the final piece of our C++ refactoring puzzle:

- **Concepts** (``requires std::invocable<Callback, ButtonEvent>``) constrain the callback signature, providing clear compilation errors
- **Forwarding references** ``Callback&&`` accept any callable object
- **Full code walkthrough** of the entire call chain from ``main()`` to ``HAL_GPIO_WritePin()``

So far, we have fully refactored the button control code using C++. The next article serves as the conclusion to this series—covering EXTI (External Interrupt) driven buttons, along with a summary of common pitfalls and practice exercises.
