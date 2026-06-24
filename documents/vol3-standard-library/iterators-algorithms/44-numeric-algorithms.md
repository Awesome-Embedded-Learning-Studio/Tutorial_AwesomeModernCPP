---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 <numeric> 这一族算法——accumulate 累加为何会把 double 截断成 int、reduce 凭什么能并行又为何要求结合律、前缀和家族 partial_sum 与 scan 的区别，以及 C++17 数论 gcd/lcm、C++20 midpoint 如何救 (a+b)/2 的溢出、lerp 为何不在 <numeric> 里
difficulty: intermediate
order: 44
platform: host
prerequisites:
- 迭代器基础与 category
- vector 深入：三指针、扩容与迭代器失效
reading_time_minutes: 14
related:
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
tags:
- host
- cpp-modern
- intermediate
- 容器
title: 'numeric：累加、填充、内积与相邻差'
---

# `<numeric>`：累加、填充、内积与相邻差

前面几篇我们把容器和迭代器都过了一遍，算法那一头也提了不少。但标准库的算法其实分在两个头文件里：大家熟的 `<algorithm>` 装的是 `find` / `sort` / `copy` 这些「对元素本身做手脚」的家伙；还有一个低调得多的 `<numeric>`，专门管「把一堆数算成一个数」或者「把一堆数变成另一堆数」——累加、内积、前缀和、填充序号，都在这里。

这一篇我们就把 `<numeric>` 这一族拆开讲。它看着都是些「for 循环就能写」的简单活，但每个都藏着至少一个值得讲透的设计决定：`accumulate` 的返回类型为什么会偷偷截断 `double`、`reduce` 凭什么敢并行、C++17 那一堆 `scan` 是怎么把前缀和家族拆细的、还有 C++20 的 `midpoint` 为什么能救 `(a+b)/2` 的溢出。把这些点串起来，你才算真正会用这一族算法，而不是每次都手写循环。

## `accumulate`：累加，以及它最坑的返回类型

最基础的一个。`std::accumulate(first, last, init)` 就是「从 `init` 开始，把区间里每个元素依次累加进去」，默认运算是 `+`。给个 `vector` 求和：

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    std::vector<double> v{1.5, 2.5, 3.5, 4.5};   // 数学和 = 12.0
    std::cout << "accumulate(v, 0):    " << std::accumulate(v.begin(), v.end(), 0) << '\n';
    std::cout << "accumulate(v, 0.0):  " << std::accumulate(v.begin(), v.end(), 0.0) << '\n';
    return 0;
}
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
accumulate(v, 0):    10
accumulate(v, 0.0):  12
```

想跑一遍看截断？点开下面这个在线示例：

<OnlineCompilerDemo
  title="accumulate 的截断坑：初始值类型决定返回类型"
  source-path="code/examples/vol3/44_accumulate_truncation.cpp"
  description="同一个 double 向量（数学和 12.0）：accumulate(v, 0) 返回 10（int 截断），accumulate(v, 0.0) 返回 12——返回类型由初始值决定"
  allow-run
/>

区别只在初始值一个是 `0`（`int`），一个是 `0.0`（`double`），结果一个 `10` 一个 `12`。这正是 `accumulate` 最坑的地方，也是它后面被 C++23 的 `std::fold`（下一篇专门讲）专门修掉的缺陷。

### 为什么会截断：返回类型 = 初始值类型

去看 `accumulate` 的签名就明白了：

```cpp
T accumulate(InputIt first, InputIt last, T init);
```

返回类型 `T` 不是「区间里元素的类型」，而是「初始值 `init` 的类型」。内部的累加大致是 `acc = acc + *it`，而 `acc` 一开始就是 `init`，类型固定。所以传 `0` 进去，整个累加过程都在 `int` 里做——每个 `double` 元素先被隐式转成 `int`（截断小数），再加起来。`1.5 + 2.5 + 3.5 + 4.5` 截成 `1 + 2 + 3 + 4 = 10`，小数部分被默默丢掉了，编译器一句警告都不给。

把初始值改成 `0.0`，`T` 就是 `double`，累加全程在 `double` 里做，结果才正确。所以用 `accumulate` 求浮点序列的和，**初始值必须带小数点**——这是个一行代码就能埋下、却很难看出来对错的坑。整数序列就没这问题，但一旦元素类型比初始值类型「宽」（比如 `long long` 元素配 `int` 初始值），照样截断。

::: warning accumulate 的返回类型 = 初始值类型，不是元素类型
求浮点和务必传 `0.0`，求大整数和务必传 `0LL`。传错类型不会报错，只会给你一个「看起来差不多」的错误结果。C++23 的 `std::fold_left` 改成了「用元素类型推断累加类型」，从根上消除了这个坑——我们在下一篇展开。
:::

`accumulate` 的第四个参数可以换累加运算。传 `std::multiplies<>{}` 进去，累加就变累乘；传个 lambda，什么自定义「合并」都能做。注意这里的运算要满足「左结合」的语义（`acc = op(acc, *it)`，初始值在最左），这对顺序敏感的运算（比如浮点加法）会有影响——这一点我们讲 `reduce` 的时候还会再撞上。

## `iota`：用递增值填充

一个看着不起眼、但写起来特别省事的工具。`std::iota(first, last, value)` 从 `value` 开始，依次填入 `value, value+1, value+2, ...`。最经典的用法是生成一组序号：

```cpp
std::vector<int> ids(6);
std::iota(ids.begin(), ids.end(), 0);   // 0 1 2 3 4 5
```

名字 `iota` 来自编程语言 APL 里的那个生成序号的运算符（希腊字母 ι），不是「I-O-T-A」缩写，知道这个就不容易记混。它常用来给一组元素「编上号」，比如做索引 shuffle、给候选集打标：

```cpp
// Standard: C++20
#include <array>
#include <iostream>
#include <numeric>
#include <vector>

