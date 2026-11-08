---
title: "叫醒所有人,各自重查真相:事件组、直接通知与满环背压"
description: "92a9aae(feat: event group, mailbox, sync wait)给同步家族收尾:EventBase 用一张 uint32 位图加一条侵入式等待链,wait 的掩码支持 ANY 与 AND 两种读法,set() 置位后广播唤醒全部等待者而不审判谁的条件已满足。任务控制块内嵌单槽邮箱,notify 覆盖写、最新值胜,目标睡在哪个原语上都投得到。队列拆出永不睡眠的 post() 与带背压的 post_for(),满环时发送方睡到有空位。我在 /tmp 副本跑通了全部 14 个 host 测试目标,Renode 按 e 与 f 演双钥匙 AND 互锁、按 n 看定时器在 tick 上下文直接投信,还把事件任务的栈缩回 64 词复现了一次活捉,金丝雀报告的 eaten=6,96 词的新栈深就是这么量出来的"
chapter: 6
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 并发
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 22
prerequisites:
  - "事件来得再早也不许丢:计数信号量与八操作的 SystemContext 接缝"
  - "中断只递条子:环形队列、工作者线程与底半推迟"
related:
  - "数到十就换人:同级时间片与不继承的切片债"
  - "芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志"
---

# 叫醒所有人,各自重查真相:事件组、直接通知与满环背压

上一篇把调度器的公平补齐了,咱们这边的同步家族还欠着最后一口气。信号量数的是事情发生过几次,互斥锁管的是现在轮到了谁,队列送的是一字节一字节的消息,可是它们等的东西都只有一个条件。咱们想表达“三样材料都齐了才开工”的时候,手上只有信号量的咱们,就只能拿三个信号量各等上一轮的笨办法,三次睡眠排成了一串。还有另一类请求更直白:我就是想把一个数递到那个任务的手里,不想为它专门去造一个内核的对象。这些需求归的是同一个 commit,`92a9aae`,subject 写的是 “feat: event group, mailbox, sync wait”。我数过它的 diff,改动落在了 21 个文件上,新增了 752 行,删掉了 8 行,事件组、直接通知和满环背压一次就交齐了。

这一篇也是整条同步线的收尾。讲完它您回头看,会发现家族里每个原语醒人的方式其实是同一句话:发布的人不审判谁的条件满足了,把所有可能相关的人都叫醒,醒来的人各自重查自己的真相,查完了发现自己还没轮到,就接着睡了回去。这句话信号量是遵守的,互斥锁也是遵守的,队列和事件组也是遵守的,本篇新来的直接通知一样遵守。咱们把它当作本站的收束语,正文里每讲到一个新的原语,您都能再见到它一次。

## 接缝再添两个动词

动原语的前一步,咱们照惯例看接缝动了哪里。`SystemContext` 是同步原语认系统的要求清单,信号量那一篇咱们数过它的动词,后来互斥锁添了 `prio` 与 `reprioritize`,定时器那一篇添了 `arm_timer` 与 `cancel_timer`。到了 `92a9aae`,清单上又多了一对,咱们看 `include/ZerOS/kernel/system_concept.hpp` 里的改动:

```cpp
{ s.notify(t, 42u) } -> std::same_as<bool>;                    // mailbox drop
{ s.wait_notify(span) } -> std::same_as<std::expected<std::uint32_t, sync::SyncError>>;
```

`notify` 干的是投递的活,它把一个 32 位的值放进指定任务的信箱,投成了返回真。`wait_notify` 干的是收信的活,当前任务等自己的信,等到了返回值,等不到就返回 `SyncError::TimedOut` 的错。概念扩了参数,六个测试文件里的 `FakeSys` 替身就全部跟着升级了,每个替身都补上了这一对动词的实现,少了哪一个都编不过去。这是咱们在互斥锁那一篇见过的演化模式,概念的每一次扩参,都是编译器替咱们清点全部实现方的一趟。

## 事件组:一张位图加一条等待链

