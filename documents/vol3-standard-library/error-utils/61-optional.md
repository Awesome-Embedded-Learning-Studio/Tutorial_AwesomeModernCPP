---
chapter: 7
cpp_standard:
- 17
- 20
- 23
description: 讲透 std::optional——为什么不用裸指针/sentinel 值/pair<bool,T> 表示空、构造与访问（has_value/value/value_or/operator* 空时是 UB）、emplace/reset 的值语义生命周期，以及 C++23 monadic（and_then/or_else/transform）怎么把"可能没值"的链式查询从三层 if 压成一条链
difficulty: intermediate
order: 61
platform: host
prerequisites:
- variant：类型安全的联合体
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 16
related:
- std::expected<T, E>：类型安全的错误传播
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- optional
title: 'optional：把「可能没有」做成类型'
---

# optional：把「可能没有」做成类型

写过查找函数的人，对下面这种返回值都不陌生：找不到就返回 `-1`，找到了返回下标。在数组里找 `10`，拿到 `-1` 你知道是没找到；可如果哪天要找的值本身就允许是 `-1` 呢？这时候 `-1` 到底是「找到了，值是 -1」还是「没找到」？光看返回值，类型系统根本帮不了你，只能靠注释和约定。这种「靠约定」的写法，一旦换个不知道约定的人来接手，就是一颗定时炸弹。

`std::optional<T>`（C++17 起进入标准库，定义在 `<optional>`）解决的就是这个痛点。它把「可能没有值」这件事从注释和口头约定，提升成了**类型系统里的事实**：一个 `optional<int>` 要么装着一个 `int`，要么是空的，这个「有没有」是值本身的一部分，拿值之前你不得不面对它。这一篇我们把 optional 的设计动机、构造与访问、最容易踩的解引用空 optional 的未定义行为、以及 C++23 新加的 monadic 链式操作都跑一遍，看清楚它为什么值得用、又怎么用对。

## 先回答一个问题：现有的「表示空」方案差在哪

你可能会想，表示「没有」我早有一堆办法了，为什么还要专门搞个 `optional`？我们把三种最常见的土办法摆出来，逐个看它们的毛病。

**第一种：sentinel 值（哨兵）。** 找不到返回 `-1`、`nullptr`、空字符串。问题前面已经点过——sentinel 是一个**合法值域里的值**被你征用了，一旦业务上这个值本身有意义（下标 `-1`、空字符串作为合法输入），约定就自相矛盾。而且它完全靠人记，编译器不会替你检查。

**第二种：返回裸指针 `T*`。** 找到返回指向结果的指针，找不到返回 `nullptr`。这个方案看着干净，但有两个麻烦。一是**所有权歧义**：调用方拿到一个 `T*`，它不知道这个指针指向的东西归谁管、能不能删、什么时候失效——是指向容器内部元素（删了就悬空），还是指向堆上需要自己 `delete` 的对象？光看签名根本看不出来。二是**和值语义格格不入**：一个「装着值的盒子」明明是值类型（拷贝、移动、生命周期都该像普通变量一样），用指针反而把它变成了引用语义。

**第三种：`pair<bool, T>` 或 `struct { bool ok; T value; }`。** 看着很合理——带一个标记位说明有没有。可它的坑在「失败时 `value` 是什么」。你看下面这个实测：

```cpp
// Standard: C++17
struct Result { bool ok; int value; };
Result find_pair(int needle, const int* a, int n) {
    for (int i = 0; i < n; ++i) if (a[i] == needle) return {true, i};
    return {false, 0};   // 失败时 value=0 是凑数的, 不是真结果
}
```

找不到时 `value` 给个 `0` 凑数——可这个 `0` 谁都不能保证它不是个误报。更要命的是，调用方完全可以直接 `.value` 拿这个凑数的 `0` 来用，忘了先看 `.ok`，编译器一声不吭。`pair<bool, T>` 还有个隐藏成本：`T` 是个非平凡类型（比如 `string`）时，哪怕失败也得默认构造一个空 `T` 填进去，平白无故多了一次构造。

