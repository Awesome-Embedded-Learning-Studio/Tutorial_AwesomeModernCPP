---
title: "模板导论:从一份代码配方说起"
description: "把模板说回它本来样子:一份带占位符的代码配方。讲清它和宏、和虚函数多态的区别,以及 C++ 里函数、类、变量、别名四种模板实体各管什么"
chapter: 12
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 泛型
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
reading_time_minutes: 18
prerequisites:
  - "卷一 · 函数模板"
related:
  - "函数模板深化:编译模型与 extern template"
  - "类模板:成员、依赖名与惰性实例化"
---

# 模板导论:从一份代码配方说起

咱们在卷一已经写过函数模板,知道 `template <typename T> T max_value(T a, T b)` 这么一写,编译器就能照着给 `int`、给 `double`、给 `std::string` 各生成一份代码。这一卷咱们不再停留在「怎么用」,而是想搞明白另外几件事:模板到底是个什么东西,它凭什么能做到「写一遍、套多种类型」,以及这套机制背后的代价是什么。把这些底层细节吃透,后面无论是读 STL 源码、读 Chromium 那种重模板的工业代码,还是自己动手写库,心里都不会发虚。

## 模板到底是什么:一份带占位符的代码配方

笔者喜欢把模板想成一份**代码配方**。模板本身不是代码,而是「代码该怎么写」的说明。`T` 是配方里的占位符,意思是「这里先空着,等真正用到的时候再填」。

```cpp
template <typename T>
T max_value(T a, T b) {
    return (a > b) ? a : b;
}
```

上面这几行,编译器不会为它生成任何机器码。它只是把配方记下来。真正让机器码出现的,是**实例化**(instantiation)。当咱们写下 `max_value(3, 5)`,编译器看到 `3` 和 `5` 都是 `int`,于是照着配方抄一份,把 `T` 全部换成 `int`,得到一个真实的 `int max_value(int, int)` 函数,这一份才会被编译成机器码。

```cpp
int x = max_value(3, 5);        // T=int,编译器抄出一份 int 版本
double y = max_value(1.0, 2.0); // T=double,再抄一份 double 版本
```

这两次调用,编译器抄出了两个完全独立的函数,各自编译。效果跟咱们手写两个重载没有区别。

这里有个反直觉的点值得停一下。很多人以为模板是「运行时根据类型选一个版本」,其实它是「编译期为每种用到的类型各生成一份」。所以模板调用**没有运行时开销**:调的就是普通函数,没有虚函数表那一套分派。代价咱们待会说。

::: warning 占位符不是宏替换
有人把模板理解成「高级一点的宏替换」,这只对了一半。宏(`#define`)是预处理阶段的纯文本替换,不认类型、不认作用域,稍不留神就翻车。模板的替换发生在编译阶段,编译器清楚 `T` 是个类型,会做类型检查、重载解析、名字查找。后面讲两阶段查找的时候咱们会看到,这套机制比宏精细得多。拿「带类型检查的宏」当初步印象可以,但别停在这一步。
:::

## 为什么不是宏,也不是虚函数

「逻辑相同、类型不同」这个需求,C++ 里有好几条路能走。咱们把最常被拿来比的三条放一起看。

宏这条路,`#define max(a, b) ((a) > (b) ? (a) : (b))` 能用,但它是预处理期的纯文本替换。参数会被求值两次、不认类型、没有作用域、调试器里看不到它的影子。卷一讲函数模板的时候提过,Windows 上 `<windows.h>` 的 `max` 宏能让您血压拉满。宏能干的事,模板几乎都能干得更好。

虚函数多态这条路,把 `max` 写成虚函数,运行时根据对象的实际类型分派到对应实现。它要求所有参与类型处在同一个继承体系里,而且每次调用都要走一次虚函数表查找,有运行时开销。更要命的是,`int`、`double` 这些内置类型根本进不了您的继承体系。

模板这条路,编译期为每种用到的类型各生成一份代码,调用的是普通的、可内联的函数。没有继承体系的约束,内置类型、自定义类型一视同仁;没有运行时分派开销。

这三条路谁也不替代谁。虚函数适合「同一接口、运行时才知道具体实现」的场景,比如插件系统、GUI 事件分发;模板适合「同一份逻辑、编译期就知道类型」的场景,比如容器和算法。至于宏,基本能不用就不用。

