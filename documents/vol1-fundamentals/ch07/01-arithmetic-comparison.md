---
title: "算术与比较运算符"
description: "掌握算术运算符和比较运算符的重载方法，实现一个完整的 Fraction 分数类"
chapter: 7
order: 1
difficulty: intermediate
reading_time_minutes: 15
platform: host
prerequisites:
  - "this 指针与链式调用"
tags:
  - cpp-modern
  - host
  - intermediate
  - 进阶
cpp_standard: [11, 14, 17, 20]
---
# 算术与比较运算符

到目前为止，我们自定义的类型只能通过成员函数来操作——想加两个对象，得写 `a.add(b)`；想判断相等，得写 `a.equals(b)`。说实话，这种写法用在业务逻辑里倒也无所谓，但一旦涉及到数学运算、物理量、日期这类"天然带有运算语义"的类型，满屏幕的 `.add()` 和 `.compare()` 就会让人特别难受。我们更希望代码读起来像数学表达式本身：`a + b`、`x == y`、`p1 < p2`。

运算符重载就是 C++ 给我们提供的这个能力——让自定义类型直接使用 `+`、`-`、`==`、`<` 这些运算符，代码读起来自然，写起来也舒服。这一章我们聚焦在算术运算符和比较运算符上，用一个完整的 `Fraction`（分数）类把整个过程走通。

> **踩坑预警**：运算符重载虽好，但千万不要滥用。只有当运算符的含义"一眼就能看明白"时才值得重载——比如 `a + b` 表示加法、`a == b` 表示判等。如果你打算用 `+` 表示"从容器中删除元素"，那还不如老老实实写一个 `remove()` 函数，不然接手你代码的人可能会半夜打电话友好的跟你交流的（确信）

## 为什么要重载运算符

在动手实现之前，我们先把动机想清楚。核心原因只有一个——可读性。假设有一个二维向量类，两种写法放在一起对比就很明显了：

```cpp
// 函数调用风格
auto v3 = v1.add(v2);
auto v4 = v1.scale(2.0f);

// 运算符重载风格
auto v3 = v1 + v2;
auto v4 = v1 * 2.0f;
```

第二种写法跟数学公式几乎一模一样，阅读代码的时候不需要在脑子里做额外的"翻译"。这在处理复杂表达式时差距更明显——`a + b * c - d / e` 对比 `a.add(b.scale(c)).subtract(d.divide(e))`，前者一目了然，后者读着读着就迷失了。

但运算符重载是一个需要克制的特性。笔者就一个准则：**当你觉得某个运算符"自然而然"就该这么用时，才去重载它**。向量加法用 `+` 是自然的，日期比较用 `<` 是自然的，但如果你给一个日志类重载了 `<<` 让它"发送日志到远程服务器"，那语义已经跑偏了。

## 成员还是非成员——一个影响深远的选择

运算符可以通过两种方式重载：**成员函数**和**非成员函数**。这个选择不仅影响语法，还直接影响类型转换的行为。

成员函数的左侧操作数**必须**是当前类的对象。如果你把 `operator+` 写成成员函数，那么 `Fraction(1, 2) + 3` 可以工作（`3` 能通过构造函数隐式转换为 `Fraction`），但 `3 + Fraction(1, 2)` 就不行了——编译器不会跑到 `int` 上面去找 `operator+`。非成员函数没有这个限制，左右两个操作数是对称的，编译器会对两侧都尝试隐式转换，所以 `3 + f` 和 `f + 3` 都能正常工作。而赋值类的运算符（`=`, `+=`, `-=`, `[]`, `()` 等）则必须是成员函数——语言规定了其中某些运算符只能作为成员重载，而且赋值操作的左侧本来就是被修改的对象，放在成员函数里语义最自然。

由此得出一个被广泛采用的实现模式：先把复合赋值运算符（如 `+=`）实现为成员函数，然后基于它来实现二元运算符（如 `+`）作为非成员函数。二元运算的逻辑完全复用了复合赋值的代码，不需要重复写加法细节，而且非成员的位置保证了左右操作数的对称性。我们在 `Fraction` 类里就会严格遵循这个模式。

## 从 `operator+=` 开始搭建算术运算

理论讲够了，现在开始动手。`Fraction` 类先从复合赋值运算符开始：

