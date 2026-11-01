---
title: "配置进类型：指定初始化器当模板实参"
description: "TimConfig 的字段用指定初始化器直接写进 TimBase 的模板参数位，配置经类型折叠变成指令流里的立即数，笔误在编译期报错；对照 ST 官方 HAL_TIM_Base_Init 的句柄判空、五连 assert_param 与运行时读结构体，两条路线在错误暴露的时机与配置编进固件的形态上分了岔，报错里印出的配置值长、每份配置一个类型、运行时改不了周期这些代价也挨个交代"
chapter: 4
order: 2
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 模板
  - constexpr
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [20, 23]
reading_time_minutes: 10
prerequisites:
  - "TIM 家族与确认式消抖：从 1Hz 心跳到 20ms 确认窗"
related:
  - "零开销的价格表：18200 字节日志固件的解剖"
---

# 配置进类型：指定初始化器当模板实参

## 引言：家族清单里放过的那两行

上一篇的家族清单里，咱们亮过 TimConfig 结构体与 `using Heartbeat = TimBase<{...}, OnBeat>` 的装配，三件套的分工、PSC 与 ARR 的求解、APB1×2 的时钟规则，那篇都讲完了。有一件事咱们当时只是路过：配置凭什么能直接写进模板的参数位，这么写换来了什么，要付的又是什么。本篇咱们只扛一个问题，别的轴一概不展开。

换来的与要付的都得掂量，咱们需要一个对照的对象。ST 的官方路线把配置放在了运行时的结构体里，调用方在 RAM 里填好了一份 TIM_HandleTypeDef，再把它的指针递给 HAL_TIM_Base_Init，CubeMX 生成的工程默认就带着它，这样的工程成千上万。咱们就拿 tim.hpp 的写法跟它对拍，只看两处：填错的配置在哪一刻被拦下，编完的固件里配置变成了什么。

素材仍取自固定在提交 8026841 的子模块。笔者在开发电脑上用 GCC 16.2.1 配 `-std=c++23` 做的几段小实验，与 arm-none-eabi-g++ 16.2.0 编出来的 09_tim_heartbeat 固件产物，外加同款交叉编译器配 `-std=gnu++23` 的一次报错探针，实验都是 2026-10-28 那天跑的，数字与报错原文咱们原样贴。

## ST 的官方路线：句柄、五连断言与运行时读结构体

把 HAL 摆上来不是为了贬它，它是 ST 生态里的正统写法。驱动源码就冻在子模块自带的 third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/ 里（路径相对 libestdx 仓库根），咱们引的每一段都出自同一份文本。

调用方的姿势是这样的：在 RAM 里摆一个 TIM_HandleTypeDef，Instance 成员指着外设寄存器块的地址，Init 成员装着的配置叫 TIM_Base_InitTypeDef，旁边还挂着 State、Lock 与回调的指针。这样的打包结构体，行业里的叫法是句柄（handle），初始化的时候，咱们把句柄的指针递给 HAL_TIM_Base_Init，剩下的活由函数挨段干。

函数开头的十四行，咱们原样引：

```c
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim)
{
  /* Check the TIM handle allocation */
  if (htim == NULL)
  {
    return HAL_ERROR;
  }

  /* Check the parameters */
  assert_param(IS_TIM_INSTANCE(htim->Instance));
  assert_param(IS_TIM_COUNTER_MODE(htim->Init.CounterMode));
  assert_param(IS_TIM_CLOCKDIVISION_DIV(htim->Init.ClockDivision));
  assert_param(IS_TIM_PERIOD(htim->Init.Period));
  assert_param(IS_TIM_AUTORELOAD_PRELOAD(htim->Init.AutoReloadPreload));
```

头一段检查是真的，句柄为 NULL 的时候，函数就直接返回了 HAL_ERROR。后一段的五连 assert_param 查的才是配置内容，五个宏各管一样：实例的合法性、计数模式、时钟分频、周期与预装载。这些检查的成与不成，全看 assert_param 展开成了什么，咱们把它的两档原文都贴上：

```c
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#define assert_param(expr) ((void)0U)
```

头一行是定义了 USE_FULL_ASSERT 时的样子：表达式为假的时候就去调 assert_failed，报出的内容带着源文件名与行号，模板文件里只给了它的声明，实现留给了用户。第二行是默认的档位，在不定义 USE_FULL_ASSERT 的工程里，宏就成了一句空操作，咱们这套库用的正是默认档，五连检查在预处理之后就不在了。

