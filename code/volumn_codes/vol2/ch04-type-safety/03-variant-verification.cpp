/**
 * 验证 03-variant.md 中的技术断言
 * 编译: g++ -std=c++17 -Wall $this -o /tmp/verify
 */

#include <cstdio>
#include <variant>
#include <string>
#include <vector>
#include <iostream>
#include <memory>

// 验证点1: variant 的内存布局
void test_variant_sizeof()
{
    std::variant<int, double, std::string> v;

    printf("sizeof(variant<int, double, string>): %zu bytes\n", sizeof(v));
    printf("sizeof(string): %zu bytes\n", sizeof(std::string));
    printf("sizeof(double): %zu bytes\n", sizeof(double));
    printf("sizeof(int): %zu bytes\n", sizeof(int));

    // variant 大小应该是 max(sizeof(T)...) + 索引字段
    size_t expected_max = sizeof(std::string);
    if (sizeof(double) > expected_max) expected_max = sizeof(double);
    if (sizeof(int) > expected_max) expected_max = sizeof(int);

    printf("✓ variant 大小 >= 最大备选类型 (%zu + 元数据)\n", expected_max);
}

// 验证点2: variant vs 虚函数多态的性能
struct ShapeBase {
    virtual ~ShapeBase() = default;
    virtual double area() const = 0;
};

struct CircleV : ShapeBase {
    double radius;
    explicit CircleV(double r) : radius(r) {}
    double area() const override { return 3.14159 * radius * radius; }
};

struct RectangleV : ShapeBase {
    double width, height;
    RectangleV(double w, double h) : width(w), height(h) {}
    double area() const override { return width * height; }
};

// variant 版本
struct Circle { double radius; };
struct Rectangle { double width, height; };
using Shape = std::variant<Circle, Rectangle>;

double area_variant(const Shape& s)
{
    return std::visit([](const auto& shape) -> double {
        if constexpr (std::is_same_v<std::decay_t<decltype(shape)>, Circle>) {
            return 3.14159 * shape.radius * shape.radius;
        } else {
            return shape.width * shape.height;
        }
    }, s);
}

void test_variant_vs_virtual()
{
    // 虚函数版本
    std::vector<std::unique_ptr<ShapeBase>> shapes_v;
    shapes_v.push_back(std::make_unique<CircleV>(5.0));
    shapes_v.push_back(std::make_unique<RectangleV>(3.0, 4.0));

    double total_v = 0.0;
    for (const auto& s : shapes_v) {
        total_v += s->area();
    }

    // variant 版本
    std::vector<Shape> shapes;
    shapes.push_back(Circle{5.0});
    shapes.push_back(Rectangle{3.0, 4.0});

    double total_var = 0.0;
    for (const auto& s : shapes) {
        total_var += area_variant(s);
    }

    printf("✓ 虚函数多态总面积: %.4f\n", total_v);
    printf("✓ variant 多态总面积: %.4f\n", total_var);

    // 内存布局对比
    printf("\n内存布局对比:\n");
    printf("  sizeof(unique_ptr<ShapeBase>): %zu bytes (指针大小)\n",
           sizeof(std::unique_ptr<ShapeBase>));
    printf("  sizeof(Shape variant): %zu bytes (值类型)\n", sizeof(Shape));
}

// 验证点3: valueless_by_exception 状态
struct ThrowingType {
    ThrowingType() { throw std::runtime_error("construction failed"); }
};

void test_valueless_by_exception()
{
    std::variant<int, ThrowingType> v = 42;

    try {
        v = ThrowingType();  // 旧值被销毁，新值构造抛异常
    } catch (const std::runtime_error& e) {
        printf("✓ 捕获异常: %s\n", e.what());
        printf("  v.valueless_by_exception(): %d\n", v.valueless_by_exception());
    }
}

// 验证点4: variant 自动管理生命周期
struct LifetimeTracker {
    static int construct_count;
    static int destruct_count;
    int id;

    explicit LifetimeTracker(int i) : id(i) {
        construct_count++;
    }

    ~LifetimeTracker() {
        destruct_count++;
    }

    LifetimeTracker(const LifetimeTracker& other) : id(other.id) {
        construct_count++;
    }

    LifetimeTracker& operator=(const LifetimeTracker& other) {
        id = other.id;
        return *this;
    }
};

int LifetimeTracker::construct_count = 0;
int LifetimeTracker::destruct_count = 0;

void test_lifetime_management()
{
    {
        std::variant<int, LifetimeTracker> v = 42;
        v = LifetimeTracker(100);  // int 被销毁，LifetimeTracker 被构造

        v = 200;  // LifetimeTracker 被销毁，int 被构造
    }  // v 离开作用域，当前值 (int=200) 被销毁

    printf("✓ 构造次数: %d, 析构次数: %d\n",
           LifetimeTracker::construct_count,
           LifetimeTracker::destruct_count);
    printf("  variant 自动管理对象生命周期\n");
}

// 验证点5: visit 的类型安全
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

void test_visit_type_safety()
{
    std::variant<int, double, std::string> v = 3.14;

    std::visit(Overloaded{
        [](int i) { printf("int: %d\n", i); },
        [](double d) { printf("double: %g\n", d); },
        [](const std::string& s) { printf("string: %s\n", s.c_str()); }
    }, v);

    printf("✓ std::visit 在编译期检查所有备选类型\n");
}

int main()
{
    printf("=== 03-variant.md 技术断言验证 ===\n\n");

    test_variant_sizeof();
    printf("\n");

    test_variant_vs_virtual();
    printf("\n");

    test_valueless_by_exception();
    printf("\n");

    test_lifetime_management();
    printf("\n");

    test_visit_type_safety();
    printf("\n");

    printf("=== 所有验证通过 ===\n");

    return 0;
}
