---
chapter: 1
cpp_standard:
- 23
description: 从核心骨架到完整组件，四步走读 once_callback 的实现策略，重点理解模板技巧和所有权设计
difficulty: advanced
order: 2
platform: host
prerequisites:
- once_callback 设计指南（一）：动机与接口设计
reading_time_minutes: 24
related:
- bind_once / bind_repeating 与参数绑定
- 回调取消与组合模式
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: once_callback 设计指南（二）：逐步实现
---
# once_callback 设计指南（二）：逐步实现

上一篇咱们把 `OnceCallback` 的目标 API 和内部架构定下来了。这一篇该撸代码了。不过笔者先把丑话说在前头:这篇不打算把完整头文件端上来——那玩意儿贴出来几百行,您盯着看容易走神。咱们只挑骨架和真正费脑子的模板技巧过一遍,把"为什么这么写"想透;完整的、能编译的代码留给课后练习和第三篇测试篇。

实现拆四步,层层往上叠:先把 `run()` 语义这条命脉打通,再加 `bind_once()` 参数绑定,接着挂上取消检查,最后接 `then()` 链式组合。每一步只盯着两个问题——这玩意儿长什么样、关键的模板技巧在哪儿。

---

## 第一步:核心骨架 — 从模板偏特化起手

### 为什么是 `OnceCallback<R(Args...)>` 这种写法

您要是扫过一眼标准库,会发现 `std::function`、`std::move_only_function` 都长一个样——模板参数不是把返回值和参数列表分开写,而是塞一整个函数签名进去。咱们 `OnceCallback` 也跟着这么写,图的就是这套"签名式模板参数"的清爽。

底下干活的招是模板偏特化。咱们先甩个光秃秃的主模板,光声明、不定义:

```cpp
template<typename FuncSignature>
class OnceCallback;  // 主模板：不提供实现
```

然后单开一个偏特化版本,专门接住"签名恰好是函数类型"这种情形:

```cpp
template<typename ReturnType, typename... FuncArgs>
class OnceCallback<ReturnType(FuncArgs...)> {
    // 所有真正的代码都在这个偏特化里
};
```

您写 `OnceCallback<int(int, int)>` 的时候,编译器先把 `int(int, int)` 当一整个类型塞进主模板的 `FuncSignature`,接着发现偏特化能把这个整体拆开——`ReturnType = int`、`FuncArgs... = {int, int}`,于是偏特化中选。这套写法的好处明摆着:用户拿自然的"函数签名"语法就能指定回调类型,不用把返回值和参数列表拆成两份模板参数传进来。

这里有个小坑容易绊人:`R(Args...)` 拼法看着像函数声明,但在模板参数位置上它其实是一个函数类型(function type)。`int(int, int)` 在 C++ 里就是合法的类型,描述"吃两个 int、吐一个 int 的函数"。偏特化就是搭了这个便车,靠模式匹配把它拆包。

### 内部存储:类的骨架长什么样

上一篇咱们定下了三态架构。现在把类的骨架立起来,先别管方法实现,光看数据成员和接口签名长什么样:

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

每个成员都有明确分工。`func_` 干的是类型擦除的脏活,不管您喂的是 lambda、函数指针还是仿函数,统统收编成一个已知签名的调用口子。`status_` 是个三态枚举,把"从未赋值"(kEmpty)、"随时能跑"(kValid)、"已经跑过了"(kConsumed)三个阶段分开。`token_` 是个可选的取消令牌,回调真跑之前替您把把门。移动操作走的是指针级转移,源对象挪完回到 kEmpty。

骨架立起来后,有两个地方模板技巧密度最高——`run()` 的 deducing this 和构造函数的 `requires` 约束。这两块单独拎出来讲透,剩下的就好读了。

### deducing this:让编译器替咱们挡掉错误的调用

`run()` 是整个组件的灵魂,也是 C++23 特性最密集的一个方法。先盯它的声明:

```cpp
template<typename Self>
auto run(this Self&& self, Args... args) -> R;
```

