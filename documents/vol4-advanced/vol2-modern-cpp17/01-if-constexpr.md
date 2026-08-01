---
chapter: 12
cpp_standard:
- 17
description: if constexpr 在编译期挑分支,丢弃的分支不实例化,让模板里按类型分派不再依赖一摞重载或偏特化
difficulty: intermediate
order: 1
platform: host
prerequisites:
- TMP 核心技巧:concepts 之前的世界
- 模板特化与偏特化:模式匹配的艺术
reading_time_minutes: 12
related:
- 可变参数模板:参数包的展开
- 完美转发:forwarding references 与引用折叠
tags:
- host
- cpp-modern
- intermediate
- if_constexpr
- 模板
- 编译期计算
title: "if constexpr:编译期分支"
---
# if constexpr:编译期分支

上一篇咱们看了 concepts 之前那套 TMP 老技巧——SFINAE、`void_t`、fold expressions。那些工具回答的是「这个类型合不合格」「在类型上算个数」。但泛型代码里还有一类特别朴素的需求:同一个模板函数,笔者想让 `int` 走这条分支、`std::string` 走那条分支、别的类型走第三条。用 SFINAE 能做,但要么写一摞重载,要么写偏特化,样板很重。C++17 给了一个轻得多的答案。`if constexpr` 能在编译期挑分支,被丢弃的分支压根不实例化。

这一篇咱们就把 `if constexpr` 的机制、它能替掉多少样板、以及它最容易被误解的那条边界讲清楚。

## 先看普通 `if` 为什么编不过

咱们从一个最小的例子入手。我想写一个 `double_it`,整数传进去返回翻倍,字符串传进去返回拼接后的结果。第一反应多半是这样:

```cpp
template <typename T>
auto double_it(T x) {
    if (std::is_integral_v<T>) {
        return x + x;          // 整数:加法
    } else {
        return x + " world";   // 字符串:拼接
    }
}
```

这份代码看着自然,但编译过不了。咱们把 `if` 换成普通 `if`,只调用 `double_it(21)` 这一处,GCC 16.1.1 吐出来的核心是这一句:

```text
error: inconsistent deduction for auto return type
   9 |         return x + " world";
     |                ~~^~~~~~~~~~
note: could be 'int'
note: or 'const char*'
```

问题不在 `is_integral_v` 判错了,也不在哪个分支真的被执行了。问题在于**普通 `if` 的两个分支都得实例化**——编译器要为 `T = int` 把整个函数体实例化一遍,包括 `else` 里那句 `x + " world"`。对 `int` 来说,前一个 `return` 推出 `auto` 是 `int`,后一个 `return` 推出 `const char*`,两个不一致,`auto` 就推导失败了。运行时只会走一条分支,但编译器两个分支都得编过,这就是普通 `if` 在模板里到处碰壁的根源。

## `if constexpr`:把不走的分支整个丢弃

把 `if` 换成 `if constexpr`,同样一份代码就编得过:

```cpp
template <typename T>
auto double_it(T x) {
    if constexpr (std::is_integral_v<T>) {
        return x + x;          // 整数走加法,返回 int
    } else {
        return x + " world";   // 别的类型走拼接,返回 string
    }
}
```

<OnlineCompilerDemo allow-run
  title="if constexpr:丢弃的分支不参与实例化,auto 不再冲突"
  source-path="code/examples/vol4/vol2-modern-cpp17/if_vs_plain_if.cpp"
  description="传 int 时 else 分支被整个丢弃不实例化,auto 返回类型只有 int 一种,不再和 const char* 冲突。"
/>

运行结果:

```text
42
hello world
```

差别就一个词:`constexpr`。`if constexpr` 的条件在编译期求值,条件为假的那条分支**不进入实例化**。换句话说,实例化 `double_it<int>` 的时候,编译器只看到 `return x + x;`,`else` 那条分支对 `int` 这个实例根本不存在。`auto` 只有一种推导结果——`int`,冲突自然就没了。实例化 `double_it<std::string>` 时反过来,只看到 `return x + " world";`。

