---
title: "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
description: "commit feef8fb 把开局工程篇欠下的兜底兑现成了遗言报告,naked 的 HardFault_Handler 用 tst 加 ite 判出 MSP 与 PSP,尾部一跳不带一个字节,FaultReport 带着 CFSR、HFSR、BFAR 与八字的硬件现场,金丝雀 0xEF114514 埋在 spawn 与 idle 的栈底,巡逻长在 context_switch 里借 PendSV 的路免费跑,零分配格式化器用一个 {} 让类型当文法、重载当扩展点,printdec 就此退役,我在 /tmp 副本交叉构建,在 Renode 按 x 注入 udf 看到完整遗言(cfsr=0x00010000 的 UNDEFINR 配 hfsr=0x40000000 的 FORCED),按 y 砸自家金丝雀等下一次切换报出 eaten=2,host 侧 test_format 七用例十五断言全绿"
chapter: 5
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "中断只递条子:环形队列、工作者线程与底半推迟"
  - "等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货"
---

# 芯片死前立遗嘱：HardFault 现场、栈金丝雀与零分配日志

《工程搭建：从空仓库到 Renode 的第一行输出》的向量表里，HardFault 的槽位指着 `fault_handler`，它打印一行 `[FAULT] halted` 就停机了。我当时把话写在那里，说的是完整的现场捕获，要留给后面内核里的 HardFault 模块来做。您把芯片想象成猝死的人，就会觉得这样的兜底连死亡证明都算不上，人没了，死在哪一行、手里抓着什么寄存器、踩的是谁的栈，咱们一个字都不知道。野外的固件坏起来就是这么无声的，板子就凉着了，您手里只有一根 USB 线和一个猜不透的现场。

commit `feef8fb` 把欠着的现场兑现了，subject 写的是 “feat: fault_package, logs ok”，我数过它的 diff，17 个文件的改动，新增了 429 行，删除了 45 行。咱们本站的主题是**代码跑在哪个上下文**，本篇管其中的 fault 上下文，也就是崩了之后，处理代码跑到谁的世界里、用谁的栈、还能不能说话。中断把重活卸给工作者的路数，归的是 02《中断只递条子》的环形队列加工作者。不用任务也能等三百毫秒的路数，归的是 03《等三百毫秒，不养一个任务》的软件定时器。它们是同一次主题下的姊妹篇，我这里不越界。今天的正文里，咱们把遗言挨个讲清：一个不污染现场的捕获口，一根埋在栈底的金丝雀，还有一个不碰堆就能打数字的格式化器。

## naked 的捕获口：现场一个字节都不许动

一份合格的遗言该是什么样子，咱们得在亮代码前把它说定。芯片猝死的那一下，出事的时候谁在跑、死在哪一行、死因是什么，缺了哪一样都算不上遗嘱，而旧的 `fault_handler` 只留下一行 `[FAULT] halted` 就把人打发走了。遗言的形态也就此定下来了：它得是一份只装数据的报告，内核负责的是把现场收进报告里，打印的事归板级登记的口子，两边各干各的。遗言的声明面住在 `include/ZerOS/arch/arm_cortex_m3/fault.hpp` 里，作者自己的注释就把比喻定下了，我原样搬给您，头一段是遗嘱的正文：

```cpp
#pragma once

// The last will and testament: when the chip dies, it dictates the scene
// before lying down. Declaration face only — bodies live in fault.cpp.

#include <cstdint>

namespace ZerOS::arch::cortex_m3 {

struct FaultReport {
    std::uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr; // the hardware-pushed frame
    std::uint32_t cfsr, hfsr, bfar;                  // SCB fault status & address
    const char* task_name;                           // who was running ("boot" if nobody)
    std::uint8_t task_prio;
    const std::uint32_t* frame;                      // stack top, for dumping
};
```

