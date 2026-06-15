// Standard: C++20
// vector 实现层深入：扩容追踪 / 迭代器失效 / move_if_noexcept / constexpr vector / erase_if
#include <iostream>
#include <vector>

// ---- move_if_noexcept 观察：move 构造故意不标 noexcept ----
class Tracked {
  public:
    int id;
    static int move_count;
    static int copy_count;

    explicit Tracked(int i) : id(i) {}
    Tracked(const Tracked& o) : id(o.id) { ++copy_count; }
    Tracked(Tracked&& o) noexcept(false) : id(o.id) { ++move_count; }
};
int Tracked::move_count = 0;
int Tracked::copy_count = 0;

// ---- constexpr vector：编译期当临时工作区，只返回标量 ----
// transient allocation：常量求值期分配的内存必须在该求值内释放，
// 所以不能定义持久 constexpr vector 变量，只能让缓冲在函数内自然析构。
constexpr int sum_first_n(int n) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) {
        v.push_back(i + 1);
    }
    int sum = 0;
    for (int x : v) {
        sum += x;
    }
    return sum;
}
static_assert(sum_first_n(100) == 5050); // 全程编译期完成

int main() {
    std::cout << "== 扩容追踪（push_back 17 次）==\n";
    std::vector<int> v;
    for (int i = 0; i < 17; ++i) {
        std::size_t cap_before = v.capacity();
        v.push_back(i);
        if (v.capacity() != cap_before) {
            std::cout << "push " << i << ": capacity " << cap_before << " -> " << v.capacity()
                      << '\n';
        }
    }

    std::cout << "\n== 迭代器失效 ==\n";
    std::vector<int> w{1, 2, 3};
    w.reserve(3);
    const int* p = &w[1];
    w.push_back(4); // 余量足够，不扩容 → 指针仍有效
    std::cout << "push_back 不扩容，指针有效? " << (p == &w[1]) << '\n';
    w.reserve(100); // 超过 capacity → 换缓冲 → 指针失效
    std::cout << "reserve 超容量后，指针有效? " << (p == &w[1]) << '\n';

    std::cout << "\n== move_if_noexcept ==\n";
    std::vector<Tracked> t;
    t.reserve(2);
    t.emplace_back(1);
    t.emplace_back(2);
    t.emplace_back(3); // 触发扩容
    std::cout << "扩容时 moves=" << Tracked::move_count << " copies=" << Tracked::copy_count
              << "（noexcept(false) → 扩容倾向 copy；改成 noexcept 则变 move）\n";

    std::cout << "\n== constexpr vector (C++20) ==\n";
    std::cout << "sum_first_n(100) = " << sum_first_n(100) << "（编译期 static_assert 已验证）\n";

    std::cout << "\n== erase_if (C++20) ==\n";
    std::vector<int> e{1, 2, 3, 4, 5, 6};
    std::size_t removed = std::erase_if(e, [](int x) { return x % 2 == 0; });
    std::cout << "removed=" << removed << " left:";
    for (int x : e) {
        std::cout << ' ' << x;
    }
    std::cout << '\n';
    return 0;
}