`optional` 把这些问题一次性解决了：「有没有」是类型的一部分，不是游离的 `bool`；它是**值类型**，拷贝移动析构都按值语义走，没有所有权歧义；空的时候里面压根没构造 `T`，也就没有「失败时还得凑个 `T`」的浪费。我们看个 `sizeof` 对比，先有个直观印象：

```cpp
std::cout << "sizeof(int):                   " << sizeof(int) << "\n";
std::cout << "sizeof(optional<int>):         " << sizeof(std::optional<int>) << "\n";
std::cout << "sizeof(pair<bool,int>):        " << sizeof(Result) << "\n";
std::cout << "sizeof(string):                " << sizeof(std::string) << "\n";
std::cout << "sizeof(optional<string>):      " << sizeof(std::optional<std::string>) << "\n";
```

GCC 16.1.1 上跑出来：

```text
sizeof(int):                   4
sizeof(optional<int>):         8
sizeof(pair<bool,int>):        8
sizeof(int*):                  8
sizeof(string):                32
sizeof(optional<string>):      40
```

`optional<int>` 是 8 字节——4 字节装 `int`，1 字节做「有没有」的标记位，剩下 3 字节是对齐填充。开销和 `pair<bool,int>` 一样大，但你换来了「拿值前必须面对空」的类型保护，值语义，以及空时不构造 `T` 的惰性。这笔账很划算。

## 构造与访问：四种拿值的方式

optional 的 API 不多，但拿值这一步有几个长得像、行为差很多的接口，得一个个分清楚。我们用一个最简单的例子把构造和访问都过一遍：

```cpp
// Standard: C++17
#include <optional>
#include <vector>
#include <string>

std::optional<int> find_first_even(const std::vector<int>& v) {
    for (int x : v) if (x % 2 == 0) return x;
    return std::nullopt;   // 显式返回"空"
}

int main() {
    std::optional<int> empty;           // 默认构造: 空
    std::optional<int> a = 42;          // 从值构造
    std::optional<int> b{a};            // 拷贝构造

    // 访问的四种方式
    a.has_value();     // true:  显式问"有没有"
    (bool)a;           // true:  operator bool, 等价于 has_value()
    a.value();         // 42:    空时抛 std::bad_optional_access
    *a;                // 42:    空时是未定义行为(下面单独讲)
    a.value_or(0);     // 42:    空时返回参数里的默认值
}
```

完整跑一遍，看真实输出：

```text
empty.has_value(): 0
empty as bool:     no
a.has_value():     1
a.value():         42
*a:                42
a.value_or(0):     42
empty.value_or(0): 0
find {1,3,5,8,9}: 8
find {1,3,5,7}:   none
```

四种访问方式的区别其实就一句话：**空的时候它们怎么处理，决定了你该用哪个。**

- `has_value()` / `operator bool()`——纯查询，最安全，空和不空都不会出事。
- `value()`——**空时抛 `std::bad_optional_access` 异常**。适合那种「我懒得在调用点判空，空了就是程序逻辑错了、直接抛出去让上层处理」的场景。
- `value_or(default)`——**空时返回你给的默认值**。最适合「有空就用默认值顶上」的兜底逻辑，一行搞定，不用写 `if`。
- `operator*` 和 `operator->`——**空时是未定义行为**。最快，但前提是你已经确认它非空。

`value()` 抛异常这件事，我们实测一下，免得空口断言：

```cpp
// Standard: C++17
std::optional<int> empty;
try {
    int v = empty.value();
} catch (const std::bad_optional_access& e) {
    std::cout << "caught: " << e.what() << '\n';
}
```

```text
caught: bad optional access
```

异常对象 `what()` 返回的就是 `"bad optional access"` 这么个串。注意 `bad_optional_access` 是从 `std::logic_error` 派生的——这意味着标准库把它归类为「程序逻辑错误」（本该判空却没判），而不是「运行时偶发错误」。换句话说，用 `value()` 靠异常兜底，等于承认「这里的空是 bug」，别拿它当正常控制流用。

## 真正的坑：解引用空 optional 是未定义行为

