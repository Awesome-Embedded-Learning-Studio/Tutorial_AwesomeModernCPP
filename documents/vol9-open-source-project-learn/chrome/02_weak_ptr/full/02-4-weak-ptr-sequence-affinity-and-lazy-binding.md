---
chapter: 1
cpp_standard:
- 17
- 20
description: 正面讲清 WeakPtr 的序列契约——deref/失效必须在绑定序列,以及 Flag 的 lazy 序列绑定
  机制(release 下靠自律),把 IsValid 与 MaybeValid 的差别讲准
difficulty: intermediate
order: 4
platform: host
prerequisites:
- WeakPtr 实战（三）：WeakPtrFactory 与"最后成员"惯用法
- WeakPtr 前置知识（三）：序列、SEQUENCE_CHECKER 与 DCHECK/CHECK
reading_time_minutes: 12
related:
- WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环
- WeakPtr 前置知识（二）：std::atomic 与 memory_order
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 并发
title: "WeakPtr 实战（四）：序列亲和性与 lazy 绑定"
---
# WeakPtr 实战（四）：序列亲和性与 lazy 绑定

前面几篇笔者带您把 WeakPtr 的骨架搭起来了——铸币、解引用、析构失效,功能上能跑。但有一根线咱们一直没碰,Chromium 在源码顶部专门拿大段注释把它拎出来:`WeakPtr` 可以跨序列传递,可解引用和失效**必须发生在绑定的那个序列上**。这根线笔者想在这一篇正面拆开,因为它下面挂着两件容易踩混的东西:一是 Flag 的 lazy 序列绑定,二是 `IsValid` 和 `MaybeValid` 这对长得像、语义完全不同的查询。弄懂这两件,WeakPtr 在多序列里怎么用才算真正落地。

---

## 序列契约:为什么 deref/失效要同序列

先把契约原话摆出来(对应 `weak_ptr.h:50-54`):

> Weak pointers may be passed safely between sequences, but must always be dereferenced and invalidated on the same SequencedTaskRunner otherwise checking the pointer would be racey.

意思一句话:弱指针能安全地跨序列传递,但解引用和失效得始终落在同一个 `SequencedTaskRunner` 上,不然"检查这枚指针"这个动作本身就是 race。

您可能会想,`invalidated_` 不是原子的吗,怎么会 race。原子操作本身确实不撕裂,问题出在"`get()` 返回非空 → 调用方拿着 `T*` 去访问"这一段窗口上。想象序列 A 持有 `WeakPtr`,`get()` 刚读到一次"有效",正准备 deref;序列 B 这时调 `Invalidate()`,紧接着 owner 把对象析构了。A 手里那个看似还行的 `T*`,真访问下去踩的可能是半成品、甚至已经被回收的内存。原子性只保证读写不撕裂,保证不了 deref 的窗口里对象没人动它。最干脆的约束就是——让 deref 和失效串在同一个序列上跑,窗口压根不存在。

至于跨序列传递,那是允许的。您把一个 `WeakPtr<Controller>` 从序列 A handed off 到序列 B(比如丢进线程池,再 post 一条回 A 的任务),传递本身只是搬数据,不碰 Flag;只有到了 B 想真正"用"它(deref)或"作废"它的时候,才踩进契约的地界。

---

## lazy 绑定:Flag 不在构造时绑定序列

契约立好了,下一个躲不开的问题——Flag 怎么知道"绑定序列"到底是哪一个?Chromium 的做法是:它根本不在构造时绑,而是等到第一次被人"触碰"才落定。这叫 lazy 绑定。

咱们看 `Flag` 的构造函数,它就干一件事——`DETACH_FROM_SEQUENCE`(`weak_ptr.cc:15-20`):

```cpp
WeakReference::Flag::Flag() {
    // Flags only become bound when checked for validity, or invalidated,
    // so that we can check that later validity/invalidation operations
    // on the same Flag take place on the same sequenced thread.
    DETACH_FROM_SEQUENCE(sequence_checker_);
}
```

