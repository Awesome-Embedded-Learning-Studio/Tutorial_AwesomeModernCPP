---
chapter: 13
cpp_standard:
- 17
- 20
description: concepts 之前的十几年里,泛型库靠 TMP 回答类型问题。讲清 type_traits 的特化内幕、模板递归、SFINAE 与 enable_if、void_t 的 detection idiom、C++17 fold expressions,以及怎么把约束类的 SFINAE 往 concepts 上迁
difficulty: intermediate
order: 4
platform: host
prerequisites:
- Concepts:把模板约束写进签名
- 使用 Concepts 约束模板:subsumption 与重载
- Requires 表达式深度解析:四种成分
reading_time_minutes: 17
related:
- Requires 表达式深度解析:四种成分
- 编译期字符串:NTTP class type 与 fixed_string
tags:
- host
- cpp-modern
- intermediate
- 模板元编程
- 编译期计算
- 泛型
title: TMP 核心技巧:concepts 之前的世界
---
# TMP 核心技巧:concepts 之前的世界

前三篇咱们一直在 C++20 concepts 的光照下看「约束」。但 concepts 是 2020 年才进标准的新东西,在那之前的十几年里,泛型库的作者要回答「这个类型合不合格」,靠的是另一套完全不同的机制——模板元编程(Template Metaprogramming,TMP)。这一篇咱们倒回去看看 TMP 的几样看家本事:用特化做编译期问询、用模板递归做编译期循环、用 SFINAE 让不合适的重载优雅退场、用 `void_t` 检测成员是否存在,以及 C++17 的 fold expressions 怎么替掉一大部分模板递归。

这些东西今天看起来啰嗦,但至今没被 concepts 完全取代。您翻标准库源码、翻 Boost、翻 Chromium 的 base,到处都是 SFINAE 和 `void_t`。concepts 接管的是「类型合不合格」这类约束,「在类型上算个数」的活儿还得靠 TMP。这一篇讲的因此是您读和写真实泛型代码绕不开的基本功。

## type_traits 的内幕:特化就是编译期的 if-else

`std::is_pointer_v<int*>` 算出 `true`,`std::is_pointer_v<int>` 算出 `false`。这个编译期的判断是怎么实现的?答案朴素得让人意外,靠的是模板偏特化。咱们自己手写一个 `is_pointer`,结构就和标准库的差不多:

```cpp
template <typename T>
struct is_pointer_impl {
    static constexpr bool value = false;
};

template <typename T>
struct is_pointer_impl<T*> {
    static constexpr bool value = true;
};

template <typename T>
constexpr bool is_pointer_v = is_pointer_impl<T>::value;
```

主模板兜底,给所有类型一个 `false`;偏特化专门匹配 `T*` 这种「指向某个类型的指针」,给 `true`。编译器实例化 `is_pointer_impl<int*>` 的时候,会发现偏特化比主模板更贴,就选偏特化;实例化 `is_pointer_impl<int>` 的时候没有更特化的版本能匹配,只能落回主模板。这就是 TMP 最基本的范式,用特化做编译期的分派,等价于一个在类型上展开的 if-else。

跑一下,和标准库对一遍:

<OnlineCompilerDemo allow-run
  title="手写 is_pointer,用特化做编译期分派"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/traits_from_scratch.cpp"
  description="主模板兜底 false,偏特化匹配 T* 给 true,和标准库 std::is_pointer 结果一致。"
/>

运行结果:

```text
is_pointer_v<int>:    false
is_pointer_v<int*>:   true
is_pointer_v<int**>:  true
is_pointer_v<double*>:true
与 std::is_pointer 结果一致
```

标准库的 `<type_traits>` 里那一两百个 trait(`is_integral`、`is_class`、`remove_const`、`decay`……)底层基本都是这套:主模板 + 一组偏特化覆盖各种情况。`remove_const_t<const int>` 变成 `int`,无非是给 `const T` 写了个偏特化,里头 `using type = T`。一旦您把这个「特化即分派」的模型想明白,再去看 `<type_traits>` 的源码就不会发怵了。

## 模板递归:用实例化做循环

光做类型问询还不够,TMP 还能在编译期算数。做法是把循环展开成一连串模板实例化,靠特化提供终止条件。经典例子是阶乘:

```cpp
template <unsigned N>
struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <>
struct Factorial<0> {
    static constexpr unsigned value = 1;
};
```

`Factorial<5>::value` 的求值过程是编译器在编译期一路实例化下去:`Factorial<5>` 引用 `Factorial<4>::value`,`Factorial<4>` 又引用 `Factorial<3>`,直到撞上 `Factorial<0>` 的全特化,`value` 是 `1`,递归才回溯回来把每一层乘出来。这个展开完全发生在编译期,到运行时 `Factorial<5>::value` 就是个常量 `120`,没有任何函数调用开销。

<OnlineCompilerDemo allow-run
  title="模板递归算阶乘,特化提供终止条件"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/tmp_factorial.cpp"
  description="Factorial<N> 递归引用 Factorial<N-1>,直到撞上 Factorial<0> 的全特化,值在编译期算定。"
