---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 <cmath>——浮点分类 fpclassify/isnan/isinf/isnormal 与 NaN「不等于自己」的坑、为什么别用 == 判浮点、abs(INT_MIN) 的未定义行为、hypot 怎么救溢出、fma 单次舍入凭什么保精度、C++17 特殊数学函数与 C++20 std::numbers 编译期常量
difficulty: intermediate
order: 59
platform: host
prerequisites:
- '<numeric>：累加、填充、内积与相邻差'
- '迭代器适配器：反向、插入与流，把现成迭代器改出新行为'
reading_time_minutes: 16
related:
- '<numeric>：累加、填充、内积与相邻差'
tags:
- host
- cpp-modern
- intermediate
- 基础
title: '<cmath>：数学函数、浮点分类与精度陷阱'
---

# `<cmath>`：数学函数、浮点分类与精度陷阱

写到这一篇，我们已经把容器、迭代器、算法甚至 `<numeric>` 都过了一遍。但有一类工具一直没正经讲——数学运算。`sqrt` / `pow` / `sin` / `cos` 这些函数我们用了很多年，看着像「送分题」，可一旦它们撞上浮点数的真实表示，事情就完全不是「调个函数拿结果」那么简单了。

这一篇我们把 `<cmath>` 拆开讲，但不是把函数表抄一遍——cppreference 干这事比谁都全。我们要讲的是三件真正会让你翻车的事：浮点数到底有哪几种「异常态」（`NaN` / `inf` / 次正规数），它们怎么互相比较；为什么 `==` 在浮点世界里几乎是个陷阱，又该怎么替代；以及标准库给了哪些「看起来差不多、行为却完全不同」的函数（`abs` 的整数陷阱、`hypot` 救溢出、`fma` 保精度的单次舍入）。顺带把 C++17 的特殊数学函数和 C++20 的编译期数学常量 `std::numbers` 点到。`lerp` / `midpoint` 那一对我们在 `<numeric>` 篇里已经讲透了（`lerp` 其实在 `<cmath>` 里，这里不重复），这一篇聚焦浮点分类、常用函数和精度。

## 浮点分类：先搞清楚你的数「是哪一种」

`<cmath>` 里所有数学函数都建立在 IEEE 754 浮点表示之上。`double` 不是「能存任意实数的盒子」，它是 64 位、按符号位 + 指数 + 尾数编码的有限集合。这一节先把浮点数能处的几种状态理清楚，因为后面所有的坑——`NaN` 不等于自己、`==` 失灵、次正规数精度变差——都是这些状态直接派生的。

`<cmath>` 给了一组分类函数，配合 `fpclassify` 这个「总入口」用。先看一段把它们全跑一遍的代码：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double nan = std::numeric_limits<double>::quiet_NaN();
    double inf = std::numeric_limits<double>::infinity();

    std::cout << std::boolalpha;
    std::cout << "isnan(NaN)?        " << std::isnan(nan) << '\n';
    std::cout << "isinf(inf)?        " << std::isinf(inf) << '\n';
    std::cout << "isfinite(inf)?     " << std::isfinite(inf) << '\n';
    std::cout << "isfinite(3.14)?    " << std::isfinite(3.14) << '\n';
    std::cout << "isnormal(3.14)?    " << std::isnormal(3.14) << '\n';

    // 几种来源各异的异常态
    std::cout << "sqrt(-1) is NaN:   " << std::isnan(std::sqrt(-1.0)) << '\n';
    std::cout << "0.0/0.0 is NaN:    " << std::isnan(0.0 / 0.0) << '\n';
    std::cout << "1.0/0.0 is inf:    " << std::isinf(1.0 / 0.0) << '\n';

    std::cout << "\n--- fpclassify 逐类 ---\n";
    double values[] = {3.14, inf, -inf, nan, 0.0, -0.0};
    const char* names[] = {"3.14", "+inf", "-inf", "NaN", "0.0", "-0.0"};
    for (int i = 0; i < 6; ++i) {
        int c = std::fpclassify(values[i]);
        const char* cls = "unknown";
        switch (c) {
            case FP_INFINITE:  cls = "FP_INFINITE";  break;
            case FP_NAN:       cls = "FP_NAN";       break;
            case FP_NORMAL:    cls = "FP_NORMAL";    break;
            case FP_SUBNORMAL: cls = "FP_SUBNORMAL"; break;
            case FP_ZERO:      cls = "FP_ZERO";      break;
        }
        std::cout << names[i] << " -> " << cls
                  << "  signbit=" << std::signbit(values[i]) << '\n';
    }
    return 0;
}
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
isnan(NaN)?        true
isinf(inf)?        true
isfinite(inf)?     false
isfinite(3.14)?    true
isnormal(3.14)?    true
sqrt(-1) is NaN:   true
0.0/0.0 is NaN:    true
1.0/0.0 is inf:    true

