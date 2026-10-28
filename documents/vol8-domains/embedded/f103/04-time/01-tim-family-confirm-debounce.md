---
title: "TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗"
description: "tim.hpp 的 302 行把 TIM 包成三件套：TimBase 周期拍、TimOneShot 单发窗、TimPwm 占空比输出，配置用指定初始化器直接做非类型模板参数。consteval 求解器从 timclk 与周期解出 PSC 与 ARR：整除因子从 1 往上找、余数兜底、0.1% 精度闸，64MHz 下 1Hz、20ms、10ms 三个实例的 ARR 全落在 63999。timclk 的 APB1 ×2 规则、bluepill.repl 定时器频率 10MHz 改 64MHz 的对齐补丁、EGR.UG 装载即置 UIF 的假中断与代码顺序里的防法，收在两个示例里：09_tim_heartbeat 的 1Hz 心跳配 WFI 主循环，10_button_confirm 用 EXTI 边沿启动 20ms 单发确认窗，抖动让窗顺延、毛刺落空，确认事件与 10ms 慢拍同喂 ButtonFsm，换源不换机"
chapter: 4
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 外设管理
  - 寄存器
  - 状态机
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 10
prerequisites:
  - "一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍"
related:
  - "把日志用起来：两种写法与两个观看面"
---

# TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗

## 引言：上一篇留下的时间维，本篇交给 TIM

上一篇的收尾处，咱们把幕二的短板留在了台面上：[《一串抖动，两条链：轮询消抖与 EXTI 边沿的对拍》](../02-button/01-polling-debounce-vs-async-exti.md)里的 EXTI 链拿到了精确到毫秒的时间戳，却分不清抖动与真按的界线，想分清的话，补的就得是时间维。供货的硬件就是本篇的主角 TIM。F103 上 TIM1 到 TIM4 各有各的计数器、预分频器与中断向量，时间从这一篇起可以脱离主循环的转速独立走。

素材取自固定在提交 8026841 的子模块，两个示例笔者都跑过：`examples/09_tim_heartbeat` 与 `examples/10_button_confirm`。咱们把两段实抓的日志并排摆上来，一段管的是周期，一段管的是确认。

```text
[0ms][Info ][boot] heartbeat ready
[1000ms][Info ][beat] 1
[2000ms][Info ][beat] 2
[3000ms][Info ][beat] 3
[4000ms][Info ][beat] 4
[5000ms][Info ][beat] 5
```

```text
[0ms][Info ][boot] button confirm ready
[21ms][Info ][touch] 20
[21ms][Info ][press] 20
[321ms][Info ][leave] 321
[321ms][Info ][release] 321
[3169ms][Info ][touch] 3169
[3169ms][Info ][press] 3169
[3870ms][Info ][long] 3870
[4069ms][Info ][leave] 4069
[4069ms][Info ][release] 4069
```

第一段的节奏咱们数得出来：五拍心跳的间隔全是整 1000ms，主循环睡在 WFI（等待中断的睡眠指令）里一拍未丢，时间全从定时器的中断里来。第二段的看点在 touch 行：注入的抖动是 10ms 间隔的四连翻，凑成了两个完整的抖动周期，日志里认下的却只有一次 touch 与一次 press。四连翻把 20ms 的确认窗一次次往后推，推到电平收了场，窗才算到了点。

本篇咱们顺着 `tim.hpp` 的 302 行往下走：家族清单与写进类型的配置，consteval 求解器怎么把咱们要的周期解成预分频器 PSC 与自动重装载 ARR 的分频对，timclk 从时钟树的哪条岔路来。中间有一处假 UIF（更新中断标志、状态寄存器里的一位）的机关值得单讲，装载与清标志的顺序就是它的防法。收尾是两个示例的完整链路，09 管的是心跳，10 管的是确认，10 的确认窗补上的正是上一篇欠下的那一维。

## 家族清单：三件套与写进类型的配置

