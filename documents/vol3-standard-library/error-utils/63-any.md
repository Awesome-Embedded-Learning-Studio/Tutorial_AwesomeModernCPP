---
title: "any：能装任何类型——以及为什么你多半用不到它"
description: 讲透 std::any 的类型擦除机制——SBO 内联存储与堆分配的边界、any_cast 的两种重载与精确类型匹配、为什么多数场景 variant 更该上场，以及 any 真正不可替代的那几个边缘场景
chapter: 7
order: 63
cpp_standard:
  - 17
  - 20
difficulty: intermediate
platform: host
prerequisites:
  - "variant：类型安全的判别联合"
  - "optional：值可能不存在"
related:
  - "variant：类型安全的判别联合"
  - "optional：值可能不存在"
reading_time_minutes: 11
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
  - variant
  - optional
---

# any：能装任何类型——以及为什么你多半用不到它

C++ 是静态类型语言，每个变量的类型在编译期就钉死了。可有时候我们确实会撞上一个需求：手里有个值，但它的类型在写代码时还说不准——可能是个 `int`，也可能是个 `std::string`，甚至可能是个连我们这些写库的人都还没定义出来的用户类型。标准库给了一个兜底答案：`std::any`（C++17），一个"能装任意 `CopyConstructible` 类型"的容器。

先说在前面：这篇的基调不是"快去用 `any`"，恰恰相反。`any` 在标准库的三大类型擦除件（`optional` / `variant` / `any`）里是存在感最低的一个——多数你以为该用 `any` 的地方，其实 `variant` 更合适、更安全、更快。但 `any` 确实有几处不可替代的边缘场景，而且它"怎么把任意类型塞进同一个类型"的机制，本身值得拆开看一眼。所以我们诚实地讲：`any` 是什么、它怎么存东西、什么时候它真的比 `variant` 强、什么时候用它是在给自己挖坑。

## any 到底存了什么：类型擦除的最朴素做法

`std::any` 的对外承诺很朴素——同一个 `any` 类型，能先后装下不同类型的值：

```cpp
// Standard: C++17
#include <any>
#include <iostream>

int main()
{
    std::any a = 1;           // 装 int
    std::cout << a.type().name() << ": " << std::any_cast<int>(a) << '\n';
    a = 3.14;                 // 同一个 a，现在装 double
    std::cout << a.type().name() << ": " << std::any_cast<double>(a) << '\n';
    a = std::string("hi");    // 现在装 string
    std::cout << a.type().name() << ": " << std::any_cast<std::string>(a) << '\n';
}
```

用 `g++ -std=c++23 -O2`（本机 GCC 16.1.1）跑出来：

```text
i: 1
d: 3.14
NSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE: hi
```

注意那个又臭又长的 `type().name()` —— 它是 ABI 的内部名修饰（mangled name），`i` 是 `int`、`d` 是 `double`，`string` 那串（`NSt7...` 开头）是 libstdc++ 的实现细节，换编译器/库版本还会变。`type()` 返回的是 `const std::type_info&`，你一般不会去读它的 `name()`，而是拿它跟 `typeid(T)` 比，这一点等下用到再说。

这里要讲清楚的是：**`any` 凭什么能让同一个静态类型先后装下不同的值类型？** 答案是它把"具体是什么类型"这个信息从编译期推迟到了运行期，手法叫做类型擦除（type erasure）。`any` 对象内部存了两样东西：一块用来放值本身的存储区，以及一组"知道这个值是什么类型、怎么拷贝/销毁它"的函数指针（标准库里通常叫 manager 或 handler）。你在编译期看到的 `std::any` 是个固定类型，但它在运行期拿着一坨类型信息，靠虚函数或函数指针分发，去调用正确类型的构造、拷贝、析构。

这就解释了 `any` 的第一个硬约束：**它只能装 `CopyConstructible` 的类型**。因为 `any` 自己是可拷贝的（拷贝一个 `any` 会拷贝它装的东西），而要拷贝一个未知类型的值，标准库就得在类型擦除时预先留好"怎么拷贝它"的函数。不可拷贝的类型，这个函数根本写不出来，于是编译期就被挡住。实测一下，把一个 `MoveOnly`（含 `unique_ptr`）塞进去：

