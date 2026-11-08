---
title: "一秒钟的定义权:tick、回绕算术与到期队列"
description: "这一篇写时间子系统:Ticks 值类型用带符号差值让 uint32 回绕后照样比早晚,SelfList 侵入式链表按截止时间排序、同期限 FIFO,Kernel<Driver> 模板配 BASEPRI 嵌套临界区,同一份代码在 host mock 与 Blue Pill 上各跑一遍;坦白 54817f3 那站 host 测试因缺 self_list.hpp 编不过,checkout 4f12867 后 340 行时间测试全部通过,Renode 里周期 ticker 仿真时间每秒打印一行"
chapter: 2
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - 寄存器
  - cpp-modern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "没有堆的世界·第 4 篇:Make/Destroy:类型化的出生入死与编译期防线"
related:
  - "没有堆的世界:先把内存发对"
---

# 一秒钟的定义权:tick、回绕算术与到期队列

内存的悬念上一站就收了尾:咱们给 `buffer_` 补了一对花括号,`constinit` 的悬念也就随之放行了,实验的全文留在了上一站的收尾里。从本站起咱们进入正题。内存管的是“东西放在哪”,内核接下来要回答的是“什么时候做”。这一篇咱们把时间子系统的两块地基立起来:一块是 tick,咱们把“一秒”的定义权收进一个值类型里。另一块就是咱们的到期队列,咱们让一批截止时间排好了队,回绕了也不乱。

内存有了,内核的下一个真实职责是时间。不过动笔谈时间之前,咱们在手边做一个小实验,它牵着内存栈收尾的一个尾巴,咱们把它做完了再进入正题,因为实验里那个问题正好由这篇要交的代码给出答案。

## Ticks:把一秒钟装进一个类型

咱们要立的新类型叫 Ticks,它的新家是 `include/ZerOS/kernel/clock/ticks.hpp`,整个文件的行数是 33 行,咱们把它拆成三段,一段一段地过。不过在进文件之前,有三个问题是得立在前面的,它们就是这个类型存在的理由。头一个问题:一秒钟在芯片上是什么?芯片上是没有钟表的,定时器硬件会做的事只有数拍子一件,咱们给时钟配一个节拍,数出来的每一个 tick 就是一段定死的时长,所以咱们嘴里的“一秒”,数到底是一千拍的约定,数拍子的活,归咱们的新类型 Ticks 管。第二个问题:裸的 `uint32_t` 会在哪儿出事?它的毛病是不记自己的身份,装的是时刻还是一段跨度,从数值上是看不出来的,用的人只能靠注释跟人脑来记。

第三个问题:为什么这个错一到回绕就变成难查的病?数拍子的计数器是 32 位的,数到顶了就会归零,所以回绕这件事躲不掉,回绕之后谁早谁晚的判断要怎么才靠得住,这就是咱们交给 `is_after` 的题,这道题的答案,咱们放到回绕那一段里细讲。三个问题立住了,咱们进文件,头一段是类型的外壳,咱们连同头注释一起看:

```cpp
#pragma once

#include <cstdint>
namespace ZerOS::clock {
/*
    Well Well, Clock Ticks everytime, how long is it?
    or we say, how should we define a second :)
*/
struct Ticks {
  public:
    using tick_t = uint32_t;
    using tick_diff_t = int32_t;

    constexpr Ticks() = default;
    constexpr Ticks(tick_t tick) : tick_(tick) {}
```

头注释第一句就把大问题摆上了桌:时钟每时每刻都在走,那到底多长算一秒呢?咱们给它配 1 kHz 的节拍,数出来一个 tick 就是一毫秒了。咱们换成 100 Hz,它就是十毫秒了。类型管的只是把若干个 tick 记清楚,周期定成多少是使用它的人的事。

类型本身有个立场值得您留意:裸的 `uint32_t` 不让直接满天飞,咱们想表达“过了 500 拍”,就得写 `Ticks{500}` 这样的代码。好处在调用点就看得出来了,`call_after_span(w, 1000)` 这样的代码,读的人知道 1000 的单位是 tick,连注释都省了。`tick_t` 和 `tick_diff_t` 是成对出现的两个别名,一个是 32 位无符号的计数值,一个是 32 位有符号的差值,该配哪个的问题,拆到下一段的 `is_after` 咱们马上见分晓。

### 回绕:寄存器宽度替咱们做的决定

tick 计数为什么非得回绕?因为硬件定时器是 32 位的,数到 `0xFFFFFFFF` 之后再走一步就归了零,寄存器宽度替咱们做了这个决定。代码里那行注释写得倒是直白,“Register can be Overflow”。谜底咱们就在下面这一段里揭开:

```cpp
    // Or, we say is_due, but i think this is much hard to understand :(
    constexpr bool is_after(Ticks now) const noexcept {
        // We say, if the now ROLL back to 0(Register can be Overflow)
        return static_cast<tick_diff_t>(now.tick_ - tick_) >= 0;
    }

    void forward(tick_t step) noexcept { tick_ += step; }
```

