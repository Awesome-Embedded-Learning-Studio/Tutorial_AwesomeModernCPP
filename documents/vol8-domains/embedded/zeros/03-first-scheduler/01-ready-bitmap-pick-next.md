---
title: "谁最急,谁上场:就绪位图、任务三态与选人仲裁"
description: "咱们在 host 上复现 0bf9461 的真实编译破:commit message 写着 Sched OK,测试桩却缺 lock/unlock,Switchable 概念当场把 static_assert 判死;照概念亲手补两个空函数,ctest 全绿。正文写仲裁面:TaskState 三态、32 级就绪位图加每级 FIFO、pick_next 与 yield 的不对称入队(被抢占者回队头、让出者去队尾)、RBIT+CLZ 单周期选人,选人的机制在 objdump 反汇编里当面对质"
chapter: 3
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
reading_time_minutes: 22
prerequisites:
  - "一秒钟的定义权:tick、回绕算术与到期队列"
related:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
---

# 谁最急,谁上场:就绪位图、任务三态与选人仲裁

上一站收尾的时候,时间内核已经在 host 和板子两头都跑通了,Renode 里的 ticker 每秒给咱们打一行。不过您把当时的 `main` 再回想一遍,就会发现一个事实:周期到了要干什么,是回调函数直接在 SysTick 的中断上下文里执行的,程序里没有任务的概念,CPU 打印完了就回 `main` 待着。谁在用 CPU 的答案,咱们在当时的代码里找不出一个主人来。

`0bf9461` 把这个答案改掉了。它一口气交了 15 个文件,新增了 674 行,删除了 31 行,subject 写的是 “feat: Sched OK”,调度器内核从此进了场,仓库从时钟库变成 RTOS 的改变就发生在咱们眼前。它交的东西太多,咱们把同一份 diff 分成两篇来讲:咱们今天管仲裁面,看下一个 CPU 归谁的决定是怎么做出来的。决定做好了之后硬件怎么真的切过去,伪造栈帧、PendSV、SVC 这些汇编的活儿,归的是下一篇《在栈顶伪造出生证明》。开讲之前有一个事实咱们得摆在桌面上,这个 commit 的 host 测试当时是编不过的,咱们会现场复现它,再照仓库自己的修法补好,最后把测试跑绿了。

## 任务的三种状态和它的档案卡

咱们新建的 `types.hpp` 全文只有 11 行,它的职责是给任务的状态定下词汇:

```cpp
#pragma once
#include <cstdint>

namespace ZerOS::sched {
/**
 * @brief State is the Task State
 *
 */
enum class TaskState : std::uint8_t { Ready, Running, Blocked };
using TaskPriority_t = std::uint8_t;
} // namespace ZerOS::sched
```

这里一共列了三种状态,咱们一个个说清它们各管什么。`Ready` 表示任务万事俱备了,就差一个空出来的 CPU。`Running` 表示它此刻真的在 CPU 上跑着。`Blocked` 表示它在等一件没发生的事,比如等时间到了再干活,或者等一个字节的到来,就算 CPU 闲下来了也不归它。`TaskPriority_t` 用的是 `std::uint8_t`,一个字节的优先级编号,数值越小的越紧急,咱们把这个约定记牢,后面所有的判断都建立在它上面。

状态得挂在什么东西上,咱们看 `task_control_block.hpp` 里的 TCB(Task Control Block,任务在内核里的档案卡,咱们后面都叫它 TCB):

```cpp
struct TCB : public base::SelfNode<TCB> {
    using StackPointer_t = std::uint32_t*;
    StackPointer_t stack_pointer_{}; // always be the first

    TaskPriority_t task_priority_{};
    TaskState state_{};

    const char* name_{}; // Constant name's for debugings

    clock::TimeWaiter action{};
    UserTaskWrapper task_wrapper_{};
    std::span<std::uint32_t> stack_view_{};
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(TCB, stack_pointer_) == sizeof(base::SelfNode<TCB>),
              "stack_pointer_ must stay immediately after the SelfNode base");
#pragma GCC diagnostic pop
```

