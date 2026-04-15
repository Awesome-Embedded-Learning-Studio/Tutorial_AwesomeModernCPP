# 第29篇：Concepts 约束回调 + 完整代码走读

> 承接上一篇：Button 模板类的骨架搭好了。这一篇解决最后一个 C++ 特性——用 Concepts 约束回调参数的类型，然后从头到尾走读一遍完整的 `main.cpp` 调用链。

---

## 回调函数的类型问题

`poll_events()` 接受一个回调函数作为参数，每当按钮状态确认变化时调用它。问题是：C++ 的模板参数 `Callback` 可以是任何类型——函数指针、lambda、函数对象、甚至一个整数（如果你的代码写错了）。

没有 Concepts 的时候，如果传了一个签名不对的回调，错误信息会是什么样的？

```cpp
// 错误的回调：接受 int 而不是 ButtonEvent
button.poll_events([](int x) { /* ... */ }, HAL_GetTick());
```text

编译器会尝试实例化 `poll_events()` 的代码，在调用 `cb(Pressed{})` 时发现 `int` 不能从 `Pressed` 构造，然后报错。但错误信息可能是这样的：

```text
error: no match for call to '(lambda) (Pressed)'
note: candidate expects 1 argument of type 'int', got 'Pressed'
  in instantiation of 'void Button::poll_events(Callback&&, uint32_t, uint32_t)
    [with Callback = main()::<lambda(int)>; ...]'
```text

几行模板实例化堆栈加上晦涩的类型信息。虽然比 C++98 的 SFINAE 错误好很多，但还不够直观。

---

## Concepts：一行约束，清晰报错

```cpp
template <typename Callback>
    requires std::invocable<Callback, ButtonEvent>
void poll_events(Callback&& cb, uint32_t now_ms, uint32_t debounce_ms = 20) {
```text

`requires std::invocable<Callback, ButtonEvent>` 是一个 Concepts 约束。它告诉编译器：`Callback` 类型的对象必须能用一个 `ButtonEvent` 参数来调用。

如果传入签名不对的回调：

```cpp
button.poll_events([](int x) { /* ... */ }, HAL_GetTick());
```text

编译器在**模板实例化之前**就报错：

```text
error: constraint 'std::invocable<lambda, ButtonEvent>' not satisfied
note: the expression 'std::invocable<lambda, ButtonEvent>' evaluated to 'false'
```text

一句话就说明白了：你的回调不满足 `std::invocable<Callback, ButtonEvent>` 约束。不需要去翻模板实例化堆栈——约束失败直接告诉你问题所在。

### std::invocable 什么意思

`std::invocable<F, Args...>` 是 C++20 `<concepts>` 头文件中定义的概念。它检查：给定类型 `F` 的对象 `f`，`f(args...)` 是否是合法的调用表达式。

对于 `std::invocable<Callback, ButtonEvent>`：
- `Callback` 是你传入的 lambda 或函数对象
- `ButtonEvent` 是 `std::variant<Pressed, Released>`
- 约束要求：`cb(ButtonEvent{})` 必须是合法的调用

合法的回调示例：

```cpp
// Lambda 接受 ButtonEvent
button.poll_events([](device::ButtonEvent e) { /* ... */ }, HAL_GetTick());

// Lambda 接受 auto（泛型 lambda）
button.poll_events([](auto&& e) { /* ... */ }, HAL_GetTick());

// Lambda 接受 Pressed（variant 的一个选项）— 这不行！
// std::invocable<Callback, ButtonEvent> 检查的是用 ButtonEvent 调用，不是 Pressed
button.poll_events([](device::Pressed e) { /* ... */ }, HAL_GetTick());  // 编译错误
```text

### Concepts 和 SFINAE 的对比

在 Concepts 之前，约束模板参数用 SFINAE（Substitution Failure Is Not An Error）：

```cpp
// SFINAE 方式 — 丑陋且难以理解
template <typename Callback,
          typename = std::enable_if_t<std::is_invocable_v<Callback, ButtonEvent>>>
void poll_events(Callback&& cb, uint32_t now_ms, uint32_t debounce_ms = 20);
```text

SFINAE 的原理是：如果 `std::enable_if_t` 的条件为假，模板会被静默地从候选列表中移除，编译器去寻找其他匹配的重载。如果找不到任何匹配，才报"no matching function"的错误——而这个错误通常伴随着几十行模板实例化堆栈。

Concepts 把约束变成了语言的一等公民：`requires` 子句直接声明约束，编译器直接检查约束，约束失败直接报约束的名字。不需要理解 SFINAE 的工作原理。

---

## Callback&& 是右值引用吗？

```cpp
void poll_events(Callback&& cb, ...)
```text

