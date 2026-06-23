---
title: "variant：类型安全的联合体与 visit"
description: 讲透 std::variant 为什么取代带 tag 的裸 union——自动析构与索引、get/get_if/holds_alternative 的取舍、overloaded lambda 配 std::visit 做模式匹配、valueless_by_exception 的病态状态，以及 variant 在封闭类型集合上为何比继承多态更值语义、更省内存
chapter: 7
order: 62
cpp_standard:
- 17
- 20
difficulty: intermediate
platform: host
tags:
  - host
  - cpp-modern
  - intermediate
  - 类型安全
prerequisites:
  - "对象大小、对齐与平凡类型"
  - "vector 深入：三指针、扩容与迭代器失效"
related:
  - "容器选择指南：按操作、内存与失效规则挑对容器"
reading_time_minutes: 16
---

# variant：类型安全的联合体与 visit

我们写过很多「同一个变量，有时候是 A，有时候是 B」的代码。状态机里一个连接可能是 `Connecting`、`Connected`、`Error`；解析器里一个 token 可能是数字、字符串或符号；配置项可能是一个标量、也可能是一个列表。做法传统上有两条：要么搞个 `enum` 加一个 `union`，自己记「现在到底是哪个」；要么拉一套继承体系，`class Shape` 下面挂 `Circle`、`Square`、`Triangle`，靠虚函数分发。

这两条路各有各的难受。`union` 不记当前是哪个类型——你往里塞了个 `int`，读出来当成 `string` 用，编译器一声不吭，运行时直接是未定义行为，析构更是一笔糊涂账（`string` 的析构该调没调，内存就漏了）。继承多态倒是类型安全，但每个对象都得 `new` 到堆上、挂个虚表指针，光是为了「存一个值」就搭进去一次堆分配和一次间接跳转，还要操心生命周期。

C++17 给了第三条路：`std::variant<Ts...>`，一个**类型安全的联合体**。它在 `union` 的「共用一块内存」基础上，多记了一个「当前是第几个类型」的索引，并且自动管析构。这一篇我们把它从「为什么不用裸 union」一路讲到 `std::visit` 的模式匹配、`valueless_by_exception` 的怪状态，最后跟继承多态正面对比一下性能。

## 为什么不用裸 union

先看裸 `union` 到底烂在哪。下面这段代码，编译器一句警告都不给，但它就是错的：

```cpp
// Standard: C++98
union BadUnion {
    int i;
    std::string s;   // 带 non-trivial 成员的 union
};

void misuse() {
    BadUnion u;
    u.s = std::string("hello");   // 当 string 存
    int x = u.i;                  // 当 int 读 —— 未定义行为
    // 函数结束: 没人调 string 析构, 内存泄漏
}
```

`union` 自己**不知道**现在存的是 `int` 还是 `string`。从 `string` 当 `int` 读，是 UB；`string` 该调的析构函数没人调，是泄漏。要正确用，程序员得在外面挂一个 tag、手动判断、手动析构——这一整套样板代码，正确性全靠人肉保证。笔者见过太多「以为 union 省内存，结果留下一堆内存泄漏」的代码。

`std::variant` 把这套全自动化了。它干两件事：

1. **记索引**：内部存一个「当前是第几个备选类型」的下标。`index()` 拿得到，`holds_alternative<T>()` 直接判断。
2. **自动析构**：每次换类型（赋值、`emplace`），它先析构掉旧的，再构造新的。生命周期到了，它析构当前持有的那个。

代价是它多占了一点空间存那个索引（通常就是几个字节），换来的就是「读到错误的类型会抛异常而不是 UB，析构永远正确」。

## 构造与访问：四件套

最基础的用法，我们直接跑一遍：

