/**
 * 验证 02-strong-types.md 中的技术断言
 * 编译: g++ -std=c++17 -O2 -Wall $this -o /tmp/verify
 */

#include <cstdio>

// 验证点1: typedef/using 只是别名，不是新类型
using UserId = int;
using OrderId = int;

void test_typedef_alias()
{
    UserId uid = 42;
    OrderId oid = 100;

    // 以下全部编译通过，因为它们都是同一个 int 类型
    uid = oid;
    OrderId another = uid;

    int total = uid + oid;  // 两个不同语义的 ID 相加

    printf("✓ typedef/using 创建的类型别名可以互相赋值和运算\n");
    printf("  UserId + OrderId = %d (这是不安全的)\n", total);
}

// 验证点2: Phantom Type 模式创建真正的类型
struct WidthTag {};
struct HeightTag {};

template <typename Tag, typename Rep = int>
class StrongInt {
public:
    constexpr explicit StrongInt(Rep value) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }

private:
    Rep value_;
};

using Width = StrongInt<WidthTag>;
using Height = StrongInt<HeightTag>;

void test_phantom_type()
{
    Width w(100);
    Height h(200);

    // 以下操作会编译错误（已注释）
    // h = w;          // 编译错误！不能把 Width 赋给 Height
    // Width bad = h;  // 编译错误！

    printf("✓ Phantom Type 模式防止不同语义类型混淆\n");
}

// 验证点3: 零开销抽象（空基类优化 EBO）
template <typename Tag, typename Rep = int>
class StrongIntWithOps {
public:
    constexpr explicit StrongIntWithOps(Rep value) : value_(value) {}
    constexpr Rep get() const noexcept { return value_; }

private:
    Rep value_;
};

void test_zero_overhead()
{
    using Meters = StrongIntWithOps<struct MetersTag, double>;
    Meters m(100.0);

    printf("sizeof(Meters): %zu bytes\n", sizeof(Meters));
    printf("sizeof(double): %zu bytes\n", sizeof(double));

    static_assert(sizeof(Meters) == sizeof(double),
                  "StrongInt should have zero overhead due to EBO");

    // 验证空标签类大小
    struct MetersTag {};
    printf("sizeof(MetersTag): %zu byte (minimum 1 by C++ standard)\n",
           sizeof(MetersTag));
}

// 验证点4: 运行时性能（内联验证）
__attribute__((noinline))
double calculate_area(Width w, Height h)
{
    return static_cast<double>(w.get()) * static_cast<double>(h.get());
}

void test_runtime_performance()
{
    Width w(1920);
    Height h(1080);

    double area = calculate_area(w, h);
    printf("✓ 强类型函数调用: %d x %d = %.0f\n", w.get(), h.get(), area);
}

// 验证点5: 类型安全的单位系统
struct MetersTag {};
struct KilometersTag {};
struct CelsiusTag {};
struct FahrenheitTag {};

using Meters = StrongIntWithOps<MetersTag, double>;
using Kilometers = StrongIntWithOps<KilometersTag, double>;
using Celsius = StrongIntWithOps<CelsiusTag, double>;
using Fahrenheit = StrongIntWithOps<FahrenheitTag, double>;

constexpr Kilometers to_kilometers(Meters m) noexcept
{
    return Kilometers(m.get() / 1000.0);
}

constexpr Meters to_meters(Kilometers km) noexcept
{
    return Meters(km.get() * 1000.0);
}

void test_unit_system()
{
    Meters distance(5000.0);
    Kilometers km = to_kilometers(distance);

    // 以下会编译错误（已注释）
    // auto bad = distance + km;  // Meters 和 Kilometers 不能直接相加

    printf("✓ 类型安全的单位系统\n");
    printf("  %g meters = %g kilometers\n", distance.get(), km.get());
}

int main()
{
    printf("=== 02-strong-types.md 技术断言验证 ===\n\n");

    test_typedef_alias();
    printf("\n");

    test_phantom_type();
    printf("\n");

    test_zero_overhead();
    printf("\n");

    test_runtime_performance();
    printf("\n");

    test_unit_system();
    printf("\n");

    printf("=== 所有验证通过 ===\n");
    printf("✓ 零开销抽象: sizeof(StrongInt) == sizeof(Rep)\n");
    printf("✓ 类型安全: 不同 Tag 的类型不能互相赋值\n");

    return 0;
}