那个 `this Self&& self` 笔者第一次见的时候愣了一下,后来才搞明白这是 C++23 的 deducing this,官方叫"显式对象参数"(explicit object parameter)。传统成员函数里 `this` 是隐式的——编译器悄悄把当前对象地址塞进来,您看不见摸不着。deducing this 干的事就是把 `this` 显式写成函数的第一个参数,再用模板参数推导它的类型和值类别。

```cpp
// 传统写法：this 是隐式的
void run(FuncArgs... args);          // 编译器看到的是 run(OnceCallback* this, FuncArgs... args)

// deducing this 写法：this 是显式的
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType;  // self 就是 this
```

门道在 `Self&&` 上,看着是右值引用,其实因为 `Self` 是模板参数,它退化成转发引用(forwarding reference)。转发引用的妙处是会按实参的值类别变脸:`cb.run(args)` 这种左值调用,`Self` 被推导成 `OnceCallback&`;写成 `std::move(cb).run(args)`,`Self` 就成了纯右值 `OnceCallback`;const 左值 `std::as_const(cb).run(args)` 则是 `const OnceCallback&`。三种值类别,一个模板全接住。

#### 咱们怎么拿它干活

知道推导规则,拦截左值调用就一句话的事:

```cpp
template<typename Self>
auto run(this Self&& self, FuncArgs&&... args) -> ReturnType {
    static_assert(!std::is_lvalue_reference_v<Self>,
        "OnceCallback::run() must be called on an rvalue. "
        "Use std::move(cb).run(...) instead.");
    return std::forward<Self>(self).impl_run(std::forward<FuncArgs>(args)...);
}
```

`std::is_lvalue_reference_v<Self>` 是个编译期常量,专查 `Self` 是不是左值引用。调用方写 `cb.run(args)`,`Self` 推成 `OnceCallback&`,是左值引用,条件取反触发 `static_assert`,编译器当场报错,报的就是咱们写的那句"必须用 std::move"。写成 `std::move(cb).run(args)`,`Self` 是纯右值类型,`static_assert` 放行,接着 `std::forward<Self>(self).impl_run(...)` 把活儿派给真正的实现。这里特意用 `std::forward<Self>(self)` 而不是直接 `self.impl_run(...)`,是为了保证 `impl_run` 是在右值上调用的,值类别不能在这一步丢掉。

有个细节笔者觉得值得玩味:`static_assert` 的条件挂在模板参数 `Self` 上,所以它只在模板实例化那一刻才求值。换句话说,只要没人调用 `run()`,这条 assert 就不触发,管您传的是左值还是右值。只有真有人在某处写了个调用点,编译器要实例化这个模板了,`Self` 的具体类型才落定,assert 才求值。这套机制叫惰性实例化(lazy instantiation),是模板元编程里的家常便饭。

#### 跟 Chromium 的做法对比

Chromium 享受不到 C++23 的福利,它走的是两个重载的老路:`Run() &&` 是真正执行的版本,`Run() const&` 里头塞个 `static_assert(!sizeof(*this), "...")` 故意制造编译错误。那个 `!sizeof` hack 利用了 C++ 的一条性质——`sizeof` 只能在完整类型上求值,所以 `!sizeof(*this)` 一旦求值,就说明此刻在类定义内部(`*this` 是完整类型),值必然是 `false`。C++23 之前直接写 `static_assert(false, "...")` 会在所有代码路径上触发,哪怕这个重载从没被调用过,所以 Chromium 只能拿 `!sizeof` 这种绕弯子的写法。C++23 把这条限制松开了,但 Chromium 的代码库还没全量迁 C++23,旧写法就这么留着了。

咱们这套 deducing this 方案,一个函数模板就靠 `Self` 的推导把左值右值分得清清楚楚,比 Chromium 那两个重载加 `!sizeof` hack 干净一大截。这是踩在新标准肩膀上得来的便宜,笔者得说一句公道话。

### 构造函数的 requires 约束

构造函数模板上有行约束,乍一看像多余的:

```cpp
template<typename Functor>
    requires not_the_same_t<Functor, OnceCallback>
explicit OnceCallback(Functor&& f);
```

直接 `template<typename Functor>` 不就完了?不行,问题出在模板构造函数跟移动构造函数抢活儿上。

咱们写 `OnceCallback cb2 = std::move(cb1)` 的时候,编译器面前摆着两条路:一条是走隐式声明的移动构造 `OnceCallback(OnceCallback&&)`,另一条是把模板构造实例化成 `OnceCallback(OnceCallback&&)`(令 `Functor = OnceCallback`)。直觉上咱们会觉得移动构造"更特殊",理应优先。但 C++ 的重载决议不按直觉来——有些情况下,模板实例化出来的函数签名比隐式声明的特殊成员函数匹配得还更"精确",编译器二话不说就选了模板版本。这一选就出事:模板构造大概率不会老老实实把源对象状态置回 kEmpty。

咱们的实现用一个自定义 concept `not_the_same_t` 把这事儿摁住了:它本质上是 `!std::is_same_v<std::decay_t<F>, T>`,意思是"`F` 退化之后恰好等于 `T` 本身时,把这个模板排除掉"。退化(decay)在这儿的作用是剥掉 `F` 身上的引用和 cv 限定——`F` 可能是 `OnceCallback&&`、也可能是 `const OnceCallback&`,退化后统统变回 `OnceCallback`。挂上约束之后,只要传进来的是 `OnceCallback` 自己,模板直接出局,编译器才会乖乖去匹配移动构造。

这个套路在写 move-only 的类型擦除包装器时太常见了,`std::move_only_function` 自己的实现里就挂着类似的约束。您以后要是也撸这种组件,把这个模式记牢:模板构造加 requires 排除自身类型,就是给移动语义的正确匹配兜底。

### 消费语义的内部实现思路

`impl_run` 的主干逻辑一眼就懂:查状态、查取消、跑可调用对象、改状态。但有几个细节,笔者踩过之后才意识到里头的讲究。

头一个,取消检查得赶在执行之前。`impl_run` 先看令牌还有没有效——要是已经取消,直接消费回调但不执行,返回类型是 void 就 return,非 void 就抛 `std::bad_function_call`。这里抛异常乍看有点猛,但理由其实很硬:调用方眼巴巴等一个返回值,咱们没法凭空变出一个有意义的值给他,抛异常比返回一个未定义值体面得多。

第二个细节是 `if constexpr (std::is_void_v<ReturnType>)` 这条分支。返回类型是 void 的时候,`ReturnType result = func_(args...)` 这种写法编译都过不去——void 压根不是能赋值的类型。`if constexpr` 在编译期挑分支,void 的情况走"调用但不赋值",非 void 走"调用并赋给 result"。这是 `if constexpr` 对付 void 返回的标准套路。

第三个是消费后置空这个动作的顺序,笔者一开始没在意,后来差点被坑。`impl_run` 得先把 `func_` move 到一个局部变量里,再把 `func_` 置 `nullptr`、`status_` 设成 kConsumed,最后才执行局部变量里那个可调用对象。这个顺序断不能颠倒——先把对象挪出来、状态标好,再开跑。这么一来,就算可调用对象内部抛了异常,`status_` 也已经稳稳是 kConsumed,回调不至于卡在一个不上不下的脏状态。置空这一步还不光是改状态——它会触发 `std::move_only_function` 析构它内部持有的可调用对象,顺手把 lambda 捕获的资源(比如 `unique_ptr`)释放掉。

### 验证核心骨架

骨架写完,挑四个场景跑一遍就够:基本类型返回、void 返回、move-only 捕获、移动语义。这四样都过——构造回调拿到正确的返回值、void 回调正常执行、捕获 `unique_ptr` 的回调跑完资源被释放、移动后源对象变空目标对象有效——骨架就算立住了。完整的测试用例留到第三篇统一收拾。

---

## 第二步:参数绑定 — `bind_once()`

