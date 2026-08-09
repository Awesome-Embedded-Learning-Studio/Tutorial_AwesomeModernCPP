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
description: ''
translation:
  source: documents/vol8-domains/embedded/01-led/05-hal-gpio-init.md
  source_hash: fdb9a615a63872f1dd7ab226810d9fd31511f4d77d830dfe9f6bae44997b426d
  translated_at: '2026-06-16T04:09:35.095902+00:00'
  engine: anthropic
  token_count: 3069
---
# Part 10: HAL_GPIO_Init — The Ritual of Configuring Pins

## Prologue: The Pin is Awake, But It Doesn't Know What to Do

In the previous post, we finally pushed open the gate to the clock. Once the ``__HAL_RCC_GPIOC_CLK_ENABLE()`` macro executes, the GPIOC port wakes from its slumber, and its registers begin responding to read and write requests on the bus. We used an analogy: enabling the clock is like connecting a factory to the power grid; the machines now have the prerequisite condition to run. But power on doesn't mean production starts—every machine still needs someone to tell it what to produce, at what rhythm to operate, and what the safety standards are.

The same logic applies to GPIO pins. After the clock is enabled, the pin's seven registers (CRL, CRH, IDR, ODR, BSRR, BRR, LCKR) all become writable, but they still hold the default values from reset. For PC13, the default values in CRL and CRH after reset are ``0x44444444``, which means each pin is configured as "floating input" mode. In other words, PC13 is currently like a pedestrian standing at a crossroads, looking around blankly, not knowing which way to go.

We need to explicitly tell it: you should act as a push-pull output, toggle at 2MHz, and require no pull-up or pull-down resistors. The way we deliver this "appointment letter" to the chip is by calling ``HAL_GPIO_Init()``. This function is a contract between us and the hardware—we pack all our expectations for the pin into a structure, and it is responsible for translating those expectations into register configuration values bit by bit, writing them to the corresponding memory-mapped addresses. In today's article, we will dissect every clause of this contract to understand exactly what is happening behind every line of code.

## GPIO_InitTypeDef: A Carefully Designed Configuration Checklist

Let's first look at the function signature of ``HAL_GPIO_Init()``:

````c
void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
````

Two parameters: one pointing to the port, one pointing to the configuration. It couldn't be more concise. But beneath this simplicity lies a wealth of details worth digging into.

### First Parameter: GPIO_TypeDef *GPIOx

``GPIOx`` is a pointer to the ``GPIO_TypeDef`` structure. In the memory map of the STM32F103C8T6, each GPIO port occupies a contiguous address space, and ``GPIO_TypeDef`` is the structured description of that space. The base address of GPIOA is ``0x40010800``, GPIOB is ``0x40010C00``, and GPIOC is ``0x40011000``—each port is separated by ``0x400`` bytes, or 1KB of space. Of this 1KB, only seven 32-bit registers (28 bytes) are actually used; the rest is reserved.

In our ``gpio.hpp``, we used ``enum class GpioPort`` to wrap these base addresses into type-safe enum values:

````cpp
enum class GpioPort : uintptr_t {
    A = GPIOA_BASE,
    B = GPIOB_BASE,
    C = GPIOC_BASE,
    D = GPIOD_BASE,
    E = GPIOE_BASE,
};
````

And in the ``native_port()`` method of the ``GPIO`` class, we convert this enum value back to the ``GPIO_TypeDef*`` pointer expected by the HAL library via ``reinterpret_cast``:

````cpp
static constexpr GPIO_TypeDef* native_port() noexcept {
    return reinterpret_cast<GPIO_TypeDef*>(static_cast<uintptr_t>(PORT));
}
````