咱们从上往下过一遍成员,每个都有它的用处。`stack_pointer_` 是任务的栈指针,注释喊出的口号是“always be the first”,下面的 `static_assert` 把它的位置定在了 TCB 的开头:它必须紧跟在 SelfNode 基类后面,而 SelfNode 就是那个背在自己身上的 `next_` 指针。作者为什么这么狠?因为 PendSV 切换的汇编代码要按固定偏移摸到栈指针,布局错了汇编不会报错,任务就死得莫名其妙了,所以咱们让编译器替咱们守着布局,谁动了字段顺序,固件就连编译都过不去了。

`task_priority_` 和 `state_` 就是上面两个类型的落地。`name_` 是给调试用的常量字符串。`action` 的类型咱们上一站见过,是个 `TimeWaiter`:任务要睡觉的时候,咱们把它内嵌的等待者挂进时间内核的到期队列,时间到了回调就把任务叫醒。内存布局上它就长在 TCB 的身上,所以从到期记录反推任务只是一次指针减法,不用额外的映射表,咱们在验收的时候还会见到这个反推的手法。

`task_wrapper_` 装的是任务函数和参数,`stack_view_` 是任务栈的 `span` 视图。`UserTaskWrapper` 里有个静态函数值得您多看一眼:

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

注释把用意说了:它刻意做成静态成员,走的是普通 ABI,不带隐藏的 `this` 参数,这样异常返回的汇编可以直接跳进它当跳板。跳板怎么架、异常帧怎么伪造,这些是下一篇的主菜,咱们今天不抢它的戏。

## 给调度器立的合同:两个 concept

调度器的正式名字是 `Scheduler<Driver>`,它对送进来的驱动有两份要求。咱们打开 `is_scheduler.hpp` 看:

```cpp
template <typename SwitchPort>
concept Switchable =
    irq::CriticalSection<SwitchPort> && requires(SwitchPort& p) { p.request_switch(); };

template <typename Stuff>
concept IsScheduler = requires(Stuff& stuff, base::BorrowedPtr<TCB> aTask) {
    // A Scheduler can add a task
    { stuff.add(aTask) };
    // A Scheduler can tell current task is what
    { stuff.current_task() } -> std::same_as<base::BorrowedPtr<TCB>>;
    // A Scheduler can tell what task should be executed next
    { stuff.pick_next() } -> std::same_as<base::BorrowedPtr<TCB>>;
    { stuff.ready(aTask) }; // Wake up task
    { stuff.block(aTask) }; // Block the task
    { stuff.yield() };      // Yield or hang a task
};
```

`Switchable` 是复合的:咱们要求驱动得会调 `lock` 和 `unlock`(它组合了咱们上一站讲过的 CriticalSection 概念),还得会做 `request_switch` 的请求,它请求的就是一次任务切换。组合上一站的概念而不重新声明,合同就没有第二份拷贝了,时间内核认的锁和调度器认的锁始终是同一把。`IsScheduler` 更像一份能被 `static_assert` 检查的文档:里面是五个动作加两个返回类型的清单,注释一行一条地写着,咱们读概念就跟读接口说明一样。这个 commit 的测试就靠它们俩站岗,咱们到坦白的段落会看到它们怎么把编译拦下来。

## 32 级就绪队列:一张位图加 32 条 FIFO

合同立好了,咱们看 `scheduler.hpp` 的家底:

```cpp
    static constexpr TaskPriority_t kPriorityLevels = 32;
    static_assert(kPriorityLevels <= 32, "one-word bitmap fast path requires <= 32 levels");
    static constexpr TaskPriority_t kMaxPrio = kPriorityLevels;
    static constexpr TaskPriority_t kIdlePrio = 0xFF;

    DISABLE_COPY_MOVE(Scheduler);

    void push_back(TCB* t) {
        if (t->task_priority_ >= kMaxPrio) {
            return;
        }
        t->state_ = TaskState::Ready;
        fifo_[t->task_priority_].insert(t);
        present_.set(t->task_priority_);
    }

    TCB idle_{};
    TCB* current_{nullptr};
    base::Bitmap<kPriorityLevels> present_{};
    base::SelfList<TCB> fifo_[kPriorityLevels]{};
    [[no_unique_address]] Driver driver_{};
```

就绪结构分了两层,咱们一层一层来看。底层的 `fifo_` 是一张表,里面装了 32 条 `SelfList<TCB>`,一条对应的是一个优先级。`SelfList` 咱们上一站讲过,不指定比较策略的时候它就是纯 FIFO,早来的排在前面,所以同级的任务按到达次序轮。上层的 `present_` 是一张 32 位的 `Bitmap`,某个级的队列里有人的时候位就是 1,空了就清 0。咱们要找还剩哪些级有人的时候,咱们不用把 32 条链挨个问一遍,位图一句话就回答了咱们,选人能快的全部底细就在这里。

