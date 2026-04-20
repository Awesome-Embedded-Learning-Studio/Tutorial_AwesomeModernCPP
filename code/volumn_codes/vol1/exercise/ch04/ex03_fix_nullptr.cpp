/**
 * @file ex03_fix_nullptr.cpp
 * @brief 练习：修复空指针 bug
 *
 * 展示两种常见指针 bug：
 *   1. 使用未初始化的野指针
 *   2. 返回局部变量的地址（悬空指针）
 * 然后给出修正版本。
 */

#include <iostream>

// ---- Bug 1: 未初始化的指针 ----
void demo_uninitialized_pointer() {
    std::cout << "Bug 1: 未初始化的指针\n";

    // 错误写法（已注释，避免实际运行崩溃）:
    // int* p;        // 野指针：指向随机地址
    // *p = 42;       // 未定义行为！

    // 正确写法：初始化为 nullptr，使用前检查
    int* p = nullptr;
    int value = 42;
    p = &value;

    if (p != nullptr) {
        std::cout << "  *p = " << *p << " (安全访问)\n";
    }
    std::cout << "\n";
}

// ---- Bug 2: 返回局部变量的地址 ----
// 错误写法：
int* buggy_get_address() {
    int local = 100;
    return &local;  // 警告：返回局部变量的地址！
}

// 正确写法 1：返回值而非地址
int correct_get_value() {
    int local = 100;
    return local;  // 按值返回，安全
}

// 正确写法 2：使用动态内存（调用者负责释放）
int* correct_get_heap() {
    return new int(100);
}

// 正确写法 3：使用输出参数
void correct_get_via_param(int* out) {
    *out = 100;
}

int main() {
    std::cout << "===== 修复空指针 bug =====\n\n";

    demo_uninitialized_pointer();

    std::cout << "Bug 2: 返回局部变量的地址\n";
    // 不调用 buggy_get_address()，行为未定义
    // int* bad = buggy_get_address();  // 悬空指针！

    // 正确做法演示
    int val = correct_get_value();
    std::cout << "  按值返回: " << val << "\n";

    int* heap_val = correct_get_heap();
    std::cout << "  动态内存: " << *heap_val << "\n";
    delete heap_val;

    int param_val = 0;
    correct_get_via_param(&param_val);
    std::cout << "  输出参数: " << param_val << "\n";

    std::cout << "\n要点总结:\n";
    std::cout << "  1. 指针声明时初始化为 nullptr\n";
    std::cout << "  2. 解引用前检查是否为空\n";
    std::cout << "  3. 永远不要返回局部变量的地址\n";

    return 0;
}
