---
title: "一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍"
description: "examples/07_button_events 的 117 行 main 把同一根 PA0 接进两条链：轮询消抖链用 Debouncer<20,3> 做 20ms 周期采样加连续三次一致确认，ButtonFsm<700> 把稳态翻成 Press/LongPress/Release 三种 variant 事件交给 std::visit 分派。EXTI 边沿链在上升沿中断里抓 HAL_GetTick 推进 SpscQueue<uint32_t,8>，主循环抽干队列逐条打 edge 日志。Renode 注入三起三落的抖动，3 行 edge 对 1 次 press 的对拍实测，EXTI_PR 写 1 清零、EXTI0 向量归属、SPSC 无锁队列的依据，收在两条链各自的代价与取舍"
chapter: 2
order: 1
tags:
  - stm32f1
  - intermediate
  - 状态机
  - variant
  - 无锁
  - 嵌入式
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 9
prerequisites:
  - "把配置写进类型：Gpio 模板与编译期的验证"
related:
  - "把日志用起来：两种写法与两个观看面"
---

# 一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍

## 引言：同一次触摸，两种报法

v1 的边界咱们在[《把日志用起来：两种写法与两个观看面》](../06-patterns/07-logger-dual-syntax-usage.md)里交代过：日志只认线程上下文的调用，中断服务函数里是禁用的。接下来轮到输入侧的第一颗料：按键。素材笔者取了 libestdx 的 `examples/07_button_events`，一份 117 行的 `main.cpp` 把两条链全装在了里面。

输入是一颗 TTP223 类的电容触摸模块，出厂的默认是瞬时模式，空闲的时候是低电平，手一摸就成了高电平，模块的默认配置 diyusthad 的页面写得明白<RefLink :id="3" preview="diyusthad, TTP223 touch switch" />。咱们让固件在 Renode 里连吃三次触摸，下面的日志流是实抓的，咱们一个字节都没动：

```text
[0ms][Info ][boot] button events ready
[0ms][Info ][edge] 0
[60ms][Info ][press] 60
[360ms][Info ][release] 360
[1727ms][Info ][edge] 1727
[1780ms][Info ][press] 1780
[2480ms][Info ][long] 2480
[2780ms][Info ][release] 2780
[4876ms][Info ][edge] 4876
[4896ms][Info ][edge] 4896
[4916ms][Info ][edge] 4916
[4920ms][Info ][press] 4920
[5620ms][Info ][long] 5620
[5860ms][Info ][release] 5860
```

咱们把目光压到最后六行。抖动那一场里 `edge` 连来了三行（4876、4896、4916），`press` 却只来了一次（4920）。同样的一串抖动，一条链报了三次，另一条只认了一次，为什么？

原因要等咱们把两条链都看过才有分晓。库作者在 `renode.resc` 的头注释里给两条链起了名字：消抖轮询的叫幕一，EXTI 记录的叫幕二。本篇咱们就顺着这两幕往下走：装配、两条链的机制、对拍实验，取舍咱们放在最后。

## 装配：一根 PA0，两条链同时挂上

硬件咱们一样一样过。输入的脚是 PA0，别名 `KeyPin` 用的是 `Gpio<GpioPort::A, GPIO_PIN_0, GpioDirection::Input, GpioPull::Down>`，内部下拉把空闲的电平钳在低位，模块的手感是摸上去给高，所以按键的极性别名用了 `ActiveHigh`。

```cpp
using LedPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::C, GPIO_PIN_13,
                                    estdx::gpio::GpioDirection::Output>;
using Led = estdx::device::LED<LedPin, estdx::gpio::GpioPolarity::ActiveLow>;

using KeyPin = estdx::stm32f1::Gpio<estdx::stm32f1::GpioPort::A, GPIO_PIN_0,
                                    estdx::gpio::GpioDirection::Input, estdx::gpio::GpioPull::Down>;
using Key = estdx::device::Button<KeyPin, estdx::gpio::GpioPolarity::ActiveHigh>;

using Serial = estdx::stm32f1::Uart<estdx::stm32f1::UartInstance::Usart1>;
using Log = estdx::logger::Log<estdx::logger::UartLogSink<Serial>, estdx::logger::LogLevel::Info,
                               128, estdx::stm32f1::HalTickClock>;
static_assert(estdx::logger::LogSink<estdx::logger::UartLogSink<Serial>>);
static_assert(estdx::logger::LogClock<estdx::stm32f1::HalTickClock>);

using KeyDebouncer = estdx::device::Debouncer<20, 3>;
using KeyFsm = estdx::device::ButtonFsm<700>;
using EdgeQueue = estdx::base::SpscQueue<std::uint32_t, 8>;

static_assert(estdx::base::StateMachine<KeyFsm>);
```

