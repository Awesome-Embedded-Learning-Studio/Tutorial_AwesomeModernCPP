---
title: "事件来得再早也不许丢:计数信号量与八操作的 SystemContext 接缝"
description: "咱们锚定 5c62c0d,给上一站的一字节邮箱补上不会丢的记忆:SemaphoreBase 的 release 把单位落进计数器之后才去唤醒,被唤醒的人醒来重新查计数,竞争的输家自己回去睡;同一份信号量代码靠 SystemContext 概念在 host 的 FakeSys 与板上 CortexM3System 两头跑,限时等待按剩余的量重挂,不攒漂移;host 的 188 行 test_sync 七个用例二十条断言全部通过,Renode 的串口回声也从手写标志位换成了信号量驱动,我塞进去三个字符,板子一个一个回了回来"
chapter: 4
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - mutex
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
  - "CPU 归谁:从就绪位图到中断唤醒"
related:
  - "看 M 闭嘴那三秒:互斥锁、优先级继承与所有权移交"
current_status:
  title: 事件不再丢包
  detail: ZerOS的篇章正在打磨ing
  variant: polishing
  icon: 🎟
---

# 事件来得再早也不许丢:计数信号量与八操作的 SystemContext 接缝

上一站收尾的时候,咱们已经打通了头一条真中断的唤醒链路,USART1 收到字节的时刻,睡着的任务就被咱们叫醒了。不过您把当时的接法再回想一遍,就会发现它的记性不好,唤醒走的是一根手写的标志位,任务还没睡下去的时候字节就到了的话,登记就落了空,任务睡了下去之后,就没有人来叫它了。咱们干嵌入式的,事件来得比任务早的事见得多了,所以需要的东西很简单,它得把发生过的事实记成一个数目,等人来了再兑付。

本站的头一个 commit `5c62c0d`,subject 写的是 “feat: add Semaphore and system free funcs collects to Sys Concept”,它交来的就是咱们缺的记性。它一边交出了同步原语家族里的第一个成员 `SemaphoreBase`,一边把系统是什么的答案收进了一个八组操作的 `SystemContext` 概念,同一份信号量代码从此在 host 上靠替身跑、在真正的开发板上靠 `CortexM3System` 跑。我在自己的 /tmp 副本里把两条路都走了一遍,host 的 `test_sync` 一共一百八十八行,七个用例二十条断言是全部通过的,Renode 的串口回声也换了驱动,我往串口里塞了三个字符,板子把它们一个一个回了回来。中断优先级的那堂硬课和 `ThisTask` 的公共脸,上一站的文章已经交代过了,咱们今天不重复它们。互斥锁和优先级继承是本站的下一篇《看 M 闭嘴那三秒》,它管所有权的事,咱们今天铺的计数就是它的地基。

## 一字节邮箱输在哪

输的原因,咱们得对着代码说才服气。上一站的手写接法,骨架是下面的样子(它已经不在仓库里了,我凭当时的结构复述,细节以正文引用的源码为准):

```cpp
// 手写的接法(示意):标志位一根,读了就清
volatile bool g_ready = false;
char g_byte = 0;

extern "C" void USART1_IRQHandler() {
    g_byte = uart1_getc();
    g_ready = true; // 事件登记:只记得"有",不记得"几次"
}

void task_c(void*) {
    for (;;) {
        if (g_ready) { // 任务还没睡下去的时候字节就到了,这里照样是 false
            consume(g_byte);
            g_ready = false;
        }
        ThisTask::sleep_for(...);
    }
}
```

问题出在时序的缝隙里,咱们把它摆在桌面上。中断的登记和任务的登记是两个动作,它们中间隔着的是一次调度,要是任务还没来得及睡、字节就冲了进来,标志位置起来的时刻就落在了检查之后,任务接着睡了下去,就没有人再来叫它了。就算时序的运气好,标志位也只有一个格子的容量,连着来的两个字节里,头一个就被第二个盖掉了。6308dd0 用 `park_for` 把布防和阻塞关进了同一个临界区,睡死的窗口是关上了,不过“事件比人早到”的记性问题,那个修法就管不着了,因为它治的是布防的原子性,不治记忆的容量。

`5c62c0d` 在 example 里换上的接法,注释自己就把立场摆开了(这里引的是仓库的话,咱们后面逐句兑现):

