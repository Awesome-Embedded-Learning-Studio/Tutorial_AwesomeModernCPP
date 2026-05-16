/*
 * 验证：std::span 基本用法和隐式转换（C++20）
 *
 * 背景：文章展示了 std::span 替代裸指针+长度的用法，
 *       包括从 vector、C 数组隐式转换，以及 subspan 取子范围。
 *       注意：使用 uint8_t 需要包含 <cstdint>。
 *
 * 预期结果：
 *   vector 直接传 span → "收到 5 字节数据\n1 2 3 4 5"
 *   subspan(1, 3) → "收到 3 字节数据\n2 3 4"
 *   C 数组 → "收到 3 字节数据\n10 20 30"
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -o /tmp/05-08-span-demo 05-08-span-demo.cpp
 *
 * 运行：
 *   /tmp/05-08-span-demo
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/container/span
 *
 * 编译器：GCC 16.1.1
 */

#include <span>
#include <vector>
#include <cstdint>
#include <iostream>

void process(std::span<const uint8_t> data) {
    std::cout << "收到 " << data.size() << " 字节数据\n";
    for (size_t i = 0; i < data.size(); ++i) {
        std::cout << static_cast<int>(data[i]) << " ";
    }
    std::cout << "\n";
}

int main() {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 5};

    process(vec);

    process(std::span<uint8_t>(vec).subspan(1, 3));

    uint8_t arr[] = {10, 20, 30};
    process(arr);

    return 0;
}