LED 还是 PC13 那颗低电平点亮的，跟起步篇用的是同一颗。`Gpio` 模板的出身在[《把配置写进类型：Gpio 模板与编译期的验证》](../01-led/08-gpio-template.md)，咱们这里只当熟面孔用。`Log` 的别名与日志系列一字不差，行格式的解剖咱们不重复，您要细看就翻[《把日志用起来：两种写法与两个观看面》](../06-patterns/07-logger-dual-syntax-usage.md)。真正的新面孔是压在后面的几行：消抖器、状态机、队列，还有一行检查状态的静态合同。

EXTI 是外部中断的控制器，专门盯引脚上的边沿，边沿一到的时候，中断就发出去了。它的装配同样压在别名里。`OnTouch` 是中断里要干的那点活，KeyEvent 干的是把引脚、边沿、处理器绑成一个类型，`init(2)` 的 2 是给 NVIC（Cortex-M 内核里的中断控制器，管各路中断的轻重次序）的抢占等级。向量函数咱们也贴上，它的全部就三行：

```cpp
struct OnTouch {
    static void operator()() { edge_queue.push(HAL_GetTick()); }
};
using KeyEvent = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Rising,
                                      OnTouch>;

extern "C" void EXTI0_IRQHandler() {
    KeyEvent::irq();
}
```

这个向量函数为什么不住在 `stm32f1xx_it.c` 里？模板工程的 `it.c` 只有一句注释给了答案：“EXTI0 向量在 main.cpp 定义(KeyEvent::irq 只有 C++ TU 可见)。” SysTick_Handler 还是留在了 `it.c` 里。咱们把它翻译过来：`KeyEvent::irq` 是模板里的静态函数，只有看得见 C++ 类型的翻译单元才碰得到它（TU 就是翻译单元的缩写），HAL 的 C 文件里放不住，向量就跟着搬进了 `main.cpp`。

## 幕一：轮询消抖链，采样三次一致才算按下

主循环是幕一的全部现场，咱们把它原样搬过来：

```cpp
for (;;) {
    std::uint32_t edge_ms = 0;
    while (edge_queue.pop(edge_ms)) {
        Log::info(estdx::logger::Tag("edge"), edge_ms);
    }

    const std::uint32_t now = HAL_GetTick();
    debouncer.update(Key::is_pressed(), now);
    if (const auto event = fsm.feed(debouncer.stable(), now)) {
        report(*event, now);
    }
}
```

幕一的每一圈都是固定的三步：读到的原始电平交给消抖器，消抖后的稳态喂给状态机，到手的事件就交给 `report` 去分派。分派的代码也是一整块 `std::visit`，咱们一并搬过来：

```cpp
void report(const estdx::device::ButtonEvent& event, std::uint32_t now_ms) {
    std::visit(
        [&](const auto& e) {
            using E = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<E, estdx::device::Press>) {
                Log::info(estdx::logger::Tag("press"), now_ms);
                Led::on();
            } else if constexpr (std::is_same_v<E, estdx::device::LongPress>) {
                Log::info(estdx::logger::Tag("long"), now_ms);
            } else {
                Log::info(estdx::logger::Tag("release"), now_ms);
                Led::off();
            }
        },
        event);
}
```

亮灯的差事归 press，灭灯的归 release，而 long 只在日志里露面。三种事件咱们都看清了：全是空壳的标签，分支的裁决全都压在编译期，variant 加 `if constexpr` 的家常做法就是这么一副样子。

### Debouncer：到期才采样，一致才计数

`Debouncer<20, 3>` 的两个参数是采样间隔 20ms 和一致门槛 3 次。算法咱们照源码念：

```cpp
bool update(bool raw, std::uint32_t now_ms) {
    if (now_ms - last_sample_ms_ < INTERVAL_MS) {
        return false;
    }
    last_sample_ms_ = now_ms;

    if (raw == candidate_) {
        if (count_ < STABLE_COUNT) {
            ++count_;
        }
    } else {
        candidate_ = raw;
        count_ = 1;
    }

    if (count_ >= STABLE_COUNT && candidate_ != stable_) {
        stable_ = candidate_;
        return true;
    }
    return false;
}
```

