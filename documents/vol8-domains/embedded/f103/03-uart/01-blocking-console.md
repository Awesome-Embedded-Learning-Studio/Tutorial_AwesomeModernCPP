---
title: "阻塞收发的命令控制台：UART 帧的分频算术与 48 字节行缓冲"
description: "examples/05_uart 的 117 行把 USART1 装成一台命令控制台，init 走时钟使能、consteval 的 pin_map 与 HAL_UART_Init，send 与 receive 压在 HAL_UART_Transmit/Receive 的轮询上，超 65535 按 16 位上限分块，HAL 报错直接 __builtin_trap 保住 void 的合同，波特率照 RM0008 的 baud = fCK/(16×USARTDIV) 从 PCLK2 的 64MHz 算出 DIV_Mantissa 34 加 DIV_Fraction 12，BRR 摆 556，实际 115107.9 波特误差 0.08%，主循环是一字节一停的行读取，48 字节缓冲配 CR/LF 归一、退格回显与到回车才报的溢出标志，Renode 的 PTY 里 help、ping、echo、退格修字与 49 字符的超长行全部实抓对拍"
chapter: 3
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 寄存器
difficulty: intermediate
platform: stm32f1
cpp_standard: [20]
reading_time_minutes: 23
prerequisites:
  - "把配置写进类型：Gpio 模板与编译期的验证"
  - "时钟门控：灯不亮，从时钟查起"
related:
  - "把日志用起来：两种写法与两个观看面"
  - "一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍"
---

# 阻塞收发的命令控制台：UART 帧的分频算术与 48 字节行缓冲

## 引言：给板子装一条双向的线

前两站咱们把 LED 点过、把按键摸过，输入输出都是板子单方面地发信号，咱们跟它之间还缺一条能一来一回说话的线。本站补的就是它：USART（universal synchronous/asynchronous receiver/transmitter，咱们惯用的译名是通用异步收发器，F103 板上最常用的那路串口），素材是 libestdx 的 `examples/05_uart`<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />，一份 117 行的 `main.cpp` 把收发、行编辑与命令分发全装了进去，收发的底座压在 `boards/stm32f1/uart.hpp` 里。素材咱们固定在提交 8026841 上，本篇贴出的每一段终端输出都是笔者在 Renode 里实抓的，咱们一个字节都没有动。

固件上电之后的第一件事就是打招呼，下面这段就是咱们在 PTY（pseudo-terminal，伪终端的直译，Renode 把虚拟串口接成宿主侧的一个文件）里抓到的原文，咱们拿终端程序连上它，效果就跟插了根 USB 转串口的线一样：

```text
UART CMD ready
Type help for commands.
> help
Commands: help, ping, led on, led off, status, echo TEXT
> ping
pong
>
```

您敲 `help`，固件回的是命令清单。换成 `ping` 的话，它回的就是 `pong`。这样的问答背后有三个环节在干活，摆节拍的分频算术咱们放在波特率的小节里讲，把收发包成类型的活归 `Uart` 模板的小节，主循环怎么凑出一行带编辑的命令归行缓冲的小节，收尾咱们再摆一摆这套阻塞写法的边界，看看主循环忙起来的时候它会丢掉什么。

## 一帧 10 个线位，波特率是道分频算术

UART 的线上没有时钟线，收发两端靠约定的节拍对表，节拍的单位就是波特率。咱们的控制台跑 115200 8N1，写法的含义咱们一个个念，线位率摆的是每秒 115200 个，数据位摆的是 8 个，校验省掉了，停止位摆的是 1 个。发一个字节实际占的是 1 个起始位、8 个数据位、1 个停止位，线位的总数凑成 10 个。咱们拿 115200 去除一下，一个字节的线时间约 86.8 微秒，每秒塞进去的字节顶到 11520 个了。这个 86.8 微秒咱们留着，下一篇算缓冲容量的时候它还要出场。

