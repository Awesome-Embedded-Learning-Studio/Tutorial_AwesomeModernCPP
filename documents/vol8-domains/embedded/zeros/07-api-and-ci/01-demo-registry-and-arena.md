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
---

# 一幕一房:demo 注册中心、任务创建三条路与遗言成义务

跟着前面的篇目一路走到这儿,咱们手里的 Renode 串口窗一直是同一副面孔:反转剧场的 L、M、H,收字节造工作者的队列,打字母的 A 和 B,全住在同一个 `example/main.cpp` 里。您想单独验一验优先级继承,就得连着底半部和心跳一起编进去,342 行的屋子演着所有的戏,改一句台词,八竿子打不着的角色也得跟着重新出场。

`0054cf8` 把这间屋拆了。它的 subject 写的是 “feat: seperate the examples and find more cases”(seperate 就是仓库里的原样拼写,作者少写了一个 i,咱们照实引),一共交了 37 个文件,新增 1051 行,删除 472 行。342 行的 `example/main.cpp` 在 diff 里被整个删掉,顶替它的是 example 目录下一幕一房的八个子目录。同一份改动还把三个面上的 API 重新定下了形状:任务创建从一条路变成三条路,日志与硬件分了家,死亡报告从可选变成了不接线就过不去的义务。咱们把这三个面按顺序走一遍,八个 demo 您最后都能用一行命令亲自跑起来。

## 342 行的旧屋,咱们看一眼它挤成什么样

被删掉的旧 `main.cpp` 开头就是一排全局声明,任务的家当全靠它们:

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

数组是床,`TCBStorage` 是盒子,每加一个任务,您得声明一处床、声明一处盒子,再把链条接到它们的身上。文件尾部的主函数里,a、b、c 三个打字母的,L、H、M 三个演反转剧的,加上 worker,六个任务七份声明排成一面墙,`start_scheduler(idle_stack)` 还要您把 idle 的床也亲手递进去。单看每一行都没有错,合起来就是教学上的死路:读者打开示例想看一个概念,迎面是全部概念,噪声把信号埋在了里面。

## 注册中心:一行注册,固件和仿真目标一并生成

新结构的地基在 `src/board/stm32f103_bluepill/example/CMakeLists.txt`,它的文件头把主张写在了注释里:

```cmake
# Example Registrytion — the demo sample plate.
#
# One scenario, one subdirectory. Each subdirectory registers itself with
# the one-liner:  zeros_add_demo(<name>)
```

(Registrytion 又是一个照原样引的拼写,文件头把它拼错了,咱们不替作者改。)主张的落地是一个 CMake 函数:

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
    add_custom_command(
        TARGET zeros-demo-${name}
        POST_BUILD
        COMMAND arm-none-eabi-size $<TARGET_FILE:zeros-demo-${name}>
        COMMENT "size report: demo-${name}"
    )
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

我把中间 include 路径的几行抽掉了,原文件里它们给每个 demo 补上内核头、板卡头和 CMSIS 的搜索路径,咱们今天不展开。留下的部分才是注册中心的价值:您注册一个名字,函数就替您生成 `zeros-demo-<name>` 的固件目标(链接脚本、裁剪无用的段、体积报告、map 文件全都配好),再生成一个 `run-<name>` 的仿真目标,它调 `run-renode.cmake` 把 ELF 喂给 Renode,跑 `ZEROS_SIM_SECONDS` 秒(默认 5 秒,是个缓存变量,想看久一点就在配置时改掉)再退出。每个 demo 子目录里的 `CMakeLists.txt` 因此只剩一行,heartbeat 的全文就是:

```cmake
zeros_add_demo(heartbeat)
```

注册中心的末尾按次序把八个子目录都挂了进来:

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

一幕一房就是这个意思:每个子目录一个 `main.cpp`,一个场景从头演到尾,您看反转就开 inversion 的固件,看遗言就开 testaments 的,互不牵连。

每间屋还共享同一段开场仪式,住在 `example/demo_setup.hpp` 里:

```cpp
inline void setup(const char* name) {
    board::uart1_init();
    board::register_uart_channel(); // this board talks over UART1
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    arch::cortex_m3::init_time(72'000'000 / 1000); // 1kHz tick
}
```

