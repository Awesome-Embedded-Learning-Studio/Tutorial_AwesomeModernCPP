---
chapter: 17
difficulty: intermediate
order: 12
platform: stm32f1
reading_time_minutes: 7
tags:
- cpp-modern
- intermediate
- stm32f1
title: 第42篇：命令处理器与完整代码走读 —— 从串口输入到 LED 控制
description: ''
---
# 第42篇：命令处理器与完整代码走读 —— 从串口输入到 LED 控制

> 所有零件都准备好了。这一篇做一次 `main.cpp` 的完整走读，看它们怎么协同工作。

---

## main.cpp 全貌

这是我们的最终代码。你已经在前面的文章中见过它的各个片段，现在让我们把它们拼成一幅完整的图：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
#include "base/circular_buffer.hpp"
#include "device/button.hpp"
#include "device/button_event.hpp"
#include "device/led.hpp"
#include "device/uart/uart_manager.hpp"
#include "system/clock.h"

extern "C" {
#include "stm32f1xx_hal.h"
}

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>

extern base::CircularBuffer<128>& uart_rx_buffer();
extern void uart_start_receive();

using Logger = device::uart::UartManager<device::uart::UartInstance::Usart1>;

static void usart1_gpio_init() noexcept {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void handle_command(std::string_view cmd,
                           device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>& led) {
    if (cmd == "LED ON") {
        led.on();
        Logger::driver().send_string("OK: LED ON\r\n");
    } else if (cmd == "LED OFF") {
        led.off();
        Logger::driver().send_string("OK: LED OFF\r\n");
    } else if (cmd == "HELP") {
        Logger::driver().send_string("Commands: LED ON, LED OFF, HELP\r\n");
    } else if (!cmd.empty()) {
        Logger::driver().send_string("ERR: unknown command\r\n");
    }
}

int main() {
    HAL_Init();
    clock::ClockConfig::instance().setup_system_clock();

    device::LED<device::gpio::GpioPort::C, GPIO_PIN_13> led;
    device::Button<device::gpio::GpioPort::A, GPIO_PIN_0> button;

    Logger::driver().set_gpio_init(usart1_gpio_init);
    Logger::driver().init(device::uart::UartConfig{.baud_rate = 115200});
    Logger::driver().enable_interrupt();
    Logger::driver().send_string("UART Logger Ready!\r\n");

    uart_start_receive();

    std::array<char, 128> line_buf{};
    size_t line_len = 0;

    while (1) {
        button.poll_events(
            [&](device::ButtonEvent event) {
                std::visit(
                    [&](auto&& e) {
                        using T = std::decay_t<decltype(e)>;
                        if constexpr (std::is_same_v<T, device::Pressed>) {
                            led.on();
                            Logger::driver().send_string("Button pressed!\r\n");
                        } else {
                            led.off();
                            Logger::driver().send_string("Button released!\r\n");
                        }
                    },
                    event);
            },
            HAL_GetTick());

        auto& rx = uart_rx_buffer();
        std::byte b{};
        while (rx.pop(b)) {
            char c = static_cast<char>(b);
            if (c == '\r' || c == '\n') {
                if (line_len > 0) {
                    handle_command({line_buf.data(), line_len}, led);
                    line_len = 0;
                }
            } else if (line_len < line_buf.size() - 1) {
                line_buf[line_len++] = c;
            }
        }
    }
}
```

---

## 初始化序列

`main()` 的前半部分是初始化，按严格的顺序执行：

![main() 初始化流程](./12-main-flow.drawio)

每一步的顺序都不能调换。时钟没配就调 HAL 函数会 hard fault。GPIO 没配好 USART 信号到不了引脚。中断没使能就启动接收的话，字节到了也不会触发 ISR。`send_string` 放在 `uart_start_receive` 之前是故意的——先发欢迎信息确认发送链路正常，再启动接收。

---

## 主循环的两个任务

主循环做两件事：处理按钮事件，处理 UART 接收。两者都不阻塞。

### 任务一：按钮轮询 → UART 日志

```cpp
button.poll_events(
    [&](device::ButtonEvent event) {
        std::visit(
            [&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, device::Pressed>) {
                    led.on();
                    Logger::driver().send_string("Button pressed!\r\n");
                } else {
                    led.off();
                    Logger::driver().send_string("Button released!\r\n");
                }
            },
            event);
    },
    HAL_GetTick());
