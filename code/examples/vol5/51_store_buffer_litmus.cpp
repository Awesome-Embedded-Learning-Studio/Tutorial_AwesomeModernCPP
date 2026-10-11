// store buffer 的现场：同一组双线程「先写自己的、再读对方的」，
// relaxed 内存序下 (0,0) 结局非零（两个 store 都被拖在了对方的 load 后面），
// seq_cst 下归零。跑的轮数越多，relaxed 的非零计数越稳定。
#include <atomic>
#include <iostream>
#include <thread>

constexpr long kRounds = 200000;

// 一轮的协议：
//   main 把 x、y 清零，放行（gate 用 release/acquire 做回合门闩）；
//   T1 写 x=1、读 y；T2 写 y=1、读 x（读写都用传入的内存序）；
//   main 收卷，双方都读到 0 记一次「(0,0) 结局」。
template <std::memory_order MO> long run_litmus() {
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
            x.store(1, MO);
            out1.store(y.load(MO), std::memory_order_relaxed);
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
            y.store(1, MO);
            out2.store(x.load(MO), std::memory_order_relaxed);
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
    long relaxed_zero = run_litmus<std::memory_order_relaxed>();
    long seqcst_zero = run_litmus<std::memory_order_seq_cst>();

    std::cout << "rounds per phase: " << kRounds << "\n";
    std::cout << "relaxed  (0,0) outcomes: " << relaxed_zero << "\n";
    std::cout << "seq_cst  (0,0) outcomes: " << seqcst_zero << "\n";
    return 0;
}
