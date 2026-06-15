---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 演讲笔记 —— 模板不该被孤立编译、Concepts 作为编译期函数构建类型系统、接口继承与 Concepts 互补、未来生态建设
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 28
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: 模板编译模型与未来展望
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
---
# 模板不该被孤立编译

前面聊到用 concepts 给模板加约束的时候，我脑子里一直盘旋着一个问题：如果我对 `advance` 的参数加上精确的 concept 约束，比如要求必须是 `random_access_iterator`，那 `input_iterator` 不就被拒之门外了吗？我是不是得针对不同迭代器类别写一堆重载，每个重载里用不同的方式推进？这岂不是又回到了接口不稳定的老路上——每多支持一种迭代器类型，我就得回去改 `advance` 的声明？

说实话这个问题困扰了我挺久的。我之前一直觉得，有了 concepts 之后，"把模板拆开孤立编译"应该是理想状态——每个模板独立检查，独立通过，然后组装。但这次看到这个推进迭代器的例子，我整个人被点醒了：原来不能这么干，甚至不应该这么干。

## 先看一个看似简单的问题

你可能会问，什么叫"孤立编译模板"？我理解的就是：编译器在看到模板定义的时候，仅凭模板签名上的 concept 约束来判断这个模板是否合法，不去看实际调用时传进来的具体类型能提供什么操作。

听起来很美好对吧？但问题马上就来了。

我们来看 `std::advance` 这个函数。它的作用是把迭代器向前推进 n 步。对于不同类别的迭代器，推进方式完全不同：`random_access_iterator` 可以直接 `+= n`，一步到位；而 `input_iterator` 没有 `+=`，只能一个一个 `++`。

我之前一直以为 `input_iterator` 没有 `+=` 是标准里的某种"缺陷"，或者至少是一个应该被修正的限制。但事实并非如此——`input_iterator` 不提供 `+=` 是有充分理由的，它代表的就是"只能逐个前进而不能跳跃"的抽象。这是特性，不是缺陷。

## 动手写个例子感受一下

我写了一段代码来验证这个行为，跑在我的 Arch Linux WSL 上，编译器是 GCC 16.1.1，开了 `-std=c++20`。

```cpp
#include <vector>
#include <list>
#include <iostream>

// 一个简化版的 advance，模拟标准库的行为
template<typename Iter>
void my_advance(Iter& it, int n) {
    // 如果迭代器支持 +=，直接跳
    if constexpr (requires(Iter i, int m) { i += m; }) {
        it += n;
    } else {
        // 否则一步一步走
        for (int i = 0; i < n; ++i) {
            ++it;
        }
    }
}

int main() {
    // vector 的迭代器是 random_access_iterator，支持 +=
    std::vector<int> vec = {10, 20, 30, 40, 50};
    auto vit = vec.begin();
    my_advance(vit, 2);
    std::cout << *vit << "\n";  // 输出 30

    // list 的迭代器是 bidirectional_iterator，不支持 +=
    std::list<int> lst = {10, 20, 30, 40, 50};
    auto lit = lst.begin();
    my_advance(lit, 2);
    std::cout << *lit << "\n";  // 输出 30

    return 0;
}
```

跑一下，输出完全符合预期：两个 30。你看，同一个 `my_advance`，对 `vector` 用了 `+=`，对 `list` 用了循环 `++`。这一切之所以能工作，恰恰是因为模板在被实例化之前，不会去孤立地检查"你到底支不支持 `+=`"——它等到看到具体类型的那一刻，才通过 `if constexpr` 做出选择。

## 如果真的孤立编译会怎样？

现在我们假设一下，如果 C++ 将来实现了模板的孤立编译，而且没有豁免条款，会发生什么？

编译器在看到 `my_advance` 的定义时，会检查里面的每一行代码是否对约束所描述的类型都合法。如果我的约束写的是 `input_iterator`，而 `input_iterator` 的定义里不包含 `+=`，那编译器就会直接拒绝 `it += n` 这行——即使它在运行时永远不会被执行到（因为被 `if constexpr` 分支挡住了）。

