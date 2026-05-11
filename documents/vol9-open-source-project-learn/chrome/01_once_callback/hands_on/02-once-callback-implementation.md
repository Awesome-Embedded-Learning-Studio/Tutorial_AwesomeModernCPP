---
title: "once_callback 设计指南（二）：逐步实现"
description: "从核心骨架到完整组件，四步走读 once_callback 的实现策略，重点理解模板技巧和所有权设计"
chapter: 1
order: 2
tags:
  - host
  - cpp-modern
  - advanced
  - 回调机制
  - 函数对象
difficulty: advanced
platform: host
cpp_standard: [23]
reading_time_minutes: 30
prerequisites:
  - "once_callback 设计指南（一）：动机与接口设计"
related:
  - "bind_once / bind_repeating 与参数绑定"
  - "回调取消与组合模式"
---

# once_callback 设计指南（二）：逐步实现

## 引言

上一篇我们完成了动机分析和接口设计，确定了 `OnceCallback` 的目标 API 和内部架构。这一篇我们正式上手写代码。不过先说好——这一篇的重点不是"把完整实现端上来"，而是带你理解每一步的设计思路和关键技术选型。我们会看到代码的关键骨架，但不会贴出完整的、可直接编译的头文件——那些细节留给课后练习和第三篇的测试验证。

实现分四步，每一步都建立在前一步的基础上：先搞定核心的 `run()` 语义，再加参数绑定，然后是取消检查，最后是 `then()` 链式组合。每一步我们只关注"这个组件长什么样"和"关键的模板技巧是什么"，不会逐行解读实现。

> **学习目标**
>
> - 理解 `OnceCallback<R(Args...)>` 的模板偏特化模式和内部存储设计
> - 掌握 deducing this、requires 约束、lambda capture pack expansion 等高级模板技巧在实际组件中的应用
> - 理解 `bind_once()` 的参数绑定机制和 `then()` 的所有权链设计

---

## 第一步：核心骨架 — 从模板偏特化开始

### 为什么是 `OnceCallback<R(Args...)>` 这种写法

你可能已经注意到了，我们声明 `OnceCallback` 的方式有点特殊——不是 `OnceCallback<R, Args...>`，而是 `OnceCallback<R(Args...)>`。这种写法叫"签名式模板参数"（signature-style template parameter），`std::function` 和 `std::move_only_function` 也是这么做的。

背后的技巧是**模板偏特化**（template partial specialization）。我们先声明一个主模板，只有声明没有定义：

```cpp
template<typename FuncSignature>
class OnceCallback;  // 主模板：不提供实现
```

然后为 `FuncSignature` 恰好是函数类型的情形提供一个偏特化版本：

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这个偏特化里
};
```

当用户写 `OnceCallback<int(int, int)>` 时，编译器把 `int(int, int)` 当作一个整体类型匹配到主模板的 `FuncSignature`，然后发现偏特化版本能将这个整体拆解成返回类型 `ReturnType = int` 和参数包 `FuncArgs... = {int, int}`，于是选择偏特化版本。这个模式的好处是用户可以用一种非常自然的"函数签名"语法来指定回调的类型，而不需要分别传入返回值和参数列表。

这里有一个容易混淆的点：`R(Args...)` 看起来像函数声明，但在模板参数的上下文中，它是一个**函数类型**（function type）。`int(int, int)` 是一种合法的 C++ 类型——它描述的是"接受两个 int 参数、返回 int 的函数"。模板偏特化利用了这个类型，通过模式匹配把它拆开，提取出返回值类型和参数包。

### 内部存储：类的骨架长什么样

上一篇我们确定了三态架构。现在来看看类的骨架——先不管方法实现，只看数据成员和接口签名：

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 核心存储：持有实际的可调用对象
    // 不管你传入 lambda、函数指针还是仿函数，它都能装下
    std::move_only_function<FuncSig> func_;

    // 三态标记：kEmpty → kValid → kConsumed
    Status status_ = Status::kEmpty;

    // 取消令牌（可选）
    std::shared_ptr<CancelableToken> token_;

public:
    // 构造：接受任意可调用对象（带 requires 约束，后面解释）
    template<typename Functor>
        requires not_the_same_t<Functor, OnceCallback>
    explicit OnceCallback(Functor&& f);

    // Move-only：删除拷贝
    OnceCallback(const OnceCallback&) = delete;
    OnceCallback& operator=(const OnceCallback&) = delete;
    OnceCallback(OnceCallback&& other) noexcept;
    OnceCallback& operator=(OnceCallback&& other) noexcept;

    // 核心：执行回调并消费 *this（用 deducing this 实现，后面解释）
    template<typename Self>
    auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;

    // 查询接口
    [[nodiscard]] bool is_cancelled() const noexcept;
    [[nodiscard]] bool maybe_valid() const noexcept;
    [[nodiscard]] bool is_null() const noexcept;
    explicit operator bool() const noexcept;

    // 设置取消令牌
    void set_token(std::shared_ptr<CancelableToken> token);

    // 链式组合
    template<typename Next> auto then(Next&& next) &&;

private:
    ReturnType impl_run(FuncArgs... args);  // 真正的执行逻辑
};
```

