---
chapter: 15
difficulty: beginner
order: 5
platform: stm32f1
reading_time_minutes: 21
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 10: HAL_GPIO_Init — The Ritual of Telling the Chip About Pin Configurations'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/01-led/05-hal-gpio-init.md
  source_hash: 072f6cbaf94f8bf45298818233d9e9c74661fe24b4b350037a2e52a96b8838b9
  token_count: 3062
  translated_at: '2026-05-26T12:05:21.215210+00:00'
description: ''
---
# Part 10: HAL_GPIO_Init — The Ritual of Telling the Chip How to Configure Its Pins

## Introduction: The Pin Is Awake, But It Doesn't Know What to Do Yet

In the previous article, we finally pushed open the gates to the clock. `__HAL_RCC_GPIOC_CLK_ENABLE()` Once this macro executes, the GPIOC port wakes from its slumber, and its registers begin responding to bus read and write requests. We used an analogy at the time: enabling the clock is like connecting power to a factory, giving the machines the prerequisite conditions to run. But powering up doesn't mean starting production—each machine still needs someone to tell it what to produce, at what pace, and what the safety standards are.

The same logic applies to GPIO pins. After the clock is enabled, the pin's seven registers (CRL, CRH, IDR, ODR, BSRR, BRR, LCKR) all become writable, but they still hold their default post-reset values. For PC13, the default values of CRL and CRH after reset are `0x44444444`, which means each pin is configured in "floating input" mode. In other words, PC13 is right now like a pedestrian standing at a crossroads, looking around blankly, unsure of which way to go.

We need to explicitly tell it: you should operate in push-pull output mode, toggle at 2MHz, and you don't need pull-up or pull-down resistors. And the way we deliver this "appointment letter" to the chip is by calling `HAL_GPIO_Init()`. This function is a contract between us and the hardware—we pack all our expectations for the pin into a struct, and it takes responsibility for translating those expectations bit by bit into register configuration values, writing them into the corresponding memory-mapped addresses. In today's article, we will tear apart every clause of this contract to understand exactly what happens behind each line of code.

## GPIO_InitTypeDef: A Carefully Designed Configuration Checklist

Let's first look at the function signature of `HAL_GPIO_Init()`:

```c
void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
```

Two parameters: one pointing to the port, and one pointing to the configuration. It couldn't be more concise. But beneath this simplicity lies a wealth of details worth digging into.

### The First Parameter: GPIO_TypeDef *GPIOx

`GPIOx` is a pointer to a `GPIO_TypeDef` struct. In the memory map of the STM32F103C8T6, each GPIO port occupies a contiguous address space, and `GPIO_TypeDef` is the structured description of that space. The base address of GPIOA is `0x40010800`, GPIOB is `0x40010C00`, and GPIOC is `0x40011000`—each port is separated by `0x400` bytes, which is 1KB of space. Out of this 1KB, only seven 32-bit registers are actually used, totaling 28 bytes, with the rest reserved.

In our `gpio.hpp`, we use `enum class GpioPort` to wrap these base addresses into type-safe enum values:

```cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,
    B = GPIOB_BASE,
    C = GPIOC_BASE,
    D = GPIOD_BASE,
    E = GPIOE_BASE,
};
```

And in the `native_port()` method of the `GPIO` class, we convert this enum value back to the `GPIO_TypeDef*` pointer that the HAL library expects via `reinterpret_cast`:

```cpp
static constexpr GPIO_TypeDef* native_port() noexcept {
    return reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
}
```

This layer of conversion might seem redundant at first glance—why not just use the `GPIOC` macro directly? Because C++'s type system doesn't allow us to treat an integer directly as a pointer. Although the underlying value of `GpioPort::C` is the integer `GPIOC_BASE`, in C++'s type system it is a `GpioPort` enum value and cannot be implicitly converted to a pointer. We need to first convert it to `uintptr_t` (an integer type large enough to hold a pointer), and then use `reinterpret_cast` to tell the compiler, "please treat this integer as a pointer." The benefit of doing this is that at the template parameter level, `GpioPort` is a genuine type, and the compiler can help us check at compile time whether a valid port value was passed.

### The Second Parameter: GPIO_InitTypeDef *GPIO_Init

