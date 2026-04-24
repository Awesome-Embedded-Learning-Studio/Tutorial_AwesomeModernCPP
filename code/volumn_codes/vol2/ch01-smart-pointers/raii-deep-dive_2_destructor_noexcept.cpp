// GCC 13, -O2 -std=c++11
#include <iostream>
#include <type_traits>
#include <stdexcept>

/// @brief 验证 C++11 起析构函数默认为 noexcept(true)
struct TestDestructor {
    ~TestDestructor() {
        std::cout << "Destructor called\n";
    }
};

/// @brief 验证析构函数中抛出异常会调用 std::terminate()
struct BadDestructor {
    ~BadDestructor() noexcept(false) {  // 显式指定为非 noexcept
        throw std::runtime_error("Exception from destructor");
    }
};

/// @brief 验证隐式声明的析构函数默认为 noexcept
struct BadDestructorImplicit {
    ~BadDestructorImplicit() {  // 隐式 noexcept，抛出会警告
        throw std::runtime_error("Exception from implicit destructor");
    }
};

int main() {
    std::cout << "C++ Standard: " << __cplusplus << "\n";
    std::cout << "Is TestDestructor noexcept? "
              << std::is_nothrow_destructible<TestDestructor>::value << "\n";
    std::cout << "Is BadDestructor noexcept? "
              << std::is_nothrow_destructible<BadDestructor>::value << "\n";
    std::cout << "Is BadDestructorImplicit noexcept? "
              << std::is_nothrow_destructible<BadDestructorImplicit>::value << "\n";

    std::cout << "\n=== Testing exception escaping destructor ===\n";
    try {
        BadDestructor bad;
        throw std::runtime_error("Primary exception");
    } catch (const std::exception& e) {
        std::cout << "Caught: " << e.what() << "\n";
    }

    return 0;
}

// 编译警告：
// raii-deep-dive_2_destructor_noexcept.cpp:25:13: warning: 'throw' will always call 'terminate' [-Wterminate]
//    25 |             throw std::runtime_error("Exception from implicit destructor");
//       |             ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// raii-deep-dive_2_destructor_noexcept.cpp:25:13: note: in C++11 destructors default to 'noexcept'
//
// 运行输出：
// C++ Standard: 201103
// Is TestDestructor noexcept? 1
// Is BadDestructor noexcept? 0
// Is BadDestructorImplicit noexcept? 1
//
// === Testing exception escaping destructor ===
// terminate called after throwing an instance of 'std::runtime_error'
//   what():  Exception from destructor
