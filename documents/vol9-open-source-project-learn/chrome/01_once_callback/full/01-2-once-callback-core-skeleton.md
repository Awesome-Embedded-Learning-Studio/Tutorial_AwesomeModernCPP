---
chapter: 1
cpp_standard:
- 23
description: 从零开始五步搭建 OnceCallback 的类骨架——模板偏特化、数据成员、构造函数约束、run() 消费语义、查询接口
difficulty: beginner
order: 2
platform: host
prerequisites:
- OnceCallback 实战（一）：动机与接口设计
- OnceCallback 前置知识（一）：函数类型与模板偏特化
- OnceCallback 前置知识（四）：Concepts 与 requires 约束
- OnceCallback 前置知识（五）：std::move_only_function
- OnceCallback 前置知识（六）：Deducing this
reading_time_minutes: 9
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: OnceCallback 实战（二）：核心骨架搭建
---
# OnceCallback 实战（二）：核心骨架搭建

上一篇咱们把"为什么需要 OnceCallback"和目标 API 的长相捋清楚了。动机讲完,笔者就有点手痒——光看接口不过瘾,得自己一行行把它撸出来,才知道哪些设计是真金,哪些是纸面好看。

这一篇咱们就动手。先把类骨架从零搭起来,分五步走,每一步只在前一步上加一层。骨架立住了,后面的 `bind_once`、取消令牌、`then()` 全都是往这套骨架上挂件,不会有伤筋动骨的改动。前置知识那七篇咱们默认您已经过完了——函数类型、模板偏特化、`requires`、`move_only_function`、deducing this,底下直接拿来用,不再回头讲。

## 第一步：主模板与偏特化

前置知识(一)讲过的"函数类型 + 模板偏特化"模式,现在直接落到 OnceCallback 上。

```cpp
namespace tamcpp::chrome {

// 主模板：只有声明，没有定义
// 如果有人写了 OnceCallback<int>（传了非函数类型），编译器会报错
template<typename FuncSignature>
class OnceCallback;

// 偏特化：FuncSignature 是 R(Args...) 形式的函数类型时匹配
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这个偏特化里
public:
    using FuncSig = ReturnType(FuncArgs...);
    // ...
};

} // namespace tamcpp::chrome
```

您写 `OnceCallback<int(int, int)>` 的时候,编译器先把 `int(int, int)` 这个整体喂给主模板的 `FuncSignature`,再回头一看偏特化能把 `int` 拆成 `ReturnType`、把 `{int, int}` 拆成 `FuncArgs`,于是偏特化胜出。这个"先收成整体、再被偏特化拆开"的套路,是这类回调库的通用骨架——`std::function`、Chromium 的 `RepeatingCallback` 都是同一个模子。`FuncSig` 这个类型别名顺手存一份完整签名,后面声明 `std::move_only_function<FuncSig>` 时直接拿来用,省得再拼一遍。

---

## 第二步：数据成员——三个核心存储

有了类型骨架,咱们往里头填数据成员。OnceCallback 要管住自己的状态,得靠三样东西:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
public:
    using FuncSig = ReturnType(FuncArgs...);

private:
    enum class Status : uint8_t {
        kEmpty,     // 从未被赋值（默认构造）
        kValid,     // 持有有效的可调用对象
        kConsumed   // 已被 run() 调用过
    } status_ = Status::kEmpty;

    std::move_only_function<FuncSig> func_;          // 类型擦除的可调用对象
    std::shared_ptr<CancelableToken> token_;         // 可选的取消令牌
};
```

三个成员里,`func_` 是类型擦除的核心。lambda、函数指针、仿函数,形态千差万别,`func_` 把它们统一塞进 `FuncSig` 签名的同一个 `operator()`,这就是咱们要的"一套接口接住所有可调用对象"。

真正值得多嘴的是 `status_` 这个三态枚举。您可能会问:为什么不能只靠 `func_` 判空?因为 `std::move_only_function` 的 `operator bool()` 只能区分"空"和"非空",而 OnceCallback 的语义要求更细——"从未被赋值"和"已经被 `run()` 消费过"是两件不同的事,后者是个明确的契约违规(回调只能跑一次),它得跟"出厂就是空的"区分开。更要命的是,`move_only_function` 移动之后的状态在标准里是"未指定但有效",靠它判空本身就靠不住。所以笔者老老实实给状态位单开一个枚举,不指望底层容器替咱们管语义。前置知识(五)里讲过这个坑,这里就是落地。

`token_` 是可选的取消令牌,默认空指针(不启用取消机制),要靠 `set_token()` 显式挂上。这一篇笔者先把它摆在这儿占位,取消机制后面有专门一篇细讲。

---

## 第三步：构造函数与 requires 约束

数据成员就位,咱们给这个类配上构造函数。这里有个模板构造函数的老坑:它会在重载决议里抢移动构造的活儿,必须拿 `requires` 约束挡一道。前置知识(四)讲过原理,这里看它怎么落地。

```cpp
// not_the_same_t concept：F 退化后不是 T
template<typename F, typename T>
concept not_the_same_t = !std::is_same_v<std::decay_t<F>, T>;