主角登场了,咱们打开 `include/ZerOS/kernel/sync/event_group.hpp`,全文 92 行的个头,是本篇的主菜之一。事件组的本体只有两个成员:

```cpp
    uint32_t bits_{};
    base::SelfList<EventWaiter> waiters_{};
```

咱们看两个成员各自的成分:`bits_` 是一张 32 位的事件位图,每一位就是一面可以点亮也可以熄灭的灯。`waiters_` 是一条侵入式的等待链,等事件的任务把一个轻量的节点挂上来,节点里只记着一个任务的指针:

```cpp
    struct EventWaiter : base::SelfNode<EventWaiter> {
        constexpr explicit EventWaiter(sched::TCB* task) noexcept : task_(task) {}
        sched::TCB* task_;
    };
```

等待的接口有两个重载,永久版咱们整段读,它是理解事件组的钥匙:

```cpp
    std::expected<uint32_t, SyncError> wait(uint32_t mask, bool all_of) noexcept {
        auto& system = System::self();

        while (1) {
            irq::CriticalGuard g{system};
            if (auto hit = satisfied_or(mask, all_of); hit.has_value()) {
                return hit;
            }
            EventWaiter me{system.current_task().get()};
            waiters_.insert(&me);
            system.block(system.current_task()); // park + sleep, one lock — no gap
        }
    }
```

咱们把 `mask` 的成分说清:它是调用者关心的位的集合,`all_of` 决定了掩码怎么读。读法的判断住在私有的 `satisfied_or` 里:

```cpp
    [[nodiscard]] std::expected<uint32_t, SyncError> satisfied_or(uint32_t mask,
                                                                  bool all_of) const noexcept {
        const uint32_t hit = bits_ & mask;
        if (hit != 0 && (!all_of || hit == mask)) {
            return hit;
        }
        return std::unexpected(SyncError::TimedOut); // "not satisfied yet"
    }
```

`bits_ & mask` 把咱们关心的位捞了出来。`all_of` 为假的时候,捞出来的结果只要有一位亮着就算满足,这是 ANY 的读法,等的是“随便哪一件事发生了都行”。`all_of` 为真的时候,捞出来的结果必须与掩码完全相等,少了一位都不行,这是 AND 的读法,等的是“全部材料都到了齐”。注释把返回 `std::unexpected` 的意思也写明了,它在这里代表的只是“还没满足”,超时与未满足两种情况在返回类型里共用了同一个通道,等待的循环靠它决定要不要接着睡。

循环的形状您留意一下:查了一次,不满足就挂了链睡去,醒来之后从循环的头顶重新查。返回值的类型是 `uint32_t` 而不是布尔,它交回的是实际亮起来的那些位,多个条件同时满足的时候,调用者能看出都满足了哪几个。

### 事件是状态,不是消耗

信号量走的是消耗语义,计数被取走一次就少了一次。事件组就不一样了,它走的是电平语义:位亮了就一直亮着,同一个位在 `clear` 之前可以反复地等到。仓库给咱们配了专测,用例的名字就把话说完了,咱们照录如下:`events are states, not consumptions: same bit waits twice`:

```cpp
    e.set(0b0101);
    auto first = e.wait(0b0101, true, Milliseconds{0});
    auto second = e.wait(0b0101, true, Milliseconds{0}); // nobody set anything again
    REQUIRE(first.has_value());
    REQUIRE(second.has_value()); // still lit — no take-one-lose-one here
    CHECK(*first == 0b0101);
    CHECK(*second == 0b0101);
```

后面没有人再调 `set` 了,第二次的等待照样拿到了 `0b0101`。定时版的重载咱们就不整段念了,它的形状和信号量的 `try_acquire` 一样:算出了截止时刻,每一圈的循环都要查一次,剩余时间归零就摘掉自己的节点返回超时,否则带着剩余的时间睡去。

## set() 广播:发布者不审判,醒来者各自重查

置位的代码短,可是它是本篇标题的出处,咱们整段读:

```cpp
    // Light bits. TASK/ISR safe. Wakes EVERY waiter — broadcast: the setter
    // never judges who is satisfied; each woken one re-checks its own mask
    // at the loop head (a spurious wake-up costs one loop turn, nothing more).
    void set(uint32_t bits) noexcept {
        auto& system = System::self();
        irq::CriticalGuard g{system};
        bits_ |= bits;
        while (auto w = waiters_.pop_head()) {
            system.ready(w->task_);
        }
    }
```

咱们整段读完它的动作:`set` 把位点亮了,然后把等待链上的人一个不剩地全部 `ready` 回就绪队列。注释把设计说了出来:置位的人从不判断谁的条件满足了,每个被唤醒的人回到自己 `wait` 循环的头顶,拿自己的掩码重查一遍。把 `bits` 或进位图的动作发生在临界区里,所以这个函数任务和中断都能调,注释开头的那句 “TASK/ISR safe” 说的就是它。

咱们咂一下这个设计的味道。另一种写法是 `set` 替等待的人们做判断,只唤醒掩码已经满足的那些,看起来省了几次空跑。可是那样 `set` 就得理解每个人的等待条件,等待链上的每个节点都得背着自己的掩码,两个原语的内部互相缠绕了。广播的写法把判断还给了等待者自己,`set` 只管点亮和叫人的活儿,代价是多出了伪唤醒,被叫醒但条件未满足的人白转一圈循环再睡回去。注释给这个代价标了价:“a spurious wake-up costs one loop turn, nothing more”,一圈循环而已,换来的接口是置位方对等待方零了解。

这个形状咱们其实早就见过。信号量的 `release` 在加了计数之后,注释里写的话是 “woken sleepers race for it (loser re-sleeps, that is POSIX-semantics)”,抢输的人接着睡。互斥锁的 `lock` 循环醒来之后重新查 `owner_`,移交没到就重新排了队。队列的等待、上一站睡眠的唤醒,走的也全是同一条路。到这里咱们可以把它说完整了:广播唤醒,醒来之后各自重查自己的真相。它不写在任何的头文件里,它是横跨信号量、互斥锁、队列、工作者、定时器、事件组与直接通知的家族不变式,工作者睡在信号量的等待上,定时器的回调跑在 tick 的上下文里,它们被叫醒之后同样要把自己的真相重查一遍,每个原语的实现细节各不相同,醒人与查真相的分工却是同一副骨架。收束语的兑现就在这里,您往后自己写同步原语的时候,不妨拿它当您的第一道自查。

## 直接通知:内嵌在任务身上的单槽信箱

第二个主角的需求场景是这样的:咱们只想把一个数递给某个任务,而不想为它造信号量、造队列、占一块 RAM。Blue Pill(C8T6)只有 20KB 的 RAM,内核对象省下了一个是一个。ZerOS 的答案是把这个信箱直接做进任务控制块里,咱们看 `task_control_block.hpp` 里 TCB 新增的两个字段:

```cpp
    // the task's private mailbox: direct notifications, no kernel object.
    // Single slot, LATEST value wins (overwrite) — see Scheduler::notify.
    std::uint32_t notify_{};
    bool has_notify_{};
```

信箱的槽只有一个,一个 32 位的值加一面写着“有信”的标记,一共占了 5 个字节。投递的实现住在调度器里,咱们整段读:

```cpp
    // Direct-to-task mailbox drop. Task/ISR safe. Single slot, OVERWRITE:
    // the latest value wins. The target may be parked ANYWHERE (a semaphore,
    // a queue, a sleep) — the ready() below may wake it "by mistake", and
    // that is fine: every waiter family re-checks its own truth on waking
    // and simply goes back to sleep; the mailbox owner checks has_notify_.
    bool notify(base::BorrowedPtr<TCB> t, std::uint32_t value) noexcept {
        irq::CriticalGuard guard{driver_};
        if (!t || t == &idle_) {
            return false;
        }
        t->notify_ = value;
        t->has_notify_ = true;
        if (t->state_ == TaskState::Blocked) {
            ready(t); // nested guard; pends the switch, never switches here
        }
        return true;
    }
```

