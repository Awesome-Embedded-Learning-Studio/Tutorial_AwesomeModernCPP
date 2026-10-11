// 验证：x86-64 的 store buffer 会造成 StoreLoad 重排，seq_cst 将其消除
//
// 背景：文章声称「两个线程各自先写自己的标志、再读对方的标志」时，
//       在 relaxed 内存序下会出现双方都读到 0 的结局——两个 store 都被
//       store buffer 拖在了对方的 load 后面；换成 seq_cst 后该结局归零。
//
// 预期结果：relaxed 阶段 (0,0) 计数非零（比例随机器与调度波动，非零是常态）；
//           seq_cst 阶段 (0,0) 计数为 0。
//
// 与 examples/vol5/51_store_buffer_litmus.cpp 的区别：本实验把两个线程
// 固定到不同核心（默认 CPU 0 / CPU 2），轮数更大，并输出比例与checksum。
//
// 编译命令（仓库根目录）：
//   g++ -std=c++20 -O2 -pthread -o /tmp/store_buffer_litmus store_buffer_litmus.cpp
//
// 检查：/tmp/store_buffer_litmus
//
// 编译器：GCC 16.2.1（WSL2, AMD Ryzen 7 9700X）

// Standard: C++20
// Platform: Linux x86-64; CPU 0 and CPU 2 are separate cores on the test host.

#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

#include <pthread.h>
#include <sched.h>

constexpr std::uint64_t kRounds = 1'000'000;
constexpr int kThreadACpu = 0;
constexpr int kThreadBCpu = 2;

bool pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

struct LitmusState {
    alignas(64) std::atomic<int> x{0};
    alignas(64) std::atomic<int> y{0};
    alignas(64) std::atomic<std::uint64_t> gate{0};
    alignas(64) std::atomic<std::uint64_t> done_a{0};
    alignas(64) std::atomic<std::uint64_t> done_b{0};
    alignas(64) std::atomic<int> out_a{0};
    alignas(64) std::atomic<int> out_b{0};
};

template <std::memory_order MO> std::uint64_t run_litmus() {
    // 每个阶段全新构造：gate/done/out 的残留值会让下一阶段的回合屏障
    // 直接放行（done 已经是上一阶段的百万），协议就废了
    LitmusState st;
    std::uint64_t both_zero = 0;

    std::thread ta([&] {
        if (!pin_to_cpu(kThreadACpu)) { /* 绑核失败照常跑，只影响比例 */
        }
        std::uint64_t seen = 0;
        for (;;) {
            std::uint64_t r;
            while ((r = st.gate.load(std::memory_order_acquire)) == seen) {
            }
            if (r == static_cast<std::uint64_t>(-1))
                return;
            seen = r;
            st.x.store(1, MO);
            st.out_a.store(st.y.load(MO), std::memory_order_relaxed);
            st.done_a.store(r, std::memory_order_release);
        }
    });
    std::thread tb([&] {
        if (!pin_to_cpu(kThreadBCpu)) { /* 同上 */
        }
        std::uint64_t seen = 0;
        for (;;) {
            std::uint64_t r;
            while ((r = st.gate.load(std::memory_order_acquire)) == seen) {
            }
            if (r == static_cast<std::uint64_t>(-1))
                return;
            seen = r;
            st.y.store(1, MO);
            st.out_b.store(st.x.load(MO), std::memory_order_relaxed);
            st.done_b.store(r, std::memory_order_release);
        }
    });

    for (std::uint64_t r = 1; r <= kRounds; ++r) {
        st.x.store(0, std::memory_order_relaxed);
        st.y.store(0, std::memory_order_relaxed);
        st.gate.store(r, std::memory_order_release);
        while (st.done_a.load(std::memory_order_acquire) < r ||
               st.done_b.load(std::memory_order_acquire) < r) {
        }
        if (st.out_a.load(std::memory_order_relaxed) == 0 &&
            st.out_b.load(std::memory_order_relaxed) == 0)
            ++both_zero;
    }
    st.gate.store(static_cast<std::uint64_t>(-1), std::memory_order_release);
    ta.join();
    tb.join();
    return both_zero;
}

int main() {
    std::uint64_t relaxed_zero = run_litmus<std::memory_order_relaxed>();
    std::uint64_t seqcst_zero = run_litmus<std::memory_order_seq_cst>();

    std::cout << "rounds per phase: " << kRounds << "\n";
    std::cout << "relaxed  (0,0) outcomes: " << relaxed_zero << " (" << std::fixed
              << std::setprecision(2) << 100.0 * static_cast<double>(relaxed_zero) / kRounds
              << "%)\n";
    std::cout << "seq_cst  (0,0) outcomes: " << seqcst_zero << "\n";
    return 0;
}