这就是 `if constexpr` 的全部核心:它让模板代码能像普通代码一样按条件分支,但每个实例只保留它真正会走的那一条。过去这事儿得靠一摞重载或者偏特化才能做到,现在一个函数体就收掉了。

::: warning 条件必须是编译期常量
`if constexpr` 的条件得是一个常量表达式。要是您拿一个运行时才能确定的值去当条件,编译器会直接拒收:

```cpp
int main(int argc, char**) {
    if constexpr (argc > 1) {   // argc 是运行时参数,不是常量表达式
        return 0;
    }
    return 1;
}
```

GCC 报的是 `error: 'argc' is not a constant expression`。想用运行时值走分支,那就用普通 `if`;`if constexpr` 只服务编译期已知的条件。
:::

## `else if constexpr`:把一摞重载拍扁

`if constexpr` 真正省事的地方,是替掉那种「按类型分派」的一摞重载。咱们写一个 `inspect`,整数打印翻倍,字符串追加一个感叹号并报长度,别的类型调它的 `foo()`:

```cpp
template <typename T>
void inspect(T& x) {
    if constexpr (std::is_integral_v<T>) {
        std::cout << "整数:" << x << ",翻倍:" << (x + x) << "\n";
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "字符串长度:" << x.size() << "\n";   // 只有 string 才有 size
        x.append("!");
    } else {
        x.foo();   // 只有走 else 分支的类型才需要 foo
    }
}
```

<OnlineCompilerDemo allow-run
  title="else if constexpr 多分派:丢弃分支里依赖 T 的操作不报错"
  source-path="code/examples/vol4/vol2-modern-cpp17/multi_dispatch.cpp"
  description="传 int 走 integral 分支,string 分支对 int 不存在;传 string 走第二支,else 里的 x.foo() 不实例化。"
/>

运行结果:

```text
整数:7,翻倍:14
字符串长度:2
s 现在是:hi!
```

这里有个关键点容易被错过。`x.size()`、`x.append("!")`、`x.foo()` 这三句,分别只有特定类型才有。但传 `int` 进来时,编译器实例化的是 `inspect<int>`,条件 `is_integral_v<int>` 为真,第一支被保留,后面两支整个丢弃——`int` 没有 `size`、没有 `append`、没有 `foo`,全都不重要,因为那两支对 `int` 来说不存在。传 `HasFoo`(一个只有 `foo()` 的结构体)进来时,前两支条件都为假,落到 `else`,`h.foo()` 能编过。

这种「一个函数体替代三个特化」的写法,是 `if constexpr` 最日常的用法。过去要写成三个偏特化或者三个重载,现在顺着类型条件一路 `else if constexpr` 就行,读起来和普通 `if-else` 一样顺。

## 丢弃的分支到底查到哪一步

讲到这里,有个边界得说清楚,这大概是 `if constexpr` 最容易被误解的地方。**被丢弃的分支照样得过语法检查,只是不做实例化**。只有那些依赖模板参数的语义检查,才会跟着实例化一起跳过。

咱们看个对照。下面这段,条件 `sizeof(T) > 100` 对绝大多数类型都是假的,分支会被丢弃:

```cpp
template <typename T>
void f() {
    if constexpr (sizeof(T) > 100) {
        T x = ;   // 语法非法:等号右边什么都没有
    }
}
```

即便分支被丢弃,GCC 照样报错:

```text
error: expected primary-expression before ';' token [-Wtemplate-body]
    4 |         T x = ;
      |               ^
```

`T x = ;` 这句的毛病不依赖 `T` 是什么——等号右边空着,谁来看都是语法错误,这种错误在解析阶段就抓到了,轮不到「丢弃分支」来放过。反过来,像 `x.foo()` 这种「`T` 到底有没有 `foo`」的问题,必须把 `T` 代进去才知道,它属于实例化阶段的检查,丢弃分支自然就跳过了。

