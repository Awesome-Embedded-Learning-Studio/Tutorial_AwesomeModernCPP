---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 从全局配置表的痛点切入(Chromium 禁全局 ctor/dtor),讲清 NoDestructor 要补的洞,
  定下完整目标 API 与接口决策
difficulty: intermediate
order: 1
platform: host
prerequisites:
- NoDestructor 前置知识（零）：静态存储期、初始化与析构
- NoDestructor 前置知识（一）：placement new 与对齐存储
reading_time_minutes: 10
related:
- NoDestructor 实战（二）：核心实现
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor 实战（一）：动机与接口设计"
---
# NoDestructor 实战（一）：动机与接口设计

[前置知识（零）](./pre-00-static-storage-and-init.md) 里咱们把 Chromium 禁全局 ctor/dtor 的账翻了一遍——SIOF、关停竞态、启动延迟。可问题是,`//base` 满地都是"需要一个全局单例"的场景:默认配置表、feature flag 表、按需生成的随机 nonce。规矩立在那儿,活儿还得干,这就轮到 `base::NoDestructor<T>` 上场了。咱们这一篇先把动机和接口掰扯清楚,实现留给下一篇。

## 从一张全局配置表说起

假设咱们手头有一张"默认配置表",程序各处都要用,内容固定:

```cpp
const std::map<std::string, Config>& DefaultConfig() {
    // 怎么实现这个全局?
}
```

最直觉的写法是甩一个全局变量出去:

```cpp
const std::map<std::string, Config> g_default = LoadDefault();   // ❌ Chromium 禁
```

读起来很顺,可它在 `main` 之前就要生成全局构造器(调 `LoadDefault` + 构造 `std::map`),程序退出时还得跑全局析构器去拆 map。Chromium 挂着的 `-Wglobal-constructors` / `-Wexit-time-destructors` 两个开关会直接把它拒了。那还能怎么改?

---

## 三条现成的路,为什么都不够

**裸函数局部静态** —— 这是 Scott Meyers 那本书里推荐的写法,社区俗称 Meyers singleton:

```cpp
const std::map<...>& DefaultConfig() {
    static const std::map<std::string, Config> g = LoadDefault();   // magic statics:线程安全构造
    return g;
}
```

它确实绕开了全局构造器——首次调 `DefaultConfig()` 才构造,magic statics 把线程安全也顺带管了(见 [pre-00](./pre-00-static-storage-and-init.md))。可笔者第一次用的时候没注意到一个尾巴:`g` 退出时还是会析构。`std::map` 的析构器照样会被登记成全局析构。换句话说,构造这关过了,析构这关没过。

更扎眼的是关停竞态。假设 `g` 里揣着别的全局对象的引用(比如某个 logger 指针),或者反过来——别的全局对象在析构的当口又回头调了 `DefaultConfig()`。这会儿 `g` 说不定已经析构过了,您手里捏的是一具无效引用,程序直接撞上未定义行为。Chromium 的关停路径本来就绕,这种竞态笔者在生产里见过不止一次,是真痛。

**手写 placement new + 跳过析构** —— 自己撸 `alignas(T) char buf[...]`,placement new 把对象摁上去,析构索性不调。这路子能跑,但笔者图省事写了一版之后回头一看,坑比想的密:LSan 兼容(见 [04-4])、`static_assert` 把关、对齐、生命周期,全得自己兜。写两遍就开始重复造轮子了。

三条路摆这儿——裸全局被禁、Meyers singleton 有析构竞态、手写 placement new 重复易错。NoDestructor 就是 Chromium 把第二种路子里"那颗析构的钉子"拔掉、再把第三种路子的样板代码封装好的官方工具。

---

## Chromium 的回答:NoDestructor

NoDestructor 的设计思想笔者觉得可以收成两句话。

