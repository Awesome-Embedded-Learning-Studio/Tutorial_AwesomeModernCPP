// ch02/01《mutex 与 RAII 守卫》完整演示
//
// 编译：
//   g++ -std=c++20 -Wall -Wextra -pedantic -pthread -o 01_mutex_and_raii_guards 01_mutex_and_raii_guards.cpp
//   （想看无名守卫的 [[nodiscard]] 警告，加 -DSHOW_UNNAMED_GUARD_TRAP 再编一份）
//
// 演示矩阵：
//   1) 手动 lock/unlock 的两条漏锁路径（提前 return / 抛异常），用另一个线程的 try_lock 取证
//      —— 同一线程对已持有的 mutex 调 try_lock 是未定义行为，取证必须换线程
//   2) lock_guard 修复：计数器竞态收敛 + 临界区抛异常后锁仍被释放
//   3) 无名守卫两代坑（默认不编译，-DSHOW_UNNAMED_GUARD_TRAP 打开）：
//      花括号形式构造临时对象，语句末析构，锁当场白拿，GCC 13+ 有 [[nodiscard]] 警告兜底
//   4) unique_lock 三种构造 tag：defer_lock / try_to_lock / adopt_lock，owns_lock() 逐一验明
//   5) unique_lock 的提前 unlock 与所有权移动
//   6) scoped_lock 一条语句拿两把（转账）+ std::lock + adopt_lock 的等价写法
//   7) ThreadSafeQueue 黑盒：push 用 lock_guard 讲透；pop 为什么能等人，cv 篇（ch02/04）收编
//   8) 裸 cout 交错 vs osyncstream 整行

#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <syncstream>
#include <thread>
#include <utility>
#include <vector>

// 用另一个线程探一把锁是否仍被占用：try_lock 成功说明锁是空闲的。
// 谁拿的锁谁放，所以探针线程自己在 lambda 里配平。
bool probe_locked(std::mutex& m)
{
    bool got = false;
    std::jthread probe([&] {
        got = m.try_lock();
        if (got) {
            m.unlock();
        }
    });
    probe.join();    // 必须等探针干完活再读 got，不然读到的是初始值
    return !got;     // true = 锁仍被原线程攥着
}

// ---------------------------------------------------------------------------
// 1) 手动 lock/unlock 的两条漏锁路径
// ---------------------------------------------------------------------------
void demo_manual_unlock_leaks()
{
    std::cout << "\n== 1) 手动 lock/unlock：两条漏锁路径 ==\n";

    // 路径 A：提前 return，写在后面的 unlock 被跳过
    std::mutex early_ret_m;
    auto early_return_path = [&early_ret_m] {
        early_ret_m.lock();
        if (true) {
            return;  // 漏：unlock 在 return 之后，永远执行不到
        }
        early_ret_m.unlock();
    };
    early_return_path();
    const bool leaked_a = probe_locked(early_ret_m);
    std::cout << "提前 return 后锁仍被持有: " << leaked_a << '\n';
    early_ret_m.unlock();  // 锁的主人还是本线程，自己放才合法

    // 路径 B：临界区里抛异常，手动 unlock 没机会跑
    std::mutex throw_m;
    try {
        throw_m.lock();
        throw std::runtime_error("临界区里出了事");
        // throw_m.unlock();  // 写了也到不了
    } catch (const std::exception& e) {
        std::cout << "异常被接住: " << e.what() << '\n';
    }
    const bool leaked_b = probe_locked(throw_m);
    std::cout << "异常路径后锁仍被持有: " << leaked_b << '\n';
    throw_m.unlock();  // 同上，本线程自救
}

// ---------------------------------------------------------------------------
// 2) lock_guard 修复：异常安全由析构兜底
// ---------------------------------------------------------------------------
void demo_lock_guard_fixes()
{
    std::cout << "\n== 2) lock_guard：锁的生老病死跟着作用域 ==\n";

    // 2a. 计数器竞态收敛
    long counter = 0;
    std::mutex counter_m;
    auto bump = [&] {
        std::lock_guard<std::mutex> lk(counter_m);
        ++counter;
    };
    std::vector<std::jthread> ts;
    for (int i = 0; i != 4; ++i) {
        ts.emplace_back([&] {
            for (int k = 0; k != 100000; ++k) {
                bump();
            }
        });
    }
    for (auto& t : ts) {
        t.join();
    }
    std::cout << "4 线程 x 100000 次自增, 结果 = " << counter
              << "（期望 400000）\n";

    // 2b. 临界区抛异常，析构照样放锁
    std::mutex throw_m;
    try {
        std::lock_guard<std::mutex> lk(throw_m);
        throw std::runtime_error("守卫还在，异常先走");
    } catch (const std::exception& e) {
        std::cout << "异常被接住: " << e.what() << '\n';
    }
    std::cout << "异常过后锁已被释放: " << !probe_locked(throw_m) << '\n';
}

