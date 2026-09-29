// ch02/05 同步原语工具箱：call_once / latch / barrier / counting_semaphore / shared_mutex
// 演示矩阵 + 分片锁参考实现（练习 4 的底稿）
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic -pthread 05_sync_primitives_toolkit.cpp -o toolkit
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <iostream>
#include <latch>
#include <mutex>
#include <semaphore>
#include <shared_mutex>
#include <stdexcept>
#include <syncstream>
#include <thread>
#include <vector>
// 特性宏的正源头：__cpp_lib_semaphore / latch / barrier 全部由 <version> 统一定义
#include <version>

// 场景 0：特性宏探针（GCC 16.2.1 实测值均为 201907）
void ftm_probe()
{
    std::cout << "__cpp_lib_semaphore   = " << __cpp_lib_semaphore << '\n'
              << "__cpp_lib_latch       = " << __cpp_lib_latch << '\n'
              << "__cpp_lib_barrier     = " << __cpp_lib_barrier << '\n'
              << "__cpp_lib_atomic_wait = " << __cpp_lib_atomic_wait << '\n';
}

// 场景 1：call_once——失败的尝试不算数，成功的尝试全进程只执行一次
int init_attempts = 0;
std::once_flag config_once;

void flaky_init()
{
    ++init_attempts;
    std::cout << "初始化第 " << init_attempts << " 次尝试\n";
    if (init_attempts < 3) {
        throw std::runtime_error("依赖还没就绪");
    }
    std::cout << "初始化成功\n";
}

void call_once_demo()
{
    for (int round = 0; round != 3; ++round) {
        try {
            std::call_once(config_once, flaky_init);
            std::cout << "第 " << round + 1 << " 轮：call_once 顺利返回\n";
        } catch (const std::exception& e) {
            std::cout << "第 " << round + 1 << " 轮：异常传出 call_once（" << e.what() << "）\n";
        }
    }
    // 后到的线程：直接通过，初始化不再执行
    std::vector<std::jthread> latecomers;
    for (int id = 0; id != 4; ++id) {
        latecomers.emplace_back([] { std::call_once(config_once, flaky_init); });
    }
    for (auto& t : latecomers) {
        t.join();
    }
    std::cout << "后到线程全部通过，总尝试次数 = " << init_attempts << "（期望 3）\n";
}

// 场景 2：Meyers 单例——块级 static 的并发安全由 [stmt.dcl]/4 兜底
struct Heavy {
    static inline int constructions = 0;
    Heavy()
    {
        ++constructions;
        std::cout << "Heavy 构造（只应出现一次）\n";
    }
    static Heavy& instance()
    {
        static Heavy h;   // C++11 起：并发进入这里只有一个线程执行初始化
        return h;
    }
    int value = 42;
};

void meyers_demo()
{
    std::vector<std::jthread> readers;
    for (int id = 0; id != 4; ++id) {
        readers.emplace_back([] {
            int v = Heavy::instance().value;
            std::osyncstream(std::cout) << "线程读到 value = " << v << '\n';
        });
    }
    for (auto& t : readers) {
        t.join();
    }
    std::cout << "构造次数 = " << Heavy::constructions << "（期望 1）\n";
}

// 场景 3：latch——单向计数，等全到齐放行一次
void latch_demo()
{
    constexpr int n = 4;
    std::latch ready(n);
    std::vector<int> data(n, -1);
    std::cout << "开工前 try_wait = " << ready.try_wait() << "（计数未归零）\n";
    std::vector<std::jthread> workers;
    for (int i = 0; i != n; ++i) {
        workers.emplace_back([i, &ready, &data] {
            data[i] = i * i;        // 各自的准备活
            ready.count_down();     // 报到：计数减一，不等待
        });
    }
    ready.wait();                   // main 在这里等计数归零
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "归零后 try_wait = " << ready.try_wait()
              << "，data = [" << data[0] << ' ' << data[1] << ' '
              << data[2] << ' ' << data[3] << "]\n";
}

