// 配套 01-if-constexpr.md「丢弃分支的边界:语法必须合法,依赖 T 的语义检查才跳过」
// 默认能编过(演示依赖 T 的操作在丢弃分支里不触发实例化)
// 加 -DSYNTAX_ERR 复现:非依赖的语法错误即使分支被丢弃也照样报错
// 编译运行:g++ -std=c++17 -Wall -Wextra discarded_boundary.cpp -o db && ./db
// 复现报错:g++ -std=c++17 -Wall -Wextra -DSYNTAX_ERR discarded_boundary.cpp -o db
#include <iostream>
#include <type_traits>

template <typename T> void only_for_floats(T x) {
    if constexpr (std::is_floating_point_v<T>) {
        // 传 int 时整个 if 分支被丢弃,这一行不实例化
        std::cout << "浮点:" << x << ",倒数 1/" << x << "\n";
    }
#ifdef SYNTAX_ERR
    if constexpr (sizeof(T) > 100) {
        T bogus = ; // 语法非法:即使 sizeof(T)>100 为假、分支被丢弃,语法仍要合法
    }
#endif
}

int main() {
    only_for_floats(3);   // int:分支被整个丢弃,什么都不打印
    only_for_floats(2.5); // double:走分支
}