## C++ 里有四种模板实体

很多人以为模板就是函数模板和类模板两种,其实 C++ 标准里模板能定义四种实体。cppreference 给的定义是:模板是定义一族(family)实体的 C++ 实体。

函数模板(function template)定义一族函数,`std::max`、`std::sort` 都是。类模板(class template)定义一族类,`std::vector`、`std::map` 都是。这两种从 C++98 就有,是咱们最熟的。除此之外还有两种稍新的:变量模板(variable template,C++14 起)定义一族变量或静态成员;别名模板(alias template,C++11 起)定义一族类型别名。

变量模板解决的是一个很朴素的需求:给「每个类型」配一个常量。在 C++14 之前,大家是用类模板的静态成员模拟的:

```cpp
// C++14 之前的老写法:用类模板的静态成员模拟「参数化的常量」
template <typename T>
struct pi_trait {
    static constexpr T value = T(3.1415926535897932385L);
};
double r = pi_trait<double>::value * 2.0;
```

C++14 有了变量模板,直接写成一个参数化的变量,清爽多了:

```cpp
template <typename T>
constexpr T pi = T(3.1415926535897932385L);  // 变量模板

double r = pi<double> * 2.0;  // 用起来像普通常量,只是带个 <T>
```

标准库里 `std::numeric_limits<T>::max()` 之所以是函数而不是变量,部分原因就是它诞生的时候变量模板还不存在。`std::tuple_size`、`std::extent` 这些后来都补了对应的 `_v` 变量模板版本(比如 `std::extent_v<T>`,C++17 起),就是为了让大家少写一句 `::value`。cppreference 上变量模板的特性测试宏是 `__cpp_variable_templates = 201304L`,对应 C++14。

别名模板解决的是「给一族类型起个短名字」:

```cpp
// 没有 alias template 的年代,要给 vector<T> 起别名得借助类模板的嵌套 using
template <typename T>
struct vec_alias { using type = std::vector<T>; };
typename vec_alias<int>::type v;  // 又是 typename 又是 ::type,啰嗦

// C++11 alias template,一行搞定
template <typename T>
using vec = std::vector<T>;
vec<int> v;  // 干净
```

这一卷后面会专门讲别名模板,这里咱们先有个印象:**C++ 里能被参数化的,不只是函数和类**。

## 三种模板参数

模板的「占位符」有三种,对应三种参数。

类型参数(type parameter),写作 `typename T` 或 `class T`,占位的是一个类型,最常见。非类型参数(non-type parameter),写作 `template <int N>` 这种形式,占位的是一个编译期已经知道的**值**,可以是整数、指针、引用,以及 C++20 起的浮点数和满足一定条件的类类型。模板模板参数(template template parameter),占位本身就是一个模板。

```cpp
template <typename T, std::size_t N>   // T 是类型参数,N 是非类型参数
struct array {
    T data[N];
};

template <template <typename> class Container>  // Container 是模板模板参数
struct wrapper {
    Container<int> c;
};
```

`std::array<T, N>` 是前两种配合的经典例子:`T` 是元素类型,`N` 是数组大小,`N` 必须在编译期就知道。模板模板参数平时用得少,但写高阶泛型库(比如「换一个底层容器」的策略)时会碰到。这三种参数本卷后面都会各自展开,其中非类型参数那一篇还会专门讲 C++20 对它的大幅度放宽。

## 模板是编译期图灵完备的

这件事值得单独拎出来说,因为它决定了模板能走多远。**C++ 的模板机制是图灵完备的**,也就是说,只要愿意,咱们可以在编译期用模板做任意复杂的计算,分支、循环(用递归模拟)、任何逻辑全在编译阶段完成,运行时拿到的是已经算好的结果。

一个最小的例子,编译期算阶乘:

```cpp
template <unsigned N>
struct Factorial {
    static constexpr unsigned value = N * Factorial<N - 1>::value;
};

template <>
struct Factorial<0> {              // 递归终止:0! = 1
    static constexpr unsigned value = 1;
};

template <unsigned N>
constexpr unsigned factorial_v = Factorial<N>::value;  // C++14 变量模板,顺手用上
```

跑一下,乘法全部发生在编译期:

```bash
$ g++ -std=c++14 factorial.cpp -o factorial && ./factorial
5! = 120
10! = 3628800
```