它的路数是“周期采样加连续一致确认”：不到 20ms 的间隔不采，采样值跟候选对得上的时候计数往上添，对不上的时候就换新候选、计数回到 1，攒到 3 次而且候选跟稳态不同的时候，咱们才让稳态翻过去。它守的是一道一致性的门槛：三次采样、每次相隔 20ms、读数全一样，咱们这一关才放行。

咱们在构造函数里还发现一个小机关：`last_sample_ms_` 的初值是 `0 - INTERVAL_MS`，头文件注释的原话是“初值让第一次 update 立即到期:0 - (0 - INTERVAL) = INTERVAL,不小于门槛。”在无符号减法的语义下，0 时刻的第一次 `update` 也算到期，回绕的减法它也顺手摆平了。

参数为什么选 20 和 3？输入侧的模块是电容触摸的，身上没有金属的触点，本篇的抖动全是 Renode 注进去的。Ganssle 的测量采自机械开关，拿来当参数选择的保守依据正合适：罩得住最差一档的机械开关，就罩得住注入的抖动。多数开关的抖动在 10ms 内收场<RefLink :id="2" preview="Jack Ganssle, A Guide to Debouncing, Part 2" />，50ms 的响应延迟人手感觉不出来，平均的量级在 1.6ms 上下<RefLink :id="5" preview="Jack Ganssle, A Guide to Debouncing, Part 1" />。20ms 的采样间隔踩在抖动收尾之后，60ms 的确认窗又落在人的感知预算里，给咱们撑腰的两头依据都齐了。

### ButtonFsm：布尔稳态进去，variant 事件出来

稳态的翻转交给 `ButtonFsm<700>`。咱们把三种对外的报文摆出来，库作者给它们配的注释也是原话：

```cpp
struct Press {};     // 孩子们我按下了
struct Release {};   // 孩子们我松手了
struct LongPress {}; // 孩子们我在长按
using ButtonEvent = std::variant<Press, Release, LongPress>;
```

空壳的结构体咱们拿来当标签用，variant 装的就是“发生了什么”，不带多余的数据。状态机的内部另有一套词汇，内部事件的词汇是三样：Pressed、Released、Tick。状态的词汇也是三样：Idle、Down、LongDown，Down 的身上带着 `since_ms`。`feed` 的差事是把布尔稳态翻译成内部事件：稳态变了就发 Pressed 或 Released，没变的时候发 Tick。

```cpp
struct Idle {
    Transition on(const Pressed& e) const { return Transition{Down{e.now_ms}, Press{}}; }
};

struct Down {
    std::uint32_t since_ms;
    Transition on(const Tick& t) const {
        if (t.now_ms - since_ms >= LONG_PRESS_MS) {
            return Transition{LongDown{}, LongPress{}};
        }
        return Transition{*this};
    }
    Transition on(const Released&) const { return Transition{Idle{}, Release{}}; }
};

struct LongDown {
    Transition on(const Released&) const { return Transition{Idle{}, Release{}}; }
};
```

转移的规则咱们念一遍：Idle 收到 Pressed 的时候就进 Down，报出来的就是 Press。Down 里的 Tick 每次都对着 `since_ms` 对表，压过 700ms 的时候就升 LongDown 并报 LongPress，Down 或 LongDown 收到 Released 的时候都回 Idle，报出来的就是 Release。长按的判定是被动式的：状态机自己是不计时的，全靠外部一次次的 Tick 来对表。

底座用的是 `base::VariantStateMachine`：`handle` 里咱们能看到两次 `std::visit`，一次访的是事件、另一次访的是当前状态，`if constexpr` 命中 `HandlesEvent` 的时候才调 `on` 做转移，没命中的组合返回 `nullopt`、停在原状态。开头的那个状态必须默认可构造，底座里的 `static_assert` 就等着查它。这套静态合同咱们放了两份：`main.cpp` 里的一份挤在别名堆里，查的是 `KeyFsm`，`button.hpp` 里的一份缀在文件结尾，查的是 `ButtonFsm<700>`，两处查的是同一个合同。库作者的尾注释原话：“这里塞一下，正常Button是一个经典的状态机”。