咱们接着看 State 状态机的段落，下面这段与上面那段之间只隔了一个空行，连起来就是函数的全部：

```c
  if (htim->State == HAL_TIM_STATE_RESET)
  {
    /* Allocate lock resource and initialize it */
    htim->Lock = HAL_UNLOCKED;

#if (USE_HAL_TIM_REGISTER_CALLBACKS == 1)
    /* Reset interrupt callbacks to legacy weak callbacks */
    TIM_ResetCallback(htim);

    if (htim->Base_MspInitCallback == NULL)
    {
      htim->Base_MspInitCallback = HAL_TIM_Base_MspInit;
    }
    /* Init the low level hardware : GPIO, CLOCK, NVIC */
    htim->Base_MspInitCallback(htim);
#else
    /* Init the low level hardware : GPIO, CLOCK, NVIC */
    HAL_TIM_Base_MspInit(htim);
#endif /* USE_HAL_TIM_REGISTER_CALLBACKS */
  }

  /* Set the TIM state */
  htim->State = HAL_TIM_STATE_BUSY;

  /* Set the Time Base configuration */
  TIM_Base_SetConfig(htim->Instance, &htim->Init);

  /* Initialize the DMA burst operation state */
  htim->DMABurstState = HAL_DMA_BURST_STATE_READY;

  /* Initialize the TIM channels state */
  TIM_CHANNEL_STATE_SET_ALL(htim, HAL_TIM_CHANNEL_STATE_READY);
  TIM_CHANNEL_N_STATE_SET_ALL(htim, HAL_TIM_CHANNEL_STATE_READY);

  /* Initialize the TIM state*/
  htim->State = HAL_TIM_STATE_READY;

  return HAL_OK;
}
```

咱们挨段念。State 还是 RESET 的时候做的是首次初始化：Lock 摆到 HAL_UNLOCKED，USE_HAL_TIM_REGISTER_CALLBACKS 关闭的构建里，直调的是 HAL_TIM_Base_MspInit。它带着 `__weak` 的修饰，链接时您可以用同名的函数把它顶掉，干的活是时钟、引脚、NVIC 这层底层硬件，开关打开时调的就换成句柄里挂的回调指针。随后 State 置成了 BUSY，`TIM_Base_SetConfig(htim->Instance, &htim->Init)` 干的活是写寄存器，DMABurstState 与全部通道的状态摆到 READY，State 置成了 READY，最后返回了 HAL_OK。

配置的值怎么落到寄存器，咱们看 SetConfig 落笔的那两句，它前头还有从 Structure->CounterMode、Structure->ClockDivision 一路运行时读进 CR1 的动作：

```c
  /* Set the Autoreload value */
  TIMx->ARR = (uint32_t)Structure->Period ;

  /* Set the Prescaler value */
  TIMx->PSC = Structure->Prescaler;
```

配置在那一刻还在 RAM 的结构体里，寄存器拿到的值是运行时读出来的。HAL 的通用性正是这么来的：驱动的代码一行不改，换一份填好的 Init 就能换一套配置，回调想挂也挂上了。不过这样的通用是有前提的，得等配置的值在运行时才到手，比如参数是设备跑起来以后从通信或者存储里读来的，这时候驱动不用重编才算真省了事。CubeMX 生成的工程里，Init 是编译期写死的字面值，想改 Period 的时候，就得把固件重新编一遍了，这一点上它跟模板路线的处境没有差别。tim.hpp 只伺候 F103 上的 TIM1 到 TIM4，两边要的东西不一样，写法也就分了岔，咱们接着看库这边。

## 库的写法：配置折叠进类型，动作留在运行时

TimConfig 咱们单独再摆一次，上一篇是连同 TimPwmConfig 一起引的：

```cpp
struct TimConfig {
    TimInstance instance;
    std::uint32_t period_us;
    std::uint32_t timclk_hz = 64'000'000;
};
```

模板的签名与开头的常量，咱们也看一眼：

```cpp
template <TimConfig CFG, irq::IrqHandler Handler>
struct TimBase {
    static constexpr auto kDiv =
        detail::resolve_ticks(detail::ticks_for_period(CFG.timclk_hz, CFG.period_us));
```

