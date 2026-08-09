---
chapter: 17
difficulty: beginner
order: 4
platform: stm32f1
reading_time_minutes: 8
tags:
- beginner
- cpp-modern
- stm32f1
title: 第34篇：HAL UART 初始化与发送 —— 让芯片开口说话
description: ''
---
# 第34篇：HAL UART 初始化与发送 —— 让芯片开口说话

> 硬件原理讲了三篇，现在终于可以动手写代码了。这一篇的目标很简单：让芯片通过 UART 向你的电脑发送第一句话。

---

## 我们的目标

在开始写代码之前，先明确这一篇要达成什么。最终效果是：烧录代码后，打开 PC 端的终端软件（波特率 115200、8N1），你会看到终端里出现 "Hello UART!"。就这么简单。但这意味着整个 UART 发送链路——GPIO 配置、USART 时钟使能、HAL 初始化、阻塞式发送——全部打通了。

这一篇只讲发送，不讲接收。原因很简单：发送比接收容易得多。发送是主动行为——芯片决定什么时候发、发什么。接收是被动行为——你不知道外部数据什么时候来，也不知道来多少。先把发送搞通，建立信心，再在下一篇处理接收。

---

## 初始化序列的五个步骤

要让 USART1 工作，需要按顺序完成以下五步。每一步都有明确的理由，我们来逐个过一遍。

### 第一步：使能 GPIOA 时钟

PA9 和 PA10 都在 GPIOA 上。和 LED/按钮教程一样，GPIO 端口默认时钟关闭，必须先打开。

```c
__HAL_RCC_GPIOA_CLK_ENABLE();
```

### 第二步：配置 PA9（TX）为复用功能推挽输出

上一篇已经解释过为什么 TX 引脚需要 AF_PP 模式——USART 外设直接控制这个引脚的电平，GPIO 控制器退居二线。

```c
GPIO_InitTypeDef gpio = {0};
gpio.Pin   = GPIO_PIN_9;
gpio.Mode  = GPIO_MODE_AF_PP;
gpio.Speed = GPIO_SPEED_FREQ_HIGH;
HAL_GPIO_Init(GPIOA, &gpio);
```

`GPIO_SPEED_FREQ_HIGH` 设置引脚的输出翻转速率。在 115200 baud 下，每个 bit 持续约 8.68 微秒，信号边沿需要足够陡峭才能在采样窗口内稳定。高速模式确保这一点。

### 第三步：配置 PA10（RX）为上拉输入

虽然这一篇只做发送，但初始化时把 RX 也配好是常见的做法，避免后续添加接收功能时回头改。

```c
gpio.Pin  = GPIO_PIN_10;
gpio.Mode = GPIO_MODE_INPUT;
gpio.Pull = GPIO_PULLUP;
HAL_GPIO_Init(GPIOA, &gpio);
```

上拉电阻保证 RX 线在空闲时保持高电平，和 UART 协议的空闲状态一致。如果没有上拉，RX 线浮空，可能会被噪声触发虚假的起始位检测。

### 第四步：使能 USART1 时钟

```c
__HAL_RCC_USART1_CLK_ENABLE();
```

USART1 挂在 APB2 上，这个宏操作的是 RCC_APB2ENR 寄存器的 USART1EN 位。和 GPIO 时钟使能一样，不调这个宏，USART 寄存器写不进去。

### 第五步：配置并初始化 USART

这是最关键的一步。`UART_InitTypeDef` 结构体定义了 USART 的通信参数：

```c
UART_InitTypeDef init = {0};
init.BaudRate   = 115200;
init.WordLength = UART_WORDLENGTH_8B;
init.StopBits   = UART_STOPBITS_1;
init.Parity     = UART_PARITY_NONE;
init.Mode       = UART_MODE_TX_RX;
init.HwFlowCtl  = UART_HWCONTROL_NONE;
init.OverSampling = UART_OVERSAMPLING_16;

huart1.Instance = USART1;
huart1.Init     = init;
HAL_UART_Init(&huart1);
```

逐个参数解释：

- **BaudRate = 115200** — 我们选择的波特率。上一篇分析过，64 MHz 时钟下误差只有 0.08%，完全没问题。
- **WordLength = UART_WORDLENGTH_8B** — 8 位数据位。这是标准配置，覆盖所有 ASCII 字符和一个字节的全部范围（0-255）。
- **StopBits = UART_STOPBITS_1** — 1 个停止位。最常用的配置。
- **Parity = UART_PARITY_NONE** — 无校验。不加校验位，一个帧就是 1+8+1=10 个 bit。
- **Mode = UART_MODE_TX_RX** — 同时使能发送和接收。即使现在只发不收，先把两个方向都打开也没坏处。
- **HwFlowCtl = UART_HWCONTROL_NONE** — 无硬件流控制。调试场景不需要。
- **OverSampling = UART_OVERSAMPLING_16** — 16 倍过采样。默认且最稳健的选择。

这些参数组合在一起就是我们常说的 **8N1**（8 数据位、无校验、1 停止位）配置，115200 波特率。这是嵌入式世界中最常见的 UART 配置——如果你不确定用什么，8N1 + 115200 是最安全的选择。

---

## UartConfig 结构体

