---
title: 'Part 27: `std::variant` Events + `std::visit` Dispatching — Type-Safe "What
  Happened'
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 9
---
# Part 27: `std::variant` Events + `std::visit` Dispatch — Type-Safe "What Happened"

> Following up on the previous article: `enum class` achieved type-safe configuration and state. This article introduces the C++17 `std::variant` to express button events—"Pressed" and "Released" are no longer two integers, but two distinct types.

---

## How C Expresses Events

A button has only two events: Pressed and Released. C typically uses an `enum` or `#define` to represent them:

```c
#define EVENT_PRESSED  1
#define EVENT_RELEASED 0

// 或者
enum ButtonEvent { Pressed = 1, Released = 0 };
```

We then pass this integer in a callback or as a return value:

```c
void handle_event(int event) {
    if (event == EVENT_PRESSED) {
        // 按下处理
    } else if (event == EVENT_RELEASED) {
        // 释放处理
    }
    // 如果传了一个 42 进来呢？编译器不会警告你
}
```

The problem is obvious: `int` can be any value. If we pass in `42`, the compiler stays completely silent. Even if we use `enum`, C's `enum` is fundamentally an integer, offering no type safety guarantees.

A deeper problem is that an event can only carry a single integer. If a `Pressed` event later needs to include a timestamp, and a `Released` event needs to include a duration, an integer is no longer sufficient. We would have to add an extra `struct` parameter or use a global variable to pass the additional data.

---

## std::variant: A Type-Safe Union

`std::variant` is a type-safe union introduced in C++17. It can hold one of several types at any given time—similar to C's `union`, but with key differences:

1. **Type safety**: `variant` knows which type it currently holds.
2. **Compile-time checking**: When accessing it, we must handle all possible types, otherwise the compiler issues a warning or error.
3. **Support for complex types**: Unlike `union`, which cannot hold classes with constructors, `variant` can hold any type.

### Our Event Definition

```cpp
// button_event.hpp
#pragma once
#include <cstdint>
#include <variant>

namespace device {

struct Pressed {};
struct Released {};

using ButtonEvent = std::variant<Pressed, Released>;

} // namespace device
```

`Pressed` and `Released` are empty structs—they carry no data and serve purely as type tags. `ButtonEvent` is a `std::variant` that can hold either a `Pressed` or a `Released` at any given time.

Why use empty structs instead of `enum class`? Two reasons:

**First, extensibility.** If `Pressed` later needs to carry a timestamp:

```cpp
struct Pressed { uint32_t timestamp; };
```

We simply add a field to the struct, and the way we use `variant` remains completely unchanged. If we had used `enum class`, carrying data would require an additional `struct` wrapper.

**Second, type dispatch.** `std::visit` can perform compile-time dispatch based on the actual type held in the `variant`—different types take different code paths. Using empty structs as type tags makes this dispatch mechanism very clean.

### Comparison with union

```cpp
// C 风格 union — 不安全
union ButtonEvent {
    int pressed;
    int released;
};
// 没有办法知道当前是 pressed 还是 released

// C++17 variant — 安全
using ButtonEvent = std::variant<Pressed, Released>;
// variant 内部记录了当前持有的类型
```

C's `union` does not record "which member is currently active," so we need to manually maintain a tag variable. If we set the tag to indicate `pressed` but actually read `released`, the result is undefined behavior. `variant` maintains this tag internally and forces us to correctly handle each type through `std::visit`.

---

## std::visit: Type-Safe Dispatch

`std::visit` accepts a "visitor" (a callable) and a `variant`, invoking the corresponding overload of the visitor based on the type currently held by the `variant`.

### Generic Lambda Approach

```cpp
std::visit(
    [](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, Pressed>) {
            // 处理按下
        } else if constexpr (std::is_same_v<T, Released>) {
            // 处理释放
        }
    },
    event
);
```

What does this code do? Let's break it down layer by layer:

1. `std::visit(visitor, event)` — Invokes the `visitor` based on the type held by the `event`.
2. `[](auto&& e)` — A generic lambda where `auto&&` is a forwarding reference, and the type of `e` is deduced from the actual type held in the `variant`.
3. `using T = std::decay_t<decltype(e)>` — Extracts the "bare type" of `e` (removing references and const).
4. `if constexpr (std::is_same_v<T, Pressed>)` — A compile-time check for whether `T` is `Pressed`.
5. `else if constexpr (std::is_same_v<T, Released>)` — A compile-time check for whether `T` is `Released`.