参数位上写的不是 typename。那里站着的是一枚 TimConfig 类型的值，这类参数的正式名字叫非类型模板参数（non-type template parameter，文档里也常见 NTTP 的缩写），C++26 草案把这个名字改成了常量模板参数（constant template parameter），cppreference 的词条已按新名标注，本篇咱们仍按 C++20/23 的名字说。这类参数的老面孔是整数字面值，std::array 的大小就是，类类型能站上来则是 C++20 才放开的事。09 的装配上一篇咱们引过，形状是这样的：`TimBase<{.instance = ..., .period_us = 1'000'000}, OnBeat>`，花括号里 `.字段名 = 值` 的写法叫指定初始化器（designated initializer），它现在直接站在了模板实参的位置上。

这件事笔者在开发电脑上验过一轮，用的是 GCC 16.2.1 配 `-std=c++23`。功能测试宏 `__cpp_nontype_template_args` 的值是 201911，对应的正是 C++20 那一回的放宽<RefLink :id="1" preview="cppreference, Template parameters" /><RefLink :id="4" preview="GCC, C++ Standards Support" />。最小的正例只用了两行：

```cpp
struct Cfg { int a; int b; };
template <Cfg c> struct X { static constexpr int sum = c.a + c.b; };
```

咱们拿 `X<{.a = 2, .b = 3}>::sum` 过了一遍编译，折叠成了常量，算出来的值是 5。配置没有经过任何运行时的变量，它成了类型的一部分。

类类型进参数位是有门槛的，标准里的说法是 structural type。白话讲：一个类的非静态数据成员都得是公共的，基类也得是公共的，成员本身还得是能在编译期求值的字面类型，出现了私有成员，资格就没了。从提案的脉络看，P0732R2 是最初落地的设计，里头已经在用 strong structural equality 的说法，P1907R1 做的是放宽与一致性修订，立住 structural type 这个术语的正是它<RefLink :id="2" preview="Jeff Snyder, Louis Dionne, P0732R2" /><RefLink :id="3" preview="Jens Maurer, P1907R1" />。编译器的进度也有讲究，GCC 9 在 C++20 模式实现的是 P0732R2 的最初设计，P1907R1 的修订 GCC 10 起部分跟上，GCC 11 才算齐了，咱们本机量到的宏值与文档对得上。咱们顺带提一句旧事：P1837R0 的提议是把类类型从 C++20 里撤掉，不过委员会没有采纳<RefLink :id="7" preview="Arthur O'Dwyer, P1837R0" />。

配置进了类型，运行时的活并没有消失，库把两者分了家。窗口的启动与收回就是运行时动作，咱们把 TimOneShot 的两个成员原样引：

```cpp
    static void arm() {
        auto* const t = HW::regs();
        t->CR1 |= TIM_CR1_OPM;
        t->EGR = TIM_EGR_UG;
        t->SR = ~TIM_SR_UIF;
        t->CR1 |= TIM_CR1_CEN;
    }

    static void disarm() { HW::regs()->CR1 &= ~(TIM_CR1_OPM | TIM_CR1_CEN); }
```

窗宽 20ms 在编译期就定下了，而开与收都发生在运行时，两头走的是不同的机制。开窗的那一步，EXTI 的边沿在运行时调 `Window::arm()`，链路咱们上一篇串过，10 号示例里干这件事的 OnEdge，函数体咱们原样引：`static void operator()() { Window::arm(); }`。收窗的那一步没有人插手，`arm()` 置上的 OPM 位让定时器进了单发模式，数到头硬件自己就停了，上一篇讲单脉冲模式时说的就是它。咱们还反查过 10 号示例的全文，`disarm()` 的调用次数是零，它是库留给别的用途的门，手动收窗的活归它管。

## 错误暴露的时机：编译期的报错与空操作的断言

填错配置的时候，两条路各在什么时刻告诉您，咱们各做了一次实验。

头一个负例动的是私有成员。把带私有成员的类 Bad 放进模板参数位，编译当场就退了回来，报错的头两行咱们原样贴：

```text
error: 'Bad' is not a valid type for a template non-type parameter because it is not structural
note: 'Bad::x' is not public
```

您看，报错把两样都说全了：Bad 不够格做模板的参数，原因是 Bad::x 不是公共的成员，structural 的条件就卡在这里。错误停在了编译期，固件还没开始编就被拦下了。