`static_assert` 的那句话值得咱们停一下:位图快路径要求级数不超过 32,因为一张机器字正好装下了全部标记。作者把设计的前提写成了编译期断言,哪天有人把级数扩到了 64,编译器就当场拦住了他,改掉快路径的事就躲不掉了,设计约束和实现绑在了一根绳上。

`push_back` 开头的判断是 idle 任务剧本的关键。idle 是调度器自己内嵌的任务,优先级给的是 `0xFF`,不小于 32 的优先级都会在 `push_back` 被拒收,所以 idle 永远进不了就绪队列。它出现在哪?咱们看选人:

```cpp
    TCB* pop_highest() {
        const auto level = present_.find_first_set();
        if (level == decltype(present_)::npos) {
            return nullptr;
        }
        auto t = fifo_[level].pop_head();
        if (fifo_[level].empty()) {
            present_.clear(level);
        }
        return t.get();
    }
```

`pop_highest` 问位图要最低的非空级,从那个级的队头弹出一个人,级空了就把位清掉。全空的时候它交回空指针,而 `pick_next` 拿到空指针就返回内嵌的 idle。这么说吧,idle 是不占队列位子的,它只在所有真任务都睡去的时候被 `pick_next` 兜底请出来。idle 的任务体是 `wfi`(等待中断的指令),CPU 没活干的时候就去打盹,中断来了再被拉起来,上一站收尾时 main 的循环就剩一句 WFI,当时咱们没有展开,它在这里成了正式的主人。

### 位图怎么一句话找到最低的 1

`find_first_set` 的实现住在 `bitmap.hpp` 里,咱们把相关的两个函数放在一起看:

```cpp
    /// Index of the lowest set bit; x must be non-zero.
    /// GCC/Clang lower this to RBIT+CLZ on Cortex-M3/M4.
    constexpr std::size_t ctz(std::uint32_t x) {
#if defined(__GNUC__) || defined(__clang__)
        return static_cast<std::size_t>(__builtin_ctz(x));
#else
        return zeros_impl::_ctz(x);
#endif
    }

    /// First set bit, npos if none. Tail word is naturally safe: padding 0s never fake-hit.
    [[nodiscard]] constexpr std::size_t find_first_set() const {
        for (std::size_t w = 0; w < WORDS; ++w) {
            if (words_[w] != 0) {
                return (w << 5) + ctz(words_[w]);
            }
        }
        return npos;
    }
```

`__builtin_ctz` 数的是最低位的 1 后面跟着几个 0,在位图里它就是最低的非空级编号。注释预告了硬件上的下场:到了 Cortex-M3/M4 上,GCC 和 clang 会把它降成 RBIT 加 CLZ 的两条指令。RBIT 把 32 位整个倒了过来,最低的 1 就变成了最高的 1。咱们再让 CLZ 去数最高位前面有几个 0,数出来的正好是原数里最低那个 1 的位置。两条指令在 M3 上都是单周期的,所以“问位图要最紧急的级”的动作,板上花的就是两个周期。

注释咱们照单收下了,板上的汇编还得自己验。我把 `__builtin_ctz` 包在一个实参为 `unsigned int` 的最小函数里,扔给了 arm-none-eabi-g++ 16.2.0,目标设成 Cortex-M3 的 `-mcpu=cortex-m3 -mthumb -O2`,objdump 出来的符号是 `_Z3ctzj`。咱们照实说一句,反汇编出来的东西不是我拿仓库文件原样编译的,它是我自己包的同形函数,形参和返回的类型都跟仓库的 `ctz` 对得上。objdump 给咱们的输出如下:

```text
00000000 <_Z3ctzj>:
   0:	fa90 f0a0 	rbit	r0, r0
   4:	fab0 f080 	clz	r0, r0
   8:	4770      	bx	lr
   a:	bf00       	nop
```

咱们数下来是三条指令,其中 rbit 和 clz 是干活的两条,bx lr 是负责返回的,注释说的和硅片做的一致。 nop 只是填充对齐的占位,咱们没把它算进指令数里。位图注释里的那句 “one-word bitmap fast path”,咱们说的 fast 指的就是它。

