// 配套 03-perfect-forwarding.md「std::forward vs std::move:泛型代码里别用 move 转发」
// 同一个转发器:用 std::move 会错误地把左值搬空;用 std::forward 则保持类别、左值完好
// 编译运行:g++ -std=c++17 -Wall -Wextra move_vs_forward.cpp -o mvf && ./mvf
#include <iostream>
#include <utility>
using namespace std;

// 一个持有堆内存的类,移动后清空,能直观看出有没有被搬走
class Box {
  public:
    Box() : data_(nullptr), size_(0) {}
    explicit Box(const char* s) {
        size_ = 0;
        while (s[size_])
            ++size_;
        data_ = new char[size_ + 1];
        for (int i = 0; i <= size_; ++i)
            data_[i] = s[i];
    }
    ~Box() { delete[] data_; }
    Box(const Box& o) : data_(new char[o.size_ + 1]), size_(o.size_) {
        for (int i = 0; i <= size_; ++i)
            data_[i] = o.data_[i];
    }
    Box(Box&& o) noexcept : data_(o.data_), size_(o.size_) {
        o.data_ = nullptr;
        o.size_ = 0;
    }
    const char* raw() const { return data_ ? data_ : "<空>"; }

  private:
    char* data_;
    int size_;
};

void consume(Box& x) {
    cout << "  [consume] 命中左值重载(没搬走):" << x.raw() << "\n";
}
void consume(Box&& x) {
    Box tmp(std::move(x));
    cout << "  [consume] 右值,搬走后源对象=\"" << x.raw() << "\"\n";
}

template <typename T> void wrap_move(T&& x) {
    consume(std::move(x));
} // 无条件转右值
template <typename T> void wrap_forward(T&& x) {
    consume(std::forward<T>(x));
} // 条件转发

int main() {
    cout << "--- std::forward 转发左值(正确)---\n";
    Box a("hello");
    wrap_forward(a);
    cout << "  调用方 a.raw() = " << a.raw() << " (完好)\n";

    cout << "\n--- std::move 错误转发左值(破坏)---\n";
    Box b("hello");
    wrap_move(b);
    cout << "  调用方 b.raw() = " << b.raw() << " (被搬空!)\n";
}