那我就不得不把代码拆成两个重载：

```cpp
// 给 random_access_iterator 用的版本
template<std::random_access_iterator Iter>
void my_advance(Iter& it, int n) {
    it += n;
}

// 给其他迭代器用的版本
template<std::input_iterator Iter>
    requires (!std::random_access_iterator<Iter>)
void my_advance(Iter& it, int n) {
    for (int i = 0; i < n; ++i) {
        ++it;
    }
}
```

看起来也没那么糟？但你仔细想想，这其实是把"一个算法根据类型能力做不同选择"这件事，从算法内部推到了接口层面。每多一种需要特殊处理的迭代器类别，我就得多加一个重载。接口在膨胀，维护成本在上升，而且本质上是在重复同一个逻辑。

更关键的是性能问题。如果 `advance` 不能对 `random_access_iterator` 使用 `+=`，而只能用 `++` 循环，那复杂度就从 O(1) 变成了 O(n)。这在一个算法内部被调用的时候，如果外层循环也是 O(n) 的，整体就从 O(n) 暴涨到 O(n^2)。对于大数据量来说，这是致命的。

STL 之所以能高效，很大一部分原因就是这种"在模板内部根据类型能力做分支选择"的能力。如果孤立编译把这条路堵死了，STL 的性能优势就会大打折扣。

## 那 concepts 到底有什么用？

说到这里你可能会问：那 concepts 岂不是没什么用了？之前说能更早捕获错误，现在又说不能孤立检查，这不是自相矛盾吗？

我一开始也懵了，但想明白之后发现其实不矛盾。Concepts 的价值在于：当系统中确实没有满足约束的类型时，错误会在更早的时候被捕获，而且错误信息会清晰得多——它告诉你"你传进来的类型不满足 `input_iterator`"，而不是给你吐出一屏幕看不懂的模板实例化回溯。

但"更早捕获错误"和"孤立编译模板"是两回事。模板仍然需要看到具体的类型才能做最终的合法性判断，只是 concepts 让这个判断的失败信息变得可读了。从这个意义上说，模板一直都是类型安全的——只不过以前报错的时候你根本看不懂，现在能看懂了。

## 务实而不是教条

所以结论是什么？至少在现阶段，我们不应该追求模板的孤立编译。如果将来 C++ 真的实现了这个特性，那必须要有某种豁免机制，让像 `advance` 这种"根据类型能力在内部做分支"的写法能够合法存在。因为说实话，我们日常用的软件基础设施里，这种模式到处都是。

这让我想起自己以前学模板的时候，总觉得"约束越严格越好"，恨不得把每个模板参数都锁死。但实际写多了才发现，泛型编程的精髓恰恰在于：你描述一个最小化的需求，然后在实现内部灵活地适应不同能力的类型。这不是偷懒，这是务实。

## 回到泛型编程的本质

折腾完这些之后，我回头重新理解了一下泛型编程到底是什么。它不是什么玄学，它就是编程本身——只不过是用最通用、最高效、写起来最舒服的方式来做。这里说的"concept"不是指 C++20 的那个语言特性，而是指你对某个想法的一般性抽象：迭代器是什么、可调用对象是什么、范围是什么。

这些东西不是 C++ 发明的。你去翻 Alexander Stepanov 和 Daniel E. Rose 的 *From Mathematics to Generic Programming*（中文版：《数学与泛型编程：高效编程的奥秘》），里面全是纯数学——代数结构、公理、定理。如果你不喜欢数学，那本书确实读起来会很痛苦（我承认我翻了几页就先放下了）。但核心思想其实很朴素：找到不同类型之间的共同代数结构，然后针对这个结构写算法，而不是针对某个具体类型。

