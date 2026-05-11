---
title: "第35篇：printf 重定向与阻塞接收 —— 让芯片用 printf 说话，也学会倾听"
description: ""
tags:
  - beginner
  - cpp-modern
  - stm32f1
difficulty: beginner
platform: stm32f1
chapter: 17
order: 5
---
# 第35篇：printf 重定向与阻塞接收 —— 让芯片用 printf 说话，也学会倾听

> 上一篇让芯片发出了第一句话。这一篇我们做两件事：让 `printf()` 直接输出到串口，然后尝试阻塞式接收——接着你就会明白为什么阻塞接收行不通。

---

## printf 重定向：原理

如果你在嵌入式项目中用过 `printf()`，你可能注意到：默认情况下它什么也不输出。这是因为 `printf()` 本身不知道数据应该发到哪里——它只负责格式化字符串，然后把格式化后的结果交给底层 I/O 函数。在 PC 上，这个底层函数把数据写到终端；在 bare-metal STM32 上，你需要自己提供这个底层函数。

newlib（ARM 工具链使用的 C 标准库实现）提供了一组可重定向的系统调用。其中 `_write(int fd, char* ptr, int len)` 负责把 `ptr` 指向的 `len` 个字节写到文件描述符 `fd`。当 `printf()` 被调用时，格式化后的字符串最终会通过 `_write()` 输出。如果我们覆盖 `_write()`，让它把数据发给 UART，那所有 `printf()` 的输出就自动转到串口了。

这个机制叫"retarget"——重定向标准 I/O 到自定义的硬件接口。

---

## printf_redirect.cpp 逐行讲解

这是我们代码中的完整实现，只有 11 行：

```cpp
// 来源: code/stm32f1-tutorials/3_uart_logger/system/printf_redirect.cpp
#include "device/uart/uart_manager.hpp"

extern "C" {

int _write(int fd [[maybe_unused]], char* ptr, int len) {
    auto* huart = device::uart::UartManager<device::uart::UartInstance::Usart1>::handle();
    HAL_UART_Transmit(huart, reinterpret_cast<uint8_t*>(ptr), len, HAL_MAX_DELAY);
    return len;
}

} // extern "C"
```

逐行拆解：

### `extern "C"` 块

`_write()` 的函数签名必须在链接器眼中是一个 C 函数。因为 newlib 用 C 链接来查找这个符号——它期望 `_write` 在符号表中的名字就是 `_write`，而不是 C++ 编译器 mangle 后的 `_write__FiPcPi` 之类的东西。`extern "C"` 告诉 C++ 编译器："用 C 的链接规则来处理这个函数，不要做 name mangling。"

### `int _write(int fd, char* ptr, int len)`

三个参数：`fd` 是文件描述符（1 = stdout，2 = stderr），`ptr` 指向待发送的数据，`len` 是数据长度。`fd` 参数我们不需要区分——不管是 stdout 还是 stderr，都发到同一个 UART。

`[[maybe_unused]]` 属性告诉编译器"我知道 `fd` 没被使用，不要警告"。这是 C++17 的属性，比 `(void)fd;` 这种旧式写法更清晰地表达了意图。

### `auto* huart = UartManager&lt;UartInstance::Usart1&gt;::handle()`

获取 USART1 的 HAL 句柄指针。`handle()` 是一个静态方法，返回 `UART_HandleTypeDef*`——这是 HAL 库所有 UART 函数都需要的参数。我们通过 `UartManager` 来获取句柄，而不是使用全局变量 `extern UART_HandleTypeDef huart1`。这样做的好处是：句柄的生命周期和访问权限完全由 C++ 类型系统管理，没有任何全局状态泄漏。

### `HAL_UART_Transmit(huart, ...)`

阻塞式发送。和上一篇讲的完全一样——把 `len` 个字节逐个发出去，发完才返回。因为用了 `HAL_MAX_DELAY`，所以永远不会超时。

### `return len`

返回实际写入的字节数。告诉 C 库"所有数据都成功写入了"。如果返回 -1 或 0，C 库可能会认为发生了错误。

---

## printf 的威力

有了这个重定向之后，你的代码中任何 `printf()` 调用都会自动输出到串口：

```c
printf("System initialized at %lu Hz\r\n", SystemCoreClock);
printf("Button pressed! Count: %d\r\n", count);
printf("Temperature: %d.%d C\r\n", temp / 10, temp % 10);
```

这比手动拼字符串然后调 `send_string()` 方便太多了。特别是格式化输出——`%d`、`%x`、`%s` 这些格式化符让你能直接输出数字、十六进制值和字符串，不用自己写 `itoa()` 和字符串拼接。

