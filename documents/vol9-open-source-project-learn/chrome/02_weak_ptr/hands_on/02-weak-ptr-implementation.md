---
chapter: 1
cpp_standard:
- 17
- 20
description: 逐层实现 WeakPtr 四件套——RefCountedThreadSafe/AtomicFlag/Flag/WeakReference/WeakPtr/
  WeakPtrFactory,含序列检查与 lazy 绑定,代码密集少废话
difficulty: advanced
order: 2
platform: host
prerequisites:
- weak_ptr 设计指南（一）：动机、接口与控制块设计
- WeakPtr 前置知识（一）：侵入式引用计数与 scoped_refptr
- WeakPtr 前置知识（二）：std::atomic 与 memory_order
reading_time_minutes: 13
related:
- weak_ptr 设计指南（三）：测试策略与性能对比
- WeakPtr 实战（二）：核心骨架与控制块
tags:
- host
- cpp-modern
- advanced
- 智能指针
- weak_ptr
- atomic
- 引用计数
title: "weak_ptr 设计指南（二）：逐步实现"
---
# weak_ptr 设计指南（二）：逐步实现

> hands-on 轨,代码密集。细节论证翻 [full/02-2](../full/02-2-weak-ptr-core-skeleton-and-control-block.md) 与 [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md),配套可编译工程在 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`(`16_weak_ptr_skeleton.cpp`、`17_weak_ptr_factory.cpp`)。

上一篇咱们把架构和"为什么这么设计"想明白了,四层叠出来挺漂亮:Flag、WeakReference、WeakPtr、WeakPtrFactory。可纸面讲清楚是一回事,真要把这套东西一行行撸出来,坑比您预想的多。笔者写这一篇的时候反复在想,Chromium 那帮人是怎么把"析构 private 堵外部 delete"这种细节抠出来的,`WeakPtrFactory` 凭什么非得排最后一个成员,`uintptr_t` 那一手模板瘦身到底省在哪。咱们这一篇就边写边拆,从最底下的引用计数一路搭到 factory。

## 第 0 层:引用计数 + 原子标志

搭房子先打地基。最底下两块砖咱们上一篇提过:一个跨序列安全的侵入式引用计数基类,一个一次性 release/acquire 的原子标志。先别急着往上叠,这两块本身就有讲究。

```cpp
// Platform: host | C++ Standard: C++17
#pragma once
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace tamcpp::chrome::internal {

class RefCountedThreadSafe {
public:
    void add_ref() const noexcept { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    bool release() const noexcept {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;   // 减到 0 返回 true
    }
    bool has_one_ref() const noexcept { return ref_count_.load(std::memory_order_acquire) == 1; }
protected:
    RefCountedThreadSafe() = default;
    ~RefCountedThreadSafe() = default;
private:
    mutable std::atomic<int> ref_count_{0};
};

// 对应 base::AtomicFlag:一次性、release-Set / acquire-IsSet
class AtomicFlag {
public:
    void Set() noexcept { flag_.store(1, std::memory_order_release); }
    bool IsSet() const noexcept { return flag_.load(std::memory_order_acquire) != 0; }
private:
    std::atomic<uint_fast8_t> flag_{0};
};

}  // namespace tamcpp::chrome::internal
```

这里笔者重点说一下 `release` 为什么用 `acq_rel` 而不是 `release`。减法这一步得读到最新的 count(否则两个线程同时减到负数就完蛋了),归零那一刻又得把"析构前的所有写"发布给接管 delete 的线程,两头都要,所以 `acq_rel`。`AtomicFlag` 这边是故意把 `std::atomic<uint_fast8_t>` 收窄成一次性语义,没有 public clear,release/acquire 配对。意思是"Set 之前的写,IsSet 之后的读一定能看见",这正是后面 `Invalidate` 之后所有 WeakPtr 能同步看到失效的根基。

## 第 0.5 层:序列检查器(debug-only)

地基搭完,先别急着上 Flag,咱们还得备一个 debug 用的"序列检查器"。Chromium 真正用的是 SequenceToken,概念上比线程 id 精细,能区分"同一线程跑两个序列"这种情况;教学版笔者就用线程 id 模拟一下,够把意思讲清楚就行。它最妙的地方在于 release 编译下整个类退化成两个 no-op,一个字节都不占。这点优化后面咱们会反复用到。

```cpp
#if defined(NDEBUG)
class SequenceChecker {
public:
    void detach_from_sequence() noexcept {}
    bool called_on_valid_sequence() const noexcept { return true; }
};
#else
class SequenceChecker {
public:
    void detach_from_sequence() noexcept { bound_ = std::thread::id{}; }
    bool called_on_valid_sequence() const noexcept {
        if (bound_ == std::thread::id{}) { bound_ = std::this_thread::get_id(); return true; }
        return bound_ == std::this_thread::get_id();
    }
private:
    mutable std::thread::id bound_;
};
#endif
```

## 第 1 层:Flag —— refcounted 的 liveness

地基备齐,该把上一篇反复念叨的那个 Flag 立起来了。它是 `RefCountedThreadSafe` 的派生,里头就一枚 `AtomicFlag` 当 liveness 位,外加一个 lazy 绑定的 `SequenceChecker`。整套判活、失效、跨序列析构,都靠它一个对象扛。

```cpp
namespace tamcpp::chrome::internal {

class Flag : public RefCountedThreadSafe {
public:
    Flag() { seq_.detach_from_sequence(); }              // 构造时未绑定(lazy)

    void Invalidate() noexcept {
        // 同序列,或只剩一个引用(允许跨线程析构)
        assert(seq_.called_on_valid_sequence() || has_one_ref());
        invalidated_.Set();                              // release-store
    }
    bool IsValid() const noexcept {
        assert(seq_.called_on_valid_sequence());         // 首次触碰 → 绑定
        return !invalidated_.IsSet();                    // acquire-load
    }
    bool MaybeValid() const noexcept {
        return !invalidated_.IsSet();                    // 无序列断言,任意序列可调
    }
private:
    template <typename> friend class scoped_refptr;      // 允许计数归零时 delete
    ~Flag() = default;                                   // private:受控析构
    mutable SequenceChecker seq_;
    AtomicFlag invalidated_;
};

}  // namespace tamcpp::chrome::internal
```

这块是整套机制的心脏,咱们细看几个笔者反复踩过的地方。

第一眼看构造函数,`Flag()` 里那句 `seq_.detach_from_sequence()` 有点反直觉——刚构造出来就 detach?其实是 lazy 绑定:Flag 构造在哪条序列都行,真正第一次有人碰它(调 `IsValid` 或 `Invalidate`)的时候才记下当前序列。这么做是因为 Flag 经常在一条序列上构造,然后被另一条序列上的 WeakPtr 拿去用,构造期不能贸然绑死。

析构写成 `private ~Flag()` 这一手,是堵"有人拿了裸指针自己 delete"。Flag 的命只能由引用计数管,外面谁都不许插手。`Invalidate` 和 `IsValid` 里的 `assert` 对应 Chromium 的 `DCHECK`,意思是这两个调用默认得在绑定的序列上——否则 lazy 绑定就被破坏了。`Invalidate` 那个 `assert` 还多了个 `|| has_one_ref()` 的后路,是为了允许"最后一个引用的线程跨序列析构"这种合法场景。

真正容易看漏的是 `MaybeValid`,它故意不碰 `seq_`。为什么?因为这个接口的设计意图就是"任意序列都能调一下看个大概",一旦它也走 `IsValid` 那套 acquire + 序列断言,就会在跨序列调用时把 `seq_` 错绑过去,正面的判活结果就再也不能信了。所以它走的是另一条更松的通道,只读原子位、不做序列约束——记住这个区分,下一篇讲测试策略时咱们还会回来。

## 第 2 层:WeakReference + scoped_refptr

Flag 立起来之后,得有个东西替它管引用计数。`scoped_refptr` 是侵入式引用计数的标准操作,笔者这里给个极简版,完整版在前置知识里讲过。`WeakReference` 则是 Flag 的引用侧封装:它持有一个 `scoped_refptr<const Flag>`,对外暴露 `IsValid` / `MaybeValid` / `Reset` 三个动作,基本就是把 Flag 的接口转手一遍。

```cpp
namespace tamcpp::chrome::internal {

template <typename T>
class scoped_refptr {    // 简化版,完整见 pre-01
public:
    scoped_refptr() noexcept = default;
    explicit scoped_refptr(T* p) noexcept : ptr_(p) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(const scoped_refptr& o) noexcept : ptr_(o.ptr_) { if (ptr_) ptr_->add_ref(); }
    scoped_refptr(scoped_refptr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    ~scoped_refptr() { if (ptr_ && ptr_->release()) delete ptr_; }
    scoped_refptr& operator=(scoped_refptr r) noexcept { T* t = ptr_; ptr_ = r.ptr_; r.ptr_ = t; return *this; }
    T* get() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
private:
    T* ptr_ = nullptr;
};

class WeakReference {
public:
    WeakReference() = default;
    explicit WeakReference(const scoped_refptr<Flag>& flag) : flag_(flag) {}
    bool IsValid() const noexcept { return flag_ && flag_->IsValid(); }
    bool MaybeValid() const noexcept { return flag_ && flag_->MaybeValid(); }
    void Reset() noexcept { flag_ = nullptr; }
private:
    scoped_refptr<const Flag> flag_;
};

}  // namespace tamcpp::chrome::internal
```

## 第 3 层:WeakPtr\<T\> —— 用户句柄

终于到用户能摸到的 API 了。WeakPtr 内部就两样东西:一个 `WeakReference`、一个裸指针 `ptr_`。判活全靠前者,真正解引用用后者。咱们上一篇提过的那个 `[[clang::trivial_abi]]`,就挂在这个类头上。

```cpp
namespace tamcpp::chrome {

template <typename T> class WeakPtrFactory;

template <typename T>
class [[clang::trivial_abi]] WeakPtr {
public:
    WeakPtr() = default;
    WeakPtr(std::nullptr_t) noexcept {}

    template <typename U> requires(std::convertible_to<U*, T*>)   // 向上转型
    WeakPtr(const WeakPtr<U>& o) noexcept : ref_(o.ref_), ptr_(o.ptr_) {}
    template <typename U> requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& o) noexcept : ref_(std::move(o.ref_)), ptr_(o.ptr_) {}

    T* get() const noexcept { return ref_.IsValid() ? ptr_ : nullptr; }
    T& operator*() const { assert(ref_.IsValid()); return *ptr_; }     // Chromium 用 CHECK
    T* operator->() const { assert(ref_.IsValid()); return ptr_; }
    explicit operator bool() const noexcept { return get() != nullptr; }
    void reset() noexcept { ref_.Reset(); ptr_ = nullptr; }

    bool maybe_valid() const noexcept { return ref_.MaybeValid(); }
    bool was_invalidated() const noexcept { return ptr_ && !ref_.IsValid(); }
private:
    template <typename U> friend class WeakPtr;
    friend class WeakPtrFactory<T>;
    WeakPtr(internal::WeakReference&& ref, T* ptr) noexcept : ref_(std::move(ref)), ptr_(ptr) {
        assert(ptr);   // 只有 factory 能调
    }
    internal::WeakReference ref_;
    T* ptr_ = nullptr;
};

}  // namespace tamcpp::chrome
```

真正的坑在 `[[clang::trivial_abi]]` 这个标注上。它告诉编译器:这个类型虽然析构不平凡,但您可以放心把它当平凡类型传参,塞寄存器、用 memcpy 搬都没事。这事儿本身有风险——析构时机会被前移,Move 后对象可能提前失效。Chromium 敢这么标,是因为 `ptr_` 是裸的平凡指针,`scoped_refptr` 那部分也能平凡 relocate,整套搬过去 invariant 不破。这个安全前提的完整论证在 [full/pre-06](../full/pre-06-weak-ptr-trivial-abi.md),笔者这里只点一句:别看了眼花就给自己的类型乱贴,这玩意儿贴错了是会 UAF 的。

还有一处得提:私有构造函数加 `friend class WeakPtrFactory`,意思是只有 factory 能铸出 WeakPtr,外部不能凭空捏一个。这是把"铸币权"收口的关键。

## 第 4 层:WeakPtrFactory\<T\>

最后一层,也是用户真正会 new 出来的那个东西。它先内含一个 `WeakReferenceOwner`(Flag 的发行方),再加一个存被观察对象指针的成员。两个一起协作:铸币、批量失效、析构时兜底。

```cpp
namespace tamcpp::chrome {

class WeakReferenceOwner {    // Flag 发行方
public:
    WeakReferenceOwner() : flag_(new internal::Flag()) {}
    ~WeakReferenceOwner() { if (flag_) flag_->Invalidate(); }    // 析构即失效所有
    internal::WeakReference GetRef() const { return internal::WeakReference(flag_); }
    void Invalidate() { flag_->Invalidate(); flag_ = internal::scoped_refptr<internal::Flag>(new internal::Flag()); }  // 失效 + 新 Flag
    void InvalidateAndDoom() { flag_->Invalidate(); flag_ = nullptr; }        // 失效 + 不再铸
    bool HasRefs() const { return !flag_->has_one_ref(); }
private:
    internal::scoped_refptr<internal::Flag> flag_;
};

template <typename T>
class WeakPtrFactory {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr) : ptr_(reinterpret_cast<uintptr_t>(ptr)) { assert(ptr); }
    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(owner_.GetRef(), reinterpret_cast<const T*>(ptr_));
    }
    WeakPtr<T> get_weak_ptr() requires(!std::is_const_v<T>) {
        return WeakPtr<T>(owner_.GetRef(), reinterpret_cast<T*>(ptr_));
    }
    void invalidate_weak_ptrs() { assert(ptr_); owner_.Invalidate(); }
    void invalidate_weak_ptrs_and_doom() { assert(ptr_); owner_.InvalidateAndDoom(); ptr_ = 0; }
    bool has_weak_ptrs() const { return ptr_ && owner_.HasRefs(); }
private:
    WeakReferenceOwner owner_;
    uintptr_t ptr_;    // 非模板依赖的指针存储(可下沉到基类压膨胀)
};

}  // namespace tamcpp::chrome
```

这段代码藏了三个笔者觉得最值得说的细节。

头一个是 `WeakReferenceOwner` 的析构函数,它调了 `Invalidate`。这一句就是"factory 必须放最后一个成员"这条铁律的根。C++ 成员按声明顺序构造、逆序析构,factory 排最后,析构时就最先被调用——这时候对象里其它成员还都活着,Flag 翻个面把所有 WeakPtr 失效掉,正好赶在那些成员一个一个被销毁之前。要是 factory 排前面呢?它先析构、Flag 失效,可紧接着 `buf_` 之类的成员也被析构了,这中间有个窗口:WeakPtr 看着还可能 valid,但对象内部已经开始散架。所以这个顺序不是风格偏好,是内存安全。详细的析构期 race 论证见 [full/02-3](../full/02-3-weak-ptr-factory-and-last-member.md)。

第二个是 `ptr_` 存成了 `uintptr_t`。这看着多此一举——明明是 `T*`,干嘛绕一圈 reinterpret_cast?目的是把指针存储下沉到一个非模板基类。您想想,`WeakPtrFactory<Controller>`、`WeakPtrFactory<Service>`、`WeakPtrFactory<十几个类型>`,每个都实例化一份一模一样的指针操作代码,模板膨胀很吓人。把这部分挪到非模板基类里,每个 T 就只剩薄薄一层派生,二进制体积省得相当可观。Chromium 里这种"用 uintptr_t 换模板瘦身"的小招挺多的。

第三个是 `Invalidate` 和 `InvalidateAndDoom` 的区别。前者失效完马上铸一枚新 Flag,意味着 factory 还能继续发新的 WeakPtr;后者把 Flag 指针直接置空,不再铸——这是给"这个 factory 以后再也不用了"的场景准备的,省一次堆分配。`invalidate_weak_ptrs_and_doom` 那行顺手把 `ptr_` 也清零,后续再调它直接 `assert` 炸掉,防的是 use-after-free 之后的误用。

## 串起来跑

四层都搭完了,咱们把它们拼起来跑一次,看看实际用起来长什么样。下面这个 Controller 是个典型场景:有个会被异步回调的成员函数,配一个 factory 当最后成员,对外甩 WeakPtr 出去。

```cpp
struct Controller {
    void on_done(int v) { /* ... */ }
    std::vector<int> buf_;
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
    WeakPtrFactory<Controller> weak_factory_{this};   // 最后成员
};

