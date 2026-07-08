---
chapter: 1
cpp_standard:
- 23
description: 逐行拆解 bind_once 的参数绑定实现——从动机到 lambda 捕获包展开，再到手动展开一个完整的模板实例化例子
difficulty: beginner
order: 3
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（二）：std::invoke 与统一调用协议
- OnceCallback 前置知识（三）：Lambda 高级特性
reading_time_minutes: 7
related:
- OnceCallback 实战（四）：取消令牌设计
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: OnceCallback 实战（三）：bind_once 实现
---
# OnceCallback 实战（三）：bind_once 实现

骨架搭好了，`run()` 也能消费回调了。可笔者写着写着就撞上一种很常见的别扭:每次构造 OnceCallback 都得塞一个签名完整的可调用对象进去,参数全得在调用那一刻才给齐。现实里哪有这么齐整——十有八九是几个参数在创建回调时就捏死了,只有剩下那一两个得留到调用现场。

`bind_once` 就是干这事的。它把"已经定下来的参数"提前塞进回调里,调用方只管剩下的那几个。咱们这一篇把它的实现逐行抠一遍,再手动展开一个完整的模板实例化,让您看清编译器在背后到底做了哪些动作。

先看没有 `bind_once` 的样子。假设有个三参函数,前两个参数绑定时就能定:

```cpp
int compute(int x, int y, int z) {
    return x + y + z;
}

// 没有 bind_once：每次调用都得传三个参数
auto cb = OnceCallback<int(int, int, int)>(compute);
int r = std::move(cb).run(10, 20, 30);  // r == 60
```

如果 `x = 10` 和 `y = 20` 在绑定时就确定了,只有 `z` 要留到调用时传入,咱们真正想要的是个只收一个参数的 `OnceCallback<int(int)>`。

不用 `bind_once`，您只能手写一个 lambda 包一层：

```cpp
auto wrapped = OnceCallback<int(int)>(
    [](int z) { return compute(10, 20, z); }
);
int r = std::move(wrapped).run(30);  // r == 60
```

能跑。可参数一多、类型一复杂(比如绑的是 move-only 的 `unique_ptr`),手写 lambda 就开始烦人了。`bind_once` 干的就是把这个"手写 lambda 包一层"的过程自动化掉。

```cpp
auto bound = bind_once<int(int)>(compute, 10, 20);
int r = std::move(bound).run(30);  // r == 60
```

## bind_once 的完整实现逐行拆解

先把源码整个摆出来,咱们对着它一段段啃。

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

## 从模板参数一路看到 lambda 体

先看模板参数,这是入口。`bind_once` 顶上挂了三个:`Signature` 是目标回调的签名(比如 `int(int)`),必须您手动写,编译器推不出来;`F` 是那个可调用对象的类型(lambda 闭包、函数指针之类),由第一个实参推;`BoundArgs...` 是绑定参数的类型包,跟着后面的实参走。后两个是 CTAD 干的活,只有第一个得您亲自动手。

接下来是捕获列表,这块是整套实现里最精巧的。`f = std::forward<F>(funtor)` 用 init capture 把可调用对象完美转发进闭包:传进来的是右值就移进来,是左值就拷进来,值类别一路保到底。下一行 `...bound = std::forward<BoundArgs>(args)` 是 C++20 才有的 lambda init capture pack expansion,给 `BoundArgs...` 里每个类型都发一个捕获变量,各自走 `std::forward` 初始化。要是 `BoundArgs = {int, std::string}`,展开完等价于:

```cpp
[f = std::forward<F>(funtor),
 b1 = std::forward<int>(arg1),
 b2 = std::forward<std::string>(arg2)]
```

参数列表 `(auto&&... call_args)` 接的是运行时才传进来的那些。`auto&&` 在这儿等同于模板参数的 `T&&`,是转发引用,不是右值引用,新手很容易看走眼。

`mutable` 这关键字千万别省。lambda 体里要调 `std::move(f)` 和 `std::move(bound)...`,这俩操作都得改捕获变量。lambda 不写 `mutable` 就是 const 的,捕获变量在里头也跟着 const——您没法从 const 对象上 move,编译器当场给您撅回来。

最后一层是 lambda 体:

```cpp
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

`std::invoke` 在前置知识(二)讲过了,它统一兜住所有形态的可调用对象,成员函数指针也不例外。`std::move(f)` 和 `std::move(bound)...` 把捕获进来的东西按右值甩出去——因为 `mutable` lambda 里的捕获变量本身是左值,想以右值出栈就得靠 `std::move` 显式打一下;`call_args...` 那行则是把运行时参数按原样完美转发。

这里有个顺序得盯一眼:绑定参数在前,运行时参数在后。不是随手排的,它直接决定了哪些参数被"预绑定"、哪些留到调用那一刻。排反了,签名和实参就对不上。

## 手动展开一个具体例子

光看源码还是隔着一层。咱们拿一个具体调用,把模板实例化后的样子手动铺开,看看编译器到底生成了什么。假设:

```cpp
struct Calc {
    int multiply(int a, int b) { return a * b; }
};

Calc calc;
auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
int r = std::move(bound).run(8);  // r == 40
```

## 一步步把模板铺开

先推参数。`Signature = int(int)` 是您写的,没得商量;`F = int (Calc::*)(int, int)`——成员函数指针类型,编译器从 `&Calc::multiply` 推出来的;`BoundArgs = {Calc*, int}`,一个对象指针加头一个参数。

捕获列表展开长这样:

```cpp
[f = std::forward<int (Calc::*)(int, int)>(&Calc::multiply),
 b1 = std::forward<Calc*>(&calc),
 b2 = std::forward<int>(5)]
```

`f` 咬住成员函数指针,`b1` 咬住对象指针,`b2` 咬住那个绑定的整数 5。

接着看 `bound.run(8)` 真正被调用时发生了什么。这一刻 `call_args = {8}`,lambda 体里的 `std::invoke` 收到的实参是:

```cpp
std::invoke(std::move(f), std::move(b1), std::move(b2), 8)
```

也就是:

```cpp
std::invoke(&Calc::multiply, &calc, 5, 8)
```

`std::invoke` 一看头一个参数是成员函数指针,第二个是指向对象的指针,自动按成员调用规则展开:

```cpp
((*(&calc)).*(&Calc::multiply))(5, 8)
```

等价于 `calc.multiply(5, 8)`,结果 `40`。整个魔法其实就是 `std::invoke` 对成员函数指针那条重载在做兜底。

## 这里有个生命周期坑

`b1 = std::forward<Calc*>(&calc)` 捕的是个裸指针 `&calc`。`bind_once` 压根不替您管 `calc` 的死活。要是 `calc` 在回调跑之前先被销毁了,lambda 里头那个就是一根悬空指针,`std::invoke` 顺着它去摸已经释放的内存——未定义行为,典型的 use-after-free。

Chromium 在这块下了三套补丁:用 `base::Unretained` 显式把"我担保它活着"标出来,用 `base::Owned` 直接把所有权接管掉,用 `base::WeakPtr` 让回调在对象析构那一刻自动作废。咱们这个简化版暂时图省事,把担子甩给调用方——真要上生产,这三套总得补一套。

## 为什么签名必须显式指定

您大概注意到了,`bind_once<int(int)>(...)` 里那个 `int(int)` 必须亲手写上去。理想情况下,编译器该能从可调用对象的签名和绑定参数的个数,自动把剩余签名推出来。但这事在 C++ 里比想象中难缠得多。

函数指针 `R(*)(Args...)` 还算好办,靠模板偏特化能把参数列表抠出来,再在编译期做"类型列表切片"砍掉前 N 个就行;有固定签名的仿函数也行,`decltype(&T::operator())` 一发入魂。真正的硬骨头是泛型 lambda(`[](auto x) { ... }`),它的 `operator()` 本身就是个模板,压根不存在唯一确定的签名,编译器在类型层面拿不到"这个 lambda 到底吃什么参数"这条信息。

Chromium 为了把这套边界情况兜住,写了足足几百行模板元编程。教学版就没必要陪它卷了,让调用方多敲一个 `int(int)` 是性价比最高的安排。

下一篇咱们去看取消令牌怎么搭——一个用 `shared_ptr` 配 `atomic<bool>` 拼出来的轻量级取消机制。

## 参考资源

- [Chromium bind_internal.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/bind_internal.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [P0780R2 - Pack Expansion in Lambda Capture](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0780r2.html)
