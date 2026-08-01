// 配套 02-variadic-templates.md「sizeof... 与模式展开」
// 演示 sizeof...(args) 数包大小,以及模式展开对异类型包逐元素独立调用
// 编译运行:g++ -std=c++17 -Wall -Wextra pack_sizeof_and_print.cpp -o psp && ./psp
#include <iostream>
#include <typeinfo>

// sizeof...(pack):编译期拿到包里元素个数,返回 size_t 常量
template <typename... Ts> constexpr std::size_t pack_size(Ts... args) {
    return sizeof...(args); // args 这个函数参数包的元素个数
}

// 对单个元素的处理函数,每个元素类型独立推导
template <typename T> void print_one(const T& x) {
    std::cout << "  [" << typeid(x).name() << "] " << x << "\n";
}

// 模式展开:print_one(args)... 展开成 print_one(a0), print_one(a1), ...
// 注意:模式展开不能直接写在语句位置,得借助数组初始化列表或 fold
template <typename... Ts> void print_all(const Ts&... args) {
    using expand_t = int[];
    (void)expand_t{0, (print_one(args), 0)...}; // 老办法(C++11 起可用)
}

// 对比:逗号 fold(C++17)做同一件事更直接
template <typename... Ts> void print_all_fold(const Ts&... args) {
    std::cout << "  (fold 写法)\n";
    (void)((print_one(args), ...)); // 一元右逗号折叠
}

int main() {
    std::cout << "sizeof... 数包大小:\n";
    std::cout << "  pack_size():             " << pack_size() << "\n";
    std::cout << "  pack_size(1):            " << pack_size(1) << "\n";
    std::cout << "  pack_size(1,2.5,\"hi\"):  " << pack_size(1, 2.5, "hi") << "\n";

    static_assert(pack_size() == 0);
    static_assert(pack_size(1, 2, 3) == 3);

    std::cout << "\n模式展开(异类型包,每个元素类型独立推导):\n";
    print_all(1, 2.5, "hi");

    std::cout << "\n逗号 fold 同样效果:\n";
    print_all_fold(1, 2.5, "hi");
}
