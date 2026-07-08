---
chapter: 0
cpp_standard:
- 20
description: 在 01 系列 concepts 基础上,聚焦 WeakPtr 如何用 std::convertible_to 与成员函数 requires
  子句,把 const 正确性与向上转型约束焊进类型签名
difficulty: intermediate
order: 4
platform: host
prerequisites:
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
- WeakPtr 前置知识（零）：弱引用与生命周期难题
reading_time_minutes: 10
related:
- WeakPtr 实战（一）：动机与接口设计
- WeakPtr 前置知识（五）：模板友元与 uintptr_t 类型擦除
tags:
- host
- cpp-modern
- intermediate
- concepts
- 类型安全
- weak_ptr
title: "WeakPtr 前置知识（四）：concepts 与 requires 在 WeakPtr 里的应用"
---
# WeakPtr 前置知识（四）：concepts 与 requires 在 WeakPtr 里的应用

## 先把工具摆桌上

[前一篇](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md) 把 concepts 的基本盘讲过了——`requires` 子句怎么挂、约束怎么短路。这篇不重复那些,咱们直接看 WeakPtr 把 concepts 拿去干了哪两件实事:管住向上转型的合法性(`WeakPtr<Derived>` 能不能塞给 `WeakPtr<Base>`),还有 const 正确性(const 的 factory 不许发可变弱指针)。

这两处在 Chromium 的 `weak_ptr.h` 里就寥寥几行,笔者第一次读差点滑过去。但它们恰恰是 concepts 最典型的工程用法——读者扫一眼签名上的 `requires(...)`,就知道这个构造在什么类型关系下才存在,不用翻注释、不用追实现。咱们先把概念回顾一下,再拆它们各自落在哪。

一句话带过(细节回 01 那篇看):concept 是个编译期谓词,`requires(expr)` 把它挂在模板参数或成员函数上,意思是"这个模板/重载只在谓词为真时才存在"。WeakPtr 这回用到两个谓词。`std::convertible_to<U*, T*>` 判的是 `U*` 能不能隐式转成 `T*`——同类型、派生类到公有基类都算,正好覆盖向上转型那档子事。`std::is_const_v<T>` 判的是 `T` 有没有顶 const,配个 `!` 就能区分 `WeakPtrFactory<T>` 和 `WeakPtrFactory<const T>`。

---

## 转换构造:WeakPtr\<U\> 到 WeakPtr\<T\> 的向上转型

笔者先讲个最直白的诉求。手上有 `WeakPtr<Derived>`,需要 `WeakPtr<Base>`,咱们本能就觉得应该能直接塞过去——`Derived*` 本来就能转成 `Base*`。但反过来不行(`Base*` 撑不成 `Derived*`),`WeakPtr<int>` 想转 `WeakPtr<Foo>` 这种乱七八糟的更别想。

Chromium 把这条规则用一条 `requires` 子句刻在签名上(`weak_ptr.h:211-214`):

```cpp
template <typename T>
class WeakPtr {
public:
    // ...
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}

    template <typename U>
        requires(std::convertible_to<U*, T*>)
    WeakPtr(WeakPtr<U>&& other)
        : ref_(std::move(other.ref_)), ptr_(std::move(other.ptr_)) {}
    // 对应的 operator= 同理
};
```

注意几个细节。首先这是个成员模板,不是普通构造——外层 `WeakPtr<T>` 已经把 `T` 定下来,内层再模板化出一个 `U`,意思是"从任意 `WeakPtr<U>` 构造我"。然后 `requires(std::convertible_to<U*, T*>)` 挂在这个成员模板上,只有指针能转时它才参与重载。还有一处容易看漏:它跟默认的拷贝/移动构造是两条分开的路(注释里专门写了 "separate from the (implicit) copy and move constructors"),别把它们搅一块。

效果是这样的:

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base {};

WeakPtr<Derived> wd = factory_derived.get_weak_ptr();
WeakPtr<Base> wb = wd;           // ✓ Derived* → Base* 合法

WeakPtr<Base> wb2 = wb;          // ✓ 同类型,也走 convertible_to(B* → B*)
WeakPtr<Derived> wd2 = wb;       // ✗ Base* → Derived* 非法,这个构造不参与,编译错
WeakPtr<int> wi = wb;            // ✗ Base* → int* 非法,编译错
```

后两种错都在编译期就被挡下了。因为用的是 concepts 而不是 SFINAE,编译器报错会直愣愣地指向"约束不满足",不会甩您一脸模板替换栈。把类型契约挪到签名上,该不该转、能不能转,读签名就明白。

### 为什么不用 SFINAE

老写法是这样的:

```cpp
// 老式 SFINAE:可读性差,报错信息糟糕
template <typename U,
          typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
WeakPtr(const WeakPtr<U>& other) : ref_(other.ref_), ptr_(other.ptr_) {}
```

功能等价,但 `typename = std::enable_if_t<...>` 这种凭空塞默认模板参数的套路,读起来比 `requires(...)` 别扭得多,约束失败时编译器也只能硬抠模板替换失败的细节给您看。Chromium 迁到 C++20 以后,新代码一律走 concepts,WeakPtr 这几行就是迁移后的产物。

---

## 成员函数 requires:const 正确性与可变重载

WeakPtrFactory 那边还有个更有意思的玩法,把 `requires` 直接挂在成员函数上,按 `T` 是不是 const 来挑重载。咱们看 `GetWeakPtr`(`weak_ptr.h:374-384`):

```cpp
template <class T>
class WeakPtrFactory : public internal::WeakPtrFactoryBase {
public:
    // const 版本:factory 是 const 的,只能发 WeakPtr<const T>
    WeakPtr<const T> GetWeakPtr() const {
        return WeakPtr<const T>(weak_reference_owner_.GetRef(),
                                reinterpret_cast<const T*>(ptr_));
    }