// ---------------------------------------------------------------------------
// 3) 无名守卫两代坑（默认不参与编译，-DSHOW_UNNAMED_GUARD_TRAP 打开）
// ---------------------------------------------------------------------------
#ifdef SHOW_UNNAMED_GUARD_TRAP
void demo_unnamed_guard_trap()
{
    std::cout << "\n== 3) 无名守卫：花括号形式，锁当场白拿 ==\n";
    std::mutex mtx;
    std::lock_guard<std::mutex>{mtx};  // 临时对象！语句末析构，锁已释放
    // 编译这一行时，GCC 13+ 会给 [[nodiscard]] 警告：
    //   warning: ignoring return value of 'std::lock_guard<_Mutex>::lock_guard(mutex_type&)',
    //            declared with attribute 'nodiscard' [-Wunused-result]
    std::cout << "下一行用另一个线程探锁，free = " << !probe_locked(mtx)
              << "（1 = 锁确实白拿了）\n";
}
#endif

// ---------------------------------------------------------------------------
// 4) unique_lock 三种构造 tag
// ---------------------------------------------------------------------------
void demo_unique_lock_tags()
{
    std::cout << "\n== 4) unique_lock：defer / try / adopt 三种拿法 ==\n";

    // defer_lock：只包装，不锁
    std::mutex m;
    {
        std::unique_lock<std::mutex> lk(m, std::defer_lock);
        std::cout << "defer_lock 构造后 owns_lock = " << lk.owns_lock() << '\n';
        lk.lock();  // 想锁的时候自己锁
        std::cout << "lock() 之后 owns_lock = " << lk.owns_lock() << '\n';
    }

    // adopt_lock：锁已经在手里（手动拿的），把看管责任接过来
    {
        std::mutex m2;
        m2.lock();  // 手动拿锁
        std::unique_lock<std::mutex> lk(m2, std::adopt_lock);
        std::cout << "adopt_lock 构造后 owns_lock = " << lk.owns_lock()
                  << "（析构时替我们放）\n";
    }

    // try_to_lock：摸一下，拿不到不强求（拿不到时同线程再试是未定义行为，换线程演示）
    {
        std::mutex m3;
        m3.lock();  // 主线程先占住
        bool child_got = false;
        std::jthread t([&] {
            std::unique_lock<std::mutex> lk(m3, std::try_to_lock);
            child_got = lk.owns_lock();
        });
        t.join();
        std::cout << "主线程持锁时, 子线程 try_to_lock 拿到 = " << child_got
                  << '\n';
        m3.unlock();
    }
}

// ---------------------------------------------------------------------------
// 5) unique_lock 的提前 unlock 与所有权移动
// ---------------------------------------------------------------------------
void demo_unique_lock_flexibility()
{
    std::cout << "\n== 5) unique_lock：中途放锁、把锁带走 ==\n";

    std::mutex m;
    int shared = 0;
    std::unique_lock<std::mutex> lk(m);
    ++shared;             // 临界区：改共享数据
    lk.unlock();          // 提前放：后面的活不碰共享数据，没必要占着锁
    std::cout << "提前 unlock 后 owns_lock = " << lk.owns_lock() << '\n';
    const int snapshot = shared;  // 临界区外读快照（演示用，非并发安全范式）
    lk.lock();            // 还能再拿回来
    --shared;
    std::cout << "拿回来后 owns_lock = " << lk.owns_lock()
              << ", snapshot = " << snapshot << '\n';

    // 所有权移动：锁跟着 unique_lock 走，原来的壳变空
    std::unique_lock<std::mutex> handed_off = std::move(lk);
    std::cout << "移动后原对象 owns_lock = " << lk.owns_lock()
              << ", 新对象 owns_lock = " << handed_off.owns_lock() << '\n';
    // 交给别的函数/线程保管，正是 cv 等待要借的能力（悬念在正文篇尾）
}

// ---------------------------------------------------------------------------
// 6) scoped_lock 一次拿两把 + std::lock/adopt_lock 等价写法
// ---------------------------------------------------------------------------
struct Account {
    long balance = 1000;
    std::mutex m;
};