template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // ... 数据成员 ...

    // 禁止拷贝
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;

public:
    // 模板构造函数：接受任意可调用对象
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& function)
        : status_(Status::kValid), func_(std::move(function)) {}

    // 默认构造：创建空回调
    explicit OnceCallback() = default;

    // 移动构造
    OnceCallback(OnceCallback&& other) noexcept
        : status_(other.status_),
          func_(std::move(other.func_)),
          token_(std::move(other.token_)) {
        other.status_ = Status::kEmpty;
    }

    // 移动赋值
    OnceCallback& operator=(OnceCallback&& other) noexcept {
        if (this != &other) {
            status_ = other.status_;
            func_ = std::move(other.func_);
            token_ = std::move(other.token_);
            other.status_ = Status::kEmpty;
        }
        return *this;
    }
};
```

这段里最常用的是模板构造函数。您写 `OnceCallback<int(int)>([](int x) { return x; })` 的时候,走的就是它——`Functor` 被推导成 lambda 的闭包类型,`requires not_the_same_t` 把"传入的恰好是 `OnceCallback` 本身"这种情形挡在模板外面,让移动构造函数去接手。要是没这一条约束,模板构造函数会贪心地劫持拷贝/移动,编译期重载决议就乱了套。`std::move(function)` 把可调用对象移进 `func_`,`status_` 同时置为 `kValid`。

默认构造函数就平淡多了,产出的是一个空回调:`status_` 是 `kEmpty`(成员初始化器给定的默认值),`func_` 和 `token_` 都空着。它存在主要是为了让 OnceCallback 能放进容器、能延迟赋值。

移动构造这边有个笔者刻意做的取舍。`func_` 和 `token_` 靠 `std::move` 转移,`status_` 直接拷过来,这些都不意外。意外的是源对象被咱们主动设回 `kEmpty`——而不是依赖 `move_only_function` 移动后那个"未指定"的状态。道理前面讲过:语义要握在自己手里,底层容器移动后是空的还是有效的,标准留着口子,咱们不能赌。移动赋值走的是同一套逻辑,多一个自赋值检查。

---

## 第四步：run() 的 deducing this 实现

这一步咱们把"只能跑一次"的契约,用编译期手段卡到调用点上。`run()` 借 deducing this 在编译期拦住左值调用,只有右值(也就是 `std::move(cb).run(...)`)才放行,转发到内部的 `impl_run()`。

```cpp
// 声明（在类体内）
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

// 实现（在类体外，once_callback_impl.hpp 中）
template<typename ReturnType, typename... FuncArgs>
template<typename Self>
auto OnceCallback<ReturnType(FuncArgs...)>::run(this Self&& self, FuncArgs&&... args)
    -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "once_callback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

机制其实很直白。调用方写 `cb.run(args)`(没加 `std::move`)的时候,`Self` 被推导成 `OnceCallback&`——左值引用,`static_assert` 当场炸,而且报错信息里直接把正确写法 `std::move(cb).run(...)` 甩在调用方面前,不用他自己猜。写成 `std::move(cb).run(args)`,`Self` 推导成 `OnceCallback`(非引用),编译通过,转发进 `impl_run`。

`impl_run` 才是真正干活的地方:

```cpp
template<typename ReturnType, typename... FuncArgs>
ReturnType OnceCallback<ReturnType(FuncArgs...)>::impl_run(FuncArgs... args) {
    assert(status_ == Status::kValid);

    // 取消检查：消费但不执行
    if (token_ && !token_->is_valid()) {
        status_ = Status::kConsumed;
        func_ = nullptr;
        if constexpr (std::is_void_v<ReturnType>) {
            return;
        } else {
            throw std::bad_function_call{};
        }
    }

    // 消费：先把 func_ 拿出来，再更新状态，最后执行
    auto functor = std::move(func_);
    func_ = nullptr;
    status_ = Status::kConsumed;

    if constexpr (std::is_void_v<ReturnType>) {
        functor(std::forward<FuncArgs>(args)...);
    } else {
        return functor(std::forward<FuncArgs>(args)...);
    }
}
```

这段实现里笔者最想跟您掰扯的,是消费顺序。