`FaultReport` 就是遗嘱的正文了，报的是三个来源的东西：硬件替咱们压好的八字现场（r0 到 xpsr，《在栈顶伪造出生证明》里伪造帧时咱们逐个打过交道），SCB 里三个故障状态寄存器的值，还有当时正在跑的任务的名字与优先级。

遗嘱的正文立住了，接下来要登记的是接收的口子，咱们看头文件的另一半：

```cpp
using FaultSink = void (*)(const FaultReport&);
inline FaultSink fault_sink{nullptr}; // the board wires its printer here

} // namespace ZerOS::arch::cortex_m3
```

`FaultSink` 是个裸的函数指针，内核只负责把 `FaultReport` 填好了递出去，打不打印、打到哪儿去的问题，都是板级说了算的。您细想就会看出，这样的分工让内核保持了沉默。

声明面顶上的 `—` 我留着没改，因为它是仓库的原话。捕获的本体在 `src/arch/arm_cortex_m3/fault.cpp`，开场的那段汇编值得您一个字一个字地读：

```cpp
extern "C" [[gnu::naked]] void HardFault_Handler() {
    asm volatile("tst    lr, #4\n" // EXC_RETURN bit2: which stack was in use
                 "ite    eq\n"
                 "mrseq  r0, msp\n"
                 "mrsne  r0, psp\n"
                 "mov    r1, lr\n"
                 "b      hardfault_capture\n"); // tail jump — no push on the way
}
```

`[[gnu::naked]]` 咱们在《在栈顶伪造出生证明》里见过，编译器就不生成序言与尾声了，栈上的一切归汇编自己管。这里比上一次更苛刻的地方在于，它连压栈的动作都不做。原因就藏在 `tst lr, #4` 里：异常进来时硬件往 `lr` 放的 EXC_RETURN，第 2 位说的是出事那一刻用的是哪根栈，为 0 用的是 MSP（中断与启动早期的那根），为 1 用的是 PSP（任务的那根）。`ite eq` 是 Cortex-M3 的条件块指令，后面的 `mrseq` 与 `mrsne`，一条在等号成立时取的是 MSP，另一条在等号不成立时取的是 PSP，四条指令跑完了，出事的现场在哪根栈上就判出来了。判出来的结果放到了 r0，`lr` 原样挪进了 r1 里存着作证，最后的 `b` 是一个尾跳，落进普通的 C++ 函数里。注释写的 “no push on the way” 就是全段的底线了：尾跳不经过任何压栈，出事现场的每个字节都保持着芯片倒下那一刻的原样。

为什么咱们要这么计较？您想，如果处理程序是普通的函数，编译器就会往当前的栈上压一堆寄存器存档，而此刻用的栈可能正是死者的栈，压下去的每一个字都踩在了遗物上。naked 加尾跳的组合让捕获代码站到了现场之外，咱们在别人的废墟上取证，自己也不往废墟里扔东西的。

接手的 C++ 函数把遗嘱填好，咱们看它的头一段：

```cpp
extern "C" void hardfault_capture(std::uint32_t* frame,
                                  std::uint32_t exc_return [[maybe_unused]]) noexcept {
    using ZerOS::arch::cortex_m3::FaultReport;
    using ZerOS::sched::zeros_impl::TCBKeys;

    const char* name = "boot";
    std::uint8_t prio = 0xFF;
    if (auto cur = ZerOS::system::os().current_task()) { // boot crash: nobody runs
        name = TCBKeys::name(*cur);
        prio = TCBKeys::prio(*cur);
    }
```

死亡的时间可能落在两个世界里，咱们分开看。任务跑着的时候崩的，`current_task()` 拿得到自己的主人，名字与优先级就从 TCB 里问出来了。调度器还没开张就崩的，`current_task()` 交回来的是空，名字落回默认的 `"boot"`，注释说的 “nobody runs” 指的就是它。调度器还没出生的历史时期，这一句兜的是开机头几毫秒的死法。您顺着往下看，函数把三个故障寄存器读进了报告，地址都是写死的：CFSR 落在了 0xE000ED28，HFSR 落在了 0xE000ED2C，BFAR 落在了 0xE000ED38。报告递给了 `fault_sink` 之后，函数自己落进一个 `wfi` 的死循环，注释的原文是 `testament delivered; lie down`，说的就是遗嘱交完了就地躺下。

