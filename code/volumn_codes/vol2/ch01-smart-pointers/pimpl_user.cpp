/**
 * @file pimpl_user.cpp
 * @brief PIMPL 模式的用户代码示例
 *
 * 编译命令：
 *   g++ -std=c++17 -c pimpl_widget.cpp -o pimpl_widget.o
 *   g++ -std=c++17 -c pimpl_user.cpp -o pimpl_user.o
 *   g++ -std=c++17 pimpl_widget.o pimpl_user.o -o test_pimpl
 *   ./test_pimpl
 *
 * 关键点：用户代码只需要包含 pimpl_widget.h，完全不需要知道 Widget::Impl 的定义
 */

#include "pimpl_widget.h"
#include <iostream>

int main() {
    std::cout << "=== PIMPL Pattern Demonstration ===\n\n";

    Widget w;
    w.do_something();
    w.do_something();

    std::cout << "\n=== Key Points ===\n";
    std::cout << "✓ Header file only contains forward declaration\n";
    std::cout << "✓ Implementation details hidden in .cpp file\n";
    std::cout << "✓ Modifying Impl doesn't require recompiling users\n";
    std::cout << "✓ unique_ptr manages incomplete type safely\n";
    std::cout << "\nNote: Destructor must be defined in .cpp where Impl is complete!\n";

    return 0;
}