--- fpclassify 逐类 ---
3.14 -> FP_NORMAL  signbit=0
+inf -> FP_INFINITE  signbit=0
-inf -> FP_INFINITE  signbit=1
NaN -> FP_NAN  signbit=0
0.0 -> FP_ZERO  signbit=0
-0.0 -> FP_ZERO  signbit=1
```

这几类合起来就是 IEEE 754 `double` 的全部「分类」：正常数（`FP_NORMAL`）、零（`FP_ZERO`，注意有正零和负零）、无穷（`FP_INFINITE`，也有正负）、`NaN`（`FP_NAN`，非数），以及马上要单独讲的次正规数（`FP_SUBNORMAL`）。`fpclassify` 是总入口，`isnan` / `isinf` / `isfinite` / `isnormal` 都是它的快捷谓词——判一类的时候用快捷函数更直观，要 switch 全部情况就上 `fpclassify`。

### `NaN` 不等于自己：这才是浮点世界最经典的坑

上面对 `NaN` 用了 `std::isnan` 判，而不是 `== nan`。这不是风格偏好，是硬性要求——因为 **`NaN` 与任何值（包括它自己）比较都返回 `false`**。实测：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double nan = std::numeric_limits<double>::quiet_NaN();
    std::cout << std::boolalpha;
    std::cout << "NaN == NaN?   " << (nan == nan) << '\n';
    std::cout << "NaN != NaN?   " << (nan != nan) << '\n';
    return 0;
}
```

```text
NaN == NaN?   false
NaN != NaN?   true
```

`NaN == NaN` 是 `false`，`NaN != NaN` 反而是 `true`。这是 IEEE 754 在标准层面定死的语义：`NaN` 代表「没有意义的运算结果」（`0/0`、`sqrt(-1)`、`inf - inf`），「相等」对它没有定义，所以一律判不等。这就直接带来了一个写法陷阱——**想判 `NaN` 永远只能用 `std::isnan`，绝不能用 `==`**：

```cpp
double x = compute_something();
if (x == std::numeric_limits<double>::quiet_NaN()) {   // 永远进不来！
    handle_error();
}
if (std::isnan(x)) {                                    // 这才对
    handle_error();
}
```

更阴险的是 `NaN` 会「污染」后续计算——`NaN` 参与任何算术，结果几乎都是 `NaN`。所以一个 `NaN` 如果没在源头用 `isnan` 拦住，会一路顺着表达式传播下去，最后你拿到一个莫名其妙的输出，回查半天才发现是上游某次 `sqrt(-1)`。

::: warning 判 NaN/inf 永远用 isnan/isinf，别用 ==
`== NaN` 永远是 `false`，写出来就是 bug。判 `NaN` 用 `std::isnan`，判无穷用 `std::isinf`，判「既不 NaN 也不 inf」用 `std::isfinite`。这三个谓词是 `<cmath>` 专门为绕开「NaN 不等自己」而准备的，没有理由手写比较。
:::

### 次正规数：`isnormal` 为什么会返回 `false`

看上面输出，`3.14` 是 `FP_NORMAL`，`0.0` 是 `FP_ZERO`。那 `FP_SUBNORMAL`（次正规数，也叫非规格化数 denormal）是什么？它是 IEEE 754 为了支持「比最小正常数还小的正数」设计的一档渐近精度区——代价是这个区间的精度会变差（尾数有效位数变少）。

直接用 `std::numeric_limits<double>` 拿到边界值看：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>
#include <limits>

