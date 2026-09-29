// ch02/03《死锁与现场诊断》完整演示
//
// 编译：
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread -g -O0 -o 03_deadlock_and_gdb 03_deadlock_and_gdb.cpp
//
// 用法（场景名作为第一个参数）：
//   ./03_deadlock_and_gdb deadlock   # AB-BA 现场：两行打印后挂住，配 timeout 或 gdb 用
//   ./03_deadlock_and_gdb order      # 防线一：总锁序，两个线程都按 A、B 的顺序拿
//   ./03_deadlock_and_gdb scoped     # 防线二：scoped_lock 一次拿两把，传参顺序相反也免疫
//   ./03_deadlock_and_gdb trylock    # 防线三：try_lock 回退，第二把摸不到就全放重试
//   ./03_deadlock_and_gdb hierarchy  # 层级锁：合法下行 + 越级抛异常 + 新线程层级独立
//
// gdb 验尸剧本（deadlock 场景）：
//   gdb ./03_deadlock_and_gdb
//   (gdb) run deadlock          # 挂住后按 Ctrl-C 停住现场
//   (gdb) info threads          # 找停在 pthread_mutex_lock 的线程
//   (gdb) thread apply all bt   # 对帧上的 <mtx_a>/<mtx_b> 符号连环
//   (gdb) thread 3              # 切到卡住的线程单独看栈
// 附注：正文的最小现场程序是独立的 deadlock.cpp（35 行，行号与文中 gdb 会话一致），
//       本文件把它收成 scenario_deadlock()，行为相同。

#include <chrono>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// 现场：AB-BA 死锁（thread1 持 A 等 B，thread2 持 B 等 A）
// ---------------------------------------------------------------------------
std::mutex mtx_a;
std::mutex mtx_b;

void scenario_deadlock()
{
    std::cout << "== 现场：AB-BA（两行打印后挂住）==\n";

    auto thread1 = [] {
        std::lock_guard<std::mutex> a(mtx_a);
        std::cout << "t1: 拿到 A，伸手等 B\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> b(mtx_b);   // 等 B：B 在 t2 手里
        std::cout << "t1: 两把都到手\n";
    };
    auto thread2 = [] {
        std::lock_guard<std::mutex> b(mtx_b);
        std::cout << "t2: 拿到 B，伸手等 A\n" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> a(mtx_a);   // 等 A：A 在 t1 手里
        std::cout << "t2: 两把都到手\n";
    };

    std::thread t1(thread1);
    std::thread t2(thread2);
    t1.join();   // 死锁时两行 join 都回不来
    t2.join();
    std::cout << "程序正常收尾（死锁时到不了这里）\n";
}

// ---------------------------------------------------------------------------
// 防线一：总锁序——全工程统一按 A、B 的顺序拿，循环等待无从形成
// ---------------------------------------------------------------------------
void scenario_order()
{
    std::cout << "== 防线一：总锁序 ==\n";

    auto worker = [](int id) {
        for (int round = 0; round != 4; ++round) {
            std::lock_guard<std::mutex> a(mtx_a);   // 谁都先拿 A
            std::lock_guard<std::mutex> b(mtx_b);   // 再拿 B
            std::cout << "worker " << id << " round " << round << ": 两把都到手\n";
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
}

// ---------------------------------------------------------------------------
// 防线二：scoped_lock 一次拿两把——死锁避免算法接管，顺序传反也不僵
// ---------------------------------------------------------------------------
struct Account {
    long balance = 1000;
    mutable std::mutex m;
};

void transfer(Account& from, Account& to, long amount)
{
    std::scoped_lock lk(from.m, to.m);   // 一条语句拿两把，析构全放
    from.balance -= amount;
    to.balance += amount;
}

void scenario_scoped()
{
    std::cout << "== 防线二：scoped_lock ==\n";

    Account a;
    Account b;
    std::thread t1([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(a, b, 1);   // 传参顺序 a, b
        }
    });
    std::thread t2([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(b, a, 1);   // 传参顺序 b, a：顺序反了也不僵
        }
    });
    t1.join();
    t2.join();
    std::cout << "总额 = " << a.balance + b.balance << "（期望 2000）\n";
}

