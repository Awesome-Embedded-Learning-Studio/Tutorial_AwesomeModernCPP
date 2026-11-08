---
title: "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
description: "0bf9461 把 ZerOS 从时钟库变成了 RTOS,这一篇讲它的机制面:硬件异常帧八个字、fabricate_frame 在新任务栈顶伪造出生证明(xPSR 必须置 Thumb 位)、svc 0 做首次切换的跳板、EXC_RETURN 0xFFFFFFFD 与 naked 汇编里保存恢复 r4-r11;还兑现站 2 留下的伏笔,init_time 里 0xF0 改成 0xFF,让 SysTick 跟 PendSV 排在同一级;我自己在 /tmp 副本按 0bf9461 导出源码交叉编译,Renode 里看到 A/B 两任务按 1000/700ms 交错打印"
chapter: 3
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 26
prerequisites:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
related:
  - "CPU 归谁:从就绪位图到中断唤醒"
current_status:
  title: ZerOS的篇章正在打磨ing
  detail: PendSV 首切 · 等你验收
  variant: polishing
  icon: 🪄
---

# 在栈顶伪造出生证明:PendSV、SVC 与第一次切换

上一站收尾的时候,我留下过一个没有兑现的伏笔:`init_time` 里的 0xF0,后来被改成了 0xFF,让 SysTick 跟上下文切换的中断排在了同一级。您要问时基的心跳凭什么跟切换排一档,答案我就放在这里,不过它得等咱们把切换的机制走完才讲得清,因为 0xFF 的道理长在切换的过程里,咱们得眼看着切换从出生走到第一次发生,才明白两根中断为什么绑在一起。

咱们要拆的 commit 是 `0bf9461`,subject 写的是 “feat: Sched OK”,它一口气交了 674 行,调度器、切换的移植层、板上的双任务 demo 全在里面。选人的仲裁逻辑,就是就绪位图怎么挑人、被抢占的任务回不回队头,归仲裁面的文章管,我在这里就不重复了。咱们在机制面里只回答一个问题:一个从没运行过的任务,它的 CPU 状态是凭空造出来的,咱们到底怎么造,造完了又怎么让它跑起来。

## 异常帧:硬件与咱们各管八个字

Cortex-M3 上每个任务有一根自己的栈,栈顶的骨架(我把栈顶记录 CPU 现场的帧叫骨架,后面都这么叫)决定 CPU 从哪里接着跑。骨架的两半分工是清楚的,一半是硬件压的,另一半是咱们自己压的,咱们看 `include/ZerOS/arch/arm_cortex_m3/arch.hpp`,它是 0bf9461 新交的文件:

```cpp
struct ExceptionFrame {
    std::uint32_t r0;
    std::uint32_t r1;
    std::uint32_t r2;
    std::uint32_t r3;
    std::uint32_t r12;
    std::uint32_t lr;
    std::uint32_t pc;
    std::uint32_t xpsr;
};

struct SoftwareFrame {
    std::uint32_t r4;
    std::uint32_t r5;
    std::uint32_t r6;
    std::uint32_t r7;
    std::uint32_t r8;
    std::uint32_t r9;
    std::uint32_t r10;
    std::uint32_t r11;
};

static_assert(sizeof(ExceptionFrame) == 8 * sizeof(std::uint32_t));
static_assert(sizeof(SoftwareFrame) == 8 * sizeof(std::uint32_t));
```

`ExceptionFrame` 的八字是 ARMv7-M 的架构替咱们定的。异常进来的时候(您把中断、把 `svc` 都算进去),硬件会自动把八个寄存器(r0、r1、r2、r3、r12、lr、pc、xPSR)按次序压进了当前任务的栈,压完之后异常的处理程序就在骨架上干活了。异常返回的时候,硬件照原样把它们弹回去了。文件的末尾,两条 `static_assert` 守着八字帧的布局,哪天有人动了字段,固件是连编译都过不去的,布局对不上的事是轮不到运行时才暴露的。

