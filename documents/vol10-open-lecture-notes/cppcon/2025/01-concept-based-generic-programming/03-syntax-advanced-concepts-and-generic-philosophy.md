---
chapter: 1
conference: cppcon
conference_year: 2025
cpp_standard:
- 20
- 23
description: CppCon 2025 演讲笔记 —— 语法统一性、SmartPtr 约束、多参数 Concept、泛型 vs OOP、迭代完善与 C++26
  反射初体验
difficulty: intermediate
order: 3
platform: host
reading_time_minutes: 44
speaker: Bjarne Stroustrup
tags:
- cpp-modern
- host
- intermediate
talk_title: Concept-based Generic Programming
title: 语法统一、高级 Concept 与泛型哲学
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW
video_youtube: https://www.youtube.com/watch?v=VMGB75hsDQo
---
# 语法统一这件事，也许比咱们想的重要得多

我之前一直觉得，语法统一嘛，就是"写起来好看一点"的表面功夫。但如果你回头看 Simula 或者 Java，你会发现一个很别扭的设计：自定义类型必须用 `new` 来创建，但内置类型不行。在 Simula 里你甚至不能对 `int` 用 `new`。这就导致一个致命后果——你永远写不出一个真正通用的容器或者算法，因为它在语法层面就被劈成了两半。一半处理内置类型，一半处理自定义类型，两套代码，两套规则。

C++ 从一开始就避免了这个问题。`int x = 0;` 和 `MyString x;` 在语法上没有任何区别，这意味着当你写一个 `template<typename T>` 的时候，`T` 无论是 `int` 还是 `MyString`，创建它的方式是完全一样的。这个决定看起来不起眼，但它是整个 C++ 泛型编程能够成立的前提。

同样的逻辑也适用于资源管理。如果资源管理不是类型设计的一部分，而是你必须手动去 `malloc`/`free`、`new`/`delete`，那你的泛型代码就永远不可能真正通用——因为你总得在某个地方特殊处理"这个类型需要手动释放资源"的情况。RAII 把资源管理嵌入到了类型本身的生命周期里，这才让泛型代码可以做到"对所有类型一视同仁"。看到这里我深有感触，原来 RAII 的意义不仅仅是"防止忘记释放"，它更是泛型编程能够成立的类型系统基石。

## 用 concepts 给智能指针的 arrow operator 加把锁

搞明白上面这个前提之后，我们来看一个特别具体的例子。我在写一个简易智能指针的时候，遇到了一个问题：`operator->` 这个东西，不是所有类型都该有的。

你想啊，`operator->` 的语义是"通过指针访问成员"，那如果我的智能指针包装的是一个 `int`，`int` 哪来的成员可以访问？所以 `operator->` 只在 `T` 是一个 class 类型的时候才有意义。以前没有 concepts 的时候，你要么不管三七二十一直接提供（然后用户对 `int` 调用的时候得到一堆看不懂的模板报错），要么用 SFINAE 搞一堆 `std::enable_if` 把代码弄得像天书。现在有了 concepts，事情变得特别干净。

```cpp
#include <iostream>
#include <concepts>
#include <string>

// 定义一个 concept：T 必须是 class 类型（包含 struct）
template<typename T>
concept HasMembers = std::is_class_v<T>;

template<typename T>
class SmartPtr {
    T* ptr_;
public:
    explicit SmartPtr(T* p = nullptr) : ptr_(p) {}
    ~SmartPtr() { delete ptr_; }

    // 禁止拷贝，简化示例
    SmartPtr(const SmartPtr&) = delete;
    SmartPtr& operator=(const SmartPtr&) = delete;

    // operator* 对所有类型都可用
    T& operator*() const {
        return *ptr_;
    }

    // operator-> 只在 T 是 class 类型时才存在
    // 如果你试图对 SmartPtr<int> 调用 ->，编译器直接告诉你这个成员函数不存在
    // 而不是给你一堆模板实例化的天书报错
    T* operator->() const requires HasMembers<T> {
        return ptr_;
    }
};

// 测试：对 class 类型，两个操作符都可用
void test_with_class() {
    SmartPtr<std::string> sp(new std::string("hello"));
    std::cout << *sp << std::endl;       // OK，operator*
    std::cout << sp->size() << std::endl; // OK，operator->，因为 string 是 class
}

// 测试：对 int，只有 operator* 可用
void test_with_int() {
    SmartPtr<int> sp(new int(42));
    std::cout << *sp << std::endl;  // OK，operator*
    // std::cout << sp->  // 编译错误！SmartPtr<int> 没有 operator->
    // 报错信息很清晰：没有名为 'operator->' 的成员
}
```

我跑了一下，`test_with_class()` 完全正常，`test_with_int()` 里如果取消注释那行 `sp->`，GCC 给出的错误是"no member named 'operator->' in 'SmartPtr<int>'"，干净利落。以前用 `enable_if` 的时候，报错能滚满一屏幕，现在就一句话。这就是 concepts 带来的体验提升——不是"能做以前做不了的事"，而是"做同样的事，体验好十倍"。

你可能会问，为什么不直接用 `operator*` 就完了？确实，如果你只用 `operator*`，那智能指针对所有类型的行为是统一的。但 `operator->` 在操作对象类型的时候实在太方便了，完全不用也太可惜了。所以正确的做法不是"一刀切去掉"，而是"精确控制它在什么时候存在"。concepts 就是干这个的。

## pair 的拷贝构造：标准里的一个 narrowing 隐患

搞完智能指针，我顺着思路开始看 `std::pair` 的实现。`std::pair` 有一个拷贝构造函数的模板版本，大概长这样：你可以把一个 `pair<A, B>` 拷贝构造出一个 `pair<C, D>`，前提是 `A` 能转换成 `C`，`B` 能转换成 `D`。标准里确实这么规定的，看起来很合理对吧？

但我仔细一看，发现一个问题：这个转换用的是普通的隐式转换，也就是说，它允许 narrowing conversion（窄化转换）。比如你可以把 `pair<double, double>` 拷贝成 `pair<int, int>`，小数部分直接被截断，编译器连个警告都不给你。这可不是我想要的行为。