int main()
{
    double smallest_normal = std::numeric_limits<double>::min();      // 最小正常数
    double smallest_denorm = std::numeric_limits<double>::denorm_min(); // 最小次正规数
    double half = smallest_normal / 2.0;
    std::cout << "smallest normal      = " << smallest_normal << '\n';
    std::cout << "smallest denorm      = " << smallest_denorm << '\n';
    std::cout << "min/2 (subnormal)    = " << half << '\n';
    std::cout << "isnormal(min)?       " << std::isnormal(smallest_normal) << '\n';
    std::cout << "isnormal(min/2)?     " << std::isnormal(half) << '\n';
    std::cout << "isnormal(denorm_min)? " << std::isnormal(smallest_denorm) << '\n';
    std::cout << "fpclassify(min/2)==FP_SUBNORMAL? "
              << (std::fpclassify(half) == FP_SUBNORMAL) << '\n';
    return 0;
}
```

```text
smallest normal      = 2.22507e-308
smallest denorm      = 4.94066e-324
min/2 (subnormal)    = 1.11254e-308
isnormal(min)?       true
isnormal(min/2)?     false
isnormal(denorm_min)? false
fpclassify(min/2)==FP_SUBNORMAL? true
```

最小正常数是 `2.22507e-308`，把它除以 2 得到的 `1.11254e-308` 还能表示，但已经不是正常数了——`isnormal` 返回 `false`，`fpclassify` 判为 `FP_SUBNORMAL`。这就是次正规数：数值能存，但精度降级了。

为什么这值得单独拎出来讲？因为有些代码假设「`x != 0` 就是个能正常参与计算的数」，可一旦 `x` 落在次正规区间，精度会塌，累加甚至可能变成原地踏步；某些平台/编译器还会开 FTZ（flush-to-zero）直接把次正规数当零处理以保性能，导致同一份代码在不同机器上结果不一致。`std::isnormal(x)` 就是用来判「这数是不是精度有保证的正常数」的——`0.0`、次正规数、`inf`、`NaN` 全都返回 `false`，只有 `FP_NORMAL` 返回 `true`。做数值敏感的判断（比如「分母是否太小、要钳到阈值」）时，`isnormal` 比单纯 `x == 0` 靠谱得多。

### `signbit`：负零和负无穷的符号

最后那个 `signbit` 看着多余——判断正负直接 `x < 0` 不就行？大部分时候可以，但有三个值 `x < 0` 处理不了：正零和负零用 `==` 相等（`0.0 == -0.0` 是 `true`），`NaN` 跟任何东西比都是 `false`，包括「负 `NaN`」。`signbit` 直接读符号位，能区分这些「比较语义拿不到」的情况：

```text
0.0 == -0.0?      true
signbit(0.0)?     false
signbit(-0.0)?    true
```

实际上 `1.0 / 0.0` 得 `+inf`，`1.0 / -0.0` 得 `-inf`——零的符号会在除法里显灵，这也是为什么标准库要专门给一个 `signbit`。

## 常用函数与它们的坑：`abs` / `hypot` / `fmod`

分类讲完，来看日常用得最多的几个运算。这部分不长篇大论，只挑「长得像、行为却差很多」的几个点讲透。

### `std::abs` 的整数陷阱

`abs` 是最朴素的需求——取绝对值。但它在整数上藏着一个未定义行为的坑。看实测：

```cpp
// Standard: C++20
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    int imin = std::numeric_limits<int>::min();   // 通常是 -2147483648
    auto r = std::abs(imin);
    std::cout << "INT_MIN              = " << imin << '\n';
    std::cout << "std::abs(INT_MIN)    = " << r << '\n';
    std::cout << "INT_MIN == abs?      = " << (imin == r ? "yes (overflowed)" : "no") << '\n';

    // 浮点重载：abs 和 fabs 等价
    double dn = -3.14;
    std::cout << "\nstd::fabs(-3.14)     = " << std::fabs(dn) << '\n';
    std::cout << "std::abs(-3.14)      = " << std::abs(dn) << '\n';

    // long long 整数 abs 也有重载，安全
    long long big = -9000000000000000000LL;
    std::cout << "std::abs(-9e18 LL)   = " << std::abs(big) << '\n';
    return 0;
}
```

```text
INT_MIN              = -2147483648
std::abs(INT_MIN)    = -2147483648
INT_MIN == abs?      = yes (overflowed)

