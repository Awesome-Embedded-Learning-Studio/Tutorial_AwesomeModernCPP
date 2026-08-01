---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 模板里的 T&& 其实是转发引用,左值右值都能绑,和普通的右值引用是两码事。讲透转发引用、引用折叠四条规则、std::forward 条件转发的机制,以及 std::move 与 std::forward 的边界
difficulty: intermediate
order: 3
platform: host
prerequisites:
- '可变参数模板:参数包的展开'
- '移动语义与右值引用'
reading_time_minutes: 14
related:
- '可变参数模板:参数包的展开'
- 'CTAD:类模板参数推导'
tags:
- host
- cpp-modern
- intermediate
- 移动语义
- 泛型
- 模板
title: '完美转发:forwarding references 与引用折叠'
---
# 完美转发:forwarding references 与引用折叠

上一篇咱们看了 `if constexpr` 怎么在编译期挑分支,把一摞重载拍扁。那篇处理的是「同一个模板里按类型走不同实现」。这一篇换一个方向:泛型代码里把实参原样往里传。写一个 `make_unique`、写一个透明的包装器,笔者都希望外层收到左值就把左值传进去、收到右值就把右值传进去,中间一步不能偷偷改掉值类别。这件事看着简单,实际有一个叫「完美转发」的机制在底下撑着,它的两块基石都特别容易被误读。

这两块基石一个叫**转发引用(forwarding reference)**,一个叫**引用折叠(reference collapsing)**。很多教程把 `T&&` 直接叫成「右值引用」,这个说法在模板里是错的,会让您对一整套泛型代码的意图产生误判。咱们这一篇就把它拆开:为什么 `T&&` 不是右值引用、引用折叠的四条规则长什么样、`std::forward` 到底在转什么、以及它在泛型代码里和 `std::move` 的那条边界。

## `T&&` 长得像右值引用,但它不是

先从一个最小的例子入手。下面这个模板函数,参数写成 `T&&`:

```cpp
template <typename T>
void show(T&& x);
```

这个 `T&&` 是右值引用吗?看起来和 `int&&` 一个模样。咱们把左值、右值、`std::move` 后的值都传进去,看 `T` 到底推成什么:

```cpp
template <typename T>
void show(T&& x) {
    std::cout << "  T 是否左值引用:" << std::is_lvalue_reference_v<T>
              << "  T 是否右值引用:" << std::is_rvalue_reference_v<T> << "\n";
    using X = decltype(x);
    std::cout << "  x 是否左值引用:" << std::is_lvalue_reference_v<X>
              << "  x 是否右值引用:" << std::is_rvalue_reference_v<X> << "\n";
}

int a = 10;
show(a);              // 传左值
show(20);             // 传右值
show(std::move(a));   // 传右值
```

<OnlineCompilerDemo allow-run
  title="转发引用 T&& 同时绑左值右值,T 的推导结果随实参值类别变化"
  source-path="code/examples/vol4/vol2-modern-cpp17/forwarding_reference_deduce.cpp"
  description="传左值时 T 推成 int&,x 类型 int&;传右值时 T 推成 int,x 类型 int&&。同一个 T&& 形参,绑了两种截然不同的东西。"
/>

运行结果:

```text
传左值 a:
  T 是否左值引用:1  T 是否右值引用:0
  x 是否左值引用:1  x 是否右值引用:0
传右值 20:
  T 是否左值引用:0  T 是否右值引用:0
  x 是否左值引用:0  x 是否右值引用:1
传 std::move(a):
  T 是否左值引用:0  T 是否右值引用:0
  x 是否左值引用:0  x 是否右值引用:1
```

看清楚了:`T&&` 这个形参,左值能传进来,右值也能传进来。传左值 `a` 时,`T` 被推成了 `int&`(一个左值引用);传右值 `20` 时,`T` 被推成了 `int`(一个非引用类型)。同一个语法 `T&&`,绑了两种截然不同的东西,这就是它和右值引用最根本的区别。