```cpp
// Standard: C++17
#include <variant>
#include <string>
#include <iostream>

int main()
{
    std::variant<int, double, std::string> v;  // 默认构造 -> 持第一个类型(int)
    std::cout << "默认构造 index=" << v.index() << " (int)\n";

    v = 3.14;                                  // 赋 double
    std::cout << "赋值 3.14 index=" << v.index() << " (double)\n";

    v = std::string("hello");                  // 赋 string
    std::cout << "赋值 hello index=" << v.index() << " (string)\n";

    // 1. holds_alternative<T>: 当前是不是 T?
    std::cout << "holds<string>=" << std::holds_alternative<std::string>(v) << "\n";
    std::cout << "holds<int>="    << std::holds_alternative<int>(v) << "\n";

    // 2. get<T>: 取值, 类型不符抛 bad_variant_access
    std::cout << "get<string>=" << std::get<std::string>(v) << "\n";
    try {
        std::cout << std::get<int>(v) << "\n";   // 当前是 string, 取 int
    } catch (const std::bad_variant_access& e) {
        std::cout << "异常: " << e.what() << "\n";
    }

    // 3. get_if<T>: 取指针, 不符返 nullptr (不抛)
    if (auto* p = std::get_if<double>(&v)) {
        std::cout << "double: " << *p << "\n";
    } else {
        std::cout << "不是 double, get_if 返回 nullptr\n";
    }

    // 4. get<I>: 按索引取(0=int, 1=double, 2=string)
    std::cout << "get<2>=" << std::get<2>(v) << "\n";
    return 0;
}
```

用 `g++ -std=c++23 -O2`（本机 GCC 16.1.1）跑出来：

```text
默认构造 index=0 (int)
赋值 3.14 index=1 (double)
赋值 hello index=2 (string)
holds<string>=1
holds<int>=0
get<string>=hello
异常: std::get: wrong index for variant
不是 double, get_if 返回 nullptr
get<2>=hello
```

四件套该怎么选，关键看「类型不符时你想怎么办」：

- **想抛异常**：用 `get<T>()`。干净，但每次访问都有一次分支 + 可能的异常开销。
- **不想抛，自己处理**：用 `get_if<T>()`，返回 `nullptr` 就说明类型不对。在性能敏感或禁用异常的代码里，这是更稳的选择。
- **只想判断、不取值**：`holds_alternative<T>()` 返回 `bool`，读起来最清楚。
- **按位置而不是按类型**：`get<I>()`。偶尔有用，比如遍历一个编译期已知的索引序列时。

::: warning get 和 get_if 的「类型」必须是备选类型之一
`std::get<long>(v)` 在 `variant<int, double, string>` 上是**编译错误**——`long` 不在备选集合里。`variant` 的类型安全正来自于「只能取它声明过的那几种」，不像裸 `union` 想读成什么就读成什么。
:::

## std::visit：把 if-else 链变成模式匹配

到这里你可能会说：四件套够用了，写一堆 `if (holds_alternative<A>) ... else if (holds_alternative<B>) ...` 不就完了？能跑，但有几个问题。第一，丑——每加一个类型就得回来改这个链，忘了就是漏处理。第二，慢——每次访问都是一次 `holds_alternative` 分支。第三，编译器不帮你检查「是不是每个类型都处理了」。

`std::visit` 解决的就是这三件事。它把一个「访问者」函数对象喂给 `variant`，要求这个访问者能处理**每一个**备选类型——漏一个就编译失败。我们在上面跑个最小例子之前，先介绍让它真正好用的关键技巧：**overloaded lambda**。

访问者得是一个能对所有备选类型调 `operator()` 的对象。最直接的写法是手写一个 `struct`：

```cpp
// Standard: C++17
struct Describe {
    std::string operator()(int i) const { return "int:" + std::to_string(i); }
    std::string operator()(double d) const { return "double:" + std::to_string(d); }
    std::string operator()(const std::string& s) const { return "string:\"" + s + "\""; }
};
```

能用，但每加一个分支就得回这个 `struct` 里加一行成员函数，啰嗦。C++17 有个干净得多的写法——把一组 lambda 继承到一起，凑成一个能匹配所有类型的函数对象：

