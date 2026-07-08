---
chapter: 1
cpp_standard:
- 11
- 17
- 20
description: 讲清 NoDestructor 的使用边界——该用(函数局部静态+非平凡析构 T)、不该用(局部变量/成员、
  平凡析构、平凡可构造、很少用)、constinit 全局与 magic statics 的取舍,附决策表
difficulty: intermediate
order: 3
platform: host
prerequisites:
- NoDestructor 实战（二）：核心实现
- NoDestructor 前置知识（零）：静态存储期、初始化与析构
reading_time_minutes: 9
related:
- NoDestructor 实战（四）：LSan 泄漏权衡与 reachability hack
tags:
- host
- cpp-modern
- intermediate
- 内存管理
- RAII
title: "NoDestructor 实战（三）：何时用、何时不用"
---
# NoDestructor 实战（三）：何时用、何时不用

NoDestructor 是把锋利的刀,用对地方省心,用错地方埋雷——真泄漏、白占内存、把 bug 藏起来都干得出来。Chromium 自己在源码注释里用整整一节 "Caveats"(no_destructor.h:15-46)列了哪些坑不能踩。笔者把这一节翻来覆去读了三遍,发现它的边界其实就一句话:**唯一推荐姿势是函数局部静态,且 T 非平凡析构**。这篇咱们就把这条线内外的事情掰开讲。

## 该用的场景:函数局部静态 + 非平凡析构 T