`SoftwareFrame` 的八字是 r4 到 r11,它们的归属就得说清楚了:架构只规定了硬件压的八字,剩下寄存器的保存它是不管的,r4 到 r11 属于被调用方保存的范围,处理程序要动它们的话,就得自己存了再还。所以切换的代码是咱们亲手压软件八字的写法,而且压的位置比硬件的八字更低,因为异常返回的时候,硬件只认栈顶的八字。压和弹各是哪条指令的活,咱们到汇编的节里对号。

## fabricate_frame:在栈顶造一副假现场

任务还没跑过的时候,栈是空的,可是切换的逻辑要求每个任务的栈顶都得摆着一副完整的现场,所以出生的活儿由咱们替它干,伪造了一副完整的现场。代码我给您搬来了,搬的是 `src/arch/arm_cortex_m3/switch.cpp` 里 fabricate_frame 的一段,咱们整段看:

```cpp
void fabricate_frame(ZerOS::sched::TCB& t) {
    auto* top = t.stack_view_.data() + t.stack_view_.size();
    auto* hw = reinterpret_cast<ExceptionFrame*>(top) - 1;
    auto* sw = reinterpret_cast<SoftwareFrame*>(hw) - 1;

    *sw = {}; // Clear, memset set bytes, we say class clean
    *hw = ExceptionFrame{
        .r0 = reinterpret_cast<std::uint32_t>(&t.task_wrapper_), // 任务参数
        .r1 = 0x00000000,
        .r2 = 0x00000000,
        .r3 = 0x00000000,
        .r12 = 0x00000000,
        .lr = reinterpret_cast<std::uint32_t>(&Trap), // 返回即陷阱
        .pc = reinterpret_cast<std::uint32_t>(&sched::UserTaskWrapper::InvokeUserTask), // 跳板
        .xpsr = 0x01000000u,
    };

    t.stack_pointer_ = reinterpret_cast<std::uint32_t*>(sw);
}
```

指针的算术,您跟着我走一遍就够了:栈的 `span` 是从低地址往高地址长的,栈顶落在 `data() + size()` 的位置,`hw` 拿栈顶减去了一副 `ExceptionFrame` 的宽度,意思是硬件的八字住在栈顶紧底下的八个字里,`sw` 再往下让出了软件保存区的位置。字段都填完之后 `stack_pointer_` 里存的就是 `sw` 的地址,往后的切换汇编拿着它就能把整副现场续上。

假现场里的字段,咱们逐个说它们的用意。

`pc` 填的是 `InvokeUserTask`,它是异常返回之后 CPU 开始执行的落点,它的老家在 `include/ZerOS/kernel/sched/task_control_block.hpp`,咱们看:

```cpp
struct UserTaskWrapper {
    using UserTask = void (*)(void*);
    UserTask function;
    void* argument;

    // When Marking Static, it is free, plain ABI
    static void InvokeUserTask(UserTaskWrapper& wrapper) {
        if (wrapper.function) {
            wrapper.function(wrapper.argument);
        }
    }
};
```

它被做成了 `static` 的成员函数,注释里的那句 “it is free, plain ABI” 把理由带了出来:异常返回的时候,CPU 是把 `pc` 直接塞进程序计数器就跳过去的,不走任何 C++ 的调用约定,所以函数的实参得按最普通的 ABI 来收才接得住,您给它一个 `this` 形状的引用(也就是 `wrapper`)正合适。您要是做成了普通成员函数或者带捕获的可调用物,首参数是什么就说不清了,所以这里挑的正是 Plain ABI 的形状,首切直接跳进来的戏法才成立。

`lr` 填的是 `Trap`,它的家安在 `include/ZerOS/arch/arm_cortex_m3/trap.hpp` 里,咱们看它的样子:

```cpp
extern "C" [[noreturn]] inline void Trap() {
    while (1) {
        asm volatile("wfi");
    }
}
```

`InvokeUserTask` 把用户函数跑完就 `return` 了,返回的地址就是 `lr` 里的 `Trap`,落进去就是一个 `wfi` 的死循环(等中断)。任务函数是不许返回的,它一返回就落进了陷阱,`Trap` 的名字把用意带上了,您一眼就看明白。

