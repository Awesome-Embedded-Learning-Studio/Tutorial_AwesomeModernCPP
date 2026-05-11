---
title: 第28篇：Button 模板类设计 —— 把一切交给编译器
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 10
---
# 第28篇：Button 模板类设计 —— 把一切交给编译器

> 承接上一篇：`std::variant` + `std::visit` 搞定了事件表达。这一篇设计 Button 模板类，把端口、引脚、上下拉、电平极性全部编码进编译时类型。

---

## 模板参数：四维配置

LED 教程中，`LED` 模板接受三个参数：`GpioPort`、`PIN`、`ActiveLevel`。Button 模板多了一个维度：

```cpp
template <gpio::GpioPort PORT, uint16_t PIN,
          gpio::PullPush PULL = gpio::PullPush::PullUp,
          ButtonActiveLevel LEVEL = ButtonActiveLevel::Low>
class Button : public gpio::GPIO<PORT, PIN> {
```

| 参数 | 类型 | 默认值 | 含义 |
|------|------|--------|------|
| `PORT` | `GpioPort` 枚举 | 无（必须指定） | GPIO 端口（A/B/C/D/E） |
| `PIN` | `uint16_t` | 无（必须指定） | 引脚编号（GPIO_PIN_0 ~ GPIO_PIN_15） |
| `PULL` | `PullPush` 枚举 | `PullUp` | 上下拉模式 |
| `LEVEL` | `ButtonActiveLevel` 枚举 | `Low` | 按下时的电平极性 |

四个参数都有默认值（除了 `PORT` 和 `PIN`），所以最常见的用法很简洁：

```cpp
// PA0 上拉输入，低电平有效 — 最常见的配置
device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;

// 如果需要下拉输入 + 高电平有效
device::Button<device::gpio::GpioPort::A, GPIO_PIN_0,
               device::gpio::PullPush::PullDown,
               device::ButtonActiveLevel::High> button;
```

### 和 LED 模板的对比

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

结构几乎一样——都继承 `GPIO<PORT, PIN>`，都用非类型模板参数（NTTP）编码硬件配置。Button 多了一个 `PULL` 参数，因为输入模式需要明确指定上下拉方向，而输出模式不需要。

---

## static_assert：编译时防御

```cpp
static_assert(PIN <= GPIO_PIN_15, "Pin number must be <= 15");
```

`static_assert` 在编译时检查一个常量表达式是否为真。如果为假，编译立即终止，输出你写的错误信息。

`GPIO_PIN_0` 到 `GPIO_PIN_15` 的值是 `0x0001` 到 `0x8000`（每个 bit 对应一个引脚）。`GPIO_PIN_15` 是 `0x8000`（bit 15 置位）。任何超过这个值的引脚编号都是无效的——STM32F103 每个 GPIO 端口最多 16 个引脚。

如果你写了：

```cpp
device::Button<device::gpio::GpioPort::A, 0xFFFF> button;  // 错误！
```

编译器会立即报错：

```text
error: static assertion failed: Pin number must be <= 15
```

不用等到烧录到板子上才发现引脚号写错了。这就是编译时防御的价值。

⚠️ 注意 `static_assert` 的位置——它在类体内部、`public` 之前。这意味着它在模板实例化时（也就是你写 `Button<GpioPort::A, GPIO_PIN_0>` 的时候）执行。只有被实际使用的模板实例才会触发检查。

---

## 构造函数：自动配置输入模式

```cpp
Button() {
    Base::setup(Base::Mode::Input, PULL, Base::Speed::Low);
}
```

和 LED 的构造函数对比：

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

两个区别：

1. `Mode::Input` 替代 `Mode::OutputPP` — 输入模式替代推挽输出
2. `PULL` 替代 `PullPush::NoPull` — 上下拉由模板参数决定，不再是硬编码的 `NoPull`

`setup()` 内部做了三件事（LED 教程第 09 篇拆解过）：

1. 调用 `GPIOClock::enable_target_clock()` — 用 `if constexpr` 自动选择端口时钟
2. 填充 `GPIO_InitTypeDef` 结构体
3. 调用 `HAL_GPIO_Init()` 写入寄存器

构造函数被调用时，PA0 就被配置成了上拉输入模式，GPIOA 时钟也自动使能了。你不需要记住"先开时钟再初始化"——模板帮你搞定了。

---

## is_pressed()：编译时分支

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

这段代码和 LED 的 `on()`/`off()` 方法是同一个 `if constexpr` 模式：

```cpp
// LED::on()
void on() const {
    Base::set_gpio_pin_state(
        LEVEL == ActiveLevel::Low ? Base::State::UnSet : Base::State::Set);
}
```

但有一个区别：LED 用的是三元运算符 `? :`，Button 用的是 `if constexpr`。效果完全一样——都是编译时选择分支。`if constexpr` 在语义上更清晰，特别是当两个分支的逻辑更复杂时（比如按下分支要做三件事，释放分支要做两件事）。

对于 `ButtonActiveLevel::Low` 的按钮（上拉方案，按下=低电平），编译后的 `is_pressed()` 等价于：

```cpp
bool is_pressed() const {
    return HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET;
}
```

一次寄存器读取，一次比较。没有 `if constexpr` 的运行时开销——因为它根本不生成没有选择的分支的代码。

---

## 模板实例化：不同配置 = 不同类型

```cpp
Button<GpioPort::A, GPIO_PIN_0>                        button1;  // PA0, 上拉, 低电平有效
Button<GpioPort::A, GPIO_PIN_0, PullPush::PullDown, ButtonActiveLevel::High>
                                                      button2;  // PA0, 下拉, 高电平有效
Button<GpioPort::B, GPIO_PIN_5>                        button3;  // PB5, 上拉, 低电平有效
```

`button1`、`button2`、`button3` 是三个完全不同的类型。编译器为每个不同的模板参数组合生成一份独立的代码。`button1::is_pressed()` 和 `button2::is_pressed()` 的实现不同——前者检查低电平，后者检查高电平。

这就是模板的"代价"：编译时间增加，代码体积可能增加（如果有很多不同实例化）。但在嵌入式场景中，通常只有少数几种按钮配置，代码体积增加可以忽略。换来的好处是编译时类型安全和零运行时开销。

---

## 完整的类布局

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

`sizeof(Button<GpioPort::A, GPIO_PIN_0>)` 的组成：

- 基类 `GPIO<PORT, PIN>` 没有成员变量（所有操作都是通过模板参数在编译时确定的），`sizeof` 为 1（空基类优化后通常为 0）
- 派生类成员：`State` (4B) + 3 个 `bool` (3B) + `uint32_t` (4B) + 对齐 ≈ 12 字节

12 字节的状态存储。在一个 20KB RAM 的 STM32F103C8T6 上，这什么都不算。

---

## 我们回头看

这一篇设计了 Button 模板类的骨架：

- **四个模板参数**：`PORT`、`PIN`、`PULL`、`LEVEL`，编译时确定所有硬件配置
- **`static_assert`**：编译时校验引脚号合法性
- **构造函数**：自动配置输入模式 + 时钟使能
- **`is_pressed()`**：`if constexpr` 编译时分支，零开销
- **内存占用**：仅 12 字节的状态变量

和 LED 模板类的设计一脉相承——同样的 NTTP 模式、同样的 `if constexpr`、同样的零开销抽象。唯一的新增是 `static_assert`，一个简单但有效的编译时防御手段。

下一篇是 C++ 重构的最后一篇：Concepts 约束回调参数，然后走读完整的 `main.cpp` 调用链。
