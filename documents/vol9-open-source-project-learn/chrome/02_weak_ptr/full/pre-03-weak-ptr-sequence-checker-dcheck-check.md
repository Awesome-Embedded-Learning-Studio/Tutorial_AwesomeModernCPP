---
chapter: 0
cpp_standard:
- 11
- 17
- 20
description: 区分线程与序列,讲清 SEQUENCE_CHECKER 三宏的用法与 release 下零开销原理,
  以及 DCHECK 与 CHECK 的取舍——为什么 WeakPtr 的 operator* 用 CHECK
difficulty: intermediate
order: 3
platform: host
prerequisites:
- WeakPtr 前置知识（二）：std::atomic 与 memory_order
reading_time_minutes: 11
related:
- WeakPtr 实战（四）：序列亲和性与 lazy 绑定
tags:
- host
- cpp-modern
- intermediate
- atomic
- 并发
- weak_ptr
title: "WeakPtr 前置知识（三）：序列、SEQUENCE_CHECKER 与 DCHECK/CHECK"
---
# WeakPtr 前置知识（三）：序列、SEQUENCE_CHECKER 与 DCHECK/CHECK

[前置知识（二）](./pre-02-weak-ptr-atomic-and-memory-order.md) 把可见性问题摆平了:两个序列同时戳 flag,靠 acquire/release 保证看得见。但 WeakPtr 还有一条契约,写在 `weak_ptr.h` 顶部注释里,笔者第一次读直接略过了,后来踩坑才回头细看——**弱指针随便跨序列传递,但 deref 和失效必须在绑定的那一个序列上**。

这条契约靠原子操作守不住,得靠一组叫 `SEQUENCE_CHECKER` 的宏:debug 构建下抓违规,release 构建下蒸发成零开销。咱们这一篇就把这套机制拆透,顺带把 `DCHECK` 和 `CHECK` 这对断言的取舍讲明白——理解了它俩,您才看得懂 WeakPtr 为啥 `operator*` 上 `CHECK`、`IsValid` 却只敢上 `DCHECK`。

## 线程 vs 序列:Chromium 的并发模型

先纠一个常见误解。好多人一听"线程安全",脑子里冒出来的画面就是"多根线程一起上"。Chromium 不这么算账,它的并发模型建在**序列(sequence)**上。

序列是什么?您把它当成一条"虚拟线程"就行。里头的任务排队跑,前一个完事才轮到下一个;但这条虚拟线本身不绑死任何一根 OS 线根,它可以在不同的物理线程上轮流借宿,只要不同时挤一块儿就行。

为啥要绕这么一圈?笔者当初也纳闷,直接用线程不就完了。后来想通了:浏览器里绝大多数对象的真实诉求是"我想被串行访问,至于哪根线伺候我,不在乎"。您真用线程,就得伺候它那一堆 TLS、消息泵、生命周期;换成序列,这些破事全甩给任务调度器,您只声明一句"这几坨代码归同一序列",调度器保它们不并发。这套思路在 [OnceCallback 实战（一）](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md) 里讲过,核心一句话:**消息传递优于锁,序列化优于线程**。

WeakPtr 的契约就长在序列这片土上:弱指针您随便 handed off,把 `WeakPtr<Controller>` 丢给线程池 post 回调没人拦您;可**真要解引用、要让 factory 失效**,必须落回绑定的那个序列。不然 deref 和 invalidate 两头一挤,就是 race。

---

## SEQUENCE_CHECKER:盯的是"互斥"不是"同线程"

`SequenceChecker` 这名字有歧义,容易让人觉得它查"是不是同一根线程"。其实它盯的是更底层的东西——**互斥(mutual exclusion)**。一个 `SequenceChecker` 第一次被碰时记下当前上下文,之后每次再被碰,它就核一遍:现在这个上下文,跟我记的那份,是不是互斥的。

"互斥"在 Chromium 里头有三个来路,凑上任意一个就算过关:同一根物理线程、同一个序列(同一个 `SequencedTaskRunner`)、或者同一把被追踪的锁。判定靠的是 `SequenceToken`,这玩意儿塞在线程局部存储(TLS)里头,同一个序列的任务被调度出去时都揣着同一个 token,`SequenceChecker` 比对一下 token 就心里有数了。

这套机制值钱的地方在哪?并发 bug 里最坑人的那一类,就是"我以为这段是串行的,结果它被并发了"——难复现, debugger 里跑一百次都不带犯一次,上了线用户一点就崩。`SequenceChecker` 让您几乎零成本在 debug 下逮住这帮家伙。

## 三个宏:怎么用

Chromium 把 `SequenceChecker` 包成三个宏,debug 和 release 的差异全藏在宏后头:

```cpp
class Controller {
public:
    void do_work() {
        DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);  // 我必须跑在绑定序列
        // ... 业务 ...
    }

private:
    SEQUENCE_CHECKER(sequence_checker_);   // 声明一个 checker 成员
};
```

`SEQUENCE_CHECKER(name)` 负责声明成员,搁哪儿都行,成员或者本地变量都可以。`DCHECK_CALLED_ON_VALID_SEQUENCE(name)` 是真正干活的,它赌现在就在绑定序列上;不在的话,debug 下打印调用栈然后 abort,release 下装死什么也不干。`DETACH_FROM_SEQUENCE(name)` 是显式解绑,跟 checker 喊一嗓子"我现在还没绑序列,等下次 `DCHECK_CALLED_ON_VALID_SEQUENCE` 再绑"。这玩意儿用在对象构造时还不知道自己将来在哪个序列服役的场景,挺常见的。

