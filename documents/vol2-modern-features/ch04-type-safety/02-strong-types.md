---
chapter: 4
cpp_standard:
- 11
- 14
- 17
description: 用 phantom type 模式和 C++17 参数推导实现类型安全的单位系统
difficulty: intermediate
order: 2
platform: host
reading_time_minutes: 11
related:
- 用户自定义字面量
tags:
- host
- cpp-modern
- intermediate
- 类型安全
- 类型别名
title: 强类型 typedef：防止混淆的类型安全
---
# 强类型 typedef：防止混淆的类型安全

笔者在一次代码审查里见过一个非常经典的 bug：一个函数的签名是 `void set_rect(int width, int height)`，调用方写成了 `set_rect(h, w)`，参数顺序搞反了。编译器当然一点警告都没有，因为 `width` 和 `height` 的类型都是 `int`，所以类型完全匹配。但屏幕上的矩形就是歪的。这 bug 其实不难解，但是就是感觉整个人被狠狠发可了一顿。

根源倒是不复杂，咱们一句话就能摊开：`typedef` 和 `using` 创建的只是**类型别名**而非新类型。等咱们写完 `using Width = int;` 和 `using Height = int;`，`Width` 和 `Height` 其实仍然是同一个 `int`，编译器不会替您区分它们。想要编译器真能区分的类型，咱们就得请出“强类型 typedef”这门技术。它还有两个名字呢。opaque typedef 直译的话是不透明别名，phantom type 咱们一般叫幽灵类型。

咱们就从眼下的第一步走起，把 `typedef` 的局限摆出来看清楚。

## 第一步——理解 typedef / using 的局限

咱们直接上代码，您感受一下普通别名到底有多“脆弱”：

```cpp
using UserId = int;
using OrderId = int;

UserId uid = 42;
OrderId oid = 100;

// 以下全部编译通过，没有任何警告
uid = oid;           // OrderId 赋给 UserId？编译器觉得没问题
OrderId another = uid;  // 反过来也行

void process_order(OrderId id);
process_order(uid);   // 传了 UserId 进去？编译器不管

int total = uid + oid;  // 两个"不同语义"的 ID 相加？随便加
```

问题咱们看清楚了：`using UserId = int` 只是给 `int` 起了个绰号。在编译器的眼里，`UserId`、`OrderId` 和 `int` 就是同一个东西的三个名字。凡是接受 `int` 的操作，`UserId` 和 `OrderId` 其实全都能掺一脚，编译器连语义上完全说不通的组合都照收。

进了大型代码库，隐患也跟着被放大了。函数的参数列表越长，参数类型越是翻来覆去地用同一个底层类型，出错的概率就越高。更麻烦的是，编译器偏偏一声不吭，单元测试的覆盖也未必到位，最后能指望的只剩人眼，得靠您在 code review 里把它翻出来。可人眼偏偏最不擅长发现“看起来都对”的问题。您想想，`uid = oid` 夹在一排赋值语句的中间，谁会多看它一眼呢。

## 第二步——Phantom Type 模式

解法是现成的，就是前面已经点过名的幽灵类型 phantom type。咱们把它的机制拆开看：模板参数只起标记的作用，实际的空间一丁点不占，靠它把不同的类型区分开。

```cpp
// 标签结构体，只用来区分类型，不需要实现任何东西
struct WidthTag {};
struct HeightTag {};

// 强类型包装器
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    constexpr explicit StrongInt(Rep value) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }

private:
    Rep value_;
};

using Width  = StrongInt<WidthTag>;
using Height = StrongInt<HeightTag>;
```

现在 `Width` 和 `Height` 是两个完全不同的类型，您要是把一个赋给另一个，编译器会直接把您拦下来：

```cpp
Width w(100);
Height h(200);

// h = w;          // 编译错误！不能把 Width 赋给 Height
// Width bad = h;  // 编译错误！

void set_rect(Width w, Height h);
set_rect(h, w);    // 编译错误！参数类型不匹配
set_rect(Width(100), Height(200));  // OK
```