`Callback&&` 看起来像右值引用，但实际上是**转发引用**（forwarding reference）。当 `Callback` 是模板参数时，`Callback&&` 的含义取决于传入的实参：

- 传入左值（如一个有名字的 lambda 变量）：`Callback` 推导为 `Lambda&`，`Callback&&` 变成 `Lambda& &&` 折叠为 `Lambda&`（左值引用）
- 传入右值（如临时 lambda）：`Callback` 推导为 `Lambda`，`Callback&&` 就是 `Lambda&&`（右值引用）

所以 `Callback&&` 可以接受任何东西——左值、右值、const、non-const。这正是我们想要的：用户可以传一个临时 lambda，也可以传一个有名字的函数对象。

为什么不用 `const Callback&`？因为 `const` 引用不能调用非 const 的 `operator()`。虽然我们的 lambda 不修改捕获的变量，但保持通用性更安全。

在这个场景中我们没有用 `std::forward<Callback>(cb)`——因为回调只在 `poll_events()` 内部调用一次，不需要完美转发。如果 `cb` 是左值，直接调用就行；如果是右值，也是直接调用。转发引用在这里的作用只是"接受任意类型的可调用对象"，而不是"完美转发"。

---

## 完整代码走读

现在让我们从头到尾走一遍 `main.cpp` 的执行流程，看看每一行代码在做什么。

```cpp
#include "device/button.hpp"
#include "device/button_event.hpp"
#include "device/led.hpp"
#include "system/clock.h"
extern "C" {
#include "stm32f1xx_hal.h"
}
```text

头文件包含。`button.hpp` 会间接包含 `gpio.hpp`。`extern "C"` 包裹 HAL 头文件确保 C++ 编译器用 C 链接规则查找 HAL 函数（LED 教程第 12 篇讲过）。

```cpp
int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();
```text

系统初始化。和 LED 教程完全一样：初始化 HAL 庝始化，配置系统时钟到 64MHz。

```cpp
    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;
```text

对象构造。这两行各做了三件事：

**LED 构造：**
1. `GPIOClock::enable_target_clock()` — `if constexpr` 使能 GPIOC 时钟
2. `setup(Mode::OutputPP, NoPull, Low)` — 配置 PC13 为推挽输出
3. 对象 `led` 就绪，提供 `on()`、`off()`、`toggle()` 接口

**Button 构造：**
1. `GPIOClock::enable_target_clock()` — `if constexpr` 使能 GPIOA 时钟
2. `setup(Mode::Input, PullUp, Low)` — 配置 PA0 为上拉输入
3. `static_assert` 校验引脚号 — 编译时通过
4. 对象 `button` 就绪，状态机初始状态为 `BootSync`

```cpp
    while (1) {
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
    }
```text

主循环。每次循环做一件事：调用 `button.poll_events()`。

**`HAL_GetTick()`** 获取当前时间戳（毫秒），传给状态机做时间判断。

**回调 lambda** `[&](device::ButtonEvent event)` 按引用捕获了 `led`。当状态机确认状态变化时，调用这个 lambda，参数 `event` 是 `std::variant<Pressed, Released>`。

**`std::visit`** 根据 `event` 持有的类型分发：
- 如果是 `Pressed`：调用 `led.on()`
- 如果是 `Released`（`else` 分支）：调用 `led.off()`

**整条调用链：**

```text
main() 循环
  → poll_events(lambda, HAL_GetTick())
    → is_pressed() → read_pin_state() → HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)
    → switch(state_) 状态机判断
    → 确认变化时: cb(Pressed{}) 或 cb(Released{})
      → lambda 被调用，event = ButtonEvent
      → std::visit(lambda2, event)
        → if constexpr: led.on() 或 led.off()
          → HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, ...)
```text

从用户按下按钮到 LED 亮起，经历：物理电平变化 → IDR 寄存器更新 → `HAL_GPIO_ReadPin()` 读取 → 状态机消抖确认 → `Pressed` 事件触发 → `std::visit` 分发 → `led.on()` → `HAL_GPIO_WritePin()` → ODR 寄存器更新 → LED 亮。

整个过程没有虚函数、没有堆分配、没有异常处理。每一层都是编译时确定的内联调用。

---

## 我们回头看

这一篇完成了 C++ 重构的最后一环：

- **Concepts** (`requires std::invocable<Callback, ButtonEvent>`) 约束回调签名，提供清晰的编译错误
- **转发引用** `Callback&&` 接受任意可调用对象
- **完整代码走读** 从 `main()` 到 `HAL_GPIO_WritePin()` 的整条调用链

到目前为止，我们已经用 C++ 重构了按钮控制的全部代码。下一篇是本系列的收尾——EXTI 中断驱动按钮，加上常见坑位汇总和练习题。