```cpp
// the event side lives in the semaphore (a unit arriving BEFORE the task
// sleeps survives — the hand-rolled ready() flag could not do that);
// the byte itself is only the payload, still a one-slot mailbox
```

单位要是比任务睡下去的时刻来得早,它照样活了下来,手写的 `ready` 标志做不到这件事。凭什么信号量做得到?因为发生过的证据存在计数器里,它不跟着任务的睡与醒走。咱们这就去读它的实现。

## SystemContext:把“系统是什么”收进八组操作

信号量的代码要办的事,有睡下一个人的,有叫醒一个人的,也有问出现在几点的,这些活它自己一样都干不了,所以它要找的对象,就是系统这个东西了。可是系统是什么?在真正的开发板上,它是调度器加时间内核两个单例的组合,到了 host 的测试里,替身就换成了另一套。`5c62c0d` 把什么才算系统的问题写成了一个概念,它住的地方是 `include/ZerOS/kernel/system_concept.hpp`,咱们读懂了它,后面的代码才读得动。

按朴素的思路,信号量的头文件直接伸手去够调度器与时间内核两个单例,认的是 `system_sched` 与 `system_time` 的名字。后果咱们在上一站已经见过一遍了,`this_task.cpp` 走的就是上面的思路,它自己认识两个单例、自己装配叫醒的反应,换一个世界就得改一遍认识的名单,泛型的同步代码和具体的板绑在一起,同一份实现就两头跑不成了。这个概念把认路的方向倒了过来,同步原语不指名道姓地找谁,开出的是一份要求清单,满足清单的,就是它认的系统。写法咱们这就看:

```cpp
template <typename System>
concept SystemContext = requires(System& s, base::BorrowedPtr<sched::TCB> t,
                                 clock::Ticks::tick_t span) {
    { s.current_task() } -> std::same_as<base::BorrowedPtr<sched::TCB>>;
    { s.ready(t) };
    { s.block(t) };
    { s.sleep_for(t, span) };   // how a sleeper gets woken is the system's secret
    { s.yield() };
    { s.now() } -> std::same_as<clock::Ticks>;
    { s.self() } -> std::same_as<System&>;
    s.lock();
    s.unlock();
};
```

要求咱们一条条数下来是八组,咱们数给您看:问当前任务、就绪一个任务、阻塞一个任务、按时间睡一觉、让出 CPU、问现在几点、拿自己的引用,外加 `lock` 与 `unlock` 一对锁的操作(咱们把它算一组)。每个动作的形状都是调度器与时间内核本来就会的,概念没有发明任何的新能力,它只是把两个单例的能力捆成了一张脸。`sleep_for` 后面那句注释值得您多看一眼,“睡的人怎么被叫醒,是系统的秘密”:信号量只管把人交了出去,叫醒的路径(闹钟、回调、从到期记录反推回任务)全归系统的实现,同步原语的代码一个字都不碰硬件。

板上的答卷在 `include/ZerOS/arch/arm_cortex_m3/system.hpp`,咱们看它的头文件,里面有的只是声明,身子的部分全在 `system.cpp` 里:

```cpp
class CortexM3System {
  public:
    // the static door generic code walks in through (SystemContext: self())
    [[nodiscard]] static CortexM3System& self();

    base::BorrowedPtr<sched::TCB> current_task();
    void ready(base::BorrowedPtr<sched::TCB> t);
    void block(base::BorrowedPtr<sched::TCB> t);
    void sleep_for(base::BorrowedPtr<sched::TCB> t, clock::Ticks::tick_t span);
    void yield();
    [[nodiscard]] clock::Ticks now();
    void lock();
    void unlock();

  private:
    // guards the system face itself; BASEPRI, nest-friendly like every
    // other lock in the family
    CortexM3CriticalSection guard_{};
};
```

类的骨架过完一遍,咱们能看见的就是一行行的声明,八个动作里没有一个带着身子的,方法体的老家全在 `system.cpp` 里,头文件的注释自己也是这么交代的,类只是声明的集合,寄存器与单例的秘密就漏不出这颗头文件了。断言跟在类的后面,它值得咱们单独看一眼:

```cpp
static_assert(ZerOS::system::SystemContext<CortexM3System>);
```

它是我最喜欢的一行,类是不是真满足了八组操作,编译器在编译期就替咱们验收了,少写了任何一个函数,固件连编译都过不去的。`system.cpp` 的开头也把话挑明了,它是全仓库唯一一个同时伸手够两个单例的地方:

```cpp
// defined in switch.cpp / time_kernel_cm3.cpp, declared no further
extern SystemScheduler system_sched;
extern SystemTimeKernel system_time;
```

咱们看它怎么把八组操作一个一个转接过去,挑有内容的几组看,转发的几组形状一样就不占地方了:

```cpp
namespace {
// the wake reaction every timed sleeper on this chip shares: walk back
// from the alarm node to its owner and make it runnable again
void wake_from_sleep(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> waiter) {
    system_sched.ready(ZerOS::sched::TCB::owner_of(waiter));
}
} // namespace

void CortexM3System::sleep_for(base::BorrowedPtr<sched::TCB> t, clock::Ticks::tick_t span) {
    system_sched.sleep_for(t, system_time, span, wake_from_sleep);
}

clock::Ticks CortexM3System::now() {
    return system_time.current();
}

void CortexM3System::lock() {
    guard_.lock();
}
```

`sleep_for` 是八个里唯一要把反应函数交到调度器手里的,它把任务、时间内核和 wake_from_sleep 一起交了过去。这个 wake_from_sleep 的老家搬过几次家,咱们把它的履历摆一摆:咱们讲调度器的那两篇里,它在 example 的应用代码里手搓,到了上一站的 ThisTask 与中断唤醒篇,它收进了 arch 的 this_task.cpp,今天本 commit 又把它挪进了 system.cpp,成了这块芯片上所有限时睡眠的人共用的叫醒反应。它干的事没变,从闹钟节点反推回 TCB(`owner_of` 拿内嵌成员的地址减去偏移),再把任务放回就绪的队列。应用侧的真实变化是,ISR 从此不再摸 `system_sched` 了,上一版里那个 `g_task_c` 的全局指针删掉了,任务 c 的睡处,中断从此不关心了,它只把单位交给了信号量。

接缝装好了之后,这个 commit 还把旧的声明拆掉了(`switch.hpp` 与 `time_kernel_cm3.hpp` 里的两个 `extern` 删了),注释把话写成了“the extern is nobody's business now”,运行时的用户都走 `system::os()` 这个交接点。咱们再看 `this_task.cpp`,它也改了,它原来自己认识两个单例、自己装配叫醒的反应,现在整份文件认识的只剩 `os()` 了。头文件也搬了家,this_task.hpp 从 include/ZerOS 的门口挪进了 kernel/sched 的地盘,上一站那个 include 路径的修复,到这里就算归了位了。单例从此收进了 arch 的 .cpp 家族,泛型代码想拿系统的时刻,走的只有一个入口:

```cpp
namespace ZerOS::system {
// the handover point: generic code asks for THE system, never the parts
[[nodiscard]] inline arch::cortex_m3::CortexM3System& os() {
    return arch::cortex_m3::the_system;
}
} // namespace ZerOS::system
```

host 那头的答卷也用同一个概念验收,`test_sync.cpp` 里的替身叫 `FakeSys`,它满足的也是同一份 `SystemContext`,文件里守着它的,还有守着同一件事的一行 `static_assert(ZerOS::system::SystemContext<FakeSys>)`。替身把 sleep 做成了 host 特产,注释说得倒是直白,真硅片上这些业务跑在任务被 PendSV 停住的间隙里,host 上它就在阻塞钩子的肚子里跑。替身怎么用,咱们到考场的节里看。

同步原语的用户怎么指定系统?答案藏在 `include/ZerOS/arch/arm_cortex_m3/sync.hpp` 里,咱们看它,全文有的就一个别名:

```cpp
// The chip's own flavor of the sync family: systems travel in the TYPE,
// so applications spell a plain name and never a template argument.

using Semaphore = SemaphoreBase<arch::cortex_m3::CortexM3System>;
```

“系统跟着类型走”:板上写 `Semaphore` 就够了,模板参数在别名里定好了,测试里写的就得是 `SemaphoreBase<FakeSys>`,同一份实现由两个世界各领各的系统。咱们写出来的信号量代码一字不改,host 与板的两头都能跑,这件事的底气就是咱们本节看过的两份答卷加一条概念。

## P/V:计数器是唯一的真相

