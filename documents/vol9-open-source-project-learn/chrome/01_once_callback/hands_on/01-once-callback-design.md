---
chapter: 1
cpp_standard:
- 23
description: 从 Chromium OnceCallback 出发，设计一个 C++23 的 move-only、一次性消费回调组件——第一部分聚焦动机分析和
  API 设计
difficulty: advanced
order: 1
platform: host
prerequisites:
- std::function、std::invoke 与可调用对象
- 移动语义与完美转发
reading_time_minutes: 19
related:
- OnceCallback 与 RepeatingCallback
- bind_once / bind_repeating 与参数绑定
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: once_callback 设计指南（一）：动机与接口设计
---
# once_callback 设计指南（一）：动机与接口设计

## 问题从哪儿来:`std::function` 在异步回调里漏的那几条

笔者做异步编程这些年,踩得最深的坑就是回调被多调一次。场景太典型了:注册一个文件 I/O 完成的回调,指望它跑一遍就收工,结果某条重试路径手滑又触发了一次,回调里刚释放的资源被二次访问,直接段错误。最气人的是这种 bug 在单线程的单元测试里几乎复现不出来,正常路径就跑一次回调,触发条件藏在某种竞态或错误路径里,得跑到线上、压力够大才冒头。

`std::function` 在这种地方帮不上咱们。它能拷贝、能反复调,一个回调对象随便复制到几个地方,谁也不拦。卷二咱们拆过它的内部机制(类型擦除 + SBO),也手写过 `LightCallback` 那个简化版,可那一版的精力全花在压类型擦除的开销上,根本没碰"回调到底该被调几次"这件事。语义没管,运行时崩给您看。

Chromium 的 `base::OnceCallback` 把这件事在类型层面摁住了:`OnceCallback` 是 move-only,它的 `Run()` 只能通过右值调(`std::move(cb).Run()`),调一次回调就被消费掉,再调就是空操作或断言失败。这套约束每天在 Chrome 的任务系统里扛着数百亿次投递,硬是被它跑通了。

咱们这一系列要做的,是把 Chromium 那套设计的精髓提出来,用 C++23 的标准设施(`std::move_only_function` 和 deducing this)重做一遍。原版的实现里手写引用计数、`TRIVIAL_ABI` 注解、函数指针分派表全是硬活,咱们换个路子,把代码量压到可控,语义一点不丢。

先把 `std::function` 在异步场景下的几处漏子点清楚,再动手。

`std::function` 第一条:它能拷贝。一个回调被复制到几个地方,在异步系统里就等于几条执行路径同时攥着同一份副本。回调里要是捕获了 `std::unique_ptr` 这种 move-only 的东西,拷贝直接编译过不去,这还算好的,编译器替您摁住了;要是捕获的是裸指针或引用,几个副本一起跑,竞态就埋下了。Chrome 的思路利落:异步任务回调压根就不该被复制,那就在类型层面不让它拷。

第二条,它能被反复调。`std::function::operator()` 对次数完全没意见,您在一个对象上调一千次它照跑。可异步回调不是这么回事——一次文件读完成的回调被调两次,就是逻辑错误:资源释放两遍、状态转两遍、消息发两遍。这种错在类型系统里看不出来,只能靠运行时断言兜,或者更常见的,靠 bug 现场来发现。

第三条藏得最深:`std::function` 没法表达"调用即消费"。Chrome 的任务投递里,`PostTask(FROM_HERE, callback)` 一调,`callback` 的所有权就交给了任务系统,调用方不该再碰它。可 `std::function::operator()` 是 `const` 限定的,调一次不会改变对象状态——您没法从调用接口本身告诉外界"我这次调用把对象吃掉了"。

三条归到一处:`std::function` 的接口表达不出"这个回调只能调一次,调完即失效"这个约束。`OnceCallback` 就是来填这个空白的。

## Chromium 的回答:`OnceCallback` 的设计取向

Chrome 的回调体系立在一条原则上:消息传递优于锁,序列化优于线程。按这条原则,每个投到任务系统的回调(Chrome 里叫 task)都是一条独立的、一次性的消息——投出去所有权就归了任务系统,执行完就销毁。没有共享,没有复用,也没有歧义。

这条哲学顺着落到 `OnceCallback` 的类型设计上,几个关键取舍列在下表,您扫一眼就能对上号:

| 取舍 | OnceCallback 的选择 | 解决什么 |
|---|---|---|
| 可复制性 | 删除拷贝构造/赋值,仅 move | 类型层面保证任意时刻只有一个持有者 |
| `Run()` 的值类别 | 仅右值可调(`std::move(cb).Run()`) | 语法层面提醒"您在消费它,调完别用" |
| 调用次数 | 内部消费 `BindState`,后续访问是空操作 | 单次消费语义 |

