// 验证代码：C++20 标准库的 nodiscard 扩展
// 验证哪些类型/函数确实标记了 nodiscard

#include <vector>
#include <string>
#include <memory>
#include <cstdio>

int main() {
    // 测试 vector::empty()
    std::vector<int> vec = {1, 2, 3};
    vec.empty();  // 应该警告

    // 测试 string::empty()
    std::string str = "hello";
    str.empty();  // 应该警告

    // 测试 unique_ptr::get() - 这个不应该警告
    auto ptr = std::make_unique<int>(42);
    ptr.get();  // 不应该警告

    // 测试 std::make_unique - 这个应该警告？
    std::make_unique<int>(100);

    // 测试 std::unique_ptr 构造 - 这个应该警告？
    std::unique_ptr<int>(new int(200));

    return 0;
}

/*
编译命令（GCC 15.2.1, libstdc++）：
g++ -std=c++20 test_std_nodiscard.cpp -o test_std_nodiscard

测试结果：
✓ vec.empty() - 警告：ignoring return value, declared with attribute 'nodiscard'
✓ str.empty() - 警告：ignoring return value, declared with attribute 'nodiscard'
✓ ptr.get() - 无警告（正确，get() 不应该是 nodiscard）
✗ std::make_unique<int>(100) - 无警告！
✗ std::unique_ptr<int>(new int(200)) - 无警告！

结论：
文章中声称 "std::unique_ptr 和 std::shared_ptr（C++20 起）"
标记了 [[nodiscard]] 是不准确的。

实际情况：
1. empty() 方法确实标记了 [[nodiscard]] ✓
2. unique_ptr/shared_ptr 类型本身在 libstdc++ 15.2.1 中
   并没有标记 [[nodiscard]]

可能的原因：
1. 文章可能基于其他实现（如 libc++ 或 MSVC STL）
2. 或者是 C++ 标准的要求，但 libstdc++ 尚未实现
3. 或者是对 C++20/23 标准的误解

需要修正：文章中的这一断言需要更准确地说明是哪些
具体函数或构造函数标记了 [[nodiscard]]。
*/
