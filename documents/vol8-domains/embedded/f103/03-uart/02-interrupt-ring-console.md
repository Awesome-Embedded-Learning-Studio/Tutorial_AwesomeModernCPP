---
title: "阻塞收发的对照篇：RXNE 中断与 64 字节环形缓冲"
description: "examples/08_uart_ring 的 221 行把收字节挪进 USART1 中断，UartRx<Usart1,64> 的 start(3) 摆 HAL_NVIC_SetPriority 的抢占等级并置 CR1 第 5 位 RXNEIE，irq() 一次读 SR 分三路：RXNE 读 DR、FE/NE 丢带伤字节、ORE 读 DR 清过载，字节过 SpscQueue 的 64 格无锁环（2 的幂掩码回绕，空一格换 head==tail 的空判，元素放完才动索引，volatile 单写者索引在无数据缓存的 M3 上免屏障），63 格容量按 86.8 微秒线位时间约合 5.5 毫秒的缓冲余量，主循环 try_pop 非阻塞消化，解析换 std::expected<Command, CommandError> 加 std::variant 分派，错误带证据返回（unknown command: frobnicate 与 not a number: xyz 的 token 都在 warn 日志里），300 毫秒的 HAL_Delay 里敲进来的 ping 与 echo 在唤醒后按序回话，Renode 实抓对拍，text+data 从 5488 涨到 20044 字节，data+bss 从 88 涨到 460 字节"
chapter: 3
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 循环缓冲区
  - expected
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 12
prerequisites:
  - "阻塞收发的命令控制台：UART 帧的分频算术与 48 字节行缓冲"
related:
  - "谁打断谁：NVIC 抢占等级怎么排"
  - "一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍"
---

# 阻塞收发的对照篇：RXNE 中断与 64 字节环形缓冲

## 忙 300 毫秒，字节一个都不丢

前一篇[《阻塞收发的命令控制台：UART 帧的分频算术与 48 字节行缓冲》](01-blocking-console.md)收在阻塞的边界那里：主循环要是忙上 300 毫秒，收发还压在同一个循环里的话，过载旗位 ORE 立了起来，多出来的字节就没了。本篇接的就是这个题目，素材换成 libestdx 的 `examples/08_uart_ring`<RefLink :id="2" preview="Awesome-Embedded-Learning-Studio, libestdx" />，221 行的 `main.cpp` 把中断、环形缓冲与带证据的命令解析全装了进去，咱们固定在提交 8026841 上看它，本篇贴出的终端输出都是笔者在 Renode 的 PTY 里实抓的原文。

咱们直接看效果。笔者的抓取脚本把三条命令一口气写进 PTY，`sleep 300` 排在了最前头，`ping` 与一条长 `echo` 跟在了后面，三行之间咱们不做任何等待：

```text
> sleep 300
[311ms][Info ][sleep] busy 300
[612ms][Info ][sleep] done
> ping
[612ms][Info ][ping] pong
> echo burst-canary-012345678901234567890123456789
[612ms][Info ][echo] burst-canary-012345678901234567890123456789
```

`busy 300` 与 `done` 之间的 301 毫秒（311 到 612）就是 `HAL_Delay(300)` 在忙。这段时间里 `ping` 与 `echo` 的字节早就到了线上，转录里却看不见回显的影子，回显是主循环的活，它忙得顾不上了。丢是没有丢的，中断把每一个都收进了环里待着。`done` 落了地，主循环回到收字节的循环上，把攒下的两行按原来的次序回放，`pong` 与 echo 的正文盖着同一个 [612ms] 的时间戳，攒下的活消化起来就是快。时间戳是固件侧 `HAL_GetTick()` 的毫秒数，开机日志与第一条应答之间那段空档是笔者抓取时在等提示符，跟固件的快慢没有关系。

忙碌的 300 毫秒里主循环一遍都没回到收字节的代码上，字节是一个都没有少的，次序也是原样的，这就是标题里两个词的分工：RXNE 中断把字节接住，64 格的环形缓冲把节奏差扛住。咱们把三样东西一个个看过去，中断怎么开，环怎么转，解析怎么把错误带着证据交回来。