另一个负例错的是类型。咱们照着 Bad 的路子又摆了个模板 Z，参数位上站的是另一个也叫 Cfg 的最小结构体，字段照 TimConfig 的 period_us 裁成了 std::uint32_t，这回把字符串字面量塞给了整数字段，`Z<{.period_us = "1ms"}>` 的报错原文是：

```text
error: could not convert '{"1ms"}' from '<brace-enclosed initializer list>' to 'Cfg'
```

`"1ms"` 进不了 std::uint32_t 的字段，模板实参的那一关就过不去了。您顺带也看见了，报错里把填进去的配置值原样印了出来，咱们把它记下，等讲到代价的部分再说。

咱们换到 HAL 那边，看同一类笔误的待遇。默认档里的五连 assert_param 是空操作，预处理完就不在了。句柄的判空是真检查，可它查的是指针，不是配置的内容。您想让这些检查活过来的话，就得把 USE_FULL_ASSERT 的宏定义出来，assert_failed 的实现也得您自己来写，而检查跑起来的时机也还在运行时，也就是设备上初始化的那一刻。同一份填错的 Period，库这边在编译期就拦下了，HAL 的默认配置下要等到设备跑起来才可能被发现，而发现得了还是发现不了，还得看断言开了没有。

## 指令流里的立即数：配置怎么编进固件

笔者拿 arm-none-eabi-nm 过了一遍 09 的固件文件，接上 `grep -c HAL_TIM` 的过滤，数出来的结果是 0：HAL 的 TIM 驱动压根没链进来，管 TIM 寄存器的活全在 tim.hpp 里直写。HAL 倒是没有被整个扔掉，09 的 main.cpp 还在调 HAL_Init 与 HAL_RCC_OscConfig，只是 TIM 的寄存器不归它管。

配置落进固件是什么样子，咱们把 09 构建产物里 TimBase 装配那段的反汇编摘出来，箭头与省略的那行是白话注释，指令的字节一个没动：

```text
800151c: f04f 4380  mov.w r3, #1073741824 @ 0x40000000   ← TIM2 基址
8001520: f240 38e7  movw  r8, #999 @ 0x3e7               ← PSC
8001524: f64f 1eff  movw  lr, #63999 @ 0xf9ff            ← ARR
8001528: f06f 0c01  mvn.w ip, #1                          ← ~UIF
...(中间是 RCC 时钟门的开与读回,非本篇素材)
8001548: f8c3 8028  str.w r8, [r3, #40] @ 0x28            ← PSC 写进 TIM2+0x28
```

999 与 63999 就是上一篇求解器解出的那对值，来历咱们不重讲。它们在这段指令流里是 movw 的立即数，也就是直接编进指令里的常数，装进寄存器就写往 TIM2+0x28 的 PSC，中间没有一次从 RAM 读配置的动作。TIM2 的基址 0x40000000 也是立即数，它是 TimInstance::Tim2 按地址取值带下来的，这层便利上一篇交代过了。对照就是 ST 官方路线里引过的那句对 ARR 的赋值，那边的寄存器值要等运行时才从 RAM 里读出来。配置经过了类型的折叠，从 RAM 里的结构体变成了固件代码区里的常数。

## 代价与边界

换来了什么，前两节都摆开了：错误停在了编译期，配置折成了立即数。该付的代价有几样，咱们挨个交代。

第一样代价在报错的消息里。负例二那句 could not convert 您已经见过，那还是两行结构体的最小例子，真到了库里它会变成什么样，笔者拿真实的装配探过一回，装配写成了 `TimBase<{.instance = TimInstance::Tim2, .period_us = "1ms"}, OnBeat>`，用的还是 arm-none-eabi-g++ 16.2.0 配 `-std=gnu++23`，构建选项与 09 的相同，报出来的主错就一行：

```text
nested.cpp:3:80: error: could not convert '{estdx::stm32f1::TimInstance::Tim2, "1ms"}' from '<brace-enclosed initializer list>' to 'estdx::stm32f1::TimConfig'
```

真正的报错就这一行，后头跟着的只是 `expected type-specifier` 与 `has not been declared` 的两行连带错。您再对比消息的形状，跟最小例子里的一样，变长的只有里头印出来的配置值，Tim2 这一项打成了 `estdx::stm32f1::TimInstance::Tim2`，命名空间全都带上了。