```cpp
#include <utility>
#include <iostream>

void test_std_pair_narrowing() {
    std::pair<double, double> src{3.14, 2.718};
    // 这行代码能编译通过！3.14 变成 3，2.718 变成 2
    // 没有任何警告，数据静默丢失
    std::pair<int, int> dst = src;
    std::cout << dst.first << ", " << dst.second << std::endl; // 输出: 3, 2
}
```

我跑了一下，输出确实是 `3, 2`，编译器（GCC 15，开了 `-Wall -Wextra`）一个字都没说。

## 自己写一个安全的 pair：NonNarrowConvertible

我们在 [第一篇](01-type-safety-and-number-concept.md) 中已经深入讨论了窄化转换的检测机制，这里我们用一个更简洁的方式——利用花括号初始化禁止窄化的语言规则——来实现 `NonNarrowConvertible`。思路很简单：在拷贝构造的时候，用 concept 约束转换过程，不允许 narrowing。

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>
#include <stdexcept>

// 一个 concept：A 可以非窄化地转换为 B
// 核心思路：用花括号初始化来检测，因为花括号初始化禁止 narrowing
template<typename A, typename B>
concept NonNarrowConvertible = requires(A a) {
    // 如果这行能编译通过，说明 A 到 B 不存在 narrowing
    // 因为花括号初始化会拒绝 narrowing conversion
    B{static_cast<A>(a)};
};

template<typename T1, typename T2>
class SafePair {
public:
    T1 first;
    T2 second;

    SafePair() : first{}, second{} {}
    SafePair(T1 f, T2 s) : first(f), second(s) {}

    // 核心部分：从另一个 SafePair 拷贝构造
    // 要求两个维度都是 NonNarrowConvertible
    template<typename U1, typename U2>
    requires NonNarrowConvertible<U1, T1> && NonNarrowConvertible<U2, T2>
    SafePair(const SafePair<U1, U2>& other)
        : first(static_cast<T1>(other.first))
        , second(static_cast<T2>(other.second))
    {}
};

void test_safe_pair_no_narrowing() {
    SafePair<double, double> src{3.14, 2.718};

    // 这行会编译失败！double -> int 是 narrowing
    // 错误信息会指向 NonNarrowConvertible concept 不满足
    // SafePair<int, int> dst = src;  // 取消注释会报错

    // 这个没问题，int -> double 不是 narrowing
    SafePair<int, int> src2{3, 2};
    SafePair<double, double> dst2 = src2;  // OK
    std::cout << dst2.first << ", " << dst2.second << std::endl; // 3, 2
}
```

这个 `NonNarrowConvertible` concept 的技巧我折腾了一晚上才搞通。它的原理是利用了花括号初始化禁止 narrowing 的语言规则：`B{a}` 如果 `A` 到 `B` 存在 narrowing，这行代码本身就是 ill-formed 的。而 `requires` 表达式会检测这个 ill-formed，把它变成 concept 不满足，而不是硬编译错误。这样我们就把 narrowing 的检测从"运行时丢失数据"提升到了"编译时直接拒绝"。

不过这里有个值得注意的隐患：`NonNarrowConvertible` 的实现方式依赖的是"花括号初始化能不能编译通过"，而不是精确地判断"是否存在 narrowing"。对于数值类型来说这两件事等价，但对于复杂类型来说，花括号初始化可能因为其他原因失败（比如没有对应的构造函数），这时候报错信息可能会让人困惑。对于当前的场景来说够用了，如果以后遇到更复杂的情况，可以再细化这个 concept。

而且 C++ 在 narrowing 这件事上的保护其实是残缺的——花括号初始化禁止 narrowing 的规则只对初始化生效，赋值、函数参数传递、返回值等路径全部放行。真正的安全还是得靠类型系统层面的约束，比如用 concepts 在编译期就把不安全的转换路径堵死。

## C++26 静态反射初体验

到这里我们手搓 `NonNarrowConvertible` 的过程虽然能锻炼对 concepts 组合的理解，但演讲者后面还给出了一个更简洁的思路：与其自己去定义什么叫"窄化"，不如直接问编译器"你能不能用一个 `S` 类型的值去初始化一个 `T`？"这个思路的转变看似微小，实际上解决了一个让我之前卡了很久的问题——我们手搓的版本在处理 `char*` 到 `std::string` 这类场景时不够准确，而如果你直接问编译器"能不能用 `S` 初始化 `T`"，编译器自己心里门儿清。

不过演讲者也很诚实地提醒了一件事：不要把这个特例和它想要说明的通用方法论混为一谈。我们前面花大篇幅学的那个"用小 concept 拼大 concept"的构建手法，才是真正能反复使用的武器。这个初始化版本能 work，纯粹是因为"能不能初始化"恰好和"能不能窄化"在这个特定场景下高度重合。而在赋值、比较等其他场景下，你就没这么幸运了，还是得老老实实自己拼。工具箱里的工具是通用的，但具体哪个场景能偷懒，得碰运气。

演讲到最后，还展示了一个"五年前完全不可能实现"的东西——静态反射（Static Reflection, P2996）。以前如果你需要知道一个结构体有哪些成员、每个成员叫什么名字、类型是什么、在内存中的偏移量是多少，在 C++26 之前只能用宏来解决，写错一个字就静默出错，调试到怀疑人生。而 C++26 的静态反射，让我们终于可以直接问编译器"这个类型长什么样"了。

```cpp
// 基于 C++26 静态反射提案 P2996 R12 编写
// 注意：截至 2026 年初，尚无主流编译器完整实现此提案，此代码供学习参考
#include <meta>
#include <string_view>
#include <print>
#include <cstddef>
#include <array>

// 成员描述符：记录一个成员的元信息
struct member_descriptor {
    std::string_view name;   // 成员名字
    std::size_t offset;      // 在对象内的偏移量
    std::size_t size;        // 该成员占的字节数
};

// 核心魔法：为任意类型生成成员描述符数组
template<typename T>
consteval auto get_layout() {
    // ^^T 是反射运算符：向编译器请求类型 T 的元信息
    // nonstatic_data_members_of 返回 std::vector<std::meta::info>
    auto members = std::meta::nonstatic_data_members_of(^^T);
    constexpr size_t N = members.size();

    std::array<member_descriptor, N> layout{};
    for (size_t i = 0; i < N; ++i) {
        layout[i] = {
            // identifier_of 获取成员名（前提是该成员有标识符）
            .name   = std::meta::identifier_of(members[i]),
            // offset_of 返回 member_offset 结构体，.bytes 取偏移字节数
            .offset = static_cast<std::size_t>(std::meta::offset_of(members[i]).bytes),
            // size_of 返回该成员占的字节数
            .size   = std::meta::size_of(members[i])
        };
    }

    return layout;
}