投递干的就是覆盖写的活,槽里不管有没有上一封的旧信,新信直接压了上去,注释里的 “OVERWRITE, the latest value wins” 就是语义本身:最新的值胜出,旧值丢掉了,信箱是不排队的。这个取舍和队列是互补的,咱们要一个不丢就排队列,要最新的行情就用通知,遥测数据、心跳计数这一类的场景,中间那几个过期的值本来就没人要。

真正的要点在注释的中段。目标任务此刻可能睡在任何的地方,睡在信号量的等待里,睡在队列的排队里,睡在自己的定时觉里,而 `notify` 只会调一个 `ready`。目标睡在信号量上、信却投进了信箱的时候,这次唤醒对信号量的世界来说就是“叫错了人”的一次,可是没有关系的,咱们上面提炼的家族不变式在这里接住了它:目标醒来,重查自己睡下去的条件,发现自己要的信号量还是零,接着睡了回去。信箱的主人多查一步 `has_notify_`,查到了信,事情就办完了。误叫醒谈不上是设计的缺陷,它反而是整条设计成立的前提。

收信的这一侧,咱们看调度器里的探测:

```cpp
    // the current task reads its mailbox; taking the letter clears the flag.
    // ONE atomic probe only — the re-check loop lives in the system face,
    // whose sleep goes through the door the Mock can see (an inner
    // Scheduler-side loop would park via kernel-direct block and dodge it)
    std::expected<std::uint32_t, sync::SyncError> wait_notify() noexcept {
        irq::CriticalGuard guard{driver_};
        if (current_ != nullptr && current_->has_notify_) {
            current_->has_notify_ = false;
            return current_->notify_;
        }
        return std::unexpected(sync::SyncError::TimedOut);
    }
```

取信的动作把标记清掉了,信读走了信箱就空了,同一个值被读走了就不会再有第二遍。注释里的 “ONE atomic probe only” 后面跟着的,是本 commit 里我认为最有意思的一处取舍:重查的循环不放在调度器里,放在了系统接缝的那一层。它的原因写在括号里,内核侧的循环要是用 `block` 直接停车,host 测试的替身看不见这次睡眠,替身的钩子被绕开了,测试就没法在任务睡着的瞬间安排世界的变化。循环上提到了 `CortexM3System`,睡眠走的是接缝自己的 `sleep_for`,替身就能看见了。可测性就这样倒逼了分层,这个咱们在信号量那一篇见过一回,这里是咱们见的第二回。板上 `src/arch/arm_cortex_m3/system.cpp` 的实现就是探测加睡一小觉的循环:

```cpp
std::expected<std::uint32_t, sync::SyncError>
CortexM3System::wait_notify(clock::Ticks::tick_t span) {
    // re-check loop in the face: probe the mailbox, sleep through THIS
    // object's sleep_for (singletons stay private to the arm .cpp family)
    const auto ddl = system_time.current().tick_ + span;
    for (;;) {
        if (auto letter = system_sched.wait_notify(); letter.has_value()) {
            return letter;
        }
        const auto left =
            static_cast<clock::Ticks::tick_diff_t>(ddl - system_time.current().tick_);
        if (left <= 0) {
            return std::unexpected(sync::SyncError::TimedOut);
        }
        system_sched.sleep_for(system_sched.current_task(), system_time,
                               static_cast<clock::Ticks::tick_t>(left), wake_from_sleep);
    }
}
```

应用的入口在 `ThisTask::wait_notify(timeout)` 上,咱们看 `src/system/this_task_impl.hpp`,它把调用转发给了后端,零的意思是只试一次,正数的意思是最多等这么多个毫秒。`notify` 因此不用自己做任何中断安全的活:底下的 `ready` 只把任务放回就绪队列,更紧急的时候就把 PendSV 的切换请求挂起来,绝不在中断里亲手做切换的这个约定,是调度器出生那天就立好的全内核不变式,好处它白白继承了下来。