线上的节拍是分频器压出来的。RM0008（ST 的 STM32F1 参考手册）§27.3.4 写的公式是 baud = fCK / (16 × USARTDIV)，fCK 是这个 USART 挂着的总线时钟，USART1 挂的是 APB2（AHB 之下挂外设的一条总线），拿的是 PCLK2（APB2 的总线时钟），USART2 与 USART3 挂的是 APB1（AHB 之下另一条挂外设且慢上一档的总线），拿到的是 PCLK1（APB1 的时钟）<RefLink :id="1" preview="STMicroelectronics, RM0008" />。两条总线的时钟都从 SYSCLK（system clock 的缩写，芯片主时钟的意思）分出来，中间过的那道主干是 AHB（advanced high-performance bus 的缩写，高速主干总线的意思）。板子的时钟树咱们照[起步篇《时钟门控：灯不亮，从时钟查起》](../01-led/03-rcc-clock-gate.md)的推演摆一遍，05_uart 的配置对的就是这套档位：

```text
HSI 8MHz / 2 × 16 → SYSCLK 64MHz → AHB /1 = HCLK 64MHz
HCLK → APB1 /2 = PCLK1 32MHz（USART2、USART3 的时钟）
HCLK → APB2 /1 = PCLK2 64MHz（USART1 的时钟）
```

`05_uart` 的 `SystemClock_Config` 里 `APB1CLKDivider = RCC_HCLK_DIV2`、`APB2CLKDivider = RCC_HCLK_DIV1`，咱们把源码的档位跟上面的推演对着核了一遍，是分毫不差的。USART1 的算术咱们一步步来：

```text
USARTDIV = 64MHz / (16 × 115200) = 34.72
DIV_Mantissa（整数部分，BRR 第 15:4 位）= 34
DIV_Fraction（小数部分，BRR 第 3:0 位）  = 0.72 × 16 ≈ 12
USARTDIV 实值 = 34 + 12/16 = 34.75
实际波特率   = 64MHz / (16 × 34.75) = 64MHz / 556 = 115107.9
```

小数不是想摆多细就能多细的，BRR（baud rate divider 的缩写，波特率的分频寄存器）只给了 4 位小数，凑整的精度到 1/16<RefLink :id="1" preview="STMicroelectronics, RM0008" />。凑整之后实际跑出来的是 115107.9 波特，跟 115200 差出来的误差是 0.08%，UART 的收发对这么小的偏差不敏感，这个量级咱们用着放心。USART2 若是挂到 APB1 的 32MHz 上，同一个公式算出来的整数是 17、小数是 6，BRR 摆的是 278，实际的波特率还是 115107.9，误差同样是 0.08% 的量级，两条总线的分频档位不同，凑整后的落点碰到了一起。

这套算术咱们手算完了，固件里是谁干的？HAL 在 `UART_SetConfig` 里用同一套公式现算，`UART_BRR_SAMPLING16` 宏拿时钟乘 25、再除 4 倍的波特率，整数与小数是分开来凑的，凑完了拼进 BRR，咱们代进去的得数正是整数 34 加小数 12。`Uart` 模板把这一层包掉了，装配的人只报一个波特率：

```cpp
struct UartConfig {
    std::uint32_t baud = 115200;
    WordLength word_length = WordLength::Bits8;
    StopBits stop_bits = StopBits::One;
    Parity parity = Parity::None;
};
```

`uart_types.hpp` 里的缺省值 115200 8N1 就是咱们控制台的配置。字长与校验的组合有一道编译期的关卡，`uart.hpp` 的断言咱们原样搬：

```cpp
static_assert(CONFIG.word_length != uart::WordLength::Bits9 ||
                  CONFIG.parity == uart::Parity::None,
              "F1 cannot send nine data bits plus parity");
```

9 位字长加校验位的组合为什么过不了编译？咱们把 PCE 的语义翻出来就明白了。校验位不吃额外的线位，它挤在字长的最高位上，咱们翻 RM0008 §27.6.4，里面对 PCE（parity control enable 的缩写，校验控制的使能位）的记载是校验位插在 MSB 的位置上，M=1 的时候占的是第 9 位，M=0 的时候占的是第 8 位<RefLink :id="1" preview="STMicroelectronics, RM0008" />。字长摆 9 位再开校验的话，9 个数据位里就只剩 8 位能用了，想要 9 个数据位的人拿到的是 8 个，配置写错这件事在编译期就说明白了。

## Uart 模板：把轮询收发包进类型

收发的类型咱们看装配。`main.cpp` 的别名只有一行，模板参数带的是实例编号与配置：

```cpp
using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
```