动代码之前咱们把要办的事说清,说清了代码就好读了。标题里的 P 与 V 是信号量的老名字,P 对应的就是 `acquire`,干的活是从计数器里取走一个单位,V 对应的就是 `release`,它干的事是把一个单位放回计数器,文章开头要的“把发生过的事实记成一个数目”,落到代码上就是它们两个的配合。难办的是取的那一半,计数的值还是零,取的人就得睡下去等人放回。朴素的写法是“查一眼,没货就睡”,拆成了两个自由的动作,查与睡的中间就留出了缝隙,查的时刻单位还在,睡下去的前一刻单位被别人领走了,叫醒就落了空,文章开头丢字节的事故就是从这个缝隙里来的。所以取单位得握着临界区办,咱们让查计数、扣单位、挂等待链、睡下去四步全在同一个临界区里完成,而且睡醒不算拿到,醒来的人得重新查计数,查到了才算数,这里头的讲究,咱们到 `release` 那一段就看明白了。

主角登场了,它是 `include/ZerOS/kernel/sync/semaphore.hpp` 里的 `acquire`,咱们整段看:

```cpp
    void acquire() noexcept {
        auto& system = System::self();
        const auto& current_task_ = system.current_task();
        SemaWaiter me{current_task_.get()};

        while (1) {
            ZerOS::irq::CriticalGuard g{system};
            if (count_ > 0) {
                --count_; // down self
                waiters_.remove(&me);
                return; // quit the issue
            } else {
                waiters_.insert(&me);
                system.block(current_task_);
            }
        }
    }
```

咱们顺着一个睡着的任务走一遍它的命。计数的值是零,它进了 `else` 的分支,把自己包成了一个 `SemaWaiter` 挂进等待链,随后把 `block` 的活交给了系统,任务就睡了。在这之后的某个时刻,一个单位到了,任务就被叫醒了,它从 `block` 的返回处接着跑,您注意它跑回的是 `while (1)` 的开头,人还留在函数的肚子里。醒来的人要重新抢临界区、重新看计数器,计数的值还是零的话,它就把自己重新插回了等待链,接着睡了下去。被唤醒的人不等于拿到了单位,拿没拿到的裁决,权力永远在计数器的手里,这就是竞争的输家回睡的意思,仓库在 `release` 的注释里也点了名,这是 POSIX 信号量的语义。

为什么被唤醒的人可能拿不到?咱们看 `release` 就明白了:

```cpp
    void release() noexcept {
        auto& system = System::self();
        ZerOS::irq::CriticalGuard g{system};
        ++count_; // the unit ALWAYS lands in the counter — woken sleepers
                  // race for it (loser re-sleeps, that is POSIX-semantics)
        if (auto head = waiters_.pop_head()) {
            // Some one is waiting!
            system.ready(head->task_);
        }
    }
```

`release` 的头一个动作是 `++count_`,而且是在临界区里做的。注释把设计立场喊了出来,单位落进计数器的动作永远是头一步,被唤醒的睡客们去抢它,抢输的回睡。这个次序是丢与不丢的分水岭,单位落进计数器的动作在前,唤醒的登记在后,所以哪怕唤醒的登记因为任何原因走样了,单位也已经在计数器里躺着了,后来的 `acquire` 一查计数就能把它领走,事件比任务早到的场景就这样活了。次序要是反了过来,唤了人却不落单位,唤醒一旦落了空,单位就跟着蒸发了。等待链上要是有人的话,`pop_head` 就把队头叫起来了,没有人的话就只落单位不叫人,`release` 自己一个字不问调用的人是谁,中断里调它也一样是安全的,这一点咱们到串口的节里兑现。

还有一个细节我请您留意,`SemaWaiter me` 是栈上的局部对象,每次进 `acquire` 的时候都新造一个。等待链是侵入式的(`SemaWaiter` 背着 `SelfNode` 的 `next_`),人睡着的时候栈不动,节点就稳稳地挂在链上,醒来了之后 `waiters_.remove(&me)` 把自己摘掉,退栈的时刻一到,节点也就跟着没了。零堆的纪律在这里也没有破。

## 限时等待:按剩余的量重挂,不攒漂移