骨架里的每一个成员都有明确的职责。`func_` 负责类型擦除——把各种不同形态的可调用对象统一成一个已知签名的调用接口。`status_` 是一个三态枚举，区分"从未赋值"（kEmpty）、"随时可调用"（kValid）和"已经调用过了"（kConsumed）。`token_` 是一个可选的取消令牌，用于在回调执行前检查是否应该取消执行。移动操作做指针级别的转移，源对象回到 kEmpty 状态。

接下来我们聚焦骨架里两个最精巧的部分：`run()` 的 deducing this 技巧和构造函数的 `requires` 约束。这两个是整个组件里模板技巧最密集的地方，值得单独拿出来讲透。

### deducing this：让编译器帮我们拦截错误调用

`run()` 是整个组件的灵魂，也是 C++23 特性最密集的一个方法。先看它的声明：

```cpp
template<typename Self>
auto run(this Self&& self, Args... args) -> R;
```

如果你没见过 `this Self&& self` 这种写法，别慌，我们一步步来。

#### 什么是 deducing this

deducing this 是 C++23 引入的特性，官方名称叫"显式对象参数"（explicit object parameter）。在传统的成员函数里，`this` 是隐式参数——编译器自动传入当前对象的地址，你看不见也摸不着。deducing this 让我们可以把 `this` 显式地写成函数的第一个参数，并且用模板参数来推导它的类型和值类别。

```cpp
// 传统写法：this 是隐式的
void run(FuncArgs... args);          // 编译器看到的是 run(OnceCallback* this, FuncArgs... args)

// deducing this 写法：this 是显式的
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;  // self 就是 this
```

关键在于 `Self&&`——它看起来像右值引用，但实际上是**转发引用**（forwarding reference），因为 `Self` 是模板参数。转发引用的特殊之处在于，它可以根据传入参数的值类别被推导为不同的类型：

- `cb.run(args)` — `cb` 是左值，`Self` 推导为 `OnceCallback&`（左值引用）
- `std::move(cb).run(args)` — `std::move(cb)` 是右值，`Self` 推导为 `OnceCallback`（纯右值）
- `std::as_const(cb).run(args)` — const 左值，`Self` 推导为 `const OnceCallback&`

#### 我们怎么利用它

知道了 `Self` 的推导规则，拦截左值调用就很简单了：

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

`std::is_lvalue_reference_v<Self>` 是一个编译期常量，检查 `Self` 是不是左值引用类型。当调用方写 `cb.run(args)` 时，`Self` 被推导为 `OnceCallback&`，这是一个左值引用，条件为 `true`，取反后 `static_assert` 失败，编译器直接报错——报错信息就是我们写的那句话。当调用方写 `std::move(cb).run(args)` 时，`Self` 被推导为 `OnceCallback`，不是引用，`static_assert` 通过，进入 `impl_run` 执行真正的逻辑。注意这里用的是 `std::forward<Self>(self)` 而不是 `self.run_impl()`，这确保了 `impl_run` 被正确地在右值上调用。