而且泛型编程从一开始就内置了对类型的统一使用——作用域怎么管、名字怎么解析、对象怎么创建和销毁，这些在泛型代码里和在任何代码里一样重要。它早在 C++ 出现之前就被引入了，C++ 只是把这套思想用模板这个机制表达了出来。

到这里我终于把"模板为什么不该被孤立编译"这件事想通了。回头看看，其实也不复杂——就是不要为了理论上的纯洁性，去牺牲实际工程中的灵活性和性能。说到底，我们是写代码解决问题的，不是写论文证明纯洁性的。

---

# Concepts：在编译期构建你自己的类型系统

说实话，看到这个结论的时候我恍然大悟。之前学 Concepts 的时候，我一直把它当成"更优雅的 SFINAE"，觉得它就是个约束模板参数的语法糖，写起来比 `std::enable_if` 好看而已。但折腾了一晚上之后我才搞明白，Concepts 的本质其实是**编译期函数**——它接受类型和值作为参数，返回一个 bool，告诉你某个类型满不满足某个条件。这个认知转变之后，后面很多东西一下子就通了。

## 先搞清楚：Concepts 到底在做什么

我之前一直有个误解，以为 Concepts 是在描述"一个类型长什么样"，比如"它必须有 `begin()` 和 `end()`"。但实际上不是的。Concepts 描述的是"一个泛型函数对参数的要求"，而且它**不关心这个要求是怎么被满足的**。这个区别非常关键，我一开始完全没意识到。

什么意思呢？比如你写一个 Concept 要求"能做加法"，你不需要说"通过 `operator+` 实现"还是"通过某个成员函数实现"，你只说"能做加法就行"。编译器自己去判断。这跟经典面向对象编程里"必须继承自某个基类、必须重写某个虚函数"的思路完全不同——OOP 是自上而下地规定"你怎么提供"，而 Concepts 是自下而上地说"我需要什么"。

而且 Concepts 可以接受多个参数，不只是一个类型参数。这意味着你可以表达"类型 A 和类型 B 之间能做某种操作"这种跨类型约束，这在传统 OOP 里几乎没法优雅地表达。

## 动手写几个 Concept 感受一下

我的实验环境是 Arch Linux WSL，GCC 16.1.1，编译命令加 `-std=c++20 -Wall -Wextra`。下面这段代码是我自己写的，用来验证"Concepts 是编译期函数"这个理解：

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// 最简单的 Concept：接受一个类型参数，返回 bool
template<typename T>
concept HasSize = requires(T t) {
    { t.size() } -> std::convertible_to<std::size_t>;
};

// 接受两个类型参数的 Concept：表达跨类型约束
template<typename T, typename U>
concept CanAdd = requires(T a, U b) {
    a + b;  // 只要求 a + b 是合法表达式
};

// 接受类型参数和值参数的 Concept
// sizeof 在编译期求值，所以这里不需要运行时信息
template<typename T, std::size_t N>
concept IsLargeType = (sizeof(T) >= N);

void test_single_param(HasSize auto& container) {
    std::cout << "size = " << container.size() << "\n";
}

// 双参数 concept 不能用 "CanAdd auto" 语法——那只对单参数 concept 有效
// 必须用显式的 requires 子句，把两个模板参数传进去
template<typename T, typename U>
    requires CanAdd<T, U>
void test_cross_add(T a, U b) {
    auto result = a + b;
    std::cout << "a + b = " << result << "\n";
}