真正的右值引用 `int&&` 只能绑右值,绑左值要挨编译错。但模板里的 `T&&` 不一样,它由一条特殊规则赋予了一种「左右通吃」的能力。C++ 标准给它起了一个专门的名字,**转发引用(forwarding reference)**,也有人沿用 Scott Meyers 的旧称叫**通用引用(universal reference)**。两条规则成立时它才是转发引用:一是发生在模板参数推导的上下文里,二是形参的形式必须严格是 `T&&`(或 `auto&&`),`T` 是该上下文里正在推导的那个模板参数。形式稍有偏离,就不是转发引用,下一节咱们就看到一个反例。

那 `T&&` 怎么做到左右通吃?靠的就是引用折叠。

## 引用折叠:四条规则凑出一个「左右通吃」

推导出 `T = int&` 之后,把 `T` 代回形参 `T&&`,就得到了 `int& &&` 这种写法。「引用的引用」在 C++ 里不允许直接写(`int& && x` 是语法错),但模板推导和几个特定场景里会**产生**这种组合,标准用一套叫引用折叠的规则来收掉它。规则一共四条:

| 组合 | 折叠结果 |
|---|---|
| `T& &`    | `T&`  |
| `T& &&`   | `T&`  |
| `T&& &`   | `T&`  |
| `T&& &&`  | `T&&` |

记忆口诀只有一句:**只要其中一个是左值引用(`&`),结果就是左值引用;两个都是右值引用(`&&`),结果才是右值引用**。

回看上面的推导。传左值 `a`,`T` 推成 `int&`,形参 `T&&` 即 `int& &&`,按表折叠成 `int&`,所以 `x` 是个左值引用,`is_lvalue_reference_v<decltype(x)>` 是 1。传右值 `20`,`T` 推成 `int`,形参 `T&&` 即 `int&&`(没有引用可折叠),`x` 是右值引用,`is_rvalue_reference_v<decltype(x)>` 是 1。咱们跑一段最小代码,把四种折叠组合过一遍,确认表里这几行是真实的:

```cpp
template <typename T> using lref = T&;
template <typename T> using rref = T&&;

// is_lvalue_reference_v / is_rvalue_reference_v 的结果
// lref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& &  -> int&)
// rref<int&>   -> lvalue_ref=1 rvalue_ref=0   (int& && -> int&)
// lref<int&&>  -> lvalue_ref=1 rvalue_ref=0   (int&& & -> int&)
// rref<int&&>  -> lvalue_ref=0 rvalue_ref=1   (int&& &&-> int&&)
```

实测输出和表完全对得上,四种折叠里只有「右值引用套右值引用」这一种能保住右值身份,其余三种统统折成左值引用。引用折叠不只服务转发引用,`typedef`/`using`、`decltype`、`auto&&` 这些场景都会触发,是 C++ 类型系统里一条贯穿始终的规则。前面那条「转发引用左右通吃」的能力,本质上就是推导 + 折叠两步合起来达成的。

::: warning 别把 `vector<T>&&` 当转发引用
转发引用的形式要求很严格,必须是「正在推导的模板参数直接跟 `&&`」这一种写法。下面这个参数看着有 `T` 又有 `&&`,但它不是转发引用:

```cpp
template <typename T>
void takes_vec_rvalue(std::vector<T>&& v);
```

这里的 `T` 是给 `vector<T>` 推导的,参数本身已经定型成 `vector<T>` 的右值引用,不是 `T&&` 这种形式。咱们传一个左值 vector 进去,编译器直接拒收:

```text
error: cannot bind rvalue reference of type 'std::vector<int>&&' to lvalue 'std::vector<int>'
```

判断标准就一条:形参是不是恰好「`T&&`」或「`auto&&`」这种「裸露的、正在推导的参数加 `&&`」的形式。形式稍变(`vector<T>&&`、`const T&&`、`T& &&`),就退回成普通右值引用,左值传不进去。
:::

## `std::forward` 在干什么:把值类别找回来

到这里咱们有了一个左右通吃的 `T&&` 形参,但光这样还不够。`x` 这个变量名本身是个左值(任何有名字的变量都是左值,哪怕它的类型是右值引用),所以您把它直接传给下一层函数,下一层收到的是左值,右值那条重载永远走不到。咱们需要一个工具,根据 `T` 的推导结果**有条件地**把 `x` 还原回它原本的值类别。这个工具就是 `std::forward<T>(x)`。