记住这条区分就够了:**语法得合法,这是底线;依赖模板参数的语义检查,才会被「丢弃」一并跳过**。所以您在丢弃分支里写 `t.someMember()` 没事,但少写一个分号、括号没配平,该报还是报。

<OnlineCompilerDemo allow-run
  title="丢弃分支的边界:默认能编,-DSYNTAX_ERR 复现语法报错"
  source-path="code/examples/vol4/vol2-modern-cpp17/discarded_boundary.cpp"
  description="默认演示依赖 T 的操作在丢弃分支里不触发实例化;加 -DSYNTAX_ERR 复现:语法错误即使分支被丢弃也照样报。"
/>

## 两个顺手就能用的写法

`if constexpr` 还有两个常见用法,顺手记一下。

第一个是带初始化语句的形式,和普通 `if` 的 init-statement 一样,C++17 起可用:

```cpp
template <typename T>
void first_kind(const T& container) {
    if constexpr (auto v = *container.begin(); std::is_integral_v<decltype(v)>) {
        std::cout << "装的是整数,第一个:" << v << "\n";   // v 在这里可见
    } else {
        std::cout << "装的不是整数\n";
    }
}
```

`auto v = *container.begin()` 先取首元素,`v` 在条件和两个分支里都可见,省得在外面先声明一个变量。

第二个是配合泛型 lambda,它是 `std::visit` 的标准搭子。一个持有多类型的 `std::variant`,访问它本来要写一个带一堆 `operator()` 重载的访问器,用泛型 lambda 加 `if constexpr` 一把就收掉:

```cpp
auto print_variant = [](const auto& v) {
    using T = std::decay_t<decltype(v)>;
    if constexpr (std::is_same_v<T, int>) {
        std::cout << "int:" << v << "\n";
    } else if constexpr (std::is_same_v<T, double>) {
        std::cout << "double:" << v << "\n";
    } else {
        std::cout << "string:" << v << "\n";
    }
};

std::variant<int, double, std::string> var = 42;
std::visit(print_variant, var);   // int:42
```

<OnlineCompilerDemo allow-run
  title="init-statement 与 std::visit 泛型 lambda"
  source-path="code/examples/vol4/vol2-modern-cpp17/visit_init.cpp"
  description="if constexpr (init; cond) 形式 + 泛型 lambda 里用 if constexpr 替代一摞 operator() 重载。"
/>

运行结果:

```text
装的是整数,第一个:10
装的不是整数
int:42
string:hello
```

## 什么时候用,什么时候别用

`if constexpr` 好用,但不是所有「按类型分情况」都该用它。笔者给一条判断准则。

凡是要在**一个函数体里按类型走不同实现**,且这些实现共享大部分上下文(同样的参数、同样的前后处理),`if constexpr` 是最直接的写法,比拆成一摞重载或偏特化干净得多。`std::visit` 的泛型 lambda、模板里给数值类型和字符串类型分别处理的逻辑,都属于这类。

反过来,如果您要**约束模板参数合不合格**(比如「这个函数只接受整数」),那该用 concepts(`requires std::integral<T>`),不该用 `if constexpr` 配 `static_assert` 凑。concepts 能进重载决议、能给出点名约束的报错,这些是 `if constexpr` 给不了的。上一篇咱们看过 SFINAE 往 concepts 迁移的对比,结论在这里一样:约束归 concepts,分派归 `if constexpr`,两者各管一摊。

还有一类容易写错的:分支条件不是真的依赖类型,只是图省事。比如普通函数里写 `if constexpr (sizeof(int) == 4)`,虽然合法,但条件永远为真或永远为假,这种「编译期常量的 if」意义不大,还容易把真正该用 `#ifdef` 的平台相关代码藏起来。`if constexpr` 的价值在于条件**依赖模板参数**,随实例化结果变化,这才让它有别于普通 `if` 和预处理指令。

下一篇咱们把 `if constexpr` 用到一个更出彩的地方——变参模板。C++17 之前,处理「任意个参数」得靠模板递归配一个终止重载,`if constexpr` 配 fold expressions 能把那一套收得干干净净。