## RXNE 中断：字节到了自己来叫

阻塞写法是主循环拿着 `receive` 死等旗位的路数，中断写法把这个等待反了过来，您敲的字节一到，硬件就过来叫咱们了。装配还是一行别名的事：

```cpp
using Rx = estdx::stm32f1::UartRx<estdx::stm32f1::UartInstance::Usart1, 64>;
```

模板参数带的是实例编号与环的容量，64 这个数是有讲究的，它得是 2 的幂，库用 static_assert 把了关，不是 2 的幂直接编不过。上电的装配序列里 `Rx::start(3)` 跟在 `Serial::init()` 之后，`uart_irq.hpp` 里那三行咱们照源码念：

```cpp
void start(std::uint32_t preempt) {
    HAL_NVIC_SetPriority(irq_number(), preempt, 0);
    HAL_NVIC_EnableIRQ(irq_number());
    usart()->CR1 |= USART_CR1_RXNEIE;
}
```

三行的事咱们挨着念。头一行摆的是等级，HAL 在 `HAL_Init` 里已经把分组摆成了 `NVIC_PRIORITYGROUP_4`，4 位的份量全划给抢占等级了，从属的等级一位都不留，可摆的档位一共 16 档，所以 `start` 的第二个参数整个落在抢占等级上。等级的数字越小越急，3 摆的是个中间档，更急的中断能打断它，它也能打断更缓的。等级机制的全貌（嵌套、同级挂起、分组阶梯）04 号时间站的[《谁打断谁：NVIC 抢占等级怎么排》](../04-time/04-nvic-preempt-priorities.md)里单独讲过，本篇拿 start(3) 这个中间档就够用了。

第二行与第三行咱们接着念。第二行把 USART1 的中断线使能了，编号由 `irq_number()` 按实例取：USART1 在 CMSIS（ARM 给 Cortex-M 内核写的寄存器访问层，Cortex Microcontroller Software Interface Standard）里的编号是 37（`stm32f103xb.h` 里的 `USART1_IRQn`），算上排在前面的 16 个系统异常，它在 Cortex-M3 异常表里的序号是 53，向量的地址是 0x0000_00D4，RM0008 的向量表（Table 63）里这一行就这么记着<RefLink :id="1" preview="STMicroelectronics, RM0008" />。第三行才是 USART 本体的事：CR1 的第 5 位 RXNEIE 立起来，之后 RXNE（read data register not empty 的缩写，接收寄存器非空的旗位）一被硬件置位，USART1 就向 NVIC 发出了中断请求。ORE（overrun error 的缩写，过载错误的旗位）走的是同一条请求线，所以过载的告知也从这个中断进来。

向量表里的名字是启动文件定的，`startup_stm32f103xb.s` 把 `USART1_IRQHandler` 声明成了弱符号，弱符号是链接器留的占位定义，固件给出同名的实定义时它就让位，咱们给出同名的定义，中断就落到咱们的函数上：

```cpp
extern "C" void USART1_IRQHandler() {
    Rx::irq();
}
```

咱们把函数体写成一行，转手交给了 `Rx::irq()`。中断里干的活越少越好，这是 ISR 的纪律，`irq()` 里没有日志、没有回显、没有任何会等的调用，它把字节放进环里就退了场，把主循环的时间还给主循环。

## irq() 的三路：一次读 SR，各走各的

`irq()` 的全貌在 `uart_irq.hpp` 里，十几行的分量，咱们整段搬过来：

```cpp
void irq() {
    auto* const regs = usart();
    const auto sr = regs->SR;
    if (sr & USART_SR_RXNE) {
        const auto data = static_cast<std::byte>(regs->DR);
        if ((sr & (USART_SR_FE | USART_SR_NE)) == 0) {
            (void)ring.push(data);
        }
    } else if (sr & USART_SR_ORE) {
        (void)regs->DR;
    }
}
```