This layer of conversion might seem redundant at first glance—why not just use the ``GPIOC`` macro directly? Because C++'s type system doesn't allow us to treat an integer directly as a pointer. Although the underlying value of ``GpioPort::C`` is the integer ``GPIOC_BASE``, in the C++ type system it is a ``GpioPort`` enum value and cannot be implicitly converted to a pointer. We need to first cast it to ``uintptr_t`` (an integer type large enough to hold a pointer), and then use ``reinterpret_cast`` to tell the compiler "please treat this integer as a pointer." The benefit of this is that at the template parameter level, ``GpioPort`` is a real type, and the compiler can help us check at compile time whether a valid port value was passed.

### Second Parameter: GPIO_InitTypeDef *GPIO_Init

This is the real protagonist of today. ``GPIO_InitTypeDef`` is a structure with only four fields, but these four fields determine all behavioral characteristics of a pin:

````c
typedef struct {
    uint32_t Pin;    // 引脚编号
    uint32_t Mode;   // 工作模式
    uint32_t Pull;   // 上下拉配置
    uint32_t Speed;  // 输出速度
} GPIO_InitTypeDef;
````

Four ``uint32_t``s, sixteen bytes, and the "personality" of a pin is fully defined. Let's break them down one by one.

### The Pin Field: Selecting Your Pin with a Bitmask

The usage of the Pin field might feel a bit strange when you first encounter it—it's not a simple number (like ``13``), but a bitmask (like ``0x2000``). In the HAL library header file, the sixteen pins are defined like this:

````c
#define GPIO_PIN_0   ((uint16_t)0x0001U)  // 0000 0000 0000 0001
#define GPIO_PIN_1   ((uint16_t)0x0002U)  // 0000 0000 0000 0010
#define GPIO_PIN_2   ((uint16_t)0x0004U)  // 0000 0000 0000 0100
#define GPIO_PIN_3   ((uint16_t)0x0008U)  // 0000 0000 0000 1000
// ... 以此类推，每一位对应一个引脚
#define GPIO_PIN_13  ((uint16_t)0x2000U)  // 0010 0000 0000 0000
#define GPIO_PIN_14  ((uint16_t)0x4000U)  // 0100 0000 0000 0000
#define GPIO_PIN_15  ((uint16_t)0x8000U)  // 1000 0000 0000 0000
#define GPIO_PIN_ALL ((uint16_t)0xFFFFU)  // 1111 1111 1111 1111
````

If you are sensitive to binary, you will see the pattern immediately: the essence of ``GPIO_PIN_n`` is ``(1 << n)``, which shifts ``1`` left by *n* bits. ``GPIO_PIN_0`` has bit 0 set to 1, ``GPIO_PIN_13`` has bit 13 set to 1, a perfect one-to-one correspondence. This is no coincidence, but a carefully designed encoding scheme. Each pin occupies a unique bit in a 16-bit integer, and the pin number is the bit position.

This bitmask design brings a direct benefit: you can configure multiple pins at once using a bitwise OR operation. For example, if you want to configure PA0 and PA5 simultaneously, you just write ``GPIO_PIN_0 | GPIO_PIN_5``, which results in ``0x0021``, where both bit 0 and bit 5 are 1. Internally, ``HAL_GPIO_Init()`` uses a loop to scan these 16 bits; wherever a bit is 1, it configures that pin. This is extremely useful when batch-initializing multiple pins—one call handles it all, no need to write sixteen separate calls.

In our project, the LED is connected to PC13, so we pass in ``GPIO_PIN_13``. It is worth noting that in ``main.cpp``, we directly use the HAL library macro:

````cpp
device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
````

This ``GPIO_PIN_13`` macro expands to ``(uint16_t)0x2000U``, which is passed as a template parameter to the ``GPIO<PORT, PIN>`` class and is directly written into the Pin field of ``GPIO_InitTypeDef`` in the ``setup()`` method.

### The Mode Field: Determining the Soul of the Pin

If the Pin field answers the question "which pin to configure," the Mode field answers "what this pin is used for." Mode is the most complex of the four fields because it covers not just simple input/output, but also alternate functions and various interrupt modes.

In the HAL library, the available values for Mode are a series of predefined macros. Here is the complete list we re-wrapped in ``gpio.hpp`` using ``enum class``:

````cpp
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
````

These values look like scattered integers, but they actually follow the encoding rules of the STM32F1 series register definitions. The STM32F1 GPIO configuration registers (CRL and CRH) allocate 4 configuration bits for each pin, where the upper 2 bits are configuration (CNF) and the lower 2 bits are mode (MODE). To express these configurations uniformly at the software level, the HAL library designed its own encoding scheme and then performs the conversion inside ``HAL_GPIO_Init()``.

For our LED project, we chose ``GPIO_MODE_OUTPUT_PP``, which is the push-pull output mode. Push-pull output means there are two MOS transistors working alternately inside the pin—one responsible for pulling the level high, one for pulling it low. This structure can actively drive both high and low levels with relatively strong driving capability, making it the most common general-purpose output mode. In contrast is open-drain output (``GPIO_MODE_OUTPUT_OD``), which only has the ability to pull down; to output a high level, an external pull-up resistor is required. Open-drain is typically used for I2C communication or scenarios requiring wired-OR logic; controlling an LED doesn't need such complexity.

### The Pull Field: That Silent Resistor

The Pull field controls the internal pull-up and pull-down resistors of the pin. Each GPIO pin on the STM32 integrates a pull-up resistor and a pull-down resistor that can be enabled via software. These three optional values are simple:

````cpp
enum class PullPush : uint32_t {
    NoPull = GPIO_NOPULL,     // 0x00  不使用上下拉
    PullUp = GPIO_PULLUP,     // 0x01  内部上拉
    PullDown = GPIO_PULLDOWN, // 0x02  内部下拉
};
````

What is the purpose of pull-up/pull-down resistors? When a pin is configured as an input mode, if the external signal source is in a high-impedance state (neither pulling high nor low), the pin level is undefined and will fluctuate randomly with environmental noise. In scenarios like button detection, this leads to serious false triggers. Connecting a pull-up resistor ensures the pin stably holds a high level when there is no external drive; connecting a pull-down resistor keeps it low.

However, for our LED project, PC13 is configured as a push-pull output. In output mode, the pin actively drives the level, so pull-up/pull-down resistors are useless. In fact, the PC13 pin on the STM32F103 has design limitations—it is an RTC domain pin with weaker driving capability and its internal pull-up/pull-down functionality is not fully supported. So we choose ``GPIO_NOPULL``, which is both correct and saves trouble.

### The Speed Field: Faster Isn't Always Better

The Speed field is likely the most misunderstood of the four. It controls the toggling speed of the GPIO pin when outputting signals, that is, the steepness of the edge when the level transitions from low to high or high to low.

````cpp
enum class Speed : uint32_t {
    Low = GPIO_SPEED_FREQ_LOW,     // 0x00  2MHz
    Medium = GPIO_SPEED_FREQ_MEDIUM, // 0x01  10MHz
    High = GPIO_SPEED_FREQ_HIGH,   // 0x03  50MHz
};
````

Notice the values here: Low is 0x00, Medium is 0x01, but High is not 0x02, it is 0x03. This isn't a typo, but is determined by the STM32F1 series register encoding. In the MODE bits of CRL/CRH, ``00`` means input, ``01`` means 10MHz output, ``10`` means 2MHz output, and ``11`` means 50MHz output. The HAL library did a mapping when encapsulating to make the macro names more intuitive, but the underlying values still follow the hardware encoding.

A common misconception is "choosing the fastest speed is always right." Not true. The faster the GPIO toggles, the steeper the output signal edge, the greater the high-frequency harmonic components, and the more severe the electromagnetic interference (EMI). If your LED only needs to toggle once every 500 milliseconds, the signal frequency is only 1Hz. Driving it with 50MHz speed is overkill—it wastes energy and generates unnecessary noise on the board. So choosing ``GPIO_SPEED_FREQ_LOW`` (2MHz) for LED control is more than sufficient.

Interestingly, in the LED constructor of ``led.hpp``, we did pass ``Base::Speed::Low``:

````cpp
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}
````