这里有一个值得玩味的细节：`static_assert` 的条件依赖模板参数 `Self`，所以它只有在模板实例化时才求值。这意味着如果 `run()` 从未被调用，`static_assert` 不会触发——不管传的是左值还是右值。只有在某个调用点上编译器需要实例化这个模板时，`Self` 的具体类型才会被确定，`static_assert` 才会求值。这叫"惰性实例化"（lazy instantiation），是模板元编程里非常常见的模式。

#### 跟 Chromium 的做法对比

Chromium 没有享受 C++23 的福利，它用的是两个重载：`Run() &&` 是真正的执行版本，`Run() const&` 里面放了一个 `static_assert(!sizeof(*this), "...")` 来产生编译错误。`!sizeof` 那个 hack 利用了 C++ 的一个性质：`sizeof` 必须在完整类型上才能求值，所以 `!sizeof(*this)` 求值时一定在类的定义内部（`*this` 的类型是完整的），表达式的值一定是 `false`。在 C++23 之前，直接写 `static_assert(false, "...")` 会在所有代码路径上触发（即使这个重载从未被调用），所以 Chromium 不得不用 `!sizeof` 的技巧。C++23 放宽了这个限制，但 Chromium 的代码库还没有全面迁移到 C++23，所以仍然保留着旧写法。

我们的 deducing this 方案只需要一个函数模板，通过 `Self` 的推导自然地区分左值和右值，比 Chromium 的两个重载 + `!sizeof` hack 干净得多。

### 构造函数的 requires 约束