## 满环背压:发送方睡到有空位

队列这一侧也动了刀。`msg_queue.hpp` 在这一 commit 之前只有 ISR 友好的 `post`,环满了直接拒绝。中断用它是合适的,因为中断里的处理代码不能睡。可是任务与任务之间传数据的时候,消费的一方暂时慢了,生产的一方把“拒绝”当成了常态,数据就真的丢了。这一 commit 给任务侧补了带背压的发送,咱们把队列的两个信号量摆出来看:

```cpp
    SemaphoreBase<System> semaphore{0};       // units: "there is data"
    SemaphoreBase<System> spaces_{capacity};  // slots: "there is room"
```

一个信号量数的是“有数据”,初始的值是零,接收方在它的上面等。另一个数的是“有空位”,初始的值是容量,发送方在它的上面等。注释把两个计数器的语义都写在了行尾。发送的新版咱们整段读:

```cpp
    // Task-side post with back-pressure: on a full ring, SLEEP until a slot
    // frees or the time runs out. Never call this from an interrupt.
    QueueError post_for(const Message& m, clock::Milliseconds timeout) noexcept {
        if (spaces_.try_acquire(timeout) != SyncError::Ok) {
            return timeout.count == 0 ? QueueError::Full : QueueError::Timeout;
        }
        auto& sys = System::self();
        ZerOS::irq::CriticalGuard g{sys};
        ring_[(head_ + count_) & (capacity - 1)] = m; // slot is already reserved
        ++count_;
        semaphore.release(); // the unit is handed out only AFTER the data landed
        return QueueError::Ok;
    }
```

咱们把流程拆成三拍:向“有空位”要槽位的动作打头,要到了才动环,数据写进去了才放行一个“有数据”。要槽位的这一步是会睡的,环满的时候 `try_acquire` 在 `spaces_` 上睡到有人腾出了位子,注释里的 “SLEEP until a slot frees” 就是背压本身:生产者慢了下来,而不是数据被丢掉。腾位子的动作在 `pop_ring` 里,它把一个消息取了出来,跟着把 `spaces_.release()` 调了,注释写的是 “a slot just freed — sleeping senders may take it”,睡着的发送方按家族不变式被叫醒,各自重查了一遍,抢到槽位的走,没抢到的接着睡。

旧版 `post` 对外的行为保留原样:不睡、满环返回 `Full`,实现改成了转发给 `post_for` 配零超时的等效写法。注释里另有一句把边界画清了的话,“the ONLY post an ISR may call”。这里有个命名的小疙瘩我照实录给您:零超时用的常量名叫 `TRY_RECEIVE_TIME`,一个发送的路径复用了收信名字的常量,行为是对的,名字是歪的。我把它当素材留在了正文里,咱们读代码的时候,行为和命名谁说了算,这里是现成的一例。

同一个 commit 还把 `SyncError` 枚举从 `semaphore.hpp` 搬进了新的 `sync_error.hpp`,11 行的小文件,咱们读它的头注释,搬家的理由就写在里面,“the shared verdict of the synchronization family (and of anything that sleeps with a deadline — hence its own dependency-free home)”,共享的裁决配自己的家。事件组的实现要包含它,通知的路径也要它,信号量早就在用它了,谁都不该为了一个枚举去包含信号量的头,无依赖的共享声明配一个自己的头文件,家族的公共词汇从此有了自己的户口。

## host 考场:十四个目标与一句坦白

机制讲完了,咱们上考场。我在 `/tmp` 副本(`git archive 92a9aae` 导出,补上了 CMSIS 头)配了 host 构建,本机的 GCC 是 16.2.1,构建命令还是咱们的那三行:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j
ctest --test-dir build-host --output-on-failure
```

咱们跑出的结果是 14 个测试目标全部通过了,总耗时只花了 0.36 秒:

```text
100% tests passed out of 14