第一,不析构。对象一旦构造出来,`~T()` 就再也不会被调用——没有析构,就没有析构顺序,关停竞态从根上没了。代价是"故意泄漏",靠 OS 在进程退出时一把回收内存。在浏览器这种长跑进程里,这点泄漏根本不算账;在嵌入式或者短命工具里您得自己掂量。

第二,和 magic statics 配着用。NoDestructor 通常包在函数局部静态里,靠 C++11 magic statics 把首次构造的线程安全兜住。

两句话合起来就是:`static const NoDestructor<T> x(args...);` 一行,您拿到一个线程安全构造、永不析构的全局单例。全局 ctor 这关靠局部静态延迟构造绕过去了,全局 dtor 这关靠 NoDestructor 不析构绕过去了。这正是 Chromium 风格指南认可的写法。

### 用法示例

```cpp
#include "base/no_destructor.h"

const std::string& GetDefaultText() {
    static const base::NoDestructor<std::string> s("Hello world!");
    return *s;
}
```

`*s` 走的是 `operator*`,返回 `std::string&`。`s` 是 NoDestructor,首调时构造 string(线程安全),程序退出的时候不析构,内存交给 OS 回收。

初始化要是复杂点,塞个 lambda 进去跑 IIFE 也很顺手,比如要按需生成一段随机 nonce:

```cpp
const std::string& GetRandomNonce() {
    static const base::NoDestructor<std::string> nonce([] {
        std::string s(16);
        FillRandom(s.data(), s.size());
        return s;
    }());
    return *nonce;
}
```

---

## 目标 API 长什么样

笔者先把目标 API 摆出来,跟 Chromium 对齐:

```cpp
namespace tamcpp::chrome {

template <typename T>
class NoDestructor {
public:
    // 从任意参数构造(转发给 T 的构造函数)
    template <typename... Args>
    explicit NoDestructor(Args&&... args);

    // 从 T 直接拷贝/移动构造(方便 initializer_list 等)
    explicit NoDestructor(const T& x);
    explicit NoDestructor(T&& x);

    // 不可拷贝
    NoDestructor(const NoDestructor&) = delete;
    NoDestructor& operator=(const NoDestructor&) = delete;

    // 析构:默认(关键!不调 ~T())
    ~NoDestructor() = default;

    // 像 T 一样用
    const T& operator*() const;
    T&       operator*();
    const T* operator->() const;
    T*       operator->();
    const T* get() const;
    T*       get();
};

}  // namespace tamcpp::chrome
```

用法很直接:拿它当函数局部静态,`*nd` 或者 `nd->` 当 T 用就行。

---

## 几处签名决策

签名看着简单,可每一处都是 Chromium 实打实踩过坑之后定下来的。笔者挑几处值得说的拆一下。

**为什么把拷贝删掉。** NoDestructor 持的是一段内联缓冲 `alignas(T) char storage_[sizeof(T)]`,不是指针。允许拷贝的话,得对 storage_ 里的 T 做深拷贝(placement new 一份新的),语义一下子就乱了——T 未必 trivially copyable,浅拷贝是搬字节、深拷贝要走构造,到底哪种?与其让用户踩这个坑,不如直接 `delete` 掉拷贝,把它收在"静态变量容器"这个定位上,别当值类型传来传去。

**为什么不直接继承 T,也不公开 T&。** 继承会把 NoDestructor 拽成 T 的 is-a 关系,而它本质上是个容器,语义不对;再说 T 可能是 `final`,继承根本走不通。直接公开内部 T& 成员也不行,那等于把 storage_ 的细节漏出去了。Chromium 选的是智能指针那一套——`operator*` / `operator->` / `get()`,让 NoDestructor 行为上像"指向 T 的指针"。这么一来,`static const NoDestructor<std::string> s(...)` 用起来跟 `std::string*` 几乎一样自然。