`UartInstance` 按外设基地址取值，USART1 对应的地址是 0x40013800，配置不带的话走的就是缺省的 115200 8N1。`init()` 的次序咱们照源码念：开 USART1 的时钟、配引脚、填 HAL 句柄、`HAL_UART_Init` 收尾：

```cpp
static void init() {
    enable_clock();
    init_pins();

    auto& handle = detail::UartState<INSTANCE>::handle;
    handle.Instance = reinterpret_cast<USART_TypeDef*>(static_cast<std::uintptr_t>(INSTANCE));
    handle.Init.BaudRate = CONFIG.baud;
    handle.Init.WordLength = word_length();
    handle.Init.StopBits = stop_bits();
    handle.Init.Parity = parity();
    handle.Init.Mode = UART_MODE_TX_RX;
    handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    handle.Init.OverSampling = UART_OVERSAMPLING_16;
    check(HAL_UART_Init(&handle));
}
```

引脚配在哪是编译期定好的，咱们念一遍 `pin_map()` 的答案，它是编译期求值的 consteval 函数，三个实例各配一组：USART1 的 TX 落在 PA9、RX 落在 PA10，USART2 落的是 PA2 与 PA3，USART3 落的是 PB10 与 PB11。PA9 与 PA10 这组跟 RM0008 §9.3.8 的重映射表对得上，`USART1_REMAP = 0` 的时候 TX 就是 PA9、RX 就是 PA10<RefLink :id="1" preview="STMicroelectronics, RM0008" />。TX 配的是复用推挽（引脚交给外设驱动），RX 配的是输入上拉（线空闲时被拉到高电平，UART 的空闲态本来就是高）。

收发的主体是轮询，咱们看它是怎么切的。`send` 与 `receive` 走的都是 `transfer` 的路子，它拿数据切成一段一段的，一段一段地喂给 HAL：

```cpp
template <typename Span, typename Operation>
static void transfer(Span data, Operation operation) {
    constexpr auto max_chunk = std::numeric_limits<std::uint16_t>::max();
    auto& handle = detail::UartState<INSTANCE>::handle;
    for (std::size_t offset = 0; offset < data.size();) {
        const auto remaining = data.size() - offset;
        const auto count =
            static_cast<std::uint16_t>(remaining > max_chunk ? max_chunk : remaining);
        check(operation(&handle, data.data() + offset, count));
        offset += count;
    }
}
```

`send` 喂的轮询函数是 `HAL_UART_Transmit`，`receive` 喂的是 `HAL_UART_Receive`，超时参数给的都是 `HAL_MAX_DELAY`。咱们把 HAL 的姿势念一遍，两个函数是一样的死等，等的是状态寄存器里的旗位，TX 方向等的是 TXE（transmit data register empty 的缩写，发送寄存器空的旗位），RX 方向等的是 RXNE（read data register not empty 的缩写，接收寄存器非空的旗位），等到了就搬一个字节，超时与否拿的是 `HAL_GetTick()` 的毫秒差。`HAL_MAX_DELAY` 的值是 0xFFFFFFFF，超时判断被它跳过去了，所以这里的语义就是没有上限地等。分段的上限 65535 来自 HAL 的计数参数只有 16 位，长过一段的数据切着送。

咱们把 `check` 的两行单拿出来念，注释是库作者的原话：

```cpp
static void check(HAL_StatusTypeDef status) {
    if (status != HAL_OK) {
        // Preserve the void contract: returning here would claim a full transfer.
        __builtin_trap();
    }
}
```

咱们把 `send` 的合同摊开看。它的返回类型是 void，函数正常返回就等于宣布数据全送出去了，HAL 要是报了错，这个宣布就是假的，与其拿一个骗人的正常返回，不如当场 `__builtin_trap()` 把失败的现场留在原地。错误的路只有一条，走到头的都是停机，这是阻塞式 API 的取舍。

轮询收发还有一条带超时上限的支路 `try_send`，它给那些不能陪着断线一起挂死的调用方用，注释把公式的设计讲得很全，咱们整个搬过来：