## pick_next:被抢占的回队头,让出去的排队尾

咱们的主角登场了,咱们把 `pick_next` 和 `yield` 摆在一起看,它们俩合起来才是公平性设计的本体:

```cpp
    [[nodiscard]] base::BorrowedPtr<TCB> pick_next() {
        irq::CriticalGuard guard{driver_};
        if (current_ != nullptr && current_ != &idle_ && current_->state_ == TaskState::Running) {
            push_front(current_);
        }
        TCB* next = pop_highest();
        if (next == nullptr) {
            next = &idle_;
        }
        current_ = next;
        next->state_ = TaskState::Running;
        return next;
    }

    void yield() {
        irq::CriticalGuard guard{driver_};
        if (current_ == nullptr || current_ == &idle_) {
            return;
        }
        push_back(current_);
        driver_.request_switch();
    }
```

`pick_next` 开头的判断就是不对称的来源。当前任务的状态还是 `Running`,说明它自己没有让出 CPU 的意思,是被人抢了位子,咱们把它 `push_front` 塞回它那个级的队头,轮到它的时候它就接着原来的位置跑,什么都不用重算。而 `yield` 里当前任务走的是 `push_back` 去队尾,同级的伙伴按次序一个一个轮,让出去一次就排到了最后。回来的位置完全由回来的原因决定,同样都是回到就绪队列的,被抢占者接着干它的活,让出者就得重新排队了。

`push_front` 本身是 `0bf9461` 新增的,咱们查过 diff,`self_list.hpp` 在这个 commit 里就多了这 5 行:

```cpp
    void push_front(BorrowedPtr<Stuff> new_stuff) {
        new_stuff->next_ = stuff;
        stuff = new_stuff.get();
    }
```

公平性的需求逼出了容器的新接口,咱们顺着 diff 读的时候能看到需求和接口的对应关系,比读一份凭空设计好的容器有意思得多。

选人的决定做完了,切换呢?您留意 `pick_next` 和 `yield` 全程没有碰过任何寄存器,它们最多调了一下 `driver_.request_switch()`,而板上的这个函数只是把 PendSV 的挂起位写 1。调度 API 在咱们这儿从来不在调用点直接切,真正的切换被推迟到了 PendSV 异常里执行,这样任何调用点(包括中断服务程序在内的调用点)都只发出了请求,不在别人的栈上动手。汇编那边的事归《在栈顶伪造出生证明》,咱们今天只要知道请求是安全的就够了。

`add`、`ready`、`block` 三个原语咱们快速过一遍,它们的判断条件各有讲究:

```cpp
    void add(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t) {
            return;
        }
        push_back(t.get());
        if (current_ != nullptr && t->task_priority_ < current_->task_priority_) {
            driver_.request_switch();
        }
    }

    void ready(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t || t->state_ != TaskState::Blocked) {
            return;
        }
        push_back(t.get());
        if (current_ != nullptr && t->task_priority_ < current_->task_priority_) {
            driver_.request_switch();
        }
    }

    void block(base::BorrowedPtr<TCB> t) {
        irq::CriticalGuard guard{driver_};
        if (!t) {
            return;
        }
        t->state_ = TaskState::Blocked;
        if (t == current_) {
            driver_.request_switch();
        }
    }
```

三个函数都握住了临界区再动队列,咱们得想一下为什么要这样:`ready` 会从中断里被调,ISR 和任务两边共享的是位图和 FIFO,不加锁的队列迟早会被写坏。`ready` 开头只收 `Blocked` 态的任务,没睡的任务来 `ready` 一律不理,这个判断往后还有个重要的故事:它挡住的场景里恰好埋着一个真正的丢唤醒竞态,咱们在讲丢唤醒的那一篇,也就是《睡死那个 bug:丢唤醒、park_for 与让错误用法编不过的建造者链》里,当着它的面把它复现出来再修掉。`request_switch` 的条件三处都是同一个逻辑,新来的比当前的紧急,或者睡的是当前自己,才轮得到一次切换的请求。不紧急的时候就只入队,切换的请求咱们就不发了。能不切的时候咱们就不切,毕竟每次切换都是有代价的。

## 坦白:commit 说 OK,host 测试当时编不过