家族的入口是一枚按地址取值的枚举，四个成员咱们挨个数下来，每个名字对着一个外设的基地址：

```cpp
enum class TimInstance : std::uintptr_t {
    Tim1 = TIM1_BASE,
    Tim2 = TIM2_BASE,
    Tim3 = TIM3_BASE,
    Tim4 = TIM4_BASE,
};
```

中断向量的差别咱们顺带交代：TIM1 的更新中断走 `TIM1_UP_IRQn`，其余三个走各自的 `TIMx_IRQn`。这层差别被 `TimHardware` 的 `irq_number()` 包住了，装配的人不用背。

配置的结构体咱们也搬出来，只有薄薄的两份：

```cpp
struct TimConfig {
    TimInstance instance;
    std::uint32_t period_us;
    std::uint32_t timclk_hz = 64'000'000;
};

struct TimPwmConfig {
    TimInstance instance;
    TimChannel channel;
    std::uint32_t freq_hz;
    std::uint32_t timclk_hz = 64'000'000;
};
```

C++20 把结构体放进了非类型模板参数的名单，库作者把这个特性用到了底：指定初始化器就这么写进了模板实参。咱们看 09 的装配长什么样：

```cpp
using Heartbeat =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 1'000'000},
                            OnBeat>;
```

`period_us` 的单位是微秒，您看一眼就能对上周期。`timclk_hz` 带的缺省值是 64MHz，缺省为什么偏偏是它，咱们到讲时钟的小节再交代。三件套的分工咱们列出来：

| 模板 | 用途 | 到点之后 |
|---|---|---|
| `TimBase` | 周期拍，09 的 1Hz 心跳 | UIF 周期性来，计数器一直跑 |
| `TimOneShot` | 单发窗，10 的 20ms 确认 | OPM 单脉冲模式数完自停，UIF 只来一次 |
| `TimPwm` | 占空比输出，呼吸灯留给后续篇 | CCR 撑起波形，与更新中断无关 |

`TimBase` 的 `init()` 次序咱们照源码念：开时钟、写 PSC 与 ARR、`EGR = TIM_EGR_UG` 装载、`DIER = UIE` 开更新中断、`SR = ~UIF` 清标志、`CR1 = CEN` 放计数器跑，最后配 NVIC 的抢占等级。

`TimOneShot` 的 `init()` 反着来，头一句 `CR1 = 0` 就把计数器停住了，真正的启动挪到了 `arm()` 里，四步咱们排开：置 OPM、装载、清 UIF、置 CEN，硬件数完一个周期就自己停了。`disarm()` 把 OPM 与 CEN 一起清了，窗还没数完的时候也收得回来。

`TimPwm` 本篇咱们只当家族名单的成员点出来：它跑的是 PWM 模式 1（`OCxM_2 | OCxM_1`），四个通道任您挑，引脚映射来自 consteval 的 `pin_map()`，TIM2 的 CH1 落在 PA0，TIM3 的落在 PA6，TIM4 的落在 PB6，TIM1 的落在 PA8，都配成了复用推挽输出。`set_duty` 的用法是按百分比写比较寄存器 CCR，超出 100 的部分会钳到 100。呼吸灯的实操留给后续篇。

处理器那头的合同还是老样子，咱们在上一篇见过。`IrqHandler` 与 Exti 共用的概念写在 `irq_base.hpp`，咱们把它整个搬过来：

```cpp
template <typename H>
concept IrqHandler = requires {
    H::operator()();
};
```

上一篇咱们提过它的身世：2026-10-03 从 exti 自己的 ExtiHandler 提升成共享概念，exti 当了第一个用户，tim.hpp 是它的第二个。能提供静态 `operator()` 的类型都算过，两个模板的 Handler 参数位因此长得一模一样。

## consteval 求解器：从周期解出 PSC 与 ARR

