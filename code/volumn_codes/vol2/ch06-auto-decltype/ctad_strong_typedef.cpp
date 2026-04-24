#include <cstdint>
#include <utility>

/// @brief 强类型包装器，防止不同语义的类型混用
template<typename T, typename Tag>
class StrongTypedef {
public:
    explicit constexpr StrongTypedef(T value) : value_(value) {}
    constexpr T& get() { return value_; }
    constexpr const T& get() const { return value_; }
private:
    T value_;
};

// 标签类型（空类，不占空间）
struct MeterTag {};
struct KilometerTag {};
struct CelsiusTag {};

// 别名
using Meter     = StrongTypedef<double, MeterTag>;
using Kilometer = StrongTypedef<double, KilometerTag>;
using Celsius   = StrongTypedef<double, CelsiusTag>;

// 自定义推导指引：字面量自动推导为对应类型
// （这个例子中其实不太需要，因为已经有 using 别名了）
// 但展示了语法
template<typename T>
StrongTypedef(T) -> StrongTypedef<T, struct GenericTag>;

// 使用
int main() {
    Meter distance(100.0);
    Celsius temp(23.5);

    // distance + temp 编译错误——不同的 Tag，不能混用
    // 这是强类型的核心价值
}