`r0` 填的是 `&t.task_wrapper_`,咱们把用户函数和它的实参装在里面,当成传给跳板的参数。剩下的 r1、r2、r3、r12 填的是零,咱们用零当初值,是最干净的选择。

最要紧的值是 `xpsr = 0x01000000u`,我写文章的时候在它身上停了很久。0x01000000 对应的是 xPSR 的 T 位(Thumb 位)。Cortex-M3 只认 Thumb 的指令,异常返回的时候硬件会查 T 位,查到了零就当场进 HardFault。您忘了置它的话,伪造的现场看起来什么都是对的,任务也切过去了,可是 CPU 落在 `InvokeUserTask` 的头一条指令上,整个系统就被拖进了 fault。一个值的生死压在了一个位上,标题里的“出生证明”选的就是它:少了 T 位,出生证就作废了。

::: warning
xPSR 除 Thumb 位之外还有别的字段(条件标志、中断号这样的),伪造帧的时候把它们全置零是安全的,异常返回会把 IPSR 之外的部分照单全收,拦路的检查只有 T 位。您往后要是到了带 FPU 的 M4 这样的核上做同样的活,硬件压的帧会更长,布局就得跟着架构手册的图走,0x01000000 的值本身仍然是对的。
:::

## start_scheduler:武装与首切,一去不回

会造现场了,咱们来看它是怎么被点燃的。要一个从没跑过的任务上 CPU,最直白的想法是在 C++ 里直接调它的函数,可这么干的话,跑着的还是 main 的现场:用的栈是 main 的栈,寄存器是 main 的寄存器,任务自己的栈一步都没沾上,咱们要的切换就根本没发生过。仓库的办法是把点火交给一场异常,为什么偏偏是异常,咱们看完代码细说。代码就在 `switch.cpp` 的末尾,头一截是负责装配的 `spawn`:

```cpp
void spawn(ZerOS::sched::TCB& t) {
    fabricate_frame(t);
    system_sched.add(&t);
}
```

头文件里的声明挂着中文注释,`spawn` 的那一句写的是“伪造帧 + add”,咱们看函数体,干的就是这两件事:替任务把现场伪造好、再交给调度器入队。

点火的半截是 `start_scheduler`,咱们看它:

```cpp
void start_scheduler(std::span<std::uint32_t> idle_stack) {
    auto idle = system_sched.fetch_idle_task();
    idle->task_wrapper_ = {+[](void*) {
                               for (;;) {
                                   asm volatile("wfi");
                               }
                           },
                           nullptr};
    idle->stack_view_ = idle_stack;
    fabricate_frame(*idle);

    *kShpr3 = (*kShpr3 & 0xFF00FFFFu) | (0xFFu << 16);
    asm volatile("dsb\nisb\nsvc 0");
    for (;;) {
    }
}
```

它的注释写的是“武装 + 首切,一去不回”,您看它还标了 `[[noreturn]]`,调度器开跑之后 `main` 就再也回不了头了,末尾的空循环其实是到不了的,摆在那里是为了让编译器安心。函数里的事情,咱们按次序过:idle 的任务体被装成了一个 `wfi` 的循环(没活干的时刻让 CPU 睡着等中断),栈给它配上了,现场也给 idle 伪造了一副。PendSV 的优先级写在后面,伏笔的节里咱们再讲。最后落的是 `svc 0`,首切的引线就从这里点着了。

`svc` 是 Supervisor Call 的指令,它当场就触发了一个 SVC 异常,向量表的 11 号槽住着它的处理程序。您也许要问,首切为什么不写成一个直接的函数调用,非要绕道异常?因为异常返回是咱们手里唯一能把“任意的现场”装进 CPU 的通道:`svc` 进来的时候,硬件在 MSP 上压了 main 的现场(main 的现场咱们是不要的),处理程序干完了活就做一次异常返回,返回到哪里、用哪根栈都由咱们在处理程序里布置。main 在 MSP 上留下的栈从此就没人再碰了,MSP 留给了中断,任务都搬去了 PSP,分家就在 `svc 0` 的这一下完成。