这里有一段必须照实交代的事。仓库的提交信息写着 “Sched OK”,可是咱们 checkout 到 `0bf9461` 之后按 README 的路子配 host 测试构建,咱们看到的第一个报错来得又快又直接:

```text
/tmp/zeros-t0bf9/test/test_sched.cpp:33:15: error: static assertion failed
   33 | static_assert(Switchable<HostSwitchPort>);
      |               ^~~~~~~~~~~~~~~~~~~~~~~~~~
  • constraints not satisfied
    • required for the satisfaction of ‘CriticalSection<SwitchPort>’
      [with SwitchPort = <unnamed>::HostSwitchPort]
    • the required expression ‘t.lock()’ is invalid
```

原因咱们对着源码就能看清,测试文件里的替身端口只写了切换要用的部分:

```cpp
struct HostSwitchPort {
    void request_switch() { ++g_switch_requests; }
};
```

而 `Switchable` 的要求是复合的,咱们还得给替身写上 `lock` 和 `unlock` 两个函数,当时的替身一个都没写。concept 的检查就在 `static_assert` 那一行把编译拦下了,后面 `Scheduler<HostSwitchPort>` 的每一处使用跟着全炸。我本机的 GCC 16.2.1 给出的完整报错里,这样的约束失败重复了四处,连带 `s.add` 被推断成 `int` 类型的一串误会,报错刷了小半屏。

咱们得说句公道话,仓库其实没有坏,坏掉的是时间线本身:替身的 `lock` 和 `unlock` 要到下一个 commit(`6308dd0`)才补上,“Sched OK” 那天验证的对象是真正的开发板上那套 PendSV 路径,host 测试当时就是编译不过的半成品。工程的真实节奏有时候就是这样,commit message 里的 OK 说的是板子,咱们不能替它扩写成“全部绿灯”。

咱们要自己走通,修法就照下一个 commit 的官方答案:给替身补两个空函数。host 上咱们用不着真锁,函数形状对了,concept 就放行了:

```diff
 struct HostSwitchPort {
     void request_switch() { ++g_switch_requests; }
+    void lock() {}
+    void unlock() {}
 };
```

咱们重新构建,再跑一遍全部的测试:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

我本机还是那套 GCC 16.2.1,跑出来的真实结果里,七个目标全部通过了:

```text
100% tests passed out of 7

Total Test time (real) =   0.12 sec
```

调度测试自己的成绩单,咱们看一眼:

```text
All tests passed (42 assertions in 9 test cases)
```

234 行的测试文件,9 个用例、42 条断言都照原样跑了下来,咱们补的两个空函数没有改任何断言,跑出来的就是仓库自己写的验收。

## host 考场:三个用例对证仲裁面

测试里的用例跟咱们的正文一一对得上座,咱们挑三个看,它们恰好各管仲裁面的一根柱子。

咱们数的第一根柱子,是数值小者优先的约定。咱们准备了三个任务,优先级分别给的是 1、3、0,把它们加进了调度器,调了一次 `pick_next`,出来的必须是 0 号的 c:

```cpp
TEST_CASE("pick_next takes the highest priority; add marks Ready", "[sched]") {
    g_switch_requests = 0;
    Scheduler<HostSwitchPort> s;
    TCB a = task(1, "a"), b = task(3, "b"), c = task(0, "c");

    s.add(&a);
    s.add(&b);
    s.add(&c);
    CHECK(a.state_ == TaskState::Ready);
    CHECK(b.state_ == TaskState::Ready);
    CHECK(c.state_ == TaskState::Ready);

    auto t1 = s.pick_next();
    REQUIRE(t1.get() == &c);
    CHECK(c.state_ == TaskState::Running);
    CHECK(s.current_task().get() == &c);

    s.block(&c);
    auto t2 = s.pick_next();
    REQUIRE(t2.get() == &a);
}
```

咱们看用例的后半段,它还捎带验了次序:0 号的 c 睡了之后,轮到的是 1 号的 a 而不是 3 号的 b,位图给出的级次序和优先级数值的约定咬合得上。

第二根柱子管的是不对称入队里让出者去队尾的那一半,咱们看用例:

```cpp
TEST_CASE("same level round-robins via yield-to-tail", "[sched]") {
    g_switch_requests = 0;
    Scheduler<HostSwitchPort> s;
    TCB a = task(1, "a"), b = task(1, "b"), d = task(1, "d");

    s.add(&a);
    s.add(&b);
    s.add(&d);

    REQUIRE(s.pick_next().get() == &a);
    s.yield();
    REQUIRE(s.pick_next().get() == &b);
    s.yield();
    REQUIRE(s.pick_next().get() == &d);
    s.yield();
    REQUIRE(s.pick_next().get() == &a);
}
```

同级的是三个任务,谁让出了,下一个就轮到了谁,转满一圈又回到了 a。咱们想象一下把 `yield` 改成回队头会发生什么:每次出场的还是 a,b 和 d 就要永远饿肚子了,所以那 5 行 `push_front` 无论如何不能被 `yield` 借走。

咱们数的第三根柱子,是被抢占者回队头的做法,用例的名字就叫 preempted task resumes from the head of its level:

```cpp
    s.add(&a);
    REQUIRE(s.pick_next().get() == &a);

    auto before = g_switch_requests;
    c.state_ = TaskState::Blocked;
    s.ready(&c);
    CHECK(switches_after(before) == 1);

    REQUIRE(s.pick_next().get() == &c);
    CHECK(a.state_ == TaskState::Ready);

    s.block(&c);
    REQUIRE(s.pick_next().get() == &a);
    CHECK(a.state_ == TaskState::Running);
```

咱们把剧情捋一遍:a 的优先级是 1,它已经在 CPU 上了,0 号的 c 被 `ready` 唤醒了,切换请求计了 1 次,因为新来的更紧急,抢是应该的。a 被 `pick_next` 塞回了队头,所以 c 睡了之后下一个还是 a,而且它的状态断言是 `Running`,它在 CPU 上接着自己原来的位置跑。9 个用例里还覆盖了唤醒同级不请求切换、全阻塞时 idle 接管的场景,也有只阻塞非当前任务不请求切换的边角。剩下的用例里还有一个专门验睡眠唤醒的,咱们在里面拿 owner_of 的指针减法把到期记录反推回了 TCB,档案卡一节留下的承诺在这里兑现了。更细的咱们就不逐个念了,您跑一遍就能全见着。

## 验收

咱们把本篇的可查证事实归拢一下,一共归出了三件,咱们一件一件来。

第一件是编译破的复现与修复。您 checkout 到 `0bf9461`,按上面的三条命令配好 host 构建,第一个报错应该和坦白段落贴的原文一字不差。补上了两个空函数之后,您的 `ctest` 应该给出 7 个全部通过。您跑出来的数字应该跟我的一样,调度测试给出的就是 9 个用例、42 条断言。

第二件是 RBIT 加 CLZ 的对质。注释的说法在 `bitmap.hpp` 里,注释里写的那句 “GCC/Clang lower this to RBIT+CLZ” 是仓库自己的话,后面还注明了平台是 Cortex-M3/M4。咱们拿 arm-none-eabi-g++ 16.2.0 以 `-mcpu=cortex-m3 -mthumb -O2` 编了最小的 `__builtin_ctz` 函数,objdump 给咱们出来的是三条指令,干活的是 rbit 加 clz 两条,反汇编原文在位图的段落里,您拿同样的命令就能复现同样的输出。

第三件是本篇没验的东西,我也照实说:仲裁的决定做出来了,但任务真的被切过去这件事,咱们在 host 替身里是验不到的,那里的 `request_switch` 只是计数器加一而已。伪造异常帧、PendSV 里的汇编保存恢复,这些要等咱们下一篇在 Renode 里看 A、B 两个任务按 1000 毫秒和 700 毫秒的节奏交错打印,咱们才算验完。仲裁面和机制面合在一起的时候,才是 `0bf9461` 的完整验收。

## 下一篇

仲裁面咱们讲完了,手里有了一张会选人的调度器,可是它还从来没真的把 CPU 从一个任务手里交到另一个任务手里。下一篇《在栈顶伪造出生证明》进入同一份 diff 的机制面:硬件异常帧的八个字、`fabricate_frame` 怎么在新任务的栈顶伪造出第一次上下文、`svc 0` 首切跳板和 naked 汇编里的保存恢复,还有上一站欠着的那个问题,`init_time` 里 SysTick 的优先级为什么从 `0xF0` 改成了 `0xFF`,跟 PendSV 排在了同一级。咱们那儿见。