构造函数模板上有一行看起来多余的约束：

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f);
```

为什么不直接 `template<typename Functor>` 就完事了？问题出在模板构造函数和移动构造函数之间的竞争。

当我们写 `OnceCallback cb2 = std::move(cb1)` 时，编译器面前有两条路：调用隐式声明的移动构造函数 `OnceCallback(OnceCallback&&)`，或者把模板构造函数实例化为 `OnceCallback(OnceCallback&&)`（令 `Functor = OnceCallback`）。直觉上我们会觉得移动构造函数是"更特殊"的匹配，应该优先选择。但 C++ 的重载决议规则不是这么运作的——在某些情况下，模板实例化出来的函数签名比隐式声明的特殊成员函数是"更精确"的匹配，编译器会毫不犹豫地选择模板版本。这可能导致意想不到的行为，比如模板构造函数可能不会正确地将源对象的状态设为 kEmpty。

我们的实现用了一个自定义 concept `not_the_same_t` 来解决这个问题：`!std::is_same_v<std::decay_t<F>, T>` 意味着"当 `F` 的退化类型恰好是 `T` 本身时，排除这个模板"。退化（decay）在这里的作用是去掉 `F` 上的引用和 cv 限定符——因为 `F` 可能是 `OnceCallback&&` 或 `const OnceCallback&`，退化后都变成 `OnceCallback`。加上约束后，当传入的是 `OnceCallback` 本身时模板被排除，编译器才会正确地匹配移动构造函数。

这个技巧在实现 move-only 的类型擦除包装器时非常常见——`std::move_only_function` 自己的实现里也有类似的约束。如果你以后写类似的组件，记住这个模式：**模板构造函数 + requires 排除自身类型 = 保护移动语义的正确匹配**。

### 消费语义的内部实现思路

`impl_run` 的实现逻辑很直观——检查状态、处理取消、调用可调用对象、更新状态。有几个细节值得提一下。

第一个是取消检查在执行前发生。`impl_run` 先检查令牌是否有效——如果已取消，直接消费回调但不执行，void 返回的情况直接 return，非 void 的情况抛出 `std::bad_function_call`。这个抛出异常的行为可能看起来有些激进，但它的理由很充分：调用方期望得到一个返回值，但我们无法提供一个有意义的值，所以抛异常是比返回未定义值更安全的做法。

第二个是 `if constexpr (std::is_void_v<ReturnType>)` 的分支。当返回类型是 `void` 时，我们不能写 `ReturnType result = func_(args...)`——void 不是一种可以赋值的类型。`if constexpr` 在编译期选择分支，void 的情况走"调用但不赋值"的路径，非 void 的情况走"调用并赋值给 result"的路径。这是 `if constexpr` 处理 void 返回类型的标准模式。

第三个是消费后置空。`impl_run` 先把 `func_` move 出来作为局部变量，然后将 `func_` 置为 `nullptr`、`status_` 设为 kConsumed，最后执行局部变量里的可调用对象。这个顺序很重要——先把可调用对象拿出去、状态标记好，再执行。这样即使可调用对象内部抛出异常，`status_` 也已经是 kConsumed 了，回调不会处于一个不一致的状态。置空这一步不仅仅是标记状态——它触发了 `std::move_only_function` 析构其内部持有的可调用对象，释放 lambda 捕获的资源（比如 `unique_ptr`）。

### 验证核心骨架

骨架写完之后，快速验证几个场景就够了：基本类型返回、void 返回、move-only 捕获、移动语义。如果这四个场景都通过——构造回调能拿到正确的返回值、void 回调能正常执行、捕获 `unique_ptr` 的回调用完之后资源被释放、移动后源对象变空、目标对象有效——骨架就没有问题。完整的测试用例我们在第三篇统一整理。

---

## 第二步：参数绑定 — `bind_once()`

### 我们要解决什么问题

`bind_once` 的场景很直观：你有一个三参数的函数 `f(int, int, int)`，但前两个参数在绑定时就能确定（比如 10 和 20），只有第三个参数要等到调用时才传入。你希望拿到一个只需传一个参数的 `OnceCallback<int(int)>`，调用时它自动把 10、20 和你传入的参数拼在一起喂给原函数。

这就是参数绑定——把"已知参数"提前塞进回调里，让调用方只需关心"未知参数"。Chromium 的 `BindOnce` 在这方面做了大量工作来处理参数的生命周期（`Unretained`、`Owned`、`Passed`、`WeakPtr` 等），我们的简化版只关注核心的参数绑定逻辑。

### `bind_once` 的实现骨架

```cpp
template<typename Signature, typename F, typename... BoundArgs>
auto bind_once(F&& funtor, BoundArgs&&... args) {
    return OnceCallback<Signature>(
        [f = std::forward<F>(funtor),
         ...bound = std::forward<BoundArgs>(args)]
        (auto&&... call_args) mutable -> decltype(auto) {
            return std::invoke(
                std::move(f),
                std::move(bound)...,
                std::forward<decltype(call_args)>(call_args)...
            );
        }
    );
}
```

这段代码不长，但里面有好几个值得展开讲的模板技巧。我们逐个拆。

### Lambda Capture Pack Expansion

`...bound = std::forward<BoundArgs>(args)` 这一行是 C++20 引入的 **lambda 初始化捕获包展开**语法。它是整个 `bind_once` 能够简洁实现的关键。

在 C++20 之前，可变参数模板的参数包（parameter pack）不能直接展开到 lambda 的捕获列表里——你没法写 "把 `args...` 的每一个元素分别捕获到 lambda 里" 这样的代码。变通方案是用一个 `std::tuple` 把所有绑定参数打包存起来，然后在 lambda 内部用 `std::apply` 展开成单独的参数再调用。这个方案能用，但代码会膨胀很多——你需要一个额外的 tuple、一个 `std::apply` 调用、以及处理 tuple 元素移动语义的模板辅助代码。

C++20 终于允许了包展开进 lambda 捕获。具体来说，`...bound = std::forward<BoundArgs>(args)` 的效果是为 `BoundArgs...` 中的每一个类型生成一个对应的捕获变量，每个变量用 `std::forward` 完美转发初始化。举个具体例子，假设 `BoundArgs...` 是 `int, std::string`，那么展开后等价于：

```cpp
[b1 = std::forward<int>(arg1), b2 = std::forward<std::string>(arg2)]
```

每个捕获变量在 lambda 内部都可以独立使用，而在我们的 `bind_once` 里，它们在 lambda 被调用时通过 `std::move(bound)...` 一起展开传给 `std::invoke`。注意这里用的是 `std::move` 而不是 `std::forward`——因为 lambda 是 `mutable` 的，捕获变量在 lambda 内部是左值，我们想把它们当作右值传出去以触发移动语义。

### `std::invoke` 的统一调用能力

lambda 内部用 `std::invoke` 而不是直接调用 `f(...)`，原因是 `std::invoke` 能统一处理各种可调用对象。普通函数指针直接调用没问题，但成员函数指针就不一样了——你没法写 `(&Class::method)(obj, args...)`，必须用 `(obj.*method)(args...)` 这种特殊语法。`std::invoke` 把这些差异全部封装了：`std::invoke(&Class::method, &obj, args...)` 等价于 `(obj.*method)(args...)`。

这意味着 `bind_once` 天然支持成员函数绑定，不需要额外的代码：

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

不过这里有一个**生命周期陷阱**需要注意：`&calc` 是裸指针，`bind_once` 不会管理它的生命周期。如果 `calc` 在回调被调用之前就被销毁了，`std::invoke` 会通过悬空指针访问已释放的内存。Chromium 用 `base::Unretained` 显式标记"我知道这个裸指针的生命周期是安全的"，用 `base::Owned` 接管所有权，用 `base::WeakPtr` 在对象析构时自动取消回调。我们的简化版里，这个安全责任暂时交给调用方。

### 签名推导：为什么需要显式指定 `Signature`

你可能注意到了 `bind_once` 的第一个模板参数 `Signature`（比如 `int(int)`）需要调用方显式指定。理想情况下，编译器应该能从 `F` 的可调用签名中自动推导出"去掉已绑定参数后的剩余签名"。但这件事在 C++ 里比想象中复杂得多。

对于函数指针 `R(*)(Args...)`，可以通过模板偏特化提取参数列表，然后用一种编译期的"类型列表切片"操作去掉前 N 个类型。对于有确定签名的仿函数（functor），也可以通过 `decltype(&T::operator())` 提取签名。但对于**泛型 lambda**（`[](auto x) { ... }`），它的 `operator()` 本身是模板，不存在唯一确定的签名——编译器根本无法在类型层面获取"这个 lambda 接受什么参数"的信息。

Chromium 为此写了一整套类型操作工具（`MakeUnboundRunType`、`DropTypeListItem` 等），大概有几百行模板元编程代码来处理各种边界情况。对于我们的教学目的，让调用方多写一个模板参数 `int(int)` 是更务实的选择——省去了大量复杂的模板元编程，代码清晰度也更好。

---

## 第三步：取消检查 — `is_cancelled()` 与 `maybe_valid()`

### 取消令牌的概念

回调在创建时可以关联一个"取消令牌"（cancellation token）。令牌代表某个外部对象的生命周期——当那个对象被销毁后，令牌失效，通过令牌关联的所有回调都变为"已取消"状态。

你可以把它想象成一张"通行证"：创建回调时发一张通行证给它，通行证上写着"有效"。某个时刻外部对象说"通行证作废了"（调用 `invalidate()`），之后所有持有这张通行证的回调在执行前检查时都会发现"通行证已经无效"，跳过执行。在 Chromium 里，这个通行证就是 `WeakPtr` 内部的控制块——`WeakPtr` 指向的对象被销毁后，控制块中的标志位被清除，所有绑定到这个 `WeakPtr` 的回调自动取消。

### `CancelableToken` 的设计思路

我们的简化版取消令牌只需要三个核心操作：创建（生成有效令牌）、失效（标记为作废）、检查（查询是否还有效）。内部用 `shared_ptr` 管理一个包含 `atomic<bool>` 的 `Flag` 结构体：

```cpp
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};  // 原子变量，多线程安全
    };
    // 所有 token 副本共享同一个 Flag
    std::shared_ptr<Flag> flag_;

