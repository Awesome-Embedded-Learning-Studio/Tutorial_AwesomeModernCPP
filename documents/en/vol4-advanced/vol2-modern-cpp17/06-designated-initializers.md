---
chapter: 11
cpp_standard:
- 20
description: A Detailed Guide to Modern C++ Designated Initializers and Embedded Applications
difficulty: intermediate
order: 6
platform: host
prerequisites:
- 'Chapter 11.1: auto与decltype'
- 'Chapter 11.2: 结构化绑定'
reading_time_minutes: 14
tags:
- cpp-modern
- host
- intermediate
title: designated initializer
translation:
  engine: anthropic
  source: documents/vol4-advanced/vol2-modern-cpp17/06-designated-initializers.md
  source_hash: ca599e190c8d7c1554ecd34fcb3c3316cbc301c74ec52b75609537dbcacc9fd6
  token_count: 4251
  translated_at: '2026-05-26T11:39:42.384508+00:00'
---
# Modern C++ for Embedded Development—Designated Initializers

## Introduction

Have you ever been driven crazy by obscure struct initializations like this when writing embedded code?

```cpp
// 传统初始化——必须记住声明顺序
UART_Config uart_cfg = {
    115200,    // baudrate
    8,         // data_bits
    0,         // parity
    1,         // stop_bits
    0,         // flow_control
    1,         // rx_enabled
    1          // tx_enabled
};
```

The biggest problem with this line of code is that we must remember the declaration order of the struct members. Once the struct definition changes (for example, inserting a new member in the middle), all initialization code might break. What's worse, the compiler won't flag this as an error—the weird behavior only shows up at runtime.

Designated initializers, introduced in C99 and officially adopted into the C++20 standard, exist to solve this problem. They allow us to initialize members by name, making our code clearer, safer, and easier to maintain.

> In a nutshell: **Designated initializers allow us to initialize struct members by name using the `.field = value` syntax, resulting in self-explanatory code that is independent of declaration order.**

However, using designated initializers in embedded development requires us to understand their mechanics and limitations because:

1. The syntax differs slightly from C (C++ uses `{.field = value}`)
2. They only work with aggregate types, not classes with constructors
3. We need a clear understanding of the default behavior for partial initialization
4. Compiler support levels vary

Let's walk through the correct way to use this feature step by step.

------

## Basic Syntax

### The Simplest Designated Initialization

C++20 designated initializers use the `.field = value` syntax inside braces:

```cpp
struct UART_Config {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
};

// 传统写法——按顺序初始化
UART_Config cfg1 = {115200, 8, 0, 1};

// 指定初始化器——按名字初始化
UART_Config cfg2 = {.baudrate = 115200, .data_bits = 8, .parity = 0, .stop_bits = 1};

// 乱序也没问题
UART_Config cfg3 = {.stop_bits = 1, .baudrate = 115200, .data_bits = 8, .parity = 0};
```

The advantages of the second approach are obvious:

1. **Self-explanatory code**: Each value is explicitly labeled with its corresponding field
2. **Order-independent**: It does not rely on the struct declaration order
3. **Easy to maintain**: Initialization code remains correct even if the struct definition changes

### Differences from C

The C language's designated initializer syntax is slightly different:

```c
// C99写法（C语言）
UART_Config cfg = {
    .baudrate = 115200,
    .data_bits = 8
};

// C++20写法（与C99相同）
UART_Config cfg = {
    .baudrate = 115200,
    .data_bits = 8
};
```

The good news is that C++20 adopted the same syntax as C99, which makes code between the two languages much more interoperable.

**Note**: Before C++20, certain compilers (like GCC and Clang) supported designated initializers as an extension, but their behavior might differ slightly from the C++20 standard.

------

## Aggregate Type Requirements

Designated initializers can only be used with aggregates. So, what exactly is an aggregate type?

### Definition of an Aggregate Type

In C++20, an aggregate type is a class type that meets the following conditions:

1. No user-declared constructors
2. No private or protected non-static data members
3. No virtual functions
4. No virtual base classes
5. No default member initializers (prior to C++14)