无限等讲完了,带期限的等待是同一个循环再加一把闹钟。朴素的做法是把闹钟上成参数里的原值,睡下去被打断了一回,它就照原值把整段的时长再睡一遍,重演了两回,实际的等待就奔着十毫秒去了(拿用例里的五毫秒来说,数字是假想的)。仓库的答法是给等待定一个截止点,进函数的时刻加时长,往后每一轮睡的都只是剩下的量,截止点从头到尾是不动的,漂移就没有攒起来的地方,它的名字咱们在标题里也见过了:“按剩余的量重挂”。函数的头部把截止点定下来,咱们看:

```cpp
    SyncError try_acquire(const clock::Milliseconds t) noexcept {
        auto& system = System::self();
        const auto& current_task_ = system.current_task();
        SemaWaiter me{current_task_.get()};
        const auto ddl = system.now().tick_ + t.count;
```

`ddl` 在进函数的时刻就算定了,它记的是“此刻加时长”的截止点,往后是不动的。不过它只是个终点,循环里的每一轮要睡多久,咱们得看此刻离截止点还剩多少,算剩余量的事全在循环里,咱们接着往下看:

```cpp
        while (1) {
            ZerOS::irq::CriticalGuard g{system};
            if (count_ > 0) {
                --count_;
                waiters_.remove(&me);
                return SyncError::Ok;
            }
            const auto left = static_cast<clock::Ticks::tick_diff_t>(ddl - system.now().tick_);
            if (left <= 0) {
                waiters_.remove(&me);
                return SyncError::TimedOut;
            }
            waiters_.insert(&me);
            system.sleep_for(system.current_task(), static_cast<clock::Ticks::tick_t>(left));
        }
    }
```

循环里 `left` 算的就是剩余量,咱们看它的算式:拿截止点减此刻,睡下去的时长缩到了剩下的部分。为什么非要按剩余量算?因为一轮等待可能被中途打断,单位是没有来的,任务却被系统叫醒了(可能是别人 `release` 的时候把它一起唤的,也可能是别的原因把它从睡眠里捞了出来),它回到循环的开头一查,计数的值还是零,这时候它重新睡下去的时长要是照原始参数再来一遍,总期限就被悄悄拉长了,打断的次数一多,攒下来的误差就越来越大了。按剩余量重挂的意思就是,无论中间被捞起来了多少回,截止点都是一动不动的,到期的判定永远是 `left <= 0`,漂移就没有累积的地方了。

循环里还剩一处没有讲的,它和本 commit 的另一行改动配着看。时间内核的 `enqueue_locked` 在这个 commit 里多了一行,我从 diff 里把相关的几行照原样搬给您,签名的形态您一眼就能对上仓库:

```diff
     void enqueue_locked(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span,
                         TimeWaiter::OnTimeAction wake) {
+        queue_.remove(task);
         if (wake != nullptr) {
             task->OnTime = wake;
         }
```

这一行删掉的是旧闹钟,咱们把场景走一遍。限时等待的任务,它上一轮挂的闹钟可能还没响,单位就来了,任务赢下了这一局,立刻又开一轮新的限时等待,旧的闹钟节点要是还留在队列里,到点的时刻它还会再响一回,把正等着别处的人搅一下。重挂的路径收下了这行改动,摘掉旧的再挂新的,等待队列里的同一个任务,它的名下永远只有一个节点。

::: warning
`left` 的计算里有个类型转换值得您写代码的时候留神,`ddl - now` 拿的是无符号的 tick 差,咱们把它强转成 `tick_diff_t`(有符号)之后再判 `<= 0`,这样截止点被回绕算术绕过去的时候,判定也不会错的。直接拿无符号比较的话,回绕了一次,判定就反了。咱们在讲时间的那一篇里说过的回绕算法,在这里又用上了。
:::

## host 考场:一百八十八行的 test_sync

机制的每一个角落,仓库都配了用例,咱们 checkout 到 `5c62c0d`,把 host 的构建跑起来:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机是 GCC 16.2.1 加 CMake 4.4.4,这次没有编译的意外,`ctest` 的成绩单是八个目标全过:

```text
100% tests passed out of 8

Total Test time (real) =   0.16 sec
```

新交的 `test_sync.cpp` 一共一百八十八行,咱们看它自己的成绩单:

```text
All tests passed (20 assertions in 7 test cases)
```

测试文件有意思的地方在于它的替身设计,咱们看 `FakeSys` 的关键几行:

```cpp
    static inline std::function<void()> during_sleep = nullptr;

    void block(BorrowedPtr<TCB> t) {
        sched_->block(t);
        if (during_sleep != nullptr) {
            during_sleep();
        }
    }
```

