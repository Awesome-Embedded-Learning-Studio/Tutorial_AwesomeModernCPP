---
title: "一幕一房:demo 注册中心、任务创建三条路与遗言成义务"
description: "咱们拆 0054cf8(feat: seperate the examples and find more cases):342 行的单体 example 被分成一个场景一个子目录的注册中心,zeros_add_demo 一行注册就生成固件加 Renode 目标,任务创建收敛成手铺 slot、arena 切片、建造者链三条路,weak 与 strong 的中断处理器把选择权交给了链接器,遗言报告从可选变成不接线就过不去的义务。八个 demo 我在 /tmp 副本里逐个跑通,host 侧 14 个测试全部通过,testaments 的两种死法都留下了各自的遗言"
chapter: 7
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
  - 构建器模式
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
  - "中断只递条子:环形队列、工作者线程与底半推迟"
related:
  - "数到十就换人:同级时间片与不继承的切片债"
  - "跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门"
current_status:
  title: 八间屋分家完成
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🏠
---

# 一幕一房:demo 注册中心、任务创建三条路与遗言成义务

跟着前面的篇目一路走到这儿,咱们手里的 Renode 串口窗一直是同一副面孔:反转剧场的 L、M、H,收字节的队列和 worker,打字母的 A 和 B,全都住在同一个 342 行的 `example/main.cpp` 里。您想单独验一验优先级继承,就得把底半部和心跳的戏份一起编进固件,因为 342 行的屋子演着所有的戏,改了台词,八竿子打不着的角色也得跟着重新出场。

`0054cf8` 把这间屋拆掉了。它的 subject 写的是 “feat: seperate the examples and find more cases”(seperate 就是仓库里的原样拼写,作者少写了一个 i,咱们照实引),一共交了 37 个文件,新增了 1051 行,删除了 472 行。旧的 `example/main.cpp` 在 diff 里被整个删掉,顶替它的是 example 目录下一幕一房的八个子目录。同一份改动还把三个面上的 API 重新定了形状:任务创建从一条路变成了三条路,日志与硬件分了家,死亡报告从可选变成了不接线就过不去的义务。咱们把这些面按顺序走一遍,八个 demo 您最后都能用一行命令亲自跑起来。

## 342 行的旧屋,咱们看一眼它挤成了什么样

被删掉的旧 `main.cpp` 开头就是一排全局声明,任务的家当全靠它们撑着,咱们看一眼:

```cpp
alignas(8) constinit std::uint32_t stack_a[64] = {};
alignas(8) constinit std::uint32_t stack_b[64] = {};
alignas(8) constinit std::uint32_t stack_c[96] = {}; // canary 32B + ~350B usable: burns in 3-4 levels
alignas(8) constinit std::uint32_t stack_holder[96] = {};
alignas(8) constinit std::uint32_t stack_urgent[64] = {};
alignas(8) constinit std::uint32_t stack_bully[64] = {};
alignas(8) constinit std::uint32_t idle_stack[32] = {};

// zero storage, meaning comes only from the factory chain
constinit ZerOS::sched::TCBStorage tcb_a{};
constinit ZerOS::sched::TCBStorage tcb_b{};
constinit ZerOS::sched::TCBStorage tcb_c{};
```

数组是任务的床,`TCBStorage` 是它的盒子,咱们每加一个任务的时候,就得把床声明了一处、盒子声明了一处,再把链条接到了它们身上。文件尾部的 main 函数里,a、b、c 三个打字母的,L、H、M 三个演反转剧的,把 worker 也加了进来,六份声明排成了一列,`start_scheduler(idle_stack)` 还要您把 idle 的床也亲手递进去。咱们单看每一行,都是没有错的,可是合起来就是教学上的死路:读者打开示例想看的是一个概念,迎面来的却是全部概念,噪声把信号埋在了里头。

## 注册中心:一行注册,固件和仿真目标一并生成

新结构的地基在 `src/board/stm32f103_bluepill/example/CMakeLists.txt`,咱们看它的文件头,主张就写在了注释里:

```cmake
# Example Registrytion — the demo sample plate.
#
# One scenario, one subdirectory. Each subdirectory registers itself with
# the one-liner:  zeros_add_demo(<name>)
```