```cpp
// ✅ 聚合类型——可以使用指定初始化器
struct SensorConfig {
    uint8_t id;
    uint16_t sampling_rate;
    bool enabled;
};

SensorConfig cfg = {.id = 5, .sampling_rate = 1000, .enabled = true};

// ❌ 非聚合类型——不能使用指定初始化器
class DeviceConfig {
private:
    uint8_t id_;  // 私有成员
public:
    uint16_t rate;
    bool enabled;
};

// 下面的代码会编译错误
// DeviceConfig cfg = {.rate = 1000, .enabled = true};  // 错误！

// ❌ 非聚合类型——有构造函数
struct TimerConfig {
    uint32_t period;
    bool auto_reload;

    TimerConfig() = default;  // 用户声明的构造函数
};

// TimerConfig cfg = {.period = 1000};  // 错误！
```

### Arrays Are Also Aggregates

Arrays can also use designated initializers:

```cpp
// C风格数组的指定初始化
int pins[5] = {[0] = 1, [2] = 5, [4] = 12};
// 结果: {1, 0, 5, 0, 12}

// 嵌入式场景：GPIO引脚映射
constexpr uint8_t uart_tx_pins[] = {
    [0] = 9,   // UART1_TX -> PA9
    [1] = 2,   // UART2_TX -> PA2
    [2] = 10,  // UART3_TX -> PB10
    [3] = 0    // UART4_TX -> PA0（假设）
};
```

**Note**: The array designated initializer syntax `[index] = value` has complex support across C++ compilers; we recommend verifying compiler support before using it.

------

## Practical Embedded Scenarios

### Scenario 1: UART Configuration Initialization

```cpp
struct UART_Config {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;     // 0=None, 1=Odd, 2=Even
    uint8_t stop_bits;
    uint8_t flow_control;
    bool rx_enabled;
    bool tx_enabled;
};

// 只配置需要的参数，其他使用默认值
UART_Config uart1_cfg = {
    .baudrate = 115200,
    .data_bits = 8,
    .parity = 0,
    .stop_bits = 1
    // flow_control默认为0
    // rx_enabled, tx_enabled需要明确处理
};

// 完整配置
UART_Config uart2_cfg = {
    .baudrate = 921600,
    .data_bits = 8,
    .parity = 2,       // Even parity
    .stop_bits = 1,
    .flow_control = 1, // Hardware flow control
    .rx_enabled = true,
    .tx_enabled = true
};

void uart_init(UART_TypeDef* uart, const UART_Config& cfg) {
    // 配置波特率
    uart->BRR = SystemClock / cfg.baudrate;

    // 配置数据位
    uart->CR1 = (cfg.data_bits - 8) << USART_CR1_M_Pos;

    // 配置校验位
    if (cfg.parity == 1) {
        uart->CR1 |= USART_CR1_PCE;
    } else if (cfg.parity == 2) {
        uart->CR1 |= USART_CR1_PCE | USART_CR1_PS;
    }

    // 配置停止位
    uart->CR2 = (cfg.stop_bits - 1) << USART_CR2_STOP_Pos;

    // 使能接收和发送
    if (cfg.rx_enabled) {
        uart->CR1 |= USART_CR1_RE;
    }
    if (cfg.tx_enabled) {
        uart->CR1 |= USART_CR1_TE;
    }
}

// 使用
uart_init(USART1, {.baudrate = 115200, .data_bits = 8, .parity = 0});
```

### Scenario 2: GPIO Configuration

