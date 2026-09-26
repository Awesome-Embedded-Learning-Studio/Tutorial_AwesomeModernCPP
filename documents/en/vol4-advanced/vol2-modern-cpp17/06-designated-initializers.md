---
chapter: 12
cpp_standard:
- 20
description: 'C++20 designated initializers initialize aggregate members by name with .field = value; the designators must follow declaration order (unlike C99), and members left unspecified fall back to defaults. They only work on aggregates, and the [index] syntax for arrays is not supported'
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
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/06-designated-initializers.md
  source_hash: fb210e41141f8b33a482785b6b773baa9ab8039970ef50fb540903eb620bd59d
  translated_at: '2026-09-26T03:26:14+00:00'
  engine: anthropic
  token_count: 6200
---
# Designated Initializers

Once a config struct grows long, positional initialization starts to get scary. Take a UART config with seven or eight fields, filled in one by one:

```cpp
UartConfig cfg = {115200, 8, 0, 1, 0, 1, 1};   // who can remember which slot is which
```

We have all been bitten by this style: once the field count grows you end up counting positions against the declaration, inserting a new member in the middle forces every initializer to shift, and the compiler will not stop you—a mistake just surfaces later as weird runtime behavior. C99 offered a fix called the designated initializer, and C++20 brought it into the standard, letting us initialize with `.field = value`, by name. But the C++20 version differs from C99 in two key places, which we will cover as we go—and those two places happen to be the easiest traps to step into.

## Specify with `.field = value`, and in Declaration Order

The most basic usage: inside the braces, name each one as `.field = value`:

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

Every value is labeled with a name instead of leaning on position—that is its biggest benefit. But C++20 has a hard rule here that differs from C99: **the designators must appear in the same order as the member declarations**. If, for the sake of convenience, you write `stop_bits` ahead of `baudrate`:

```cpp
UartConfig bad{.stop_bits = 1, .baudrate = 115200};   // won't compile in C++20
```

GCC rejects it outright:

```text
error: designator order for field 'UartConfig::baudrate' does not match declaration order in 'UartConfig'
```

In C99 this kind of reordering is legal; C++20 dropped it. The reason traces back to C++'s initialization rules: brace initialization already walks the members in declaration order, and a designator is merely an annotation that "sticks a name onto the current position"—not a pass to "jump around and pick". So you may omit later members when writing designators, but the order must not be scrambled.

<OnlineCompilerDemo allow-run
  title="Basic syntax: specify by name, follow declaration order, partial initialization falls back to defaults"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_basics.cpp"
  description="Specify each field in declaration order; under partial initialization, unspecified members use their default member initializers; add -DOUT_OF_ORDER to reproduce the out-of-order compile error."
/>

Output:

```text
cfg: baud=115200 bits=8 parity=0 stop=1
partial: baud=921600 data_bits=8(默认8) parity=2 stop=1(默认1)
```

## Aggregates Only

Designated initializers only work on **aggregate types**. Put simply, a class is an aggregate when it satisfies: no user-declared constructors, no private or protected non-static data members, no virtual functions, no virtual base classes. Ordinary structs, and classes that meet those conditions, are all aggregates.

The moment you add a constructor, it is no longer an aggregate, and designated initializers immediately stop working:

```cpp
struct WithCtor {
    int a;
    int b;
    WithCtor(int, int) {}
};

WithCtor x{.a = 1, .b = 2};   // won't compile: not an aggregate
```

The error message is blunt:

```text
error: designated initializers cannot be used with a non-aggregate type 'WithCtor'
```

This restriction actually draws a clean division of responsibility: for a class with constructors, initialization logic belongs to the constructor (where you can validate, provide defaults, and throw); an aggregate has no constructor, so initialization relies on braces, and designated initializers simply make the braces clearer. Want both? Give the struct a static factory function, and use designated initializers inside the factory:

```cpp
struct Config {
    int baudrate;
    int data_bits;
    static Config standard() { return {.baudrate = 115200, .data_bits = 8}; }
};
```

## What Happens to Members You Did Not Specify

Under partial initialization, members that go unnamed follow two rules: those with a default member initializer (`int data_bits = 8;`) get the default; the rest are value-initialized, which for built-in types means zero. In the `partial` line above, `{.baudrate = 921600, .parity = 2}` leaves `data_bits` and `stop_bits` unwritten, so they fall back to the defaults `8` and `1` respectively.

