// 防线二：scoped_lock 一次拿两把——传参顺序相反也免疫
#include <iostream>
#include <mutex>
#include <thread>

struct Account {
    long balance = 1000;
    mutable std::mutex m;
};

void transfer(Account& from, Account& to, long amount) {
    std::scoped_lock lk(from.m, to.m); // 一条语句，死锁避免算法接管
    from.balance -= amount;
    to.balance += amount;
}

int main() {
    Account a;
    Account b;
    std::thread t1([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(a, b, 1); // 传参顺序 a, b
        }
    });
    std::thread t2([&] {
        for (int i = 0; i != 20000; ++i) {
            transfer(b, a, 1); // 传参顺序 b, a：顺序反了也不僵
        }
    });
    t1.join();
    t2.join();
    std::cout << "总额 = " << a.balance + b.balance << "（期望 2000）\n";
    return 0;
}