### 咱们要解决什么问题

`bind_once` 的场景一句话能说清:手上有个三参数函数 `f(int, int, int)`,前两个参数绑定时就知道(比如 10 和 20),只有第三个得等到调用那一刻才传。咱们想要的就是一个只吃一个参数的 `OnceCallback<int(int)>`,跑的时候它自动把 10、20 和您传进来的参数凑齐了喂给原函数。

这就是参数绑定——把"已知参数"提前塞进回调,让调用方只操心"未知参数"。Chromium 的 `BindOnce` 在这块下了大功夫处理参数生命周期(`Unretained`、`Owned`、`Passed`、`WeakPtr` 一堆帮手),咱们的简化版只管核心的绑定逻辑。

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

这段代码不长,里头却藏着好几个能单独拎出来讲的模板技巧。咱们一个一个拆。

### Lambda Capture Pack Expansion

`...bound = std::forward<BoundArgs>(args)` 这一行是 C++20 才放出来的 lambda 初始化捕获包展开语法。整个 `bind_once` 能写得这么利索,全靠它。

C++20 之前,可变参数模板的参数包(parameter pack)没法直接展开进 lambda 的捕获列表——您写不出"把 `args...` 每个元素各自捕获进 lambda"这种代码。变通的土办法是拿 `std::tuple` 把绑定参数全打包,lambda 内部再 `std::apply` 拆开调用。能用是能用,但代码会膨胀一大截——额外一个 tuple、一次 `std::apply`、再加上处理 tuple 元素移动语义的模板帮手代码。

C++20 终于松口了。`...bound = std::forward<BoundArgs>(args)` 的效果是,为 `BoundArgs...` 里每个类型生成一个对应的捕获变量,各自用 `std::forward` 完美转发初始化。举个具体的例子,假设 `BoundArgs...` 是 `int, std::string`,展开后等价于:

```cpp
[b1 = std::forward<int>(arg1), b2 = std::forward<std::string>(arg2)]
```

每个捕获变量在 lambda 内部都能独立用,在咱们的 `bind_once` 里,它们在 lambda 被调用的那一刻通过 `std::move(bound)...` 一块儿展开喂给 `std::invoke`。这里头有个坑笔者得提醒一句:用的是 `std::move` 不是 `std::forward`——因为 lambda 标了 `mutable`,捕获变量在 lambda 内部是左值,咱们要把它们当右值送出去,才能触发移动语义。

### `std::invoke` 的统一调用能力

lambda 内部用的是 `std::invoke`,不是直接 `f(...)`。原因是 `std::invoke` 能把各种可调用对象的差异抹平。普通函数指针直接调没问题,成员函数指针就两码事了——您写不出 `(&Class::method)(obj, args...)`,必须改用 `(obj.*method)(args...)` 这种专门语法。`std::invoke` 把这些花样全收编了:`std::invoke(&Class::method, &obj, args...)` 就等价于 `(obj.*method)(args...)`。

这么一来,`bind_once` 天然就支持成员函数绑定,一行额外代码都不用写:

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

不过这儿埋着个生命周期陷阱,笔者非得提醒您一句不可:`&calc` 是裸指针,`bind_once` 压根不管它的死活。要是 `calc` 在回调真跑之前就先一步析构了,`std::invoke` 就会顺着悬空指针摸到已经释放的内存上,典型的 use-after-free。Chromium 这块配套了一整套帮手——`base::Unretained` 显式声明"这个裸指针的生命周期我心里有数",`base::Owned` 把所有权接管过来,`base::WeakPtr` 在对象析构时顺手把回调作废。咱们的简化版里,这份安全责任暂时压在调用方肩上。

### 签名推导:为什么非得显式指定 `Signature`

您大概注意到了,`bind_once` 头一个模板参数 `Signature`(比如 `int(int)`),得调用方自己写明。理想情况下,编译器该能从 `F` 的可调用签名里自动推出"去掉已绑定参数之后的剩余签名"才对。但这事在 C++ 里比想象中难搞得多。