归零了倒不可怕,可怕的是归零之后“谁早谁晚”就没法比了。咱们把 `is_after` 的机制走一遍。它回答的是一个问题:现在这一刻算不算已经到了或者越过了这个截止时刻?留意一下方向:差值是拿 `now` 减 `deadline` 得到的,大于等于零的时候才算到点。麻烦在于 `now.tick_ - tick_` 是无符号的减法,如果 deadline 在前、now 归了零,减出来的就是一个巨大的数。

`static_cast<tick_diff_t>` 干的就是把这个巨大的数重新解读成有符号数。咱们拿真数字走一遍:deadline 存的是 `0xFFFFFFFC`,now 已经回绕到了 `0x00000002`。咱们拿无符号减法一算:`0x00000002 - 0xFFFFFFFC` 按 32 位取模得到了 6,把它按 `int32_t` 解读出来的值还是 6,结果是个大于等于零的数,咱们就判它到点了。咱们反过来试 now 等于 `0xFFFFFFFB` 的情形,它还没到回绕的时候,算出来的差值是 -1,咱们就判它没到了。所以真实的次序间隔只要不超过 `int32_t` 能表达的范围,这套比较在回绕前后给出的答案是一致的。

这个范围咱们算一下:1 kHz 的节拍下,`int32_t` 的正半轴装得下 2147483647 个 tick,折下来咱们得到约 24.8 天。两个截止时刻彼此隔得比 24.8 天近的时候,咱们比较它们的早晚就不会出错,比如一个排在 20 天后、另一个排在 40 天后的两个时刻,中间隔了 20 天,判断照常是成立的。真实间隔一旦超过了 24.8 天,差值就绕回负数了,后来的那个时刻在插入队列的时候就被判成了“已经过期”,还没等到点就被咱们弹出来执行,这才是出事的机理。嵌入式系统里咱们很少让任务等这么久,这个边界咱们记在心里就够了,不用咱们写代码去防。

旁边的 `forward` 咱们也顺带看一眼,它做的事是拿一个步长把计数往前拨,后面 Clock 推时钟用的就是它。

序列号算术这个套路在协议栈里也是常见的,TCP 的序号比较走的是同一条路。咱们在这里把它头一次写进自己的内核,后面的调度器、看门狗都靠它。

文件的尾巴还剩一小段,咱们把它收了:

```cpp
  public:
    tick_t tick_;
};

constexpr Ticks operator+(Ticks ticks, Ticks::tick_t move_steps) {
    return {ticks.tick_ + move_steps};
}

} // namespace ZerOS::clock
```

末尾那个 `operator+` 写成了自由函数,它标的是 `constexpr`,表示的是拿当前时刻加一段跨度,`clock_.current() + span` 得出一个新的截止时刻,左边的值是不动的。咱们把这个函数放在类型外面,左边 `Ticks`、右边裸 `tick_t` 的混合运算就顺理成章了,咱们不用在类里再开一堆重载。

## Clock 与 TimeWaiter:当前时刻与何时行动

Ticks 把一拍装进了类型,时间内核还缺的东西有两样,一样回答的是现在几点了,另一样记的是到点该做什么。同一个目录下的 `clock.hpp`,54817f3 在这里交了两个角色,Clock 托着当前的 Ticks,时刻的答案都在它手里,TimeWaiter 的身上写着截止时刻,还挂着到点要执行的动作,往后排进队里的全是它。咱们把文件拆成两段,头一段咱们看 Clock:

```cpp
#pragma once

#include "ZerOS/base/borrowed_ptr.hpp"
#include "ZerOS/base/self_list.hpp"
#include "ZerOS/kernel/clock/ticks.hpp"
namespace ZerOS::clock {

struct Clock {
    [[nodiscard("Hey boy, dont throw me away!")]]
    constexpr Ticks current() const noexcept {
        return now_;
    };

    constexpr void tick_forward(Ticks::tick_t steps) { now_.forward(steps); }

  private:
    Ticks now_{};
};
```

咱们看 `Clock`,它是个小得像一个变量的类型,管的事只有一件:现在几点了。`current()` 返回的是 `Ticks`,上面挂了一句自定义的弃置警告,`[[nodiscard("Hey boy, dont throw me away!")]]`。为什么这么严厉?咱们想一下,取了当前时刻却不接住返回值,这样的调用就没有任何意义,让编译器在丢弃返回值的地方喊一声,比咱们事后查 bug 便宜得多。

轮到 TimeWaiter 了,咱们看它的样子:

```cpp

// Wait... oh it's time to take action!
struct TimeWaiter : public base::SelfNode<TimeWaiter> {
    using OnTimeAction = void (*)(base::BorrowedPtr<TimeWaiter> self);
    Ticks deadline_;
    OnTimeAction OnTime;

    constexpr TimeWaiter() = default;
    constexpr TimeWaiter(Ticks deadline, OnTimeAction on_time)
        : deadline_(deadline), OnTime(on_time) {};
};

} // namespace ZerOS::clock
```

`TimeWaiter` 是到期记录上的一行:一个截止时刻,加一个到点后要执行的动作。有两处设计咱们分别看。一是它继承了 `SelfNode<TimeWaiter>`,把自己的 `next_` 指针背在身上,这就是侵入式容器的入伙仪式,后面的 `SelfList` 不用额外给节点分配包装内存。二是回调写成 `void (*)(BorrowedPtr<TimeWaiter>)` 的裸函数指针,`std::function` 在咱们这儿用不了,因为它可能分配堆而咱们整个内核没有堆。裸指针加无捕获 lambda 的组合正好能转成函数指针,零开销的搭配在后面的例子里您就能见到。

