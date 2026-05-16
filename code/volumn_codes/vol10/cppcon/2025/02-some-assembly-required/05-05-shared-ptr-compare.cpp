/*
 * 验证：std::shared_ptr 与 Boost.shared_ptr 接口一致性
 *
 * 背景：文章声称 std::shared_ptr 的 API（use_count, make_shared, 拷贝语义）
 *       与 Boost 版本几乎一模一样，因为标准库版本源自 Boost.SmartPtr。
 *
 * 预期结果：use_count 在 make_shared 后为 1，拷贝后为 2
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -o /tmp/05-05-shared-ptr-compare 05-05-shared-ptr-compare.cpp
 *
 * 运行：
 *   /tmp/05-05-shared-ptr-compare
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/memory/shared_ptr
 *
 * 编译器：GCC 16.1.1
 */

#include <iostream>
#include <memory>

int main() {
    auto p1 = std::make_shared<int>(42);
    std::cout << "use_count: " << p1.use_count() << "\n";
    std::cout << "value: " << *p1 << "\n";

    auto p3 = p1;
    std::cout << "after copy, use_count: " << p1.use_count() << "\n";

    return 0;
}