```cpp
// Standard: C++17
template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
```

这三行是 C++ 社区的「公共咒语」（`using Ts::operator()...` 是 C++17 的 pack-using 声明，推导指引让一组 lambda 直接推出 `overloaded<L1, L2, ...>`）。配合 `std::visit`，上面那个 `Describe` 就能写成一组就地 lambda：

```cpp
// Standard: C++17
#include <variant>
#include <string>
#include <vector>
#include <iostream>

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

using Value = std::variant<int, double, std::string>;

std::string describe(const Value& v)
{
    return std::visit(overloaded{
        [](int i)                -> std::string { return "int:" + std::to_string(i); },
        [](double d)             -> std::string { return "double:" + std::to_string(d); },
        [](const std::string& s) -> std::string { return "string:\"" + s + "\""; }
    }, v);
}

int main()
{
    std::vector<Value> vals{42, 3.14, std::string("hello"), 7};
    for (const auto& v : vals) std::cout << describe(v) << "\n";
    return 0;
}
```

```text
int:42
double:3.140000
string:"hello"
int:7
```

这一段的威力在于：`std::visit` 在编译期就知道 `variant` 一共有哪几个类型，访问者的 `operator()` 重载集合也全是编译期已知的，于是它能**把整组分发编译成一个跳转表**（通常就是按 `index()` 的一次间接跳转），没有运行期的 `holds_alternative` 链，也没有继承那套虚表间接寻址。而且——一旦 `Value` 加了第四个类型而你忘了在 `overloaded` 里处理，**直接编译失败**。这是编译器在帮你查漏，比手写 `if-else` 链安全得多。

::: warning overloaded 的三行咒语不要背错
`using Ts::operator()...;` 末尾的 `...` 不能漏——它表示「把每个基类的 `operator()` 都 using 进来」，漏了就是只引入一个，分发不全。推导指引 `overloaded(Ts...) -> overloaded<Ts...>;` 也别忘，少了它你没法直接 `overloaded{...}` 就地构造。C++20 之后这套写法仍然成立，是社区最稳的范式。
:::

## C++20 的两个新工具：就地 lambda + visit\<R\>

到了 C++20，上面那个 `overloaded` 咒语其实可以省掉——直接用一个泛型 lambda 配 `if constexpr`，就地写「看到啥类型就干啥」：

```cpp
// Standard: C++20
#include <variant>
#include <string>
#include <vector>
#include <iostream>
#include <type_traits>

struct Connect { std::string addr; };
struct Disconnect {};
struct Data { std::vector<unsigned char> bytes; };
using Event = std::variant<Connect, Disconnect, Data>;

int main()
{
    std::vector<Event> evs{
        Connect{"10.0.0.1"},
        Data{{1, 2, 3}},
        Disconnect{},
    };
    for (const auto& e : evs) {
        std::visit([](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Connect>) {
                std::cout << "connect -> " << x.addr << "\n";
            } else if constexpr (std::is_same_v<T, Disconnect>) {
                std::cout << "disconnect\n";
            } else {
                std::cout << "data " << x.bytes.size() << " bytes\n";
            }
        }, e);
    }
    return 0;
}
```

```text
connect -> 10.0.0.1
data 3 bytes
disconnect
```

泛型 lambda + `if constexpr` 的好处是「不要求每个分支返回类型一样」，坏处是分支得自己写 `is_same_v`，没 `overloaded` 那么整齐。两种写法都能用，按场景挑——分支少、各自返回类型差不多，用 `overloaded`；分支里有复杂逻辑或不同返回类型，用泛型 lambda。

C++20 还给 `std::visit` 加了一个显式返回类型形式 `std::visit<R>(...)`，用来「强制所有分支的返回值都转换成同一个类型 `R`」。这在各个分支天然返回不同类型、但你想要一个公共类型（比如都转 `double`）时很顺手：

```cpp
// Standard: C++20
std::variant<int, double> v = 2;
double r = std::visit<double>([](auto x){ return x; }, v);  // int 分支也转成 double
```