```cpp
enum class GPIOMode {
    Input,
    Output,
    Alternate,
    Analog
};

enum class GPIOPull {
    None,
    Up,
    Down
};

struct GPIO_PinConfig {
    uint8_t pin;
    GPIOMode mode;
    GPIOPull pull;
    uint8_t alternate;  // 复用功能编号
    uint8_t speed;      // GPIO速度等级
};

// 配置多个GPIO引脚
constexpr GPIO_PinConfig gpio_configs[] = {
    {.pin = 0, .mode = GPIOMode::Output, .pull = GPIOPull::None, .speed = 2},
    {.pin = 1, .mode = GPIOMode::Input, .pull = GPIOPull::Up, .speed = 0},
    {.pin = 9, .mode = GPIOMode::Alternate, .pull = GPIOPull::None, .alternate = 7, .speed = 3},
    {.pin = 10, .mode = GPIOMode::Alternate, .pull = GPIOPull::None, .alternate = 7, .speed = 3}
};

void gpio_init_port(GPIO_TypeDef* port, const GPIO_PinConfig* configs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto& cfg = configs[i];
        // 配置模式
        uint32_t mode_value = static_cast<uint32_t>(cfg.mode);
        port->MODER &= ~(0x3 << (cfg.pin * 2));
        port->MODER |= mode_value << (cfg.pin * 2);

        // 配置上下拉
        uint32_t pull_value = static_cast<uint32_t>(cfg.pull);
        port->PUPDR &= ~(0x3 << (cfg.pin * 2));
        port->PUPDR |= pull_value << (cfg.pin * 2);

        // 配置速度
        port->OSPEEDR &= ~(0x3 << (cfg.pin * 2));
        port->OSPEEDR |= cfg.speed << (cfg.pin * 2);

        // 配置复用功能
        if (cfg.mode == GPIOMode::Alternate) {
            uint32_t afr_index = (cfg.pin < 8) ? 0 : 1;
            uint32_t afr_shift = (cfg.pin < 8) ? cfg.pin * 4 : (cfg.pin - 8) * 4;
            port->AFR[afr_index] &= ~(0xF << afr_shift);
            port->AFR[afr_index] |= cfg.alternate << afr_shift;
        }
    }
}

// 使用
gpio_init_port(GPIOA, gpio_configs, 4);
```

### Scenario 3: SPI Configuration

```cpp
struct SPI_Config {
    uint32_t baudrate_prescaler;
    uint8_t mode;              // CPOL和CPHA组合：0-3
    uint8_t data_size;         // 数据位宽度：4-16
    bool first_bit_msb;        // true=MSB优先，false=LSB优先
    bool hardware_cs;          // 硬件片选控制
    bool crc_enable;           // CRC计算使能
};

// 标准SPI模式配置
constexpr SPI_Config spi_mode0_config = {
    .baudrate_prescaler = 2,   // 最高速度
    .mode = 0,                 // CPOL=0, CPHA=0
    .data_size = 8,
    .first_bit_msb = true,
    .hardware_cs = false,
    .crc_enable = false
};

constexpr SPI_Config spi_mode3_config = {
    .baudrate_prescaler = 4,   // 中等速度
    .mode = 3,                 // CPOL=1, CPHA=1
    .data_size = 16,
    .first_bit_msb = true,
    .hardware_cs = true,
    .crc_enable = true
};

// SD卡SPI配置（低速，特殊时序）
constexpr SPI_Config sdcard_spi_config = {
    .baudrate_prescaler = 64,  // 低速初始化
    .mode = 0,
    .data_size = 8,
    .first_bit_msb = true,
    .hardware_cs = false,
    .crc_enable = false
};
```

### Scenario 4: Timer Configuration

```cpp
enum class TimerMode {
    OneShot,
    Periodic,
    PWM
};

struct Timer_Channel {
    uint8_t channel;
    uint32_t pulse;       // 捕获比较值
    bool enabled;
};

struct Timer_Config {
    uint32_t prescaler;
    uint32_t period;      // 自动重装载值
    TimerMode mode;
    Timer_Channel channels[4];  // 4个通道
};

// PWM定时器配置
constexpr Timer_Config timer1_pwm_config = {
    .prescaler = 71,      // 1MHz计数频率（假设72MHz时钟）
    .period = 999,        // 1kHz PWM频率
    .mode = TimerMode::PWM,
    .channels = {
        {.channel = 1, .pulse = 500, .enabled = true},   // 50%占空比
        {.channel = 2, .pulse = 250, .enabled = true},   // 25%占空比
        {.channel = 3, .pulse = 0, .enabled = false},
        {.channel = 4, .pulse = 750, .enabled = true}    // 75%占空比
    }
};

// 基本定时器配置
constexpr Timer_Config timer2_base_config = {
    .prescaler = 7199,    // 10kHz计数频率
    .period = 9999,       // 1Hz定时频率
    .mode = TimerMode::Periodic,
    .channels = {}  // 所有通道不使能
};
```

### Scenario 5: Register Mapping Table