在我们的 C++ 代码中，这些 HAL 常量被封装进了类型安全的 `enum class`，然后组合到 `UartConfig` 结构体中：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_config.hpp
struct UartConfig {
    uint32_t baud_rate      = 115200;
    WordLength word_length  = WordLength::Bits8;
    Parity parity           = Parity::None;
    StopBits stop_bits      = StopBits::One;
    Mode mode               = Mode::TxRx;
    HwFlowControl hw_flow   = HwFlowControl::None;
};
```

默认值就是 8N1 + 115200 + 全双工 + 无流控。在 `main.cpp` 中初始化时只需要这样写：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/main.cpp
Logger::driver().init(device::uart::UartConfig{.baud_rate = 115200});
```

这里用到了 C++20 的指定初始化器（Designated Initializer）——只指定需要改的字段（`baud_rate`），其余字段自动使用默认值。如果你需要改校验位，写 `.parity = Parity::Even` 就行，不用把所有字段都写一遍。

---

## 阻塞式发送：HAL_UART_Transmit

初始化完成后，发送数据只需要一个函数调用：

```c
uint8_t data[] = "Hello UART!\r\n";
HAL_UART_Transmit(&huart1, data, strlen((char*)data), HAL_MAX_DELAY);
```

`HAL_UART_Transmit()` 的工作方式是：

1. 把第一个字节写入 DR 寄存器（触发发送）
2. 轮询等待 TXE 标志（发送数据寄存器为空）
3. TXE 置位后写入下一个字节
4. 重复直到所有字节发送完毕
5. 最后等待 TC 标志（发送完成）

`HAL_MAX_DELAY` 表示无限等待——函数会一直等到所有数据发完才返回。在调试场景下这完全没问题。如果你的系统对响应时间有要求，可以指定一个超时值（毫秒），超时后函数返回 `HAL_TIMEOUT`。

这个函数为什么叫"阻塞式"？因为它在发送过程中把 CPU 卡住了。115200 baud 下发送一个字节（10 bits）大约需要 87 微秒。发送 13 个字节的 "Hello UART!\r\n" 大约需要 1.1 毫秒。在这 1.1 毫秒内，CPU 什么都不能做——它在忙等 TXE 标志。对于调试日志输出来说，这个代价完全可以接受。但如果你在实时系统中需要每 100 微秒做一次控制循环，那 1.1 毫秒的阻塞就是致命的了。

---

## 在我们的代码中：send_string

C++ 驱动把阻塞式发送封装成了更友好的接口。`send_string()` 接受一个 `std::string_view`：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/device/uart/uart_driver.hpp
void send_string(std::string_view str) {
    auto bytes = std::as_bytes(std::span<const char>{str});
    [[maybe_unused]] auto result = send(bytes, HAL_MAX_DELAY);
}
```

`std::string_view` 是 C++17 的字符串视图——不拷贝数据，只持有一个指向原始字符数据的指针和长度。`std::as_bytes()` 把字符视图转换成字节视图，然后传给 `send()`。`send()` 内部调用 `HAL_UART_Transmit()`，返回 `std::expected<size_t, UartError>`——但 `send_string()` 简单地忽略了返回值（`[[maybe_unused]]`），因为它主要用于调试日志，出错也不需要特殊处理。

如果你需要更精细的错误控制，可以直接调用 `send()`：

```cpp
auto result = driver.send(std::as_bytes(std::span<const char>{"Hello\r\n"}), 1000);
if (!result) {
    // 处理错误：result.error() 是 UartError 枚举值
}
```

错误处理的详细机制会在第 39 篇讲 `std::expected` 时展开。

---

## 第一次测试

代码写好了，烧录到板子上，打开终端（115200, 8N1），你应该会看到：

```text
Hello UART!
```

如果看到了——恭喜，你的 UART 发送链路完全打通了。

如果没有看到，大概率是以下三个问题之一：

**终端什么都没有？** 检查接线。适配器 TX 接 PA10，适配器 RX 接 PA9，GND 接 GND。三根线缺一不可。另外确认终端连接的是正确的 COM 口（Linux 下是 `/dev/ttyUSB0` 或 `/dev/ttyACM0`，Windows 下是 `COM3` 之类的）。

**终端显示乱码？** 波特率不匹配。确认终端和代码都设置为 115200。如果你的代码用了别的波特率，终端必须保持一致。

**只有第一行正常，后面就不对了？** 可能是 TX 线接触不良。杜邦线连接不稳的时候会出现这种现象——第一行发送时线还接触着，后面的发送过程中线松了。换一根线试试。

---

## 小结

这一篇我们完成了 UART 发送的全流程：五步初始化（GPIO 时钟 → TX/RX 引脚配置 → USART 时钟 → UART_InitTypeDef → HAL_UART_Init）+ 阻塞式发送。终端里出现 "Hello UART!" 的那一刻，意味着硬件接线正确、时钟配置正确、波特率匹配正确、USART 外设工作正常。

发送搞定了，下一篇我们做两件事：让 `printf()` 的输出直接发到串口（printf 重定向），以及尝试阻塞式接收——然后你会发现阻塞接收的致命问题，为后续引入中断驱动接收做好铺垫。
