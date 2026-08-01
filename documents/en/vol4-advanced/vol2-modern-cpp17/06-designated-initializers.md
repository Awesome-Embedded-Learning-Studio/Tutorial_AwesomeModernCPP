---
chapter: 12
cpp_standard:
- 20
description: 'C++20 designated initializers use .field = value to initialize aggregate members by name, must follow declaration order (unlike C99), and default the rest. They only work on aggregates, and C++20 does not support the [index] syntax for arrays.'
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'CTAD: Class Template Argument Deduction'
reading_time_minutes: 12
related:
- 'if constexpr: Compile-Time Branching'
- 'CTAD: Class Template Argument Deduction'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'Designated Initializers'
---
# Designated Initializers

Once a config struct grows past a few fields, positional initialization gets scary. A UART config with seven or eight fields, filled in by position:

```cpp
UartConfig cfg = {115200, 8, 0, 1, 0, 1, 1};   // who can read this
```

We've all hit the problems with this style: with many fields you end up counting positions against the declaration, inserting a new member in the middle forces every initializer to shift, and the compiler won't catch a mistake — it just shows up as weird behavior at runtime. C99 had an answer, the designated initializer, and C++20 brought it into the standard so we can write `.field = value` and initialize by name. But the C++20 version differs from C99 in two important places, and those two places are exactly where the traps are. We'll get to them as we go.

## Specify with `.field = value`, and follow declaration order

The basic shape is `.field = value`, one per line inside the braces:

```cpp
struct UartConfig {
    std::uint32_t baudrate = 0;
    std::uint8_t data_bits = 8;
    std::uint8_t parity = 0;      // 0=None 1=Odd 2=Even
    std::uint8_t stop_bits = 1;
};

UartConfig cfg{
    .baudrate = 115200,
    .data_bits = 8,
    .parity = 0,
    .stop_bits = 1,
};
```

Each value is labeled, no more counting on position — that's the main win. But C++20 adds a hard rule that C99 does not have: **the designators must appear in the same order as the member declarations**. If you move `stop_bits` ahead of `baudrate` for convenience:

```cpp
UartConfig bad{.stop_bits = 1, .baudrate = 115200};   // won't compile in C++20
```

GCC refuses outright:

```text
error: designator order for field 'UartConfig::baudrate' does not match declaration order in 'UartConfig'
```

This reordering is legal in C99; C++20 dropped it. The reason ties back to how brace initialization walks members in declaration order — a designator is "a name tag on the current position," not "a pass to jump around." So you may leave later members out, but you cannot list them out of order.

<OnlineCompilerDemo allow-run
  title="Basic syntax: name fields, follow declaration order, partial init uses defaults"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_basics.cpp"
  description="Specify fields in declaration order; unlisted members fall back to default member initializers; add -DOUT_OF_ORDER to reproduce the out-of-order compile error."
/>

Run it:

```text
cfg: baud=115200 bits=8 parity=0 stop=1
partial: baud=921600 data_bits=8(默认8) parity=2 stop=1(默认1)
```

## Aggregates only

Designated initializers only work on **aggregate types**. Roughly, a class is an aggregate when it has: no user-declared constructors, no private or protected non-static data members, no virtual functions, no virtual base classes. Ordinary structs and qualifying classes are aggregates.

The moment you add a constructor, it stops being an aggregate, and designated initializers stop working:

```cpp
struct WithCtor {
    int a;
    int b;
    WithCtor(int, int) {}
};

WithCtor x{.a = 1, .b = 2};   // won't compile: not an aggregate
```

The error says so plainly:

```text
error: designated initializers cannot be used with a non-aggregate type 'WithCtor'
```

This draws a clean line: classes with constructors own their initialization logic (validation, defaults, throws); aggregates have no constructor, so initialization is done with braces, and designated initializers just make the braces clearer. Want both? Give the struct a static factory, and use designated initialization inside it:

```cpp
struct Config {
    int baudrate;
    int data_bits;
    static Config standard() { return {.baudrate = 115200, .data_bits = 8}; }
};
```

## What about the members you didn't list

Under partial initialization, the members you don't name follow two rules: if they have a default member initializer (`int data_bits = 8;`), that default applies; otherwise they're value-initialized, which for built-in types means zero. In the `partial` line above, `{.baudrate = 921600, .parity = 2}` leaves `data_bits` and `stop_bits` unwritten, so they fall back to the defaults `8` and `1`.

::: warning Watch the implicit zero, and don't be spooked by -Wmissing-field-initializers
Without a default member initializer, an omitted member is zero-initialized. For a `bool auto_reload`, zero-initializing to `false` may not be what you meant — write the important ones explicitly.