处理程序整个就是一段内联的汇编,咱们看:

```cpp
extern "C" [[gnu::naked]] void SVC_Handler() {
    asm volatile("cpsid   i\n"
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

`[[gnu::naked]]` 告诉编译器别生成序言和尾声(压栈、恢复寄存器之类的活),因为栈上的每个字节都归处理程序自己管,编译器要是不知情也来压一份的话,布局就全乱了。咱们逐条走:

`cpsid i` 把总中断关了,切换的中间谁也进不来。`bl context_switch` 调进了 C++ 的世界去选下一个任务,r0 进去的时候装的是“要保存的栈指针”,出来的时候换成了“下一个任务的栈指针”。`ldmia r0!, {r4-r11}` 把软件保存区的八字弹回了 r4 到 r11,弹完之后 r0 自己也涨到了硬件帧的头上。`msr psp, r0` 把指针写进了 PSP(线程模式的栈指针),任务从此就跑在 PSP 上了。`ldr lr, =0xFFFFFFFD` 是异常返回的钥匙:0xFFFFFFFD 告诉硬件的是“回到线程模式,用 PSP,没有 FPU 扩展”。`cpsie i` 把中断开了,最后 `bx lr` 执行的正是异常返回,硬件从 PSP 指着的栈顶弹出八字,r0 到 r3、r12 是咱们伪造的初值,pc 落在了 `InvokeUserTask` 上,头一个任务就这样活过来了。

C++ 那边的 `context_switch` 只有三行,咱们也看一眼:

```cpp
extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    if (saved_sp != nullptr) {
        system_sched.current_task()->stack_pointer_ = saved_sp;
    }
    return system_sched.pick_next()->stack_pointer_;
}
```

实参非空的话,说明有一个正在跑的任务,咱们就把它的栈指针存进它的 TCB。实参是空的话就说明没有任务在跑,咱们直接去选下一个。选人的活归 `pick_next`,仲裁面的文章讲过了。这里我照实交代一件事:`svc 0` 的路径上,r0 进 `context_switch` 之前是没人清零的。此刻的 `current_` 还是空指针,只要 r0 的残留值不是零,`current_task()` 的解引用就会往一个野地址上写字,这就成了未定义行为。真实的运行里没炸,靠的是运气,main 那一刻的 r0 残留值恰好不作怪。仓库的下一个 commit(6308dd0)补了一句 `movs r0, #0`,注释写的是 “first switch ever: nobody is running, nobody to save”,运气就这样换成了明示的 nullptr。我把当时的形状原样给您看,因为它对理解首切的路径太有用了:您能亲眼看到首切时没有任务可存,当时的代码却是用一个没写的分支把它表达出来的。

PendSV 的处理程序跟 SVC 是同一副骨架,多出来的是“保存”的半截,咱们对照着看:

```cpp
extern "C" [[gnu::naked]] void PendSV_Handler() {
    asm volatile("cpsid   i\n"
                 "mrs     r0, psp\n"
                 "cbz     r0, 1f\n"
                 "stmdb   r0!, {r4-r11}\n"
                 "1:\n"
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

PendSV 是从任务里切出来的(任务跑在 PSP 上),所以它得 `mrs r0, psp` 拿到当前任务的栈指针,把 r4 到 r11 用 `stmdb r0!, {r4-r11}` 压了进去(`db` 的意思是 decrement before,压完之后指针就恰好落在软件保存区上了,前面说的布局就这么续上了)。`cbz r0, 1f` 是个兜底的保险:PSP 是零的话就说明没有任务在跑,它就跳过保存的步骤直接去选人。后面的五行跟 SVC 是一字不差的,您把两个处理程序并排放着读一遍,保存与恢复的对称关系就全在眼前了。

以后任务主动让出了 CPU、或者睡眠到点被叫醒了、或者更急的任务就绪了,咱们看到的路都归 PendSV。调度 API 自己从不在调用的现场直接切,它们只把一个切换的请求记下来,真正干活的是 PendSV。请求是怎么记的?就写在下面的一行里:

```cpp
void CortexM3SwitchPort::request_switch() {
    *kIcsr = (1u << 28); // Set the PendSV intrs
}
```

ICSR 的全名是中断控制与状态寄存器,它的地址落在 0xE000ED04,它的 bit 28 是 PENDSVSET,咱们往里写 1,就把 PendSV 挂起了。挂起了却不立刻切,这就是延迟切换的路数:中断的处理程序里调它,切换也不会发生在中断的肚子里,而是等中断退出之后,PendSV 排上了队再切。延迟的设计在后面的中断唤醒篇里会兑现,咱们今天把机制记在心里就够了。

## 伏笔兑现:SysTick 为什么跟 PendSV 排同一级

现在咱们能回答上一站留下的问题了。上一站里 `init_time` 的优先级是 0xF0,咱们翻 0bf9461 的 `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`,改动落在了 `init_time` 的身上:

```cpp
void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    // same level with PendSV
    *kScbShpr3 = (*kScbShpr3 & 0x00FFFFFFu) | (0xFFu << 24);
    *kSystRvr = cycles_per_tick - 1;
    *kSystCvr = 0;
    *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
}
```

`start_scheduler` 里给 PendSV 的一行是 `0xFFu << 16`,给 SysTick 的一行是 `0xFFu << 24`,咱们对一对,写的是同一个寄存器 SHPR3 的两个字节:PendSV 的优先级字段住在 bits 16 到 23 里,SysTick 的住在 bits 24 到 31 里(PendSV 与 SysTick 两个内核异常的编号是 14 和 15,SHPR3 管的正是它们)。两个都写了 0xFF,注释里的一句 “same level with PendSV”,把意图说完了。

您可能要问为什么都得是 0xFF、为什么还得同级?Cortex-M3 的口径是数值越小越急,0xFF 是垫底的一档,咱们分两头说。

咱们让 PendSV 垫底,图的是不插队。任务切换的活儿要压栈、要弹栈、要换栈指针,整套动作的前提是旧任务的现场已经保存完整了。要是 PendSV 的优先级很高,一个 USART 的中断在切换进行到一半的时候扎进来,它自己压栈走的是 MSP,不碰任务的栈,可它看见的 r4 到 r11 还是切换到一半的残留值,而且它一旦调了调度 API,又可能把新的 PendSV 挂起一遍,现场的归属就乱成一团了。垫了底之后,任何真正的中断都能把切换的脚步顶住,等它们全干完了,切换的戏才轮得到上场。

SysTick 跟它是同级的,是因为时基的心跳不能戳进切换的肚子里。您想这样的画面:PendSV 刚弹完了 r4 到 r11,PSP 已经换成了新任务的栈,异常返回还没执行的当口,就在这样的夹缝里 SysTick 进来了。它会跑 `SysTick_Handler` 里的 `on_elapsed`,要动时间队列的。它的栈、它看见的寄存器状态,都属于一个还没交接完的半成品现场。两个中断要是不同级的话,急的那个就能扎进来,可是这个场景里的两个都是内核自己的心跳,谁急谁慢的事根本不该由它们俩自己决出。同级的两个异常互相是抢不着的,一个在跑的时候另一个就只有排队的份,跑完的退出之后,尾链(硬件一口气处理排着的异常、不再完整出入栈)就过去了,切换的中间态就这样被保护起来了。上一站留的问题,咱们的答案就落在这里:时基的心跳排一档,理由与谁更重要的评判无关,道理在于同级的异常谁也不能打断谁。

## 板上 demo:两个任务轮流醒来

机制说完了,咱们看它跑起来是什么样。验收一个调度器的话,单任务是看不出切换的,打印的图案要对得上一张时间表,场上就得有两个节奏错开的任务。仓库把 `example/main.cpp` 从单 ticker 升级成了双任务,骨架咱们分两截看,头一截是栈和 TCB 的静态声明:

```cpp
alignas(8) constinit std::uint32_t stack_a[64] = {};
alignas(8) constinit std::uint32_t stack_b[64] = {};
alignas(8) constinit std::uint32_t idle_stack[32] = {};

