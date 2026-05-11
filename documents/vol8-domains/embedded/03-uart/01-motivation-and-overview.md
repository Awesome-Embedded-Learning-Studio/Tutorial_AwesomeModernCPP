---
title: "第31篇：从按钮到串口 —— 为什么 UART 是嵌入式通信的基石"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
chapter: 17
order: 1
---
# 第31篇：从按钮到串口 —— 为什么 UART 是嵌入式通信的基石

> LED 教程让芯片学会了"说话"，按钮教程让芯片学会了"听话"。现在该学一件事了：让芯片和别的设备"对话"。

---

## 我们的芯片还是一座孤岛

回顾一下我们走过的路。LED 教程 13 篇，我们从 GPIO 输出模式开始，一路搞懂了时钟使能、寄存器配置、HAL 封装，最后用 C++ 模板和 `enum class` 构建了一个零开销的 LED 抽象。按钮教程 12 篇，我们转向 GPIO 输入模式，啃下了上拉/下拉电路、机械抖动、消抖状态机、`std::variant` 事件系统和 Concepts 约束回调。两套教程结束之后，我们的 STM32 已经能独立完成输入和输出了——按按钮、亮灯、消抖、状态管理，一个不少。

但如果你退后一步审视整个系统，就会发现一个问题：我们的芯片本质上还是一座孤岛。LED 是芯片自己的输出，按钮是物理世界给芯片的输入，但这两者都没有离开板子。你想知道芯片内部的状态？你得盯着板子上的 LED。你想给芯片下命令？你得伸手去按按钮。如果你的项目需要芯片把温度数据发给 PC 做可视化，或者你想从 PC 端下发配置参数，LED 和按钮就完全不够用了。

我们需要的是一种让芯片和外部世界交换数据的机制。不是简单的 0 和 1，而是真正的、结构化的数据流。这就是串口通信的用武之地。

---

## UART：最古老、最简单、仍然无处不在的协议

UART 的全称是 Universal Asynchronous Receiver/Transmitter，通用异步收发传输器。说它"古老"一点都不夸张——这个协议的基本原理可以追溯到 1960 年代的电传打字机时代。但说它"过时"就完全不对了，因为直到今天，几乎每一块微控制器上都至少有一个 UART 外设。STM32F103C8T6 这颗芯片上有三个：USART1、USART2、USART3。

为什么 UART 能活这么久？原因很简单：它只需要两根线。一根 TX（发送），一根 RX（接收），再加一根共地线。没有时钟线（不像 SPI 需要 SCK），没有地址机制（不像 I2C 需要设备地址和应答），没有主机从机的概念。两个设备只要约定好"用多快的速度说话"（波特率），就可以直接通信。这种极简性让 UART 成为嵌入式调试、日志输出、传感器通信的默认选择。

你可能听过 SPI 和 I2C。SPI 速度快但需要 4 根线（MOSI、MISO、SCK、CS），适合板内高速通信（比如驱动显示屏或读 Flash）。I2C 只需要 2 根线（SDA、SCL）但需要地址和应答机制，适合挂多个低速设备（比如温度传感器和 EEPROM）。UART 处于两者之间——线数最少（2 根），协议最简单（无地址无应答无时钟），但足以满足绝大多数"芯片和 PC 通信"或"芯片和芯片一对一通信"的需求。

对于我们这个教程来说，UART 还有一个不可替代的优势：它可以直接连到你的电脑。买一个几块钱的 USB-TTL 适配器（CH340 或 CP2102 芯片的就行），插上 USB，打开一个终端软件（minicom、PuTTY、或者 Arduino IDE 的串口监视器），你就能在电脑上看到芯片发来的文字，也能从电脑给芯片发命令。没有 JTAG 调试器那么复杂，没有 SPI/I2C 那样需要额外的协议解析。芯片 `printf` 出来的内容，你在终端里直接看到——就这么简单。

---

## 我们要构建什么

在正式开始之前，让我们先看看终点。这是我们完成所有代码后，`main.cpp` 的样子：

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

如果你完成了 LED 和按钮教程，这段代码的大结构应该不会完全陌生。`HAL_Init()`、系统时钟、LED 和 Button 的模板实例化，这些都和之前一模一样。新鲜的部分集中在 UART 相关的代码上，而这些正是接下来 13 篇文章要一个一个拆解的内容。

简单说几个亮点。`UartManager<UartInstance::Usart1>` 是一个类型别名——通过模板参数在编译时就锁定了"我们要用 USART1"。`send_string()` 让芯片能向 PC 发送文字。`uart_start_receive()` 启动了中断驱动的接收——每当 PC 发来一个字节，硬件中断就会把这个字节塞进一个环形缓冲区。主循环从缓冲区里取字节，拼成一行，然后交给 `handle_command()` 解析命令。你在终端里输入 "LED ON"，回车，LED 就亮了——整个链条就是这么运转的。

---

## 我们要走的路

UART 教程共 13 篇，分六个阶段。

### 阶段一：动机（第 31 篇）

就是你正在读的这一篇。讲清楚为什么要学 UART，最终效果长什么样，硬件需要准备什么。

### 阶段二：硬件基础（第 32-33 篇）

第 32 篇拆解 UART 协议本身——没有时钟线怎么同步、数据帧长什么样、波特率和过采样是怎么工作的、为什么 115200 是最常见的默认波特率。第 33 篇转向 STM32F103 的 USART 外设——三个 USART 实例的区别、关键寄存器、GPIO 复用功能引脚的配置，以及 NVIC 中断连接的预览。