Also, partial initialization trips GCC's `-Wmissing-field-initializers` warning (part of `-Wextra`), nudging you about members you didn't list. It's a **warning, not an error**, and for built-in types an omission is usually safe (zero), so treat it as a "did I mean to skip this?" nudge rather than a sign of trouble.
:::

## Nested structs, bit-fields, and unions all work

Designated initializers handle the common aggregate variants. Nested structs go level by level:

```cpp
struct Pin { std::uint8_t port; std::uint8_t pin; };
struct UartCfg { std::uint32_t baud; Pin tx; Pin rx; };

UartCfg u{
    .baud = 115200,
    .tx = {.port = 0, .pin = 9},
    .rx = {.port = 0, .pin = 10},
};
```

Bit-fields work too, and so do unions (initialize one member only):

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };
Flags f{.a = 1, .b = 0, .c = 5};

union Value { int i; float f; };
Value v{.f = 3.14f};
```

<OnlineCompilerDemo allow-run
  title="Aggregate variants: nested structs, bit-fields, unions"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_aggregate.cpp"
  description="Nested structs specified layer by layer, bit-fields set by name, a union with one member named. Add -DNON_AGGREGATE to reproduce the non-aggregate error."
/>

Run it:

```text
uart: baud=115200 tx=PA9 rx=PA10
flags: a=1 b=0 c=5
union as float: 3.14
```

## Don't bother with `[index]` for arrays: C++20 doesn't have it

This is the second trap, and the other C99 difference. C99 lets arrays use `[index] = value`:

```c
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // legal C99
```

C++20 **did not** adopt this. Feed the same line to a C++ compiler:

```cpp
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // won't compile in C++20
```

GCC's wording is a little coy: `sorry, unimplemented: non-trivial designated initializers not supported`. It doesn't quite say "the standard forbids it," but the effect is the same — no compile. C++20's designated initializers only cover the `.field` form for aggregates; array-index designators aren't in the standard. For partial array initialization, just use positional braces like `{1, 0, 5, 0, 12}`.

## Embedded use: constexpr config tables

Where designated initializers land most naturally in embedded is the **compile-time config table**. A set of pin configs or register maps, written as a `constexpr` array with `.field = value`, is self-documenting, fixed at compile time, and zero-cost at runtime:

```cpp
constexpr std::array<PinCfg, 4> kUartPins = {{
    {.pin = 9,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 10, .mode = GpioMode::Alternate, .pull = GpioPull::Up,   .alternate = 7},
    {.pin = 2,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
    {.pin = 3,  .mode = GpioMode::Alternate, .pull = GpioPull::None, .alternate = 7},
}};

constexpr std::array<RegMap, 4> kUartRegs = {{
    {.name = "SR",  .offset = 0x00, .read_only = true},
    {.name = "DR",  .offset = 0x04, .read_only = false},
    {.name = "BRR", .offset = 0x08, .read_only = false},
    {.name = "CR1", .offset = 0x0C, .read_only = false},
}};
```

This table reads like a table. Adding a row or changing a field doesn't ripple anywhere. Because it's `constexpr`, it's all settled at compile time, and the generated code is indistinguishable from a hand-written set of constants — but far easier to read. Register maps, pin tables, message templates, PWM channel configs all fit this pattern.

<OnlineCompilerDemo allow-run
  title="constexpr config table: compile-time pin table and register map"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_config_table.cpp"
  description="Designated initializers plus constexpr plus std::array form an embedded config table — zero runtime cost, far more readable than positional aggregate initialization."
/>

Run it:

```text
UART 引脚配置表:
  P9  mode=2 pull=0 af=7
  P10 mode=2 pull=1 af=7
  P2  mode=2 pull=0 af=7
  P3  mode=2 pull=0 af=7

UART 寄存器映射:
  SR   @0x00  RO
  DR   @0x04  RW
  BRR  @0x08  RW
  CR1  @0x0C  RW
```

## A few practical rules of thumb

When a type is an aggregate, reach for `.field = value`. Config code becomes self-documenting, and restructuring the struct later won't break initializers. For types with many fields and sensible defaults, pair them with default member initializers so partial initialization is painless.

If you want both "validation logic" and "field-style initialization," use a static factory — don't shoehorn a constructor into an aggregate. The moment you add one, you lose designated initializers.

And keep those two C++20-versus-C99 differences in mind: designators must follow declaration order, and there is no `[index]` for arrays. They're the easiest spots to take for granted when porting C code into C++. The compiler catches both on the first try, but it's better to know why going in.