This is the real star of today's show. `GPIO_InitTypeDef` is a struct with only four fields, but these four fields determine every behavioral characteristic of a pin:

```c
typedef struct {
    uint32_t Pin;    // 引脚编号
    uint32_t Mode;   // 工作模式
    uint32_t Pull;   // 上下拉配置
    uint32_t Speed;  // 输出速度
} GPIO_InitTypeDef;
```

Four `uint32_t`s, sixteen bytes, and the personality of a pin is fully defined. Let's break them down one by one.

### The Pin Field: Selecting Your Pin with a Bitmask

The way the Pin field is used might seem a bit odd when you first encounter it—it's not a simple number (like `13`), but a bitmask (like `0x2000`). In the HAL library's header file, the sixteen pins are defined like this:

```c
#define GPIO_PIN_0   ((uint16_t)0x0001U)  // 0000 0000 0000 0001
#define GPIO_PIN_1   ((uint16_t)0x0002U)  // 0000 0000 0000 0010
#define GPIO_PIN_2   ((uint16_t)0x0004U)  // 0000 0000 0000 0100
#define GPIO_PIN_3   ((uint16_t)0x0008U)  // 0000 0000 0000 1000
// ... 以此类推，每一位对应一个引脚
#define GPIO_PIN_13  ((uint16_t)0x2000U)  // 0010 0000 0000 0000
#define GPIO_PIN_14  ((uint16_t)0x4000U)  // 0100 0000 0000 0000
#define GPIO_PIN_15  ((uint16_t)0x8000U)  // 1000 0000 0000 0000
#define GPIO_PIN_ALL ((uint16_t)0xFFFFU)  // 1111 1111 1111 1111
```

If you have a good eye for binary, you'll spot the pattern immediately: the essence of `GPIO_PIN_n` is simply `(1 << n)`, which shifts `1` left by n bits. `GPIO_PIN_0` has bit 0 set to 1, and `GPIO_PIN_13` has bit 13 set to 1—a perfect one-to-one correspondence. This is no coincidence, but a carefully designed encoding scheme. Each pin occupies an independent bit in a 16-bit integer, and the pin number is the bit position.

This bitmask design brings a direct benefit: you can configure multiple pins at once using a bitwise OR operation. For example, if you want to configure PA0 and PA5 simultaneously, you only need to write `GPIO_PIN_0 | GPIO_PIN_5`, which results in `0x0021`, with both bit 0 and bit 5 set to 1. Internally, `HAL_GPIO_Init()` uses a loop to scan these 16 bits, configuring whichever pin has a corresponding bit set to 1. This is extremely useful when you need to batch-initialize multiple pins—one single call gets the job done, instead of writing sixteen.

In our project, the LED is connected to PC13, so we pass in `GPIO_PIN_13`. It's worth noting that in `main.cpp`, we directly use the HAL library's macro:

```cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
```

This `GPIO_PIN_13` macro expands to `(uint16_t)0x2000U`, which is passed as a template parameter to the `GPIO<PORT, PIN>` class and is directly written into the Pin field of `GPIO_InitTypeDef` in the `setup()` method.

### The Mode Field: Deciding the Pin's Soul

If the Pin field answers the question "which pin to configure," then the Mode field answers "what this pin is used for." Mode is the most complex of the four fields because it covers not just simple input and output, but also alternate functions and various interrupt modes.

In the HAL library, the available values for Mode are a series of predefined macros. Here is the complete list, which we re-wrapped using `enum class` in `gpio.hpp`:

```cpp
enum class Mode : uint32_t {
    Input = GPIO_MODE_INPUT,           // 0x00  输入模式
    OutputPP = GPIO_MODE_OUTPUT_PP,    // 0x01  推挽输出
    OutputOD = GPIO_MODE_OUTPUT_OD,    // 0x11  开漏输出
    AfPP = GPIO_MODE_AF_PP,            // 0x02  复用推挽
    AfOD = GPIO_MODE_AF_OD,            // 0x12  复用开漏
    AfInput = GPIO_MODE_AF_INPUT,      //       复用输入
    Analog = GPIO_MODE_ANALOG,         // 0x03  模拟模式
    ItRising = GPIO_MODE_IT_RISING,    //       上升沿中断
    ItFalling = GPIO_MODE_IT_FALLING,  //       下降沿中断
    ItRisingFalling = GPIO_MODE_IT_RISING_FALLING,  // 双边沿中断
    EvtRising = GPIO_MODE_EVT_RISING,  //       上升沿事件
    EvtFalling = GPIO_MODE_EVT_FALLING,  //     下降沿事件
    EvtRisingFalling = GPIO_MODE_EVT_RISING_FALLING,  // 双边沿事件
};
```