实测 GCC 16.1.1 两种 C++20 写法都正常。注意：C++23 **没有**给 `variant` 加 `optional`/`expected` 那套 monadic 接口（`.and_then` / `.transform` / `.or_else`）。`variant` 不像 `optional` 有「空」语义——它永远持有一个值（除了下面要讲的病态状态），所以设计上没塞 monadic 链。要那一套去翻 `optional` 和 `expected` 各自的专篇。

## valueless_by_exception：variant 唯一的病态状态

我们前面一直在说「variant 永远持有一个值」，这句话基本对，但有一个例外。`variant` 有一个叫做 `valueless_by_exception()` 的状态，字面意思是「因为异常，变体没值了」。听起来很怪——一个声称永远有值的类型，怎么就没值了？

这要从赋值/`emplace` 的异常保证说起。当你执行 `v = new_value`，`variant` 要做两件事：先销毁旧值，再构造新值。如果「构造新值」这一步抛了异常，而且实现没法把旧值恢复回来，`variant` 就进了一个尴尬的中间态——旧的没了，新的没成。这时候它就是 `valueless`。

我们来人为制造一个：

```cpp
// Standard: C++17
#include <variant>
#include <iostream>
#include <stdexcept>

struct S {
    S() = default;
    S(const S&) { throw std::runtime_error("copy throw"); }  // 拷贝构造必抛
};

int main()
{
    std::variant<double, S> v = 1.5;   // 当前持 double
    std::cout << "before index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    S src;                              // 默认构造 OK
    try {
        v = src;                        // 拷贝构造 S -> 抛
    } catch (const std::runtime_error& e) {
        std::cout << "caught: " << e.what() << "\n";
    }
    std::cout << "after index=" << v.index()
              << " valueless=" << v.valueless_by_exception() << "\n";

    if (v.valueless_by_exception()) {
        try {
            (void)std::get<double>(v);   // 连原本的 double 都取不到了
        } catch (const std::bad_variant_access& e) {
            std::cout << "get<double> 也抛: " << e.what() << "\n";
        }
    }
    return 0;
}
```

```text
before index=0 valueless=0
caught: copy throw
after index=18446744073709551615 valueless=1
get<double> 也抛: std::get: variant is valueless
```

那个吓人的 `18446744073709551615` 就是 `variant::npos`（`(size_t)-1`，即 2^64-1），是 `valueless` 时 `index()` 的标记值。一旦进了这个状态，连原本那个 `double` 都取不回来了——`get<double>` 也抛 `bad_variant_access`，错误信息直接写 `variant is valueless`。

这状态有多容易碰到？说实话，很难。它要求「构造新值抛异常 + 实现无法回滚」，标准库里能让实现回滚的情况（比如新值是 nothrow 拷贝的）不会进 valueless。真正会触发它的，通常是你自己写了个拷贝/移动构造会抛的类型。工程上这状态基本可以当「不该出现，出现了说明你的类型异常保证有 bug」来对待——`valueless_by_exception()` 主要是给做库的人留的自检接口，业务代码里见到了，修那个抛异常的构造比处理 valueless 更对。

## variant vs 继承多态：封闭集合选谁

讲完机制，我们来回答一个最实际的问题：什么时候用 `variant`，什么时候用继承多态？关键就一个词——**类型集合是封闭的还是开放的**。

继承多态强在**开放**：基类定义好接口，任何人都能加一个新的派生类，不用动现有代码。你有一个 `Shape*` 数组，明天加一个 `Hexagon`，老代码一行不用改。代价是每个对象走虚表间接调用，对象通常得放堆上（多一次分配），缓存局部性差。

`variant` 强在**封闭**：所有可能的类型在编译期就列死了（`variant<A, B, C>`），加新类型要改这个声明，所有访问者都得跟着补一个分支——但这反过来是**好处**：编译器逼着你处理新类型，不会漏。而且 `variant` 是值语义、栈上存储、无虚函数开销，访问者分发出的是个紧凑的跳转表，缓存友好。

