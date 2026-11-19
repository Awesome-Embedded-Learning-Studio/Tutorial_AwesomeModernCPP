// 三种构造方式之二:lambda,按引用捕获要在 join 之后才能读结果
#include <print>
#include <thread>
#include <vector>

int main() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    long long sum = 0;

    std::thread t([&data, &sum] {
        for (int v : data)
            sum += v;
    });
    t.join();
    std::println("sum = {}", sum);
}