`value()` 空了会抛异常，那 `*empty` 呢？标准说得很清楚：**对空的 optional 解引用，是未定义行为（UB）**。它不会帮你判空，也不会抛异常——它直接是 UB。这件事的阴险之处在于，它**大多数时候不崩**，你就这么把一个错误的结果用下去了，直到某天换了编译器选项或平台才炸。

我们拿 GCC 16.1.1 实测，先看默认编译会发生什么：

```cpp
// Standard: C++17
std::optional<int> empty;
std::cout << *empty << '\n';   // 空的解引用: UB
```

用 `g++ -std=c++23 -O2` 直接编译运行：

```text
0
```

没崩，打印了个 `0`。但别被这个 `0` 骗了——它**不是 optional 在告诉你「我是空的」**，而是恰好读到了那块未初始化内存里默认的零值。换个场景、换个优化级别、换个类型，它完全可能是任何垃圾值，或者直接段错误。这就是 UB 的可怕之处：它今天「能跑」，恰恰是最危险的信号。

::: warning ASan 抓不住这个 UB
很多人第一反应是「上 AddressSanitizer 抓」。可实测下来，ASan 对这个 UB **无能为力**：

```text
O2 -fsanitize=address: 打印 0, 不报错, 正常退出
O2 -fsanitize=undefined: 打印 0, 不报错
```

原因在于 optional 内部用的是一块**合法分配的 union 内存**来装值，解引用空 optional 读的是这块内存——既不是 use-after-free（内存还活着），也不是越界（大小没超），ASan/UBSan 根本没把它当成错误。这块「读了但没构造过」的访问，属于「活跃但未初始化」的灰色地带，运行时 sanitizer 看不见。

想抓住它，得靠 libstdc++ 自带的 assertion。把同一个程序用 `-D_GLIBCXX_ASSERTIONS` 编译：

```text
/usr/include/c++/16.1.1/optional:1249: constexpr _Tp& std::optional<_Tp>::operator*() &
  [with _Tp = int]: Assertion 'this->_M_is_engaged()' failed.
退出码 134 (SIGABRT)
```

libstdc++ 的 `operator*` 里藏了一句 `__glibcxx_assert(this->_M_is_engaged())`，开了 `_GLIBCXX_ASSERTIONS` 它就在运行时替你判空，空了直接 abort。生产构建里要不要开这个宏（有少量性能代价），可以看团队取舍；但**调试阶段强烈建议开**，它能替你挡掉一大批「看起来能跑」的 UB。

话说回来，靠 assertion 是兜底，不是写代码的依据。正确的心态是：**`operator*` 只在你已经确认非空的语境里用**——比如刚 `if (opt)` 判过，或者 `opt.has_value()` 为真之后。否则就用 `value()`（让异常替你喊）或 `value_or()`（让默认值替你兜底）。把判空责任交给 UB，早晚要还的。
:::

## emplace、reset 与值语义的生命周期

optional 是值类型，这意味着它**自己管里面那个 `T` 的生命周期**：你构造一个非空 optional，`T` 就被构造；optional 析构，`T` 跟着析构；你重新赋值或清空，旧的 `T` 会被先析构。这套自动管理是 optional 比裸指针省心的核心。我们用一个带日志的类型把生命周期看得明明白白：

```cpp
// Standard: C++17
struct User {
    std::string name;
    int age;
    User(std::string n, int a) : name{std::move(n)}, age{a} {
        std::cout << "  User(" << name << ", " << age << ") 构造\n";
    }
    ~User() { std::cout << "  User(" << name << ") 析构\n"; }
    void greet() const { std::cout << "  hi, 我是 " << name << ", " << age << " 岁\n"; }
};

int main() {
    std::optional<User> opt;          // 空, 还没构造 User
    opt.emplace("alice", 30);         // 就地构造, 不产生临时对象
    opt->greet();                     // operator-> 访问成员

    opt.emplace("bob", 25);           // 再次 emplace: 先析构旧的, 再构造新的
    opt->greet();

    opt.reset();                      // 主动清空, 调用析构
    opt = std::nullopt;               // 赋值 nullopt, 等价于清空
}
```

