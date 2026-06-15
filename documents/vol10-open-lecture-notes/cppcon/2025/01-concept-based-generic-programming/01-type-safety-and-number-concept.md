---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 演讲笔记 —— 从隐式窄化转换到 Number<T> 包装类型，再到 safe_int 与 checked_span
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 45
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: 类型安全、Number 约束与边界检查
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
---
# 让我们从手动判断到隐式守护说起

:::tip
PS一下，这个部分是基于CppCon的二次发散，上面的链接是他们向Youtube发送的视频系列，国内的用户可以访问Bilibili链接进行观看。
:::

C++ 的泛型编程可以追溯到 1991 年模板被引入语言的时候（C++ Release 3.0）。Stroustrup 当初设计模板，首要动机是取代 C 预处理器宏来实现类型安全的泛型容器。他在 *The Design and Evolution of C++* 中写道，宏 "fail to obey scope and type rules and don't interact well with tools"，而模板被设计为 "as efficient as macros" 但类型安全<RefLink :id="1" preview="Stroustrup, The Design and Evolution of C++, 1994, Ch.15" />。

但故事在 1994 年出现了意想不到的转折。Erwin Unruh 在 C++ 委员会会议上展示了一段合法的 C++ 程序，它甚至不能编译通过，但编译器在报错信息中逐行输出了素数序列<RefLink :id="2" preview="Unruh, Prime Number Computation, C++ 委员会会议, 1994" />。整个委员会这才意识到，模板无意中构成了一套图灵完备的编译期计算系统。次年，Todd Veldhuizen 发表论文系统描述了这套技术，并将其命名为 **模板元编程**（Template Metaprogramming）<RefLink :id="3" preview="Veldhuizen, Using C++ Template Metaprograms, C++ Report, 1995" />。模板由此从 "类型安全的宏替代品" 进化为 C++ 中一个不可绕过的编译期抽象机制。

模板报错信息动辄几百行、可读性极差——这是很多 C++ 开发者对泛型编程望而却步的原因。但随着项目规模增长，不用泛型的代码重复度会高到难以维护。这一篇我们从泛型编程的基础动机出发，走到一个具体的、可以落地的类型安全问题——隐式窄化转换。

本文的实验环境为 Arch Linux WSL，GCC 16.1.1，以下是环境信息：

```bash
❯ gcc -v
Using built-in specs.
COLLECT_GCC=gcc
COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-pc-linux-gnu/16.1.1/lto-wrapper
Target: x86_64-pc-linux-gnu
Configured with: /build/gcc/src/gcc/configure --enable-languages=ada,c,c++,d,fortran,go,lto,m2,objc,obj-c++,rust,cobol --enable-bootstrap --prefix=/usr --libdir=/usr/lib --libexecdir=/usr/lib --mandir=/usr/share/man --infodir=/usr/share/info --with-bugurl=https://gitlab.archlinux.org/archlinux/packaging/packages/gcc/-/issues --with-build-config=bootstrap-lto --with-linker-hash-style=gnu --with-system-zlib --enable-cet=auto --enable-checking=release --enable-clocale=gnu --enable-default-pie --enable-default-ssp --enable-gnu-indirect-function --enable-gnu-unique-object --enable-libstdcxx-backtrace --enable-link-serialization=1 --enable-linker-build-id --enable-lto --enable-multilib --enable-plugin --enable-shared --enable-threads=posix --disable-libssp --disable-libstdcxx-pch --disable-werror --disable-fixincludes
Thread model: posix
Supported LTO compression algorithms: zlib zstd
gcc version 16.1.1 20260430 (GCC)

❯ uname -a
Linux Charliechen 6.6.114.1-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Mon Dec  1 20:46:23 UTC 2025 x86_64 GNU/Linux
```


## 先搞清楚泛型编程到底想干什么

泛型编程的效果是把代码写得更通用、更抽象——这只说对了一半。Alex Stepanov（STL 之父）指出，泛型编程的目标是"最通用、最高效、最灵活地表达想法"，关键是表达想法，不是为了抽象而抽象。把手段当作目的，是编程中常见的误区——另一个典型就是设计模式的滥用。

这个区别很重要。我们不是从某个抽象模型出发设计代码，而是从具体的、高效的算法出发，发现其中的共性，再把共性提取出来。而且性能不能丢，因为 C++ 存在的意义很大一部分就在于此。硬件变强的同时，我们对软件的期望也在飞速膨胀，而半导体工艺似乎走到了一个瓶颈，随意编写代码的空间越来越小。

泛型编程对我们的要求更高了：它要求我们洞察抽象领域中可复用的模式。而它的底线是——抽象完了，性能不能比手写的具体版本差。否则引入泛型编程的意义就不存在了。编写代码本身是需求层次中完成工作的那一层，不做多余的事情。如果某个地方不会被复用、且对性能更加敏感，那就不要引入泛型。

## Alex Stepanov 认为的几个 C++ 设计标准

Stepanov 在 1994 年左右提了三个设计标准<RefLink :id="4" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" />：第一是通用性，好的泛型组件应该能表达出连设计者自己都没想到的用法；第二是不妥协的效率，用 C++ 写系统级代码，效率得能跟 C 打平，写线性代数得能跟 Fortran 打平；第三是静态类型的接口，编译期就能检查，不把错误留到运行时。后来他又加了两个很接地气的要求：编译时间不能长得让人去喝咖啡（header only 的库表示这很难保证），以及学习曲线不能陡到需要 MIT 博士学位才能上手<RefLink :id="5" preview="Nygaard, cited in Stroustrup, Concept-Based Generic Programming in C++, 2025, §1" />——至于 C++ 有没有做到这一点，大家心里有数。

## 隐式窄化转换：一个经典的类型安全陷阱

动机说完了，我们从一个具体问题开始。一个概念的引入，必须有对应的问题场景，否则就是空中楼阁。来看这段代码：

```cpp
#include <iostream>

int main() {
    int big = 30000;
    short small = big;          // 30000 超出了 short 的范围吗？其实没有，short 一般是 -32768~32767
                                // 但如果是 40000 呢？

    short overflow = 40000;     // 编译通过！但值已经错了

    double pi = 3.14159;
    int int_pi = pi;            // 小数部分直接丢了

    std::cout << "overflow = " << overflow << "\n";  // 输出一个奇怪的负数
    std::cout << "int_pi = " << int_pi << "\n";      // 输出 3

    return 0;
}
```

这段代码使用的是 C++23 以下的写法，保证所有编译器都能直接编译。