## 遗言怎么读：CFSR、HFSR、BFAR 的位段

三个寄存器交下来是一串十六进制的数，值不值钱的事儿，看咱们会不会读。ARMv7-M 的口径咱们按《ARMv7-M Architecture Reference Manual》来记：CFSR（Configurable Fault Status Register）是个 32 位的三合体，低字节归的是 MemManage，第二个字节归了 BusFault，高半字归了 UsageFault。HFSR（HardFault Status Register）里咱们最关心的是 bit 30 的 FORCED，它置 1 的意思是本来的轻量故障没能被接住，被硬件强迫升级成了 HardFault。BFAR 是 BusFault 的地址寄存器。不过它有个开关，CFSR 的 BFARVALID（bit 15）置了位，它才有了意义。

空口的位段表不如一句真的遗言。咱们到后面的验收段落里，拿 Renode 注出来的 `cfsr=0x00010000 hfsr=0x40000000` 现场对一对号，您会看到 UNDEFINR 与 FORCED 两个位和手册一一对上了。

::: warning
BFAR 输出 0x00000000 不代表总线访问的坏地址就是零，它只代表 BFARVALID 没置位的情形，寄存器里没有有效的值。咱们读 fault 报告的时候，每个地址寄存器都得拿着它的 VALID 位作准，单独的数字作不了证。位段表我核的是 ARMv7-M 手册的口径，具体的位在验收段落里拿真输出验过 UNDEFINR 与 FORCED 两个，其余的位您用的时候照手册查。
:::

## 栈金丝雀：挂在切换路径上免费巡逻

栈溢出是裸机世界里最常见的死法之一，死状又特别迷惑：冲坏的往往是别人家的数据，崩的地点离作案的地点隔着好几个任务。查案用的是矿上的老手艺，矿工下井要带一只金丝雀的，鸟一晕人就撤了。咱们看 ZerOS，它把鸟埋进了 TCB 的栈视图里，代码住的地方是 `include/ZerOS/kernel/sched/stack_guard.hpp`：

```cpp
namespace ZerOS::task {
constexpr inline std::uint32_t kCanaryWord = 0xEF114514;
constexpr inline std::size_t kCanarySize = 8;

struct TaskGuardHelper {
    static void bury_canary(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        // word-wise: memset fills BYTES, it would chop the pattern to 0x14
        for (std::size_t i = 0; i < kCanarySize && i < stack_view.size(); ++i) {
            stack_view[i] = kCanaryWord;
        }
    }
```

咱们看埋的位置与几何。八个哨字（每个都是 `0xEF114514`）占的是栈视图最低的 32 字节，那里是整根栈的最后一道防线。任务的栈是从高地址往低地址长的，溢出的第一刀啃的就是哨字里最高的 `view[7]`，啃穿了它之后，再向下吃的是 `view[6]`、`view[5]`，一路向着最低的地址去。埋的手法是逐字写的，注释把一个真实的误判留了下来：`memset` 是按字节填的，`0xEF114514` 的四个字节里三个是 0x14，填出来的是一条一色的条纹，八个字的花纹没了，查案就不认得了。

咱们看检查，它分了两层，便宜的在前：

```cpp
    static bool fast_check_stack(sched::TCB& t) {
        // the deepest sentinel is the first thing an overflow eats
        return sched::zeros_impl::TCBKeys::stack_view(t)[kCanarySize - 1] != kCanaryWord;
    }

    static std::size_t eaten_length(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        std::size_t length = 0;
        for (std::size_t i = 0; i < kCanarySize; ++i) {
            if (stack_view[i] != kCanaryWord) {
                length++;
            }
        }
        return length;
    }
```