But in the ``setup()`` method signature of ``gpio.hpp``, Speed's default value is ``Speed::High``:

````cpp
void setup(Mode gpio_mode, PullPush pull_push = PullPush::NoPull, Speed speed = Speed::High) {
````

This default is set to High because for most GPIO uses, high-speed output is the common need. The LED is an exception, so we explicitly specified Low in the LED constructor.

## In Practice: Configuring PC13 as Push-Pull Output Step by Step

Enough theory, now let's string the above knowledge together and walk through the configuration process completely. We'll write it using the raw HAL calls so every step is clearly visible.

### Step 1: Enable the Clock

````c
__HAL_RCC_GPIOC_CLK_ENABLE();
````

Content covered in the last post. When this macro expands, it writes a 1 to bit 4 (IOPCEN) of the RCC's APB2ENR register, turning on the GPIOC port clock. Without this step, all subsequent configuration operations are playing the lute to a cow—the registers simply won't respond to writes.

In our project, this step is encapsulated in the ``GPIOClock::enable_target_clock()`` method of the ``GPIO`` class:

````cpp
static inline void enable_target_clock() {
    if constexpr (PORT == GpioPort::C) {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    // ... 其他端口的分支
}
````

``if constexpr`` ensures the compiler only generates code corresponding to the actual port; other branches are discarded at compile time.

### Step 2: Define and Initialize the Configuration Structure

````c
GPIO_InitTypeDef g = {0};
````

This line looks plain, but it hides a mystery. ``GPIO_InitTypeDef g`` allocates 16 bytes on the stack to store four ``uint32_t`` fields. If declared like this without initialization, the content of these 16 bytes is garbage left on the stack—data left over from the last function call, or completely unpredictable random numbers.

⚠️ The trap here is very subtle: if the Speed field happens to be a non-zero garbage value, ``HAL_GPIO_Init()`` will faithfully write it into the MODE bits of the CRH register. You might have no idea what speed the pin was configured to, because that value wasn't in your expectations. Worse, this problem is almost impossible to reproduce during debugging—because garbage values on the stack can vary with each run; sometimes it happens to be zero and it's fine, sometimes it's not and it breaks. A classic "Schrödinger's Bug."

``= {0}`` exists to eliminate this uncertainty. It sets all bytes in the structure to zero, so all four fields start from zero. This way, even if you forget to set a field, it won't be a random value, but a safe default—Mode 0 is input mode, Pull 0 is no pull-up/down, Speed 0 is low speed. No unexpected behavior.

### Step 3: Fill in the Configuration Field by Field

````c
g.Pin = GPIO_PIN_13;              // 选中PC13
g.Mode = GPIO_MODE_OUTPUT_PP;     // 推挽输出
g.Pull = GPIO_NOPULL;             // 无上下拉
g.Speed = GPIO_SPEED_FREQ_LOW;    // 2MHz低速
````

Four lines of code, four fields, each corresponding to the content we analyzed in detail earlier. Read together, they mean: please configure PC13 as push-pull output mode, no internal pull-up/pull-down resistors, output speed 2MHz.

Here is a detail worth noting: in our ``GPIO`` template class, Pin is passed via a template parameter, not a function parameter. This means the value of Pin is already determined at compile time:

````cpp
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
````

``static_cast<uint32_t>(gpio_mode)`` converts the value of our custom ``enum class Mode`` back to the ``uint32_t`` integer expected by the HAL library. This design maintains type safety (you can't accidentally pass a Pull value to the Mode parameter, the compiler will error) while seamlessly interfacing with the HAL library's C interface.

### Step 4: Submit the Configuration

````c
HAL_GPIO_Init(GPIOC, &g);
````

This line is the climax of the entire configuration process. After the call, ``HAL_GPIO_Init()`` performs the following operations:

First, it iterates through the 16 bits in the Pin field, finding all bits set to 1. For ``GPIO_PIN_13``, only bit 13 is 1.

Then, it determines which register the pin's configuration bits reside in based on the pin number. The STM32F1 rule is: Pin 0 to Pin 7 are in CRL (Port Configuration Low Register), Pin 8 to Pin 15 are in CRH (Port Configuration High Register). PC13's number is 13, which is greater than 7, so its configuration is in CRH.

Each pin occupies 4 configuration bits in CRH. For Pin 13, these 4 bits are bits 20 to 23 of CRH (``bit[23:20]``). ``HAL_GPIO_Init()`` first clears these 4 bits—erasing the previous configuration—and then fills in the new configuration based on the Mode and Speed values.

Specifically for our configuration: Mode is push-pull output (CNF=00), Speed is 2MHz (MODE=10), so the 4-bit value filled into CRH is ``0010``, which is binary ``0010``. ``HAL_GPIO_Init()`` internally reads the current value of CRH, uses a mask to clear bits 20 to 23, ORs the new 4-bit value in, and writes it back to CRH.

If the Pull field is not ``GPIO_NOPULL``, the function also performs an extra operation on the corresponding bit of ODR (Output Data Register). Pull-up corresponds to setting the ODR bit, pull-down corresponds to clearing it. However, since our Pull is ``GPIO_NOPULL``, this step is skipped.

After this operation, PC13 changes from "floating input" to "2MHz push-pull output." It is now ready to receive our instructions to output high and low levels.

## The True Face of GPIO_PIN_13: Tracing a Macro's Journey

Let's temporarily step away from the application layer and trace the full path of the ``GPIO_PIN_13`` macro from definition to use, seeing how it step-by-step becomes a tangible signal change on the chip.

The story begins in the HAL library header file ``stm32f1xx_hal_gpio.h``. There, we find this line of definition:

````c
#define GPIO_PIN_13  ((uint16_t)0x2000U)
````

``0x2000``, which converts to binary ``0010 0000 0000 0000``. Counting from the right, bit 13 is 1, the rest are all 0. The meaning of this number is very straightforward: in a 16-bit bitmap, the 13th position is marked. Since a GPIO port has exactly 16 pins (Pin 0 to Pin 15), each bit in this bitmap corresponds to one pin.

Why does the HAL library go to such trouble to use bitmasks instead of simple integer numbers? The answer lies in efficiency. In embedded development, we often need to manipulate multiple pins simultaneously—lighting two LEDs at once, reading the status of four buttons. If the Pin field were just an integer, we could only operate on one pin at a time, requiring a looped call to operate on multiple. With bitmasks, one call handles multiple pins because bitwise OR operations naturally support multi-select:

````c
// 同时配置Pin 0和Pin 13
GPIO_InitTypeDef g = {0};
g.Pin = GPIO_PIN_0 | GPIO_PIN_13;  // 0x0001 | 0x2000 = 0x2001
g.Mode = GPIO_MODE_OUTPUT_PP;
g.Speed = GPIO_SPEED_FREQ_LOW;
HAL_GPIO_Init(GPIOC, &g);
````

The value ``0x2001`` marks both bit 0 and bit 13. Inside ``HAL_GPIO_Init()``, a for loop scans from 0 to 15, checking if ``Pin & (1 << i)`` is non-zero for each bit; if non-zero, it configures that pin. The bitwise operations of the bitmask naturally align with the bit structure of hardware registers—checking, setting, and clearing are all single bitwise instructions, which is a precious efficiency advantage on a Cortex-M3 without MMU or cache.

In our C++ encapsulation, ``GPIO_PIN_13`` is passed as a template non-type parameter:

````cpp
template <GpioPort PORT, uint16_t PIN> class GPIO { ... };
````

The template parameter ``PIN`` is bound to a specific value at compile time. When the compiler instantiates ``GPIO<GpioPort::C, GPIO_PIN_13>``, it replaces all ``PIN`` with ``(uint16_t)0x2000U``. This means there is no extra table lookup or calculation overhead at runtime—the code after template instantiation is exactly the same as hand-writing ``0x2000``, but the expressiveness of the code is enhanced by more than an order of magnitude.

## Aggregate Initialization: The Past and Present of {0} and {}

Earlier when configuring the structure, we mentioned using ``= {0}`` for initialization. It's worth expanding on this topic, as it touches on subtle differences between C and C++ regarding initialization, and in embedded development, this difference is real—our code contains both styles.

First, the C style, appearing in ``clock.cpp``:

````c
RCC_OscInitTypeDef osc = {0};
RCC_ClkInitTypeDef clk = {0};
````

``= {0}`` is C's aggregate initialization syntax. Its meaning is: initialize the first field of the structure to 0, and if the remaining fields are not explicitly given an initialization value, they are automatically initialized to zero (0 for integers, NULL for pointers, 0.0 for floats). This rule is clearly defined in the C89/C99 standards, so using ``{0}`` to initialize a structure results in all fields being zeroed out—safe and reliable.

Now, the C++ style, appearing in ``gpio.hpp``:

````cpp
GPIO_InitTypeDef init_types{};
````

No equals sign, no 0 inside the braces, just a pair of empty braces. This is the value initialization syntax introduced in C++11. For aggregate types (like C-style structs), its effect is identical to ``= {0}``—all fields are initialized to zero. But its semantics are more universal: for non-aggregate types (like classes with custom constructors), ``{}`` calls the default constructor; for scalar types, ``{}`` initializes to zero. ``{}`` is the standard C++ way of writing, expressing "please initialize this object to a clean default state in the most reasonable way."

So why do both styles appear in our project? The reason is simple: ``RCC_OscInitTypeDef`` and ``RCC_ClkInitTypeDef`` in ``clock.cpp`` are C structures defined by the HAL library, so using ``= {0}`` fits the C programmer's reading habit better and makes the code's intent more explicit—"I am zeroing this." Using ``{}`` in ``gpio.hpp`` is because this is C++ code, and using modern C++ initialization syntax is more natural and keeps the overall style of our project consistent.

Both styles are completely correct and safe choices in embedded development. There is no question of which is better, only differences in style preference. If you deal with C code a lot, ``= {0}`` is more intuitive; if you are immersed in the world of C++, ``{}`` is more unified. The only thing to avoid is writing nothing—``GPIO_InitTypeDef g;`` in local scope does not initialize, leaving random garbage on the stack, which is the breeding ground for all strange bugs.

⚠️ By the way, there is another style: ``GPIO_InitTypeDef g = {};`` (empty braces with an equals sign in C++). This is also legal in C++ and has the same effect as ``GPIO_InitTypeDef g{};``. One more equals sign or one less is purely personal preference. But if you write ``GPIO_InitTypeDef g = {0};``, some particularly strict C++ compilers might warn about "signed/unsigned conversion" or "narrowing conversion" because ``0`` is an int while the structure field might be uint32_t. However, for mainstream embedded compilers (ARM GCC, IAR, etc.), this won't trigger warnings, so feel free to use it.

## Ritual Complete, Pin in Place

At this point, we have dissected every detail of ``HAL_GPIO_Init()``. From the meaning of the four fields of ``GPIO_InitTypeDef``, to the design philosophy of bitmasks, to the bit operations on the CRH register inside the function, to the choice of initialization style—every step arises not from thin air, but from the careful consideration of chip designers and library developers.

Looking back at what our C++ encapsulation in ``setup()`` did: it packaged clock enabling, structure initialization, field assignment, and the HAL call into a clean method call. The external user only needs to write one line:

````cpp
Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
````

All the details behind the scenes are properly handled. This is the meaning of abstraction—not hiding complexity (because as an embedded developer, you must understand the underlying layer), but making complexity surface only when needed.

PC13 is now configured, quietly waiting for instructions. In the next post, we will make this pin move—through ``HAL_GPIO_WritePin()`` and ``HAL_GPIO_TogglePin()``, we will make the LED light up, turn off, and light up again. We will see that after the pin configuration is complete, controlling the level is surprisingly simple.