`DETACH_FROM_SEQUENCE` 的意思是"我现在还没绑任何序列,先别检查"。这会儿 Flag 处于未绑定态。等第一次有人调 `IsValid` 或 `Invalidate` 触碰它,`DCHECK_CALLED_ON_VALID_SEQUENCE` 才把当前序列记下来,从此 Flag 就认准了它,后续所有 `IsValid`/`Invalidate` 都得在这个序列上跑:

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // 首次触碰 → 绑定;之后 → 校验
    return !invalidated_.IsSet();
}
```

为什么非得 lazy 不可?因为 Chromium 里头大量对象是"在序列 A 上构造,却要在序列 B 上用",构造那一刻您压根不知道它最终落在哪条序列上跑。要是 Flag 一构造就绑死构造序列,这堆对象直接没法用了。lazy 绑定让"构造时不表态、第一次真用的时候才认序列"这条路走通,使用门槛一下就降下来了。

这里头还藏着一个更细的口子。如果一枚 Flag 当前没有任何 WeakPtr 持有(`!HasRefs()`),`WeakReferenceOwner::GetRef` 会把它重新 detach(`weak_ptr.cc:91-101`):

```cpp
WeakReference WeakReferenceOwner::GetRef() const {
#if DCHECK_IS_ON()
    DCHECK(flag_);
    if (!HasRefs()) {
        flag_->DetachFromSequence();   // 没人持有 → 解绑,下次可绑到别的序列
    }
#endif
    return WeakReference(flag_);
}
```

这段只在 `DCHECK_IS_ON()` 下生效,但它给"factory 的所有 WeakPtr 都没了之后,factory 换个序列接着用"开了门——下次铸出来的 WeakPtr 第一次触碰,会重新绑到新序列。源码注释专门点了这层意思(`weak_ptr.h:63-65`):所有 WeakPtr 一旦销毁或失效,factory 就从序列上 unbound,可以在别的序列销毁,也可以重新铸 WeakPtr。

---

## release 下:零开销 + 靠自律

这里要跟 [前置知识（三）](./pre-03-weak-ptr-sequence-checker-dcheck-check.md) 那条关键事实对一下:lazy 绑定加序列检查的全部逻辑,都裹在 `DCHECK_IS_ON()` 里,release 构建直接编译成空。

换句话说,release 版的 WeakPtr 一点运行时序列检查都没有。序列契约在 release 里全靠开发者自律,您违反了它程序不会当场崩,只会在某个不凑巧的时机冒出一个 race。所以 debug 构建下认真对待每一条 DCHECK 才这么要紧——那是您抓序列违规几乎唯一的机会。

---

## IsValid vs MaybeValid:同序列准 vs 跨序列 hint

lazy 绑定把"序列契约的检查"全压在 `IsValid` 身上,可 WeakPtr 还甩出另一个查询 `MaybeValid`。这俩长得像,语义却差得远,不分清很容易踩坑。

先看 `IsValid`,它会做序列断言——您必须在绑定的序列上调它,返回值百分百准:true 就是真的还有效,false 就是真的失效了。`WeakPtr::get()` 和 `operator bool` 走的就是这条路,所以"判活后 deref"才是安全的。还有一层副作用:正是这次 `IsValid` 调用会触发 lazy 绑定(如果之前没绑过)。

```cpp
bool WeakReference::Flag::IsValid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);   // 同序列契约
    return !invalidated_.IsSet();
}
```

`MaybeValid` 不一样,它没有任何序列断言,任意序列都能调。Chromium 源码注释把它的边界说得很直白(`weak_ptr.h:266-283`):

> Returns false if the WeakReference is confirmed to be invalid. This call is safe to make from any thread, e.g. to optimize away unnecessary work, but `RefIsValid()` must always be called, on the correct sequence, before actually using the pointer.

它返回值的不对称得记牢。返回 false 是可信的——acquire 读到了 release 写下去的失效位,板上钉钉已经失效。可返回 true 不能全信,只算"也许"还有效:保不齐您刚读到 true,在 deref 之前,绑定序列那边已经把它失效了。所以 `MaybeValid` 真正合适的用途只有一个——从别的序列做一个投机性的"能不能跳过"判断。比如 message loop 派发任务前拿它瞄一眼,返回 false 就知道这任务铁定没意义,直接跳,省一次跨序列投递。但真要动指针,必须回到绑定序列,用 `IsValid` 再过一遍。拿正面结果当 deref 的通行证,迟早翻车。

```cpp
bool WeakReference::Flag::MaybeValid() const {
    return !invalidated_.IsSet();   // 无序列断言,任意序列可调
}
```

对照表收一下,方便您回头查:

| 查询 | 序列约束 | 触发 lazy 绑定 | 结果可信度 |
|---|---|---|---|
| `IsValid()` | 必须绑定序列 | 是 | 100% 准确 |
| `MaybeValid()` | 任意序列 | 否 | 负面可信 / 正面不可信 |

这层差别到了下一篇(02-5,BindOnce 集成)会派上大用场——您会看到 Chromium 的回调取消走的是 `IsValid`(同序列准),而 `MaybeValid` 是另一条独立的"调度器投机查询"通道。

---

## 把序列检查加进教学版

接下来咱们给 02-2 那个 `Flag` 把序列检查补上。教学版用一个简化的 `SequenceChecker`,debug 下记录线程 id:

```cpp
// Platform: host | C++ Standard: C++17  (debug-only 检查)
#if defined(NDEBUG)
// release:全部 no-op,零字节、零开销
class SequenceChecker {
public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
#include <thread>
// debug:记录绑定线程,违规即 abort
class SequenceChecker {
public:
    void detach_from_sequence() noexcept { bound_thread_ = std::thread::id{}; }
    bool called_on_valid_sequence() const noexcept {
        if (bound_thread_ == std::thread::id{}) {
            bound_thread_ = std::this_thread::get_id();   // lazy 绑定
            return true;
        }
        return bound_thread_ == std::this_thread::get_id();
    }
private:
    mutable std::thread::id bound_thread_;
};
#endif
```

您可以对照着看它怎么映上 Chromium 的三宏:`detach_from_sequence` 对 `DETACH_FROM_SEQUENCE`,`called_on_valid_sequence` 对 `DCHECK_CALLED_ON_VALID_SEQUENCE`,release 下全是 no-op。教学版拿线程 id 模拟序列,真实 Chromium 用的是更细的 `SequenceToken`,但 lazy 绑定的形态是一致的。

然后 `Flag` 把它接上:

```cpp
class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }                 // 构造:未绑定

    void Invalidate() noexcept {
        // DCHECK:同序列,或只剩自己一个引用(可跨线程析构)
        assert(seq_.called_on_valid_sequence() || has_one_ref());
        invalidated_.Set();
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence());            // 首次触碰 → 绑定
        return !invalidated_.IsSet();
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet();                       // 不碰 seq_,不绑定
    }