std::fabs(-3.14)     = 3.14
std::abs(-3.14)      = 3.14
std::abs(-9e18 LL)   = 9000000000000000000
```

`std::abs(INT_MIN)` 给回 `-2147483648`，一个负数——取绝对值取出了负数。原因在补码表示：32 位补码能表示的负数比正数多一个（`[-2^31, 2^31-1]`），`INT_MIN` 即 `-2^31`，它的绝对值 `2^31` 在 `int` 里根本表示不出来，于是 `abs` 溢出，结果是**未定义行为**——本机 GCC 16.1.1 这里碰巧绕回成了原值，但标准不保证任何特定结果，优化器甚至可能基于「UB 不会发生」做让你更意外的变换。

::: warning abs(INT_MIN) 是未定义行为
`INT_MIN` 的绝对值无法用同类型 `int` 表示，`std::abs(INT_MIN)` 是 UB。如果你的输入可能触到 `INT_MIN`（比如解析可能返回极小值的整数、或者 `INT_MIN` 作哨兵值），要么用更宽的类型（先转 `long long` 再 `abs`），要么在调用前显式判一下。浮点没这个问题——`std::fabs` 和 `std::abs(double)` 重载都是安全的。
:::

顺带提一句历史上很容易踩的另一个坑：C 时代的 `abs` 在 `<cstdlib>` 里只对 `int` 有效，传 `long` / `long long` 会被悄悄截断。C++ 的 `std::abs` 在 `<cmath>` / `<cstdlib>` 里有完整重载集（`int` / `long` / `long long` / `float` / `double` / `long double`），所以**只要用 `std::abs` 而不是裸 `abs`，并把对应头文件 include 对**，就不会撞上老 C 的截断坑。

### `hypot`：朴素 `sqrt(x*x+y*y)` 会溢出

求直角三角形斜边、或者向量的模长，直觉写法是 `std::sqrt(x*x + y*y)`。能用，但藏着一个数值溢出陷阱——当 `x` 和 `y` 本身没溢出，可 `x*x` 已经溢出成 `inf` 的时候，结果就废了。`std::hypot` 内部用了一套避免中间溢出的等价算法，专门救这个：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << "hypot(3,4)           = " << std::hypot(3.0, 4.0) << '\n';
    std::cout << "sqrt(3*3+4*4)        = " << std::sqrt(3.0*3.0 + 4.0*4.0) << '\n';

    double x = 1e200, y = 1e200;   // x、y 都在 double 范围内
    std::cout << "\nnaive sqrt(x*x+y*y) = " << std::sqrt(x*x + y*y) << '\n';
    std::cout << "hypot(1e200, 1e200)  = " << std::hypot(x, y) << '\n';
    return 0;
}
```

```text
hypot(3,4)           = 5
sqrt(3*3+4*4)        = 5

naive sqrt(x*x+y*y) = inf
hypot(1e200, 1e200)  = 1.41421e+200
```

`1e200` 没溢出，但 `1e200 * 1e200 = 1e400` 远超 `double` 上限（约 `1.8e308`），平方这一步直接变成 `inf`，开方回来还是 `inf`，完全丢失了正确结果 `1.41421e+200`。`hypot` 算对了。C++17 起 `hypot` 还多了个三参数重载 `std::hypot(x, y, z)`，求三维向量模长同理。

教训：凡是要算模长、欧氏距离、$\sqrt{\sum x_i^2}$ 这种，别图省事写朴素平方和开方，直接上 `hypot`（高维用 `hypot` 链式或者更稳的算法）。除了防溢出，它对下溢（极小数平方变 0）也有保护，精度更稳。

### `fmod`：浮点取余，结果符号跟被除数

`std::fmod(x, y)` 是浮点版的取余，结果是 `x - n*y`，其中 `n` 是「向零截断」的商。它和 `std::remainder`（C++11，向最近整数取余）符号规则不同，是个常见的混淆点：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << "fmod(5.3, 2.0)       = " << std::fmod(5.3, 2.0) << '\n';
    std::cout << "fmod(-5.3, 2.0)      = " << std::fmod(-5.3, 2.0) << '\n';
    std::cout << "remainder(-5.3, 2.0) = " << std::remainder(-5.3, 2.0) << '\n';
    return 0;
}
```

```text
fmod(5.3, 2.0)       = 1.3
fmod(-5.3, 2.0)      = -1.3
remainder(-5.3, 2.0) = 0.7
```

`fmod(-5.3, 2.0)` 给 `-1.3`——结果的符号跟**被除数** `x` 走（因为商是向零截断的 `-2`，`-5.3 - (-2)*2 = -1.3`）。而 `remainder(-5.3, 2.0)` 给 `0.7`——它取**最近的**整数商 `-3`，`-5.3 - (-3)*2 = 0.7`。两者都对，但语义不同：要做周期映射（比如把角度归到 `[-pi, pi]`）通常想要 `remainder` 或 `fmod` 后再手动平移，搞混了会得到符号相反的结果。

## 精度：别用 `==` 比浮点，用 epsilon；`fma` 保单次舍入

这是整篇里最该记住的一节。前面讲的 `NaN` / 次正规数是「极端态」，而这一节讲的是**正常计算也会出问题**——浮点 `==` 几乎总是错的。

### `==` 为什么失灵：累加误差

浮点数是有限的二进制小数，大部分十进制小数（`0.1`、`0.2`）在 `double` 里都是无限循环、被截断的近似值。每一步运算都会引入微小的舍入误差，误差累积起来就能让「理应相等」的两个数在 `==` 下判为不等。最经典的演示——把 `0.1` 加十次：

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    double acc = 0.0;
    for (int i = 0; i < 10; ++i) acc += 0.1;
    std::cout << "sum of 10*0.1  = " << acc << '\n';
    std::cout << "acc == 1.0?    = " << (acc == 1.0 ? "true" : "false") << '\n';
    return 0;
}
```