// ---------------------------------------------------------------------------
// 防线三：try_lock 回退——第二把摸不到就把头一把也放掉，退避后重试
// ---------------------------------------------------------------------------
void scenario_trylock()
{
    std::cout << "== 防线三：try_lock 回退 ==\n";

    auto worker = [](int id) {
        for (int round = 0; round != 100000; ++round) {
            while (true) {
                std::unique_lock<std::mutex> a(mtx_a, std::defer_lock);
                if (!a.try_lock()) {
                    std::this_thread::yield();   // 头一把就没摸到，让一让再来
                    continue;
                }
                std::unique_lock<std::mutex> b(mtx_b, std::defer_lock);
                if (b.try_lock()) {
                    break;   // 两把都在手：进临界区
                }
                std::this_thread::yield();   // 第二把没摸到：a 随析构放掉，空手回去
            }
            // 临界区干活的活儿省略：a、b 一直看管到本轮结束
        }
        std::cout << "worker " << id << " done\n";
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();
    std::cout << "程序正常收尾\n";
}

// ---------------------------------------------------------------------------
// 层级锁：thread_local 记录本线程当前层级，越级上锁当场抛异常
// （ch02/02 结尾留下的钩子在这里兑现：每线程一份的层级状态）
// ---------------------------------------------------------------------------
class HierarchicalMutex {
public:
    explicit HierarchicalMutex(unsigned long level)
        : level_(level)
    {
    }

    void lock()
    {
        check_violation();
        internal_.lock();
        push_level();
    }

    void unlock()
    {
        current_level_ = previous_level_;   // 回到拿下这把锁之前的层级
        internal_.unlock();
    }

    bool try_lock()
    {
        check_violation();
        if (!internal_.try_lock()) {
            return false;
        }
        push_level();
        return true;
    }

private:
    void check_violation()
    {
        if (level_ >= current_level_) {
            throw std::logic_error("锁层级越级：当前线程已持有同级或更低层级的锁");
        }
    }

    void push_level()
    {
        previous_level_ = current_level_;
        current_level_ = level_;
    }

    std::mutex internal_;
    const unsigned long level_;
    unsigned long previous_level_ = 0;
    static thread_local unsigned long current_level_;
};

thread_local unsigned long HierarchicalMutex::current_level_
    = std::numeric_limits<unsigned long>::max();

HierarchicalMutex high_mutex(10000);   // 应用层
HierarchicalMutex mid_mutex(5000);     // 业务层
HierarchicalMutex low_mutex(100);      // 底层 IO

void scenario_hierarchy()
{
    std::cout << "== 层级锁 ==\n";

    // 合法路径：高 -> 中 -> 低，一路下行
    {
        std::lock_guard<HierarchicalMutex> h(high_mutex);
        std::lock_guard<HierarchicalMutex> m(mid_mutex);
        std::lock_guard<HierarchicalMutex> l(low_mutex);
        std::cout << "下行加锁：一路顺利\n";
    }

    // 越级路径：在低层里回头够中层，当场抛异常
    try {
        std::lock_guard<HierarchicalMutex> l(low_mutex);
        std::lock_guard<HierarchicalMutex> m(mid_mutex);   // 5000 >= 100：违规
    } catch (const std::logic_error& e) {
        std::cout << "抓到越级: " << e.what() << '\n';
    }

    // 层级状态是 thread_local 的：上一个线程的状态不会漏到别的线程
    std::thread fresh([] {
        std::lock_guard<HierarchicalMutex> m(mid_mutex);   // 新线程直接拿中层：合法
        std::cout << "新线程直接拿中层：合法\n";
    });
    fresh.join();
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    const std::string scenario = argc > 1 ? argv[1] : "deadlock";
    if (scenario == "deadlock") {
        scenario_deadlock();
    } else if (scenario == "order") {
        scenario_order();
    } else if (scenario == "scoped") {
        scenario_scoped();
    } else if (scenario == "trylock") {
        scenario_trylock();
    } else if (scenario == "hierarchy") {
        scenario_hierarchy();
    } else {
        std::cout << "未知场景: " << scenario
                  << "（可用: deadlock order scoped trylock hierarchy）\n";
        return 1;
    }
    return 0;
}
