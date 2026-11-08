---
title: "睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链"
description: "6308dd0 给上一站手搓 task_sleep 的丢唤醒竞态收口:我在 /tmp 副本里把定时器到点摆进布防与阻塞之间的窗口,亲眼看着任务睡了过去;park_for 把布防与阻塞收进同一临界区,类型状态建造者链让跳步直接编不过,friend 钥匙孔收走了 TCB 的写权,placement new 成了唯一的出生方式;include 路径的笔误照 5c62c0d 的答案改掉之后,重写过的 host 调度测试七个目标全部通过"
chapter: 3
order: 3
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
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "CPU 归谁:从就绪位图到中断唤醒"
---

# 睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链

上一篇收尾的时候,我把雷埋好了:example 里手搓的 `task_sleep` 是三步分离的,设了回调、布了防、阻塞了任务,三步之间没有任何的锁,定时器要是正好在布防之后、阻塞之前到了点,任务就睡了过去,再也没有人叫它了。其实仲裁面的文章还留过更早的一句承诺,咱们讲 `ready` 的时候说过,它的开头只收 Blocked 态的任务,没睡的任务来 `ready` 一律不理,而被它挡掉的唤醒里恰好藏着一个真正的丢唤醒竞态,当时我说了,要当着它的面复现出来再修掉。今天咱们就兑掉这两笔:把竞态当着大家的面复现了出来,随后把它修掉了,写错的可能性也被类型系统一并收掉了。

咱们要拆的 commit 是 `6308dd0`,subject 写的是 “optimized: better sched explanation type”,交了 13 个文件,新增了 418 行,删除了 188 行。您看到 optimized 可别急着当成性能优化,它的实际成色咱们照实摆出来:一半是类型表达力的重做,字段全私有化了,出生的方式也定了。另一半是真 bug 的修复,`park_for` 把布防与阻塞收进了同一临界区,SVC 首切补了 `movs r0, #0` 这一句,host 测试桩也补上了前面补空函数的那篇里咱们亲手补过的那对空函数。标题里的“类型”说的是主体,可标题没说的活儿,它也统统干了,咱们后面都会亲眼见到。

## 窗口长什么样:三步分离的 task_sleep

咱们把案发现场摆回桌面上,代码是上一篇贴过的 `task_sleep`,它住在 0bf9461 的 `example/main.cpp` 里:

```cpp
void task_sleep(std::uint32_t ms) {
    auto cur = system_sched.current_task();
    cur->action.OnTime = wake;
    system_time.call_after_span(&cur->action, ms);
    system_sched.block(cur);
}
```

三步的分工您已经熟了,咱们快速过一遍:设回调,它把唤醒的动作挂在了 TCB 内嵌的 TimeWaiter 身上。`call_after_span` 管的是布防,它把等待者登记进了时间内核,还给硬件定时器布了防。最后的 `block` 把任务标成了 Blocked,顺便请求了切换。麻烦的地方在于,三步各自是独立的调用,中间没有任何的临界区罩着,而布防一生效,定时器到点的时候咱们就拦不住了。

咱们顺着最坏的时间线推一遍。布防刚做完的时候,任务还处在没被 `block` 阻塞的状态, SysTick 就打了进来,`on_elapsed` 发现了到期的等待者,回调 `wake` 就跑了起来,它调了 `ready`,想把任务放回就绪的队列。可 `ready` 的开头有判断,咱们在仲裁面那篇读过的:

```cpp
    void ready(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t || t->state_ != TaskState::Blocked) {
            return;
        }
        push_back(t.get());
        ...
```

可任务此刻还跑在 CPU 的上面,它的状态还是 Running,Blocked 的状态它还没有拿到,`ready` 转身就走了,这次的唤醒无声无息地丢了。中断退出了之后,`task_sleep` 接着执行它的第三步,`block` 把任务标成了 Blocked,任务睡下了。时间队列里的到期记录倒还挂着,可是它已经被弹掉了,再也不会有人来调 `ready` 了,任务就这么睡到了天荒地老。您留意丢唤醒的成立条件,它恰恰依赖 `ready` 的谨慎:它不收没睡的任务,这是对的,不然重复的唤醒会把队列搅乱,错的不是它,错的是布防与阻塞之间留出来的窗口。

## 复现:让定时器正好落在窗口里

板子上的窗口是硬件的时序挤出来的,碰上的概率全凭运气,咱们赌不起,而 host 上咱们可以把它变成确定性的:我在复现的程序里把 `on_elapsed` 的调用摆在了布防与阻塞之间,这就等于替 SysTick 选好了它打进来的时刻,交错被咱们亲手摆好了。