PSC 与 ARR 的寄存器各占 16 位，预分频值与计数值都被压在了 65536 以内，两个 16 位相乘才是定时器够得着的最大 tick 数。求解器把咱们要的周期翻译成 PSC 与 ARR 的值，活全部在 consteval 里干完了：

```cpp
consteval std::uint64_t ticks_for_period(std::uint64_t clk_hz, std::uint64_t period_us) {
    return (clk_hz * period_us + 500'000) / 1'000'000;
}

consteval TimDivisors resolve_ticks(std::uint64_t ticks) {
    if (ticks < 1 || ticks > 65'536ull * 65'536ull) {
        return {0, 0, false};
    }
    for (std::uint64_t div = 1; div <= 65'536; ++div) {
        if (ticks % div == 0 && ticks / div <= 65'536) {
            return {static_cast<std::uint32_t>(div - 1),
                    static_cast<std::uint32_t>(ticks / div - 1), true};
        }
    }
    const std::uint64_t div = (ticks + 65'535) / 65'536;
    return {static_cast<std::uint32_t>(div - 1), static_cast<std::uint32_t>(ticks / div - 1), true};
}
```

`ticks_for_period` 的活是把周期折成 tick 数，计算的时候加了半个百万再除，落点取的是最近的微秒。`resolve_ticks` 的找法咱们看明白：从 1 往上找的是整除因子，能整除 ticks、商又塞得进 16 位的第一个 div 就是答案，PSC 记的是 div−1，ARR 记的是商−1。整除对找不到的时候当然有兜底，兜底的 div 取的是 `ceil(ticks/65536)`，代价是带上余数的损失。

接着是两道把关的 `static_assert`，咱们原样搬：

```cpp
static_assert(kDiv.ok, "period below resolution or beyond 16-bit PSC*ARR at this clock");
static_assert(detail::relative_error_ppm(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us),
                                          kDiv) <= 1'000,
              "period misses 0.1% precision at this clock");
```

第一道拦的是范围：周期或频率超出 16 位乘 16 位的量程，编译就直接失败了，错误信息把失败的原因也写明白了。第二道是 0.1% 的精度闸，它算的是配出来的周期与目标之间的相对误差（单位 ppm），超了 1000 就不放行。这道闸真会超线的时刻，咱们在[《量程的边界：16 位乘 16 位能配多长》](03-period-range-boundary.md)里算过，这里就不展开了。填错的参数在编译期就被拦下了，怪拍子咱们根本见不着。

本篇的三个实例，笔者用 g++ -std=c++23 把同一份算法编了一遍，解出来的值列在下面：

| 实例 | 周期（µs） | tick 数 | PSC | ARR |
|---|---|---|---|---|
| 09 的 1Hz 心跳 | 1'000'000 | 64'000'000 | 999 | 63999 |
| 10 的 20ms 确认窗 | 20'000 | 1'280'000 | 19 | 63999 |
| 10 的 10ms 慢拍 | 10'000 | 640'000 | 9 | 63999 |

三个周期的取值各不相同，ARR 的值却全是 63999，咱们把这个巧合拆解明白。64MHz 之下每 1ms 的 tick 恰好是 64000 个，三个周期又都落在整毫秒的刻度上，求解器从 1 往上找的头一个可用因子，恰好就是周期里含着的整毫秒数：1Hz 含了 1000 个毫秒，所以 PSC 是 999。20ms 里的毫秒数是 20，所以 PSC 是 19。10ms 里的毫秒数是 10，PSC 也落在了 9。落进 ARR 的商全是 64000，ARR 自然也就全是 63999 了。这对值的读法是算术分解：PSC 加 1 是周期里的整毫秒数，ARR 加 1 是 1ms 里的 64000 个输入 tick，相乘就是周期的总 tick 数，您拿着它做心算验证，会觉得挺顺手的。

## timclk 从哪来：APB1 的 ×2 规则

`TimConfig` 的缺省值 64MHz 不是随手写的，它来自两个示例共用的 `SystemClock_Config`。咱们把配置摘出来，算术也一并列在了旁边：

