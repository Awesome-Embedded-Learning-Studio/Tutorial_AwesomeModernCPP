---
title: "等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货"
description: "commit 766c462 把等待从任务的身上解了下来:base::Callback 拿 fn 加 void* 当裸机硬通货,trivially copyable 的静态断言挡住了 std::function 的堆与捕获,TimerBase 的单次到点即冷、周期自转重挂、stop 幂等,连回调内自杀的竞态都有专测看着,host 侧六个定时器用例十一断言全数通过,我在 /tmp 副本交叉构建,Renode 里按 t 挂了三百毫秒的单次定时器,虚拟时间 2.3 秒布防,2.6 秒回调开腔,而任务 c 布完防转身就跑了,worker 的重活与定时器的提醒从此用的是同一枚硬币"
chapter: 5
order: 3
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 回调机制
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
  - "中断只递条子:环形队列、工作者线程与底半推迟"
---

# 等三百毫秒,不养一个任务:软件定时器与 fn+void* 硬通货

《中断只递条子》收尾的时候,咱们留了一个问题:worker 干的是立刻执行的活儿,要是想要的只是“过 300ms 之后提醒我一声”呢?为一声提醒去 spawn 一个任务,任务控制块、一整块栈、出生的整套手续,全都得跟着上了岗,而它干的其实只是睡一觉,到点了喊一嗓子,接着又睡了回去。等待本该是内核里最便宜的服务,`766c462` 把它做成了,subject 写的是 “feat: timer setup”,13 个文件的改动,新增了 309 行,删了 16 行,软件定时器从它起进了内核。咱们本站的主题是**代码跑在哪个上下文**,《芯片死前立遗嘱》管的是崩了之后的现场,《中断只递条子》管的是中断把重活卸给谁,定时器管的是末尾的一段:不占任务、不碰堆的等待怎么个等法。

## 一个提醒的代价,咱们算一下

任务的成本咱们在调度器那几站里就数过了:一个 TCB,一根按字对齐的栈,内核还得替它排了班。spawn 出来的任务里最小的栈是 64 个字,256 字节的 RAM,这还只是栈本身的数,管理它的结构没算进去。idle 的栈是另一回事,它不走 spawn 的手续,32 个字的长度也就是 128 字节,走的是 `start_scheduler` 收进来的那根专用栈。如果系统的需求只是“每隔一段时间提醒我”,那么为它常驻一个任务的玩法,等于让一位全职的员工坐在工位上,每天的活儿只是中午响了一次闹钟。

`766c462` 给出的定时器是什么样的家底呢,咱们到 demo 里看它的声明:

```cpp
// 't' arms a one-shot reminder — no task is consumed while it waits
constinit ZerOS::clock::Timer g_demo_timer{};

void timer_ping(void*) {
    ZerOS::board::print("timer: fired (tick context, no task spent)\r\n");
}
```

带着 `constinit` 的声明,编译期就各就各位了,注释把承诺写在了名字旁边,等它响的全程没有一个任务被消耗掉。注释说的话可不是口号,咱们到源码里对证,定时器自己长什么样、靠谁计时、到点之后谁去执行回调,三件事都有实的代码撑着。

## base::Callback:fn 加 void*,裸机的硬通货

定时器到点之后要执行的是用户的代码,而代码得有个地方装它才跑得起来。装回调的容器,咱们在桌面世界里用惯了的是 `std::function`,它什么都能装:lambda、函数对象、绑定了一堆参数的调用。代价藏在它什么都能装这件事里。捕获了变量的 lambda 进了 `std::function`,状态就得找地方放,捕获的状态放不下实现内嵌的那一小块缓冲区的时候,实现就去堆上讨了一块内存。拷贝了一份回调,咱们管理的还是堆上不知道什么类型的生命周期,它因此也不是 trivially copyable 的,咱们连把它按字节塞进队列、塞进 TCB 都做不到。`std::function` 是桌面世界的富家做派,裸机是养不起它的。

`include/ZerOS/base/callback.hpp` 交出了裸机的答案,整个文件就 28 行的长度,咱们全篇看:

