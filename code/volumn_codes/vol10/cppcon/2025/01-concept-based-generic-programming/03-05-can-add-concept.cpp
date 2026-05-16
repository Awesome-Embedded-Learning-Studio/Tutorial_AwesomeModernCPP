/*
 * 验证：can_add concept 的修正版本
 *
 * 背景：原文初稿使用默认模板参数推导返回类型
 *   typename R = std::remove_cvref_t<decltype(std::declval<A>() + std::declval<B>())>
 *       当 A+B 不合法时会导致硬编译错误。
 *       修正版改用 std::common_type_t<A,B> 作为约束目标。
 *
 * 预期结果：所有 static_assert 通过，包括 !can_add<int, std::string>
 *
 * 编译命令：
 *   g++ -std=c++20 -o /tmp/03-05-can-add-concept 03-05-can-add-concept.cpp
 *
 * 运行：
 *   /tmp/03-05-can-add-concept
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/language/constraints
 *
 * 编译器：GCC 16.1.1
 */

#include <concepts>
#include <string>
#include <iostream>

// 修正版：不使用默认模板参数推导返回类型
template<typename A, typename B>
concept can_add = requires(A a, B b) {
    { a + b } -> std::convertible_to<std::common_type_t<A, B>>;
};

// 内置类型加法
static_assert(can_add<int, int>);
static_assert(can_add<int, double>);
static_assert(can_add<std::string, std::string>);

// 关键：int + string 应该返回 false，不是硬错误
static_assert(!can_add<int, std::string>);

// 自定义类型：成员函数实现 operator+
class MyInt {
    int val;
public:
    MyInt(int v) : val(v) {}
    MyInt operator+(const MyInt& other) const {
        return MyInt(val + other.val);
    }
};
static_assert(can_add<MyInt, MyInt>);

// 自定义类型：自由函数实现 operator+
class MyFloat {
    float val;
public:
    MyFloat(float v) : val(v) {}
    float get() const { return val; }
};
MyFloat operator+(const MyFloat& a, const MyFloat& b) {
    return MyFloat(a.get() + b.get());
}
static_assert(can_add<MyFloat, MyFloat>);

int main() {
    std::cout << "All static_asserts passed!\n";
    return 0;
}