咱们再看 `WidthTag` 和 `HeightTag`：它们是空的类，但在这里只起到模板形参的作用，只参与编译期的类型区分，也不进入对象的布局，所以 `StrongInt<WidthTag>` 的大小和裸 `int` 一样。

整套模式的收益就落在这一句上：**用编译期的类型信息，换运行时的零开销**。咱们要的类型检查全部在编译期完成，运行时剩下的就是普通的整数操作。

咱们把两种写法放在一起，同一句 `set_rect(h, w)` 的编译结果是这样的，顺带也把 `StrongInt` 包装器的要点收进了图里：

![类型别名与强类型包装对同一句 set_rect(h, w) 的不同编译结果](./02-strong-types-wrapper.drawio)

## 第三步——构建实用的强类型包装器

刚才的 `StrongInt` 还太素了，真到了项目里，光靠构造和取值就撑不住了。加减、比较、流输出这些日常的操作都得有，咱们把它扩成一份实用版本：

```cpp
#include <cstdint>
#include <functional>
#include <iostream>
#include <type_traits>

/// @brief 强类型整数包装器
/// @tparam Tag   幽灵标签，用于区分不同类型
/// @tparam Rep   底层存储类型
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    using ValueType = Rep;

    // 构造
    constexpr explicit StrongInt(Rep value = Rep{}) : value_(value) {}

    // 获取底层值
    constexpr Rep get() const noexcept { return value_; }

    // 自增/自减
    constexpr StrongInt& operator++() noexcept { ++value_; return *this; }
    constexpr StrongInt operator++(int) noexcept {
        StrongInt tmp = *this;
        ++value_;
        return tmp;
    }
    constexpr StrongInt& operator--() noexcept { --value_; return *this; }
    constexpr StrongInt operator--(int) noexcept {
        StrongInt tmp = *this;
        --value_;
        return tmp;
    }

    // 复合赋值（同类型）
    constexpr StrongInt& operator+=(const StrongInt& other) noexcept {
        value_ += other.value_;
        return *this;
    }
    constexpr StrongInt& operator-=(const StrongInt& other) noexcept {
        value_ -= other.value_;
        return *this;
    }

    // 算术运算（同类型）
    constexpr StrongInt operator+(const StrongInt& other) const noexcept {
        return StrongInt(value_ + other.value_);
    }
    constexpr StrongInt operator-(const StrongInt& other) const noexcept {
        return StrongInt(value_ - other.value_);
    }

    // 比较运算
    constexpr bool operator==(const StrongInt& other) const noexcept {
        return value_ == other.value_;
    }
    constexpr bool operator!=(const StrongInt& other) const noexcept {
        return value_ != other.value_;
    }
    constexpr bool operator<(const StrongInt& other) const noexcept {
        return value_ < other.value_;
    }
    constexpr bool operator<=(const StrongInt& other) const noexcept {
        return value_ <= other.value_;
    }
    constexpr bool operator>(const StrongInt& other) const noexcept {
        return value_ > other.value_;
    }
    constexpr bool operator>=(const StrongInt& other) const noexcept {
        return value_ >= other.value_;
    }

private:
    Rep value_;
};

// 流输出（方便调试）
template <typename Tag, typename Rep>
std::ostream& operator<<(std::ostream& os, const StrongInt<Tag, Rep>& v)
{
    os << v.get();
    return os;
}
```

写完咱们盘一下，`StrongInt` 模板覆盖了日常使用里最常见的需求，构造、取值、加减、比较、流输出这些日常操作都有了。所有运算要求的操作数都是**同一种 StrongInt 特化**，您也没法把 `Width` 和 `Height` 相加，因为它们的 `Tag` 不同，编译器直接就拒绝了。

## 第四步——类型安全的单位系统

接下来咱们拿强类型包装器搭一个类型安全的物理单位系统，这也是强类型 typedef 最经典的应用场景之一：靠的就是类型系统，防止不同物理量的值被混用。