```cpp
// Best-effort path for callers that must not die on a dead line
// (logging): bounded timeout instead of HAL_MAX_DELAY — with the UART
// clock off, HAL_MAX_DELAY would hang forever; a bound turns the hang
// into a false return. Timeout derives from the compile-time baud:
// 8N1 = 10 line bits per byte, doubled for margin (HAL polls in 1 ms
// ticks; a slow-but-alive line must not time out).
static bool try_send(std::span<const std::byte> data)
    requires(CONFIG.word_length == uart::WordLength::Bits8)
{
    auto& handle = detail::UartState<INSTANCE>::handle;
    constexpr auto max_chunk = std::numeric_limits<std::uint16_t>::max();
    for (std::size_t offset = 0; offset < data.size();) {
        const std::size_t remaining = data.size() - offset;
        const auto count =
            static_cast<std::uint16_t>(remaining > max_chunk ? max_chunk : remaining);
        const std::uint32_t timeout =
            static_cast<std::uint32_t>(count) * 20000 / CONFIG.baud + 10;
        if (HAL_UART_Transmit(&handle,
                              reinterpret_cast<const std::uint8_t*>(data.data() + offset),
                              count, timeout) != HAL_OK) {
            return false;
        }
        offset += count;
    }
    return true;
}
```

超时公式的数咱们当面算一遍。一个字节占的是 10 个线位，每字节的线时间是 `10000/baud` 毫秒，公式里用的是 `20000/baud`，正好留了一倍的余量，后面还垫了 10 毫秒的底。拿 115200 代进去的话，送 128 个字节的超时上限约 32 毫秒，这段数据的真实线时间约 11 毫秒，上限是它的三倍，慢而不断的线不会被误判，真断了线也只等这么一会儿，`false` 就回来了。谁在用它？日志库的 `UartLogSink` 走的也是带超时的路子，`sinks.hpp` 里把 `try_send` 注明成 best-effort 的路径，日志的调用方不能陪断线挂死，这个立场咱们在[《把日志用起来：两种写法与两个观看面》](../06-patterns/07-logger-dual-syntax-usage.md)已经认过一回。

## 主循环：一字节一停的行读取

装配完咱们就进主循环。发送侧的助手只有四行，字符串视图是切成字节段再交出去的，接手的是 `Serial::send`：

```cpp
static void write(std::string_view text) {
    Serial::send(std::as_bytes(std::span<const char>{text.data(), text.size()}));
}
```

命令的分发是一条 if 链，咱们原样搬：

```cpp
static void run_command(std::string_view line, bool& led_on) {
    if (line == "help") {
        write("Commands: help, ping, led on, led off, status, echo TEXT\r\n");
    } else if (line == "ping") {
        write("pong\r\n");
    } else if (line == "led on") {
        Led::on();
        led_on = true;
        write("LED on\r\n");
    } else if (line == "led off") {
        Led::off();
        led_on = false;
        write("LED off\r\n");
    } else if (line == "status") {
        write(led_on ? "LED: on\r\n" : "LED: off\r\n");
    } else if (line.starts_with("echo ")) {
        write(line.substr(5));
        write("\r\n");
    } else if (!line.empty()) {
        write("Unknown command. Type help.\r\n");
    }
}
```

分发的内容咱们念起来就是逐条比对、逐条回话，`echo` 拿 `starts_with` 把前缘认了出来，再截出后面的正文，认不出的给一句提示。空行什么都不回，这是固件对连敲两次回车的人的礼貌。命令清单里的 `led on` 与 `led off` 动的是 PC13 那颗板载灯，起步篇的老朋友，01 号站点亮过的就是同一颗，`status` 回读的是一个布尔。

行读取循环是整个示例里最要紧的一段，咱们把它整段搬过来：

```cpp
std::array<char, 48> line{};
std::array<std::byte, 1> input{};
std::size_t length = 0;
bool led_on = false;
bool overflow = false;
bool after_cr = false;

for (;;) {
    Serial::receive(input);
    const auto ch = std::to_integer<unsigned char>(input[0]);

    if (ch == '\n' && after_cr) {
        after_cr = false;
        continue;
    }
    after_cr = ch == '\r';

    if (ch == '\r' || ch == '\n') {
        write("\r\n");
        if (overflow) {
            write("Line too long.\r\n");
        } else {
            run_command({line.data(), length}, led_on);
        }
        length = 0;
        overflow = false;
        write("> ");
    } else if (ch == '\b' || ch == 0x7f) {
        if (length > 0 && !overflow) {
            --length;
            write("\b \b");
        }
    } else if (ch >= 0x20 && ch <= 0x7e) {
        if (!overflow && length < line.size()) {
            line[length++] = static_cast<char>(ch);
            Serial::send(input);
        } else {
            overflow = true;
        }
    }
}
```