```cpp
class Fraction {
private:
    int numerator_;   // 分子
    int denominator_; // 分母

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) {
            denominator_ = 1;
        }
        normalize();
    }

    // 复合赋值：就地修改，返回 *this 的引用
    Fraction& operator+=(const Fraction& rhs)
    {
        // a/b + c/d = (a*d + c*b) / (b*d)
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

private:
    void normalize()
    {
        int g = gcd(numerator_, denominator_);
        numerator_ /= g;
        denominator_ /= g;
        if (denominator_ < 0) {
            numerator_ = -numerator_;
            denominator_ = -denominator_;
        }
    }

    static int gcd(int a, int b)
    {
        a = (a < 0) ? -a : a;
        b = (b < 0) ? -b : b;
        while (b != 0) { int t = b; b = a % b; a = t; }
        return (a == 0) ? 1 : a;
    }
};
```

这里有两个要点。第一，`operator+=` 的返回类型是 `Fraction&`，返回的是 `*this` 的引用——这就是链式调用的基础，`a += b += c` 才能正确工作。第二，每次运算后我们都进行了约分（`normalize()`），保证分数始终是最简形式且分母为正。这是分数类的内在不变量，维护好它可以让后续的比较操作更简单——两个最简分数相等当且仅当分子分母完全相同，不需要额外通分。

> **踩坑预警**：`operator+=` 必须返回 `*this` 的引用（`Fraction&`），而不是按值返回。如果你写成 `Fraction operator+=(...)`，虽然编译能过，但 `a += b` 返回的是一个临时对象而不是 `a` 本身，链式赋值 `(a += b) = c` 就不会修改 `a`——这跟内置类型的行为完全不一致。`operator-=`、`operator*=`、`operator/=` 都要遵守同样的规则。

有了 `+=` 之后，`+` 的实现就非常简洁了：

```cpp
// 非成员函数：通过 += 来实现 +
Fraction operator+(Fraction lhs, const Fraction& rhs)
{
    lhs += rhs;  // 复用 operator+=
    return lhs;  // 返回修改后的副本
}
```

注意 `lhs` 是**按值传递**的，它本身就是调用者传入参数的拷贝，所以直接在 `lhs` 上调用 `+=`，修改的是这个副本而不是原始对象。函数结束时返回这个副本，正好就是加法的结果，既复用了 `+=` 的逻辑，又避免了额外创建临时对象。

> **踩坑预警**：二元算术运算符（`+`, `-`, `*`, `/`）必须返回**新对象（按值返回）**，而不是引用。因为 `a + b` 的结果是一个新值，它跟 `a` 和 `b` 都没有关系——如果你返回了局部变量的引用，那就是典型的悬垂引用（dangling reference），使用时大概率会得到垃圾值或者直接崩溃。

其余运算符模式完全相同。先补齐 `*=` 和 `/=`：

```cpp
Fraction& operator*=(const Fraction& rhs)
{
    numerator_ *= rhs.numerator_;
    denominator_ *= rhs.denominator_;
    normalize();
    return *this;
}

Fraction& operator/=(const Fraction& rhs)
{
    // 除以一个分数等于乘以它的倒数
    numerator_ *= rhs.denominator_;
    denominator_ *= rhs.numerator_;
    if (denominator_ == 0) { denominator_ = 1; }
    normalize();
    return *this;
}
```

然后基于它们派生二元运算：`Fraction operator-(Fraction lhs, const Fraction& rhs)` 在内部调用 `lhs -= rhs; return lhs;`，乘法和除法同理，不再赘述。

## 比较运算符——从 `==` 到全套六个

因为我们已经在 `normalize()` 里保证了分数始终是最简形式，相等比较非常简单——分子分母都相同就是相等：

```cpp
bool operator==(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() == rhs.num() && lhs.den() == rhs.den();
}

// 关键：!= 始终基于 == 来实现
bool operator!=(const Fraction& lhs, const Fraction& rhs)
{
    return !(lhs == rhs);
}
```

> **踩坑预警**：`operator!=` **必须**基于 `operator==` 来实现，写成 `!(lhs == rhs)`，而不是自己重新写一套比较逻辑。如果你分别独立实现 `==` 和 `!=`，迟早有一天你会修改了其中一个却忘了同步另一个，导致 `a == b` 和 `!(a != b)` 给出矛盾的结果。这不仅是逻辑 bug，还会让依赖比较操作的容器和算法（比如 `std::set`、`std::find`）全部乱套。