顺带一提,Chrome 还有 `RepeatingCallback`——可复制、可反复调的版本。两个回调类共用同一套 `BindState` 实现,差别只在 `Run()` 的值类别限定和 `BindState` 的所有权语义。一套绑定基础设施同时伺候"一次性任务"和"重复监听器"两种用法,这点设计笔者觉得相当干净。

### Chromium 内部实现概览

咱们不打算逐行啃 Chromium 的源码,但得把它的核心架构摸清楚——咱们要做的 `OnceCallback` 走的是同一套分层思路,只是用 C++23 的标准设施把实现简化掉。

从底往上看,Chromium 的回调系统叠了三层。

最底下一层是 `BindStateBase`,做类型擦除的基类,带引用计数。这里有个笔者第一次读时愣了一下的取舍:它带引用计数,**却不用虚函数**。取而代之的是三个函数指针成员,`polymorphic_invoke_` 管调用、`destructor_` 管析构、`query_cancellation_traits_` 管取消查询。Chrome 团队这么干是为了压二进制膨胀:虚函数会给每个模板实例化生成一个独立的 vtable,项目里要是有一百种 `BindState<Functor, BoundArgs...>` 实例化,就有一百个 vtable;函数指针这路子能复用同一份静态函数,只是指针值不同,代码段不跟着涨。

中间层是 `BindState<Functor, BoundArgs...>`,模板化的具体类,继承自 `BindStateBase`。您把它当成一个"装着所有东西的盒子"就行,盒子里有您的 lambda、绑定的参数,以及基类要的那几个函数指针。它的生命周期交给 `scoped_refptr` 管(Chromium 自己的侵入式引用计数智能指针):`OnceCallback` 在 `Run()` 时释放引用,`RepeatingCallback` 每次 `Run()` 都保持引用。

顶上是 `OnceCallback<Signature>` 和 `RepeatingCallback<Signature>`,用户直接打交道的类型。它们其实就是 `BindStateHolder` 的薄包装,而 `BindStateHolder` 不过是一个挂了 `TRIVIAL_ABI` 注解的 `scoped_refptr<BindStateBase>`。`TRIVIAL_ABI` 是 Clang 的扩展属性,告诉编译器"这个类型可以像 int 一样在寄存器里传",于是 `OnceCallback` 实际大小只有一个指针(8 字节),移动操作就是复制一个指针,轻得离谱。

三层关系一句话:顶层的回调对象只是一个指向中间层盒子的指针,盒子里装着底层要的函数指针和真正的数据。咱们接下来设计的 `OnceCallback` 会保留"外层接口 + 中间存储 + 类型擦除"这套分层骨架,但底层用 `std::move_only_function` 替掉 Chromium 手写的 `BindState` + `scoped_refptr`,用 deducing this 替掉 `const&` 重载加 `static_assert` 那套 hack。

---

## 环境与前置

开工前先确认工具链。`OnceCallback` 这一系列吃的几口 C++23 饭是:`std::move_only_function`(`<functional>` 里头,C++23 引入的 move-only 类型擦除可调用包装器,咱们的核心积木)、deducing this(显式对象参数 `this auto&& self`,让成员函数能推导 `this` 的值类别),偶尔还会碰到 `if consteval` 做编译期条件判断。

编译器这块,GCC 12+ 或 Clang 16+ 能完整支持上述特性,编译时挂 `-std=c++23` 就行。下面这段代码可以快速验环境:

```cpp
#include <functional>

// 验证 std::move_only_function 可用
static_assert(__cpp_lib_move_only_function >= 202110L);

// 验证 deducing this 可用（编译通过即说明支持）
struct Check {
    void test(this auto&& self) {}
};

int main() {
    Check c;
    c.test();
    return 0;
}
```

这段能编过,环境就绑了。不过说句实在话,笔者写这篇文章时,部分编译器的 `std::move_only_function` 实现还有 bug(GCC 12 早期版本在某些 SFINAE 场景下会编不过),保险起见用 GCC 13+ 或 Clang 17+ 的稳定版。

前置知识这边,咱们假设您已经熟这几样(卷二都覆盖过):移动语义和完美转发——`OnceCallback` 的核心就是 move-only,`std::move` 和 `std::forward` 的原理不熟的话,实现过程会很难受(卷二 ch00 移动语义系列);`std::function` 的类型擦除和 SBO——咱们直接在 `std::move_only_function` 上头盖,得明白类型擦除是怎么回事、小对象优化为什么重要(卷二 ch03);`std::invoke` 和统一调用协议——`bind_once` 里头用它统一处理函数指针、成员函数指针、仿函数这些不同的可调用对象(同卷二 ch03);还有可变参数模板和参数包展开——`OnceCallback<R(Args...)>` 的模板特化、`bind_once` 的参数绑定都离不开参数包语法(卷二 ch00 完美转发、卷四模板基础)。

