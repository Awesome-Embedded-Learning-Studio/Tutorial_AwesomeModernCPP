---
chapter: 0
cpp_standard:
- 14
- 17
- 20
- 23
description: 深入讲解 mutable lambda、初始化捕获（init capture）、C++20 lambda capture pack expansion
  和泛型 lambda——OnceCallback 中 bind_once 与 then() 的核心实现技巧
difficulty: intermediate
order: 3
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
reading_time_minutes: 8
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（五）：then 链式组合
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: OnceCallback 前置知识（三）：Lambda 高级特性
---
# OnceCallback 前置知识（三）：Lambda 高级特性

上一篇速查里咱们快速过了一遍 lambda 的基础语法。这篇要钻进去。OnceCallback 的实现代码里真正顶事的几个 lambda 特性——`mutable`、初始化捕获、C++20 的 capture pack expansion——真不是什么锦上添花的语法糖。把这三个搞不明白,后面读 `bind_once` 和 `then()` 的代码,您大概率会一边读一边骂笔者写得晦涩。其实是笔者也绕不开它们,绕开了 OnceCallback 就做不出来。

咱们一个个拆。先从 `mutable` 说起——它为什么在这套实现里一刀都不能少。

## mutable lambda：为什么在 OnceCallback 里不能省

Lambda 默认生成的 `operator()` 是 `const` 的。换句话说,值捕获进来的变量,您在 lambda 体内只能看、不能摸。加 `mutable` 之后,`operator()` 变成非 const,捕获的副本就任您改了。

看个对照:

```cpp
int x = 10;

// const lambda：不能修改捕获的变量
auto f1 = [x]() {
    // x++;  // 编译错误：operator() 是 const 的
    return x;
};

// mutable lambda：可以修改捕获的变量
auto f2 = [x]() mutable {
    x++;       // OK：operator() 是非 const 的
    return x;
};

f2();  // 返回 11，x 的副本被修改
f2();  // 返回 12，同一个 lambda 对象再次调用，x 继续增加
```

这里有个容易看漏的细节——`mutable` lambda 的状态是**跨调用保持**的。`f2` 第一次返回 11,第二次返回 12。闭包对象持有捕获变量的副本,`mutable` 把这些副本的修改权交给了 `operator()`,改完就留在那儿,下次进来接着改。这一点 OnceCallback 正好用得上。

### 在 OnceCallback 里的角色

`bind_once` 和 `then()` 内部的 lambda,通通得标 `mutable`,没得商量。原因说白了就一句:这些 lambda 的捕获列表里塞了一个 `OnceCallback` 对象(走的是 `self = std::move(*this)`,下面马上讲),而您一旦调用 `std::move(self).run()`,就得改它的内部状态——把 `status_` 从 kValid 翻成 kConsumed。这玩意儿如果 lambda 是 const 的,`self` 在体内就是个 const 引用,您想在一个 const 对象上跑修改状态的操作?编译器第一个不答应。

```cpp
// then() 内部的 lambda——mutable 不可省略
[self = std::move(*this), cont = std::forward<Next>(next)]
(FuncArgs... args) mutable -> NextRet {
    // self 在这里需要被修改（run() 会消费它）
    auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
    return std::invoke(std::move(cont), std::move(mid));
}
```

---

## 初始化捕获（Init Capture）：把对象搬进 lambda

C++14 给了咱们一个新玩具——初始化捕获。语法长这样:`name = expression`。在捕获列表里当场跑一个表达式,拿结果去初始化一个新的捕获变量。听起来不起眼,但它解决了 C++11 lambda 最大的一个痛点。

### 和简单捕获的区别

简单捕获 `[x]` 只能抓已经存在的变量,而且要么拷贝、要么引用,二选一。初始化捕获 `[name = expr]` 多了一层,能干三件简单捕获完全做不到的事:

```cpp
auto ptr = std::make_unique<int>(42);

// 1. 移动捕获——把 unique_ptr 搬进 lambda
auto f1 = [p = std::move(ptr)]() { return *p; };
// ptr 在外面已经被搬空了

// 2. 存储计算结果
std::string s = "hello";
auto f2 = [len = s.size()]() { return len; };  // len 是 size_t 类型

// 3. 捕获不存在于外部的变量
auto f3 = [counter = 0]() mutable { return ++counter; };  // counter 是 lambda 自己的变量
```

第一条最要命。C++11 的 lambda 没有 move capture,您想往 lambda 里塞一个 `unique_ptr`,得绕一大圈——先存进 `std::function` 或者手工搓个函数对象。P0780 之前,Chromium 的 `base::Bind` 内部就是靠手动写 functor 来扛这个缺口的。初始化捕获一出,这些 hack 基本可以进博物馆了。

### 在 OnceCallback 中的使用

`then()` 的实现里,初始化捕获挑了两副担子。

第一副——把整个 OnceCallback 对象搬进 lambda:

```cpp
self = std::move(*this)
```

`*this` 是当前的 OnceCallback 对象。`std::move(*this)` 把它转成右值,初始化捕获 `self = std::move(*this)` 就地触发 OnceCallback 的移动构造,把 `func_`、`status_`、`token_` 一股脑搬进 lambda 的闭包对象。搬完之后,外面的 `*this` 就是个被掏空的壳——`func_` 空了,`token_` 是 null,跟"已死"差不多。这一步是 OnceCallback move-only 语义的核心动作,所有权就这么平移进了 lambda。

第二副——把后续回调搬进来:

```cpp
cont = std::forward<Next>(next)
```

`std::forward<Next>(next)` 原样保持 `next` 的值类别——传进来的是右值就移动,是左值就拷贝。`then()` 实际用的时候,接的多半是临时 lambda(右值),所以这里通常走移动。

### 所有权链

