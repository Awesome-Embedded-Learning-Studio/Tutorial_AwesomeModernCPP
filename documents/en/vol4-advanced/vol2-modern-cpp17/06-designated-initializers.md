---
chapter: 11
cpp_standard:
- 20
description: 'Modern C++ Designated Initializers: Deep Dive and Embedded Applications'
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 11.1: auto与decltype'
- 'Chapter 11.2: 结构化绑定'
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
title: designated initializer
translation:
  source: documents/vol4-advanced/vol2-modern-cpp17/06-designated-initializers.md
  source_hash: 8d2e920124d4dbab7a3677f94d872d61e3e2abfff3c3ec12d2b69ef21d649cf2
  translated_at: '2026-06-16T04:02:08.087200+00:00'
  engine: anthropic
  token_count: 4247
---
# Modern C++ for Embedded Development — Designated Initializers

## Introduction

When writing embedded code, have you ever been frustrated by obscure struct initializations like this?

```cpp
UART_InitTypeDef uart;
uart.BaudRate = 115200;
uart.WordLength = UART_WORDLENGTH_8B;
uart.StopBits = UART_STOPBITS_1;
uart.Parity = UART_PARITY_NONE;

// Or even worse, the positional initialization nightmare:
TIM_TimeBaseInitTypeDef timer = { 0, 999, 0, TIM_COUNTERMODE_UP, 0 };
```

The biggest problem with this code is that you must remember the declaration order of the struct members. If the struct definition changes (for example, inserting a new member in the middle), all initialization code might break. Worse still, the compiler won't report an error, and strange behaviors only manifest at runtime.

C99 introduced designated initializers, and C++20 officially incorporated them into the standard to solve this problem—allowing us to initialize members by name. This makes code clearer, safer, and easier to maintain.

> TL;DR: **Designated initializers allow initializing struct members by name using the `.member = value` syntax, creating self-documenting code that is independent of declaration order.**

However, using designated initializers in embedded development requires understanding their mechanics and limitations because:

1. The syntax differs slightly from C (C++ uses braces `{}`).
2. They can only be used for aggregate types, not classes with constructors.
3. The default behavior of partial initialization needs to be clearly understood.
4. Support varies across different compilers.

Let's walk through the correct usage of this feature step by step.

------

## Basic Syntax

### Simple Designated Initialization

C++20 designated initializers use the `.member{value}` syntax inside braces:

```cpp
struct Point {
    int x;
    int y;
    int z;
};

// Traditional initialization (order-dependent)
Point p1 = { 10, 20, 30 }; // x=10, y=20, z=30

// Designated initialization (order-independent)
Point p2 = { .z{30}, .x{10}, .y{20} }; // x=10, y=20, z=30
```

The advantage of the second approach is obvious:

1. **Self-documenting code**: Each value explicitly labels its corresponding field.
2. **Order-independent**: Does not rely on the struct declaration order.
3. **Easy to maintain**: Initialization code remains correct even if the struct definition changes.

### Differences from C

The syntax for designated initializers in C is slightly different:

```c
// C99 style (uses =)
Point p2 = { .z = 30, .x = 10, .y = 20 };
```

Good news: C++20 adopted the same syntax as C99, allowing for better interoperability between the two languages.

**Note**: Before C++20, some compilers (like GCC, Clang) supported designated initializers as an extension, but the behavior might differ slightly from the C++20 standard.

------

## Aggregate Type Requirements

Designated initializers can only be used with aggregate types. So, what is an aggregate type?

### Definition of an Aggregate Type

In C++20, an aggregate type is a class type that satisfies the following conditions:

1. No user-declared constructors.
2. No private or protected non-static data members.
3. No virtual functions.
4. No virtual base classes.
5. No default member initializers (prior to C++14).

```cpp
// This is an aggregate
struct SensorConfig {
    int pin;
    int threshold;
    bool enabled;
};

// This is NOT an aggregate (has user-declared constructor)
struct SensorConfig {
    int pin;
    SensorConfig(int p) : pin(p) {} // Not an aggregate
};
```

### Arrays Are Also Aggregate Types

Arrays can also use designated initializers:

```cpp
int arr[5] = { [3] = 10, [1] = 20 }; // C style, mostly C-compatible
// Note: C++ designated initializers for arrays have limited support
```

**Note**: Support for array designated initializer syntax `[index] =` varies in C++; verify compiler support before use.

------

## Embedded Scenarios in Practice

### Scenario 1: UART Configuration Initialization

```cpp
struct UARTConfig {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
    bool flow_control;
};

void init_uart() {
    // Clear and safe
    UARTConfig cfg = {
        .baud_rate = 115200,
        .data_bits = 8,
        .stop_bits = 1,
        .parity = 0,       // None
        .flow_control = false
    };
    // Apply configuration...
}
```