These values might look like scattered integers, but they actually follow the encoding rules defined by the STM32F1 series registers. The GPIO configuration registers (CRL and CRH) of the STM32F1 allocate 4 configuration bits for each pin, where the upper 2 bits are CNF (configuration) and the lower 2 bits are MODE. To express these configurations uniformly at the software level, the HAL library designed its own encoding scheme, which is then converted internally within `HAL_GPIO_Init()`.

For our LED project, we chose `GPIO_MODE_OUTPUT_PP`, which is push-pull output mode. Push-pull output means there are two MOSFETs inside the pin working alternately—one responsible for pulling the level high, and the other for pulling it low. This structure can actively drive both high and low levels with relatively strong drive capability, making it the most commonly used general-purpose output mode. In contrast, there is open-drain output (`GPIO_MODE_OUTPUT_OD`), which only has the ability to pull low; to output a high level, an external pull-up resistor is required. Open-drain output is typically used for I2C communication or scenarios requiring wired-OR logic—completely unnecessary overkill for LED control.

### The Pull Field: That Silent Resistor

The Pull field controls the internal pull-up and pull-down resistors of the pin. Every GPIO pin on the STM32 integrates a pull-up resistor and a pull-down resistor internally, which can be enabled via software. These three optional values are very simple:

```cpp
enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,     // 0x00  不使用上下拉
    PullUp = GPIO_PULLUP,     // 0x01  内部上拉
    PullDown = GPIO_PULLDOWN, // 0x02  内部下拉
};
```

What is the purpose of pull-up and pull-down resistors? When a pin is configured in input mode, if the external signal source is in a high-impedance state (neither pulling high nor pulling low), the pin's level is undefined and will randomly fluctuate with environmental noise. In scenarios like button detection, this leads to severe false triggers. Connecting a pull-up resistor allows the pin to stably maintain a high level when there is no external drive; connecting a pull-down resistor keeps it at a low level.

But for our LED project, PC13 is configured in push-pull output mode. In output mode, the pin actively drives the level, so pull-up and pull-down resistors are useless. In fact, the PC13 pin on the STM32F103 has special design limitations—it belongs to the RTC domain, has weaker drive capability, and doesn't fully support internal pull-up/pull-down functions. So we choose `GPIO_NOPULL`, which is both correct and hassle-free.

### The Speed Field: Faster Isn't Always Better

The Speed field is probably the most easily misunderstood of the four. It controls the toggle speed of the GPIO pin's output signal—that is, the steepness of the edge when the level changes from low to high or from high to low.

```cpp
enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,     // 0x00  2MHz
    Medium = GPIO_SPEED_FREQ_MEDIUM, // 0x01  10MHz
    High = GPIO_SPEED_FREQ_HIGH,   // 0x03  50MHz
};
```

Note the values here: Low is 0x00, Medium is 0x01, but High is not 0x02—it's 0x03. This is not a typo, but is dictated by the STM32F1 series register encoding. In the MODE bits of CRL/CRH, `00` means input, `01` means 10MHz output, `10` means 2MHz output, and `11` means 50MHz output. The HAL library did a mapping when wrapping these, making the macro names more intuitive, but the underlying values still follow the hardware encoding.

A common misconception is that "choosing the fastest speed is always safe." This is not the case. The faster the GPIO toggle speed, the steeper the output signal's edges, the greater the high-frequency harmonic components, and the more severe the electromagnetic interference (EMI). If your LED only needs to toggle once every 500 milliseconds, the signal frequency is just 1Hz—driving it at 50MHz is completely overkill. Not only does it waste energy, but it also generates unnecessary noise on the PCB. So choosing `GPIO_SPEED_FREQ_LOW` (2MHz) for LED control is more than sufficient.