constinit ZerOS::sched::TCB tcb_a{};
constinit ZerOS::sched::TCB tcb_b{};
```

栈和 TCB 全是 `constinit` 的静态对象,零堆的纪律咱们延续着。

栈和 TCB 备齐了,接着上场的才是干活的两个任务体,咱们看:

```cpp
void task_a(void*) {
    for (;;) {
        ZerOS::board::print("A\r\n");
        task_sleep(1000);
    }
}

void task_b(void*) {
    for (;;) {
        ZerOS::board::print("B\r\n");
        task_sleep(700);
    }
}
```

两个任务体是对称的,干的都是打一个字母就睡一觉的循环。A 睡 1000ms 的话,B 睡的是 700ms,所以咱们看打印的节奏,注定就是交错的。

`task_sleep` 是在 example 里手搓的,咱们把它单独拎出来看,因为下一篇文章的主角就藏在里面:

```cpp
ZerOS::sched::TCB* owner_of(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    return reinterpret_cast<ZerOS::sched::TCB*>(
        reinterpret_cast<std::byte*>(w.get()) - offsetof(ZerOS::sched::TCB, action));
}

void wake(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    system_sched.ready(owner_of(w));
}

void task_sleep(std::uint32_t ms) {
    auto cur = system_sched.current_task();
    cur->action.OnTime = wake;
    system_time.call_after_span(&cur->action, ms);
    system_sched.block(cur);
}
```

睡眠的思路是借力:任务的 TCB 身上本来就内嵌着一个 `TimeWaiter`(上一站的老朋友),咱们把它挂进时间内核,到点了之后回调 `wake` 把任务放回就绪的队列。`owner_of` 是一个从成员反推所属对象的把戏,内嵌成员的地址减去它在结构体里的偏移,得到的就是 TCB 自己的地址,`offsetof` 在编译期就把这个偏移算好了。

`main` 的收尾动作是下面的几行,咱们一起看:

```cpp
    tcb_a.task_priority_ = 1;
    tcb_a.name_ = "a";
    tcb_a.task_wrapper_ = {task_a, nullptr};
    tcb_a.stack_view_ = stack_a;

    tcb_b.task_priority_ = 1;
    tcb_b.name_ = "b";
    tcb_b.task_wrapper_ = {task_b, nullptr};
    tcb_b.stack_view_ = stack_b;

    ZerOS::arch::cortex_m3::spawn(tcb_a);
    ZerOS::arch::cortex_m3::spawn(tcb_b);

    ZerOS::arch::cortex_m3::init_time(SystemCoreClock / 1000);
    ZerOS::arch::cortex_m3::start_scheduler(idle_stack);
```

两个任务排的是同一级(它们的优先级都是 1),`spawn` 替咱们把帧伪造了、把任务入队了,时基布的是 1kHz 的,然后 `start_scheduler` 点了火。`svc 0` 的异常一响,队头的 task_a 就活过来了,它打完一个字母就睡了 1000ms。调度器接着选到了 task_b,它打了一个字母就去睡 700ms,谁睡醒了就轮到谁回来跑。`main` 的栈从此再没人用过,CPU 就在两个任务和 idle 之间来回倒手了。

## 验收

我自己复现的链路是这样的:把仓库拷到了 /tmp 的副本里,checkout 到了 `0bf9461`(third_party 下的工作树是个 gitfile,也就是 .git 只是个指到真实仓库的文件,我是拿 `git archive 0bf9461` 把树导出去的),再配上交叉的工具链构建固件:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

构建的体积是真实的,我把它贴在下面:

```text
   FLASH:        1680 B        64 KB      2.56%
     RAM:         944 B        20 KB      4.61%
   text	   data	    bss	    dec	    hex
    1488	     192	    752	   2432	    980