复现的场地还是 /tmp 副本的老路子,gitfile 形式的 .git 咱们拿 `git archive` 绕开,我把 0bf9461 的树导出到了 /tmp/zeros-t0bf9-repro,再写了一个 `lost_wakeup_repro.cpp`,里面照着 example 的原样手搓了三步,只多了一行夹在窗口中间的 `tk.on_elapsed(5)`。符号的出处咱们交代一下,免得您读着迷路:`tk` 是时间内核 `Kernel` 的对象,`sched` 是包着替身端口的调度器,`kWake` 对应的就是 example 里那个 `wake` 回调:

```cpp
    sched.add(&tcb);
    assert(sched.pick_next().get() == &tcb);

    // the hand-rolled task_sleep of the 0bf9461 example, three separate steps
    auto* cur = sched.current_task().get();
    cur->action.OnTime = kWake;          // step 1: set the reaction
    tk.call_after_span(&cur->action, 5); // step 2: arm the timer
    // ---- the window: on the board SysTick fires right here ----
    std::printf("-- tick 5 fires inside the window --\n");
    tk.on_elapsed(5);                    // wake runs: task is still Running
    std::printf("task state after the dropped wake: %d (Blocked=%d, Running=%d)\n",
                static_cast<int>(tcb.state_), static_cast<int>(TaskState::Blocked),
                static_cast<int>(TaskState::Running));
    // ---- end of window ----
    sched.block(cur);                    // step 3: block, now it sleeps

    // wait far past the deadline: nobody will ever call ready again
    for (int i = 0; i < 1000; ++i) tk.on_elapsed(1);
```

`wake` 回调还是 example 里的那副骨架,它从等待者那里用 `offsetof` 反推回了 TCB,再调了 `ready`,咱们上一篇讲过它的把戏,这里就不重复了。任务睡下了之后,我又让它空转了一千个 tick,这个数字远远超过了五 tick 的期限,我要看的就是有没有人来叫它。编译跟运行的命令就两行,我本机的 GCC 16.2.1 直接编了它:

```shell
g++ -std=c++23 -Iinclude lost_wakeup_repro.cpp -o /tmp/lost_wakeup_repro
/tmp/lost_wakeup_repro
```

跑出来的真实输出如下,您看好了:

```text
-- tick 5 fires inside the window --
wake fired: ready() called
task state after the dropped wake: 1 (Blocked=2, Running=1)
after 1000 more ticks: state=2 (0=Ready 1=Running 2=Blocked)
LOST WAKEUP REPRODUCED: task sleeps forever
```

咱们逐行读它:唤醒确实跑了,`ready` 也确实被调了,可任务的状态打出来是 1,它对应的是 Running,唤醒在状态检查那里被挡了回去。等阻塞执行完了,任务睡成了 Blocked,打出来的是 2。之后一千个 tick 过去了,状态纹丝不动地停在了原地,它睡死了。您在自己的机器上跑同样的程序,窗口摆在了哪里,睡死就在哪里发生了,这就是 0bf9461 留在 example 里的真实缺陷,而 6308dd0 修的就是它。

## park_for:布防与阻塞收进同一把锁

修法的思路一句话就说得完:让布防与阻塞之间不存在窗口。6308dd0 在时间内核上加了一个 `park_for`,咱们看 `include/ZerOS/kernel/clock/kernel.hpp`:

```cpp
    // Parking: arm AND block inside ONE guard. A wake slipping in between
    // the two steps would find the task un-blocked, drop it, and the task
    // sleeps forever — the order here IS the fix
    template <typename ParkBlock>
    void park_for(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                  TimeWaiter::OnTimeAction wake, ParkBlock&& block_now) {
        irq::CriticalGuard guard{lowlevel_driver_};
        enqueue_locked(task, span, wake);
        block_now(); // still under the very same lock
    }
```

注释把整个竞态写在了代码里,连结局都写了,`sleeps forever`。咱们看它的结构,`park_for` 接的是四样东西:等待者、期限、唤醒的回调,外加一个可调用的 `block_now`,它封装的其实是“现在就把任务阻塞掉”的动作。函数体里的两步被同一个 `CriticalGuard` 罩住了,布防的活儿交给了 `enqueue_locked`,阻塞的活儿交给了 `block_now()`,注释里的 `still under the very same lock`,说的就是此时锁还握在手里的状态。窗口为什么会消失,咱们把中断的视角代进去想一想:临界区开着的时候,中断是进不来的, SysTick 就没有可能落在布防与阻塞的中间。等临界区放开的时刻,两步都已经做完了,任务要么已经睡了、唤醒正常地登记了,要么还没布防,中断根本就轮不到插手的份。“the order here IS the fix” 咱们可以这么读:修复靠的就是两步被同一把锁罩住的顺序,布防与阻塞再也没有各自单独暴露的时刻,窗口自然就关上了。