```cpp
osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
osc.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
osc.PLL.PLLMUL = RCC_PLL_MUL16;
clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
clk.APB1CLKDivider = RCC_HCLK_DIV2;
```

```text
HSI 8MHz / 2 × 16 → SYSCLK 64MHz → AHB /1 = 64MHz → APB1 /2 = PCLK1 32MHz
```

TIM2 到 TIM4 的时钟来自 APB1，一条挂着这批定时器的外设总线，输入的却不按 32MHz 算，咱们去 RM0008（ST 的 STM32F1 参考手册）里找原因。时钟树的记载很明白：APB1 的预分频为 1 时，定时器拿到的就是 PCLK1。预分频不是 1 的时候，定时器时钟要在 PCLK1 的基础上乘 2<RefLink :id="1" preview="STMicroelectronics, RM0008" />。咱们拿 32 乘 2 得 64，与缺省的 `timclk_hz` 对上了。经典 72MHz 配置走的是同一条路：APB1 二分频之后的 PCLK1 是 36MHz，定时器拿的是 72MHz。DigiKey 的入门教程把 ×2 的规则讲得很直白，它举例举的是 APB2 侧，F1 的 APB1 侧走的是同一条规则<RefLink :id="2" preview="DigiKey, Getting Started with STM32 Timers" />。

仿真那头咱们还得对一次表。8026841 之前的提交里，`sim/stm32f1/bluepill.repl` 的四个定时器模型 frequency 是 10MHz，固件解分频用的却是 64MHz，仿真里的周期就静默地错了：咱们看不到任何报错，日志的输出照旧，错的只有拍子，能兜住它的只有间隔断言。补丁把模型的频率改成了 64MHz，跟固件的 timclk 对上了：

```text
timer2: Timers.STM32_Timer @ sysbus 0x40000000
    -> nvic@28
    frequency: 64000000
```

## 假 UIF：EGR.UG 的副作用，装载之后紧跟清零

这句 `EGR = TIM_EGR_UG` 咱们单拿出来讲。它干的是装载的正事：触发的是一次软件发起的更新事件，把预分频与重装载的值从预装载寄存器装进影子寄存器，改完就立即生效了。副作用是更新事件顺手把 UIF 也置了起来，RM0008 对 EGR.UG 与更新事件的记载把这半句写得很清楚<RefLink :id="1" preview="STMicroelectronics, RM0008" />。

不清掉它的代价很直接：中断放行之后的第一脚就是一次假到期，周期的计数还没开始，UIF 已经在状态寄存器里等着了。`tim.hpp` 的防法咱们贴在下面，证据就是代码的顺序本身：

```cpp
t->PSC = kDiv.psc;
t->ARR = kDiv.arr;
t->EGR = TIM_EGR_UG;
t->DIER = TIM_DIER_UIE;
t->SR = ~TIM_SR_UIF;
t->CR1 = TIM_CR1_CEN;
```

`EGR = UG` 之后紧跟的那句 `SR = ~UIF`，清的就是装载时顺手置起的 UIF。`TimOneShot::arm()` 重臂的时候同样办理，咱们把四步排开：`OPM` 置位、`EGR` 装载、`SR` 清 UIF、`CR1` 置 CEN，上一轮到点留下的 UIF 早被 `irq()` 清掉了，这里清的是重装载刚置起来的新 UIF。

中断里的姿势与 `exti.hpp` 同款，标志也清在干活的前面，咱们看代码：

```cpp
static void irq() {
    auto* const t = HW::regs();
    if (t->SR & TIM_SR_UIF) {
        t->SR = ~TIM_SR_UIF;
        Handler::operator()();
    }
}
```

上一篇咱们在 EXTI 的 PR 上见过同样的次序：干活的途中又来了事件，所以标志会被重新置位，中断退出之后也就不会漏了。UIF 走的是一样的道理。

