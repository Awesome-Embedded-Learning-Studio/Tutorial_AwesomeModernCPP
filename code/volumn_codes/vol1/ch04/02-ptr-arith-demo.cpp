#include <cstddef>
#include <iostream>

int main()
{
    // --- 1. 多种方式遍历数组 ---
    int data[6] = {5, 12, 7, 23, 18, 9};

    std::cout << "=== 指针遍历 ===\n";
    for (int* p = data; p != data + 6; ++p) {
        std::cout << *p << " ";
    }
    std::cout << "\n";

    // --- 2. 指针减法计算元素距离 ---
    int* first = &data[0];
    int* last  = &data[5];
    std::cout << "\n=== 指针距离 ===\n";
    std::cout << "first 和 last 之间隔了 "
              << (last - first) << " 个元素\n";

    // 用指针减法找到某个值的下标
    int target = 23;
    for (int* p = data; p != data + 6; ++p) {
        if (*p == target) {
            std::cout << "值 " << target << " 的下标是: "
                      << (p - data) << "\n";
            break;
        }
    }

    // --- 3. 用指针实现 strlen ---
    const char* msg = "pointer";
    const char* scan = msg;
    while (*scan != '\0') {
        ++scan;
    }
    std::cout << "\n=== 手写 strlen ===\n";
    std::cout << "\"" << msg << "\" 的长度: "
              << (scan - msg) << "\n";

    // --- 4. 用指针反转数组 ---
    std::cout << "\n=== 反转数组 ===\n";
    std::cout << "反转前: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    int* left  = data;
    int* right = data + 5;
    while (left < right) {
        int temp = *left;
        *left  = *right;
        *right = temp;
        ++left;
        --right;
    }

    std::cout << "反转后: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    return 0;
}