`fast_check_stack` 只读刀口上的 `view[7]` 一个字，不等了就过，每次切换的巡逻税就是一次比较加一次访存。等到它发现不对了，`eaten_length` 才挨个把八个字数了一遍，报出来的就是被吃掉几只。我挺喜欢它便宜的形状，查案的成本常年是零，定损的成本只在出事之后付一次。

埋鸟的时机在出生的时候，咱们看：

```cpp
void spawn(ZerOS::sched::TCB& t) {
    task::TaskGuardHelper::bury_canary(t);
    fabricate_frame(t);
    system_sched.add(&t);
}
```

`spawn` 埋完了金丝雀再伪造帧再入队，idle 任务在 `start_scheduler` 里走的也是同一道 `bury_canary`，谁的栈都没有漏。真正的巡逻长在哪儿呢？长在《在栈顶伪造出生证明》里咱们读过的 `context_switch` 里，`feef8fb` 给它添了一段：

```cpp
extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    using TaskGuardHelper = task::TaskGuardHelper;
    if (saved_sp != nullptr) {
        auto prev = system_sched.current_task();
        TCBKeys::sp(*prev) = saved_sp;
        if (TaskGuardHelper::fast_check_stack(*prev) &&
            TaskGuardHelper::overflow_reporter != nullptr) {
            TaskGuardHelper::overflow_reporter(prev, TaskGuardHelper::eaten_length(*prev));
        }
    }
    return TCBKeys::sp(*system_sched.pick_next());
}
```

巡逻的搭车逻辑，您看 `saved_sp` 就明白了：非空的 `saved_sp` 意味着真的有一个任务被切下了 CPU，PendSV 把它的现场存好了才走到这里，此刻正是检查它的最好时机，它的栈已经完整落定，查的是定格的证据。首切走的是 `svc 0`，`saved_sp` 是空的，没有任务被切下来的，检查自然就跳过了。每次任务切换本来就必经的是该函数，金丝雀的巡逻一趟不落，却没有为它多开一根中断、多起一个任务的，不为巡逻多占任何运行时的资源，每趟只多付一次比较的代价。

检出问题后的去向也是老路数的延续：`overflow_reporter` 又是一个裸的函数指针，内核报的只是数，板子来做的定夺。demo 里的接线打印一行 `[STACK OVERFLOW] task=c eaten=2` 然后就停机，真的输出咱们在 Renode 段落里见。

## 零分配格式化器：类型当文法，重载当扩展点

遗言要能打出来的话，咱们得有办法把十六进制和十进制变成字符。开局那几篇里的 `printdec` 干过十进制的活，手搓的逆序取余数循环，《一秒钟的定义权》的 ticker 也一直在用它。它有的只有十进制一个本领，可遗言要的是 `pc=0x08000460` 这样的带格式的行，`printdec` 就接不住了。`feef8fb` 让它退了役，换上来的是 `include/ZerOS/log/format.hpp`。

接替的路数，摆在咱们面前的本来有两条，而且是两条走不通的。沿着 printdec 的老路走下去，十进制之外的每种格式都得再手搓一个印数的函数，遗言的格式每变一回，内核里的代码就得跟着回炉了。立一套格式串文法的路也摆在那里，printf 认 `%08x` 的那套花样就是现成的样子，可文法离了解析器是转不动的。`feef8fb` 的答案两条都没有走，每个值带着自己的类型进场，怎么印由类型自己说了算。

咱们把头文件分两段看，头一段装的是门面与立场：

```cpp
#pragma once

// The whole "upper-layer formatter" the architecture allows (kernel stays
// formatting-free). One placeholder "{}" — the TYPE says how to print, so
// there is no format-string grammar and no parser:
//
//   format_to(buf, "task={} pc=0x{}\r\n", "H", Hex{0x08000F42u});
//
#include <cstddef>
#include <span>

#include "ZerOS/log/format_impl.hpp"

namespace ZerOS::log {
```

