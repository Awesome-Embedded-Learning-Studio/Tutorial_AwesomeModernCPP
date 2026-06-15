// Standard: C++20
// std::initializer_list：花括号背后的只读视图，以及元素无法 move 的「移动陷阱」
#include <initializer_list>
#include <iostream>
#include <vector>

class Tracked {
  public:
    int id;
    static int copy_count;
    static int move_count;

    explicit Tracked(int i) : id(i) {}
    Tracked(const Tracked& o) : id(o.id) { ++copy_count; }
    Tracked(Tracked&& o) noexcept : id(o.id) { ++move_count; }
};
int Tracked::copy_count = 0;
int Tracked::move_count = 0;

int main() {
    std::cout << "== initializer_list：编译器为 {…} 生成的只读视图 ==\n";
    std::initializer_list<int> il = {1, 2, 3, 4};
    std::cout << "size = " << il.size() << '\n';
    int sum = 0;
    for (auto x : il) {
        sum += x;
    }
    std::cout << "sum = " << sum << '\n';

    std::cout << "\n== 移动陷阱：initializer_list 元素是 const，进容器只能拷贝 ==\n";
    Tracked::copy_count = 0;
    Tracked::move_count = 0;
    std::vector<Tracked> v{Tracked(1), Tracked(2), Tracked(3)};
    std::cout << "vector{3 个 Tracked}: copies = " << Tracked::copy_count
              << ", moves = " << Tracked::move_count << '\n';
    std::cout << "（元素是 const → 无法 move 出 initializer_list，只能拷贝进 vector）\n";

    std::cout << "\n== 花括号初始化的重载优先级 ==\n";
    std::vector<int> a{1, 2, 3}; // 走 initializer_list 构造：3 个元素 [1,2,3]
    std::vector<int> b(3, 1);    // 走 (count, value)：3 个 1
    std::cout << "a{1,2,3} = [" << a[0] << ',' << a[1] << ',' << a[2] << "]  (initializer_list)\n";
    std::cout << "b(3,1)  = [" << b[0] << ',' << b[1] << ',' << b[2] << "]  (count, value)\n";
    return 0;
}
