// WeakPtrFactory 演示:最后成员惯用法 + invalidate_weak_ptrs(铸新 Flag)vs and_doom(不再铸)
// 来源:WeakPtr 实战(三):WeakPtrFactory 与"最后成员"惯用法 (02-3)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 17_weak_ptr_factory.cpp -o 17_weak_ptr_factory -pthread

#include "weak_ptr.hpp"

#include <iostream>
#include <vector>

// ✓ 正确顺序:factory 最后声明 → 最先析构 → 守护成员析构期
class Controller {
  public:
    void on_done(int v) { buf_.push_back(v); }
    std::size_t buf_size() const { return buf_.size(); }

    tamcpp::chrome::WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
    void invalidate() { weak_factory_.invalidate_weak_ptrs(); }
    bool has_observers() const { return weak_factory_.has_weak_ptrs(); }

  private:
    std::vector<int> buf_;                                          // 先声明 → 最后析构
    tamcpp::chrome::WeakPtrFactory<Controller> weak_factory_{this}; // 最后成员 → 最先析构
};

int main() {
    using namespace tamcpp::chrome;

    std::cout << "=== ① factory 析构 = 失效所有 WeakPtr(最后成员惯用法)==\n";
    WeakPtr<Controller> wp;
    {
        Controller c;
        wp = c.get_weak();
        c.on_done(1);
        std::cout << "  c 活着:  wp 判活=" << (wp ? "yes" : "no") << ", buf_size=" << wp->buf_size()
                  << "\n";
    } // c 析构:weak_factory_ 最先析构 → wp 失效 → buf_ 才析构(成员析构期被守护)
    std::cout << "  c 析构后:wp 判活=" << (wp ? "yes" : "no") << "\n";

    std::cout << "\n=== ② invalidate_weak_ptrs:集体失效 + factory 可继续铸新 Flag ===\n";
    Controller c;
    auto wp1 = c.get_weak();
    auto wp2 = c.get_weak();
    std::cout << "  invalidate 前:  wp1=" << (wp1 ? "yes" : "no") << " wp2=" << (wp2 ? "yes" : "no")
              << " has_observers=" << c.has_observers() << "\n";
    c.invalidate(); // 失效 wp1/wp2 + 铸一枚新 Flag
    std::cout << "  invalidate 后:  wp1=" << (wp1 ? "yes" : "no") << " wp2=" << (wp2 ? "yes" : "no")
              << "\n";
    auto wp3 = c.get_weak(); // factory 还能继续铸(用的是新 Flag)
    std::cout << "  再铸 wp3:       wp3=" << (wp3 ? "yes" : "no")
              << "(wp3 与 wp1/wp2 不共享 Flag)\n";

    std::cout << "\n=== ③ 反例:factory 放前面会留出成员析构期的悬垂窗口 ===\n";
    std::cout << "  BadController { WeakPtrFactory fac_{this}; vector buf_; } ——\n";
    std::cout << "  析构顺序:buf_ 先析构 → fac_ 后析构 → 中间窗口 WeakPtr 仍有效 → deref 即 UAF\n";
    std::cout << "  这就是为什么 WeakPtrFactory 必须是最后一个成员\n";
    return 0;
}