设计的底线写在注释里了，咱们对着代码看。您看，占位符就只有 `{}` 这么一个，文法由类型说了算：传 `Hex` 就打八位的大写十六进制，传 `Dec` 打的就是十进制，格式串里没有 `%08x` 那样的花样，也就不需要解析器了。

门面立住了，咱们接着看干活的模板本体，无分配的声明就压在它的头顶：

```cpp
// No allocation, no state, no locks → safe from tasks, ISRs and fault
// handlers. Returns the number of chars written (truncates at out.size()).
template <typename... Args>
std::size_t format_to(std::span<char> out, const char* fmt, Args... args) {
    char* cursor = out.data();
    char* limit = out.data() + out.size();
    [[maybe_unused]] auto print_arg = [&](auto&& arg) {
        if (format_impl::emit_plain(fmt, cursor, limit)) { // a placeholder for it?
            format_impl::emit_value(cursor, limit, arg);   // a plain call, one body
        }
    };
    (print_arg(args), ...);
    while (*fmt != '\0') { // trailing plain text
        format_impl::emit(cursor, limit, *fmt++);
    }
    return static_cast<std::size_t>(cursor - out.data());
}

} // namespace ZerOS::log
```

它是无分配、无状态、也无锁的，所以咱们在任务里、中断里、fault 处理里都敢用，遗言恰恰必须在不能碰堆的地方打出来，无分配对它而言是入场的资格，美德就谈不上了。缓冲区满了就截断，随行的还有 `cursor` 与 `limit`，越界的东西是写不出去的。

`(print_arg(args), ...)` 是 C++17 的折叠表达式，参数包按着次序展开了，每个实参问的问题是格式串里还有没有占位符等着它，有才把自己的值交给 `emit_value`（代码注释里的那句 “a plain call, one body” 咱们下面马上用上）。多出来的实参安静地被略过，占位符多出来的部分原样留在输出里，两个方向的失配都不炸。

扩展点的形状在 `include/ZerOS/log/format_impl.hpp` 的注释里写得明白，新类型想上印刷机的话，就在本文件里加一个值包装的类（就像 `Hex`、`Dec`），再添一个 `emit_value` 的重载，`format.hpp` 自己的一行都不用动。重载决议替咱们把类型分派给正确的印刷函数，“类型即文法”的落点就在这里了。咱们看两个现成的印刷体的样子：

```cpp
void emit_value(char*& cursor, char* limit, Hex h) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) { // fixed 8 digits, zero padded
        emit(cursor, limit, kDigits[(h.value >> shift) & 0xFu]);
    }
}

void emit_value(char*& cursor, char* limit, Dec d) {
    char tmp[10]; // 2^32 needs at most 10 digits
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + d.value % 10);
        d.value /= 10;
    } while (d.value != 0);
    while (n > 0) {
        emit(cursor, limit, tmp[--n]);
    }
}
```

咱们看 `Hex`，它固定打的是八位，零也给填上了，大写的输出，遗言里的 `0x08000460` 就是它的手笔。`Dec` 还是 `printdec` 的老手艺，逆序取余再倒着写的老路子，不过它换了个岗位：从板级的手搓函数变成了格式化器的一个重载，十进制从此也归到了类型这边，不再是手搓的特例。

还有一处编译期的巧思藏在文件头，注释的原文是 `ONE copy per binary, no matter how many translation units call format_to` 这一句，它说的就是整个二进制里的身体只有一份，翻译单元来调了多少个都一样。模板的展开本来会把循环内联进每个调用点，二进制里就复制出几十份一模一样的循环了。仓库的拆法是模板薄壳加普通函数的身体，身体只有 `src/log/format_impl.cpp` 的一份，`feef8fb` 把它归进了新立的 `zeros_common` 静态库，板包与 host 测试都是从库里链的。代码体积在咱们项目里是写着预算编号 D12 的硬约束，一份身体就是一份 flash 的开销。

