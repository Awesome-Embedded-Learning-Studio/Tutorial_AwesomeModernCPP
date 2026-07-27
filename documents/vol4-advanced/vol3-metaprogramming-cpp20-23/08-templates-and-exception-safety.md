---
chapter: 13
cpp_standard:
- 11
description: 模板和异常纠缠在一起的那条线:异常安全三档、noexcept 作为契约、move_if_noexcept 怎么在「可能回滚」时退回拷贝、vector 扩容凭什么保住强异常保证,以及模板里用条件 noexcept 把底层的是否抛异常如实传出去
difficulty: intermediate
order: 8
platform: host
prerequisites:
- TMP 核心技巧:concepts 之前的世界
- 模板实例化控制:extern template 与编译时间
reading_time_minutes: 12
related:
- 模板实例化控制:extern template 与编译时间
- 综合项目:concepts 约束的 mini-STL 算法库
tags:
- host
- cpp-modern
- intermediate
- 模板
- 类型安全
- 内存管理
title: 模板与异常安全:move_if_noexcept 与扩容
---
# 模板与异常安全:move_if_noexcept 与扩容

上一篇末尾留了个话头:`vector` 扩容为什么要在意元素类型的 `noexcept`。这一篇就把这条线讲透。模板和异常看起来是两个不相干的话题,但只要您写过容器或者泛型算法,它们就会在「扩容时该 move 还是 copy」这种地方撞到一起。核心是两个工具:`std::move_if_noexcept` 和条件 `noexcept`。理解了它们,您就理解了为什么大家都反复强调「move 构造函数能 noexcept 就一定要 noexcept」。

## 先把异常安全的几档说清楚

异常安全有几档约定俗成的保证,先快速过一遍,后面用的主要是前两档。

- **基本保证**:函数要么成功,要么抛异常,但即便抛了也不会泄漏资源、不会把对象留在损坏的状态。
- **强保证**(strong guarantee):函数要么成功,要么「像没调用过一样」,整个程序状态回滚到调用前。
- **不抛保证**(`noexcept`):函数保证不抛异常。

强保证比基本保证严格得多,它要求「能回滚」。这一篇的主线就是:`vector` 扩容想保住强保证,而这个目标直接决定了它搬元素时该 move 还是 copy。

## noexcept 是给调用方的契约

`noexcept` 这个关键字,经常被误解成「我尽量不抛」。它真正的意思是「我保证不抛」,一旦函数真抛了异常,程序直接 `std::terminate`,不展开、不传播。所以 `noexcept` 不是写给函数自己的,是写给**调用方**的契约——调用方看到 `noexcept`,就可以放心做优化。

对这一篇最重要的优化是:`noexcept` 的 move 才敢被用在「不能中途失败」的场景。`vector` 扩容搬元素就是这么个场景。如果您的 move 构造函数标了 `noexcept`,`vector` 扩容时就敢用它搬;没标,`vector` 就不敢,宁可去 copy。这个差别下面会实跑出来。

## move_if_noexcept:可能要回滚时,退回拷贝

C++11 给了一个工具 `std::move_if_noexcept`,它把「move」和「强异常保证」的张力收进了一个函数。行为很直白:如果 `T` 的 move 构造是 `noexcept` 的,它返回右值引用(走 move);否则返回 const 左值引用(走 copy)。

```cpp
// 大意(标准库的真实实现等价于这个判断)
template <typename T>
conditional_t<is_nothrow_move_constructible_v<T> && !is_lvalue_reference_v<T>,
              T&&, const T&>
move_if_noexcept(T& x) noexcept;
```

为什么需要这么个东西?设想一个「搬完可能要回滚」的操作:把一批元素从旧内存搬到新内存,搬到一半某个 move 抛了。如果用的是 move,被搬的元素可能已经被从旧内存里掏走了一半,状态乱了,想回滚也回滚不回去,强保证就没了。如果用的是 copy,copy 抛了的时候,旧内存里的原始元素根本没被动过,回滚就是「释放新内存」,干干净净。所以「可能要回滚」的场合,move 不够安全就该退回 copy,这就是 `move_if_noexcept` 的用途。咱们实测一下:

```cpp
struct NothrowMove {
    int* p;
    explicit NothrowMove(int v) : p(new int(v)) {}
    NothrowMove(const NothrowMove& o) : p(new int(*o.p)) { std::cout << "    copy\n"; }
    NothrowMove(NothrowMove&& o) noexcept : p(o.p) { o.p = nullptr; std::cout << "    move\n"; }
};

struct ThrowingMove {
    int* p;
    explicit ThrowingMove(int v) : p(new int(v)) {}
    ThrowingMove(const ThrowingMove& o) : p(new int(*o.p)) { std::cout << "    copy\n"; }
    ThrowingMove(ThrowingMove&& o) noexcept(false) : p(o.p) { o.p = nullptr; std::cout << "    move\n"; }
};
```

<OnlineCompilerDemo allow-run
  title="move_if_noexcept:按 noexcept 在 move/copy 间挑"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/move_if_noexcept_demo.cpp"
  description="NothrowMove 走移动、ThrowingMove 退回拷贝,看 move_if_noexcept 怎么按 noexcept 决定。"
/>

运行结果:

```text
is_nothrow_move_constructible:
  NothrowMove:  true
  ThrowingMove: false
move_if_noexcept 对 NothrowMove(应为 move):
    [NothrowMove] 被移动
move_if_noexcept 对 ThrowingMove(应为 copy):
    [ThrowingMove] 被拷贝
```