头一步读的是 SR（status register 的缩写，状态寄存器的意思），旗位的快照落在 `sr` 里，后面各分支的判断用的都是快照里的值。读一次快照有它的道理：DR 一动旗位就变，咱们拿快照做判断，中途变卦的事就扰不到咱们。三个分支各走各的路。

RXNE 分支走的是正路。字节在 DR（data register 的缩写，数据寄存器的意思）里等着，读 DR 的动作本身就把 RXNE 清掉了，这是 F1 的硬件约定，RM0008 对 RXNE 的清除写的就是这个<RefLink :id="1" preview="STMicroelectronics, RM0008" />。字节到手之后还得查快照里的 FE（framing error 的缩写，帧错误的旗位）与 NE（noise error 的缩写，噪声错误的旗位），两个里头有一个立着的话，说明这一字节的线位摆错了或采样时被干扰了，固件把它丢掉了，进环的资格都没有，只有干净的字节才被 `push` 进环，`(void)` 扔掉的返回值咱们放到环的小节里说。

ORE 分支是过载的善后。RM0008 对 ORE 的清除配方是读 SR 之后跟着读 DR<RefLink :id="1" preview="STMicroelectronics, RM0008" />，平常的路径里 RXNE 分支一读 SR 一读 DR，ORE 要是在这之前立的，这套组合顺手就把它清了。这个分支兜的是读快照与读 DR 之间那个窄窗口：ORE 要是在快照落地之后、DR 读过去之前立了起来，清除的序列就没有走全，硬件为 ORE 单独叫的一回中断就落在这个分支上，进来读的都是 DR，把清除的序列走完，过载就清掉了。ORE 的语义咱们照 RM0008 的条文说，它立起来的时候接收寄存器里的内容不丢，移位寄存器被后来的接收覆盖，真正少掉的是没来得及进接收寄存器的那个字节。

FE 与 NE 两条路、ORE 的善后路，笔者得跟您交代证据的边界，这几条路在 Renode 的实验里喂不出来。PTY 是个干净的通道，Renode 的终端模型送出来的帧个个是好的，帧错误与噪声错误没有注入的口子。过载这边咱们另说，环有 63 格的余量，中断响应是微秒的量级，靠敲字把它撑爆是不现实的。所以这几段的依据是源码的分支与 RM0008 的条文，不是注入出来的现场，咱们把它们算作设计里的防线，这几个分支在实际跑的实验里一次也没触发过，这话笔者说在了明处。

## 64 格的环：一个写一个读，谁也不等谁

字节进环的是中断，出环的是主循环，两头顶着的是同一个数据结构，`spsc_queue.hpp` 的开头把合同写得很全，咱们大段搬过来：

```cpp
// Single-producer single-consumer queue, no locks, no atomics.
//
// Concurrency contract: exactly ONE context calls push() (typically an ISR)
// and exactly ONE other context calls pop() (typically the main loop).
// Adding a second producer or consumer voids the contract — that needs
// critical sections, not this class.
//
// Why plain volatile indices suffice on a single-core Cortex-M: each index
// has exactly one writer, aligned 32-bit loads/stores are single bus
// accesses (no tearing), and a reader observing a stale index merely
// revisits "empty"/"full" — it can never see a half-written element, because
// the writer publishes the index AFTER storing the element. volatile only
// stops the compiler from caching indices in registers across the ISR
// boundary; it is not a memory barrier and not needed as one here (no data
// cache on the M3, and code ordering within push/pop is fixed by the
// data dependency).
//
// Why one slot stays unused: head == tail unambiguously means empty, and
// full is detected one slot early. The classic alternative — an extra count
// variable — buys back the slot but pays with a second shared variable that
// both sides must reason about. One spare byte of RAM is the cheaper trade.
template <typename T, std::size_t N>
class SpscQueue {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of two (mask-based wrap)");
    static_assert(std::is_trivially_copyable_v<T>, "elements cross an ISR boundary by plain copy");

public:
    // Producer side (ISR). Returns false when full; the caller owns the
    // overflow policy (drop, count, overwrite — but never block in an ISR).
    bool push(const T& value) {
        const auto head = head_;
        const auto next = wrap(head + 1);
        if (next == tail_) {
            return false;
        }
        storage_[head] = value;
        head_ = next;  // element first, index last — the release order readers rely on
        return true;
    }

    // Consumer side (main loop). Returns false when empty.
    bool pop(T& out) {
        const auto tail = tail_;
        if (tail == head_) {
            return false;
        }
        out = storage_[tail];
        tail_ = wrap(tail + 1);
        return true;
    }
```

