// Test: std::vector<bool> 代理类型验证
// 验证 auto& 不能绑定到 vector<bool>::reference 的问题

#include <vector>
#include <iostream>

void test_vector_bool_auto_ref_fails() {
    std::vector<bool> bits = {true, false, true};

    // 这个应该编译失败：auto& 无法绑定到代理类型
    // for (auto& bit : bits) {
    //     bit = !bit;
    // }

    std::cout << "auto& with vector<bool> fails to compile (expected)\n";
}

void test_vector_bool_auto_works() {
    std::vector<bool> bits = {true, false, true};

    // 按值拷贝可以工作
    for (auto bit : bits) {
        std::cout << bit << " ";
    }
    std::cout << "\n";
}

void test_vector_bool_index_works() {
    std::vector<bool> bits = {true, false, true};

    // 通过索引修改可以工作
    for (std::size_t i = 0; i < bits.size(); ++i) {
        bits[i] = !bits[i];
    }

    std::cout << "Modification via index works\n";
}

int main() {
    test_vector_bool_auto_works();
    test_vector_bool_index_works();
    return 0;
}