// 场景 4：barrier——一轮一轮汇合，完成函数在放行前替全队做聚合
void barrier_demo()
{
    constexpr int n = 4;
    constexpr int rounds = 2;
    std::array<int, n> partial{};
    int round_total = 0;
    long grand_total = 0;
    std::barrier sync_point(n, [&]() noexcept {
        round_total = 0;
        for (int x : partial) {
            round_total += x;
        }
        grand_total += round_total;
    });
    std::vector<std::jthread> workers;
    for (int id = 0; id != n; ++id) {
        workers.emplace_back([&, id] {
            for (int r = 0; r != rounds; ++r) {
                partial[id] = (id + 1) * (r + 1);    // 本轮贡献
                sync_point.arrive_and_wait();        // 到齐后完成函数先跑，然后全队放行
                std::osyncstream(std::cout)
                    << "第 " << r + 1 << " 轮：worker " << id
                    << " 看到 round_total = " << round_total << '\n';
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "两轮合计 grand_total = " << grand_total << "（期望 30）\n";
}

// 场景 5a：资源池——2 个座位 4 个用户，限流由计数本身承担
void semaphore_pool_demo()
{
    constexpr int permits = 2;
    constexpr int users = 4;
    std::counting_semaphore<permits> seats{permits};
    std::atomic<int> in_use{0};
    std::atomic<int> peak{0};
    std::vector<std::jthread> holders;
    for (int id = 0; id != users; ++id) {
        holders.emplace_back([id, &seats, &in_use, &peak] {
            seats.acquire();    // 没空位就阻塞：不烧 CPU
            int now = in_use.fetch_add(1) + 1;
            int seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now)) {
            }
            std::osyncstream(std::cout) << "用户 " << id << " 入座，在场 " << now << '\n';
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            in_use.fetch_sub(1);
            seats.release();    // 离开设卡：空位 +1，等的人被唤醒
            std::osyncstream(std::cout) << "用户 " << id << " 离开\n";
        });
    }
    for (auto& t : holders) {
        t.join();
    }
    std::cout << "同时在库峰值 = " << peak.load() << "（上限 " << permits << "）\n";
}

// 场景 5b：0 初始化的信号语义——release 与 acquire 可以落在线程两端
void semaphore_signal_demo()
{
    std::binary_semaphore signal{0};    // 0 初始化：纯信号，没有存量
    std::jthread producer([&signal] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        signal.release();               // 发信号的是这个线程
    });
    signal.acquire();                   // 收信号的是 main：跨线程合法
    std::cout << "main 收到了信号\n";

    auto t0 = std::chrono::steady_clock::now();
    bool got = signal.try_acquire_for(std::chrono::milliseconds(50));
    auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
    std::cout << "try_acquire_for(50ms) 拿到 = " << got
              << "，实际等了 " << waited << "ms\n";
}

// 场景 6：shared_mutex——读档多人同场，独占档一次一人
void shared_mutex_demo()
{
    constexpr int n = 2;
    std::shared_mutex m;
    int protected_value = 7;
    std::atomic<int> readers_inside{0};   // 只做计数展示，原子的正式规则归 ch03
    std::barrier phase(n + 1, []() noexcept {});

    std::vector<std::jthread> readers;
    for (int id = 0; id != n; ++id) {
        readers.emplace_back([&, id] {
            std::shared_lock lk(m);    // 读档：多人可同时在场
            int inside = readers_inside.fetch_add(1) + 1;
            std::osyncstream(std::cout) << "读者 " << id << " 进入，在读 " << inside << " 人\n";
            phase.arrive_and_wait();    // 第 1 轮：两个读者与 main 都到位
            phase.arrive_and_wait();    // 第 2 轮：等 main 探完独占档再撤
        });                             // 作用域结束：shared_lock 放锁
    }
    phase.arrive_and_wait();            // 此刻两个读者都持着读锁
    bool got = m.try_lock();
    if (got) {
        m.unlock();
    }
    std::cout << "两读者在场时独占档 try_lock 拿到 = " << got << "（期望 0）\n";
    phase.arrive_and_wait();            // 放读者们出去
    for (auto& t : readers) {
        t.join();
    }
    bool got2 = m.try_lock();
    if (got2) {
        ++protected_value;
        m.unlock();
    }
    std::cout << "读者全撤后独占档 try_lock 拿到 = " << got2
              << "，protected_value = " << protected_value << "（期望 8）\n";
}

// 场景 7：分片锁——N 把锁摊开竞争，读路径走读档
template <std::size_t N>
class ShardedCounter {
public:
    void add(std::size_t key, long delta)
    {
        Shard& s = shards_[key % N];    // key 映射到固定分片
        std::lock_guard<std::shared_mutex> lk(s.m);
        s.value += delta;
    }

    long total() const
    {
        long sum = 0;
        for (const Shard& s : shards_) {
            std::shared_lock<std::shared_mutex> lk(s.m);   // 逐片读，不挡别片的写
            sum += s.value;
        }
        return sum;
    }

private:
    struct Shard {
        mutable std::shared_mutex m;    // total() 是 const，锁要 mutable
        long value = 0;
    };
    std::array<Shard, N> shards_{};
};

void sharded_demo()
{
    constexpr std::size_t shards = 8;
    constexpr int threads = 8;
    constexpr int ops_per_thread = 10000;
    ShardedCounter<shards> counter;
    std::vector<std::jthread> workers;
    for (int id = 0; id != threads; ++id) {
        workers.emplace_back([id, &counter] {
            for (int i = 0; i != ops_per_thread; ++i) {
                counter.add(static_cast<std::size_t>((i * 8 + id) % 32), 1);
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    std::cout << "分片计数总数 = " << counter.total()
              << "（期望 " << threads * ops_per_thread << "）\n";
}

int main()
{
    std::cout << "== 特性宏 ==\n";
    ftm_probe();
    std::cout << "\n== call_once：失败的尝试不算数 ==\n";
    call_once_demo();
    std::cout << "\n== Meyers 单例 ==\n";
    meyers_demo();
    std::cout << "\n== latch：等全到齐 ==\n";
    latch_demo();
    std::cout << "\n== barrier：完成函数替全队聚合 ==\n";
    barrier_demo();
    std::cout << "\n== semaphore：资源池限流 ==\n";
    semaphore_pool_demo();
    std::cout << "\n== semaphore：跨线程信号与超时 ==\n";
    semaphore_signal_demo();
    std::cout << "\n== shared_mutex：读档与独占档 ==\n";
    shared_mutex_demo();
    std::cout << "\n== 分片锁 ==\n";
    sharded_demo();
    return 0;
}