int main() {
    std::string s = "hello";
    test_single_param(s);  // OK，string 有 size()

    // test_single_param(42);  // 编译错误：int 不满足 HasSize

    test_cross_add(10, 20);      // OK，int + int -> int
    test_cross_add(10, 3.14);    // OK，int + double -> double

    // test_cross_add("hello", 42);  // 编译错误：const char* + int 不合法

    static_assert(IsLargeType<double, 4>);  // sizeof(double) == 8 >= 4
    static_assert(!IsLargeType<char, 4>);   // sizeof(char) == 1 < 4
}
```

跑一下就能看到，`HasSize` 接受一个类型参数，`CanAdd` 接受两个类型参数，`IsLargeType` 更有意思，它同时接受类型和编译期值。这三种参数形式可以自由组合，表达能力非常强。

不过有一个坑我踩了半天：多参数 concept 不能像单参数那样直接写在 `auto` 前面当约束（比如 `CanAdd auto a` 会直接编译报错，因为 `CanAdd` 需要两个模板参数而你只给了一个）。多参数 concept 必须用显式的 `requires` 子句来把参数传进去。单参数 concept 就没有这个限制，`HasSize auto& container` 写起来非常自然。

## 用 Concepts 做重载：比普通重载还简单

这块是我之前最懵的地方。我以前觉得模板重载是个噩梦——你要用 SFINAE 做偏特化，报错信息长达三屏，而且规则复杂到让人怀疑人生。但用 Concepts 做重载，规则反而比普通函数重载**更简单**。

我写了个例子来验证这三种情况：

```cpp
#include <iostream>
#include <concepts>
#include <vector>
#include <list>

// 约束 A：可排序的容器
template<typename T>
concept SortableContainer = requires(T t) {
    requires std::ranges::range<T>;
    requires std::totally_ordered<typename T::value_type>;
};

// 约束 B：可排序的随机访问容器（比 A 更严格）
template<typename T>
concept RandomAccessSortable = SortableContainer<T> && requires(T t) {
    requires std::random_access_iterator<typename T::iterator>;
};

// 情况1：只有一个匹配
void process(SortableContainer auto& c) {
    std::cout << "sortable container\n";
}

// 情况2：两个都匹配，但一个是另一个的子集 -> 选最严格的
void process(RandomAccessSortable auto& c) {
    std::cout << "random access sortable container\n";
}

int main() {
    std::list<int> lst = {3, 1, 2};
    std::vector<int> vec = {3, 1, 2};

    process(lst);  // 只匹配 SortableContainer -> 输出 "sortable container"
    process(vec);  // 两个都匹配，但 RandomAccessSortable 更严格 -> 输出 "random access sortable container"
}
```

你看，规则就三条，清清楚楚：只有一个匹配就直接用；两个匹配且一个是另一个的子集就选更严格的；其他情况都是错误。没有普通重载里那些隐式转换排名、歧义消除的复杂规则。我在这卡了半天，之前总以为 Concepts 重载会有什么隐藏的坑，结果回头看看原理真的很简单。

## 真正让我亮了的部分：扩展 C++ 的类型系统

到这里才是让我觉得真正有意思的。之前我一直觉得 C++ 的类型系统是固定的——int 就是 int，double 就是 double，窄化转换就是不安全的，你只能绕着走或者用 `-Wnarrowing` 警告一下。但 Concepts 让你可以在编译期**构建自己的类型系统**，把原本需要运行时检查的东西，在编译期就拦住。

我顺着这个思路自己写了一个 `SafeNumericConvert` 的 Concept，用来在编译期区分"安全的数值转换"和"可能丢数据的窄化转换"：

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <limits>
#include <stdexcept>

// 编译期判断：从 From 到 To 的转换是否可能丢失数据
// 安全条件：To 严格更宽，或者同宽且符号性相同
template<typename From, typename To>
concept SafeNumericConvert =
    std::integral<From> && std::integral<To> &&
    (sizeof(From) < sizeof(To) ||
     (sizeof(From) == sizeof(To) &&
      std::is_signed_v<From> == std::is_signed_v<To>));

// 只有安全转换才能编译通过的包装函数
template<typename To, typename From>
    requires SafeNumericConvert<From, To>
constexpr To safe_cast(From val) {
    return static_cast<To>(val);
}

// 运行时才检查的版本：处理编译期无法判断的情况
template<typename To, typename From>
    requires (std::integral<From> && std::integral<To> && !SafeNumericConvert<From, To>)
To checked_cast(From val) {
    if constexpr (std::is_signed_v<From> && std::is_unsigned_v<To>) {
        if (val < 0) throw std::overflow_error("negative to unsigned");
    } else if constexpr (std::is_unsigned_v<From> && std::is_signed_v<To>) {
        // unsigned -> signed 同 size：0 永远合法，只需检查上界
        if (val > static_cast<From>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    } else {
        // signed -> signed 缩小 或其他情况，用公共类型做安全比较
        using Common = std::common_type_t<From, To>;
        if (static_cast<Common>(val) < static_cast<Common>(std::numeric_limits<To>::min()) ||
            static_cast<Common>(val) > static_cast<Common>(std::numeric_limits<To>::max())) {
            throw std::overflow_error("narrowing conversion would overflow");
        }
    }
    return static_cast<To>(val);
}

int main() {
    int x = 42;
    auto y = safe_cast<long long>(x);       // OK，int -> long long 是安全的
    // auto z = safe_cast<char>(x);          // 编译错误！int -> char 可能窄化
    // auto u = safe_cast<int>(uint32_t(0)); // 编译错误！uint32_t -> int32_t 同 size 但 unsigned -> signed

    // 需要运行时检查的场景，用 checked_cast
    auto w = checked_cast<char>(x);         // 运行时检查，42 在 char 范围内，OK
    // auto q = checked_cast<char>(300);     // 运行时抛异常
}
```