```text
sum of 10*0.1  = 0.99999999999999989
acc == 1.0?    = false
```

数学上 `0.1 * 10 == 1`，但浮点累加出来的结果是 `0.99999999999999989`，`== 1.0` 判 `false`。如果你拿这个 `acc` 去做 `if (acc == 1.0) ...`，那个分支永远进不去。这就是为什么**浮点数永远不要直接用 `==` 比相等**。

替代方案是带容差的比较，俗称 epsilon 比较。基本思路是：两个数之差的绝对值落在某个很小的阈值（绝对容差 + 相对容差）内，就认为相等：

```cpp
// Standard: C++20
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>

bool nearly_equal(double a, double b, double abs_eps, double rel_eps)
{
    double diff = std::fabs(a - b);
    if (diff <= abs_eps) return true;                       // 绝对容差：处理接近 0 的情况
    return diff <= rel_eps * std::max(std::fabs(a), std::fabs(b));  // 相对容差：按数量级缩放
}

int main()
{
    std::cout << std::setprecision(17);
    double acc = 0.0;
    for (int i = 0; i < 10; ++i) acc += 0.1;
    std::cout << "acc == 1.0?            = " << (acc == 1.0) << '\n';
    std::cout << "nearly_equal(eps=1e-9) = "
              << nearly_equal(acc, 1.0, 1e-12, 1e-9) << '\n';
    return 0;
}
```

```text
acc == 1.0?            = false
nearly_equal(eps=1e-9) = true
```

`nearly_equal` 这套写法的要点：**绝对容差** `abs_eps` 专门兜底「两数都接近 0」的情况（这时相对容差会失灵，因为分母也接近 0）；**相对容差** `rel_eps` 按数值的量级缩放，处理大数（比 `1e9` 大的数，期望的误差也比 `1e-6` 大）。两个容差一起用，覆盖全量级。具体阈值取多少要看场景——图形学里 `1e-5` 就够，科学计算常常要 `1e-12`。没有银弹，但「`==` 改成带容差比较」这一步几乎永远是该做的。

### `fma`：一次融合乘加，只舍入一次

`a * b + c` 这个再普通不过的式子，朴素写法要做两次运算、两次舍入（先算 `a*b` 舍入到 `double`，再加 `c` 再舍入一次）。`std::fma(a, b, c)`（C++99 从 C 借来，C++11 起标配）把这个表达式做成**单次**融合乘加（fused multiply-add）——中间的无限精度乘积直接加上 `c`，最后**只舍入一次**。

只舍入一次的好处是精度。代价是——见下面实测——当编译器已经把朴素表达式收缩成硬件 FMA 指令时，`std::fma` 可能**更慢**（因为它强制走标准库实现以保证语义）。先看精度差异，这个差异是确凿的：

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    // (1/3)*3 在朴素两步里被舍入成 1.0，再减 1 得 0
    // fma 单次舍入，保留了 (1/3)*3 的真实近似值
    double p = 1.0 / 3.0;
    double naive = p * 3.0 + (-1.0);
    double fused = std::fma(p, 3.0, -1.0);
    std::cout << "naive (1/3)*3 - 1 = " << naive << '\n';
    std::cout << "fma   (1/3)*3 - 1 = " << fused << '\n';
    return 0;
}
```

```text
naive (1/3)*3 - 1 = 0
fma   (1/3)*3 - 1 = -5.5511151231257827e-17
```

两者结果**不一样**：朴素写法给 `0`，`fma` 给 `-5.55e-17`。哪个「对」？取决于你怎么定义「对」——`1.0/3.0` 在 `double` 里被截断成 `0.333...`（一个比真实 `1/3` 略小的近似），朴素写法里 `*3` 又把它舍入回了恰好 `1.0`（误差恰好被第二步乘法「修正」了），`-1` 得 `0`；而 `fma` 不做中间舍入，老老实实反映了「`0.333... * 3` 略小于 1」这个事实，减 1 得到一个极小的负数。从「忠实反映中间近似值的真实数学结果」角度看，`fma` 更诚实；从「我希望 `(x/x)*x` 算出 `x`」的角度看，朴素反而更顺手。这正是浮点精度的微妙之处——**没有绝对正确的答案，只有「我清楚自己在要什么」**。

这个例子还顺带印证了上一节的结论：连 `(1/3)*3 == 1.0` 这种「显然成立」的等式，朴素写法成立、`fma` 写法不成立——再次说明 `==` 比浮点多么不可靠。

那性能呢？理论上硬件 FMA 比「一次乘 + 一次加」更快（一条指令干两件事）。但 `std::fma` 是个**库函数**，语义上必须保证「单次舍入」，编译器不能随便把它和上下文折叠。我们用一个 noinline 的循环跑两组对比：

```cpp
// Standard: C++20
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

