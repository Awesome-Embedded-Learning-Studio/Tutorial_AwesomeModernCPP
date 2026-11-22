// move-only 类型:unique_ptr 的所有权随 std::move 交进线程
#include <cstddef>
#include <memory>
#include <print>
#include <thread>

void process_data(std::unique_ptr<int[]> data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i)
        data[i] *= 2;
    std::println("  worker: data[0] = {}", data[0]);
}

int main() {
    constexpr std::size_t kSize = 5;
    auto data = std::make_unique<int[]>(kSize);
    for (std::size_t i = 0; i < kSize; ++i)
        data[i] = static_cast<int>(i + 1);
    std::thread t(process_data, std::move(data), kSize);
    std::println("main: data after move is {}", data ? "not null" : "null");
    t.join();
}