```cpp
// 标签定义
struct MetersTag {};
struct KilometersTag {};
struct CelsiusTag {};
struct FahrenheitTag {};
struct SecondsTag {};
struct MillisecondsTag {};

// 类型别名
using Meters        = StrongInt<MetersTag, double>;
using Kilometers    = StrongInt<KilometersTag, double>;
using Celsius       = StrongInt<CelsiusTag, double>;
using Fahrenheit    = StrongInt<FahrenheitTag, double>;
using Seconds       = StrongInt<SecondsTag, double>;
using Milliseconds  = StrongInt<MillisecondsTag, int64_t>;

// 单位转换函数
constexpr Kilometers to_kilometers(Meters m) noexcept
{
    return Kilometers(m.get() / 1000.0);
}

constexpr Meters to_meters(Kilometers km) noexcept
{
    return Meters(km.get() * 1000.0);
}

constexpr Milliseconds to_milliseconds(Seconds s) noexcept
{
    return Milliseconds(static_cast<int64_t>(s.get() * 1000.0));
}
```

咱们用起来看看：

```cpp
Meters distance(5000.0);
Kilometers km = to_kilometers(distance);
// km = distance;  // 编译错误！不能直接赋值

Seconds duration(2.5);
Milliseconds ms = to_milliseconds(duration);
// auto bad = distance + duration;  // 编译错误！Meters 和 Seconds 不能相加
```

咱们这就见识到了类型安全单位系统的威力：编译期的时候，编译器就替您拦下了所有“物理量不匹配”的错误。您不可能不小心把米和秒加在一起，当然也不可能把摄氏度当华氏度用。

当然，咱们刚搭的单位系统还是简化版，真正的物理单位系统还要多管几样东西，比如复合单位（速度 = 距离 / 时间）的合成，后面讲到用户自定义字面量的时候，会把单位系统真的搭起来。不过思路没有变：用 phantom type 在编译期区分不同的物理量。

## 第五步——避免参数混淆的实战案例

强类型能干的当然不止物理单位，咱们再看一个常见的场景，业务系统里的 ID 类型到处都是。

```cpp
struct UserIdTag {};
struct OrderIdTag {};
struct ProductIdTag {};

using UserId    = StrongInt<UserIdTag, uint64_t>;
using OrderId   = StrongInt<OrderIdTag, uint64_t>;
using ProductId = StrongInt<ProductIdTag, uint64_t>;

class OrderService {
public:
    OrderId create_order(UserId user, ProductId product, int quantity)
    {
        // 如果参数写反了，编译器会直接报错
        return OrderId(next_id_++);
    }

    void cancel_order(OrderId id)
    {
        // 只接受 OrderId，不接受 UserId 或 ProductId
    }

private:
    uint64_t next_id_ = 1;
};
```

```cpp
OrderService service;
UserId user(42);
ProductId product(100);
OrderId order(1);

service.create_order(user, product, 3);  // OK
// service.create_order(product, user, 3);  // 编译错误！
// service.cancel_order(user);              // 编译错误！UserId 不是 OrderId
```

大型项目的数据库表里，主键、外键、各种关联的 ID 全都是 `uint64_t`。少了强类型区分，调用方很容易把 `user_id` 传进 `order_id` 的位置。笔者就见过这么一回：两个 ID 在调用里换了位置，review 的时候也没人看出来，生产数据库照着执行了错误的删除。事后修复花的成本，比当初就引入强类型的成本高得多。

## 第六步——C++17 CTAD 简化使用

C++17 引入了类模板参数推导，大家一般叫它 CTAD（Class Template Argument Deduction），本意是把显式指定模板参数的麻烦省掉。可咱们手上的 `StrongInt` 偏偏有两个模板参数（`Tag` 和 `Rep`），构造的时候只递一个 `Rep` 值进去，实参里看不见 `Tag` 的存在，编译器当然没得推导。咱们要是写一条推导指引硬教编译器，推导出来的类型连初始化都过不去，指引在这里反而帮不上忙。真正的解法是让构造函数带上一个 tag 形参：