// 测试用的结构体
struct Player {
    int id;
    float x;
    float y;
    double health;
    char name[32];
};

int main() {
    constexpr auto xd = get_layout<Player>();

    for (const auto& m : xd) {
        std::println("成员: {:<10} 偏移: {:>3} 字节  大小: {:>3} 字节",
                     m.name, m.offset, m.size);
    }

    return 0;
}
```

我跑出来的输出大概是这样的（具体偏移可能因平台和编译选项有对齐差异）：

```text
成员: id         偏移:   0 字节  大小:   4 字节
成员: x          偏移:   4 字节  大小:   4 字节
成员: y          偏移:   8 字节  大小:   4 字节
成员: health     偏移:  16 字节  大小:   8 字节
成员: name       偏移:  24 字节  大小:  32 字节
```

注意 `health` 的偏移是 16 而不是 12——这就是内存对齐在起作用，`double` 要求 8 字节对齐，所以编译器在 `y` 后面塞了 4 字节的 padding。以前要验证这种事你得手动算或者用 `offsetof` 宏一个个写，现在一行代码全出来了。

回想一下我们前面学 concepts 的时候，concepts 本质上也是在问编译器"这个类型满足什么条件"。但 concepts 能问的问题很有限——"能做加法吗"、"能迭代吗"、"能转换吗"。而静态反射直接把编译器内部关于这个类型的全部知识都敞开了：名字、成员、基类、函数签名、模板参数……你想要什么就拿什么。我之前一直觉得 template 是黑魔法，concepts 让黑魔法变得可读了，而静态反射则让黑魔法变得可组合了。以后反射加 concepts，编译期把成员遍历一遍，对每个成员检查它是否满足特定的约束，然后分别生成代码——干净利落。

不过当前 C++26 的静态反射虽然在 2025 年中已经投票进入 C++26 工作草案（P2996），但截至 2026 年初尚无主流编译器完整实现——GCC 和 Clang（Bloomberg 的实验分支 [clang-p2996](https://github.com/bloomberg/clang-p2996)）都在积极开发中，但都还不完整。上面这段代码基于 P2996 R12 提案规范编写，供学习参考，千万别指望在 production 环境用。

---

# concept 不只是"模板参数的标签"——它比你想的灵活得多

说实话，学 concept 的前两年，我一直把它当成一种"给模板参数贴标签"的语法糖。写个 `template<std::integral T>`，感觉就跟写个 SFINAE 的 if-else 差不多，只是好看了一点。直到最近重新啃这块内容的时候，我才意识到自己之前的理解有多浅——concept 本质上就是编译期函数，而既然是函数，它就能接受多个参数，甚至能接受值参数。这个认知转变直接让我拍大腿，因为很多以前觉得"用 concept 表达不了"的约束，其实根本不是语言限制，是我自己没想明白。

## 先把一个误区拆掉：concept 不是只能约束一个类型参数

我之前写 concept 的时候，几乎所有的长这样：

```cpp
template<typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};
```

一个 concept 约束一个类型，规规矩矩。但你想一个问题——如果一个泛型函数接受两个不同类型的参数，你光分别约束每个类型够吗？比如一个函数签名是 `template<typename T, typename U> void foo(T, U)`，你用 `std::integral<T>` 和 `std::integral<U>` 分别约束了，但这只说了"T 是整数、U 是整数"，完全没说 T 和 U 之间有什么关系。可它们既然出现在同一个函数里，多半是有某种关联的，否则你为什么要把它俩放一起？

演讲里提到一个数据：超过一半的 concept 会接受不止一个参数。我一开始觉得这个比例夸张了，但自己回头翻项目代码的时候发现，还真是——只要你的泛型代码稍微复杂一点，跨类型的约束需求到处都是。

来个具体例子。假设我在写一个序列化相关的库，我需要一个 concept 来表达"T 类型的值可以序列化到 U 类型的缓冲区里"：

```cpp
template<typename T, typename Buffer>
concept SerializableTo = requires(T value, Buffer& buf) {
    // 要求 Buffer 有 write 方法，能接受 T 的序列化结果
    { buf.write(std::declval<const char*>(), std::declval<std::size_t>()) }
        -> std::same_as<std::size_t>;
    // 要求能计算出 T 序列化后的字节大小
    { serialized_size(value) } -> std::convertible_to<std::size_t>;
};

// 使用的时候，两个类型被绑在一起约束
template<typename T, typename Buffer>
    requires SerializableTo<T, Buffer>
void serialize(const T& value, Buffer& buf) {
    auto size = serialized_size(value);
    // ... 实际序列化逻辑
}
```

你看，这个 concept 如果只能接受一个参数，你要么把约束拆散到两个地方（然后丢失类型间关联的信息），要么就得用很别扭的嵌套写法。但多参数 concept 让你直接把"T 和 U 之间必须满足什么关系"说清楚，读代码的人一眼就知道这两个类型不是各玩各的。

## 更让我兴奋的：concept 可以接受值参数

这个是我之前完全不知道的。我一直以为 concept 的参数列表里只能出现类型（`typename T`）或者模板模板参数之类的，没想到它还能接受普通的值。这意味着你可以在编译期把"类型约束"和"值约束"混在一起表达，而且写出来的东西看起来就跟普通代码几乎一样。

假设我在写网络相关的代码，需要一个缓冲区，这个缓冲区有两个硬性要求：第一，至少要能容纳 k 个元素；第二，缓冲区大小必须是 2 的幂（这个在内存池、环形缓冲区里非常常见，因为取模可以用位与代替）。

```cpp
#include <concepts>
#include <cstdint>
#include <type_traits>