WeakPtr 的 `Flag` 就是这套用法的样板。它的 `sequence_checker_` 一构造就先 detach——言下之意,"我还不知道自己归哪个序列",等头一回被 `IsValid`/`Invalidate` 碰了才绑定,往后所有访问都得落同一个序列。这就是 WeakPtr 的 lazy 序列绑定,笔者留到 02-4 专门拆。

## release 下全 no-op:零开销怎么来的

这块有个事实特别关键,也好多人没留意:**这三个宏在 release 构建(`DCHECK_IS_ON()` 为 false)下,全部编译成空**。

挨个说。`SEQUENCE_CHECKER(name)` 展开成一个 `static_assert(true, "")` 占位,半个成员字节都不占,`sizeof(Controller)` 不因为这个变大。`DCHECK_CALLED_ON_VALID_SEQUENCE(name)` 展开成一个空操作占位(真实 Chromium 里是 `EAT_CHECK_STREAM_PARAMS(...)`,作用就是把附带的流参数吞了,啥也不查)。`DETACH_FROM_SEQUENCE(name)` 同样啥也不剩。

笔者第一次读到这个的时候愣了一下:debug 抓违规分文不收,release 里这些检查掏的钱是 0,代码全速跑不带任何检查。这就是 `DCHECK` 体系的根本设计——**开发期抓 bug,生产期零开销**。

可零开销也有代价。正因为 release 下一点强制力都没有,WeakPtr 的"序列契约"到了 release 里**完全靠开发者自觉**。`MaybeValid()`(02-4)是唯一一个故意不绑序列、能从任意序列调的查询接口,可它的返回值也就是个"乐观估计"。您在 release 里违反了序列契约,程序不会崩给您看,只会在某个不凑巧的节骨眼上给您一发 race。所以 debug 构建里头,每一个 DCHECK 都得当回事——那是您逮这种 bug 几乎唯一的机会,放过了就真没了。

## DCHECK vs CHECK:debug 才崩,还是 release 也崩

`DCHECK` 和 `CHECK` 是 Chromium 断言体系里两个等级,差别就一条线。

`DCHECK(expr)` 只在 debug 构建(`DCHECK_IS_ON()`)下查 `expr`,挂了就 abort;到了 release,整个表达式连带 `expr` 一起不求值,直接蒸发。`CHECK(expr)` 不挑,debug 还是 release 都查,挂了就 abort。

说白了,`DCHECK` 是"开发期契约",`CHECK` 是"生产期也要守的红线"。挑哪个,就看这断言挂了之后,release 里您接不接受它继续跑。逻辑写错了、但接着跑不会马上引发内存安全问题的,上 `DCHECK`,debug 抓 release 放,省开销。要是断言挂了意味着"接下来必定 use-after-free 或者更狠的未定义行为",那 release 里也得立刻停,这种就上 `CHECK`。

### WeakPtr 的取舍:operator* 上 CHECK,IsValid 上 DCHECK

带着上面这条原则,咱们瞧瞧 WeakPtr 两个关键断言,它俩选择正好拧着,可背后的逻辑是同一套。

`operator*` 和 `operator->` 上的是 `CHECK`,release 也照崩:

```cpp
T& operator*() const {
    CHECK(ref_.IsValid());   // 失效还硬解引用 → release 也 abort
    return *ptr_;
}
```

凭啥用 `CHECK`?您想啊,解引用一个已经失效的 WeakPtr,等于手里攥着一个**可能早就析构了**的对象的指针,接下来要去戳它的内存——这就是 use-after-free 的直接前身,程序带着悬垂指针接着跑,指不定吐出啥妖蛾子。这种 bug release 里也得立马爆,一刻都不能含糊。所以这里上 `CHECK`:您硬要解引用失效弱指针,管它 debug 还是 release,程序当场中止,给您一个干净的 crash,而不是一笔追不回去的 UAF 烂账。

`IsValid` 上的是 `DCHECK`,只 debug 抓:

```cpp
bool Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // 序列违规 → 仅 debug 抓
    return !invalidated_.IsSet();
}
```

为啥这里降一档?序列违规是**使用契约**问题,不是"立刻内存安全"问题。您从错的序列调 `IsValid`,结果可能不准,读到个 stale 状态;可它自个儿不会去 deref 悬垂指针——真正危险的 deref 在 `operator*` 那一层已经用 `CHECK` 死死守住了。所以 `IsValid` 这里用 `DCHECK`:debug 期抓序列违规帮您揪 bug,release 期放过去省检查开销。再说了,原子操作本身保证 `IsValid` 的读不会被撕裂,就算 release 里没逮着序列违规,也不至于冒出数据竞争层面的 UB。

这俩断言搁一块,WeakPtr 的安全分层就立起来了:**内存安全(deref 失效)用 `CHECK`,天塌下来也得守;使用契约(序列绑定)用 `DCHECK`,开发期抓、生产期信开发者**。后面实战篇每一处断言都会撞上这套取舍,您趁早把它吃透不亏。

## 参考资源

- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
- [Chromium threading & sequences 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/threading_and_tasks.md)
- [cppreference: std::atomic 与 memory_order](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Chromium `base/check.h` —— CHECK/DCHECK](https://source.chromium.org/chromium/chromium/src/+/main:base/check.h)