```text
1. emplace 就地构造:
  User(alice, 30) 构造
  hi, 我是 alice, 30 岁
2. emplace 再次: 先析构旧的再构造新的
  User(alice) 析构
  User(bob, 25) 构造
  hi, 我是 bob, 25 岁
3. reset 主动清空:
  User(bob) 析构
4. 赋值 nullopt: 同样会析构当前值
```

几个细节值得注意。`emplace(args...)` 是「就地构造」——它直接在 optional 内部的存储上用 `args` 调 `T` 的构造函数，不会先生成一个临时 `T` 再移动/拷贝进去，对非平凡类型（比如这个 `User`）更高效，也比 `opt = User{...}` 表达得更清楚。`operator->` 让你能像用指针一样访问里面的成员（`opt->greet()`），但前提同样是「非空」——空 optional 上用 `operator->` 和解引用一样是 UB。`reset()` 和 `= nullopt` 是清空的两种等价写法，都会析构当前持有的值并把 optional 变成空。

这套「optional 管生命周期」的语义，和返回裸指针形成了鲜明对比。返回指针的函数，调用方拿到指针后，它的生命周期是**悬的**——指向容器内部、指向堆、指向静态区，行为完全不同，签名上看不出来；而返回 `optional<T>`（值），值就在 optional 这个对象里，optional 析构了值也就没了，边界清清楚楚，没有任何所有权歧义。

## C++23 的重头戏：monadic 操作

到这里为止都是 C++17 就有的东西。C++23 给 optional 加了三个 monadic 接口——`and_then`、`or_else`、`transform`——这是这一篇真正想讲的新东西，也是 optional 最值得期待的能力。

为什么需要它们？看一个真实场景：给定一个用户名，我们要「查用户 id → 查邮箱 → 取邮箱域名」。三步每一步都可能落空（用户不存在、用户没留邮箱、邮箱格式不对拿不到域名）。用 C++17 的 optional 写，是这个样子的：

```cpp
// Standard: C++17
std::string classic(const std::string& name) {
    auto uid = get_user_id(name);
    if (!uid) return "(no user)";          // 第一层判空
    auto email = get_email(*uid);
    if (!email) return "(no email)";       // 第二层判空
    auto dom = domain_of(*email);
    if (!dom) return "(no domain)";        // 第三层判空
    return *dom;
}
```

三层 `if` 嵌套，每一层都是「判空 + 取值」，逻辑被切得稀碎。这种「一连串可能失败的步骤」在业务代码里极其常见，传统写法就是层层 `if` 堆出来，又长又容易漏判。`and_then` 就是来消灭这些 `if` 的——它接收一个函数，**当 optional 非空时把值喂给这个函数，空时直接把空透传下去**。于是上面那段变成了一条链：

```cpp
// Standard: C++23
std::string monadic(const std::string& name) {
    return get_user_id(name)
        .and_then(get_email)          // optional<int>    -> optional<string>
        .and_then(domain_of)          // optional<string> -> optional<string>
        .value_or("(missing)");       // 链尾兜底
}
```

链上任意一步返回空，后面整条链就自动短路成空，最后 `value_or` 给个默认值。我们先确认它在 GCC 16.1.1 上跑得通，再对比传统写法的结果：

```text
name   classic         monadic
alice      'example.com'   'example.com'
bob      '(no email)'   '(missing)'
carol      '(no user)'   '(missing)'
```

`alice` 一路顺到底拿到域名 `example.com`；`bob` 在「查邮箱」那步落空（没留邮箱），传统写法返回 `(no email)`，monadic 写法短路到 `(missing)`；`carol` 第一步用户名就不存在，同样短路。两种写法语义一致，但 monadic 版的**控制流是线性的、从左往右读下来**，没有被 `if` 打断。

三个接口的区别要记牢，它们长得太像，用混了编译器会给你一堆 concepts 报错：