把这两步放一起看,`then()` 造出来的新 lambda,手里攥着原回调和后续回调的完整所有权。这个 lambda 又被塞进一个新的 `OnceCallback` 的 `std::move_only_function`。整条所有权链一层套一层:

```mermaid
graph LR
    A["新 OnceCallback"] --> B["move_only_function"] --> C["lambda 闭包"] --> D["原 OnceCallback + 后续回调"]
```

每一层都靠移动语义递所有权,不见共享,不见拷贝。OnceCallback 那套 move-only 规矩,在 `then()` 里就这么从外到内一路传到底,没有漏的地方。

---

## C++20 Lambda Capture Pack Expansion：bind_once 的简洁秘诀

这一篇里,真正让 `bind_once` 能用几行代码搞定就是它。C++20 之前,可变参数模板的参数包没法直接展开到 lambda 的捕获列表里——您得先用 `std::tuple` 把参数打包存起来,再在 lambda 内部拿 `std::apply` 展开调用。绕,但没别的办法。

### 旧方案（C++17）：tuple + apply

```cpp
template<typename F, typename... BoundArgs>
auto bind_old(F&& f, BoundArgs&&... args) {
    // 把所有绑定参数打包进 tuple
    return [f = std::forward<F>(f),
            tup = std::make_tuple(std::forward<BoundArgs>(args)...)]
        (auto&&... call_args) mutable -> decltype(auto) {
        // 用 std::apply 展开 tuple 并调用
        return std::apply([&](auto&... bound) -> decltype(auto) {
            return f(bound..., std::forward<decltype(call_args)>(call_args)...);
        }, tup);
    };
}
```

能工作,但代码臃肿得可以——中间塞个 tuple、外面套个 `std::apply`、里头还得再嵌一层 lambda 处理展开。三件套全上了。

### 新语法（C++20）：直接在捕获列表里展开包

C++20 终于松了口,允许在 lambda 的初始化捕获里做包展开。语法是 `...name = expression`,效果是给参数包里的每个类型单独生成一个捕获变量。

```cpp
template<typename F, typename... BoundArgs>
auto bind_new(F&& f, BoundArgs&&... args) {
    return [f = std::forward<F>(f),
            ...bound = std::forward<BoundArgs>(args)]  // ← 包展开！
        (auto&&... call_args) mutable -> decltype(auto) {
        return std::invoke(std::move(f),
                          std::move(bound)...,         // ← 展开捕获变量
                          std::forward<decltype(call_args)>(call_args)...);
    };
}
```

### 手动展开一个具体例子

咱们拿个具体的调用来看看编译器到底在背后做了什么。假设调用 `bind_new([](int a, std::string b, int c) { ... }, 10, std::string("hello"))`,这时 `BoundArgs = {int, std::string}`。编译器会把 `...bound = std::forward<BoundArgs>(args)` 这个包展开,摊成:

```cpp
[f = std::forward<F>(f),
 b1 = std::forward<int>(arg1),              // int 直接转发
 b2 = std::forward<std::string>(arg2)]      // std::string 移动转发
(auto&&... call_args) mutable -> decltype(auto) {
    return std::invoke(std::move(f),
                      std::move(b1), std::move(b2),    // 展开捕获变量
                      std::forward<decltype(call_args)>(call_args)...);
}
```

每个绑定参数摇身一变,成了 lambda 闭包里的一个独立成员变量。等 lambda 被调用,再通过 `std::move(bound)...` 一把全展开,递给 `std::invoke`。

### 为什么用 std::move 而不是 std::forward

这里有个坑,笔者第一次看差点没绕过来。lambda 内部用的是 `std::move(bound)...`,不是 `std::forward<BoundArgs>(bound)...`。为什么?

关键在于 lambda 是 `mutable` 的,捕获变量 `bound` 在 lambda 体内是个**左值**——具名变量永远是左值,这没跑。咱们希望绑定参数在回调触发时以右值的方式传出去(触发移动),那就得拿 `std::move` 把它转成右值。要是手滑写成 `std::forward<BoundArgs>(bound)`,因为 `bound` 已经是左值了,`std::forward` 压根不会动它的值类别——返回的还是左值引用,移动语义当场蒸发。OnceCallback 是 move-only 的,这一步丢移动就等于丢所有权,后面全乱套。

---

## 泛型 Lambda：auto&& 作为转发引用

最后说一个 `bind_once` 内部 lambda 的签名:`(auto&&... call_args)`。这套写法是用来接运行时传进来的参数的。这里头 `auto&&` 是转发引用——`auto` 在 lambda 参数里等同于模板参数,所以 `auto&&` 享受跟 `T&&`(T 是模板参数时)一模一样的推导规则。

```cpp
auto f = [](auto&& x) {
    // x 是转发引用
    // 传入左值：auto = int&, x 的类型是 int&（左值引用）
    // 传入右值：auto = int, x 的类型是 int&&（右值引用）
};

int v = 10;
f(v);       // x 绑定到左值
f(10);      // x 绑定到右值
```

`auto&&...` 这个组合,等于给 lambda 开了个口子,什么数量、什么类型的参数都能往里塞,同时每个参数的值类别(左值还是右值)它都记着。再配上 `std::forward<decltype(call_args)>(call_args)...`,这些参数就能被完美转发到最终的可调用对象那里,不丢一点信息。

---

下一篇咱们去看 Concepts 和 `requires` 约束——它们是保护 OnceCallback 的模板构造函数不被错误匹配的关键防御手段。

## 参考资源

- [cppreference: Lambda 表达式](https://en.cppreference.com/w/cpp/language/lambda)
- [P0780R2 - Pack Expansion in Lambda Init-Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
- [cppreference: std::forward](https://en.cppreference.com/w/cpp/utility/forward)
