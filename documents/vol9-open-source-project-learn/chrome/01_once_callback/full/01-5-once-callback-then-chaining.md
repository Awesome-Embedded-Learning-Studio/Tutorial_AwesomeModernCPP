---
chapter: 1
cpp_standard:
- 23
description: 逐行拆解 then() 的所有权链设计——从管道思维到 void/非 void 分支处理，理解 OnceCallback 中最精巧的所有权管理
difficulty: beginner
order: 5
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 前置知识（二）：std::invoke 与统一调用协议
- OnceCallback 前置知识（三）：Lambda 高级特性
reading_time_minutes: 7
related:
- OnceCallback 实战（六）：测试与性能对比
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
- 模板
title: OnceCallback 实战（五）：then 链式组合
---
# OnceCallback 实战（五）：then 链式组合

`then()` 把两个回调串成一根管道，上一个的输出喂给下一个。Unix 管道的老把戏，您肯定不陌生：

```bash
# Unix 管道：cmd1 的输出是 cmd2 的输入
echo "hello" | tr 'h' 'H' | wc -c
```

落到回调上，就是同一回事——回调 A 的输出交给回调 B：

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // 第一步：3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // 第二步：7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

咱们一开始想得轻巧，`then()` 不就是把俩回调缝一块嘛。可 OnceCallback 是 move-only 的，原回调的所有权得整副家当搬进新回调里——少了 `func_`、少了 `token_`、少了 `status_`，哪一样都不行。这一篇笔者就带您逐行把 `then()` 拆开，重点盯两件事：所有权链是怎么一节一节接上的，void 和非 void 两套返回类型又是怎么分叉处理的。

## 所有权：then() 的真问题

如果您用过 Unix 管道，`then()` 的语义就很直觉了：

```bash
# Unix 管道：cmd1 的输出是 cmd2 的输入
echo "hello" | tr 'h' 'H' | wc -c
```

`then()` 做的是同样的事情——回调 A 的输出是回调 B 的输入。用代码表达：

```cpp
auto pipeline = OnceCallback<int(int, int)>([](int a, int b) {
    return a + b;          // 第一步：3 + 4 = 7
}).then([](int sum) {
    return sum * 2;        // 第二步：7 * 2 = 14
});

int result = std::move(pipeline).run(3, 4);  // result == 14
```

`then()` 把两个独立的回调串联成一个新的回调。调用新回调时，自动走完 A → B 的整个流程。

---

串联后的新回调得把原回调和后续回调都攥在自己手里。这事儿在普通 `std::function` 上不算难——拷一份就完事——可 OnceCallback 偏偏是 move-only 的，`func_`、`status_`、`token_` 一样都不许复制。`then()` 只能消费 `*this` 和 `next`，把两副家当整个搬进一个新的 lambda 闭包。

所有权链条画出来就一根线：

```mermaid
graph LR
    A["新 OnceCallback"] --> B["move_only_function"] --> C["lambda 闭包"] --> D["原回调 + 后续回调"]
```

每一节都是移动语义在接力，没有拷贝、没有共享。move-only 这套约束在 `then()` 里的完整长相，就是这根线。

---

## then() 的完整实现逐行拆解

```cpp
template<typename ReturnType, typename... FuncArgs>
template<typename Next>
auto OnceCallback<ReturnType(FuncArgs...)>::then(Next&& next) && {
    using NextType = std::decay_t<Next>;

    if constexpr (std::is_void_v<ReturnType>) {
        using NextRet = std::invoke_result_t<NextType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont));
            });
    } else {
        using NextRet = std::invoke_result_t<NextType, ReturnType>;
        return OnceCallback<NextRet(FuncArgs...)>(
            [self = std::move(*this),
             cont = std::forward<Next>(next)]
            (FuncArgs... args) mutable -> NextRet {
                auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
                return std::invoke(std::move(cont), std::move(mid));
            });
    }
}
```

### 函数签名：右值限定

```cpp
auto then(Next&& next) &&
```

末尾那个 `&&` 把它做成了右值限定的成员函数，意思是 `then()` 只接受 `std::move(cb).then(next)` 或者临时对象上的 `.then(next)`。谁要是不小心写了 `cb.then(next)` 这种左值调用，编译器当场就甩一句"没有匹配的重载函数"，连错都报得直白。这跟 `run()` 走 deducing this 那一套是两条路——`run()` 得在左值和右值上分别给出不同的错误信息，麻烦些；`then()` 不用区分，一个 ref-qualifier 就够了，干净。

### std::decay_t\<Next\>：退化去掉引用

```cpp
using NextType = std::decay_t<Next>;
```