```cpp
#pragma once

#include <type_traits>

namespace ZerOS::base {

// the bare-metal currency: a function plus its context. No closures, no
// heap — whenever something must "call back later", this is the coin.
struct Callback {
    using Fn = void (*)(void*);

    Fn fn{};
    void* arg{};

    constexpr Callback() = default;
    constexpr Callback(Fn f, void* a) noexcept : fn(f), arg(a) {}

    void invoke() const noexcept {
        if (fn != nullptr) {
            fn(arg);
        }
    }
    explicit operator bool() const noexcept { return fn != nullptr; }
};

static_assert(std::is_trivially_copyable_v<Callback>); // must ride queues and TCBs

} // namespace ZerOS::base
```

作者自己的注释把它的地位定下了,咱们翻成白话:这是裸机的通货,函数指针加一个上下文指针的组合,闭包是没有的,堆也是没有的,凡是内核得回头调用您一回的场合,流通的就是它这一枚。`invoke` 判了空再调用,`operator bool` 让您问得出它装没装东西,成员全是编译期的形状,连构造函数都标了 `constexpr`。真正的门槛在最后那行 `static_assert`:它必须是 trivially copyable 的,注释说了,能搭乘队列与 TCB 是它的硬条件。这行断言把 `std::function` 拒之门外的方式很有意思,它不跟您争论该不该用堆,它只要求的是这个类型可以按字节复制,而 `std::function` 天生就过不了,争论都省了。

`void* arg` 这个设计咱们也停一下。回调执行的时候,函数指针收到的就是 `arg` 这个指针,上下文的解释权归调用方。想要带状态的回调,姿势是把状态的地址塞进 `arg`(测试里定时器自杀的那个用例就是这么干的,咱们后面看),想要无状态的回调,塞个 `nullptr` 也就完事了。丢掉的是 lambda 捕获的便利,换来的是两根指针的确定形状,这笔交易放在 flash 按字节算的项目里,咱们觉得是划算的。

## TimerBase:出生自带反应的定时器

定时器的真身在 `include/ZerOS/kernel/clock/timer.hpp`,咱们把骨架搬出来:

```cpp
template <ZerOS::system::SystemContext System> struct TimerBase {
    constexpr TimerBase() noexcept : waiter_{Ticks{0}, &TimerBase::fired} {}
```

头一行就值得您停一下。定时器的身上内嵌着一个 `TimeWaiter`,出生的时候就把自己的反应定好了,反应定的是它自己的 `TimerBase::fired` 这个静态函数。《一秒钟的定义权》里咱们见过时间内核的用法:往到期队列里挂一个等待者,到点了内核按等待者身上带着的反应去回调。那个 ticker 的反应是咱们用一元 `+` 把无捕获的 lambda 褪出来的函数指针,现场定制的。`TimerBase` 把这套手续收进了构造函数,每个定时器出生的那一刻,反应就长在了它身上,用户再也用不着自己褪 lambda 了。这是同一个内核件的两种用法:内核管的是到期队列,定时器是站在队列之上的用户面。

`owner_of` 这个把戏咱们也认得,《在栈顶伪造出生证明》里 TCB 就用它从内嵌的等待者反推过自己的主人,咱们看定时器的版本:

```cpp
    [[nodiscard]] static TimerBase* owner_of(base::BorrowedPtr<clock::TimeWaiter> waiter) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
        return reinterpret_cast<TimerBase*>(reinterpret_cast<std::byte*>(waiter.get()) -
                                            offsetof(TimerBase, waiter_));
#pragma GCC diagnostic pop
    }
```

内嵌成员的地址减去它在结构体里的偏移,得到的就是定时器自己的地址,内核从到期队列里递回来的只是等待者的指针,定时器的身份靠这一次减法找回来,没有任何额外的映射表要维护。`offsetof` 对非标准布局的类型是有意见的,所以这里压了一条编译警告,咱们照实告诉您,这层压制是咱们为把戏付出的价格,白来的便利在这里是没有的。

布防的三个入口在同一个文件里,咱们挨个看:

```cpp
    // once: fn(arg) fires after `span`, then the timer goes cold
    void oneshot(Milliseconds span, base::Callback::Fn fn, void* arg) noexcept {
        arm_me(0, span, base::Callback{fn, arg});
    }

    // forever: every `span` — until stop(), even from inside fn
    void periodic(Milliseconds span, base::Callback::Fn fn, void* arg) noexcept {
        arm_me(span.count, span, base::Callback{fn, arg});
    }

    void stop() noexcept {
        auto& s = System::self();
        ZerOS::irq::CriticalGuard g{s};
        period_ = 0;
        running_ = false;
        s.cancel_timer(&waiter_); // idempotent: not booked = false, harmless
    }
```

单次与周期的全部区别,是传给 `arm_me` 的周期数是零还是非零。`stop` 进门就握了临界区,`period_` 被它清了零,`running_` 也置了假,接着它向系统讨来了 `cancel_timer`,把登记在时间内核里的那个等待者撤了下来。注释把幂等的语义交代了:没登记过的等待者,`cancel` 交回来的就是假,不吵也不闹的,谁也不会为它的返回值发愁。您 stop 一个从来没布过防的定时器,它安静地让您 stop,这就是幂等的做派。

布防的本体是私有的 `arm_me`,它的次序有讲究,咱们看:

```cpp
    void arm_me(Ticks::tick_t period, Milliseconds span, base::Callback callback) {
        auto& s = System::self();
        ZerOS::irq::CriticalGuard g{s}; // fields only move under the lock —
        period_ = period;               // a firing tick may be reading them
        call_ = callback;
        running_ = true;
        s.arm_timer(&waiter_, span.count);
    }
```

咱们把它的纪律念一遍:字段的动只能在锁下,注释把原因写在了旁边,到点的那一拍可能正在读它们。`period_`、`call_`、`running_` 三个成员是定时器与 tick 两头的共享状态,写入的每一回都关了中断再动,这是这个文件里所有共享状态共同的纪律。板上的临界区走的是 BASEPRI,掩的级别是 0x40,而 SysTick 的优先级在 `init_time` 里被压到了最低(源码往 SHPR3 的高字节写的是 0xFF,NVIC 只实现优先级的高四位,落到硅片的有效级别与 0xF0 等价,都是最低的那一档),写字段的时候 tick 根本插不进来,读写交不了错。

## 到点之后:fired 的两个分支

到点的执行者是出生就带上的 `fired`,它是整个定时器的发动机,咱们完整看它:

```cpp
    static void fired(base::BorrowedPtr<TimeWaiter> w) {
        auto* t = owner_of(w);
        t->running_ = false;    // oneshot cools off by default
        t->call_.invoke();      // user fn (tick context!)
        if (t->period_ != 0) {  // periodic: re-book — a stop() INSIDE fn
            t->running_ = true; // already zeroed period_ and walks free
            System::self().arm_timer(w, t->period_);
        }
    }
```

它的三个动作咱们按次序念。头一个动作是 `running_` 被置了假,单次定时器到了点就冷了下来,注释说了这就是默认的下场。跟着它执行用户的回调,注释里的那句 “tick context!” 是您得放在心上的事实,回调跑在 tick 的上下文里,不在任何任务的身上,咱们到验收的段落里拿真输出验它。末了它检查 `period_`,非零的话就重挂下一轮,周期定时器就这样自转起来了,《一秒钟的定义权》里 ticker 自己给自己续期的手工自转,在这里成了内核件的标准行为,内核里依然没有专门的周期定时器类型,周期的本质还是“回调里再登记一次”,只不过登记这一步归了内核代劳。

而注释里那两行的分量,得配着 `stop` 的原文才读得出来。咱们设想这个场面:周期定时器到点了,`fired` 跑进了用户的回调,回调干的却是把这个定时器自己 stop 掉。等回调返回了,`fired` 回头来读的是 `period_`,它读到的是零,因为 `stop` 在锁下把它清了,重挂的分支走不进去,定时器就这般退了场。回调内自杀这个动作看起来像跟时间内核抢一个正在执行自己的对象,实际上抢的只是三个标志位,而标志位的读写全在关中断的临界区里,次序是定死的,清零的走在前头,检查的跟在后头,检查的人看到的永远是清零之后的世界。这层保证要是缺了,自杀的定时器会复活一轮,周期就多打了一轮,这类错在板子上查起来是要命的,它几百毫秒才犯了一回,查案的人等到头发白都未必逮得着。