回调与节点都归您安排,内核不替您管 `TimeWaiter` 的生死:您静态定义一个是可以的,放进池里造一个也是可以的,内核只借它的地址用,借了就是借了,`BorrowedPtr` 这个名字把分寸写在脸上了。

## 这一批 base 工具是怎么来的

`clock.hpp` 头上 include 了 `borrowed_ptr.hpp`,54817f3 一并把几个 base 层小工具交了,咱们一个个看。

咱们看 `BorrowedPtr`,它是给非拥有指针做的包装,头注释里的那句 “Bro I borrowed this :)” 把立场说完了:

```cpp
template <typename BorrowedSrc> struct BorrowedPtr {
    constexpr BorrowedPtr() = default;
    constexpr BorrowedPtr(BorrowedSrc* src) noexcept : source(src) {}

    constexpr explicit operator bool() const { return source != nullptr; }
    constexpr BorrowedSrc* operator->() const { return source; }
    [[nodiscard]] constexpr BorrowedSrc* get() const { return source; }
    constexpr BorrowedSrc& operator*() const {
        debug::Check(source, "Deref NulPtr");
        return *source;
    }

    friend constexpr bool operator==(BorrowedPtr a, BorrowedSrc* b) { return a.source == b; }
    friend constexpr bool operator==(BorrowedPtr a, BorrowedPtr b) { return a.source == b.source; }

  private:
    BorrowedSrc* source{nullptr};
};

// Make A Compile Time Assumptions that we COST NOTHING
static_assert(sizeof(BorrowedPtr<int>) == sizeof(int*));
static_assert(std::is_trivially_copyable_v<BorrowedPtr<int>>);
```

它比裸指针多给的东西只有一样:解引用之前多了 `debug::Check` 对空指针的拦截。代价是多少?替它作证的是文件尾的两条 `static_assert`:它的大小和裸指针是一样的,而且是 trivially copyable 的,随拷随传而不藏任何行为。咱们想证明零开销,断言写进了编译期,比文档里的十句话都硬。

`debug::Check` 本身是很短的,但有一段值得您多看一眼:

```cpp
inline constexpr void Check(bool ShouldBeTrue, [[maybe_unused]] const char* msg,
                            [[maybe_unused]] std::source_location loc = std::source_location::current()) {
    if (ShouldBeTrue) {
        return;
    }

    // auto global_channel = ZerOS::log::FetchGlobalChannel();
    // global_channel();

    // Oh we sucks!
    while (1) {
        // Halt here in debug mode
    }
}
```

断言失败的时候,它原本要把消息送进全局的日志通道,那两行被注释掉了,原因是那会儿的日志通道还没有出生。现在的行为是死循环挂起,断言失败了就停在那儿,咱们挂上调试器,现场是一点没跑的。参数里的 `std::source_location` 带着默认实参,将来日志通道接上了,调用点一行都不用咱们改,文件名和行号自动就到位了。`global_channel.hpp` 留的也是同一样的缝:只声明了 `RegisterGlobalChannel` 和 `FetchGlobalChannel`,实现是空着的,留给后面的 fault/log 线来填。

`traits.hpp` 里住着的只有一个 `AlwaysFalse`,它是咱们见到的第一个策略默认值:对所有比较都返回 false 的函数对象,马上就在链表里露脸了。

## SelfList 与 TimerQueue:按截止时间排队的到期记录

到期记录的容器是 `SelfList`,一个带策略比较器的侵入式链表。这个头文件身上的插曲,咱们放在这里讲,因为它直接决定了咱们的验收怎么算。

咱们看接口(摘录,完整的在 4f12867 的 `include/ZerOS/base/self_list.hpp`):

```cpp
template <typename Stuff> struct SelfNode {
    Stuff* next_{};
};

template <typename Stuff>
concept SelfLinked = std::is_base_of_v<SelfNode<Stuff>, Stuff>;

template <SelfLinked Stuff, typename Less = ZerOS::traits::AlwaysFalse> struct SelfList {
    SelfList() = default;
    SelfList(const SelfList&) = delete;
    SelfList& operator=(const SelfList&) = delete;
    [[nodiscard]] BorrowedPtr<Stuff> head() const { return stuff; }

    void insert(BorrowedPtr<Stuff> new_stuff) {
        Stuff** slot = &stuff; // Lets walk!

        while (*slot && !less_(*new_stuff, **slot)) {
            slot = &(*slot)->next_;
        }

        new_stuff->next_ = *slot;
        *slot = new_stuff.get();

        // OK, quit here
    }
```

`concept SelfLinked` 把入伙写成了类型约束:咱们想住进 `SelfList`,得继承自己的 `SelfNode`。插入的走法是指针的指针,咱们让 `slot` 一路沿着 `next_` 往后挪,直到它比眼前的元素小了为止,然后一步就接上了,新节点就落在它前面了。整个过程里没有 new 也没有临时包装的影子,节点就是在链表上的那个对象本身。