咱们接着看下一个函数,`enqueue_locked` 是从原来的 `call_after_span` 里抽出来的私有函数,它设了回调、写了期限、把等待者插进了队列、还更新了闹钟,原来的一套原封未动,只是改了名,还明确地标注了调用者必须握着锁:

```cpp
    // caller must hold the guard; wake is optional (nullptr keeps the born-with reaction)
    void enqueue_locked(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                        TimeWaiter::OnTimeAction wake) {
        if (wake != nullptr) {
            task->OnTime = wake;
        }

        // set the deadline
        task->deadline_ = clock_.current() + span;
        queue_.insert(task);
        update_alarm();
    }
```

调度器的那一边配了一个成员函数,它把整件事串了起来,咱们看 `include/ZerOS/kernel/sched/scheduler.hpp`:

```cpp
    // Park the task on the clock: arm and block share ONE guard, so a
    // too-early wake always finds us already blocked — the wake then
    // lands, it never gets dropped. Hand-rolling this from call_after_span
    // + block reopens the gap (arm, wake, block, sleep forever).
    template <clock::TimeKeeper Time>
    void sleep_for(base::BorrowedPtr<TCB> t, Time& time, clock::Ticks::tick_t span,
                   clock::TimeWaiter::OnTimeAction wake) {
        time.park_for(&t->action, span, wake, [this, t] { block(t); });
    }
```

注释里的 `Hand-rolling this ... reopens the gap` 是写给未来的读者的警告:谁要是绕开了它,自己拿 `call_after_span` 加上 `block` 拼了一遍,窗口就重新打开了,注释把死亡的顺序 arm、wake、block、sleep forever 都排好了,我在正文里的复现走的正是同一条路。lambda 捕获了 `this` 和任务的指针,`park_for` 布防完了就地调它,阻塞发生在锁还握着的时候。调用链的末端在 arch 层,`src/arch/arm_cortex_m3/switch.cpp` 里添了一个 `sleep_for` 的自由函数,它把两个单例接在了一起:

```cpp
namespace {
void wake_from_sleep(base::BorrowedPtr<clock::TimeWaiter> waiter) {
    system_sched.ready(sched::TCB::owner_of(waiter));
}
} // namespace

void sleep_for(std::uint32_t ms) {
    system_sched.sleep_for(system_sched.current_task(), system_time, ms, wake_from_sleep);
}
```

`wake_from_sleep` 就是 example 里那个 `wake` 的官方版,`owner_of` 也升格成了 TCB 自己的静态成员,咱们到钥匙孔的节里再回头看它。example 里的三步手搓整段删除了,任务体里只剩了一行 `sleep_for(1000)`。

::: warning
`park_for` 堵的是布防与阻塞之间的窗口,它并不改变唤醒的语义:唤醒的回调依然是 `ready`,同级的唤醒照样只入队而不请求切换,期限依旧按当前的时刻往后加,抖动的来源跟上一站一致。您拿它跟 FreeRTOS 的 `vTaskDelay` 对照的话,相同的点是复合的原子性,不同的点是这里没有任何按绝对时刻对齐的约定。
:::

仓库自己给这个修复配了回归测试,咱们能在 `test/test_sched.cpp` 里找到一个用例,它专门盯着零期限的睡眠:

```cpp
TEST_CASE("a zero-span sleep still wakes: no gap between arm and block", "[sched][time]") {
    // regression: hand-rolled sleep (arm, then block) loses a wake that
    // fires in between and the task sleeps forever. The compound must
    // not have that gap, not even for span 0.
```

注释说得很诚实:手搓版连 span 为 0 的睡眠都会翻车,而复合版连 span 为 0 都不许有窗口。零期限是最极端的情形,期限越短的时候,到点和阻塞挨得就越近了,手搓版就越容易翻车了,而复合版无所谓,因为它的两步之间根本就不存在可被插入的间隙。这个用例咱们在验收的段落里跑给它看。

## 建造者链:跳一步就编不过