函数指针 `R(*)(Args...)` 这种好办,模板偏特化把参数列表提出来,再做一次编译期的"类型列表切片"砍掉前 N 个类型就行。有确定签名的仿函数(functor)也凑合,`decltype(&T::operator())` 能把签名挖出来。但碰到泛型 lambda(`[](auto x) { ... }`)就歇菜了——它的 `operator()` 本身就是模板,压根不存在唯一确定的签名,编译器没法在类型层面问出"这个 lambda 吃什么参数"这种问题。

Chromium 为了这事儿写了一整套类型操作工具(`MakeUnboundRunType`、`DropTypeListItem` 之类的),前前后后几百行模板元编程来应付各种边界情况。咱们教学目的嘛,让调用方多写一个模板参数 `int(int)` 反而更务实——大段复杂的元编程全省了,代码清清爽爽。

---

## 第三步:取消检查 — `is_cancelled()` 与 `maybe_valid()`

### 取消令牌是干嘛的

回调创建的时候可以挂上一个"取消令牌"(cancellation token)。令牌背后代表的是某个外部对象的生死——那个对象一旦没了,令牌就跟着失效,所有靠这张令牌关联起来的回调统统进入"已取消"状态。

您就把它当成一张通行证:回调出生的时候发一张,上面盖着"有效"。哪天外部对象说"通行证作废"(调一下 `invalidate()`),之后所有捏着这张通行证的回调,执行前查一眼都会发现"通行证已经盖戳了",自个儿跳过去不跑了。在 Chromium 里,这张通行证就是 `WeakPtr` 内部的控制块——`WeakPtr` 指着的对象一析构,控制块里那个标志位就被翻掉,绑在这枚 `WeakPtr` 上的回调自动作废。

### `CancelableToken` 的设计思路

咱们这版简化令牌,核心就三个动作:创建(发一张有效的)、失效(盖戳作废)、检查(查还有没有效)。内部拿 `shared_ptr` 管着一个装着 `atomic<bool>` 的 `Flag` 结构体:

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

为啥用 `shared_ptr` 不用裸指针?图的是令牌能被拷来移去,而所有副本始终共享同一枚 `Flag`。`atomic<bool>` 保的是多线程下的安全——一个线程这边正查着 `is_valid()`,另一个线程那边已经把 `invalidate()` 调下去了,`memory_order_acquire/release` 这一对语义恰好把两边对齐:前者的读一定能看到后者的写。

### 塞进 `OnceCallback`

令牌塞进 `OnceCallback` 的方式很直白:数据成员里挂一个可选的 `shared_ptr<CancelableToken>`,通过 `set_token()` 设进来,然后两处查它,一处是 `is_cancelled()` 查询时,一处是 `impl_run()` 真跑之前。

`is_cancelled()` 的逻辑一句话:状态只要不是 kValid 就返回 true(空回调和已消费回调都算"已取消"),另外要是有令牌且令牌已失效,也返回 true。`impl_run` 这边呢,真要动手执行可调用对象之前先扫一眼令牌,要是已取消,消费回调但不执行,void 情况直接返回,需要返回值的情况抛 `std::bad_function_call`。

`maybe_valid()` 眼下就是 `!is_cancelled()` 套了个壳。Chromium 的完整实现里,这俩的差别在线程安全保证的强弱上——`is_cancelled()` 只能在回调绑定的序列(也就是创建回调那条线)上调用,返回的是确定性结果;`maybe_valid()` 哪条线都能调,但结果可能已经过时。咱们简化版暂时不抠这层语义,但两个方法名都留着,以后在 `RepeatingCallback` 或者跨线程场景里要用。

---

## 第四步:链式组合 — `then()`

### `then()` 到底干什么

`then()` 把两个回调串成一根管子。语义一句话:管子被调用时,先用原始参数跑头一个回调,把它吐的返回值递给第二个回调接着跑。举个例子,回调 A 算 `3 + 4 = 7`,回调 B 算 `7 * 2 = 14`,拿 `then()` 一串,您得到的就是一个新回调,跑它的时候自动把 A → B 整条流程走完。