标准用法长这样,笔者建议您把它当模板背下来:

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // ✓ 函数局部静态
    return *x;
}
```

三个条件同时成立才轮得到它出场。第一,T 得是非平凡析构的,像 `std::string`、`std::vector`、`std::map` 这类,直接做函数局部静态会产生全局析构器,正中 NoDestructor 要消掉的靶心。这里有个笔者踩过的反例坑:`std::mutex` 看着像非平凡,可它在 libstdc++/libc++ 实现里其实是平凡析构的,根本不需要 NoDestructor,笔者第一次手滑给它套了一层,review 时被指出来才反应过来。第二,得包在函数局部静态里,靠 magic statics 保证首次构造线程安全,这事儿后面单独说。第三,这对象得是整个程序生命周期都要用的,临时凑一下的不算。三个条件缺一个,NoDestructor 就从省心变成多余甚至有害。

## 哪些地方别动它

NoDestructor 误用的场景,Chromium 的注释一条条点过来了。笔者按"坑的隐蔽程度"重新排了个序,从最显眼到最容易看漏,咱们挨个过。

**最显眼也最致命的:拿它装局部变量或成员变量。** 您一看这写法就该本能地皱眉:

```cpp
void f() {
    base::NoDestructor<std::string> s("temp");   // ❌ 真泄漏!
    // f 返回后,s 不析构,string 的堆分配永远不释放(直到程序退出)
}
```

NoDestructor 的全部意义就是"不析构",这前提是对象本来就该活到程序结束。您把它套在局部变量或成员上,对象本来该早早销毁,可析构被 NoDestructor 摁住永远不跑,内存就真的漏了。这不是"程序退出统一回收"那种无害泄漏,是"该回收的时候不回收"的真泄漏。源码注释(no_destructor.h:18-20)把这事儿说得很硬:**Must not be used for locals or fields**。

**再往下:平凡析构的 T。** 这种情况 NoDestructor 是多余的,您压根不需要它:

```cpp
static const base::NoDestructor<int> x(42);   // ❌ int 平凡析构,不需要 NoDestructor
```

T 平凡析构(像 `int`、`double`、POD struct),它的函数局部静态本来就不产生全局析构器,直接裸静态就行。NoDestructor 的 static_assert 会拒绝您(第二条断言),但比编译报错更要紧的是:您一开始就没认清这玩意儿在这儿没活干。直接写干净:

```cpp
static const int x = 42;   // ✓ 平凡析构,无全局析构器
```

**更隐蔽的一类:T 既平凡可构造又平凡析构。** 这种连函数局部静态都嫌重,该上 `constinit`:

```cpp
static const base::NoDestructor<uint64_t> seed(GetRand());   // ❌
```

`uint64_t` 这种能直接用 `constinit` 做全局常量初始化的,套 NoDestructor 纯属给自己加戏。源码注释(no_destructor.h:33-44)给的正面例子是这样的:

```cpp
const uint64_t GetUnstableSessionSeed() {
    static const uint64_t kSessionSeed = base::RandUint64();   // ✓ 平凡析构,无需 NoDestructor
    return kSessionSeed;
}
// 或更好:constinit uint64_t g_seed = constexpr_value;
```

**最容易看漏的:很少用到的数据,别拿 NoDestructor 缓存。** 这一条坑在它不会报错、不会泄漏,只是悄悄吃内存:

```cpp
const BigTable& GetRareTable() {
    static const base::NoDestructor<BigTable> t(BuildBigTable());   // ⚠ 慎用
    return *t;
}
```

这张表要是整个程序生命周期只用一两次,拿 NoDestructor 缓存它就是浪费内存——编译器在 bss 段给 `BigTable` 预留了空间,程序运行期间一直占着,哪怕您只用一次。源码注释(no_destructor.h:28-31)的建议很直接:rarely used data 按需创建,别缓存。改成这样,用完即析构,不长期占内存:

```cpp
// 按需创建(用完即析构,不长期占内存)
BigTable GetRareTable() { return BuildBigTable(); }   // 返回值,临时用
```

---

## constinit 全局这条路,走不通

NoDestructor 的标准用法是函数局部静态。那能不能做成全局?源码注释(no_destructor.h:22-26)提到一笔"constinit 可构造的 T 可作全局,但要标 constinit"——笔者一开始真去试,结果撞墙了,**实测编不过**:

```cpp
constinit const base::NoDestructor<MyConstexprType> g_data(args...);   // ⚠ 编译失败
```

原因藏在构造函数里:NoDestructor 内部要调 placement new(`new (storage_) T(...)`),而 placement new 压根不是 `constexpr`,于是 NoDestructor 的构造函数被显式标成非 constexpr(真实头 no_destructor.h:95 注释白纸黑字写着 "Not constexpr")。`constinit` 要求初始化器是常量表达式,这一条过不去,编译器甩给您一句 `constinit variable does not have a constant initializer`。笔者还顺手翻了 Chromium 的 unittest,**里头没有任何一处 constinit 全局 NoDestructor**,清一色函数局部静态。

所以这一节真正要记住的就两句话:constinit 可构造的 T,直接用 `constinit T g(...)`,别拿 NoDestructor 包它,常量初始化在编译期就办妥了,运行时一片干净;非 constinit 可构造、但非平凡析构的 T,才是函数局部静态 `NoDestructor<T>` 的唯一实用舞台。源码注释里那条"标 constinit 做全局",更像是理想或者历史遗留,当前实现(placement new 构造)压根撑不起来。

---

## magic statics 复盘:线程安全靠它,不靠 NoDestructor

有件事得点破:NoDestructor 自己**不加锁**,它一个字都没提线程安全。它能在并发下安然无恙,全靠 C++11 的 magic statics,也就是函数局部静态变量的初始化,标准给您兜了并发安全这一层(细节见 [pre-00](./pre-00-static-storage-and-init.md))。

```cpp
const T& GetGlobal() {
    static const base::NoDestructor<T> x(args...);   // magic statics 保 x 只构造一次,线程安全
    return *x;
}
```

这里有个笔者想强调的因果:一旦您脱离函数局部静态,比如把 NoDestructor 挂成成员、或者做非 constinit 全局,magic statics 的保护就跟着没了,多线程并发去碰一个还没初始化的状态,竞态就冒出来了。所以"包在函数局部静态里"这件事同时干了两活:一是绕开全局构造器,二是把线程安全的担子转交给 magic statics。这两件事叠在一起,才把 NoDestructor 的标准用法限定在函数局部静态这一种姿势上。

---

## 决策表

把上面散在各处的判断收拢成一张表,您下次拿不准的时候直接对号入座:

| 场景 | 用什么 | 理由 |
|---|---|---|
| 全局/静态 + 非平凡析构 T + 整个程序要用 | **`static const NoDestructor<T>`**(函数局部静态) | 绕开 ctor/dtor + magic statics 线程安全 |
| 平凡析构 T | 裸 `static T x = ...;` | 不产生全局析构器,无需 NoDestructor |
| 平凡可构造 + 平凡析构 T | `constinit T x = ...;` 或 `constexpr` | 编译期初始化,无运行时代码 |
| 局部变量 / 成员变量 | 普通 `T x(...)` 或 `unique_ptr<T>` | NoDestructor 会真泄漏 |
| 很少用的数据 | 按需创建函数(返回值) | NoDestructor 缓存浪费内存 |
| 需要 T 的析构副作用(刷盘/通知) | 函数局部静态 `static T x;`(接受析构) | NoDestructor 跳过析构副作用 |

到此 NoDestructor 的使用边界就清楚了。但它还给咱们留了个反直觉的尾巴:它故意让对象不析构,LeakSanitizer 看在眼里就是一坨泄漏,这两个该怎么共存?Chromium 的 reachability hack 是怎么把这事儿圆过去的?这就是下一篇要拆的。

---

## 参考资源

- [Chromium `base/no_destructor.h` —— Caveats 节](https://source.chromium.org/chromium/chromium/src/+/main:base/no_destructor.h)
- [cppreference: constinit(C++20)](https://en.cppreference.com/w/cpp/language/constinit)
- [cppreference: magic statics(变量初始化线程安全)](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)
- [NoDestructor 前置知识（零）：静态存储期、初始化与析构](./pre-00-static-storage-and-init.md)
