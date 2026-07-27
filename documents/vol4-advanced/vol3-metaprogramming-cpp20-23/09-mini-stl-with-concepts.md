---
chapter: 13
cpp_standard:
- 20
description: 把这一卷讲过的 concepts、requires、TMP、异常安全焊成一个东西:用 C++20 concepts 约束的 mini-STL 算法库。实现 transform、accumulate、find_if,配上恰当约束,看 concepts 在真实泛型库里带来的签名清晰与报错友好
difficulty: intermediate
order: 9
platform: host
prerequisites:
- Concepts:把模板约束写进签名
- 使用 Concepts 约束模板:subsumption 与重载
- Requires 表达式深度解析:四种成分
- TMP 核心技巧:concepts 之前的世界
reading_time_minutes: 11
related:
- Concepts:把模板约束写进签名
- 模板与异常安全:move_if_noexcept 与扩容
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 编译期计算
title: 综合项目:concepts 约束的 mini-STL 算法库
---
# 综合项目:concepts 约束的 mini-STL 算法库

子卷走到这里,概念性的东西讲了一圈:concepts 怎么写、requires 表达式的四种成分、TMP 的老技巧、编译期字符串、C++26 反射、实例化控制、异常安全。这一篇把这些东西焊起来,做一个实际的东西——一个用 concepts 约束的 mini-STL 算法库。咱们实现三个经典算法:`transform`、`accumulate`、`find_if`,给它们配上恰当的约束,看看 concepts 在一个真实的泛型库里到底带来了什么。不卖关子,就两样:签名读得懂了,报错也说人话了。

## 目标:三个算法,签名即文档

先定要做的事。`transform` 把一个 range 的元素经函数变换后写到输出;`accumulate` 把 range 的元素累加起来;`find_if` 找第一个满足谓词的元素。这三个算法标准库都有(`std::ranges::transform` 等),咱们重写一遍的目的不是替代它们,而是把这一卷学的东西攒到一个能跑的例子里。

设计上的一个原则:**每个算法的模板参数都用 concept 约束,读签名就能知道它要什么**。下面逐个看。

## 两个自定义 concept:给标准库没现成的需求起名

标准库 `<concepts>` 和 `<iterator>` 提供了一批现成概念(`input_iterator`、`predicate`、`invocable`、`convertible_to`……),大部分需求都能直接用。但有两个需求标准库没现成的,咱们自己起名:

```cpp
template <typename T, typename U = T>
concept Addable = requires(T a, U b) {
    { a + b } -> std::convertible_to<U>;
};

template <typename T>
concept Ordered = requires(T a, T b) {
    { a < b } -> std::convertible_to<bool>;
};
```

`Addable<T, U>` 问的是「`T` 和 `U` 能相加,且结果能转成 `U`」。默认 `U = T`,所以 `Addable<int>` 就是「int 能和自己加」。`Ordered` 问「能不能用 `<` 比较,结果是 bool」。这俩名字一立起来,后面算法的签名就有的写了。这一步对应的是 03 篇讲的 requires 表达式的复合要求:`{ a + b } -> std::convertible_to<U>` 既检查表达式合法,又检查返回类型满足约束。

## transform:用现成的 range 和迭代器概念

```cpp
template <std::ranges::input_range R, typename Out, typename F>
    requires std::output_iterator<Out, std::ranges::range_value_t<R>>
          && std::invocable<F&, std::ranges::range_reference_t<R>>
Out transform(R&& r, Out out, F f) {
    for (auto&& x : r) {
        *out++ = std::invoke(f, x);
    }
    return out;
}
```

这个签名读起来就是文档本身:`R` 是个 input range,`Out` 是个 output iterator(能写 `R` 的元素类型),`F` 是个可调用对象(能接受 `R` 的元素引用)。三个要求都摆在模板参数列表和 requires 子句里,没有半个 `enable_if` 套娃。

这里有个 03 篇讲过的细节值得复读:`std::invocable<F&, std::ranges::range_reference_t<R>>` 里用的 `range_reference_t<R>`,不是 `range_value_t<R>`。因为遍历 range 拿到的是元素的**引用**,函数要能接受引用类型才合规。这种「把类型算对」的活,在 concepts 之前要靠 `decltype` 和 `std::declval` 绕一圈,现在标准库的别名(`range_value_t`、`range_reference_t`)直接给到。

## accumulate:自定义 Addable 上场

```cpp
template <std::ranges::input_range R, typename T>
    requires Addable<T, std::ranges::range_value_t<R>>
T accumulate(R&& r, T init) {
    for (auto&& x : r) {
        init = init + x;
    }
    return init;
}
```

约束是 `Addable<T, range_value_t<R>>`:累加值类型 `T` 要能和 range 的元素类型相加。这就比标准库 `std::accumulate` 的「无约束模板」更直白——标准库那个传错类型时,报错会一路钻进 `operator+` 的替换失败里,而咱们这个会直接说「`Addable` 没满足」。`Addable` 的好处还在于它**不限于数值**:只要类型有 `operator+` 且结果能转回来,`std::string` 一样能 accumulate(下面实测会看到)。

