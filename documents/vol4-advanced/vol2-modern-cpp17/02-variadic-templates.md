---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 可变参数模板怎么把任意个参数吃下去——参数包的声明、sizeof... 数个数、模式展开 pattern expansion 与 fold 的区别、三种展开写法对照,以及空包的坑
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'if constexpr:编译期分支'
- 'TMP 核心技巧:concepts 之前的世界'
reading_time_minutes: 14
related:
- 'if constexpr:编译期分支'
- '完美转发:forwarding references 与引用折叠'
- 'TMP 核心技巧:concepts 之前的世界'
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- 编译期计算
title: '可变参数模板:参数包的展开'
---
# 可变参数模板:参数包的展开

上一篇咱们把 `if constexpr` 讲透了:它让模板函数能按编译期条件挑分支,被丢弃的分支不实例化。这一篇咱们接上一个更早就存在、也更绕不开的需求——让一个模板吃下**任意个数**的参数。`std::make_unique<T>(arg1, arg2, arg3)`、`std::tuple<int, double, std::string>`、`std::printf("%d %d", a, b)`,这些 API 背后都是同一套机制:可变参数模板(variadic template)。

可变参数模板从 C++11 进标准,核心就一个概念:**参数包**(parameter pack)——一串能装零到 N 个类型或值的占位。但「装进去」好说,「拿出来用」才是真正的难点。这一篇咱们就把参数包的展开机制讲清楚:`sizeof...` 怎么数个数、**模式展开**怎么逐个处理、它和 fold 有什么不一样,以及那个最容易踩的空包坑。

## 参数包长什么样

先看声明。参数包分两种。

一种是**模板参数包**,写在模板参数列表里,用省略号 `...` 标记:

```cpp
template <typename... Ts>   // Ts 是一个模板参数包,能匹配任意个类型
struct Tuple {};
```

另一种是**函数参数包**,在函数签名里,它的类型通常就是上面那个模板参数包展开成实参:

```cpp
template <typename... Ts>
void f(Ts... args);   // args 是一个函数参数包,能接收任意个实参
```

`Ts...` 这里的意思是「把 `Ts` 这个包里的每个类型依次拿出来当参数类型」。`f(1, 2.5, "hi")` 调用时,`Ts` 被推导成 `int, double, const char*`,`args` 就是这三个实参。注意 `Ts` 是个类型包,`args` 是个值包,两者一一对应。

拿个数是第一个最常见的需求。`sizeof...(pack)` 在编译期给您包里元素的个数:

<OnlineCompilerDemo allow-run
  title="sizeof... 数包大小,模式展开对异类型包逐元素独立调用"
  source-path="code/examples/vol4/vol2-modern-cpp17/pack_sizeof_and_print.cpp"
  description="sizeof...(args) 返回包里元素个数(编译期常量);模式展开 print_one(args)... 对每个元素独立调用,每个元素的类型独立推导。"
/>

运行结果:

```text
sizeof... 数包大小:
  pack_size():             0
  pack_size(1):            1
  pack_size(1,2.5,"hi"):  3

模式展开(异类型包,每个元素类型独立推导):
  [i] 1
  [d] 2.5
  [A3_c] hi

逗号 fold 同样效果:
  (fold 写法)
  [i] 1
  [d] 2.5
  [A3_c] hi
```

`sizeof...(args)` 返回的是 `size_t`,而且是个常量表达式,所以 `static_assert(pack_size(1, 2, 3) == 3)` 能在编译期过。有了个数,咱们就能进到展开。

## 模式展开:对每个元素做同一件事

模式展开(pattern expansion)是参数包最核心的机制,它和 fold 是两码事,很多人混在一起,这里咱们把它们分清楚。

什么叫模式展开?当您写 `f(args)...`,这里的 `f(args)` 就是一个**模式**(pattern),省略号 `...` 的意思是「把这个模式对 `args` 包里的每一个元素各做一次,再把结果依次拼起来」。编译器把 `print_one(args)...`(假设 `args` 有三个元素 `a0, a1, a2`)展开成 `print_one(a0), print_one(a1), print_one(a2)`。每个元素是**独立**展开的,它就是靠这点处理异类型包的:`print_one` 是个函数模板,对 `a0` 实例化出 `print_one<int>`、对 `a1` 实例化出 `print_one<double>`,每个元素的类型各自推导,互不相干。