```cpp
// Standard: C++17
#include <any>
#include <memory>

struct MoveOnly {
    std::unique_ptr<int> p;
    MoveOnly() : p(std::make_unique<int>(1)) {}
};

int main()
{
    std::any a = MoveOnly{};   // 编译失败：MoveOnly 不可拷贝
    (void)a;
}
```

GCC 16.1.1 当场拒绝：

```text
error: conversion from ‘MoveOnly’ to non-scalar type ‘std::any’ requested
   12 |     std::any a = MoveOnly{};   // 编译失败：MoveOnly 不可拷贝
```

报错直白——`MoveOnly` 不能转成 `std::any`。这是 `any` 和 `variant` 一个本质区别的伏笔：`variant` 只要它的每个候选类型可析构就行（拷贝/移动是按需的），而 `any` 连"装进去"都要求类型可拷贝。所以要装 `unique_ptr` 这种只能移动的类型，`any` 根本办不到，得另想办法（比如 `std::move_only_function` 那类更晚近的擦除件）。

## make_any 与 any_cast 的两种重载

存值用 `make_any<T>(args...)` 或直接赋值，取值用 `any_cast<T>`。`any_cast` 有两种重载，行为差异很大，这是用 `any` 最大的坑之一：

```cpp
// Standard: C++17
#include <any>
#include <iostream>
#include <string>

int main()
{
    std::any b = std::string("hello");

    // 值形式：类型不符抛 std::bad_any_cast
    auto* sp = std::any_cast<std::string>(&b);   // 指针重载：失败返回 nullptr，不抛
    auto* ip = std::any_cast<int>(&b);
    std::cout << "any_cast<string>(&b) = " << (sp ? sp->c_str() : "nullptr") << '\n';
    std::cout << "any_cast<int>(&b)    = " << (ip ? "non-null" : "nullptr") << '\n';

    try {
        [[maybe_unused]] auto v = std::any_cast<double>(b);  // 值重载：b 里是 string
    } catch (const std::bad_any_cast& e) {
        std::cout << "caught bad_any_cast: " << e.what() << '\n';
    }
}
```

跑出来：

```text
any_cast<string>(&b) = hello
any_cast<int>(&b)    = nullptr
caught bad_any_cast: bad any cast
```

两条规则要记牢：

- **指针重载 `any_cast<T>(&any)`**：传入 `any` 的地址，返回 `T*`（或 const 重载的 `const T*`）。类型对得上返回指向内部值的指针，对不上返回 `nullptr`，**绝不抛异常**。这是"我自己想检查类型、自己处理失败"时该用的形式。
- **值重载 `any_cast<T>(any)`**：直接返回 `T` 的副本（或引用）。类型对不上直接抛 `std::bad_any_cast`。这是"我确信里面就是 `T`、错了程序也就没法继续"时才该用的形式。

::: warning any_cast 要求精确类型匹配，不做任何转换
`any_cast` 是按 `typeid` 严格比对的，**不做隐式转换**。存的是 `int`，用 `any_cast<long>` 取，返回 `nullptr`（指针形式）或抛异常（值形式）；存的是 `unsigned`，用 `any_cast<int>` 也取不出来。实测一下：

```text
stored int; any_cast<long>  -> nullptr
stored int; any_cast<double> -> nullptr
stored unsigned; any_cast<int>      -> nullptr
stored unsigned; any_cast<unsigned> -> ok
```

`int` 跟 `long`、`int` 跟 `unsigned int`、`int` 跟 `double`——这些在普通 C++ 里能互相隐式转换的类型，在 `any_cast` 这里**全部是不同类型**。这是新手最常踩的坑：存的时候随手写了 `42u`（`unsigned`），取的时候按 `int` 取，结果拿到了 `nullptr`，一脸懵。记住：`any_cast` 的类型参数必须跟当初存进去的类型**一字不差**。
:::

