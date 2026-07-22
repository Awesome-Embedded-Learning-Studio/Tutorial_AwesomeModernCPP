---
chapter: 13
cpp_standard:
- 20
description: concept 是命名的编译期谓词,把「这个模板参数必须是什么样」从 enable_if 的黑魔法里拎出来写进签名。讲清它的四种语法形式、和 enable_if 的报错信息对比、标准库常用 concept
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 类模板:成员、依赖名与惰性实例化
- 别名模板与 using 声明:给类型起个短名字
reading_time_minutes: 13
related:
- 使用 Concepts 约束模板:subsumption 与重载
- Requires 表达式深度解析:四种成分
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: Concepts:把模板约束写进签名
---
# Concepts:把模板约束写进签名

卷一咱们写过函数模板,知道 `template <typename T> T add(T a, T b)` 对 `int`、`double` 都能工作。可一旦您想规定「`add` 只接受数值类型,字符串别来凑」,在 C++17 及以前的日子,标准答案是 `std::enable_if`,把约束塞进一个额外的默认模板参数里,靠 SFINAE 在替换失败时把这个重载踢出去。这套机制能跑,但它把一个简单的要求埋进了模板参数的层层套娃,更要命的是报错信息——出问题的时候,编译器吐出来的是一堆 `enable_if<false, void>` 的内部展开,您得先懂 SFINAE 才能猜出到底哪条没满足。

C++20 的 concepts 解决的就是这件事。它让您**把约束命名出来、写进签名**,像写类型一样写要求,编译器也终于能用「约束没满足」这种人说的话报错。这一篇讲清 concept 到底是什么、它有几种写法、为什么它的报错信息能让您少掉一半头发,以及标准库 `<concepts>` 里那些现成能用的概念。

## concept 是什么:一个命名的编译期谓词

concept 本质上是一个**在编译期求值为 bool 的谓词**,只不过您给它起了个名字。名字是关键——有了名字,它就能在签名里出现、在报错里被点名、在多个模板之间复用。

```cpp
#include <concepts>

// 定义一个 concept:只要 T 是整数或浮点数,就是「数值」
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;
```

`Numeric<T>` 在编译期要么算出 `true`,要么算出 `false`。它本身不产生任何代码,只是一个带名字的判断。一旦有了这个名字,您就能在模板参数列表里直接用它当约束:

```cpp
template <Numeric T>
T add(T a, T b) {
    return a + b;
}
```

`template <Numeric T>` 这一行读起来就是「`T` 是个 `Numeric` 类型」。约束从「藏在默认模板参数里的黑魔法」变成了「写在类型位置上的明文要求」。这就是 concept 最核心的价值:它给约束一个名字,让要求变得可读、可复用、可被编译器引用。

## 四种语法形式

concept 写出来之后,在模板里有四种地方能用上它。咱们同一个 `add` 函数,用四种方式各写一遍,都能编译运行。

```cpp
#include <concepts>
#include <iostream>
#include <string>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// 形式①:把 concept 直接当约束写在模板参数列表里
template <Numeric T>
T form1(T a, T b) { return a + b; }

// 形式②:requires 子句(trailing requires-clause),写在参数列表之后
template <typename T>
    requires Numeric<T>
T form2(T a, T b) { return a + b; }

// 形式③:简写模板语法(constrained auto),auto 前面跟约束
auto form3(Numeric auto a, Numeric auto b) { return a + b; }

// 形式④:模板参数列表后跟 requires,内层是一个 requires 表达式
template <typename T>
    requires requires(T x) { x + x; }
T form4(T a, T b) { return a + b; }
```

形式①最直观,适合「约束就是现成的 concept」的情况。形式②`requires 子句`更灵活,后面会看到它能组合多个条件。形式③是 C++20 的简写语法,写起来像普通函数,`Numeric auto` 等价于一个带约束的模板参数。形式④里出现了两个连着的 `requires`,外层是子句、内层是表达式(下一篇详拆),这种写法不需要预先定义 concept,当场把要求描述出来。

跑一下,四种形式各调一次:

```bash
$ g++ -std=c++20 -Wall -Wextra four_forms.cpp -o four_forms && ./four_forms
form1: 8
form2: 5
form3: 30
form4: ab
```

四种写法,四种调用,全部按预期返回。形式④接收 `std::string` 也没问题,因为 `string` 有 `operator+`,内层那个 `requires(T x) { x + x; }` 对它成立。

## 报错信息的对比:为什么 concept 让人少掉头发

光说「报错更好」是空的,咱们跑跑看。同一个 `add`,分别用 `enable_if` 和 concept 约束成「只接受数值类型」,然后故意用 `std::string` 去调它,看编译器各吐什么。

先看 `enable_if` 版的报错(节选):

```text
add_enable_if.cpp:13:8: error: no matching function for call to 'add(std::string&, std::string&)'
   13 |     add(s1, s2);
      |     ~~~^~~~~~~~
  • candidate 1: 'template<class T, class> T add(T, T)'
      • template argument deduction/substitution failed:
        • /usr/include/c++/16/type_traits: In substitution of
          'template<bool _Cond, class _Tp> using std::enable_if_t = ... [with bool _Cond = false; _Tp = void]':
        • error: no type named 'type' in 'struct std::enable_if<false, void>'
```

报错的核心是最后那行 `no type named 'type' in 'struct std::enable_if<false, void>'`。您得知道 `enable_if<false>` 里没有 `type` 这个成员类型,还得知道这是 SFINAE 在替换失败时把这个重载踢出去的表现,才能倒推出「哦,原来是因为 `string` 不是数值类型」。报错信息通篇在讲 `enable_if` 的内部机制,唯独没提您真正关心的那件事:`string` 到底不满足什么。