template <class T>
void print(const char* label, const T& v)
{
    std::cout << label;
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> ids(6);
    std::iota(ids.begin(), ids.end(), 0);     // 0 1 2 3 4 5
    print("iota(0): ", ids);

    std::vector<int> ids5(6);
    std::iota(ids5.begin(), ids5.end(), 100); // 100 101 102 ...
    print("iota(100): ", ids5);
    return 0;
}
```

跑出来：

```text
iota(0): 0 1 2 3 4 5
iota(100): 100 101 102 103 104 105
```

`iota` 干的活其实等价于「`for (i, v) { *i = val++; }`」，但名字一旦认得，读代码时一眼就懂意图——「这段是在生成序号」，比一段裸循环清晰得多。

## `inner_product`：两序列的内积

`std::inner_product(first1, last1, first2, init)` 算的是两个序列的内积：把对应位置的元素两两相乘，再累加到 `init` 上。数学上就是 `init + Σ a[i] * b[i]`：

```cpp
std::vector<int> a{1, 2, 3, 4};
std::vector<int> b{2, 3, 4, 5};
std::cout << std::inner_product(a.begin(), a.end(), b.begin(), 0);
// = 1*2 + 2*3 + 3*4 + 4*5 = 40
```

它和 `accumulate` 一样有返回类型 = 初始值类型的坑（`init` 决定类型），也一样接受两个额外的可调用参数来自定义「乘」和「加」两个运算：`inner_product(first1, last1, first2, init, op1, op2)` 内部做的是 `acc = op1(acc, op2(*it1, *it2))`。

这个双自定义形式不常用，但偶尔能写出很精炼的表达。比如想判断「两个布尔序列是不是在对应位置上同时为真」——把 `op1` 和 `op2` 都设成逻辑与，初始值设 `1`（真），只要有一个位置不同时为真，结果就归零：

```cpp
std::vector<int> flags1{1, 1, 0, 1};
std::vector<int> flags2{1, 0, 1, 1};
auto all_both = std::inner_product(flags1.begin(), flags1.end(), flags2.begin(), 1,
    [](int x, int y){ return x && y; },   // op1: 累计「与」
    [](int x, int y){ return x && y; });  // op2: 逐位「与」