排序的行为全看 `Less` 这个策略的取值。不指定的时候默认 `AlwaysFalse`,它对所有的比较都返回 false,于是插入永远走到链尾去了,链表退化成了纯粹的 FIFO。指定了策略,它就是有序表了。文件尾的三条断言里有一条的失败文案我很喜欢,直接引给您看:

```cpp
static_assert(std::is_trivially_destructible_v<SelfList<zeros_impl::Probe>>);
static_assert(sizeof(SelfList<zeros_impl::Probe>) == sizeof(zeros_impl::Probe*),
              "SelfList is not Zero cost");
```

断言本身证明 `SelfList` 就是一个裸指针的大小,连空基类优化的痕迹都替咱们查了,策略对象用 `[[no_unique_address]]` 挤了进来,不占自己的格子。失败文案倒是写着 “not Zero cost”,您细品这个幽默:您要是不信这个邪,编译器就当面回您一句 SelfList 不是零开销的,证据您在上一条断言里就能找到。

SelfList 给的是“怎么排”的机制,至于排成什么样子、什么时候出队,咱们得找 `TimerQueue` 要答案。它要答的问题有两个,咱们一个个看:一个是两个截止时刻谁更早的问题,咱们的答案得经得住回绕,另一个是队头的时刻到了没有,到了的时候才交节点,没到的时候交回空。头一个答案是 `DeadLineOrder` 给的,咱们看它的样子:

```cpp
struct TimerQueue {
    struct DeadLineOrder {
        bool operator()(const TimeWaiter& lhs, const TimeWaiter& rhs) const noexcept {
            return static_cast<Ticks::tick_diff_t>(rhs.deadline_.tick_ - lhs.deadline_.tick_) > 0;
        }
    };
```

`DeadLineOrder` 就是回绕算术的第二次上岗:咱们把差值转成 `tick_diff_t` 再判符号,和 `Ticks::is_after` 走的是同一个思路。所以咱们看到的队头永远是真实意义上最早的截止时刻,哪怕最近刚回绕过了。同截止时间的次序也有讲究,比较器对相等的两个时刻是返回 false 的,插入的 while 循环就继续往后走,新节点排在所有等值节点的尾巴上,早来的就排在了前面。

出口上的另一个答案,咱们一起看剩下的部分:

```cpp

    void insert(base::BorrowedPtr<TimeWaiter> timer_task) { list_.insert(timer_task); }
    [[nodiscard]] base::BorrowedPtr<TimeWaiter> head() const { return list_.head(); }
    bool remove(base::BorrowedPtr<TimeWaiter> w) { return list_.remove(w); }

    [[nodiscard]] base::BorrowedPtr<TimeWaiter> pop_due(Ticks now) {
        // Do WE have task? and is there something we need to do by given NOW ticks
        if (list_.head() && list_.head()->deadline_.is_after(now)) {
            return list_.pop_head();
        }
        return nullptr;
    }

  private:
    base::SelfList<TimeWaiter, DeadLineOrder> list_;
};
```

`pop_due` 定下的做法是只从队头出队:队头的截止时刻到了咱们才弹,没到的时候交回的是空指针。因为链表是有序的,咱们判断一个就够,全队是不用扫的。

## 一段要坦白的事:54817f3 编不过

上面这套 `SelfList` 是咱们从 4f12867 引的。可是到了 54817f3 那个 commit,`clock.hpp` 和 `test_self_list.cpp` 都 include 了 `base/self_list.hpp`,咱们拿 `git ls-tree` 去那个 commit 的树里查过,`include/ZerOS/base/` 底下是找不到这个文件的。

后果是实打实的,咱们 checkout 54817f3 现场跑了一遍 host 测试构建,卡在了这里:

```text
/tmp/zeros-t548/test/test_self_list.cpp:6:10: fatal error:
    ZerOS/base/self_list.hpp: No such file or directory
    6 | #include "ZerOS/base/self_list.hpp"
```

`test/CMakeLists.txt` 里 `zeros_add_test(test_self_list)` 已经挂上了,148 行的测试文件也交了,实现倒是得等下一个 commit 来补,咱们拿到手的就是这样的半成品。接口和测试交在前头、实现跟在后面的做法,是真实的开发节奏,提交者自己也没掩饰的打算:接口定了下来,测试写好了当作规格书,实现紧跟着就补上了。所以您要是 checkout 到 54817f3 就跑 `-DZEROS_BUILD_TESTS=ON`,编不过是预期内的现象,和您 clone 得对不对没有关系。

咱们的验收 checkpoint 定在 4f12867,您往下读到验收节,就知道它是怎么凑齐的。

## Kernel\<Driver\>:一套模板,两个世界

4f12867 在同一天上午 10:10 交了上来,subject 写的是 “feat: clock subsys finished”,时间子系统就在这个 commit 里成了形。咱们要看的主件是 `include/ZerOS/kernel/clock/kernel.hpp` 的 `Kernel<Driver>` 模板。为什么内核要做成模板?因为咱们要它在两个世界里跑同一份代码,咱们开发用的电脑是一个世界,另一个世界是 Blue Pill 所在的那一头,两个世界给时间的方式不一样,内核能信的办法,是把布防与关中断的动作交给一个可替换的驱动参数,内核自己是一行都不用改的。参数不是随便给的,它的准入条件就是 `TimePortable`,不满足的驱动连编译都过不去,咱们从合同看到实现。

