// Test: 智能指针 CTAD 验证
// 验证 unique_ptr 和 shared_ptr 是否支持 CTAD

#include <memory>
#include <iostream>

void test_unique_ptr_ctad_fails() {
    // 这个应该编译失败：unique_ptr 不支持从 new 表达式 CTAD
    // std::unique_ptr up(new int(42));

    std::cout << "unique_ptr CTAD from 'new' does NOT work\n";
}

void test_shared_ptr_ctad_fails() {
    // 这个应该编译失败：shared_ptr 不支持从 new 表达式 CTAD
    // std::shared_ptr sp(new int(42));

    std::cout << "shared_ptr CTAD from 'new' does NOT work\n";
}

void test_make_functions_work() {
    // 正确的做法是使用 make 函数
    auto up = std::make_unique<int>(42);
    auto sp = std::make_shared<int>(42);

    std::cout << "make_unique/make_shared work correctly\n";
}

int main() {
    test_make_functions_work();
    return 0;
}
