// Standard: C++20
// Platform: Linux x86-64; CPU 0 and CPU 2 are separate cores on the test host.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <immintrin.h>
#include <pthread.h>
#include <sched.h>

constexpr std::uint64_t kIterations = 2'000'000;
constexpr int kTrials = 7;
constexpr int kProducerCpu = 0;
constexpr int kConsumerCpu = 2;

bool pin_to_cpu(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

struct SharedState {
    alignas(64) std::array<std::atomic<std::uint64_t>, 3> fields{};
    alignas(64) std::atomic<std::uint64_t> acknowledged{0};
    alignas(64) std::array<std::uint64_t, 3> payload{};
};

struct Sample {
    double ns_per_round;
    std::uint64_t checksum;
};

template <bool UseFences> Sample run_once(std::uint64_t iterations) {
    SharedState state;
    std::uint64_t checksum = 0;
    std::atomic<bool> affinity_failed{false};
    constexpr auto kStoreOrder = UseFences ? std::memory_order_relaxed : std::memory_order_release;
    constexpr auto kLoadOrder = UseFences ? std::memory_order_relaxed : std::memory_order_acquire;

    const auto start = std::chrono::steady_clock::now();
    std::thread producer([&] {
        if (!pin_to_cpu(kProducerCpu)) {
            affinity_failed.store(true, std::memory_order_relaxed);
        }
        for (std::uint64_t i = 1; i <= iterations; ++i) {
            // 确认上一轮读取已完成，再改写非原子 payload。
            while (state.acknowledged.load(std::memory_order_acquire) != i - 1) {
                _mm_pause();
            }

            state.payload[0] = i;
            state.payload[1] = i + 7;
            state.payload[2] = i * 3;

            if constexpr (UseFences) {
                std::atomic_thread_fence(std::memory_order_release);
            }
            for (auto& field : state.fields) {
                field.store(i, kStoreOrder);
            }
        }
    });

    std::thread consumer([&] {
        if (!pin_to_cpu(kConsumerCpu)) {
            affinity_failed.store(true, std::memory_order_relaxed);
        }
        for (std::uint64_t i = 1; i <= iterations; ++i) {
            // 两种写法每次轮询都读取三个字段，只改变内存序。
            while (true) {
                const auto a = state.fields[0].load(kLoadOrder);
                const auto b = state.fields[1].load(kLoadOrder);
                const auto c = state.fields[2].load(kLoadOrder);
                if (a == i && b == i && c == i) {
                    break;
                }
                _mm_pause();
            }

            if constexpr (UseFences) {
                std::atomic_thread_fence(std::memory_order_acquire);
            }
            checksum += state.payload[0] + state.payload[1] + state.payload[2];
            state.acknowledged.store(i, std::memory_order_release);
        }
    });

    producer.join();
    consumer.join();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (affinity_failed.load(std::memory_order_relaxed)) {
        throw std::runtime_error("could not pin worker threads to CPUs 0 and 2");
    }

    const std::uint64_t expected = 5 * iterations * (iterations + 1) / 2 + 7 * iterations;
    if (checksum != expected) {
        throw std::runtime_error("incorrect checksum");
    }

    const double nanoseconds = std::chrono::duration<double, std::nano>(elapsed).count();
    return {nanoseconds / static_cast<double>(iterations), checksum};
}

double median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

int main() {
    run_once<false>(20'000);
    run_once<true>(20'000);

    std::vector<double> ordered;
    std::vector<double> fenced;
    ordered.reserve(kTrials);
    fenced.reserve(kTrials);

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "Iterations per trial: " << kIterations << '\n';
    std::cout << "Producer CPU: " << kProducerCpu << ", consumer CPU: " << kConsumerCpu << '\n';
    std::cout << "Round-trip time includes both threads and the acknowledgement.\n";
    for (int trial = 0; trial < kTrials; ++trial) {
        // 交替测试顺序，避免一种写法始终先运行。
        if (trial % 2 == 0) {
            ordered.push_back(run_once<false>(kIterations).ns_per_round);
            fenced.push_back(run_once<true>(kIterations).ns_per_round);
        } else {
            fenced.push_back(run_once<true>(kIterations).ns_per_round);
            ordered.push_back(run_once<false>(kIterations).ns_per_round);
        }
        std::cout << "Trial " << trial + 1 << ": per-field release/acquire = " << ordered.back()
                  << " ns, fences + relaxed = " << fenced.back() << " ns\n";
    }

    const double ordered_median = median(ordered);
    const double fenced_median = median(fenced);
    std::cout << "Median: per-field release/acquire = " << ordered_median
              << " ns, fences + relaxed = " << fenced_median << " ns\n";
    std::cout << "Fence / per-field ratio: " << fenced_median / ordered_median << '\n';
}
