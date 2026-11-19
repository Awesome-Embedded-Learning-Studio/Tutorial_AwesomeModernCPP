// 任务太细的代价:微任务串行循环对一任务一线程
// 在线沙箱限制并发线程数,这里收窄为 8 个任务演示开销量级;正文读数来自本地 2000 任务实测
#include <chrono>
#include <print>
#include <thread>
#include <vector>

using namespace std::chrono;

long long tiny_task(int base) {
    long long s = 0;
    for (int i = 0; i < 100; ++i)
        s += (base + i) % 64;
    return s;
}

int main() {
    constexpr int kTasks = 8;
    std::vector<long long> results(kTasks);

    // 串行:一个循环做完所有任务
    auto t0 = steady_clock::now();
    for (int k = 0; k < kTasks; ++k)
        results[k] = tiny_task(k);
    auto t1 = steady_clock::now();
    std::println("serial loop             : {} us", duration_cast<microseconds>(t1 - t0).count());

    // 每个任务单独开一个线程
    t0 = steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(kTasks);
    for (int k = 0; k < kTasks; ++k) {
        threads.emplace_back([&results, k] { results[k] = tiny_task(k); });
    }
    for (auto& t : threads)
        t.join();
    t1 = steady_clock::now();
    std::println("one thread per tiny task: {} us", duration_cast<microseconds>(t1 - t0).count());
}