Total Test time (real) =   0.36 sec
```

三个新测试文件各自的成绩单,我单独念一遍,事件的成绩单是七用例二十四断言,通知的是五用例十九断言,队列的是十用例八十二断言:

```text
All tests passed (24 assertions in 7 test cases)
All tests passed (19 assertions in 5 test cases)
All tests passed (82 assertions in 10 test cases)
```

用例和正文的对应咱们挑几个看。AND 语义有专门的用例,它的名字是 “AND stays asleep until the WHOLE mask is lit”,只点亮了半个掩码时等待不返回,补上了第二个位才拿到 `0b0011`。通知的“最新值胜”也配了专测,连投了两个值只取一次,读到的是 2:

```cpp
    REQUIRE(FakeSys::sched_->notify(&w.task, 1));
    REQUIRE(FakeSys::sched_->notify(&w.task, 2)); // overwrites — by design
    auto got = w.wait_notify(Milliseconds{0});
    REQUIRE(got.has_value());
    CHECK(*got == 2);
```

背压的核心用例是 “post_for sleeps until a receive frees a slot (back pressure)”:咱们把环填满,发送方在 `post_for` 的调用里睡下去,替身的钩子趁它睡着替世界腾出了一个槽位,发送方醒来重查了一遍,拿到了槽位返回 `Ok`。伪唤醒的重查路径,就是这么在测试里真跑了一遍。

坦白的部分我原文搬给您,`test_event.cpp` 的结尾有作者自己写的一句话:

```cpp
// Multi-waiter broadcast (two tasks waking from ONE set) cannot park two
// waiters on this single-threaded Mock — left to the Renode demo / silicon.
```

两个任务从同一次 `set` 的广播里醒来,这个场景在单线程的替身上摆不出来,替身一次只能伺候一个停车的人。作者把这段路径明明白白留给了仿真,咱们等下就去 Renode,不过得把话说在前头:demo 里挂在这根事件链上的等待者也只有一个,能看的是单等待者的广播,严格意义上的多等待者广播,host 和 Renode 两边都没有真的跑到,验收的一节咱们再对这件事负责任。

## Renode 剧场:双钥匙与三段接力

板上的活儿我在同一个 `/tmp` 副本里做的,交叉工具链用的是 arm-none-eabi-g++ 16.2.0,固件的体积是真实的:

```text
   FLASH:        7788 B        64 KB     11.88%
             RAM:        4480 B        20 KB     21.88%
   text	   data	    bss	    dec	    hex
    7112	     676	   3800	  11588	   2d44
```

RAM 用到了 4480 字节,大头是接收队列、几个新任务的栈和更长的等待链,FLASH 也过了 6KB 的预算线,服务面铺开的代价就摆在了体积表里,我照实报数。Renode 1.17.0 认不出 repl 里的 `I2C.STM32F4_I2C`,我把 i2c1、i2c2 两块整段删了,demo 的路径不经过 i2c,行为不受影响的,处理跟咱们的旧例一致。按键的注入走的是 Renode 的 `sysbus.usart1 WriteLine`,串口的输出写进了 run-keys.log 这样的文件里,下面的每一行都是真实输出。

### 按 e 与 f:两把钥匙的互锁

demo 里的 `e` 与 `f` 是一把双钥匙的锁。咱们按下了 `e`,它就孵出了等待任务,它的等待掩码是 `0b0011`,AND 的读法,超时给足了 5 秒,然后 `e` 自己点亮了第 0 位:

```cpp
constinit ZerOS::sync::Event g_keys{};
alignas(8) constinit std::uint32_t stack_evt[96] = {}; // wait chain + format buffer
                                                       // ate 6 canary words at 64 — caught live