`fold` 不一样。`(args + ...)` 是把整包用一个运算符折叠起来,它要求所有元素能塞进同一个表达式。模式展开的产物是「一串独立的调用」,fold 的产物是「一个值」。上面示例里两种写法效果一样,但机制不同:`print_one(args)...` 是三个独立调用,逗号 fold `(print_one(args), ...)` 是用逗号运算符把三个调用拼成一个表达式。

这里有个初学者常撞的墙:**模式展开不能直接写在语句位置**。您可能会想当然写成:

```cpp
template <typename... Ts>
void print_all(Ts... args) {
    print_one(args)...;   // 编不过
}
```

GCC 16.1.1 吐的是:

```text
error: expected ';' before '...' token
   2 |     print_all(Ts... args) {
note: parameter packs not expanded with '...':
   3 |     print_one(args)...;
```

省略号得贴在一个合法的上下文里:函数实参列表、初始化列表、逗号表达式、基类列表、模板实参列表。C++11 时代最常见的搭法是把模式塞进一个数组初始化列表,靠逗号表达式把副作用串起来:

```cpp
template <typename... Ts>
void print_all(const Ts&... args) {
    using expand_t = int[];
    (void)expand_t{0, (print_one(args), 0)...};   // 老办法
}
```

这段看着绕,原理其实朴素。`(print_one(args), 0)` 是个逗号表达式,先执行 `print_one(args)` 打印,再求值成 `0`。整个 `{0, (print_one(args), 0)...}` 展开成 `{0, (print_one(a0), 0), (print_one(a1), 0), (print_one(a2), 0)}`,初始化一个临时 `int[]` 数组,副作用就是挨个打印。开头的那个 `0` 是为了防止空包(空包会让数组变成零大小,不合法)。`(void)` 是告诉编译器「我知道这个数组没人用,别警告」。

到了 C++17,逗号 fold 把这段收得干干净净:`(void)((print_one(args), ...));` 一行就完事。所以今天写新代码,模式展开的活儿大多交给 fold。但您读老库、读 C++11 时代的代码,数组初始化那套到处都是,得认得出来。

## 模式展开能出现在哪

模式展开能出现的位置有一份固定清单,记下这张表就够:

| 位置 | 写法例子 | 展开成什么 |
|---|---|---|
| 函数实参列表 | `f(args)...` | `f(a0), f(a1), ...` 一串实参 |
| 初始化列表 | `{args...}` 或 `{f(args)...}` | 一串初始化元素 |
| 逗号表达式 / fold | `(f(args), ...)` | 一个用逗号拼起来的表达式 |
| 基类列表 | `struct D : Bases... {};` | 一串基类 |
| 模板实参列表 | `std::tuple<Ts...>` | 一串模板实参 |
| lambda 捕获 | `[args...] {}` | 捕获整包 |

最后那条特别有用:完美转发的包就是模式展开用在函数实参列表里,咱们马上看。

## 三种展开写法对照

讲了机制,咱们落到一个具体需求上:对任意个参数求和。同一个需求,C++11、C++17 给了三种差别很大的写法,把它们摆在一起看,您能清楚看到这套机制是怎么演进的。

**写法一:模板递归 + 终止重载(C++11)**。这是 variadic 模板最早的标准姿势。靠两个函数模板:一个递归版剥掉首参数,一个终止版匹配「只剩一个参数」的情况。

```cpp
template <typename T>
constexpr T sum_rec(T first) {
    return first;   // 终止:只剩一个,直接返回
}

template <typename T, typename... Rest>
constexpr T sum_rec(T first, Rest... rest) {
    return first + sum_rec(rest...);   // 剥首参数,对剩下的递归
}
```

`sum_rec(1, 2, 3)` 的调用链是:`sum_rec(1, 2, 3)` → `1 + sum_rec(2, 3)` → `1 + (2 + sum_rec(3))`,最后那个 `sum_rec(3)` 匹配单参数的终止重载,递归才回溯回来。这种写法能跑,但要写两个函数,样板重,而且空包没法处理(空包既不匹配单参数重载,也不匹配多参数重载,编译失败)。

