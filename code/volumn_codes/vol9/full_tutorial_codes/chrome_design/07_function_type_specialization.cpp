// 函数类型与模板偏特化
// 来源：OnceCallback 前置知识（一）(pre-01)
// 编译：g++ -std=c++17 -Wall -Wextra 07_function_type_specialization.cpp -o 07_function_type_specialization

#include <type_traits>
#include <tuple>
#include <string>
#include <iostream>

// --- 函数类型验证 ---

static_assert(std::is_function_v<int(int, int)>);
static_assert(!std::is_pointer_v<int(int, int)>);
static_assert(std::is_pointer_v<int(*)(int, int)>);

// --- 主模板 + 偏特化模式 ---

// 主模板：只有声明，没有定义
template<typename T>
struct FuncTraits;

// 偏特化：拆解函数类型 R(Args...)
template<typename R, typename... Args>
struct FuncTraits<R(Args...)> {
    using ReturnType = R;
    using ArgsTuple = std::tuple<Args...>;
    static constexpr std::size_t kArity = sizeof...(Args);
};

// --- std::function 风格的简化实现 ---

template<typename> class SimpleFunction;  // 主模板

template<typename R, typename... Args>
class SimpleFunction<R(Args...)> {
    // 偏特化：实际的实现在这里
public:
    using Signature = R(Args...);
    using ReturnType = R;
    static constexpr std::size_t kArity = sizeof...(Args);
};

int main() {
    std::cout << "=== 函数类型 static_assert ===\n";
    std::cout << "  int(int, int) is a function type\n";
    std::cout << "  int(int, int) is NOT a pointer\n";
    std::cout << "  int(*)(int, int) IS a pointer\n";

    std::cout << "\n=== FuncTraits 验证 ===\n";
    static_assert(std::is_same_v<FuncTraits<int(double, char)>::ReturnType, int>);
    static_assert(std::is_same_v<FuncTraits<void()>::ReturnType, void>);
    static_assert(FuncTraits<int(int, int, int)>::kArity == 3);

    std::cout << "  FuncTraits<int(double, char)>::ReturnType == int\n";
    std::cout << "  FuncTraits<void()>::ReturnType == void\n";
    std::cout << "  FuncTraits<int(int, int, int)>::kArity == 3\n";

    std::cout << "\n=== 更复杂的类型验证 ===\n";
    static_assert(std::is_same_v<
        FuncTraits<std::string(const std::string&, int)>::ReturnType,
        std::string>);
    static_assert(std::is_same_v<
        FuncTraits<void(int&&)>::ArgsTuple,
        std::tuple<int&&>>);

    std::cout << "  FuncTraits<std::string(const std::string&, int)>::ReturnType == std::string\n";
    std::cout << "  FuncTraits<void(int&&)>::ArgsTuple == std::tuple<int&&>\n";

    std::cout << "\n=== SimpleFunction 偏特化模式 ===\n";
    static_assert(SimpleFunction<int(int, int)>::kArity == 2);
    static_assert(std::is_same_v<SimpleFunction<double()>::ReturnType, double>);
    std::cout << "  SimpleFunction<int(int, int)>::kArity == 2\n";
    std::cout << "  SimpleFunction<double()>::ReturnType == double\n";

    return 0;
}
