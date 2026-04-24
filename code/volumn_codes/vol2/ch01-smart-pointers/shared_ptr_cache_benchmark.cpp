// shared_ptr_cache_benchmark.cpp
// Benchmark: shared_ptr vs raw pointer in a multi-threaded producer-consumer queue.
// Measures throughput degradation caused by control-block cache-line bouncing.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <sys/utsname.h>

// ── Configuration ──────────────────────────────────────────────────────────
static constexpr int kProducers        = 4;
static constexpr int kConsumers        = 4;
static constexpr int kMessagesPerProd  = 2'000'000;
static constexpr int kTotalMessages    = kProducers * kMessagesPerProd;

// ── Payload ────────────────────────────────────────────────────────────────
struct Message {
    int  id;
    char payload[56]; // pad to 64 bytes so the object itself is cache-line-sized
};

// ── Thread-safe queue (raw pointer) ────────────────────────────────────────
struct RawQueue {
    std::mutex              mtx;
    std::queue<Message*>    q;
    std::atomic<bool>       done{false};

    void push(Message* msg) {
        std::lock_guard lock(mtx);
        q.push(msg);
    }

    bool try_pop(Message*& msg) {
        std::lock_guard lock(mtx);
        if (q.empty()) return false;
        msg = q.front();
        q.pop();
        return true;
    }
};

// ── Thread-safe queue (shared_ptr) ─────────────────────────────────────────
struct SharedQueue {
    std::mutex                          mtx;
    std::queue<std::shared_ptr<Message>> q;
    std::atomic<bool>                   done{false};

    void push(std::shared_ptr<Message> msg) {
        std::lock_guard lock(mtx);
        q.push(std::move(msg));
    }

    bool try_pop(std::shared_ptr<Message>& msg) {
        std::lock_guard lock(mtx);
        if (q.empty()) return false;
        msg = std::move(q.front());
        q.pop();
        return true;
    }
};

// ── Raw-pointer benchmark ──────────────────────────────────────────────────
static double run_raw_benchmark() {
    RawQueue queue;
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;

    // Pre-allocate all messages (no allocation in hot path)
    std::vector<std::unique_ptr<Message>> pool;
    pool.reserve(kTotalMessages);
    for (int i = 0; i < kTotalMessages; ++i) {
        pool[i] = std::make_unique<Message>();
        pool[i]->id = i;
    }
    std::atomic<int> next{0};

    auto start = std::chrono::high_resolution_clock::now();

    // Consumers
    for (int c = 0; c < kConsumers; ++c) {
        threads.emplace_back([&] {
            while (true) {
                if (queue.done.load(std::memory_order_acquire)) {
                    Message* msg;
                    while (queue.try_pop(msg)) {
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    }
                    break;
                }
                Message* msg;
                if (queue.try_pop(msg)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Producers
    for (int p = 0; p < kProducers; ++p) {
        threads.emplace_back([&, p] {
            for (int i = 0; i < kMessagesPerProd; ++i) {
                int idx = next.fetch_add(1, std::memory_order_relaxed);
                queue.push(pool[idx].get());
            }
        });
    }

    // Wait for producers, then signal done
    for (int p = 0; p < kProducers; ++p) {
        threads[kConsumers + p].join();
    }
    queue.done.store(true, std::memory_order_release);

    // Wait for consumers
    for (int c = 0; c < kConsumers; ++c) {
        threads[c].join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::printf("  [raw ptr  ] %d messages in %.1f ms  (%.0f msg/s)\n",
                consumed.load(), ms, kTotalMessages / (ms / 1000.0));
    return ms;
}

// ── shared_ptr benchmark ───────────────────────────────────────────────────
static double run_shared_benchmark() {
    SharedQueue queue;
    std::atomic<int> consumed{0};
    std::vector<std::thread> threads;

    // Pre-allocate a single shared_ptr per message; each producer copies it
    // into the queue (which increments the ref-count), and each consumer's
    // pop then drops it (which decrements).  This exercises the control-block
    // cache-line contention that we want to measure.
    std::vector<std::shared_ptr<Message>> pool;
    pool.reserve(kTotalMessages);
    for (int i = 0; i < kTotalMessages; ++i) {
        pool[i] = std::make_shared<Message>();
        pool[i]->id = i;
    }
    std::atomic<int> next{0};

    auto start = std::chrono::high_resolution_clock::now();

    // Consumers
    for (int c = 0; c < kConsumers; ++c) {
        threads.emplace_back([&] {
            while (true) {
                if (queue.done.load(std::memory_order_acquire)) {
                    std::shared_ptr<Message> msg;
                    while (queue.try_pop(msg)) {
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    }
                    break;
                }
                std::shared_ptr<Message> msg;
                if (queue.try_pop(msg)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Producers — copy shared_ptr into queue (ref-count bump on enqueue)
    for (int p = 0; p < kProducers; ++p) {
        threads.emplace_back([&, p] {
            for (int i = 0; i < kMessagesPerProd; ++i) {
                int idx = next.fetch_add(1, std::memory_order_relaxed);
                queue.push(pool[idx]); // copy → ref-count++
            }
        });
    }

    // Wait for producers, then signal done
    for (int p = 0; p < kProducers; ++p) {
        threads[kConsumers + p].join();
    }
    queue.done.store(true, std::memory_order_release);

    // Wait for consumers
    for (int c = 0; c < kConsumers; ++c) {
        threads[c].join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();

    std::printf("  [shared   ] %d messages in %.1f ms  (%.0f msg/s)\n",
                consumed.load(), ms, kTotalMessages / (ms / 1000.0));
    return ms;
}

// ── Main ───────────────────────────────────────────────────────────────────
int main() {
    // Print machine info
    struct utsname info;
    if (uname(&info) == 0) {
        std::printf("shared_ptr cache-line contention benchmark\n");
        std::printf("==========================================\n");
        std::printf("hostname : %s\n", info.nodename);
        std::printf("kernel   : %s %s\n", info.sysname, info.release);
    }
    std::printf("producers: %d  consumers: %d  messages: %d\n\n",
                kProducers, kConsumers, kTotalMessages);

    // Warm up
    run_raw_benchmark();
    run_shared_benchmark();
    std::printf("\n--- measured runs ---\n\n");

    double raw_total = 0, shared_total = 0;
    constexpr int kRuns = 5;

    for (int i = 0; i < kRuns; ++i) {
        std::printf("run %d:\n", i + 1);
        raw_total    += run_raw_benchmark();
        shared_total += run_shared_benchmark();
        std::printf("\n");
    }

    double raw_avg    = raw_total    / kRuns;
    double shared_avg = shared_total / kRuns;
    double overhead   = ((shared_avg - raw_avg) / raw_avg) * 100.0;

    std::printf("=== Summary (avg of %d runs) ===\n", kRuns);
    std::printf("  raw ptr avg:     %.1f ms\n", raw_avg);
    std::printf("  shared_ptr avg:  %.1f ms\n", shared_avg);
    std::printf("  overhead:        %+.1f%%\n", overhead);
}