### 合同写在前头

```cpp
template <typename Portable>
concept Armable = requires(Portable p, Ticks::tick_t ticks) {
    p.arm(ticks);
    p.disarm();
};

template <typename IsTimePort>
concept TimePortable = Armable<IsTimePort> && irq::CriticalSection<IsTimePort>;
```

`TimePortable` 就是硬件移植合同:想伺候时间内核的驱动得会 arm(过了一段时长就提醒内核)与 disarm(撤防),还得会 lock/unlock 提供的临界区。咱们把合同写成 concept,不满足的驱动在模板实参那一行就报错,报错的位置离病因最近,咱们排障就省了事。

### 临界区:嵌套计数的 BASEPRI

咱们看合同的后半截,它的家在 `include/ZerOS/kernel/irq/critical_section.hpp` 这个文件里。lock 与 unlock 伺候的是时间内核的另一类麻烦:`now`、到期队列、布防状态都是任务侧和中断侧两边要一起动的状态,而 tick 中断随时可能切进来,改到一半的状态被它看见了,队列就乱了,所以内核需要一段谁也打断不了的临界区,进去的时候把中断挡在外面,出来的时候再放行。

```cpp
template <typename IsCriticalSection>
concept CriticalSection = requires(IsCriticalSection& t) {
    t.lock();
    t.unlock();
};

template <CriticalSection Lockable> class CriticalGuard {
  public:
    [[nodiscard]] explicit CriticalGuard(Lockable& target) : lockable_(target) { lockable_.lock(); }
    ~CriticalGuard() { lockable_.unlock(); }

  private:
    DISABLE_COPY_MOVE(CriticalGuard);

  private:
    Lockable& lockable_;
};
```

这是一个 RAII 守卫的样板:构造的时候上锁,析构的时候解锁,拷贝和移动全删掉了,锁的持有权不许被复制走。`[[nodiscard]]` 挂在了构造函数上,防您写个临时对象把守卫当场析构掉。

咱们再看 CM3 的实现,它住的房子是 `include/ZerOS/arch/arm_cortex_m3/cm3_irq.hpp`。CM3 给了咱们两个关中断的开关,这里挑的是做选择性掩蔽的 BASEPRI,为什么不用一刀切的 PRIMASK,咱们看完代码再说:

```cpp
struct CortexM3CriticalSection {
    void lock() {
        if (nest_++ == 0) {
            saved_basepri_ = __get_BASEPRI();
            __set_BASEPRI(kMaskLevel);
        }
    }
    void unlock() {
        if (--nest_ == 0) {
            __set_BASEPRI(saved_basepri_);
        }
    }

  private:
    static constexpr std::uint32_t kMaskLevel = 0x40;
    std::uint32_t nest_{0};
    std::uint32_t saved_basepri_{0};
};

static_assert(ZerOS::irq::CriticalSection<CortexM3CriticalSection>);
```

这段代码用的是 BASEPRI 而没有动 PRIMASK。BASEPRI 是个做选择性掩蔽的寄存器,咱们往里写 `0x40`,中断级别数值上大于等于 `0x40` 的全被挡住,数值更小的照常进来,所以最急的那批中断还进得来。PRIMASK 就不一样了,它是一刀切的做法,连最急的中断也被咱们拖住了,所以咱们选了 BASEPRI 给紧急中断留路。不过 BASEPRI 掩不住级别为 0 的中断,这一句咱们记在心里,下一篇的对质要用到它。

嵌套计数 `nest_` 解决的是锁中锁,而锁中锁在这个内核里真会发生:板上 ticker 的回调是在 `on_elapsed` 的临界区里执行的,回调又去调了 `call_after_span`,后者自己开了守卫,一层套一层地嵌下去。咱们要是不在上锁时分层,内层守卫析构时就会把锁全放掉了,外层还当自己是待在临界区里的。所以 lock 只在计数从 0 变 1 的时候动硬件,并把旧的 BASEPRI 存了起来。unlock 只在计数减回 0 的时候恢复原值。恢复的是保存的那个值而不是写 0,这个细节在嵌套的场合也站得住:外层如果本来就在更高的掩蔽级里,内层退出之后就不该把掩蔽给降下去了。

### Kernel 本体

合同验完了,咱们看 `Kernel<Driver>` 的本体。它对外管的是排定时、取消、问时刻这样三件事,对内还留着 ISR 侧的入口,给中断服务程序推时钟用的是 `on_elapsed`。任务侧的三个动作,咱们看代码:

```cpp
template <TimePortable Driver> struct Kernel {
    // A Timer fetch w for span
    void call_after_span(base::BorrowedPtr<TimeWaiter> task, Ticks::tick_t span) {
        irq::CriticalGuard guard{lowlevel_driver_};

        // set the deadline
        task->deadline_ = clock_.current() + span;
        queue_.insert(task);
        update_alarm();
    }

    // if true, we cancel success
    bool cancel(base::BorrowedPtr<TimeWaiter> task) {
        irq::CriticalGuard guard{lowlevel_driver_};
        bool result = queue_.remove(task);
        update_alarm();
        return result;
    }

    Ticks current() const { return clock_.current(); }
```