(Registrytion 又是一个照原样引的拼写,文件头把它拼错了,咱们不替作者改。)主张的落地是一个 CMake 函数。函数的活儿咱们在念代码之前立住:一间 demo 的屋子要跑起来,背后跟着一整套固定的开销,可执行目标要把这一幕的 main.cpp 加上公共的内核源一起编出来,链接要喂进板子的链接脚本,要用 gc-sections 裁掉没用到的段,要留下一份查符号用的 map 文件,编完要报一次体积的读数,本地想复跑的话,还得有一个把 ELF 喂给 Renode 的仿真目标。没有这个函数的话,这一整套就得在每个子目录的 CMakeLists 里原样写上八遍,链接选项哪天改了,八间屋的 CMakeLists 就得挨个跟着改。`zeros_add_demo` 把这一套收进了一个函数,咱们分作三段看它:

```cmake
function(zeros_add_demo name)
    add_executable(zeros-demo-${name} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
                                         ${ZEROS_DEMO_COMMON_SOURCES})
    target_link_libraries(zeros-demo-${name} PRIVATE zeros_common)
    target_link_options(
        zeros-demo-${name}
        PRIVATE -nostdlib
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/../../link.ld
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map,${CMAKE_BINARY_DIR}/demo-${name}.map
    )
```

固件的目标在咱们眼前这一段成形:`zeros-demo-<name>` 把这一幕自己的 main.cpp 和 `${ZEROS_DEMO_COMMON_SOURCES}` 里的公共内核源一起编了出来,挂上了 `zeros_common`。链接的选项是一次配齐的:标准库是不带的,链接脚本用的是上一层目录的 `link.ld`,gc-sections 裁掉了没用到的段,内存的使用情况编完当场打印,map 文件落在了构建目录里备查。

```cmake
    add_custom_command(
        TARGET zeros-demo-${name}
        POST_BUILD
        COMMAND arm-none-eabi-size $<TARGET_FILE:zeros-demo-${name}>
        COMMENT "size report: demo-${name}"
    )
```

编译收尾的时候,`arm-none-eabi-size` 就把体积当场报了出来,咱们验收一节看到的八个 demo 体积表,数就是它打出来的。

```cmake
    add_custom_target(
        run-${name}
        COMMAND ${CMAKE_COMMAND}
        -DRENODE_ROOT=${CMAKE_SOURCE_DIR}
        -DRENODE_ELF=$<TARGET_FILE:zeros-demo-${name}>
        -DRENODE_RESC=src/board/stm32f103_bluepill/sim/renode/bluepill.resc
        -DRENODE_SECONDS=${ZEROS_SIM_SECONDS}
        -P ${CMAKE_CURRENT_LIST_DIR}/../../sim/renode/run-renode.cmake
        DEPENDS zeros-demo-${name}
        USES_TERMINAL
    )
endfunction()
```

最后的一个成员是仿真目标 `run-<name>`:它把固件与仿真脚本的路径递给了 `run-renode.cmake`,由它把 ELF 喂给了 Renode,`DEPENDS` 保证了固件是编好了的,仿真跑满了 `ZEROS_SIM_SECONDS` 秒才退出来,这个秒数的默认值是 5 秒,是配置时能改的缓存变量,您想看久一点就改它。

include 路径的几行被我抽掉了,原文件里它们替每个 demo 补上内核头、板卡头和 CMSIS 的搜索路径,咱们今天不展开它。三段合起来的效果,就是您注册一个名字,固件和仿真目标就一并到位了。每个 demo 子目录里的 `CMakeLists.txt` 因此只剩一行,heartbeat 的全文就是:

```cmake
zeros_add_demo(heartbeat)
```

注册中心的末尾按次序把八个子目录都挂了进来,咱们照着念一遍:

```cmake
add_subdirectory(heartbeat)
add_subdirectory(event_stream)
add_subdirectory(inversion)
add_subdirectory(bottom_half)
add_subdirectory(timers)
add_subdirectory(events_notify)
add_subdirectory(testaments)
add_subdirectory(round_robin)
```

一幕一房说的就是它:每个子目录是一幕的戏,`main.cpp` 从头演到了尾,您看反转就开 inversion 的固件,看遗言就开 testaments 的,而彼此谁也牵连不了谁。

每间屋还共享着一段同款的开场仪式,它住在了 `example/demo_setup.hpp` 里,咱们看:

```cpp
inline void setup(const char* name) {
    board::uart1_init();
    board::register_uart_channel(); // this board talks over UART1
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    arch::cortex_m3::init_time(72'000'000 / 1000); // 1kHz tick
}
```