名字里的 SPSC 是 single-producer single-consumer 的缩写，指的是单生产者对单消费者的组合。注释的第二条写的就是合同本身：`push` 的调用方只有中断，`pop` 的调用方只有主循环，谁多来了一个，这个类就不作数了，那是临界区的活，不是它的活。类里剩下的几个成员（`empty`、`available`、`capacity` 与私有的存储）都是小查询，咱们在正文里用到哪句说哪句。

容量的算术咱们当面算。N 摆的是 64，`wrap` 拿 `index & 63` 做的是回绕，模运算被一个与门替掉了，这是 N 必须是 2 的幂的原因。64 格的数组真正可用的只有 63 格，head 与 tail 的相等是空判，满判提前了一格（`next == tail_` 就算满了）。注释里把另一条路也摆了出来，额外的 count 变量能把空着的一格省回来，代价是两边都得为一个共享变量多操心了，库选的是空一格的便宜方案。63 格乘上 86.8 微秒的线位时间（前一篇算过的每字节 10 线位），环能把约 5.5 毫秒的流量整个接住，这句算术有两种用法：持续的流（线速拉满的每秒 11520 个字节）里环撑不了几毫秒，主循环得不停地转、随时清环。短促的爆发（`sleep` 之后脚本一口气写进来的 54 个字节，`ping` 连它的回车占 5 个，`echo` 连正文与回车占了 49 个）里环把它们全数接住了，主循环回来得再晚也不怕了。

两个索引的写者各是一个，`head_` 的写者只有中断，`tail_` 的写者只有主循环，volatile 修饰的就是它们。这里的门道注释讲得很全，咱们用自己的话收拢一遍：单核的 M3 上对齐的 32 位读写一次总线就完成了，扯是扯不开的。volatile 挡的是编译器把索引缓存在寄存器里跨中断用的行为，它不是当内存屏障用的，屏障在这里也是多余的，因为 M3 的身上没有数据缓存，push 与 pop 内部的次序被数据依赖管着。读的人拿着旧索引会怎么样？顶多把空判成空、把满判成满，回过头再瞧一眼就是了，写的人是元素放完了才放索引，读的人永远碰不见半个元素。

`push` 满了返回 false，环自己是不阻塞的，覆盖也不是它的选项，注释写明丢不丢是调用方的政策，咱们把 `irq()` 的选择接着念。`irq()` 里那个 `(void)ring.push(data)` 把返回值扔了，环满的时候字节就悄悄地少了一个，计数的活固件也没有做，吭声就更没有了。这是设计里的取舍，63 格的余量算出来，就是为了让满这个分支在实际的敲字节奏里摸不着。`push` 的那头是中断，主循环这头的口子是 `try_pop`，它包的就是 `ring.pop`，环空了立刻返回 false，主循环一刻也不等它的。

## 解析换 expected，错误带着证据回来

前一篇的 `run_command` 是一条 if 链就地回话，认不出的回一句笼统的 `Unknown command. Type help.`，哪个词不认识是不告诉您的。示例 08（`examples/08_uart_ring`）把认与办分成了两步，`parse_line` 做的是认命令的活，认出来的结果装进 std::variant，认不出的错误装进 std::expected 的 error 位里，连证据也一并带上了。错误与命令的类型咱们照源码搬：

