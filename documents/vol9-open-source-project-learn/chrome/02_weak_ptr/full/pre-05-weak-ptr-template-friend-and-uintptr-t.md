---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: 拆解 WeakPtr 的两处模板工程技巧——template friend 解决跨类型私有访问,uintptr_t 把指针
  存储下沉到非模板基类压膨胀,以及 RAW_PTR_EXCLUSION 的取舍
difficulty: intermediate
order: 5
platform: host
prerequisites:
- WeakPtr 前置知识（四）：concepts 与 requires 在 WeakPtr 里的应用
- WeakPtr 前置知识（一）：侵入式引用计数与 scoped_refptr
reading_time_minutes: 10
related:
- WeakPtr 实战（二）：核心骨架与控制块
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 内存管理
- weak_ptr
title: "WeakPtr 前置知识（五）：模板友元与 uintptr_t 类型擦除"
---
# WeakPtr 前置知识（五）：模板友元与 uintptr_t 类型擦除

WeakPtr 读到这里,笔者之前一直绕着没讲的两个细节,这会儿得单独拎出来。一个藏在转换构造里——`WeakPtr<U>` 升格成 `WeakPtr<T>` 时,要直接伸手读对方的私有成员,这事儿靠一行 `template friend` 撑着。另一个藏在 `WeakPtrFactory` 的基类那儿,它把指针存成了 `uintptr_t` 而不是 `T*`,听着像多此一举,真要追下去,是个压模板膨胀的实在招。

俩都不是语言层面的花活。咱们看完,WeakPtr 那个看起来拧巴的类层次也就顺了。

---

## 跨类型友元:WeakPtr\<U\> 要访问 WeakPtr\<T\>

[前置知识（四）](./pre-04-weak-ptr-concepts-and-requires.md) 里那个转换构造,咱们再贴一遍:

```cpp
template <typename U>
    requires(std::convertible_to<U*, T*>)
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

第一眼笔者没觉得不对——直到反应过来它直接读了 `other.ref_` 和 `other.ptr_`,这俩是 `WeakPtr<U>` 的私有成员。私有嘛,按规矩只有类自己和朋友碰得;而 `WeakPtr<T>` 和 `WeakPtr<U>`(当 `U != T`)是两个完全不同的类型,默认互相看不见。

那这构造是怎么编过去的?靠的是一行模板友元(`weak_ptr.h:291-292`):

```cpp
template <typename T>
class WeakPtr {
public:
    // ...
private:
    template <typename U>
    friend class WeakPtr;   // 所有 WeakPtr<U> 都是 WeakPtr<T> 的朋友

    internal::WeakReference ref_;
    RAW_PTR_EXCLUSION T* ptr_ = nullptr;
};
```

`template <typename U> friend class WeakPtr;` 这一句,意思是把任意 `U` 套进去得到的 `WeakPtr<U>`,统统算本类的友元。一刷子把所有实例化版本之间的私有壁垒全拆了。

这是模板代码里的常规操作。但凡"同模板不同实例化要互访"——转换构造、`WeakPtr<Base>` 和 `WeakPtr<Derived>` 互拷内部状态——模板友元就是答案。它和非模板的 `friend class Foo;` 区别就多一层 `template <typename U>`,语义是"把这个模板的所有实例化都列成朋友"。

顺带一提,Chromium 把 `WeakPtrFactory<T>` 也列成了友元(`weak_ptr.h:293-294`)——factory 铸币时得直接构造 `WeakPtr`、写它的私有成员。这张友元名单,本质就是"谁需要直接碰内脏"的清单。

---

## uintptr_t:把指针存成整数

`WeakPtrFactory` 的类层次长这样(`weak_ptr.h:332-340`):

```cpp
namespace internal {
class BASE_EXPORT WeakPtrFactoryBase {
protected:
    WeakPtrFactoryBase(uintptr_t ptr);
    ~WeakPtrFactoryBase();
    internal::WeakReferenceOwner weak_reference_owner_;
    uintptr_t ptr_;
};
}  // namespace internal

template <class T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    explicit WeakPtrFactory(T* ptr)
        : WeakPtrFactoryBase(reinterpret_cast<uintptr_t>(ptr)) {}
    // ...
};
```

盯着 `uintptr_t ptr_;` 这一行看。基类没把指针存成 `T*`,而是存成了一个整数。派生类构造时拿 `reinterpret_cast<uintptr_t>(ptr)` 把 `T*` 拍扁成整数塞进去,回头用(`GetWeakPtr`)再 `reinterpret_cast<T*>(ptr_)` 转回来(`weak_ptr.h:375,383`)。

直接存 `T*` 行不行?功能上完全行。那为什么还要绕这一圈——为了压模板膨胀。

`WeakPtrFactory<T>` 是模板,每个不同的 `T` 都得实例化一份完整类代码。`ptr_` 如果是 `T*`,那"存指针、读指针"这套和 `T` 八竿子打不着的操作,会被编译器给每个 `T` 都生成一份。可生成的机器码又完全一样——指针就是指针,`T*` 在机器层面都不过是个地址。

把 `ptr_` 换成 `uintptr_t`(一个跟 `T` 无关的整数类型),再把它沉到一个非模板基类 `WeakPtrFactoryBase` 里,这招就好使了:"存指针"的逻辑归了非模板基类,管它 `T` 是什么,基类代码只生成一份。模板派生类只留着和 `T` 真有关系的活——类型转换、`GetWeakPtr` 的返回类型。把"类型无关的脏活"从模板里扒出来,扔进非模板基,这是压二进制体积的老办法。搁浏览器这种有几千个 `WeakPtrFactory<X>` 实例化的工程里,省下来的字节数是真能看见的。

安全性也别担心。`reinterpret_cast` 在 `T*` 和 `uintptr_t` 之间往返,标准是认的——指针和足够大的整数类型互转,`uintptr_t` 当年就是为这事儿设计的。唯一的前提是转回来的 `T` 得跟当初存进去的一致,这个由 `WeakPtrFactory<T>` 自己的类型守着。

---

## RAW_PTR_EXCLUSION:为什么 `ptr_` 不用 raw_ptr

这一段是 Chromium 家特有的,但笔者觉得值得单独讲——它把"通用工具在特定场景下反而帮倒忙"这事摆得很清楚。

Chromium 给 `//base` 做内存安全加固的时候,把大批裸指针 `T*` 换成了 `raw_ptr<T>`——一个挂了 PartitionAlloc backup-ref 计数的智能指针包装。好处是能抓 use-after-free:被释放的内存不会马上被复用,而是留在隔离区里,真有人偷摸访问,当场逮住。