## SBO：小对象内联，大对象上堆

讲类型擦除时我们埋了个问题：那块"放值本身的存储区"到底在哪？是每次都堆分配吗？标准没有强制，但 cppreference 明确写着：*"Implementations are encouraged to avoid dynamic allocations for small objects"*（鼓励实现为小对象避免动态分配）。三大实现（libstdc++ / libc++ / MSVC STL）都实现了小对象优化（Small Buffer Optimization，SBO），机制跟 `std::string` 的 SSO 同源：`any` 对象内部预留一小段内联缓冲区，装得下就直接放里面，装不下才去堆上 `new`。

我们直接拿实测来看这条边界。先看 `any` 自己有多大：

```cpp
// Standard: C++17
#include <any>
#include <array>
#include <iostream>
#include <string>

int main()
{
    std::cout << "sizeof(std::any)            = " << sizeof(std::any) << '\n';
    std::cout << "sizeof(void*)               = " << sizeof(void*) << '\n';
    std::cout << "sizeof(std::string)         = " << sizeof(std::string) << '\n';
    std::cout << "sizeof(std::array<char,64>) = " << sizeof(std::array<char,64>) << '\n';
}
```

libstdc++ 16 跑出来：

```text
sizeof(std::any)            = 16
sizeof(void*)               = 8
sizeof(std::string)         = 32
sizeof(std::array<char,64>) = 64
```

`sizeof(std::any)` 是 **16 字节**。这 16 字节里塞了：一块内联缓冲区（用来放小对象本身），加上一个函数指针（指向那个"知道怎么管理这个值"的 manager）。两者挤在一起，所以真正能内联存放的 payload 大小，远小于 16——因为指针本身就要占地方。那到底能装多大的对象才不上堆？我们用一个会按地址差判断"值在不在 `any` 对象内部"的小探针扫一遍：

```cpp
// Standard: C++17
#include <any>
#include <array>
#include <cstdio>
#include <cstddef>

template <std::size_t N>
struct Blob {
    std::array<unsigned char, N> data{};
};

template <std::size_t N>
void probe()
{
    std::any a = Blob<N>{};
    auto* p = std::any_cast<Blob<N>>(&a);
    // delta 小 => 值在 any 对象内部(SBO)；delta 大/像堆地址 => 堆分配
    long delta = (long)((char*)p - (char*)&a);
    std::printf("N=%3zu  sizeof(Blob)=%3zu  -> %s\n",
                N, sizeof(Blob<N>),
                (delta >= 0 && delta < 32) ? "INLINE (SBO)" : "HEAP");
}

int main()
{
    probe<1>();  probe<8>();  probe<12>();  probe<16>();  probe<32>();  probe<64>();
}
```

跑出来：

```text
N=  1  sizeof(Blob)=  1  -> INLINE (SBO)
N=  8  sizeof(Blob)=  8  -> INLINE (SBO)
N= 12  sizeof(Blob)= 12  -> HEAP
N= 16  sizeof(Blob)= 16  -> HEAP
N= 32  sizeof(Blob)= 32  -> HEAP
N= 64  sizeof(Blob)= 64  -> HEAP
```

libstdc++ 16 的 SBO 临界点很干脆：**大小不超过一个指针（8 字节）的对象走内联，超过就上堆**。12 字节就溢出了。这是个值得直观记住的事实——它意味着 `int`、`double`、裸指针这些常见标量进 `any` 不分配，但 `std::string`（`sizeof` 是 32，本身又带 SSO）、`std::vector`、任何有点规模的结构体，塞进 `any` 都会触发一次堆分配。

我们再把 SBO 和堆分配的代价用分配计数量化一遍。下面这段重载了全局 `operator new` 来数 `any` 到底分配了几次：