文件头注释给它的定位是 “Three lines of boilerplate, one call”:板子起来、日志通道登记、遗言安装、1kHz 的 tick,四件事收进一个 `demo::setup("名字")`。其中遗言安装那一行是咱们后面遗言成义务一节的主角,这里您记住它的存在就够了。

## 任务创建的三条路

demo 拆开了,创建任务的老办法就该跟着升级,因为旧屋里那种“床一份、盒子一份、链条一份”的写法正是 342 行能膨胀起来的土壤。`0054cf8` 之后,创建任务的路一共三条,通向的是同一个建造者链(它出厂在调度器的岁月里,咱们在《睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链》里正面拆过它的类型状态设计),差别只在内存从哪儿来。

头一条是老路,手铺存储。您自己声明 `TaskStack` 形状的数组加 `TCBStorage` 盒子,链条走 `named().prio().stack(span).entry(fn, arg).spawn_into(box)`。这条路没有消失,底半部 demo 里的 worker 至今走它,user_worker 内嵌的栈总得有人接过去。第二条和第三条都是 `0054cf8` 新铺的,咱们细看。

### slot 路:盒子和床长成一个类型

第二条路解决的是“床和盒子分家”的麻烦,`include/ZerOS/kernel/sched/stack.hpp` 交出了新类型:

```cpp
template <std::size_t Words>
struct TaskStack {
    static_assert(Words % 2 == 0, "even word count keeps the top 8-aligned");

    alignas(8) std::uint32_t words_[Words]{};

    constexpr operator std::span<std::uint32_t>() noexcept { return words_; }
};

static_assert(alignof(TaskStack<8>) == 8);
```

文件头的注释把动机交代得清楚:裸的 `uint32_t` 数组只保证 4 字节对齐,AAPCS(ARM 的过程调用标准)要求公共接口处的栈指针 8 字节对齐,任务起在一个歪了的栈顶上,死相是莫名其妙的。字数取偶数,栈顶就恒是 8 对齐的,对齐这件事从“用户要记得”变成了“类型替您保证”,谁写了奇数字数,编译当场就拦。`task.hpp` 里再让盒子和床合体:

```cpp
template <std::size_t Words = 64>
struct TaskSlot {
    sched::TCBStorage tcb{};
    TaskStack<Words> stack{};
};
```

声明一个 `TaskSlot`,任务的全部存储就齐了,链条相应地多了一个阶段:

```cpp
    // the everyday road: hand in a TaskSlot, box and bed travel together
    template <std::size_t Words>
    TaskBuilder<Slotted> stack(TaskSlot<Words>& slot) requires std::same_as<Stage, Priorized> {
        return TaskBuilder<Slotted>{
            Slotted{stage.name, stage.prio, &slot.tcb, slot.stack}};
    }
```

`Slotted` 阶段里盒子指针和栈视图结伴而行,走到 `entry` 时凑齐的是 `Armed` 阶段,它自己的 `spawn_into()` 已经不需要任何参数,因为该记的东西全都记在链条上了。文件头的示例把日常用法写成了两行:

```cpp
//     constinit task::TaskSlot<64> a{};
//     spawn(task::named("a").prio(1).stack(a).entry(fn, arg).spawn_into());
```

我第一次读到 `stack(a)` 既接 `span` 又接 `TaskSlot` 的时候,还担心重载会分不清,回头看阶段类型才明白,两种形态落在两个不同的阶段上,编译器认路比人还准。

### arena 路:您只说字数,内存由工地切

第三条路连 `TaskSlot` 都不让您声明。`src/arch/arm_cortex_m3/launch.cpp` 在布线期备了一块 2048 字节的静态内存,声明带着注释:

```cpp
// the wiring-time task block: ~8 demo-sized tasks of (TCB + 64-word stack).
// A named number beats a spell — resize here, the Check reports if dry
constexpr std::size_t kTaskArenaBytes = 2048;

alignas(8) constinit std::byte g_task_block[kTaskArenaBytes]{};
constinit ZerOS::task::Arena g_task_arena{g_task_block, sizeof(g_task_block)};
```

Arena(字面意思是竞技场,在这儿指一块划出来供分配用的静态内存)的真身在 `include/ZerOS/kernel/sched/arena.hpp`,分配动作就是一次指针推移:

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