### 阶段三：HAL + 阻塞 I/O（第 34-35 篇）

第 34 篇用 HAL API 完成初始化和第一次发送——让芯片对 PC 说 "Hello"。第 35 篇实现 `printf` 重定向（让 `printf()` 直接输出到串口），并尝试阻塞式接收。然后你会发现阻塞接收的致命问题：主循环被卡死了。这就自然引出了下一阶段的主题。

### 阶段四：中断驱动（第 36-38 篇）

这是本系列的核心阶段。第 36 篇全面讲解 Cortex-M3 的中断机制和 NVIC 配置。第 37 篇设计和实现一个无锁环形缓冲区，作为 ISR 和主循环之间的安全数据通道。第 38 篇把中断接收的完整回调链串起来——从 `USART1_IRQHandler` 到 `HAL_UART_RxCpltCallback` 再到环形缓冲区的 push 和重启接收。

### 阶段五：C++ 抽象（第 39-42 篇）

第 39 篇引入 C++23 的 `std::expected` 做错误处理，取代 C 风格的错误码。第 40 篇设计 UART 驱动模板——用 NTTP 选择 USART 实例，零大小空类优化消除对象开销。第 41 篇用 Concepts 约束 GPIO 初始化回调，并设计 UartManager 生命周期管理器。第 42 篇做一次完整的 `main.cpp` 走读，把所有零件组装起来。

### 阶段六：总结（第 43 篇）

常见坑位汇总（TX/RX 接反、波特率不匹配、环形缓冲区溢出、volatile 遗漏等）和三个递进练习。

---

## 硬件准备

好消息是，UART 教程不需要比按钮教程更多的核心硬件——Blue Pill + ST-Link 还是那一套。但你需要额外准备一样东西：一个 USB-TTL 串口适配器。

具体清单如下：

- **STM32F103C8T6 Blue Pill 开发板** — 和 LED/按钮教程同一块板子
- **ST-Link V2 调试器** — 烧录和调试用，和之前一样
- **USB-TTL 串口适配器** — CH340 或 CP2102 芯片的就行，淘宝十块钱以内。这个适配器负责把 USB 信号转成 UART 的 TTL 电平信号，让 PC 和 Blue Pill 能互相发送数据
- **3 根杜邦线（母对母）** — 连接适配器和 Blue Pill

接线方案：

```text
适配器 TX  → PA10（Blue Pill RX）
适配器 RX  → PA9 （Blue Pill TX）
适配器 GND → GND （Blue Pill GND）
```

注意这里的一个关键点：适配器的 TX 要接 Blue Pill 的 RX，适配器的 RX 要接 Blue Pill 的 TX。"你的发送就是我的接收"——这一点搞反了是 UART 最常见的接线错误，后面我们会反复强调。

为什么选 PA9 和 PA10？因为 STM32F103 的 USART1 的 TX 和 RX 默认复用功能引脚就是 PA9 和 PA10。这是芯片出厂就定好的，不是我们随便选的。

软件方面，你需要在 PC 上安装一个终端程序：

- **Linux**：`minicom`（`sudo apt install minicom`）或 `screen /dev/ttyUSB0 115200`
- **Windows**：PuTTY（选 Serial 模式）或 Arduino IDE 的串口监视器
- **macOS**：`screen /dev/tty.usbserial* 115200` 或 CoolTerm

终端的波特率设置为 115200，8 数据位，无校验，1 停止位（简称 8N1）——这也是我们代码中的默认配置。

---

## 我们将学到的新 C++ 特性

UART 教程涉及的 C++ 特性比前两个系列都多，因为我们需要处理错误、中断回调、模板实例选择等新问题。这里先列一个清单，后面每篇文章会逐一拆解：

- **`std::expected<T, E>`**（C++23）— 嵌入式中的错误处理，比异常更轻量，比错误码更安全
- **`std::span`**（C++20）— 对连续内存的安全视图，替代裸指针 + 长度
- **`std::string_view`**（C++17）— 零拷贝字符串视图，命令解析的利器
- **`consteval`**（C++20）— 编译时波特率误差校验
- **Concepts**（C++20）— 约束 GPIO 初始化回调的签名
- **`static inline` 成员**（C++17）— 模板类中的每实例独立存储
- **`volatile`** — ISR 和主循环之间的共享变量语义
- **`extern "C"` ISR 桥接** — C++ 代码和 C 链接的中断向量之间的桥接模式
- **`if constexpr`**（C++17）— 编译时选择不同的 USART 实例

每个特性都不是为了用而用——它们各自解决 UART 驱动实现中的一个实际问题。我们不会先讲语法再讲应用，而是在具体问题中引入特性，让你知道"为什么需要它"。

---

## 接下来去哪

准备工作做完了。UART 是什么、为什么学它、最终效果长什么样、硬件怎么接——这些你都已经知道了。

下一篇我们从头开始：UART 协议本身。没有时钟线，两个设备怎么知道一个字节从哪开始、到哪结束？起始位、数据位、校验位、停止位各自扮演什么角色？波特率的数字背后，芯片到底在做什么？这些问题搞清楚了，后面写代码时你就不会是"照抄参数"，而是"我知道这个参数在协议里意味着什么"。

准备好了吗？我们出发。