// 写法一：scoped_lock 一条语句拿两把（内部按 std::lock 的死锁避免算法拿）
void transfer_scoped(Account& from, Account& to, long amount)
{
    std::scoped_lock lk(from.m, to.m);
    from.balance -= amount;
    to.balance += amount;
}

// 写法二：std::lock 先抓，再用 adopt_lock 的守卫接管看管（C++17 之前只有这条路）
void transfer_std_lock(Account& from, Account& to, long amount)
{
    std::lock(from.m, to.m);
    std::lock_guard<std::mutex> lk1(from.m, std::adopt_lock);
    std::lock_guard<std::mutex> lk2(to.m, std::adopt_lock);
    from.balance -= amount;
    to.balance += amount;
}

void demo_scoped_lock_transfer()
{
    std::cout << "\n== 6) 两把锁的转账：scoped_lock 与 std::lock ==\n";

    Account a;
    Account b;
    const long before = a.balance + b.balance;

    // 两个方向对转：拿锁顺序相反，靠一次拿两把避免僵持（为什么免疫，死锁篇细说）
    std::jthread t1([&] {
        for (int i = 0; i != 10000; ++i) {
            transfer_scoped(a, b, 1);
        }
    });
    std::jthread t2([&] {
        for (int i = 0; i != 10000; ++i) {
            transfer_std_lock(b, a, 1);
        }
    });
    t1.join();
    t2.join();

    std::cout << "对转 2 万次后总额不变: " << (a.balance + b.balance == before)
              << "（a = " << a.balance << ", b = " << b.balance << "）\n";
}

// ---------------------------------------------------------------------------
// 7) ThreadSafeQueue 黑盒：push 是本篇的活，pop 是 cv 篇的债
// ---------------------------------------------------------------------------
template <typename T>
class ThreadSafeQueue {
public:
    void push(T value)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            q_.push_back(std::move(value));
        }              // 临界区到此为止：锁在花括号处归还
        cv_.notify_one();  // 通知放在锁外，为什么，收编时讲
    }

    // 黑盒成员：为什么 pop 能「等人」（队列空时不忙等、不空转），
    // 答案在 ch02/04 condition_variable 一篇，此处只管用。
    T pop()
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return !q_.empty(); });
        T value = std::move(q_.front());
        q_.pop_front();
        return value;
    }

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<T> q_;
};

void demo_thread_safe_queue()
{
    std::cout << "\n== 7) ThreadSafeQueue：push 讲透，pop 先当黑盒 ==\n";

    ThreadSafeQueue<int> q;
    long sum = 0;
    std::jthread consumer([&] {
        for (int i = 0; i != 8; ++i) {
            sum += q.pop();  // 队列空的时候，这里能等人
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "消费者已在 pop 里等了 100ms（没有忙转）\n";
    for (int i = 1; i <= 8; ++i) {
        q.push(i);
    }
    consumer.join();
    std::cout << "消费者取完 1..8, sum = " << sum << "（期望 36）\n";
}

// ---------------------------------------------------------------------------
// 8) 裸 cout 交错 vs osyncstream 整行
// ---------------------------------------------------------------------------
void demo_cout_interleaving()
{
    std::cout << "\n== 8) 裸 cout 可能交错, osyncstream 整行交付 ==\n";

    std::cout << "-- 裸 cout（输出可能交错, 每次跑都不一样）--\n";
    {
        std::vector<std::jthread> ts;
        for (int id = 0; id != 4; ++id) {
            ts.emplace_back([id] {
                for (int r = 0; r != 3; ++r) {
                    // 一条语句是多次 operator<< 调用，交错单位是「一次调用」
                    std::cout << "writer " << id << " round " << r << '\n';
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }
    }

    std::cout << "-- osyncstream（同一行内不再被别的线程插进来）--\n";
    {
        std::vector<std::jthread> ts;
        for (int id = 0; id != 4; ++id) {
            ts.emplace_back([id] {
                for (int r = 0; r != 3; ++r) {
                    std::osyncstream(std::cout)
                        << "writer " << id << " round " << r << '\n';
                }
            });
        }
        for (auto& t : ts) {
            t.join();
        }
    }
}

int main()
{
    demo_manual_unlock_leaks();
    demo_lock_guard_fixes();
#ifdef SHOW_UNNAMED_GUARD_TRAP
    demo_unnamed_guard_trap();
#endif
    demo_unique_lock_tags();
    demo_unique_lock_flexibility();
    demo_scoped_lock_transfer();
    demo_thread_safe_queue();
    demo_cout_interleaving();
    return 0;
}