关系比较也是同样的思路。数学上 `a/b < c/d` 等价于 `a*d < c*b`（假设分母都是正数，`normalize()` 已经保证了这一点），然后 `>`、`<=`、`>=` 全部基于 `<` 来派生：

```cpp
bool operator<(const Fraction& lhs, const Fraction& rhs)
{
    return lhs.num() * rhs.den() < rhs.num() * lhs.den();
}
bool operator>(const Fraction& lhs, const Fraction& rhs)  { return rhs < lhs; }
bool operator<=(const Fraction& lhs, const Fraction& rhs) { return !(rhs < lhs); }
bool operator>=(const Fraction& lhs, const Fraction& rhs) { return !(lhs < rhs); }
```

我们只实际写了 `<` 的逻辑，其他三个全是基于 `<` 来实现的——这和 `!=` 基于 `==` 是同一个道理：单一真相源（single source of truth），修改时只需要改一个地方。

## 对称性与隐式转换——让 `3 + f` 也能工作

前面一直在说"非成员函数保证对称性"，现在来看具体效果。`Fraction` 的构造函数有两个 `int` 参数且都有默认值，所以 `Fraction f = 3;` 会创建 `Fraction(3, 1)`。当 `operator+` 是非成员函数时，编译器在遇到 `3 + Fraction(1, 2)` 时会尝试把 `3` 隐式转换为 `Fraction(3, 1)`，然后调用 `operator+`，一切正常。但如果 `operator+` 是成员函数，`3.operator+(Fraction(1,2))` 就完全不合法了——`int` 可没有什么 `operator+` 接受 `Fraction` 参数。

因为我们通过 `num()` 和 `den()` 暴露了数据访问，非成员函数不需要 `friend` 也能工作。如果你的类不方便暴露 getter，那就用 `friend` 函数来访问私有成员。

> **踩坑预警**：如果你决定给构造函数加上 `explicit` 来禁止隐式转换（这本身是个好习惯），那 `3 + Fraction(1, 2)` 就会编译失败。你需要额外提供接受 `int` 的重载版本：`Fraction operator+(int lhs, const Fraction& rhs)`。对于数学类型的类，不加 `explicit` 是常见的取舍——牺牲一点安全性换来更自然的表达式。

## 实战：完整的 fraction.cpp

现在我们把所有零件组装起来：

```cpp
// fraction.cpp
#include <iostream>

class Fraction {
private:
    int numerator_;
    int denominator_;

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
    }

    Fraction& operator+=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator-=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     - rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator*=(const Fraction& rhs)
    {
        numerator_ *= rhs.numerator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator/=(const Fraction& rhs)
    {
        numerator_ *= rhs.denominator_;
        denominator_ *= rhs.numerator_;
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
        return *this;
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

    Fraction operator-() const { return Fraction(-numerator_, denominator_); }

private:
    void normalize()
    {
        int g = gcd(numerator_, denominator_);
        numerator_ /= g;
        denominator_ /= g;
        if (denominator_ < 0) {
            numerator_ = -numerator_;
            denominator_ = -denominator_;
        }
    }

    static int gcd(int a, int b)
    {
        a = (a < 0) ? -a : a;
        b = (b < 0) ? -b : b;
        while (b != 0) { int t = b; b = a % b; a = t; }
        return (a == 0) ? 1 : a;
    }
};

// 二元算术（非成员）
Fraction operator+(Fraction lhs, const Fraction& rhs) { lhs += rhs; return lhs; }
Fraction operator-(Fraction lhs, const Fraction& rhs) { lhs -= rhs; return lhs; }
Fraction operator*(Fraction lhs, const Fraction& rhs) { lhs *= rhs; return lhs; }
Fraction operator/(Fraction lhs, const Fraction& rhs) { lhs /= rhs; return lhs; }

// 比较（非成员）
bool operator==(const Fraction& l, const Fraction& r)
{ return l.num() == r.num() && l.den() == r.den(); }
bool operator!=(const Fraction& l, const Fraction& r) { return !(l == r); }
bool operator<(const Fraction& l, const Fraction& r)
{ return l.num() * r.den() < r.num() * l.den(); }
bool operator>(const Fraction& l, const Fraction& r)  { return r < l; }
bool operator<=(const Fraction& l, const Fraction& r) { return !(r < l); }
bool operator>=(const Fraction& l, const Fraction& r) { return !(l < r); }

std::ostream& operator<<(std::ostream& os, const Fraction& f)
{ os << f.num() << "/" << f.den(); return os; }

int main()
{
    Fraction a(1, 2), b(1, 3);

    std::cout << a << " + " << b << " = " << (a + b) << std::endl;
    std::cout << a << " - " << b << " = " << (a - b) << std::endl;
    std::cout << a << " * " << b << " = " << (a * b) << std::endl;
    std::cout << a << " / " << b << " = " << (a / b) << std::endl;

    // 与整数的混合运算（隐式转换）
    std::cout << a << " + 1 = " << (a + 1) << std::endl;
    std::cout << "2 * " << b << " = " << (2 * b) << std::endl;

    a += b;
    std::cout << "a += b -> a = " << a << std::endl;

    Fraction c(1, 6), d(1, 4);
    std::cout << c << " == " << d << " : " << (c == d) << std::endl;
    std::cout << c << " < " << d << " : " << (c < d) << std::endl;
    std::cout << c << " >= " << d << " : " << (c >= d) << std::endl;

    Fraction e(3, 4);
    std::cout << "-" << e << " = " << (-e) << std::endl;

    return 0;
}
```