void key_waiter(void*) {
    auto got = g_keys.wait(0b0011, true, Milliseconds{5000});
    if (got.has_value()) {
        ZerOS::board::format("evt: both keys in (bits=0x{})\r\n", ZerOS::log::Hex{*got});
    } else {
        ZerOS::board::print("evt: nobody pressed the second key\r\n");
    }
    ThisTask::block(); // self-retire
}
```

串口窗的真实输出,咱们裁成两行一对:

```text
[INFO] usart1: [host:  1.42s (+4.55ms)|virt:    1.3s (+2.78ms)] c: waiter out, needs BOTH keys (got bit0)
[INFO] usart1: [host:   2.71s (+0.9ms)|virt:   2.3s (+16.62µs)] evt: both keys in (bits=0x00000003)
```

第一行是任务 `c` 的台词,它就是 demo 里读按键的那个任务,它替咱们报出了等待者已上场、拿到了第 0 位。等待者在虚拟时间 1.3 秒挂上了等待链,此时第 0 位亮了、第 1 位还灭着,`satisfied_or` 查了一遍,`hit != mask`,AND 的条件不成立,它睡回去了。我按 `f` 的动作在虚拟时间 2.3 秒前后才落到了串口上,`set(1u << 1)` 点亮了第 1 位,广播把等待者叫醒了,它回到了循环的头顶重查,`hit == mask`,交回了 `0x00000003`。打印里的 8 个十六进制位是格式化器按 `Hex` 的宽度补的零,数值就是掩码的 `0b0011`。只按了一半钥匙它不出门,等满了 5 秒,它就报了 “nobody pressed the second key”,这个分支咱们没按到,它留给您亲手试。

### 按 n:定时器在 tick 上下文直接投信

`n` 键的三段接力,是本篇的三个原语在同一个剧本里的一次合演,咱们把源码的接线看清楚了再看输出:

```cpp
void mail_filler(void*) { // runs in TICK context — notify is safe there
    ZerOS::system::os().notify(g_nt, 42);
}