## 同一枚硬币:Work 也换成了 Callback

`base::Callback` 在这个 commit 里的第二个去处,咱们也得看,那就是《中断只递条子》里的工作者。`user_worker.hpp` 里原来的 `Work` 结构体,自己定义了一套函数指针加 `void*` 的字段,还配了一个 `InvokeSelf` 的静态方法。`766c462` 把它整个删了,换上的是:

```cpp
using Work = base::Callback;
```

这是轻飘飘的一行别名,咱们把它的分量掂一掂,工作者的队列里流的活儿,从此与定时器的回调是同一个类型。`defer` 的签名跟着改了,`Loop` 里的 `Work::InvokeSelf(*user_work)` 换成了 `user_work->invoke()`,调用逻辑在咱们眼里一个字都没有动过,因为 `Callback::invoke` 干的就是原来 `InvokeSelf` 干的事。改动的意义在类型层面,咱们放在一起看:内核里所有“以后再执行”的东西,现在共享的是同一个平凡的、可按字节复制的形状,工作者收它的重活,定时器收它的提醒,通货是统一的。demo 里 worker 的 `heavy_job` 与定时器的 `timer_ping`,咱们现在可以拿同一把尺子量它们了。

通货统一了,系统的接口也得跟着扩容。`SystemContext` 这个 concept 是系统面的清单,咱们看 `766c462` 给它添的两个要求:

```cpp
    { s.arm_timer(w, span) };   // book a waiter on the clock ledger (timers)
    { s.cancel_timer(w) };      // ...and strike it off (idempotent)
```

定时器不直接认识任何一个具体的系统,它只要求的是系统会布防、也会撤防。板上的实现在 `system.cpp` 里,各自就是转手调一次时间内核的一行函数。这个扩容有个连带的小代价,咱们照实交代:三个旧测试文件里手搓的 `FakeSys`(模拟系统,替真系统在 host 上跑测试的那类)都得把 `arm_timer` 与 `cancel_timer` 补上,每个文件添了两行一模一样的转手代码。接口扩了,实现方也就得跟着补课了,concept 的纪律就是这么严。

demo 的按键实验也添了一个角色,咱们看任务 c 收到 `t` 的时候:

```cpp
        } else if (c == 't') {
            ZerOS::board::print("c: timer armed, 300ms\r\n");
            g_demo_timer.oneshot(Milliseconds{300}, timer_ping, nullptr);
        }
```

咱们看这一段布防:一行就完事了,任务 c 打完了招呼转身继续跑它自己的循环,三百毫秒之后 `timer_ping` 在 tick 的上下文里开腔。等待的全程里,c 该干嘛干嘛,别的任务也没被打扰,这就是标题承诺的效果。

## host 考场:六个用例对证定时器

定时器的行为是纯逻辑,咱们把 host 当考场正合适。考卷是写好的 `test/test_timer.cpp`,170 行的长度,装了六个 `TEST_CASE`,十一断言的阵仗。它们守的各是一条要害,咱们挑三个看。

咱们挑的头一个管的是单次的准点,它连早一拍都不肯放过的:

```cpp
TEST_CASE("oneshot fires exactly on time, not a tick earlier", "[timer]") {
    World w;
    Tmr t;

    t.oneshot(Milliseconds{5}, kCounting, nullptr);
    w.time.on_elapsed(4);
    CHECK(g_fires == 0); // one tick early: silence
    w.time.on_elapsed(1);
    CHECK(g_fires == 1); // right on the deadline
}
```

咱们布了 5 拍的防,拨了 4 拍过去,这时候的计数得是零,再拨了 1 拍,这回的计数得是 1。边界卡得这么死是有道理的,到期队列的排序与回绕的判断在《一秒钟的定义权》里是咱们一笔一笔对过号的,定时器的准点说的就是那套判断,早一拍响跟晚十拍响在咱们眼里一样是错。

咱们看的第二个,是标题里那个竞态的专测:

```cpp
TEST_CASE("a periodic may stop ITSELF from inside its own fn", "[timer]") {
    World w;
    Tmr t;

    constexpr auto self_destruct = +[](void* self) {
        ++g_fires;
        static_cast<Tmr*>(self)->stop(); // suicide note, honored
    };
    t.periodic(Milliseconds{4}, self_destruct, &t);

    w.time.on_elapsed(4);
    CHECK(g_fires == 1);
    w.time.on_elapsed(40); // dead by its own hand: no further fires
    CHECK(g_fires == 1);
}
```

回调带上了它自己的地址,执行到了一半把自己 stop 掉,注释把它写成了一句遗言,遗言是被尊重的。断言验了两个时刻:第一轮它确实响过,跟着拨了 40 拍过去,计数是纹丝不动的。咱们在源码那段讲的机制,清零的事在锁下完成,检查排在了清零之后,在这里被正对着验了一遍。要是重挂的分支没被堵住,40 拍的时间足够它再响十轮,计数早就不止 1 了。

咱们看的第三个,管的是冷了的定时器还能不能再用:

```cpp
TEST_CASE("a cold oneshot can be armed again", "[timer]") {
    World w;
    Tmr t;

    t.oneshot(Milliseconds{2}, kCounting, nullptr);
    w.time.on_elapsed(2);
    CHECK(g_fires == 1);

    t.oneshot(Milliseconds{9}, kCounting, nullptr); // second life
    w.time.on_elapsed(9);
    CHECK(g_fires == 2);
}
```

响完冷却的定时器,再布了一回防,它得像新的一样干活,注释给它起的名字是第二春。定时器是能复用的物件,用完了布第二回就是了,这个用例把复用的语义定了下来。剩下的用例里,咱们的目光还扫过了“到点之后时间继续走、它不再响”的冷态验证、“stop 把登记划掉之后 50 拍内一声不吭”的幂等验证、周期连响三轮每轮准点的验证,我不逐个念了,您跑一遍就全见得着了。

咱们看 host 全场的成绩单,它是真实的:

```text
100% tests passed out of 12

Total Test time (real) =   0.22 sec
```

定时器自己的成绩单,咱们看:

```text
All tests passed (11 assertions in 6 test cases)
```

我本机的 GCC 16.2.1,六个用例十一断言的阵仗,一个断言都没有被咱们动过。

::: warning
回调跑在 tick 的上下文里,而且此刻时间内核的临界区还握着,板子上中断是被掩着的。所以 `fired` 里执行的用户代码必须短,咱们打一行日志、置一个标志都是合格的,敢在里面睡 200ms 的活儿不是它的,那类重活请照《中断只递条子》的路数递给 worker,worker 有自己的任务上下文,睡的是自己的觉。定时器管的是提醒,代办归 worker 的队列管。
:::

## Renode 验收:按 t 挂三百毫秒

我自己复现的链路照旧:拿 `git archive 766c462` 把树导出到 /tmp 的副本,真实的 STM32CubeF1 从本机的 third_party 拷进副本(它在仓库里只是一段引用的文字,归档里是空的),host 侧的构建命令与成绩单就是上面那两段。板子那一侧的活,交叉的工具链是 arm-none-eabi-g++ 16.2.0,用的两条命令与上一篇是一样的:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

体积是真实的,咱们看:

```text
Memory region         Used Size  Region Size  %age Used
           FLASH:        5440 B        64 KB      8.30%
             RAM:        2880 B        20 KB     14.06%
   text	   data	    bss	    dec	    hex	filename
   5220	     220	   2656	   8096	   1fa0	.../zeros-bluepill.elf
```

flash 从上一篇的 5152 字节涨到了 5440 字节,定时器加回调的家当全住了进去,距离 D12 的 6KB 预算线还剩约 700 字节。预算还守得住的,不过空间矮得能听见天花板了,我照实报数。

Renode 的部分,经验我照实再说一遍:我本机的 Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 类型,我在副本里把 i2c1、i2c2 两个节点连着中断接线整块注释掉了,仿真就起来了,demo 并不经过 i2c 的路径,行为是不受影响的。注入按键走的还是监控器的 `WriteChar`:等仿真跑了起来,我从监控器往 usart1 的接收侧喂了一个 `0x74`,它也就是字符 t 的编码。串口窗的真实输出如下,行数多的 M、L、H 几个搭戏的角色我只抽了跟定时器挨着的几行,它们在原始的日志里一行都没有缺席:

```text
[INFO] usart1: [host:  2.13s (+0.32ms)|virt:       2s (+3.58µs)] L: locked, busy 300ms
[INFO] usart1: [host:  2.64s (+0.34s)|virt:     2.3s (+0.2s)] L: unlocked
[INFO] usart1: [host: 2.64s (+0.37ms)|virt:     2.3s (+3.49µs)] c: timer armed, 300ms
[INFO] usart1: [host:  2.64s (+0.36ms)|virt:     2.3s (+6.09µs)] H: LOCKED (inheritance paid off)
[INFO] usart1: [host: 2.65s (+11.07ms)|virt:    2.4s (+99.98ms)] M: running
[INFO] usart1: [host:  2.66s (+8.57ms)|virt:      2.5s (+100ms)] M: running
[INFO] usart1: [host: 2.73s (+65.94ms)|virt:      2.6s (+100ms)] timer: fired (tick context, no task spent)
[INFO] usart1: [host:  2.83s (+99.5ms)|virt:    2.7s (+99.99ms)] M: running
[INFO] usart1: [host:    3.13s (+0.1s)|virt:         3s (+0.1s)] A
```

您看 virt 那一列,验收的事实全在里面:我在虚拟时间 2.3 秒喂进了 t,任务 c 立刻报了 “c: timer armed, 300ms”,然后它转身就走了,2.5 秒的 M 照常跑着,到了 2.6 秒,正好是 2.3 加 0.3 的和,`timer_ping` 报了 “timer: fired (tick context, no task spent)”。三百毫秒的间隔,一拍的误差都是没有的。最值钱的是布防与开腔之间那几行:M 在 2.4 秒、2.5 秒各跑了一轮,A 在 3 秒也照常打了字母,等待的三百毫秒里系统连一个节拍都没乱。任务 c 布完防去干别的了,咱们看见的这股从容,就是标题留在板上的实物。

## 验收

本篇的可查证事实归拢为四件,咱们一件一件过。

头一件是 host 的考场。您 checkout 到 `766c462`,照着它的命令配置构建,`ctest` 该给您的成绩是十二个目标全部通过,`test_timer` 的成绩单应是六用例十一断言的阵仗。

第二件是三个旧测试的扩容痕迹。`test_sync.cpp`、`test_queue.cpp`、`test_mutex.cpp` 各自的 `FakeSys` 里,都补上了 `arm_timer` 与 `cancel_timer` 两行转手的代码,它们是 `SystemContext` 添了两个要求之后跟着补的产物,您在 diff 里一眼就能对上号。

第三件是板上的按 t 实验。交叉构建后跑起 Renode(i2c 两个节点要照我说的注释掉,或者等上游补了 F1 的 I2C 模型),等仿真跑了起来,您往 usart1 喂一个 `t`,您该看到 “c: timer armed, 300ms” 的报行,隔了整的三百毫秒,“timer: fired (tick context, no task spent)” 就开腔了,而且中间隔着的虚拟时间里,M、A、B 这些角色的一拍也没有乱。

第四件是竞态专测的对源。`fired` 的重挂分支判断的是 `period_`,`stop` 在临界区里把 `period_` 清了零,回调内自杀的用例靠这个机制绿着,您要是把 `fired` 里检查 `period_` 的位置挪到调用用户回调的面前(等于检查完再执行),用例就得变红了,机制经得起您动手改。

## 收尾

到点了提醒一声,这件事的等待没有任务、没有堆,身上只有三个标志位加一个内嵌的等待者,回调的形状是 fn 加 `void*`,跟工作者队列里的重活是同一枚硬币。咱们本站的三件套到这里合卷了:《芯片死前立遗嘱》管崩了之后的现场,《中断只递条子》管的是中断把重活卸给谁,定时器管免任务的等待。应用想在自己的内核上站住脚,要的就是看得见、卸得下、等得起,三件事就都有了着落,下一站咱们接着往用户面走。