## 案例 09：1Hz 心跳，主循环睡在 WFI 里

09 的首行注释自带对照，咱们原文引上来：

```cpp
// 09_tim_heartbeat: TIM2 1Hz 中断心跳, 主循环 WFI 睡眠; 对照 01_blinky 的 HAL_Delay 轮询。
```

装配咱们看三样：中断里要干的活、心跳模板、向量函数。`OnBeat` 与 `Heartbeat` 的绑法如下，`TIM2_IRQHandler` 在 `main.cpp` 里占的只有一行：

```cpp
volatile std::uint32_t beats = 0;

struct OnBeat {
    static void operator()() {
        beats = beats + 1;
        Led::toggle();
    }
};
using Heartbeat =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim2, .period_us = 1'000'000},
                            OnBeat>;

extern "C" void TIM2_IRQHandler() {
    Heartbeat::irq();
}
```

`OnBeat` 在中断里干的活只有两样。头一样是把 volatile 的 `beats` 加一，另一样翻转的是 LED。打日志的活留给主循环去干，`Heartbeat::init(3)` 的 3 是 NVIC 的抢占等级。ISR 里动的只有标志，打日志的活回线程上下文，老边界咱们在上一篇已经沿用，条款的原文在[《把日志用起来：两种写法与两个观看面》](../06-patterns/07-logger-dual-syntax-usage.md)里。

主循环咱们原样搬过来：

```cpp
std::uint32_t reported = 0;
for (;;) {
    __WFI();
    if (beats != reported) {
        reported = beats;
        Log::info(estdx::logger::Tag("beat"), reported);
    }
}
```

`__WFI()` 是 Cortex-M 的睡眠指令，主循环挂起了，中断把它唤醒了，ISR 跑完了再回到循环里接着转。醒来之后的比较也有一层小心思：`beats` 与 `reported` 有了出入才打。跟 01_blinky 里 HAL_Delay 忙等占满 CPU 的写法对照，CPU 的时间让出来了，日志的输出一条不漏。日志行前缀的毫秒数来自 `HalTickClock`，它包着的就是 `HAL_GetTick`，咱们在日志用法篇里交代过，这里咱们不重开战场。

Renode 的自动化面这次简单到了极致：日志的 PTY 挂在 `/tmp/libestdx-heartbeat`，reset 宏之外没有别的注入宏，时间自由地跑。校验脚本做的推进是 pause 之后 `RunFor "5.2"`，推完了五秒多，咱们再回 PTY 里数行，引言里那段五拍心跳就是实抓的。`check_renode.py` 的断言咱们引过来：beat 号恰为 1 到 5 的连续，相邻间隔落在 900 到 1100ms 的窗里。当天实跑的输出咱们原样贴：

```text
tim heartbeat check: PASS (1Hz beats under WFI main loop, spacing 1000ms±10%)
```

固件的尺寸笔者用 `arm-none-eabi-size` 量了，量出来的结果是 text 14160、data 112、bss 84。

## 案例 10：20ms 确认窗，抖动顺延、毛刺落空

10 的输入还是上一篇那颗 PA0 触摸模块，空闲的时候是低电平，摸上去就成了高电平。三个中断源各管各的活，NVIC 的抢占等级咱们排开：`Touch=2`、`Window=3`、`SlowTick=4`。

```cpp
struct OnEdge {
    static void operator()() { Window::arm(); }
};
using Touch = estdx::stm32f1::Exti<KeyPin, estdx::stm32f1::ExtiEdge::Both, OnEdge>;

struct OnConfirm {
    static void operator()() {
        const bool pressed = KeyPin::level();
        if (pressed != confirmed_level) {
            confirmed_level = pressed;
            (void)source_queue.push(Confirmed{pressed, HAL_GetTick()});
        }
    }
};
using Window = estdx::stm32f1::TimOneShot<
    {.instance = estdx::stm32f1::TimInstance::Tim3, .period_us = 20'000}, OnConfirm>;

struct OnTick {
    static void operator()() { (void)source_queue.push(Tick{HAL_GetTick()}); }
};
using SlowTick =
    estdx::stm32f1::TimBase<{.instance = estdx::stm32f1::TimInstance::Tim4, .period_us = 10'000},
                            OnTick>;
```