看到没有？`safe_cast` 在编译期就把窄化转换拦住了，根本不需要跑到运行时才发现问题。而 `checked_cast` 只在编译期无法确定安全性的时候才引入运行时开销。这就是"扩展 C++ 类型系统"的意思——你用 Concepts 在 C++ 原有的类型规则之上，加了一层你自己定义的类型安全检查。

以前我觉得 template 是黑魔法，看到尖括号就头疼，报错信息一屏一屏的根本不想看。但现在回头看，Concepts 把模板从"编译器内部实现细节"拉到了"你自己的类型系统设计语言"这个层面。你不再是在跟编译器搏斗，而是在**设计规则**。

到这里终于搞通了。Concepts 不是 SFINAE 的替代品，不是 `enable_if` 的语法糖，它是让你在编译期写函数、做判断、选分支、构建类型约束的一整套机制。而这套机制最终指向的目标是：让你能在 C++ 已有的类型系统之上，按你自己的领域需求，长出新的类型规则来。回头看看其实没那么难，但要是没人点破"编译期函数"这层窗户纸，我可能还要在 SFINAE 的泥潭里挣扎很久。

---

# concept 里传值参数：打破最后的思维定势

搞通了"Concepts 是编译期函数"之后，我突然想到一个之前一直想不通的问题：既然 concept 本质上就是返回 `bool` 的 constexpr 变量模板，那它能不能接受非类型参数呢？答案是可以的，而且写出来非常自然。

## 从最基础的地方开始：concept 到底是什么

我之前一直把 concept 当成一种"特殊的类型约束语法"，觉得它和普通的函数是完全不同的两个世界。这个误解其实挺害人的，因为它让你无法理解很多更高级的用法。

我们来看一个最普通的 concept 定义：

```cpp
#include <concepts>
#include <type_traits>

// 我以前写的 concept，长这样——只接受类型参数
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

这看起来很"类型专属"对吧？但如果你把 concept 翻译成它真正的面貌，它其实就是一个返回 `bool` 的 constexpr 变量模板（variable template）。上面这段代码在编译器内部看待它的方式，大致等价于：

```cpp
template<typename T>
constexpr bool Addable_v = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

既然它是 constexpr 的，既然它是模板，那它为什么不能接受非类型参数呢？没有任何理由禁止这件事。我之前想不通，纯粹是因为看多了 `typename T` 的写法，形成了思维定势。