`during_sleep` 是一个测试自己填的钩子,任务一睡了下去,钩子就替全世界跑业务了。它让单任务的单测能演出双人戏,任务睡了,钩子里的“全世界”把一个单位放了出来,任务被唤回来了,断言确认它拿到的是那个单位。这套写法的妙处是单测不用起真线程,时序全在咱们手里捏着。

用例和正文的座是一一对得上的,咱们挑四个看。用例里的 World,它是每个用例现搭的夹具,它把调度器和时间内核装在了一起,又配上了一个现成的任务,构造的时候把它们接给了 FakeSys,再把任务选成了 current,咱们写用例的时候就只管演剧情了。头一个验的就是“release 在前”的次序:

```cpp
TEST_CASE("release first: acquire takes the fast path, nobody sleeps", "[sync]") {
    World w;
    Sem sem{0};

    sem.release();
    CHECK(sem.try_acquire() == SyncError::Ok);
    sem.release();
    CHECK(sem.try_acquire(Milliseconds{7}) == SyncError::Ok); // fast path ignores time
    CHECK(sem.try_acquire() == SyncError::TimedOut);           // drained
}
```

单位放出来的时候没有人在等,它就躺在了计数器里,后来的 `try_acquire` 连零期限的都不用碰时间,直接就从快路径把它领走了。领完了,计数归了零,再来一次的话就是 `TimedOut`,一进一出的,数目是分毫不差的。咱们刚才数的,就是咱们要找的“事件来得再早也不许丢”那句话的 host 证词。

第二个演的是睡与唤醒的整条弧线,咱们看用例:

```cpp
TEST_CASE("acquire sleeps, release lands while asleep, wake and take", "[sync]") {
    World w;
    Sem sem{0};

    FakeSys::during_sleep = [&sem] { sem.release(); }; // the world releases while I park
    sem.acquire();                                     // returns only after the world gave a unit
    // host: the task object sits Ready until someone picks it again
    REQUIRE(w.sched.pick_next().get() == &w.task.tcb);
    CHECK(ZerOS::sched::zeros_impl::TCBKeys::state(w.task.tcb) == TaskState::Running);
}
```

咱们看这段的剧情,任务睡了,钩子里的世界放出了一个单位,任务被唤醒了,`acquire` 也就返回了,断言确认任务回到 `Running` 而且确实归了队。注释里写下的 “returns only after the world gave a unit” 是测试自己给 `acquire` 写的判词,它返回的时刻,就是单位兑付的时刻。

第三个是限时等待的闹钟,咱们看:

```cpp
TEST_CASE("timed acquire times out through the alarm", "[sync]") {
    World w;
    Sem sem{0};

    FakeSys::during_sleep = [&w] { w.time.on_elapsed(5); }; // the clock runs out
    CHECK(sem.try_acquire(Milliseconds{5}) == SyncError::TimedOut);
}
```

钩子这回推动的是时钟,时钟走了五个 tick,闹钟就到了点,`try_acquire` 如实交回了 `TimedOut`。剩下的用例把边角也扫了,咱们点一遍名。零期限的 `try_acquire` 根本不许睡,断言查了任务的状态还停在 `Running`。单位与闹钟同刻到场的时候,胜负的裁决权在计数器手里。无限等的 `acquire` 压根不给闹钟留位子,用例里推了一百个 tick,任务还是 `Blocked` 的状态,注释的话说得够狠,一个世纪过去了也不该有人来叫它。还有本 commit 那行 `queue_.remove(task)` 的专门证词,旧的闹钟节点必须被忘掉,新一轮的限时等待才干净。这些咱们就不逐条念了,您把文件打开一遍就能全见着。

## Renode:串口回声换成信号量驱动

机制的验收做完了,咱们到 Renode 里跑板上的部分。example 的 `main.cpp` 在这个 commit 里多出了第三个任务,咱们把它分成三段看,头一段是全局的家当,事件与载荷的登记处:

```cpp
// the event side lives in the semaphore (a unit arriving BEFORE the task
// sleeps survives — the hand-rolled ready() flag could not do that);
// the byte itself is only the payload, still a one-slot mailbox
constinit ZerOS::sync::Semaphore g_rx_sem{0};
constinit ZerOS::arch::cortex_m3::CortexM3CriticalSection mailbox_lock{};
char g_rx_byte = 0;
```