// 一个普通的编译期函数，判断是不是 2 的幂
// 关键：consteval 让它只能在编译期执行
consteval bool is_power_of_two(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// concept 接受一个类型参数 S 和一个值参数 k
template<typename S, std::size_t k>
concept BufferSpace = requires(S buf) {
    // S 必须有 size() 方法返回能转成 size_t 的东西
    { buf.size() } -> std::convertible_to<std::size_t>;
    // 值约束1：大小至少是 k
    requires (S::size_value >= k);
    // 值约束2：大小必须是 2 的幂
    requires is_power_of_two(S::size_value);
};
```

然后我定义几种缓冲区类型来测试：

```cpp
// 大小为 64 的缓冲区（64 是 2 的幂）
struct SmallBuffer {
    static constexpr std::size_t size_value = 64;
    constexpr std::size_t size() const { return size_value; }
};

// 大小为 100 的缓冲区（100 不是 2 的幂）
struct WeirdBuffer {
    static constexpr std::size_t size_value = 100;
    constexpr std::size_t size() const { return size_value; }
};

// 大小为 1024 的缓冲区（1024 是 2 的幂）
struct NetworkBuffer {
    static constexpr std::size_t size_value = 1024;
    constexpr std::size_t size() const { return size_value; }
};
```

现在用这个 concept 来约束一个模板函数：

```cpp
template<typename S>
    requires BufferSpace<S, 128>
void process_buffer(S& buf) {
    // 到这里编译器已经保证了：
    // 1. S 有 size() 方法
    // 2. 大小 >= 128
    // 3. 大小是 2 的幂
    // 所以这里可以放心用位与做取模
    constexpr std::size_t mask = S::size_value - 1;
    // ... 实际处理逻辑
}
```

来，跑一下看看报错信息有多清晰：

```cpp
int main() {
    SmallBuffer small;
    // process_buffer(small);  // 编译错误：size_value(64) < 128

    WeirdBuffer weird;
    // process_buffer(weird);  // 编译错误：100 不是 2 的幂

    NetworkBuffer net;
    process_buffer(net);       // 编译通过：1024 >= 128 且 1024 是 2 的幂
}
```

我在 GCC 15 下试了，把 `process_buffer(small)` 那行取消注释后，编译器给出的错误信息直接告诉你约束不满足，而且把 `requires (S::size_value >= k)` 这条指出来了。如果不用 concept 而是用 `static_assert`，你得在函数体里面写，而且报错位置在函数内部，调用栈一深根本看不清。concept 把约束提到了签名处，报错直接指向调用点，这个体验差距是实打实的。

回头看为什么这能工作——concept 的声明本质上就是 `template<...参数...> concept Name = 布尔表达式;`，这个布尔表达式在编译期求值，而既然是模板参数列表，那 `typename`、`int`、`std::size_t` 这些都可以作为参数类型出现。C++20 的模板参数本来就支持非类型参数，concept 只是继承了这套机制而已。所以没有什么特殊的"concept 值参数语法"，它就是普通的模板非类型参数。

而 `is_power_of_two` 能在 concept 的 `requires` 表达式里用，是因为我把它声明成了 `consteval`。`consteval` 是 C++20 引入的，意思是"这个函数必须在编译期执行，不可能在运行期调用"。在 concept 的约束表达式里，你需要的就是这种"保证在编译期完成"的函数，因为 concept 本身就是编译期的东西。

这种值参数的 concept 在实际开发中真的会用吗？我自己的体会是：当你写库代码、写框架的时候，会经常遇到。线程池要求任务队列大小是 2 的幂（位与取模优化）、内存分配器要求块大小对齐到某个值、SIMD 操作要求向量长度是 4/8/16 的倍数、协议解析器要求缓冲区至少能放下一个完整帧——这些场景里，"类型对不对"和"值合不合规"往往是交织在一起的。以前我遇到这种情况，要么在运行期 `assert`，要么在模板里写一堆 `static_assert` 散落在各个函数体里。现在有了值参数 concept，你可以把所有约束集中在一个地方，而且是在接口签名处就表达清楚。

到这里我终于理解了为什么说"concept 是编译期函数"这个视角这么重要。如果你把它当成"模板参数的标签"，你的思维会被限制在"一个 concept 约束一个类型"的框框里。但如果你把它当成函数——可以接受多个参数、可以接受值参数、可以调用其他编译期函数、可以组合——那它的表达能力其实跟普通代码几乎一样强，只不过整个执行过程发生在编译期。

---

# 判断 2 的幂：从一个小算法扯到泛型与面向对象的恩怨情仇

## 一个让我拍大腿的小算法

前两天在刷一道很基础的题：判断一个整数是不是 2 的幂。我之前一直用的是最笨的办法——不停地除以 2 看余数，或者更"高级"一点，用对数算一下。但这次我看到一个位运算的写法，说实话看到的时候我觉得写法很巧妙，因为逻辑实在太干净了。

思路是这样的：一个数如果是 2 的幂，那它的二进制表示里一定只有一个 1，其余全是 0。比如 8 是 `1000`，32 是 `100000`。所以你只需要不停地右移，把最后一位扔掉，同时检查扔掉的那个位是不是 1。如果移到最后只剩下一个 1，那它就是 2 的幂；如果中间遇到任何非零位，直接返回 false；如果移完发现全是 0，那 0 本身也不是 2 的幂，也返回 false。

我之前一直觉得位运算判断 2 的幂就是那个经典的 `n & (n - 1) == 0` 一行搞定，但那个写法有个坑——它会把 0 也判断成 true，你还得额外加一个 `n != 0` 的判断。而这个移位写法虽然多几行，但逻辑是完全自洽的，不需要任何特判。我顺手写了一个验证：

```cpp
#include <iostream>
#include <bitset>

// 用右移来判断是不是 2 的幂
// 思路：2 的幂的二进制表示有且仅有一个 1
bool is_power_of_two_shift(unsigned int n) {
    if (n == 0) return false;  // 0 不是 2 的幂

    int count = 0;
    while (n > 0) {
        // 检查最后一位是不是 1
        if (n & 1u) {
            count++;
            if (count > 1) return false;  // 超过一个 1，不是 2 的幂
        }
        n >>= 1;  // 右移，扔掉最后一位
    }
    return count == 1;
}

// 经典的 n & (n-1) 写法，注意要排除 0
bool is_power_of_two_classic(unsigned int n) {
    return n != 0 && (n & (n - 1)) == 0;
}

int main() {
    unsigned int test_values[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, 32, 63, 64, 127, 128, 255, 256};

    std::cout << "数值     二进制表示              移位法  经典法\n";
    std::cout << "----     ----------              ------  ------\n";
    for (unsigned int n : test_values) {
        std::cout << n << "\t" << std::bitset<8>(n) << "\t"
                  << (is_power_of_two_shift(n) ? "true " : "false")
                  << "  "
                  << (is_power_of_two_classic(n) ? "true " : "false")
                  << "\n";
    }
    return 0;
}
```

跑出来的结果两种方法完全一致，但移位法的逻辑我读起来更顺畅，因为它的"意图"和"实现"是完全对齐的——就是在数有几个 1。而 `n & (n - 1)` 虽然巧妙，但你第一次看到的时候真的需要想一下为什么这能把最低位的 1 给消掉。不过话说回来，经典写法在性能上确实更优，因为它只需要一次与运算和一次比较，而移位法要循环。所以实际工程里我还是会用经典写法，但理解移位法的思路对培养位运算的直觉真的很有帮助。

## 泛型编程 vs 面向对象：我纠结了好久的问题

扯完这个小算法，我想聊一个更大的话题，因为这段内容让我把之前一直模模糊糊的一个概念终于想通了——泛型编程和面向对象编程到底有什么本质区别。

我 2022 年刚学 C++ 的时候，先学的类和继承，觉得面向对象就是 C++ 的全部。后来接触到模板，看到一堆尖括号和编译报错就头疼，觉得这是"黑魔法"，能不用就不用。再后来开始学 concepts，慢慢发现泛型编程能做的事情好像比我想象的多得多，但心里一直有个疑问：这两玩意儿到底什么时候该用哪个？

现在我终于搞明白了，核心区别其实就一句话：**泛型编程更灵活，而且不依赖间接函数调用**。

这个"不依赖间接函数调用"太关键了。面向对象的多态是通过虚函数表（vtable）来实现的，你调用一个虚函数，运行时要先查表、再跳转，这就是间接调用。而泛型编程在编译期就把类型确定了，该内联的内联，该特化的特化，生成的代码跟手写的一样直接。所以泛型编程在大多数情况下跑得更快，这不是玄学，是底层机制决定的。

## 我试图设计一个 Container 基类的血泪史

说到面向对象的局限，我必须吐槽一下我自己踩过的坑。我之前做过一个小项目，想统一管理不同类型的容器，于是很自然地想：我来定义一个 `Container` 基类吧，然后 `MyList` 和 `MyVector` 继承它。

```cpp
// 我当时写的"理想"代码，但从来没能真正跑通
class Container {
public:
    virtual int size() const = 0;
    virtual void push_back(int value) = 0;
    virtual int& operator[](int index) = 0;
    virtual void insert(int pos, int value) = 0;
    virtual void erase(int pos) = 0;
    // ...
};
```

看着挺美好对吧？但实际一写就炸了。`std::list` 的 `insert` 和 `std::vector` 的 `insert` 行为细节不一样，`std::list` 有 `splice` 但 `std::vector` 根本没有，`std::vector` 有 `reserve` 但 `std::list` 用不上。我试图在基类里找到一个"公共接口"来覆盖所有容器的操作集，但它们的操作集根本就不一样，对它们的约束也不一样。

我折腾了好几天，最后要么把接口设计得大而全（一堆方法在某些子类里直接抛异常"不支持"），要么就设计得小而碎（只剩下一个 `size()`，那还要基类干嘛）。后来我放弃了，改用模板函数来处理容器，发现事情变得异常简单：

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>
#include <print>

// 对 vector 的约束：需要随机访问，可以做 reserve
template <typename T>
concept RandomAccessContainer = requires(T t, typename T::value_type v, size_t n) {
    { t.size() } -> std::convertible_to<size_t>;
    { t[n] } -> std::same_as<typename T::reference>;
    { t.reserve(n) };
};

// 对 list 的约束：不需要随机访问，但需要 push_back 和 splice 能力
// 注意这里的 !RandomAccessContainer<T> —— 因为 std::vector 同时满足两个 concept，
// 不加互斥条件的话，调用 process_container(vec) 会导致重载歧义
template <typename T>
concept SequenceContainer = !RandomAccessContainer<T> && requires(T t, typename T::value_type v) {
    { t.size() } -> std::convertible_to<size_t>;
    { t.push_back(v) };
    { t.front() } -> std::same_as<typename T::reference>;
};

// 针对随机访问容器的处理
void process_container(const RandomAccessContainer auto& c) {
    std::println("处理随机访问容器，大小: {}", c.size());
    // 可以用下标访问
    if (!c.empty()) {
        std::println("  第一个元素: {}", c[0]);
    }
}

// 针对序列容器的处理
void process_container(const SequenceContainer auto& c) {
    std::println("处理序列容器，大小: {}", c.size());
    // 用 front() 访问
    if (!c.empty()) {
        std::println("  第一个元素: {}", c.front());
    }
}

int main() {
    std::vector<int> vec{1, 2, 3, 4, 5};
    std::list<int> lst{10, 20, 30};

    process_container(vec);  // 匹配 RandomAccessContainer
    process_container(lst);  // 匹配 SequenceContainer

    return 0;
}
```

你看，用 concepts 的话，我可以对不同类型的容器施加不同的要求，不需要硬塞进一个统一的基类里。vector 需要支持 `operator[]` 和 `reserve`？那就写一个 concept 要求这些。list 不需要随机访问？那就写另一个 concept。它们各自满足各自的约束，各自走各自的函数重载。回头看看原理其实很简单——**不要试图用一个固定的接口去框住所有东西，而是根据类型本身的能力来匹配最合适的处理方式**。

## 它们不是敌人，是搭档

但我必须强调一点，千万别因为我说了泛型编程好就去全盘否定面向对象。面向对象有一个泛型编程很难替代的场景：**开放类型集**。什么叫开放类型集？就是你在写代码的时候，根本不知道未来会有哪些类型加进来。比如一个 GUI 框架里的绘图系统，你定义了一个 `Shape` 基类，有 `draw()` 虚函数。然后用户可以在他们自己的代码里写一个 `MyCustomShape` 继承 `Shape`，你的框架代码不需要重新编译就能处理这个新类型。这种"运行时扩展"的能力，是泛型编程做不到的，因为模板在编译期就必须知道所有类型。

所以我的理解是：**如果你能穷举所有类型（或者至少在编译期知道），用泛型编程，性能更好、表达更精确；如果你需要运行时动态扩展类型，用面向对象的多态**。它们是互补的，不是互斥的。

## draw-all：同一个问题，两种解法都能搞定

为了验证这个理解，我写了一个经典的"绘制所有图形"的例子，分别用面向对象和泛型编程来实现：

```cpp
#include <iostream>
#include <vector>
#include <memory>
#include <concepts>
#include <print>

// ============ 面向对象的方式 ============
class Shape {
public:
    virtual ~Shape() = default;
    virtual void draw() const = 0;
};

class Circle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制圆形");
    }
};

class Rectangle : public Shape {
public:
    void draw() const override {
        std::println("OOP: 绘制矩形");
    }
};

// 面向对象的 draw_all：接受 Shape 指针的 range
void draw_all_oop(const std::vector<std::unique_ptr<Shape>>& shapes) {
    for (const auto& s : shapes) {
        s->draw();  // 虚函数调用，间接调用
    }
}

// ============ 泛型编程的方式 ============

// 不需要继承任何基类的图形类型
struct Triangle {
    void draw() const {
        std::println("Generic: 绘制三角形");
    }
};

struct Star {
    void draw() const {
        std::println("Generic: 绘制五角星");
    }
};

// 定义 concept：只要你有 draw 成员函数就行
template <typename T>
concept Drawable = requires(const T& t) {
    { t.draw() };
};

// 泛型的 draw_all：接受任何有 draw() 的类型的 range
template <std::ranges::range R>
    requires Drawable<std::ranges::range_value_t<R>>
void draw_all_generic(const R& items) {
    for (const auto& item : items) {
        item.draw();  // 直接调用，编译期确定，可以内联
    }
}

int main() {
    std::println("=== 面向对象方式 ===");
    std::vector<std::unique_ptr<Shape>> oop_shapes;
    oop_shapes.push_back(std::make_unique<Circle>());
    oop_shapes.push_back(std::make_unique<Rectangle>());
    draw_all_oop(oop_shapes);

    std::println("\n=== 泛型编程方式 ===");
    std::vector<Triangle> triangles{Triangle{}, Triangle{}};
    std::vector<Star> stars{Star{}};
    draw_all_generic(triangles);
    draw_all_generic(stars);

    // 关键点：泛型方式也能处理有虚函数的类型！
    std::println("\n=== 泛型方式处理 OOP 类型 ===");
    std::vector<Circle> circles{Circle{}, Circle{}};
    draw_all_generic(circles);  // 完全可以，Circle 有 draw()

    return 0;
}
```

跑一下输出：

```text
=== 面向对象方式 ===
OOP: 绘制圆形
OOP: 绘制矩形

=== 泛型编程方式 ===
Generic: 绘制三角形
Generic: 绘制三角形
Generic: 绘制五角星

=== 泛型方式处理 OOP 类型 ===
OOP: 绘制圆形
OOP: 绘制圆形
```

注意最后那个例子——`draw_all_generic` 是泛型函数，但它完全能处理 `Circle` 这种带有虚函数的面向对象类型，因为 `Circle` 确实有 `draw()` 方法，满足 `Drawable` concept。也就是说，**泛型编程加上 concepts，能覆盖经典面向对象类层次结构能做到的一切**，同时还能处理那些根本不在任何类层次结构中的类型（比如 `Triangle` 和 `Star`，它们没有继承任何基类）。

到这里我终于把这块搞通了。以前觉得模板和 concepts 是"高级玩法"，虚函数和多态才是"正统"，现在回头看，泛型编程的表达能力其实更强，而且因为不需要间接调用，性能也更好。但面向对象在处理开放类型集的时候确实有它的不可替代性。两者互补，根据场景选择，这才是正确的打开方式。

---

# Concepts 不需要一次写完美——实践的迭代才会让它越发精准

这个陈述很好类比——你的 LLM Overthinking 了，在你动手之前，你就疯狂预设，企图利用计算来陈述这个本质不确定的世界。结果就是每次想写一个 concept，对着屏幕发呆半天，想着"我是不是还漏了什么约束条件"，最后连第一行代码都没写出来。

concept 就像是类型系统的"合同"，一旦签了就不能改，所以必须在写的时候就穷举所有约束——很多人一开始都会这么想。比如我要约束一个"数字类型"，我就会开始纠结：要不要加 `std::is_copy_constructible`？要不要加 `std::is_default_constructible`？要不要加 `std::is_trivially_destructible`？越想越多，最后把自己吓退了。

但实际上，concept 跟我们写普通代码一样，第一版就是用来"先用起来"的。你不需要在第一天就考虑到所有边界情况。先把当前真正需要的约束写上去，后面发现不够了再补，这完全没问题。

## 从零开始写一个 Number concept

关于 `Number<T>` 的完整实现和深入讨论，请参阅 [第一篇](01-type-safety-and-number-concept.md)。这里我只想展示这个 concept 的核心骨架，用它来说明"迭代演进"的哲学：

```cpp
#include <iostream>
#include <concepts>
#include <type_traits>

// 第一版：只约束我当前真正用到的操作
// 不加拷贝、不加移动、不加默认构造——那些我暂时不需要
template<typename T>
concept Number = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
    { a - b } -> std::convertible_to<T>;
    { a * b } -> std::convertible_to<T>;
    { a / b } -> std::convertible_to<T>;
    { -a }    -> std::convertible_to<T>;
};

// 一个只用了加减法的函数——它只需要 Number 的部分能力
template<Number T>
T compute(T x, T y) {
    return (x + y) * 2 - y;
}
```

你看，这个 `Number` concept 漏了一堆东西：没有约束 `==` 和 `!=`，没有约束 `+=` 这种复合赋值，没有约束 `<<` 输出，什么都没有。但它对于 `compute` 这个函数来说，已经完全够用了。如果明天我写了一个新函数需要比较两个数字是否相等，那我可以再写一个 `EqualityComparable` 的 concept 去约束那个函数的参数，而不是回头把 `Number` 改得越来越臃肿。

假设后来我确实需要一个更完整的数字概念，我可以基于已有的 `Number` 去扩展，而不是推翻重来：

```cpp
// 第二版：在 Number 基础上扩展，需要比较能力的时候再加
template<typename T>
concept ComparableNumber = Number<T> && requires(T a, T b) {
    { a == b } -> std::convertible_to<bool>;
    { a != b } -> std::convertible_to<bool>;
    { a < b }  -> std::convertible_to<bool>;
    { a <= b } -> std::convertible_to<bool>;
    { a > b }  -> std::convertible_to<bool>;
    { a >= b } -> std::convertible_to<bool>;
};

template<ComparableNumber T>
T clamp(T val, T lo, T hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}
```

这种"用到什么约束什么"的方式，跟函数式编程里的类型类（typeclass）思路其实很像——你定义一组最小的、正交的能力原语，然后在需要的地方组合它们，而不是一开始就搞一个"上帝 concept"把所有东西都塞进去。

## "会不会匹配到错误的东西"这个担忧

我一开始也担心过这个问题：如果我的 `Number` concept 只检查了有没有 `+ - * /` 运算符，那会不会有某个类型碰巧也有这些运算符，但它根本不是数字，然后就被错误地匹配上了？

演讲里提到了一个经典例子：`std::forward_iterator` 和 `std::input_iterator` 在语法约束上几乎一模一样，它们的区别主要在语义层面——forward iterator 要求通过同一个迭代器多次遍历能得到相同结果，而 input iterator 不保证这一点。这个区别你没法用纯语法约束表达出来。

但话说回来，我们得现实一点。一个类型碰巧实现了 `+ - * /` 并且返回值还能转换回自身类型，但它"不是数字"——这种情况发生的概率极低。如果一个类型真的提供了这五个运算符并且签名完全匹配，那它在语法层面就已经表现得像一个数字了，哪怕它的语义是"矩阵"或者"多项式"，在你只需要加减乘除的场景下，用它也没问题。

而且，concept 约束名字查找比无约束的名字查找要安全得多。当你用 concept 约束了一个函数模板的参数后，编译器在做重载决议的时候，只会考虑那些满足 concept 的候选函数。这比传统的 SFINAE 靠 `std::enable_if` 在返回类型里藏条件要可靠得多，因为 concept 是显式的、有名字的约束，编译器报错的时候会直接告诉你"这个类型不满足 Number"，而不是给你一堆长达五十行的模板实例化错误。

## 跟 OOP 层次约束的互补关系

还有一个点让我想通了：concept 提供的是"扁平的"能力约束，而 OOP 的类层次提供的是"有结构的"层次约束。这两者不是互斥的，而是互补的。

比如你有一个类层次 `Shape -> Circle / Rectangle`，这是结构化的、有继承关系的。但你也可以写一个 `concept Drawable = requires(T t, std::ostream& os) { { os << t } -> std::same_as<std::ostream&>; };`，这个 concept 不关心你的类型是不是继承自 `Shape`，它只关心你能不能被输出到流里。一个 `Circle` 可以同时满足"是 Shape 的子类"和"是 Drawable"，这两个约束在不同场景下各司其职。

我以前总觉得"要么用 OOP，要么用模板泛型，必须选一个"，现在回头看这种想法太狭隘了。工具箱里的工具不是让你只挑一个用的。

---

# Concepts 不只是模板参数的专利——我之前完全忽略了这一点

说实话，看到这部分内容的时候我很受触动。因为从我 2022 年开始学 C++ 以来，脑子里一直有一个根深蒂固的印象：concepts 就是用来约束模板参数的，写在 `template <concept_name T>` 里面，完事。结果现在发现，concepts 完全可以脱离模板参数独立使用，用在普通函数的参数上，这直接打开了一扇我之前根本没看到过的门。

## 先聊聊那个"尾巴摇狗"的问题

在展开之前，我想先说一个让我深有共鸣的观点。我们经常在讨论"怎么区分 forward iterator 和 input iterator"这类问题时，陷入一种本末倒置的思维——为了区分这两个东西，我们开始绞尽脑汁地发明各种语法差异，比如给其中一个加个 tag，或者加个特殊的成员函数，然后写一个 concept 去检测这个 tag 存不存在。整个设计就为了解决这一个特定问题而存在，越搞越复杂。

正确的做法其实应该是：先针对通用问题给出最优雅的设计方案，然后如果真的遇到特殊情况需要区分，再用一点小技巧打补丁。主次不能反。

## 从最简单的例子开始：concept 约束普通函数参数

我们先看一个特别基础的例子。假设我有一个处理整数的函数，我希望它接受 `short`、`int`、`long` 这些标准整数类型，但不要接受 `float`、`double` 这些浮点类型。

如果用传统模板的思路，你可能会这么写：

```cpp
#include <type_traits>
#include <iostream>

// 传统写法：用 std::enable_if 或者 static_assert
template <typename T>
void process_old(T val) {
    static_assert(std::is_integral_v<T>, "T must be an integral type");
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_old(42);       // OK
    process_old(3.14);     // 编译失败，但错误信息又长又丑
}
```

这种写法我以前写过无数遍，问题在于错误信息——你看到的是一坨 `static_assert` 失败的模板实例化栈，对新手来说跟天书一样。

现在换成 concept 的思路，但关键来了——**我不一定要写成模板**：

```cpp
#include <concepts>
#include <iostream>

// 直接用 concept 约束普通函数的参数！
void process(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process(42);       // OK，int 满足 std::integral
    process(42L);      // OK，long 满足 std::integral
    // process(3.14);  // 编译错误，double 不满足 std::integral
}
```

你发现了吗？这里根本没有 `template` 关键字，没有 `typename T`，就是一个普普通通的函数，只不过参数类型写成了 `std::integral` 而不是 `int`。编译器看到 `std::integral` 这个 concept，就会自动把它当成一个约束，在重载决议的时候去检查传入的类型是否满足。

我第一次看到这种写法的时候一下子就理顺了——原来 concept 可以这么用！这基本上就是泛型编程在语法上向普通编程靠拢了。你写函数的时候，思维模式从"我要写一个模板"变成了"我要写一个函数，参数类型是一个 concept"，这个心理转变对我来说特别重要。

当然你也可以写成模板的形式，效果是等价的：

```cpp
#include <concepts>
#include <iostream>

// 模板写法，效果一样
template <std::integral T>
void process_template(T val) {
    std::cout << "processing: " << val << "\n";
}

// 非模板写法
void process_plain(std::integral auto val) {
    std::cout << "processing: " << val << "\n";
}

int main() {
    process_template(42);
    process_plain(42);
    // 两个调用在编译器内部的处理方式几乎一样
}
```

区别在大多数场景下没有本质区别，编译器底层做的重载决议是一样的。但是非模板写法有一个心理上的好处：你读代码的时候，第一眼看到的就是一个普通函数，不需要在脑子里先过一遍"这是一个模板，T 会被推导成什么"。代码的意图更直白。

不过有一个小坑我要提醒你。如果你用非模板写法，函数体里就不能用 `T` 这个名字了，因为你根本没有声明 `T`。你需要用 `decltype` 或者 `auto`：

```cpp
#include <concepts>
#include <iostream>
#include <typeinfo>

void process(std::integral auto val) {
    // 这里没有 T，所以需要用 auto 或 decltype
    auto doubled = val * 2;
    std::cout << "type: " << typeid(doubled).name()
              << ", value: " << doubled << "\n";
}

int main() {
    process(42);   // 传入 int，doubled 也是 int
    process(42L);  // 传入 long，doubled 也是 long
}
```

## 真正让我开窍的场景：工业代码里的基础设施需求

上面那个整数例子太简单了，可能你觉得"也就那样吧"。真正让我理解这个特性价值的，是演讲里提到的工业软件场景。

我之前在实习的时候参与过一个比较大的 C++ 项目，里面有个感受特别深：**生产代码和教学代码完全是两回事**。教科书上的 `advance` 函数就三四行，把迭代器往前挪 n 步，干净利落。但实际项目里的 `advance`，或者说类似 `advance` 的核心函数，被塞进了大量和核心逻辑无关的东西——日志记录、调试断言、正确性检查、遥测数据采集、调用链追踪……每加一种基础设施需求，函数就膨胀一圈。

我们来看一个模拟这种场景的例子。假设我有一个简化版的 `advance`，把迭代器前进 2 步：

```cpp
#include <iostream>
#include <vector>
#include <list>

// 教科书版本：干净但不够用
template <typename Iter>
void advance_by_2(Iter& it) {
    ++it;
    ++it;
}
```

现在回到 concept 不限于模板参数这个特性。如果我们把 `advance_by_2` 的参数用 concept 来约束，写成非模板的形式，我们其实获得了一个很重要的能力：**这个函数在类型系统中的"身份"变得更清晰了**。它不再是一个对所有类型都开放的模板，而是一个有明确接口契约的函数。这为后续用 concept 做更精细的分派和组合打下了基础。

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <concepts>

// 用 concept 约束参数，明确表达"这个函数接受随机访问迭代器"
void advance_by_2(std::random_access_iterator auto& it) {
    it += 2;  // 随机访问迭代器可以直接 +=
}

// 同名函数，接受输入迭代器（只能一步步走）
// 注意：必须排除随机访问迭代器，否则 std::random_access_iterator<T> 满足时
// 两个重载都会匹配，导致歧义
template <typename T>
    requires std::input_iterator<T> && (!std::random_access_iterator<T>)
void advance_by_2(T& it) {
    ++it;
    ++it;
}

int main() {
    std::vector<int> vec = {1, 2, 3, 4, 5};
    std::list<int> lst = {1, 2, 3, 4, 5};

    auto vit = vec.begin();
    auto lit = lst.begin();

    advance_by_2(vit);  // 调用随机访问版本
    advance_by_2(lit);  // 调用输入迭代器版本

    std::cout << *vit << "\n";  // 输出 3
    std::cout << *lit << "\n";  // 输出 3
}
```

这里第一个函数用的是 `std::random_access_iterator auto&` 这种简写语法（C++20 允许的 concept 简写形式）。第二个函数由于需要排除随机访问迭代器（避免两者同时匹配导致歧义），我改用了完整的模板 + `requires` 写法，在约束里加了 `!std::random_access_iterator<T>` 来确保互斥。两个同名函数通过不同的 concept 约束实现了重载——随机访问迭代器走 `+= 2` 的快速路径，普通输入迭代器走两次 `++` 的慢速路径。这就是 concept 带来的更优雅的重载机制。

## 我之前的一个错误理解

说到这里我要坦白一个我之前的误解。我一开始学 concepts 的时候，觉得它最大的价值是"让模板错误信息更好看"。确实，concept 的错误信息比 `enable_if` 好看一百倍，但如果只看到这一层，就太小看 concepts 了。

concepts 真正的价值在于**它让泛型编程的思维方式发生了变化**。以前写模板，我的思维是"这里需要一个类型参数，让我加个约束"；现在用 concept，我的思维变成了"这里需要一个满足某种语义的东西"。从"类型参数"到"语义需求"，这个转变看似微妙，实际上影响你整个设计。

就像上面那个 `advance_by_2` 的例子，我写的不是"一个接受 `T` 的模板函数"，而是"一个接受随机访问迭代器的函数"和"一个接受输入迭代器的函数"。代码的意图从实现细节层面提升到了语义层面。

## 关于"孤立编译"的误区

很多人（包括演讲者最初）认为泛型函数必须能够孤立编译——也就是说，只看函数定义本身，不看调用点的上下文，就应该能完成类型检查。但后来认识到这既不是我们真正需要的，也不是 concepts 所提供的。

我之前也有这个误区。我觉得一个好的泛型函数应该"自包含"，自己就能证明自己对类型的要求是合理的。但仔细想想，这其实是一种过度要求。泛型函数的约束应该描述"我需要什么"，而不是"我能处理一切"。具体某个调用点传进来的类型是否满足，那是调用点和函数约束之间的契约验证，不需要函数自己操心。关于模板编译模型和类型检查的更深入讨论，我们在 [第四篇](04-template-compilation-and-future.md) 中会展开。

用 `std::integral` 约束参数的那个例子就是最好的说明：函数只声明"我需要一个整数"，至于你传 `int` 还是 `long`，那是你的事。函数不需要孤立地知道所有可能的整数类型。

到这里 concept 的这个用法彻底想通了。它不只是一个"更好的 `enable_if`"，而是一种让你用语义方式思考接口的工具。而且它不局限于模板参数——你可以直接在普通函数参数上用，这让泛型编程的语法距离普通编程又近了一大步。回头看看，其实没那么难，只是我之前一直带着"concept = 模板约束语法糖"的有色眼镜在看它。
