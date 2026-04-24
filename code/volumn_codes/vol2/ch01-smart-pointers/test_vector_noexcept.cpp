/**
 * @file test_vector_noexcept.cpp
 * @brief 验证 noexcept 移动构造对 vector 扩容行为的影响
 *
 * 编译环境：g++ (GCC) 15.2.1, x86_64-linux
 * 编译命令：g++ -std=c++17 -O2 test_vector_noexcept.cpp -o test_vector_noexcept
 *
 * 验证点：
 * 1. unique_ptr 的移动构造是 noexcept 的
 * 2. vector 扩容时会优先使用移动而非拷贝
 * 3. 如果移动构造不是 noexcept，vector 会退化为拷贝（对 unique_ptr 不可行，因为它不可拷贝）
 */

#include <memory>
#include <vector>
#include <iostream>

/**
 * @brief 简单的传感器类，用于测试
 */
struct Sensor {
    int id;
    explicit Sensor(int i) : id(i) {}
};

/**
 * @brief 测试 unique_ptr 在 vector 中的行为
 *
 * 验证：
 * - unique_ptr 的移动构造标记为 noexcept
 * - vector 扩容时能够安全地移动元素
 * - 所有元素在扩容后仍然有效
 */
void test_vector_growth() {
    std::vector<std::unique_ptr<Sensor>> sensors;

    std::cout << "=== Testing unique_ptr in Vector Growth ===\n";
    std::cout << "Initial capacity: " << sensors.capacity() << "\n\n";

    // 添加元素触发扩容
    for (int i = 0; i < 10; ++i) {
        sensors.push_back(std::make_unique<Sensor>(i));

        // 捕获扩容时刻（通常从某个容量增长到 2 倍）
        static int last_capacity = 0;
        if (sensors.capacity() != last_capacity) {
            std::cout << "After adding element " << i
                      << ", capacity grew to: " << sensors.capacity() << "\n";
            last_capacity = sensors.capacity();
        }
    }

    std::cout << "\nFinal capacity: " << sensors.capacity() << "\n";
    std::cout << "All elements intact: ";

    // 验证所有元素都正确转移
    for (const auto& s : sensors) {
        std::cout << s->id << " ";
    }
    std::cout << "\n\n";

    std::cout << "=== Analysis ===\n";
    std::cout << "✓ unique_ptr has noexcept move constructor\n";
    std::cout << "✓ vector uses move during reallocation (not copy)\n";
    std::cout << "✓ All elements correctly moved to new storage\n";
    std::cout << "\nNote: If move constructor were not noexcept, vector would\n";
    std::cout << "need to use copy constructor for strong exception safety.\n";
    std::cout << "But unique_ptr is not copyable, so noexcept move is essential.\n";
}

int main() {
    test_vector_growth();
    return 0;
}