我们拿一个封闭的「形状集合」正面对比一下。三种形状 `Circle`/`Square`/`Triangle`，算面积——一套用继承 + 虚函数，一套用 `variant` + `visit`，400 万个对象各跑三轮：

```cpp
// Standard: C++17
// 继承: ShapeBase 虚函数 area(); variant: visit + AreaVisitor
// (完整代码见 /tmp/variant_lab/perf.cpp, 这里给关键骨架)
struct CircleV { double r; };
struct SquareV { double s; };
struct TriangleV { double b, h; };
using ShapeV = std::variant<CircleV, SquareV, TriangleV>;

struct AreaVisitor {
    double operator()(const CircleV& c)    const { return 3.14159265 * c.r * c.r; }
    double operator()(const SquareV& sq)   const { return sq.s * sq.s; }
    double operator()(const TriangleV& t)  const { return 0.5 * t.b * t.h; }
};

// 继承版: for (auto& p : poly) acc += p->area();
// variant 版: for (auto& v : vars) acc += std::visit(AreaVisitor{}, v);
```

本机 GCC 16.1.1、`-O2`，跑两遍：

```text
shapes: 4000000 x3 iters
inheritance (virtual): 87 ms
variant + visit:       54 ms
shapes: 4000000 x3 iters
inheritance (virtual): 78 ms
variant + visit:       55 ms
```

`variant + visit` 大约快 30%~40%。差距主要来自三点：`variant` 版的形状是一个紧挨一个排在 `vector` 里的（继承版是 `vector<unique_ptr>`，指针散在堆各处，缓存不命中）；`visit` 分发是按 `index` 的一次跳转表，没有虚表那层间接；以及没有 400 万次堆分配。绝对时间会随机器波动，但「variant 更快」这个数量级是稳健的。

当然，这是为对比而设计的场景——形状集合封闭、对象密集遍历。换成「插件式扩展、外部模块随时加新类型」，继承多态该用还得用。判断标准就一条：**你能事先列出所有类型吗？能，就用 variant；不能，就用继承。**

顺带说一句内存。`variant` 的大小等于「最大备选类型 + 索引」对齐后的结果，跟 `union` 一样要为最大的那个买单：

```text
sizeof(variant<int,double,string>) = 40   // 被 string(32) 主导 + 索引
sizeof(variant<int,int,int>)        = 8    // 三个 int 共用空间 + 索引
sizeof(variant<int>)                = 8    // 单个 int 也要带索引
sizeof(string)                      = 32
sizeof(int)                         = 4
sizeof(variant<char,char>)          = 2    // char + 1 字节索引
```

注意 `variant<int>` 不等于 `int`——哪怕只有一个备选，那点索引空间也省不掉。`variant<int, int, int>` 同样是 8 字节而不是 4：三个 `int` 共用同一块内存，但索引还得记「现在活的是第几个」。

## variant 想要「先空着」：monostate

有个常见需求：`variant` 默认构造会持**第一个**备选类型。可如果第一个类型没默认构造函数（比如它要求必须传参数），整个 `variant` 就没法默认构造了。这时候用一个占位类型 `std::monostate` 打头：

```cpp
// Standard: C++17
struct NoDefault {
    NoDefault() = delete;
    NoDefault(int) {}
};

std::variant<std::monostate, NoDefault, int> v;  // 默认持 monostate, 可默认构造
std::cout << "default index=" << v.index() << " (0=monostate)\n";
v.emplace<2>(42);
std::cout << "emplace<2>(42) index=" << v.index() << "\n";
```

```text
default index=0 (0=monostate)
emplace<2>(42) index=2
```