不过有一个需要注意的地方：我们的 CMakeLists.txt 使用了 `-specs=nano.specs` 链接器选项。这个选项使用精简版的 C 库来节省 Flash 空间，代价是**不支持浮点数的 `printf`**。也就是说，`printf("%f", 3.14)` 不会输出正确的结果。如果你需要输出浮点数，要么用整数模拟（`printf("%d.%d", val / 100, val % 100)`），要么改用完整的 `printf` 实现（去掉 `-specs=nano.specs`，但 Flash 占用会增加不少）。

---

## 阻塞式接收：HAL_UART_Receive

发送搞定了，现在来看接收。HAL 库提供了和 `HAL_UART_Transmit()` 对称的阻塞式接收函数：

```c
uint8_t byte;
HAL_StatusTypeDef result = HAL_UART_Receive(&huart1, &byte, 1, HAL_MAX_DELAY);
if (result == HAL_OK) {
    printf("Received: 0x%02X\r\n", byte);
}
```

`HAL_UART_Receive()` 等待接收指定数量的字节。上面的代码等待接收 1 个字节，收到后打印出来。如果超时（`HAL_MAX_DELAY` 的情况下永远不会），返回 `HAL_TIMEOUT`。

听起来很合理，对吧？但让我们把这个接收放到一个完整的主循环中看看会发生什么：

```c
while (1) {
    uint8_t byte;
    HAL_UART_Receive(&huart1, &byte, 1, HAL_MAX_DELAY);
    // 处理接收到的字节...
    process_byte(byte);

    // 检查按钮
    button_poll();  // <-- 这行永远不会执行，直到收到下一个字节！

    // 闪烁 LED
    led_toggle();   // <-- 同上
}
```

问题很明显：`HAL_UART_Receive(&huart1, &byte, 1, HAL_MAX_DELAY)` 会**永远阻塞**，直到收到一个字节。如果 PC 端没有发送任何数据，这行代码后面的所有代码都不会执行。按钮轮询停了，LED 不闪了，整个系统"冻住"了，等着一个可能永远不来的字节。

这和按钮教程中 `HAL_Delay()` 阻塞系统的问题是同一个本质——你的主循环被一个可能长时间不返回的调用卡住了。在按钮教程中，解决方案是非阻塞消抖（用 `HAL_GetTick()` 做时间戳管理）。在 UART 接收中，解决方案是——中断。

你可能会想："那我把超时设短一点不就行了？比如 100 毫秒。"

```c
while (1) {
    uint8_t byte;
    HAL_StatusTypeDef result = HAL_UART_Receive(&huart1, &byte, 1, 100);
    if (result == HAL_OK) {
        process_byte(byte);
    }
    // 即使没收到数据，100ms 后也会返回
    button_poll();
    led_toggle();
}
```

这确实能让主循环继续跑，但它引入了新的问题。100 毫秒的超时意味着你的按钮轮询间隔变成了最坏 100 毫秒——对于快速按键来说可能太慢了。而且每次调用 `HAL_UART_Receive()` 都要重新配置接收寄存器，频繁的配置/超时/重新配置会浪费 CPU 时间。这不是一个优雅的方案。

正确的方案是让硬件在数据到来时主动通知 CPU，而不是 CPU 主动去等。这就是中断驱动接收——本系列的核心主题。

---

## 从阻塞到中断：问题的本质

让我们退一步，看清楚问题的本质。

阻塞式发送其实没什么大问题。你主动发数据，发多快由你决定，发完了继续干别的事。阻塞的时间是可预测的——115200 baud 下一个字节 87 微秒，发送一个 100 字节的日志也就 8.7 毫秒。在调试场景下完全可以接受。

阻塞式接收则完全不同。接收是被动行为——你不知道数据什么时候来，可能下一毫秒就来，也可能十分钟都不来。如果你选择等待（阻塞），那等待期间系统什么都干不了。如果你选择不等待（超时），那检查频率和系统响应之间存在矛盾——检查太频繁浪费 CPU，检查太慢漏数据。

这个问题的通用解法是：让接收这件事从"主循环主动问"变成"硬件主动通知"。数据来了，硬件产生一个中断信号，CPU 暂停当前任务去处理这个字节，处理完继续回来。主循环不需要等，不需要轮询，不需要在"及时响应"和"不浪费 CPU"之间做权衡。

这就是接下来三篇要讲的内容。第 36 篇讲 Cortex-M3 的中断机制和 NVIC 配置。第 37 篇设计一个无锁环形缓冲区来安全地连接 ISR 和主循环。第 38 篇把完整的回调链串起来。

---

## 小结

这一篇做了两件事。printf 重定向让我们能用熟悉的 `printf()` 格式化输出到串口，调试体验大幅提升。阻塞式接收让我们亲眼看到了"等数据"的致命问题——主循环被冻住。这个问题的存在不是 bug，而是阻塞式 I/O 的根本局限。

下一篇开始，我们进入本系列的核心阶段：中断。先搞清楚 Cortex-M3 的中断硬件是怎么工作的，然后逐步构建一个完整的中断驱动接收系统。