public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const {
        return flag_->valid.load(std::memory_order_acquire);
    }
};
```

用 `shared_ptr` 而不是裸指针的原因是让令牌可以被拷贝和移动，同时所有副本共享同一个 `Flag`。`atomic<bool>` 保证多线程访问的安全性——一个线程可能在执行 `is_valid()` 的同时另一个线程在调 `invalidate()`，`memory_order_acquire/release` 语义保证前者的读一定能看到后者的写。

### 集成到 `OnceCallback`

取消令牌集成到 `OnceCallback` 的方式很直接：在数据成员里加一个可选的 `shared_ptr<CancelableToken>`，通过 `set_token()` 方法设置，然后在两个地方检查它——`is_cancelled()` 查询时和 `impl_run()` 执行前。

`is_cancelled()` 的逻辑是：状态不是 kValid 就返回 true（空回调和已消费回调都算"已取消"），如果有令牌且令牌失效也返回 true。`impl_run` 里在真正执行可调用对象之前先检查令牌状态——如果已取消，消费回调但不执行，直接返回（void 情况）或者抛出 `std::bad_function_call`（需要返回值的情况）。

`maybe_valid()` 暂时就是 `!is_cancelled()` 的简单包装。在 Chromium 的完整实现中，两者的区别在于线程安全保证的强弱——`is_cancelled()` 只能在回调绑定的序列（即创建回调的线程）上调用，保证返回确定性结果；`maybe_valid()` 可以从任何线程调用，但结果可能过时。我们的简化版暂时不区分这个语义，但保留了两个方法名以备后续在 `RepeatingCallback` 或跨线程场景中扩展。

---

## 第四步：链式组合 — `then()`

### `then()` 的语义

`then()` 允许我们把两个回调串联成一个管道。语义很直观：当管道被调用时，先用原始参数执行第一个回调，然后把返回值传给第二个回调。举个例子，回调 A 计算 `3 + 4 = 7`，回调 B 计算 `7 * 2 = 14`，用 `then()` 串联后，你得到一个新回调，调用它时自动走完 A → B 的整个流程。

听起来简单，但 `then()` 是四个功能里所有权设计最精巧的一个。

### 所有权是关键

串联后的新回调需要持有原回调和后续回调的**所有权**——否则原回调可能在外部被提前消费掉，管道就断了。而 `OnceCallback` 是 move-only 的，这意味着 `then()` 必须消费 `*this`（原回调）和 `next`（后续回调），把两者的所有权转移到一个新的 lambda 闭包里。整个所有权链条是这样的：

```text
新回调 → move_only_function → lambda 闭包 → [原回调 + 后续回调]
```

实现思路的骨架大概是这样：

```cpp
template<typename Next>
auto then(Next&& next) &&       // 末尾的 && 使其成为右值限定成员函数
    -> OnceCallback</* 返回类型和签名待推导 */>
{
    return OnceCallback</* ... */>(
        [self = std::move(*this),             // 把整个原回调移进 lambda
         cont = std::forward<Next>(next)]     // 把后续回调也移进来
        (FuncArgs... args) mutable -> decltype(auto) {
            if constexpr (std::is_void_v<ReturnType>) {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));     // void → 无参数传递
            } else {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));  // 传递中间结果
            }
        }
    );
}
```

注意这里和 Chromium 原版的一个重要区别：我们对后续回调使用 `std::invoke` 而不是 `.run()`。这是因为 `then()` 接受的 `next` 参数是一个普通可调用对象（比如 lambda），不是 `OnceCallback`——调用方不需要显式地写 `std::move(cont).run()`，`std::invoke` 直接调用就好。只有 `self`（原回调）才需要 `std::move(...).run()` 来表达消费语义。

### 几个容易踩坑的地方

**第一，`&&` 限定。** 函数声明末尾的 `&&` 使其成为右值限定的成员函数，只能通过 `std::move(cb).then(next)` 或者临时对象 `.then(next)` 调用。这是另一种表达"消费语义"的方式——和 `run()` 用 deducing this 不同，`then()` 直接用传统的 ref-qualifier。为什么不用 deducing this？因为 `then()` 不需要区分左值和右值给出不同的错误信息——它就是只接受右值，没有中间地带。

**第二，`self = std::move(*this)`。** 这一行把当前 `OnceCallback` 对象的**所有内容**移动到 lambda 的闭包对象里。移动之后，当前对象进入已消费状态（因为我们没有把它设为 kEmpty，而是让它自然地保持一个"被移走"的状态）。闭包对象又被存入返回的新 `OnceCallback` 的 `move_only_function` 里——`move_only_function` 的类型擦除能力保证了不管 lambda 的实际类型是什么，都能被统一存储。

**第三，`mutable` 关键字不可省略。** Lambda 默认生成的 `operator()` 是 `const` 的——这意味着 lambda 内部不能修改捕获的变量。但我们需要在 lambda 内部对 `self` 调用 `std::move(self).run()`，这个操作会修改对象状态（把 status 从 kValid 改为 kConsumed）。所以 lambda 必须声明为 `mutable`，让 `operator()` 变成非 const 的。

**第四，`if constexpr (std::is_void_v<ReturnType>)`。** 和 `impl_run` 里的情况一样——当原回调返回 `void` 时，`then()` 的语义是"先执行原回调，再执行后续回调（无参数传递）"。`if constexpr` 在编译期选择分支，两种情况生成完全不同的代码路径。

### 多级管道

`then()` 可以链式调用，形成多级管道：

```cpp
using namespace tamcpp::chrome;
auto pipeline = OnceCallback<int(int)>([](int x) {
    return x * 2;
}).then([](int x) {
    return x + 10;
}).then([](int x) {
    return std::to_string(x);
});