咱们再看板级这边，`uart.hpp` 里的 `printdec` 没了，顶上来的是：

```cpp
// one-shot formatted line: fixed buffer, polled out — task/ISR/fault safe
template <typename... Args>
void format(const char* f, Args... args) {
    char buf[160];
    const auto n = ZerOS::log::format_to(buf, f, args...);
    for (std::size_t i = 0; i < n; ++i) {
        uart1_putc(buf[i]);
    }
}
```

您看它的一次性用法，160 字节的栈上缓冲，格式化完了逐字节轮询发出，任务、ISR、fault 三处都是使得上的。轮询发送在《工程搭建》那篇里就聊过了，它是慢的，但是死到临头的遗言不赶时间，慢而稳就刚刚好了。

::: warning
一条缝我还照实留着没填：时间内核那篇《一秒钟的定义权》里见过的 `global_channel.hpp`，`RegisterGlobalChannel` 到 commit `feef8fb` 依然只有声明而没有实现，那篇里被注释掉的两行断言日志还挂着。`feef8fb` 交的是格式化器与 fault 报告两块地基，全局日志通道的门面还是空的，真要接通得等后面的活了。
:::

## host 考场：七用例对文法

格式化器是纯的逻辑，host 上考它正合适不过，考卷是写好的 `test/test_format.cpp`，出的是七个 `TEST_CASE`。咱们挑三个看，它们各守文法的一条边界。头一个管十六进制的格式：

```cpp
TEST_CASE("hex is 8 digits zero padded, uppercase", "[format]") {
    CHECK(run("{}", Hex{0u}) == "00000000");
    CHECK(run("{}", Hex{0xDEADBEEFu}) == "DEADBEEF");
    CHECK(run("{}", Hex{0x0000000Fu}) == "0000000F");
}
```

零、满值、低位补零三种形状咱们都对过了，`0x0000000F` 打出来的是 `0000000F`，填充的格式就这样定了。第二个管失配的两个方向：

```cpp
TEST_CASE("more args than placeholders: extras are ignored", "[format]") {
    CHECK(run("only {}", "used", "dropped") == "only used");
}

TEST_CASE("more placeholders than args: leftover {} stays visible", "[format]") {
    CHECK(run("got {} want {} - only one", "a") == "got a want {} - only one");
}
```

实参多了就被略过，占位符多了就原样可见。第二条我觉得选得漂亮，日志里一个没了着落的 `{}` 是明晃晃的告示，比静默吞掉的要诚实得多，排错的您一眼就知道哪个值没接上。第三个管的是截断：

```cpp
TEST_CASE("buffer full: truncates, never overruns", "[format]") {
    char small[4];
    const auto n = format_to(small, "abcdefgh");
    CHECK(n == 4);
    CHECK(std::memcmp(small, "abcd", 4) == 0);
}
```

咱们看截断的用例，四个字节的缓冲塞八个字母，写出去的正好是前面的四个，返回值也如实报了四个。该用例是 fault 现场安全性的直接考题，`limit` 那道墙真的挡住了。剩下的用例管纯文本的透传、占位符与文本的交错次序、十进制的零与满值，我不逐个念了，您跑一遍就全见得着了。

## Renode 验收：按 x 立遗嘱，按 y 砸金丝雀

我自己复现的链路照旧：拿 `git archive feef8fb` 把树导出到 /tmp 的副本，CMSIS 是从子模块拷进去的，交叉构建的两条命令与上一篇一样：

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

体积是真实的，遗言三件套加上 demo 全住了进去，咱们看：

```text
   FLASH:        4812 B        64 KB      7.34%
     RAM:        2448 B        20 KB     11.95%
   text	   data	    bss	    dec	    hex	filename
   4620	     192	   2256	   7068	   1b9c	.../zeros-bluepill.elf
```