它的工作就两条:

- 当 `T` 推成了左值引用(`int&`),`std::forward<T>(x)` 返回一个左值引用;
- 当 `T` 不是引用(`int`),`std::forward<T>(x)` 返回一个右值引用。

换句话说,`std::forward` 在根据当初推导出的 `T`,把 `x` 被「变量名是左值」这条规则吞掉的值类别,重新恢复回来。咱们写一个透明转发器,把它和被调函数的左值/右值重载放一起,看值类别是不是真的传到了底:

```cpp
void target(std::string& s)  { std::cout << "  [target] 命中左值重载:" << s << "\n"; }
void target(std::string&& s) { std::cout << "  [target] 命中右值重载:" << s << "\n"; }

template <typename T>
void wrap(T&& x) {
    target(std::forward<T>(x));
}

std::string s = "hello";
wrap(s);                 // 传左值,期望走左值重载
wrap(std::string("world")); // 传右值,期望走右值重载
wrap(std::move(s));      // 传右值
```

<OnlineCompilerDemo allow-run
  title="wrap 用 std::forward 转发,target 的左值/右值重载都正确命中"
  source-path="code/examples/vol4/vol2-modern-cpp17/wrap_with_forward.cpp"
  description="传左值时 T=int& ,forward 返回左值引用,命中左值重载;传右值时 T=string,forward 返回右值引用,命中右值重载。值类别一路保到底。"
/>

运行结果:

```text
wrap(s)                传左值:
  [target] 命中左值重载:hello
wrap(string("world")) 传右值:
  [target] 命中右值重载:world
wrap(std::move(s))     传右值:
  [target] 命中右值重载:hello
```

传左值 `s` 时,`T` 推成 `std::string&`,`std::forward<std::string&>(x)` 走第一条,返回左值引用,`target` 命中左值重载;传右值 `std::string("world")` 时,`T` 推成 `std::string`,`std::forward<std::string>(x)` 走第二条,返回右值引用,`target` 命中右值重载。这就是「完美转发」三个字的含义:**外层收到什么值类别,里层就拿到什么值类别,中间一步没改**。

这套机制最日常的舞台是泛型工厂和透明包装器。`std::make_unique<T>(args...)` 接收任意个实参,把它们原封不动地转给 `T` 的构造函数,既要支持拷贝构造(实参是左值),又要支持移动构造(实参是右值),靠的就是参数包配 `std::forward`。下一篇讲参数包展开时咱们还会再见到它。

## `std::forward` vs `std::move`:泛型代码里别用 move 转发

到这里有个边界必须讲清楚,它是新手写泛型代码最容易踩的坑。`std::move` 和 `std::forward` 都能产生右值,但它们的语义截然不同。

`std::move(x)` 是**无条件**转换:不管 `x` 原本是左值还是右值,统统转成右值引用。它的用途是您明确知道「这个对象我要搬走了」,比如您手上的变量马上要离开作用域,或者您就是想触发移动构造。`std::forward<T>(x)` 是**条件**转换:只在 `T` 不是引用时才转成右值,否则保持左值。它的用途是泛型转发,把值类别原样传下去。

如果您在泛型转发器里写了 `std::move(x)` 而不是 `std::forward<T>(x)`,会发生什么?咱们写个对照,内部维护一个持有堆内存的 `Box` 类,移动后会清空,这样能直观看出对象有没有被搬走:

```cpp
template <typename T>
void wrap_move(T&& x)    { consume(std::move(x)); }        // 无条件转右值
template <typename T>
void wrap_forward(T&& x) { consume(std::forward<T>(x)); }  // 条件转发

Box a("hello");
wrap_forward(a);   // 左值:forward 保持左值,consume 走左值重载,不搬
std::cout << a.raw();   // 仍是 "hello"

Box b("hello");
wrap_move(b);      // 左值:move 错误地转成右值,consume 走右值重载,把 b 搬空
std::cout << b.raw();   // 变成 "<空>"
```

