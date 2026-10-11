// 与示例 51 成对的第二场对拍：同样「先写自己的、再读对方的」，
// 这回两边的写读用 acquire/release 配对（store 用 release，load 用 acquire）。
// 结果：x86 上 (0,0) 照样非零——发布/获取这对形状管的是「写先于读可见」，
// 管不住「自己的写排在自己的读前面」这一种换序（StoreLoad）。
// 只有 seq_cst 把它按住。三轮对照一次看清：relaxed、acq/rel、seq_cst。
#include <atomic>
#include <iostream>
#include <thread>

constexpr long kRounds = 200000;

template <std::memory_order StoreMO, std::memory_order LoadMO> long run_litmus() {
    std::atomic<int> x{0}, y{0};
    std::atomic<long> gate{0}, done1{0}, done2{0};
    std::atomic<int> out1{0}, out2{0};
    long both_zero = 0;

    std::thread t1([&] {
        long seen = 0;
        for (;;) {
            long r;
            while ((r = gate.load(std::memory_order_acquire)) == seen) {
            }
            if (r < 0)
                return;
            seen = r;
            x.store(1, StoreMO);
            out1.store(y.load(LoadMO), std::memory_order_relaxed);
            done1.store(r, std::memory_order_release);
        }
    });
    std::thread t2([&] {
        long seen = 0;
        for (;;) {
            long r;
            while ((r = gate.load(std::memory_order_acquire)) == seen) {
            }
            if (r < 0)
                return;
            seen = r;
            y.store(1, StoreMO);
            out2.store(x.load(LoadMO), std::memory_order_relaxed);
            done2.store(r, std::memory_order_release);
        }
    });

    for (long r = 1; r <= kRounds; ++r) {
        x.store(0, std::memory_order_relaxed);
        y.store(0, std::memory_order_relaxed);
        gate.store(r, std::memory_order_release);
        while (done1.load(std::memory_order_acquire) < r ||
               done2.load(std::memory_order_acquire) < r) {
        }
        if (out1.load(std::memory_order_relaxed) == 0 && out2.load(std::memory_order_relaxed) == 0)
            ++both_zero;
    }
    gate.store(-1, std::memory_order_release);
    t1.join();
    t2.join();
    return both_zero;
}

int main() {
    long relaxed = run_litmus<std::memory_order_relaxed, std::memory_order_relaxed>();
    long acqrel = run_litmus<std::memory_order_release, std::memory_order_acquire>();
    long seqcst = run_litmus<std::memory_order_seq_cst, std::memory_order_seq_cst>();

    std::cout << "rounds per phase: " << kRounds << "\n";
    std::cout << "relaxed          (0,0) outcomes: " << relaxed << "\n";
    std::cout << "release/acquire  (0,0) outcomes: " << acqrel << "\n";
    std::cout << "seq_cst          (0,0) outcomes: " << seqcst << "\n";
    return 0;
}