循环的头一句 `Serial::receive(input)` 是阻塞的，没字节来的时候主循环就挂在它身上，咱们跟着字节走一遍。`HAL_UART_Receive` 对着 RXNE 的旗位死等，字节到了手，后面的路分成三条：可打印字符（0x20 到 0x7e 的区间）进缓冲并原样回显，退格（`\b` 或 0x7f）把长度倒回去并回显三个字节的 `\b \b`（退格、空格、退格，屏幕上的效果是那个字被抹掉了），回车或换行把攒好的行交付出去。

回车与换行一起来的场合咱们得多看一眼，这里有个小机关：不少终端发的是 `\r\n` 两个字节，要是每个都被当成了行尾，一行命令就得做两回的分发。`after_cr` 这个布尔记下的是上一个字节是不是回车，换行跟着回车来的时候就跳过，代码里头一个 if 干的就是这件事。

48 这个数字是缓冲的容量，也是本节标题里的边界。可打印字符那一路的判断 `length < line.size()` 挡住第 49 个字符，被挡的这个字符不进缓冲，回显的份也没有，固件只把 `overflow` 立了起来，真正的报错要等回车才给：`Line too long.`。报错挪到行尾是有讲究的，超长行的前 48 个字符跟合法行的回显长得一模一样，用户在回车之前拿到的反馈只有回显的个数，咱们到实验场里数给您看。

## 实验场：PTY 里跑控制台

仿真的装配咱们过一遍，它写在 `renode.resc` 里：Renode 是一套开源的仿真器，`sim/stm32f1/bluepill.repl` 描述咱们这块 F103 板，`CreateUartPtyTerminal` 把 usart1 接到 PTY 的口子上：

```text
emulation CreateUartPtyTerminal "uartTerminal" $uart_pty true
connector Connect usart1 uartTerminal
```

PTY 的默认路径是 `/tmp/libestdx-uart`，`$uart_pty?=` 带问号的写法是可覆盖的默认值，校验脚本就是这么把自己的临时路径塞进去的。手动玩的姿势在库的 README 里写着，咱们照着来，`sim_uart_example` 起的是仿真，咱们另开一个终端 `screen /tmp/libestdx-uart 115200`，屏幕就是咱们的了。

笔者把整场会话实抓了一遍，前面引言里看过打招呼的两行，下面贴的是从 `help` 一路到超长行的完整转录，咱们一个字节都没删：

```text
> help
Commands: help, ping, led on, led off, status, echo TEXT
> ping
pong
> echo hello console
hello console
> led on
LED on
> status
LED: on
> nope
Unknown command. Type help.
> pinh\b \bg
pong
> xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
Line too long.
>
```

咱们逐段看。`help`、`ping`、`echo hello console` 三段是 if 链的直译，敲什么回什么。`led on` 之后跟的是 `status`，回读的是 `LED: on`，命令动过了布尔，回读的值就跟着变了。`nope` 走到 if 链的最后一格，`Unknown command. Type help.` 就是空命令之外唯一的兜底话。

`pinh\b \bg` 的转录值得单拿出来看，`\b` 是 0x08 退格字节的转写，笔者手滑把 `ping` 敲成了 `pinh`，补了一个 0x7f 的退格再敲 `g`。固件收退格时把长度倒回去、回显 `\b \b` 的三个字节，屏幕上的 `h` 被抹掉了，`g` 补进原来的位置，回车之后分发的就是修好的 `ping`，回的 `pong` 是最硬的证据。抹字的动作发生在固件侧，行缓冲里存的是修完的版本，终端那头的原始字节流里 `h` 与 `g` 两个都来过。

`x` 的长行是 48 这个边界的实证。笔者往里打了 49 个 `x`，转录里回显出来的只有 48 个，第 49 个字符把 `overflow` 立了起来，回显就静悄悄地少了一个，这个缺席就是边界给的第一条提示。回车落下的时候，报错是在那之后才来的：`Line too long.`。报错等的是回车，报完了清零重来，缓冲的行为就这么三条。

