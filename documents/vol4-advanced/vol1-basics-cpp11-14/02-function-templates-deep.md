---
chapter: 12
cpp_standard:
- 11
- 14
- 17
description: 从写库的视角重看函数模板:包含模型为什么逼着模板写进头文件、显式实例化和 extern template 怎么控制代码膨胀,以及那个经典坑——函数模板不能偏特化,得用重载绕
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 卷一 · 函数模板
- 模板导论:从一份代码配方说起
reading_time_minutes: 12
related:
- 类模板:成员、依赖名与惰性实例化
- 模板特化与偏特化:模式匹配的艺术
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
title: 函数模板深化:编译模型与那个不能偏特化的坑
---
# 函数模板深化:编译模型与那个不能偏特化的坑

卷一咱们写过函数模板,知道语法、实例化、推导、特化、重载那一套。这一篇换个视角,从「想拿模板写个库」的角度重新看函数模板,有三件事绕不开:模板的编译模型为什么跟普通函数不一样,显式实例化和 `extern template` 怎么帮您控制代码膨胀,以及一个能让人盯屏幕半天的坑——函数模板不能偏特化。把这三件讲透,您再看 STL、看 Eigen 那种重模板库的源码,就能看懂他们为什么那么组织代码。

## 包含模型:为什么模板都得写在头文件

先回忆普通函数怎么分文件。声明放头文件 `add.h`,实现放 `add.cpp`,别处 `#include "add.h"` 就能用。编译器在调用点只看到声明,链接器再把调用绑到 `add.cpp` 里那份实现上。这套叫**分离编译**,是 C/C++ 的老传统。

模板不按这套来。模板用的是**包含模型**(inclusion model):模板的定义必须在它被实例化的地方完整可见。您不能把函数模板的声明放头文件、定义放 `.cpp`,然后在别的翻译单元(translation unit, TU)里用它。原因是模板实例化发生在编译期,编译器要在调用点把 `T` 替换成具体类型、当场生成代码,这个生成过程需要看到完整的模板定义。

```cpp
// add.h —— 只放声明,行不行?
template <typename T>
T add(T a, T b);   // 只有声明
```

```cpp
// add.cpp —— 放定义
template <typename T>
T add(T a, T b) { return a + b; }
```

```cpp
// main.cpp
#include "add.h"
int main() {
    return add(1, 2);   // 链接报错!undefined reference to `add<int>(int, int)`
}
```

上面这套编译能过,链接会挂:`main.cpp` 里编译器看不到 `add` 的定义,没法实例化 `add<int>`,留下的只是一个未解析的符号引用;而 `add.cpp` 里因为没人「用到」`add`,编译器根本没实例化任何版本。两边对不上,链接器报 undefined reference。

解决办法很朴素,把定义搬回头文件:

```cpp
// add.h —— 定义也在头文件里
template <typename T>
T add(T a, T b) { return a + b; }
```

这就是为什么几乎所有模板库都是头文件库,boost 是典型。代价咱们上一篇说过:每个 `#include` 它的翻译单元都要重新解析一遍模板定义,大项目编译起来就很慢。

C++98 委员会其实想过治这个,搞了个 `export template` 想给模板也整一套分离编译,结果只有 EDG 一家实现,主流编译器全部放鸽子,C++11 干脆把 `export` 删了。所以到今天,包含模型还是模板的唯一现实选择。下一节讲的显式实例化,是包含模型下「省一点」的手段,不是真的分离编译。

## 显式实例化:手动控制实例化点

默认情况下,模板是**隐式实例化**:您在哪用到 `add<int>`,编译器就在那个翻译单元里生成一份 `add<int>` 的代码。十个翻译单元都用 `add<int>`,就生成十份,链接时再合并掉重复的。合并不要钱,但生成要钱——每个翻译单元都得做一遍替换和编译,这就是慢的根源。

**显式实例化**(explicit instantiation)让您手动指定「在这里生成某个版本的代码」。

显式实例化定义,长这样,以 `template` 关键字开头,跟一个不含 `template<>` 的具体签名:

```cpp
template <typename T>
T add(T a, T b) { return a + b; }

// 显式实例化定义:在这个翻译单元里,强制生成 add<double> 的代码
template double add<double>(double, double);
```

这一行告诉编译器:不管这个翻译单元里有没有真的用到 `add<double>`,都给我生成一份。编译运行没问题:

```bash
$ g++ -std=c++17 explicit_inst.cpp -o explicit_inst && ./explicit_inst
add(1.0, 2.0) = 3
add(1, 2) = 3
```