`NothrowMove` 的 move 是 `noexcept`,所以 `move_if_noexcept` 给了右值引用,走 move;`ThrowingMove` 的 move 标了 `noexcept(false)`,可能抛,`move_if_noexcept` 退回给 const 引用,走 copy。这就是它「看 noexcept 决定 move 还是 copy」的行为。

## vector 扩容凭什么保住强异常保证

`std::vector` 在元素装满后再 `push_back`,会分配一块更大的新内存,把旧元素搬过去,再释放旧内存。这个「搬」用的就是 `move_if_noexcept`。咱们直接看不同元素类型下,扩容时的 copy/move 调用:

<OnlineCompilerDemo allow-run
  title="vector 扩容的 move vs copy,看强异常保证"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/vector_realloc.cpp"
  description="扩容时 NothrowMove 被 move、ThrowingMove 被 copy,实证 vector 为保强保证付出的代价。"
/>

运行结果:

```text
vector<NothrowMove> 预留 2,再 push 第三个触发扩容:
  >>> 扩容时(move noexcept,应为 move):
    move
    move

vector<ThrowingMove> 预留 2,再 push 第三个触发扩容:
  >>> 扩容时(move 可能抛,应为 copy 保强异常保证):
    copy
    copy
```

`NothrowMove` 的 move 是 `noexcept`,`vector` 扩容放心地 move,又快又不抛;`ThrowingMove` 的 move 可能抛,`vector` 不敢用,老老实实 copy。copy 比 move 慢(要深拷贝 `int*` 指向的内容),但这是保强保证的代价:万一 copy 中途抛了,旧内存里的元素原封不动,`vector` 能回滚到扩容前的状态。

::: warning 您的 move 构造函数没标 noexcept,vector 就会 copy
这是这一篇最实在的教训。很多初学者写完一个类的 move 构造函数,觉得「反正不会抛」就不标 `noexcept`。结果这个类塞进 `vector` 里,每次扩容都被 copy 一遍,白白损失了 move 的性能,而且编译器不会给您任何提示。规则记死:move 构造、move 赋值,只要您确定它不会抛(通常它只是在搬指针和几个基础类型),就一定要加 `noexcept`。这不只是风格,是实打实的性能开关。
:::

当然不是所有 move 都能 `noexcept`。如果一个 move 内部要分配内存(比如 move 一个 `std::vector` 的元素,可能要分配新 `vector`),分配可能抛,那就不能瞎标。标准库的 `std::vector` 自己的 move 构造是 `noexcept` 的,因为它只是偷对方的内部指针,不分配。判断标准很朴素:move 里干了什么,有没有可能抛。

## 条件 noexcept:把底层会不会抛如实传出去

模板函数自己不知道它操作的类型 `T` 会不会抛,但它可以「继承」这个信息,靠的是**条件 noexcept**:`noexcept(noexcept(表达式))`。外层 `noexcept` 是说明符,内层 `noexcept(...)` 是运算符(在编译期求值,问「这个表达式是不是 noexcept」)。合起来就是「我这个函数的 noexcept 性,等于底层那个表达式的 noexcept 性」。

```cpp
// 没标 noexcept:调用方只能保守地认为它可能抛
template <typename T>
void uncond_op(T& x) {
    T tmp(std::move(x));
    x = std::move(tmp);
}

// 条件 noexcept:继承 T 的 move 构造是否 noexcept
template <typename T>
void cond_op(T& x) noexcept(noexcept(T(std::move(x)))) {
    T tmp(std::move(x));
    x = std::move(tmp);
}
```

跑一下,看两种写法在调用方眼里的 noexcept 性:

<OnlineCompilerDemo allow-run
  title="条件 noexcept:把底层会不会抛如实传出去"
  source-path="code/examples/vol4/vol3-metaprogramming-cpp20-23/noexcept_propagation.cpp"
  description="没标 noexcept 的模板被保守认为可能抛,noexcept(noexcept(...)) 让 noexcept 性跟着底层走。"
/>

运行结果:

```text
uncond_op<NoThrowMove> noexcept: false
cond_op<NoThrowMove> noexcept:   true
cond_op<ThrowMove> noexcept:     false
```

`uncond_op` 哪怕底层类型 move 是 noexcept,自己没标,调用方查出来还是 `false`——它白白丢掉了这个信息。`cond_op` 用条件 noexcept 把底层的 truth 传了出来:底层 move noexcept 时它就 noexcept,底层可能抛时它也如实是 `false`。这一条在写泛型容器、算法时很要紧:您的 `swap`、`move`、`emplace` 这些操作如果不用条件 noexcept,就会在整条调用链上把「本来 noexcept」的操作误报成「可能抛」,下游容器一查就退回 copy,move 的优化全丢了。

## 串起来

这一篇的几样东西是同一条逻辑串起来的。`noexcept` 是给调用方的契约,说「我不抛」;`move_if_noexcept` 利用这个契约,在「可能要回滚」的场合挑是 move 还是 copy;`vector` 扩容正是这样的场合,所以它根据元素 move 是否 noexcept 决定 move 还是 copy;条件 `noexcept` 则让模板函数能把底层的 noexcept 性如实往上传。所以一个看似只是「写法问题」的 `noexcept` 标注,实际上是 move 性能能不能兑现的关键。下一篇咱们把这些元编程的本事攒到一起,做一个用 concepts 约束的 mini-STL 算法库当收尾。