```cpp
enum class ParseError { Unknown, MissingArg, BadNumber };

// 错误带证据: 哪个词不认识/缺的参数名
struct CommandError {
    ParseError code;
    std::string_view token;
};

struct Help {};
struct Ping {};
struct LedOn {};
struct LedOff {};
struct Status {};
struct Echo {
    std::string_view text;
};
struct Sleep {
    std::uint32_t ms;
};
using Command = std::variant<Help, Ping, LedOn, LedOff, Status, Echo, Sleep>;
```

咱们把每条命令配一个小结构，七个结构装进了一个 std::variant，它是 C++17 起的标准件，它的本领是让一个变量在几个类型里取其一。`parse_line` 的骨架还是 if 链，返回类型换成了 `std::expected<Command, CommandError>`，C++23 起的标准件，expected 把有值与有错误的两个分支装进了同一个返回类型，本篇 frontmatter 的 cpp_standard 摆的 23 就是冲着它：

```cpp
std::expected<Command, CommandError> parse_line(std::string_view line) {
    if (line == "help") {
        return Help{};
    }
    if (line == "ping") {
        return Ping{};
    }
    if (line == "led on") {
        return LedOn{};
    }
    if (line == "led off") {
        return LedOff{};
    }
    if (line == "status") {
        return Status{};
    }
    if (line.starts_with("echo ")) {
        return Echo{line.substr(5)};
    }
    if (line.starts_with("sleep ")) {
        const auto arg = line.substr(6);
        std::uint32_t ms = 0;
        const auto result = std::from_chars(arg.data(), arg.data() + arg.size(), ms);
        if (arg.empty() || result.ec != std::errc{} || result.ptr != arg.data() + arg.size()) {
            return std::unexpected(CommandError{ParseError::BadNumber, arg});
        }
        return Sleep{ms};
    }
    return std::unexpected(CommandError{ParseError::Unknown, line});
}
```

sleep 的分支咱们单独念一遍。参数是从 `line.substr(6)` 里出来的，数字解析用的是 `std::from_chars`，它不沾 locale 的边，异常也是不抛的，解析失败的原因摆在 `result.ec` 里，`result.ptr` 没走到参数末尾就说明尾巴上还挂着不是数字的字符，两样合起来一查的话，`BadNumber` 连着出问题的原文一起返回，`token` 里装的就是那个参数。认不出的命令走最后一行，`Unknown` 连着整行的原文返回。

主循环拿到解析的结果之后的走法咱们看代码，有值的话走 `execute(*parsed)`，没值的话拿 `parsed.error()` 查错误码：

```cpp
const auto parsed = parse_line({line.data(), length});
if (parsed.has_value()) {
    execute(*parsed);
} else {
    const auto& error = parsed.error();
    switch (error.code) {
    case ParseError::Unknown:
        Log::warn(estdx::logger::Tag("cmd"), "unknown command: ", error.token);
        break;
    case ParseError::MissingArg:
        Log::warn(estdx::logger::Tag("cmd"), "missing argument: ", error.token);
        break;
    case ParseError::BadNumber:
        Log::warn(estdx::logger::Tag("cmd"), "not a number: ", error.token);
        break;
    }
}
```

三种错误摆的是三句 warn，句句带着出问题的原文，哪句话惹的祸您当场就看得见，这是与前一篇最实在的一个差别。

回话的执行在 `execute` 里，`std::visit` 拿着 variant 里的真实类型调进来，咱们把全貌搬过来：