---

## 设计接口:咱们想要什么样的 API

先把目标 API 钉下来,再回头抠每个决策。这是工程师的干活路子——先想清楚"我要什么",再想"怎么做"。

### 核心用法

```cpp
#include "once_callback/once_callback.hpp"

// 1. 构造：从 lambda 创建
using namespace tamcpp::chrome;
auto cb = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
});

// 2. 调用：必须通过右值（std::move）
int result = std::move(cb).run(3, 4);  // result == 7

// 3. 调用后，cb 被消费
// std::move(cb).run(1, 2);  // 运行时断言失败：callback already consumed
```

### 参数绑定

```cpp
// bind_once：预绑定部分参数，返回一个 OnceCallback
using namespace tamcpp::chrome;
auto bound = bind_once<int(int)>(
    [](int x, int y, int z) { return x + y + z; },
    10, 20  // 预绑定前两个参数
);

int r = std::move(bound).run(30);  // r == 60
```

### 取消检查

```cpp
using namespace tamcpp::chrome;
auto cb = OnceCallback<void(int)>([](int x) { /* ... */ });

// 检查回调是否仍然有效
if (!cb.is_cancelled()) {
    std::move(cb).run(42);
}

// maybe_valid：乐观检查，适用于跨序列场景
if (cb.maybe_valid()) {
    // "可能"有效，不保证
    std::move(cb).run(42);
}
```

### 链式组合

```cpp
using namespace tamcpp::chrome;
// then()：将当前回调的返回值传给下一个回调
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;
}).then([](int sum) {
    return sum * 2;
});

int final_result = std::move(pipeline).run(3, 4);
// final_result == 14  (3+4)*2
```

### 接口设计决策分析

API 定下来了,咱们挨个抠背后的决策。

先说为什么用 `run()` 而不是 `operator()`。Chromium 用的是 `Run()`(Google C++ 风格要求首字母大写),咱们走 snake_case 用 `run()`。但这不只是命名规范的差别:`operator()` 太通用了,任何可调用对象都能有;`run()` 这个词明确表达"执行任务"的意思,代码审查时一眼就能看出这是在消费一个 `OnceCallback`,不是在调一个普通可调用对象。语义边界划得清楚,读代码的人也省心。

真正要害的一处:`run()` 凭什么必须通过右值调?这是整个设计里最关键的一环。咱们要的是一种机制,让 `cb.run(args)`(左值调)编不过,让 `std::move(cb).run(args)`(右值调)编得过。Chromium 的实现靠两个重载达成,一个 `Run() &&` 是真执行版,一个 `Run() const&` 里头塞了个 `static_assert(!sizeof(*this))` 拦左值。这 hack 管用,但说实话挺丑。