```cpp
struct RegisterMap {
    const char* name;
    uint32_t offset;
    uint32_t size;
    bool read_only;
};

// 外设寄存器映射
constexpr RegisterMap uart_registers[] = {
    {.name = "SR", .offset = 0x00, .size = 4, .read_only = true},
    {.name = "DR", .offset = 0x04, .size = 4, .read_only = false},
    {.name = "BRR", .offset = 0x08, .size = 4, .read_only = false},
    {.name = "CR1", .offset = 0x0C, .size = 4, .read_only = false},
    {.name = "CR2", .offset = 0x10, .size = 4, .read_only = false},
    {.name = "CR3", .offset = 0x14, .size = 4, .read_only = false}
};

void dump_registers(uintptr_t base_addr, const RegisterMap* map, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        volatile uint32_t* reg = reinterpret_cast<volatile uint32_t*>(base_addr + map[i].offset);
        printf("%s (0x%02X): 0x%08X\n", map[i].name, map[i].offset, *reg);
    }
}

// 使用
dump_registers(USART1_BASE, uart_registers, 6);
```

### Scenario 6: Message Packet Construction

```cpp
enum class MessageType : uint8_t {
    Heartbeat = 0x01,
    SensorData = 0x02,
    Command = 0x03,
    Ack = 0x04
};

struct Message {
    MessageType type;
    uint8_t source_id;
    uint8_t dest_id;
    uint16_t sequence;
    uint8_t payload[32];
    uint8_t payload_length;
    uint16_t checksum;
};

// 心跳消息
Message create_heartbeat(uint8_t id, uint16_t seq) {
    return Message{
        .type = MessageType::Heartbeat,
        .source_id = id,
        .dest_id = 0,     // 广播
        .sequence = seq,
        .payload = {},
        .payload_length = 0,
        .checksum = 0     // 稍后计算
    };
}

// 传感器数据消息
Message create_sensor_message(uint8_t id, uint16_t seq, const uint8_t* data, uint8_t len) {
    Message msg{
        .type = MessageType::SensorData,
        .source_id = id,
        .dest_id = 0,     // 发送到基站
        .sequence = seq,
        .payload_length = len,
        .checksum = 0
    };
    memcpy(msg.payload, data, len);
    msg.checksum = calculate_checksum(&msg);
    return msg;
}
```

------

## Partial Initialization and Default Values

### Behavior of Partial Initialization

When using designated initializers, unspecified members follow these rules:

1. If a default member initializer is present, use that default value
2. Otherwise, for aggregate types, perform value initialization (zero initialization)

```cpp
struct Config {
    uint32_t baudrate = 115200;  // 默认值
    uint8_t data_bits = 8;       // 默认值
    uint8_t parity = 0;          // 默认值
    uint8_t stop_bits = 1;       // 默认值
    bool enabled = true;         // 默认值
};

// 只覆盖部分成员
Config cfg1{.baudrate = 921600, .parity = 2};
// 结果：baudrate=921600, parity=2
//      data_bits=8(默认), stop_bits=1(默认), enabled=true(默认)

// 没有默认成员初始化器的情况
struct RawConfig {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
};

RawConfig cfg2{.baudrate = 115200, .parity = 0};
// 结果：baudrate=115200, parity=0
//      data_bits=0(零初始化), stop_bits=0(零初始化)
```

### Beware of Implicit Zero Initialization

```cpp
struct TimerConfig {
    uint32_t prescaler;
    uint32_t period;
    bool auto_reload;
};

// ❌ 可能引入bug：忘记初始化auto_reload
TimerConfig cfg{.prescaler = 1000, .period = 999};
// auto_reload被零初始化为false，这可能不是预期的！

// ✅ 明确指定所有重要成员
TimerConfig cfg{.prescaler = 1000, .period = 999, .auto_reload = true};
```

In embedded development, this implicit zero initialization can lead to hard-to-find bugs. We recommend always explicitly initializing all important members.

------

## Nested Structs and Arrays

### Initialization of Nested Structs

```cpp
struct PinConfig {
    uint8_t port;  // 0=GPIOA, 1=GPIOB, etc.
    uint8_t pin;
};

struct UARTConfig {
    uint32_t baudrate;
    PinConfig tx_pin;
    PinConfig rx_pin;
    bool hardware_flow_control;
};

// 嵌套初始化
UARTConfig cfg = {
    .baudrate = 115200,
    .tx_pin = {.port = 0, .pin = 9},   // PA9
    .rx_pin = {.port = 0, .pin = 10},  // PA10
    .hardware_flow_control = false
};
```