::: warning Watch out for implicit zero-initialization, and do not be spooked by -Wmissing-field-initializers
Without a default member initializer, a member you leave out gets zero-initialized. For a `bool auto_reload`, zero-initializing to `false` may not be what you want—write the critical members out explicitly.

Also, partial initialization trips GCC's `-Wmissing-field-initializers` warning (it is part of `-Wextra`), reminding you that some members went unwritten. It is a **warning, not an error**, and for built-in types an omission is usually safe (zero-initialization)—but it serves nicely as a "did I miss something?" nudge, so do not assume something is broken the moment you see it.
:::

## Nested Structs, Bit-fields, and Unions Are All Supported

Designated initializers work across the common aggregate variants. Nested structs just go down one level at a time:

```cpp
struct Pin { std::uint8_t port; std::uint8_t pin; };
struct UartCfg { std::uint32_t baud; Pin tx; Pin rx; };

UartCfg u{
    .baud = 115200,
    .tx = {.port = 0, .pin = 9},
    .rx = {.port = 0, .pin = 10},
};
```

Bit-fields work too, and so do unions (a union can only have one member initialized):

```cpp
struct Flags { unsigned a : 1; unsigned b : 1; unsigned c : 6; };
Flags f{.a = 1, .b = 0, .c = 5};

union Value { int i; float f; };
Value v{.f = 3.14f};
```

<OnlineCompilerDemo allow-run
  title="Aggregate variants: nested structs, bit-fields, unions"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_aggregate.cpp"
  description="Nested structs specified layer by layer, bit-fields set by name, a union with one member specified. Add -DNON_AGGREGATE to reproduce the non-aggregate error."
/>

Output:

```text
uart: baud=115200 tx=PA9 rx=PA10
flags: a=1 b=0 c=5
union as float: 3.14
```

## No `[index]` for Arrays: C++20 Does Not Support It

This is the second easy trap to fall into, and the other difference from C99. C99 allows array designated initializers of the form `[index] = value`:

```c
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // legal C99
```

C++20 **did not** adopt this. Feed the same syntax to a C++ compiler:

```cpp
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};   // won't compile in C++20
```

GCC's wording is a bit coy—it reports `sorry, unimplemented: non-trivial designated initializers not supported`. It does not quite say "the standard forbids it", but the effect is the same: it will not compile. C++20's designated initializers cover only the `.field` form for aggregates; designators with array subscripts simply are not in the standard. To partially initialize an array, honestly use positional braces like `{1, 0, 5, 0, 12}` and be done with it.

## Embedded Practice: The constexpr Config Table

The most natural landing spot for designated initializers in embedded work is the **compile-time config table**. A pile of pin configurations or register maps, written as `constexpr` arrays with `.field = value`, is self-explanatory, nailed down at compile time, and zero-cost at runtime:

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

This config table reads like a table: adding a row or changing a field drags nothing else along with it. Because it lives in `constexpr`, everything is settled at compile time, and the generated code is no different from a hand-written set of constants—yet the readability is a notch higher. Register maps, pin tables, message templates, PWM channel configurations: the pattern applies to all of them directly.

<OnlineCompilerDemo allow-run
  title="constexpr config table: a pin table and register map fixed at compile time"
  source-path="code/examples/vol4/vol2-modern-cpp17/designated_config_table.cpp"
  description="Designated initializers plus constexpr plus std::array form an embedded config table: zero runtime overhead, and far more readable than positional aggregate initialization."
/>

Output:

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

## A Few Practical Guidelines

When a type is an aggregate, reach for `.field = value` first: the configuration code becomes self-explanatory on the spot, and later changes to the struct definition lose their power to frighten you. For types with many members where you also want defaults, pair them with default member initializers—partial initialization becomes painless.

If you want both "validation logic" and "field-style initialization", go with a static factory; do not shoehorn a constructor into an aggregate—the moment you do, the type forfeits its designated-initializer eligibility.

Finally, burn in those two differences between C++20 and C99: designators must follow declaration order, and there is no `[index]` syntax for arrays. These two are the easiest things to take for granted when migrating C code into C++. The compiler will stop you the first time you trip on either, but it is best to know the why in advance.