```cpp
void execute(const Command& command) {
    std::visit(
        [&](const auto& cmd) {
            using C = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<C, Help>) {
                Log::info(estdx::logger::Tag("help"),
                          "help, ping, led on, led off, status, echo TEXT, sleep MS");
            } else if constexpr (std::is_same_v<C, Ping>) {
                Log::info(estdx::logger::Tag("ping"), "pong");
            } else if constexpr (std::is_same_v<C, LedOn>) {
                Led::on();
                led_on = true;
                Log::info(estdx::logger::Tag("led"), "on");
            } else if constexpr (std::is_same_v<C, LedOff>) {
                Led::off();
                led_on = false;
                Log::info(estdx::logger::Tag("led"), "off");
            } else if constexpr (std::is_same_v<C, Status>) {
                Log::info(estdx::logger::Tag("led"), led_on ? "LED: on" : "LED: off");
            } else if constexpr (std::is_same_v<C, Echo>) {
                Log::info(estdx::logger::Tag("echo"), cmd.text);
            } else {
                Log::info(estdx::logger::Tag("sleep"), "busy ", cmd.ms);
                HAL_Delay(cmd.ms);
                Log::info(estdx::logger::Tag("sleep"), "done");
            }
        },
        command);
}
```

lambda 的参数是 auto，咱们看 `if constexpr` 在编译期把七条命令的分支各各展开，运行期按真实类型跳到对应的分支上，没有运行时的字符串比对。sleep 的分支里 `HAL_Delay(cmd.ms)` 就地忙等，标题实验里那个 300 毫秒的忙碌就是它。

回话的写法在 08 里还分了两路，`main.cpp` 顶上的注释就一行，咱们原样搬过来：

```cpp
// 终端机械动作(回显/提示符)走裸字节, 信息行一律走 Log
```

终端的机械动作（回显、提示符）走的是 `write_raw` 的裸字节，信息行一律是 `Log::info` 的活。为什么分？咱们把两头的活分开看就清楚了。回显要跟敲进来的字符一个对一个，快是唯一的要求。Log 行带的是 Tag 与时间戳，是给人看的事件记录，走的是 `UartLogSink<Serial>` 的 `try_send`，前一篇讲过的带超时路子，日志的调用方不陪着断线挂死。

## 实验场：忙碌前后的对拍

仿真的装配跟前一篇共用的是同一份板描述，咱们再念一遍装配的两行，`sim/stm32f1/bluepill.repl` 描述的是这块 F103 板，`renode.resc` 里两行把 usart1 接上了 PTY：

```text
emulation CreateUartPtyTerminal "uartTerminal" $uart_pty true
connector Connect usart1 uartTerminal
```

笔者拿抓取脚本把整场会话实抓了一遍，开机、help、ping、忙碌的三连发、两条错误命令，一个字节都没删的原文在这儿：

```text
[0ms][Info ][boot] ring console ready
[0ms][Info ][boot] type help for commands
> help
[301ms][Info ][help] help, ping, led on, led off, status, echo TEXT, sleep MS
> ping
[304ms][Info ][ping] pong
> sleep 300
[311ms][Info ][sleep] busy 300
[612ms][Info ][sleep] done
> ping
[612ms][Info ][ping] pong
> echo burst-canary-012345678901234567890123456789
[612ms][Info ][echo] burst-canary-012345678901234567890123456789
> frobnicate
[1127ms][Warn ][cmd] unknown command: frobnicate
> sleep xyz
[1134ms][Warn ][cmd] not a number: xyz
>
```

咱们逐段看。开机两行 Log 与提示符在 [0ms] 就出来了。help 的应答在 [301ms]，中间的空档是抓取脚本在等 PTY 就绪与提示符，跟固件的快慢没有关系。探路用的 ping 在 [304ms] 回了 pong，收发的链路是通的。`sleep 300` 之后的三连发是引言里逐毫秒讲过的标题段。`frobnicate` 的 warn 带着原文 `unknown command: frobnicate`，`sleep xyz` 的 warn 带着原文 `not a number: xyz`，错误的证据就摆在日志里。

自动化校验跑的是 `check_ring_renode`，底下的 `check_renode.py` 把忙碌三连发排成了断言，唤醒之后的回话要按次序来，错误的日志要带着 token。实跑的输出咱们原样贴：

```text
ring console check: PASS (burst survives 300ms busy main loop, in order; errors carry reasons)
```

固件的尺寸笔者量了，与前一篇的 05 摆在一张表里看：