### Actual Usage in main.cpp

```cpp
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
```

Here we use two layers of lambdas. The outer lambda is the callback parameter for `poll_events()`, called each time an event occurs, with the parameter `event` being a `ButtonEvent` (i.e., a `std::variant<Pressed, Released>`). The inner lambda is the visitor for `std::visit`, responsible for handling the specific event types.

### std::decay_t and decltype

`decltype(e)` returns the declared type of `e`. Since `auto&&` is a forwarding reference, the actual type of `e` might be a reference type like `Pressed&&` or `const Pressed&`. `std::decay_t` strips away references, const, and volatile, yielding the "bare type" `Pressed` or `Released`.

```cpp
// 如果 variant 持有 Pressed：
decltype(e) → Pressed&& （或 const Pressed&，取决于调用方式）
std::decay_t<Pressed&&> → Pressed

// 所以 T 就是 Pressed
```

### The Role of if constexpr

`if constexpr` is a compile-time conditional branch. When `T` is `Pressed`, the code in the `else` branch **is not compiled**—it simply does not exist in the generated machine code. This differs from a runtime `if-else`: with a runtime `if-else`, both branches are compiled and one is selected at execution time; with `if constexpr`, only the matching branch is compiled.

This means if we write operations exclusive to `Released` (such as accessing a specific field of `Released`) inside the `else` branch, there will be no compilation error when `T` is `Pressed`—because that line of code does not exist at all.

---

## Comparison with Virtual Functions

We might ask: why not use virtual functions and inheritance to express polymorphic events?

```cpp
// 虚函数方案
struct ButtonEvent {
    virtual ~ButtonEvent() = default;
    virtual void handle() = 0;
};
struct Pressed : ButtonEvent { void handle() override { /* ... */ } };
```

In desktop applications, this is the classic approach. But in an embedded environment, it has several fatal flaws:

1. **Virtual function table (vtable)**: Every class with virtual functions has a vtable stored in Flash. Both `Pressed` and `Released` would each need a vtable.
2. **Dynamic allocation**: Polymorphism typically requires `new` or `std::make_unique`. We have disabled exceptions in our embedded environment, and we avoid heap allocation whenever possible.
3. **Runtime dispatch**: Virtual function calls perform an indirect jump via a vtable pointer, resulting in an extra memory access.

`std::variant` + `std::visit` suffers from none of these issues:

- No vtable is needed—type information is encoded in the `variant`'s own tag.
- No heap allocation is needed—`variant` stores values directly on the stack.
- Dispatch is completed at compile time—the compiler sees the `if constexpr` and directly generates the corresponding code.

In our `-fno-exceptions -fno-rtti` compilation environment, `std::variant` is a more suitable choice than virtual functions.

---

## Zero-Overhead Proof

The memory layout of `std::variant<Pressed, Released>`:

```mermaid
graph LR
    subgraph "std::variant&lt;Pressed, Released&gt; Memory Layout"
        TAG["tag (1B)<br/>0 = Pressed<br/>1 = Released"]
        PAYLOAD["payload<br/>(empty struct, no extra space)"]
    end
    TAG --- PAYLOAD
```

Since both `Pressed` and `Released` are empty structs (`sizeof = 1`), `variant` only needs a single tag byte to identify which type it currently holds. With alignment, `sizeof(ButtonEvent)` is typically 2 bytes.

`std::visit` combined with `if constexpr` generates code from the compiler equivalent to:

```c
if (event.tag == 0) {
    led_on();   // Pressed 分支
} else {
    led_off();  // Released 分支
}
```

One comparison, one jump. Exactly the same as hand-written C code using `if-else`. The variant's tag check is simply the conditional judgment of an `if-else`—the compiler optimizes it into the simplest machine code.

---

## Looking Back

This article introduced two C++17 features to build a type-safe event system:

- **`std::variant<Pressed, Released>`** — A type-safe union replacing C-style integer event codes.
- **`std::visit` + generic lambda** — Compile-time type dispatch ensuring all event types are handled.
- **Empty structs as type tags** — Extensible, allowing fields to be added later.
- **`std::decay_t<decltype(e)>` + `std::is_same_v`** — A tool combination for compile-time type checking.

Compared to the virtual function approach, `variant` + `visit` requires no vtable, no heap allocation, and no RTTI—perfectly suited for our embedded environment.

The next article assembles these pieces into a Button template class.