// 结果 0（第二位 1&&0=0，累计归零）
```

需要提醒一句：`inner_product` 在 C++17 之后已经不再被推荐用于新代码——C++17 给了更通用的 `std::transform_reduce`，既能多线程并行，又能配执行策略。不过 `inner_product` 在简单单线程场景下读起来还是最直白的，老代码里也到处都是，认识它仍然必要。

## 前缀和家族：`partial_sum` / `adjacent_difference` / `inclusive_scan` / `exclusive_scan`

`<numeric>` 里有一族专门处理「把一个序列变成另一个序列」的算法，核心是前缀和。老接口（C++11）有两个，新接口（C++17）又拆出两个 `scan`，把前缀和的两种语义分得清清楚楚。我们一次跑完看差异：

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

template <class T>
void print(const char* label, const T& v)
{
    std::cout << label;
    for (auto x : v) std::cout << x << ' ';
    std::cout << '\n';
}

int main()
{
    std::vector<int> v{1, 2, 3, 4, 5};
    std::vector<int> out(v.size());

    std::partial_sum(v.begin(), v.end(), out.begin());
    print("partial_sum      : ", out);   // 含当前: 1 3 6 10 15

    std::adjacent_difference(v.begin(), v.end(), out.begin());
    print("adjacent_diff    : ", out);   // 1 (2-1) (3-2) (4-3) (5-4) = 1 1 1 1 1

    std::inclusive_scan(v.begin(), v.end(), out.begin(), std::plus<>{}, 0);
    print("inclusive_scan(0): ", out);   // 含当前, 同 partial_sum

    std::inclusive_scan(v.begin(), v.end(), out.begin());
    print("inclusive_scan   : ", out);

    std::exclusive_scan(v.begin(), v.end(), out.begin(), 0);
    print("exclusive_scan(0): ", out);   // 不含当前: 0 1 3 6 10
    return 0;
}
```

跑出来：

```text
partial_sum      : 1 3 6 10 15
adjacent_diff    : 1 1 1 1 1
exclusive_scan(0): 0 1 3 6 10
inclusive_scan(0): 1 3 6 10 15
inclusive_scan   : 1 3 6 10 15
```

`partial_sum` 就是教科书上的前缀和——位置 `i` 的输出是 `v[0] + v[1] + ... + v[i]`，**含当前位置**。`adjacent_difference` 是它的逆运算：位置 `i` 的输出是 `v[i] - v[i-1]`（首元素原样保留），所以上面 `1 2 3 4 5` 算出来全 `1`（每个数都比前一个多 1）。两者是一对，先 `partial_sum` 再 `adjacent_difference` 就能还原原序列。

C++17 的 `inclusive_scan` / `exclusive_scan` 把前缀和的两种语义做了明确区分：

- `inclusive_scan` —— **含**当前位置，和 `partial_sum` 语义一致；
- `exclusive_scan` —— **不含**当前位置，位置 `i` 的输出是 `v[0] + ... + v[i-1]`，首元素位置是给定的初始值。

上面 `exclusive_scan(0)` 算出来 `0 1 3 6 10`——第一个位置直接给了初始值 `0`，后面每个位置才是「前面所有元素之和」。这种「不含当前」的前缀和在做扫描线、流水线这类算法时特别常用，以前得自己手写循环或者偏移一位，现在一个 `exclusive_scan` 搞定。

`scan` 系列相比老 `partial_sum` 的真正价值，一是语义分得清（含/不含），二是**像 `reduce` 一样可以并行**——它们都支持传入执行策略（`std::execution::par`），前缀和这种以前必须严格串行的计算，现在标准库给了并行实现。这一点我们马上在 `reduce` 那节展开。

## `reduce`：`accumulate` 的可并行版

`std::reduce`（C++17）看上去和 `accumulate` 干一样的事——把区间累加起来：

```cpp
std::vector<int> v{1, 2, 3, 4, 5};
std::cout << std::reduce(v.begin(), v.end(), 0);   // 15
```

但它和 `accumulate` 有两个本质区别。

**第一，可以并行。** `reduce` 支持传执行策略，标准库可以把它切成多段、分到多个线程上各算各的，最后合并。而 `accumulate` 是严格从左到右串行的——它必须保证「先算左边，再算右边」的顺序，没法并行。这就是为什么 C++17 要新增 `reduce`：单线程的 `accumulate` 在大数据量上吃不满多核。