公开的动作有三个,咱们顺着调用现场讲。`call_after_span` 是任务侧的入口:在临界区里拿当前时刻加跨度定下截止时刻并把它放进了到期记录,接下来要考虑的才是动不动硬件。咱们把队列操作圈进临界区,防的就是中断切进来的场面,任务这边读到一半的队列状态就是 ISR 眼里的真相,队列状态就乱了。

中断那边的入口,咱们交给 `on_elapsed`:

```cpp

    // ISR side call
    void on_elapsed(Ticks::tick_t n) {
        if (n == 0) {
            return;
        }
        irq::CriticalGuard guard{lowlevel_driver_};
        clock_.tick_forward(n);
        while (auto due = queue_.pop_due(clock_.current())) {
            due->OnTime(due);
        }
        update_alarm();
    }
```

`on_elapsed` 是 ISR 侧的入口,函数名的意思是“过去了 n 拍”。它把时钟往前推了 n 拍,把到期记录上所有到期的节点一个个弹出来执行,然后仍然走一遍 `update_alarm` 的流程。`n == 0` 的提前返回放在拿锁之前,零拍不值得咱们动锁。

回调签名那个 `due->OnTime(due)` 值得咱们停一下:到了点执行的时候,它把节点自己的 BorrowedPtr 递回给回调。这一下成就了板端 demo 的自重挂模式,回调里拿着自己就能再排下一个周期了,全局变量都省了。

### 贪婪布防:update_alarm 的分寸

布防的策略落在 `update_alarm` 这个私有函数里,它回答的问题很实在:到期队列是活的,更早的截止时刻随时会被任务插进来,取消的时候也是说来就来,硬件闹钟什么时候值得重新编程?朴素的做法是队列每动一次就重布一次防,可重编程有实打实的代价,写寄存器、清计数的过程,每一步都是实打实的开销,布防的次数一多,下一拍的精度还会被管理开销吃掉。这里的答案叫贪婪布防,每次只认当前最早的截止时刻,已布的闹钟还作数,就不动硬件了。咱们把它分成两半看,头一半是队列空了的时候:

```cpp
  private:
    void update_alarm() {
        auto head = queue_.head();
        if (!head) {
            if (arm_valid_) {
                lowlevel_driver_.disarm();
                arm_valid_ = false;
            }
            return;
        }
```

队头不存在的时候它就撤防,`arm_valid_` 保证的是撤防只做一次。剩下的那一半是队头存在的时候,咱们接着看:

```cpp

        const auto now = clock_.current().tick_;
        bool armed_pending =
            arm_valid_ && static_cast<Ticks::tick_diff_t>(armed_.tick_ - now) > 0;
        bool head_earlier =
            arm_valid_ && static_cast<Ticks::tick_diff_t>(armed_.tick_ - head->deadline_.tick_) > 0;

        if (armed_pending && !head_earlier) {
            return;
        }

        auto remaining = static_cast<Ticks::tick_diff_t>(head->deadline_.tick_ - now);
        if (remaining < 1) {
            remaining = 1; // Clamp to 1, call it next
        }
        lowlevel_driver_.arm(static_cast<Ticks::tick_t>(remaining));
        armed_ = head->deadline_;
        arm_valid_ = true;
    }
```

队头存在的时候,它要算的布尔有两个:`armed_pending` 问的是已布的防还作不作数(已布的闹钟还在未来吗),`head_earlier` 问的是新队头是不是比已布的更早。已布的防还有效、新队头又不比它早的时候,函数就直接返回了,硬件定时器是一个字都不会动的。已布的闹钟够用就让它响,响完 `on_elapsed` 末尾自然会为新的队头重新布防,只有出现更早的截止时刻,才值得咱们改写闹钟,这就是咱们开头说的那个贪婪:每次都布当前最早的,绝不为更晚的重布。

注释里的那句 “Clamp to 1, call it next” 在咱们眼里是另一个分寸:队头其实已经过期(剩余时间小于 1)的时候,布防值被钳到了 1,这样的布防值等于下一拍就叫它。过期的任务是不会被丢弃的,顺延了一拍再执行,超时回调还是会得到执行的,只是晚了一拍。

### CM3 移植层:arm 与 disarm 暂时空着

咱们接着看 `include/ZerOS/arch/arm_cortex_m3/cm3_time_port.hpp`,它把两半合在了一起:

```cpp
// For simple and toy, ARM and disarm currently using systicks
struct CortexM3Armable {
    void arm([[maybe_unused]] ZerOS::clock::Ticks::tick_t f) { return; }
    void disarm() {}
};

struct CortexM3Portable : public CortexM3Armable, CortexM3CriticalSection {};
static_assert(ZerOS::clock::TimePortable<CortexM3Portable>);
```

现在的 arm 与 disarm 都是空操作,注释言明了:现在处于简单玩具的阶段,布防暂时交给周期性的 SysTick,时间内核就按每过一拍问一次队头的方式来跑,arm 该有的精度暂时没有兑现。真正的单次布防咱们要等 tickless 出场,到了那时候,两个空函数才会被填实了,布防动作就改由硬件定时器来管了,咱们不再靠周期询问。最后一行 `static_assert` 是合同的自检:这副拼装满足的是 `TimePortable`,哪天不满足了,固件是连编译都过不去的。

