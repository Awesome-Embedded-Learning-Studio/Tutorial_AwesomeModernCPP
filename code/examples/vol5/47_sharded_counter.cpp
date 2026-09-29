// 分片锁计数：8 把 shared_mutex 摊开竞争，八个线程各加一万次，总数分毫不差
#include <array>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

// 分片锁：N 把 shared_mutex 摊开竞争，写走独占档，读走读档
template <std::size_t N> class ShardedCounter {
  public:
    void add(std::size_t key, long delta) {
        Shard& s = shards_[key % N]; // key 映射到固定分片
        std::lock_guard<std::shared_mutex> lk(s.m);
        s.value += delta;
    }

    long total() const {
        long sum = 0;
        for (const Shard& s : shards_) {
            std::shared_lock<std::shared_mutex> lk(s.m); // 逐片读，不挡别片的写
            sum += s.value;
        }
        return sum;
    }

  private:
    struct Shard {
        mutable std::shared_mutex m; // total() 是 const，锁要 mutable
        long value = 0;
    };
    std::array<Shard, N> shards_{};
};

int main() {
    ShardedCounter<8> counter;
    std::vector<std::jthread> workers;
    for (int t = 0; t != 8; ++t) {
        workers.emplace_back([t, &counter] {
            for (int i = 0; i != 10000; ++i) {
                counter.add((t * 7 + i) % 32, 1); // key 撒在 32 个数上，落到 8 片
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "分片计数总数 = " << counter.total() << "（期望 80000）\n";
    return 0;
}