`impl_run` 不是直接调用 `func_`,而是先把它 move 出来作为局部变量 `functor`,再把成员 `func_` 置空、`status_` 置 `kConsumed`,最后才执行 `functor`。三步的次序不是随手排的——状态先标记好,可调用对象先脱离成员、落到栈上,然后再跑。这么一来,即便 `functor` 内部抛异常往外冒,`status_` 也早就是 `kConsumed` 了,回调对象不会卡在一个"func_ 还在但状态说没消费"的中间态。异常安全就是这么抠出来的:把"已消费"这个不可逆的状态,提前到执行之前。

取消检查挪到了执行最前面。如果令牌挂上了且已失效,回调直接消费、不执行。这里 void 返回走 `return`,非 void 返回抛 `std::bad_function_call`——后者乍看激进,但您站到调用方的角度想就通了:人家写 `auto x = std::move(cb).run(...)`,指望拿到一个值回去,您这边给不出任何有意义的返回值,与其返回个未定义的东西让人家用出花来,不如抛异常把问题摊在台面上。这是"失败要响亮"的取舍,跟 WeakPtr 里 `operator*` 失效用 `CHECK` 是一类思路。

剩下的 `if constexpr` 是为 void 返回类型开的编译期分支。void 没法走"调用然后 return 结果"的常规路径,`if constexpr (std::is_void_v<ReturnType>)` 在编译期就选好走哪条路,void 的走"调用但不赋值",非 void 的走"调用并 return"。这是速查篇里讲过的标准模式,这里不展开了。

---

## 第五步：查询接口

骨架还差最后一块——一组查询接口,让调用方在执行前能探一下回调现在到底什么状态。

```cpp
[[nodiscard]] bool is_cancelled() const noexcept {
    if (status_ != Status::kValid) return true;
    if (token_ && !token_->is_valid()) return true;
    return false;
}

[[nodiscard]] bool maybe_valid() const noexcept {
    return !is_cancelled();
}

[[nodiscard]] bool is_null() const noexcept {
    return status_ == Status::kEmpty;
}

explicit operator bool() const noexcept {
    return !is_null() && !is_cancelled();
}

void set_token(std::shared_ptr<CancelableToken> token) {
    token_ = std::move(token);
}
```

`is_cancelled()` 这一段判定的口径,笔者得跟您讲清楚:只要状态不是 `kValid`,一律算作"已取消"——空回调和已消费回调在这层语义上归为一类,对调用方来说都是"别指望能跑了"。再叠一层令牌检查,令牌挂了且失效也算取消。`maybe_valid()` 现阶段就是 `!is_cancelled()`,留这个名字是为后面引入跨序列语义时做扩展。`is_null()` 只盯一件事——是否从未被赋值,跟取消是两码事。`operator bool()` 把"非空"和"未取消"两个条件合在一起,是调用点最常用的判活入口。

几个查询方法清一色挂了 `[[nodiscard]]`。调用方拿这些方法的返回值就是冲着做判断去的,忽略返回值的调用基本等于手滑,编译器得替咱们吼一声。`operator bool()` 那个 `explicit` 是老规矩,挡掉隐式转换,免得 `cb` 悄悄溜进本该要 `int` 的位置。

---

## 验证核心骨架

骨架搭完,笔者习惯性地先跑几个最朴素的场景压一压——别上来就追求边界,先把基本盘打实:

```cpp
#include "once_callback/once_callback.hpp"
#include <cassert>
#include <memory>

int main() {
    using namespace tamcpp::chrome;

    // 1. 非 void 返回
    OnceCallback<int(int, int)> add([](int a, int b) { return a + b; });
    assert(std::move(add).run(3, 4) == 7);

    // 2. void 返回
    bool called = false;
    OnceCallback<void()> side_effect([&called] { called = true; });
    std::move(side_effect).run();
    assert(called);

    // 3. move-only 捕获
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> capture_move([p = std::move(ptr)] { return *p; });
    assert(std::move(capture_move).run() == 42);

    // 4. 移动语义
    OnceCallback<int()> movable([] { return 1; });
    OnceCallback<int()> moved_to = std::move(movable);
    assert(movable.is_null());            // 源对象变空
    assert(std::move(moved_to).run() == 1);  // 目标对象有效

    return 0;
}
```

这四个 case 是骨架的最低门槛:非 void 回调要拿到正确的返回值、void 回调的副作用要正常触发、捕获 `unique_ptr` 的 move-only 回调用完资源得释放、移动之后源对象变空目标对象能用。全绿,骨架就能扛住后续的组件挂载;任何一个挂了,不用急着往下走,先回头查这一步。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