    // 非 const 版本:factory 不是 const 的,发 WeakPtr<T>(可变)
    WeakPtr<T> GetWeakPtr()
        requires(!std::is_const_v<T>)
    {
        return WeakPtr<T>(weak_reference_owner_.GetRef(),
                          reinterpret_cast<T*>(ptr_));
    }
    // ...
};
```

这里头藏着个笔者第一次没看明白的小机巧。`WeakPtrFactory<T>` 上同时挂着两个 `GetWeakPtr`:一个 `const` 成员函数返回 `WeakPtr<const T>`;另一个非 `const` 成员函数返回 `WeakPtr<T>`,可后者偏偏带个 `requires(!std::is_const_v<T>)`。

凭什么要加这个 `requires`?咱们拿 `WeakPtrFactory<const Foo>` 想想——这时候 `T = const Foo`,`std::is_const_v<T>` 是真。如果非 const 版的 `GetWeakPtr()` 没约束,它会被实例化成 `WeakPtr<const Foo> GetWeakPtr()`(非 const 成员),返回类型跟 const 版一模一样,可 const 属性却不一样——重载解析要么歧义,要么选错。`requires(!std::is_const_v<T>)` 在 `T` 本身就是 const 的时候把这个非 const 重载掐掉,只留 const 版本,语义就清爽了:factory 是 const 的、或者 `T` 是 const 的,都只能拿到 `WeakPtr<const T>`;只有 factory 非 const 而且 `T` 非 const,才拿得到可变的 `WeakPtr<T>`。

这套约束把 const 正确性塞进了类型系统,塞得很深。您从一个 const 对象上,在类型层面就拿不到指向它可变状态的 `WeakPtr<T>`——不靠运行时纪律,编译器替您盯着。`GetMutableWeakPtr()`(`weak_ptr.h:386-391`)用的是同一个 `requires(!std::is_const_v<T>)`,保证"可变"路径只在类型允许时才存在。

### 一个最小复刻

咱们自己搓个最小版,验证约束真在编译期起作用:

```cpp
// Platform: host | C++ Standard: C++20
#include <concepts>
#include <type_traits>

struct Base { virtual ~Base() = default; };
struct Derived : Base {};

template <typename T>
class MiniWeakPtr {
public:
    MiniWeakPtr() = default;
    // 向上转型转换构造
    template <typename U>
        requires(std::convertible_to<U*, T*>)
    MiniWeakPtr(const MiniWeakPtr<U>&) {}
};

int main() {
    MiniWeakPtr<Derived> wd;
    MiniWeakPtr<Base> wb = wd;          // ✓
    // MiniWeakPtr<Derived> wd2 = wb;   // ✗ 编译错:Base* → Derived* 不满足 convertible_to
    return 0;
}
```

把注释那行打开,编译器当场就跳脚(Clang 报 `constraints not satisfied`,GCC 报 `conversion from ... to non-scalar type ... requested`,措辞不一样,意思都是"约束没满足")。什么类型关系合法、什么不合法,从注释里挪进了编译器的检查项。

---

## 为什么这事值得单独讲

您可能会嘀咕:不就是两行 `requires` 吗,至于单开一篇?至于。这两处是 WeakPtr 类型签名上的安全网,分量比看着重。

转换构造那条约束拦的是"用 WeakPtr 做不安全的向下转型"。这是个常被忽视的 UAF 入口——向下转型拿错类型的指针,一访问就是越界或者 UB,运行期您根本抓不到。挂上 `requires(std::convertible_to<U*, T*>)` 之后,这种错在编译期就死了。const 重载那条约束管的是另一头:让"const 对象不能被弱引用改状态"这条规矩不用人盯着守,类型系统替咱们兜底。

更深一层的价值在于,它们示范了 concepts 的工程用法——不是炫技的花活,是把语义约束用类型系统写出来,让接口自己解释自己。咱们到 02-2 搭骨架的时候,会照抄这两条 `requires`,您会发现它们跟 Chromium 源码几乎一字不差。

---

WeakPtr 里两处 concepts 的用法到这儿就拆完了。转换构造挂 `requires(std::convertible_to<U*, T*>)`,让 `WeakPtr<Derived> → WeakPtr<Base>` 合法,反向和无关转换在编译期就拒掉;成员函数 `GetWeakPtr`/`GetMutableWeakPtr` 挂 `requires(!std::is_const_v<T>)`,把 const 正确性塞进类型系统,const 对象拿不到可变弱指针。两处都在演示同一件事——concepts 的真正价值是把语义契约挪到签名上,让编译器去守那些原本靠人盯的规矩。

WeakPtr 还有另一处模板技巧——用 `template friend` 解决跨类型私有访问,以及 `WeakPtrFactory` 为什么把指针存成 `uintptr_t`,放在紧接着的那篇里拆。

## 参考资源

- [cppreference: std::convertible_to](https://en.cppreference.com/w/cpp/concepts/convertible_to)
- [cppreference: requires clause](https://en.cppreference.com/w/cpp/language/constraints)
- [OnceCallback 前置知识（四）：Concepts 与 requires 约束](../../01_once_callback/full/pre-04-once-callback-concepts-and-requires.md)
- [Chromium `base/memory/weak_ptr.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/memory/weak_ptr.h)