修完了竞态,6308dd0 转过头来收拾了另一件更隐蔽的事,咱们也跟着它看:TCB 的字段谁都能写。上一站的代码里,example 逐个地给 `tcb_a.task_priority_`、`tcb_a.name_` 赋了值,字段全是公开的,顺序写错了、漏写了一个,而编译器一声不吭,炸点就被推到了运行时。6308dd0 给出的回答分了三层:最外面的一层是类型状态的建造者链,中间的一层是 placement new 的出生仪式,最里面的一层是收走字段写权的 friend 钥匙孔,咱们一层一层看。最外面的一层是新的 `include/ZerOS/kernel/sched/task.hpp`,它是一个类型状态的建造者链:

```cpp
#pragma once

// The task factory: the chain IS the contract. Skip a step and the rest
// of the chain simply does not exist — no comment can save you then.
//   named("blink").prio(1).stack(stack).entry(fn, arg).spawn_into(box)

#include "ZerOS/kernel/sched/task_control_block.hpp"
#include "ZerOS/kernel/sched/types.hpp"

#include <concepts>
#include <cstdint>
#include <span>

namespace ZerOS::task {

using sched::TaskPriority_t;

// stages: each struct is what the chain knows so far, nothing more
struct Named {
    const char* name;
};
struct Priorized {
    const char* name;
    TaskPriority_t prio;
};
struct Stacked {
    const char* name;
    TaskPriority_t prio;
    std::span<std::uint32_t> stack;
};
```

注释里的 `the chain IS the contract` 是整个设计的宣言:链条本身就是合同,注释救不了跳步的人。咱们再看三个阶段的结构体,它们把链条到此刻为止知道了什么这件事,逐级编进了类型的身上:Named 只有一个名字,Priorized 加了优先级,Stacked 再加了栈。链条每往前走了一步,类型的身上就多出一个成员,类型就是咱们的进度条。

转移函数才是真正的卡口,咱们看 `TaskBuilder` 的身体:

```cpp
template <typename Stage>
struct TaskBuilder {
    Stage stage;

    explicit TaskBuilder(Stage filled) : stage(filled) {}

    // one road per stage: asking for a step from the wrong spot is a
    // compile error, exactly the point of this whole dance
    TaskBuilder<Priorized> prio(TaskPriority_t value) && requires std::same_as<Stage, Named> {
        return TaskBuilder<Priorized>{Priorized{stage.name, value}};
    }

    TaskBuilder<Stacked> stack(std::span<std::uint32_t> body) && requires std::same_as<Stage, Priorized> {
        return TaskBuilder<Stacked>{Stacked{stage.name, stage.prio, body}};
    }

    // entry carries its argument with it: "an arg without an entry"
    // is not a state the chain can be in
    sched::TCBCreator entry(sched::UserTaskWrapper::UserTask function, void* argument) &&
        requires std::same_as<Stage, Stacked> {
        return sched::TCBCreator{
            .name = stage.name,
            .prio = stage.prio,
            .stack = stage.stack,
            .run = {function, argument},
        };
    }
};

// the road starts here
[[nodiscard]] inline TaskBuilder<Named> named(const char* name) {
    return TaskBuilder<Named>{Named{name}};
}

} // namespace ZerOS::task
```

每个转移函数的身上都带着两道闸,咱们把闸的道理说明白。`&&` 限定符要求调用者拿的是右值,链条只能顺着返回值的路径一路点下去,存在中间变量里的左值是调不动它们的。`requires std::same_as<Stage, Named>` 这样的约束,把某个函数只准从哪个阶段出发的限定,写成了编译期的事实:`prio` 只认 Named,`stack` 认的只有 Priorized,`entry` 认的只有 Stacked。跳步的人拿到的是硬邦邦的编译错误,注释里的 `an arg without an entry is not a state the chain can be in` 顺手解释了为什么 entry 连参数一起收:函数和实参是一对,拆散了,链条就处在一种不该存在的状态里。

错误长什么样,我给您编了一个真实的小试验,`named("a")` 之后直接点了 `stack`,把 `prio` 跳了过去:

```cpp
int main() {
    alignas(8) static std::uint32_t stack[64] = {};
    static ZerOS::sched::TCBStorage storage{};
    // skips prio(): straight from named() to stack()
    auto& t = ZerOS::task::named("a").stack(stack).entry(+[](void*){}, nullptr).spawn_into(storage);
    (void)t;
    return 0;
}
```

GCC 16.2.1 的报错原文如下,咱们一起读它:

```text
/tmp/skip_step.cpp:8:44: error: no matching function for call to 'ZerOS::task::TaskBuilder<ZerOS::task::Named>::stack(uint32_t [64])'
    8 |     auto& t = ZerOS::task::named("a").stack(stack).entry(+[](void*){}, nullptr).spawn_into(storage);
      |               ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~
  • there is 1 candidate
In file included from /tmp/skip_step.cpp:2:
  • candidate 1: 'ZerOS::task::TaskBuilder<ZerOS::task::Stacked> ZerOS::task::TaskBuilder<Stage>::stack(std::span<unsigned int>) && requires  same_as<Stage, ZerOS::task::Priorized> [with Stage = ZerOS::task::Named]'
      include/ZerOS/kernel/sched/task.hpp:44:26:
```

报错把道理自己讲了:唯一的候选写着约束 `same_as<Stage, Priorized>`,而您此刻拿在手里的 Stage 恰恰就是 Named 这个类型的形状。跳步不再是一个深埋的运行时 bug,它是第一眼就能看懂的编译错误,“让错误用法编不过”说的就是这个意思。

合法的走法落在了 `example/main.cpp` 里,咱们看改版之后的出生段:

```cpp
// zero storage, meaning comes only from the factory chain
constinit ZerOS::sched::TCBStorage tcb_a{};
constinit ZerOS::sched::TCBStorage tcb_b{};

    auto& a =
        ZerOS::task::named("a").prio(1).stack(stack_a).entry(task_a, nullptr).spawn_into(tcb_a);
    auto& b =
        ZerOS::task::named("b").prio(1).stack(stack_b).entry(task_b, nullptr).spawn_into(tcb_b);
```

上一站里逐字段赋值的写法没有了,如今的每一行读起来就是任务的简历:叫什么、多急、栈在哪里、干什么。`TCBStorage` 是新的类型,它是出生仪式的舞台,咱们接着看。

## 出生仪式:placement new 与四条核对

出生仪式的舞台 TCBStorage,它的定义与守卫都住在 `task_control_block.hpp` 里,咱们一起看:

```cpp
// Meaning-free RAM: zero it, park it. The only pen that writes meaning
// into it is TCBCreator::spawn_into, so a half-built task is not a thing.
struct TCBStorage {
    alignas(alignof(TCB)) std::byte bytes[sizeof(TCB)];
};

// The factory's final word: every field is in, now it may write a TCB.
// The chain fills it, spawn_into spends it.
class TCBCreator {
  public:
    // The one legal pen: fresh zero bits + meaning, hands back the handle
    TCB& spawn_into(TCBStorage& storage) const {
        auto& tcb = *new (storage.bytes) TCB{}; // private birth, but we hold the pen
        tcb.name_ = name;
        tcb.task_priority_ = prio;
        tcb.task_wrapper_ = run;
        tcb.stack_view_ = stack;
        return tcb;
    }

    // filled by the builder chain, nothing to hide in a receipt
    const char* name{};
    TaskPriority_t prio{};
    std::span<std::uint32_t> stack{};
    UserTaskWrapper run{};
};
```

咱们看 `TCBStorage`,它是一段裸的字节,注释说得够直白的,`meaning-free RAM`,零化了它、放着它。字节本身是没有意义的,唯一往里写意义的笔是 `spawn_into`,所以“造了一半的任务”这样的东西在设计上就不存在。`spawn_into` 里那行 `new (storage.bytes) TCB{}` 就是 placement new(在给定的地址上构造对象、而不分配内存的 new 用法),它在这套代码里的角色是出生仪式:对象被正式地构造了出来,而非被人赋值拼凑。为什么非要这么办?因为 TCB 的字段马上要全私有化,而 TCB 又有内嵌成员与对齐的讲究,咱们拿裸字节存着、到点就地构造,对齐的事由 `alignas(alignof(TCB))` 担着,出生的事由构造函数担着,两条腿就都齐了。

文件的末尾有一组 `static_assert`,咱们把它们全看一遍:

```cpp
static_assert(std::is_trivially_copyable_v<TCB>);
static_assert(std::is_trivially_destructible_v<TCB>);
static_assert(sizeof(TCBStorage) == sizeof(TCB));
static_assert(alignof(TCBStorage) == alignof(TCB));
```

咱们逐条看它们对着什么现实。头两条对应的是 PendSV 的现实:切换的汇编按原始的字拿捏 TCB,栈指针的保存恢复从来不过析构函数,所以 TCB 必须可平凡复制、可平凡析构,断了其中任何一条,汇编路径的前提也就断了。后两条管的是存储与本体的大小对齐一致,`spawn_into` 往字节里构造的时候,容不得半点的错位。有意思的是挨着的那句注释,仓库自己招了半句实话:TCB 的默认构造并非 trivial(libstdc++ 的 span 默认构造会初始化自己的成员),不过没关系,出生只走 `spawn_into` 的 placement new,零字节的本身从来不会被当成 TCB 用。每一条断言的背后都有一段会炸的汇编,或者一次会错位的构造。