**写法二:`if constexpr` 终止(C++17)**。上一篇咱们刚学的 `if constexpr` 正好用上。一个函数体里用编译期分支判 `sizeof...(rest) == 0` 当终止条件,递归版和终止版合二为一。

```cpp
namespace detail {
template <typename T, typename... Rest>
constexpr auto sum_ifc_impl(T first, Rest... rest) {
    if constexpr (sizeof...(rest) == 0) {
        return first;   // 编译期分支:剩零个就终止
    } else {
        return first + sum_ifc_impl(rest...);
    }
}
}   // namespace detail

template <typename... Args>
constexpr auto sum_ifc(Args... args) {
    if constexpr (sizeof...(args) == 0) {
        return 0;   // 外层处理空包
    } else {
        return detail::sum_ifc_impl(args...);
    }
}
```

写法二比写法一干净:一个函数体里既有递归又有终止,不用再单独写一个终止重载。更关键的是,外层那个 `if constexpr (sizeof...(args) == 0)` 让它能处理空包(写法一处理不了)。代价是 helper 要拆一层,因为 `if constexpr` 内部要分别访问「首参数」和「剩下的包」,而空包根本没有首参数可取。

**写法三:fold(C++17)**。一行收掉,连递归都不用。

```cpp
template <typename... Ts>
constexpr auto sum_fold(Ts... ts) {
    return (ts + ...);   // 一元右折叠
}
```

`(ts + ...)` 把整包用 `+` 折叠起来。fold 的四种形式上一篇咱们在 vol3-04 详细讲过,这里不重复,只说一句:fold 是模式展开的一个特例——它把一串模式用运算符拼成一个表达式,而不像通用模式展开那样产出「一串独立调用」。

三种写法跑出来的结果完全一致:

<OnlineCompilerDemo allow-run
  title="三种求和写法对照:递归+终止重载 / if constexpr 终止 / fold"
  source-path="code/examples/vol4/vol2-modern-cpp17/recursion_vs_ifconstexpr_vs_fold.cpp"
  description="同一个求和需求的三种写法,sum_rec / sum_ifc / sum_fold 结果一致;写法二能处理空包,写法一和写法三对空包都不好使。"
/>

运行结果:

```text
sum_rec(1,2,3,4,5):  15
sum_ifc(1,2,3,4,5):  15
sum_fold(1,2,3,4,5): 15

空包处理:
  sum_ifc(): 0   (if constexpr 终止,空包返回 0)

static_assert 全过:三种写法结果一致
```

怎么挑?新代码默认 fold,它最短最直接,绝大多数「对整包做一个二元运算」的需求都能用。需要逐个元素做不同的事(比如每个元素调一个类型相关的函数),用模式展开配逗号 fold。需要处理空包、或者终止逻辑不只是「返回初值」这么简单,用 `if constexpr`。写法一的递归 + 终止重载今天基本不写了,但读 C++11 时代的库(包括标准库的不少实现)仍是主流,得会读。

## 完美转发的包

模式展开最日常、也最出彩的用法之一,是完美转发的包。下一篇咱们会专门讲 forwarding reference 和引用折叠的机制,这里先看包这一侧长什么样。

