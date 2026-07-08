---
chapter: 0
cpp_standard:
- 17
description: 深入理解 std::invoke 如何统一函数指针、成员函数指针、lambda、仿函数的调用方式，以及 std::invoke_result_t
  在 OnceCallback 中的类型推导作用
difficulty: intermediate
order: 2
platform: host
prerequisites:
- OnceCallback 前置知识速查：C++11/14/17 核心特性回顾
- OnceCallback 前置知识（一）：函数类型与模板偏特化
reading_time_minutes: 8
related:
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（五）：then 链式组合
tags:
- host
- cpp-modern
- intermediate
- 函数对象
- std_invoke
title: OnceCallback 前置知识（二）：std::invoke 与统一调用协议
---
# OnceCallback 前置知识（二）：std::invoke 与统一调用协议

咱们在上一篇把函数类型和模板偏特化理顺了，这篇要解决一个更烦人的问题：可调用对象的调用语法。

写回调系统最直觉的诉求是，管传进来什么，函数指针也好、lambda 也好、成员函数指针也好，咱们都想用同一种写法把它跑起来。可 C++ 偏不给您这个面子。普通函数直接 `f(args...)` 就行，成员函数指针却非得 `(obj.*pmf)(args...)`，连指向数据成员的指针都得走 `obj.*pmd` 那一套。十种可调用对象，您就得在模板里写十套分支判断它到底属于哪一类。这事写多了血压会上来。

`std::invoke`（C++17）就是来把这堆语法糊成一层的。OnceCallback 的 `bind_once` 和 `then()` 内部全靠它，才能做到管您传进来什么都能正确调用。

## 问题：可调用对象的调用语法分裂

咱们把几种常见的可调用对象摆一起看看，您就明白这套分裂有多扎眼。

普通函数指针最老实，怎么写都行：

```cpp
int add(int a, int b) { return a + b; }
int (*fp)(int, int) = &add;

int result = fp(3, 4);       // 直接调用
int result2 = (*fp)(3, 4);   // 解引用后调用（等价）
```

lambda 和仿函数走的是 `operator()`，语法上跟普通函数长得几乎一样，这部分还算友好：

```cpp
auto lam = [](int a, int b) { return a + b; };
int result = lam(3, 4);  // 通过 operator() 调用

struct Adder {
    int operator()(int a, int b) { return a + b; }
};
Adder fn;
int result2 = fn(3, 4);  // 同样通过 operator() 调用
```

事情到成员函数指针这里就开始拧巴了。它没法像普通函数那样直接 `()`，您得先有个对象实例，然后用 `.*` 或 `->*` 这两个生僻运算符把它套上去。笔者第一次写这玩意儿翻了半天书：

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
int (Calculator::*pmf)(int, int) = &Calculator::multiply;

// 必须用 .* 运算符
int result = (calc.*pmf)(3, 4);  // result == 12
```

还有一类更冷门的，指向数据成员的指针。C++ 允许您取数据成员的「指针」，它本质是个偏移量，访问方式同样得走 `.*`：

```cpp
struct Point {
    double x, y;
};

Point p{1.0, 2.0};
double Point::*pmx = &Point::x;

double val = p.*pmx;  // val == 1.0
```

您看出来了吧。如果您在写一个模板函数，要调用一个「完全不知道具体类型」的可调用对象，根本没法写出一个统一的语法。您不知道它是函数还是成员指针，写错一个就编译失败。`std::invoke` 就是来堵这个窟窿的。

---

## std::invoke 的分派规则

`std::invoke(f, args...)` 干的活儿其实就一句话：盯着 `f` 和 `args` 的具体类型，挑出对的调用语法。标准里把这套规则叫 INVOKE 表达式，分三大类。

最棘手、也最值得记牢的是成员函数指针这一类。当 `f` 是指向成员函数的指针，而 `args` 第一个元素是对象本身（可以是引用、可以是值，也可以是指向对象的指针），`std::invoke` 就把它展开成 `(obj.*pmf)(rest...)`：

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;

// 通过引用
std::invoke(&Calculator::multiply, calc, 3, 4);        // (calc.*multiply)(3, 4)
// 通过指针
std::invoke(&Calculator::multiply, &calc, 3, 4);       // ((*ptr).*multiply)(3, 4)
```

第二行那个 `&calc` 您留意一下。第一个实参是指针的时候，`std::invoke` 会替您把指针解引用再走 `.*`。这行为看着不起眼，但在 `bind_once` 绑定成员函数的时候救命，后文咱们会看到。

指向数据成员的指针走的是同一路子，只是把「调用」换成了「访问」：

```cpp
struct Point { double x, y; };
Point p{1.0, 2.0};

double val = std::invoke(&Point::x, p);    // p.*&Point::x == p.x
```

剩下那批，函数指针、lambda、仿函数，凡是能直接 `()` 上去的，`std::invoke` 就老老实实给您 `f(args...)`：

```cpp
std::invoke([](int a, int b) { return a + b; }, 3, 4);  // lambda(3, 4)
```