事件与载荷在这里分了家,事件归了信号量,字节本身还是一格的邮箱,分工在开头那三行注释里就写清楚了。登记处齐了,咱们看取货的人:

```cpp
// event-sleeper: woken by the RX interrupt, not by the clock
void task_c(void*) {
    for (;;) {
        g_rx_sem.acquire();
        char c;
        {
            ZerOS::irq::CriticalGuard guard{mailbox_lock};
            c = g_rx_byte;
        }
        ZerOS::board::print("got '");
        ZerOS::board::uart1_putc(c);
        ZerOS::board::print("'\r\n");
    }
}
```

它 `acquire` 到了单位才去邮箱里取货,取货的时刻,咱们拿临界区护住,取完了把字节原样回显出去。剩下的问题是中断那一头,它凭什么可以放心地调 `release`?咱们看:

```cpp
// RXNE (and ORE, sharing RXNEIE): park the byte, hand the event over.
// release() is ISR-safe by construction (BASEPRI inside, switch pended,
// never switched here)
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) == 0) {
        return;
    }
    const char c = ZerOS::board::uart1_getc();
    {
        ZerOS::irq::CriticalGuard guard{mailbox_lock};
        g_rx_byte = c;
    }
    g_rx_sem.release();
}
```

注释里给了答案,锁是 BASEPRI 实现的,切换是挂起给 PendSV 的,ISR 里是从来不真的切任务的。它把字节停进了邮箱,再 `release` 一个单位的量,事件的登记就算完成了。还有一层是咱们正文里说过的,`release` 的单位落进计数器在唤醒之前,所以哪怕字节到达的时刻任务还在跑别的、还没睡进 `acquire`,单位也就在计数器里等着它了,一条都丢不了。

我把仓库用 `git archive 5c62c0d` 导到 /tmp 的副本里,配上交叉的工具链构建固件:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

体积报告是真实的,我贴在下面:

```text
FLASH:        2564 B        64 KB      3.91%
  RAM:        1208 B        20 KB      5.90%
size report (D12 预算:核心内核 flash<=6KB RAM<=1KB)
   text	   data	    bss	    dec	    hex	filename
   2372	     192	   1016	   3580	    dfc	/tmp/zeros-t5c62/build/src/board/stm32f103_bluepill/zeros-bluepill.elf
```

我数了数,信号量、系统脸、第三个任务全住进去了,flash 用掉了 2564 字节,RAM 占了 1208 字节,离 D12 的预算线(flash 6KB、RAM 1KB)还远得很。

到了上 Renode 的这一步,我得照实交代一段折腾。我本机的 Renode 1.17.0 认不出 `bluepill.repl` 里的 `I2C.STM32F4_I2C`,上一站咱们就遇过,我把 i2c1、i2c2 两个节点注释掉了(demo 不经过 i2c,行为是不受影响的)。这回还多出一个新的问题,任务 c 的回声要有人往串口里塞字节才演得出来,而 1.17.0 的监视器命令里翻不到给 UART 注入接收字节的方法,`showAnalyzer` 的窗只能看,不能写的。我的办法是把仿真端的串口接到一个 socket 终端上,再拿几行 python 的脚本往 socket 里塞字符,板子就把 socket 的对端当成了串口的另一头:

```text
emulation CreateServerSocketTerminal 34567 "term" false
connector Connect sysbus.usart1 term
```

之后咱们 `start`,等 A 与 B 的节奏跑稳了,就从咱们开发它的电脑上连进 127.0.0.1:34567,发送用的脚本就下面几行,您拿去改改字符就能复现:

```python
import socket, time

s = socket.create_connection(("127.0.0.1", 34567))
for ch in b"hZ!":
    s.send(bytes([ch]))  # h、Z、! 一个一个来
    time.sleep(1.0)      # 彼此隔一秒
```

我发出去的字符依次是 `h`、`Z`、`!`,彼此隔了一秒。串口窗的真实输出摘录如下,发送的时刻落在仿真的大约 5.1 秒、6.1 秒、7.1 秒:

```text
[INFO] usart1: [host:    4.22s (+0.5s)|virt:         4s (+0.5s)] A
[INFO] usart1: [host:    4.42s (+0.2s)|virt:       4.2s (+0.2s)] B
[INFO] usart1: [host:    5.12s (+0.7s)|virt:       4.9s (+0.7s)] B
[INFO] usart1: [host:   5.22s (+99.95ms)|virt:        5s (+100ms)] A
[INFO] usart1: [host:  5.31s (+90.09ms)|virt:   5.09s (+85.35ms)] got 'h'
[INFO] usart1: [host:   5.82s (+0.51s)|virt:      5.6s (+0.51s)] B
[INFO] usart1: [host:    6.22s (+0.4s)|virt:         6s (+0.3s)] A
[INFO] usart1: [host:  6.3s (+81.32ms)|virt:   6.08s (+81.16ms)] got 'Z'
[INFO] usart1: [host:   6.52s (+0.22s)|virt:      6.3s (+0.22s)] B
[INFO] usart1: [host:    7.22s (+0.7s)|virt:       7s (+0.7s)] A
[INFO] usart1: [host:  7.22s (+0.14ms)|virt:       7s (+1.96µs)] B
[INFO] usart1: [host:  7.3s (+81.09ms)|virt:   7.08s (+81.37ms)] got '!'
```

咱们从刚才的输出里读出的验收事实有这些。咱们数下来,三个字符对上了三个 `got`,一个也没有丢掉的,每个都赶在 A 与 B 的缝隙里回来了。回声的延迟您看 virt 的列,`h` 的发送落在 5.09 秒,回声几乎同时就出来了,字节一到了,USART1 的中断就立刻打了进来,`release` 的单位落进了计数器,优先级 0 的 task_c 比打着 1 号牌的 A 与 B 都急,它被 `ready` 了之后,下一次的 `pick_next` 就归它,它把字节取出来做了回显,再回去睡它的 `acquire`。整条链是中断叫醒了任务,时钟是从头到尾没有参与过的。还有一处对得上咱们正文的,回声的三个时刻都没有惊动 A 与 B,它们照旧 0.7 秒与 1 秒一行地走着,task_c 抢的只是它的那一次切片,切完就把 CPU 还了回来。

## 验收

咱们把本篇的可查证事实归拢一下,一共归出了三件,咱们一件一件来。

第一件是 host 的全部通过。您 checkout 到 `5c62c0d`(third_party 下的工作树是个 gitfile,拿 `git archive 5c62c0d` 把树导出来就行了),照正文的三条命令配 host 构建,`ctest` 给您的应该是八个目标全部通过,`test_sync` 自己的成绩单是七个用例二十条断言,数目跟我跑出来的一致。

第二件是双世界的同一份代码。概念住的地方是 `system_concept.hpp`,板上的答卷带着 `static_assert(ZerOS::system::SystemContext<CortexM3System>)`,host 的答卷带着 `static_assert(ZerOS::system::SystemContext<FakeSys>)`,您删掉答卷上的任何一个函数,这两行断言就替编译器把您拦下来。`sync.hpp` 里的那个别名是两头共用一份实现的落点,`SemaphoreBase<FakeSys>` 与 `Semaphore` 跑的是同一份 `acquire` 与 `release`。

第三件是板上的回声。您拿同样的交叉构建命令出固件,Renode 起来了之后,咱们把串口接到 socket 终端,往里发什么它回什么,咱们贴出来的那段串口窗输出,您用同样的步骤应该能得出一模一样的形状,三个字符换来了三个回声,A 与 B 的节奏照旧。我没验的部分也照实说,咱们今天验的是事件不丢,互斥的那一半(所有权、移交、优先级继承)在 `7dbbcdf` 里,是姊妹篇《看 M 闭嘴那三秒》的地盘,计数器只是它脚下的地基,咱们到那儿再拆。

## 下一篇

计数会记的是事,不过它不认人,谁 `acquire` 到的单位,`release` 的那个人根本无从知晓,两个任务抢同一个外设的时候,光靠计数是拦不住它们同时伸手的。咱们下一篇《看 M 闭嘴那三秒》把所有权加到计数之上,等待链按优先级排了队,解锁把所有权直接移交给队头的等待者(不放回竞争),再带上单层的优先级继承,把低任务的优先级临时抬起来,让卡在外面的最高任务等得短一点。收尾是串口剧场上演的 L、M、H 三个任务,M 的打印突然安静下来的三秒,就是继承在起的作用。咱们那儿见。