C++23 的 deducing this(显式对象参数)能让咱们做得更体面。简单讲,它允许成员函数把 `this` 显式写成一个模板参数,编译器会按调用时对象是左值还是右值来推导这个参数的类型。靠着这个,`run(this auto&& self, Args... args)` 就能根据 `self` 推导出的值类别,在编译期把非法用法挡掉:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    // ... 实际调用逻辑
}
```

调用方写 `cb.run(args)` 时,`Self` 推成 `OnceCallback&`(左值引用),`static_assert` 触发,报错信息直接告诉您该怎么改;写 `std::move(cb).run(args)` 时,`Self` 推成 `OnceCallback`(右值),编译通过。deducing this 具体怎么干活、跟 Chromium 那套方案的细致对比,留到下一篇实现篇再展开。

接下来一个小但容易踩的取舍:`is_cancelled()` 和 `maybe_valid()` 凭什么分两个?这个设计直接来自 Chromium 的 `CancellationQueryMode`,差别在安全保证的强弱。`is_cancelled()` 给确定性回答,只能在回调绑定的序列上调,结果准确;`maybe_valid()` 给乐观估计,能从任何线程调,但结果可能过时。实际用的时候,`is_cancelled()` 用在"投递前查一下还有没有意义"这种判断,`maybe_valid()` 用在"跨线程快速看一眼值不值得投"的优化路径。咱们的简化实现里,这两个方法都走 `CancelableToken` 查询,`is_cancelled()` 查状态有效性和令牌是否还在,`maybe_valid()` 就是 `!is_cancelled()` 的薄包装。往后要是需要更细的线程安全语义,在这两个方法上做区分就行。

最后是 `then()` 为什么也得吃掉 `*this`。`then()` 的语义是"把当前回调的执行结果传给下一个回调",这要求当前回调在新回调里被完整捕获。要是 `then()` 不消费 `*this`,同一个回调就会同时待在两个地方,原位置和 `then()` 返回的新回调里各一份,move-only 的语义当场就破了。所以 `then()` 被声明成右值限定成员函数(`then(...) &&`),调完原回调进入已消费状态。

---

## 内部机制:类型擦除的两层架构

接口定好了,该看内部怎么组织。Chromium 那套 `BindStateBase` + `scoped_refptr` + 函数指针表的组合拳做类型擦除,效果是好,但代码量也确实惊人。咱们的路子是让 `std::move_only_function` 把类型擦除和小对象优化这些脏活累活扛掉,把精力集中在消费语义、参数绑定和链式组合这些有嚼头的部分。

### 为什么选 `std::move_only_function`

`std::move_only_function<R(Args...)>` 是 C++23 引入的,定位就是"move-only 版的 `std::function`"。它内部把类型擦除和 SBO 都做好了,行为跟 `std::function` 差不多,只是把拷贝操作删了。

您可能已经留意到 `OnceCallback<R(Args...)>` 这种写法——`R(Args...)` 看着像函数声明,但在模板参数的语境里,它是一种合法的 C++ 类型:函数类型。`int(int, int)` 描述的就是"吃两个 int、返回一个 int 的函数"。咱们靠模板偏特化来拆这个类型,下一篇会细讲这个技巧。

拿 `std::move_only_function` 做内部存储,几处好处凑一块儿。头一个,它把咱们手写类型擦除的活儿全包了,回想卷二那个 `LightCallback`,咱们花了整整一章手写函数指针表、SBO 缓冲区、移动和析构,`std::move_only_function` 把这些全封装好,直接拿来用。再者它天生支持 move-only 的可调用对象:回调要是捕获了 `std::unique_ptr`,`std::function` 因为拷贝语义的要求会直接编不过,`std::move_only_function` 没这个毛病。它的 SBO 实现也是标准库作者精心调过的,绝大多数情况下不需要堆分配,对捕获少量参数的 lambda 来说,性能完全够用。

### 三态管理

把 `std::move_only_function` 引进来之后,有个设计问题得解决:怎么区分"空回调"和"已被消费的回调"?

`std::move_only_function` 自己可以是空的(默认构造或从 `nullptr` 构造),但"空"和"被 `run()` 消费过"是两回事。空回调意思是"从没被赋过值",调它该报个明确的错("callback is null");已消费回调意思是"曾经有值,但已经被调过了",也该报错("callback already consumed"),但报错信息不一样。这点差别在调试时很有用,能帮您一眼看出回调到底卡在哪一步。所以咱们的内部状态得是三态:

```cpp
enum class Status : uint8_t {
    kEmpty,     // 默认构造，从未被赋值
    kValid,     // 持有有效的可调用对象
    kConsumed   // 已被 run() 消费
};
```

配上 `std::move_only_function`,内部存储大概长这样:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    std::move_only_function<FuncSig> func_;
    Status status_ = Status::kEmpty;

    // 取消令牌（可选）
    std::shared_ptr<CancelableToken> token_;
};
```

移动构造时,`func_` 和 `status_` 一起挪过去,源对象状态置成 `kEmpty`。`run()` 执行时先查 `status_` 是不是 `kValid`,执行完把 `func_` 置空、`status_` 设成 `kConsumed`。调试时按 `status_` 的值就能给出精确的报错信息。

### 与 Chromium 原版的取舍

拿 `std::move_only_function` 做底层存储,换来了简洁的实现,代价也不是没有。Chromium 的 `OnceCallback` 大小只有一个指针(8 字节),靠的是 `TRIVIAL_ABI` 注解加引用计数的 `BindState`,回调对象本身只是个指向堆上 `BindState` 的指针。咱们的 `OnceCallback` 包了 `std::move_only_function`(通常 32 字节),加上 `Status` 枚举和可选的 `CancelableToken` 指针(16 字节),总大小约在 56-64 字节。

另一处差别在引用计数。Chromium 的 `BindState` 是引用计数的,允许多个回调共享同一份绑定状态(这对 `RepeatingCallback` 的拷贝语义是必需的)。咱们的实现里,`std::move_only_function` 自己独占所有权,不支持共享。对 `OnceCallback` 的 move-only 语义来说这不碍事,但往后做 `RepeatingCallback` 时得重新琢磨这块。

这些取舍笔者觉得是划得来的:用大小和引用计数的灵活性,换来了大幅降低的实现复杂度。实际用起来,56-64 字节的回调对象在绝大多数场景都不是瓶颈,代码结构清楚,维护和扩展的成本都低得多。

下一篇咱们就进实现阶段:从核心骨架 `run()` 开工,逐步把 `bind_once`、取消检查和 `then()` 链式组合加上去。

## 参考资源

- [Chromium Callback 文档](https://chromium.googlesource.com/chromium/src/+/main/docs/callback.md)
- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