跟它配对的是 `extern template` 声明。意思是「这个版本在别的翻译单元已经实例化了,我这儿就别再生成了,直接用别处的」:

```cpp
// 在某个 .cpp 里:显式实例化定义,真的生成代码
//   template int add<int>(int, int);

// 在其它 .cpp / .h 里:声明,不生成,链接时找别处的
extern template int add<int>(int, int);
```

咱们跑一个两翻译单元的例子,看它能不能正常链接。

```cpp
// tu_a.cpp —— 这里隐式实例化 doubler<int>(因为 call_a 用了它)
template <typename T>
T doubler(T x) { return x * 2; }
int call_a() { return doubler(21); }
```

```cpp
// tu_b.cpp —— 声明 doubler<int> 在别处已实例化,本 TU 不再生成
template <typename T>
T doubler(T x) { return x * 2; }
extern template int doubler<int>(int);
int call_b() { return doubler(21); }
```

```cpp
// main_ab.cpp
#include <iostream>
int call_a();
int call_b();
int main() {
    std::cout << "call_a=" << call_a() << " call_b=" << call_b() << "\n";
}
```

```bash
$ g++ -std=c++17 tu_a.cpp tu_b.cpp main_ab.cpp -o main_ab && ./main_ab
call_a=42 call_b=42
```

`tu_b` 里虽然也调用了 `doubler(21)`,但因为有 `extern template` 声明,它不生成 `doubler<int>` 的代码,而是等链接时用 `tu_a` 生成的那一份。链接通过,结果正确。

这套机制真正的用武之地在库作者那边。标准库就大量用了这个套路:`std::basic_string<char>`、`std::vector<int>` 这些高频组合,libstdc++ 在自己的源文件里预先显式实例化好,然后在公开头文件里对用户代码声明 `extern template`。这样成千上万个用户的翻译单元就都不用重复实例化 `std::string` 的几十个成员函数了,编译时间和二进制体积都省下来一大块。您写自己的模板库时,对几个最常用的类型组合做同样处理,效果立竿见影。

::: warning extern template 不是分离编译
容易误会的一点:`extern template` 让您「不在本 TU 生成」。它确实让使用方 TU 不必把模板的完整定义也抄进来——只要声明加上 `extern template` 声明就够(前面 `tu_b` 那个例子把完整定义写出来其实多余,删掉定义只留声明,照样能编译链接)。但它的定位仍是「减少重复实例化」,不是把声明和定义真正分开:模板的实例化点必须存在(在某 TU 显式或隐式实例化),`extern template` 只是让别的 TU 复用它。模板真正的分离编译(像普通函数那样声明在 `.h`、实现只放一个 `.cpp`),至今没有干净的解法。
:::

## 函数模板不能偏特化:那个经典坑

重头戏来了。类模板可以偏特化(partial specialization),变量模板(C++14)也可以偏特化,唯独**函数模板不能偏特化**。这不是哪个编译器的限制,是标准明确规定的。cppreference 在模板总览页写得很直白:偏特化只允许用在类模板和变量模板上。

很多人第一次撞这个墙,是想给函数模板「针对指针类型写一个特殊版本」。直觉上既然类模板能写 `template <typename T> class Foo<T*>`,函数模板照葫芦画瓢应该也行:

```cpp
template <typename T>
T identity(T x) { return x; }

// 试图为 T* 偏特化 —— 编译报错!
template <typename T>
T identity<T*>(T* x) { return *x; }
```

编译器当场拦下:

```text
fn_partial.cpp:6:3: error: non-class, non-variable partial specialization
      'identity<T*>' is not allowed
    6 | T identity<T*>(T* x) { return *x; }
      |   ^~~~~~~~~~~~
```

GCC 的措辞把规则说得很明白:只有类(class)和变量(variable)的偏特化是被允许的,函数的不行。

为什么标准要这么规定?因为函数有**重载**,重载能干偏特化想干的所有事,而且更灵活。偏特化是「针对模板参数的某种模式提供一个特化版本」,重载是「针对参数类型提供一个独立函数」。两者目标重叠,标准就让函数走重载这条路,不让它再叠加一套偏特化语法,免得语义打架。

那想给指针类型写特殊版本怎么办?用重载:

```cpp
template <typename T>
T identity(T x) { return x; }            // 通用版本

// 用重载给指针一个专门版本
template <typename T>
T identity(T* x) { return *x; }          // 指针版本

int main() {
    int v = 42;
    identity(v);        // 调 T=int 版本,返回 42
    identity(&v);       // 调 T=int 版本的指针重载,返回 42
}
```