在笔者机器上跑的结果是 `overflow = -25536`，`int_pi = 3`。编译器一个警告都没有（除非你开 `-Wall -Wextra`，但很多项目不开）。这种 bug 特别阴险：代码能跑，结果就是错的，而且往往在数据量小的时候不暴露，上线了才出事。

很多人觉得"这就是 C++ 的特性，自己小心就行"。但这种事情靠人肉小心是靠不住的。Bjarne Stroustrup 自己也说过，他当年想解决这问题但没搞定，C 语言阵营那边也不让动。那我们作为使用者，能不能自己防住？

## 用 C++20 的 concept 来建模"数字"

C++20 给了我们一个新武器：concept。它的本质很简单——concept 就是一个编译期求值的布尔谓词，输入是类型，输出是 true 或 false。换个说法：它让编译器理解一个"概念"，而不需要我们用复杂的自然语言描述一遍。

标准库里已经定义好了一些基础的 concept，比如 `std::integral` 和 `std::floating_point`，它们判断一个类型是不是整数类型、是不是浮点类型。这些不是什么新发明，C 语言的 K&R 第一版里就在区分 int 和 float 了，只不过现在我们有了语言层面的、编译期可查询的表示。

我们先写一个最简单的 concept，把"数字"这个概念表达出来：

```cpp
#include <concepts>
#include <type_traits>

// 我自己的 "number" concept：要么是整数，要么是浮点数
template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// 验证一下
static_assert(number<int>, "int 应该是 number");
static_assert(number<double>, "double 应该是 number");
static_assert(number<char>, "char 也是整数类型，所以是 number");
static_assert(!number<std::string>, "string 不是 number");
```

这里有个语法细节值得说明：`std::integral<T>` 看起来像函数调用，但它不是。`std::integral` 是一个 concept，`<T>` 是把它用类型 T 实例化，整个表达式的值是一个编译期 bool。你不能写 `std::integral(T)`，那个语法不对。把它理解成"对 T 做 integral 这个测试"，返回 true 或 false 即可。

跑一下上面的代码，四个 `static_assert` 全部通过，说明我们的 `number` concept 基本能用。

## 动手写一个 narrowing 判断

我们能不能写一个 concept，判断"把类型 U 的值赋给类型 T 时，是否会发生窄化转换"？既然写下这篇文章。

首先，如果 T 的表示范围比 U 小，那显然可能窄化。比如把 `int` 赋给 `short`，`int` 能表示的值比 `short` 多得多。但"范围更小"怎么判断？C++ 标准库里没有直接给我们"类型的取值范围"这种 concept，但 `<type_traits>` 里有 `std::numeric_limits`，可以查到各种类型的 min 和 max。如果 U 是浮点数、T 是整数，那小数部分一定会丢，这也是窄化。

还有一种容易忽略的情况：U 和 T 都是整数，大小也一样（比如都是 32 位），但是有符号和无符号不同，那负数赋给无符号类型也会出问题。把这些规则写成代码：

```cpp
#include <concepts>
#include <type_traits>
#include <limits>

template<typename T>
concept number = std::integral<T> || std::floating_point<T>;

// 判断 T 是否"比 U 小"（能表示的值更少）
// 这里用 numeric_limits 的范围来比较
template<typename T, typename U>
concept smaller_range =
    number<T> && number<U> &&
    (std::numeric_limits<T>::max() < std::numeric_limits<U>::max() ||
     std::numeric_limits<T>::min() > std::numeric_limits<U>::min());

// 核心判断：从 U 到 T 的赋值是否会发生窄化
template<typename T, typename U>
concept narrowing_assign =
    number<T> && number<U> &&
    (
        // 情况1：T 的范围比 U 小，可能放不下
        smaller_range<T, U> ||
        // 情况2：U 是浮点数，T 是整数，小数部分会丢
        (std::floating_point<U> && std::integral<T>) ||
        // 情况3：U 和 T 大小相同但有符号性不同
        (std::integral<T> && std::integral<U> &&
         std::signed_integral<U> != std::signed_integral<T>)
    );

// 测试用例
static_assert(narrowing_assign<short, int>, "int -> short 应该是窄化");
static_assert(narrowing_assign<int, double>, "double -> int 应该是窄化（丢小数）");
static_assert(narrowing_assign<unsigned int, int>, "int -> unsigned int 可能窄化（负数问题）");
static_assert(!narrowing_assign<int, short>, "short -> int 不是窄化");
static_assert(!narrowing_assign<double, float>, "float -> double 不是窄化");
static_assert(!narrowing_assign<int, int>, "int -> int 不是窄化");
```

编译跑一下，六个 `static_assert` 全部通过。我们可以拿最后一个 `!narrowing_assign<int, int>` 来验证一下逻辑：同一个类型赋值，情况1 的 `smaller_range<int, int>` 里 `max() < max()` 是 false、`min() > min()` 也是 false，所以不触发；情况2 要求 U 是浮点 T 是整数，不满足；情况3 要求有符号性不同，`int` 和 `int` 显然相同。三个分支全 false，整体就是 false，取反后 `static_assert` 通过——这跟我们"同类型赋值不窄化"的直觉完全吻合。

还有一点值得提：`narrowing_assign` 里 `&&` 和 `||` 混用的地方必须加括号。因为 `&&` 的优先级高于 `||`，如果不加括号，`number<T> && number<U>` 只会约束第一个 `||` 分支，后面两个分支在非 number 类型上也可能被求值——虽然对当前测试用例来说结果碰巧正确，但语义上就是错的。加括号让三个分支成为一个整体，再由 `number<T> && number<U>` 统一约束，逻辑才严谨。

## 还有一些边界情况要想清楚

上面的实现能覆盖大部分场景，但还有一些细节值得说。比如浮点数之间的转换：`double` 到 `float` 算不算窄化？从精度角度来说，当然算，因为 `double` 能表示的有效数字比 `float` 多。但在当前实现里，`smaller_range<float, double>` 会判断 `numeric_limits<float>::max() < numeric_limits<double>::max()`，这是 true，所以会被正确识别为窄化。

再比如 `char` 到 `unsigned char` 的情况。`char` 的有符号性是实现定义的（在某些平台上是 signed，某些上是 unsigned）。如果平台上 `char` 是 signed 的，那 `signed_integral<char> != signed_integral<unsigned char>` 就是 true，会被识别为窄化。这其实是合理的，因为如果 `char` 是 -1，赋给 `unsigned char` 会变成 255。