/>

运行结果:

```text
Factorial<5>::value  = 120
Factorial<10>::value = 3628800
编译期断言全部通过
```

`static_assert` 能在编译期断言 `Factorial<10>::value == 3628800`,说明这个值在编译阶段就已经算定了。这种「模板递归 + 特化终止」是 TMP 的经典循环模式,曾经被用来做编译期排序、编译期字符串处理、typelist 操作各种重活。它的代价也实在:实例化层数深、编译慢,报错还特别难读。这正是后来 fold expressions 被造出来的直接动机。

## SFINAE:让不合适的重载优雅退场

traits 解决了「这个类型是什么」,但泛型库还要解决另一个问题:我有两个重载,一个给整数、一个给浮点,怎么让编译器在传错类型时**不报错、而是悄悄跳过不合适的那个**?答案是一组叫 SFINAE 的规则,全称 Substitution Failure Is Not An Error,「替换失败不是错误」。

意思是:编译器在把模板参数往签名里替换的时候,如果某一步替换出了问题(比如生成了 `int::value_type` 这种不存在的类型),它不会立刻报错,而是把这个重载当作「替换失败」默默踢出候选集,继续试别的。`std::enable_if` 就是利用这条规则的老牌工具,它把约束藏进一个默认模板参数里:

```cpp
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) {
    return a + b;
}
```

`std::enable_if_t<条件>` 只有在条件为 `true` 时才有内嵌的 `type`;条件为 `false` 时它是个空壳,替换进去会让默认参数的类型推导失败,这个重载就被 SFINAE 踢掉。结果是:传 `int` 能匹配,传 `std::string` 时这个重载压根不在候选里。

SFINAE 能跑,但它的报错信息是出了名的折磨人。咱们故意用 `std::string` 去调上面那个 `add_old`,看 GCC 16.1.1 吐什么(节选):

```text
error: no matching function for call to 'add_old(std::string, std::string)'
  candidate: 'template<class T, class> T add_old(T, T)'
    template argument deduction/substitution failed:
    error: no type named 'type' in 'struct std::enable_if<false, void>'
```

报错的核心是 `no type named 'type' in 'std::enable_if<false, void>'`。它讲的是 `enable_if` 的内部机制——条件为假所以没有 `type` 这个成员——偏偏不提您真正关心的那件事。您得先懂 SFINAE,再倒推出「哦,是因为 string 不是整数」。这正是前三篇里 concepts 反复对比的那条老路子,这里咱们是从机制这一侧看清它为什么难读。

## void_t 与 detection idiom:C++17 的高光

SFINAE 最优雅的用法,是 2014 年前后由 Walter Brown 提出的一套叫 detection idiom(检测惯用法)的模式。它的核心是一个看起来什么都没干的工具:`std::void_t`。

```cpp
template <typename...>
using void_t = void;
```

`void_t` 把任意类型映射成 `void`。它本身毫无逻辑,但配上偏特化,就能做一件 SFINAE 时代最难做的事:**优雅地检测一个类型有没有某个成员**。咱们检测「T 有没有 `value_type` 这个内嵌类型」:

```cpp
template <typename T, typename = void>
struct has_value_type : std::false_type {};

template <typename T>
struct has_value_type<T, std::void_t<typename T::value_type>> : std::true_type {};
```

主模板默认继承 `false_type`;偏特化的第二个模板参数是 `std::void_t<typename T::value_type>`。关键在编译器怎么挑这两个版本。实例化 `has_value_type<std::vector<int>>` 时,编译器会先试更特化的偏特化,把 `T` 替换成 `std::vector<int>`,然后试图求值 `std::void_t<std::vector<int>::value_type>`——`vector<int>` 确实有 `value_type`,替换成功,`void_t` 把它变成 `void`,偏特化的第二个参数落定为 `void`,和主模板的默认 `void` 对上了,偏特化被选中,结果是 `true_type`。反过来实例化 `has_value_type<int>` 时,`int::value_type` 不存在,替换失败——按 SFINAE 规则,失败不是错误,偏特化被悄悄踢掉,编译器退回主模板,结果是 `false_type`。

跑一下:

<OnlineCompilerDemo allow-run
  title="void_t 的 detection idiom,检测内嵌类型是否存在"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/void_t_detection.cpp"
  description="主模板继承 false_type,偏特化靠 void_t 在替换成功时被选中,从而优雅地返回 true 或 false。"
/>

运行结果:

```text
has_value_type_v<std::vector<int>>: true
has_value_type_v<std::string>:      true
has_value_type_v<int>:              false
```

`void_t` 把过去又脏又长的 SFINAE 检测代码收成两行偏特化,这是它在 C++17 被正式收进标准的原因。但有个事实得说清楚。