`Next` 进来的时候可能是 `SomeLambda&&`，也可能是 `SomeLambda&`，沾着引用就不好做后续的类型推导。`std::decay_t` 把引用扒掉，留下裸的 lambda 类型，后面 `std::invoke_result_t` 就拿这个 `NextType` 去查返回。

### if constexpr 的两个分支

真正让 `then()` 分叉的，是原回调返回类型到底是不是 void。这一刀切下去，两边长得很不一样。

原回调返回一个值的情况——也就是非 void 分支——这个值得接着往下喂给后续回调：

```cpp
using NextRet = std::invoke_result_t<NextType, ReturnType>;
```

`std::invoke_result_t<NextType, ReturnType>` 在编译期替咱们问一句：把一个 `ReturnType` 类型的值递给 `NextType` 这种可调用对象，它吐回来的又是什么类型？这便是新管道对外的返回类型。lambda 体内的活儿也好讲，先把原回调跑起来拿到中间结果 `mid`，再原样递给后续回调：

```cpp
auto mid = std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont), std::move(mid));
```

void 分支则换了个长相。原回调啥也不返回，后续回调自然也就不收参数：

```cpp
using NextRet = std::invoke_result_t<NextType>;
```

这里 `std::invoke_result_t<NextType>` 推导的是"空着参数列表调 `NextType`，得到啥"。lambda 体内就两步：先跑原回调，结果扔掉不管；再把后续回调掏出来执行，也不传参：

```cpp
std::move(self).run(std::forward<FuncArgs>(args)...);
return std::invoke(std::move(cont));
```

### lambda 捕获：所有权的核心

```cpp
[self = std::move(*this), cont = std::forward<Next>(next)]
```

`self = std::move(*this)` 是整条所有权链的要害。它把当前 OnceCallback 的全部家当——`func_`、`status_`、`token_`，一个不落——搬进 lambda 的闭包里。搬完之后，当前对象就是个被掏空的壳，`func_` 和 `token_` 都不归它了。`cont = std::forward<Next>(next)` 把后续回调也接进来，`std::forward` 守着 `next` 本来的值类别：右值就移动，左值就拷贝。

这个 lambda 最终被递给一个新的 `OnceCallback<NextRet(FuncArgs...)>` 构造函数，塞进它的 `std::move_only_function`。类型擦除那套机制，使得不管外头这个 lambda 长成什么样，都能被收编进同一个壳子。

---

## 多级管道

`then()` 自然可以一节一节接下去，接成多级管道：

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
// 5 * 2 = 10, 10 + 10 = 20, to_string(20) = "20"
```

每调一次 `then()` 都会新铸一个 OnceCallback，里头嵌着捕获了前一步回调的闭包。最外层那次 `run()` 一动，执行就像套娃一样层层展开：最外层被 `run()` → 跑它自己的 lambda → lambda 里头对上一层再 `std::move(self).run()` → 再往上一层 → 一路钻到底。

代价也有。每多一级 `then()`，就多一次 `std::move_only_function` 的间接调用。两三级管道这点开销完全可以忽略；真要堆到十级以上，嵌套深了恐怕得换扁平化的管道结构——不过那已经离咱们眼下的题目太远，先按下不表。

## 几个容易踩坑的地方

### mutable 不可省略

lambda 里头要调 `std::move(self).run()`，这一下是真改 `self` 的状态——把 status 从 kValid 拨到 kConsumed。lambda 不加 `mutable`，`self` 在里头就是个 const 引用，对 const 对象动手脚这种事编译器见一次拦一次，直接报错。

### self = std::move(*this) 的状态

搬完之后，原 OnceCallback 的 `func_` 和 `token_` 都已经离家出走了，落得个"被移走"的下场。`status_` 没人显式把它拨回 kEmpty，原值还挂着。可 `func_` 都空了，这壳子实际已经废了，谁再去碰它一下都是未定义行为。好在 `then()` 那个 `&&` 限定把门守死了，调用方压根没机会在 `then()` 之后接着用原对象。

### 为什么用 std::invoke 而不是直接调用

`cont` 多半就是个 lambda，您直接写 `cont(mid)` 也跑得动。可万一哪天有人递进来一个成员函数指针当后续回调，直接调用的语法当场就废了，`std::invoke` 不会。统一走 `std::invoke`，就是图个无论对方使什么家伙式儿，咱们这一套都接得住。

## 参考资源

- [Chromium callback.h 源码](https://chromium.googlesource.com/chromium/src/+/HEAD/base/functional/callback.h)
- [cppreference: std::invoke](https://en.cppreference.com/w/cpp/utility/functional/invoke)
- [cppreference: if constexpr](https://en.cppreference.com/w/cpp/language/if)