不过要注意，这个实现还不是 100% 严谨。标准对窄化转换的定义（在 C++11 的列表初始化规则里）比这里写的更细致，比如还考虑了浮点数到整数时值是否在整数范围内等。但作为一个起点，这个 concept 已经能帮我们挡住大部分坑了。后续可以慢慢完善。

到这里可以总结一件事：concept 不是什么高深莫测的元编程技巧，它就是一种"把对类型的约束，写成编译期能检查的布尔表达式"的机制。以前写模板，约束全靠文档和命名约定（比如"请传入一个随机访问迭代器"），编译器不管，传错了就报一堆天书。现在有了 concept，编译器能在第一时间告诉你"你传的类型不满足要求"，而且报错信息是人类能看懂的。

下一步要做的是把这个 `narrowing_assign` concept 用到实际函数里，做一个安全的赋值包装——这是下一节的内容。至少"用 concept 表达类型约束"这个核心思路，到这里已经理顺了。

---

# 从手动判断到隐式守护：把 narrowing 转换检查塞进类型里

上一节我们搞清楚了 narrowing 转换的判定规则。那些规则如果每次写代码都在脑子里过一遍，几乎不可能做到——signed 和 unsigned 混合的时候，到底哪个大、会不会溢出、正数部分能不能表示，光想这些就已经很晕了。演讲者说这东西手写出来大约一页纸，而且很杂乱很棘手。

所以这一节要做的事情就是：把那一页纸的杂乱逻辑，变成真正能跑的代码，然后再把它藏起来，让你平时写代码的时候根本感觉不到它的存在。

## 先把判断逻辑翻译成代码

一个直觉是：要判断一个值从类型 U 赋值给类型 T 会不会发生 narrowing，直接用个 `static_cast` 然后比较一下就行了。但仔细想想根本不是那么回事——signed 和 unsigned 混合的时候，比较本身就有陷阱。所以我们需要一个老老实实的、逐步判断的函数。

思路是：先在编译期做尽可能多的排除工作，把那些"绝对不可能发生 narrowing"的情况直接筛掉，只留下真正需要在运行时检查的路径。这其实就是泛型编程一直强调的——不该在运行时做的工作，就别做。

```cpp
#include <type_traits>
#include <limits>
#include <stdexcept>

// 核心判断：值 u 赋值给 T 类型的变量，会不会发生 narrowing？
template<typename T, typename U>
constexpr bool would_narrow(U u) noexcept {
    // 第一层：编译期就能排除的情况
    // 如果 T 能表示 U 的所有值，那不管 u 是什么，都不可能 narrowing
    if constexpr (std::is_same_v<T, U>) {
        return false;  // 同类型，废话
    } else if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // 浮点转整数，几乎总是可能 narrowing 的
        // 除非这个浮点数恰好是个整数值且在范围内
        // 这个我们放运行时判断
    } else if constexpr (std::is_floating_point_v<T> && std::is_floating_point_v<U>) {
        // 浮点转浮点，只有当 T 的精度/范围小于 U 时才可能 narrowing
        if constexpr (std::numeric_limits<T>::digits >= std::numeric_limits<U>::digits &&
                      std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                      std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
            return false;  // T 的范围和精度都够，编译期直接排除
        }
    } else if constexpr (std::is_integral_v<T> && std::is_integral_v<U>) {
        // 整数转整数的情况最复杂，下面细说
        if constexpr (std::is_signed_v<T> == std::is_signed_v<U>) {
            // 同号比较简单：看范围
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max() &&
                          std::numeric_limits<T>::lowest() <= std::numeric_limits<U>::lowest()) {
                return false;
            }
        } else if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
            // signed T 接收 unsigned U
            // T 的正数范围能覆盖 U 的全部值就行
            if constexpr (std::numeric_limits<T>::max() >= std::numeric_limits<U>::max()) {
                return false;
            }
        } else {
            // unsigned T 接收 signed U
            // U 是负数的话肯定 narrowing，但编译期不知道 u 的值
            // 所以这种情况不能在编译期排除，得放运行时
        }
    }

    // 第二层：编译期排不掉的，运行时判断

    // signed -> unsigned 且源值为负数：一定是 narrowing
    // 注意：不能用 round-trip 检测（int(-1) → unsigned → int(-1) 在补码上是可逆的）
    if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        if (u < 0) return true;
    }

    // 先做静态转换，看值有没有变
    T t = static_cast<T>(u);
    if (static_cast<U>(t) != u) {
        return true;  // 转过去再转回来，值不一样，信息丢了
    }

    // 浮点转整数的额外检查：即使转回来一样，也要确认不是那种
    // "3.0 转成 3 再转回 3.0"的巧合——不过这种情况下值确实没丢，
    // 所以其实上面的检查已经够了。但标准对浮点转整数有更严格的要求：
    // 原值必须是整数值（没有小数部分）
    if constexpr (std::is_floating_point_v<U> && std::is_integral_v<T>) {
        // 如果 u 不是整数值，即使 static_cast 恰好截断了，
        // 严格来说也是 narrowing（信息丢失了小数部分）
        if (u != static_cast<U>(static_cast<long long>(u))) {
            return true;
        }
    }

    return false;
}
```

写完这个函数回头来看，signed 和 unsigned 混合的时候，编译期能排除多少、运行时必须检查多少，这个边界确实需要仔细想。有一个容易踩的坑：单纯用 round-trip（转过去再转回来）来检测 narrowing，在 signed→unsigned 转换时会失效——因为 `int(-1) → unsigned(4294967295) → int(-1)` 在补码上是完全可逆的，round-trip 检测不出来。所以必须在 round-trip 之前显式检查"源值是否为负数"。`if constexpr` 在这里起到了关键作用——编译期能确定的分支根本不会生成代码，不会有一堆无用的比较指令。

## 发生 narrowing 的时候怎么办？抛异常

判断逻辑有了，接下来要决定的是：检测到 narrowing 之后怎么处理？

演讲者的方案很直接——抛异常。narrowing 转换在经过编译期过滤之后，真正会在运行时触发的概率极低。大部分代码里类型都是匹配的，编译期就排除了；剩下那些需要运行时检查的，绝大多数也不会真的溢出。可能一百万次调用里才触发一次，这正好是异常最擅长的场景——处理极其罕见的异常情况。

```cpp
template<typename T, typename U>
constexpr T narrow_convert(U u) {
    if (would_narrow<T>(u)) {
        throw std::invalid_argument("narrowing conversion detected");
    }
    return static_cast<T>(u);
}
```

就这么简单。你可以直接拿来用：