std::string result = std::move(pipeline).run(5);
// 5 * 2 = 10, 10 + 10 = 20, "20"
```

每次 `then()` 调用都会创建一个新的 `once_callback`，内部嵌套捕获了前一步的回调。从外到内的调用顺序是递归展开的：最外层回调被 `run()` → 执行其 lambda → lambda 内部对上一层调用 `std::move(self).run()` → 再对更上一层调用 → 直到底层。性能上，每一层 `then()` 增加一次 `std::move_only_function` 的间接调用，对于 2-3 级管道来说完全可接受。如果管道层级很深（超过 10 级），可以考虑用 `std::variant` 做一个扁平化的管道结构来避免嵌套闭包的开销——但这已经超出我们当前的讨论范围了。

---

## 小结

这一篇我们完成了 `OnceCallback` 四个核心功能的设计走读。和第一篇的接口设计不同，这里的重点是理解"为什么这样写"和"关键的模板技巧是什么"。几个核心知识点回顾一下：

- **模板偏特化** `OnceCallback<R(Args...)>` 让用户可以用自然的函数签名语法来指定回调类型，编译器通过模式匹配把函数类型拆解成返回值和参数包
- **Deducing this** 让 `run()` 通过一个函数模板实现编译期的左值/右值拦截，比 Chromium 的双重重载 + `!sizeof` hack 更干净
- **`requires` 约束**（通过 `not_the_same_t` concept）解决了模板构造函数与移动构造函数的匹配冲突，是 move-only 类型擦除包装器的标准防御手段
- **Lambda capture pack expansion** 是 `bind_once` 得以简洁实现的关键，C++20 之前需要用 tuple + apply 的变通方案
- **`then()` 的核心挑战**是所有权管理——它通过右值限定 + lambda 捕获 move 来保证管道中每个回调的所有权链完整，对后续回调使用 `std::invoke` 统一调用

下一篇我们会用系统化的测试用例来验证这些设计，并对比我们与 Chromium 原版在性能上的取舍。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [Chromium bind_internal.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