Interestingly, in the LED constructor of `led.hpp`, we actually passed `Base::Speed::Low`:

```cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
```

But in the `setup()` method signature of `gpio.hpp`, the default value for Speed is `Speed::High`:

```cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
```

This default value is set to High because for most GPIO use cases, high-speed output is the most common requirement. LEDs are an exception, which is why the LED constructor explicitly specifies Low.

## In Practice: Step by Step, Configuring PC13 as Push-Pull Output

Enough theory—now let's string together the knowledge above and walk through the complete configuration process. We'll write it using the most raw HAL calls so that every step is clearly visible.

### Step 1: Enable the Clock

```c
__HAL_RCC_GPIOC_CLK_ENABLE();
```

Content we covered in the previous article. When this macro expands, it writes a 1 to bit 4 (the IOPCEN bit) of the RCC's APB2ENR register, connecting the clock to the GPIOC port. Without this step, all subsequent configuration operations are wasted effort—the registers simply won't respond to writes.

In our project, this step is encapsulated in the `GPIOClock::enable_target_clock()` method of the `GPIO` class:

```cpp
static inline void enable_target_clock() {
    if constexpr (PORT == GpioPort::C) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    // ... 其他端口的分支
}
```

`if constexpr` ensures the compiler only generates code corresponding to the actual port, discarding all other branches at compile time.

### Step 2: Define and Initialize the Configuration Struct

```c
GPIO_InitTypeDef g = {0};
```

This line might look unremarkable, but it hides a subtle trick. `GPIO_InitTypeDef g` allocates 16 bytes on the stack to hold the four `uint32_t` fields. If we just declared it without initializing, the contents of these 16 bytes would be leftover garbage values on the stack—data left behind from a previous function call, or completely unpredictable random numbers.

⚠️ The trap here is very well-hidden: if the Speed field happens to be a non-zero garbage value, `HAL_GPIO_Init()` will faithfully write it into the MODE bits of the CRH register. You might have no idea what speed the pin was configured to, because that value wasn't in your expectations at all. What's worse is that this problem is almost impossible to reproduce during debugging—because the garbage values on the stack can be different each time the program runs. Sometimes it happens to be zero and everything is fine; sometimes it isn't zero and things break. It's a classic "Schrödinger's Bug."

The appearance of `= {0}` is precisely to eliminate this uncertainty. It sets all bytes in the struct to zero, so all four fields start from zero. This way, even if you forget to set a certain field, it won't be a random value but a safe default—Mode of 0 is input mode, Pull of 0 is no pull-up/pull-down, and Speed of 0 is low speed. There will be no unexpected behavior.

### Step 3: Fill in the Configuration Field by Field

```c
g.Pin = GPIO_PIN_13;              // 选中PC13
g.Mode = GPIO_MODE_OUTPUT_PP;     // 推挽输出
g.Pull = GPIO_NOPULL;             // 无上下拉
g.Speed = GPIO_SPEED_FREQ_LOW;    // 2MHz低速
```

Four lines of code, four fields, each corresponding to the content we analyzed in detail earlier. Read together, they mean: please configure PC13 as push-pull output mode, without internal pull-up or pull-down resistors, at an output speed of 2MHz.

There is a detail worth noting here: in our `GPIO` template class, Pin is passed in as a template parameter rather than a function parameter. This means the value of Pin is already determined at compile time:

```cpp
template <GpioPort PORT, uint16_t PIN> class GPIO {
    void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
        GPIO_InitTypeDef init_types{};
        init_types.Pin = PIN;  // PIN是模板参数，编译期常量
        init_types.Mode = static_cast<uint32_t>(gpio_mode);
        init_types.Pull = static_cast<uint32_t>(pull_push);
        init_types.Speed = static_cast<uint32_t>(speed);
        HAL_GPIO_Init(native_port(), &init_types);
    }
};
```