## 动手试试：在 concept 里传值

搞明白上面那层关系之后，写代码就顺理成章了。我们来定义一个 concept，它不仅约束类型，还约束一个具体的数值条件：

```cpp
#include <iostream>
#include <concepts>
#include <array>

// 这个 concept 接受一个类型参数和一个值参数
// 它表达的含义是：T 是一个整数类型，且值 v 必须大于等于 0
template<typename T, T v>
concept NonNegativeIntegral = std::integral<T> && (v >= 0);

// 用它来约束一个函数
template<typename T, T v>
    requires NonNegativeIntegral<T, v>
constexpr T safe_value() {
    return v;
}

int main() {
    // 这个没问题，int 类型，值是 42，满足 >= 0
    std::cout << safe_value<int, 42>() << "\n";

    // 这个也没问题，值是 0，边界情况
    std::cout << safe_value<int, 0>() << "\n";

    // 下面这行如果取消注释，编译会直接报错
    // 因为值 -1 不满足 v >= 0 的约束
    // std::cout << safe_value<int, -1>() << "\n";

    return 0;
}
```

编译运行，输出 `42` 和 `0`，完全符合预期。你可能会说，这看起来和非类型模板参数的约束没什么区别啊？确实，在简单场景下它和非类型模板参数的 `static_assert` 或者 `requires` 子句效果类似。但 concept 的优势在于它可以被命名、被组合、被重载，这就完全不一样了。

## 更有意思的用法：用值参数做 concept 重载

既然 concept 可以带值参数，那我能不能用不同的值来触发不同的重载？答案是可以的，而且写出来非常清晰：

```cpp
#include <iostream>
#include <string>

// 定义两个 concept，用不同的值来区分
template<int N>
concept IsSmall = (N <= 10);

template<int N>
concept IsLarge = (N > 10);

// 当 N 小于等于 10 时走这个实现
template<int N>
    requires IsSmall<N>
std::string describe_size() {
    return "small: " + std::to_string(N);
}

// 当 N 大于 10 时走这个实现
template<int N>
    requires IsLarge<N>
std::string describe_size() {
    return "LARGE: " + std::to_string(N);
}

int main() {
    std::cout << describe_size<3>() << "\n";   // 输出: small: 3
    std::cout << describe_size<50>() << "\n";  // 输出: LARGE: 50
    return 0;
}
```

看到这里我突然想明白了一件事。以前如果要做这种基于编译期数值的分发，我大概率会写 `if constexpr`，虽然也能用，但那种写法是把所有分支塞进同一个函数体里，数值分支一多，函数就会变得又长又难读。用 concept 重载的话，每个分支是独立的函数，逻辑完全隔离，清爽太多了。

## constexpr vs concept：到底什么时候用哪个？

concept 里能写值逻辑，constexpr 函数里也能写值逻辑，那什么时候该用哪个？

这个问题我想了挺久，后来想通了一个很简单的判断标准。你问自己一个问题：这个计算的结果是什么？如果结果是一个值，比如算出来是个 `7`，那它天然就应该是一个 constexpr 函数。如果结果是一个"对类型的判定"，是 yes 或 no，那它才适合写成 concept。

举个很直观的例子。假设我要在编译期算一个整数的阶乘：

```cpp
// 结果是值，用 constexpr 函数，天经地义
constexpr int factorial(int n) {
    int result = 1;
    for (int i = 2; i <= n; ++i) {
        result *= i;
    }
    return result;
}

static_assert(factorial(5) == 120);
```

你不会把阶乘写成一个 concept，因为阶乘的结果不是一个布尔值，它不是一个约束。反过来，如果你要表达"这个类型是不是可以用来做某种数值计算"，那才是 concept 的活：

```cpp
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<Numeric T>
T compute(T x) {
    return x * x + 1;
}
```

