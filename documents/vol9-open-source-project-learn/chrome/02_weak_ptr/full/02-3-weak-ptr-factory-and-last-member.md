---
chapter: 1
cpp_standard:
- 17
- 20
description: 实现 WeakPtrFactory——铸币、InvalidateWeakPtrs/AndDoom 的差别,以及为什么它必须
  是最后一个成员(析构逆序论证),含组合 vs 继承取舍
difficulty: intermediate
order: 3
platform: host
prerequisites:
- WeakPtr 实战（二）：核心骨架与控制块
- WeakPtr 前置知识（五）：模板友元与 uintptr_t 类型擦除
reading_time_minutes: 13
related:
- WeakPtr 实战（四）：序列亲和性与 lazy 绑定
- WeakPtr 实战（一）：动机与接口设计
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 内存管理
title: "WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法"
---
# WeakPtr 实战（三）：WeakPtrFactory 与「最后成员」惯用法

[02-2](./02-2-weak-ptr-core-skeleton-and-control-block.md) 里咱们手搓了一枚 Flag 当铸币的原材料。但说真的,您总不能每次想发个 WeakPtr 都去 new 一个 Flag、自己盯着引用计数吧?那也太累了。Chromium 把这套琐碎活儿打包成了 `WeakPtrFactory<T>`——挂在被观察对象身上的一个"铸币厂":您要 WeakPtr,它铸;对象该走了,它一次性把所有铸出去的 WeakPtr 全部作废。

这一篇咱们把 factory 撸出来,顺带啃掉它最出名的那条使用规矩——`WeakPtrFactory<T> weak_factory_{this}` 得排成类的最后一个成员。这条惯用法看着像吹毛求疵,实际是用对用错的分界,笔者第一次见还纳闷至于这么讲究吗,后来真踩过坑才明白人家为什么特意写进头文件顶部 EXAMPLE。咱们用析构逆序把它彻底讲透。

## WeakPtrFactory 内部那枚 Flag

factory 手里攥着的就一样东西:一枚 Flag,所有从它铸出去的 WeakPtr 共享同一枚。所以 factory 内部包了一个 `WeakReferenceOwner`——就是 Flag 的发行方兼持有方(`weak_ptr.cc:82-89`):

```cpp
// 发行方:持有一枚 Flag,负责失效
class WeakReferenceOwner {
public:
    WeakReferenceOwner() : flag_(make_ref<Flag>()) {}   // 构造即铸一枚新 Flag
    ~WeakReferenceOwner() {
        if (flag_) flag_->Invalidate();                 // 析构时作废所有 WeakPtr
    }
    WeakReference GetRef() const { return WeakReference(flag_); }   // 铸币
    // ...
private:
    scoped_refptr<Flag> flag_;
};
```

这段读起来很平实,但每一行都有戏。构造就铸一枚新 Flag;`GetRef()` 每次吐一个指向同一枚 Flag 的 `WeakReference`;析构那句 `flag_->Invalidate()` 才是真正埋的雷管——它让"factory 死了,所有 WeakPtr 立刻失效"成了 factory 析构函数的自动副作用,您压根不用记得去手动调 `invalidate`。笔者第一次读这儿的反应是:这设计真省心,把最容易忘的那一步塞进析构链里了。

`WeakPtrFactory<T>` 在 `WeakReferenceOwner` 之上,只多加了一样东西——一个指向被观察对象的裸指针:

```cpp
// Platform: host | C++ Standard: C++20
namespace tamcpp::chrome {

template <typename T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    WeakPtrFactory() = delete;
    explicit WeakPtrFactory(T* ptr)
        : WeakPtrFactoryBase(reinterpret_cast<uintptr_t>(ptr)) {}

    WeakPtrFactory(const WeakPtrFactory&) = delete;
    WeakPtrFactory& operator=(const WeakPtrFactory&) = delete;

    // 铸币:const factory 发 WeakPtr<const T>
    WeakPtr<const T> get_weak_ptr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // 非 const 重载:发 WeakPtr<T>(pre-04 的 requires)
    WeakPtr<T> get_weak_ptr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }

    // 主动批量失效(对象还活着,但想让所有 WeakPtr 失效)
    void invalidate_weak_ptrs() {
        assert(ptr_);
        weak_reference_owner_.Invalidate();   // 失效旧 Flag + 铸一枚新 Flag
    }

    void invalidate_weak_ptrs_and_doom() {
        assert(ptr_);
        weak_reference_owner_.InvalidateAndDoom();   // 失效 + 不再铸新 Flag
        ptr_ = 0;
    }

    bool has_weak_ptrs() const { return ptr_ && weak_reference_owner_.HasRefs(); }

private:
    internal::WeakReferenceOwner weak_reference_owner_;
    // ptr_ 在非模板基类 WeakPtrFactoryBase 里,uintptr_t(prec 见 pre-05)
};

}  // namespace tamcpp::chrome
```

代码里有几处笔者想专门点一下,都是 [pre-05](./pre-05-weak-ptr-template-friend-and-uintptr-t.md) 那篇讲的技巧。`reinterpret_cast<uintptr_t>(ptr)` 把 `T*` 存成整数,这样能下沉到非模板基类 `WeakPtrFactoryBase`,省得每个 `T` 都重新生成一份模板代码;`get_weak_ptr` 里再用 `reinterpret_cast<T*>(ptr_)` 转回来。还有那两个 `get_weak_ptr` 重载,用的是 [pre-04](./pre-04-weak-ptr-concepts-and-requires.md) 的成员函数 `requires(!std::is_const_v<T>)`,const 正确性直接挂到签名上——const factory 出 `WeakPtr<const T>`,非 const 出 `WeakPtr<T>`,编译期就分得清清楚楚。

## invalidate_weak_ptrs 和 invalidate_weak_ptrs_and_doom,差在哪

factory 提供了俩失效方法,光看名字您可能跟我当初一样一脸懵——这不都失效吗?差别就藏在"失效之后 factory 还能不能继续铸币"这事儿上。咱们对着 `WeakReferenceOwner` 的两份实现看(`weak_ptr.cc:103-113`):

```cpp
void WeakReferenceOwner::Invalidate() {
    assert(flag_);
    flag_->Invalidate();                 // 作废旧 Flag
    flag_ = make_ref<Flag>();            // 铸一枚新 Flag,factory 可继续铸币
}

void WeakReferenceOwner::InvalidateAndDoom() {
    assert(flag_);
    flag_->Invalidate();                 // 作废旧 Flag
    flag_.reset();                       // 不再持新 Flag,factory 进入"已死"态
}
```

差别就在作废旧 Flag 之后那一行。`invalidate_weak_ptrs()` 作废旧 Flag 后,紧接着 `flag_ = make_ref<Flag>()` 又铸一枚新的——所有已有 WeakPtr 集体失效,但 factory 自己还喘着气,可以接着 `get_weak_ptr()` 铸新的,新铸出来的 WeakPtr 之间共享那枚新 Flag。这套适合"对象进入新阶段,旧观察者该清退了,但后面还要接新观察者"的场景。

`invalidate_weak_ptrs_and_doom()` 就狠一点,作废旧 Flag 之后不铸新的,还顺手把 `ptr_` 清零——factory 直接进入"已死"状态,之后您再调 `get_weak_ptr()` 拿到的就是无效结果。它比上一个更省事,连一次 Flag 分配都省了。顾名思义 doom,这是给"这对象彻底不再用了"的收尾场景准备的。

这俩的差别在 [02-6](./02-6-weak-ptr-sequence-affinity-and-lazy-binding.md) 性能对比里还会再露一次脸。不过说句实在话,日常九成的场景,您根本不会显式调这俩——光靠下面要讲的 factory 析构自动失效就够用了。

---

## 重头戏:「最后成员」惯用法

接下来这条是这一篇真正想讲的东西。Chromium 的 `weak_ptr.h` 顶部那段 EXAMPLE,专门把这条规矩写在最显眼的位置(`weak_ptr.h:22-26`):