private:
    // ...
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};
```

这么一接,debug 下您要在错误的线程上调 `IsValid`,assert 就会把序列违规抓出来;到了 release,这些 assert 连同 `SequenceChecker` 全部消失,零开销。

---

这一篇把序列契约正面拆完了:弱指针能跨序列传递,可 deref 和失效得落在绑定的同一序列,否则检查本身就是 race;Flag 的 lazy 绑定——构造时 detach、首次触碰才绑、没引用了能 unbound 重用——让"构造时不指定序列"走得通,配 Chromium 一堆"一处构造、另处使用"的对象;这整套检查都裹在 `DCHECK_IS_ON()` 里,release 零开销,契约全靠自律;`IsValid`(同序列、准、触发绑定)和 `MaybeValid`(任意序列、乐观、负面可信正面不可信)这对查询非分清不可,前者是 deref 前的硬门,后者只是调度器投机跳过的 hint。

整个系列真正的"眼"在下一篇——咱们把 WeakPtr 接到回调系统里,看 `BindOnce` 怎么拿 `IsValid` 实现对象死后回调自动变 no-op,把 01-4 那个手搓取消令牌留下的尾巴彻底收掉。

## 参考资源

- [Chromium `base/memory/weak_ptr.h` —— 顶部 Thread-safety 注释(50-69 行)](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` —— Flag/WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [WeakPtr 前置知识（三）：序列、SEQUENCE_CHECKER 与 DCHECK/CHECK](./pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [Chromium `base/sequence_checker.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/sequence_checker.h)