```

调度器、切换层、双任务的 demo 全住进去了,咱们看数字:flash 用掉的是 1680 字节,RAM 占了 944 字节(栈和 TCB 的家都在 bss 里,752 字节主要是它们的花销),离板包注释里立项时定下的预算编号 D12 还很远,预算线画在核心 flash 6KB、RAM 1KB 的位置。

然后咱们上 Renode 跑了 8 秒。上一站跑 ticker 的文章交代过的经验,我再照实说一遍:我本机的 Renode 1.17.0 照样认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 类型,我把 i2c1、i2c2 的两个节点整块注释掉了,仿真就起来了(demo 是不经过 i2c 的,所以行为是不受影响的)。串口窗的真实输出如下:

```text
[INFO] usart1: [host: 0.17s (+0.17s)|virt: 0s (+0s)]
[INFO] usart1: [host: 0.17s (+0.81ms)|virt: 0s (+0s)] ZerOS multi-task demo @ Blue Pill
[INFO] usart1: [host: 0.18s (+13.96ms)|virt: 19.12µs (+19.12µs)] A
[INFO] usart1: [host:  0.19s (+0.66ms)|virt:  20.74µs (+1.62µs)] B
[INFO] usart1: [host:   0.83s (+0.64s)|virt:       0.7s (+0.7s)] B
[INFO] usart1: [host:    1.13s (+0.3s)|virt:         1s (+0.3s)] A
[INFO] usart1: [host:    1.53s (+0.4s)|virt:       1.4s (+0.4s)] B
[INFO] usart1: [host:    2.13s (+0.6s)|virt:         2s (+0.6s)] A
[INFO] usart1: [host: 2.23s (+99.71ms)|virt:      2.1s (+100ms)] B
[INFO] usart1: [host:    2.93s (+0.7s)|virt:       2.8s (+0.7s)] B
[INFO] usart1: [host:    3.13s (+0.2s)|virt:         3s (+0.2s)] A
[INFO] usart1: [host:    3.63s (+0.5s)|virt:       3.5s (+0.5s)] B
[INFO] usart1: [host:    4.13s (+0.5s)|virt:         4s (+0.4s)] A
[INFO] usart1: [host:    4.33s (+0.2s)|virt:       4.2s (+0.2s)] B
[INFO] usart1: [host:    5.03s (+0.7s)|virt:       4.9s (+0.7s)] B
[INFO] usart1: [host:    5.13s (+0.1s)|virt:        5s (+100ms)] A
```

您看 virt 的部分,咱们要的验收事实都在:B 的节奏是每 0.7 秒一行,A 的节奏是每 1 秒一行,B 的行数比 A 密(700 比 1000 的周期比),交错的图案跟两个周期的重叠规律完全对得上。开机 19 微秒的时候 A 打了头一个字母,21 微秒 B 跟上了,这正是首切的痕迹:`svc 0` 选中了队头的任务,它打完字母就睡了,调度器立刻切给了第二个。

host 侧的单测我照实交代:0bf9461 交的 `test/test_sched.cpp` 在当时是编不过的,测试的桩缺了 `Switchable` 概念要求的 lock/unlock,直到下一个 commit 才补上了,所以当时的 “Sched OK” 验的是 Renode 里复现的板子,CI 的绿灯压根就不存在。故事的后续连同怎么亲手补上两个空函数,仲裁面的文章里有完整的展开,我不越它的界。

## 下一篇

机制跑通了,可是您回头再看 `task_sleep` 会发现它是三步分离的:设回调、`call_after_span` 布防、`block` 阻塞,三步之间是没有任何锁的。要是布防之后、阻塞之前的话,定时器正好到了点,`wake` 会发现任务还没进入 Blocked 的状态,唤醒的登记就落了空,任务接着 `block` 睡了下去,永远没有人来叫它了。这个丢唤醒的竞态是真实存在的,提交者把它原样留在了 example 里,等到 6308dd0 才用 `park_for` 把布防和阻塞关进了同一个临界区,整个调度器的类型封装也被它重做了一遍。下一篇咱们要干的事就是复现睡死的过程,再看它是怎么被堵上的。