文件头把规矩写成了一句话(它写的是 “Running dry is a wiring bug, the caller decides how loud to die”),翻过来就是:切不动了说明布线有错,死得多响由调用方决定。`launch()` 采纳了响的报法:

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

调用方交给 `launch` 的只是一个 `Launchable`:名字、优先级、栈的字数、入口函数加参数,TCB 的盒子和栈的内存都从工地现切。链条为此多了两个阶段,`words(n)` 从 `Priorized` 走进 `Worded`,`entry` 再把 `Worded` 补成 `Launchable`,声明就齐了。您注意 `take` 之后没有 free、没有重试,调度器一启动整块内存就冻结了,所以它只服务开工布线的那一小段岁月,注释里 “frozen once the scheduler starts” 说的就是这条边界。顺带还有一件小事跟着改了:idle 的床收编进了内核自己身上,`start_scheduler()` 从此不带参数,`switch.hpp` 里的声明换成了:

```cpp
[[noreturn]] void start_scheduler(); // idle 栈内核自带
```

三条路里我自己的偏好很明确:日常 demo 走 arena 路,声明最短,2048 字节的预算由 `Check` 看着;要精确控制某块存储的位置(比如把它按进特定的链接段),slot 路和手铺路都在,路没有堵死。heartbeat demo 演的是 arena 路的一个小巧用法,同一个形状克隆出两个任务:

```cpp
auto shape = ZerOS::task::named("HeartBeat").prio(1).words(64);

// clone the shapes
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &banner));
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &task));
```

`shape` 是个 `Worded` 阶段的半成品,`entry` 每调一次就分岔出一个完整的 `Launchable`,两个心跳任务用不同的参数各跑各的。

### 漏一步,编译说话

三条路共用同一道防线,就是建造者链的类型状态:`TaskBuilder` 的每个成员都挂着 `requires std::same_as<Stage, ...>`,从错误的阶段调错误的步骤,编译器直接拒绝,`task.hpp` 文件头把话说绝了:“Skip a step and the rest of the chain simply does not exist”。这不是新防线,咱们在丢唤醒那一篇见过它的价值,`0054cf8` 做的事是把三条路全部接到同一道防线上,不管内存从哪来,链条上的约束一条都没松。

## 用链接器写 if:weak 与 strong 的中断处理器

demo 一多,新的问题冒了出来:不是每个 demo 都想听串口。收字节的 demo 得接住 USART1 的中断,定时器和心跳的 demo 对串口收到的字节毫无兴趣,可是 RXNE 中断要是没人处理,标志清不掉,中断就会一次次地重新进来。`0054cf8` 的答案是一个单独的翻译单元 `src/board/stm32f103_bluepill/uart_drain.cpp`,全文就 13 行,我把注释连同代码一起给您:

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

`__attribute__((weak))` 把它声明成了弱符号,链接器的规则是:同名符号里强定义在,弱的就让位。所以没定义处理器的 demo(心跳、定时器、反转)链接进来的是这条排水沟,字节来了,读一次数据寄存器把标志清掉,水排掉了,没人被吵醒;想听字节的 demo 自己定义一个强处理器,链接器就把 weak 的那份丢掉,一个二进制里只剩一份。选择发生在链接期,固件里没有任何运行时的判断分支,我管它叫用链接器写 if,`event_stream` 的 demo 给了它一个现场:

```cpp
// a REAL uart interrupt may join the stream too — this strong handler
// overrides the board's weak drain
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) != 0) {
        g_stream.post(ZerOS::board::uart1_getc());
    }
}
```

同一个符号,强定义顶掉了排水沟,收进来的字节进了队列。链接器认强不认弱这件事,我拿仿真当面对质过:跑起来之后我从 Renode 的监视器往接收侧喂了一个字符 q,500 毫秒一拍的定时器正在投喂 a 到 f,串口日志里 q 在 b 和 c 的中间插了进来(输出是抽取的,完整日志在验收一节交代):

```text
[INFO] usart1: [host:  1.09s|virt:   1s] got 'b'
[INFO] usart1: [host:  1.34s|virt: 1.25s] got 'q'
[INFO] usart1: [host:  1.58s|virt:  1.5s] got 'c'
```

q 是我喂的,能出现在队列里,说明强处理器真的在岗位上,排水沟真的让了位。