```cpp
static_assert(Factorial<5>::value == 120, "");      // 编译期就验过
static_assert(factorial_v<10> == 3628800, "");
```

`Factorial<5>::value` 在程序跑起来之前就已经是 `120` 了。这就是模板元编程(template metaprogramming, TMP)的根:把计算塞进编译期。它能做出很厉害的东西,表达式模板、编译期字符串、`constexpr` 编译期解析都建立在上面,代价是编译慢、报错难读、代码可读性差。本卷第三部分(C++20/23 元编程)会专门讲 C++ 怎么用 `concepts`、`consteval`、`if constexpr` 把 TMP 从「黑魔法」拉回「人能写的代码」。这一篇咱们只要记住一件事:**模板不只是生成代码,它本身就能在编译期算东西**。

## 代价:实例化、代码膨胀、header-only

模板不是白嫖的,它有三笔代价。

第一笔是**惰性实例化**。类模板的成员函数,只有真正被用到的那几个才会被实例化。写一个 `std::vector<Heavy>` 不会把 `vector` 的所有成员函数都实例化一遍,只实例化您实际调用的那些。这也是为什么 STL 敢把那么多功能塞进一个模板里,没用到就不付钱。

```cpp
template <typename T>
struct Demo {
    void used_function() { T t{}; /* ... */ }
    void unused_function() {
        // 这里哪怕写了 T 上根本不存在的成员,也不会报错
        // 因为这个函数从没被调用,根本不会被实例化
    }
};

int main() {
    Demo<int> d;
    d.used_function();   // 只实例化这一个
    // 没调用 unused_function(),所以它不会被实例化,里面的胡写编译器看不见
}
```

这条特性有利有弊。利是编译快、二进制小;弊是某些错误要等到真正用了才暴露,藏得深。

第二笔是**代码膨胀**。每种用到的类型各生成一份代码。`max_value` 给 `int`、`double`、`std::string` 各用一次,就是三份函数。对小函数无所谓,对大模板(比如某些 STL 算法的完整特化)累积起来会让二进制明显变大。这一卷讲函数模板深化的那一篇会讲 `extern template`,就是用来控制这种膨胀的。

第三笔是 **header-only**。模板的定义必须在它被实例化的地方可见,所以绝大多数模板库都是纯头文件库,boost 是典型例子。您不能像普通函数那样把声明放头文件、实现放 `.cpp`。这一点直接导致了 C++ 「编译慢」这个老问题:每个翻译单元都要重新解析一遍模板定义。C++20 的模块(modules)就是为了治这个病,但那是另一个大话题。

## export template:一个被放弃的尝试

说到「模板定义必须可见」,其实 C++98 标准里委员会给过一条出路,叫 `export template`。想法很美好:加了 `export` 的模板,使用者只要看到声明就能实例化,不用包含定义,从而实现模板的分离编译。

现实很骨感。整套 C++98 标准里,只有 Edison Design Group(EDG)的前端和基于它的 Comeau 编译器真正实现了 `export template`,GCC、Clang、MSVC 全部没实现。原因是这套机制实现起来极其复杂,收益又看不出多少。到了 C++11,委员会干脆把它从标准里删掉,cppreference 上对它的标注是 `(until C++11)`。

这段历史给咱们的提示是:**模板的分离编译,直到今天仍然是个没被彻底解决的问题**。`extern template` 能减少重复实例化,但不能真正把声明和定义分开;C++20 模块是另一套全新的解法,生态还在建设中。所以现阶段,模板代码基本还是得写在头文件里,这是 C++ 程序员要接受的现实。

## 这卷要走的路

这一篇咱们把模板说回了它本来样子:一份带占位符的代码配方,编译期照配方生成真实代码,零运行时开销,代价是代码膨胀和必须写在头文件里。接下来咱们从「会用」往「写库」推进。下一篇把函数模板的编译模型、`extern template`,还有「函数模板不能偏特化」这个经典坑掰开讲透;之后是类模板、特化与偏特化、非类型参数、两阶段名字查找、友元注入、别名模板,最后用 CRTP 收口静态多态,再用一个 `fixed_vector` 综合项目把前面学的全串起来。

读到这儿,您应该对模板「是什么」有了底气。接下来的篇幅,咱们把它「为什么」一件件讲透。