<OnlineCompilerDemo allow-run
  title="std::move 错误转发把左值搬空,vs std::forward 保持左值完好"
  source-path="code/examples/vol4/vol2-modern-cpp17/move_vs_forward.cpp"
  description="同样的左值实参,wrap_forward 走左值重载、对象完好;wrap_move 无条件转右值、对象被搬空。这就是泛型代码里用 move 转发的破坏性。"
/>

运行结果:

```text
--- std::forward 转发左值(正确)---
  [consume] 命中左值重载(没搬走):hello
  调用方 a.raw() = hello (完好)

--- std::move 错误转发左值(破坏)---
  [consume] 右值,搬走后源对象="<空>"
  调用方 b.raw() = <空> (被搬空!)
```

差别肉眼可见。同一个左值实参,用 `std::forward` 转发,调用方手里还是完好的 `"hello"`;用 `std::move` 转发,调用方手里的对象被悄悄搬空了。在非泛型代码里,您写 `consume(std::move(local))` 是在宣告「我知道 local 接下来不要了」,这是合法且有用的。但在泛型转发里,您根本不知道调用方传进来的是左值还是右值,把左值当右值搬走,等于在调用方不知情的情况下改了他的对象,这是 silent bug,定位起来特别折磨。

判断准则一句话:**转发用 `std::forward`,您已知要搬走的本地对象用 `std::move`**。两者长得像、都返回右值引用,但语义和适用场景完全不重叠。

## 转发引用 + 重载的灾难

理解了转发引用的「贪婪」之后,看一个经典陷阱。很多人给某个具体类型写了专门的重载,顺手再加一个 `T&&` 兜底版,以为具体类型重载会被优先选中。结果是转发引用把本该主角的重载挤掉了。

咱们看一个最小例子。给 `Widget` 准备了三组重载:const 左值引用接左值,`Widget&&` 接右值,转发引用兜底别的类型:

```cpp
struct Widget { int v; };

void tag(const Widget&) { std::cout << "  命中 const Widget& 重载\n"; }
void tag(Widget&&)      { std::cout << "  命中 Widget&& 重载\n"; }
template <typename T>
void tag(T&&) {
    std::cout << "  命中 T&& 转发引用:" << __PRETTY_FUNCTION__ << "\n";
}

Widget w{1};
tag(w);            // 传左值,直觉该走 const Widget&
tag(Widget{2});    // 传右值
```

<OnlineCompilerDemo allow-run
  title="转发引用在重载决议里击败 const Widget&,抢走左值实参"
  source-path="code/examples/vol4/vol2-modern-cpp17/overload_gotcha.cpp"
  description="传左值 Widget 时,T&& 推成 Widget&,精确匹配 const Widget& 还不需要加 const,重载决议里转发引用反而胜出,把 const& 挤掉了。传右值时非模板的 Widget&& 优先。"
/>

运行结果:

```text
tag(w)        传左值(直觉该走 const Widget&):
  命中 T&& 转发引用:void tag(T&&) [with T = Widget&]
tag(Widget{2}) 传右值:
  命中 Widget&& 重载
```

传左值 `w` 时,`T&&` 推成 `Widget&`,折叠后是个左值引用,绑定到 `w` 是精确匹配;而 `const Widget&` 要绑定到非 const 的 `w` 还得加一道 const 限定(虽然这条加 const 通常免费,但重载决议看的是形式上的匹配等级)。两者都精确匹配的情况下,转发引用这个模板版本在某些关键维度上更优,结果它把 `const Widget&` 挤掉了,本该走特化重载的左值落进了兜底版本。传右值 `Widget{2}` 时,`Widget&&` 是非模板的精确匹配,优先于模板版本,所以这条还正常。

这就是转发引用的贪婪性:**它对左值实参的吸引力远超直觉**。Scott Meyers 在《Effective Modern C++》里专门留了一项(Item 26)讲这个陷阱,它最危险的形态是「完美转发构造函数」:一个类只要写了接受 `T&&` 的转发构造函数,就会盖过编译器本想生成的拷贝/移动构造,产生一堆莫名其妙的编译错。

