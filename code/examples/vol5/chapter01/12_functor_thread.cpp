// 三种构造方式之三:函数对象,复制进线程的只是对象(含引用成员)本身
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

    Accumulator acc(data);
    std::thread t(acc); // acc 被复制一份进线程
    t.join();
}
