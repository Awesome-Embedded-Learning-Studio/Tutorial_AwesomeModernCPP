---
chapter: 13
cpp_standard:
- 20
description: concept 写进签名只是第一步,它真正改变泛型代码写法的是参与重载解析。讲清 subsumption(约束蕴含)怎么在多个带约束的重载里挑出最合适的一个,以及原子约束那条容易踩的坑
difficulty: intermediate
order: 2
platform: host
prerequisites:
- Concepts:把模板约束写进签名
reading_time_minutes: 14
related:
- Concepts:把模板约束写进签名
- Requires 表达式深度解析:四种成分
tags:
- host
- cpp-modern
- intermediate
- 模板
- 泛型
- concepts
- 类型安全
title: 使用 Concepts 约束模板:subsumption 与重载
---
# 使用 Concepts 约束模板:subsumption 与重载

上一篇咱们把 concept 写进了签名,看到了报错信息怎么从 `enable_if` 的内部展开变成一句「约束没满足」。但 concept 真正改变泛型代码写法的,是另一件事:它让模板**重载**变得可行。在 C++17 及以前,两个函数模板除非参数个数或类型明显不同,否则很容易撞车,靠 `enable_if` 做条件重载写得极其别扭。有了 concept,您可以写一堆约束各不相同的同名重载,让编译器根据实参满足哪个约束来挑。挑的规则叫 **subsumption**(约束蕴含),这是这一篇的主角。

## 先分清两个同名词:requires 子句与 requires 表达式

往下走之前,得先把 `requires` 这个词的两种用法分清,否则后面会越看越晕。

**requires 子句(requires-clause)** 出现在模板参数列表之后,作用是「给模板加一个约束条件」。上一篇形式②见过了:

```cpp
template <typename T>
    requires Numeric<T>      // 这整句是 requires 子句
T add(T a, T b) { return a + b; }
```

**requires 表达式(requires-expression)** 是一个能在编译期算出 `bool` 值的表达式,用来当场描述「类型要提供哪些操作」。下一篇会专门拆它,这里先看一眼:

```cpp
requires(T t) { t + t; t.size(); }   // 这是一个 requires 表达式,值为 bool
```

两者的区别在于:子句是给模板「立规矩」的语法位置,表达式是描述规矩内容、产生真假值的算式。子句里常常塞一个表达式,比如 `requires requires(T t){ t+t; }`(外层子句,内层表达式),这就是上一篇形式④那个连写两个 `requires` 的来历。这一篇重点讲子句怎么用、约束怎么参与重载,表达式留给下一篇。

## 约束可以用在哪

concept 的约束不止能加在自由函数模板上。函数模板、类模板、成员函数、甚至简写 `auto` 参数,都能约束。一个综合例子:

```cpp
#include <concepts>

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

// ① 函数模板
template <Numeric T>
T square(T x) { return x * x; }

// ② 类模板:只对数值类型实例化
template <Numeric T>
struct SafeNumber {
    T value;
    SafeNumber(T v) : value(v) {}
    // ③ 成员函数也能再加自己的约束
    SafeNumber& operator+=(Numeric auto other) {
        value += other;
        return *this;
    }
};

// ④ 简写语法:约束直接写在 auto 前
Numeric auto half(Numeric auto x) { return x / 2; }
```

```bash
$ g++ -std=c++20 -Wall -Wextra constraints_everywhere.cpp -o ce && ./ce
square(4) = 16
square(2.5) = 6.25
SafeNumber(3) + 4 = 7
half(10) = 5
```

四种位置都能编译。需要特别留心类模板:一旦给类模板加了约束,`SafeNumber<std::string>` 这种不满足 `Numeric` 的实例化会直接在声明处报约束失败,而不是等到用某个成员才炸。约束把「这个类只能给数值用」这件事提前到了实例化那一刻。

## subsumption:编译器靠约束蕴含挑重载

现在进入正题。咱们写两个同名重载,一个要求宽一点、一个要求窄一点,看编译器怎么选。

```cpp
#include <concepts>
#include <iostream>

template <typename T>
concept Animal = requires(T t) { t.eat(); };

template <typename T>
concept Dog = Animal<T> && requires(T t) { t.bark(); };   // Dog 比 Animal 多要一个 bark()

void describe(Animal auto) { std::cout << "an animal\n"; }   // 宽重载
void describe(Dog auto)    { std::cout << "a dog\n"; }       // 窄重载

struct Cat { void eat() {} };
struct Pup { void eat() {} void bark() {} };

int main() {
    describe(Cat{});   // Cat 只满足 Animal
    describe(Pup{});   // Pup 同时满足 Animal 和 Dog
}
```

```bash
$ g++ -std=c++20 -Wall -Wextra subsumption_basic.cpp -o sub1 && ./sub1
an animal
a dog
```

`Cat` 只满足 `Animal`,没有第二个重载可选,走宽重载。`Pup` 同时满足 `Animal` 和 `Dog`,但走的是**窄重载** `Dog`。这就是 subsumption 在起作用:`Dog` 的要求是 `Animal<T> && bark要求`,它把 `Animal` 的要求全包进去了,咱们说 **`Dog` 蕴含(subsumes)`Animal`**。两个重载都能匹配时,编译器选约束更紧、更特定的那个。

这件事在 SFINAE 时代要写成什么样?得用 `enable_if` 配合「取地址成员函数有没有」的层层探测,或者上 tag dispatch,代码量翻几倍还不一定读得懂。concept 把「谁更特定」这件事交给编译器按约束关系算,这才是它改变泛型代码的根本。

## 两个互不蕴含的约束:会歧义