```cpp
#include <iostream>

int main() {
    // 正常情况，不会抛异常
    int a = narrow_convert<int>(42.0);        // OK，42.0 是整数值
    unsigned int b = narrow_convert<unsigned int>(100);  // OK

    // 这些会抛异常
    try {
        char c = narrow_convert<char>(300);   // 300 超出 char 范围
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        unsigned int d = narrow_convert<unsigned int>(-1);  // 负数转 unsigned
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        int e = narrow_convert<int>(3.14);  // 浮点转整数，有小数部分
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    std::cout << "a = " << a << ", b = " << b << "\n";
}
```

跑一下看看输出：

```text
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
a = 42, b = 100
```

很好，该拦的都拦住了。但问题来了——不可能在每个赋值的地方都写 `narrow_convert<int>(xxx)`。代码会变得冗长，而且完全没法保持一致性。靠程序员自觉去加检查，最后一定会有漏网之鱼。有些地方加了，有些地方忘了，然后 bug 就藏在那些忘了的地方。

## 把检查塞进类型里：Number<T>

所以真正的解法是——让检查变成隐式的。定义一个包装类型 `Number<T>`，它在构造的时候就自动做 narrowing 检查。之后这个 `Number<T>` 就像普通的 `T` 一样用，但不用担心 narrowing 问题，因为如果构造都过不了，这个对象根本就不存在。

```cpp
template<typename T>
class Number {
    T value_;

public:
    // 构造函数：这里是所有魔法发生的地方
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}

    // 同类型构造不需要检查，但为了统一接口也走一遍（编译期会优化掉）
    constexpr Number(T t) : value_(t) {}

    // 隐式转换回 T，让 Number<T> 能像 T 一样用
    constexpr operator T() const noexcept { return value_; }

    // 取值
    constexpr T get() const noexcept { return value_; }
};
```

你看，这个类本身就这么点东西。看起来像演示代码，但它真的能用。我们来试：

```cpp
int main() {
    // 这些都能正常工作
    Number<int> x = 42;              // int -> int，没问题
    Number<int> y = 3.0;             // double -> int，3.0 是整数值，没问题
    Number<unsigned int> z = 100u;   // unsigned int -> unsigned int，没问题

    // Number<T> 可以当 T 用，因为有了 operator T()
    int sum = x + static_cast<int>(z);  // 正常运算
    std::cout << "x = " << x << ", y = " << y << ", z = " << z << "\n";
    std::cout << "sum = " << sum << "\n";

    // 这些会在构造时抛异常
    try {
        Number<char> c = 300;  // 编译不报错，运行时抛异常
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }

    try {
        Number<unsigned int> bad = -1;  // 负数转 unsigned，运行时抛异常
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获到: " << e.what() << "\n";
    }
}
```

输出：

```text
x = 42, y = 3, z = 100
sum = 142
捕获到: narrowing conversion detected
捕获到: narrowing conversion detected
```

到这里可以看出一个关键的设计思路：以前觉得模板元编程和类型系统是两回事，但实际上类型系统本身就是做检查最好的地方。不需要记住哪里该检查、哪里不该检查，只要用 `Number<T>` 代替 `T`，检查就自动发生了。而且因为编译期的 `if constexpr` 分支，那些不需要检查的路径（比如同类型赋值）连判断代码都不会生成，零开销。

## 但光能构造还不够，得能算术

一个数字类型如果只能构造不能运算，那跟个常量有什么区别？所以我们需要给 `Number<T>` 加算术运算符。但这里有个问题：`Number<int>` 加 `Number<double>` 应该返回什么？你不能随便返回一个类型，得有个规矩。

标准库里有个东西叫 `std::common_type`，它就是干这个的——给定两个类型，告诉你把它们做算术运算后应该用什么类型。比如 `common_type_t<int, double>` 是 `double`，`common_type_t<int, unsigned int>` 在大多数平台上是 `unsigned int`。我们直接用它：

```cpp
#include <type_traits>

template<typename T>
class Number {
    T value_;

public:
    template<typename U>
    constexpr Number(U u) : value_(narrow_convert<T>(u)) {}
    constexpr Number(T t) : value_(t) {}
    constexpr operator T() const noexcept { return value_; }
    constexpr T get() const noexcept { return value_; }

    // 加法：Number<T> + Number<U> -> Number<common_type_t<T, U>>
    template<typename U>
    constexpr auto operator+(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        // value_ + other.value_ 先做普通算术（会隐式提升），
        // 然后用结果构造 Number<ResultType>，构造时自动做 narrowing 检查
        return Number<ResultType>(value_ + other.get());
    }

    // 减法，同理
    template<typename U>
    constexpr auto operator-(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ - other.get());
    }

    // 乘法
    template<typename U>
    constexpr auto operator*(const Number<U>& other) const
        -> Number<std::common_type_t<T, U>>
    {
        using ResultType = std::common_type_t<T, U>;
        return Number<ResultType>(value_ * other.get());
    }
};
```

来跑个稍微复杂的例子验证一下：

```cpp
int main() {
    Number<int> a = 10;
    Number<double> b = 3.5;

    // int + double -> common_type 是 double
    auto result = a + b;
    std::cout << "10 + 3.5 = " << result << "\n";
    std::cout << "结果类型是 Number<double>? "
              << std::is_same_v<decltype(result), Number<double>> << "\n";

    // unsigned + int 的混合运算
    Number<unsigned int> big = 3000000000u;  // 30亿，unsigned int 能表示
    Number<int> small = 100;

    auto result2 = big + small;
    std::cout << "3000000000u + 100 = " << result2 << "\n";

    // 试试溢出场景：两个大数相加
    Number<unsigned int> x = 3000000000u;
    Number<unsigned int> y = 2000000000u;
    try {
        // 3000000000 + 2000000000 = 5000000000，超出 unsigned int 范围
        auto overflow = x + y;
        std::cout << "不应该到这里\n";
    } catch (const std::invalid_argument& e) {
        std::cout << "加法溢出捕获到: " << e.what() << "\n";
    }
}
```

输出：

```text
10 + 3.5 = 13.5
结果类型是 Number<double>? 1
3000000000u + 100 = 3000000100
加法溢出捕获到: narrowing conversion detected
```

:::warning 原文错误更正：unsigned 算术溢出不会被 narrow_convert 检测到
上面的输出中，最后一行"加法溢出捕获到"在实际编译运行中**不会出现**。实测结果（GCC 16.1.1, C++20）：