### Scenario 2: GPIO Configuration

```cpp
struct GPIOConfig {
    GPIO_Port port;
    uint16_t pin;
    GPIO_Mode mode;
    GPIO_Pull pull;
    GPIO_Speed speed;
};

GPIOConfig led_config = {
    .port = GPIOA,
    .pin = 5,
    .mode = GPIO_MODE_OUTPUT_PP,
    .pull = GPIO_NOPULL,
    .speed = GPIO_SPEED_FREQ_LOW
};
```

### Scenario 3: SPI Configuration

```cpp
struct SPIConfig {
    SPI_HandleTypeDef handle;
    uint32_t mode;
    uint32_t baud_prescaler;
    uint32_t bit_order;
};

SPIConfig spi_flash = {
    .mode = SPI_MODE_MASTER,
    .baud_prescaler = SPI_BAUDRATEPRESCALER_4,
    .bit_order = SPI_FIRSTBIT_MSB
    // handle left default-initialized
};
```

### Scenario 4: Timer Configuration

```cpp
struct TimerConfig {
    uint32_t prescaler;
    uint32_t period;
    uint32_t clock_division;
    uint32_t counter_mode;
};

TimerConfig pwm_timer = {
    .prescaler = 71,      // 1MHz tick
    .period = 999,        // 1kHz PWM
    .counter_mode = TIM_COUNTERMODE_UP
};
```

### Scenario 5: Register Map Table

```cpp
struct RegisterMap {
    volatile uint32_t ctrl;
    volatile uint32_t status;
    volatile uint32_t data;
    volatile uint32_t reserved[4];
};

// Memory-mapped IO initialization
const RegisterMap peripheral_base = {
    .ctrl = 0x00,
    .status = 0x00,
    .data = 0x00
};
```

### Scenario 6: Message Packet Construction

```cpp
struct Packet {
    uint8_t start_byte;
    uint8_t cmd;
    uint16_t length;
    uint8_t payload[256];
    uint16_t checksum;
};

Packet cmd_packet = {
    .start_byte = 0xAA,
    .cmd = 0x01,
    .length = 4,
    .payload = { 0x01, 0x02, 0x03, 0x04 },
    .checksum = 0x1234
};
```

------

## Partial Initialization and Default Values

### Behavior of Partial Initialization

When using designated initializers, unspecified members follow these rules:

1. If there is a default member initializer, use that default value.
2. Otherwise, for aggregate types, perform value initialization (zero-initialization).

```cpp
struct Device {
    int id = 1;           // Default member initializer
    int status;          // No default
    int priority = 10;   // Default member initializer
};

Device dev = { .id{5} };
// Result: id=5, status=0 (zero-initialized), priority=10 (default initializer)
```

### Beware of Implicit Zero Initialization

```cpp
struct Buffer {
    uint8_t* data;
    size_t size;
    bool is_ready;
};

Buffer buf = { .data{nullptr} };
// Result: data=nullptr, size=0, is_ready=false
```

In embedded development, this implicit zero-initialization can lead to hard-to-find bugs. It is recommended to always explicitly initialize all important members.

------

## Nested Structs and Arrays

### Initializing Nested Structs

```cpp
struct Inner {
    int x;
    int y;
};

struct Outer {
    int a;
    Inner inner;
    int b;
};

Outer out = {
    .a{10},
    .inner{ .x{1}, .y{2} },
    .b{20}
};
```

### Initializing Array Members

```cpp
struct ArrayHolder {
    int values[5];
    int count;
};

ArrayHolder holder = {
    .values{ [0]{1}, [4]{5} }, // Note: Array designated init support varies
    .count{2}
};
```

**Note**: Support for array designated initializer syntax `[index]` in C++20 may vary by compiler; verify before use.

------

## Interaction with Constructors

### Aggregate Types Cannot Have User-Defined Constructors

```cpp
struct Bad {
    int x;
    Bad() = default; // User-declared constructor -> Not an aggregate
};

// Bad b = { .x{1} }; // Error: Not an aggregate
```

If you need to support both constructors and designated initializers, consider the following approaches:

### Solution 1: Use Static Factory Methods

```cpp
struct Config {
    int baud;
    int mode;

    static Config create(int b) {
        return { .baud{b}, .mode{0} };
    }
};

Config cfg = Config::create(115200);
```

### Solution 2: Use Aggregate Initialization + Helper Functions

```cpp
struct Config {
    int baud;
    int mode;
};

Config make_default_config() {
    return { .baud{9600}, .mode{1} };
}
```

------

## Common Pitfalls and Limitations

### Pitfall 1: Order-Dependent Initialization

```cpp
struct Data {
    int a;
    int b;
};

Data d = { .b{2}, .a{1} }; // Valid, but confusing
```