::: warning
弱符号的让位是按符号名全图生效的,一个固件里同名处理器只能有一份强定义。您把两个 demo 的 main.cpp 合并编译的时候,两边都写了强处理器,链接器报的会是重复定义而不是让位,这算它替您拦下了一次含糊,别绕过它。
:::

## 日志是便利,遗言是义务

demo 拆成一幕一房之后,打印的路径也趁机理清了。`include/ZerOS/log/print.hpp` 是新的格式化入口,头注释把分工划开了:

```cpp
// format → the registered global channel, in one breath. No hardware
// anywhere near this file: where the text lands is the board's business
// (register a channel), not the formatter's.
//
// An unregistered channel DROPS the line — logging is a convenience, not
// an obligation. (Testaments are obligations; those have their own teeth.)
```

`log::print` 把内容格式化进一个 128 字节的栈上缓冲,再交给全局通道,格式化器从头到尾不认识任何硬件,文本落在哪里归板卡决定(它在 `demo::setup` 里把 UART1 登记成了通道)。128 这个数字不是拍脑袋来的,注释里带着案底:

```cpp
// one line at a time; long lines truncate, they never overrun.
// 128 keeps a 64-word task stack printable (160 overflowed it — the
// canary testified twice); heavy printers should ask for 96 words
```

160 字节的行缓冲溢出过 64 词的任务栈,金丝雀两次出庭作证之后,缓冲降到了 128,顺带把默认任务栈从 64 词提到了 128 词。一行超长会被截断而不会越界,打印是便利,便利可以有上限。

而遗言(testament,芯片临死前留下的报告)是另一头,`include/ZerOS/arch/arm_cortex_m3/testaments.hpp` 是本 commit 的新文件,它交的是一套默认的死亡报告,头注释写得清楚:

```cpp
// The opt-in default testaments: print-and-halt for both death styles.
// Explicitly installed (nobody silently gets defaults), printing through
// the registered log channel — this file never touches a UART itself.
```

默认遗言得显式安装,谁也不会被悄悄塞一份,安装动作就是 `demo::setup` 里那一行 `install_default_testaments()`,它给两处钩子各接了一个打印然后停机的实现。您要是问不装会怎样,答案是过不去,`fault.cpp` 里的断言盯着:

```cpp
    // dying with no testament is not allowed either — wire one, or install
    // the defaults; silence on a hard fault is a bug, not a mode
    ZerOS::debug::Check(ZerOS::arch::cortex_m3::fault_sink != nullptr, "fault sink not wired");
    ZerOS::arch::cortex_m3::fault_sink(r);
```

硬错误发生了而没人听,这不算一种工作模式,算 bug,`Check` 当场把话挑明。`switch.cpp` 对栈溢出报告器动了同一刀,原来 `overflow_reporter` 为空就静默跳过的判断,改成了先过一遍断言:

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

金丝雀被吃了而没人听,同样不许无声通过。日志是便利、遗言是义务,这对分工在《芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志》里铺过底,不过当时报告器还是可选的,`0054cf8` 把可选收成了义务:您可以换掉默认实现,但不许不接。

## 八个 demo,一场一场过

注册中心挂了八间屋,咱们把每间的戏份报一下,顺带把谱系对齐:它们大多不是新故事,而是把前面各篇亲手验过的场景搬进各自的屋子。

heartbeat 是新司仪,两个心跳任务 500 毫秒一拍,演的是 arena 路的形状克隆。event_stream 演的是队列加定时器投喂,再搭上刚才那段强处理器。inversion 演的是反转剧场,L 持锁忙等 300 毫秒,M 霸着 CPU,H 迟到 100 毫秒来求锁,戏本来自《看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交》。bottom_half 演的是底半推迟,定时器站在中断的位置上每秒递一份重活,worker 接住之后在任务上下文里敢睡 200 毫秒,戏本来自《中断只递条子:环形队列、工作者线程与底半推迟》。timers 演的是软件定时器,一次性 300 毫秒、周期 250 毫秒各一个,戏本来自《等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货》。events_notify 演的是事件组 AND 掩码加直接通知,两把钥匙分别在 400 和 900 毫秒按下,600 毫秒时邮差往任务信箱里直投一个 42,戏本来自《叫醒所有人,各自重查真相:事件组、直接通知与满环背压》。testaments 演的是遗言本身,平跑是安静的心跳,您从监视器注入字符就能选死法。round_robin 演的是同级时间片,两个永不睡觉的自旋任务同优先级死磕三秒,交错输出本身就是特性,戏本来自《数到十就换人:同级时间片与不继承的切片债》。

