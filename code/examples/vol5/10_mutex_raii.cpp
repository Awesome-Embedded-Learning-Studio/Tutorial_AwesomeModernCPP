// mutex and RAII lock guards: lock_guard, unique_lock, scoped_lock
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 1. lock_guard: simplest RAII lock
std::mutex g_mtx;
int g_counter = 0;

void safe_increment(int times) {
    for (int i = 0; i < times; ++i) {
        std::lock_guard<std::mutex> lock(g_mtx);
        ++g_counter;
    }
}

// 2. unique_lock with condition_variable: thread-safe queue
template <typename T> class ThreadSafeQueue {
  public:
    void push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

  private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
};

// 3. scoped_lock: deadlock-free multi-lock (C++17)
std::mutex mtx_a;
std::mutex mtx_b;
int shared_a = 0;
int shared_b = 0;

void safe_swap_values(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        std::scoped_lock lock(mtx_a, mtx_b);
        int tmp = shared_a;
        shared_a = shared_b;
        shared_b = tmp;
    }
}

int main() {
    // Demo 1: lock_guard
    std::cout << "=== lock_guard demo ===\n";
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(safe_increment, 250000);
    }
    for (auto& t : threads)
        t.join();
    std::cout << "counter = " << g_counter << " (expected 1000000)\n";

    // Demo 2: unique_lock + condition_variable
    std::cout << "\n=== unique_lock + CV demo ===\n";
    ThreadSafeQueue<int> tsq;
    constexpr int kItems = 10;

    std::thread producer([&]() {
        for (int i = 0; i < kItems; ++i) {
            tsq.push(i);
            std::cout << "Produced: " << i << "\n";
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < kItems; ++i) {
            int val = tsq.pop();
            std::cout << "Consumed: " << val << "\n";
        }
    });

    producer.join();
    consumer.join();

    // Demo 3: scoped_lock
    std::cout << "\n=== scoped_lock demo ===\n";
    shared_a = 100;
    shared_b = 200;

    std::thread t1(safe_swap_values, 10000);
    std::thread t2(safe_swap_values, 10000);
    t1.join();
    t2.join();

    std::cout << "After " << 20000 << " swaps: a=" << shared_a << " b=" << shared_b
              << " sum=" << (shared_a + shared_b) << "\n";

    return 0;
}