> Member variables should appear before the WeakPtrFactory, to ensure that any WeakPtrs to Controller are invalidated before its members variable's destructors are executed.

翻译过来一句话:成员变量得声明在 `WeakPtrFactory` 前面,factory 放最后。笔者第一次读到这条时心里嘀咕——至于吗,成员顺序还能影响正确性?还真能。咱们一条一条拆。

### 先垫个底:C++ 的析构是逆序的

这是 C++ 一条基础规则,但容易在用 WeakPtr 时被忘掉:对象析构时,成员按声明顺序的**逆序**析构——先声明的后死,后声明的先死。所以如果 `WeakPtrFactory` 是最后一个声明的成员,它就**最先**被析构;反过来您要是把它放最前面,它就成了**最后**才析构的那个。记住这个"逆序",下面推论全靠它。

### 再垫一条:factory 析构 = 失效所有 WeakPtr

前面 `WeakReferenceOwner::~WeakReferenceOwner()` 里那句 `flag_->Invalidate()` 是关键——factory 一析构,它铸出去的所有 WeakPtr 跟着作废。换句话说,"factory 什么时候死"直接决定了"所有 WeakPtr 什么时候失效"。

### 两条叠起来:为什么 factory 非得放最后

把上面这两条拼一块儿,推论自己就冒出来了。咱们假设 `Controller` 有几个普通成员外加一个 factory,然后故意把两种声明顺序都写一遍对比:

```cpp
// ✗ 错误顺序:factory 放前面
class BadController {
public:
    void on_work_done() { /* 用 buf_ */ }
private:
    WeakPtrFactory<BadController> weak_factory_{this};   // 先声明 → 最后析构
    std::vector<int> buf_;                                // 后声明 → 先析构
};

// ✓ 正确顺序:factory 放最后
class GoodController {
public:
    void on_work_done() { /* 用 buf_ */ }
private:
    std::vector<int> buf_;                                // 先声明 → 最后析构
    WeakPtrFactory<GoodController> weak_factory_{this};   // 后声明 → 先析构
};
```

先看 `BadController` 这版反例。析构按逆序走:`buf_` 先死,然后才轮到 `weak_factory_`,这时候才想起来失效所有 WeakPtr。问题就出在这俩中间那个窗口——`buf_` 已经析构了,可所有 WeakPtr 还活蹦乱跳地"有效"着。偏偏这种时候要是有个异步任务拿着 WeakPtr 解引用 `Controller`,它顺顺当当进 `on_work_done()`,一头撞上已经析构的 `buf_`。UAF,稳稳的。笔者当年在类似场景里查过一个偶现的 ASAN 报红,根因就是这个顺序。

`GoodController` 就把顺序倒过来了:`weak_factory_` 最后声明,所以最先析构,一析构就把所有 WeakPtr 作废,然后 `buf_` 才轮到死。等到 `buf_` 真的开始析构时,外头已经没有任何"有效"的 WeakPtr 能碰到它了,后续解引用顶多拿到个 `nullptr`。安全。

「最后成员」惯用法,根子就这么一句话:让 factory 在其它成员之前先析构,借它析构时那一下自动失效,把其它成员的析构期给罩住。

### 一个容易踩的边界:守护的是成员析构期,不是析构函数体

这里有个细节笔者特意要讲清楚,因为它最容易让人误判——咱们研究核验的时候专门确认过这一条。factory 放最后,罩住的是**成员的析构期**;它**不**拦着您在对象自己的析构函数体里用 WeakPtr。咱们把时间线摊开看:

```text
GoodController 析构:
  ① 析构函数体执行(此时所有成员还活着,WeakPtr 仍有效)
  ② 成员按逆序析构:
       weak_factory_ 先析构 → 所有 WeakPtr 失效  ← 守门发生在这里
       buf_        后析构
```

也就是说,在 ① 析构函数体执行那会儿,WeakPtr 还有效。这其实是符合直觉的——析构体里您常常还想引用同对象的其它成员(比如通知观察者一句"我要走了"),这种时候 WeakPtr 有效反而顺手。真正作废发生在 ② 进入成员析构之后,目的就是确保后续再有任何 deref 都碰不到半销毁的成员。这条边界 Chromium 源码注释里没明写,笔者是读着代码自己推出来的,您记一下,免得日后以为"对象一进析构 WeakPtr 就立刻失效"而误判。