### Initialization of Array Members

```cpp
struct SPIConfig {
    uint32_t baudrate;
    uint8_t cs_pins[4];  // 最多4个片选引脚
    uint8_t cs_count;
};

SPIConfig cfg = {
    .baudrate = 1000000,
    .cs_pins = {[0] = 4, [1] = 5},  // 只初始化部分元素
    .cs_count = 2
};
// cs_pins = {4, 5, 0, 0}
```

**Note**: Support for the array designated initializer syntax `[index] = value` in C++20 may vary by compiler; we recommend verifying support before use.

------

## Working with Constructors

### Aggregate Types Cannot Have User-Defined Constructors

```cpp
// ❌ 有构造函数——不是聚合类型
struct Config {
    uint32_t baudrate;
    uint8_t data_bits;

    Config(uint32_t br, uint8_t db) : baudrate(br), data_bits(db) {}
};

// Config cfg{.baudrate = 115200};  // 编译错误！
```

If we need to support both constructors and designated initializers, we can consider the following approaches:

### Approach 1: Use a Static Factory Method

```cpp
struct Config {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;

    // 常用配置的静态工厂方法
    static Config standard() {
        return {.baudrate = 115200, .data_bits = 8, .parity = 0, .stop_bits = 1};
    }

    static Config custom(uint32_t br) {
        return {.baudrate = br, .data_bits = 8, .parity = 0, .stop_bits = 1};
    }
};

// 使用
auto cfg1 = Config::standard();
auto cfg2 = Config::custom(921600);
```

### Approach 2: Use Aggregate Initialization + Helper Functions

```cpp
struct Config {
    uint32_t baudrate;
    uint8_t data_bits;
    uint8_t parity;
    uint8_t stop_bits;
};

// 辅助函数用于配置验证和默认值填充
Config validate_config(Config partial) {
    if (partial.baudrate == 0) {
        partial.baudrate = 115200;
    }
    if (partial.data_bits == 0) {
        partial.data_bits = 8;
    }
    return partial;
}

// 使用
auto cfg = validate_config({.baudrate = 921600});
```

------

## Common Pitfalls and Limitations

### Pitfall 1: Order-Dependent Initialization

```cpp
struct Device {
    uint32_t base_address;
    uint32_t control_reg;
    uint32_t status_reg;

    // 方法：根据base_address计算寄存器偏移
    uint32_t get_control() const {
        return *reinterpret_cast<volatile uint32_t*>(base_address + control_reg);
    }
};

// ❌ 混乱的顺序
Device dev{.control_reg = 0x10, .base_address = 0x40000000, .status_reg = 0x14};
```

Although the syntax allows out-of-order initialization, for code readability, we recommend keeping the same order as the struct declaration.

### Pitfall 2: Impact of Member Reordering

```cpp
struct Config {
    uint8_t a;
    uint8_t b;
    uint8_t c;
};

Config cfg{.b = 2, .a = 1, .c = 3};
// 在内存中的布局仍然是 a=1, b=2, c=3（按声明顺序）

// 指定初始化器只影响初始化的书写，不影响内存布局
```

### Pitfall 3: Bit-Field Members

```cpp
struct Flags {
    unsigned int flag1 : 1;
    unsigned int flag2 : 1;
    unsigned int flag3 : 1;
    unsigned int reserved : 5;
};

// 位域可以使用指定初始化器
Flags f{.flag1 = 1, .flag3 = 1};
// 结果：flag1=1, flag2=0, flag3=1, reserved=0
```

### Pitfall 4: Designated Initialization of Unions

```cpp
union Data {
    uint32_t as_uint32;
    struct {
        uint16_t low;
        uint16_t high;
    } as_words;
    uint8_t as_bytes[4];
};

// 只能初始化一个成员
Data d1{.as_uint32 = 0x12345678};
Data d2{.as_words = {.low = 0x5678, .high = 0x1234}};
// Data d3{.as_uint32 = 0x1234, .as_words = {...}};  // 错误！
```

### Pitfall 5: Precedence of Non-Static Member Initializers

```cpp
struct Config {
    uint32_t baudrate = 9600;
    uint8_t data_bits = 8;
};

Config cfg{.baudrate = 115200};
// data_bits使用默认成员初始化器8
```

Values explicitly specified by designated initializers will override default member initializers.