编译运行：

```bash
g++ -Wall -Wextra -std=c++17 fraction.cpp -o fraction && ./fraction
```

验证输出：

```text
1/2 + 1/3 = 5/6
1/2 - 1/3 = 1/6
1/2 * 1/3 = 1/6
1/2 / 1/3 = 3/2
1/2 + 1 = 3/2
2 * 1/3 = 2/3
a += b -> a = 5/6
1/6 == 1/4 : 0
1/6 < 1/4 : 1
1/6 >= 1/4 : 0
-3/4 = -3/4
```

所有运算结果都正确。`a + b` 得到 `5/6`（通分后 `3/6 + 2/6`），除法 `1/2 / 1/3` 得到 `3/2`，混合运算 `2 * 1/3` 也正常工作——`2` 被隐式转换为 `Fraction(2, 1)` 然后参与乘法。约分在每个运算步骤中都自动完成了，这是 `normalize()` 的功劳。

## C++20 的曙光——三路比较运算符 `<=>`

在结束之前不得不提一下 C++20 引入的三路比较运算符（spaceship operator）`<=>`。如果编译器支持 C++20，只需要实现一个 `operator<=>`，编译器就能自动生成全部六个比较运算符：

```cpp
// C++20：一行搞定所有比较
auto operator<=>(const Fraction&, const Fraction&) = default;
```

如果类的成员变量本身都支持三路比较（`int` 当然支持），直接 `= default` 就完事了。这省去了手写六个比较函数的工作量，也彻底杜绝了"改了 `<` 忘了改 `<=`"这类 bug。不过目前我们的教程以 C++17 为基准线，手写比较运算符仍然是必须掌握的基本功。

## 练习

**练习 1：补全 Fraction 的减法和除法**

上面的完整代码里已经给出了 `operator-=` 和 `operator/=` 的实现，但如果你是跟着教程一步步写下来的，试着在不看答案的情况下独立完成这两个运算符，然后对照代码检查是否一致。重点关注除法中对零分母的处理。

**练习 2：为 Date 类实现比较运算符**

创建一个 `Date` 类，包含 `year`、`month`、`day` 三个字段，实现全部六个比较运算符。提示：可以先实现 `operator<`（依次比较年、月、日），然后基于它派生其他五个。思考一下：如果两个 `Date` 对象的年份不同但月份相同，比较逻辑应该怎么写？

## 小结

这一章我们围绕运算符重载的核心实践，走完了从理论到实现的完整路径。复合赋值运算符（`+=`, `-=`, `*=`, `/=`）实现为成员函数，就地修改对象并返回 `*this` 的引用；二元算术运算符（`+`, `-`, `*`, `/`）实现为非成员函数，按值传递左侧操作数、复用复合赋值来实现，按值返回新对象；比较运算符中 `!=` 基于 `==` 实现，`>`、`<=`、`>=` 基于 `<` 实现，保证单一真相源。非成员函数保证了左右操作数的对称性，让 `3 + f` 和 `f + 3` 都能正常工作。

下一章我们继续运算符重载的旅程，来看看流运算符（`<<`、`>>`）和下标运算符（`[]`）的重载方法——前者让自定义类型能跟 `std::cout` 打交道，后者是自定义容器的标配接口。