**第二，运算必须满足结合律。** 这是「可以并行」的直接代价。`accumulate` 的合并是 `acc = acc + *it`，固定从左到右，所以哪怕运算本身顺序敏感（比如浮点加法），它也算得「按定义一致」。`reduce` 要切分再合并，合并时元素的结合顺序是任意的——`(a+b)+c+d` 可能变成 `a+(b+c)+d`，甚至更怪的分块。只有运算满足结合律（`op(a, op(b,c)) == op(op(a,b), c)`），任意结合顺序才得到同一个结果。

浮点加法偏偏不满足结合律。我们拿一个对顺序敏感的浮点序列，让 `accumulate` 和 `reduce` 各算一遍，看实际差多少：

```cpp
// Standard: C++20
#include <iostream>
#include <numeric>
#include <vector>

int main()
{
    // 一百万个 0.1f 加一个 1e7f: 浮点累加顺序不同, 结果会有出入
    std::vector<float> f;
    for (int i = 0; i < 1000000; ++i) f.push_back(0.1f);
    f.push_back(1e7f);

    float acc = 0.0f;
    for (auto x : f) acc += x;                              // 顺序累加
    auto red = std::reduce(f.begin(), f.end(), 0.0f);       // 允许任意结合顺序

    std::cout.precision(15);
    std::cout << "accumulate float : " << acc << '\n';
    std::cout << "reduce float     : " << red << '\n';
    std::cout << "数学期望(约)    : " << (1000000 * 0.1 + 1e7) << '\n';
    return 0;
}
```

跑出来：

```text
accumulate float : 10100958
reduce float     : 10099760
数学期望(约)    : 1.01e+07
```

两个都没精确等于期望的 `10100000`——这本来就是浮点累加的固有问题。但关键是它们俩**不相等**：`10100958` vs `10099760`，差了上千。`accumulate` 是严格左结合的结果，`reduce` 则按 GCC 实现的分块顺序合并。两边都没「错」，只是浮点加法的非结合性让结果取决于顺序。

这就是 `reduce` 对运算的隐含要求：**整数加法、乘法、按位与/或/异或、逻辑与/或、`max`/`min` 这些满足结合律的运算，随便并行结果一致；浮点加法这种顺序敏感的，并行后结果可能漂移**。用 `reduce` 算浮点和大体上能接受（误差在浮点精度范围内），但如果你依赖「精确复现某个值」，就得回到串行的 `accumulate`。

::: warning reduce / scan 要求运算满足结合律
只要开了并行执行策略（或者用了默认的 `reduce`），就别指望它保持左结合顺序。整数和无符号运算没问题；浮点运算结果会随结合顺序漂移。需要严格顺序就用 `accumulate`。
:::

顺带提一句还没展开的事：`reduce` 目前**不在 C++20 `std::ranges` 命名空间里**——`ranges::reduce` 并不存在。原因是并行算法（带执行策略的那一族）在 ranges 化的设计上还有没敲定的点，标准委员会没在 C++20 里把它一起推出来。这一点我们下一篇讲 `fold` 家族时会接着说，因为 `fold` 正是 ranges 化的「可串行折叠」，它在某种程度上是 `reduce` 的 ranges 对应物。

## C++17 数论：`gcd` 与 `lcm`

从这一节起是 `<numeric>` 里一些「小而实用」的数论和几何工具。先是 C++17 加进来的最大公约数和最小公倍数：

```cpp
std::gcd(54, 24)   // 6
std::lcm(4, 6)     // 12
std::gcd(17, 13)   // 1（互素）
```

`gcd` / `lcm` 是模板函数，对整数类型都适用，内部用的是高效的辗转相除。不用再自己手写欧几里得算法或者去翻 Boost。有几个边界值值得记一下（都已实测）：

- `gcd(0, 0) = 0`、`gcd(0, 12) = 12`——`gcd(0, n)` 就是 `|n|`；
- `lcm(0, x) = 0`——只要有一个是 0，最小公倍数就是 0（任何数都是 0 的「倍数」，但约定取 0）。

