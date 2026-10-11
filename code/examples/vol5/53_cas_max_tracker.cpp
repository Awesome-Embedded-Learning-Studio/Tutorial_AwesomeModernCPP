// 无锁最大值追踪：八个线程各自撞随机数，撞大了就往全局发布。
// 发布靠 compare_exchange_weak 的 CAS 循环——期望旧值、想换新值，
// 被别人抢先了就带着「别人发布的新最大」重试，直到成功或发现自己的数不再最大。
#include <atomic>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

std::atomic<int> g_max{0};

void worker(int seed) {
    // 每个线程一个种子，各撞各的数
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_int_distribution<int> dist(0, 1000000);

    for (int i = 0; i < 200000; ++i) {
        int candidate = dist(rng);
        int observed = g_max.load(std::memory_order_relaxed);
        if (candidate > observed) {
            // CAS 循环：observed 是咱们看到的旧值，若此刻全局还是它，就换成 candidate；
            // 若已被别人换大，compare_exchange 会把 observed 更新成新值，循环带着它再判
            while (candidate > observed &&
                   !g_max.compare_exchange_weak(observed, candidate, std::memory_order_relaxed)) {
                // 走到这里说明有人抢先，observed 已被填成对方的值，回到条件再比
            }
        }
    }
}

int main() {
    std::vector<std::jthread> threads;
    for (int s = 1; s <= 8; ++s)
        threads.emplace_back(worker, s);
    for (auto& t : threads)
        t.join();

    std::cout << "final max: " << g_max.load(std::memory_order_relaxed) << "\n";
    // 八个线程各 20 万次、值域到 100 万，最终最大值几乎必然贴着 100 万——
    // 任何一个被漏掉的大数，都说明 CAS 循环写错了
    return 0;
}