checkout 到 `0054cf8` 之后,您照下面几条命令走,固件和仿真就全有了:

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

每个 `run-*` 目标编好固件就把 Renode 拉起来跑 5 秒,串口日志落在仓库根的 `run.log` 里。有一处环境差异我得交代:我本机的 Renode 是 1.17.0,加载 `bluepill.repl` 时认不出 `I2C.STM32F4_I2C` 类型,我把 i2c1、i2c2 两个节点连同中断接线整块注释掉了,仿真就起来了,这跟《在栈顶伪造出生证明:PendSV、SVC 与第一次切换》验收时用过的办法是同一副药,八个 demo 都不走 i2c 的路径,行为不受影响。

下面是代表性输出,都是我从各自的 `run.log` 里抽的,时间取 Renode 的虚拟时间列,每个 demo 的完整日志行数都比贴出的多。

heartbeat 的两拍心跳,两个任务同参数异内容,500 毫秒一拍整整齐齐:

```text
[INFO] usart1: [virt: 0s] ZerOS demo: heartbeat
[INFO] usart1: [virt: 46.9µs] Hello! ZerOS
[INFO] usart1: [virt: 52.23µs] A heartbeat example!
[INFO] usart1: [virt: 0.5s] Hello! ZerOS
[INFO] usart1: [virt: 0.5s] A heartbeat example!
[INFO] usart1: [virt: 1s] Hello! ZerOS
[INFO] usart1: [virt: 1s] A heartbeat example!
```

inversion 的安静窗口。M 本来每 100 毫秒喊一嗓子,虚拟时间 2.0 秒 L 拿了锁开始忙等,2.1 秒 H 来求锁,此后 M 整整 300 毫秒没有出声,2.3 秒 L 放锁,H 立刻拿到,M 的声音在同一毫秒回来了:

```text
[INFO] usart1: [virt:      2s] M: running
[INFO] usart1: [virt:      2s] L: locked, busy 300ms
[INFO] usart1: [virt:    2.1s] H: wants the lock
[INFO] usart1: [virt:    2.3s] L: unlocked
[INFO] usart1: [virt:    2.3s] H: LOCKED (inheritance paid off)
[INFO] usart1: [virt:    2.3s] M: running
```

M 闭嘴的 300 毫秒就是继承在付的代价,L 抬着 H 的级别把忙等跑完,M 才挤不进来。这行 “inheritance paid off” 是仓库自己写的台词,后面 CI 的冒烟检查 grep 的就是它,咱们下一篇见分晓。

bottom_half 的重活,job 进来睡 200 毫秒再报完成,睡眠发生在 worker 的任务上下文里:

```text
[INFO] usart1: [virt: 1s] worker: job #1 in, napping 200ms
[INFO] usart1: [virt: 1.2s] worker: job #1 done
[INFO] usart1: [virt: 2s] worker: job #2 in, napping 200ms
[INFO] usart1: [virt: 2.2s] worker: job #2 done
```

timers 的两个定时器,一次性那个 300 毫秒点火之后就没影了,周期那个 250 毫秒一拍接着拍:

```text
[INFO] usart1: [virt: 0.25s] beat #1
[INFO] usart1: [virt:  0.3s] one-shot: fired, going cold
[INFO] usart1: [virt:  0.5s] beat #2
[INFO] usart1: [virt: 0.75s] beat #3
```

events_notify 的两件事,600 毫秒的信先到,900 毫秒钥匙凑齐,AND 掩码放行:

```text
[INFO] usart1: [virt: 0.6s] nt: letter says 42
[INFO] usart1: [virt: 0.9s] evt: both keys in (bits=0x00000003)
```

round_robin 的两个自旋任务,rr1 在 47 微秒出场,rr2 在 10 毫秒跟上来,此后每 500 毫秒各报一次到,间隔恒是 10 毫秒,谁也没饿着谁:

```text
[INFO] usart1: [virt:   47.15µs] rr1 alive
[INFO] usart1: [virt:      10ms] rr2 alive
[INFO] usart1: [virt:      0.5s] rr1 alive
[INFO] usart1: [virt:     0.51s] rr2 alive
[INFO] usart1: [virt:        1s] rr1 alive
[INFO] usart1: [virt:     1.01s] rr2 alive
```

testaments 平跑是一行一行的 alive,戏在注入里。我从 Renode 监视器往串口喂 `0x79`,terminal 任务砸了自己的栈金丝雀,下一次 PendSV 巡逻就把报告递了出来:

```text
[INFO] usart1: [virt: 1.25s] term: smashing my canary
[INFO] usart1: [virt: 1.25s] [STACK OVERFLOW] task=term eaten=2
```

喂 `0x78` 则是另一条路,`udf #0` 烧一条未定义指令,CPU 级的硬错误当场触发,完整报告连同现场八个字一起留下来:

```text
[INFO] usart1: [virt: 1.23s] term: executing an undefined instruction, goodbye
[INFO] usart1: [virt: 1.23s] *** HARD FAULT *** task=term prio=0
[INFO] usart1: [virt: 1.23s] pc=0x080004EA lr=0x08000D8B xpsr=0x01000000
[INFO] usart1: [virt: 1.23s] cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000
[INFO] usart1: [virt: 1.23s] stack: 00000001 20000383 C0000000 08000D79 00000000 08000D8B 080004EA 01000000
```

cfsr 的 0x00010000 是未定义指令位,hfsr 的 0x40000000 是 FORCED 位,报告跟 ARMv7-M 的寄存器定义对得上号。同一个 demo,两种死法,遗言一份没缺,`demo::setup` 里那行安装在这两场戏里都在岗位上。

## 验收

咱们把可查证的事实归拢一下,每件我都交代验法。

第一件是八个 demo 的构建与运行。您 checkout 到 `0054cf8`,照上面那条 `cmake -B` 配好交叉构建,八个 `zeros-demo-*` 目标应该全部编出来,各自 `run-*` 一行就能在 Renode 里跑。固件体积是真实的,我把 `arm-none-eabi-size` 的报告抄成一张表(text 是代码段,data 是已初始化数据,bss 是零初始化数据):

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

(bss 差异有它的道理:走 arena 路的 demo 把任务存储切在 bss 里的那块 2048 字节工地上,bottom_half 和 timers 要么任务少要么存储长在了 data 侧,数字对得上各家的戏份。)

第二件是 host 侧。我把仓库用 `git archive 0054cf8` 导到了 /tmp 的副本里,真实的 STM32CubeF1 拷进副本的 third_party(它在仓库里是一段引用,归档里是空的),按 README 的路子配了测试构建:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机的 GCC 是 16.2.1,ctest 的成绩单是真实的,14 个测试全部通过,总耗时 0.21 秒。调度测试自己的清单是 70 条断言、14 个用例,其中一个是本 commit 新交的,Arena 的对齐、切干拒收、不回绕全在它手里:

```text
startup arena: aligned slices, dry block refuses, no wraparound
```

用例里特意验了对齐垫在起点而不计进尺寸这件事:`take(1, 8)` 只挪 1 字节,下一次 8 字节对齐的请求要付 7 字节的垫,剩 8 字节的工地就真的切不动了。布线期的分配器看着朴素,边界一条没含糊。

第三件是注入实验,前面遗言和强处理器两节贴的 q、0x78、0x79 三场都是我亲手从 Renode 监视器喂出来的,`WriteChar` 命令往接收侧送字节,串口日志里的响应都在 5 秒的仿真窗内。

第四件是本篇没验的东西,我照实说:八间屋里 events_notify 和 round_robin 的机制面(事件组的等待链、时间片的切换节拍)我验的是串口行为的表象,内核侧的推演归它们各自的篇目,咱们不在这里抢戏。还有,串口输出我贴的全是抽取,每场日志的原始行数都比贴的多,您复跑的时候以自己 `run.log` 里的完整记录为准。

## 下一篇

屋子分好了,门牌挂齐了(注册中心给每个 demo 都配了名字和台本),下一件自然的事是让机器替咱们挨个点名验收 —— CI。下一篇《跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门》拆的是 d48f5a3,四道门禁把这一站的八个 demo 变成四道检查里的一道,每个 demo 的那句台词在 CI 里都被 grep 等着。咱们那儿见。