自动化校验跑的是 `check_uart_renode` 目标，底下的 `check_renode.py` 把上面的场景全排成了断言，逐字符的回显要逐个比对，CRLF 算的是一行而不是两行，退格修字之后必须回的是 `pong`，49 个字符之后必须报的是 `Line too long.`。实跑的输出咱们原样贴：

```text
Renode UART CMD: b'UART CMD ready\r\nType help for commands.\r\n> '; commands, CRLF and line editing passed
```

固件的尺寸笔者也量了，`arm-none-eabi-size` 的结果：text 5476、data 12、bss 76。一条 if 链加一个 48 字节的数组，RAM 的常驻开销 88 字节，这个尺寸是咱们后面所有对照的起点。

## 阻塞的边界：主循环忙起来会丢什么

转录里的一切看着都好，不过好背后的条件咱们得讲明白。整场会话笔者是一个字符一个字符敲的，人手的速率在每秒几个字符的量级，控制台等一个字节最多等一个 86.8 微秒的线时间，绝大多数的时刻它都闲着挂在 `receive` 上。阻塞的代价在人手的节奏下根本显不出来，因为控制台本来就是主循环唯一的正经活。

咱们换个条件试试。假设主循环有了第二个正经活，比如一段要忙 300 毫秒的计算，收发还压在同一个循环里的话，忙起来的 300 毫秒里 `receive` 一遍都不跑，您在这段时间敲的字节最多只剩两个有地方待，待的位置是接收寄存器与移位寄存器，一边一个地待着。RXNE 还没被读走的时候下一个字节到了，硬件就把过载的旗位立了起来，旗位的名字是 ORE（overrun error 的缩写，过载错误的意思），报的就是过载，RM0008 对 ORE 的注解是接收寄存器里的内容不丢、移位寄存器被后来的接收覆盖，多出来的字节就这么没了。人手在 300 毫秒里是敲不出 63 个字节的，一台自动化的对端按线速发，63 个字节的发送只占 63 × 86.8 微秒 ≈ 5.5 毫秒，转眼就送完了，到时候丢的就是成片的字节。

这个边界就是 libestdx 的 `examples/08_uart_ring`（08 号示例）的出发点：把收字节这一步挪进中断，主循环忙它的，字节就有别人接了。[《阻塞收发的对照篇：RXNE 中断与 64 字节环形缓冲》](02-interrupt-ring-console.md)拿着同一个命令控制台重演了一遍，咱们在那里见。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 终端满屏乱码 | 波特率对不上，两端的分频各算各的 | 两端都按 115200 配，时钟树核实了再算 BRR |
| 敲字没回显，`ping` 也没回应 | TX 与 RX 接反了，PA9 是固件的 TX 要接适配器的 RX | 对照 RM0008 的重映射表，TX 与 RX 交叉着接 |
| 一行命令执行了两回 | 终端发的是 `\r\n` 两个字节，每个都被当成了行尾 | 照 `after_cr` 的写法把跟着回车来的换行跳过 |
| 编译报 `F1 cannot send nine data bits plus parity` | 9 位字长与校验位同时配置了，校验位要占走最高位 | 校验按需求二选一，9 个数据位与校验不能兼得 |
| `receive` 永远不返回 | RX 线悬空或对端没发，`HAL_MAX_DELAY` 没有超时 | 接线核实了再跑，或者换带超时的接收方案 |
| 第 49 个字符敲上去没反应 | 48 字节的缓冲已满，`overflow` 已立起，回显被静默跳过 | 到回车拿 `Line too long.` 的报错，回头重敲这一行 |
| `send` 之后固件停机不跑 | HAL 返回了非 OK，`check` 按 void 的合同当场 trap | 查 HAL 的入参与外设状态，trap 是设计内的失败路径 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="9.3.8 USART1 remapping; 27.3.4 Fractional baud rate generation; 27.6.1 USART_SR; 27.6.3 USART_BRR; 27.6.4 USART_CR1"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（examples/05_uart 与 boards/stm32f1/uart.hpp）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="Uart, UartConfig, try_send 有界超时, consteval pin_map"
  />
</ReferenceCard>