## host 上的考场:同一份 Kernel 跑 mock

移植合同带来的直接红利在测试里兑现。咱们看 `test/test_time_kernel.cpp` 的节选(完整文件 187 行),里面定义了一个 host 替身:

```cpp
struct HostTimePort {
    void arm(Ticks::tick_t d) { g_arms.push_back(d); }
    void disarm() { ++g_disarms; }
    void lock() {}
    void unlock() {}
};

static_assert(ZerOS::clock::TimePortable<HostTimePort>);
```

arm 的调用被它记进日志里,disarm 的次数被它数着,锁是空的,咱们在 host 上用不着真锁。它满足的同一个 concept 一验就过,于是 `Kernel<HostTimePort>` 和板上的 `Kernel<CortexM3Portable>` 是同一个模板的两份实例,桌面这边把布防策略、回绕、取消的逻辑全部跑穿了,板上要验的就只剩移植层了。host、Renode、QEMU、真正的开发板四个目标,吃的是同一份内核代码,这个目标头一次在时间线上兑现了。

测试里咱们挑几个看。第一个用例把时钟一下推到了 `0xFFFFFF00`,离 32 位计数的顶只剩 0x100 拍,回绕线就在眼前了(摘录,`Waiter`、`reset_logs`、`fired_deadlines` 这些辅助物在同一个文件的前面):

```cpp
TEST_CASE("wraparound across the rollover", "[timekernel]") {
    reset_logs();
    Kernel<HostTimePort> k;
    k.on_elapsed(0xFFFFFF00u);

    Waiter a;
    k.call_after_span(&a.node, 0x80u);

    k.on_elapsed(0x7Fu);
    CHECK(g_fired_nodes.empty());

    k.on_elapsed(0x01u);
    CHECK(fired_deadlines() == std::vector<std::uint32_t>{0xFFFFFF80u});
}
```

时钟被咱们推到了 `0xFFFFFF00`,截止时刻就定在了 `0xFFFFFF00 + 0x80 = 0xFFFFFF80`,距计数值的顶只剩 0x80 的距离。咱们走 0x7F 拍到 `0xFFFFFF7F`,差了一拍,响是不许的。再走一拍就正好到了 `0xFFFFFF80`,该响了。您要是拿数值大小去比,`0xFFFFFF80` 在无符号的意义下大得吓人,好像永远轮不到它的头上。咱们真正比的是差值的符号,这个判断在回绕前后给出的答案是一致的。

贪婪布防也有一条专门的用例,咱们引给您看:

```cpp
TEST_CASE("greedy arm: only a strictly earlier head re-arms", "[timekernel]") {
    reset_logs();
    Kernel<HostTimePort> k;
    Waiter a, b, c;
    k.call_after_span(&a.node, 100);
    REQUIRE(g_arms.back() == 100);

    k.call_after_span(&b.node, 50);
    REQUIRE(g_arms.back() == 50);

    auto arms_before = g_arms.size();
    k.call_after_span(&c.node, 70);
    CHECK(g_arms.size() == arms_before);

    CHECK(k.cancel(&b.node));
    CHECK(g_arms.size() == arms_before);
}
```

布了 100 的防,再来一个更早的 50,咱们就重布。再来 70 的,它排在 50 的后面,布防的次数不变。然后咱们取消 50 那个节点,队头换成 70 的 c。直觉上已布的闹钟护的是 b 的 50,被取消了,按理是要重布的。但是 `update_alarm` 一算,已布的 50 的时刻还没到,c 的 70 又不比它早,函数就直接返回了,布防的记录也就一个没涨。断言数的是整个序列里 arm 调用的次数,咱们数一数 arm 调用的次数,策略对不对就知道了。

到期记录的测试在 `test/test_timequeue.cpp`(153 行)里,它专测的是排序和到期,同期限 FIFO 有自己的独立用例,恰好到点的边界也有自己的用例,跨过回绕线的排序也有独立用例。两个文件一共写了 340 行,这就是咱们要的验收标的。

## 板上:自重挂的周期 ticker

板子这边咱们看 `example/main.cpp`,它从内存 demo 换成了时间 demo:

```cpp
std::uint32_t tick_count = 0;

constexpr auto on_tick = +[](ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    ZerOS::board::print("tick ");
    ZerOS::board::printdec(++tick_count);
    ZerOS::board::print("\r\n");
    ZerOS::arch::cortex_m3::system_time.call_after_span(w, 1000);
};

constinit ZerOS::clock::TimeWaiter ticker{ZerOS::clock::Ticks{0}, on_tick};
```

这就是前面埋的两处伏笔合体:一元 `+` 把无捕获 lambda 褪成一个裸函数指针,正好塞进了 `OnTimeAction` 那个函数指针槽,堆是没有的,也不见 `std::function` 的影子。`call_after_span(w, 1000)` 里那个 `w` 就是回调收到的自己,咱们拿着它再排下一个 1000 拍,周期定时器就这样自转起来了,内核里并没有专门的周期定时器类型。`ticker` 本身是 `constinit` 的,编译期就各就各位了,上一站实验里定下的纪律在这里用到了时间对象上。

初始化和 ISR 的代码各是一行,咱们打开 `src/arch/arm_cortex_m3/time_kernel_cm3.cpp`:

```cpp
void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    NVIC_SetPriority(SysTick_IRQn, 0xF0); // Lowest Level
    SysTick_Config(cycles_per_tick);
}

extern "C" void SysTick_Handler() {
    ZerOS::arch::cortex_m3::system_time.on_elapsed(1);
}
```

咱们看 `main` 所在的 `example/main.cpp`,里面调的是 `init_time(SystemCoreClock / 1000)`,SystemCoreClock 在这块板上是 72 MHz 的主频,除下来每个 tick 分得的时钟周期是 72000 个,算下来的节拍正好是 1 kHz,一个 tick 就是一毫秒了,“一秒”的定义在这里落地:1000 拍。SysTick 的中断服务程序就一行,它做的只是把过了 1 拍这件事转给时间内核,咱们看得到,ISR 里是找不出多余动作的,解析到期记录与跑回调的活儿全在 `on_elapsed` 里走,这个写法让中断服务程序的耗时边界一眼可见。

`init_time` 的第一行 `NVIC_SetPriority(SysTick_IRQn, 0xF0)`,注释写的是 Lowest Level。这里我按下不表,您只需要知道这一行和上面 BASEPRI 那段“掩不住级别 0”的话有牵连,下一篇文章咱们把 CMSIS 一个个看,当庭对质这一行到底把 SysTick 设成了什么级别。

板级 CMake 同步动了,`src/arch/arm_cortex_m3/time_kernel_cm3.cpp` 进了固件目标的源文件列表,咱们在 `src/board/stm32f103_bluepill/CMakeLists.txt` 里能对上。

## 验收

验收分的是两头,咱们从 host 端做起,做完了再到真正的开发板上跑。

host 这边咱们 checkout 到 4f12867,命令用的还是上面的三条:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

笔者本机 GCC 16.2.1 的真实输出,六个目标是全部通过的:

```text
100% tests passed out of 6

Total Test time (real) =   0.16 sec
```

咱们再看两个时间测试的成绩单:

```text
All tests passed (32 assertions in 8 test cases)      # test_timequeue,153 行
All tests passed (27 assertions in 10 test cases)     # test_time_kernel,187 行
```

咱们把两个数加一下,153 加 187 得到的正是 340 行,18 个用例、59 条断言这样的成绩是全部通过的。用例是死的,您跑出来应该一字不差。

板上这边咱们交叉编译固件:

```text
   FLASH:         768 B        64 KB      1.17%
     RAM:          44 B        20 KB      0.21%
   text	   data	    bss	    dec	    hex
    752	     16	     28	    796	    31c
```

时间内核连同 demo 全须全尾地住进去了,flash 才用掉了 768 字节,RAM 占的也只有 44 字节,运行期的状态大多在 bss 那 28 字节里,因为内核的全局对象是 `constinit`,零初始化的东西不占 flash,上一站实验里咱们说过的那笔开销直觉,在这里有了数字。

然后咱们再上 Renode,咱们把仿真跑上 6 秒,串口窗的输出如下:

```text
[INFO] usart1: [host: 0.15s (+1.03ms)|virt: 0s (+0s)] ZerOS time demo: SysTick -> TimeKernel @ Blue Pill
[INFO] usart1: [host:  1.11s (+0.96s)|virt: 1s (+1s)] tick 1
[INFO] usart1: [host:  2.11s (+1s)|virt: 2s (+1s)] tick 2
[INFO] usart1: [host:  3.11s (+1s)|virt: 3s (+1s)] tick 3
[INFO] usart1: [host:  4.11s (+1s)|virt: 4s (+1s)] tick 4
[INFO] usart1: [host:  5.11s (+1s)|virt: 5s (+1s)] tick 5
[INFO] usart1: [host:  6.11s (+1s)|virt: 6s (+1s)] tick 6
```

您看 virt 那一栏,仿真时间每走满了一秒就打一行 tick,咱们要的验收就是这个节奏。照实说一句吧:我本机的 Renode 1.17.0 不认 `bluepill.repl` 里的 `I2C.STM32F4_I2C` 这个外设类型,挂在 i2c1、i2c2 两个节点上的 `EventInterrupt`、`ErrorInterrupt` 连线也跟着它一起报错,咱们把两个 i2c 节点整块注释掉,仿真就起来了。exti、gpio 的连线我这版没有动,它们是报不出错的。demo 的 SysTick 和 UART1 都不经过 i2c,行为是不受影响的。您要是也在这个版本上遇到同样的报错,照同样的办法注释掉两个 i2c 节点就行。

到了这里,咱们把验收凑齐了:54817f3 交的是骨架,4f12867 补的是实现,host 的 340 行时间测试全部通过,板上的 ticker 一秒一行。

## 下一篇

时间内核跑通了,但是有一根刺没拔:`init_time` 里那行 `NVIC_SetPriority(SysTick_IRQn, 0xF0)`,注释写的是 Lowest Level,注释说的是真话吗?下一篇咱们把 CMSIS 从 arch 层清出去,SysTick 的三寄存器改成直写,拿仓库自带的 `core_cm3.h` 当庭对质,移位算完您会发现这行注释骗了咱们多久,还有 BASEPRI 临界区这几个月到底防住了谁。参考答案在 `e63067e` 等着咱们,咱们那儿见。