链路咱们从触摸沿开始串。EXTI 配的是双边沿，`OnEdge` 干的只有一件事：调 `Window::arm()` 把 20ms 的单发窗重新启动。抖动的每个沿都让窗顺延，一直推到电平收了场。窗到了点，`OnConfirm` 读一眼 PA0 的电平：与上次确认的不同，`confirmed_level` 也就跟着更新了，推进队列的是一条 `Confirmed`，时间戳用的是 `HAL_GetTick()`。电平已经弹回去的就算毛刺，队列里也就什么都不会有。TIM4 的 10ms 慢拍是第三个源，`OnTick` 推的只有 `Tick` 一种，给长按的对表供拍子。

首行注释把行为的口径写全了，咱们原文引：

```cpp
// 10_button_confirm: 确认式消抖(完整版) —— EXTI 沿 arm 20ms 单发确认窗, 到点采样定稳定态;
// 确认事件 + TIM 慢拍同喂 ButtonFsm(换源不换机), 主循环全程 WFI。
// 对拍 07: 消抖不再靠轮询耐心, 靠时间确认; 抖动 = 窗顺延, 毛刺 = 无事件。
```

队列的装配与上一篇有一个要紧的差别，咱们把它摆出来：

```cpp
struct Confirmed {
    bool pressed;
    std::uint32_t now_ms;
};
struct Tick {
    std::uint32_t now_ms;
};
using SourceEvent = std::variant<Confirmed, Tick>;
using SourceQueue = estdx::base::SpscQueue<SourceEvent, 16>;
```

`SpscQueue<SourceEvent, 16>` 的 push 有两处调用，一处是 TIM3 的确认中断，一处是 TIM4 的慢拍中断，而上一篇只有 EXTI 一路生产者。合同从调用方的角度看仍然成立：push 的调用全在中断上下文里，pop 的调用方只有主循环，两头是碰不上的。SPSC 的内部解剖上一篇拆过了，这里咱们只认边界。队列的容量是 16，push 的返回值照旧丢弃，满了就丢，溢出的策略归调用者管。

咱们把主循环也搬上来，它醒来的头一个动作就是抽干队列，两种事件的喂法分了两样：

```cpp
for (;;) {
    __WFI();
    SourceEvent event;
    while (source_queue.pop(event)) {
        std::visit(
            [&](const auto& ev) {
                using E = std::decay_t<decltype(ev)>;
                if constexpr (std::is_same_v<E, Confirmed>) {
                    stable_level = ev.pressed;
                    Log::info(estdx::logger::Tag(ev.pressed ? "touch" : "leave"), ev.now_ms);
                    if (const auto out = fsm.feed(ev.pressed, ev.now_ms)) {
                        report(*out, ev.now_ms);
                    }
                } else {
                    if (const auto out = fsm.feed(stable_level, ev.now_ms)) {
                        report(*out, ev.now_ms);
                    }
                }
            },
            event);
    }
}
```

到了手的 `Confirmed` 由主循环消化：`stable_level` 的更新紧跟着，touch 或 leave 的日志也跟上，pressed 也就喂给了状态机。`Tick` 到了手就拿 `stable_level` 喂，长按的对表靠它踩着拍子走。状态机还是上一篇拆过的那个 `ButtonFsm<700>`，`report` 的一行都没动，亮灯的差事归 press，灭灯的事归 release，long 打的只是一条日志。咱们看的只是喂数的源：上一篇喂的是轮询消抖的稳态，本篇喂的是确认事件加慢拍，换源不换机的说法来自库作者的注释。