```cpp
// Standard: C++17
#include <any>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

static std::size_t g_alloc_count = 0;
void* operator new(std::size_t n) { ++g_alloc_count; return std::malloc(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    constexpr int N = 1'000'000;

    g_alloc_count = 0;
    {
        std::vector<std::any> as;
        as.reserve(N);
        for (int i = 0; i < N; ++i) as.emplace_back(i);   // int -> SBO，不该有额外分配
    }
    std::cout << "any(int)  [SBO]:  allocs during build = " << g_alloc_count << '\n';

    struct Big { std::int64_t d[8]; };   // 64 字节，必上堆
    g_alloc_count = 0;
    {
        std::vector<std::any> as;
        as.reserve(N);
        for (int i = 0; i < N; ++i) as.emplace_back(Big{i,0,0,0,0,0,0,0});
    }
    std::cout << "any(Big64)[heap]: allocs during build = " << g_alloc_count << '\n';
}
```

跑出来：

```text
any(int)  [SBO]:  allocs during build = 1
    （那 1 次是 vector 自己 reserve 容量）
any(Big64)[heap]: allocs during build = 1000001
    （reserve 1 次 + 每个元素堆分配 1 次）
```

数字不会撒谎。装 `int`，一百万个元素 0 次额外分配（SBO 把它们全内联了）；装 64 字节的大对象，**每一个元素都单独堆分配一次**，一百万次。这就是 `any` 装大对象的真实代价——不光是访问慢，光是构造就把分配器打爆。如果你在热路径上用 `any` 装大对象，这是个实打实的性能问题。

## 与 variant 对比：为什么多数时候你该用 variant

到这里我们可以正面回答开篇那个问题了：既然 `any` 这么灵活，为什么说它"多半用不到"？因为它把 `optional` / `variant` 那一套编译期类型安全，换成了运行期类型擦除，而代价全压在了你身上。我们把 `variant` 和 `any` 放在一起对比着看。

第一，**类型集合是开放还是封闭**。`variant<int, double, string>` 把候选类型钉死在三个，这是"封闭集合"——你在写代码时就知道值只可能是这三种之一，编译器也知道，于是 `std::visit` 能保证你把所有分支都处理了，访问 `std::get<T>` 时类型对不对在很大程度是编译期可查的（不对会抛 `bad_variant_access`，但因为你列了类型清单，写错的概率低得多）。`any` 是"开放集合"——任何 `CopyConstructible` 类型都能塞，编译器没法帮你核对，类型对不对**只有运行期 `any_cast` 那一下才知道**，错了就抛异常或拿 `nullptr`。

第二，**能不能遍历所有可能**。`variant` 配 `std::visit`，能写出"不管现在装的是哪个候选，我都能统一处理"的代码：

```cpp
// Standard: C++17
#include <iostream>
#include <variant>

int main()
{
    std::variant<int, double, std::string> v = std::string("hi");
    std::visit([](auto&& x) { std::cout << "variant holds: " << x << '\n'; }, v);
}
```

```text
variant holds: hi
```

`any` 没有等价物——因为它根本不知道"候选类型集合"是什么，没法遍历。你要么在调用点**显式说出类型**（`any_cast<std::string>(a)`），要么自己 `if (a.type() == typeid(X)) ... else if ...` 一个个猜。这正是类型擦除的代价：类型信息从签名里消失了，所有"用类型信息"的活儿都得你自己在调用点补回来。

第三，**性能**。我们做个对比：同样是装一百万个 `int`，`variant<int,double>` 取值 vs `any` 取值，各跑一百万次 `get` / `any_cast`：

```text
variant<int,double>: access 1000000 ints = 1259 us
any(int) [SBO]:      any_cast<int> access 1000000 ints = 1340 us
```

（绝对值随机器波动，这里只看量级。）访问耗时两者其实差不多——都是"读个类型标签再分发"的套路。`variant` 的优势不在单次访问快，而在**类型集合已知带来的零额外分配和编译期可核对**：`variant` 永远不分配（它的大小就是"最大候选 + 一个 index"），而 `any` 装大对象就要堆分配；`variant` 写错类型编译器能警告，`any` 写错类型只能运行期炸。

所以一条很实在的经验：**你能列出所有可能类型的时候，永远用 `variant`**。`variant` 的类型集合是封闭的、编译期可见的、零分配的；`any` 只有在"连你自己都不知道会有哪些类型"时才有意义。