::: warning lcm(0, x) = 0，不是抛异常
数学上 `lcm(0, x)` 有点歧义，标准库取 `0`。如果你在写涉及分数化简、周期对齐的代码，别假设 `lcm` 一定会返回正数，传 0 进去会拿到 0。
:::

## C++20：`midpoint` 救了 `(a+b)/2`，`lerp` 做线性插值

C++20 给了两个看起来平淡、但专门修真实 bug 的工具。

### `midpoint`：安全求中点

二分查找、区间对半这些场景里，求两个数的中点是高频操作。直觉写法是 `(a + b) / 2`——但这行在 `a`、`b` 都接近类型上限时会**溢出**。比如两个 `int64` 都是 9e18 量级，加起来超过 `int64` 最大值（约 9.22e18），有符号整数溢出是未定义行为，结果可能是个负数。我们直接实测这个坑：

```cpp
// Standard: C++20
#include <cstdint>
#include <iostream>
#include <numeric>

int main()
{
    std::int64_t big1 = 7'000'000'000'000'000'000LL;   // 7e18
    std::int64_t big2 = 9'000'000'000'000'000'000LL;   // 9e18
    auto naive = (big1 + big2) / 2;                      // 溢出!
    auto safe  = std::midpoint(big1, big2);
    std::cout << "naive (big1+big2)/2 = " << naive << " (溢出!)\n";
    std::cout << "midpoint(big1,big2) = " << safe << " (正确)\n";
    return 0;
}
```

跑出来：

```text
naive (big1+big2)/2 = -1223372036854775808 (溢出!)
midpoint(big1,big2) = 8000000000000000000 (正确)
```

`(big1+big2)/2` 给了个 `-1223372036854775808`——一个荒唐的负数，正是溢出翻车后的典型表现。正确的中点应该是 `8000000000000000000`（8e18），`std::midpoint` 算对了。

`midpoint` 内部用的是不溢出的等价算法（大致是 `a + (b - a) / 2`，并处理好符号和奇偶），全程不经过 `a + b` 这一步，所以不会溢出。这是 C++20 把一个「人人都会写错」的小运算扶正成标准设施的典型案例——别再自己写 `(a+b)/2` 了，写二分、分治、区间对半的时候，直接 `std::midpoint`。

`midpoint` 还有个重载能对两个**指针**求中点：

```cpp
int arr[]{10, 20, 30, 40, 50};
auto mid = std::midpoint(arr, arr + 4);
std::cout << "midpoint(arr, arr+4) -> arr[" << (mid - arr) << "] = " << *mid << '\n';
// 输出: midpoint(arr, arr+4) -> arr[2] = 30
```

指针版会正确处理奇偶长度（长度为 4 时取偏移 2，长度为 5 时按「向下取整」取偏移 2），在实现二分、分块时比手写 `(lo + hi) / 2` 更稳。指针中点还有个额外好处：它避免了 `lo + hi` 这种「两个指针相加」的非法操作（C++ 里指针只能相减、不能相加），所以光从语法上 `midpoint` 就比手写循环干净。

### `lerp`：线性插值（注意它不在 `<numeric>` 里）

`std::lerp(a, b, t)` 算线性插值 `a + t * (b - a)`，`t=0` 返回 `a`，`t=1` 返回 `b`，`t=0.5` 是中点。动画、渐变、游戏里的插值全用它：

```cpp
std::lerp(0.0, 100.0, 0.25)   // 25
std::lerp(0.0, 100.0, 1.0)    // 100
std::lerp(0.0, 100.0, 2.0)    // 200（可外插，t 不限 [0,1]）
```

看起来平平无奇，但它有几个手写 `a + t*(b-a)` 拿不到的保证：`t=0` 精确返回 `a`、`t=1` 精确返回 `b`（手写版因为浮点误差可能给出 `99.9999...`）、对无穷和 NaN 有定义良好的行为。这些在数值和图形代码里是真有意义的保证。

::: warning lerp 在 `<cmath>`，不在 `<numeric>`
这是个容易翻车的头文件坑：`gcd` / `lcm` / `midpoint` 都在 `<numeric>`，但 `std::lerp` 偏偏在 **`<cmath>`** 里。只 `#include <numeric>` 然后用 `std::lerp` 会直接编不过，报 `'lerp' is not a member of 'std'`。我们实测过——必须额外 `#include <cmath>`。
:::