它的定位是文件头注释里的 “Three lines of boilerplate, one call”:板子起来了,日志的通道登记了,遗言安装了,1kHz 的 tick 也定好了,四件事收进了一个 `demo::setup("名字")` 里。其中遗言安装的那一行是咱们后面“遗言成义务”一节的主角,这里您留意它的存在就够了。

## 任务创建的三条路

demo 分了家,创建任务的老办法就该跟着升级了,因为床、盒子、链条各写一份的写法,正是 342 行能膨胀起来的土壤。`0054cf8` 过了之后,创建任务的路一共就有了三条,它们通向的是同一个建造者链(它出厂在调度器那段岁月里,丢唤醒的篇目里咱们正面拆过它的类型状态设计),差别只落在内存的来路上。

老路走的是手铺存储:您自己声明 `TaskStack` 形状的数组加 `TCBStorage` 盒子,链条走的是 `named().prio().stack(span).entry(fn, arg).spawn_into(box)`。老路咱们并没有丢掉,底半部 demo 里的 worker 至今走的还是它,因为 user_worker 内嵌的栈总得有人接过去。另外两条都是 `0054cf8` 新铺的,咱们一条条细看。

### slot 路:盒子和床合成了一个类型

第二条路解决的是床和盒子分了家的麻烦。床的身上还带着一桩旧病,咱们把它交代在类型的前面:裸的 `uint32_t` 数组只保证 4 字节对齐,而 AAPCS(ARM 的过程调用标准)要求公共接口处的栈指针 8 字节对齐,任务起在一个歪了的栈顶上,死相是莫名其妙的。`include/ZerOS/kernel/sched/stack.hpp` 交出的新类型,办的就是把对齐的事从您要记得,变成编译替您把关,咱们看:

```cpp
template <std::size_t Words>
struct TaskStack {
    static_assert(Words % 2 == 0, "even word count keeps the top 8-aligned");

    alignas(8) std::uint32_t words_[Words]{};

    constexpr operator std::span<std::uint32_t>() noexcept { return words_; }
};

static_assert(alignof(TaskStack<8>) == 8);
```

解法看明白了:字数取了偶数,栈顶就恒是 8 对齐的,谁写了奇数字数,编译当场就拦下了,对齐的事,从用户要记得的,变成了类型替您保证的。

接着咱们看 `task.hpp`,它再让盒子和床合了体:

```cpp
template <std::size_t Words = 64>
struct TaskSlot {
    sched::TCBStorage tcb{};
    TaskStack<Words> stack{};
};
```

您声明一个 `TaskSlot`,任务的全部存储就齐了,链条相应地多出了一个阶段:

```cpp
    // the everyday road: hand in a TaskSlot, box and bed travel together
    template <std::size_t Words>
    TaskBuilder<Slotted> stack(TaskSlot<Words>& slot) requires std::same_as<Stage, Priorized> {
        return TaskBuilder<Slotted>{
            Slotted{stage.name, stage.prio, &slot.tcb, slot.stack}};
    }
```

`Slotted` 阶段里盒子指针和栈视图结伴而行,走到 `entry` 的时候凑齐的是 `Armed` 阶段,它自己的 `spawn_into()` 已经不需要任何参数了,因为该记的东西全都记在了链条上。文件头的示例把日常用法写成了两行,咱们看:

```cpp
//     constinit task::TaskSlot<64> a{};
//     spawn(task::named("a").prio(1).stack(a).entry(fn, arg).spawn_into());
```

我头一回读到 `stack(a)` 既接 `span` 又接 `TaskSlot` 的时候,还担心这样的重载会分不清,回头看才放了心:两个重载都停在 `Priorized` 的同一站上,编译器按您递的是裸 `span` 还是 `TaskSlot` 挑人,咱们看得出它认路比人还准。

### arena 路:您只说字数,内存由工地来切

第三条路连 `TaskSlot` 都不让您声明。`src/arch/arm_cortex_m3/launch.cpp` 在布线期备好了一块 2048 字节的静态内存,声明是带着注释来的:

```cpp
// the wiring-time task block: ~8 demo-sized tasks of (TCB + 64-word stack).
// A named number beats a spell — resize here, the Check reports if dry
constexpr std::size_t kTaskArenaBytes = 2048;

alignas(8) constinit std::byte g_task_block[kTaskArenaBytes]{};
constinit ZerOS::task::Arena g_task_arena{g_task_block, sizeof(g_task_block)};
```

Arena(字面是竞技场的意思,在咱们这儿指一块划出来供分配用的静态内存)的真身住在 `include/ZerOS/kernel/sched/arena.hpp`,分配的动作就是一次指针的推移:

```cpp
    // alignment-rounded slice, or nullptr when the block ran dry
    [[nodiscard]] void* take(std::size_t bytes, std::size_t align) noexcept {
        const auto base = reinterpret_cast<std::uintptr_t>(cursor_);
        const auto miss = (align - (base % align)) % align;
        if (miss + bytes > left_) {
            return nullptr;
        }
        cursor_ += miss;
        left_ -= miss;
        auto* out = cursor_;
        cursor_ += bytes;
        left_ -= bytes;
        return out;
    }
```

文件头把它的世界观写成了一句话,原话是这么一句:“Running dry is a wiring bug, the caller decides how loud to die”,咱们翻过来就是:切不动了说明布线有错,死得多响的决定权,交给了调用方。`launch()` 采纳的是响的报法:

```cpp
sched::TCB* launch(task::Launchable spec) noexcept {
    // even words keep the stack TOP 8-aligned — same rule TaskStack asserts
    ZerOS::debug::Check(spec.words % 2 == 0, "launch: odd stack words");

    auto* box = static_cast<sched::TCBStorage*>(
        g_task_arena.take(sizeof(sched::TCBStorage), alignof(sched::TCBStorage)));
    auto* words =
        static_cast<std::uint32_t*>(g_task_arena.take(spec.words * 4, 8));
    ZerOS::debug::Check(box != nullptr && words != nullptr, "launch: task arena dry");

    return &spawn(task::named(spec.name)
                      .prio(spec.prio)
                      .stack(std::span<std::uint32_t>{words, spec.words})
                      .entry(spec.run.function, spec.run.argument),
                  *box);
}
```

调用方交给 `launch` 的只是一个 `Launchable`,它的字段是名字、优先级、栈的字数、入口函数加参数。TCB 的盒子和栈的内存都由工地现切,链条为此多了两个阶段,`words(n)` 把您从 `Priorized` 送进 `Worded`,`entry` 再把 `Worded` 补成了 `Launchable`,声明就齐了。您留意 `take` 之后没有 free,也没有重试的余地,调度器一启动整块内存就冻结了,所以它服务的只是开工布线的一段岁月。注释里的那句,“frozen once the scheduler starts”,说的正是它的边界。还有一件小事跟着改了:idle 的床被收编进了内核自己身上,`start_scheduler()` 从此就不带参数了,`switch.hpp` 里的声明换成了:

```cpp
[[noreturn]] void start_scheduler(); // idle 栈内核自带
```

三条路里我自己的偏好很明确:日常的 demo 走 arena 路,声明是最短的,2048 字节的预算有 `Check` 看着,真要精确控制某块存储的位置(比如把它按进特定的链接段),slot 的路和手铺的路都还在,路是没有堵死的。heartbeat demo 演的是 arena 路的一个小巧用法,同一个形状克隆出了两个任务:

```cpp
auto shape = ZerOS::task::named("HeartBeat").prio(1).words(64);

// clone the shapes
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &banner));
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &task));
```

咱们再看 `shape`:它是个 `Worded` 阶段的半成品,`entry` 每调一次就分岔出一个完整的 `Launchable`,两个心跳任务用不同的参数各跑各的。

### 漏一步,编译说话

三条路共用的防线是同一个,咱们看它,就是建造者链的类型状态:`TaskBuilder` 的每个成员都挂着 `requires std::same_as<Stage, ...>`,从错误的阶段调错误的步骤,编译器就直接拒绝了。`task.hpp` 的文件头把话说绝了,原话就躺在文件的开头:

```cpp
// The task factory: the chain IS the contract. Skip a step and the rest
// of the chain simply does not exist — no comment can save you then.
//   named("blink").prio(1).stack(stack).entry(fn, arg).spawn_into(box)
```

防线本身咱们见过,丢唤醒的篇目里验过它的价值,`0054cf8` 做的事是把三条路全部接到了同一道防线上,内存的来路不管在哪,链条上的约束一条都没有松过。

## 用链接器写 if:weak 与 strong 的中断处理器

demo 多了,新的问题就冒了出来:不是每个 demo 都想听串口。收字节的 demo 得接住 USART1 的中断,定时器和心跳的 demo 对串口收到的字节毫无兴趣,但是 RXNE 中断要是没人处理,标志就清不掉了,中断会一次又一次地重新进来。`0054cf8` 的答案是一个单独的翻译单元 `src/board/stm32f103_bluepill/uart_drain.cpp`,它的全文只有 13 行,我把注释连同代码一起给您:

```cpp
// Default RX consumer, its OWN translation unit: when an image has nobody
// listening, bytes rain into this drain. A demo that wants them defines
// its own strong handler and the linker drops this weak one.

#include "stm32f1xx.h"
#include "uart.hpp"

extern "C" void USART1_IRQHandler() __attribute__((weak));
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) != 0) {
        (void)USART1->DR; // read DR, drop the byte, clear the flag
    }
}
```

`__attribute__((weak))` 把它声明成了弱符号,而链接器的规则是:同名符号里强定义在,弱的就让位。于是没定义处理器的 demo(心跳、定时器、反转)链接进来的就是排水沟,字节来了,它读一次数据寄存器就把标志清掉了,水也排掉了,大家都没有被吵醒。想听字节的 demo 自己定义一个强处理器,链接器就把 weak 的定义丢掉,一个二进制里就只剩下一份了。选择是发生在链接期的,固件里没有任何运行时的判断分支,我管它叫用链接器写 if,`event_stream` 的 demo 给了它一个现场:

```cpp
// a REAL uart interrupt may join the stream too — this strong handler
// overrides the board's weak drain
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) != 0) {
        g_stream.post(ZerOS::board::uart1_getc());
    }
}
```

同一个符号的身上,强定义顶掉了排水沟,收进来的字节就进了队列。链接器认强不认弱的行为,我是拿仿真当面对质过的:固件跑起来之后,我从 Renode 的监视器往接收侧喂了一个字符 q,那会儿 500 毫秒一拍的定时器正在投喂 a 到 f,串口日志里 q 就在 b 和 c 的中间插了进来(输出是抽取的,这一块我只取了虚拟时间列,完整的日志在验收一节交代):

```text
[INFO] usart1: [host:  1.09s|virt:   1s] got 'b'
[INFO] usart1: [host:  1.34s|virt: 1.25s] got 'q'
[INFO] usart1: [host:  1.58s|virt:  1.5s] got 'c'
```

q 是我喂进去的,它出现在了队列里,这说明强处理器真的在岗位上,排水沟也真的让了位。

::: warning
弱符号的让位是按符号名全图生效的,一个固件里的同名处理器只能有一份强定义。您要是把两个 demo 的 main.cpp 合并编译,两边都写了强处理器,链接器报的会是重复定义而不是让位,它这是替您拦下了一次含糊,咱们别绕过它。
:::

## 日志是便利,遗言是义务

demo 分成了房,咱们也趁机理清了打印的路径。`include/ZerOS/log/print.hpp` 是新的格式化入口,头注释把分工划开了:

```cpp
// format → the registered global channel, in one breath. No hardware
// anywhere near this file: where the text lands is the board's business
// (register a channel), not the formatter's.
//
// An unregistered channel DROPS the line — logging is a convenience, not
// an obligation. (Testaments are obligations; those have their own teeth.)
```

咱们看 `log::print` 的分工:它把内容格式化进一个 128 字节的栈上缓冲,再交到了全局通道的手里,而格式化器从头到尾不认识任何硬件,文本落在哪儿归板卡决定(板卡在 `demo::setup` 里把 UART1 登记成了通道)。128 可不是拍脑袋来的数字,它的注释里带着案底:

```cpp
// one line at a time; long lines truncate, they never overrun.
// 128 keeps a 64-word task stack printable (160 overflowed it — the
// canary testified twice); heavy printers should ask for 96 words
```

案底咱们念一遍:160 字节的行缓冲曾经溢出过 64 词的任务栈,金丝雀出庭作证了两次之后,缓冲降到了 128。还有一处跟着收紧的是 worker 的默认栈:`user_worker.hpp` 的注释写着,`log::print` 一开腔就往 worker 的栈上放 160 字节的行缓冲,64 词量下来是太薄的,这一回它就被金丝雀抓到了,所以 `StackWords` 的默认值提到了 128。slot 路和手铺路的默认栈仍是 64 词,它们的任务要是只打短行,64 词就还是够用的。一行超长会被截断而不会越界,打印是便利的,而便利就该有上限。

遗言是另一头:咱们说的 testament,指的就是芯片临死前留下的报告。`include/ZerOS/arch/arm_cortex_m3/testaments.hpp` 是本 commit 交的新文件,它带来的是一套默认的死亡报告,头注释是写得清楚的:

```cpp
// The opt-in default testaments: print-and-halt for both death styles.
// Explicitly installed (nobody silently gets defaults), printing through
// the registered log channel — this file never touches a UART itself.
```

默认的遗言得显式安装,而谁也不会被悄悄塞一份,安装的动作就是 `demo::setup` 里那行 `install_default_testaments()`,它给两处钩子各接了一个打印然后停机的实现。您要是问不装会怎样,答案就是过不去的,`fault.cpp` 里的断言盯着呢:

```cpp
    // dying with no testament is not allowed either — wire one, or install
    // the defaults; silence on a hard fault is a bug, not a mode
    ZerOS::debug::Check(ZerOS::arch::cortex_m3::fault_sink != nullptr, "fault sink not wired");
    ZerOS::arch::cortex_m3::fault_sink(r);
```

硬错误发生了而没人听,咱们说它不算一种工作的模式,而是 bug,`Check` 当场把话挑明了。`switch.cpp` 对栈溢出报告器动了同样的刀,原来 `overflow_reporter` 为空就静默跳过的判断,改成了要过一遍断言:

```diff
-        if (TaskGuardHelper::fast_check_stack(*prev) &&
-            TaskGuardHelper::overflow_reporter != nullptr) {
+        if (TaskGuardHelper::fast_check_stack(*prev)) {
+            // an eaten canary with nobody listening must NOT pass in silence
+            ZerOS::debug::Check(TaskGuardHelper::overflow_reporter != nullptr,
+                                "overflow reporter not wired");
             TaskGuardHelper::overflow_reporter(prev, TaskGuardHelper::eaten_length(*prev));
         }
```

金丝雀被吃了而没人听,同样不许无声地通过。日志是便利的事、遗言是义务的事,咱们在《芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志》里铺过它的底,不过当时报告器还是可选的,`0054cf8` 把可选收成了义务:您可以换掉默认的实现,但是不许不接。

## 八个 demo,一场一场过

注册中心挂了八间屋,咱们把每间的戏份报一下,谱系也顺路对齐:它们大多是老戏搬了新台,前面各篇亲手验过的场景,现在各有了自己的屋子。

heartbeat 是新的司仪,两个心跳任务 500 毫秒一拍的节奏,演的是 arena 路的形状克隆。event_stream 演的是队列加定时器的投喂,再搭上咱们刚看过的那段强处理器。inversion 演的是反转剧场:L 持锁忙等了 300 毫秒,M 占着 CPU 的空当,H 迟到了 100 毫秒来求锁,它的戏本来自《看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交》。bottom_half 演的是底半推迟,定时器站的是中断的位置,每秒递了一份重活,worker 接住之后在任务的上下文里敢睡 200 毫秒,戏本来自咱们拆过的《中断只递条子:环形队列、工作者线程与底半推迟》。timers 演的是软件定时器,一个 300 毫秒的一次性,一个 250 毫秒的周期,戏本来自咱们写过的《等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货》的篇目。events_notify 演的是事件组的 AND 掩码加直接通知,两把钥匙分别在 400 和 900 毫秒按了下去,600 毫秒时邮差往任务信箱里直投了一个 42,戏本来自咱们写过的《叫醒所有人,各自重查真相:事件组、直接通知与满环背压》的篇目。testaments 演的是遗言本身,平跑是安静的心跳,您从监视器注入字符就能选它的死法。round_robin 演的是同级时间片,两个永不睡觉的自旋任务同优先级死磕了三秒,交错的输出本身就是它的特性,戏本来自咱们拆过的《数到十就换人:同级时间片与不继承的切片债》。