```text
Raw unsigned sum: 705032704
Would narrow? 0
No exception thrown! overflow = 705032704
```

原因在于：`unsigned int + unsigned int` 的算术运算在 C++ 中是**回绕的**（well-defined wrapping），`3000000000u + 2000000000u` 的结果是 `705032704`——一个合法的 `unsigned int` 值。随后 `narrow_convert<unsigned int>(705032704u)` 检测到同类型赋值，`would_narrow` 直接返回 false，异常根本不会抛出。

这是 `Number<T>` 当前设计的一个根本局限：`narrow_convert` 只能检测**赋值时的窄化转换**，不能检测**算术运算本身的溢出**。要检测溢出，需要使用编译器内置函数（如 `__builtin_add_overflow`）或手动检查：

```cpp
template<typename T>
constexpr T safe_add(T a, T b) {
    if constexpr (std::is_unsigned_v<T>) {
        if (a > std::numeric_limits<T>::max() - b) {
            throw std::overflow_error("unsigned addition overflow");
        }
    } else {
        // signed overflow is UB, 必须用 __builtin_add_overflow 或类似机制
        T result;
        if (__builtin_add_overflow(a, b, &result)) {
            throw std::overflow_error("signed addition overflow");
        }
        return result;
    }
    return a + b;
}
```

验证代码见 [01-06-overflow-not-caught.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/01-concept-based-generic-programming/01-06-overflow-not-caught.cpp)。
:::

看最后一个溢出捕获的例子——我们需要注意，`narrow_convert` 只能拦截**类型转换时**的窄化，对于同类型算术运算本身的溢出（如 `unsigned int + unsigned int` 的回绕），它是无能为力的。`common_type_t<unsigned int, unsigned int>` 就是 `unsigned int` 本身，运算结果在赋值给 `Number<unsigned int>` 之前就已经回绕成了一个合法值。要完整防御算术溢出，需要额外的机制（如编译器内置的 overflow 检查函数），这超出了 `narrow_convert` 的职责范围。

到这里，从手动判断规则、到运行时检查函数、到异常处理策略、再到包装类型和算术运算，这条线终于串起来了。关键在于把这些东西当成一个完整的 narrowing 防御体系来理解，而不是孤立的知识点。

---

# 不用自己造轮子：标准库里的运算对象 + 消灭比较陷阱

要实现一套安全的整数类型，直觉上得把加减乘除比较运算全自己手写一遍，想想就头大。但实际上标准库里早就准备好了 `std::plus`、`std::multiplies` 这些函数对象，每个就那么几行代码，根本不是什么黑魔法。当然，造轮子算 C++ 的传统艺能了。

## 先看看运算符怎么写

一个常见的误解是：要给自定义类型重载 `operator+`、`operator*`，就得在类里面或者全局写一堆 `friend` 函数，每个函数里处理各种边界情况。但实际上只需要把标准库里的函数对象拿过来用就行。

```cpp
#include <functional>

// 我定义了一个简化版的 safe_int，只展示核心思路
template <typename T>
struct safe_int {
    T value;

    // 加法：直接用标准库的 std::plus，一行搞定
    friend safe_int operator+(const safe_int& a, const safe_int& b) {
        return safe_int{std::plus<T>{}(a.value, b.value)};
    }

    // 乘法：同理
    friend safe_int operator*(const safe_int& a, const safe_int& b) {
        return safe_int{std::multiplies<T>{}(a.value, b.value)};
    }
};
```

你会发现这里的关键在于：`std::plus<T>{}` 是一个函数对象，调用它的时候，如果发生了不该发生的类型转换（比如有符号和无符号混在一起），它会被我们之前设好的规则拦住。运算逻辑本身不需要操心，标准库已经写好了，我们只管"拦截"和"放行"。

## 比较运算：signed/unsigned 混用的重灾区

运算符重载本身不难，但比较运算才是 signed/unsigned 混用的重灾区。排查了一整个下午的 bug，最后发现就是一行比较写错了——这种情况并不少见。

来看这段代码：

```cpp
#include <iostream>

int main() {
    int a = -1;
    unsigned int b = 2;

    std::cout << (a < b) << "\n";  // 你猜输出什么？
}
```

跑一下，输出是 `0`，也就是 `false`。负数小于正数，结果居然是 false？为什么？答案就是C++ 的隐式转换规则存在一条——当有符号和无符号混在一起做比较时，有符号数会被转换成无符号数。所以 `-1` 变成了一个巨大的数（`4294967295`），当然不小于 2。这个规则从 1972 年 C 语言诞生就有了，当时可能觉得没什么，但几十年下来不知道埋了多少 bug。

演讲里说得好：这套规则本来在 1972 年就该修正的，但等到大家意识到有多糟糕的时候，世界上已经存在太多依赖这个行为的代码了，改不动了。直到今天我们还在为此吃苦头。

## 亲手修掉这个比较陷阱

既然内置类型不靠谱，那就在我们的 safe_int 里把比较运算接管过来。思路很直接：如果两边类型不一致（一个 signed 一个 unsigned），先做特殊判断；如果类型一致，直接走正常比较。

```cpp
template <typename T>
struct safe_int {
    T value;
};

// 跨类型的 operator<：模板化的自由函数，能处理 safe_int<T> 和 safe_int<U> 的比较
template <typename T, typename U>
bool operator<(const safe_int<T>& a, const safe_int<U>& b) {
    if constexpr (std::is_signed_v<T> && std::is_unsigned_v<U>) {
        // a 是有符号，b 是无符号
        // 如果 a 小于零，那它一定小于任何无符号数，直接返回 true
        if (a.value < 0) {
            return true;
        }
        // 否则两边都转成无符号再比，此时 a.value 一定是非负的，转换安全
        return static_cast<std::make_unsigned_t<T>>(a.value) < b.value;
    } else if constexpr (std::is_unsigned_v<T> && std::is_signed_v<U>) {
        // 反过来，a 是无符号，b 是有符号
        if (b.value < 0) {
            return false;
        }
        return a.value < static_cast<std::make_unsigned_t<U>>(b.value);
    } else {
        // 类型一致，正常比较，没有任何转换问题
        return a.value < b.value;
    }
}
```

这里有一个关键点：`operator<` 写成了**模板化的自由函数**而不是类内的 `friend`。原因是类内的 `friend bool operator<(const safe_int& a, const safe_int& b)` 只接受两个**相同 T** 的 `safe_int<T>`。而 `safe_int<int> < safe_int<unsigned int>` 是两个不同模板实例之间的比较，类内 friend 根本匹配不到。写成 `template<typename T, typename U>` 的自由函数后，编译器才能在 `safe_int<int>` 和 `safe_int<unsigned int>` 之间正确匹配这个 operator。`if constexpr` 让编译器把不走的分支直接优化掉，零开销。相等比较、大于比较也是同样的思路，照着写就行。