三类合起来看，关键就一句话：管您 `f` 落在三类里头哪一类，写出来都是 `std::invoke(f, args...)` 这一种长相。您的模板代码再也不必知道 `f` 到底是什么货色，分派这事儿 `std::invoke` 在内部替您办了。

---

## std::invoke_result_t：编译期推导返回类型

光会统一调用还不够。有些场合您得在编译期就把「`std::invoke(f, args...)` 到底返回什么类型」这事问出来，`then()` 的链式实现就是典型。您把上一个回调的返回值往下喂给下一个回调，编译器得提前算出这条链最终吐什么类型，不然类型签名都没法落笔。

`std::invoke_result_t<F, Args...>` 就是来算这事的。您喂它一个可调用对象类型 `F` 加一组参数类型 `Args...`，它在编译期把 `std::invoke(f, args...)` 的返回类型给您算出来：

```cpp
#include <type_traits>
#include <functional>

auto add(int a, int b) -> int { return a + b; }

// 编译期推导 add(1, 2) 的返回类型
using R = std::invoke_result_t<decltype(add), int, int>;
static_assert(std::is_same_v<R, int>);

// 对 lambda 也能推导
auto lam = [](double x) { return std::to_string(x); };
using R2 = std::invoke_result_t<decltype(lam), double>;
static_assert(std::is_same_v<R2, std::string>);
```

## 在 OnceCallback 源码里它俩具体怎么用

咱们直接对着 OnceCallback 的源码看，比抽象讲更清楚。`std::invoke` 在里头出现了两次，分别管 `bind_once` 和 `then()`。

先看 `bind_once` 里这一段：

```cpp
// bind_once 的 lambda 内部
return std::invoke(
    std::move(f),
    std::move(bound)...,
    std::forward<decltype(call_args)>(call_args)...
);
```

这里头那个 `f` 是来者不拒的，它可能是 lambda，可能是成员函数指针，甚至可能是指向数据成员的指针。您要是不用 `std::invoke`，直接写一句 `f(bound..., call_args...)`，遇到成员函数指针当场编不过，成员函数指针压根不能直接套 `()`。

`then()` 里这一段同理：

```cpp
// then() 的非 void 分支
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

这里的 `cont`（后续回调）按设计就是个普通可调用对象，多半是 lambda，理论上您写 `cont(mid)` 八成也能跑。那笔者干嘛还要套一层 `std::invoke`？当防御性写法用。哪天有人手滑传进来一个成员函数指针当后续回调，直接调用语法当场挂掉，而 `std::invoke` 不会。统一走它，能省下为特殊类型开洞的麻烦。

至于 `then()` 怎么用 `std::invoke_result_t` 推导返回类型，咱们的需求很具体：链式调用里下一个回调 `next` 拿到上一个回调的返回值后，自己又返回什么。代码读起来是这样：

```cpp
// 在 then() 的非 void 分支中
using NextRet = std::invoke_result_t<NextType, ReturnType>;
// NextRet 就是"把 ReturnType 类型的值传给 next，返回什么类型"
```

void 分支那一路，后续回调根本不收参数，调用形态就简化了：

```cpp
// 在 then() 的 void 分支中
using NextRet = std::invoke_result_t<NextType>;
// next 不接受参数，直接调用
```

## 踩坑预警：成员函数绑定的生命周期陷阱

`std::invoke` 把调用语法统一了，但有一件事它不管：对象什么时候死。这点特别容易栽，因为统一调用的便利性会让人忘了底下还藏着裸指针。

您在 `bind_once` 里绑成员函数，写出来是这样的：

```cpp
struct Calculator {
    int multiply(int a, int b) { return a * b; }
};

Calculator calc;
auto bound = bind_once<int(int)>(&Calculator::multiply, &calc, 5);
```

那个 `&calc` 是个裸指针，`bind_once` 把它原样存进 lambda 的捕获列表。要是在回调真正跑起来之前 `calc` 先一步析构了，lambda 里头攥着的就一个悬空指针，`std::invoke` 照常通过它去摸内存，未定义行为，十有八九段错误。这事 `std::invoke` 帮不了您，它压根不知道您传进来的指针合不合法。

Chromium 在 `//base` 里把这事儿做得很细：`base::Unretained` 让您显式声明「这裸指针的生命周期我自己兜底」，`base::Owned` 干脆把对象所有权交给回调框架接管，`base::WeakPtr` 则在对象析构时自动把回调作废。咱们这版 OnceCallback 是简化教学版，这层保护暂时不给，安全责任先压在调用方身上。这个取舍后头实战篇还会再聊，到时候您就明白为什么 Chromium 非要发明 `WeakPtr` 这么个机制。

下一篇咱们去看 Lambda 的高级特性，特别是 C++20 的 init-capture 包展开，那玩意儿是 `bind_once` 能写得这么利索的关键。

## 参考资源

- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: std::invoke_result](https://en.cppreference.com/w/cpp/types/result_of)
- [cppreference: Callable](https://en.cppreference.com/w/cpp/named_req/Callable)