`monostate` 是个空的、可默认构造的类型，存在的唯一目的就是当 `variant` 的「空状态占位」。注意它和 `valueless_by_exception` 不一样——持 `monostate` 时 `variant` 是「有值」的，那个值就是 `monostate`；`valueless` 才是病态的「真没值」。如果你想要的是「可能没有值」的语义，那其实更该用 `std::optional<T>`，别拿 `variant<monostate, T>` 凑——`optional` 语义更直白、API 更顺手（见 `optional` 专篇）。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下：

::: warning variant 至少要有一个备选类型
`std::variant<>`（空参数包）是非法的，编译失败。`variant` 必须列至少一个类型——它「永远有值」的保证正是建立在「至少有一个备选」之上。
:::

::: warning get 的类型必须在备选集合里
`std::get<long>(variant<int, double, string>)` 是**编译错误**，不是运行时异常。`variant` 的类型安全靠「只能取声明过的类型」实现。要按运行时索引取，用 `get<I>()`，`I` 越界同样是编译错误。
:::

::: warning 别用 variant<monostate, T> 代替 optional
能编能跑，但语义绕。`optional<T>` 表达「有或没有」更直接，API（`has_value()`/`value()`/`value_or()`）也更顺手。`variant` 是「这几个类型之一」，把其中一个换成 `monostate` 去模拟「没有」，是杀鸡用牛刀还难读。
:::

::: warning overloaded 咒语背全
`using Ts::operator()...;` 末尾的 `...` 不能漏，推导指引 `overloaded(Ts...) -> overloaded<Ts...>;` 也不能漏。漏了要么编译失败，要么分发不全。这三行是固定写法，照抄即可。
:::

::: warning valueless 出现 = 你的类型异常保证有 bug
正常代码几乎不该见到 `valueless_by_exception() == true`。它只在「构造新值抛异常且无法回滚」时出现，通常意味着你某个类型的拷贝/移动构造抛了异常。修那个构造，别去写一堆 `if (v.valueless_by_exception())` 的防御代码。
:::

## 小结

`std::variant` 的定位很清楚——**类型安全的联合体，给封闭类型集合做值语义多态**。几条关键结论收一下：

- 相比裸 `union`：`variant` 多记一个索引、自动管析构，读错类型抛异常而不是 UB，代价是多占几字节存索引。
- 访问四件套：`holds_alternative<T>()` 判断、`get<T>()` 取值（不符抛异常）、`get_if<T>()` 取指针（不符返 `nullptr`，不抛）、`index()` 看当前位置。`get` 的类型必须在备选集合里，否则编译错误。
- `std::visit` + `overloaded` lambda 是 C++ 的模式匹配：访问者漏处理一个类型就编译失败，分发编译成跳转表，没有 `if-else` 链和虚表间接。C++20 还能省掉 `overloaded`，直接用泛型 lambda + `if constexpr`，以及 `visit<R>` 强制公共返回类型。
- `valueless_by_exception()` 是 variant 唯一的病态状态：构造新值抛异常且无法回滚时触发，`index()` 变成 `variant::npos`。正常代码不该见到，见到说明你的类型异常保证有问题。
- `variant` vs 继承：类型集合**封闭**选 `variant`（值语义、栈上、无虚函数开销、缓存友好，实测遍历比虚函数快三到四成）；**开放**选继承（随时加派生类，老代码不用改）。
- 想要「可能没值」用 `optional`，别用 `variant<monostate, T>` 凑。

下一篇我们去看 `std::any`——另一种「装任意类型」的方式，以及它和 `variant` 在「类型集合已知 vs 未知」上的根本分野。

## 参考资源

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant) —— 构造、访问、`index`、异常保证总览
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit) —— 访问者分发与 C++20 的 `visit<R>` 形式
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access) —— `get` 类型不符、`valueless` 时抛的异常
- [cppreference: std::variant::valueless_by_exception](https://en.cppreference.com/w/cpp/utility/variant/valueless) —— 病态状态的成因与 `variant::npos`
- [cppreference: std::monostate](https://en.cppreference.com/w/cpp/utility/monostate) —— 占位类型，让无默认构造的备选也能默认构造 variant