验证一下：

```cpp
int main() {
    safe_int<int> a{-1};
    safe_int<unsigned int> b{2};

    std::cout << (a < b) << "\n";  // 输出 1，终于正确了！
    // 注意：a 和 b 是不同模板实例 safe_int<int> 和 safe_int<unsigned int>，
    // 只有模板化的自由 operator< 才能匹配这个调用
}
```


## 更大的坑：范围检查被静默绕过

比较运算修好了，但还有一个更隐蔽的场景。演讲里举了一个 span 的例子——这种模式在实际代码中非常普遍。

先说背景。`std::span` 本质上就是一个"胖指针"——一个指向元素序列的指针加上序列的长度。这种思路其实不新，Dennis Ritchie 早在 1990 年代初就提出过在 C 语言中加入携带边界信息的指针（用于变长数组），当时叫 fat pointer，但委员会觉得运行时开销太大没有采纳<RefLink :id="7" preview="Ritchie, Variable-Size Arrays in C, 1990" />。现在 C++20 终于把 span 加进来了，算是迟到了几十年的正名——虽然 span 本身不做边界检查，但它为上层的安全包装提供了基础。

问题出在哪呢？看这段代码：

```cpp
#include <span>
#include <vector>

void process(std::span<int> data) {
    // 我想取 data 的前 max_size - 500 个元素
    unsigned int max_size = 50;  // 本来想写 500，手误写成了 50
    auto sub = data.subspan(0, max_size - 500);
    // sub 现在是什么？
}
```

`max_size` 是 `unsigned int`，值是 50。`50 - 500` 在无符号运算下会发生什么？下溢，变成一个巨大的数（`4294967296 - 450` 左右）。然后 `subspan` 拿到这个巨大的长度——而 `std::span::subspan` 在 C++20 里**没有**边界检查，它只有 precondition（违反即未定义行为），不会抛异常<RefLink :id="6" preview="cppreference, std::span::subspan, C++20" />。这意味着那个巨大的数直接被传进去，后果是未定义行为——可能读到了不该读的内存，可能刚好没崩，但你完全不能指望 span 自己帮你拦住。

仅仅因为一个小小的笔误，仅仅因为内置类型的转换规则，你完全失去了范围检查的保护。很多人觉得 span 已经够安全了，没想到在参数计算这一层就被绕过去了。

## 用 safe_int 给 span 加上真正的保护

现在我们有了能拦截所有错误转换的 safe_int，那能不能让 span 的尺寸参数也受到保护？当然可以。

我的思路是：先定义一个概念，表示"可以被 span 的类型"，然后在这个概念里要求尺寸类型必须是安全的整数。

```cpp
#include <concepts>
#include <span>
#include <vector>

// 先定义我们自己的 safe_int（简化版，假设已经实现了完整的安全运算）
template <typename T>
struct safe_int {
    T value;
    // ... 之前写的所有运算符重载都在这里
};

// 定义一个概念：可 span 的类型
// 标准库里有 std::contiguous_range，我基于它扩展
template <typename T>
concept spanable = std::contiguous_range<T>;

// 现在定义一个安全 span，尺寸类型用 safe_int
template <typename T>
struct safe_span {
    T* data_;
    safe_int<std::size_t> size_;  // 关键：尺寸是安全整数

    // 构造函数，从普通容器构造
    template <spanable Container>
    explicit safe_span(Container& c)
        : data_(c.data())
        , size_(safe_int<std::size_t>{c.size()})
    {}

    // 安全的 subspan
    safe_span subspan(std::size_t offset, safe_int<std::size_t> count) {
        // count 是 safe_int，任何溢出运算都会在构造阶段就被拦住
        // 不可能再出现 "50 - 500 变成巨大数" 的情况
        return safe_span{data_ + offset, count};
    }

    T* data() const { return data_; }
    std::size_t size() const { return size_.value; }
};
```

关键点在于成员变量 `size_` 的类型是 `safe_int<std::size_t>` 而不是裸的 `std::size_t`。这意味着任何对这个尺寸的运算——减法、比较、赋值——都会经过我们的安全检查。如果有人写了 `50 - 500`，safe_int 会在运算的那一刻就报错，而不是让一个巨大的数悄悄流进 subspan 里。**我们不需要在 span 的边界检查里去补救，我们需要从源头——整数运算本身——就杜绝错误值的产生。**回头看看，其实思路很简单：把不安全的内置整数替换成安全的包装类型，让错误在发生的那一刻就被抓住，而不是等它传播到某个边界检查里才被发现。换而言之——让真正应该负责处理的类处理对应的错误，而不是让其他组件给你兜底。

---

# 给 span 加上边界检查：从手动防御到类型推导

数组越界这个问题一直让人头疼：跑起来确实快，但一旦越界了，程序可能在某个完全不相干的地方崩掉，然后对着 gdb 发呆半个小时。接下来我们来看一种结构化的下标越界检查方式。

## 先搞清楚我们要做什么

核心需求其实特别简单：我有一个连续的内存区域，我知道它有多大，我想在每次用下标访问它的时候，自动检查这个下标有没有越界。如果越界了，立刻抛异常或者被编译器拦住，而不是等到内存被写坏了我才发现。

这听起来不就是 `std::vector` 的 `at()` 干的事吗？但区别在于，我不想承担一个动态分配的 vector，我可能只是拿到了一个裸指针加一个长度，或者一个原生数组，我想用同样安全的方式去访问它。这就是 span 的意义所在——它不拥有数据，只是"看"着数据，但看的时候可以帮你盯着边界。

## 动手写一个带检查的下标访问

我们先从最基础的场景开始。假设我已经有了一个 span 类型的东西，它内部持有数据和大小。我现在要做的，就是重载 `operator[]`，让它在执行访问之前先做范围检查。

```cpp
#include <iostream>
#include <stdexcept>
#include <span>
#include <array>

// 一个简单的带边界检查的 span 包装
template<typename T>
class checked_span {
    T* ptr_;
    std::size_t size_;

public:
    // 用指针和大小初始化——这就是"spanable"的本质
    checked_span(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // 带检查的下标访问
    T& operator[](std::size_t index) {
        if (index >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    const T& operator[](std::size_t index) const {
        if (index >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};
```