## 幕二：EXTI 边沿链，中断里只记一个时间戳

幕一的三个组件全在主循环里跑，幕二则把一半的活搬进了中断。链条咱们从触摸沿开始串：上升沿一来就触发 EXTI0 的中断，`EXTI0_IRQHandler` 调用的是 `KeyEvent::irq()`，`irq()` 里的次序是排好的：pending 清掉了，OnTouch 才得到出场的机会，`OnTouch` 把 `HAL_GetTick()` 的快照推进队列，主循环随后抽干队列、逐条打出 edge 的日志。

这里有一处两条链咬合的地方咱们得摆出来：`OnTouch` 为什么不直接 `Log::info`？因为 logger 的 v1 只认线程上下文，中断里是禁用的，边界咱们在日志用法那一篇里列过。所以中断里只干最轻的活：抓一个 `HAL_GetTick()` 的快照推进队列，格式化、走串口这些重活全留给了主循环。edge 行的 payload 是 ISR（中断服务函数）里抓的时刻，行前缀是主循环打印的时刻，眼下两个数是相等的，只因主循环的转速快，主循环忙起来的时候，两个数就分家了。这正是 ISR 推时间戳、把打日志留给主循环的意义之一。

丢弃的策略咱们也交代一下：`push` 的返回值在 `OnTouch` 里被忽略了，队列满的时候新边沿直接丢。这是蓄意的简化：容量 8 的队列，校验注入的沿最多 3 个，示例的节奏根本填不满。真讲究起来的时候，`push` 注释里的话也是现成的：`the caller owns the overflow policy (drop, count, overwrite — but never block in an ISR)`，丢弃的策略归调用者管，ISR 里永远不做阻塞的事。

### Exti 模板：从领线号到 NVIC 的装配

`Exti` 模板的参数有三个：输入引脚、边沿、处理器。处理器要满足 `irq::IrqHandler` 的要求，能提供静态的 `operator()` 就算过。这个概念的出身咱们也查过：2026-10-03 从 exti 自己的 ExtiHandler 提升成了共享概念，exti 也就顺理成章地当了第一个用户。

`init` 的流程咱们按顺序过：领线号、配引脚、开 AFIO（复用功能 IO 模块、管 EXTI 线到端口映射）的时钟、把端口写进 EXTICR、按边沿置 RTSR/FTSR、IMR 开中断、PR 清残留，最后是 NVIC 的抢占等级和使能。领线号那步带着防线号重复的机关：同一条 EXTI 线被第二个模板声明的时候，`claim_line` 给的就是 `__builtin_trap()`。

```cpp
static void init(std::uint32_t preempt) {
    claim_line();

    Pin::init();

    __HAL_RCC_AFIO_CLK_ENABLE();
    map_source();

    if constexpr (EDGE == ExtiEdge::Rising || EDGE == ExtiEdge::Both) {
        EXTI->RTSR |= Pin::mask;
    }
    if constexpr (EDGE == ExtiEdge::Falling || EDGE == ExtiEdge::Both) {
        EXTI->FTSR |= Pin::mask;
    }
    EXTI->IMR |= Pin::mask;
    EXTI->PR = Pin::mask;

    const auto irq = irq_number();
    HAL_NVIC_SetPriority(irq, preempt, 0);
    HAL_NVIC_EnableIRQ(irq);
}

static void irq() {
    if (EXTI->PR & Pin::mask) {
        EXTI->PR = Pin::mask;
        Handler::operator()();
    }
}
```

EXTICR 的映射是个填空题：寄存器 `EXTICR[line/4]` 里 `(line%4)*4` 起的那 4 个位，填的是端口的序号。PA0 的线号是 0，咱们拿它对一遍：落在 `EXTICR[0]` 的最低 4 位，填的值是端口 A 的序号。

PR 的语义是幕二最值得记的一条，咱们去 RM0008（ST 的 STM32F1 参考手册）的 §10.3.6 对过原文<RefLink :id="1" preview="STMicroelectronics, RM0008" />：往 PR 里写 1 是清零的动作，而写 0 什么都不会发生。`irq()` 里清 PR 的动作排在 handler 之前：干活的途中要是又来一个沿，PR 是会被重新置位的，中断退出之后就不会漏掉它了。清零排在干活的前面，就是这么来的。