void mailbox_task(void*) {
    auto letter = ThisTask::wait_notify(Milliseconds{1000});
    if (letter.has_value()) {
        ZerOS::board::format("nt: letter says {}\r\n", ZerOS::log::Dec{*letter});
    } else {
        ZerOS::board::print("nt: mailbox stayed empty\r\n");
    }
    ThisTask::block(); // self-retire
}
```

按了 `n` 之后,任务 `c` 孵出了收信任务,它调了 `wait_notify` 停在自己的信箱上,等待的时限是 1000 毫秒。`c` 接着挂了一个 300 毫秒的单次定时器,回调就是咱们刚看过的 `mail_filler`。300 毫秒的期限一到,SysTick 的中断里时间内核就把回调调了起来,它跑在 tick 的上下文里,一句话把信投进了任务 `g_nt` 的信箱,信的值是 42。`notify` 在中断里是安全的,咱们在直接通知的那一小节确认过它的全部路径。串口的真实输出两行:

```text
[INFO] usart1: [host:  3.42s (+7.26ms)|virt:   3.31s (+7.02ms)] c: mailbox task out, timer writes in 300ms
[INFO] usart1: [host:  3.72s (+7.24ms)|virt:      3.61s (+7ms)] nt: letter says 42
```

咱们对着虚拟时间轴读,两行相差的正好是 300 毫秒,信到的时间不多不少。这个剧本里没有信号量也没有队列的戏份,一个 `uint32_t` 从定时器的回调直落收信任务的信箱,三段接力各干各的:任务只管等信,定时器只管到点开火的活,`notify` 只管投递的事。它也是 tick 上下文能直接给任务递数据的一个样板,中断里想递信的话,您照这个写法来就行。

## 金丝雀活捉:六个词逼出 96

上一站的栈护卫在 demo 里还挂着岗,巡逻长依旧坐在 `context_switch` 的必经之路上。它在本站又立了一功,功勋写在了 `stack_evt` 的行尾注释里:“wait chain + format buffer ate 6 canary words at 64 — caught live”,等待链加格式化缓冲在 64 词的栈上吃掉了 6 个金丝雀词,当场被抓了。这句话是复现的结果,过程咱们可以亲手走一遍,我在 `/tmp` 副本里把 `stack_evt` 从 96 词缩回了 64 词,重新做了交叉构建,再按了一次 `e` 和 `f`:

```text
[INFO] usart1: [host:  3.43s (+13.5ms)|virt:   3.31s (+13.24ms)] evt: both keys in (bits=0x00000003)
[INFO] usart1: [host:   3.43s (+0.3ms)|virt:    3.31s (+1.01µs)] [STACK OVERFLOW] task=evt eaten=6
```

溢出的报告在 “both keys in” 那行喜讯之后 1 微秒就冒了出来,`eaten=6`,被吃的金丝雀词数与注释里记的数字严丝合缝。复现的剧本值得咱们体会一下:溢出本身没有触发任何的异常,等待任务把两把钥匙的喜讯都打印出来了,程序看起来是一切正常的,直到下一次的上下文切换,巡逻长查过了栈底,6 个哨兵词变成了 0,溢出才现了形。栈深 96 这个数字就是这么量出来的,64 词的真实使用加 6 词的越界,再加一段的余量,它不是拍的。金丝雀的巡逻只查得出来,救是救不回来的,报告之后 demo 就地 `wfi` 停机,这是上一站定下的老处理,到了本站也没有变过。

## 验收

咱们把本篇可查证的事实归拢一下,一件一件地来。

第一件是 host 的考场。您 checkout 到 `92a9aae`,按上面的三条命令配好构建,14 个测试目标全部通过了,事件、通知、队列三份新的成绩单,分别是事件的七用例二十四断言,通知的五用例十九断言,队列的十用例八十二断言。AND 的半掩码不出门、最新值覆盖旧值、满环的发送睡到腾位,三样核心行为都有各自的专测,替身的钩子在任务睡着的瞬间替世界动手,重查的路径走的是生产代码。

第二件是板上的剧场。固件的体积是 FLASH 7788 字节、RAM 4480 字节,双钥匙的输出在虚拟时间 2.3 秒,`bits=0x00000003`,三段接力的输出在虚拟时间 3.31 与 3.61 秒,间隔了 300 毫秒,信的值是 42。您复现的时候记得把 repl 里的两块 i2c 删掉,日志用 `renode --console --disable-xwt -e "logFile @run-keys.log" -e '$bin=@build/src/board/stm32f103_bluepill/zeros-bluepill.elf' -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" -e "start"` 的命令起跑,键用 `sysbus.usart1 WriteLine "e"` 的形状喂进串口,e、f、n 三下在 `start` 之后各隔一秒地喂入,输出落进了咱们留给它的 run-keys.log 里。

第三件是没验的,我照实摆。多等待者的广播,两个任务从同一次 `set` 的广播里醒来,这样的路径替身到不了它的现场,板上的现实也够不着,源码的走读看得到广播循环本身,可它没有在任何一个环境里真的跑过两个等待者。`post_for` 的错误映射里,零超时的时候返回 `Full`,非零超时的时候返回 `Timeout`,行为都有专测的覆盖,常量名 `TRY_RECEIVE_TIME` 的复用是源码里的实况,我原样报告。事件组的广播对中断的安全性,注释里声称了 TASK/ISR safe,临界区的存在支撑了它的说法,不过板上 demo 的 `set` 全部发生在任务的上下文里,ISR 里调 `set` 的路径同样没有实测过。

## 下一篇

同步家族到这一篇齐装了,咱们手上有了信号量、互斥锁、队列、工作者、定时器、事件组、直接通知,七样原语排成了一排,家族不变式是它们共享的底座。姊妹篇《数到十就换人:同级时间片与不继承的切片债》与本篇并行,它管调度器那半边的公平,咱们这篇管同步这半边的收束,两篇合了起来,咱们本站“公平与收束”的名号才站得住。往后 example 要拆成一个个独立的示例工程,CI 也要并进咱们的流程里来,那些是下一站的事,咱们到时候见。