## friend 钥匙孔:写权收归三把钥匙

现在咱们看封装的本体,TCB 从公开的结构变成了带锁的房子:

```cpp
class TCB : public base::SelfNode<TCB> {
  public:
    using StackPointer_t = std::uint32_t*;

    // Wait... who owns this waiter? The node sleeps inside its owner,
    // waking walks the road back from node to TCB
    [[nodiscard]] static TCB* owner_of(base::BorrowedPtr<clock::TimeWaiter> waiter) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Winvalid-offsetof"
        return reinterpret_cast<TCB*>(
            reinterpret_cast<std::byte*>(waiter.get()) - offsetof(TCB, action));
        #pragma GCC diagnostic pop
    }

  private:
    // Locked fields: walkers talk to the scheduler or the creator chain.
    // Naked mechanics (the PendSV road, whitebox tests) pass through TCBKeys.
    // A comment contract cannot stop anyone, the type system can.
    template <typename> friend struct Scheduler;
    friend class TCBCreator;
    friend struct zeros_impl::TCBKeys;

    // Birth stays public (the triviality contract needs it) but a born TCB
    // holds zero bits and no pen: only the three names above can write it.
    // Everyone else gets a TCBStorage and hands it to the factory chain.

    StackPointer_t stack_pointer_{}; // always be the first
    TaskPriority_t task_priority_{};
    TaskState state_{};

    const char* name_{}; // Constant name's for debugings

    clock::TimeWaiter action{};
    UserTaskWrapper task_wrapper_{};
    std::span<std::uint32_t> stack_view_{};
};
```

注释里的 `A comment contract cannot stop anyone, the type system can` 是本篇我最想让您带走的一句话:注释约束不了任何人,类型系统却是可以的。字段全私有化了,写的权力只发给了三个 friend,咱们把三把钥匙的分工念一遍:`Scheduler` 模板管运行时的状态迁移,`TCBCreator` 管的是出生,`zeros_impl::TCBKeys` 管的是裸的机制。`owner_of` 从 example 里的黑科技升格成了公开的静态成员,反正它是只读的,摆在明面上大家用着方便,写 `offsetof` 那个把戏的人再也不用自己 `reinterpret_cast` 了。

`TCBKeys` 是给裸机制开的钥匙孔,咱们看它在命名空间里的预告:

```cpp
namespace zeros_impl {
struct TCBKeys; // the keyhole, declared early so the lock knows its guard
}
```

`TCBKeys` 的全部身体如下,咱们接着看:

```cpp
namespace zeros_impl {
// The keyhole for naked mechanics: the PendSV path and the whitebox tests.
// If you are neither, you are just walking by, please use the scheduler.
struct TCBKeys {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(TCB, stack_pointer_) == sizeof(base::SelfNode<TCB>),
                  "stack_pointer_ must stay immediately after the SelfNode base");
    #pragma GCC diagnostic pop

    static TCB::StackPointer_t& sp(TCB& t) { return t.stack_pointer_; }
    static UserTaskWrapper& wrapper(TCB& t) { return t.task_wrapper_; }
    static std::span<std::uint32_t>& stack_view(TCB& t) { return t.stack_view_; }
    static TaskPriority_t prio(TCB& t) { return t.task_priority_; }
    static TaskState state(TCB& t) { return t.state_; }
    static void set_state(TCB& t, TaskState s) { t.state_ = s; }
    static const char* name(TCB& t) { return t.name_; }
};
} // namespace zeros_impl
```

注释把准入写清楚了:PendSV 的路与白盒的测试,二者之外的人都算路过,路过的请走调度器的正路。仲裁面那篇守着栈指针布局的 `static_assert` 搬进了钥匙孔里,守门的位置跟用门的人放在了一处。`switch.cpp` 里原来的裸字段访问全改成了钥匙孔的形状,咱们对比看 `fabricate_frame` 与 `context_switch` 的改法:

```cpp
void fabricate_frame(ZerOS::sched::TCB& t) {
    auto view = sched::zeros_impl::TCBKeys::stack_view(t);
    auto* top = view.data() + view.size();
    auto* hw = reinterpret_cast<ExceptionFrame*>(top) - 1;
    auto* sw = reinterpret_cast<SoftwareFrame*>(hw) - 1;
    ...
    TCBKeys::sp(t) = reinterpret_cast<std::uint32_t*>(sw);
}

extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    if (saved_sp != nullptr) {
        TCBKeys::sp(*system_sched.current_task()) = saved_sp;
    }
    return TCBKeys::sp(*system_sched.pick_next());
}
```

汇编的路径照旧按裸字干活,C++ 的封装没有拖慢它一根指头,而应用的代码再也摸不到 `stack_pointer_` 了。封装的边界与性能的边界各归各的位置,这是嵌入式 C++ 里少见的两全写法,咱们往后写自己的内核时值得照着学。

字段的私有化还往两头各延了一截,咱们把两头都看一眼。一头是 `SelfNode` 的 `next_`,它在 `include/ZerOS/base/self_list.hpp` 里也私有化了,注释写得够凶的,`a stray hand wiring nodes behind our back is how ready queues die quietly`,谁背地里乱接了链节,就绪队列就悄悄地死掉了。链的走动权收归了 `SelfList` 自己,连测试的代码都要改,`test_self_list.cpp` 里那句 `CHECK(a.next_ == nullptr)` 的白盒窥视删掉了,换成了注释 `the drain below proves re-insertion lands it back in order`,链表正不正常的问题,改由行为的断言来证明,咱们不再偷看它的内脏了。另一头是 `TimeWaiter` 的私有化,它的 `deadline_` 与 `OnTime` 也藏了起来,friend 只发给了 `Kernel` 与 `TimerQueue`,外面只留了一个只读的 `deadline()` 访问器,注释的对仗写在代码里,`reading is free for diagnosis, writing is not`。

编译期约束的呈现方式也在这个 commit 里换了一套,咱们看它换成了什么样。原来的写法把 concept 顶在了模板的形参上,`template <Switchable Driver> struct Scheduler`,约束不满足的时候,报错是一整页概念推导的栈。6308dd0 把形参上的 concept 拆了下来,改成了类内的 `static_assert`:

```cpp
template <typename Driver> struct Scheduler {
    // the picker only rides a Switchable driver
    static_assert(Switchable<Driver>, "Scheduler wants a Switchable driver");
```

时间内核是同款的做法,咱们把两处对照着看:`Kernel` 的形参从 `TimePortable Driver` 改成了 `static_assert(TimePortable<Driver>, "Kernel wants a TimePortable driver")`。约束还是原来的约束,报错变了,一行的人话换掉了一页的栈,API 的解释力本身也成了设计的对象。配套的 `TimeKeeper` concept 把时间内核对调度器承诺的能力固化了下来:`park_for` 与 `current`,缺了哪一个,`Scheduler` 的 `sleep_for` 就编不过。

伏笔也在这里兑现了。上一篇咱们坦白过,SVC 的首切路径上没有人清零 r0,`context_switch` 拿着残留值当了栈指针,没炸靠的是运气。6308dd0 的 `SVC_Handler` 补上了一行:

```cpp
extern "C" [[gnu::naked]] void SVC_Handler() {
    asm volatile("cpsid   i\n"
                 "movs    r0, #0\n" // first switch ever: nobody is running, nobody to save
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

`movs r0, #0` 把没有人需要保存的这件事,从运气变成了明示的 nullptr,注释 `nobody is running, nobody to save` 说的正是这个意思。上一篇咱们看着漏洞叹气,今天咱们看着它被一行指令填平,首切的路径从此没有未定义的行为。

## 坦白:重写过的测试又编不过了

照例有一段要照实交代的事。6308dd0 把 `test_sched.cpp` 重写了,232 行的 diff,测试的任务改走和应用同一条工厂路,咱们看测试里的 Rig,它是这些用例共用的测试夹具:

```cpp
// tasks are built the legal way: the same factory road the app walks
struct Rig {
    ZerOS::sched::TCBStorage storage{};
    TCB& tcb;

    explicit Rig(TaskPriority_t prio, const char* name)
        : tcb(named(name).prio(prio).stack(std::span<std::uint32_t>{})
                          .entry(kNoopEntry, nullptr)
                          .spawn_into(storage)) {}
};
```

字段全私有了之后,测试拿状态、改状态都走的 `TCBKeys`,文件顶部的注释写着 `whitebox key, tests only`。仲裁面那篇里编不过的 `HostSwitchPort` 这次自带了 `lock` 和 `unlock`,咱们不用再补了。

可是我 checkout 到了 `6308dd0`,照上一站用过的三条命令配好了 host 的构建,构建却在半路停了下来:

```text
/tmp/zeros-t6308/test/test_sched.cpp:8:10: fatal error: ZerOS/task.hpp: No such file or directory
    8 | #include "ZerOS/task.hpp"
      |          ^~~~~~~~~~~~~~~~
compilation terminated.
make[3]: *** [test/CMakeFiles/test_sched.dir/build.make:79: test/CMakeFiles/test_sched.dir/test_sched.cpp.o] Error 1
```

原因拿源码一对就现形了:新写的测试,它的第 8 行 include 的是 `ZerOS/task.hpp`,可咱们在仓库里压根就找不到这个文件,而建造者链住在 `include/ZerOS/kernel/sched/task.hpp`。我翻了一遍提交的历史,这个路径上从头到尾就没出现过什么头文件,include 写错的地方,要等两个 commit 之后的 `5c62c0d` 才修掉,官方的答案就一行:

```diff
-#include "ZerOS/task.hpp"
+#include "ZerOS/kernel/sched/task.hpp"
```

跟仲裁面那篇的故事几乎是同一个模子:提交信息写着 optimized,类型系统、竞态的修复、出生的仪式都齐了,可是重写的测试自己编不过,当天验证的多半又是板子,host 的绿灯在仓库的历史上是暂缺的。咱们想自己走通,就照 5c62c0d 的答案改这一行,重新构建了,再跑全部的测试:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机是 GCC 16.2.1 加 CMake 4.4.4,七个测试的目标全数通过了:

```text
100% tests passed out of 7

Total Test time (real) =   0.12 sec
```

调度测试的成绩单出来了,咱们在重写了之后看它,它给出的就是十个用例、四十五条断言,比仲裁面那篇的九个用例、四十二条断言多出一个零期限睡眠的回归用例,原有的睡眠用例也重写走了工厂路:

```text
All tests passed (45 assertions in 10 test cases)
```

## 验收

咱们把可查证的事实归拢归拢,一共归出了三件,我一件一件摆给您。

第一件是睡死的复现。您把 0bf9461 用 `git archive` 导到 /tmp,照正文的样子写了复现的程序,把 `on_elapsed` 夹在了布防与阻塞之间,您会看到唤醒确实跑了、状态确实还是 Running、一千个 tick 之后依然 Blocked,四行的输出跟我贴的一字不差。您再把窗口里的那行 `tk.on_elapsed(5)` 挪到 `block` 之后,程序就会打出 `task woke up` 的字样,同一个缺陷换个时序就隐了身,这正是它难抓的原因。

第二件是类型的卡口。跳步试验的报错原文在正文里,`same_as<Stage, Priorized>` 那一行候选信息就是编译器替咱们宣读的限制。私有成员的探针试验也给您留了,您对着 6308dd0 的头文件写 `t.action.OnTime = nullptr`,GCC 给您的报错是 `'...TCB::action' is private within this context`,除了三把钥匙的持有者,谁也写不进去一个字节了。改回工厂链的合法走法,任务就正常地出生了,我在 host 上跑通了 `named("a").prio(1).stack(...).entry(...).spawn_into(storage)` 的全链。

第三件是修复后的全部通过。include 改对了之后,`ctest` 的七项就全过了,调度测试给出的就是十个用例、四十五条断言,其中零期限睡眠的回归用例就是丢唤醒修复的官方证人,它跑过了,复合睡眠的窗口在仓库的验收里就是关闭的。我没验的部分也照实说:Renode 里双任务的打印节奏,咱们本篇就没有重跑,6308dd0 对行为语义的改动(按它自己的说明)是零,板上的交错图案按咱们的判断应当跟上一篇一致,不过咱们的判断不算数,您要较真的话,拿上一篇的命令跑一遍就行。

## 下一篇

类型的外壳齐了,可调度器还欠着应用的一张脸,任务想睡觉的时候,还得绕道 arch 层的自由函数,想阻塞也没有正经的入口。下一篇讲的是配平中断的优先级,然后再让中断来敲任务的门,它给应用的是一张“当前任务”的公共脸,把 `ThisTask::sleep_for`、`yield`、`block` 逐个落了位,然后打通了第一条真中断的唤醒链路,USART1 收到了字节,睡着的任务就被叫醒了。沿途还有一堂硬课等着咱们,NVIC 的优先级数值必须大于临界区的 BASEPRI,默认的 0 会把一切的临界区撕开,咱们到时候现场把 0x80 改回 0,亲眼看一次队列被撕坏的样子。咱们那儿见。