__attribute__((noinline)) double run_naive(const double* x, const double* y,
                                           const double* z, std::size_t n)
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += x[i] * y[i] + z[i];
    return s;
}

__attribute__((noinline)) double run_fma(const double* x, const double* y,
                                         const double* z, std::size_t n)
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += std::fma(x[i], y[i], z[i]);
    return s;
}

int main()
{
    constexpr std::size_t kN = 4'000'000;
    static double x[kN], y[kN], z[kN];
    for (std::size_t i = 0; i < kN; ++i) {
        x[i] = static_cast<double>(i % 1000) * 0.001 + 0.5;
        y[i] = static_cast<double>(i %  500) * 0.002 + 0.25;
        z[i] = static_cast<double>(i %  200) * 0.003 + 0.125;
    }
    volatile double sink = 0.0;
    auto t1 = std::chrono::high_resolution_clock::now();
    double r1 = run_naive(x, y, z, kN);
    auto t2 = std::chrono::high_resolution_clock::now();
    double r2 = run_fma(x, y, z, kN);
    auto t3 = std::chrono::high_resolution_clock::now();
    sink = r1 + r2;
    std::cout << "naive a*b+c: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
              << " ms\n";
    std::cout << "fma:         "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count()
              << " ms\n";
    return 0;
}
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）连跑三次，结果稳定：

```text
naive a*b+c: 4 ms
fma:         13 ms
```

`fma` 反而**慢了三倍多**。原因在于 `-O2` 下编译器本来就会把 `x[i] * y[i] + z[i]` 自动收缩成硬件 FMA 指令（x86 上是 `vfmadd`），又顺手做了循环向量化，一条指令干完乘加还吃了 SIMD 的红利；而显式的 `std::fma` 因为要严格保证「单次舍入」的库语义，编译器不敢随意向量化折叠，退化成逐元素调用，于是更慢。

所以对 `fma` 要有个清醒的认识：

::: warning fma 是精度工具，不一定是性能工具
`std::fma` 的核心价值是**精度**（单次舍入，避免中间结果丢位），不是速度。在你的朴素 `a*b+c` 已经被编译器收缩成硬件 FMA 的常见场景下，显式 `std::fma` 反而可能更慢。需要 `fma` 的情况是：朴素计算出现了可见的精度损失（灾难性抵消、中间值溢出或丢位），并且你不能依赖编译器的自动收缩（开了 `-ffp-contract=off`，或者要跨平台严格可复现）。否则朴素写法既够准又够快。
:::

判断「朴素够不够准」最实在的办法，就是像上面那样把你怀疑的表达式和 `fma` 版各跑一遍，比一比结果差异——差异在你容忍范围内就用朴素，否则换 `fma`。

## C++17 特殊数学函数：`beta` / `riemann_zeta` / 椭圆积分

到 C++17，`<cmath>` 收进了一大票「特殊数学函数」——贝塔函数、黎曼 ζ 函数、各类拉盖尔/勒让德/埃尔米特多项式、各种椭圆积分。这些主要面向科学计算和工程（量子力学、统计学、电磁场），日常业务代码基本用不到，这里只点到它们存在，实测两个让读者有个直观印象：

```cpp
// Standard: C++17
#include <cmath>
#include <iomanip>
#include <iostream>

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "beta(1,1)           = " << std::beta(1.0, 1.0) << '\n';      // 数学上 = 1
    std::cout << "riemann_zeta(2)     = " << std::riemann_zeta(2.0) << '\n';   // = pi^2/6 ~ 1.6449
    std::cout << "comp_ellint_2(0)    = " << std::comp_ellint_2(0.0) << '\n';  // = pi/2 ~ 1.5708
    std::cout << "assoc_laguerre(2,0,1) = " << std::assoc_laguerre(2, 0, 1.0) << '\n';
    return 0;
}
```

```text
beta(1,1)           = 1
riemann_zeta(2)     = 1.6449340668482264
comp_ellint_2(0)    = 1.5707963267948968
assoc_laguerre(2,0,1) = -0.5
```

