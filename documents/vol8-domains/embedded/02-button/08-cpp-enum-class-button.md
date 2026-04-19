---
title: 第26篇：`enum class` 重构按钮代码 —— 类型安全的输入
description: ''
tags:
- cpp-modern
- intermediate
- stm32f1
difficulty: intermediate
platform: stm32f1
chapter: 16
order: 8
---
# 第26篇：`enum class` 重构按钮代码 —— 类型安全的输入

> 承接上一篇：7 状态消抖状态机已经讲透了。现在开始 C++ 重构之旅——和 LED 教程一样，先从 `enum class` 开始。

---

## C 语言版本的痛点

到目前为止，我们的按钮代码都是 C 风格的。看看消抖代码中的"魔法数字"：

```c
uint8_t stable_pressed = 0;   // 0 是松开，1 是按下——但类型是 uint8_t，编译器不知道这个语义
uint8_t last_raw = 0;
uint8_t current = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) ? 1 : 0;
```

`uint8_t` 可以是任何东西——引脚编号、状态值、模式选择。编译器不会阻止你把一个引脚编号赋给一个状态变量。在 15 行代码里这不是问题，在 1500 行的项目里就是定时炸弹。

LED 教程第 08 篇讲过同样的问题——C 宏的 `#define LED_PIN GPIO_PIN_13` 没有类型安全。按钮面对的是同一个问题，只是"魔法数字"从宏变成了裸整数。

---

## ButtonActiveLevel 枚举

LED 有 `ActiveLevel` 表示高电平有效还是低电平有效。按钮也有同样的概念——上拉方案中按下=低电平（Active Low），下拉方案中按下=高电平（Active High）。

```cpp
enum class ButtonActiveLevel { Low, High };
```

这个枚举和 LED 的 `ActiveLevel` 是同构的，但我们用了不同的名字（`ButtonActiveLevel`）来区分语义。LED 的 `ActiveLevel` 描述的是"LED 亮需要的电平"，按钮的 `ButtonActiveLevel` 描述的是"按钮按下时的电平"。虽然底层值一样，但它们是不同的概念——不应该混用。

有了 `ButtonActiveLevel`，`is_pressed()` 方法就不需要 `#ifdef` 或者运行时判断了：

```cpp
bool is_pressed() const {
    auto state = Base::read_pin_state();
    if constexpr (LEVEL == ButtonActiveLevel::Low) {
        return state == Base::State::UnSet;  // 低电平 = 按下
    } else {
        return state == Base::State::Set;    // 高电平 = 按下
    }
}
```

`if constexpr` 在编译时选择分支——对于 `ButtonActiveLevel::Low` 的按钮，编译器只生成 `state == State::UnSet` 的代码；对于 `ButtonActiveLevel::High`，只生成 `state == State::Set`。零运行时开销，编译时就把电平逻辑"写死"了。

这和 LED 教程第 10 篇的 `if constexpr` 时钟使能是同一个模式——用编译时分支替代运行时判断。

---

## 私有 enum class State

上一篇我们详细解读了 7 个状态。现在看看它们在代码中是怎么定义的：

```cpp
enum class State {
    BootSync,
    Idle,
    DebouncingPress,
    Pressed,
    DebouncingRelease,
    BootPressed,
    BootReleaseDebouncing,
};
```

几个设计决策值得说明：

**为什么是 `enum class` 而不是 `enum`？** 作用域隔离。`Idle`、`Pressed` 这些名字很常见——如果你的代码里有其他状态机（比如 LED 闪烁状态机、通信协议状态机），普通 `enum` 的 `Idle` 就会冲突。`enum class` 要求 `State::Idle` 完整限定，不同 `enum class` 的同名成员互不干扰。

**为什么是私有枚举？** `State` 定义在 `Button` 类的 `private` 区域。外部代码不需要知道按钮内部有 7 个状态——它们只需要调用 `poll_events()` 就够了。把 `State` 设为私有，就是信息隐藏：实现细节不暴露给调用者。

**为什么没有指定底层类型？** 默认底层类型是 `int`（通常是 32 位）。只有 7 个值，用 `uint8_t` 更省空间？在 `sizeof(Button)` 的上下文中，`State` 类型的成员变量 `state_` 确实可以用 `uint8_t` 来存。但编译器通常会对齐到自然字长，所以 `uint8_t` 和 `int` 的实际占用可能一样。除非你的 RAM 真的紧张到每一个字节都要抠，否则默认 `int` 是最安全的选择。

---

## 回顾：enum class 在 LED 和按钮教程中的对比

| 特性 | LED 教程 | 按钮教程 |
|------|---------|---------|
| GpioPort | 端口地址 | 复用，无变化 |
| Mode | 输出模式 | 新增输入/中断模式的枚举值 |
| PullPush | 上下拉 | 复用，按钮用 `PullUp` |
| State | Set/UnSet | 复用，`read_pin_state()` 返回它 |
| ActiveLevel | LED 亮灭电平 | **新增** `ButtonActiveLevel` |
| 内部状态 | 无 | **新增** 私有 `State` 枚举 |

`enum class` 在按钮教程中有两个新的应用场景：`ButtonActiveLevel` 作为模板参数（编译时常量），`State` 作为内部状态机的状态类型。两者的用途完全不同——前者是面向调用者的配置参数，后者是实现细节——但都受益于 `enum class` 的类型安全和作用域隔离。

---

## 我们回头看

这一篇用 `enum class` 重构了按钮代码中的两类枚举：

1. **`ButtonActiveLevel`** — 模板参数，编译时决定电平逻辑，配合 `if constexpr` 实现零开销分支
2. **`State`** — 私有状态机枚举，7 个状态各司其职，作用域隔离防止命名冲突

这些和 LED 教程的 `enum class` 章节一脉相承——同样的工具，不同的应用场景。下一篇引入一个全新的 C++ 特性：`std::variant` 和 `std::visit`，用类型安全的方式表达按钮事件。