subsumption 只在「一个约束严格蕴含另一个」时帮您消歧。如果两个约束彼此独立、谁也不包含谁,而某个类型同时满足两者,编译器就选不出来了。

```cpp
template <typename T> concept Swimmable = requires(T t) { t.swim(); };
template <typename T> concept Flyable   = requires(T t) { t.fly(); };

void act(Swimmable auto) { /* ... */ }
void act(Flyable auto)   { /* ... */ }

struct Duck { void swim() {} void fly() {} };   // 两个都满足

int main() { act(Duck{}); }   // 谁也不 subsume 谁
```

```text
ambiguity.cpp:12:8: error: call of overloaded 'act(Duck)' is ambiguous
```

`Swimmable` 和 `Flyable` 互不蕴含,`Duck` 同时满足,编译器没有理由偏向哪一个,直接报歧义。解决办法要么再加一个 `Duck = Swimmable<T> && Flyable<T>` 的窄重载(它同时 subsumes 两个,会被选中),要么在调用点显式写 `act<具体类型>`。关键是理解:**subsumption 只解决「有包含关系」的歧义,解决不了「平级竞争」的歧义**。

## 原子约束:决定 subsumption 的真正单位

上面 `Dog` 蕴含 `Animal` 是怎么算出来的?这就得讲**原子约束**。编译器不会看「`Dog` 和 `Animal` 这两个名字谁包含谁」,它会把约束拆成一堆最小的、不可再分的原子约束,然后看集合的包含关系。

`concept Dog = Animal<T> && bark要求` 规范化之后,原子约束集合是 `{ Animal<T>, bark要求 }`。`Animal` 的原子约束集合是 `{ Animal<T> }`。前者是后者的真超集,所以 `Dog` subsumes `Animal`。

这里有个特别容易栽的坑,咱们直接跑一个看。直觉上,「`C2 = C1<T>`」这种把一个 concept 原样赋给另一个的写法,好像 `C2` 应该比 `C1` 更特定吧?跑一下:

```cpp
template <typename T> concept C1 = std::is_integral_v<T>;
template <typename T> concept C2 = C1<T>;          // 只是换了名字

void g(C1 auto) { /* ... */ }
void g(C2 auto) { /* ... */ }

int main() { g(42); }   // int 同时满足 C1 和 C2
```

```text
atomic.cpp:14:15: error: call of overloaded 'g(int)' is ambiguous
```

歧义了。为什么?因为 `C2 = C1<T>` 规范化之后,原子约束就是 `C1<T>` 本身,和 `C1` 的原子约束**一模一样**。两个重载的约束集合相等,既不互相真包含,谁也不 subsume 谁,就退回普通歧义。换个名字不会凭空造出一个「更特定」的关系,subsumption 要的是原子约束集合的**真包含**,名字换不换不影响。

让 `C2` 真正胜出,得给它加一个 `C1` 之外的原子约束。`&&` 组合恰好干这个:

```cpp
template <typename T> concept A = requires(T t){ t.a(); };
template <typename T> concept B = requires(T t){ t.b(); };
template <typename T> concept C = A<T> && B<T>;   // 原子约束 = { A<T>, B<T> }

void f(A auto) { /* ... */ }
void f(B auto) { /* ... */ }
void f(C auto) { /* ... */ }   // C subsumes A,也 subsumes B

int main() { struct S{ void a(){} void b(){} } s; f(s); }
```

```bash
$ g++ -std=c++20 -Wall -Wextra conjunction.cpp -o conj && ./conj
C
```

`C` 的原子约束集合 `{ A<T>, B<T> }` 同时是 `{ A<T> }` 和 `{ B<T> }` 的超集,所以它 subsumes `A` 也 subsumes `B`,三选一时编译器选了 `C`。这就把 `&&` 的真实角色点清楚了:`&&` 不是「把约束拼成一个新的」,而是把两边的原子约束**并集**进同一个集合。

::: warning 约束不是看名字,是看原子
subsumption 比较的是规范化后的**原子约束集合**,不是 concept 的名字。`C2 = C1<T>` 不会让 `C2` 比 `C1` 更特定,因为它们的原子约束相同。想让一个重载胜出,得让它的原子约束集合是对方的真超集,最常用的办法就是用 `&&` 再叠一个约束。记住这一条,后面写带约束的重载族时就不会被「明明名字不一样怎么还歧义」绕进去。
:::

## 坑:别把 concept 当 is_same 用

`std::same_as` 这个概念容易把人带歪。它能写 `template <std::same_as<int> T>`,把 `T` 钉成必须恰好是 `int`。

```cpp
template <std::same_as<int> T>
void only_int(T x) { /* ... */ }

only_int(42);       // 没问题
// only_int(3.14);  // 编译失败:double 不满足 same_as<int>
```

能跑,但这通常不是好设计。如果您的函数只接受 `int`,直接写一个普通的非模板函数 `void only_int(int x)` 更清楚、更省事,还能让编译器少实例化一个模板。`same_as` 这类「类型等价」约束真正的用武之地,是在模板内部对**两个参数**之间的关系做要求,比如 `template <typename A, typename B> requires std::same_as<A, B>`,约束「A 和 B 必须是同一个类型」。拿它去把单个模板参数锁死成某个具体类型,是用错了工具。

concept 写进签名、参与重载,这两件事凑齐,泛型代码就能写得既清楚又有分派能力。下一篇咱们把 `requires` 那个最容易混的表达式形式拆透——它能当场描述「类型要提供哪些操作」,是上面所有 `requires(T t){ ... }` 写法的基础。