所以本质上，constexpr/consteval 解决的是"编译期算值"的问题，concept 解决的是"编译期判类型"的问题。它们都是编译期求值的机制，内部实现上也有很多相似之处——毕竟 concept 的约束表达式本身就是在 constexpr 上下文里求值的——但它们的职责边界很清晰。

不过话说回来，正如我前面演示的，当 concept 带上值参数之后，这个边界变得稍微模糊了一点。因为你的 concept 里确实在做一些数值计算（比如 `v >= 0`），只是最终结果被归约为一个布尔值。我觉得这种模糊是好事，它给了我们更多的表达力，只要你自己心里清楚你在做什么就行。

## 顺便说一句 consteval 和 constinit

既然提到了 constexpr，我顺带提一嘴 C++20 引入的另外两个关键字，因为它们经常被放在一起讨论，我之前也经常搞混。

`consteval` 叫"立即函数"，意思是这个函数必须在编译期执行，连运行期调用的可能性都不给你。而 `constexpr` 函数是"尽量在编译期执行，但如果参数不是编译期常量，也允许在运行期执行"。`constinit` 则是保证变量在编译期初始化，但不要求之后不能被修改（和 `const` 不同）。这三个东西各有各的用途，但我目前实际项目里用得最多的还是 `constexpr`，`consteval` 在一些对性能极其敏感的嵌套模板场景里用过几次。

---

# 接口继承 vs Concepts：不是谁替代谁的问题

有人问了一个我之前也一直纠结的问题：既然 C++20 有了 concepts，那以前那种只有纯虚函数的接口类是不是就可以淘汰了？concepts 能不能完全覆盖接口继承的功能？

说实话，我学 concepts 的时候也产生过这种想法。当时觉得，concepts 多优雅啊，编译期检查，零运行时开销，不用写一堆虚函数和 vtable，简直是降维打击。但听完这个回答我才意识到自己想得太简单了。Bjarne Stroustrup 的回答很直接：不，concepts 不能完全覆盖接口继承。而且他自己使用接口继承的频率远高于实现继承。这里的关键区分在于，接口继承是你定义一个类"长什么样"，而实现继承是你定义一个类"怎么干活"。前者在 C++ 里一直是个好实践，后者才是大家经常吐槽的对象。Bjarne Stroustrup 说指定接口有两种根本不同的方式：一种是固定的、严格定义的接口，另一种是灵活的、开放的接口。这两种你都需要，它们解决的是不同的问题。

我之前一直没把这个区分想清楚。现在回头看，固定接口就是那种"你必须提供这五个方法，签名必须完全匹配，少一个都不行"的情况。典型的就是插件系统——主程序定义好一个 `IPlugin` 接口，所有插件必须精确实现它。这种场景下，虚函数接口类其实是天然契合的，因为接口本身就是一份"契约"，白纸黑字写清楚你需要什么。

而灵活接口更像是 concepts 擅长的领域。你不需要精确匹配某个方法签名，你只需要满足某些"约束条件"。比如你不需要一个叫 `draw` 的方法，你只需要"能被传给某个接受流式输出的函数"就行。这种宽松的、基于能力的约束，用 concepts 表达确实更自然。换而言之，比 is-a 关系更加宽松，你只要"能做到"就好。

至于什么时候用哪种，就我自己的实践来说，我现在的粗浅判断是这样的：如果你的接口是给"人"看的——也就是另一个开发者需要明确知道"我要实现哪些方法"，那用接口类更清晰，因为 IDE 会直接提示你缺了什么纯虚函数没实现。如果你的接口是给"编译器"看的——也就是在模板里做约束，让类型检查更早报错、报错信息更可读，那 concepts 更合适。

---

# Concepts 之后是什么？——从"语言还能加什么"到"我们该怎么用"


下一个阶段不是让语言变得更完美，而是**写更多真正用好 concepts 的库**。