再看 concept 版的报错(节选):

```text
add_concept.cpp:16:8: error: no matching function for call to 'add(std::string&, std::string&)'
  • candidate 1: 'template<class T>  requires  Numeric<T> T add(T, T)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • required for the satisfaction of 'Numeric<T>'
              [with T = std::__cxx11::basic_string<char>]
            concept Numeric = std::integral<T> || std::floating_point<T>;
```

关键差别在 `constraints not satisfied` 和 `required for the satisfaction of 'Numeric<T>' [with T = ... basic_string<char>]` 这两句。编译器直接告诉您:`string` 没能满足 `Numeric` 这个约束。约束的名字被点出来了,失败的具体类型也被代进去了。您不用懂 SFINAE,不用去读 `enable_if` 的展开,一眼就知道是哪条规矩没过。

::: warning 别拿行数当唯一标准
在新一点的 GCC(咱们这里用的是 16.1.1)上,两版报错的行数其实差不多——编译器把 `enable_if` 的诊断也优化得结构化了。所以 concept 的优势不在「报错短了几十行」这种老黄历上,而在**信息直接指向约束本身**。您要的是「string 不是 Numeric」,不是「enable_if<false> 没有 type」。换到老一点的编译器(比如 GCC 9/10),行数差距会非常夸张,这也是当年推 concepts 的主要动力之一。
:::

报错可读性这一条,是 concept 最直接的收益。您写库给别人用,别人传错类型时看到的不再是天书,而是一句「Numeric 约束没满足」。

## 标准库 `<concepts>` 里现成的概念

不用每次都自己造 concept。标准库 `<concepts>` 提供了一批常用概念,覆盖了类型关系、可构造性、可转换性这些高频场景。挑几个最常碰到的,实际跑一下看它们的判断:

```cpp
#include <concepts>
#include <iostream>
#include <string>
#include <vector>

struct Base {};
struct Derived : Base {};
struct Unrelated {};

int main() {
    std::cout << std::boolalpha;
    std::cout << "same_as<int,int>:           " << std::same_as<int, int> << "\n";
    std::cout << "same_as<int, const int>:    " << std::same_as<int, const int> << "\n";
    std::cout << "convertible_to<int,double>: " << std::convertible_to<int, double> << "\n";
    std::cout << "derived_from<Derived,Base>: " << std::derived_from<Derived, Base> << "\n";
    std::cout << "derived_from<Unrelated,Base>: " << std::derived_from<Unrelated, Base> << "\n";
    std::cout << "common_with<int,double>:    " << std::common_with<int, double> << "\n";
    std::cout << "integral<bool>:            " << std::integral<bool> << "\n";
    std::cout << "floating_point<float>:     " << std::floating_point<float> << "\n";
}
```

```bash
$ g++ -std=c++20 -Wall -Wextra stdconcepts.cpp -o stdconcepts && ./stdconcepts
same_as<int,int>:           true
same_as<int, const int>:    false
convertible_to<int,double>: true
derived_from<Derived,Base>: true
derived_from<Unrelated,Base>: false
common_with<int,double>:    true
integral<bool>:            true
floating_point<float>:     true
```

这里有个容易踩的点。`same_as<int, const int>` 跑出来是 **false**,而您的直觉可能觉得「不都是 int 嘛」。原因是 `const` 限定让它们成为两个不同的类型,`std::is_same_v<int, const int>` 本来就是 false,`same_as` 建立在它之上,自然也是 false。如果您想判断「剥掉 cv 限定和引用之后是不是同一个类型」,得先用 `std::remove_cvref_t` 把限定擦掉再比。

`derived_from` 顺带检查了继承的**可达性**(public 继承才算),私有继承的基类这里会返回 false。`convertible_to` 允许隐式窄化,所以 `int` 到 `double`(提升)和 `double` 到 `int`(窄化)都是 true。标准库没有现成的「禁止窄化」概念,做严格的数值类型检查时,要么接受这种宽松行为,要么自己用 `std::is_convertible_v` 配合一个禁窄化的 trait 拦一下。`integral<bool>` 是 true,因为 `bool` 在标准里属于整数类型族,写数值算法时要不要把 `bool` 算进去,得您自己用 `std::integral && !std::same_as<T, bool>` 拦一下。

这些现成的 concept 基本能覆盖日常九成的类型判断需求。真正需要自己写 concept 的场景,往往是「我这个算法要求类型提供某些操作」,比如「有 `size()` 方法」「能用 `<` 比较」「有 `value_type` 内嵌类型」——这些下篇讲约束、再下篇讲 requires 表达式时,会反复用到。

## concept 是个 bool,也能直接拿来判断

concept 归根到底是个编译期的 bool,所以它不只能写在签名里,也能在 `static_assert`、`if constexpr` 这些需要编译期判断的地方直接用:

```cpp
template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

static_assert(Numeric<int>);        // 编译期断言:int 是数值
static_assert(!Numeric<std::string>); // string 不是,断言它「不是」

template <typename T>
void describe(T x) {
    if constexpr (Numeric<T>) {
        // 编译期分支:T 是数值类型时才编译这段
    }
}
```

这一点在下一篇讲「怎么用 concept 约束模板、做重载分派」时会变成主角——有了能当 bool 用的约束,基于约束的函数重载才有了基础。

下一篇咱们往深一层走:concept 不只是「写在签名里好看」,它还会真正参与**重载解析**。多个带不同约束的重载放在一起时,编译器靠一种叫 subsumption(约束蕴含)的规则挑出最合适的那一个,这才是 concept 改变泛型代码写法的关键一环。