```cpp
// 让构造函数带上一个 tag 形参（其余成员与第三步的一致）
template <typename Tag, typename Rep = int>
class StrongInt {
public:
    constexpr explicit StrongInt(Tag*, Rep value) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }
};

// 构造的时候把 tag 一起递进去
struct ScoreTag {};
using Score = StrongInt<ScoreTag, int>;

Score s((ScoreTag*)nullptr, 100);                 // 走别名构造，Tag 在编译期对上号
StrongInt auto_deduced((ScoreTag*)nullptr, 100);  // 不写别名，CTAD 推出 StrongInt<ScoreTag, int>
```

tag 形参的路子能走通，只是 `(ScoreTag*)nullptr` 一遍遍地写下来，确实够啰嗦的。对咱们更省心的办法是靠 `auto` 推导，配一个 `make_strong` 这样的工厂函数，模板代码写起来自然多了：

```cpp
template <typename Tag, typename Rep>
constexpr auto make_strong(Rep value)
{
    return StrongInt<Tag, Rep>(value);
}

// 使用
auto width = make_strong<WidthTag>(100);
// width 的类型是 StrongInt<WidthTag, int>，自动推导
```

> 这里的 `StrongInt<Tag, Rep>(value)` 用的是第三步那个单参构造：进了工厂函数，`Tag` 在调用点就是显式给出来的模板实参，用不着再往构造函数的参数表里塞。本节新添的 tag 形参构造，是专门留给裸 CTAD 的，两条路各管各的，咱们在类里把两个构造并存着用。

## 嵌入式实战——寄存器地址的类型安全

咱们把视角挪到嵌入式开发里：外设寄存器的地址通常就是个裸 `uint32_t`。不同外设的地址要是不小心混在一起，后果可能是写错了寄存器，硬件的行为跟着出异常。强类型在这儿倒是同样派得上用场：

```cpp
struct GpioRegTag {};
struct UartRegTag {};
struct SpiRegTag {};

using GpioRegAddr = StrongInt<GpioRegTag, uint32_t>;
using UartRegAddr = StrongInt<UartRegTag, uint32_t>;
using SpiRegAddr  = StrongInt<SpiRegTag, uint32_t>;

void gpio_write(GpioRegAddr addr, uint32_t value);
void uart_write(UartRegAddr addr, uint32_t value);

// gpio_write(UartRegAddr(0x40001000), 42);  // 编译错误！类型不匹配
```

项目一大它就更值钱了。您的芯片有几十个外设、几百个寄存器地址的时候，类型安全的地址系统能防止您写错寄存器。真到了运行时，`StrongInt` 的 `get()` 会被内联，生成的代码和直接用 `uint32_t` 完全一样。

## 已有库推荐

您要是不想自己维护一套强类型框架，社区里有几个成熟的开源库可以考虑。Jonathan Boccara 的 [NamedType](https://github.com/joboccara/NamedType) 是最知名的一个，它把运算符继承、函数式操作、哈希、流输出全都包圆了，可以说是非常全面了。foonathan 的 [type_safe](https://github.com/foonathan/type_safe) 里也有 strong_typedef，参考资源里的第一篇文章就是他写的。

不过笔者的建议是：只为区分不同语义的同类型参数的话，手写一个简单的 `StrongInt` 模板就够了。咱们写下来的代码不到一百行，控制权完全在咱们手里，也不需要引入外部的依赖。等真需要运算符继承、隐式转换策略定制之类更复杂的特性时，咱们再引入第三方库也不迟。

## 参考资源

- [foonathan.net: Emulating strong/opaque typedefs in C++](https://www.foonathan.net/2016/10/strong-typedefs/)
- [Fluent C++: Strong types by struct](https://www.fluentcpp.com/2018/04/06/strong-types-by-struct/)
- [NamedType (GitHub)](https://github.com/joboccara/NamedType)
- [C++ Core Guidelines: Type safety](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#prosafety-type-safety-profile)