`beta(1,1)` 是 `1`（贝塔函数 $B(1,1) = \Gamma(1)\Gamma(1)/\Gamma(2) = 1$），`riemann_zeta(2)` 是 $1.6449...$（即著名的巴塞尔问题 $\pi^2/6$），`comp_ellint_2(0)` 是 $\pi/2 \approx 1.5708$——都对得上数学预期。GCC 16.1.1 全部支持。

有一点要先打预防针：这一批函数在标准里被标注为「no longer part of C++」的传闻不时出现（C++23 期间有过移除讨论，最终在 C++26 草案里被正式移除）。但对 GCC 16.1.1 这个本机工具链而言，它们仍然完整可用。如果你写的是长期维护的科学计算代码，建议把它们当作「能用、但未来可能要换实现」的依赖，封装一层别散用，万一迁移也只改一处。

## C++20 数学常量：`std::numbers`

最后一个板块。以前要用 $\pi$，标准做法是 `const double PI = 3.141592653589793;` 或者 `M_PI`（POSIX，不在 C++ 标准里，可移植性看平台）。C++20 给了正经的编译期常量——`std::numbers`：

```cpp
// Standard: C++20
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numbers>

int main()
{
    std::cout << std::setprecision(17);
    std::cout << "std::numbers::pi    = " << std::numbers::pi << '\n';
    std::cout << "std::numbers::e     = " << std::numbers::e << '\n';
    std::cout << "std::numbers::sqrt2 = " << std::numbers::sqrt2 << '\n';
    std::cout << "std::numbers::phi   = " << std::numbers::phi << '\n';   // 黄金比例
    std::cout << "cos(pi)             = " << std::cos(std::numbers::pi) << '\n';

    // 它们是变量模板，能编译期用
    constexpr double kPi = std::numbers::pi_v<double>;
    std::cout << "constexpr pi_v      = " << kPi << '\n';
    static_assert(std::numbers::pi_v<double> > 3.14, "pi > 3.14");
    return 0;
}
```

```text
std::numbers::pi    = 3.1415926535897931
std::numbers::e     = 2.7182818284590451
std::numbers::sqrt2 = 1.4142135623730951
std::numbers::phi   = 1.6180339887498949
cos(pi)             = -1
constexpr pi_v      = 3.1415926535897931
```

`std::numbers::pi` / `e` / `sqrt2` / `phi`（黄金比例）/ `ln2` / `log2e` / `egamma`（欧拉常数）等一票常量都在，且匹配 `double` 的全精度。注意 `cos(pi)` 算出来是精确的 `-1`——因为 `pi` 这个 `double` 值本身比真实 $\pi$ 略大一点点，`cos` 的舍入恰好把这个误差吃掉，给出了干净的 `-1`（这也是为什么前面 `fma` 那节强调浮点结果有时「意外地准」，别拿这种巧合当普遍规律）。

`std::numbers::pi` 其实是个变量模板（variable template）`std::numbers::pi_v<T>` 的快捷写法，针对 `T = float/double/long double` 各有特化。这意味着你可以把它当编译期常量用——`constexpr`、`static_assert`、模板参数里都能塞，这是手写 `const double PI = 3.14` 做不到的。GCC 16.1.1 完整支持。

`M_PI` 那种老写法不是不能用（要 `#define _USE_MATH_DEFINES` 再 `#include <cmath>`，且依赖 POSIX 扩展），但既然 C++20 给了标准设施，新代码就别再用 `M_PI` 了——可移植、类型安全、编译期可用，全面更好。

## 整数溢出与 `<cmath>`：行为是「定义好的」

最后简单收一下「溢出」这个话题在 `<cmath>` 里的表现。前面讲 `hypot` 时已经看到，`<cmath>` 函数对溢出和定义域错误有一套相对确定的语义，不像整数溢出那样是 UB：

```cpp
// Standard: C++20
#include <cmath>
#include <iostream>

int main()
{
    std::cout << std::boolalpha;
    std::cout << "exp(1000) isinf? " << std::isinf(std::exp(1000.0)) << '\n';  // 上溢 -> +inf
    std::cout << "pow(2,1024) isinf? " << std::isinf(std::pow(2.0, 1024.0)) << '\n'; // 2^1024 刚好溢出
    std::cout << "log(0) isinf?   " << std::isinf(std::log(0.0)) << '\n';     // -> -inf
    std::cout << "log(-1) isnan?  " << std::isnan(std::log(-1.0)) << '\n';    // 定义域错 -> NaN
    std::cout << "sqrt(-1) isnan? " << std::isnan(std::sqrt(-1.0)) << '\n';   // 定义域错 -> NaN
    return 0;
}
```