::: warning 进了标准的只有 void_t,不是整套 detection idiom
Walter Brown 围绕 detection idiom 写过三篇提案(N3911 → N4436 → N4502),最后一篇 N4502 提议把 `std::is_detected`、`std::detected_t` 这一套现成的检测工具收进标准库。但这套库工具**最终没被投票通过**,进 C++17 的只有 `void_t` 这一个原子零件。所以您在标准库里找不到 `std::is_detected`,要用 detection idiom 只能照着上面那两行偏特化自己手写,或者借助 Boost 等第三方库。concepts 出来之后,「检测某个操作存不存在」的需求大多能更直接地写成 `requires(T t){ t.foo(); }`,新代码里的 detection idiom 在减少,但读老库时它仍然到处都是。
:::

## fold expressions:干掉递归样板

模板递归能算阶乘,但遇到「对任意个参数求和」这种 variadic 任务,递归写法得给一个主模板、一个递归分支、再加一个终止特化,样板很重。C++17 的 fold expressions 把这块直接省了。对照下面两段:

```cpp
// 老办法:递归 + 终止
template <typename T>
constexpr T sum_rec(T first) { return first; }

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);
}

// C++17:一个 fold 表达式收掉
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);  // 一元右折叠
}
```

`(ts + ...)` 把参数包里的所有东西用 `+` 折叠起来。跑一下,两种写法结果一致:

<OnlineCompilerDemo allow-run
  title="variadic 递归 vs C++17 fold expression"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/fold_vs_recursion.cpp"
  description="老办法要主模板、递归分支、终止特化三段,C++17 一个 (ts + ...) 折叠就收掉了。"
/>

运行结果:

```text
sum_rec(1,2,3,4):  10
sum_fold(1,2,3,4): 10
逗号折叠展开: 1 2.5 hi
```

最后一行那个「逗号折叠展开」是 fold 的另一个常见用法:用逗号运算符把一包操作并起来,`(printer(1), printer(2.5), printer("hi"))` 一行就能展开任意个调用。这在 variadic 场景里几乎替代了过去的递归展开。

fold 一共有四种形式,记一张表就够:

| 形式 | 写法 | 含义(包为 a, b, c,初值 e) |
|---|---|---|
| 一元右折叠 | `(pack op ...)` | `(a op (b op c))` |
| 一元左折叠 | `(... op pack)` | `((a op b) op c)` |
| 二元右折叠 | `(pack op ... op e)` | `(a op (b op (c op e)))` |
| 二元左折叠 | `(e op ... op pack)` | `(((e op a) op b) op c)` |

二元形式带个初值 `e`,主要是为了解决空包的问题。一元 fold 对空参数包是 ill-formed,`(ts + ...)` 在没有参数时会编译失败,因为「什么都没有」没法 `+`。但 `&&`、`||` 和逗号这三种运算符的一元 fold 对空包有规定的默认值(`&&` 是 `true`、`||` 是 `false`、逗号是 `void()`),所以 `(... && bs)` 即便 `bs` 为空也能编过。这点在写「一堆约束全部满足」时特别有用,后面讲综合项目还会再用到。

## SFINAE 往 concepts 迁移:同一需求的新旧写法

走到这里,咱们可以把这一篇的东西和前三篇焊一下。同样是「`add` 只接受整数」这个需求,SFINAE 老办法和 concept 新办法放在一起:

```cpp
// SFINAE(C++11 起):约束藏在默认模板参数里
template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
T add_old(T a, T b) { return a + b; }

// concept(C++20):约束写在签名里
template <typename T>
    requires std::integral<T>
T add_new(T a, T b) { return a + b; }
```

用 `std::string` 去调 concept 版,报错直接点名约束:

```text
error: no matching function for call to 'add_new(std::string, std::string)'
  constraints not satisfied
  required for the satisfaction of 'integral<T>' [with T = std::__cxx11::basic_string<char>]
```

对比前面 SFINAE 版那个 `no type named 'type' in 'std::enable_if<false, void>'`,差别一目了然:concept 版说人话,直接告诉您 `integral<T>` 没满足,还把具体类型代进去。`enable_if` 版讲的是它自己的内部机制,您得自己翻译回「到底哪条没满足」。

那是不是所有 SFINAE 都该迁成 concepts?大体上,凡是用来「约束模板参数合不合格」的 SFINAE,今天都该优先用 concept,可读性和报错质量是质变。detection idiom 那种「检测某个成员或操作存不存在」的需求,concepts 用 requires 表达式也能写得相当干净(`requires(T t){ t.foo(); }`),新代码同样推荐迁。真正不必迁的,是把 SFINAE 当「类型计算零件」用的场景——比如根据某个条件从两个类型里挑一个,那本来就该用 `std::conditional_t`,跟约束不是一回事。

concepts 接管了 SFINAE 最难读的那部分,没接管 TMP 的全部:模板特化做类型问询、fold expressions 做编译期折叠,这俩仍然是写泛型代码的日常工具。下一篇咱们把 TMP 往一个具体方向推——编译期字符串处理,看看 C++20 的 NTTP class type 怎么让「把字符串当模板参数」这件过去极痛苦的事变得顺手。