听着简单,但 `then()` 是四个功能里所有权设计最费心思的一个。

### 所有权是命门

串起来的新回调,得把原回调和后续回调的所有权都攥在手里——不然原回调哪天在外头被人提前消费掉,管子当场就断了。偏生 `OnceCallback` 又是 move-only 的,这意味着 `then()` 非得消费 `*this`(原回调)和 `next`(后续回调)不可,把两者的所有权一并挪进一个新的 lambda 闭包里。整条所有权链长这样:

```mermaid
graph LR
    A["新回调"] --> B["move_only_function"] --> C["lambda 闭包"] --> D["原回调 + 后续回调"]
```

实现骨架大概长这样:

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

这里头有个跟 Chromium 原版的差别笔者得点一句:咱们对后续回调用的是 `std::invoke`,不是 `.run()`。原因是 `then()` 收的 `next` 参数是个普通可调用对象(比如 lambda),不是 `OnceCallback`——调用方没必要费劲写 `std::move(cont).run()`,`std::invoke` 一把梭就行。只有 `self`(原回调)那一步,才需要 `std::move(...).run()` 来表达消费语义。

### 几个笔者替您踩过的坑

`then()` 这块有几个地方笔者栽过跟头,挨个讲。

先说末尾那个 `&&`。它把成员函数限定成右值版本,只能通过 `std::move(cb).then(next)` 或者临时对象 `.then(next)` 来调。这是另一种表达"消费语义"的路子,跟 `run()` 用 deducing this 不一样,`then()` 直接走传统的 ref-qualifier。为啥不也用 deducing this?因为 `then()` 用不着在左值右值之间给出不同的报错信息——它就只吃右值,没中间地带,ref-qualifier 已经够干净了。

接着是 `self = std::move(*this)` 这一行。它把当前 `OnceCallback` 对象的所有家当,一股脑儿挪进 lambda 的闭包对象里。挪完之后,当前对象就进了已消费态(咱们没把它设回 kEmpty,让它自然地维持"被搬空"的样子)。这个闭包对象接着又被塞进返回的新 `OnceCallback` 的 `move_only_function` 里——类型擦除的本事保证了不管 lambda 实际是什么类型,都能被统一存进去。

然后是 `mutable`,这个关键字一个字都不能省。Lambda 默认生成的 `operator()` 是 `const` 的,意味着 lambda 内部不许动捕获的变量。可咱们偏偏要在 lambda 内部对 `self` 调 `std::move(self).run()`,这一步要改对象状态(把 status 从 kValid 拨到 kConsumed)。所以 lambda 必须标 `mutable`,让 `operator()` 变成非 const 的。

最后是老朋友 `if constexpr (std::is_void_v<ReturnType>)`。跟 `impl_run` 里那处是同一回事,原回调返回 `void` 的时候,`then()` 的语义是"先跑原回调,再跑后续回调,中间不传东西"。`if constexpr` 编译期挑分支,两种情形生成两条完全不同的代码路径。

### 多级管道

`then()` 能一路链下去,拼成多级管子:

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

每调一次 `then()` 都会生出一只新的 `OnceCallback`,里头嵌套着捕获前一步的回调。从外往里的调用顺序是递归铺开的:最外层被 `run()` → 跑它的 lambda → lambda 内部对上一层调 `std::move(self).run()` → 再对更上一层调 → 一路到底。性能这边,每多一层 `then()` 就多一次 `std::move_only_function` 的间接调用,对 2-3 级的管子来说完全扛得住。真要是管子层数深到 10 级以上,可以考虑拿 `std::variant` 做个扁平化的管道结构,躲开嵌套闭包那点开销——不过这就超出咱们当下的范围了。

下一篇咱们上系统化的测试用例,逐条验这些设计,顺便对比一下咱们跟 Chromium 原版在性能上的取舍。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [Chromium bind_internal.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0847R7 - Deducing this 提案](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0847r7.html)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