flash 涨到了 4812 字节，距离 D12 的 6KB 预算线还剩约 1.2KB，RAM 的 2256 字节 bss 里多了金丝雀与更大的任务栈。预算还守得住的，不过空间在肉眼可见地变矮，我照实报数。

demo 的 `main` 里多了两根接线，注释一句话就把分工说完了：`the two testaments: kernel catches, board decides (print + halt)`（两份遗嘱都是内核来捕获、板子来定夺的，定夺完了就打印，然后就停机了）。任务 c 供咱们按键实验，按 `x` 的话，它执行一条 `udf #0`（未定义的指令，CPU 级的故障，模拟器是不会宽容它的），按 `y` 它砸自己的金丝雀。砸的动作在源码里有一段长的注释，我原样搬给您：

```cpp
// 'y' smashes task C's own canary — an honest simulation of what a real
// overflow leaves behind. (Actually burning the stack with recursion is a
// fight against the optimizer: GCC turned three "unfolds" in a row into
// loops. The mechanism under test — eaten sentinel → PendSV patrol →
// report — doesn't care who ate the words.)
```

它说的是诚实的作弊：您想用递归把栈真烧穿就得跟优化器打架，GCC 把三次手写的展开折回了循环，所以 demo 直接把最深的两只鸟掐死，被测的机制不关心吃字的凶手是谁。

注入按键的路子我交代一下，因为 Renode 的串口分析窗在无头模式下敲不了键。我照的是 Renode 上游自带的演示脚本（`scripts/complex/hci_uart/hci_uart.resc`，本机装在 /opt/renode 之下）的路子，给 usart1 挂了一个 socket 终端，再用几行 Python 从 TCP 的那头把字节送进去。Renode 侧的命令在 start 之后 sleep 10 才退出，注入得赶在它的十秒窗口里完成，我是在仿真跑到 8 秒的时候送的字：

```shell
renode --console --disable-xwt \
  -e "emulation CreateServerSocketTerminal 34568 \"term\"" \
  -e '$bin=@build/src/board/stm32f103_bluepill/zeros-bluepill.elf' \
  -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" \
  -e "connector Connect sysbus.usart1 term" \
  -e "start" -e "sleep 10" -e "quit" &
```

```python
import socket
s = socket.create_connection(("127.0.0.1", 34568))
s.sendall(b"x")  # 按 y 的实验就把 b"x" 换成 b"y"
s.close()
```

咱们看按 `x` 的遗言，串口窗的真实输出如下：

```text
[INFO] usart1: [host: 5.59s (+51.49ms)|virt:   8.04s (+42.58ms)] c: executing an undefined instruction, goodbye
[INFO] usart1: [host:  5.59s (+0.16ms)|virt:   8.04s (+11.17µs)] *** HARD FAULT *** task=c prio=0
[INFO] usart1: [host:  5.59s (+0.42ms)|virt:   8.04s (+0s)] pc=0x08000460 lr=0x08000175 xpsr=0x81000000
[INFO] usart1: [host:  5.59s (+0.11ms)|virt:   8.04s (+0s)] cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000
[INFO] usart1: [host:  5.59s (+0.17ms)|virt:   8.04s (+0s)] stack: 00000000 20000740 C0000000 40013800 00000000 08000175 08000460 81000000
```