可 `WeakPtr::ptr_` 偏偏不用 `raw_ptr<T>`,就留着裸 `T*`,还特意标了个 `RAW_PTR_EXCLUSION`(`weak_ptr.h:311`):

```cpp
// This pointer is only valid when ref_.is_valid() is true. Otherwise, its
// value is undefined (as opposed to nullptr). The pointer is allowed to
// dangle as we verify its liveness through `ref_` before allowing access to
// the pointee. We don't use raw_ptr<T> here to prevent WeakPtr from keeping
// the memory allocation in quarantine, as it can't be accessed through the
// WeakPtr.
RAW_PTR_EXCLUSION T* ptr_ = nullptr;
```

注释把话说透了:`ptr_` 就是允许悬垂的。WeakPtr 的设计本就是"对象可以先死,只要 flag 翻了面,deref 之前会被 `IsValid()` 挡住"。也就是说,对象析构之后,`ptr_` 会在一段时间里合法地指向一块已经释放的内存——这是设计自己要的悬垂,不是 bug。

换成 `raw_ptr<T>` 会怎么样?这个悬垂指针会让 PartitionAlloc 把那块内存扣在隔离区里不回收(backup-ref 计数还非零),直到所有 WeakPtr 全销毁。一个本来就允许悬垂的类型,被这么一包,等于白拖一大片内存。所以 WeakPtr 退回到裸指针,拿 `RAW_PTR_EXCLUSION` 明说:这儿有悬垂风险,我心里有数,是设计的一部分,别拿 raw_ptr 套它。

笔者在这儿琢磨了半天。安全工具不是堆得越多越稳,得看它跟对象的生命周期模型搭不搭。WeakPtr 那套"允许悬垂 + flag 守门",跟 raw_ptr 那套"绝不悬垂 + 隔离区",俩模型本来就顶着的。硬凑一块,安全没多一分,内存倒先被拖垮。

---

## 最小复刻:非模板基 + 模板派生

光讲不练印象不深,笔者把这套分层扒个最小骨架出来,咱们跑一遍看看:

```cpp
// Platform: host | C++ Standard: C++17
#include <cstdint>
#include <iostream>

namespace internal {
class FactoryBase {
protected:
    FactoryBase(uintptr_t p) : ptr_(p) {}
    ~FactoryBase() { ptr_ = 0; }            // 非模板基:这份代码只生成一次
    uintptr_t ptr_;                          // 指针存成整数,和 T 无关
};
}  // namespace internal

template <typename T>
class Factory : public internal::FactoryBase {
public:
    explicit Factory(T* p) : FactoryBase(reinterpret_cast<uintptr_t>(p)) {}

    T* get() const {
        return reinterpret_cast<T*>(ptr_);   // 转回来,类型由模板保证
    }
};

struct Foo { int x = 42; };

int main() {
    Foo f;
    Factory<Foo> fac(&f);
    std::cout << fac.get()->x << '\n';       // 42
    return 0;
}
```

`FactoryBase` 压根不知道 `T` 是什么,它只管"存一个整数"。模板膨胀只剩那层薄薄的 `Factory<T>` 派生,核心存储逻辑大家共用一份。WeakPtr 真实结构比这复杂(还挂着 refcount 的 flag),但分层思路一模一样。

两处模板工程技巧拆完了。`template<typename U> friend class WeakPtr;` 让同模板的不同实例化(`WeakPtr<Base>` 和 `WeakPtr<Derived>`)互访私有成员,这是转换构造能直接读 `other.ref_`/`other.ptr_` 的前提;`WeakPtrFactoryBase` 把指针存成 `uintptr_t` 沉到非模板基类,让"存指针"这份跟类型无关的逻辑只生成一份,压模板膨胀;`RAW_PTR_EXCLUSION` 则是个反向取舍——WeakPtr 允许 `ptr_` 悬垂的模型跟 `raw_ptr` 的隔离区顶着,所以特意退回裸指针。

前置知识就剩最后一块了:`TRIVIAL_ABI`——让有非平凡析构的 WeakPtr 还能按平凡类型传参进寄存器。

## 参考资源

- [cppreference: friend declaration 与 template friend](https://en.cppreference.com/w/cpp/language/friend)
- [cppreference: uintptr_t](https://en.cppreference.com/w/cpp/types/integer)
- [Chromium `base/memory/weak_ptr.h` —— 类层次](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
- [Chromium MiraclePtr / raw_ptr 设计文档](https://chromium.googlesource.com/chromium/src/+/main/docs/unsafe_relocations.md)