中断号的映射也有一张现成的表：线号 0 到 4 的是各自独享向量，5 到 9 的合用 EXTI9_5，10 到 15 的合用 EXTI15_10。PA0 的线号是 0，所以向量就是 EXTI0，咱们前面看到的 `EXTI0_IRQHandler` 正是它。词汇表也补一句：`ExtiEdge` 的枚举里还有 Both 一档，双边沿的用法本篇用不上。

### SpscQueue：无锁队列的依据

队列的合同咱们照头文件的注释过一遍。合同的要害是单生产者单消费者（SPSC）：`push` 的调用方只有一个（ISR），`pop` 的调用方也只有一个（主循环），多出任何一个就作废了。存储是 N 格的环形数组，N 必须是 2 的幂，容量按 N-1 的口径算：head 追到 tail 前一格的时候就算满，head 等于 tail 的时候算空，一个空位换来空与满的无歧义。两个索引的写权各归各：head 的写权归生产者，tail 的写权归消费者，两个都是 volatile 的。

```cpp
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
```

无锁无原子的底气也在注释里：单核的 Cortex-M3 没有数据 cache，每个索引的写者只有一个，按自然边界排好的 32 位读写是单次总线访问，半个值是读不到的。就算读者看到的是过期索引，也只是把空和满误判了一小圈，半个元素是绝对不会读到的，因为写入的顺序是元素进去了、索引才跟着动。volatile 拦的只是编译器把索引缓存在寄存器里，内存屏障的活儿它干不了，在咱们这里也不需要。

## 对拍：抖动注进去，两条链各报各的

注入的活儿都写在 `renode.resc` 里。平台的描述文件是 `sim/stm32f1/bluepill.repl`，日志走 USART1 的 PTY（伪终端），默认的路径是 `/tmp/libestdx-button`，`$bin?=`、`$uart_pty?=` 带问号的写法跟日志系列同款，意思是可覆盖的默认值。模型不知道引脚的上下拉，所以 reset 宏提前把 PA0 拉到低电平，模拟模块空闲时的低电平。三个注入宏咱们从它的头注释里搬过来：

```text
runMacro $press    # 干净摸一下(摸 0.3s 松)
runMacro $long     # 摸住 1s(触发长按)再松开
runMacro $bounce   # 带抖动摸住(3 次上升沿)→ 0.9s → 松开
```

时序的纪律咱们引 resc 头注释的原话：

```text
monitor 的 sleep 等 host 时间,虚拟时间不成比例 —— 注入宏
一律 pause → OnGPIO → emulation RunFor "<虚拟秒>" 精确推进。
```

三个宏的日志咱们对着时间轴读：

| 注入 | 动作描述 | 日志里的行（时刻 ms） | 值得看的数 |
|---|---|---|---|
| `$press` | 干净摸一下，0.3s 松手 | edge 0 / press 60 / release 360 | press 比触摸晚 60ms，正是 20×3 的确认窗 |
| `$long` | 摸住 1s 松手 | edge 1727 / press 1780 / long 2480 / release 2780 | 2480 = 1780 + 700，长按的判定分毫不差 |
| `$bounce` | 10ms 三起三落后摸住 0.9s | edge 4876 / 4896 / 4916 / press 4920 / long 5620 / release 5860 | 3 行 edge 对 1 次 press，5620 = 4920 + 700 |

抖动那一场咱们单拿出来讲。`$bounce` 的手法是 10ms 间隔三起三落，最后的那下是摸住 0.9s 再松，resc 里的注释把预期写得明明白白：“幕二应报 3 行 edge,幕一只认 1 次 press(20ms×3 一致性窗滤毛刺)”。实测的日志也正是这么走的：`edge` 行连来了三行，报的数是 4876、4896、4916，`press` 只来了一次（4920），后面的 long 与 release 也按时来了，落点咱们记下：5620 和 5860。

为什么幕一只认一次？咱们看凹陷的宽度：两次低电平的凹陷各只有 10ms，不足两个采样点的间距，就算有采样点恰好落进了凹陷，被污染的读数顶多一个，连续三次的一致也凑不齐了，稳态是翻不过去的。resc 注释里的那句“一致性窗滤毛刺”，说的就是它。