## 几个真实容易踩的点

把这一族用的时候容易翻车的位置集中收一下，每个都是上面实测验证过的：

::: warning accumulate / inner_product 的初始值决定返回类型
求浮点和务必传 `0.0`，求大整数和务必传 `0LL`。传 `0`（`int`）累加 `double` 序列，每个元素先被截断成 `int`，小数部分默默丢掉，编译器不警告。这是 `accumulate` 最经典也最隐蔽的坑。

::: warning reduce / scan 并行时运算必须满足结合律
开了执行策略（或者依赖 `reduce` 的可结合语义）后，结合顺序任意。整数和位运算没问题，浮点加法结果会随顺序漂移（实测 `10100958` vs `10099760`）。需要严格左结合就用 `accumulate` / `partial_sum`。

::: warning (a+b)/2 在大整数下溢出，用 midpoint
二分、区间对半里求中点，`a + b` 会溢出。实测 `(7e18 + 9e18) / 2` 给出 `-1223372036854775808`。C++20 起一律用 `std::midpoint`，整数和指针都能用。

::: warning std::lerp 在 `<cmath>` 不在 `<numeric>`
`gcd` / `lcm` / `midpoint` 在 `<numeric>`，但 `lerp` 在 `<cmath>`。只 include `<numeric>` 用 `lerp` 会编不过，记得补 `<cmath>`。

## 小结

`<numeric>` 这一族看着都是「for 循环能写」的小工具，但每个都至少藏着一个值得知道的设计决定。几条关键结论收一下：

- `accumulate` 求和时返回类型 = 初始值类型，浮点序列必须传 `0.0`，否则截断成整数（这个坑 C++23 的 `fold` 才修掉，下一篇讲）。
- `iota` 用递增值填充，生成序号序列的标准写法；`inner_product` 算两序列内积，单线程老接口，新代码可考虑 `transform_reduce`。
- 前缀和家族：`partial_sum`（含当前）、`adjacent_difference`（差分，`partial_sum` 的逆）；C++17 的 `inclusive_scan`（含当前）/ `exclusive_scan`（不含当前）把语义拆清，还能并行。
- `reduce` 是 `accumulate` 的可并行版，代价是要求运算满足结合律；浮点加法不满足，并行结果会漂移。它目前还不 ranges 化，下一篇讲 `fold` 时展开原因。
- C++17 数论 `gcd` / `lcm`（注意 `lcm(0, x) = 0`）；C++20 `midpoint` 救了 `(a+b)/2` 的溢出，还能对指针用；`lerp` 做线性插值，但在 `<cmath>` 不在 `<numeric>`。

下一篇我们正式进 C++23 的 `fold` 家族——看它是怎么从根上修掉 `accumulate` 那个返回类型缺陷的，以及它和 `reduce`、ranges 折叠到底是什么关系。

## 参考资源

- [cppreference: `<numeric>`](https://en.cppreference.com/w/cpp/numeric) —— 整族算法总览
- [cppreference: std::accumulate](https://en.cppreference.com/w/cpp/algorithm/accumulate) —— 返回类型 = 初始值类型（`T init`）的正式说明
- [cppreference: std::reduce (C++17)](https://en.cppreference.com/w/cpp/algorithm/reduce) —— 并行语义与「运算需满足结合律」的要求
- [cppreference: std::exclusive_scan / inclusive_scan (C++17)](https://en.cppreference.com/w/cpp/algorithm/exclusive_scan) —— 含/不含当前位置的两种前缀和语义
- [cppreference: std::midpoint (C++20)](https://en.cppreference.com/w/cpp/numeric/midpoint) —— 不溢出的中点，整数与指针重载
- [cppreference: std::lerp (C++20)](https://en.cppreference.com/w/cpp/numeric/lerp) —— 线性插值，注意它定义在 `<cmath>`
- [cppreference: std::gcd / std::lcm (C++17)](https://en.cppreference.com/w/cpp/numeric/gcd) —— 数论工具及 `lcm(0,x)=0` 的约定