`static_cast<uint32_t>(gpio_mode)` converts the value of our custom `enum class Mode` back to the `uint32_t` integer that the HAL library expects. This design maintains type safety (you can't accidentally pass a Pull value to the Mode parameter—the compiler will error out) while seamlessly interfacing with the HAL library's C API.

### Step 4: Commit the Configuration

```c
HAL_GPIO_Init(GPIOC, &g);
```

This line is the climax of the entire configuration process. After it is called, `HAL_GPIO_Init()` performs the following operations:

First, it iterates through the 16 bits in the Pin field, finding all bits with a value of 1. For `GPIO_PIN_13`, only bit 13 is 1.

Then, it determines which register the pin's configuration bits reside in based on the pin number. The STM32F1 rule is: Pin 0 through Pin 7 are in CRL (Port Configuration Low Register), and Pin 8 through Pin 15 are in CRH (Port Configuration High Register). PC13's number is 13, which is greater than 7, so its configuration is in CRH.

Each pin occupies 4 configuration bits in CRH. For Pin 13, these 4 bits are bits 20 through 23 of CRH (`bit[23:20]`). `HAL_GPIO_Init()` first clears these 4 bits to zero—erasing the previous configuration—and then fills in the new configuration based on the Mode and Speed values.

Specifically for our configuration: Mode is push-pull output (CNF=00), Speed is 2MHz (MODE=10), so the 4-bit value filled into CRH is `0010`, which is binary `0010`. `HAL_GPIO_Init()` internally reads the current value of CRH, uses a mask to clear bits 20 through 23, ORs in the new 4-bit value, and finally writes it back to CRH.

If the Pull field is not `GPIO_NOPULL`, the function will also additionally manipulate the corresponding bit in the ODR (Port Output Data Register). Pull-up corresponds to setting the ODR bit, and pull-down corresponds to clearing the ODR bit. However, our Pull here is `GPIO_NOPULL`, so this step is skipped.

After this series of operations, PC13 transforms from "floating input" to "2MHz push-pull output." It is now ready to receive our instructions to output high and low levels.

## The True Face of GPIO_PIN_13: Tracing a Macro's Journey

Let's temporarily step away from the application layer and trace the complete path of the `GPIO_PIN_13` macro from definition to use, seeing how it step by step becomes a tangible signal change on the chip.

The story begins in the HAL library's header file `stm32f1xx_hal_gpio.h`. There, we find this line of definition:

```c
#define GPIO_PIN_13  ((uint16_t)0x2000U)
```

`0x2000`, which converts to binary as `0010 0000 0000 0000`. Counting from the right, bit 13 is 1, and all the rest are 0. The meaning of this number is very straightforward: in a 16-bit bitmap, the 13th position is marked. And since a GPIO port happens to have exactly 16 pins (Pin 0 through Pin 15), each bit in this bitmap corresponds to one pin.

Why does the HAL library go to such lengths to use a bitmask instead of a simple integer ID? The answer lies in efficiency. In embedded development, we frequently need to manipulate multiple pins simultaneously—lighting two LEDs at the same time, reading the state of four buttons at once. If the Pin field were just an integer, you could only operate on one pin at a time, requiring a loop to handle multiple pins. With a bitmask, a single call can process multiple pins, because bitwise OR operations naturally support multi-selection:

```c
// 同时配置Pin 0和Pin 13
GPIO_InitTypeDef g = {0};
g.Pin = GPIO_PIN_0 | GPIO_PIN_13;  // 0x0001 | 0x2000 = 0x2001
g.Mode = GPIO_MODE_OUTPUT_PP;
g.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &g);
```

The value `0x2001` marks both bit 0 and bit 13 simultaneously. Internally, `HAL_GPIO_Init()` uses a for loop scanning from 0 to 15, checking for each bit whether `Pin & (1 << i)` is non-zero, and configuring that pin if it is. The bitwise operations of bitmasks naturally align with the bit structure of hardware registers—checking, setting, and clearing are all just a single bitwise instruction, which is an incredibly valuable efficiency advantage on a Cortex-M3 with no MMU and no cache.

In our C++ wrapper, `GPIO_PIN_13` is passed as a template non-type parameter:

```cpp
template <GpioPort PORT, uint16_t PIN> class GPIO { ... };
```

The template parameter `PIN` is bound to a specific value at compile time. When the compiler instantiates `GPIO<GpioPort::C, GPIO_PIN_13>`, it replaces all occurrences of `PIN` with `(uint16_t)0x2000U`. This means there is zero additional lookup or calculation overhead at runtime—the code after template instantiation has exactly the same effect as hand-writing `0x2000`, but the expressiveness of the code is improved by more than an order of magnitude.

## Aggregate Initialization: The Past and Present of {0} and {}

Earlier, when initializing the configuration struct, we mentioned using `= {0}`. It's worth diving deeper into this topic here, because it involves subtle differences in initialization between the C and C++ languages, and in embedded development, this difference is real—both styles appear simultaneously in our code.

First, the C style, which appears in `clock.cpp`:

```c
RCC_OscInitTypeDef osc = {0};
RCC_ClkInitTypeDef clk = {0};
```

`= {0}` is C's aggregate initialization syntax. Its meaning is: initialize the first field of the struct to 0, and if the remaining fields are not explicitly given initialization values, automatically initialize them to zero (for integer types that's 0, for pointers it's NULL, for floating-point it's 0.0). This rule is clearly specified in the C89/C99 standards, so using `{0}` to initialize a struct results in all fields being zeroed out—safe and reliable.