校验咱们交给 `check_renode.py`。`$bounce` 那一场断言的清单是 edge 恰好 3 行、press/long/release 各 1 次，顺序的断言是 press ≤ long < release，长按的间隔落在 600 到 1000ms 的窗口，long 之后的 500ms 里要等到 release。`press` 那一场断言的清单换成 1 行 edge、1 次 press、1 次 release，而 long 是不许出现的。命令和当天的实跑输出咱们一并贴上：

```sh
cmake --build build --target check_button_renode
```

```text
button events check: PASS (3 edges vs 1 debounced press; long/release ordered)
```

当天的校验用的都是同一份固定在提交 8026841 的子模块，连日志示例的回归在内，全都过了。固件的尺寸是 text 16016、data 168、bss 136，咱们原样抄了下来。仿真的目标叫 `sim_button_events`，烧录的目标叫 `flash_button_events`，后者把同一份固件烧进实体的板子。

## 取舍：EXTI 拿到了时间戳，分不清抖动与真按

两条链的性子，咱们放在一起比：

| | 幕一（轮询消抖链） | 幕二（EXTI 边沿链） |
|---|---|---|
| 语义 | 确认：报出来的都是稳的 | 记录：有沿就记，真假不管 |
| 延迟 | 固定背 60ms 的确认窗 | 时间贴着沿走，主循环睡了也不丢 |
| 代价 | 主循环得一直转，每圈都要采样喂状态机 | 配置面大：AFIO、EXTICR、NVIC、向量，ISR 只能干短活 |
| 消抖 | 现成带上（采样加一致确认） | 没有抖动与真按的区分能力 |

在笔者看来，轮询链的强项是语义干净：press 一出手就是稳态，长按这类时间维度的玩法在它这边好做。代价也清楚：60ms 的确认延迟固定存在，主循环也是不能停下来的。EXTI 链的强项是时刻准、不依赖主循环的勤快，代价是配置面宽、ISR 里只能干最短的活，队列满了还会丢边沿。

幕二手里攥着的只有边沿事件，时间维是没有的，抖动与真按它是分不清的，3 行 edge 对 1 次 press 的对比就在眼前。想分清的话，补的就得是时间维：这一维咱们交给 TIM（定时器），时间主题的那一篇开头就来接。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 上电就进 `__builtin_trap()` | 同一条 EXTI 线被第二个 `Exti` 声明，`claim_line` 防的就是它 | 一条线只装配一个 `Exti` |
| 链接器抱怨 `EXTI0_IRQHandler` 重复定义 | 向量函数已经写在 `main.cpp`，`it.c` 里又放了一份 | `it.c` 里只留注释，向量归 `main.cpp` |
| edge 行数比注入的沿少 | 队列满时 `push` 被忽略，新边沿丢弃 | 容量按 `available()` 的参考值规划，注入节奏别太密 |
| edge 与 press 的时间戳对不上 | 行前缀是主循环打印时刻，payload 才是 ISR 抓的 | 对时刻就看 payload 那个数 |
| press 总比触摸晚 60ms | 20ms×3 的确认窗是设计内的延迟 | 要更快就调 `Debouncer` 的两个参数，别低于抖动收尾的时间 |
| 在中断里调 `Log::info` 没有输出 | logger v1 仅线程上下文，ISR 禁用 | 学 `OnTouch` 推队列，主循环再打 |
| Renode 里 `sleep` 注入后日志乱序 | monitor 的 sleep 等 host 时间，与虚拟时间不成比例 | 注入一律 `pause` 后 `RunFor` 虚拟秒 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="10.3.6 EXTI pending bit; 10 Interrupts and events"
  />
  <ReferenceItem
    :id="2"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 2"
    url="https://www.ganssle.com/debouncing-pt2.htm"
    chapter="Switch bounce measurements: most contacts settle within 10ms"
  />
  <ReferenceItem
    :id="3"
    author="diyusthad"
    title="How to use a TTP223 based touch switch"
    :year="2019"
    url="https://diyusthad.com/2019/06/how-to-use-ttp223-based-touch-switch.html"
    chapter="TTP223 factory default momentary mode, idle low / touch high"
  />
  <ReferenceItem
    :id="4"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（examples/07_button_events 与 device/base/boards 模块）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="Debouncer, ButtonFsm, Exti, SpscQueue"
  />
  <ReferenceItem
    :id="5"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 1"
    url="https://www.ganssle.com/debouncing.htm"
    chapter="16 switches averaged 1557 µsec of bouncing (max 6200 µsec)"
  />
</ReferenceCard>