## 那 any 到底什么时候真该用

讲了这么多"别用"，`any` 并不是没用的摆设。它真正不可替代的场景有一个共同特征：**类型集合是开放的，而且消费端不关心具体是哪种类型**。最典型的两个：

**属性表 / 配置表**。一个配置系统要装各种类型的值——超时是 `int`、主机名是 `string`、重试次数是 `unsigned`、某个开关是 `bool`——而写配置框架的人不可能预见到所有配置项的类型。这种"键值对、值类型五花八门"的场景，`map<string, any>` 是个自然的落点：

```cpp
// Standard: C++17
#include <any>
#include <iostream>
#include <map>
#include <string>

int main()
{
    std::map<std::string, std::any> props;
    props["timeout"] = 30;                       // int
    props["host"]    = std::string("localhost"); // string
    props["retries"] = 3u;                       // unsigned

    auto get_int = [&](const std::string& key) -> int {
        auto it = props.find(key);
        if (it == props.end()) return -1;
        auto* p = std::any_cast<int>(&it->second);   // 指针重载，安全取值
        return p ? *p : -1;
    };

    std::cout << "timeout=" << get_int("timeout")
              << "  host=" << std::any_cast<std::string>(props["host"])
              << "  retries-as-int=" << get_int("retries") << '\n';
}
```

跑出来：

```text
timeout=30  host=localhost  retries-as-int=-1
```

注意 `retries-as-int=-1` 这一格——`retries` 存的是 `unsigned`，用 `any_cast<int>` 取取不出来，指针重载返回 `nullptr`，我们安全地兜底成了 `-1`。这正是 `any` 属性表的正确用法：消费端用**指针重载**做防御式取值，类型对不上有明确的失败语义，而不是一抛异常整个程序崩。这也呼应了前面那个警告——`int` 和 `unsigned` 在 `any` 里是两个不同类型，存取必须严格对应。

**跨边界的"值信封"**。当值要穿越一层你无法干预的边界——比如某个消息系统、某个脚本绑定层、某个插件接口——而你只想透传"一个值"而不关心它具体是什么，`any` 是个不挑类型的信封。接收方拿到之后再按自己知道的方式拆。这种场景下，"类型集合开放 + 不关心具体类型"两条都成立，`any` 才是真的合适。

反过来，下面这些场景**都不是**用 `any` 的理由，换 `variant`：

- "这个值可能是 A 或 B 或 C"——类型列得出来，用 `variant<A,B,C>`。
- "这个值可能没有"——用 `optional<T>`。
- "我想存一堆不同类型的对象"——如果你能在写代码时列出它们，用 `variant`；只有真列不出来（比如配置框架）才轮到 `any`。

## 与 void* 和模板的对比：类型擦除的三条路

把 `any` 放回"类型擦除"这个更大的语境里，会更清楚它的位置。C++ 里有三条把具体类型藏起来的路：

- **`void*`**：最原始、最危险。什么指针都能转成 `void*` 再转回来，但类型信息**彻底丢失**，转错类型编译器一声不吭，运行期直接未定义行为。`any` 可以理解为"带类型信息的安全 `void*`"——它在内部记着原本是什么类型，`any_cast` 会按 `typeid` 核对，对不上抛异常而不是默默 UB。
- **模板**：把类型留在编译期，零运行期开销、零类型擦除，但代价是"用模板写的代码，类型在调用点必须都已知"，而且模板代码会实例化出多份。模板适合"类型在编译期都明确"的场景，`any` 适合"编译期说不清"的场景，两者不矛盾。
- **`any` / `variant` / `function`**：标准库提供的类型擦除件。`any` 擦除"单值的具体类型"，`variant` 把类型集合列出来再擦除判别信息，`function` 擦除"可调用对象的具体类型"。它们的共同点是：在编译期固定一个签名/外壳，把"具体是哪个类型"的细节推迟到运行期，但都保留了类型安全的访问（错了抛异常而不是 UB）。