## find_if:用 std::predicate

```cpp
template <std::ranges::input_range R, typename Pred>
    requires std::predicate<Pred&, std::ranges::range_reference_t<R>>
std::ranges::borrowed_iterator_t<R> find_if(R&& r, Pred pred) {
    for (auto it = std::ranges::begin(r); it != std::ranges::end(r); ++it) {
        if (std::invoke(pred, *it)) return it;
    }
    return std::ranges::end(r);
}
```

`std::predicate<Pred&, T>` 比 `std::invocable` 更严:不仅要求能调用,还要求返回类型能转成 `bool`。`find_if` 的谓词当然得返回 bool,用 `predicate` 比 `invocable` 更贴。返回类型 `borrowed_iterator_t<R>` 是 ranges 里的一个细节,处理「传临时 range 进来时迭代器会不会悬空」的问题,这里不展开。

## 跑起来

把三个算法一起跑,测试覆盖数值、字符串、谓词几种场景:

<OnlineCompilerDemo allow-run
  title="concepts 约束的 mini-STL:transform / accumulate / find_if"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/mini_stl.cpp"
  description="transform 平方、accumulate 求和与拼接、find_if 找偶数,同一个 accumulate 既能加整数也能拼字符串。"
/>

运行结果:

```text
transform 平方: 1 4 9 16
accumulate 求和:  10
accumulate 拼接:  start:abc
find_if 第一个偶数: 2
```

四行输出都符合预期。`transform` 把 `{1,2,3,4}` 平方成 `{1,4,9,16}`;`accumulate` 求和得 10;第三个最有意思,`accumulate` 拼接字符串得 `start:abc`——同一个算法,既能加整数,也能拼字符串,因为 `Addable` 只认 `operator+`,不预设是数值。这就是泛型的价值,而 concepts 让这个泛型既灵活(任何 Addable 类型)又安全(不 Addable 的进不来)。

## 传错类型,报错说人话

最有说服力的是看它怎么拒绝错的类型。咱们定义一个没有 `operator+` 的 `NoPlus`,拿它去调 `accumulate`:

```cpp
struct NoPlus {};
std::vector<NoPlus> v(3);
my::accumulate(v, NoPlus{});   // Addable 约束不满足
```

```text
constraints not satisfied
required for the satisfaction of 'Addable<T, std::ranges::range_value_t<_Range>>'
    [with T = NoPlus; R = std::vector<NoPlus, ...>&]
```

把这一段和 04 篇里 `enable_if` 那套天书对比一下。那边是 `no type named 'type' in 'std::enable_if<false, void>'`,通篇在讲 `enable_if` 的内部机制;这边直接说 `Addable<NoPlus, ...>` 没满足,把约束的名字和具体的类型都点了出来。读者不用懂 SFINAE,一眼就知道「哦,`NoPlus` 不能相加」。01 篇讲 concept 报错时说的「少掉一半头发」,在真实的泛型库里就是这么兑现的。

## concepts 给泛型库带来了什么

把这三段代码和一个等价的 C++17 `enable_if` 版本放一起,差别就清楚了。concepts 给泛型库带来三样实在的东西。

一是**签名即文档**。`requires input_range<R> && output_iterator<Out, ...>` 这种写法,读签名就知道算法要什么,不用钻进实现去看它到底对参数做了什么假设。二是**报错点名约束**。传错类型时,编译器说的是「哪个 concept 没满足」,而不是「enable_if 替换失败」。三是**约束可复用、可组合**。`Addable` 定义一次,任何需要「能相加」的算法都能用;多个 concept 用 `&&` 组合,还能靠 02 篇讲的 subsumption 规则参与重载分派。这三样加起来,就是从「写得出泛型代码」到「写得舒服、读得明白、错得起」的跨越。

## 子卷收官

这一卷从 concepts 起手,讲清了约束怎么写、怎么参与重载、requires 表达式的四种成分;然后倒回去看 TMP 的老本事——type_traits 的特化内幕、模板递归、SFINAE、`void_t`、fold expressions,以及怎么把这些往 concepts 上迁;接着是编译期字符串和 C++20 的 NTTP class type;再往 C++26 看了一眼静态反射;最后回到工程层面,讲模板实例化怎么控制、模板和异常怎么纠缠,并用这个 mini-STL 算法库把主线焊在一起。concepts 是这一卷的脊柱,但它不是全部——TMP 的递归和特化、fold expressions、条件 noexcept,这些在 concepts 出现之后仍然是写泛型代码的日常工具。读完这一卷,您应该既会用 C++20 的新办法写约束,也读得懂老库里 SFINAE 和 `void_t` 的写法,并且知道什么时候该用哪个。