您把仓库 checkout 到 `0054cf8` 之后,照下面的几条命令走,固件和仿真就全有了:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build
cmake --build build --target run-heartbeat
cmake --build build --target run-event_stream
cmake --build build --target run-inversion
cmake --build build --target run-bottom_half
cmake --build build --target run-timers
cmake --build build --target run-events_notify
cmake --build build --target run-testaments
cmake --build build --target run-round_robin
```

每个 `run-*` 目标把固件编好了,就把 Renode 拉了起来,跑上 5 秒的时间,串口的日志落在仓库根的 `run.log` 里。有一处环境差异我得交代:我本机的 Renode 是 1.17.0,加载 `bluepill.repl` 时它认不出 `I2C.STM32F4_I2C` 的类型,我把 i2c1、i2c2 两个节点连同中断接线整块注释掉了,仿真就起来了。这跟《在栈顶伪造出生证明:PendSV、SVC 与第一次切换》验收时用过的办法是同一副药,八个 demo 都不走 i2c 的路径,行为是不受影响的。

下面的输出都是我从各自的 `run.log` 里抽的代表作,时间取的是 Renode 的虚拟时间列,每个 demo 的完整日志都比贴出的多。

heartbeat 的两拍心跳:两个任务同参数而内容不同,各自 500 毫秒一拍地走,咱们数着它们的拍子:

```text
[INFO] usart1: [virt: 0s] ZerOS demo: heartbeat
[INFO] usart1: [virt: 46.9µs] Hello! ZerOS
[INFO] usart1: [virt: 52.23µs] A heartbeat example!
[INFO] usart1: [virt: 0.5s] Hello! ZerOS
[INFO] usart1: [virt: 0.5s] A heartbeat example!
[INFO] usart1: [virt: 1s] Hello! ZerOS
[INFO] usart1: [virt: 1s] A heartbeat example!
```

inversion 的安静窗口。咱们看:M 本来每 100 毫秒喊一嗓子,虚拟时间走到了 2.0 秒,L 拿了锁开始忙等,2.1 秒 H 来求了锁,此后的 M 整整 300 毫秒没有出声,2.3 秒 L 放了锁,H 立刻拿到了,M 的声音在同一毫秒里也回来了:

```text
[INFO] usart1: [virt:      2s] M: running
[INFO] usart1: [virt:      2s] L: locked, busy 300ms
[INFO] usart1: [virt:    2.1s] H: wants the lock
[INFO] usart1: [virt:    2.3s] L: unlocked
[INFO] usart1: [virt:    2.3s] H: LOCKED (inheritance paid off)
[INFO] usart1: [virt:    2.3s] M: running
```

M 闭嘴的 300 毫秒就是继承在付的代价:L 抬着 H 的级别把忙等跑完,M 也就挤不进来了。仓库自己写下的 “inheritance paid off”,它是仓库自己的台词,后面 CI 的冒烟检查 grep 的就是它,咱们下一篇见分晓。

bottom_half 的重活,job 进来睡了 200 毫秒再报完成,睡眠发生在 worker 的任务上下文里,咱们数一数时间,它睡满了:

```text
[INFO] usart1: [virt: 1s] worker: job #1 in, napping 200ms
[INFO] usart1: [virt: 1.2s] worker: job #1 done
[INFO] usart1: [virt: 2s] worker: job #2 in, napping 200ms
[INFO] usart1: [virt: 2.2s] worker: job #2 done
```

timers 的两个定时器,一次性的在 300 毫秒点了火就没了影,周期的 250 毫秒一拍接着拍,咱们听着它的节拍:

```text
[INFO] usart1: [virt: 0.25s] beat #1
[INFO] usart1: [virt:  0.3s] one-shot: fired, going cold
[INFO] usart1: [virt:  0.5s] beat #2
[INFO] usart1: [virt: 0.75s] beat #3
```

events_notify 的两件事,600 毫秒的信到得更早,900 毫秒钥匙凑齐了,AND 掩码放了行,咱们对一对时间:

```text
[INFO] usart1: [virt: 0.6s] nt: letter says 42
[INFO] usart1: [virt: 0.9s] evt: both keys in (bits=0x00000003)
```

round_robin 的两个自旋任务,rr1 在 47 微秒出的场,rr2 在 10 毫秒跟了上来,咱们看它们此后每 500 毫秒各报一次到,间隔是恒定的 10 毫秒,谁也没有饿着谁的:

```text
[INFO] usart1: [virt:   47.15µs] rr1 alive
[INFO] usart1: [virt:      10ms] rr2 alive
[INFO] usart1: [virt:      0.5s] rr1 alive
[INFO] usart1: [virt:     0.51s] rr2 alive
[INFO] usart1: [virt:        1s] rr1 alive
[INFO] usart1: [virt:     1.01s] rr2 alive
```

testaments 平跑是一行一行的 alive,真戏在注入里等着咱们。我从 Renode 的监视器往串口喂了 `0x79`,terminal 任务砸了自己的栈金丝雀,下一次 PendSV 的巡逻就把报告递了出来:

```text
[INFO] usart1: [virt: 1.25s] term: smashing my canary
[INFO] usart1: [virt: 1.25s] [STACK OVERFLOW] task=term eaten=2
```

喂 `0x78` 走的是另一条路,咱们看:`udf #0` 烧掉了一条未定义指令,CPU 级的硬错误当场就触发了,完整的报告连同现场的八个字一起留了下来:

```text
[INFO] usart1: [virt: 1.23s] term: executing an undefined instruction, goodbye
[INFO] usart1: [virt: 1.23s] *** HARD FAULT *** task=term prio=0
[INFO] usart1: [virt: 1.23s] pc=0x080004EA lr=0x08000D8B xpsr=0x01000000
[INFO] usart1: [virt: 1.23s] cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000
[INFO] usart1: [virt: 1.23s] stack: 00000001 20000383 C0000000 08000D79 00000000 08000D8B 080004EA 01000000
```

cfsr 的 0x00010000 是未定义指令位,hfsr 的 0x40000000 是 FORCED 位,咱们把报告跟 ARMv7-M 的寄存器定义对了对号,是能对得上的。同一个 demo 演了两种死法,遗言是两场都留全了的,`demo::setup` 里安装的那行在这两场戏里都在岗位上。

## 验收

咱们把可查证的事实归拢一下,每件我都交代验法。

第一件是八个 demo 的构建与运行。您 checkout 到 `0054cf8`,照上面的 `cmake -B` 配好交叉构建,八个 `zeros-demo-*` 目标就该全部编出来了,各自的 `run-*` 一行就能在 Renode 里跑。固件的体积是真实的,交叉的工具链是咱们拆调度器那篇用过的同一套,版本是 arm-none-eabi-g++ 的 16.2.0,我把 `arm-none-eabi-size` 的报告抄成了一张表(text、data、bss 分别是代码段、已初始化的数据、零初始化的数据):

| demo | text | data | bss |
| --- | --- | --- | --- |
| heartbeat | 3448 | 364 | 2096 |
| event_stream | 4168 | 420 | 2096 |
| inversion | 4248 | 348 | 2104 |
| bottom_half | 4040 | 1028 | 48 |
| timers | 3032 | 396 | 48 |
| events_notify | 4308 | 436 | 2104 |
| testaments | 4212 | 380 | 2096 |
| round_robin | 3188 | 348 | 2096 |

bss 的差异有它的道理,咱们摆两条线索:走 arena 路的 demo 把任务存储切在了 bss 里的那块 2048 字节工地上,bottom_half 和 timers 的任务要么少些,要么存储长在了 data 的一侧。这个差异我没有逐家验到底,两条线索摆到这儿就收了,我不再往下断言了。

第二件是 host 侧的验收。我用 `git archive 0054cf8` 把仓库导到了 /tmp 的副本里,真实的 STM32CubeF1 拷进了副本的 third_party(它在仓库里只是一段引用的文字,归档是空着的,咱们拷贝的时候补上了),再按 README 的路子配了测试构建:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机的 GCC 是 16.2.1,ctest 的成绩单是真实的:14 个测试全部通过了,总耗时是实打实的 0.21 秒。调度测试自己的清单是 70 条断言、14 个用例,其中一个是本 commit 新交的,Arena 的对齐、切干拒收、不回绕,全在它的手里:

```text
startup arena: aligned slices, dry block refuses, no wraparound
```

用例里特意验了对齐垫在起点而不计进尺寸:咱们看它的算术,`take(1, 8)` 只挪了 1 字节,下一次 8 字节对齐的请求要付 7 字节的垫,剩 8 字节的工地就真的切不动了。布线期的分配器看着朴素,边界的处理一条都没有含糊。

第三件是注入的实验,前面遗言和强处理器两节贴的 q、0x78、0x79 三场,都是我亲手从 Renode 的监视器喂出来的,`WriteChar` 命令往接收侧送的是字节,串口日志里的响应都在 5 秒的仿真窗内。

第四件是本篇没验的东西,我照实说:八间屋里 events_notify 和 round_robin 的机制面(事件组的等待链、时间片的切换节拍),我验的只是串口行为的表象,内核侧的推演归它们各自的篇目,咱们不在这里抢戏。串口的输出我贴的全是抽取,这件事我还有交代:每场日志的原始行数都比贴的多,您复跑的时候,以自己 `run.log` 里的完整记录为准。

## 下一篇

屋子分好了,戏本也各就各位了,接下来自然的事,是让机器替咱们挨个验收,它的名字就是 CI。下一篇要拆的是 d48f5a3,它的标题是您在 related 里见过的《跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门》,四道检查把本站的八个 demo 变成了其中的一道,每个 demo 的台词在 CI 里都被 grep 守着。咱们那儿见。