Now, the C++ style, which appears in `gpio.hpp`:

```cpp
GPIO_InitTypeDef init_types{};
```

No equals sign, no 0 inside the braces, just an empty pair of braces. This is the value initialization syntax introduced in C++11. For aggregate types (like C-style structs), its effect is exactly the same as `= {0}`—all fields are initialized to zero. But its semantics are more universal: for non-aggregate types (like classes with custom constructors), `{}` calls the default constructor; for scalar types, `{}` initializes to zero. `{}` is the standard C++ way of writing this, expressing "please initialize this object to a clean default state in the most reasonable way."

So why do both styles appear in our project? The reason is simple: the `RCC_OscInitTypeDef` and `RCC_ClkInitTypeDef` in `clock.cpp` are C structs defined by the HAL library, so initializing them with `= {0}` better fits the reading habits of C programmers and makes the code's intent more explicit—"I am zeroing this out." Using `{}` in `gpio.hpp`, on the other hand, is because this is C++ code, and using C++'s modern initialization syntax is more natural and consistent with our project's overall C++ style.

Both approaches are completely correct and safe choices in embedded development. There is no question of which is superior; it's only a matter of style preference. If you interact with C code a lot, `= {0}` is more intuitive; if you're immersed in the C++ world, `{}` is more uniform. The only thing you need to avoid is writing nothing at all—`GPIO_InitTypeDef g;` in a local scope does not perform initialization, leaving behind random garbage values on the stack, which is the breeding ground for all sorts of bizarre bugs.

⚠️ By the way, there's another way to write it: `GPIO_InitTypeDef g = {};` (empty braces with an equals sign in C++). This is also legal in C++ and has the same effect as `GPIO_InitTypeDef g{};`. One equals sign more or less is purely a personal preference. But if you write `GPIO_InitTypeDef g = {0};`, some particularly strict C++ compilers might issue warnings about "signed/unsigned conversion" or "narrowing conversion," because `0` is an int while the struct fields might be uint32_t. However, for mainstream embedded compilers (ARM GCC, IAR, etc.), this situation won't trigger warnings, so you can use it with confidence.

## The Ritual Is Complete, the Pin Is in Position

At this point, we have dissected every detail of `HAL_GPIO_Init()`. From the meaning of the four fields in `GPIO_InitTypeDef`, to the design philosophy of bitmasks, to the function's internal bit manipulation of the CRH register, to the choice of initialization style—none of these steps came out of nowhere; each is the result of careful consideration by the chip designers and library developers.

Looking back at what our C++ wrapper does in `setup()`: it packages clock enabling, struct initialization, field assignment, and the HAL call into a clean method call. External users only need to write one line:

```cpp
Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
```

All the details behind the scenes are properly handled. This is the true meaning of abstraction—not hiding complexity (because as embedded developers, you must understand the underlying hardware), but making the complexity surface only when it is needed.

PC13 is now configured and quietly awaiting instructions. In the next article, we will make this pin move—through `HAL_GPIO_WritePin()` and `HAL_GPIO_TogglePin()`, we will make the LED turn on, turn off, and turn on again. We will see that once the pin configuration is complete, controlling the high and low levels is actually an exceptionally simple task.