注入的宏还是三件：`$press` 的动作是干净摸 0.3s，`$bounce` 的配方是 10ms 间隔的四连翻再摸住 0.9s，`$glitch` 的配方是 5ms 的孤立毛刺，日志的 PTY 挂在 `/tmp/libestdx-confirm`。resc 里的两条注释值得咱们原样引过来：

```text
# 四连翻(2 完整抖动周期)后摸住 0.9s 再松: 窗每次沿顺延, 只认最终稳定态
# 5ms 孤立毛刺: 窗到点时电平已回, 无事件
```

咱们依次注入 `$press`、`$bounce`、`$glitch`，实抓的流就是引言里那段。下面咱们逐段解。

干净触摸的一段：触摸沿落在 0ms 的位置，窗到了点就确认，touch 与 press 的 payload 同在 20ms，校验的口径是两者相差不超过 5ms。行前缀的 21 与 payload 的 20 差的那 1ms，是咱们碰过的老话题了：payload 记的是 ISR 里抓的时刻，前缀记的是主循环打印的时刻，上一篇的 edge 行讲过，主循环一忙起来两个数就分了家。

`$bounce` 的一段咱们接着看：四连翻的每个沿都重臂窗口，确认认的只是最后的稳定电平，touch 与 press 的行也就都落在了 3169。长按的 long 落在 3870，3169 加 700 的和是 3869，判定要等跨过 700ms 之后的那一拍慢拍，所以多出了 1ms。摸住 0.9s 之后松的手，leave 与 release 的行同在 4069。

`$glitch` 的一段咱们最后看：5ms 的孤立脉冲过去，窗还没到点电平就回了，`OnConfirm` 到点读到的还是旧电平，与 `confirmed_level` 的值相同，进队列的事件一条都没有，日志也就整段静默了。

校验咱们交给 `check_renode.py`，三个场景配的三组断言如下：

- `bounce` 折叠成一次确认，touch、press、long、release 各一行，顺序为 press ≤ long < release，长按的间隔落在 600 到 1000ms，press 与 touch 的差不超过 5ms
- `glitch` 场景要求完全无事件
- `press` 场景要求 touch、press、release 各一行，且不许出 long

当天实跑的输出咱们原样贴：

```text
button confirm check: PASS (bounce->1 confirm, glitch->silence, fsm fed by source events)
```

20ms 的窗宽是有出处的。Ganssle 的测量咱们在上一篇引过，多数开关的抖动 10ms 内就收了场<RefLink :id="3" preview="Jack Ganssle, A Guide to Debouncing, Part 2" />。20ms 的窗把抖动的收尾整个盖住，窗到点的时候抖动大概率已经收了场，采到的电平也就信得过。上一篇拿它论证的是采样间隔，本篇咱们拿它论证的是窗宽。

固件的尺寸笔者也量了，量出来的结果是 text 13444、data 176、bss 304。参照系是 06 站[价格表篇](../06-patterns/06-zero-overhead-price-list.md)冻结过的纯日志示例 log_example，它的 text 是 18200，今天笔者再跑了一遍，出来的还是这个数。三个中断源、FSM、队列都进了固件，确认版的 text 反而比纯日志示例小了 4756 字节。模板把分发折进直代码的行情，价格表篇里早就列过一遍了。

## 取舍：与上一篇的轮询链对拍

| | 上一篇（轮询消抖链） | 本篇（确认窗链） |
|---|---|---|
| 语义 | 采样攒一致，报稳态 | 沿后等 20ms，到点确认 |
| 确认延迟 | 固定背 60ms（20ms×3） | 锚在最后一个沿，约 20ms |
| 主循环 | 不能停，每圈采样喂 FSM | 全程 WFI，中断推队列 |
| 抖动 | 单凹陷至多污染一个读数，凑不齐三次一致，状态不翻 | 每个沿让窗顺延，收场才确认 |
| 毛刺 | 同样凑不齐三次一致，状态不翻 | 窗到点电平已回，零事件 |
| 配置面 | Debouncer 两个参数 | EXTI 加两个定时器，NVIC 排抢占等级 |