- **`and_then(f)`**——`f` 接收**值类型 `T`**，返回一个**新的 `optional<U>`**。它的语义是「可能把有变没」（`f` 自己决定返回空还是不空），适合串联「每步都可能失败的查询」。这是 monadic 链的主力。
- **`transform(f)`**——`f` 接收**值类型 `T`**，返回一个**普通值 `U`（不是 optional）**。它只做「有变有」的纯映射，**不会引入新的空**（只要 optional 本来非空，结果就非空）。适合「对值做一次变换，不涉及失败」的场景。
- **`or_else(f)`**——和前两个反着来：**optional 非空时不调用 `f`，原样返回；空时调用 `f()`**（注意 `f` **不接收参数**），`f` 必须返回一个**同类型的 `optional<T>`** 作为兜底。适合「空了就给个默认 / 记个日志」。

我们用 `transform` 和 `or_else` 各跑一个例子，把语义钉死。先看 `transform`：对一个可能存在的用户名做「大写化」映射，这个操作本身不会失败，所以用 `transform` 而不是 `and_then`：

```cpp
// Standard: C++23
std::string to_upper(std::string s) { /* 转大写 */ return s; }

std::optional<std::string> name{"alice"};
std::optional<std::string> empty;

auto big = name.transform(to_upper);         // 有值 -> 映射 -> 仍有值: ALICE
auto big_empty = empty.transform(to_upper);  // 空 -> 透传 -> 仍空
```

```text
name.transform(upper): ALICE
empty.transform(upper): (none)
```

注意 `empty.transform(to_upper)` 是空的——`transform` 对空 optional 啥都不做，直接把空往下传，不会调用 `to_upper`。再看 `or_else`：空的时候回退到一个默认值，顺带打个日志：

```cpp
// Standard: C++23
auto fallback = empty.or_else([] {
    std::cout << "  [or_else] 没值, 回退到 GUEST\n";
    return std::optional<std::string>{"GUEST"};
});
// empty 时: 打日志, 返回装着 "GUEST" 的 optional
// 非空时: 不调用, 原样返回
```

```text
  [or_else] 没值, 回退到 GUEST
empty.or_else(GUEST): GUEST
name.or_else(GUEST): alice  (or_else 没被调用)
```

`name` 是非空的，`or_else` 压根没被调用，直接把 `alice` 原样返回。这就是它「有就保留、没有就兜底」的语义。

::: warning 三个接口的签名区别,别用混
这三个接口最容易在**参数和返回值**上踩坑，混了就是一堆 concepts 报错：

- `and_then`、`transform` 的函数接收的是**值 `T`**（或引用），不是 `optional<T>`——别写成 `[](std::optional<int> o){...}`。
- `and_then`、`or_else` 的函数返回 **`optional`**；`transform` 的函数返回 **普通值**。
- `or_else` 的函数**不接收参数**（空的时候根本没有值可传），返回的 optional 必须和原 optional **同类型** `optional<T>`，不能换类型。

一句话记忆：`and_then`/`or_else` 摆弄的是 optional 本身（可能改变「有没有」），`transform` 只对里面的值做一次纯变换（不改变「有没有」）。
:::

这套 monadic 接口的价值，在「一连串可能失败的步骤」里最能体现。更重要的是，它和 C++23 的 `std::expected<T, E>` 是**同一套思路**——`expected` 是「optional + 错误信息」，它的 `and_then`/`or_else`/`transform` 签名几乎一模一样，区别只在于「空」被换成了「带错误原因的意外值」。学会 optional 的 monadic 链，expected 的链你也会了一半。这两者的对照我们在 expected 那篇里展开。

## 移动语义与 C++20 constexpr

optional 对移动语义的支持是完整的：把 optional 里的值「移出」、把一个 optional 移给另一个，都按你期望的方式工作。但有个细节得看清楚——**你 `std::move(*opt)` 把值搬走之后，optional 自己并不知道，它仍然 `has_value()` 为真**。我们用一个会打印移动日志的类型实测：

```cpp
// Standard: C++17
auto o = make_box();              // optional<Box>, 装着 tag="payload"
Box taken = std::move(*o);        // 把值搬出来
// o 仍然 has_value()=true, 但里面的 Box 已是 moved-from 状态
```

```text
--- 从 optional 移出值 ---
  Box(payload) ctor
  Box(payload) MOVE
  o has payload
  Box(payload) MOVE          <-- std::move(*o) 触发移动构造
  taken.tag = payload
  o still has_value=1 (optional 不知道值被搬空了, 仍 engaged)
  o->tag = (moved-from)  (moved-from 状态, 别用)
```