演讲者说得很实在——论文里确实列了大约十件"可能做到的事情"，但他认为真正需要的不是这些。我们需要的是**在实践中积累经验**，看看 concepts 和语言的其它部分（比如约束偏序、SFINAE 的交互、模块的配合）在实际大型代码库里到底表现如何。这个观察期可能需要好几年。

我现在的理解是：语言特性不是越先进越好，而是要由**现实世界确实存在的问题**来驱动。如果没有人真正在用 concepts 写库、遇到真实的痛点，那提案再多也只是纸上的玩具。

## 另一个很实际的问题：标准库该不该加更多 concepts 约束

现场还有人问了一个特别接地气的问题：`std::vector` 的类型参数现在基本没有约束，那要不要加个 `std::copyable` 之类的 concept 来限制一下？

这个问题我之前自己也想过。我写过这样的代码：

```cpp
#include <vector>
#include <string>

int main() {
    // 这玩意儿能编译通过，但你基本没法对它做任何有意义的事
    std::vector<std::unique_ptr<int>> v;
    // v.push_back(std::make_unique<int>(42));  // 编译错误
    // 但 vector 本身的实例化是完全合法的
}
```

我当时就觉得奇怪——`unique_ptr` 不可拷贝，你把它放进 vector 里，大部分操作都会炸，为什么标准库不在声明的时候就拦住？

演讲者的回答让我理解了标准库维护者的苦衷。他说"你必须非常小心"，因为**多年来人们一直在做一些事情，仅仅因为他们能做**。他举了个例子：有人用 `std::accumulate` 来拼接字符串。这玩意儿本来是给数值类型做的归约，但因为你没加约束，它就能编译，于是大家就这么用了。

现在如果你突然给 `std::accumulate` 加上 `std::arithmetic` 约束，那所有用 `accumulate` 拼字符串的代码全炸了。你不知道会破坏谁的代码。所以标准委员会面临的选择是：要么提供两个重载（数值版和非数值版），要么什么都不动。无论哪种，都不是随便能决定的。

我跑了个小实验验证一下这个"accumulate 拼字符串"到底是怎么回事：

```cpp
#include <numeric>
#include <string>
#include <vector>

int main() {
    std::vector<std::string> words = {"hello", " ", "world"};

    // accumulate 的默认操作是 std::plus，对 string 来说就是 operator+
    // 初始值 "" 是 string，所以整个推导就顺着走下去了
    auto result = std::accumulate(
        words.begin(), words.end(),
        std::string("")
    );

    // result == "hello world"，确实能跑
}
```

你看，这代码能跑，而且结果是对的。但如果 C++20 的 `<numeric>` 直接给 `accumulate` 加了 `std::integral` 或 `std::floating_point` 约束，这段代码就当场去世。这种"历史包袱"不是你想清就能清的。（这是没办法的事情！）

## 所以 concepts 的"下一阶段"到底是什么

把这两个问题合在一起看，我现在的理解是这样的：

concepts 作为语言特性，已经落地了。C++20 给了我们 `<concepts>` 头文件中的标准 concept、`requires` 子句、`requires` 表达式、约束偏序——工具箱已经够用了。**瓶颈不在语言，在生态。**

什么叫"生态"？就是：

第一，标准库自身要更合理地使用 concepts。C++20 已经做了很多——`std::ranges` 里的算法几乎全部用 concepts 约束了迭代器类型、投影类型等等。但像 `std::vector` 这种老古董，改起来牵一发动全身，需要极度谨慎。

第二，我们这些写应用代码、写第三方库的人，要开始在**自己的接口**里用 concepts。不是在博客里写几个玩具例子，而是在真正的项目里，把模板参数用 concept 约束起来，把 `static_assert` 替换掉，把 SFINAE 的 `std::enable_if` 地狱干掉。然后在这个过程中积累经验——哪些 concept 粒度合适、约束写在哪里最清晰、怎么给用户最好的错误信息。

第三，等这些实践经验积累够了，才会知道"语言还缺什么"。而不是现在坐在椅子上空想。