```

这段代码和按钮教程最终版本一模一样——`poll_events()` 采样引脚电平、运行消抖状态机、在确认事件后调用回调。回调通过 `std::visit` + 泛型 lambda 处理 `Pressed` 和 `Released` 两种事件。唯一的新东西是 `Logger::driver().send_string(...)`——把按钮事件通过 UART 发送到 PC。

这意味着：当你按下按钮时，终端里会出现 "Button pressed!"；松开时出现 "Button released!"。按钮事件从芯片流到了 PC——方向是芯片 → PC。

### 任务二：UART 接收 → 命令解析

```cpp
auto& rx = uart_rx_buffer();
std::byte b{};
while (rx.pop(b)) {
    char c = static_cast<char>(b);
    if (c == '\r' || c == '\n') {
        if (line_len > 0) {
            handle_command({line_buf.data(), line_len}, led);
            line_len = 0;
        }
    } else if (line_len < line_buf.size() - 1) {
        line_buf[line_len++] = c;
    }
}
```

这是主循环中的 UART 接收处理。`rx.pop(b)` 从环形缓冲区弹出一个字节——ISR 在后台不断往里 push，主循环在这里消费。`while (rx.pop(b))` 一次性弹出所有可用字节，不会遗漏。

行解析逻辑很直接：把弹出的字节逐个拼入 `line_buf`，遇到 `\r` 或 `\n` 时认为一行结束，把完整行交给 `handle_command()` 处理，然后重置行缓冲。`line_len < line_buf.size() - 1` 确保不会溢出——超过 127 个字符的部分被丢弃。

方向和按钮相反：PC → 芯片。你在终端里输入 "LED ON" 然后回车，这个字符串从 PC 通过 UART 发到芯片，ISR 把字节逐个 push 进环形缓冲区，主循环 pop 出来拼成一行，识别为 "LED ON" 命令，然后点亮 LED。

---

## handle_command：一个微型 shell

```cpp
static void handle_command(std::string_view cmd,
                           device::LED<device::gpio::GpioPort::C, GPIO_PIN_13>& led) {
    if (cmd == "LED ON") {
        led.on();
        Logger::driver().send_string("OK: LED ON\r\n");
    } else if (cmd == "LED OFF") {
        led.off();
        Logger::driver().send_string("OK: LED OFF\r\n");
    } else if (cmd == "HELP") {
        Logger::driver().send_string("Commands: LED ON, LED OFF, HELP\r\n");
    } else if (!cmd.empty()) {
        Logger::driver().send_string("ERR: unknown command\r\n");
    }
}
```

`cmd` 参数是 `std::string_view`——指向 `line_buf` 中的原始数据，零拷贝。`==` 比较直接逐字符匹配。支持的命令：`LED ON`（开灯）、`LED OFF`（关灯）、`HELP`（显示帮助）。未知命令返回错误提示。空行（连续回车）被忽略。

每个命令执行后通过 `send_string` 返回确认信息——PC 端能立即看到命令执行结果。这就是一个简单的请求-响应模式：PC 发命令，芯片执行并确认。

---

## std::string_view 的零拷贝优势

`handle_command({line_buf.data(), line_len}, led)` 这一行创建了 `std::string_view`——它只包含一个指针和长度，不拷贝任何字符数据。`line_buf` 中的原始字符被直接比较，没有中间的 `std::string` 构造、内存分配和释放。

在 bare-metal 环境中，动态内存分配（`new`/`malloc`）可能导致碎片化和不确定性。`std::string_view` 让你能在不分配内存的情况下操作字符串——它只是指向已有数据的视图。配合 `std::array<char, 128>` 行缓冲（栈上分配），整个命令解析过程不涉及任何堆操作。

---

## 双向通信的架构

把所有数据流画在一起，整个系统的架构是这样的：

![系统整体数据流架构](./12-system-architecture.drawio)

芯片 → PC 方向：按钮事件和命令响应通过 `send_string()` 发出。这些调用使用阻塞式发送（`HAL_UART_Transmit`），因为发送量小（几十字节），阻塞时间可控（不到 1 毫秒），对系统响应没有影响。

PC → 芯片方向：终端输入的命令通过中断接收进入环形缓冲区，主循环消费并解析。完全非阻塞——ISR 在微秒级完成字节入队，主循环在自己的节奏下处理。

LED 和 Button 组件来自前两个教程，完全复用，没有任何修改。这就是好的抽象的威力——LED 模板和 Button 模板不知道 UART 的存在，但它们自然地和 UART 命令处理器协同工作。

---

## 小结

这一篇做了 `main.cpp` 的完整走读，把所有零件组装成一幅完整的架构图。系统有两个独立的数据流：按钮事件从芯片流向 PC（通过阻塞式发送），UART 命令从 PC 流向芯片（通过中断接收 + 环形缓冲区 + 行解析）。LED 和 Button 组件被完美复用——零修改，零耦合。

下一篇是本系列的收官：常见坑位汇总和三个递进练习。
