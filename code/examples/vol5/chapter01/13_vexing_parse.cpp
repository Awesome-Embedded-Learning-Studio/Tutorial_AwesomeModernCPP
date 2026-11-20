// 最易错解析:std::thread t(Accumulator(data)) 会被当成函数声明
// 错误写法留在注释里;放开后编译器警告 [-Wvexing-parse],对 t 的成员调用编不过(报错见正文)
#include <print>
#include <thread>
#include <vector>

class Accumulator {
  public:
    explicit Accumulator(const std::vector<int>& data) : data_(data) {}
    void operator()() const {
        long long s = 0;
        for (int v : data_)
            s += v;
        std::println("functor: sum = {}", s);
    }

  private:
    const std::vector<int>& data_; // 引用成员：拷贝进线程的只是引用本身
};

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};

    // std::thread t(Accumulator(data));  // 这一行声明的是名叫 t 的函数,不是线程对象

    std::thread t{Accumulator(data)}; // 花括号统一初始化,才是线程对象
    t.join();
}