While the syntax allows out-of-order initialization, for readability, it is recommended to keep the order consistent with the struct declaration.

### Pitfall 2: Impact of Member Reordering

```cpp
struct V1 {
    int x;
    int y;
};

struct V2 {
    int y; // Reordered
    int x;
};

V2 v = { .x{1}, .y{2} }; // Safe! Order independent
```

### Pitfall 3: Bit Field Members

```cpp
struct Flags {
    unsigned int flag1 : 1;
    unsigned int flag2 : 1;
};

Flags f = { .flag1{1}, .flag2{0} }; // Supported
```

### Pitfall 4: Designated Initialization for Unions

```cpp
union Data {
    int i;
    float f;
};

Data d = { .i{42} }; // OK
// Data d2 = { .i{42}, .f{3.14f} }; // Error: Only one member can be initialized
```

### Pitfall 5: Precedence of Non-Static Member Initializers

```cpp
struct S {
    int x = 10;
};

S s = { .x{20} }; // x is 20, the explicit value overrides the default
```

Explicitly specified values in designated initializers override default member initializers.

### Limitation 1: Cannot Be Used on Non-Aggregate Types

```cpp
class NonAggregate {
private:
    int x;
public:
    NonAggregate(int v) : x(v) {}
};

// NonAggregate n = { .x{10} }; // Error: Not an aggregate
```

### Limitation 2: Cannot Specify the Same Member Multiple Times

```cpp
struct Point { int x; int y; };
// Point p = { .x{1}, .x{2} }; // Error: Duplicate member initialization
```

### Limitation 3: Cannot Skip Members in Some Compilers

While the C++20 standard allows partial initialization, some compilers may have additional restrictions or warnings in practice.

### Limitation 4: Interaction with Base Classes

```cpp
struct Base { int x; };
struct Derived : Base { int y; };

// Derived d = { .x{1}, .y{2} }; // Error: Cannot designate base class members directly
Derived d = { .y{2} }; // OK, x is zero-initialized
```

------

## C++20 Updates

C++20 officially incorporated designated initializers into the standard. Key features include:

1. **Standardized Syntax**: `.member{value}` becomes standard syntax.
2. **Updated Aggregate Definition**: Relaxed the definition of aggregate types.
3. **Interaction with Templates**: Can be used in templates.

### Usage in Templates

```cpp
template<typename T>
struct Container {
    T value;
    int id;
};

Container<float> c = { .value{3.14f}, .id{1} };
```

### constexpr Context

```cpp
struct Point {
    int x;
    int y;
};

constexpr Point origin = { .x{0}, .y{0} };
static_assert(origin.x == 0);
```

------

## Compiler Support

| Compiler | Support as Extension | C++20 Standard Support |
|----------|---------------------|------------------------|
| GCC | 4.x+ | GCC 8+ |
| Clang | 3.x+ | Clang 10+ |
| MSVC | Not Supported | VS 2019 16.8+ |

When writing portable code, it is recommended to:

```cpp
#if __cplusplus >= 202002L
    Point p = { .x{1}, .y{2} };
#else
    Point p = { 1, 2 }; // Fallback
#endif
```

------

## Summary

Designated initializers offer a concise and safe way to initialize objects in modern C++:

**Comparison with Traditional Initialization**:

| Feature | Traditional Initialization | Designated Initializers |
|---------|---------------------------|------------------------|
| Order Dependency | Yes | No |
| Code Readability | Poor (need to check definition) | Good (self-documenting) |
| Maintainability | Poor (struct changes require updates) | Good (immune to struct changes) |
| Partial Initialization | Supported (positional) | Supported (by name) |

**Practical Recommendations**:

1. **Prefer in these scenarios**:
   - Configuration struct initialization.
   - Register map tables.
   - Hardware configuration constants.
   - Message packet construction.

2. **Use with caution in these scenarios**:
   - Initialization requiring validation logic (consider factory functions).
   - Complex initialization order dependencies.
   - Projects needing to support older compilers.

3. **Embedded specific focus**:
   - Understand the default behavior of partial initialization.
   - Be aware of bugs introduced by zero-initialization.
   - Verify compiler support.
   - Keep order consistent with struct declaration for readability.

4. **Performance considerations**:
   - Designated initializers are a compile-time feature with no runtime overhead.
   - Generates the same machine code as traditional aggregate initialization.
   - Safe to use in performance-critical code.

Designated initializers bring C++ configuration code closer to a declarative programming style. Combined with `constexpr`, we can accomplish significant configuration work at compile time, making it an essential tool for modern C++ embedded development. Along with previously learned features like `auto`, structured binding, and attributes, we can write embedded C++ code that is both efficient and easy to maintain.