### Limitation 1: Cannot Be Used with Non-Aggregate Types

```cpp
class NonAggregate {
private:
    int x;
public:
    int y;
};

// NonAggregate na{.y = 5};  // 编译错误！有私有成员
```

### Limitation 2: Cannot Specify the Same Member Multiple Times

```cpp
struct Config {
    uint32_t baudrate;
};

// Config cfg{.baudrate = 115200, .baudrate = 921600};  // 编译错误！
```

### Limitation 3: Skipping Member Initialization on Certain Compilers

Although the C++20 standard allows partial initialization, in practice, some compilers may have additional restrictions or warnings.

### Limitation 4: Interaction with Base Classes

```cpp
struct Base {
    int x;
};

struct Derived : Base {
    int y;
};

// Derived d{.x = 1, .y = 2};  // 编译错误！不能直接初始化基类成员

// 需要先初始化基类部分
Derived d{{.x = 1}, .y = 2};  // 可能的语法，但取决于编译器支持
```

------

## C++20 Updates

C++20 officially brought designated initializers into the standard, with key features including:

1. **Standardized syntax**: `.field = value` became standard syntax
2. **Updated aggregate definition**: The definition of an aggregate type was relaxed
3. **Interaction with templates**: Designated initializers can be used within templates

### Usage in Templates

```cpp
template<typename T>
struct Buffer {
    T* data;
    size_t size;
    size_t capacity;
};

// 在模板中使用指定初始化器
Buffer<int> buf{.data = nullptr, .size = 0, .capacity = 100};
```

### constexpr Contexts

```cpp
struct Pin {
    uint8_t port;
    uint8_t pin;
};

constexpr Pin uart_pins[] = {
    {.port = 0, .pin = 9},
    {.port = 0, .pin = 10}
};
// 可以在编译期使用

static_assert(uart_pins[0].port == 0);
```

------

## Compiler Support

| Compiler | Extension Support | C++20 Standard Support |
|--------|------------|--------------|
| GCC | 4.x+ | GCC 8+ |
| Clang | 3.x+ | Clang 10+ |
| MSVC | Not supported | VS 2019 16.8+ |

When writing portable code, we recommend:

```cpp
// 检查编译器支持
#if __cplusplus >= 202002L && \
    (defined(__GNUC__) && __GNUC__ >= 8 || \
     defined(__clang__) && __clang_major__ >= 10 || \
     defined(_MSC_VER) && _MSC_VER >= 1928)
    #define HAVE_DESIGNATED_INIT 1
#else
    #define HAVE_DESIGNATED_INIT 0
#endif

#if HAVE_DESIGNATED_INIT
    Config cfg{.baudrate = 115200};
#else
    Config cfg;
    cfg.baudrate = 115200;
#endif
```

------

## Summary

Designated initializers provide a concise and safe initialization method in modern C++:

**Comparison with Traditional Initialization**:

| Feature | Traditional Initialization | Designated Initializers |
|------|----------|------------|
| Order-dependent | Yes | No |
| Code readability | Poor (requires checking definition) | Good (self-explanatory) |
| Maintainability | Poor (requires updates when struct changes) | Good (unaffected by struct changes) |
| Partial initialization | Supported (positional) | Supported (by name) |

**Practical Recommendations**:

1. **Preferred use cases**:
   - Configuration struct initialization
   - Register mapping tables
   - Hardware configuration constants
   - Message packet construction

2. **Use with caution**:
   - Initialization requiring validation logic (consider factory functions)
   - Complex initialization order dependencies
   - Projects that need to support older compilers

3. **Embedded-specific focus**:
   - Understand the default behavior of partial initialization
   - Be aware of bugs that zero initialization might introduce
   - Verify compiler support
   - Maintain consistency with the struct declaration order for better readability

4. **Performance considerations**:
   - Designated initializers are a compile-time feature with no runtime overhead
   - They generate the same machine code as traditional aggregate initialization
   - We can safely use them in performance-critical code

Designated initializers bring C++ configuration code closer to a declarative programming style. Combined with `constexpr`, we can accomplish a great deal of configuration work at compile time, making them an essential tool for modern C++ embedded development. Along with features we've covered earlier like `auto`, structured bindings, and attributes, we can write embedded C++ code that is both efficient and easy to maintain.