移动之后 `taken` 拿到了 `payload`，而 `o` 里的对象变成了 moved-from 状态（tag 显示 `(moved-from)`）。关键是 `o.has_value()` 仍是 `true`——optional 的「有没有」标记位没动，它不知道你把值掏空了。所以**移出之后别再用 `*o` 访问那个值**（moved-from 对象只保证能析构和重新赋值）。如果确实想让 optional 变空，显式 `o.reset()` 或 `o = std::nullopt`。

最后说一个 C++20 的能力：**optional 的绝大多数操作都是 `constexpr` 的**，包括构造、`emplace`、`reset`、`value_or`、`operator*`。这意味着 optional 可以在**编译期**求值，塞进 `static_assert` 里：

```cpp
// Standard: C++20
constexpr int compute() {
    std::optional<int> o;
    o.emplace(7);
    int v = *o;
    o.reset();
    return v + 35;          // 42
}

int main() {
    static_assert(compute() == 42);                // 编译期就定下来
    constexpr std::optional<int> empty;
    static_assert(empty.value_or(99) == 99);       // value_or 也 constexpr
}
```

```text
constexpr compute() = 42
empty.value_or(99) = 99
C++20 constexpr optional: OK
```

想跑一遍看 optional 编译期求值？点开下面这个在线示例：

<OnlineCompilerDemo
  title="C++20 constexpr optional：编译期求值"
  source-path="code/examples/vol3/61_optional_constexpr.cpp"
  description="optional 的 emplace/reset/value_or 都是 constexpr：compute() 和 empty.value_or(99) 能塞进 static_assert 编译期验证，运行也打印同样结果"
  allow-run
/>

这一点在模板元编程、编译期查表、`consteval` 函数里非常有用——你需要一个「可能没值」的盒子时，C++20 起编译期也能用 optional 了，不用再自己手搓 union。

## 一点性能直觉：optional 在热路径上有多少开销

担心 optional 性能的人不少。直觉上「多一个标记位、多一次分支」，好像会拖慢。我们在一个 5 亿次循环的热路径上，把 `optional<int>` + `value_or` 和直接返回 `int`（用 `-1` 当 sentinel）做个对比：

```cpp
// Standard: C++17
std::optional<int> lookup_opt(int i) { return i & 1 ? std::optional<int>{i} : std::nullopt; }
int                lookup_raw(int i) { return i & 1 ? i : -1; }

for (long i = 0; i < 500'000'000L; ++i) acc += lookup_opt(i).value_or(0);
```

实测（`g++ -std=c++23 -O2`，多次运行取代表值）：

```text
optional<int>.value_or(0): 132 ms
raw int:                   234 ms
```

```text
optional<int>.value_or(0): 192 ms
raw int:                   403 ms
```

绝对值在不同运行里波动不小（机器负载影响很大），但有一个稳健的结论：**`optional<int>.value_or` 在这条热路径上和直接返回 int 在同一个量级，甚至常常更快**，绝不出现「慢一个数量级」的情况。原因在于 `-O2` 下 optional 的小体积（就一个 `int` 加一个标记位）、`value_or` 的内联、以及现代 CPU 的分支预测，让这点开销被优化得几乎看不见。**结论是：不要为了性能回避 optional**——它带来的类型安全收益，远比那点测不出来的开销值钱。当然，如果你的值类型本身很大（比如一个 1KB 的结构体），optional 会多存一份标记位并对齐填充，拷贝开销也要考虑，这时候该不该传 optional 就得看具体场景了。

## 几个真实容易踩的点

把这一路容易翻车的位置集中收一下，每条都是上面实测验证过的：

::: warning operator* / operator-> 空时是 UB
`*empty` 和 `empty->member` 是**未定义行为**，不是抛异常。默认编译下大概率「看起来正常」（打印个 `0`），把你坑在最深的地方。ASan/UBSan 抓不住它，得靠 `-D_GLIBCXX_ASSERTIONS` 才能在运行时 abort。规矩：**只在你已经判过空的语境里用 `*` 和 `->`**，否则用 `value()`（抛异常）或 `value_or()`（兜底）。