**`~NoDestructor() = default`,这一处是命门。** 这一行看着平平无奇,却是整套设计的根。`= default` 让编译器生成的析构函数去析构它的成员 `storage_`,而 `storage_` 是 char 数组,平凡析构,什么都不做。所以 `~NoDestructor()` 跑完,`~T()` 根本不会被调——T 是 placement new 摁上去的,析构得手动调,可这里偏不调。笔者第一次读到这儿愣了一下:那为啥不直接 `= delete`?后来想通了,`= delete` 会拦住整个对象的生命周期管理,作为成员或者基类时根本用不了;`= default` 才能让 NoDestructor 自己正常生死,只是让里头的 T 装聋作哑。

要是不嫌麻烦,改成 `~NoDestructor() { reinterpret_cast<T*>(storage_)->~T(); }`,那就跟 Meyers singleton 没两样了,关停竞态原封不动地回来,整个工具白做。所以"不调 `~T()`"不是疏忽,是刻意的核心选择。

**为什么不用 `[[clang::no_destroy]]` 属性。** Clang 其实有这么个属性,标在变量上就让它的析构不跑:

```cpp
[[clang::no_destroy]] static const std::string s = "...";   // 不析构
```

笔者一开始也嘀咕,既然属性能干这活,何必再裹一层类?后来扒了 Chromium 的注释才明白账得这么算。属性是 Clang 专属,GCC 和 MSVC 上根本搬不动,可移植性这一关就过不去。它也只管析构这一件事,加不了 `static_assert` 把关,拦不住对平凡类型的误用。更要命的是 LSan 兼容那一套 hack(见 [04-4])、还有类型安全的 API 门面,这些非得封装成类才能塞进去。属性更底层、更轻,但工业代码用封装好的工具更稳,出了问题也好排查。

---

## 教学版和 Chromium 的取舍

跟前几个系列一样,教学版咱们只保核心机制——placement new + 不析构 + magic statics 配合 + `static_assert` 把关,外围那些工业级的零碎先扒掉:

| 维度 | Chromium | 教学版 |
|---|---|---|
| 存储与 placement new | 完整 | 同 |
| `~NoDestructor()=default` 跳过析构 | 完整 | 同 |
| static_assert 把关 | 2 条(平凡 ctor+dtor / 平凡 dtor) | 同 |
| LSan reachability hack | `#ifdef LEAK_SANITIZER` 持 storage_ptr_ | 省略或注明(见 04-4) |
| Chromium 宏 `BASE_EXPORT` | 有 | 省略 |

核心机制一字不差。LSan 兼容这一块涉及 sanitizers 的黑魔法,单独拎出来到 04-4 那篇讲,免得这一篇主线上分心。

---

## 环境先搭一下

NoDestructor 本身用 C++17(`alignas` / `std::forward`)就够,部分 `static_assert` 用 C++20 的 `_v` 变量模板写起来更利落,但 C++17 也能写。咱们用 C++20,跟前几个系列对齐。

### 编译器要求

GCC 11+ 或 Clang 12+,`-std=c++20`。

### 验证代码

```cpp
#include <new>
#include <type_traits>

// 验证 alignas + placement new 可用
struct Foo { int x; };
alignas(Foo) char buf[sizeof(Foo)];

int main() {
    new (buf) Foo{42};                                   // placement new 在 buf 上构造
    return reinterpret_cast<Foo*>(buf)->x - 42;          // 0,验证访问正确
}
```

这段能跑过,环境就算齐活了。配套工程在 `code/volumn_codes/vol9/full_tutorial_codes/chrome_design/`,咱们从 04-2 开始往里加 `23`~`25` 这一批 NoDestructor 示例。

---

## 参考资源

- [Chromium `base/no_destructor.h`](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [Clang `[[clang::no_destroy]]` 属性](https://clang.llvm.org/docs/AttributeReference.html#no-destroy)
- [isocpp FAQ —— Meyers singleton 与关停问题](https://isocpp.org/wiki/faq/ctors#construct-on-first-use)
- [NoDestructor 前置知识（零）：静态存储期、初始化与析构](./pre-00-static-storage-and-init.md)