你看，这里的构造函数只接受指针和大小，这就是所谓的"spanable"——任何能提供数据指针和元素个数的东西，都可以用来初始化它。然后 `operator[]` 里面做了一件事：如果你给的 index 大于等于 size，直接扔异常。

## 跑一下看看效果

```cpp
int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span<int> s(data, 5);

    // 正常访问，没问题
    std::cout << s[2] << "\n";  // 输出 3

    // 越界访问，抛异常
    try {
        std::cout << s[10] << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

跑起来输出是这样的：

```text
3
捕获到异常: 下标越界了兄弟
```

到这里你可能觉得，这也没什么特别的嘛，`std::vector::at()` 不就是这样吗。别急，关键点在后面。

## 负数下标的问题——有符号无符号的坑

这里有一个容易忽略的陷阱。`operator[]` 接受的参数类型是 `std::size_t`，这是一个无符号整数。如果直接传一个 `-10` 进去，会发生什么？

```cpp
// 你以为你在传 -10，其实编译器会做隐式转换
// -10 作为无符号整数会变成一个巨大的正数
// s[-10] 实际上变成了 s[18446744073709551606] 之类的鬼东西
```

但是！如果你把参数类型改成有符号的 `ptrdiff_t`，那编译器就能在编译期帮你拦住一些明显的问题。或者说，如果你用 `std::span` 的标准实现，它对下标类型是有讲究的。

让我换个写法，把下标类型改成有符号的，这样负数就能被正确识别了：

```cpp
template<typename T>
class checked_span_v2 {
    T* ptr_;
    std::size_t size_;

public:
    checked_span_v2(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    // 注意这里用 ptrdiff_t（有符号）而不是 size_t（无符号）
    T& operator[](std::ptrdiff_t index) {
        // 先检查负数
        if (index < 0) {
            throw std::out_of_range("负数下标，你想干嘛");
        }
        // 再检查上界
        if (static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界了兄弟");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};
```

```cpp
int main() {
    int data[] = {1, 2, 3, 4, 5};
    checked_span_v2<int> s(data, 5);

    try {
        auto val = s[-10];  // 现在能正确捕获负数下标了
        std::cout << val << "\n";
    } catch (const std::out_of_range& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

输出：

```text
捕获到异常: 负数下标，你想干嘛
```

这里值得注意的是用 `size_t` 做下标类型时，负数传进来直接被隐式转成一个天文数字，然后要么刚好没越界读到垃圾数据（更可怕），要么越界抛异常但错误信息完全误导人。改成 `ptrdiff_t` 之后，负数就是负数，清清楚楚。

不过编译器能拦住的只是字面量负数这种最简单的情况。实际工程里，真正出问题的往往是那种在别处算出来的值——某个函数返回了一个 -1 表示失败，忘了检查就直接拿去当下标用了。这种运行时才能抓到，但至少有了这个检查，程序不会静默地写坏内存。

## 用另一个 span 的元素当大小——更贴近实际的场景

演讲里提到一个很实际的例子：你用一个 span 里的某个元素值，去作为另一个操作的大小参数。你其实并不知道那个值具体是多少，但除非它是一个合理的正整数，否则就应该被拦住。

```cpp
void process_with_dynamic_size(std::span<double> params, std::span<double> data) {
    // params[0] 里存的是我们想要处理的元素个数
    // 但我们不知道它到底是多少，可能是 5，可能是 -3，可能是 100000
    double count_raw = params[0];

    // 把它转成整数之前，先做检查
    if (count_raw < 0 || count_raw != static_cast<double>(static_cast<std::size_t>(count_raw))) {
        throw std::invalid_argument("params[0] 不是合法的正整数");
    }

    std::size_t count = static_cast<std::size_t>(count_raw);
    if (count > data.size()) {
        throw std::out_of_range("请求的元素个数超过了数据范围");
    }

    // 安全地处理前 count 个元素
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        sum += data[i];
    }
    std::cout << "前 " << count << " 个元素的和: " << sum << "\n";
}
```

```cpp
int main() {
    double params_good[] = {3.0};
    double params_bad[] = {-5.0};
    double data[] = {1.0, 2.0, 3.0, 4.0, 5.0};

    // 正常情况
    process_with_dynamic_size(params_good, data);

    // 异常情况
    try {
        process_with_dynamic_size(params_bad, data);
    } catch (const std::exception& e) {
        std::cout << "捕获到异常: " << e.what() << "\n";
    }

    return 0;
}
```

输出：

```text
前 3 个元素的和: 6
捕获到异常: params[0] 不是合法的正整数
```

这种写法在真实项目里特别常见。你从配置文件、网络协议、用户输入里拿到一个数字，然后用它去决定要访问多少元素。如果不做检查，这就是一个完美的安全漏洞。

## 类型推导：别再重复编译器已经知道的事

到这里，每次都要写 `checked_span<int>`、`checked_span<double>` 把元素类型重复一遍，而编译器明明从初始化参数就能推断出来。这就是 C++17 引入的 CTAD（Class Template Argument Deduction，类模板类型推导）要解决的问题。加一个推导指引就行了：

```cpp
template<typename T>
class checked_span_v3 {
    T* ptr_;
    std::size_t size_;

public:
    checked_span_v3(T* ptr, std::size_t size) : ptr_(ptr), size_(size) {}

    T& operator[](std::ptrdiff_t index) {
        if (index < 0 || static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界");
        }
        return ptr_[index];
    }

    const T& operator[](std::ptrdiff_t index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= size_) {
            throw std::out_of_range("下标越界");
        }
        return ptr_[index];
    }

    std::size_t size() const { return size_; }
};

// 推导指引：只要看到 (指针, 大小) 的组合，自动推导元素类型
template<typename T>
checked_span_v3(T*, std::size_t) -> checked_span_v3<T>;
```

现在写起来就清爽多了：

```cpp
int main() {
    int aa[100] = {};

    // 以前要这么写：checked_span_v3<int> s(aa, 100);
    // 现在编译器自己推断，aa 是 int*，所以 s 就是 checked_span_v3<int>
    // 这样，我们可以少写很多代码
    checked_span_v3 s(aa, 100);

    // 编译器非常清楚 s 是 checked_span_v3<int>，有 100 个元素
    // 我不需要再重复一遍 int 和 100
    s[0] = 42;
    std::cout << s[0] << "\n";  // 42

    // 配合 range-based for 循环，现代 C++ 的常规操作
    // 比以前写 for (int i = 0; i < 100; ++i) 舒服太多了
    std::size_t count = 0;
    for (auto& val : aa) {
        if (val != 0) ++count;
    }
    std::cout << "非零元素个数: " << count << "\n";  // 1

    return 0;
}
```

类型推导看似是"语法糖"，但在项目里写了上百个 span 相关的代码之后会发现，少写一个 `int` 不是省了三个字符的事，而是当你以后把 `int` 改成 `int64_t` 的时候，只需要改一处，而不是满世界找哪里漏写了。

这就是泛型编程的一个核心哲学：不要重复编译器已经知道、你也已经知道的事情。

## 子 span 和从指针构造——更完整的工具箱

光有一个完整的 span 还不够用。实际开发中，经常需要从一个大 span 里切出一小块来，或者从一个裸指针构造一个 span。

先说从指针构造的场景。既然 span 的意义是安全，那从裸指针构造 span 本身不就是 Unsafe 的吗？确实没办法检查那个指针到底是不是真的指向那么多元素——编译器不知道，运行时也没法验证。但关键在于：**从指针构造 span 这件事本身，在代码审查和静态分析工具面前会显得极其突兀**。如果一个项目规范要求"所有数组访问必须通过 span"，那一写 `span(ptr, n)` 这种代码，审查的人一眼就能看到：这里有一个不安全的边界，需要重点看。这比满地都是 `ptr[i]` 要好管得多。

```cpp
#include <span>

// 从指针构造 span 的辅助函数
// 故意写成函数形式，让它在代码审查中更显眼
template<typename T>
std::span<T> make_span_from_ptr(T* ptr, std::size_t size) {
    return std::span<T>(ptr, size);
}

// 取前 n 个元素的子 span
template<typename T>
std::span<T> take_front(std::span<T> s, std::size_t n) {
    if (n > s.size()) {
        throw std::out_of_range("take_front: n 超过了 span 的大小");
    }
    return s.subspan(0, n);
}

// 取某个范围内的子 span
template<typename T>
std::span<T> take_range(std::span<T> s, std::size_t offset, std::size_t count) {
    if (offset > s.size() || count > s.size() - offset) {
        throw std::out_of_range("take_range: 范围超出");
    }
    return s.subspan(offset, count);
}
```

```cpp
int main() {
    int data[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    // 从指针构造——代码审查时这行会特别显眼
    auto full = make_span_from_ptr(data, 10);

    // 取前 3 个
    auto front3 = take_front(full, 3);
    std::cout << "前3个: ";
    for (auto v : front3) std::cout << v << " ";
    std::cout << "\n";

    // 取下标 2 到 5 的范围（3 个元素）
    auto mid = take_range(full, 2, 3);
    std::cout << "中间3个: ";
    for (auto v : mid) std::cout << v << " ";
    std::cout << "\n";

    // 越界测试
    try {
        auto bad = take_front(full, 20);
    } catch (const std::out_of_range& e) {
        std::cout << "捕获: " << e.what() << "\n";
    }

    return 0;
}
```

输出：

```text
前3个: 10 20 30
中间3个: 30 40 50
捕获: take_front: n 超过了 span 的大小
```

注意我在 `take_range` 里写边界检查的方式：`count > s.size() - offset`。这里没有用 `offset + count > s.size()`，是因为后者在有符号无符号混用的时候可能溢出。虽然在这个场景下 `offset` 和 `count` 都是 `size_t` 不会溢出，但养成用减法而不是加法做范围检查的习惯，能让你在别的地方少踩坑。这也是演讲里提到的"使用数字而不是混用有符号无符号"的思路。

同样，这些辅助函数也可以加上推导指引，让调用处不用写模板参数。两行推导指引的事，但代码读起来就完全不一样了——你看到的就是 `take_front(full, 3)`，而不是 `take_front<int>(full, 3)`。编译器知道 `full` 是 `span<int>`，它就能推出返回值也是 `span<int>`，你不需要帮它操心。

到这里，span 的基本安全访问、类型推导、子 span 切片都搞通了。代码看起来相当干净，没有多余的重复，该检查的地方都检查了。但事情还没完——后面还有更复杂的场景。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup"
    title="The Design and Evolution of C++"
    publisher="Addison-Wesley"
    :year="1994"
    chapter="Chapter 15: Templates"
    url="https://www.stroustrup.com/dne.html"
  />
  <ReferenceItem
    :id="2"
    author="Erwin Unruh"
    title="Prime Number Computation"
    :year="1994"
  />
  <ReferenceItem
    :id="3"
    author="Todd Veldhuizen"
    title="Using C++ Template Metaprograms"
    publisher="C++ Report"
    :year="1995"
    chapter="Vol. 7, No. 4, pp. 36-43"
  />
  <ReferenceItem
    :id="4"
    author="Alexander Stepanov, Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories"
    :year="1995"
    chapter="TR95-11(R.1)"
    url="https://www.stepanovpapers.com/stl.pdf"
  />
  <ReferenceItem
    :id="5"
    author="Kristen Nygaard (cited by Bjarne Stroustrup)"
    title="If you need a PhD to use it, you have failed"
    :year="2001"
    chapter="Stroustrup 引用于 CppCon 2025 演讲 Concept-Based Generic Programming in C++, §1"
  />
  <ReferenceItem
    :id="6"
    author="cppreference.com"
    title="std::span::subspan"
    :year="2020"
    url="https://en.cppreference.com/w/cpp/container/span/subspan"
  />
  <ReferenceItem
    :id="7"
    author="Dennis M. Ritchie"
    title="Variable-Size Arrays in C"
    :year="1990"
    url="https://www.nokia.com/bell-labs/about/dennis-m-ritchie/vararray.pdf"
  />
</ReferenceCard>

### 延伸阅读

- Stroustrup, B. ["A History of C++: 1979–1991"](https://www.stroustrup.com/hopl2.pdf). *HOPL-II*, 1993. — C++ 语言早期历史的权威记录，涵盖模板设计决策的完整上下文。
- Lourseyre, C. ["[History of C++] Templates: from C-style macros to concepts"](https://belaycpp.com/2021/10/01/history-of-c-templates-from-c-style-macros-to-concepts/). *Belay the C++*, 2021. — 对 Stroustrup *D&E* 第 15 章的优质二次整理，梳理了从 C 宏到 C++20 concepts 的完整演进脉络。
- Stroustrup, B. *The Design and Evolution of C++*. Addison-Wesley, 1994. — C++ 语言设计决策的权威解读，第 15 章专门讨论模板的设计动机与取舍。
