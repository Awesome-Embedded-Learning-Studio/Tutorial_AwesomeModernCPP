// unique_lock 的提前 unlock 与所有权移动：owns_lock 随时报告手里有没有锁，锁的看管权可以整个移动走
#include <iostream>
#include <mutex>
#include <utility>

int main() {
    std::mutex m;
    int shared = 0;
    std::unique_lock<std::mutex> lk(m);
    ++shared;    // 临界区：改共享数据
    lk.unlock(); // 提前放：后面的活不碰共享数据，没必要占着锁
    std::cout << "提前 unlock 后 owns_lock = " << lk.owns_lock() << '\n';
    const int snapshot = shared; // 临界区外读快照（演示用，非并发安全范式）
    lk.lock();                   // 还能再拿回来
    --shared;
    std::cout << "拿回来后 owns_lock = " << lk.owns_lock() << ", snapshot = " << snapshot << '\n';

    // 所有权移动：锁跟着 unique_lock 走，原来的壳变空
    std::unique_lock<std::mutex> handed_off = std::move(lk);
    std::cout << "移动后原对象 owns_lock = " << lk.owns_lock()
              << ", 新对象 owns_lock = " << handed_off.owns_lock() << '\n';
    return 0;
}
