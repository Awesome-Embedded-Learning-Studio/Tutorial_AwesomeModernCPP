// 消息传递的配对形状：生产者写完载荷，release 发布标志；
// 消费者 acquire 看到标志，再读载荷——这一对配好了，载荷必然是新的。
// 对照相位把 store 的 release 换成 relaxed：配对缺了一半。
// 在 x86 上对照相位大概率也读不到旧值（硬件白送了 StoreStore），
// 读到 0 不代表它合法——这正是开篇讲过的「平台送的和合同保的是两回事」。
#include <atomic>
#include <iostream>
#include <thread>

constexpr long kRounds = 500000;

struct Channel {
    alignas(64) std::atomic<long> flag{0};
    alignas(64) long payload = 0;
};

template <bool ReleaseStore> long run_rounds() {
    Channel ch;
    long stale = 0;

    std::jthread producer([&] {
        for (long r = 1; r <= kRounds; ++r) {
            ch.payload = r; // 先写载荷
            if constexpr (ReleaseStore)
                ch.flag.store(r, std::memory_order_release); // 后发布
            else
                ch.flag.store(r, std::memory_order_relaxed); // 只写，不发布
            // 等消费者确认这轮收完，避免生产者跑太远
            while (ch.flag.load(std::memory_order_acquire) != -r) {
            }
        }
    });
    std::jthread consumer([&] {
        for (long r = 1; r <= kRounds; ++r) {
            long observed;
            while ((observed = ch.flag.load(std::memory_order_acquire)) != r) {
            }
            // 看到标志为 r：release 版保证 payload 已是 r；relaxed 版没有这个保证
            if (ch.payload != r)
                ++stale;
            ch.flag.store(-r, std::memory_order_release); // 确认收卷
        }
    });

    return stale;
}

int main() {
    long paired_stale = run_rounds<true>();
    long relaxed_stale = run_rounds<false>();

    std::cout << "rounds per phase: " << kRounds << "\n";
    std::cout << "release/acquire pairing, stale payload: " << paired_stale << "\n";
    std::cout << "relaxed store (no pairing), stale payload: " << relaxed_stale << "\n";
    return 0;
}