在笔者看来，确认窗的强项是响应贴着沿走：干净触摸咱们等 20ms 就拿到确认，轮询链却要攒满 60ms 的窗。代价是配置面铺开了，三个中断源的抢占等级要排队，队列与 variant 也都进了固件，好在 13444 的 text 摆在那儿，尺寸咱们也不嫌它大。而轮询链胜在简单，开工要的只是一个 Debouncer 加两个参数，主循环的转速就是它的采样钟，主循环忙起来的时候，采样也就跟着抖了。两条链喂的是同一个 ButtonFsm，按键语义那一层的资产是共享的。

这一维到了手，咱们也攒下两样东西：会重复的周期拍、只来一次的单发窗。家族名单上的第三件 `TimPwm` 还没出场，引脚映射与 `set_duty` 都备好了，把时间变成占空比的活儿，留给后续篇的呼吸灯。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 使能中断后立刻进一次到期处理 | `EGR.UG` 装载顺手置起 UIF，放行前没清 | 学 `tim.hpp`，装载之后紧跟 `SR = ~UIF` |
| 仿真里周期静默地错 | `bluepill.repl` 的 `frequency` 与固件 timclk 不一致 | 模型频率跟着固件时钟改，8026841 改的就是它 |
| 编译报 `period beyond 16-bit PSC*ARR` | 周期在这个 timclk 下超出 16 位乘 16 位的量程 | 换更慢的 timclk，或把长周期拆到软件里续数 |
| 精度闸报 `misses 0.1% precision` | 周期出了量程，这道闸跟着量程断言一起报，量程之内的兜底误差够不着它 | 解法同上一行，先把周期收回量程 |
| 定时器比预期快或慢一倍 | APB1 预分频不是 1 时定时器时钟要乘 2，忘了乘 | 按 RM0008 的规则算 timclk，再填 `timclk_hz` |
| TIM1 的更新中断进不去 | TIM1 的向量是 `TIM1_UP_IRQn`，与 TIM2 到 TIM4 不同 | 向量名跟着 `TimInstance` 走 |
| 中断里调 `Log::info` 没有输出 | logger 只认线程上下文，ISR 禁用 | 学 `OnBeat` 只动标志，主循环醒来再打 |
| 长按的判定早晚差着一拍 | 长按靠 10ms 慢拍对表，判定落在跨过 700ms 的那一拍 | 想更准就把 `period_us` 调小，代价是中断更密 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="STMicroelectronics"
    title="RM0008 Reference Manual — STM32F101/102/103/105/107"
    :year="2021"
    url="https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf"
    chapter="7.2 Clocks; 15.4.5 TIMx_SR UIF; 15.4.6 TIMx_EGR"
  />
  <ReferenceItem
    :id="2"
    author="Shawn Hymel"
    title="Getting Started with STM32 Timers and Timer Interrupts"
    :year="2020"
    url="https://www.digikey.gr/en/maker/projects/getting-started-with-stm32-timers-and-timer-interrupts/d08e6493cefa486fb1e79c43c0b08cc6"
    chapter="APB ×2 定时器时钟规则的入门讲解（APB2 侧，F1 的 APB1 侧同理）"
  />
  <ReferenceItem
    :id="3"
    author="Jack Ganssle"
    title="A Guide to Debouncing, Part 2"
    url="https://www.ganssle.com/debouncing-pt2.htm"
    chapter="Switch bounce measurements: most contacts settle within 10ms"
  />
  <ReferenceItem
    :id="4"
    author="Awesome-Embedded-Learning-Studio"
    title="libestdx（examples/09_tim_heartbeat、examples/10_button_confirm 与 boards/stm32f1/tim.hpp）"
    :year="2026"
    url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx"
    chapter="TimBase, TimOneShot, TimPwm, consteval PSC/ARR solver"
  />
</ReferenceCard>