这里第二个 `identity` 是一个新的函数模板(参数是 `T*`),不是第一个的偏特化。调用 `identity(&v)` 时,编译器做重载解析,指针版本更匹配,胜出。

要是逻辑分支更复杂,比如「指针走这条、整型走那条、其它走通用」,重载也能写,但会更啰嗦。这时候有两个更现代的工具,本卷第二部分会专门讲:`std::enable_if` 配合 SFINAE(C++11),以及 `if constexpr` 编译期分支(C++17)。前者靠「让某个重载在条件不满足时从候选里消失」来分流,后者直接在函数体里写编译期的 `if`。下面给个 `if constexpr` 的预告,您感受一下它能简洁到什么程度:

```cpp
template <typename T>
void process(T x) {
    if constexpr (std::is_pointer_v<T>) {
        std::cout << "是指针,解引用:" << *x << "\n";
    } else if constexpr (std::is_integral_v<T>) {
        std::cout << "是整数:" << x << "\n";
    } else {
        std::cout << "其它类型\n";
    }
}
```

`if constexpr` 在编译期就把不成立的分支丢掉,不会留下「解引用一个非指针」这种编译错误。这是 C++17 之后处理「函数模板针对不同类型走不同逻辑」的首选方式,把以前要靠 SFINAE 写得很别扭的代码拉回了正常人类能读的样子。

## 全特化是合法的,但要用对地方

说了半天不能偏特化,那全特化呢?**函数模板的全特化是合法的**,语法是 `template<>` 开头,所有模板参数都钉死:

```cpp
template <typename T>
const char* type_name() { return "unknown"; }

// 全特化:int 版本
template <>
const char* type_name<int>() { return "int"; }

// 全特化:double 版本
template <>
const char* type_name<double>() { return "double"; }
```

全特化有个坑要知道:它**不参与重载解析里「模板」那条路径**,而是作为一个普通函数插入候选。这意味着全特化的签名必须和主模板的某个实例化精确对应,差一点都不行。而且全特化一旦写了,它就「钉死」了那个具体版本,不会再随主模板变化。

实际工程里,函数模板的全特化用得不多。多数情况下,直接写一个普通重载(非模板函数)比写全特化更省心,因为重载的规则更直观。全特化更适合「想保留模板身份、又想针对某个类型定制实现」的场景,比如给某个函数模板特化一个 `const char*` 版本去做字符串比较。

::: warning 别在全特化上踩 ODR
全特化的定义只能出现在一个翻译单元里,否则违反一次定义规则(ODR)。如果您把全特化写在头文件里又被多个 `.cpp` 包含,链接时会报重复定义。解决办法要么把它放进单个 `.cpp`,要么写成 `inline`。主模板的实例化没有这个问题(链接器会合并重复实例化),但全特化是普通函数,不享受这个待遇。
:::

## 一个有节制的小库长什么样

把前面几节串起来,看一个写库时的典型组织方式。假设咱们要提供一个 `clamp` 函数模板,想让常用类型编译快、又不放弃泛型。

头文件放模板定义,并对最常用的类型声明 `extern template`:

```cpp
// clamp.h
template <typename T>
const T& clamp(const T& v, const T& lo, const T& hi) {
    if (v < lo) return lo;
    if (hi < v) return hi;
    return v;
}

// 预声明:int 和 double 版本在别处已实例化,包含本头文件的 TU 别再生成
extern template const int&    clamp<int>(const int&, const int&, const int&);
extern template const double& clamp<double>(const double&, const double&, const double&);
```

源文件里对这些类型做显式实例化定义:

```cpp
// clamp.cpp
#include "clamp.h"

// 真正在这里生成代码
template const int&    clamp<int>(const int&, const int&, const int&);
template const double& clamp<double>(const double&, const double&, const double&);
```

这样,所有包含 `clamp.h` 的翻译单元,用到 `clamp<int>` 时都不会自己实例化,而是链接到 `clamp.cpp` 里那份。别的类型(比如 `clamp<long>`)因为没有 `extern template` 声明,仍然按隐式实例化各自生成。常用类型省了,非常用类型也没被堵死。这就是标准库内部的典型做法,只是它处理的规模大得多。

下一篇咱们进类模板。类模板的成员函数有「惰性实例化」的脾气,模板里还有依赖名和非依赖名的区分,这两件事合在一起,决定了类模板写起来跟函数模板是两种体验。