// 对象死后回调自动 no-op(接 01 的 OnceCallback,见 full/02-5)
auto task = bind_weak_once(&Controller::on_done, ctrl.get_weak(), 42);
std::move(task).run();      // ctrl 活着 → 调用;ctrl 死后 → 静默 no-op
```

`bind_weak_once` 这一行,是咱们上一篇 OnceCallback 留下的尾巴终于接上的地方。它的核心思路就是把工业级 `InvokeHelper<true>::MakeItSo` 翻译过来:回调跑之前先 `if (!receiver) return;`,这个 receiver 是 WeakPtr,经过 `operator bool` 调 `get()`,`get()` 又查 `IsValid`,一路落到同序列的准确判活。对象活着就正常调用,死了就静默 no-op。完整的回调集成机制——编译期 `kIsWeakMethod` 怎么接线、`MaybeValid` 为什么走独立通道、void 返回约束是怎么来的——见 [full/02-5](../full/02-5-weak-ptr-bind-integration.md),笔者在那篇里专门拆了一遍。

代码到这里全撸完了,Flag 的 acquire/release 保无锁 deref、lazy 序列绑定、factory 析构兜底、`TRIVIAL_ABI` 进寄存器,该落的承诺都落了。但这一篇笔者只顾着把东西写对,还没正经验证过它跑起来到底稳不稳——`MaybeValid` 那条独立通道会不会有 race,跨序列析构的窗口到底关没关上,factory 排错位置到底炸不炸。这些得靠测试和 TSan 说话,下一篇咱们就专挑这些地方往死里打。

## 参考资源

- [Chromium `base/memory/weak_ptr.{h,cc}`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [weak_ptr 设计指南（三）：测试策略与性能对比](./03-weak-ptr-testing.md)
- [WeakPtr 实战（二）：核心骨架与控制块](../full/02-2-weak-ptr-core-skeleton-and-control-block.md)