| 固件 | text | data | bss | text+data（flash） | data+bss（RAM） |
|---|---|---|---|---|---|
| 05_uart 阻塞 | 5476 | 12 | 76 | 5488 | 88 |
| 08_uart_ring 中断化 | 19740 | 304 | 156 | 20044 | 460 |

涨出来的 14556 字节 flash 与 372 字节 RAM 花在了哪，咱们按增量估一估。前一篇的 05 里没有日志库，08 的 `Log` 模板带格式化与 128 字节的缓冲，variant 与 expected 的分派、`from_chars` 也都是 05 里没有的新件，大头在日志的格式化上。中断服务与环本身的代码其实很小，贵的是信息的呈现。

## 取舍：两份清单各管各的场合

两份示例咱们摆在一张表里对看：

| | 05 阻塞 | 08 中断化 |
|---|---|---|
| 收字节的时机 | 主循环拿着 receive 等 | 中断里进环，主循环 try_pop |
| 主循环忙 300 毫秒的下场 | ORE 立起来，字节丢 | 环接住，回来按序消化 |
| API 的形状 | receive 阻塞到有字节 | try_pop 空了立刻 false |
| 错误的回话 | Unknown command. Type help. 一句笼统话 | warn 带着出问题的原文 |
| 中断的成本 | 无 | 一次 ISR 进出加环的读写 |
| flash（text+data） | 5488 字节 | 20044 字节 |
| RAM（data+bss） | 88 字节 | 460 字节 |
| 合适的场合 | 控制台就是主循环唯一的正经活 | 主循环另有要紧的活要干 |

笔者把立场摆在明处。练习与玩具固件用 05 的阻塞就够，控制台本来是主循环唯一的活，代码是简单的，flash 也是省的。主循环一旦有了第二个正经活，08 的结构就回本了，忙碌 300 毫秒而字节一个都没有少，14556 字节的 flash 花得不冤。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 敲字没回显，控制台全无回应 | `USART1_IRQHandler` 没定义，启动文件里的弱符号落进默认处理，字节没人收 | 照 `main.cpp` 补上 `extern "C"` 的定义，函数体转手 `Rx::irq()` |
| `try_pop` 永远是 false | `Rx::start(3)` 没调，RXNEIE 没立，中断不来 | 装配序列里把 `Rx::start` 摆在 `Serial::init()` 之后 |
| 编译报 `capacity must be a power of two` | 环的模板参数不是 2 的幂 | 容量取 64、128 这类 2 的幂 |
| 动了抢占等级分组之后中断行为变怪 | `start` 的第二参语义跟着分组走，`HAL_Init` 的默认是 `NVIC_PRIORITYGROUP_4` | 动分组之前把《谁打断谁：NVIC 抢占等级怎么排》过了一遍 |
| 两个中断都往环里 push | SPSC 的合同是一个生产者配一个消费者 | 加生产者得换方案，临界区或别的队列 |
| 中断里回话或延时 | ISR 里调 `Log` 或 `HAL_Delay`，环的两端一起停 | ISR 只收字节，回话的活归主循环 |
| 拿 `available()` 当精确余量 | 对面的索引在同时动，注释把它写成了提示而不是事实，原话是 `treat this as a hint, not a fact` | 当提示用，容量按 63 格的最坏情形算 |
| 忙碌超过 5.5 毫秒且字节不断流 | 63 格满了之后 `push` 返回 false，字节悄悄少 | 缩短忙碌的段，或者把环加大（容量还是 2 的幂） |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="27.6.1 USART_SR（RXNE/ORE/FE/NE 的置位与清除）; 27.6.4 USART_CR1（RXNEIE）; Table 63 向量表"
  />
  <ReferenceItem
    :id="2"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（examples/08_uart_ring、boards/stm32f1/uart_irq.hpp、base/spsc_queue.hpp）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="UartRx 的 start 与 irq, SpscQueue 单写者索引与空一格的设计, expected 加 variant 的命令解析"
  />
</ReferenceCard>