怎么避开?几条实务做法:

1. **别让转发引用和您想特化的重载同时出现**。要么只留转发引用,在内层用 `if constexpr` 或 `tag_dispatch` 分派;要么干脆不写转发引用,用一组具名重载。
2. **用 const T& 隔开**。把兜底版本改成 `const T&`,它就不再是转发引用,贪婪性消失。代价是右值会多一次拷贝,对轻量类型可以接受。
3. **用 concepts 约束**。C++20 起,`template <typename T> requires (!std::same_as<std::decay_t<T>, Widget>) void tag(T&&)` 这种 requires 子句能把转发引用挡在具体类型之外,既保留转发能力又不抢主角。

## `auto&&` 也是转发引用

最后顺手把 `auto&&` 也讲掉,它是转发引用在非模板语境里的重要出口。`auto&&` 触发的推导和 `T&&` 完全一样,`auto` 占了模板参数 `T` 的位置,所以 `auto&&` 同样能左右通吃,同样走引用折叠。

```cpp
int a = 1;
auto&& r1 = a;              // a 是左值,auto=int&, r1 类型 int& && -> int&
auto&& r2 = std::move(a);   // 右值,auto=int, r2 类型 int&&
auto&& r3 = 42;             // 右值,auto=int, r3 类型 int&&
```

跑出来 `r1` 是左值引用,`r2`、`r3` 是右值引用,和模板版一致。`auto&&` 最常见的用法是在范围 for 和泛型 lambda 里捕获任意值类别的实参:写一个 `[](auto&& x){ ... }` 的泛型 lambda,`x` 就是转发引用,把 `std::forward<decltype(x)>(x)` 传给下一层,就能保持值类别一路到底。标准库里 `std::bind`、`std::invoke` 的内部实现,以及各种「完美转发调用器」,都靠这一套 `auto&&` + `std::forward<decltype(...)>(...)` 的组合。

## 完美转发的边界:不是 100% 能保

讲了这么多「完美」,得说清它的边界,免得您把它当万能工具。有几类实参,完美转发是转不动的:

第一类是**花括号初始化列表(braced-init-list)**。`{1, 2, 3}` 本身没有类型,模板参数推导推不出来,所以转发器收到 `{1, 2, 3}` 会直接失败:

```cpp
template <typename T, typename... Args>
void relay(Args&&... args) { auto p = std::make_unique<T>(std::forward<Args>(args)...); }

relay<std::vector<int>>({1, 2, 3});   // 编译错:{1,2,3} 推不出 Args
```

GCC 报的是 `too many arguments to function ... Args = {}`,意思就是花括号那包东西压根没参与推导,`Args` 推成了空包。解决办法是外层先建好 `std::vector<int>{1,2,3}` 这个临时对象,把它当右值传进去,而不是指望转发器替您构造。

第二类是**整型 0 当指针**。本意是给某个接受 `char*` 的函数传一个空指针,写了 `0` 或 `NULL`,模板推导会把 `0` 推成 `int`,而不是指针类型:

```cpp
auto lam = [](auto&& x) { using T = std::decay_t<decltype(x)>; /* 看 T 是什么 */ };
lam(0);    // T 推成 int,不是 char*/nullptr
```

推成 `int` 之后,转发给接受 `char*` 的目标,类型对不上,要么报错要么走到意料之外的重载。要传空指针,直接写 `nullptr`,它的类型是 `std::nullptr_t`,推导和重载决议都正确。

第三类是**位域(bitfield)**。位域成员不能绑定到非 const 引用,而转发引用推导出的常常是非 const 左值引用,因此把位域成员转发出去会编译失败。这是语言层面的限制,标准库也绕不开。

记住这三类,遇到「明明该转发过去却编不过」时就不会一头雾水。完美转发的「完美」指的是**值类别**层面零损失,不是「什么都能转」。这几类失败的根子都在推导阶段,不在转发机制本身。

下一篇咱们把完美转发和变参模板焊到一起。一个接收任意个实参的转发器,配上参数包展开,就是 `std::make_unique`、`std::emplace_back` 这一类接口的底层模板,也是您写泛型工厂的日常武器。