**`value()` 还是 `operator*`？** 取舍就一条：这里的「空」是「正常可能发生、我得处理」，还是「不该发生、发生了就是 bug」？前者用 `value_or` 或判空后 `*`；后者用 `value()` 让异常替你把 bug 暴露出来。别用 `value()` 当正常控制流——异常的开销和语义都不适合。
:::

::: warning 移出之后 optional 仍 engaged
`std::move(*opt)` 把值搬走，但 optional 的「有没有」标记位没动，`has_value()` 仍是 `true`。这时候访问 `*opt` 拿到的是 moved-from 对象（合法但状态未指定），只能析构或重新赋值。要让 optional 真正变空，显式 `reset()` 或 `= nullopt`。
:::

::: warning optional 的判空不是免费的,但很便宜
optional 多一个标记位，访问值前总隐含一次「判空」。在你已经 `if (opt)` 判过、循环里反复用 `*opt` 的场景，可以把判空提到循环外，省掉重复判断。但别为了这点微优化牺牲可读性——绝大多数场景编译器自己能优化掉，先写对再说。
:::

::: warning or_else 的函数不接收参数,且返回类型必须一致
`or_else` 的函数是 `f()`（无参），不是 `f(value)`——空的时候根本没有值可传。而且它返回的必须是**同一个 `optional<T>`**，不能借机换个类型（想换类型用 `and_then`）。混了就是一串 concepts 报错。
:::

## 小结

`std::optional` 的核心价值，就是把「可能没有值」从注释和约定提升成了**类型系统的事实**。几条关键结论收一下：

- **替代三种土办法**：比 sentinel 值安全（不会和合法值冲突）、比裸指针清晰（值语义、无所有权歧义）、比 `pair<bool, T>` 干净（空时不构造 `T`、标记位和值绑死）。
- **四种拿值方式**：`has_value()`/`operator bool()`（查询）、`value()`（空抛 `bad_optional_access`）、`value_or(default)`（空兜底）、`operator*`/`operator->`（空是 UB，慎用）。
- **最大的坑是解引用空 optional**：UB，默认编译下多半不崩（打印个凑数的值），ASan 抓不住，靠 `-D_GLIBCXX_ASSERTIONS` 才能在运行时 abort。规矩是只在判过空的语境用 `*`/`->`。
- **值语义的生命周期**：optional 自管 `T` 的构造析构，`emplace` 就地构造、`reset()`/`= nullopt` 清空调析构；移出后 optional 仍 engaged，访问到的是 moved-from 对象。
- **C++23 monadic 是重头戏**：`and_then`（串联可能失败的步骤，函数返回 optional）、`transform`（对值做纯映射，函数返回普通值）、`or_else`（空时兜底，函数无参返回同类型 optional）。三者配合，把「层层 if 判空」压成一条线性链。和 `expected` 共享同一套思路。
- **性能不是问题**：热路径上 `optional<int>` 和直接返回 `int` 同量级，别为性能回避它；C++20 起 optional 还是 `constexpr` 的，编译期也能用。

下一篇我们看 `std::expected<T, E>`——它是「optional + 错误原因」，当你不光需要知道「失败」，还需要知道「为什么失败」时，就该它上场了。optional 的 monadic 链你熟练了，expected 的链你会上手很快。

## 参考资源

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional) —— 接口总览、`bad_optional_access`、C++20 constexpr 说明
- [cppreference: std::optional::and_then, or_else, transform](https://en.cppreference.com/w/cpp/utility/optional/and_then) —— C++23 monadic 接口的签名与语义
- [P0798R8 Monadic operations for std::optional](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p0798r8.html) —— C++23 引入 `and_then`/`or_else`/`transform` 的提案，含设计动机
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access) —— `value()` 空时抛出的异常类型
- libstdc++ 源码 `/usr/include/c++/16.1.1/optional` —— `operator*` 的 `__glibcxx_assert` 与 `_M_is_engaged` 检查（GCC 16.1.1）
