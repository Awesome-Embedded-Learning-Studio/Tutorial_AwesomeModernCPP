// 演示 parallel_for_each 骨架：1000 个数并行翻倍求和，与串行版同为 999000
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>

template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func func, unsigned thread_count) {
    const std::size_t length = std::distance(first, last);
    if (length == 0) {
        return;
    }
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
    }

    const std::size_t block_size = length / thread_count;
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    Iterator block_start = first;
    for (unsigned i = 0; i < thread_count - 1; ++i) {
        Iterator block_end = block_start;
        std::advance(block_end, block_size);
        threads.emplace_back(
            [block_start, block_end, &func] { std::for_each(block_start, block_end, func); });
        block_start = block_end;
    }

    std::for_each(block_start, last, func); // 最后一块，调用方自己啃

    for (std::thread& t : threads) {
        t.join(); // 一串手动 join，一个都不能少
    }
}

int main() {
    std::vector<int> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i);
    }

    parallel_for_each(data.begin(), data.end(), [](int& v) { v *= 2; }, 0);

    long sum = 0;
    for (int v : data) {
        sum += v;
    }
    std::cout << "sum = " << sum << "\n"; // 期望 999000
    return 0;
}