另一头的代价是类型数量。一份配置就对应了一个类型，09 与 10 用到的三个定时器，就是三次独立的实例化，类型各是各的，编译的时间与代码的体积都按份数走，好在检查与折叠也各归各的。体积的实情上一篇量过，确认版的 text 反而比纯日志示例小，这里咱们不重复量。

配置在运行时改不了。period_us 写进了类型，想换周期的时候，就得把固件重新编一遍了。运行时真正会变的部分，库交给了 arm() 这样的调用与 OPM 的自停去扛，窗的开与收都不用碰配置。咱们要是想运行时换周期，除了重建没有别的路。

浮点与指针也进得了模板实参的名单，C++20 把它们放行了。咱们拿 `struct F { float f; int i; }` 配 `W<{.f = 1.5f, .i = 2}>` 试过，编译也通过了。库里的两份配置用的全是整数字段，咱们只当边界提一句，您真要用浮点，门槛还是那套 structural 的条件。

配置进类型的第一笔收获，上一篇咱们已经拿到过了：那两道 static_assert 把出界的周期拦在了编译期，超差的精度也一起拦下了。第二笔收获在量程的身上，还是在同一个 timclk 的约束下，16 位乘 16 位的量程到底装得下哪些周期，下一篇咱们把量程的边界算清楚。

## 注意事项与常见错误

| 症状 | 原因 | 解法 |
|---|---|---|
| 报 `'Bad' is not a valid type ... because it is not structural` | 类带了非公共成员，过不了 structural 的条件 | 成员改公共，或把配置拆成整数字段 |
| 报 `could not convert ... to 'Cfg'` | 指定初始化器里的值与字段类型对不上 | 按字段类型填值，period_us 认的是 uint32_t |
| 想在运行时改周期 | 周期已折叠进类型，固件里是指令流的立即数 | 改配置重建固件，运行时的变化交给 arm() 与 disarm() |
| HAL 工程里错配置没人拦 | 默认档的 assert_param 是空操作，五连检查不在 | 定义 USE_FULL_ASSERT 并实现 assert_failed，检查在运行时跑 |
| 浮点字段想进模板实参 | C++20 起放行，条件仍是 structural | 整数字段够用就不必上浮点，库里的两份配置全整数 |

## 参考资源

<ReferenceCard title="参考文献">
  <ReferenceItem :id="1" author="cppreference" title="Template parameters" url="https://en.cppreference.com/w/cpp/language/template_parameters" chapter="非类型模板参数与 structural type 的规则，C++26 起这个参数改叫常量模板参数" />
  <ReferenceItem :id="2" author="Jeff Snyder, Louis Dionne" title="P0732R2: Class Types in Non-Type Template Parameters" :year="2018" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0732r2.pdf" chapter="类类型进非类型模板参数的最初设计" />
  <ReferenceItem :id="3" author="Jens Maurer" title="P1907R1: Inconsistencies with non-type template parameters" :year="2019" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1907r1.html" chapter="structural type 一词的出处，放宽与一致性修订" />
  <ReferenceItem :id="4" author="GCC Project" title="C++ Standards Support in GCC" url="https://gcc.gnu.org/projects/cxx-status.html" chapter="类类型非类型模板参数的实现进度，GCC 9 落地 P0732R2，P1907R1 的修订 GCC 10 起部分、GCC 11 完整" />
  <ReferenceItem :id="5" author="STMicroelectronics" title="stm32f1xx_hal_tim.c 与 stm32_assert_template.h" chapter="HAL_TIM_Base_Init 与 TIM_Base_SetConfig，assert_param 的两档展开，文本冻在 libestdx 子模块 third_party/STM32F1/Drivers/STM32F1xx_HAL_Driver/，tim.c 在 Src/ 子目录，stm32_assert_template.h 在 Inc/ 子目录" />
  <ReferenceItem :id="6" author="Awesome-Embedded-Learning-Studio" title="libestdx（boards/stm32f1/tim.hpp，子模块固定在提交 8026841）" :year="2026" url="https://github.com/Awesome-Embedded-Learning-Studio/libestdx" chapter="TimConfig, TimBase, TimOneShot, arm/disarm" />
  <ReferenceItem :id="7" author="Arthur O'Dwyer" title="P1837R0: Remove NTTPs of class type from C++20" :year="2019" url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1837r0.html" chapter="把类类型 NTTP 从 C++20 撤掉的提议，委员会未采纳" />
</ReferenceCard>