---

## 为什么 Chromium 只留了组合式这一条路

您翻遍 Chromium 现在的 `//base`,能拿到 WeakPtr 的官方姿势就一种:把 `WeakPtrFactory<Controller> weak_factory_{this}` 当成员塞进 `Controller`。这就是「最后成员」惯用法的载体,Chromium 选它是有理由的——失效时机您说了算,能对非自身类型用(极端点,`WeakPtrFactory<bool>` 都行),也不污染继承链。

其实历史上还有过一种继承式的 `SupportsWeakPtr<T>`,继承它就白得一个 `GetWeakPtr()`。听上去更省事对吧?但它把人往不安全的用法上带,后来 Chromium 索性从 `//base` 里挪走了,现在 `weak_ptr.h` 里 grep `SupportsWeakPtr` 已经搜不到。笔者提它一嘴不为别的,就怕您翻老代码、老文档时撞见这名字发懵。新代码一律走组合式,理解了组合式,那套老机制无非是把 factory 藏进基类,本质没差。

---

## 把 02-1 那个坑用 factory 重写一遍

光说不练假把式,咱们把 [02-1](./02-1-weak-ptr-motivation-and-api-design.md) 那个悬空回调的场景,用刚撸好的 factory 重写一遍,看它怎么把 UAF 给堵死的:

```cpp
// Platform: host | C++ Standard: C++20
#include <functional>
#include <iostream>
#include <vector>

class Controller {
public:
    void on_work_done(int v) {
        buf_.push_back(v);
        std::cout << "got " << v << ", buf size=" << buf_.size() << '\n';
    }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }

    ~Controller() = default;
private:
    std::vector<int> buf_;                                  // 先声明
    WeakPtrFactory<Controller> weak_factory_{this};         // 最后成员!
};

int main() {
    using namespace tamcpp::chrome;
    WeakPtr<Controller> wp;                                 // 先声明,待会儿填

    {
        Controller c;
        wp = c.get_weak();
        std::cout << (wp ? "alive" : "dead") << '\n';       // alive
        if (wp) wp->on_work_done(7);                        // got 7, buf size=1
    }   // c 离开作用域:weak_factory_ 先析构 → wp 失效 → buf_ 才析构

    std::cout << (wp ? "alive" : "dead") << '\n';           // dead
    if (wp) {
        wp->on_work_done(8);                                // 不会进这里
    } else {
        std::cout << "controller gone, skip\n";             // 走这条
    }
    return 0;
}
```

咱们跑一下看看。输出会按顺序蹦出来:`alive` → `got 7, buf size=1` → `dead` → `controller gone, skip`。重点盯最后一行——`Controller` 析构之后,`wp` 已经自动失效,`if (wp)` 这道闸把对已析构对象的访问拦在了外面。02-1 那个让人头疼的悬空回调,到这儿被 factory 析构时的自动失效给彻底堵死了,您一行防护代码都不用写。

到这里 WeakPtr 的几样核心机制——铸币、失效、解引用守门——算是凑齐了。不过咱们还有一条使用契约一直绕着没讲:**WeakPtr 的解引用和失效,得发生在绑定时的同一个序列上**。这条契约值得正面掰扯清楚,顺带把 factory 的 lazy 序列绑定也讲一下,下一篇见。

## 参考资源

- [Chromium `base/memory/weak_ptr.h` —— WeakPtrFactory 与顶部 EXAMPLE](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium `base/memory/weak_ptr.cc` —— WeakReferenceOwner](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.cc)
- [WeakPtr 实战（二）：核心骨架与控制块](./02-2-weak-ptr-core-skeleton-and-control-block.md)
- [WeakPtr 前置知识（五）：模板友元与 uintptr_t 类型擦除](./pre-05-weak-ptr-template-friend-and-uintptr-t.md)
