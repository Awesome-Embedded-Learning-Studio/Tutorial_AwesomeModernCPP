// static_extent.cpp - 静态 vs 动态 extent 演示
#include <span>
#include <array>
#include <iostream>

void process_dynamic(std::span<int> s) {
    std::cout << "Dynamic extent span, size: " << s.size() << '\n';
    for (auto& v : s) {
        std::cout << v << ' ';
    }
    std::cout << '\n';
}

// 编译期固定大小 - 更强的类型安全
void process_static_4(std::span<int, 4> s) {
    std::cout << "Static extent(4) span, size: " << s.size() << '\n';
    for (auto& v : s) {
        std::cout << v << ' ';
    }
    std::cout << '\n';
}

int main() {
    int arr[4] = {1, 2, 3, 4};

    // 动态 extent - 可以接受任意大小
    std::span<int> s_dyn(arr);
    process_dynamic(s_dyn);

    // 静态 extent - 编译期检查大小
    std::span<int, 4> s_fixed(arr);
    process_static_4(s_fixed);

    // 编译期大小检查演示
    std::cout << "\nExtent values:\n";
    std::cout << "s_dyn.extent: " << s_dyn.extent << '\n';     // std::dynamic_extent
    std::cout << "s_fixed.extent: " << s_fixed.extent << '\n';  // 4

    // 类型安全演示
    int arr2[6] = {1, 2, 3, 4, 5, 6};
    std::span<int, 6> s_fixed6(arr2);

    // 以下代码会在编译期报错（取消注释以查看错误）:
    // process_static_4(s_fixed6);  // 错误：大小不匹配

    // 但可以用 first<N>() 获取静态大小的子视图
    auto first_4 = s_fixed6.first<4>();
    process_static_4(first_4);

    return 0;
}