信息量咱们一起读出来。死的是优先级 0 的任务 c，名字与优先级是从 TCB 里问出来的，说明调度器已经开张、死在了任务的 PSP 上。`pc=0x08000460` 是案发地点，`udf` 躺在案发的地址上，您反汇编一眼就查得到。两个状态寄存器跟手册对了号：`cfsr=0x00010000`，低的两个字节是全零的，高的半字是 0x0001，正落在 UsageFault 半字的 bit 16 上，UNDEFINR（未定义指令）置了位。`hfsr=0x40000000` 对的正是 bit 30，FORCED 置了位，UsageFault 没被独立地接住，硬件把它强迫升级成了 HardFault，两处的值与 ARMv7-M 的位段一一咬合。BFAR 的 0x00000000 按上面的 warning 读：BFARVALID 没置位，打印的零不作数。最后一行的八字是硬件压下的现场原样，`stack:` 里的倒数第二个字 0x08000460 与 `pc=` 互为印证，同一份现场的两个抄本对上了。demo 的启动代码把向量表 3 到 6 号槽都指到 `HardFault_Handler`，模拟器从哪个槽进来都归的是同一个人接手，保险的来历就写在 startup 的注释里。

按 `y` 的报告就短得多了，咱们看：

```text
[INFO] usart1: [host: 8.28s (+74.73ms)|virt:   8.17s (+68.99ms)] c: smashing my canary
[INFO] usart1: [host:   8.28s (+0.4ms)|virt:   8.17s (+11.19µs)] [STACK OVERFLOW] task=c eaten=2
```

`smash_canary` 掐死了最深的两只鸟（`view[7]` 与 `view[6]`），任务 c 睡了两毫秒，PendSV 把它切下的那一刻巡逻发现了，`eaten_length` 数出了两个字，报告的数字与作案的数字严丝合缝。您注意时间线：砸完之后芯片没有立刻死，它多活了两毫秒，死因是下一次切换顺路查出来的。这就是挂在切换路径上的巡逻与故障中断的区别了，溢出的性质不是异常，它不触发任何的向量，只有等别人来查才现形的。

host 侧的 `ctest` 我也照实贴，这套测试带着 ASan 加 UBSan 的加固选项，十个目标全部通过了：

```text
100% tests passed out of 10

Total Test time (real) =   0.18 sec
```

格式化器自己的成绩单，咱们看一眼：

```text
All tests passed (15 assertions in 7 test cases)
```

七用例、十五断言的数字，与我本机的 GCC 16.2.1 一起跑出来的就是这些。

## 验收

本篇的可查证事实归拢为四件，咱们一件一件过。

头一件是 host 的考场。您 checkout 到 `feef8fb`，按 `cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug` 的配置再构建，`ctest` 应给出十个目标全部通过的结果，`test_format` 的成绩单应是七用例十五断言。

第二件是 `x` 的完整遗言。交叉构建后跑起了 Renode，把 `x` 注进了 usart1，您就该看到 `task=c prio=0` 的遗言头行了，还有 `cfsr=0x00010000` 的 UNDEFINR、`hfsr=0x40000000` 的 FORCED 与八字现场。`pc` 指的地址上躺着 `udf`，`arm-none-eabi-objdump -d` 反汇编对一眼就知道了。

第三件是 `y` 的溢出报告。按 `y` 之后您该看到 `[STACK OVERFLOW] task=c eaten=2`，被吃两个字对应 demo 掐死的最深两只鸟。您还可以自己改一把 `smash_canary`，多掐了一只，`eaten` 就得跟着长了，机制经得起您加码。

第四件是位段与手册的对质，也是我照实交代没验全的地方。UNDEFINR（CFSR bit 16）与 FORCED（HFSR bit 30）两个位我拿真输出验过了，与 ARMv7-M 手册的位段咬合。CFSR 其余的位（BusFault 字节的 PRECISERR、IMPRECISERR 之类）与 BFARVALID 的行为，本篇没有构造出对应的故障来触发，我只核了手册的口径，没有实验的背书，您要用的时候照手册查，别拿我的表当终审。

## 下一篇

fault 上下文的遗言写完了，死因、现场、溢出都有了着落。同一份主题还剩两件活：中断卸货的路，归的是《中断只递条子》，锚点落在了 `3570f27`。免任务等待的路，归的是《等三百毫秒，不养一个任务》的定时器，锚点落在了 `766c462`。它们与咱们本篇是互相独立的，咱们把三篇合起来，才是本章的完整答卷。
