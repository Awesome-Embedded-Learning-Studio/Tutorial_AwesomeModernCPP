---
chapter: 16
difficulty: intermediate
order: 10
platform: stm32f1
reading_time_minutes: 6
tags:
- cpp-modern
- intermediate
- stm32f1
title: 'Part 28: Button Template Class Design — Leave It All to the Compiler'
description: ''
translation:
  source: documents/vol8-domains/embedded/02-button/10-cpp-template-button.md
  source_hash: 03fcd681c1670e5da8e297382d34683a4546ee8e6813b936d2348bad097bcccf
  translated_at: '2026-06-16T04:11:15.914463+00:00'
  engine: anthropic
  token_count: 1492
---
# Part 28: Button Template Class Design — Delegate Everything to the Compiler

> Following the previous part: `std::variant` + `std::visit` handled event expression. In this part, we design a Button template class, encoding port, pin, pull-up/pull-down, and level polarity entirely into compile-time types.

---

## Template Parameters: Four-Dimensional Configuration

In the LED tutorial, the `LED` template accepted three parameters: `GpioPort`, `PIN`, and `ActiveLevel`. The Button template adds one more dimension:

```cpp
template <gpio::GpioPort PORT, uint16_t PIN,
          gpio::PullPush PULL = gpio::PullPush::PullUp,
          ButtonActiveLevel LEVEL = ButtonActiveLevel::Low>
class Button : public gpio::GPIO<PORT, PIN> {
```

| Parameter | Type | Default | Meaning |
|-----------|------|---------|---------|
| `PORT` | `GpioPort` enum | None (Required) | GPIO Port (A/B/C/D/E) |
| `PIN` | `uint16_t` | None (Required) | Pin Number (GPIO_PIN_0 ~ GPIO_PIN_15) |
| `PULL` | `PullPush` enum | `PullUp` | Pull-up/Pull-down Mode |
| `LEVEL` | `ButtonActiveLevel` enum | `Low` | Level Polarity When Pressed |

All four parameters have default values (except for `PORT` and `PIN`), so the most common usage is concise:

```cpp
// PA0 上拉输入，低电平有效 — 最常见的配置
device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;

// 如果需要下拉输入 + 高电平有效
device::Button<device::gpio::GpioPort::A, GPIO_PIN_0,
               device::gpio::PullPush::PullDown,
               device::ButtonActiveLevel::High> button;
```

### Comparison with the LED Template

```cpp
// LED 模板：PORT + PIN + ActiveLevel（输出模式）
template <gpio::GpioPort PORT, uint16_t PIN, ActiveLevel LEVEL = ActiveLevel::Low>
class LED : public gpio::GPIO<PORT, PIN>;

// Button 模板：PORT + PIN + PullPush + ButtonActiveLevel（输入模式）
template <gpio::GpioPort PORT, uint16_t PIN,
          gpio::PullPush PULL = gpio::PullPush::PullUp,
          ButtonActiveLevel LEVEL = ButtonActiveLevel::Low>
class Button : public gpio::GPIO<PORT, PIN>;
```

The structure is nearly identical—both inherit from `GPIO<PORT, PIN>` and use Non-Type Template Parameters (NTTP) to encode hardware configuration. Button adds a `PULL` parameter because input modes require an explicit pull direction, whereas output modes do not.

---

## static_assert: Compile-Time Defense

```cpp
static_assert(PIN <= GPIO_PIN_15, "Pin number must be <= 15");
```

`static_assert` checks at compile time whether a constant expression is true. If false, compilation terminates immediately, outputting your custom error message.

The values from `GPIO_PIN_0` to `GPIO_PIN_15` are `0x0001` to `0x8000` (each bit corresponds to a pin). `GPIO_PIN_15` is `0x8000` (bit 15 set). Any pin number exceeding this value is invalid—the STM32F103 has a maximum of 16 pins per GPIO port.

If you write:

```cpp
device::Button<device::gpio::GpioPort::A, 0xFFFF> button;  // 错误！
```

The compiler will immediately report an error:

```text
error: static assertion failed: Pin number must be <= 15
```

There is no need to wait until you flash the board to discover the wrong pin number. This is the value of compile-time defense.

⚠️ Note the position of `static_assert`—it is inside the class body, before `public`. This means it executes during template instantiation (when you write `Button<GpioPort::A, GPIO_PIN_0>`). Only template instances that are actually used will trigger the check.

---

## Constructor: Automatic Input Mode Configuration

```cpp
Button() {
    Base::setup(Base::Mode::Input, PULL, Base::Speed::Low);
}
```

Comparing with the LED constructor:

```cpp
// LED 构造函数
LED() {
    Base::setup(Base::Mode::OutputPP, Base::PullPush::NoPull, Base::Speed::Low);
}

// Button 构造函数
Button() {
    Base::setup(Base::Mode::Input, PULL, Base::Speed::Low);
}
```

Two differences:

1. `Mode::Input` replaces `Mode::OutputPP` — Input mode replaces push-pull output.
2. `PULL` replaces `PullPush::NoPull` — Pull-up/pull-down is determined by template parameters, not the hardcoded `NoPull`.

`setup()` internally does three things (deconstructed in LED Tutorial Part 09):

1. Calls `GPIOClock::enable_target_clock()` — Uses `if constexpr` to automatically select the port clock.
2. Fills the `GPIO_InitTypeDef` structure.
3. Calls `HAL_GPIO_Init()` to write to registers.

When the constructor is called, PA0 is configured as a pull-up input mode, and the GPIOA clock is automatically enabled. You don't need to remember "enable the clock before initializing"—the template handles it for you.

---

## is_pressed(): Compile-Time Branching

```cpp
bool is_pressed() const {
    auto state = Base::read_pin_state();
    if constexpr (LEVEL == ButtonActiveLevel::Low) {
        return state == Base::State::UnSet;
    } else {
        return state == Base::State::Set;
    }
}
```

This code uses the same `if constexpr` pattern as the LED's `on()`/`off()` methods:

```cpp
// LED::on()
void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}
```

But there is one difference: LED used the ternary operator `? :`, while Button uses `if constexpr`. The effect is identical—both select branches at compile time. `if constexpr` is semantically clearer, especially when the logic in both branches is more complex (e.g., the pressed branch does three things, the released branch does two).

For a `ButtonActiveLevel::Low` button (pull-up scheme, pressed=low level), the compiled `is_pressed()` is equivalent to:

```cpp
bool is_pressed() const {
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
}
```

One register read, one comparison. There is no runtime overhead of `if constexpr`—because the compiler generates no code for the unselected branch.

---

## Template Instantiation: Different Configurations = Different Types

```cpp
Button<GpioPort::A, GPIO_PIN_0>                        button1;  // PA0, 上拉, 低电平有效
Button<GpioPort::A, GPIO_PIN_0, PullPush::PullDown, ButtonActiveLevel::High>
                                                      button2;  // PA0, 下拉, 高电平有效
Button<GpioPort::B, GPIO_PIN_5>                        button3;  // PB5, 上拉, 低电平有效
```

`button1`, `button2`, and `button3` are three completely different types. The compiler generates a separate copy of the code for each unique combination of template parameters. The implementations of `button1::is_pressed()` and `button2::is_pressed()` differ—the former checks for a low level, the latter for a high level.

This is the "cost" of templates: increased compilation time and potentially increased code size (if there are many different instantiations). However, in embedded scenarios, there are usually only a few button configurations, so the code size increase is negligible. The benefit is compile-time type safety and zero runtime overhead.

---

## Complete Class Layout

```cpp
template <...>
class Button : public gpio::GPIO<PORT, PIN> {
    using Base = gpio::GPIO<PORT, PIN>;

    static_assert(PIN <= GPIO_PIN_15, "Pin number must be <= 15");

  public:
    Button();                          // 构造：配置输入模式
    bool is_pressed() const;           // 读取：当前是否按下
    template <typename Callback>
        requires std::invocable<Callback, ButtonEvent>
    void poll_events(Callback&&, uint32_t, uint32_t = 20);  // 状态机轮询

  private:
    enum class State { ... };          // 7 状态枚举
    State state_ = State::BootSync;    // 当前状态
    bool raw_pressed_ = false;         // 原始采样值
    bool stable_pressed_ = false;      // 确认的稳定值
    bool boot_locked_ = false;         // 启动锁标志
    uint32_t debounce_start_ = 0;      // 消抖计时起点
};
```

The composition of `sizeof(Button<GpioPort::A, GPIO_PIN_0>)`:

- The base class `GPIO<PORT, PIN>` has no member variables (all operations are determined at compile time via template parameters), `sizeof` is 1 (usually 0 after Empty Base Optimization).
- Derived class members: `State` (4B) + 3 `bool` (3B) + `uint32_t` (4B) + alignment ≈ 12 bytes.

12 bytes of state storage. On an STM32F103C8T6 with 20KB RAM, this is negligible.

---

## Looking Back

In this part, we designed the skeleton of the Button template class:

- **Four Template Parameters**: `PORT`, `PIN`, `PULL`, `LEVEL`, determining all hardware configuration at compile time.
- **`static_assert`**: Validates pin number legality at compile time.
- **Constructor**: Automatically configures input mode + enables clock.
- **`is_pressed()`**: `if constexpr` compile-time branching, zero overhead.
- **Memory Footprint**: Only 12 bytes of state variables.

This design follows the lineage of the LED template class—the same NTTP pattern, the same `if constexpr`, and the same zero-overhead abstraction. The only addition is `static_assert`, a simple but effective compile-time defense mechanism.

The next part is the final one for this C++ refactoring: Concepts to constrain callback parameters, followed by a walkthrough of the complete `main.cpp` call chain.