`std::make_unique<T>(args...)` 是经典例子:它要接收任意个实参,把它们的值类别原样转发给 `T` 的构造函数。lvalue 进来得是 lvalue 出去,rvalue 进来得是 rvalue 出去,不能在转发过程中悄悄变成拷贝。`std::forward<Args>(args)...` 就是干这件事的模式展开:

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make_tracked(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

`std::forward<Args>(args)...` 展开成 `std::forward<A0>(a0), std::forward<A1>(a1), ...`。每个实参按它自己的模板参数 `Ai` 独立 forward,这是模式展开「每个元素独立」特性的直接收益:一包实参里可以有的 lvalue、有的 rvalue,各自保持自己的值类别,互不干扰。这里有个容易看走眼的地方:`Args&&...` 里的 `&&` 是 forwarding reference(转发引用),和普通的 rvalue 引用不是一回事,它得配合 `std::forward` 才能保住值类别。下一篇咱们把这个机制讲透。

<OnlineCompilerDemo allow-run
  title="std::forward<Args>(args)... 转发包,lvalue/rvalue 各自保持值类别"
  source-path="code/examples/vol4/vol2-modern-cpp17/forward_pack.cpp"
  description="relay 接收任意个 forwarding reference,sink 有 const& 和 && 两个重载;传 lvalue 走 const& 重载,传 rvalue 走 && 重载。make_tracked 工厂转发给 string 构造函数。"
/>

运行结果:

```text
传一个具名对象(lvalue):
  Tracked() 默认构造
  -> sink(const Tracked&) 收到 lvalue

传一个临时对象(rvalue):
  Tracked() 默认构造
  -> sink(Tracked&&)      收到 rvalue

工厂转发给 string 构造函数("hello", 2):取前 2 个字符
  *p = "he"
```

具名对象 `t` 是 lvalue,经过 `std::forward` 转给 `sink` 走 `const Tracked&` 重载;临时对象 `Tracked{}` 是 rvalue,转过去走 `Tracked&&` 重载。值类别没丢,这就是 `std::forward<Args>(args)...` 这个模式展开在干的事。

## 空包的坑:fold 会炸,if constexpr 不会

最后讲一个反直觉的坑,它正好接上上一篇的内容。

一元 fold 对**空包**是 ill-formed。`(ts + ...)` 在 `ts` 没有任何元素的时候,编译器没法把「什么都没有」用 `+` 折叠起来,标准规定这种情况是编译错误。GCC 的报错相当点名:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

标准只给三种运算符开了后门:`&&` 一元 fold 对空包是 `true`、`||` 是 `false`、逗号是 `void()`。别的运算符(包括 `+`、`*`、`|`)对空包都没默认值,一律 ill-formed。这条规则上一篇 vol3-04 讲 fold 四种形式时提过,这里咱们看到它在实参侧的真实后果。

但 `if constexpr` 终止的递归对空包没事。因为外层那个 `if constexpr (sizeof...(args) == 0)` 在编译期就把空包引到「返回 0」的分支去了,压根不会走到任何 fold。这就是写法二比写法三多出来的能力:它能优雅地处理零个参数的情况。

<OnlineCompilerDemo allow-run
  title="空包的坑:默认演示 if constexpr 终止能编,-DEMPTY_FOLD 复现 fold 空包报错"
  source-path="code/examples/vol4/vol2-modern-cpp17/empty_pack_pitfall.cpp"
  description="默认 sum_ifc() 空包走 if constexpr 终止分支能编过;加 -DEMPTY_FOLD 切换到一元 fold 版本,空包调用触发 fold of empty expansion 编译报错。"
/>

运行结果(默认,if constexpr 终止):

```text
sum_ifc():      0   (空包走 if constexpr 终止分支)
sum_ifc(1,2,3): 6

默认演示通过。加 -DEMPTY_FOLD 复现空包 fold 的编译报错。
```

加 `-DEMPTY_FOLD` 切到 fold 版本,空包调用编译失败:

```text
error: fold of empty expansion over operator+
   return (ts + ...);   // 空包调用时会编译失败
```

所以您写 variadic 函数的时候,如果调用方有可能传零个参数,fold 就不是首选,得用 `if constexpr` 加一个空包分支,或者用带初值的二元 fold `(0 + ... + ts)`(初值 `0` 让空包有东西可折叠)。这条坑在写通用库的时候特别容易撞上,因为「能不能被空包调用」往往是调用方的事,不是您能控制的。

---

可变参数模板的核心就这些:参数包声明、`sizeof...` 数个数、模式展开逐个处理、fold 折叠、空包的边界。真正让这套机制发光的是完美转发——`std::forward<Args>(args)...` 这个模式展开是 `make_unique`、`emplace_back`、`tuple` 构造这些标准库设施的基石。下一篇咱们就钻进完美转发的内部,看 `Args&&` 这个 forwarding reference 凭什么能保住值类别,以及它背后的引用折叠规则。