所以 `any` 不是 `void*` 的时髦包装，它是有运行期类型核对的安全件；它也不是模板的对立面，而是补上了模板够不到的那块"编译期类型未知"的空地。理解了它在这三条路里的位置，你就知道什么时候该选它、什么时候不该。

## 几个真实容易踩的点

把这一路用到的坑集中收一下，每个都是上面实测验证过的：

::: warning any_cast 要求精确类型，不做任何转换
`int` 取成 `long`、`unsigned` 取成 `int`、`int` 取成 `double`——在 `any_cast` 里**全是失败**。指针重载返回 `nullptr`，值重载抛 `bad_any_cast`。记住 `any_cast` 的模板参数必须和存进去的类型一字不差，存的时候随手写了个字面量（`42u` 是 `unsigned`、`42` 是 `int`），取的时候就要对上。
:::

::: warning any 只能装 CopyConstructible 类型
不可拷贝的类型（含 `unique_ptr` 的、删了拷贝构造的）连编译都过不了。`variant` 没这个限制——它只要求候选类型可析构。要装只能移动的类型，`any` 帮不了你，考虑 `std::move_only_function`（C++23）或自己写擦除层。
:::

::: warning 装大对象 = 每个元素一次堆分配
libstdc++ 的 SBO 只容得下一个指针大小（8 字节）的 payload，超过就上堆。装 `string`/`vector`/有规模的结构体，构造时各分配一次，热路径上要当心。能预知类型就别用 `any`，`variant` 零分配。
:::

::: warning 别用 any 替代 variant
这是最常见也最隐蔽的误用。凡是能列出候选类型的，用 `variant` + `visit`/`get`：编译期可核对、零分配、能遍历。`any` 留给"类型集合开放、消费端不关心具体类型"的边缘场景（属性表、跨边界值信封）。
:::

## 小结

`std::any` 是标准库里那个"能装任意 `CopyConstructible` 类型"的兜底容器，但在 `optional` / `variant` / `any` 三件套里，它是最该谨慎使用的那个。几条关键结论收一下：

- `any` 靠类型擦除把"具体是什么类型"推迟到运行期：内部一块存储区 + 一组管理函数指针，因此**只能装 `CopyConstructible` 类型**，不可拷贝的类型（如含 `unique_ptr`）编译期就被挡住。
- `any_cast<T>` 有两种重载：值形式类型不符抛 `bad_any_cast`，指针形式（传 `&any`）返回 `nullptr` 不抛。消费端做防御式取值用指针重载。无论哪种，**都要求精确类型匹配，不做任何隐式转换**。
- SBO：libstdc++ 16 的内联临界点是 8 字节（一个指针），`int`/`double`/裸指针内联零分配，`string` 及更大对象每个都上堆。实测装 100 万个 64 字节对象 = 100 万次堆分配。
- 多数场景该用 `variant`：候选类型封闭、编译期可核对、零分配、能 `visit` 遍历。`any` 的不可替代场景是"类型集合开放且消费端不关心具体类型"——典型如配置/属性表、跨边界值信封。
- 在类型擦除的谱系里，`any` 是"带运行期类型核对的安全 `void*`"，跟模板（编译期、零开销）分工明确，不是二选一。

一句话收尾：**写 `any` 之前，先问自己一句"我列得出所有可能类型吗"——列得出就用 `variant`，列不出再用 `any`。** 这个习惯能挡掉九成对 `any` 的误用。

## 参考资源

- [cppreference: std::any](https://en.cppreference.com/w/cpp/utility/any) —— 类型擦除容器的规范，CopyConstructible 要求与"鼓励为小对象避免动态分配"的 SBO 说明
- [cppreference: std::any_cast](https://en.cppreference.com/w/cpp/utility/any/any_cast) —— 值形式抛 `bad_any_cast`、指针形式返回 `nullptr` 的两套重载
- [cppreference: std::bad_any_cast](https://en.cppreference.com/w/cpp/utility/any/bad_any_cast) —— 值重载类型不符时抛出的异常
- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) —— 封闭类型集合的判别联合，多数场景下 `any` 的更优替代