```text
exp(1000) isinf? true
pow(2,1024) isinf? true
log(0) isinf?   true
log(-1) isnan?  true
sqrt(-1) isnan? true
```

规律很清楚：

- **上溢**（结果超出 `double` 上限，约 $1.8 \times 10^{308}$）——返回 `±inf`。`exp(1000)` 远超这个上限，给 `+inf`；`pow(2, 1024)` 因为 $2^{1024}$ 刚好越过 `double` 最大指数（最大正常指数是 1023），也给 `inf`。
- **定义域错**（输入无意义，如 `sqrt` / `log` 收到负数）——返回 `NaN`。
- **极点**（如 `log(0)`）——返回 `-inf`。

这些不是未定义行为，是 IEEE 754 + C 标准库规定的确定语义（具体是否额外置 `errno` 看 `<cerrno>` 和编译器的 `-fmath-errno` 设置，默认 GCC 不置）。对比之下，整数运算的溢出（`abs(INT_MIN)`、`a + b` 越界）才是真 UB。所以一句话总结：「在 `<cmath>` 里，异常结果有确定形态（`inf` / `NaN`），可以用 `isinf` / `isnan` 检测；真正危险的溢出在整数侧，要靠更宽类型或前置检查规避」。

## 小结

`<cmath>` 看着是个「数学函数大全」，真正决定你能不能用对的，是对浮点表示和精度的理解。几条关键结论收一下：

- **浮点分类**：`fpclassify` 是总入口，`isnan` / `isinf` / `isfinite` / `isnormal` 是快捷谓词。浮点数有正常数、零（含正零负零）、无穷、`NaN`、次正规数五种状态，次正规数精度降级，`isnormal` 能帮你判「这数精度有没有保证」。
- **`NaN` 不等于自己**：`NaN == NaN` 是 `false`，判 `NaN` 永远用 `std::isnan`，判无穷用 `std::isinf`，绝不用 `==`。
- **`abs(INT_MIN)` 是 UB**：补码不对称导致 `INT_MIN` 的绝对值无法用 `int` 表示。浮点没这问题，`std::fabs` / `std::abs(double)` 安全。
- **`hypot` 救溢出**：`sqrt(x*x+y*y)` 会在中间平方时溢出成 `inf`，`std::hypot` 用防溢出算法给出正确结果。
- **别用 `==` 比浮点**：累加误差让「理应相等」的数判不等。用绝对 + 相对 epsilon 的 `nearly_equal` 比较，阈值按场景定。
- **`fma` 保精度不一定提速**：`std::fma(a,b,c)` 单次舍入保精度，但 `-O2` 下朴素 `a*b+c` 常被自动收缩成硬件 FMA 还能向量化，反而更快。需要 `fma` 的是精度敏感、不能依赖编译器收缩的场景。
- **C++17 特殊函数 / C++20 `std::numbers`**：特殊数学函数（`beta` / `riemann_zeta` / 椭圆积分）面向科学计算，C++26 草案已移除但 GCC 16.1.1 仍支持；`std::numbers::pi` 等是编译期全精度常量，新代码用它替代 `M_PI`。
- **`<cmath>` 的溢出是定义好的**：上溢给 `inf`，定义域错给 `NaN`，极点给 `±inf`，都能用 `isinf` / `isnan` 检测；真正危险的 UB 溢出在整数侧。

下一篇我们继续在标准库的数学相关设施里转，把视角从「单点函数」拉到「类型层面」——`std::complex` 怎么把复数运算做成和实数一样自然，以及它和 `<cmath>` 函数的复数重载是怎么咬合的。

## 参考资源

- [cppreference: Common math functions (`<cmath>`)](https://en.cppreference.com/w/cpp/numeric/math) —— 函数总览、`fpclassify` / `isnan` / `abs` / `fma` / `hypot` 的签名与行为
- [cppreference: Floating-point environment](https://en.cppreference.com/w/cpp/numeric/fenv) —— 浮点环境、舍入模式、`errno` 与 `-fmath-errno`
- [cppreference: Special mathematical functions (C++17)](https://en.cppreference.com/w/cpp/numeric/special_functions) —— `beta` / `riemann_zeta` / 椭圆积分等
- [cppreference: Mathematical constants `std::numbers` (C++20)](https://en.cppreference.com/w/cpp/numeric/constants) —— `pi` / `e` / `sqrt2` 等编译期常量
- [IEEE 754-2019](https://standards.ieee.org/ieee/7594/) —— 浮点表示、`NaN` 语义、次正规数的标准出处
